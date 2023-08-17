// SPDX-License-Identifier: BSD-3-Clause
/*
 * Clock drivers for QTI DEVSOC
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
#include <dm/device-internal.h>
#include "clock-snapdragon.h"

#include <dt-bindings/clock/gcc-devsoc.h>

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
	return 0;
}

int msm_enable(struct clk *clk)
{
	return 0;
}
