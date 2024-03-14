// SPDX-License-Identifier: BSD-3-Clause
/*
 * Clock drivers for QTI DEVSOC
 *
 * (C) Copyright 2022 Sumit Garg <sumit.garg@linaro.org>
 *
 * Copyright (c) 2023-2024, Qualcomm Innovation Center, Inc. All rights reserved.
 *
 */

#include <common.h>
#include <clk-uclass.h>
#include <dm.h>
#include <errno.h>
#include <asm/io.h>
#include <linux/bitops.h>
#include <dm/device-internal.h>
#include "clock-snapdragon.h"

#include <dt-bindings/clock/gcc-devsoc.h>

static const struct bcr_regs_v2 nss_cc_ppe_regs = {
	.cfg_rcgr = NSS_CC_PPE_CFG_RCGR,
	.cmd_rcgr = NSS_CC_PPE_CMD_RCGR,
};

static const struct bcr_regs_v2 nss_cc_ce_regs = {
	.cfg_rcgr = NSS_CC_CE_CFG_RCGR,
	.cmd_rcgr = NSS_CC_CE_CMD_RCGR,
};

static const struct bcr_regs_v2 nss_cc_cfg_regs = {
	.cfg_rcgr = NSS_CC_CFG_CFG_RCGR,
	.cmd_rcgr = NSS_CC_CFG_CMD_RCGR,
};

int msm_set_parent(struct clk *clk, struct clk* parent)
{
	assert(clk);
	assert(parent);
	clk->dev->parent = parent->dev;
	dev_set_uclass_priv(parent->dev, parent);
	return 0;
}

ulong msm_get_rate(struct clk *clk)
{
	return (ulong)clk->rate;
}

ulong msm_set_rate(struct clk *clk, ulong rate)
{
	struct msm_clk_priv *priv = dev_get_priv(clk->dev);
	int ret;

	switch (clk->id) {
	case NSS_CC_PPE_CLK:
		clk_rcg_set_rate_v2(priv->base, &nss_cc_ppe_regs,
				1, 0, NSS_CC_PPE_SRC_SEL_CMN_PLL_NSS_CLK_375M);
		break;
	case NSS_CC_CE_CLK:
		clk_rcg_set_rate_v2(priv->base, &nss_cc_ce_regs,
				1, 0, NSS_CC_PPE_SRC_SEL_CMN_PLL_NSS_CLK_375M);
		break;
	case NSS_CC_CFG_CLK:
		clk_rcg_set_rate_v2(priv->base, &nss_cc_cfg_regs,
				15, 0, NSS_CC_PPE_SRC_SEL_GCC_GPLL0_OUT_AUX);
		break;
	default:
		ret = 0;
	}

	return 0;
}

int msm_enable(struct clk *clk)
{
	struct msm_clk_priv *priv = dev_get_priv(clk->dev);

	switch (clk->id) {
	case GCC_NSSCFG_CLK:
		clk_enable_cbc(priv->base + GCC_NSSCFG_CBCR);
		break;
	case GCC_NSSNOC_MEMNOC_CLK:
		clk_enable_cbc(priv->base + GCC_NSSNOC_MEMNOC_CBCR);
		break;
	case GCC_NSSNOC_MEMNOC_1_CLK:
		clk_enable_cbc(priv->base + GCC_NSSNOC_MEMNOC_1_CBCR);
		break;
	case NSS_CC_PPE_SWITCH_CLK:
		clk_enable_cbc(priv->base + NSS_CC_PPE_SWITCH_CBCR);
		break;
	case GCC_SDCC1_APPS_CLK:
		clk_enable_cbc(priv->base + GCC_SDCC1_APPS_CBCR);
		break;
	case GCC_SDCC1_AHB_CLK:
		clk_enable_cbc(priv->base + GCC_SDCC1_AHB_CBCR);
		break;
	case NSS_CC_PPE_EDMA_CLK:
		clk_enable_cbc(priv->base + NSS_CC_PPE_EDMA_CBCR);
		break;
	case NSS_CC_PPE_EDMA_CFG_CLK:
		clk_enable_cbc(priv->base + NSS_CC_PPE_EDMA_CFG_CBCR);
		break;
	case NSS_CC_PORT1_MAC_CLK:
		clk_enable_cbc(priv->base + NSS_CC_PORT1_MAC_CBCR);
		break;
	case NSS_CC_PORT2_MAC_CLK:
		clk_enable_cbc(priv->base + NSS_CC_PORT2_MAC_CBCR);
		break;
	case NSS_CC_PORT3_MAC_CLK:
		clk_enable_cbc(priv->base + NSS_CC_PORT3_MAC_CBCR);
		break;
	case NSS_CC_NSSNOC_PPE_CLK:
		clk_enable_cbc(priv->base + NSS_CC_NSSNOC_PPE_CBCR);
		break;
	case NSS_CC_PORT1_RX_CLK:
		clk_enable_cbc(priv->base + NSS_CC_PORT1_RX_CBCR);
		break;
	case NSS_CC_PORT1_TX_CLK:
		clk_enable_cbc(priv->base + NSS_CC_PORT1_TX_CBCR);
		break;
	case NSS_CC_PORT2_RX_CLK:
		clk_enable_cbc(priv->base + NSS_CC_PORT2_RX_CBCR);
		break;
	case NSS_CC_PORT2_TX_CLK:
		clk_enable_cbc(priv->base + NSS_CC_PORT2_TX_CBCR);
		break;
	case NSS_CC_PORT3_RX_CLK:
		clk_enable_cbc(priv->base + NSS_CC_PORT3_RX_CBCR);
		break;
	case NSS_CC_PORT3_TX_CLK:
		clk_enable_cbc(priv->base + NSS_CC_PORT3_TX_CBCR);
		break;
	case NSS_CC_UNIPHY_PORT1_RX_CLK:
		clk_enable_cbc(priv->base + NSS_CC_UNIPHY_PORT1_RX_CBCR);
		break;
	case NSS_CC_UNIPHY_PORT1_TX_CLK:
		clk_enable_cbc(priv->base + NSS_CC_UNIPHY_PORT1_TX_CBCR);
		break;
	case NSS_CC_UNIPHY_PORT2_RX_CLK:
		clk_enable_cbc(priv->base + NSS_CC_UNIPHY_PORT2_RX_CBCR);
		break;
	case NSS_CC_UNIPHY_PORT2_TX_CLK:
		clk_enable_cbc(priv->base + NSS_CC_UNIPHY_PORT2_TX_CBCR);
		break;
	case NSS_CC_UNIPHY_PORT3_RX_CLK:
		clk_enable_cbc(priv->base + NSS_CC_UNIPHY_PORT3_RX_CBCR);
		break;
	case NSS_CC_UNIPHY_PORT3_TX_CLK:
		clk_enable_cbc(priv->base + NSS_CC_UNIPHY_PORT3_TX_CBCR);
		break;
	case NSS_CC_CE_APB_CLK:
		clk_enable_cbc(priv->base + NSS_CC_CE_APB_CBCR);
		break;
	case NSS_CC_CE_AXI_CLK:
		clk_enable_cbc(priv->base + NSS_CC_CE_AXI_CBCR);
		break;
	case NSS_CC_NSSNOC_CE_APB_CLK:
		clk_enable_cbc(priv->base + NSS_CC_NSSNOC_CE_APB_CBCR);
		break;
	case NSS_CC_NSSNOC_CE_AXI_CLK:
		clk_enable_cbc(priv->base + NSS_CC_NSSNOC_CE_AXI_CBCR);
		break;
	case NSS_CC_NSS_CSR_CLK:
		clk_enable_cbc(priv->base + NSS_CC_NSS_CSR_CBCR);
		break;
	case NSS_CC_NSSNOC_NSS_CSR_CLK:
		clk_enable_cbc(priv->base + NSS_CC_NSSNOC_NSS_CSR_CBCR);
		break;
	case NSS_CC_PPE_SWITCH_IPE_CLK:
		clk_enable_cbc(priv->base + NSS_CC_PPE_SWITCH_IPE_CBCR);
		break;
	default:
	}

	return 0;
}
