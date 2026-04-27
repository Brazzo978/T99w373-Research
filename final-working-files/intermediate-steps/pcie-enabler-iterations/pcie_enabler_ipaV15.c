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

#define DRV_NAME        "pcie_enabler_v15"
#define PCIE_RC_PATH    "/soc/qcom,pcie@1c00000"
#define PCIE_RC_COMPAT  "qcom,pci-msm"

#define RTL8125_VENDOR  0x10ec
#define RTL8125_DEVICE  0x8125

#define T99_IOSS_PHANDLE        0x123
/* ipa_smmu_eth phandle — needed by IPA to associate RTL8125 DMA
 * with the correct SMMU domain before mapping ring buffers */
#define T99_IOMMU_GRP_PHANDLE   0x12a

#define ETH_FULL_PATH   "/soc/qcom,pcie@1c00000/pcie0_rp/realtek,rtl8125@0,0"
#define CH_RX_FULL_PATH ETH_FULL_PATH "/channel-rx"
#define CH_TX_FULL_PATH ETH_FULL_PATH "/channel-tx"

#define CH_RX_PHANDLE   0x81250001
#define CH_TX_PHANDLE   0x81250002

static struct platform_device *pcie_pdev;

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

static struct device_node *build_channel_node(const char *name,
					      const char *full_name,
					      u32 phandle,
					      bool is_rx)
{
	struct device_node *np;

	np = kzalloc(sizeof(*np), GFP_KERNEL);
	if (!np)
		return NULL;

	/*
	 * of_parse_phandle() ultimately compares against np->phandle,
	 * not only the "phandle" property content. On this OEM tree,
	 * parent/child/sibling traversal is enough for discovery, but
	 * np->phandle must be populated explicitly for runtime nodes.
	 */
	np->phandle = phandle;
	np->name = kstrdup(name, GFP_KERNEL);
	np->full_name = kstrdup(full_name, GFP_KERNEL);
	if (!np->name || !np->full_name)
		goto err;

	prop_prepend(np, prop_build_string("name", name));
	prop_prepend(np, prop_build_u32("phandle", phandle));
	prop_prepend(np, prop_build_u32("linux,phandle", phandle));
	prop_prepend(np, prop_build_u32("qcom,ring-size", is_rx ? 1023 : 1024));
	prop_prepend(np, prop_build_u32("qcom,buff-size", 2048));
	prop_prepend(np, prop_build_u32("qcom,mod-count-min", 32));
	prop_prepend(np, prop_build_u32("qcom,mod-count-max", 64));
	prop_prepend(np, prop_build_u32("qcom,mod-usecs-min", 30));
	prop_prepend(np, prop_build_u32("qcom,mod-usecs-max", 60));

	if (is_rx) {
		prop_prepend(np, prop_build_raw("qcom,dir-rx", NULL, 0));
		prop_prepend(np, prop_build_raw("qcom,rx-filter-ip", NULL, 0));
	} else {
		prop_prepend(np, prop_build_raw("qcom,dir-tx", NULL, 0));
	}

	if (!np->properties)
		goto err;

	return np;

err:
	free_prepared_node(np);
	return NULL;
}

static struct device_node *build_eth_node_v9(void)
{
	static const char pci_ids[] = "10ec:8125" "\0" "10ec:3000";
	static const __be32 ioss_channels[] = {
		cpu_to_be32(CH_RX_PHANDLE),
		cpu_to_be32(CH_TX_PHANDLE),
	};
	__be32 reg_data[5] = { 0, 0, 0, 0, 0 };
	__be32 ioss_be = cpu_to_be32(T99_IOSS_PHANDLE);
	struct device_node *eth_np;
	struct device_node *ch_rx;
	struct device_node *ch_tx;

	eth_np = kzalloc(sizeof(*eth_np), GFP_KERNEL);
	if (!eth_np)
		return NULL;

	eth_np->name = kstrdup("realtek,rtl8125", GFP_KERNEL);
	eth_np->full_name = kstrdup(ETH_FULL_PATH, GFP_KERNEL);
	if (!eth_np->name || !eth_np->full_name)
		goto err;

	ch_rx = build_channel_node("channel-rx", CH_RX_FULL_PATH, CH_RX_PHANDLE, true);
	if (!ch_rx)
		goto err;

	ch_tx = build_channel_node("channel-tx", CH_TX_FULL_PATH, CH_TX_PHANDLE, false);
	if (!ch_tx) {
		free_prepared_node(ch_rx);
		goto err;
	}

	attach_node_manual(eth_np, ch_tx);
	attach_node_manual(eth_np, ch_rx);

	prop_prepend(eth_np, prop_build_raw("pci-ids", pci_ids, sizeof(pci_ids)));
	prop_prepend(eth_np, prop_build_u32("#size-cells", 0x01));
	prop_prepend(eth_np, prop_build_u32("#address-cells", 0x01));
	prop_prepend(eth_np, prop_build_u32("qcom,ioss_instance", 0x00));
	prop_prepend(eth_np, prop_build_raw("qcom,ioss_channels",
					    ioss_channels, sizeof(ioss_channels)));
	{
		__be32 iommu_be = cpu_to_be32(T99_IOMMU_GRP_PHANDLE);
		prop_prepend(eth_np, prop_build_raw("qcom,iommu-group",
						    &iommu_be, sizeof(iommu_be)));
	}
	prop_prepend(eth_np, prop_build_raw("qcom,ioss", &ioss_be, sizeof(ioss_be)));
	prop_prepend(eth_np, prop_build_string("qcom,iommu-dma", "bypass"));
	prop_prepend(eth_np, prop_build_string("compatible", "qcom,ioss-v2-device"));
	prop_prepend(eth_np, prop_build_raw("reg", reg_data, sizeof(reg_data)));

	if (!eth_np->properties)
		goto err;

	return eth_np;

err:
	free_prepared_node(eth_np);
	return NULL;
}

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

static int __init pcie_enabler_v15_init(void)
{
	unsigned long flags;
	struct device_node *pcie_node = NULL;
	struct device_node *rp_node = NULL;
	struct device_node *eth_node = NULL;
	struct property *status_pp;
	struct property *rp_addr_cells_pp = NULL;
	struct property *rp_size_cells_pp = NULL;
	static char status_okay[] = "okay";

	pr_info("[%s] init: OF/PCI + channels + iommu-group + qcom,iommu-dma=bypass\n",
		DRV_NAME);

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

	if (find_child_by_name(rp_node, "realtek,rtl8125")) {
		pr_warn("[%s] RTL8125 child already present under pcie0_rp\n", DRV_NAME);
	} else {
		eth_node = build_eth_node_v9();
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

	pr_info("[%s] RC enabled, Root Port prepared, RTL8125 node + channel phandles injected\n",
		DRV_NAME);

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

static void __exit pcie_enabler_v15_exit(void)
{
	if (pcie_pdev)
		of_device_unregister(pcie_pdev);

	pr_info("[%s] exit: OF surgery not reverted\n", DRV_NAME);
}

module_init(pcie_enabler_v15_init);
module_exit(pcie_enabler_v15_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("OpenAI / Manuel lab");
MODULE_DESCRIPTION("T99W373 PCIe V15 IOSS channels + qcom,iommu-dma=bypass");
