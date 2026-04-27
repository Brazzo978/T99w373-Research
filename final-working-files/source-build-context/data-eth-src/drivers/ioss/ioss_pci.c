/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */

#include <linux/pci.h>
#include <linux/device.h>
#include <linux/platform_device.h>

#include <linux/msm_pcie.h>

#include "ioss_i.h"

static int ioss_pci_add_device(struct pci_dev *pdev, struct ioss *ioss)
{
	int rc;
	struct ioss_device *idev;

	ioss_log_cfg(NULL, "Adding device %s", dev_name(&pdev->dev));

	idev = ioss_bus_alloc_idev(ioss, &pdev->dev);
	if (!idev)
		return -ENOMEM;

	dev_set_name(&idev->dev, "%s-ioss", dev_name(idev->dev.parent));

	rc = ioss_bus_register_idev(idev);
	if (rc) {
		ioss_dev_err(idev, "Failed to register device with ioss bus");
		ioss_bus_free_idev(idev);
		return rc;
	}

	return 0;
}

static int ioss_pci_remove_device(struct pci_dev *pdev, struct ioss *ioss)
{
	struct ioss_device *idev = ioss_real_to_idev(&pdev->dev);

	if (!idev)
		return -ENODEV;

	ioss_dev_log(idev, "Removing device %s", dev_name(idev->dev.parent));

	ioss_bus_unregister_idev(idev);
	ioss_bus_free_idev(idev);

	return 0;
}

static int pci_bus_notification(struct notifier_block *nb,
		unsigned long action, void *data)
{
	struct pci_dev *pdev = to_pci_dev(data);
	struct ioss *ioss = container_of(nb, typeof(*ioss), pci_bus_nb);
	struct device *dev = &pdev->dev;

	ioss_log_dbg(NULL, "PCIe bus notif: %lu device: %s, driver: %s",
			action, dev_name(dev), dev_driver_string(dev));

	switch (action) {
	case BUS_NOTIFY_BOUND_DRIVER:
		ioss_pci_add_device(pdev, ioss);
		break;
	case BUS_NOTIFY_UNBIND_DRIVER:
		ioss_pci_remove_device(pdev, ioss);
		break;
	}

	return NOTIFY_OK;
}

static int __pcie_walk_add_device(struct device *dev, void *data)
{
	struct ioss *ioss = data;

	ioss_log_dbg(NULL, "PCIe walk add device: %s, driver: %s",
			dev_name(dev), dev_driver_string(dev));

	/* Call BUS_NOTIFY_BOUND_DRIVER if the device is already bound to a
	 * driver.
	 */
	if (dev->driver)
		pci_bus_notification(&ioss->pci_bus_nb,
				BUS_NOTIFY_BOUND_DRIVER, dev);

	return 0;
}

int ioss_pci_start(struct ioss *ioss)
{
	int rc;

	ioss_log_cfg(NULL, "Starting PCI");

	ioss->pci_bus_nb.notifier_call = pci_bus_notification;
	rc = bus_register_notifier(&pci_bus_type, &ioss->pci_bus_nb);
	if (rc) {
		ioss_log_err(NULL, "Failed to register PCI bus notifier");
		return rc;
	}

	/* Manually scan all existing PCI devices */
	bus_for_each_dev(&pci_bus_type, NULL, ioss, __pcie_walk_add_device);

	return 0;
}

static int __pcie_walk_del_device(struct device *dev, void *data)
{
	struct ioss *ioss = data;

	ioss_log_dbg(NULL, "PCIe walk del device: %s, driver: %s",
			dev_name(dev), dev_driver_string(dev));

	if (dev->driver)
		pci_bus_notification(&ioss->pci_bus_nb,
				BUS_NOTIFY_UNBIND_DRIVER, dev);

	return 0;
}

void ioss_pci_stop(struct ioss *ioss)
{
	ioss_log_cfg(NULL, "Stopping PCI");

	bus_unregister_notifier(&pci_bus_type, &ioss->pci_bus_nb);

	/* Unregister all devices. Drivers will be unregistered when
	 * the glue drivers are unloaded.
	 */
	bus_for_each_dev(&pci_bus_type, NULL, ioss, __pcie_walk_del_device);
}

static int ioss_pci_nop_suspend(struct ioss_device *idev)
{
	struct pci_dev *pdev = to_pci_dev(ioss_idev_to_real(idev));

	idev->pm_stats.apps_suspend++;

	/* state_saved unset to avoid pci state restore that will reset MSI-X */
	pdev->state_saved = false;

	/* no_d3hot set to avoid pci restore state during pci_pm_resume_noirq()
	 * and to avoid pci save state during pci_pm_suspend_noirq()
	 */
	pdev->no_d3hot = true;

	ioss_dev_dbg(idev, "Device suspend performing nop");

	return 0;
}

static int ioss_pci_nop_resume(struct ioss_device *idev)
{
	struct pci_dev *pdev = to_pci_dev(ioss_idev_to_real(idev));

	idev->pm_stats.apps_resume++;
	pdev->no_d3hot = false;

	ioss_dev_dbg(idev, "Device resume performing nop");

	return 0;
}

static int ioss_pci_suspend_handler(struct device *dev);
static int ioss_pci_resume_handler(struct device *dev);

static bool __ioss_check_pm_ops(const struct dev_pm_ops *pm_ops)
{
	return pm_ops->suspend == ioss_pci_suspend_handler &&
		pm_ops->resume == ioss_pci_resume_handler;
}

/* There could be instances where a physical device may not have an idev or the
 * idev could not bind to the idrv. Use below API to retrieve real pm ops more
 * reliably. Call this function only through ioss_pci suspend/resume handler.
 */
static const struct dev_pm_ops *__real_pm_ops(struct device *dev)
{
	const struct dev_pm_ops *pm_ops = NULL;

	/* Provided no one has hijacked pm ops any further, below logic should
	 * be able to retrieve real pm ops.
	 */
	if (__ioss_check_pm_ops(dev->driver->pm))
		pm_ops = container_of(dev->driver->pm,
				struct ioss_driver, pm_ops)->pm_ops_real;
	else
		ioss_log_err(dev, "Driver PM ops not pointing to IOSS PM ops");

	if (!pm_ops) {
		struct ioss_device *idev = ioss_real_to_idev(dev);

		/* If there exists a idev and it is bound to an idrv, use real pm ops
		* from it.
		*/
		if (idev) {
			struct ioss_driver *idrv = ioss_dev_to_drv(idev);

			if (idrv)
				pm_ops = idrv->pm_ops_real;
			else
				ioss_dev_err(idev, "idev not bound to driver");
		} else {
			ioss_log_err(dev, "idev not present for device");
		}
	}

	return pm_ops;
}

static int ioss_pci_real_suspend(struct device *dev)
{
	struct ioss_device *idev = ioss_real_to_idev(dev);
	const struct dev_pm_ops *real_pm_ops = __real_pm_ops(dev);

	if (idev)
		idev->pm_stats.system_suspend++;

	ioss_log_msg(dev, "Delegating device suspend to real driver");

	if (real_pm_ops && real_pm_ops->suspend)
		return real_pm_ops->suspend(dev);

	ioss_log_err(dev, "Unable to delegate suspend to real driver");

	return -EINVAL;
}

static int ioss_pci_real_resume(struct device *dev)
{
	struct ioss_device *idev = ioss_real_to_idev(dev);
	const struct dev_pm_ops *real_pm_ops = __real_pm_ops(dev);

	if (idev)
		idev->pm_stats.system_resume++;

	ioss_log_msg(dev, "Delegating device resume to real driver");

	if (real_pm_ops && real_pm_ops->resume)
		return real_pm_ops->resume(dev);

	ioss_log_err(dev, "Unable to delegate resume to real driver");

	return -EINVAL;
}

static int ioss_pci_suspend_handler(struct device *dev)
{
	struct ioss_device *idev = ioss_real_to_idev(dev);

	return (idev && idev->interface.state == IOSS_IF_ST_ONLINE)?
			ioss_pci_nop_suspend(idev):
			ioss_pci_real_suspend(dev);
}

static int ioss_pci_resume_handler(struct device *dev)
{
	struct ioss_device *idev = ioss_real_to_idev(dev);

	return (idev && idev->interface.state == IOSS_IF_ST_ONLINE)?
			ioss_pci_nop_resume(idev):
			ioss_pci_real_resume(dev);
}

/* MSM PCIe driver invokes only suspend and resume callbacks, other operations
 * can be ignored unless we see a client requiring the feature.
 */

static const struct dev_pm_ops __ioss_pci_pm_ops = {
	.suspend = ioss_pci_suspend_handler,
	.resume = ioss_pci_resume_handler,
};

void ioss_pci_hijack_pm_ops(struct ioss_device *idev)
{
	struct device *dev = ioss_idev_to_real(idev);
	struct ioss_driver *idrv = ioss_dev_to_drv(idev);

	mutex_lock(&idrv->pm_lock);
	/* Hijack pm_ops */
	if (!refcount_inc_not_zero(&idrv->pm_refcnt)) {
		idrv->pm_ops_real = dev->driver->pm;
		dev->driver->pm = &idrv->pm_ops;
		refcount_set(&idrv->pm_refcnt, 1);
	}
	mutex_unlock(&idrv->pm_lock);
}

void ioss_pci_restore_pm_ops(struct ioss_device *idev)
{
	struct device *dev = ioss_idev_to_real(idev);
	struct ioss_driver *idrv = ioss_dev_to_drv(idev);

	mutex_lock(&idrv->pm_lock);
	/* Revert Hijack pm_ops */
	if (refcount_dec_and_test(&idrv->pm_refcnt))
		dev->driver->pm = idrv->pm_ops_real;
	mutex_unlock(&idrv->pm_lock);
}


int ioss_pci_enable_pc(struct ioss_device *idev)
{
	int rc;
	struct device *dev = ioss_idev_to_real(idev);
	struct pci_dev *pci_dev = to_pci_dev(dev);

	rc = msm_pcie_pm_control(MSM_PCIE_ENABLE_PC,
		pci_dev->bus->number, pci_dev, NULL, MSM_PCIE_CONFIG_INVALID);

	if (rc) {
		ioss_dev_err(idev,
			"Failed to enable MSM PCIe power collapse");
		return rc;
	}

	ioss_dev_log(idev, "Enabled MSM PCIe power collapse");

	return rc;
}

int ioss_pci_disable_pc(struct ioss_device *idev)
{
	int rc;
	struct device *dev = ioss_idev_to_real(idev);
	struct pci_dev *pci_dev = to_pci_dev(dev);

	rc = msm_pcie_pm_control(MSM_PCIE_DISABLE_PC,
		pci_dev->bus->number, pci_dev, NULL, MSM_PCIE_CONFIG_INVALID);

	if (rc) {
		ioss_dev_log(idev,
			"Failed to disable MSM PCIe power collapse");
		return rc;
	}

	ioss_dev_log(idev, "Disabled MSM PCIe power collapse");

	return rc;
}

int __ioss_pci_register_driver(struct ioss_driver *idrv, struct module *owner)
{
	ioss_log_cfg(NULL, "Registering PCI driver %s", idrv->name);

	idrv->drv.owner = owner;
	idrv->pm_ops = __ioss_pci_pm_ops;

	/* Init PCI driver attributes here */

	return ioss_bus_register_driver(idrv);
}
EXPORT_SYMBOL(__ioss_pci_register_driver);

void ioss_pci_unregister_driver(struct ioss_driver *idrv)
{
	ioss_log_cfg(NULL, "Unregistering PCI driver %s", idrv->name);

	ioss_bus_unregister_driver(idrv);
}
EXPORT_SYMBOL(ioss_pci_unregister_driver);
