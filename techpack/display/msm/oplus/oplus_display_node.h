/***************************************************************
** Copyright (C),  2020,  OnePlus Mobile Comm Corp.,  Ltd
** File : oplus_display_node.h
** Description : OPlus display node
** Version : 1.0
** Date : 2020/09/03
**
** ------------------------------- Revision History: -----------
**  <author>        <data>        <version >        <desc>
**  xxxxx.xxx       2020/09/03        1.0           Build this moudle
******************************************************************/

#ifndef _ONEPLUS_DISPLAY_NODE_H_
#define _ONEPLUS_DISPLAY_NODE_H_

#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/regulator/consumer.h>
#include <sound/soc.h>
#include <drm/drm_mipi_dsi.h>
#include <linux/input.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <drm/drm_sysfs.h>
#include <linux/sysfs.h>
#include <drm/drm_panel.h>


#include "dsi_display.h"
#include "dsi_panel.h"
#ifdef CONFIG_PXLW_IRIS
#include "oplus_adfr.h"
#endif

#define DSI_PANEL_SAMSUNG_S6E3HC2 0
#define DSI_PANEL_SAMSUNG_S6E3FC2X01 1
#define DSI_PANEL_SAMSUNG_SOFEF03F_M 2
#define DSI_PANEL_SAMSUNG_ANA6705 3
#define DSI_PANEL_SAMSUNG_ANA6706 4
#define DSI_PANEL_SAMSUNG_AMB655XL 5
#define DSI_PANEL_SAMSUNG_AMB655XL08 6
#define DSI_PANEL_SAMSUNG_AMB670YF01 7

enum {
	/* panel:lcd doze mode */
	DRM_PANEL_BLANK_NORMAL = 3,
	/* panel power off */
	DRM_PANEL_BLANK_POWERDOWN_CUST,
	DRM_PANEL_ONSCREENFINGERPRINT_EVENT,
	DRM_PANEL_BLANK_UNBLANK_CHARGE,
	DRM_PANEL_BLANK_POWERDOWN_CHARGE,
	/* panel power on for tp */
	DRM_PANEL_BLANK_UNBLANK_CUST,
	/*panel 60HZ */
	DRM_PANEL_DYNAMICFPS_60 = 60,
	/*panel 90HZ */
	DRM_PANEL_DYNAMICFPS_90 = 90,
};


extern char dsi_panel_name;
extern int reg_read_len;

extern int oneplus_get_panel_brightness_to_alpha(void);
extern ssize_t notify_fppress_store(struct device *dev,
			struct device_attribute *attr, const char *buf, size_t count);
extern ssize_t notify_dim_store(struct device *dev,
			struct device_attribute *attr, const char *buf, size_t count);
extern ssize_t notify_aod_store(struct device *dev,
			struct device_attribute *attr, const char *buf, size_t count);

extern int dsi_display_set_hbm_mode(struct drm_connector *connector, int level);
extern int dsi_display_get_hbm_mode(struct drm_connector *connector);
extern int dsi_display_get_serial_number(struct drm_connector *connector);
extern int dsi_display_get_serial_number_year(struct drm_connector *connector);
extern int dsi_display_get_serial_number_mon(struct drm_connector *connector);
extern int dsi_display_get_serial_number_day(struct drm_connector *connector);
extern int dsi_display_get_serial_number_hour(struct drm_connector *connector);
extern int dsi_display_get_serial_number_min(struct drm_connector *connector);
extern int dsi_display_set_acl_mode(struct drm_connector *connector, int level);
extern int dsi_display_get_acl_mode(struct drm_connector *connector);
extern int dsi_display_set_hbm_mode(struct drm_connector *connector, int level);
extern int dsi_display_get_hbm_mode(struct drm_connector *connector);
extern int dsi_display_set_hbm_brightness(struct drm_connector *connector, int level);
extern int dsi_display_get_hbm_brightness(struct drm_connector *connector);
extern int dsi_display_set_aod_mode(struct drm_connector *connector, int level);
extern int dsi_display_get_aod_mode(struct drm_connector *connector);
extern int dsi_display_set_dci_p3_mode(struct drm_connector *connector, int level);
extern int dsi_display_get_dci_p3_mode(struct drm_connector *connector);
extern int dsi_display_set_night_mode(struct drm_connector *connector, int level);
extern int dsi_display_get_night_mode(struct drm_connector *connector);
extern int dsi_display_update_gamma_para(struct drm_connector *connector);
extern int dsi_display_get_serial_number(struct drm_connector *connector);
extern int dsi_display_get_serial_number_year(struct drm_connector *connector);
extern int dsi_display_get_serial_number_mon(struct drm_connector *connector);
extern int dsi_display_get_serial_number_day(struct drm_connector *connector);
extern int dsi_display_get_serial_number_hour(struct drm_connector *connector);
extern int dsi_display_get_serial_number_min(struct drm_connector *connector);
extern int dsi_display_get_serial_number_sec(struct drm_connector *connector);
extern int dsi_display_get_serial_number_msec_int(struct drm_connector *connector);
extern int dsi_display_get_serial_number_msec_rem(struct drm_connector *connector);
extern uint64_t dsi_display_get_serial_number_id(uint64_t serial_number);
extern int dsi_display_get_code_info(struct drm_connector *connector);
extern int dsi_display_get_stage_info(struct drm_connector *connector);
extern int dsi_display_get_production_info(struct drm_connector *connector);
extern int dsi_display_panel_mismatch_check(struct drm_connector *connector);
extern int dsi_display_panel_mismatch(struct drm_connector *connector);
extern int dsi_display_set_aod_disable(struct drm_connector *connector, int disable);
extern int dsi_display_get_aod_disable(struct drm_connector *connector);
extern int dsi_display_set_fp_hbm_mode(struct drm_connector *connector, int level);
extern int dsi_display_get_fp_hbm_mode(struct drm_connector *connector);
extern int dsi_display_update_dsi_on_command(struct drm_connector *connector, const char *buf, size_t count);
extern int dsi_display_get_dsi_on_command(struct drm_connector *connector, char *buf);
extern int dsi_display_update_dsi_panel_command(struct drm_connector *connector, const char *buf, size_t count);
extern int dsi_display_get_dsi_panel_command(struct drm_connector *connector, char *buf);
extern int dsi_display_update_dsi_seed_command(struct drm_connector *connector, const char *buf, size_t count);
extern int dsi_display_get_dsi_seed_command(struct drm_connector *connector, char *buf);
extern int dsi_display_get_reg_read_command_and_value(struct drm_connector *connector, char *buf);
extern int dsi_display_reg_read(struct drm_connector *connector, const char *buf, size_t count);
extern int dsi_display_set_native_display_p3_mode(struct drm_connector *connector, int level);
extern int dsi_display_get_native_display_p3_mode(struct drm_connector *connector);
extern int dsi_display_set_native_display_wide_color_mode(struct drm_connector *connector, int level);
extern int dsi_display_get_native_display_wide_color_mode(struct drm_connector *connector);
extern int dsi_display_set_native_display_srgb_color_mode(struct drm_connector *connector, int level);
extern int dsi_display_get_native_display_srgb_color_mode(struct drm_connector *connector);
extern int dsi_display_set_native_loading_effect_mode(struct drm_connector *connector, int level);
extern int dsi_display_get_native_display_loading_effect_mode(struct drm_connector *connector);
extern int dsi_display_set_customer_srgb_mode(struct drm_connector *connector, int level);
extern int dsi_display_set_customer_p3_mode(struct drm_connector *connector, int level);
extern int dsi_display_get_customer_srgb_mode(struct drm_connector *connector);
extern int dsi_display_get_customer_p3_mode(struct drm_connector *connector);
extern int dsi_display_get_panel_ic_v_info(struct drm_connector *connector);
extern int dsi_display_set_seed_lp_mode(struct drm_connector *connector, int seed_lp_level);
extern int dsi_display_get_seed_lp_mode(struct drm_connector *connector);
extern int dsi_display_get_ddic_check_info(struct drm_connector *connector);
extern int dsi_display_get_ToolsType_ANA6706(struct drm_connector *connector);
extern int iris_loop_back_test(struct drm_connector *connector);

#endif
