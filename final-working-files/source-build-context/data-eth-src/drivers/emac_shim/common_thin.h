/* SPDX-License-Identifier: GPL-2.0-only */
/******************************************************************************
 * STMMAC Common Header File
 *
 * Copyright (C) 2007-2009  STMicroelectronics Ltd
 *
 * Author: Giuseppe Cavallaro <peppe.cavallaro@st.com>
 *****************************************************************************/

#ifndef __COMMON_H__
#define __COMMON_H__

#include <linux/etherdevice.h>
#include <linux/stmmac.h>
#include <linux/module.h>

#include "hwif_thin.h"

#define STMMAC_VLAN_TAG_USED
/* These need to be power of two, and >= 4 */
#define DMA_TX_SIZE 512
#define DMA_RX_SIZE 512
#define STMMAC_GET_ENTRY(x, size)	(((x) + 1) & ((size) - 1))

#undef FRAME_FILTER_DEBUG
/* #define FRAME_FILTER_DEBUG */

/* Extra statistic and debug information exposed by ethtool */
struct stmmac_extra_stats {
	/* Transmit errors */
	unsigned long tx_underflow ____cacheline_aligned;
	unsigned long tx_carrier;
	unsigned long tx_losscarrier;
	unsigned long vlan_tag;
	unsigned long tx_deferred;
	unsigned long tx_vlan;
	unsigned long tx_jabber;
	unsigned long tx_frame_flushed;
	unsigned long tx_payload_error;
	unsigned long tx_ip_header_error;
	/* Receive errors */
	unsigned long rx_desc;
	unsigned long sa_filter_fail;
	unsigned long overflow_error;
	unsigned long ipc_csum_error;
	unsigned long rx_collision;
	unsigned long rx_crc_errors;
	unsigned long dribbling_bit;
	unsigned long rx_length;
	unsigned long rx_mii;
	unsigned long rx_multicast;
	unsigned long rx_gmac_overflow;
	unsigned long rx_watchdog;
	unsigned long da_rx_filter_fail;
	unsigned long sa_rx_filter_fail;
	unsigned long rx_missed_cntr;
	unsigned long rx_overflow_cntr;
	unsigned long rx_vlan;
	unsigned long rx_split_hdr_pkt_n;
	/* Tx/Rx IRQ error info */
	unsigned long tx_undeflow_irq;
	unsigned long tx_process_stopped_irq;
	unsigned long tx_jabber_irq;
	unsigned long rx_overflow_irq;
	unsigned long rx_buf_unav_irq;
	unsigned long rx_process_stopped_irq;
	unsigned long rx_watchdog_irq;
	unsigned long tx_early_irq;
	unsigned long fatal_bus_error_irq;
	/* Tx/Rx IRQ Events */
	unsigned long rx_early_irq;
	unsigned long threshold;
	unsigned long tx_pkt_n;
	unsigned long rx_pkt_n;
	unsigned long q_tx_pkt_n[MTL_MAX_TX_QUEUES];
	unsigned long q_rx_pkt_n[MTL_MAX_RX_QUEUES];
	unsigned long normal_irq_n;
	unsigned long rx_normal_irq_n;
	unsigned long napi_poll;
	unsigned long tx_normal_irq_n;
	unsigned long tx_clean;
	unsigned long tx_set_ic_bit;
	unsigned long irq_receive_pmt_irq_n;
	/* Extended RDES status */
	unsigned long ip_hdr_err;
	unsigned long ip_payload_err;
	unsigned long ip_csum_bypassed;
	unsigned long ipv4_pkt_rcvd;
	unsigned long ipv6_pkt_rcvd;
	unsigned long no_ptp_rx_msg_type_ext;
	unsigned long ptp_rx_msg_type_sync;
	unsigned long ptp_rx_msg_type_follow_up;
	unsigned long ptp_rx_msg_type_delay_req;
	unsigned long ptp_rx_msg_type_delay_resp;
	unsigned long ptp_rx_msg_type_pdelay_req;
	unsigned long ptp_rx_msg_type_pdelay_resp;
	unsigned long ptp_rx_msg_type_pdelay_follow_up;
	unsigned long ptp_rx_msg_type_announce;
	unsigned long ptp_rx_msg_type_management;
	unsigned long ptp_rx_msg_pkt_reserved_type;
	unsigned long ptp_frame_type;
	unsigned long ptp_ver;
	unsigned long timestamp_dropped;
	unsigned long av_pkt_rcvd;
	unsigned long av_tagged_pkt_rcvd;
	unsigned long vlan_tag_priority_val;
	unsigned long l3_filter_match;
	unsigned long l4_filter_match;
	unsigned long l3_l4_filter_no_match;
	/* debug register */
	unsigned long mtl_tx_status_fifo_full;
	unsigned long mtl_tx_fifo_not_empty;
	unsigned long mmtl_fifo_ctrl;
	unsigned long mtl_tx_fifo_read_ctrl_write;
	unsigned long mtl_tx_fifo_read_ctrl_wait;
	unsigned long mtl_tx_fifo_read_ctrl_read;
	unsigned long mtl_tx_fifo_read_ctrl_idle;
	unsigned long mac_tx_in_pause;
	unsigned long mac_tx_frame_ctrl_xfer;
	unsigned long mac_tx_frame_ctrl_idle;
	unsigned long mac_tx_frame_ctrl_wait;
	unsigned long mac_tx_frame_ctrl_pause;
	unsigned long mac_gmii_tx_proto_engine;
	unsigned long mtl_rx_fifo_fill_level_full;
	unsigned long mtl_rx_fifo_fill_above_thresh;
	unsigned long mtl_rx_fifo_fill_below_thresh;
	unsigned long mtl_rx_fifo_fill_level_empty;
	unsigned long mtl_rx_fifo_read_ctrl_flush;
	unsigned long mtl_rx_fifo_read_ctrl_read_data;
	unsigned long mtl_rx_fifo_read_ctrl_status;
	unsigned long mtl_rx_fifo_read_ctrl_idle;
	unsigned long mtl_rx_fifo_ctrl_active;
	unsigned long mac_rx_frame_ctrl_fifo;
	unsigned long mac_gmii_rx_proto_engine;
	/* TSO */
	unsigned long tx_tso_frames;
	unsigned long tx_tso_nfrags;
	/* DMA channel stats */
	unsigned long dma_ch_status;
	unsigned long dma_ch_intr_en;
	unsigned long dma_ch_tx_control;
	unsigned long dma_ch_txdesc_list_addr;
	unsigned long dma_ch_txdesc_ring_len;
	unsigned long dma_ch_curr_app_txdesc;
	unsigned long dma_ch_txdesc_tail_ptr;
	unsigned long dma_ch_curr_app_txbuf;
	unsigned long dma_ch_rx_control;
	unsigned long dma_ch_rxdesc_list_addr;
	unsigned long dma_ch_rxdesc_ring_len;
	unsigned long dma_ch_curr_app_rxdesc;
	unsigned long dma_ch_rxdesc_tail_ptr;
	unsigned long dma_ch_curr_app_rxbuf;
	unsigned long dma_ch_miss_frame_count;
};

#define SF_DMA_MODE 1		/* DMA STORE-AND-FORWARD Operation Mode */

#define DEFAULT_DMA_PBL		8

/* Max/Min RI Watchdog Timer count value */
#define MAX_DMA_RIWT		0xff
#define MIN_DMA_RIWT		0x10
/* Tx coalesce parameters */
#define STMMAC_COAL_TX_TIMER	1000
#define STMMAC_MAX_COAL_TX_TICK	100000
#define STMMAC_TX_MAX_FRAMES	256
#define STMMAC_TX_FRAMES	1
#define STMMAC_RX_FRAMES	25

/* Packets types */
enum packets_types {
	PACKET_AVCPQ = 0x1, /* AV Untagged Control packets */
	PACKET_PTPQ = 0x2, /* PTP Packets */
	PACKET_DCBCPQ = 0x3, /* DCB Control Packets */
	PACKET_UPQ = 0x4, /* Untagged Packets */
	PACKET_MCBCQ = 0x5, /* Multicast & Broadcast Packets */
};

/* Rx IPC status */
enum rx_frame_status {
	good_frame = 0x0,
	discard_frame = 0x1,
	csum_none = 0x2,
	llc_snap = 0x4,
	dma_own = 0x8,
	rx_not_ls = 0x10,
	ctxt_desc = 0x20,
};

/* Tx status */
enum tx_frame_status {
	tx_done = 0x0,
	tx_not_ls = 0x1,
	tx_err = 0x2,
	tx_dma_own = 0x4,
};

enum dma_irq_status {
	tx_hard_error = 0x1,
	tx_hard_error_bump_tc = 0x2,
	handle_rx = 0x4,
	handle_tx = 0x8,
};

#define CORE_IRQ_MTL_RX_OVERFLOW	BIT(8)

/* RX Buffer size must be multiple of 4/8/16 bytes */
#define BUF_SIZE_16KiB 16368
#define BUF_SIZE_8KiB 8188
#define BUF_SIZE_4KiB 4096
#define BUF_SIZE_2KiB 2048

#define STMMAC_RING_MODE	0x2

#define JUMBO_LEN		9000

/* VLAN */
#define STMMAC_VLAN_NONE	0x0
#define STMMAC_VLAN_REMOVE	0x1
#define STMMAC_VLAN_INSERT	0x2
#define STMMAC_VLAN_REPLACE	0x3

/* Basic descriptor structure for normal and alternate descriptors */
struct dma_desc {
	__le32 des0;
	__le32 des1;
	__le32 des2;
	__le32 des3;
};

struct mac_device_info {
	const struct stmmac_ops *mac;
	const struct stmmac_desc_ops *desc;
	const struct stmmac_dma_ops *dma;
	void __iomem *pcsr;     /* vpointer to device CSRs */
};

int dwmac4_setup(struct stmmac_priv *priv);

#endif /* __COMMON_H__ */
