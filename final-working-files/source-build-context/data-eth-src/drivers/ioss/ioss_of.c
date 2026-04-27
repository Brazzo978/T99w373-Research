/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */

#include <linux/of.h>
#include <linux/device.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/string.h>

#include "ioss_i.h"

typedef int (*of_parser_t)(struct ioss_device *idev, struct device_node *np);

#define IOSS_R8125_PCI_DEVICE_ID 0x8125
#define IOSS_R8125_INSTANCE_ID 0
#define IOSS_R8125_RX_RING_SIZE 1023
#define IOSS_R8125_TX_RING_SIZE 1024
#define IOSS_R8125_BUFFER_SIZE 2048
#define IOSS_R8125_MOD_COUNT_MIN 32
#define IOSS_R8125_MOD_COUNT_MAX 64
#define IOSS_R8125_MOD_USECS_MIN 30
#define IOSS_R8125_MOD_USECS_MAX 60

static void ioss_of_free_channels(struct ioss_interface *iface)
{
	struct ioss_channel *ch;
	struct ioss_channel *tmp_ch;

	list_for_each_entry_safe(ch, tmp_ch, &iface->channels, node) {
		list_del(&ch->node);
		kzfree(ch->ioss_priv);
		kzfree(ch);
	}
}

static int ioss_of_add_fallback_channel(struct ioss_device *idev,
		enum ioss_channel_dir direction)
{
	struct ioss_channel *ch;
	struct ioss_interface *iface = &idev->interface;

	ch = kzalloc(sizeof(*ch), GFP_KERNEL);
	if (!ch)
		return -ENOMEM;

	ch->ioss_priv = kzalloc(sizeof(struct ioss_ch_priv), GFP_KERNEL);
	if (!ch->ioss_priv) {
		kfree(ch);
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&ch->node);
	INIT_LIST_HEAD(&ch->desc_mem);
	INIT_LIST_HEAD(&ch->buff_mem);

	ch->iface = iface;
	ch->direction = direction;
	ch->default_config.ring_size = (direction == IOSS_CH_DIR_RX) ?
			IOSS_R8125_RX_RING_SIZE : IOSS_R8125_TX_RING_SIZE;
	ch->default_config.buff_size = IOSS_R8125_BUFFER_SIZE;
	ch->default_config.desc_alctr = &ioss_default_alctr;
	ch->default_config.buff_alctr = &ioss_default_alctr;
	ch->event.mod_count_min = IOSS_R8125_MOD_COUNT_MIN;
	ch->event.mod_count_max = IOSS_R8125_MOD_COUNT_MAX;
	ch->event.mod_usecs_min = IOSS_R8125_MOD_USECS_MIN;
	ch->event.mod_usecs_max = IOSS_R8125_MOD_USECS_MAX;

	if (direction == IOSS_CH_DIR_RX)
		ch->filter_types = IOSS_RXF_F_IP;

	list_add_tail(&ch->node, &iface->channels);

	return 0;
}

static int ioss_of_parse_r8125_fallback(struct ioss_device *idev)
{
	int rc;
	struct ioss_interface *iface = &idev->interface;

	iface->ioss_priv = kzalloc(sizeof(struct ioss_iface_priv), GFP_KERNEL);
	if (!iface->ioss_priv)
		return -ENOMEM;

	iface->instance_id = IOSS_R8125_INSTANCE_ID;

	rc = ioss_of_add_fallback_channel(idev, IOSS_CH_DIR_RX);
	if (rc)
		goto err;

	rc = ioss_of_add_fallback_channel(idev, IOSS_CH_DIR_TX);
	if (rc)
		goto err;

	ioss_dev_cfg(idev,
		"Using built-in RTL8125 IOSS fallback config (DT bindings missing)");

	return 0;

err:
	ioss_of_free_channels(iface);
	kfree(iface->ioss_priv);
	iface->ioss_priv = NULL;
	return rc;
}

static bool ioss_is_r8125_pci_dev(struct ioss_device *idev)
{
	struct device *real_dev = ioss_idev_to_real(idev);
	struct pci_dev *pdev;

	if (!real_dev || real_dev->bus != &pci_bus_type)
		return false;

	pdev = to_pci_dev(real_dev);

	return pdev->vendor == PCI_VENDOR_ID_REALTEK &&
		pdev->device == IOSS_R8125_PCI_DEVICE_ID;
}

static int ioss_of_parse_channel(struct ioss_device *idev,
		struct device_node *np)
{
	const char *key;
	struct ioss_channel *ch = NULL;
	struct ioss_interface *iface = &idev->interface;

	ch = kzalloc(sizeof(*ch), GFP_KERNEL);
	if (!ch)
		return -ENOMEM;

	ch->ioss_priv = kzalloc(sizeof(struct ioss_ch_priv), GFP_KERNEL);
	if (!ch->ioss_priv) {
		kfree(ch);
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&ch->node);
	INIT_LIST_HEAD(&ch->desc_mem);
	INIT_LIST_HEAD(&ch->buff_mem);

	ch->iface = iface;

	if (!np) {
		ioss_dev_err(idev, "Channel DT node is NULL");
		goto err;
	}

	if (!!of_find_property(np, "qcom,dir-rx", NULL))
		ch->direction = IOSS_CH_DIR_RX;
	else if (!!of_find_property(np, "qcom,dir-tx", NULL))
		ch->direction = IOSS_CH_DIR_TX;
	else
		goto err;

	key = "qcom,ring-size";
	if (of_property_read_u32(np, key, &ch->default_config.ring_size)) {
		ioss_dev_err(idev, "Failed to parse key %s", key);
		goto err;
	}

	key = "qcom,buff-size";
	if (of_property_read_u32(np, key, &ch->default_config.buff_size)) {
		ioss_dev_err(idev, "Failed to parse key %s", key);
		goto err;
	}

	key = "qcom,mod-count-min";
	if (of_property_read_u32(np, key, &ch->event.mod_count_min)) {
		ioss_dev_err(idev, "Failed to parse key %s", key);
		goto err;
	}

	key = "qcom,mod-count-max";
	if (of_property_read_u32(np, key, &ch->event.mod_count_max)) {
		ioss_dev_err(idev, "Failed to parse key %s", key);
		goto err;
	}

	key = "qcom,mod-usecs-min";
	if (of_property_read_u32(np, key, &ch->event.mod_usecs_min)) {
		ioss_dev_err(idev, "Failed to parse key %s", key);
		goto err;
	}

	key = "qcom,mod-usecs-max";
	if (of_property_read_u32(np, key, &ch->event.mod_usecs_max)) {
		ioss_dev_err(idev, "Failed to parse key %s", key);
		goto err;
	}

	if (!!of_find_property(np, "qcom,rx-filter-be", NULL))
		ch->filter_types |= IOSS_RXF_F_BE;

	if (!!of_find_property(np, "qcom,rx-filter-ip", NULL))
		ch->filter_types |= IOSS_RXF_F_IP;

	ch->default_config.desc_alctr = &ioss_default_alctr;
	ch->default_config.buff_alctr = &ioss_default_alctr;

	list_add_tail(&ch->node, &iface->channels);

	return 0;

err:
	kfree(ch->ioss_priv);
	kfree(ch);

	return -EINVAL;
}

static struct device_node *ioss_of_find_fallback_channel(struct device_node *np,
							 int index)
{
	struct device_node *child;
	const char *name = NULL;
	int seen = 0;

	if (!np)
		return NULL;

	if (index == 0)
		name = "channel-rx";
	else if (index == 1)
		name = "channel-tx";

	if (name) {
		for_each_child_of_node(np, child) {
			if (of_node_name_eq(child, name))
				return of_node_get(child);
		}
	}

	for_each_child_of_node(np, child) {
		if (strncmp(child->name, "channel-", 8))
			continue;
		if (seen == index)
			return of_node_get(child);
		seen++;
	}

	return NULL;
}

static int ioss_of_parse_v2(struct ioss_device *idev, struct device_node *np)
{
	int i, count;
	const char *key;
	struct ioss_interface *iface = &idev->interface;

	iface->ioss_priv = kzalloc(sizeof(struct ioss_iface_priv), GFP_KERNEL);
	if (!iface->ioss_priv)
		return -ENOMEM;

	key = "qcom,ioss_instance";
	if (of_property_read_u32(np, key, &iface->instance_id)) {
		ioss_dev_log(idev, "Failed to parse key %s", key);
		goto err;
	}

	/* Parse channels */
	key = "qcom,ioss_channels";
	count = of_count_phandle_with_args(np, "qcom,ioss_channels", NULL);
	if (count < 0) {
		ioss_dev_err(idev, "Failed to parse key %s", key);
		goto err;
	}

	/* Get channels */
	for (i = 0; i < count; i++) {
		struct device_node *n = of_parse_phandle(np,
						"qcom,ioss_channels", i);
		struct device_node *fallback = NULL;

		/*
		 * Some runtime DT mutations may leave stale/colliding phandles.
		 * If the resolved node is not a channel node, force fallback to
		 * child lookup by name/order.
		 */
		if (n && strncmp(n->name, "channel-", 8)) {
			ioss_dev_cfg(idev,
				"channel[%d] phandle resolved to non-channel node '%s', trying fallback",
				i, n->name ? n->name : "<?>");
			of_node_put(n);
			n = NULL;
		}

		if (!n) {
			fallback = ioss_of_find_fallback_channel(np, i);
			n = fallback;
			if (fallback)
				ioss_dev_cfg(idev,
					"channel[%d] phandle missing, using child node fallback",
					i);
		}
		if (!n) {
			ioss_dev_err(idev,
				"Failed to parse channel[%d] phandle and fallback", i);
			goto err;
		}

		if (ioss_of_parse_channel(idev, n)) {
			/*
			 * If phandle-target parsing fails, retry once using child
			 * fallback node (channel-rx/channel-tx) before failing.
			 */
			if (!fallback) {
				fallback = ioss_of_find_fallback_channel(np, i);
				if (fallback && fallback != n) {
					ioss_dev_cfg(idev,
						"channel[%d] parse failed, retrying with child fallback",
						i);
					of_node_put(n);
					n = fallback;
					fallback = NULL;
					if (!ioss_of_parse_channel(idev, n)) {
						of_node_put(n);
						continue;
					}
				}
			}
			of_node_put(n);
			ioss_dev_err(idev, "Failed to parse channel[%d]", i);
			goto err;
		}
		of_node_put(n);
	}

	if (!!of_find_property(np, "qcom,ioss-wol-phy", NULL))
		idev->wol.wolopts |= WAKE_PHY;

	if (!!of_find_property(np, "qcom,ioss-wol-magic", NULL))
		idev->wol.wolopts |= WAKE_MAGIC;

	return 0;

err:
	ioss_of_free_channels(iface);
	kfree(iface->ioss_priv);
	iface->ioss_priv = NULL;

	return -EINVAL;
}

static const struct of_device_id ioss_dev_match_table[] = {
	{ .compatible = "qcom,ioss-v2-device", .data = ioss_of_parse_v2, },
	{},
};

static int __ioss_of_parse(struct ioss_device *idev, struct device_node *np)
{
	const struct of_device_id *match =
		of_match_node(ioss_dev_match_table, np);

	if (match) {
		of_parser_t parser = match->data;

		return parser(idev, np);
	}

	return -EINVAL;
}

int ioss_of_parse(struct ioss_device *idev)
{
	const char *key;
	int rc;
	struct device_node *of_root;
	struct platform_device *pdev;
	struct device_node *np = dev_of_node(idev->dev.parent);

	if (!np) {
		if (ioss_is_r8125_pci_dev(idev))
			return ioss_of_parse_r8125_fallback(idev);

		return -EINVAL;
	}

	/* Get ioss root */
	key = "qcom,ioss";
	of_root = of_parse_phandle(np, key, 0);
	if (!of_root) {
		if (ioss_is_r8125_pci_dev(idev))
			return ioss_of_parse_r8125_fallback(idev);

		ioss_dev_dbg(idev, "DT prop %s not found, skipping.", key);
		return -ENOENT;
	}

	pdev = ioss_find_dev_from_of_node(of_root);
	if (!pdev) {
		ioss_dev_err(idev, "Failed to find ioss root node");
		return -EFAULT;
	}

	if (idev->root->pdev != pdev) {
		ioss_dev_dbg(idev, "Device belongs to a different root");
		return -ENOENT;
	}

	rc = __ioss_of_parse(idev, np);
	if (!rc)
		return 0;

	if (ioss_is_r8125_pci_dev(idev))
		return ioss_of_parse_r8125_fallback(idev);

	return rc;
}
