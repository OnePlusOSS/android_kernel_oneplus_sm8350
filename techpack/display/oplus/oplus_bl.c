/***************************************************************
** Copyright (C),  2021,  OPLUS Mobile Comm Corp.,  Ltd
**
** File : oplus_bl.c
** Description : oplus display backlight
** Version : 1.0
** Date : 2021/02/22
** Author : kevin.liuwq@PSW.MM.Display.Stability
**
** ------------------------------- Revision History: -----------
**  <author>        <data>        <version >        <desc>
**  kevin.liuwq    2020/02/22        1.0           Build this moudle
******************************************************************/

#include "oplus_bl.h"

static char oplus_global_hbm_flags = 0x0;
static int enable_hbm_enter_dly_on_flags = 0;
static int enable_hbm_exit_dly_on_flags = 0;

static int oplus_display_panel_dly(struct dsi_panel *panel, char hbm_switch)
{
	int count = 0;
	struct dsi_display_mode *mode;

	mode = panel->cur_mode;
	count = mode->priv_info->cmd_sets[DSI_CMD_DLY_ON].count;
	if (!count) {
		DSI_ERR("This panel does not support samsung panel dly on command\n");
		return 0;
	}

	count = mode->priv_info->cmd_sets[DSI_CMD_DLY_OFF].count;
	if (!count) {
		DSI_ERR("This panel does not support samsung panel dly off command\n");
		return 0;
	}

	if(hbm_switch) {
		if(enable_hbm_enter_dly_on_flags)
			enable_hbm_enter_dly_on_flags++;
		if(0 == oplus_global_hbm_flags) {
			if(dsi_panel_tx_cmd_set(panel, DSI_CMD_DLY_ON)){
				DSI_ERR("Failed to send DSI_CMD_DLY_ON commands\n");
				return 0;
			}
			enable_hbm_enter_dly_on_flags = 1;
		} else if (4 == enable_hbm_enter_dly_on_flags) {
			if(dsi_panel_tx_cmd_set(panel, DSI_CMD_DLY_OFF)){
				DSI_ERR("Failed to send DSI_CMD_DLY_OFF commands\n");
				return 0;
			}
			enable_hbm_enter_dly_on_flags = 0;
		}
	} else {
		if(oplus_global_hbm_flags == 1) {
			if(dsi_panel_tx_cmd_set(panel, DSI_CMD_DLY_ON)){
				DSI_ERR("Failed to send DSI_CMD_DLY_ON commands\n");
				return 0;
			}
			enable_hbm_exit_dly_on_flags = 1;
		}
		else {
			if(enable_hbm_exit_dly_on_flags)
				enable_hbm_exit_dly_on_flags++;
			if(3 == enable_hbm_exit_dly_on_flags) {
				enable_hbm_exit_dly_on_flags = 0;
				if(dsi_panel_tx_cmd_set(panel, DSI_CMD_DLY_OFF)){
					DSI_ERR("Failed to send DSI_CMD_DLY_OFF commands\n");
					return 0;
				}
			}
		}
	}
	return 0;
}

int oplus_display_panel_backlight_mapping(struct dsi_panel *panel, u32 *backlight_level)
{
	u32 bl_lvl = *backlight_level;
	int count = 0;
	struct dsi_display_mode *mode;

	mode = panel->cur_mode;
	count = mode->priv_info->cmd_sets[DSI_CMD_HBM_ENTER_SWITCH].count;
	if (!count) {
		DSI_ERR("This panel does not support samsung panel hbm enter command\n");
		return 0;
	}

	count = mode->priv_info->cmd_sets[DSI_CMD_HBM_EXIT_SWITCH].count;
	if (!count) {
		DSI_ERR("This panel does not support samsung panel hbm exit command\n");
		return 0;
	}

	if (!strcmp(panel->oplus_priv.vendor_name, "AMB670YF01")) {
		if(bl_lvl <= PANEL_MAX_NOMAL_BRIGHTNESS) {
			bl_lvl = backlight_buf[bl_lvl];
		} else if(bl_lvl > HBM_BASE_600NIT) {
			if(panel->panel_id2 <= 2) {
					bl_lvl = HBM_BASE_600NIT;
			} else {
				oplus_display_panel_dly(panel, 1);
				if(oplus_global_hbm_flags == 0) {
					if(dsi_panel_tx_cmd_set(panel, DSI_CMD_HBM_ENTER_SWITCH)) {
						DSI_ERR("Failed to send DSI_CMD_HBM_ENTER_SWITCH commands\n");
						return 0;
					}
					oplus_global_hbm_flags = 1;
				}
				bl_lvl = backlight_600_800nit_buf[bl_lvl - HBM_BASE_600NIT];
			}
		} else if (bl_lvl > PANEL_MAX_NOMAL_BRIGHTNESS) {
			if(oplus_global_hbm_flags == 1) {
				if(dsi_panel_tx_cmd_set(panel, DSI_CMD_HBM_EXIT_SWITCH)){
					DSI_ERR("Failed to send DSI_CMD_HBM_EXIT_SWITCH commands\n");
					return 0;
				}
				oplus_global_hbm_flags = 0;
			}
			bl_lvl = backlight_500_600nit_buf[bl_lvl - PANEL_MAX_NOMAL_BRIGHTNESS];
		}
	}

	*backlight_level = bl_lvl;
	return 0;
}