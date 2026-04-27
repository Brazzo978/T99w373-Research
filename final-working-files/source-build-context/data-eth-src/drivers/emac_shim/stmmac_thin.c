// SPDX-License-Identifier: GPL-2.0-only
/*******************************************************************************
 * This is the driver for the ST MAC 10/100/1000 on-chip Ethernet controllers.
 * ST Ethernet IPs are built around a Synopsys IP Core.
 *
 * Copyright(C) 2007-2011 STMicroelectronics Ltd
 *
 * Author: Giuseppe Cavallaro <peppe.cavallaro@st.com>
 *
 * Documentation available at:
 * http://www.stlinux.com
 * Support available at:
 * https://bugzilla.stlinux.com/
 ******************************************************************************/

#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/skbuff.h>
#include <linux/ethtool.h>
#include <linux/if_ether.h>
#include <linux/crc32.h>
#include <linux/if.h>
#include <linux/if_vlan.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/prefetch.h>
#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#endif /* CONFIG_DEBUG_FS */
#include <linux/net_tstamp.h>

#include <net/pkt_cls.h>
#include "stmmac_thin.h"
#include <linux/reset.h>

#include "hwif_thin.h"
#include "dwmac4_thin.h"

#define CREATE_TRACE_POINTS
#include "stmmac_trace.h"

#define	TSO_MAX_BUFF_SIZE	(SZ_16K - 1)

/* Module parameters */
#define TX_TIMEO	5000
static int watchdog = TX_TIMEO;
static int debug = -1;

#define STMMAC_TX_THRESH	(DMA_TX_SIZE / 4)
#define STMMAC_RX_THRESH	(DMA_RX_SIZE / 4)

#define TC_DEFAULT 64
static int tc = TC_DEFAULT;

#define	DEFAULT_BUFSIZE	1536
static int buf_sz = DEFAULT_BUFSIZE;

#define	STMMAC_RX_COPYBREAK	256

static const u32 default_msg_level = (NETIF_MSG_DRV | NETIF_MSG_PROBE |
				      NETIF_MSG_LINK | NETIF_MSG_IFUP |
				      NETIF_MSG_IFDOWN | NETIF_MSG_TIMER);

static irqreturn_t stmmac_interrupt(int irq, void *dev_id);

#ifdef CONFIG_DEBUG_FS
static int stmmac_init_fs(struct net_device *dev);
static void stmmac_exit_fs(struct net_device *dev);
#endif

#define STMMAC_COAL_TIMER(x) (jiffies + usecs_to_jiffies(x))

/**
 * stmmac_verify_args - verify the driver parameters.
 * Description: it checks the driver parameters and set a default in case of
 * errors.
 */
static void stmmac_verify_args(void)
{
	if (unlikely(watchdog < 0))
		watchdog = TX_TIMEO;
	if (unlikely(buf_sz < DEFAULT_BUFSIZE || buf_sz > BUF_SIZE_16KiB))
		buf_sz = DEFAULT_BUFSIZE;
}

/**
 * stmmac_disable_queue - Disable queue
 * @priv: driver private structure
 */
static void stmmac_disable_queue(struct stmmac_priv *priv)
{
	struct stmmac_channel *ch = &priv->channel;

	napi_disable(&ch->rx_napi);
	napi_disable(&ch->tx_napi);
}

/**
 * stmmac_enable_queue - Enable queue
 * @priv: driver private structure
 */
static void stmmac_enable_queue(struct stmmac_priv *priv)
{
	struct stmmac_channel *ch = &priv->channel;

	napi_enable(&ch->rx_napi);
	napi_enable(&ch->tx_napi);
}

/**
 * stmmac_stop_queues - Stop queue
 * @priv: driver private structure
 */
static void stmmac_stop_queue(struct stmmac_priv *priv)
{
	netif_tx_stop_queue(netdev_get_tx_queue(priv->dev, priv->queue));
}

/**
 * stmmac_start_queue - Start queue
 * @priv: driver private structure
 */
static void stmmac_start_queue(struct stmmac_priv *priv)
{
	netif_tx_start_queue(netdev_get_tx_queue(priv->dev, priv->queue));
}

static void stmmac_service_event_schedule(struct stmmac_priv *priv)
{
	if (!test_bit(STMMAC_DOWN, &priv->state) &&
	    !test_and_set_bit(STMMAC_SERVICE_SCHED, &priv->state))
		queue_work(priv->wq, &priv->service_task);
}

static void stmmac_global_err(struct stmmac_priv *priv)
{
	netif_carrier_off(priv->dev);
	set_bit(STMMAC_RESET_REQUESTED, &priv->state);
	stmmac_service_event_schedule(priv);
}

static void print_pkt(unsigned char *buf, int len)
{
	pr_debug("len = %d byte, buf addr: 0x%p\n", len, buf);
	print_hex_dump_bytes("", DUMP_PREFIX_OFFSET, buf, len);
}

static inline u32 stmmac_tx_avail(struct stmmac_priv *priv)
{
	struct stmmac_tx_queue *tx_q = &priv->tx_queue;
	u32 avail;

	if (tx_q->dirty_tx > tx_q->cur_tx)
		avail = tx_q->dirty_tx - tx_q->cur_tx - 1;
	else
		avail = DMA_TX_SIZE - tx_q->cur_tx + tx_q->dirty_tx - 1;

	return avail;
}

/**
 * stmmac_rx_dirty - Get RX queue dirty
 * @priv: driver private structure
 * @queue: RX queue index
 */
static inline u32 stmmac_rx_dirty(struct stmmac_priv *priv)
{
	struct stmmac_rx_queue *rx_q = &priv->rx_queue;
	u32 dirty;

	if (rx_q->dirty_rx <= rx_q->cur_rx)
		dirty = rx_q->cur_rx - rx_q->dirty_rx;
	else
		dirty = DMA_RX_SIZE - rx_q->dirty_rx + rx_q->cur_rx;

	return dirty;
}

/* stmmac_get_tx_hwtstamp - get HW TX timestamps
 * @priv: driver private structure
 * @p : descriptor pointer
 * @skb : the socket buffer
 * Description :
 * This function will read timestamp from the descriptor & pass it to stack.
 * and also perform some sanity checks.
 */
static void stmmac_get_tx_hwtstamp(struct stmmac_priv *priv,
				   struct dma_desc *p, struct sk_buff *skb)
{
	struct skb_shared_hwtstamps shhwtstamp;
	bool found = false;
	u64 ns = 0;

	if (!priv->hwts_tx_en)
		return;

	/* exit if skb doesn't support hw tstamp */
	if (likely(!skb || !(skb_shinfo(skb)->tx_flags & SKBTX_IN_PROGRESS)))
		return;

	/* check tx tstamp status */
	if (stmmac_get_tx_timestamp_status(priv, p)) {
		stmmac_get_timestamp(priv, p, &ns);
		found = true;
	}

	if (found) {
		memset(&shhwtstamp, 0, sizeof(struct skb_shared_hwtstamps));
		shhwtstamp.hwtstamp = ns_to_ktime(ns);

		netdev_dbg(priv->dev, "get valid TX hw timestamp %llu\n", ns);
		/* pass tstamp to stack */
		skb_tstamp_tx(skb, &shhwtstamp);
	}
}

/* stmmac_get_rx_hwtstamp - get HW RX timestamps
 * @priv: driver private structure
 * @p : descriptor pointer
 * @np : next descriptor pointer
 * @skb : the socket buffer
 * Description :
 * This function will read received packet's timestamp from the descriptor
 * and pass it to stack. It also perform some sanity checks.
 */
static void stmmac_get_rx_hwtstamp(struct stmmac_priv *priv, struct dma_desc *p,
				   struct dma_desc *np, struct sk_buff *skb)
{
	struct skb_shared_hwtstamps *shhwtstamp = NULL;
	struct dma_desc *desc = np;
	u64 ns = 0;

	if (!priv->hwts_rx_en)
		return;

	/* Check if timestamp is available */
	if (stmmac_get_rx_timestamp_status(priv, p, np)) {
		stmmac_get_timestamp(priv, desc, &ns);
		netdev_dbg(priv->dev, "get valid RX hw timestamp %llu\n", ns);
		shhwtstamp = skb_hwtstamps(skb);
		memset(shhwtstamp, 0, sizeof(struct skb_shared_hwtstamps));
		shhwtstamp->hwtstamp = ns_to_ktime(ns);
	} else  {
		netdev_dbg(priv->dev, "cannot get RX hw timestamp\n");
	}
}

static int stmmac_set_bfsize(int mtu, int bufsize)
{
	int ret = bufsize;

	if (mtu >= BUF_SIZE_8KiB)
		ret = BUF_SIZE_16KiB;
	else if (mtu >= BUF_SIZE_4KiB)
		ret = BUF_SIZE_8KiB;
	else if (mtu >= BUF_SIZE_2KiB)
		ret = BUF_SIZE_4KiB;
	else if (mtu > DEFAULT_BUFSIZE)
		ret = BUF_SIZE_2KiB;
	else
		ret = DEFAULT_BUFSIZE;

	return ret;
}

/**
 * stmmac_clear_rx_descriptors - clear RX descriptors
 * @priv: driver private structure
 * Description: this function is called to clear the RX descriptors
 * in case of both basic and extended descriptors are used.
 */
static void stmmac_clear_rx_descriptors(struct stmmac_priv *priv)
{
	struct stmmac_rx_queue *rx_q = &priv->rx_queue;
	int i;

	/* Clear the RX descriptors */
	for (i = 0; i < DMA_RX_SIZE; i++)
		stmmac_init_rx_desc(priv, &rx_q->dma_rx[i],
				    priv->use_riwt, priv->mode,
				    (i == DMA_RX_SIZE - 1),
				    priv->dma_buf_sz);
}

/**
 * stmmac_clear_tx_descriptors - clear tx descriptors
 * @priv: driver private structure
 * Description: this function is called to clear the TX descriptors
 * in case of both basic and extended descriptors are used.
 */
static void stmmac_clear_tx_descriptors(struct stmmac_priv *priv)
{
	struct stmmac_tx_queue *tx_q = &priv->tx_queue;
	int i;

	/* Clear the TX descriptors */
	for (i = 0; i < DMA_TX_SIZE; i++)
		stmmac_init_tx_desc(priv, &tx_q->dma_tx[i],
				    priv->mode, (i == DMA_TX_SIZE - 1));
}

/**
 * stmmac_clear_descriptors - clear descriptors
 * @priv: driver private structure
 * Description: this function is called to clear the TX and RX descriptors
 * in case of both basic and extended descriptors are used.
 */
static void stmmac_clear_descriptors(struct stmmac_priv *priv)
{
	/* Clear the RX descriptors */
	stmmac_clear_rx_descriptors(priv);

	/* Clear the TX descriptors */
	stmmac_clear_tx_descriptors(priv);
}

/**
 * stmmac_init_rx_buffers - init the RX descriptor buffer.
 * @priv: driver private structure
 * @p: descriptor pointer
 * @i: descriptor index
 * @flags: gfp flag
 * Description: this function is called to allocate a receive buffer, perform
 * the DMA mapping and init the descriptor.
 */
static int stmmac_init_rx_buffers(struct stmmac_priv *priv, struct dma_desc *p,
				  int i, gfp_t flags)
{
	struct stmmac_rx_queue *rx_q = &priv->rx_queue;
	struct stmmac_rx_buffer *buf = &rx_q->buf_pool[i];

	buf->page = page_pool_dev_alloc_pages(rx_q->page_pool);
	if (!buf->page)
		return -ENOMEM;

	buf->addr = page_pool_get_dma_addr(buf->page);
	stmmac_set_desc_addr(priv, p, buf->addr);

	return 0;
}

/**
 * stmmac_free_rx_buffer - free RX dma buffers
 * @priv: private structure
 * @i: buffer index.
 */
static void stmmac_free_rx_buffer(struct stmmac_priv *priv, int i)
{
	struct stmmac_rx_queue *rx_q = &priv->rx_queue;
	struct stmmac_rx_buffer *buf = &rx_q->buf_pool[i];

	if (buf->page)
		page_pool_put_page(rx_q->page_pool, buf->page, false);
	buf->page = NULL;
}

/**
 * stmmac_free_tx_buffer - free RX dma buffers
 * @priv: private structure
 * @i: buffer index.
 */
static void stmmac_free_tx_buffer(struct stmmac_priv *priv, int i)
{
	struct stmmac_tx_queue *tx_q = &priv->tx_queue;

	if (tx_q->tx_skbuff_dma[i].buf) {
		if (tx_q->tx_skbuff_dma[i].map_as_page)
			dma_unmap_page(GET_MEM_PDEV_DEV,
				       tx_q->tx_skbuff_dma[i].buf,
				       tx_q->tx_skbuff_dma[i].len,
				       DMA_TO_DEVICE);
		else
			dma_unmap_single(GET_MEM_PDEV_DEV,
					 tx_q->tx_skbuff_dma[i].buf,
					 tx_q->tx_skbuff_dma[i].len,
					 DMA_TO_DEVICE);
	}

	if (tx_q->tx_skbuff[i]) {
		dev_kfree_skb_any(tx_q->tx_skbuff[i]);
		tx_q->tx_skbuff[i] = NULL;
		tx_q->tx_skbuff_dma[i].buf = 0;
		tx_q->tx_skbuff_dma[i].map_as_page = false;
	}
}

/**
 * init_dma_rx_desc_rings - init the RX descriptor rings
 * @dev: net device structure
 * @flags: gfp flag.
 * Description: this function initializes the DMA RX descriptors
 * and allocates the socket buffers. It supports the chained and ring
 * modes.
 */
static int init_dma_rx_desc_rings(struct net_device *dev, gfp_t flags)
{
	struct stmmac_priv *priv = netdev_priv(dev);
	struct stmmac_rx_queue *rx_q = &priv->rx_queue;
	int ret = -ENOMEM;
	int i;

	/* RX INITIALIZATION */
	netif_dbg(priv, probe, priv->dev,
		  "SKB addresses:\nskb\t\tskb data\tdma data\n");

	netif_dbg(priv, probe, priv->dev, "(%s) dma_rx_phy=0x%08x\n",
		  __func__, (u32)rx_q->dma_rx_phy);

	stmmac_clear_rx_descriptors(priv);

	for (i = 0; i < DMA_RX_SIZE; i++) {
		struct dma_desc *p;

		p = rx_q->dma_rx + i;

		ret = stmmac_init_rx_buffers(priv, p, i, flags);
		if (ret)
			goto err_init_rx_buffers;
	}

	rx_q->cur_rx = 0;
	rx_q->dirty_rx = (unsigned int)(i - DMA_RX_SIZE);

	return 0;

err_init_rx_buffers:
	while (--i >= 0)
		stmmac_free_rx_buffer(priv, i);

	return ret;
}

/**
 * init_dma_tx_desc_rings - init the TX descriptor rings
 * @dev: net device structure.
 * Description: this function initializes the DMA TX descriptors
 * and allocates the socket buffers. It supports the chained and ring
 * modes.
 */
static int init_dma_tx_desc_rings(struct net_device *dev)
{
	struct stmmac_priv *priv = netdev_priv(dev);
	struct stmmac_tx_queue *tx_q = &priv->tx_queue;
	int i;

	netif_dbg(priv, probe, priv->dev, "(%s) dma_tx_phy=0x%08x\n",
		  __func__, (u32)tx_q->dma_tx_phy);

	for (i = 0; i < DMA_TX_SIZE; i++) {
		struct dma_desc *p;

		p = tx_q->dma_tx + i;
		stmmac_clear_desc(priv, p);

		tx_q->tx_skbuff_dma[i].buf = 0;
		tx_q->tx_skbuff_dma[i].map_as_page = false;
		tx_q->tx_skbuff_dma[i].len = 0;
		tx_q->tx_skbuff_dma[i].last_segment = false;
		tx_q->tx_skbuff[i] = NULL;
	}

	tx_q->dirty_tx = 0;
	tx_q->cur_tx = 0;
	tx_q->mss = 0;

	netdev_tx_reset_queue(netdev_get_tx_queue(priv->dev, priv->queue));

	return 0;
}

/**
 * init_dma_desc_rings - init the RX/TX descriptor rings
 * @dev: net device structure
 * @flags: gfp flag.
 * Description: this function initializes the DMA RX/TX descriptors
 * and allocates the socket buffers. It supports the chained and ring
 * modes.
 */
static int init_dma_desc_rings(struct net_device *dev, gfp_t flags)
{
	struct stmmac_priv *priv = netdev_priv(dev);
	int ret;

	ret = init_dma_rx_desc_rings(dev, flags);
	if (ret)
		return ret;

	ret = init_dma_tx_desc_rings(dev);

	stmmac_clear_descriptors(priv);

	return ret;
}

/**
 * dma_free_rx_skbufs - free RX dma buffers
 * @priv: private structure
 */
static void dma_free_rx_skbufs(struct stmmac_priv *priv)
{
	int i;

	for (i = 0; i < DMA_RX_SIZE; i++)
		stmmac_free_rx_buffer(priv, i);
}

/**
 * dma_free_tx_skbufs - free TX dma buffers
 * @priv: private structure
 */
static void dma_free_tx_skbufs(struct stmmac_priv *priv)
{
	int i;

	for (i = 0; i < DMA_TX_SIZE; i++)
		stmmac_free_tx_buffer(priv, i);
}

/**
 * free_dma_rx_desc_resources - free RX dma desc resources
 * @priv: private structure
 */
static void free_dma_rx_desc_resources(struct stmmac_priv *priv)
{
	/* Free RX queue resources */
	struct stmmac_rx_queue *rx_q = &priv->rx_queue;

	/* Release the DMA RX socket buffers */
	dma_free_rx_skbufs(priv);

	/* Free DMA regions of consistent memory previously allocated */
	dma_free_coherent(GET_MEM_PDEV_DEV,
			  DMA_RX_SIZE * sizeof(struct dma_desc),
			  rx_q->dma_rx, rx_q->dma_rx_phy);

	kfree(rx_q->buf_pool);
	if (rx_q->page_pool)
		page_pool_destroy(rx_q->page_pool);
}

/**
 * free_dma_tx_desc_resources - free TX dma desc resources
 * @priv: private structure
 */
static void free_dma_tx_desc_resources(struct stmmac_priv *priv)
{
	/* Free TX queue resources */
	struct stmmac_tx_queue *tx_q = &priv->tx_queue;

	/* Release the DMA TX socket buffers */
	dma_free_tx_skbufs(priv);

	/* Free DMA regions of consistent memory previously allocated */
	dma_free_coherent(GET_MEM_PDEV_DEV,
			  DMA_TX_SIZE * sizeof(struct dma_desc),
			  tx_q->dma_tx, tx_q->dma_tx_phy);

	kfree(tx_q->tx_skbuff_dma);
	kfree(tx_q->tx_skbuff);
}

/**
 * alloc_dma_rx_desc_resources - alloc RX resources.
 * @priv: private structure
 * Description: according to which descriptor can be used (extend or basic)
 * this function allocates the resources for TX and RX paths. In case of
 * reception, for example, it pre-allocated the RX socket buffer in order to
 * allow zero-copy mechanism.
 */
static int alloc_dma_rx_desc_resources(struct stmmac_priv *priv)
{
	int ret = -ENOMEM;
	struct stmmac_rx_queue *rx_q = &priv->rx_queue;
	struct page_pool_params pp_params = { 0 };
	unsigned int num_pages;

	/* RX queues buffers and DMA */
	rx_q->priv_data = priv;

	pp_params.flags = PP_FLAG_DMA_MAP;
	pp_params.pool_size = DMA_RX_SIZE;
	num_pages = DIV_ROUND_UP(priv->dma_buf_sz, PAGE_SIZE);
	pp_params.order = ilog2(num_pages);
	pp_params.nid = dev_to_node(GET_MEM_PDEV_DEV);
	pp_params.dev = GET_MEM_PDEV_DEV;
	pp_params.dma_dir = DMA_FROM_DEVICE;

	rx_q->page_pool = page_pool_create(&pp_params);
	if (IS_ERR(rx_q->page_pool)) {
		ret = PTR_ERR(rx_q->page_pool);
		rx_q->page_pool = NULL;
		goto err_dma;
	}

	rx_q->buf_pool = kcalloc(DMA_RX_SIZE, sizeof(*rx_q->buf_pool),
				 GFP_KERNEL);
	if (!rx_q->buf_pool)
		goto err_dma;

	rx_q->dma_rx = dma_alloc_coherent(GET_MEM_PDEV_DEV,
					  DMA_RX_SIZE *
					  sizeof(struct dma_desc),
					  &rx_q->dma_rx_phy,
					  GFP_KERNEL);
	if (!rx_q->dma_rx) {
		netdev_err(priv->dev, "%s: dma_alloc_coherent failed\n",
			   __func__);
		goto err_dma;
	}
	return 0;

err_dma:
	free_dma_rx_desc_resources(priv);

	return ret;
}

/**
 * alloc_dma_tx_desc_resources - alloc TX resources.
 * @priv: private structure
 * Description: according to which descriptor can be used (extend or basic)
 * this function allocates the resources for TX and RX paths. In case of
 * reception, for example, it pre-allocated the RX socket buffer in order to
 * allow zero-copy mechanism.
 */
static int alloc_dma_tx_desc_resources(struct stmmac_priv *priv)
{
	int ret = -ENOMEM;
	struct stmmac_tx_queue *tx_q = &priv->tx_queue;

	/* TX queues buffers and DMA */
	tx_q->priv_data = priv;

	tx_q->tx_skbuff_dma = kcalloc(DMA_TX_SIZE,
				      sizeof(*tx_q->tx_skbuff_dma),
				      GFP_KERNEL);
	if (!tx_q->tx_skbuff_dma)
		goto err_dma;

	tx_q->tx_skbuff = kcalloc(DMA_TX_SIZE,
				  sizeof(struct sk_buff *),
				  GFP_KERNEL);
	if (!tx_q->tx_skbuff)
		goto err_dma;

	tx_q->dma_tx = dma_alloc_coherent(GET_MEM_PDEV_DEV,
					  DMA_TX_SIZE *
					  sizeof(struct dma_desc),
					  &tx_q->dma_tx_phy,
					  GFP_KERNEL);
	if (!tx_q->dma_tx)
		goto err_dma;

	return 0;

err_dma:
	free_dma_tx_desc_resources(priv);

	return ret;
}

/**
 * alloc_dma_desc_resources - alloc TX/RX resources.
 * @priv: private structure
 * Description: according to which descriptor can be used (extend or basic)
 * this function allocates the resources for TX and RX paths. In case of
 * reception, for example, it pre-allocated the RX socket buffer in order to
 * allow zero-copy mechanism.
 */
static int alloc_dma_desc_resources(struct stmmac_priv *priv)
{
	/* RX Allocation */
	int ret = alloc_dma_rx_desc_resources(priv);

	if (ret)
		return ret;

	ret = alloc_dma_tx_desc_resources(priv);

	return ret;
}

/**
 * free_dma_desc_resources - free dma desc resources
 * @priv: private structure
 */
static void free_dma_desc_resources(struct stmmac_priv *priv)
{
	/* Release the DMA RX socket buffers */
	free_dma_rx_desc_resources(priv);

	/* Release the DMA TX socket buffers */
	free_dma_tx_desc_resources(priv);
}

/**
 * stmmac_start_rx_dma - start RX DMA channel
 * @priv: driver private structure
 * @chan: RX channel index
 * Description:
 * This starts a RX DMA channel
 */
static void stmmac_start_rx_dma(struct stmmac_priv *priv, u32 chan)
{
	netdev_dbg(priv->dev, "DMA RX processes started in channel %d\n",
		   chan);
	stmmac_start_rx(priv, priv->ioaddr, chan);
}

/**
 * stmmac_start_tx_dma - start TX DMA channel
 * @priv: driver private structure
 * @chan: TX channel index
 * Description:
 * This starts a TX DMA channel
 */
static void stmmac_start_tx_dma(struct stmmac_priv *priv, u32 chan)
{
	netdev_dbg(priv->dev, "DMA TX processes started in channel %d\n",
		   chan);
	stmmac_start_tx(priv, priv->ioaddr, chan);
}

/**
 * stmmac_stop_rx_dma - stop RX DMA channel
 * @priv: driver private structure
 * @chan: RX channel index
 * Description:
 * This stops a RX DMA channel
 */
static void stmmac_stop_rx_dma(struct stmmac_priv *priv, u32 chan)
{
	netdev_dbg(priv->dev, "DMA RX processes stopped in channel %d\n",
		   chan);
	stmmac_stop_rx(priv, priv->ioaddr, chan);
}

/**
 * stmmac_stop_tx_dma - stop TX DMA channel
 * @priv: driver private structure
 * @chan: TX channel index
 * Description:
 * This stops a TX DMA channel
 */
static void stmmac_stop_tx_dma(struct stmmac_priv *priv, u32 chan)
{
	netdev_dbg(priv->dev, "DMA TX processes stopped in channel %d\n",
		   chan);
	stmmac_stop_tx(priv, priv->ioaddr, chan);
}

/**
 * stmmac_start_dma - start RX and TX DMA channel
 * @priv: driver private structure
 * Description:
 * This starts the RX and TX DMA channel
 */
static void stmmac_start_dma(struct stmmac_priv *priv)
{
	stmmac_start_rx_dma(priv, priv->queue);

	stmmac_start_tx_dma(priv, priv->queue);
}

/**
 * stmmac_stop_all_dma - stop RX and TX DMA channel
 * @priv: driver private structure
 * Description:
 * This stops the RX and TX DMA channel
 */
static void stmmac_stop_dma(struct stmmac_priv *priv)
{
	stmmac_stop_rx_dma(priv, priv->queue);

	stmmac_stop_tx_dma(priv, priv->queue);
}

/**
 *  stmmac_dma_operation_mode - HW DMA operation mode
 *  @priv: driver private structure
 *  Description: it is used for configuring the DMA operation mode register in
 *  order to program the tx/rx DMA thresholds or Store-And-Forward mode.
 */
static void stmmac_dma_operation_mode(struct stmmac_priv *priv)
{
	int rxfifosz = priv->plat->rx_fifo_size;
	int txfifosz = priv->plat->tx_fifo_size;
	u32 txmode = 0;
	u32 rxmode = 0;
	u32 chan = priv->queue;
	u8 qmode = 0;

	if (priv->plat->force_thresh_dma_mode) {
		txmode = tc;
		rxmode = tc;
	} else if (priv->plat->force_sf_dma_mode || priv->plat->tx_coe) {
		/* In case of GMAC, SF mode can be enabled
		 * to perform the TX COE in HW. This depends on:
		 * 1) TX COE if actually supported
		 * 2) There is no bugged Jumbo frame support
		 *    that needs to not insert csum in the TDES.
		 */
		txmode = SF_DMA_MODE;
		rxmode = SF_DMA_MODE;
		priv->xstats.threshold = SF_DMA_MODE;
	} else {
		txmode = tc;
		rxmode = SF_DMA_MODE;
	}
	pr_info("%s: txmode [%u], rxmode [%u], txfifosz [%u], rxfifosz [%u]\n",
		__func__, txmode, rxmode, txfifosz, rxfifosz);
	/* configure channel */
	qmode = priv->plat->rx_queues_cfg[chan].mode_to_use;
	stmmac_dma_rx_mode(priv, priv->ioaddr, rxmode, chan, rxfifosz, qmode);
	stmmac_set_dma_bfsize(priv, priv->ioaddr, priv->dma_buf_sz, chan);

	qmode = priv->plat->tx_queues_cfg[chan].mode_to_use;
	stmmac_dma_tx_mode(priv, priv->ioaddr, txmode, chan, txfifosz, qmode);
}

/**
 * stmmac_tx_clean - to manage the transmission completion
 * @priv: driver private structure
 * @queue: TX queue index
 * Description: it reclaims the transmit resources after transmission completes.
 */
static int stmmac_tx_clean(struct stmmac_priv *priv, int budget, u32 queue)
{
	struct stmmac_tx_queue *tx_q = &priv->tx_queue;
	unsigned int bytes_compl = 0, pkts_compl = 0;
	unsigned int entry, count = 0;

	if (queue != priv->queue)
		return count;

	__netif_tx_lock_bh(netdev_get_tx_queue(priv->dev, queue));

	priv->xstats.tx_clean++;

	entry = tx_q->dirty_tx;
	while ((entry != tx_q->cur_tx) && (count < budget)) {
		struct sk_buff *skb = tx_q->tx_skbuff[entry];
		struct dma_desc *p;
		int status;

		p = tx_q->dma_tx + entry;

		status = stmmac_tx_status(priv, &priv->dev->stats,
					  &priv->xstats, p, priv->ioaddr);
		/* Check if the descriptor is owned by the DMA */
		if (unlikely(status & tx_dma_own))
			break;

		count++;

		/* Make sure descriptor fields are read after reading
		 * the own bit.
		 */
		dma_rmb();

		/* Just consider the last segment and ...*/
		if (likely(!(status & tx_not_ls))) {
			/* ... verify the status error condition */
			if (unlikely(status & tx_err)) {
				priv->dev->stats.tx_errors++;
			} else {
				priv->dev->stats.tx_packets++;
				priv->xstats.tx_pkt_n++;
				priv->xstats.q_tx_pkt_n[queue]++;
#ifdef CONFIG_QGKI_MSM_BOOT_TIME_MARKER
if (priv->dev->stats.tx_packets == 1)
	place_marker("M - Ethernet first packet transmitted");
#endif
			}
			stmmac_get_tx_hwtstamp(priv, p, skb);
		}

		if (likely(tx_q->tx_skbuff_dma[entry].buf)) {
			if (tx_q->tx_skbuff_dma[entry].map_as_page)
				dma_unmap_page(GET_MEM_PDEV_DEV,
					       tx_q->tx_skbuff_dma[entry].buf,
					       tx_q->tx_skbuff_dma[entry].len,
					       DMA_TO_DEVICE);
			else
				dma_unmap_single(GET_MEM_PDEV_DEV,
						 tx_q->tx_skbuff_dma[entry].buf,
						 tx_q->tx_skbuff_dma[entry].len,
						 DMA_TO_DEVICE);
			tx_q->tx_skbuff_dma[entry].buf = 0;
			tx_q->tx_skbuff_dma[entry].len = 0;
			tx_q->tx_skbuff_dma[entry].map_as_page = false;
		}

		tx_q->tx_skbuff_dma[entry].last_segment = false;

		if (likely(skb)) {
			pkts_compl++;
			bytes_compl += skb->len;
			dev_consume_skb_any(skb);
			tx_q->tx_skbuff[entry] = NULL;
		}

		stmmac_release_tx_desc(priv, p, priv->mode);

		entry = STMMAC_GET_ENTRY(entry, DMA_TX_SIZE);
	}
	tx_q->dirty_tx = entry;

	if (!priv->tx_coal_timer_disable)
		netdev_tx_completed_queue(netdev_get_tx_queue(priv->dev, queue),
					  pkts_compl, bytes_compl);

	if (unlikely(netif_tx_queue_stopped(netdev_get_tx_queue(priv->dev,
								queue))) &&
	    stmmac_tx_avail(priv) > STMMAC_TX_THRESH) {
		netif_dbg(priv, tx_done, priv->dev,
			  "%s: restart transmit\n", __func__);
		netif_tx_wake_queue(netdev_get_tx_queue(priv->dev, queue));
	}

	if (!priv->tx_coal_timer_disable) {
		/**
		 * We still have pending packets,
		 * let's call for a new scheduling
		 */
		if (tx_q->dirty_tx != tx_q->cur_tx)
			mod_timer(&tx_q->txtimer, STMMAC_COAL_TIMER(10));
	}

	__netif_tx_unlock_bh(netdev_get_tx_queue(priv->dev, queue));

	return count;
}

/**
 * stmmac_tx_err - to manage the tx error
 * @priv: driver private structure
 * @chan: channel index
 * Description: it cleans the descriptors and restarts the transmission
 * in case of transmission errors.
 */
static void stmmac_tx_err(struct stmmac_priv *priv, u32 chan)
{
	struct stmmac_tx_queue *tx_q = &priv->tx_queue;
	int i;

	netif_tx_stop_queue(netdev_get_tx_queue(priv->dev, chan));

	stmmac_stop_tx_dma(priv, chan);
	dma_free_tx_skbufs(priv);
	for (i = 0; i < DMA_TX_SIZE; i++)
		stmmac_init_tx_desc(priv, &tx_q->dma_tx[i],
				    priv->mode, (i == DMA_TX_SIZE - 1));
	tx_q->dirty_tx = 0;
	tx_q->cur_tx = 0;
	tx_q->mss = 0;
	netdev_tx_reset_queue(netdev_get_tx_queue(priv->dev, chan));
	stmmac_start_tx_dma(priv, chan);

	priv->dev->stats.tx_errors++;
	netif_tx_wake_queue(netdev_get_tx_queue(priv->dev, chan));
}

/**
 *  stmmac_set_dma_operation_mode - Set DMA operation mode by channel
 *  @priv: driver private structure
 *  @txmode: TX operating mode
 *  @rxmode: RX operating mode
 *  @chan: channel index
 *  Description: it is used for configuring of the DMA operation mode in
 *  runtime in order to program the tx/rx DMA thresholds or Store-And-Forward
 *  mode.
 */
static void stmmac_set_dma_operation_mode(struct stmmac_priv *priv, u32 txmode,
					  u32 rxmode, u32 chan)
{
	u8 rxqmode = priv->plat->rx_queues_cfg[chan].mode_to_use;
	u8 txqmode = priv->plat->tx_queues_cfg[chan].mode_to_use;
	int rxfifosz = priv->plat->rx_fifo_size;
	int txfifosz = priv->plat->tx_fifo_size;

	stmmac_dma_rx_mode(priv, priv->ioaddr, rxmode, chan, rxfifosz, rxqmode);
	stmmac_dma_tx_mode(priv, priv->ioaddr, txmode, chan, txfifosz, txqmode);
}

static int stmmac_napi_check(struct stmmac_priv *priv, u32 chan)
{
	int status = stmmac_dma_interrupt_status(priv, priv->ioaddr,
						 &priv->xstats, chan);
	struct stmmac_channel *ch = &priv->channel;

	trace_stmmac_irq_status(chan, status);
	if (status & handle_rx) {
		if (napi_schedule_prep(&ch->rx_napi)) {
			trace_stmmac_disable_irq(chan);
			stmmac_disable_dma_irq(priv, priv->ioaddr, chan);
			__napi_schedule_irqoff(&ch->rx_napi);
			status |= handle_tx;
		}
	}

	if (status & handle_tx)
		napi_schedule_irqoff(&ch->tx_napi);

	return status;
}

/**
 * stmmac_dma_interrupt - DMA ISR
 * @priv: driver private structure
 * Description: this is the DMA ISR. It is called by the main ISR.
 * It calls the dwmac dma routine and schedule poll method in case of some
 * work can be done.
 */
static void stmmac_dma_interrupt(struct stmmac_priv *priv)
{
	u32 chan = priv->queue;
	int status;

	status = stmmac_napi_check(priv, chan);

	if (unlikely(status & tx_hard_error_bump_tc)) {
		/* Try to bump up the dma threshold on this failure */
		if (unlikely(priv->xstats.threshold != SF_DMA_MODE) &&
		    tc <= 256) {
			tc += 64;
			if (priv->plat->force_thresh_dma_mode)
				stmmac_set_dma_operation_mode(priv,
							      tc,
							      tc,
							      chan);
			else
				stmmac_set_dma_operation_mode(priv,
							      tc,
							      SF_DMA_MODE,
							      chan);
			priv->xstats.threshold = tc;
		}
	} else if (unlikely(status == tx_hard_error)) {
		stmmac_tx_err(priv, chan);
	}
}

/**
 * stmmac_init_dma_engine - DMA init.
 * @priv: driver private structure
 * Description:
 * It inits the DMA invoking the specific MAC/GMAC callback.
 * Some DMA parameters can be passed from the platform;
 * in case of these are not passed a default is kept for the MAC or GMAC.
 */
static int stmmac_init_dma_engine(struct stmmac_priv *priv)
{
	struct stmmac_rx_queue *rx_q = &priv->rx_queue;
	struct stmmac_tx_queue *tx_q = &priv->tx_queue;
	u32 chan = priv->queue;

	if (!priv->plat->dma_cfg || !priv->plat->dma_cfg->pbl) {
		dev_err(priv->device, "Invalid DMA configuration\n");
		return -EINVAL;
	}

	/* DMA CSR Channel configuration */
	stmmac_init_chan(priv, priv->ioaddr, priv->plat->dma_cfg, chan);

	/* DMA RX Channel Configuration */
	stmmac_init_rx_chan(priv, priv->ioaddr, priv->plat->dma_cfg,
			    rx_q->dma_rx_phy, chan);

	rx_q->rx_tail_addr = rx_q->dma_rx_phy +
			    (DMA_RX_SIZE * sizeof(struct dma_desc));
	stmmac_set_rx_tail_ptr(priv, priv->ioaddr,
			       rx_q->rx_tail_addr, chan);

	/* DMA TX Channel Configuration */
	stmmac_init_tx_chan(priv, priv->ioaddr, priv->plat->dma_cfg,
			    tx_q->dma_tx_phy, chan);

	tx_q->tx_tail_addr = tx_q->dma_tx_phy;
	stmmac_set_tx_tail_ptr(priv, priv->ioaddr,
			       tx_q->tx_tail_addr, chan);

	return 0;
}

static void stmmac_tx_timer_arm(struct stmmac_priv *priv)
{
	struct stmmac_tx_queue *tx_q = &priv->tx_queue;

	mod_timer(&tx_q->txtimer, STMMAC_COAL_TIMER(priv->tx_coal_timer));
}

/**
 * stmmac_tx_timer - mitigation sw timer for tx.
 * @data: data pointer
 * Description:
 * This is the timer handler to directly invoke the stmmac_tx_clean.
 */
static void stmmac_tx_timer(struct timer_list *t)
{
	struct stmmac_tx_queue *tx_q = from_timer(tx_q, t, txtimer);
	struct stmmac_priv *priv = tx_q->priv_data;
	struct stmmac_channel *ch;

	ch = &priv->channel;

	/* If NAPI is already running we can miss some events. Let's rearm
	 * the timer and try again.
	 */
	if (likely(napi_schedule_prep(&ch->tx_napi)))
		__napi_schedule(&ch->tx_napi);
	else
		mod_timer(&tx_q->txtimer, STMMAC_COAL_TIMER(10));
}

/**
 * stmmac_init_coalesce - init mitigation options.
 * @priv: driver private structure
 * Description:
 * This inits the coalesce parameters: i.e. timer rate,
 * timer handler and default threshold used for enabling the
 * interrupt on completion bit.
 */
static void stmmac_init_coalesce(struct stmmac_priv *priv)
{
	struct stmmac_tx_queue *tx_q = &priv->tx_queue;

	priv->tx_coal_frames = STMMAC_TX_FRAMES;
	priv->tx_coal_timer = STMMAC_COAL_TX_TIMER;
	priv->rx_coal_frames = STMMAC_RX_FRAMES;

	timer_setup(&tx_q->txtimer, stmmac_tx_timer, 0);
}

static void stmmac_set_rings_length(struct stmmac_priv *priv)
{
	u32 chan = priv->queue;

	/* set TX ring length */
	stmmac_set_tx_ring_len(priv, priv->ioaddr, (DMA_TX_SIZE - 1), chan);

	/* set RX ring length */
	stmmac_set_rx_ring_len(priv, priv->ioaddr, (DMA_RX_SIZE - 1), chan);
}

/**
 *  stmmac_set_tx_queue_weight - Set TX queue weight
 *  @priv: driver private structure
 *  Description: It is used for setting TX queues weight
 */
static void stmmac_set_tx_queue_weight(struct stmmac_priv *priv)
{
	u32 weight;

	weight = priv->plat->tx_queues_cfg[priv->queue].weight;
	stmmac_set_mtl_tx_queue_weight(priv, priv->hw, weight, priv->queue);
}

/**
 * stmmac_hw_setup - setup mac in a usable state.
 *  @dev : pointer to the device structure.
 *  Description:
 *  this is the main function to setup the HW in a usable state because the
 *  dma engine is reset, the core registers are configured. The DMA is ready
 *  to start receiving and transmitting.
 *  Return value:
 *  0 on success and an appropriate (-)ve integer as defined in errno.h
 *  file on failure.
 */
static int stmmac_hw_setup(struct net_device *dev)
{
	struct stmmac_priv *priv = netdev_priv(dev);
	u32 chan = priv->queue;
	int ret;

	dev_info(priv->device, "%s: ch = %u\n", __func__, chan);
	priv->mac_addr(dev);

	/* DMA initialization and SW reset */
	ret = stmmac_init_dma_engine(priv);
	if (ret < 0) {
		netdev_err(priv->dev, "%s: DMA engine initialization failed\n",
			   __func__);
		return ret;
	}

	/* Initialize queue*/
	stmmac_set_tx_queue_weight(priv);

	/* Set the HW DMA mode and the COE */
	stmmac_dma_operation_mode(priv);

	if (priv->use_riwt) {
		ret = stmmac_rx_watchdog(priv, priv->ioaddr,
					 MIN_DMA_RIWT, chan);
		if (!ret)
			priv->rx_riwt = MIN_DMA_RIWT;
	}

	/* set TX and RX rings length */
	stmmac_set_rings_length(priv);

	/* Enable TSO */
	if (priv->tso)
		stmmac_enable_tso(priv, priv->ioaddr, 1, chan);

	/* Start the ball rolling... */
	stmmac_start_dma(priv);

	return 0;
}

/**
 *  stmmac_open - open entry point of the driver
 *  @dev : pointer to the device structure.
 *  Description:
 *  This function is the open entry point of the driver.
 *  Return value:
 *  0 on success and an appropriate (-)ve integer as defined in errno.h
 *  file on failure.
 */
static int stmmac_open(struct net_device *dev)
{
	struct stmmac_priv *priv = netdev_priv(dev);
	int bfsize = 0;
	int ret = 0;

#ifdef CONFIG_QGKI_MSM_BOOT_TIME_MARKER
	place_marker("M - Ethernet device open");
#endif
	/* Extra statistics */
	dev_info(priv->device, "%s: emac_state = %u\n",
		 __func__, priv->emac_state);
	memset(&priv->xstats, 0, sizeof(struct stmmac_extra_stats));
	priv->xstats.threshold = tc;

	bfsize = stmmac_set_bfsize(dev->mtu, priv->dma_buf_sz);

	priv->dma_buf_sz = bfsize;
	buf_sz = bfsize;

	priv->rx_copybreak = STMMAC_RX_COPYBREAK;
	priv->dev_opened = true;
	priv->dev_inited = false;

	if (priv->emac_state > EMAC_INIT_ST)
		ret = stmmac_dvr_init(dev);
	if (!ret && priv->emac_state == EMAC_LINK_UP_ST)
		stmmac_mac_link_up(dev);

	dev_info(priv->device, "%s: ret = %d\n", __func__, ret);
	return ret;
}

/**
 *  stmmac_release - close entry point of the driver
 *  @dev : device pointer.
 *  Description:
 *  This is the stop entry point of the driver.
 */
static int stmmac_release(struct net_device *dev)
{
	struct stmmac_priv *priv = netdev_priv(dev);

	dev_info(priv->device, "%s Enter\n", __func__);
	priv->dev_inited = false;
	priv->dev_opened = false;

	stmmac_stop_queue(priv);

	stmmac_disable_queue(priv);

	if (!priv->tx_coal_timer_disable)
		del_timer_sync(&priv->tx_queue.txtimer);

	/* Free the IRQ line */
	free_irq(dev->irq, dev);

	/* Stop TX/RX DMA and clear the descriptors */
	stmmac_stop_dma(priv);

	/* Release and free the Rx/Tx resources */
	free_dma_desc_resources(priv);

	netif_carrier_off(dev);

	return 0;
}

static bool stmmac_vlan_insert(struct stmmac_priv *priv, struct sk_buff *skb,
			       struct stmmac_tx_queue *tx_q)
{
	u16 tag = 0x0, inner_tag = 0x0;
	u32 inner_type = 0x0;
	struct dma_desc *p;

	if (!skb_vlan_tag_present(skb))
		return false;
	if (skb->vlan_proto == htons(ETH_P_8021AD)) {
		inner_tag = skb_vlan_tag_get(skb);
		inner_type = STMMAC_VLAN_INSERT;
	}

	tag = skb_vlan_tag_get(skb);

	p = tx_q->dma_tx + tx_q->cur_tx;
	if (stmmac_set_desc_vlan_tag(priv, p, tag, inner_tag, inner_type))
		return false;

	stmmac_set_tx_owner(priv, p);
	tx_q->cur_tx = STMMAC_GET_ENTRY(tx_q->cur_tx, DMA_TX_SIZE);
	return true;
}

/**
 *  stmmac_tso_allocator - close entry point of the driver
 *  @priv: driver private structure
 *  @des: buffer start address
 *  @total_len: total length to fill in descriptors
 *  @last_segmant: condition for the last descriptor
 *  Description:
 *  This function fills descriptor and request new descriptors according to
 *  buffer length to fill
 */
static void stmmac_tso_allocator(struct stmmac_priv *priv, dma_addr_t des,
				 int total_len, bool last_segment)
{
	struct stmmac_tx_queue *tx_q = &priv->tx_queue;
	struct dma_desc *desc;
	u32 buff_size;
	int tmp_len;

	tmp_len = total_len;

	while (tmp_len > 0) {
		dma_addr_t curr_addr;

		tx_q->cur_tx = STMMAC_GET_ENTRY(tx_q->cur_tx, DMA_TX_SIZE);
		WARN_ON(tx_q->tx_skbuff[tx_q->cur_tx]);
		desc = tx_q->dma_tx + tx_q->cur_tx;

		curr_addr = des + (total_len - tmp_len);
		desc->des0 = cpu_to_le32(curr_addr);

		buff_size = tmp_len >= TSO_MAX_BUFF_SIZE ?
			    TSO_MAX_BUFF_SIZE : tmp_len;

		stmmac_prepare_tso_tx_desc
			(priv, desc, 0, buff_size, 0, 1,
			 (last_segment) && (tmp_len <= TSO_MAX_BUFF_SIZE),
			 0, 0);

		tmp_len -= TSO_MAX_BUFF_SIZE;
	}
}

/**
 *  stmmac_tso_xmit - Tx entry point of the driver for oversized frames (TSO)
 *  @skb : the socket buffer
 *  @dev : device pointer
 *  Description: this is the transmit function that is called on TSO frames
 *  (support available on GMAC4 and newer chips).
 *  Diagram below show the ring programming in case of TSO frames:
 *
 *  First Descriptor
 *   --------
 *   | DES0 |---> buffer1 = L2/L3/L4 header
 *   | DES1 |---> TCP Payload (can continue on next descr...)
 *   | DES2 |---> buffer 1 and 2 len
 *   | DES3 |---> must set TSE, TCP hdr len-> [22:19]. TCP payload len [17:0]
 *   --------
 *	|
 *     ...
 *	|
 *   --------
 *   | DES0 | --| Split TCP Payload on Buffers 1 and 2
 *   | DES1 | --|
 *   | DES2 | --> buffer 1 and 2 len
 *   | DES3 |
 *   --------
 *
 * mss is fixed when enable tso, so w/o programming the TDES3 ctx field.
 */
static netdev_tx_t stmmac_tso_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct dma_desc *desc, *first, *mss_desc = NULL;
	struct stmmac_priv *priv = netdev_priv(dev);
	int nfrags = skb_shinfo(skb)->nr_frags;
	u32 queue = skb_get_queue_mapping(skb);
	struct stmmac_tx_queue *tx_q;
	unsigned int first_entry;
	int tmp_pay_len = 0;
	u32 pay_len, mss;
	u8 proto_hdr_len;
	dma_addr_t des;
	bool has_vlan;
	int i;

	tx_q = &priv->tx_queue;

	/* Compute header lengths */
	proto_hdr_len = skb_transport_offset(skb) + tcp_hdrlen(skb);

	/* Desc availability based on threshold should be enough safe */
	if (unlikely(stmmac_tx_avail(priv) <
		(((skb->len - proto_hdr_len) / TSO_MAX_BUFF_SIZE + 1)))) {
		if (!netif_tx_queue_stopped(netdev_get_tx_queue(dev, queue))) {
			netif_tx_stop_queue(netdev_get_tx_queue(priv->dev,
								queue));
			/* This is a hard error, log it. */
			netdev_err(priv->dev,
				   "%s: Tx Ring full when queue awake\n",
				   __func__);
		}
		trace_stmmac_xmit_err(queue);
		return NETDEV_TX_BUSY;
	}

	pay_len = skb_headlen(skb) - proto_hdr_len; /* no frags */

	mss = skb_shinfo(skb)->gso_size;

	/* set new MSS value if needed */
	if (mss != tx_q->mss) {
		mss_desc = tx_q->dma_tx + tx_q->cur_tx;
		stmmac_set_mss(priv, mss_desc, mss);
		tx_q->mss = mss;
		tx_q->cur_tx = STMMAC_GET_ENTRY(tx_q->cur_tx, DMA_TX_SIZE);
		WARN_ON(tx_q->tx_skbuff[tx_q->cur_tx]);
	}

	if (netif_msg_tx_queued(priv)) {
		pr_info("%s: tcphdrlen %d, hdr_len %d, pay_len %d, mss %d\n",
			__func__, tcp_hdrlen(skb), proto_hdr_len, pay_len, mss);
		pr_info("\tskb->len %d, skb->data_len %d\n", skb->len,
			skb->data_len);
	}

	/* Check if VLAN can be inserted by HW */
	has_vlan = stmmac_vlan_insert(priv, skb, tx_q);

	first_entry = tx_q->cur_tx;
	WARN_ON(tx_q->tx_skbuff[first_entry]);

	desc = tx_q->dma_tx + first_entry;
	first = desc;

	if (has_vlan)
		stmmac_set_desc_vlan(priv, first, STMMAC_VLAN_INSERT);

	/* first descriptor: fill Headers on Buf1 */
	des = dma_map_single(GET_MEM_PDEV_DEV, skb->data, skb_headlen(skb),
			     DMA_TO_DEVICE);
	if (dma_mapping_error(GET_MEM_PDEV_DEV, des))
		goto dma_map_err;

	tx_q->tx_skbuff_dma[first_entry].buf = des;
	tx_q->tx_skbuff_dma[first_entry].len = skb_headlen(skb);

	first->des0 = cpu_to_le32(des);

	/* Fill start of payload in buff2 of first descriptor */
	if (pay_len)
		first->des1 = cpu_to_le32(des + proto_hdr_len);

	/* If needed take extra descriptors to fill the remaining payload */
	tmp_pay_len = pay_len - TSO_MAX_BUFF_SIZE;

	stmmac_tso_allocator(priv, des, tmp_pay_len, (nfrags == 0));

	/* Prepare fragments */
	for (i = 0; i < nfrags; i++) {
		const skb_frag_t *frag = &skb_shinfo(skb)->frags[i];

		des = skb_frag_dma_map(GET_MEM_PDEV_DEV, frag, 0,
				       skb_frag_size(frag),
				       DMA_TO_DEVICE);
		if (dma_mapping_error(GET_MEM_PDEV_DEV, des))
			goto dma_map_err;

		stmmac_tso_allocator(priv, des, skb_frag_size(frag),
				     (i == nfrags - 1));

		tx_q->tx_skbuff_dma[tx_q->cur_tx].buf = des;
		tx_q->tx_skbuff_dma[tx_q->cur_tx].len = skb_frag_size(frag);
		tx_q->tx_skbuff_dma[tx_q->cur_tx].map_as_page = true;
	}

	tx_q->tx_skbuff_dma[tx_q->cur_tx].last_segment = true;

	/* Only the last descriptor gets to point to the skb. */
	tx_q->tx_skbuff[tx_q->cur_tx] = skb;

	/* Manage tx mitigation */
	tx_q->tx_count_frames += nfrags + 1;
	if (likely(priv->tx_coal_timer_disable)) {
		tx_q->tx_count_frames = 0;
		stmmac_set_tx_ic(priv, desc);
		priv->xstats.tx_set_ic_bit++;
	} else {
		if (likely(priv->tx_coal_frames > tx_q->tx_count_frames) &&
		    !((skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP) &&
			priv->hwts_tx_en)) {
			stmmac_tx_timer_arm(priv);
		} else {
			tx_q->tx_count_frames = 0;
			stmmac_set_tx_ic(priv, desc);
			priv->xstats.tx_set_ic_bit++;
		}
	}

	/* We've used all descriptors we need for this skb, however,
	 * advance cur_tx so that it references a fresh descriptor.
	 * ndo_start_xmit will fill this descriptor the next time it's
	 * called and stmmac_tx_clean may clean up to this descriptor.
	 */
	tx_q->cur_tx = STMMAC_GET_ENTRY(tx_q->cur_tx, DMA_TX_SIZE);

	if (unlikely(stmmac_tx_avail(priv) <= (MAX_SKB_FRAGS + 1))) {
		netif_dbg(priv, hw, priv->dev, "%s: stop transmitted packets\n",
			  __func__);
		netif_tx_stop_queue(netdev_get_tx_queue(priv->dev, queue));
	}

	dev->stats.tx_bytes += skb->len;
	priv->xstats.tx_tso_frames++;
	priv->xstats.tx_tso_nfrags += nfrags;

	skb_tx_timestamp(skb);

	if (unlikely((skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP) &&
		     priv->hwts_tx_en)) {
		/* declare that device is doing timestamping */
		skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;
		stmmac_enable_tx_timestamp(priv, first);
	}

	/* Complete the first descriptor before granting the DMA */
	stmmac_prepare_tso_tx_desc
		(priv, first, 1, proto_hdr_len, pay_len, 1,
		 tx_q->tx_skbuff_dma[first_entry].last_segment,
		 tcp_hdrlen(skb) / 4, (skb->len - proto_hdr_len));

	/* If context desc is used to change MSS */
	if (mss_desc) {
		/* Make sure that first descriptor has been completely
		 * written, including its own bit. This is because MSS is
		 * actually before first descriptor, so we need to make
		 * sure that MSS's own bit is the last thing written.
		 */
		dma_wmb();
		stmmac_set_tx_owner(priv, mss_desc);
	}

	/* The own bit must be the latest setting done when prepare the
	 * descriptor and then barrier is needed to make sure that
	 * all is coherent before granting the DMA engine.
	 */
	wmb();

	if (netif_msg_pktdata(priv)) {
		pr_info("%s: curr=%d dirty=%d f=%d, e=%d, f_p=%p, nfrags %d\n",
			__func__, tx_q->cur_tx, tx_q->dirty_tx, first_entry,
			tx_q->cur_tx, first, nfrags);

		pr_info(">>> frame to be transmitted:\n");
		print_pkt(skb->data, skb_headlen(skb));
	}

	if (!priv->tx_coal_timer_disable)
		netdev_tx_sent_queue(netdev_get_tx_queue(dev, queue), skb->len);

	tx_q->tx_tail_addr = tx_q->dma_tx_phy + (tx_q->cur_tx * sizeof(*desc));
	stmmac_set_tx_tail_ptr(priv, priv->ioaddr, tx_q->tx_tail_addr, queue);
	if (!priv->tx_coal_timer_disable)
		stmmac_tx_timer_arm(priv);
	trace_stmmac_xmit_exit(queue);

	return NETDEV_TX_OK;

dma_map_err:
	dev_err(priv->device, "Tx dma map failed\n");
	dev_kfree_skb(skb);
	priv->dev->stats.tx_dropped++;
	trace_stmmac_xmit_err(queue);

	return NETDEV_TX_OK;
}

/**
 *  stmmac_xmit - Tx entry point of the driver
 *  @skb : the socket buffer
 *  @dev : device pointer
 *  Description : this is the tx entry point of the driver.
 *  It programs the chain or the ring and supports oversized frames
 *  and SG feature.
 */
static netdev_tx_t stmmac_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct stmmac_priv *priv = netdev_priv(dev);
	unsigned int nopaged_len = skb_headlen(skb);
	int i, csum_insertion = 0;
	u32 queue = skb_get_queue_mapping(skb);
	int nfrags = skb_shinfo(skb)->nr_frags;
	struct dma_desc *desc, *first;
	struct stmmac_tx_queue *tx_q;
	unsigned int first_entry;
	dma_addr_t des;
	bool has_vlan;
	int entry;
	unsigned int int_mod;
	bool last_seg = (nfrags == 0);

	if (queue != priv->queue)
		return NETDEV_TX_OK;

	if (!priv->dev_inited || priv->emac_state != EMAC_LINK_UP_ST) {
		netdev_err(priv->dev, "%s device not ready, emac state %u\n",
			   __func__, priv->emac_state);
		return NETDEV_TX_OK;
	}

	trace_stmmac_xmit_entry(queue);
	tx_q = &priv->tx_queue;

	/* Manage oversized TCP frames for GMAC4 device */
	if (skb_is_gso(skb) && priv->tso) {
		if (skb_shinfo(skb)->gso_type & (SKB_GSO_TCPV4 | SKB_GSO_TCPV6))
			return stmmac_tso_xmit(skb, dev);
	}

	if (unlikely(stmmac_tx_avail(priv) < nfrags + 1)) {
		if (!netif_tx_queue_stopped(netdev_get_tx_queue(dev, queue))) {
			netif_tx_stop_queue(netdev_get_tx_queue(priv->dev,
								queue));
			/* This is a hard error, log it. */
			netdev_err(priv->dev,
				   "%s: Tx Ring full when queue awake\n",
				   __func__);
		}
		trace_stmmac_xmit_err(queue);

		return NETDEV_TX_BUSY;
	}

	/* Check if VLAN can be inserted by HW */
	has_vlan = stmmac_vlan_insert(priv, skb, tx_q);

	entry = tx_q->cur_tx;
	first_entry = entry;
	WARN_ON(tx_q->tx_skbuff[first_entry]);

	csum_insertion = (skb->ip_summed == CHECKSUM_PARTIAL);

	desc = tx_q->dma_tx + entry;

	first = desc;

	if (has_vlan)
		stmmac_set_desc_vlan(priv, first, STMMAC_VLAN_INSERT);

	for (i = 0; i < nfrags; i++) {
		const skb_frag_t *frag = &skb_shinfo(skb)->frags[i];
		int len = skb_frag_size(frag);
		bool last_segment = (i == (nfrags - 1));

		entry = STMMAC_GET_ENTRY(entry, DMA_TX_SIZE);
		WARN_ON(tx_q->tx_skbuff[entry]);

		desc = tx_q->dma_tx + entry;

		des = skb_frag_dma_map(GET_MEM_PDEV_DEV, frag, 0, len,
				       DMA_TO_DEVICE);
		if (dma_mapping_error(GET_MEM_PDEV_DEV, des))
			goto dma_map_err; /* should reuse desc w/o issues */

		tx_q->tx_skbuff_dma[entry].buf = des;

		stmmac_set_desc_addr(priv, desc, des);

		tx_q->tx_skbuff_dma[entry].map_as_page = true;
		tx_q->tx_skbuff_dma[entry].len = len;
		tx_q->tx_skbuff_dma[entry].last_segment = last_segment;

		/* Prepare the descriptor and set the own bit too */
		stmmac_prepare_tx_desc(priv, desc, 0, len, csum_insertion,
				       priv->mode, 1, last_segment, skb->len);
	}

	/* Only the last descriptor gets to point to the skb. */
	tx_q->tx_skbuff[entry] = skb;

	/* According to the coalesce parameter the IC bit for the latest
	 * segment is reset and the timer re-started to clean the tx status.
	 * This approach takes care about the fragments: desc is the first
	 * element in case of no SG.
	 */
	tx_q->tx_count_frames += nfrags + 1;
	if (likely(priv->tx_coal_timer_disable)) {
		if (priv->plat->get_plat_tx_coal_frames) {
			int_mod = priv->plat->get_plat_tx_coal_frames(skb);

			if (!(tx_q->cur_tx % int_mod)) {
				tx_q->tx_count_frames = 0;
				stmmac_set_tx_ic(priv, desc);
				priv->xstats.tx_set_ic_bit++;
			}
		}
	} else {
		if (likely(priv->tx_coal_frames > tx_q->tx_count_frames) &&
		    !((skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP) &&
		    priv->hwts_tx_en)) {
			stmmac_tx_timer_arm(priv);
	} else {
		desc = &tx_q->dma_tx[entry];

		tx_q->tx_count_frames = 0;
		stmmac_set_tx_ic(priv, desc);
		priv->xstats.tx_set_ic_bit++;
	}
	}

	/* We've used all descriptors we need for this skb, however,
	 * advance cur_tx so that it references a fresh descriptor.
	 * ndo_start_xmit will fill this descriptor the next time it's
	 * called and stmmac_tx_clean may clean up to this descriptor.
	 */
	entry = STMMAC_GET_ENTRY(entry, DMA_TX_SIZE);
	tx_q->cur_tx = entry;

	if (netif_msg_pktdata(priv)) {
		netdev_dbg(priv->dev, ">>> frame to be transmitted: ");
		print_pkt(skb->data, skb->len);
	}

	if (unlikely(stmmac_tx_avail(priv) <= (MAX_SKB_FRAGS + 1))) {
		netif_dbg(priv, hw, priv->dev, "%s: stop transmitted packets\n",
			  __func__);
		netif_tx_stop_queue(netdev_get_tx_queue(priv->dev, queue));
	}

	dev->stats.tx_bytes += skb->len;

	if (!priv->hwts_tx_en)
		skb_tx_timestamp(skb);

	/* Ready to fill the first descriptor and set the OWN bit w/o any
	 * problems because all the descriptors are actually ready to be
	 * passed to the DMA engine.
	 */
	des = dma_map_single(GET_MEM_PDEV_DEV, skb->data,
			     nopaged_len, DMA_TO_DEVICE);
	if (dma_mapping_error(GET_MEM_PDEV_DEV, des))
		goto dma_map_err;

	tx_q->tx_skbuff_dma[first_entry].buf = des;

	stmmac_set_desc_addr(priv, first, des);

	tx_q->tx_skbuff_dma[first_entry].len = nopaged_len;
	tx_q->tx_skbuff_dma[first_entry].last_segment = last_seg;

	if (unlikely((skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP) &&
		     priv->hwts_tx_en)) {
		/* declare that device is doing timestamping */
		skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;
		stmmac_enable_tx_timestamp(priv, first);
	}

	/* Prepare the first descriptor setting the OWN bit too */
	stmmac_prepare_tx_desc(priv, first, 1, nopaged_len,
			       csum_insertion, priv->mode, 1,
			       last_seg, skb->len);

	/* The own bit must be the latest setting done when prepare the
	 * descriptor and then barrier is needed to make sure that
	 * all is coherent before granting the DMA engine.
	 */
	wmb();

	if (!priv->tx_coal_timer_disable)
		netdev_tx_sent_queue(netdev_get_tx_queue(dev, queue), skb->len);

	tx_q->tx_tail_addr = tx_q->dma_tx_phy + (tx_q->cur_tx * sizeof(*desc));
	stmmac_set_tx_tail_ptr(priv, priv->ioaddr, tx_q->tx_tail_addr, queue);
	if (!priv->tx_coal_timer_disable)
		stmmac_tx_timer_arm(priv);
	trace_stmmac_xmit_exit(queue);

	return NETDEV_TX_OK;

dma_map_err:
	netdev_err(priv->dev, "Tx DMA map failed\n");
	dev_kfree_skb(skb);
	priv->dev->stats.tx_dropped++;
	trace_stmmac_xmit_err(queue);

	return NETDEV_TX_OK;
}

static void stmmac_rx_vlan(struct net_device *dev, struct sk_buff *skb)
{
	struct vlan_ethhdr *veth;
	__be16 vlan_proto;
	u16 vlanid;

	veth = (struct vlan_ethhdr *)skb->data;
	vlan_proto = veth->h_vlan_proto;

	if ((vlan_proto == htons(ETH_P_8021Q) &&
	     dev->features & NETIF_F_HW_VLAN_CTAG_RX) ||
	    (vlan_proto == htons(ETH_P_8021AD) &&
	     dev->features & NETIF_F_HW_VLAN_STAG_RX)) {
		/* pop the vlan tag */
		vlanid = ntohs(veth->h_vlan_TCI);
		memmove(skb->data + VLAN_HLEN, veth, ETH_ALEN * 2);
		skb_pull(skb, VLAN_HLEN);
		__vlan_hwaccel_put_tag(skb, vlan_proto, vlanid);
	}
}

static inline int stmmac_rx_threshold_count(struct stmmac_rx_queue *rx_q)
{
	if (rx_q->rx_zeroc_thresh < STMMAC_RX_THRESH)
		return 0;

	return 1;
}

/**
 * stmmac_rx_refill - refill used skb preallocated buffers
 * @priv: driver private structure
 * @queue: RX queue index
 * Description : this is to reallocate the skb for the reception process
 * that is based on zero-copy.
 */
static inline void stmmac_rx_refill(struct stmmac_priv *priv, u32 queue)
{
	struct stmmac_rx_queue *rx_q = &priv->rx_queue;
	int len, dirty = stmmac_rx_dirty(priv);
	unsigned int entry = rx_q->dirty_rx;

	len = DIV_ROUND_UP(priv->dma_buf_sz, PAGE_SIZE) * PAGE_SIZE;

	while (dirty-- > 0) {
		struct stmmac_rx_buffer *buf = &rx_q->buf_pool[entry];
		struct dma_desc *p;
		bool use_rx_wd;

		p = rx_q->dma_rx + entry;

		if (!buf->page) {
			buf->page = page_pool_dev_alloc_pages(rx_q->page_pool);
			if (!buf->page)
				break;
		}

		buf->addr = page_pool_get_dma_addr(buf->page);

		/* Sync whole allocation to device. This will invalidate old
		 * data.
		 */
		dma_sync_single_for_device(GET_MEM_PDEV_DEV, buf->addr, len,
					   DMA_FROM_DEVICE);

		stmmac_set_desc_addr(priv, p, buf->addr);

		rx_q->rx_count_frames++;
		if (rx_q->rx_count_frames > priv->rx_coal_frames)
			rx_q->rx_count_frames = 0;
		use_rx_wd = priv->use_riwt && rx_q->rx_count_frames;

		dma_wmb();
		stmmac_set_rx_owner(priv, p, use_rx_wd);

		entry = STMMAC_GET_ENTRY(entry, DMA_RX_SIZE);
	}
	rx_q->dirty_rx = entry;
	rx_q->rx_tail_addr = rx_q->dma_rx_phy +
			    (rx_q->dirty_rx * sizeof(struct dma_desc));
	stmmac_set_rx_tail_ptr(priv, priv->ioaddr, rx_q->rx_tail_addr, queue);
}

/**
 * stmmac_rx - manage the receive process
 * @priv: driver private structure
 * @limit: napi bugget
 * @queue: RX queue index.
 * Description :  this the function called by the napi poll method.
 * It gets all the frames inside the ring.
 */
static int stmmac_rx(struct stmmac_priv *priv, int limit, u32 queue)
{
	struct stmmac_rx_queue *rx_q = &priv->rx_queue;
	struct stmmac_channel *ch = &priv->channel;
	unsigned int count = 0, error = 0, len = 0;
	int status = 0, coe = 1;
	unsigned int next_entry = rx_q->cur_rx;
	struct sk_buff *skb = NULL;

	if (queue != priv->queue)
		return count;

	if (!priv->dev_inited || priv->emac_state != EMAC_LINK_UP_ST) {
		netdev_err(priv->dev, "%s device not ready, emac state %u\n",
			   __func__, priv->emac_state);
		return count;
	}

	trace_stmmac_rx_entry(queue);
	while (count < limit) {
		unsigned int prev_len = 0;
		enum pkt_hash_types hash_type;
		struct stmmac_rx_buffer *buf;
		struct dma_desc *np, *p;
		int entry;
		u32 hash;

		if (!count && rx_q->state_saved) {
			skb = rx_q->state.skb;
			error = rx_q->state.error;
			len = rx_q->state.len;
		} else {
			rx_q->state_saved = false;
			skb = NULL;
			error = 0;
			len = 0;
		}

		if (count >= limit)
			break;

read_again:
		entry = next_entry;
		buf = &rx_q->buf_pool[entry];

		p = rx_q->dma_rx + entry;

		/* read the status of the incoming frame */
		status = stmmac_rx_status(priv, &priv->dev->stats,
					  &priv->xstats, p);
		/* check if managed by the DMA otherwise go ahead */
		if (unlikely(status & dma_own))
			break;

		rx_q->cur_rx = STMMAC_GET_ENTRY(rx_q->cur_rx, DMA_RX_SIZE);
		next_entry = rx_q->cur_rx;

		np = rx_q->dma_rx + next_entry;

		prefetch(np);
		prefetch(page_address(buf->page));

		if (unlikely(status & discard_frame)) {
			page_pool_recycle_direct(rx_q->page_pool, buf->page);
			buf->page = NULL;
			error = 1;
			if (!(status & ctxt_desc) && !priv->hwts_rx_en)
				priv->dev->stats.rx_errors++;
		}

		if (unlikely(error && (status & rx_not_ls)))
			goto read_again;
		if (unlikely(error)) {
			dev_kfree_skb(skb);
			count++;
			continue;
		}

		/* Buffer is good. Go on. */

		if (likely(status & rx_not_ls)) {
			len += priv->dma_buf_sz;
		} else {
			prev_len = len;
			len = stmmac_get_rx_frame_len(priv, p, coe);
		}
		if (!skb) {
			skb = napi_alloc_skb(&ch->rx_napi, len);
			if (!skb) {
				priv->dev->stats.rx_dropped++;
				count++;
				continue;
			}

			dma_sync_single_for_cpu(GET_MEM_PDEV_DEV,
						buf->addr, len,
						DMA_FROM_DEVICE);
			skb_copy_to_linear_data(skb, page_address(buf->page),
						len);
			skb_put(skb, len);

			/* Data payload copied into SKB,
			 * page ready for recycle
			 */
			page_pool_recycle_direct(rx_q->page_pool, buf->page);
			buf->page = NULL;
		} else {
			unsigned int buf_len = len - prev_len;

			if (likely(status & rx_not_ls))
				buf_len = priv->dma_buf_sz;

			dma_sync_single_for_cpu(GET_MEM_PDEV_DEV, buf->addr,
						buf_len, DMA_FROM_DEVICE);
			skb_add_rx_frag(skb, skb_shinfo(skb)->nr_frags,
					buf->page, 0, buf_len,
					priv->dma_buf_sz);

			/* Data payload appended into SKB */
			page_pool_release_page(rx_q->page_pool, buf->page);
			buf->page = NULL;
		}

		if (likely(status & rx_not_ls))
			goto read_again;

		/* Got entire packet into SKB. Finish it. */

		stmmac_get_rx_hwtstamp(priv, p, np, skb);
		stmmac_rx_vlan(priv->dev, skb);
		skb->protocol = eth_type_trans(skb, priv->dev);

		if (unlikely(!coe))
			skb_checksum_none_assert(skb);
		else
			skb->ip_summed = CHECKSUM_UNNECESSARY;

		if (!stmmac_get_rx_hash(priv, p, &hash, &hash_type))
			skb_set_hash(skb, hash, hash_type);

		skb_record_rx_queue(skb, queue);
		napi_gro_receive(&ch->rx_napi, skb);
		trace_stmmac_rx_pkt(queue);
		priv->dev->stats.rx_packets++;
#ifdef CONFIG_QGKI_MSM_BOOT_TIME_MARKER
	if (priv->dev->stats.rx_packets == 1)
		place_marker("M - Ethernet first packet received");
#endif
		priv->dev->stats.rx_bytes += len;
		count++;
	}

	if (status & rx_not_ls) {
		rx_q->state_saved = true;
		rx_q->state.skb = skb;
		rx_q->state.error = error;
		rx_q->state.len = len;
	}

	stmmac_rx_refill(priv, queue);

	priv->xstats.rx_pkt_n += count;
	priv->xstats.q_rx_pkt_n[queue] += count;

	trace_stmmac_rx_exit(queue);
	return count;
}

static int stmmac_napi_poll_rx(struct napi_struct *napi, int budget)
{
	struct stmmac_channel *ch =
		container_of(napi, struct stmmac_channel, rx_napi);
	struct stmmac_priv *priv = ch->priv_data;
	u32 chan = priv->queue;
	int work_done;

	priv->xstats.napi_poll++;

	trace_stmmac_poll_enter(chan);
	work_done = stmmac_rx(priv, budget, chan);

	if (work_done < budget && napi_complete_done(napi, work_done)) {
		trace_stmmac_enable_irq(chan);
		stmmac_enable_dma_irq(priv, priv->ioaddr, chan);
	}
	trace_stmmac_poll_exit(chan, work_done);
	return work_done;
}

static int stmmac_napi_poll_tx(struct napi_struct *napi, int budget)
{
	struct stmmac_channel *ch =
		container_of(napi, struct stmmac_channel, tx_napi);
	struct stmmac_priv *priv = ch->priv_data;
	struct stmmac_tx_queue *tx_q;
	u32 chan = priv->queue;
	int work_done;

	priv->xstats.napi_poll++;

	work_done = stmmac_tx_clean(priv, DMA_TX_SIZE, chan);
	work_done = min(work_done, budget);

	if (work_done < budget)
		napi_complete_done(napi, work_done);

	/* Force transmission restart */
	tx_q = &priv->tx_queue;
	if (tx_q->cur_tx != tx_q->dirty_tx) {
		stmmac_enable_dma_transmission(priv, priv->ioaddr);
		stmmac_set_tx_tail_ptr(priv, priv->ioaddr, tx_q->tx_tail_addr,
				       chan);
	}

	return work_done;
}

/**
 *  stmmac_tx_timeout
 *  @dev : Pointer to net device structure
 *  Description: this function is called when a packet transmission fails to
 *   complete within a reasonable time. The driver will mark the error in the
 *   netdev structure and arrange for the device to be reset to a sane state
 *   in order to transmit a new packet.
 */
static void stmmac_tx_timeout(struct net_device *dev)
{
	struct stmmac_priv *priv = netdev_priv(dev);

	stmmac_global_err(priv);
}

/**
 *  stmmac_set_rx_mode - entry point for multicast addressing
 *  @dev : pointer to the device structure
 *  Description:
 *  This function is a driver entry point which gets called by the kernel
 *  whenever multicast addresses must be enabled/disabled.
 *  Return value:
 *  void.
 */
static void stmmac_set_rx_mode(struct net_device *dev)
{
	struct stmmac_priv *priv = netdev_priv(dev);

	if (priv->emac_state > EMAC_INIT_ST) {
		if (!netdev_mc_empty(dev)) {
			priv->filter_type = MULTICAST_TYPE;
			priv->add_filter(dev);
		}
		if (netdev_uc_count(dev) > 0) {
			priv->filter_type = UNICAST_TYPE;
			priv->add_filter(dev);
		}
	}
}

static netdev_features_t stmmac_fix_features(struct net_device *dev,
					     netdev_features_t features)
{
	struct stmmac_priv *priv = netdev_priv(dev);

	if (priv->plat->rx_coe == STMMAC_RX_COE_NONE)
		features &= ~NETIF_F_RXCSUM;

	if (!priv->plat->tx_coe)
		features &= ~NETIF_F_CSUM_MASK;

	/* Disable tso if asked by ethtool */
	if (priv->plat->tso_en) {
		if (features & NETIF_F_TSO)
			priv->tso = true;
		else
			priv->tso = false;
	}

	return features;
}

/**
 *  stmmac_interrupt - main ISR
 *  @irq: interrupt number.
 *  @dev_id: to pass the net device pointer.
 *  Description: this is the main driver interrupt service routine.
 *  It can call:
 *  o DMA service routine (to manage incoming frame reception and transmission
 *    status)
 *  o Core interrupts to manage: remote wake-up, management counter, LPI
 *    interrupts.
 */
static irqreturn_t stmmac_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = (struct net_device *)dev_id;
	struct stmmac_priv *priv = netdev_priv(dev);
	struct stmmac_rx_queue *rx_q = &priv->rx_queue;
	int status;

	if (unlikely(!dev)) {
		netdev_err(priv->dev, "%s: invalid dev pointer\n", __func__);
		return IRQ_NONE;
	}

	if (irq != dev->irq)
		return IRQ_HANDLED;

	/* Check if adapter is up */
	if (test_bit(STMMAC_DOWN, &priv->state))
		return IRQ_HANDLED;

	trace_stmmac_irq_enter(irq);
	/* To handle GMAC own interrupts */
	status = stmmac_host_mtl_irq_status(priv, priv->hw, priv->queue);
	if (status & CORE_IRQ_MTL_RX_OVERFLOW)
		stmmac_set_rx_tail_ptr(priv, priv->ioaddr,
				       rx_q->rx_tail_addr,
				       priv->queue);

	/* To handle DMA interrupts */
	stmmac_dma_interrupt(priv);
	trace_stmmac_irq_exit(irq);

	return IRQ_HANDLED;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
/* Polling receive - used by NETCONSOLE and other diagnostic tools
 * to allow network I/O with interrupts disabled.
 */
static void stmmac_poll_controller(struct net_device *dev)
{
	disable_irq(dev->irq);
	stmmac_interrupt(dev->irq, dev);
	enable_irq(dev->irq);
}
#endif

static int stmmac_set_mac_address(struct net_device *ndev, void *addr)
{
	struct stmmac_priv *priv = netdev_priv(ndev);
	int ret = 0;

	ret = eth_mac_addr(ndev, addr);
	if (ret)
		return ret;

	if (priv->emac_state > EMAC_INIT_ST)
		ret = priv->mac_addr(ndev);

	return ret;
}

static u16 stmmac_tx_select_queue(struct net_device *dev,
				  struct sk_buff *skb,
				  struct net_device *sb_dev)
{
	struct stmmac_priv *priv = netdev_priv(dev);

	if (likely(priv->plat->tx_select_queue))
		return priv->plat->tx_select_queue(dev, skb, sb_dev);

	return netdev_pick_tx(dev, skb, NULL) % dev->real_num_tx_queues;
}

static int stmmac_vlan_rx_add_vid(struct net_device *ndev,
				  __be16 proto, u16 vid)
{
	struct stmmac_priv *priv = netdev_priv(ndev);

	if (vid == 0 || vid > 4095) {
		dev_info(priv->device, "Invalid vlan id %u\n", vid);
		return 0;
	}

	priv->vid = vid;
	priv->proto = proto;
	priv->filter_type = VLAN_TYPE;

	return priv->add_filter(ndev);
}

static int stmmac_vlan_rx_kill_vid(struct net_device *ndev,
				   __be16 proto, u16 vid)
{
	struct stmmac_priv *priv = netdev_priv(ndev);

	if (vid == 0 || vid > 4095) {
		dev_info(priv->device, "Invalid vlan id %u\n", vid);
		return 0;
	}

	priv->vid = vid;
	priv->proto = proto;
	priv->filter_type = VLAN_TYPE;

	return priv->del_filter(ndev);
}

static const struct net_device_ops stmmac_netdev_ops = {
	.ndo_open = stmmac_open,
	.ndo_start_xmit = stmmac_xmit,
	.ndo_stop = stmmac_release,
	.ndo_fix_features = stmmac_fix_features,
	.ndo_set_rx_mode = stmmac_set_rx_mode,
	.ndo_tx_timeout = stmmac_tx_timeout,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller = stmmac_poll_controller,
#endif
	.ndo_set_mac_address = stmmac_set_mac_address,
	.ndo_vlan_rx_add_vid = stmmac_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid = stmmac_vlan_rx_kill_vid,
	.ndo_select_queue = stmmac_tx_select_queue,
};

static void stmmac_reset_subtask(struct stmmac_priv *priv)
{
	if (!test_and_clear_bit(STMMAC_RESET_REQUESTED, &priv->state))
		return;
	if (test_bit(STMMAC_DOWN, &priv->state))
		return;

	netdev_err(priv->dev, "Reset adapter.\n");

	rtnl_lock();
	netif_trans_update(priv->dev);
	while (test_and_set_bit(STMMAC_RESETTING, &priv->state))
		usleep_range(1000, 2000);

	set_bit(STMMAC_DOWN, &priv->state);
	dev_close(priv->dev);
	dev_open(priv->dev, NULL);
	clear_bit(STMMAC_DOWN, &priv->state);
	clear_bit(STMMAC_RESETTING, &priv->state);
	rtnl_unlock();
}

static void stmmac_service_task(struct work_struct *work)
{
	struct stmmac_priv *priv = container_of(work, struct stmmac_priv,
			service_task);

	stmmac_reset_subtask(priv);
	clear_bit(STMMAC_SERVICE_SCHED, &priv->state);
}

/**
 *  stmmac_hw_init - Init the MAC device
 *  @priv: driver private structure
 *  Description: this function is to configure the MAC device according to
 *  some platform parameters or the HW capability register. It prepares the
 *  driver to use either ring or chain modes and to setup either enhanced or
 *  normal descriptors.
 */
static int stmmac_hw_init(struct stmmac_priv *priv)
{
	int ret;

	/* Initialize HW Interface */
	ret = stmmac_hwif_init(priv);
	if (ret)
		return ret;

	priv->plat->tx_coe = 1;
	dev_info(priv->device, "TX Checksum insertion supported\n");

	if (priv->tso)
		dev_info(priv->device, "TSO supported\n");

	/* Run HW quirks, if any */
	if (priv->hwif_quirks) {
		ret = priv->hwif_quirks(priv);
		if (ret)
			return ret;
	}

	/* Rx Watchdog is available in the COREs newer than the 3.40.
	 * In some case, for example on bugged HW this feature
	 * has to be disable and this can be done by passing the
	 * riwt_off field from the platform.
	 */
	priv->use_riwt = 1;
	dev_info(priv->device,
		 "Enable RX Mitigation via HW Watchdog Timer\n");

	return 0;
}

void stmmac_mac_link_down(struct net_device *ndev)
{
	struct stmmac_priv *priv;

	if (!ndev)
		return;

	priv = netdev_priv(ndev);
	if (priv->dev_opened)
		netif_carrier_off(ndev);
#ifdef CONFIG_QGKI_MSM_BOOT_TIME_MARKER
	if (priv->boot_kpi) {
		place_marker("M - Ethernet is not Ready. Link is DOWN");
		priv->boot_kpi = false;
	}
#endif
}

void stmmac_mac_link_up(struct net_device *ndev)
{
	struct stmmac_priv *priv;

	if (!ndev)
		return;

	priv = netdev_priv(ndev);
	if (priv->dev_opened) {
		netif_carrier_on(ndev);
#ifdef CONFIG_QGKI_MSM_BOOT_TIME_MARKER
		if (!priv->boot_kpi) {
			place_marker("M - Ethernet is Ready. Link is UP");
			priv->boot_kpi = true;
		}
#endif
	}
}

void stmmac_ch_status(struct net_device *ndev)
{
	struct stmmac_priv *priv;
	int status;

	if (!ndev)
		return;

	priv = netdev_priv(ndev);
	status = stmmac_host_mtl_irq_status(priv, priv->hw, priv->queue);
	if (status & CORE_IRQ_MTL_RX_OVERFLOW) {
		struct stmmac_rx_queue *rx_q = &priv->rx_queue;

		stmmac_set_rx_tail_ptr(priv, priv->ioaddr,
				       rx_q->rx_tail_addr,
				       priv->queue);
	}
}

int stmmac_dvr_init(struct net_device *dev)
{
	struct stmmac_priv *priv;
	int ret = 0;

	if (!dev)
		return -ENOMEM;

	pr_info("%s: Enter\n", __func__);
	priv = netdev_priv(dev);

	mutex_lock(&priv->lock);

	ret = alloc_dma_desc_resources(priv);
	if (ret < 0) {
		netdev_err(priv->dev, "%s: DMA descriptors alloc failed\n",
			   __func__);
		goto dma_desc_error;
	}

	ret = init_dma_desc_rings(dev, GFP_KERNEL);
	if (ret < 0) {
		netdev_err(priv->dev, "%s: DMA descriptors init failed\n",
			   __func__);
		goto init_error;
	}

	ret = stmmac_hw_setup(dev);
	if (ret < 0) {
		netdev_err(priv->dev, "%s: Hw setup failed\n", __func__);
		goto init_error;
	}

	if (!priv->tx_coal_timer_disable)
		stmmac_init_coalesce(priv);
	else
		priv->rx_coal_frames = STMMAC_RX_FRAMES;

	/* Request the IRQ lines */
	if (dev->irq) {
		ret = request_irq(dev->irq, stmmac_interrupt,
				  IRQF_SHARED, dev->name, dev);
		if (unlikely(ret < 0)) {
			netdev_err(priv->dev,
				   "%s: allocating the IRQ %d (error: %d)\n",
				   __func__, dev->irq, ret);
			goto irq_error;
		}
	}

	stmmac_enable_queue(priv);
	stmmac_start_queue(priv);

	priv->dev_inited = true;

	mutex_unlock(&priv->lock);

	return 0;

irq_error:
	if (!priv->tx_coal_timer_disable)
		del_timer_sync(&priv->tx_queue.txtimer);

init_error:
	free_dma_desc_resources(priv);
dma_desc_error:
	mutex_unlock(&priv->lock);
	return ret;
}

#ifdef CONFIG_DEBUG_FS
static struct dentry *stmmac_fs_dir;

static void sysfs_display_ring(void *head, int size, struct seq_file *seq)
{
	int i;
	struct dma_desc *p = (struct dma_desc *)head;

	for (i = 0; i < size; i++) {
		seq_printf(seq, "%d [0x%x]: 0x%x 0x%x 0x%x 0x%x\n",
			   i, (unsigned int)virt_to_phys(p),
			   le32_to_cpu(p->des0), le32_to_cpu(p->des1),
			   le32_to_cpu(p->des2), le32_to_cpu(p->des3));
		p++;
	}
}

static int stmmac_rings_status_show(struct seq_file *seq, void *v)
{
	struct net_device *dev = seq->private;
	struct stmmac_priv *priv = netdev_priv(dev);
	u32 ch = priv->queue;
	struct stmmac_rx_queue *rx_q = &priv->rx_queue;
	struct stmmac_tx_queue *tx_q = &priv->tx_queue;
	struct stmmac_extra_stats *x = &priv->xstats;

	if ((dev->flags & IFF_UP) == 0 || !priv->dev_inited) {
		netdev_err(priv->dev, "%s: device is not ready for dump\n",
			   __func__);
		return 0;
	}

	stmmac_get_dma_stats(priv, priv->ioaddr, x, ch);

	seq_printf(seq, "\n***** DMA channel %u dump *****\n", ch);
	seq_printf(seq, "dma_ch_status(reg offset 0x%X) = 0x%x\n",
		   DMA_CHAN_STATUS(ch), x->dma_ch_status);
	seq_printf(seq, "dma_ch_intr_enable(reg offset 0x%X) = %#x\n",
		   DMA_CHAN_INTR_ENA(ch), x->dma_ch_intr_en);

	seq_printf(seq, "\n***** DMA TX channel %u dump *****\n", ch);
	seq_printf(seq, "dma_ch_tx_control(reg offset 0x%X) = %#x\n",
		   DMA_CHAN_TX_CONTROL(ch), x->dma_ch_tx_control);
	seq_printf(seq, "dma_ch_txdesc_list_addr(reg offset 0x%X) = 0x%x\n",
		   DMA_CHAN_TX_BASE_ADDR(ch), x->dma_ch_txdesc_list_addr);
	seq_printf(seq, "dma_ch_txdesc_ring_len(reg offset 0x%X) = %u\n",
		   DMA_CHAN_TX_RING_LEN(ch), x->dma_ch_txdesc_ring_len);
	seq_printf(seq, "dma_ch_curr_app_txdesc(reg offset 0x%X) = %#x\n",
		   DMA_CHAN_CUR_TX_DESC(ch), x->dma_ch_curr_app_txdesc);
	seq_printf(seq, "dma_ch_txdesc_tail_ptr(reg offset 0x%X) = %#x\n",
		   DMA_CHAN_TX_END_ADDR(ch), x->dma_ch_txdesc_tail_ptr);
	seq_printf(seq, "dma_ch_curr_app_txbuf(reg offset 0x%X) = %#x\n",
		   DMA_CHAN_CUR_TX_BUF_ADDR(ch), x->dma_ch_curr_app_txbuf);

	seq_printf(seq, "\nTX chan %u ring cnt=%u cur_tx=%u\n",
		   ch, DMA_TX_SIZE, tx_q->cur_tx);
	sysfs_display_ring((void *)tx_q->dma_tx, DMA_TX_SIZE, seq);

	seq_printf(seq, "\n***** DMA RX channel %u dump *****\n", ch);
	seq_printf(seq, "dma_ch_rx_control(reg offset 0x%X) = %#x\n",
		   DMA_CHAN_RX_CONTROL(ch), x->dma_ch_rx_control);
	seq_printf(seq, "dma_ch_rxdesc_list_addr(reg offset 0x%X) = 0x%x\n",
		   DMA_CHAN_RX_BASE_ADDR(ch), x->dma_ch_rxdesc_list_addr);
	seq_printf(seq, "dma_ch_rxdesc_ring_len(reg offset 0x%X) = %u\n",
		   DMA_CHAN_RX_RING_LEN(ch), x->dma_ch_rxdesc_ring_len);
	seq_printf(seq, "dma_ch_curr_app_rxdesc(reg offset 0x%X) = %#x\n",
		   DMA_CHAN_CUR_RX_DESC(ch), x->dma_ch_curr_app_rxdesc);
	seq_printf(seq, "dma_ch_rxdesc_tail_ptr(reg offset 0x%X) = %#x\n",
		   DMA_CHAN_RX_END_ADDR(ch), x->dma_ch_rxdesc_tail_ptr);
	seq_printf(seq, "dma_ch_curr_app_rxbuf(reg offset 0x%X) = %#x\n",
		   DMA_CHAN_CUR_RX_BUF_ADDR(ch), x->dma_ch_curr_app_rxbuf);
	seq_printf(seq, "dma_ch_miss_frame_count(reg offset0x%X) = %#x\n",
		   DMA_CHAN_MISS_FRAME_CNT(ch), x->dma_ch_miss_frame_count);

	seq_printf(seq, "\nRX chan %u ring cnt=%u cur_rx=%u\n",
		   ch, DMA_RX_SIZE, rx_q->cur_rx);
	sysfs_display_ring((void *)rx_q->dma_rx, DMA_RX_SIZE, seq);

	return 0;
}

static int stmmac_rxtx_status_show(struct seq_file *seq, void *v)
{
	struct net_device *dev = seq->private;
	struct stmmac_priv *priv = netdev_priv(dev);
	u32 ch = priv->queue;
	struct stmmac_extra_stats *x = &priv->xstats;
	struct net_device_stats *stats = &priv->dev->stats;

	if ((dev->flags & IFF_UP) == 0 || !priv->dev_inited) {
		netdev_err(priv->dev, "%s: device is not ready for dump\n",
			   __func__);
		return 0;
	}

	seq_printf(seq, "\n***** channel %u data stats *****\n", ch);
	seq_printf(seq, "tx packets = %u\n", stats->tx_packets);
	seq_printf(seq, "rx packets = %u\n", stats->rx_packets);

	seq_printf(seq, "\n***** channel %u tx errors *****\n", ch);
	seq_printf(seq, "tx drop packets = %u\n", stats->tx_dropped);
	seq_printf(seq, "tx_underflow = %u\n", x->tx_underflow);
	seq_printf(seq, "tx no carrier = %u\n", x->tx_carrier);

	seq_printf(seq, "\n***** channel %u rx errors *****\n", ch);
	seq_printf(seq, "rx drop packets = %u\n", stats->rx_dropped);
	seq_printf(seq, "rx_crc_errors = %u\n", x->rx_crc_errors);
	seq_printf(seq, "rx_gmac_overflow = %u\n", x->rx_gmac_overflow);

	return 0;
}

static int stmmac_reg_value_dump(struct seq_file *seq, void *v)
{
	struct net_device *dev = seq->private;
	struct stmmac_priv *priv = netdev_priv(dev);
	u32 ch = priv->queue;

	/* dump the reg info in dmesg */
	stmmac_get_reg(priv, priv->ioaddr, ch);
	return 0;
}

static int stmmac_sysfs_ring_open(struct inode *inode, struct file *file)
{
	return single_open(file, stmmac_rings_status_show, inode->i_private);
}

static int stmmac_sysfs_data_open(struct inode *inode, struct file *file)
{
	return single_open(file, stmmac_rxtx_status_show, inode->i_private);
}

static int stmmac_sysfs_reg_open(struct inode *inode, struct file *file)
{
	return single_open(file, stmmac_reg_value_dump, inode->i_private);
}

/* Debugfs files, should appear in /sys/kernel/debug/eth1 */

static const struct file_operations stmmac_rings_status_fops = {
	.owner = THIS_MODULE,
	.open = stmmac_sysfs_ring_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static const struct file_operations stmmac_data_status_fops = {
	.owner = THIS_MODULE,
	.open = stmmac_sysfs_data_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static const struct file_operations stmmac_reg_info_fops = {
	.owner = THIS_MODULE,
	.open = stmmac_sysfs_reg_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int stmmac_init_fs(struct net_device *dev)
{
	struct stmmac_priv *priv = netdev_priv(dev);

	rtnl_lock();

	/* Create per netdev entries */
	priv->dbgfs_dir = debugfs_create_dir(dev->name, stmmac_fs_dir);

	if (!priv->dbgfs_dir || IS_ERR(priv->dbgfs_dir)) {
		netdev_err(priv->dev,
			   "ERROR failed to create debugfs directory\n");
		rtnl_unlock();
		return -ENOMEM;
	}
	/* Entry to report DMA RX/TX rings */
	debugfs_create_file("dump_dma", 0444, priv->dbgfs_dir, dev,
			    &stmmac_rings_status_fops);

	/* Entry to report RX/TX data path stats */
	debugfs_create_file("data_stats", 0444, priv->dbgfs_dir, dev,
			    &stmmac_data_status_fops);

	/* Entry to report RX/TX data path stats */
	debugfs_create_file("dump_reg", 0444, priv->dbgfs_dir, dev,
			    &stmmac_reg_info_fops);

	rtnl_unlock();

	return 0;
}

static void stmmac_exit_fs(struct net_device *dev)
{
	struct stmmac_priv *priv = netdev_priv(dev);

	debugfs_remove_recursive(priv->dbgfs_dir);
}
#endif /* CONFIG_DEBUG_FS */

/**
 * stmmac_dvr_probe
 * @device: device pointer
 * @plat_dat: platform data pointer
 * @res: stmmac resource pointer
 * Description: this is the main probe function used to
 * call the alloc_etherdev, allocate the priv structure.
 * Return:
 * returns 0 on success, otherwise errno.
 */
int stmmac_dvr_probe(struct device *device,
		     struct plat_stmmacenet_data *plat_dat,
		     struct stmmac_resources *res)
{
	struct net_device *ndev = NULL;
	struct stmmac_priv *priv;
	int ret = 0;
	struct stmmac_channel *ch;

	/* Set tx used queue to 4 so NW stack can trigger tx queue selection */
	ndev = devm_alloc_etherdev_mqs(device, sizeof(struct stmmac_priv),
				       4, 1);
	if (!ndev)
		return -ENOMEM;

	SET_NETDEV_DEV(ndev, device);

	priv = netdev_priv(ndev);
	priv->device = device;
	priv->dev = ndev;

	stmmac_set_ethtool_ops(ndev);
	priv->plat = plat_dat;
	priv->ioaddr = res->addr;
	priv->dev->base_addr = (unsigned long)res->addr;
	priv->queue = res->ch;
	priv->dev->irq = res->irq[priv->queue];
	dev_info(priv->device, "%s: ioaddr[0x%x] ch[%u] irq is [%d]\n",
		 __func__, priv->ioaddr, res->ch, priv->dev->irq);

	if (!IS_ERR_OR_NULL(res->mac))
		memcpy(priv->dev->dev_addr, res->mac, ETH_ALEN);

	dev_set_drvdata(device, priv->dev);

	/* Verify driver arguments */
	stmmac_verify_args();

	/* Allocate workqueue */
	priv->wq = create_singlethread_workqueue("stmmac_wq");
	if (!priv->wq) {
		dev_err(priv->device, "failed to create workqueue\n");
		return -ENOMEM;
	}

	INIT_WORK(&priv->service_task, stmmac_service_task);

	if (priv->plat->stmmac_rst) {
		ret = reset_control_assert(priv->plat->stmmac_rst);
		reset_control_deassert(priv->plat->stmmac_rst);
		/* Some reset controllers have only reset callback instead of
		 * assert + deassert callbacks pair.
		 */
		if (ret == -EOPNOTSUPP)
			reset_control_reset(priv->plat->stmmac_rst);
	}

	/* Init MAC and get the capabilities */
	ret = stmmac_hw_init(priv);
	if (ret)
		goto error_hw_init;

	/* Configure real RX and TX queues */
	netif_set_real_num_rx_queues(ndev, 1);
	/* To trigger tx queue number selection other then 0 */
	netif_set_real_num_tx_queues(ndev, 4);

	ndev->netdev_ops = &stmmac_netdev_ops;
	dev_info(priv->device, "dev opt=0x%X\n", ndev->netdev_ops);

	ndev->hw_features = NETIF_F_SG | NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM |
			    NETIF_F_RXCSUM;

	if (priv->plat->tso_en) {
		ndev->hw_features |= NETIF_F_TSO | NETIF_F_TSO6;
		priv->tso = true;
		dev_info(priv->device, "TSO feature enabled\n");
	}

	ndev->features |= ndev->hw_features | NETIF_F_HIGHDMA;
	ndev->watchdog_timeo = msecs_to_jiffies(watchdog);
#ifdef STMMAC_VLAN_TAG_USED
	ndev->vlan_features |= ndev->hw_features;
	/* Both mac100 and gmac support receive VLAN tag detection */
	ndev->features |= NETIF_F_HW_VLAN_CTAG_RX | NETIF_F_HW_VLAN_STAG_RX;
	ndev->features |= NETIF_F_HW_VLAN_CTAG_FILTER |
			  NETIF_F_HW_VLAN_STAG_FILTER;
#endif
	priv->msg_enable = netif_msg_init(debug, default_msg_level);

	/* MTU range: 46 - hw-specific max */
	ndev->min_mtu = ETH_ZLEN - ETH_HLEN;
	ndev->max_mtu = JUMBO_LEN;
	/* Will not overwrite ndev->max_mtu if plat->maxmtu > ndev->max_mtu
	 * as well as plat->maxmtu < ndev->min_mtu which is a invalid range.
	 */
	if (priv->plat->maxmtu < ndev->max_mtu &&
	    priv->plat->maxmtu >= ndev->min_mtu)
		ndev->max_mtu = priv->plat->maxmtu;
	else if (priv->plat->maxmtu < ndev->min_mtu)
		dev_warn(priv->device,
			 "%s: warning: maxmtu having invalid value (%d)\n",
			 __func__, priv->plat->maxmtu);

	/* Setup channel NAPI */
	ch = &priv->channel;

	ch->priv_data = priv;

	netif_napi_add(ndev, &ch->rx_napi, stmmac_napi_poll_rx,
		       NAPI_POLL_WEIGHT);

	netif_tx_napi_add(ndev, &ch->tx_napi, stmmac_napi_poll_tx,
			  NAPI_POLL_WEIGHT);

	mutex_init(&priv->lock);

	/* If a specific clk_csr value is passed from the platform
	 * this means that the CSR Clock Range selection cannot be
	 * changed at run-time and it is fixed. Viceversa the driver'll try to
	 * set the MDC clock dynamically according to the csr actual
	 * clock input.
	 */
	ret = register_netdev(ndev);
	dev_info(priv->device, "register_netdev[%s] ret=%d\n", ndev->name, ret);
	if (ret) {
		dev_err(priv->device, "%s: ERROR %i registering the device\n",
			__func__, ret);
		goto error_netdev_register;
	}

	/* Disable tx_coal_timer if plat provides callback */
	priv->tx_coal_timer_disable =
		plat_dat->get_plat_tx_coal_frames ? true : false;

#ifdef CONFIG_DEBUG_FS
	if (stmmac_init_fs(ndev) < 0)
		netdev_warn(priv->dev, "%s: failed debugFS registration\n",
			    __func__);
#endif

	return ret;

error_netdev_register:
	netif_napi_del(&ch->rx_napi);
	netif_napi_del(&ch->tx_napi);
error_hw_init:
	destroy_workqueue(priv->wq);

	return ret;
}
EXPORT_SYMBOL_GPL(stmmac_dvr_probe);

/**
 * stmmac_dvr_remove
 * @dev: device pointer
 * Description: this function resets the TX/RX processes, disables the MAC RX/TX
 * changes the link status, releases the DMA descriptor rings.
 */
int stmmac_dvr_remove(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct stmmac_priv *priv = netdev_priv(ndev);

	netdev_info(priv->dev, "%s: removing driver", __func__);

#ifdef CONFIG_DEBUG_FS
	stmmac_exit_fs(ndev);
#endif

	if (priv->emac_state > EMAC_INIT_ST) {
		stmmac_stop_dma(priv);
		netif_carrier_off(ndev);
	}

	unregister_netdev(ndev);
	if (priv->plat->stmmac_rst)
		reset_control_assert(priv->plat->stmmac_rst);
	destroy_workqueue(priv->wq);
	mutex_destroy(&priv->lock);

	return 0;
}
EXPORT_SYMBOL_GPL(stmmac_dvr_remove);

/**
 * stmmac_suspend - suspend callback
 * @dev: device pointer
 * Description: this is the function to suspend the device and it is called
 * by the platform driver to stop the network queue, release the resources,
 * program the PMT register (for WoL), clean and release driver resources.
 */
int stmmac_suspend(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct stmmac_priv *priv = netdev_priv(ndev);

	if (!ndev || !netif_running(ndev))
		return 0;

	mutex_lock(&priv->lock);

	netif_device_detach(ndev);
	stmmac_stop_queue(priv);

	stmmac_disable_queue(priv);

	del_timer_sync(&priv->tx_queue.txtimer);

	/* Stop TX/RX DMA */
	stmmac_stop_dma(priv);

	mutex_unlock(&priv->lock);

	return 0;
}
EXPORT_SYMBOL_GPL(stmmac_suspend);

/**
 * stmmac_reset_queues_param - reset queue parameters
 * @dev: device pointer
 */
static void stmmac_reset_queues_param(struct stmmac_priv *priv)
{
	struct stmmac_rx_queue *rx_q = &priv->rx_queue;
	struct stmmac_tx_queue *tx_q = &priv->tx_queue;

	rx_q->cur_rx = 0;
	rx_q->dirty_rx = 0;

	tx_q->cur_tx = 0;
	tx_q->dirty_tx = 0;
	tx_q->mss = 0;
}

/**
 * stmmac_resume - resume callback
 * @dev: device pointer
 * Description: when resume this function is invoked to setup the DMA and CORE
 * in a usable state.
 */
int stmmac_resume(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct stmmac_priv *priv = netdev_priv(ndev);

	if (!netif_running(ndev))
		return 0;

	netif_device_attach(ndev);

	mutex_lock(&priv->lock);

	stmmac_reset_queues_param(priv);

	stmmac_clear_descriptors(priv);

	stmmac_hw_setup(ndev);

	if (!priv->tx_coal_timer_disable)
		stmmac_init_coalesce(priv);
	else
		priv->rx_coal_frames = STMMAC_RX_FRAMES;

	stmmac_set_rx_mode(ndev);

	stmmac_enable_queue(priv);

	stmmac_start_queue(priv);

	mutex_unlock(&priv->lock);

	return 0;
}
EXPORT_SYMBOL_GPL(stmmac_resume);

static int __init stmmac_init(void)
{
	return 0;
}

static void __exit stmmac_exit(void)
{
}

module_init(stmmac_init)
module_exit(stmmac_exit)

MODULE_DESCRIPTION("STMMAC 10/100/1000 Ethernet device driver");
MODULE_AUTHOR("Giuseppe Cavallaro <peppe.cavallaro@st.com>");
MODULE_LICENSE("GPL v2");
