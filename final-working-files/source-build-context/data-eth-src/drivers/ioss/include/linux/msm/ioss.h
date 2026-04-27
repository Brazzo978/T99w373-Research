/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */

#ifndef _IOSS_H_
#define _IOSS_H_

#include <linux/list.h>
#include <linux/bitops.h>
#include <linux/ipc_logging.h>
#include <linux/scatterlist.h>

#include <linux/device.h>
#include <linux/netdevice.h>

#include <linux/pci.h>
#include <linux/pm_wakeup.h>
#include <linux/refcount.h>

/**
 *   API
 * Version    Changes
 * ---------------------------------------------------------------------------
 *   1      - Initial version
 *   2      - Support for crashdump collection
 *   3      - Added driver ops to report statistics and status
 *   4      - Added head and tail pointer addresses to ioss_channel
 *   5      - Added ioss_channel_{map,unmap}_event() API as we moved DMA mapping
 *            of event doorbell address logic to glue drivers
 *   6      - Change one-to-many mapping to one-to-one mapping from ioss_device to
 *            ioss_interface
 */

#define IOSS_API_VER 6
#define IOSS_SUBSYS "ioss"

#define __ioss_log_msg(ipcbuf, fmt, args...) \
	do { \
		void *__buf = (ipcbuf); \
		if (__buf) \
			ipc_log_string(__buf, " %s:%d " fmt "\n", \
					__func__, __LINE__, ## args); \
	} while (0)

#define ioss_log_err(dev, fmt, args...) \
	do { \
		void *ioss_get_ipclog_buf_prio(void); \
		void *ioss_get_ipclog_buf_norm(void); \
		dev_err(dev, IOSS_SUBSYS ":ERR:" fmt "\n", ##args); \
		__ioss_log_msg(ioss_get_ipclog_buf_prio(), \
					"ERR:" fmt, ## args); \
		__ioss_log_msg(ioss_get_ipclog_buf_norm(), \
					"ERR:" fmt, ## args); \
	} while (0)

#define ioss_log_bug(dev, fmt, args...) \
	do { \
		ioss_log_err(dev, "BUG:" fmt, ##args); \
		dump_stack(); \
	} while (0)

#define ioss_log_cfg(dev, fmt, args...) \
	do { \
		void *ioss_get_ipclog_buf_prio(void); \
		void *ioss_get_ipclog_buf_norm(void); \
		dev_info(dev, IOSS_SUBSYS ":cfg:" fmt "\n", ##args); \
		__ioss_log_msg(ioss_get_ipclog_buf_prio(), \
					"cfg:" fmt, ## args); \
		__ioss_log_msg(ioss_get_ipclog_buf_norm(), \
					"cfg:" fmt, ## args); \
	} while (0)

#define ioss_log_msg(dev, fmt, args...) \
	do { \
		void *ioss_get_ipclog_buf_norm(void); \
		dev_dbg(dev, IOSS_SUBSYS fmt, ##args); \
		__ioss_log_msg(ioss_get_ipclog_buf_norm(), fmt, ## args); \
	} while (0)

#define ioss_log_dbg(dev, fmt, args...) \
	do { \
		void *ioss_get_ipclog_buf_debug(void); \
		dev_dbg(dev, IOSS_SUBSYS ":dbg:" fmt "\n", ##args); \
		__ioss_log_msg(ioss_get_ipclog_buf_debug(), \
					"dbg:" fmt, ## args); \
	} while (0)

/**
 * enum ioss_device_events - Events supported by a device
 * @IOSS_DEV_EV_MSI_BIT: MSI interrupt
 * @IOSS_DEV_EV_PTR_BIT: Ring pointer write-back
 */
enum ioss_device_events {
	IOSS_DEV_EV_MSI_BIT,
	IOSS_DEV_EV_PTR_BIT,
};

#define IOSS_DEV_EV(ev) BIT(IOSS_DEV_EV_##ev##_BIT)

enum ioss_device_event {
	IOSS_DEV_EV_MSI = IOSS_DEV_EV(MSI),
	IOSS_DEV_EV_PTR = IOSS_DEV_EV(PTR),
};

/**
 * enum ioss_channel_dir - Direction of a ring / channel
 * @IOSS_CH_DIR_RX: Traffic flowing from device to IPA
 * @IOSS_CH_DIR_TX: Traffic flowing from IPA to device
 */
enum ioss_channel_dir {
	IOSS_CH_DIR_RX,
	IOSS_CH_DIR_TX,
	IOSS_CH_DIR_MAX,
};

/**
 * enum ioss_offload_state - Offload state of a device
 * @IOSS_IF_ST_DEINITED: No offload path resources are allocated
 * @IOSS_IF_ST_INITED: Offload path resources are allocated, but not started
 * @IOSS_IF_ST_STARTED: Offload path is started and ready to handle traffic
 * @IOSS_IF_ST_ERROR: One or more offload path components are in error state
 * @IOSS_IF_ST_RECOVERY: Offload path is attempting to recover from error
 */
enum ioss_interface_state {
	IOSS_IF_ST_OFFLINE,
	IOSS_IF_ST_ONLINE,
	IOSS_IF_ST_ERROR,
	IOSS_IF_ST_RECOVERY,
	IOSS_IF_ST_MAX,
};

/* Rx filter types */
enum {
	IOSS_RXF_BE, /* Best effort */
	IOSS_RXF_IP, /* IP traffic */
};

enum ioss_filter_types {
	IOSS_RXF_F_BE = BIT(IOSS_RXF_BE),
	IOSS_RXF_F_IP = BIT(IOSS_RXF_IP),
};

extern struct bus_type ioss_bus;
extern struct device_type ioss_idev_type;
extern struct device_type ioss_iface_type;

/* IOSS platform node */
struct ioss {
	u32 max_ddr_bandwidth;
	struct workqueue_struct *wq;
	struct platform_device *pdev;

	struct notifier_block pci_bus_nb;

	void *ioss_priv;

	unsigned long status;
};

struct ioss_driver;
struct ioss_device;

struct ioss_interface {
	struct device dev;
	enum ioss_interface_state state;

	u32 instance_id;

	struct notifier_block net_dev_nb;
	struct work_struct refresh;
	struct list_head channels;

	void *ioss_priv;

	u32 link_speed;

	struct {
		u64 rx_packets;
		u64 rx_bytes;
		u64 rx_drops;
	} exception_stats;

	struct net_device_ops netdev_ops;
	const struct net_device_ops *netdev_ops_real;

	struct notifier_block pm_nb;
	struct wakeup_source *active_ws;
	struct delayed_work check_active;
	struct rtnl_link_stats64 netdev_stats;
};

#define to_ioss_interface(device) \
	container_of(device, struct ioss_interface, dev)

#define ioss_iface_dev(iface) \
	container_of(iface, struct ioss_device, interface)

#define ioss_iface_to_netdev(iface) \
		(iface->dev.parent ? to_net_dev(iface->dev.parent) : NULL)

static inline int __match_iface(struct device *dev, void *data)
{
	return dev->type == &ioss_iface_type;
}

static inline struct ioss_interface *ioss_netdev_to_iface(
			struct net_device *net_dev)
{
	struct device *iface_dev =
			device_find_child(&net_dev->dev, NULL, __match_iface);

	return iface_dev ? to_ioss_interface(iface_dev) : NULL;
}

#define ioss_nb_to_iface(nb) \
	container_of(nb, struct ioss_interface, net_dev_nb)

#define ioss_refresh_work_to_iface(work) \
	container_of(work, struct ioss_interface, refresh)

#define ioss_active_work_to_iface(work) \
	container_of(work, struct ioss_interface, check_active.work)

struct ioss_mem {
	void *addr;
	size_t size;

	dma_addr_t daddr;

	/* IOSS internal */
	struct list_head node;
	struct sg_table sgt;
};

struct ioss_mem_allocator {
	const char *name;

	void * (*alloc)(struct ioss_device *idev,
			size_t size, dma_addr_t *daddr,
			gfp_t gfp, struct ioss_mem_allocator *alctr);

	void (*free)(struct ioss_device *idev,
			size_t size, void *addr, dma_addr_t daddr,
			struct ioss_mem_allocator *alctr);

	phys_addr_t (*pa)(struct ioss_device *idev,
			void *addr, dma_addr_t daddr,
			struct ioss_mem_allocator *alctr);

	size_t (*get)(size_t size);
	void (*put)(size_t size);

	/* IOSS internal */
	struct ioss *iroot;
	void *ioss_priv;
};

struct ioss_channel_config {
	u32 ring_size;
	u32 buff_size;

	struct ioss_mem_allocator *desc_alctr;
	struct ioss_mem_allocator *buff_alctr;
};

enum ioss_channel_event_type {
	IOSS_CH_EV_MSI,
	IOSS_CH_EV_PTR,
};

struct ioss_channel;

struct ioss_channel_event {
	struct ioss_channel *channel;

	enum ioss_channel_event_type type;
	phys_addr_t paddr;
	dma_addr_t daddr;
	u64 data;

	u32 mod_count_min;
	u32 mod_count_max;
	u32 mod_usecs_min;
	u32 mod_usecs_max;

	void *private;

	/* IOSS managed */
	bool allocated;
	bool enabled;
};

struct ioss_channel {
	struct list_head node;

	struct ioss_interface *iface;

	struct ioss_channel_config default_config;
	struct ioss_channel_config config;
	enum ioss_channel_dir direction;
	enum ioss_filter_types filter_types;

	struct dentry *debugfs;

	struct ioss_channel_event event;

	int id;

	struct list_head desc_mem;
	struct list_head buff_mem;

	phys_addr_t head_ptr_addr;
	phys_addr_t tail_ptr_addr;

	void *private;

	bool allocated;
	bool enabled;

	void *ioss_priv;
};

struct ioss_device {
	struct ioss *root;
	struct device dev;
	struct net_device *net_dev; /* Real net dev */
	struct ethtool_drvinfo drv_info;

	struct dentry *debugfs;
	struct ioss_interface interface;

	struct notifier_block panic_nb;

	struct ethtool_wolinfo wol;

	void *private;

	bool unbinding;

	struct {
		u64 apps_suspend;
		u64 apps_resume;
		u64 system_suspend;
		u64 system_resume;
	} pm_stats;
};

#define to_ioss_device(device) \
	container_of(device, struct ioss_device, dev)

#define ioss_idev_to_real(idev) (idev->dev.parent)

static inline int __match_idev(struct device *dev, void *data)
{
	return dev->type == &ioss_idev_type;
}

static inline struct ioss_device *ioss_real_to_idev(struct device *real_dev)
{
	struct device *idev_dev =
			device_find_child(real_dev, NULL, __match_idev);

	return idev_dev ? to_ioss_device(idev_dev) : NULL;
}

static inline const char *__ioss_dev_name(struct ioss_device *idev)
{
	if (!idev)
		return NULL;

	if (idev->net_dev)
		return idev->net_dev->name;

	return dev_name(idev->dev.parent);
}

static inline const char *ioss_dev_name(struct ioss_device *idev)
{
	const char *name = __ioss_dev_name(idev);

	if (!name)
		name = "<noname>";

	return name;
}

#define ioss_dev_err(idev, fmt, args...) \
	do { \
		struct ioss_device *__idev = (idev); \
		struct device *dev = __idev ? &__idev->dev : NULL; \
		ioss_log_err(dev, "(%s) " fmt, ioss_dev_name(idev), ## args); \
	} while (0)

#define ioss_dev_bug(idev, fmt, args...) \
	do { \
		struct ioss_device *__idev = (idev); \
		struct device *dev = __idev ? &__idev->dev : NULL; \
		ioss_log_bug(dev, "(%s) " fmt, ioss_dev_name(idev), ## args); \
	} while (0)

#define ioss_dev_dbg(idev, fmt, args...) \
	do { \
		struct ioss_device *__idev = (idev); \
		struct device *dev = __idev ? &__idev->dev : NULL; \
		ioss_log_dbg(dev, "(%s) " fmt, ioss_dev_name(idev), ## args); \
	} while (0)

#define ioss_dev_log(idev, fmt, args...) \
	do { \
		struct ioss_device *__idev = (idev); \
		struct device *dev = __idev ? &__idev->dev : NULL; \
		ioss_log_msg(dev, "(%s) " fmt, ioss_dev_name(idev), ## args); \
	} while (0)

#define ioss_dev_cfg(idev, fmt, args...) \
	do { \
		struct ioss_device *__idev = (idev); \
		struct device *dev = __idev ? &__idev->dev : NULL; \
		ioss_log_cfg(dev, "(%s) " fmt, ioss_dev_name(idev), ## args); \
	} while (0)

struct ioss_device_stats {
	u64 hwp_rx_packets;
	u64 hwp_tx_packets;
	u64 hwp_rx_bytes;
	u64 hwp_tx_bytes;
	u64 hwp_rx_errors;
	u64 hwp_tx_errors;
	u64 hwp_rx_drops;
	u64 hwp_tx_drops;
	u64 exp_rx_packets;
	u64 exp_tx_packets;
	u64 exp_rx_bytes;
	u64 exp_tx_bytes;
	u64 exp_rx_errors;
	u64 exp_tx_errors;
	u64 exp_rx_drops;
	u64 exp_tx_drops;
	u64 emac_rx_packets;
	u64 emac_tx_packets;
	u64 emac_rx_bytes;
	u64 emac_tx_bytes;
	u64 emac_rx_errors;
	u64 emac_tx_errors;
	u64 emac_rx_drops;
	u64 emac_tx_drops;
	u64 emac_rx_pause_frames;
	u64 emac_tx_pause_frames;
};

struct ioss_channel_stats {
	u64 overflow_error;
	u64 underflow_error;
	u64 desc_unavail;
};

/**
 * struct ioss_channel_status - Status information of a channel
 * @enabled: Channel is enabled and operational
 * @ring_size: Descriptor ring size is number of elements
 * @interrupt_modc: Interrupt moderation counter config
 * @interrupt_modt: Interrumt moderation timer in nanoseconds
 * @head_ptr: Value of ring head pointer
 * @tail_ptr: Value of ring tail pointer
 */
struct ioss_channel_status {
	bool enabled;
	u64 ring_size;
	u64 interrupt_modc;
	u64 interrupt_modt;
	u64 head_ptr;
	u64 tail_ptr;
};

#define ioss_ch_dev(ch) ioss_iface_dev(ch->iface)

#define ioss_for_each_channel(ch, iface) \
	list_for_each_entry(ch, &iface->channels, node)

static inline char *ioss_ch_dir_s(struct ioss_channel *ch)
{
	return (ch->direction == IOSS_CH_DIR_RX) ? "RX" : "TX";
}

static inline int ioss_channel_add_mem(
			struct ioss_channel *ch,
			struct list_head *list,
			void *addr, dma_addr_t daddr, size_t size)
{
	struct device *real_dev = ioss_idev_to_real(ioss_ch_dev(ch));
	struct ioss_mem *imem = kzalloc(sizeof(*imem), GFP_KERNEL);

	if (!imem)
		return -ENOMEM;

	imem->addr = addr;
	imem->daddr = daddr;
	imem->size = size;

	if (dma_get_sgtable(real_dev, &imem->sgt, addr, daddr, size)) {
		kfree(imem);
		return -EFAULT;
	}

	list_add_tail(&imem->node, list);

	return 0;
}

static inline void ioss_channel_del_mem(
			struct ioss_channel *ch,
			struct list_head *list, void *addr)
{
	struct ioss_mem *imem, *tmp;

	list_for_each_entry_safe(imem, tmp, list, node) {
		if (imem->addr == addr) {
			list_del(&imem->node);
			sg_free_table(&imem->sgt);
			kfree(imem);
		}
	}
}

/**
 * ioss_channel_map_event() - DMA map IPA door bell to the peripheral.
 * @ch: Channel that contains event DB physical address to map
 *
 * Helper routine to map IPA door bell physical address to the peripheral IOMMU
 * context bank.
 *
 * Return: 0 on success, non-zero on failure. On success, DMA mapped address of
 *         door bell is filled in ch->event.daddr.
 */
static inline int ioss_channel_map_event(struct ioss_channel *ch)
{
	struct ioss_device *idev = ioss_ch_dev(ch);
	struct device *dev = ioss_idev_to_real(idev);

	if (ch->event.daddr)
		return -EEXIST;

	ch->event.daddr = dma_map_resource(dev,
				ch->event.paddr, sizeof(ch->event.data),
				DMA_FROM_DEVICE, 0);

	if (dma_mapping_error(dev, ch->event.daddr)) {
		ioss_dev_err(idev, "Failed to DMA map %s-%d event to %pap",
				ioss_ch_dir_s(ch), ch->id, &ch->event.paddr);
		ch->event.daddr = 0;
		return -EFAULT;
	}

	ioss_dev_cfg(idev, "Mapped %s-%d event %pad -> %pap",
			ioss_ch_dir_s(ch), ch->id,
			&ch->event.daddr, &ch->event.paddr);

	return 0;
}

/**
 * ioss_channel_unmap_event() - Unmap IPA DB from peripheral IOMMU CB.
 * @ch: Channel that contains IPA DB (event DB) information
 *
 * Unmaps IPA DB from peripheral IOMMU CB that was previously mapped using
 * ioss_channel_map_event().
 */
static inline void ioss_channel_unmap_event(struct ioss_channel *ch)
{
	struct ioss_device *idev = ioss_ch_dev(ch);
	struct device *dev = ioss_idev_to_real(idev);

	if (!ch->event.daddr)
		return;

	dma_unmap_resource(dev,
			ch->event.daddr, sizeof(ch->event.data),
			DMA_FROM_DEVICE, 0);

	ioss_dev_cfg(idev, "Unmapped %s-%d event %pad -> %pap",
			ioss_ch_dir_s(ch), ch->id,
			&ch->event.daddr, &ch->event.paddr);

	ch->event.daddr = 0;
}

#define ioss_channel_add_desc_mem(ch, addr, daddr, size) \
	ioss_channel_add_mem(ch, &ch->desc_mem, addr, daddr, size)

#define ioss_channel_del_desc_mem(ch, addr) \
	ioss_channel_del_mem(ch, &ch->desc_mem, addr)

#define ioss_channel_add_buff_mem(ch, addr, daddr, size) \
	ioss_channel_add_mem(ch, &ch->buff_mem, addr, daddr, size)

#define ioss_channel_del_buff_mem(ch, addr) \
	ioss_channel_del_mem(ch, &ch->buff_mem, addr)

struct ioss_driver {
	struct list_head node;

	/* Fields to be filled by network driver */
	const char *name;
	bool (*match)(struct device *dev);

	struct ioss_driver_ops *ops;
	enum ioss_filter_types filter_types;

	/* IOSS managed */
	struct device_driver drv;

	struct mutex pm_lock;
	refcount_t pm_refcnt;
	struct dev_pm_ops pm_ops;
	const struct dev_pm_ops *pm_ops_real;
};

#define to_ioss_driver(driver) \
	container_of(driver, struct ioss_driver, drv)

#define ioss_dev_to_drv(idev) \
	to_ioss_driver(idev->dev.driver)

struct ioss_driver_ops {
	int (*open_device)(struct ioss_device *idev);
	int (*close_device)(struct ioss_device *idev);

	int (*request_channel)(struct ioss_channel *ch);
	int (*release_channel)(struct ioss_channel *ch);

	int (*enable_channel)(struct ioss_channel *ch);
	int (*disable_channel)(struct ioss_channel *ch);

	int (*request_event)(struct ioss_channel *ch);
	int (*release_event)(struct ioss_channel *ch);

	int (*enable_event)(struct ioss_channel *ch);
	int (*disable_event)(struct ioss_channel *ch);

	int (*save_regs)(struct ioss_device *idev,
			void **regs, size_t *size);

	int (*get_device_statistics)(struct ioss_device *idev,
				     struct ioss_device_stats *stats);
	int (*get_channel_statistics)(struct ioss_channel *ch,
				      struct ioss_channel_stats *stats);
	int (*get_channel_status)(struct ioss_channel *ch,
				  struct ioss_channel_status *status);
};

#define ioss_dev_op(idev, op, args...) \
	({ \
		struct ioss_driver *idrv = to_ioss_driver(idev->dev.driver); \
		(idrv && idrv->ops->op) ? idrv->ops->op(args) : -ENOENT; \
	}) \

int __ioss_pci_register_driver(struct ioss_driver *drv, struct module *owner);

#define ioss_pci_register_driver(driver) \
	__ioss_pci_register_driver(driver, THIS_MODULE)

void ioss_pci_unregister_driver(struct ioss_driver *drv);

#endif /* _IOSS_H_ */
