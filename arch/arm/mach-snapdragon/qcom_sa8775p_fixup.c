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
	boardinfo_fixup_handler(fdt_ptr);
	ddrinfo_fixup_handler(fdt_ptr);
	subsetparts_fixup_handler(fdt_ptr);
	hypervisor_fixup_handler(fdt_ptr);
}
