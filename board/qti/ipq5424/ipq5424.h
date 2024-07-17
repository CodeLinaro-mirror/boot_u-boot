// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2023-2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _IPQ5424_H_
#define _IPQ5424_H_

#include <configs/ipq5424.h>
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
#define MACH_TYPE_IPQ5424_EMU			0x8050001
#define MACH_TYPE_IPQ5424_EMU_FBC		0xF060000

/*
 * TCSR Registers
 */
#define TCSR_TZ_WONCE0				0x195C000
#define TCSR_TZ_WONCE1				0x195C004

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

#define LINUX_6_x_USB_DTS_NODE		"/soc@0/usb3@8a00000/dwc3@8a00000/"
#define LINUX_6_x_USB_DR_MODE_FIXUP	"/soc@0/usb3@8a00000/dwc3@8a00000%dr_mode%?peripheral"
#define LINUX_6_x_USB_MAX_SPEED_FIXUP	"/soc@0/usb3@8a00000/dwc3@8a00000%maximum-speed%?high-speed"

#define LINUX_6_x_USB2_DTS_NODE		"/soc@0/usb2@1e00000/dwc3@1e00000/"
#define LINUX_6_x_USB2_DR_MODE_FIXUP	"/soc@0/usb2@1e00000/dwc3@1e00000%dr_mode%?peripheral"
#define LINUX_6_x_USB2_MAX_SPEED_FIXUP	"/soc@0/usb2@1e00000/dwc3@1e00000%maximum-speed%?high-speed"

#define LINUX_RSVD_MEM_DTS_NODE		"/reserved-memory/"
#define STATUS_OK			"status%?okay"
#define STATUS_DISABLED			"status%?disabled"

/* USB softsku fuse */
#define USB_SOFTSKU_STATUS		0xA628C
#define USB_SOFTSKU_STATUS_DISABLE	BIT(0)

/*
 * Rootfs authentication fuse
 */
#define ROOTFS_AUTH_FUSE	0xA0058

#define OEM_SEC_BOOT_ENABLE	BIT(7)

#endif /* _IPQ5424_H_ */
