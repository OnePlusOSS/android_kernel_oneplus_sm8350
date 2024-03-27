/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2022, Qualcomm Innovation Center, Inc. All rights reserved. */

#ifndef _QCOM_HV_HAPTICS_H
#define _QCOM_HV_HAPTICS_H

#include <linux/notifier.h>

enum hbst_event {
	HBST_OFF,
};

int register_hbst_off_notifier(struct notifier_block *nb);
int unregister_hbst_off_notifier(struct notifier_block *nb);
#endif