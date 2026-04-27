/* SPDX-License-Identifier: GPL-2.0-only */
/*******************************************************************************
 * Copyright (C) 2007-2009  STMicroelectronics Ltd
 *
 * Author: Giuseppe Cavallaro <peppe.cavallaro@st.com>
 ******************************************************************************/

#ifndef __STMMAC_THIN_H__
#define __STMMAC_THIN_H__

#define STMMAC_RESOURCE_NAME   "stmmaceth"
#define DRV_MODULE_VERSION	"Jan_2016"

#include <linux/if_vlan.h>
#include <linux/stmmac.h>

#include <linux/pci.h>
#include "common_thin.h"

#include <linux/net_tstamp.h>
#include <linux/reset.h>
#include "net/page_pool.h"
#ifdef CONFIG_QGKI_MSM_BOOT_TIME_MARKER
#include <soc/qcom/boot_stats.h>
#endif

#define MAX_NUM_CH 8
struct stmmac_resources {
	void __iomem *addr;
	const char *mac;
	int irq[MAX_NUM_CH];
	u32 ch;
};

struct stmmac_tx_info {
	dma_addr_t buf;
	bool map_as_page;
	unsigned int len;
	bool last_segment;
};

/* Frequently used values are kept adjacent for cache effect */
struct stmmac_tx_queue {
	u32 tx_count_frames;
	struct timer_list txtimer;
	struct stmmac_priv *priv_data;
	struct dma_desc *dma_tx;
	struct sk_buff **tx_skbuff;
	struct stmmac_tx_info *tx_skbuff_dma;
	unsigned int cur_tx;
	unsigned int dirty_tx;
	dma_addr_t dma_tx_phy;
	u32 tx_tail_addr;
	u32 mss;
};

struct stmmac_rx_buffer {
	struct page *page;
	dma_addr_t addr;
};

struct stmmac_rx_queue {
	u32 rx_count_frames;
	struct page_pool *page_pool;
	struct stmmac_rx_buffer *buf_pool;
	struct stmmac_priv *priv_data;
	struct dma_desc *dma_rx ____cacheline_aligned_in_smp;
	unsigned int cur_rx;
	unsigned int dirty_rx;
	u32 rx_zeroc_thresh;
	dma_addr_t dma_rx_phy;
	u32 rx_tail_addr;
	unsigned int state_saved;
	struct {
		struct sk_buff *skb;
		unsigned int len;
		unsigned int error;
	} state;
};

struct stmmac_channel {
	struct napi_struct rx_napi ____cacheline_aligned_in_smp;
	struct napi_struct tx_napi ____cacheline_aligned_in_smp;
	struct stmmac_priv *priv_data;
};

enum filter_type {
	UNICAST_TYPE,
	MULTICAST_TYPE,
	VLAN_TYPE,
};

struct stmmac_priv {
	/* Frequently used values are kept adjacent for cache effect */
	u32 tx_coal_frames;
	u32 tx_coal_timer;
	u32 rx_coal_frames;
	bool tx_coal_timer_disable;

	int tx_coalesce;
	int hwts_tx_en;
	bool tso;

	unsigned int dma_buf_sz;
	unsigned int rx_copybreak;
	u32 rx_riwt;
	int hwts_rx_en;

	void __iomem *ioaddr;
	struct net_device *dev;
	struct device *device;
	struct mac_device_info *hw;
	int (*hwif_quirks)(struct stmmac_priv *priv);
	int (*add_filter)(struct net_device *ndev);
	int (*del_filter)(struct net_device *ndev);
	int (*mac_addr)(struct net_device *ndev);
	/* lock for priv data */
	struct mutex lock;

	/* RX Queue */
	struct stmmac_rx_queue rx_queue;

	/* TX Queue */
	struct stmmac_tx_queue tx_queue;

	/* Generic channel for NAPI */
	struct stmmac_channel channel;

	u32 queue;

	struct stmmac_extra_stats xstats ____cacheline_aligned_in_smp;
	struct plat_stmmacenet_data *plat;
#ifdef CONFIG_DEBUG_FS
	struct dentry *dbgfs_dir;
#endif
	u32 msg_enable;

	unsigned int mode;
	int use_riwt;
	bool boot_kpi;

	enum filter_type filter_type;
	u16 vid;
	__be16 proto;

	unsigned long emac_state;
	bool dev_opened;
	bool dev_inited;

	unsigned long state;
	struct workqueue_struct *wq;
	struct work_struct service_task;
};

enum stmmac_state {
	STMMAC_DOWN,
	STMMAC_RESET_REQUESTED,
	STMMAC_RESETTING,
	STMMAC_SERVICE_SCHED,
};

enum emac_state {
	EMAC_HW_DOWN_ST,
	EMAC_INIT_ST,
	EMAC_HW_UP_ST,
	EMAC_LINK_UP_ST,
};

#define GET_MEM_PDEV_DEV (priv->plat->stmmac_emb_smmu_ctx.valid ? \
			  &priv->plat->stmmac_emb_smmu_ctx.smmu_pdev->dev : \
			  priv->device)

void stmmac_set_ethtool_ops(struct net_device *netdev);

void stmmac_mac_link_down(struct net_device *ndev);
void stmmac_mac_link_up(struct net_device *ndev);
int stmmac_dvr_init(struct net_device *ndev);
void stmmac_ch_status(struct net_device *ndev);

int stmmac_resume(struct device *dev);
int stmmac_suspend(struct device *dev);
int stmmac_dvr_remove(struct device *dev);
int stmmac_dvr_probe(struct device *device,
		     struct plat_stmmacenet_data *plat_dat,
		     struct stmmac_resources *res);

#endif /* __STMMAC_THIN_H__ */
