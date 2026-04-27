/*
 * pcie_enabler_ipaV22.c
 *
 * Residual IPA crash fix:
 * - force av8l_fast pmds/base/end from ipa_smmu_ap to both
 *   ipa_smmu_eth and ipa_smmu_eth1
 * - periodically re-check for a short window to survive late re-init races
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/iommu.h>
#include <linux/io-pgtable.h>
#include <linux/io-pgtable-fast.h>
#include <linux/vmalloc.h>
#include <linux/string.h>
#include <linux/workqueue.h>
#include "arm-smmu.h"

#define DRV_NAME "pcie_enabler_v22"
#define RECHECK_MS 2000
#define RECHECK_MAX 15

struct smmu_dev_search {
	const char *suffix;
	struct device *found;
};

static struct delayed_work pmds_recheck_work;
static int pmds_recheck_count;

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

static void run_fix_pass(void)
{
	int changed = 0;

	changed |= fix_one_slave_pmds(":ipa_smmu_eth");
	changed |= fix_one_slave_pmds(":ipa_smmu_eth1");

	if (!changed)
		pr_info("[%s] fix pass: no changes needed\n", DRV_NAME);
}

static void pmds_recheck_fn(struct work_struct *work)
{
	run_fix_pass();

	pmds_recheck_count++;
	if (pmds_recheck_count < RECHECK_MAX)
		schedule_delayed_work(&pmds_recheck_work, msecs_to_jiffies(RECHECK_MS));
}

static int __init pcie_enabler_v22_init(void)
{
	pr_info("[%s] init: fixing ipa_smmu_eth + ipa_smmu_eth1 PMDS (with recheck)\n",
		DRV_NAME);

	run_fix_pass();

	INIT_DELAYED_WORK(&pmds_recheck_work, pmds_recheck_fn);
	pmds_recheck_count = 0;
	schedule_delayed_work(&pmds_recheck_work, msecs_to_jiffies(RECHECK_MS));

	return 0;
}

static void __exit pcie_enabler_v22_exit(void)
{
	cancel_delayed_work_sync(&pmds_recheck_work);
	pr_info("[%s] exit\n", DRV_NAME);
}

module_init(pcie_enabler_v22_init);
module_exit(pcie_enabler_v22_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Manuel lab / OpenAI");
MODULE_DESCRIPTION("T99W373 PCIe V22 — fix IPA ETH/ETH1 av8l_fast PMDS + recheck");

