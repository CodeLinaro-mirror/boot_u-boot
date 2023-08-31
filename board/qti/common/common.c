/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <asm/global_data.h>
#include <cpu_func.h>
#include <sysreset.h>
#include <common.h>
#include <command.h>
#include <malloc.h>
#include <memalign.h>
#include <bootm.h>
#include <mach/ipq_scm.h>
#ifdef CONFIG_CMD_UBI
#include <ubi_uboot.h>
#endif

#include "ipq_board.h"

DECLARE_GLOBAL_DATA_PTR;

enum atf_status_t {
	ATF_STATE_DISABLED,
	ATF_STATE_ENABLED,
	ATF_STATE_UNKNOWN,
} atf_status = ATF_STATE_UNKNOWN;

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

#ifdef CONFIG_CMD_UBI
long long ubi_get_volume_size(char *volume)
{
	int i;
	struct ubi_device *ubi = ubi_get_device(0);
	struct ubi_volume *vol = NULL;

	if(NULL == ubi)
		return -ENODEV;

	for (i = 0; i < ubi->vtbl_slots; i++) {
		vol = ubi->volumes[i];
		if (vol && !strcmp(vol->name, volume))
			return vol->used_bytes;
	}

	printf("Volume %s not found!\n", volume);
	return -ENODEV;
}
#endif

int is_atf_enbled(void)
{
	scm_param param;
	int ret;

	if (likely(atf_status != ATF_STATE_UNKNOWN))
		return (atf_status == ATF_STATE_ENABLED);

	memset(&param, 0, sizeof(scm_param));

	param.type = SCM_CHECK_AUTHENTICATE_SUPPORT;
	param.buff[0] = SCM_SMC_FNID(QCOM_SCM_SVC_INFO,
					QCOM_GET_SECURE_STATE_CMD);
	param.arg_type[0] = SCM_VAL;

	param.len = 1;
	param.get_ret = 1;

	ret = ipq_scm_call(&param);

	if(!ret && (le32_to_cpu(param.res.result[0]) > 0)) {
		memset(&param, 0, sizeof(scm_param));
		param.type = SCM_CHECK_ATF_SUPPORT;

		ret = ipq_scm_call(&param);
		if(ret == 0 && (param.res.result[0] & 0x08))
			atf_status = ATF_STATE_ENABLED;
	} else {
		return 0;
	}

	return atf_status == ATF_STATE_ENABLED;

}

int is_secure_boot(void)
{
	scm_param param;
	uint8_t *buff = NULL;
	int ret = 0;

	buff = (uint8_t *)malloc_cache_aligned(CONFIG_SYS_CACHELINE_SIZE);
	if(!buff) {
		printf("Unable allocate memory\n");
		return -1;
	}

	memset(&param, 0, sizeof(scm_param));

	param.type = SCM_CHECK_SECURE_FUSE;
	/*Buffer to read fuse status*/
	param.buff[0] = (uintptr_t)buff;
	param.arg_type[0] = SCM_READ_OP;

	/*Buffer size*/
	param.buff[1] = sizeof(uint8_t);
	param.arg_type[1] = SCM_VAL;

	param.len = 2;

	ret = ipq_scm_call(&param);

	/* invalidate cache to update latest value in buff */
	invalidate_dcache_range((unsigned long)buff,
				(unsigned long)buff +
				CONFIG_SYS_CACHELINE_SIZE);

	if(!ret && *(uint8_t *)buff == 1) {

		ret  = 1;
	} else {
		ret = 0;
	}

	if(buff)
		free(buff);

	return ret;
}
