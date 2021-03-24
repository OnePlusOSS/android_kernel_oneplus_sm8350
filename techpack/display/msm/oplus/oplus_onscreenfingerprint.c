/***************************************************************
** Copyright (C),  2020,  OPLUS Mobile Comm Corp.,  Ltd
** File : oplus_onscreenfingerprint.c
** Description : oplus onscreenfingerprint feature
** Version : 1.0
** Date : 2021/01/08
**
** ------------------------------- Revision History: -----------
**  <author>        <data>        <version >        <desc>
**  xxxxx.xxx      2021/01/08        1.0           Build this moudle
******************************************************************/

#include "oplus_onscreenfingerprint.h"
#include "dsi_display.h"

#define to_dsi_bridge(x)  container_of((x), struct dsi_bridge, base)
#define KEY_SPACE_OPLUS	32
#define KEY_LINEFEED_OPLUS 10
#define ALPHA_RANGE_DEBUG 5
#define ALPHA_DEFAULT_DEBUG 1

static struct oplus_brightness_alpha brightness_alpha_lut[] = {
	{0, 0xff},
	{1, 0xee},
	{2, 0xe8},
	{3, 0xe6},
	{4, 0xe5},
	{6, 0xe4},
	{10, 0xe0},
	{20, 0xd5},
	{30, 0xce},
	{45, 0xc6},
	{70, 0xb7},
	{100, 0xad},
	{150, 0xa0},
	{227, 0x8a},
	{300, 0x80},
	{400, 0x6e},
	{500, 0x5b},
	{600, 0x50},
	{800, 0x38},
	{1023, 0x18},
};

static struct oplus_brightness_alpha brightness_alpha_lut_dc[] = {
	{0, 0xff},
	{1, 0xE0},
	{2, 0xd1},
	{3, 0xd0},
	{4, 0xcf},
	{5, 0xc9},
	{6, 0xc7},
	{8, 0xbe},
	{10, 0xb6},
	{15, 0xaa},
	{20, 0x9c},
	{30, 0x92},
	{45, 0x7c},
	{70, 0x5c},
	{100, 0x40},
	{120, 0x2c},
	{140, 0x20},
	{160, 0x1c},
	{180, 0x16},
	{200, 0x8},
	{223, 0x0},
};

static int front_brightness = 0;
static int front_alpha = 0;
static int after_brightness = 0;
static int after_alpha = 0;
int cur_brightness = 0;
static int oneplus_panel_alpha = 0;
u8 alpha_debug_mode = 0;

static int interpolate(int x, int xa, int xb, int ya, int yb)
{
	int bf, factor, plus;
	int sub = 0;

	bf = 2 * (yb - ya) * (x - xa) / (xb - xa);
	factor = bf / 2;
	plus = bf % 2;
	if ((xa - xb) && (yb - ya))
		sub = 2 * (x - xa) * (x - xb) / (yb - ya) / (xa - xb);

	return ya + factor + plus + sub;
}

static int brightness_to_alpha(int brightness)
{
	struct dsi_display *display = get_main_display();
	struct oplus_brightness_alpha *lut = NULL;
	int count = 0;
 	int i = 0;
	int alpha;

	if (!display) {
		return 0;
	}

	if (display->panel->ba_seq && display->panel->ba_count) {
		count = display->panel->ba_count;
		lut = display->panel->ba_seq;

	} else {
		count = ARRAY_SIZE(brightness_alpha_lut);
		lut = brightness_alpha_lut;
	}

 	for (i = 0; i < count; i++){
 		if (lut[i].brightness >= brightness)
 			break;
 	}

	if (i == 0) {
		alpha = lut[0].alpha;
	} else if (i == count) {
		alpha = lut[count - 1].alpha;
	} else
		alpha = interpolate(brightness, lut[i - 1].brightness,
				    lut[i].brightness, lut[i - 1].alpha,
				    lut[i].alpha);
	return alpha;
}

static int bl_to_alpha_dc(int brightness)
{
	struct dsi_display *display = get_main_display();
	struct oplus_brightness_alpha *lut_dc = NULL;
	int count = 0;
	int level = ARRAY_SIZE(brightness_alpha_lut_dc);
	int i = 0;
	int alpha;

	if (!display) {
		return 0;
	}

	if (display->panel->dc_ba_seq && display->panel->dc_ba_count) {
		count = display->panel->dc_ba_count;
		lut_dc = display->panel->dc_ba_seq;

	} else {
		count = ARRAY_SIZE(brightness_alpha_lut_dc);
		lut_dc = brightness_alpha_lut_dc;
	}

	for (i = 0; i < count; i++) {
		if (brightness_alpha_lut_dc[i].brightness >= brightness) {
			break;
		}
	}

	if (i == 0) {
		alpha = lut_dc[0].alpha;
	} else if (i == level) {
		alpha = lut_dc[level - 1].alpha;
	} else
		alpha = interpolate(brightness,
				    lut_dc[i - 1].brightness, lut_dc[i].brightness,
				    lut_dc[i - 1].alpha, lut_dc[i].alpha);

	return alpha;
}

int brightness_to_alpha_debug(int fb, int fa, int ab, int aa, int cb)
{
	if(cb == fb)
		return fa;
	else if(cb == ab)
		return aa;
	else
		return interpolate(cb, fb, ab, fa, aa);
}

int oneplus_get_panel_brightness_to_alpha(void)
{
	struct dsi_display *display = get_main_display();

	if (!display)
		return 0;

	if (oneplus_panel_alpha)
		return oneplus_panel_alpha;
	else if(alpha_debug_mode) {
		return brightness_to_alpha_debug(front_brightness, front_alpha, after_brightness, after_alpha, cur_brightness);
	}

	if (display->panel->dim_status)
		return brightness_to_alpha(display->panel->hbm_backlight);
	else
		return bl_to_alpha_dc(display->panel->hbm_backlight);
}

int dsi_panel_parse_oplus_fod_config(struct dsi_panel *panel)
{
	int rc = 0;
	int i;
	u32 length = 0;
	u32 count = 0;
	u32 size = 0;
	u32 *arr_32 = NULL;
	const u32 *arr;
	struct dsi_parser_utils *utils = &panel->utils;
	struct oplus_brightness_alpha *seq;

	if (panel->host_config.ext_bridge_mode) {
		return 0;
	}

	arr = utils->get_property(utils->data, "oplus,dsi-fod-brightness", &length);

	if (!arr) {
		DSI_ERR("[%s] oplus,dsi-fod-brightness not found\n", panel->name);
		return -EINVAL;
	}

	if (length & 0x1) {
		DSI_ERR("[%s] oplus,dsi-fod-brightness length error\n", panel->name);
		return -EINVAL;
	}

	DSI_DEBUG("RESET SEQ LENGTH = %d\n", length);
	length = length / sizeof(u32);
	size = length * sizeof(u32);

	arr_32 = kzalloc(size, GFP_KERNEL);

	if (!arr_32) {
		rc = -ENOMEM;
		goto error;
	}

	rc = utils->read_u32_array(utils->data, "oplus,dsi-fod-brightness",
				   arr_32, length);

	if (rc) {
		DSI_ERR("[%s] cannot read dsi-fod-brightness\n", panel->name);
		goto error_free_arr_32;
	}

	count = length / 2;
	size = count * sizeof(*seq);
	seq = kzalloc(size, GFP_KERNEL);


	if (!seq) {
		rc = -ENOMEM;
		goto error_free_arr_32;
	}

	panel->ba_seq = seq;
	panel->ba_count = count;

	for (i = 0; i < length; i += 2) {
		seq->brightness = arr_32[i];
		seq->alpha = arr_32[i + 1];
		seq++;
	}

error_free_arr_32:
	kfree(arr_32);
error:
	return rc;
}

int dsi_panel_parse_oplus_dc_config(struct dsi_panel *panel)
{
	int rc = 0;
	int i;
	u32 length = 0;
	u32 count = 0;
	u32 size = 0;
	u32 *arr_32 = NULL;
	const u32 *arr;
	struct dsi_parser_utils *utils = &panel->utils;
	struct oplus_brightness_alpha *seq;

	if (panel->host_config.ext_bridge_mode) {
		return 0;
	}

	arr = utils->get_property(utils->data, "oplus,dsi-dc-brightness", &length);

	if (!arr) {
		DSI_ERR("[%s] oplus,dsi-dc-brightness not found\n", panel->name);
		return -EINVAL;
	}

	if (length & 0x1) {
		DSI_ERR("[%s] oplus,dsi-dc-brightness length error\n", panel->name);
		return -EINVAL;
	}

	DSI_DEBUG("RESET SEQ LENGTH = %d\n", length);
	length = length / sizeof(u32);
	size = length * sizeof(u32);

	arr_32 = kzalloc(size, GFP_KERNEL);

	if (!arr_32) {
		rc = -ENOMEM;
		goto error;
	}

	rc = utils->read_u32_array(utils->data, "oplus,dsi-dc-brightness",
				   arr_32, length);

	if (rc) {
		DSI_ERR("[%s] cannot read oplus,dsi-dc-brightness\n", panel->name);
		goto error_free_arr_32;
	}

	count = length / 2;
	size = count * sizeof(*seq);
	seq = kzalloc(size, GFP_KERNEL);


	if (!seq) {
		rc = -ENOMEM;
		goto error_free_arr_32;
	}

	panel->dc_ba_seq = seq;
	panel->dc_ba_count = count;

	for (i = 0; i < length; i += 2) {
		seq->brightness = arr_32[i];
		seq->alpha = arr_32[i + 1];
		seq++;
	}

error_free_arr_32:
	kfree(arr_32);
error:
	return rc;
}

int dsi_panel_parse_oplus_config(struct dsi_panel *panel)
{
	int rc = 0;
	rc = dsi_panel_parse_oplus_dc_config(panel);
	if(rc)
		DSI_ERR("failed to parse dc config, rc=%d\n", rc);

	rc = dsi_panel_parse_oplus_fod_config(panel);
	if(rc)
		DSI_ERR("failed to parse fod config, rc=%d\n", rc);

	return 0;
}

static void Split_StrtoInt(const char* Src, int *Dest, int count) {
	int i;
	int len = 0;

	for(i=0; i<count; i++) {
		if(KEY_SPACE_OPLUS == Src[i] || KEY_LINEFEED_OPLUS == Src[i]) {
			len++;
		} else {
			Dest[len] = Dest[len]*10 +(Src[i] - '0');
		}
	}
}

int dsi_panel_brightness_alpha_debug(struct drm_connector *connector, const char *buf, size_t count)
{
	int *data;
	unsigned int length;
	int i = 0;
  	int rc = 0;
	struct dsi_display *dsi_display = NULL;
  	struct dsi_panel *panel = NULL;
  	struct dsi_bridge *c_bridge;

  	if ((connector == NULL) || (connector->encoder == NULL)
  			|| (connector->encoder->bridge == NULL))
  		return -EINVAL;

  	c_bridge =  to_dsi_bridge(connector->encoder->bridge);
  	dsi_display = c_bridge->display;

  	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
  		return -EINVAL;

  	panel = dsi_display->panel;

  	mutex_lock(&dsi_display->display_lock);

  	if (!dsi_panel_initialized(panel))
  		goto error;

	length = 0;
	for(i=0; i<count; i++) {
		if(KEY_SPACE_OPLUS == buf[i] || KEY_LINEFEED_OPLUS == buf[i])
			length++;
	}

	data = kzalloc(length, GFP_KERNEL);
	memset(data, 0, length*sizeof(int)) ;
	Split_StrtoInt(buf, data, count);

	if(ALPHA_RANGE_DEBUG == length) {
		front_brightness = data[0];
		front_alpha = data[1];
		after_brightness = data[2];
		after_alpha = data[3];
		cur_brightness = data[4];
		alpha_debug_mode = 1;
		pr_err("front_brightness=%d front_alpha=%d after_brightness=%d after_alpha=%d cur_brightness=%d\n",
			front_brightness, front_alpha, after_brightness, after_alpha, cur_brightness);
	} else if(ALPHA_DEFAULT_DEBUG == length) {
		oneplus_panel_alpha = data[0];
		DSI_ERR("dsi_panel_brightness_alpha_debug oneplus_panel_alpha=%d\n", oneplus_panel_alpha);
		goto error;
	} else {
		front_brightness = 0;
		front_alpha = 0;
		after_brightness = 0;
		after_alpha = 0;
		cur_brightness = 0;
		DSI_ERR("dsi_panel_brightness_alpha_debug cmd parameter err!\n");
		goto error;
	}

	rc = dsi_display_set_backlight(connector, dsi_display, cur_brightness);
	if (rc)
  		DSI_ERR("unable to set backlight\n");

error:
	kfree(data);
  	mutex_unlock(&dsi_display->display_lock);
	return rc;
}