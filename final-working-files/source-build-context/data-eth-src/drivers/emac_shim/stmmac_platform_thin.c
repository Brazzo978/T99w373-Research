// SPDX-License-Identifier: GPL-2.0-only
/******************************************************************************
 * This contains the functions to handle the platform driver.
 *
 * Copyright (C) 2007-2011  STMicroelectronics Ltd
 *
 * Author: Giuseppe Cavallaro <peppe.cavallaro@st.com>
 *****************************************************************************/

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_net.h>
#include <linux/of_device.h>

#include "stmmac_thin.h"
#include "stmmac_platform_thin.h"

#define VM_TRAFFIC_CHANNEL 1

#ifdef CONFIG_OF

/**
 * stmmac_mtl_setup - parse DT parameters for multiple queues configuration
 * @pdev: platform device
 */
static int stmmac_mtl_setup(struct platform_device *pdev,
			    struct plat_stmmacenet_data *plat,
			    u32 queue)
{
	struct device_node *q_node = NULL;
	struct device_node *rx_node = NULL;
	struct device_node *tx_node = NULL;
	struct device_node *np = pdev->dev.of_node;

	/* For backwards-compatibility with device trees that don't have any
	 * snps,mtl-rx-config or snps,mtl-tx-config properties, we fall back
	 * to one RX and TX queues each.
	 */
	plat->rx_queues_to_use = 1;
	plat->tx_queues_to_use = 1;

	rx_node = of_parse_phandle(np, "snps,mtl-rx-config", 0);
	if (!rx_node)
		return  -EINVAL;

	tx_node = of_parse_phandle(np, "snps,mtl-tx-config", 0);
	if (!tx_node) {
		of_node_put(rx_node);
		return -EINVAL;
	}

	/* We only support one pair of RX/TX queue. */
	/* First Queue must always be in DCB mode. As MTL_QUEUE_DCB = 1 we need
	 * to always set this, otherwise Queue will be classified as AVB
	 * (because MTL_QUEUE_AVB = 0).
	 */
	plat->rx_queues_cfg[queue].mode_to_use = MTL_QUEUE_DCB;
	plat->tx_queues_cfg[queue].mode_to_use = MTL_QUEUE_DCB;

	/* Processing RX queues common config */
	if (of_property_read_bool(rx_node, "snps,rx-sched-sp"))
		plat->rx_sched_algorithm = MTL_RX_ALGORITHM_SP;
	else if (of_property_read_bool(rx_node, "snps,rx-sched-wsp"))
		plat->rx_sched_algorithm = MTL_RX_ALGORITHM_WSP;
	else
		plat->rx_sched_algorithm = MTL_RX_ALGORITHM_SP;

	/* Processing individual RX queue config */
	q_node = of_get_next_child(rx_node, NULL);
	if (q_node) {
		if (of_property_read_bool(q_node, "snps,avb-algorithm"))
			plat->rx_queues_cfg[queue].mode_to_use = MTL_QUEUE_AVB;
		else
			plat->rx_queues_cfg[queue].mode_to_use = MTL_QUEUE_DCB;

		if (of_property_read_u32(q_node, "snps,map-to-dma-channel",
					 &plat->rx_queues_cfg[queue].chan))
			plat->rx_queues_cfg[queue].chan = queue;

		if (of_property_read_u32(q_node, "snps,priority",
					 &plat->rx_queues_cfg[queue].prio)) {
			plat->rx_queues_cfg[queue].prio = 0;
			plat->rx_queues_cfg[queue].use_prio = false;
		} else {
			plat->rx_queues_cfg[queue].use_prio = true;
		}

		/* RX queue specific packet type routing */
		if (of_property_read_bool(q_node, "snps,route-avcp"))
			plat->rx_queues_cfg[queue].pkt_route = PACKET_AVCPQ;
		else if (of_property_read_bool(q_node, "snps,route-ptp"))
			plat->rx_queues_cfg[queue].pkt_route = PACKET_PTPQ;
		else if (of_property_read_bool(q_node, "snps,route-dcbcp"))
			plat->rx_queues_cfg[queue].pkt_route = PACKET_DCBCPQ;
		else if (of_property_read_bool(q_node, "snps,route-up"))
			plat->rx_queues_cfg[queue].pkt_route = PACKET_UPQ;
		else if (of_property_read_bool(q_node,
					       "snps,route-multi-broad"))
			plat->rx_queues_cfg[queue].pkt_route = PACKET_MCBCQ;
		else
			plat->rx_queues_cfg[queue].pkt_route = 0x0;

		of_node_put(q_node);
	}
	/* Processing TX queues common config */
	if (of_property_read_bool(tx_node, "snps,tx-sched-wrr"))
		plat->tx_sched_algorithm = MTL_TX_ALGORITHM_WRR;
	else if (of_property_read_bool(tx_node, "snps,tx-sched-wfq"))
		plat->tx_sched_algorithm = MTL_TX_ALGORITHM_WFQ;
	else if (of_property_read_bool(tx_node, "snps,tx-sched-dwrr"))
		plat->tx_sched_algorithm = MTL_TX_ALGORITHM_DWRR;
	else
		plat->tx_sched_algorithm = MTL_TX_ALGORITHM_SP;

	q_node = of_get_next_child(tx_node, NULL);
	if (q_node) {
		if (of_property_read_u32(q_node, "snps,weight",
					 &plat->tx_queues_cfg[queue].weight))
			plat->tx_queues_cfg[queue].weight = 0x10 + queue;

		if (of_property_read_bool(q_node, "snps,avb-algorithm")) {
			plat->tx_queues_cfg[queue].mode_to_use = MTL_QUEUE_AVB;

			/* Credit Base Shaper parameters used by AVB */
			if (of_property_read_u32
				(q_node, "snps,send_slope",
				 &plat->tx_queues_cfg[queue].send_slope))
				plat->tx_queues_cfg[queue].send_slope = 0x0;
			if (of_property_read_u32
				(q_node, "snps,idle_slope",
				 &plat->tx_queues_cfg[queue].idle_slope))
				plat->tx_queues_cfg[queue].idle_slope = 0x0;
			if (of_property_read_u32
				(q_node, "snps,high_credit",
				 &plat->tx_queues_cfg[queue].high_credit))
				plat->tx_queues_cfg[queue].high_credit = 0x0;
			if (of_property_read_u32
				(q_node, "snps,low_credit",
				 &plat->tx_queues_cfg[queue].low_credit))
				plat->tx_queues_cfg[queue].low_credit = 0x0;
		} else {
			plat->tx_queues_cfg[queue].mode_to_use = MTL_QUEUE_DCB;
		}

		if (of_property_read_u32(q_node, "snps,priority",
					 &plat->tx_queues_cfg[queue].prio)) {
			plat->tx_queues_cfg[queue].prio = 0;
			plat->tx_queues_cfg[queue].use_prio = false;
		} else {
			plat->tx_queues_cfg[queue].use_prio = true;
		}

		of_node_put(q_node);
	}

	of_node_put(rx_node);
	of_node_put(tx_node);

	return 0;
}

/**
 * stmmac_probe_config_dt - parse device-tree driver parameters
 * @pdev: platform_device structure
 * @mac: MAC address to use
 * Description:
 * this function is to read the driver parameters from device-tree and
 * set some private fields that will be used by the main at runtime.
 */
struct plat_stmmacenet_data *
stmmac_probe_config_dt(struct platform_device *pdev, const char **mac, u32 ch)
{
	struct device_node *np = pdev->dev.of_node;
	struct plat_stmmacenet_data *plat;
	struct stmmac_dma_cfg *dma_cfg;
	int rc;

	plat = devm_kzalloc(&pdev->dev, sizeof(*plat), GFP_KERNEL);
	if (!plat)
		return ERR_PTR(-ENOMEM);

	*mac = of_get_mac_address(np);
	if (IS_ERR(*mac)) {
		if (PTR_ERR(*mac) == -EPROBE_DEFER)
			return ERR_CAST(*mac);

		*mac = NULL;
	}

	of_property_read_u32(np, "tx-fifo-depth", &plat->tx_fifo_size);

	of_property_read_u32(np, "rx-fifo-depth", &plat->rx_fifo_size);

	plat->force_sf_dma_mode =
		of_property_read_bool(np, "snps,force_sf_dma_mode");

	/* Set the maxmtu to a default of JUMBO_LEN in case the
	 * parameter is not present in the device tree.
	 */
	plat->maxmtu = JUMBO_LEN;
	plat->tso_en = of_property_read_bool(np, "snps,tso");

	dma_cfg = devm_kzalloc(&pdev->dev, sizeof(*dma_cfg),
			       GFP_KERNEL);
	if (!dma_cfg)
		return ERR_PTR(-ENOMEM);
	plat->dma_cfg = dma_cfg;

	of_property_read_u32(np, "snps,pbl", &dma_cfg->pbl);
	if (!dma_cfg->pbl)
		dma_cfg->pbl = DEFAULT_DMA_PBL;
	of_property_read_u32(np, "snps,txpbl", &dma_cfg->txpbl);
	of_property_read_u32(np, "snps,rxpbl", &dma_cfg->rxpbl);
	dma_cfg->pblx8 = !of_property_read_bool(np, "snps,no-pbl-x8");

	plat->force_thresh_dma_mode =
		of_property_read_bool(np, "snps,force_thresh_dma_mode");
	if (plat->force_thresh_dma_mode) {
		plat->force_sf_dma_mode = 0;
		dev_warn(&pdev->dev,
			 "force_sf_dma_mode is ignored if force_thresh_dma_mode is set.\n");
	}

	rc = stmmac_mtl_setup(pdev, plat, ch);
	if (rc)
		return ERR_PTR(rc);

	plat->stmmac_rst = devm_reset_control_get(&pdev->dev,
						  STMMAC_RESOURCE_NAME);
	if (IS_ERR(plat->stmmac_rst)) {
		if (PTR_ERR(plat->stmmac_rst) == -EPROBE_DEFER)
			goto error_hw_init;

		dev_info(&pdev->dev, "no reset control found\n");
		plat->stmmac_rst = NULL;
	}

	return plat;

error_hw_init:
	return ERR_PTR(-EPROBE_DEFER);
}

#else
struct plat_stmmacenet_data *
stmmac_probe_config_dt(struct platform_device *pdev, const char **mac)
{
	return ERR_PTR(-EINVAL);
}
#endif /* CONFIG_OF */
EXPORT_SYMBOL_GPL(stmmac_probe_config_dt);

int stmmac_get_platform_resources(struct platform_device *pdev,
				  struct stmmac_resources *stmmac_res)
{
	struct resource *res;
	int i = 0;

	memset(stmmac_res, 0, sizeof(*stmmac_res));

	/* Get IRQ information early to have an ability to ask for deferred
	 * probe if needed before we went too far with resource allocation.
	 */
	stmmac_res->irq[0] = platform_get_irq_byname(pdev, "tx_rx_ch0_intr");
	stmmac_res->irq[1] = platform_get_irq_byname(pdev, "tx_rx_ch1_intr");
	stmmac_res->irq[2] = platform_get_irq_byname(pdev, "tx_rx_ch2_intr");
	stmmac_res->irq[3] = platform_get_irq_byname(pdev, "tx_rx_ch3_intr");
	stmmac_res->irq[4] = platform_get_irq_byname(pdev, "tx_rx_ch4_intr");
	stmmac_res->irq[5] = platform_get_irq_byname(pdev, "tx_rx_ch5_intr");
	stmmac_res->irq[6] = platform_get_irq_byname(pdev, "tx_rx_ch6_intr");
	stmmac_res->irq[7] = platform_get_irq_byname(pdev, "tx_rx_ch7_intr");
	for (i = 0; i < MAX_NUM_CH; i++) {
		if (stmmac_res->irq[i] < 0) {
			dev_warn(&pdev->dev,
				 "ch irq [%d] not configured\n", i);
			stmmac_res->irq[i] = 0;
		}
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "No resource for the device\n");
		return  -EINVAL;
	}
	stmmac_res->addr = devm_ioremap_resource(&pdev->dev, res);
	dev_info(&pdev->dev, "stmmac regs virt=0x%x, regs phy=0x%x\n",
		 stmmac_res->addr, res->start);

	if (of_property_read_u32(pdev->dev.of_node,
				 "queue", &stmmac_res->ch)) {
		dev_warn(&pdev->dev, "queue is not in dtsi\n");
		stmmac_res->ch = VM_TRAFFIC_CHANNEL;
	}
	return PTR_ERR_OR_ZERO(stmmac_res->addr);
}
EXPORT_SYMBOL_GPL(stmmac_get_platform_resources);

/**
 * stmmac_pltfr_remove
 * @pdev: platform device pointer
 * Description: this function calls the main to free the net resources
 * and calls the platforms hook and release the resources (e.g. mem).
 */
int stmmac_pltfr_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct stmmac_priv *priv = netdev_priv(ndev);
	struct plat_stmmacenet_data *plat = priv->plat;
	int ret = stmmac_dvr_remove(&pdev->dev);

	if (plat->exit)
		plat->exit(pdev, plat->bsp_priv);

	return ret;
}
EXPORT_SYMBOL_GPL(stmmac_pltfr_remove);

MODULE_DESCRIPTION("STMMAC 10/100/1000 Ethernet platform support");
MODULE_AUTHOR("Giuseppe Cavallaro <peppe.cavallaro@st.com>");
MODULE_LICENSE("GPL v2");
