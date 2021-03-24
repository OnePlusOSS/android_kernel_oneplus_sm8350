/***************************************************************
** Copyright (C),  2020,  OPLUS Mobile Comm Corp.,  Ltd
** File : oplus_adfr.h
** Description : ADFR kernel module
** Version : 1.0
** Date : 2020/10/23
**
** ------------------------------- Revision History: -----------
**  <author>        <data>        <version >        <desc>
******************************************************************/

#ifndef _OPLUS_ADFR_H_
#define _OPLUS_ADFR_H_

#include <linux/device.h>
#include <linux/hrtimer.h>

enum oplus_vsync_switch {
	OPLUS_VSYNC_SWITCH_TP = 0,
	OPLUS_VSYNC_SWITCH_TE = 1,
};

enum h_skew_type {
	SDC_ADFR = 0,
	SDC_MFR = 1,
	OPLUS_ADFR = 2,
	OPLUS_MFR = 3,
};

enum oplus_adfr_auto_mode_value {
	OPLUS_ADFR_AUTO_OFF = 0,
	OPLUS_ADFR_AUTO_ON = 1,
};

enum oplus_adfr_auto_fakeframe_value {
	OPLUS_ADFR_FAKEFRAME_OFF = 0,
	OPLUS_ADFR_FAKEFRAME_ON = 1,
};

enum oplus_adfr_auto_min_fps_value {
	OPLUS_ADFR_AUTO_MIN_FPS_MAX = 0,
	OPLUS_ADFR_AUTO_MIN_FPS_60HZ = 1,
};

enum deferred_window_status {
	DEFERRED_WINDOW_END = 0,
	DEFERRED_WINDOW_START = 1,
	DEFERRED_WINDOW_NEXT_FRAME = 2,
	SET_WINDOW_IMMEDIATELY = 3,
};

void oplus_adfr_init(void *dsi_panel);
ssize_t oplus_adfr_get_debug(struct device *dev,
	struct device_attribute *attr, char *buf);
ssize_t oplus_adfr_set_debug(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count);
ssize_t oplus_set_vsync_switch(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count);
ssize_t oplus_get_vsync_switch(struct device *dev,
		struct device_attribute *attr, char *buf);


int oplus_adfr_thread_create(void *msm_param,
	void *msm_priv, void *msm_ddev, void *msm_dev);
void oplus_adfr_thread_destroy(void *msm_priv);

int oplus_adfr_handle_qsync_mode_minfps(u32 propval);
bool oplus_adfr_qsync_mode_minfps_is_updated(void);
u32 oplus_adfr_get_qsync_mode_minfps(void);

void sde_crtc_adfr_handle_frame_event(void *crtc, void* event);

int sde_encoder_adfr_cancel_fakeframe(void *enc);
enum hrtimer_restart sde_encoder_fakeframe_timer_handler(struct hrtimer *timer);
void sde_encoder_fakeframe_work_handler(struct kthread_work *work);
void oplus_adfr_fakeframe_timer_start(void *enc, int deferred_ms);
int sde_encoder_adfr_trigger_fakeframe(void *enc);
void sde_encoder_adfr_prepare_commit(void *crtc, void *enc, void *conn);
void sde_encoder_adfr_kickoff(void *crtc, void *enc, void *conn);

void oplus_adfr_force_qsync_mode_off(void *drm_connector);
int oplus_adfr_adjust_tearcheck_for_dynamic_qsync(void *sde_phys_enc);

int sde_connector_send_fakeframe(void *conn);

int dsi_display_qsync_update_min_fps(void *dsi_display, void *dsi_params);
int dsi_display_qsync_restore(void *dsi_display);

int dsi_display_send_fakeframe(void *disp);
void dsi_display_adfr_change_te_irq_status(void *display, bool enable);

int dsi_panel_parse_adfr(void *dsi_mode, void *dsi_utils);
int dsi_panel_send_qsync_min_fps_dcs(void *dsi_panel,
				int ctrl_idx, uint32_t min_fps);
int dsi_panel_send_fakeframe_dcs(void *dsi_panel,
				int ctrl_idx);
void dsi_panel_adfr_status_reset(void *dsi_panel);

void oplus_dsi_display_vsync_switch(void *disp, bool force_te_vsync);
bool oplus_adfr_vsync_switch_is_enable(void);
void sde_encoder_adfr_vsync_switch(void *enc);
void sde_kms_adfr_vsync_switch(void *m_kms, void *d_crtc);
void oplus_adfr_resolution_vsync_switch(void *dsi_panel);
void oplus_adfr_aod_fod_vsync_switch(void *dsi_panel, bool force_te_vsync);
void oplus_adfr_vsync_switch_reset(void *dsi_panel);

bool oplus_adfr_auto_on_cmd_filter_set(bool enable);
bool oplus_adfr_auto_on_cmd_filter_get(void);
int oplus_adfr_handle_auto_mode(u32 propval);
int dsi_display_auto_mode_update(void *dsi_display);
bool oplus_adfr_has_auto_mode(u32 value);

#endif
