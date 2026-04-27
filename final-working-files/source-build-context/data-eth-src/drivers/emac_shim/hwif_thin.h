/* SPDX-License-Identifier: (GPL-2.0 OR MIT) */
// Copyright (c) 2018 Synopsys, Inc. and/or its affiliates.
// stmmac HW Interface Callbacks

#ifndef __STMMAC_HWIF_H__
#define __STMMAC_HWIF_H__

#include <linux/netdevice.h>
#include <linux/stmmac.h>

#define stmmac_do_void_callback(__priv, __module, __cname,  __arg0, __args...) \
({ \
	int __result = -EINVAL; \
	if ((__priv)->hw->__module && (__priv)->hw->__module->__cname) { \
		(__priv)->hw->__module->__cname((__arg0), ##__args); \
		__result = 0; \
	} \
	__result; \
})
#define stmmac_do_callback(__priv, __module, __cname,  __arg0, __args...) \
({ \
	int __result = -EINVAL; \
	if ((__priv)->hw->__module && (__priv)->hw->__module->__cname) \
		__result = (__priv)->hw->__module->__cname((__arg0), ##__args); \
	__result; \
})

struct stmmac_extra_stats;
struct dma_desc;

/* Descriptors helpers */
struct stmmac_desc_ops {
	/* DMA RX descriptor ring initialization */
	void (*init_rx_desc)(struct dma_desc *p, int disable_rx_ic, int mode,
			     int end, int bfsize);
	/* DMA TX descriptor ring initialization */
	void (*init_tx_desc)(struct dma_desc *p, int mode, int end);
	/* Invoked by the xmit function to prepare the tx descriptor */
	void (*prepare_tx_desc)(struct dma_desc *p, int is_fs, int len,
				bool csum_flag, int mode, bool tx_own, bool ls,
				unsigned int tot_pkt_len);
	void (*prepare_tso_tx_desc)(struct dma_desc *p, int is_fs, int len1,
				    int len2, bool tx_own, bool ls,
				    unsigned int tcphdrlen,
				    unsigned int tcppayloadlen);
	/* Set/get the owner of the descriptor */
	void (*set_tx_owner)(struct dma_desc *p);
	/* Clean the tx descriptor as soon as the tx irq is received */
	void (*release_tx_desc)(struct dma_desc *p, int mode);
	/* Clear interrupt on tx frame completion. When this bit is
	 * set an interrupt happens as soon as the frame is transmitted
	 */
	void (*set_tx_ic)(struct dma_desc *p);
	/* Return the transmit status looking at the TDES1 */
	int (*tx_status)(void *data, struct stmmac_extra_stats *x,
			 struct dma_desc *p, void __iomem *ioaddr);
	/* Handle extra events on specific interrupts hw dependent */
	void (*set_rx_owner)(struct dma_desc *p, int disable_rx_ic);
	/* Get the receive frame size */
	int (*get_rx_frame_len)(struct dma_desc *p, int rx_coe_type);
	/* Return the reception status looking at the RDES1 */
	int (*rx_status)(void *data, struct stmmac_extra_stats *x,
			 struct dma_desc *p);
	/* Set tx timestamp enable bit */
	void (*enable_tx_timestamp)(struct dma_desc *p);
	/* get tx timestamp status */
	int (*get_tx_timestamp_status)(struct dma_desc *p);
	/* get timestamp value */
	void (*get_timestamp)(void *desc, u64 *ts);
	/* get rx timestamp status */
	int (*get_rx_timestamp_status)(void *desc, void *next_desc);
	/* set MSS via context descriptor */
	void (*set_mss)(struct dma_desc *p, unsigned int mss);
	/* set descriptor skbuff address */
	void (*set_addr)(struct dma_desc *p, dma_addr_t addr);
	/* clear descriptor */
	void (*clear)(struct dma_desc *p);
	/* RSS */
	int (*get_rx_hash)(struct dma_desc *p, u32 *hash,
			   enum pkt_hash_types *type);
	int (*get_rx_header_len)(struct dma_desc *p, unsigned int *len);
	void (*set_vlan_tag)(struct dma_desc *p, u16 tag, u16 inner_tag,
			     u32 inner_type);
	void (*set_vlan)(struct dma_desc *p, u32 type);
};

#define stmmac_init_rx_desc(__priv, __args...) \
	stmmac_do_void_callback(__priv, desc, init_rx_desc, __args)
#define stmmac_init_tx_desc(__priv, __args...) \
	stmmac_do_void_callback(__priv, desc, init_tx_desc, __args)
#define stmmac_prepare_tx_desc(__priv, __args...) \
	stmmac_do_void_callback(__priv, desc, prepare_tx_desc, __args)
#define stmmac_prepare_tso_tx_desc(__priv, __args...) \
	stmmac_do_void_callback(__priv, desc, prepare_tso_tx_desc, __args)
#define stmmac_set_tx_owner(__priv, __args...) \
	stmmac_do_void_callback(__priv, desc, set_tx_owner, __args)
#define stmmac_release_tx_desc(__priv, __args...) \
	stmmac_do_void_callback(__priv, desc, release_tx_desc, __args)
#define stmmac_set_tx_ic(__priv, __args...) \
	stmmac_do_void_callback(__priv, desc, set_tx_ic, __args)
#define stmmac_tx_status(__priv, __args...) \
	stmmac_do_callback(__priv, desc, tx_status, __args)
#define stmmac_set_rx_owner(__priv, __args...) \
	stmmac_do_void_callback(__priv, desc, set_rx_owner, __args)
#define stmmac_get_rx_frame_len(__priv, __args...) \
	stmmac_do_callback(__priv, desc, get_rx_frame_len, __args)
#define stmmac_rx_status(__priv, __args...) \
	stmmac_do_callback(__priv, desc, rx_status, __args)
#define stmmac_enable_tx_timestamp(__priv, __args...) \
	stmmac_do_void_callback(__priv, desc, enable_tx_timestamp, __args)
#define stmmac_get_tx_timestamp_status(__priv, __args...) \
	stmmac_do_callback(__priv, desc, get_tx_timestamp_status, __args)
#define stmmac_get_timestamp(__priv, __args...) \
	stmmac_do_void_callback(__priv, desc, get_timestamp, __args)
#define stmmac_get_rx_timestamp_status(__priv, __args...) \
	stmmac_do_callback(__priv, desc, get_rx_timestamp_status, __args)
#define stmmac_set_mss(__priv, __args...) \
	stmmac_do_void_callback(__priv, desc, set_mss, __args)
#define stmmac_set_desc_addr(__priv, __args...) \
	stmmac_do_void_callback(__priv, desc, set_addr, __args)
#define stmmac_clear_desc(__priv, __args...) \
	stmmac_do_void_callback(__priv, desc, clear, __args)
#define stmmac_get_rx_hash(__priv, __args...) \
	stmmac_do_callback(__priv, desc, get_rx_hash, __args)
#define stmmac_get_rx_header_len(__priv, __args...) \
	stmmac_do_callback(__priv, desc, get_rx_header_len, __args)
#define stmmac_set_desc_vlan_tag(__priv, __args...) \
	stmmac_do_void_callback(__priv, desc, set_vlan_tag, __args)
#define stmmac_set_desc_vlan(__priv, __args...) \
	stmmac_do_void_callback(__priv, desc, set_vlan, __args)

struct stmmac_dma_cfg;

/* Specific DMA helpers */
struct stmmac_dma_ops {
	/* DMA core initialization */
	void (*init_chan)(void __iomem *ioaddr,
			  struct stmmac_dma_cfg *dma_cfg, u32 chan);
	void (*init_rx_chan)(void __iomem *ioaddr,
			     struct stmmac_dma_cfg *dma_cfg,
			     dma_addr_t phy, u32 chan);
	void (*init_tx_chan)(void __iomem *ioaddr,
			     struct stmmac_dma_cfg *dma_cfg,
			     dma_addr_t phy, u32 chan);
	void (*dma_rx_mode)(void __iomem *ioaddr, int mode, u32 channel,
			    int fifosz, u8 qmode);
	void (*dma_tx_mode)(void __iomem *ioaddr, int mode, u32 channel,
			    int fifosz, u8 qmode);
	void (*enable_dma_transmission)(void __iomem *ioaddr);
	void (*enable_dma_irq)(void __iomem *ioaddr, u32 chan);
	void (*disable_dma_irq)(void __iomem *ioaddr, u32 chan);
	void (*start_tx)(void __iomem *ioaddr, u32 chan);
	void (*stop_tx)(void __iomem *ioaddr, u32 chan);
	void (*start_rx)(void __iomem *ioaddr, u32 chan);
	void (*stop_rx)(void __iomem *ioaddr, u32 chan);
	int (*dma_interrupt)(void __iomem *ioaddr,
			     struct stmmac_extra_stats *x, u32 chan);
	/* Program the HW RX Watchdog */
	void (*rx_watchdog)(void __iomem *ioaddr, u32 riwt, u32 chan);
	void (*set_tx_ring_len)(void __iomem *ioaddr, u32 len, u32 chan);
	void (*set_rx_ring_len)(void __iomem *ioaddr, u32 len, u32 chan);
	void (*set_rx_tail_ptr)(void __iomem *ioaddr, u32 tail_ptr, u32 chan);
	void (*set_tx_tail_ptr)(void __iomem *ioaddr, u32 tail_ptr, u32 chan);
	void (*enable_tso)(void __iomem *ioaddr, bool en, u32 chan);
	void (*set_bfsize)(void __iomem *ioaddr, int bfsize, u32 chan);
	void (*dma_stats)(void __iomem *ioaddr,
			  struct stmmac_extra_stats *x, u32 chan);
	void (*reg_vals)(void __iomem *ioaddr, u32 chan);
};

#define stmmac_init_chan(__priv, __args...) \
	stmmac_do_void_callback(__priv, dma, init_chan, __args)
#define stmmac_init_rx_chan(__priv, __args...) \
	stmmac_do_void_callback(__priv, dma, init_rx_chan, __args)
#define stmmac_init_tx_chan(__priv, __args...) \
	stmmac_do_void_callback(__priv, dma, init_tx_chan, __args)
#define stmmac_dma_rx_mode(__priv, __args...) \
	stmmac_do_void_callback(__priv, dma, dma_rx_mode, __args)
#define stmmac_dma_tx_mode(__priv, __args...) \
	stmmac_do_void_callback(__priv, dma, dma_tx_mode, __args)
#define stmmac_enable_dma_transmission(__priv, __args...) \
	stmmac_do_void_callback(__priv, dma, enable_dma_transmission, __args)
#define stmmac_enable_dma_irq(__priv, __args...) \
	stmmac_do_void_callback(__priv, dma, enable_dma_irq, __args)
#define stmmac_disable_dma_irq(__priv, __args...) \
	stmmac_do_void_callback(__priv, dma, disable_dma_irq, __args)
#define stmmac_start_tx(__priv, __args...) \
	stmmac_do_void_callback(__priv, dma, start_tx, __args)
#define stmmac_stop_tx(__priv, __args...) \
	stmmac_do_void_callback(__priv, dma, stop_tx, __args)
#define stmmac_start_rx(__priv, __args...) \
	stmmac_do_void_callback(__priv, dma, start_rx, __args)
#define stmmac_stop_rx(__priv, __args...) \
	stmmac_do_void_callback(__priv, dma, stop_rx, __args)
#define stmmac_dma_interrupt_status(__priv, __args...) \
	stmmac_do_callback(__priv, dma, dma_interrupt, __args)
#define stmmac_rx_watchdog(__priv, __args...) \
	stmmac_do_void_callback(__priv, dma, rx_watchdog, __args)
#define stmmac_set_tx_ring_len(__priv, __args...) \
	stmmac_do_void_callback(__priv, dma, set_tx_ring_len, __args)
#define stmmac_set_rx_ring_len(__priv, __args...) \
	stmmac_do_void_callback(__priv, dma, set_rx_ring_len, __args)
#define stmmac_set_rx_tail_ptr(__priv, __args...) \
	stmmac_do_void_callback(__priv, dma, set_rx_tail_ptr, __args)
#define stmmac_set_tx_tail_ptr(__priv, __args...) \
	stmmac_do_void_callback(__priv, dma, set_tx_tail_ptr, __args)
#define stmmac_enable_tso(__priv, __args...) \
	stmmac_do_void_callback(__priv, dma, enable_tso, __args)
#define stmmac_set_dma_bfsize(__priv, __args...) \
	stmmac_do_void_callback(__priv, dma, set_bfsize, __args)
#define stmmac_get_dma_stats(__priv, __args...) \
	stmmac_do_void_callback(__priv, dma, dma_stats, __args)
#define stmmac_get_reg(__priv, __args...) \
	stmmac_do_void_callback(__priv, dma, reg_vals, __args)

struct mac_device_info;

/* Helpers to program the MAC core */
struct stmmac_ops {
	/* Set MTL TX queues weight */
	void (*set_mtl_tx_queue_weight)(struct mac_device_info *hw,
					u32 weight, u32 queue);
	/* Handle MTL interrupts */
	int (*host_mtl_irq_status)(struct mac_device_info *hw, u32 chan);
	void (*debug)(void __iomem *ioaddr, struct stmmac_extra_stats *x,
		      u32 rx_queue, u32 tx_queue);
};

#define stmmac_set_mtl_tx_queue_weight(__priv, __args...) \
	stmmac_do_void_callback(__priv, mac, set_mtl_tx_queue_weight, __args)
#define stmmac_host_mtl_irq_status(__priv, __args...) \
	stmmac_do_callback(__priv, mac, host_mtl_irq_status, __args)
#define stmmac_mac_debug(__priv, __args...) \
	stmmac_do_void_callback(__priv, mac, debug, __args)

struct stmmac_priv;

int stmmac_hwif_init(struct stmmac_priv *priv);

extern const struct stmmac_ops dwmac4_ops;
extern const struct stmmac_dma_ops dwmac4_dma_ops;
extern const struct stmmac_desc_ops dwmac4_desc_ops;

#endif /* __STMMAC_HWIF_H__ */
