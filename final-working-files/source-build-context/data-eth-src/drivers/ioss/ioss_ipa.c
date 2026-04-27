/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */

#include <linux/etherdevice.h>
#include <linux/log2.h>
#include <linux/mm.h>
#include <linux/moduleparam.h>

#include "ioss_i.h"

/*
 * Diagnostic switch: allow probing everything up to pipe connect and then
 * fail gracefully, to isolate crashes inside ipa_eth_client_conn_pipes().
 */
static bool ioss_skip_conn_pipes;
module_param_named(skip_conn_pipes, ioss_skip_conn_pipes, bool, 0644);
MODULE_PARM_DESC(skip_conn_pipes,
	"Skip ipa_eth_client_conn_pipes() (diagnostic)");

/*
 * Hard diagnostic switch: bypass the entire IPA register/unregister path.
 * This isolates crashes that happen even when ipa_eth_client_conn_pipes()
 * is never reached.
 */
static bool ioss_skip_ipa_register;
module_param_named(skip_ipa_register, ioss_skip_ipa_register, bool, 0644);
MODULE_PARM_DESC(skip_ipa_register,
	"Skip full IPA register/unregister path (diagnostic)");

/*
 * IPA SMMU ETH CB on this target has a practical iova ceiling around
 * 0x9fffffff. R8125 rings can expose dma/iova at 0xffxxxxxx; mapping those
 * into IPA ETH CB can fault in av8l_fast_map(). If a DMA address is above the
 * ceiling, remap IPA side iova to the buffer physical address.
 */
static bool ioss_fix_high_iova = true;
module_param_named(fix_high_iova, ioss_fix_high_iova, bool, 0644);
MODULE_PARM_DESC(fix_high_iova,
	"Use PA as IPA IOVA when buffer DMA address is above iova_ceiling");

static uint ioss_iova_ceiling = 0x9fffffff;
module_param_named(iova_ceiling, ioss_iova_ceiling, uint, 0644);
MODULE_PARM_DESC(iova_ceiling,
	"Maximum IPA IOVA accepted for direct DMA-address reuse");

/*
 * Preflight diagnostics/hardening for IPA connect path.
 * Validate all pipe descriptors before ipa_eth_client_conn_pipes().
 */
static bool ioss_preflight_strict = true;
module_param_named(preflight_strict, ioss_preflight_strict, bool, 0644);
MODULE_PARM_DESC(preflight_strict,
	"Fail before connect if IPA preflight detects invalid pipe settings");

static bool ioss_preflight_only;
module_param_named(preflight_only, ioss_preflight_only, bool, 0644);
MODULE_PARM_DESC(preflight_only,
	"Run preflight and stop before ipa_eth_client_conn_pipes() (diagnostic)");

static uint ioss_trb_align = 0x8000;
module_param_named(trb_align, ioss_trb_align, uint, 0644);
MODULE_PARM_DESC(trb_align,
	"Expected transfer ring base alignment for IPA/GSI preflight");

static uint ioss_buff_iova_align = 0x800;
module_param_named(buff_iova_align, ioss_buff_iova_align, uint, 0644);
MODULE_PARM_DESC(buff_iova_align,
	"Expected data buffer IOVA alignment for IPA/GSI preflight");

static int ioss_ipa_preflight_pipe(struct ioss_device *idev,
		struct ipa_eth_client_pipe_info *pi)
{
	int i;
	int rc = 0;
	int warn = 0;
	dma_addr_t trb_end;
	struct ipa_eth_pipe_setup_info *si = &pi->info;

	if (!si->is_transfer_ring_valid) {
		ioss_dev_err(idev, "Preflight: dir=%u transfer ring not valid",
			pi->dir);
		return -EINVAL;
	}

	if (!si->transfer_ring_base || !si->transfer_ring_size) {
		ioss_dev_err(idev,
			"Preflight: dir=%u invalid transfer ring base=%#llx size=%u",
			pi->dir, (unsigned long long)si->transfer_ring_base,
			si->transfer_ring_size);
		return -EINVAL;
	}

	if (ioss_trb_align &&
	    (si->transfer_ring_base & ((dma_addr_t)ioss_trb_align - 1))) {
		ioss_dev_err(idev,
			"Preflight: dir=%u transfer ring base %#llx not aligned to %#x",
			pi->dir, (unsigned long long)si->transfer_ring_base,
			ioss_trb_align);
		rc = -EINVAL;
	}

	if (!is_power_of_2(si->transfer_ring_size)) {
		ioss_dev_err(idev,
			"Preflight: dir=%u transfer ring size %u is not power-of-two",
			pi->dir, si->transfer_ring_size);
		warn++;
	}

	if (!si->data_buff_list || !si->data_buff_list_size || !si->fix_buffer_size) {
		ioss_dev_err(idev,
			"Preflight: dir=%u invalid buffers list=%p count=%u size=%u",
			pi->dir, si->data_buff_list, si->data_buff_list_size,
			si->fix_buffer_size);
		return -EINVAL;
	}

	if (!is_power_of_2(si->fix_buffer_size)) {
		ioss_dev_err(idev,
			"Preflight: dir=%u buffer size %u is not power-of-two",
			pi->dir, si->fix_buffer_size);
		warn++;
	}

	trb_end = si->transfer_ring_base + si->transfer_ring_size;
	for (i = 0; i < si->data_buff_list_size; i++) {
		dma_addr_t iova = si->data_buff_list[i].iova;
		phys_addr_t pa = si->data_buff_list[i].pa;
		dma_addr_t iova_end = iova + si->fix_buffer_size;

		if (!iova || !pa) {
			ioss_dev_err(idev,
				"Preflight: dir=%u idx=%d invalid iova=%#llx pa=%#llx",
				pi->dir, i,
				(unsigned long long)iova, (unsigned long long)pa);
			rc = -EINVAL;
			break;
		}

		if (iova > (dma_addr_t)ioss_iova_ceiling) {
			ioss_dev_err(idev,
				"Preflight: dir=%u idx=%d iova=%#llx above ceiling %#x",
				pi->dir, i, (unsigned long long)iova,
				ioss_iova_ceiling);
			rc = -ERANGE;
			break;
		}

		if (ioss_buff_iova_align &&
		    (iova & ((dma_addr_t)ioss_buff_iova_align - 1))) {
			ioss_dev_err(idev,
				"Preflight: dir=%u idx=%d iova=%#llx not aligned to %#x",
				pi->dir, i, (unsigned long long)iova,
				ioss_buff_iova_align);
			warn++;
		}

		if (!(iova_end <= si->transfer_ring_base || iova >= trb_end)) {
			ioss_dev_err(idev,
				"Preflight: dir=%u idx=%d buffer [%#llx-%#llx) overlaps transfer ring [%#llx-%#llx)",
				pi->dir, i,
				(unsigned long long)iova, (unsigned long long)iova_end,
				(unsigned long long)si->transfer_ring_base,
				(unsigned long long)trb_end);
			rc = -EINVAL;
			break;
		}
	}

	ioss_dev_cfg(idev,
		"Preflight summary: dir=%u trb=%#llx trsz=%u buffs=%u bsz=%u rc=%d warns=%d",
		pi->dir, (unsigned long long)si->transfer_ring_base,
		si->transfer_ring_size, si->data_buff_list_size,
		si->fix_buffer_size, rc, warn);

	return rc;
}

static int ioss_ipa_preflight_client(struct ioss_interface *iface)
{
	int rc = 0;
	int tx = 0;
	int rx = 0;
	struct ioss_channel *ch;
	struct ioss_device *idev = ioss_iface_dev(iface);

	ioss_for_each_channel(ch, iface) {
		struct ioss_ch_priv *cp = ch->ioss_priv;
		struct ipa_eth_client_pipe_info *pi = &cp->ipa_pi;
		int prc = ioss_ipa_preflight_pipe(idev, pi);

		if (pi->dir == IPA_ETH_PIPE_DIR_TX)
			tx++;
		else if (pi->dir == IPA_ETH_PIPE_DIR_RX)
			rx++;

		if (prc)
			rc = prc;
	}

	ioss_dev_cfg(idev, "Preflight pipe count: tx=%d rx=%d rc=%d", tx, rx, rc);

	if (!tx || !rx) {
		ioss_dev_err(idev,
			"Preflight: expected both TX and RX pipes (tx=%d rx=%d)",
			tx, rx);
		if (!rc)
			rc = -EINVAL;
	}

	return rc;
}

static void ioss_ipa_notify_cb(void *priv,
					enum ipa_dp_evt_type evt,
					unsigned long data)
{
	struct ioss_channel *ch = priv;
	struct sk_buff *skb = (struct sk_buff *)data;
	struct ioss_interface *iface = ch->iface;

	if (evt != IPA_RECEIVE)
		return;

	iface->exception_stats.rx_packets++;
	iface->exception_stats.rx_bytes += skb->len;

	skb->protocol = eth_type_trans(skb, ioss_iface_to_netdev(iface));

	if (netif_rx_ni(skb) == NET_RX_DROP)
		iface->exception_stats.rx_drops++;
}

static int ioss_ipa_fill_pipe_info(struct ioss_channel *ch,
		struct ipa_eth_client_pipe_info *pi)
{
	struct ioss_mem *desc_mem;
	struct ioss_mem *buff_mem;
	struct ipa_eth_buff_smmu_map *sm;
	struct ipa_eth_pipe_setup_info *si = &pi->info;
	struct ioss_device *idev = ioss_ch_dev(ch);
	phys_addr_t desc_pa;
	dma_addr_t desc_iova;
	int idx = 0;

	pi->dir = (ch->direction == IOSS_CH_DIR_TX) ?
			IPA_ETH_PIPE_DIR_TX : IPA_ETH_PIPE_DIR_RX;

	desc_mem = list_first_entry_or_null(
			&ch->desc_mem, typeof(*desc_mem), node);
	if (!desc_mem) {
		ioss_dev_err(idev, "Descriptor memory not found");
		return -EINVAL;
	}

	si->is_transfer_ring_valid = true;
	desc_iova = desc_mem->daddr;
	desc_pa = ch->config.desc_alctr->pa(ioss_ch_dev(ch),
			desc_mem->addr, desc_mem->daddr,
			ch->config.desc_alctr);
	if (unlikely(!desc_pa)) {
		ioss_dev_err(idev, "Invalid descriptor PA=0 for DMA %#llx",
			(unsigned long long)desc_mem->daddr);
		return -EINVAL;
	}
	if (ioss_fix_high_iova && desc_iova > (dma_addr_t)ioss_iova_ceiling) {
		dma_addr_t remap_iova = desc_iova & (dma_addr_t)ioss_iova_ceiling;

		if (!remap_iova || remap_iova > (dma_addr_t)ioss_iova_ceiling)
			remap_iova = (dma_addr_t)desc_pa;
		ioss_dev_err(idev,
			"Descriptor DMA IOVA %#llx above ceiling %#x, remap IPA IOVA to %#llx (PA %#llx)",
			(unsigned long long)desc_iova, ioss_iova_ceiling,
			(unsigned long long)remap_iova,
			(unsigned long long)desc_pa);
		desc_iova = remap_iova;
	}
	if (unlikely(desc_iova > (dma_addr_t)ioss_iova_ceiling)) {
		ioss_dev_err(idev, "Descriptor IPA IOVA %#llx still above ceiling %#x",
			(unsigned long long)desc_iova, ioss_iova_ceiling);
		return -ERANGE;
	}

	si->transfer_ring_base = desc_iova;
	si->transfer_ring_sgt = &desc_mem->sgt;
	si->transfer_ring_size = desc_mem->size;

	si->data_buff_list_size = ch->config.ring_size;
	si->data_buff_list = sm = kcalloc(si->data_buff_list_size,
			sizeof(*si->data_buff_list), GFP_KERNEL);
	if (!sm) {
		ioss_dev_err(idev,
			"Kcalloc failed for data buff list");
		return -EINVAL;
	}
	si->fix_buffer_size = ch->config.buff_size;

	list_for_each_entry(buff_mem, &ch->buff_mem, node) {
		phys_addr_t pa;
		dma_addr_t iova = buff_mem->daddr;

		if (unlikely(idx >= si->data_buff_list_size)) {
			ioss_dev_err(idev,
				"Buffer list overflow: idx=%d list_size=%u (ring_size=%u)",
				idx, si->data_buff_list_size, ch->config.ring_size);
			kfree(si->data_buff_list);
			si->data_buff_list = NULL;
			return -EOVERFLOW;
		}

		pa = ch->config.buff_alctr->pa(ioss_ch_dev(ch),
				buff_mem->addr, buff_mem->daddr,
				ch->config.buff_alctr);

		/*
		 * Some allocator paths can return a kernel virtual address
		 * instead of a physical address; IPA SMMU map then crashes
		 * in av8l_fast_map(). Detect and sanitize to a linear-map PA.
		 */
		if ((unsigned long)pa >= PAGE_OFFSET) {
			phys_addr_t fixed_pa = virt_to_phys(buff_mem->addr);

			ioss_dev_err(idev,
				"Invalid PA %#llx for IOVA %#llx, fixing to %#llx",
				(unsigned long long)pa,
				(unsigned long long)buff_mem->daddr,
				(unsigned long long)fixed_pa);
			pa = fixed_pa;
		}

		if (unlikely(!pa)) {
			ioss_dev_err(idev, "Invalid PA=0 for DMA %#llx",
				(unsigned long long)buff_mem->daddr);
			kfree(si->data_buff_list);
			si->data_buff_list = NULL;
			return -EINVAL;
		}

		if (ioss_fix_high_iova && iova > (dma_addr_t)ioss_iova_ceiling) {
			dma_addr_t remap_iova = iova & (dma_addr_t)ioss_iova_ceiling;

			if (!remap_iova || remap_iova > (dma_addr_t)ioss_iova_ceiling)
				remap_iova = (dma_addr_t)pa;
			if (idx < 4)
				ioss_dev_err(idev,
					"High DMA IOVA %#llx above ceiling %#x, remap IPA IOVA to %#llx (PA %#llx)",
					(unsigned long long)iova, ioss_iova_ceiling,
					(unsigned long long)remap_iova,
					(unsigned long long)pa);
			iova = remap_iova;
		}

		if (unlikely(iova > (dma_addr_t)ioss_iova_ceiling)) {
			ioss_dev_err(idev,
				"IPA IOVA %#llx still above ceiling %#x",
				(unsigned long long)iova, ioss_iova_ceiling);
			kfree(si->data_buff_list);
			si->data_buff_list = NULL;
			return -ERANGE;
		}

		sm->iova = iova;
		sm->pa = pa;
		if (idx < 4)
			ioss_dev_cfg(idev,
				"pipe dir=%u idx=%d iova=%#llx pa=%#llx vaddr=%p daddr=%#llx",
				pi->dir, idx,
				(unsigned long long)sm->iova,
				(unsigned long long)sm->pa,
				buff_mem->addr,
				(unsigned long long)buff_mem->daddr);
		sm++;
		idx++;
	}

	if (idx != si->data_buff_list_size) {
		ioss_dev_err(idev,
			"Buffer list count mismatch: built=%d expected=%u (ring_size=%u)",
			idx, si->data_buff_list_size, ch->config.ring_size);
		si->data_buff_list_size = idx;
	}

	if (pi->dir == IPA_ETH_PIPE_DIR_RX) {
		si->notify = ioss_ipa_notify_cb;
		si->priv = ch;
	}

	if (ioss_ipa_hal_fill_si(ch)) {
		ioss_dev_err(idev,
			"Failed to fill IPA pipe setup info");
		kfree(si->data_buff_list);
		return -EINVAL;
	}

	return 0;
}

#if IPA_ETH_API_VER < 2
static void ioss_ipa_fill_eth_hdrs(struct ioss_interface *iface)
{
	struct net_device *net_dev = ioss_iface_to_netdev(iface);

	struct ioss_iface_priv *ifp = iface->ioss_priv;
	struct ipa_eth_intf_info *ii = &ifp->ipa_ii;

	union ioss_ipa_eth_hdr *hdr_v4 = &ifp->ipa_hdr_v4;
	union ioss_ipa_eth_hdr *hdr_v6 = &ifp->ipa_hdr_v6;

	struct ipa_eth_hdr_info *hi_v4 = &ii->hdr[0];
	struct ipa_eth_hdr_info *hi_v6 = &ii->hdr[1];

	/* IPv4 */
	memcpy(hdr_v4->l2.h_source, net_dev->dev_addr, ETH_ALEN);
	hdr_v4->l2.h_proto = htons(ETH_P_IP);

	hi_v4->hdr = (u8 *)hdr_v4;
	hi_v4->hdr_len = ETH_HLEN;
	hi_v4->hdr_type = IPA_HDR_L2_ETHERNET_II;

	/* IPv6 */
	memcpy(hdr_v6->l2.h_source, net_dev->dev_addr, ETH_ALEN);
	hdr_v6->l2.h_proto = htons(ETH_P_IPV6);

	hi_v6->hdr = (u8 *)hdr_v6;
	hi_v6->hdr_len = ETH_HLEN;
	hi_v6->hdr_type = IPA_HDR_L2_ETHERNET_II;
}

static void ioss_ipa_fill_vlan_hdrs(struct ioss_interface *iface)
{
	struct net_device *net_dev = ioss_iface_to_netdev(iface);

	struct ioss_iface_priv *ifp = iface->ioss_priv;
	struct ipa_eth_intf_info *ii = &ifp->ipa_ii;

	union ioss_ipa_eth_hdr *hdr_v4 = &ifp->ipa_hdr_v4;
	union ioss_ipa_eth_hdr *hdr_v6 = &ifp->ipa_hdr_v6;

	struct ipa_eth_hdr_info *hi_v4 = &ii->hdr[0];
	struct ipa_eth_hdr_info *hi_v6 = &ii->hdr[1];

	/* IPv4 */
	memcpy(hdr_v4->vlan.h_source, net_dev->dev_addr, ETH_ALEN);
	hdr_v4->vlan.h_vlan_proto = htons(ETH_P_8021Q);
	hdr_v4->vlan.h_vlan_encapsulated_proto = htons(ETH_P_IP);

	hi_v4->hdr = (u8 *)hdr_v4;
	hi_v4->hdr_len = VLAN_ETH_HLEN;
	hi_v4->hdr_type = IPA_HDR_L2_802_1Q;

	/* IPv6 */
	memcpy(hdr_v6->vlan.h_source, net_dev->dev_addr, ETH_ALEN);
	hdr_v6->vlan.h_vlan_proto = htons(ETH_P_8021Q);
	hdr_v6->vlan.h_vlan_encapsulated_proto = htons(ETH_P_IPV6);

	hi_v6->hdr = (u8 *)hdr_v6;
	hi_v6->hdr_len = VLAN_ETH_HLEN;
	hi_v6->hdr_type = IPA_HDR_L2_802_1Q;
}

static int ioss_ipa_fill_hdrs(struct ioss_interface *iface)
{
	bool ipa_vlan_mode = false;
	struct ioss_device *idev = ioss_iface_dev(iface);

	if (ipa_is_vlan_mode(IPA_VLAN_IF_EMAC, &ipa_vlan_mode)) {
		ioss_dev_err(idev, "Failed to get IPA vlan mode");
		return -EINVAL;
	}

	if (ipa_vlan_mode)
		ioss_ipa_fill_vlan_hdrs(iface);
	else
		ioss_ipa_fill_eth_hdrs(iface);

	return 0;
}
#endif

static int ioss_ipa_vote_bw(struct ioss_interface *iface)
{
	struct ipa_eth_perf_profile profile;
	struct ioss_iface_priv *ifp = iface->ioss_priv;
	struct ipa_eth_client *ec = &ifp->ipa_ec;
	struct ioss_device *idev = ioss_iface_dev(iface);

	ioss_dev_dbg(idev,
		"Voting for IPA bandwidth of %u Mbps", iface->link_speed);

	memset(&profile, 0, sizeof(profile));
	profile.max_supported_bw_mbps = iface->link_speed;

	if (ipa_eth_client_set_perf_profile(ec, &profile)) {
		ioss_dev_err(idev,
				"Failed to set IPA perf profile");
		return -EINVAL;
	}

	ioss_dev_cfg(idev,
		"Voted for IPA bandwidth of %u Mbps", iface->link_speed);

	return 0;
}

#if IPA_ETH_API_VER < 2
static int __ioss_ipa_msg(struct net_device *net_dev, bool connect)
{
	struct ipa_ecm_msg msg;

	memset(&msg, 0, sizeof(msg));
	snprintf(msg.name, sizeof(msg.name), "%s", net_dev->name);
	msg.ifindex = net_dev->ifindex;

	return connect ?
		ipa_eth_client_conn_evt(&msg) :
		ipa_eth_client_disconn_evt(&msg);
}

static int ioss_ipa_msg_connect(struct net_device *net_dev)
{
	return __ioss_ipa_msg(net_dev, true);
}

static int ioss_ipa_msg_disconnect(struct net_device *net_dev)
{
	return __ioss_ipa_msg(net_dev, false);
}
#endif

int ioss_ipa_register(struct ioss_interface *iface)
{
	int i;
	int rc;
#if IPA_ETH_API_VER < 2
	int ch_count;
#endif
	struct ioss_iface_priv *ifp = iface->ioss_priv;
	struct ipa_eth_client *ec = &ifp->ipa_ec;
	struct ipa_eth_intf_info *ii = &ifp->ipa_ii;
	struct ioss_channel *ch;
#if IPA_ETH_API_VER < 2
	struct net_device *net_dev = ioss_iface_to_netdev(iface);
#endif
	struct ioss_device *idev = ioss_iface_dev(iface);

	if (ioss_skip_ipa_register) {
		ioss_dev_err(idev,
			"DIAG: skip_ipa_register=1, bypassing full ioss_ipa_register");
		return 0;
	}

	ec->priv = iface;
	ec->inst_id = iface->instance_id;
	ec->traffic_type = IPA_ETH_PIPE_BEST_EFFORT;
	ec->client_type = ioss_ipa_hal_get_ctype(idev);
	ioss_dev_cfg(idev, "IPA register start: inst=%u ctype=%u",
		ec->inst_id, ec->client_type);

	if (ec->client_type == IPA_ETH_CLIENT_MAX) {
		ioss_dev_err(idev, "Failed to determine client type");
		return -EFAULT;
	}

	INIT_LIST_HEAD(&ec->pipe_list);

#if IPA_ETH_API_VER < 2
	ch_count = 0;
#endif
	ioss_for_each_channel(ch, iface) {
		struct ioss_ch_priv *cp = ch->ioss_priv;

		ioss_dev_log(idev,
			"IPA channel prepare: dir=%u ring_size=%u buff_size=%u",
			ch->direction, ch->config.ring_size, ch->config.buff_size);
		cp->ipa_pi.client_info = ec;

		if (ioss_ipa_fill_pipe_info(ch, &cp->ipa_pi)) {
			ioss_dev_err(idev, "Failed to fill pipe info");
			goto err_ioss_pipe_info;
		}
		ioss_dev_log(idev,
			"IPA channel prepared: dir=%u trb=%pad trsz=%u buffs=%u",
			ch->direction,
			&cp->ipa_pi.info.transfer_ring_base,
			cp->ipa_pi.info.transfer_ring_size,
			cp->ipa_pi.info.data_buff_list_size);

		list_add_tail(&cp->ipa_pi.link, &ec->pipe_list);
#if IPA_ETH_API_VER < 2
		ch_count++;
#endif
	}
#if IPA_ETH_API_VER < 2
	ii->pipe_hdl_list_size = ch_count;

	ii->pipe_hdl_list = kcalloc(ii->pipe_hdl_list_size,
				sizeof(*ii->pipe_hdl_list), GFP_KERNEL);
	if (!ii->pipe_hdl_list) {
		ioss_dev_err(idev, "Failed to alloc pipe hdl list");
		goto err_pipe_hdl;
	}

	ii->netdev_name = net_dev->name;

	if (ioss_ipa_fill_hdrs(iface)) {
		ioss_dev_err(idev, "Failed to fill partial headers");
		goto err_ipa_fill_hdrs;
	}
#endif

	/* connect pipes */
#if IPA_ETH_API_VER >= 2
	ec->net_dev = ioss_iface_to_netdev(iface);
#endif
	rc = ioss_ipa_preflight_client(iface);
	if (rc && ioss_preflight_strict) {
		ioss_dev_err(idev, "Preflight failed rc=%d; aborting connect", rc);
		goto err_ipa_conn_pipes;
	}
	if (ioss_preflight_only) {
		ioss_dev_err(idev, "DIAG: preflight_only=1, stopping before connect");
		rc = -EOPNOTSUPP;
		goto err_ipa_conn_pipes;
	}
	if (ioss_skip_conn_pipes) {
		ioss_dev_err(idev,
			"DIAG: skip_conn_pipes=1, bypassing ipa_eth_client_conn_pipes");
		rc = -EOPNOTSUPP;
		goto err_ipa_conn_pipes;
	}
	ioss_dev_cfg(idev, "Calling ipa_eth_client_conn_pipes");
	rc = ipa_eth_client_conn_pipes(ec);
	if (rc) {
		ioss_dev_err(idev, "Failed to connect pipes rc=%d", rc);
		goto err_ipa_conn_pipes;
	}
	ioss_dev_cfg(idev, "ipa_eth_client_conn_pipes done");

	/* vote for bw */
	ioss_dev_cfg(idev, "Calling ioss_ipa_vote_bw");
	rc = ioss_ipa_vote_bw(iface);
	if (rc) {
		ioss_dev_err(idev, "Failed to vote for bandwidth rc=%d", rc);
		goto err_ipa_vote_bw;
	}
	ioss_dev_cfg(idev, "ioss_ipa_vote_bw done");

	/* Retrieve output from conn_pipes call */
	i = 0;
	ioss_for_each_channel(ch, iface) {
		struct ioss_ch_priv *cp = ch->ioss_priv;
		struct ipa_eth_client_pipe_info *pi = &cp->ipa_pi;
		struct ipa_eth_pipe_setup_info *si = &pi->info;

		ch->event.paddr = si->db_pa;
		ch->event.data = si->db_val;
		ioss_dev_log(idev,
			"IPA channel output: dir=%u pipe_hdl=%u db=%pap db_val=%u",
			ch->direction, pi->pipe_hdl, &si->db_pa, si->db_val);

#if IPA_ETH_API_VER < 2
		ii->pipe_hdl_list[i++] = pi->pipe_hdl;
#endif
	}

	/* register interface */
#if IPA_ETH_API_VER >= 2
	ii->client = ec;
	ii->is_conn_evt = true;
#endif
	ioss_dev_cfg(idev, "Calling ipa_eth_client_reg_intf");
	rc = ipa_eth_client_reg_intf(ii);
	if (rc) {
		ioss_dev_err(idev, "Failed to register interface rc=%d", rc);
		goto err_ipa_reg_intf;
	}
	ioss_dev_cfg(idev, "ipa_eth_client_reg_intf done");

#if IPA_ETH_API_VER < 2
	/* send ecm msg */
	if (ioss_ipa_msg_connect(net_dev)) {
		ioss_dev_err(idev, "Failed to send connect message");
		return -EINVAL;
	}
#endif

	return 0;

err_ipa_reg_intf:
	ioss_for_each_channel(ch, iface) {
		ch->event.paddr = 0;
		ch->event.data = 0;
	}
err_ipa_vote_bw:
	if (ipa_eth_client_disconn_pipes(ec))
		ioss_dev_err(idev, "Failed to disconnect pipes");
err_ipa_conn_pipes:
#if IPA_ETH_API_VER < 2
	memset(hdr_v4, 0, sizeof(*hdr_v4));
	memset(hdr_v6, 0, sizeof(*hdr_v6));
#endif
#if IPA_ETH_API_VER < 2
err_ipa_fill_hdrs:
	kfree(ii->pipe_hdl_list);
err_pipe_hdl:
#endif
	ioss_for_each_channel(ch, iface) {
		struct ioss_ch_priv *cp = ch->ioss_priv;
		struct ipa_eth_client_pipe_info *pi = &cp->ipa_pi;
		struct ipa_eth_pipe_setup_info *si = &pi->info;

		kfree(si->data_buff_list);
		memset(&cp->ipa_pi, 0, sizeof(cp->ipa_pi));
	}
err_ioss_pipe_info:
	memset(ii, 0, sizeof(*ii));
	memset(ec, 0, sizeof(*ec));

	return -EINVAL;
}

int ioss_ipa_unregister(struct ioss_interface *iface)
{
	int rc;
	struct ioss_channel *ch;
	struct ioss_iface_priv *ifp = iface->ioss_priv;
	struct ipa_eth_client *ec = &ifp->ipa_ec;
	struct ipa_eth_intf_info *ii = &ifp->ipa_ii;
#if IPA_ETH_API_VER < 2
	union ioss_ipa_eth_hdr *hdr_v4 = &ifp->ipa_hdr_v4;
	union ioss_ipa_eth_hdr *hdr_v6 = &ifp->ipa_hdr_v6;
	struct net_device *net_dev = ioss_iface_to_netdev(iface);
#endif
	struct ioss_device *idev = ioss_iface_dev(iface);

	if (ioss_skip_ipa_register) {
		ioss_dev_err(idev,
			"DIAG: skip_ipa_register=1, bypassing full ioss_ipa_unregister");
		return 0;
	}

	/* connect pipes */
	rc = ipa_eth_client_disconn_pipes(ec);
	if (rc) {
		ioss_dev_err(idev, "Failed to disconnect pipes");
		return rc;
	}

	/* unregister interface */
	rc = ipa_eth_client_unreg_intf(ii);
	if (rc) {
		ioss_dev_err(idev, "Failed to unregister interface");
		return rc;
	}

#if IPA_ETH_API_VER < 2
	/* send ecm msg */
	rc = ioss_ipa_msg_disconnect(net_dev);
	if (rc) {
		ioss_dev_err(idev, "Failed to send disconnect message");
		return rc;
	}

	memset(hdr_v4, 0, sizeof(*hdr_v4));
	memset(hdr_v6, 0, sizeof(*hdr_v6));

	kfree(ii->pipe_hdl_list);
#endif
	memset(ii, 0, sizeof(*ii));

	ioss_for_each_channel(ch, iface) {
		struct ioss_ch_priv *cp = ch->ioss_priv;
		struct ipa_eth_client_pipe_info *pi = &cp->ipa_pi;
		struct ipa_eth_pipe_setup_info *si = &pi->info;

		ch->event.paddr = 0;
		ch->event.data = 0;

		kfree(si->data_buff_list);
		memset(&cp->ipa_pi, 0, sizeof(cp->ipa_pi));
	}

	memset(ec, 0, sizeof(*ec));

	return 0;
}
