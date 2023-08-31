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

#define FUSEPROV_SUCCESS		0x0
#define FUSEPROV_INVALID_HASH		0x09
#define FUSEPROV_SECDAT_LOCK_BLOWN	0xB
#define MAX_FUSE_ADDR_SIZE		0x8

static int do_secure(struct cmd_tbl *cmdtp, int flag,
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

static int do_fuseipq(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
	int ret;
	scm_param param;
	uint32_t fuse_status = 0;
	uint32_t fuse_address;

	if (argc != 2) {
		printf("No Arguments provided\n");
		printf("Command format: fuseipq <address>\n");
		return 1;
	}

	fuse_address = simple_strtoul(argv[1], NULL, 16);

	memset(&param, 0, sizeof(scm_param));

	param.type = SCM_FUSE_IPQ;

	param.buff[0] = (uint64_t) fuse_address;
	param.arg_type[0] = SCM_READ_OP;
	param.len = 1;
	param.get_ret = 1;

	ret = ipq_scm_call(&param);

	fuse_status = param.res.result[0];

	if (ret || fuse_status)
		printf("%s: Error in QFPROM write (%d, %d)\n",
			__func__, ret, fuse_status);

	if (fuse_status == FUSEPROV_SECDAT_LOCK_BLOWN)
		printf("Fuse already blown\n");
	else if (fuse_status == FUSEPROV_INVALID_HASH)
		printf("Invalid sec.dat\n");
	else if (fuse_status  != FUSEPROV_SUCCESS)
		printf("Failed to Blow fuses");
	else
		printf("Blow Success\n");

	return 0;
}

U_BOOT_CMD(fuseipq, 2, 0, do_fuseipq,
		"fuse QFPROM registers from memory\n",
		"fuseipq [address]  - Load fuse(s) and blows in the qfprom\n");

#ifdef CONFIG_TARGET_IPQ5332
static int do_list_ipq5332_fuse(struct cmd_tbl *cmdtp, int flag, int argc,
					char *const argv[])
{
	int ret;
	int index, next = 0;
	unsigned long addr = 0xA00E8;
	struct fuse_payload {
		u32 fuse_addr;
		u32 lsb_val;
		u32 msb_val;
	};
	struct fuse_payload *fuse = NULL;
	scm_param param;

	fuse = malloc(sizeof(struct fuse_payload ) * MAX_FUSE_ADDR_SIZE);
	if (fuse == NULL) {
		return 1;
	}

	memset(fuse, 0, MAX_FUSE_ADDR_SIZE * sizeof(struct fuse_payload));

	fuse[0].fuse_addr = 0xA00D0;
	for (index = 1; index < MAX_FUSE_ADDR_SIZE; index++) {
		fuse[index].fuse_addr = addr + next;
		next += 0x8;
	}

	memset(&param, 0, sizeof(scm_param));

	param.type = SCM_LIST_FUSE;

	param.buff[0] = (unsigned long)fuse;
	param.arg_type[0] = SCM_WRITE_OP;

	param.buff[1] = sizeof(struct fuse_payload ) * MAX_FUSE_ADDR_SIZE;
	param.arg_type[1] = SCM_VAL;

	param.len = 2;

	ret = ipq_scm_call(&param);

/*	ret = qca_scm_list_ipq5332_fuse(SCM_SVC_FUSE, TZ_READ_FUSE_VALUE, fuse,
			sizeof(struct fuse_payload ) * MAX_FUSE_ADDR_SIZE);
*/
	printf("Fuse Name\tAddress\t\tValue\n");
	printf("------------------------------------------------\n");

	printf("TME_AUTH_EN\t0x%08X\t0x%08X\n", fuse[0].fuse_addr,
			fuse[0].lsb_val & 0x41);
	printf("TME_OEM_ID\t0x%08X\t0x%08X\n", fuse[0].fuse_addr,
			fuse[0].lsb_val & 0xFFFF0000);
	printf("TME_PRODUCT_ID\t0x%08X\t0x%08X\n", fuse[0].fuse_addr + 0x4,
			fuse[0].msb_val & 0xFFFF);

	for (index = 1; index < MAX_FUSE_ADDR_SIZE; index++) {
		printf("TME_MRC_HASH\t0x%08X\t0x%08X\n",
				fuse[index].fuse_addr, fuse[index].lsb_val);
		printf("TME_MRC_HASH\t0x%08X\t0x%08X\n",
				fuse[index].fuse_addr + 0x4, fuse[index].msb_val);
	}

	if (ret) {
		printf("Failed to read OEM parameters at Address 0x%X\n", ret);
	}
	free(fuse);
	return 0;
}

U_BOOT_CMD(list_ipq5332_fuse, 1, 0, do_list_ipq5332_fuse,
		"fuse set of QFPROM registers from memory\n",
		"");
#endif
