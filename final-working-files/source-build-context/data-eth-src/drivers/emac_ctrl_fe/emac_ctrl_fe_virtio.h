/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */

#ifndef _EMAC_CTRL_FE_VIRTIO_DRIVER_H_
#define _EMAC_CTRL_FE_VIRTIO_DRIVER_H_

#include <linux/virtio_config.h>

/*Virtio ID of EMAC*/
#define VIRTIO_ID_EMAC_CTL_FE       38

/* VIRTIO Driver Feature Flags*/
#define VERSION_MAJOR                0
#define VERSION_MINOR                1

#define EMAC_CTRL_FE_VIRTIO_VERSION


/*message header*/

/* EMAC_CTRL_FRONTEND Feature flags and defines*/
#define EMAC_MAX_VLAN_ID          4094
#define EMAC_MIN_VLAN_ID             1
/*check vlan validity*/
#define IS_VLAN_ID_VALID(vlan_id)    ( vlan_id<EMAC_MAX_VLAN_ID && vlan_id >=EMAC_MIN_VLAN_ID)

#define DRIVER_NAME "emac_ctrl_fe_virtio_driver"

#define EMAC_CTL_FE_ERR(fmt, args...)                            \
	pr_err("[ ERR %s: %d] " fmt, __func__,  __LINE__, ##args)
#define EMAC_CTL_FE_WARN(fmt, args...)                           \
	pr_warn("[ WARN: %s: %d] " fmt, __func__,  __LINE__, ##args)
#define EMAC_CTL_FE_INFO(fmt, args...)                           \
	pr_info("[ INFO: %s: %d] " fmt, __func__,  __LINE__, ##args)
#define EMAC_CTL_FE_DBG(fmt, args...)                           \
	pr_debug("[ DBG: %s: %d] " fmt, __func__,  __LINE__, ##args)

enum {
	EMAC_CTRL_FE_TX_VQ =0,
	EMAC_CTRL_FE_RX_VQ =1,
	EMAC_CTRL_FE_VIRTQ_NUM =2,
}emac_ctrl_fe_VIRTQ;

/**
 * EMAC_CTRL_FE_DMA_DRV_DOWN  - Thin Driver down state.
 * EMAC_CTRL_FE_DMA_DRV_UNREG - Thin Driver unregistered.
 * EMAC_CTRL_FE_DMA_DRV_REG   - Thin Driver  registered for
 * events.
 * EMAC_CTRL_FE_DMA_DRV_SUSPEND - GVM SUSPEND
					*/
enum emac_dma_drv_state_type{
	EMAC_CTRL_FE_DMA_DRV_DOWN =0,
	EMAC_CTRL_FE_DMA_DRV_UNREG=1,
	EMAC_CTRL_FE_DMA_DRV_SUSPEND=3,
	EMAC_CTRL_FE_DMA_DRV_REG=4,
};

/**
 * EMAC_CTRL_FE_DOWN - Front end is down state, no link with back end
 * EMAC_CTRL_FE_EVENT_LISTN_INIT  - comm with BE established
 * EMAC_CTRL_FE_DMA_HW_UP_NOTIFIED - DMA_DRV notified of HW_UP State
 * EMAC_CTRL_FE_DMA_LINK_UP_NOTIFIED - DMA_DRV notified of LINK_UP State
 * EMAC_CTRL_FE_DMA_LINK_DOWN_NOTIFIED - DMA_DRV notified of LINK_DOWN State
 * EMAC_CTRL_FE_DMA_HW_DOWN_NOTIFIED - DMA DRV notified of HW need to go dow
 */
enum emac_ctrl_fe_state_type{
	EMAC_CTRL_FE_DOWN =0,
	EMAC_CTRL_FE_EVENT_LISTN_INIT=1,
	EMAC_CTRL_FE_HW_DOWN_NOTIFIED=2,
	EMAC_CTRL_FE_HW_UP_NOTIFIED=3,
	EMAC_CTRL_FE_LINK_UP_NOTIFIED=4,
	EMAC_CTRL_FE_LINK_DOWN_NOTIFIED=5,
};

struct emac_ctrl_fe_msg {
	u16 msgid;
	struct completion work;
	void *txbuf;
	void *rxbuf;
	u32 txbuf_size;
	u32 rxbuf_used;
	u64 ts_begin;
	u64 ts_end;
};

enum emac_ctrl_fe_to_be_cmds {
	VIRTIO_EMAC_DMA_VIRT_UNREG_EVENTS=0,
	VIRTIO_EMAC_DMA_VIRT_REG_EVENTS=1,
	VIRTIO_EMAC_DMA_VIRT_UNICAST_MAC_ADD=2,
	VIRTIO_EMAC_DMA_VIRT_UNICAST_MAC_DEL=3,
	VIRTIO_EMAC_DMA_VIRT_MULTICAST_ADD=4,
	VIRTIO_EMAC_DMA_VIRT_MULTICAST_DEL=5,
	VIRTIO_EMAC_DMA_VIRT_VLAN_FTR_ADD=6,
	VIRTIO_EMAC_DMA_VIRT_VLAN_FTR_DEL=7,
	VIRTIO_EMAC_DMA_STOP_ACK=8,
};

/*Analogous to emac_ctrl_fe_gvm_event
enum emac_ctrl_be_to_fe_cmds {
	VIRTIO_EMAC_HW_UP=10,
	VIRTIO_EMAC_LINK_UP=11,
	VIRTIO_EMAC_LINK_DOWN=12,
	VIRTIO_EMAC_HW_DOWN=13,
	VIRTIO_EMAC_DMA_INT_STS_AVAIL=14,
};
*/

struct emac_ctrl_be_to_fe_virtio_msg {
	uint16_t msgid;        /* unique message id */
	uint16_t len;          /* command total length */
	uint32_t cmd;          /* command */
	int32_t result;        /* command result */
} __packed;

struct emac_ctrl_fe_to_be_virtio_msg {
	uint8_t type;         /* unique message id       1byte  */
	uint16_t len;         /* command total length    2byte  */
	uint8_t result;       /* command result          1byte  */
	//uint32_t data[4];      /* data field*/
	union emac_ctrl_fe_filter    request_data;/* opt data  6byte  */
} __packed;

struct emac_ctrl_fe_virtio_dev {

	/*device driver properties*/
	char                         *name;

	/*Virtio device*/
	struct virtio_device          *vdev;
	struct virtqueue              *emac_ctl_txq;
	spinlock_t                     txq_lock;
	struct virtqueue              *emac_ctl_rxq;
	spinlock_t                     rxq_lock;

	struct emac_ctrl_fe_to_be_virtio_msg tx_msg;
	struct emac_ctrl_be_to_fe_virtio_msg rx_msg[10];

	/*Emac FE Control Driver*/
	enum emac_ctrl_fe_state_type   emac_ctrl_fe_state;
	bool                           emac_ctrl_fe_ready;
	struct list_head               emac_ctrl_fe_ready_cb_list;
	struct mutex                   emac_ctl_fe_lock;

	enum emac_dma_drv_state_type   emac_dma_drv_state;

};

#endif /* _EMAC_CTRL_FE_VIRTIO_DRIVER_H_ */