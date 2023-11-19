// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/sizes.h>
#include <asm/arch/sysmap-devsoc.h>

#ifndef __ASSEMBLY__
#include <compiler.h>
extern uint32_t g_board_machid;
#endif

/*
 * Memory layout
   8000_0000-->  _____________________  DRAM BASE
	        |		      |
	        |		      |
	        |_____________________|
	        |		      |
	        |	STACK	      |
	        |_____________________|
	        |		      |
	        |         GD          |
	        |_____________________|
	        |		      |
	        |         BD          |
	        |_____________________|
	        |		      |
	        |  HEAP + ENV - 2MB   |
   8A30_0000--> |_____________________|
	        |		      |
                |     TEXT - 2MB      |
	        |_____________________|
	        |        .	      |
	        |	 .	      |
	        |	 .	      |
	        |________.____________| DRAM END
*/

#define CONFIG_HAS_CUSTOM_SYS_INIT_SP_ADDR
#define CONFIG_CUSTOM_SYS_INIT_SP_ADDR         	(CONFIG_TEXT_BASE -	\
						CONFIG_SYS_MALLOC_LEN - \
						CONFIG_ENV_SIZE -	\
						GENERATED_GBL_DATA_SIZE)

#define CFG_SYS_BAUDRATE_TABLE			{ 115200, 230400, 	\
							460800, 921600 }

#define CFG_SYS_HZ_CLOCK			240000

/* In 64-bit, SDRAM will contains 2 banks */
#define CFG_SYS_SDRAM_BASE0			0x80000000
#define CFG_SYS_SDRAM_BASE0_SZ			0x80000000
#if (CONFIG_NR_DRAM_BANKS > 1)
#define CFG_SYS_SDRAM_BASE1			0x800000000
#define CFG_SYS_SDRAM_BASE1_SZ			0x180000000
#endif

#define CFG_SYS_SDRAM_BASE			CFG_SYS_SDRAM_BASE0
#define KERNEL_START_ADDR                   	CFG_SYS_SDRAM_BASE
#define BOOT_PARAMS_ADDR                    	(KERNEL_START_ADDR + 0x100)

#define CONFIG_MACH_TYPE			(g_board_machid)

#define PHY_ANEG_TIMEOUT			100
#define FDT_HIGH				0x88500000

#define DEVSOC_UBOOT_END_ADDRESS		CONFIG_TEXT_BASE + \
							CONFIG_TEXT_SIZE
#define DEVSOC_DDR_SIZE				(0x3UL * SZ_2G)
#define DEVSOC_DDR_UPPER_SIZE_MAX		(DEVSOC_DDR_SIZE - \
						(CFG_SYS_SDRAM_BASE - \
						DEVSOC_UBOOT_END_ADDRESS))
#define DEVSOC_DDR_LOWER_SIZE			(CONFIG_TEXT_BASE - \
							CFG_SYS_SDRAM_BASE)
#define ROOT_FS_PART_NAME			"rootfs"

#define CONFIG_ROOTFS_LOAD_ADDR			CFG_SYS_SDRAM_BASE + (32 << 20)
