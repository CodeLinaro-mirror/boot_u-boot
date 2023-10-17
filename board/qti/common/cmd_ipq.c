/*
 * Copyright (c) 2018 The Linux Foundation. All rights reserved.
 *
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
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
#ifdef CONFIG_IPQ_QCN9224_FUSING
#include <dm.h>
#include <init.h>
#include <pci.h>
#include <dt-bindings/pci/pci.h>
#include <asm/io.h>
#include <linux/iopoll.h>
#endif

#include "ipq_board.h"


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
#endif

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
#endif
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

#endif
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

	fuse = malloc(sizeof(struct fuse_payload ) * MAX_FUSE_ADDR_SIZE);
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

	ret = ipq_scm_call(&param);

/*	ret = qca_scm_list_ipq5332_fuse(SCM_SVC_FUSE, TZ_READ_FUSE_VALUE, fuse,
			sizeof(struct fuse_payload ) * MAX_FUSE_ADDR_SIZE);
*/
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

	if (ret) {
		printf("Failed to read OEM parameters at Address 0x%X\n", ret);
	}
	free(fuse);
	return 0;
}

U_BOOT_CMD(list_ipq5332_fuse, 1, 0, do_list_ipq5332_fuse,
		"fuse set of QFPROM registers from memory\n",
		"");
#endif
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
#endif

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
#endif

#ifdef CONFIG_DPR_VER_2_0
U_BOOT_CMD(dpr_execute, 3, 0, do_dpr,
                "Debug Policy Request processing\n",
                "dpr_execute [fileaddr] [filesize] - Processing dpr\n");
#endif
#endif
