// SPDX-License-Identifier: GPL-2.0+
/*
 * Bootmethod for distro boot via EFI
 *
 * Copyright 2021 Google LLC
 * Written by Simon Glass <sjg@chromium.org>
 */

#define LOG_CATEGORY LOGC_EFI

#include <efi_device_path.h>
#include <efi_loader.h>
#include <env.h>
#include <errno.h>
#include <log.h>
#include <string.h>
#include <stdlib.h>
#include <vsprintf.h>
#include <fdt_support.h>
#include <smem.h>
#include <dm.h>
#include <soc/qcom/socinfo.h>

/**
 * efi_get_distro_fdt_name() - get the filename for reading the .dtb file
 *
 * @fname:	buffer for filename
 * @size:	buffer size
 * @seq:	sequence number, to cycle through options (0=first)
 *
 * Returns:
 * 0 on success,
 * -ENOENT if the "fdtfile" env var does not exist,
 * -EINVAL if there are no more options,
 * -EALREADY if the control FDT should be used
 */
int efi_get_distro_fdt_name(char *fname, int size, int seq)
{
	const char *fdt_fname;
	const char *prefix;

	/* select the prefix */
	switch (seq) {
	case 0:
		/* this is the default */
		prefix = "/dtb";
		break;
	case 1:
		prefix = "";
		break;
	case 2:
		prefix = "/dtb/current";
		break;
	case 3:
		prefix = "/dtbs";
		break;
	default:
		return log_msg_ret("pref", -EINVAL);
	}

	fdt_fname = env_get("fdtfile");
	if (fdt_fname) {
		snprintf(fname, size, "%s/%s", prefix, fdt_fname);
		log_debug("Using device tree: %s\n", fname);
	} else if (IS_ENABLED(CONFIG_OF_HAS_PRIOR_STAGE)) {
		strcpy(fname, "<prior>");
		return log_msg_ret("pref", -EALREADY);
	/* Use this fallback only for 32-bit ARM */
	} else if (IS_ENABLED(CONFIG_ARM) && !IS_ENABLED(CONFIG_ARM64)) {
		const char *soc = env_get("soc");
		const char *board = env_get("board");
		const char *boardver = env_get("boardver");

		/* cf the code in label_boot() which seems very complex */
		snprintf(fname, size, "%s/%s%s%s%s.dtb", prefix,
			 soc ? soc : "", soc ? "-" : "", board ? board : "",
			 boardver ? boardver : "");
		log_debug("Using default device tree: %s\n", fname);
	} else {
		return log_msg_ret("env", -ENOENT);
	}

	return 0;
}

/**
 * efi_load_distro_fdt() - load distro device-tree
 *
 * @handle:	handle of loaded image
 * @fdt:	on return device-tree, must be freed via efi_free_pages()
 * @fdt_size:	buffer size
 */
void efi_load_distro_fdt(efi_handle_t handle, void **fdt, efi_uintn_t *fdt_size)
{
	struct efi_device_path *dp;
	efi_status_t  ret;
	struct efi_handler *handler;
	struct efi_loaded_image *loaded_image;
	efi_handle_t device;

	*fdt = NULL;

	/* Get boot device from loaded image protocol */
	ret = efi_search_protocol(handle, &efi_guid_loaded_image, &handler);
	if (ret != EFI_SUCCESS)
		return;
	loaded_image = handler->protocol_interface;
	device = loaded_image->device_handle;

	/* Get device path of boot device */
	ret = efi_search_protocol(device, &efi_guid_device_path, &handler);
	if (ret != EFI_SUCCESS)
		return;
	dp = handler->protocol_interface;

	/* Try the various available names */
	for (int seq = 0; ; ++seq) {
		struct efi_device_path *file;
		char buf[255];

		if (efi_get_distro_fdt_name(buf, sizeof(buf), seq))
			break;
		file = efi_dp_from_file(dp, buf);
		if (!file)
			break;
		ret = efi_load_image_from_path(true, file, fdt, fdt_size);
		efi_free_pool(file);
		if (ret == EFI_SUCCESS) {
			log_debug("Fdt %pD loaded\n", file);
			break;
		}
	}
}

struct qcom_board_id {
	u32 variant;
	u32 sub_type;
};

struct qcom_pmic_id {
	u32 pmic_ver[4];
};

struct qcom_plat_id {
	u32 plat_id;
	u32 soc_rev;
};

struct qcom_smem_pmic_info {
	u32 model;
	u32 version;
};

struct qcom_dt_entry {
	u32 plat_id;
	u32 variant;
	u32 sub_type;
	u32 soc_rev;
	u32 pmic_ver[4];
	void *fdt;
	u32 size;
	u32 idx;
};

struct qcom_dt_entry qcom_dt_entries[64];

#define PLAT_ID_SIZE    sizeof (struct qcom_plat_id)
#define BOARD_ID_SIZE   sizeof (struct qcom_board_id)
#define PMIC_ID_SIZE    sizeof (struct qcom_pmic_id)
#define DEV_TREE_VERSION_V1 1
#define DEV_TREE_VERSION_V2 2
#define DEV_TREE_VERSION_V3 3
#define DT_ENTRY_V1_SIZE 0xC
#define PMIC_EXT_VERSION 0x0000000000010003

#define fdt_board_variant(_b, _i)	\
	(fdt32_to_cpu(((struct qcom_board_id *)_b)[_i].variant) & 0xff)
#define fdt_board_sub_type(_b, _i)	\
	(fdt32_to_cpu(((struct qcom_board_id *)_b)[_i].sub_type) & 0xff)
#define fdt_plat_plat_id(_b, _i)	\
	(fdt32_to_cpu(((struct qcom_plat_id *)_b)[_i].plat_id) & 0xffff)
#define fdt_plat_soc_rev(_b, _i)	\
	fdt32_to_cpu(((struct qcom_plat_id *)_b)[_i].soc_rev)
#define fdt_pmic_pmic_ver(_b, _i, _x)	\
	fdt32_to_cpu(((struct qcom_pmic_id *)_b)[_i].pmic_ver[_x])


static int qcom_parse_one_dtb(void *fdt, struct socinfo *socinfo, void **match)
{
	const char *model, *pmic, *board, *plat;
	int pmiclen, boardlen, platlen, minplatlen;
	struct qcom_smem_pmic_info *smem_pmic_info;
	int dtbver, i, j, k;

	*match = NULL;

	model = fdt_getprop(fdt, 0, "model", NULL);
	pmic = fdt_getprop(fdt, 0, "qcom,pmic-id", &pmiclen);
	board = fdt_getprop(fdt, 0, "qcom,board-id", &boardlen);
	plat = fdt_getprop(fdt, 0, "qcom,msm-id", &platlen);

	if (pmic && (pmiclen > 0) && board && (boardlen > 0)) {
		if ((pmiclen % PMIC_ID_SIZE) || (boardlen % BOARD_ID_SIZE)) {
			printf("qcom,pmic-id (%d) or qcom,board-id (%d) not a multiple of (%lu, %lu)\n",
				pmiclen, boardlen, PMIC_ID_SIZE, BOARD_ID_SIZE);
			return -1;
		}
		dtbver = DEV_TREE_VERSION_V3;
		minplatlen = PLAT_ID_SIZE;
	} else if (board && (boardlen > 0)) {
		if (boardlen % BOARD_ID_SIZE) {
			printf("qcom,board-id (%d) not a multiple of %lu\n",
				boardlen, BOARD_ID_SIZE);
			return -1;
		}
		dtbver = DEV_TREE_VERSION_V2;
		minplatlen = PLAT_ID_SIZE;
	} else {
		dtbver = DEV_TREE_VERSION_V1;
		minplatlen = DT_ENTRY_V1_SIZE;
	}

	if (!plat || (platlen < 0)) {
		printf("qcom,msm-id not found\n");
		return -1;
	} else if (platlen % minplatlen) {
		printf("qcom,msm-id (%d) not a multiple of %d\n",
			platlen, minplatlen);
		return -1;
	}


	smem_pmic_info = ((void *)socinfo) + socinfo->pmic_array_offset;

	for (i = 0; i < (boardlen / BOARD_ID_SIZE); i++)
		for (j = 0; j < (platlen / PLAT_ID_SIZE); j++)
			if (pmic) {
				for (k = 0; k < (pmiclen / PMIC_ID_SIZE); k++) {
					if (((socinfo->id & 0x0000ffff) == fdt_plat_plat_id(plat, j)) &&
					    (socinfo->hw_plat == fdt_board_variant(board, i)) &&
					    (socinfo->hw_plat_subtype == fdt_board_sub_type(board, i)) &&
					    (socinfo->plat_ver == fdt_plat_soc_rev(plat, j)) &&
					    (smem_pmic_info[0].model == fdt_pmic_pmic_ver(pmic, k, 0)) &&
					    (smem_pmic_info[1].model == fdt_pmic_pmic_ver(pmic, k, 1)) &&
					    (smem_pmic_info[2].model == fdt_pmic_pmic_ver(pmic, k, 2)) &&
					    (smem_pmic_info[3].model == fdt_pmic_pmic_ver(pmic, k, 3))) {
						*match = fdt;
						printf("%8x| %8x| %8x| %4d.%03d| %8x| %8x| %8x| %8x| %s\n",
							fdt_board_variant(board, i),
							fdt_board_sub_type(board, i),
							fdt_plat_plat_id(plat, j),
							SOCINFO_MAJOR(fdt_plat_soc_rev(plat, j)),
							SOCINFO_MINOR(fdt_plat_soc_rev(plat, j)),
							fdt_pmic_pmic_ver(pmic, k, 0),
							fdt_pmic_pmic_ver(pmic, k, 1),
							fdt_pmic_pmic_ver(pmic, k, 2),
							fdt_pmic_pmic_ver(pmic, k, 3), model);
						return 0;
					}
				}
			} else {
				if (((socinfo->id & 0x0000ffff) == fdt_plat_plat_id(plat, j)) &&
				    (socinfo->hw_plat == fdt_board_variant(board, i)) &&
				    (socinfo->hw_plat_subtype == fdt_board_sub_type(board, i)) &&
				    (socinfo->plat_ver == fdt_plat_soc_rev(plat, j))) {
					*match = fdt;
					printf("%8x| %8x| %8x| %4d.%03d| %8s| %8s| %8s| %8s| %s\n",
						fdt_board_variant(board, i),
						fdt_board_sub_type(board, i),
						fdt_plat_plat_id(plat, j),
						SOCINFO_MAJOR(fdt_plat_soc_rev(plat, j)),
						SOCINFO_MINOR(fdt_plat_soc_rev(plat, j)),
						"--", "--", "--", "--", model);
					return 0;
				}
			}

	return 0;
}

struct fdt_header *qcom_parse_combined_dtb(struct fdt_header *fdt)
{
	int fdt_count = 0;
	struct udevice *dev;
	size_t size;
	struct socinfo *socinfo;
	struct fdt_header *match;
	struct fdt_header *fdt_blob = fdt;

	if (!fdt_valid(&fdt)) {
		printf("%s: fdt not valid!\n", __func__);
		return NULL;
	}

	uclass_get_device(UCLASS_SMEM, 0, &dev);

	socinfo = smem_get(dev, 0, SMEM_HW_SW_BUILD_ID, &size);
	printf("%s: socinfo: %p\n", __func__, socinfo);

	printf("%-8s| %-8s| %-8s| %-8s| %-8s| %-8s| %-8s| %-8s| %s\n"
		"--------+---------+---------+---------+---------+---------+---------+---------+------\n",
		"Variant", "Sub Type", "Plat Id", "SoC Rev.",
		"pmic[0]", "pmic[1]", "pmic[2]", "pmic[3]", "Model");

	while (fdt_valid(&fdt)) {
		qcom_parse_one_dtb(fdt, socinfo, &match);

		if (match)
			return match;

		fdt = ((void *)fdt) + fdt_totalsize(fdt);
		fdt_count++;
	}

	if (fdt_count == 1)
		return fdt_blob;

	return NULL;
}
/**
 * efi_load_qcom_fdt() - load distro device-tree
 *
 * @handle:	handle of loaded image
 * @fdt:	on return device-tree, must be freed via efi_free_pages()
 * @fdt_size:	buffer size
 */
void efi_load_qcom_fdt(efi_handle_t handle, void **_fdt, efi_uintn_t *_fdt_size)
{
	u32 i;
	void *fdt;
	efi_status_t ret;
	efi_uintn_t fdt_size, count;
	const u16 *fdt_name = u"/combined-dtb.dtb";
	struct efi_handler *handler;
	efi_handle_t *volume_handles = NULL;
	struct efi_simple_file_system_protocol *v;
	struct efi_file_handle *f = NULL;
	void *match;

	*_fdt = NULL;
	*_fdt_size = 0;

	ret = efi_locate_handle_buffer_int(BY_PROTOCOL, &efi_simple_file_system_protocol_guid,
					   NULL, &count, (efi_handle_t **)&volume_handles);
	if (ret != EFI_SUCCESS) {
		printf("%s: No block device found! (0x%lx)\n", __func__, ret);
		return;
	}

	printf("Using device tree: %ls\n", fdt_name);

	/* Use this fallback only for 32-bit ARM */
	for (i = 0; i < count; i++) {
		struct efi_file_handle *root;

		ret = efi_search_protocol(volume_handles[i],
					  &efi_simple_file_system_protocol_guid, &handler);
		if (ret != EFI_SUCCESS)
			continue;
		ret = efi_protocol_open(handler, (void **)&v, efi_root, NULL,
					EFI_OPEN_PROTOCOL_GET_PROTOCOL);
		if (ret != EFI_SUCCESS)
			continue;

		ret = EFI_CALL(v->open_volume(v, &root));
		if (ret != EFI_SUCCESS)
			goto out;

		ret = EFI_CALL(root->open(root, &f, fdt_name,
					  EFI_FILE_MODE_READ, 0));
		if (ret != EFI_SUCCESS) {
			printf("%s: Reading volume failed! (0x%lx)\n", __func__, ret);
			continue;
		} else {
			printf("%s: %ls found!\n", __func__, fdt_name);
			break;
		}
	}

	if (!f)
		goto out;

	ret = efi_file_size(f, &count);
	if (ret != EFI_SUCCESS)
		goto out;

	ret = efi_allocate_pages(EFI_ALLOCATE_ANY_PAGES, EFI_BOOT_SERVICES_DATA,
				 efi_size_in_pages(count), &fdt);
	if (ret != EFI_SUCCESS) {
		printf("%s: Out of memory! (0x%lx bytes) (0x%lx)\n",
							__func__, count, ret);
		goto out;
	}
	ret = EFI_CALL(f->read(f, &count, fdt));
	if (ret != EFI_SUCCESS) {
		printf("%s: Can't read fdt! (0x%lx)\n", __func__, ret);
		goto out;
	}

	match = qcom_parse_combined_dtb(fdt);
	if (match) {
		void *tmp;

		*_fdt_size = fdt_totalsize(match);

		ret = efi_allocate_pages(EFI_ALLOCATE_ANY_PAGES,
					EFI_BOOT_SERVICES_DATA,
					efi_size_in_pages(*_fdt_size), &tmp);
		if (ret != EFI_SUCCESS) {
			printf("%s: Out of memory! (0x%lx bytes) (0x%lx)\n",
					__func__, *_fdt_size, ret);
			goto out;
		}
		/*
		 * The memory allocated for reading the fdt is eventually freed
		 * by the caller using the address we return. Hence copy it out
		 */
		memcpy(tmp, match, *_fdt_size);
		*_fdt = tmp;

		printf("%s: fdt@0x%p %lu\n", __func__, *_fdt, *_fdt_size);

		//efi_create_event
	} else {
		printf("%s: No FDT matched!!\n", __func__);
	}
out:
	efi_free_pages((uintptr_t)fdt, efi_size_in_pages(count));

	efi_free_pool(volume_handles);
}
