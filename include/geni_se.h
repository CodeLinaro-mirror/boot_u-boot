// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2023, Qualcomm Innovation Center, Inc. All rights reserved.

#ifndef _GENI_SE_H
#define _GENI_SE_H

enum {
	QUPV3_SE_UART = 0,
	QUPV3_SE_HSUART,
	QUPV3_SE_SPI,
	QUPV3_SE_I2C,
	QUPV3_SE_MAX,
};

void geni_se_fw_load(phys_addr_t se_base, uint8_t se_mode);
#endif
