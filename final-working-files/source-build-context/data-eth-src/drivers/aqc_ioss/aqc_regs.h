/* Copyright (c) 2019,2021 The Linux Foundation. All rights reserved.
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

#ifndef __AQC_REGS_H__
#define __AQC_REGS_H__

#ifndef __LITTLE_ENDIAN_BITFIELD
#error Big endian bit field is not supported
#endif

#include <linux/ktime.h>

#define AQC_RX_TAIL_PTR_OFFSET 0x00005B10
#define AQC_RX_TAIL_PTR(base, idx) \
	(base + AQC_RX_TAIL_PTR_OFFSET + (idx * 0x20))

#define AQC_RX_HEAD_PTR_OFFSET 0x00005B0C
#define AQC_RX_HEAD_PTR(base, idx) \
	(base + AQC_RX_HEAD_PTR_OFFSET + (idx * 0x20))

#define AQC_TX_TAIL_PTR_OFFSET 0x00007C10
#define AQC_TX_TAIL_PTR(base, idx) \
	(base + AQC_TX_TAIL_PTR_OFFSET + (idx * 0x40))

#define AQC_TX_HEAD_PTR_OFFSET 0x00007C0C
#define AQC_TX_HEAD_PTR(base, idx) \
	(base + AQC_TX_HEAD_PTR_OFFSET + (idx * 0x40))

struct aqc_reg_rx_ctrl1 {
	unsigned reset:1;
	unsigned par_chk_sense:1;
	unsigned reg_reset_dsbl:1;
	unsigned rsvd1:20;
	unsigned tpo_rpf_sys_loopback:1;
	unsigned pkt_sys_loopback:1;
	unsigned dma_sys_loopback:1;
	unsigned pkt_net_loopback:1;
	unsigned dma_net_loopback:1;
	unsigned passthru_en:1;
	unsigned rx_phy_mode:2;
	unsigned rsvd0:1;
} __packed;

struct aqc_reg_rx_ctrl2 {
	unsigned rpf_dp_clk_en:1;
	unsigned rpf_clk_en:1;
	unsigned rpo_dp_clk_en:1;
	unsigned rpo_clk_en:1;
	unsigned rpb_dp_clk_en:1;
	unsigned rpb_clk_en:1;
	unsigned rdm_dp_clk_en:1;
	unsigned rdm_clk_en:1;
	unsigned rps_dp_clk_en:1;
	unsigned rps_clk_en:1;
	unsigned rro_dp_clk_en:1;
	unsigned rro_clk_en:1;
	unsigned rpf2_dp_clk_en:1;
	unsigned rpf2_clk_en:1;
	unsigned rsvd0:18;
} __packed;

struct aqc_reg_rxf_ctrl1 {
	unsigned l2_bc_en:1;
	unsigned rsvd0:2;
	unsigned l2_promis_mode:1;
	unsigned byte_swap:1;
	unsigned rsvd1:3;
	unsigned l2_bc_rst:1;
	unsigned rsvd2:3;
	unsigned l2_bc_act:3;
	unsigned l2_bc_mng_rxq:1;
	unsigned l2_bc_thresh:16;
} __packed;

struct aqc_reg_rxf_ctrl2 {
	unsigned rpf_dont_split_l4_other_opt:1;
	unsigned pif_rpf_dont_split_tcp_udp_opt:1;
	unsigned pif_rpf_sctp_icmp_is_l4_other:1;
	unsigned pif_rpf_dont_split_ipv6_ext_hdr:1;
	unsigned pif_rpf_dont_split_ipv4_frag:1;
	unsigned pif_rpf_dont_split_non_ip:1;
	unsigned pif_rpf_ipv6_ext_hdr_parse_en_i:1;
	unsigned rsvd0:25;
} __packed;

struct aqc_reg_rxf_bcf_status {
	unsigned l2_bc_detopt:1;
	unsigned rsvd0:31;
} __packed;

struct aqc_reg_rxf_bcf_count {
	unsigned l2_bc_cnt:16;
	unsigned rsvd0:16;
} __packed;

struct aqc_reg_rxf_ucf1 {
	unsigned l2_uc_da0_LSW:32;
} __packed;

struct aqc_reg_rxf_ucf2 {
	unsigned l2_uc_da0_MSW:16;
	unsigned l2_uc_act0:3;
	unsigned l2_uc_mng_rxq0:1;
	unsigned rsvd0:11;
	unsigned l2_uc_en0:1;
} __packed;

struct aqc_reg_rxf_mcf {
	unsigned l2_mc_da0_pat:12;
	unsigned rsvd0:4;
	unsigned l2_mc_act0:3;
	unsigned l2_mc_mng_rxq0:1;
	unsigned rsvd1:11;
	unsigned l2_mc_en0:1;
} __packed;

struct aqc_reg_rxf_mcf_msk {
	unsigned l2_mc_da_msk:12;
	unsigned l2_mc_da_loc:2;
	unsigned l2_mc_accept_all:1;
	unsigned rsvd0:17;
} __packed;

struct aqc_reg_rxf_vlan_ctrl1 {
	unsigned vl_double_en:1;
	unsigned vl_promis_mode:1;
	unsigned vl_accept_untagged_mode:1;
	unsigned vl_untagged_ac:3;
	unsigned vl_untagged_rxq:5;
	unsigned vl_untagged_mng_rxq:1;
	unsigned vl_untagged_rxq_en:1;
	unsigned rsvd0:19;
} __packed;

struct aqc_reg_rxf_vlan_ctrl2 {
	unsigned vl_inner_tpid:16;
	unsigned vl_outer_tpid:16;
} __packed;

struct aqc_reg_rxf_vlan {
	unsigned vl_id0:12;
	unsigned rsvd0:4;
	unsigned vl_act0:3;
	unsigned vl_mng_rxq0:1;
	unsigned vl_rxq0:5;
	unsigned rsvd1:3;
	unsigned vl_rxq0_en:1;
	unsigned vl_cfi0:1;
	unsigned vl_cfi0_en:1;
	unsigned vl_en0:1;
} __packed;

struct aqc_reg_rxf_ether {
	unsigned et_val0:16;
	unsigned et_act0:3;
	unsigned et_mng_rxq0:1;
	unsigned et_rxq0:5;
	unsigned rsvd0:1;
	unsigned et_up0:3;
	unsigned et_rxq0_en:1;
	unsigned et_up0_en:1;
	unsigned et_en0:1;
} __packed;

struct aqc_reg_rxf_l3l4 {
	unsigned l4_prot0:3;
	unsigned rsvd0:5;
	unsigned l3_l4_rxq0:5;
	unsigned rsvd1:3;
	unsigned l3_l4_act0:3;
	unsigned rsvd2:3;
	unsigned l3_l4_mng_rxq0:1;
	unsigned l3_l4_rxq0_en:1;
	unsigned l3_arp0_en:1;
	unsigned l4_prot0_en:1;
	unsigned l4_dp0_en:1;
	unsigned l4_sp0_en:1;
	unsigned l3_da0_en:1;
	unsigned l3_sa0_en:1;
	unsigned l3_v6_en0:1;
	unsigned l3_l4_en0:1;
} __packed;

struct aqc_reg_rxf_l3_sa {
	unsigned l3_sa0:32;
} __packed;

struct aqc_reg_rxf_l3_da {
	unsigned l3_da0:32;
} __packed;

struct aqc_reg_rxf_l4_sp {
	unsigned l4_sp0:16;
	unsigned rsvd0:16;
} __packed;

struct aqc_reg_rxf_l4_dp {
	unsigned l4_dp0:16;
	unsigned rsvd0:16;
} __packed;

struct aqc_reg_rxf_tcp_syn {
	unsigned syn_act:3;
	unsigned rsvd0:1;
	unsigned syn_en:1;
	unsigned rsvd1:3;
	unsigned syn_pri:1;
	unsigned rsvd2:7;
	unsigned syn_rxq:5;
	unsigned rsvd3:2;
	unsigned syn_mng_rxq:1;
	unsigned syn_rxq_en:1;
	unsigned rsvd4:7;
} __packed;

struct aqc_reg_rx_dma_ctrl1 {
	unsigned desc_init:1;
	unsigned rsvd0:30;
	unsigned rdm_en_scg:1;
} __packed;

struct aqc_reg_rx_dma_ctrl2 {
	unsigned rx_desc_rd_req_limit:4;
	unsigned rsvd0:28;
} __packed;

struct aqc_reg_rx_dma_status {
	unsigned desc_cache_init_done:1;
	unsigned rsvd0:31;
} __packed;

struct aqc_reg_rx_dma_pci_ctrl {
	unsigned desc_wr_tag:8;
	unsigned data_wr_tag:8;
	unsigned rsvd0:16;
} __packed;

struct aqc_reg_rx_intr_ctrl {
	unsigned int_desc_empty_en:1;
	unsigned int_desc_nempty_en:1;
	unsigned int_desc_wrb_en:1;
	unsigned rdm_int_rim_en:1;
	unsigned rsvd0:28;
} __packed;

struct aqc_reg_rx_rim_ctrl {
	unsigned rdm_rim0_mode:2;
	unsigned rsvd0:2;
	unsigned rdm_rim0_desc_empty_en:1;
	unsigned rdm_rim0_desc_nempty_en:1;
	unsigned rdm_rim0_obff_en:1;
	unsigned rsvd1:1;
	unsigned rdm_rim0_min_timeout:8;
	unsigned rdm_rim0_max_timeout:9;
	unsigned rsvd2:7;
} __packed;

struct aqc_reg_rx_desc_lsw {
	unsigned rsvd0:7;
	unsigned desc_baddr_LSW:25;
} __packed;

struct aqc_reg_rx_desc_msw {
	unsigned desc_baddr_MSW:32;
} __packed;

struct aqc_reg_rx_desc_ctrl {
	unsigned rsvd0:3;
	unsigned desc_len:10;
	unsigned rsvd1:12;
	unsigned desc_reset:1;
	unsigned desc_wrb:1;
	unsigned desc_pfetch:1;
	unsigned desc_hdr_split:1;
	unsigned desc_vl_strip:1;
	unsigned desc_freeze:1;
	unsigned desc_en:1;
} __packed;

struct aqc_reg_rx_desc_head {
	unsigned desc_hd:13;
	unsigned rsvd0:3;
	unsigned desc_ptr:13;
	unsigned rsvd1:3;
} __packed;

struct aqc_reg_rx_desc_tail {
	unsigned desc_tl:13;
	unsigned rsvd0:3;
	unsigned rsvd1:16;
} __packed;

struct aqc_reg_rx_desc_status {
	unsigned desc_empty:1;
	unsigned desc_almost_empty:1;
	unsigned desc_wrb_stat:1;
	unsigned desc_pfetch_stat:1;
	unsigned desc_queue_stop:1;
	unsigned rsvd0:3;
	unsigned desc_cache_perr:1;
	unsigned rsvd1:3;
	unsigned desc_wrb_perr:1;
	unsigned rsvd2:19;
} __packed;

struct aqc_reg_rx_desc_buffsz {
	unsigned desc_data_size:5;
	unsigned rsvd0:3;
	unsigned desc_hdr_size:5;
	unsigned rsvd1:3;
	unsigned rsvd2:16;
} __packed;

struct aqc_reg_rx_desc_thresh {
	unsigned desc_lo_thresh:3;
	unsigned rsvd0:13;
	unsigned desc_host_thresh:7;
	unsigned rsvd1:1;
	unsigned desc_pfetch_thresh:7;
	unsigned rsvd2:1;
} __packed;

struct aqc_reg_rx_stats1 {
	unsigned RX_DMA_Good_Packet_Counter_LSW:32;
} __packed;

struct aqc_reg_rx_stats2 {
	unsigned RX_DMA_Good_Packet_Counter_MSW:32;
} __packed;

struct aqc_reg_rx_stats3 {
	unsigned RX_DMA_Good_Octet_Counter_LSW:32;
} __packed;

struct aqc_reg_rx_stats4 {
	unsigned RX_DMA_Good_Octet_Counter_MSW:32;
} __packed;

struct aqc_reg_rx_stats5 {
	unsigned RX_Loopback_Good_Packet_Counter_LSW:32;
} __packed;

struct aqc_reg_rx_stats6 {
	unsigned RX_Loopback_Good_Packet_Counter_MSW:32;
} __packed;

struct aqc_reg_rx_stats7 {
	unsigned RX_DMA_Drop_Packet_Counter:32;
} __packed;

struct aqc_reg_tx_ctrl1 {
	unsigned rsvd0:3;
	unsigned passthru_en:1;
	unsigned dma_net_loopback:1;
	unsigned pkt_net_loopback:1;
	unsigned dma_sys_loopback:1;
	unsigned pkt_sys_loopback:1;
	unsigned rsvd1:21;
	unsigned reg_reset_dsbl:1;
	unsigned par_chk_sense:1;
	unsigned reset:1;
} __packed;

struct aqc_reg_tx_ctrl2 {
	unsigned tps_dp_clk_en:1;
	unsigned tps_clk_en:1;
	unsigned tpo_dp_clk_en:1;
	unsigned tpo_clk_en:1;
	unsigned tpb_dp_clk_en:1;
	unsigned tpb_clk_en:1;
	unsigned tdm_dp_clk_en:1;
	unsigned tdm_clk_en:1;
	unsigned tpo2_dp_clk_en:1;
	unsigned tpo2_clk_en:1;
	unsigned rsvd0:22;
} __packed;

struct aqc_reg_tx_dma_ctrl1 {
	unsigned desc_cache_init:1;
	unsigned desc_ctx_init:1;
	unsigned rsvd0:2;
	unsigned early_pkt_brk:1;
	unsigned rsvd1:26;
	unsigned tdm_en_scg:1;
} __packed;

struct aqc_reg_tx_dma_ctrl2 {
	unsigned tx_desc_rd_req_limit:4;
	unsigned rsvd0:4;
	unsigned tx_data_rd_req_limit:5;
	unsigned rsvd1:19;
} __packed;

struct aqc_reg_tx_dma_status1 {
	unsigned desc_cache_init_done:1;
	unsigned desc_ctx_init_done:1;
	unsigned rsvd0:2;
	unsigned desc_ctx_dtack:1;
	unsigned rsvd1:27;
} __packed;

struct aqc_reg_tx_dma_status2 {
	unsigned desc_ctx_perr:32;
} __packed;

struct aqc_reg_tx_dma_status3 {
	unsigned desc_lso_perr:32;
} __packed;

struct aqc_reg_tx_dma_limit {
	unsigned desc_total_req_limit:12;
	unsigned rsvd0:20;
} __packed;

struct aqc_reg_tx_dma_pcie_ctrl {
	unsigned desc_wr_tag:8;
	unsigned rsvd0:24;
} __packed;

struct aqc_reg_tx_intr_ctrl {
	unsigned int_desc_empty_en:1;
	unsigned int_desc_wrb_en:1;
	unsigned int_lso_done_en:1;
	unsigned int_pkt_tx_en:1;
	unsigned tdm_int_mod_en:1;
	unsigned rsvd0:27;
} __packed;

struct aqc_reg_tx_desc_lsw {
	unsigned rsvd0:7;
	unsigned desc_baddr_LSW:25;
} __packed;

struct aqc_reg_tx_desc_msw {
	unsigned desc_baddr_MSW:32;
} __packed;

struct aqc_reg_tx_desc_ctrl {
	unsigned rsvd0:3;
	unsigned desc_len:10;
	unsigned rsvd1:12;
	unsigned desc_reset:1;
	unsigned desc_wrb:1;
	unsigned desc_pfetch:1;
	unsigned desc_wrb_hdr_en:1;
	unsigned rsvd2:1;
	unsigned desc_freeze:1;
	unsigned desc_en:1;
} __packed;

struct aqc_reg_tx_desc_head {
	unsigned desc_hd:13;
	unsigned rsvd0:3;
	unsigned desc_ptr:13;
	unsigned rsvd1:3;
} __packed;

struct aqc_reg_tx_desc_tail {
	unsigned desc_tl:13;
	unsigned rsvd0:3;
	unsigned rsvd1:16;
} __packed;

struct aqc_reg_tx_desc_status {
	unsigned desc_empty:1;
	unsigned rsvd0:1;
	unsigned desc_wrb_stat:1;
	unsigned desc_pfetch_stat:1;
	unsigned desc_queue_stop:1;
	unsigned rsvd1:3;
	unsigned desc_cache_perr:1;
	unsigned rsvd2:23;
} __packed;

struct aqc_reg_tx_desc_thresh {
	unsigned rsvd0:8;
	unsigned desc_wrb_thresh:7;
	unsigned rsvd1:1;
	unsigned desc_host_thresh:7;
	unsigned rsvd2:1;
	unsigned desc_pfetch_thresh:7;
	unsigned rsvd3:1;
} __packed;

struct aqc_reg_tx_desc_hdr_wrb1 {
	unsigned rsvd0:2;
	unsigned desc_wrb_hdr_addr_LSB:30;
} __packed;

struct aqc_reg_tx_desc_hdr_wrb2 {
	unsigned desc_wrb_hdr_addr_MSB:32;
} __packed;

struct aqc_reg_tx_stats1 {
	unsigned TX_DMA_Good_Packet_Counter_LSW:32;
} __packed;

struct aqc_reg_tx_stats2 {
	unsigned TX_DMA_Good_Packet_Counter_MSW:32;
} __packed;

struct aqc_reg_tx_stats3 {
	unsigned TX_DMA_Good_Octet_Counter_LSW:32;
} __packed;

struct aqc_reg_tx_stats4 {
	unsigned TX_DMA_Good_Octet_Counter_MSW:32;
} __packed;

struct aqc_reg_tx_stats5 {
	unsigned TX_Loopback_Good_Packet_Counter_LSW:32;
} __packed;

struct aqc_reg_tx_stats6 {
	unsigned TX_Loopback_Good_Packet_Counter_MSW:32;
} __packed;

struct aqc_reg_tx_desc_error {
	unsigned TX_DMA_Desc_Error:32;
} __packed;

struct aqc_reg_tx_data_error {
	unsigned TX_DMA_Data_Error:32;
} __packed;

struct aqc_reg_intr_status {
	unsigned imr_LSW:32;
} __packed;

struct aqc_reg_intr_mask {
	unsigned imr_LSW:32;
} __packed;

struct aqc_reg_intr_throttle_mask {
	unsigned itmr_LSW:32;
} __packed;

struct aqc_reg_intr_autoclear {
	unsigned iacsr_LSW:32;
} __packed;

struct aqc_reg_intr_automask {
	unsigned iamr_LSW:32;
} __packed;

struct aqc_reg_txrx_intr_map {
	unsigned imr_rx1:5;
	unsigned rsvd0:2;
	unsigned imr_rx1_en:1;
	unsigned imr_rx0:5;
	unsigned rsvd1:2;
	unsigned imr_rx0_en:1;
	unsigned imr_tx1:5;
	unsigned rsvd2:2;
	unsigned imr_tx1_en:1;
	unsigned imr_tx0:5;
	unsigned rsvd3:2;
	unsigned imr_tx0_en:1;
} __packed;

struct aqc_reg_gen_intr_map1 {
	unsigned imr_phy:5;
	unsigned rsvd0:2;
	unsigned imr_phy_en:1;
	unsigned imr_link:5;
	unsigned rsvd1:2;
	unsigned imr_link_en:1;
	unsigned imr_fatal:5;
	unsigned rsvd2:2;
	unsigned imr_fatal_en:1;
	unsigned imr_pci:5;
	unsigned rsvd3:2;
	unsigned imr_pci_en:1;
} __packed;

struct aqc_reg_gen_intr_map2 {
	unsigned imr_rx_drop:5;
	unsigned rsvd0:2;
	unsigned imr_rx_drop_en:1;
	unsigned imr_tx_drop:5;
	unsigned rsvd1:2;
	unsigned imr_tx_drop_en:1;
	unsigned imr_tcp_timer:5;
	unsigned rsvd2:2;
	unsigned imr_tcp_timer_en:1;
	unsigned imr_wol:5;
	unsigned rsvd3:2;
	unsigned imr_wol_en:1;
} __packed;

struct aqc_reg_gen_intr_map3 {
	unsigned imr_rx_ext1:5;
	unsigned rsvd0:2;
	unsigned imr_rx_ext1_en:1;
	unsigned imr_rx_ext0:5;
	unsigned rsvd1:2;
	unsigned imr_rx_ext0_en:1;
	unsigned imr_tx_ext1:5;
	unsigned rsvd2:2;
	unsigned imr_tx_ext1_en:1;
	unsigned imr_tx_ext0:5;
	unsigned rsvd3:2;
	unsigned imr_tx_ext0_en:1;
} __packed;

struct aqc_reg_gen_intr_map4 {
	unsigned imr_mif3:5;
	unsigned rsvd0:2;
	unsigned imr_mif3_en:1;
	unsigned imr_mif2:5;
	unsigned rsvd1:2;
	unsigned imr_mif2_en:1;
	unsigned imr_mif1:5;
	unsigned rsvd2:2;
	unsigned imr_mif1_en:1;
	unsigned imr_mif0:5;
	unsigned rsvd3:2;
	unsigned imr_mif0_en:1;
} __packed;

struct aqc_reg_gen_intr_status {
	unsigned mif_int:4;
	unsigned rsvd0:4;
	unsigned rx_ext_int:2;
	unsigned tx_ext_int:2;
	unsigned rsvd1:4;
	unsigned rx_drop_int:1;
	unsigned tx_drop_int:1;
	unsigned tcp_timer_int:1;
	unsigned wol_int:1;
	unsigned rsvd2:4;
	unsigned phy:1;
	unsigned link:1;
	unsigned fatal:1;
	unsigned pci:1;
	unsigned rsvd3:4;
} __packed;

struct aqc_reg_tdm_intr_status {
	unsigned tdm_int:32;
} __packed;

struct aqc_reg_rdm_intr_status {
	unsigned rdm_int:32;
} __packed;

struct aqc_reg_intr_ctrl {
	unsigned int_mode:2;
	unsigned mult_vec_en:1;
	unsigned rsvd0:2;
	unsigned iamr_clr_en:1;
	unsigned issr_im_en:1;
	unsigned isr_cor_en:1;
	unsigned gen_fatal_int:1;
	unsigned rsvd1:20;
	unsigned itr_reg_reset_dsbl:1;
	unsigned rsvd2:1;
	unsigned itr_reset:1;
} __packed;

struct aqc_reg_intr_throttle {
	unsigned timer_cnt:9;
	unsigned rsc_timer_cnt:4;
	unsigned rsvd0:2;
	unsigned timer_max_protect:1;
	unsigned timer_max:9;
	unsigned timer_state:3;
	unsigned timer_start_mode:1;
	unsigned timer_start_dsbl:1;
	unsigned timer_clr:1;
	unsigned timer_en:1;
} __packed;

#define AQC_REG_DECL(reg, count) \
	struct aqc_reg_##reg aqc_##reg[count]; \
	size_t aqc_##reg##_count

struct aqc_ioss_regs {
	ktime_t begin_ktime;
	ktime_t end_ktime;
	u64 duration_ns;

	AQC_REG_DECL(rx_ctrl1, 1);
	AQC_REG_DECL(rx_ctrl2, 1);

	/*
	 * Rx filter registers
	 */

	AQC_REG_DECL(rxf_ctrl1, 1);
	AQC_REG_DECL(rxf_ctrl2, 1);

	/* Broadcast filter registers */
	AQC_REG_DECL(rxf_bcf_status, 1);
	AQC_REG_DECL(rxf_bcf_count, 1);

	/* Unicast filter registers */
	AQC_REG_DECL(rxf_ucf1, 38);
	AQC_REG_DECL(rxf_ucf2, 38);

	/* Multicast filter registers */
	AQC_REG_DECL(rxf_mcf, 8);
	AQC_REG_DECL(rxf_mcf_msk, 1);

	/* VLAN filter registers */
	AQC_REG_DECL(rxf_vlan_ctrl1, 1);
	AQC_REG_DECL(rxf_vlan_ctrl2, 1);
	AQC_REG_DECL(rxf_vlan, 16);

	/* EtherType filter registers */
	AQC_REG_DECL(rxf_ether, 16);

	/* L3/L4 filter registers */
	AQC_REG_DECL(rxf_l3l4, 8);
	AQC_REG_DECL(rxf_l3_sa, 8);
	AQC_REG_DECL(rxf_l3_da, 8);
	AQC_REG_DECL(rxf_l4_sp, 8);
	AQC_REG_DECL(rxf_l4_dp, 8);

	/* TCP Syn filter registers */
	AQC_REG_DECL(rxf_tcp_syn, 1);

	/* Add RSS and Flex Filters here */

	/* Rx DMA registers */
	AQC_REG_DECL(rx_dma_ctrl1, 1);
	AQC_REG_DECL(rx_dma_ctrl2, 1);
	AQC_REG_DECL(rx_dma_status, 1);
	AQC_REG_DECL(rx_dma_pci_ctrl, 1);
	AQC_REG_DECL(rx_intr_ctrl, 1);

	AQC_REG_DECL(rx_rim_ctrl, 32);
	AQC_REG_DECL(rx_desc_lsw, 32);
	AQC_REG_DECL(rx_desc_msw, 32);
	AQC_REG_DECL(rx_desc_ctrl, 32);
	AQC_REG_DECL(rx_desc_head, 32);
	AQC_REG_DECL(rx_desc_tail, 32);
	AQC_REG_DECL(rx_desc_status, 32);
	AQC_REG_DECL(rx_desc_buffsz, 32);
	AQC_REG_DECL(rx_desc_thresh, 32);

	AQC_REG_DECL(rx_stats1, 1);
	AQC_REG_DECL(rx_stats2, 1);
	AQC_REG_DECL(rx_stats3, 1);
	AQC_REG_DECL(rx_stats4, 1);
	AQC_REG_DECL(rx_stats5, 1);
	AQC_REG_DECL(rx_stats6, 1);
	AQC_REG_DECL(rx_stats7, 1);

	/*
	 * Tx Registers
	 */
	AQC_REG_DECL(tx_ctrl1, 1);
	AQC_REG_DECL(tx_ctrl2, 1);

	AQC_REG_DECL(tx_dma_ctrl1, 1);
	AQC_REG_DECL(tx_dma_ctrl2, 1);
	AQC_REG_DECL(tx_dma_status1, 1);
	AQC_REG_DECL(tx_dma_status2, 1);
	AQC_REG_DECL(tx_dma_status3, 1);
	AQC_REG_DECL(tx_dma_limit, 1);
	AQC_REG_DECL(tx_dma_pcie_ctrl, 1);
	AQC_REG_DECL(tx_intr_ctrl, 1);

	AQC_REG_DECL(tx_desc_lsw, 32);
	AQC_REG_DECL(tx_desc_msw, 32);
	AQC_REG_DECL(tx_desc_ctrl, 32);
	AQC_REG_DECL(tx_desc_head, 32);
	AQC_REG_DECL(tx_desc_tail, 32);
	AQC_REG_DECL(tx_desc_status, 32);
	AQC_REG_DECL(tx_desc_thresh, 32);
	AQC_REG_DECL(tx_desc_hdr_wrb1, 32);
	AQC_REG_DECL(tx_desc_hdr_wrb2, 32);

	AQC_REG_DECL(tx_stats1, 1);
	AQC_REG_DECL(tx_stats2, 1);
	AQC_REG_DECL(tx_stats3, 1);
	AQC_REG_DECL(tx_stats4, 1);
	AQC_REG_DECL(tx_stats5, 1);
	AQC_REG_DECL(tx_stats6, 1);

	AQC_REG_DECL(tx_desc_error, 1);
	AQC_REG_DECL(tx_data_error, 1);

	AQC_REG_DECL(intr_status, 1);
	AQC_REG_DECL(intr_mask, 1);
	AQC_REG_DECL(intr_throttle_mask, 1);
	AQC_REG_DECL(intr_autoclear, 1);
	AQC_REG_DECL(intr_automask, 1);
	AQC_REG_DECL(txrx_intr_map, 16);
	AQC_REG_DECL(gen_intr_map1, 1);
	AQC_REG_DECL(gen_intr_map2, 1);
	AQC_REG_DECL(gen_intr_map3, 1);
	AQC_REG_DECL(gen_intr_map4, 1);
	AQC_REG_DECL(gen_intr_status, 1);
	AQC_REG_DECL(tdm_intr_status, 1);
	AQC_REG_DECL(rdm_intr_status, 1);
	AQC_REG_DECL(intr_ctrl, 1);
	AQC_REG_DECL(intr_throttle, 32);
};

size_t aqc_regs_save(void __iomem *regs_base, struct aqc_ioss_regs *regs);

#endif /* __AQC_REGS_H__ */
