// SPDX-License-Identifier: GPL-2.0

#ifndef __QCOM_PRIV_H__
#define __QCOM_PRIV_H__

/**
 * enum qcom_boot_source - Track where we got loaded from.
 * Used for capsule update logic.
 *
 * @QCOM_BOOT_SOURCE_ANDROID: chainloaded (typically from ABL)
 * @QCOM_BOOT_SOURCE_XBL: flashed to the XBL or UEFI partition
 */
enum qcom_boot_source {
	QCOM_BOOT_SOURCE_ANDROID = 1,
	QCOM_BOOT_SOURCE_XBL,
};

extern enum qcom_boot_source qcom_boot_source;

int board_serial_num(u32 *serial_num_ptr);

#if IS_ENABLED(CONFIG_EFI_HAVE_CAPSULE_SUPPORT)
/*
 * Capsule Update GUIDs for FIT capsules
 * Each board has a unique GUID to prevent cross-board flashing
 */

/* QCS615 FIT Capsule GUID: 9fd379d2-670e-4bb3-86a1-40497e6e17b0 */
#define QCOM_QCS615_FIT_CAPSULE_GUID \
	EFI_GUID(0x9fd379d2, 0x670e, 0x4bb3, 0x86, 0xa1, \
		 0x40, 0x49, 0x7e, 0x6e, 0x17, 0xb0)

/* QCS6490 FIT Capsule GUID: b2c3d4e5-f6a7-4859-ab1c-2d3e4f5a6b7c */
#define QCOM_QCS6490_FIT_CAPSULE_GUID \
	EFI_GUID(0x6f25bfd2, 0xa165, 0x468b, 0x98, 0x0f, \
		 0xac, 0x51, 0xa0, 0xa4, 0x5c, 0x52)

/* Common name for FIT capsule (same for all boards) */
#define QCOM_FIT_CAPSULE_NAME u"QCOM_FIT_CAPSULE"

void qcom_configure_capsule_updates(void);
#else
void qcom_configure_capsule_updates(void) {}
#endif /* EFI_HAVE_CAPSULE_SUPPORT */

#endif /* __QCOM_PRIV_H__ */
