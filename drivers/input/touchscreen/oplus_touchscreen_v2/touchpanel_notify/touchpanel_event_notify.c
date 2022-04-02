// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include <linux/module.h>
#include <linux/export.h>
#include <linux/notifier.h>
#include "touchpanel_event_notify.h"

static BLOCKING_NOTIFIER_HEAD(touchpanel_notifier_list);

int touchpanel_event_register_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&touchpanel_notifier_list, nb);
}
EXPORT_SYMBOL(touchpanel_event_register_notifier);

int touchpanel_event_unregister_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&touchpanel_notifier_list, nb);
}
EXPORT_SYMBOL(touchpanel_event_unregister_notifier);

void touchpanel_event_call_notifier(unsigned long action, void *data)
{
	blocking_notifier_call_chain(&touchpanel_notifier_list, action, data);
}
EXPORT_SYMBOL(touchpanel_event_call_notifier);

int (*tp_gesture_enable_notifier)(unsigned int tp_index) = NULL;
EXPORT_SYMBOL(tp_gesture_enable_notifier);

MODULE_DESCRIPTION("Touchscreen Event Notify Driver");
MODULE_LICENSE("GPL");
