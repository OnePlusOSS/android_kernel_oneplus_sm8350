/***************************************************************
** Copyright (C),  2020,  oneplus Mobile Comm Corp.,  Ltd
** File : oplus_msd_aod.c
** Description : oneplus aod feature
** Version : 1.0
** Date : 2020/11/28
**
** ------------------------------- Revision History: -----------
**  <author>        <data>        <version >        <desc>
**   xxxxx.xxx         2020/11/28        1.0           Build this moudle
******************************************************************/

#include "dsi_defs.h"
#include "oplus_msd_aod.h"
#include <video/mipi_display.h>
#include "dsi_panel.h"
#define OPLUS_MSD_EC_REGISTER 0xEC
#define OPLUS_MSD_AC_REGISTER 0xAC
#define OPLUS_MSD_AB_REGISTER 0xAB
#define GET_BIT(x,n,m)    ((x & ~(~0U<<(m-n+1))<<(n-1)) >>(n-1))

bool msd_aod_support;

int oplus_panel_send_msd_aod_command(struct dsi_panel *panel)
{
	int rc = 0;
	int count;
	struct dsi_display_mode *mode;

	if (!panel || !panel->cur_mode) {
		pr_err("Invalid params\n");
		return -EINVAL;
	}
	mode = panel->cur_mode;

	if (panel->msd_config.msd_mode) {
		count = mode->priv_info->cmd_sets[DSI_CMD_SET_MSD_AOD_ON].count;
		if (!count) {
			pr_err("This panel does not support DSI_CMD_SET_MSD_AOD_ON\n");
			goto error;
		}
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MSD_AOD_ON);
		if (rc)
			pr_err("Failed to send msd aod on command\n");
		pr_err("Send DSI_CMD_SET_MSD_AOD_ON cmds.\n");
	} else {
		count = mode->priv_info->cmd_sets[DSI_CMD_SET_MSD_AOD_OFF].count;
		if (!count) {
			pr_err("This panel does not support DSI_CMD_SET_MSD_AOD_OFF\n");
			goto error;
		}
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MSD_AOD_OFF);
		if (rc)
			pr_err("Failed to send msd aod off command\n");
		pr_err("Send DSI_CMD_SET_MSD_AOD_OFF cmds.\n");
	}
error:
	return rc;
}

int oplus_panel_parse_msd_aod_config(struct dsi_panel *panel)
{
	int rc = 0;
	u32 val = 0;
	bool msd_support = false;
	struct dsi_parser_utils *utils = NULL;

	if (!panel)
		return -EINVAL;

	utils = &panel->utils;
	if (!utils)
		return -EINVAL;

	msd_support = utils->read_bool(utils->data,
			"oplus,msd-aod-support");

	pr_err("%s(), msd aod support: %s",
			__func__, msd_support ? "true" : "false");
	msd_aod_support = msd_support;

	rc = utils->read_u32(utils->data, "oplus,msd-ac-start-point-x", &val);
	if (rc) {
		pr_err("[%s] msd-ac-start-point-x unspecified, defaulting to zero\n",
			 panel->name);
		panel->msd_config.msd_ac_start_point_x = 0;
	} else {
		panel->msd_config.msd_ac_start_point_x = val;
	}

	rc = utils->read_u32(utils->data, "oplus,msd-ac-start-point-y", &val);
	if (rc) {
		pr_err("[%s] msd-ac-start-point-y unspecified, defaulting to zero\n",
			 panel->name);
		panel->msd_config.msd_ac_start_point_y = 0;
	} else {
		panel->msd_config.msd_ac_start_point_y = val;
	}

	rc = utils->read_u32(utils->data, "oplus,msd-ac-width", &val);
	if (rc) {
		pr_err("[%s] msd-ac-width unspecified, defaulting to zero\n",
			 panel->name);
		panel->msd_config.msd_ac_width = 0;
	} else {
		panel->msd_config.msd_ac_width = val;
	}

	rc = utils->read_u32(utils->data, "oplus,msd-ac-height", &val);
	if (rc) {
		pr_err("[%s] msd-ac-start-point-x unspecified, defaulting to zero\n",
			 panel->name);
		panel->msd_config.msd_ac_height = 0;
	} else {
		panel->msd_config.msd_ac_height = val;
	}

	rc = utils->read_u32(utils->data, "oplus,msd-ac-center-x", &val);
	if (rc) {
		pr_err("[%s] msd-ac-center-x unspecified, defaulting to zero\n",
			 panel->name);
		panel->msd_config.msd_ac_center_x = 0;
	} else {
		panel->msd_config.msd_ac_center_x = val;
	}

	rc = utils->read_u32(utils->data, "oplus,msd-ac-center-y", &val);
	if (rc) {
		pr_err("[%s] msd-ac-center-y unspecified, defaulting to zero\n",
			 panel->name);
		panel->msd_config.msd_ac_center_y = 0;
	} else {
		panel->msd_config.msd_ac_center_y = val;
	}

	rc = utils->read_u32(utils->data, "oplus,msd-ac-start-distance", &val);
	if (rc) {
		pr_err("[%s] msd-ac-start-distance unspecified, defaulting to zero\n",
			 panel->name);
		panel->msd_config.msd_ac_start_distance = 0;
	} else {
		panel->msd_config.msd_ac_start_distance = val;
	}

	rc = utils->read_u32(utils->data, "oplus,msd-ac-end-distance-hour", &val);
	if (rc) {
		pr_err("[%s] msd-ac-end-distance-hour unspecified, defaulting to zero\n",
			 panel->name);
		panel->msd_config.msd_ac_end_distance_hour = 0;
	} else {
		panel->msd_config.msd_ac_end_distance_hour = val;
	}

	rc = utils->read_u32(utils->data, "oplus,msd-ac-end-distance-minute", &val);
	if (rc) {
		pr_err("[%s] msd-ac-end-distance-minute unspecified, defaulting to zero\n",
			 panel->name);
		panel->msd_config.msd_ac_end_distance_minute = 0;
	} else {
		panel->msd_config.msd_ac_end_distance_minute = val;
	}

	rc = utils->read_u32(utils->data, "oplus,msd-ac-end-distance-second", &val);
	if (rc) {
		pr_err("[%s] msd-ac-end-distance-second unspecified, defaulting to zero\n",
			 panel->name);
		panel->msd_config.msd_ac_end_distance_second = 0;
	} else {
		panel->msd_config.msd_ac_end_distance_second = val;
	}

	rc = utils->read_u32(utils->data, "oplus,msd-ac-thickness-hour", &val);
	if (rc) {
		pr_err("[%s] msd-ac-thickness-hour unspecified, defaulting to zero\n",
			 panel->name);
		panel->msd_config.msd_ac_thickness_hour = 0;
	} else {
		panel->msd_config.msd_ac_thickness_hour = val;
	}

	rc = utils->read_u32(utils->data, "oplus,msd-ac-thickness-minute", &val);
	if (rc) {
		pr_err("[%s] msd-ac-thickness-minute unspecified, defaulting to zero\n",
			 panel->name);
		panel->msd_config.msd_ac_thickness_minute = 0;
	} else {
		panel->msd_config.msd_ac_thickness_minute = val;
	}

	rc = utils->read_u32(utils->data, "oplus,msd-ac-thickness-second", &val);
	if (rc) {
		pr_err("[%s] msd-ac-thickness-second unspecified, defaulting to zero\n",
			 panel->name);
		panel->msd_config.msd_ac_thickness_second = 0;
	} else {
		panel->msd_config.msd_ac_thickness_second = val;
	}

	rc = utils->read_u32(utils->data, "oplus,msd-ac-opposite-distance-hour", &val);
	if (rc) {
		pr_err("[%s] msd-ac-opposite-distance-hour unspecified, defaulting to zero\n",
			 panel->name);
		panel->msd_config.msd_ac_opposite_distance_hour = 0;
	} else {
		panel->msd_config.msd_ac_opposite_distance_hour = val;
	}

	rc = utils->read_u32(utils->data, "oplus,msd-ac-opposite-distance-minute", &val);
	if (rc) {
		pr_err("[%s] msd-ac-opposite-distance-minute unspecified, defaulting to zero\n",
			 panel->name);
		panel->msd_config.msd_ac_opposite_distance_minute = 0;
	} else {
		panel->msd_config.msd_ac_opposite_distance_minute = val;
	}

	rc = utils->read_u32(utils->data, "oplus,msd-ac-opposite-distance-second", &val);
	if (rc) {
		pr_err("[%s] msd-ac-opposite-distance-second unspecified, defaulting to zero\n",
			 panel->name);
		panel->msd_config.msd_ac_opposite_distance_second = 0;
	} else {
		panel->msd_config.msd_ac_opposite_distance_second = val;
	}

	rc = utils->read_u32(utils->data, "oplus,msd-ac-resolution-hour", &val);
	if (rc) {
		pr_err("[%s] msd-ac-resolution-hour unspecified, defaulting to zero\n",
			 panel->name);
		panel->msd_config.msd_ac_resolution_hour = 0;
	} else {
		panel->msd_config.msd_ac_resolution_hour = val;
	}

	rc = utils->read_u32(utils->data, "oplus,msd-ac-resolution-minute", &val);
	if (rc) {
		pr_err("[%s] msd-ac-resolution-minute unspecified, defaulting to zero\n",
			 panel->name);
		panel->msd_config.msd_ac_resolution_minute = 0;
	} else {
		panel->msd_config.msd_ac_resolution_minute = val;
	}

	rc = utils->read_u32(utils->data, "oplus,msd-ac-resolution-second", &val);
	if (rc) {
		pr_err("[%s] msd-ac-resolution-second unspecified, defaulting to zero\n",
			 panel->name);
		panel->msd_config.msd_ac_resolution_second = 0;
	} else {
		panel->msd_config.msd_ac_resolution_second = val;
	}

	rc = utils->read_u32(utils->data, "oplus,msd-ac-hour-color-r", &val);
	if (rc) {
		pr_err("[%s] msd-ac-hour-color-r unspecified, defaulting to zero\n",
			 panel->name);
		panel->msd_config.msd_ac_hour_color_r = 0;
	} else {
		panel->msd_config.msd_ac_hour_color_r = val;
	}

	rc = utils->read_u32(utils->data, "oplus,msd-ac-hour-color-g", &val);
	if (rc) {
		pr_err("[%s] msd-ac-hour-color-g unspecified, defaulting to zero\n",
			 panel->name);
		panel->msd_config.msd_ac_hour_color_g = 0;
	} else {
		panel->msd_config.msd_ac_hour_color_g = val;
	}

	rc = utils->read_u32(utils->data, "oplus,msd-ac-hour-color-b", &val);
	if (rc) {
		pr_err("[%s] msd-ac-hour-color-b unspecified, defaulting to zero\n",
			 panel->name);
		panel->msd_config.msd_ac_hour_color_b = 0;
	} else {
		panel->msd_config.msd_ac_hour_color_b = val;
	}

	rc = utils->read_u32(utils->data, "oplus,msd-ac-minute-color-r", &val);
	if (rc) {
		pr_err("[%s] msd-ac-minute-color-r unspecified, defaulting to zero\n",
			 panel->name);
		panel->msd_config.msd_ac_minute_color_r = 0;
	} else {
		panel->msd_config.msd_ac_minute_color_r = val;
	}

	rc = utils->read_u32(utils->data, "oplus,msd-ac-minute-color-g", &val);
	if (rc) {
		pr_err("[%s] msd-ac-minute-color-g unspecified, defaulting to zero\n",
			 panel->name);
		panel->msd_config.msd_ac_minute_color_g = 0;
	} else {
		panel->msd_config.msd_ac_minute_color_g = val;
	}

	rc = utils->read_u32(utils->data, "oplus,msd-ac-minute-color-b", &val);
	if (rc) {
		pr_err("[%s] msd-ac-minute-color-b unspecified, defaulting to zero\n",
			 panel->name);
		panel->msd_config.msd_ac_minute_color_b = 0;
	} else {
		panel->msd_config.msd_ac_minute_color_b = val;
	}

	rc = utils->read_u32(utils->data, "oplus,msd-ac-second-color-r", &val);
	if (rc) {
		pr_err("[%s] msd-ac-second-color-r unspecified, defaulting to zero\n",
			 panel->name);
		panel->msd_config.msd_ac_second_color_r = 0;
	} else {
		panel->msd_config.msd_ac_second_color_r = val;
	}

	rc = utils->read_u32(utils->data, "oplus,msd-ac-second-color-g", &val);
	if (rc) {
		pr_err("[%s] msd-ac-second-color-g unspecified, defaulting to zero\n",
			 panel->name);
		panel->msd_config.msd_ac_second_color_g = 0;
	} else {
		panel->msd_config.msd_ac_second_color_g = val;
	}

	rc = utils->read_u32(utils->data, "oplus,msd-ac-second-color-b", &val);
	if (rc) {
		pr_err("[%s] msd-ac-second-color-b unspecified, defaulting to zero\n",
			 panel->name);
		panel->msd_config.msd_ac_second_color_b = 0;
	} else {
		panel->msd_config.msd_ac_second_color_b = val;
	}

	rc = utils->read_u32(utils->data, "oplus,msd-ac-hms-mask", &val);
	if (rc) {
		pr_err("[%s] oplus,msd-ac-hms-mask unspecified, defaulting to zero\n",
			 panel->name);
		panel->msd_config.msd_ac_hms_mask = 0;
	} else {
		panel->msd_config.msd_ac_hms_mask = val;
	}

	rc = utils->read_u32(utils->data, "oplus,msd-ac-hms-priority", &val);
	if (rc) {
		pr_err("[%s] oplus,msd-ac-hms-priority unspecified, defaulting to zero\n",
			 panel->name);
		panel->msd_config.msd_ac_hms_priority = 0;
	} else {
		panel->msd_config.msd_ac_hms_priority = val;
	}

	rc = utils->read_u32(utils->data, "oplus,msd-ac-gradation-on", &val);
	if (rc) {
		pr_err("[%s] oplus,msd-ac-gradation-on unspecified, defaulting to zero\n",
			 panel->name);
		panel->msd_config.msd_ac_gradation_on = 0;
	} else {
		panel->msd_config.msd_ac_gradation_on = val;
	}

	rc = utils->read_u32(utils->data, "oplus,msd-ac-gradation-2nd_r", &val);
	if (rc) {
		pr_err("[%s] oplus,msd-ac-gradation-2nd_r unspecified, defaulting to zero\n",
			 panel->name);
		panel->msd_config.msd_ac_gradation_2nd_r = 0;
	} else {
		panel->msd_config.msd_ac_gradation_2nd_r = val;
	}

	rc = utils->read_u32(utils->data, "oplus,msd-ac-gradation-2nd_g", &val);
	if (rc) {
		pr_err("[%s] oplus,msd-ac-gradation-2nd_g unspecified, defaulting to zero\n",
			 panel->name);
		panel->msd_config.msd_ac_gradation_2nd_g = 0;
	} else {
		panel->msd_config.msd_ac_gradation_2nd_g = val;
	}

	rc = utils->read_u32(utils->data, "oplus,msd-ac-gradation-2nd_b", &val);
	if (rc) {
		pr_err("[%s] oplus,msd-ac-gradation-2nd_b unspecified, defaulting to zero\n",
			 panel->name);
		panel->msd_config.msd_ac_gradation_2nd_b = 0;
	} else {
		panel->msd_config.msd_ac_gradation_2nd_b = val;
	}

	rc = utils->read_u32(utils->data, "oplus,msd-ac-gradation-3nd_r", &val);
	if (rc) {
		pr_err("[%s] oplus,msd-ac-gradation-3nd_r unspecified, defaulting to zero\n",
			 panel->name);
		panel->msd_config.msd_ac_gradation_3nd_r = 0;
	} else {
		panel->msd_config.msd_ac_gradation_3nd_r = val;
	}

	rc = utils->read_u32(utils->data, "oplus,msd-ac-gradation-3nd_g", &val);
	if (rc) {
		pr_err("[%s] oplus,msd-ac-gradation-3nd_g unspecified, defaulting to zero\n",
			 panel->name);
		panel->msd_config.msd_ac_gradation_3nd_g = 0;
	} else {
		panel->msd_config.msd_ac_gradation_3nd_g = val;
	}

	rc = utils->read_u32(utils->data, "oplus,msd-ac-gradation-3nd_b", &val);
	if (rc) {
		pr_err("[%s] oplus,msd-ac-gradation-3nd_b unspecified, defaulting to zero\n",
			 panel->name);
		panel->msd_config.msd_ac_gradation_3nd_b = 0;
	} else {
		panel->msd_config.msd_ac_gradation_3nd_b = val;
	}

	rc = utils->read_u32(utils->data, "oplus,msd-ac-hour", &val);
	if (rc) {
		pr_err("[%s] msd-ac-hour unspecified, defaulting to zero\n",
			 panel->name);
		panel->msd_config.hour = 0;
	} else {
		panel->msd_config.hour = val;
	}

	rc = utils->read_u32(utils->data, "oplus,msd-ac-minute", &val);
	if (rc) {
		pr_err("[%s] msd-ac-minute unspecified, defaulting to zero\n",
			 panel->name);
		panel->msd_config.minute = 0;
	} else {
		panel->msd_config.minute = val;
	}

	rc = utils->read_u32(utils->data, "oplus,msd-ac-second", &val);
	if (rc) {
		pr_err("[%s] msd-ac-second unspecified, defaulting to zero\n",
			 panel->name);
		panel->msd_config.second = 0;
	} else {
		panel->msd_config.second = val;
	}

	return rc;
}

bool oplus_is_msd_aod_supported(void)
{
	return msd_aod_support;
}

int oplus_display_update_msd_clock_location(struct dsi_cmd_desc *cmds,
		enum dsi_cmd_set_type type,
		struct dsi_panel *panel)
{
	int rc = 0;
	u8 *payload;

	if (!panel) {
		pr_err("Invalid Params\n");
		rc = -ENOTSUPP;
		goto error;
	}

	/* Update msd_clock_location info */
	payload = (u8 *)cmds[7].msg.tx_buf;
	if (payload) {
		payload[0] = OPLUS_MSD_EC_REGISTER;
		payload[1] = (GET_BIT(panel->msd_config.msd_ac_start_point_x,9,11) << 4)
					| GET_BIT(panel->msd_config.msd_ac_start_point_y,9,12);
		payload[2] = GET_BIT(panel->msd_config.msd_ac_start_point_x,1,8);
		payload[3] = GET_BIT(panel->msd_config.msd_ac_start_point_y,1,8);

		payload[4] = (GET_BIT(panel->msd_config.msd_ac_width,9,11) << 4)
					| GET_BIT(panel->msd_config.msd_ac_height,9,12);
		payload[5] = GET_BIT(panel->msd_config.msd_ac_width,1,8);
		payload[6] = GET_BIT(panel->msd_config.msd_ac_height,1,8);

		payload[7] = (GET_BIT(panel->msd_config.msd_ac_center_x,9,11) << 4)
					| GET_BIT(panel->msd_config.msd_ac_center_y,9,12);
		payload[8] = GET_BIT(panel->msd_config.msd_ac_center_x,1,8);
		payload[9] = GET_BIT(panel->msd_config.msd_ac_center_y,1,8);
	}

error:
	return rc;
}
EXPORT_SYMBOL(oplus_display_update_msd_clock_location);

int oplus_display_update_msd_hand_length(struct dsi_cmd_desc *cmds,
		enum dsi_cmd_set_type type,
		struct dsi_panel *panel)
{
	int rc = 0;
	u8 *payload;

	if (!panel) {
		pr_err("Invalid Params\n");
		rc = -ENOTSUPP;
		goto error;
	}

	/* Update msd_hand_length info */
	payload = (u8 *)cmds[9].msg.tx_buf;
	if (payload) {
		payload[0] = OPLUS_MSD_EC_REGISTER;
		payload[1] = (GET_BIT(panel->msd_config.msd_ac_start_distance,9,10) << 6)
					| (GET_BIT(panel->msd_config.msd_ac_end_distance_hour,9,10) << 4)
					| (GET_BIT(panel->msd_config.msd_ac_end_distance_minute,9,10) << 2)
					| GET_BIT(panel->msd_config.msd_ac_end_distance_second,9,10);
		payload[2] = GET_BIT(panel->msd_config.msd_ac_start_distance,1,8);
		payload[3] = GET_BIT(panel->msd_config.msd_ac_end_distance_hour,1,8);
		payload[4] = GET_BIT(panel->msd_config.msd_ac_end_distance_minute,1,8);
		payload[5] = GET_BIT(panel->msd_config.msd_ac_end_distance_second,1,8);
	}

error:
	return rc;
}
EXPORT_SYMBOL(oplus_display_update_msd_hand_length);

int oplus_display_update_msd_hand_backward_length(struct dsi_cmd_desc *cmds,
		enum dsi_cmd_set_type type,
		struct dsi_panel *panel)
{
	int rc = 0;
	u8 *payload;

	if (!panel) {
		pr_err("Invalid Params\n");
		rc = -ENOTSUPP;
		goto error;
	}

	/* Update msd_hand_length info */
	payload = (u8 *)cmds[11].msg.tx_buf;
	if (payload) {
		payload[0] = OPLUS_MSD_EC_REGISTER;
		payload[1] = (GET_BIT(panel->msd_config.msd_ac_opposite_distance_hour,9,9) << 6)
					| (GET_BIT(panel->msd_config.msd_ac_opposite_distance_minute,9,9) << 5)
					| (GET_BIT(panel->msd_config.msd_ac_opposite_distance_second,9,9) << 4)
					| GET_BIT(panel->msd_config.msd_ac_thickness_second,1,4);
		payload[2] = GET_BIT(panel->msd_config.msd_ac_opposite_distance_hour,1,8);
		payload[3] = GET_BIT(panel->msd_config.msd_ac_opposite_distance_minute,1,8);
		payload[4] = GET_BIT(panel->msd_config.msd_ac_opposite_distance_second,1,8);
	}

error:
	return rc;
}
EXPORT_SYMBOL(oplus_display_update_msd_hand_backward_length);

int oplus_display_update_msd_hand_thickness(struct dsi_cmd_desc *cmds,
		enum dsi_cmd_set_type type,
		struct dsi_panel *panel)
{
	int rc = 0;
	u8 *payload;

	if (!panel) {
		pr_err("Invalid Params\n");
		rc = -ENOTSUPP;
		goto error;
	}

	/* Update msd_hand_thickness info */
	payload = (u8 *)cmds[13].msg.tx_buf;
	if (payload) {
		payload[0] = OPLUS_MSD_EC_REGISTER;
		payload[1] = (GET_BIT(panel->msd_config.msd_ac_thickness_hour,1,4) << 4)
					| GET_BIT(panel->msd_config.msd_ac_thickness_minute,1,4);
		payload[2] = (GET_BIT(panel->msd_config.msd_ac_opposite_distance_hour,9,9) << 6)
					| (GET_BIT(panel->msd_config.msd_ac_opposite_distance_minute,9,9) << 5)
					| (GET_BIT(panel->msd_config.msd_ac_opposite_distance_second,9,9) << 4)
					| GET_BIT(panel->msd_config.msd_ac_thickness_second,1,4);
	}

error:
	return rc;
}
EXPORT_SYMBOL(oplus_display_update_msd_hand_thickness);

int oplus_display_update_msd_hand_rotate_resolution(struct dsi_cmd_desc *cmds,
		enum dsi_cmd_set_type type,
		struct dsi_panel *panel)
{
	int rc = 0;
	u8 *payload;

	if (!panel) {
		pr_err("Invalid Params\n");
		rc = -ENOTSUPP;
		goto error;
	}

	/* Update msd_hand_rotate_resolution info */
	payload = (u8 *)cmds[15].msg.tx_buf;
	if (payload) {
		payload[0] = OPLUS_MSD_EC_REGISTER;
		payload[1] = (GET_BIT(panel->msd_config.msd_ac_resolution_hour,1,3) << 3)
					| GET_BIT(panel->msd_config.msd_ac_resolution_minute,1,3);
		payload[2] = GET_BIT(panel->msd_config.msd_ac_resolution_second,1,3) << 3
					| GET_BIT(panel->msd_config.msd_ac_gradation_on,1,2);
	}

error:
	return rc;
}
EXPORT_SYMBOL(oplus_display_update_msd_hand_rotate_resolution);

int oplus_display_update_msd_hand_color(struct dsi_cmd_desc *cmds,
		enum dsi_cmd_set_type type,
		struct dsi_panel *panel)
{
	int rc = 0;
	u8 *payload;

	if (!panel) {
		pr_err("Invalid Params\n");
		rc = -ENOTSUPP;
		goto error;
	}

	/* Update msd_hand_color info */
	payload = (u8 *)cmds[17].msg.tx_buf;
	if (payload) {
		payload[0] = OPLUS_MSD_EC_REGISTER;
		payload[1] = (GET_BIT(panel->msd_config.msd_ac_hour_color_r,9,10) << 4)
					| (GET_BIT(panel->msd_config.msd_ac_hour_color_g,9,10) << 2)
					| GET_BIT(panel->msd_config.msd_ac_hour_color_b,9,10);
		payload[2] = GET_BIT(panel->msd_config.msd_ac_hour_color_r,1,8);
		payload[3] = GET_BIT(panel->msd_config.msd_ac_hour_color_g,1,8);
		payload[4] = GET_BIT(panel->msd_config.msd_ac_hour_color_b,1,8);

		payload[5] = (GET_BIT(panel->msd_config.msd_ac_minute_color_r,9,10) << 4)
					| (GET_BIT(panel->msd_config.msd_ac_minute_color_g,9,10) << 2)
					| GET_BIT(panel->msd_config.msd_ac_minute_color_b,9,10);
		payload[6] = GET_BIT(panel->msd_config.msd_ac_minute_color_r,1,8);
		payload[7] = GET_BIT(panel->msd_config.msd_ac_minute_color_g,1,8);
		payload[8] = GET_BIT(panel->msd_config.msd_ac_minute_color_b,1,8);

		payload[9] = (GET_BIT(panel->msd_config.msd_ac_second_color_r,9,10) << 4)
					| (GET_BIT(panel->msd_config.msd_ac_second_color_g,9,10) << 2)
					| GET_BIT(panel->msd_config.msd_ac_second_color_b,9,10);
		payload[10] = GET_BIT(panel->msd_config.msd_ac_second_color_r,1,8);
		payload[11] = GET_BIT(panel->msd_config.msd_ac_second_color_g,1,8);
		payload[12] = GET_BIT(panel->msd_config.msd_ac_second_color_b,1,8);
	}

error:
	return rc;
}
EXPORT_SYMBOL(oplus_display_update_msd_hand_color);

int oplus_display_update_msd_hand_hms_priority(struct dsi_cmd_desc *cmds,
		enum dsi_cmd_set_type type,
		struct dsi_panel *panel)
{
	int rc = 0;
	u8 *payload;

	if (!panel) {
		pr_err("Invalid Params\n");
		rc = -ENOTSUPP;
		goto error;
	}

	/* Update msd_hand_thickness info */
	payload = (u8 *)cmds[19].msg.tx_buf;
	if (payload) {
		payload[0] = OPLUS_MSD_EC_REGISTER;
		payload[1] = (GET_BIT(panel->msd_config.msd_ac_hms_priority,1,3) << 3)
					| GET_BIT(panel->msd_config.msd_ac_hms_mask,1,3);
	}

error:
	return rc;
}
EXPORT_SYMBOL(oplus_display_update_msd_hand_hms_priority);

int oplus_display_update_msd_hand_gradation(struct dsi_cmd_desc *cmds,
		enum dsi_cmd_set_type type,
		struct dsi_panel *panel)
{
	int rc = 0;
	u8 *payload;

	if (!panel) {
		pr_err("Invalid Params\n");
		rc = -ENOTSUPP;
		goto error;
	}

	/* Update msd_hand_thickness info */
	payload = (u8 *)cmds[21].msg.tx_buf;
	if (payload) {
		payload[0] = OPLUS_MSD_EC_REGISTER;
		payload[1] = GET_BIT(panel->msd_config.msd_ac_resolution_second,1,3) << 3
					| GET_BIT(panel->msd_config.msd_ac_gradation_on,1,2);
		payload[2] = (GET_BIT(panel->msd_config.msd_ac_gradation_2nd_r,9,10) << 4)
					| (GET_BIT(panel->msd_config.msd_ac_gradation_2nd_g,9,10) << 2)
					| GET_BIT(panel->msd_config.msd_ac_gradation_2nd_b,9,10);
		payload[3] = GET_BIT(panel->msd_config.msd_ac_gradation_2nd_r,1,8);
		payload[4] = GET_BIT(panel->msd_config.msd_ac_gradation_2nd_g,1,8);
		payload[5] = GET_BIT(panel->msd_config.msd_ac_gradation_2nd_b,1,8);

		payload[6] = (GET_BIT(panel->msd_config.msd_ac_gradation_3nd_r,9,10) << 4)
					| (GET_BIT(panel->msd_config.msd_ac_gradation_3nd_g,9,10) << 2)
					| GET_BIT(panel->msd_config.msd_ac_gradation_3nd_b,9,10);
		payload[7] = GET_BIT(panel->msd_config.msd_ac_gradation_3nd_r,1,8);
		payload[8] = GET_BIT(panel->msd_config.msd_ac_gradation_3nd_g,1,8);
		payload[9] = GET_BIT(panel->msd_config.msd_ac_gradation_3nd_b,1,8);
	}

error:
	return rc;
}
EXPORT_SYMBOL(oplus_display_update_msd_hand_gradation);

int oplus_display_update_msd_time(struct dsi_cmd_desc *cmds,
		enum dsi_cmd_set_type type,
		struct dsi_panel *panel)
{
	int rc = 0;
	u8 *payload;

	if (!panel) {
		pr_err("Invalid Params\n");
		rc = -ENOTSUPP;
		goto error;
	}

	/* Update msd_time info */
	payload = (u8 *)cmds[22].msg.tx_buf;
	if (payload) {
		payload[0] = OPLUS_MSD_AC_REGISTER;
		payload[1] = (GET_BIT(panel->msd_config.second,11,11) << 6)
					| GET_BIT(panel->msd_config.hour,1,6);
		payload[2] = (GET_BIT(panel->msd_config.second,9,10) << 6)
					| GET_BIT(panel->msd_config.minute,1,6);
		payload[3] = GET_BIT(panel->msd_config.second,1,8);
		payload[4] = 0x01;
	}

error:
	return rc;
}
EXPORT_SYMBOL(oplus_display_update_msd_time);

int oplus_display_update_msd_aod(struct dsi_panel *panel)
{
	int rc = 0;
	int count;
	struct dsi_cmd_desc *cmds;
	struct dsi_display *dsi_display = get_main_display();

	if (!panel) {
		pr_err("Invalid Params\n");
		rc = -ENOTSUPP;
		goto error;
	}

	mutex_lock(&panel->panel_lock);

	if (!dsi_panel_initialized(panel))
		goto error;

	count = panel->cur_mode->priv_info->cmd_sets[DSI_CMD_SET_MSD_AOD_ON].count;
	cmds = panel->cur_mode->priv_info->cmd_sets[DSI_CMD_SET_MSD_AOD_ON].cmds;
	if (count == 0) {
		DSI_ERR("This panel does not support msd aod.\n");
		goto error;
	}

	rc = oplus_display_update_msd_clock_location(cmds, DSI_CMD_SET_MSD_AOD_ON, panel);
	rc = oplus_display_update_msd_hand_length(cmds, DSI_CMD_SET_MSD_AOD_ON, panel);
	rc = oplus_display_update_msd_hand_backward_length(cmds, DSI_CMD_SET_MSD_AOD_ON, panel);
	rc = oplus_display_update_msd_hand_thickness(cmds, DSI_CMD_SET_MSD_AOD_ON, panel);
	rc = oplus_display_update_msd_hand_rotate_resolution(cmds, DSI_CMD_SET_MSD_AOD_ON, panel);
	rc = oplus_display_update_msd_hand_color(cmds, DSI_CMD_SET_MSD_AOD_ON, panel);
	rc = oplus_display_update_msd_hand_hms_priority(cmds, DSI_CMD_SET_MSD_AOD_ON, panel);
	rc = oplus_display_update_msd_hand_gradation(cmds, DSI_CMD_SET_MSD_AOD_ON, panel);
	rc = oplus_display_update_msd_time(cmds, DSI_CMD_SET_MSD_AOD_ON, panel);
	if (rc) {
		pr_err("Failed to update msd aod cmds, rc=%d\n", rc);
		goto error;
	}

	rc = dsi_display_clk_ctrl(dsi_display->dsi_clk_handle,
			DSI_CORE_CLK, DSI_CLK_ON);
	if (rc) {
		pr_err("[%s] failed to enable DSI core clocks, rc=%d\n",
			dsi_display->name, rc);
		goto error;
	}

	rc = oplus_panel_send_msd_aod_command(panel);
	if (rc)
		pr_err("Failed to send msd aod command\n");

	rc = dsi_display_clk_ctrl(dsi_display->dsi_clk_handle,
			DSI_CORE_CLK, DSI_CLK_OFF);
	if (rc) {
		pr_err("[%s] failed to disable DSI core clocks, rc=%d\n",
			dsi_display->name, rc);
		goto error;
	}

error:
	mutex_unlock(&panel->panel_lock);
	return rc;
}
EXPORT_SYMBOL(oplus_display_update_msd_aod);

int oplus_display_get_mipi_dsi_msg(const struct mipi_dsi_msg *msg, char* buf)
{
	int len = 0;
	size_t i;
	char *tx_buf = (char*)msg->tx_buf;
	/* Packet Info */
	len += snprintf(buf + len, PAGE_SIZE - len, "%02X ", msg->type);
	/* Last bit */
	len += snprintf(buf + len, PAGE_SIZE - len, "%02X ", (msg->flags & MIPI_DSI_MSG_LASTCOMMAND) ? 1 : 0);
	len += snprintf(buf + len, PAGE_SIZE - len, "%02X ", msg->channel);
	len += snprintf(buf + len, PAGE_SIZE - len, "%02X ", (unsigned int)msg->flags);
	/* Delay */
	len += snprintf(buf + len, PAGE_SIZE - len, "%02X ", msg->wait_ms);
	len += snprintf(buf + len, PAGE_SIZE - len, "%02X %02X ", msg->tx_len >> 8, msg->tx_len & 0x00FF);

	/* Packet Payload */
	for (i = 0 ; i < msg->tx_len ; i++) {
		len += snprintf(buf + len, PAGE_SIZE - len, "%02X ", tx_buf[i]);
	}
	len += snprintf(buf + len, PAGE_SIZE - len, "\n");

	return len;
}

int oplus_display_get_msd_aod_command(char *buf)
{
	int i = 0;
	int count = 0;
	struct dsi_panel_cmd_set *cmd;
	struct dsi_display *dsi_display = get_main_display();

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return 0;

	cmd = &dsi_display->panel->cur_mode->priv_info->cmd_sets[DSI_CMD_SET_MSD_AOD_ON];

	for (i = 0; i < cmd->count; i++) {
		count += oplus_display_get_mipi_dsi_msg(&cmd->cmds[i].msg, &buf[count]);
	}

	return count;
}
EXPORT_SYMBOL(oplus_display_get_msd_aod_command);