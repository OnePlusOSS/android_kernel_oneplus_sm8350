/***************************************************************
** Copyright (C),  2020,  OPLUS Mobile Comm Corp.,  Ltd
** File : oplus_onscreenfingerprint.h
** Description : oplus onscreenfingerprint feature
** Version : 1.0
** Date : 2021/01/08
**
** ------------------------------- Revision History: -----------
**  <author>        <data>        <version >        <desc>
**  xxxxx.xxx       2021/01/08        1.0           Build this moudle
******************************************************************/
#ifndef _OPLUS_ONSCREENFINGERPRINT_H_
#define _OPLUS_ONSCREENFINGERPRINT_H_

#include <drm/drm_mipi_dsi.h>
#include "dsi_display.h"
#include "dsi_panel.h"
#include "dsi_drm.h"

int oneplus_get_panel_brightness_to_alpha(void);
int dsi_panel_parse_oplus_config(struct dsi_panel *panel);
int dsi_panel_brightness_alpha_debug(struct drm_connector *connector, const char *buf, size_t count);

#endif /*_OPLUS_ONSCREENFINGERPRINT_H_*/