/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2023-2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _DT_BINDINGS_CLK_GCC_DEVSOC_H
#define _DT_BINDINGS_CLK_GCC_DEVSOC_H

/* GCC controlled clock IDs */
#define GCC_QUPV3_UART0_CLK				0
#define GCC_QUPV3_UART1_CLK				1
#define GCC_QPIC_IO_MACRO_CLK				2
#define GCC_QUPV3_SE2_CLK				3
#define GCC_QUPV3_SE3_CLK				4
#define GCC_QUPV3_SE4_CLK				5
#define GCC_QUPV3_SE5_CLK				6
#define GCC_SDCC1_APPS_CLK				7
#define GCC_SDCC1_AHB_CLK				8
#define GCC_NSSCFG_CLK					9
#define GCC_NSSNOC_MEMNOC_CLK				10
#define GCC_NSSNOC_MEMNOC_1_CLK				11
#define GCC_USB0_MASTER_CLK				12
#define GCC_USB0_MOCK_UTMI_CLK				13
#define GCC_USB0_SLEEP_CLK				14
#define GCC_USB0_AUX_CLK				15
#define GCC_USB0_PHY_CFG_AHB_CLK			16
#define GCC_USB1_MASTER_CLK				17
#define GCC_USB1_MOCK_UTMI_CLK				18
#define GCC_USB1_SLEEP_CLK				19
#define GCC_USB1_PHY_CFG_AHB_CLK			20
#define GCC_USB0_PIPE_CLK				21
#define GCC_CNOC_USB_CLK				22

/* NSS controlled clock IDs */
#define NSS_CC_PPE_CLK					100
#define NSS_CC_PPE_SWITCH_IPE_CLK			101
#define NSS_CC_PPE_SWITCH_CLK				102
#define NSS_CC_PPE_EDMA_CLK				103
#define NSS_CC_PPE_EDMA_CFG_CLK				104
#define NSS_CC_CFG_CLK					105
#define NSS_CC_CE_CLK					106
#define NSS_CC_CE_APB_CLK				107
#define NSS_CC_CE_AXI_CLK				108
#define NSS_CC_NSSNOC_CE_APB_CLK			109
#define NSS_CC_NSSNOC_CE_AXI_CLK			110
#define NSS_CC_NSS_CSR_CLK				111
#define NSS_CC_NSSNOC_NSS_CSR_CLK			112
#define NSS_CC_PORT1_MAC_CLK				113
#define NSS_CC_PORT2_MAC_CLK				114
#define NSS_CC_PORT3_MAC_CLK				115
#define NSS_CC_NSSNOC_PPE_CLK				116
#define NSS_CC_PORT1_RX_CLK				117
#define NSS_CC_PORT1_TX_CLK				118
#define NSS_CC_PORT2_RX_CLK				119
#define NSS_CC_PORT2_TX_CLK				120
#define NSS_CC_PORT3_RX_CLK				121
#define NSS_CC_PORT3_TX_CLK				122
#define NSS_CC_UNIPHY_PORT1_RX_CLK			123
#define NSS_CC_UNIPHY_PORT1_TX_CLK			124
#define NSS_CC_UNIPHY_PORT2_RX_CLK			125
#define NSS_CC_UNIPHY_PORT2_TX_CLK			126
#define NSS_CC_UNIPHY_PORT3_RX_CLK			127
#define NSS_CC_UNIPHY_PORT3_TX_CLK			128

#endif
