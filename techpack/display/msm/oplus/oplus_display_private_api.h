/***************************************************************
** Copyright (C),  2021,  Oplus Mobile Comm Corp.,  Ltd
** File : oplus_display_private_api.h
** Description : oplus display private api implement
** Version : 1.0
** Date : 2020/09/03
** Author :
**
** ------------------------------- Revision History: -----------
**  <author>        <data>        <version >        <desc>
**                 2020/09/03        1.0           Build this moudle
******************************************************************/

#ifndef _OPLUS_DISPLAY_PRIVATE_API_H_
#define _OPLUS_DISPLAY_PRIVATE_API_H_

#include <linux/err.h>
#include <linux/list.h>
#include <linux/of.h>
#include <linux/err.h>
#include <linux/kobject.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/module.h>
#include <linux/init.h>
#include <drm/drm_mipi_dsi.h>

#include "dsi_display.h"
#include "dsi_drm.h"

#define MIPI_DCS_SET_DISPLAY_BRIGHTNESS 0x51

int mipi_dsi_dcs_set_display_brightness_samsung(struct dsi_display *dsi_display, u16 brightness);
int iris_loop_back_test(struct drm_connector *connector);

/* ---------------- dsi_panel ----------------*/
int oplus_dsi_panel_enable(void *dsi_panel);

/* --------------- dsi_display ---------------*/
/* factory function */
int dsi_display_back_ToolsType_ANA67061(u8 *buff);
int dsi_display_get_ToolsType_ANA6706(struct drm_connector *connector);
int dsi_display_get_ddic_coords_X(struct drm_connector *connector);
int dsi_display_get_ddic_coords_Y(struct drm_connector *connector);
int dsi_display_get_ddic_check_info(struct drm_connector *connector);
int dsi_display_get_serial_number(struct drm_connector *connector);
int dsi_display_get_serial_number_at(struct drm_connector *connector);
int dsi_display_get_serial_number_year(struct drm_connector *connector);
int dsi_display_get_serial_number_mon(struct drm_connector *connector);
int dsi_display_get_serial_number_day(struct drm_connector *connector);
int dsi_display_get_serial_number_hour(struct drm_connector *connector);
int dsi_display_get_serial_number_min(struct drm_connector *connector);
int dsi_display_get_serial_number_sec(struct drm_connector *connector);
int dsi_display_get_serial_number_msec_int(struct drm_connector *connector);
int dsi_display_get_serial_number_msec_rem(struct drm_connector *connector);
int dsi_display_get_code_info(struct drm_connector *connector);
int dsi_display_get_stage_info(struct drm_connector *connector);
int dsi_display_get_production_info(struct drm_connector *connector);
int dsi_display_get_panel_ic_v_info(struct drm_connector *connector);
/* display effect function */
int dsi_display_set_native_loading_effect_mode(struct drm_connector *connector, int level);
int dsi_display_get_native_display_loading_effect_mode(struct drm_connector *connector);
/* misc */
int dsi_display_read_panel_reg(struct dsi_display *dsi_display, unsigned char registers, char *buf, size_t count);
int dsi_display_write_panel_reg(struct dsi_display *dsi_display, unsigned char registers, char *buf, size_t count);

#endif
