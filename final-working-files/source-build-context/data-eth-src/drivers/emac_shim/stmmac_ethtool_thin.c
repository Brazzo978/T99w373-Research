// SPDX-License-Identifier: GPL-2.0-only
/******************************************************************************
 * STMMAC Ethtool support
 *
 * Copyright (C) 2007-2009  STMicroelectronics Ltd
 *
 * Author: Giuseppe Cavallaro <peppe.cavallaro@st.com>
 *****************************************************************************/

#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/interrupt.h>
#include <linux/io.h>

#include "stmmac_thin.h"
#include "hwif_thin.h"

#define GMAC_ETHTOOL_NAME	"st_gmac"

struct stmmac_stats {
	char stat_string[ETH_GSTRING_LEN];
	int sizeof_stat;
	int stat_offset;
};

#define STMMAC_STAT(m)	\
	{ #m, FIELD_SIZEOF(struct stmmac_extra_stats, m),	\
	offsetof(struct stmmac_priv, xstats.m)}

static const struct stmmac_stats stmmac_gstrings_stats[] = {
	/* Transmit errors */
	STMMAC_STAT(tx_underflow),
	STMMAC_STAT(tx_carrier),
	STMMAC_STAT(tx_losscarrier),
	STMMAC_STAT(vlan_tag),
	STMMAC_STAT(tx_deferred),
	STMMAC_STAT(tx_vlan),
	STMMAC_STAT(tx_jabber),
	STMMAC_STAT(tx_frame_flushed),
	STMMAC_STAT(tx_payload_error),
	STMMAC_STAT(tx_ip_header_error),
	/* Receive errors */
	STMMAC_STAT(rx_desc),
	STMMAC_STAT(sa_filter_fail),
	STMMAC_STAT(overflow_error),
	STMMAC_STAT(ipc_csum_error),
	STMMAC_STAT(rx_collision),
	STMMAC_STAT(rx_crc_errors),
	STMMAC_STAT(dribbling_bit),
	STMMAC_STAT(rx_length),
	STMMAC_STAT(rx_mii),
	STMMAC_STAT(rx_multicast),
	STMMAC_STAT(rx_gmac_overflow),
	STMMAC_STAT(rx_watchdog),
	STMMAC_STAT(da_rx_filter_fail),
	STMMAC_STAT(sa_rx_filter_fail),
	STMMAC_STAT(rx_missed_cntr),
	STMMAC_STAT(rx_overflow_cntr),
	STMMAC_STAT(rx_vlan),
	STMMAC_STAT(rx_split_hdr_pkt_n),
	/* Tx/Rx IRQ error info */
	STMMAC_STAT(tx_undeflow_irq),
	STMMAC_STAT(tx_process_stopped_irq),
	STMMAC_STAT(tx_jabber_irq),
	STMMAC_STAT(rx_overflow_irq),
	STMMAC_STAT(rx_buf_unav_irq),
	STMMAC_STAT(rx_process_stopped_irq),
	STMMAC_STAT(rx_watchdog_irq),
	STMMAC_STAT(tx_early_irq),
	STMMAC_STAT(fatal_bus_error_irq),
	/* Tx/Rx IRQ Events */
	STMMAC_STAT(rx_early_irq),
	STMMAC_STAT(threshold),
	STMMAC_STAT(tx_pkt_n),
	STMMAC_STAT(q_tx_pkt_n[0]),
	STMMAC_STAT(q_tx_pkt_n[1]),
	STMMAC_STAT(q_tx_pkt_n[2]),
	STMMAC_STAT(q_tx_pkt_n[3]),
	STMMAC_STAT(q_tx_pkt_n[4]),
	STMMAC_STAT(rx_pkt_n),
	STMMAC_STAT(q_rx_pkt_n[0]),
	STMMAC_STAT(q_rx_pkt_n[1]),
	STMMAC_STAT(q_rx_pkt_n[2]),
	STMMAC_STAT(q_rx_pkt_n[3]),
	STMMAC_STAT(normal_irq_n),
	STMMAC_STAT(rx_normal_irq_n),
	STMMAC_STAT(napi_poll),
	STMMAC_STAT(tx_normal_irq_n),
	STMMAC_STAT(tx_clean),
	STMMAC_STAT(tx_set_ic_bit),
	STMMAC_STAT(irq_receive_pmt_irq_n),
	/* Extended RDES status */
	STMMAC_STAT(ip_hdr_err),
	STMMAC_STAT(ip_payload_err),
	STMMAC_STAT(ip_csum_bypassed),
	STMMAC_STAT(ipv4_pkt_rcvd),
	STMMAC_STAT(ipv6_pkt_rcvd),
	STMMAC_STAT(timestamp_dropped),
	STMMAC_STAT(av_pkt_rcvd),
	STMMAC_STAT(av_tagged_pkt_rcvd),
	STMMAC_STAT(vlan_tag_priority_val),
	STMMAC_STAT(l3_filter_match),
	STMMAC_STAT(l4_filter_match),
	STMMAC_STAT(l3_l4_filter_no_match),
	/* DEBUG */
	STMMAC_STAT(mtl_tx_status_fifo_full),
	STMMAC_STAT(mtl_tx_fifo_not_empty),
	STMMAC_STAT(mmtl_fifo_ctrl),
	STMMAC_STAT(mtl_tx_fifo_read_ctrl_write),
	STMMAC_STAT(mtl_tx_fifo_read_ctrl_wait),
	STMMAC_STAT(mtl_tx_fifo_read_ctrl_read),
	STMMAC_STAT(mtl_tx_fifo_read_ctrl_idle),
	STMMAC_STAT(mac_tx_in_pause),
	STMMAC_STAT(mac_tx_frame_ctrl_xfer),
	STMMAC_STAT(mac_tx_frame_ctrl_idle),
	STMMAC_STAT(mac_tx_frame_ctrl_wait),
	STMMAC_STAT(mac_tx_frame_ctrl_pause),
	STMMAC_STAT(mac_gmii_tx_proto_engine),
	STMMAC_STAT(mtl_rx_fifo_fill_level_full),
	STMMAC_STAT(mtl_rx_fifo_fill_above_thresh),
	STMMAC_STAT(mtl_rx_fifo_fill_below_thresh),
	STMMAC_STAT(mtl_rx_fifo_fill_level_empty),
	STMMAC_STAT(mtl_rx_fifo_read_ctrl_flush),
	STMMAC_STAT(mtl_rx_fifo_read_ctrl_read_data),
	STMMAC_STAT(mtl_rx_fifo_read_ctrl_status),
	STMMAC_STAT(mtl_rx_fifo_read_ctrl_idle),
	STMMAC_STAT(mtl_rx_fifo_ctrl_active),
	STMMAC_STAT(mac_rx_frame_ctrl_fifo),
	STMMAC_STAT(mac_gmii_rx_proto_engine),
	/* TSO */
	STMMAC_STAT(tx_tso_frames),
	STMMAC_STAT(tx_tso_nfrags),
};

#define STMMAC_STATS_LEN ARRAY_SIZE(stmmac_gstrings_stats)

static void stmmac_ethtool_getdrvinfo(struct net_device *dev,
				      struct ethtool_drvinfo *info)
{
	strlcpy(info->driver, GMAC_ETHTOOL_NAME, sizeof(info->driver));

	strlcpy(info->version, DRV_MODULE_VERSION, sizeof(info->version));
}

static u32 stmmac_ethtool_getmsglevel(struct net_device *dev)
{
	struct stmmac_priv *priv = netdev_priv(dev);

	return priv->msg_enable;
}

static void stmmac_ethtool_setmsglevel(struct net_device *dev, u32 level)
{
	struct stmmac_priv *priv = netdev_priv(dev);

	priv->msg_enable = level;
}

static int stmmac_check_if_running(struct net_device *dev)
{
	if (!netif_running(dev))
		return -EBUSY;
	return 0;
}

static void stmmac_get_ethtool_stats(struct net_device *dev,
				     struct ethtool_stats *dummy, u64 *data)
{
	struct stmmac_priv *priv = netdev_priv(dev);
	int i, j = 0;

	stmmac_mac_debug(priv, priv->ioaddr, (void *)&priv->xstats,
			 priv->queue, priv->queue);

	for (i = 0; i < STMMAC_STATS_LEN; i++) {
		char *p = (char *)priv + stmmac_gstrings_stats[i].stat_offset;

		data[j++] = (stmmac_gstrings_stats[i].sizeof_stat ==
			     sizeof(u64)) ? (*(u64 *)p) : (*(u32 *)p);
	}
}

static int stmmac_get_sset_count(struct net_device *netdev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return STMMAC_STATS_LEN;
	default:
		return -EOPNOTSUPP;
	}
}

static void stmmac_get_strings(struct net_device *dev, u32 stringset, u8 *data)
{
	int i;
	u8 *p = data;

	switch (stringset) {
	case ETH_SS_STATS:
		for (i = 0; i < STMMAC_STATS_LEN; i++) {
			memcpy(p, stmmac_gstrings_stats[i].stat_string,
				ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}
		break;
	case ETH_SS_TEST:
		break;
	default:
		WARN_ON(1);
		break;
	}
}

static int stmmac_get_rxnfc(struct net_device *dev,
			    struct ethtool_rxnfc *rxnfc, u32 *rule_locs)
{
	switch (rxnfc->cmd) {
	case ETHTOOL_GRXRINGS:
		rxnfc->data = 1;
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static const struct ethtool_ops stmmac_ethtool_ops = {
	.begin = stmmac_check_if_running,
	.get_drvinfo = stmmac_ethtool_getdrvinfo,
	.get_msglevel = stmmac_ethtool_getmsglevel,
	.set_msglevel = stmmac_ethtool_setmsglevel,
	.get_ethtool_stats = stmmac_get_ethtool_stats,
	.get_strings = stmmac_get_strings,
	.get_sset_count	= stmmac_get_sset_count,
	.get_rxnfc = stmmac_get_rxnfc,
};

void stmmac_set_ethtool_ops(struct net_device *netdev)
{
	netdev->ethtool_ops = &stmmac_ethtool_ops;
}
