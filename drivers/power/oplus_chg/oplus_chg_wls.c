// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2020 Oplus. All rights reserved.
 */

#define pr_fmt(fmt) "OPLUS_CHG[WLS]: %s[%d]: " fmt, __func__, __LINE__

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/of_platform.h>
#include <linux/interrupt.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/uaccess.h>
#include <linux/proc_fs.h>
#include <linux/power_supply.h>
#ifdef CONFIG_OPLUS_CHG_OOS
#include <linux/oem/oplus_chg.h>
#include <linux/oem/boot_mode.h>
#include <linux/oem/oplus_chg_voter.h>
#else
#include <linux/oplus_chg.h>
#include <soc/oplus/system/boot_mode.h>
#include <linux/oplus_chg_voter.h>
#endif
#ifndef CONFIG_OPLUS_CHARGER_MTK
#include <linux/soc/qcom/smem.h>
#endif
#include "oplus_chg_module.h"
#include "oplus_chg_cfg.h"
#include "oplus_chg_ic.h"
#include "oplus_chg_wls.h"
#include "wireless/wls_chg_intf.h"
#ifdef OPLUS_CHG_OP_DEF
#include "oplus_op_def.h"
#endif
struct oplus_chg_wls_state_handler {
	int (*enter_state)(struct oplus_chg_wls *wls_dev);
	int (*handle_state)(struct oplus_chg_wls *wls_dev);
	int (*exit_state)(struct oplus_chg_wls *wls_dev);
};

static ATOMIC_NOTIFIER_HEAD(wls_ocm_notifier);
static bool adsp_started;
static bool online_pending;

static struct wls_base_type wls_base_table[] = {
	{ 0x00, 30000 }, { 0x01, 40000 }, { 0x02, 50000 }, { 0x03, 50000 }, { 0x04, 50000 },
	{ 0x05, 50000 }, { 0x06, 50000 }, { 0x07, 50000 }, { 0x08, 50000 }, { 0x09, 50000 },
	{ 0x0a, 100000 }, { 0x0b, 100000 }, { 0x10, 100000 }, { 0x11, 100000 }, { 0x12, 100000 },
	{ 0x13, 100000 }, { 0x1f, 50000 },
};

static u8 oplus_trx_id_table[] = {
	0x02, 0x03, 0x04, 0x05, 0x06
};

/*static int oplus_chg_wls_pwr_table[] = {0, 12000, 12000, 35000, 50000};*/
static struct wls_pwr_table oplus_chg_wls_pwr_table[] = {/*(f2_id, r_power, t_power)*/
	{ 0x00, 12, 15 }, { 0x01, 12, 20 }, { 0x02, 12, 30 }, { 0x03, 35, 50 }, { 0x04, 45, 65 },
	{ 0x05, 50, 75 }, { 0x06, 60, 85 }, { 0x07, 65, 95 }, { 0x08, 75, 105 }, { 0x09, 80, 115 },
	{ 0x0A, 90, 125 }, { 0x0B, 20, 20 }, { 0x0C, 100, 140 }, { 0x0D, 115, 160 }, { 0x0E, 130, 180 },
	{ 0x0F, 145, 200 },
	{ 0x11, 35, 50 }, { 0x12, 35, 50 }, { 0x13, 12, 20 }, { 0x14, 45, 65 }, { 0x15, 12, 20 },
	{ 0x16, 12, 20 }, { 0x17, 12, 30 }, { 0x18, 12, 30 }, { 0x19, 12, 30 }, { 0x1A, 12, 33 },
	{ 0x1B, 12, 33 }, { 0x1C, 12, 44 }, { 0x1D, 12, 44 }, { 0x1E, 12, 44 },
	{ 0x21, 35, 50 }, { 0x22, 12, 44 }, { 0x23, 35, 50 }, { 0x24, 35, 55 }, { 0x25, 35, 55 },
	{ 0x26, 35, 55 }, { 0x27, 35, 55 }, { 0x28, 45, 65 }, { 0x29, 12, 30 }, { 0x2A, 45, 65 },
	{ 0x2B, 45, 66 }, { 0x2C, 45, 67 }, { 0x2D, 45, 67 }, { 0x2E, 45, 67 },
	{ 0x31, 35, 50 }, { 0x32, 90, 120 }, { 0x33, 35, 50 }, { 0x34, 12, 20 }, { 0x35, 45, 65 },
	{ 0x36, 45, 66 }, { 0x37, 50, 88 }, { 0x38, 50, 88 }, { 0x39, 50, 88 }, { 0x3A, 50, 88 },
	{ 0x3B, 75, 100 }, { 0x3C, 75, 100 }, { 0x3D, 75, 100 }, { 0x3E, 75, 100 },
	{ 0x41, 12, 30 }, { 0x42, 12, 30 }, { 0x43, 12, 30 }, { 0x44, 12, 30 }, { 0x45, 12, 30 },
	{ 0x46, 12, 30 }, { 0x47, 90, 120 }, { 0x48, 90, 120 }, { 0x49, 12, 33 }, { 0x4A, 12, 33 },
	{ 0x4B, 50, 80 }, { 0x4C, 50, 80 }, { 0x4D, 50, 80 }, { 0x4E, 50, 80 },
	{ 0x51, 90, 125 },
	{ 0x61, 12, 33 }, { 0x62, 35, 50 }, { 0x63, 45, 65 }, { 0x64, 45, 66 }, { 0x65, 50, 80 },
	{ 0x66, 45, 65 }, { 0x67, 90, 125 }, { 0x68, 90, 125 }, { 0x69, 75, 100 }, { 0x6A, 75, 100 },
	{ 0x6B, 90, 120 }, { 0x6C, 45, 67 }, { 0x6D, 45, 67 }, { 0x6E, 45, 65 },
	{ 0x7F, 30, 0 },
};

static struct wls_pwr_table oplus_chg_wls_tripartite_pwr_table[] = {
	{0x01, 20, 20}, {0x02, 30, 30}, {0x03, 40, 40},  {0x04, 50, 50},
};

static const char * const oplus_chg_wls_rx_state_text[] = {
	[OPLUS_CHG_WLS_RX_STATE_DEFAULT] = "default",
	[OPLUS_CHG_WLS_RX_STATE_BPP] = "bpp",
	[OPLUS_CHG_WLS_RX_STATE_EPP] = "epp",
	[OPLUS_CHG_WLS_RX_STATE_EPP_PLUS] = "epp-plus",
	[OPLUS_CHG_WLS_RX_STATE_FAST] = "fast",
	[OPLUS_CHG_WLS_RX_STATE_FFC] = "ffc",
	[OPLUS_CHG_WLS_RX_STATE_DONE] = "done",
	[OPLUS_CHG_WLS_RX_STATE_QUIET] = "quiet",
	[OPLUS_CHG_WLS_RX_STATE_STOP] = "stop",
	[OPLUS_CHG_WLS_RX_STATE_DEBUG] = "debug",
	[OPLUS_CHG_WLS_RX_STATE_FTM] = "ftm",
	[OPLUS_CHG_WLS_RX_STATE_ERROR] = "error",
};

static const char * const oplus_chg_wls_trx_state_text[] = {
	[OPLUS_CHG_WLS_TRX_STATE_DEFAULT] = "default",
	[OPLUS_CHG_WLS_TRX_STATE_READY] = "ready",
	[OPLUS_CHG_WLS_TRX_STATE_WAIT_PING] = "wait_ping",
	[OPLUS_CHG_WLS_TRX_STATE_TRANSFER] = "transfer",
	[OPLUS_CHG_WLS_TRX_STATE_OFF] = "off",
};

static u8 oplus_chg_wls_disable_fod_parm[WLS_FOD_PARM_LENGTH] = {
	0xff, 0x7f, 0xff, 0x7f, 0xff, 0x7f,
	0xff, 0x7f, 0xff, 0x7f, 0xff, 0x7f,
};

static struct oplus_chg_wls_dynamic_config default_config = {
	.fcc_step = {
		{},
		{},
		{},
		{},
	},
};

#ifndef CONFIG_OPLUS_CHG_OOS

#define COOL_DOWN_12V_THR		2

static int cool_down_swarp[] = {
	0, 500, 500, 1200, 1200, 1500, 1500,
	2000, 2000, 2500, 3000, 4000, 5000,
};

static int cool_down_warp[] = {
	0, 500, 500, 1200, 1200, 1200, 1200,
};

static int cool_down_epp_plus[] = {
	0, 500, 500, 1000, 1000, 1000, 1500,
};

static int cool_down_epp[] = {
	0, 500, 500, 1000, 1000, 1000, 1000,
};

static int cool_down_bpp[] = {
	0, 500, 500, 500, 500, 500, 500,
};
#endif

__maybe_unused static bool is_wls_psy_available(struct oplus_chg_wls *wls_dev)
{
	if (!wls_dev->wls_psy)
		wls_dev->wls_psy = power_supply_get_by_name("wireless");
	return !!wls_dev->wls_psy;
}

__maybe_unused static bool is_usb_ocm_available(struct oplus_chg_wls *wls_dev)
{
	if (!wls_dev->usb_ocm)
		wls_dev->usb_ocm = oplus_chg_mod_get_by_name("usb");
	return !!wls_dev->usb_ocm;
}

__maybe_unused static bool is_batt_ocm_available(struct oplus_chg_wls *wls_dev)
{
	if (!wls_dev->batt_ocm)
		wls_dev->batt_ocm = oplus_chg_mod_get_by_name("battery");
	return !!wls_dev->batt_ocm;
}

__maybe_unused static bool is_comm_ocm_available(struct oplus_chg_wls *wls_dev)
{
	if (!wls_dev->comm_ocm)
		wls_dev->comm_ocm = oplus_chg_mod_get_by_name("common");
	return !!wls_dev->comm_ocm;
}

__maybe_unused static bool oplus_chg_wls_is_usb_present(struct oplus_chg_wls *wls_dev)
{
	union oplus_chg_mod_propval pval;
	bool present = false;
	int rc;

	if (!is_usb_ocm_available(wls_dev)) {
		pr_err("usb_ocm not found\n");
		return false;
	}
	rc = oplus_chg_mod_get_property(wls_dev->usb_ocm, OPLUS_CHG_PROP_PRESENT, &pval);
	if (rc == 0)
		present = !!pval.intval;
	rc = oplus_chg_mod_get_property(wls_dev->usb_ocm, OPLUS_CHG_PROP_ONLINE, &pval);
	if (rc == 0)
		present |= !!pval.intval;

	return present;
}

__maybe_unused static bool oplus_chg_wls_is_usb_connected(struct oplus_chg_wls *wls_dev)
{
	if (!gpio_is_valid(wls_dev->usb_int_gpio)) {
		pr_err("usb_int_gpio invalid\n");
		return false;
	}

	return !!gpio_get_value(wls_dev->usb_int_gpio);
}

bool oplus_chg_wls_is_otg_mode(struct oplus_chg_wls *wls_dev)
{
	int rc;
	union oplus_chg_mod_propval pval;

	if (!is_usb_ocm_available(wls_dev)) {
		pr_err("usb ocm not found\n");
		return false;
	}
	rc = oplus_chg_mod_get_property(wls_dev->usb_ocm, OPLUS_CHG_PROP_OTG_MODE, &pval);

	if (rc < 0)
		return false;
	return !!pval.intval;
}

static int oplus_chg_wls_get_base_power_max(u8 id)
{
	int i;
	int pwr = WLS_WARP_PWR_MAX_MW;

	for (i = 0; i < ARRAY_SIZE(wls_base_table); i++) {
		if (wls_base_table[i].id == id) {
			pwr = wls_base_table[i].power_max_mw;
			return pwr;
		}
	}

	return pwr;
}

static int oplus_chg_wls_get_r_power(struct oplus_chg_wls *wls_dev, u8 f2_data)
{
	int i = 0;
	int r_pwr = WLS_RECEIVE_POWER_DEFAULT;
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;

	for (i = 0; i < ARRAY_SIZE(oplus_chg_wls_pwr_table); i++) {
		if (oplus_chg_wls_pwr_table[i].f2_id == (f2_data & 0x7F)) {
			r_pwr = oplus_chg_wls_pwr_table[i].r_power * 1000;
			break;
		}
	}
	if (wls_status->wls_type == OPLUS_CHG_WLS_PD_65W)
		return WLS_RECEIVE_POWER_PD65W;
	return r_pwr;
}

static int oplus_chg_wls_get_tripartite_r_power(u8 f2_data)
{
	int i = 0;
	int r_pwr = WLS_RECEIVE_POWER_DEFAULT;

	for (i = 0; i < ARRAY_SIZE(oplus_chg_wls_tripartite_pwr_table); i++) {
		if (oplus_chg_wls_tripartite_pwr_table[i].f2_id == (f2_data & 0x7F)) {
			r_pwr = oplus_chg_wls_tripartite_pwr_table[i].r_power * 1000;
			break;
		}
	}
	return r_pwr;
}

static int oplus_chg_wls_get_ibat(struct oplus_chg_wls *wls_dev, int *ibat_ma)
{
	int rc;
	union oplus_chg_mod_propval pval;

	if (!is_batt_ocm_available(wls_dev)) {
		pr_err("batt ocm not found\n");
		return -ENODEV;
	}
	rc = oplus_chg_mod_get_property(wls_dev->batt_ocm, OPLUS_CHG_PROP_CURRENT_NOW, &pval);
	if (rc < 0)
		return rc;
#ifdef CONFIG_OPLUS_CHG_OOS
	*ibat_ma = pval.intval / 1000;
#else
	*ibat_ma = pval.intval;
#endif

	return 0;
}

static int oplus_chg_wls_get_vbat(struct oplus_chg_wls *wls_dev, int *vbat_mv)
{
	int rc;
	union oplus_chg_mod_propval pval;

	if (!is_batt_ocm_available(wls_dev)) {
		pr_err("batt ocm not found\n");
		return -ENODEV;
	}
	rc = oplus_chg_mod_get_property(wls_dev->batt_ocm, OPLUS_CHG_PROP_VOLTAGE_NOW, &pval);
	if (rc < 0)
		return rc;
	*vbat_mv = pval.intval / 1000;

	return 0;
}

static int oplus_chg_wls_get_ui_soc(struct oplus_chg_wls *wls_dev, int *ui_soc)
{
	int rc;
	union oplus_chg_mod_propval pval;

	if (!is_batt_ocm_available(wls_dev)) {
		pr_err("batt ocm not found\n");
		return -ENODEV;
	}
	rc = oplus_chg_mod_get_property(wls_dev->batt_ocm, OPLUS_CHG_PROP_CAPACITY, &pval);
	if (rc < 0)
		return rc;
	*ui_soc = pval.intval;

	return 0;
}

static int oplus_chg_wls_get_real_soc(struct oplus_chg_wls *wls_dev, int *soc)
{
	int rc;
	union oplus_chg_mod_propval pval;

	if (!is_batt_ocm_available(wls_dev)) {
		pr_err("batt ocm not found\n");
		return -ENODEV;
	}
	rc = oplus_chg_mod_get_property(wls_dev->batt_ocm, OPLUS_CHG_PROP_REAL_CAPACITY, &pval);
	if (rc < 0)
		return rc;
	*soc = pval.intval;

	return 0;
}

static int oplus_chg_wls_get_batt_temp(struct oplus_chg_wls *wls_dev, int *temp)
{
	int rc;
	union oplus_chg_mod_propval pval;

	if (!is_batt_ocm_available(wls_dev)) {
		pr_err("batt ocm not found\n");
		return -ENODEV;
	}
	rc = oplus_chg_mod_get_property(wls_dev->batt_ocm, OPLUS_CHG_PROP_TEMP, &pval);
	if (rc < 0)
		return rc;
	*temp = pval.intval;

	return 0;
}

static int oplus_chg_wls_get_skin_temp(struct oplus_chg_wls *wls_dev, int *temp)
{
	int rc;
	union oplus_chg_mod_propval pval;

	if (!is_comm_ocm_available(wls_dev)) {
		pr_err("comm ocm not found\n");
		return -ENODEV;
	}
	if(wls_dev->factory_mode){
		*temp = 250;
		return 0;
	}

	rc = oplus_chg_mod_get_property(wls_dev->comm_ocm, OPLUS_CHG_PROP_SKIN_TEMP, &pval);
	if (rc < 0)
		return rc;
	*temp = pval.intval;

	return 0;
}

static int oplus_chg_wls_set_wrx_enable(struct oplus_chg_wls *wls_dev, bool en)
{
	int rc;

	if (IS_ERR_OR_NULL(wls_dev->pinctrl) ||
	    IS_ERR_OR_NULL(wls_dev->wrx_en_active) ||
	    IS_ERR_OR_NULL(wls_dev->wrx_en_sleep)) {
		pr_err("wrx_en pinctrl error\n");
		return -ENODEV;
	}

	rc = pinctrl_select_state(wls_dev->pinctrl,
		en ? wls_dev->wrx_en_active : wls_dev->wrx_en_sleep);
	if (rc < 0)
		pr_err("can't %s wrx, rc=%d\n", en ? "enable" : "disable", rc);
	else
		pr_debug("set wrx %s\n", en ? "enable" : "disable");

	return rc;
}

static int oplus_chg_wls_fcc_vote_callback(struct votable *votable, void *data,
				int fcc_ma, const char *client, bool step)
{
	struct oplus_chg_wls *wls_dev = data;
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	struct oplus_chg_wls_dynamic_config *dynamic_cfg =&wls_dev->dynamic_config;

	if (fcc_ma < 0)
		return 0;

	if (fcc_ma > dynamic_cfg->fastchg_curr_max_ma)
		fcc_ma = dynamic_cfg->fastchg_curr_max_ma;

	wls_status->fastchg_target_curr_ma = fcc_ma;
	pr_info("set target current to %d\n", wls_status->fastchg_target_curr_ma);

	if (wls_dev->wls_ocm)
		oplus_chg_mod_changed(wls_dev->wls_ocm);

	return 0;
}

static int oplus_chg_wls_fastchg_disable_vote_callback(struct votable *votable, void *data,
				int disable, const char *client, bool step)
{
	struct oplus_chg_wls *wls_dev = data;
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;

	wls_status->fastchg_disable = disable;
	pr_info("%s wireless fast charge\n", disable ? "disable" : "enable");

	return 0;
}

static int oplus_chg_wls_wrx_en_vote_callback(struct votable *votable, void *data,
				int en, const char *client, bool step)
{
	struct oplus_chg_wls *wls_dev = data;
	int rc;

	rc = oplus_chg_wls_set_wrx_enable(wls_dev, en);
	return rc;
}

static int oplus_chg_wls_nor_icl_vote_callback(struct votable *votable, void *data,
				int icl_ma, const char *client, bool step)
{
	struct oplus_chg_wls *wls_dev = data;
	int rc;

	if (icl_ma < 0)
		return 0;

	if (step)
		rc = oplus_chg_wls_nor_set_icl_by_step(wls_dev->wls_nor, icl_ma, 100, false);
	else
		rc = oplus_chg_wls_nor_set_icl(wls_dev->wls_nor, icl_ma);

	if (wls_dev->wls_ocm)
		oplus_chg_mod_changed(wls_dev->wls_ocm);

	return rc;
}

static int oplus_chg_wls_nor_fcc_vote_callback(struct votable *votable, void *data,
				int fcc_ma, const char *client, bool step)
{
	struct oplus_chg_wls *wls_dev = data;
	int rc;

	if (fcc_ma < 0)
		return 0;

	if (step)
		rc = oplus_chg_wls_nor_set_fcc_by_step(wls_dev->wls_nor, fcc_ma, 100, false);
	else
		rc = oplus_chg_wls_nor_set_fcc(wls_dev->wls_nor, fcc_ma);

	if (wls_dev->wls_ocm)
		oplus_chg_mod_changed(wls_dev->wls_ocm);

	return rc;
}

static int oplus_chg_wls_nor_fv_vote_callback(struct votable *votable, void *data,
				int fv_mv, const char *client, bool step)
{
	struct oplus_chg_wls *wls_dev = data;
	int rc;

	if (fv_mv < 0)
		return 0;

	rc = oplus_chg_wls_nor_set_fv(wls_dev->wls_nor, fv_mv);

	if (wls_dev->wls_ocm)
		oplus_chg_mod_changed(wls_dev->wls_ocm);

	return rc;
}

static int oplus_chg_wls_nor_out_disable_vote_callback(struct votable *votable,
			void *data, int disable, const char *client, bool step)
{
	struct oplus_chg_wls *wls_dev = data;
	int rc;

	rc = oplus_chg_wls_nor_set_output_enable(wls_dev->wls_nor, !disable);
	if (rc < 0)
		pr_err("can't %s wireless nor out charge\n", disable ? "disable" : "enable");
	else
		pr_err("%s wireless nor out charge\n", disable ? "disable" : "enable");

	return rc;
}

static int oplus_chg_wls_rx_disable_vote_callback(struct votable *votable, void *data,
				int disable, const char *client, bool step)
{
	struct oplus_chg_wls *wls_dev = data;
	int rc;

	rc = oplus_chg_wls_rx_enable(wls_dev->wls_rx, !disable);
	if (rc < 0)
		pr_err("can't %s wireless charge\n", disable ? "disable" : "enable");
	else
		pr_info("%s wireless charge\n", disable ? "disable" : "enable");

	return rc;
}

static int oplus_chg_wls_set_cp_boost_enable(struct oplus_chg_wls *wls_dev, bool en)
{
	int rc;

	if (IS_ERR_OR_NULL(wls_dev->pinctrl) ||
	    IS_ERR_OR_NULL(wls_dev->cp_boost_en_active) ||
	    IS_ERR_OR_NULL(wls_dev->cp_boost_en_sleep)) {
		pr_err("cp_boost_en pinctrl error\n");
		return -ENODEV;
	}

	rc = pinctrl_select_state(wls_dev->pinctrl,
		en ? wls_dev->cp_boost_en_active : wls_dev->cp_boost_en_sleep);
	if (rc < 0)
		pr_err("can't %s cp_boost, rc=%d\n", en ? "enable" : "disable", rc);
	else
		pr_debug("set cp_boost %s\n", en ? "enable" : "disable");

	return rc;
}

#ifdef OPLUS_CHG_DEBUG
void oplus_chg_wls_set_config(struct oplus_chg_mod *wls_ocm, u8 *buf)
{
	struct oplus_chg_wls *wls_dev;
	struct oplus_chg_wls_dynamic_config *wls_cfg;
	int m, i;
	int index = 0;

	if (buf == NULL || wls_ocm == NULL)
		return;

	wls_dev = oplus_chg_mod_get_drvdata(wls_ocm);
	wls_cfg = &wls_dev->dynamic_config;

	index = load_word_val_by_buf(buf, index, &wls_cfg->batt_vol_max_mv);
	for (m = 0; m < OPLUS_WLS_CHG_MODE_MAX; m++) {
		for (i = 0; i < BATT_TEMP_INVALID; i++)
			index = load_word_val_by_buf(buf, index, &wls_cfg->iclmax_ma[OPLUS_WLS_CHG_BATT_CL_LOW][m][i]);
	}
	for (i = 0; i < OPLUS_WLS_CHG_MODE_MAX; i++) {
		wls_dev->icl_max_ma[i] = 0;
		for (m = 0; m < BATT_TEMP_INVALID; m++) {
			if (wls_cfg->iclmax_ma[OPLUS_WLS_CHG_BATT_CL_LOW][i][m] > wls_dev->icl_max_ma[i])
				wls_dev->icl_max_ma[i] = wls_cfg->iclmax_ma[OPLUS_WLS_CHG_BATT_CL_LOW][i][m];
		}
	}
	for (m = 0; m < OPLUS_WLS_CHG_MODE_MAX; m++) {
		for (i = 0; i < BATT_TEMP_INVALID; i++)
			index = load_word_val_by_buf(buf, index, &wls_cfg->iclmax_ma[OPLUS_WLS_CHG_BATT_CL_HIGH][m][i]);
	}
	for (i = 0; i < OPLUS_WLS_CHG_MODE_MAX; i++) {
		for (m = 0; m < BATT_TEMP_INVALID; m++) {
			if (wls_cfg->iclmax_ma[OPLUS_WLS_CHG_BATT_CL_HIGH][i][m] > wls_dev->icl_max_ma[i])
				wls_dev->icl_max_ma[i] = wls_cfg->iclmax_ma[OPLUS_WLS_CHG_BATT_CL_HIGH][i][m];
		}
	}
	for (i = 0; i < WLS_MAX_STEP_CHG_ENTRIES; i++) {
		index = load_word_val_by_buf(buf, index, &wls_cfg->fcc_step[i].low_threshold);
		index = load_word_val_by_buf(buf, index, &wls_cfg->fcc_step[i].high_threshold);
		index = load_word_val_by_buf(buf, index, &wls_cfg->fcc_step[i].curr_ma);
		index = load_word_val_by_buf(buf, index, &wls_cfg->fcc_step[i].vol_max_mv);
		index = load_word_val_by_buf(buf, index, &wls_cfg->fcc_step[i].need_wait);
		index = load_word_val_by_buf(buf, index, &wls_cfg->fcc_step[i].max_soc);
	}
	for (i = 0; i < WLS_MAX_STEP_CHG_ENTRIES; i++) {
		if (wls_cfg->fcc_step[i].low_threshold == 0 &&
		    wls_cfg->fcc_step[i].high_threshold == 0 &&
		    wls_cfg->fcc_step[i].curr_ma == 0 &&
		    wls_cfg->fcc_step[i].vol_max_mv == 0 &&
		    wls_cfg->fcc_step[i].need_wait == 0 &&
		    wls_cfg->fcc_step[i].max_soc == 0) {
			break;
		}
	}
	wls_dev->wls_fcc_step.max_step = i;
	for (i = 0; i < WLS_MAX_STEP_CHG_ENTRIES; i++) {
		index = load_word_val_by_buf(buf, index, &wls_cfg->epp_plus_skin_step[i].low_threshold);
		index = load_word_val_by_buf(buf, index, &wls_cfg->epp_plus_skin_step[i].high_threshold);
		index = load_word_val_by_buf(buf, index, &wls_cfg->epp_plus_skin_step[i].curr_ma);
	}
	for (i = 0; i < WLS_MAX_STEP_CHG_ENTRIES; i++) {
		if (wls_cfg->epp_plus_skin_step[i].low_threshold == 0 &&
		    wls_cfg->epp_plus_skin_step[i].high_threshold == 0 &&
		    wls_cfg->epp_plus_skin_step[i].curr_ma == 0) {
			break;
		}
	}
	wls_dev->epp_plus_skin_step.max_step = i;
	for (i = 0; i < WLS_MAX_STEP_CHG_ENTRIES; i++) {
		index = load_word_val_by_buf(buf, index, &wls_cfg->epp_skin_step[i].low_threshold);
		index = load_word_val_by_buf(buf, index, &wls_cfg->epp_skin_step[i].high_threshold);
		index = load_word_val_by_buf(buf, index, &wls_cfg->epp_skin_step[i].curr_ma);
	}
	for (i = 0; i < WLS_MAX_STEP_CHG_ENTRIES; i++) {
		if (wls_cfg->epp_skin_step[i].low_threshold == 0 &&
		    wls_cfg->epp_skin_step[i].high_threshold == 0 &&
		    wls_cfg->epp_skin_step[i].curr_ma == 0) {
			break;
		}
	}
	wls_dev->epp_skin_step.max_step = i;
	for (i = 0; i < WLS_MAX_STEP_CHG_ENTRIES; i++) {
		index = load_word_val_by_buf(buf, index, &wls_cfg->bpp_skin_step[i].low_threshold);
		index = load_word_val_by_buf(buf, index, &wls_cfg->bpp_skin_step[i].high_threshold);
		index = load_word_val_by_buf(buf, index, &wls_cfg->bpp_skin_step[i].curr_ma);
	}
	for (i = 0; i < WLS_MAX_STEP_CHG_ENTRIES; i++) {
		if (wls_cfg->bpp_skin_step[i].low_threshold == 0 &&
		    wls_cfg->bpp_skin_step[i].high_threshold == 0 &&
		    wls_cfg->bpp_skin_step[i].curr_ma == 0) {
			break;
		}
	}
	wls_dev->bpp_skin_step.max_step = i;
	index = load_word_val_by_buf(buf, index, &wls_cfg->fastchg_curr_max_ma);
}
#endif

static void oplus_chg_wls_get_third_part_verity_data_work(
				struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_wls *wls_dev =
		container_of(dwork, struct oplus_chg_wls, wls_get_third_part_verity_data_work);
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;

	oplus_chg_common_set_mutual_cmd(wls_dev->comm_ocm,
		CMD_WLS_THIRD_PART_AUTH,
		sizeof(wls_status->vendor_id), &(wls_status->vendor_id));
}
static void oplus_chg_wls_standard_msg_handler(struct oplus_chg_wls *wls_dev,
					       u8 mask, u8 data)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	struct oplus_chg_rx_msg *rx_msg = &wls_dev->rx_msg;
	bool msg_ok = false;
	int pwr_mw;

	pr_info("mask=0x%02x, msg_type=0x%02x\n", mask, rx_msg->msg_type);
	switch (mask) {
	case WLS_RESPONE_EXTERN_CMD:
		if (rx_msg->msg_type == WLS_CMD_GET_EXTERN_CMD) {
			pr_info("tx extern cmd=0x%02x\n", data);
			wls_status->tx_extern_cmd_done = true;
		}
		break;
	case WLS_RESPONE_VENDOR_ID:
		if (rx_msg->msg_type == WLS_CMD_GET_VENDOR_ID) {
			wls_status->vendor_id = data;
			wls_status->aes_verity_data_ok = false;
			if (is_comm_ocm_available(wls_dev) && !wls_status->verify_by_aes)
				schedule_delayed_work(&wls_dev->wls_get_third_part_verity_data_work, 0);
			wls_status->verify_by_aes = true;
			pr_info("verify_by_aes=%d, vendor_id=0x%02x\n",
				wls_status->verify_by_aes, wls_status->vendor_id);
		}
		break;
	case WLS_RESPONE_ADAPTER_TYPE:
		if (rx_msg->msg_type == WLS_CMD_INDENTIFY_ADAPTER) {
			wls_status->adapter_type = data & WLS_ADAPTER_TYPE_MASK;
			wls_status->adapter_id = (data & WLS_ADAPTER_ID_MASK) >> 3;
			pr_err("wkcs: adapter_id = %d\n", wls_status->adapter_id);
			pr_info("wkcs: adapter_type =0x%02x\n", wls_status->adapter_type);
			pwr_mw = oplus_chg_wls_get_base_power_max(wls_status->adapter_id);
			vote(wls_dev->fcc_votable, BASE_MAX_VOTER, true, (pwr_mw * 1000 / WLS_RX_VOL_MAX_MV), false);
			if (wls_dev->wls_ocm)
				oplus_chg_mod_changed(wls_dev->wls_ocm);
			if ((wls_status->adapter_type == WLS_ADAPTER_TYPE_WARP) ||
			    (wls_status->adapter_type == WLS_ADAPTER_TYPE_SWARP) ||
			    (wls_status->adapter_type == WLS_ADAPTER_TYPE_PD_65W)) {
				(void)oplus_chg_wls_rx_send_match_q(wls_dev->wls_rx, wls_dev->static_config.fastchg_match_q);
				pr_err("fastchg_match_q = 0x%02x\n", wls_dev->static_config.fastchg_match_q);
				msleep(200);
			}
		}
		break;
	case WLS_RESPONE_INTO_FASTCHAGE:
		if (rx_msg->msg_type == WLS_CMD_INTO_FASTCHAGE) {
			wls_status->adapter_power = data;
			if (wls_status->adapter_id != WLS_ADAPTER_THIRD_PARTY) {
				if (wls_status->adapter_power >= ARRAY_SIZE(oplus_chg_wls_pwr_table))
					wls_status->pwr_max_mw = 0;
				else
					wls_status->pwr_max_mw = oplus_chg_wls_get_r_power(wls_dev, wls_status->adapter_power);
				} else {
					wls_status->pwr_max_mw = oplus_chg_wls_get_tripartite_r_power(wls_status->adapter_power);
				}
			if (wls_status->pwr_max_mw > 0) {
				vote(wls_dev->fcc_votable, RX_MAX_VOTER, true,
				     (wls_status->pwr_max_mw * 1000 / WLS_RX_VOL_MAX_MV),
				     false);
			}
			wls_status->charge_type = WLS_CHARGE_TYPE_FAST;
			if (wls_dev->static_config.fastchg_fod_enable) {
				if(wls_status->fod_parm_for_fastchg)
					(void)oplus_chg_wls_rx_set_fod_parm(wls_dev->wls_rx,
						wls_dev->static_config.fastchg_fod_parm, WLS_FOD_PARM_LENGTH);
				else if(wls_dev->static_config.fastchg_12V_fod_enable)
					(void)oplus_chg_wls_rx_set_fod_parm(wls_dev->wls_rx,
						wls_dev->static_config.fastchg_fod_parm_12V, WLS_FOD_PARM_LENGTH);
			} else {
				(void)oplus_chg_wls_rx_set_fod_parm(wls_dev->wls_rx,
					oplus_chg_wls_disable_fod_parm, WLS_FOD_PARM_LENGTH);
			}
		}
		break;

	case WLS_RESPONE_INTO_NORMAL_MODE:
		if (rx_msg->msg_type == WLS_CMD_SET_NORMAL_MODE) {
			wls_status->quiet_mode = false;
			wls_status->quiet_mode_init = true;
		}
		break;
	case WLS_RESPONE_INTO_QUIET_MODE:
		if (rx_msg->msg_type == WLS_CMD_SET_QUIET_MODE) {
			wls_status->quiet_mode = true;
			wls_status->quiet_mode_init = true;
		}
		break;
	case WLS_RESPONE_FAN_SPEED:
		if (rx_msg->msg_type == WLS_CMD_SET_FAN_SPEED) {
			if (data == QUIET_MODE_FAN_THR_SPEED)
				wls_status->quiet_mode = true;
			else
				wls_status->quiet_mode = false;
		}
		break;
	case WLS_RESPONE_CEP_TIMEOUT:
		if (rx_msg->msg_type == WLS_CMD_SET_CEP_TIMEOUT) {
			wls_status->cep_timeout_adjusted = true;
		}
		break;
	case WLS_RESPONE_READY_FOR_EPP:
		wls_status->adapter_type = WLS_ADAPTER_TYPE_EPP;
		break;
	case WLS_RESPONE_WORKING_IN_EPP:
		wls_status->epp_working = true;
		break;
	default:
		break;
	}

	mutex_lock(&wls_dev->send_msg_lock);
	switch (mask) {
	case WLS_RESPONE_ADAPTER_TYPE:
		if (rx_msg->msg_type == WLS_CMD_INDENTIFY_ADAPTER)
			msg_ok = true;
		break;
	case WLS_RESPONE_INTO_FASTCHAGE:
		if (rx_msg->msg_type == WLS_CMD_INTO_FASTCHAGE)
			msg_ok = true;
		break;

	case WLS_RESPONE_INTO_NORMAL_MODE:
		if (rx_msg->msg_type == WLS_CMD_SET_NORMAL_MODE)
			msg_ok = true;
		break;
	case WLS_RESPONE_INTO_QUIET_MODE:
		if (rx_msg->msg_type == WLS_CMD_SET_QUIET_MODE)
			msg_ok = true;
		break;
	case WLS_RESPONE_CEP_TIMEOUT:
		if (rx_msg->msg_type == WLS_CMD_SET_CEP_TIMEOUT)
			msg_ok = true;
		break;
	case WLS_RESPONE_LED_BRIGHTNESS:
		if (rx_msg->msg_type == WLS_CMD_SET_LED_BRIGHTNESS)
			msg_ok = true;
		break;
	case WLS_RESPONE_FAN_SPEED:
		if (rx_msg->msg_type == WLS_CMD_SET_FAN_SPEED)
			msg_ok = true;
		break;
	case WLS_RESPONE_VENDOR_ID:
		if (rx_msg->msg_type == WLS_CMD_GET_VENDOR_ID)
			msg_ok = true;
		break;
	case WLS_RESPONE_EXTERN_CMD:
		if (rx_msg->msg_type == WLS_CMD_GET_EXTERN_CMD)
			msg_ok = true;
		break;
	default:
		pr_err("unknown msg respone(=%d)\n", mask);
	}
	mutex_unlock(&wls_dev->send_msg_lock);

	if (msg_ok) {
		cancel_delayed_work_sync(&wls_dev->wls_send_msg_work);
		complete(&wls_dev->msg_ack);
	}
}

static void oplus_chg_wls_data_msg_handler(struct oplus_chg_wls *wls_dev,
					   u8 mask, u8 data[3])
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	struct oplus_chg_rx_msg *rx_msg = &wls_dev->rx_msg;
	bool msg_ok = false;

	switch (mask) {
	case WLS_RESPONE_ENCRYPT_DATA4:
		if (rx_msg->msg_type == WLS_CMD_GET_ENCRYPT_DATA4) {
			wls_status->encrypt_data[0] = data[0];
			wls_status->encrypt_data[1] = data[1];
			wls_status->encrypt_data[2] = data[2];
		}
		break;
	case WLS_RESPONE_ENCRYPT_DATA5:
		if (rx_msg->msg_type == WLS_CMD_GET_ENCRYPT_DATA5) {
			wls_status->encrypt_data[3] = data[0];
			wls_status->encrypt_data[4] = data[1];
			wls_status->encrypt_data[5] = data[2];
		}
		break;
	case WLS_RESPONE_ENCRYPT_DATA6:
		if (rx_msg->msg_type == WLS_CMD_GET_ENCRYPT_DATA6) {
			wls_status->encrypt_data[6] = data[0];
			wls_status->encrypt_data[7] = data[1];
		}
		break;
	case WLS_RESPONE_GET_AES_DATA1:
		if (rx_msg->msg_type == WLS_CMD_GET_AES_DATA1) {
			wls_status->aes_encrypt_data[0] = data[0];
			wls_status->aes_encrypt_data[1] = data[1];
			wls_status->aes_encrypt_data[2] = data[2];
		}
		break;
	case WLS_RESPONE_GET_AES_DATA2:
		if (rx_msg->msg_type == WLS_CMD_GET_AES_DATA2) {
			wls_status->aes_encrypt_data[3] = data[0];
			wls_status->aes_encrypt_data[4] = data[1];
			wls_status->aes_encrypt_data[5] = data[2];
		}
		break;
	case WLS_RESPONE_GET_AES_DATA3:
		if (rx_msg->msg_type == WLS_CMD_GET_AES_DATA3) {
			wls_status->aes_encrypt_data[6] = data[0];
			wls_status->aes_encrypt_data[7] = data[1];
			wls_status->aes_encrypt_data[8] = data[2];
		}
		break;
	case WLS_RESPONE_GET_AES_DATA4:
		if (rx_msg->msg_type == WLS_CMD_GET_AES_DATA4) {
			wls_status->aes_encrypt_data[9] = data[0];
			wls_status->aes_encrypt_data[10] = data[1];
			wls_status->aes_encrypt_data[11] = data[2];
		}
		break;
	case WLS_RESPONE_GET_AES_DATA5:
		if (rx_msg->msg_type == WLS_CMD_GET_AES_DATA5) {
			wls_status->aes_encrypt_data[12] = data[0];
			wls_status->aes_encrypt_data[13] = data[1];
			wls_status->aes_encrypt_data[14] = data[2];
		}
		break;
	case WLS_RESPONE_GET_AES_DATA6:
		if (rx_msg->msg_type == WLS_CMD_GET_AES_DATA6) {
			wls_status->aes_encrypt_data[15] = data[0];
		}
		break;
	case WLS_RESPONE_PRODUCT_ID:
		if (rx_msg->msg_type == WLS_CMD_GET_PRODUCT_ID) {
			wls_status->tx_product_id_done = true;
			wls_status->product_id = (data[0] << 8) | data[1];
			pr_info("product_id:0x%x, tx_product_id_done:%d\n",
			    wls_status->product_id, wls_status->tx_product_id_done);
		}
		break;
	case WLS_RESPONE_BATT_TEMP_SOC:
		if (rx_msg->msg_type == WLS_CMD_SEND_BATT_TEMP_SOC) {
			pr_info("temp:%d, soc:%d\n", (data[0] << 8) | data[1], data[2]);
		}
		break;
	default:
		break;
	}

	mutex_lock(&wls_dev->send_msg_lock);
	switch (mask) {
	case WLS_RESPONE_ENCRYPT_DATA1:
		if (rx_msg->msg_type == WLS_CMD_GET_ENCRYPT_DATA1)
			msg_ok = true;
		break;
	case WLS_RESPONE_ENCRYPT_DATA2:
		if (rx_msg->msg_type == WLS_CMD_GET_ENCRYPT_DATA2)
			msg_ok = true;
		break;
	case WLS_RESPONE_ENCRYPT_DATA3:
		if (rx_msg->msg_type == WLS_CMD_GET_ENCRYPT_DATA3)
			msg_ok = true;
		break;
	case WLS_RESPONE_ENCRYPT_DATA4:
		if (rx_msg->msg_type == WLS_CMD_GET_ENCRYPT_DATA4)
			msg_ok = true;
		break;
	case WLS_RESPONE_ENCRYPT_DATA5:
		if (rx_msg->msg_type == WLS_CMD_GET_ENCRYPT_DATA5)
			msg_ok = true;
		break;
	case WLS_RESPONE_ENCRYPT_DATA6:
		if (rx_msg->msg_type == WLS_CMD_GET_ENCRYPT_DATA6)
			msg_ok = true;
		break;
	case WLS_RESPONE_SET_AES_DATA1:
		if (rx_msg->msg_type == WLS_CMD_SET_AES_DATA1)
			msg_ok = true;
		break;
	case WLS_RESPONE_SET_AES_DATA2:
		if (rx_msg->msg_type == WLS_CMD_SET_AES_DATA2)
			msg_ok = true;
		break;
	case WLS_RESPONE_SET_AES_DATA3:
		if (rx_msg->msg_type == WLS_CMD_SET_AES_DATA3)
			msg_ok = true;
		break;
	case WLS_RESPONE_SET_AES_DATA4:
		if (rx_msg->msg_type == WLS_CMD_SET_AES_DATA4)
			msg_ok = true;
		break;
	case WLS_RESPONE_SET_AES_DATA5:
		if (rx_msg->msg_type == WLS_CMD_SET_AES_DATA5)
			msg_ok = true;
		break;
	case WLS_RESPONE_SET_AES_DATA6:
		if (rx_msg->msg_type == WLS_CMD_SET_AES_DATA6)
			msg_ok = true;
		break;
	case WLS_RESPONE_GET_AES_DATA1:
		if (rx_msg->msg_type == WLS_CMD_GET_AES_DATA1)
			msg_ok = true;
		break;
	case WLS_RESPONE_GET_AES_DATA2:
		if (rx_msg->msg_type == WLS_CMD_GET_AES_DATA2)
			msg_ok = true;
		break;
	case WLS_RESPONE_GET_AES_DATA3:
		if (rx_msg->msg_type == WLS_CMD_GET_AES_DATA3)
			msg_ok = true;
		break;
	case WLS_RESPONE_GET_AES_DATA4:
		if (rx_msg->msg_type == WLS_CMD_GET_AES_DATA4)
			msg_ok = true;
		break;
	case WLS_RESPONE_GET_AES_DATA5:
		if (rx_msg->msg_type == WLS_CMD_GET_AES_DATA5)
			msg_ok = true;
		break;
	case WLS_RESPONE_GET_AES_DATA6:
		if (rx_msg->msg_type == WLS_CMD_GET_AES_DATA6)
			msg_ok = true;
		break;
	case WLS_RESPONE_PRODUCT_ID:
		if (rx_msg->msg_type == WLS_CMD_GET_PRODUCT_ID)
			msg_ok = true;
		break;
	case WLS_RESPONE_BATT_TEMP_SOC:
		if (rx_msg->msg_type == WLS_CMD_SEND_BATT_TEMP_SOC)
			msg_ok = true;
		break;
	default:
		pr_err("unknown msg respone(=%d)\n", mask);
	}
	mutex_unlock(&wls_dev->send_msg_lock);

	if (msg_ok) {
		cancel_delayed_work_sync(&wls_dev->wls_send_msg_work);
		complete(&wls_dev->msg_ack);
	}
}

static bool oplus_chg_wls_is_oplus_trx(u8 id)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(oplus_trx_id_table); i++) {
		if (oplus_trx_id_table[i] == id)
			return true;
	}

	return false;
}

static void oplus_chg_wls_extended_msg_handler(struct oplus_chg_wls *wls_dev,
					       u8 data[])
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	bool msg_ok = false;

	pr_info("msg_type=0x%02x\n", data[0]);

	switch (data[0]) {
	case WLS_CMD_GET_TX_ID:
		if (data[4] == 0x04 && data[2] == 0x05 && oplus_chg_wls_is_oplus_trx(data[3])) {
			wls_status->is_op_trx = true;
			pr_info("is oplus trx\n");
		}
		msg_ok = true;
		break;
	case WLS_CMD_GET_TX_PWR:
		wls_status->tx_pwr_mw = (data[2] << 8) | data[1];
		wls_status->rx_pwr_mw = (data[4] << 8) | data[3];
		wls_status->ploos_mw = wls_status->tx_pwr_mw - wls_status->rx_pwr_mw;
		pr_err("tx_pwr=%d, rx_pwr=%d, ploos=%d\n", wls_status->tx_pwr_mw,
			wls_status->rx_pwr_mw, wls_status->ploos_mw);
		break;
	default:
		pr_err("unknown msg_type(=%d)\n", data[0]);
	}

	if (msg_ok) {
		cancel_delayed_work_sync(&wls_dev->wls_send_msg_work);
		complete(&wls_dev->msg_ack);
	}
}

static void oplus_chg_wls_rx_msg_callback(void *dev_data, u8 data[])
{
	struct oplus_chg_wls *wls_dev = dev_data;
	struct oplus_chg_wls_status *wls_status;
	struct oplus_chg_rx_msg *rx_msg;
	u8 temp[2];

	if (wls_dev == NULL) {
		pr_err("wls_dev is NULL\n");
		return;
	}

	wls_status = &wls_dev->wls_status;
	rx_msg = &wls_dev->rx_msg;

	temp[0] = ~data[2];
	temp[1] = ~data[4];
	switch (data[0]) {
	case WLS_MSG_TYPE_STANDARD_MSG:
		pr_info("received: standrad msg\n");
		if (!((data[1] >= WLS_RESPONE_ENCRYPT_DATA1 && data[1] <= WLS_RESPONE_ENCRYPT_DATA6) ||
		    (data[1] >= WLS_RESPONE_SET_AES_DATA1 && data[1] <= WLS_RESPONE_GET_AES_DATA6) ||
		     data[1] == WLS_RESPONE_PRODUCT_ID || data[1] == WLS_RESPONE_BATT_TEMP_SOC)) {
			if ((data[1] == temp[0]) && (data[3] == temp[1])) {
				pr_info("Received TX command: 0x%02X, data: 0x%02X\n",
					data[1], data[3]);
				oplus_chg_wls_standard_msg_handler(wls_dev, data[1], data[3]);
			} else {
				pr_err("msg data error\n");
			}
		} else {
			pr_info("Received TX data: 0x%02X, 0x%02X, 0x%02X, 0x%02X\n", data[1], data[2], data[3], data[4]);
			oplus_chg_wls_data_msg_handler(wls_dev, data[1], &data[2]);
		}
		break;
	case WLS_MSG_TYPE_EXTENDED_MSG:
		oplus_chg_wls_extended_msg_handler(wls_dev, &data[1]);
		break;
	default:
		pr_err("Unknown msg type(=%d)\n", data[0]);
	}
}

static void oplus_chg_wls_send_msg_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_wls *wls_dev =
		container_of(dwork, struct oplus_chg_wls, wls_send_msg_work);
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	struct oplus_chg_rx_msg *rx_msg = &wls_dev->rx_msg;
	int delay_ms = 1000;
	int cep;
	int rc;

	if (!wls_status->rx_online) {
		pr_err("wireless charge is not online\n");
		complete(&wls_dev->msg_ack);
		return;
	}

	if (rx_msg->msg_type == WLS_CMD_GET_TX_PWR || rx_msg->long_data) {
		// need wait cep
		rc = oplus_chg_wls_get_cep(wls_dev->wls_rx, &cep);
		if (rc < 0) {
			pr_err("can't read cep, rc=%d\n", rc);
			complete(&wls_dev->msg_ack);
			return;
		}
		pr_info("wkcs: cep = %d\n", cep);
		if (abs(cep) > 3) {
			delay_ms = 3000;
			goto out;
		}
		pr_info("wkcs: get tx pwr\n");
	}
	if (rx_msg->long_data)
		rc = oplus_chg_wls_rx_send_data(wls_dev->wls_rx, rx_msg->msg_type,
			rx_msg->buf, ARRAY_SIZE(rx_msg->buf));
	else
		rc = oplus_chg_wls_rx_send_msg(wls_dev->wls_rx, rx_msg->msg_type, rx_msg->data);
	if (rc < 0) {
		pr_err("rx send msg error, rc=%d\n", rc);
		complete(&wls_dev->msg_ack);
		return;
	}

out:
	schedule_delayed_work(&wls_dev->wls_send_msg_work, msecs_to_jiffies(delay_ms));
}

static int oplus_chg_wls_send_msg(struct oplus_chg_wls *wls_dev, u8 msg, u8 data, int wait_time_s)
{
	struct oplus_chg_rx_msg *rx_msg = &wls_dev->rx_msg;
	int cep;
	int rc;

	if (!wls_dev->msg_callback_ok) {
		rc = oplus_chg_wls_rx_register_msg_callback(wls_dev->wls_rx, wls_dev,
			oplus_chg_wls_rx_msg_callback);
		if (rc < 0) {
			pr_err("can't reg msg callback, rc=%d\n", rc);
			return rc;
		} else {
			wls_dev->msg_callback_ok = true;
		}
	}

	if (rx_msg->pending) {
		pr_err("msg pending\n");
		return -EAGAIN;
	}

	if (msg == WLS_CMD_GET_TX_PWR) {
		// need wait cep
		rc = oplus_chg_wls_get_cep(wls_dev->wls_rx, &cep);
		if (rc < 0) {
			pr_err("can't read cep, rc=%d\n", rc);
			return rc;
		}
		if (abs(cep) > 3) {
			pr_info("wkcs: cep = %d\n", cep);
			return -EAGAIN;
		}
	}

	rc = oplus_chg_wls_rx_send_msg(wls_dev->wls_rx, msg, data);
	if (rc) {
		pr_err("send msg error, rc=%d\n", rc);
		return rc;
	}

	mutex_lock(&wls_dev->send_msg_lock);
	rx_msg->msg_type = msg;
	rx_msg->data = data;
	rx_msg->long_data = false;
	mutex_unlock(&wls_dev->send_msg_lock);
	if (wait_time_s > 0) {
		rx_msg->pending = true;
		reinit_completion(&wls_dev->msg_ack);
		schedule_delayed_work(&wls_dev->wls_send_msg_work, msecs_to_jiffies(1000));
		rc = wait_for_completion_timeout(&wls_dev->msg_ack, msecs_to_jiffies(wait_time_s * 1000));
		if (!rc) {
			pr_err("Error, timed out sending message\n");
			cancel_delayed_work_sync(&wls_dev->wls_send_msg_work);
			rc = -ETIMEDOUT;
		}
		rx_msg->msg_type = 0;
		rx_msg->data = 0;
		rx_msg->buf[0] = 0;
		rx_msg->buf[1] = 0;
		rx_msg->buf[2] = 0;
		rx_msg->respone_type = 0;
		rx_msg->long_data = false;
		rx_msg->pending = false;
	} else if (wait_time_s < 0) {
		rx_msg->pending = false;
		schedule_delayed_work(&wls_dev->wls_send_msg_work, msecs_to_jiffies(1000));
	}

	return rc;
}

static int oplus_chg_wls_send_data(struct oplus_chg_wls *wls_dev, u8 msg, u8 data[3], int wait_time_s)
{
	struct oplus_chg_rx_msg *rx_msg = &wls_dev->rx_msg;
	int cep;
	int rc;

	if (!wls_dev->msg_callback_ok) {
		rc = oplus_chg_wls_rx_register_msg_callback(wls_dev->wls_rx, wls_dev,
			oplus_chg_wls_rx_msg_callback);
		if (rc < 0) {
			pr_err("can't reg msg callback, rc=%d\n", rc);
			return rc;
		} else {
			wls_dev->msg_callback_ok = true;
		}
	}

	if (rx_msg->pending) {
		pr_err("msg pending\n");
		return -EAGAIN;
	}

	/* need wait cep */
	rc = oplus_chg_wls_get_cep(wls_dev->wls_rx, &cep);
	if (rc < 0) {
		pr_err("can't read cep, rc=%d\n", rc);
		return rc;
	}
	if (abs(cep) > 3) {
		pr_info("wkcs: cep = %d\n", cep);
		return -EAGAIN;
	}

	rc = oplus_chg_wls_rx_send_data(wls_dev->wls_rx, msg, data, 3);
	if (rc) {
		pr_err("send data error, rc=%d\n", rc);
		return rc;
	}

	mutex_lock(&wls_dev->send_msg_lock);
	rx_msg->msg_type = msg;
	rx_msg->buf[0] = data[0];
	rx_msg->buf[1] = data[1];
	rx_msg->buf[2] = data[2];
	rx_msg->long_data = true;
	mutex_unlock(&wls_dev->send_msg_lock);
	if (wait_time_s > 0) {
		rx_msg->pending = true;
		reinit_completion(&wls_dev->msg_ack);
		schedule_delayed_work(&wls_dev->wls_send_msg_work, msecs_to_jiffies(1000));
		rc = wait_for_completion_timeout(&wls_dev->msg_ack, msecs_to_jiffies(wait_time_s * 1000));
		if (!rc) {
			pr_err("Error, timed out sending message\n");
			cancel_delayed_work_sync(&wls_dev->wls_send_msg_work);
			rc = -ETIMEDOUT;
		}
		rx_msg->msg_type = 0;
		rx_msg->data = 0;
		rx_msg->buf[0] = 0;
		rx_msg->buf[1] = 0;
		rx_msg->buf[2] = 0;
		rx_msg->respone_type = 0;
		rx_msg->long_data = false;
		rx_msg->pending = false;
	} else if (wait_time_s < 0) {
		rx_msg->pending = false;
		schedule_delayed_work(&wls_dev->wls_send_msg_work, msecs_to_jiffies(1000));
	}

	return rc;
}

static void oplus_chg_wls_clean_msg(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_rx_msg *rx_msg = &wls_dev->rx_msg;

	cancel_delayed_work_sync(&wls_dev->wls_send_msg_work);
	if (rx_msg->pending)
		complete(&wls_dev->msg_ack);
	rx_msg->msg_type = 0;
	rx_msg->data = 0;
	rx_msg->buf[0] = 0;
	rx_msg->buf[1] = 0;
	rx_msg->buf[2] = 0;
	rx_msg->respone_type = 0;
	rx_msg->long_data = false;
	rx_msg->pending = false;
}

static int oplus_chg_wls_get_verity_data(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;

#ifdef CONFIG_OPLUS_CHG_OOS
	mutex_lock(&wls_dev->cmd_data_lock);
	memset(&wls_dev->cmd, 0, sizeof(struct wls_dev_cmd));
	wls_dev->cmd.cmd = WLS_DEV_CMD_WLS_AUTH;
	wls_dev->cmd.data_size = 0;
	wls_dev->cmd_data_ok = true;
	wls_status->verity_data_ok = false;
	mutex_unlock(&wls_dev->cmd_data_lock);
	wake_up(&wls_dev->read_wq);
#else /* CONFIG_OPLUS_CHG_OOS */
#ifdef CONFIG_OPLUS_CHARGER_MTK
	/* MTK not support */
	wls_status->verity_data_ok = false;
	return -EINVAL;
#else /* CONFIG_OPLUS_CHARGER_MTK */
	size_t smem_size;
	void *smem_addr;
	struct oplus_chg_auth_result *smem_data;

	wls_status->verity_data_ok = false;
	smem_addr = qcom_smem_get(QCOM_SMEM_HOST_ANY, SMEM_RESERVED_BOOT_INFO_FOR_APPS, &smem_size);
	if (IS_ERR(smem_addr)) {
		pr_err("unable to acquire smem SMEM_RESERVED_BOOT_INFO_FOR_APPS entry\n");
		return PTR_ERR(smem_addr);
	} else {
		smem_data = (struct oplus_chg_auth_result *)smem_addr;
		if (smem_data == ERR_PTR(-EPROBE_DEFER)) {
			smem_data = NULL;
			pr_err("fail to get smem_data\n");
			return -EPROBE_DEFER;
		} else {
			memcpy(wls_status->verfity_data.random_num, &smem_data->wls_auth_data.random_num, WLS_AUTH_RANDOM_LEN);
			memcpy(wls_status->verfity_data.encode_num, &smem_data->wls_auth_data.encode_num, WLS_AUTH_ENCODE_LEN);
			wls_status->verity_data_ok = true;
			pr_info("random number: %02x %02x %02x %02x %02x %02x %02x %02x\n",
				wls_status->verfity_data.random_num[0], wls_status->verfity_data.random_num[1],
				wls_status->verfity_data.random_num[2], wls_status->verfity_data.random_num[3],
				wls_status->verfity_data.random_num[4], wls_status->verfity_data.random_num[5],
				wls_status->verfity_data.random_num[6], wls_status->verfity_data.random_num[7]);
			pr_info("encode number: %02x %02x %02x %02x %02x %02x %02x %02x\n",
				wls_status->verfity_data.encode_num[0], wls_status->verfity_data.encode_num[1],
				wls_status->verfity_data.encode_num[2], wls_status->verfity_data.encode_num[3],
				wls_status->verfity_data.encode_num[4], wls_status->verfity_data.encode_num[5],
				wls_status->verfity_data.encode_num[6], wls_status->verfity_data.encode_num[7]);
		}
	}
#endif /* CONFIG_OPLUS_CHARGER_MTK */
#endif /* CONFIG_OPLUS_CHG_OOS */

	return 0;
}

#define VERITY_TIMEOUT_MAX	40
static void oplus_chg_wls_des_verity(struct oplus_chg_wls *wls_dev)
{
	u8 buf[3];
	int rc;
	struct oplus_chg_wls_status *wls_status;

	if (!wls_dev)
		return;

	wls_status = &wls_dev->wls_status;
	if (!wls_status->rx_online)
		return;

	wls_status->verity_started = true;

	wls_status->verity_wait_timeout = jiffies + VERITY_TIMEOUT_MAX * HZ;
retry:
	if (!wls_status->verity_data_ok) {
		pr_err("verity data not ready\n");
		if (wls_status->verity_count < 5) {
			wls_status->verity_count++;
			rc = oplus_chg_wls_get_verity_data(wls_dev);
			if (rc < 0) {
				pr_err("can't get verity data\n");
				wls_status->verity_pass = false;
				goto done;
			}
			schedule_delayed_work(&wls_dev->wls_verity_work,
					      msecs_to_jiffies(500));
			return;
		}
		wls_status->verity_pass = false;
		goto done;
	}

	buf[0] = wls_status->verfity_data.random_num[0];
	buf[1] = wls_status->verfity_data.random_num[1];
	buf[2] = wls_status->verfity_data.random_num[2];
	wls_status->verity_count = 0;
	do {
		rc = oplus_chg_wls_send_data(wls_dev, WLS_CMD_GET_ENCRYPT_DATA1, buf, 5);
		if (rc < 0) {
			if (rc != -EAGAIN)
				wls_status->verity_count++;
			msleep(500);
		}
	} while (rc < 0 && wls_status->verity_count < 5 && wls_status->rx_online);
	if (rc < 0 || !wls_status->rx_online) {
		pr_err("send 0x%02x error, rc=%d\n", WLS_CMD_GET_ENCRYPT_DATA1, rc);
		wls_status->verity_pass = false;
		goto done;
	}
	buf[0] = wls_status->verfity_data.random_num[3];
	buf[1] = wls_status->verfity_data.random_num[4];
	buf[2] = wls_status->verfity_data.random_num[5];
	wls_status->verity_count = 0;
	do {
		rc = oplus_chg_wls_send_data(wls_dev, WLS_CMD_GET_ENCRYPT_DATA2, buf, 5);
		if (rc < 0) {
			if (rc != -EAGAIN)
				wls_status->verity_count++;
			msleep(500);
		}
	} while (rc < 0 && wls_status->verity_count < 5 && wls_status->rx_online);
	if (rc < 0 || !wls_status->rx_online) {
		pr_err("send 0x%02x error, rc=%d\n", WLS_CMD_GET_ENCRYPT_DATA2, rc);
		wls_status->verity_pass = false;
		goto done;
	}
	buf[0] = wls_status->verfity_data.random_num[6];
	buf[1] = wls_status->verfity_data.random_num[7];
	buf[2] = WLS_ENCODE_MASK;
	wls_status->verity_count = 0;
	do {
		rc = oplus_chg_wls_send_data(wls_dev, WLS_CMD_GET_ENCRYPT_DATA3, buf, 5);
		if (rc < 0) {
			if (rc != -EAGAIN)
				wls_status->verity_count++;
			msleep(500);
		}
	} while (rc < 0 && wls_status->verity_count < 5 && wls_status->rx_online);
	if (rc < 0 || !wls_status->rx_online) {
		pr_err("send 0x%02x error, rc=%d\n", WLS_CMD_GET_ENCRYPT_DATA3, rc);
		wls_status->verity_pass = false;
		goto done;
	}

	/* Wait for the base encryption to complete */
	msleep(500);

	wls_status->verity_count = 0;
	do {
		rc = oplus_chg_wls_send_msg(wls_dev, WLS_CMD_GET_ENCRYPT_DATA4, 0xff, 5);
		if (rc < 0) {
			if (rc != -EAGAIN)
				wls_status->verity_count++;
			msleep(500);
		}
	} while (rc < 0 && wls_status->verity_count < 5 && wls_status->rx_online);
	if (rc < 0 || !wls_status->rx_online) {
		pr_err("send 0x%02x error, rc=%d\n", WLS_CMD_GET_ENCRYPT_DATA4, rc);
		wls_status->verity_pass = false;
		goto done;
	}
	wls_status->verity_count = 0;
	do {
		rc = oplus_chg_wls_send_msg(wls_dev, WLS_CMD_GET_ENCRYPT_DATA5, 0xff, 5);
		if (rc < 0) {
			if (rc != -EAGAIN)
				wls_status->verity_count++;
			msleep(500);
		}
	} while (rc < 0 && wls_status->verity_count < 5 && wls_status->rx_online);
	if (rc < 0 || !wls_status->rx_online) {
		pr_err("send 0x%02x error, rc=%d\n", WLS_CMD_GET_ENCRYPT_DATA5, rc);
		wls_status->verity_pass = false;
		goto done;
	}
	wls_status->verity_count = 0;
	do {
		rc = oplus_chg_wls_send_msg(wls_dev, WLS_CMD_GET_ENCRYPT_DATA6, 0xff, 5);
		if (rc < 0) {
			if (rc != -EAGAIN)
				wls_status->verity_count++;
			msleep(500);
		}
	} while (rc < 0 && wls_status->verity_count < 5 && wls_status->rx_online);
	if (rc < 0 || !wls_status->rx_online) {
		pr_err("send 0x%02x error, rc=%d\n", WLS_CMD_GET_ENCRYPT_DATA6, rc);
		wls_status->verity_pass = false;
		goto done;
	}

	pr_info("encrypt_data: 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x",
		wls_status->encrypt_data[0], wls_status->encrypt_data[1],
		wls_status->encrypt_data[2], wls_status->encrypt_data[3],
		wls_status->encrypt_data[4], wls_status->encrypt_data[5],
		wls_status->encrypt_data[6], wls_status->encrypt_data[7]);

	if (memcmp(&wls_status->encrypt_data,
		   &wls_status->verfity_data.encode_num, WLS_AUTH_ENCODE_LEN)) {
		pr_err("verity faile\n");
		wls_status->verity_pass = false;
	} else {
		pr_err("verity pass\n");
		wls_status->verity_pass = true;
	}

done:
	wls_status->verity_count = 0;
	if (!wls_status->verity_pass && wls_status->rx_online &&
	    time_before(jiffies, wls_status->verity_wait_timeout))
		goto retry;
	if (!wls_status->verity_pass && wls_status->rx_online) {
		wls_status->online_keep = true;
		wls_status->verity_state_keep = true;
		vote(wls_dev->rx_disable_votable, VERITY_VOTER, true, 1, false);
		schedule_delayed_work(&wls_dev->rx_verity_restore_work, msecs_to_jiffies(500));
	}
}

static void oplus_chg_wls_aes_verity(struct oplus_chg_wls *wls_dev)
{
	u8 buf[3];
	int rc;
	int ret;
	struct oplus_chg_wls_status *wls_status;

	if (!wls_dev)
		return;

	wls_status = &wls_dev->wls_status;
	if (!wls_status->rx_online)
		return;

	wls_status->verity_started = true;
#ifndef OPLUS_CHG_WLS_AUTH_ENABLE
	wls_status->verity_pass = true;
	goto done;
#endif

	wls_status->verity_wait_timeout = jiffies + VERITY_TIMEOUT_MAX * HZ;
retry:
	if (!wls_status->aes_verity_data_ok) {
		pr_err("verity data not ready\n");
		if (is_comm_ocm_available(wls_dev))
			ret = oplus_chg_common_set_mutual_cmd(wls_dev->comm_ocm,
					CMD_WLS_THIRD_PART_AUTH,
					sizeof(wls_status->vendor_id), &(wls_status->vendor_id));
		else
			goto done;
		if (ret == CMD_ERROR_HIDL_NOT_READY) {
			schedule_delayed_work(&wls_dev->wls_verity_work,
						      msecs_to_jiffies(1000));
			return;
		} else if (ret != CMD_ACK_OK) {
			if (wls_status->verity_count < 5) {
				wls_status->verity_count++;
				schedule_delayed_work(&wls_dev->wls_verity_work,
						      msecs_to_jiffies(1500));
				return;
			}
			wls_status->verity_pass = false;
			goto done;
		}
	}

	buf[0] = wls_status->aes_verfity_data.aes_random_num[0];
	buf[1] = wls_status->aes_verfity_data.aes_random_num[1];
	buf[2] = wls_status->aes_verfity_data.aes_random_num[2];
	wls_status->verity_count = 0;
	do {
		rc = oplus_chg_wls_send_data(wls_dev, WLS_CMD_SET_AES_DATA1, buf, 5);
		if (rc < 0) {
			if (rc != -EAGAIN)
				wls_status->verity_count++;
			msleep(500);
		}
	} while (rc < 0 && wls_status->verity_count < 5 && wls_status->rx_online);
	if (rc < 0 || !wls_status->rx_online) {
		pr_err("send 0x%02x error, rc=%d\n", WLS_CMD_SET_AES_DATA1, rc);
		wls_status->verity_pass = false;
		goto done;
	}
	msleep(500);
	buf[0] = wls_status->aes_verfity_data.aes_random_num[3];
	buf[1] = wls_status->aes_verfity_data.aes_random_num[4];
	buf[2] = wls_status->aes_verfity_data.aes_random_num[5];
	wls_status->verity_count = 0;
	do {
		rc = oplus_chg_wls_send_data(wls_dev, WLS_CMD_SET_AES_DATA2, buf, 5);
		if (rc < 0) {
			if (rc != -EAGAIN)
				wls_status->verity_count++;
			msleep(500);
		}
	} while (rc < 0 && wls_status->verity_count < 5 && wls_status->rx_online);
	if (rc < 0 || !wls_status->rx_online) {
		pr_err("send 0x%02x error, rc=%d\n", WLS_CMD_SET_AES_DATA2, rc);
		wls_status->verity_pass = false;
		goto done;
	}
	msleep(500);
	buf[0] = wls_status->aes_verfity_data.aes_random_num[6];
	buf[1] = wls_status->aes_verfity_data.aes_random_num[7];
	buf[2] = wls_status->aes_verfity_data.aes_random_num[8];
	wls_status->verity_count = 0;
	do {
		rc = oplus_chg_wls_send_data(wls_dev, WLS_CMD_SET_AES_DATA3, buf, 5);
		if (rc < 0) {
			if (rc != -EAGAIN)
				wls_status->verity_count++;
			msleep(500);
		}
	} while (rc < 0 && wls_status->verity_count < 5 && wls_status->rx_online);
	if (rc < 0 || !wls_status->rx_online) {
		pr_err("send 0x%02x error, rc=%d\n", WLS_CMD_SET_AES_DATA3, rc);
		wls_status->verity_pass = false;
		goto done;
	}
	msleep(500);
	buf[0] = wls_status->aes_verfity_data.aes_random_num[9];
	buf[1] = wls_status->aes_verfity_data.aes_random_num[10];
	buf[2] = wls_status->aes_verfity_data.aes_random_num[11];
	wls_status->verity_count = 0;
	do {
		rc = oplus_chg_wls_send_data(wls_dev, WLS_CMD_SET_AES_DATA4, buf, 5);
		if (rc < 0) {
			if (rc != -EAGAIN)
				wls_status->verity_count++;
			msleep(500);
		}
	} while (rc < 0 && wls_status->verity_count < 5 && wls_status->rx_online);
	if (rc < 0 || !wls_status->rx_online) {
		pr_err("send 0x%02x error, rc=%d\n", WLS_CMD_SET_AES_DATA4, rc);
		wls_status->verity_pass = false;
		goto done;
	}
	msleep(500);
	buf[0] = wls_status->aes_verfity_data.aes_random_num[12];
	buf[1] = wls_status->aes_verfity_data.aes_random_num[13];
	buf[2] = wls_status->aes_verfity_data.aes_random_num[14];
	wls_status->verity_count = 0;
	do {
		rc = oplus_chg_wls_send_data(wls_dev, WLS_CMD_SET_AES_DATA5, buf, 5);
		if (rc < 0) {
			if (rc != -EAGAIN)
				wls_status->verity_count++;
			msleep(500);
		}
	} while (rc < 0 && wls_status->verity_count < 5 && wls_status->rx_online);
	if (rc < 0 || !wls_status->rx_online) {
		pr_err("send 0x%02x error, rc=%d\n", WLS_CMD_SET_AES_DATA5, rc);
		wls_status->verity_pass = false;
		goto done;
	}
	msleep(500);
	buf[0] = wls_status->aes_verfity_data.aes_random_num[15];
	buf[1] = wls_status->vendor_id;
	buf[2] = wls_status->aes_key_num;
	wls_status->verity_count = 0;
	do {
		rc = oplus_chg_wls_send_data(wls_dev, WLS_CMD_SET_AES_DATA6, buf, 5);
		if (rc < 0) {
			if (rc != -EAGAIN)
				wls_status->verity_count++;
			msleep(500);
		}
	} while (rc < 0 && wls_status->verity_count < 5 && wls_status->rx_online);
	if (rc < 0 || !wls_status->rx_online) {
		pr_err("send 0x%02x error, rc=%d\n", WLS_CMD_SET_AES_DATA6, rc);
		wls_status->verity_pass = false;
		goto done;
	}
	/* Wait for the base encryption to complete */
	msleep(500);

	wls_status->verity_count = 0;
	do {
		rc = oplus_chg_wls_send_msg(wls_dev, WLS_CMD_GET_AES_DATA1, 0xff, 5);
		if (rc < 0) {
			if (rc != -EAGAIN)
				wls_status->verity_count++;
			msleep(500);
		}
	} while (rc < 0 && wls_status->verity_count < 5 && wls_status->rx_online);
	if (rc < 0 || !wls_status->rx_online) {
		pr_err("send 0x%02x error, rc=%d\n", WLS_CMD_GET_AES_DATA1, rc);
		wls_status->verity_pass = false;
		goto done;
	}
	msleep(500);
	wls_status->verity_count = 0;
	do {
		rc = oplus_chg_wls_send_msg(wls_dev, WLS_CMD_GET_AES_DATA2, 0xff, 5);
		if (rc < 0) {
			if (rc != -EAGAIN)
				wls_status->verity_count++;
			msleep(500);
		}
	} while (rc < 0 && wls_status->verity_count < 5 && wls_status->rx_online);
	if (rc < 0 || !wls_status->rx_online) {
		pr_err("send 0x%02x error, rc=%d\n", WLS_CMD_GET_AES_DATA2, rc);
		wls_status->verity_pass = false;
		goto done;
	}
	msleep(500);
	wls_status->verity_count = 0;
	do {
		rc = oplus_chg_wls_send_msg(wls_dev, WLS_CMD_GET_AES_DATA3, 0xff, 5);
		if (rc < 0) {
			if (rc != -EAGAIN)
				wls_status->verity_count++;
			msleep(500);
		}
	} while (rc < 0 && wls_status->verity_count < 5 && wls_status->rx_online);
	if (rc < 0 || !wls_status->rx_online) {
		pr_err("send 0x%02x error, rc=%d\n", WLS_CMD_GET_AES_DATA3, rc);
		wls_status->verity_pass = false;
		goto done;
	}
	msleep(500);
	wls_status->verity_count = 0;
	do {
		rc = oplus_chg_wls_send_msg(wls_dev, WLS_CMD_GET_AES_DATA4, 0xff, 5);
		if (rc < 0) {
			if (rc != -EAGAIN)
				wls_status->verity_count++;
			msleep(500);
		}
	} while (rc < 0 && wls_status->verity_count < 5 && wls_status->rx_online);
	if (rc < 0 || !wls_status->rx_online) {
		pr_err("send 0x%02x error, rc=%d\n", WLS_CMD_GET_AES_DATA4, rc);
		wls_status->verity_pass = false;
		goto done;
	}
	msleep(500);
	wls_status->verity_count = 0;
	do {
		rc = oplus_chg_wls_send_msg(wls_dev, WLS_CMD_GET_AES_DATA5, 0xff, 5);
		if (rc < 0) {
			if (rc != -EAGAIN)
				wls_status->verity_count++;
			msleep(500);
		}
	} while (rc < 0 && wls_status->verity_count < 5 && wls_status->rx_online);
	if (rc < 0 || !wls_status->rx_online) {
		pr_err("send 0x%02x error, rc=%d\n", WLS_CMD_GET_AES_DATA5, rc);
		wls_status->verity_pass = false;
		goto done;
	}
	msleep(500);
	wls_status->verity_count = 0;
	do {
		rc = oplus_chg_wls_send_msg(wls_dev, WLS_CMD_GET_AES_DATA6, 0xff, 5);
		if (rc < 0) {
			if (rc != -EAGAIN)
				wls_status->verity_count++;
			msleep(500);
		}
	} while (rc < 0 && wls_status->verity_count < 5 && wls_status->rx_online);
	if (rc < 0 || !wls_status->rx_online) {
		pr_err("send 0x%02x error, rc=%d\n", WLS_CMD_GET_AES_DATA6, rc);
		wls_status->verity_pass = false;
		goto done;
	}

	pr_info("aes encrypt_data: 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x \
		0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x",
		wls_status->aes_encrypt_data[0], wls_status->aes_encrypt_data[1],
		wls_status->aes_encrypt_data[2], wls_status->aes_encrypt_data[3],
		wls_status->aes_encrypt_data[4], wls_status->aes_encrypt_data[5],
		wls_status->aes_encrypt_data[6], wls_status->aes_encrypt_data[7],
		wls_status->aes_encrypt_data[8], wls_status->aes_encrypt_data[9],
		wls_status->aes_encrypt_data[10], wls_status->aes_encrypt_data[11],
		wls_status->aes_encrypt_data[12], wls_status->aes_encrypt_data[13],
		wls_status->aes_encrypt_data[14], wls_status->aes_encrypt_data[15]);

	if (memcmp(&wls_status->aes_encrypt_data,
		   &wls_status->aes_verfity_data.aes_encode_num, WLS_AUTH_AES_ENCODE_LEN)) {
		pr_err("verity faile\n");
		wls_status->verity_pass = false;
	} else {
		pr_err("verity pass\n");
		wls_status->verity_pass = true;
	}

done:
	wls_status->verity_count = 0;
	if (!wls_status->verity_pass && wls_status->rx_online &&
	    time_before(jiffies, wls_status->verity_wait_timeout))
		goto retry;
	wls_status->aes_verity_done = true;
	vote(wls_dev->fcc_votable, VERITY_VOTER, false, 0, false);
	if (!wls_status->verity_pass && wls_status->rx_online) {
		wls_status->online_keep = true;
		wls_status->verity_state_keep = true;
		vote(wls_dev->rx_disable_votable, VERITY_VOTER, true, 1, false);
		schedule_delayed_work(&wls_dev->rx_verity_restore_work, msecs_to_jiffies(500));
	}
}

static void oplus_chg_wls_verity_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_wls *wls_dev =
		container_of(dwork, struct oplus_chg_wls, wls_verity_work);
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;

	if (!wls_status->verify_by_aes)
		oplus_chg_wls_des_verity(wls_dev);
	else
		oplus_chg_wls_aes_verity(wls_dev);
}

static void oplus_chg_wls_exchange_batt_mesg(struct oplus_chg_wls *wls_dev)
{
	int soc = 0, temp = 0;
	u8 buf[3];
	struct oplus_chg_wls_status *wls_status;
	int rc;

	if (!wls_dev)
		return;

	wls_status = &wls_dev->wls_status;
	if (!wls_status->rx_online || !wls_status->aes_verity_done ||
	    wls_status->adapter_id != WLS_ADAPTER_THIRD_PARTY)
		return;

	rc = oplus_chg_wls_get_ui_soc(wls_dev, &soc);
	if (rc < 0) {
		pr_err("can't get ui soc, rc=%d\n", rc);
		return;
	}
	pr_err("get ui soc, rc=%d\n", rc);
	rc = oplus_chg_wls_get_batt_temp(wls_dev, &temp);
	if (rc < 0) {
		pr_err("can't get batt temp, rc=%d\n", rc);
		return;
	}
	pr_err("get batt temp, rc=%d\n", rc);

	buf[0] = (temp >> 8) & 0xff;
	buf[1] = temp & 0xff;
	buf[2] = soc & 0xff;
	pr_info("soc:%d, temp:%d\n", soc, temp);

	mutex_lock(&wls_dev->update_data_lock);
	oplus_chg_wls_send_data(wls_dev, WLS_CMD_SEND_BATT_TEMP_SOC, buf, 0);
	msleep(100);
	mutex_unlock(&wls_dev->update_data_lock);
}

static void oplus_chg_wls_reset_variables(struct oplus_chg_wls *wls_dev) {
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;

	wls_status->adapter_type = WLS_ADAPTER_TYPE_UNKNOWN;
	wls_status->adapter_id = 0;
	wls_status->adapter_power = 0;
	wls_status->charge_type = WLS_CHARGE_TYPE_DEFAULT;
	wls_status->current_rx_state = OPLUS_CHG_WLS_RX_STATE_DEFAULT;
	wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_DEFAULT;
	wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_DEFAULT;
	wls_status->trx_state = OPLUS_CHG_WLS_TRX_STATE_DEFAULT;
	wls_status->wls_type = OPLUS_CHG_WLS_UNKNOWN;

	// wls_status->rx_online = false;
	// wls_status->rx_present = false;
	wls_status->is_op_trx = false;
	wls_status->epp_working = false;
	wls_status->epp_5w = false;
	wls_status->quiet_mode = false;
	wls_status->switch_quiet_mode = false;
	wls_status->quiet_mode_init = false;
	wls_status->cep_timeout_adjusted = false;
	wls_status->upgrade_fw_pending = false;
	wls_status->fw_upgrading = false;
	wls_status->trx_present = false;
	wls_status->trx_online = false;
	wls_status->fastchg_started = false;
	wls_status->fastchg_disable = false;
	wls_status->fastchg_vol_set_ok = false;
	wls_status->fastchg_curr_set_ok = false;
	wls_status->fastchg_curr_need_dec = false;
	wls_status->ffc_check = false;
	wls_status->wait_cep_stable = false;
	wls_status->fastchg_restart = false;
	wls_status->rx_adc_test_enable = false;
	wls_status->rx_adc_test_pass = false;
	wls_status->boot_online_keep = false;
	wls_status->chg_done = false;
	wls_status->chg_done_quiet_mode = false;

	wls_status->state_sub_step = 0;
	wls_status->iout_ma = 0;
	wls_status->iout_ma_conunt = 0;
	wls_status->vout_mv = 0;
	wls_status->vrect_mv = 0;
	wls_status->trx_curr_ma = 0;
	wls_status->trx_vol_mv = 0;
	// wls_status->fastchg_target_curr_ma = 0;
	wls_status->fastchg_target_vol_mv = 0;
	wls_status->fastchg_ibat_max_ma = 0;
	wls_status->tx_pwr_mw = 0;
	wls_status->rx_pwr_mw = 0;
	wls_status->ploos_mw = 0;
	wls_status->epp_plus_skin_step = 0;
	wls_status->epp_skin_step = 0;
	wls_status->bpp_skin_step = 0;
	wls_status->epp_plus_led_on_skin_step = 0;
	wls_status->epp_led_on_skin_step = 0;
	wls_status->pwr_max_mw = 0;
	wls_status->quiet_mode_need = 0;
	wls_status->adapter_info_cmd_count = 0;
	wls_status->fastchg_retry_count = 0;
#ifndef CONFIG_OPLUS_CHG_OOS
	wls_status->cool_down = 0;
#endif

	wls_status->cep_ok_wait_timeout = jiffies;
	wls_status->fastchg_retry_timer = jiffies;

	wls_dev->batt_charge_enable = true;

	/*wls encrypt*/
	if (!wls_status->verity_state_keep) {
		/*
		 * After the AP is actively disconnected, the verification
		 * status flag does not need to change.
		 */
		wls_status->verity_pass = true;
	}
	wls_status->verity_started = false;
	wls_status->verity_data_ok = false;
	wls_status->verity_count = 0;
	wls_status->aes_verity_done = false;
	wls_status->verify_by_aes = false;
	wls_status->tx_extern_cmd_done = false;
	wls_status->tx_product_id_done = false;
	memset(&wls_status->encrypt_data, 0, ARRAY_SIZE(wls_status->encrypt_data));
	memset(&wls_status->verfity_data, 0, sizeof(struct wls_auth_result));

	vote(wls_dev->nor_icl_votable, USER_VOTER, true, 0, false);
	vote(wls_dev->nor_fcc_votable, USER_VOTER, true, 0, false);
	vote(wls_dev->nor_icl_votable, STOP_VOTER, false, 0, false);
	vote(wls_dev->nor_icl_votable, MAX_VOTER, false, 0, false);
	vote(wls_dev->nor_icl_votable, SKIN_VOTER, false, 0, false);
	vote(wls_dev->nor_icl_votable, RX_MAX_VOTER, false, 0, false);
	vote(wls_dev->nor_icl_votable, UOVP_VOTER, false, 0, false);
	vote(wls_dev->nor_icl_votable, CHG_DONE_VOTER, false, 0, false);
	vote(wls_dev->nor_fcc_votable, MAX_VOTER, false, 0, false);
	vote(wls_dev->nor_fv_votable, USER_VOTER, false, 0, false);
	vote(wls_dev->fcc_votable, MAX_VOTER, false, 0, false);
	vote(wls_dev->fcc_votable, DEF_VOTER, false, 0, false);
	vote(wls_dev->fcc_votable, STEP_VOTER, false, 0, false);
	vote(wls_dev->fcc_votable, EXIT_VOTER, false, 0, false);
	vote(wls_dev->fcc_votable, FCC_VOTER, false, 0, false);
	vote(wls_dev->fcc_votable, JEITA_VOTER, false, 0, false);
	vote(wls_dev->fcc_votable, CEP_VOTER, false, 0, false);
	vote(wls_dev->fcc_votable, SKIN_VOTER, false, 0, false);
	vote(wls_dev->fcc_votable, DEBUG_VOTER, false, 0, false);
	vote(wls_dev->fcc_votable, BASE_MAX_VOTER, false, 0, false);
	vote(wls_dev->fcc_votable, RX_MAX_VOTER, false, 0, false);
	vote(wls_dev->fcc_votable, COOL_DOWN_VOTER, false, 0, false);
	vote(wls_dev->fastchg_disable_votable, QUIET_VOTER, false, 0, false);
	vote(wls_dev->fastchg_disable_votable, CEP_VOTER, false, 0, false);
	vote(wls_dev->fastchg_disable_votable, FCC_VOTER, false, 0, false);
	vote(wls_dev->fastchg_disable_votable, SKIN_VOTER, false, 0, false);
	vote(wls_dev->fastchg_disable_votable, BATT_VOL_VOTER, false, 0, false);
	vote(wls_dev->fastchg_disable_votable, BATT_CURR_VOTER, false, 0, false);
	vote(wls_dev->fastchg_disable_votable, IOUT_CURR_VOTER, false, 0, false);
	vote(wls_dev->fastchg_disable_votable, STARTUP_CEP_VOTER, false, 0, false);
	vote(wls_dev->fastchg_disable_votable, HW_ERR_VOTER, false, 0, false);
	vote(wls_dev->fastchg_disable_votable, CURR_ERR_VOTER, false, 0, false);
	vote(wls_dev->fastchg_disable_votable, COOL_DOWN_VOTER, false, 0, false);
	vote(wls_dev->nor_out_disable_votable, FFC_VOTER, false, 0, false);
	vote(wls_dev->nor_out_disable_votable, USER_VOTER, false, 0, false);
	vote(wls_dev->nor_out_disable_votable, STOP_VOTER, false, 0, false);
	vote(wls_dev->nor_out_disable_votable, UOVP_VOTER, false, 0, false);
	rerun_election(wls_dev->nor_icl_votable, false);
	rerun_election(wls_dev->nor_fcc_votable, false);
	rerun_election(wls_dev->nor_fv_votable, false);
	rerun_election(wls_dev->fcc_votable, false);
	rerun_election(wls_dev->fastchg_disable_votable, false);
	rerun_election(wls_dev->nor_out_disable_votable, false);

	if (is_comm_ocm_available(wls_dev))
		oplus_chg_comm_status_init(wls_dev->comm_ocm);
}

static int oplus_chg_wls_set_trx_enable(struct oplus_chg_wls *wls_dev, bool en)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	int rc = 0;

	if (en && wls_status->fw_upgrading) {
		pr_err("FW is upgrading, reverse charging cannot be used\n");
		return -EFAULT;
	}
	if (en && wls_dev->usb_present) {
		pr_err("during USB charging, reverse charging cannot be used");
		return -EFAULT;
	}
	if (en && wls_status->rx_present) {
		pr_err("wireless charging, reverse charging cannot be used");
		return -EFAULT;
	}

	mutex_lock(&wls_dev->connect_lock);
	if (en) {
		if (wls_status->wls_type == OPLUS_CHG_WLS_TRX)
			goto out;
		cancel_delayed_work_sync(&wls_dev->wls_connect_work);
		//vote(wls_dev->wrx_en_votable, TRX_EN_VOTER, true, 1, false);
		//msleep(20);
		rc = oplus_chg_wls_nor_set_boost_vol(wls_dev->wls_nor, WLS_TRX_MODE_VOL_MV);
		if (rc < 0) {
			pr_err("can't set trx boost vol, rc=%d\n", rc);
			goto out;
		}
		oplus_chg_wls_nor_set_boost_en(wls_dev->wls_nor, true);
		if (rc < 0) {
			pr_err("can't enable trx boost, rc=%d\n", rc);
			goto out;
		}
		msleep(500);
		oplus_chg_wls_reset_variables(wls_dev);
		wls_status->wls_type = OPLUS_CHG_WLS_TRX;

		if (!wls_dev->trx_wake_lock_on) {
			pr_info("acquire trx_wake_lock\n");
			__pm_stay_awake(wls_dev->trx_wake_lock);
			wls_dev->trx_wake_lock_on = true;
		} else {
			pr_err("trx_wake_lock is already stay awake\n");
		}

		cancel_delayed_work_sync(&wls_dev->wls_trx_sm_work);
		queue_delayed_work(wls_dev->wls_wq, &wls_dev->wls_trx_sm_work, 0);
	} else {
		if (wls_status->wls_type != OPLUS_CHG_WLS_TRX)
			goto out;
		cancel_delayed_work_sync(&wls_dev->wls_trx_sm_work);
		oplus_chg_wls_nor_set_boost_en(wls_dev->wls_nor, false);
		msleep(20);
		//vote(wls_dev->wrx_en_votable, TRX_EN_VOTER, false, 0, false);
		oplus_chg_wls_reset_variables(wls_dev);
		if (is_batt_ocm_available(wls_dev))
			oplus_chg_mod_changed(wls_dev->batt_ocm);
		if (wls_dev->trx_wake_lock_on) {
			pr_info("release trx_wake_lock\n");
			__pm_relax(wls_dev->trx_wake_lock);
			wls_dev->trx_wake_lock_on = false;
		} else {
			pr_err("trx_wake_lock is already relax\n");
		}
	}

	if (wls_dev->wls_ocm)
		oplus_chg_mod_changed(wls_dev->wls_ocm);

out:
	mutex_unlock(&wls_dev->connect_lock);
	return rc;
}

#ifdef OPLUS_CHG_DEBUG
static int oplus_chg_wls_path_ctrl(struct oplus_chg_wls *wls_dev,
				enum oplus_chg_wls_path_ctrl_type type)
{
	if (wls_dev->force_type != OPLUS_CHG_WLS_FORCE_TYPE_FAST)
		return -EINVAL;
	if (!wls_dev->support_fastchg)
		return -EINVAL;

	switch(type) {
	case OPLUS_CHG_WLS_PATH_CTRL_TYPE_DISABLE_ALL:
		(void)oplus_chg_wls_fast_set_enable(wls_dev->wls_fast, false);
		vote(wls_dev->nor_icl_votable, USER_VOTER, true, 0, false);
		break;
	case OPLUS_CHG_WLS_PATH_CTRL_TYPE_ENABLE_NOR:
		vote(wls_dev->nor_icl_votable, USER_VOTER, true, 300, false);
		break;
	case OPLUS_CHG_WLS_PATH_CTRL_TYPE_DISABLE_NOR:
		vote(wls_dev->nor_icl_votable, USER_VOTER, true, 0, false);
		break;
	case OPLUS_CHG_WLS_PATH_CTRL_TYPE_ENABLE_FAST:
		(void)oplus_chg_wls_fast_set_enable(wls_dev->wls_fast, true);
		(void)oplus_chg_wls_fast_start(wls_dev->wls_fast);
		break;
	case OPLUS_CHG_WLS_PATH_CTRL_TYPE_DISABLE_FAST:
		(void)oplus_chg_wls_fast_set_enable(wls_dev->wls_fast, false);
		break;
	default:
		pr_err("unknown path ctrl type(=%d), \n", type);
		return -EINVAL;
	}

	return 0;
}
#endif /* OPLUS_CHG_DEBUG */

#ifndef CONFIG_OPLUS_CHG_OOS
static int oplus_chg_wls_get_cool_down_iout(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_dynamic_config *dynamic_cfg =&wls_dev->dynamic_config;
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	int cool_down = wls_status->cool_down;
	int icl_ma = 0;
	int ibat_ma = 0;

	if (!wls_status->led_on)
		return 0;

	switch (wls_status->wls_type) {
	case OPLUS_CHG_WLS_BPP:
		if (cool_down >= ARRAY_SIZE(cool_down_bpp) && ARRAY_SIZE(cool_down_bpp) > 1)
			cool_down = ARRAY_SIZE(cool_down_bpp) - 1;
		ibat_ma = cool_down_bpp[cool_down];
		icl_ma = ibat_ma * 10000 / dynamic_cfg->bpp_vol_mv;
		break;
	case OPLUS_CHG_WLS_EPP:
		if (cool_down >= ARRAY_SIZE(cool_down_epp) && ARRAY_SIZE(cool_down_epp) > 1)
			cool_down = ARRAY_SIZE(cool_down_epp) - 1;
		ibat_ma = cool_down_epp[cool_down];
		icl_ma = ibat_ma * 10000 / dynamic_cfg->epp_vol_mv;
		break;
	case OPLUS_CHG_WLS_EPP_PLUS:
		if (cool_down >= ARRAY_SIZE(cool_down_epp_plus) && ARRAY_SIZE(cool_down_epp_plus) > 1)
			cool_down = ARRAY_SIZE(cool_down_epp_plus) - 1;
		ibat_ma = cool_down_epp_plus[cool_down];
		icl_ma = ibat_ma * 10000 / dynamic_cfg->epp_plus_vol_mv;
		break;
	case OPLUS_CHG_WLS_WARP:
		if (cool_down >= ARRAY_SIZE(cool_down_warp) && ARRAY_SIZE(cool_down_warp) > 1)
			cool_down = ARRAY_SIZE(cool_down_warp) - 1;
		ibat_ma = cool_down_warp[cool_down];
		icl_ma = ibat_ma * 10000 / dynamic_cfg->warp_vol_mv;
		break;
	case OPLUS_CHG_WLS_SWARP:
	case OPLUS_CHG_WLS_PD_65W:
		if (cool_down >= ARRAY_SIZE(cool_down_swarp) && ARRAY_SIZE(cool_down_swarp) > 1)
			cool_down = ARRAY_SIZE(cool_down_swarp) - 1;
		if (cool_down > COOL_DOWN_12V_THR)
			cool_down = COOL_DOWN_12V_THR;
		ibat_ma = cool_down_swarp[cool_down];
		icl_ma = ibat_ma * 10000 / dynamic_cfg->swarp_vol_mv;
		break;
	default:
		pr_err("Unsupported charging mode(=%d)\n", wls_status->wls_type);
		return 0;
	}

	return icl_ma;
}
#endif

static void oplus_chg_wls_config(struct oplus_chg_wls *wls_dev)
{
	enum oplus_chg_temp_region temp_region;
	struct oplus_chg_wls_dynamic_config *dynamic_cfg =&wls_dev->dynamic_config;
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	enum oplus_chg_ffc_status ffc_status;
	int icl_max_ma;
	int fcc_max_ma;
	int icl_index;
	static bool pre_temp_abnormal;
#ifndef CONFIG_OPLUS_CHG_OOS
	int icl_cool_down_ma;
#endif

	if (!is_comm_ocm_available(wls_dev)) {
		pr_err("comm mod not fount\n");
		return;
	}

	ffc_status = oplus_chg_comm_get_ffc_status(wls_dev->comm_ocm);
	if (ffc_status != FFC_DEFAULT) {
		pr_err("ffc charging, exit\n");
		return;
	}

	if (!wls_status->rx_online && !wls_status->online_keep) {
		pr_err("rx is offline\n");
		return;
	}

	if (oplus_chg_comm_batt_vol_over_cl_thr(wls_dev->comm_ocm))
		icl_index = OPLUS_WLS_CHG_BATT_CL_HIGH;
	else
		icl_index = OPLUS_WLS_CHG_BATT_CL_LOW;

	temp_region = oplus_chg_comm_get_temp_region(wls_dev->comm_ocm);
	switch (temp_region) {
	case BATT_TEMP_COLD:
	case BATT_TEMP_HOT:
		if (!pre_temp_abnormal) {
			pre_temp_abnormal = true;
			wls_status->online_keep = true;
			vote(wls_dev->rx_disable_votable, JEITA_VOTER, true, 1, false);
			vote(wls_dev->nor_icl_votable, JEITA_VOTER, true, 0, true);
			schedule_delayed_work(&wls_dev->rx_restore_work, msecs_to_jiffies(500));
		} else {
			vote(wls_dev->rx_disable_votable, JEITA_VOTER, false, 0, false);
			vote(wls_dev->nor_icl_votable, JEITA_VOTER, true, 0, true);
		}
		break;
	default:
		pre_temp_abnormal = false;
		vote(wls_dev->rx_disable_votable, JEITA_VOTER, false, 0, false);
		vote(wls_dev->nor_icl_votable, JEITA_VOTER, false, 0, true);
		break;
	}
	switch (wls_status->wls_type) {
	case OPLUS_CHG_WLS_BPP:
		icl_max_ma = dynamic_cfg->iclmax_ma[icl_index][OPLUS_WLS_CHG_MODE_BPP][temp_region];
		fcc_max_ma = 1000;
		break;
	case OPLUS_CHG_WLS_EPP:
		icl_max_ma = dynamic_cfg->iclmax_ma[icl_index][OPLUS_WLS_CHG_MODE_EPP][temp_region];
		fcc_max_ma = 1500;
		break;
	case OPLUS_CHG_WLS_EPP_PLUS:
		icl_max_ma = dynamic_cfg->iclmax_ma[icl_index][OPLUS_WLS_CHG_MODE_EPP_PLUS][temp_region];
		fcc_max_ma = 2000;
		break;
	case OPLUS_CHG_WLS_WARP:
		icl_max_ma = dynamic_cfg->iclmax_ma[icl_index][OPLUS_WLS_CHG_MODE_EPP_PLUS][temp_region];
		fcc_max_ma = 2000;
		break;
	case OPLUS_CHG_WLS_SWARP:
	case OPLUS_CHG_WLS_PD_65W:
		icl_max_ma = dynamic_cfg->iclmax_ma[icl_index][OPLUS_WLS_CHG_MODE_FAST][temp_region];
		fcc_max_ma = 2000;
		break;
	default:
		pr_err("Unsupported charging mode(=%d)\n", wls_status->wls_type);
		return;
	}
#ifndef CONFIG_OPLUS_CHG_OOS
	icl_cool_down_ma = oplus_chg_wls_get_cool_down_iout(wls_dev);
	if (icl_cool_down_ma > 0 && icl_cool_down_ma < icl_max_ma && !wls_dev->factory_mode)
		icl_max_ma = icl_cool_down_ma;
	pr_info("icl_cool_down_ma:%d\n", icl_cool_down_ma);
#endif
	vote(wls_dev->nor_icl_votable, MAX_VOTER, true, icl_max_ma, true);
	vote(wls_dev->nor_fcc_votable, MAX_VOTER, true, fcc_max_ma, false);
	pr_info("chg_type=%d, temp_region=%d, fcc=%d, icl=%d\n",
		wls_status->wls_type, temp_region, fcc_max_ma, icl_max_ma);
}

#ifdef OPLUS_CHG_DEBUG

#define UPGRADE_START 0
#define UPGRADE_FW    1
#define UPGRADE_END   2
struct oplus_chg_fw_head {
	u8 magic[4];
	int size;
};
static ssize_t oplus_chg_wls_upgrade_fw_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	int rc;

	rc = sprintf(buf, "wireless\n");
	return rc;
}

static ssize_t oplus_chg_wls_upgrade_fw_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct oplus_chg_mod *ocm = dev_get_drvdata(dev);
	struct oplus_chg_wls *wls_dev = oplus_chg_mod_get_drvdata(ocm);
	u8 temp_buf[sizeof(struct oplus_chg_fw_head)];
	static u8 *fw_buf;
	static int upgrade_step = UPGRADE_START;
	static int fw_index;
	static int fw_size;
	struct oplus_chg_fw_head *fw_head;

start:
	switch (upgrade_step) {
	case UPGRADE_START:
		if (count < sizeof(struct oplus_chg_fw_head)) {
			pr_err("<FW UPDATE>image format error\n");
			return -EINVAL;
		}
		memset(temp_buf, 0, sizeof(struct oplus_chg_fw_head));
		memcpy(temp_buf, buf, sizeof(struct oplus_chg_fw_head));
		fw_head = (struct oplus_chg_fw_head *)temp_buf;
		if (fw_head->magic[0] == 0x02 && fw_head->magic[1] == 0x00 &&
		    fw_head->magic[2] == 0x03 && fw_head->magic[3] == 0x00) {
			fw_size = fw_head->size;
			fw_buf = kzalloc(fw_size, GFP_KERNEL);
			if (fw_buf == NULL) {
				pr_err("<FW UPDATE>alloc fw_buf err\n");
				return -ENOMEM;
			}
			wls_dev->fw_buf = fw_buf;
			wls_dev->fw_size = fw_size;
			pr_err("<FW UPDATE>image header verification succeeded, fw_size=%d\n", fw_size);
			memcpy(fw_buf, buf + sizeof(struct oplus_chg_fw_head), count - sizeof(struct oplus_chg_fw_head));
			fw_index = count - sizeof(struct oplus_chg_fw_head);
			pr_info("<FW UPDATE>Receiving image, fw_size=%d, fw_index=%d\n", fw_size, fw_index);
			if (fw_index >= fw_size) {
				upgrade_step = UPGRADE_END;
				goto start;
			} else {
				upgrade_step = UPGRADE_FW;
			}
		} else {
			pr_err("<FW UPDATE>image format error\n");
			return -EINVAL;
		}
		break;
	case UPGRADE_FW:
		memcpy(fw_buf + fw_index, buf, count);
		fw_index += count;
		pr_info("<FW UPDATE>Receiving image, fw_size=%d, fw_index=%d\n", fw_size, fw_index);
		if (fw_index >= fw_size) {
			upgrade_step = UPGRADE_END;
			goto start;
		}
		break;
	case UPGRADE_END:
		wls_dev->fw_upgrade_by_buf = true;
		schedule_delayed_work(&wls_dev->wls_upgrade_fw_work, 0);
		fw_buf = NULL;
		upgrade_step = UPGRADE_START;
		break;
	default:
		upgrade_step = UPGRADE_START;
		pr_err("<FW UPDATE>status error\n");
		if (fw_buf != NULL) {
			kfree(fw_buf);
			fw_buf = NULL;
			wls_dev->fw_buf = NULL;
			wls_dev->fw_size = 0;
			wls_dev->fw_upgrade_by_buf = false;
		}
		break;
	}

	return count;
}

static ssize_t oplus_chg_wls_path_curr_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct oplus_chg_mod *ocm = dev_get_drvdata(dev);
	struct oplus_chg_wls *wls_dev = oplus_chg_mod_get_drvdata(ocm);
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	int nor_curr_ma, fast_curr_ma;
	ssize_t rc;

	rc = sscanf(buf, "%d,%d", &nor_curr_ma, &fast_curr_ma);
	if (rc < 0) {
		pr_err("can't read input string, rc=%d\n", rc);
		return rc;
	}
	nor_curr_ma = nor_curr_ma /1000;
	fast_curr_ma = fast_curr_ma / 1000;

	switch (wls_dev->force_type) {
	case OPLUS_CHG_WLS_FORCE_TYPE_NOEN:
	case OPLUS_CHG_WLS_FORCE_TYPE_AUTO:
		return -EINVAL;
	case OPLUS_CHG_WLS_FORCE_TYPE_BPP:
	case OPLUS_CHG_WLS_FORCE_TYPE_EPP:
	case OPLUS_CHG_WLS_FORCE_TYPE_EPP_PLUS:
		if (nor_curr_ma >= 0) {
			vote(wls_dev->nor_icl_votable, USER_VOTER, true, nor_curr_ma, false);
		} else {
			pr_err("Parameter error\n");
			return -EINVAL;
		}
		break;
	case OPLUS_CHG_WLS_FORCE_TYPE_FAST:
		if (fast_curr_ma > 0) {
			wls_status->fastchg_started = true;
			vote(wls_dev->fcc_votable, DEBUG_VOTER, true, fast_curr_ma, false);
		} else {
			wls_status->fastchg_started = false;
			vote(wls_dev->fcc_votable, DEBUG_VOTER, false, 0, false);
		}
		if (nor_curr_ma >= 0)
			vote(wls_dev->nor_icl_votable, USER_VOTER, true, nor_curr_ma, false);
	}

	return count;
}
#endif /* OPLUS_CHG_DEBUG */

static ssize_t oplus_chg_wls_ftm_test_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct oplus_chg_mod *ocm = dev_get_drvdata(dev);
	struct oplus_chg_wls *wls_dev = oplus_chg_mod_get_drvdata(ocm);
	int rc;
	ssize_t index = 0;

	if (oplus_chg_wls_is_usb_present(wls_dev)) {
		pr_info("usb online, can't run rx smt test\n");
		index += sprintf(buf + index, "%d,%s\n", WLS_PATH_RX, "usb_online");
		goto skip_rx_check;
	}

	if (wls_dev->wls_status.rx_present) {
		pr_info("wls online, can't run rx smt test\n");
		index += sprintf(buf + index, "%d,%s\n", WLS_PATH_RX, "wls_online");
		goto skip_rx_check;
	}

	vote(wls_dev->wrx_en_votable, FTM_TEST_VOTER, true, 1, false);
	rc = oplus_chg_wls_rx_smt_test(wls_dev->wls_rx);
	if (rc != 0)
		index += sprintf(buf + index, "%d,%d\n", WLS_PATH_RX, rc);
	vote(wls_dev->wrx_en_votable, FTM_TEST_VOTER, true, 0, false);

skip_rx_check:
	if (wls_dev->support_fastchg) {
		rc = oplus_chg_wls_fast_smt_test(wls_dev->wls_fast);
		if (rc != 0)
			index += sprintf(buf + index, "%d,%d\n", WLS_PATH_FAST, rc);
	}

	if (index == 0)
		index += sprintf(buf + index, "OK\r\n");
	else
		index += sprintf(buf + index, "ERROR\r\n");

	return index;
}

static int oplus_chg_wls_get_max_wireless_power(struct oplus_chg_wls *wls_dev)
{
	int max_wls_power = 0;
	int max_adapter_wls_power = 0;
	int max_r_wls_power = 0;
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;

	max_adapter_wls_power = oplus_chg_wls_get_base_power_max(wls_status->adapter_id);
	max_r_wls_power = oplus_chg_wls_get_r_power(wls_dev, wls_status->adapter_power);
	max_r_wls_power = max_adapter_wls_power > wls_status->pwr_max_mw ?
						wls_status->pwr_max_mw : max_adapter_wls_power;
	max_wls_power = max_r_wls_power > wls_dev->wls_power_mw ?
						wls_dev->wls_power_mw : max_r_wls_power;
	pr_err("max_wls_power=%d,max_adapter_wls_power=%d,max_r_wls_power=%d\n",
						max_wls_power, max_adapter_wls_power, max_r_wls_power);
	return max_wls_power;
}

static enum oplus_chg_mod_property oplus_chg_wls_props[] = {
	OPLUS_CHG_PROP_ONLINE,
	OPLUS_CHG_PROP_PRESENT,
	OPLUS_CHG_PROP_VOLTAGE_NOW,
	OPLUS_CHG_PROP_VOLTAGE_MAX,
	OPLUS_CHG_PROP_VOLTAGE_MIN,
	OPLUS_CHG_PROP_CURRENT_NOW,
	OPLUS_CHG_PROP_CURRENT_MAX,
	OPLUS_CHG_PROP_INPUT_CURRENT_NOW,
	OPLUS_CHG_PROP_WLS_TYPE,
	OPLUS_CHG_PROP_FASTCHG_STATUS,
	OPLUS_CHG_PROP_ADAPTER_SID,
	OPLUS_CHG_PROP_ADAPTER_TYPE,
	OPLUS_CHG_PROP_CHG_ENABLE,
	OPLUS_CHG_PROP_MAX_W_POWER,
	OPLUS_CHG_PROP_TRX_VOLTAGE_NOW,
	OPLUS_CHG_PROP_TRX_CURRENT_NOW,
	OPLUS_CHG_PROP_TRX_STATUS,
	OPLUS_CHG_PROP_TRX_ONLINE,
	OPLUS_CHG_PROP_DEVIATED,
#ifdef OPLUS_CHG_DEBUG
	OPLUS_CHG_PROP_FORCE_TYPE,
	OPLUS_CHG_PROP_PATH_CTRL,
#endif
	OPLUS_CHG_PROP_STATUS_DELAY,
	OPLUS_CHG_PROP_QUIET_MODE,
	OPLUS_CHG_PROP_VRECT_NOW,
	OPLUS_CHG_PROP_TRX_POWER_EN,
	OPLUS_CHG_PROP_TRX_POWER_VOL,
	OPLUS_CHG_PROP_TRX_POWER_CURR_LIMIT,
	OPLUS_CHG_PROP_FACTORY_MODE,
	OPLUS_CHG_PROP_TX_POWER,
	OPLUS_CHG_PROP_RX_POWER,
	OPLUS_CHG_PROP_FOD_CAL,
	OPLUS_CHG_PROP_BATT_CHG_ENABLE,
	OPLUS_CHG_PROP_ONLINE_KEEP,
	OPLUS_CHG_PROP_STATUS_KEEP,
#ifndef CONFIG_OPLUS_CHG_OOS
	OPLUS_CHG_PROP_TX_VOLTAGE_NOW,
	OPLUS_CHG_PROP_TX_CURRENT_NOW,
	OPLUS_CHG_PROP_CP_VOLTAGE_NOW,
	OPLUS_CHG_PROP_CP_CURRENT_NOW,
	OPLUS_CHG_PROP_WIRELESS_MODE,
	OPLUS_CHG_PROP_WIRELESS_TYPE,
	OPLUS_CHG_PROP_CEP_INFO,
	OPLUS_CHG_PROP_REAL_TYPE,
	OPLUS_CHG_PROP_COOL_DOWN,
#endif /* CONFIG_OPLUS_CHG_OOS */
};

static enum oplus_chg_mod_property oplus_chg_wls_uevent_props[] = {
	OPLUS_CHG_PROP_ONLINE,
	OPLUS_CHG_PROP_PRESENT,
	OPLUS_CHG_PROP_VOLTAGE_NOW,
	OPLUS_CHG_PROP_CURRENT_NOW,
	OPLUS_CHG_PROP_WLS_TYPE,
	OPLUS_CHG_PROP_FASTCHG_STATUS,
	OPLUS_CHG_PROP_ADAPTER_SID,
	OPLUS_CHG_PROP_TRX_VOLTAGE_NOW,
	OPLUS_CHG_PROP_TRX_CURRENT_NOW,
	OPLUS_CHG_PROP_TRX_STATUS,
	OPLUS_CHG_PROP_TRX_ONLINE,
	OPLUS_CHG_PROP_QUIET_MODE,
};

static struct oplus_chg_exten_prop oplus_chg_wls_exten_props[] = {
#ifdef OPLUS_CHG_DEBUG
	OPLUS_CHG_EXTEN_RWATTR(OPLUS_CHG_EXTERN_PROP_UPGRADE_FW, oplus_chg_wls_upgrade_fw),
	OPLUS_CHG_EXTEN_WOATTR(OPLUS_CHG_EXTERN_PROP_PATH_CURRENT, oplus_chg_wls_path_curr),
#endif
	OPLUS_CHG_EXTEN_ROATTR(OPLUS_CHG_PROP_FTM_TEST, oplus_chg_wls_ftm_test),
};

static int oplus_chg_wls_get_prop(struct oplus_chg_mod *ocm,
			enum oplus_chg_mod_property prop,
			union oplus_chg_mod_propval *pval)
{
	struct oplus_chg_wls *wls_dev = oplus_chg_mod_get_drvdata(ocm);
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	int rc = 0;

	switch (prop) {
	case OPLUS_CHG_PROP_ONLINE:
		pval->intval = wls_status->rx_online;
		break;
	case OPLUS_CHG_PROP_PRESENT:
		pval->intval = wls_status->rx_present;
		break;
	case OPLUS_CHG_PROP_VOLTAGE_NOW:
#ifdef CONFIG_OPLUS_CHG_OOS
		pval->intval = wls_status->vout_mv * 1000;
#else
		pval->intval = wls_status->vout_mv;
		if (wls_status->trx_present) {
			rc = oplus_chg_wls_rx_get_trx_vol(wls_dev->wls_rx, &wls_status->trx_vol_mv);
			if (rc < 0)
				pr_err("can't get trx vol, rc=%d\n", rc);
			else
				pval->intval = wls_status->trx_vol_mv;
		}
#endif
		break;
	case OPLUS_CHG_PROP_VOLTAGE_MAX:
		pval->intval = wls_status->vout_mv * 1000;
		break;
	case OPLUS_CHG_PROP_VOLTAGE_MIN:
		pval->intval = wls_status->vout_mv * 1000;
		break;
	case OPLUS_CHG_PROP_CURRENT_NOW:
#ifdef CONFIG_OPLUS_CHG_OOS
		pval->intval = wls_status->iout_ma * 1000;
#else
		pval->intval = wls_status->iout_ma;
		if (wls_status->trx_present) {
			rc = oplus_chg_wls_rx_get_trx_curr(wls_dev->wls_rx, &wls_status->trx_curr_ma);
			if (rc < 0)
				pr_err("can't get trx curr, rc=%d\n", rc);
			else
				pval->intval = wls_status->trx_curr_ma;
		}
#endif
		break;
	case OPLUS_CHG_PROP_CURRENT_MAX:
		pval->intval = wls_status->iout_ma * 1000;
		break;
	case OPLUS_CHG_PROP_INPUT_CURRENT_NOW:
		rc = oplus_chg_wls_nor_get_input_curr(wls_dev->wls_nor,
						      &pval->intval);
		break;
	case OPLUS_CHG_PROP_WLS_TYPE:
		pval->intval = wls_status->wls_type;
		break;
	case OPLUS_CHG_PROP_FASTCHG_STATUS:
		pval->intval = wls_status->fastchg_started;
		break;
	case OPLUS_CHG_PROP_ADAPTER_SID:
		pval->intval = 0;
		break;
	case OPLUS_CHG_PROP_ADAPTER_TYPE:
		pval->intval = wls_status->adapter_type;
		break;
	case OPLUS_CHG_PROP_CHG_ENABLE:
		pval->intval = wls_dev->charge_enable;
		break;
	case OPLUS_CHG_PROP_MAX_W_POWER:
		pval->intval = oplus_chg_wls_get_max_wireless_power(wls_dev);
		break;
	case OPLUS_CHG_PROP_TRX_VOLTAGE_NOW:
		if (wls_status->trx_present) {
			rc = oplus_chg_wls_rx_get_trx_vol(wls_dev->wls_rx, &wls_status->trx_vol_mv);
			if (rc < 0)
				pr_err("can't get trx vol, rc=%d\n", rc);
			else
				pval->intval = wls_status->trx_vol_mv * 1000;
		} else {
			pval->intval = 0;
		}
		break;
	case OPLUS_CHG_PROP_TRX_CURRENT_NOW:
		if (wls_status->trx_present) {
			rc = oplus_chg_wls_rx_get_trx_curr(wls_dev->wls_rx, &wls_status->trx_curr_ma);
			if (rc < 0)
				pr_err("can't get trx curr, rc=%d\n", rc);
			else
				pval->intval = wls_status->trx_curr_ma * 1000;
		} else {
			pval->intval = 0;
		}
		break;
	case OPLUS_CHG_PROP_TRX_STATUS:
		if (wls_status->trx_online)
			pval->intval = OPLUS_CHG_WLS_TRX_STATUS_CHARGING;
		else if (wls_status->trx_present)
			pval->intval = OPLUS_CHG_WLS_TRX_STATUS_ENABLE;
		else
			pval->intval = OPLUS_CHG_WLS_TRX_STATUS_DISENABLE;
		break;
	case OPLUS_CHG_PROP_TRX_ONLINE:
		pval->intval = wls_status->trx_present;
		break;
	case OPLUS_CHG_PROP_DEVIATED:
		pval->intval = 0;
		break;
#ifdef OPLUS_CHG_DEBUG
	case OPLUS_CHG_PROP_FORCE_TYPE:
		pval->intval = wls_dev->force_type;
		break;
	case OPLUS_CHG_PROP_PATH_CTRL:
		rc = 0;
		break;
#endif
	case OPLUS_CHG_PROP_STATUS_DELAY:
		pval->intval = 0;
		break;
	case OPLUS_CHG_PROP_QUIET_MODE:
		pval->intval = wls_status->switch_quiet_mode;
		break;
	case OPLUS_CHG_PROP_VRECT_NOW:
		pval->intval = wls_status->vrect_mv *1000;
		break;
	case OPLUS_CHG_PROP_TRX_POWER_EN:
	case OPLUS_CHG_PROP_TRX_POWER_VOL:
	case OPLUS_CHG_PROP_TRX_POWER_CURR_LIMIT:
		pval->intval = 0;
		break;
	case OPLUS_CHG_PROP_FACTORY_MODE:
		pval->intval = wls_dev->factory_mode;
		break;
	case OPLUS_CHG_PROP_TX_POWER:
		pval->intval = wls_status->tx_pwr_mw;
		break;
	case OPLUS_CHG_PROP_RX_POWER:
		pval->intval = wls_status->rx_pwr_mw;
		break;
	case OPLUS_CHG_PROP_FOD_CAL:
		pval->intval = wls_dev->fod_is_cal;
		break;
	case OPLUS_CHG_PROP_ONLINE_KEEP:
		pval->intval = wls_status->online_keep || wls_status->boot_online_keep;
		break;
	case OPLUS_CHG_PROP_BATT_CHG_ENABLE:
		pval->intval = wls_dev->batt_charge_enable;
		break;
	case OPLUS_CHG_PROP_STATUS_KEEP:
		pval->intval = wls_dev->status_keep;
		break;
#ifndef CONFIG_OPLUS_CHG_OOS
	case OPLUS_CHG_PROP_TX_VOLTAGE_NOW:
		if (wls_status->trx_present) {
			rc = oplus_chg_wls_rx_get_trx_vol(wls_dev->wls_rx, &wls_status->trx_vol_mv);
			if (rc < 0)
				pr_err("can't get trx vol, rc=%d\n", rc);
			else
				pval->intval = wls_status->trx_vol_mv * 1000;
		} else {
			pval->intval = 0;
		}
		break;
	case OPLUS_CHG_PROP_TX_CURRENT_NOW:
		if (wls_status->trx_present) {
			rc = oplus_chg_wls_rx_get_trx_curr(wls_dev->wls_rx, &wls_status->trx_curr_ma);
			if (rc < 0)
				pr_err("can't get trx curr, rc=%d\n", rc);
			else
				pval->intval = wls_status->trx_curr_ma * 1000;
		} else {
			pval->intval = 0;
		}
		break;
	case OPLUS_CHG_PROP_CP_VOLTAGE_NOW:
		pval->intval = 0;
		break;
	case OPLUS_CHG_PROP_CP_CURRENT_NOW:
		pval->intval = 0;
		break;
	case OPLUS_CHG_PROP_WIRELESS_MODE:
		pval->intval = 0;
		break;
	case OPLUS_CHG_PROP_WIRELESS_TYPE:
		pval->intval = 0;
		break;
	case OPLUS_CHG_PROP_CEP_INFO:
		pval->intval = 0;
		break;
	case OPLUS_CHG_PROP_REAL_TYPE:
		switch (wls_status->wls_type) {
		case OPLUS_CHG_WLS_BPP:
			pval->intval = POWER_SUPPLY_TYPE_USB_DCP;
			break;
		case OPLUS_CHG_WLS_EPP:
			pval->intval = POWER_SUPPLY_TYPE_USB_PD;
			break;
		case OPLUS_CHG_WLS_EPP_PLUS:
			pval->intval = POWER_SUPPLY_TYPE_USB_PD;
			break;
		case OPLUS_CHG_WLS_WARP:
		case OPLUS_CHG_WLS_SWARP:
		case OPLUS_CHG_WLS_PD_65W:
			pval->intval = POWER_SUPPLY_TYPE_USB_DCP;
			break;
		default:
			pr_err("Unsupported charging mode(=%d)\n", wls_status->wls_type);
			pval->intval = POWER_SUPPLY_TYPE_UNKNOWN;
			break;
		}
		break;
	case OPLUS_CHG_PROP_COOL_DOWN:
		pval->intval = wls_status->cool_down;
		break;
#endif /* CONFIG_OPLUS_CHG_OOS */
	default:
		pr_err("get prop %d is not supported\n", prop);
		return -EINVAL;
	}
	if (rc < 0) {
		pr_err("Couldn't get prop %d rc = %d\n", prop, rc);
		return -ENODATA;
	}
	return 0;
}

static int oplus_chg_wls_set_prop(struct oplus_chg_mod *ocm,
			enum oplus_chg_mod_property prop,
			const union oplus_chg_mod_propval *pval)
{
	struct oplus_chg_wls *wls_dev = oplus_chg_mod_get_drvdata(ocm);
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	int rc = 0;

	switch (prop) {
	case OPLUS_CHG_PROP_VOLTAGE_MAX:
		if (wls_dev->debug_mode) {
			wls_status->fastchg_started = false;
			rc = oplus_chg_wls_rx_set_vout(wls_dev->wls_rx,
				pval->intval / 1000, 0);
			vote(wls_dev->fcc_votable, DEBUG_VOTER, false, 0, false);
		} else {
			rc = -EINVAL;
			pr_err("need to open debug mode first\n");
		}
		break;
	case OPLUS_CHG_PROP_VOLTAGE_MIN:
		break;
	case OPLUS_CHG_PROP_CURRENT_MAX:
		if (wls_status->fastchg_started)
			rc = vote(wls_dev->fcc_votable, MAX_VOTER, true, pval->intval / 1000, false);
		else
			rc = 0;
		break;
	case OPLUS_CHG_PROP_CHG_ENABLE:
		wls_dev->charge_enable = !!pval->intval;
		vote(wls_dev->rx_disable_votable, DEBUG_VOTER, !wls_dev->charge_enable, 1, false);
		break;
	case OPLUS_CHG_PROP_TRX_ONLINE:
		rc = oplus_chg_wls_set_trx_enable(wls_dev, !!pval->intval);
		break;
#ifdef OPLUS_CHG_DEBUG
	case OPLUS_CHG_PROP_FORCE_TYPE:
		wls_dev->force_type = pval->intval;
		if (wls_dev->force_type == OPLUS_CHG_WLS_FORCE_TYPE_NOEN) {
			wls_dev->debug_mode = false;
			vote(wls_dev->fcc_votable, DEBUG_VOTER, false, 0, false);
		} if (wls_dev->force_type == OPLUS_CHG_WLS_FORCE_TYPE_AUTO) {
			wls_dev->debug_mode = false;
			vote(wls_dev->fcc_votable, DEBUG_VOTER, false, 0, false);
		} else {
			wls_dev->debug_mode = true;
		}
		break;
	case OPLUS_CHG_PROP_PATH_CTRL:
		rc = oplus_chg_wls_path_ctrl(wls_dev, pval->intval);
		break;
#endif
	case OPLUS_CHG_PROP_STATUS_DELAY:
		break;
	case OPLUS_CHG_PROP_QUIET_MODE:
		if (!wls_status->rx_present ||
		 wls_status->adapter_type == WLS_ADAPTER_TYPE_USB ||
		 wls_status->adapter_type == WLS_ADAPTER_TYPE_NORMAL ||
		 wls_status->adapter_type == WLS_ADAPTER_TYPE_UNKNOWN) {
			pr_err("wls not present, can't %s quiet mode\n", !!pval->intval ? "enable" : "disable");
			rc = -EINVAL;
			break;
		}
		pr_info("%s quiet mode\n", !!pval->intval ? "enable" : "disable");
		wls_status->switch_quiet_mode = !!pval->intval;
		if (wls_status->switch_quiet_mode != wls_status->quiet_mode) {
			cancel_delayed_work_sync(&wls_dev->wls_rx_sm_work);
			queue_delayed_work(wls_dev->wls_wq, &wls_dev->wls_rx_sm_work, 0);
		}
		break;
	case OPLUS_CHG_PROP_TRX_POWER_EN:
		rc = oplus_chg_wls_nor_set_boost_en(wls_dev->wls_nor, !!pval->intval);
		break;
	case OPLUS_CHG_PROP_TRX_POWER_VOL:
		rc = oplus_chg_wls_nor_set_boost_vol(wls_dev->wls_nor, pval->intval);
		break;
	case OPLUS_CHG_PROP_TRX_POWER_CURR_LIMIT:
		rc = oplus_chg_wls_nor_set_boost_curr_limit(wls_dev->wls_nor, pval->intval);
		break;
	case OPLUS_CHG_PROP_FACTORY_MODE:
		wls_dev->factory_mode = !!pval->intval;
		break;
	case OPLUS_CHG_PROP_FOD_CAL:
		wls_dev->fod_is_cal = false;
		schedule_delayed_work(&wls_dev->fod_cal_work, 0);
		break;
	case OPLUS_CHG_PROP_BATT_CHG_ENABLE:
		wls_dev->batt_charge_enable = !!pval->intval;
		cancel_delayed_work_sync(&wls_dev->wls_rx_sm_work);
		queue_delayed_work(wls_dev->wls_wq, &wls_dev->wls_rx_sm_work, 0);
		break;
	case OPLUS_CHG_PROP_STATUS_KEEP:
		wls_dev->status_keep = pval->intval;
		pr_info("set wls_status_keep=%d\n", wls_dev->status_keep);
		if (wls_dev->status_keep == WLS_SK_NULL) {
			if (is_batt_ocm_available(wls_dev))
				oplus_chg_mod_changed(wls_dev->batt_ocm);
		}
		break;
#ifndef CONFIG_OPLUS_CHG_OOS
	case OPLUS_CHG_PROP_COOL_DOWN:
		wls_status->cool_down = pval->intval;
		pr_info("set cool down level to %d\n", wls_status->cool_down);
		oplus_chg_wls_config(wls_dev);
		break;
#endif
	default:
		pr_err("set prop %d is not supported\n", prop);
		rc = -EINVAL;
		break;
	}

	return rc;
}

static int oplus_chg_wls_prop_is_writeable(struct oplus_chg_mod *ocm,
				enum oplus_chg_mod_property prop)
{
	switch (prop) {
	case OPLUS_CHG_PROP_VOLTAGE_MAX:
	case OPLUS_CHG_PROP_VOLTAGE_MIN:
	case OPLUS_CHG_PROP_CURRENT_MAX:
	case OPLUS_CHG_PROP_CHG_ENABLE:
	case OPLUS_CHG_PROP_TRX_ONLINE:
#ifdef OPLUS_CHG_DEBUG
	case OPLUS_CHG_PROP_FORCE_TYPE:
	case OPLUS_CHG_PROP_PATH_CTRL:
#endif
	case OPLUS_CHG_PROP_STATUS_DELAY:
	case OPLUS_CHG_PROP_QUIET_MODE:
	case OPLUS_CHG_PROP_FACTORY_MODE:
#ifdef OPLUS_CHG_DEBUG
	case OPLUS_CHG_EXTERN_PROP_UPGRADE_FW:
	case OPLUS_CHG_EXTERN_PROP_PATH_CURRENT:
#endif
	case OPLUS_CHG_PROP_FOD_CAL:
	case OPLUS_CHG_PROP_BATT_CHG_ENABLE:
	case OPLUS_CHG_PROP_STATUS_KEEP:
#ifndef CONFIG_OPLUS_CHG_OOS
	case OPLUS_CHG_PROP_COOL_DOWN:
#endif
		return 1;
	default:
		break;
	}

	return 0;
}

static const struct oplus_chg_mod_desc oplus_chg_wls_mod_desc = {
	.name = "wireless",
	.type = OPLUS_CHG_MOD_WIRELESS,
	.properties = oplus_chg_wls_props,
	.num_properties = ARRAY_SIZE(oplus_chg_wls_props),
	.uevent_properties = oplus_chg_wls_uevent_props,
	.uevent_num_properties = ARRAY_SIZE(oplus_chg_wls_uevent_props),
	.exten_properties = oplus_chg_wls_exten_props,
	.num_exten_properties = ARRAY_SIZE(oplus_chg_wls_exten_props),
	.get_property = oplus_chg_wls_get_prop,
	.set_property = oplus_chg_wls_set_prop,
	.property_is_writeable	= oplus_chg_wls_prop_is_writeable,
};

static int oplus_chg_wls_event_notifier_call(struct notifier_block *nb,
		unsigned long val, void *v)
{
	struct oplus_chg_wls *wls_dev = container_of(nb, struct oplus_chg_wls, wls_event_nb);
	struct oplus_chg_mod *owner_ocm = v;

	switch(val) {
	case OPLUS_CHG_EVENT_ONLINE:
		if (owner_ocm == NULL) {
			pr_err("This event(=%d) does not support anonymous sending\n",
				val);
			return NOTIFY_BAD;
		}
		if (!strcmp(owner_ocm->desc->name, "usb")) {
			pr_info("usb online\n");
			wls_dev->usb_present = true;
			schedule_delayed_work(&wls_dev->usb_int_work, 0);
			if (wls_dev->wls_ocm)
				oplus_chg_mod_changed(wls_dev->wls_ocm);
		}
		break;
	case OPLUS_CHG_EVENT_OFFLINE:
		if (owner_ocm == NULL) {
			pr_err("This event(=%d) does not support anonymous sending\n",
				val);
			return NOTIFY_BAD;
		}
		if (!strcmp(owner_ocm->desc->name, "usb")) {
			wls_dev->usb_present = false;
			schedule_delayed_work(&wls_dev->usb_int_work, 0);
			if (wls_dev->wls_status.upgrade_fw_pending) {
				wls_dev->wls_status.upgrade_fw_pending = false;
				schedule_delayed_work(&wls_dev->wls_upgrade_fw_work, 0);
			}
		}
		break;
	case OPLUS_CHG_EVENT_PRESENT:
		if (owner_ocm == NULL) {
			pr_err("This event(=%d) does not support anonymous sending\n",
				val);
			return NOTIFY_BAD;
		}
		if (!strcmp(owner_ocm->desc->name, "usb")) {
			pr_info("usb present\n");
			wls_dev->usb_present = true;
			schedule_delayed_work(&wls_dev->usb_int_work, 0);
			if (wls_dev->wls_ocm)
				oplus_chg_mod_changed(wls_dev->wls_ocm);
		}
		if (!strcmp(owner_ocm->desc->name, "wireless")) {
			pr_info("wls present\n");
			if (wls_dev->wls_ocm) {
				oplus_chg_anon_mod_event(wls_dev->wls_ocm, val);
				oplus_chg_mod_changed(wls_dev->wls_ocm);
			}
		}
		break;
	case OPLUS_CHG_EVENT_ADSP_STARTED:
		pr_info("adsp started\n");
		adsp_started = true;
		if (online_pending) {
			online_pending = false;
			schedule_delayed_work(&wls_dev->wls_connect_work, 0);
		} else {
			if (get_boot_mode() != MSM_BOOT_MODE__CHARGE) {
				schedule_delayed_work(&wls_dev->wls_upgrade_fw_work, 0);
			} else {
				pr_info("check connect\n");
				wls_dev->wls_status.boot_online_keep = true;
				(void)oplus_chg_wls_rx_connect_check(wls_dev->wls_rx);
			}
		}
		break;
	case OPLUS_CHG_EVENT_OTG_ENABLE:
		vote(wls_dev->wrx_en_votable, OTG_EN_VOTER, true, 1, false);
		break;
	case OPLUS_CHG_EVENT_OTG_DISABLE:
		vote(wls_dev->wrx_en_votable, OTG_EN_VOTER, false, 0, false);
		break;
	case OPLUS_CHG_EVENT_CHARGE_DONE:
		wls_dev->wls_status.chg_done = true;
		break;
	case OPLUS_CHG_EVENT_CLEAN_CHARGE_DONE:
		wls_dev->wls_status.chg_done = false;
		wls_dev->wls_status.chg_done_quiet_mode = false;
		break;
	default:
		break;
	}

	return NOTIFY_OK;
}

static int oplus_chg_wls_mod_notifier_call(struct notifier_block *nb,
		unsigned long val, void *v)
{
	struct oplus_chg_wls *wls_dev = container_of(nb, struct oplus_chg_wls, wls_mod_nb);
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
#ifdef OPLUS_CHG_OP_DEF
	struct oplus_chg_wls_dynamic_config *dynamic_cfg =&wls_dev->dynamic_config;
#endif
	switch(val) {
	case OPLUS_CHG_EVENT_ONLINE:
		pr_info("wls online\n");
		wls_status->boot_online_keep = false;
		if (wls_status->rx_online)
			break;
		wls_status->rx_online = true;
		wls_status->online_keep = false;
		if (adsp_started)
			schedule_delayed_work(&wls_dev->wls_connect_work, 0);
		else
			online_pending = true;
		if (wls_dev->wls_ocm) {
			oplus_chg_global_event(wls_dev->wls_ocm, OPLUS_CHG_EVENT_ONLINE);
			oplus_chg_mod_changed(wls_dev->wls_ocm);
		}
		break;
	case OPLUS_CHG_EVENT_OFFLINE:
		pr_info("wls offline\n");
		wls_status->boot_online_keep = false;
		if (!wls_status->rx_online && !wls_status->rx_present)
			break;
		wls_status->rx_present = false;
		wls_status->rx_online = false;
		schedule_delayed_work(&wls_dev->wls_connect_work, 0);
		if (wls_dev->wls_ocm) {
			oplus_chg_global_event(wls_dev->wls_ocm, OPLUS_CHG_EVENT_OFFLINE);
			oplus_chg_mod_changed(wls_dev->wls_ocm);
		}
		break;
	case OPLUS_CHG_EVENT_PRESENT:
		pr_info("wls present\n");
		wls_status->boot_online_keep = false;
		if (wls_status->rx_present)
			break;
		wls_status->rx_present = true;
		wls_status->online_keep = false;
		if (wls_dev->wls_ocm) {
			#ifdef OPLUS_CHG_OP_DEF
			oplus_chg_global_event(wls_dev->wls_ocm, OPLUS_CHG_EVENT_PRESENT);
			#endif
			oplus_chg_mod_changed(wls_dev->wls_ocm);
		}
		break;
	case OPLUS_CHG_EVENT_OP_TRX:
		wls_status->is_op_trx = true;
		break;
	case OPLUS_CHG_EVENT_CHECK_TRX:
		if (wls_status->trx_present) {
			cancel_delayed_work(&wls_dev->wls_trx_sm_work);
			queue_delayed_work(wls_dev->wls_wq, &wls_dev->wls_trx_sm_work, 0);
		} else {
			pr_err("trx not present\n");
		}
		break;
	case OPLUS_CHG_EVENT_POWER_CHANGED:
		oplus_chg_wls_config(wls_dev);
		break;
	case OPLUS_CHG_EVENT_LCD_ON:
		pr_info("lcd on\n");
		wls_status->led_on = true;
		break;
	case OPLUS_CHG_EVENT_LCD_OFF:
		pr_info("lcd off\n");
		wls_status->led_on = false;
		break;
	case OPLUS_CHG_EVENT_CALL_ON:
		pr_info("call on\n");
		vote(wls_dev->fcc_votable, CALL_VOTER, true, dynamic_cfg->wls_fast_chg_call_on_curr_ma, false);
		break;
	case OPLUS_CHG_EVENT_CALL_OFF:
		pr_info("call off\n");
		vote(wls_dev->fcc_votable, CALL_VOTER, false, 0, false);
		break;
	case OPLUS_CHG_EVENT_CAMERA_ON:
		pr_info("camera on\n");
		vote(wls_dev->fcc_votable, CAMERA_VOTER, true, dynamic_cfg->wls_fast_chg_camera_on_curr_ma, false);
		break;
	case OPLUS_CHG_EVENT_CAMERA_OFF:
		pr_info("camera off\n");
		vote(wls_dev->fcc_votable, CAMERA_VOTER, false, 0, false);
		break;
	case OPLUS_CHG_EVENT_RX_IIC_ERR:
		if (wls_status->rx_present) {
			pr_info("Restart the rx disable\n");
			wls_dev->wls_status.online_keep = true;
			vote(wls_dev->rx_disable_votable, RX_IIC_VOTER, true, 1, false);
			schedule_delayed_work(&wls_dev->rx_iic_restore_work, msecs_to_jiffies(500));
		}
		break;
	default:
		break;
	}

	return NOTIFY_OK;
}

static int oplus_chg_wls_changed_notifier_call(struct notifier_block *nb,
		unsigned long val, void *v)
{
	struct oplus_chg_wls *wls_dev = container_of(nb, struct oplus_chg_wls, wls_changed_nb);
	struct oplus_chg_mod *owner_ocm = v;

	switch(val) {
	case OPLUS_CHG_EVENT_CHANGED:
		if (!strcmp(owner_ocm->desc->name, "wireless") && is_wls_psy_available(wls_dev))
			power_supply_changed(wls_dev->wls_psy);
		break;
	default:
		break;
	}

	return NOTIFY_OK;
}

static int oplus_chg_wls_aes_mutual_notifier_call(struct notifier_block *nb,
		unsigned long val, void *v)
{
	int i;
	struct oplus_chg_wls *wls_dev;
	struct oplus_chg_wls_status *wls_status;
	struct oplus_chg_cmd *p_cmd;
	wls_third_part_auth_result *aes_auth_result;

	wls_dev = container_of(nb, struct oplus_chg_wls, wls_aes_nb);
	wls_status = &wls_dev->wls_status;

	p_cmd = (struct oplus_chg_cmd *)v;
	if (p_cmd->cmd != CMD_WLS_THIRD_PART_AUTH) {
		pr_err("cmd is not matching, should return\n");
		return NOTIFY_OK;
	}

	if (p_cmd->data_size != sizeof(wls_third_part_auth_result)) {
		pr_err("data_len is not ok, datas is invalid\n");
		return NOTIFY_DONE;
	}

	aes_auth_result = (wls_third_part_auth_result *)(p_cmd->data_buf);
	if (aes_auth_result) {
		wls_status->aes_key_num = aes_auth_result->effc_key_index;
		pr_info("aes_key_num:%d\n", wls_status->aes_key_num);

		memcpy(wls_status->aes_verfity_data.aes_random_num,
			aes_auth_result->aes_random_num,
			sizeof(aes_auth_result->aes_random_num));
		for (i = 0; i < WLS_AUTH_AES_ENCODE_LEN; i++)
			pr_info("aes_random_num[%d]:0x%02x\n",
			    i, (wls_status->aes_verfity_data.aes_random_num)[i]);

		memcpy(wls_status->aes_verfity_data.aes_encode_num,
		    aes_auth_result->aes_encode_num,
		    sizeof(aes_auth_result->aes_encode_num));
		wls_status->aes_verity_data_ok = true;
		for (i = 0; i < WLS_AUTH_AES_ENCODE_LEN; i++)
			pr_info("aes_encode_num[%d]:0x%02x ", i,
			    (wls_status->aes_verfity_data.aes_encode_num)[i]);
	}

	return NOTIFY_OK;
}

static int oplus_chg_wls_init_mod(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_mod_config ocm_cfg = {};
	int rc;

	ocm_cfg.drv_data = wls_dev;
	ocm_cfg.of_node = wls_dev->dev->of_node;

	wls_dev->wls_ocm = oplus_chg_mod_register(wls_dev->dev,
					   &oplus_chg_wls_mod_desc,
					   &ocm_cfg);
	if (IS_ERR(wls_dev->wls_ocm)) {
		pr_err("Couldn't register wls ocm\n");
		rc = PTR_ERR(wls_dev->wls_ocm);
		return rc;
	}
	wls_dev->wls_ocm->notifier = &wls_ocm_notifier;
	wls_dev->wls_mod_nb.notifier_call = oplus_chg_wls_mod_notifier_call;
	rc = oplus_chg_reg_mod_notifier(wls_dev->wls_ocm, &wls_dev->wls_mod_nb);
	if (rc) {
		pr_err("register wls mod notifier error, rc=%d\n", rc);
		goto reg_wls_mod_notifier_err;
	}
	wls_dev->wls_event_nb.notifier_call = oplus_chg_wls_event_notifier_call;
	rc = oplus_chg_reg_event_notifier(&wls_dev->wls_event_nb);
	if (rc) {
		pr_err("register wls event notifier error, rc=%d\n", rc);
		goto reg_wls_event_notifier_err;
	}
	wls_dev->wls_changed_nb.notifier_call = oplus_chg_wls_changed_notifier_call;
	rc = oplus_chg_reg_changed_notifier(&wls_dev->wls_changed_nb);
	if (rc) {
		pr_err("register wls changed notifier error, rc=%d\n", rc);
		goto reg_wls_changed_notifier_err;
	}


	wls_dev->wls_aes_nb.notifier_call = oplus_chg_wls_aes_mutual_notifier_call;
	rc = oplus_chg_comm_reg_mutual_notifier(&wls_dev->wls_aes_nb);
	if (rc) {
		pr_err("register wls aes mutual notifier error, rc=%d\n", rc);
		goto reg_wls_aes_mutual_notifier_err;
	}
	return 0;

	oplus_chg_comm_unreg_mutual_notifier(&wls_dev->wls_aes_nb);
reg_wls_aes_mutual_notifier_err:
	oplus_chg_unreg_changed_notifier(&wls_dev->wls_changed_nb);
reg_wls_changed_notifier_err:
	oplus_chg_unreg_event_notifier(&wls_dev->wls_event_nb);
reg_wls_event_notifier_err:
	oplus_chg_unreg_mod_notifier(wls_dev->wls_ocm, &wls_dev->wls_mod_nb);
reg_wls_mod_notifier_err:
	oplus_chg_mod_unregister(wls_dev->wls_ocm);
	return rc;
}

static void oplus_chg_wls_usb_int_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_wls *wls_dev = container_of(dwork, struct oplus_chg_wls, usb_int_work);

	if (wls_dev->usb_present) {
		vote(wls_dev->rx_disable_votable, USB_VOTER, true, 1, false);
		(void)oplus_chg_wls_set_trx_enable(wls_dev, false);
		oplus_chg_anon_mod_event(wls_dev->wls_ocm, OPLUS_CHG_EVENT_OFFLINE);
	} else {
		vote(wls_dev->rx_disable_votable, USB_VOTER, false, 0, false);
	}
}

static void oplus_chg_wls_connect_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_wls *wls_dev = container_of(dwork, struct oplus_chg_wls, wls_connect_work);
	struct oplus_chg_wls_dynamic_config *dynamic_cfg =&wls_dev->dynamic_config;
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	struct oplus_wls_chg_rx *wls_rx = wls_dev->wls_rx;
	struct oplus_wls_chg_normal *wls_nor = wls_dev->wls_nor;
	unsigned long delay_time = jiffies + msecs_to_jiffies(500);
	int skin_temp;
	int rc;

	if (wls_status->rx_online) {
		pr_info("wls connect >>>>>>>>>>>>>>>>>>>>>>>>>>>>\n");
		if (!wls_dev->rx_wake_lock_on) {
			pr_info("acquire rx_wake_lock\n");
			__pm_stay_awake(wls_dev->rx_wake_lock);
			wls_dev->rx_wake_lock_on = true;
		} else {
			pr_err("rx_wake_lock is already stay awake\n");
		}

		cancel_delayed_work_sync(&wls_dev->wls_clear_trx_work);
		oplus_chg_wls_reset_variables(wls_dev);
		/*
		 * The verity_state_keep flag needs to be cleared immediately
		 * after reconnection to ensure that the subsequent verification
		 * function is normal.
		 */
		wls_status->verity_state_keep = false;
		pr_info("nor_fcc_votable: client:%s, result=%d\n",
			get_effective_client(wls_dev->nor_fcc_votable),
			get_effective_result(wls_dev->nor_fcc_votable));
		vote(wls_dev->nor_fcc_votable, USER_VOTER, true, 1500, false);
		rerun_election(wls_dev->nor_fcc_votable, false);
		vote(wls_dev->nor_icl_votable, USER_VOTER, true, 100, false);
		rerun_election(wls_dev->nor_icl_votable, false);
		schedule_delayed_work(&wls_dev->wls_data_update_work, 0);
		queue_delayed_work(wls_dev->wls_wq, &wls_dev->wls_rx_sm_work, 0);
		/* reset charger status */
		vote(wls_dev->nor_out_disable_votable, USER_VOTER, true, 1, false);
		vote(wls_dev->nor_out_disable_votable, USER_VOTER, false, 0, false);
		rc = oplus_chg_wls_get_skin_temp(wls_dev, &skin_temp);
		if (rc < 0) {
			pr_err("can't get skin temp, rc=%d\n", rc);
			skin_temp = 250;
		}
		oplus_chg_strategy_init(&wls_dev->wls_fast_chg_led_off_strategy,
			dynamic_cfg->wls_fast_chg_led_off_strategy_data,
			oplus_chg_get_chg_strategy_data_len(
				dynamic_cfg->wls_fast_chg_led_off_strategy_data,
				CHG_STRATEGY_DATA_TABLE_MAX),
			skin_temp);
		oplus_chg_strategy_init(&wls_dev->wls_fast_chg_led_on_strategy,
			dynamic_cfg->wls_fast_chg_led_on_strategy_data,
			oplus_chg_get_chg_strategy_data_len(
				dynamic_cfg->wls_fast_chg_led_on_strategy_data,
				CHG_STRATEGY_DATA_TABLE_MAX),
			skin_temp);
	} else {
		pr_info("wls disconnect <<<<<<<<<<<<<<<<<<<<<<<<<<\n");
		vote(wls_dev->rx_disable_votable, CONNECT_VOTER, true, 1, false);
		if (wls_dev->support_fastchg) {
			oplus_chg_wls_set_cp_boost_enable(wls_dev, false);
			oplus_chg_wls_fast_set_enable(wls_dev->wls_fast, false);
		}
		cancel_delayed_work_sync(&wls_dev->wls_data_update_work);
		(void)oplus_chg_wls_clean_msg(wls_dev);
		(void)oplus_chg_wls_rx_clean_source(wls_rx);
		(void)oplus_chg_wls_nor_clean_source(wls_nor);
		oplus_chg_wls_reset_variables(wls_dev);
		cancel_delayed_work_sync(&wls_dev->wls_rx_sm_work);
		cancel_delayed_work_sync(&wls_dev->wls_trx_sm_work);
		cancel_delayed_work_sync(&wls_dev->wls_verity_work);
		if (time_is_after_jiffies(delay_time)) {
			delay_time = delay_time - jiffies;
			msleep(jiffies_to_msecs(delay_time));
		}
		wls_dev->wls_fast_chg_led_off_strategy.initialized = false;
		wls_dev->wls_fast_chg_led_on_strategy.initialized = false;
		vote(wls_dev->rx_disable_votable, CONNECT_VOTER, false, 0, false);
		oplus_chg_wls_reset_variables(wls_dev);
		if (wls_status->online_keep) {
			schedule_delayed_work(&wls_dev->online_keep_remove_work, msecs_to_jiffies(2000));
		} else {
			if (wls_dev->rx_wake_lock_on) {
				pr_info("release rx_wake_lock\n");
				__pm_relax(wls_dev->rx_wake_lock);
				wls_dev->rx_wake_lock_on = false;
			} else {
				pr_err("rx_wake_lock is already relax\n");
			}
		}
	}
}

static int oplus_chg_wls_nor_skin_check(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	struct oplus_chg_wls_skin_step *skin_step;
	int *skin_step_count;
	int skin_temp;
	int curr_ma;
	int rc;

	rc = oplus_chg_wls_get_skin_temp(wls_dev, &skin_temp);
	if (rc < 0) {
		pr_err("can't get skin temp, rc=%d\n", rc);
		return rc;
	}

	switch (wls_status->wls_type) {
	case OPLUS_CHG_WLS_BPP:
		skin_step = &wls_dev->bpp_skin_step;
		skin_step_count = &wls_status->bpp_skin_step;
		break;
	case OPLUS_CHG_WLS_EPP:
#ifdef CONFIG_OPLUS_CHG_OOS
		if (wls_status->led_on && !wls_dev->factory_mode) {
			skin_step = &wls_dev->epp_led_on_skin_step;
			skin_step_count = &wls_status->epp_led_on_skin_step;
		} else {
			skin_step = &wls_dev->epp_skin_step;
			skin_step_count = &wls_status->epp_skin_step;
		}
#else
		skin_step = &wls_dev->epp_skin_step;
		skin_step_count = &wls_status->epp_skin_step;
#endif
		break;
	case OPLUS_CHG_WLS_EPP_PLUS:
#ifdef CONFIG_OPLUS_CHG_OOS
		if (wls_status->led_on && !wls_dev->factory_mode) {
			skin_step = &wls_dev->epp_plus_led_on_skin_step;
			skin_step_count = &wls_status->epp_plus_led_on_skin_step;
		} else {
			skin_step = &wls_dev->epp_plus_skin_step;
			skin_step_count = &wls_status->epp_plus_skin_step;
		}
#else
		skin_step = &wls_dev->epp_plus_skin_step;
		skin_step_count = &wls_status->epp_plus_skin_step;
#endif
		break;
	case OPLUS_CHG_WLS_WARP:
	case OPLUS_CHG_WLS_SWARP:
	case OPLUS_CHG_WLS_PD_65W:
		skin_step = &wls_dev->epp_plus_skin_step;
		skin_step_count = &wls_status->epp_plus_skin_step;
		break;
	default:
		pr_err("Unsupported charging mode(=%d)\n", wls_status->wls_type);
		return -EINVAL;
	}

	if (skin_step->max_step == 0)
		return 0;

	if ((skin_temp < skin_step->skin_step[*skin_step_count].low_threshold) &&
	    (*skin_step_count > 0)) {
		pr_info("skin_temp=%d, switch to the previous gear\n", skin_temp);
		*skin_step_count = *skin_step_count - 1;
	} else if ((skin_temp > skin_step->skin_step[*skin_step_count].high_threshold) &&
		   (*skin_step_count < (skin_step->max_step - 1))) {
		pr_info("skin_temp=%d, switch to the next gear\n", skin_temp);
		*skin_step_count = *skin_step_count + 1;
	}
	curr_ma = skin_step->skin_step[*skin_step_count].curr_ma;
	vote(wls_dev->nor_icl_votable, SKIN_VOTER, true, curr_ma, true);

	return 0;
}

static void oplus_chg_wls_fast_fcc_param_init(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_fcc_step *fcc_chg = &wls_dev->wls_fcc_step;
	int i;

	for(i = 0; i < fcc_chg->max_step; i++) {
		if (fcc_chg->fcc_step[i].low_threshold > 0)
			fcc_chg->allow_fallback[i] = true;
		else
			fcc_chg->allow_fallback[i] = false;
	}
}

static void oplus_chg_wls_fast_switch_next_step(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_fcc_step *fcc_chg = &wls_dev->wls_fcc_step;
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	struct oplus_chg_wls_dynamic_config *dynamic_cfg =&wls_dev->dynamic_config;
	enum oplus_chg_temp_region temp_region;
	u32 batt_vol_max = fcc_chg->fcc_step[wls_status->fastchg_level].vol_max_mv;
	int batt_vol_mv;
	int batt_temp;
	int rc;

	if (!is_comm_ocm_available(wls_dev)) {
		pr_err("comm mod not fount\n");
		return;
	}
	temp_region = oplus_chg_comm_get_temp_region(wls_dev->comm_ocm);
	rc = oplus_chg_wls_get_vbat(wls_dev, &batt_vol_mv);
	if (rc < 0) {
		pr_err("can't get batt vol, rc=%d\n", rc);
		return;
	}
	rc = oplus_chg_wls_get_batt_temp(wls_dev, &batt_temp);
	if (rc < 0) {
		pr_err("can't get batt temp, rc=%d\n");
		return;
	}

	if (fcc_chg->fcc_step[wls_status->fastchg_level].need_wait == 0) {
		if (batt_vol_mv >= batt_vol_max) {
			/* Must delay 1 sec and wait for the batt voltage to drop */
			fcc_chg->fcc_wait_timeout = jiffies + HZ * 5;
		} else {
			fcc_chg->fcc_wait_timeout = jiffies;
		}
	} else {
		/* Delay 1 minute and wait for the temperature to drop */
		fcc_chg->fcc_wait_timeout = jiffies + HZ * 60;
	}

	wls_status->fastchg_level++;
	pr_info("switch to next level=%d\n", wls_status->fastchg_level);
	if (wls_status->fastchg_level >= fcc_chg->max_step) {
		if (batt_vol_mv >= batt_vol_max) {
			pr_info("run normal charge ffc\n");
			wls_status->ffc_check = true;
		}
	} else {
		wls_status->wait_cep_stable = true;
		vote(wls_dev->fcc_votable, FCC_VOTER, true,
		     fcc_chg->fcc_step[wls_status->fastchg_level].curr_ma, false);
	}
	wls_status->fastchg_level_init_temp = batt_temp;
	if (batt_vol_mv >= batt_vol_max) {
		fcc_chg->allow_fallback[wls_status->fastchg_level] = false;
		if ((temp_region == BATT_TEMP_PRE_NORMAL) &&
		    (fcc_chg->fcc_step[wls_status->fastchg_level].curr_ma >= dynamic_cfg->fastchg_curr_max_ma)) {
			fcc_chg->fcc_wait_timeout = jiffies;
		}
	}
}

static void oplus_chg_wls_fast_switch_prev_step(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_fcc_step *fcc_chg = &wls_dev->wls_fcc_step;
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;

	wls_status->fastchg_level--;
	pr_info("switch to prev level=%d\n", wls_status->fastchg_level);
	vote(wls_dev->fcc_votable, FCC_VOTER, true,
	     fcc_chg->fcc_step[wls_status->fastchg_level].curr_ma, false);
	wls_status->fastchg_level_init_temp = 0;
	fcc_chg->fcc_wait_timeout = jiffies;
}

static int oplus_chg_wls_fast_temp_check(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_fcc_step *fcc_chg = &wls_dev->wls_fcc_step;
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	struct oplus_chg_wls_dynamic_config *dynamic_cfg =&wls_dev->dynamic_config;
	enum oplus_chg_temp_region temp_region;
	int batt_temp;
	int batt_vol_mv;
	int def_curr_ma, fcc_curr_ma;
	/*
	 * We want the temperature to drop when switching to a lower current range.
	 * If the temperature rises by 2 degrees before the next gear begins to
	 * detect temperature, then you should immediately switch to a lower gear.
	 */
	int temp_diff;
	u32 batt_vol_max = fcc_chg->fcc_step[wls_status->fastchg_level].vol_max_mv;
	int rc;

	if (!is_comm_ocm_available(wls_dev)) {
		pr_err("comm mod not fount\n");
		return -ENODEV;
	}

	temp_region = oplus_chg_comm_get_temp_region(wls_dev->comm_ocm);

	if ((temp_region != BATT_TEMP_PRE_NORMAL) &&
	    (temp_region != BATT_TEMP_NORMAL)) {
		pr_info("Abnormal battery temperature, exit fast charge\n");
		vote(wls_dev->fastchg_disable_votable, FCC_VOTER, true, 1, false);
		return -EPERM;
	}
	rc = oplus_chg_wls_get_vbat(wls_dev, &batt_vol_mv);
	if (rc < 0) {
		pr_err("can't get batt vol, rc=%d\n", rc);
		return rc;
	}
	rc = oplus_chg_wls_get_batt_temp(wls_dev, &batt_temp);
	if (rc < 0) {
		pr_err("can't get batt temp, rc=%d\n");
		return rc;
	}
	def_curr_ma = get_client_vote(wls_dev->fcc_votable, JEITA_VOTER);
	if (def_curr_ma <= 0)
		def_curr_ma = get_client_vote(wls_dev->fcc_votable, MAX_VOTER);
	else
		def_curr_ma = min(get_client_vote(wls_dev->fcc_votable, MAX_VOTER), def_curr_ma);
	pr_err("wkcs: def_curr_ma=%d, max_step=%d\n", def_curr_ma, fcc_chg->max_step);
	fcc_curr_ma = fcc_chg->fcc_step[wls_status->fastchg_level].curr_ma;
	if (wls_status->fastchg_level_init_temp != 0)
		temp_diff = batt_temp - wls_status->fastchg_level_init_temp;
	else
		temp_diff = 0;

	pr_err("battery temp = %d, vol = %d, level = %d, temp_diff = %d, fcc_curr_ma=%d\n",
		 batt_temp, batt_vol_mv, wls_status->fastchg_level, temp_diff, fcc_curr_ma);

	if (wls_status->fastchg_level == 0) {
		if (def_curr_ma < fcc_curr_ma) {
			if ((wls_status->fastchg_level + 1) < fcc_chg->max_step) {
				if (def_curr_ma < fcc_chg->fcc_step[wls_status->fastchg_level + 1].curr_ma) {
					pr_info("target current too low, switch next step\n");
					oplus_chg_wls_fast_switch_next_step(wls_dev);
					fcc_chg->fcc_wait_timeout = jiffies;
					return 0;
				}
			} else {
				pr_info("target current too low, switch next step\n");
				oplus_chg_wls_fast_switch_next_step(wls_dev);
				fcc_chg->fcc_wait_timeout = jiffies;
				return 0;
			}
		}
		if ((batt_temp > fcc_chg->fcc_step[wls_status->fastchg_level].high_threshold) ||
		    (batt_vol_mv >= batt_vol_max)) {
			oplus_chg_wls_fast_switch_next_step(wls_dev);
		}
	} else if (wls_status->fastchg_level >= fcc_chg->max_step) {  // switch to pmic
		vote(wls_dev->fastchg_disable_votable, FCC_VOTER, true, 1, false);
		return -EPERM;
	} else {
		if (def_curr_ma < fcc_curr_ma) {
			if ((wls_status->fastchg_level + 1) < fcc_chg->max_step) {
				if (def_curr_ma < fcc_chg->fcc_step[wls_status->fastchg_level + 1].curr_ma) {
					pr_info("target current too low, switch next step\n");
					oplus_chg_wls_fast_switch_next_step(wls_dev);
					fcc_chg->fcc_wait_timeout = jiffies;
					return 0;
				}
			} else {
				pr_info("target current too low, switch next step\n");
				oplus_chg_wls_fast_switch_next_step(wls_dev);
				fcc_chg->fcc_wait_timeout = jiffies;
				return 0;
			}
		}
		if (batt_vol_mv >= dynamic_cfg->batt_vol_max_mv) {
			pr_info("batt voltage too high, switch next step\n");
			oplus_chg_wls_fast_switch_next_step(wls_dev);
			return 0;
		}
		if ((batt_temp < fcc_chg->fcc_step[wls_status->fastchg_level].low_threshold) &&
		    fcc_chg->allow_fallback[wls_status->fastchg_level] &&
		    (def_curr_ma > fcc_chg->fcc_step[wls_status->fastchg_level].curr_ma)) {
			pr_info("target current too low, switch next step\n");
			oplus_chg_wls_fast_switch_prev_step(wls_dev);
			return 0;
		}
		pr_info("jiffies=%u, timeout=%u, high_threshold=%d, batt_vol_max=%d\n",
			jiffies, fcc_chg->fcc_wait_timeout,
			fcc_chg->fcc_step[wls_status->fastchg_level].high_threshold,
			batt_vol_max);
		if (time_after(jiffies, fcc_chg->fcc_wait_timeout) || (temp_diff > 200)) {
			if ((batt_temp > fcc_chg->fcc_step[wls_status->fastchg_level].high_threshold) ||
			    (batt_vol_mv >= batt_vol_max)) {
				oplus_chg_wls_fast_switch_next_step(wls_dev);
			}
		}
	}

	return 0;
}

#define CEP_ERR_MAX 3
#define CEP_OK_MAX 10
#define CEP_WAIT_MAX 20
#define CEP_OK_TIMEOUT_MAX 60
static int oplus_chg_wls_fast_cep_check(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	int cep = 0;
	int curr_ma, cep_curr_ma;
	static int wait_cep_count;
	static int cep_err_count;
	static int cep_ok_count;
	int rc;

	rc = oplus_chg_wls_get_cep(wls_dev->wls_rx, &cep);
	if (rc < 0) {
		pr_err("can't get cep, rc=%d\n", rc);
		return 0;
	}

	if (!wls_status->wait_cep_stable) {
		/* Insufficient energy only when CEP is positive */
		if (cep < 3) {
			cep_ok_count++;
			cep_err_count = 0;
			if ((cep_ok_count >= CEP_OK_MAX) &&
			    time_after(jiffies, wls_status->cep_ok_wait_timeout) &&
			    is_client_vote_enabled(wls_dev->fcc_votable, CEP_VOTER)) {
				pr_info("recovery charging current\n");
				cep_ok_count = 0;
				wls_status->wait_cep_stable = true;
				wls_status->cep_ok_wait_timeout = jiffies + CEP_OK_TIMEOUT_MAX * HZ;
				wait_cep_count = 0;
				vote(wls_dev->fcc_votable, CEP_VOTER, false, 0, false);
			}
		} else {
			cep_ok_count = 0;
			cep_err_count++;
			if (cep_err_count >= CEP_ERR_MAX) {
				pr_info("reduce charging current\n");
				cep_err_count = 0;
				wls_status->wait_cep_stable = true;
				wait_cep_count = 0;
				if (is_client_vote_enabled(wls_dev->fcc_votable, CEP_VOTER))
					cep_curr_ma = get_client_vote(wls_dev->fcc_votable, CEP_VOTER);
				else
					cep_curr_ma = 0;
				if ((cep_curr_ma > 0) && (cep_curr_ma <= WLS_FASTCHG_CURR_MIN_MA)){
					pr_info("Energy is too low, exit fast charge\n");
					vote(wls_dev->fastchg_disable_votable, CEP_VOTER, true, 1, false);
					wls_status->cep_ok_wait_timeout = jiffies + CEP_OK_TIMEOUT_MAX * HZ;
					return -1;
				} else {
					curr_ma = wls_status->iout_ma;
					/* Target current is adjusted in 50ma steps*/
					curr_ma = curr_ma - (curr_ma % WLS_FASTCHG_CURR_ERR_MA) - WLS_FASTCHG_CURR_ERR_MA;
					if (curr_ma < WLS_FASTCHG_CURR_MIN_MA)
						curr_ma = WLS_FASTCHG_CURR_MIN_MA;
					vote(wls_dev->fcc_votable, CEP_VOTER, true, curr_ma, false);
				}
				wls_status->cep_ok_wait_timeout = jiffies + CEP_OK_TIMEOUT_MAX * HZ;
			}
		}
	} else {
		if (wait_cep_count < CEP_WAIT_MAX) {
			wait_cep_count++;
		} else {
			wls_status->wait_cep_stable = false;
			wait_cep_count =0;
		}
	}

	return 0;
}

static int oplus_chg_wls_fast_ibat_check(struct oplus_chg_wls *wls_dev)
{
	int ibat_ma = 0;
	int rc;

	rc = oplus_chg_wls_get_ibat(wls_dev, &ibat_ma);
	if (rc < 0) {
		pr_err("can't get ibat, rc=%d\n");
		return rc;
	}

	if (ibat_ma >= WLS_FASTCHG_CURR_DISCHG_MAX_MA) {
		pr_err("discharge current is too large, exit fast charge\n");
		vote(wls_dev->fastchg_disable_votable, BATT_CURR_VOTER, true, 1, false);
		return -1;
	}

	return 0;
}

#define IOUT_SMALL_COUNT 30
static int oplus_chg_wls_fast_iout_check(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	int iout_ma = 0;

	iout_ma = wls_status->iout_ma;

	if (iout_ma <= WLS_FASTCHG_IOUT_CURR_MIN_MA) {
		wls_status->iout_ma_conunt++;
		if (wls_status->iout_ma_conunt >= IOUT_SMALL_COUNT) {
			pr_err("iout current is too small, exit fast charge\n");
			vote(wls_dev->fastchg_disable_votable, IOUT_CURR_VOTER, true, 1, false);
			return -1;
		}
	} else {
		wls_status->iout_ma_conunt = 0;
	}
	return 0;
}

static void oplus_chg_wls_fast_skin_temp_check(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	int skin_temp;
	int curr_ma;
	int rc;

	if (wls_dev->factory_mode) {
		vote(wls_dev->fcc_votable, SKIN_VOTER, false, 0, false);
		return;
	}

	rc = oplus_chg_wls_get_skin_temp(wls_dev, &skin_temp);
	if (rc < 0) {
		pr_err("can't get skin temp, rc=%d\n", rc);
		skin_temp = 250;
	}

	if (wls_status->led_on) {
#ifdef CONFIG_OPLUS_CHG_OOS
		if (wls_dev->wls_fast_chg_led_on_strategy.initialized) {
			curr_ma = oplus_chg_strategy_get_data(&wls_dev->wls_fast_chg_led_on_strategy,
				&wls_dev->wls_fast_chg_led_on_strategy.temp_region, skin_temp);
			pr_info("led is on, curr = %d\n", curr_ma);
			vote(wls_dev->fcc_votable, SKIN_VOTER, true, curr_ma, false);
		}
#else
		vote(wls_dev->fcc_votable, SKIN_VOTER, false, 0, false);
#endif
	} else {
		if (wls_dev->wls_fast_chg_led_off_strategy.initialized) {
			curr_ma = oplus_chg_strategy_get_data(&wls_dev->wls_fast_chg_led_off_strategy,
				&wls_dev->wls_fast_chg_led_off_strategy.temp_region, skin_temp);
			pr_info("led is off, curr = %d\n", curr_ma);
			vote(wls_dev->fcc_votable, SKIN_VOTER, true, curr_ma, false);
		}
	}
}

#ifndef CONFIG_OPLUS_CHG_OOS
static int oplus_chg_wls_fast_cool_down_check(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	int cool_down = wls_status->cool_down;
	int ibat_ma = 0;
	int iout_ma = 0;

	if (wls_status->led_on && !wls_dev->factory_mode) {
		if (cool_down > 0 && cool_down <= COOL_DOWN_12V_THR) {
			pr_err("cool_down level < %d, exit fast charge\n", COOL_DOWN_12V_THR);
			vote(wls_dev->fastchg_disable_votable, COOL_DOWN_VOTER, true, 1, false);
			vote(wls_dev->fcc_votable, COOL_DOWN_VOTER, false, 0, false);
			return -1;
		}
		if (cool_down >= ARRAY_SIZE(cool_down_swarp) && ARRAY_SIZE(cool_down_swarp) > 1)
			cool_down = ARRAY_SIZE(cool_down_swarp) - 1;
		ibat_ma = cool_down_swarp[cool_down];
		iout_ma = ibat_ma / 2;
		if (iout_ma > 0)
			vote(wls_dev->fcc_votable, COOL_DOWN_VOTER, true, iout_ma, false);
	} else {
		vote(wls_dev->fcc_votable, COOL_DOWN_VOTER, false, 0, false);
	}

	return 0;
}
#endif /* CONFIG_OPLUS_CHG_OOS */

static void oplus_chg_wls_check_quiet_mode(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;

	if (wls_status->fastchg_started)
		return;

	if (wls_status->quiet_mode) {
		if (!wls_status->switch_quiet_mode && !wls_status->chg_done_quiet_mode) {
			if (wls_status->adapter_id == WLS_ADAPTER_MODEL_0) {
				(void)oplus_chg_wls_send_msg(wls_dev, WLS_CMD_SET_NORMAL_MODE, 0xff, 2);
			} else if (wls_status->adapter_id == WLS_ADAPTER_MODEL_1) {
				if (wls_status->adapter_power & WLS_ADAPTER_POWER_MASK)
					(void)oplus_chg_wls_send_msg(wls_dev, WLS_CMD_SET_FAN_SPEED, 93, 2);
				else
					(void)oplus_chg_wls_send_msg(wls_dev, WLS_CMD_SET_FAN_SPEED, 60, 2);
			} else if (wls_status->adapter_id == WLS_ADAPTER_MODEL_2) {
				if (wls_status->adapter_power & WLS_ADAPTER_POWER_MASK)
					(void)oplus_chg_wls_send_msg(wls_dev, WLS_CMD_SET_FAN_SPEED, 85, 2);
				else
					(void)oplus_chg_wls_send_msg(wls_dev, WLS_CMD_SET_FAN_SPEED, 55, 2);
			} else if (wls_status->adapter_id <= WLS_ADAPTER_MODEL_7) {
				if (wls_status->adapter_power & WLS_ADAPTER_POWER_MASK)
					(void)oplus_chg_wls_send_msg(wls_dev, WLS_CMD_SET_FAN_SPEED, 85, 2);
				else
					(void)oplus_chg_wls_send_msg(wls_dev, WLS_CMD_SET_FAN_SPEED, 55, 2);
			} else if (wls_status->adapter_id <= WLS_ADAPTER_MODEL_15) {
				if (wls_status->adapter_power & WLS_ADAPTER_POWER_MASK)
					(void)oplus_chg_wls_send_msg(wls_dev, WLS_CMD_SET_FAN_SPEED, 70, 2);
				else
					(void)oplus_chg_wls_send_msg(wls_dev, WLS_CMD_SET_FAN_SPEED, 40, 2);
			} else {
				if (wls_status->adapter_power & WLS_ADAPTER_POWER_MASK)
					(void)oplus_chg_wls_send_msg(wls_dev, WLS_CMD_SET_FAN_SPEED, 80, 2);
				else
					(void)oplus_chg_wls_send_msg(wls_dev, WLS_CMD_SET_FAN_SPEED, 52, 2);
			}

			(void)oplus_chg_wls_send_msg(wls_dev, WLS_CMD_SET_LED_BRIGHTNESS, 100, 2);
		}
	} else {
		if (wls_status->switch_quiet_mode || wls_status->chg_done_quiet_mode) {
			if (wls_status->adapter_id == WLS_ADAPTER_MODEL_0)
				(void)oplus_chg_wls_send_msg(wls_dev, WLS_CMD_SET_QUIET_MODE, 0xff, 2);
			else
				(void)oplus_chg_wls_send_msg(wls_dev, WLS_CMD_SET_FAN_SPEED, 0, 2);

			(void)oplus_chg_wls_send_msg(wls_dev, WLS_CMD_SET_LED_BRIGHTNESS, QUIET_MODE_LED_BRIGHTNESS, 2);
		}
	}
}

static void oplus_chg_wls_check_term_charge(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	int curr_ma;
	int skin_temp;
	int rc;

	if (!wls_status->cep_timeout_adjusted &&
	    !wls_status->fastchg_started &&
	    (wls_status->charge_type == WLS_CHARGE_TYPE_FAST) &&
	    ((wls_status->current_rx_state == OPLUS_CHG_WLS_RX_STATE_EPP) ||
	     (wls_status->current_rx_state == OPLUS_CHG_WLS_RX_STATE_EPP_PLUS) ||
	     (wls_status->current_rx_state == OPLUS_CHG_WLS_RX_STATE_FFC) ||
	     (wls_status->current_rx_state == OPLUS_CHG_WLS_RX_STATE_DONE))) {
		oplus_chg_wls_send_msg(wls_dev, WLS_CMD_SET_CEP_TIMEOUT, 0xff, 2);
	}
	curr_ma = get_client_vote(wls_dev->nor_icl_votable, CHG_DONE_VOTER);
	rc = oplus_chg_wls_get_skin_temp(wls_dev, &skin_temp);
	if (rc < 0) {
		pr_err("can't get skin temp, rc=%d\n", rc);
		skin_temp = 250;
	}

	if (wls_status->chg_done) {
		if (curr_ma <= 0) {
			pr_info("chg done, set icl to %dma\n", WLS_CURR_STOP_CHG_MA);
			vote(wls_dev->nor_icl_votable, CHG_DONE_VOTER, true, WLS_CURR_STOP_CHG_MA, true);
		}
		if (skin_temp < CHARGE_FULL_FAN_THREOD_LO)
			wls_status->chg_done_quiet_mode = true;
		else if (skin_temp > CHARGE_FULL_FAN_THREOD_HI)
			wls_status->chg_done_quiet_mode = false;
	} else {
		if (curr_ma > 0) {
			vote(wls_dev->nor_icl_votable, CHG_DONE_VOTER, false, 0, true);
		}
		wls_status->chg_done_quiet_mode = false;
	}
}

static int oplus_chg_wls_fastchg_restart_check(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	struct oplus_chg_wls_fcc_step *fcc_chg = &wls_dev->wls_fcc_step;
	struct oplus_chg_wls_dynamic_config *dynamic_cfg =&wls_dev->dynamic_config;
	struct oplus_chg_wls_fcc_step *fcc_step = &wls_dev->wls_fcc_step;
	enum oplus_chg_temp_region temp_region;
#ifndef CONFIG_OPLUS_CHG_OOS
	int cool_down = wls_status->cool_down;
#endif
	int batt_temp;
	int real_soc = 100;
	int ibat_ma = 0;
	int rc;

	if (!is_comm_ocm_available(wls_dev)) {
		pr_err("comm mod not fount\n");
		return -ENODEV;
	}

	if (wls_status->fastchg_disable || wls_status->switch_quiet_mode ||
	    !wls_dev->batt_charge_enable)
		return -EPERM;

	rc = oplus_chg_wls_get_batt_temp(wls_dev, &batt_temp);
	if (rc < 0 || (batt_temp >= (fcc_chg->fcc_step[fcc_chg->max_step - 1].high_threshold - BATT_TEMP_HYST))) {
		pr_err("can't get batt temp or batt_temp too high, rc=%d\n");
		return -EPERM;
	}

	rc = oplus_chg_wls_get_ibat(wls_dev, &ibat_ma);
	if (rc < 0) {
		pr_err("can't get ibat, rc=%d\n");
		return rc;
	}

	rc = oplus_chg_wls_get_real_soc(wls_dev, &real_soc);
	if ((rc < 0) || (real_soc >= dynamic_cfg->fcc_step[fcc_step->max_step - 1].max_soc)) {
		pr_err("can't get real soc or soc is too high, rc=%d\n", rc);
		return -EPERM;
	}

	temp_region = oplus_chg_comm_get_temp_region(wls_dev->comm_ocm);
	if ((temp_region != BATT_TEMP_PRE_NORMAL) &&
	    (temp_region != BATT_TEMP_NORMAL)) {
		pr_info("Abnormal battery temperature, can not restart fast charge\n");
		return -EPERM;
	}

	if (is_client_vote_enabled(wls_dev->fastchg_disable_votable, FCC_VOTER) &&
	(batt_temp < (fcc_chg->fcc_step[fcc_chg->max_step - 1].high_threshold - BATT_TEMP_HYST))) {
		vote(wls_dev->fastchg_disable_votable, FCC_VOTER, false, 0, false);
		wls_status->fastchg_level = fcc_chg->max_step - 1;
	}

	if (is_client_vote_enabled(wls_dev->fastchg_disable_votable, BATT_CURR_VOTER) && (ibat_ma < 0))
		vote(wls_dev->fastchg_disable_votable, BATT_CURR_VOTER, false, 0, false);


	if (is_client_vote_enabled(wls_dev->fastchg_disable_votable, STARTUP_CEP_VOTER) &&
	    (wls_status->fastchg_retry_count < 10) &&
	    time_is_before_jiffies(wls_status->fastchg_retry_timer))
		vote(wls_dev->fastchg_disable_votable, STARTUP_CEP_VOTER, false, 0, false);

#ifndef CONFIG_OPLUS_CHG_OOS
	if (is_client_vote_enabled(wls_dev->fastchg_disable_votable, COOL_DOWN_VOTER)) {
		if (!wls_status->led_on) {
			wls_status->cool_down = 0;
			vote(wls_dev->fastchg_disable_votable, COOL_DOWN_VOTER, false, 0, false);
		} else if (cool_down > COOL_DOWN_12V_THR) {
			vote(wls_dev->fastchg_disable_votable, COOL_DOWN_VOTER, false, 0, false);
		}
	}
#endif

	return 0;
}

static int oplus_chg_wls_get_third_adapter_ext_cmd_p_id(struct oplus_chg_wls *wls_dev)
{
	int rc = 0;
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	int try_count = 0;
	char buf[3] = {0};
	int soc = 0, temp = 0;

	if (wls_status->adapter_id != WLS_ADAPTER_THIRD_PARTY)
		return rc;

	if (!wls_status->tx_extern_cmd_done) {
		rc = oplus_chg_wls_send_msg(wls_dev, WLS_CMD_GET_EXTERN_CMD, 0xff, 5);
		if (rc < 0) {
			pr_err("can't extern cmd, rc=%d\n", rc);
			wls_status->tx_extern_cmd_done = false;
			return rc;
		}
		msleep(200);
	}

	if (!wls_status->tx_product_id_done) {
		buf[0] = (wls_dev->wls_phone_id >> 8) & 0xff;
		buf[1] = wls_dev->wls_phone_id & 0xff;
		pr_err("wls_phone_id=0x%x\n", wls_dev->wls_phone_id);
		do {
			rc = oplus_chg_wls_send_data(wls_dev, WLS_CMD_GET_PRODUCT_ID, buf, 5);
			if (rc < 0) {
				if (rc != -EAGAIN)
					try_count++;
				msleep(200);
			}
		} while (rc < 0 && try_count < 2 && wls_status->rx_online);
		if (rc < 0 || !wls_status->rx_online) {
			pr_err("can't get product id, rc=%d\n", rc);
			wls_status->tx_product_id_done = false;
			return rc;
		}
		msleep(200);
	}

	rc = oplus_chg_wls_get_ui_soc(wls_dev, &soc);
	if (rc < 0) {
		pr_err("can't get ui soc, rc=%d\n", rc);
		return rc;
	}
	rc = oplus_chg_wls_get_batt_temp(wls_dev, &temp);
	if (rc < 0) {
		pr_err("can't get batt temp, rc=%d\n", rc);
		return rc;
	}

	buf[0] = (temp >> 8) & 0xff;
	buf[1] = temp & 0xff;
	buf[2] = soc & 0xff;
	pr_info("soc:%d, temp:%d\n", soc, temp);
	oplus_chg_wls_send_data(wls_dev, WLS_CMD_SEND_BATT_TEMP_SOC, buf, 0);

	msleep(1000);
	return rc;
}

static int oplus_chg_wls_get_third_adapter_v_id(struct oplus_chg_wls *wls_dev)
{
	int rc = 0;
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;

	if (wls_status->adapter_id != WLS_ADAPTER_THIRD_PARTY)
		return rc;

	if (!wls_status->verify_by_aes) {
		rc = oplus_chg_wls_send_msg(wls_dev, WLS_CMD_GET_VENDOR_ID, 0xff, 5);
		if (rc < 0) {
			pr_err("can't vendor id, rc=%d\n", rc);
			wls_status->verify_by_aes = false;
			wls_status->adapter_type = WLS_ADAPTER_TYPE_UNKNOWN;
			return rc;
		}
	}

	return rc;
}
static int oplus_chg_wls_rx_handle_state_default(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
#ifdef WLS_SUPPORT_OPLUS_CHG
	struct oplus_chg_wls_dynamic_config *dynamic_cfg =&wls_dev->dynamic_config;
	struct oplus_chg_wls_fcc_step *fcc_step = &wls_dev->wls_fcc_step;
	int real_soc = 100;
#endif
	enum oplus_chg_temp_region temp_region;
	enum oplus_chg_wls_rx_mode rx_mode;
	int rc;

#ifdef WLS_SUPPORT_OPLUS_CHG
	vote(wls_dev->nor_icl_votable, USER_VOTER, true, 500, true);
	if (wls_status->verity_pass)
		rc = oplus_chg_wls_send_msg(wls_dev, WLS_CMD_INDENTIFY_ADAPTER, 0xff, 5);
	else
		rc = -EINVAL;
	if (rc < 0) {
		pr_info("can't get adapter type, rc=%d\n", rc);
		wls_status->wls_type = OPLUS_CHG_WLS_UNKNOWN;
	} else {
		switch (wls_status->adapter_type) {
		case WLS_ADAPTER_TYPE_USB:
		case WLS_ADAPTER_TYPE_NORMAL:
			wls_status->wls_type = OPLUS_CHG_WLS_BPP;
			wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_BPP;
			goto out;
		case WLS_ADAPTER_TYPE_EPP:
			rc = oplus_chg_wls_rx_get_rx_mode(wls_dev->wls_rx, &rx_mode);
			if (rc < 0) {
				pr_err("get rx mode error, rc=%d\n", rc);
				wls_status->wls_type = OPLUS_CHG_WLS_EPP_PLUS;
				wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_EPP_PLUS;
				goto out;
			}
			if (rx_mode == OPLUS_CHG_WLS_RX_MODE_EPP_PLUS) {
				wls_status->wls_type = OPLUS_CHG_WLS_EPP_PLUS;
				wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_EPP_PLUS;
			} else if (rx_mode == OPLUS_CHG_WLS_RX_MODE_EPP) {
				wls_status->wls_type = OPLUS_CHG_WLS_EPP;
				wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_EPP;
			} else {
				wls_status->wls_type = OPLUS_CHG_WLS_BPP;
				wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_BPP;
			}
			goto out;
		case WLS_ADAPTER_TYPE_WARP:
			if (wls_dev->support_fastchg) {
				wls_status->wls_type = OPLUS_CHG_WLS_WARP;
			} else {
				wls_status->epp_working = true;
				wls_status->wls_type = OPLUS_CHG_WLS_EPP_PLUS;
			}
			wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_EPP_PLUS;
			goto out;
		case WLS_ADAPTER_TYPE_SWARP:
		case WLS_ADAPTER_TYPE_PD_65W:
			rc = oplus_chg_wls_get_third_adapter_v_id(wls_dev);
			if (rc < 0)
				goto out;
			if (wls_dev->support_fastchg) {
				if (wls_status->adapter_type == WLS_ADAPTER_TYPE_SWARP)
					wls_status->wls_type = OPLUS_CHG_WLS_SWARP;
				else
					wls_status->wls_type = OPLUS_CHG_WLS_PD_65W;
				if (wls_status->switch_quiet_mode)
					wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_QUIET;
				else if (!wls_dev->batt_charge_enable)
					wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_STOP;
				else
					wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_FAST;
				/*
		 		* The fan of the 30w wireless charger cannot be reset automatically.
		 		* Actively turn on the fan once when wireless charging is connected.
		 		*/
				if (wls_status->adapter_id == WLS_ADAPTER_MODEL_0 &&
				    !wls_status->quiet_mode_init &&
				    !wls_status->switch_quiet_mode)
					(void)oplus_chg_wls_send_msg(wls_dev, WLS_CMD_SET_NORMAL_MODE, 0xff, 2);

				rc = oplus_chg_wls_get_real_soc(wls_dev, &real_soc);
				if ((rc < 0) || (real_soc >= dynamic_cfg->fcc_step[fcc_step->max_step - 1].max_soc)) {
					pr_err("can't get real soc or soc is too high, rc=%d\n", rc);
					wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_DONE;
				}
				if (is_comm_ocm_available(wls_dev)) {
					temp_region = oplus_chg_comm_get_temp_region(wls_dev->comm_ocm);
				} else {
					pr_err("not find comm ocm\n");
					temp_region = BATT_TEMP_COLD;
				}
				if ((temp_region != BATT_TEMP_PRE_NORMAL) &&
				    (temp_region != BATT_TEMP_NORMAL)) {
					pr_err("Abnormal battery temperature, temp_region=%d\n", temp_region);
					wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_DONE;
				}
			} else {
				wls_status->epp_working = true;
				wls_status->wls_type = OPLUS_CHG_WLS_EPP_PLUS;
				wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_EPP_PLUS;
			}
			if (wls_dev->debug_mode)
				wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_DEBUG;
			goto out;
		default:
			wls_status->wls_type = OPLUS_CHG_WLS_UNKNOWN;
		}
	}
#endif
	rc = oplus_chg_wls_rx_get_rx_mode(wls_dev->wls_rx, &rx_mode);
	if (rc < 0) {
		pr_err("get rx mode error, rc=%d\n", rc);
		wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_BPP;
		goto out;
	}

	switch (rx_mode) {
	case OPLUS_CHG_WLS_RX_MODE_BPP:
		wls_status->wls_type = OPLUS_CHG_WLS_BPP;
		wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_BPP;
		break;
	case OPLUS_CHG_WLS_RX_MODE_EPP_5W:
		wls_status->epp_5w = true;
	case OPLUS_CHG_WLS_RX_MODE_EPP:
		wls_status->epp_working = true;
		wls_status->wls_type = OPLUS_CHG_WLS_EPP;
		wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_EPP;
		break;
	case OPLUS_CHG_WLS_RX_MODE_EPP_PLUS:
		wls_status->epp_working = true;
		wls_status->wls_type = OPLUS_CHG_WLS_EPP_PLUS;
		wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_EPP_PLUS;
		break;
	default:
		wls_status->wls_type = OPLUS_CHG_WLS_BPP;
		wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_BPP;
		break;
	}

out:
	if (is_comm_ocm_available(wls_dev))
		oplus_chg_comm_update_config(wls_dev->comm_ocm);
	oplus_chg_wls_config(wls_dev);
	return 0;
}

static int oplus_chg_wls_rx_exit_state_default(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;

	switch (wls_status->target_rx_state) {
	case OPLUS_CHG_WLS_RX_STATE_BPP:
		wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_BPP;
		oplus_chg_wls_nor_set_aicl_enable(wls_dev->wls_nor, true);
		break;
	case OPLUS_CHG_WLS_RX_STATE_EPP:
		wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_EPP;
		oplus_chg_wls_nor_set_aicl_enable(wls_dev->wls_nor, true);
		break;
	case OPLUS_CHG_WLS_RX_STATE_EPP_PLUS:
		wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_EPP_PLUS;
		oplus_chg_wls_nor_set_aicl_enable(wls_dev->wls_nor, true);
		break;
	case OPLUS_CHG_WLS_RX_STATE_FAST:
		wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_FAST;
		vote(wls_dev->nor_icl_votable, USER_VOTER, true, 500, false);
		break;
	case OPLUS_CHG_WLS_RX_STATE_QUIET:
		wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_QUIET;
		vote(wls_dev->nor_icl_votable, USER_VOTER, true, 500, false);
		break;
	case OPLUS_CHG_WLS_RX_STATE_STOP:
		wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_STOP;
		vote(wls_dev->nor_icl_votable, USER_VOTER, true, 0, false);
		break;
	case OPLUS_CHG_WLS_RX_STATE_DEBUG:
		wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_DEBUG;
		vote(wls_dev->nor_icl_votable, USER_VOTER, true, 500, false);
		break;
	case OPLUS_CHG_WLS_RX_STATE_DONE:
		wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_DONE;
		vote(wls_dev->nor_icl_votable, USER_VOTER, true, 500, false);
		break;
	default:
		pr_err("unsupported target status(=%s)\n", oplus_chg_wls_rx_state_text[wls_status->target_rx_state]);
		wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_ERROR;
		wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_ERROR;
		break;
	}

	return 0;
}

static int oplus_chg_wls_rx_enter_state_bpp(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	int icl_max_ma = wls_dev->icl_max_ma[OPLUS_WLS_CHG_MODE_BPP];
	int wait_time_ms = 0;
#ifdef WLS_SUPPORT_OPLUS_CHG
	int rc;
	switch(wls_status->state_sub_step) {
	case 0:
		vote(wls_dev->nor_fcc_votable, USER_VOTER, true, 1500, false);
		vote(wls_dev->nor_icl_votable, USER_VOTER, true, 500, false);
		if (wls_status->adapter_type != WLS_ADAPTER_TYPE_UNKNOWN) {
			(void)oplus_chg_wls_rx_set_vout(wls_dev->wls_rx, 5000, 0);
		} else {
			rc = oplus_chg_wls_send_msg(wls_dev, WLS_CMD_GET_TX_ID, 0xff, 5);
			if (rc < 0)
				pr_info("can't get tx id, it's not op tx\n");
		}
		wait_time_ms = 300;
		wls_status->state_sub_step = 1;
		break;
	case 1:
		if (wls_status->is_op_trx){
			vote(wls_dev->nor_icl_votable, USER_VOTER, true, 650, false);
			oplus_chg_wls_nor_set_aicl_enable(wls_dev->wls_nor, false);
			oplus_chg_wls_nor_set_aicl_enable(wls_dev->wls_nor, true);
			wls_status->state_sub_step = 0;
			wls_status->current_rx_state = OPLUS_CHG_WLS_RX_STATE_BPP;
		}else{
			vote(wls_dev->nor_icl_votable, USER_VOTER, true, 700, false);
			wls_status->state_sub_step = 2;
			wait_time_ms = 300;
		}
		wls_status->adapter_info_cmd_count = 0;
		break;
	case 2:
		vote(wls_dev->nor_icl_votable, USER_VOTER, true, icl_max_ma, true);
		wls_status->state_sub_step = 0;
		wls_status->current_rx_state = OPLUS_CHG_WLS_RX_STATE_BPP;
		break;
	default:
		wls_status->state_sub_step = 0;
		break;
	}
#else
	switch (wls_status->state_sub_step) {
	case 0:
		vote(wls_dev->nor_fcc_votable, USER_VOTER, true, 2000, false);
		wait_time_ms = 300;
		wls_status->state_sub_step = 1;
		break;
	case 1:
		oplus_chg_wls_nor_set_aicl_enable(wls_dev->wls_nor, true);
		vote(wls_dev->nor_icl_votable, USER_VOTER, true, 200, false);
		wait_time_ms = 300;
		wls_status->state_sub_step = 2;
		break;
	case 2:
		vote(wls_dev->nor_icl_votable, USER_VOTER, true, 400, false);
		wait_time_ms = 300;
		wls_status->state_sub_step = 3;
		break;
	case 3:
		vote(wls_dev->nor_icl_votable, USER_VOTER, true, 700, false);
		wait_time_ms = 300;
		wls_status->state_sub_step = 4;
		break;
	case 4:
		vote(wls_dev->nor_icl_votable, USER_VOTER, true, 1000, true);
		wls_status->state_sub_step = 0;
		wls_status->current_rx_state = OPLUS_CHG_WLS_RX_STATE_BPP;
		break;
	default:
		wls_status->state_sub_step = 0;
		break;
	}
#endif
	return wait_time_ms;
}

static int oplus_chg_wls_rx_handle_state_bpp(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	int iout_ma, vout_mv, vrect_mv;
	int wait_time_ms = 4000;
#ifdef WLS_SUPPORT_OPLUS_CHG
	int rc;
	struct oplus_chg_wls_dynamic_config *dynamic_cfg =&wls_dev->dynamic_config;
	struct oplus_chg_wls_fcc_step *fcc_step = &wls_dev->wls_fcc_step;
	int real_soc = 100;
	enum oplus_chg_temp_region temp_region;
	enum oplus_chg_wls_rx_mode rx_mode;

	if (wls_dev->factory_mode && wls_dev->support_get_tx_pwr) {
		(void)oplus_chg_wls_send_msg(wls_dev, WLS_CMD_GET_TX_PWR, 0xff, 0);
		wait_time_ms = 3000;
	} else {
		if (wls_status->adapter_info_cmd_count < 10 && wls_status->verity_pass) {
			(void)oplus_chg_wls_send_msg(wls_dev, WLS_CMD_INDENTIFY_ADAPTER, 0xff, 0);
			wait_time_ms = 1000;
			wls_status->adapter_info_cmd_count++;
		}
	}
#endif

	if (wls_dev->batt_charge_enable) {
		if (!!get_client_vote(wls_dev->nor_out_disable_votable, STOP_VOTER)) {
			vote(wls_dev->nor_icl_votable, STOP_VOTER, false, 0, true);
			vote(wls_dev->nor_out_disable_votable, STOP_VOTER, false, 0, false);
		}
	} else {
		if (!get_client_vote(wls_dev->nor_out_disable_votable, STOP_VOTER)) {
			vote(wls_dev->nor_icl_votable, STOP_VOTER, true, WLS_CURR_STOP_CHG_MA, true);
			vote(wls_dev->nor_out_disable_votable, STOP_VOTER, true, 0, false);
		}
	}

#ifdef WLS_SUPPORT_OPLUS_CHG
	switch (wls_status->adapter_type) {
	case WLS_ADAPTER_TYPE_EPP:
		rc = oplus_chg_wls_rx_get_rx_mode(wls_dev->wls_rx, &rx_mode);
		if (rc < 0) {
			pr_err("get rx mode error, rc=%d\n", rc);
			wls_status->wls_type = OPLUS_CHG_WLS_EPP_PLUS;
			wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_EPP_PLUS;
			break;
		}
		if (rx_mode == OPLUS_CHG_WLS_RX_MODE_EPP_PLUS) {
			wls_status->wls_type = OPLUS_CHG_WLS_EPP_PLUS;
			wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_EPP_PLUS;
		} else {
			wls_status->wls_type = OPLUS_CHG_WLS_EPP;
			wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_EPP;
		}
		break;
	case WLS_ADAPTER_TYPE_WARP:
		if (wls_dev->support_fastchg) {
			wls_status->wls_type = OPLUS_CHG_WLS_WARP;
		} else {
			wls_status->epp_working = true;
			wls_status->wls_type = OPLUS_CHG_WLS_EPP_PLUS;
		}
		wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_EPP_PLUS;
		break;
	case WLS_ADAPTER_TYPE_SWARP:
	case WLS_ADAPTER_TYPE_PD_65W:
		rc = oplus_chg_wls_get_third_adapter_v_id(wls_dev);
		if (rc < 0)
			goto out;
		if (wls_dev->support_fastchg) {
			if (wls_status->adapter_type == WLS_ADAPTER_TYPE_SWARP)
				wls_status->wls_type = OPLUS_CHG_WLS_SWARP;
			else
				wls_status->wls_type = OPLUS_CHG_WLS_PD_65W;
			if (wls_status->switch_quiet_mode)
				wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_QUIET;
			else if (!wls_dev->batt_charge_enable)
				wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_STOP;
			else
				wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_FAST;
			rc = oplus_chg_wls_get_real_soc(wls_dev, &real_soc);
			if ((rc < 0) || (real_soc >= dynamic_cfg->fcc_step[fcc_step->max_step - 1].max_soc)) {
				pr_err("can't get real soc or soc is too high, rc=%d\n", rc);
				wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_DONE;
			}
			if (is_comm_ocm_available(wls_dev)) {
				temp_region = oplus_chg_comm_get_temp_region(wls_dev->comm_ocm);
			} else {
				pr_err("not find comm ocm\n");
				temp_region = BATT_TEMP_COLD;
			}
			if ((temp_region != BATT_TEMP_PRE_NORMAL) &&
			    (temp_region != BATT_TEMP_NORMAL)) {
				pr_err("Abnormal battery temperature, temp_region=%d\n", temp_region);
				wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_DONE;
			}
		} else {
			wls_status->epp_working = true;
			wls_status->wls_type = OPLUS_CHG_WLS_EPP_PLUS;
			wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_EPP_PLUS;
		}
		if (wls_dev->debug_mode)
			wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_DEBUG;
		break;
	default:
		goto out;
	}
	wait_time_ms = 0;
	oplus_chg_wls_config(wls_dev);
out:
#endif
	(void)oplus_chg_wls_nor_skin_check(wls_dev);
	oplus_chg_wls_check_term_charge(wls_dev);

	iout_ma = wls_status->iout_ma;
	vout_mv = wls_status->vout_mv;
	vrect_mv = wls_status->vrect_mv;
	pr_err("wkcs: iout=%d, vout=%d, vrect=%d\n", iout_ma, vout_mv, vrect_mv);

	return wait_time_ms;
}

static int oplus_chg_wls_rx_exit_state_bpp(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;

	switch (wls_status->target_rx_state) {
	case OPLUS_CHG_WLS_RX_STATE_EPP:
		wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_EPP;
		oplus_chg_wls_nor_set_aicl_enable(wls_dev->wls_nor, true);
		break;
	case OPLUS_CHG_WLS_RX_STATE_EPP_PLUS:
		wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_EPP_PLUS;
		oplus_chg_wls_nor_set_aicl_enable(wls_dev->wls_nor, true);
		break;
	case OPLUS_CHG_WLS_RX_STATE_FAST:
		wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_FAST;
		vote(wls_dev->nor_icl_votable, USER_VOTER, true, 500, false);
		break;
	case OPLUS_CHG_WLS_RX_STATE_QUIET:
		wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_QUIET;
		vote(wls_dev->nor_icl_votable, USER_VOTER, true, 500, false);
		break;
	case OPLUS_CHG_WLS_RX_STATE_STOP:
		wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_STOP;
		vote(wls_dev->nor_icl_votable, USER_VOTER, true, 0, false);
		break;
	case OPLUS_CHG_WLS_RX_STATE_DEBUG:
		wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_DEBUG;
		vote(wls_dev->nor_icl_votable, USER_VOTER, true, 500, false);
		break;
	case OPLUS_CHG_WLS_RX_STATE_DONE:
		wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_DONE;
		vote(wls_dev->nor_icl_votable, USER_VOTER, true, 500, false);
		break;
	default:
		pr_err("unsupported target status(=%s)\n", oplus_chg_wls_rx_state_text[wls_status->target_rx_state]);
		wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_ERROR;
		wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_ERROR;
		break;
	}

	return 0;
}

static int oplus_chg_wls_rx_enter_state_epp(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	struct oplus_chg_wls_dynamic_config *dynamic_cfg =&wls_dev->dynamic_config;
	int icl_max_ma = wls_dev->icl_max_ma[OPLUS_WLS_CHG_MODE_EPP];
	int wait_time_ms = 0;
#ifdef WLS_SUPPORT_OPLUS_CHG
	switch(wls_status->state_sub_step) {
	case 0:
		vote(wls_dev->nor_fcc_votable, USER_VOTER, true, 1500, false);
		vote(wls_dev->nor_icl_votable, USER_VOTER, true, 750, false);
		if (wls_status->adapter_type == WLS_ADAPTER_TYPE_WARP ||
		    wls_status->adapter_type == WLS_ADAPTER_TYPE_SWARP ||
		    wls_status->adapter_type == WLS_ADAPTER_TYPE_PD_65W) {
			(void)oplus_chg_wls_rx_set_vout(wls_dev->wls_rx, dynamic_cfg->warp_vol_mv, 0);
			if(wls_dev->support_fastchg)
				(void)oplus_chg_wls_send_msg(wls_dev, WLS_CMD_INTO_FASTCHAGE, 0xff, -1);
			else
				(void)oplus_chg_wls_send_msg(wls_dev, WLS_CMD_INTO_FASTCHAGE, 0x01, -1);
			wls_status->fod_parm_for_fastchg = true;
			(void)oplus_chg_wls_rx_set_vout(wls_dev->wls_rx, dynamic_cfg->warp_vol_mv, 15);

			if (wls_status->pwr_max_mw > 0) {
				vote(wls_dev->nor_icl_votable, RX_MAX_VOTER, true,
				     wls_status->pwr_max_mw * 1000 /dynamic_cfg->warp_vol_mv, false);
			}
		}
		wait_time_ms = 300;
		wls_status->state_sub_step = 1;
		break;
	case 1:
		if(wls_status->epp_5w || wls_status->vout_mv < 8000)
			vote(wls_dev->nor_icl_votable, USER_VOTER, true, 450, true);
		else if (wls_status->vout_mv > 9000)
			vote(wls_dev->nor_icl_votable, USER_VOTER, true, icl_max_ma, true);
		wls_status->state_sub_step = 0;
		wls_status->current_rx_state = OPLUS_CHG_WLS_RX_STATE_EPP;
		break;
	default:
		wls_status->state_sub_step = 0;
		break;
	}
#else
switch(wls_status->state_sub_step) {
    case 0:
        vote(wls_dev->nor_fcc_votable, USER_VOTER, true, 2000, false);
        wait_time_ms = 300;
        wls_status->state_sub_step = 1;
        break;
    case 1:
        oplus_chg_wls_nor_set_aicl_enable(wls_dev->wls_nor, true);
        vote(wls_dev->nor_icl_votable, USER_VOTER, true, 200, false);
        wait_time_ms = 300;
        wls_status->state_sub_step = 2;
        break;
    case 2:
        vote(wls_dev->nor_icl_votable, USER_VOTER, true, 500, false);
        wait_time_ms = 300;
        wls_status->state_sub_step = 3;
        break;
    case 3:
        vote(wls_dev->nor_icl_votable, USER_VOTER, true, 900, false);
		wls_status->state_sub_step = 0;
		wls_status->current_rx_state = OPLUS_CHG_WLS_RX_STATE_EPP;
        break;
	default:
		wls_status->state_sub_step = 0;
		break;
	}
#endif
	return wait_time_ms;
}

static int oplus_chg_wls_rx_handle_state_epp(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	int icl_max_ma = wls_dev->icl_max_ma[OPLUS_WLS_CHG_MODE_EPP];
	int iout_ma, icl_ma, vout_mv, vrect_mv;
	int nor_input_curr_ma;
	int wait_time_ms = 4000;
	int rc = 0;

#ifdef WLS_SUPPORT_OPLUS_CHG
	if (wls_dev->factory_mode && wls_dev->support_get_tx_pwr) {
		(void)oplus_chg_wls_send_msg(wls_dev, WLS_CMD_GET_TX_PWR, 0xff, 0);
		wait_time_ms = 3000;
	} else {
		oplus_chg_wls_check_quiet_mode(wls_dev);
	}
#endif

	if (wls_dev->batt_charge_enable) {
		if (!!get_client_vote(wls_dev->nor_out_disable_votable, STOP_VOTER)) {
			vote(wls_dev->nor_icl_votable, STOP_VOTER, false, 0, true);
			vote(wls_dev->nor_out_disable_votable, STOP_VOTER, false, 0, false);
		}
	} else {
		if (!get_client_vote(wls_dev->nor_out_disable_votable, STOP_VOTER)) {
			vote(wls_dev->nor_icl_votable, STOP_VOTER, true, WLS_CURR_STOP_CHG_MA, true);
			vote(wls_dev->nor_out_disable_votable, STOP_VOTER, true, 0, false);
		}
	}

	if(wls_status->epp_5w || wls_status->vout_mv < 8000)
		vote(wls_dev->nor_icl_votable, USER_VOTER, true, 450, true);
	else if (wls_status->vout_mv > 9000)
		vote(wls_dev->nor_icl_votable, USER_VOTER, true, icl_max_ma, true);

	(void)oplus_chg_wls_nor_skin_check(wls_dev);
	oplus_chg_wls_check_term_charge(wls_dev);

	if ((wls_status->adapter_type != WLS_ADAPTER_TYPE_UNKNOWN) &&
	     !wls_status->verity_started) {
		icl_ma = get_effective_result(wls_dev->nor_icl_votable);
		rc = oplus_chg_wls_nor_get_input_curr(wls_dev->wls_nor, &nor_input_curr_ma);
		if (rc < 0)
			nor_input_curr_ma = wls_status->iout_ma;
		if (icl_ma - nor_input_curr_ma < 300)
			schedule_delayed_work(&wls_dev->wls_verity_work, msecs_to_jiffies(500));
	}

	iout_ma = wls_status->iout_ma;
	vout_mv = wls_status->vout_mv;
	vrect_mv = wls_status->vrect_mv;
	pr_err("wkcs: iout=%d, vout=%d, vrect=%d\n", iout_ma, vout_mv, vrect_mv);
	return wait_time_ms;
}

static int oplus_chg_wls_rx_exit_state_epp(struct oplus_chg_wls *wls_dev)
{
	// struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	// int rc;

	return 0;
}

static int oplus_chg_wls_rx_enter_state_epp_plus(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	struct oplus_chg_wls_dynamic_config *dynamic_cfg =&wls_dev->dynamic_config;
	int icl_max_ma = wls_dev->icl_max_ma[OPLUS_WLS_CHG_MODE_EPP_PLUS];
	int wait_time_ms = 0;
	int rc = 0;

#ifdef WLS_SUPPORT_OPLUS_CHG
	switch(wls_status->state_sub_step) {
	case 0:
		vote(wls_dev->nor_fcc_votable, USER_VOTER, true, 2000, false);
		vote(wls_dev->nor_icl_votable, USER_VOTER, true, 750, false);
		if (wls_status->adapter_type == WLS_ADAPTER_TYPE_WARP ||
		    wls_status->adapter_type == WLS_ADAPTER_TYPE_SWARP ||
		    wls_status->adapter_type == WLS_ADAPTER_TYPE_PD_65W) {
			(void)oplus_chg_wls_rx_set_vout(wls_dev->wls_rx, dynamic_cfg->warp_vol_mv, 0);
			if(wls_dev->support_fastchg)
				(void)oplus_chg_wls_send_msg(wls_dev, WLS_CMD_INTO_FASTCHAGE, 0xff, -1);
			else
				(void)oplus_chg_wls_send_msg(wls_dev, WLS_CMD_INTO_FASTCHAGE, 0x01, -1);
			wls_status->fod_parm_for_fastchg = true;
			(void)oplus_chg_wls_rx_set_vout(wls_dev->wls_rx, dynamic_cfg->warp_vol_mv, 15);
			if (wls_status->pwr_max_mw > 0) {
				vote(wls_dev->nor_icl_votable, RX_MAX_VOTER, true,
				     wls_status->pwr_max_mw * 1000 / dynamic_cfg->warp_vol_mv, false);
			}
		}
		wait_time_ms = 300;
		wls_status->state_sub_step = 1;
		break;
	case 1:
		if(wls_status->vout_mv < 8000) {
			vote(wls_dev->nor_icl_votable, USER_VOTER, true, 500, false);
			wls_status->state_sub_step = 0;
			wls_status->current_rx_state = OPLUS_CHG_WLS_RX_STATE_EPP_PLUS;
		} else {
			vote(wls_dev->nor_icl_votable, USER_VOTER, true, 900, false);
			wait_time_ms = 300;
			wls_status->state_sub_step = 2;
		}
		if (wls_status->adapter_id == WLS_ADAPTER_THIRD_PARTY) {
			rc = oplus_chg_wls_get_third_adapter_ext_cmd_p_id(wls_dev);
			if (rc < 0) {
				if (!wls_status->rx_online)
					return 0;
				pr_err("get product id fail\n");
				wls_status->online_keep = true;
				vote(wls_dev->rx_disable_votable, VERITY_VOTER, true, 1, false);
				schedule_delayed_work(&wls_dev->rx_verity_restore_work, msecs_to_jiffies(500));
				wait_time_ms = 100;
			}
		}
		break;
	case 2:
		vote(wls_dev->nor_icl_votable, USER_VOTER, true, icl_max_ma, false);
		wls_status->state_sub_step = 0;
		wls_status->current_rx_state = OPLUS_CHG_WLS_RX_STATE_EPP_PLUS;
		break;
	default:
		wls_status->state_sub_step = 0;
		break;
	}
#else
switch(wls_status->state_sub_step) {
    case 0:
        vote(wls_dev->nor_fcc_votable, USER_VOTER, true, 3000, false);
        wait_time_ms = 300;
        wls_status->state_sub_step = 1;
        break;
    case 1:
        oplus_chg_wls_nor_set_aicl_enable(wls_dev->wls_nor, true);
        vote(wls_dev->nor_icl_votable, USER_VOTER, true, 200, false);
        wait_time_ms = 300;
        wls_status->state_sub_step = 2;
        break;
    case 2:
        vote(wls_dev->nor_icl_votable, USER_VOTER, true, 500, false);
        wait_time_ms = 300;
        wls_status->state_sub_step = 3;
        break;
    case 3:
        vote(wls_dev->nor_icl_votable, USER_VOTER, true, 900, false);
        wait_time_ms = 300;
        wls_status->state_sub_step = 4;
        break;
    case 4:
        vote(wls_dev->nor_icl_votable, USER_VOTER, true, 1250, false);
		wls_status->state_sub_step = 0;
		wls_status->current_rx_state = OPLUS_CHG_WLS_RX_STATE_EPP_PLUS;
        break;
	default:
		wls_status->state_sub_step = 0;
		break;
	}
#endif
	return wait_time_ms;
}

static int oplus_chg_wls_rx_handle_state_epp_plus(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	int icl_max_ma = wls_dev->icl_max_ma[OPLUS_WLS_CHG_MODE_EPP_PLUS];
	int iout_ma, icl_ma, vout_mv, vrect_mv;
	int nor_input_curr_ma;
	int wait_time_ms = 4000;
	int rc = 0;

#ifdef WLS_SUPPORT_OPLUS_CHG
	if (wls_dev->factory_mode && wls_dev->support_get_tx_pwr) {
		(void)oplus_chg_wls_send_msg(wls_dev, WLS_CMD_GET_TX_PWR, 0xff, 0);
		wait_time_ms = 3000;
	} else {
		oplus_chg_wls_check_quiet_mode(wls_dev);
	}
#endif

	if (wls_dev->batt_charge_enable) {
		if (!!get_client_vote(wls_dev->nor_out_disable_votable, STOP_VOTER)) {
			vote(wls_dev->nor_icl_votable, STOP_VOTER, false, 0, true);
			vote(wls_dev->nor_out_disable_votable, STOP_VOTER, false, 0, false);
		}
	} else {
		if (!get_client_vote(wls_dev->nor_out_disable_votable, STOP_VOTER)) {
			vote(wls_dev->nor_icl_votable, STOP_VOTER, true, WLS_CURR_STOP_CHG_MA, true);
			vote(wls_dev->nor_out_disable_votable, STOP_VOTER, true, 0, false);
		}
	}

	if (wls_status->vout_mv > 9000)
		vote(wls_dev->nor_icl_votable, USER_VOTER, true, icl_max_ma, true);
	else if (wls_status->vout_mv < 8000)
		vote(wls_dev->nor_icl_votable, USER_VOTER, true, 500, true);

	(void)oplus_chg_wls_nor_skin_check(wls_dev);
	oplus_chg_wls_check_term_charge(wls_dev);

	if ((wls_status->adapter_type != WLS_ADAPTER_TYPE_UNKNOWN) &&
	     !wls_status->verity_started) {
		icl_ma = get_effective_result(wls_dev->nor_icl_votable);
		rc = oplus_chg_wls_nor_get_input_curr(wls_dev->wls_nor, &nor_input_curr_ma);
		if (rc < 0)
			nor_input_curr_ma = wls_status->iout_ma;
		if (icl_ma - nor_input_curr_ma < 300)
			schedule_delayed_work(&wls_dev->wls_verity_work, msecs_to_jiffies(500));
	}

	iout_ma = wls_status->iout_ma;
	vout_mv = wls_status->vout_mv;
	vrect_mv = wls_status->vrect_mv;
	pr_err("wkcs: iout=%d, vout=%d, vrect=%d\n", iout_ma, vout_mv, vrect_mv);
	return wait_time_ms;
}

static int oplus_chg_wls_rx_exit_state_epp_plus(struct oplus_chg_wls *wls_dev)
{
	// struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	// int rc;

	return 0;
}

#define CP_OPEN_OFFSET 100
static int oplus_chg_wls_rx_enter_state_fast(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	struct oplus_chg_wls_fcc_step *fcc_chg = &wls_dev->wls_fcc_step;
	struct oplus_chg_wls_dynamic_config *dynamic_cfg =&wls_dev->dynamic_config;
	struct oplus_chg_wls_fcc_step *fcc_step = &wls_dev->wls_fcc_step;
	union oplus_chg_mod_propval pval;
	int vbat_mv, vout_mv;
	static int curr_err_count;
	int delay_ms = 0;
	int real_soc;
	int iout_max_ma;
	int i;
	int scale_factor;
	int rc;

	if (wls_status->switch_quiet_mode) {
		wls_status->current_rx_state = OPLUS_CHG_WLS_RX_STATE_FAST;
		wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_QUIET;
		return 0;
	}

	if (!wls_dev->batt_charge_enable) {
		wls_status->current_rx_state = OPLUS_CHG_WLS_RX_STATE_FAST;
		wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_STOP;
		return 0;
	} else {
		if (!!get_client_vote(wls_dev->nor_out_disable_votable, STOP_VOTER)) {
			vote(wls_dev->nor_icl_votable, STOP_VOTER, false, 0, true);
			vote(wls_dev->nor_out_disable_votable, STOP_VOTER, false, 0, false);
		}
	}

	rc = oplus_chg_wls_get_real_soc(wls_dev, &real_soc);
	if ((rc < 0) || (real_soc >= dynamic_cfg->fcc_step[fcc_step->max_step - 1].max_soc)) {
		pr_err("can't get real soc or soc is too high, rc=%d\n", rc);
		wls_status->current_rx_state = OPLUS_CHG_WLS_RX_STATE_FAST;
		wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_DONE;
		return 0;
	}

	rc = oplus_chg_wls_get_vbat(wls_dev, &vbat_mv);
	if (rc < 0) {
		pr_err("can't get vbat, rc=%d\n");
		delay_ms = 100;
		return delay_ms;
	}

	pr_err("state_sub_step=%d\n", wls_status->state_sub_step);

	switch(wls_status->state_sub_step) {
	case OPLUS_CHG_WLS_FAST_SUB_STATE_INIT:
		vote(wls_dev->nor_icl_votable, USER_VOTER, true, 750, false);
		(void)oplus_chg_wls_rx_set_vout(wls_dev->wls_rx, WLS_VOUT_FASTCHG_INIT_MV, 0);
		(void)oplus_chg_wls_send_msg(wls_dev, WLS_CMD_INTO_FASTCHAGE, 0xff, -1);
		vote(wls_dev->nor_icl_votable, USER_VOTER, true, WLS_CURR_WAIT_FAST_MA, false);
		wls_status->fod_parm_for_fastchg = true;
		wls_status->state_sub_step = OPLUS_CHG_WLS_FAST_SUB_STATE_WAIT_FAST;
		oplus_chg_wls_get_verity_data(wls_dev);
		delay_ms = 100;
		break;
	case OPLUS_CHG_WLS_FAST_SUB_STATE_WAIT_FAST:
		if (wls_status->charge_type != WLS_CHARGE_TYPE_FAST)
			return 500;
		(void)oplus_chg_wls_rx_set_vout(wls_dev->wls_rx, WLS_VOUT_FASTCHG_INIT_MV, -1);
		(void)oplus_chg_wls_rx_set_dcdc_enable(wls_dev->wls_rx);
		(void)oplus_chg_wls_set_cp_boost_enable(wls_dev, true);
		curr_err_count = 0;
		if (wls_status->rx_online && wls_status->adapter_id == WLS_ADAPTER_THIRD_PARTY) {
			rc = oplus_chg_wls_get_third_adapter_ext_cmd_p_id(wls_dev);
			if (rc < 0) {
				if (!wls_status->rx_online)
					return 0;
				pr_err("get product id fail\n");
				wls_status->online_keep = true;
				vote(wls_dev->rx_disable_votable, VERITY_VOTER, true, 1, false);
				schedule_delayed_work(&wls_dev->rx_verity_restore_work, msecs_to_jiffies(500));
				return 500;
			}
		}
		if (wls_status->iout_ma < 250) {
			wls_status->state_sub_step = OPLUS_CHG_WLS_FAST_SUB_STATE_WAIT_IOUT;
			curr_err_count++;
			delay_ms = 100;
		} else {
			wls_status->state_sub_step = OPLUS_CHG_WLS_FAST_SUB_STATE_WAIT_VOUT;
			curr_err_count = 0;
			delay_ms = 0;
		}
		break;
	case OPLUS_CHG_WLS_FAST_SUB_STATE_WAIT_IOUT:
		if (wls_status->iout_ma < 250) {
			curr_err_count++;
			delay_ms = 100;
		} else {
			curr_err_count = 0;
			delay_ms = 0;
			wls_status->state_sub_step = OPLUS_CHG_WLS_FAST_SUB_STATE_WAIT_VOUT;
		}
		if (curr_err_count > 100) {
			curr_err_count = 0;
			wls_status->state_sub_step = 0;
			vote(wls_dev->fastchg_disable_votable, CURR_ERR_VOTER, true, 1, false);
			wls_status->current_rx_state = OPLUS_CHG_WLS_RX_STATE_FAST;
			wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_DONE;
			delay_ms = 0;
		}
		break;
	case OPLUS_CHG_WLS_FAST_SUB_STATE_WAIT_VOUT:
		rc = oplus_chg_wls_rx_set_vout_step(wls_dev->wls_rx, 4 * vbat_mv + CP_OPEN_OFFSET, 1000, 50);
		if (rc < 0) {
			pr_err("can't set vout to %d, rc=%d\n", 4 * vbat_mv + CP_OPEN_OFFSET, rc);
			wls_status->state_sub_step = 0;
			wls_status->fastchg_retry_count++;
			wls_status->fastchg_retry_timer = jiffies + (unsigned long)(300 * HZ);
			vote(wls_dev->fastchg_disable_votable, STARTUP_CEP_VOTER, true, 1, false);
			wls_status->current_rx_state = OPLUS_CHG_WLS_RX_STATE_FAST;
			wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_DONE;
			delay_ms = 0;
			break;
		}
		wls_status->state_sub_step = OPLUS_CHG_WLS_FAST_SUB_STATE_CHECK_IOUT;
		delay_ms = 0;
		break;
	case OPLUS_CHG_WLS_FAST_SUB_STATE_CHECK_IOUT:
		rc = oplus_chg_wls_rx_get_vout(wls_dev->wls_rx, &vout_mv);
		if (rc < 0) {
			pr_err("can't get vout, rc=%d\n", rc);
			delay_ms = 100;
			break;
		}
		if (vout_mv < vbat_mv * 4 + CP_OPEN_OFFSET) {
			pr_err("rx vout(=%d) < %d, retry\n", vout_mv, vbat_mv * 4 + CP_OPEN_OFFSET);
			(void)oplus_chg_wls_rx_set_vout_step(wls_dev->wls_rx, 4 * vbat_mv + CP_OPEN_OFFSET + 100, 1000, 10);
			delay_ms = 0;
			break;
		}
		wls_status->state_sub_step = OPLUS_CHG_WLS_FAST_SUB_STATE_START;
		delay_ms = 0;
		break;
	case OPLUS_CHG_WLS_FAST_SUB_STATE_START:
		(void)oplus_chg_wls_fast_set_enable(wls_dev->wls_fast, true);
		(void)oplus_chg_wls_fast_start(wls_dev->wls_fast);
		rc = oplus_chg_wls_get_real_soc(wls_dev, &real_soc);
		if (rc < 0) {
			pr_err("can't get real soc, rc=%d\n");
			goto err;
		}
		if (is_batt_ocm_available(wls_dev)) {
			rc = oplus_chg_mod_get_property(wls_dev->batt_ocm,
							OPLUS_CHG_PROP_CELL_NUM,
							&pval);
			if (rc != 0)
				scale_factor = 1;
			else
				scale_factor =
					WLS_RX_VOL_MAX_MV / 5000 / pval.intval;
		} else {
			scale_factor = 1;
		}
		if ((wls_status->wls_type == OPLUS_CHG_WLS_SWARP) ||
		    (wls_status->wls_type == OPLUS_CHG_WLS_PD_65W)) {
			wls_status->fastchg_ibat_max_ma = dynamic_cfg->fastchg_curr_max_ma * scale_factor;
			for(i = 0; i < fcc_step->max_step; i++) {
				if (real_soc < dynamic_cfg->fcc_step[i].max_soc) {
					iout_max_ma = dynamic_cfg->fcc_step[i].curr_ma;
					break;
				}
			}
			if (i >= fcc_step->max_step) {
				pr_err("soc is too high, exit fast charge\n");
				goto err;
			}
			if(iout_max_ma > WLS_FASTCHG_CURR_45W_MAX_MA && wls_status->wls_type == OPLUS_CHG_WLS_SWARP)
				iout_max_ma = WLS_FASTCHG_CURR_45W_MAX_MA;
			vote(wls_dev->fcc_votable, MAX_VOTER, true, iout_max_ma, false);
		} else {
			wls_status->fastchg_ibat_max_ma = WLS_FASTCHG_CURR_15W_MAX_MA * scale_factor;;
			vote(wls_dev->fcc_votable, MAX_VOTER, true, WLS_FASTCHG_CURR_15W_MAX_MA, false);
		}
		wls_status->state_sub_step = 0;
		wls_status->fastchg_started = true;
		wls_status->fastchg_level_init_temp = 0;
		wls_status->wait_cep_stable = true;
		wls_status->fastchg_retry_count = 0;
		wls_status->iout_ma_conunt = 0;
		fcc_chg->fcc_wait_timeout = jiffies;
		if (!wls_status->fastchg_restart) {
			wls_status->fastchg_level = 0;
			oplus_chg_wls_fast_fcc_param_init(wls_dev);
			wls_status->fastchg_restart = true;
		} else {
			vote(wls_dev->fcc_votable, FCC_VOTER, true,
				fcc_chg->fcc_step[wls_status->fastchg_level].curr_ma, false);
		}
		wls_status->current_rx_state = OPLUS_CHG_WLS_RX_STATE_FAST;
		vote(wls_dev->nor_fv_votable, USER_VOTER, true,
			dynamic_cfg->batt_vol_max_mv + 100, false);
		delay_ms = 0;
		break;
	}

	return delay_ms;

err:
	wls_status->current_rx_state = OPLUS_CHG_WLS_RX_STATE_FAST;
	wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_DONE;
	return 0;
}

static int oplus_chg_wls_rx_handle_state_fast(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	int wait_time_ms = 4000;
	int rc;

	if ((wls_status->iout_ma > 500) && (wls_dev->wls_nor->icl_set_ma != 0)) {
		vote(wls_dev->nor_icl_votable, USER_VOTER, true, 0, false);
	}

	if (wls_dev->factory_mode && wls_dev->support_get_tx_pwr) {
		(void)oplus_chg_wls_send_msg(wls_dev, WLS_CMD_GET_TX_PWR, 0xff, 0);
		wait_time_ms = 3000;
	}

	if (wls_dev->force_type == OPLUS_CHG_WLS_FORCE_TYPE_AUTO)
		return wait_time_ms;

	if (wls_status->switch_quiet_mode) {
		wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_QUIET;
		return 0;
	}

	if (!wls_dev->batt_charge_enable) {
		wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_STOP;
		return 0;
	}

	if (!wls_status->verity_started &&
	    (wls_status->fastchg_target_curr_ma - wls_status->iout_ma <
	     WLS_CURR_ERR_MIN_MA)) {
		schedule_delayed_work(&wls_dev->wls_verity_work, msecs_to_jiffies(500));
	}

	rc = oplus_chg_wls_fast_temp_check(wls_dev);
	if (rc < 0) {
		pr_info("exit wls fast charge, exit_code=%d\n", rc);
		wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_DONE;
		return 0;
	}

	rc = oplus_chg_wls_fast_cep_check(wls_dev);
	if (rc < 0) {
		pr_info("exit wls fast charge by cep check\n");
		wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_DONE;
		return 0;
	}

	rc = oplus_chg_wls_fast_ibat_check(wls_dev);
	if (rc < 0) {
		pr_info("exit wls fast charge by ibat check\n");
		wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_DONE;
		return 0;
	}

	rc = oplus_chg_wls_fast_iout_check(wls_dev);
	if (rc < 0) {
		pr_info("exit wls fast charge by iout check\n");
		wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_DONE;
		return 0;
	}

	oplus_chg_wls_fast_skin_temp_check(wls_dev);
#ifndef CONFIG_OPLUS_CHG_OOS
	rc = oplus_chg_wls_fast_cool_down_check(wls_dev);
	if (rc < 0) {
		pr_info("exit wls fast charge by cool down\n");
		wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_DONE;
		return 0;
	}
#endif

	if (wls_status->ffc_check) {
		wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_FFC;
		wls_status->ffc_check = false;
		return 0;
	}
	oplus_chg_wls_exchange_batt_mesg(wls_dev);

	return wait_time_ms;
}

static int oplus_chg_wls_rx_exit_state_fast(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	// int rc;

	vote(wls_dev->fcc_votable, EXIT_VOTER, true, WLS_FASTCHG_CURR_EXIT_MA, false);
	while (wls_status->iout_ma > (WLS_FASTCHG_CURR_EXIT_MA + WLS_FASTCHG_CURR_ERR_MA)) {
		msleep(500);
	}
	wls_status->fastchg_started = false;

	if (is_comm_ocm_available(wls_dev))
		oplus_chg_comm_update_config(wls_dev->comm_ocm);

	(void)oplus_chg_wls_rx_set_fod_parm(wls_dev->wls_rx,
		oplus_chg_wls_disable_fod_parm, WLS_FOD_PARM_LENGTH);

	switch (wls_status->target_rx_state) {
	case OPLUS_CHG_WLS_RX_STATE_DONE:
		wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_DONE;
		vote(wls_dev->nor_icl_votable, USER_VOTER, true, 300, false);
		(void)oplus_chg_wls_fast_set_enable(wls_dev->wls_fast, false);
		(void)oplus_chg_wls_rx_set_vout_step(wls_dev->wls_rx,
			WLS_VOUT_FASTCHG_INIT_MV, 1000, -1);
		vote(wls_dev->fcc_votable, EXIT_VOTER, false, 0, false);
		break;
	case OPLUS_CHG_WLS_RX_STATE_QUIET:
		wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_QUIET;
		vote(wls_dev->nor_icl_votable, USER_VOTER, true, 300, false);
		(void)oplus_chg_wls_fast_set_enable(wls_dev->wls_fast, false);
		(void)oplus_chg_wls_rx_set_vout_step(wls_dev->wls_rx,
			WLS_VOUT_FASTCHG_INIT_MV, 1000, -1);
		vote(wls_dev->fcc_votable, EXIT_VOTER, false, 0, false);
		break;
	case OPLUS_CHG_WLS_RX_STATE_STOP:
		wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_STOP;
		vote(wls_dev->nor_icl_votable, USER_VOTER, true, 300, false);
		(void)oplus_chg_wls_fast_set_enable(wls_dev->wls_fast, false);
		(void)oplus_chg_wls_rx_set_vout_step(wls_dev->wls_rx,
			WLS_VOUT_FASTCHG_INIT_MV, 1000, -1);
		vote(wls_dev->fcc_votable, EXIT_VOTER, false, 0, false);
		break;
	case OPLUS_CHG_WLS_RX_STATE_DEBUG:
		wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_DEBUG;
		vote(wls_dev->nor_icl_votable, USER_VOTER, true, 300, false);
		(void)oplus_chg_wls_fast_set_enable(wls_dev->wls_fast, false);
		(void)oplus_chg_wls_rx_set_vout_step(wls_dev->wls_rx,
			WLS_VOUT_FASTCHG_INIT_MV, 1000, -1);
		vote(wls_dev->fcc_votable, EXIT_VOTER, false, 0, false);
		break;
	case OPLUS_CHG_WLS_RX_STATE_FFC:
		wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_FFC;
		vote(wls_dev->nor_icl_votable, USER_VOTER, true, 300, false);
		(void)oplus_chg_wls_fast_set_enable(wls_dev->wls_fast, false);
		(void)oplus_chg_wls_rx_set_vout_step(wls_dev->wls_rx,
			WLS_VOUT_FASTCHG_INIT_MV, 1000, -1);
		vote(wls_dev->fcc_votable, EXIT_VOTER, false, 0, false);
		break;
	default:
		pr_err("unsupported target status(=%s)\n",
			oplus_chg_wls_rx_state_text[wls_status->target_rx_state]);
		wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_ERROR;
		wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_ERROR;
		break;
	}
	return 0;
}

static int oplus_chg_wls_rx_enter_state_ffc(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	static unsigned long stop_time;
	int wait_time_ms = 0;
	int rc;

	switch(wls_status->state_sub_step) {
	case 0:
		vote(wls_dev->nor_out_disable_votable, FFC_VOTER, true, 1, false);
		stop_time = jiffies + msecs_to_jiffies(30000);
		wait_time_ms = (int)jiffies_to_msecs(stop_time - jiffies);
		wls_status->fod_parm_for_fastchg = false;

		if(wls_dev->static_config.fastchg_12V_fod_enable)
			(void)oplus_chg_wls_rx_set_fod_parm(wls_dev->wls_rx,
				wls_dev->static_config.fastchg_fod_parm_12V, WLS_FOD_PARM_LENGTH);
		else
			(void)oplus_chg_wls_rx_set_fod_parm(wls_dev->wls_rx,
				oplus_chg_wls_disable_fod_parm, WLS_FOD_PARM_LENGTH);

		oplus_chg_wls_get_verity_data(wls_dev);
		wls_status->state_sub_step = 1;
		break;
	case 1:
		if (!time_is_before_jiffies(stop_time)) {
			wait_time_ms = (int)jiffies_to_msecs(stop_time - jiffies);
			break;
		}
		vote(wls_dev->nor_out_disable_votable, FFC_VOTER, false, 0, false);
		if (is_comm_ocm_available(wls_dev)) {
			rc = oplus_chg_comm_switch_ffc(wls_dev->comm_ocm);
			if (rc < 0) {
				pr_err("can't switch to ffc charge, rc=%d\n", rc);
				wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_DONE;
			}
		} else {
			pr_err("comm mod not found, can't switch to ffc charge\n");
			wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_DONE;
		}
		wls_status->current_rx_state = OPLUS_CHG_WLS_RX_STATE_FFC;
		wls_status->state_sub_step = 0;
		break;
	default:
		wls_status->state_sub_step = 0;
		break;
	}

	return wait_time_ms;
}

static int oplus_chg_wls_rx_handle_state_ffc(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	int iout_ma, vout_mv, vrect_mv;
	int rc;

	oplus_chg_wls_check_quiet_mode(wls_dev);

	if (wls_dev->batt_charge_enable) {
		if (!!get_client_vote(wls_dev->nor_out_disable_votable, STOP_VOTER)) {
			vote(wls_dev->nor_icl_votable, STOP_VOTER, false, 0, true);
			vote(wls_dev->nor_out_disable_votable, STOP_VOTER, false, 0, false);
		}
	} else {
		if (!get_client_vote(wls_dev->nor_out_disable_votable, STOP_VOTER)) {
			vote(wls_dev->nor_icl_votable, STOP_VOTER, true, WLS_CURR_STOP_CHG_MA, true);
			vote(wls_dev->nor_out_disable_votable, STOP_VOTER, true, 0, false);
		}
	}

	if (is_comm_ocm_available(wls_dev)) {
		rc = oplus_chg_comm_check_ffc(wls_dev->comm_ocm);
		if (rc < 0) {
			pr_err("ffc check error, exit ffc charge, rc=%d\n", rc);
			wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_DONE;
			return 0;
		} else if (rc > 0) {
			pr_info("ffc done\n");
			wls_status->ffc_done = true;
			wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_DONE;
			return 0;
		}
	} else {
		pr_err("comm mod not found, exit ffc charge\n");
		wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_DONE;
		return 0;
	}

	iout_ma = wls_status->iout_ma;
	vout_mv = wls_status->vout_mv;
	vrect_mv = wls_status->vrect_mv;
	pr_err("wkcs: iout=%d, vout=%d, vrect=%d\n", iout_ma, vout_mv, vrect_mv);

	if (!wls_status->verity_started)
		schedule_delayed_work(&wls_dev->wls_verity_work, msecs_to_jiffies(500));

	return 4000;
}

static int oplus_chg_wls_rx_exit_state_ffc(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	// int rc;

	switch (wls_status->target_rx_state) {
	case OPLUS_CHG_WLS_RX_STATE_DONE:
		wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_DONE;
		break;
	case OPLUS_CHG_WLS_RX_STATE_QUIET:
		wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_QUIET;
		break;
	case OPLUS_CHG_WLS_RX_STATE_STOP:
		wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_STOP;
		break;
	case OPLUS_CHG_WLS_RX_STATE_DEBUG:
		break;
	default:
		pr_err("unsupported target status(=%s)\n", oplus_chg_wls_rx_state_text[wls_status->target_rx_state]);
		wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_ERROR;
		wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_ERROR;
		break;
	}

	return 0;
}

static int oplus_chg_wls_rx_enter_state_done(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	int rc;

	switch(wls_status->state_sub_step) {
	case 0:
		oplus_chg_wls_get_verity_data(wls_dev);
		(void)oplus_chg_wls_rx_set_vout(wls_dev->wls_rx,
			WLS_VOUT_FASTCHG_INIT_MV, 0);
		wls_status->state_sub_step = 1;
	case 1:
		if (wls_status->charge_type != WLS_CHARGE_TYPE_FAST) {
			rc = oplus_chg_wls_send_msg(wls_dev, WLS_CMD_INTO_FASTCHAGE, 0xff, 0);
			if (rc < 0) {
				pr_err("send WLS_CMD_INTO_FASTCHAGE err, rc=%d\n", rc);
				return 100;
			}
			wls_status->fod_parm_for_fastchg = false;
			return 1000;
		}else{
			if (wls_status->adapter_id == WLS_ADAPTER_THIRD_PARTY) {
				rc = oplus_chg_wls_get_third_adapter_ext_cmd_p_id(wls_dev);
				if (rc < 0) {
					if (!wls_status->rx_online)
						return 0;
					pr_err("get product id fail\n");
					wls_status->online_keep = true;
					vote(wls_dev->rx_disable_votable, VERITY_VOTER, true, 1, false);
					schedule_delayed_work(&wls_dev->rx_verity_restore_work, msecs_to_jiffies(500));
					return 100;
				}
			}

			if(wls_dev->static_config.fastchg_12V_fod_enable)
				(void)oplus_chg_wls_rx_set_fod_parm(wls_dev->wls_rx,
					wls_dev->static_config.fastchg_fod_parm_12V, WLS_FOD_PARM_LENGTH);
			else
				(void)oplus_chg_wls_rx_set_fod_parm(wls_dev->wls_rx,
					oplus_chg_wls_disable_fod_parm, WLS_FOD_PARM_LENGTH);
		}
		(void)oplus_chg_wls_rx_set_vout(wls_dev->wls_rx, WLS_VOUT_FASTCHG_INIT_MV, -1);
		vote(wls_dev->nor_fcc_votable, USER_VOTER, true, 2000, false);
		vote(wls_dev->nor_icl_votable, USER_VOTER, true, 1250, true);
		wls_status->current_rx_state = OPLUS_CHG_WLS_RX_STATE_DONE;
		wls_status->state_sub_step = 0;
		break;
	default:
		wls_status->state_sub_step = 0;
		break;
	}

	return 0;
}

static int oplus_chg_wls_rx_handle_state_done(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	int wait_time_ms = 4000;
	int iout_ma, vout_mv, vrect_mv;
	int rc;

	oplus_chg_wls_check_quiet_mode(wls_dev);

	if (wls_dev->batt_charge_enable) {
		if (!!get_client_vote(wls_dev->nor_out_disable_votable, STOP_VOTER)) {
			vote(wls_dev->nor_icl_votable, STOP_VOTER, false, 0, true);
			vote(wls_dev->nor_out_disable_votable, STOP_VOTER, false, 0, false);
		}
	} else {
		if (!get_client_vote(wls_dev->nor_out_disable_votable, STOP_VOTER)) {
			vote(wls_dev->nor_icl_votable, STOP_VOTER, true, WLS_CURR_STOP_CHG_MA, true);
			vote(wls_dev->nor_out_disable_votable, STOP_VOTER, true, 0, false);
		}
	}

	rc = oplus_chg_wls_fastchg_restart_check(wls_dev);
	if ((rc >= 0) && (!wls_status->fastchg_disable))
		wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_FAST;

	(void)oplus_chg_wls_nor_skin_check(wls_dev);
	oplus_chg_wls_check_term_charge(wls_dev);
	oplus_chg_wls_exchange_batt_mesg(wls_dev);

	iout_ma = wls_status->iout_ma;
	vout_mv = wls_status->vout_mv;
	vrect_mv = wls_status->vrect_mv;
	pr_err("wkcs: iout=%d, vout=%d, vrect=%d\n", iout_ma, vout_mv, vrect_mv);

	if (!wls_status->verity_started)
		schedule_delayed_work(&wls_dev->wls_verity_work, msecs_to_jiffies(500));

	return wait_time_ms;
}

static int oplus_chg_wls_rx_exit_state_done(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;

	switch (wls_status->target_rx_state) {
	case OPLUS_CHG_WLS_RX_STATE_FAST:
		if (!!get_client_vote(wls_dev->nor_out_disable_votable, STOP_VOTER)) {
			vote(wls_dev->nor_icl_votable, STOP_VOTER, false, 0, true);
			vote(wls_dev->nor_out_disable_votable, STOP_VOTER, false, 0, false);
		}
		wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_FAST;
		vote(wls_dev->nor_icl_votable, USER_VOTER, true, 500, true);
		(void)oplus_chg_wls_rx_set_fod_parm(wls_dev->wls_rx,
			oplus_chg_wls_disable_fod_parm, WLS_FOD_PARM_LENGTH);
		break;
	default:
		pr_err("unsupported target status(=%s)\n", oplus_chg_wls_rx_state_text[wls_status->target_rx_state]);
		wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_ERROR;
		wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_ERROR;
		break;
	}

	return 0;
}

static int oplus_chg_wls_rx_enter_state_quiet(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	int rc;

	switch(wls_status->state_sub_step) {
	case 0:
		(void)oplus_chg_wls_rx_set_vout(wls_dev->wls_rx, WLS_VOUT_FASTCHG_INIT_MV, 0);
		wls_status->state_sub_step = 1;
	case 1:
		if (wls_status->charge_type != WLS_CHARGE_TYPE_FAST) {
			rc = oplus_chg_wls_send_msg(wls_dev, WLS_CMD_INTO_FASTCHAGE, 0xff, 0);
			if (rc < 0) {
				pr_err("send WLS_CMD_INTO_FASTCHAGE err, rc=%d\n", rc);
				return 100;
			}
			wls_status->fod_parm_for_fastchg = false;
			return 1000;
		}else{
			if (wls_status->adapter_id == WLS_ADAPTER_THIRD_PARTY) {
				rc = oplus_chg_wls_get_third_adapter_ext_cmd_p_id(wls_dev);
				if (rc < 0) {
					if (!wls_status->rx_online)
						return 0;
					pr_err("get product id fail\n");
					wls_status->online_keep = true;
					vote(wls_dev->rx_disable_votable, VERITY_VOTER, true, 1, false);
					schedule_delayed_work(&wls_dev->rx_verity_restore_work, msecs_to_jiffies(500));
					return 100;
				}
			}

			if(wls_dev->static_config.fastchg_12V_fod_enable)
				(void)oplus_chg_wls_rx_set_fod_parm(wls_dev->wls_rx,
					wls_dev->static_config.fastchg_fod_parm_12V, WLS_FOD_PARM_LENGTH);
			else
				(void)oplus_chg_wls_rx_set_fod_parm(wls_dev->wls_rx,
					oplus_chg_wls_disable_fod_parm, WLS_FOD_PARM_LENGTH);
		}

		(void)oplus_chg_wls_rx_set_vout(wls_dev->wls_rx, WLS_VOUT_FASTCHG_INIT_MV, -1);
		vote(wls_dev->nor_fcc_votable, USER_VOTER, true, 1500, false);
		vote(wls_dev->nor_icl_votable, USER_VOTER, true, 1250, true);
		wls_status->current_rx_state = OPLUS_CHG_WLS_RX_STATE_QUIET;
		wls_status->state_sub_step = 0;
		break;
	default:
		wls_status->state_sub_step = 0;
		break;
	}

	return 0;
}

static int oplus_chg_wls_rx_handle_state_quiet(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	int icl_ma, nor_input_curr_ma;
	int wait_time_ms = 4000;
	int rc;

	oplus_chg_wls_check_quiet_mode(wls_dev);

	if (!wls_status->switch_quiet_mode || wls_status->chg_done_quiet_mode) {
		if (wls_status->quiet_mode && !wls_status->chg_done_quiet_mode) {
			wait_time_ms = 1000;
			goto out;
		} else {
			if (wls_dev->batt_charge_enable) {
				if (!wls_status->chg_done_quiet_mode)
					wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_FAST;
				else
					wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_DONE;
			} else {
				wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_STOP;
			}
			return 0;
		}
	}

	if (wls_dev->batt_charge_enable) {
		if (!!get_client_vote(wls_dev->nor_out_disable_votable, STOP_VOTER)) {
			vote(wls_dev->nor_icl_votable, STOP_VOTER, false, 0, true);
			vote(wls_dev->nor_out_disable_votable, STOP_VOTER, false, 0, false);
		}
	} else {
		if (!get_client_vote(wls_dev->nor_out_disable_votable, STOP_VOTER)) {
			vote(wls_dev->nor_icl_votable, STOP_VOTER, true, WLS_CURR_STOP_CHG_MA, true);
			vote(wls_dev->nor_out_disable_votable, STOP_VOTER, true, 0, false);
		}
	}

out:
	(void)oplus_chg_wls_nor_skin_check(wls_dev);
	oplus_chg_wls_check_term_charge(wls_dev);
	oplus_chg_wls_exchange_batt_mesg(wls_dev);

	if ((wls_status->adapter_type != WLS_ADAPTER_TYPE_UNKNOWN) &&
	     !wls_status->verity_started) {
		icl_ma = get_effective_result(wls_dev->nor_icl_votable);
		rc = oplus_chg_wls_nor_get_input_curr(wls_dev->wls_nor, &nor_input_curr_ma);
		if (rc < 0)
			nor_input_curr_ma = wls_status->iout_ma;
		if (icl_ma - nor_input_curr_ma < 300)
			schedule_delayed_work(&wls_dev->wls_verity_work, msecs_to_jiffies(500));
	}

	return 4000;
}

static int oplus_chg_wls_rx_exit_state_quiet(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	// int rc;

	switch (wls_status->target_rx_state) {
	case OPLUS_CHG_WLS_RX_STATE_FAST:
		if (!!get_client_vote(wls_dev->nor_out_disable_votable, STOP_VOTER)) {
			vote(wls_dev->nor_icl_votable, STOP_VOTER, false, 0, true);
			vote(wls_dev->nor_out_disable_votable, STOP_VOTER, false, 0, false);
		}
		wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_FAST;
		vote(wls_dev->nor_icl_votable, USER_VOTER, true, 500, true);
		(void)oplus_chg_wls_rx_set_fod_parm(wls_dev->wls_rx,
			oplus_chg_wls_disable_fod_parm, WLS_FOD_PARM_LENGTH);
		break;
	case OPLUS_CHG_WLS_RX_STATE_STOP:
		wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_STOP;
		break;
	case OPLUS_CHG_WLS_RX_STATE_DONE:
		wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_DONE;
		break;
	default:
		pr_err("unsupported target status(=%s)\n", oplus_chg_wls_rx_state_text[wls_status->target_rx_state]);
		wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_ERROR;
		wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_ERROR;
		break;
	}

	return 0;
}

static int oplus_chg_wls_rx_enter_state_stop(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	int rc;

	switch(wls_status->state_sub_step) {
	case 0:
		(void)oplus_chg_wls_rx_set_vout(wls_dev->wls_rx,
			WLS_VOUT_FASTCHG_INIT_MV, 0);
		wls_status->state_sub_step = 1;
	case 1:
		if (wls_status->charge_type != WLS_CHARGE_TYPE_FAST) {
			rc = oplus_chg_wls_send_msg(wls_dev, WLS_CMD_INTO_FASTCHAGE, 0xff, 0);
			if (rc < 0) {
				pr_err("send WLS_CMD_INTO_FASTCHAGE err, rc=%d\n", rc);
				return 100;
			}
			wls_status->fod_parm_for_fastchg = false;
			return 1000;
		}else{
			if(wls_dev->static_config.fastchg_12V_fod_enable)
				(void)oplus_chg_wls_rx_set_fod_parm(wls_dev->wls_rx,
					wls_dev->static_config.fastchg_fod_parm_12V, WLS_FOD_PARM_LENGTH);
			else
				(void)oplus_chg_wls_rx_set_fod_parm(wls_dev->wls_rx,
					oplus_chg_wls_disable_fod_parm, WLS_FOD_PARM_LENGTH);
		}

		(void)oplus_chg_wls_rx_set_vout(wls_dev->wls_rx, WLS_VOUT_FASTCHG_INIT_MV, -1);
		vote(wls_dev->nor_icl_votable, STOP_VOTER, true, 300, true);
		vote(wls_dev->nor_out_disable_votable, STOP_VOTER, true, 1, false);
		wls_status->current_rx_state = OPLUS_CHG_WLS_RX_STATE_STOP;
		wls_status->state_sub_step = 0;
		WARN_ON(wls_dev->batt_charge_enable);
		break;
	default:
		wls_status->state_sub_step = 0;
		break;
	}

	return 0;
}

static int oplus_chg_wls_rx_handle_state_stop(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	int wait_time_ms = 4000;
	// int rc;

	if (wls_dev->batt_charge_enable) {
		vote(wls_dev->nor_icl_votable, STOP_VOTER, false, 0, true);
		vote(wls_dev->nor_out_disable_votable, STOP_VOTER, false, 0, false);
		if (wls_status->switch_quiet_mode)
			wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_QUIET;
		else
			wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_FAST;
		return 0;
	}

	oplus_chg_wls_check_quiet_mode(wls_dev);

	(void)oplus_chg_wls_nor_skin_check(wls_dev);
	oplus_chg_wls_check_term_charge(wls_dev);
	return wait_time_ms;
}

static int oplus_chg_wls_rx_exit_state_stop(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	// int rc;

	switch (wls_status->target_rx_state) {
	case OPLUS_CHG_WLS_RX_STATE_FAST:
		wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_FAST;
		vote(wls_dev->nor_icl_votable, USER_VOTER, true, 500, true);
		(void)oplus_chg_wls_rx_set_fod_parm(wls_dev->wls_rx,
			oplus_chg_wls_disable_fod_parm, WLS_FOD_PARM_LENGTH);
		break;
	case OPLUS_CHG_WLS_RX_STATE_QUIET:
		wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_QUIET;
		vote(wls_dev->nor_icl_votable, USER_VOTER, true, 500, true);
		break;
	default:
		pr_err("unsupported target status(=%s)\n", oplus_chg_wls_rx_state_text[wls_status->target_rx_state]);
		wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_ERROR;
		wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_ERROR;
		break;
	}

	return 0;
}

static int oplus_chg_wls_rx_enter_state_debug(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	struct oplus_chg_wls_dynamic_config *dynamic_cfg =&wls_dev->dynamic_config;
	union oplus_chg_mod_propval pval;
	int scale_factor;
	int rc;

	switch (wls_dev->force_type) {
	case OPLUS_CHG_WLS_FORCE_TYPE_BPP:
		vote(wls_dev->nor_fcc_votable, USER_VOTER, true, 1500, false);
		vote(wls_dev->nor_icl_votable, USER_VOTER, true, 500, false);
		(void)oplus_chg_wls_rx_set_vout(wls_dev->wls_rx, 5000, 0);
		msleep(500);
		vote(wls_dev->nor_icl_votable, USER_VOTER, true, 1000, false);
		if (wls_dev->factory_mode)
			(void)oplus_chg_wls_send_msg(wls_dev, WLS_CMD_GET_TX_PWR, 0xff, -1);
		break;
	case OPLUS_CHG_WLS_FORCE_TYPE_EPP:
		vote(wls_dev->nor_fcc_votable, USER_VOTER, true, 1500, false);
		vote(wls_dev->nor_icl_votable, USER_VOTER, true, 500, false);
		(void)oplus_chg_wls_rx_set_vout(wls_dev->wls_rx, WLS_VOUT_EPP_MV, 0);
		(void)oplus_chg_wls_send_msg(wls_dev, WLS_CMD_INTO_FASTCHAGE, 0xff, -1);
		(void)oplus_chg_wls_rx_set_vout(wls_dev->wls_rx, WLS_VOUT_EPP_MV, -1);
		msleep(500);
		vote(wls_dev->nor_icl_votable, USER_VOTER, true, 850, false);
		if (wls_dev->factory_mode)
			(void)oplus_chg_wls_send_msg(wls_dev, WLS_CMD_GET_TX_PWR, 0xff, -1);
		break;
	case OPLUS_CHG_WLS_FORCE_TYPE_EPP_PLUS:
		vote(wls_dev->nor_fcc_votable, USER_VOTER, true, 2000, false);
		vote(wls_dev->nor_icl_votable, USER_VOTER, true, 500, false);
		(void)oplus_chg_wls_rx_set_vout(wls_dev->wls_rx, WLS_VOUT_EPP_MV, 0);
		(void)oplus_chg_wls_send_msg(wls_dev, WLS_CMD_INTO_FASTCHAGE, 0xff, -1);
		(void)oplus_chg_wls_rx_set_vout(wls_dev->wls_rx, WLS_VOUT_EPP_MV, -1);
		msleep(500);
		vote(wls_dev->nor_icl_votable, USER_VOTER, true, 1250, false);
		if (wls_dev->factory_mode)
			(void)oplus_chg_wls_send_msg(wls_dev, WLS_CMD_GET_TX_PWR, 0xff, -1);
		break;
	case OPLUS_CHG_WLS_FORCE_TYPE_FAST:
		vote(wls_dev->nor_icl_votable, USER_VOTER, true, 500, false);
		(void)oplus_chg_wls_rx_set_vout(wls_dev->wls_rx, WLS_VOUT_FASTCHG_INIT_MV, 0);
		(void)oplus_chg_wls_send_msg(wls_dev, WLS_CMD_INTO_FASTCHAGE, 0xff, -1);
		(void)oplus_chg_wls_rx_set_vout(wls_dev->wls_rx, WLS_VOUT_FASTCHG_INIT_MV, -1);
		(void)oplus_chg_wls_rx_set_vout_step(wls_dev->wls_rx, 15000, 1000, -1);
		if (is_batt_ocm_available(wls_dev)) {
			rc = oplus_chg_mod_get_property(wls_dev->batt_ocm,
							OPLUS_CHG_PROP_CELL_NUM,
							&pval);
			if (rc != 0)
				scale_factor = 1;
			else
				scale_factor =
					WLS_RX_VOL_MAX_MV / 5000 / pval.intval;
		} else {
			scale_factor = 1;
		}
		wls_status->fastchg_ibat_max_ma = dynamic_cfg->fastchg_curr_max_ma * scale_factor;;
		vote(wls_dev->fcc_votable, MAX_VOTER, true, WLS_FASTCHG_CURR_45W_MAX_MA, false);
		break;
	default:
		break;
	}

	wls_status->current_rx_state = OPLUS_CHG_WLS_RX_STATE_DEBUG;

	return 0;
}

static int oplus_chg_wls_rx_handle_state_debug(struct oplus_chg_wls *wls_dev)
{
	// struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	// int rc;

	return 4000;
}

static int oplus_chg_wls_rx_exit_state_debug(struct oplus_chg_wls *wls_dev)
{
	// struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	// int rc;

	return 0;
}

static int oplus_chg_wls_rx_enter_state_ftm(struct oplus_chg_wls *wls_dev)
{
	// struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	// int rc;

	return 0;
}

static int oplus_chg_wls_rx_handle_state_ftm(struct oplus_chg_wls *wls_dev)
{
	// struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	// int rc;

	return 4000;
}

static int oplus_chg_wls_rx_exit_state_ftm(struct oplus_chg_wls *wls_dev)
{
	// struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	// int rc;

	return 0;
}

static int oplus_chg_wls_rx_enter_state_error(struct oplus_chg_wls *wls_dev)
{
	// struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	// int rc;

	return 0;
}

static int oplus_chg_wls_rx_handle_state_error(struct oplus_chg_wls *wls_dev)
{
	// struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	// int rc;

	return 4000;
}

static int oplus_chg_wls_rx_exit_state_error(struct oplus_chg_wls *wls_dev)
{
	// struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	// int rc;

	return 0;
}

struct oplus_chg_wls_state_handler oplus_chg_wls_rx_state_handler[] = {
	[OPLUS_CHG_WLS_RX_STATE_DEFAULT] = {
		NULL,
		oplus_chg_wls_rx_handle_state_default,
		oplus_chg_wls_rx_exit_state_default
	},
	[OPLUS_CHG_WLS_RX_STATE_BPP] = {
		oplus_chg_wls_rx_enter_state_bpp,
		oplus_chg_wls_rx_handle_state_bpp,
		oplus_chg_wls_rx_exit_state_bpp
	},
	[OPLUS_CHG_WLS_RX_STATE_EPP] = {
		oplus_chg_wls_rx_enter_state_epp,
		oplus_chg_wls_rx_handle_state_epp,
		oplus_chg_wls_rx_exit_state_epp
	},
	[OPLUS_CHG_WLS_RX_STATE_EPP_PLUS] = {
		oplus_chg_wls_rx_enter_state_epp_plus,
		oplus_chg_wls_rx_handle_state_epp_plus,
		oplus_chg_wls_rx_exit_state_epp_plus,
	},
	[OPLUS_CHG_WLS_RX_STATE_FAST] = {
		oplus_chg_wls_rx_enter_state_fast,
		oplus_chg_wls_rx_handle_state_fast,
		oplus_chg_wls_rx_exit_state_fast,
	},
	[OPLUS_CHG_WLS_RX_STATE_FFC] = {
		oplus_chg_wls_rx_enter_state_ffc,
		oplus_chg_wls_rx_handle_state_ffc,
		oplus_chg_wls_rx_exit_state_ffc,
	},
	[OPLUS_CHG_WLS_RX_STATE_DONE] = {
		oplus_chg_wls_rx_enter_state_done,
		oplus_chg_wls_rx_handle_state_done,
		oplus_chg_wls_rx_exit_state_done,
	},
	[OPLUS_CHG_WLS_RX_STATE_QUIET] = {
		oplus_chg_wls_rx_enter_state_quiet,
		oplus_chg_wls_rx_handle_state_quiet,
		oplus_chg_wls_rx_exit_state_quiet,
	},
	[OPLUS_CHG_WLS_RX_STATE_STOP] = {
		oplus_chg_wls_rx_enter_state_stop,
		oplus_chg_wls_rx_handle_state_stop,
		oplus_chg_wls_rx_exit_state_stop,
	},
	[OPLUS_CHG_WLS_RX_STATE_DEBUG] = {
		oplus_chg_wls_rx_enter_state_debug,
		oplus_chg_wls_rx_handle_state_debug,
		oplus_chg_wls_rx_exit_state_debug,
	},
	[OPLUS_CHG_WLS_RX_STATE_FTM] = {
		oplus_chg_wls_rx_enter_state_ftm,
		oplus_chg_wls_rx_handle_state_ftm,
		oplus_chg_wls_rx_exit_state_ftm,
	},
	[OPLUS_CHG_WLS_RX_STATE_ERROR] = {
		oplus_chg_wls_rx_enter_state_error,
		oplus_chg_wls_rx_handle_state_error,
		oplus_chg_wls_rx_exit_state_error,
	},
};

static void oplus_chg_wls_rx_sm(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_wls *wls_dev = container_of(dwork, struct oplus_chg_wls, wls_rx_sm_work);
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	int delay_ms = 4000;

	if (!wls_status->rx_online) {
		pr_info("wireless charge is offline\n");
		return;
	}

	pr_err("curr_state=%s, next_state=%s, target_state=%s\n",
		oplus_chg_wls_rx_state_text[wls_status->current_rx_state],
		oplus_chg_wls_rx_state_text[wls_status->next_rx_state],
		oplus_chg_wls_rx_state_text[wls_status->target_rx_state]);

	if (wls_status->current_rx_state != wls_status->target_rx_state) {
		if (wls_status->current_rx_state != wls_status->next_rx_state) {
			if (oplus_chg_wls_rx_state_handler[wls_status->next_rx_state].enter_state != NULL) {
				delay_ms =oplus_chg_wls_rx_state_handler[wls_status->next_rx_state].enter_state(wls_dev);
			} else {
				delay_ms = 0;
			}
		} else {
			if (oplus_chg_wls_rx_state_handler[wls_status->current_rx_state].exit_state != NULL) {
				delay_ms = oplus_chg_wls_rx_state_handler[wls_status->current_rx_state].exit_state(wls_dev);
			} else {
				delay_ms = 0;
			}
		}
	} else {
		if (oplus_chg_wls_rx_state_handler[wls_status->current_rx_state].handle_state != NULL) {
			delay_ms = oplus_chg_wls_rx_state_handler[wls_status->current_rx_state].handle_state(wls_dev);
		}
	}

	queue_delayed_work(wls_dev->wls_wq, &wls_dev->wls_rx_sm_work, msecs_to_jiffies(delay_ms));
}

static void oplus_chg_wls_trx_disconnect_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_wls *wls_dev = container_of(dwork, struct oplus_chg_wls, wls_trx_disconnect_work);
	static int retry_num;
	int rc;

	rc = oplus_chg_wls_set_trx_enable(wls_dev, false);
	if (rc < 0) {
		retry_num++;
		pr_err("can't disable trx, retry_num=%d, rc=%d\n", retry_num, rc);
		if (retry_num < 5)
			schedule_delayed_work(&wls_dev->wls_trx_disconnect_work, msecs_to_jiffies(100));
		else
			retry_num = 0;
	}
	retry_num = 0;
}

#ifndef CONFIG_OPLUS_CHG_OOS
static void oplus_chg_wls_clear_trx_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_wls *wls_dev = container_of(dwork, struct oplus_chg_wls, wls_clear_trx_work);
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;

	if (!wls_status->rx_present && !wls_status->rx_online) {
		wls_status->trx_close_delay = false;
		oplus_chg_mod_changed(wls_dev->batt_ocm);
	}
}
#endif

static void oplus_chg_wls_trx_sm(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_wls *wls_dev = container_of(dwork, struct oplus_chg_wls, wls_trx_sm_work);
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	u8 trx_status, trx_err;
	static int err_count;
	int delay_ms = 5000;
	int rc;

	rc = oplus_chg_wls_rx_get_trx_err(wls_dev->wls_rx, &trx_err);
	if (rc < 0) {
		pr_err("can't get trx err code, rc=%d\n", rc);
		goto out;
	}
	pr_err("wkcs: trx_err=0x%02x\n", trx_err);
	if (trx_err & WLS_TRX_ERR_RXAC) {
		pr_err("trx err: RXAC\n");
		wls_status->trx_state = OPLUS_CHG_WLS_TRX_STATE_OFF;
#ifndef CONFIG_OPLUS_CHG_OOS
		wls_status->trx_close_delay = true;
#endif
	}
	if (trx_err & WLS_TRX_ERR_OCP) {
		pr_err("trx err: OCP\n");
		wls_status->trx_state = OPLUS_CHG_WLS_TRX_STATE_OFF;
#ifndef CONFIG_OPLUS_CHG_OOS
		wls_status->trx_close_delay = true;
#endif
	}
	if (trx_err & WLS_TRX_ERR_OVP) {
		pr_err("trx err: OVP\n");
		wls_status->trx_state = OPLUS_CHG_WLS_TRX_STATE_OFF;
	}
	if (trx_err & WLS_TRX_ERR_LVP) {
		pr_err("trx err: LVP\n");
		wls_status->trx_state = OPLUS_CHG_WLS_TRX_STATE_OFF;
	}
	if (trx_err & WLS_TRX_ERR_FOD) {
		pr_err("trx err: FOD\n");
		wls_status->trx_state = OPLUS_CHG_WLS_TRX_STATE_OFF;
	}
	if (trx_err & WLS_TRX_ERR_OTP) {
		pr_err("trx err: OTP\n");
		wls_status->trx_state = OPLUS_CHG_WLS_TRX_STATE_OFF;
	}
	if (trx_err & WLS_TRX_ERR_CEPTIMEOUT) {
		pr_err("trx err: CEPTIMEOUT\n");
	}
	if (trx_err & WLS_TRX_ERR_RXEPT) {
		pr_err("trx err: RXEPT\n");
		wls_status->trx_state = OPLUS_CHG_WLS_TRX_STATE_OFF;
	}
	if (wls_status->trx_state == OPLUS_CHG_WLS_TRX_STATE_OFF)
		goto err;

	rc = oplus_chg_wls_rx_get_trx_status(wls_dev->wls_rx, &trx_status);
	if (rc < 0) {
		pr_err("can't get trx err code, rc=%d\n", rc);
		goto out;
	}
	pr_err("wkcs: trx_status=0x%02x\n", trx_status);
	if (trx_status & WLS_TRX_STATUS_READY) {
		wls_status->trx_present = true;
		wls_status->trx_online = false;
		if (is_batt_ocm_available(wls_dev))
			oplus_chg_mod_changed(wls_dev->batt_ocm);
		rc = oplus_chg_wls_rx_set_trx_enable(wls_dev->wls_rx, true);
		if (rc < 0) {
			pr_err("can't enable trx, rc=%d\n", rc);
			goto out;
		}
		wls_status->trx_state = OPLUS_CHG_WLS_TRX_STATE_READY;
		delay_ms = 200;
	} else if ((trx_status & WLS_TRX_STATUS_DIGITALPING) ||
		   (trx_status & WLS_TRX_STATUS_ANALOGPING)) {
		wls_status->trx_online = false;
		if (is_batt_ocm_available(wls_dev))
			oplus_chg_mod_changed(wls_dev->batt_ocm);
		if (wls_status->trx_state == OPLUS_CHG_WLS_TRX_STATE_WAIT_PING) {
			pr_info("trx wait device connect timedout\n");
			goto err;
		} else {
			wls_status->trx_state = OPLUS_CHG_WLS_TRX_STATE_WAIT_PING;
#ifdef CONFIG_OPLUS_CHG_OOS
			/* Automatically shut down trx after 1 minute */
			delay_ms = 60000;
#else
			/*
			 * After waiting for 3 minutes, trx is forced to close
			 * to prevent the upper layer function from failing.
			 */
			delay_ms = 180000;
#endif
			pr_info("trx wait device connect, %ds\n", delay_ms / 1000);
		}
	} else if (trx_status & WLS_TRX_STATUS_TRANSFER) {
		wls_status->trx_state = OPLUS_CHG_WLS_TRX_STATE_TRANSFER;
		wls_status->trx_online = true;
		if (is_batt_ocm_available(wls_dev))
			oplus_chg_mod_changed(wls_dev->batt_ocm);
		rc = oplus_chg_wls_rx_get_trx_curr(wls_dev->wls_rx, &wls_status->trx_curr_ma);
		if (rc < 0)
			pr_err("can't get trx curr, rc=%d\n", rc);
		rc = oplus_chg_wls_rx_get_trx_vol(wls_dev->wls_rx, &wls_status->trx_vol_mv);
		if (rc < 0)
			pr_err("can't get trx vol, rc=%d\n", rc);
		pr_err("trx_vol=%d, trx_curr=%d\n", wls_status->trx_vol_mv, wls_status->trx_curr_ma);
		delay_ms = 5000;
	} else if (trx_status == 0) {
		goto out;
	}

	pr_err("trx_state=%s\n", oplus_chg_wls_trx_state_text[wls_status->trx_state]);
	err_count = 0;

schedule:
	queue_delayed_work(wls_dev->wls_wq, &wls_dev->wls_trx_sm_work, msecs_to_jiffies(delay_ms));
	return;
out:
	err_count++;
	delay_ms = 200;
	if (err_count > 5) {
		pr_err("trx status err, exit\n");
		goto err;
	}
	goto schedule;
err:
	wls_status->trx_state = OPLUS_CHG_WLS_TRX_STATE_OFF;
	err_count = 0;
	schedule_delayed_work(&wls_dev->wls_trx_disconnect_work, 0);
}

static void oplus_chg_wls_upgrade_fw_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_wls *wls_dev = container_of(dwork, struct oplus_chg_wls, wls_upgrade_fw_work);
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	bool usb_present;
	int boot_mode;
	static int retry_num;
	int rc;

	boot_mode = get_boot_mode();
	if (boot_mode == MSM_BOOT_MODE__FACTORY || boot_mode == MSM_BOOT_MODE__RF) {
		pr_err("is factory mode, can't upgrade fw\n");
		vote(wls_dev->rx_disable_votable, UPGRADE_VOTER, true, 1, false);
		msleep(1000);
		vote(wls_dev->rx_disable_votable, UPGRADE_VOTER, false, 0, false);
		retry_num = 0;
		return;
	}

	usb_present = oplus_chg_wls_is_usb_present(wls_dev);
	if (usb_present) {
		pr_info("usb online, wls fw upgrade pending\n");
		wls_status->upgrade_fw_pending = true;
		return;
	}

	if (wls_status->rx_present) {
		pr_err("rx present, exit upgrade\n");
		retry_num = 0;
		return;
	}

	if (wls_dev->fw_upgrade_by_buf) {
		if (wls_dev->fw_buf == NULL || wls_dev->fw_size == 0) {
			pr_err("fw buf is NULL or fw size is 0, can't upgrade\n");
			wls_dev->fw_upgrade_by_buf = false;
			wls_dev->fw_size = 0;
			return;
		}
		vote(wls_dev->wrx_en_votable, UPGRADE_FW_VOTER, true, 1, false);
		rc = oplus_chg_wls_rx_upgrade_firmware_by_buf(wls_dev->wls_rx, wls_dev->fw_buf, wls_dev->fw_size);
		vote(wls_dev->wrx_en_votable, UPGRADE_FW_VOTER, false, 0, false);
		if (rc < 0)
			pr_err("upgrade error, rc=%d\n", rc);
		kfree(wls_dev->fw_buf);
		wls_dev->fw_buf = NULL;
		wls_dev->fw_upgrade_by_buf = false;
		wls_dev->fw_size = 0;
	} else {
		vote(wls_dev->wrx_en_votable, UPGRADE_FW_VOTER, true, 1, false);
		rc = oplus_chg_wls_rx_upgrade_firmware_by_img(wls_dev->wls_rx);
		vote(wls_dev->wrx_en_votable, UPGRADE_FW_VOTER, false, 0, false);
		if (rc < 0) {
			retry_num++;
			pr_err("upgrade error, retry_num=%d, rc=%d\n", retry_num, rc);
			goto out;
		}
	}
	pr_info("update successed\n");

	return;

out:
	if (retry_num >= 5) {
		retry_num = 0;
		return;
	}
	schedule_delayed_work(&wls_dev->wls_upgrade_fw_work, msecs_to_jiffies(1000));
}

static void oplus_chg_wls_data_update_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_wls *wls_dev = container_of(dwork, struct oplus_chg_wls, wls_data_update_work);
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	int ibat_ma, tmp_val, ibat_err, cep, vol_set_mv;
	int iout_ma, vout_mv, vrect_mv;
	static int cep_err_count;
	bool skip_cep_check = false;
	bool cep_ok = false;
	int rc;

	if (!wls_status->rx_online) {
		pr_err("wireless charge is not online\n");
		return;
	}

	rc = oplus_chg_wls_rx_get_iout(wls_dev->wls_rx, &iout_ma);
	if (rc < 0)
		pr_err("can't get rx iout, rc=%d\n", rc);
	rc = oplus_chg_wls_rx_get_vout(wls_dev->wls_rx, &vout_mv);
	if (rc < 0)
		pr_err("can't get rx vout, rc=%d\n", rc);
	rc = oplus_chg_wls_rx_get_vrect(wls_dev->wls_rx, &vrect_mv);
	if (rc < 0)
		pr_err("can't get rx vrect, rc=%d\n", rc);
	WRITE_ONCE(wls_status->iout_ma, iout_ma);
	WRITE_ONCE(wls_status->vout_mv, vout_mv);
	WRITE_ONCE(wls_status->vrect_mv, vrect_mv);

	if (!wls_status->fastchg_started)
		goto out;

	rc = oplus_chg_wls_get_ibat(wls_dev, &ibat_ma);
	if (rc < 0) {
		pr_err("can't get ibat, rc=%d\n");
		goto out;
	}

	pr_info("Iout: target=%d, out=%d, ibat_max=%d, ibat=%d\n",
		wls_status->fastchg_target_curr_ma, wls_status->iout_ma,
		wls_status->fastchg_ibat_max_ma, ibat_ma);

	tmp_val = wls_status->fastchg_target_curr_ma - wls_status->iout_ma;
	ibat_err = ((wls_status->fastchg_ibat_max_ma - abs(ibat_ma)) / 4) - (WLS_CURR_ERR_MIN_MA / 2);
	/* Prevent the voltage from increasing too much, ibat exceeds expectations */
	if ((ibat_err > -(WLS_CURR_ERR_MIN_MA / 2)) && (ibat_err < 0) && (tmp_val > 0)) {
		/*
		 * When ibat is greater than 5800mA, the current is not
		 * allowed to continue to increase, preventing fluctuations.
		 */
		tmp_val = 0;
	} else {
		tmp_val = tmp_val > ibat_err ? ibat_err : tmp_val;
	}
	rc = oplus_chg_wls_get_cep_check_update(wls_dev->wls_rx, &cep);
	if (rc < 0) {
		pr_err("can't get cep, rc=%d\n", rc);
		cep_ok = false;
	} else {
		if (abs(cep) < 3)
			cep_ok = true;
		else
			cep_ok = false;
	}
	if (tmp_val < 0) {
		if (!cep_ok)
			cep_err_count++;
		else
			cep_err_count = 0;
		if (!wls_status->fastchg_curr_need_dec || cep_err_count >= WLS_CEP_ERR_MAX) {
			skip_cep_check = true;
			wls_status->fastchg_curr_need_dec = true;
			cep_err_count = 0;
		}
	} else {
		cep_err_count = 0;
		wls_status->fastchg_curr_need_dec = false;
	}
	vol_set_mv = wls_dev->wls_rx->vol_set_mv;
	if (cep_ok || skip_cep_check) {
		if (tmp_val > 0 || tmp_val < -WLS_CURR_ERR_MIN_MA) {
			if (tmp_val > 0) {
				if (tmp_val > 200)
					vol_set_mv += 200;
				else if (tmp_val > 50)
					vol_set_mv += 100;
				else
					vol_set_mv += 20;
			} else {
				if (tmp_val < -200)
					vol_set_mv -= 200;
				else if (tmp_val < -50)
					vol_set_mv -= 100;
				else
					vol_set_mv -= 20;
			}
			if (vol_set_mv > WLS_RX_VOL_MAX_MV)
				vol_set_mv = WLS_RX_VOL_MAX_MV;
			if (vol_set_mv < WLS_FASTCHG_MODE_VOL_MIN_MV)
				vol_set_mv = WLS_FASTCHG_MODE_VOL_MIN_MV;
			mutex_lock(&wls_dev->update_data_lock);
			(void)oplus_chg_wls_rx_set_vout(wls_dev->wls_rx, vol_set_mv, 0);
			mutex_unlock(&wls_dev->update_data_lock);
		}
	}

out:
	schedule_delayed_work(&wls_dev->wls_data_update_work, msecs_to_jiffies(500));
}

static void oplus_chg_wls_usb_connect_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_wls *wls_dev = container_of(dwork, struct oplus_chg_wls, usb_connect_work);
	bool connected, pre_connected;

	pre_connected = oplus_chg_wls_is_usb_connected(wls_dev);
retry:
	msleep(10);
	connected = oplus_chg_wls_is_usb_connected(wls_dev);
	if (connected != pre_connected) {
		pre_connected = connected;
		goto retry;
	}
	if (is_usb_ocm_available(wls_dev)) {
		if (connected) {
			// todo
		} else {
			// todo
		}
	} else {
		pr_err("usb ocm not found\n");
	}
}

static void oplus_chg_wls_fod_cal_work(struct work_struct *work)
{
}

static void oplus_chg_wls_rx_restore_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_wls *wls_dev = container_of(dwork, struct oplus_chg_wls, rx_restore_work);

	vote(wls_dev->rx_disable_votable, JEITA_VOTER, false, 0, false);
	vote(wls_dev->nor_icl_votable, JEITA_VOTER, true, 0, true);
}

static void oplus_chg_wls_rx_iic_restore_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_wls *wls_dev = container_of(dwork, struct oplus_chg_wls, rx_iic_restore_work);

	vote(wls_dev->rx_disable_votable, RX_IIC_VOTER, false, 0, false);
}

static void oplus_chg_wls_rx_verity_restore_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_wls *wls_dev = container_of(dwork, struct oplus_chg_wls, rx_verity_restore_work);

	vote(wls_dev->rx_disable_votable, VERITY_VOTER, false, 0, false);
	schedule_delayed_work(&wls_dev->verity_state_remove_work, msecs_to_jiffies(10000));
}

static void oplus_chg_wls_rx_restart_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_wls *wls_dev = container_of(dwork, struct oplus_chg_wls, rx_restart_work);
	static int retry_count;

	if (!is_rx_ic_available(wls_dev->wls_rx)) {
		if (retry_count > 5) {
			pr_err("can't found wls rx ic\n");
			retry_count = 0;
			return;
		}
		retry_count++;
		schedule_delayed_work(&wls_dev->rx_restart_work, msecs_to_jiffies(500));
	}
	if (!oplus_chg_wls_rx_is_connected(wls_dev->wls_rx)) {
		pr_info("wireless charging is not connected\n");
		return;
	}

	retry_count = 0;
	wls_dev->wls_status.online_keep = true;
	vote(wls_dev->rx_disable_votable, USER_VOTER, true, 1, false);
	msleep(1000);
	vote(wls_dev->rx_disable_votable, USER_VOTER, false, 0, false);
	if (READ_ONCE(wls_dev->wls_status.online_keep))
		schedule_delayed_work(&wls_dev->online_keep_remove_work, msecs_to_jiffies(4000));
}

static void oplus_chg_wls_online_keep_remove_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_wls *wls_dev = container_of(dwork, struct oplus_chg_wls, online_keep_remove_work);

	wls_dev->wls_status.online_keep = false;
	if (!wls_dev->wls_status.rx_online) {
		if (wls_dev->rx_wake_lock_on) {
			pr_info("release rx_wake_lock\n");
			__pm_relax(wls_dev->rx_wake_lock);
			wls_dev->rx_wake_lock_on = false;
		} else {
			pr_err("rx_wake_lock is already relax\n");
		}
	}
}

static void oplus_chg_wls_verity_state_remove_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_wls *wls_dev = container_of(dwork, struct oplus_chg_wls, verity_state_remove_work);

	wls_dev->wls_status.verity_state_keep = false;
}

static int oplus_chg_wls_dev_open(struct inode *inode, struct file *filp)
{
	struct oplus_chg_wls *wls_dev = container_of(filp->private_data,
		struct oplus_chg_wls, misc_dev);

	filp->private_data = wls_dev;
	pr_debug("%d,%d\n", imajor(inode), iminor(inode));
	return 0;
}

static ssize_t oplus_chg_wls_dev_read(struct file *filp, char __user *buf,
		size_t count, loff_t *offset)
{
	struct oplus_chg_wls *wls_dev = filp->private_data;
	struct wls_dev_cmd cmd;
	int rc = 0;

	mutex_lock(&wls_dev->read_lock);
	rc = wait_event_interruptible(wls_dev->read_wq, wls_dev->cmd_data_ok);
	mutex_unlock(&wls_dev->read_lock);
	if (rc)
		return rc;
	if (!wls_dev->cmd_data_ok)
		pr_err("wlchg false wakeup, rc=%d\n", rc);
	mutex_lock(&wls_dev->cmd_data_lock);
	wls_dev->cmd_data_ok = false;
	memcpy(&cmd, &wls_dev->cmd, sizeof(struct wls_dev_cmd));
	mutex_unlock(&wls_dev->cmd_data_lock);
	if (copy_to_user(buf, &cmd, sizeof(struct wls_dev_cmd))) {
		pr_err("failed to copy to user space\n");
		return -EFAULT;
	}

	return sizeof(struct wls_dev_cmd);
}

#define WLS_IOC_MAGIC			0xf1
#define WLS_NOTIFY_WLS_AUTH		_IOW(WLS_IOC_MAGIC, 1, struct wls_auth_result)

static long oplus_chg_wls_dev_ioctl(struct file *filp, unsigned int cmd,
		unsigned long arg)
{
	struct oplus_chg_wls *wls_dev = filp->private_data;
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	void __user *argp = (void __user *)arg;
	int rc;

	switch (cmd) {
	case WLS_NOTIFY_WLS_AUTH:
		rc = copy_from_user(&wls_status->verfity_data, argp, sizeof(struct wls_auth_result));
		if (rc) {
			pr_err("failed copy to user space\n");
			return rc;
		}
		wls_status->verity_data_ok = true;
		break;
	default:
		pr_err("bad ioctl %u\n", cmd);
		return -EINVAL;
	}

	return 0;
}

static ssize_t oplus_chg_wls_dev_write(struct file *filp, const char __user *buf,
		size_t count, loff_t *offset)
{
	return count;
}

static const struct file_operations oplus_chg_wls_dev_fops = {
	.owner			= THIS_MODULE,
	.llseek			= no_llseek,
	.write			= oplus_chg_wls_dev_write,
	.read			= oplus_chg_wls_dev_read,
	.open			= oplus_chg_wls_dev_open,
	.unlocked_ioctl	= oplus_chg_wls_dev_ioctl,
};

static irqreturn_t oplus_chg_wls_usb_int_handler(int irq, void *dev_id)
{
	struct oplus_chg_wls *wls_dev = dev_id;

	schedule_delayed_work(&wls_dev->usb_connect_work, 0);
	return IRQ_HANDLED;
}

static int read_range_data_from_node(struct device_node *node,
		const char *prop_str, struct oplus_chg_wls_range_data *ranges,
		int max_threshold, u32 max_value)
{
	int rc = 0, i, length, per_tuple_length, tuples;

	if (!node || !prop_str || !ranges) {
		pr_err("Invalid parameters passed\n");
		return -EINVAL;
	}

	rc = of_property_count_elems_of_size(node, prop_str, sizeof(u32));
	if (rc < 0) {
		pr_err("Count %s failed, rc=%d\n", prop_str, rc);
		return rc;
	}

	length = rc;
	per_tuple_length = sizeof(struct oplus_chg_wls_range_data) / sizeof(u32);
	if (length % per_tuple_length) {
		pr_err("%s length (%d) should be multiple of %d\n",
				prop_str, length, per_tuple_length);
		return -EINVAL;
	}
	tuples = length / per_tuple_length;

	if (tuples > WLS_MAX_STEP_CHG_ENTRIES) {
		pr_err("too many entries(%d), only %d allowed\n",
				tuples, WLS_MAX_STEP_CHG_ENTRIES);
		return -EINVAL;
	}

	rc = of_property_read_u32_array(node, prop_str,
			(u32 *)ranges, length);
	if (rc) {
		pr_err("Read %s failed, rc=%d\n", prop_str, rc);
		return rc;
	}

	for (i = 0; i < tuples; i++) {
		if (ranges[i].low_threshold >
				ranges[i].high_threshold) {
			pr_err("%s thresholds should be in ascendant ranges\n",
						prop_str);
			rc = -EINVAL;
			goto clean;
		}

		if (ranges[i].low_threshold > max_threshold)
			ranges[i].low_threshold = max_threshold;
		if (ranges[i].high_threshold > max_threshold)
			ranges[i].high_threshold = max_threshold;
		if (ranges[i].curr_ma > max_value)
			ranges[i].curr_ma = max_value;
	}

	return tuples;
clean:
	memset(ranges, 0, tuples * sizeof(struct oplus_chg_wls_range_data));
	return rc;
}

static int read_skin_range_data_from_node(struct device_node *node,
		const char *prop_str, struct oplus_chg_wls_skin_range_data *ranges,
		int max_threshold, u32 max_value)
{
	int rc = 0, i, length, per_tuple_length, tuples;

	if (!node || !prop_str || !ranges) {
		pr_err("Invalid parameters passed\n");
		return -EINVAL;
	}

	rc = of_property_count_elems_of_size(node, prop_str, sizeof(u32));
	if (rc < 0) {
		pr_err("Count %s failed, rc=%d\n", prop_str, rc);
		return rc;
	}

	length = rc;
	per_tuple_length = sizeof(struct oplus_chg_wls_skin_range_data) / sizeof(u32);
	if (length % per_tuple_length) {
		pr_err("%s length (%d) should be multiple of %d\n",
				prop_str, length, per_tuple_length);
		return -EINVAL;
	}
	tuples = length / per_tuple_length;

	if (tuples > WLS_MAX_STEP_CHG_ENTRIES) {
		pr_err("too many entries(%d), only %d allowed\n",
				tuples, WLS_MAX_STEP_CHG_ENTRIES);
		return -EINVAL;
	}

	rc = of_property_read_u32_array(node, prop_str,
			(u32 *)ranges, length);
	if (rc) {
		pr_err("Read %s failed, rc=%d\n", prop_str, rc);
		return rc;
	}

	for (i = 0; i < tuples; i++) {
		if (ranges[i].low_threshold >
				ranges[i].high_threshold) {
			pr_err("%s thresholds should be in ascendant ranges\n",
						prop_str);
			rc = -EINVAL;
			goto clean;
		}

		if (ranges[i].low_threshold > max_threshold)
			ranges[i].low_threshold = max_threshold;
		if (ranges[i].high_threshold > max_threshold)
			ranges[i].high_threshold = max_threshold;
		if (ranges[i].curr_ma > max_value)
			ranges[i].curr_ma = max_value;
	}

	return tuples;
clean:
	memset(ranges, 0, tuples * sizeof(struct oplus_chg_wls_skin_range_data));
	return rc;
}

static int read_unsigned_data_from_node(struct device_node *node,
		const char *prop_str, u32 *addr, int len_max)
{
	int rc = 0, length;

	if (!node || !prop_str || !addr) {
		pr_err("Invalid parameters passed\n");
		return -EINVAL;
	}

	rc = of_property_count_elems_of_size(node, prop_str, sizeof(u32));
	if (rc < 0) {
		pr_err("Count %s failed, rc=%d\n", prop_str, rc);
		return rc;
	}

	length = rc;

	if (length > len_max) {
		pr_err("too many entries(%d), only %d allowed\n",
				length, len_max);
		return -EINVAL;
	}

	rc = of_property_read_u32_array(node, prop_str,
			(u32 *)addr, length);
	if (rc) {
		pr_err("Read %s failed, rc=%d\n", prop_str, rc);
		return rc;
	}

	return rc;
}

static const char * const strategy_soc[] = {
	[WLS_FAST_SOC_0_TO_30]	= "strategy_soc_0_to_30",
	[WLS_FAST_SOC_30_TO_70]	= "strategy_soc_30_to_70",
	[WLS_FAST_SOC_70_TO_90]	= "strategy_soc_70_to_90",
};
static const char * const strategy_temp[] = {
	[WLS_FAST_TEMP_0_TO_50]		= "strategy_temp_0_to_50",
	[WLS_FAST_TEMP_50_TO_120]	= "strategy_temp_50_to_120",
	[WLS_FAST_TEMP_120_TO_160]	= "strategy_temp_120_to_160",
	[WLS_FAST_TEMP_160_TO_400]	= "strategy_temp_160_to_400",
	[WLS_FAST_TEMP_400_TO_440]	= "strategy_temp_400_to_440",
};
static int oplus_chg_wls_parse_dt(struct oplus_chg_wls *wls_dev)
{
	struct device_node *node = wls_dev->dev->of_node;
	struct device_node  *soc_strategy_node;
	struct device_node *wls_third_part_strategy_node;
	struct oplus_chg_wls_static_config *static_cfg = &wls_dev->static_config;
	struct oplus_chg_wls_dynamic_config *dynamic_cfg =&wls_dev->dynamic_config;
	//struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	struct oplus_chg_wls_fcc_step *fcc_step = &wls_dev->wls_fcc_step;
	struct oplus_chg_wls_skin_step *skin_step;
	int i, m, n, j, k, length;
	int rc;

	wls_dev->support_epp_plus = of_property_read_bool(node, "oplus,support_epp_plus");
	wls_dev->support_fastchg = of_property_read_bool(node, "oplus,support_fastchg");
	wls_dev->support_get_tx_pwr = of_property_read_bool(node, "oplus,support_get_tx_pwr");

	rc = of_property_read_u32(node, "oplus,wls_phone_id",
				  &wls_dev->wls_phone_id);
	if (rc < 0) {
		pr_err("oplus,wls_phone_id reading failed, rc=%d\n", rc);
		wls_dev->wls_phone_id = 0x000A;
	}

	rc = of_property_read_u32(node, "oplus,wls_power_mw", &wls_dev->wls_power_mw);
	if (rc < 0) {
		pr_err("oplus,oplus,wls_power_mw reading failed, rc=%d\n", rc);
		wls_dev->wls_power_mw = 0;
	}

	static_cfg->fastchg_fod_enable = of_property_read_bool(node, "oplus,fastchg-fod-enable");
	if (static_cfg->fastchg_fod_enable) {
		rc = of_property_read_u8(node, "oplus,fastchg-match-q",
			&static_cfg->fastchg_match_q);
		if (rc < 0) {
			pr_err("oplus,fastchg-match-q reading failed, rc=%d\n", rc);
			static_cfg->fastchg_match_q = 0x3F;
		}
		rc = of_property_read_u8_array(node, "oplus,fastchg-fod-parm",
			(u8 *)&static_cfg->fastchg_fod_parm, WLS_FOD_PARM_LENGTH);
		if (rc < 0) {
			static_cfg->fastchg_fod_enable = false;
			pr_err("Read oplus,fastchg-fod-parm failed, rc=%d\n", rc);
		}
		rc = of_property_read_u8_array(node, "oplus,fastchg-fod-parm-12V",
			(u8 *)&static_cfg->fastchg_fod_parm_12V, WLS_FOD_PARM_LENGTH);
		static_cfg->fastchg_12V_fod_enable = true;
		if (rc < 0) {
			static_cfg->fastchg_12V_fod_enable = false;
			pr_err("Read oplus,fastchg-fod-parm-12V failed, rc=%d\n", rc);
		}
	}

	rc = of_property_read_string(node, "oplus,wls_chg_fw", &wls_dev->wls_chg_fw_name);
	if (rc < 0) {
		pr_err("oplus,wls_chg_fw reading failed, rc=%d\n", rc);
		wls_dev->wls_chg_fw_name = "IDT_P9415_default";
	}

	rc = of_property_read_u32(node, "oplus,max-voltage-mv", &dynamic_cfg->batt_vol_max_mv);
	if (rc < 0) {
		pr_err("oplus,max-voltage-mv reading failed, rc=%d\n", rc);
		dynamic_cfg->batt_vol_max_mv = 4550;
	}

	rc = of_property_read_u32(node, "oplus,fastchg_curr_max_ma", &dynamic_cfg->fastchg_curr_max_ma);
	if (rc < 0) {
		pr_err("oplus,fastchg_curr_max_ma reading failed, rc=%d\n", rc);
		dynamic_cfg->fastchg_curr_max_ma = 1500;
	}

	fcc_step->fcc_step = dynamic_cfg->fcc_step;
	rc = read_range_data_from_node(node, "oplus,fastchg-fcc_step",
				       dynamic_cfg->fcc_step,
				       450, // todo: temp
				       2500); //todo: current
	if (rc < 0) {
		pr_err("Read oplus,fastchg-fcc_step failed, rc=%d\n", rc);
		dynamic_cfg->fcc_step[0].low_threshold = 0;
		dynamic_cfg->fcc_step[0].high_threshold = 405;
		dynamic_cfg->fcc_step[0].curr_ma = 2250;
		dynamic_cfg->fcc_step[0].vol_max_mv = 4420;
		dynamic_cfg->fcc_step[0].need_wait = 1;
		dynamic_cfg->fcc_step[0].max_soc = 50;

		dynamic_cfg->fcc_step[1].low_threshold = 380;
		dynamic_cfg->fcc_step[1].high_threshold = 420;
		dynamic_cfg->fcc_step[1].curr_ma = 1500;
		dynamic_cfg->fcc_step[1].vol_max_mv = 4450;
		dynamic_cfg->fcc_step[1].need_wait = 1;
		dynamic_cfg->fcc_step[1].max_soc = 70;

		dynamic_cfg->fcc_step[2].low_threshold = 390;
		dynamic_cfg->fcc_step[2].high_threshold = 420;
		dynamic_cfg->fcc_step[2].curr_ma = 850;
		dynamic_cfg->fcc_step[2].vol_max_mv = 4480;
		dynamic_cfg->fcc_step[2].need_wait = 1;
		dynamic_cfg->fcc_step[2].max_soc = 90;

		dynamic_cfg->fcc_step[3].low_threshold = 400;
		dynamic_cfg->fcc_step[3].high_threshold = 420;
		dynamic_cfg->fcc_step[3].curr_ma =625;
		dynamic_cfg->fcc_step[3].vol_max_mv = 4480;
		dynamic_cfg->fcc_step[3].need_wait = 0;
		dynamic_cfg->fcc_step[3].max_soc = 90;
		fcc_step->max_step = 4;
	} else {
		fcc_step->max_step = rc;
	}

	wls_third_part_strategy_node = of_get_child_by_name(node,
	    "wireless_fastchg_third_part_strategy");
	if (!wls_third_part_strategy_node) {
		pr_err("Can not find wls third_part_strategy node\n");
		/* memcpy(wls_dev->fcc_third_part_steps, */
		/*     wls_dev->fcc_steps, sizeof(wls_dev->fcc_steps)); */
		goto non_ffc;
	}

	for (i = 0; i < WLS_FAST_SOC_MAX; i++) {
		soc_strategy_node = of_get_child_by_name(
		    wls_third_part_strategy_node, strategy_soc[i]);
		if (!soc_strategy_node) {
			pr_err("Can not find %s node\n", strategy_soc[i]);
			return -EINVAL;
		}

		for (j = 0; j < WLS_FAST_TEMP_MAX; j++) {
			rc = of_property_count_elems_of_size(
			    soc_strategy_node, strategy_temp[j], sizeof(u32));
			if (rc < 0) {
				pr_err("Count %s failed, rc=%d\n", strategy_temp[j], rc);
				return rc;
			}

			length = rc;
			rc = of_property_read_u32_array(soc_strategy_node, strategy_temp[j],
					(u32 *)wls_dev->fcc_third_part_steps[i].fcc_step[j].fcc_step,
					length);
			wls_dev->fcc_third_part_steps[i].fcc_step[j].max_step = length/5;
		}
	}

	for (i = 0; i < WLS_FAST_SOC_MAX; i++) {
		for (j = 0; j < WLS_FAST_TEMP_MAX; j++) {
			for (k = 0; k < wls_dev->fcc_third_part_steps[i].fcc_step[j].max_step; k++) {
				pr_err("third_part %s: %d %d %d %d %d\n", __func__,
				    wls_dev->fcc_third_part_steps[i].fcc_step[j].fcc_step[k].low_threshold,
				    wls_dev->fcc_third_part_steps[i].fcc_step[j].fcc_step[k].high_threshold,
				    wls_dev->fcc_third_part_steps[i].fcc_step[j].fcc_step[k].curr_ma,
				    wls_dev->fcc_third_part_steps[i].fcc_step[j].fcc_step[k].vol_max_mv,
				    wls_dev->fcc_third_part_steps[i].fcc_step[j].fcc_step[k].need_wait);
			}
		}
	}
non_ffc:
	for(i = 0; i < fcc_step->max_step; i++) {
		if (fcc_step->fcc_step[i].low_threshold > 0)
			fcc_step->allow_fallback[i] = true;
		else
			fcc_step->allow_fallback[i] = false;
	}

	rc = of_property_read_u32(node, "oplus,bpp-vol-mv", &dynamic_cfg->bpp_vol_mv);
	if (rc < 0) {
		pr_err("oplus,bpp-vol-mv reading failed, rc=%d\n",
		       rc);
		dynamic_cfg->bpp_vol_mv = 5000;
	}
	rc = of_property_read_u32(node, "oplus,epp-vol-mv", &dynamic_cfg->epp_vol_mv);
	if (rc < 0) {
		pr_err("oplus,epp-vol-mv reading failed, rc=%d\n",
		       rc);
		dynamic_cfg->epp_vol_mv = 10000;
	}
	rc = of_property_read_u32(node, "oplus,epp_plus-vol-mv", &dynamic_cfg->epp_plus_vol_mv);
	if (rc < 0) {
		pr_err("oplus,epp_plus-vol-mv reading failed, rc=%d\n",
		       rc);
		dynamic_cfg->epp_plus_vol_mv = 10000;
	}
	rc = of_property_read_u32(node, "oplus,warp-vol-mv", &dynamic_cfg->warp_vol_mv);
	if (rc < 0) {
		pr_err("oplus,warp-vol-mv reading failed, rc=%d\n",
		       rc);
		dynamic_cfg->warp_vol_mv = 10000;
	}
	rc = of_property_read_u32(node, "oplus,swarp-vol-mv", &dynamic_cfg->swarp_vol_mv);
	if (rc < 0) {
		pr_err("oplus,swarp-vol-mv reading failed, rc=%d\n",
		       rc);
		dynamic_cfg->swarp_vol_mv = 12000;
	}

	rc = read_unsigned_data_from_node(node, "oplus,iclmax-ma",
		(u32 *)(&dynamic_cfg->iclmax_ma[OPLUS_WLS_CHG_BATT_CL_LOW]),
		BATT_TEMP_INVALID * OPLUS_WLS_CHG_MODE_MAX);
	if (rc < 0) {
		pr_err("get oplus,iclmax-ma property error, rc=%d\n", rc);
		for (i = 0; i < OPLUS_WLS_CHG_MODE_MAX; i++) {
			for (m = 0; m < BATT_TEMP_INVALID; m++)
				dynamic_cfg->iclmax_ma[OPLUS_WLS_CHG_BATT_CL_LOW][i][m] =
					default_config.iclmax_ma[OPLUS_WLS_CHG_BATT_CL_LOW][i][m];
		}
	}
	for (i = 0; i < OPLUS_WLS_CHG_MODE_MAX; i++) {
		wls_dev->icl_max_ma[i] = 0;
		for (m = 0; m < BATT_TEMP_INVALID; m++) {
			if (dynamic_cfg->iclmax_ma[OPLUS_WLS_CHG_BATT_CL_LOW][i][m] > wls_dev->icl_max_ma[i])
				wls_dev->icl_max_ma[i] = dynamic_cfg->iclmax_ma[OPLUS_WLS_CHG_BATT_CL_LOW][i][m];
		}
	}
	for (n = 0; n < BATT_TEMP_INVALID; n++) {
		dynamic_cfg->iclmax_ma[OPLUS_WLS_CHG_BATT_CL_LOW][OPLUS_WLS_CHG_MODE_BPP][n] =
			dynamic_cfg->iclmax_ma[OPLUS_WLS_CHG_BATT_CL_LOW][OPLUS_WLS_CHG_MODE_BPP][n] *
			100 / dynamic_cfg->bpp_vol_mv * 10;
	}
	for (n = 0; n < BATT_TEMP_INVALID; n++) {
		dynamic_cfg->iclmax_ma[OPLUS_WLS_CHG_BATT_CL_LOW][OPLUS_WLS_CHG_MODE_EPP][n] =
			dynamic_cfg->iclmax_ma[OPLUS_WLS_CHG_BATT_CL_LOW][OPLUS_WLS_CHG_MODE_EPP][n] *
			100 / dynamic_cfg->epp_vol_mv * 10;
	}
	for (n = 0; n < BATT_TEMP_INVALID; n++) {
		dynamic_cfg->iclmax_ma[OPLUS_WLS_CHG_BATT_CL_LOW][OPLUS_WLS_CHG_MODE_EPP_PLUS][n] =
			dynamic_cfg->iclmax_ma[OPLUS_WLS_CHG_BATT_CL_LOW][OPLUS_WLS_CHG_MODE_EPP_PLUS][n] *
			100 / dynamic_cfg->epp_plus_vol_mv * 10;
	}
	for (n = 0; n < BATT_TEMP_INVALID; n++) {
		dynamic_cfg->iclmax_ma[OPLUS_WLS_CHG_BATT_CL_LOW][OPLUS_WLS_CHG_MODE_FAST][n] =
			dynamic_cfg->iclmax_ma[OPLUS_WLS_CHG_BATT_CL_LOW][OPLUS_WLS_CHG_MODE_FAST][n] *
			100 / dynamic_cfg->swarp_vol_mv * 10;
	}

	for (i = 0; i < OPLUS_WLS_CHG_MODE_MAX; i++) {
		pr_err("OPLUS_WLS_CHG_BATT_CL_LOW: %d %d %d %d %d %d %d %d\n",
			dynamic_cfg->iclmax_ma[OPLUS_WLS_CHG_BATT_CL_LOW][i][0],
			dynamic_cfg->iclmax_ma[OPLUS_WLS_CHG_BATT_CL_LOW][i][1],
			dynamic_cfg->iclmax_ma[OPLUS_WLS_CHG_BATT_CL_LOW][i][2],
			dynamic_cfg->iclmax_ma[OPLUS_WLS_CHG_BATT_CL_LOW][i][3],
			dynamic_cfg->iclmax_ma[OPLUS_WLS_CHG_BATT_CL_LOW][i][4],
			dynamic_cfg->iclmax_ma[OPLUS_WLS_CHG_BATT_CL_LOW][i][5],
			dynamic_cfg->iclmax_ma[OPLUS_WLS_CHG_BATT_CL_LOW][i][6],
			dynamic_cfg->iclmax_ma[OPLUS_WLS_CHG_BATT_CL_LOW][i][7]);
	}
	rc = read_unsigned_data_from_node(node, "oplus,iclmax-batt-high-ma",
		(u32 *)(&dynamic_cfg->iclmax_ma[OPLUS_WLS_CHG_BATT_CL_HIGH]),
		BATT_TEMP_INVALID * OPLUS_WLS_CHG_MODE_MAX);
	if (rc < 0) {
		pr_err("get oplus,iclmax-ma property error, rc=%d\n", rc);
		for (i = 0; i < OPLUS_WLS_CHG_MODE_MAX; i++) {
			for (m = 0; m < BATT_TEMP_INVALID; m++)
				dynamic_cfg->iclmax_ma[OPLUS_WLS_CHG_BATT_CL_HIGH][i][m] =
					default_config.iclmax_ma[OPLUS_WLS_CHG_BATT_CL_HIGH][i][m];
		}
	}
	for (i = 0; i < OPLUS_WLS_CHG_MODE_MAX; i++) {
		for (m = 0; m < BATT_TEMP_INVALID; m++) {
			if (dynamic_cfg->iclmax_ma[OPLUS_WLS_CHG_BATT_CL_HIGH][i][m] > wls_dev->icl_max_ma[i])
				wls_dev->icl_max_ma[i] = dynamic_cfg->iclmax_ma[OPLUS_WLS_CHG_BATT_CL_HIGH][i][m];
		}
	}
	for (n = 0; n < BATT_TEMP_INVALID; n++) {
		dynamic_cfg->iclmax_ma[OPLUS_WLS_CHG_BATT_CL_HIGH][OPLUS_WLS_CHG_MODE_BPP][n] =
			dynamic_cfg->iclmax_ma[OPLUS_WLS_CHG_BATT_CL_HIGH][OPLUS_WLS_CHG_MODE_BPP][n] *
			100 / dynamic_cfg->bpp_vol_mv * 10;
	}
	for (n = 0; n < BATT_TEMP_INVALID; n++) {
		dynamic_cfg->iclmax_ma[OPLUS_WLS_CHG_BATT_CL_HIGH][OPLUS_WLS_CHG_MODE_EPP][n] =
			dynamic_cfg->iclmax_ma[OPLUS_WLS_CHG_BATT_CL_HIGH][OPLUS_WLS_CHG_MODE_EPP][n] *
			100 / dynamic_cfg->epp_vol_mv * 10;
	}
	for (n = 0; n < BATT_TEMP_INVALID; n++) {
		dynamic_cfg->iclmax_ma[OPLUS_WLS_CHG_BATT_CL_HIGH][OPLUS_WLS_CHG_MODE_EPP_PLUS][n] =
			dynamic_cfg->iclmax_ma[OPLUS_WLS_CHG_BATT_CL_HIGH][OPLUS_WLS_CHG_MODE_EPP_PLUS][n] *
			100 / dynamic_cfg->epp_plus_vol_mv * 10;
	}
	for (n = 0; n < BATT_TEMP_INVALID; n++) {
		dynamic_cfg->iclmax_ma[OPLUS_WLS_CHG_BATT_CL_HIGH][OPLUS_WLS_CHG_MODE_FAST][n] =
			dynamic_cfg->iclmax_ma[OPLUS_WLS_CHG_BATT_CL_HIGH][OPLUS_WLS_CHG_MODE_FAST][n] *
			100 / dynamic_cfg->swarp_vol_mv * 10;
	}
	for (i = 0; i < OPLUS_WLS_CHG_MODE_MAX; i++) {
		pr_err("OPLUS_WLS_CHG_BATT_CL_HIGH: %d %d %d %d %d %d %d %d\n",
			dynamic_cfg->iclmax_ma[OPLUS_WLS_CHG_BATT_CL_HIGH][i][0],
			dynamic_cfg->iclmax_ma[OPLUS_WLS_CHG_BATT_CL_HIGH][i][1],
			dynamic_cfg->iclmax_ma[OPLUS_WLS_CHG_BATT_CL_HIGH][i][2],
			dynamic_cfg->iclmax_ma[OPLUS_WLS_CHG_BATT_CL_HIGH][i][3],
			dynamic_cfg->iclmax_ma[OPLUS_WLS_CHG_BATT_CL_HIGH][i][4],
			dynamic_cfg->iclmax_ma[OPLUS_WLS_CHG_BATT_CL_HIGH][i][5],
			dynamic_cfg->iclmax_ma[OPLUS_WLS_CHG_BATT_CL_HIGH][i][6],
			dynamic_cfg->iclmax_ma[OPLUS_WLS_CHG_BATT_CL_HIGH][i][7]);
	}
	skin_step = &wls_dev->epp_plus_skin_step;
	skin_step->skin_step = dynamic_cfg->epp_plus_skin_step;
	rc = read_skin_range_data_from_node(node, "oplus,epp_plus-skin-step",
					    skin_step->skin_step,
					    WLS_SKIN_TEMP_MAX,
					    wls_dev->icl_max_ma[OPLUS_WLS_CHG_MODE_EPP_PLUS]);
	if (rc < 0) {
		for (i = 0; i < WLS_MAX_STEP_CHG_ENTRIES; i++) {
			skin_step->skin_step[i].low_threshold =
				default_config.epp_plus_skin_step[i].low_threshold;
			skin_step->skin_step[i].high_threshold =
				default_config.epp_plus_skin_step[i].high_threshold;
			skin_step->skin_step[i].curr_ma =
				default_config.epp_plus_skin_step[i].curr_ma;
			if ((skin_step->skin_step[i].low_threshold == 0) &&
			    (skin_step->skin_step[i].high_threshold == 0) &&
			    (skin_step->skin_step[i].curr_ma == 0)) {
				skin_step->max_step = i;
				break;
			}
		}
	} else {
		skin_step->max_step = rc;
		for (i = 0; i < skin_step->max_step; i++)
			skin_step->skin_step[i].curr_ma =
				skin_step->skin_step[i].curr_ma * 100 / dynamic_cfg->epp_plus_vol_mv * 10;
	}
	for (i = 0; i < skin_step->max_step; i++) {
		pr_err("epp_plus-skin-step: %d %d %d\n",
			skin_step->skin_step[i].low_threshold,
			skin_step->skin_step[i].high_threshold,
			skin_step->skin_step[i].curr_ma);
	}

	skin_step = &wls_dev->epp_skin_step;
	skin_step->skin_step = dynamic_cfg->epp_skin_step;
	rc = read_skin_range_data_from_node(node, "oplus,epp-skin-step",
					    skin_step->skin_step,
					    WLS_SKIN_TEMP_MAX,
					    wls_dev->icl_max_ma[OPLUS_WLS_CHG_MODE_EPP]);
	if (rc < 0) {
		for (i = 0; i < WLS_MAX_STEP_CHG_ENTRIES; i++) {
			skin_step->skin_step[i].low_threshold =
				default_config.epp_skin_step[i].low_threshold;
			skin_step->skin_step[i].high_threshold =
				default_config.epp_skin_step[i].high_threshold;
			skin_step->skin_step[i].curr_ma =
				default_config.epp_skin_step[i].curr_ma;
			if ((skin_step->skin_step[i].low_threshold == 0) &&
			    (skin_step->skin_step[i].high_threshold == 0) &&
			    (skin_step->skin_step[i].curr_ma == 0)) {
				skin_step->max_step = i;
				break;
			}
		}
	} else {
		skin_step->max_step = rc;
		for (i = 0; i < skin_step->max_step; i++)
			skin_step->skin_step[i].curr_ma =
				skin_step->skin_step[i].curr_ma * 100 / dynamic_cfg->epp_vol_mv * 10;
	}
	for (i = 0; i < skin_step->max_step; i++) {
		pr_err("epp-skin-step: %d %d %d\n",
			skin_step->skin_step[i].low_threshold,
			skin_step->skin_step[i].high_threshold,
			skin_step->skin_step[i].curr_ma);
	}
	skin_step = &wls_dev->bpp_skin_step;
	skin_step->skin_step = dynamic_cfg->bpp_skin_step;
	rc = read_skin_range_data_from_node(node, "oplus,bpp-skin-step",
					    skin_step->skin_step,
					    WLS_SKIN_TEMP_MAX,
					    wls_dev->icl_max_ma[OPLUS_WLS_CHG_MODE_BPP]);
	if (rc < 0) {
		for (i = 0; i < WLS_MAX_STEP_CHG_ENTRIES; i++) {
			skin_step->skin_step[i].low_threshold =
				default_config.bpp_skin_step[i].low_threshold;
			skin_step->skin_step[i].high_threshold =
				default_config.bpp_skin_step[i].high_threshold;
			skin_step->skin_step[i].curr_ma =
				default_config.bpp_skin_step[i].curr_ma;
			if ((skin_step->skin_step[i].low_threshold == 0) &&
			    (skin_step->skin_step[i].high_threshold == 0) &&
			    (skin_step->skin_step[i].curr_ma == 0)) {
				skin_step->max_step = i;
				break;
			}
		}
	} else {
		skin_step->max_step = rc;
		for (i = 0; i < skin_step->max_step; i++)
			skin_step->skin_step[i].curr_ma =
				skin_step->skin_step[i].curr_ma * 100 / dynamic_cfg->bpp_vol_mv * 10;
	}
	for (i = 0; i < skin_step->max_step; i++) {
		pr_err("bpp-skin-step: %d %d %d\n",
			skin_step->skin_step[i].low_threshold,
			skin_step->skin_step[i].high_threshold,
			skin_step->skin_step[i].curr_ma);
	}
	skin_step = &wls_dev->epp_plus_led_on_skin_step;
	skin_step->skin_step = dynamic_cfg->epp_plus_led_on_skin_step;
	rc = read_skin_range_data_from_node(node, "oplus,epp_plus-led-on-skin-step",
					    skin_step->skin_step,
					    WLS_SKIN_TEMP_MAX,
					    wls_dev->icl_max_ma[OPLUS_WLS_CHG_MODE_EPP_PLUS]);
	if (rc < 0) {
		for (i = 0; i < WLS_MAX_STEP_CHG_ENTRIES; i++) {
			skin_step->skin_step[i].low_threshold =
				default_config.epp_plus_led_on_skin_step[i].low_threshold;
			skin_step->skin_step[i].high_threshold =
				default_config.epp_plus_led_on_skin_step[i].high_threshold;
			skin_step->skin_step[i].curr_ma =
				default_config.epp_plus_led_on_skin_step[i].curr_ma;
			if ((skin_step->skin_step[i].low_threshold == 0) &&
			    (skin_step->skin_step[i].high_threshold == 0) &&
			    (skin_step->skin_step[i].curr_ma == 0)) {
				skin_step->max_step = i;
				break;
			}
		}
	} else {
		skin_step->max_step = rc;
		for (i = 0; i < skin_step->max_step; i++)
			skin_step->skin_step[i].curr_ma =
				skin_step->skin_step[i].curr_ma * 100 / dynamic_cfg->epp_plus_vol_mv * 10;
	}
	for (i = 0; i < skin_step->max_step; i++) {
		pr_err("epp_plus-led-on-skin-step: %d %d %d\n",
			skin_step->skin_step[i].low_threshold,
			skin_step->skin_step[i].high_threshold,
			skin_step->skin_step[i].curr_ma);
	}
	skin_step = &wls_dev->epp_led_on_skin_step;
	skin_step->skin_step = dynamic_cfg->epp_led_on_skin_step;
	rc = read_skin_range_data_from_node(node, "oplus,epp-led-on-skin-step",
					    skin_step->skin_step,
					    WLS_SKIN_TEMP_MAX,
					    wls_dev->icl_max_ma[OPLUS_WLS_CHG_MODE_EPP]);
	if (rc < 0) {
		for (i = 0; i < WLS_MAX_STEP_CHG_ENTRIES; i++) {
			skin_step->skin_step[i].low_threshold =
				default_config.epp_led_on_skin_step[i].low_threshold;
			skin_step->skin_step[i].high_threshold =
				default_config.epp_led_on_skin_step[i].high_threshold;
			skin_step->skin_step[i].curr_ma =
				default_config.epp_led_on_skin_step[i].curr_ma;
			if ((skin_step->skin_step[i].low_threshold == 0) &&
			    (skin_step->skin_step[i].high_threshold == 0) &&
			    (skin_step->skin_step[i].curr_ma == 0)) {
				skin_step->max_step = i;
				break;
			}
		}
	} else {
		skin_step->max_step = rc;
		for (i = 0; i < skin_step->max_step; i++)
			skin_step->skin_step[i].curr_ma =
				skin_step->skin_step[i].curr_ma * 100 / dynamic_cfg->epp_vol_mv * 10;
	}
	for (i = 0; i < skin_step->max_step; i++) {
		pr_err("epp-led-on-skin-step: %d %d %d\n",
			skin_step->skin_step[i].low_threshold,
			skin_step->skin_step[i].high_threshold,
			skin_step->skin_step[i].curr_ma);
	}

	wls_dev->icl_max_ma[OPLUS_WLS_CHG_MODE_BPP] =
		wls_dev->icl_max_ma[OPLUS_WLS_CHG_MODE_BPP] * 100 / dynamic_cfg->bpp_vol_mv * 10;


	wls_dev->icl_max_ma[OPLUS_WLS_CHG_MODE_EPP] =
		wls_dev->icl_max_ma[OPLUS_WLS_CHG_MODE_EPP] * 100 / dynamic_cfg->epp_vol_mv * 10;


	wls_dev->icl_max_ma[OPLUS_WLS_CHG_MODE_EPP_PLUS] =
		wls_dev->icl_max_ma[OPLUS_WLS_CHG_MODE_EPP_PLUS] * 100 / dynamic_cfg->epp_plus_vol_mv * 10;


	wls_dev->icl_max_ma[OPLUS_WLS_CHG_MODE_FAST] =
		wls_dev->icl_max_ma[OPLUS_WLS_CHG_MODE_FAST] * 100 / dynamic_cfg->swarp_vol_mv * 10;


	pr_err("icl_max_ma BPP EPP EPP_PLUS FAST: %d %d %d %d\n",
		wls_dev->icl_max_ma[OPLUS_WLS_CHG_MODE_BPP],
		wls_dev->icl_max_ma[OPLUS_WLS_CHG_MODE_EPP],
		wls_dev->icl_max_ma[OPLUS_WLS_CHG_MODE_EPP_PLUS],
		wls_dev->icl_max_ma[OPLUS_WLS_CHG_MODE_FAST]);

	rc = read_chg_strategy_data_from_node(
		node, "oplus,wls-fast-chg-led-off-strategy-data",
		dynamic_cfg->wls_fast_chg_led_off_strategy_data);
	if (rc < 0) {
		pr_err("oplus,wls-fast-chg-led-off-strategy-data error, rc=%d\n", rc);
	}
	for (i = 0; i < rc; i++) {
		pr_err("pd9v_chg_led_off_strategy[%d]: %d %d %d %d %d\n", i,
			dynamic_cfg->wls_fast_chg_led_off_strategy_data[i].cool_temp,
			dynamic_cfg->wls_fast_chg_led_off_strategy_data[i].heat_temp,
			dynamic_cfg->wls_fast_chg_led_off_strategy_data[i].curr_data,
			dynamic_cfg->wls_fast_chg_led_off_strategy_data[i].heat_next_index,
			dynamic_cfg->wls_fast_chg_led_off_strategy_data[i].cool_next_index);
	}

	rc = read_chg_strategy_data_from_node(node, "oplus,wls-fast-chg-led-on-strategy-data", dynamic_cfg->wls_fast_chg_led_on_strategy_data);
	if (rc < 0) {
		pr_err("oplus,wls-fast-chg-led-on-strategy-data error, rc=%d\n", rc);
	}
	for (i = 0; i < rc; i++) {
		pr_err("pd9v_chg_led_on_strategy[%d]: %d %d %d %d %d\n", i,
			dynamic_cfg->wls_fast_chg_led_on_strategy_data[i].cool_temp,
			dynamic_cfg->wls_fast_chg_led_on_strategy_data[i].heat_temp,
			dynamic_cfg->wls_fast_chg_led_on_strategy_data[i].curr_data,
			dynamic_cfg->wls_fast_chg_led_on_strategy_data[i].heat_next_index,
			dynamic_cfg->wls_fast_chg_led_on_strategy_data[i].cool_next_index);
	}

	rc = of_property_read_u32(node, "oplus,wls-fast-chg-call-on-curr-ma",
				  &dynamic_cfg->wls_fast_chg_call_on_curr_ma);
	if (rc < 0) {
		pr_err("oplus,oplus,wls-fast-chg-call-on-curr-ma reading failed, rc=%d\n",
		       rc);
		dynamic_cfg->wls_fast_chg_call_on_curr_ma = 600;
	}
	rc = of_property_read_u32(node, "oplus,wls-fast-chg-camera-on-curr-ma",
				  &dynamic_cfg->wls_fast_chg_camera_on_curr_ma);
	if (rc < 0) {
		pr_err("oplus,oplus,wls-fast-chg-camera-on-curr-ma reading failed, rc=%d\n",
		       rc);
		dynamic_cfg->wls_fast_chg_camera_on_curr_ma = 600;
	}

	return 0;
}

static int oplus_chg_wls_gpio_init(struct oplus_chg_wls *wls_dev)
{
	int rc = 0;
	struct device_node *node = wls_dev->dev->of_node;

	wls_dev->pinctrl = devm_pinctrl_get(wls_dev->dev);
	if (IS_ERR_OR_NULL(wls_dev->pinctrl)) {
		pr_err("get pinctrl fail\n");
		return -ENODEV;
	}

	wls_dev->wrx_en_gpio = of_get_named_gpio(node, "oplus,wrx_en-gpio", 0);
	if (!gpio_is_valid(wls_dev->wrx_en_gpio)) {
		pr_err("wrx_en_gpio not specified\n");
		return -ENODEV;
	}
	rc = gpio_request(wls_dev->wrx_en_gpio, "wrx_en-gpio");
	if (rc < 0) {
		pr_err("wrx_en_gpio request error, rc=%d\n", rc);
		return rc;
	}
	wls_dev->wrx_en_active = pinctrl_lookup_state(wls_dev->pinctrl, "wrx_en_active");
	if (IS_ERR_OR_NULL(wls_dev->wrx_en_active)) {
		pr_err("get wrx_en_active fail\n");
		goto free_wrx_en_gpio;
	}
	wls_dev->wrx_en_sleep = pinctrl_lookup_state(wls_dev->pinctrl, "wrx_en_sleep");
	if (IS_ERR_OR_NULL(wls_dev->wrx_en_sleep)) {
		pr_err("get wrx_en_sleep fail\n");
		goto free_wrx_en_gpio;
	}
	gpio_direction_output(wls_dev->wrx_en_gpio, 0);
	pinctrl_select_state(wls_dev->pinctrl, wls_dev->wrx_en_sleep);

	if (!wls_dev->support_fastchg)
		return 0;

	wls_dev->cp_boost_en_gpio = of_get_named_gpio(node, "oplus,cp_boost_en-gpio", 0);
	if (!gpio_is_valid(wls_dev->cp_boost_en_gpio)) {
		pr_err("cp_boost_en_gpio not specified\n");
		goto free_wrx_en_gpio;
	}
	rc = gpio_request(wls_dev->cp_boost_en_gpio, "cp_boost_en-gpio");
	if (rc < 0) {
		pr_err("cp_boost_en_gpio request error, rc=%d\n", rc);
		goto free_wrx_en_gpio;
	}
	wls_dev->cp_boost_en_active = pinctrl_lookup_state(wls_dev->pinctrl, "cp_boost_en_active");
	if (IS_ERR_OR_NULL(wls_dev->cp_boost_en_active)) {
		pr_err("get cp_boost_en_active fail\n");
		goto free_cp_boost_en_gpio;
	}
	wls_dev->cp_boost_en_sleep = pinctrl_lookup_state(wls_dev->pinctrl, "cp_boost_en_sleep");
	if (IS_ERR_OR_NULL(wls_dev->cp_boost_en_sleep)) {
		pr_err("get cp_boost_en_sleep fail\n");
		goto free_cp_boost_en_gpio;
	}
	gpio_direction_output(wls_dev->cp_boost_en_gpio, 0);
	pinctrl_select_state(wls_dev->pinctrl, wls_dev->cp_boost_en_sleep);

	wls_dev->usb_int_gpio = of_get_named_gpio(node, "oplus,usb_int-gpio", 0);
	if (!gpio_is_valid(wls_dev->usb_int_gpio)) {
		pr_err("usb_int_gpio not specified\n");
		goto free_cp_boost_en_gpio;
	}
	rc = gpio_request(wls_dev->usb_int_gpio, "usb_int-gpio");
	if (rc < 0) {
		pr_err("usb_int_gpio request error, rc=%d\n", rc);
		goto free_cp_boost_en_gpio;
	}
	gpio_direction_input(wls_dev->usb_int_gpio);
	wls_dev->usb_int_irq = gpio_to_irq(wls_dev->usb_int_gpio);
	rc = devm_request_irq(wls_dev->dev, wls_dev->usb_int_irq,
			      oplus_chg_wls_usb_int_handler,
			      IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
			      "usb_int_irq", wls_dev);
	if (rc < 0) {
		pr_err("usb_int_irq request error, rc=%d\n", rc);
		goto free_int_gpio;
	}
	enable_irq_wake(wls_dev->usb_int_irq);

	return 0;

free_int_gpio:
	if (!gpio_is_valid(wls_dev->usb_int_gpio))
		gpio_free(wls_dev->usb_int_gpio);
free_cp_boost_en_gpio:
	if (!gpio_is_valid(wls_dev->cp_boost_en_gpio))
		gpio_free(wls_dev->cp_boost_en_gpio);
free_wrx_en_gpio:
	if (!gpio_is_valid(wls_dev->wrx_en_gpio))
		gpio_free(wls_dev->wrx_en_gpio);
	return rc;
}

#ifndef CONFIG_OPLUS_CHG_OOS
static ssize_t oplus_chg_wls_proc_deviated_read(struct file *file,
						char __user *buf, size_t count,
						loff_t *ppos)
{
	uint8_t ret = 0;
	char page[7];
	size_t len = 7;

	memset(page, 0, 7);
	len = snprintf(page, len, "%s\n", "false");
	ret = simple_read_from_buffer(buf, count, ppos, page, len);

	return ret;
}

static const struct file_operations oplus_chg_wls_proc_deviated_ops = {
	.read = oplus_chg_wls_proc_deviated_read,
	.write = NULL,
	.open = simple_open,
	.owner = THIS_MODULE,
};

static ssize_t oplus_chg_wls_proc_tx_read(struct file *file, char __user *buf,
					  size_t count, loff_t *ppos)
{
	uint8_t ret = 0;
	char page[10] = { 0 };
	struct oplus_chg_wls *wls_dev = PDE_DATA(file_inode(file));
	union oplus_chg_mod_propval val;
	int rc;

	if (wls_dev == NULL) {
		pr_err("wls dev is not fount\n");
		return -ENODEV;
	}

	rc = oplus_chg_mod_get_property(wls_dev->wls_ocm,
					OPLUS_CHG_PROP_TRX_STATUS, &val);
	if (rc < 0) {
		pr_err("can't get wls trx status, rc=%d\n", rc);
		return (ssize_t)rc;
	}
#ifndef CONFIG_OPLUS_CHG_OOS
	/*
	*When the wireless reverse charging error occurs,
	*the upper layer turns off the reverse charging button.
	*/
	if(wls_dev->wls_status.trx_close_delay) {
		val.intval = OPLUS_CHG_WLS_TRX_STATUS_ENABLE;
		schedule_delayed_work(&wls_dev->wls_clear_trx_work, msecs_to_jiffies(3000));
	}
#endif
	switch (val.intval) {
	case OPLUS_CHG_WLS_TRX_STATUS_ENABLE:
		snprintf(page, 10, "%s\n", "enable");
		break;
	case OPLUS_CHG_WLS_TRX_STATUS_CHARGING:
		snprintf(page, 10, "%s\n", "charging");
		break;
	case OPLUS_CHG_WLS_TRX_STATUS_DISENABLE:
	default:
		snprintf(page, 10, "%s\n", "disable");
		break;
	}

	ret = simple_read_from_buffer(buf, count, ppos, page, strlen(page));
	return ret;
}

static ssize_t oplus_chg_wls_proc_tx_write(struct file *file,
					   const char __user *buf, size_t count,
					   loff_t *lo)
{
	char buffer[5] = { 0 };
	struct oplus_chg_wls *wls_dev = PDE_DATA(file_inode(file));
	int val;
	int rc;

	if (wls_dev == NULL) {
		pr_err("wls dev is not fount\n");
		return -ENODEV;
	}

	if (count > 5) {
		return -EFAULT;
	}

	if (copy_from_user(buffer, buf, count)) {
		pr_err("%s: error.\n", __func__);
		return -EFAULT;
	}

	pr_err("buffer=%s", buffer);
	kstrtoint(buffer, 0, &val);
	pr_err("val = %d", val);
#ifndef CONFIG_OPLUS_CHG_OOS
	/*
	*When the wireless reverse charging error occurs,
	*the upper layer turns off the reverse charging button.
	*/
	wls_dev->wls_status.trx_close_delay = false;
#endif
	rc = oplus_chg_wls_set_trx_enable(wls_dev, !!val);
	if (rc < 0) {
		pr_err("can't enable trx, rc=%d\n", rc);
		return (ssize_t)rc;
	}
	return count;
}

static const struct file_operations oplus_chg_wls_proc_tx_ops = {
	.read = oplus_chg_wls_proc_tx_read,
	.write = oplus_chg_wls_proc_tx_write,
	.open = simple_open,
	.owner = THIS_MODULE,
};

static ssize_t oplus_chg_wls_proc_rx_read(struct file *file, char __user *buf,
					  size_t count, loff_t *ppos)
{
	uint8_t ret = 0;
	char page[3];
	struct oplus_chg_wls *wls_dev = PDE_DATA(file_inode(file));

	if (wls_dev == NULL) {
		pr_err("wls dev is not fount\n");
		return -ENODEV;
	}

	memset(page, 0, 3);
	snprintf(page, 3, "%c\n", wls_dev->charge_enable ? '1' : '0');
	ret = simple_read_from_buffer(buf, count, ppos, page, 3);
	return ret;
}

static ssize_t oplus_chg_wls_proc_rx_write(struct file *file,
					   const char __user *buf, size_t count,
					   loff_t *lo)
{
	char buffer[5] = { 0 };
	struct oplus_chg_wls *wls_dev = PDE_DATA(file_inode(file));
	int val;

	if (wls_dev == NULL) {
		pr_err("wls dev is not fount\n");
		return -ENODEV;
	}

	if (count > 5) {
		return -EFAULT;
	}

	if (copy_from_user(buffer, buf, count)) {
		pr_err("%s: error.\n", __func__);
		return -EFAULT;
	}

	pr_err("buffer=%s", buffer);
	kstrtoint(buffer, 0, &val);
	pr_err("val = %d", val);

	wls_dev->charge_enable = !!val;
	vote(wls_dev->rx_disable_votable, DEBUG_VOTER, !wls_dev->charge_enable,
	     1, false);
	pr_info("%s wls rx\n", wls_dev->charge_enable ? "enable" : "disable");
	return count;
}

static const struct file_operations oplus_chg_wls_proc_rx_ops = {
	.read = oplus_chg_wls_proc_rx_read,
	.write = oplus_chg_wls_proc_rx_write,
	.open = simple_open,
	.owner = THIS_MODULE,
};

static ssize_t oplus_chg_wls_proc_user_sleep_mode_read(struct file *file,
						       char __user *buf,
						       size_t count,
						       loff_t *ppos)
{
	uint8_t ret = 0;
	char page[10];
	struct oplus_chg_wls *wls_dev = PDE_DATA(file_inode(file));

	if (wls_dev == NULL) {
		pr_err("wls dev is not fount\n");
		return -ENODEV;
	}

	pr_err("quiet_mode_need = %d.\n", wls_dev->wls_status.quiet_mode_need);
	sprintf(page, "%d", wls_dev->wls_status.quiet_mode_need);
	ret = simple_read_from_buffer(buf, count, ppos, page, strlen(page));

	return ret;
}

#define FASTCHG_MODE 0
#define SILENT_MODE 1
#define BATTERY_FULL_MODE 2
#define CALL_MODE 598
#define EXIT_CALL_MODE 599
static ssize_t oplus_chg_wls_proc_user_sleep_mode_write(struct file *file,
							const char __user *buf,
							size_t len, loff_t *lo)
{
	char buffer[4] = { 0 };
	int pmw_pulse = 0;
	int rc = -1;
	struct oplus_chg_wls *wls_dev = PDE_DATA(file_inode(file));
	union oplus_chg_mod_propval pval;

	if (wls_dev == NULL) {
		pr_err("wls dev is not fount\n");
		return -ENODEV;
	}

	if (len > 4) {
		pr_err("len[%d] -EFAULT\n", len);
		return -EFAULT;
	}

	if (copy_from_user(buffer, buf, len)) {
		pr_err("copy from user error\n");
		return -EFAULT;
	}

	pr_err("user mode: buffer=%s\n", buffer);
	kstrtoint(buffer, 0, &pmw_pulse);
	if (pmw_pulse == FASTCHG_MODE) {
		pval.intval = 0;
		rc = oplus_chg_mod_set_property(wls_dev->wls_ocm,
				OPLUS_CHG_PROP_QUIET_MODE, &pval);
		if (rc == 0)
			wls_dev->wls_status.quiet_mode_need = FASTCHG_MODE;
		pr_err("set user mode: %d, fastchg mode, rc: %d\n", pmw_pulse,
		       rc);
	} else if (pmw_pulse == SILENT_MODE) {
		pval.intval = 1;
		rc = oplus_chg_mod_set_property(wls_dev->wls_ocm,
				OPLUS_CHG_PROP_QUIET_MODE, &pval);
		if (rc == 0)
			wls_dev->wls_status.quiet_mode_need = SILENT_MODE;
		pr_err("set user mode: %d, silent mode, rc: %d\n", pmw_pulse,
		       rc);
		//nu1619_set_dock_led_pwm_pulse(3);
	} else if (pmw_pulse == BATTERY_FULL_MODE) {
		pr_err("set user mode: %d, battery full mode\n", pmw_pulse);
		wls_dev->wls_status.quiet_mode_need = BATTERY_FULL_MODE;
		//nu1619_set_dock_fan_pwm_pulse(60);
	} else if (pmw_pulse == CALL_MODE) {
		pr_err("set user mode: %d, call mode\n", pmw_pulse);
		//chip->nu1619_chg_status.call_mode = true;
	} else if (pmw_pulse == EXIT_CALL_MODE) {
		//chip->nu1619_chg_status.call_mode = false;
		pr_err("set user mode: %d, exit call mode\n", pmw_pulse);
	} else {
		pr_err("user sleep mode: pmw_pulse: %d\n", pmw_pulse);
		wls_dev->wls_status.quiet_mode_need = pmw_pulse;
		//nu1619_set_dock_fan_pwm_pulse(pmw_pulse);
	}

	return len;
}

static const struct file_operations oplus_chg_wls_proc_user_sleep_mode_ops = {
	.read = oplus_chg_wls_proc_user_sleep_mode_read,
	.write = oplus_chg_wls_proc_user_sleep_mode_write,
	.open = simple_open,
	.owner = THIS_MODULE,
};

static ssize_t oplus_chg_wls_proc_idt_adc_test_read(struct file *file,
						    char __user *buf,
						    size_t count, loff_t *ppos)
{
	uint8_t ret = 0;
	char page[10];
	int rx_adc_result = 0;
	struct oplus_chg_wls *wls_dev = PDE_DATA(file_inode(file));

	if (wls_dev == NULL) {
		pr_err("wls dev is not fount\n");
		return -ENODEV;
	}

	if (wls_dev->wls_status.rx_adc_test_pass == true) {
		rx_adc_result = 1;
	} else {
		rx_adc_result = 0;
	}
	rx_adc_result = 1; // needn't test
	pr_err("rx_adc_test: result %d.\n", rx_adc_result);
	sprintf(page, "%d", rx_adc_result);
	ret = simple_read_from_buffer(buf, count, ppos, page, strlen(page));

	return ret;
}

static ssize_t oplus_chg_wls_proc_idt_adc_test_write(struct file *file,
						     const char __user *buf,
						     size_t len, loff_t *lo)
{
	char buffer[4] = { 0 };
	int rx_adc_cmd = 0;
	struct oplus_chg_wls *wls_dev = PDE_DATA(file_inode(file));

	if (wls_dev == NULL) {
		pr_err("wls dev is not fount\n");
		return -ENODEV;
	}

	if (len > 4) {
		pr_err("%s: len[%d] -EFAULT.\n", __func__, len);
		return -EFAULT;
	}

	if (copy_from_user(buffer, buf, len)) {
		pr_err("%s:  error.\n", __func__);
		return -EFAULT;
	}

	kstrtoint(buffer, 0, &rx_adc_cmd);
	if (rx_adc_cmd == 0) {
		pr_err("rx_adc_test: set 0.\n");
		wls_dev->wls_status.rx_adc_test_enable = false;
	} else if (rx_adc_cmd == 1) {
		pr_err("rx_adc_test: set 1.\n");
		wls_dev->wls_status.rx_adc_test_enable = true;
	} else {
		pr_err("rx_adc_test: set %d, invalid.\n", rx_adc_cmd);
	}

	return len;
}

static const struct file_operations oplus_chg_wls_proc_idt_adc_test_ops = {
	.read = oplus_chg_wls_proc_idt_adc_test_read,
	.write = oplus_chg_wls_proc_idt_adc_test_write,
	.open = simple_open,
	.owner = THIS_MODULE,
};

static ssize_t oplus_chg_wls_proc_rx_power_read(struct file *file,
						char __user *buf, size_t count,
						loff_t *ppos)
{
	uint8_t ret = 0;
	char page[16] = { 0 };
	int rx_power = 0;
	struct oplus_chg_wls *wls_dev = PDE_DATA(file_inode(file));

	if (wls_dev == NULL) {
		pr_err("wls dev is not fount\n");
		return -ENODEV;
	}

	rx_power = wls_dev->wls_status.rx_pwr_mw;
	if (wls_dev->wls_status.trx_online)
		rx_power = 1563;

	pr_err("rx_power = %d\n", rx_power);
	sprintf(page, "%d\n", rx_power);
	ret = simple_read_from_buffer(buf, count, ppos, page, strlen(page));

	return ret;
}

static ssize_t oplus_chg_wls_proc_rx_power_write(struct file *file,
						 const char __user *buf,
						 size_t count, loff_t *lo)
{
	return count;
}

static const struct file_operations oplus_chg_wls_proc_rx_power_ops = {
	.read = oplus_chg_wls_proc_rx_power_read,
	.write = oplus_chg_wls_proc_rx_power_write,
	.open = simple_open,
	.owner = THIS_MODULE,
};

static ssize_t oplus_chg_wls_proc_tx_power_read(struct file *file,
						char __user *buf, size_t count,
						loff_t *ppos)
{
	uint8_t ret = 0;
	char page[16] = { 0 };
	int tx_power = 0;
	struct oplus_chg_wls *wls_dev = PDE_DATA(file_inode(file));

	if (wls_dev == NULL) {
		pr_err("wls dev is not fount\n");
		return -ENODEV;
	}

	tx_power = wls_dev->wls_status.tx_pwr_mw;
	if (wls_dev->wls_status.trx_online)
		tx_power = wls_dev->wls_status.trx_vol_mv * wls_dev->wls_status.trx_curr_ma / 1000;

	pr_err("tx_power = %d\n", tx_power);
	sprintf(page, "%d\n", tx_power);
	ret = simple_read_from_buffer(buf, count, ppos, page, strlen(page));

	return ret;
}

static ssize_t oplus_chg_wls_proc_tx_power_write(struct file *file,
						 const char __user *buf,
						 size_t count, loff_t *lo)
{
	return count;
}

static const struct file_operations oplus_chg_wls_proc_tx_power_ops = {
	.read = oplus_chg_wls_proc_tx_power_read,
	.write = oplus_chg_wls_proc_tx_power_write,
	.open = simple_open,
	.owner = THIS_MODULE,
};

static ssize_t oplus_chg_wls_proc_rx_version_read(struct file *file,
						  char __user *buf,
						  size_t count, loff_t *ppos)
{
	uint8_t ret = 0;
	char page[16] = { 0 };
	u32 rx_version = 0;
	struct oplus_chg_wls *wls_dev = PDE_DATA(file_inode(file));
	int rc;

	if (wls_dev == NULL) {
		pr_err("wls dev is not fount\n");
		return -ENODEV;
	}

	rc = oplus_chg_wls_rx_get_version_by_img(wls_dev->wls_rx, &rx_version);
	if (rc < 0) {
		pr_err("can't get fw version by img, rc=%d\n", rc);
		return rc;
	}

	pr_err("rx_version = 0x%x\n", rx_version);
	sprintf(page, "0x%x\n", rx_version);
	ret = simple_read_from_buffer(buf, count, ppos, page, strlen(page));

	return ret;
}

static ssize_t oplus_chg_wls_proc_rx_version_write(struct file *file,
						   const char __user *buf,
						   size_t count, loff_t *lo)
{
	return count;
}

static const struct file_operations oplus_chg_wls_proc_rx_version_ops = {
	.read = oplus_chg_wls_proc_rx_version_read,
	.write = oplus_chg_wls_proc_rx_version_write,
	.open = simple_open,
	.owner = THIS_MODULE,
};

static ssize_t oplus_chg_wls_proc_ftm_mode_read(struct file *file,
						char __user *buf, size_t count,
						loff_t *ppos)
{
	uint8_t ret = 0;
	char page[256];
	struct oplus_chg_wls *wls_dev = PDE_DATA(file_inode(file));

	if (wls_dev == NULL) {
		pr_err("wls dev is not fount\n");
		return -ENODEV;
	}

	sprintf(page, "ftm_mode[%d], engineering_mode[%d]\n", wls_dev->ftm_mode,
		wls_dev->factory_mode);
	ret = simple_read_from_buffer(buf, count, ppos, page, strlen(page));

	return ret;
}

#define FTM_MODE_DISABLE 0
#define FTM_MODE_ENABLE 1
#define ENGINEERING_MODE_ENABLE 2
#define ENGINEERING_MODE_DISABLE 3
static ssize_t oplus_chg_wls_proc_ftm_mode_write(struct file *file,
						 const char __user *buf,
						 size_t len, loff_t *lo)
{
	char buffer[4] = { 0 };
	int ftm_mode = 0;
	struct oplus_chg_wls *wls_dev = PDE_DATA(file_inode(file));

	if (wls_dev == NULL) {
		pr_err("wls dev is not fount\n");
		return -ENODEV;
	}

	if (len > 4) {
		pr_err("len[%d] -EFAULT\n", len);
		return -EFAULT;
	}

	if (copy_from_user(buffer, buf, len)) {
		pr_err("copy from user error\n");
		return -EFAULT;
	}

	pr_err("ftm mode: buffer=%s\n", buffer);
	kstrtoint(buffer, 0, &ftm_mode);

	if (ftm_mode == FTM_MODE_DISABLE) {
		wls_dev->ftm_mode = false;
	} else if (ftm_mode == FTM_MODE_ENABLE) {
		wls_dev->ftm_mode = true;
	} else if (ftm_mode == ENGINEERING_MODE_ENABLE) {
		wls_dev->factory_mode = true;
	} else if (ftm_mode == ENGINEERING_MODE_DISABLE) {
		wls_dev->factory_mode = false;
	}

	return len;
}

static const struct file_operations oplus_chg_wls_proc_ftm_mode_ops = {
	.read = oplus_chg_wls_proc_ftm_mode_read,
	.write = oplus_chg_wls_proc_ftm_mode_write,
	.open = simple_open,
	.owner = THIS_MODULE,
};

static int oplus_chg_wls_init_charge_proc(struct oplus_chg_wls *wls_dev)
{
	int ret = 0;
	struct proc_dir_entry *prEntry_da = NULL;
	struct proc_dir_entry *prEntry_tmp = NULL;

	prEntry_da = proc_mkdir("wireless", NULL);
	if (prEntry_da == NULL) {
		pr_err("Couldn't create wireless proc entry\n");
		return -ENOMEM;
	}

	prEntry_tmp = proc_create_data("enable_tx", 0664, prEntry_da,
				       &oplus_chg_wls_proc_tx_ops, wls_dev);
	if (prEntry_tmp == NULL) {
		ret = -ENOMEM;
		pr_err("Couldn't create enable_tx proc entry\n");
		goto fail;
	}

	prEntry_tmp =
		proc_create_data("deviated", 0664, prEntry_da,
				 &oplus_chg_wls_proc_deviated_ops, wls_dev);
	if (prEntry_tmp == NULL) {
		ret = -ENOMEM;
		pr_err("Couldn't create deviated proc entry\n");
		goto fail;
	}

	prEntry_tmp = proc_create_data("user_sleep_mode", 0664, prEntry_da,
				       &oplus_chg_wls_proc_user_sleep_mode_ops,
				       wls_dev);
	if (prEntry_tmp == NULL) {
		ret = -ENOMEM;
		pr_err("Couldn't create user_sleep_mode proc entry\n");
		goto fail;
	}

	prEntry_tmp =
		proc_create_data("idt_adc_test", 0664, prEntry_da,
				 &oplus_chg_wls_proc_idt_adc_test_ops, wls_dev);
	if (prEntry_tmp == NULL) {
		ret = -ENOMEM;
		pr_err("Couldn't create idt_adc_test proc entry\n");
		goto fail;
	}

	prEntry_tmp = proc_create_data("enable_rx", 0664, prEntry_da,
				       &oplus_chg_wls_proc_rx_ops, wls_dev);
	if (prEntry_tmp == NULL) {
		ret = -ENOMEM;
		pr_err("Couldn't create enable_rx proc entry\n");
		goto fail;
	}

	prEntry_tmp =
		proc_create_data("rx_power", 0664, prEntry_da,
				 &oplus_chg_wls_proc_rx_power_ops, wls_dev);
	if (prEntry_tmp == NULL) {
		ret = -ENOMEM;
		pr_err("Couldn't create rx_power proc entry\n");
		goto fail;
	}

	prEntry_tmp =
		proc_create_data("tx_power", 0664, prEntry_da,
				 &oplus_chg_wls_proc_tx_power_ops, wls_dev);
	if (prEntry_tmp == NULL) {
		ret = -ENOMEM;
		pr_err("Couldn't create tx_power proc entry\n");
		goto fail;
	}

	prEntry_tmp =
		proc_create_data("rx_version", 0664, prEntry_da,
				 &oplus_chg_wls_proc_rx_version_ops, wls_dev);
	if (prEntry_tmp == NULL) {
		ret = -ENOMEM;
		pr_err("Couldn't create rx_version proc entry\n");
		goto fail;
	}

	prEntry_tmp =
		proc_create_data("ftm_mode", 0664, prEntry_da,
				 &oplus_chg_wls_proc_ftm_mode_ops, wls_dev);
	if (prEntry_tmp == NULL) {
		ret = -ENOMEM;
		pr_err("Couldn't create ftm_mode proc entry\n");
		goto fail;
	}

	return 0;

fail:
	remove_proc_entry("wireless", NULL);
	return ret;
}

#endif

static int oplus_chg_wls_driver_probe(struct platform_device *pdev)
{
	struct oplus_chg_wls *wls_dev;
	int boot_mode;
	bool usb_present;
	int rc;

	wls_dev = devm_kzalloc(&pdev->dev, sizeof(struct oplus_chg_wls), GFP_KERNEL);
	if (wls_dev == NULL) {
		pr_err("alloc memory error\n");
		return -ENOMEM;
	}

	wls_dev->dev = &pdev->dev;
	platform_set_drvdata(pdev, wls_dev);

	rc = oplus_chg_wls_parse_dt(wls_dev);
	if (rc < 0) {
		pr_err("oplus chg wls parse dts error, rc=%d\n", rc);
		goto parse_dt_err;
	}

	rc = oplus_chg_wls_init_mod(wls_dev);
	if (rc < 0) {
		pr_err("oplus chg wls mod init error, rc=%d\n", rc);
		goto wls_mod_init_err;
	}

	wls_dev->wls_wq = alloc_ordered_workqueue("wls_wq", WQ_FREEZABLE | WQ_HIGHPRI);
	if (!wls_dev->wls_wq) {
		pr_err("alloc wls work error\n");
		rc = -ENOMEM;
		goto alloc_work_err;
	}

	rc = oplus_chg_wls_rx_init(wls_dev);
	if (rc < 0) {
		goto rx_init_err;
	}
	rc = oplus_chg_wls_rx_register_msg_callback(wls_dev->wls_rx, wls_dev,
		oplus_chg_wls_rx_msg_callback);
	if (!rc)
		wls_dev->msg_callback_ok = true;
	rc = oplus_chg_wls_nor_init(wls_dev);
	if (rc < 0) {
		goto nor_init_err;
	}
	if (wls_dev->support_fastchg) {
		rc = oplus_chg_wls_fast_init(wls_dev);
		if (rc < 0) {
			goto fast_init_err;
		}
	}

	rc = oplus_chg_wls_gpio_init(wls_dev);
	if (rc < 0)
		goto gpio_init_err;

	wls_dev->fcc_votable = create_votable("WLS_FCC", VOTE_MIN,
				oplus_chg_wls_fcc_vote_callback,
				wls_dev);
	if (IS_ERR(wls_dev->fcc_votable)) {
		rc = PTR_ERR(wls_dev->fcc_votable);
		wls_dev->fcc_votable = NULL;
		goto create_fcc_votable_err;
	}

	wls_dev->fastchg_disable_votable = create_votable("WLS_FASTCHG_DISABLE",
				VOTE_SET_ANY,
				oplus_chg_wls_fastchg_disable_vote_callback,
				wls_dev);
	if (IS_ERR(wls_dev->fastchg_disable_votable)) {
		rc = PTR_ERR(wls_dev->fastchg_disable_votable);
		wls_dev->fastchg_disable_votable = NULL;
		goto create_disable_votable_err;
	}
	wls_dev->wrx_en_votable = create_votable("WLS_WRX_EN",
				VOTE_SET_ANY,
				oplus_chg_wls_wrx_en_vote_callback,
				wls_dev);
	if (IS_ERR(wls_dev->wrx_en_votable)) {
		rc = PTR_ERR(wls_dev->wrx_en_votable);
		wls_dev->wrx_en_votable = NULL;
		goto create_wrx_en_votable_err;
	}
	wls_dev->nor_icl_votable = create_votable("WLS_NOR_ICL",
				VOTE_MIN,
				oplus_chg_wls_nor_icl_vote_callback,
				wls_dev);
	if (IS_ERR(wls_dev->nor_icl_votable)) {
		rc = PTR_ERR(wls_dev->nor_icl_votable);
		wls_dev->nor_icl_votable = NULL;
		goto create_nor_icl_votable_err;
	}
	wls_dev->nor_fcc_votable = create_votable("WLS_NOR_FCC",
				VOTE_MIN,
				oplus_chg_wls_nor_fcc_vote_callback,
				wls_dev);
	if (IS_ERR(wls_dev->nor_fcc_votable)) {
		rc = PTR_ERR(wls_dev->nor_fcc_votable);
		wls_dev->nor_fcc_votable = NULL;
		goto create_nor_fcc_votable_err;
	}
	wls_dev->nor_fv_votable = create_votable("WLS_NOR_FV",
				VOTE_MIN,
				oplus_chg_wls_nor_fv_vote_callback,
				wls_dev);
	if (IS_ERR(wls_dev->nor_fv_votable)) {
		rc = PTR_ERR(wls_dev->nor_fv_votable);
		wls_dev->nor_fv_votable = NULL;
		goto create_nor_fv_votable_err;
	}
	wls_dev->nor_out_disable_votable = create_votable("WLS_NOR_OUT_DISABLE",
				VOTE_SET_ANY,
				oplus_chg_wls_nor_out_disable_vote_callback,
				wls_dev);
	if (IS_ERR(wls_dev->nor_out_disable_votable)) {
		rc = PTR_ERR(wls_dev->nor_out_disable_votable);
		wls_dev->nor_out_disable_votable = NULL;
		goto create_nor_out_disable_votable_err;
	}
	wls_dev->rx_disable_votable = create_votable("WLS_RX_DISABLE",
				VOTE_SET_ANY,
				oplus_chg_wls_rx_disable_vote_callback,
				wls_dev);
	if (IS_ERR(wls_dev->rx_disable_votable)) {
		rc = PTR_ERR(wls_dev->rx_disable_votable);
		wls_dev->rx_disable_votable = NULL;
		goto create_rx_disable_votable_err;
	}

	wls_dev->misc_dev.minor = MISC_DYNAMIC_MINOR;
	wls_dev->misc_dev.name = "wls_dev";
	wls_dev->misc_dev.fops = &oplus_chg_wls_dev_fops;
	rc = misc_register(&wls_dev->misc_dev);
	if (rc) {
		pr_err("misc_register failed, rc=%d\n", rc);
		goto misc_reg_err;
	}

#ifndef CONFIG_OPLUS_CHG_OOS
	rc = oplus_chg_wls_init_charge_proc(wls_dev);
	if (rc < 0) {
		pr_err("can't init charge proc, rc=%d\n", rc);
		goto init_charge_proc_err;
	}
#endif

	INIT_DELAYED_WORK(&wls_dev->wls_rx_sm_work, oplus_chg_wls_rx_sm);
	INIT_DELAYED_WORK(&wls_dev->wls_trx_sm_work, oplus_chg_wls_trx_sm);
	INIT_DELAYED_WORK(&wls_dev->wls_connect_work, oplus_chg_wls_connect_work);
	INIT_DELAYED_WORK(&wls_dev->wls_send_msg_work, oplus_chg_wls_send_msg_work);
	INIT_DELAYED_WORK(&wls_dev->wls_upgrade_fw_work, oplus_chg_wls_upgrade_fw_work);
	INIT_DELAYED_WORK(&wls_dev->usb_int_work, oplus_chg_wls_usb_int_work);
	INIT_DELAYED_WORK(&wls_dev->wls_data_update_work, oplus_chg_wls_data_update_work);
	INIT_DELAYED_WORK(&wls_dev->wls_trx_disconnect_work, oplus_chg_wls_trx_disconnect_work);
	INIT_DELAYED_WORK(&wls_dev->usb_connect_work, oplus_chg_wls_usb_connect_work);
	INIT_DELAYED_WORK(&wls_dev->fod_cal_work, oplus_chg_wls_fod_cal_work);
	INIT_DELAYED_WORK(&wls_dev->rx_restore_work, oplus_chg_wls_rx_restore_work);
	INIT_DELAYED_WORK(&wls_dev->rx_iic_restore_work, oplus_chg_wls_rx_iic_restore_work);
	INIT_DELAYED_WORK(&wls_dev->rx_restart_work, oplus_chg_wls_rx_restart_work);
	INIT_DELAYED_WORK(&wls_dev->online_keep_remove_work, oplus_chg_wls_online_keep_remove_work);
	INIT_DELAYED_WORK(&wls_dev->rx_verity_restore_work, oplus_chg_wls_rx_verity_restore_work);
	INIT_DELAYED_WORK(&wls_dev->verity_state_remove_work, oplus_chg_wls_verity_state_remove_work);
	INIT_DELAYED_WORK(&wls_dev->wls_verity_work, oplus_chg_wls_verity_work);
	INIT_DELAYED_WORK(&wls_dev->wls_get_third_part_verity_data_work,
		oplus_chg_wls_get_third_part_verity_data_work);
#ifndef CONFIG_OPLUS_CHG_OOS
	INIT_DELAYED_WORK(&wls_dev->wls_clear_trx_work, oplus_chg_wls_clear_trx_work);
#endif
	init_completion(&wls_dev->msg_ack);
	mutex_init(&wls_dev->connect_lock);
	mutex_init(&wls_dev->read_lock);
	mutex_init(&wls_dev->cmd_data_lock);
	mutex_init(&wls_dev->send_msg_lock);
	mutex_init(&wls_dev->update_data_lock);
	init_waitqueue_head(&wls_dev->read_wq);

	wls_dev->rx_wake_lock = wakeup_source_register(wls_dev->dev, "rx_wake_lock");
	wls_dev->trx_wake_lock = wakeup_source_register(wls_dev->dev, "trx_wake_lock");

	wls_dev->charge_enable = true;
	wls_dev->batt_charge_enable = true;

	boot_mode = get_boot_mode();
	if (boot_mode == MSM_BOOT_MODE__FACTORY)
		wls_dev->ftm_mode = true;

	usb_present = oplus_chg_wls_is_usb_present(wls_dev);
	if (usb_present) {
		wls_dev->usb_present = true;
		schedule_delayed_work(&wls_dev->usb_int_work, 0);
	} else {
		if (boot_mode == MSM_BOOT_MODE__CHARGE)
			wls_dev->wls_status.boot_online_keep = true;
		(void)oplus_chg_wls_rx_connect_check(wls_dev->wls_rx);
	}

	pr_info("probe done\n");

	return 0;

#ifndef CONFIG_OPLUS_CHG_OOS
init_charge_proc_err:
	misc_deregister(&wls_dev->misc_dev);
#endif
misc_reg_err:
	destroy_votable(wls_dev->rx_disable_votable);
create_rx_disable_votable_err:
	destroy_votable(wls_dev->nor_out_disable_votable);
create_nor_out_disable_votable_err:
	destroy_votable(wls_dev->nor_fv_votable);
create_nor_fv_votable_err:
	destroy_votable(wls_dev->nor_fcc_votable);
create_nor_fcc_votable_err:
	destroy_votable(wls_dev->nor_icl_votable);
create_nor_icl_votable_err:
	destroy_votable(wls_dev->wrx_en_votable);
create_wrx_en_votable_err:
	destroy_votable(wls_dev->fastchg_disable_votable);
create_disable_votable_err:
	destroy_votable(wls_dev->fcc_votable);
create_fcc_votable_err:
	disable_irq(wls_dev->usb_int_irq);
	if (!gpio_is_valid(wls_dev->usb_int_gpio))
		gpio_free(wls_dev->usb_int_gpio);
	if (!gpio_is_valid(wls_dev->cp_boost_en_gpio))
		gpio_free(wls_dev->cp_boost_en_gpio);
	if (!gpio_is_valid(wls_dev->wrx_en_gpio))
		gpio_free(wls_dev->wrx_en_gpio);
gpio_init_err:
	if (wls_dev->support_fastchg)
		oplus_chg_wls_fast_remove(wls_dev);
fast_init_err:
	oplus_chg_wls_nor_remove(wls_dev);
nor_init_err:
	oplus_chg_wls_rx_remove(wls_dev);
rx_init_err:
	destroy_workqueue(wls_dev->wls_wq);
alloc_work_err:
	oplus_chg_unreg_changed_notifier(&wls_dev->wls_changed_nb);
	oplus_chg_unreg_event_notifier(&wls_dev->wls_event_nb);
	oplus_chg_unreg_mod_notifier(wls_dev->wls_ocm, &wls_dev->wls_mod_nb);
	oplus_chg_mod_unregister(wls_dev->wls_ocm);
wls_mod_init_err:
parse_dt_err:
	devm_kfree(&pdev->dev, wls_dev);
	return rc;
}

static int oplus_chg_wls_driver_remove(struct platform_device *pdev)
{
	struct oplus_chg_wls *wls_dev = platform_get_drvdata(pdev);

#ifndef CONFIG_OPLUS_CHG_OOS
	remove_proc_entry("wireless", NULL);
#endif
	misc_deregister(&wls_dev->misc_dev);
	destroy_votable(wls_dev->rx_disable_votable);
	destroy_votable(wls_dev->nor_out_disable_votable);
	destroy_votable(wls_dev->nor_fv_votable);
	destroy_votable(wls_dev->nor_fcc_votable);
	destroy_votable(wls_dev->nor_icl_votable);
	destroy_votable(wls_dev->wrx_en_votable);
	destroy_votable(wls_dev->fastchg_disable_votable);
	destroy_votable(wls_dev->fcc_votable);
	disable_irq(wls_dev->usb_int_irq);
	if (!gpio_is_valid(wls_dev->usb_int_gpio))
		gpio_free(wls_dev->usb_int_gpio);
	if (!gpio_is_valid(wls_dev->cp_boost_en_gpio))
		gpio_free(wls_dev->cp_boost_en_gpio);
	if (!gpio_is_valid(wls_dev->wrx_en_gpio))
		gpio_free(wls_dev->wrx_en_gpio);
	if (wls_dev->support_fastchg)
		oplus_chg_wls_fast_remove(wls_dev);
	oplus_chg_wls_nor_remove(wls_dev);
	oplus_chg_wls_rx_remove(wls_dev);
	destroy_workqueue(wls_dev->wls_wq);
	oplus_chg_unreg_changed_notifier(&wls_dev->wls_changed_nb);
	oplus_chg_unreg_event_notifier(&wls_dev->wls_event_nb);
	oplus_chg_unreg_mod_notifier(wls_dev->wls_ocm, &wls_dev->wls_mod_nb);
	oplus_chg_mod_unregister(wls_dev->wls_ocm);
	devm_kfree(&pdev->dev, wls_dev);
	return 0;
}

static const struct of_device_id oplus_chg_wls_match[] = {
	{ .compatible = "oplus,wireless-charge" },
	{},
};

static struct platform_driver oplus_chg_wls_driver = {
	.driver = {
		.name = "oplus_chg_wls",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(oplus_chg_wls_match),
	},
	.probe = oplus_chg_wls_driver_probe,
	.remove = oplus_chg_wls_driver_remove,
};

static __init int oplus_chg_wls_driver_init(void)
{
	return platform_driver_register(&oplus_chg_wls_driver);
}

static __exit void oplus_chg_wls_driver_exit(void)
{
	platform_driver_unregister(&oplus_chg_wls_driver);
}

oplus_chg_module_register(oplus_chg_wls_driver);
