/***************************************************************
** Copyright (C),  2020,  OPLUS Mobile Comm Corp.,  Ltd
** File : oplus_onscreenfingerprint.c
** Description : oplus onscreenfingerprint feature
** Version : 1.0
** Date : 2020/04/15
**
** ------------------------------- Revision History: -----------
**  <author>        <data>        <version >        <desc>
**   Qianxu         2020/04/15        1.0           Build this moudle
******************************************************************/

#include "sde_crtc.h"
#include "oplus_onscreenfingerprint.h"
#include "oplus_display_private_api.h"
#include "oplus_display_panel.h"
#include "oplus_adfr.h"

#define DSI_PANEL_OPLUS_DUMMY_VENDOR_NAME  "PanelVendorDummy"
#define DSI_PANEL_OPLUS_DUMMY_MANUFACTURE_NAME  "dummy1024"

bool oplus_pcc_enabled = false;
bool oplus_skip_pcc = false;
bool apollo_backlight_enable = false;
int oplus_aod_dim_alpha = CUST_A_NO;
struct drm_msm_pcc oplus_save_pcc;

EXPORT_SYMBOL(oplus_skip_pcc);
EXPORT_SYMBOL(oplus_save_pcc);
EXPORT_SYMBOL(oplus_pcc_enabled);

extern int oplus_underbrightness_alpha;
extern int oplus_dimlayer_dither_threshold;
extern u32 oplus_last_backlight;
extern int oplus_dimlayer_hbm;
extern int oplus_panel_alpha;
extern int hbm_mode;
extern bool oplus_ffl_trigger_finish;
extern struct oplus_apollo_backlight_list *p_apollo_backlight;
extern int dynamic_osc_clock;
extern int oplus_dimlayer_hbm_vblank_count;
extern atomic_t oplus_dimlayer_hbm_vblank_ref;
extern int oplus_onscreenfp_status;
extern u32 oplus_onscreenfp_vblank_count;
extern ktime_t oplus_onscreenfp_pressed_time;


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

void oplus_set_aod_dim_alpha(int cust)
{
	oplus_aod_dim_alpha = cust;
	DSI_DEBUG("set oplus_aod_dim_alpha = %d\n", oplus_aod_dim_alpha);
}

int oplus_get_panel_brightness(void)
{
	struct dsi_display *display = get_main_display();

	if (!display) {
		return 0;
	}

	return display->panel->bl_config.bl_level;
}
EXPORT_SYMBOL(oplus_get_panel_brightness);

int oplus_get_panel_power_mode(void)
{
	struct dsi_display *display = get_main_display();

	if (!display)
		return 0;

	return display->panel->power_mode;
}

static int bl_to_alpha(int brightness)
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

	for (i = 0; i < count; i++) {
		if (lut[i].brightness >= brightness) {
			break;
		}
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
	int level = ARRAY_SIZE(brightness_alpha_lut_dc);
	int i = 0;
	int alpha;

	for (i = 0; i < ARRAY_SIZE(brightness_alpha_lut_dc); i++) {
		if (brightness_alpha_lut_dc[i].brightness >= brightness) {
			break;
		}
	}

	if (i == 0) {
		alpha = brightness_alpha_lut_dc[0].alpha;
	} else if (i == level) {
		alpha = brightness_alpha_lut_dc[level - 1].alpha;
	} else
		alpha = interpolate(brightness,
				    brightness_alpha_lut_dc[i - 1].brightness,
				    brightness_alpha_lut_dc[i].brightness,
				    brightness_alpha_lut_dc[i - 1].alpha,
				    brightness_alpha_lut_dc[i].alpha);

	return alpha;
}

static int brightness_to_alpha(int brightness)
{
	int alpha;
	struct dsi_display *display = get_main_display();

	if ((strcmp(display->panel->oplus_priv.vendor_name, "AMB655X") != 0)
		&& (strcmp(display->panel->oplus_priv.vendor_name, "AMB670YF01") != 0)) {
		if (brightness == 0 || brightness == 1) {
			brightness = oplus_last_backlight;
		}
	}

	if (oplus_dimlayer_hbm) {
		alpha = bl_to_alpha(brightness);

	} else {
		alpha = bl_to_alpha_dc(brightness);
	}

	return alpha;
}

extern int oplus_display_fix_apollo_level(void);

static int oplus_find_index_invmaplist(uint32_t bl_level)
{
	int index = 0;
	int ret = -1;

	if (bl_level == p_apollo_backlight->bl_level_last) {
		index = p_apollo_backlight->bl_index_last;
		return index;
	}

	if (!p_apollo_backlight->bl_fix) {
		ret = oplus_display_fix_apollo_level();
		if (ret < 0) {
			return ret;
		}
	}

	for (; index < p_apollo_backlight->bl_id_lens; index++) {
		if (p_apollo_backlight->apollo_bl_list[index] == bl_level) {
			p_apollo_backlight->bl_index_last = index;
			p_apollo_backlight->bl_level_last = bl_level;
			return index;
		}
	}

	return -1;
}

static int oplus_get_panel_brightness_to_alpha(void)
{
	struct dsi_display *display = get_main_display();
	int index = 0;
	uint32_t brightness_panel = 0;

	if (!display) {
		return 0;
	}

	if (oplus_panel_alpha) {
		return oplus_panel_alpha;
	}

	/* force dim layer alpha in AOD scene */
	if (oplus_aod_dim_alpha != CUST_A_NO) {
		if (oplus_aod_dim_alpha == CUST_A_TRANS)
			return 0;
		else if (oplus_aod_dim_alpha == CUST_A_OPAQUE)
			return 255;
	}

	if (hbm_mode) {
		return 0;
	}

	if (!oplus_ffl_trigger_finish) {
		return brightness_to_alpha(FFL_FP_LEVEL);
	}

	if (apollo_backlight_enable) {
		index = oplus_find_index_invmaplist(display->panel->bl_config.bl_level);
		DSI_DEBUG("[%s] index = %d, panel_level = %d, apollo_level = %d", __func__, index,
			p_apollo_backlight->panel_bl_list[index], p_apollo_backlight->apollo_bl_list[index]);
		if (index >= 0) {
			brightness_panel = p_apollo_backlight->panel_bl_list[index];
			return brightness_to_alpha(brightness_panel);
		}
	}

	return brightness_to_alpha(display->panel->bl_config.bl_level);
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
		DSI_ERR("[%s] oplus,dsi-fod-brightness  not found\n", panel->name);
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

static int dsi_panel_parse_oplus_backlight_remapping_config(struct dsi_panel *panel)
{
	int rc = 0;
	int i;
	u32 length = 0;
	u32 count = 0;
	u32 size = 0;
	u32 *arr_32 = NULL;
	const u32 *arr;
	struct dsi_parser_utils *utils = &panel->utils;
	struct oplus_brightness_alpha *bl_remap;

	if (panel->host_config.ext_bridge_mode)
		return 0;

	arr = utils->get_property(utils->data, "oplus,dsi-brightness-remapping", &length);
	if (!arr) {
		DSI_ERR("[%s] oplus,dsi-fod-brightness not found\n", panel->name);
		return -EINVAL;
	}

	if (length & 0x1) {
		DSI_ERR("[%s] oplus,dsi-fod-brightness length error\n", panel->name);
		return -EINVAL;
	}

	DSI_INFO("oplus,dsi-brightness-remapping length = %d\n", length);
	length = length / sizeof(u32);
	size = length * sizeof(u32);

	arr_32 = kzalloc(size, GFP_KERNEL);
	if (!arr_32) {
		rc = -ENOMEM;
		goto error;
	}

	rc = utils->read_u32_array(utils->data, "oplus,dsi-brightness-remapping",
			arr_32, length);
	if (rc) {
		DSI_ERR("[%s] cannot read oplus,dsi-brightness-remapping\n", panel->name);
		goto error_free_arr_32;
	}

	count = length / 2;
	size = count * sizeof(*bl_remap);
	bl_remap = kzalloc(size, GFP_KERNEL);
	if (!bl_remap) {
		rc = -ENOMEM;
		goto error_free_arr_32;
	}

	panel->oplus_priv.bl_remap = bl_remap;
	panel->oplus_priv.bl_remap_count = count;

	for (i = 0; i < length; i += 2) {
		bl_remap->brightness = arr_32[i];
		bl_remap->alpha = arr_32[i + 1];
		bl_remap++;
	}

error_free_arr_32:
	kfree(arr_32);
error:
	return rc;
}

int dsi_panel_parse_oplus_config(struct dsi_panel *panel)
{
	struct dsi_parser_utils *utils = &panel->utils;
	int ret = 0;

	dsi_panel_parse_oplus_fod_config(panel);
	dsi_panel_parse_oplus_backlight_remapping_config(panel);

	panel->oplus_priv.vendor_name = utils->get_property(utils->data,
				       "oplus,mdss-dsi-vendor-name", NULL);

	if (!panel->oplus_priv.vendor_name) {
		pr_err("Failed to found panel name, using dumming name\n");
		panel->oplus_priv.vendor_name = DSI_PANEL_OPLUS_DUMMY_VENDOR_NAME;
	}

	panel->oplus_priv.manufacture_name = utils->get_property(utils->data,
					    "oplus,mdss-dsi-manufacture", NULL);

	if (!panel->oplus_priv.manufacture_name) {
		pr_err("Failed to found panel name, using dumming name\n");
		panel->oplus_priv.manufacture_name = DSI_PANEL_OPLUS_DUMMY_MANUFACTURE_NAME;
	}

	panel->oplus_priv.is_pxlw_iris5 = utils->read_bool(utils->data,
					 "oplus,is_pxlw_iris5");
	DSI_INFO("is_pxlw_iris5: %s",
		 panel->oplus_priv.is_pxlw_iris5 ? "true" : "false");

	panel->oplus_priv.is_osc_support = utils->read_bool(utils->data, "oplus,osc-support");
	pr_info("[%s]osc mode support: %s", __func__, panel->oplus_priv.is_osc_support ? "Yes" : "Not");

	if(panel->oplus_priv.is_osc_support) {
		ret = utils->read_u32(utils->data, "oplus,mdss-dsi-osc-clk-mode0-rate",
					&panel->oplus_priv.osc_clk_mode0_rate);
		if (ret) {
			pr_err("[%s]failed get panel parameter: oplus,mdss-dsi-osc-clk-mode0-rate\n", __func__);
			panel->oplus_priv.osc_clk_mode0_rate = 0;
		}
		dynamic_osc_clock = panel->oplus_priv.osc_clk_mode0_rate;

		ret = utils->read_u32(utils->data, "oplus,mdss-dsi-osc-clk-mode1-rate",
					&panel->oplus_priv.osc_clk_mode1_rate);
		if (ret) {
			pr_err("[%s]failed get panel parameter: oplus,mdss-dsi-osc-clk-mode1-rate\n", __func__);
			panel->oplus_priv.osc_clk_mode1_rate = 0;
		}
	}

	/* Add for apollo */
	panel->oplus_priv.is_apollo_support = utils->read_bool(utils->data, "oplus,apollo_backlight_enable");
	apollo_backlight_enable = panel->oplus_priv.is_apollo_support;
	DSI_INFO("apollo_backlight_enable: %s", panel->oplus_priv.is_apollo_support ? "true" : "false");

	if (panel->oplus_priv.is_apollo_support) {
		ret = utils->read_u32(utils->data, "oplus,apollo-sync-brightness-level",
				&panel->oplus_priv.sync_brightness_level);

		pr_err("sync level:%d\n", panel->oplus_priv.sync_brightness_level);

		if (ret) {
			pr_info("[%s] failed to get panel parameter: oplus,apollo-sync-brightness-level\n", __func__);
			/* Default sync brightness level is set to 200 */
			panel->oplus_priv.sync_brightness_level = 200;
		}
	}

	if (oplus_adfr_is_support()) {
		if (oplus_adfr_get_vsync_mode() == OPLUS_EXTERNAL_TE_TP_VSYNC) {
			/* power on with vsync_switch_gpio high bacause default timing is fhd OA 60hz */
			panel->vsync_switch_gpio_level = 1;
			/* default resolution is FHD when use mux switch */
			panel->cur_h_active = 1080;
		}
	}

	panel->oplus_priv.cabc_enabled = utils->read_bool(utils->data,
			"oplus,mdss-dsi-cabc-enabled");
	DSI_INFO("oplus,mdss-dsi-cabc-enabled: %s", panel->oplus_priv.cabc_enabled ? "true" : "false");

	panel->oplus_priv.dre_enabled = utils->read_bool(utils->data,
			"oplus,mdss-dsi-dre-enabled");
	DSI_INFO("oplus,mdss-dsi-cabc-enabled: %s", panel->oplus_priv.dre_enabled ? "true" : "false");

	return 0;
}
EXPORT_SYMBOL(dsi_panel_parse_oplus_config);

int dsi_panel_parse_oplus_mode_config(struct dsi_display_mode *mode,
				     struct dsi_parser_utils *utils)
{
	int rc;
	struct dsi_display_mode_priv_info *priv_info;
	int val = 0;

	priv_info = mode->priv_info;

	rc = utils->read_u32(utils->data, "oplus,fod-on-vblank", &val);

	if (rc) {
		DSI_ERR("oplus,fod-on-vblank is not defined, rc=%d\n", rc);
		priv_info->fod_on_vblank = 0;

	} else {
		priv_info->fod_on_vblank = val;
		DSI_INFO("oplus,fod-on-vblank is %d", val);
	}

	rc = utils->read_u32(utils->data, "oplus,fod-off-vblank", &val);

	if (rc) {
		DSI_ERR("oplus,fod-on-vblank is not defined, rc=%d\n", rc);
		priv_info->fod_off_vblank = 0;

	} else {
		priv_info->fod_off_vblank = val;
		DSI_INFO("oplus,fod-off-vblank is %d", val);
	}

	return 0;
}
EXPORT_SYMBOL(dsi_panel_parse_oplus_mode_config);

bool sde_crtc_get_dimlayer_mode(struct drm_crtc_state *crtc_state)
{
	struct sde_crtc_state *cstate;

	if (!crtc_state) {
		return false;
	}

	cstate = to_sde_crtc_state(crtc_state);
	return !!cstate->fingerprint_dim_layer;
}

bool sde_crtc_get_fingerprint_mode(struct drm_crtc_state *crtc_state)
{
	struct sde_crtc_state *cstate;

	if (!crtc_state) {
		return false;
	}

	cstate = to_sde_crtc_state(crtc_state);
	return !!cstate->fingerprint_mode;
}

bool sde_crtc_get_fingerprint_pressed(struct drm_crtc_state *crtc_state)
{
	struct sde_crtc_state *cstate;

	if (!crtc_state) {
		return false;
	}

	cstate = to_sde_crtc_state(crtc_state);
	return cstate->fingerprint_pressed;
}

int sde_crtc_set_onscreenfinger_defer_sync(struct drm_crtc_state *crtc_state,
		bool defer_sync)
{
	struct sde_crtc_state *cstate;

	if (!crtc_state) {
		return -EINVAL;
	}

	cstate = to_sde_crtc_state(crtc_state);
	cstate->fingerprint_defer_sync = defer_sync;
	return 0;
}

int sde_crtc_config_fingerprint_dim_layer(struct drm_crtc_state *crtc_state,
		int stage)
{
	struct sde_crtc_state *cstate;
	struct drm_display_mode *mode = &crtc_state->adjusted_mode;
	struct sde_hw_dim_layer *fingerprint_dim_layer;
	int alpha = oplus_get_panel_brightness_to_alpha();
	struct sde_kms *kms;

	kms = _sde_crtc_get_kms_(crtc_state->crtc);

	if (!kms || !kms->catalog) {
		SDE_ERROR("invalid kms\n");
		return -EINVAL;
	}

	cstate = to_sde_crtc_state(crtc_state);

	if (cstate->num_dim_layers == SDE_MAX_DIM_LAYERS - 1) {
		pr_err("failed to get available dim layer for custom\n");
		return -EINVAL;
	}

	if ((stage + SDE_STAGE_0) >= kms->catalog->mixer[0].sblk->maxblendstages) {
		return -EINVAL;
	}

	fingerprint_dim_layer = &cstate->dim_layer[cstate->num_dim_layers];
	fingerprint_dim_layer->flags = SDE_DRM_DIM_LAYER_INCLUSIVE;
	fingerprint_dim_layer->stage = stage + SDE_STAGE_0;

	DSI_DEBUG("fingerprint_dim_layer: stage = %d, alpha = %d\n", stage, alpha);

	fingerprint_dim_layer->rect.x = 0;
	fingerprint_dim_layer->rect.y = 0;
	fingerprint_dim_layer->rect.w = mode->hdisplay;
	fingerprint_dim_layer->rect.h = mode->vdisplay;
	fingerprint_dim_layer->color_fill = (struct sde_mdss_color) {0, 0, 0, alpha};

	cstate->fingerprint_dim_layer = fingerprint_dim_layer;
	oplus_underbrightness_alpha = alpha;

	return 0;
}
EXPORT_SYMBOL(sde_crtc_config_fingerprint_dim_layer);

bool _sde_encoder_setup_dither_for_onscreenfingerprint(struct sde_encoder_phys *phys,
		void *dither_cfg, int len, struct sde_hw_pingpong *hw_pp)
{
	struct drm_encoder *drm_enc = phys->parent;
	struct drm_msm_dither dither;

	if (!drm_enc || !drm_enc->crtc) {
		return -EFAULT;
	}
	if (!sde_crtc_get_dimlayer_mode(drm_enc->crtc->state)) {
		return -EINVAL;
	}
	if (len != sizeof(dither)) {
		return -EINVAL;
	}
	if (oplus_get_panel_brightness_to_alpha() < oplus_dimlayer_dither_threshold) {
		return -EINVAL;
	}
	if (hw_pp == 0) {
		return 0;
	}
	memcpy(&dither, dither_cfg, len);
	dither.c0_bitdepth = 6;
	dither.c1_bitdepth = 6;
	dither.c2_bitdepth = 6;
	dither.c3_bitdepth = 6;
	dither.temporal_en = 1;
	phys->hw_pp->ops.setup_dither(hw_pp, &dither, len);
	return 0;
}

int sde_plane_check_fingerprint_layer(const struct drm_plane_state *drm_state)
{
	struct sde_plane_state *pstate;

	if (!drm_state) {
		return 0;
	}

	pstate = to_sde_plane_state(drm_state);

	return sde_plane_get_property(pstate, PLANE_PROP_CUSTOM);
}
EXPORT_SYMBOL(sde_plane_check_fingerprint_layer);

int oplus_display_panel_get_dimlayer_hbm(void *data)
{
	uint32_t *dimlayer_hbm = data;

	(*dimlayer_hbm) = oplus_dimlayer_hbm;

	return 0;
}

int oplus_display_panel_set_dimlayer_hbm(void *data)
{
	struct dsi_display *display = get_main_display();
	struct drm_connector *dsi_connector = display->drm_conn;
	uint32_t *dimlayer_hbm = data;
	int err = 0;
	int value = (*dimlayer_hbm);

	value = !!value;
	if (oplus_dimlayer_hbm == value)
		return 0;
	if (!dsi_connector || !dsi_connector->state || !dsi_connector->state->crtc) {
		pr_err("[%s]: display not ready\n", __func__);
	} else {
		err = drm_crtc_vblank_get(dsi_connector->state->crtc);
		if (err) {
			pr_err("failed to get crtc vblank, error=%d\n", err);
		} else {
			/* do vblank put after 5 frames */
			oplus_dimlayer_hbm_vblank_count = 5;
			atomic_inc(&oplus_dimlayer_hbm_vblank_ref);
		}
	}
	oplus_dimlayer_hbm = value;
	pr_err("debug for oplus_display_set_dimlayer_hbm set oplus_dimlayer_hbm = %d\n", oplus_dimlayer_hbm);

	return 0;
}

int oplus_display_panel_notify_fp_press(void *data)
{
	struct dsi_display *display = get_main_display();
	struct drm_device *drm_dev = display->drm_dev;
	struct drm_connector *dsi_connector = display->drm_conn;
	struct drm_mode_config *mode_config = &drm_dev->mode_config;
	struct msm_drm_private *priv = drm_dev->dev_private;
	struct drm_atomic_state *state;
	struct drm_crtc_state *crtc_state;
	struct drm_crtc *crtc;
	static ktime_t on_time;
	int onscreenfp_status = 0;
	int vblank_get = -EINVAL;
	int err = 0;
	int i;
	bool if_con = false;
	uint32_t *p_onscreenfp_status = data;

#ifdef OPLUS_FEATURE_AOD_RAMLESS
	struct drm_display_mode *cmd_mode = NULL;
	struct drm_display_mode *vid_mode = NULL;
	struct drm_display_mode *mode = NULL;
	bool mode_changed = false;
#endif /* OPLUS_FEATURE_AOD_RAMLESS */

	if (!dsi_connector || !dsi_connector->state || !dsi_connector->state->crtc) {
		pr_err("[%s]: display not ready\n", __func__);
		return -EINVAL;
	}

	onscreenfp_status = (*p_onscreenfp_status);

	onscreenfp_status = !!onscreenfp_status;
	if (onscreenfp_status == oplus_onscreenfp_status)
		return 0;

	pr_err("notify fingerpress %s\n", onscreenfp_status ? "on" : "off");
	if (OPLUS_DISPLAY_AOD_SCENE == get_oplus_display_scene()) {
		if (onscreenfp_status) {
			on_time = ktime_get();
		} else {
			ktime_t now = ktime_get();
			ktime_t delta = ktime_sub(now, on_time);

			if (ktime_to_ns(delta) < 300000000)
				msleep(300 - (ktime_to_ns(delta) / 1000000));
		}
	}

	vblank_get = drm_crtc_vblank_get(dsi_connector->state->crtc);
	if (vblank_get) {
		pr_err("failed to get crtc vblank\n", vblank_get);
	}
	oplus_onscreenfp_status = onscreenfp_status;

	if_con = false;/*if_con = onscreenfp_status && (OPLUS_DISPLAY_AOD_SCENE == get_oplus_display_scene());*/
#ifdef OPLUS_FEATURE_AOD_RAMLESS
		if_con = if_con && !display->panel->oplus_priv.is_aod_ramless;
#endif /* OPLUS_FEATURE_AOD_RAMLESS */
	if (if_con) {
		/* enable the clk vote for CMD mode panels */
		if (display->config.panel_mode == DSI_OP_CMD_MODE) {
			dsi_display_clk_ctrl(display->dsi_clk_handle,
					DSI_ALL_CLKS, DSI_CLK_ON);
		}

		mutex_lock(&display->panel->panel_lock);

		if (display->panel->panel_initialized)
			err = dsi_panel_tx_cmd_set(display->panel, DSI_CMD_AOD_HBM_ON);

		mutex_unlock(&display->panel->panel_lock);
		if (err)
			pr_err("failed to setting aod hbm on mode %d\n", err);

		if (display->config.panel_mode == DSI_OP_CMD_MODE) {
			dsi_display_clk_ctrl(display->dsi_clk_handle,
					DSI_ALL_CLKS, DSI_CLK_OFF);
		}
	}
#ifdef OPLUS_FEATURE_AOD_RAMLESS
	if (!display->panel->oplus_priv.is_aod_ramless) {
#endif /* OPLUS_FEATURE_AOD_RAMLESS */
		oplus_onscreenfp_vblank_count = drm_crtc_vblank_count(
			dsi_connector->state->crtc);
		oplus_onscreenfp_pressed_time = ktime_get();
#ifdef OPLUS_FEATURE_AOD_RAMLESS
	}
#endif /* OPLUS_FEATURE_AOD_RAMLESS */

	drm_modeset_lock_all(drm_dev);

	state = drm_atomic_state_alloc(drm_dev);
	if (!state)
		goto error;

	state->acquire_ctx = mode_config->acquire_ctx;
	crtc = dsi_connector->state->crtc;
	crtc_state = drm_atomic_get_crtc_state(state, crtc);
	for (i = 0; i < priv->num_crtcs; i++) {
		if (priv->disp_thread[i].crtc_id == crtc->base.id) {
			if (priv->disp_thread[i].thread)
				kthread_flush_worker(&priv->disp_thread[i].worker);
		}
	}

#ifdef OPLUS_FEATURE_AOD_RAMLESS
	if (display->panel->oplus_priv.is_aod_ramless) {
		struct drm_display_mode *set_mode = NULL;

		if (oplus_display_mode == 2)
			goto error;

		list_for_each_entry(mode, &dsi_connector->modes, head) {
			if (drm_mode_vrefresh(mode) == 0)
				continue;
			if (mode->flags & DRM_MODE_FLAG_VID_MODE_PANEL)
				vid_mode = mode;
			if (mode->flags & DRM_MODE_FLAG_CMD_MODE_PANEL)
				cmd_mode = mode;
		}

		set_mode = oplus_display_mode ? vid_mode : cmd_mode;
		set_mode = onscreenfp_status ? vid_mode : set_mode;
		if (!crtc_state->active || !crtc_state->enable)
			goto error;

		if (set_mode && drm_mode_vrefresh(set_mode) != drm_mode_vrefresh(&crtc_state->mode)) {
			mode_changed = true;
		} else {
			mode_changed = false;
		}

		if (mode_changed) {
			display->panel->dyn_clk_caps.dyn_clk_support = false;
			drm_atomic_set_mode_for_crtc(crtc_state, set_mode);
		}

		wake_up(&oplus_aod_wait);
	}
#endif /* OPLUS_FEATURE_AOD_RAMLESS */

	err = drm_atomic_commit(state);
	drm_atomic_state_put(state);

#ifdef OPLUS_FEATURE_AOD_RAMLESS
	if (display->panel->oplus_priv.is_aod_ramless && mode_changed) {
		for (i = 0; i < priv->num_crtcs; i++) {
			if (priv->disp_thread[i].crtc_id == crtc->base.id) {
				if (priv->disp_thread[i].thread) {
					kthread_flush_worker(&priv->disp_thread[i].worker);
				}
			}
		}
		if (oplus_display_mode == 1)
			display->panel->dyn_clk_caps.dyn_clk_support = true;
	}
#endif /* OPLUS_FEATURE_AOD_RAMLESS */

error:
	drm_modeset_unlock_all(drm_dev);
	if (!vblank_get)
		drm_crtc_vblank_put(dsi_connector->state->crtc);

	return 0;
}

