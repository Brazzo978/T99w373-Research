/* SPDX-License-Identifier: GPL-2.0-only */
/* DWMAC4 DMA and MTL Header file.
 *
 * Copyright (C) 2007-2015  STMicroelectronics Ltd
 *
 * Author: Alexandre Torgue <alexandre.torgue@st.com>
 */

#ifndef __DWMAC4_THIN_H__
#define __DWMAC4_THIN_H__

/*  MTL debug */
#define MTL_DEBUG_TXSTSFSTS		BIT(5)
#define MTL_DEBUG_TXFSTS		BIT(4)
#define MTL_DEBUG_TWCSTS		BIT(3)

/* MTL debug: Tx FIFO Read Controller Status */
#define MTL_DEBUG_TRCSTS_MASK		GENMASK(2, 1)
#define MTL_DEBUG_TRCSTS_SHIFT		1
#define MTL_DEBUG_TRCSTS_IDLE		0
#define MTL_DEBUG_TRCSTS_READ		1
#define MTL_DEBUG_TRCSTS_TXW		2
#define MTL_DEBUG_TRCSTS_WRITE		3
#define MTL_DEBUG_TXPAUSED		BIT(0)

/* MAC debug: GMII or MII Transmit Protocol Engine Status */
#define MTL_DEBUG_RXFSTS_MASK		GENMASK(5, 4)
#define MTL_DEBUG_RXFSTS_SHIFT		4
#define MTL_DEBUG_RXFSTS_EMPTY		0
#define MTL_DEBUG_RXFSTS_BT		1
#define MTL_DEBUG_RXFSTS_AT		2
#define MTL_DEBUG_RXFSTS_FULL		3
#define MTL_DEBUG_RRCSTS_MASK		GENMASK(2, 1)
#define MTL_DEBUG_RRCSTS_SHIFT		1
#define MTL_DEBUG_RRCSTS_IDLE		0
#define MTL_DEBUG_RRCSTS_RDATA		1
#define MTL_DEBUG_RRCSTS_RSTAT		2
#define MTL_DEBUG_RRCSTS_FLUSH		3
#define MTL_DEBUG_RWCSTS		BIT(0)

/*  MTL channel registers
 *  The ioaddr configured in dtsi offsets to the channel base address
 *  for single channel access (ex. for queue 2: offset a000 is mapped).
 *  So no need to provide base addr offset by queue number.
 */
#define MTL_CHAN_TX_OP_MODE(x)		0
#define MTL_CHAN_TX_DEBUG(x)		(0x8)
#define MTL_ETSX_CTRL_BASE_ADDR(x)	(0x10)
#define MTL_TXQX_WEIGHT_BASE_ADDR(x)	(0x18)
#define MTL_CHAN_INT_CTRL(x)		(0x2c)
#define MTL_CHAN_RX_OP_MODE(x)		(0x30)
#define MTL_CHAN_RX_DEBUG(x)		(0x38)

/* MTL Queue Quantum Weight */
#define MTL_TXQ_WEIGHT_ISCQW_MASK	GENMASK(20, 0)
/* MTL interrupt */
#define MTL_RX_OVERFLOW_INT_EN		BIT(24)
#define MTL_RX_OVERFLOW_INT		BIT(16)

#define MTL_OP_MODE_RSF			BIT(5)
#define MTL_OP_MODE_TXQEN_MASK		GENMASK(3, 2)
#define MTL_OP_MODE_TXQEN_AV		BIT(2)
#define MTL_OP_MODE_TXQEN		BIT(3)
#define MTL_OP_MODE_TSF			BIT(1)

#define MTL_OP_MODE_TQS_MASK		GENMASK(24, 16)
#define MTL_OP_MODE_TQS_SHIFT		16

#define MTL_OP_MODE_TTC_MASK		0x70
#define MTL_OP_MODE_TTC_SHIFT		4

#define MTL_OP_MODE_TTC_32		0
#define MTL_OP_MODE_TTC_64		(1 & BIT(4))
#define MTL_OP_MODE_TTC_96		(2 << MTL_OP_MODE_TTC_SHIFT)
#define MTL_OP_MODE_TTC_128		(3 << MTL_OP_MODE_TTC_SHIFT)
#define MTL_OP_MODE_TTC_192		(4 << MTL_OP_MODE_TTC_SHIFT)
#define MTL_OP_MODE_TTC_256		(5 << MTL_OP_MODE_TTC_SHIFT)
#define MTL_OP_MODE_TTC_384		(6 << MTL_OP_MODE_TTC_SHIFT)
#define MTL_OP_MODE_TTC_512		(7 << MTL_OP_MODE_TTC_SHIFT)

#define MTL_OP_MODE_RQS_MASK		GENMASK(29, 20)
#define MTL_OP_MODE_RQS_SHIFT		20

#define MTL_OP_MODE_RFD_MASK		GENMASK(19, 14)
#define MTL_OP_MODE_RFD_SHIFT		14

#define MTL_OP_MODE_RFA_MASK		GENMASK(13, 8)
#define MTL_OP_MODE_RFA_SHIFT		8

#define MTL_OP_MODE_EHFC		BIT(7)

#define MTL_OP_MODE_RTC_MASK		0x18
#define MTL_OP_MODE_RTC_SHIFT		3

#define MTL_OP_MODE_RTC_32		(1 & BIT(3))
#define MTL_OP_MODE_RTC_64		0
#define MTL_OP_MODE_RTC_96		(2 << MTL_OP_MODE_RTC_SHIFT)
#define MTL_OP_MODE_RTC_128		(3 << MTL_OP_MODE_RTC_SHIFT)

/* DMA SYS Bus Mode bitmap */
#define DMA_BUS_MODE_SPH		BIT(24)
#define DMA_BUS_MODE_PBL		BIT(16)
#define DMA_BUS_MODE_PBL_SHIFT		16
#define DMA_BUS_MODE_RPBL_SHIFT		16
#define DMA_BUS_MODE_MB			BIT(14)
#define DMA_BUS_MODE_FB			BIT(0)

/* Define the max channel number used for tx (also rx).
 * dwmac4 accepts up to 8 channels for TX (and also 8 channels for RX
 */
#define DMA_CHANNEL_NB_MAX		1

/* Following DMA defines are channels oriented */
#define DMA_CHAN_REG_NUMBER		17

#define DMA_CHANX_BASE_ADDR(x)		0x100
#define DMA_CHAN_CONTROL(x)		DMA_CHANX_BASE_ADDR(x)
#define DMA_CHAN_TX_CONTROL(x)		(DMA_CHANX_BASE_ADDR(x) + 0x4)
#define DMA_CHAN_RX_CONTROL(x)		(DMA_CHANX_BASE_ADDR(x) + 0x8)
#define DMA_CHAN_TX_BASE_ADDR(x)	(DMA_CHANX_BASE_ADDR(x) + 0x14)
#define DMA_CHAN_RX_BASE_ADDR(x)	(DMA_CHANX_BASE_ADDR(x) + 0x1c)
#define DMA_CHAN_TX_END_ADDR(x)		(DMA_CHANX_BASE_ADDR(x) + 0x20)
#define DMA_CHAN_RX_END_ADDR(x)		(DMA_CHANX_BASE_ADDR(x) + 0x28)
#define DMA_CHAN_TX_RING_LEN(x)		(DMA_CHANX_BASE_ADDR(x) + 0x2c)
#define DMA_CHAN_RX_RING_LEN(x)		(DMA_CHANX_BASE_ADDR(x) + 0x30)
#define DMA_CHAN_INTR_ENA(x)		(DMA_CHANX_BASE_ADDR(x) + 0x34)
#define DMA_CHAN_RX_WATCHDOG(x)		(DMA_CHANX_BASE_ADDR(x) + 0x38)
#define DMA_CHAN_SLOT_CTRL_STATUS(x)	(DMA_CHANX_BASE_ADDR(x) + 0x3c)
#define DMA_CHAN_CUR_TX_DESC(x)		(DMA_CHANX_BASE_ADDR(x) + 0x44)
#define DMA_CHAN_CUR_RX_DESC(x)		(DMA_CHANX_BASE_ADDR(x) + 0x4c)
#define DMA_CHAN_CUR_TX_BUF_ADDR(x)	(DMA_CHANX_BASE_ADDR(x) + 0x54)
#define DMA_CHAN_CUR_RX_BUF_ADDR(x)	(DMA_CHANX_BASE_ADDR(x) + 0x5c)
#define DMA_CHAN_STATUS(x)		(DMA_CHANX_BASE_ADDR(x) + 0x60)
#define DMA_CHAN_MISS_FRAME_CNT(x)	(DMA_CHANX_BASE_ADDR(x) + 0x64)

/* DMA Control X */
#define DMA_CONTROL_MSS_MASK		GENMASK(13, 0)

/* DMA Tx Channel X Control register defines */
#define DMA_CONTROL_TSE			BIT(12)
#define DMA_CONTROL_OSP			BIT(4)
#define DMA_CONTROL_ST			BIT(0)

/* DMA Rx Channel X Control register defines */
#define DMA_CONTROL_SR			BIT(0)
#define DMA_RBSZ_MASK			GENMASK(14, 1)
#define DMA_RBSZ_SHIFT			1

/* Interrupt status per channel */
#define DMA_CHAN_STATUS_REB		GENMASK(21, 19)
#define DMA_CHAN_STATUS_REB_SHIFT	19
#define DMA_CHAN_STATUS_TEB		GENMASK(18, 16)
#define DMA_CHAN_STATUS_TEB_SHIFT	16
#define DMA_CHAN_STATUS_NIS		BIT(15)
#define DMA_CHAN_STATUS_AIS		BIT(14)
#define DMA_CHAN_STATUS_CDE		BIT(13)
#define DMA_CHAN_STATUS_FBE		BIT(12)
#define DMA_CHAN_STATUS_ERI		BIT(11)
#define DMA_CHAN_STATUS_ETI		BIT(10)
#define DMA_CHAN_STATUS_RWT		BIT(9)
#define DMA_CHAN_STATUS_RPS		BIT(8)
#define DMA_CHAN_STATUS_RBU		BIT(7)
#define DMA_CHAN_STATUS_RI		BIT(6)
#define DMA_CHAN_STATUS_TBU		BIT(2)
#define DMA_CHAN_STATUS_TPS		BIT(1)
#define DMA_CHAN_STATUS_TI		BIT(0)

/* Interrupt enable bits per channel */
#define DMA_CHAN_INTR_ENA_NIE		BIT(15)
#define DMA_CHAN_INTR_ENA_AIE		BIT(14)
#define DMA_CHAN_INTR_ENA_CDE		BIT(13)
#define DMA_CHAN_INTR_ENA_FBE		BIT(12)
#define DMA_CHAN_INTR_ENA_ERE		BIT(11)
#define DMA_CHAN_INTR_ENA_ETE		BIT(10)
#define DMA_CHAN_INTR_ENA_RWE		BIT(9)
#define DMA_CHAN_INTR_ENA_RSE		BIT(8)
#define DMA_CHAN_INTR_ENA_RBUE		BIT(7)
#define DMA_CHAN_INTR_ENA_RIE		BIT(6)
#define DMA_CHAN_INTR_ENA_TBUE		BIT(2)
#define DMA_CHAN_INTR_ENA_TSE		BIT(1)
#define DMA_CHAN_INTR_ENA_TIE		BIT(0)

#define DMA_CHAN_INTR_NORMAL		(DMA_CHAN_INTR_ENA_NIE | \
					 DMA_CHAN_INTR_ENA_RIE | \
					 DMA_CHAN_INTR_ENA_TIE)

#define DMA_CHAN_INTR_ABNORMAL		(DMA_CHAN_INTR_ENA_AIE | \
					 DMA_CHAN_INTR_ENA_FBE | \
					 DMA_CHAN_INTR_ENA_RSE | \
					 DMA_CHAN_INTR_ENA_RBUE)

/* DMA default interrupt mask for 4.00 */
#define DMA_CHAN_INTR_DEFAULT_MASK	(DMA_CHAN_INTR_NORMAL | \
					 DMA_CHAN_INTR_ABNORMAL)

#endif /* __DWMAC4_THIN_H__ */
