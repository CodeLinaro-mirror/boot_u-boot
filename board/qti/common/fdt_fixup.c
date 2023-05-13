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

#ifdef CONFIG_FDT_FIXUP_PARTITIONS
void ipq_smem_part_to_mtdparts(char *mtdid, int len)
{
	ipq_smem_flash_info_t *sfi = get_ipq_smem_flash_info();
	int i, ret;
	int device_id = 0;
	char *part = mtdid, *unit;
	int init = 0;
	uint32_t bsize;
	struct smem_ptable *ptable = get_ipq_part_table_info();
#ifdef CONFIG_CMD_NAND
	struct mtd_info *mtd = get_nand_dev_by_index(0);
#endif

	ret = snprintf(part, len, "%s:", mtdid);
	part += ret;
	len -= ret;

	for (i = 0; i < ptable->len && len > 0; i++) {
		struct smem_ptn *p = &ptable->parts[i];
		loff_t psize;
		bsize = get_part_block_size(p, sfi);

		if (part_which_flash(p) && init == 0) {
			device_id = 0;
			ret = snprintf(part, len, ";nand%d:", device_id);
			part += ret;
			len -= ret;
			init = 1;
		}
		if (p->size == (~0u)) {
			/*
			 * Partition size is 'till end of device', calculate
			 * appropriately
			 */
#ifdef CONFIG_CMD_NAND
			psize = (mtd->size - (((loff_t)p->start) * bsize));
#else
			psize = 0;
#endif
		} else {
			psize =  ((loff_t)p->size) * bsize;
		}

		if ((psize > SZ_1M) && (((psize & (SZ_1M - 1)) == 0))) {
			psize /= SZ_1M;
			unit = "M@";
		} else if ((psize > SZ_1K) && (((psize & (SZ_1K - 1)) == 0))) {
			psize /= SZ_1K;
			unit = "K@";
		} else {
			unit = "@";
		}

		ret = snprintf(part, len, "%lld%s0x%llx(%s),", psize, unit,
				((loff_t)p->start) * bsize, p->name);
		part += ret;
		len -= ret;
	}

	if (i == 0)
		*mtdid = '\0';

	*(part-1) = 0;	/* Remove the trailing ',' character */
}

static int ipq_fdt_fixup_spi_nor_params(void *blob,
		const struct node_info *node_info, int node_info_size)
{
        int ret, nodeoff = -1;
	ipq_smem_flash_info_t *sfi = get_ipq_smem_flash_info();
        uint32_t val, i;

	for (i = 0; i < node_info_size; i++) {
		if (node_info[i].type != MTD_DEV_TYPE_NOR)
			continue;

	        nodeoff = fdt_node_offset_by_compatible(blob, -1,
				node_info[i].compat);
		if (nodeoff >= 0)
			break;
        }

	if (nodeoff < 0) {
		printf("fdt-fixup: unable to find compatible node\n");
		return nodeoff;
	}

        val = cpu_to_fdt32(sfi->flash_block_size);
        ret = fdt_setprop(blob, nodeoff, "sector-size",
			&val, sizeof(uint32_t));
        if (ret) {
                printf("fdt-fixup: unable to set sector size(%d)\n", ret);
                return ret;
        }

        if (sfi->flash_density != 0) {
                val = cpu_to_fdt32(sfi->flash_density);
                ret = fdt_setprop(blob, nodeoff, "density",
				&val, sizeof(uint32_t));
                if (ret) {
                        printf("fdt-fixup: unable to set density(%d)\n", ret);
                        return ret;
                }
        }

        return 0;
}

static void ipq_fdt_fixup_mtdparts(void *blob)
{
	ipq_smem_flash_info_t *sfi = get_ipq_smem_flash_info();
	char *parts;
	char parts_str[4096];
	char mtdids[256];
	char *mtdparts = NULL;
	int len = sizeof(parts_str);

	if (((sfi->flash_type == SMEM_BOOT_NAND_FLASH) ||
			(sfi->flash_type == SMEM_BOOT_QSPI_NAND_FLASH))) {
		snprintf(parts_str, sizeof(parts_str), "mtdparts=nand0");
	} else if (sfi->flash_type == SMEM_BOOT_SPI_FLASH) {
		/* NOR density & sector-size fix-up */
		ipq_fdt_fixup_spi_nor_params(blob, fnodes, *fnode_entires);
		snprintf(parts_str, sizeof(parts_str), "mtdparts=" \
				CONFIG_IPQ_SPI_NOR_DEV_NAME);

		if ((sfi->flash_secondary_type == SMEM_BOOT_NAND_FLASH) ||
			(sfi->flash_secondary_type ==
			 SMEM_BOOT_QSPI_NAND_FLASH)) {
			snprintf(mtdids, sizeof(mtdids), "nand0=nand0,nor0="
					CONFIG_IPQ_SPI_NOR_DEV_NAME);
		} else {
			snprintf(mtdids, sizeof(mtdids), "nor0="
					CONFIG_IPQ_SPI_NOR_DEV_NAME);
		}

		env_set("mtdids", mtdids);
	}

	mtdparts = parts_str;
	if (mtdparts) {
		ipq_smem_part_to_mtdparts(mtdparts,len);
		if (mtdparts[0] != '\0') {
			debug("mtdparts = %s\n", mtdparts);
			env_set("mtdparts", mtdparts);
		}
	}

	parts = env_get("mtdparts");
	if (parts)
		fdt_fixup_mtdparts(blob, fnodes, *fnode_entires);
	else
		return;
}
#endif

static const fdt_fixup_t fixup_functions[] = {
	ipq_fdt_fixup_socinfo,
#ifdef CONFIG_FDT_FIXUP_PARTITIONS
	ipq_fdt_fixup_mtdparts,
#endif
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
