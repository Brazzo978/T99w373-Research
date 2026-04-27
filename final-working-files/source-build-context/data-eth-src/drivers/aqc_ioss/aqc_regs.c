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

#include "aqc_ioss.h"

struct aqc_reg_info {
	u64 reg_base;
	size_t reg_size;
	size_t reg_count;
	size_t dst_reg_off;
	size_t dst_count_off;
};

#define AQC_REG_INFO(reg, base, size) \
	{ \
		.reg_base = base, \
		.reg_size = size, \
		.reg_count = ARRAY_SIZE(((struct aqc_ioss_regs *)0)->aqc_##reg), \
		.dst_reg_off = offsetof(struct aqc_ioss_regs, aqc_##reg), \
		.dst_count_off = \
			offsetof(struct aqc_ioss_regs, aqc_##reg##_count), \
	}

struct aqc_reg_info aqc_regs_info[] = {
	AQC_REG_INFO(rx_ctrl1, 0x5000, 0),
	AQC_REG_INFO(rx_ctrl2, 0x5004, 0),
	AQC_REG_INFO(rxf_ctrl1, 0x5100, 0),
	AQC_REG_INFO(rxf_ctrl2, 0x5104, 0),
	AQC_REG_INFO(rxf_bcf_status, 0x5108, 0),
	AQC_REG_INFO(rxf_bcf_count, 0x510C, 0),

	AQC_REG_INFO(rxf_ucf1, 0x5110, 0x8),
	AQC_REG_INFO(rxf_ucf2, 0x5114, 0x8),

	AQC_REG_INFO(rxf_mcf, 0x5250, 0x4),
	AQC_REG_INFO(rxf_mcf_msk, 0x5270, 0),

	AQC_REG_INFO(rxf_vlan_ctrl1, 0, 0x5280),
	AQC_REG_INFO(rxf_vlan_ctrl2, 0, 0x5284),
	AQC_REG_INFO(rxf_vlan, 0x5290, 0x4),

	AQC_REG_INFO(rxf_ether, 0x5300, 0x4),

	AQC_REG_INFO(rxf_l3l4, 0x5380, 0x4),
	AQC_REG_INFO(rxf_l3_sa, 0x53B0, 0x4),
	AQC_REG_INFO(rxf_l3_da, 0x53D0, 0x4),
	AQC_REG_INFO(rxf_l4_sp, 0x5400, 0x4),
	AQC_REG_INFO(rxf_l4_dp, 0x5420, 0x4),

	AQC_REG_INFO(rxf_tcp_syn, 0x5450, 0),

	AQC_REG_INFO(rx_dma_ctrl1, 0x5A00, 0),
	AQC_REG_INFO(rx_dma_ctrl2, 0x5A04, 0),
	AQC_REG_INFO(rx_dma_status, 0x5A10, 0),
	AQC_REG_INFO(rx_dma_pci_ctrl, 0x5A20, 0),
	AQC_REG_INFO(rx_intr_ctrl, 0x5A30, 0),

	AQC_REG_INFO(rx_rim_ctrl, 0x5A40, 0x4),
	AQC_REG_INFO(rx_desc_lsw, 0x5B00, 0x20),
	AQC_REG_INFO(rx_desc_msw, 0x5B04, 0x20),
	AQC_REG_INFO(rx_desc_ctrl, 0x5B08, 0x20),
	AQC_REG_INFO(rx_desc_head, 0x5B0C, 0x20),
	AQC_REG_INFO(rx_desc_tail, 0x5B10, 0x20),
	AQC_REG_INFO(rx_desc_status, 0x5B14, 0x20),
	AQC_REG_INFO(rx_desc_buffsz, 0x5B18, 0x20),
	AQC_REG_INFO(rx_desc_thresh, 0x5B1C, 0x20),

	AQC_REG_INFO(rx_stats1, 0x6800, 0x0),
	AQC_REG_INFO(rx_stats2, 0x6804, 0x0),
	AQC_REG_INFO(rx_stats3, 0x6808, 0x0),
	AQC_REG_INFO(rx_stats4, 0x680C, 0x0),
	AQC_REG_INFO(rx_stats5, 0x6810, 0x0),
	AQC_REG_INFO(rx_stats6, 0x6814, 0x0),
	AQC_REG_INFO(rx_stats7, 0x6818, 0x0),

	AQC_REG_INFO(tx_ctrl1, 0x7000, 0x0),
	AQC_REG_INFO(tx_ctrl2, 0x7004, 0x0),

	AQC_REG_INFO(tx_dma_ctrl1, 0x7B00, 0x0),
	AQC_REG_INFO(tx_dma_ctrl2, 0x7B04, 0x0),
	AQC_REG_INFO(tx_dma_status1, 0x7B10, 0x0),
	AQC_REG_INFO(tx_dma_status2, 0x7B14, 0x0),
	AQC_REG_INFO(tx_dma_status3, 0x7B18, 0x0),
	AQC_REG_INFO(tx_dma_limit, 0x7B20, 0x0),
	AQC_REG_INFO(tx_dma_pcie_ctrl, 0x7B30, 0x0),
	AQC_REG_INFO(tx_intr_ctrl, 0x7B40, 0x0),

	AQC_REG_INFO(tx_desc_lsw, 0x7C00, 0x40),
	AQC_REG_INFO(tx_desc_msw, 0x7C04, 0x40),
	AQC_REG_INFO(tx_desc_ctrl, 0x7C08, 0x40),
	AQC_REG_INFO(tx_desc_head, 0x7C0C, 0x40),
	AQC_REG_INFO(tx_desc_tail, 0x7C10, 0x40),
	AQC_REG_INFO(tx_desc_status, 0x7C14, 0x40),
	AQC_REG_INFO(tx_desc_thresh, 0x7C18, 0x40),
	AQC_REG_INFO(tx_desc_hdr_wrb1, 0x7C1C, 0x40),
	AQC_REG_INFO(tx_desc_hdr_wrb2, 0x7C20, 0x40),

	AQC_REG_INFO(tx_stats1, 0x8800, 0x0),
	AQC_REG_INFO(tx_stats2, 0x8804, 0x0),
	AQC_REG_INFO(tx_stats3, 0x8808, 0x0),
	AQC_REG_INFO(tx_stats4, 0x880C, 0x0),
	AQC_REG_INFO(tx_stats5, 0x8810, 0x0),
	AQC_REG_INFO(tx_stats6, 0x8814, 0x0),

	AQC_REG_INFO(tx_desc_error, 0x8900, 0x0),
	AQC_REG_INFO(tx_data_error, 0x8904, 0x0),

	AQC_REG_INFO(intr_status, 0x2000, 0x0),
	AQC_REG_INFO(intr_mask, 0x2010, 0x0),
	AQC_REG_INFO(intr_throttle_mask, 0x2020, 0x0),
	AQC_REG_INFO(intr_autoclear, 0x2080, 0x0),
	AQC_REG_INFO(intr_automask, 0x2090, 0x0),
	AQC_REG_INFO(txrx_intr_map, 0x2100, 0x4),
	AQC_REG_INFO(gen_intr_map1, 0x2180, 0x0),
	AQC_REG_INFO(gen_intr_map2, 0x2184, 0x0),
	AQC_REG_INFO(gen_intr_map3, 0x2188, 0x0),
	AQC_REG_INFO(gen_intr_map4, 0x218C, 0x0),

	AQC_REG_INFO(gen_intr_status, 0x21A0, 0x0),
	AQC_REG_INFO(tdm_intr_status, 0x21A4, 0x0),
	AQC_REG_INFO(rdm_intr_status, 0x21A8, 0x0),
	AQC_REG_INFO(intr_ctrl, 0x2300, 0x0),
	AQC_REG_INFO(intr_throttle, 0x2800, 0x4),
};

size_t aqc_regs_save(void __iomem *regs_base, struct aqc_ioss_regs *regs)
{
	size_t i = 0, j = 0;

	/* Device register memory is not mapped yet */
	if (!regs_base)
		return 0;

	regs->begin_ktime = ktime_get();

	for (i = 0; i < ARRAY_SIZE(aqc_regs_info); i++) {
		struct aqc_reg_info *ri = &aqc_regs_info[i];
		void __iomem *reg_base = regs_base + ri->reg_base;
		size_t reg_size = ri->reg_size;
		u32 *dst_regs = ((void *) regs) + ri->dst_reg_off;
		size_t *dst_count = ((void *) regs) + ri->dst_count_off;

		for (j = 0; j < ri->reg_count; j++)
			dst_regs[j] = readl_relaxed(reg_base + (j * reg_size));

		*dst_count = j;
	}

	regs->end_ktime = ktime_get();

	regs->duration_ns =
		ktime_to_ns(ktime_sub(regs->end_ktime, regs->begin_ktime));

	return i;
}
