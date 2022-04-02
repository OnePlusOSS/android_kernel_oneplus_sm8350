/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2016 Oplus. All rights reserved.
 */


#ifndef _TOUCH_H_
#define _TOUCH_H_

#include <linux/kernel.h>
#include <linux/module.h>
#include "../oplus_touchscreen_v2/tp_devices.h"
#include "../oplus_touchscreen_v2/touchpanel_common.h"


extern bool tp_judge_ic_match(char *tp_ic_name);

extern int tp_util_get_vendor(struct hw_resource *hw_res, struct panel_info *panel_data);

extern int preconfig_power_control(struct touchpanel_data *ts);

extern int reconfig_power_control(struct touchpanel_data *ts);

extern void display_esd_check_enable_bytouchpanel(bool enable);
#endif
