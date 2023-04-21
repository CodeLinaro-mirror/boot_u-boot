// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <common.h>
#include <cpu_func.h>
#include <asm/cache.h>
#include <asm/global_data.h>
#ifdef CONFIG_ARM64
#include <asm/armv8/mmu.h>
#endif

DECLARE_GLOBAL_DATA_PTR;

void reset_cpu(void)
{
	return;
}

int print_cpuinfo(void)
{
        return 0;
}

void lowlevel_init(void)
{
	return;
}

void enable_caches(void)
{
	icache_enable();
	dcache_enable();
}

int board_fit_config_name_match(const char *name)
{
	if (!strcmp(name, "ipq5332-mi1-4"))
		return 0;
	return -1;
}

#ifdef CONFIG_ARM64
/*
 * Set XN (PTE_BLOCK_PXN | PTE_BLOCK_UXN)bit for all dram regions
 * and Peripheral block except uboot code region
 */
static struct mm_region ipq5332_mem_map[] = {
	{
		/* Peripheral block */
		.virt = 0x0UL,
		.phys = 0x0UL,
		.size = CFG_SYS_SDRAM_BASE,
		.attrs = PTE_BLOCK_MEMTYPE(MT_DEVICE_NGNRNE) |
			 PTE_BLOCK_NON_SHARE |
			 PTE_BLOCK_PXN | PTE_BLOCK_UXN

	}, {
		/* DDR region upto u-boot CONFIG_TEXT_BASE */
		.virt = CFG_SYS_SDRAM_BASE,
		.phys = CFG_SYS_SDRAM_BASE,
		.size = IPQ5332_DDR_LOWER_SIZE,
		.attrs = PTE_BLOCK_MEMTYPE(MT_NORMAL) |
			 PTE_BLOCK_INNER_SHARE |
			 PTE_BLOCK_PXN | PTE_BLOCK_UXN
	}, {
		/* DDR region U-boot text base */
		.virt = CONFIG_TEXT_BASE,
		.phys = CONFIG_TEXT_BASE,
		.size = CONFIG_TEXT_SIZE,
		.attrs = PTE_BLOCK_MEMTYPE(MT_NORMAL) |
			 PTE_BLOCK_INNER_SHARE
	}, {
		/* DDR region after u-boot text base till max DDR region */
		.virt = IPQ5332_UBOOT_END_ADDRESS,
		.phys = IPQ5332_UBOOT_END_ADDRESS,
		.size = IPQ5332_DDR_UPPER_SIZE_MAX,
		.attrs = PTE_BLOCK_MEMTYPE(MT_NORMAL) |
			 PTE_BLOCK_INNER_SHARE |
			 PTE_BLOCK_PXN | PTE_BLOCK_UXN
	}, {
		/* List terminator */
		0,
	}
};

struct mm_region *mem_map = ipq5332_mem_map;
#endif
