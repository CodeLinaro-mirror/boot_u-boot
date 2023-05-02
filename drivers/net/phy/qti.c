// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <common.h>
#include <errno.h>
#include <phy.h>

#include "qti.h"
#include "qti_8x8x.h"

int phy_qti_init(void)
{
#ifdef CONFIG_PHY_QTI_8X8X
	phy_8x8x_init();
#endif
	return 0;
}
