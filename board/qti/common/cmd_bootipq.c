// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2015-2017, 2020 The Linux Foundation. All rights reserved.
 *
 * Copyright (c) 2023-2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <common.h>
#include <command.h>
#include <image.h>
#include <nand.h>
#include <errno.h>
#include <part.h>
#include <linux/mtd/ubi.h>
#include <mmc.h>
#include <part_efi.h>
#include <fdtdec.h>
#include <usb.h>
#include <elf.h>
#include <memalign.h>
#include <cpu_func.h>
#include <bootm.h>
#include <mach/ipq_scm.h>
#include <linux/bug.h>
#include <asm/io.h>
#ifdef CONFIG_IPQ_SPI_NOR
#include <spi.h>
#include <spi_flash.h>
#endif
#ifdef CONFIG_MMC
#include <mmc.h>
#endif

#include "ipq_board.h"

#define XMK_STR(x)#x
#define MK_STR(x)XMK_STR(x)

#define KERNEL_SEC_AUTH_SW_ID	0x71
#define ROOTFS_SEC_AUTH_SW_ID	0x67
#define SEC_AUTH_SW_ID          0x17
#define ROOTFS_IMAGE_TYPE       0x13
#define NO_OF_PROGRAM_HDRS      3

#define ELF_HDR_PLUS_PHDR_SIZE	sizeof(Elf32_Ehdr) + \
		(NO_OF_PROGRAM_HDRS * sizeof(Elf32_Phdr))

#ifdef CONFIG_SYS_MAXARGS
#define MAX_BOOT_ARGS_SIZE	CONFIG_SYS_MAXARGS
#elif
#define MAX_BOOT_ARGS_SIZE	64
#endif

#define FIT_BOOTARGS_PROP	"append-bootargs"

#define PRIMARY_PARTITION	1
#define SECONDARY_PARTITION	2

#ifndef IPQ_NAND_BOOTARGS
#define IPQ_NAND_BOOTARGS	"ubi.mtd=rootfs root=mtd:ubi_rootfs "	\
				"rootfstype=squashfs"
#endif

DECLARE_GLOBAL_DATA_PTR;

typedef struct {
	uint32_t kernel_load_addr;
	uint32_t kernel_load_size;
	uint32_t kernel_meta_data_size;
} kernel_img_info_t;

#ifdef CONFIG_IPQ_ELF_AUTH
typedef struct {
	unsigned int img_offset;
	unsigned int img_load_addr;
	unsigned int img_size;
} image_info;

static image_info img_info;
#endif

int set_bootargs(void);
int config_select(void);
int boot_kernel(void);
int image_authentication(void);

typedef struct boot_info_t{
#ifdef CONFIG_IPQ_SPI_NOR
	struct spi_flash *flash;
#endif
	ulong load_address;
	ulong size;
	ulong meta_data_size;
	uint8_t debug;
	const char *config;
} boot_info_t;

static boot_info_t boot_info;

#ifdef CONFIG_IPQ_NAND
extern int ubi_volume_read(char *volume, char *buf, size_t size);
#endif

void update_load_addr(image_info *img_info)
{
	boot_info.load_address = img_info->img_load_addr;
	boot_info.load_address -= img_info->img_offset;
	boot_info.meta_data_size = img_info->img_offset;
}

#ifdef CONFIG_MMC
static struct mmc *__init_mmc_dev(int dev, bool force_init,
				     enum bus_mode speed_mode)
{
	struct mmc *mmc;
	mmc = find_mmc_device(dev);
	if (!mmc) {
		printf("no mmc device at slot %x\n", dev);
		return NULL;
	}

	if (!mmc_getcd(mmc))
		force_init = true;

	if (force_init)
		mmc->has_init = 0;

	if (IS_ENABLED(CONFIG_MMC_SPEED_MODE_SET))
		mmc->user_speed_mode = speed_mode;

	if (mmc_init(mmc))
		return NULL;

#ifdef CONFIG_BLOCK_CACHE
	struct blk_desc *bd = mmc_get_blk_desc(mmc);
	blkcache_invalidate(bd->uclass_id, bd->devnum);
#endif

	return mmc;
}
#endif

unsigned int get_rootfs_active_partition(void)
{
	int i;
	ipq_smem_flash_info_t *sfi = get_ipq_smem_flash_info();

	if (!sfi->ipq_smem_bootconfig_info)
		return 0;

	for (i = 0; i < sfi->ipq_smem_bootconfig_info->numaltpart; i++) {
		if (strncmp("rootfs",
			sfi->ipq_smem_bootconfig_info->per_part_entry[i].name,
			     CONFIG_RAM_PART_NAME_LENGTH) == 0)
			return sfi->ipq_smem_bootconfig_info->		\
						per_part_entry[i].primaryboot;
	}

	return 0; /* alt partition not available */
}

#ifdef CONFIG_MMC
int set_mmc_bootargs(char *boot_args, char *part_name, int buflen,
			bool gpt_flag)
{
	int ret;
	struct disk_partition disk_info;
	blkpart_info_t  bpart_info;

	BLK_PART_GET_INFO_S(bpart_info, part_name, &disk_info,
				SMEM_BOOT_MMC_FLASH);

	if (buflen <= 0 || buflen > MAX_BOOT_ARGS_SIZE)
		return -EINVAL;

	ret = ipq_part_get_info_by_name(&bpart_info);
	if (ret) {
		printf("bootipq: unsupported partition name %s\n",part_name);
		return -EINVAL;
	}
#ifdef CONFIG_PARTITION_UUIDS
	snprintf(boot_args, MAX_BOOT_ARGS_SIZE, "root=PARTUUID=%s%s",
			disk_info.uuid, (gpt_flag == true)?" gpt" : "");
#else
	snprintf(boot_args, MAX_BOOT_ARGS_SIZE, "rootfsname==%s gpt",
			part_name, (gpt_flag == true)?" gpt" : "");
#endif
	if (env_get("fsbootargs") == NULL)
		env_set("fsbootargs", boot_args);

	return 0;
}
#endif
int set_bootargs(void)
{
	char *fit_bootargs =  NULL;
	char *strings = env_get("bootargs");
	int len, ret = CMD_RET_SUCCESS;
	char * cmd_line;
#ifdef CONFIG_MMC
	bool gpt_flag = true;
	char runcmd[MAX_BOOT_ARGS_SIZE];
	int active_part = get_rootfs_active_partition();

	uint8_t	flash_type = gd->board_type & FLASH_TYPE_MASK;

	if(flash_type  == SMEM_BOOT_MMC_FLASH)
		gpt_flag = true;
	else if( flash_type ==  SMEM_BOOT_NORPLUSEMMC)
		gpt_flag = false;

	if (active_part)
		ret  = set_mmc_bootargs(runcmd, "rootfs_1",
				MAX_BOOT_ARGS_SIZE, gpt_flag);
	else
		ret  = set_mmc_bootargs(runcmd, "rootfs",
				MAX_BOOT_ARGS_SIZE, gpt_flag );
#elif CONFIG_IPQ_NAND
	if (env_get("fsbootargs") == NULL)
		ret = env_set("fsbootargs", IPQ_NAND_BOOTARGS);
#endif
	if (ret)
		return ret;

	if(!strings) {
		printf("%s: bootargs not available\n", __func__);
		return -ENXIO;
	}

	cmd_line = malloc(CONFIG_SYS_CBSIZE);
	if(!cmd_line) {
		printf("%s: Memory allocation failed\n", __func__);
		return -ENOMEM;
	}


	memset(cmd_line, 0, CONFIG_SYS_CBSIZE);
	fit_bootargs = (char *)fdt_getprop((void *)boot_info.load_address, 0,
						FIT_BOOTARGS_PROP, &len);
	if ((fit_bootargs != NULL) && len) {
		if ((strlen(strings) + len) > CONFIG_SYS_CBSIZE) {
			ret = CMD_RET_FAILURE;
		} else {
			memcpy(cmd_line, strings, strlen(strings));
			snprintf(cmd_line + strlen(strings), CONFIG_SYS_CBSIZE,
					" %s rootwait", fit_bootargs);
		}
	} else {
		fit_bootargs = env_get("fsbootargs");
		if(!fit_bootargs) {
			printf("%s: bootargs not available\n", __func__);
			return -ENXIO;
		}

		memcpy(cmd_line, strings, strlen(strings));
		len = snprintf(cmd_line + strlen(strings), CONFIG_SYS_CBSIZE,
			" %s rootwait", fit_bootargs);
		if (len >= CONFIG_SYS_CBSIZE)
			ret = CMD_RET_FAILURE;
	}

	if (ret == CMD_RET_FAILURE) {
		printf("Env size exceed ...\n");
	} else {
		env_set("bootargs", NULL);
		env_set("bootargs", cmd_line);
	}

	if(cmd_line)
		free(cmd_line);

	return CMD_RET_SUCCESS;
}

#ifdef CONFIG_IPQ_ELF_AUTH
static int parse_elf_image_phdr(image_info *img_info, unsigned int addr)
{
	Elf32_Ehdr *ehdr; /* Elf header structure pointer */
	Elf32_Phdr *phdr; /* Program header structure pointer */
	int i;

	ehdr = (Elf32_Ehdr *)(uintptr_t)addr;
	phdr = (Elf32_Phdr *)(uintptr_t)(addr + ehdr->e_phoff);

	if (!IS_ELF(*ehdr)) {
		printf("It is not a elf image \n");
		return -EINVAL;
	}

	if (ehdr->e_type != ET_EXEC) {
		printf("Not a valid elf image\n");
		return -EINVAL;
	}

	/* Load each program header */
	for (i = 0; i < NO_OF_PROGRAM_HDRS; ++i) {
		printf("Parsing phdr load addr 0x%x offset 0x%x"
			" size 0x%x type 0x%x\n",
			phdr->p_paddr, phdr->p_offset, phdr->p_filesz,
			phdr->p_type);
		if(phdr->p_type == PT_LOAD) {
			img_info->img_offset = phdr->p_offset;
			img_info->img_load_addr = phdr->p_paddr;
			img_info->img_size =  phdr->p_filesz;
			return 0;
		}
		++phdr;
	}

	return -EINVAL;
}
#endif

#ifdef CONFIG_MMC
static int boot_mmc(void)
{
	struct disk_partition disk_info;
	int ret;
	int curr_device = -1;
	struct mmc *mmc;
	uint32_t blk, cnt, n;
	void *addr;
	int active_part = get_rootfs_active_partition();

	ipq_smem_flash_info_t *sfi = get_ipq_smem_flash_info();
	blkpart_info_t  bpart_info;

	BLK_PART_GET_INFO_S(bpart_info, NULL, &disk_info,
					SMEM_BOOT_MMC_FLASH);
	if (curr_device < 0) {
		if (get_mmc_num() > 0) {
			curr_device = 0;
		} else {
			puts("No MMC device available\n");
			return CMD_RET_FAILURE;
		}
	}

	mmc = __init_mmc_dev(curr_device, false, MMC_MODES_END);
	if (!mmc)
		return CMD_RET_FAILURE;

	if ((sfi->ipq_smem_bootconfig_info != NULL) && (active_part)) {
		bpart_info.name = "0:HLOS_1";
	} else {
		bpart_info.name = "0:HLOS";
	}

	if(boot_info.debug)
		printf("[debug]Reading %s\n", bpart_info.name);

	ret = ipq_part_get_info_by_name(&bpart_info);
	if (ret == 0) {
		if((gd->board_type & SECURE_BOARD) &&
				!(gd->board_type & ATF_ENABLED)) {
#ifdef CONFIG_IPQ_ELF_AUTH
			addr = (void *)boot_info.load_address;
			blk = (uint32_t) disk_info.start;
			cnt = (uintptr_t)ELF_HDR_PLUS_PHDR_SIZE;

			printf("\nMMC read: dev # %d, block # %d, "
				"count %d ... ", curr_device, blk, cnt);

			n = blk_dread(mmc_get_blk_desc(mmc), blk, cnt, addr);

			printf("%d blocks read: %s\n", n,
				(n == cnt) ? "OK" : "ERROR");

			if (n != cnt)
				return CMD_RET_FAILURE;

			if (parse_elf_image_phdr(&img_info,
				boot_info.load_address))
				return CMD_RET_FAILURE;

			update_load_addr(&img_info);
#endif
		}

		boot_info.size = disk_info.size;
		boot_info.size *= disk_info.blksz;

		addr = (void *)boot_info.load_address;
		blk = (uint32_t) disk_info.start;
		cnt = (uint32_t) disk_info.size;

		printf("\nMMC read: dev # %d, block # %d, count %d ... ",
			curr_device, blk, cnt);

		n = blk_dread(mmc_get_blk_desc(mmc), blk, cnt, addr);
		printf("%d blocks read: %s\n", n, (n == cnt) ? "OK" : "ERROR");

		if (n != cnt)
			return CMD_RET_FAILURE;

		if(boot_info.debug)
			printf("[debug]loaded kernel @ 0x%lx\n",
					boot_info.load_address);

		return CMD_RET_SUCCESS;
	}
	return CMD_RET_FAILURE;
}
#endif

#ifdef CONFIG_IPQ_NAND
static int boot_nand(void)
{
	int ret;
	ipq_smem_flash_info_t *sfi = get_ipq_smem_flash_info();

	if (sfi->rootfs.offset == 0xBAD0FF5E) {
		printf(" bad offset of hlos");
		return CMD_RET_FAILURE;
	}
	/*
	 * init ubi
	 */
	ret = init_ubi_part();
	if (ret) {
		printf(" Ubi error %d\n", ret);
		return CMD_RET_FAILURE;
	}

#ifdef CONFIG_IPQ_ELF_AUTH
	if((gd->board_type & SECURE_BOARD) &&
				!(gd->board_type & ATF_ENABLED)) {
		ret = ubi_volume_read("kernel", (char *)boot_info.load_address,
					(uintptr_t)ELF_HDR_PLUS_PHDR_SIZE);
		if(ret)
			return CMD_RET_FAILURE;

		if (parse_elf_image_phdr(&img_info, boot_info.load_address))
			return CMD_RET_FAILURE;

		update_load_addr(&img_info);
	}
#endif
	boot_info.size = ubi_get_volume_size("kernel");

	if(boot_info.size < 0)
		BUG();

	ret = ubi_volume_read("kernel", (char *)boot_info.load_address, 0);
	if(ret)
		return CMD_RET_FAILURE;

	if(boot_info.debug)
		printf("[debug]loaded kernel @ 0x%lx\n",
				boot_info.load_address);

	return CMD_RET_SUCCESS;
}
#endif

#ifdef CONFIG_IPQ_SPI_NOR
static int boot_nor(void)
{
	int ret;
	ipq_smem_flash_info_t *sfi = get_ipq_smem_flash_info();

	if (sfi->hlos.offset == 0xBAD0FF5E) {
		printf(" bad offset of hlos");
		return CMD_RET_FAILURE;
	}

	boot_info.flash = ipq_spi_probe();
	if(!boot_info.flash) {
		printf("Failed to probe spi flash\n");
		return CMD_RET_FAILURE;
	}

#ifdef CONFIG_IPQ_ELF_AUTH
	if((gd->board_type & SECURE_BOARD) &&
				!(gd->board_type & ATF_ENABLED)) {
		ret = spi_flash_read(boot_info.flash, sfi->hlos.offset,
				ELF_HDR_PLUS_PHDR_SIZE,
				(void *)boot_info.load_address);
		if(ret)
			return CMD_RET_FAILURE;

		if (parse_elf_image_phdr(&img_info, boot_info.load_address))
			return CMD_RET_FAILURE;

		update_load_addr(&img_info);
	}
#endif
	boot_info.size = sfi->hlos.size;

	ret = spi_flash_read(boot_info.flash, sfi->hlos.offset,
			sfi->hlos.size,
			(void*)boot_info.load_address);

	if(ret)
		return CMD_RET_FAILURE;

	if(boot_info.debug)
		printf("[debug]loaded kernel @ 0x%lx\n",
				boot_info.load_address);
	return CMD_RET_SUCCESS;
}
#endif

int config_select(void)
{
	/* Selecting a config name from the list of available
	 * config names by passing them to the fit_conf_get_node()
	 * function which is used to get the node_offset with the
	 * config name passed. Based on the return value index based
	 * or board name based config is used.
	 */

	int len, i, ret;
	const char *config = env_get("config_name");
	ulong request;
	if(boot_info.debug)
		printf("[debug]Get Config\n");


	if(!(gd->board_type & SECURE_BOARD))
	{
		request = boot_info.load_address;
		ret = genimg_get_format((void *)request);
		if ((ret != IMAGE_FORMAT_LEGACY) && (ret != IMAGE_FORMAT_FIT))
		{
#ifdef CONFIG_IPQ_ELF_AUTH
			if (!parse_elf_image_phdr(&img_info, request)) {
				request += img_info.img_offset;
				boot_info.load_address = request;
			}
#endif
		} else
			goto get_img_config;
	} else if (gd->board_type & SECURE_BOARD) {
#ifndef CONFIG_IPQ_ELF_AUTH
		request = boot_info.load_address + sizeof(mbn_header_t);
#else
		request = img_info.img_load_addr;
#endif
	}

	ret = genimg_get_format((void *)request);

get_img_config:
	if (ret == IMAGE_FORMAT_LEGACY) {

		if(boot_info.debug)
			printf("[debug]Image type - LEGACY\n");

		config = NULL;
		goto exit;
	} else if (ret == IMAGE_FORMAT_FIT) {

		if(boot_info.debug)
			printf("[debug]Image type - FIT\n");

		int noff = fit_image_get_node((void*)request,
							"kernel-1");

		if (noff < 0) {
			noff = fit_image_get_node((void*)request,
					"kernel@1");
			if (noff < 0)
				return CMD_RET_FAILURE;
		}

		if (!fit_image_check_arch((void*)request, noff,
					IH_ARCH_DEFAULT)) {

			printf("Cross Arch Kernel jump is not supported!!!\n");
			printf("Please use %d-bit kernel image.\n",
				((IH_ARCH_DEFAULT == IH_ARCH_ARM64)?64:32));

			return CMD_RET_FAILURE;

		}
	} else {
		printf("Unknown Image format\n");
		return CMD_RET_FAILURE;
	}

	if (config) {
		printf("Manual device tree config selected!\n");
		if (fit_conf_get_node((void *)request, config) >= 0) {

			goto exit;
		}

	} else {
		for (i = 0;
			(config = fdt_stringlist_get(gd->fdt_blob, 0,
					"config_name", i,&len)); ++i) {
			if (config == NULL)
				break;
			if (fit_conf_get_node((void *)request, config) >= 0) {

				goto exit;
			}
		}
	}

	printf("Config not available\n");
	return -1;
exit:
	boot_info.config = config;
	return 0;
}

static int copy_rootfs(uint32_t request, uint32_t size)
{
	int ret = 0;
#ifdef CONFIG_MMC
	int curr_device = -1;
	struct mmc *mmc;
	uint32_t blk, cnt, n;
	void *addr;
	struct disk_partition disk_info;
	unsigned int active_part = get_rootfs_active_partition();
	blkpart_info_t  bpart_info;

	BLK_PART_GET_INFO_S(bpart_info, NULL, &disk_info, SMEM_BOOT_MMC_FLASH);
#endif
	uint8_t	flash_type = gd->board_type & FLASH_TYPE_MASK;
#ifdef CONFIG_IPQ_SPI_NOR
	ipq_smem_flash_info_t *sfi = get_ipq_smem_flash_info();
#endif

	if (SMEM_BOOT_NORPLUSNAND == flash_type ||
		SMEM_BOOT_QSPI_NAND_FLASH == flash_type) {
#ifdef CONFIG_IPQ_NAND
		ret = ubi_volume_read("ubi_rootfs", (char *)(uintptr_t)request,
					0);
#endif
#ifdef CONFIG_MMC
	} else if (flash_type == SMEM_BOOT_MMC_FLASH ||
			(flash_type == SMEM_BOOT_NORPLUSEMMC)) {

		if (curr_device < 0) {
			if (get_mmc_num() > 0) {
				curr_device = 0;
			} else {
				puts("No MMC device available\n");
				return CMD_RET_FAILURE;
			}
		}

		mmc = __init_mmc_dev(curr_device, false, MMC_MODES_END);
		if (!mmc)
			return CMD_RET_FAILURE;

		if (sfi->ipq_smem_bootconfig_info != NULL) {
			if (active_part) {
				bpart_info.name = "rootfs_1";
			} else {
				bpart_info.name = "rootfs";
						}
		} else {
			bpart_info.name = "rootfs";
		}

		if(boot_info.debug)
			printf("[debug]Reading %s\n", bpart_info.name);

		ret = ipq_part_get_info_by_name(&bpart_info);
		if (ret == 0) {
			addr = (void *)(uintptr_t)request;
			blk = (uint32_t) disk_info.start;
			cnt = (uintptr_t) (size / disk_info.blksz) + 1;

			printf("\nMMC read: dev# %d, block# %d, count %d ... ",
				curr_device, blk, cnt);

			n = blk_dread(mmc_get_blk_desc(mmc), blk, cnt, addr);
			printf("%d blocks read: %s\n", n,
					(n == cnt) ? "OK" : "ERROR");
			if (n != cnt)
				return CMD_RET_FAILURE;
		}
#endif
	} else {
#ifdef CONFIG_IPQ_SPI_NOR
		ret = spi_flash_read(boot_info.flash, sfi->rootfs.offset,
					sfi->rootfs.size,
					(void *) (uintptr_t)request);
#endif
	}

	if (ret) {
		printf("rootfs read failed \n");
		return CMD_RET_FAILURE;
	}

	return 0;
}

#ifndef CONFIG_IPQ_ELF_AUTH
static int authenticate_rootfs(uintptr_t kernel_addr,
					kernel_img_info_t kernel_img_info)
{
	unsigned int kernel_imgsize;
	unsigned int request;
	int ret;
	mbn_header_t *mbn_ptr;
	auth_cmd_buf rootfs_img_info;
	scm_param param;

	request = CONFIG_ROOTFS_LOAD_ADDR;
	rootfs_img_info.addr = CONFIG_ROOTFS_LOAD_ADDR;
	rootfs_img_info.type = SEC_AUTH_SW_ID;
	request += sizeof(mbn_header_t);/* space for mbn header */

	/* get , kernel size = header + kernel + certificate */
	mbn_ptr = (mbn_header_t *) (uintptr_t)kernel_addr;
	kernel_imgsize = mbn_ptr->image_size + sizeof(mbn_header_t);

	/* get rootfs MBN header and validate it */
	mbn_ptr = (mbn_header_t *) (uintptr_t)((uint32_t) (uintptr_t)mbn_ptr + \
		       						kernel_imgsize);
	if (mbn_ptr->image_type != ROOTFS_IMAGE_TYPE &&
			(mbn_ptr->code_size + mbn_ptr->signature_size +
			 mbn_ptr->cert_chain_size != mbn_ptr->image_size))
		return CMD_RET_FAILURE;

	/* pack, MBN header + rootfs + certificate */
	/* copy rootfs from the boot device */
	copy_rootfs(request, mbn_ptr->code_size);

	/* copy rootfs MBN header */
	memcpy((void *)CONFIG_ROOTFS_LOAD_ADDR,
			(void *) (uintptr_t)kernel_addr + kernel_imgsize,
			sizeof(mbn_header_t));

	/* copy rootfs certificate */
	memcpy((void *) (uintptr_t)request + mbn_ptr->code_size,
		(void *) (uintptr_t)kernel_addr + kernel_imgsize +	\
							sizeof(mbn_header_t),
		mbn_ptr->signature_size + mbn_ptr->cert_chain_size);

	/* copy rootfs size */
	rootfs_img_info.size = sizeof(mbn_header_t) + mbn_ptr->image_size;

	do {
		ret = -ENOTSUPP;
		IPQ_SCM_SECURE_AUTHENTICATE(param, rootfs_img_info.type,
						rootfs_img_info.size,
						rootfs_img_info.addr, 0, 0);
		param.get_ret = true;
		ret = ipq_scm_call(&param);
	} while (0);

	memset((void *) (uintptr_t)kernel_img_info.kernel_load_addr,  0,
						sizeof(mbn_header_t));

	memset(mbn_ptr,  0,
		(sizeof(mbn_header_t) + mbn_ptr->signature_size +
			mbn_ptr->cert_chain_size));

	if (ret == -ENOTSUPP) {
		printf("Unsupported SCM call\n");
		return CMD_RET_FAILURE;
	}

	return ret? CMD_RET_FAILURE : CMD_RET_SUCCESS;
}
#else

static int authenticate_rootfs_elf(uint32_t rootfs_hdr)
{
	int ret = -1;
	uint32_t request;
	image_info img_info;
	auth_cmd_buf rootfs_img_info;
	scm_param param;
	struct image_region root_data = {0, 0};
	char hash_buff[SHA384_SUM_LEN] = {0};

	if (parse_elf_image_phdr(&img_info, rootfs_hdr))
		return CMD_RET_FAILURE;

	request = img_info.img_load_addr - img_info.img_offset;
	rootfs_img_info.addr = request;
	rootfs_img_info.type = ROOTFS_SEC_AUTH_SW_ID;

	memcpy((void *) (uintptr_t)request, (void *) (uintptr_t)rootfs_hdr,
			img_info.img_offset);

	request += img_info.img_offset;

	/* copy rootfs from the boot device */
	copy_rootfs(request, img_info.img_size);
	root_data.data  = (void *)(uintptr_t)img_info.img_load_addr;
	root_data.size  = img_info.img_size;

	hash_calculate("sha384", &root_data, 1, hash_buff);

#if IS_ENABLED(CONFIG_SCM_V1)
	rootfs_img_info.size = img_info.img_offset + img_info.img_size;
#elif IS_ENABLED(CONFIG_SCM_V2)
	rootfs_img_info.size = img_info.img_offset - 1;
#endif

	do {
		ret = -ENOTSUPP;
		IPQ_SCM_SECURE_AUTHENTICATE(param, rootfs_img_info.type,
						rootfs_img_info.size,
						rootfs_img_info.addr, 0, 0);
		param.get_ret = true;

		ret = ipq_scm_call(&param);

		if(ret || (param.res.result[0] && !ret)) {
			printf("Rootfs Authentication is failed\n");
			ret = CMD_RET_FAILURE;
			goto exit;
		}

	} while (0);

	if (ret == -ENOTSUPP) {
		printf("Unsupported SCM call\n");
		ret = CMD_RET_FAILURE;
		goto exit;
	}

#if IS_ENABLED(CONFIG_SCM_V2)
	do {
		ret = -ENOTSUPP;
		IPQ_SCM_VERIFY_HASH(param, rootfs_img_info.type,
						rootfs_img_info.addr,
						rootfs_img_info.addr,
						(uintptr_t) hash_buff,
						SHA384_SUM_LEN);
		ret = ipq_scm_call(&param);

		if(ret) {
			printf("Rootfs integrity check filed\n");
			ret = CMD_RET_FAILURE;
		}

	} while (0);

	if (ret == -ENOTSUPP) {
		printf("Unsupported SCM call\n");
		ret =  CMD_RET_FAILURE;
	}
#endif
exit:
	memset((void *) (uintptr_t)rootfs_hdr, 0, img_info.img_offset);

	return ret == CMD_RET_FAILURE? ret : CMD_RET_SUCCESS;
}
#endif

static int check_rootfs_authentication(void)
{
#ifdef ROOTFS_AUTH_FUSE
	return (readl(ROOTFS_AUTH_FUSE) & 0x20);
#else
	return 0;
#endif
}

int image_authentication(void)
{
	int ret;
	scm_param param;
	kernel_img_info_t kernel_img_info = {0, 0, 0};
#ifdef CONFIG_VERSION_ROLLBACK_PARTITION_INFO
	int active_part = get_rootfs_active_partition();
	active_part = active_part ? SECONDARY_PARTITION : PRIMARY_PARTITION;
#endif
	int secure_boot = (gd->board_type & SECURE_BOARD) &&
				!(gd->board_type & ATF_ENABLED);
	if(!secure_boot)
		return CMD_RET_SUCCESS;

	if(boot_info.debug)
		printf("[debug]Authenticating Image\n");

#ifdef CONFIG_VERSION_ROLLBACK_PARTITION_INFO
	do {
		ret = -ENOTSUPP;
		IPQ_SCM_SET_ACTIVE_PARTITION(param, active_part);

		ret = ipq_scm_call(&param);

		if (ret) {
			printf(" Partition info authentication failed \n");
			goto clear_mem;
		}
	} while (0);

	if (ret == -ENOTSUPP) {
		printf("Unsupported SCM call\n");
		goto clear_mem;
	}

#endif
	kernel_img_info.kernel_load_addr = boot_info.load_address;
	kernel_img_info.kernel_load_size = boot_info.size;
	kernel_img_info.kernel_meta_data_size = boot_info.meta_data_size;

#ifndef CONFIG_IPQ_ELF_AUTH
	mbn_header_t * mbn_ptr = (mbn_header_t *) boot_info.load_address;
	boot_info.load_address += sizeof(mbn_header_t);
#else
	boot_info.load_address = img_info.img_load_addr;
#endif

	do {
		ret = -ENOTSUPP;
		IPQ_SCM_AUTHENTICATE_KERNEL(param,
					kernel_img_info.kernel_load_addr,
					kernel_img_info.kernel_meta_data_size,
					KERNEL_SEC_AUTH_SW_ID, 0, 0);

		ret = ipq_scm_call(&param);
	} while (0);

#ifdef CONFIG_VERSION_ROLLBACK_PARTITION_INFO
clear_mem:
#endif
#ifndef CONFIG_IPQ_ELF_AUTH
	memset((void *) (uintptr_t)mbn_ptr->signature_ptr, 0,
		(mbn_ptr->signature_size + mbn_ptr->cert_chain_size));
#else
	memset((void *) (uintptr_t)kernel_img_info.kernel_load_addr,  0,
		img_info.img_offset);
#endif

	if (ret == -ENOTSUPP) {
		printf("Unsupported SCM call\n");
		return CMD_RET_FAILURE;
	}

	if (ret) {
		printf("Kernel image authentication failed \n");
		BUG();
	}

	gd->board_type |= KERNEL_AUTH_SUCCESS;

	if(boot_info.debug)
		printf("[debug]Kernel authenticated successfully\n");

	if (check_rootfs_authentication()) {
#ifdef CONFIG_IPQ_ELF_AUTH
		if (authenticate_rootfs_elf(img_info.img_load_addr +
			img_info.img_size) != CMD_RET_SUCCESS) {
			printf("Rootfs elf image authentication failed\n");
			BUG();
		}
#else
		/* Rootfs's header and certificate at end of kernel image,
		 * copy from there and pack with rootfs image and
		 * authenticate rootfs */
		if (authenticate_rootfs(boot_info.load_address, kernel_img_info)
			!= CMD_RET_SUCCESS) {
			printf("Rootfs image authentication failed\n");
			BUG();
		}

#endif
		gd->board_type |= ROOTFS_AUTH_SUCCESS;

		if(boot_info.debug)
			printf("[debug]Rootfs authenticated successfully\n");
	}

	return 0;
}

int read_kernel(void)
{
	int ret;
	int flash_type = gd->board_type & FLASH_TYPE_MASK;

	/*
	 * set fdt_high parameter so that u-boot will not load
	 * dtb above FDT_HIGH region.
	 */

	ret = env_set("fdt_high", MK_STR(FDT_HIGH));
	if (ret)
		return CMD_RET_FAILURE;

	if(boot_info.debug)
		printf("[debug]Loading Kernel\n");

	switch(flash_type) {
#ifdef CONFIG_MMC
	case SMEM_BOOT_MMC_FLASH:
	case SMEM_BOOT_NORPLUSEMMC:
		ret = boot_mmc();
		break;
#endif
#ifdef CONFIG_IPQ_NAND
	case SMEM_BOOT_QSPI_NAND_FLASH:
	case SMEM_BOOT_NORPLUSNAND:
		ret = boot_nand();
		break;
#endif
#ifdef CONFIG_IPQ_SPI_NOR
	case SMEM_BOOT_SPI_FLASH:
	case SMEM_BOOT_NORGPT_FLASH:
		ret = boot_nor();
		break;
#endif
	default:
		printf("Unsupported BOOT flash type\n");
		return -1;
	}

	return ret;
}

int boot_kernel(void)
{
	char boot_cmd[MAX_BOOT_ARGS_SIZE];

	if(boot_info.config)
	{
		snprintf(boot_cmd, sizeof(boot_cmd),
			"bootm 0x%lx#%s\n",
			 boot_info.load_address, boot_info.config);
	} else {
		snprintf(boot_cmd, sizeof(boot_cmd),
			"bootm 0x%lx\n", boot_info.load_address);
	}

	return run_command(boot_cmd, 0);
}

typedef int (*state_fuc_t)(void);
static const state_fuc_t state_sequence[] = {
	read_kernel,
#ifdef CONFIG_IPQ_SECURE
	image_authentication,
#endif
	config_select,
	set_bootargs,
	boot_kernel,
	NULL
};

static int do_bootipq(struct cmd_tbl *cmdtp, int flag, int argc,
			char *const argv[])
{
	int ret, state;
	const state_fuc_t *state_sequence_ptr = state_sequence;

	if (argc == 2 && strncmp(argv[1], "debug", 5) == 0)
		boot_info.debug = 1;

#ifdef CFG_CUSTOM_LOAD_ADDR
	boot_info.load_address = CFG_CUSTOM_LOAD_ADDR;
#else
	boot_info.load_address = CONFIG_SYS_LOAD_ADDR;
#endif

	for(state_sequence_ptr = state_sequence, state = 1;
					*state_sequence_ptr;
					++state_sequence_ptr, state++)
	{
		ret = (*state_sequence_ptr)();
		if(ret)
		{
			printf("Failed at state %d\n", state);
			return CMD_RET_FAILURE;
		}
	}

	return CMD_RET_SUCCESS;
}

U_BOOT_CMD(
	bootipq, 2, 0, do_bootipq,
	"bootipq from flash device",
	"bootipq [debug] - Load image(s) and boots the kernel\n"
	);
