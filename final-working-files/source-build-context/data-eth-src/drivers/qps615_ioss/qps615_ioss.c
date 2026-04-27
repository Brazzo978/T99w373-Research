/* Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * Copyright (c) 2021 The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/msm/ioss.h>
#include "tc956x_ipa_intf.h"
#include "tc956xmac.h"

struct qps615_ioss_device {
	struct ioss_device *idev;
	struct tc956xmac_priv *_priv;
	struct tc956x_regs regs_save;
};

static void *qps615_ioss_dma_alloc(struct ioss_device *idev,
			       size_t size, dma_addr_t *daddr, gfp_t gfp,
			       struct ioss_mem_allocator *alctr)
{
	return alctr->alloc(idev, size, daddr, gfp, alctr);
}

static void qps615_ioss_dma_free(struct ioss_device *idev,
			     size_t size, void *buf, dma_addr_t daddr,
			     struct ioss_mem_allocator *alctr)
{
	return alctr->free(idev, size, buf, daddr, alctr);
}

static void *qps615_ioss_alloc_descs(struct net_device *ndev, size_t size,
				 dma_addr_t *daddr, gfp_t gfp, struct mem_ops *mem_ops,
				 struct channel_info *ch_info)
{
	struct ioss_channel *ch = ch_info->client_ch_priv;

	return qps615_ioss_dma_alloc(ioss_ch_dev(ch), size, daddr, gfp,
			ch->config.desc_alctr);
}

static void *qps615_ioss_alloc_buf(struct net_device *ndev, size_t size,
			       dma_addr_t *daddr, gfp_t gfp,struct mem_ops *mem_ops,
			       struct channel_info *ch_info)
{
	struct ioss_channel *ch = ch_info->client_ch_priv;

	return qps615_ioss_dma_alloc(ioss_ch_dev(ch), size, daddr, gfp,
			ch->config.buff_alctr);
}

static void qps615_ioss_free_descs(struct net_device *ndev, void *buf, size_t size,
			       dma_addr_t *daddr, struct mem_ops *mem_ops, struct channel_info *ch_info)
{
	struct ioss_channel *ch = ch_info->client_ch_priv;

	return qps615_ioss_dma_free(ioss_ch_dev(ch), size, buf, *daddr,
			ch->config.desc_alctr);
}

static void qps615_ioss_free_buf(struct net_device *ndev, void *buf, size_t size,
			     dma_addr_t *daddr, struct mem_ops *mem_ops, struct channel_info *ch_info)
{
	struct ioss_channel *ch = ch_info->client_ch_priv;

	return qps615_ioss_dma_free(ioss_ch_dev(ch), size, buf, *daddr,
			ch->config.buff_alctr);
}

static int qps615_ioss_open_device(struct ioss_device *idev)
{
	struct qps615_ioss_device *qps615dev;

	ioss_dev_dbg(idev, "%s", __func__);

	qps615dev = kzalloc(sizeof(*qps615dev), GFP_KERNEL);
	if (!qps615dev)
		return -ENOMEM;

	qps615dev->idev = idev;
	qps615dev->_priv = netdev_priv(idev->net_dev);

	idev->private = qps615dev;

	return 0;
}

static int qps615_ioss_close_device(struct ioss_device *idev)
{
	struct qps615_ioss_device *qps615dev = idev->private;

	ioss_dev_dbg(idev, "%s", __func__);

	kzfree(qps615dev);

	return 0;
}

static int qps615_ioss_request_channel(struct ioss_channel *ch)
{
	int i;
	int rc = -EFAULT;
	struct request_channel_input ipa_channel_info;
	enum channel_dir direction =
			(ch->direction == IOSS_CH_DIR_RX) ?
				CH_DIR_RX : CH_DIR_TX;
	struct channel_info *ring;

	ioss_dev_log(ioss_ch_dev(ch), "%s, ring_size=%d, buf_size=%d, dir=%d",
		     __func__, ch->config.ring_size, ch->config.buff_size,
			ch->direction);


	ipa_channel_info.mem_ops = kzalloc(sizeof(*ipa_channel_info.mem_ops), GFP_KERNEL);
	if (!ipa_channel_info.mem_ops)
		return -ENOMEM;

	ipa_channel_info.mem_ops->alloc_descs = qps615_ioss_alloc_descs;
	ipa_channel_info.mem_ops->alloc_buf = qps615_ioss_alloc_buf;
	ipa_channel_info.mem_ops->free_descs = qps615_ioss_free_descs;
	ipa_channel_info.mem_ops->free_buf = qps615_ioss_free_buf;
	ipa_channel_info.client_ch_priv = ch;

	ipa_channel_info.ndev = ioss_ch_dev(ch)->net_dev;
	ipa_channel_info.desc_cnt = ch->config.ring_size;
	ipa_channel_info.ch_dir = direction;
	ipa_channel_info.buf_size = ch->config.buff_size;
	ipa_channel_info.ch_flags = TC956X_CONTIG_BUFS;
	ipa_channel_info.flags = GFP_KERNEL;
	ring = request_channel(&ipa_channel_info);

	if (!ring) {
		ioss_dev_err(ioss_ch_dev(ch), "Failed to request ring\n");
		kzfree(ipa_channel_info.mem_ops);
		return -ENOMEM;
	}

	ch->tail_ptr_addr = ipa_channel_info.tail_ptr_addr;

	rc = ioss_channel_add_desc_mem(ch,
			ring->desc_addr.desc_virt_addrs_base,
			ring->desc_addr.desc_dma_addrs_base, ring->desc_size*ring->desc_cnt);

	if (rc) {
		ioss_dev_err(ioss_ch_dev(ch), "Failed to add desc mem\n");
		goto release_channel;
	}

	for (i = 0; i < ring->desc_cnt; i++) {
		void *vaddr = (void *)ring->buff_pool_addr.buff_pool_va_addrs_base[0];
		void *addr = vaddr + (ring->buf_size * i);
		dma_addr_t daddr = ring->buff_pool_addr.buff_pool_dma_addrs_base[0] +
			(ring->buf_size * i);

		rc = ioss_channel_add_buff_mem(ch,
				addr,
				daddr,
				ring->buf_size);

		if (rc) {
			ioss_dev_err(ioss_ch_dev(ch), "Failed to add buff mem\n");
			goto release_desc;
		}
	}

	ch->id = ring->channel_num;
	ch->private = ring;

	return 0;

release_desc:
	ioss_channel_del_desc_mem(ch, ring->desc_addr.desc_virt_addrs_base);
	for (i = 0; i < ring->desc_cnt; i++) {
		void *vaddr = (void *)ring->buff_pool_addr.buff_pool_va_addrs_base[0];
		void *addr = vaddr + (ring->buf_size * i);
		ioss_channel_del_buff_mem(ch, addr);
	}
release_channel:
	release_channel(ioss_ch_dev(ch)->net_dev,ring);
	kzfree(ipa_channel_info.mem_ops);
	return -ENOMEM;
}

static int qps615_ioss_release_channel(struct ioss_channel *ch)
{
	int i;
	struct channel_info *ring = ch->private;
	struct mem_ops *mem_ops = ring->mem_ops;

	ioss_dev_log(ioss_ch_dev(ch), "Release ring %d\n", ring->channel_num);

	ioss_channel_del_desc_mem(ch, ring->desc_addr.desc_virt_addrs_base);

	for (i = 0; i < ring->desc_cnt; i++) {
		void *vaddr = (void *)ring->buff_pool_addr.buff_pool_va_addrs_base[0];
		void *addr = vaddr + (ring->buf_size * i);
		ioss_channel_del_buff_mem(ch, addr);
	}

	release_channel(ioss_ch_dev(ch)->net_dev,ring);
	kzfree(mem_ops);

	ch->id = -1;
	ch->private = NULL;

	return 0;
}

static int qps615_ioss_enable_channel(struct ioss_channel *ch)
{
	struct channel_info *ring = ch->private;

	return start_channel(ioss_ch_dev(ch)->net_dev,ring);
}

static int qps615_ioss_disable_channel(struct ioss_channel *ch)
{
	struct channel_info *ring = ch->private;

	return stop_channel(ioss_ch_dev(ch)->net_dev,ring);
}

static int qps615_ioss_request_event(struct ioss_channel *ch)
{
	int rc;
	int wdt;
	struct channel_info *ring = ch->private;

	ioss_dev_log(ioss_ch_dev(ch), "Request EVENT: paddr=%pad, DATA: %llu\n",
		&ch->event.paddr, ch->event.data);

	rc = request_event(ioss_ch_dev(ch)->net_dev,ring,
						ch->event.paddr);
	if (rc) {
		ioss_dev_err(ioss_ch_dev(ch), "Failed to request event\n");
		return rc;
	}

	/*call for rx and not tx direction */
	if(ch->direction == IOSS_CH_DIR_RX)
	{
		/* Each wdt unit is 2048 ns (~2 uS). */
		wdt = ch->event.mod_usecs_max / 2;
		ioss_dev_log(ioss_ch_dev(ch),"EVENT: wdt=%d\n", wdt);

		rc = set_event_mod(ioss_ch_dev(ch)->net_dev,ring, wdt);
		if (rc) {
			ioss_dev_err(ioss_ch_dev(ch), "Failed to set interrupt moderation\n");
			release_event(ioss_ch_dev(ch)->net_dev,ring);
			return rc;
		}
	}

	return 0;
}

static int qps615_ioss_release_event(struct ioss_channel *ch)
{
	struct channel_info *ring = ch->private;

	ioss_dev_log(ioss_ch_dev(ch), "Release EVENT: daddr=%pad, DATA: %llu\n",
		&ch->event.paddr, ch->event.data);

	release_event(ioss_ch_dev(ch)->net_dev,ring);

	return 0;
}

enum {
	FLT_TYPE_IP4,
	FLT_TYPE_IP6,
	FLT_TYPE_VLAN,

	/* Must be the last entry */
	FLT_NUM_TYPES,
};

static int qps615_set_filter_info(struct rx_filter_info *filter_info)
{
	if (ARRAY_SIZE(filter_info->entries) < FLT_NUM_TYPES)
		return -EFAULT;

	/* Number of filter installed*/
	filter_info->npe = FLT_NUM_TYPES;
	filter_info->nve = FLT_NUM_TYPES;

	/* Ipv4 ether type 0x0800
	 * Only Check for 0x08 and 0x00
	 */
	filter_info->entries[FLT_TYPE_IP4].match_data = 0x00000008;
	filter_info->entries[FLT_TYPE_IP4].match_en = 0x0000FFFF;
	filter_info->entries[FLT_TYPE_IP4].af = 1;
	filter_info->entries[FLT_TYPE_IP4].rf = 0;
	filter_info->entries[FLT_TYPE_IP4].im = 0;
	filter_info->entries[FLT_TYPE_IP4].nc = 0;
	filter_info->entries[FLT_TYPE_IP4].res1 = 0;
	filter_info->entries[FLT_TYPE_IP4].frame_offset = 3;
	filter_info->entries[FLT_TYPE_IP4].res2 = 0;
	filter_info->entries[FLT_TYPE_IP4].ok_index = 0;
	filter_info->entries[FLT_TYPE_IP4].res3 = 0;
	filter_info->entries[FLT_TYPE_IP4].dma_ch_no = 2;
	filter_info->entries[FLT_TYPE_IP4].res4 = 0;

	/* Ipv6 ether type 0x86DD
	 * Only Check for 0x86 and 0xDD
	 */
	filter_info->entries[FLT_TYPE_IP6].match_data = 0x0000DD86;
	filter_info->entries[FLT_TYPE_IP6].match_en = 0x0000FFFF;
	filter_info->entries[FLT_TYPE_IP6].af = 1;
	filter_info->entries[FLT_TYPE_IP6].rf = 0;
	filter_info->entries[FLT_TYPE_IP6].im = 0;
	filter_info->entries[FLT_TYPE_IP6].nc = 0;
	filter_info->entries[FLT_TYPE_IP6].res1 = 0;
	filter_info->entries[FLT_TYPE_IP6].frame_offset = 3;
	filter_info->entries[FLT_TYPE_IP6].res2 = 0;
	filter_info->entries[FLT_TYPE_IP6].ok_index = 0;
	filter_info->entries[FLT_TYPE_IP6].res3 = 0;
	filter_info->entries[FLT_TYPE_IP6].dma_ch_no = 2;
	filter_info->entries[FLT_TYPE_IP6].res4 = 0;

	/* VLAN  ether type 0x8100
	 * Only Check for 0x81 and 0x00
	 */
	filter_info->entries[FLT_TYPE_VLAN].match_data = 0x00000081;
	filter_info->entries[FLT_TYPE_VLAN].match_en = 0x0000FFFF;
	filter_info->entries[FLT_TYPE_VLAN].af = 1;
	filter_info->entries[FLT_TYPE_VLAN].rf = 0;
	filter_info->entries[FLT_TYPE_VLAN].im = 0;
	filter_info->entries[FLT_TYPE_VLAN].nc = 0;
	filter_info->entries[FLT_TYPE_VLAN].res1 = 0;
	filter_info->entries[FLT_TYPE_VLAN].frame_offset = 3;
	filter_info->entries[FLT_TYPE_VLAN].res2 = 0;
	filter_info->entries[FLT_TYPE_VLAN].ok_index = 0;
	filter_info->entries[FLT_TYPE_VLAN].res3 = 0;
	filter_info->entries[FLT_TYPE_VLAN].dma_ch_no = 2;
	filter_info->entries[FLT_TYPE_VLAN].res4 = 0;

	return 0;
}

static int __qps615_ioss_enable_filters(struct ioss_channel *ch)
{
	struct net_device *net_dev = ioss_ch_dev(ch)->net_dev;
	enum ioss_filter_types filters = ch->filter_types;
	struct rx_filter_info filter_info;

	memset(&filter_info, 0, sizeof(filter_info));

	if (!filters)
		return 0;

	if (ch->direction != IOSS_CH_DIR_RX)
		return -EFAULT;

	if (filters & IOSS_RXF_F_IP) {
		ioss_dev_log(ioss_ch_dev(ch), "Enabling RSS filter\n");

		clear_rx_filter(net_dev);

		if (qps615_set_filter_info(&filter_info)) {
			ioss_dev_err(ioss_ch_dev(ch),
					"Failed to set FRP filters");
			return -EFAULT;
		}

		if (set_rx_filter(net_dev, &filter_info)) {
			ioss_dev_err(ioss_ch_dev(ch), "Failed to install rss filter\n");
			return -EFAULT;
		}

		filters &= ~IOSS_RXF_F_IP;
	}

	if (filters) {
		ioss_dev_err(ioss_ch_dev(ch), "Unsupported filters requested\n");
		clear_rx_filter(net_dev);
		return -EFAULT;
	}

	return 0;
}

static int __qps615_ioss_disable_filters(struct ioss_channel *ch)
{
	struct net_device *net_dev = ioss_ch_dev(ch)->net_dev;
	enum ioss_filter_types filters = ch->filter_types;

	if (!filters)
		return 0;

	if (ch->direction != IOSS_CH_DIR_RX)
		return -EFAULT;

	if (filters & IOSS_RXF_F_IP) {
		ioss_dev_log(ioss_ch_dev(ch), "Disabling RSS filter\n");

		if (clear_rx_filter(net_dev)) {
			ioss_dev_err(ioss_ch_dev(ch), "Failed to reset rss filter\n");
			return -EFAULT;
		}

		filters &= ~IOSS_RXF_F_IP;
	}

	return 0;
}

static int qps615_ioss_enable_event(struct ioss_channel *ch)
{
	int rc;
	struct channel_info *ring = ch->private;

	rc = enable_event(ioss_ch_dev(ch)->net_dev, ring);
	if (rc)
	{
		ioss_dev_err(ioss_ch_dev(ch), "Failed to enable event\n");
		return rc;
	}

	rc = __qps615_ioss_enable_filters(ch);
	if (rc) {
		ioss_dev_err(ioss_ch_dev(ch), "Failed to enable filters\n");
		disable_event(ioss_ch_dev(ch)->net_dev, ring);
		return rc;
	}

	return 0;
}

static int qps615_ioss_disable_event(struct ioss_channel *ch)
{
	int rc;
	struct channel_info *ring = ch->private;

	rc = __qps615_ioss_disable_filters(ch);
	if (rc) {
		ioss_dev_err(ioss_ch_dev(ch), "Failed to disable filters\n");
		return rc;
	}

	rc = disable_event(ioss_ch_dev(ch)->net_dev, ring);
	if (rc)
	{
		ioss_dev_err(ioss_ch_dev(ch), "Failed to disable event\n");
		return rc;
	}

	return 0;
}

static u64 __get_stats_data(const char *name, u64 data[], const u8 *strings_data, int scount)
{
	int i = 0;
	const char (*strings)[ETH_GSTRING_LEN] = (typeof(strings))strings_data;

	/* iterate through strings[], find matchging index */
	for (i = 0; i < scount; i++) {
		if (strcmp(name, strings[i]) == 0)
			return data[i];
	}

	return 0;
}

static int qps615_ioss_device_statistics(struct ioss_device *idev,
		struct ioss_device_stats *statistics)
{
	u64 *data;
	u8 *strings;
	int strings_count = 0;
	struct ethtool_stats stats;
	const struct ethtool_ops *ops = idev->net_dev->ethtool_ops;

	if (ops == NULL || ops->get_sset_count == NULL ||
		ops->get_ethtool_stats == NULL || ops->get_strings == NULL)
		return -EOPNOTSUPP;

	strings_count = ops->get_sset_count(idev->net_dev, ETH_SS_STATS);

	data = kcalloc(strings_count, sizeof(u64), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	strings = kcalloc(strings_count, ETH_GSTRING_LEN, GFP_KERNEL);
	if (!strings) {
		kfree(data);
		return -ENOMEM;
	}

	memset(&stats, 0, sizeof(stats));
	stats.n_stats = strings_count;

	rtnl_lock();
	ops->get_ethtool_stats(idev->net_dev, &stats, data);
	rtnl_unlock();

	ops->get_strings(idev->net_dev, ETH_SS_STATS, strings);

	statistics->emac_rx_packets =
		__get_stats_data("mmc_rx_framecount_gb", data, strings, strings_count);
	statistics->emac_tx_packets =
		__get_stats_data("mmc_tx_framecount_gb", data, strings, strings_count);
	statistics->emac_rx_bytes =
		__get_stats_data("mmc_rx_octetcount_gb", data, strings, strings_count);
	statistics->emac_tx_bytes =
		__get_stats_data("mmc_tx_octetcount_gb", data, strings, strings_count);
	statistics->emac_rx_drops =
		__get_stats_data("mmc_rx_fifo_overflow", data, strings, strings_count);
	statistics->emac_rx_pause_frames =
		__get_stats_data("mmc_rx_pause_frames", data, strings, strings_count);
	statistics->emac_tx_pause_frames =
		__get_stats_data("mmc_tx_pause_frame", data, strings, strings_count);

	kfree(data);
	kfree(strings);

	return 0;
}

static int qps615_ioss_channel_statistics(struct ioss_channel *ch,
		struct ioss_channel_stats *statistics)
{
	struct ioss_device *idev = ioss_ch_dev(ch);
	struct tc956xmac_priv *priv = netdev_priv(idev->net_dev);

	if (ch->direction == IOSS_CH_DIR_RX) {
		statistics->desc_unavail = priv->xstats.rx_buf_unav_irq[ch->id];
		statistics->overflow_error = priv->xstats.overflow_error;
	} else {
		statistics->underflow_error = priv->xstats.tx_underflow;
	}

	return 0;
}

static int qps615_ioss_channel_status(struct ioss_channel *ch, struct ioss_channel_status *status)
{
	u64 *data;
	int strings_count = 0;
	struct ethtool_stats stats;
	struct ioss_device *idev = ioss_ch_dev(ch);
	const struct ethtool_ops *ops = idev->net_dev->ethtool_ops;
	struct tc956xmac_priv *priv = netdev_priv(idev->net_dev);

	if (ops == NULL || ops->get_sset_count == NULL ||
		ops->get_ethtool_stats == NULL || ops->get_strings == NULL)
		return -EOPNOTSUPP;

	strings_count = ops->get_sset_count(idev->net_dev, ETH_SS_STATS);

	data = kcalloc(strings_count, sizeof(u64), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	memset(&stats, 0, sizeof(stats));
	stats.n_stats = strings_count;

	rtnl_lock();
	ops->get_ethtool_stats(idev->net_dev, &stats, data);
	rtnl_unlock();

	if (ch->direction == IOSS_CH_DIR_RX) {
		status->ring_size = priv->xstats.rxch_desc_ring_len[ch->id];
		status->head_ptr = priv->xstats.rxch_desc_list_laddr[ch->id];
		status->tail_ptr = priv->xstats.rxch_desc_tail[ch->id];
	} else {
		status->ring_size = priv->xstats.txch_desc_ring_len[ch->id];
		status->head_ptr = priv->xstats.txch_desc_list_laddr[ch->id];
		status->tail_ptr = priv->xstats.txch_desc_tail[ch->id];
	}
	kfree(data);

	return 0;
}

static int qps615_save_regs(struct ioss_device *idev,
		void **regs, size_t *size)
{
	int rc = 0;
	struct qps615_ioss_device *qps615dev = idev->private;
	struct tc956x_regs *regs_save = &qps615dev->regs_save;

	memset(regs_save, 0, sizeof(*regs_save));

	rc = tc956x_dump_regs(idev->net_dev, regs_save);
	if (rc) {
		ioss_dev_err(idev, "Failed to save qps615 device registers");
		return rc;
	}

	if (regs)
		*regs = regs_save;

	if (size)
		*size = sizeof(*regs_save);

	return rc;
}

static struct ioss_driver_ops qps615_ioss_ops = {
	.open_device = qps615_ioss_open_device,
	.close_device = qps615_ioss_close_device,

	.request_channel = qps615_ioss_request_channel,
	.release_channel = qps615_ioss_release_channel,

	.enable_channel = qps615_ioss_enable_channel,
	.disable_channel = qps615_ioss_disable_channel,

	.request_event = qps615_ioss_request_event,
	.release_event = qps615_ioss_release_event,

	.enable_event = qps615_ioss_enable_event,
	.disable_event = qps615_ioss_disable_event,

	.get_device_statistics = qps615_ioss_device_statistics,
	.get_channel_statistics = qps615_ioss_channel_statistics,
	.get_channel_status = qps615_ioss_channel_status,

	.save_regs = qps615_save_regs,
};

bool qps615_driver_match(struct device *dev)
{
	/* Just match the driver name */
	return (dev->bus == &pci_bus_type) &&
		!strcmp(to_pci_driver(dev->driver)->name, TC956X_RESOURCE_NAME);
}

static struct ioss_driver qps615_ioss_drv = {
	.name = "qps615_ioss",
	.match = qps615_driver_match,
	.ops = &qps615_ioss_ops,
	.filter_types = IOSS_RXF_F_IP,
};

static int __init qps615_ioss_init(void)
{
	return ioss_pci_register_driver(&qps615_ioss_drv);

}
module_init(qps615_ioss_init);

static void __exit qps615_ioss_exit(void)
{
	return ioss_pci_unregister_driver(&qps615_ioss_drv);
}
module_exit(qps615_ioss_exit);

MODULE_DESCRIPTION("QPS615 IOSS Glue Driver");
MODULE_LICENSE("GPL v2");
