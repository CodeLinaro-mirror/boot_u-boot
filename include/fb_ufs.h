/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * UFS fastboot support separated from MMC implementation.
 */

#ifndef _FB_UFS_H_
#define _FB_UFS_H_

struct blk_desc;
struct disk_partition;

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
			       struct disk_partition *part_info,
			       char *response);

/**
 * fastboot_ufs_flash_write() - Write image to UFS for fastboot
 *
 * @cmd: Named partition to write image to
 * @download_buffer: Pointer to image data
 * @download_bytes: Size of image data
 * @response: Pointer to fastboot response buffer
 */
void fastboot_ufs_flash_write(const char *cmd, void *download_buffer,
			      u32 download_bytes, char *response);

/**
 * fastboot_ufs_erase() - Erase UFS partition for fastboot
 *
 * @cmd: Named partition to erase
 * @response: Pointer to fastboot response buffer
 */
void fastboot_ufs_erase(const char *cmd, char *response);

#endif
