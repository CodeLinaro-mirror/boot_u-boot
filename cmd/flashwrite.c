/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * Copyright (c) 2018, 2020 The Linux Foundation. All rights reserved

 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

/*
 * FlashWrite command support
 */
#include <common.h>
#include <command.h>
#include <part.h>
#include <linux/mtd/mtd.h>
#include <nand.h>
#include <mmc.h>
#include <sdhci.h>
#include <ubi_uboot.h>
#include <fdtdec.h>
//#include <mach/qpic_nand.h>
#include <nand.h>

#include "../board/qti/common/ipq_board.h"

DECLARE_GLOBAL_DATA_PTR;
#ifdef CONFIG_SDHCI_SUPPORT
extern struct sdhci_host mmc_host;
#endif

#define GPT_PART_NAME "0:GPT"
#define GPT_BACKUP_PART_NAME "0:GPTBACKUP"

static int write_to_flash(int flash_type, uint32_t address, uint32_t offset,
uint32_t part_size, uint32_t file_size, char *layout)
{

	char runcmd[256];
	int nand_dev = 0;

	if (((flash_type == SMEM_BOOT_NAND_FLASH) ||
		(flash_type == SMEM_BOOT_QSPI_NAND_FLASH))) {

		snprintf(runcmd, sizeof(runcmd), "nand device %d && ",
				nand_dev);

		if (strcmp(layout, "default") != 0) {

			snprintf(runcmd + strlen(runcmd), sizeof(runcmd),
						"ipq_nand %s && ", layout);
		}

		snprintf(runcmd + strlen(runcmd), sizeof(runcmd),
			"nand erase 0x%x 0x%x && "
			"nand write 0x%x 0x%x 0x%x && ",
			offset, part_size,
			address, offset, file_size);

	} else if (flash_type == SMEM_BOOT_MMC_FLASH) {

		snprintf(runcmd, sizeof(runcmd),
			"mmc erase 0x%x 0x%x && "
			"mmc write 0x%x 0x%x 0x%x && ",
			offset, part_size,
			address, offset, file_size);

	} else if (flash_type == SMEM_BOOT_SPI_FLASH) {

		snprintf(runcmd, sizeof(runcmd),
			"sf probe && "
			"sf erase 0x%x 0x%x && "
			"sf write 0x%x 0x%x 0x%x && ",
			offset, part_size,
			address, offset, file_size);
	}

	if (run_command(runcmd, 0) != CMD_RET_SUCCESS)
		return CMD_RET_FAILURE;

	return CMD_RET_SUCCESS;
}

static int fl_erase(int flash_type, uint32_t offset, uint32_t part_size,
							 char *layout)
{
	char runcmd[256];
	int nand_dev = 0;

	if (((flash_type == SMEM_BOOT_NAND_FLASH) ||
		(flash_type == SMEM_BOOT_QSPI_NAND_FLASH))) {

		snprintf(runcmd, sizeof(runcmd), "nand device %d && ",
				nand_dev);
		if (strcmp(layout, "default") != 0) {
			snprintf(runcmd + strlen(runcmd), sizeof(runcmd),
						"ipq_nand %s && ", layout);
		}

		snprintf(runcmd + strlen(runcmd), sizeof(runcmd),
					"nand erase 0x%x 0x%x ",
					 offset, part_size);

	} else if (flash_type == SMEM_BOOT_MMC_FLASH) {

		snprintf(runcmd, sizeof(runcmd),
				"mmc erase 0x%x 0x%x ",
				 offset, part_size);

	} else if (flash_type == SMEM_BOOT_SPI_FLASH) {

		snprintf(runcmd, sizeof(runcmd),
				"sf probe && "
				"sf erase 0x%x 0x%x ",
				 offset, part_size);
	}

	if (run_command(runcmd, 0) != CMD_RET_SUCCESS)
		return CMD_RET_FAILURE;

	return CMD_RET_SUCCESS;
}

#ifdef CONFIG_MMC
static int prepare_mmc_flash(char *part_name, uint32_t *offset,
				uint32_t *part_size, uint32_t* file_size)
{
	int ret;
	struct disk_partition disk_info = {0};
	struct blk_desc *blk_dev;

	blk_dev = blk_get_devnum_by_uclass_id(UCLASS_MMC, 0);
	if (blk_dev != NULL) {

		if (strncmp(GPT_PART_NAME,
				(const char *)part_name,
				sizeof(GPT_PART_NAME))  == 0) {
			*file_size = *file_size / blk_dev->blksz;
			*offset = 0;
			*part_size = (ulong) *file_size;
		}
		else if (strncmp(GPT_BACKUP_PART_NAME,
				(const char *)part_name,
				sizeof(GPT_BACKUP_PART_NAME)) == 0) {
			*file_size = *file_size / blk_dev->blksz;
			*offset = (ulong) blk_dev->lba - *file_size;
			*part_size = (ulong) *file_size;
		}
		else
		{
			ret = part_get_info_efi_by_name(
					part_name, &disk_info);
			if (ret)
				return ret;
			*offset = (ulong)disk_info.start;
			*part_size = (ulong)disk_info.size;
		}
	}
	return ret;
}
#endif

static int prepare_nand_flash(char *part_name, uint32_t *offset,
		uint32_t *part_size)
{

	uint32_t size_block, start_block;
	unsigned int active_part = 0;
	int ret;
	ipq_smem_flash_info_t *sfi = get_ipq_smem_flash_info();

	if ((sfi->flash_type == SMEM_BOOT_SPI_FLASH &&
		((sfi->flash_secondary_type == SMEM_BOOT_NAND_FLASH)||
		(sfi->flash_secondary_type == SMEM_BOOT_QSPI_NAND_FLASH)))
		&& (strncmp(part_name, "rootfs", 6) == 0)) {

		if (sfi->rootfs.offset == 0xBAD0FF5E) {
			if (sfi->ipq_smem_bootconfig_info == 0)
				active_part = get_rootfs_active_partition();

			*offset = (ulong) active_part * IPQ_NAND_ROOTFS_SIZE;
			*part_size = (ulong) IPQ_NAND_ROOTFS_SIZE;
		}

	} else {
		ret = smem_getpart(part_name, &start_block, &size_block);
		*offset = sfi->flash_block_size * start_block;
		*part_size = sfi->flash_block_size * size_block;
	}

	return ret;
}

int do_flash(struct cmd_tbl *cmdtp, int flag, int argc,
char * const argv[])
{
	int flash_cmd = 0;
	uint32_t offset, part_size, adj_size;
	uint32_t load_addr = 0;
	uint32_t file_size = 0;
	uint32_t size_block, start_block, file_size_cpy;
	char *part_name = NULL, *filesize, *loadaddr;
	int flash_type = 0;
	int ret, retn;
	char *layout = NULL;
	offset = 0;
	part_size = 0;
	layout = "default";
	retn = CMD_RET_FAILURE;
	ipq_smem_flash_info_t *sfi = get_ipq_smem_flash_info();
#ifdef CONFIG_MMC
	struct disk_partition disk_info = {0};
#endif
#ifdef CONFIG_CMD_NAND
	struct mtd_info *nand = get_nand_dev_by_index(0);
#endif
	if (strcmp(argv[0], "flash") == 0)
		flash_cmd = 1;

	if (flash_cmd) {
		if ((argc < 2) || (argc > 5))
			return CMD_RET_USAGE;

		if (argc ==3 || argc == 5) {
			if(strcmp(argv[argc-1], "mmc") == 0)
				flash_type = SMEM_BOOT_MMC_FLASH;
			else if (strcmp(argv[argc-1], "nand") == 0)
				flash_type = SMEM_BOOT_QSPI_NAND_FLASH;
			else if (strcmp(argv[argc-1], "nor") == 0)
				flash_type = SMEM_BOOT_SPI_FLASH;
			else
				return CMD_RET_USAGE;
		}

		if (argc == 2 || argc == 3) {
			loadaddr = env_get("fileaddr");
			if (loadaddr != NULL)
				load_addr = simple_strtoul(loadaddr, NULL, 16);
			else
				return CMD_RET_USAGE;

			filesize = env_get("filesize");
			if (filesize != NULL)
				file_size = simple_strtoul(filesize, NULL, 16);
			else
				return CMD_RET_USAGE;

		} else if (argc == 4 || argc ==5) {
			load_addr = simple_strtoul(argv[2], NULL, 16);
			file_size = simple_strtoul(argv[3], NULL, 16);

		} else
			return CMD_RET_USAGE;

		file_size_cpy = file_size;
	}
	else {
		if (argc != 2)
			return CMD_RET_USAGE;
	}

	flash_type = flash_type ? flash_type : sfi->flash_type;
	part_name = argv[1];

	if ((((sfi->flash_type == SMEM_BOOT_NAND_FLASH) ||
		(sfi->flash_type == SMEM_BOOT_QSPI_NAND_FLASH)) &&
		(flash_type == SMEM_BOOT_QSPI_NAND_FLASH)) ||
		((flash_type == SMEM_BOOT_QSPI_NAND_FLASH) &&
		(sfi->flash_type == SMEM_BOOT_SPI_FLASH))) {
#ifndef CONFIG_CMD_NAND
		goto usage_err;
#endif
		ret = prepare_nand_flash(part_name, &offset, &part_size);
		if(ret)
			return retn;
#ifdef CONFIG_MMC
	} else if (((sfi->flash_type == SMEM_BOOT_MMC_FLASH) ||
		(sfi->flash_type == SMEM_BOOT_NO_FLASH) ||
		(sfi->flash_type == SMEM_BOOT_SPI_FLASH)) &&
		(flash_type == SMEM_BOOT_MMC_FLASH)) {

		ret = prepare_mmc_flash(part_name, &offset,
						&part_size, &file_size);
		if(ret)
			return retn;


#endif
	} else if (flash_type == SMEM_BOOT_SPI_FLASH) {


#ifndef CONFIG_SPI
		goto usage_err;
#endif

		if (get_which_flash_param(part_name)) {

			/* NOR + NAND*/
			flash_type = SMEM_BOOT_NAND_FLASH;
			ret = getpart_offset_size(part_name, &offset,
					&part_size);
			if (ret)
				return retn;

		} else if (((sfi->flash_secondary_type ==
				SMEM_BOOT_NAND_FLASH)||
				(sfi->flash_secondary_type ==
				 SMEM_BOOT_QSPI_NAND_FLASH))
				&& (strncmp(part_name, "rootfs", 6) == 0)) {

			flash_type = sfi->flash_secondary_type;

			ret = prepare_nand_flash(part_name, &offset,
					&part_size);
			if(ret)
				return retn;

#ifdef CONFIG_MMC
		} else if ((smem_getpart(part_name, &start_block, &size_block)
				== -ENOENT) && (sfi->rootfs.offset ==
					0xBAD0FF5E)){

			/* NOR + EMMC */
			ret = prepare_mmc_flash(part_name, &offset,
						&part_size, &file_size);
			if(ret)
				return retn;

			flash_type = SMEM_BOOT_MMC_FLASH;
#endif
		} else {

			ret = smem_getpart(part_name, &start_block,
							&size_block);
			if (ret)
				return retn;

			offset = sfi->flash_block_size * start_block;
			part_size = sfi->flash_block_size * size_block;
		}
	} else
		return CMD_RET_USAGE;

	if (flash_cmd) {
#ifdef CONFIG_CMD_NAND
		if (((flash_type == SMEM_BOOT_NAND_FLASH) ||
			(flash_type == SMEM_BOOT_QSPI_NAND_FLASH))) {

			adj_size = file_size % nand->writesize;
			if (adj_size)
				file_size = file_size + (nand->writesize -
						adj_size);
		}
#endif
#ifdef CONFIG_MMC
		if (flash_type == SMEM_BOOT_MMC_FLASH) {
			ret = part_get_info_efi_by_name(
					part_name, &disk_info);
			if (ret)
				return retn;

			if (disk_info.blksz) {
				file_size = file_size / disk_info.blksz;
				adj_size = file_size_cpy % disk_info.blksz;
				if (adj_size)
					file_size = file_size + 1;
			}
		}
#endif
		if (file_size > part_size) {
			printf("Image size is greater than "
					"partition memory\n");
			return CMD_RET_FAILURE;
		}

		ret = write_to_flash(flash_type, load_addr, offset, part_size,
							file_size, layout);
	} else
		ret = fl_erase(flash_type, offset, part_size, layout);

	return ret;
usage_err:
	return CMD_RET_USAGE;

}

U_BOOT_CMD(
	flash,       5,      0,      do_flash,
	"flash part_name \n"
	"\tflash part_name flash_type \n"
	"\tflash part_name load_addr file_size \n"
	"\tflash part_name load_addr file_size flash_type\n",
	"flash the image at load_addr, given file_size in hex\n"
);

U_BOOT_CMD(
	flasherase,       4,      0,      do_flash,
	"flerase part_name \n",
	"erases on flash the given partition \n"
);
