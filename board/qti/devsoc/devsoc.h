// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2023-2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _DEVSOC_H_
#define _DEVSOC_H_

#include <configs/devsoc.h>
#include <asm/u-boot.h>

typedef enum {
	SMEM_SPINLOCK_ARRAY = 7,
	SMEM_AARM_PARTITION_TABLE = 9,
	SMEM_HW_SW_BUILD_ID = 137,
	SMEM_USABLE_RAM_PARTITION_TABLE = 402,
	SMEM_POWER_ON_STATUS_INFO = 403,
	SMEM_MACHID_INFO_LOCATION = 425,
	SMEM_IMAGE_VERSION_TABLE = 469,
	SMEM_BOOT_FLASH_TYPE = 498,
	SMEM_BOOT_FLASH_INDEX = 499,
	SMEM_BOOT_FLASH_CHIP_SELECT = 500,
	SMEM_BOOT_FLASH_BLOCK_SIZE = 501,
	SMEM_BOOT_FLASH_DENSITY = 502,
	SMEM_BOOT_DUALPARTINFO = 503,
	SMEM_PARTITION_TABLE_OFFSET = 504,
	SMEM_SPI_FLASH_ADDR_LEN = 505,
	SMEM_FIRST_VALID_TYPE = SMEM_SPINLOCK_ARRAY,
	SMEM_LAST_VALID_TYPE = SMEM_SPI_FLASH_ADDR_LEN,
	SMEM_MAX_SIZE = SMEM_SPI_FLASH_ADDR_LEN + 1,
} smem_mem_type_t;

/* MACH IDs for various RDPs */
#define MACH_TYPE_DEVSOC_EMU			0x8050001
#define MACH_TYPE_DEVSOC_EMU_FBC		0xF060000

/* Crashdump Magic registers & values */
#define TCSR_BOOT_MISC_REG			((u32*)0x195C100)

#define DLOAD_MAGIC_COOKIE			0x10
#define DLOAD_DISABLED				0x40
#define DLOAD_ENABLE				BIT(4)
#define DLOAD_DISABLE				(~BIT(4))
#define CRASHDUMP_RESET				BIT(11)

/* DT Fixup nodes */
#define LINUX_6_x_NAND_DTS_NODE		"/soc@0/nand@79b0000/"
#define LINUX_6_x_MMC_DTS_NODE		"/soc@0/mmc@7804000/"

#define LINUX_RSVD_MEM_DTS_NODE		"/reserved-memory/"
#define STATUS_OK			"status%?okay"
#define STATUS_DISABLED			"status%?disabled"

/*
 * Rootfs authentication fuse
 */
#define ROOTFS_AUTH_FUSE	0xA0058

#define OEM_SEC_BOOT_ENABLE	BIT(7)

#endif /* _DEVSOC_H_ */
