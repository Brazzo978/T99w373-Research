/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */

#include <linux/of.h>
#include <linux/device.h>
#include <linux/platform_device.h>

#include <linux/module.h>

#include "ioss_i.h"
#include "ioss_version.h"

unsigned long ioss_ver = IOSS_VER;
unsigned long ioss_api_ver = IOSS_API_VER;

static int ioss_parse_dt(struct ioss *ioss)
{
	int rc;
	struct device_node *node = dev_of_node(&ioss->pdev->dev);

	if (!node)
		return -EFAULT;

	rc  = of_property_read_u32(node,
			"qcom,max-ddr-bandwidth", &ioss->max_ddr_bandwidth);
	if (rc)
		ioss_log_dbg(NULL, "No DDR bandwidth limit specified in DT");

	if (ioss->max_ddr_bandwidth)
		ioss_log_dbg(NULL, "DDR bandwidth limit set to %u",
				ioss->max_ddr_bandwidth);

	return 0;
}

static void ioss_ipa_ready_notif(void *userdata)
{
	struct ioss *ioss = userdata;

	set_bit(IOSS_ST_IPA_RDY, &ioss->status);

	ioss_log_cfg(NULL, "IPA is ready");

	if (ioss_pci_start(ioss)) {
		set_bit(IOSS_ST_ERROR, &ioss->status);
		ioss_log_err(NULL, "Failed to start PCI sub-system");
		return;
	}
}

static int ioss_probe(struct platform_device *pdev)
{
	int rc = -EFAULT;
	struct ioss *ioss;
	struct ioss_priv_data *ioss_priv;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	unsigned int wq_flags = WQ_UNBOUND | WQ_MEM_RECLAIM;

	dev_dbg(dev, "Probing IOSS root device\n");

	ioss = kzalloc(sizeof(*ioss), GFP_KERNEL);
	if (!ioss)
		return -ENOMEM;

	ioss->pdev = pdev;

	ioss->ioss_priv = kzalloc(sizeof(struct ioss_priv_data), GFP_KERNEL);
	if (!ioss->ioss_priv) {
		kfree(ioss);
		return -ENOMEM;
	}
	ioss_priv = ioss->ioss_priv;
	ioss_priv->ioss = ioss;

	np->data = ioss;
	platform_set_drvdata(pdev, ioss);

	rc = ioss_parse_dt(ioss);
	if (rc) {
		ioss_log_err(NULL, "Failed to parse devicetree\n");
		goto err;
	}

	/* Freeze the workqueue so that a refresh will not happen while the
	 * device is suspended as the suspend operation itself can generate
	 * Netlink events.
	 */
	wq_flags |= WQ_FREEZABLE;

	ioss->wq = alloc_workqueue(dev_name(&pdev->dev), wq_flags, 0);
	if (!ioss->wq) {
		ioss_log_err(NULL, "Failed to alloc work queue\n");
		goto err;
	}

	ioss_priv->ipa_ready.notify = ioss_ipa_ready_notif;
	ioss_priv->ipa_ready.userdata = ioss;

	rc = ipa_eth_register_ready_cb(&ioss_priv->ipa_ready);
	if (rc) {
		ioss_log_err(NULL, "Failed to register IPA ready cb");
		goto err;
	}

	set_bit(IOSS_ST_PROBED, &ioss->status);

	ioss_log_cfg(NULL, "Root device probe completed");

	return 0;

err:
	if (ioss->wq)
		destroy_workqueue(ioss->wq);

	kzfree(ioss->ioss_priv);
	kzfree(ioss);

	platform_set_drvdata(pdev, NULL);
	np->data = NULL;

	return -EFAULT;
}

static int ioss_remove(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct ioss *ioss = platform_get_drvdata(pdev);
	struct ioss_priv_data *ioss_priv = ioss->ioss_priv;

	ipa_eth_unregister_ready_cb(&ioss_priv->ipa_ready);

	/* Safe to call even if ioss_pci_start() was not called */
	ioss_pci_stop(ioss);

	destroy_workqueue(ioss->wq);

	kzfree(ioss->ioss_priv);
	kzfree(ioss);

	platform_set_drvdata(pdev, NULL);
	np->data = NULL;

	return 0;
}

static const struct of_device_id ioss_pf_match_table[] = {
	{ .compatible = "qcom,ioss-v2", },
	{},
};

static struct platform_driver ioss_drv = {
	.probe = ioss_probe,
	.remove = ioss_remove,
	.driver = {
		.name = IOSS_SUBSYS,
		.owner = THIS_MODULE,
		.of_match_table = ioss_pf_match_table,
	},
};

struct platform_device *ioss_find_dev_from_of_node(struct device_node *np)
{
	struct ioss *ioss = np->data;

	return ioss ? ioss->pdev : NULL;
}

static int __init ioss_module_init(void)
{
	int rc;

	rc = ioss_log_init();
	if (rc) {
		pr_err("Failed to initialize IOSS logging\n");
		return rc;
	}

	rc = bus_register(&ioss_bus);
	if (rc) {
		ioss_log_err(NULL, "Failed to register IOSS bus");
		goto err_bus;
	}

	rc = ioss_debugfs_init();
	if (rc) {
		ioss_log_err(NULL, "Failed to create the debugfs directory");
		goto err_debugfs;
	}

	rc = platform_driver_register(&ioss_drv);
	if (rc) {
		ioss_log_err(NULL, "Failed to register IOSS platform driver");
		goto err_platform;
	}

	return 0;

err_platform:
	ioss_debugfs_exit();
err_debugfs:
	bus_unregister(&ioss_bus);
err_bus:
	ioss_log_deinit();
	return rc;
}
module_init(ioss_module_init);

static void __exit ioss_module_exit(void)
{
	platform_driver_unregister(&ioss_drv);
	ioss_debugfs_exit();
	bus_unregister(&ioss_bus);
	ioss_log_deinit();
}
module_exit(ioss_module_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("IPA Offload Sub-System v2");
