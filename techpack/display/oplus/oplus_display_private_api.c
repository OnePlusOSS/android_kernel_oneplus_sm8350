/***************************************************************
** Copyright (C),  2018,  OPLUS Mobile Comm Corp.,  Ltd
** File : oplus_display_private_api.h
** Description : oplus display private api implement
** Version : 1.0
** Date : 2018/03/20
** Author :
**
** ------------------------------- Revision History: -----------
**  <author>        <data>        <version >        <desc>
**   Hu.Jie          2018/03/20        1.0           Build this moudle
******************************************************************/
#include "oplus_display_private_api.h"
#include "oplus_onscreenfingerprint.h"
#include "oplus_aod.h"
#include "oplus_ffl.h"
#include "oplus_display_panel_power.h"
#include "oplus_display_panel_seed.h"
#include "oplus_display_panel_hbm.h"
#include "oplus_display_panel_common.h"

/*
 * we will create a sysfs which called /sys/kernel/oplus_display,
 * In that directory, oplus display private api can be called
 */
#include <linux/notifier.h>
#include <linux/msm_drm_notify.h>
/*#include "oplus_mm_kevent_fb.h"*/
#include <soc/oplus/device_info.h>
#if defined(OPLUS_FEATURE_PXLW_IRIS5)
#include <video/mipi_display.h>
#include "iris/dsi_iris5_api.h"
#include "iris/dsi_iris5_lightup.h"
#include "iris/dsi_iris5_loop_back.h"
#endif
#include "dsi_pwr.h"
#include "oplus_display_panel.h"

#ifdef OPLUS_BUG_STABILITY
#include "oplus_adfr.h"
#endif

extern int hbm_mode;
int spr_mode = 0;
int lcd_closebl_flag = 0;
int lcd_closebl_flag_fp = 0;
int oplus_request_power_status = 0;/*0:unknown 1:off 2:on 3:doze 4:doze suspend 5:vr 6:on suspend*/
int iris_recovery_check_state = -1;

int backlight_smooth_enable = 1;

extern int oplus_underbrightness_alpha;
extern int msm_drm_notifier_call_chain(unsigned long val, void *v);
int oplus_dimlayer_fingerprint_failcount = 0;
EXPORT_SYMBOL(oplus_dimlayer_fingerprint_failcount);
int oplus_dc2_alpha;
int oplus_dimlayer_bl_enable_v3 = 0;
int oplus_dimlayer_bl_enable_v2 = 0;
int oplus_dimlayer_bl_alpha_v2 = 260;
int oplus_dimlayer_bl_enable = 0;
int oplus_dimlayer_bl_alpha = 223;
int oplus_dimlayer_bl_alpha_value = 223;
int oplus_dimlayer_bl_enable_real = 0;
int oplus_dimlayer_dither_threshold = 0;
int oplus_dimlayer_dither_bitdepth = 6;
int oplus_dimlayer_bl_delay = -1;
int oplus_dimlayer_bl_delay_after = -1;
int oplus_boe_dc_min_backlight = 175;
int oplus_boe_dc_max_backlight = 3350;
int oplus_boe_max_backlight = 2050;
int oplus_dimlayer_bl_enable_v3_real;

int oplus_dimlayer_bl_enable_v2_real = 0;
bool oplus_skip_datadimming_sync = false;

extern int oplus_debug_max_brightness;
int oplus_seed_backlight = 0;
int oplus_seed_last_backlight = 0;
bool oplus_set_color_mode = false;
static bool is_set_seed = false;

ktime_t oplus_backlight_time;
u32 oplus_last_backlight = 0;
u32 oplus_backlight_delta = 0;

int oplus_dimlayer_hbm = 0;

/*#ifdef OPLUS_BUG_STABILITY*/
int dsi_cmd_log_enable = 0;
int dsi_cmd_panel_debug = 0;
/*#endif*/

EXPORT_SYMBOL(oplus_dimlayer_bl_alpha);
EXPORT_SYMBOL(oplus_dimlayer_bl_enable_real);
EXPORT_SYMBOL(oplus_last_backlight);
EXPORT_SYMBOL(oplus_seed_backlight);
EXPORT_SYMBOL(lcd_closebl_flag);
EXPORT_SYMBOL(oplus_request_power_status);
EXPORT_SYMBOL(lcd_closebl_flag_fp);
EXPORT_SYMBOL(oplus_backlight_delta);
EXPORT_SYMBOL(oplus_backlight_time);
EXPORT_SYMBOL(oplus_dimlayer_bl_alpha_value);
EXPORT_SYMBOL(oplus_dimlayer_bl_enable);
EXPORT_SYMBOL(oplus_dimlayer_hbm);
/*#ifdef OPLUS_BUG_STABILITY*/
EXPORT_SYMBOL(dsi_cmd_log_enable);
EXPORT_SYMBOL(backlight_smooth_enable);

extern PANEL_VOLTAGE_BAK panel_vol_bak[PANEL_VOLTAGE_ID_MAX];
extern u32 panel_pwr_vg_base;
extern int seed_mode;
extern int aod_light_mode;

#define PANEL_CMD_MIN_TX_COUNT 2
#define OPLUS_ATTR(_name, _mode, _show, _store) \
struct kobj_attribute oplus_attr_##_name = __ATTR(_name, _mode, _show, _store)

DEFINE_MUTEX(oplus_spr_lock);

int oplus_set_display_vendor(struct dsi_display *display)
{
	if (!display || !display->panel ||
			!display->panel->oplus_priv.vendor_name ||
			!display->panel->oplus_priv.manufacture_name) {
		pr_err("failed to config lcd proc device");
		return -EINVAL;
	}

	register_device_proc("lcd", (char *)display->panel->oplus_priv.vendor_name,
			     (char *)display->panel->oplus_priv.manufacture_name);

	return 0;
}
EXPORT_SYMBOL(oplus_set_display_vendor);

bool is_dsi_panel(struct drm_crtc *crtc)
{
	struct dsi_display *display = get_main_display();

	if (!display || !display->drm_conn || !display->drm_conn->state) {
		pr_err("failed to find dsi display\n");
		return false;
	}

	if (crtc != display->drm_conn->state->crtc) {
		return false;
	}

	return true;
}
EXPORT_SYMBOL(is_dsi_panel);

int dsi_panel_spr_mode(struct dsi_panel *panel, int mode)
{
	int rc = 0;

	if (!panel) {
		pr_err("Invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);

	if (!dsi_panel_initialized(panel)) {
		rc = -EINVAL;
		goto error;
	}

	switch (mode) {
	case 0:
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SPR_MODE0);

		if (rc) {
			pr_err("[%s] failed to send DSI_CMD_SPR_MODE0 cmds, rc=%d\n",
			       panel->name, rc);
		}

		break;

	case 1:
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SPR_MODE1);

		if (rc) {
			pr_err("[%s] failed to send DSI_CMD_SPR_MODE1 cmds, rc=%d\n",
			       panel->name, rc);
		}

		break;

	case 2:
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SPR_MODE2);

		if (rc) {
			pr_err("[%s] failed to send DSI_CMD_SEED_MODE2 cmds, rc=%d\n",
			       panel->name, rc);
		}

		break;

	default:
		rc = dsi_panel_tx_cmd_set(panel,
					  DSI_CMD_SPR_MODE0);

		if (rc) {
			pr_err("[%s] failed to send DSI_CMD_SPR_MODE0 cmds, rc=%d\n",
			       panel->name, rc);
		}

		pr_err("[%s] seed mode Invalid %d\n",
		       panel->name, mode);
	}

error:
	mutex_unlock(&panel->panel_lock);
	return rc;
}

int dsi_panel_read_panel_reg(struct dsi_display_ctrl *ctrl,
			     struct dsi_panel *panel, u8 cmd, void *rbuf,  size_t len)
{
	int rc = 0;
	struct dsi_cmd_desc cmdsreq;
	u32 flags = 0;

	if (!panel || !ctrl || !ctrl->ctrl) {
		return -EINVAL;
	}

	if (!dsi_ctrl_validate_host_state(ctrl->ctrl)) {
		return 1;
	}

	/* acquire panel_lock to make sure no commands are in progress */
	mutex_lock(&panel->panel_lock);

	if (!dsi_panel_initialized(panel)) {
		rc = -EINVAL;
		goto error;
	}

	memset(&cmdsreq, 0x0, sizeof(cmdsreq));
	cmdsreq.msg.type = 0x06;
	cmdsreq.msg.tx_buf = &cmd;
	cmdsreq.msg.tx_len = 1;
	cmdsreq.msg.rx_buf = rbuf;
	cmdsreq.msg.rx_len = len;
	cmdsreq.msg.flags |= MIPI_DSI_MSG_LASTCOMMAND;
	flags |= (DSI_CTRL_CMD_FETCH_MEMORY | DSI_CTRL_CMD_READ |
		  DSI_CTRL_CMD_CUSTOM_DMA_SCHED |
		  DSI_CTRL_CMD_LAST_COMMAND);

	rc = dsi_ctrl_cmd_transfer(ctrl->ctrl, &cmdsreq.msg, &flags);

	if (rc <= 0) {
		pr_err("%s, dsi_display_read_panel_reg rx cmd transfer failed rc=%d\n",
		       __func__,
		       rc);
		goto error;
	}

error:
	/* release panel_lock */
	mutex_unlock(&panel->panel_lock);
	return rc;
}

int dsi_display_spr_mode(struct dsi_display *display, int mode)
{
	int rc = 0;

	if (!display || !display->panel) {
		pr_err("Invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&display->display_lock);

	/* enable the clk vote for CMD mode panels */
	if (display->config.panel_mode == DSI_OP_CMD_MODE) {
		dsi_display_clk_ctrl(display->dsi_clk_handle,
				     DSI_CORE_CLK, DSI_CLK_ON);
	}

	rc = dsi_panel_spr_mode(display->panel, mode);

	if (rc) {
		pr_err("[%s] failed to dsi_panel_spr_on, rc=%d\n",
		       display->name, rc);
	}

	if (display->config.panel_mode == DSI_OP_CMD_MODE) {
		rc = dsi_display_clk_ctrl(display->dsi_clk_handle,
					  DSI_CORE_CLK, DSI_CLK_OFF);
	}

	mutex_unlock(&display->display_lock);
	return rc;
}

int dsi_display_read_panel_reg(struct dsi_display *display, u8 cmd, void *data,
			       size_t len)
{
	int rc = 0;
	struct dsi_display_ctrl *m_ctrl;

	if (!display || !display->panel || data == NULL) {
		pr_err("%s, Invalid params\n", __func__);
		return -EINVAL;
	}

	mutex_lock(&display->display_lock);
	if (is_set_seed && (get_oplus_display_power_status() != OPLUS_DISPLAY_POWER_ON)) {
		pr_err("%s:panel off\n", __func__);
		goto done;
	}

	m_ctrl = &display->ctrl[display->cmd_master_idx];

	if (display->tx_cmd_buf == NULL) {
		rc = dsi_host_alloc_cmd_tx_buffer(display);

		if (rc) {
			pr_err("%s, failed to allocate cmd tx buffer memory\n", __func__);
			goto done;
		}
	}

	rc = dsi_display_cmd_engine_enable(display);

	if (rc) {
		pr_err("%s, cmd engine enable failed\n", __func__);
		goto done;
	}

	/* enable the clk vote for CMD mode panels */
	if (display->config.panel_mode == DSI_OP_CMD_MODE) {
		dsi_display_clk_ctrl(display->dsi_clk_handle,
				     DSI_ALL_CLKS, DSI_CLK_ON);
	}

	rc = dsi_panel_read_panel_reg(m_ctrl, display->panel, cmd, data, len);

	if (rc < 0) {
		pr_err("%s, [%s] failed to read panel register, rc=%d,cmd=%d\n",
		       __func__,
		       display->name,
		       rc,
		       cmd);
	}

	if (display->config.panel_mode == DSI_OP_CMD_MODE) {
		rc = dsi_display_clk_ctrl(display->dsi_clk_handle,
					  DSI_ALL_CLKS, DSI_CLK_OFF);
	}

	dsi_display_cmd_engine_disable(display);

done:
	mutex_unlock(&display->display_lock);
	pr_err("%s, return: %d\n", __func__, rc);
	return rc;
}

static ssize_t oplus_display_set_hbm(struct kobject *obj,
		struct kobj_attribute *attr,
		const char *buf, size_t count)
{
	struct dsi_display *display = get_main_display();
	int temp_save = 0;
	int ret = 0;

	sscanf(buf, "%du", &temp_save);
	printk(KERN_INFO "%s oplus_display_set_hbm = %d\n", __func__, temp_save);

	if (get_oplus_display_power_status() != OPLUS_DISPLAY_POWER_ON) {
		printk(KERN_ERR
		       "%s oplus_display_set_hbm = %d, but now display panel status is not on\n",
		       __func__, temp_save);
		return -EFAULT;
	}

	if (!display) {
		printk(KERN_INFO "oplus_display_set_hbm and main display is null");
		return -EINVAL;
	}

	__oplus_display_set_hbm(temp_save);

	if ((!strcmp(display->panel->oplus_priv.vendor_name, "S6E3HC3")) ||
		(!strcmp(display->panel->oplus_priv.vendor_name, "AMB670YF01"))) {
		if((hbm_mode > 1) &&(hbm_mode <= 10)) {
			ret = dsi_display_normal_hbm_on(get_main_display());
		} else if(hbm_mode == 1) {
			ret = dsi_display_normal_hbm_on(get_main_display());
		} else if(hbm_mode == 0) {
			ret = dsi_display_hbm_off(get_main_display());
		} else if (hbm_mode == 4095) {
			ret = oplus_display_panel_hbm_lightspot_check();
		}
	} else {
		if ((hbm_mode > 1) && (hbm_mode <= 10)) {
			ret = dsi_display_normal_hbm_on(get_main_display());

		} else if (hbm_mode == 1) {
			ret = dsi_display_hbm_on(get_main_display());

		} else if (hbm_mode == 0) {
			ret = dsi_display_hbm_off(get_main_display());
		}
	}
	if (ret) {
		pr_err("failed to set hbm status ret=%d", ret);
		return ret;
	}

	return count;
}

static ssize_t oplus_display_set_seed(struct kobject *obj,
		struct kobj_attribute *attr,
		const char *buf, size_t count)
{
	int temp_save = 0;
	char reg[10] = {0x0};
	char value0[] = {0x5A, 0x5A};
	char value1[] = {0xA5, 0xA5};
	int i = 0;

	sscanf(buf, "%du", &temp_save);
	printk(KERN_INFO "%s oplus_display_set_seed = %d\n", __func__, temp_save);

	__oplus_display_set_seed(temp_save);

	if (get_oplus_display_power_status() == OPLUS_DISPLAY_POWER_ON) {
		if (get_main_display() == NULL) {
			printk(KERN_INFO "oplus_display_set_seed and main display is null");
			return count;
		}
		for (i = 0;i < 5;i++) {
			dsi_display_seed_mode(get_main_display(), seed_mode);
			if (seed_mode == 0 || strcmp(get_main_display()->panel->name, "samsung AMS643YE01 dsc cmd mode panel")) {
				break;
			}
			mipi_dsi_dcs_write(&(get_main_display()->panel->mipi_device), 0xF0, value0, sizeof(value0));
			is_set_seed = true;
			dsi_display_read_panel_reg(get_main_display(), 0x62, reg, 4);
			is_set_seed = false;
			mipi_dsi_dcs_write(&(get_main_display()->panel->mipi_device), 0xF0, value1, sizeof(value1));
			printk(KERN_INFO "reg[0]:%x,reg[1]:%x,reg[2]:%x,reg[3]:%x\n", reg[0], reg[1], reg[2], reg[3]);
			if (reg[1] == 0xb0) {
				break;
			}
		}

	} else {
		printk(KERN_ERR
		       "%s oplus_display_set_seed = %d, but now display panel status is not on\n",
		       __func__, temp_save);
	}

	return count;
}

static ssize_t oplus_set_aod_light_mode(struct kobject *obj,
		struct kobj_attribute *attr,
		const char *buf, size_t count)
{
	int temp_save = 0;

	sscanf(buf, "%du", &temp_save);

	__oplus_display_set_aod_light_mode(temp_save);
	oplus_update_aod_light_mode();

	return count;
}

int __oplus_display_set_spr(int mode)
{
	mutex_lock(&oplus_spr_lock);

	if (mode != spr_mode) {
		spr_mode = mode;
	}

	mutex_unlock(&oplus_spr_lock);
	return 0;
}
int oplus_dsi_update_spr_mode(void)
{
	struct dsi_display *display = get_main_display();
	int ret = 0;

	if (!display) {
		pr_err("failed for: %s %d\n", __func__, __LINE__);
		return -EINVAL;
	}

	ret = dsi_display_spr_mode(display, spr_mode);

	return ret;
}

static ssize_t oplus_display_set_spr(struct kobject *obj,
		struct kobj_attribute *attr,
		const char *buf, size_t count)
{
	int temp_save = 0;

	sscanf(buf, "%du", &temp_save);
	printk(KERN_INFO "%s oplus_display_set_spr = %d\n", __func__, temp_save);

	__oplus_display_set_spr(temp_save);

	if (get_oplus_display_power_status() == OPLUS_DISPLAY_POWER_ON) {
		if (get_main_display() == NULL) {
			printk(KERN_INFO "oplus_display_set_spr and main display is null");
			return count;
		}

		dsi_display_spr_mode(get_main_display(), spr_mode);

	} else {
		printk(KERN_ERR
		       "%s oplus_display_set_spr = %d, but now display panel status is not on\n",
		       __func__, temp_save);
	}

	return count;
}

int oplus_display_audio_ready = 0;
static ssize_t oplus_display_set_audio_ready(struct kobject *obj,
		struct kobj_attribute *attr,
		const char *buf, size_t count)
{
	sscanf(buf, "%du", &oplus_display_audio_ready);
	return count;
}

static ssize_t oplus_display_get_hbm(struct kobject *obj,
		struct kobj_attribute *attr, char *buf)
{
	printk(KERN_INFO "oplus_display_get_hbm = %d\n", hbm_mode);
	return sprintf(buf, "%d\n", hbm_mode);
}

static ssize_t oplus_display_get_seed(struct kobject *obj,
		struct kobj_attribute *attr, char *buf)
{
	printk(KERN_INFO "oplus_display_get_seed = %d\n", seed_mode);
	return sprintf(buf, "%d\n", seed_mode);
}

static ssize_t oplus_get_aod_light_mode(struct kobject *obj,
		struct kobj_attribute *attr, char *buf)
{
	printk(KERN_INFO "oplus_get_aod_light_mode = %d\n", aod_light_mode);
	return sprintf(buf, "%d\n", aod_light_mode);
}

static ssize_t oplus_display_get_spr(struct kobject *obj,
		struct kobj_attribute *attr, char *buf)
{
	printk(KERN_INFO "oplus_display_get_spr = %d\n", spr_mode);
	return sprintf(buf, "%d\n", spr_mode);
}

static ssize_t oplus_display_get_iris_state(struct kobject *obj,
		struct kobj_attribute *attr, char *buf)
{
#if defined(OPLUS_FEATURE_PXLW_IRIS5)
	if (iris_is_chip_supported() && iris_loop_back_validate() == 0) {
		iris_recovery_check_state = 0;
	}
	printk(KERN_INFO "oplus_display_get_iris_state = %d\n",
		iris_recovery_check_state);
#endif
	return sprintf(buf, "%d\n", iris_recovery_check_state);
}

static ssize_t oplus_display_regulator_control(struct kobject *obj,
		struct kobj_attribute *attr,
		const char *buf, size_t count)
{
	int temp_save = 0;
	struct dsi_display *temp_display;
	sscanf(buf, "%du", &temp_save);
	printk(KERN_INFO "%s oplus_display_regulator_control = %d\n", __func__,
	       temp_save);
	if (get_main_display() == NULL) {
		printk(KERN_INFO "oplus_display_regulator_control and main display is null");
		return count;
	}
	temp_display = get_main_display();
	if (temp_save == 0) {
#if defined(OPLUS_FEATURE_PXLW_IRIS5)
		if (iris_is_chip_supported()) {
			iris_control_pwr_regulator(false);
		}
#endif
		dsi_pwr_enable_regulator(&temp_display->panel->power_info, false);
	} else if (temp_save == 1) {
#if defined(OPLUS_FEATURE_PXLW_IRIS5)
		if (iris_is_chip_supported()) {
			iris_control_pwr_regulator(true);
		}
#endif
		dsi_pwr_enable_regulator(&temp_display->panel->power_info, true);
	}
	return count;
}

static char oplus_display_id = 0;

static ssize_t oplus_display_set_panel_serial_number(struct kobject *obj,
		struct kobj_attribute *attr,
		const char *buf, size_t count)
{
	sscanf(buf, "%x", &oplus_display_id);

	return count;
}

static ssize_t oplus_display_get_panel_serial_number(struct kobject *obj,
		struct kobj_attribute *attr, char *buf)
{
	int ret = 0;
	unsigned char read[30];
	PANEL_SERIAL_INFO panel_serial_info;
	uint64_t serial_number;
	struct dsi_display *display = get_main_display();
	int i;

	if (!display) {
		printk(KERN_INFO
		       "oplus_display_get_panel_serial_number and main display is null");
		return -1;
	}

	if (0 == oplus_display_id && display->enabled == false) {
		pr_err("%s main panel is disabled");
		return -1;
	}

	if (1 == oplus_display_id) {
		oplus_display_id = 0;
		display = get_sec_display();
		if (!display) {
			pr_info("%s sec display is null\n", __func__);
			return -1;
		}
		if (display->enabled == false) {
			pr_info("%s second panel is disabled", __func__);
			return -1;
		}
	}

	if (get_oplus_display_power_status() != OPLUS_DISPLAY_POWER_ON) {
		printk(KERN_ERR"%s display panel in off status\n", __func__);
		return ret;
	}

	/*
	 * for some unknown reason, the panel_serial_info may read dummy,
	 * retry when found panel_serial_info is abnormal.
	 */
	for (i = 0; i < 10; i++) {
		if (!strcmp(display->panel->name, "samsung amb655x fhd cmd mode dsc dsi panel")
			|| !strcmp(display->panel->name, "samsung SOFE03F dsc cmd mode panel")) {
			ret = dsi_display_read_panel_reg(get_main_display(), 0xA1, read, 18);
		} else if (!strcmp(display->panel->oplus_priv.vendor_name, "S6E3XA1")
					|| !strcmp(display->panel->name, "samsung AMS643YE01 dsc cmd mode panel")) {
			mutex_lock(&display->display_lock);
			mutex_lock(&display->panel->panel_lock);

			if (display->panel->panel_initialized) {
				if (display->config.panel_mode == DSI_OP_CMD_MODE) {
					dsi_display_clk_ctrl(display->dsi_clk_handle, DSI_ALL_CLKS, DSI_CLK_ON);
				}
			        {
					char value[] = {0x5A, 0x5A};
					ret = mipi_dsi_dcs_write(&display->panel->mipi_device, 0xF0, value, sizeof(value));
				}
				if (display->config.panel_mode == DSI_OP_CMD_MODE) {
					dsi_display_clk_ctrl(display->dsi_clk_handle, DSI_ALL_CLKS, DSI_CLK_OFF);
				}
			}
			mutex_unlock(&display->panel->panel_lock);
			mutex_unlock(&display->display_lock);

			if (ret < 0) {
				ret = scnprintf(buf, PAGE_SIZE, "Get panel serial number failed, reason:%d", ret);
				msleep(20);
				continue;
			}
			ret = dsi_display_read_panel_reg(get_main_display(), 0xD8, read, 22);
		} else if (!strcmp(display->panel->oplus_priv.vendor_name, "NT37701")) {
				ret = dsi_display_read_panel_reg(display, 0xA3, read, 8);
		}
		else
			ret = dsi_display_read_panel_reg(get_main_display(), 0xA1, read, 11);

		if (ret < 0) {
			ret = scnprintf(buf, PAGE_SIZE,
					"Get panel serial number failed, reason:%d", ret);
			msleep(20);
			continue;
		}

		/*  0xA1               11th        12th    13th    14th    15th
		 *  HEX                0x32        0x0C    0x0B    0x29    0x37
		 *  Bit           [D7:D4][D3:D0] [D5:D0] [D5:D0] [D5:D0] [D5:D0]
		 *  exp              3      2       C       B       29      37
		 *  Yyyy,mm,dd      2014   2m      12d     11h     41min   55sec
		*/
		if (!strcmp(display->panel->name, "samsung amb655x fhd cmd mode dsc dsi panel")
			 || !strcmp(display->panel->name, "samsung SOFE03F dsc cmd mode panel")) {
			panel_serial_info.reg_index = 11;
		} else if (!strcmp(display->panel->oplus_priv.vendor_name, "S6E3XA1")) {
			panel_serial_info.reg_index = 15;
		} else if (!strcmp(display->panel->oplus_priv.vendor_name, "NT37701")) {
			panel_serial_info.reg_index = 0;
		} else if (!strcmp(display->panel->name, "samsung AMS643YE01 dsc cmd mode panel")) {
			panel_serial_info.reg_index = 7;
		}
		else
			panel_serial_info.reg_index = 4;

		panel_serial_info.year		= (read[panel_serial_info.reg_index] & 0xF0) >> 0x4;
		if (!strcmp(display->panel->oplus_priv.vendor_name, "NT37701")) {
			panel_serial_info.year += 1;
		}
		panel_serial_info.month		= read[panel_serial_info.reg_index]	& 0x0F;
		panel_serial_info.day		= read[panel_serial_info.reg_index + 1]	& 0x1F;
		panel_serial_info.hour		= read[panel_serial_info.reg_index + 2]	& 0x1F;
		panel_serial_info.minute	= read[panel_serial_info.reg_index + 3]	& 0x3F;
		panel_serial_info.second	= read[panel_serial_info.reg_index + 4]	& 0x3F;
		panel_serial_info.reserved[0] = read[panel_serial_info.reg_index + 5];
		panel_serial_info.reserved[1] = read[panel_serial_info.reg_index + 6];

		serial_number = (panel_serial_info.year		<< 56)\
				+ (panel_serial_info.month		<< 48)\
				+ (panel_serial_info.day		<< 40)\
				+ (panel_serial_info.hour		<< 32)\
				+ (panel_serial_info.minute	<< 24)\
				+ (panel_serial_info.second	<< 16)\
				+ (panel_serial_info.reserved[0] << 8)\
				+ (panel_serial_info.reserved[1]);

		if (!panel_serial_info.year) {
			/*
			 * the panel we use always large than 2011, so
			 * force retry when year is 2011
			 */
			msleep(20);
			continue;
		}

		ret = scnprintf(buf, PAGE_SIZE, "Get panel serial number: %llx\n",
				serial_number);
		break;
	}

	return ret;
}

static char oplus_rx_reg[PANEL_TX_MAX_BUF] = {0x0};
static char oplus_rx_len = 0;
static ssize_t oplus_display_get_panel_reg(struct kobject *obj,
		struct kobj_attribute *attr, char *buf)
{
	struct dsi_display *display = get_main_display();
	int i, cnt = 0;

	if(1 == dsi_cmd_panel_debug)
		display = get_sec_display();

	if (!display) {
		return -EINVAL;
	}

	mutex_lock(&display->display_lock);

	for (i = 0; i < oplus_rx_len; i++)
		cnt += snprintf(buf + cnt, PANEL_TX_MAX_BUF - cnt,
				"%02x ", oplus_rx_reg[i]);

	cnt += snprintf(buf + cnt, PANEL_TX_MAX_BUF - cnt, "\n");
	mutex_unlock(&display->display_lock);

	return cnt;
}

static ssize_t oplus_display_set_panel_reg(struct kobject *obj,
		struct kobj_attribute *attr,
		const char *buf, size_t count)
{
	char reg[PANEL_TX_MAX_BUF] = {0x0};
	char payload[PANEL_TX_MAX_BUF] = {0x0};
	u32 index = 0, value = 0, step = 0;
	int ret = 0;
	int len = 0;
	char *bufp = (char *)buf;
	struct dsi_display *display = get_main_display();
	char read;

	if(1 == dsi_cmd_panel_debug)
		display = get_sec_display();

	if (!display || !display->panel) {
		pr_err("debug for: %s %d\n", __func__, __LINE__);
		return -EFAULT;
	}

	if (sscanf(bufp, "%c%n", &read, &step) && read == 'r') {
		bufp += step;
		sscanf(bufp, "%x %d", &value, &len);

		if (len > PANEL_TX_MAX_BUF) {
			pr_err("failed\n");
			return -EINVAL;
		}

		dsi_display_read_panel_reg(display, value, reg, len);

		for (index = 0; index < len; index++) {
			printk("%x ", reg[index]);
		}

		mutex_lock(&display->display_lock);
		memcpy(oplus_rx_reg, reg, PANEL_TX_MAX_BUF);
		oplus_rx_len = len;
		mutex_unlock(&display->display_lock);
		return count;
	}

	while (sscanf(bufp, "%x%n", &value, &step) > 0) {
		reg[len++] = value;

		if (len >= PANEL_TX_MAX_BUF) {
			pr_err("wrong input reg len\n");
			return -EFAULT;
		}

		bufp += step;
	}

	for (index = 0; index < len; index ++) {
		payload[index] = reg[index + 1];
	}

	if (get_oplus_display_power_status() == OPLUS_DISPLAY_POWER_ON) {
		/* enable the clk vote for CMD mode panels */
		mutex_lock(&display->display_lock);
		mutex_lock(&display->panel->panel_lock);

		if (display->panel->panel_initialized) {
			if (display->config.panel_mode == DSI_OP_CMD_MODE) {
				dsi_display_clk_ctrl(display->dsi_clk_handle,
						     DSI_ALL_CLKS, DSI_CLK_ON);
			}

			ret = mipi_dsi_dcs_write(&display->panel->mipi_device, reg[0],
						 payload, len - 1);

			if (display->config.panel_mode == DSI_OP_CMD_MODE) {
				dsi_display_clk_ctrl(display->dsi_clk_handle,
						     DSI_ALL_CLKS, DSI_CLK_OFF);
			}
		}

		mutex_unlock(&display->panel->panel_lock);
		mutex_unlock(&display->display_lock);

		if (ret < 0) {
			return ret;
		}
	}

	return count;
}

static ssize_t oplus_display_get_panel_id(struct kobject *obj,
		struct kobj_attribute *attr, char *buf)
{
	struct dsi_display *display = get_main_display();
	int ret = 0;
	unsigned char read[30];
	char DA = 0;
	char DB = 0;
	char DC = 0;

	if (get_oplus_display_power_status() == OPLUS_DISPLAY_POWER_ON) {
		if (display == NULL) {
			printk(KERN_INFO "oplus_display_get_panel_id and main display is null");
			ret = -1;
			return ret;
		}

		ret = dsi_display_read_panel_reg(display, 0xDA, read, 1);

		if (ret < 0) {
			pr_err("failed to read DA ret=%d\n", ret);
			return -EINVAL;
		}

		DA = read[0];

		ret = dsi_display_read_panel_reg(display, 0xDB, read, 1);

		if (ret < 0) {
			pr_err("failed to read DA ret=%d\n", ret);
			return -EINVAL;
		}

		DB = read[0];

		ret = dsi_display_read_panel_reg(display, 0xDC, read, 1);

		if (ret < 0) {
			pr_err("failed to read DA ret=%d\n", ret);
			return -EINVAL;
		}

		DC = read[0];
		ret = scnprintf(buf, PAGE_SIZE, "%02x %02x %02x\n", DA, DB, DC);

	} else {
		printk(KERN_ERR
		       "%s oplus_display_get_panel_id, but now display panel status is not on\n",
		       __func__);
	}

	return ret;
}

static ssize_t oplus_display_get_panel_dsc(struct kobject *obj,
		struct kobj_attribute *attr, char *buf)
{
	int ret = 0;
	unsigned char read[30];

	if (get_oplus_display_power_status() == OPLUS_DISPLAY_POWER_ON) {
		if (get_main_display() == NULL) {
			printk(KERN_INFO "oplus_display_get_panel_dsc and main display is null");
			ret = -1;
			return ret;
		}

		ret = dsi_display_read_panel_reg(get_main_display(), 0x03, read, 1);

		if (ret < 0) {
			ret = scnprintf(buf, PAGE_SIZE, "oplus_display_get_panel_dsc failed, reason:%d",
					ret);

		} else {
			ret = scnprintf(buf, PAGE_SIZE, "oplus_display_get_panel_dsc: 0x%x\n", read[0]);
		}

	} else {
		printk(KERN_ERR
		       "%s oplus_display_get_panel_dsc, but now display panel status is not on\n",
		       __func__);
	}

	return ret;
}

static ssize_t oplus_display_dump_info(struct kobject *obj,
		struct kobj_attribute *attr, char *buf)
{
	int ret = 0;
	struct dsi_display *temp_display;

	temp_display = get_main_display();

	if (temp_display == NULL) {
		printk(KERN_INFO "oplus_display_dump_info and main display is null");
		ret = -1;
		return ret;
	}

	if (temp_display->modes == NULL) {
		printk(KERN_INFO "oplus_display_dump_info and display modes is null");
		ret = -1;
		return ret;
	}

	ret = scnprintf(buf, PAGE_SIZE,
			"oplus_display_dump_info: height =%d,width=%d,frame_rate=%d,clk_rate=%llu\n",
			temp_display->modes->timing.h_active, temp_display->modes->timing.v_active,
			temp_display->modes->timing.refresh_rate,
			temp_display->modes->timing.clk_rate_hz);

	return ret;
}

static ssize_t oplus_display_get_power_status(struct kobject *obj,
		struct kobj_attribute *attr, char *buf)
{

	printk(KERN_INFO "oplus_display_get_power_status = %d, request power :%d\n",
			get_oplus_display_power_status(), oplus_request_power_status);

	return sprintf(buf, "kernel power :%d   request power :%d\n",
			get_oplus_display_power_status(), oplus_request_power_status);
}

static ssize_t oplus_display_set_power_status(struct kobject *obj,
		struct kobj_attribute *attr,
		const char *buf, size_t count)
{
	int temp_save = 0;

	sscanf(buf, "%du", &temp_save);
	printk(KERN_INFO "%s oplus_display_set_power_status = %d\n", __func__,
	       temp_save);

	__oplus_display_set_power_status(temp_save);

	return count;
}

static ssize_t oplus_display_get_closebl_flag(struct kobject *obj,
	struct kobj_attribute *attr, char *buf)
{
	printk(KERN_INFO "oplus_display_get_closebl_flag = %d\n", lcd_closebl_flag);
	return sprintf(buf, "%d\n", lcd_closebl_flag);
}

static ssize_t oplus_display_set_closebl_flag(struct kobject *obj,
		struct kobj_attribute *attr,
		const char *buf, size_t count)
{
	int closebl = 0;
	sscanf(buf, "%du", &closebl);
	pr_err("lcd_closebl_flag = %d\n", closebl);

	if (1 != closebl) {
		lcd_closebl_flag = 0;
	}

	pr_err("oplus_display_set_closebl_flag = %d\n", lcd_closebl_flag);
	return count;
}

static ssize_t oplus_backlight_smooth_get_debug(struct kobject *obj,
	struct kobj_attribute *attr, char *buf)
{
	pr_info("oplus_backlight_smooth_get_debug = %d\n", backlight_smooth_enable);
	return sprintf(buf, "%d\n", backlight_smooth_enable);
}

static ssize_t oplus_backlight_smooth_set_debug(struct kobject *obj,
		struct kobj_attribute *attr,
		const char *buf, size_t count)
{
	int bk_feature = 0;
	sscanf(buf, "%du", &bk_feature);
	pr_err("backlight_smooth_enable = %d\n", bk_feature);

	if (1 != bk_feature) {
		backlight_smooth_enable = 0;
	} else {
		backlight_smooth_enable = 1;
	}

	pr_info("oplus_backlight_smooth_set_debug = %d\n", backlight_smooth_enable);
	return count;
}

extern const char *cmd_set_prop_map[];
static ssize_t oplus_display_get_dsi_command(struct kobject *obj,
		struct kobj_attribute *attr, char *buf)
{
	int i, cnt;

	cnt = snprintf(buf, PAGE_SIZE,
		       "read current dsi_cmd:\n"
		       "    echo dump > dsi_cmd  - then you can find dsi cmd on kmsg\n"
		       "set sence dsi cmd:\n"
		       "  example hbm on:\n"
		       "    echo qcom,mdss-dsi-hbm-on-command > dsi_cmd\n"
		       "    echo [dsi cmd0] > dsi_cmd\n"
		       "    echo [dsi cmd1] > dsi_cmd\n"
		       "    echo [dsi cmdX] > dsi_cmd\n"
		       "    echo flush > dsi_cmd\n"
		       "available dsi_cmd sences:\n");

	for (i = 0; i < DSI_CMD_SET_MAX; i++)
		cnt += snprintf(buf + cnt, PAGE_SIZE - cnt,
				"    %s\n", cmd_set_prop_map[i]);

	return cnt;
}
static int oplus_display_dump_dsi_command(struct dsi_display *display)
{
	struct dsi_display_mode *mode;
	struct dsi_display_mode_priv_info *priv_info;
	struct dsi_panel_cmd_set *cmd_sets;
	enum dsi_cmd_set_state state;
	struct dsi_cmd_desc *cmds;
	const char *cmd_name;
	int i, j, k, cnt = 0;
	const u8 *tx_buf;
	char bufs[SZ_256];

	if (!display || !display->panel || !display->panel->cur_mode) {
		pr_err("failed to get main dsi display\n");
		return -EFAULT;
	}

	mode = display->panel->cur_mode;

	if (!mode || !mode->priv_info) {
		pr_err("failed to get dsi display mode\n");
		return -EFAULT;
	}

	priv_info = mode->priv_info;
	cmd_sets = priv_info->cmd_sets;

	for (i = 0; i < DSI_CMD_SET_MAX; i++) {
		cmd_name = cmd_set_prop_map[i];

		if (!cmd_name) {
			continue;
		}

		state = cmd_sets[i].state;
		pr_err("%s: %s", cmd_name, state == DSI_CMD_SET_STATE_LP ?
		       "dsi_lp_mode" : "dsi_hs_mode");

		for (j = 0; j < cmd_sets[i].count; j++) {
			cmds = &cmd_sets[i].cmds[j];
			tx_buf = cmds->msg.tx_buf;
			cnt = snprintf(bufs, SZ_256,
				       " %02x %02x %02x %02x %02x %02x %02x",
				       cmds->msg.type, cmds->last_command,
				       cmds->msg.channel,
				       cmds->msg.flags == MIPI_DSI_MSG_REQ_ACK,
				       cmds->post_wait_ms,
				       (int)(cmds->msg.tx_len >> 8),
				       (int)(cmds->msg.tx_len & 0xff));

			for (k = 0; k < cmds->msg.tx_len; k++)
				cnt += snprintf(bufs + cnt,
						SZ_256 > cnt ? SZ_256 - cnt : 0,
						" %02x", tx_buf[k]);

			pr_err("%s", bufs);
		}
	}

	return 0;
}

static int oplus_dsi_panel_get_cmd_pkt_count(const char *data, u32 length,
		u32 *cnt)
{
	const u32 cmd_set_min_size = 7;
	u32 count = 0;
	u32 packet_length;
	u32 tmp;

	while (length >= cmd_set_min_size) {
		packet_length = cmd_set_min_size;
		tmp = ((data[5] << 8) | (data[6]));
		packet_length += tmp;

		if (packet_length > length) {
			pr_err("format error packet_length[%d] length[%d] count[%d]\n",
			       packet_length, length, count);
			return -EINVAL;
		}

		length -= packet_length;
		data += packet_length;
		count++;
	}

	*cnt = count;

	return 0;
}

static int oplus_dsi_panel_create_cmd_packets(const char *data,
		u32 length,
		u32 count,
		struct dsi_cmd_desc *cmd)
{
	int rc = 0;
	int i, j;
	u8 *payload;

	for (i = 0; i < count; i++) {
		u32 size;

		cmd[i].msg.type = data[0];
		cmd[i].last_command = (data[1] == 1 ? true : false);
		cmd[i].msg.channel = data[2];
		cmd[i].msg.flags |= (data[3] == 1 ? MIPI_DSI_MSG_REQ_ACK : 0);
		cmd[i].msg.ctrl = 0;
		cmd[i].post_wait_ms = data[4];
		cmd[i].msg.tx_len = ((data[5] << 8) | (data[6]));

		size = cmd[i].msg.tx_len * sizeof(u8);

		payload = kzalloc(size, GFP_KERNEL);

		if (!payload) {
			rc = -ENOMEM;
			goto error_free_payloads;
		}

		for (j = 0; j < cmd[i].msg.tx_len; j++) {
			payload[j] = data[7 + j];
		}

		cmd[i].msg.tx_buf = payload;
		data += (7 + cmd[i].msg.tx_len);
	}

	return rc;
error_free_payloads:

	for (i = i - 1; i >= 0; i--) {
		cmd--;
		kfree(cmd->msg.tx_buf);
	}

	return rc;
}

static ssize_t oplus_display_set_dsi_command(struct kobject *obj,
		struct kobj_attribute *attr,
		const char *buf, size_t count)
{
	struct dsi_display *display = get_main_display();
	struct dsi_display_mode *mode;
	struct dsi_display_mode_priv_info *priv_info;
	struct dsi_panel_cmd_set *cmd_sets;
	char *bufp = (char *)buf;
	struct dsi_cmd_desc *cmds;
	struct dsi_panel_cmd_set *cmd;
	static char *cmd_bufs;
	static int cmd_counts;
	static u32 oplus_dsi_command = DSI_CMD_SET_MAX;
	static int oplus_dsi_state = DSI_CMD_SET_STATE_HS;
	u32 old_dsi_command = oplus_dsi_command;
	u32 packet_count = 0, size;
	int rc = count, i;
	char data[SZ_256];
	bool flush = false;

	if(1 == dsi_cmd_panel_debug)
		display = get_sec_display();

	if (!cmd_bufs) {
		cmd_bufs = kmalloc(SZ_4K, GFP_KERNEL);

		if (!cmd_bufs) {
			return -ENOMEM;
		}
	}

	sscanf(buf, "%s", data);

	if (!strcmp("dump", data)) {
		rc = oplus_display_dump_dsi_command(display);

		if (rc < 0) {
			return rc;
		}

		return count;

	} else if (!strcmp("flush", data)) {
		flush = true;

	} else if (!strcmp("dsi_hs_mode", data)) {
		oplus_dsi_state = DSI_CMD_SET_STATE_HS;

	} else if (!strcmp("dsi_lp_mode", data)) {
		oplus_dsi_state = DSI_CMD_SET_STATE_LP;

	} else {
		for (i = 0; i < DSI_CMD_SET_MAX; i++) {
			if (!strcmp(cmd_set_prop_map[i], data)) {
				oplus_dsi_command = i;
				flush = true;
				break;
			}
		}
	}

	if (!flush) {
		u32 value = 0, step = 0;

		while (sscanf(bufp, "%x%n", &value, &step) > 0) {
			if (value > 0xff) {
				pr_err("input reg don't large than 0xff\n");
				return -EINVAL;
			}

			cmd_bufs[cmd_counts++] = value;

			if (cmd_counts >= SZ_4K) {
				pr_err("wrong input reg len\n");
				cmd_counts = 0;
				return -EFAULT;
			}

			bufp += step;
		}

		return count;
	}

	if (!cmd_counts) {
		return rc;
	}

	if (old_dsi_command >= DSI_CMD_SET_MAX) {
		pr_err("UnSupport dsi command set\n");
		goto error;
	}

	if (!display || !display->panel || !display->panel->cur_mode) {
		pr_err("failed to get main dsi display\n");
		rc = -EFAULT;
		goto error;
	}

	mode = display->panel->cur_mode;

	if (!mode || !mode->priv_info) {
		pr_err("failed to get dsi display mode\n");
		rc = -EFAULT;
		goto error;
	}

	priv_info = mode->priv_info;
	cmd_sets = priv_info->cmd_sets;

	cmd = &cmd_sets[old_dsi_command];

	rc = oplus_dsi_panel_get_cmd_pkt_count(cmd_bufs, cmd_counts,
					      &packet_count);

	if (rc) {
		pr_err("commands failed, rc=%d\n", rc);
		goto error;
	}

	size = packet_count * sizeof(*cmd->cmds);

	cmds = kzalloc(size, GFP_KERNEL);

	if (!cmds) {
		rc = -ENOMEM;
		goto error;
	}

	rc = oplus_dsi_panel_create_cmd_packets(cmd_bufs, cmd_counts,
					       packet_count, cmds);

	if (rc) {
		pr_err("failed to create cmd packets, rc=%d\n", rc);
		goto error_free_cmds;
	}

	mutex_lock(&display->panel->panel_lock);

	kfree(cmd->cmds);
	cmd->cmds = cmds;
	cmd->count = packet_count;

	if (oplus_dsi_state == DSI_CMD_SET_STATE_LP) {
		cmd->state = DSI_CMD_SET_STATE_LP;

	} else if (oplus_dsi_state == DSI_CMD_SET_STATE_LP) {
		cmd->state = DSI_CMD_SET_STATE_HS;
	}

	mutex_unlock(&display->panel->panel_lock);

	cmd_counts = 0;
	oplus_dsi_state = DSI_CMD_SET_STATE_HS;

	return count;

error_free_cmds:
	kfree(cmds);
error:
	cmd_counts = 0;
	oplus_dsi_state = DSI_CMD_SET_STATE_HS;

	return rc;
}

extern int oplus_panel_alpha;
struct oplus_brightness_alpha *oplus_brightness_alpha_lut = NULL;
int interpolate(int x, int xa, int xb, int ya, int yb)
{
	int bf, factor, plus;
	int sub = 0;

	bf = 2 * (yb - ya) * (x - xa) / (xb - xa);
	factor = bf / 2;
	plus = bf % 2;

	return ya + factor + plus + sub;
}
EXPORT_SYMBOL(interpolate);

static ssize_t oplus_display_get_dim_alpha(struct kobject *obj,
		struct kobj_attribute *attr, char *buf)
{
	struct dsi_display *display = get_main_display();

	if (!display->panel->is_hbm_enabled ||
			(get_oplus_display_power_status() != OPLUS_DISPLAY_POWER_ON)) {
		return sprintf(buf, "%d\n", 0);
	}

	return sprintf(buf, "%d\n", oplus_underbrightness_alpha);
}

static ssize_t oplus_display_set_dim_alpha(struct kobject *obj,
		struct kobj_attribute *attr,
		const char *buf, size_t count)
{
	sscanf(buf, "%x", &oplus_panel_alpha);

	return count;
}

static ssize_t oplus_display_get_dc_dim_alpha(struct kobject *obj,
		struct kobj_attribute *attr, char *buf)
{
	int ret = 0;
	struct dsi_display *display = get_main_display();

	if (display->panel->is_hbm_enabled ||
			get_oplus_display_power_status() != OPLUS_DISPLAY_POWER_ON) {
		ret = 0;
	}

	if (oplus_dc2_alpha != 0) {
		ret = oplus_dc2_alpha;

	} else if (oplus_underbrightness_alpha != 0) {
		ret = oplus_underbrightness_alpha;

	} else if (oplus_dimlayer_bl_enable_v3_real) {
		ret = 1;
	}

	return sprintf(buf, "%d\n", ret);
}

static ssize_t oplus_display_get_dimlayer_backlight(struct kobject *obj,
		struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d %d %d %d %d %d\n", oplus_dimlayer_bl_alpha,
		       oplus_dimlayer_bl_alpha_value, oplus_dimlayer_dither_threshold,
		       oplus_dimlayer_dither_bitdepth, oplus_dimlayer_bl_delay,
		       oplus_dimlayer_bl_delay_after);
}

static ssize_t oplus_display_set_dimlayer_backlight(struct kobject *obj,
		struct kobj_attribute *attr,
		const char *buf, size_t count)
{
	sscanf(buf, "%d %d %d %d %d %d", &oplus_dimlayer_bl_alpha,
	       &oplus_dimlayer_bl_alpha_value, &oplus_dimlayer_dither_threshold,
	       &oplus_dimlayer_dither_bitdepth, &oplus_dimlayer_bl_delay,
	       &oplus_dimlayer_bl_delay_after);

	return count;
}

int oplus_dimlayer_bl_on_vblank = INT_MAX;
int oplus_dimlayer_bl_off_vblank = INT_MAX;
int oplus_fod_on_vblank = -1;
int oplus_fod_off_vblank = -1;
static int oplus_datadimming_v3_debug_value = -1;
static int oplus_datadimming_v3_debug_delay = 16000;
static ssize_t oplus_display_get_debug(struct kobject *obj,
		struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d %d %d %d %d %d %d\n", oplus_dimlayer_bl_on_vblank,
		       oplus_dimlayer_bl_off_vblank, oplus_fod_on_vblank, oplus_fod_off_vblank,
		       oplus_datadimming_v3_debug_value, oplus_datadimming_v3_debug_delay,
		       dsi_cmd_panel_debug);
}

static ssize_t oplus_display_set_debug(struct kobject *obj,
		struct kobj_attribute *attr,
		const char *buf, size_t count)
{
	sscanf(buf, "%d %d %d %d %d %d %d", &oplus_dimlayer_bl_on_vblank,
	       &oplus_dimlayer_bl_off_vblank,
	       &oplus_fod_on_vblank, &oplus_fod_off_vblank, &oplus_datadimming_v3_debug_value,
	       &oplus_datadimming_v3_debug_delay, &dsi_cmd_panel_debug);

	return count;
}


static ssize_t oplus_display_get_dimlayer_enable(struct kobject *obj,
		struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d %d\n", oplus_dimlayer_bl_enable,
		       oplus_dimlayer_bl_enable_v2);
}


int oplus_datadimming_vblank_count = 0;
atomic_t oplus_datadimming_vblank_ref = ATOMIC_INIT(0);
static int oplus_boe_data_dimming_process_unlock(int brightness, int enable)
{
	struct dsi_display *display = get_main_display();
	struct drm_connector *dsi_connector = display->drm_conn;
	struct dsi_panel *panel = display->panel;
	struct mipi_dsi_device *mipi_device;
	int rc = 0;

	if (!panel) {
		pr_err("failed to find display panel\n");
		return -EINVAL;
	}

	if (!dsi_panel_initialized(panel)) {
		pr_err("dsi_panel_aod_low_light_mode is not init\n");
		return -EINVAL;
	}

	mipi_device = &panel->mipi_device;

	if (!panel->is_hbm_enabled && oplus_datadimming_vblank_count != 0) {
		drm_crtc_wait_one_vblank(dsi_connector->state->crtc);
#if defined(OPLUS_FEATURE_PXLW_IRIS5)
	if (iris_is_chip_supported() && iris_is_pt_mode(panel))
		rc = iris_update_backlight(1, brightness);
	else
#endif
		rc = mipi_dsi_dcs_set_display_brightness(mipi_device, brightness);
		drm_crtc_wait_one_vblank(dsi_connector->state->crtc);
	}

	/* enable the clk vote for CMD mode panels */
	if (display->config.panel_mode == DSI_OP_CMD_MODE) {
		dsi_display_clk_ctrl(display->dsi_clk_handle,
				     DSI_ALL_CLKS, DSI_CLK_ON);
	}

	if (enable) {
		rc = dsi_panel_tx_cmd_set(display->panel, DSI_CMD_DATA_DIMMING_ON);

	} else {
		rc = dsi_panel_tx_cmd_set(display->panel, DSI_CMD_DATA_DIMMING_OFF);
	}

	if (display->config.panel_mode == DSI_OP_CMD_MODE) {
		dsi_display_clk_ctrl(display->dsi_clk_handle,
				     DSI_ALL_CLKS, DSI_CLK_OFF);
	}

	return rc;
}

struct oplus_brightness_alpha brightness_remapping[] = {
	{0, 0},
	{1, 1},
	{2, 155},
	{3, 160},
	{5, 165},
	{6, 170},
	{8, 175},
	{10, 180},
	{20, 190},
	{40, 230},
	{80, 270},
	{100, 300},
	{150, 400},
	{200, 600},
	{300, 800},
	{400, 1000},
	{600, 1250},
	{800, 1400},
	{1000, 1500},
	{1200, 1650},
	{1400, 1800},
	{1600, 1900},
	{1780, 2000},
	{2047, 2047},
};

int oplus_backlight_remapping(int brightness)
{
	struct oplus_brightness_alpha *lut = brightness_remapping;
	int count = ARRAY_SIZE(brightness_remapping);
	int i = 0;
	int bl_lvl = brightness;

	if (oplus_datadimming_v3_debug_value >= 0) {
		return oplus_datadimming_v3_debug_value;
	}

	for (i = 0; i < count; i++) {
		if (lut[i].brightness >= brightness) {
			break;
		}
	}

	if (i == 0) {
		bl_lvl = lut[0].alpha;

	} else if (i == count) {
		bl_lvl = lut[count - 1].alpha;

	}else
		bl_lvl = interpolate(brightness, lut[i - 1].brightness,
				lut[i].brightness, lut[i - 1].alpha,
				lut[i].alpha);

	return bl_lvl;
}

static bool dimming_enable = false;

int oplus_panel_process_dimming_v3_post(struct dsi_panel *panel, int brightness)
{
	bool enable = oplus_dimlayer_bl_enable_v3_real;
	int ret = 0;

	if (brightness <= 1) {
		enable = false;
	}

	if (enable != dimming_enable) {
		if (enable) {
			ret = oplus_boe_data_dimming_process_unlock(brightness, true);

			if (ret) {
				pr_err("failed to enable data dimming\n");
				goto error;
			}

			dimming_enable = true;
			pr_err("Enter DC backlight v3\n");

		} else {
			ret = oplus_boe_data_dimming_process_unlock(brightness, false);

			if (ret) {
				pr_err("failed to enable data dimming\n");
				goto error;
			}

			dimming_enable = false;
			pr_err("Exit DC backlight v3\n");
		}
	}

error:
	return 0;
}

static bool oplus_datadimming_v2_need_flush = false;
static bool oplus_datadimming_v2_need_sync = false;
void oplus_panel_process_dimming_v2_post(struct dsi_panel *panel,
					bool force_disable)
{
	struct dsi_display *display = get_main_display();
	struct drm_connector *dsi_connector = display->drm_conn;

	if (oplus_datadimming_v2_need_flush) {
		if (oplus_datadimming_v2_need_sync &&
				((!strcmp(panel->oplus_priv.vendor_name, "S6E3HC3")) || (!strcmp(panel->oplus_priv.vendor_name, "AMB670YF01"))
						|| (!strcmp(display->panel->oplus_priv.vendor_name, "AMS643YE01"))) &&
				dsi_connector && dsi_connector->state && dsi_connector->state->crtc) {
			struct drm_crtc *crtc = dsi_connector->state->crtc;
			int frame_time_us, ret = 0;
			u32 current_vblank;

			frame_time_us = mult_frac(1000, 1000, panel->cur_mode->timing.refresh_rate);

			current_vblank = drm_crtc_vblank_count(crtc);
			ret = wait_event_timeout(*drm_crtc_vblank_waitqueue(crtc),
						 current_vblank != drm_crtc_vblank_count(crtc),
						 usecs_to_jiffies(frame_time_us + 1000));
			if (!ret) {
				pr_err("%s: crtc wait_event_timeout \n", __func__);
			}
		}

		if (display->config.panel_mode == DSI_OP_VIDEO_MODE) {
			panel->oplus_priv.skip_mipi_last_cmd = true;
		}
		if (oplus_seed_backlight && !strcmp(panel->oplus_priv.vendor_name, "AMS643YE01")) {
		}
		else {
			dsi_panel_seed_mode_unlock(panel, seed_mode);
		}

		if (display->config.panel_mode == DSI_OP_VIDEO_MODE) {
			panel->oplus_priv.skip_mipi_last_cmd = false;
		}

		oplus_datadimming_v2_need_flush = false;
	}
}
EXPORT_SYMBOL(oplus_panel_process_dimming_v2_post);

int oplus_panel_process_dimming_v2(struct dsi_panel *panel, int bl_lvl,
				  bool force_disable)
{
	struct dsi_display *display = get_main_display();
	struct drm_connector *dsi_connector = display->drm_conn;

	oplus_datadimming_v2_need_flush = false;
	oplus_datadimming_v2_need_sync = false;

	if (!force_disable && oplus_dimlayer_bl_enable_v2_real &&
			bl_lvl > 1 && bl_lvl < oplus_dimlayer_bl_alpha_v2) {
		if (!oplus_seed_backlight) {
			pr_err("Enter DC backlight v2\n");

			if (!oplus_skip_datadimming_sync &&
					oplus_last_backlight != 0 &&
					oplus_last_backlight != 1) {
				oplus_datadimming_v2_need_sync = true;
			}
		}

		oplus_seed_backlight = bl_lvl;
		bl_lvl = oplus_dimlayer_bl_alpha_v2;
		oplus_datadimming_v2_need_flush = true;
		if (!strcmp(panel->oplus_priv.vendor_name, "AMS643YE01")) {
			dsi_panel_seed_mode_unlock(panel, seed_mode);
		}

	} else if (oplus_seed_backlight) {
		pr_err("Exit DC backlight v2\n");
		oplus_seed_backlight = 0;
		oplus_seed_last_backlight = 0;
		oplus_dc2_alpha = 0;
		oplus_datadimming_v2_need_flush = true;
		oplus_datadimming_v2_need_sync = true;
	}

	if (oplus_datadimming_v2_need_flush) {
		if (oplus_datadimming_v2_need_sync &&
				((!strcmp(panel->oplus_priv.vendor_name, "S6E3HC3")) || (!strcmp(panel->oplus_priv.vendor_name, "AMB670YF01"))
					|| (!strcmp(panel->oplus_priv.vendor_name, "AMS643YE01")))&&
				dsi_connector && dsi_connector->state && dsi_connector->state->crtc) {
			struct drm_crtc *crtc = dsi_connector->state->crtc;
			int frame_time_us, ret = 0;
			u32 current_vblank;

			frame_time_us = mult_frac(1000, 1000, panel->cur_mode->timing.refresh_rate);

			current_vblank = drm_crtc_vblank_count(crtc);
			ret = wait_event_timeout(*drm_crtc_vblank_waitqueue(crtc),
						 current_vblank != drm_crtc_vblank_count(crtc),
						 usecs_to_jiffies(frame_time_us + 1000));

			if (!ret) {
				pr_err("%s: crtc wait_event_timeout \n", __func__);
			}

		}
	}

	return bl_lvl;
}
EXPORT_SYMBOL(oplus_panel_process_dimming_v2);

int oplus_panel_process_dimming_v3(struct dsi_panel *panel, int brightness)
{
	bool enable = oplus_dimlayer_bl_enable_v3_real;
	int bl_lvl = brightness;

	if (enable) {
		bl_lvl = oplus_backlight_remapping(brightness);
	}

	oplus_panel_process_dimming_v3_post(panel, bl_lvl);
	return bl_lvl;
}
EXPORT_SYMBOL(oplus_panel_process_dimming_v3);

int oplus_panel_update_backlight_unlock(struct dsi_panel *panel)
{
	return dsi_panel_set_backlight(panel, panel->bl_config.bl_level);
}

static ssize_t oplus_display_set_dimlayer_enable(struct kobject *obj,
		struct kobj_attribute *attr,
		const char *buf, size_t count)
{
	struct dsi_display *display = get_main_display();
	struct drm_connector *dsi_connector = display->drm_conn;

	if (display && display->name) {
		int enable = 0;
		int err = 0;

		sscanf(buf, "%d", &enable);
		mutex_lock(&display->display_lock);

		if (!dsi_connector || !dsi_connector->state || !dsi_connector->state->crtc) {
			pr_err("[%s]: display not ready\n", __func__);

		} else {
			err = drm_crtc_vblank_get(dsi_connector->state->crtc);

			if (err) {
				pr_err("failed to get crtc vblank, error=%d\n", err);

			} else {
				/* do vblank put after 7 frames */
				oplus_datadimming_vblank_count = 7;
				atomic_inc(&oplus_datadimming_vblank_ref);
			}
		}

		usleep_range(17000, 17100);

		if ((!strcmp(display->panel->oplus_priv.vendor_name, "S6E3HC3")) ||
			(!strcmp(display->panel->oplus_priv.vendor_name, "AMB670YF01")) || !strcmp(display->panel->oplus_priv.vendor_name, "AMS643YE01")) {
			oplus_dimlayer_bl_enable_v2 = enable;

		} else {
			if (!strcmp(display->name,
					"qcom,mdss_dsi_oplus19101boe_nt37800_1080_2400_cmd")) {
				oplus_dimlayer_bl_enable_v3 = enable;

			} else {
				oplus_dimlayer_bl_enable = enable;
			}
		}

		mutex_unlock(&display->display_lock);
	}

	return count;
}

static ssize_t oplus_display_get_dimlayer_hbm(struct kobject *obj,
		struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", oplus_dimlayer_hbm);
}

int oplus_dimlayer_hbm_vblank_count = 0;
atomic_t oplus_dimlayer_hbm_vblank_ref = ATOMIC_INIT(0);
static ssize_t oplus_display_set_dimlayer_hbm(struct kobject *obj,
		struct kobj_attribute *attr,
		const char *buf, size_t count)
{
	struct dsi_display *display = get_main_display();
	struct drm_connector *dsi_connector = display->drm_conn;
	int err = 0;
	int value = 0;

	sscanf(buf, "%d", &value);
	value = !!value;

	if (oplus_dimlayer_hbm == value) {
		return count;
	}

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

#ifdef OPLUS_BUG_STABILITY
	pr_err("debug for oplus_display_set_dimlayer_hbm set oplus_dimlayer_hbm = %d\n",
	       oplus_dimlayer_hbm);
#endif

	return count;
}

int oplus_force_screenfp = 0;
static ssize_t oplus_display_get_forcescreenfp(struct kobject *obj,
		struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", oplus_force_screenfp);
}

static ssize_t oplus_display_set_forcescreenfp(struct kobject *obj,
		struct kobj_attribute *attr,
		const char *buf, size_t count)
{
	sscanf(buf, "%x", &oplus_force_screenfp);

	return count;
}

static ssize_t oplus_display_get_esd_status(struct kobject *obj,
		struct kobj_attribute *attr, char *buf)
{
	struct dsi_display *display = get_main_display();
	int rc = 0;

	return rc;

	if (!display) {
		return -ENODEV;
	}

	mutex_lock(&display->display_lock);

	if (!display->panel) {
		rc = -EINVAL;
		goto error;
	}

	rc = sprintf(buf, "%d\n", display->panel->esd_config.esd_enabled);

error:
	mutex_unlock(&display->display_lock);
	return rc;
}

static ssize_t oplus_display_set_esd_status(struct kobject *obj,
		struct kobj_attribute *attr,
		const char *buf, size_t count)
{
	struct dsi_display *display = get_main_display();
	int enable = 0;

	return count;

	sscanf(buf, "%du", &enable);

#ifdef OPLUS_BUG_STABILITY
	pr_err("debug for oplus_display_set_esd_status, the enable value = %d\n",
	       enable);
#endif

	if (!display) {
		return -ENODEV;
	}

	if (!display->panel || !display->drm_conn) {
		return -EINVAL;
	}

	if (!enable) {
		if (display->panel->esd_config.esd_enabled) {
			sde_connector_schedule_status_work(display->drm_conn, false);
			display->panel->esd_config.esd_enabled = false;
			pr_err("disable esd work");
		}
	}

	return count;
}

static ssize_t oplus_display_notify_panel_blank(struct kobject *obj,
		struct kobj_attribute *attr,
		const char *buf, size_t count)
{

	struct msm_drm_notifier notifier_data;
	int blank;
	int temp_save = 0;

	sscanf(buf, "%du", &temp_save);
	printk(KERN_INFO "%s oplus_display_notify_panel_blank = %d\n", __func__,
			temp_save);

	if (temp_save == 1) {
		blank = MSM_DRM_BLANK_UNBLANK;
		notifier_data.data = &blank;
		notifier_data.id = 0;
		msm_drm_notifier_call_chain(MSM_DRM_EARLY_EVENT_BLANK,
					    &notifier_data);
		msm_drm_notifier_call_chain(MSM_DRM_EVENT_BLANK,
					    &notifier_data);

	} else if (temp_save == 0) {
		blank = MSM_DRM_BLANK_POWERDOWN;
		notifier_data.data = &blank;
		notifier_data.id = 0;
		msm_drm_notifier_call_chain(MSM_DRM_EARLY_EVENT_BLANK,
					    &notifier_data);
	}

	return count;
}

extern int is_ffl_enable;
static ssize_t oplus_get_ffl_setting(struct kobject *obj,
		struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", is_ffl_enable);
}

static ssize_t oplus_set_ffl_setting(struct kobject *obj,
		struct kobj_attribute *attr,
		const char *buf, size_t count)
{
	int enable = 0;

	sscanf(buf, "%du", &enable);
	printk(KERN_INFO "%s oplus_set_ffl_setting = %d\n", __func__, enable);

	oplus_ffl_set(enable);

	return count;
}

int oplus_onscreenfp_status = 0;
EXPORT_SYMBOL(oplus_onscreenfp_status);
ktime_t oplus_onscreenfp_pressed_time;
EXPORT_SYMBOL(oplus_onscreenfp_pressed_time);
u32 oplus_onscreenfp_vblank_count = 0;
EXPORT_SYMBOL(oplus_onscreenfp_vblank_count);
static ssize_t oplus_display_notify_fp_press(struct kobject *obj,
		struct kobj_attribute *attr,
		const char *buf, size_t count)
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

	if (!dsi_connector || !dsi_connector->state || !dsi_connector->state->crtc) {
		pr_err("[%s]: display not ready\n", __func__);
		return count;
	}

	sscanf(buf, "%du", &onscreenfp_status);
	onscreenfp_status = !!onscreenfp_status;

	if (onscreenfp_status == oplus_onscreenfp_status) {
		return count;
	}

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

	if (onscreenfp_status &&
			OPLUS_DISPLAY_AOD_SCENE == get_oplus_display_scene()) {
		/* enable the clk vote for CMD mode panels */
		if (display->config.panel_mode == DSI_OP_CMD_MODE) {
			dsi_display_clk_ctrl(display->dsi_clk_handle,
					     DSI_ALL_CLKS, DSI_CLK_ON);
		}

		mutex_lock(&display->panel->panel_lock);

		if (display->panel->panel_initialized) {
			if ((!strcmp(display->panel->oplus_priv.vendor_name, "S6E3HC3") && (display->panel->panel_id2 >= 5)) ||
				(!strcmp(display->panel->oplus_priv.vendor_name, "AMB670YF01") && (display->panel->panel_id2 >= 5))) {
				err = dsi_panel_tx_cmd_set(display->panel, DSI_CMD_AOD_HBM_ON_PVT);
			} else {
				err = dsi_panel_tx_cmd_set(display->panel, DSI_CMD_AOD_HBM_ON);
			}
		}

		mutex_unlock(&display->panel->panel_lock);

		if (err) {
			pr_err("failed to setting aod hbm on mode %d\n", err);
		}

		if (display->config.panel_mode == DSI_OP_CMD_MODE) {
			dsi_display_clk_ctrl(display->dsi_clk_handle,
					     DSI_ALL_CLKS, DSI_CLK_OFF);
		}
	}

	oplus_onscreenfp_vblank_count = drm_crtc_vblank_count(
					       dsi_connector->state->crtc);
	oplus_onscreenfp_pressed_time = ktime_get();

	drm_modeset_lock_all(drm_dev);

	state = drm_atomic_state_alloc(drm_dev);

	if (!state) {
		goto error;
	}

	state->acquire_ctx = mode_config->acquire_ctx;
	crtc = dsi_connector->state->crtc;
	crtc_state = drm_atomic_get_crtc_state(state, crtc);

	for (i = 0; i < priv->num_crtcs; i++) {
		if (priv->disp_thread[i].crtc_id == crtc->base.id) {
			if (priv->disp_thread[i].thread) {
				kthread_flush_worker(&priv->disp_thread[i].worker);
			}
		}
	}

	err = drm_atomic_commit(state);
	drm_atomic_state_put(state);

error:
	drm_modeset_unlock_all(drm_dev);

	if (!vblank_get) {
		drm_crtc_vblank_put(dsi_connector->state->crtc);
	}

	return count;
}

static ssize_t oplus_display_get_roundcorner(struct kobject *obj,
		struct kobj_attribute *attr, char *buf)
{
	struct dsi_display *display = get_main_display();
	bool roundcorner = true;

	if (display && display->name &&
			!strcmp(display->name, "qcom,mdss_dsi_oplus19101boe_nt37800_1080_2400_cmd")) {
		roundcorner = false;
	}

	return sprintf(buf, "%d\n", roundcorner);
}

DEFINE_MUTEX(dynamic_osc_clock_lock);
int dynamic_osc_clock = 139600;

int dsi_update_dynamic_osc_clock(void)
{
	struct dsi_display *display = get_main_display();
	int rc = 0;
	int osc_clock_rate = dynamic_osc_clock;

	if (!display||!display->panel) {
		pr_err("failed for: %s %d\n", __func__, __LINE__);
		return -EINVAL;
	}

	if (!display->panel->oplus_priv.is_osc_support) {
		pr_err("not support osc\n");
		return 0;
	}

	mutex_lock(&display->display_lock);
	mutex_lock(&display->panel->panel_lock);

	if (!dsi_panel_initialized(display->panel)) {
		rc = -EINVAL;
		goto unlock;
	}

	/* enable the clk vote for CMD mode panels */
	if (display->config.panel_mode == DSI_OP_CMD_MODE) {
		dsi_display_clk_ctrl(display->dsi_clk_handle,
				DSI_CORE_CLK, DSI_CLK_ON);
	}

	if (osc_clock_rate) {
		if (osc_clock_rate == display->panel->oplus_priv.osc_clk_mode0_rate) {
			rc = dsi_panel_tx_cmd_set(display->panel, DSI_CMD_OSC_CLK_MODEO0);
		} else if (osc_clock_rate == display->panel->oplus_priv.osc_clk_mode1_rate) {
			rc = dsi_panel_tx_cmd_set(display->panel, DSI_CMD_OSC_CLK_MODEO1);
		} else {
			pr_err("[%s]not support osc clk rate=%d\n", __func__, osc_clock_rate);
		}
		if (rc)
			pr_err("Failed to configure osc dynamic clk\n");
	} else {
		pr_info("[%s] osc clk rate is 0, not config !\n", __func__);
	}

	if (display->config.panel_mode == DSI_OP_CMD_MODE) {
		rc = dsi_display_clk_ctrl(display->dsi_clk_handle,
				DSI_CORE_CLK, DSI_CLK_OFF);
	}

unlock:
	mutex_unlock(&display->panel->panel_lock);
	mutex_unlock(&display->display_lock);

	return rc;
}

static ssize_t oplus_display_get_dynamic_osc_clock(struct kobject *obj,
		struct kobj_attribute *attr, char *buf)
{
	struct dsi_display *display = get_main_display();
	int rc = 0;

	if (!display) {
		pr_err("failed for: %s %d\n", __func__, __LINE__);
		return -EINVAL;
	}

	mutex_lock(&display->display_lock);

	rc = snprintf(buf, PAGE_SIZE, "%d\n", dynamic_osc_clock);
	pr_debug("%s: read dsi clk rate %d\n", __func__,
		 dynamic_osc_clock);

	mutex_unlock(&display->display_lock);

	return rc;
}

static ssize_t oplus_display_set_dynamic_osc_clock(struct kobject *obj,
		struct kobj_attribute *attr,
		const char *buf, size_t count)
{
	struct dsi_display *display = get_main_display();
	int osc_clk = 0;
	int rc = 0;

	if (!display) {
		pr_err("failed for: %s %d\n", __func__, __LINE__);
		return -EINVAL;
	}

	if(display->panel->oplus_priv.is_osc_support) {
		sscanf(buf, "%du", &osc_clk);
		dynamic_osc_clock = osc_clk;
		if (display->panel->panel_mode != DSI_OP_CMD_MODE) {
			pr_err("only supported for command mode\n");
			return -EFAULT;
		}

		pr_info("%s: osc clk param value: '%d'\n", __func__, osc_clk);
		dsi_update_dynamic_osc_clock();
		return count;
	}

	if (get_oplus_display_power_status() != OPLUS_DISPLAY_POWER_ON) {
		printk(KERN_ERR"%s display panel in off status\n", __func__);
		return -EFAULT;
	}

	sscanf(buf, "%du", &osc_clk);

	if (display->panel->panel_mode != DSI_OP_CMD_MODE) {
		pr_err("only supported for command mode\n");
		return -EFAULT;
	}

	pr_info("%s: osc clk param value: '%d'\n", __func__, osc_clk);

	mutex_lock(&display->display_lock);
	mutex_lock(&display->panel->panel_lock);

	if (!dsi_panel_initialized(display->panel)) {
		rc = -EINVAL;
		goto unlock;
	}

	/* enable the clk vote for CMD mode panels */
	if (display->config.panel_mode == DSI_OP_CMD_MODE) {
		dsi_display_clk_ctrl(display->dsi_clk_handle,
				     DSI_CORE_CLK, DSI_CLK_ON);
	}

	if (osc_clk == 139600) {
		rc = dsi_panel_tx_cmd_set(display->panel, DSI_CMD_OSC_CLK_MODEO0);

	} else {
		rc = dsi_panel_tx_cmd_set(display->panel, DSI_CMD_OSC_CLK_MODEO1);
	}

	if (rc) {
		pr_err("Failed to configure osc dynamic clk\n");

	} else {
		rc = count;
	}

	if (display->config.panel_mode == DSI_OP_CMD_MODE) {
		rc = dsi_display_clk_ctrl(display->dsi_clk_handle,
					  DSI_CORE_CLK, DSI_CLK_OFF);
	}

	dynamic_osc_clock = osc_clk;

unlock:
	mutex_unlock(&display->panel->panel_lock);
	mutex_unlock(&display->display_lock);

	return count;
}

static ssize_t oplus_display_get_max_brightness_show(struct kobject *obj,
		struct kobj_attribute *attr, char *buf)
{
	struct dsi_display *display = get_main_display();

	if (!display) {
		pr_err("failed for: %s %d\n", __func__, __LINE__);
		return -EINVAL;
	}

	if(oplus_debug_max_brightness == 0) {
		return sprintf(buf, "%d\n", display->panel->bl_config.brightness_normal_max_level);
	} else {
		return sprintf(buf, "%d\n", oplus_debug_max_brightness);
	}
}

static ssize_t oplus_display_set_max_brightness_store(struct kobject *obj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	sscanf(buf, "%du", &oplus_debug_max_brightness);
	return count;
}

static ssize_t oplus_display_get_ccd_check(struct kobject *obj,
		struct kobj_attribute *attr, char *buf)
{
	int ccd_check = 0;
#if 0
	struct dsi_display *display = get_main_display();
	struct mipi_dsi_device *mipi_device;
	int rc = 0;
	int ccd_check = 0;

	if (!display || !display->panel) {
		pr_err("failed for: %s %d\n", __func__, __LINE__);
		return -EINVAL;
	}

	if (get_oplus_display_power_status() != OPLUS_DISPLAY_POWER_ON) {
		printk(KERN_ERR"%s display panel in off status\n", __func__);
		return -EFAULT;
	}

	if (display->panel->panel_mode != DSI_OP_CMD_MODE) {
		pr_err("only supported for command mode\n");
		return -EFAULT;
	}

	if (!(display && display->panel->oplus_priv.vendor_name) ||
			!strcmp(display->panel->oplus_priv.vendor_name, "NT37800")) {
		ccd_check = 0;
		goto end;
	}

	mipi_device = &display->panel->mipi_device;

	mutex_lock(&display->display_lock);
	mutex_lock(&display->panel->panel_lock);

	if (!dsi_panel_initialized(display->panel)) {
		rc = -EINVAL;
		goto unlock;
	}

	rc = dsi_display_cmd_engine_enable(display);

	if (rc) {
		pr_err("%s, cmd engine enable failed\n", __func__);
		goto unlock;
	}

	/* enable the clk vote for CMD mode panels */
	if (display->config.panel_mode == DSI_OP_CMD_MODE) {
		dsi_display_clk_ctrl(display->dsi_clk_handle,
				     DSI_CORE_CLK, DSI_CLK_ON);
	}

	if (!strcmp(display->panel->oplus_priv.vendor_name, "AMB655UV01")) {
		{
			char value[] = { 0x5A, 0x5A };
			rc = mipi_dsi_dcs_write(mipi_device, 0xF0, value, sizeof(value));
		}
		{
			char value[] = { 0x44, 0x50 };
			rc = mipi_dsi_dcs_write(mipi_device, 0xE7, value, sizeof(value));
		}
		usleep_range(1000, 1100);
		{
			char value[] = { 0x03 };
			rc = mipi_dsi_dcs_write(mipi_device, 0xB0, value, sizeof(value));
		}

	} else {
		{
			char value[] = { 0x5A, 0x5A };
			rc = mipi_dsi_dcs_write(mipi_device, 0xF0, value, sizeof(value));
		}
		{
			char value[] = { 0x02 };
			rc = mipi_dsi_dcs_write(mipi_device, 0xB0, value, sizeof(value));
		}
		{
			char value[] = { 0x44, 0x50 };
			rc = mipi_dsi_dcs_write(mipi_device, 0xCC, value, sizeof(value));
		}
		usleep_range(1000, 1100);
		{
			char value[] = { 0x05 };
			rc = mipi_dsi_dcs_write(mipi_device, 0xB0, value, sizeof(value));
		}
	}

	if (display->config.panel_mode == DSI_OP_CMD_MODE) {
		rc = dsi_display_clk_ctrl(display->dsi_clk_handle,
					  DSI_CORE_CLK, DSI_CLK_OFF);
	}

	dsi_display_cmd_engine_disable(display);

	mutex_unlock(&display->panel->panel_lock);
	mutex_unlock(&display->display_lock);

	if (!strcmp(display->panel->oplus_priv.vendor_name, "AMB655UV01")) {
		{
			unsigned char read[10];

			rc = dsi_display_read_panel_reg(display, 0xE1, read, 1);

			pr_err("read ccd_check value = 0x%x rc=%d\n", read[0], rc);
			ccd_check = read[0];
		}

	} else {
		{
			unsigned char read[10];

			rc = dsi_display_read_panel_reg(display, 0xCC, read, 1);

			pr_err("read ccd_check value = 0x%x rc=%d\n", read[0], rc);
			ccd_check = read[0];
		}
	}

	mutex_lock(&display->display_lock);
	mutex_lock(&display->panel->panel_lock);

	if (!dsi_panel_initialized(display->panel)) {
		rc = -EINVAL;
		goto unlock;
	}

	rc = dsi_display_cmd_engine_enable(display);

	if (rc) {
		pr_err("%s, cmd engine enable failed\n", __func__);
		goto unlock;
	}

	if (display->config.panel_mode == DSI_OP_CMD_MODE) {
		dsi_display_clk_ctrl(display->dsi_clk_handle,
				     DSI_CORE_CLK, DSI_CLK_ON);
	}

	{
		char value[] = { 0xA5, 0xA5 };
		rc = mipi_dsi_dcs_write(mipi_device, 0xF0, value, sizeof(value));
	}

	if (display->config.panel_mode == DSI_OP_CMD_MODE) {
		rc = dsi_display_clk_ctrl(display->dsi_clk_handle,
					  DSI_CORE_CLK, DSI_CLK_OFF);
	}

	dsi_display_cmd_engine_disable(display);
unlock:

	mutex_unlock(&display->panel->panel_lock);
	mutex_unlock(&display->display_lock);
end:
	pr_err("[%s] ccd_check = %d\n",  display->panel->oplus_priv.vendor_name,
	       ccd_check);
#endif
	return sprintf(buf, "%d\n", ccd_check);
}

int dsi_display_oplus_set_power(struct drm_connector *connector,
			       int power_mode, void *disp)
{
	struct dsi_display *display = disp;
	int rc = 0;
	struct msm_drm_notifier notifier_data;
	int blank;
	bool notify_off = false;

	if (!display || !display->panel) {
		pr_err("invalid display/panel\n");
		return -EINVAL;
	}

#if defined(OPLUS_FEATURE_PXLW_IRIS5)

	if (iris_is_chip_supported() && NULL != display->display_type
			&& !strcmp(display->display_type, "secondary")) {
		return rc;
	}

#endif

	pr_info("[%s] <%s> power_mode : %d\n", __func__, display->panel->oplus_priv.vendor_name, power_mode);

	if (power_mode == SDE_MODE_DPMS_OFF)
		atomic_set(&display->panel->esd_pending, 1);

	switch (power_mode) {
	case SDE_MODE_DPMS_LP1:
	case SDE_MODE_DPMS_LP2:
		if (power_mode == SDE_MODE_DPMS_LP1 &&
				display->panel->power_mode == SDE_MODE_DPMS_ON) {
			notify_off = true;
		}

		memset(&notifier_data, 0, sizeof(notifier_data));

		if (notify_off) {
			blank = MSM_DRM_BLANK_POWERDOWN;
			notifier_data.data = &blank;
			notifier_data.id = 0;

			msm_drm_notifier_call_chain(MSM_DRM_EARLY_EVENT_BLANK,
						    &notifier_data);
			drm_panel_notifier_call_chain(&display->panel->drm_panel,
							MSM_DRM_EARLY_EVENT_BLANK,
							&notifier_data);
		}

		switch (get_oplus_display_scene()) {
		case OPLUS_DISPLAY_NORMAL_SCENE:
		case OPLUS_DISPLAY_NORMAL_HBM_SCENE:
			rc = dsi_panel_set_lp1(display->panel);
			rc = dsi_panel_set_lp2(display->panel);
			set_oplus_display_scene(OPLUS_DISPLAY_AOD_SCENE);
			break;

		case OPLUS_DISPLAY_AOD_HBM_SCENE:

			/* Skip aod off if fingerprintpress exist */
			if (!sde_crtc_get_fingerprint_pressed(connector->state->crtc->state)) {
				mutex_lock(&display->panel->panel_lock);
				dsi_display_clk_ctrl(display->dsi_clk_handle,
						     DSI_CORE_CLK, DSI_CLK_ON);

				if (display->panel->panel_initialized) {
					if ((!strcmp(display->panel->oplus_priv.vendor_name, "S6E3HC3") && (display->panel->panel_id2 >= 5)) ||
						(!strcmp(display->panel->oplus_priv.vendor_name, "AMB670YF01") && (display->panel->panel_id2 >= 5))) {
						rc = dsi_panel_tx_cmd_set(display->panel, DSI_CMD_AOD_HBM_OFF_PVT);
					} else {
						rc = dsi_panel_tx_cmd_set(display->panel, DSI_CMD_AOD_HBM_OFF);
					}
					oplus_update_aod_light_mode_unlock(display->panel);

				} else {
					pr_err("[%s][%d]failed to setting dsi command", __func__, __LINE__);
				}

				dsi_display_clk_ctrl(display->dsi_clk_handle,
						     DSI_CORE_CLK, DSI_CLK_OFF);
				mutex_unlock(&display->panel->panel_lock);
				set_oplus_display_scene(OPLUS_DISPLAY_AOD_SCENE);
			}

			break;

		case OPLUS_DISPLAY_AOD_SCENE:
		default:
			break;
		}

#ifdef OPLUS_BUG_STABILITY
		/*switch to panel TP-VSYNC while exit aod scene*/
		if (oplus_adfr_is_support()) {
			if (power_mode == SDE_MODE_DPMS_LP1) {
				if (oplus_adfr_get_vsync_mode() == OPLUS_TE_SOURCE_TP)
					sde_encoder_adfr_aod_fod_source_switch(display, OPLUS_TE_SOURCE_TE);
			}
        }
#endif

		if (notify_off) {
			msm_drm_notifier_call_chain(MSM_DRM_EVENT_BLANK,
						    &notifier_data);
			drm_panel_notifier_call_chain(&display->panel->drm_panel,
							MSM_DRM_EVENT_BLANK,
							&notifier_data);
		}

		set_oplus_display_power_status(OPLUS_DISPLAY_POWER_DOZE_SUSPEND);
		break;

	case SDE_MODE_DPMS_ON:
		blank = MSM_DRM_BLANK_UNBLANK;
		notifier_data.data = &blank;
		notifier_data.id = 0;
		pr_err("[%s:%d] SDE_MODE_DPMS_ON\n", __func__, __LINE__);
		msm_drm_notifier_call_chain(MSM_DRM_EARLY_EVENT_BLANK,
						&notifier_data);
		drm_panel_notifier_call_chain(&display->panel->drm_panel,
							MSM_DRM_EARLY_EVENT_BLANK,
							&notifier_data);

		if ((display->panel->power_mode == SDE_MODE_DPMS_LP1) ||
				(display->panel->power_mode == SDE_MODE_DPMS_LP2)) {
			#ifdef OPLUS_BUG_STABILITY
			if (oplus_adfr_is_support()) {
				/*switch to panel TP-VSYNC while exit aod scene*/
				if (oplus_adfr_get_vsync_mode() == OPLUS_DOUBLE_TE_VSYNC) {
					sde_encoder_adfr_aod_fod_source_switch(display, OPLUS_TE_SOURCE_TP);
				}
			}
			#endif
			if (sde_crtc_get_fingerprint_mode(connector->state->crtc->state) && oplus_dimlayer_hbm) {
				mutex_lock(&display->panel->panel_lock);
				dsi_display_clk_ctrl(display->dsi_clk_handle,
							DSI_CORE_CLK, DSI_CLK_ON);

				if (display->panel->panel_initialized) {
					if ((!strcmp(display->panel->oplus_priv.vendor_name, "S6E3HC3") && (display->panel->panel_id2 >= 5)) ||
						(!strcmp(display->panel->oplus_priv.vendor_name, "AMB670YF01") && (display->panel->panel_id2 >= 5))) {
						rc = dsi_panel_tx_cmd_set(display->panel, DSI_CMD_AOD_HBM_ON_PVT);
					} else {
						rc = dsi_panel_tx_cmd_set(display->panel, DSI_CMD_AOD_HBM_ON);
					}
				} else {
					pr_err("[%s][%d]failed to setting dsi command", __func__, __LINE__);
				}

				dsi_display_clk_ctrl(display->dsi_clk_handle,
							DSI_CORE_CLK, DSI_CLK_OFF);
				mutex_unlock(&display->panel->panel_lock);
				set_oplus_display_scene(OPLUS_DISPLAY_AOD_HBM_SCENE);

			} else {
				rc = dsi_panel_set_nolp(display->panel);
				set_oplus_display_scene(OPLUS_DISPLAY_NORMAL_SCENE);
			}
		}
		if (!sde_crtc_get_fingerprint_mode(connector->state->crtc->state)) {
			oplus_dsi_update_seed_mode(display);
		}
		set_oplus_display_power_status(OPLUS_DISPLAY_POWER_ON);
		msm_drm_notifier_call_chain(MSM_DRM_EVENT_BLANK,
						&notifier_data);
		drm_panel_notifier_call_chain(&display->panel->drm_panel,
							MSM_DRM_EVENT_BLANK,
							&notifier_data);
		break;

	case SDE_MODE_DPMS_OFF:
	default:
		return rc;
	}

	DSI_DEBUG("Power mode transition from %d to %d %s",
		display->panel->power_mode, power_mode,
		rc ? "failed" : "successful");

	if (!rc) {
		display->panel->power_mode = power_mode;
	}

	return rc;
}
EXPORT_SYMBOL(dsi_display_oplus_set_power);

static int oplus_display_find_vreg_by_name(const char *name)
{
	int count = 0, i = 0;
	struct dsi_vreg *vreg = NULL;
	struct dsi_regulator_info *dsi_reg = NULL;
	struct dsi_display *display = get_main_display();

	if (!display) {
		return -ENODEV;
	}

	if (!display->panel) {
		return -EINVAL;
	}

	dsi_reg = &display->panel->power_info;
	count = dsi_reg->count;

	for (i = 0; i < count; i++) {
		vreg = &dsi_reg->vregs[i];
		pr_err("%s : find  %s", __func__, vreg->vreg_name);

		if (!strcmp(vreg->vreg_name, name)) {
			pr_err("%s : find the vreg %s", __func__, name);
			return i;

		} else {
			continue;
		}
	}

	pr_err("%s : dose not find the vreg [%s]", __func__, name);

	return -EINVAL;
}

static u32 update_current_voltage(u32 id)
{
	int vol_current = 0, pwr_id = 0;
	struct dsi_vreg *dsi_reg = NULL;
	struct dsi_regulator_info *dsi_reg_info = NULL;
	struct dsi_display *display = get_main_display();

	if (!display) {
		return -ENODEV;
	}

	if (!display->panel || !display->drm_conn) {
		return -EINVAL;
	}

	dsi_reg_info = &display->panel->power_info;
	pwr_id = oplus_display_find_vreg_by_name(panel_vol_bak[id].pwr_name);

	if (pwr_id < 0) {
		pr_err("%s: can't find the pwr_id, please check the vreg name\n", __func__);
		return pwr_id;
	}

	dsi_reg = &dsi_reg_info->vregs[pwr_id];

	if (!dsi_reg) {
		return -EINVAL;
	}

	vol_current = regulator_get_voltage(dsi_reg->vreg);

	return vol_current;
}

static ssize_t oplus_display_get_panel_pwr(struct kobject *obj,
		struct kobj_attribute *attr, char *buf)
{

	u32 ret = 0;
	u32 i = 0;

	for (i = 0; i < (PANEL_VOLTAGE_ID_MAX - 1); i++) {
		ret = update_current_voltage(panel_vol_bak[i].voltage_id);

		if (ret < 0) {
			pr_err("%s : update_current_voltage error = %d\n", __func__, ret);

		} else {
			panel_vol_bak[i].voltage_current = ret;
		}
	}

	return sprintf(buf, "%d %d %d %d %d %d %d %d %d %d %d %d\n",
		       panel_vol_bak[0].voltage_id, panel_vol_bak[0].voltage_min,
		       panel_vol_bak[0].voltage_current, panel_vol_bak[0].voltage_max,
		       panel_vol_bak[1].voltage_id, panel_vol_bak[1].voltage_min,
		       panel_vol_bak[1].voltage_current, panel_vol_bak[1].voltage_max,
		       panel_vol_bak[2].voltage_id, panel_vol_bak[2].voltage_min,
		       panel_vol_bak[2].voltage_current, panel_vol_bak[2].voltage_max);
}

static ssize_t oplus_display_set_panel_pwr(struct kobject *obj,
		struct kobj_attribute *attr,
		const char *buf, size_t count)
{
	u32 panel_vol_value = 0, rc = 0, panel_vol_id = 0, pwr_id = 0;
	struct dsi_vreg *dsi_reg = NULL;
	struct dsi_regulator_info *dsi_reg_info = NULL;
	struct dsi_display *display = get_main_display();

	sscanf(buf, "%d %d", &panel_vol_id, &panel_vol_value);
	panel_vol_id = panel_vol_id & 0x0F;

	pr_err("debug for %s, buf = [%s], id = %d value = %d, count = %d\n",
	       __func__, buf, panel_vol_id, panel_vol_value, count);

	if (panel_vol_id < 0 || panel_vol_id > PANEL_VOLTAGE_ID_MAX) {
		return -EINVAL;
	}

	if (panel_vol_value < panel_vol_bak[panel_vol_id].voltage_min ||
			panel_vol_id > panel_vol_bak[panel_vol_id].voltage_max) {
		return -EINVAL;
	}

	if (!display) {
		return -ENODEV;
	}

	if (!display->panel || !display->drm_conn) {
		return -EINVAL;
	}

	if (panel_vol_id == PANEL_VOLTAGE_ID_VG_BASE) {
		pr_err("%s: set the VGH_L pwr = %d \n", __func__, panel_vol_value);
		panel_pwr_vg_base = panel_vol_value;
		return count;
	}

	dsi_reg_info = &display->panel->power_info;

	pwr_id = oplus_display_find_vreg_by_name(panel_vol_bak[panel_vol_id].pwr_name);

	if (pwr_id < 0) {
		pr_err("%s: can't find the vreg name, please re-check vreg name: %s \n",
		       __func__,
		       panel_vol_bak[panel_vol_id].pwr_name);
		return pwr_id;
	}

	dsi_reg = &dsi_reg_info->vregs[pwr_id];

	rc = regulator_set_voltage(dsi_reg->vreg, panel_vol_value, panel_vol_value);

	if (rc) {
		pr_err("Set voltage(%s) fail, rc=%d\n",
		       dsi_reg->vreg_name, rc);
		return -EINVAL;
	}

	return count;
}

/*#ifdef OPLUS_BUG_STABILITY*/
static ssize_t oplus_display_get_dsi_cmd_log_switch(struct kobject *obj,
	struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", dsi_cmd_log_enable);
}

static ssize_t oplus_display_set_dsi_cmd_log_switch(struct kobject *obj,
		struct kobj_attribute *attr,
		const char *buf, size_t count)
{
	sscanf(buf, "%d", &dsi_cmd_log_enable);
	pr_err("debug for %s, buf = [%s], dsi_cmd_log_enable = %d , count = %d\n",
             __func__, buf, dsi_cmd_log_enable, count);

	return count;
}
/*#endif*/

static struct kobject *oplus_display_kobj;

static OPLUS_ATTR(hbm, S_IRUGO | S_IWUSR, oplus_display_get_hbm,
			oplus_display_set_hbm);
static OPLUS_ATTR(audio_ready, S_IRUGO | S_IWUSR, NULL,
			oplus_display_set_audio_ready);
static OPLUS_ATTR(seed, S_IRUGO | S_IWUSR, oplus_display_get_seed,
			oplus_display_set_seed);
static OPLUS_ATTR(panel_serial_number, S_IRUGO | S_IWUSR,
			oplus_display_get_panel_serial_number, oplus_display_set_panel_serial_number);
static OPLUS_ATTR(dump_info, S_IRUGO | S_IWUSR, oplus_display_dump_info, NULL);
static OPLUS_ATTR(panel_dsc, S_IRUGO | S_IWUSR, oplus_display_get_panel_dsc,
			NULL);
static OPLUS_ATTR(power_status, S_IRUGO | S_IWUSR,
			oplus_display_get_power_status, oplus_display_set_power_status);
static OPLUS_ATTR(display_regulator_control, S_IRUGO | S_IWUSR, NULL,
			oplus_display_regulator_control);
static OPLUS_ATTR(panel_id, S_IRUGO | S_IWUSR, oplus_display_get_panel_id,
			NULL);
static OPLUS_ATTR(sau_closebl_node, S_IRUGO | S_IWUSR,
			oplus_display_get_closebl_flag, oplus_display_set_closebl_flag);
static OPLUS_ATTR(write_panel_reg, S_IRUGO | S_IWUSR,
			oplus_display_get_panel_reg, oplus_display_set_panel_reg);
static OPLUS_ATTR(dsi_cmd, S_IRUGO | S_IWUSR, oplus_display_get_dsi_command,
			oplus_display_set_dsi_command);
static OPLUS_ATTR(dim_alpha, S_IRUGO | S_IWUSR, oplus_display_get_dim_alpha,
			oplus_display_set_dim_alpha);
static OPLUS_ATTR(dim_dc_alpha, S_IRUGO | S_IWUSR,
			oplus_display_get_dc_dim_alpha, oplus_display_set_dim_alpha);
static OPLUS_ATTR(dimlayer_hbm, S_IRUGO | S_IWUSR,
			oplus_display_get_dimlayer_hbm, oplus_display_set_dimlayer_hbm);
static OPLUS_ATTR(dimlayer_bl_en, S_IRUGO | S_IWUSR,
			oplus_display_get_dimlayer_enable, oplus_display_set_dimlayer_enable);
static OPLUS_ATTR(dimlayer_set_bl, S_IRUGO | S_IWUSR,
			oplus_display_get_dimlayer_backlight, oplus_display_set_dimlayer_backlight);
static OPLUS_ATTR(debug, S_IRUGO | S_IWUSR, oplus_display_get_debug,
			oplus_display_set_debug);
static OPLUS_ATTR(force_screenfp, S_IRUGO | S_IWUSR,
			oplus_display_get_forcescreenfp, oplus_display_set_forcescreenfp);
static OPLUS_ATTR(esd_status, S_IRUGO | S_IWUSR, oplus_display_get_esd_status,
			oplus_display_set_esd_status);
static OPLUS_ATTR(notify_panel_blank, S_IRUGO | S_IWUSR, NULL,
			oplus_display_notify_panel_blank);
static OPLUS_ATTR(ffl_set, S_IRUGO | S_IWUSR, oplus_get_ffl_setting,
			oplus_set_ffl_setting);
static OPLUS_ATTR(notify_fppress, S_IRUGO | S_IWUSR, NULL,
			oplus_display_notify_fp_press);
static OPLUS_ATTR(aod_light_mode_set, S_IRUGO | S_IWUSR,
			oplus_get_aod_light_mode, oplus_set_aod_light_mode);
static OPLUS_ATTR(spr, S_IRUGO | S_IWUSR, oplus_display_get_spr,
			oplus_display_set_spr);
static OPLUS_ATTR(roundcorner, S_IRUGO | S_IRUSR, oplus_display_get_roundcorner,
			NULL);
static OPLUS_ATTR(dynamic_osc_clock, S_IRUGO | S_IWUSR,
			oplus_display_get_dynamic_osc_clock, oplus_display_set_dynamic_osc_clock);
static OPLUS_ATTR(max_brightness, S_IRUGO | S_IWUSR,
			oplus_display_get_max_brightness_show, oplus_display_set_max_brightness_store);
static OPLUS_ATTR(ccd_check, S_IRUGO | S_IRUSR, oplus_display_get_ccd_check,
			NULL);
static OPLUS_ATTR(iris_rm_check, S_IRUGO | S_IWUSR,
			oplus_display_get_iris_state, NULL);
static OPLUS_ATTR(panel_pwr, S_IRUGO | S_IWUSR, oplus_display_get_panel_pwr,
			oplus_display_set_panel_pwr);
/*#ifdef OPLUS_BUG_STABILITY*/
static OPLUS_ATTR(dsi_cmd_log_switch, S_IRUGO | S_IWUSR, oplus_display_get_dsi_cmd_log_switch,
                        oplus_display_set_dsi_cmd_log_switch);
/*#endif*/

#ifdef OPLUS_BUG_STABILITY
static DEVICE_ATTR(adfr_debug, S_IRUGO|S_IWUSR, oplus_adfr_get_debug, oplus_adfr_set_debug);
static DEVICE_ATTR(vsync_switch, S_IRUGO|S_IWUSR, oplus_get_vsync_switch, oplus_set_vsync_switch);
#endif

static OPLUS_ATTR(backlight_smooth, S_IRUGO|S_IWUSR, oplus_backlight_smooth_get_debug,
			oplus_backlight_smooth_set_debug);

/*
 * Create a group of attributes so that we can create and destroy them all
 * at once.
 */
static struct attribute *oplus_display_attrs[] = {
	&oplus_attr_hbm.attr,
	&oplus_attr_audio_ready.attr,
	&oplus_attr_seed.attr,
	&oplus_attr_panel_serial_number.attr,
	&oplus_attr_dump_info.attr,
	&oplus_attr_panel_dsc.attr,
	&oplus_attr_power_status.attr,
	&oplus_attr_display_regulator_control.attr,
	&oplus_attr_panel_id.attr,
	&oplus_attr_sau_closebl_node.attr,
	&oplus_attr_write_panel_reg.attr,
	&oplus_attr_dsi_cmd.attr,
	&oplus_attr_dim_alpha.attr,
	&oplus_attr_dim_dc_alpha.attr,
	&oplus_attr_dimlayer_hbm.attr,
	&oplus_attr_dimlayer_set_bl.attr,
	&oplus_attr_dimlayer_bl_en.attr,
	&oplus_attr_debug.attr,
	&oplus_attr_force_screenfp.attr,
	&oplus_attr_esd_status.attr,
	&oplus_attr_notify_panel_blank.attr,
	&oplus_attr_ffl_set.attr,
	&oplus_attr_notify_fppress.attr,
	&oplus_attr_aod_light_mode_set.attr,
	&oplus_attr_spr.attr,
	&oplus_attr_roundcorner.attr,
	&oplus_attr_dynamic_osc_clock.attr,
	&oplus_attr_max_brightness.attr,
	&oplus_attr_ccd_check.attr,
	&oplus_attr_iris_rm_check.attr,
	&oplus_attr_panel_pwr.attr,
#ifdef OPLUS_BUG_STABILITY
	&dev_attr_adfr_debug.attr,
	&dev_attr_vsync_switch.attr,
#endif
	&oplus_attr_backlight_smooth.attr,
/*#ifdef OPLUS_BUG_STABILITY*/
        &oplus_attr_dsi_cmd_log_switch.attr,
/*#endif*/
	NULL,	/* need to NULL terminate the list of attributes */
};

static struct attribute_group oplus_display_attr_group = {
	.attrs = oplus_display_attrs,
};

/*
 * Create a new API to get display resolution
 */
int oplus_display_get_resolution(unsigned int *xres, unsigned int *yres)
{
	*xres = *yres = 0;

	if (get_main_display() && get_main_display()->modes) {
		*xres = get_main_display()->modes->timing.v_active;
		*yres = get_main_display()->modes->timing.h_active;
	}

	return 0;
}
EXPORT_SYMBOL(oplus_display_get_resolution);

extern int oplus_display_panel_init(void);
int oplus_display_private_api_init(void)
{
	struct dsi_display *display = get_main_display();
	int retval;

	if (!display) {
		return -EPROBE_DEFER;
	}

	oplus_display_kobj = kobject_create_and_add("oplus_display", kernel_kobj);

	if (!oplus_display_kobj) {
		return -ENOMEM;
	}

	/* Create the files associated with this kobject */
	retval = sysfs_create_group(oplus_display_kobj, &oplus_display_attr_group);

	if (retval) {
		goto error_remove_kobj;
	}

	retval = sysfs_create_link(oplus_display_kobj,
				   &display->pdev->dev.kobj, "panel");

	if (retval) {
		goto error_remove_sysfs_group;
	}

	if (oplus_ffl_thread_init()) {
		pr_err("fail to init oplus_ffl_thread\n");
	}

	if(oplus_display_panel_init())
		pr_err("fail to init oplus_display_panel_init\n");
	/*mm_kevent_init();*/

	return 0;

error_remove_sysfs_group:
	sysfs_remove_group(oplus_display_kobj, &oplus_display_attr_group);
error_remove_kobj:
	kobject_put(oplus_display_kobj);
	oplus_display_kobj = NULL;
	/*mm_kevent_deinit();*/

	return retval;
}

void  oplus_display_private_api_exit(void)
{
	/*mm_kevent_deinit();*/
	oplus_ffl_thread_exit();
	sysfs_remove_link(oplus_display_kobj, "panel");
	sysfs_remove_group(oplus_display_kobj, &oplus_display_attr_group);
	kobject_put(oplus_display_kobj);
}
