// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2013, 2015-2017, 2020 The Linux Foundation. All rights reserved.
 *
 * Copyright (c) 2023, Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * Based on smem.c from lk.
 *
 * Copyright (c) 2009, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <asm/byteorder.h>
#include <memalign.h>
#include <fdtdec.h>
#include <mmc.h>
#include <sdhci.h>
#include <command.h>
#include <env.h>
#include <env_internal.h>
#include <linux/delay.h>
#include <part.h>
#include <dm.h>
#include <smem.h>
#include <common.h>

#include "ipq_board.h"

DECLARE_GLOBAL_DATA_PTR;

uint32_t g_board_machid;

struct udevice *smem;

ipq_smem_flash_info_t ipq_smem_flash_info;
struct smem_ptable *ptable;
socinfo_t ipq_socinfo;

extern int ipq_smem_get_socinfo(void);
extern int part_get_info_efi(struct blk_desc *dev_desc, int part,
		struct disk_partition *info);

__weak
void ipq_uboot_fdt_fixup(void)
{
	return;
}

__weak void set_flash_secondary_type(uint32_t flash_type)
{
	return;
}


ipq_smem_flash_info_t * get_ipq_smem_flash_info(void)
{
	return &ipq_smem_flash_info;
}

socinfo_t * get_socinfo(void)
{
	return &ipq_socinfo;
}

void *smem_get_item(unsigned int item) {

	int ret;
	struct udevice *smem_tmp;
	const char *name = "smem";
	size_t size;
	unsigned long int reloc_flag = (gd->flags & GD_FLG_RELOC);

	if (reloc_flag == 0)
		ret = uclass_get_device_by_name(UCLASS_SMEM, name, &smem_tmp);
	else if(!smem)
		ret = uclass_get_device_by_name(UCLASS_SMEM, name, &smem);

	if (ret < 0) {
		printf("Failed to find SMEM node. Check device tree %d\n", ret);
		return 0;
	}

	return smem_get(reloc_flag ? smem : smem_tmp, -1, item, &size);

}

uint32_t get_part_block_size(struct smem_ptn *p,
					   ipq_smem_flash_info_t *sfi)
{
#ifdef CONFIG_CMD_NAND
        return (part_which_flash(p) == 1) ?
		get_nand_block_size(is_spi_nand_available())
		: sfi->flash_block_size;
#else
	return sfi->flash_block_size;
#endif
}

/*
 * smem_getpart - retreive partition start and size
 * @part_name: partition name
 * @start: location where the start offset is to be stored
 * @size: location where the size is to be stored
 *
 * Retreive the start offset in blocks and size in blocks, of the
 * specified partition.
 */
int smem_getpart(char *part_name, uint32_t *start, uint32_t *size)
{
	unsigned i;
	ipq_smem_flash_info_t *sfi = &ipq_smem_flash_info;
	struct smem_ptn *p;
	uint32_t bsize;

	for (i = 0; i < ptable->len; i++) {
		if (!strncmp(ptable->parts[i].name, part_name,
			     SMEM_PTN_NAME_MAX))
			break;
	}
	if (i == ptable->len)
		return -ENOENT;

	p = &ptable->parts[i];
	bsize = get_part_block_size(p, sfi);

	*start = p->start;

	if (p->size == (~0u)) {
		/*
		 * Partition size is 'till end of device', calculate
		 * appropriately
		 */
#ifdef CONFIG_CMD_NAND
		*size = (nand_info[get_device_id_by_part(p)].size /
			 bsize) - p->start;
#else
		*size = 0;
		bsize = bsize;
#endif
	} else {
		*size = p->size;
	}

	return 0;
}

/*
 * retrieve the which_flash flag based on partition name.
 * flash_var is 1 if partition is in NAND.
 * flash_var is 0 if partition is in NOR.
 * flash_var is -1 if partition is in EMMC.
 */
unsigned int get_which_flash_param(char *part_name)
{
	int i;
	int flash_var = -1;

	for (i = 0; i < ptable->len; i++) {
		struct smem_ptn *p = &ptable->parts[i];
		if (strcmp(p->name, part_name) == 0)
			flash_var = part_which_flash(p);
	}

	return flash_var;
}

int get_current_board_flash_config(void)
{
	int ret;
	int board_type;

	ret = get_which_flash_param("rootfs");
	if (ret == -1) {
		board_type = SMEM_BOOT_NORPLUSEMMC;
	} else if (ret) {
		board_type = SMEM_BOOT_NORPLUSNAND;
	} else {
		board_type = SMEM_BOOT_SPI_FLASH;
	}

	return board_type;
}
/*
 * get flash block size based on partition name.
 */
static inline uint32_t get_flash_block_size(char *name,
					    ipq_smem_flash_info_t *smem)
{
#ifdef CONFIG_CMD_NAND
	return (get_which_flash_param(name) == 1) ?
		get_nand_block_size(is_spi_nand_available())
		: smem->flash_block_size;
#else
	return smem->flash_block_size;
#endif
}

void ipq_set_part_entry(char *name, ipq_smem_flash_info_t *smem,
		ipq_part_entry_t *part, uint32_t start, uint32_t size)
{
	uint32_t bsize = get_flash_block_size(name, smem);
	part->offset = ((loff_t)start) * bsize;
	part->size = ((loff_t)size) * bsize;
}

void get_kernel_fs_part_details(void)
{
	int ret, i;
	uint32_t start;         /* block number */
	uint32_t size;          /* no. of blocks */

	ipq_smem_flash_info_t *smem = &ipq_smem_flash_info;

	struct { char *name; ipq_part_entry_t *part; } entries[] = {
		{ "0:HLOS", &smem->hlos },
		{ "rootfs", &smem->rootfs },
	};

	for (i = 0; i < ARRAY_SIZE(entries); i++) {
		ret = smem_getpart(entries[i].name, &start, &size);
		if (ret < 0) {
			ipq_part_entry_t *part = entries[i].part;

			debug("cdp: get part failed for %s\n",
				entries[i].name);
			part->offset = 0xBAD0FF5E;
			part->size = 0xBAD0FF5E;
		} else {
			ipq_set_part_entry(entries[i].name, smem,
					entries[i].part, start, size);
		}
	}

	return;
}

int board_init(void)
{
	ipq_smem_bootconfig_info_t *ipq_smem_bootconfig_info;
	ipq_smem_flash_info_t *sfi = &ipq_smem_flash_info;
	uint32_t board_type;
	uint32_t *flash_type;
	uint32_t *flash_chip_select;
	uint32_t *primary_mibib;
	uint32_t *flash_index;
	uint32_t *flash_block_size;
	uint32_t *flash_density;

	gd->bd->bi_boot_params = BOOT_PARAMS_ADDR;

	flash_type = smem_get_item(SMEM_BOOT_FLASH_TYPE);

	if (!flash_type) {
		printf("Failed to find SMEM partition.\n");
		return -ENODEV;
	}

	flash_index = smem_get_item(SMEM_BOOT_FLASH_INDEX);

	if (!flash_index) {
		printf("Failed to find SMEM partition.\n");
		return -ENODEV;
	}

	flash_chip_select = smem_get_item(SMEM_BOOT_FLASH_CHIP_SELECT);

	if (!flash_chip_select) {
		printf("Failed to find SMEM partition.\n");
		return -ENODEV;
	}

	flash_block_size = smem_get_item(SMEM_BOOT_FLASH_BLOCK_SIZE);

	if (!flash_block_size) {
		printf("Failed to find SMEM partition.\n");
		return -ENODEV;
	}

	flash_density = smem_get_item(SMEM_BOOT_FLASH_DENSITY);

	if (!flash_density) {
		printf("Failed to find SMEM partition.\n");
		return -ENODEV;
	}

	primary_mibib = smem_get_item(SMEM_PARTITION_TABLE_OFFSET);

	if (!primary_mibib) {
		*primary_mibib = 0;
	}

	ipq_smem_bootconfig_info = smem_get_item(SMEM_BOOT_DUALPARTINFO);

	if (!ipq_smem_bootconfig_info ||
		(ipq_smem_bootconfig_info->magic_start !=
			_SMEM_DUAL_BOOTINFO_MAGIC_START) ||
		(ipq_smem_bootconfig_info->magic_end !=
			_SMEM_DUAL_BOOTINFO_MAGIC_END)) {
		ipq_smem_bootconfig_info = 0;
		debug("Failed to find SMEM partition.\n");
	}

	sfi->flash_type = *flash_type;
	sfi->flash_index = *flash_index;
	sfi->flash_chip_select = *flash_chip_select;
	sfi->flash_block_size = *flash_block_size;
	sfi->flash_density = *flash_density;
	sfi->primary_mibib = *primary_mibib;
	sfi->ipq_smem_bootconfig_info = ipq_smem_bootconfig_info;

	switch(sfi->flash_type) {
	case SMEM_BOOT_MMC_FLASH:
	case SMEM_BOOT_NO_FLASH:
		break;
	default:
		ptable = smem_get_item(SMEM_AARM_PARTITION_TABLE);

		if (!ptable) {
			printf("Failed to find SMEM partition.\n");
			return -ENODEV;
		}

		if (ptable->magic[0] != _SMEM_PTABLE_MAGIC_1 ||
			ptable->magic[1] != _SMEM_PTABLE_MAGIC_2)
			return -ENOMSG;

		get_kernel_fs_part_details();
	}

	board_type = (sfi->flash_type == SMEM_BOOT_SPI_FLASH) ?
			get_current_board_flash_config() :
			sfi->flash_type;

	switch(board_type) {
	case SMEM_BOOT_NORPLUSEMMC:
		sfi->flash_secondary_type = SMEM_BOOT_MMC_FLASH;
		break;
	case SMEM_BOOT_NORPLUSNAND:
		sfi->flash_secondary_type = SMEM_BOOT_QSPI_NAND_FLASH;
		break;
	default:
		break;
	}
	/*
	 * To set SoC specific secondary flash type to
	 * eMMC/NAND device based on the one that is enabled.
	 */
	set_flash_secondary_type(sfi->flash_secondary_type);

	/*
	 * get soc_version, cpu_type, machid
	 */

	ipq_smem_get_socinfo();

#ifdef CONFIG_BOARD_TYPES
	gd->board_type = board_type;
#endif
	return 0;
}

int ipq_smem_get_socinfo()
{
	union ipq_platform *platform_type;

	platform_type = smem_get_item(SMEM_HW_SW_BUILD_ID);
	if(!platform_type) {
		printf("Failed to find SMEM partition.\n");
		return -ENODEV;
	}

	ipq_socinfo.cpu_type = platform_type->v1.id;
	ipq_socinfo.version = platform_type->v1.version;
	ipq_socinfo.soc_version_major =
				SOCINFO_VERSION_MAJOR(ipq_socinfo.version);
	ipq_socinfo.soc_version_minor =
				SOCINFO_VERSION_MINOR(ipq_socinfo.version);
	ipq_socinfo.machid = g_board_machid;

	return 0;

}

/*
 * This function is called in the very beginning.
 * Retreive the machtype info from SMEM and map the board specific
 * parameters. Shared memory region at Dram address
 * contains the machine id/ board type data polulated by SBL.
 */
int board_early_init_f(void)
{
#ifdef CONFIG_SMEM_VERSION_C
	union ipq_platform *platform_type;

	platform_type = smem_get_item(SMEM_HW_SW_BUILD_ID);
	if(!platform_type) {
		printf("Failed to find SMEM partition.\n");
		return -ENODEV;
	}

	g_board_machid = ((platform_type->v1.hw_platform << 24) |
			  ((SOCINFO_VERSION_MAJOR(
				platform_type->v1.platform_version)) << 16) |
			  ((SOCINFO_VERSION_MINOR(
				platform_type->v1.platform_version)) << 8) |
			  (platform_type->v1.hw_platform_subtype));
	return 0;
#else
	struct smem_machid_info *machid_info;
	machid_info = smem_get_item(SMEM_MACHID_INFO_LOCATION);
	if(!machid_info) {
		printf("Failed to find SMEM partition.\n");
		return -ENODEV;
	}
		g_board_machid = machid_info->machid;
		return 0;
#endif

	return 0;
}

int board_fix_fdt(void *rw_fdt_blob)
{
	ipq_uboot_fdt_fixup();
	return 0;
}

int board_late_init(void)
{
	return 0;
}

int dram_init(void)
{
	int i, ret = CMD_RET_SUCCESS;
	int count = 0;
	struct smem_ram_ptable *ram_ptable;
	struct smem_ram_ptn *p;

	ram_ptable = smem_get_item(SMEM_USABLE_RAM_PARTITION_TABLE);
	if (!ram_ptable) {
		printf("Failed to find SMEM partition.\n");
		ret = -ENODEV;
	}

	gd->ram_size = 0;
	/* Check validy of RAM */
	for (i = 0; i < CONFIG_RAM_NUM_PART_ENTRIES; i++) {
		p = &ram_ptable->parts[i];
		if (p->category == RAM_PARTITION_SDRAM &&
					p->type == RAM_PARTITION_SYS_MEMORY) {
			gd->ram_size += p->size;
			debug("Detected memory bank %u: "
				"start: 0x%llx size: 0x%llx\n",
					count, p->start, p->size);
			count++;
		}
        }

	if (!count) {
		printf("Failed to detect any memory bank\n");
		ret = CMD_RET_FAILURE;
	}

	return ret;
}

void *env_sf_get_env_addr(void)
{
        return NULL;
}

int part_get_info_efi_by_name(const char *name, struct disk_partition *info)
{
	struct blk_desc *mmc_dev;
	int ret = -1;
	int i;

	mmc_dev = blk_get_devnum_by_uclass_id(UCLASS_MMC, 0);

	if (mmc_dev->type == DEV_TYPE_UNKNOWN)
		goto done;

	for (i = 1; i < GPT_ENTRY_NUMBERS; i++) {
		ret = part_get_info_efi(mmc_dev, i, info);
		if (ret != 0) {
			/* no more entries in table */
			goto done;
		}
		if (strcmp(name, (const char *)info->name) == 0) {
			/* matched */
			ret = 0;
			goto done;
		}
	}
done:
	return ret;
}

enum env_location env_get_location(enum env_operation op, int prio)
{
	int ret;
	uint32_t *flash_type;

	if (prio)
		return ENVL_UNKNOWN;

	flash_type = smem_get_item(SMEM_BOOT_FLASH_TYPE);
	if (!flash_type) {
		printf("Failed to find SMEM partition.\n");
		return -ENODEV;
	}

	if (*flash_type == SMEM_BOOT_SPI_FLASH) {
		ret = ENVL_SPI_FLASH;
	} else if (*flash_type == SMEM_BOOT_MMC_FLASH) {
		ret = ENVL_MMC;
	} else if ((*flash_type == SMEM_BOOT_QSPI_NAND_FLASH) ||
		(*flash_type == SMEM_BOOT_NAND_FLASH)) {
		ret = ENVL_NAND;
	} else {
		ret = ENVL_NOWHERE;
	}

	return ret;
}

int mmc_get_env_addr(struct mmc *mmc, int copy, u32 *env_addr)
{
	int ret;
	struct disk_partition disk_info;

	ret = part_get_info_efi_by_name("0:APPSBLENV", &disk_info);

	if (!ret) {
		*env_addr = (u32)disk_info.start * disk_info.blksz;
	}

	return ret;
}
