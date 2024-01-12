/*
 * Copyright (c) 2018 The Linux Foundation. All rights reserved.
 *
 * Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <common.h>
#include <command.h>
#include <malloc.h>
#include <memalign.h>
#include <mach/ipq_scm.h>
#include <cpu_func.h>
#include <linux/bug.h>
#include <linux/arm-smccc.h>
#include <dm.h>
#ifdef CONFIG_IPQ_QCN9224_FUSING
#include <init.h>
#include <pci.h>
#include <dt-bindings/pci/pci.h>
#include <asm/io.h>
#include <linux/iopoll.h>
#endif
#include <serial.h>

#include "ipq_board.h"

#ifdef CONFIG_IPQ_SMP_CMD_SUPPORT
#include <cli.h>
#include <console.h>

DECLARE_GLOBAL_DATA_PTR;

#define SECONDARY_CORE_STACKSZ	(8 * 1024)
#define CPU_POWER_DOWN		(1 << 16)

struct cpu_entry_arg {
	void *stack_ptr;
	volatile void *gd_ptr;
	void *arg_ptr;
	int  cpu_up;
	int cmd_complete;
	int cmd_result;
	void *stack_top_ptr;
};

extern void secondary_cpu_init(void);
extern void *global_core_array;

struct cpu_entry_arg core[CFG_NR_CPUS - 1];

#endif /* CONFIG_IPQ_SMP_CMD_SUPPORT */

#define PRINT_BUF_LEN		0x400
#define MDT_SIZE		0x1B88
/* Region for loading test application */
#define TZT_LOAD_ADDR		0x49600000
/* Reserved size for application */
#define TZT_LOAD_SIZE		0x00200000

#define XPU_TEST_ID		0x80100004

static int tzt_loaded;

struct udevice *dev;

struct xpu_tzt {
	uint64_t test_id;
	uint64_t num_param;
	uint64_t param1;
	uint64_t param2;
	uint64_t param3;
	uint64_t param4;
	uint64_t param5;
	uint64_t param6;
	uint64_t param7;
	uint64_t param8;
	uint64_t param9;
	uint64_t param10;
};

struct resp {
	uint64_t status;
	uint64_t index;
	uint64_t total_tests;
};

struct log_buff {
	uint16_t wrap;
	uint16_t log_pos;
	char buffer[PRINT_BUF_LEN];
};

#ifdef CONFIG_IPQ_QCN9224_FUSING
struct jtag_ids {
        u32 id;
        char *name;
};

struct jtag_ids qcn9224_jtag_ids[] = {
        { 0x101D50E1, "QCN9274" },
        { 0x101D80E1, "QCN9272" },
        { 0x101ED0E1, "QCN6214" },
        { 0x101EE0E1, "QCN6224" },
        { 0x101EF0E1, "QCN6274" },
};

enum {
	PCI_LIST_QCN9224_FUSE = 0,
	PCI_FUSE_QCN9224,
	PCI_DETECT_QCN9224,
	PCI_LIST
};
#endif /* CONFIG_IPQ_QCN9224_FUSING */

#define PRI_PARTITION	1
#define ALT_PARTITION	2

#define FUSEPROV_SUCCESS		0x0
#define FUSEPROV_INVALID_HASH		0x09
#define FUSEPROV_SECDAT_LOCK_BLOWN	0xB
#define MAX_FUSE_ADDR_SIZE		0x8

static int do_secure(struct cmd_tbl *cmdtp, int flag,
				int argc, char *const argv[])
{
	int ret = CMD_RET_FAILURE;
#ifdef CONFIG_VERSION_ROLLBACK_PARTITION_INFO
	int active_part = PRI_PARTITION;
#endif /* CONFIG_VERSION_ROLLBACK_PARTITION_INFO */
	uint8_t *buff = NULL;
	auth_cmd_buf auth_buf;

	scm_param param;

	if(argc!=4 && argc !=1)
		return CMD_RET_USAGE;

	if (strncmp(argv[0], "is_sec_boot_enabled", 19) == 0 && argc == 1) {

		buff = (uint8_t *)malloc_cache_aligned(CONFIG_SYS_CACHELINE_SIZE);
		if(!buff) {
			printf("Unable allocate memory\n");
			ret = CMD_RET_FAILURE;
			goto exit;
		}

		memset(buff, 0, CONFIG_SYS_CACHELINE_SIZE);

		memset(&param, 0, sizeof(scm_param));

		param.type = SCM_CHECK_SECURE_FUSE;
		/*Buffer to read fuse status*/
		param.buff[0] = (uintptr_t)buff;
		param.arg_type[0] = SCM_READ_OP;

		/*Buffer size*/
		param.buff[1] = sizeof(uint8_t);
		param.arg_type[1] = SCM_VAL;

		param.len = 2;

		ret = ipq_scm_call(&param);

		/* invalidate cache to update latest value in buff */
		invalidate_dcache_range((unsigned long)buff,
					(unsigned long)buff +
					CONFIG_SYS_CACHELINE_SIZE);

		if(!ret) {
			printf("secure boot fuse is%senabled\n",
					1 == *(uint8_t *)buff? " ": " not ");
			ret = CMD_RET_SUCCESS;
		} else {
			printf("secure cmd: scm call failed. ret = %d\n", ret);
			ret = CMD_RET_FAILURE;
		}

		if(buff)
			free(buff);
	} else if(strncmp(argv[0], "secure_authenticate", 19) == 0 && argc == 4) {

		memset(&param, 0, sizeof(scm_param));

		param.type = SCM_CHECK_AUTHENTICATE_SUPPORT;
		param.buff[0] = SCM_SMC_FNID(QCOM_SCM_SVC_BOOT,
					QCOM_SCM_SEC_AUTH_CMD) |
					(ARM_SMCCC_OWNER_SIP <<
					ARM_SMCCC_OWNER_SHIFT);
		param.arg_type[0] = SCM_VAL;

		param.len = 1;
		param.get_ret = 1;

		ret = ipq_scm_call(&param);

		if (ret || (!ret && le32_to_cpu(param.res.result[0]) <= 0)) {
			printf("secure authentication scm call"
				" is not supported. ret = %d\n", ret);
			ret = CMD_RET_SUCCESS;
			goto exit;
		}

		auth_buf.type = simple_strtoul(argv[1], NULL, 16);
		auth_buf.addr = simple_strtoul(argv[2], NULL, 16);
		auth_buf.size = simple_strtoul(argv[3], NULL, 16);
#ifdef CONFIG_VERSION_ROLLBACK_PARTITION_INFO
		active_part = get_rootfs_active_partition();
		active_part = active_part ? ALT_PARTITION : PRI_PARTITION;

		memset(&param, 0, sizeof(scm_param));

		param.type = SCM_SET_ACTIVE_PART;

		/*pass current avtive partition */
		param.buff[0] = active_part;
		param.arg_type[0] = SCM_VAL;

		param.len = 1;

		ret = ipq_scm_call(&param);

		if(ret) {
			printf("Partition info authentication failed\n");
			BUG(); //:TODO check if BUG is necessary
		}

#endif /* CONFIG_VERSION_ROLLBACK_PARTITION_INFO */
		memset(&param, 0, sizeof(scm_param));

		param.type = SCM_SECURE_AUTH;

		/* args[0] has the image SW ID*/
		param.buff[0] = auth_buf.type;
		param.arg_type[0] = SCM_VAL;

		/* args[1] has the image size */
		param.buff[1] = auth_buf.size;
		param.arg_type[1] = SCM_VAL;

		/* args[2] has the load address*/
		param.buff[2] = auth_buf.addr;
		param.arg_type[2] = SCM_WRITE_OP;

		param.len = 3;
		param.get_ret = 1;

		ret = ipq_scm_call(&param);

		if(ret || (param.res.result[0] && !ret)) {
			printf("image authentication failed. ret  = %d\n",
									ret);
			ret = CMD_RET_FAILURE;
		} else {
			printf("image authentication success\n");
			ret = CMD_RET_SUCCESS;
		}
	} else {
		return CMD_RET_USAGE;
	}

exit:
	return ret;
}

U_BOOT_CMD(is_sec_boot_enabled, 1, 0, do_secure,
		"check secure boot fuse is enabled or not\n",
		"is_sec_boot_enabled - check secure boot fuse "
		"is enabled or not\n");

U_BOOT_CMD(secure_authenticate, 4, 0, do_secure,
		"authenticate the signed image\n",
		"secure_authenticate <sw_id> <img_addr> <img_size>\n"
		"	- authenticate the signed image\n");

static int do_fuseipq(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
	int ret;
	scm_param param;
	uint32_t fuse_status = 0;
	uint32_t fuse_address;

	if (argc != 2) {
		printf("No Arguments provided\n");
		printf("Command format: fuseipq <address>\n");
		return 1;
	}

	fuse_address = simple_strtoul(argv[1], NULL, 16);

	memset(&param, 0, sizeof(scm_param));

	param.type = SCM_FUSE_IPQ;

	param.buff[0] = (uint64_t) fuse_address;
	param.arg_type[0] = SCM_READ_OP;
	param.len = 1;
	param.get_ret = 1;

	ret = ipq_scm_call(&param);

	fuse_status = param.res.result[0];

	if (ret || fuse_status)
		printf("%s: Error in QFPROM write (%d, %d)\n",
			__func__, ret, fuse_status);

	if (fuse_status == FUSEPROV_SECDAT_LOCK_BLOWN)
		printf("Fuse already blown\n");
	else if (fuse_status == FUSEPROV_INVALID_HASH)
		printf("Invalid sec.dat\n");
	else if (fuse_status  != FUSEPROV_SUCCESS)
		printf("Failed to Blow fuses");
	else
		printf("Blow Success\n");

	return 0;
}

U_BOOT_CMD(fuseipq, 2, 0, do_fuseipq,
		"fuse QFPROM registers from memory\n",
		"fuseipq [address]  - Load fuse(s) and blows in the qfprom\n");

#ifdef CONFIG_TARGET_IPQ5332
static int do_list_ipq5332_fuse(struct cmd_tbl *cmdtp, int flag, int argc,
					char *const argv[])
{
	int ret;
	int index, next = 0;
	unsigned long addr = 0xA00E8;
	struct fuse_payload {
		u32 fuse_addr;
		u32 lsb_val;
		u32 msb_val;
	};
	struct fuse_payload *fuse = NULL;
	scm_param param;
	size_t size = sizeof(struct fuse_payload ) * MAX_FUSE_ADDR_SIZE;

	size = roundup(size, CONFIG_SYS_CACHELINE_SIZE);

	fuse = malloc_cache_aligned(size);
	if (fuse == NULL) {
		return 1;
	}

	memset(fuse, 0, MAX_FUSE_ADDR_SIZE * sizeof(struct fuse_payload));

	fuse[0].fuse_addr = 0xA00D0;
	for (index = 1; index < MAX_FUSE_ADDR_SIZE; index++) {
		fuse[index].fuse_addr = addr + next;
		next += 0x8;
	}

	memset(&param, 0, sizeof(scm_param));

	param.type = SCM_LIST_FUSE;

	param.buff[0] = (unsigned long)fuse;
	param.arg_type[0] = SCM_WRITE_OP;

	param.buff[1] = sizeof(struct fuse_payload ) * MAX_FUSE_ADDR_SIZE;
	param.arg_type[1] = SCM_VAL;

	param.len = 2;

	/* invalidate cache to update latest value in buff */
	flush_dcache_range((unsigned long)fuse,
				(unsigned long)fuse +
				size);

	ret = ipq_scm_call(&param);
	if (ret) {
		printf("Error (%d) failed to read fuse\n", ret);
	}

	printf("Fuse Name\tAddress\t\tValue\n");
	printf("------------------------------------------------\n");

	printf("TME_AUTH_EN\t0x%08X\t0x%08X\n", fuse[0].fuse_addr,
			fuse[0].lsb_val & 0x41);
	printf("TME_OEM_ID\t0x%08X\t0x%08X\n", fuse[0].fuse_addr,
			fuse[0].lsb_val & 0xFFFF0000);
	printf("TME_PRODUCT_ID\t0x%08X\t0x%08X\n", fuse[0].fuse_addr + 0x4,
			fuse[0].msb_val & 0xFFFF);

	for (index = 1; index < MAX_FUSE_ADDR_SIZE; index++) {
		printf("TME_MRC_HASH\t0x%08X\t0x%08X\n",
				fuse[index].fuse_addr, fuse[index].lsb_val);
		printf("TME_MRC_HASH\t0x%08X\t0x%08X\n",
				fuse[index].fuse_addr + 0x4, fuse[index].msb_val);
	}

	free(fuse);
	return 0;
}

U_BOOT_CMD(list_ipq5332_fuse, 1, 0, do_list_ipq5332_fuse,
		"fuse set of QFPROM registers from memory\n",
		"");
#endif /* CONFIG_TARGET_IPQ5332 */
#ifdef CONFIG_IPQ_QCN9224_FUSING
static struct pci_device_id device_table [] = {
	{QCN_VENDOR_ID, QCN9224_DEVICE_ID},
	{}
};

static int pci_cmd(const char *cmd)
{
	if (strcmp(cmd, "list_qcn9224_fuse") == 0)
		return PCI_LIST_QCN9224_FUSE;
	else if (strcmp(cmd, "fuse_qcn9224") == 0)
		return PCI_FUSE_QCN9224;
	else if (strcmp(cmd, "detect_qcn9224") == 0)
		return PCI_DETECT_QCN9224;
	else
		return PCI_LIST;
}

static void pci_select_window(uintptr_t base, uint32_t offset)
{
	uint32_t window = (offset >> WINDOW_SHIFT) & WINDOW_VALUE_MASK;
	uint32_t prev_window = 0, curr_window = 0, prev_cleared_window = 0;

	prev_window = readl(base + QCN9224_PCIE_REMAP_BAR_CTRL_OFFSET);

	/* Clear out last 6 bits of window register */
	prev_cleared_window = prev_window & ~(0x3f);

	/* Write the new last 6 bits of window register. Only window 1 values
	 * are changed. Window 2 and 3 are unaffected.
	 */
	curr_window = prev_cleared_window | window;

	writel(WINDOW_ENABLE_BIT | curr_window, base +
			QCN9224_PCIE_REMAP_BAR_CTRL_OFFSET);
}

static void print_error_code(pci_addr_t addr, bool pbl_log)
{
	int i;
	u32 val;
	struct {
		char *name;
		u32 offset;
	} error_reg[] = {
		{ "ERROR_CODE", BHI_ERRCODE },
		{ "ERROR_DBG1", BHI_ERRDBG1 },
		{ "ERROR_DBG2", BHI_ERRDBG2 },
		{ "ERROR_DBG3", BHI_ERRDBG3 },
		{ NULL },
	};

	for (i = 0; error_reg[i].name; i++) {
		val = readl(addr + error_reg[i].offset);
		printf("Reg: %s value: 0x%x\n", error_reg[i].name, val);
	}
	if (pbl_log) {
		pci_select_window(addr, QCN9224_TCSR_PBL_LOGGING_REG);
		val = readl(addr + WINDOW_START +
				(QCN9224_TCSR_PBL_LOGGING_REG &
					WINDOW_RANGE_MASK));
		printf("Reg: TCSR_PBL_LOGGING: 0x%x\n", val);
	}
}

static void qcn92xx_global_soc_reset(uintptr_t bar0_base)
{
	u32 val, ret, count = 0;
	uintptr_t reg;

	do {
		reg = bar0_base + PCIE_SOC_GLOBAL_RESET_ADDRESS;
		writel(PCIE_SOC_GLOBAL_RESET_VALUE, reg);

		reg = bar0_base + BHI_EXECENV;
		ret = readl_poll_sleep_timeout(reg, val, val == 0, 1 * 1000,
						20 * 1000);
		if (ret == 0)
			break;
		else
			++count;
	} while (count < MAX_SOC_GLOBAL_RESET_WAIT_CNT);

	if (val != 0)
		printk("SoC global reset failed! Reset count : %d\n",count);
}

static int fuse_qcn9224(const struct pci_device_id *ids, int device_id)
{
	struct udevice *dev;
	ulong vendor, device;
	int version, val, ret = 0;
	uintptr_t bar0_base, reg;
	uint32_t load_addr, file_size;

	ret = pci_find_device_id(ids, device_id, &dev);
	if (ret) {
		printf("Device not found\n");
		return CMD_RET_FAILURE;
	}

	load_addr = env_get_ulong("fileaddr", 16, 0);
	file_size = env_get_ulong("filesize", 16, 0);

	if ((file_size == 0) || (load_addr == 0)) {
		printf("Fuse data not found\n");
		return CMD_RET_FAILURE;
	}

	ret = CMD_RET_FAILURE;

	dm_pci_read_config32(dev, PCI_BASE_ADDRESS_0, (uint32_t *)&bar0_base);
	bar0_base &= 0xFFF00000;

	dm_pci_read_config(dev, PCI_VENDOR_ID, &vendor, PCI_SIZE_16);
	dm_pci_read_config(dev, PCI_DEVICE_ID, &device, PCI_SIZE_16);

	/* Read QCN9224 version */
	pci_select_window(bar0_base, QCN9224_TCSR_SOC_HW_VERSION);

	version = readl(bar0_base + WINDOW_START +
			(QCN9224_TCSR_SOC_HW_VERSION & WINDOW_RANGE_MASK));

	version = (version & QCN9224_TCSR_SOC_HW_VERSION_MASK) >>
					QCN9224_TCSR_SOC_HW_VERSION_SHIFT;

	if (version == 1) {
		printk("Fusing not supported in QCN9224 V1\n");
		return CMD_RET_FAILURE;
	}

	printf("Fusing on Vendor ID:0x%lx device ID:0x%lx devbusfn:0x%x\n",
				vendor, device, dm_pci_get_bdf(dev));
	/*
	 *flush dcache
	 */
	flush_dcache_all();

	writel(0, bar0_base + BHI_STATUS);
	writel(upper_32_bits(load_addr), bar0_base + BHI_IMGADDR_HIGH);
	writel(lower_32_bits(load_addr), bar0_base + BHI_IMGADDR_LOW);
	writel(file_size, bar0_base + BHI_IMGSIZE);
	writel(1, bar0_base + BHI_IMGTXDB);

	printf("Waiting for fuse blower bin download...\n");

	reg = bar0_base + BHI_STATUS;
	ret = readl_poll_sleep_timeout(reg, val,
			((val & BHI_STATUS_MASK) >> BHI_STATUS_SHIFT) ==
				BHI_STATUS_SUCCESS, 250 * 1000, 12500 * 1000);
	if (ret) {
		printf("Fuse blower bin Download failed, "
				"BHI_STATUS 0x%x, ret %d\n", val, ret);
		print_error_code(bar0_base, true);
		ret = CMD_RET_FAILURE;
		goto fail;
	}

	reg = bar0_base + BHI_EXECENV;
	ret = readl_poll_sleep_timeout(reg, val, (val & NO_MASK ) == 1,
					250 * 1000, 12500 * 1000);
	if (ret) {
		printf("EXECENV is not correct, "
				"BHI_EXECENV 0x%x, ret %d\n",val, ret);
		print_error_code(bar0_base, true);
		ret = CMD_RET_FAILURE;
		goto fail;
	}

	printf("Fuse blower bin loaded sucessfully\n");

	reg = bar0_base + BHI_ERRCODE;
	ret = readl_poll_sleep_timeout(reg, val, (val & NO_MASK) == 0xCAFECACE,
					250 * 1000, 12500 * 1000);
	if (ret) {
		printf("Fusing failed, ret %d\n",ret);
		print_error_code(bar0_base, false);
		ret = CMD_RET_FAILURE;
		goto fail;
	}

	printf("Fusing completed sucessfully\n");
	ret = CMD_RET_SUCCESS;

fail:
	/* Target SoC global reset */
	qcn92xx_global_soc_reset(bar0_base);

	mdelay(1000);

	/* Target MHI reset */
	val = readl(bar0_base + MHICTRL);
	writel(val | MHICTRL_RESET_MASK, bar0_base + MHICTRL);
	return ret;
}

static void print_qcn9224_fuse(struct udevice *bus,
				const struct pci_device_id *ids)
{
	struct udevice *dev;
	int val, ret, i = 0;
	uintptr_t bar0_base;

	ret = pci_bus_find_devices(bus, ids, &i, &dev);
	if (ret)
		return;

	dm_pci_read_config32(dev, PCI_BASE_ADDRESS_0, (uint32_t *)&bar0_base);
	bar0_base &= 0xFFF00000;

	printf("Slot id: %d\tPCIe Bus ID: %d\nFuse Name \t\t  Address\t "
		"Value\n", ((dev_seq(bus) - 1) >> 1), dev_seq(bus));
	printf("------------------------------------------------------\n");

	pci_select_window(bar0_base, QCN9224_SECURE_BOOT0_AUTH_EN);

	val = readl(bar0_base + WINDOW_START +
			(QCN9224_SECURE_BOOT0_AUTH_EN & WINDOW_RANGE_MASK));

	printf("SECURE_BOOT0_AUTH_EN\t   0x%x \t 0x%x \n",
		QCN9224_SECURE_BOOT0_AUTH_EN,
			(val & QCN9224_SECURE_BOOT0_AUTH_EN_MASK));

	pci_select_window(bar0_base, QCN9224_OEM_MODEL_ID);

	val = readl(bar0_base + WINDOW_START +
			(QCN9224_OEM_MODEL_ID & WINDOW_RANGE_MASK));

	printf("OEM ID\t\t\t   0x%x \t 0x%lx \n",QCN9224_OEM_MODEL_ID,
			(val & QCN9224_OEM_ID_MASK) >> QCN9224_OEM_ID_SHIFT);
	printf("MODEL ID\t\t   0x%x \t 0x%lx \n",QCN9224_OEM_MODEL_ID,
			(val & QCN9224_MODEL_ID_MASK));

	pci_select_window(bar0_base, QCN9224_ANTI_ROLL_BACK_FEATURE);

	val = readl(bar0_base + WINDOW_START +
			(QCN9224_ANTI_ROLL_BACK_FEATURE & WINDOW_RANGE_MASK));
	printf("ANTI_ROLL_BACK_FEATURE_EN  0x%x \t 0x%lx \n",
			QCN9224_ANTI_ROLL_BACK_FEATURE,
			(val & QCN9224_ANTI_ROLL_BACK_FEATURE_EN_MASK) >>
				QCN9224_ANTI_ROLL_BACK_FEATURE_EN_SHIFT);
	printf("TOTAL_ROT_NUM\t\t   0x%x \t 0x%lx \n",
			QCN9224_ANTI_ROLL_BACK_FEATURE,
			(val & QCN9224_TOTAL_ROT_NUM_MASK) >>
				QCN9224_TOTAL_ROT_NUM_SHIFT);
	printf("ROT_REVOCATION\t\t   0x%x \t 0x%lx \n",
			QCN9224_ANTI_ROLL_BACK_FEATURE,
			(val & QCN9224_ROT_REVOCATION_MASK) >>
				QCN9224_ROT_REVOCATION_SHIFT);
	printf("ROT_ACTIVATION\t\t   0x%x \t 0x%lx \n",
			QCN9224_ANTI_ROLL_BACK_FEATURE,
			(val & QCN9224_ROT_ACTIVATION_MASK) >>
			QCN9224_ROT_ACTIVATION_SHIFT);

	for(i = 0; i <= QCN9224_OEM_PK_HASH_SIZE ; i+=4) {
		pci_select_window(bar0_base, QCN9224_OEM_PK_HASH + i);

		val = readl(bar0_base + WINDOW_START +
				((QCN9224_OEM_PK_HASH + i) &
					WINDOW_RANGE_MASK));

		printf("OEM PK hash \t\t   0x%x \t 0x%x\n",
			QCN9224_OEM_PK_HASH + i, val);
	}

	pci_select_window(bar0_base, QCN9224_JTAG_ID);
	val = readl(bar0_base + WINDOW_START +
			(QCN9224_JTAG_ID & WINDOW_RANGE_MASK));

	for(i = 0; i < ARRAY_SIZE(qcn9224_jtag_ids); i++) {
		if(qcn9224_jtag_ids[i].id == val) {
			printf("JTAG ID\t\t\t   0x%x \t 0x%x(%s)\n",
					QCN9224_JTAG_ID, val,
					qcn9224_jtag_ids[i].name);
			break;
		}
	}

	if(i >= ARRAY_SIZE(qcn9224_jtag_ids))
		printf("JTAG ID\t\t\t   0x%x \t 0x%x\n",
			QCN9224_JTAG_ID, val);

	pci_select_window(bar0_base, QCN9224_SERIAL_NUM);
	val = readl(bar0_base + WINDOW_START +
			(QCN9224_SERIAL_NUM & WINDOW_RANGE_MASK));
	printf("Serial Number\t\t   0x%x \t 0x%x\n",
			QCN9224_SERIAL_NUM, val);

	pci_select_window(bar0_base, QCN9224_PART_TYPE_EXTERNAL);
	val = readl(bar0_base + WINDOW_START +
			(QCN9224_PART_TYPE_EXTERNAL & WINDOW_RANGE_MASK));
	val = (val & QCN9224_PART_TYPE_EXTERNAL_MASK) >>
			QCN9224_PART_TYPE_EXTERNAL_SHIFT;
	printf("Part Type\t\t   0x%x \t 0x%x(%s)\n",
			QCN9224_PART_TYPE_EXTERNAL, val, val?"EXT":"INT");

	printf("------------------------------------------------------\n\n");
}

static void detect_qcn9224(struct udevice *bus,
				const struct pci_device_id *ids)
{
	int ret;
	struct udevice *dev;
	int qcn9224_version, index = 0;
	uintptr_t bar0_base;

	ret = pci_bus_find_devices(bus, ids, &index, &dev);
	if (ret)
		return;

	dm_pci_read_config32(dev, PCI_BASE_ADDRESS_0, (uint32_t *)&bar0_base);
	bar0_base &= 0xFFF00000;

	/* Read QCN9224 version */
	pci_select_window(bar0_base, QCN9224_TCSR_SOC_HW_VERSION);

	qcn9224_version = readl(bar0_base + WINDOW_START +
				(QCN9224_TCSR_SOC_HW_VERSION &
					WINDOW_RANGE_MASK));

	qcn9224_version = (qcn9224_version &
				QCN9224_TCSR_SOC_HW_VERSION_MASK) >>
					QCN9224_TCSR_SOC_HW_VERSION_SHIFT;

	env_set_ulong("qcn9224_version",(unsigned long)qcn9224_version);
}

static void list_pci_device(struct udevice *bus)
{
	struct udevice *dev;
	ulong vendor, device;
	uint32_t bar0_base;

	for (device_find_first_child(bus, &dev);
		dev;
		device_find_next_child(&dev)) {

		dm_pci_read_config(dev, PCI_VENDOR_ID, &vendor, PCI_SIZE_16);
		dm_pci_read_config(dev, PCI_DEVICE_ID, &device, PCI_SIZE_16);
		dm_pci_read_config32(dev->parent, PCI_BASE_ADDRESS_0,
					&bar0_base);

		printf("\t   %d  \t\t    %d    \t\t0x%x        \t0x%lx\n",
				((dev_seq(bus) - 1) >> 1),
				dev_seq(bus),
				bar0_base & 0xFF000000,
				PCI_VENDEV(vendor,device));
	}
}

static int do_pci_cmd(struct cmd_tbl *cmdtp, int flag, int argc,
                                          char *const argv[])
{
	struct udevice *bus;
	int busnum, cmd, device_id, ret = CMD_RET_SUCCESS;

	/*
	 * Init Pci
	 * disable console to avoid pci init logs
	 */
	gd->have_console = 0;
	pci_init();
	gd->have_console = 1;

	cmd = pci_cmd(argv[0]);

	switch (cmd) {
	case PCI_FUSE_QCN9224:
		if (argc != 2) {
			ret = CMD_RET_USAGE;
			goto fail;
		} else {
			device_id = simple_strtoul(argv[1], NULL, 16);
			if (device_id > CONFIG_IPQ_MAX_PCIE) {
				printf("Supported PCIe instances 0 to %d\n",
					CONFIG_IPQ_MAX_PCIE - 1);
				ret = CMD_RET_USAGE;
				goto fail;
			}
		}
		ret = fuse_qcn9224(device_table, device_id);
		break;
	case PCI_LIST:
		printf("\t Slotid\t\tBus Number\t\tBase Address\t\t"
			"Device ID \n");
	case PCI_LIST_QCN9224_FUSE:
	case PCI_DETECT_QCN9224:
		for (busnum = 0; busnum < CONFIG_IPQ_MAX_PCIE * 2; ++busnum) {
			/*
			 * avoid unwannted error logs, so disabling console
			 */
			gd->have_console = 0;

			if (uclass_get_device_by_seq(UCLASS_PCI,busnum, &bus))
				continue;

			if (!device_is_on_pci_bus(bus))
				continue;

			gd->have_console = 1;

			if (cmd == PCI_LIST)
				list_pci_device(bus);
			else if (cmd == PCI_DETECT_QCN9224)
				detect_qcn9224(bus, device_table);
			else
				print_qcn9224_fuse(bus, device_table);
		}
		break;
	default:
		;
	}
fail:
	gd->have_console = 1;

	return ret;
}

U_BOOT_CMD(list_pci, 1, 1, do_pci_cmd,
	   "Print the RC's PCIe details and attached device ID",
	   "If no attach is present, then nothing will be printed");

U_BOOT_CMD(list_qcn9224_fuse, 1, 1, do_pci_cmd,
	   "Print QCN9224 fuse details from attached PCIe slots",
	   "If there is no QCN9224 attach, then nothing will be printed");

U_BOOT_CMD(detect_qcn9224, 1, 1, do_pci_cmd,
	   "Detect qcn9224 version and populate it on qcn9224_version Env",
	   "qcn9224_version will be zero if not attached else one / two");

U_BOOT_CMD(fuse_qcn9224, 2, 1, do_pci_cmd,
	   "Fuse QCN9224 V2 fuses and argument is PCIe device ID",
	   "If not QCN9224 V2, then fuse blow will be skipped");
#endif /* CONFIG_IPQ_QCN9224_FUSING */

static int run_xpu_config_test(void)
{
	struct resp resp_buf __aligned(CONFIG_SYS_CACHELINE_SIZE);
	uint32_t passed = 0, failed = 0;
	int ret = CMD_RET_FAILURE;
	struct log_buff logbuff;
	struct xpu_tzt xputzt;
	scm_param param;
	int i = 0;

	memset(&xputzt, 0, sizeof(struct xpu_tzt));
	memset(&logbuff, 0, sizeof(struct log_buff));
	memset(&resp_buf, 0, sizeof(struct resp));
	xputzt.test_id = XPU_TEST_ID;
	xputzt.num_param = 0x3;
	xputzt.param3 = (uintptr_t)&resp_buf;

	printf("****** xPU Configuration Validation Test Begin ******\n");

	do {
		memset(&param, 0, sizeof(scm_param));
		param.type = SCM_XPU_LOG_BUFFER;
		/* Log Buffer */
		param.buff[0] = (uintptr_t)&logbuff;
		param.arg_type[0] = SCM_WRITE_OP;

		/* Log Buffer size*/
		param.buff[1] = PRINT_BUF_LEN;
		param.len = 2;

		ret = ipq_scm_call(&param);
		if (ret) {
			printf("\nipq_scm_call: SCM_XPU_LOG_BUFFER"
						" failed, ret : %d\n", ret);
			goto fail;
		}

		memset(&param, 0, sizeof(scm_param));
		param.type = SCM_XPU_SEC_TEST_1;

		xputzt.param2 = i++;
		param.buff[0] = (uintptr_t)&xputzt;
		param.arg_type[0] = SCM_WRITE_OP;

		param.buff[1] = sizeof(struct xpu_tzt);
		param.len = 2;

		ret = ipq_scm_call(&param);
		if (ret) {
			printf("\nipq_scm_call: SCM_SEC_TEST_1"
					" failed, ret : %d\n", ret);
			goto fail;
		}

		invalidate_dcache_range((unsigned long)&resp_buf,
					(unsigned long)&resp_buf +
					CONFIG_SYS_CACHELINE_SIZE);
		if (resp_buf.status == 0)
			passed++;
		else if (resp_buf.status == 1)
			failed++;

		logbuff.buffer[logbuff.log_pos] = '\0';
		printf("%s", logbuff.buffer);

	} while(i < resp_buf.total_tests);

	printf("******************************************************\n");
	printf("Test Result: Passed %u Failed %u (total %u)\n",
	       passed, failed, (uint32_t)resp_buf.total_tests);
	printf("****** xPU Configuration Validation Test End ******\n");

fail:
	return ret;
}

static int do_tzt(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
	uint32_t img_addr;
	uint32_t img_size;
	int ret = 0;
	scm_param param;

	/* at least two arguments should be there */
	if (argc < 2) {
		ret = CMD_RET_USAGE;
		goto fail;
	}

	if (strncmp(argv[1], "load", sizeof("load")) == 0) {
		if (argc < 4) {
			ret = CMD_RET_USAGE;
			goto fail;
		}

		memset(&param, 0, sizeof(scm_param));
		param.type = SCM_TZT_REGION_NOTIFICATION;
		/* TZT Load Address */
		param.buff[0] = TZT_LOAD_ADDR;
		param.arg_type[0] = SCM_WRITE_OP;

		/* TZT Load Size */
		param.buff[1] = TZT_LOAD_SIZE;
		param.len = 2;

		ret = ipq_scm_call(&param);
		if (ret) {
			printf("\nipq_scm_call: SCM_TZT_REGION_NOTIFICATION"
					" failed, ret : %d\n", ret);
			ret = CMD_RET_FAILURE;
			goto fail;
		}

		img_addr = simple_strtoul(argv[2], NULL, 16);
		img_size = simple_strtoul(argv[3], NULL, 16);

		memset(&param, 0, sizeof(scm_param));
		param.type = SCM_TZT_TESTEXEC_IMG;
		param.buff[0] = MDT_SIZE;
		param.buff[1] = img_size - MDT_SIZE;
		param.buff[2] = img_addr;
		param.len = 3;

		ret = ipq_scm_call(&param);
		if (ret) {
			printf("\nipq_scm_call: SCM_TZT_TESTEXEC_IMG"
					" failed, ret : %d\n", ret);
			ret = CMD_RET_FAILURE;
			goto fail;
		}

		tzt_loaded = 1;
		return 0;
	}

	if (!tzt_loaded) {
		printf("load tzt image before running test cases\n");
		ret = CMD_RET_FAILURE;
		goto fail;
	}

	if (strncmp(argv[1], "xpu", sizeof("xpu")) == 0)
		ret = run_xpu_config_test();
fail:
	return ret;
}

U_BOOT_CMD(tzt, 4, 0, do_tzt,
	   "load and run tzt\n",
	   "tzt load address size - To load tzt image\n"
	   "tzt xpu - To run xpu config test\n");

#if defined(CONFIG_DPR_VER_1_0) || defined(CONFIG_DPR_VER_2_0)
int do_dpr(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
	int ret = CMD_RET_USAGE, i;
	unsigned long loadaddr, filesize;
	unsigned long default_hex_val = 0xFFFFFFFF;
	uint32_t dpr_status = 0;
	scm_param param;

	memset(&param, 0, sizeof(scm_param));
	if (argc > cmdtp->maxargs || (cmdtp->maxargs == 3 && argc == 2))
		goto fail;

	if (argc == cmdtp->maxargs)
		for(i = 0; i < cmdtp->maxargs - 1; i++)
			param.buff[i] = simple_strtoul(argv[i + 1], NULL, 16);
	else {
		loadaddr = env_get_hex("fileaddr", default_hex_val);
		if (loadaddr == default_hex_val)
			goto fail;

		param.buff[0] = loadaddr;

		if (cmdtp->maxargs == 3) {
			filesize = env_get_hex("filesize", default_hex_val);
			if (filesize == default_hex_val)
				goto fail;

			param.buff[1] = filesize;
		}
	}

	param.type = SCM_TME_DPR_PROCESSING;
	param.len = cmdtp->maxargs - 1;
	param.get_ret = 1;

	ret = ipq_scm_call(&param);
	dpr_status = param.res.result[0];
	if (ret || dpr_status) {
		printf("Error in DPR Processing ret : %d, dpr_status : %d\n",
			ret, dpr_status);
	} else
		printf("DPR Process Successful\n");

fail:
	return ret;
}

#ifdef CONFIG_DPR_VER_1_0
U_BOOT_CMD(dpr_execute, 2, 0, do_dpr,
                "Debug Policy Request processing\n",
                "dpr_execute [address] - Processing dpr\n");
#endif /* CONFIG_DPR_VER_1_0 */

#ifdef CONFIG_DPR_VER_2_0
U_BOOT_CMD(dpr_execute, 3, 0, do_dpr,
                "Debug Policy Request processing\n",
                "dpr_execute [fileaddr] [filesize] - Processing dpr\n");
#endif /* CONFIG_DPR_VER_2_0 */
#endif /* CONFIG_DPR_VER_1_0 or CONFIG_DPR_VER_2_0 */

static void uart_read_data(struct udevice *dev)
{
	struct dm_serial_ops *ops = serial_get_ops(dev);
	int val = 0;

	if (ops == NULL)
		return;

	for(;;) {
		do {
			val = ops->getc(dev);
			if (val == -EAGAIN)
				schedule();
		} while (val == -EAGAIN);

		if (val == 0x03)
			break;
		else
			serial_putc(val);
	}
}

static void uart_write_data(struct udevice *dev, const char *str)
{
	struct dm_serial_ops *ops = serial_get_ops(dev);
	int ret = 0;

	if(ops == NULL)
		return;

	while (*str != '\0') {
		do {
			ret = ops->putc(dev, *str);
		} while (ret == -EAGAIN);

		++str;
	}
}

static int do_uart(struct cmd_tbl *cmdtp, int flag, int argc,
			char *const argv[])
{
	int ret = CMD_RET_USAGE;
	char node_name[8] = {0};
	int node, id = CONFIG_SECONDARY_UART_INDEX;

	if (argc < 2)
		return CMD_RET_USAGE;

	if ((strncmp(argv[1], "start", 5) == 0) && (argc == 2)) {
		printf("starting secondary UART %d...", id);
		snprintf(node_name, sizeof(node_name), "uart%d", id);
		node = fdt_path_offset(gd->fdt_blob, node_name);
		/*
		 * allow probe after reloc
		 */
		gd->flags &= ~GD_FLG_RELOC;
		uclass_get_device_by_of_offset(UCLASS_SERIAL, node, &dev);
		gd->flags |= GD_FLG_RELOC;
		if (dev == NULL) {
			printf(" No device found \n");
			ret = CMD_RET_FAILURE;
		} else {
			printf(" Success \n");
			ret = CMD_RET_SUCCESS;
		}
	}

	if ((strcmp(argv[1], "read") == 0) && (argc == 2)) {
		if (dev != NULL) {
			uart_read_data(dev);
			ret = CMD_RET_SUCCESS;
		} else {
			printf(" No device found... do start!!!\n");
			ret = CMD_RET_FAILURE;
		}
	}

	if ((strcmp(argv[1], "write") == 0) && (argc == 3)) {
		if (dev != NULL) {
			uart_write_data(dev, argv[2]);
			ret = CMD_RET_SUCCESS;
		} else {
			printf(" No device found... do start!!!\n");
			ret = CMD_RET_FAILURE;
		}
	}

	return ret;
}

U_BOOT_CMD(
	uart,	3,	0,	do_uart,
	"UART sub-system cli",
	"start - initialize secondary uart\n"
	"uart read - read strings from second UART\n"
	"uart write - write strings to second UART\n"
);

#ifdef CONFIG_IPQ_SMP_CMD_SUPPORT
asmlinkage void secondary_core_entry(char *argv, int *cmd_complete,
					int *cmd_result)
{
	dcache_enable();

	*cmd_result = cli_simple_run_command(argv, 0);
	*cmd_complete = 1;

	bring_secondary_core_down(CPU_POWER_DOWN);
}

static void console_silent_enable(void)
{
	gd->flags |= GD_FLG_SILENT | GD_FLG_DISABLE_CONSOLE;
}

static void console_silent_disable(void)
{
	gd->flags &= ~(GD_FLG_SILENT | GD_FLG_DISABLE_CONSOLE);
}

int do_runmulticore(struct cmd_tbl *cmdtp,
			   int flag, int argc, char *const argv[])
{
	int ret = CMD_RET_SUCCESS;
	int i, j, delay = 0, core_status = 0, core_on_status = 0;
	uint8_t *ptr = NULL;

	if ((argc <= 1) || (argc > 4)) {
		ret = CMD_RET_USAGE;
		goto exit;
	}

	for (i = 1; i < argc; i++) {
		if (!strncmp("runmulticore", argv[i],
			sizeof("runmulticore") - 1)) {
			printf("Restricted command 'runmulticore' for "
				"secondary core\n");
			ret = CMD_RET_USAGE;
			goto exit;
		}
	}

	dcache_disable();

	/* Setting up stack for secondary cores */
	memset(core, 0, sizeof(core));

	global_core_array = core;

	for (i = 1; i < argc; i++) {
		ptr = malloc_cache_aligned(SECONDARY_CORE_STACKSZ);
		if (!ptr) {
			j = i - 1;
			while (j >= 0) {
				if (core[i - 1].stack_ptr != NULL) {
					free(core[i - 1].stack_ptr);
					core[i - 1].stack_ptr = NULL;
				}
				j--;
			}
			printf("Memory allocation failure\n");
			ret = CMD_RET_FAILURE;
			goto exit;
		}
		/* 0xf0 is the padding length */
		core[i - 1].stack_top_ptr = ptr;
		core[i - 1].stack_ptr = (ptr + (SECONDARY_CORE_STACKSZ) - 0xf0);

		core[i - 1].cpu_up = 0;
		core[i - 1].cmd_complete = 0;
		core[i - 1].cmd_result = -1;
		core[i - 1].gd_ptr = gd;
		core[i - 1].arg_ptr = argv[i];
	}

	dcache_enable();

	/* Bringing up the secondary cores */
	for (i = 1; i < argc; i++) {
		printf("Scheduling Core %d\n", i);
		delay = 0;
		console_silent_enable();
		ret = bring_secondary_core_up(i, (uint32_t)secondary_cpu_init,
				(uintptr_t)&core[i - 1]);
		if (ret) {
			panic("Some problem to getting core %d up\n", i);
		}

		while ((delay < 5) && (!(core[i - 1].cpu_up))) {
			mdelay(1000);
			delay++;
		}
		if (!(core[i - 1].cpu_up)) {
			panic("Can't bringup core %d\n",i);
		}
		console_silent_disable();

		core_status |= (BIT(i - 1));
		core_on_status |= (BIT(i - 1));
	}

	/* Waiting for secondary cores to complete the task */
	while (core_status) {
		for (i = 1; i < argc; i++) {
			if ((core_status & (BIT(i - 1))) &&
					(core[i - 1].cmd_complete)) {
				printf("Command on core %d is %s\n", i,
					core[i - 1].cmd_complete ?
					((core[i - 1].cmd_result == -1) ?
					"FAIL" : "PASS"):
					"INCOMPLETE");
				core_status &= (~BIT((i - 1)));
			}
		}
		if (ctrlc()) {
			run_command("reset", 0);
		}
	}

	/* Waiting for cores to powerdown */
	delay = 0;
	while (core_on_status) {
		for (i = 1; i < argc; i++) {
			if (core_on_status & (BIT(i - 1))) {
				if (is_secondary_core_off(i) == 1) {
					printf("core %d powered off\n", i);
					core_on_status &= (~BIT((i - 1)));
				}
			}
		}
		mdelay(1000);
		delay++;
		if (delay > 5)
			panic("Some cores can't be powered off\n");
	}

	/* Free up all the stack */
	for (i = 1; i < argc; i++) {
		free(core[i - 1].stack_top_ptr);
	}

	printf("Status:\n");
	for (i = 1; i < argc; i++) {
		printf("Core %d: %s\n", i,
				core[i - 1].cmd_complete ?
				((core[i - 1].cmd_result == -1) ?
				 "FAIL" : "PASS"): "INCOMPLETE");
	}

exit:
	invalidate_dcache_all();
	dcache_enable();
	return ret;
}

U_BOOT_CMD(runmulticore, 4, 0, do_runmulticore,
	   "Enable and schedule secondary cores",
	   "runmulticore <\"command to core1\"> [core2 core3 ...]");
#endif /* CONFIG_IPQ_SMP_CMD_SUPPORT */

#ifdef CONFIG_CMD_AES_256
enum tz_crypto_service_aes_type_t {
	TZ_CRYPTO_SERVICE_AES_SHK = 0x1,
	TZ_CRYPTO_SERVICE_AES_PHK = 0x2,
	TZ_CRYPTO_SERVICE_AES_TYPE_MAX,

};

enum tz_crypto_service_aes_mode_t {
	TZ_CRYPTO_SERVICE_AES_ECB = 0x0,
	TZ_CRYPTO_SERVICE_AES_CBC = 0x1,
	TZ_CRYPTO_SERVICE_AES_MODE_MAX,
};

#ifndef CONFIG_AES_256_DERIVE_KEY
struct crypto_aes_req_data_t {
	uint64_t type;
	uint64_t mode;
	uint64_t req_buf;
	uint64_t req_len;
	uint64_t ivdata;
	uint64_t iv_len;
	uint64_t resp_buf;
	uint64_t resp_len;
};
#else
#define MAX_CONTEXT_BUFFER_LEN		64
#define DEFAULT_POLICY_DESTINATION	0
#define DEFAULT_KEY_TYPE		2
struct crypto_aes_operation_policy {
	uint32_t operations;
	uint32_t algorithm;
};

struct crypto_aes_hwkey_policy  {
	struct crypto_aes_operation_policy op_policy;
	uint32_t kdf_depth;
	uint32_t permissions;
	uint32_t key_type;
	uint32_t destination;
};

struct crypto_aes_hwkey_bindings {
	uint32_t bindings;
	uint32_t context_len;
	uint8_t context[MAX_CONTEXT_BUFFER_LEN];
};

struct crypto_aes_derive_key_cmd_t {
	struct crypto_aes_hwkey_policy policy;
	struct crypto_aes_hwkey_bindings hw_key_bindings;
	uint32_t source;
	uint64_t mixing_key;
	uint64_t key;
};

struct crypto_aes_req_data_t {
	uint64_t key_handle;
	uint64_t type;
	uint64_t mode;
	uint64_t req_buf;
	uint64_t req_len;
	uint64_t ivdata;
	uint64_t iv_len;
	uint64_t resp_buf;
	uint64_t resp_len;
};

/**
 * do_derive_aes_256_key() - Handle the "derive_key" command-line command
 * @cmdtp:	Command data struct pointer
 * @flag:	Command flag
 * @argc:	Command-line argument count
 * @argv:	Array of command-line arguments
 *
 * Returns zero on success, CMD_RET_USAGE in case of misuse and negative
 * on error.
 */
static int do_derive_aes_256_key(struct cmd_tbl *cmdtp, int flag,
				 int argc, char *const argv[])
{
	struct crypto_aes_derive_key_cmd_t *req_ptr = NULL;
	int ret = CMD_RET_USAGE;
	uintptr_t *key_handle = NULL;
	uint8_t *context_buf = NULL;
	int context_len = 0;
	int i = 0, j = 0;
	scm_param param;

	if (argc != 5)
		return ret;
	context_buf = (uint8_t *)simple_strtoul(argv[3], NULL, 16);;
	context_len = simple_strtoul(argv[4], NULL, 16);
	if (context_len > 64) {
		printf("Error: context length should be less than 64\n");
		return ret;
	}
	req_ptr = (struct crypto_aes_derive_key_cmd_t *)memalign(ARCH_DMA_MINALIGN,
					sizeof(struct crypto_aes_derive_key_cmd_t));
	if (!req_ptr) {
		printf("Error allocating memory for key handle request buf");
		return -ENOMEM;
	}

	req_ptr->policy.key_type = DEFAULT_KEY_TYPE;
	req_ptr->policy.destination = DEFAULT_POLICY_DESTINATION;
	req_ptr->source = simple_strtoul(argv[1], NULL, 16);
	req_ptr->hw_key_bindings.bindings = simple_strtoul(argv[2], NULL, 16);
	key_handle = (uintptr_t *)memalign(ARCH_DMA_MINALIGN,
					sizeof(uint64_t));
	req_ptr->key = (uintptr_t) key_handle;
	req_ptr->mixing_key = 0;
	req_ptr->hw_key_bindings.context_len = context_len;
	while (i < context_len) {
		req_ptr->hw_key_bindings.context[j++] = context_buf[i++];
	}

	memset(&param, 0, sizeof(scm_param));
	param.type = SCM_AES_256_GEN_KEY;
	param.buff[0] = (uintptr_t)req_ptr;
	param.arg_type[0] = SCM_WRITE_OP;
	param.buff[1] = sizeof(struct crypto_aes_derive_key_cmd_t);
	param.len = 2;

	invalidate_dcache_all();
	ret = ipq_scm_call(&param);
	if (ret) {
		printf("\nipq_scm_call: SCM_AES_256_GEN_KEY"
				" failed, ret : %d\n", ret);
		ret = CMD_RET_FAILURE;
	} else
		printf("Key handle is %u\n", (unsigned int)*key_handle);

	if (key_handle)
		free(key_handle);
	if (req_ptr)
		free(req_ptr);

	return ret;
}

/***************************************************/
U_BOOT_CMD(
	derive_aes_256_key, 5, 1, do_derive_aes_256_key,
	"Derive AES 256 key before encrypt/decrypt in TME-L based systems",
	"Key Derivation: derive_aes_256_key <source_data> <bindings_data>"
	"<context_data address> <context data len>"
);
#endif /* CONFIG_AES_256_DERIVE_KEY */

/**
 * do_aes_256() - Handle the "aes" command-line command
 * @cmdtp:	Command data struct pointer
 * @flag:	Command flag
 * @argc:	Command-line argument count
 * @argv:	Array of command-line arguments
 *
 * Returns zero on success, CMD_RET_USAGE in case of misuse and negative
 * on error.
 */
static int do_aes_256(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
	uint64_t src_addr, dst_addr, ivdata;
	uint64_t req_len, iv_len, resp_len, type, mode;
	struct crypto_aes_req_data_t *req_ptr = NULL;
	scm_param param;
	int ret = CMD_RET_USAGE;

#ifndef CONFIG_AES_256_DERIVE_KEY
	if (argc != 10)
		return ret;
#else
	if (argc != 11)
		return ret;
#endif /* CONFIG_AES_256_DERIVE_KEY */

	memset(&param, 0, sizeof(scm_param));
	if (!strncmp(argv[1], "enc", 3))
		param.type = SCM_AES_256_ENC;
	else if (!strncmp(argv[1], "dec", 3))
		param.type = SCM_AES_256_DEC;
	else
		return ret;

	type = simple_strtoul(argv[2], NULL, 16);
	if (type >= TZ_CRYPTO_SERVICE_AES_TYPE_MAX) {
		printf("unkown type specified, use 0x1 - SHK, 0x2 - PHK\n");
		return ret;
	}

	mode = simple_strtoul(argv[3], NULL, 16);
	if (mode >= TZ_CRYPTO_SERVICE_AES_MODE_MAX) {
		printf("unkown mode specified, use 0x0 - ECB, 0x1 - CBC\n");
		return ret;
	}

	src_addr = simple_strtoull(argv[4], NULL, 16);
	req_len = simple_strtoul(argv[5], NULL, 16);
	if (req_len <= 0 || (req_len % 16) != 0) {
		printf("Invalid request buffer length, length "
			"should be multiple of AES block size (16)\n");
		return ret;
	}

	ivdata = simple_strtoull(argv[6], NULL, 16);
	iv_len = simple_strtoul(argv[7], NULL, 16);
	if (iv_len != 16) {
		printf("Error: iv length should be equal to AES block size (16)\n");
		return ret;
	}

	dst_addr = simple_strtoull(argv[8], NULL, 16);
	resp_len =  simple_strtoul(argv[9], NULL, 16);
	if (resp_len < req_len) {
		printf("Error: response buffer cannot be less then request buffer\n");
		return ret;
	}

	req_ptr = (struct crypto_aes_req_data_t *)memalign(ARCH_DMA_MINALIGN,
					sizeof(struct crypto_aes_req_data_t));
	if (!req_ptr) {
		printf("Error allocating memory");
		return -ENOMEM;
	}

#ifdef CONFIG_AES_256_DERIVE_KEY
	req_ptr->key_handle = simple_strtoul(argv[10], NULL, 16);
#endif /* CONFIG_AES_256_DERIVE_KEY */
	req_ptr->type = type;
	req_ptr->mode = mode;
	req_ptr->req_buf = (uint64_t)src_addr;
	req_ptr->req_len = req_len;
	req_ptr->ivdata = (mode == TZ_CRYPTO_SERVICE_AES_CBC) ? (uint64_t)ivdata : 0;
	req_ptr->iv_len = iv_len;
	req_ptr->resp_buf = (uint64_t)dst_addr;
	req_ptr->resp_len = resp_len;

	param.buff[0] = (uintptr_t)req_ptr;
	param.arg_type[0] = SCM_WRITE_OP;
	param.buff[1] = sizeof(struct crypto_aes_req_data_t);
	param.len = 2;

	invalidate_dcache_all();
	ret = ipq_scm_call(&param);
	if (ret) {
		printf("\nipq_scm_call: %s failed, ret : %d\n", \
			(param.type == SCM_AES_256_ENC)? \
			"SCM_AES_256_ENC" : "SCM_AES_256_DEC", ret);
		ret = CMD_RET_FAILURE;
	} else
		printf("Encryption/Decryption successful\n");

	if (req_ptr) {
		free(req_ptr);
		req_ptr = NULL;
	}
	return ret;
}

/***************************************************/
U_BOOT_CMD(
	aes_256, 11, 1, do_aes_256,
	"\nAES 256 CBC/ECB Encryption / Decryption",
	"\nEncryption: aes_256 enc <type> <mode> <plain data address> "
	"<plain data length> <iv data address> <iv length> "
	"<response buf address> <response buffer length>"
#ifdef CONFIG_AES_256_DERIVE_KEY
	" <key_handle>"
#endif /* CONFIG_AES_256_DERIVE_KEY */
	"\nDecryption: aes_256 dec <type> <mode> <encrypted buffer address> "
	"<encrypted buffer length> <iv data address> <iv length> "
	"<response buffer address> <response buffer length>"
#ifdef CONFIG_AES_256_DERIVE_KEY
	" <key_handle>"
#endif /* CONFIG_AES_256_DERIVE_KEY */
);
#endif /* CONFIG_CMD_AES_256 */
