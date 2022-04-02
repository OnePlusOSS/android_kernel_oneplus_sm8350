/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef _TOUCHPANEL_PROC_H_
#define _TOUCHPANEL_PROC_H_

int init_touchpanel_proc(struct touchpanel_data *ts);
void remove_touchpanel_proc(struct touchpanel_data *ts);
void tp_freq_hop_work(struct work_struct *work);
void switch_usb_state_work(struct work_struct *work);
void switch_headset_work(struct work_struct *work);

#endif /*_TOUCHPANEL_PROC_H_*/
