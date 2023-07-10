// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

/*
 * Copyright (c) 2013, 2015-2017, 2020 The Linux Foundation. All rights reserved.
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

#include <common.h>
#include <command.h>
#include <smem.h>
#include <nand.h>
#include <asm/io.h>
#include <ubi_uboot.h>
#include <linux/sizes.h>
#include <fdtdec.h>

#include "ipq_board.h"

#ifdef CONFIG_CMD_UBI
static struct ubi_device *ubi;
#endif

/*
 * getpart_offset_size - retreive partition offset and size
 * @part_name - partition name
 * @offset - location where the offset of partition to be stored
 * @size - location where partition size to be stored
 *
 * Retreive partition offset and size in bytes with respect to the
 * partition specific flash block size
 */
int getpart_offset_size(char *part_name, uint32_t *offset, uint32_t *size)
{
	int i;
	uint32_t bsize;
	ipq_smem_flash_info_t *sfi = get_ipq_smem_flash_info();
	struct smem_ptable * ptable = get_ipq_part_table_info();
#ifdef CONFIG_CMD_NAND
	struct mtd_info *mtd = get_nand_dev_by_index(0);
#endif

	for (i = 0; i < ptable->len; i++) {
		struct smem_ptn *p = &ptable->parts[i];
		loff_t psize;
		if (!strncmp(p->name, part_name, SMEM_PTN_NAME_MAX)) {
			bsize = get_part_block_size(p, sfi);
			if (p->size == (~0u)) {
				/*
				 * Partition size is 'till end of device',
				 * calculate appropriately
				 */
#ifdef CONFIG_CMD_NAND
				psize = mtd->size - (((loff_t)p->start) \
								* bsize);
#else
				psize = 0;
#endif
			} else {
				psize = ((loff_t)p->size) * bsize;
			}

		*offset = ((loff_t)p->start) * bsize;
		*size = psize;
		break;
		}
	}

	if (i == ptable->len)
		return -ENOENT;

	return 0;
}

#ifdef CONFIG_CMD_UBI
int ubi_set_rootfs_part(void)
{
	int ret;
	uint32_t offset;
	uint32_t part_size = 0;
	uint32_t size_block, start_block;
	ipq_smem_flash_info_t *sfi = get_ipq_smem_flash_info();
	char runcmd[256];
	int i;

	if(ubi)
		del_mtd_partitions(ubi->mtd);

	if (((sfi->flash_type == SMEM_BOOT_NAND_FLASH) ||
		(sfi->flash_type == SMEM_BOOT_QSPI_NAND_FLASH))) {
		ret = smem_getpart(ROOT_FS_PART_NAME,
				&start_block, &size_block);
		if (ret)
			return ret;

		offset = sfi->flash_block_size * start_block;
		part_size = sfi->flash_block_size * size_block;
	} else if (sfi->flash_type == SMEM_BOOT_SPI_FLASH &&
				get_which_flash_param(ROOT_FS_PART_NAME)) {
		ret = getpart_offset_size(ROOT_FS_PART_NAME, &offset,
								&part_size);
		if (ret)
			return ret;
	}

	if (!part_size)
		return -ENOENT;

	if (ubi) {
		for (i = 0; i < ubi->vtbl_slots; i++) {
			if (ubi->volumes[i]) {
				kfree(ubi->volumes[i]->eba_tbl);
				kfree(ubi->volumes[i]);
				ubi->volumes[i] = NULL;
			}
		}
	}

	snprintf(runcmd, sizeof(runcmd),
		 "nand device 0 && "
		 "setenv mtdids nand0=nand0 && "
		 "setenv mtdparts mtdparts=nand0:0x%x@0x%x(fs),${msmparts} && "
		 "ubi part fs && ", part_size, offset);

	if (run_command(runcmd, 0) != CMD_RET_SUCCESS)
		return CMD_RET_FAILURE;

	if (ubi) {
		kfree(ubi);
		ubi = NULL;
	}

	ubi = ubi_devices[0];
	return 0;
}

static void print_ubi_vol_info(void)
{
	int i;
	int j=0;
	struct ubi_volume *vol;

	for (i = 0; i < (ubi->vtbl_slots + 1); i++) {
		vol = ubi->volumes[i];
		if (!vol)
			continue;	/* Empty record */
		if (vol->name_len <= UBI_VOL_NAME_MAX && strnlen(vol->name,
			vol->name_len + 1) == vol->name_len) {
			printf("\tubi vol %d %s\n", vol->vol_id, vol->name);
			j++;
		} else {
			printf("\tubi vol %d %c%c%c%c%c\n",
				vol->vol_id, vol->name[0], vol->name[1],
				vol->name[2], vol->name[3], vol->name[4]);
			j++;
		}
		if (j == ubi->vol_count - UBI_INT_VOL_COUNT)
			break;
	}
}
#endif

static int do_smeminfo(struct cmd_tbl *cmdtp, int flag, int argc,
			char * const argv[])
{
	ipq_smem_flash_info_t *sfi = get_ipq_smem_flash_info();
	int i;
	uint32_t bsize;
	struct smem_ptable * ptable = get_ipq_part_table_info();
#ifdef CONFIG_CMD_NAND
	struct mtd_info *mtd = get_nand_dev_by_index(0);
#endif
#ifdef CONFIG_CMD_UBI
	char runcmd[256];
	ubi_set_rootfs_part();
#endif
	if(sfi->flash_density != 0) {
		printf(	"flash_type:		0x%x\n"
			"flash_index:		0x%x\n"
			"flash_chip_select:	0x%x\n"
			"flash_block_size:	0x%x\n"
			"flash_density:		0x%x\n"
			"partition table offset	0x%x\n",
				sfi->flash_type, sfi->flash_index,
				sfi->flash_chip_select, sfi->flash_block_size,
				sfi->flash_density, sfi->primary_mibib);
	} else {
		printf(	"flash_type:		0x%x\n"
			"flash_index:		0x%x\n"
			"flash_chip_select:	0x%x\n"
			"flash_block_size:	0x%x\n"
			"partition table offset	0x%x\n",
				sfi->flash_type, sfi->flash_index,
				sfi->flash_chip_select, sfi->flash_block_size,
				sfi->primary_mibib);
	}

	if (ptable && ptable->len > 0) {
		printf("%-3s: " smem_ptn_name_fmt " %10s %16s %16s\n",
			"No.", "Name", "Attributes", "Start", "Size");
	} else {
		printf("Partition information not available\n");
	}

	for (i = 0;ptable && i < ptable->len; i++) {
		struct smem_ptn *p = &ptable->parts[i];
		loff_t psize;
		bsize = get_part_block_size(p, sfi);

		if (p->size == (~0u)) {
			/*
			 * Partition size is 'till end of device', calculate
			 * appropriately
			 */
#ifdef CONFIG_CMD_NAND
			psize = mtd->size - (((loff_t)p->start) * bsize);
#else
			psize = 0;
#endif
		} else {
			psize = ((loff_t)p->size) * bsize;
		}

		printf("%3d: " smem_ptn_name_fmt " 0x%08x %#16llx %#16llx\n",
		       i, p->name, p->attr, ((loff_t)p->start) * bsize, psize);
#ifdef CONFIG_CMD_UBI
		if (!strncmp(p->name, ROOT_FS_PART_NAME, SMEM_PTN_NAME_MAX)
		    && ubi) {
			print_ubi_vol_info();
		}
#endif
	}

#ifdef CONFIG_CMD_UBI
	snprintf(runcmd, sizeof(runcmd), "ubi detach && ");

	if (run_command(runcmd, 0) != CMD_RET_SUCCESS)
		return CMD_RET_FAILURE;
#endif
	return CMD_RET_SUCCESS;
}

U_BOOT_CMD(
	smeminfo,    1,    1,    do_smeminfo,
	"print SMEM FLASH information",
	"\n    - print flash partition layout\n"
);
