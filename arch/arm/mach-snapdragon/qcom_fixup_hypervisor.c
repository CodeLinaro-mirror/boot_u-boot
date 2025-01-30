/* Hypervisor Fixup
 * SPDX-License-Identifier: GPL-2.0+
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * Copyright (c) 2009-2021, The Linux Foundation. All rights reserved.
 *
 * Copyright (c) 2009, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of The Linux Foundation nor
 *       the names of its contributors may be used to endorse or promote
 *       products derived from this software without specific prior written
 *       permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <fdt_support.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/libfdt.h>
#include "qcom_fixup_handlers.h"

#define HYP_BOOTINFO_MAGIC 0xC06B0071
#define HYP_MAX_NUM_DTBOS 4
#define SMC_HYPERVISOR_PARAM_ID (u32)0x42000609
#define SCM_HYP_DTB_RESULT 1

struct __packed hyp_boot_info {
	u32 hyp_boot_info_magic;
	u32 hyp_boot_info_version;
	u32 hyp_boot_info_size;
	u32 pil_enable;
	struct {
		u32 vm_type;
		u32 num_dtbos;
		union {
			struct {
				u64 dtbo_base;
				u64 dtbo_size;
			} linux_aarch64[HYP_MAX_NUM_DTBOS];
			/* union padding */
			u32 vm_info[16];
		} info;
	} primary_vm_info;
};

struct boot_param_list {
	u32 num_hyp_dtbos;
	u64 *hyp_dtbo_base_addr;
};

static bool is_vm_enabled;
static struct hyp_boot_info *hyp_info;

static void smc_wrapper_call(u32 *regs_ptr);
static int check_and_set_vm_data(struct boot_param_list *pboot_param_list_ptr);
static struct hyp_boot_info *get_vm_data(void);
static int set_hyp_info(struct boot_param_list *pboot_param_list_ptr,
			struct hyp_boot_info *hyp_info_ptr);

/**
 * smc_wrapper_call() - issue the secure monitor call
 * @regs_ptr: pointer to an array of 8 u32 values representing the input and
 * output arguments x0~x7: input arguments x0~x3: output arguments
 *
 * This function issues a secure monitor call using the SMC instruction.
 * It loads the input arguments into the registers and then calls the SMC
 * instruction. The output arguments are stored in the registers.
 */
static void smc_wrapper_call(u32 *regs_ptr)
{
	asm volatile("ldr x0, %0\n"
		     "ldr x1, %1\n"
		     "ldr x2, %2\n"
		     "ldr x3, %3\n"
		     "ldr x4, %4\n"
		     "ldr x5, %5\n"
		     "ldr x6, %6\n"
		     "smc     #0\n"
		     "str x0, %0\n"
		     "str x1, %1\n"
		     "str x2, %2\n"
		     "str x3, %3\n"
		     : "+m"(regs_ptr[0]), "+m"(regs_ptr[1]),
		     "+m"(regs_ptr[2]), "+m"(regs_ptr[3])
		     : "m"(regs_ptr[4]), "m"(regs_ptr[5]), "m"(regs_ptr[6])
		     : "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7", "x8",
		     "x9", "x10",
		     "x11", "x12", "x13", "x14", "x15", "x16", "x17");
}

/**
 * hyp_boot_info() - Retrieves the Hypervisor Boot Information.
 *
 * This function issues a secure monitor call to retrieve the Hypervisor Boot
 * Information. If the Hypervisor is enabled, it returns the Hypervisor Boot
 * Information. Otherwise, it issues the secure monitor call and retrieves the
 * Hypervisor Boot Information.
 *
 * Return: A pointer to the Hypervisor Boot Information structure.
 */
static struct hyp_boot_info *get_vm_data(void)
{
	u32 l_param[32] = { 0 };

	l_param[0] = SMC_HYPERVISOR_PARAM_ID;
	if (is_vm_enabled)
		return hyp_info;

	smc_wrapper_call(l_param);

	log_debug(" SMC call return .. R[0-3]: 0x%x | 0x%x | 0x%x | 0x%x | 0x%x | 0x%x\n",
		  l_param[0], l_param[1], l_param[2], l_param[3], l_param[4],
		  l_param[5]);

	hyp_info = (struct hyp_boot_info *)(uintptr_t)l_param[SCM_HYP_DTB_RESULT];
	if (!hyp_info)
		return hyp_info;
	is_vm_enabled = true;

	return hyp_info;
}

/**
 * set_hyp_info() - Set the Hypervisor Boot Information
 * @pboot_param_list_ptr: Pointer to the BootParamlist structure
 * @hyp_info_ptr: Pointer to the HypBootInfo structure
 *
 * This function sets the Hypervisor Boot Information based on the provided
 * BootParamlist and HypBootInfo.
 *
 * Return: 0 on success, -1 on failure
 */
static int set_hyp_info(struct boot_param_list *pboot_param_list_ptr,
			struct hyp_boot_info *hyp_info_ptr)
{
	u32 idx;
	u32 num_dtbos;
	u64 *dtbo_base_addr_ptr;

	if (hyp_info_ptr->hyp_boot_info_magic != HYP_BOOTINFO_MAGIC)
		return log_msg_ret("Invalid HYP MAGIC\n", -1);

	num_dtbos = hyp_info_ptr->primary_vm_info.num_dtbos;
	if (num_dtbos > HYP_MAX_NUM_DTBOS) {
		log_err("hyp_info_ptr: Invalid number of dtbos: %d max supported: %d\n",
			num_dtbos, HYP_MAX_NUM_DTBOS);
		num_dtbos = HYP_MAX_NUM_DTBOS;
	}

	dtbo_base_addr_ptr = (u64 *)calloc(num_dtbos, sizeof(u64));
	if (!dtbo_base_addr_ptr)
		return log_msg_ret("hyp_info_ptr: Failed to allocate memory\n",
				   -1);

	for (idx = 0; idx < num_dtbos; idx++) {
		dtbo_base_addr_ptr[idx] = hyp_info_ptr->primary_vm_info
					  .info.linux_aarch64[idx].dtbo_base;
	}

	pboot_param_list_ptr->num_hyp_dtbos = num_dtbos;
	pboot_param_list_ptr->hyp_dtbo_base_addr = dtbo_base_addr_ptr;

	return 0;
}

/**
 * check_and_set_vm_data() - Checks if the VM data is available and updates the BootParamlist if it
 * is.
 * @pboot_param_list_ptr: Pointer to the BootParamlist structure to be updated
 *
 * This function calls the secure monitor call to retrieve the VM data and then
 * updates the BootParamlist with the retrieved data.
 *
 * Return: 0 on success, -1 on failure
 */
static int check_and_set_vm_data(struct boot_param_list *pboot_param_list_ptr)
{
	int status;
	struct hyp_boot_info *hyp_boot_info_ptr = get_vm_data();

	if (!hyp_boot_info_ptr)
		return log_msg_ret("hyp_info_ptr is NULL\n", -1);

	status = set_hyp_info(pboot_param_list_ptr, hyp_boot_info_ptr);
	if (status != 0)
		log_err("Failed to update HypInfo!! Status:%d\n", status);

	return status;
}

/**
 * add_overlay_fdt() - Adds overlay FDT to base FDT.
 * @fdt_ptr: A pointer to the device tree header.
 * @dtbo_base_addr_ptr: The address of the new entry.
 *
 * Return: true if the operation is successful, false otherwise.
 */
static bool add_overlay_fdt(struct fdt_header *fdt_ptr, void *dtbo_base_addr_ptr)
{
	int ret;
	u32 hyp_offset;

	ret = fdt_increase_size(fdt_ptr, fdt_totalsize(dtbo_base_addr_ptr));
	log_debug("%s: fdt increase size proccessed result- %d\n",
		  __func__, ret);

	hyp_offset = fdt_path_offset(dtbo_base_addr_ptr,
				     "/fragment@0/__overlay__");
	if (hyp_offset < 0) {
		log_err("Error getting overlay offset: %d\n", hyp_offset);
		return false;
	}

	log_debug("%s: Hyp fdt offset- %d\n", __func__, hyp_offset);

	log_debug("%s: Before Overlay Fdt@0x%p | %lu - 0x%lx\n", __func__,
		  fdt_ptr, fdt_totalsize(fdt_ptr), fdt_totalsize(fdt_ptr));

	ret = fdt_overlay_apply_node(fdt_ptr, 0, dtbo_base_addr_ptr,
				     hyp_offset);
	if (ret != 0) {
		log_err("HypDtFixupEventNotifyFunc: fdt_overlay_apply failed Ret: %d\n",
			ret);
		return false;
	}

	return true;
}

/**
 * hypervisor_fixup_handler() - This function is responsible for updating
 * the device tree with the hypervisor boot information.
 * @fdt_ptr: A pointer to the device tree header.
 *
 * Return: None
 */
void hypervisor_fixup_handler(struct fdt_header *fdt_ptr)
{
	struct boot_param_list boot_params_list;
	int ret, idx;
	int status = check_and_set_vm_data(&boot_params_list);

	if (status != 0) {
		log_err("Failed to update HypData!! Status:%d\n", status);
		return;
	}

	if (is_vm_enabled) {
		if (!boot_params_list.hyp_dtbo_base_addr) {
			log_err("Error: HypOverlay DT is NULL\n");
			return;
		}

		for (idx = 0; idx < boot_params_list.num_hyp_dtbos; idx++) {
			if (!boot_params_list.hyp_dtbo_base_addr[idx]) {
				log_err("Not overlaying hyp Dtbo :%d is null or Bad DT header\n",
					idx);
				continue;
			}
			/*handled for first overlay only,
			 *need to support for multiple in future
			 */
			if (idx == 0)
				add_overlay_fdt(fdt_ptr,
						(void *)boot_params_list
						.hyp_dtbo_base_addr[idx]);
		}
	}
	log_debug("%s: Dtb apply overlay is success\n", __func__);
}
