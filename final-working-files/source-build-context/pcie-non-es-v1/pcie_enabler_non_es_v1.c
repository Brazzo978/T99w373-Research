/*
 * pcie_enabler_non_es_v1.c
 *
 * NON-ES secure-boot compatible runtime enabler:
 * - reuse the original runtime PCIe RC + RTL8125/IOSS DT injection flow
 * - port the ES-stable V22 IPA SMMU PMDS fix into the same loadable module
 * - avoid boot.img modification
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/iommu.h>
#include <linux/io-pgtable.h>
#include <linux/io-pgtable-fast.h>
#include <linux/vmalloc.h>
#include <linux/string.h>
#include <linux/workqueue.h>
#include "arm-smmu.h"

#define DRV_NAME "pcie_enabler_non_es_v1"
#define PCIE_RC_PATH "/soc/qcom,pcie@1c00000"
#define SOC_PATH "/soc"
#define IOSS_ROOT_PATH "/soc/qcom,ioss"
#define IPA_ETH_GROUP1_PATH "/soc/ipa_eth_group1"

#define PCIE_COMPAT_1 "qcom,pci-msm"
#define PCIE_COMPAT_2 "qcom,pcie-sdx65"
#define PCIE_COMPAT_3 "qcom,pcie-sdx62"
#define PCIE_COMPAT_4 "snps,dw-pcie"

#define RTL8125_PHANDLE_RX 0x8125
#define RTL8125_PHANDLE_TX 0x8126

#define PMDS_RECHECK_MS 2000
#define PMDS_RECHECK_MAX 15

static unsigned long kallsyms_lookup_name_addr;
module_param_named(kallsyms_lookup_name_addr, kallsyms_lookup_name_addr, ulong, 0400);
MODULE_PARM_DESC(kallsyms_lookup_name_addr,
		 "Address of kallsyms_lookup_name from /proc/kallsyms");

typedef unsigned long (*kallsyms_lookup_name_t)(const char *name);

struct pcie_runtime_symbols {
	kallsyms_lookup_name_t lookup;
	struct mutex *of_mutex;
	raw_spinlock_t *devtree_lock;
	int (*of_add_property)(struct device_node *np, struct property *prop);
	int (*of_update_property)(struct device_node *np, struct property *newprop);
};

struct smmu_dev_search {
	const char *suffix;
	struct device *found;
};

static struct device_node *pcie_node;
static struct platform_device *pcie_pdev;
static struct pcie_runtime_symbols pcie_syms;
static struct delayed_work pmds_recheck_work;
static int pmds_recheck_count;

static struct property *pcie_alloc_property(const char *name,
					    const void *value, int length)
{
	struct property *prop;

	prop = kzalloc(sizeof(*prop), GFP_KERNEL);
	if (!prop)
		return NULL;

	prop->name = kstrdup(name, GFP_KERNEL);
	if (!prop->name)
		goto err_prop;

	prop->length = length;
	if (length > 0) {
		prop->value = kmemdup(value, length, GFP_KERNEL);
		if (!prop->value)
			goto err_name;
	}

	return prop;

err_name:
	kfree(prop->name);
err_prop:
	kfree(prop);
	return NULL;
}

static struct property *pcie_alloc_bool_property(const char *name)
{
	return pcie_alloc_property(name, NULL, 0);
}

static struct property *pcie_alloc_string_property(const char *name,
						   const char *value)
{
	return pcie_alloc_property(name, value, strlen(value) + 1);
}

static struct property *pcie_alloc_u32_property(const char *name, u32 value)
{
	__be32 be = cpu_to_be32(value);

	return pcie_alloc_property(name, &be, sizeof(be));
}

static struct property *pcie_alloc_u32_array_property(const char *name,
						      const u32 *values,
						      size_t count)
{
	size_t i;
	__be32 *be_values;
	struct property *prop;

	be_values = kcalloc(count, sizeof(*be_values), GFP_KERNEL);
	if (!be_values)
		return NULL;

	for (i = 0; i < count; i++)
		be_values[i] = cpu_to_be32(values[i]);

	prop = pcie_alloc_property(name, be_values, count * sizeof(*be_values));
	kfree(be_values);

	return prop;
}

static struct property *pcie_alloc_string_list_property(const char *name,
							const char * const *values,
							size_t count)
{
	size_t i;
	size_t total = 0;
	size_t offset = 0;
	char *buf;
	struct property *prop;

	for (i = 0; i < count; i++)
		total += strlen(values[i]) + 1;

	buf = kzalloc(total, GFP_KERNEL);
	if (!buf)
		return NULL;

	for (i = 0; i < count; i++) {
		size_t len = strlen(values[i]) + 1;

		memcpy(buf + offset, values[i], len);
		offset += len;
	}

	prop = pcie_alloc_property(name, buf, total);
	kfree(buf);

	return prop;
}

static int pcie_set_property(struct device_node *node, struct property *prop)
{
	if (!prop)
		return -ENOMEM;

	if (!pcie_syms.of_add_property || !pcie_syms.of_update_property)
		return -ENOENT;

	if (of_find_property(node, prop->name, NULL))
		return pcie_syms.of_update_property(node, prop);

	return pcie_syms.of_add_property(node, prop);
}

static int pcie_resolve_symbols(void)
{
	if (!kallsyms_lookup_name_addr) {
		pr_err("[%s] missing kallsyms_lookup_name_addr module param\n", DRV_NAME);
		return -EINVAL;
	}

	pcie_syms.lookup = (kallsyms_lookup_name_t)kallsyms_lookup_name_addr;
	pcie_syms.of_mutex = (struct mutex *)pcie_syms.lookup("of_mutex");
	pcie_syms.devtree_lock = (raw_spinlock_t *)pcie_syms.lookup("devtree_lock");
	pcie_syms.of_add_property =
		(void *)pcie_syms.lookup("of_add_property");
	pcie_syms.of_update_property =
		(void *)pcie_syms.lookup("of_update_property");

	if (!pcie_syms.of_mutex || !pcie_syms.devtree_lock ||
	    !pcie_syms.of_add_property || !pcie_syms.of_update_property) {
		pr_err("[%s] failed to resolve required OF symbols\n", DRV_NAME);
		return -ENOENT;
	}

	return 0;
}

static struct device_node *pcie_alloc_node(struct device_node *parent,
					   const char *name,
					   const char *unit_name,
					   u32 phandle)
{
	struct device_node *node;

	node = kzalloc(sizeof(*node), GFP_KERNEL);
	if (!node)
		return NULL;

	node->name = kstrdup(name, GFP_KERNEL);
	if (!node->name)
		goto err_node;

	node->full_name = kasprintf(GFP_KERNEL, "%s/%s",
				    parent->full_name, unit_name);
	if (!node->full_name)
		goto err_name;

	node->parent = parent;
	node->phandle = phandle;
	node->fwnode.ops = &of_fwnode_ops;
	of_node_set_flag(node, OF_DYNAMIC);
	of_node_set_flag(node, OF_DETACHED);

	return node;

err_name:
	kfree(node->name);
err_node:
	kfree(node);
	return NULL;
}

static int pcie_attach_node(struct device_node *node)
{
	unsigned long flags;

	if (!node || !node->parent)
		return -EINVAL;

	mutex_lock(pcie_syms.of_mutex);
	raw_spin_lock_irqsave(pcie_syms.devtree_lock, flags);
	node->child = NULL;
	node->sibling = node->parent->child;
	node->parent->child = node;
	of_node_clear_flag(node, OF_DETACHED);
	raw_spin_unlock_irqrestore(pcie_syms.devtree_lock, flags);
	mutex_unlock(pcie_syms.of_mutex);

	return 0;
}

static struct device_node *pcie_find_child_by_name(struct device_node *parent,
						   const char *name)
{
	struct device_node *child;

	for_each_child_of_node(parent, child) {
		if (!strcmp(child->name, name))
			return child;
	}

	return NULL;
}

static int pcie_attach_runtime_nodes(void)
{
	int ret;
	const char *pci_ids[] = { "10ec:8125", "10ec:3000" };
	u32 reg[5] = { 0, 0, 0, 0, 0 };
	u32 ioss_channels[2] = { RTL8125_PHANDLE_RX, RTL8125_PHANDLE_TX };
	struct device_node *soc_node;
	struct device_node *ioss_root;
	struct device_node *iommu_group;
	struct device_node *rp_node;
	struct device_node *rx_node;
	struct device_node *tx_node;
	struct device_node *rtl8125_node;

	soc_node = of_find_node_by_path(SOC_PATH);
	ioss_root = of_find_node_by_path(IOSS_ROOT_PATH);
	iommu_group = of_find_node_by_path(IPA_ETH_GROUP1_PATH);
	rp_node = pcie_find_child_by_name(pcie_node, "pcie0_rp");

	if (!soc_node || !ioss_root || !iommu_group || !rp_node) {
		pr_err("[%s] missing DT deps: soc=%p ioss=%p iommu=%p rp=%p\n",
		       DRV_NAME, soc_node, ioss_root, iommu_group, rp_node);
		ret = -ENODEV;
		goto out_put_nodes;
	}

	if (pcie_find_child_by_name(soc_node, "r8125_rx") &&
	    pcie_find_child_by_name(soc_node, "r8125_tx") &&
	    pcie_find_child_by_name(rp_node, "realtek,rtl8125")) {
		pr_info("[%s] runtime DT nodes already present\n", DRV_NAME);
		ret = 0;
		goto out_put_nodes;
	}

	ret = pcie_set_property(pcie_node,
				pcie_alloc_string_property("status", "okay"));
	if (ret)
		goto out_put_nodes;

	rx_node = pcie_alloc_node(soc_node, "r8125_rx", "r8125_rx@eth0",
				  RTL8125_PHANDLE_RX);
	tx_node = pcie_alloc_node(soc_node, "r8125_tx", "r8125_tx@eth0",
				  RTL8125_PHANDLE_TX);
	rtl8125_node = pcie_alloc_node(rp_node, "realtek,rtl8125",
				       "realtek,rtl8125@0,0", 0);
	if (!rx_node || !tx_node || !rtl8125_node) {
		ret = -ENOMEM;
		goto out_put_nodes;
	}

	ret = pcie_set_property(rx_node,
				pcie_alloc_u32_property("phandle", RTL8125_PHANDLE_RX));
	if (ret)
		goto out_put_nodes;
	ret = pcie_set_property(rx_node,
				pcie_alloc_u32_property("linux,phandle", RTL8125_PHANDLE_RX));
	if (ret)
		goto out_put_nodes;
	ret = pcie_set_property(rx_node,
				pcie_alloc_string_property("name", "r8125_rx"));
	if (ret)
		goto out_put_nodes;
	ret = pcie_set_property(rx_node,
				pcie_alloc_bool_property("qcom,dir-rx"));
	if (ret)
		goto out_put_nodes;
	ret = pcie_set_property(rx_node,
				pcie_alloc_bool_property("qcom,rx-filter-ip"));
	if (ret)
		goto out_put_nodes;
	ret = pcie_set_property(rx_node,
				pcie_alloc_u32_property("qcom,ring-size", 1024));
	if (ret)
		goto out_put_nodes;
	ret = pcie_set_property(rx_node,
				pcie_alloc_u32_property("qcom,buff-size", 2048));
	if (ret)
		goto out_put_nodes;
	ret = pcie_set_property(rx_node,
				pcie_alloc_u32_property("qcom,mod-count-min", 32));
	if (ret)
		goto out_put_nodes;
	ret = pcie_set_property(rx_node,
				pcie_alloc_u32_property("qcom,mod-count-max", 64));
	if (ret)
		goto out_put_nodes;
	ret = pcie_set_property(rx_node,
				pcie_alloc_u32_property("qcom,mod-usecs-min", 30));
	if (ret)
		goto out_put_nodes;
	ret = pcie_set_property(rx_node,
				pcie_alloc_u32_property("qcom,mod-usecs-max", 60));
	if (ret)
		goto out_put_nodes;

	ret = pcie_set_property(tx_node,
				pcie_alloc_u32_property("phandle", RTL8125_PHANDLE_TX));
	if (ret)
		goto out_put_nodes;
	ret = pcie_set_property(tx_node,
				pcie_alloc_u32_property("linux,phandle", RTL8125_PHANDLE_TX));
	if (ret)
		goto out_put_nodes;
	ret = pcie_set_property(tx_node,
				pcie_alloc_string_property("name", "r8125_tx"));
	if (ret)
		goto out_put_nodes;
	ret = pcie_set_property(tx_node,
				pcie_alloc_bool_property("qcom,dir-tx"));
	if (ret)
		goto out_put_nodes;
	ret = pcie_set_property(tx_node,
				pcie_alloc_u32_property("qcom,ring-size", 1024));
	if (ret)
		goto out_put_nodes;
	ret = pcie_set_property(tx_node,
				pcie_alloc_u32_property("qcom,buff-size", 2048));
	if (ret)
		goto out_put_nodes;
	ret = pcie_set_property(tx_node,
				pcie_alloc_u32_property("qcom,mod-count-min", 32));
	if (ret)
		goto out_put_nodes;
	ret = pcie_set_property(tx_node,
				pcie_alloc_u32_property("qcom,mod-count-max", 64));
	if (ret)
		goto out_put_nodes;
	ret = pcie_set_property(tx_node,
				pcie_alloc_u32_property("qcom,mod-usecs-min", 30));
	if (ret)
		goto out_put_nodes;
	ret = pcie_set_property(tx_node,
				pcie_alloc_u32_property("qcom,mod-usecs-max", 60));
	if (ret)
		goto out_put_nodes;

	ret = pcie_set_property(rtl8125_node,
				pcie_alloc_string_property("compatible",
							   "qcom,ioss-v2-device"));
	if (ret)
		goto out_put_nodes;
	ret = pcie_set_property(rtl8125_node,
				pcie_alloc_string_property("name", "realtek,rtl8125"));
	if (ret)
		goto out_put_nodes;
	ret = pcie_set_property(rtl8125_node,
				pcie_alloc_u32_array_property("reg", reg, ARRAY_SIZE(reg)));
	if (ret)
		goto out_put_nodes;
	ret = pcie_set_property(rtl8125_node,
				pcie_alloc_string_list_property("pci-ids", pci_ids,
								ARRAY_SIZE(pci_ids)));
	if (ret)
		goto out_put_nodes;
	ret = pcie_set_property(rtl8125_node,
				pcie_alloc_u32_property("qcom,ioss_instance", 0));
	if (ret)
		goto out_put_nodes;
	ret = pcie_set_property(rtl8125_node,
				pcie_alloc_u32_property("qcom,ioss", ioss_root->phandle));
	if (ret)
		goto out_put_nodes;
	ret = pcie_set_property(rtl8125_node,
				pcie_alloc_u32_property("qcom,iommu-group",
							iommu_group->phandle));
	if (ret)
		goto out_put_nodes;
	ret = pcie_set_property(rtl8125_node,
				pcie_alloc_u32_array_property("qcom,ioss_channels",
							      ioss_channels,
							      ARRAY_SIZE(ioss_channels)));
	if (ret)
		goto out_put_nodes;

	ret = pcie_attach_node(rx_node);
	if (ret)
		goto out_put_nodes;
	ret = pcie_attach_node(tx_node);
	if (ret)
		goto out_put_nodes;
	ret = pcie_attach_node(rtl8125_node);
	if (ret)
		goto out_put_nodes;

	pr_info("[%s] runtime DT nodes attached for RTL8125 IPA bindings\n", DRV_NAME);
	ret = 0;

out_put_nodes:
	of_node_put(rp_node);
	of_node_put(iommu_group);
	of_node_put(ioss_root);
	of_node_put(soc_node);
	return ret;
}

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

static struct device *find_ipa_smmu_dev(const char *name_suffix)
{
	struct smmu_dev_search ctx = { .suffix = name_suffix, .found = NULL };

	bus_for_each_dev(&platform_bus_type, NULL, &ctx, smmu_dev_match);
	return ctx.found;
}

static int fix_one_slave_pmds(const char *slave_suffix)
{
	int fixed = 0;
	struct device *ap_dev = NULL;
	struct device *slave_dev = NULL;
	struct iommu_domain *ap_domain = NULL;
	struct iommu_domain *slave_domain = NULL;
	struct msm_iommu_domain *ap_msm, *slave_msm;
	struct arm_smmu_domain *ap_smmu, *slave_smmu;
	struct av8l_fast_io_pgtable *ap_data, *slave_data;

	ap_dev = find_ipa_smmu_dev(":ipa_smmu_ap");
	slave_dev = find_ipa_smmu_dev(slave_suffix);
	if (!ap_dev || !slave_dev) {
		pr_warn("[%s] missing SMMU dev: ap=%p slave=%p (%s)\n",
			DRV_NAME, ap_dev, slave_dev, slave_suffix);
		goto out;
	}

	ap_domain = iommu_get_domain_for_dev(ap_dev);
	slave_domain = iommu_get_domain_for_dev(slave_dev);
	if (!ap_domain || !slave_domain) {
		pr_warn("[%s] missing domain for %s\n", DRV_NAME, slave_suffix);
		goto out;
	}

	ap_msm = to_msm_iommu_domain(ap_domain);
	slave_msm = to_msm_iommu_domain(slave_domain);
	ap_smmu = container_of(ap_msm, struct arm_smmu_domain, domain);
	slave_smmu = container_of(slave_msm, struct arm_smmu_domain, domain);

	if (!ap_smmu->pgtbl_ops[0] || !slave_smmu->pgtbl_ops[0]) {
		pr_warn("[%s] missing pgtbl_ops for %s\n", DRV_NAME, slave_suffix);
		goto out;
	}

	ap_data = iof_pgtable_ops_to_data(ap_smmu->pgtbl_ops[0]);
	slave_data = iof_pgtable_ops_to_data(slave_smmu->pgtbl_ops[0]);

	pr_info("[%s] %s pre: ap pmds=%p base=0x%llx end=0x%llx | slave pmds=%p base=0x%llx end=0x%llx\n",
		DRV_NAME, slave_suffix,
		ap_data->pmds, (unsigned long long)ap_data->base,
		(unsigned long long)ap_data->end,
		slave_data->pmds, (unsigned long long)slave_data->base,
		(unsigned long long)slave_data->end);

	if (!is_vmalloc_addr(ap_data->pmds)) {
		pr_warn("[%s] ap pmds not vmalloc (%p), skip %s\n",
			DRV_NAME, ap_data->pmds, slave_suffix);
		goto out;
	}

	if (slave_data->pmds != ap_data->pmds ||
	    slave_data->base != ap_data->base ||
	    slave_data->end != ap_data->end) {
		slave_data->pmds = ap_data->pmds;
		slave_data->base = ap_data->base;
		slave_data->end = ap_data->end;
		fixed = 1;
		pr_warn("[%s] %s fixed -> pmds=%p base=0x%llx end=0x%llx\n",
			DRV_NAME, slave_suffix,
			slave_data->pmds,
			(unsigned long long)slave_data->base,
			(unsigned long long)slave_data->end);
	}

out:
	if (ap_dev)
		put_device(ap_dev);
	if (slave_dev)
		put_device(slave_dev);
	return fixed;
}

static void run_pmds_fix_pass(void)
{
	int changed = 0;

	changed |= fix_one_slave_pmds(":ipa_smmu_eth");
	changed |= fix_one_slave_pmds(":ipa_smmu_eth1");

	if (!changed)
		pr_info("[%s] PMDS fix pass: no changes needed\n", DRV_NAME);
}

static void pmds_recheck_fn(struct work_struct *work)
{
	run_pmds_fix_pass();

	pmds_recheck_count++;
	if (pmds_recheck_count < PMDS_RECHECK_MAX)
		schedule_delayed_work(&pmds_recheck_work,
					      msecs_to_jiffies(PMDS_RECHECK_MS));
}

static void start_pmds_recheck(void)
{
	INIT_DELAYED_WORK(&pmds_recheck_work, pmds_recheck_fn);
	pmds_recheck_count = 0;
	schedule_delayed_work(&pmds_recheck_work,
			      msecs_to_jiffies(PMDS_RECHECK_MS));
}

static int __init pcie_enabler_non_es_init(void)
{
	int ret;

	ret = pcie_resolve_symbols();
	if (ret)
		return ret;

	pr_info("[%s] init: runtime PCIe RC + RTL8125/IOSS DT + V22 PMDS fix\n",
		DRV_NAME);

	pcie_node = of_find_node_by_path(PCIE_RC_PATH);
	if (!pcie_node)
		pcie_node = of_find_compatible_node(NULL, NULL, PCIE_COMPAT_1);
	if (!pcie_node)
		pcie_node = of_find_compatible_node(NULL, NULL, PCIE_COMPAT_2);
	if (!pcie_node)
		pcie_node = of_find_compatible_node(NULL, NULL, PCIE_COMPAT_3);
	if (!pcie_node)
		pcie_node = of_find_compatible_node(NULL, NULL, PCIE_COMPAT_4);
	if (!pcie_node) {
		pr_err("[%s] root complex node not found\n", DRV_NAME);
		return -ENODEV;
	}

	pr_info("[%s] root complex node: %pOF\n", DRV_NAME, pcie_node);

	ret = pcie_attach_runtime_nodes();
	if (ret) {
		of_node_put(pcie_node);
		pcie_node = NULL;
		return ret;
	}

	pcie_pdev = of_platform_device_create(pcie_node, NULL, NULL);
	if (!pcie_pdev)
		pr_warn("[%s] of_platform_device_create returned NULL; continuing with PMDS fix\n",
			DRV_NAME);
	else
		pr_info("[%s] root complex device created\n", DRV_NAME);

	run_pmds_fix_pass();
	start_pmds_recheck();

	return 0;
}

static void __exit pcie_enabler_non_es_exit(void)
{
	cancel_delayed_work_sync(&pmds_recheck_work);

	pr_info("[%s] exit\n", DRV_NAME);

	if (pcie_pdev)
		of_device_unregister(pcie_pdev);

	if (pcie_node)
		of_node_put(pcie_node);
}

module_init(pcie_enabler_non_es_init);
module_exit(pcie_enabler_non_es_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Manuel lab / OpenAI");
MODULE_DESCRIPTION("NON-ES runtime PCIe enabler with RTL8125/IOSS DT injection and V22 PMDS fix");
