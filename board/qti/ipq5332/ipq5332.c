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
#include <mach/ipq_scm.h>
#ifdef CONFIG_ARM64
#include <asm/armv8/mmu.h>
#endif

#include "../common/ipq_board.h"

#include <asm/io.h>
#include <linux/delay.h>

#define PLL_POWER_ON_AND_RESET			0x9B780
#define PLL_REFERENCE_CLOCK			0x9B784
#define FREQUENCY_MASK				0xfffffdf0
#define INTERNAL_48MHZ_CLOCK			0x7

DECLARE_GLOBAL_DATA_PTR;

static dram_bank_info_t ipq5332_dram_bank_info[CONFIG_NR_DRAM_BANKS] = {
	{
		.start = CFG_SYS_SDRAM_BASE,
		.size  = CFG_SYS_SDRAM_BASE_MAX_SZ,
	},
};

dram_bank_info_t * board_dram_bank_info = ipq5332_dram_bank_info;

#if CONFIG_FDT_FIXUP_PARTITIONS
struct node_info ipq_fnodes[] = {
	{ "n25q128a11", MTD_DEV_TYPE_NOR},
	{ "micron,n25q128a11", MTD_DEV_TYPE_NOR},
	{ "qcom,ipq5332-nand", MTD_DEV_TYPE_NAND},
};

int ipq_fnode_entires = ARRAY_SIZE(ipq_fnodes);

struct node_info * fnodes = ipq_fnodes ;
int * fnode_entires = &ipq_fnode_entires;
#endif

#ifdef CONFIG_DTB_RESELECT
struct machid_dts_map machid_dts[] = {
	{ MACH_TYPE_IPQ5332_RDP468, "ipq5332-rdp468"},
	{ MACH_TYPE_IPQ5332_RDP441, "ipq5332-rdp441"},
	{ MACH_TYPE_IPQ5332_RDP442, "ipq5332-rdp442"},
	{ MACH_TYPE_IPQ5332_RDP446, "ipq5332-rdp446"},
	{ MACH_TYPE_IPQ5332_RDP474, "ipq5332-rdp474"},
	{ MACH_TYPE_IPQ5332_RDP472, "ipq5332-rdp472"},
	{ MACH_TYPE_IPQ5332_RDP477, "ipq5332-rdp477"},
	{ MACH_TYPE_IPQ5332_RDP478, "ipq5332-rdp478"},
	{ MACH_TYPE_IPQ5332_RDP479, "ipq5332-rdp479"},
	{ MACH_TYPE_IPQ5332_RDP481, "ipq5332-rdp481"},
	{ MACH_TYPE_IPQ5332_RDP484, "ipq5332-rdp484"},
	{ MACH_TYPE_IPQ5332_DB_MI01_1, "ipq5332-db-mi01.1"},
	{ MACH_TYPE_IPQ5332_DB_MI02_1, "ipq5332-db-mi02.1"},
	{ MACH_TYPE_IPQ5332_DB_MI03_1, "ipq5332-db-mi03.1"},
};

int machid_dts_nos = ARRAY_SIZE(machid_dts);

struct machid_dts_map * machid_dts_info = machid_dts;
int * machid_dts_entries = &machid_dts_nos;
#endif /* CONFIG_DTB_RESELECT */

static crashdump_infos_t dumpinfo_n[] = {
	{
		.name = "EBICS.BIN",
		.start_addr = 0x40000000,
		.size = 0xBAD0FF5E,
		.dump_level = FULLDUMP,
		.split_bin_sz = SZ_1G,
		.is_aligned_access = false,
		.compression_support = true,
		.dumptoflash_support = false
	},
	{
		.name = "IMEM.BIN",
		.start_addr = 0x08600000,
		.size = 0x00001000,
		.dump_level = FULLDUMP,
		.split_bin_sz = 0,
		.is_aligned_access = false,
		.compression_support = false,
		.dumptoflash_support = false
	},
	{
		.name = "CPU_INFO.BIN",
		.start_addr = 0x0,
		.size = 0xBAD0FF5E,
		.dump_level = MINIDUMP,
		.split_bin_sz = 0,
		.is_aligned_access = false,
		.compression_support = false,
		.dumptoflash_support = true
	},
	{
		.name = "UNAME.BIN",
		.start_addr = 0x0,
		.size = 0xBAD0FF5E,
		.dump_level = MINIDUMP,
		.split_bin_sz = 0,
		.is_aligned_access = false,
		.compression_support = false,
		.dumptoflash_support = true
	},
	{
		.name = "DMESG.BIN",
		.start_addr = 0x0,
		.size = 0xBAD0FF5E,
		.dump_level = MINIDUMP,
		.split_bin_sz = 0,
		.is_aligned_access = false,
		.compression_support = false,
		.dumptoflash_support = false
	},
	{
		.name = "PT.BIN",
		.start_addr = 0x0,
		.size = 0xBAD0FF5E,
		.dump_level = MINIDUMP,
		.split_bin_sz = 0,
		.is_aligned_access = false,
		.compression_support = false,
		.dumptoflash_support = false
	},
	{
		.name = "WLAN_MOD.BIN",
		.start_addr = 0x0,
		.size = 0xBAD0FF5E,
		.dump_level = MINIDUMP,
		.split_bin_sz = 0,
		.is_aligned_access = false,
		.compression_support = false,
		.dumptoflash_support = false
	},
};

static uint8_t dump_entries_n = ARRAY_SIZE(dumpinfo_n);

crashdump_infos_t *board_dumpinfo = dumpinfo_n;
uint8_t *board_dump_entries = &dump_entries_n;

void reset_cpu(void)
{
	reset_crashdump();
	psci_sys_reset(SYSRESET_COLD);
	return;
}

int print_cpuinfo(void)
{
        return 0;
}

void board_cache_init(void)
{
	ipq_smem_flash_info_t *sfi = get_ipq_smem_flash_info();
	icache_enable();
#if !CONFIG_IS_ENABLED(SYS_DCACHE_OFF)
	/* Disable L2 as TCM in recovery mode */
	if (!sfi->flash_type)
		writel(0x08000000, 0xB110010);

	dcache_enable();
#endif
}

void lowlevel_init(void)
{
	return;
}

void ipq_config_cmn_clock(void)
{
	unsigned int reg_val;
	/*
	 * Init CMN clock for ethernet
	 */
	reg_val = readl(PLL_REFERENCE_CLOCK);
	reg_val = (reg_val & FREQUENCY_MASK) | INTERNAL_48MHZ_CLOCK;
	/*Select clock source*/
	writel(reg_val, PLL_REFERENCE_CLOCK);

	/* Soft reset to calibration clocks */
	reg_val = readl(PLL_POWER_ON_AND_RESET);
	reg_val &= ~BIT(6);
	writel(reg_val, PLL_POWER_ON_AND_RESET);
	mdelay(1);
	reg_val |= BIT(6);
	writel(reg_val, PLL_POWER_ON_AND_RESET);
	mdelay(1);
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
		.size = CONFIG_TEXT_BASE - CFG_SYS_SDRAM_BASE,
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
		/*
		 * DDR region after u-boot text base
		 * added dummy 0xBAD0FF5EUL,
		 * will update the actual DDR limit
		 */
		.virt = CONFIG_TEXT_BASE + CONFIG_TEXT_SIZE,
		.phys = CONFIG_TEXT_BASE + CONFIG_TEXT_SIZE,
		.size = 0x0UL,
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

void board_update_RFA_settings(void)
{
	int ret;
	int slotId = 0; /* Default slotId 0 */
	uint32_t reg_val;
	uint32_t calDataOffset;
	uint32_t calData;
	uint32_t CDACIN;
	uint32_t CDACOUT;
	scm_param param;

	/* Check for Q6 DISABLE bit 15 */
	if ((readl(QFPROM_RAW_FEATURE_CONFIG_ROW0_LSB) >> 15) & 0x1)
		return;

	calDataOffset  = (((slotId * 150) + 4) * 1024 + 0x66C4);
	ret = get_partition_data("0:ART", calDataOffset, (uint8_t*)&calData, 4);
	if (ret < 0) {
		printf("\nget_partition_data failed, ret: %d\n", ret);
		return;
	}

	CDACIN = calData & 0x3FF;
	CDACOUT = (calData >> 16) & 0x1FF;

	if(((CDACIN == 0x0) || (CDACIN == 0x3FF)) &&
			((CDACOUT == 0x0) || (CDACOUT == 0x1FF))) {
		CDACIN = 0x230;
		CDACOUT = 0xB0;
	}

	CDACIN = CDACIN << 22;
	CDACOUT = CDACOUT << 13;

	memset(&param, 0, sizeof(scm_param));
	param.type = SCM_PHYA0_REGION_RD;

	/* args[0] has the addr */
	param.buff[0] = PHYA0_RFA_RFA_RFA_OTP_OTP_OV_1;
	param.arg_type[0] = SCM_VAL;

	param.len = 1;
	param.get_ret = 1;

	ret = ipq_scm_call(&param);
	if (ret) {
		printf("ipq_scm_call: PHYA0_RFA_RFA_RFA_OTP_OTP_OV_1"
			"read failed, ret : %d", ret);
		return;
	}

	reg_val = param.res.result[0];

	reg_val = (reg_val & 0xFFF9FFFF) | (0x3 << 17u);
	memset(&param, 0, sizeof(scm_param));
	param.type = SCM_PHYA0_REGION_WR;

	/* args[0] has the addr */
	param.buff[0] = PHYA0_RFA_RFA_RFA_OTP_OTP_OV_1;
	param.arg_type[0] = SCM_VAL;

	/* args[1] has the regval */
	param.buff[1] = reg_val;
	param.arg_type[1] = SCM_VAL;

	param.len = 2;

	ret = ipq_scm_call(&param);
	if (ret) {
		printf("ipq_scm_call: PHYA0_RFA_RFA_RFA_OTP_OTP_OV_1"
			"write failed, ret : %d", ret);
		return;
	}

	memset(&param, 0, sizeof(scm_param));
	param.type = SCM_PHYA0_REGION_RD;

	/* args[0] has the addr */
	param.buff[0] = PHYA0_RFA_RFA_RFA_OTP_OTP_XO_0;
	param.arg_type[0] = SCM_VAL;

	param.len = 1;
	param.get_ret = 1;

	ipq_scm_call(&param);

	reg_val = param.res.result[0];

	if((CDACIN == (reg_val & (0x3FF << 22))) &&
			(CDACOUT == (reg_val & (0x1FF << 13)))) {
		printf("ART data same as PHYA0_RFA_RFA_RFA_OTP_OTP_XO_0\n");
		return;
	}

	reg_val = ((reg_val & 0x1FFF) | ((CDACIN | CDACOUT) & (~0x1FFF)));
	memset(&param, 0, sizeof(scm_param));
	param.type = SCM_PHYA0_REGION_WR;

	/* args[0] has the addr */
	param.buff[0] = PHYA0_RFA_RFA_RFA_OTP_OTP_XO_0;
	param.arg_type[0] = SCM_VAL;

	/* args[1] has the regval */
	param.buff[1] = reg_val;
	param.arg_type[1] = SCM_VAL;

	param.len = 2;

	ret = ipq_scm_call(&param);
	if (ret) {
		printf("ipq_scm_call: PHYA0_RFA_RFA_RFA_OTP_OTP_XO_0"
			"write failed, ret : %d", ret);
		return;
	}
}
