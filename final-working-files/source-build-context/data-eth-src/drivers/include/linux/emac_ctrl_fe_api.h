/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */

#ifndef _EMAC_CTRL_FE_API_H_
#define _EMAC_CTRL_FE_API_H_

#include <linux/if_ether.h>
#include <linux/notifier.h>

#define ETHER_ADDR_LEN  ETH_ALEN

/*
 * EMAC CTRL FRONTEND DRIVER APIs for EMAC DMA DRIVER CLIENT
 */

struct unicast_mac_addr {
	uint8_t enm_addr[ETH_ALEN];
};

struct multicast_mac_addr {
	uint8_t enm_addr[ETH_ALEN];
};

union emac_ctrl_fe_filter{
	uint16_t                  vlan_id;        //2 bytes
	struct unicast_mac_addr   unicast_mac;    //6 bytes
	struct multicast_mac_addr multi_mac;      //6 bytes
};


/*filter types*/
enum emac_ctrl_fe_filter_types {
	UNICAST_FILTER=0,
	MULTICAST_FILTER=1,
	VLAN_FILTER=2,
};


/* Event codes passed from EMAC Ctrl FE to EMAC DMA Driver
 * Please note these are coupled with host side signals, any
 * changes here should reflect there as well
 */
enum emac_ctrl_fe_gvm_event {
	/* EMAC HW is not powered up*/
	EMAC_HW_DOWN = 0,
	/* EMAC HW clocks are voted and registers accesible*/
	EMAC_HW_UP =1,
	/* EMAC PHY Link is down*/
	EMAC_LINK_DOWN=2,
	/* EMAC PHY link is up*/
	EMAC_LINK_UP=3,
	/* Interrupt in DMA channel, SW must read process and clear Ch Status Reg*/
	EMAC_DMA_INT_STS_AVAIL=4,
};

enum emac_ctrl_dma_drv_event {
	/* Unmount from front end*/
	EMAC_DMA_DRV_UNMOUNT = 0,
	/* DMA Driver need to suspend.*/
	EMAC_DMA_DRV_SUSPEND = 1,
};

/* dma driver will call
 * static struct notifier_block emac_dma_notifier = {
 *   .notifier_call = emac_ctrl_fe_event_hdlr,
 * };
 * emac_ctrl_fe_register_notifier (&emac_dma_notifier);
 * static int emac_ctrl_fe_event_hdlr (struct notifier_block *unused , unsigned long event, void *data)
 */

int emac_ctrl_fe_register_notifier(struct notifier_block *nb);
int emac_ctrl_fe_unregister_notifier(struct notifier_block *nb);

typedef void (*emac_ctrl_fe_ready_cb) (void *user_data);

int emac_ctrl_fe_register_ready_cb(
	void (*emac_ctrl_fe_ready_cb)(void *user_data),
	void *user_data);

/* Notify to EMAC CTRL FE that DMA is stopped and safe to pull the plug */
void emac_ctrl_fe_gvm_dma_stopped(void);

/* request filter addition at EMAC HW*/
int emac_ctrl_fe_filter_add_request(enum emac_ctrl_fe_filter_types filter_type,
	union emac_ctrl_fe_filter *filter );

/* request filter deletion at EMAC HW*/
int emac_ctrl_fe_filter_del_request(enum emac_ctrl_fe_filter_types filter_type,
	union emac_ctrl_fe_filter filter );

/* request mac_addr_chg_req*/
int emac_ctrl_fe_mac_addr_chg(struct unicast_mac_addr *new_mac_addr);

#endif /* _EMAC_CTRL_FE_API_H_ */