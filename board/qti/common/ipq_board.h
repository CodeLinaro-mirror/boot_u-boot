// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2016-2019, The Linux Foundation. All rights reserved.
 *
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _IPQSMEM_H
#define _IPQSMEM_H

#include <linux/types.h>
#include <linux/kernel.h>
#include <asm/byteorder.h>
#include <mtd_node.h>
#include <part.h>
#ifdef CONFIG_TARGET_IPQ9574
#include "../ipq9574/ipq9574.h"
#endif
#ifdef CONFIG_TARGET_IPQ5332
#include "../ipq5332/ipq5332.h"
#endif
#ifdef CONFIG_TARGET_DEVSOC
#include "../devsoc/devsoc.h"
#endif

#define __str_fmt(x)		"%-" #x "s"
#define _str_fmt(x)		__str_fmt(x)
#define smem_ptn_name_fmt	_str_fmt(SMEM_PTN_NAME_MAX)
#define IPQ_ETH_FW_PART_NAME	"0:ETHPHYFW"
#define IPQ_ETH_FW_PART_SIZE	0x80000
#define BOARD_DTS_MAX_NAMELEN	30

#ifdef CONFIG_SMEM_VERSION_C
#define part_which_flash(p)    (((p)->attr & 0xff000000) >> 24)

struct ram_partition_entry
{
	char name[CONFIG_RAM_PART_NAME_LENGTH];
				/**< Partition name, unused for now */
	u64 start_address;	/**< Partition start address in RAM */
	u64 length;		/**< Partition length in RAM in Bytes */
	u32 partition_attribute;/**< Partition attribute */
	u32 partition_category;	/**< Partition category */
	u32 partition_domain;	/**< Partition domain */
	u32 partition_type;	/**< Partition type */
	u32 num_partitions;	/**< Number of partitions on device */
	u32 hw_info;		/**< hw information such as type and freq */
	u8 highest_bank_bit;	/**< Highest bit corresponding to a bank */
	u8 reserve0;		/**< Reserved for future use */
	u8 reserve1;		/**< Reserved for future use */
	u8 reserve2;		/**< Reserved for future use */
	u32 reserved5;		/**< Reserved for future use */
	u64 available_length;	/**< Available Part length in RAM in Bytes */
};

struct usable_ram_partition_table
{
	u32 magic1;		/**< Magic number to identify valid RAM
					partition table */
	u32 magic2;		/**< Magic number to identify valid RAM
					partition table */
	u32 version;		/**< Version number to track structure
					definition changes and maintain
					backward compatibilities */
	u32 reserved1;		/**< Reserved for future use */

	u32 num_partitions;	/**< Number of RAM partition table entries */

	u32 reserved2;		/** < Added for 8 bytes alignment of header */

	/** RAM partition table entries */
	struct ram_partition_entry ram_part_entry[CONFIG_RAM_NUM_PART_ENTRIES];
};
#endif

struct smem_ram_ptn {
	char name[16];
	unsigned long long start;
	unsigned long long size;

	/* RAM Partition attribute: READ_ONLY, READWRITE etc.  */
	unsigned attr;

	/* RAM Partition category: EBI0, EBI1, IRAM, IMEM */
	unsigned category;

	/* RAM Partition domain: APPS, MODEM, APPS & MODEM (SHARED) etc. */
	unsigned domain;

	/* RAM Partition type: system, bootloader, appsboot, apps etc. */
	unsigned type;

	/* reserved for future expansion without changing version number */
	unsigned reserved2, reserved3, reserved4, reserved5;
} __attribute__ ((__packed__));

struct smem_ram_ptable {
#define _SMEM_RAM_PTABLE_MAGIC_1	0x9DA5E0A8
#define _SMEM_RAM_PTABLE_MAGIC_2	0xAF9EC4E2
	unsigned magic[2];
	unsigned version;
	unsigned reserved1;
	unsigned len;
	unsigned buf;
	struct smem_ram_ptn parts[32];
} __attribute__ ((__packed__));

/*
 * function declaration
 */
int smem_getpart(char *part_name, uint32_t *start, uint32_t *size);
int smem_ram_ptable_init(struct smem_ram_ptable *smem_ram_ptable);
int smem_ram_ptable_init_v2(
		struct usable_ram_partition_table *usable_ram_partition_table);


#define RAM_PARTITION_SDRAM 		14
#define RAM_PARTITION_SYS_MEMORY 	1
#define IPQ_NAND_ROOTFS_SIZE 		(64 << 20)

#define SOCINFO_VERSION_MAJOR(ver) 	((ver & 0xffff0000) >> 16)
#define SOCINFO_VERSION_MINOR(ver) 	(ver & 0x0000ffff)

#define INDEX_LENGTH			2
#define SEP1_LENGTH			1
#define VERSION_STRING_LENGTH		72
#define VARIANT_STRING_LENGTH		20
#define SEP2_LENGTH			1
#define OEM_VERSION_STRING_LENGTH	32
#define BUILD_ID_LEN			32

#define SMEM_PTN_NAME_MAX     		16
#define SMEM_PTABLE_PARTS_MAX 		32
#define SMEM_PTABLE_PARTS_DEFAULT 	16

/*
 * Board type	- 0xFFFFFFFF
 * BIT(0 - 7)	- Flash type
 * BIT(8)	- Secure board
 * BIT(9)	- ATF_SUPPORT
 * BIT(10)	- Kernel Authentication Status
 * BIT(11)	- Rootfs Authentication Status
 * BIT(12 - 31)	- Reserved
 */


#define SECURE_BOARD			BIT(8)
#define ATF_ENABLED			BIT(9)
#define KERNEL_AUTH_SUCCESS		BIT(10)
#define ROOTFS_AUTH_SUCCESS		BIT(11)
#define FLASH_TYPE_MASK			0xFF

enum {
	SMEM_BOOT_NO_FLASH        = 0,
	SMEM_BOOT_NOR_FLASH       = 1,
	SMEM_BOOT_NAND_FLASH      = 2,
	SMEM_BOOT_ONENAND_FLASH   = 3,
	SMEM_BOOT_SDC_FLASH       = 4,
	SMEM_BOOT_MMC_FLASH       = 5,
	SMEM_BOOT_SPI_FLASH       = 6,
	SMEM_BOOT_NORPLUSNAND     = 7,
	SMEM_BOOT_NORPLUSEMMC     = 8,
	SMEM_BOOT_QSPI_NAND_FLASH  = 11,
};

struct version_entry
{
	char index[INDEX_LENGTH];
	char colon_sep1[SEP1_LENGTH];
	char version_string[VERSION_STRING_LENGTH];
	char variant_string[VARIANT_STRING_LENGTH];
	char colon_sep2[SEP2_LENGTH];
	char oem_version_string[OEM_VERSION_STRING_LENGTH];
} __attribute__ ((__packed__));

typedef struct smem_pmic_type
{
	unsigned pmic_model;
	unsigned pmic_die_revision;
}pmic_type;

typedef struct ipq_platform_v1 {
	unsigned format;
	unsigned id;
	unsigned version;
	char     build_id[BUILD_ID_LEN];
	unsigned raw_id;
	unsigned raw_version;
	unsigned hw_platform;
	unsigned platform_version;
	unsigned accessory_chip;
	unsigned hw_platform_subtype;
}ipq_platform_v1;

typedef struct ipq_platform_v2 {
	ipq_platform_v1 v1;
	pmic_type pmic_info[3];
	unsigned foundry_id;
}ipq_platform_v2;

typedef struct ipq_platform_v3 {
	ipq_platform_v2 v2;
	unsigned chip_serial;
} ipq_platform_v3;

union ipq_platform {
	ipq_platform_v1 v1;
	ipq_platform_v2 v2;
	ipq_platform_v3 v3;
};

struct smem_machid_info {
	unsigned format;
	unsigned machid;
};

typedef struct soc_info {
	uint32_t cpu_type;
	uint32_t version;
	uint32_t soc_version_major;
	uint32_t soc_version_minor;
	unsigned int machid;
} socinfo_t;

typedef struct {
	loff_t offset;
	loff_t size;
} ipq_part_entry_t;

struct per_part_info
{
	char name[CONFIG_RAM_PART_NAME_LENGTH];
	uint32_t primaryboot;
};

typedef struct
{
#define _SMEM_DUAL_BOOTINFO_MAGIC_START		0xA3A2A1A0
#define _SMEM_DUAL_BOOTINFO_MAGIC_END		0xB3B2B1B0

	/* Magic number for identification when reading from flash */
	uint32_t magic_start;
	/* upgradeinprogress indicates to attempting the upgrade */
	uint32_t    age;
	/* numaltpart indicate number of alt partitions */
	uint32_t    numaltpart;

	struct per_part_info per_part_entry[CONFIG_NUM_ALT_PARTITION];

	uint32_t magic_end;

} ipq_smem_bootconfig_info_t;

typedef struct {
	uint32_t		flash_type;
	uint32_t		flash_index;
	uint32_t		flash_chip_select;
	uint32_t		flash_block_size;
	uint32_t		flash_density;
	uint32_t		flash_secondary_type;
	uint32_t		primary_mibib;
	ipq_part_entry_t	hlos;
	ipq_part_entry_t	rootfs;
	ipq_part_entry_t	dtb;
	ipq_smem_bootconfig_info_t *ipq_smem_bootconfig_info;
} ipq_smem_flash_info_t;

struct smem_ptn {
	char name[SMEM_PTN_NAME_MAX];
	unsigned start;
	unsigned size;
	unsigned attr;
} __attribute__ ((__packed__));

struct smem_ptable {
#define _SMEM_PTABLE_MAGIC_1 0x55ee73aa
#define _SMEM_PTABLE_MAGIC_2 0xe35ebddb
	unsigned magic[2];
	unsigned version;
	unsigned len;
	struct smem_ptn parts[SMEM_PTABLE_PARTS_MAX];
} __attribute__ ((__packed__));

typedef struct {
        unsigned int image_type;
        unsigned int header_vsn_num;
        unsigned int image_src;
        unsigned int image_dest_ptr;
        unsigned int image_size;
        unsigned int code_size;
        unsigned int signature_ptr;
        unsigned int signature_size;
        unsigned int cert_chain_ptr;
        unsigned int cert_chain_size;
} mbn_header_t;

typedef struct auth_cmd_buf {
	unsigned long type;
	unsigned long size;
	unsigned long addr;
} auth_cmd_buf;

/*
 * NAND Flash Configs
 */
#ifdef CONFIG_QSPI_LAYOUT_SWITCH
#define QTI_NAND_LAYOUT_SBL			0
#define QTI_NAND_LAYOUT_LINUX			1
#define QTI_NAND_LAYOUT_MAX			2

int qti_nand_get_curr_layout(void);
#endif

/*
 * Extern variables
 */
extern struct node_info * fnodes;
extern int * fnode_entires;

#ifdef CONFIG_DTB_RESELECT
struct machid_dts_map
{
    int machid;
    char* dts;
};

extern struct machid_dts_map * machid_dts_info;
extern int * machid_dts_entries;
#endif /* CONFIG_DTB_RESELECT */

typedef struct {
	phys_addr_t start;
	phys_size_t size;
} dram_bank_info_t;

extern dram_bank_info_t * board_dram_bank_info;

enum debug_component {
	DBG_DISABLE = 0,
	DBG_CRASHDUMP,
};

#define DUMP_NAME_STR_MAX_LEN			20

enum {
	FULLDUMP= 0,
	MINIDUMP,
	MINIDUMP_AND_FULLDUMP,
};

enum {
	DUMP_TO_TFTP = 0,
	DUMP_TO_USB,
	DUMP_TO_MEM,
	DUMP_TO_FLASH,
};

typedef struct {
	char name[DUMP_NAME_STR_MAX_LEN];/* dump name */
	uint32_t start_addr;		/* dump start addr */
	uint32_t size;			/* dump size
					   0xBAD0FF5E - get ram_size runtime,
					   otherwise specify size */
	uint8_t dump_level;		/* dump level
					   refer crashdump_level_t */
	uint32_t split_bin_sz;		/* split bin size
					   if non-zero means, if size is
					   greater than split_bin_sz, it will
					   dump entire region as seperate bin
					   of size split_bin_sz */
	uint8_t is_aligned_access:1;	/* If this flag is set,
					   'start' is considered a unaligned
					   address, so content will be copied
					   to a aligned one and gets dumped */
	uint8_t compression_support:1;	/* does this binary need to be
					   compressed ? non-zero means true. */
	uint8_t dumptoflash_support:1;	/* does this binary need to be
					   dumped in flash ? non-zero
					   means true. */
} crashdump_infos_t;

extern crashdump_infos_t *board_dumpinfo;
extern uint8_t *board_dump_entries;

/*
 * QCN9224 fusing
 */
#define QCN_VENDOR_ID					0x17CB
#define QCN9224_DEVICE_ID				0x1109
#define QCN9000_DEVICE_ID				0x1104
#define MAX_UNWINDOWED_ADDRESS				0x80000
#define WINDOW_ENABLE_BIT				0x40000000
#define WINDOW_SHIFT					19
#define WINDOW_VALUE_MASK				0x3F
#define WINDOW_START					MAX_UNWINDOWED_ADDRESS
#define WINDOW_RANGE_MASK				0x7FFFF

#define QCN9224_PCIE_REMAP_BAR_CTRL_OFFSET		0x310C
#define PCIE_SOC_GLOBAL_RESET_ADDRESS			0x3008
#define QCN9224_TCSR_SOC_HW_VERSION			0x1B00000
#define QCN9224_TCSR_SOC_HW_VERSION_MASK		GENMASK(11,8)
#define QCN9224_TCSR_SOC_HW_VERSION_SHIFT		8
#define PCIE_SOC_GLOBAL_RESET_VALUE			0x5
#define MAX_SOC_GLOBAL_RESET_WAIT_CNT			50 /* x 20msec */

#define QCN9224_TCSR_PBL_LOGGING_REG			0x01B00094
#define QCN9224_SECURE_BOOT0_AUTH_EN			0x01e24010
#define QCN9224_OEM_MODEL_ID				0x01e24018
#define QCN9224_ANTI_ROLL_BACK_FEATURE			0x01e2401c
#define QCN9224_OEM_PK_HASH				0x01e24060
#define QCN9224_SECURE_BOOT0_AUTH_EN_MASK		(0x1)
#define QCN9224_OEM_ID_MASK				GENMASK(31,16)
#define QCN9224_OEM_ID_SHIFT				16
#define QCN9224_MODEL_ID_MASK				GENMASK(15,0)
#define QCN9224_ANTI_ROLL_BACK_FEATURE_EN_MASK		BIT(9)
#define QCN9224_ANTI_ROLL_BACK_FEATURE_EN_SHIFT		9
#define QCN9224_TOTAL_ROT_NUM_MASK			GENMASK(13,12)
#define QCN9224_TOTAL_ROT_NUM_SHIFT			12
#define QCN9224_ROT_REVOCATION_MASK			GENMASK(17,14)
#define QCN9224_ROT_REVOCATION_SHIFT			14
#define QCN9224_ROT_ACTIVATION_MASK			GENMASK(21,18)
#define QCN9224_ROT_ACTIVATION_SHIFT			18
#define QCN9224_OEM_PK_HASH_SIZE			36
#define QCN9224_JTAG_ID					0x01e22b3c
#define QCN9224_SERIAL_NUM				0x01e22b40
#define QCN9224_PART_TYPE_EXTERNAL			0x01f94090
#define QCN9224_PART_TYPE_EXTERNAL_MASK			BIT(3)
#define QCN9224_PART_TYPE_EXTERNAL_SHIFT		3

#define MHICTRL						(0x38)
#define BHI_STATUS					(0x12C)
#define BHI_IMGADDR_LOW					(0x108)
#define BHI_IMGADDR_HIGH				(0x10C)
#define BHI_IMGSIZE					(0x110)
#define BHI_ERRCODE					(0x130)
#define BHI_ERRDBG1					(0x134)
#define BHI_ERRDBG2					(0x138)
#define BHI_ERRDBG3					(0x13C)
#define BHI_IMGTXDB					(0x118)
#define BHI_EXECENV					(0x128)

#define MHICTRL_RESET_MASK				(0x2)
#define BHI_STATUS_MASK					(0xC0000000)
#define BHI_STATUS_SHIFT				(30)
#define BHI_STATUS_SUCCESS				(2)

#define NO_MASK						(0xFFFFFFFF)

/*
 * Function declaration
 */
unsigned int get_which_flash_param(char *part_name);
int get_current_board_flash_config(void);
ipq_smem_flash_info_t * get_ipq_smem_flash_info(void);
socinfo_t * get_socinfo(void);
uint32_t get_part_block_size(struct smem_ptn *p, ipq_smem_flash_info_t *sfi);
struct smem_ptable * get_ipq_part_table_info(void);
int getpart_offset_size(char *part_name, uint32_t *offset, uint32_t *size);
int smem_getpart_from_offset(uint32_t offset, uint32_t *start, uint32_t *size);
unsigned int get_rootfs_active_partition(void);
int mibib_ptable_init(unsigned int* addr);
void get_kernel_fs_part_details(void);
#ifdef CONFIG_MMC
int part_get_info_efi_by_name(const char *name, struct disk_partition *info);
#endif
#ifdef CONFIG_CMD_UBI
int init_ubi_part(void);
#endif
void fdt_fixup_flash(void *blob);
void reset_crashdump(void);
long long ubi_get_volume_size(char *volume);
int is_atf_enbled(void);
int is_secure_boot(void);
uint8_t * get_boot_mode(void);
#if CONFIG_IS_ENABLED(NAND_QTI)
void board_nand_init(void);
#endif
int get_partition_data(char *part_name, uint32_t offset, uint8_t* buf,
			size_t size);
#endif
