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

static const struct bcr_regs pcie_aux_regs = {
	.cfg_rcgr = GCC_PCIE_AUX_CFG_RCGR,
	.cmd_rcgr = GCC_PCIE_AUX_CMD_RCGR,
	.M = GCC_PCIE_AUX_M,
	.N = GCC_PCIE_AUX_N,
	.D = GCC_PCIE_AUX_D,
};

static const struct bcr_regs_v2 pcie0_axi_m_clk_regs = {
	.cfg_rcgr = GCC_PCIE0_AXI_M_CFG_RCGR,
	.cmd_rcgr = GCC_PCIE0_AXI_M_CMD_RCGR,
};

static const struct bcr_regs_v2 pcie1_axi_m_clk_regs = {
	.cfg_rcgr = GCC_PCIE1_AXI_M_CFG_RCGR,
	.cmd_rcgr = GCC_PCIE1_AXI_M_CMD_RCGR,
};

static const struct bcr_regs_v2 pcie2_axi_m_clk_regs = {
	.cfg_rcgr = GCC_PCIE2_AXI_M_CFG_RCGR,
	.cmd_rcgr = GCC_PCIE2_AXI_M_CMD_RCGR,
};

static const struct bcr_regs_v2 pcie3_axi_m_clk_regs = {
	.cfg_rcgr = GCC_PCIE3_AXI_M_CFG_RCGR,
	.cmd_rcgr = GCC_PCIE3_AXI_M_CMD_RCGR,
};

static const struct bcr_regs_v2 pcie0_axi_s_clk_regs = {
	.cfg_rcgr = GCC_PCIE0_AXI_S_CFG_RCGR,
	.cmd_rcgr = GCC_PCIE0_AXI_S_CMD_RCGR,
};

static const struct bcr_regs_v2 pcie1_axi_s_clk_regs = {
	.cfg_rcgr = GCC_PCIE1_AXI_S_CFG_RCGR,
	.cmd_rcgr = GCC_PCIE1_AXI_S_CMD_RCGR,
};

static const struct bcr_regs_v2 pcie2_axi_s_clk_regs = {
	.cfg_rcgr = GCC_PCIE2_AXI_S_CFG_RCGR,
	.cmd_rcgr = GCC_PCIE2_AXI_S_CMD_RCGR,
};

static const struct bcr_regs_v2 pcie3_axi_s_clk_regs = {
	.cfg_rcgr = GCC_PCIE3_AXI_S_CFG_RCGR,
	.cmd_rcgr = GCC_PCIE3_AXI_S_CMD_RCGR,
};

static const struct bcr_regs_v2 pcie0_rchng_clk_regs = {
	.cfg_rcgr = GCC_PCIE0_RCHNG_CFG_RCGR,
	.cmd_rcgr = GCC_PCIE0_RCHNG_CMD_RCGR,
};

static const struct bcr_regs_v2 pcie1_rchng_clk_regs = {
	.cfg_rcgr = GCC_PCIE1_RCHNG_CFG_RCGR,
	.cmd_rcgr = GCC_PCIE1_RCHNG_CMD_RCGR,
};

static const struct bcr_regs_v2 pcie2_rchng_clk_regs = {
	.cfg_rcgr = GCC_PCIE2_RCHNG_CFG_RCGR,
	.cmd_rcgr = GCC_PCIE2_RCHNG_CMD_RCGR,
};

static const struct bcr_regs_v2 pcie3_rchng_clk_regs = {
	.cfg_rcgr = GCC_PCIE3_RCHNG_CFG_RCGR,
	.cmd_rcgr = GCC_PCIE3_RCHNG_CMD_RCGR,
};

static const struct bcr_regs sdc_regs = {
	.cfg_rcgr = SDCC1_APPS_CFG_RCGR,
	.cmd_rcgr = SDCC1_APPS_CMD_RCGR,
	.M = SDCC1_APPS_M,
	.N = SDCC1_APPS_N,
	.D = SDCC1_APPS_D,
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
	case GCC_USB0_MASTER_CLK:
		break;
	case GCC_USB0_MOCK_UTMI_CLK:
		break;
	case GCC_USB0_AUX_CLK:
		break;
	case GCC_USB1_MOCK_UTMI_CLK:
		break;

	/* NSS clocks */
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
	case GCC_SDCC1_APPS_CLK:
		/* SDCC1: 192 MHz */
		clk_rcg_set_rate_mnd(priv->base, &sdc_regs, 6, 0, 0,
				     SDCC1_SRC_SEL_GPLL2_OUT_MAIN);
		break;
	case GCC_PCIE_AUX_CLK:
		/* GCC_PCIE_AUX_CLK: 20 MHz */
		clk_rcg_set_rate_mnd(priv->base, &pcie_aux_regs,
					16, 5, 2, PCIE_GPLL0_OUT_AUX);
		break;
	case GCC_PCIE0_AXI_M_CLK:
		/* GCC_PCIE0_AXI_M_CLK: 240 MHz */
		clk_rcg_set_rate_v2(priv->base, &pcie0_axi_m_clk_regs,
					9, 0, PCIE_GPLL4_OUT_MAIN);
		break;
	case GCC_PCIE1_AXI_M_CLK:
		/* GCC_PCIE1_AXI_M_CLK: 240 MHz */
		clk_rcg_set_rate_v2(priv->base, &pcie1_axi_m_clk_regs,
					9, 0, PCIE_GPLL4_OUT_MAIN);
		break;
	case GCC_PCIE2_AXI_M_CLK:
		/* GCC_PCIE2_AXI_M_CLK: 266.67 MHz */
		clk_rcg_set_rate_v2(priv->base, &pcie2_axi_m_clk_regs,
					8, 0, PCIE_GPLL4_OUT_MAIN);
		break;
	case GCC_PCIE3_AXI_M_CLK:
		/* GCC_PCIE3_AXI_M_CLK: 266.67 MHz */
		clk_rcg_set_rate_v2(priv->base, &pcie3_axi_m_clk_regs,
					8, 0, PCIE_GPLL4_OUT_MAIN);
		break;
	case GCC_PCIE0_AXI_S_CLK:
		/* GCC_PCIE0_AXI_S_CLK: 240 MHz */
		clk_rcg_set_rate_v2(priv->base, &pcie0_axi_s_clk_regs,
					9, 0, PCIE_GPLL4_OUT_MAIN);
		break;
	case GCC_PCIE1_AXI_S_CLK:
		/* GCC_PCIE1_AXI_S_CLK: 240 MHz */
		clk_rcg_set_rate_v2(priv->base, &pcie1_axi_s_clk_regs,
					9, 0, PCIE_GPLL4_OUT_MAIN);
		break;
	case GCC_PCIE2_AXI_S_CLK:
		/* GCC_PCIE2_AXI_S_CLK: 240 MHz */
		clk_rcg_set_rate_v2(priv->base, &pcie2_axi_s_clk_regs,
					9, 0, PCIE_GPLL4_OUT_MAIN);
		break;
	case GCC_PCIE3_AXI_S_CLK:
		/* GCC_PCIE3_AXI_S_CLK: 240 MHz */
		clk_rcg_set_rate_v2(priv->base, &pcie3_axi_s_clk_regs,
					9, 0, PCIE_GPLL4_OUT_MAIN);
		break;
	case GCC_PCIE0_RCHNG_CLK:
		/* GCC_PCIE0_RCHNG_CLK: 100 MHz */
		clk_rcg_set_rate_v2(priv->base, &pcie0_rchng_clk_regs,
					15, 0, PCIE_GPLL0_OUT_MAIN);
		break;
	case GCC_PCIE1_RCHNG_CLK:
		/* GCC_PCIE1_RCHNG_CLK: 100 MHz */
		clk_rcg_set_rate_v2(priv->base, &pcie1_rchng_clk_regs,
					15, 0, PCIE_GPLL0_OUT_MAIN);
		break;
	case GCC_PCIE2_RCHNG_CLK:
		/* GCC_PCIE2_RCHNG_CLK: 100 MHz */
		clk_rcg_set_rate_v2(priv->base, &pcie2_rchng_clk_regs,
					15, 0, PCIE_GPLL0_OUT_MAIN);
		break;
	case GCC_PCIE3_RCHNG_CLK:
		/* GCC_PCIE3_RCHNG_CLK: 100 MHz */
		clk_rcg_set_rate_v2(priv->base, &pcie3_rchng_clk_regs,
					15, 0, PCIE_GPLL0_OUT_MAIN);
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
	case GCC_SDCC1_APPS_CLK:
		clk_enable_cbc(priv->base + GCC_SDCC1_APPS_CBCR);
		break;
	case GCC_SDCC1_AHB_CLK:
		clk_enable_cbc(priv->base + GCC_SDCC1_AHB_CBCR);
		break;
	case GCC_USB0_MASTER_CLK:
		clk_enable_cbc(priv->base + GCC_USB0_MASTER_CBCR);
		break;
	case GCC_USB0_MOCK_UTMI_CLK:
		clk_enable_cbc(priv->base + GCC_USB0_MOCK_UTMI_CBCR);
		break;
	case GCC_USB0_SLEEP_CLK:
		clk_enable_cbc(priv->base + GCC_USB0_SLEEP_CBCR);
		break;
	case GCC_USB0_AUX_CLK:
		clk_enable_cbc(priv->base + GCC_USB0_AUX_CBCR);
		break;
	case GCC_USB0_PHY_CFG_AHB_CLK:
		clk_enable_cbc(priv->base + GCC_USB0_PHY_CFG_AHB_CBCR);
		break;
	case GCC_USB1_MASTER_CLK:
		clk_enable_cbc(priv->base + GCC_USB1_MASTER_CBCR);
		break;
	case GCC_USB1_MOCK_UTMI_CLK:
		clk_enable_cbc(priv->base + GCC_USB1_MOCK_UTMI_CBCR);
		break;
	case GCC_USB1_SLEEP_CLK:
		clk_enable_cbc(priv->base + GCC_USB1_SLEEP_CBCR);
		break;
	case GCC_USB1_PHY_CFG_AHB_CLK:
		clk_enable_cbc(priv->base + GCC_USB1_PHY_CFG_AHB_CBCR);
		break;
	case GCC_USB0_PIPE_CLK:
		clk_enable_cbc(priv->base + GCC_USB0_PIPE_CBCR);
		break;
	case GCC_CNOC_USB_CLK:
		clk_enable_cbc(priv->base + GCC_CNOC_USB_CBCR);
		break;

	/* NSS clocks */
	case NSS_CC_PPE_SWITCH_CLK:
		clk_enable_cbc(priv->base + NSS_CC_PPE_SWITCH_CBCR);
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
	case GCC_PCIE0_AHB_CLK:
		clk_enable_cbc(priv->base + GCC_PCIE0_AHB_CBCR);
		break;
	case GCC_PCIE0_AUX_CLK:
		clk_enable_cbc(priv->base + GCC_PCIE0_AUX_CBCR);
		break;
	case GCC_PCIE0_AXI_M_CLK:
		clk_enable_cbc(priv->base + GCC_PCIE0_AXI_M_CBCR);
		break;
	case GCC_PCIE0_AXI_S_BRIDGE_CLK:
		clk_enable_cbc(priv->base + GCC_PCIE0_AXI_S_BRIDGE_CBCR);
		break;
	case GCC_PCIE0_AXI_S_CLK:
		clk_enable_cbc(priv->base + GCC_PCIE0_AXI_S_CBCR);
		break;
	case GCC_PCIE0_PIPE_CLK:
		break;
	case GCC_PCIE1_AHB_CLK:
		clk_enable_cbc(priv->base + GCC_PCIE1_AHB_CBCR);
		break;
	case GCC_PCIE1_AUX_CLK:
		clk_enable_cbc(priv->base + GCC_PCIE1_AUX_CBCR);
		break;
	case GCC_PCIE1_AXI_M_CLK:
		clk_enable_cbc(priv->base + GCC_PCIE1_AXI_M_CBCR);
		break;
	case GCC_PCIE1_AXI_S_BRIDGE_CLK:
		clk_enable_cbc(priv->base + GCC_PCIE1_AXI_S_BRIDGE_CBCR);
		break;
	case GCC_PCIE1_AXI_S_CLK:
		clk_enable_cbc(priv->base + GCC_PCIE1_AXI_S_CBCR);
		break;
	case GCC_PCIE1_PIPE_CLK:
		break;
	case GCC_PCIE2_AHB_CLK:
		clk_enable_cbc(priv->base + GCC_PCIE2_AHB_CBCR);
		break;
	case GCC_PCIE2_AUX_CLK:
		clk_enable_cbc(priv->base + GCC_PCIE2_AUX_CBCR);
		break;
	case GCC_PCIE2_AXI_M_CLK:
		clk_enable_cbc(priv->base + GCC_PCIE2_AXI_M_CBCR);
		break;
	case GCC_PCIE2_AXI_S_BRIDGE_CLK:
		clk_enable_cbc(priv->base + GCC_PCIE2_AXI_S_BRIDGE_CBCR);
		break;
	case GCC_PCIE2_AXI_S_CLK:
		clk_enable_cbc(priv->base + GCC_PCIE2_AXI_S_CBCR);
		break;
	case GCC_PCIE2_PIPE_CLK:
		break;
	case GCC_PCIE3_AHB_CLK:
		clk_enable_cbc(priv->base + GCC_PCIE3_AHB_CBCR);
		break;
	case GCC_PCIE3_AUX_CLK:
		clk_enable_cbc(priv->base + GCC_PCIE3_AUX_CBCR);
		break;
	case GCC_PCIE3_AXI_M_CLK:
		clk_enable_cbc(priv->base + GCC_PCIE3_AXI_M_CBCR);
		break;
	case GCC_PCIE3_AXI_S_BRIDGE_CLK:
		clk_enable_cbc(priv->base + GCC_PCIE3_AXI_S_BRIDGE_CBCR);
		break;
	case GCC_PCIE3_AXI_S_CLK:
		clk_enable_cbc(priv->base + GCC_PCIE3_AXI_S_CBCR);
		break;
	case GCC_PCIE3_PIPE_CLK:
		break;
	case GCC_CNOC_PCIE0_1LANE_S_CLK:
		clk_enable_cbc(priv->base + GCC_CNOC_PCIE0_1LANE_S_CBCR);
		break;
	case GCC_CNOC_PCIE1_1LANE_S_CLK:
		clk_enable_cbc(priv->base + GCC_CNOC_PCIE1_1LANE_S_CBCR);
		break;
	case GCC_CNOC_PCIE2_2LANE_S_CLK:
		clk_enable_cbc(priv->base + GCC_CNOC_PCIE2_2LANE_S_CBCR);
		break;
	case GCC_CNOC_PCIE3_2LANE_S_CLK:
		clk_enable_cbc(priv->base + GCC_CNOC_PCIE3_2LANE_S_CBCR);
		break;
	case GCC_ANOC_PCIE0_1LANE_M_CLK:
		clk_enable_cbc(priv->base + GCC_ANOC_PCIE0_1LANE_M_CBCR);
		break;
	case GCC_ANOC_PCIE1_1LANE_M_CLK:
		clk_enable_cbc(priv->base + GCC_ANOC_PCIE1_1LANE_M_CBCR);
		break;
	case GCC_ANOC_PCIE2_2LANE_M_CLK:
		clk_enable_cbc(priv->base + GCC_ANOC_PCIE2_2LANE_M_CBCR);
		break;
	case GCC_ANOC_PCIE3_2LANE_M_CLK:
		clk_enable_cbc(priv->base + GCC_ANOC_PCIE3_2LANE_M_CBCR);
		break;
	default:
	}

	return 0;
}
