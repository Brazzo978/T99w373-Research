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
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/iommu.h>
#include <linux/io-pgtable.h>
#include <linux/io-pgtable-fast.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include "arm-smmu.h"

#define DRV_NAME        "pcie_enabler_v20"
#define PCIE_RC_PATH    "/soc/qcom,pcie@1c00000"
#define PCIE_RC_COMPAT  "qcom,pci-msm"

#define RTL8125_VENDOR  0x10ec
#define RTL8125_DEVICE  0x8125

#define T99_IOSS_PHANDLE        0x123

#define ETH_FULL_PATH   "/soc/qcom,pcie@1c00000/pcie0_rp/realtek,rtl8125@0,0"
#define CH_RX_FULL_PATH ETH_FULL_PATH "/channel-rx"
#define CH_TX_FULL_PATH ETH_FULL_PATH "/channel-tx"

#define CH_RX_PHANDLE   0x81250001
#define CH_TX_PHANDLE   0x81250002

static struct platform_device *pcie_pdev;

struct smmu_dev_search {
	const char *suffix;
	struct device *found;
};

static int smmu_dev_match(struct device *dev, void *data)
{
	struct smmu_dev_search *ctx = data;
	const char *name = dev_name(dev);
	size_t nlen = strlen(name);
	size_t slen = strlen(ctx->suffix);

	if (nlen >= slen && strcmp(name + nlen - slen, ctx->suffix) == 0) {
		ctx->found = get_device(dev);
		return 1;
	}

	return 0;
}

static struct device *find_ipa_smmu_dev(const char *suffix)
{
	struct smmu_dev_search ctx = { .suffix = suffix, .found = NULL };

	bus_for_each_dev(&platform_bus_type, NULL, &ctx, smmu_dev_match);
	return ctx.found;
}

static void *normalize_pmds_ptr(void *pmds_raw)
{
	unsigned long raw = (unsigned long)pmds_raw;
	unsigned long norm = raw & ~0xfffUL;

	if (!norm)
		return NULL;

	return (void *)norm;
}

static int fix_one_ipa_smmu_pmds(const char *slave_suffix)
{
	struct device *ap_dev = NULL;
	struct device *slave_dev = NULL;
	struct iommu_domain *ap_domain = NULL;
	struct iommu_domain *slave_domain = NULL;
	struct msm_iommu_domain *ap_msm, *slave_msm;
	struct arm_smmu_domain *ap_smmu, *slave_smmu;
	struct av8l_fast_io_pgtable *ap_data, *slave_data;
	int ret = 0;

	ap_dev = find_ipa_smmu_dev(":ipa_smmu_ap");
	slave_dev = find_ipa_smmu_dev(slave_suffix);

	if (!ap_dev || !slave_dev) {
		pr_warn("[%s] PMDS fix skip: ap=%p slave=%p suffix=%s\n",
			DRV_NAME, ap_dev, slave_dev, slave_suffix);
		ret = -ENODEV;
		goto out;
	}

	ap_domain = iommu_get_domain_for_dev(ap_dev);
	slave_domain = iommu_get_domain_for_dev(slave_dev);
	if (!ap_domain || !slave_domain) {
		pr_warn("[%s] PMDS fix skip: domain missing for %s\n",
			DRV_NAME, slave_suffix);
		ret = -ENODEV;
		goto out;
	}

	ap_msm = to_msm_iommu_domain(ap_domain);
	slave_msm = to_msm_iommu_domain(slave_domain);
	ap_smmu = container_of(ap_msm, struct arm_smmu_domain, domain);
	slave_smmu = container_of(slave_msm, struct arm_smmu_domain, domain);

	if (!ap_smmu->pgtbl_ops[0] || !slave_smmu->pgtbl_ops[0]) {
		pr_warn("[%s] PMDS fix skip: missing pgtbl_ops for %s\n",
			DRV_NAME, slave_suffix);
		ret = -EINVAL;
		goto out;
	}

	ap_data = iof_pgtable_ops_to_data(ap_smmu->pgtbl_ops[0]);
	slave_data = iof_pgtable_ops_to_data(slave_smmu->pgtbl_ops[0]);

	{
		void *ap_norm = normalize_pmds_ptr(ap_data->pmds);
		void *slave_norm = normalize_pmds_ptr(slave_data->pmds);

		pr_info("[%s] PMDS %s: ap_raw=%p ap_norm=%p slave_raw=%p slave_norm=%p\n",
			DRV_NAME, slave_suffix, ap_data->pmds, ap_norm,
			slave_data->pmds, slave_norm);

		if (!ap_norm || !is_vmalloc_addr(ap_norm)) {
			pr_warn("[%s] PMDS fix skip: normalized ap pmds not vmalloc (%p)\n",
				DRV_NAME, ap_norm);
			ret = -EINVAL;
			goto out;
		}

		if (slave_norm && is_vmalloc_addr(slave_norm) && slave_norm == ap_norm)
			goto out;

		pr_warn("[%s] PMDS fix apply on %s: %p -> %p\n",
			DRV_NAME, slave_suffix, slave_data->pmds, ap_norm);

		slave_data->pmds = ap_norm;
		slave_data->base = ap_data->base;
		slave_data->end = ap_data->end;
	}

out:
	if (ap_dev)
		put_device(ap_dev);
	if (slave_dev)
		put_device(slave_dev);
	return ret;
}

static void fix_ipa_smmu_pmds_v20(void)
{
	fix_one_ipa_smmu_pmds(":ipa_smmu_eth");
	fix_one_ipa_smmu_pmds(":ipa_smmu_eth1");
}

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
	/* Keep rings page-friendly for IPA SMMU mapping */
	prop_prepend(np, prop_build_u32("qcom,ring-size", 1024));
	prop_prepend(np, prop_build_u32("qcom,buff-size", 4096));
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

static int __init pcie_enabler_v20_init(void)
{
	unsigned long flags;
	struct device_node *pcie_node = NULL;
	struct device_node *rp_node = NULL;
	struct device_node *eth_node = NULL;
	struct property *status_pp;
	struct property *rp_addr_cells_pp = NULL;
	struct property *rp_size_cells_pp = NULL;
	static char status_okay[] = "okay";

	pr_info("[%s] init: OF/PCI + page-aligned channels + PMDS fix\n", DRV_NAME);

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
	if (!pcie_pdev)
		pr_warn("[%s] of_platform_device_create() failed (continuing)\n", DRV_NAME);
	else
		pr_info("[%s] RC platform device created\n", DRV_NAME);

	msleep(700);
	debug_dump_rtl8125_of_binding();
	fix_ipa_smmu_pmds_v20();

	return 0;
}

static void __exit pcie_enabler_v20_exit(void)
{
	if (pcie_pdev)
		of_device_unregister(pcie_pdev);

	pr_info("[%s] exit: OF surgery not reverted\n", DRV_NAME);
}

module_init(pcie_enabler_v20_init);
module_exit(pcie_enabler_v20_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("OpenAI / Manuel lab");
MODULE_DESCRIPTION("T99W373 PCIe V20 explicit 4K PMDS alignment + OF channels");
