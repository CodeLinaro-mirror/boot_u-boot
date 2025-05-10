// SPDX-License-Identifier: GPL-2.0+
/* DDRInfo Fixup
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 */

#include "qcom_fixup_handlers.h"

/**
 * fixup_dt_handler() - Calls the device tree fixup handlers.
 * @fdt_ptr The firmware DT node to update.
 *
 * This function retrieves the DDR details entry from the SMEM and prints it
 * out.
 */
void fixup_dt_handler(struct fdt_header *fdt_ptr)
{
        int ret;
        u32 path_offset;
	char prop_val[] = "disabled";

        path_offset = fdt_path_offset(fdt_ptr, "/soc@0/mmc@7c4000");
        if (path_offset < 0) {
                log_err("Error getting mmc offset: %d\n", path_offset);
                return;
        }

        ret = fixup_dt_node(fdt_ptr, path_offset, "status",
                            (void *)prop_val, SET_PROP_STRING);

        path_offset = fdt_path_offset(fdt_ptr, "/pcie@1c08000");
        if (path_offset < 0) {
                log_err("Error getting pci offset: %d\n", path_offset);
                return;
        }

        ret = fixup_dt_node(fdt_ptr, path_offset, "status",
                            (void *)prop_val, SET_PROP_STRING);

	boardinfo_fixup_handler(fdt_ptr);
	ddrinfo_fixup_handler(fdt_ptr);
	subsetparts_fixup_handler(fdt_ptr);
	hypervisor_fixup_handler(fdt_ptr);
}
