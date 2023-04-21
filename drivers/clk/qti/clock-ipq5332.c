// SPDX-License-Identifier: BSD-3-Clause
/*
 * Clock drivers for QTI IPQ5332
 *
 * (C) Copyright 2022 Sumit Garg <sumit.garg@linaro.org>
 *
 * Copyright (c) 2023, Qualcomm Innovation Center, Inc. All rights reserved.
 *
 */

#include <common.h>
#include <clk-uclass.h>
#include <dm.h>
#include <errno.h>
#include <asm/io.h>
#include <linux/bitops.h>
#include "clock-snapdragon.h"

#include <dt-bindings/clock/gcc-ipq5332.h>

/* GPLL0 clock control registers */
#define GPLL0_STATUS_ACTIVE BIT(31)

static struct vote_clk gcc_blsp1_ahb_clk = {
	.cbcr_reg = BLSP1_AHB_CBCR,
	.ena_vote = APCS_CLOCK_BRANCH_ENA_VOTE,
	.vote_bit = BIT(4) | BIT(2) | BIT(1),
};

static const struct bcr_regs sdc_regs = {
	.cfg_rcgr = SDCC1_APPS_CFG_RCGR,
	.cmd_rcgr = SDCC1_APPS_CMD_RCGR,
	.M = SDCC1_APPS_M,
	.N = SDCC1_APPS_N,
	.D = SDCC1_APPS_D,
};

static const struct bcr_regs uart1_regs = {
	.cfg_rcgr = BLSP1_UART_APPS_CFG_RCGR(0),
	.cmd_rcgr = BLSP1_UART_APPS_CMD_RCGR(0),
	.M = BLSP1_UART_APPS_M(0),
	.N = BLSP1_UART_APPS_N(0),
	.D = BLSP1_UART_APPS_D(0),
};

static const struct bcr_regs uart2_regs = {
	.cfg_rcgr = BLSP1_UART_APPS_CFG_RCGR(1),
	.cmd_rcgr = BLSP1_UART_APPS_CMD_RCGR(1),
	.M = BLSP1_UART_APPS_M(1),
	.N = BLSP1_UART_APPS_N(1),
	.D = BLSP1_UART_APPS_D(1),
};

static const struct bcr_regs uart3_regs = {
	.cfg_rcgr = BLSP1_UART_APPS_CFG_RCGR(2),
	.cmd_rcgr = BLSP1_UART_APPS_CMD_RCGR(2),
	.M = BLSP1_UART_APPS_M(2),
	.N = BLSP1_UART_APPS_N(2),
	.D = BLSP1_UART_APPS_D(2),
};

ulong msm_set_rate(struct clk *clk, ulong rate)
{
	struct msm_clk_priv *priv = dev_get_priv(clk->dev);

	switch (clk->id) {

	case GCC_BLSP1_UART0_APPS_CLK:
		/* UART: 115200 */
		clk_rcg_set_rate_mnd(priv->base, &uart1_regs, 0, 36, 15625,
				     SDCC1_SRC_SEL_GPLL0_OUT_MAIN);
		clk_enable_cbc(priv->base + BLSP1_UART_APPS_CBCR(0));
		break;
	case GCC_BLSP1_UART1_APPS_CLK:
		/* UART: 115200 */
		clk_rcg_set_rate_mnd(priv->base, &uart2_regs, 0, 36, 15625,
				     SDCC1_SRC_SEL_GPLL0_OUT_MAIN);
		clk_enable_cbc(priv->base + BLSP1_UART_APPS_CBCR(1));
		break;
	case GCC_BLSP1_UART2_APPS_CLK:
		/* UART: 115200 */
		clk_rcg_set_rate_mnd(priv->base, &uart3_regs, 0, 36, 15625,
				     SDCC1_SRC_SEL_GPLL0_OUT_MAIN);
		clk_enable_cbc(priv->base + BLSP1_UART_APPS_CBCR(2));
		break;
	case GCC_BLSP1_AHB_CLK:
		clk_enable_vote_clk(priv->base, &gcc_blsp1_ahb_clk);
		break;
	case GCC_SDCC1_APPS_CLK:
		/* SDCC1: 200MHz */
		clk_rcg_set_rate_mnd(priv->base, &sdc_regs, 6, 0, 0,
				     SDCC1_SRC_SEL_GPLL2_OUT_MAIN);
		clk_enable_cbc(priv->base + SDCC1_APPS_CBCR);
		break;
	case GCC_SDCC1_AHB_CLK:
		clk_enable_cbc(priv->base + SDCC1_AHB_CBCR);
		break;
	default:
		return 0;
	}

	return 0;
}

int msm_enable(struct clk *clk)
{
	switch (clk->id) {
	default:
		;
	}

	return 0;
}
