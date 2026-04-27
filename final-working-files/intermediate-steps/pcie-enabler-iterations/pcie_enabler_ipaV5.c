#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/byteorder/generic.h>
#include <linux/irqflags.h>
#include <linux/preempt.h>

#define DRV_NAME            "pcie_enabler_ipa_v4_1"

/*
 * Target live tree (T99W373):
 *   /soc/qcom,pcie@1c00000   compatible = "qcom,pci-msm"
 *   /soc/qcom,ioss           compatible = "qcom,ioss-v2"
 *   /soc/ipa_eth_group1      phandle used by Quectel RTL8125 node on reference tree
 */
#define PCIE_RC_PATH        "/soc/qcom,pcie@1c00000"
#define PCIE_RC_COMPAT      "qcom,pci-msm"
#define IOSS_COMPAT         "qcom,ioss-v2"
#define IOMMU_GROUP_NAME    "ipa_eth_group1"

static struct platform_device *pcie_pdev;

/* ---------- property helpers ---------- */

static struct property *prop_build_raw(const char *name, const void *value, int len)
{
	struct property *pp;

	pp = kzalloc(sizeof(*pp), GFP_KERNEL);
	if (!pp)
		return NULL;

	pp->name = kstrdup(name, GFP_KERNEL);
	if (!pp->name)
		goto err;

	if (value && len > 0) {
		pp->value = kmemdup(value, len, GFP_KERNEL);
		if (!pp->value)
			goto err;
	}

	pp->length = len;
	pp->next = NULL;
	return pp;

err:
	kfree(pp->value);
	kfree(pp->name);
	kfree(pp);
	return NULL;
}

static struct property *prop_build_u32(const char *name, u32 val)
{
	__be32 data = cpu_to_be32(val);
	return prop_build_raw(name, &data, sizeof(data));
}

static struct property *prop_build_bool(const char *name)
{
	return prop_build_raw(name, NULL, 0);
}

static struct property *prop_build_string(const char *name, const char *val)
{
	return prop_build_raw(name, val, strlen(val) + 1);
}

static void prop_prepend(struct device_node *np, struct property *pp)
{
	if (!np || !pp)
		return;

	pp->next = np->properties;
	np->properties = pp;
}

static struct property *find_prop(struct device_node *np, const char *name)
{
	struct property *pp;

	if (!np)
		return NULL;

	for_each_property_of_node(np, pp) {
		if (!strcmp(pp->name, name))
			return pp;
	}

	return NULL;
}

/* ---------- cleanup helpers (only for not-yet-attached nodes) ---------- */

static void free_prop_list(struct property *pp)
{
	struct property *next;

	while (pp) {
		next = pp->next;
		kfree(pp->value);
		kfree(pp->name);
		kfree(pp);
		pp = next;
	}
}

static void free_prepared_node(struct device_node *np)
{
	if (!np)
		return;

	free_prop_list(np->properties);
	kfree((char *)np->name);
	kfree((char *)np->full_name);
	kfree(np);
}

/* ---------- OF surgery helpers ---------- */

static void attach_node_manual(struct device_node *parent, struct device_node *child)
{
	child->parent = parent;
	child->sibling = parent->child;
	parent->child = child;
}

static u32 find_max_phandle(void)
{
	struct device_node *np;
	u32 max = 0;

	for_each_of_allnodes(np) {
		if (np->phandle > max)
			max = np->phandle;
	}

	return max;
}

static struct device_node *find_child_by_name(struct device_node *parent, const char *name)
{
	struct device_node *child;

	if (!parent)
		return NULL;

	for_each_child_of_node(parent, child) {
		if (of_node_name_eq(child, name))
			return child;
	}

	return NULL;
}

/* ---------- node builders ---------- */

static struct device_node *build_rx_node(u32 phandle)
{
	struct device_node *np;

	np = kzalloc(sizeof(*np), GFP_KERNEL);
	if (!np)
		return NULL;

	of_node_init(np);
	of_node_set_flag(np, OF_DYNAMIC);

	np->name = kstrdup("r8125_rx", GFP_KERNEL);
	np->full_name = kstrdup("/soc/r8125_rx@eth0", GFP_KERNEL);
	if (!np->name || !np->full_name)
		goto err;

	np->phandle = phandle;

	prop_prepend(np, prop_build_u32("linux,phandle", phandle));
	prop_prepend(np, prop_build_u32("phandle", phandle));
	prop_prepend(np, prop_build_u32("qcom,mod-usecs-max", 0x3c));
	prop_prepend(np, prop_build_u32("qcom,mod-usecs-min", 0x1e));
	prop_prepend(np, prop_build_u32("qcom,mod-count-max", 0x40));
	prop_prepend(np, prop_build_u32("qcom,mod-count-min", 0x20));
	prop_prepend(np, prop_build_u32("qcom,buff-size", 0x800));
	prop_prepend(np, prop_build_u32("qcom,ring-size", 0x3ff));
	prop_prepend(np, prop_build_bool("qcom,rx-filter-ip"));
	prop_prepend(np, prop_build_bool("qcom,dir-rx"));

	if (!np->properties)
		goto err;

	return np;

err:
	free_prepared_node(np);
	return NULL;
}

static struct device_node *build_tx_node(u32 phandle)
{
	struct device_node *np;

	np = kzalloc(sizeof(*np), GFP_KERNEL);
	if (!np)
		return NULL;

	of_node_init(np);
	of_node_set_flag(np, OF_DYNAMIC);

	np->name = kstrdup("r8125_tx", GFP_KERNEL);
	np->full_name = kstrdup("/soc/r8125_tx@eth0", GFP_KERNEL);
	if (!np->name || !np->full_name)
		goto err;

	np->phandle = phandle;

	prop_prepend(np, prop_build_u32("linux,phandle", phandle));
	prop_prepend(np, prop_build_u32("phandle", phandle));
	prop_prepend(np, prop_build_u32("qcom,mod-usecs-max", 0x3c));
	prop_prepend(np, prop_build_u32("qcom,mod-usecs-min", 0x1e));
	prop_prepend(np, prop_build_u32("qcom,mod-count-max", 0x40));
	prop_prepend(np, prop_build_u32("qcom,mod-count-min", 0x20));
	prop_prepend(np, prop_build_u32("qcom,buff-size", 0x800));
	prop_prepend(np, prop_build_u32("qcom,ring-size", 0x400));
	prop_prepend(np, prop_build_bool("qcom,dir-tx"));

	if (!np->properties)
		goto err;

	return np;

err:
	free_prepared_node(np);
	return NULL;
}

static struct device_node *build_eth_node(struct device_node *rp_node,
					  u32 ioss_phandle,
					  u32 iommu_group_phandle,
					  u32 rx_phandle,
					  u32 tx_phandle)
{
	static const char pci_ids[] = "10ec:8125" "\0" "10ec:3000";
	struct device_node *np;
	__be32 reg_data[5] = { 0, 0, 0, 0, 0 };
	__be32 channels[2];
	__be32 ioss_be;
	__be32 iommu_be;
	char *full_name;

	channels[0] = cpu_to_be32(rx_phandle);
	channels[1] = cpu_to_be32(tx_phandle);
	ioss_be = cpu_to_be32(ioss_phandle);
	iommu_be = cpu_to_be32(iommu_group_phandle);

	full_name = kasprintf(GFP_KERNEL, "%s/realtek,rtl8125@pcie0_rp",
			      rp_node->full_name ? rp_node->full_name : "/soc/qcom,pcie@1c00000/pcie0_rp");
	if (!full_name)
		return NULL;

	np = kzalloc(sizeof(*np), GFP_KERNEL);
	if (!np) {
		kfree(full_name);
		return NULL;
	}

	of_node_init(np);
	of_node_set_flag(np, OF_DYNAMIC);

	/*
	 * name = base node name (before '@')
	 * full_name = full DT path, unit-address included
	 */
	np->name = kstrdup("realtek,rtl8125", GFP_KERNEL);
	np->full_name = full_name;
	if (!np->name || !np->full_name)
		goto err;

	prop_prepend(np, prop_build_raw("pci-ids", pci_ids, sizeof(pci_ids)));
	prop_prepend(np, prop_build_u32("#size-cells", 0x01));
	prop_prepend(np, prop_build_u32("#address-cells", 0x01));
	prop_prepend(np, prop_build_u32("qcom,ioss_instance", 0x00));
	prop_prepend(np, prop_build_raw("qcom,iommu-group", &iommu_be, sizeof(iommu_be)));
	prop_prepend(np, prop_build_raw("qcom,ioss_channels", channels, sizeof(channels)));
	prop_prepend(np, prop_build_raw("qcom,ioss", &ioss_be, sizeof(ioss_be)));
	prop_prepend(np, prop_build_string("compatible", "qcom,ioss-v2-device"));
	prop_prepend(np, prop_build_raw("reg", reg_data, sizeof(reg_data)));

	if (!np->properties)
		goto err;

	return np;

err:
	free_prepared_node(np);
	return NULL;
}

/* ---------- main ---------- */

static int __init pcie_ipa_v4_1_init(void)
{
	unsigned long flags;
	u32 max_phandle, rx_phandle, tx_phandle;
	u32 ioss_phandle, iommu_group_phandle;
	struct device_node *soc_node = NULL;
	struct device_node *pcie_node = NULL;
	struct device_node *rp_node = NULL;
	struct device_node *ioss_node = NULL;
	struct device_node *iommu_node = NULL;
	struct device_node *rx_node = NULL;
	struct device_node *tx_node = NULL;
	struct device_node *eth_node = NULL;
	struct property *status_pp;
	struct property *rp_addr_cells_pp = NULL;
	struct property *rp_size_cells_pp = NULL;
	char *status_okay = NULL;

	pr_info("[%s] starting live OF mutation for PCIe/IOSS\n", DRV_NAME);

	soc_node = of_find_node_by_path("/soc");
	if (!soc_node) {
		pr_err("[%s] /soc not found\n", DRV_NAME);
		return -ENODEV;
	}

	pcie_node = of_find_node_by_path(PCIE_RC_PATH);
	if (!pcie_node)
		pcie_node = of_find_compatible_node(NULL, NULL, PCIE_RC_COMPAT);

	if (!pcie_node) {
		pr_err("[%s] PCIe RC not found (%s / %s)\n",
		       DRV_NAME, PCIE_RC_PATH, PCIE_RC_COMPAT);
		return -ENODEV;
	}

	rp_node = find_child_by_name(pcie_node, "pcie0_rp");
	if (!rp_node) {
		pr_err("[%s] child node pcie0_rp not found under RC\n", DRV_NAME);
		return -ENODEV;
	}

	ioss_node = of_find_compatible_node(NULL, NULL, IOSS_COMPAT);
	if (!ioss_node || !ioss_node->phandle) {
		pr_err("[%s] live qcom,ioss-v2 node not found / missing phandle\n", DRV_NAME);
		return -ENODEV;
	}
	ioss_phandle = ioss_node->phandle;

	iommu_node = of_find_node_by_name(NULL, IOMMU_GROUP_NAME);
	if (!iommu_node || !iommu_node->phandle) {
		pr_err("[%s] live %s node not found / missing phandle\n",
		       DRV_NAME, IOMMU_GROUP_NAME);
		return -ENODEV;
	}
	iommu_group_phandle = iommu_node->phandle;

	max_phandle = find_max_phandle();
	rx_phandle = max_phandle + 1;
	tx_phandle = max_phandle + 2;

	pr_info("[%s] resolved live phandles: ioss=0x%x iommu-group=0x%x rx=0x%x tx=0x%x\n",
		DRV_NAME, ioss_phandle, iommu_group_phandle, rx_phandle, tx_phandle);

	status_pp = find_prop(pcie_node, "status");
	status_okay = kstrdup("okay", GFP_KERNEL);
	if (!status_okay) {
		pr_err("[%s] failed to allocate status replacement\n", DRV_NAME);
		return -ENOMEM;
	}

	if (!find_prop(rp_node, "#address-cells"))
		rp_addr_cells_pp = prop_build_u32("#address-cells", 0x05);

	if (!find_prop(rp_node, "#size-cells"))
		rp_size_cells_pp = prop_build_u32("#size-cells", 0x00);

	rx_node = build_rx_node(rx_phandle);
	tx_node = build_tx_node(tx_phandle);
	eth_node = build_eth_node(rp_node, ioss_phandle, iommu_group_phandle,
				  rx_phandle, tx_phandle);

	if (!rx_node || !tx_node || !eth_node) {
		pr_err("[%s] failed preparing dynamic nodes\n", DRV_NAME);
		free_prepared_node(rx_node);
		free_prepared_node(tx_node);
		free_prepared_node(eth_node);
		kfree(status_okay);
		kfree(rp_addr_cells_pp);
		kfree(rp_size_cells_pp);
		return -ENOMEM;
	}

	/*
	 * IMPORTANT:
	 * We do not have devtree_lock exported on this OEM tree.
	 * Keep the critical section tiny: only pointer splices / property pointer swaps.
	 */
	preempt_disable();
	local_irq_save(flags);

	if (status_pp) {
		status_pp->value = status_okay;
		status_pp->length = 5; /* "okay\0" */
	} else {
		struct property *new_status = prop_build_string("status", "okay");
		if (new_status)
			prop_prepend(pcie_node, new_status);
	}

	if (rp_addr_cells_pp)
		prop_prepend(rp_node, rp_addr_cells_pp);
	if (rp_size_cells_pp)
		prop_prepend(rp_node, rp_size_cells_pp);

	attach_node_manual(soc_node, rx_node);
	attach_node_manual(soc_node, tx_node);
	attach_node_manual(rp_node, eth_node);

	local_irq_restore(flags);
	preempt_enable();

	pr_info("[%s] OF topology injected: RC enabled, RP prepared, RTL8125 + RX/TX nodes attached\n",
		DRV_NAME);

	pcie_pdev = of_platform_device_create(pcie_node, NULL, NULL);
	if (!pcie_pdev) {
		pr_err("[%s] of_platform_device_create() failed\n", DRV_NAME);
		return -ENODEV;
	}

	pr_info("[%s] platform wake-up issued successfully\n", DRV_NAME);
	return 0;
}

static void __exit pcie_ipa_v4_1_exit(void)
{
	/*
	 * Deliberately minimal:
	 * this module is intended as one-shot DT mutation for test boots.
	 * We only unregister the synthetic platform device and do not attempt
	 * to undo live OF pointer surgery on unload.
	 */
	if (pcie_pdev)
		of_device_unregister(pcie_pdev);

	pr_info("[%s] exit complete (live OF mutation not reverted)\n", DRV_NAME);
}

module_init(pcie_ipa_v4_1_init);
module_exit(pcie_ipa_v4_1_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("OpenAI / Manuel lab");
MODULE_DESCRIPTION("SDX62/T99W373 live PCIe + IOSS DT RAM injector V4.1");