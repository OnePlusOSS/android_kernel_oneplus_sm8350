/***************************************************************
** Copyright (C),  2020,  OnePlus Mobile Comm Corp.,  Ltd
** File : oplus_display_node.c
** Description : OPlus display node
** Version : 1.0
** Date : 2020/09/03
**
** ------------------------------- Revision History: -----------
**  <author>        <data>        <version >        <desc>
**  xxxxx.xxx       2020/09/03        1.0           Build this moudle
******************************************************************/
#include "oplus_display_node.h"
#include <linux/device.h>
#include "drm_mipi_dsi.h"
#include "oplus_display_private_api.h"
#include "oplus_onscreenfingerprint.h"
#include "oplus_msd_aod.h"

#define to_drm_connector(d) dev_get_drvdata(d)


int dsi_cmd_log_enable;
EXPORT_SYMBOL(dsi_cmd_log_enable);

extern ssize_t mipi_dsi_dcs_write(struct mipi_dsi_device *dsi, u8 cmd,
			   const void *data, size_t len);


int mipi_dsi_dcs_write_c1(struct mipi_dsi_device *dsi,
						u16 read_number)
{
#if 0

	u8 payload[3] = {0x0A, read_number >> 8, read_number & 0xff};
	ssize_t err;

	err = mipi_dsi_dcs_write(dsi, 0xC1, payload, sizeof(payload));
	if (err < 0)
		return err;
#endif
	return 0;
}
EXPORT_SYMBOL(mipi_dsi_dcs_write_c1);

static ssize_t acl_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct drm_connector *connector = to_drm_connector(dev);
	int ret = 0;
	int acl_mode = 0;

	acl_mode = dsi_display_get_acl_mode(connector);

	ret = scnprintf(buf, PAGE_SIZE, "acl mode = %d\n"
									"0--acl mode(off)\n"
									"1--acl mode(5)\n"
									"2--acl mode(10)\n"
									"3--acl mode(15)\n",
									acl_mode);
	return ret;
}

static ssize_t acl_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct drm_connector *connector = to_drm_connector(dev);
	int ret = 0;
	int acl_mode = 0;

	ret = kstrtoint(buf, 10, &acl_mode);
	if (ret) {
		pr_err("kstrtoint failed. ret=%d\n", ret);
		return ret;
	}

	ret = dsi_display_set_acl_mode(connector, acl_mode);
	if (ret)
		pr_err("set acl mode(%d) fail\n", acl_mode);

	return count;
}
static ssize_t hbm_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct drm_connector *connector = to_drm_connector(dev);
	int ret = 0;
	int hbm_mode = 0;

	hbm_mode = dsi_display_get_hbm_mode(connector);

	ret = scnprintf(buf, PAGE_SIZE, "hbm mode = %d\n"
											"0--hbm mode(off)\n"
											"1--hbm mode(XX)\n"
											"2--hbm mode(XX)\n"
											"3--hbm mode(XX)\n"
											"4--hbm mode(XX)\n"
											"5--hbm mode(670)\n",
											hbm_mode);
	return ret;
}

static ssize_t hbm_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct drm_connector *connector = to_drm_connector(dev);
	int ret = 0;
	int hbm_mode = 0;
	int panel_stage_info = 0;

	ret = kstrtoint(buf, 10, &hbm_mode);
	if (ret) {
		pr_err("kstrtoint failed. ret=%d\n", ret);
		return ret;
	}

	if (dsi_panel_name == DSI_PANEL_SAMSUNG_ANA6705) {
		panel_stage_info = dsi_display_get_stage_info(connector);
		if (((panel_stage_info == 0x02) || (panel_stage_info == 0x03)
			|| (panel_stage_info == 0x04)) && (hbm_mode == 4)) {
			hbm_mode = hbm_mode - 1;
		} else {
			pr_err("19821 panel stage version is T0/DVT2/PVT&MP");
		}
	}
	ret = dsi_display_set_hbm_mode(connector, hbm_mode);
	if (ret)
		pr_err("set hbm mode(%d) fail\n", hbm_mode);

	return count;
}

static ssize_t seed_lp_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct drm_connector *connector = to_drm_connector(dev);
	int ret = 0;
	int seed_lp_mode = 0;

	ret = kstrtoint(buf, 10, &seed_lp_mode);
	if (ret) {
		pr_err("kstrtoint failed. ret=%d\n", ret);
		return ret;
	}

	if ((dsi_panel_name == DSI_PANEL_SAMSUNG_ANA6706) ||
		(dsi_panel_name == DSI_PANEL_SAMSUNG_ANA6705)) {
		ret = dsi_display_set_seed_lp_mode(connector, seed_lp_mode);
		if (ret)
			pr_err("set seed lp (%d) fail\n", seed_lp_mode);
	}

	return count;
}
static ssize_t seed_lp_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct drm_connector *connector = to_drm_connector(dev);
	int ret = 0;
	int seed_lp_mode = 0;

	if ((dsi_panel_name == DSI_PANEL_SAMSUNG_ANA6706) ||
		(dsi_panel_name == DSI_PANEL_SAMSUNG_ANA6705)) {
		seed_lp_mode = dsi_display_get_seed_lp_mode(connector);
	}
	ret = scnprintf(buf, PAGE_SIZE, "seed lp mode = %d\n"
									"4--seed lp mode(off)\n"
									"0--seed lp mode(mode0)\n"
									"1--seed lp mode(mode1)\n"
									"2--seed lp mode(mode2)\n",
									seed_lp_mode);
	return ret;
}
static ssize_t hbm_brightness_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct drm_connector *connector = to_drm_connector(dev);
	int ret = 0;
	int hbm_brightness = 0;

	hbm_brightness = dsi_display_get_hbm_brightness(connector);

	ret = scnprintf(buf, PAGE_SIZE, "%d\n", hbm_brightness);
	return ret;
}

static ssize_t hbm_brightness_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct drm_connector *connector = to_drm_connector(dev);
	int ret = 0;
	int hbm_brightness = 0;
	int panel_stage_info = 0;

	ret = kstrtoint(buf, 10, &hbm_brightness);
	if (ret) {
		pr_err("kstrtoint failed. ret=%d\n", ret);
		return ret;
	}
	if (dsi_panel_name == DSI_PANEL_SAMSUNG_ANA6705) {
		panel_stage_info = dsi_display_get_stage_info(connector);
		if (((panel_stage_info == 0x02) || (panel_stage_info == 0x03)
			|| (panel_stage_info == 0x04))) {
			hbm_brightness = hbm_brightness + 380;
		} else {
			pr_err("19821 panel stage version is T0/DVT2/PVT&MP");
		}
	}
	ret = dsi_display_set_hbm_brightness(connector, hbm_brightness);
	if (ret)
		pr_err("set hbm brightness (%d) failed\n", hbm_brightness);
	return count;
}

static ssize_t op_friginer_print_hbm_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct drm_connector *connector = to_drm_connector(dev);
	int ret = 0;
	int op_hbm_mode = 0;

	op_hbm_mode = dsi_display_get_fp_hbm_mode(connector);

	ret = scnprintf(buf, PAGE_SIZE, "OP_FP mode = %d\n"
									"0--finger-hbm mode(off)\n"
									"1--finger-hbm mode(600)\n",
									op_hbm_mode);
	return ret;
}

static ssize_t op_friginer_print_hbm_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct drm_connector *connector = to_drm_connector(dev);
	int ret = 0;
	int op_hbm_mode = 0;

	ret = kstrtoint(buf, 10, &op_hbm_mode);
	if (ret) {
		pr_err("kstrtoint failed. ret=%d\n", ret);
		return ret;
	}

	ret = dsi_display_set_fp_hbm_mode(connector, op_hbm_mode);
	if (ret)
		pr_err("set hbm mode(%d) fail\n", op_hbm_mode);

	return count;
}

static ssize_t oplus_msd_aod_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int ret = 0;

	ret = oplus_display_get_msd_aod_command(buf);

	return ret;
}

static ssize_t oplus_msd_aod_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int input[41];
	int ret = 0;
	struct dsi_panel *panel = NULL;
	struct dsi_display *display = get_main_display();

	if (!display) {
		return -ENODEV;
	}

	if (!display->panel || !display->drm_conn) {
		return -EINVAL;
	}

	panel = display->panel;

	if (sscanf(buf, "%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d\
		%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d",
		&input[0], &input[1], &input[2], &input[3],
		&input[4], &input[5], &input[6], &input[7],
		&input[8], &input[9], &input[10], &input[11],
		&input[12], &input[13], &input[14], &input[15],
		&input[16], &input[17], &input[18], &input[19],
		&input[20], &input[21], &input[22], &input[23],
		&input[24], &input[25], &input[26], &input[27],
		&input[28], &input[29], &input[30], &input[31],
		&input[32], &input[33], &input[34], &input[35],
		&input[36], &input[37], &input[38], &input[39],
		&input[40]) != 41) {
		pr_err("input wrong oplus msd aod parameters\n");
		return count;
	}

	/* Get msd switch info */
	panel->msd_config.msd_mode = input[0];

	/* Get msd_clock_location info */
	panel->msd_config.msd_ac_start_point_x = input[1];
	panel->msd_config.msd_ac_start_point_y = input[2];
	panel->msd_config.msd_ac_width = input[3];
	panel->msd_config.msd_ac_height = input[4];
	panel->msd_config.msd_ac_center_x = input[5];
	panel->msd_config.msd_ac_center_y = input[6];

	/* Get msd_hand_length info */
	panel->msd_config.msd_ac_start_distance = input[7];
	panel->msd_config.msd_ac_end_distance_hour = input[8];
	panel->msd_config.msd_ac_end_distance_minute = input[9];
	panel->msd_config.msd_ac_end_distance_second = input[10];

	/* Get msd_hand_backward_length info */
	panel->msd_config.msd_ac_opposite_distance_hour = input[11];
	panel->msd_config.msd_ac_opposite_distance_minute = input[12];
	panel->msd_config.msd_ac_opposite_distance_second = input[13];

	/* Get msd_hand_thickness info */
	panel->msd_config.msd_ac_thickness_hour = input[14];
	panel->msd_config.msd_ac_thickness_minute = input[15];
	panel->msd_config.msd_ac_thickness_second = input[16];

	/* Get msd_hand_rotate_resolution info */
	panel->msd_config.msd_ac_resolution_hour = input[17];
	panel->msd_config.msd_ac_resolution_minute = input[18];
	panel->msd_config.msd_ac_resolution_second = input[19];

	/* Get msd_hand_color info */
	panel->msd_config.msd_ac_hour_color_r = (input[20]*1023)/255;
	panel->msd_config.msd_ac_hour_color_g = (input[21]*1023)/255;
	panel->msd_config.msd_ac_hour_color_b = (input[22]*1023)/255;
	panel->msd_config.msd_ac_minute_color_r = (input[23]*1023)/255;
	panel->msd_config.msd_ac_minute_color_g = (input[24]*1023)/255;
	panel->msd_config.msd_ac_minute_color_b = (input[25]*1023)/255;
	panel->msd_config.msd_ac_second_color_r = (input[26]*1023)/255;
	panel->msd_config.msd_ac_second_color_g = (input[27]*1023)/255;
	panel->msd_config.msd_ac_second_color_b = (input[28]*1023)/255;

	/* Get msd_hand_hms_priority info */
	panel->msd_config.msd_ac_hms_mask = input[29];
	panel->msd_config.msd_ac_hms_priority = input[30];

	/* Get msd_hand_gradation info */
	panel->msd_config.msd_ac_gradation_on = input[31];
	panel->msd_config.msd_ac_gradation_2nd_r = input[32];
	panel->msd_config.msd_ac_gradation_2nd_g = input[33];
	panel->msd_config.msd_ac_gradation_2nd_b = input[34];
	panel->msd_config.msd_ac_gradation_3nd_r = input[35];
	panel->msd_config.msd_ac_gradation_3nd_g = input[36];
	panel->msd_config.msd_ac_gradation_3nd_b = input[37];

	/* Get msd_time info */
	panel->msd_config.hour = input[38];
	panel->msd_config.minute = input[39];
	panel->msd_config.second = input[40];

	ret = oplus_display_update_msd_aod(panel);
	if (ret) {
		pr_err("oplus display update msd aod failed. ret=%d\n", ret);
		return ret;
	}

	return count;
}
//#endif

static ssize_t aod_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct drm_connector *connector = to_drm_connector(dev);
	int ret = 0;
	int aod_mode = 0;

	aod_mode = dsi_display_get_aod_mode(connector);

	ret = scnprintf(buf, PAGE_SIZE, "%d\n", aod_mode);
	return ret;
}

static ssize_t aod_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct drm_connector *connector = to_drm_connector(dev);
	int ret = 0;
	int aod_mode = 0;

	ret = kstrtoint(buf, 10, &aod_mode);
	if (ret) {
		pr_err("kstrtoint failed. ret=%d\n", ret);
		return ret;
	}
	pr_err("node aod_mode=%d\n", aod_mode);
	ret = dsi_display_set_aod_mode(connector, aod_mode);
	if (ret)
		pr_err("set AOD mode(%d) fail\n", aod_mode);
	return count;
}

static ssize_t aod_disable_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct drm_connector *connector = to_drm_connector(dev);
	int ret = 0;
	int aod_disable = 0;

	aod_disable = dsi_display_get_aod_disable(connector);

	ret = scnprintf(buf, PAGE_SIZE, "AOD disable = %d\n"
									"0--AOD enable\n"
									"1--AOD disable\n",
									aod_disable);
	return ret;
}

static ssize_t aod_disable_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct drm_connector *connector = to_drm_connector(dev);
	int ret = 0;
	int aod_disable = 0;

	ret = kstrtoint(buf, 10, &aod_disable);
	if (ret) {
		pr_err("kstrtoint failed. ret=%d\n", ret);
		return ret;
	}

	ret = dsi_display_set_aod_disable(connector, aod_disable);
	if (ret)
		pr_err("set AOD disable(%d) fail\n", aod_disable);

	return count;
}

static ssize_t DCI_P3_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct drm_connector *connector = to_drm_connector(dev);
	int ret = 0;
	int dci_p3_mode = 0;

	dci_p3_mode = dsi_display_get_dci_p3_mode(connector);

	ret = scnprintf(buf, PAGE_SIZE, "dci-p3 mode = %d\n"
									"0--dci-p3 mode Off\n"
									"1--dci-p3 mode On\n",
									dci_p3_mode);
	return ret;
}

static ssize_t DCI_P3_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct drm_connector *connector = to_drm_connector(dev);
	int ret = 0;
	int dci_p3_mode = 0;

	ret = kstrtoint(buf, 10, &dci_p3_mode);
	if (ret) {
		pr_err("kstrtoint failed. ret=%d\n", ret);
		return ret;
	}

	ret = dsi_display_set_dci_p3_mode(connector, dci_p3_mode);
	if (ret)
		pr_err("set dci-p3 mode(%d) fail\n", dci_p3_mode);

	return count;
}

static ssize_t night_mode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct drm_connector *connector = to_drm_connector(dev);
	int ret = 0;
	int night_mode = 0;

	night_mode = dsi_display_get_night_mode(connector);

	ret = scnprintf(buf, PAGE_SIZE, "night mode = %d\n"
									"0--night mode Off\n"
									"1--night mode On\n",
									night_mode);
	return ret;
}

static ssize_t night_mode_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct drm_connector *connector = to_drm_connector(dev);
	int ret = 0;
	int night_mode = 0;

	ret = kstrtoint(buf, 10, &night_mode);
	if (ret) {
		pr_err("kstrtoint failed. ret=%d\n", ret);
		return ret;
	}

	ret = dsi_display_set_night_mode(connector, night_mode);
	if (ret)
		pr_err("set night mode(%d) fail\n", night_mode);

	return count;
}

static ssize_t native_display_p3_mode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct drm_connector *connector = to_drm_connector(dev);
	int ret = 0;
	int native_display_p3_mode = 0;

	native_display_p3_mode = dsi_display_get_native_display_p3_mode(connector);

	ret = scnprintf(buf, PAGE_SIZE, "native display p3 mode = %d\n"
									"0--native display p3 mode Off\n"
									"1--native display p3 mode On\n",
									native_display_p3_mode);
	return ret;
}

static ssize_t native_display_p3_mode_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct drm_connector *connector = to_drm_connector(dev);
	int ret = 0;
	int native_display_p3_mode = 0;

	ret = kstrtoint(buf, 10, &native_display_p3_mode);
	if (ret) {
		pr_err("kstrtoint failed. ret=%d\n", ret);
		return ret;
	}

	ret = dsi_display_set_native_display_p3_mode(connector, native_display_p3_mode);
	if (ret)
		pr_err("set native_display_p3  mode(%d) fail\n", native_display_p3_mode);

	return count;
}
static ssize_t native_display_wide_color_mode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct drm_connector *connector = to_drm_connector(dev);
	int ret = 0;
	int native_display_wide_color_mode = 0;

	native_display_wide_color_mode = dsi_display_get_native_display_wide_color_mode(connector);

	ret = scnprintf(buf, PAGE_SIZE, "native display wide color mode = %d\n"
									"0--native display wide color mode Off\n"
									"1--native display wide color mode On\n",
									native_display_wide_color_mode);
	return ret;
}

static ssize_t native_display_loading_effect_mode_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct drm_connector *connector = to_drm_connector(dev);
	int ret = 0;
	int native_display_loading_effect_mode = 0;

	ret = kstrtoint(buf, 10, &native_display_loading_effect_mode);
	if (ret) {
		pr_err("kstrtoint failed. ret=%d\n", ret);
		return ret;
	}

	ret = dsi_display_set_native_loading_effect_mode(connector, native_display_loading_effect_mode);
	if (ret)
		pr_err("set loading effect  mode(%d) fail\n", native_display_loading_effect_mode);

	return count;
}

static ssize_t native_display_loading_effect_mode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct drm_connector *connector = to_drm_connector(dev);
	int ret = 0;
	int native_display_loading_effect_mode = 0;

	native_display_loading_effect_mode = dsi_display_get_native_display_loading_effect_mode(connector);

	ret = scnprintf(buf, PAGE_SIZE, "native display loading effect mode = %d\n"
									"0--native display loading effect mode Off\n"
									"1--native display loading effect mode On_1\n"
									"2--native display loading effect mode On_2\n",
									native_display_loading_effect_mode);
	return ret;
}

static ssize_t native_display_customer_p3_mode_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct drm_connector *connector = to_drm_connector(dev);
	int ret = 0;
	int native_display_customer_p3_mode = 0;

	ret = kstrtoint(buf, 10, &native_display_customer_p3_mode);
	if (ret) {
		pr_err("kstrtoint failed. ret=%d\n", ret);
		return ret;
	}

	ret = dsi_display_set_customer_p3_mode(connector, native_display_customer_p3_mode);
	if (ret)
		pr_err("set customer p3  mode(%d) fail\n", native_display_customer_p3_mode);

	return count;
}

static ssize_t native_display_customer_p3_mode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct drm_connector *connector = to_drm_connector(dev);
	int ret = 0;
	int native_display_customer_p3_mode = 0;

	native_display_customer_p3_mode = dsi_display_get_customer_p3_mode(connector);

	ret = scnprintf(buf, PAGE_SIZE, "native display customer p3 mode = %d\n"
									"0--native display customer p3 mode Off\n"
									"1--native display customer p3 mode On\n",
									native_display_customer_p3_mode);
	return ret;
}
static ssize_t native_display_customer_srgb_mode_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct drm_connector *connector = to_drm_connector(dev);
	int ret = 0;
	int native_display_customer_srgb_mode = 0;

	ret = kstrtoint(buf, 10, &native_display_customer_srgb_mode);
	if (ret) {
		pr_err("kstrtoint failed. ret=%d\n", ret);
		return ret;
	}

	ret = dsi_display_set_customer_srgb_mode(connector, native_display_customer_srgb_mode);
	if (ret)
		pr_err("set customer srgb  mode(%d) fail\n", native_display_customer_srgb_mode);

	return count;
}

static ssize_t native_display_customer_srgb_mode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct drm_connector *connector = to_drm_connector(dev);
	int ret = 0;
	int native_display_customer_srgb_mode = 0;

	native_display_customer_srgb_mode = dsi_display_get_customer_srgb_mode(connector);

	ret = scnprintf(buf, PAGE_SIZE, "native display customer srgb mode = %d\n"
									"0--native display customer srgb mode Off\n"
									"1--native display customer srgb mode On\n",
									native_display_customer_srgb_mode);
	return ret;
}


static ssize_t native_display_wide_color_mode_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct drm_connector *connector = to_drm_connector(dev);
	int ret = 0;
	int native_display_wide_color_mode = 0;

	ret = kstrtoint(buf, 10, &native_display_wide_color_mode);
	if (ret) {
		pr_err("kstrtoint failed. ret=%d\n", ret);
		return ret;
	}

	ret = dsi_display_set_native_display_wide_color_mode(connector, native_display_wide_color_mode);
	if (ret)
		pr_err("set native_display_p3  mode(%d) fail\n", native_display_wide_color_mode);

	return count;
}

static ssize_t native_display_srgb_color_mode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct drm_connector *connector = to_drm_connector(dev);
	int ret = 0;
	int native_display_srgb_color_mode = 0;

	native_display_srgb_color_mode = dsi_display_get_native_display_srgb_color_mode(connector);

	ret = scnprintf(buf, PAGE_SIZE, "native display srgb color mode = %d\n"
									"0--native display srgb color mode Off\n"
									"1--native display srgb color mode On\n",
									native_display_srgb_color_mode);
	return ret;
}

static ssize_t native_display_srgb_color_mode_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct drm_connector *connector = to_drm_connector(dev);
	int ret = 0;
	int native_display_srgb_color_mode = 0;

	ret = kstrtoint(buf, 10, &native_display_srgb_color_mode);
	if (ret) {
		pr_err("kstrtoint failed. ret=%d\n", ret);
		return ret;
	}

	ret = dsi_display_set_native_display_srgb_color_mode(connector, native_display_srgb_color_mode);
	if (ret)
		pr_err("set native_display_srgb  mode(%d) fail\n", native_display_srgb_color_mode);

	return count;
}

static ssize_t gamma_test_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct drm_connector *connector = to_drm_connector(dev);
	int ret = 0;
	int gamma_test_flag = 0;
	int panel_stage_info = 0;
	int pvt_mp_panel_flag = 0;

	if (dsi_panel_name == DSI_PANEL_SAMSUNG_S6E3HC2) {
		ret = dsi_display_update_gamma_para(connector);
		if (ret)
			pr_err("Failed to update gamma para!\n");

		if ((gamma_para[0][18] == 0xFF) && (gamma_para[0][19] == 0xFF) && (gamma_para[0][20] == 0xFF))
			gamma_test_flag = 0;
		else
			gamma_test_flag = 1;

		dsi_display_get_serial_number(connector);
		panel_stage_info = dsi_display_get_stage_info(connector);

		if ((panel_stage_info == 0x07) || (panel_stage_info == 0x10) ||
			(panel_stage_info == 0x11) || (panel_stage_info == 0x16))
			pvt_mp_panel_flag = 1;
		else
			pvt_mp_panel_flag = 0;

		ret = scnprintf(buf, PAGE_SIZE, "%d\n", (gamma_test_flag << 1) + pvt_mp_panel_flag);
	} else if (dsi_panel_name == DSI_PANEL_SAMSUNG_SOFEF03F_M) {
		dsi_display_get_serial_number(connector);
		panel_stage_info = dsi_display_get_stage_info(connector);

		if (panel_stage_info == 0x27)
			pvt_mp_panel_flag = 1;
		else
			pvt_mp_panel_flag = 0;

		ret = scnprintf(buf, PAGE_SIZE, "%d\n", pvt_mp_panel_flag);
	} else {
		ret = scnprintf(buf, PAGE_SIZE, "%d\n", 3);
		pr_err("Gamma test is not supported!\n");
	}

	return ret;
}

static ssize_t panel_serial_number_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct drm_connector *connector = to_drm_connector(dev);
	int panel_year = 0;
	int panel_mon = 0;
	int panel_day = 0;
	int panel_hour = 0;
	int panel_min = 0;
	int panel_sec = 0;
	int panel_msec_int = 0;
	int panel_msec_rem = 0;
	int panel_code_info = 0;
	int panel_stage_info = 0;
	int panel_production_info = 0;
	int panel_ic_v_info = 0;
	int ddic_check_info = 0;
	int panel_tool = 0;
	char *panel_tool_result = NULL;
	char *production_string_info = NULL;
	char *stage_string_info = NULL;
	char *ddic_check_result = NULL;
	int ret = 0;

	dsi_display_get_serial_number(connector);

	panel_year = dsi_display_get_serial_number_year(connector);
	panel_mon = dsi_display_get_serial_number_mon(connector);
	panel_day = dsi_display_get_serial_number_day(connector);
	panel_hour = dsi_display_get_serial_number_hour(connector);
	panel_min = dsi_display_get_serial_number_min(connector);
	panel_sec = dsi_display_get_serial_number_sec(connector);
	panel_msec_int = dsi_display_get_serial_number_msec_int(connector);
	panel_msec_rem = dsi_display_get_serial_number_msec_rem(connector);
	panel_code_info = dsi_display_get_code_info(connector);
	panel_stage_info = dsi_display_get_stage_info(connector);
	panel_production_info = dsi_display_get_production_info(connector);
	panel_ic_v_info = dsi_display_get_panel_ic_v_info(connector);
	ddic_check_info = dsi_display_get_ddic_check_info(connector);
	panel_tool = dsi_display_get_ToolsType_ANA6706(connector);

	if (ddic_check_info == 1)
		ddic_check_result = "OK";
	else if (ddic_check_info == 0)
		ddic_check_result = "NG";

	if (dsi_panel_name == DSI_PANEL_SAMSUNG_S6E3HC2) {
		if (panel_code_info == 0xED) {
			if (panel_stage_info == 0x02)
				stage_string_info = "STAGE: EVT2";
			else if (panel_stage_info == 0x03)
				stage_string_info = "STAGE: EVT2(NEW_DIMMING_SET)";
			else if (panel_stage_info == 0x99)
				stage_string_info = "STAGE: EVT2(113MHZ_OSC)";
			else if (panel_stage_info == 0x04)
				stage_string_info = "STAGE: DVT1";
			else if (panel_stage_info == 0x05)
				stage_string_info = "STAGE: DVT2";
			else if (panel_stage_info == 0x06)
				stage_string_info = "STAGE: DVT3";
			else if (panel_stage_info == 0x07)
				stage_string_info = "STAGE: PVT/MP(112MHZ_OSC)";
			else if (panel_stage_info == 0x10)
				stage_string_info = "STAGE: PVT/MP(113MHZ_OSC)";
			else if (panel_stage_info == 0x11)
				stage_string_info = "STAGE: PVT(113MHZ_OSC+X_TALK_IMPROVEMENT)";
			else
				stage_string_info = "STAGE: UNKNOWN";

			if (panel_production_info == 0x0C)
				production_string_info = "TPIC: LSI\nCOVER: JNTC\nOTP_GAMMA: 90HZ";
			else if (panel_production_info == 0x0E)
				production_string_info = "TPIC: LSI\nCOVER: LENS\nOTP_GAMMA: 90HZ";
			else if (panel_production_info == 0x1C)
				production_string_info = "TPIC: STM\nCOVER: JNTC\nOTP_GAMMA: 90HZ";
			else if (panel_production_info == 0x6C)
				production_string_info = "TPIC: LSI\nCOVER: JNTC\nOTP_GAMMA: 60HZ";
			else if (panel_production_info == 0x6E)
				production_string_info = "TPIC: LSI\nCOVER: LENS\nOTP_GAMMA: 60HZ";
			else if (panel_production_info == 0x1E)
				production_string_info = "TPIC: STM\nCOVER: LENS\nOTP_GAMMA: 90HZ";
			else if (panel_production_info == 0x0D)
				production_string_info = "TPIC: LSI\nID3: 0x0D\nOTP_GAMMA: 90HZ";
			else
				production_string_info = "TPIC: UNKNOWN\nCOVER: UNKNOWN\nOTP_GAMMA: UNKNOWN";

			ret = scnprintf(buf, PAGE_SIZE, "%04d/%02d/%02d %02d:%02d:%02d\n%s\n%s\nID: %02X %02X %02X\n",
					panel_year, panel_mon, panel_day, panel_hour, panel_min, panel_sec,
						stage_string_info, production_string_info, panel_code_info,
							panel_stage_info, panel_production_info);
		}

		if (panel_code_info == 0xEE) {
			if (panel_stage_info == 0x12)
				stage_string_info = "STAGE: T0/EVT1";
			else if (panel_stage_info == 0x13)
				stage_string_info = "STAGE: EVT2";
			else if (panel_stage_info == 0x14)
				stage_string_info = "STAGE: EVT2";
			else if (panel_stage_info == 0x15)
				stage_string_info = "STAGE: EVT3";
			else if (panel_stage_info == 0x16)
				stage_string_info = "STAGE: DVT";
			else if (panel_stage_info == 0x17)
				stage_string_info = "STAGE: DVT";
			else if (panel_stage_info == 0x19)
				stage_string_info = "STAGE: PVT/MP";
			else
				stage_string_info = "STAGE: UNKNOWN";

			ret = scnprintf(buf, PAGE_SIZE, "%04d/%02d/%02d %02d:%02d:%02d\n%s\nID: %02X %02X %02X\n",
					panel_year, panel_mon, panel_day, panel_hour, panel_min, panel_sec,
						stage_string_info, panel_code_info, panel_stage_info,
							panel_production_info);
		}

	} else if (dsi_panel_name == DSI_PANEL_SAMSUNG_SOFEF03F_M) {
		if (panel_stage_info == 0x01)
			stage_string_info = "STAGE: T0";
		else if (panel_stage_info == 0x21)
			stage_string_info = "STAGE: EVT1";
		else if (panel_stage_info == 0x22)
			stage_string_info = "STAGE: EVT2";
		else if (panel_stage_info == 0x24)
			stage_string_info = "STAGE: DVT1-1";
		else if (panel_stage_info == 0x26)
			stage_string_info = "STAGE: DVT1-2";
		else if (panel_stage_info == 0x25)
			stage_string_info = "STAGE: DVT2";
		else if (panel_stage_info == 0x28)
			stage_string_info = "STAGE: DVT3";
		else if (panel_stage_info == 0x27)
			stage_string_info = "STAGE: PVT/MP";

		ret = scnprintf(buf, PAGE_SIZE, "%04d/%02d/%02d %02d:%02d:%02d\n%s\nID: %02X %02X %02X\n",
				panel_year, panel_mon, panel_day, panel_hour, panel_min, panel_sec, stage_string_info,
					panel_code_info, panel_stage_info, panel_production_info);
	} else if (dsi_panel_name == DSI_PANEL_SAMSUNG_ANA6705) {
		if (panel_stage_info == 0x01)
			stage_string_info = "STAGE: T0";
		else if (panel_stage_info == 0x02)
			stage_string_info = "STAGE: EVT1";
		else if (panel_stage_info == 0x03)
			stage_string_info = "STAGE: EVT2";
		else if (panel_stage_info == 0x04)
			stage_string_info = "STAGE: DVT1";
		else if (panel_stage_info == 0x05)
			stage_string_info = "STAGE: DVT2";
		else if (panel_stage_info == 0x06)
			stage_string_info = "STAGE: PVT/MP";

		ret = scnprintf(buf, PAGE_SIZE,
		"%04d/%02d/%02d\n%02d:%02d:%02d:%03d.%01d\n%s\nID: %02X %02X %02X\n DDIC_Check_Result: %s\n",
					panel_year, panel_mon, panel_day, panel_hour, panel_min,
						panel_sec, panel_msec_int, panel_msec_rem,
							stage_string_info, panel_code_info,
								panel_stage_info, panel_production_info,
								ddic_check_result);
	} else if (dsi_panel_name == DSI_PANEL_SAMSUNG_ANA6706) {
		if (panel_tool == 0)
			panel_tool_result = "ToolB";
		else if (panel_tool == 1)
			panel_tool_result = "ToolA";
		else if (panel_tool == 2)
			panel_tool_result = "ToolA_HVS30";
		else
			panel_tool_result = "Indistinguishable";

		if (panel_stage_info == 0x01)
			stage_string_info = "STAGE: T0";
		else if (panel_stage_info == 0x02)
			stage_string_info = "STAGE: EVT1-1";
		else if ((panel_stage_info == 0xA2) && (panel_ic_v_info == 1))
			stage_string_info = "STAGE: EVT2";
		else if ((panel_stage_info == 0xA3) && (panel_ic_v_info == 1))
			stage_string_info = "STAGE: EVT2-1";
		else if ((panel_stage_info == 0xA3) && (panel_ic_v_info == 0))
			stage_string_info = "STAGE: EVT2-2";
		else if (panel_stage_info == 0xA4)
			stage_string_info = "STAGE: DVT1";
		else if (panel_stage_info == 0xA5)
			stage_string_info = "STAGE: DVT2";
		else if (panel_stage_info == 0xA6)
			stage_string_info = "STAGE: PVT/MP";

		ret = scnprintf(buf, PAGE_SIZE,
		"%04d/%02d/%02d\n%02d:%02d:%02d:%03d.%01d\n%s\n ID: %02X %02X %02X\n"
		"IC_V: %02d\n DDIC_Check_Result: %s\n Tool: %s\n",
					panel_year, panel_mon, panel_day, panel_hour, panel_min,
						panel_sec, panel_msec_int, panel_msec_rem,
							stage_string_info, panel_code_info,
								panel_stage_info, panel_production_info,
								panel_ic_v_info, ddic_check_result,
								panel_tool_result);
	} else if (dsi_panel_name == DSI_PANEL_SAMSUNG_AMB655XL || dsi_panel_name == DSI_PANEL_SAMSUNG_AMB655XL08) {
		if (panel_stage_info == 0x01)
			stage_string_info = "STAGE: T0";
		else if (panel_stage_info == 0x02)
			stage_string_info = "STAGE: EVT1";
		else if (panel_stage_info == 0x03)
			stage_string_info = "STAGE: DVT1";
		else if (panel_stage_info == 0x04)
			stage_string_info = "STAGE: DVT2";
		else if (panel_stage_info == 0x05)
			stage_string_info = "STAGE: PVT/MP";
		else
			stage_string_info = "STAGE: UNKNOWN";

		ret = scnprintf(buf, PAGE_SIZE,
		"%04d/%02d/%02d\n%02d:%02d:%02d:%03d.%01d\n%s\nID: %02X %02X %02X\n",
						panel_year, panel_mon, panel_day, panel_hour,
						    panel_min, panel_sec, panel_msec_int,
								panel_msec_rem, stage_string_info, panel_code_info, panel_stage_info,
										panel_production_info);
    }else if (dsi_panel_name == DSI_PANEL_SAMSUNG_AMB670YF01) {
		if (panel_stage_info == 0x01)
			stage_string_info = "STAGE: T0";
		else if (panel_stage_info == 0x02)
			stage_string_info = "STAGE: EVT1";
		else if (panel_stage_info == 0x03)
			stage_string_info = "STAGE: EVT2";
		else if (panel_stage_info == 0x04)
			stage_string_info = "STAGE: DVT1";
		else if (panel_stage_info == 0x05)
			stage_string_info = "STAGE: DVT2";
		else if (panel_stage_info == 0x06)
			stage_string_info = "STAGE: PVT/MP";
		else
			stage_string_info = "STAGE: UNKNOWN";

		ret = scnprintf(buf, PAGE_SIZE,
		"%04d/%02d/%02d\n%02d:%02d:%02d:%03d.%01d\n%s\nID: %02X %02X %02X\n",
						panel_year, panel_mon, panel_day, panel_hour,
						    panel_min, panel_sec, panel_msec_int,
								panel_msec_rem, stage_string_info, panel_code_info, panel_stage_info,
										panel_production_info);
    }
	else {
		ret = scnprintf(buf, PAGE_SIZE, "%04d/%02d/%02d %02d:%02d:%02d\n",
				panel_year, panel_mon, panel_day, panel_hour, panel_min, panel_sec);
	}

	pr_err("panel year = %d, mon = %d, day = %d, hour = %d, min = %d, msec = %d.%d\n",
		panel_year, panel_mon, panel_day, panel_hour, panel_min, panel_msec_int, panel_msec_rem);

	return ret;
}

static ssize_t panel_serial_number_AT_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int ret = 0;
	struct drm_connector *connector = to_drm_connector(dev);

	ret = scnprintf(buf, PAGE_SIZE, "%llu\n", dsi_display_get_serial_number_at(connector));

	return ret;
}

static ssize_t dsi_on_command_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct drm_connector *connector = to_drm_connector(dev);
	int ret = 0;

	ret = dsi_display_get_dsi_on_command(connector, buf);

	return ret;
}

static ssize_t dsi_on_command_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct drm_connector *connector = to_drm_connector(dev);
	int ret = 0;

	ret = dsi_display_update_dsi_on_command(connector, buf, count);
	if (ret)
		pr_err("Failed to update dsi on command, ret=%d\n", ret);

	return count;
}

static ssize_t dsi_panel_command_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct drm_connector *connector = to_drm_connector(dev);
	int ret = 0;

	ret = dsi_display_get_dsi_panel_command(connector, buf);

	return ret;
}

static ssize_t dsi_panel_command_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct drm_connector *connector = to_drm_connector(dev);
	int ret = 0;

	ret = dsi_display_update_dsi_panel_command(connector, buf, count);
	if (ret)
		pr_err("Failed to update dsi panel command, ret=%d\n", ret);

	return count;
}

static ssize_t dsi_seed_command_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct drm_connector *connector = to_drm_connector(dev);
	int ret = 0;

	ret = dsi_display_get_dsi_seed_command(connector, buf);

	return ret;
}

static ssize_t dsi_seed_command_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct drm_connector *connector = to_drm_connector(dev);
	int ret = 0;

	ret = dsi_display_update_dsi_seed_command(connector, buf, count);
	if (ret)
		pr_err("Failed to update dsi seed command, ret=%d\n", ret);

	return count;
}

static ssize_t dsi_panel_reg_len_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int ret = 0;

	ret = scnprintf(buf, PAGE_SIZE, "%d\n", reg_read_len);

	return ret;
}

static ssize_t dsi_panel_reg_len_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int ret = 0;
	int num = 0;

	ret = kstrtoint(buf, 10, &num);
	if (ret) {
		pr_err("kstrtoint failed. ret=%d\n", ret);
		return ret;
	}

	if (num <= 0)
		pr_err("Invalid length!\n");
	else
		reg_read_len = num;

	return count;
}

static ssize_t dsi_panel_reg_read_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct drm_connector *connector = to_drm_connector(dev);
	int ret = 0;

	ret = dsi_display_get_reg_read_command_and_value(connector, buf);

	return ret;
}

static ssize_t dsi_panel_reg_read_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct drm_connector *connector = to_drm_connector(dev);
	int ret = 0;

	ret = dsi_display_reg_read(connector, buf, count);
	if (ret)
		pr_err("Failed to update reg read command, ret=%d\n", ret);

	return count;
}

static ssize_t dsi_cmd_log_switch_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int ret = 0;

	ret = scnprintf(buf, PAGE_SIZE, "dsi cmd log switch = %d\n"
									"0     -- dsi cmd log switch off\n"
									"other -- dsi cmd log switch on\n",
										dsi_cmd_log_enable);

	return ret;
}

static ssize_t dsi_cmd_log_switch_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int ret = 0;

	ret = kstrtoint(buf, 10, &dsi_cmd_log_enable);
	if (ret) {
		pr_err("kstrtoint failed. ret=%d\n", ret);
		return ret;
	}

	return count;
}

int current_freq;
static ssize_t dynamic_dsitiming_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int ret = 0;

	ret = scnprintf(buf, PAGE_SIZE, "current_freq = %d\n",
											current_freq);
	return ret;
}

static ssize_t dynamic_dsitiming_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int ret = 0;
	int freq_value = 0;

	ret = kstrtoint(buf, 10, &freq_value);
	if (ret) {
		pr_err("kstrtoint failed. ret=%d\n", ret);
		return ret;
	}

	current_freq = freq_value;

	pr_err("freq setting=%d\n", current_freq);

	if (ret)
		pr_err("set dsi freq (%d) fail\n", current_freq);

	return count;
}

extern u32 mode_fps;
static ssize_t dynamic_fps_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int ret = 0;

	ret = scnprintf(buf, PAGE_SIZE, "%d\n", mode_fps);

	return ret;
}

static ssize_t panel_mismatch_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct drm_connector *connector = to_drm_connector(dev);
	int ret = 0;
	int wrong_panel = 0;

	dsi_display_panel_mismatch_check(connector);

	wrong_panel = dsi_display_panel_mismatch(connector);
	ret = scnprintf(buf, PAGE_SIZE, "panel mismatch = %d\n"
									"0--(panel match)\n"
									"1--(panel mismatch)\n",
									wrong_panel);
	return ret;
}

int oneplus_force_screenfp;
int op_dimlayer_bl_enable;
int op_dp_enable;
int op_dither_enable = 0;
int panel_dither_enable = 1;

static ssize_t dim_alpha_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", oneplus_get_panel_brightness_to_alpha());
}

static ssize_t dim_alpha_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int ret = 0;
	struct drm_connector *connector = to_drm_connector(dev);

	ret = dsi_panel_brightness_alpha_debug(connector, buf, count);
	if (ret)
		pr_err("dsi_panel_brightness_alpha_debug failed. ret=%d\n", ret);

	return count;
}

static ssize_t force_screenfp_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{

	struct drm_connector *connector = to_drm_connector(dev);
	int ret = 0;

	oneplus_force_screenfp = dsi_display_get_fp_hbm_mode(connector);

	ret = scnprintf(buf, PAGE_SIZE, "OP_FP mode = %d\n"
									"0--finger-hbm mode(off)\n"
									"1--finger-hbm mode(600)\n",
									oneplus_force_screenfp);
	return snprintf(buf, PAGE_SIZE, "%d\n", oneplus_force_screenfp);
}

static ssize_t force_screenfp_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct drm_connector *connector = to_drm_connector(dev);
	int ret = 0;

	ret = kstrtoint(buf, 10, &oneplus_force_screenfp);
	if (ret) {
		pr_err("kstrtoint failed. ret=%d\n", ret);
		return ret;
	}

	ret = dsi_display_set_fp_hbm_mode(connector, oneplus_force_screenfp);
	if (ret)
		pr_err("set hbm mode(%d) fail\n", oneplus_force_screenfp);
	return count;
}

static ssize_t dimlayer_bl_en_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", op_dimlayer_bl_enable);
}

static ssize_t dimlayer_bl_en_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int ret = 0;

	ret = kstrtoint(buf, 10, &op_dimlayer_bl_enable);
	if (ret)
		pr_err("kstrtoint failed. ret=%d\n", ret);

	pr_err("op_dimlayer_bl_enable : %d\n", op_dimlayer_bl_enable);

	return count;
}

static ssize_t dither_en_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", op_dither_enable);
}

static ssize_t dither_en_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int ret = 0;

	ret = kstrtoint(buf, 10, &op_dither_enable);
	if (ret)
		pr_err("kstrtoint failed. ret=%d\n", ret);

	return count;
}

static ssize_t panel_dither_en_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", panel_dither_enable);
}

static ssize_t panel_dither_en_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int ret = 0;

	ret = kstrtoint(buf, 10, &panel_dither_enable);
	if (ret)
		pr_err("kstrtoint failed. ret=%d\n", ret);

	return count;
}

static ssize_t dp_en_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", op_dp_enable);
}

static ssize_t dp_en_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int ret = 0;

	ret = kstrtoint(buf, 10, &op_dp_enable);
	if (ret)
		pr_err("kstrtoint failed. ret=%d\n", ret);

	return count;
}

static ssize_t iris_recovery_mode_check_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int ret = 0;
	int result = 0;
#if defined(CONFIG_PXLW_IRIS)
	struct drm_connector *connector = to_drm_connector(dev);

	result = iris_loop_back_test(connector);
#endif
	pr_err("iris_loop_back_test result = %d", result);
	ret = scnprintf(buf, PAGE_SIZE, "%d\n", (result == 0) ? 1 : 0);

	return ret;
}

static DEVICE_ATTR_RW(acl);
static DEVICE_ATTR_RW(hbm);
static DEVICE_ATTR_RW(hbm_brightness);
static DEVICE_ATTR_RW(op_friginer_print_hbm);
static DEVICE_ATTR_RW(oplus_msd_aod);
static DEVICE_ATTR_RW(aod);
static DEVICE_ATTR_RW(aod_disable);
static DEVICE_ATTR_RW(DCI_P3);
static DEVICE_ATTR_RW(night_mode);
static DEVICE_ATTR_RW(native_display_p3_mode);
static DEVICE_ATTR_RW(native_display_wide_color_mode);
static DEVICE_ATTR_RW(native_display_loading_effect_mode);
static DEVICE_ATTR_RW(native_display_srgb_color_mode);
static DEVICE_ATTR_RW(native_display_customer_p3_mode);
static DEVICE_ATTR_RW(native_display_customer_srgb_mode);
static DEVICE_ATTR_RO(gamma_test);
static DEVICE_ATTR_RO(panel_serial_number);
static DEVICE_ATTR_RO(panel_serial_number_AT);
static DEVICE_ATTR_RO(iris_recovery_mode_check);
static DEVICE_ATTR_RW(dsi_on_command);
static DEVICE_ATTR_RW(dsi_panel_command);
static DEVICE_ATTR_RW(dsi_seed_command);
static DEVICE_ATTR_RW(dsi_panel_reg_len);
static DEVICE_ATTR_RW(dsi_panel_reg_read);
static DEVICE_ATTR_RW(dsi_cmd_log_switch);
static DEVICE_ATTR_RW(dynamic_dsitiming);
static DEVICE_ATTR_RO(panel_mismatch);
static DEVICE_ATTR_RO(dynamic_fps);
static DEVICE_ATTR_RW(dim_alpha);
static DEVICE_ATTR_RW(force_screenfp);
static DEVICE_ATTR_WO(notify_fppress);
static DEVICE_ATTR_WO(notify_dim);
static DEVICE_ATTR_WO(notify_aod);
static DEVICE_ATTR_RW(dimlayer_bl_en);
static DEVICE_ATTR_RW(dp_en);
static DEVICE_ATTR_RW(dither_en);
static DEVICE_ATTR_RW(panel_dither_en);
static DEVICE_ATTR_RW(seed_lp);
#ifdef CONFIG_PXLW_IRIS
static DEVICE_ATTR(adfr_debug, S_IRUGO|S_IWUSR, oplus_adfr_get_debug, oplus_adfr_set_debug);
static DEVICE_ATTR(vsync_switch, S_IRUGO|S_IWUSR, oplus_get_vsync_switch, oplus_set_vsync_switch);
#endif

static struct attribute *connector_dev_attrs[] = {
	&dev_attr_acl.attr,
	&dev_attr_hbm.attr,
	&dev_attr_hbm_brightness.attr,
	&dev_attr_op_friginer_print_hbm.attr,
	&dev_attr_oplus_msd_aod.attr,
	&dev_attr_aod.attr,
	&dev_attr_aod_disable.attr,
	&dev_attr_DCI_P3.attr,
	&dev_attr_night_mode.attr,
	&dev_attr_native_display_p3_mode.attr,
	&dev_attr_native_display_wide_color_mode.attr,
	&dev_attr_native_display_loading_effect_mode.attr,
	&dev_attr_native_display_srgb_color_mode.attr,
	&dev_attr_native_display_customer_p3_mode.attr,
	&dev_attr_native_display_customer_srgb_mode.attr,
	&dev_attr_gamma_test.attr,
	&dev_attr_panel_serial_number.attr,
	&dev_attr_panel_serial_number_AT.attr,
	&dev_attr_iris_recovery_mode_check.attr,
	&dev_attr_dsi_on_command.attr,
	&dev_attr_dsi_panel_command.attr,
	&dev_attr_dsi_seed_command.attr,
	&dev_attr_dsi_panel_reg_len.attr,
	&dev_attr_dsi_panel_reg_read.attr,
	&dev_attr_dsi_cmd_log_switch.attr,
	&dev_attr_dynamic_dsitiming.attr,
	&dev_attr_panel_mismatch.attr,
	&dev_attr_force_screenfp.attr,
	&dev_attr_dim_alpha.attr,
	&dev_attr_dynamic_fps.attr,
	&dev_attr_notify_fppress.attr,
	&dev_attr_notify_dim.attr,
	&dev_attr_notify_aod.attr,
	&dev_attr_dimlayer_bl_en.attr,
	&dev_attr_dp_en.attr,
	&dev_attr_dither_en.attr,
	&dev_attr_panel_dither_en.attr,
	&dev_attr_seed_lp.attr,
#ifdef CONFIG_PXLW_IRIS
	&dev_attr_adfr_debug.attr,
	&dev_attr_vsync_switch.attr,
#endif
	NULL
};

static const struct attribute_group connector_dev_group = {
	.attrs = connector_dev_attrs,
};


struct file_operations mytest_ops={
         .owner  = THIS_MODULE,
};

int dsi_register_node(struct drm_connector *drm_conn_dev)
{
	if(NULL == drm_conn_dev)
	{
		pr_err("drm_conn_dev is NULL \n");
		return -1;
	}
	if(NULL == drm_conn_dev->kdev)
	{
		pr_err("drm_conn_dev->kdev is NULL \n");
		return -1;
	}

	if(sysfs_create_group(&(drm_conn_dev->kdev->kobj),&connector_dev_group))
	{
		return -1;
	}
	return 0;
}
