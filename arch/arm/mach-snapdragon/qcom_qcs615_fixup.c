// SPDX-License-Identifier: GPL-2.0+
/* DDRInfo Fixup
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 */

#include "qcom_fixup_handlers.h"
#include <fdt_support.h>

struct reserved_mem_region {
	const char *name;
	u64 base;
	u64 size;
};

/* Reserved regions for QCS615 */
static const struct reserved_mem_region mem_res_regions[] = {
	{ "hyp@80000000",          0x80000000, 0x600000 },
	{ "axon_dma@80600000",     0x80600000, 0x100000 },
	{ "boot@80700000",         0x80700000, 0x100000 },
	{ "aop_cmd@80800000",      0x80800000, 0x60000 },
	{ "aop_cmd_db@80860000",   0x80860000, 0x20000 },
	{ "xbl_log@80884000",      0x80884000, 0x10000 },
	{ "xbl_dt@80894000",       0x80894000, 0x40000 },
	{ "smem@80900000",         0x80900000, 0x200000 },
	{ "pil_reserved@84300000", 0x84300000, 0x16B00000 },
	{ "hyp_reserved@b7200000", 0xB7200000, 0x8E00000 },
	{ "tz_stat@c0000000",      0xC0000000, 0x100000 },
	{ "tzapps@c1800000",       0xC1800000, 0x1C00000 },
	{ "guest_vm@d0600000",     0xD0600000, 0x100000 },
	{ "display@e1000000",      0xE1000000, 0x2400000 },
};

/**
 * add_reserved_memory_node() - Add a reserved memory node to kernel DTB
 * @fdt_ptr: Pointer to the device tree
 * @name: Node name (e.g., "hyp@80000000")
 * @base: Base address of the region
 * @size: Size of the region
 *
 * Return: 0 on success, negative error code on failure
 */
static int add_reserved_memory_node(struct fdt_header *fdt_ptr,
				    const char *name,
				    u64 base, u64 size)
{
	int nodeoffset, subnode, ret;
	u32 reg[4];

	nodeoffset = fdt_path_offset(fdt_ptr, "/reserved-memory");
	if (nodeoffset < 0)
		return nodeoffset;

	/* Create reg property: <0x0 base 0x0 size> */
	reg[0] = cpu_to_fdt32(0x0);
	reg[1] = cpu_to_fdt32((u32)base);
	reg[2] = cpu_to_fdt32(0x0);
	reg[3] = cpu_to_fdt32((u32)size);

	subnode = fdt_add_subnode(fdt_ptr, nodeoffset, name);
	if (subnode == -FDT_ERR_EXISTS)
		subnode = fdt_subnode_offset(fdt_ptr, nodeoffset, name);
	if (subnode < 0)
		return subnode;

	ret = fdt_setprop(fdt_ptr, subnode, "reg", reg, sizeof(reg));
	if (ret < 0)
		return ret;

	ret = fdt_setprop(fdt_ptr, subnode, "no-map", NULL, 0);
	if (ret < 0)
		return ret;

	return 0;
}

/**
 * fixup_dt_handler() - Calls the device tree fixup handlers.
 * @fdt_ptr The firmware DT node to update.
 *
 * This function retrieves the DDR details entry from the SMEM and prints it
 * out.
 */
void fixup_dt_handler(struct fdt_header *fdt_ptr)
{
	int ret, i;

	/* Add all reserved regions to kernel DTB */
	for (i = 0; i < ARRAY_SIZE(mem_res_regions); i++) {
		ret = add_reserved_memory_node(fdt_ptr,
					       mem_res_regions[i].name,
					       mem_res_regions[i].base,
					       mem_res_regions[i].size);
		if (ret == -FDT_ERR_NOSPACE) {
			ret = fdt_increase_size(fdt_ptr, 512);
			if (!ret)
				ret = add_reserved_memory_node(fdt_ptr,
							       mem_res_regions[i].name,
							       mem_res_regions[i].base,
							       mem_res_regions[i].size);
		}

		if (ret < 0) {
			log_err("Failed to add %s: %d\n",
				mem_res_regions[i].name, ret);
		}
	}

	boardinfo_fixup_handler(fdt_ptr);
	ddrinfo_fixup_handler(fdt_ptr);
	subsetparts_fixup_handler(fdt_ptr);
	hypervisor_fixup_handler(fdt_ptr);
}
