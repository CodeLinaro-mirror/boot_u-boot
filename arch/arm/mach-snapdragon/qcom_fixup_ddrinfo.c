// SPDX-License-Identifier: GPL-2.0+
/*
 * DDRInfo Fixup
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 */

#include <dm.h>
#include <fdt_support.h>
#include <smem.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/libfdt.h>
#include "qcom_fixup_handlers.h"

/* With the new SMEM architecture, SMEM IDs need to be defined in individual
 * driver files
 */
#define SMEM_ID_DDRINFO 0x25B // 603
#define MAX_IDX_CH 8

struct ddr_freq_table {
	u32 freq_khz;
	u8 enable;
};

struct ddr_freq_plan_entry {
	struct ddr_freq_table ddr_freq[14];
	u8 num_ddr_freqs;
	u32 *clk_period_address;
	u32 max_nom_ddr_freq;
};

struct ddr_part_details {
	u8 revision_id1[2];
	u8 revision_id2[2];
	u8 width[2];
	u8 density[2];
};

struct ddr_details_entry {
	u8 manufacturer_id;
	u8 device_type;
	struct ddr_part_details ddr_params[MAX_IDX_CH];
	struct ddr_freq_plan_entry ddr_freq_tbl;
	u8 num_channels;
	u8 num_ranks[2]; /* number of ranks per channel */
	u8 hbb[2][2];
	/* Highest Bank Bit per rank per channel */ /*Reserved for Future use*/
};

static int get_ddr_details(struct ddr_details_entry *ddr_detail);

/**
 * get_ddr_details() - Retrieves the DDR details entry from the SMEM.
 * @ddr_detail: The DDR details entry to retrieve.
 *
 * This function retrieves the DDR details entry from the SMEM and prints it
 * out.
 *
 * Return: 0 on success, -1 on failure
 */
static int get_ddr_details(struct ddr_details_entry *ddr_detail)
{
	void *ddr_table_ptr;
	struct udevice *dev;
	size_t size;

	if (uclass_get_device(UCLASS_SMEM, 0, &dev) != 0)
		return log_msg_ret("Error: uclass_get_device\n", -1);
	ddr_table_ptr = smem_get(dev, 0, SMEM_ID_DDRINFO, &size);

	if (!ddr_table_ptr)
		return log_msg_ret("Error: invalid DDR Entry\n", -1);
	memcpy((void *)ddr_detail, ddr_table_ptr, sizeof(struct ddr_details_entry));

	return 0;
}

/**
 * ddrinfo_fixup_handler() - DDRInfo Fixup handler function
 * @fdt_ptr: Pointer to the device tree
 *
 * This function is responsible for updating the DDR information in the device
 * tree.
 */
void ddrinfo_fixup_handler(struct fdt_header *fdt_ptr)
{
	u32 path_offset, chan, ret;
	u32 value[] = { 0x00, 0x90000000, 0x00, 0x00 };
	u64 prop_value;
	char fdt_rank_prop[] = "ddr_device_rank_ch ";
	struct ddr_details_entry ddr_details;

	ret = get_ddr_details(&ddr_details);
	if (ret) {
		log_err("Error getting DDR details\n");
		return;
	}

	path_offset = fdt_path_offset(fdt_ptr, "/memory");
	if (path_offset < 0) {
		log_err("Error getting memory offset: %d\n", path_offset);
		return;
	}
	prop_value = (u64)ddr_details.device_type;
	ret = fixup_dt_node(fdt_ptr, path_offset, "ddr_device_type",
			    (void *)(&prop_value), APPEND_PROP_U64);
	if (ret)
		log_err("Failed to append DDR device type data : %d\n", ret);

	prop_value = (u64)ddr_details.num_channels;
	ret = fixup_dt_node(fdt_ptr, path_offset, "ddr_device_channel",
			    (void *)(&prop_value), APPEND_PROP_U64);
	if (ret)
		log_err("Failed to append DDR Channels data : %d\n", ret);

	for (chan = 0; chan < ddr_details.num_channels; chan++) {
		snprintf(fdt_rank_prop, sizeof(fdt_rank_prop),
			 "ddr_device_rank_ch%d", chan);
		prop_value = (u64)ddr_details.num_ranks[chan];
		ret = fixup_dt_node(fdt_ptr, path_offset,
				    (const char *)fdt_rank_prop,
				    (void *)(&prop_value),
				    APPEND_PROP_U64);
		if (ret)
			log_err("Failed to append DDR ranks data : %d\n", ret);
	}
	fdt_setprop_inplace(fdt_ptr, path_offset, "reg", value, 16);
}

/* End of File */
