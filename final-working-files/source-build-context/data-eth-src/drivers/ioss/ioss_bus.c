/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */

#include "ioss_i.h"

/* Wake lock duration to allow the device to settle after a resume */
#define IOSS_RESUME_SETTLE_MS 5000

#define MAX_IOSS_DEVICES 10
#define MAX_IOSS_DRIVERS 10

struct ioss_device *ioss_devices[MAX_IOSS_DEVICES];
struct ioss_driver *ioss_drivers[MAX_IOSS_DRIVERS];

static int ioss_panic_notifier(struct notifier_block *nb,
		unsigned long event, void *ptr)
{
	int rc;
	struct ioss_device *idev =
			container_of(nb, struct ioss_device, panic_nb);

	ioss_dev_log(idev, "Panic notifier called for device");

	rc = ioss_dev_op(idev, save_regs, idev, NULL, NULL);
	if (rc)
		ioss_dev_err(idev, "save_regs() failed with error %d", rc);
	else
		ioss_dev_cfg(idev, "Panic notifier completed for device");

	return NOTIFY_DONE;
}

static int ioss_register_panic_notifier(struct ioss_device *idev)
{
	idev->panic_nb.notifier_call = ioss_panic_notifier;

	return atomic_notifier_chain_register(
			&panic_notifier_list, &idev->panic_nb);
}

static void ioss_unregister_panic_notifier(struct ioss_device *idev)
{
	atomic_notifier_chain_unregister(&panic_notifier_list, &idev->panic_nb);
	idev->panic_nb.notifier_call = NULL;
}

static int ioss_bus_match(struct device *dev, struct device_driver *drv)
{
	struct device *real_dev = dev->parent;
	struct ioss_driver *idrv = to_ioss_driver(drv);
	struct ioss_device *idev = to_ioss_device(dev);

	if (dev->type != &ioss_idev_type)
		return false;

	ioss_dev_dbg(idev, "Matching against %s", idrv->name);

	/* If a match function is provided by the IOSS driver, use that for
	 * matching the device driver with the IOSS driver. Otherwise use a
	 * simple name matching against the IOSS driver name.
	 */
	return idrv->match ?
			idrv->match(real_dev) :
			!strcmp(idrv->name, real_dev->driver->name);
}

static ssize_t show_suspend_ipa_offload(struct device *dev,
		struct device_attribute *attr, char *user_buf)
{
	struct net_device *net_dev = to_net_dev(dev);
	struct ioss_interface *iface = ioss_netdev_to_iface(net_dev);
	struct ioss_device *idev = ioss_iface_dev(iface);

	return snprintf(user_buf, PAGE_SIZE, "%d\n", idev->dev.offline);
}

static ssize_t store_suspend_ipa_offload(struct device *dev,
		struct device_attribute *attr, const char *user_buf, size_t size)
{
	struct net_device *net_dev = to_net_dev(dev);
	struct ioss_interface *iface = ioss_netdev_to_iface(net_dev);
	struct ioss_device *idev = ioss_iface_dev(iface);
	bool input;

	if (kstrtobool(user_buf, &input) < 0)
		return -EINVAL;

	idev->dev.offline = input;

	ioss_iface_queue_refresh(iface, true);

	ioss_dev_log(idev, "Device Offline set to %d", idev->dev.offline);

	return size;
}

static DEVICE_ATTR(suspend_ipa_offload, S_IWUSR | S_IRUGO,
		show_suspend_ipa_offload, store_suspend_ipa_offload);

static int ioss_bus_probe(struct device *dev)
{
	int rc;
	struct ioss_device *idev = to_ioss_device(dev);

	ioss_dev_log(idev, "Initializing device for offload");

	device_init_wakeup(dev, true);

	rc = ioss_net_link_device(idev);
	if (rc) {
		ioss_dev_err(idev, "Failed to link to net device");
		return rc;
	}

	ioss_pci_hijack_pm_ops(idev);

	rc = ioss_register_panic_notifier(idev);
	if (rc) {
		ioss_dev_err(idev, "Failed to register panic notifier");
		goto err_notifier;
	}

	rc = ioss_dev_op(idev, open_device, idev);
	if (rc) {
		ioss_dev_err(idev, "Failed to open device");
		goto err_open;
	}

	rc = ioss_net_watch_device(idev);
	if (rc) {
		ioss_dev_err(idev, "Failed to watch net device");
		goto err_watch;
	}

	rc = sysfs_create_file(&idev->net_dev->dev.kobj,
				&dev_attr_suspend_ipa_offload.attr);
	if (rc) {
		ioss_dev_err(idev, "unable to create suspend_ipa_offload node");
		goto err_sysfs;
	}

	return 0;

err_sysfs:
	ioss_net_unwatch_device(idev);
err_watch:
	ioss_dev_op(idev, close_device, idev);
err_open:
	ioss_unregister_panic_notifier(idev);
err_notifier:
	ioss_pci_restore_pm_ops(idev);

	return rc;
}

static int ioss_bus_remove(struct device *dev)
{
	int rc;
	struct ioss_device *idev = to_ioss_device(dev);

	ioss_dev_log(idev, "De-initializing device");

	sysfs_remove_file(&idev->net_dev->dev.kobj,
			&dev_attr_suspend_ipa_offload.attr);

	rc = ioss_net_unwatch_device(idev);
	if (rc) {
		ioss_dev_err(idev, "Failed to unwatch device");
		return rc;
	}

	rc = ioss_dev_op(idev, close_device, idev);
	if (rc) {
		ioss_dev_err(idev, "Failed to close device");
		return rc;
	}

	ioss_unregister_panic_notifier(idev);
	ioss_pci_restore_pm_ops(idev);
	device_init_wakeup(dev, false);

	return 0;
}

static int __ioss_bus_suspend_idev(struct device *dev, pm_message_t state)
{
	struct ioss_device *idev = to_ioss_device(dev);

	ioss_dev_log(idev, "Suspending device");

	return 0;
}

static int __ioss_bus_resume_idev(struct device *dev)
{
	bool need_refresh = false;
	bool was_suspended = true;
	struct ioss_device *idev = to_ioss_device(dev);
	struct ioss_interface *iface = &idev->interface;

	ioss_dev_log(idev, "Resuming device");

	if (dev->offline) {
		dev->offline = 0;
		need_refresh = true;
	}

	if (iface->state == IOSS_IF_ST_ONLINE)
		was_suspended = false;

	if (need_refresh)
		ioss_iface_queue_refresh(iface, false);

	if (was_suspended)
		pm_wakeup_dev_event(dev, IOSS_RESUME_SETTLE_MS, false);

	return 0;
}

static int __ioss_bus_online_idev(struct device *dev)
{
	struct ioss_device *idev = to_ioss_device(dev);
	struct ioss_interface *iface = &idev->interface;

	dev->offline = 0;

	ioss_iface_queue_refresh(iface, true);

	ioss_dev_log(idev, "Online device");

	return 0;
}

static int __ioss_bus_offline_idev(struct device *dev)
{
	struct ioss_device *idev = to_ioss_device(dev);
	struct ioss_interface *iface = &idev->interface;

	dev->offline = 1;

	ioss_iface_queue_refresh(iface, true);

	ioss_dev_log(idev, "Offline device");

	return 0;
}

static int ioss_bus_suspend(struct device *dev, pm_message_t state)
{
	if (dev->type == &ioss_idev_type)
		return __ioss_bus_suspend_idev(dev, state);

	return 0;
}

static int ioss_bus_resume(struct device *dev)
{
	if (dev->type == &ioss_idev_type)
		return __ioss_bus_resume_idev(dev);

	return 0;
}

static int ioss_bus_online(struct device *dev)
{
	if (dev->type == &ioss_idev_type)
		return __ioss_bus_online_idev(dev);

	return 0;
}

static int ioss_bus_offline(struct device *dev)
{
	if (dev->type == &ioss_idev_type)
		return __ioss_bus_offline_idev(dev);

	return 0;
}

struct device_type ioss_idev_type = {
	.name = "ioss_device",
};

struct device_type ioss_iface_type = {
	.name = "ioss_interface",
};

struct bus_type ioss_bus = {
	.name = "ioss",
	.match = ioss_bus_match,
	.probe = ioss_bus_probe,
	.remove = ioss_bus_remove,
	.suspend = ioss_bus_suspend,
	.resume = ioss_bus_resume,
	.online = ioss_bus_online,
	.offline = ioss_bus_offline,
};

int ioss_bus_register_driver(struct ioss_driver *idrv)
{
	int i, rc;
	struct ioss_driver **idrv_list_entry = NULL;

	for (i = 0; i < ARRAY_SIZE(ioss_drivers); i++) {
		if (!ioss_drivers[i]) {
			idrv_list_entry = &ioss_drivers[i];
			break;
		}
	}

	if (!idrv_list_entry) {
		ioss_log_cfg(NULL,
			"No space to add %s to ioss_drivers[]", idrv->name);
		return -ENOSPC;
	}

	/* Add to driver list before registering the driver with kernel so that
	 * this is accessible from crashdumps.
	 */
	*idrv_list_entry = idrv;

	idrv->drv.name = idrv->name;
	idrv->drv.bus = &ioss_bus;

	mutex_init(&idrv->pm_lock);
	refcount_set(&idrv->pm_refcnt, 0);

	/* Init drv */
	/* Register driver */

	rc = driver_register(&idrv->drv);
	if (rc) {
		ioss_log_err(NULL, "Failed to register driver %s", idrv->name);
		*idrv_list_entry = NULL;
		return rc;
	}

	return 0;
}

void ioss_bus_unregister_driver(struct ioss_driver *idrv)
{
	int i;

	driver_unregister(&idrv->drv);

	mutex_destroy(&idrv->pm_lock);

	for (i = 0; i < ARRAY_SIZE(ioss_drivers); i++) {
		if (ioss_drivers[i] == idrv) {
			ioss_drivers[i] = NULL;
			break;
		}
	}
}

struct ioss_device *ioss_bus_alloc_idev(struct ioss *ioss, struct device *dev)
{
	int i;
	struct ioss_device *idev;
	struct ioss_device **idev_list_entry = NULL;

	for (i = 0; i < ARRAY_SIZE(ioss_devices); i++) {
		if (!ioss_devices[i]) {
			idev_list_entry = &ioss_devices[i];
			break;
		}
	}

	if (!idev_list_entry) {
		ioss_log_err(NULL,
			"No space to add %s to ioss_devices[]", dev_name(dev));
		return NULL;
	}

	if (!dev->driver || !dev->driver->pm) {
		ioss_log_err(NULL,
			"Driver %s does not support PM callbacks", dev_name(dev));
		return NULL;
	}

	idev = kzalloc(sizeof(*idev), GFP_KERNEL);
	if (!idev) {
		ioss_log_err(NULL, "Failed to alloc ioss device");
		return NULL;
	}

	*idev_list_entry = idev;

	idev->root = ioss;

	INIT_LIST_HEAD(&idev->interface.channels);

	idev->dev.parent = dev;
	idev->dev.bus = &ioss_bus;
	idev->dev.type = &ioss_idev_type;

	return idev;
}

void ioss_bus_free_idev(struct ioss_device *idev)
{
	int i;
	struct ioss_channel *ch, *tmp_ch;
	struct ioss_interface *iface = &idev->interface;

	/* Free channels */
	list_for_each_entry_safe(ch, tmp_ch, &iface->channels, node) {
		list_del(&ch->node);
		kzfree(ch->ioss_priv);
		kzfree(ch);
	}

	kzfree(iface->ioss_priv);

	for (i = 0; i < ARRAY_SIZE(ioss_devices); i++) {
		if (ioss_devices[i] == idev) {
			ioss_devices[i] = NULL;
			break;
		}
	}

	kzfree(idev);
}

int ioss_bus_register_idev(struct ioss_device *idev)
{
	if (ioss_of_parse(idev)) {
		ioss_dev_err(idev, "Failed to parse devicetree");
		return -EINVAL;
	}

	return device_register(&idev->dev);
}

void ioss_bus_unregister_idev(struct ioss_device *idev)
{
	device_unregister(&idev->dev);
}

int ioss_bus_register_iface(struct ioss_interface *iface,
		struct net_device *net_dev)
{
	struct ioss_device *idev = ioss_iface_dev(iface);

	dev_hold(net_dev);

	iface->dev.parent = &net_dev->dev;
	iface->dev.bus = &ioss_bus;
	iface->dev.type = &ioss_iface_type;

	dev_set_name(&iface->dev, "%s-%s",
			dev_name(&idev->dev), net_dev->name);

	return device_register(&iface->dev);
}

void ioss_bus_unregister_iface(struct ioss_interface *iface)
{
	struct net_device *net_dev = ioss_iface_to_netdev(iface);

	device_unregister(&iface->dev);
	iface->dev.parent = NULL;
	dev_put(net_dev);
}
