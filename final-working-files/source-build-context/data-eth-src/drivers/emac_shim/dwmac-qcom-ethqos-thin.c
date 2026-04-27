// SPDX-License-Identifier: GPL-2.0-only

// Copyright (c) 2021 The Linux Foundation. All rights reserved.
// Copyright (c) 2018-19 Linaro Limited
// Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.

#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#include <linux/io.h>
#include <linux/iopoll.h>

#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/dma-iommu.h>
#include <linux/iommu.h>

#include <linux/etherdevice.h>
#include <linux/rtnetlink.h>

#include "stmmac_thin.h"
#include "stmmac_platform_thin.h"
#include "dwmac-qcom-ethqos-thin.h"

#define EMAC_HW_v3_0_0 0x30000000
#define MAX_FILTER_CHK 10

void *ipc_emac_log_ctxt;

struct emac_emb_smmu_cb_ctx emac_emb_smmu_ctx = {0};
struct qcom_ethqos *pethqos;

struct emac_fe_ev {
	struct list_head list;
	unsigned long ev;
};

static struct multicast_mac_addr mc_addrs[MAX_FILTER_CHK];
static struct unicast_mac_addr uc_addrs[MAX_FILTER_CHK];

static void emac_fe_ev_wq(struct work_struct *work)
{
	struct qcom_ethqos *ethqos = container_of(work, struct qcom_ethqos,
						  emac_fe_work);
	struct stmmac_priv *priv = qcom_ethqos_get_priv(ethqos);
	struct emac_fe_ev *emac_ev;

	ETHQOSINFO("Enter - cur state [%u]\n", priv->emac_state);
	do {
		mutex_lock(&ethqos->lock);
		emac_ev = list_first_entry_or_null(&ethqos->emac_fe_ev_q,
						   struct emac_fe_ev, list);
		if (emac_ev)
			list_del(&emac_ev->list);
		mutex_unlock(&ethqos->lock);

		if (!emac_ev)
			break;

		ETHQOSINFO("get ev [%u]\n", emac_ev->ev);
		switch (emac_ev->ev) {
		case EMAC_HW_UP:
			ETHQOSDBG("HW up ev\n");
			if (priv->emac_state != EMAC_INIT_ST)
				break;

			priv->emac_state = EMAC_HW_UP_ST;
			if (ethqos->suspended &&
			    !stmmac_resume(priv->device)) {
				ETHQOSINFO("resume on HW up\n");
				ethqos->suspended = false;
			} else if (priv->dev_opened &&
				   !priv->dev_inited) {
				ETHQOSINFO("init driver on HW up\n");
				stmmac_dvr_init(priv->dev);
				priv->add_filter(priv->dev);
			}
			break;
		case EMAC_HW_DOWN:
			ETHQOSDBG("HW down ev\n");
			if (priv->emac_state > EMAC_INIT_ST &&
			    priv->dev_inited) {
				if (!stmmac_suspend(priv->device)) {
					ETHQOSINFO("suspended\n");
					ethqos->suspended = true;
					emac_ctrl_fe_gvm_dma_stopped();
				}
			}
			priv->emac_state = EMAC_INIT_ST;
			break;
		case EMAC_LINK_UP:
			ETHQOSDBG("Link up ev\n");
			if (priv->emac_state == EMAC_HW_UP_ST) {
				priv->emac_state = EMAC_LINK_UP_ST;
				stmmac_mac_link_up(priv->dev);
			}
			break;
		case EMAC_LINK_DOWN:
			ETHQOSDBG("Link down ev\n");
			if (priv->emac_state == EMAC_LINK_UP_ST) {
				priv->emac_state = EMAC_HW_UP_ST;
				stmmac_mac_link_down(priv->dev);
			}
			break;
		case EMAC_DMA_INT_STS_AVAIL:
			ETHQOSDBG("CH intr status ev\n");
			if (priv->emac_state > EMAC_INIT_ST &&
			    priv->dev_inited)
				stmmac_ch_status(priv->dev);
			break;
		default:
			ETHQOSERR("Invalid ev passed: %d\n", emac_ev->ev);
		}
		kfree(emac_ev);
	} while (1);

	ETHQOSINFO("End - cur state [%u]\n", priv->emac_state);
}

static int qcom_ethqos_emac_notify_cb(struct notifier_block *nb,
				      unsigned long ev, void *dev)
{
	struct qcom_ethqos *ethqos = container_of(nb, struct qcom_ethqos,
						  emac_nb);
	struct emac_fe_ev *emac_ev;

	ETHQOSINFO("event [%d]\n", ev);

	if (ev == EMAC_DMA_INT_STS_AVAIL)
		return NOTIFY_DONE;

	emac_ev = kzalloc(sizeof(struct emac_fe_ev), GFP_ATOMIC);
	if (!emac_ev)
		return -ENOMEM;

	emac_ev->ev = ev;
	mutex_lock(&ethqos->lock);
	list_add_tail(&emac_ev->list, &ethqos->emac_fe_ev_q);
	mutex_unlock(&ethqos->lock);

	queue_work(ethqos->wq, &ethqos->emac_fe_work);

	return NOTIFY_DONE;
}

static void qcom_ethqos_register_emac_fe_listener(struct qcom_ethqos *ethqos)
{
	int ret;

	ETHQOSINFO("Enter\n");
	ethqos->emac_nb.notifier_call = qcom_ethqos_emac_notify_cb;
	ret = emac_ctrl_fe_register_notifier(&ethqos->emac_nb);
	if (ret)
		ETHQOSERR("emac_ctrl_fe_register_notifier failed\n");
}

static void
qcom_ethqos_unregister_emac_fe_listener(struct qcom_ethqos *ethqos, int reason)
{
	ethqos->emac_nb.notifier_call = qcom_ethqos_emac_notify_cb;
	ethqos->emac_nb.priority = reason;
	emac_ctrl_fe_unregister_notifier(&ethqos->emac_nb);
}

static inline unsigned int dwmac_qcom_get_eth_type(unsigned char *buf)
{
	return
		((((u16)buf[QTAG_ETH_TYPE_OFFSET] << 8) |
		  buf[QTAG_ETH_TYPE_OFFSET + 1]) == ETH_P_8021Q) ?
		(((u16)buf[QTAG_VLAN_ETH_TYPE_OFFSET] << 8) |
		 buf[QTAG_VLAN_ETH_TYPE_OFFSET + 1]) :
		 (((u16)buf[QTAG_ETH_TYPE_OFFSET] << 8) |
		  buf[QTAG_ETH_TYPE_OFFSET + 1]);
}

static u16 dwmac_qcom_select_queue(struct net_device *dev,
				   struct sk_buff *skb,
				   struct net_device *sb_dev)
{
	struct stmmac_priv *priv = netdev_priv(dev);

	return priv->queue;
}

static unsigned int dwmac_qcom_get_plat_tx_coal_frames(struct sk_buff *skb)
{
	unsigned int eth_type;
#ifdef CONFIG_PTPSUPPORT_OBJ
	bool is_udp;
#endif

	eth_type = dwmac_qcom_get_eth_type(skb->data);

#ifdef CONFIG_PTPSUPPORT_OBJ
	if (eth_type == ETH_P_1588)
		return PTP_INT_MOD;
#endif

	if (eth_type == ETH_P_TSN)
		return AVB_INT_MOD;
	if (eth_type == ETH_P_IP || eth_type == ETH_P_IPV6) {
#ifdef CONFIG_PTPSUPPORT_OBJ
		is_udp = (((eth_type == ETH_P_IP) &&
				   (ip_hdr(skb)->protocol ==
					IPPROTO_UDP)) ||
				  ((eth_type == ETH_P_IPV6) &&
				   (ipv6_hdr(skb)->nexthdr ==
					IPPROTO_UDP)));

		if (is_udp && ((udp_hdr(skb)->dest ==
			htons(PTP_UDP_EV_PORT)) ||
			(udp_hdr(skb)->dest ==
			  htons(PTP_UDP_GEN_PORT))))
			return PTP_INT_MOD;
#endif
		return IP_PKT_INT_MOD;
	}
	return DEFAULT_INT_MOD;
}

static const struct of_device_id qcom_ethqos_match[] = {
	{ .compatible = "qcom,stmmac-ethqos-emac1", },
	{ .compatible = "qcom,emac-smmu-embedded", },
	{ }
};

static void emac_emb_smmu_exit(void)
{
	emac_emb_smmu_ctx.valid = false;
	emac_emb_smmu_ctx.pdev_master = NULL;
	emac_emb_smmu_ctx.smmu_pdev = NULL;
	emac_emb_smmu_ctx.iommu_domain = NULL;
}

static int emac_emb_smmu_cb_probe(struct platform_device *pdev)
{
	int result = 0;
	u32 iova_ap_mapping[2];
	struct device *dev = &pdev->dev;

	ETHQOSINFO("EMAC EMB SMMU CB probe: smmu pdev=%X\n", pdev);

	result = of_property_read_u32_array(dev->of_node,
					    "qcom,iommu-dma-addr-pool",
					    iova_ap_mapping,
					    ARRAY_SIZE(iova_ap_mapping));
	if (result) {
		ETHQOSERR("Failed to read EMB start/size iova addresses\n");
		return result;
	}

	emac_emb_smmu_ctx.smmu_pdev = pdev;
	emac_emb_smmu_ctx.valid = true;

	emac_emb_smmu_ctx.iommu_domain =
		iommu_get_domain_for_dev(&emac_emb_smmu_ctx.smmu_pdev->dev);

	ETHQOSINFO("Successfully attached to IOMMU\n");
	emac_emb_smmu_ctx.ret = result;

	return result;
}

inline void *qcom_ethqos_get_priv(struct qcom_ethqos *ethqos)
{
	struct platform_device *pdev = ethqos->pdev;
	struct net_device *dev = platform_get_drvdata(pdev);
	struct stmmac_priv *priv = netdev_priv(dev);

	return priv;
}

static int qcom_ethqos_mac_addr(struct net_device *ndev)
{
	struct unicast_mac_addr mac;

	ETHQOSINFO("Enter\n");
	if (is_valid_ether_addr(ndev->dev_addr)) {
		memcpy(mac.enm_addr, ndev->dev_addr, ETH_ALEN);
		return emac_ctrl_fe_mac_addr_chg(&mac);
	}

	return 0;
}

static int qcom_ethqos_set_mc_filters(struct net_device *dev)
{
	enum emac_ctrl_fe_filter_types filter_type;
	union emac_ctrl_fe_filter filter;
	struct netdev_hw_addr *ha;
	u8 *addr;
	int ret, i;

	filter_type = MULTICAST_FILTER;
	netdev_for_each_mc_addr(ha, dev) {
		addr = &filter.multi_mac.enm_addr[0];
		memcpy(addr, ha->addr, ETH_ALEN);
		for (i = 0; i < MAX_FILTER_CHK; i++) {
			if (is_zero_ether_addr(mc_addrs[i].enm_addr)) {
				ETHQOSINFO("add addr %pM to mc_addrs[%d]\n",
					   addr, i);
				memcpy(mc_addrs[i].enm_addr, addr, ETH_ALEN);
				ret = emac_ctrl_fe_filter_add_request
					(filter_type, &filter);
				if (ret) {
					ETHQOSERR("Add mc filter ret = %d\n",
						  ret);
					return ret;
				}
				break;
			} else {
				if (!memcmp(addr, mc_addrs[i].enm_addr,
					    ETH_ALEN))
					/* Same Addr found, skip */
					break;
			}
		}
		if (i == MAX_FILTER_CHK) {
			ETHQOSERR("More than %d mc addr to filter\n",
				  MAX_FILTER_CHK);
			ret = emac_ctrl_fe_filter_add_request(filter_type,
							      &filter);
			if (ret) {
				ETHQOSERR("Add mc filter ret = %d\n", ret);
				return ret;
			}
		}
	}

	return 0;
}

static int qcom_ethqos_set_uc_filters(struct net_device *dev)
{
	enum emac_ctrl_fe_filter_types filter_type;
	union emac_ctrl_fe_filter filter;
	struct netdev_hw_addr *ha;
	u8 *addr;
	int ret, i;

	filter_type = UNICAST_FILTER;
	netdev_for_each_uc_addr(ha, dev) {
		addr = &filter.unicast_mac.enm_addr[0];
		memcpy(addr, ha->addr, ETH_ALEN);
		for (i = 0; i < MAX_FILTER_CHK; i++) {
			if (is_zero_ether_addr(uc_addrs[i].enm_addr)) {
				ETHQOSINFO("add addr %pM to uc_addrs[%d]\n",
					   addr, i);
				memcpy(uc_addrs[i].enm_addr, addr, ETH_ALEN);
				ret = emac_ctrl_fe_filter_add_request
					(filter_type, &filter);
				if (ret) {
					ETHQOSERR("Add uc filter ret = %d\n",
						  ret);
					return ret;
				}
				break;
			} else {
				if (!memcmp(addr, uc_addrs[i].enm_addr,
					    ETH_ALEN))
					/* Same Addr found, skip */
					break;
			}
		}
		if (i == MAX_FILTER_CHK) {
			ETHQOSERR("More than %d uc addr to filter\n",
				  MAX_FILTER_CHK);
			ret = emac_ctrl_fe_filter_add_request(filter_type,
							      &filter);
			if (ret) {
				ETHQOSERR("Add uc filter ret = %d\n", ret);
				return ret;
			}
		}
	}

	return 0;
}

static int qcom_ethqos_add_filter(struct net_device *ndev)
{
	struct stmmac_priv *priv = netdev_priv(ndev);
	enum emac_ctrl_fe_filter_types filter_type;
	union emac_ctrl_fe_filter filter;
	int ret = -EPERM;

	if (priv->emac_state < EMAC_HW_UP_ST) {
		ETHQOSINFO("emac HW is not ready for adding filter\n");
		return -EPERM;
	}

	switch (priv->filter_type) {
	case VLAN_TYPE:
		filter_type = VLAN_FILTER;
		filter.vlan_id = priv->vid;
		ret = emac_ctrl_fe_filter_add_request(filter_type, &filter);
		break;
	case MULTICAST_TYPE:
		ret = qcom_ethqos_set_mc_filters(ndev);
		break;
	case UNICAST_TYPE:
		ret = qcom_ethqos_set_uc_filters(ndev);
		break;
	default:
		ETHQOSINFO("Wrong filter type %d\n", priv->filter_type);
		break;
	}

	return ret;
}

static int qcom_ethqos_del_filter(struct net_device *ndev)
{
	struct stmmac_priv *priv = netdev_priv(ndev);
	enum emac_ctrl_fe_filter_types filter_type;
	union emac_ctrl_fe_filter filter;

	if (priv->emac_state < EMAC_HW_UP_ST) {
		ETHQOSINFO("emac HW is not ready for deleting filter\n");
		return -EPERM;
	}

	if (priv->filter_type == VLAN_TYPE) {
		filter_type = VLAN_FILTER;
		filter.vlan_id = priv->vid;
		return emac_ctrl_fe_filter_del_request(filter_type, filter);
	}

	return -EPERM;
}

static void ethqos_emac_fe_ready_wq(struct work_struct *work)
{
	struct qcom_ethqos *ethqos = container_of(work, struct qcom_ethqos,
						  emac_fe_rdy_work);

	ETHQOSINFO("Enter\n");
	qcom_ethqos_register_emac_fe_listener(ethqos);
}

static void ethqos_emac_fe_ready_cb(void *user_data)
{
	struct qcom_ethqos *ethqos = (struct qcom_ethqos *)user_data;

	ETHQOSINFO("Enter\n");
	if (ethqos)
		queue_work(ethqos->wq, &ethqos->emac_fe_rdy_work);
}

static int qcom_ethqos_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct plat_stmmacenet_data *plat_dat = NULL;
	struct stmmac_resources stmmac_res;
	struct qcom_ethqos *ethqos = NULL;
	int ret, count = 0;
	struct net_device *ndev;
	struct stmmac_priv *priv;

	ETHQOSINFO("Start\n");

	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (ret) {
		ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
		if (ret) {
			ETHQOSERR("could not set DMA mask\n");
			return ret;
		}
	}
	if (of_device_is_compatible(np, "qcom,emac-smmu-embedded"))
		return emac_emb_smmu_cb_probe(pdev);

#ifdef CONFIG_QGKI_MSM_BOOT_TIME_MARKER
	place_marker("M - Ethernet probe start");
#endif

	ipc_emac_log_ctxt = ipc_log_context_create(IPCLOG_STATE_PAGES,
						   "emac", 0);
	if (!ipc_emac_log_ctxt)
		ETHQOSERR("Error creating logging context for emac\n");
	else
		ETHQOSDBG("IPC logging has been enabled for emac\n");
	ret = stmmac_get_platform_resources(pdev, &stmmac_res);
	if (ret)
		return ret;

	ethqos = devm_kzalloc(&pdev->dev, sizeof(*ethqos), GFP_KERNEL);
	if (!ethqos)
		return -ENOMEM;

	ethqos->pdev = pdev;

	plat_dat = stmmac_probe_config_dt(pdev,
					  &stmmac_res.mac, stmmac_res.ch);
	if (IS_ERR(plat_dat)) {
		dev_err(&pdev->dev, "dt configuration failed\n");
		return PTR_ERR(plat_dat);
	}

	plat_dat->bsp_priv = ethqos;
	plat_dat->tx_select_queue = dwmac_qcom_select_queue;
	plat_dat->get_plat_tx_coal_frames = dwmac_qcom_get_plat_tx_coal_frames;
	plat_dat->tso_en = of_property_read_bool(np, "snps,tso");

	if (of_property_read_bool(np, "emac-core-version")) {
		/* Read emac core version value from dtsi */
		ret = of_property_read_u32(np, "emac-core-version",
					   &ethqos->emac_ver);
		if (ret) {
			ETHQOSDBG(": resource emac-hw-ver! not in dtsi\n");
			ethqos->emac_ver = EMAC_HW_v3_0_0;
			WARN_ON(1);
		}
	} else {
		ethqos->emac_ver = EMAC_HW_v3_0_0;
	}
	ETHQOSDBG("emac_core_version = 0x%x\n", ethqos->emac_ver);

	if (of_property_read_bool(np, "qcom,arm-smmu")) {
		emac_emb_smmu_ctx.pdev_master = pdev;
		ret = of_platform_populate(np, qcom_ethqos_match,
					   NULL, &pdev->dev);

		if (ret)
			ETHQOSERR("Failed to populate EMAC platform\n");
		if (emac_emb_smmu_ctx.ret) {
			ETHQOSERR("smmu probe failed\n");
			of_platform_depopulate(&pdev->dev);
			ret = emac_emb_smmu_ctx.ret;
			emac_emb_smmu_ctx.ret = 0;
		}
	}

	plat_dat->stmmac_emb_smmu_ctx = emac_emb_smmu_ctx;

	/* Allocate workqueue */
	ethqos->wq = create_singlethread_workqueue("ethqos_wq");
	if (!ethqos->wq) {
		ETHQOSERR("Failed to create workqueue\n");
		ret = -ENOMEM;
		goto err_smmu;
	}

	mutex_init(&ethqos->lock);
	INIT_WORK(&ethqos->emac_fe_rdy_work, ethqos_emac_fe_ready_wq);
	INIT_WORK((struct work_struct *)&ethqos->emac_fe_work, emac_fe_ev_wq);
	INIT_LIST_HEAD(&ethqos->emac_fe_ev_q);

	ret = stmmac_dvr_probe(&pdev->dev, plat_dat, &stmmac_res);
	if (ret) {
		ETHQOSERR("Failed stmmac_dvr_probe - err = %d\n", ret);
		goto err_reg;
	}

	ndev = dev_get_drvdata(&ethqos->pdev->dev);
	priv = netdev_priv(ndev);

	priv->mac_addr = qcom_ethqos_mac_addr;
	priv->add_filter = qcom_ethqos_add_filter;
	priv->del_filter = qcom_ethqos_del_filter;

	priv->emac_state = EMAC_INIT_ST;

	while (count < 10) {
		if (!emac_ctrl_fe_register_ready_cb(ethqos_emac_fe_ready_cb,
						    (void *)ethqos))
			break;
		cond_resched();
		count++;
	}
	if (count == 10) {
		ret = -EINVAL;
		goto err_fe;
	}

#ifdef CONFIG_QGKI_MSM_BOOT_TIME_MARKER
	place_marker("M - Ethernet probe end");
#endif
	ETHQOSINFO("End\n");
	return 0;

err_fe:
	stmmac_pltfr_remove(pdev);
	platform_set_drvdata(pdev, NULL);
err_reg:
	destroy_workqueue(ethqos->wq);
	emac_emb_smmu_exit();
	mutex_destroy(&ethqos->lock);
err_smmu:
	of_platform_depopulate(&pdev->dev);
	ETHQOSERR("Ethernet probe exit with ret = %d\n", ret);
	if (ipc_emac_log_ctxt)
		ipc_log_context_destroy(ipc_emac_log_ctxt);
	ipc_emac_log_ctxt = NULL;

	return ret;
}

static int qcom_ethqos_remove(struct platform_device *pdev)
{
	struct qcom_ethqos *ethqos;
	int ret;

	if (of_device_is_compatible(pdev->dev.of_node,
				    "qcom,emac-smmu-embedded")) {
		of_platform_depopulate(&pdev->dev);
		return 0;
	}

	ethqos = get_stmmac_bsp_priv(&pdev->dev);
	if (!ethqos)
		return -ENODEV;

	ETHQOSINFO("Enter\n");
	qcom_ethqos_unregister_emac_fe_listener(ethqos, EMAC_DMA_DRV_UNMOUNT);
	cancel_work_sync(&ethqos->emac_fe_work);
	destroy_workqueue(ethqos->wq);
	mutex_destroy(&ethqos->lock);
	ret = stmmac_pltfr_remove(pdev);

	emac_emb_smmu_exit();

	platform_set_drvdata(pdev, NULL);
	of_platform_depopulate(&pdev->dev);

	if (ipc_emac_log_ctxt)
		ipc_log_context_destroy(ipc_emac_log_ctxt);
	ipc_emac_log_ctxt = NULL;

	return ret;
}

static void qcom_ethqos_shutdown(struct platform_device *pdev)
{
	struct net_device *dev = platform_get_drvdata(pdev);

	if (!dev)
		return;

	qcom_ethqos_remove(pdev);
}

static int qcom_ethqos_suspend(struct device *dev)
{
	struct qcom_ethqos *ethqos;
	struct net_device *ndev = NULL;
	struct stmmac_priv *priv = NULL;
	int ret;

	if (of_device_is_compatible(dev->of_node, "qcom,emac-smmu-embedded")) {
		ETHQOSDBG("smmu return\n");
		return 0;
	}

	ethqos = get_stmmac_bsp_priv(dev);
	if (!ethqos)
		return -ENODEV;

	ndev = dev_get_drvdata(dev);

	if (!ndev || !netif_running(ndev)) {
		ETHQOSINFO(" Suspend not possible\n");
		return 0;
	}

	priv = netdev_priv(ndev);
	ret = stmmac_suspend(dev);
	if (!ret) {
		ethqos->suspended = true;
		priv->emac_state = EMAC_INIT_ST;
	}

	emac_ctrl_fe_gvm_dma_stopped();
	qcom_ethqos_unregister_emac_fe_listener(ethqos, EMAC_DMA_DRV_SUSPEND);

	priv->boot_kpi = false;
	ETHQOSDBG(" ret = %d\n", ret);
	return ret;
}

static int qcom_ethqos_resume(struct device *dev)
{
	struct net_device *ndev = NULL;
	struct qcom_ethqos *ethqos;
	int ret;

	ETHQOSDBG("Resume Enter\n");
	if (of_device_is_compatible(dev->of_node, "qcom,emac-smmu-embedded"))
		return 0;

	ethqos = get_stmmac_bsp_priv(dev);

	if (!ethqos)
		return -ENODEV;

	ndev = dev_get_drvdata(dev);

	if (!ndev || !netif_running(ndev)) {
		ETHQOSINFO(" Resume not possible\n");
		return 0;
	}

	ret = emac_ctrl_fe_register_ready_cb(ethqos_emac_fe_ready_cb,
					     (void *)ethqos);
	ETHQOSDBG("emac_ctrl_fe_register_ready_cb return %d\n", ret);

	ETHQOSDBG("<--Resume Exit\n");
	return ret;
}

MODULE_DEVICE_TABLE(of, qcom_ethqos_match);

static const struct dev_pm_ops qcom_ethqos_pm_ops = {
	.suspend = qcom_ethqos_suspend,
	.resume = qcom_ethqos_resume,
};

static struct platform_driver qcom_ethqos_driver = {
	.probe  = qcom_ethqos_probe,
	.remove = qcom_ethqos_remove,
	.shutdown = qcom_ethqos_shutdown,
	.driver = {
		.name           = DRV_NAME,
		.pm		= &qcom_ethqos_pm_ops,
		.of_match_table = of_match_ptr(qcom_ethqos_match),
	},
};

static int __init qcom_ethqos_init_module(void)
{
	int ret = 0;

	ETHQOSINFO("\n");

	ret = platform_driver_register(&qcom_ethqos_driver);
	if (ret < 0) {
		ETHQOSINFO("qcom-ethqos: Driver registration failed");
		return ret;
	}

	ETHQOSINFO("\n");

	return ret;
}

static void __exit qcom_ethqos_exit_module(void)
{
	ETHQOSINFO("\n");

	platform_driver_unregister(&qcom_ethqos_driver);

	ETHQOSINFO("\n");
}

/*!
 * \brief Macro to register the driver registration function.
 *
 * \details A module always begin with either the init_module or the function
 * you specify with module_init call. This is the entry function for modules;
 * it tells the kernel what functionality the module provides and sets up the
 * kernel to run the module's functions when they're needed. Once it does this,
 * entry function returns and the module does nothing until the kernel wants
 * to do something with the code that the module provides.
 */

module_init(qcom_ethqos_init_module)

/*!
 * \brief Macro to register the driver un-registration function.
 *
 * \details All modules end by calling either cleanup_module or the function
 * you specify with the module_exit call. This is the exit function for modules;
 * it undoes whatever entry function did. It unregisters the functionality
 * that the entry function registered.
 */

module_exit(qcom_ethqos_exit_module)

MODULE_DESCRIPTION("Qualcomm Technologies, Inc.ETHQOS thin driver");
MODULE_LICENSE("GPL v2");
