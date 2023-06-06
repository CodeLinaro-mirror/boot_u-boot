// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <common.h>
#include <cpu_func.h>
#include <asm/cache.h>
#include <asm/global_data.h>
#include <jffs2/load_kernel.h>
#include <mtd_node.h>
#include <sysreset.h>
#include <linux/psci.h>
#ifdef CONFIG_ARM64
#include <asm/armv8/mmu.h>
#endif

#include "../common/ipq_board.h"

DECLARE_GLOBAL_DATA_PTR;

#define LINUX_NAND_DTS "/soc/nand@79b0000/"
#define LINUX6_1_NAND_DTS "/soc@0/nand@79b0000/"
#define LINUX_MMC_DTS "/soc/sdhci@7804000/"
#define LINUX6_1_MMC_DTS "/soc@0/mmc@7804000/"
#define STATUS_OK "status%?okay"
#define STATUS_DISABLED "status%?disabled"

#if CONFIG_FDT_FIXUP_PARTITIONS
struct node_info ipq_fnodes[] = {
	{ "n25q128a11", MTD_DEV_TYPE_NOR},
	{ "micron,n25q128a11", MTD_DEV_TYPE_NOR},
};

int ipq_fnode_entires = ARRAY_SIZE(ipq_fnodes);

struct node_info * fnodes = ipq_fnodes ;
int * fnode_entires = &ipq_fnode_entires;
#endif

void reset_cpu(void)
{
	psci_sys_reset(SYSRESET_COLD);
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

#ifdef CONFIG_IPQ_FDT_FIXUP
void fdt_fixup_flash(void *blob)
{
	uint32_t flash_type = SMEM_BOOT_NO_FLASH;
	int nand_nodeoff = fdt_path_offset(blob, LINUX_NAND_DTS);
	int mmc_nodeoff = fdt_path_offset(blob, LINUX_MMC_DTS);
	ipq_smem_flash_info_t *sfi = get_ipq_smem_flash_info();

	if (sfi->flash_secondary_type == SMEM_BOOT_MMC_FLASH)
		flash_type = SMEM_BOOT_NORPLUSEMMC;
	else if (sfi->flash_secondary_type == SMEM_BOOT_QSPI_NAND_FLASH)
		flash_type = SMEM_BOOT_NORPLUSNAND;
	else
		flash_type = sfi->flash_type;

	if (flash_type == SMEM_BOOT_NORPLUSEMMC ||
		flash_type == SMEM_BOOT_MMC_FLASH ) {
		(nand_nodeoff >= 0) ?
			parse_fdt_fixup(
				LINUX_NAND_DTS"%"STATUS_DISABLED, blob):
			parse_fdt_fixup(
				LINUX6_1_NAND_DTS"%"STATUS_DISABLED, blob);
		(mmc_nodeoff >= 0) ?
			parse_fdt_fixup(LINUX_MMC_DTS"%"STATUS_OK, blob) :
			parse_fdt_fixup(LINUX6_1_MMC_DTS"%"STATUS_OK, blob);
	}
	return;
}
#endif /* CONFIG_IPQ_FDT_FIXUP */

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
