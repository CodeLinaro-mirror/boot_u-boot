// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2010,2015,2019 The Linux Foundation. All rights reserved.
 * Copyright (C) 2015 Linaro Ltd.
 * Copyright (c) 2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <ubi_uboot.h>
#include <linux/stddef.h>
#include <linux/compat.h>
#include <linux/dma-mapping.h>
#include <linux/arm-smccc.h>
#include <errno.h>
#include <linux/delay.h>
#include <asm/io.h>
#include <hang.h>
#include <mach/ipq_scm.h>
#include <common.h>
#include <asm/system.h>

#ifdef DEBUG
#define debugf(fmt, args...) do { printf("%s(): ", __func__); printf(fmt, ##args); } while (0)
#else
#define debugf(fmt, args...)
#endif

enum qcom_scm_convention {
	SMC_CONVENTION_UNKNOWN,
	SMC_CONVENTION_LEGACY,
	SMC_CONVENTION_ARM_32,
	SMC_CONVENTION_ARM_64,
};

enum qcom_scm_convention qcom_scm_convention = SMC_CONVENTION_UNKNOWN;

static const char * const qcom_scm_convention_names[] = {
        [SMC_CONVENTION_UNKNOWN] = "unknown",
        [SMC_CONVENTION_ARM_32] = "smc arm 32",
        [SMC_CONVENTION_ARM_64] = "smc arm 64",
        [SMC_CONVENTION_LEGACY] = "smc legacy",
};

static void __scm_smc_do_quirk(const struct arm_smccc_args *smc,
				struct arm_smccc_res *res)
{
	unsigned long a0 = smc->args[0];
	struct arm_smccc_quirk quirk = { .id = ARM_SMCCC_QUIRK_QCOM_A6 };

	quirk.state.a6 = 0;

	do {
		arm_smccc_smc_quirk(a0, smc->args[1], smc->args[2],
					smc->args[3], smc->args[4], smc->args[5],
					quirk.state.a6, smc->args[7], res, &quirk);

		if (res->a0 == QCOM_SCM_INTERRUPTED)
			a0 = res->a0;

	} while (res->a0 == QCOM_SCM_INTERRUPTED);
}


int __scm_smc_call(const struct qcom_scm_desc *desc,
		   enum qcom_scm_convention qcom_convention,
		   struct qcom_scm_res *res, bool atomic)
{
	int i;
	u32 smccc_call_type = atomic ? ARM_SMCCC_FAST_CALL : ARM_SMCCC_STD_CALL;
	u32 qcom_smccc_convention = (qcom_convention == SMC_CONVENTION_ARM_32) ?
				    ARM_SMCCC_SMC_32 : ARM_SMCCC_SMC_64;
	struct arm_smccc_res smc_res;
	struct arm_smccc_args smc = {0};

	smc.args[0] = ARM_SMCCC_CALL_VAL(
		smccc_call_type,
		qcom_smccc_convention,
		desc->owner,
		SCM_SMC_FNID(desc->svc, desc->cmd));
	smc.args[1] = desc->arginfo;

	for (i = 0; i < SCM_SMC_N_REG_ARGS; i++)
		smc.args[i + SCM_SMC_FIRST_REG_IDX] = desc->args[i];

	__scm_smc_do_quirk(&smc, &smc_res);

	if (res) {
		res->result[0] = smc_res.a1;
		res->result[1] = smc_res.a2;
		res->result[2] = smc_res.a3;
	}

	return (long)smc_res.a0 ? qcom_scm_remap_error(smc_res.a0) : 0;
}

static enum qcom_scm_convention __get_convention(void)
{
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_INFO,
		.cmd = QCOM_SCM_INFO_IS_CALL_AVAIL,
		.args[0] = SCM_SMC_FNID(QCOM_SCM_SVC_INFO,
					   QCOM_SCM_INFO_IS_CALL_AVAIL) |
			   (ARM_SMCCC_OWNER_SIP << ARM_SMCCC_OWNER_SHIFT),
		.arginfo = QCOM_SCM_ARGS(1),
		.owner = ARM_SMCCC_OWNER_SIP,
	};
	struct qcom_scm_res res;
	enum qcom_scm_convention probed_convention;
	int ret;
	bool forced = false;

	if (likely(qcom_scm_convention != SMC_CONVENTION_UNKNOWN))
		return qcom_scm_convention;

	/*
	 * When running on 32bit kernel, SCM call with convention
	 * SMC_CONVENTION_ARM_64 is causing the system crash. To avoid that
	 * use SMC_CONVENTION_ARM_64 for 64bit kernel and SMC_CONVENTION_ARM_32
	 * for 32bit kernel.
	 */
#if IS_ENABLED(CONFIG_ARM64)
	/*
	 * Device isn't required as there is only one argument - no device
	 * needed to dma_map_single to secure world
	 */
	probed_convention = SMC_CONVENTION_ARM_64;
	ret = __scm_smc_call(&desc, probed_convention, &res, true);
	if (!ret && res.result[0] == 1)
		goto found;
#endif

	probed_convention = SMC_CONVENTION_ARM_32;
	ret = __scm_smc_call(&desc, probed_convention, &res, true);
	if (!ret && res.result[0] == 1)
		goto found;

	probed_convention = SMC_CONVENTION_LEGACY;
found:
	if (probed_convention != qcom_scm_convention) {
		qcom_scm_convention = probed_convention;
		debugf("qcom_scm: convention: %s%s\n",
			qcom_scm_convention_names[qcom_scm_convention],
			forced ? " (forced)" : "");
	}

	return qcom_scm_convention;
}

/**
 * qcom_scm_call() - qcom_scm_call()
 * @desc:       Descriptor structure containing arguments and return values
 * @res:        Structure containing results from SMC/HVC call
 *
 * Sends a command to the SCM and waits for the command to finish processing.
 * This can be called in atomic context.
 */
static int qcom_scm_call(const struct qcom_scm_desc *desc,
                                struct qcom_scm_res *res)
{
	switch (__get_convention()) {
		case SMC_CONVENTION_ARM_32:
		case SMC_CONVENTION_ARM_64:
			return scm_smc_call(desc, res, true);
		case SMC_CONVENTION_LEGACY:
		default:
			pr_err("Unknown current SCM calling convention.\n");
		return -EINVAL;
	}
}

int qcom_scm_io_readl(phys_addr_t addr, unsigned int *val)
{
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_IO,
		.cmd = QCOM_SCM_IO_READ,
		.arginfo = QCOM_SCM_ARGS(1),
		.args[0] = addr,
		.owner = ARM_SMCCC_OWNER_SIP,
	};
	struct qcom_scm_res res;
	int ret;


	ret = qcom_scm_call(&desc, &res);
	if (ret >= 0)
		*val = res.result[0];

	return ret < 0 ? ret : 0;
}

int qcom_scm_io_writel(phys_addr_t addr, unsigned int val)
{
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_IO,
		.cmd = QCOM_SCM_IO_WRITE,
		.arginfo = QCOM_SCM_ARGS(2),
		.args[0] = addr,
		.args[1] = val,
		.owner = ARM_SMCCC_OWNER_SIP,
	};

	return qcom_scm_call(&desc, NULL);
}

int qca_scm_dload(uint32_t tcsr_addr, u32 magic_cookie)
{
	return qcom_scm_io_writel(tcsr_addr, magic_cookie);
}

int qca_scm_sdi(void)
{
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_BOOT,
		.cmd = QCOM_SCM_CMD_TZ_CONFIG_HW_FOR_RAM_DUMP_ID,
		.arginfo = QCOM_SCM_ARGS(2),
		.args[0] = 1ul,	/* Disable wdog debug */
		.args[1] = 0ul,	/* SDI Enable */
		.owner = ARM_SMCCC_OWNER_SIP,
	};

	return qcom_scm_call(&desc, NULL);
}
