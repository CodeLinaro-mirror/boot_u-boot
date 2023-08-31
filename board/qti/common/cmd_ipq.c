/*
 * Copyright (c) 2018 The Linux Foundation. All rights reserved.
 *
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <common.h>
#include <command.h>
#include <malloc.h>
#include <memalign.h>
#include <mach/ipq_scm.h>
#include <cpu_func.h>
#include <linux/bug.h>
#include <linux/arm-smccc.h>
#include "ipq_board.h"

#define PRI_PARTITION	1
#define ALT_PARTITION	2

int do_secure(struct cmd_tbl *cmdtp, int flag,
				int argc, char *const argv[])
{
	int ret = CMD_RET_FAILURE;
#ifdef CONFIG_VERSION_ROLLBACK_PARTITION_INFO
	int active_part = PRI_PARTITION;
#endif
	uint8_t *buff = NULL;
	auth_cmd_buf auth_buf;

	scm_param param;

	if(argc!=4 && argc !=1)
		return CMD_RET_USAGE;

	if (strncmp(argv[0], "is_sec_boot_enabled", 19) == 0 && argc == 1) {

		buff = (uint8_t *)malloc_cache_aligned(CONFIG_SYS_CACHELINE_SIZE);
		if(!buff) {
			printf("Unable allocate memory\n");
			ret = CMD_RET_FAILURE;
			goto exit;
		}

		memset(buff, 0, CONFIG_SYS_CACHELINE_SIZE);

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

		if(!ret) {
			printf("secure boot fuse is%senabled\n",
					1 == *(uint8_t *)buff? " ": " not ");
			ret = CMD_RET_SUCCESS;
		} else {
			printf("secure cmd: scm call failed. ret = %d\n", ret);
			ret = CMD_RET_FAILURE;
		}

		if(buff)
			free(buff);
	} else if(strncmp(argv[0], "secure_authenticate", 19) == 0 && argc == 4) {

		memset(&param, 0, sizeof(scm_param));

		param.type = SCM_CHECK_AUTHENTICATE_SUPPORT;
		param.buff[0] = SCM_SMC_FNID(QCOM_SCM_SVC_BOOT,
					QCOM_SCM_SEC_AUTH_CMD) |
					(ARM_SMCCC_OWNER_SIP <<
					ARM_SMCCC_OWNER_SHIFT);
		param.arg_type[0] = SCM_VAL;

		param.len = 1;
		param.get_ret = 1;

		ret = ipq_scm_call(&param);

		if (ret || (!ret && le32_to_cpu(param.res.result[0]) <= 0)) {
			printf("secure authentication scm call"
				" is not supported. ret = %d\n", ret);
			ret = CMD_RET_SUCCESS;
			goto exit;
		}

		auth_buf.type = simple_strtoul(argv[1], NULL, 16);
		auth_buf.addr = simple_strtoul(argv[2], NULL, 16);
		auth_buf.size = simple_strtoul(argv[3], NULL, 16);
#ifdef CONFIG_VERSION_ROLLBACK_PARTITION_INFO
		active_part = get_rootfs_active_partition();
		active_part = active_part ? ALT_PARTITION : PRI_PARTITION;

		memset(&param, 0, sizeof(scm_param));

		param.type = SCM_SET_ACTIVE_PART;

		/*pass current avtive partition */
		param.buff[0] = active_part;
		param.arg_type[0] = SCM_VAL;

		param.len = 1;

		ret = ipq_scm_call(&param);

		if(ret) {
			printf("Partition info authentication failed\n");
			BUG(); //:TODO check if BUG is necessary
		}

#endif
		memset(&param, 0, sizeof(scm_param));

		param.type = SCM_SECURE_AUTH;

		/* args[0] has the image SW ID*/
		param.buff[0] = auth_buf.type;
		param.arg_type[0] = SCM_VAL;

		/* args[1] has the image size */
		param.buff[1] = auth_buf.size;
		param.arg_type[1] = SCM_VAL;

		/* args[2] has the load address*/
		param.buff[2] = auth_buf.addr;
		param.arg_type[2] = SCM_WRITE_OP;

		param.len = 3;
		param.get_ret = 1;

		ret = ipq_scm_call(&param);

		if(ret || (param.res.result[0] && !ret)) {
			printf("image authentication failed. ret  = %d\n",
									ret);
			ret = CMD_RET_FAILURE;
		} else {
			printf("image authentication success\n");
			ret = CMD_RET_SUCCESS;
		}
	} else {
		return CMD_RET_USAGE;
	}

exit:
	return ret;
}

U_BOOT_CMD(is_sec_boot_enabled, 1, 0, do_secure,
		"check secure boot fuse is enabled or not\n",
		"is_sec_boot_enabled - check secure boot fuse "
		"is enabled or not\n");

U_BOOT_CMD(secure_authenticate, 4, 0, do_secure,
		"authenticate the signed image\n",
		"secure_authenticate <sw_id> <img_addr> <img_size>\n"
		"	- authenticate the signed image\n");
