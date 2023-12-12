/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2017-2019, The Linux Foundation. All rights reserved.
 */
#ifndef _MSM_DRM_NOTIFY_H_
#define _MSM_DRM_NOTIFY_H_

#include <linux/notifier.h>

/* A hardware display blank change occurred */
#define MSM_DRM_EVENT_BLANK			0x01
/* A hardware display blank early change occurred */
#define MSM_DRM_EARLY_EVENT_BLANK		0x02

#ifdef OPLUS_FEATURE_TP_BASIC
#define MSM_DRM_EVENT_FOR_TOUCH 0x03
#endif /* OPLUS_FEATURE_TP_BASIC */

#ifdef VENDOR_EDIT
/* event for onscreenfingerprint scene */
#define MSM_DRM_ONSCREENFINGERPRINT_EVENT	0x10
#endif /* VENDOR_EDIT */

enum {
	/* panel: power on */
	MSM_DRM_BLANK_UNBLANK,
	/* panel: power off */
	MSM_DRM_BLANK_POWERDOWN,
};

enum msm_drm_display_id {
	/* primary display */
	MSM_DRM_PRIMARY_DISPLAY,
	/* external display */
	MSM_DRM_EXTERNAL_DISPLAY,
	MSM_DRM_DISPLAY_MAX
};

struct msm_drm_notifier {
	enum msm_drm_display_id id;
	void *data;
};

#if IS_ENABLED(CONFIG_DRM_OPLUS_NOTIFY) || IS_ENABLED(CONFIG_DRM_MSM)
int msm_drm_register_client(struct notifier_block *nb);
int msm_drm_unregister_client(struct notifier_block *nb);
int msm_drm_notifier_call_chain(unsigned long val, void *v);
#else
static inline int msm_drm_register_client(struct notifier_block *nb)
{
	return 0;
}

static inline int msm_drm_unregister_client(struct notifier_block *nb)
{
	return 0;
}
static inline int msm_drm_notifier_call_chain(unsigned long val, void *v)
{
	return 0;
}
#endif
#endif
