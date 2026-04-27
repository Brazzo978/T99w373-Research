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
#include <linux/pci.h>
#include <linux/delay.h>

#define DRV_NAME        "pcie_enabler_v7_base"
#define PCIE_RC_PATH    "/soc/qcom,pcie@1c00000"
#define PCIE_RC_COMPAT  "qcom,pci-msm"

#define RTL8125_VENDOR  0x10ec
#define RTL8125_DEVICE  0x8125

/* T99 live DTS values - verified from /sys/firmware/devicetree/base */
#define T99_IOSS_PHANDLE        0x123   /* /soc/qcom,ioss phandle */
#define T99_IOMMU_GRP_PHANDLE   0x12a   /* /soc/qcom,ipa@03e00000/ipa_smmu_eth phandle */

static struct platform_device *pcie_pdev;

/* ---------------- property helpers ---------------- */

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

/* ---------------- tree helpers ---------------- */

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

static void attach_node_manual(struct device_node *parent, struct device_node *child)
{
	child->parent = parent;
	child->sibling = parent->child;
	parent->child = child;
}

/* ---------------- cleanup helpers ---------------- */

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

/* ---------------- node builder ---------------- */

/*
 * IOSS does NOT import of_get_child_by_name / of_get_next_child.
 * qcom,ioss_channels is a flat u32 property (channel count), not a sub-node.
 * Per-channel properties (ring-size, buff-size, dir) are also flat arrays.
 *
 * Confirmed from nm ioss_rebuilt.ko imported symbols:
 *   of_find_property, of_property_read_variable_u32_array,
 *   of_count_phandle_with_args, of_parse_phandle
 */
#define ETH_FULL_PATH  "/soc/qcom,pcie@1c00000/pcie0_rp/realtek,rtl8125@0,0"
#define IOSS_NUM_CH    2

static struct device_node *build_eth_node_v7_base(void)
{
	static const char pci_ids[] = "10ec:8125" "\0" "10ec:3000";
	/* 2 channels: TX (idx 0) and RX (idx 1) */
	static const __be32 ring_sizes[IOSS_NUM_CH]  = {
		cpu_to_be32(512), cpu_to_be32(512) };
	static const __be32 buff_sizes[IOSS_NUM_CH]  = {
		cpu_to_be32(2048), cpu_to_be32(2048) };
	__be32 reg_data[5] = { 0, 0, 0, 0, 0 };
	__be32 ioss_be = cpu_to_be32(T99_IOSS_PHANDLE);
	struct device_node *eth_np;

	eth_np = kzalloc(sizeof(*eth_np), GFP_KERNEL);
	if (!eth_np)
		return NULL;

	eth_np->name = kstrdup("realtek,rtl8125", GFP_KERNEL);
	eth_np->full_name = kstrdup(ETH_FULL_PATH, GFP_KERNEL);
	if (!eth_np->name || !eth_np->full_name)
		goto err;

	prop_prepend(eth_np, prop_build_raw("pci-ids", pci_ids, sizeof(pci_ids)));
	prop_prepend(eth_np, prop_build_u32("#size-cells", 0x01));
	prop_prepend(eth_np, prop_build_u32("#address-cells", 0x01));
	prop_prepend(eth_np, prop_build_u32("qcom,ioss_instance", 0x00));

	/* Channel count as flat u32 */
	prop_prepend(eth_np, prop_build_u32("qcom,ioss_channels", IOSS_NUM_CH));

	/* Per-channel arrays: index 0 = TX, index 1 = RX */
	prop_prepend(eth_np, prop_build_raw("qcom,buff-size",
					    buff_sizes, sizeof(buff_sizes)));
	prop_prepend(eth_np, prop_build_raw("qcom,ring-size",
					    ring_sizes, sizeof(ring_sizes)));
	/* dir-tx / dir-rx as boolean (presence flag) on the device node */
	prop_prepend(eth_np, prop_build_raw("qcom,dir-rx", NULL, 0));
	prop_prepend(eth_np, prop_build_raw("qcom,dir-tx", NULL, 0));

	/* NOTE: qcom,iommu-group omitted for now - causes kernel panic during ioss probe.
	 * Investigate what ioss does with ipa_smmu_eth before re-enabling. */
	prop_prepend(eth_np, prop_build_raw("qcom,ioss", &ioss_be, sizeof(ioss_be)));
	prop_prepend(eth_np, prop_build_string("compatible", "qcom,ioss-v2-device"));
	prop_prepend(eth_np, prop_build_raw("reg", reg_data, sizeof(reg_data)));

	if (!eth_np->properties)
		goto err;

	return eth_np;

err:
	free_prepared_node(eth_np);
	return NULL;
}

/* ---------------- debug helpers ---------------- */

static void debug_dump_rtl8125_of_binding(void)
{
	int i;
	struct pci_dev *pdev = NULL;

	for (i = 0; i < 30; i++) {
		pdev = pci_get_device(RTL8125_VENDOR, RTL8125_DEVICE, NULL);
		if (pdev)
			break;
		msleep(100);
	}

	if (!pdev) {
		pr_warn("[%s] DEBUG: RTL8125 PCI device not found after RC wake-up\n",
			DRV_NAME);
		return;
	}

	pr_info("[%s] DEBUG: found RTL8125 at %04x:%02x:%02x.%d\n",
		DRV_NAME,
		pci_domain_nr(pdev->bus),
		pdev->bus->number,
		PCI_SLOT(pdev->devfn),
		PCI_FUNC(pdev->devfn));

	if (pdev->dev.of_node) {
		pr_info("[%s] DEBUG: pdev->dev.of_node = %p\n",
			DRV_NAME, pdev->dev.of_node);
		pr_info("[%s] DEBUG: pdev->dev.of_node->name = %s\n",
			DRV_NAME,
			pdev->dev.of_node->name ? pdev->dev.of_node->name : "(null)");
		pr_info("[%s] DEBUG: pdev->dev.of_node->full_name = %s\n",
			DRV_NAME,
			pdev->dev.of_node->full_name ? pdev->dev.of_node->full_name : "(null)");
	} else {
		pr_warn("[%s] DEBUG: pdev->dev.of_node is NULL\n", DRV_NAME);
	}

	pci_dev_put(pdev);
}

/* ---------------- module init ---------------- */

static int __init pcie_enabler_v7_base_init(void)
{
	unsigned long flags;
	struct device_node *pcie_node = NULL;
	struct device_node *rp_node = NULL;
	struct device_node *eth_node = NULL;
	struct property *status_pp;
	struct property *rp_addr_cells_pp = NULL;
	struct property *rp_size_cells_pp = NULL;
	static char status_okay[] = "okay";

	pr_info("[%s] init: OF/PCI + base IOSS properties test\n", DRV_NAME);

	pcie_node = of_find_node_by_path(PCIE_RC_PATH);
	if (!pcie_node)
		pcie_node = of_find_compatible_node(NULL, NULL, PCIE_RC_COMPAT);

	if (!pcie_node) {
		pr_err("[%s] PCIe RC not found\n", DRV_NAME);
		return -ENODEV;
	}

	rp_node = find_child_by_name(pcie_node, "pcie0_rp");
	if (!rp_node) {
		pr_err("[%s] pcie0_rp not found\n", DRV_NAME);
		return -ENODEV;
	}

	/* Avoid duplicate injection */
	if (find_child_by_name(rp_node, "realtek,rtl8125")) {
		pr_warn("[%s] RTL8125 child already present under pcie0_rp\n", DRV_NAME);
	} else {
		eth_node = build_eth_node_v7_base();
		if (!eth_node) {
			pr_err("[%s] failed to build RTL8125 child node\n", DRV_NAME);
			return -ENOMEM;
		}
	}

	status_pp = find_prop(pcie_node, "status");

	if (!find_prop(rp_node, "#address-cells"))
		rp_addr_cells_pp = prop_build_u32("#address-cells", 0x05);

	if (!find_prop(rp_node, "#size-cells"))
		rp_size_cells_pp = prop_build_u32("#size-cells", 0x00);

	preempt_disable();
	local_irq_save(flags);

	if (status_pp) {
		status_pp->value = status_okay;
		status_pp->length = sizeof(status_okay);
	} else {
		struct property *new_status = prop_build_string("status", "okay");
		if (new_status)
			prop_prepend(pcie_node, new_status);
	}

	if (rp_addr_cells_pp)
		prop_prepend(rp_node, rp_addr_cells_pp);

	if (rp_size_cells_pp)
		prop_prepend(rp_node, rp_size_cells_pp);

	if (eth_node)
		attach_node_manual(rp_node, eth_node);

	local_irq_restore(flags);
	preempt_enable();

	pr_info("[%s] RC enabled, Root Port prepared, RTL8125 child injected\n",
		DRV_NAME);
	pr_info("[%s] Added base props: qcom,ioss=0x%x qcom,iommu-group=0x%x qcom,ioss_instance=0\n",
		DRV_NAME, T99_IOSS_PHANDLE, T99_IOMMU_GRP_PHANDLE);

	pcie_pdev = of_platform_device_create(pcie_node, NULL, NULL);
	if (!pcie_pdev) {
		pr_err("[%s] of_platform_device_create() failed\n", DRV_NAME);
		return -ENODEV;
	}

	pr_info("[%s] RC platform device created\n", DRV_NAME);

	msleep(700);
	debug_dump_rtl8125_of_binding();

	return 0;
}

static void __exit pcie_enabler_v7_base_exit(void)
{
	if (pcie_pdev)
		of_device_unregister(pcie_pdev);

	pr_info("[%s] exit: OF surgery not reverted\n", DRV_NAME);
}

module_init(pcie_enabler_v7_base_init);
module_exit(pcie_enabler_v7_base_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("OpenAI / Manuel lab");
MODULE_DESCRIPTION("T99W373 PCIe V7 base IOSS property test");