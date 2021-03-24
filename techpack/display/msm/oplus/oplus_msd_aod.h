/***************************************************************
** Copyright (C),  2020,  oneplus Mobile Comm Corp.,  Ltd
** File : oplus_msd_aod.h
** Description : oneplus aod feature
** Version : 1.0
** Date : 2020/11/28
**
** ------------------------------- Revision History: -----------
**  <author>        <data>        <version >        <desc>
**   xxxxx.xxx         2020/11/28        1.0           Build this moudle
******************************************************************/
#ifndef _ONEPLUS_MSD_AOD_H_
#define _ONEPLUS_MSD_AOD_H_

#include <linux/err.h>
#include <linux/list.h>
#include <linux/of.h>
#include <linux/err.h>
#include "msm_drv.h"
#include "sde_connector.h"
#include "sde_crtc.h"
#include "sde_hw_dspp.h"
#include "sde_plane.h"
#include "msm_mmu.h"
#include "dsi_display.h"
#include "dsi_panel.h"
#include "dsi_ctrl.h"
#include "dsi_ctrl_hw.h"
#include "dsi_drm.h"
#include "dsi_clk.h"
#include "dsi_pwr.h"
#include "sde_dbg.h"
#include <linux/kobject.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/module.h>
#include <linux/init.h>
#include <drm/drm_mipi_dsi.h>

int oplus_panel_parse_msd_aod_config(struct dsi_panel *panel);
bool oplus_is_msd_aod_supported(void);
int oplus_display_update_msd_aod(struct dsi_panel *panel);
int oplus_display_update_msd_clock_location(struct dsi_cmd_desc *cmds,
		enum dsi_cmd_set_type type,
		struct dsi_panel *panel);
int oplus_display_update_msd_hand_length(struct dsi_cmd_desc *cmds,
		enum dsi_cmd_set_type type,
		struct dsi_panel *panel);
int oplus_display_update_msd_hand_backward_length(struct dsi_cmd_desc *cmds,
		enum dsi_cmd_set_type type,
		struct dsi_panel *panel);
int oplus_display_update_msd_hand_thickness(struct dsi_cmd_desc *cmds,
		enum dsi_cmd_set_type type,
		struct dsi_panel *panel);
int oplus_display_update_msd_hand_rotate_resolution(struct dsi_cmd_desc *cmds,
		enum dsi_cmd_set_type type,
		struct dsi_panel *panel);
int oplus_display_update_msd_hand_color(struct dsi_cmd_desc *cmds,
		enum dsi_cmd_set_type type,
		struct dsi_panel *panel);
int oplus_display_update_msd_hand_hms_priority(struct dsi_cmd_desc *cmds,
		enum dsi_cmd_set_type type,
		struct dsi_panel *panel);
int oplus_display_update_msd_hand_gradation(struct dsi_cmd_desc *cmds,
		enum dsi_cmd_set_type type,
		struct dsi_panel *panel);
int oplus_display_update_msd_time(struct dsi_cmd_desc *cmds,
		enum dsi_cmd_set_type type,
		struct dsi_panel *panel);
int oplus_panel_send_msd_aod_command(struct dsi_panel *panel);
struct oplus_msd_aod_info *get_msd_info(void);
int oplus_display_get_mipi_dsi_msg(const struct mipi_dsi_msg *msg, char* buf);
int oplus_display_get_msd_aod_command(char *buf);

#endif /* _ONEPLUS_MSD_AOD_H_ */
