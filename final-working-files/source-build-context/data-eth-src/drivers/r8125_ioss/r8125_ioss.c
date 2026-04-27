/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */

#include <linux/pci.h>

#include <linux/module.h>
#include <linux/device.h>
#include <linux/pm_wakeup.h>
#include <net/rtnetlink.h>

#include <linux/msm/ioss.h>

#include "r8125.h"
#include "r8125_lib.h"

#define MODC_UNIT_MASK 0x3000
#define MODC_UNIT_SHIFT 12

#define MODC_VAL_MASK 0x0F00
#define MODC_VAL_SHIFT 8

#define TC_MODE_MASK 0x0C00
#define TC_MODE_SHIFT 10

#define TIMER_MASK 0x00FF
#define TIMER_UNIT 2048

#define to_tp(idev) (((struct r8125_ioss_device *)((idev)->private))->_tp)

/* Diagnostic gates to isolate reboot trigger in r8125_ioss lifecycle */
static bool diag_skip_enable_channel;
module_param_named(skip_enable_channel, diag_skip_enable_channel, bool, 0644);
MODULE_PARM_DESC(skip_enable_channel,
	"Skip rtl8125_enable_ring() in enable_channel (diagnostic)");

static bool diag_skip_request_event;
module_param_named(skip_request_event, diag_skip_request_event, bool, 0644);
MODULE_PARM_DESC(skip_request_event,
	"Skip request_event setup/map in request_event (diagnostic)");

static bool diag_skip_enable_event;
module_param_named(skip_enable_event, diag_skip_enable_event, bool, 0644);
MODULE_PARM_DESC(skip_enable_event,
	"Skip rtl8125_enable_event()/filter enable in enable_event (diagnostic)");

static bool diag_skip_open_device;
module_param_named(skip_open_device, diag_skip_open_device, bool, 0644);
MODULE_PARM_DESC(skip_open_device,
	"Skip open_device setup/notifier registration (diagnostic)");

static bool diag_skip_request_channel;
module_param_named(skip_request_channel, diag_skip_request_channel, bool, 0644);
MODULE_PARM_DESC(skip_request_channel,
	"Skip request_channel ring allocation/map (diagnostic)");

enum RTL8125_registers_extra {
	R8125_TX_NEW_CTRL = 0x203E,
	R8125_TX_DESC_NEW_MOD = 0x0400,
	R8125_INT_MITI_V2_1_RX = 0x0A08,
	R8125_TDU_STATUS = 0x0D08,
	R8125_RDU_STATUS = 0x0D0A,
	R8125_PLA_TXQ0_IDLE_CREDIT = 0x2500,
	R8125_PLA_TXQ1_IDLE_CREDIT = 0x2504,
	R8125_RSS_KEY = 0x4600,
	R8125_RSS_I_TABLE = 0x4700,
};

struct __rtl8125_regs {
	ktime_t begin_ktime;
	ktime_t end_ktime;
	u64 duration_ns;

	u32 txq0_dsc_st_addr_0;
	u32 txq0_dsc_st_addr_2;
	u32 txq1_dsc_st_addr_0;
	u32 txq1_dsc_st_addr_2;
	u8 tppoll;
	u32 tcr;
	u8 command_register;
	u8 tx_desc_new_mod;

	u32 rxq0_dsc_st_addr_0;
	u32 rxq0_dsc_st_addr_2;
	u32 rxq1_dsc_st_addr_0;
	u32 rxq1_dsc_st_addr_2;
	u16 rms;
	u32 rcr;

	u32 phy_status;
	u16 cplus_cmd;

	u16 sw0_tail_ptr;
	u16 next_hwq0_clo_ptr;
	u16 sw1_tail_ptr;
	u16 next_hwq1_clo_ptr;
	u16 tc_mode;

	u16 int_miti_rxq0;
	u16 int_miti_txq0;
	u16 int_miti_rxq1;
	u16 int_miti_txq1;
	u8 int_config;
	u32 imr_new;
	u32 isr_new;

	u8 tdu_status;
	u16 rdu_status;

	u32 pla_tx_q0_idle_credit;
	u32 pla_tx_q1_idle_credit;

	u32 rss_ctrl;
	u8 rss_key[RTL8125_RSS_KEY_SIZE];
	u8 rss_i_table[RTL8125_MAX_INDIRECTION_TABLE_ENTRIES];
	u16 rss_queue_num_sel_r;
};

struct r8125_ioss_device {
	struct ioss_device *idev;
	struct rtl8125_private *_tp;
	struct notifier_block nb;
	struct rtl8125_counters stats;
	struct rtl8125_regs_save regs_save;
};

static int r8125_notifier_cb(struct notifier_block *nb,
		unsigned long action, void *data)
{
	struct r8125_ioss_device *rdev =
			container_of(nb, typeof(*rdev), nb);

	switch (action) {
	case RTL8125_NOTIFY_RESET_PREPARE:
		ioss_dev_log(rdev->idev, "RTL8125 reset prepare");
		break;
	case RTL8125_NOTIFY_RESET_COMPLETE:
		ioss_dev_log(rdev->idev, "RTL8125 reset complete");
		break;
	}

	return NOTIFY_DONE;
}

static int r8125_ioss_open_device(struct ioss_device *idev)
{
	struct r8125_ioss_device *rdev;

	ioss_dev_dbg(idev, "%s", __func__);

	if (diag_skip_open_device) {
		ioss_dev_err(idev,
			"DIAG: skip_open_device=1, bypassing open_device");
		return 0;
	}

	rdev = kzalloc(sizeof(*rdev), GFP_KERNEL);
	if (!rdev)
		return -ENOMEM;

	rdev->idev = idev;
	rdev->_tp = netdev_priv(idev->net_dev);

	rdev->nb.notifier_call = r8125_notifier_cb;
	if (rtl8125_register_notifier(idev->net_dev, &rdev->nb)) {
		ioss_dev_err(idev, "Failed to register with r8125 notifier");
		goto err_notif;
	}

	idev->private = rdev;

	return 0;

err_notif:
	kzfree(rdev);

	return -EFAULT;
}

static int r8125_ioss_close_device(struct ioss_device *idev)
{
	struct r8125_ioss_device *rdev = idev->private;

	ioss_dev_dbg(idev, "%s", __func__);

	rtl8125_unregister_notifier(idev->net_dev, &rdev->nb);
	kzfree(rdev);

	return 0;
}

static int r8125_ioss_request_channel(struct ioss_channel *ch)
{
	int i;
	int rc = 0;
	enum rtl8125_channel_dir direction =
			(ch->direction == IOSS_CH_DIR_RX) ?
				RTL8125_CH_DIR_RX : RTL8125_CH_DIR_TX;
	struct rtl8125_ring *ring;

	ioss_dev_log(ioss_ch_dev(ch), "%s, ring_size=%d, buf_size=%d, dir=%d",
			__func__, ch->config.ring_size, ch->config.buff_size,
			direction);

	if (diag_skip_request_channel) {
		ioss_dev_err(ioss_ch_dev(ch),
			"DIAG: skip_request_channel=1, bypassing request_channel");
		ch->id = -1;
		ch->private = NULL;
		return 0;
	}

	ring = rtl8125_request_ring(ioss_ch_dev(ch)->net_dev,
			ch->config.ring_size, ch->config.buff_size,
			direction, RTL8125_CONTIG_BUFS, NULL);
	if (!ring) {
		ioss_dev_err(ioss_ch_dev(ch), "Failed to request ring");
		return -EFAULT;
	}

	rc = ioss_channel_add_desc_mem(ch,
			ring->desc_addr, ring->desc_daddr, ring->desc_size);
	if (rc) {
		ioss_dev_err(ioss_ch_dev(ch), "Failed to add desc mem");
		goto err_add_desc;
	}

	for (i = 0; i < ring->ring_size; i++) {
		struct rtl8125_buf *buff = &ring->bufs[i];

		rc = ioss_channel_add_buff_mem(ch,
				buff->addr, buff->dma_addr, buff->size);
		if (rc) {
			ioss_dev_err(ioss_ch_dev(ch), "Failed to add buff mem");
			goto err_add_buff;
		}
	}

	ch->id = ring->queue_num;
	ch->private = ring;

	return 0;

err_add_buff:
	for (i = 0; i < ring->ring_size; i++)
		ioss_channel_del_buff_mem(ch, ring->bufs[i].addr);

	ioss_channel_del_desc_mem(ch, ring->desc_addr);

err_add_desc:
	rtl8125_release_ring(ring);

	ch->id = -1;
	ch->private = NULL;

	return rc;
}

static int r8125_ioss_release_channel(struct ioss_channel *ch)
{
	int i;
	struct rtl8125_ring *ring = ch->private;

	ioss_dev_log(ioss_ch_dev(ch), "Release ring %d", ring->queue_num);

	ioss_channel_del_desc_mem(ch, ring->desc_addr);

	for (i = 0; i < ring->ring_size; i++)
		ioss_channel_del_buff_mem(ch, ring->bufs[i].addr);

	rtl8125_release_ring(ring);

	ch->id = -1;
	ch->private = NULL;

	return 0;
}

static int r8125_ioss_enable_channel(struct ioss_channel *ch)
{
	struct rtl8125_ring *ring = ch->private;

	if (diag_skip_enable_channel) {
		ioss_dev_err(ioss_ch_dev(ch),
			"DIAG: skip_enable_channel=1, bypassing enable_channel");
		return 0;
	}

	return rtl8125_enable_ring(ring);
}

static int r8125_ioss_disable_channel(struct ioss_channel *ch)
{
	struct rtl8125_ring *ring = ch->private;

	rtl8125_disable_ring(ring);

	return 0;
}

static int r8125_ioss_request_event(struct ioss_channel *ch)
{
	int rc;
	int mod;
	struct rtl8125_ring *ring = ch->private;

	if (diag_skip_request_event) {
		ioss_dev_err(ioss_ch_dev(ch),
			"DIAG: skip_request_event=1, bypassing request_event");
		return 0;
	}

	ioss_dev_log(ioss_ch_dev(ch), "Request EVENT: paddr=%pap, DATA: %llu",
		&ch->event.paddr, ch->event.data);

	if (ioss_channel_map_event(ch))
		return -EFAULT;

	rc = rtl8125_request_event(ring, MSIX_event_type,
					ch->event.daddr, ch->event.data);
	if (rc) {
		ioss_dev_err(ioss_ch_dev(ch), "Failed to request event");
		goto err_req_event;
	}

	/* Each mod unit is 2048 ns (~2 uS). */
	mod = ch->event.mod_usecs_max / 2;

	ioss_dev_log(ioss_ch_dev(ch), "EVENT: mod=%d", mod);

	rc = rtl8125_set_ring_intr_mod(ring, mod);
	if (rc) {
		ioss_dev_err(ioss_ch_dev(ch),
			"Failed to set interrupt moderation");
		goto err_intr_mod;
	}

	return 0;

err_intr_mod:
	rtl8125_release_event(ring);
err_req_event:
	ioss_channel_unmap_event(ch);

	return -EFAULT;
}

static int r8125_ioss_release_event(struct ioss_channel *ch)
{
	struct rtl8125_ring *ring = ch->private;

	ioss_dev_log(ioss_ch_dev(ch), "Release EVENT: daddr=%pad, DATA: %llu",
			&ch->event.daddr, ch->event.data);

	rtl8125_release_event(ring);
	ioss_channel_unmap_event(ch);

	return 0;
}

static int __r8125_ioss_enable_filters(struct ioss_channel *ch)
{
	struct rtl8125_ring *ring = ch->private;
	struct net_device *net_dev = ioss_ch_dev(ch)->net_dev;
	enum ioss_filter_types filters = ch->filter_types;

	ioss_dev_log(ioss_ch_dev(ch), "Enabling filters %d", filters);

	if (!filters)
		return 0;

	if (ch->direction != IOSS_CH_DIR_RX)
		return -EFAULT;

	if (filters & IOSS_RXF_F_IP) {
		ioss_dev_log(ioss_ch_dev(ch), "Enabling RSS filter");

		if (rtl8125_rss_redirect(net_dev, 0, ring)) {
			ioss_dev_err(ioss_ch_dev(ch),
					"Failed to install rss filter");
			return -EFAULT;
		}

		filters &= ~IOSS_RXF_F_IP;
	}

	if (filters) {
		ioss_dev_err(ioss_ch_dev(ch), "Unsupported filters requested");
		rtl8125_rss_reset(net_dev);
		return -EFAULT;
	}

	return 0;
}

static int __r8125_ioss_disable_filters(struct ioss_channel *ch)
{
	struct net_device *net_dev = ioss_ch_dev(ch)->net_dev;
	enum ioss_filter_types filters = ch->filter_types;

	ioss_dev_log(ioss_ch_dev(ch), "Disabling filters %d", filters);

	if (!filters)
		return 0;

	if (ch->direction != IOSS_CH_DIR_RX)
		return -EFAULT;

	if (filters & IOSS_RXF_F_IP) {
		ioss_dev_log(ioss_ch_dev(ch), "Disabling RSS filter");

		if (rtl8125_rss_reset(net_dev)) {
			ioss_dev_err(ioss_ch_dev(ch), "Failed to reset rss filter");
			return -EFAULT;
		}

		filters &= ~IOSS_RXF_F_IP;
	}

	return 0;
}

static int r8125_ioss_enable_event(struct ioss_channel *ch)
{
	int rc;
	struct rtl8125_ring *ring = ch->private;

	if (diag_skip_enable_event) {
		ioss_dev_err(ioss_ch_dev(ch),
			"DIAG: skip_enable_event=1, bypassing enable_event");
		return 0;
	}

	rc = rtl8125_enable_event(ring);
	if (rc) {
		ioss_dev_err(ioss_ch_dev(ch), "Failed to enable event");
		return rc;
	}

	rc = __r8125_ioss_enable_filters(ch);
	if (rc) {
		ioss_dev_err(ioss_ch_dev(ch), "Failed to enable filters");
		rtl8125_disable_event(ring);
		return rc;
	}

	return 0;
}

static int r8125_ioss_disable_event(struct ioss_channel *ch)
{
	int rc;
	struct rtl8125_ring *ring = ch->private;

	rc = __r8125_ioss_disable_filters(ch);
	if (rc) {
		ioss_dev_err(ioss_ch_dev(ch), "Failed to disable filters");
		return rc;
	}

	rc = rtl8125_disable_event(ring);
	if (rc) {
		ioss_dev_err(ioss_ch_dev(ch), "Failed to disable event");
		return rc;
	}

	return 0;
}

static int r8125_save_regs(struct ioss_device *idev,
		void **regs, size_t *size)
{
	int rc = 0;
	struct r8125_ioss_device *rtldev = idev->private;

	rc = rtl8125_lib_get_stats(idev->net_dev, &rtldev->stats);
	if (rc)
		ioss_dev_err(idev, "Failed to get r8125 device statistics");

	rc = rtl8125_lib_save_regs(idev->net_dev, &rtldev->regs_save);
	if (rc)
		ioss_dev_err(idev, "Failed to save r8125 device registers");

	return rc;
}

#if IOSS_API_VER >= 3
static int r8125_ioss_device_statistics(struct ioss_device *idev,
					struct ioss_device_stats *statistics)
{
	int rc = 0;
	struct r8125_ioss_device *rtldev = idev->private;
	struct rtl8125_counters *stats = &rtldev->stats;

	rc = rtl8125_lib_get_stats(idev->net_dev, stats);
	if (rc) {
		ioss_dev_err(idev, "Failed to get r8125 device statistics");
		return rc;
	}

	statistics->emac_tx_packets = stats->tx_packets;
	statistics->emac_rx_packets = stats->rx_packets;
	statistics->emac_tx_errors = stats->tx_errors;
	statistics->emac_rx_errors = stats->rx_errors;
	statistics->emac_rx_bytes = stats->rx_octets;
	statistics->emac_tx_bytes = stats->tx_octets;
	statistics->emac_rx_drops = stats->rx_missed;
	statistics->emac_tx_drops = stats->tx_underrun32;
	statistics->emac_rx_pause_frames = stats->rx_pause_all;
	statistics->emac_tx_pause_frames = stats->tx_pause_all;

	return rc;
}

static int r8125_ioss_channel_statistics(struct ioss_channel *ch,
					 struct ioss_channel_stats *statistics)
{
	return 0;
}

static void get_ch_mod(struct ioss_channel *ch,
		       struct ioss_channel_status *status)
{
	struct rtl8125_private *tp = to_tp(ioss_ch_dev(ch));

	u16 ch_mod_reg = 0;
	u16 modc_unit = 0;
	u16 modc_val = 0;

	const u16 PKT_UNIT[4] = {1, 2, 4, 16};

	ch_mod_reg = (ch->direction == IOSS_CH_DIR_RX) ?
		RTL_R16(tp, (INT_MITI_V2_0_RX + (ch->id * 8))) :
		RTL_R16(tp, (INT_MITI_V2_0_TX + (ch->id * 8)));

	modc_unit = (ch_mod_reg & MODC_UNIT_MASK) >> MODC_UNIT_SHIFT;
	modc_val = (ch_mod_reg & MODC_VAL_MASK) >> MODC_VAL_SHIFT;

	status->interrupt_modc = modc_val * PKT_UNIT[modc_unit];
	status->interrupt_modt = (ch_mod_reg & TIMER_MASK) * TIMER_UNIT;
}

static void get_ch_enabled(struct ioss_channel *ch,
			   struct ioss_channel_status *status)
{
	struct rtl8125_private *tp = to_tp(ioss_ch_dev(ch));
	struct rtl8125_ring *ring = ch->private;

	if (ch->direction == IOSS_CH_DIR_RX)
		status->enabled = ring->enabled;
	else {
		u16 tx_new_ctrl1 = 0;
		u16 tc_mode = 0;

		tx_new_ctrl1 = RTL_R16(tp, R8125_TX_NEW_CTRL);
		tc_mode = (tx_new_ctrl1 & TC_MODE_MASK) >> TC_MODE_SHIFT;
		if (tc_mode == 1 || (tc_mode == 0 && ch->id == 0))
			status->enabled = true;
	}
}

static void get_ch_ring_size(struct ioss_channel *ch,
			     struct ioss_channel_status *status)
{
	struct rtl8125_ring *ring = ch->private;
	void *desc_mem = ring->desc_addr;

	u32 opts1 = (ch->direction == IOSS_CH_DIR_RX) ?
		((struct RxDescV3 *)desc_mem)[ring->ring_size-1].RxDescNormalDDWord4.opts1 :
		((struct TxDesc *)desc_mem)[ring->ring_size-1].opts1;

	/* Verify ring_size by checking if last descriptor is at End-of-Ring */
	if (opts1 & cpu_to_le32(RingEnd))
		status->ring_size = ring->ring_size;
}

static void get_ch_head_tail_ptr(struct ioss_channel *ch,
				 struct ioss_channel_status *status)
{
	if (ch->direction == IOSS_CH_DIR_TX) {
		struct rtl8125_private *tp = to_tp(ioss_ch_dev(ch));

		status->head_ptr = RTL_R16(tp, (SW_TAIL_PTR0_8125 + (ch->id * 4)));
		status->tail_ptr = RTL_R16(tp, (HW_CLO_PTR0_8125 + (ch->id * 4)));
	}
}

static int r8125_ioss_channel_status(struct ioss_channel *ch,
				     struct ioss_channel_status *status)
{
	get_ch_mod(ch, status);
	get_ch_enabled(ch, status);
	get_ch_ring_size(ch, status);
	get_ch_head_tail_ptr(ch, status);

	return 0;
}
#endif

static struct ioss_driver_ops r8125_ioss_ops = {
	.open_device = r8125_ioss_open_device,
	.close_device = r8125_ioss_close_device,

	.request_channel = r8125_ioss_request_channel,
	.release_channel = r8125_ioss_release_channel,

	.enable_channel = r8125_ioss_enable_channel,
	.disable_channel = r8125_ioss_disable_channel,

	.request_event = r8125_ioss_request_event,
	.release_event = r8125_ioss_release_event,

	.enable_event = r8125_ioss_enable_event,
	.disable_event = r8125_ioss_disable_event,

	.save_regs = r8125_save_regs,

#if IOSS_API_VER >= 3
	.get_device_statistics = r8125_ioss_device_statistics,
	.get_channel_statistics = r8125_ioss_channel_statistics,
	.get_channel_status = r8125_ioss_channel_status,
#endif
};

static bool r8125_driver_match(struct device *dev)
{
	ioss_log_dbg(NULL, "MATCH %s", dev_name(dev));

	/* Just match the driver name */
	return (dev->bus == &pci_bus_type) &&
		!strcmp(to_pci_driver(dev->driver)->name, MODULENAME);
}

static struct ioss_driver r8125_ioss_drv = {
	.name = MODULENAME "_ioss",
	.match = r8125_driver_match,
	.ops = &r8125_ioss_ops,
	.filter_types = IOSS_RXF_F_IP,
};

static int __init r8125_ioss_init(void)
{
	return ioss_pci_register_driver(&r8125_ioss_drv);

}
module_init(r8125_ioss_init);

static void __exit r8125_ioss_exit(void)
{
	ioss_pci_unregister_driver(&r8125_ioss_drv);
}
module_exit(r8125_ioss_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Realtek R8125 IOSS Glue Driver");
