// SPDX-License-Identifier: GPL-2.0-only
/* This is the driver for the GMAC on-chip Ethernet controller for ST SoCs.
 * DWC Ether MAC version 4.xx  has been used for  developing this code.
 *
 * This contains the functions to handle the dma.
 *
 * Copyright (C) 2007-2015  STMicroelectronics Ltd
 *
 * Author: Alexandre Torgue <alexandre.torgue@st.com>
 */

#include <linux/io.h>
#include "dwmac4_thin.h"
#include "stmmac_thin.h"

static void dwmac4_set_mtl_tx_queue_weight(struct mac_device_info *hw,
					   u32 weight, u32 queue)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 value = readl_relaxed(ioaddr + MTL_TXQX_WEIGHT_BASE_ADDR(queue));

	value &= ~MTL_TXQ_WEIGHT_ISCQW_MASK;
	value |= weight & MTL_TXQ_WEIGHT_ISCQW_MASK;
	writel_relaxed(value, ioaddr + MTL_TXQX_WEIGHT_BASE_ADDR(queue));
}

static int dwmac4_irq_mtl_status(struct mac_device_info *hw, u32 chan)
{
	void __iomem *ioaddr = hw->pcsr;
	int ret = 0;

	/* read Queue x Interrupt status */
	u32 status = readl_relaxed(ioaddr + MTL_CHAN_INT_CTRL(chan));

	if (status & MTL_RX_OVERFLOW_INT) {
		/*  clear Interrupt */
		writel_relaxed(status | MTL_RX_OVERFLOW_INT,
			       ioaddr + MTL_CHAN_INT_CTRL(chan));
		ret = CORE_IRQ_MTL_RX_OVERFLOW;
	}

	return ret;
}

static void dwmac4_debug(void __iomem *ioaddr, struct stmmac_extra_stats *x,
			 u32 rx_queue, u32 tx_queue)
{
	u32 value;

	/* Read TX debug info */
	value = readl_relaxed(ioaddr + MTL_CHAN_TX_DEBUG(tx_queue));

	if (value & MTL_DEBUG_TXSTSFSTS)
		x->mtl_tx_status_fifo_full++;
	if (value & MTL_DEBUG_TXFSTS)
		x->mtl_tx_fifo_not_empty++;
	if (value & MTL_DEBUG_TWCSTS)
		x->mmtl_fifo_ctrl++;
	if (value & MTL_DEBUG_TRCSTS_MASK) {
		u32 trcsts = (value & MTL_DEBUG_TRCSTS_MASK)
			     >> MTL_DEBUG_TRCSTS_SHIFT;
		if (trcsts == MTL_DEBUG_TRCSTS_WRITE)
			x->mtl_tx_fifo_read_ctrl_write++;
		else if (trcsts == MTL_DEBUG_TRCSTS_TXW)
			x->mtl_tx_fifo_read_ctrl_wait++;
		else if (trcsts == MTL_DEBUG_TRCSTS_READ)
			x->mtl_tx_fifo_read_ctrl_read++;
		else
			x->mtl_tx_fifo_read_ctrl_idle++;
	}
	if (value & MTL_DEBUG_TXPAUSED)
		x->mac_tx_in_pause++;

	/* Read RX debug info */
	value = readl_relaxed(ioaddr + MTL_CHAN_RX_DEBUG(rx_queue));

	if (value & MTL_DEBUG_RXFSTS_MASK) {
		u32 rxfsts = (value & MTL_DEBUG_RXFSTS_MASK)
			     >> MTL_DEBUG_RRCSTS_SHIFT;

		if (rxfsts == MTL_DEBUG_RXFSTS_FULL)
			x->mtl_rx_fifo_fill_level_full++;
		else if (rxfsts == MTL_DEBUG_RXFSTS_AT)
			x->mtl_rx_fifo_fill_above_thresh++;
		else if (rxfsts == MTL_DEBUG_RXFSTS_BT)
			x->mtl_rx_fifo_fill_below_thresh++;
		else
			x->mtl_rx_fifo_fill_level_empty++;
	}
	if (value & MTL_DEBUG_RRCSTS_MASK) {
		u32 rrcsts = (value & MTL_DEBUG_RRCSTS_MASK) >>
			     MTL_DEBUG_RRCSTS_SHIFT;

		if (rrcsts == MTL_DEBUG_RRCSTS_FLUSH)
			x->mtl_rx_fifo_read_ctrl_flush++;
		else if (rrcsts == MTL_DEBUG_RRCSTS_RSTAT)
			x->mtl_rx_fifo_read_ctrl_read_data++;
		else if (rrcsts == MTL_DEBUG_RRCSTS_RDATA)
			x->mtl_rx_fifo_read_ctrl_status++;
		else
			x->mtl_rx_fifo_read_ctrl_idle++;
	}
	if (value & MTL_DEBUG_RWCSTS)
		x->mtl_rx_fifo_ctrl_active++;
}

static void dwmac4_dma_init_rx_chan(void __iomem *ioaddr,
				    struct stmmac_dma_cfg *dma_cfg,
				    dma_addr_t dma_rx_phy, u32 chan)
{
	u32 value;
	u32 rxpbl = dma_cfg->rxpbl ?: dma_cfg->pbl;

	value = readl_relaxed(ioaddr + DMA_CHAN_RX_CONTROL(chan));
	value = value | (rxpbl << DMA_BUS_MODE_RPBL_SHIFT);
	writel_relaxed(value, ioaddr + DMA_CHAN_RX_CONTROL(chan));

	writel_relaxed(lower_32_bits(dma_rx_phy),
		       ioaddr + DMA_CHAN_RX_BASE_ADDR(chan));
}

static void dwmac4_dma_init_tx_chan(void __iomem *ioaddr,
				    struct stmmac_dma_cfg *dma_cfg,
				    dma_addr_t dma_tx_phy, u32 chan)
{
	u32 value;
	u32 txpbl = dma_cfg->txpbl ?: dma_cfg->pbl;

	value = readl_relaxed(ioaddr + DMA_CHAN_TX_CONTROL(chan));
	value = value | (txpbl << DMA_BUS_MODE_PBL_SHIFT);

	/* Enable OSP to get best performance */
	value |= DMA_CONTROL_OSP;

	writel_relaxed(value, ioaddr + DMA_CHAN_TX_CONTROL(chan));

	writel_relaxed(lower_32_bits(dma_tx_phy),
		       ioaddr + DMA_CHAN_TX_BASE_ADDR(chan));
}

static void dwmac4_dma_init_channel(void __iomem *ioaddr,
				    struct stmmac_dma_cfg *dma_cfg, u32 chan)
{
	u32 value;

	/* common channel control register config */
	value = readl_relaxed(ioaddr + DMA_CHAN_CONTROL(chan));
	if (dma_cfg->pblx8)
		value = value | DMA_BUS_MODE_PBL;
	writel_relaxed(value, ioaddr + DMA_CHAN_CONTROL(chan));

	/* Mask interrupts by writing to CSR7 */
	writel_relaxed(DMA_CHAN_INTR_DEFAULT_MASK,
		       ioaddr + DMA_CHAN_INTR_ENA(chan));
}

static void dwmac4_rx_watchdog(void __iomem *ioaddr, u32 riwt, u32 chan)
{
	writel_relaxed(riwt, ioaddr + DMA_CHAN_RX_WATCHDOG(chan));
}

static void dwmac4_dma_rx_chan_op_mode(void __iomem *ioaddr, int mode,
				       u32 channel, int fifosz, u8 qmode)
{
	unsigned int rqs = fifosz / 256 - 1;
	u32 mtl_rx_op, mtl_rx_int;

	mtl_rx_op = readl_relaxed(ioaddr + MTL_CHAN_RX_OP_MODE(channel));

	if (mode == SF_DMA_MODE) {
		mtl_rx_op |= MTL_OP_MODE_RSF;
	} else {
		mtl_rx_op &= ~MTL_OP_MODE_RSF;
		mtl_rx_op &= MTL_OP_MODE_RTC_MASK;
		if (mode <= 32)
			mtl_rx_op |= MTL_OP_MODE_RTC_32;
		else if (mode <= 64)
			mtl_rx_op |= MTL_OP_MODE_RTC_64;
		else if (mode <= 96)
			mtl_rx_op |= MTL_OP_MODE_RTC_96;
		else
			mtl_rx_op |= MTL_OP_MODE_RTC_128;
	}

	mtl_rx_op &= ~MTL_OP_MODE_RQS_MASK;
	mtl_rx_op |= rqs << MTL_OP_MODE_RQS_SHIFT;

	/* Enable flow control only if each channel gets 4 KiB or more FIFO and
	 * only if channel is not an AVB channel.
	 */
	if (fifosz >= 4096 && qmode != MTL_QUEUE_AVB) {
		unsigned int rfd, rfa;

		mtl_rx_op |= MTL_OP_MODE_EHFC;

		/* Set Threshold for Activating Flow Control to min 2 frames,
		 * i.e. 1500 * 2 = 3000 bytes.
		 *
		 * Set Threshold for Deactivating Flow Control to min 1 frame,
		 * i.e. 1500 bytes.
		 */
		switch (fifosz) {
		case 4096:
			/* This violates the above formula because of FIFO size
			 * limit therefore overflow may occur in spite of this.
			 */
			rfd = 0x03; /* Full-2.5K */
			rfa = 0x01; /* Full-1.5K */
			break;

		case 8192:
			rfd = 0x06; /* Full-4K */
			rfa = 0x0a; /* Full-6K */
			break;

		case 16384:
			rfd = 0x06; /* Full-4K */
			rfa = 0x12; /* Full-10K */
			break;

		default:
			rfd = 0x06; /* Full-4K */
			rfa = 0x1e; /* Full-16K */
			break;
		}

		mtl_rx_op &= ~MTL_OP_MODE_RFD_MASK;
		mtl_rx_op |= rfd << MTL_OP_MODE_RFD_SHIFT;

		mtl_rx_op &= ~MTL_OP_MODE_RFA_MASK;
		mtl_rx_op |= rfa << MTL_OP_MODE_RFA_SHIFT;
	}
	writel_relaxed(mtl_rx_op, ioaddr + MTL_CHAN_RX_OP_MODE(channel));

	/* Enable MTL RX overflow */
	mtl_rx_int = readl_relaxed(ioaddr + MTL_CHAN_INT_CTRL(channel));
	writel_relaxed(mtl_rx_int | MTL_RX_OVERFLOW_INT_EN,
		       ioaddr + MTL_CHAN_INT_CTRL(channel));
}

static void dwmac4_dma_tx_chan_op_mode(void __iomem *ioaddr, int mode,
				       u32 channel, int fifosz, u8 qmode)
{
	u32 mtl_tx_op = readl_relaxed(ioaddr + MTL_CHAN_TX_OP_MODE(channel));
	unsigned int tqs = fifosz / 256 - 1;

	if (mode == SF_DMA_MODE) {
		/* Transmit COE type 2 cannot be done in cut-through mode. */
		mtl_tx_op |= MTL_OP_MODE_TSF;
	} else {
		mtl_tx_op &= ~MTL_OP_MODE_TSF;
		mtl_tx_op &= MTL_OP_MODE_TTC_MASK;
		/* Set the transmit threshold */
		if (mode <= 32)
			mtl_tx_op |= MTL_OP_MODE_TTC_32;
		else if (mode <= 64)
			mtl_tx_op |= MTL_OP_MODE_TTC_64;
		else if (mode <= 96)
			mtl_tx_op |= MTL_OP_MODE_TTC_96;
		else if (mode <= 128)
			mtl_tx_op |= MTL_OP_MODE_TTC_128;
		else if (mode <= 192)
			mtl_tx_op |= MTL_OP_MODE_TTC_192;
		else if (mode <= 256)
			mtl_tx_op |= MTL_OP_MODE_TTC_256;
		else if (mode <= 384)
			mtl_tx_op |= MTL_OP_MODE_TTC_384;
		else
			mtl_tx_op |= MTL_OP_MODE_TTC_512;
	}
	/* For an IP with DWC_EQOS_NUM_TXQ == 1, the fields TXQEN and TQS are RO
	 * with reset values: TXQEN on, TQS == DWC_EQOS_TXFIFO_SIZE.
	 * For an IP with DWC_EQOS_NUM_TXQ > 1, the fields TXQEN and TQS are R/W
	 * with reset values: TXQEN off, TQS 256 bytes.
	 *
	 * TXQEN must be written for multi-channel operation and TQS must
	 * reflect the available fifo size per queue (total fifo size / number
	 * of enabled queues).
	 */
	mtl_tx_op &= ~MTL_OP_MODE_TXQEN_MASK;
	if (qmode != MTL_QUEUE_AVB)
		mtl_tx_op |= MTL_OP_MODE_TXQEN;
	else
		mtl_tx_op |= MTL_OP_MODE_TXQEN_AV;
	mtl_tx_op &= ~MTL_OP_MODE_TQS_MASK;
	mtl_tx_op |= tqs << MTL_OP_MODE_TQS_SHIFT;

	writel_relaxed(mtl_tx_op, ioaddr +  MTL_CHAN_TX_OP_MODE(channel));
}

/* Enable/disable TSO feature and set MSS */
static void dwmac4_enable_tso(void __iomem *ioaddr, bool en, u32 chan)
{
	u32 value;

	if (en) {
		/* enable TSO */
		value = readl_relaxed(ioaddr + DMA_CHAN_TX_CONTROL(chan));
		writel_relaxed(value | DMA_CONTROL_TSE,
			       ioaddr + DMA_CHAN_TX_CONTROL(chan));
	} else {
		/* enable TSO */
		value = readl_relaxed(ioaddr + DMA_CHAN_TX_CONTROL(chan));
		writel_relaxed(value & ~DMA_CONTROL_TSE,
			       ioaddr + DMA_CHAN_TX_CONTROL(chan));
	}
}

static void dwmac4_set_bfsize(void __iomem *ioaddr, int bfsize, u32 chan)
{
	u32 value = readl_relaxed(ioaddr + DMA_CHAN_RX_CONTROL(chan));

	value &= ~DMA_RBSZ_MASK;
	value |= (bfsize << DMA_RBSZ_SHIFT) & DMA_RBSZ_MASK;

	writel_relaxed(value, ioaddr + DMA_CHAN_RX_CONTROL(chan));
}

static void
dwmac4_set_rx_tail_ptr(void __iomem *ioaddr, u32 tail_ptr, u32 chan)
{
	writel_relaxed(tail_ptr, ioaddr + DMA_CHAN_RX_END_ADDR(chan));
}

static void
dwmac4_set_tx_tail_ptr(void __iomem *ioaddr, u32 tail_ptr, u32 chan)
{
	writel_relaxed(tail_ptr, ioaddr + DMA_CHAN_TX_END_ADDR(chan));
}

static void dwmac4_dma_start_tx(void __iomem *ioaddr, u32 chan)
{
	u32 value = readl_relaxed(ioaddr + DMA_CHAN_TX_CONTROL(chan));

	value |= DMA_CONTROL_ST;
	writel_relaxed(value, ioaddr + DMA_CHAN_TX_CONTROL(chan));
}

static void dwmac4_dma_stop_tx(void __iomem *ioaddr, u32 chan)
{
	u32 value = readl_relaxed(ioaddr + DMA_CHAN_TX_CONTROL(chan));

	value &= ~DMA_CONTROL_ST;
	writel_relaxed(value, ioaddr + DMA_CHAN_TX_CONTROL(chan));
}

static void dwmac4_dma_start_rx(void __iomem *ioaddr, u32 chan)
{
	u32 value = readl_relaxed(ioaddr + DMA_CHAN_RX_CONTROL(chan));

	value |= DMA_CONTROL_SR;

	writel_relaxed(value, ioaddr + DMA_CHAN_RX_CONTROL(chan));
}

static void dwmac4_dma_stop_rx(void __iomem *ioaddr, u32 chan)
{
	u32 value = readl_relaxed(ioaddr + DMA_CHAN_RX_CONTROL(chan));

	value &= ~DMA_CONTROL_SR;
	writel_relaxed(value, ioaddr + DMA_CHAN_RX_CONTROL(chan));
}

static void dwmac4_set_tx_ring_len(void __iomem *ioaddr, u32 len, u32 chan)
{
	writel_relaxed(len, ioaddr + DMA_CHAN_TX_RING_LEN(chan));
}

static void dwmac4_set_rx_ring_len(void __iomem *ioaddr, u32 len, u32 chan)
{
	writel_relaxed(len, ioaddr + DMA_CHAN_RX_RING_LEN(chan));
}

static void dwmac4_enable_dma_irq(void __iomem *ioaddr, u32 chan)
{
	writel_relaxed(DMA_CHAN_INTR_DEFAULT_MASK, ioaddr +
	       DMA_CHAN_INTR_ENA(chan));
}

static void dwmac4_disable_dma_irq(void __iomem *ioaddr, u32 chan)
{
	writel_relaxed(0, ioaddr + DMA_CHAN_INTR_ENA(chan));
}

static int dwmac4_dma_interrupt(void __iomem *ioaddr,
				struct stmmac_extra_stats *x, u32 chan)
{
	u32 intr_status = readl_relaxed(ioaddr + DMA_CHAN_STATUS(chan));
	u32 intr_en = readl_relaxed(ioaddr + DMA_CHAN_INTR_ENA(chan));
	int ret = 0;

	/* ABNORMAL interrupts */
	if (unlikely(intr_status & DMA_CHAN_STATUS_AIS)) {
		if (unlikely(intr_status & DMA_CHAN_STATUS_RBU))
			x->rx_buf_unav_irq++;
		if (unlikely(intr_status & DMA_CHAN_STATUS_RPS))
			x->rx_process_stopped_irq++;
		if (unlikely(intr_status & DMA_CHAN_STATUS_RWT))
			x->rx_watchdog_irq++;
		if (unlikely(intr_status & DMA_CHAN_STATUS_ETI))
			x->tx_early_irq++;
		if (unlikely(intr_status & DMA_CHAN_STATUS_TPS)) {
			x->tx_process_stopped_irq++;
			ret = tx_hard_error;
		}
		if (unlikely(intr_status & DMA_CHAN_STATUS_FBE)) {
			x->fatal_bus_error_irq++;
			ret = tx_hard_error;
		}
	}
	/* TX/RX NORMAL interrupts */
	if (likely(intr_status & DMA_CHAN_STATUS_NIS)) {
		x->normal_irq_n++;
		if (likely(intr_status & DMA_CHAN_STATUS_RI)) {
			x->rx_normal_irq_n++;
			ret |= handle_rx;
		}
		if (likely(intr_status & (DMA_CHAN_STATUS_TI |
					  DMA_CHAN_STATUS_TBU))) {
			x->tx_normal_irq_n++;
			ret |= handle_tx;
		}
		if (unlikely(intr_status & DMA_CHAN_STATUS_ERI))
			x->rx_early_irq++;
	}

	if (ret == 0)
		ret = intr_status;

	writel_relaxed(intr_status & intr_en, ioaddr + DMA_CHAN_STATUS(chan));
	return ret;
}

static void dwmac4_get_dma_info(void __iomem *ioaddr,
				struct stmmac_extra_stats *x, u32 chan)
{
	x->dma_ch_status = readl_relaxed(ioaddr + DMA_CHAN_STATUS(chan));
	x->dma_ch_intr_en = readl_relaxed(ioaddr + DMA_CHAN_INTR_ENA(chan));
	writel_relaxed(x->dma_ch_status & x->dma_ch_intr_en,
		       ioaddr + DMA_CHAN_STATUS(chan));

	x->dma_ch_tx_control =
		readl_relaxed(ioaddr + DMA_CHAN_TX_CONTROL(chan));
	x->dma_ch_txdesc_list_addr =
		readl_relaxed(ioaddr + DMA_CHAN_TX_BASE_ADDR(chan));
	x->dma_ch_txdesc_ring_len =
		readl_relaxed(ioaddr + DMA_CHAN_TX_RING_LEN(chan));
	x->dma_ch_curr_app_txdesc =
		readl_relaxed(ioaddr + DMA_CHAN_CUR_TX_DESC(chan));
	x->dma_ch_txdesc_tail_ptr =
		readl_relaxed(ioaddr + DMA_CHAN_TX_END_ADDR(chan));
	x->dma_ch_curr_app_txbuf =
		readl_relaxed(ioaddr + DMA_CHAN_CUR_TX_BUF_ADDR(chan));

	x->dma_ch_rx_control =
		readl_relaxed(ioaddr + DMA_CHAN_RX_CONTROL(chan));
	x->dma_ch_rxdesc_list_addr =
		readl_relaxed(ioaddr + DMA_CHAN_RX_BASE_ADDR(chan));
	x->dma_ch_rxdesc_ring_len =
		readl_relaxed(ioaddr + DMA_CHAN_RX_RING_LEN(chan));
	x->dma_ch_curr_app_rxdesc =
		readl_relaxed(ioaddr + DMA_CHAN_CUR_RX_DESC(chan));
	x->dma_ch_rxdesc_tail_ptr =
		readl_relaxed(ioaddr + DMA_CHAN_RX_END_ADDR(chan));
	x->dma_ch_curr_app_rxbuf =
		readl_relaxed(ioaddr + DMA_CHAN_CUR_RX_BUF_ADDR(chan));
	x->dma_ch_miss_frame_count =
		readl_relaxed(ioaddr + DMA_CHAN_MISS_FRAME_CNT(chan));
}

static void dwmac4_print_reg(void __iomem *ioaddr, u32 ch, u32 offset)
{
	u32 val = readl_relaxed(ioaddr + offset);
	u32 reg_addr = 0x28000 + ch * 0x1000 + offset;

	pr_info("----------------------\n");
	pr_info("The address is 0x%X\n", reg_addr);
	pr_info("0x%08X\n", val);
}

static void dwmac4_get_reg_info(void __iomem *ioaddr, u32 chan)
{
	pr_info("------Channel %u Register Dump------\n");
	dwmac4_print_reg(ioaddr, chan, MTL_CHAN_TX_OP_MODE(chan));
	dwmac4_print_reg(ioaddr, chan, MTL_CHAN_TX_DEBUG(chan));
	dwmac4_print_reg(ioaddr, chan, MTL_ETSX_CTRL_BASE_ADDR(chan));
	dwmac4_print_reg(ioaddr, chan, MTL_TXQX_WEIGHT_BASE_ADDR(chan));
	dwmac4_print_reg(ioaddr, chan, MTL_CHAN_INT_CTRL(chan));
	dwmac4_print_reg(ioaddr, chan, MTL_CHAN_RX_OP_MODE(chan));
	dwmac4_print_reg(ioaddr, chan, MTL_CHAN_RX_DEBUG(chan));

	dwmac4_print_reg(ioaddr, chan, DMA_CHAN_CONTROL(chan));
	dwmac4_print_reg(ioaddr, chan, DMA_CHAN_TX_CONTROL(chan));
	dwmac4_print_reg(ioaddr, chan, DMA_CHAN_RX_CONTROL(chan));
	dwmac4_print_reg(ioaddr, chan, DMA_CHAN_TX_BASE_ADDR(chan));
	dwmac4_print_reg(ioaddr, chan, DMA_CHAN_RX_BASE_ADDR(chan));
	dwmac4_print_reg(ioaddr, chan, DMA_CHAN_TX_END_ADDR(chan));
	dwmac4_print_reg(ioaddr, chan, DMA_CHAN_RX_END_ADDR(chan));
	dwmac4_print_reg(ioaddr, chan, DMA_CHAN_TX_RING_LEN(chan));
	dwmac4_print_reg(ioaddr, chan, DMA_CHAN_RX_RING_LEN(chan));
	dwmac4_print_reg(ioaddr, chan, DMA_CHAN_INTR_ENA(chan));
	dwmac4_print_reg(ioaddr, chan, DMA_CHAN_RX_WATCHDOG(chan));
	dwmac4_print_reg(ioaddr, chan, DMA_CHAN_SLOT_CTRL_STATUS(chan));
	dwmac4_print_reg(ioaddr, chan, DMA_CHAN_CUR_TX_DESC(chan));
	dwmac4_print_reg(ioaddr, chan, DMA_CHAN_CUR_RX_DESC(chan));
	dwmac4_print_reg(ioaddr, chan, DMA_CHAN_CUR_TX_BUF_ADDR(chan));
	dwmac4_print_reg(ioaddr, chan, DMA_CHAN_CUR_RX_BUF_ADDR(chan));
	dwmac4_print_reg(ioaddr, chan, DMA_CHAN_STATUS(chan));
	dwmac4_print_reg(ioaddr, chan, DMA_CHAN_MISS_FRAME_CNT(chan));
}

const struct stmmac_ops dwmac4_ops = {
	.set_mtl_tx_queue_weight = dwmac4_set_mtl_tx_queue_weight,
	.host_mtl_irq_status = dwmac4_irq_mtl_status,
	.debug = dwmac4_debug,
};

const struct stmmac_dma_ops dwmac4_dma_ops = {
	.init_chan = dwmac4_dma_init_channel,
	.init_rx_chan = dwmac4_dma_init_rx_chan,
	.init_tx_chan = dwmac4_dma_init_tx_chan,
	.dma_rx_mode = dwmac4_dma_rx_chan_op_mode,
	.dma_tx_mode = dwmac4_dma_tx_chan_op_mode,
	.enable_dma_irq = dwmac4_enable_dma_irq,
	.disable_dma_irq = dwmac4_disable_dma_irq,
	.start_tx = dwmac4_dma_start_tx,
	.stop_tx = dwmac4_dma_stop_tx,
	.start_rx = dwmac4_dma_start_rx,
	.stop_rx = dwmac4_dma_stop_rx,
	.dma_interrupt = dwmac4_dma_interrupt,
	.rx_watchdog = dwmac4_rx_watchdog,
	.set_rx_ring_len = dwmac4_set_rx_ring_len,
	.set_tx_ring_len = dwmac4_set_tx_ring_len,
	.set_rx_tail_ptr = dwmac4_set_rx_tail_ptr,
	.set_tx_tail_ptr = dwmac4_set_tx_tail_ptr,
	.enable_tso = dwmac4_enable_tso,
	.set_bfsize = dwmac4_set_bfsize,
	.dma_stats = dwmac4_get_dma_info,
	.reg_vals = dwmac4_get_reg_info,
};

int dwmac4_setup(struct stmmac_priv *priv)
{
	struct mac_device_info *mac = priv->hw;

	dev_info(priv->device, "\tDWMAC4/5\n");

	priv->dev->priv_flags |= IFF_UNICAST_FLT;
	mac->pcsr = priv->ioaddr;
	//mac->multicast_filter_bins = priv->plat->multicast_filter_bins;
	//mac->unicast_filter_entries = priv->plat->unicast_filter_entries;
	//mac->mcast_bits_log2 = 0;

	//if (mac->multicast_filter_bins)
	//	mac->mcast_bits_log2 = ilog2(mac->multicast_filter_bins);

	return 0;
}
