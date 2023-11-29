/*
 * Copyright (c) 2015-2018, 2020 The Linux Foundation. All rights reserved.
 * Copyright (c) 2023, Qualcomm Innovation Center, Inc. All rights reserved.

 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <stdio.h>
#include <memalign.h>
#include <command.h>
#include <common.h>
#include <time.h>
#include <cpu_func.h>
#include <mach/ipq_scm.h>
#include <dm.h>
#ifdef CONFIG_IPQ_CRASHDUMP_TO_USB
#include <usb.h>
#include <fat.h>
#endif
#ifdef CONFIG_IPQ_COMPRESSED_CRASHDUMP
#include <gzip.h>
#endif

#include "ipq_board.h"

#define TFTP_MAX_TRF_SZ_LIMIT			SZ_1G
#define DRAM_DUMP_NAME_PREFIX			"EBICS"

#ifdef CONFIG_IPQ_CRASHDUMP_TO_MEMORY

#define DUMP2MEM_MAGIC1_COOKIE			0x44554D50
#define DUMP2MEM_MAGIC2_COOKIE			0x324D454D

typedef struct {
	char name[DUMP_NAME_STR_MAX_LEN];
	uint32_t offset;
	uint32_t size;
} memdump_list_info_t;

typedef struct {
	uint32_t magic1;
	uint32_t magic2;
	uint32_t nos_memdumps;
	uint32_t total_dump_sz;
	uint32_t dump_list_info_offset;
} memdump_hdr_t;
#endif /* CONFIG_IPQ_CRASHDUMP_TO_MEMORY */

#ifdef CONFIG_IPQ_MINIDUMP

#define	CPU_DUMP_NAME_PREFIX			"CPU_INFO"
#define	UNAME_DUMP_NAME_PREFIX			"UNAME"
#define	DMESG_DUMP_NAME_PREFIX			"DMESG"
#define	PT_DUMP_NAME_PREFIX			"PT"
#define	WLAN_MOD_DUMP_NAME_PREFIX		"WLAN_MOD"

#define CFG_QTI_KERN_WDT_ADDR			*((unsigned int *)0x08600658)
#define CFG_CPU_CONTEXT_DUMP_SIZE		4096

#define QTI_WDT_SCM_TLV_TYPE_SIZE		1
#define QTI_WDT_SCM_TLV_LEN_SIZE		2
#define QTI_WDT_SCM_TLV_TYPE_LEN_SIZE		(QTI_WDT_SCM_TLV_TYPE_SIZE +\
						QTI_WDT_SCM_TLV_LEN_SIZE)
#define TME_CTXT_SIZE				(300 * 1024)
#define TLV_BUF_OFFSET				(500 * 1024) - TME_CTXT_SIZE
#define CFG_TLV_DUMP_SIZE			(12 * 1024)

typedef struct {
	uint8_t *msg_buf;
	uint8_t *cur_msg_buf;
	uint32_t buf_len;
} wdt_dump_tlv_infos_t;

typedef struct {
	uint64_t start;
	uint64_t size;
} st_tlv_data_t;

enum {
	/*Basic DDR segments */
	QTI_WDT_LOG_DUMP_TYPE_INVALID = 0,
	QTI_WDT_LOG_DUMP_TYPE_UNAME,
	QTI_WDT_LOG_DUMP_TYPE_DMESG,
	QTI_WDT_LOG_DUMP_TYPE_LEVEL1_PT,

	/* Module structures are in highmem zone*/
	QTI_WDT_LOG_DUMP_TYPE_WLAN_MOD,
	QTI_WDT_LOG_DUMP_TYPE_WLAN_MOD_DEBUGFS,
	QTI_WDT_LOG_DUMP_TYPE_WLAN_MOD_INFO,
	QTI_WDT_LOG_DUMP_TYPE_WLAN_MMU_INFO,
	QTI_WDT_LOG_DUMP_TYPE_EMPTY,
} wdt_dump_types_t;
#endif /* CONFIG_IPQ_MINIDUMP */

typedef struct {
	char name[DUMP_NAME_STR_MAX_LEN];
	uint32_t start_addr;
	uint32_t size;
	uint8_t is_aligned_access:1;
	uint8_t compression_support:1;
	struct list_head list;
} crashdump_infos_int_t;

typedef struct {
	char *tftp_serverip;
	char *tftp_dumpdir;

#ifdef CONFIG_IPQ_CRASHDUMP_TO_USB
	uint8_t usb_dev_idx;
	uint8_t usb_part_idx;
#endif /* CONFIG_IPQ_CRASHDUMP_TO_USB */

#ifdef CONFIG_IPQ_CRASHDUMP_TO_MEMORY
	uint32_t dump2mem_rsvd_addr;
	uint32_t dump2mem_curr_addr;
	memdump_hdr_t memdump_hdr;
	memdump_list_info_t *memdump_list;
#endif /* CONFIG_IPQ_CRASHDUMP_TO_MEMORY */

} crashdump_interface_cfg_t;

typedef struct {
	bool force_collect_dump;
	uint8_t debug;
	uint8_t dump_level;
	uint8_t dump_to;
	uint8_t is_compress_enabled;
	crashdump_interface_cfg_t iface_cfg;
	crashdump_infos_t *dump_infos;
	uint8_t nos_dumps;
	uint16_t actual_nos_dumps;
} crashdump_config_t;

static LIST_HEAD(actual_dumps_list);
static crashdump_config_t dump_config;

/**
 * add_entry_crashdump_table() - Adds an entry into dump table
 * &dump_config - crashdump ocnfiguration info
 * &dump_entry - entry that needs to be added into the dump table
 */
static void add_entry_crashdump_table(crashdump_config_t *dump_config,
		crashdump_infos_int_t *dump_entry)
{
	crashdump_infos_int_t *new_entry =
		malloc(sizeof(crashdump_infos_int_t));

	memset(new_entry, 0, sizeof(crashdump_infos_int_t));
	memcpy(new_entry, dump_entry, sizeof(crashdump_infos_int_t));
	list_add_tail(&new_entry->list, &actual_dumps_list);
	dump_config->actual_nos_dumps++;

	if (dump_config->debug)
		printf("%-20s\t 0x%08X\t 0x%08X\t %-10s\t %-10s\t\n",
			new_entry->name, new_entry->start_addr,
			new_entry->size,
			(new_entry->is_aligned_access ? "true":"false"),
			(new_entry->compression_support ? "true":"false"));
}

/**
 * delete_crashdump_table() - remove all the dump table entries and frees
 * the pointer.
 */
static void delete_crashdump_table(void)
{
	crashdump_infos_int_t *dump_entry, *tmp_entry;

	list_for_each_entry_safe(dump_entry, tmp_entry,
			&actual_dumps_list, list) {
		list_del(&dump_entry->list);
		free(dump_entry);
	}
}

#ifdef CONFIG_IPQ_MINIDUMP
/**
 * wdt_extract_tlv_info() - extract tlv header info
 * &tlv_info - tlv dump list head pointer
 * &type - its an o/p variable contains the tlv type
 * &size - its an o/p variable contains the tlv size
 */
static int wdt_extract_tlv_info(wdt_dump_tlv_infos_t *tlv_info,
		uint8_t *type, uint32_t *size)
{
	uint8_t *x = tlv_info->cur_msg_buf;
	uint8_t *y = tlv_info->msg_buf + tlv_info->buf_len;

	if ((x + QTI_WDT_SCM_TLV_TYPE_LEN_SIZE) >= y)
		return -EINVAL;

	*type = x[0];
	*size = x[1] | (x[2] << 8);
	return 0;
}

/**
 * wdt_extract_tlv_data() - extract tlv data
 * &tlv_info - tlv dump list head pointer
 * &data - pointer to the buffer where the extracted tlv data is copied
 * &size - size of the tlv data
 */
static int wdt_extract_tlv_data(wdt_dump_tlv_infos_t *tlv_info,
		uint8_t *data, uint32_t size)
{
	uint8_t *x = tlv_info->cur_msg_buf;
	uint8_t *y = tlv_info->msg_buf + tlv_info->buf_len;

	if ((x + QTI_WDT_SCM_TLV_TYPE_LEN_SIZE + size) >= y)
		return -EINVAL;

	memcpy(data, x + 3, size);

	tlv_info->cur_msg_buf += (size + QTI_WDT_SCM_TLV_TYPE_LEN_SIZE);
	return 0;
}

/**
 * wdt_extract_dump() - extract details of the requested tlv dump
 * &dump_config - crashdump configuration info
 * &dump_idx - buffer index points to the requested dump info
 * &dump_entry - pointer to store the extract tlv dump info
 */
static int wdt_extract_dump(crashdump_config_t *dump_config, int dump_idx,
		crashdump_infos_int_t *dump_entry)
{
	int ret = CMD_RET_FAILURE;
	uint8_t cur_type, i;
	uint8_t tlv_type = cur_type = QTI_WDT_LOG_DUMP_TYPE_INVALID;
	uint32_t cur_size = 0;
	wdt_dump_tlv_infos_t tlv_info;
	crashdump_infos_t *dump_infos = &dump_config->dump_infos[dump_idx];
	char *dumps[] = { "INVALID",
		UNAME_DUMP_NAME_PREFIX,
		DMESG_DUMP_NAME_PREFIX,
		PT_DUMP_NAME_PREFIX,
		WLAN_MOD_DUMP_NAME_PREFIX,
	};

	if (!strncmp(dump_infos->name, CPU_DUMP_NAME_PREFIX,
				strlen(CPU_DUMP_NAME_PREFIX))) {
		dump_entry->start_addr = CFG_QTI_KERN_WDT_ADDR;
		dump_entry->size = CFG_CPU_CONTEXT_DUMP_SIZE;
		snprintf(dump_entry->name, sizeof(dump_entry->name),
				"%X.BIN", dump_entry->start_addr);
		ret = CMD_RET_SUCCESS;
		return ret;
	}

	for (i=0; i<=QTI_WDT_LOG_DUMP_TYPE_WLAN_MOD; i++) {
		if (!strncmp(dump_infos->name, dumps[i], strlen(dumps[i]))) {
			tlv_type = i;
			break;
		}
	}

	if (tlv_type == QTI_WDT_LOG_DUMP_TYPE_INVALID)
		return ret;

	tlv_info.msg_buf = (uint8_t*)(uintptr_t)
		(CFG_QTI_KERN_WDT_ADDR + TLV_BUF_OFFSET);
	tlv_info.cur_msg_buf = tlv_info.msg_buf;
	tlv_info.buf_len = CFG_TLV_DUMP_SIZE;

	do {
		uint8_t buf[512] = { 0 };
		st_tlv_data_t *tlv_data = NULL;

		ret = wdt_extract_tlv_info(&tlv_info, &cur_type, &cur_size);
		if (ret)
			break;

		if ((tlv_type == QTI_WDT_LOG_DUMP_TYPE_WLAN_MOD) &&
				(cur_type >= QTI_WDT_LOG_DUMP_TYPE_WLAN_MOD)
				&& (cur_type <=
				QTI_WDT_LOG_DUMP_TYPE_WLAN_MMU_INFO)){

			ret = wdt_extract_tlv_data(&tlv_info, buf, cur_size);
			if (ret)
				break;

			tlv_data = (st_tlv_data_t*)&buf;
			dump_entry->start_addr = tlv_data->start;

			switch (cur_type) {
			case QTI_WDT_LOG_DUMP_TYPE_WLAN_MOD_INFO:
				snprintf(dump_entry->name,
					sizeof(dump_entry->name),
					"MOD_INFO.txt");
				dump_entry->size = *(uint32_t *)(uintptr_t)
					tlv_data->size;
				break;
			case QTI_WDT_LOG_DUMP_TYPE_WLAN_MMU_INFO:
				snprintf(dump_entry->name,
					sizeof(dump_entry->name),
					"MMU_INFO.txt");
				dump_entry->size = *(uint32_t *)(uintptr_t)
					tlv_data->size;
				break;
			case QTI_WDT_LOG_DUMP_TYPE_WLAN_MOD_DEBUGFS:
				snprintf(dump_entry->name,
					sizeof(dump_entry->name),
					"DEBUGFS_%X.BIN",
					(uint32_t)tlv_data->start);
				dump_entry->size = tlv_data->size;
				break;
			case QTI_WDT_LOG_DUMP_TYPE_WLAN_MOD:
				snprintf(dump_entry->name,
					sizeof(dump_entry->name),
					"%X.BIN", (uint32_t)tlv_data->start);
				dump_entry->size = tlv_data->size;
				break;
			}

			add_entry_crashdump_table(dump_config, dump_entry);
		} else if (cur_type == tlv_type) {
			ret = wdt_extract_tlv_data(&tlv_info, buf, cur_size);
			if (ret)
				break;

			switch (cur_type) {
			case QTI_WDT_LOG_DUMP_TYPE_UNAME:
				void * uname_buf = malloc(cur_size);
				if (uname_buf)
					return -ENOMEM;
				else
					memcpy(uname_buf, buf, cur_size);
				dump_entry->start_addr = (uintptr_t)uname_buf;
				dump_entry->size = cur_size;
				break;
			case QTI_WDT_LOG_DUMP_TYPE_DMESG:
				tlv_data = (st_tlv_data_t*)&buf;
				dump_entry->start_addr = tlv_data->start;
				dump_entry->size = *(uint32_t *)(uintptr_t)
					tlv_data->size;
				break;
			case QTI_WDT_LOG_DUMP_TYPE_LEVEL1_PT:
				tlv_data = (st_tlv_data_t*)&buf;
				dump_entry->start_addr = tlv_data->start;
				dump_entry->size = tlv_data->size;
				break;
			}

			snprintf(dump_entry->name, sizeof(dump_entry->name),
					"%X.BIN", dump_entry->start_addr);
			ret = CMD_RET_SUCCESS;
			break;
		} else {
			tlv_info.cur_msg_buf +=
				(cur_size + QTI_WDT_SCM_TLV_TYPE_LEN_SIZE);
		}
	} while (cur_type != QTI_WDT_LOG_DUMP_TYPE_INVALID);
	return ret;
}
#endif /* CONFIG_IPQ_MINIDUMP */

/**
 * ipq_read_tcsr_boot_misc() - read boot tcsr register
 */
static int ipq_read_tcsr_boot_misc(void)
{
	u32 *dmagic = TCSR_BOOT_MISC_REG;
	return *dmagic;
}

/**
 * ipq_iscrashed_crashdump_disabled() - to check whether crashdump is
 * enabled or disabled
 */
static int ipq_iscrashed_crashdump_disabled(void)
{
	u32 dmagic = ipq_read_tcsr_boot_misc();
	return ((dmagic & DLOAD_DISABLED) ? 1 : 0);
}

/**
 * ipq_iscrashed() - to check whether system is in crashdump path or not
 */
static int ipq_iscrashed(void)
{
	u32 dmagic = ipq_read_tcsr_boot_misc();
	return ((dmagic & DLOAD_MAGIC_COOKIE) ? 1 : 0);
}

/**
 * parse_crashdump_config() - parse the crashdump configurations from the
 * system environments
 * &dump_config - pointer where the parsed info is stored to
 */
static void parse_crashdump_config(crashdump_config_t * dump_config)
{
#ifdef CONFIG_IPQ_MINIDUMP
	if (env_get("dump_minimal_and_full"))
		dump_config->dump_level = MINIDUMP_AND_FULLDUMP;
	else if (env_get("dump_minimal"))
		dump_config->dump_level = MINIDUMP;
	else
#endif /* CONFIG_IPQ_MINIDUMP */
		dump_config->dump_level = FULLDUMP;

	dump_config->dump_to = DUMP_TO_TFTP;
#ifdef CONFIG_IPQ_CRASHDUMP_TO_USB
	if (env_get("dump_to_usb"))
		dump_config->dump_to = DUMP_TO_USB;
#endif /* CONFIG_IPQ_CRASHDUMP_TO_USB */

#ifdef CONFIG_IPQ_CRASHDUMP_TO_MEMORY
	if (env_get("dump_to_mem"))
		dump_config->dump_to = DUMP_TO_MEM;
#endif /* CONFIG_IPQ_CRASHDUMP_TO_MEMORY */

	dump_config->force_collect_dump =
		env_get("force_collect_dump") ? 1 : 0;

#ifdef CONFIG_IPQ_COMPRESSED_CRASHDUMP
	dump_config->is_compress_enabled = env_get("dump_compressed") ? 1 : 0;
#endif /* CONFIG_IPQ_COMPRESSED_CRASHDUMP */
	return;
}

/**
 * verify_crashdump_config() - verify the given crashdump configuration and
 * check for conflicts and return failure when it conflicts.
 * &dump_config - crashdump configuration info
 */
static int verify_crashdump_config(crashdump_config_t * dump_config)
{
	int ret = CMD_RET_SUCCESS;

#ifdef CONFIG_IPQ_MINIDUMP
	if (dump_config->dump_level == MINIDUMP_AND_FULLDUMP) {
		if (dump_config->is_compress_enabled) {
			printf("Compression is not supported in both "
					"minimal and fulldump\n");
			return CMD_RET_FAILURE;
		}
	}
#endif

	switch (dump_config->dump_to) {
#ifdef CONFIG_IPQ_CRASHDUMP_TO_USB
	case DUMP_TO_USB:
		break;
#endif /* CONFIG_IPQ_CRASHDUMP_TO_USB */

#ifdef CONFIG_IPQ_CRASHDUMP_TO_MEMORY
	case DUMP_TO_MEM:
		char *tmp = env_get("dump_to_mem");
		if (!str2long(tmp,(ulong*)
				&dump_config->iface_cfg.dump2mem_rsvd_addr)) {
			printf("Failed to decode dump_to_mem reserved mem \n");
			ret = CMD_RET_FAILURE;
		}

		/* range check for the dump2mem_addr */
		if (dump_config->dump_level != MINIDUMP) {
			printf("Only Minidump is supported in dump_to_mem\n");
			ret = CMD_RET_FAILURE;
		}

		if (dump_config->is_compress_enabled) {
			printf("Compression is not supported "
					"in dump_to_mem\n");
			ret = CMD_RET_FAILURE;
		}
		break;
#endif /* CONFIG_IPQ_CRASHDUMP_TO_MEMORY */

	case DUMP_TO_TFTP:
		dump_config->iface_cfg.tftp_serverip = env_get("serverip");
		if (dump_config->iface_cfg.tftp_serverip != NULL) {
			printf("Using Server IP from env %s\n",
					dump_config->iface_cfg.tftp_serverip);
		} else {
			printf("Server IP not found, run dhcp or configure\n");
			ret = CMD_RET_FAILURE;
		}

		dump_config->iface_cfg.tftp_dumpdir = env_get("dumpdir");
		if (dump_config->iface_cfg.tftp_dumpdir != NULL)
			printf("Using directory %s in TFTP server\n",
					dump_config->iface_cfg.tftp_dumpdir);
		else {
			dump_config->iface_cfg.tftp_dumpdir = "";
			printf("Env 'dumpdir' not set. "
					"Using / dir in TFTP server\n");
		}
		break;
	default:
		ret = CMD_RET_FAILURE;
		break;
	}

	return ret;
}

#ifdef CONFIG_IPQ_CRASHDUMP_TO_USB
/**
 * find_usb_dev_for_crashdump() - find the first usb device & partition with
 * FAT filesystem.
 * &dev_idx - its an o/p variable points to USB device index
 * &part_idx - its an o/p variable points to USB partition index
 */
static int find_usb_dev_for_crashdump(uint8_t *dev_idx, uint8_t *part_idx)
{
	int ret = CMD_RET_FAILURE;

	struct udevice *dev;

	for (blk_first_device(UCLASS_USB, &dev); dev;
			blk_next_device(&dev)) {
		char dev_str[5] = { 0 };
		struct blk_desc *bdesc = dev_get_uclass_plat(dev);
		struct disk_partition dpart_info;

		for (int pidx = 1; pidx <= MAX_SEARCH_PARTITIONS;
				pidx++) {
			snprintf(dev_str, sizeof(dev_str)+1, "%d:%d",
					bdesc->devnum, pidx);
			ret = blk_get_device_part_str("usb", dev_str,
					&bdesc, &dpart_info, 1);
			if (ret < 0)
				continue;

			if (fat_set_blk_dev(bdesc, &dpart_info) == 0) {
				*dev_idx = bdesc->devnum;
				*part_idx = pidx;
				ret = CMD_RET_SUCCESS;

				printf("Selected Device:%d "
					"Partition:%d for USB dump:\n",
					*dev_idx, *part_idx);
				break;
			}
		}
	}

	return ret;
}
#endif /* CONFIG_IPQ_CRASHDUMP_TO_USB */

/**
 * verify_crashdump_iface() - verify the opted crashdump inferface
 * &dump_config - crashdump configuration info
 */
static int verify_crashdump_iface(crashdump_config_t * dump_config)
{
	int ret = CMD_RET_SUCCESS;
	uint64_t etime;
	char runcmd[50] = {0};
	uint8_t ping_status = 0;

	switch (dump_config->dump_to) {
#ifdef CONFIG_IPQ_CRASHDUMP_TO_USB
	case DUMP_TO_USB:
		ret = usb_init();
		if (ret) {
			ret = CMD_RET_FAILURE;
			break;
		}

		ret = find_usb_dev_for_crashdump(
				&dump_config->iface_cfg.usb_dev_idx,
				&dump_config->iface_cfg.usb_part_idx);
		break;
#endif /* CONFIG_IPQ_CRASHDUMP_TO_USB */

#ifdef CONFIG_IPQ_CRASHDUMP_TO_MEMORY
	case DUMP_TO_MEM:
		memdump_hdr_t *hdr = &dump_config->iface_cfg.memdump_hdr;
		dump_config->iface_cfg.dump2mem_curr_addr =
			dump_config->iface_cfg.dump2mem_rsvd_addr;
		hdr->magic1 = DUMP2MEM_MAGIC1_COOKIE;
		hdr->magic2 = DUMP2MEM_MAGIC2_COOKIE;
		hdr->nos_memdumps = 0;
		hdr->total_dump_sz = 0;
		hdr->dump_list_info_offset = 0;
		dump_config->iface_cfg.dump2mem_curr_addr =
			roundup(dump_config->iface_cfg.dump2mem_curr_addr +
				sizeof(memdump_hdr_t), ARCH_DMA_MINALIGN);
		break;
#endif /* CONFIG_IPQ_CRASHDUMP_TO_MEMORY */

	case DUMP_TO_TFTP:
		printf("Trying to ping server.....\n");
		snprintf(runcmd, sizeof(runcmd), "ping %s",
				dump_config->iface_cfg.tftp_serverip);
		etime = get_timer(0) + (10 * CONFIG_SYS_HZ);
		while (get_timer(0) <= etime) {
			if (run_command(runcmd, 0) == CMD_RET_SUCCESS) {
				ping_status = 1;
				break;
			}

			mdelay(500);
		}

		if (ping_status != 1) {
			printf("Ping failed\n");
			ret = CMD_RET_FAILURE;
		}
		break;
	default:
		ret = CMD_RET_FAILURE;
		break;
	}

	return ret;
}

/**
 * split_bin_dump() - split the given dump into the requested split size
 * &dump_config - crashdump configuration info
 * &dump_entry - requested dump to split
 * &split_size - split size of the dumps
 * &dump_name_prefix - prefix name used for the output dumps
 */
static void split_bin_dump(crashdump_config_t *dump_config,
		crashdump_infos_int_t *dump_entry, uint32_t split_size,
		char dump_name_prefix[DUMP_NAME_STR_MAX_LEN])
{
	uint32_t dump_sz = dump_entry->size;
	uint32_t end_addr = dump_entry->start_addr + dump_sz;
	uint8_t file_no = (dump_sz / split_size) - 1;

	file_no = (dump_sz % split_size) ? file_no+1 : file_no;

	while (dump_sz > 0) {
		snprintf(dump_entry->name, sizeof(dump_entry->name),
				"%s%d.BIN", dump_name_prefix, file_no--);
		if (dump_sz > split_size)
			dump_entry->size = split_size;
		else
			dump_entry->size = dump_sz;

		end_addr -= dump_entry->size;
		dump_sz -= dump_entry->size;
		dump_entry->start_addr = end_addr;
		add_entry_crashdump_table(dump_config, dump_entry);
	}
}

/**
 * prepare_crashdump_level_table() - prepare the crashdump table based on the
 * dump level, dump configuration and with respect to the given into dump
 * table.
 * &dump_config - crashdump configuration info
 * &dump_level - requested dump level
 */
static void prepare_crashdump_level_table(crashdump_config_t *dump_config,
		uint8_t dump_level)
{
	int i;
	crashdump_infos_int_t dump_entry;
	crashdump_infos_t *dump_infos = dump_config->dump_infos;
	char dump_name_prefix[DUMP_NAME_STR_MAX_LEN] = { 0 };
	uint32_t split_bin_sz = 0;

	for (i=0; i < dump_config->nos_dumps; i++) {
		if (dump_level != dump_infos[i].dump_level)
			continue;

		memset(&dump_entry, 0, sizeof(crashdump_infos_int_t));
		memcpy(&dump_name_prefix, dump_infos[i].name,
				(strlen(dump_infos[i].name) - 4));
		dump_name_prefix[(strlen(dump_infos[i].name) - 4)] = '\0';

		strlcpy(dump_entry.name, dump_infos[i].name,
				DUMP_NAME_STR_MAX_LEN);
		dump_entry.start_addr = dump_infos[i].start_addr;
		dump_entry.size = dump_infos[i].size;
		dump_entry.is_aligned_access = dump_infos[i].is_aligned_access;
		split_bin_sz = dump_infos[i].split_bin_sz;

#ifdef CONFIG_IPQ_COMPRESSED_CRASHDUMP
		if (dump_config->is_compress_enabled)
			dump_entry.compression_support =
				dump_infos[i].compression_support;
#endif /* CONFIG_IPQ_COMPRESSED_CRASHDUMP */

		if (dump_infos[i].size == 0xBAD0FF5E) {
			switch (dump_level) {
#ifdef CONFIG_IPQ_MINIDUMP
			case MINIDUMP:
				if (wdt_extract_dump(dump_config, i,
							&dump_entry))
					continue;
				break;
#endif /* CONFIG_IPQ_MINIDUMP */
			case FULLDUMP:
				if (!strncmp(dump_infos[i].name,
						DRAM_DUMP_NAME_PREFIX,
						strlen(DRAM_DUMP_NAME_PREFIX)))
				{
					snprintf(dump_entry.name,
						sizeof(dump_entry.name),
						"%s0.BIN", dump_name_prefix);
					dump_entry.size = gd->ram_size;
				}
				break;
			}
		}

#ifdef CONFIG_IPQ_COMPRESSED_CRASHDUMP
		if (dump_entry.compression_support) {
			if (dump_entry.size > TFTP_MAX_TRF_SZ_LIMIT)
				split_bin_sz = TFTP_MAX_TRF_SZ_LIMIT;
			else {
				if (gd->ram_size == dump_entry.size)
					split_bin_sz = (gd->ram_size / 2);
			}
		}
#endif /* CONFIG_IPQ_COMPRESSED_CRASHDUMP */

		if (split_bin_sz && (dump_entry.size > split_bin_sz)) {
			split_bin_dump(dump_config, &dump_entry,
					split_bin_sz, dump_name_prefix);
			continue;
		}

		add_entry_crashdump_table(dump_config, &dump_entry);
	}

	return;
}

/**
 * prepare_crashdump_table() - prepare crashdump table top level
 */
static void prepare_crashdump_table(crashdump_config_t *dump_config)
{
	dump_config->actual_nos_dumps = 0;
	if (dump_config->debug)
		printf("%-20s\t %-10s\t %-10s\t %-10s\t %-10s\n",
			"Name", "Address", "Size", "Alignment", "Compressed");

	switch (dump_config->dump_level) {
#ifdef CONFIG_IPQ_MINIDUMP
	case MINIDUMP_AND_FULLDUMP:
		prepare_crashdump_level_table(dump_config, FULLDUMP);
	case MINIDUMP:
		prepare_crashdump_level_table(dump_config, MINIDUMP);
		break;
#endif /* CONFIG_IPQ_MINIDUMP */
	case FULLDUMP:
		prepare_crashdump_level_table(dump_config, FULLDUMP);
		break;
	}

	return;
}

/**
 * dump_to_dst() - Do the actual dumping based on the requested dump entry
 * &dump_config - crashdump configuration info
 * &dump_entry - requested dump entry
 */
static int dump_to_dst(crashdump_config_t *dump_config,
			crashdump_infos_int_t *dump_entry)
{
	char runcmd[256] = { 0 };
	crashdump_interface_cfg_t *iface_cfg = &dump_config->iface_cfg;

	if (dump_entry->is_aligned_access) {
		long unsigned int aligned_addr = (gd->ram_top -
				roundup(dump_entry->size, ARCH_DMA_MINALIGN));
		memcpy((void*)aligned_addr,
				(void*)(uintptr_t)dump_entry->start_addr,
				dump_entry->size);
		dump_entry->start_addr = aligned_addr;
	}

#ifdef CONFIG_IPQ_COMPRESSED_CRASHDUMP
	if (dump_entry->compression_support) {
		long unsigned int compress_out_sz = ((dump_entry->size > SZ_1M)
				? dump_entry->size : SZ_1M);
		long unsigned int compress_out_addr = gd->ram_top -
			compress_out_sz;

		printf("Compressing %s... ", dump_entry->name);
		if (!strncmp(dump_entry->name, "EBICS0.BIN",
					strlen("EBICS0.BIN"))) {
			memcpy((void*)compress_out_addr, (void*)
					(uintptr_t)dump_entry->start_addr,
					dump_entry->size);
			dump_entry->start_addr = compress_out_addr;
		}

		if (gzip((void *)(uintptr_t) compress_out_addr,
				&compress_out_sz,
				(void*)(uintptr_t) dump_entry->start_addr,
				dump_entry->size) != 0) {
			printf("failed\n");
			return CMD_RET_FAILURE;
		}
		printf("done!!\n");

		dump_entry->start_addr = compress_out_addr;
		dump_entry->size = compress_out_sz;
		snprintf(dump_entry->name, DUMP_NAME_STR_MAX_LEN,
				"%s.gz", dump_entry->name);
	}
#endif /* CONFIG_IPQ_COMPRESSED_CRASHDUMP */

	switch (dump_config->dump_to) {
#ifdef CONFIG_IPQ_CRASHDUMP_TO_USB
	case DUMP_TO_USB:
		printf("Writing file %s into USB \n", dump_entry->name);
		snprintf(runcmd, sizeof(runcmd),
				"fatwrite usb %x:%x 0x%x %s 0x%x",
				iface_cfg->usb_dev_idx,
				iface_cfg->usb_part_idx,
				dump_entry->start_addr, dump_entry->name,
				dump_entry->size);
		break;
#endif /* CONFIG_IPQ_CRASHDUMP_TO_USB */

#ifdef CONFIG_IPQ_CRASHDUMP_TO_MEMORY
	case DUMP_TO_MEM:
		memdump_hdr_t *hdr = &iface_cfg->memdump_hdr;
		int i = hdr->nos_memdumps++;
		memdump_list_info_t *list = &iface_cfg->memdump_list[i];
		strlcpy(list->name, dump_entry->name, DUMP_NAME_STR_MAX_LEN);
		list->offset = iface_cfg->dump2mem_curr_addr -
			iface_cfg->dump2mem_rsvd_addr;
		list->size = dump_entry->size;

		printf("Dumping %s @ 0x%X \n", dump_entry->name,
				iface_cfg->dump2mem_curr_addr);
		memcpy((void*)(uintptr_t)iface_cfg->dump2mem_curr_addr,
				(void*)(uintptr_t)dump_entry->start_addr,
				dump_entry->size);
		iface_cfg->dump2mem_curr_addr = roundup(
				iface_cfg->dump2mem_curr_addr +
				dump_entry->size, ARCH_DMA_MINALIGN);
		break;
#endif /* CONFIG_IPQ_CRASHDUMP_TO_MEMORY */

	case DUMP_TO_TFTP:
		snprintf(runcmd, sizeof(runcmd), "tftpput 0x%x 0x%x %s/%s",
				dump_entry->start_addr, dump_entry->size,
				iface_cfg->tftp_dumpdir, dump_entry->name);
		break;
	}

	if (runcmd[0] != 0) {
		if (run_command(runcmd, 0) != CMD_RET_SUCCESS)
			return CMD_RET_FAILURE;
	}

	return CMD_RET_SUCCESS;
}

/**
 * ipq_do_dump_data() - call dump_to_dst based on the crashdump table
 * &dump_config - crashdump configuration info
 */
void ipq_do_dump_data(crashdump_config_t *dump_config)
{
	int ret = CMD_RET_SUCCESS;
	crashdump_infos_int_t *dump_entry;
	uint16_t dumped = 0;

#ifdef CONFIG_IPQ_CRASHDUMP_TO_MEMORY
	if (dump_config->dump_to == DUMP_TO_MEM) {
		dump_config->iface_cfg.memdump_list =
			malloc(sizeof(memdump_list_info_t) *
					dump_config->actual_nos_dumps);
		if (!dump_config->iface_cfg.memdump_list) {
			printf("failed to alloc mem for memdump_list_info\n");
			return;
		}
	}
#endif /* CONFIG_IPQ_CRASHDUMP_TO_MEMORY */

	list_for_each_entry(dump_entry, &actual_dumps_list, list) {
		printf("Processing %s:\n", dump_entry->name);
		ret = dump_to_dst(dump_config, dump_entry);
		if (ret == CMD_RET_FAILURE)
			break;

		dumped++;
	}
	printf("Dumped %d files!!\n", dumped);

	switch (dump_config->dump_to) {
#ifdef CONFIG_IPQ_CRASHDUMP_TO_USB
	case DUMP_TO_USB:
		mdelay(10);
		usb_stop();
		break;
#endif /* CONFIG_IPQ_CRASHDUMP_TO_USB */

#ifdef CONFIG_IPQ_CRASHDUMP_TO_MEMORY
	case DUMP_TO_MEM:
		crashdump_interface_cfg_t *iface_cfg = &dump_config->iface_cfg;
		memdump_hdr_t *hdr = &iface_cfg->memdump_hdr;

		memcpy((void*)(uintptr_t)iface_cfg->dump2mem_curr_addr,
				(void*)dump_config->iface_cfg.memdump_list,
				sizeof(memdump_list_info_t) *
				dump_config->actual_nos_dumps);
		free(dump_config->iface_cfg.memdump_list);

		hdr->total_dump_sz = iface_cfg->dump2mem_curr_addr +
			(hdr->nos_memdumps * sizeof(memdump_list_info_t)) -
			iface_cfg->dump2mem_rsvd_addr;
		hdr->dump_list_info_offset = iface_cfg->dump2mem_curr_addr -
			iface_cfg->dump2mem_rsvd_addr;

		memcpy((void*)(uintptr_t)iface_cfg->dump2mem_rsvd_addr,
				hdr, sizeof(memdump_hdr_t));
		break;
#endif /* CONFIG_IPQ_CRASHDUMP_TO_MEMORY */
	}

	return;
}

/**
 * ipq_dump_func() - Top level crashdump function used
 */
static void ipq_dump_func(crashdump_config_t *dump_config, uint8_t debug)
{
	uint64_t etime;
	int ret = CMD_RET_SUCCESS;
	bool skip_crashdump = 1;

	dump_config->debug = debug;
	parse_crashdump_config(dump_config);
	if (!dump_config->force_collect_dump) {
		etime = get_timer(0) + (10 * CONFIG_SYS_HZ);
		printf("\nHit any key within 10s to stop dump activity...");
		while (!tstc()) {       /* while no incoming data */
			if (get_timer(0) >= etime) {
				skip_crashdump = 0;
				printf("\n");
				break;
			}
		}
	} else
		skip_crashdump = 0;

	if (skip_crashdump) {
		printf("\nSkipping crashdump ... \n");
		goto reset;
	}

	ret = verify_crashdump_config(dump_config);
	if (ret == CMD_RET_FAILURE)
		goto reset;

	ret = verify_crashdump_iface(dump_config);
	if (ret == CMD_RET_FAILURE)
		goto reset;

	dump_config->dump_infos = board_dumpinfo;
	dump_config->nos_dumps = *board_dump_entries;

	/* Stage 1 - prepare internal dump table */
	prepare_crashdump_table(dump_config);

	/* Stage 2 - dump bins as per the table */
	ipq_do_dump_data(dump_config);

	/* Stage 3 - delete internal dump table */
	delete_crashdump_table();

reset:
	run_command("reset", 0);
	return;
}

/**
 * reset_crashdump() - clear crashdump magic in SDI path
 */
void reset_crashdump(void)
{
	int ret = 0;
	scm_param param;
	unsigned int cookie = ipq_read_tcsr_boot_misc();

	memset(&param, 0, sizeof(scm_param));
	param.type = SCM_SDI_CLEAR;
	param.len = 2;

	/* Disable wdog debug */
	param.buff[0] = 1;
	param.arg_type[0] = SCM_VAL;

	/* SDI Enable */
	param.buff[1] = 0;
	param.arg_type[1] = SCM_VAL;

	ret = ipq_scm_call(&param);
	if (ret) {
		printf("Error in enabling SDI path\n");
		goto retn;
	}

	if (cookie & DLOAD_ENABLE)
		cookie |= CRASHDUMP_RESET;

	cookie &= DLOAD_DISABLE;

	memset(&param, 0, sizeof(scm_param));
	param.type = SCM_IO_WRITE;
	param.len = 2;

	/* Set TCSR_BOOT_MISC reg addr */
	param.buff[0] = (uintptr_t)TCSR_BOOT_MISC_REG;
	param.arg_type[0] = SCM_VAL;

	/* DLOAD clear cookie */
	param.buff[1] = cookie;
	param.arg_type[1] = SCM_VAL;
	ret = ipq_scm_call(&param);
	if (ret)
		printf("Error in reseting the Magic cookie\n");

retn:
	return;
}


int do_crashdump(struct cmd_tbl *cmdtp, int flag, int argc,
			char *const argv[])
{
	if (ipq_iscrashed()) {
		ulong debug = env_get_ulong("debug", 10, 0);
		if ((debug != DBG_DISABLE) && (debug != DBG_CRASHDUMP))
			debug = 0;

		printf("Crashdump magic found, "
				"initializing dump activity..\n");
		ipq_dump_func(&dump_config, debug);
	}

	if (ipq_iscrashed_crashdump_disabled()) {
		printf("Crashdump disabled, resetting the board..\n");
		run_command("reset", 0);
	}

	return 0;
}

U_BOOT_CMD(
	crashdump, 2, 0, do_crashdump,
	"Collect crashdump from ipq if crashed",
	"crashdump <debug>\n"
	);
