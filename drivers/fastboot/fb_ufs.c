// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2014 Broadcom Corporation.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * UFS fastboot support separated from MMC implementation.
 */

#include <config.h>
#include <blk.h>
#include <env.h>
#include <fastboot.h>
#include <fastboot-internal.h>
#include <fb_ufs.h>
#include <image-sparse.h>
#include <log.h>
#include <part.h>
#include <scsi.h>
#include <div64.h>
#include <linux/compat.h>

#define fb_flash_str	"scsi"
#define fb_flash_dev	CONFIG_FASTBOOT_FLASH_UFS_DEV

struct fb_ufs_sparse {
	struct blk_desc	*dev_desc;
};

/**
 * fb_ufs_blk_write() - Write/erase UFS in chunks of FASTBOOT_MAX_BLK_WRITE
 *
 * @block_dev: Pointer to block device
 * @start: First block to write/erase
 * @blkcnt: Count of blocks
 * @buffer: Pointer to data buffer for write or NULL for erase
 */
static lbaint_t fb_ufs_blk_write(struct blk_desc *block_dev, lbaint_t start,
				 lbaint_t blkcnt, const void *buffer)
{
	lbaint_t blk = start;
	lbaint_t blks_written;
	lbaint_t cur_blkcnt;
	lbaint_t blks = 0;
	int i;

	for (i = 0; i < blkcnt; i += FASTBOOT_MAX_BLK_WRITE) {
		cur_blkcnt = min((int)blkcnt - i, FASTBOOT_MAX_BLK_WRITE);
		if (buffer) {
			if (fastboot_progress_callback)
				fastboot_progress_callback("writing");
			blks_written = blk_dwrite(block_dev, blk, cur_blkcnt,
						  buffer + (i * block_dev->blksz));
		} else {
			if (fastboot_progress_callback)
				fastboot_progress_callback("erasing");
			blks_written = blk_derase(block_dev, blk, cur_blkcnt);
		}
		blk += blks_written;
		blks += blks_written;
	}
	return blks;
}

static lbaint_t fb_ufs_sparse_write(struct sparse_storage *info,
				    lbaint_t blk, lbaint_t blkcnt, const void *buffer)
{
	struct fb_ufs_sparse *sparse = info->priv;
	struct blk_desc *dev_desc = sparse->dev_desc;

	return fb_ufs_blk_write(dev_desc, blk, blkcnt, buffer);
}

static lbaint_t fb_ufs_sparse_reserve(struct sparse_storage *info,
				      lbaint_t blk, lbaint_t blkcnt)
{
	return blkcnt;
}

static void write_raw_image(struct blk_desc *dev_desc,
			    struct disk_partition *info, const char *part_name,
			    void *buffer, u32 download_bytes, char *response)
{
	lbaint_t blkcnt;
	lbaint_t blks;

	/* determine number of blocks to write */
	blkcnt = ((download_bytes + (info->blksz - 1)) & ~(info->blksz - 1));
	blkcnt = lldiv(blkcnt, info->blksz);

	if (blkcnt > info->size) {
		pr_err("too large for partition: '%s'\n", part_name);
		fastboot_fail("too large for partition", response);
		return;
	}

	puts("Flashing Raw Image\n");

	blks = fb_ufs_blk_write(dev_desc, info->start, blkcnt, buffer);

	if (blks != blkcnt) {
		pr_err("failed writing to device %d\n", dev_desc->devnum);
		fastboot_fail("failed writing to device", response);
		return;
	}

	printf("........ wrote " LBAFU " bytes to '%s'\n", blkcnt * info->blksz,
	       part_name);
	fastboot_okay(NULL, response);
}

/**
 * fastboot_ufs_get_part_info() - Lookup UFS partition by name
 *
 * @partition_name: Partition name to lookup
 * @dev_desc: Pointer to returned blk_desc pointer
 * @part_info: Pointer to returned struct disk_partition
 * @response: Pointer to fastboot response buffer
 */
int fastboot_ufs_get_part_info(const char *partition_name,
			       struct blk_desc **dev_desc,
			       struct disk_partition *part_info, char *response)
{
	int ret;

	if (!partition_name || !strcmp(partition_name, "")) {
		fastboot_fail("partition name not given", response);
		return -ENOENT;
	}

	ret = scsi_get_blk(partition_name, dev_desc, part_info);
	if (ret < 0) {
		fastboot_fail("partition not found by name", response);
		return ret;
	}

	return 0;
}

static struct blk_desc *fastboot_ufs_get_dev(char *response)
{
	struct blk_desc *ret = blk_get_dev(fb_flash_str, fb_flash_dev);

	if (!ret || ret->type == DEV_TYPE_UNKNOWN) {
		pr_err("invalid %s device\n", fb_flash_str);
		fastboot_fail("invalid ufs device", response);
		return NULL;
	}
	return ret;
}

/**
 * fastboot_ufs_flash_write() - Write image to UFS for fastboot
 *
 * @cmd: Named partition to write image to
 * @download_buffer: Pointer to image data
 * @download_bytes: Size of image data
 * @response: Pointer to fastboot response buffer
 */
void fastboot_ufs_flash_write(const char *cmd, void *download_buffer,
			      u32 download_bytes, char *response)
{
	struct blk_desc *dev_desc;
	struct disk_partition info = {0};

	if (IS_ENABLED(CONFIG_EFI_PARTITION) &&
	    strcmp(cmd, CONFIG_FASTBOOT_GPT_NAME) == 0) {
		dev_desc = fastboot_ufs_get_dev(response);
		if (!dev_desc)
			return;

		printf("%s: updating MBR, Primary and Backup GPT(s)\n",
		       __func__);
		if (is_valid_gpt_buf(dev_desc, download_buffer)) {
			printf("%s: invalid GPT - refusing to write to flash\n",
			       __func__);
			fastboot_fail("invalid GPT partition", response);
			return;
		}
		if (write_mbr_and_gpt_partitions(dev_desc, download_buffer)) {
			printf("%s: writing GPT partitions failed\n", __func__);
			fastboot_fail("writing GPT partitions failed",
				      response);
			return;
		}
		part_init(dev_desc);
		printf("........ success\n");
		fastboot_okay(NULL, response);
		return;
	}

	if (IS_ENABLED(CONFIG_DOS_PARTITION) &&
	    strcmp(cmd, CONFIG_FASTBOOT_MBR_NAME) == 0) {
		dev_desc = fastboot_ufs_get_dev(response);
		if (!dev_desc)
			return;

		printf("%s: updating MBR\n", __func__);
		if (is_valid_dos_buf(download_buffer)) {
			printf("%s: invalid MBR - refusing to write to flash\n",
			       __func__);
			fastboot_fail("invalid MBR partition", response);
			return;
		}
		if (write_mbr_sector(dev_desc, download_buffer)) {
			printf("%s: writing MBR partition failed\n", __func__);
			fastboot_fail("writing MBR partition failed",
				      response);
			return;
		}
		part_init(dev_desc);
		printf("........ success\n");
		fastboot_okay(NULL, response);
		return;
	}

	if (fastboot_ufs_get_part_info(cmd, &dev_desc, &info, response) < 0)
		return;

	if (is_sparse_image(download_buffer)) {
		struct fb_ufs_sparse sparse_priv;
		struct sparse_storage sparse;
		int err;

		sparse_priv.dev_desc = dev_desc;

		sparse.blksz = info.blksz;
		sparse.start = info.start;
		sparse.size = info.size;
		sparse.write = fb_ufs_sparse_write;
		sparse.reserve = fb_ufs_sparse_reserve;
		sparse.mssg = fastboot_fail;

		printf("Flashing sparse image at offset " LBAFU "\n",
		       sparse.start);

		sparse.priv = &sparse_priv;
		err = write_sparse_image(&sparse, cmd, download_buffer,
					 response);
		if (!err)
			fastboot_okay(NULL, response);
	} else {
		write_raw_image(dev_desc, &info, cmd, download_buffer,
				download_bytes, response);
	}
}

/**
 * fastboot_ufs_erase() - Erase UFS partition for fastboot
 *
 * @cmd: Named partition to erase
 * @response: Pointer to fastboot response buffer
 */
void fastboot_ufs_erase(const char *cmd, char *response)
{
	struct blk_desc *dev_desc;
	struct disk_partition info;
	lbaint_t blks, blks_start, blks_size;

	if (fastboot_ufs_get_part_info(cmd, &dev_desc, &info, response) < 0)
		return;

	/* UFS doesn't need erase group alignment like MMC */
	blks_start = info.start;
	blks_size = info.size;

	printf("Erasing blocks " LBAFU " to " LBAFU "\n",
	       blks_start, blks_start + blks_size);

	blks = fb_ufs_blk_write(dev_desc, blks_start, blks_size, NULL);

	if (blks != blks_size) {
		pr_err("failed erasing from device %d\n", dev_desc->devnum);
		fastboot_fail("failed erasing from device", response);
		return;
	}

	printf("........ erased " LBAFU " bytes from '%s'\n",
	       blks_size * info.blksz, cmd);
	fastboot_okay(NULL, response);
}
