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

#define DRV_NAME        "pcie_enabler_stepdiag"
#define PCIE_RC_PATH    "/soc/qcom,pcie@1c00000"
#define PCIE_RC_COMPAT  "qcom,pci-msm"

#define T99_IOSS_PHANDLE        0x123
#define T99_IOMMU_GRP_PHANDLE   0x12a
#define T99_APPS_SMMU_PHANDLE   0x34
#define T99_PCIE_EP_RID         0x100
#define T99_PCIE_RC0_SID        0x200
#define T99_IPA_ETH_SID         0x5e3

#define ETH_FULL_PATH   "/soc/qcom,pcie@1c00000/pcie0_rp/realtek,rtl8125@0,0"
#define CH_RX_FULL_PATH ETH_FULL_PATH "/channel-rx"
#define CH_TX_FULL_PATH ETH_FULL_PATH "/channel-tx"

#define CH_RX_PHANDLE   0x81250001
#define CH_TX_PHANDLE   0x81250002

static struct platform_device *pcie_pdev;

/* Step switches */
static bool enable_status = true;
module_param(enable_status, bool, 0644);
MODULE_PARM_DESC(enable_status, "Set RC status=okay");

static bool enable_eth_node;
module_param(enable_eth_node, bool, 0644);
MODULE_PARM_DESC(enable_eth_node, "Inject runtime RTL8125 child node");

static bool enable_channels;
module_param(enable_channels, bool, 0644);
MODULE_PARM_DESC(enable_channels, "Inject channel-rx/channel-tx and ioss_channels");

static bool enable_iommu_group_prop;
module_param(enable_iommu_group_prop, bool, 0644);
MODULE_PARM_DESC(enable_iommu_group_prop, "Inject qcom,iommu-group property in RTL node");

static bool enable_iommu_map_override;
module_param(enable_iommu_map_override, bool, 0644);
MODULE_PARM_DESC(enable_iommu_map_override, "Override RC iommu-map RID 0x100 -> SID 0x5e3");

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

static int set_node_property(struct device_node *np, struct property *prop)
{
	if (!np || !prop)
		return -EINVAL;

	if (of_find_property(np, prop->name, NULL))
		return of_update_property(np, prop);

	return of_add_property(np, prop);
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

	np->phandle = phandle;
	of_node_set_flag(np, OF_DYNAMIC);
	of_node_set_flag(np, OF_DETACHED);
	np->name = kstrdup(name, GFP_KERNEL);
	np->full_name = kstrdup(full_name, GFP_KERNEL);
	if (!np->name || !np->full_name)
		goto err;

	prop_prepend(np, prop_build_string("name", name));
	prop_prepend(np, prop_build_u32("phandle", phandle));
	prop_prepend(np, prop_build_u32("linux,phandle", phandle));
	prop_prepend(np, prop_build_u32("qcom,ring-size", 1024));
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

static struct device_node *build_eth_node(void)
{
	static const char pci_ids[] = "10ec:8125" "\0" "10ec:3000";
	static const __be32 ioss_channels[] = {
		cpu_to_be32(CH_RX_PHANDLE),
		cpu_to_be32(CH_TX_PHANDLE),
	};
	__be32 reg_data[5] = { 0, 0, 0, 0, 0 };
	__be32 ioss_be = cpu_to_be32(T99_IOSS_PHANDLE);
	struct device_node *eth_np;

	eth_np = kzalloc(sizeof(*eth_np), GFP_KERNEL);
	if (!eth_np)
		return NULL;

	of_node_set_flag(eth_np, OF_DYNAMIC);
	of_node_set_flag(eth_np, OF_DETACHED);
	eth_np->name = kstrdup("realtek,rtl8125", GFP_KERNEL);
	eth_np->full_name = kstrdup(ETH_FULL_PATH, GFP_KERNEL);
	if (!eth_np->name || !eth_np->full_name)
		goto err;

	if (enable_channels) {
		struct device_node *ch_rx;
		struct device_node *ch_tx;

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
		prop_prepend(eth_np, prop_build_raw("qcom,ioss_channels",
						    ioss_channels, sizeof(ioss_channels)));
	}

	prop_prepend(eth_np, prop_build_raw("pci-ids", pci_ids, sizeof(pci_ids)));
	prop_prepend(eth_np, prop_build_u32("#size-cells", 0x01));
	prop_prepend(eth_np, prop_build_u32("#address-cells", 0x01));
	prop_prepend(eth_np, prop_build_u32("qcom,ioss_instance", 0x00));
	if (enable_iommu_group_prop) {
		__be32 iommu_be = cpu_to_be32(T99_IOMMU_GRP_PHANDLE);
		prop_prepend(eth_np, prop_build_raw("qcom,iommu-group",
						    &iommu_be, sizeof(iommu_be)));
	}
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

static int __init pcie_enabler_stepdiag_init(void)
{
	struct device_node *pcie_node = NULL;
	struct device_node *rp_node = NULL;
	struct device_node *eth_node = NULL;
	struct property *status_pp = NULL;
	struct property *rp_addr_cells_pp = NULL;
	struct property *rp_size_cells_pp = NULL;
	struct property *iommu_map_pp = NULL;
	static const __be32 iommu_map_override[] = {
		cpu_to_be32(0x0),
		cpu_to_be32(T99_APPS_SMMU_PHANDLE),
		cpu_to_be32(T99_PCIE_RC0_SID),
		cpu_to_be32(0x1),
		cpu_to_be32(T99_PCIE_EP_RID),
		cpu_to_be32(T99_APPS_SMMU_PHANDLE),
		cpu_to_be32(T99_IPA_ETH_SID),
		cpu_to_be32(0x1),
	};

	pr_info("[%s] init: status=%d eth=%d ch=%d iommu_group=%d iommu_map=%d\n",
		DRV_NAME, enable_status, enable_eth_node, enable_channels,
		enable_iommu_group_prop, enable_iommu_map_override);

	pcie_node = of_find_node_by_path(PCIE_RC_PATH);
	if (!pcie_node)
		pcie_node = of_find_compatible_node(NULL, NULL, PCIE_RC_COMPAT);
	if (!pcie_node) {
		pr_err("[%s] PCIe RC node not found\n", DRV_NAME);
		return -ENODEV;
	}

	rp_node = find_child_by_name(pcie_node, "pcie0_rp");
	if (!rp_node) {
		pr_err("[%s] pcie0_rp not found\n", DRV_NAME);
		goto out_put;
	}

	if (enable_eth_node) {
		struct device_node *existing = find_child_by_name(rp_node, "realtek,rtl8125");
		if (existing) {
			pr_warn("[%s] RTL8125 child already present under pcie0_rp\n", DRV_NAME);
		} else {
			eth_node = build_eth_node();
			if (!eth_node) {
				pr_err("[%s] Failed to build runtime RTL8125 node\n", DRV_NAME);
				goto out_put;
			}
		}
	}

	if (enable_iommu_map_override) {
		iommu_map_pp = prop_build_raw("iommu-map", iommu_map_override,
					      sizeof(iommu_map_override));
	}

	if (enable_status) {
		status_pp = prop_build_string("status", "okay");
		if (!status_pp || set_node_property(pcie_node, status_pp))
			pr_warn("[%s] failed to set RC status property\n", DRV_NAME);
	}

	if (iommu_map_pp && set_node_property(pcie_node, iommu_map_pp))
		pr_warn("[%s] failed to set iommu-map override\n", DRV_NAME);

	rp_addr_cells_pp = prop_build_u32("#address-cells", 3);
	rp_size_cells_pp = prop_build_u32("#size-cells", 2);
	if (rp_addr_cells_pp && set_node_property(rp_node, rp_addr_cells_pp))
		pr_warn("[%s] failed to set #address-cells on pcie0_rp\n", DRV_NAME);
	if (rp_size_cells_pp && set_node_property(rp_node, rp_size_cells_pp))
		pr_warn("[%s] failed to set #size-cells on pcie0_rp\n", DRV_NAME);

	if (eth_node) {
		int ret;
		struct device_node *child = eth_node->child;
		struct device_node *next;

		/* Attach parent first, then children via OF dynamic API. */
		eth_node->child = NULL;
		eth_node->parent = rp_node;
		ret = of_attach_node(eth_node);
		if (ret)
			pr_warn("[%s] failed to attach eth node: %d\n", DRV_NAME, ret);

		while (child) {
			next = child->sibling;
			child->sibling = NULL;
			child->parent = eth_node;
			ret = of_attach_node(child);
			if (ret)
				pr_warn("[%s] failed to attach child node %s: %d\n",
					DRV_NAME, child->name ? child->name : "?", ret);
			child = next;
		}
	}

	pcie_pdev = of_platform_device_create(pcie_node, NULL, NULL);
	if (!pcie_pdev)
		pr_warn("[%s] of_platform_device_create() failed (device may already exist)\n", DRV_NAME);
	else
		pr_info("[%s] RC platform device created\n", DRV_NAME);

out_put:
	of_node_put(pcie_node);
	return 0;
}

static void __exit pcie_enabler_stepdiag_exit(void)
{
	if (pcie_pdev)
		of_device_unregister(pcie_pdev);
}

module_init(pcie_enabler_stepdiag_init);
module_exit(pcie_enabler_stepdiag_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("OpenAI / Manuel lab");
MODULE_DESCRIPTION("PCIe step-by-step diagnostic enabler");
