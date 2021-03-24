/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 */

#ifndef _BATTERY_CHARGER_H
#define _BATTERY_CHARGER_H

enum battery_charger_prop {
	BATTERY_RESISTANCE,
	BATTERY_CHARGER_PROP_MAX,
};

#if IS_ENABLED(CONFIG_QTI_BATTERY_CHARGER)
int qti_battery_charger_get_prop(const char *name,
				enum battery_charger_prop prop_id, int *val);
#else
static inline int
qti_battery_charger_get_prop(const char *name,
				enum battery_charger_prop prop_id, int *val)
{
	return -EINVAL;
}
#endif

enum oplus_subsys_notify_event {
	SUBSYS_EVENT_ADSP_CRASH,
	SUBSYS_EVENT_ADSP_RECOVER,
};

extern void oplus_subsys_set_notifier(void (*notify)(enum oplus_subsys_notify_event event));
extern int oplus_subsys_notify_event(enum oplus_subsys_notify_event event);
#endif
