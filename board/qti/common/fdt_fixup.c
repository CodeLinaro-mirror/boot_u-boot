// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2015-2017, 2020 The Linux Foundation. All rights reserved.
 *
 * Copyright (c) 2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <common.h>
#include <asm/global_data.h>
#include <jffs2/load_kernel.h>
#include <env.h>
#include <fdt_support.h>
#include <fdtdec.h>
#include <stdlib.h>

#include "ipq_board.h"

DECLARE_GLOBAL_DATA_PTR;

typedef void (*fdt_fixup_t)(void *blob);

#ifdef CONFIG_OF_BOARD_SETUP

__weak void ipq_fdt_fixup_socinfo(void *blob)
{
	uint32_t cpu_type;
	uint32_t soc_version_major, soc_version_minor;
	int nodeoff, ret;
	socinfo_t *ipq_socinfo = get_socinfo();

	nodeoff = fdt_path_offset(blob, "/");

	if (nodeoff < 0) {
		printf("ipq: fdt fixup cannot find root node\n");
		return;
	}

	ret = fdt_setprop(blob, nodeoff, "cpu_type",
			  (void*)&ipq_socinfo->cpu_type, sizeof(cpu_type));
	if (ret)
		printf("%s: cannot set cpu type %d\n", __func__, ret);

	ret = fdt_setprop(blob, nodeoff, "soc_version_major",
			 (void*) &ipq_socinfo->soc_version_major,
			  sizeof(soc_version_major));
	if (ret)
		printf("%s: cannot set soc_version_major %d\n",
		       __func__, soc_version_major);

	ret = fdt_setprop(blob, nodeoff, "soc_version_minor",
			  (void*)&ipq_socinfo->soc_version_minor,
			  sizeof(soc_version_minor));
	if (ret)
		printf("%s: cannot set soc_version_minor %d\n",
		       __func__, soc_version_minor);
	return;
}

static const fdt_fixup_t fixup_functions[] = {
	ipq_fdt_fixup_socinfo,
	NULL
};

static void fix_in_seq(const fdt_fixup_t fixup_f[], void *blob)
{
	const fdt_fixup_t *fixup_ptr;
	for(fixup_ptr = fixup_f ; *fixup_ptr ; ++fixup_ptr) {
		(*fixup_ptr)(blob);
	}
	return;
}

int ft_board_setup(void *blob, struct bd_info *bd)
{
	fix_in_seq(fixup_functions, blob);
	return 0;
}

#endif /* CONFIG_OF_BOARD_SETUP */
