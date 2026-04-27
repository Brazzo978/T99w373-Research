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
#include <linux/kprobes.h>
#include <linux/kobject.h>

#define DRV_NAME        "pcie_enabler_v6_3"
#define PCIE_RC_PATH    "/soc/qcom,pcie@1c00000"
#define PCIE_RC_COMPAT  "qcom,pci-msm"

#define RTL8125_VENDOR  0x10ec
#define RTL8125_DEVICE  0x8125

typedef int (*of_attach_node_t)(struct device_node *np);

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

/* ---------------- symbol resolution helpers ---------------- */

static void *resolve_symbol_via_kprobe(const char *name)
{
	struct kprobe kp = {
		.symbol_name = name,
	};
	void *addr;
	int ret;

	ret = register_kprobe(&kp);
	if (ret) {
		pr_err("[%s] failed to resolve %s via kprobe: %d\n",
		       DRV_NAME, name, ret);
		return NULL;
	}

	addr = kp.addr;
	unregister_kprobe(&kp);

	pr_info("[%s] resolved %s at %p\n", DRV_NAME, name, addr);
	return addr;
}

static int init_dynamic_of_node(struct device_node *np)
{
	static struct kobj_type *of_node_ktype_ptr;
	static const struct fwnode_operations *of_fwnode_ops_ptr;

	if (!np)
		return -EINVAL;

	if (!of_node_ktype_ptr)
		of_node_ktype_ptr = resolve_symbol_via_kprobe("of_node_ktype");
	if (!of_fwnode_ops_ptr)
		of_fwnode_ops_ptr = resolve_symbol_via_kprobe("of_fwnode_ops");

	if (!of_node_ktype_ptr || !of_fwnode_ops_ptr)
		return -ENOENT;

	of_node_set_flag(np, OF_DYNAMIC);
	of_node_set_flag(np, OF_DETACHED);
	kobject_init(&np->kobj, of_node_ktype_ptr);
	np->fwnode.ops = of_fwnode_ops_ptr;
	return 0;
}

static int attach_node_runtime(struct device_node *np)
{
	static of_attach_node_t of_attach_node_ptr;

	if (!of_attach_node_ptr)
		of_attach_node_ptr = (of_attach_node_t)resolve_symbol_via_kprobe("of_attach_node");

	if (!of_attach_node_ptr)
		return -ENOENT;

	return of_attach_node_ptr(np);
}

/* ---------------- node builder ---------------- */

static struct device_node *build_eth_test_node_v63(void)
{
	static const char pci_ids[] = "10ec:8125" "\0" "10ec:3000";
	static const char abs_full_name[] =
		"/soc/qcom,pcie@1c00000/pcie0_rp/realtek,rtl8125@0,0";
	__be32 reg_data[5] = { 0, 0, 0, 0, 0 };
	struct device_node *np;

	np = kzalloc(sizeof(*np), GFP_KERNEL);
	if (!np)
		return NULL;

	np->name = kstrdup("realtek,rtl8125", GFP_KERNEL);
	np->full_name = kstrdup(abs_full_name, GFP_KERNEL);
	if (!np->name || !np->full_name)
		goto err;

	/*
	 * of_attach_node() refreshes np->name from the "name" property
	 * on this OEM kernel, so add it explicitly.
	 */
	prop_prepend(np, prop_build_string("name", "realtek,rtl8125"));
	prop_prepend(np, prop_build_raw("pci-ids", pci_ids, sizeof(pci_ids)));
	prop_prepend(np, prop_build_u32("#size-cells", 0x01));
	prop_prepend(np, prop_build_u32("#address-cells", 0x01));
	prop_prepend(np, prop_build_string("compatible", "qcom,ioss-v2-device"));
	prop_prepend(np, prop_build_raw("reg", reg_data, sizeof(reg_data)));

	if (!np->properties)
		goto err;

	return np;

err:
	free_prepared_node(np);
	return NULL;
}

/* ---------------- debug helpers ---------------- */

static void debug_dump_rtl8125_of_binding(void)
{
	int i;
	struct pci_dev *pdev = NULL;

	for (i = 0; i < 20; i++) {
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

static int __init pcie_enabler_v6_3_init(void)
{
	unsigned long flags;
	struct device_node *pcie_node = NULL;
	struct device_node *rp_node = NULL;
	struct device_node *eth_node = NULL;
	struct property *status_pp;
	struct property *rp_addr_cells_pp = NULL;
	struct property *rp_size_cells_pp = NULL;
	static char status_okay[] = "okay";
	int ret;

	pr_info("[%s] init: minimal OF/PCI binding test v6.3\n", DRV_NAME);

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
		eth_node = build_eth_test_node_v63();
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

	local_irq_restore(flags);
	preempt_enable();

	if (eth_node) {
		eth_node->parent = rp_node;
		ret = init_dynamic_of_node(eth_node);
		if (ret) {
			pr_err("[%s] init_dynamic_of_node() failed: %d\n", DRV_NAME, ret);
			free_prepared_node(eth_node);
			return ret;
		}

		ret = attach_node_runtime(eth_node);
		if (ret) {
			pr_err("[%s] attach_node_runtime() failed: %d\n", DRV_NAME, ret);
			return ret;
		}
	}

	pr_info("[%s] RC enabled, Root Port prepared, RTL8125 child attached\n",
		DRV_NAME);

	pcie_pdev = of_platform_device_create(pcie_node, NULL, NULL);
	if (!pcie_pdev) {
		pr_err("[%s] of_platform_device_create() failed\n", DRV_NAME);
		return -ENODEV;
	}

	pr_info("[%s] RC platform device created\n", DRV_NAME);

	msleep(500);
	debug_dump_rtl8125_of_binding();

	return 0;
}

static void __exit pcie_enabler_v6_3_exit(void)
{
	if (pcie_pdev)
		of_device_unregister(pcie_pdev);

	pr_info("[%s] exit: OF surgery not reverted\n", DRV_NAME);
}

module_init(pcie_enabler_v6_3_init);
module_exit(pcie_enabler_v6_3_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("OpenAI / Manuel lab");
MODULE_DESCRIPTION("T99W373 PCIe V6.3 kprobe-assisted OF attach test");
