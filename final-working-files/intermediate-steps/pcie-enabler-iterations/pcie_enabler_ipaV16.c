/*
 * pcie_enabler_ipaV16.c
 *
 * Fixes the corrupted av8l_fast_io_pgtable.pmds pointer in
 * ipa_smmu_eth AND ipa_smmu_eth1 shared-CB domains.
 *
 * Root cause: both ipa_smmu_eth and ipa_smmu_eth1 are shared-CB
 * slaves of ipa_smmu_ap.  Their av8l_fast_io_pgtable.pmds is
 * initialised to the master's PGD page (a kernel direct-map address)
 * instead of the vmalloc-mapped PMD array.  When r8125_ioss calls
 * iommu_map() for ring buffers, av8l_fast_map() dereferences the
 * bogus pmds pointer and crashes.
 *
 * Fix: copy pmds / base / end from ipa_smmu_ap into both
 * ipa_smmu_eth and ipa_smmu_eth1 before r8125_ioss loads.
 *
 * Load order:
 *   insmod pcie_enabler_ipaV12.ko   # OF surgery + PCIe device create
 *   insmod r8125.ko
 *   insmod ioss.ko
 *   insmod pcie_enabler_ipaV16.ko   # this module — pmds fix
 *   insmod r8125_ioss.ko
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
#include "arm-smmu.h"

#define DRV_NAME "pcie_enabler_v16"

/* ------------------------------------------------------------------ */
/* Bus search helpers                                                   */
/* ------------------------------------------------------------------ */

struct smmu_dev_search {
	const char    *suffix;
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
		return 1; /* stop iteration */
	}
	return 0;
}

static struct device *find_ipa_smmu_dev(const char *name_suffix)
{
	struct smmu_dev_search ctx = { .suffix = name_suffix, .found = NULL };

	bus_for_each_dev(&platform_bus_type, NULL, &ctx, smmu_dev_match);
	return ctx.found;
}

/* ------------------------------------------------------------------ */
/* pmds fix                                                             */
/* ------------------------------------------------------------------ */

static void fix_ipa_smmu_eth_pmds(void)
{
	struct device *ap_dev  = NULL;
	struct device *eth_dev = NULL;
	struct iommu_domain *ap_domain  = NULL;
	struct iommu_domain *eth_domain = NULL;
	struct msm_iommu_domain *ap_msm, *eth_msm;
	struct arm_smmu_domain  *ap_smmu, *eth_smmu;
	struct av8l_fast_io_pgtable *ap_data, *eth_data;

	ap_dev  = find_ipa_smmu_dev(":ipa_smmu_ap");
	eth_dev = find_ipa_smmu_dev(":ipa_smmu_eth");

	if (!ap_dev) {
		pr_err("[%s] ipa_smmu_ap device not found on platform bus\n",
		       DRV_NAME);
		goto out;
	}
	if (!eth_dev) {
		pr_err("[%s] ipa_smmu_eth device not found on platform bus\n",
		       DRV_NAME);
		goto out;
	}

	pr_info("[%s] ipa_smmu_ap  = %s\n", DRV_NAME, dev_name(ap_dev));
	pr_info("[%s] ipa_smmu_eth = %s\n", DRV_NAME, dev_name(eth_dev));

	ap_domain  = iommu_get_domain_for_dev(ap_dev);
	eth_domain = iommu_get_domain_for_dev(eth_dev);

	if (!ap_domain) {
		pr_err("[%s] ipa_smmu_ap has no IOMMU domain\n", DRV_NAME);
		goto out;
	}
	if (!eth_domain) {
		pr_err("[%s] ipa_smmu_eth has no IOMMU domain\n", DRV_NAME);
		goto out;
	}

	ap_msm  = to_msm_iommu_domain(ap_domain);
	eth_msm = to_msm_iommu_domain(eth_domain);

	ap_smmu  = container_of(ap_msm,  struct arm_smmu_domain, domain);
	eth_smmu = container_of(eth_msm, struct arm_smmu_domain, domain);

	if (!ap_smmu->pgtbl_ops[0]) {
		pr_err("[%s] ipa_smmu_ap pgtbl_ops[0] is NULL\n", DRV_NAME);
		goto out;
	}
	if (!eth_smmu->pgtbl_ops[0]) {
		pr_err("[%s] ipa_smmu_eth pgtbl_ops[0] is NULL\n", DRV_NAME);
		goto out;
	}

	ap_data  = iof_pgtable_ops_to_data(ap_smmu->pgtbl_ops[0]);
	eth_data = iof_pgtable_ops_to_data(eth_smmu->pgtbl_ops[0]);

	pr_info("[%s] ap  pmds=%p base=0x%llx end=0x%llx\n",
		DRV_NAME, ap_data->pmds,
		(unsigned long long)ap_data->base,
		(unsigned long long)ap_data->end);
	pr_info("[%s] eth pmds=%p base=0x%llx end=0x%llx\n",
		DRV_NAME, eth_data->pmds,
		(unsigned long long)eth_data->base,
		(unsigned long long)eth_data->end);

	if (!is_vmalloc_addr(ap_data->pmds)) {
		pr_err("[%s] ipa_smmu_ap pmds is NOT vmalloc — unexpected, aborting\n",
		       DRV_NAME);
		goto out;
	}

	if (is_vmalloc_addr(eth_data->pmds)) {
		pr_info("[%s] ipa_smmu_eth pmds already vmalloc (%p) — no fix needed\n",
			DRV_NAME, eth_data->pmds);
		goto out;
	}

	pr_warn("[%s] ipa_smmu_eth pmds NOT vmalloc (%p) — fixing\n",
		DRV_NAME, eth_data->pmds);

	eth_data->pmds = ap_data->pmds;
	eth_data->base = ap_data->base;
	eth_data->end  = ap_data->end;

	pr_info("[%s] eth pmds fixed: pmds=%p base=0x%llx end=0x%llx\n",
		DRV_NAME, eth_data->pmds,
		(unsigned long long)eth_data->base,
		(unsigned long long)eth_data->end);

out:
	if (ap_dev)
		put_device(ap_dev);
	if (eth_dev)
		put_device(eth_dev);
}

/* ------------------------------------------------------------------ */
/* Module init / exit                                                   */
/* ------------------------------------------------------------------ */

static int __init pcie_enabler_v16_init(void)
{
	pr_info("[%s] init: fixing ipa_smmu_eth av8l_fast pmds\n", DRV_NAME);
	fix_ipa_smmu_eth_pmds();
	pr_info("[%s] init done\n", DRV_NAME);
	return 0;
}

static void __exit pcie_enabler_v16_exit(void)
{
	pr_info("[%s] exit\n", DRV_NAME);
}

module_init(pcie_enabler_v16_init);
module_exit(pcie_enabler_v16_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Manuel lab");
MODULE_DESCRIPTION("T99W373 PCIe V16 — fix ipa_smmu_eth av8l_fast pmds corruption");
