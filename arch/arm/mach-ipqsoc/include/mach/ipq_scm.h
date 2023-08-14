/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2010-2015,2019 The Linux Foundation. All rights reserved.
 * Copyright (c) 2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __IPQ_SCM_H
#define __IPQ_SCM_H

#include <linux/errno.h>

#define MAX_QCOM_SCM_ARGS	10
#define MAX_QCOM_SCM_RETS	3

#define QCOM_SCM_ARGS_IMPL(num, a, b, c, d, e, f, g, h, i, j, ...) (\
			(((a) & 0x3) << 4) | \
			(((b) & 0x3) << 6) | \
			(((c) & 0x3) << 8) | \
			(((d) & 0x3) << 10) | \
			(((e) & 0x3) << 12) | \
			(((f) & 0x3) << 14) | \
			(((g) & 0x3) << 16) | \
			(((h) & 0x3) << 18) | \
			(((i) & 0x3) << 20) | \
			(((j) & 0x3) << 22) | \
			((num) & 0xf))

#define QCOM_SCM_ARGS(...) QCOM_SCM_ARGS_IMPL(__VA_ARGS__, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)

struct qcom_scm_desc {
	uint64_t args[MAX_QCOM_SCM_ARGS];
	uint32_t svc;
	uint32_t cmd;
	uint32_t arginfo;
	uint32_t owner;
};

/**
 * struct arm_smccc_args
 * @args:	The array of values used in registers in smc instruction
 */
struct arm_smccc_args {
	unsigned long args[8];
};

/**
 * struct qcom_scm_res
 * @result:     The values returned by the secure syscall
 */
struct qcom_scm_res {
        uint64_t result[MAX_QCOM_SCM_RETS];
};

#define SCM_SMC_FNID(s, c)      ((((s) & 0xFF) << 8) | ((c) & 0xFF))
#define scm_smc_call(desc, res, atomic) \
        __scm_smc_call((desc), qcom_scm_convention, (res), (atomic))

#define SCM_SMC_N_REG_ARGS	4
#define SCM_SMC_FIRST_EXT_IDX	(SCM_SMC_N_REG_ARGS - 1)
#define SCM_SMC_N_EXT_ARGS	(MAX_QCOM_SCM_ARGS - SCM_SMC_N_REG_ARGS + 1)
#define SCM_SMC_FIRST_REG_IDX	2
#define SCM_SMC_LAST_REG_IDX	(SCM_SMC_FIRST_REG_IDX + SCM_SMC_N_REG_ARGS - 1)

/* common error codes */
#define QCOM_SCM_V2_EBUSY	-12
#define QCOM_SCM_ENOMEM		-5
#define QCOM_SCM_EOPNOTSUPP	-4
#define QCOM_SCM_EINVAL_ADDR	-3
#define QCOM_SCM_EINVAL_ARG	-2
#define QCOM_SCM_ERROR		-1
#define QCOM_SCM_INTERRUPTED	 1

static inline int qcom_scm_remap_error(int err)
{
switch (err) {
	case QCOM_SCM_ERROR:
		return -EIO;
	case QCOM_SCM_EINVAL_ADDR:
	case QCOM_SCM_EINVAL_ARG:
		return -EINVAL;
	case QCOM_SCM_EOPNOTSUPP:
		return -EOPNOTSUPP;
	case QCOM_SCM_ENOMEM:
		return -ENOMEM;
	case QCOM_SCM_V2_EBUSY:
		return -EBUSY;
	}
	return -EINVAL;
}

/* SVC & CMD IDs */
#define QCOM_SCM_SVC_BOOT		0x01
#define QCOM_SCM_CMD_TZ_CONFIG_HW_FOR_RAM_DUMP_ID	0x9
#define QCOM_SCM_EL1SWITCH_ARCH64	0xf

#define QCOM_SCM_SVC_INFO               0x06
#define QCOM_SCM_INFO_IS_CALL_AVAIL     0x01

#define QCOM_SCM_SVC_IO			0x05
#define QCOM_SCM_IO_READ		0x01
#define QCOM_SCM_IO_WRITE		0x02

typedef struct {
#ifdef CONFIG_CPU_V7A
	uint64_t reg_x0;
	uint64_t reg_x1;
	uint64_t reg_x2;
	uint64_t reg_x3;
	uint64_t reg_x4;
	uint64_t reg_x5;
	uint64_t reg_x6;
	uint64_t reg_x7;
	uint64_t reg_x8;
	uint64_t kernel_start;
#endif
#ifdef CONFIG_ARM64
	uintptr_t reg_x0;
	uintptr_t reg_x1;
	uintptr_t reg_x2;
	uintptr_t reg_x3;
	uintptr_t reg_x4;
	uintptr_t reg_x5;
	uintptr_t reg_x6;
	uintptr_t reg_x7;
	uintptr_t reg_x8;
	uintptr_t kernel_start;
#endif
} kernel_params;


void __attribute__ ((noreturn)) jump_kernel(void *kernel_entry,
		void *fdt_addr);
int qca_scm_sdi(void);
int qca_scm_dload(uint32_t tcsr_addr, uint32_t magic_cookie);

#endif
