// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/* Copyright (c) 2018 Synopsys, Inc. and/or its affiliates.
 * stmmac HW Interface Handling
 */

#include "common_thin.h"
#include "stmmac_thin.h"

static void stmmac_dwmac_mode_quirk(struct stmmac_priv *priv)
{
	dev_info(priv->device, "Ring mode enabled\n");
	priv->mode = STMMAC_RING_MODE;
}

static int stmmac_dwmac4_quirks(struct stmmac_priv *priv)
{
	stmmac_dwmac_mode_quirk(priv);
	return 0;
}

static const struct stmmac_hwif_entry {
	const void *desc;
	const void *dma;
	const void *mac;
	const void *mode;
	int (*setup)(struct stmmac_priv *priv);
	int (*quirks)(struct stmmac_priv *priv);
} stmmac_hw[] = {
	/* NOTE: New HW versions shall go to the end of this table */
	{
		.desc = &dwmac4_desc_ops,
		.dma = &dwmac4_dma_ops,
		.mac = &dwmac4_ops,
		.mode = NULL,
		.setup = dwmac4_setup,
		.quirks = stmmac_dwmac4_quirks,
	},
};

int stmmac_hwif_init(struct stmmac_priv *priv)
{
	const struct stmmac_hwif_entry *entry;
	struct mac_device_info *mac;
	bool needs_setup = true;
	int ret;

	mac = devm_kzalloc(priv->device, sizeof(*mac), GFP_KERNEL);

	if (!mac)
		return -ENOMEM;

	/* Fallback to generic HW */
	entry = &stmmac_hw[0];

	/* Only use generic HW helpers if needed */
	mac->desc = mac->desc ? : entry->desc;
	mac->dma = mac->dma ? : entry->dma;
	mac->mac = mac->mac ? : entry->mac;

	priv->hw = mac;

	/* Entry found */
	if (needs_setup) {
		ret = entry->setup(priv);
		if (ret)
			return ret;
	}

	/* Save quirks, if needed for posterior use */
	priv->hwif_quirks = entry->quirks;
	return 0;
}
