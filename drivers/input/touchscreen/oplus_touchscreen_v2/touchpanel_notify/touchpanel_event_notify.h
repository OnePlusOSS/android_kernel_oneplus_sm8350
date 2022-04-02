// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef _TOUCHPANEL_EVENTNOTIFY_H
#define _TOUCHPANEL_EVENTNOTIFY_H

#define EVENT_ACTION_FOR_FINGPRINT 0x01

struct touchpanel_event {
	int touchpanel_id;
#ifndef CONFIG_OPLUS_MTK_DRM_GKI_NOTIFY
	struct timeval time;
#endif
	int x;
	int y;
	int fid;       /* Finger ID */
	char type;     /* 'D' - Down, 'M' - Move, 'U' - Up, */
	int touch_state;
	int area_rate;
};

#define EVENT_TYPE_DOWN    'D'
#define EVENT_TYPE_MOVE    'M'
#define EVENT_TYPE_UP      'U'

/* caller API */
int touchpanel_event_register_notifier(struct notifier_block *nb);
int touchpanel_event_unregister_notifier(struct notifier_block *nb);

/* callee API */
void touchpanel_event_call_notifier(unsigned long action, void *data);
#endif /*_TOUCHPANEL_EVENTNOTIFY_H*/
