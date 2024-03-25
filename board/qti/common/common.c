/*
 * Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <asm/global_data.h>
#include <cpu_func.h>
#include <sysreset.h>
#include <common.h>
#include <command.h>
#include <malloc.h>
#include <memalign.h>
#include <bootm.h>
#include <mach/ipq_scm.h>
#ifdef CONFIG_IPQ_MMC
#include <mmc.h>
#endif
#ifdef CONFIG_IPQ_SPI_NOR
#include <spi.h>
#include <spi_flash.h>
#endif
#ifdef CONFIG_IPQ_NAND
#include <nand.h>
#endif
#ifdef CONFIG_CMD_UBI
#include <ubi_uboot.h>
#endif
#include <linux/psci.h>

#include "ipq_board.h"

DECLARE_GLOBAL_DATA_PTR;

#define MMC_MID_MICRON 0xFE
#define MMC_PNM_MICRON 0x4D4D43333247

#define MMC_GET_MID(CID0) (CID0 >> 24)
#define MMC_GET_PNM(CID0, CID1, CID2) (((long long int)(CID0 & 0xff) << 40) | \
                ((long long int)CID1 << 8) |                                  \
                (CID2 >> 24))

#define MMC_CMD_SET_WRITE_PROT		28
#define MMC_CMD_CLR_WRITE_PROT		29

#define MMC_ADDR_OUT_OF_RANGE(resp)	((resp >> 31) & 0x01)

/*
 * CSD fields
*/
#define WP_GRP_ENABLE(csd)		((csd[3] & 0x80000000) >> 31)
#define WP_GRP_SIZE(csd)		((csd[2] & 0x0000001f))
#define ERASE_GRP_MULT(csd)		((csd[2] & 0x000003e0) >> 5)
#define ERASE_GRP_SIZE(csd)		((csd[2] & 0x00007c00) >> 10)


#define EXT_CSD_BOOT_WP_B_PERM_WP_EN	(0x04)  /* permanent write-protect */

#ifndef CFG_UBI_FS_NAME
#define	CFG_UBI_FS_NAME		"fs"
#endif

#ifdef CONFIG_IPQ_MMC
int mmc_send_status(struct mmc *mmc, unsigned int *status);
int mmc_switch(struct mmc *mmc, u8 set, u8 index, u8 value);
#endif

struct spi_flash *flash = NULL;

enum atf_status_t {
	ATF_STATE_DISABLED,
	ATF_STATE_ENABLED,
	ATF_STATE_UNKNOWN,
} atf_status = ATF_STATE_UNKNOWN;

int gpt_find_which_flash(gpt_entry *p)
{
	/*
	 * bit 3 of gpt attribute denotes partition present in nand flash
	 */
	if (p->attributes.raw & CFG_IPQ_NAND_PART)
		return 1;

	return 0;
}

void arch_preboot_os(void)
{

/*
 * restrict booting if image is not authenticated
 * in secure board.
 */
	uint32_t board_type = gd->board_type;

	if(!(board_type & SECURE_BOARD))
		return;

	if((board_type & SECURE_BOARD) && (board_type & ATF_ENABLED))
		return;

	if((board_type & SECURE_BOARD) &&
		!(board_type & ATF_ENABLED) &&
		(board_type & KERNEL_AUTH_SUCCESS))
	{
		char *env = env_get("rootfs_auth");

		if(env)
			if(board_type & ROOTFS_AUTH_SUCCESS)
				return;
			else
				reset_cpu();
		else
			return;
	} else {
		reset_cpu();
	}

	return;
}

#ifdef CONFIG_CMD_UBI
long long ubi_get_volume_size(char *volume)
{
	int i;
	struct ubi_device *ubi = ubi_get_device(0);
	struct ubi_volume *vol = NULL;

	if(NULL == ubi)
		return -ENODEV;

	for (i = 0; i < ubi->vtbl_slots; i++) {
		vol = ubi->volumes[i];
		if (vol && !strcmp(vol->name, volume))
			return vol->used_bytes;
	}

	printf("Volume %s not found!\n", volume);
	return -ENODEV;
}
#endif

bool is_atf_enbled(void)
{
	scm_param param;
	int ret = -1;

	if (likely(atf_status != ATF_STATE_UNKNOWN))
		return (atf_status == ATF_STATE_ENABLED);

	do {
		ret = -ENOTSUPP;
		IPQ_SCM_CHECK_SCM_SUPPORT(param, SCM_SMC_FNID(QCOM_SCM_SVC_INFO,
						QCOM_GET_SECURE_STATE_CMD));
		param.get_ret = true;
		ret = ipq_scm_call(&param);

		if(!ret && (le32_to_cpu(param.res.result[0]) > 0)) {
			do {
				ret = -ENOTSUPP;
				check_atf_support(param);
				ret = ipq_scm_call(&param);
				if(ret == 0 && (param.res.result[0] & 0x08))
					atf_status = ATF_STATE_ENABLED;
			} while (0);

			if (ret == -ENOTSUPP) {
				printf("Unsupported SCM call\n");
				return false;
			}

		} else {
			return false;
		}

	} while (0);

	if (ret == -ENOTSUPP) {
		printf("Unsupported SCM call\n");
		return false;
	}

	return atf_status == ATF_STATE_ENABLED;

}

#ifdef CONFIG_SCM_V1
bool is_secure_boot_v1(void)
{
	scm_param param;
	uint8_t *buff = NULL;
	int ret = -1;
	bool status = false;

	buff = (uint8_t *)malloc_cache_aligned(CONFIG_SYS_CACHELINE_SIZE);
	if(!buff) {
		printf("Unable allocate memory\n");
		return false;
	}

	do {
		ret = -ENOTSUPP;
		IPQ_SCM_SECURE_BOOT(param, (uintptr_t)buff, sizeof(uint8_t));
		ret = ipq_scm_call(&param);

		/* invalidate cache to update latest value in buff */
		invalidate_dcache_range((unsigned long)buff,
					(unsigned long)buff +
					CONFIG_SYS_CACHELINE_SIZE);

		if(!ret && *(uint8_t *)buff == 1)
			status  = true;
	} while (0);

	if (ret == -ENOTSUPP) {
		printf("Unsupported SCM call\n");
	}

	if(buff)
		free(buff);

	return status;
}
#endif

#ifdef CONFIG_SCM_V2
bool is_secure_boot_v2(void)
{
	scm_param param;
	int ret = -1;
	struct fuse_payload {
		u32 fuse_addr;
		u32 lsb_val;
		u32 msb_val;
	};
	struct fuse_payload *fuse = NULL;
	size_t size = sizeof(struct fuse_payload);
	bool status = false;

	size = roundup(size, CONFIG_SYS_CACHELINE_SIZE);

	fuse = malloc_cache_aligned(size);
	if(!fuse)
		return false;

	memset(fuse, 0, sizeof(struct fuse_payload));

	fuse[0].fuse_addr = QFPROM_CORR_TME_OEM_ATE_ROW0_LSB;

	do {
		ret = -ENOTSUPP;
		IPQ_SCM_READ_FUSE(param, (unsigned long)fuse,
					sizeof(struct fuse_payload));
		/* invalidate cache to update latest value in buff */
		flush_dcache_range((unsigned long)fuse,
					(unsigned long)fuse + size);
		ret = ipq_scm_call(&param);

		if(ret)
		{
			ret = -1;
			break;
		}

		if(fuse[0].lsb_val & OEM_SEC_BOOT_ENABLE)
		{
			status = true;
		}
	} while (0);

	if (ret == -ENOTSUPP) {
		printf("Unsupported SCM call\n");
	}

	if(fuse)
		free(fuse);
	return status;
}
#endif

#if CONFIG_IPQ_MMC
int mmc_send_wp_set_clr(struct mmc *mmc, unsigned int start,
			unsigned int size, int set_clr)
{
	unsigned int err;
	unsigned int wp_group_size, count, i;
	struct mmc_cmd cmd;
	unsigned int status;

	wp_group_size = (WP_GRP_SIZE(mmc->csd) + 1) * mmc->erase_grp_size;
	count = DIV_ROUND_UP(size, wp_group_size);

	if (set_clr)
		cmd.cmdidx = MMC_CMD_SET_WRITE_PROT;
	else
		cmd.cmdidx = MMC_CMD_CLR_WRITE_PROT;
	cmd.resp_type = MMC_RSP_R1b;

	for (i = 0; i < count; i++) {
		cmd.cmdarg = start + (i * wp_group_size);
		err = mmc_send_cmd(mmc, &cmd, NULL);
		if (err) {
			printf("%s: Error at block 0x%x - %d\n", __func__,
			       cmd.cmdarg, err);
			return err;
		}

		if(MMC_ADDR_OUT_OF_RANGE(cmd.response[0])) {
			printf("%s: mmc block(0x%x) out of range", __func__,
			       cmd.cmdarg);
			return -EINVAL;
		}

		err = mmc_send_status(mmc, &status);
		if (err)
			return err;
	}

	return 0;
}

int mmc_write_protect(struct mmc *mmc, unsigned int start_blk,
		      unsigned int cnt_blk, int set_clr)
{
	ALLOC_CACHE_ALIGN_BUFFER(u8, ext_csd, MMC_MAX_BLOCK_LEN);
	int err;
	unsigned int wp_group_size;

	if (!WP_GRP_ENABLE(mmc->csd))
		return -1; /* group write protection is not supported */

	err = mmc_send_ext_csd(mmc, ext_csd);

	if (err) {
		debug("ext_csd register cannot be retrieved\n");
		return err;
	}

	if ((ext_csd[EXT_CSD_USER_WP] & EXT_CSD_BOOT_WP_B_SEC_WP_SEL)
	    || (ext_csd[EXT_CSD_USER_WP] & EXT_CSD_BOOT_WP_B_PERM_WP_EN)) {
		printf("User power-on write protection is disabled. \n");
		return -1;
	}

	err = mmc_switch(mmc, EXT_CSD_CMD_SET_NORMAL, EXT_CSD_USER_WP,
		         EXT_CSD_BOOT_WP_B_PWR_WP_EN);
	if (err) {
		printf("Failed to enable user power-on write protection\n");
		return err;
	}

	wp_group_size = (WP_GRP_SIZE(mmc->csd) + 1) * mmc->erase_grp_size;
	if ((MMC_GET_MID(mmc->cid[0]) == MMC_MID_MICRON) &&
		(MMC_GET_PNM(mmc->cid[0], mmc->cid[1],
					mmc->cid[2]) == MMC_PNM_MICRON))
		wp_group_size *= 2;

	if (!cnt_blk || start_blk % wp_group_size || cnt_blk % wp_group_size) {
		printf("Error: Unaligned offset/count. offset/count should be "
				"aligned to 0x%x blocks\n", wp_group_size);
		return -1;
	}

	err = mmc_send_wp_set_clr(mmc, start_blk, cnt_blk, set_clr);

	return err;
}

static int do_mmc_protect (struct cmd_tbl *cmdtp, int flag,
			   int argc, char * const argv[])
{
	struct mmc *mmc;
	unsigned int ret;
	unsigned int blk, cnt;
	int curr_device = -1;

	if (curr_device < 0) {
		if (get_mmc_num() > 0) {
			curr_device = 0;
		} else {
			puts("No MMC device available\n");
			return CMD_RET_FAILURE;
		}
	}

	mmc = find_mmc_device(curr_device);
	if (!mmc) {
		printf("no mmc device at slot %x\n", curr_device);
		return -ENOMEM;
	}

	if (argc != 3)
		return CMD_RET_USAGE;

	blk = (unsigned int)simple_strtoul(argv[1], NULL, 16);
	cnt = (unsigned int)simple_strtoul(argv[2], NULL, 16);

	ret = mmc_write_protect(mmc, blk, cnt, 1);

	if (!ret)
		printf("Offset: 0x%x Count: %d blocks\nDone!\n", blk, cnt);

	return ret ? CMD_RET_FAILURE : CMD_RET_SUCCESS;
}

U_BOOT_CMD(
	mmc_protect, 3, 0, do_mmc_protect,
	"MMC write protect",
	"mmc_protect start_blk cnt_blk\n"
);
#endif

#ifdef CONFIG_IPQ_SMP_CMD_SUPPORT
static int qti_invoke_psci_fn_smc
		(unsigned long function_id, unsigned long arg0,
		 unsigned long arg1, unsigned long arg2)
{
	struct arm_smccc_res res;
	arm_smccc_smc(function_id, arg0, arg1, arg2, 0, 0, 0, 0, &res);
	return res.a0;
}

int is_secondary_core_off(unsigned int cpuid)
{
	return qti_invoke_psci_fn_smc(PSCI_0_2_FN_AFFINITY_INFO, cpuid, 0, 0);
}

void bring_secondary_core_down(unsigned int state)
{
	qti_invoke_psci_fn_smc(PSCI_0_2_FN_CPU_OFF, state, 0, 0);
}

int bring_secondary_core_up(unsigned int cpuid, unsigned int entry,
				unsigned int arg)
{
	int ret;
	ret = qti_invoke_psci_fn_smc(PSCI_0_2_FN_CPU_ON, cpuid, entry, arg);
	if (ret) {
		printf("Enabling CPU%d via psci failed! (ret : %d)\n",
								cpuid, ret);
		return CMD_RET_FAILURE;
	}

	printf("Enabled CPU%d via psci successfully!\n", cpuid);
	return CMD_RET_SUCCESS;
}
#endif

#ifdef CONFIG_IPQ_SPI_NOR
struct spi_flash *ipq_spi_probe(void)
{
	if (flash != NULL)
		return flash;

#if CONFIG_IS_ENABLED(DM_SPI_FLASH)
	struct udevice *spi_dev;

	spi_flash_probe_bus_cs(CONFIG_SF_DEFAULT_BUS,
				CONFIG_SF_DEFAULT_CS,
				&spi_dev);
	flash = dev_get_uclass_priv(spi_dev);
#else
	flash = spi_flash_probe(CONFIG_SF_DEFAULT_BUS,
				CONFIG_SF_DEFAULT_CS,
				CONFIG_SF_DEFAULT_SPEED,
				CONFIG_SF_DEFAULT_MODE);
#endif
	return flash;
}
#endif

uint64_t smem_get_flash_size(uint8_t flash_type)
{
	uint64_t flash_size = 0;

	switch(flash_type) {
	case 0: /* SPI_NOR_FLASH */
#ifdef CONFIG_IPQ_SPI_NOR
		struct spi_flash *flash = ipq_spi_probe();
		if (flash)
			flash_size = flash->size;
#endif
		break;
	case 1: /* NAND_FLASH*/
#ifdef CONFIG_IPQ_NAND
		struct mtd_info *mtd = get_nand_dev_by_index(0);
		if (mtd)
			flash_size = mtd->size;
#endif
		break;
	};

	return flash_size;
}

bool is_smem_part_exceed_flash_size(struct smem_ptn *p, uint64_t psize)
{
	bool ret = false;
	ipq_smem_flash_info_t *sfi = get_ipq_smem_flash_info();
	if (!p)
		goto exit;

	/* NOR and NOR + eMMC */
	if ((sfi->flash_type == SMEM_BOOT_SPI_FLASH) &&
		(part_which_flash(p) == 0)) {
		if (psize > smem_get_flash_size(0))
			ret = true;
	/* NAND and NOR + NAND */
	} else if ((sfi->flash_type == SMEM_BOOT_QSPI_NAND_FLASH) ||
		((sfi->flash_type == SMEM_BOOT_SPI_FLASH) &&
		(part_which_flash(p) == 1))) {
		if (psize > smem_get_flash_size(1))
			ret = true;
	}

exit:
	return ret;
}
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
#ifdef CONFIG_IPQ_NAND
	struct mtd_info *mtd = get_nand_dev_by_index(0);
	if (!mtd)
		return -ENODEV;
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
#ifdef CONFIG_IPQ_NAND
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

#if defined(CONFIG_MMC) || defined(CONFIG_NOR_BLK)
gpt_entry* get_gpt_entry(struct blk_desc *dev_desc)
{
	ipq_smem_flash_info_t *sfi = get_ipq_smem_flash_info();
	int *ncount = NULL, ret = 0;
	gpt_entry **pp_gpt_pte;

	ALLOC_CACHE_ALIGN_BUFFER_PAD(gpt_header, gpt_head, 1, dev_desc->blksz);

	if (dev_desc->uclass_id == UCLASS_MMC) {
		pp_gpt_pte = &sfi->mmc_gpt_pte.gpt_pte;
		ncount = &sfi->mmc_gpt_pte.ncount;
	} else if (dev_desc->uclass_id == UCLASS_SPI) {
		pp_gpt_pte = &sfi->nor_gpt_pte.gpt_pte;
		ncount = &sfi->nor_gpt_pte.ncount;
	} else
		return NULL;

	if(*pp_gpt_pte)
#ifdef UPDATE_GPT_RUNTIME
		free(*pp_gpt_pte);
		*pp_gpt_pte = NULL;
#else
		return *pp_gpt_pte;
#endif

	ret = gpt_repair_headers(dev_desc);
	if (ret == 0) {
		/* This function validates
		 * AND fills in the GPT header and PTE
		 * This gpt header and pte from backup gpt in sucess case.
		 */
		ret = gpt_verify_headers(dev_desc, gpt_head, pp_gpt_pte);
		if(ret == 0 && ncount != NULL)
			*ncount = le32_to_cpu(gpt_head->num_partition_entries);
		else
			*pp_gpt_pte = NULL;
	}

	if(ret || !(*pp_gpt_pte))
		return NULL;
	else
		return *pp_gpt_pte;
}

int ipq_part_get_info_by_name(blkpart_info_t *blkpart)
{
	struct blk_desc *dev;
	int ret;
	enum uclass_id id;
#if defined(CONFIG_NOR_BLK) && defined(CONFIG_IPQ_NAND)
	gpt_entry *gpt_pte, *p;
#endif

	if (blkpart->flash_type == SMEM_BOOT_MMC_FLASH)
		id = UCLASS_MMC;
	else if(blkpart->flash_type == SMEM_BOOT_NORGPT_FLASH)
		id = UCLASS_SPI;
	else {
		printf("unsupported flash type \n");
		return -ENODEV;
	}

	dev = blk_get_devnum_by_uclass_id(id, blkpart->devnum);
	if (!dev) {
		printf("No such device \n");
		return -ENODEV;
	}

#ifdef CONFIG_EFI_PARTITION
	if((dev->part_type == PART_TYPE_UNKNOWN) && (id == UCLASS_MMC))
		dev->part_type = PART_TYPE_EFI;
#endif

	blkpart->desc = dev;

	ret = part_get_info_by_name(dev, blkpart->name, blkpart->info);
	if (ret < 0) {
		printf("Partition not found !!!\n");
		return -ENODEV;
	}

#if defined(CONFIG_NOR_BLK) && defined(CONFIG_IPQ_NAND)
	if (id == UCLASS_SPI) {
		gpt_pte = get_gpt_entry(dev);
		if(!gpt_pte) {
			printf("Failed to get gpt table entry\n");
			return -ENOENT;
		}
		p = &gpt_pte[ret - 1];

		blkpart->isnand = gpt_find_which_flash(p);
	}
#else
	blkpart->isnand = 0;
#endif
	return 0;
}
#endif

void update_nand_training_partition(ipq_smem_flash_info_t *sfi)
{
	uint32_t offset, part_size;
	int ret = -1;
	ipq_part_entry_t *part = &sfi->training;
#if defined(CONFIG_NOR_BLK)
	struct disk_partition disk_info;
	blkpart_info_t  bpart_info;

	if (sfi->flash_type == SMEM_BOOT_NORGPT_FLASH) {
		BLK_PART_GET_INFO_S(bpart_info, "0:TRAINING", &disk_info,
					sfi->flash_type);

		ret = ipq_part_get_info_by_name(&bpart_info);
	} else
#endif
		if (sfi->flash_type != SMEM_BOOT_NO_FLASH)
			ret = getpart_offset_size("0:TRAINING", &offset,
							&part_size);
	if (ret) {
		part->offset = 0xBAD0FF5E;
		part->size = 0xBAD0FF5E;
	} else {
#if defined(CONFIG_NOR_BLK)
		if (sfi->flash_type == SMEM_BOOT_NORGPT_FLASH) {
			part->offset = (u32)disk_info.start * disk_info.blksz;
			part->size = (u32)disk_info.size * disk_info.blksz;
		} else
#endif
		{
			part->offset = offset;
			part->size = part_size;
		}
	}
}


int ipq_get_training_part_info(uint32_t *offset, uint32_t *size)
{
	ipq_smem_flash_info_t *sfi = get_ipq_smem_flash_info();
	ipq_part_entry_t *part = &sfi->training;

	if (part->offset == 0)
		update_nand_training_partition(sfi);

	if (part->offset == 0xBAD0FF5E)
		return 1;
	else {
		*offset = part->offset;
		*size = part->size;
	}

	return 0;
}
#ifdef CONFIG_IPQ_NAND
uint32_t get_nand_block_size(uint8_t dev_id)
{
	uint32_t block_size = 0;
	struct mtd_info *mtd = get_nand_dev_by_index(0);
	if (mtd)
		block_size = mtd->erasesize;
	return block_size;
}
#endif

uint32_t get_part_block_size(struct smem_ptn *p, ipq_smem_flash_info_t *sfi)
{
#ifdef CONFIG_IPQ_NAND
        return (part_which_flash(p) == 1) ?
		get_nand_block_size(0)
		: sfi->flash_block_size;
#else
	return sfi->flash_block_size;
#endif
}
/*
 * get flash block size based on partition name.
 */
static inline uint32_t get_flash_block_size(char *name,
					    ipq_smem_flash_info_t *smem)
{
#ifdef CONFIG_IPQ_NAND
	return (get_which_flash_param(name) == 1) ?
		get_nand_block_size(0)
		: smem->flash_block_size;
#else
	return smem->flash_block_size;
#endif
}

void get_kernel_fs_part_details(int flash_type)
{
	int ret, i;
	uint32_t start;         /* block number */
	uint32_t size;          /* no. of blocks */
	uint32_t bsize;
	ipq_part_entry_t *part;
#if defined(CONFIG_NOR_BLK)
	struct disk_partition disk_info;
	blkpart_info_t  bpart_info;
#endif
	ipq_smem_flash_info_t *smem = get_ipq_smem_flash_info();

	struct { char *name; ipq_part_entry_t *part; } entries[] = {
		{ "0:HLOS", &smem->hlos },
		{ "rootfs", &smem->rootfs },
	};

	for (i = 0; i < ARRAY_SIZE(entries); i++) {
		part = entries[i].part;
#if defined(CONFIG_NOR_BLK)
		if (flash_type == SMEM_BOOT_NORGPT_FLASH) {
			if (!strncmp(entries[i].name, "0:HLOS", 6))
				continue;

			BLK_PART_GET_INFO_S(bpart_info, entries[i].name,
						&disk_info, flash_type);

			ret = ipq_part_get_info_by_name(&bpart_info);
		} else
#endif
		{
			ret = smem_getpart(entries[i].name, &start, &size);
		}

		if (ret) {
			debug("cdp: get part failed for %s\n",
				entries[i].name);
			part->offset = 0xBAD0FF5E;
			part->size = 0xBAD0FF5E;
		} else {
#if defined(CONFIG_NOR_BLK)
			if (flash_type == SMEM_BOOT_NORGPT_FLASH) {
				bsize = disk_info.blksz;
				part->offset = (u32)disk_info.start * bsize;
				part->size = (u32)disk_info.size * bsize;
			} else
#endif
			{
				bsize = get_flash_block_size(entries[i].name,
								smem);
				part->offset = ((loff_t)start) * bsize;
				part->size = ((loff_t)size) * bsize;
			}
		}
	}

	return;
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
	ipq_smem_flash_info_t *sfi = get_ipq_smem_flash_info();
	struct smem_ptable *ptable = get_ipq_part_table_info();
	struct smem_ptn *p;
	uint32_t bsize;
#ifdef CONFIG_IPQ_NAND
	struct mtd_info *mtd = get_nand_dev_by_index(0);
	if (!mtd)
		return -ENODEV;
#endif
	if (!ptable)
		return -ENODEV;

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
#ifdef CONFIG_IPQ_NAND
		*size = (mtd->size / bsize) - p->start;
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
 * smem_getpart_from_offset - retreive partition start and size for given offset
 * belongs to.
 * @part_name: offset for which part start and size needed
 * @start: location where the start offset is to be stored
 * @size: location where the size is to be stored
 *
 * Returns 0 at success or -ENOENT otherwise.
 */
int smem_getpart_from_offset(uint32_t offset, uint32_t *start, uint32_t *size)
{
	unsigned i;
	ipq_smem_flash_info_t *sfi = get_ipq_smem_flash_info();
	struct smem_ptable *ptable = get_ipq_part_table_info();
	struct smem_ptn *p;
	uint32_t bsize;
#ifdef CONFIG_IPQ_NAND
	struct mtd_info *mtd = get_nand_dev_by_index(0);
	if (!mtd)
		return -ENODEV;
#endif

	if (!ptable)
		return -ENODEV;

	for (i = 0; i < ptable->len; i++) {
		p = &ptable->parts[i];
		bsize = get_part_block_size(p, sfi);
		*start = p->start;

		if (p->size == (~0u)) {
		/*
		 * Partition size is 'till end of device', calculate
		 * appropriately
		 */
#ifdef CONFIG_IPQ_NAND
			*size = (mtd->size / bsize) - p->start;
#else
			*size = 0;
			bsize = bsize;
#endif
		} else {
			*size = p->size;
		}
		*start = *start * bsize;
		*size = *size * bsize;
		if (*start <= offset && *start + *size > offset) {
			return 0;
		}
	}

	return -ENOENT;
}

#ifdef CONFIG_CMD_UBI
int init_ubi_part(void)
{
	int ret;
	uint32_t offset = 0;
	uint32_t part_size = 0;
	ipq_smem_flash_info_t *sfi = get_ipq_smem_flash_info();
	struct ubi_device *ubi = ubi_get_device(0);
	char env_strings[64];

	if(ubi == NULL) {
		offset = sfi->rootfs.offset;
		part_size = sfi->rootfs.size;

		if ((part_size == 0xBAD0FF5E) || (offset == 0xBAD0FF5E))
			return -ENOENT;

		snprintf(env_strings, sizeof(env_strings),
			"mtdparts=nand0:0x%x@0x%x(%s)", part_size, offset,
				CFG_UBI_FS_NAME);

		ret = env_set("mtdparts", env_strings);
		if (ret)
			return -EPERM;

		ret = ubi_part(CFG_UBI_FS_NAME, NULL);
		if (ret)
			return -EPERM;
	} else
		ubi_put_device(ubi);

	return 0;
}
#endif

static void ipq_set_part_entry(char *name, ipq_smem_flash_info_t *smem,
				ipq_part_entry_t *part, uint32_t start,
				uint32_t size)
{
	uint32_t bsize = get_flash_block_size(name, smem);
	part->offset = ((loff_t)start) * bsize;
	part->size = ((loff_t)size) * bsize;
}

int get_partition_data(char *part_name, uint32_t offset, uint8_t* buf,
			size_t size, uint32_t fl_type)
{
	ipq_smem_flash_info_t *sfi = get_ipq_smem_flash_info();
	int flash_type, ret = 0, isnand = 0;
#ifdef CONFIG_IPQ_SPI_NOR
	struct spi_flash *flash = NULL;
#endif
	uint32_t start_blk;
	uint32_t blk_cnt;
	ipq_part_entry_t part;
#if defined(CONFIG_MMC) || defined(CONFIG_NOR_BLK)
	blkpart_info_t bpart_info;
	struct disk_partition disk_info;
	uint32_t start_blk_no, end_blk_no, blksz;
#if defined(CONFIG_MMC)
	struct mmc *mmc;
	unsigned char *mmc_blk = NULL;
	int i, rdatacnt = 0, buf_cur_pos = 0;
#endif
#endif
	 memset(&part, 0, sizeof(ipq_part_entry_t));

	if ((sfi->flash_type == SMEM_BOOT_NORGPT_FLASH) &&
		((fl_type == SMEM_BOOT_QSPI_NAND_FLASH) ||
		(fl_type == SMEM_BOOT_NAND_FLASH))) {
			flash_type = SMEM_BOOT_NORGPT_FLASH;
			isnand = 1;
	} else {
		flash_type = fl_type;
	}

	switch(flash_type) {
	case SMEM_BOOT_NAND_FLASH:
	case SMEM_BOOT_QSPI_NAND_FLASH:
	case SMEM_BOOT_SPI_FLASH:
		ret = smem_getpart(part_name, &start_blk, &blk_cnt);
		if (ret < 0) {
			debug("cdp: get part failed for %s\n",
					part_name);
			ret = -ENXIO;
			goto exit;
		} else {
			ipq_set_part_entry(part_name, sfi, &part, start_blk,
						blk_cnt);
			part.offset += offset;
		}
		break;
#if defined(CONFIG_MMC) || defined(CONFIG_NOR_BLK)
#if defined(CONFIG_MMC)
	case SMEM_BOOT_MMC_FLASH:
		mmc = find_mmc_device(0);
		if (!mmc) {
			printf("Failed to find MMC device \n");
			ret = -ENODEV;
			break;
		}
#endif
	case SMEM_BOOT_NORGPT_FLASH:
		BLK_PART_GET_INFO_S(bpart_info, part_name, &disk_info,
					flash_type);
		ret = ipq_part_get_info_by_name(&bpart_info);
		if (ret)
			goto exit;

		blksz = disk_info.blksz;

		if (bpart_info.isnand || isnand) {
			blksz = disk_info.blksz;
			part.offset = disk_info.start * blksz;
			part.offset += offset;
			flash_type = SMEM_BOOT_QSPI_NAND_FLASH;
			break;
		}

		start_blk_no = (uint32_t) disk_info.start + (offset / blksz);
		end_blk_no = (uint32_t) disk_info.start +
				((offset + size) / blksz);

		if ((offset == 0) && (size % blksz == 0)) {
#ifdef CONFIG_BLK
			ret = blk_dread(bpart_info.desc, start_blk_no,
						size / blksz, buf);
			if (ret < 0)
				printf("Blk read failed %d \n", ret);
			break;
#endif
		}

		if (flash_type == SMEM_BOOT_NORGPT_FLASH) {
			part.offset = disk_info.start * blksz;
			part.offset += offset;
			flash_type =  SMEM_BOOT_SPI_FLASH;
			break;
		}

#if defined(CONFIG_MMC)
		mmc_blk = (unsigned char*) malloc_cache_aligned(blksz);
		if (mmc_blk == NULL)
			return -ENOMEM;

		rdatacnt = size;
		for (i = start_blk_no; i <= end_blk_no; i++) {
#ifdef CONFIG_BLK
			ret = blk_dread(bpart_info.desc, i, 1, mmc_blk);
#else
			ret = mmc->block_dev.block_read(&mmc->block_dev,
						i, 1, mmc_blk);
#endif
			if (ret < 0) {
				printf("MMC: %s read failed %d\n", part_name,
					ret);
				break;
			}

			if (i == start_blk_no) {
				if (size <= blksz) {
					memcpy(buf, mmc_blk + (offset % blksz),
						size);
					if (start_blk_no == end_blk_no)
						break;
				} else {
					memcpy(buf, mmc_blk + (offset % blksz),
						blksz - (offset % blksz));
					buf_cur_pos +=
						(blksz - (offset % blksz));
					rdatacnt -= (blksz - (offset % blksz));
				}
			} else if (rdatacnt >= blksz) {
				memcpy(buf + buf_cur_pos, mmc_blk, blksz);
				rdatacnt -= blksz;
				buf_cur_pos += blksz;
			} else
				memcpy(buf + buf_cur_pos, mmc_blk, rdatacnt);
		}

		if (mmc_blk) {
			free(mmc_blk);
			mmc_blk = NULL;
		}
#endif
		break;
#endif
	default:
		printf("Unsupported BOOT flash type\n");
		ret = -ENXIO;
		break;
	}
#ifdef CONFIG_IPQ_SPI_NOR
	if ((flash_type == SMEM_BOOT_SPI_FLASH) ||
		(flash_type == SMEM_BOOT_NORGPT_FLASH)) {
		flash = ipq_spi_probe();
		if (flash == NULL){
			printf("No SPI flash device found\n");
			ret = -ENODEV;
		} else {
			ret = spi_flash_read(flash, part.offset, size, buf);
		}
	}
#endif
#ifdef CONFIG_IPQ_NAND
	if ((flash_type == SMEM_BOOT_NAND_FLASH) ||
		(flash_type == SMEM_BOOT_QSPI_NAND_FLASH)) {
		struct mtd_info *mtd = get_nand_dev_by_index(0);
		if (!mtd) {
			printf("No NAND flash device found\n");
			ret = -ENODEV;
		} else {
			ret = nand_read(mtd, part.offset, &size, buf);
		}
	}
#endif

exit:

#if defined(CONFIG_MMC) && defined(CONFIG_SYS_MMC_ENV_PART)
	if (mmc_blk) {
		free(mmc_blk);
		mmc_blk = NULL;
	}
#endif
	return ret;
}
