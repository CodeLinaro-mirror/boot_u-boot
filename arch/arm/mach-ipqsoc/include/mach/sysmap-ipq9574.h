/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * (C) Copyright 2022 Sumit Garg <sumit.garg@linaro.org>
 *
 * Copyright (c) 2023, Qualcomm Innovation Center, Inc. All rights reserved.
 *
 */

#ifndef _MACH_SYSMAP_IPQ9574_H
#define _MACH_SYSMAP_IPQ9574_H

#define SDCC1_SRC_SEL_GPLL2_OUT_MAIN	(2 << 8)
#define SDCC1_SRC_SEL_GPLL0_OUT_MAIN	(1 << 8)

/* BLSP UART clock register */
#define BLSP1_AHB_CBCR			0x01004
#define BLSP1_UART1_BCR			0x02028
#define APCS_CLOCK_BRANCH_ENA_VOTE	0x0B004

#define BLSP1_UART_BCR(id)		((id < 1) ? \
					(BLSP1_UART1_BCR):\
					(BLSP1_UART1_BCR + (0x1000 * id)))

#define BLSP1_UART_APPS_CMD_RCGR(id)	(BLSP1_UART_BCR(id) + 0x04)
#define BLSP1_UART_APPS_CFG_RCGR(id)	(BLSP1_UART_BCR(id) + 0x08)
#define BLSP1_UART_APPS_M(id)		(BLSP1_UART_BCR(id) + 0x0c)
#define BLSP1_UART_APPS_N(id)		(BLSP1_UART_BCR(id) + 0x10)
#define BLSP1_UART_APPS_D(id)		(BLSP1_UART_BCR(id) + 0x14)
#define BLSP1_UART_APPS_CBCR(id)	(BLSP1_UART_BCR(id) + 0x18)

/* Uart clock control registers */
#define BLSP1_UART2_BCR			(0x3028)
#define BLSP1_UART2_APPS_CBCR		(0x3040)
#define BLSP1_UART2_APPS_CMD_RCGR	(0x302C)
#define BLSP1_UART2_APPS_CFG_RCGR	(0x3030)
#define BLSP1_UART2_APPS_M		(0x3034)
#define BLSP1_UART2_APPS_N		(0x3038)
#define BLSP1_UART2_APPS_D		(0x303C)

/* SD controller clock control registers */
#define SDCC1_BCR			(0x33000)
#define SDCC1_APPS_CMD_RCGR		(0x33004)
#define SDCC1_APPS_CFG_RCGR		(0x33008)
#define SDCC1_APPS_M			(0x3300C)
#define SDCC1_APPS_N			(0x33010)
#define SDCC1_APPS_D			(0x33014)
#define SDCC1_APPS_CBCR			(0x3302C)
#define SDCC1_AHB_CBCR			(0x33034)

#endif
