/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <asm/global_data.h>
#include <cpu_func.h>
#include <sysreset.h>

DECLARE_GLOBAL_DATA_PTR;

void arch_preboot_os(void)
{

/*
 * restrict booting if image is not authenticated
 * in secure board.
 */
	uint32_t board_type = gd->board_type;

	if(!(board_type & SECURE_BOARD))
		return;

	if((board_type & SECURE_BOARD) && (board_type & ATF_ENABLED))
		return;

	if((board_type & SECURE_BOARD) &&
		!(board_type & ATF_ENABLED) &&
		(board_type & KERNEL_AUTH_SUCCESS))
	{
		char *env = env_get("rootfs_auth");

		if(env)
			if(board_type & ROOTFS_AUTH_SUCCESS)
				return;
			else
				reset_cpu();
		else
			return;
	} else {
		reset_cpu();
	}

	return;
}
