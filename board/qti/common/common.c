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
#include <mmc.h>
#ifdef CONFIG_CMD_UBI
#include <ubi_uboot.h>
#endif
#include <linux/psci.h>

#include "ipq_board.h"

DECLARE_GLOBAL_DATA_PTR;

#define MMC_MID_MICRON 0xFE
#define MMC_PNM_MICRON 0x4D4D43333247

#define MMC_GET_MID(CID0) (CID0 >> 24)
#define MMC_GET_PNM(CID0, CID1, CID2) (((long long int)(CID0 & 0xff) << 40) | \
                ((long long int)CID1 << 8) |                                  \
                (CID2 >> 24))

#define MMC_CMD_SET_WRITE_PROT		28
#define MMC_CMD_CLR_WRITE_PROT		29

#define MMC_ADDR_OUT_OF_RANGE(resp)	((resp >> 31) & 0x01)

/*
 * CSD fields
*/
#define WP_GRP_ENABLE(csd)		((csd[3] & 0x80000000) >> 31)
#define WP_GRP_SIZE(csd)		((csd[2] & 0x0000001f))
#define ERASE_GRP_MULT(csd)		((csd[2] & 0x000003e0) >> 5)
#define ERASE_GRP_SIZE(csd)		((csd[2] & 0x00007c00) >> 10)


#define EXT_CSD_BOOT_WP_B_PERM_WP_EN	(0x04)  /* permanent write-protect */


int mmc_send_status(struct mmc *mmc, unsigned int *status);
int mmc_switch(struct mmc *mmc, u8 set, u8 index, u8 value);

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

#if CONFIG_MMC
int mmc_send_wp_set_clr(struct mmc *mmc, unsigned int start,
			unsigned int size, int set_clr)
{
	unsigned int err;
	unsigned int wp_group_size, count, i;
	struct mmc_cmd cmd;
	unsigned int status;

	wp_group_size = (WP_GRP_SIZE(mmc->csd) + 1) * mmc->erase_grp_size;
	count = DIV_ROUND_UP(size, wp_group_size);

	if (set_clr)
		cmd.cmdidx = MMC_CMD_SET_WRITE_PROT;
	else
		cmd.cmdidx = MMC_CMD_CLR_WRITE_PROT;
	cmd.resp_type = MMC_RSP_R1b;

	for (i = 0; i < count; i++) {
		cmd.cmdarg = start + (i * wp_group_size);
		err = mmc_send_cmd(mmc, &cmd, NULL);
		if (err) {
			printf("%s: Error at block 0x%x - %d\n", __func__,
			       cmd.cmdarg, err);
			return err;
		}

		if(MMC_ADDR_OUT_OF_RANGE(cmd.response[0])) {
			printf("%s: mmc block(0x%x) out of range", __func__,
			       cmd.cmdarg);
			return -EINVAL;
		}

		err = mmc_send_status(mmc, &status);
		if (err)
			return err;
	}

	return 0;
}

int mmc_write_protect(struct mmc *mmc, unsigned int start_blk,
		      unsigned int cnt_blk, int set_clr)
{
	ALLOC_CACHE_ALIGN_BUFFER(u8, ext_csd, MMC_MAX_BLOCK_LEN);
	int err;
	unsigned int wp_group_size;

	if (!WP_GRP_ENABLE(mmc->csd))
		return -1; /* group write protection is not supported */

	err = mmc_send_ext_csd(mmc, ext_csd);

	if (err) {
		debug("ext_csd register cannot be retrieved\n");
		return err;
	}

	if ((ext_csd[EXT_CSD_USER_WP] & EXT_CSD_BOOT_WP_B_SEC_WP_SEL)
	    || (ext_csd[EXT_CSD_USER_WP] & EXT_CSD_BOOT_WP_B_PERM_WP_EN)) {
		printf("User power-on write protection is disabled. \n");
		return -1;
	}

	err = mmc_switch(mmc, EXT_CSD_CMD_SET_NORMAL, EXT_CSD_USER_WP,
		         EXT_CSD_BOOT_WP_B_PWR_WP_EN);
	if (err) {
		printf("Failed to enable user power-on write protection\n");
		return err;
	}

	wp_group_size = (WP_GRP_SIZE(mmc->csd) + 1) * mmc->erase_grp_size;
	if ((MMC_GET_MID(mmc->cid[0]) == MMC_MID_MICRON) &&
		(MMC_GET_PNM(mmc->cid[0], mmc->cid[1],
					mmc->cid[2]) == MMC_PNM_MICRON))
		wp_group_size *= 2;

	if (!cnt_blk || start_blk % wp_group_size || cnt_blk % wp_group_size) {
		printf("Error: Unaligned offset/count. offset/count should be "
				"aligned to 0x%x blocks\n", wp_group_size);
		return -1;
	}

	err = mmc_send_wp_set_clr(mmc, start_blk, cnt_blk, set_clr);

	return err;
}

static int do_mmc_protect (struct cmd_tbl *cmdtp, int flag,
			   int argc, char * const argv[])
{
	struct mmc *mmc;
	unsigned int ret;
	unsigned int blk, cnt;
	int curr_device = -1;

	if (curr_device < 0) {
		if (get_mmc_num() > 0) {
			curr_device = 0;
		} else {
			puts("No MMC device available\n");
			return CMD_RET_FAILURE;
		}
	}

	mmc = find_mmc_device(curr_device);
	if (!mmc) {
		printf("no mmc device at slot %x\n", curr_device);
		return -ENOMEM;
	}

	if (argc != 3)
		return CMD_RET_USAGE;

	blk = (unsigned int)simple_strtoul(argv[1], NULL, 16);
	cnt = (unsigned int)simple_strtoul(argv[2], NULL, 16);

	ret = mmc_write_protect(mmc, blk, cnt, 1);

	if (!ret)
		printf("Offset: 0x%x Count: %d blocks\nDone!\n", blk, cnt);

	return ret ? CMD_RET_FAILURE : CMD_RET_SUCCESS;
}

U_BOOT_CMD(
	mmc_protect, 3, 0, do_mmc_protect,
	"MMC write protect",
	"mmc_protect start_blk cnt_blk\n"
);
#endif

#ifdef CONFIG_IPQ_SMP_CMD_SUPPORT
static int qti_invoke_psci_fn_smc
		(unsigned long function_id, unsigned long arg0,
		 unsigned long arg1, unsigned long arg2)
{
	struct arm_smccc_res res;
	arm_smccc_smc(function_id, arg0, arg1, arg2, 0, 0, 0, 0, &res);
	return res.a0;
}

int is_secondary_core_off(unsigned int cpuid)
{
	return qti_invoke_psci_fn_smc(PSCI_0_2_FN_AFFINITY_INFO, cpuid, 0, 0);
}

void bring_secondary_core_down(unsigned int state)
{
	qti_invoke_psci_fn_smc(PSCI_0_2_FN_CPU_OFF, state, 0, 0);
}

int bring_secondary_core_up(unsigned int cpuid, unsigned int entry,
				unsigned int arg)
{
	int ret;
	ret = qti_invoke_psci_fn_smc(PSCI_0_2_FN_CPU_ON, cpuid, entry, arg);
	if (ret) {
		printf("Enabling CPU%d via psci failed! (ret : %d)\n",
								cpuid, ret);
		return CMD_RET_FAILURE;
	}

	printf("Enabled CPU%d via psci successfully!\n", cpuid);
	return CMD_RET_SUCCESS;
}
#endif
