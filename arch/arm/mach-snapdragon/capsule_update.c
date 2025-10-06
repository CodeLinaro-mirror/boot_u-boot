// SPDX-License-Identifier: GPL-2.0+
/*
 * Capsule update support for Qualcomm boards.
 *
 * Copyright (c) 2024 Linaro Ltd.
 * Author: Casey Connolly <casey.connolly@linaro.org>
 */

#define pr_fmt(fmt) "QCOM-FMP: " fmt

#include <dm/device.h>
#include <dm/uclass.h>
#include <efi.h>
#include <efi_loader.h>
#include <malloc.h>
#include <scsi.h>
#include <part.h>
#include <linux/err.h>
#include <command.h>

#include "qcom-priv.h"

/*
 * Capsule update support with conditional FIT vs RAW implementation:
 * - FIT capsules: Comprehensive partition discovery with dynamic fw_images
 * - RAW capsules: Existing single-partition approach with static fw_images
 */

#ifdef CONFIG_EFI_CAPSULE_FIRMWARE_FIT
#define MAX_DFU_STRING_SIZE 2048
#define MAX_PARTITION_GROUPS 64

struct qcom_partition_info {
	char name[32];              /* "uefi_a", "boot_b", etc. */
	char base_name[32];         /* "uefi", "boot", etc. */
	char slot_suffix[4];        /* "_a", "_b", or "" */
	int lun;                    /* SCSI LUN number */
	int partition_num;          /* Partition number within LUN */
	bool is_active;             /* From GPT vendor attributes */
	bool is_bootable;           /* From GPT vendor attributes */
};

struct partition_group {
	char base_name[32];
	struct qcom_partition_info *a_slot;
	struct qcom_partition_info *b_slot;
	struct qcom_partition_info *no_slot;
};

struct lun_group {
	int lun_number;
	struct qcom_partition_info *partitions[64];  /* Max partitions per LUN */
	int partition_count;
};
#endif /* CONFIG_EFI_CAPSULE_FIRMWARE_FIT */

struct efi_fw_image fw_images[] = {
	{
		.image_index = 1,
	},
};

struct efi_capsule_update_info update_info = {
	/* Filled in by qcom_configure_capsule_updates() */
	.dfu_string = NULL,
	.num_images = ARRAY_SIZE(fw_images),
	.images = fw_images,
};

#ifdef CONFIG_EFI_CAPSULE_FIRMWARE_RAW
enum target_part_type {
	TARGET_PART_UEFI = 1,
	TARGET_PART_XBL,
	TARGET_PART_BOOT,
};

enum ab_slot {
	SLOT_NONE,
	SLOT_A,
	SLOT_B,
};
#endif /* CONFIG_EFI_CAPSULE_FIRMWARE_RAW */

/* LSB first */
struct part_slot_status {
	u16: 2;
	u16 active : 1;
	u16: 3;
	u16 successful : 1;
	u16 unbootable : 1;
	u16 tries_remaining : 4;
};

void qcom_cleanup_capsule_updates(void)
{
	/* Clean up dynamically allocated fw_images */
	if (update_info.images && update_info.images != fw_images) {
		for (int i = 0; i < update_info.num_images; i++)
			free((void *)update_info.images[i].fw_name);
		free((void *)update_info.images);

		/* Reset to defaults */
		update_info.images = fw_images;
		update_info.num_images = ARRAY_SIZE(fw_images);
		update_info.dfu_string = NULL;
	}
}

#ifdef CONFIG_EFI_CAPSULE_FIRMWARE_RAW
/*
 * RAW Capsule Support - Original Implementation
 */

static enum ab_slot get_part_slot(const char *partname)
{
	int len = strlen(partname);

	if (partname[len - 2] != '_')
		return SLOT_NONE;
	if (partname[len - 1] == 'a')
		return SLOT_A;
	if (partname[len - 1] == 'b')
		return SLOT_B;

	return SLOT_NONE;
}

/*
 * Determine which partition U-Boot is flashed to based on the boot source (ABL/XBL),
 * the slot status, and prioritizing the uefi partition over xbl if found.
 */
static int find_target_partition(int *devnum, enum uclass_id *uclass,
				 enum target_part_type *target_part_type)
{
	int ret;
	int partnum, uefi_partnum = -1, xbl_partnum = -1;
	struct disk_partition info;
	struct part_slot_status *slot_status;
	struct udevice *dev = NULL;
	struct blk_desc *desc = NULL, *xbl_desc = NULL;
	uchar ptn_name[32] = { 0 };
	bool have_ufs = false;

	/*
	 * Check to see if we have UFS storage, if so U-Boot MUST be on it and we can skip
	 * all non-UFS block devices
	 */
	uclass_foreach_dev_probe(UCLASS_UFS, dev) {
		have_ufs = true;
		break;
	}

	uclass_foreach_dev_probe(UCLASS_BLK, dev) {
		if (device_get_uclass_id(dev) != UCLASS_BLK)
			continue;

		/* If we have a UFS then don't look at any other block devices */
		if (have_ufs) {
			if (device_get_uclass_id(dev->parent->parent) != UCLASS_UFS)
				continue;
		/*
		 * If we don't have UFS, then U-Boot must be on the eMMC which is always the first
		 * MMC device.
		 */
		} else if (dev->parent->seq_ > 0) {
			continue;
		}

		desc = dev_get_uclass_plat(dev);
		if (!desc || desc->part_type == PART_TYPE_UNKNOWN)
			continue;
		for (partnum = 1;; partnum++) {
			ret = part_get_info(desc, partnum, &info);
			if (ret)
				break;

			slot_status = (struct part_slot_status *)&info.type_flags;

			/*
			 * Qualcomm Linux devices have a "uefi" partition, it's A/B but the
			 * flags might not be set so we assume the A partition unless the B
			 * partition is active.
			 */
			if (!strncmp(info.name, "uefi_", strlen("uefi_"))) {
				/*
				 * If U-Boot was chainloaded somehow we can't be flashed to
				 * the uefi partition
				 */
				if (qcom_boot_source != QCOM_BOOT_SOURCE_XBL)
					continue;

				*target_part_type = TARGET_PART_UEFI;
				/*
				 * Found an active UEFI partition, this is where U-Boot is
				 * flashed.
				 */
				if (slot_status->active)
					goto found;

				/* Prefer A slot if it's not marked active */
				if (get_part_slot(info.name) == SLOT_A) {
					/*
					 * If we found the A slot after the B slot (both
					 * inactive) then we assume U-Boot is on the A slot.
					 */
					if (uefi_partnum >= 0)
						goto found;

					/* Didn't find the B slot yet */
					uefi_partnum = partnum;
					strlcpy(ptn_name, info.name, 32);
				} else {
					/*
					 * Found inactive B slot after inactive A slot, return
					 * the A slot
					 */
					if (uefi_partnum >= 0) {
						partnum = uefi_partnum;
						goto found;
					}

					/*
					 * Didn't find the A slot yet. Record that we found the
					 * B slot
					 */
					uefi_partnum = partnum;
					strlcpy(ptn_name, info.name, 32);
				}
				/* xbl and aboot are effectively the same */
			} else if ((!strncmp(info.name, "xbl", strlen("xbl")) &&
				    strlen(info.name) == 5) ||
				    !strncmp(info.name, "aboot", strlen("aboot"))) {
				/*
				 * If U-Boot was booted via ABL, we can't be flashed to the
				 * XBL partition
				 */
				if (qcom_boot_source != QCOM_BOOT_SOURCE_XBL)
					continue;

				/*
				 * ignore xbl partition if we have uefi partitions, U-Boot will
				 * always be on the UEFI partition in this case.
				 */
				if (*target_part_type == TARGET_PART_UEFI)
					continue;

				/* Either non-A/B or find the active XBL partition */
				if (slot_status->active || !get_part_slot(info.name)) {
					/*
					 * No quick return since we might find a uefi partition
					 * later
					 */
					xbl_partnum = partnum;
					*target_part_type = TARGET_PART_XBL;
					xbl_desc = desc;
					strlcpy(ptn_name, info.name, 32);
				}

				/*
				 * No fast return since we might also have a uefi partition which
				 * will take priority.
				 */
			} else if (!strncmp(info.name, "boot", strlen("boot"))) {
				/* We can only be flashed to boot if we were chainloaded */
				if (qcom_boot_source != QCOM_BOOT_SOURCE_ANDROID)
					continue;

				/*
				 * Either non-A/B or find the active partition. We can return
				 * immediately here since we've narrowed it down to a single option
				 */
				if (slot_status->active || !get_part_slot(info.name)) {
					*target_part_type = TARGET_PART_BOOT;
					goto found;
				}
			}
		}
	}

	/*
	 * Now we've exhausted all options, if we didn't find a uefi partition
	 * then we are indeed flashed to the xbl partition.
	 */
	if (*target_part_type == TARGET_PART_XBL) {
		partnum = xbl_partnum;
		desc = xbl_desc;
		goto found;
	}

	/* Found no candidate partitions */
	return -ENOENT;

found:
	if (desc) {
		*devnum = desc->devnum;
		*uclass = desc->uclass_id;
	}

	/* info won't match for XBL hence the copy. */
	log_info("Capsule update target: %s (disk %d:%d)\n",
		 *target_part_type == TARGET_PART_BOOT ? info.name : ptn_name,
		 *devnum, partnum);
	return partnum;
}

static void configure_raw_capsule_updates(void)
{
	int ret = 0, partnum = -1, devnum;
	static char dfu_string[32] = { 0 };
	enum target_part_type target_part_type = 0;
	enum uclass_id dev_uclass;

	/* Clean up any existing FIT configuration first */
	qcom_cleanup_capsule_updates();

	if (IS_ENABLED(CONFIG_SCSI)) {
		/* Scan for SCSI devices */
		ret = scsi_scan(false);
		if (ret) {
			debug("Failed to scan SCSI devices: %d\n", ret);
			return;
		}
	}

	partnum = find_target_partition(&devnum, &dev_uclass, &target_part_type);
	if (partnum < 0) {
		log_err("Failed to find boot partition\n");
		return;
	}

	/*
	 * Set the fw_name based on the partition type. This causes the GUID to be different
	 * so we will never accidentally flash a U-Boot image intended for XBL to the boot
	 * partition.
	 */
	switch (target_part_type) {
	case TARGET_PART_UEFI:
		fw_images[0].fw_name = u"UBOOT_UEFI_PARTITION";
		break;
	case TARGET_PART_XBL:
		fw_images[0].fw_name = u"UBOOT_XBL_PARTITION";
		break;
	case TARGET_PART_BOOT:
		fw_images[0].fw_name = u"UBOOT_BOOT_PARTITION";
		break;
	}

	switch (dev_uclass) {
	case UCLASS_SCSI:
		snprintf(dfu_string, 32, "scsi %d=u-boot.bin part %d", devnum, partnum);
		break;
	case UCLASS_MMC:
		snprintf(dfu_string, 32, "mmc 0=u-boot.bin part %d %d", devnum, partnum);
		break;
	default:
		debug("Unsupported storage uclass: %d\n", dev_uclass);
		return;
	}

	log_debug("RAW DFU string: '%s'\n", dfu_string);

	/* Explicitly set RAW configuration state */
	update_info.dfu_string = dfu_string;
	update_info.images = fw_images;           /* Ensure static array */
	update_info.num_images = ARRAY_SIZE(fw_images);  /* Always 1 for RAW */

	log_info("RAW capsule update configured (single partition: %s)\n",
		 target_part_type == TARGET_PART_UEFI ? "uefi" :
		 target_part_type == TARGET_PART_XBL ? "xbl" : "boot");
}
#endif /* CONFIG_EFI_CAPSULE_FIRMWARE_RAW */

#ifdef CONFIG_EFI_CAPSULE_FIRMWARE_FIT
/*
 * FIT Capsule Support - Implementation
 */

static void parse_partition_name(const char *full_name, char *base_name, char *slot_suffix)
{
	char *underscore = strrchr(full_name, '_');

	if (underscore && (strcmp(underscore, "_a") == 0 || strcmp(underscore, "_b") == 0)) {
		/* Has A/B suffix */
		size_t base_len = underscore - full_name;

		strlcpy(base_name, full_name, base_len + 1);
		strlcpy(slot_suffix, underscore, strlen(underscore) + 1);
	} else {
		/* No A/B suffix */
		strlcpy(base_name, full_name, strlen(full_name) + 1);
		slot_suffix[0] = '\0';
	}
}

static void parse_partition_info(struct qcom_partition_info *part,
				 struct disk_partition *info,
				 int lun, int partnum)
{
	struct part_slot_status *slot_status;

	strlcpy(part->name, info->name, sizeof(part->name));
	part->lun = lun;
	part->partition_num = partnum;

	/* Parse slot status from GPT vendor attributes */
	slot_status = (struct part_slot_status *)&info->type_flags;
	part->is_active = slot_status->active;
	part->is_bootable = !slot_status->unbootable;

	/* Extract base name and slot suffix */
	parse_partition_name(part->name, part->base_name, part->slot_suffix);
}

static struct partition_group *find_or_create_group(struct partition_group *groups,
						    int *group_count,
						    const char *base_name)
{
	/* Find existing group */
	for (int i = 0; i < *group_count; i++) {
		if (strcmp(groups[i].base_name, base_name) == 0)
			return &groups[i];
	}

	/* Create new group */
	if (*group_count >= MAX_PARTITION_GROUPS) {
		log_err("Too many partition groups\n");
		return NULL;
	}

	struct partition_group *new_group = &groups[*group_count];

	strlcpy(new_group->base_name, base_name, strlen(base_name) + 1);
	new_group->a_slot = NULL;
	new_group->b_slot = NULL;
	new_group->no_slot = NULL;

	(*group_count)++;
	return new_group;
}

static struct qcom_partition_info *select_ab_target(struct qcom_partition_info *a_slot,
						    struct qcom_partition_info *b_slot)
{
	/* Priority: Active slot > Bootable slot > A slot (fallback) */

	if (a_slot && a_slot->is_active) {
		log_debug("Selected %s (active)\n", a_slot->name);
		return a_slot;
	}
	if (b_slot && b_slot->is_active) {
		log_debug("Selected %s (active)\n", b_slot->name);
		return b_slot;
	}

	if (a_slot && a_slot->is_bootable) {
		log_debug("Selected %s (bootable)\n", a_slot->name);
		return a_slot;
	}
	if (b_slot && b_slot->is_bootable) {
		log_debug("Selected %s (bootable)\n", b_slot->name);
		return b_slot;
	}

	/* Both inactive - prefer A slot as fallback */
	struct qcom_partition_info *fallback = a_slot ? a_slot : b_slot;

	if (fallback)
		log_debug("Selected %s (fallback - both inactive)\n", fallback->name);
	return fallback;
}

static int discover_all_partitions(struct qcom_partition_info **all_parts, int *all_count)
{
	struct udevice *dev;
	struct blk_desc *desc;
	struct qcom_partition_info *partition_list;
	int partition_count = 0;
	int max_partitions = 256;

	/* Allocate partition list */
	partition_list = calloc(max_partitions, sizeof(struct qcom_partition_info));
	if (!partition_list) {
		log_err("Failed to allocate partition list\n");
		return -ENOMEM;
	}

	/* Scan for SCSI devices */
	if (IS_ENABLED(CONFIG_SCSI)) {
		if (scsi_scan(false)) {
			log_debug("Failed to scan SCSI devices\n");
			free(partition_list);
			return -EIO;
		}
	}

	/* Discover partitions only on supported SCSI LUNs */
	uclass_foreach_dev_probe(UCLASS_BLK, dev) {
		if (device_get_uclass_id(dev) != UCLASS_BLK)
			continue;

		desc = dev_get_uclass_plat(dev);
		if (!desc || desc->uclass_id != UCLASS_SCSI)
			continue;

		int lun = desc->devnum; /* Map device number to LUN */

		/* Scan all partitions on this supported LUN */
		for (int partnum = 1; partnum <= 128; partnum++) {
			struct disk_partition info;

			if (part_get_info(desc, partnum, &info) != 0)
				break; /* No more partitions */

			if (partition_count >= max_partitions) {
				log_warning("Too many partitions discovered, truncating at %d\n",
					    max_partitions);
				break;
			}

			/* Parse and store partition info */
			parse_partition_info(&partition_list[partition_count], &info, lun, partnum);
			partition_count++;
		}
	}

	*all_parts = partition_list;
	*all_count = partition_count;

	log_debug("Discovered %d partitions across all SCSI LUNs\n", partition_count);
	return 0;
}

static int select_target_partitions(struct qcom_partition_info *all_parts, int all_count,
				    struct qcom_partition_info **selected_parts,
				    int *selected_count)
{
	struct partition_group groups[MAX_PARTITION_GROUPS];
	struct qcom_partition_info *target_list;
	int group_count = 0;
	int target_count = 0;

	/* Initialize groups */
	memset(groups, 0, sizeof(groups));

	/* Allocate target list */
	target_list = calloc(all_count, sizeof(struct qcom_partition_info));
	if (!target_list) {
		log_err("Failed to allocate target partition list\n");
		return -ENOMEM;
	}

	/* Group partitions by base name */
	for (int i = 0; i < all_count; i++) {
		struct qcom_partition_info *part = &all_parts[i];
		struct partition_group *group = find_or_create_group(groups, &group_count,
								     part->base_name);

		if (!group) {
			log_err("Failed to create group for %s\n", part->base_name);
			continue;
		}

		if (strcmp(part->slot_suffix, "_a") == 0) {
			if (!group->a_slot) {
				group->a_slot = part;
			} else {
				log_info("Duplicate A-slot partition detected\n");
				log_info("  Keeping: %s (LUN %d, partition %d) [first discovered]\n",
					    group->a_slot->name, group->a_slot->lun,
					    group->a_slot->partition_num);
				log_info("  Ignoring: %s (LUN %d, partition %d) [duplicate]\n",
					    part->name, part->lun, part->partition_num);
			}
		} else if (strcmp(part->slot_suffix, "_b") == 0) {
			if (!group->b_slot) {
				group->b_slot = part;
			} else {
				log_info("Duplicate B-slot partition detected\n");
				log_info("  Keeping: %s (LUN %d, partition %d) [first discovered]\n",
					    group->b_slot->name, group->b_slot->lun,
					    group->b_slot->partition_num);
				log_info("  Ignoring: %s (LUN %d, partition %d) [duplicate]\n",
					    part->name, part->lun, part->partition_num);
			}
		} else {
			if (!group->no_slot) {
				group->no_slot = part;
			} else {
				log_info("Duplicate non-A/B partition detected\n");
				log_info("  Keeping: %s (LUN %d, partition %d) [first discovered]\n",
					    group->no_slot->name, group->no_slot->lun,
					    group->no_slot->partition_num);
				log_info("  Ignoring: %s (LUN %d, partition %d) [duplicate]\n",
					    part->name, part->lun, part->partition_num);
			}
		}
	}

	log_debug("Created %d partition groups for selection\n", group_count);

	/* Select target partition for each group */
	for (int i = 0; i < group_count; i++) {
		struct partition_group *group = &groups[i];
		struct qcom_partition_info *target = NULL;

		if (group->no_slot) {
			/* Non-A/B partition */
			target = group->no_slot;
			log_debug("Group %s: selected non-A/B partition %s\n",
				  group->base_name, target->name);
		} else {
			/* A/B partition - apply selection logic */
			target = select_ab_target(group->a_slot, group->b_slot);
			if (target) {
				log_debug("Group %s: selected %s from A/B pair\n",
					  group->base_name, target->name);
			}
		}

		if (target) {
			/* Copy selected partition to target list */
			memcpy(&target_list[target_count], target,
			       sizeof(struct qcom_partition_info));
			target_count++;
		} else {
			log_info("No target selected for group %s\n", group->base_name);
		}
	}

	*selected_parts = target_list;
	*selected_count = target_count;

	log_debug("Selected %d target partitions from %d discovered\n", target_count, all_count);
	return 0;
}

static int group_partitions_by_lun(struct qcom_partition_info *selected_parts, int selected_count,
				   struct lun_group **lun_groups, int *group_count)
{
	struct lun_group *groups;
	int max_groups = 16;  /* Reasonable limit for SCSI LUNs */
	int current_groups = 0;

	/* Allocate LUN groups array */
	groups = calloc(max_groups, sizeof(struct lun_group));
	if (!groups) {
		log_err("Failed to allocate LUN groups array\n");
		return -ENOMEM;
	}

	/* Group partitions by LUN */
	for (int i = 0; i < selected_count; i++) {
		struct qcom_partition_info *part = &selected_parts[i];
		struct lun_group *target_group = NULL;

		/* Find existing group for this LUN */
		for (int j = 0; j < current_groups; j++) {
			if (groups[j].lun_number == part->lun) {
				target_group = &groups[j];
				break;
			}
		}

		/* Create new group if not found */
		if (!target_group) {
			if (current_groups >= max_groups) {
				log_err("Too many LUN groups (max %d)\n", max_groups);
				free(groups);
				return -ENOSPC;
			}

			target_group = &groups[current_groups];
			target_group->lun_number = part->lun;
			target_group->partition_count = 0;
			current_groups++;
		}

		/* Add partition to group */
		if (target_group->partition_count >= 64) {
			log_err("Too many partitions in LUN %d (max 64)\n", part->lun);
			free(groups);
			return -ENOSPC;
		}

		target_group->partitions[target_group->partition_count] = part;
		target_group->partition_count++;
	}

		/* Sort groups by LUN number for consistent output */
		for (int i = 0; i < current_groups - 1; i++) {
			for (int j = i + 1; j < current_groups; j++) {
				if (groups[i].lun_number > groups[j].lun_number) {
					struct lun_group temp = groups[i];

					groups[i] = groups[j];
					groups[j] = temp;
				}
			}
		}

	*lun_groups = groups;
	*group_count = current_groups;

	log_debug("Grouped %d partitions into %d LUN groups\n", selected_count, current_groups);
	return 0;
}

static int generate_dfu_string(struct qcom_partition_info *selected_parts, int selected_count,
			       char *dfu_string, size_t buffer_size)
{
	struct lun_group *lun_groups = NULL;
	int group_count = 0;
	char *dfu_ptr = dfu_string;
	int remaining = buffer_size;
	int ret;

	/* Clear the buffer */
	memset(dfu_string, 0, buffer_size);

	/* Group partitions by LUN */
	ret = group_partitions_by_lun(selected_parts, selected_count, &lun_groups, &group_count);
	if (ret != 0) {
		log_err("Failed to group partitions by LUN: %d\n", ret);
		return ret;
	}

	/* Generate DFU string with grouped format */
	for (int i = 0; i < group_count; i++) {
		struct lun_group *group = &lun_groups[i];
		int written;

		/* Add LUN group separator for non-first groups */
		if (i > 0) {
			written = snprintf(dfu_ptr, remaining, "&");
			dfu_ptr += written;
			remaining -= written;
		}

		/* Add LUN prefix: "scsi X=" */
		written = snprintf(dfu_ptr, remaining, "scsi %d=", group->lun_number);
		dfu_ptr += written;
		remaining -= written;

		/* Add partitions within this LUN group */
		for (int j = 0; j < group->partition_count; j++) {
			struct qcom_partition_info *part = group->partitions[j];

			/* Add partition separator for non-first partitions in group */
			if (j > 0) {
				written = snprintf(dfu_ptr, remaining, ";");
				dfu_ptr += written;
				remaining -= written;
			}

			/* Add partition entry: "partition_name part partition_num" */
			written = snprintf(dfu_ptr, remaining, "%s part %d",
					   part->name, part->partition_num);
			dfu_ptr += written;
			remaining -= written;

			if (remaining <= 10) {
				log_err("DFU string buffer overflow at partition %s\n", part->name);
				free(lun_groups);
				return -ENOSPC;
			}
		}
	}

	/* Clean up */
	free(lun_groups);

	log_debug("Generated grouped DFU string (%zu chars): %s\n", strlen(dfu_string), dfu_string);
	return 0;
}

/**
 * get_board_fit_capsule_guid - Get board-specific FIT capsule GUID
 *
 * Detect the board type from device tree and return the appropriate GUID
 * for FIT capsule updates.
 *
 * @guid: Pointer to store the GUID
 * Return: 0 on success, negative error code on failure
 */
static int get_board_fit_capsule_guid(efi_guid_t *guid)
{
	const char *compatible;

	if (!guid)
		return -EINVAL;

	compatible = ofnode_read_string(ofnode_root(), "compatible");
	if (!compatible) {
		log_err("Failed to read board compatible string\n");
		return -ENODEV;
	}

	/* Check for QCS615 */
	if (strstr(compatible, "qcs615") || strstr(compatible, "QCS615")) {
		log_debug("Detected QCS615 board\n");
		*guid = (efi_guid_t)QCOM_QCS615_FIT_CAPSULE_GUID;
		return 0;
	}

	/* Check for QCS6490 */
	if (strstr(compatible, "qcs6490") || strstr(compatible, "QCS6490")) {
		log_debug("Detected QCS6490 board\n");
		*guid = (efi_guid_t)QCOM_QCS6490_FIT_CAPSULE_GUID;
		return 0;
	}

	log_err("Unsupported board for capsule updates: %s\n", compatible);
	return -EINVAL;
}

static void configure_fit_capsule_updates(void)
{
	struct qcom_partition_info *all_partitions = NULL;
	struct qcom_partition_info *selected_partitions = NULL;
	int all_count = 0, selected_count = 0;
	static char dfu_string[MAX_DFU_STRING_SIZE] = { 0 };
	static struct efi_fw_image single_fw_image;
	efi_guid_t board_guid;
	int ret;

	/* Step 1: Discover all partitions across all SCSI LUNs */
	ret = discover_all_partitions(&all_partitions, &all_count);
	if (ret != 0) {
		log_err("Failed to discover SCSI partitions: %d\n", ret);
		return;
	}

	if (all_count == 0) {
		log_warning("No SCSI partitions discovered\n");
		goto cleanup;
	}

	/* Step 2: Apply A/B selection logic to choose target partitions */
	ret = select_target_partitions(all_partitions, all_count,
				       &selected_partitions, &selected_count);
	if (ret != 0) {
		log_err("Failed to select target partitions: %d\n", ret);
		goto cleanup;
	}

	if (selected_count == 0) {
		log_warning("No target partitions selected\n");
		goto cleanup;
	}

	/* Step 3: Generate DFU string from selected partitions */
	ret = generate_dfu_string(selected_partitions, selected_count,
				  dfu_string, sizeof(dfu_string));
	if (ret != 0) {
		log_err("Failed to generate DFU string: %d\n", ret);
		goto cleanup;
	}

	/* Step 4: Get board-specific GUID */
	ret = get_board_fit_capsule_guid(&board_guid);
	if (ret != 0) {
		log_err("Failed to get board-specific GUID: %d\n", ret);
		goto cleanup;
	}

	/* Step 5: Create SINGLE fw_image entry for ESRT */
	memset(&single_fw_image, 0, sizeof(single_fw_image));
	single_fw_image.fw_name = QCOM_FIT_CAPSULE_NAME;  /* Same name for all boards */
	single_fw_image.image_index = 1;
	single_fw_image.image_type_id = board_guid;

	/* Step 6: Configure update_info with SINGLE entry */
	update_info.dfu_string = dfu_string;
	update_info.images = &single_fw_image;
	update_info.num_images = 1;

	log_info("FIT capsule configured successfully:\n");
	log_info("  Name: %ls\n", QCOM_FIT_CAPSULE_NAME);
	log_info("  GUID: %pUl\n", &board_guid);
	log_info("  Partitions in DFU string: %d\n", selected_count);
	log_info("  ESRT entries: 1 (single entry for all partitions)\n");

cleanup:
	free(all_partitions);
	free(selected_partitions);
}
#endif /* CONFIG_EFI_CAPSULE_FIRMWARE_FIT */

/**
 * qcom_configure_capsule_updates() - Configure capsule updates
 *
 * Priority-based conditional implementation:
 * - CONFIG_EFI_CAPSULE_FIRMWARE_RAW: Takes priority
 * - CONFIG_EFI_CAPSULE_FIRMWARE_FIT: Only runs if RAW didn't configure
 */
void qcom_configure_capsule_updates(void)
{
#ifdef CONFIG_EFI_CAPSULE_FIRMWARE_RAW
	log_info("Configuring RAW capsule updates (priority mode)\n");
	configure_raw_capsule_updates();
#endif
#ifdef CONFIG_EFI_CAPSULE_FIRMWARE_FIT
	/* Only run FIT if RAW didn't already configure */
	if (!update_info.dfu_string) {
		log_info("Configuring FIT capsule updates\n");
		configure_fit_capsule_updates();
	} else {
		log_info("Skipping FIT configuration - RAW already configured\n");
	}
#endif

	/* Final state logging */
	if (update_info.dfu_string) {
		log_info("Capsule update configured: %s mode with %d image(s)\n",
			 (update_info.num_images == 1) ? "RAW" : "FIT",
			 update_info.num_images);
	} else {
		log_warning("Capsule update configuration failed\n");
	}
}
