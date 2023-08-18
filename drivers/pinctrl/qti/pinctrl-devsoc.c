// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2019 Sartura Ltd.
 * Copyright (c) 2023, Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * Author: Robert Marko <robert.marko@sartura.hr>
 */

#include "pinctrl-snapdragon.h"
#include <common.h>

#define MAX_PIN_NAME_LEN		32

static char pin_name[MAX_PIN_NAME_LEN];

static const struct pinctrl_function msm_pinctrl_functions[] = {
	{"uart0_rfr", 1},
	{"uart0_cts", 1},
	{"uart0_rx", 1},
	{"uart0_tx", 1},
	{"uart1_rx", 1},
	{"uart1_tx", 1},
};

static const char *devsoc_get_function_name(struct udevice *dev,
					     unsigned int selector)
{
	return msm_pinctrl_functions[selector].name;
}

static const char *devsoc_get_pin_name(struct udevice *dev,
					unsigned int selector)
{
	snprintf(pin_name, MAX_PIN_NAME_LEN, "GPIO_%u", selector);
	return pin_name;
}

static unsigned int devsoc_get_function_mux(unsigned int selector)
{
	return msm_pinctrl_functions[selector].val;
}

struct msm_pinctrl_data pinctrl_data = {
	.pin_count = 52,
	.functions_count = ARRAY_SIZE(msm_pinctrl_functions),
	.get_function_name = devsoc_get_function_name,
	.get_function_mux = devsoc_get_function_mux,
	.get_pin_name = devsoc_get_pin_name,
};
