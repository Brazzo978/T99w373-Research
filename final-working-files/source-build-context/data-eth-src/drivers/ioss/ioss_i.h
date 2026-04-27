/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */

#ifndef _IOSS_I_H_
#define _IOSS_I_H_

#include <linux/stat.h>
#include <linux/module.h>

#include <linux/if_vlan.h>
#include <linux/platform_device.h>

#include <linux/ipa_eth.h>

#include <linux/msm/ioss.h>

enum ioss_statuses {
	IOSS_ST_ERROR,
	IOSS_ST_PROBED,
	IOSS_ST_IPA_RDY,
};

struct ioss_priv_data {
	struct ioss *ioss;
	struct ipa_eth_ready ipa_ready;
};

struct ioss_ch_priv {
	struct ipa_eth_client_pipe_info ipa_pi;
};

#if IPA_ETH_API_VER < 2
union ioss_ipa_eth_hdr {
	struct ethhdr l2;
	struct vlan_ethhdr vlan;
};
#endif

struct ioss_iface_priv {
	struct ipa_eth_client ipa_ec;
	struct ipa_eth_intf_info ipa_ii;

#if IPA_ETH_API_VER < 2
	union ioss_ipa_eth_hdr ipa_hdr_v4;
	union ioss_ipa_eth_hdr ipa_hdr_v6;
#endif
};

extern struct ioss_mem_allocator ioss_default_alctr;
extern struct ioss_mem_allocator ioss_llcc_alctr;

extern unsigned long ioss_ver;
extern unsigned long ioss_api_ver;

int ioss_pci_start(struct ioss *ioss);
void ioss_pci_stop(struct ioss *ioss);
void ioss_pci_hijack_pm_ops(struct ioss_device *idev);
void ioss_pci_restore_pm_ops(struct ioss_device *idev);
int ioss_pci_enable_pc(struct ioss_device *idev);
int ioss_pci_disable_pc(struct ioss_device *idev);


int ioss_of_parse(struct ioss_device *idev);

struct platform_device *ioss_find_dev_from_of_node(
		struct device_node *np);

int ioss_ipa_register(struct ioss_interface *iface);
int ioss_ipa_unregister(struct ioss_interface *iface);

enum ipa_eth_client_type ioss_ipa_hal_get_ctype(struct ioss_device *idev);
int ioss_ipa_hal_fill_si(struct ioss_channel *ch);

int ioss_bus_register_driver(struct ioss_driver *idrv);
void ioss_bus_unregister_driver(struct ioss_driver *idrv);

struct ioss_device *ioss_bus_alloc_idev(struct ioss *ioss,
			struct device *dev);
void ioss_bus_free_idev(struct ioss_device *idev);
int ioss_bus_register_idev(struct ioss_device *idev);
void ioss_bus_unregister_idev(struct ioss_device *idev);

int ioss_bus_register_iface(struct ioss_interface *iface,
		struct net_device *net_dev);
void ioss_bus_unregister_iface(struct ioss_interface *iface);

int ioss_net_watch_device(struct ioss_device *idev);
int ioss_net_unwatch_device(struct ioss_device *idev);
int ioss_net_link_device(struct ioss_device *idev);

int ioss_log_init(void);
void ioss_log_deinit(void);

int ioss_list_iter_action(struct list_head *head,
	int (*action)(struct list_head *node),
	void (*revert)(struct list_head *node));

const char *ioss_if_state_name(enum ioss_interface_state state);
const char *ioss_ch_dir_name(enum ioss_channel_dir dir);

#define if_st_s(iface) ioss_if_state_name(iface->state)

void ioss_iface_queue_refresh(struct ioss_interface *iface, bool flush);

int ioss_debugfs_init(void);
void ioss_debugfs_exit(void);
int ioss_debugfs_add_idev(struct ioss_device *idev);
void ioss_debugfs_remove_idev(struct ioss_device *idev);
int ioss_debugfs_add_channel(struct ioss_channel *ch);
void ioss_debugfs_remove_channel(struct ioss_channel *ch);

#endif /* _IOSS_I_H_ */
