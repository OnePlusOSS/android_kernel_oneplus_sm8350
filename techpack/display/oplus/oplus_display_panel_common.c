/***************************************************************
** Copyright (C),  2020,  OPLUS Mobile Comm Corp.,  Ltd
** File : oplus_display_panel_common.c
** Description : oplus display panel common feature
** Version : 1.0
** Date : 2020/06/13
**
** ------------------------------- Revision History: -----------
**  <author>        <data>        <version >        <desc>
**  Li.Sheng       2020/06/13        1.0           Build this moudle
******************************************************************/
#include "oplus_display_panel_common.h"
#include <linux/notifier.h>
#include <linux/msm_drm_notify.h>

#include "oplus_display_panel.h"
#include "oplus_display_panel_seed.h"

int oplus_debug_max_brightness = 0;
#ifdef OPLUS_BUG_STABILITY
int oplus_dither_enable = 0;
int oplus_cabc_status = OPLUS_DISPLAY_CABC_UI;
int oplus_dre_status = 0;
#endif
EXPORT_SYMBOL(oplus_debug_max_brightness);
#ifdef OPLUS_BUG_STABILITY
EXPORT_SYMBOL(oplus_dither_enable);
#endif

extern int dsi_display_read_panel_reg(struct dsi_display *display, u8 cmd,
				      void *data, size_t len);
extern int oplus_display_audio_ready;
char oplus_rx_reg[PANEL_TX_MAX_BUF] = {0x0};
char oplus_rx_len = 0;
extern int lcd_closebl_flag;
extern int spr_mode;
extern int dynamic_osc_clock;
int mca_mode = 1;
extern int oplus_dimlayer_hbm;
extern int oplus_dimlayer_bl;

enum {
	REG_WRITE = 0,
	REG_READ,
	REG_X,
};

extern int msm_drm_notifier_call_chain(unsigned long val, void *v);
extern int __oplus_display_set_spr(int mode);
extern int dsi_display_spr_mode(struct dsi_display *display, int mode);
extern int dsi_panel_spr_mode(struct dsi_panel *panel, int mode);
extern int dsi_panel_read_panel_reg(struct dsi_display_ctrl *ctrl,
			     struct dsi_panel *panel, u8 cmd, void *rbuf,  size_t len);

int oplus_display_panel_get_id(void *buf)
{
	struct dsi_display *display = get_main_display();
	int ret = 0;
	unsigned char read[30];
	struct panel_id *panel_rid = buf;
	int display_id = panel_rid->DA;

	if (get_oplus_display_power_status() == OPLUS_DISPLAY_POWER_ON) {
		if (display == NULL) {
			printk(KERN_INFO "oplus_display_get_panel_id and main display is null");
			ret = -1;
			return ret;
		}

		if (0 == display_id && display->enabled == false) {
			pr_err("%s main panel is disabled");
			return -1;
		}

		if (1 == display_id) {
			display = get_sec_display();
			if (!display) {
				printk(KERN_INFO "oplus_display_get_panel_serial_number and second display is null");
				return -1;
			}
			if (display->enabled == false) {
				pr_err("%s second panel is disabled");
				return -1;
			}
		}

		ret = dsi_display_read_panel_reg(display, 0xDA, read, 1);

		if (ret < 0) {
			pr_err("failed to read DA ret=%d\n", ret);
			return -EINVAL;
		}

		panel_rid->DA = (uint32_t)read[0];

		ret = dsi_display_read_panel_reg(display, 0xDB, read, 1);

		if (ret < 0) {
			pr_err("failed to read DB ret=%d\n", ret);
			return -EINVAL;
		}

		panel_rid->DB = (uint32_t)read[0];

		ret = dsi_display_read_panel_reg(display, 0xDC, read, 1);

		if (ret < 0) {
			pr_err("failed to read DC ret=%d\n", ret);
			return -EINVAL;
		}

		panel_rid->DC = (uint32_t)read[0];

	} else {
		printk(KERN_ERR	 "%s oplus_display_get_panel_id, but now display panel status is not on\n", __func__);
		return -EINVAL;
	}

	return ret;
}

int oplus_display_panel_get_max_brightness(void *buf)
{
	uint32_t *max_brightness = buf;
	int panel_id = (*max_brightness >> 12);
	struct dsi_display *display = get_main_display();
	if (panel_id == 1)
		display = get_sec_display();

	if (oplus_debug_max_brightness == 0) {
		(*max_brightness) = display->panel->bl_config.bl_normal_max_level;
	} else {
		(*max_brightness) = oplus_debug_max_brightness;
	}

	return 0;
}

int oplus_display_panel_set_max_brightness(void *buf)
{
	uint32_t *max_brightness = buf;

	oplus_debug_max_brightness = (*max_brightness);

	return 0;
}

int oplus_display_panel_get_oplus_max_brightness(void *buf)
{
	uint32_t *max_brightness = buf;
	int panel_id = (*max_brightness >> 12);
	struct dsi_display *display = get_main_display();
	if (panel_id == 1)
		display = get_sec_display();

	(*max_brightness) = display->panel->bl_config.bl_normal_max_level;

	return 0;
}

int oplus_display_panel_get_brightness(void *buf)
{
	uint32_t *brightness = buf;
	int panel_id = (*brightness >> 12);
	struct dsi_display *display = get_main_display();
	if (panel_id == 1)
		display = get_sec_display();

		if(!strcmp(display->panel->oplus_priv.vendor_name, "AMS643YE01")) {
		(*brightness) = display->panel->bl_config.oplus_raw_bl;
	} else {
		(*brightness) = display->panel->bl_config.bl_level;
	}
	return 0;
}

int oplus_display_panel_get_vendor(void *buf)
{
	struct panel_info *p_info = buf;
	struct dsi_display *display = NULL;
	char *vendor = NULL;
	char *manu_name = NULL;
	int panel_id = p_info->version[0];

	display = get_main_display();
	if (1 == panel_id)
		display = get_sec_display();
	if (!display || !display->panel ||
	    !display->panel->oplus_priv.vendor_name ||
	    !display->panel->oplus_priv.manufacture_name) {
		pr_err("failed to config lcd proc device");
		return -EINVAL;
	}

	vendor = (char *)display->panel->oplus_priv.vendor_name;
	manu_name = (char *)display->panel->oplus_priv.manufacture_name;

	memcpy(p_info->version, vendor, strlen(vendor) > 31?31:(strlen(vendor)+1));
	memcpy(p_info->manufacture, manu_name, strlen(manu_name) > 31?31:(strlen(manu_name)+1));

	return 0;
}

int oplus_display_panel_get_ccd_check(void *buf)
{
	struct dsi_display *display = get_main_display();
	struct mipi_dsi_device *mipi_device;
	int rc = 0;
	unsigned int *ccd_check = buf;

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

	if (true) {
		/* all new project does not have this function, ccd_check = 0 */
		(*ccd_check) = 0;
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
			(*ccd_check) = read[0];
		}

	} else {
		{
			unsigned char read[10];

			rc = dsi_display_read_panel_reg(display, 0xCC, read, 1);

			pr_err("read ccd_check value = 0x%x rc=%d\n", read[0], rc);
			(*ccd_check) = read[0];
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
	pr_err("[%s] ccd_check = %d\n",  display->panel->oplus_priv.vendor_name, (*ccd_check));
	return 0;
}

int oplus_display_panel_get_serial_number(void *buf) {
	int ret = 0;
	unsigned char read[30];
	PANEL_SERIAL_INFO panel_serial_info;
	struct panel_serial_number *panel_rnum = buf;
	uint64_t serial_number;
	struct dsi_display *display = get_main_display();
	int i;
	int panel_id = panel_rnum->serial_number[0];

	pr_info("%s panel_id = %d\n", __func__, panel_id);
	if (!display) {
		printk(KERN_INFO "oplus_display_get_panel_serial_number and main display is null");
		return -1;
	}

	if (0 == panel_id && display->enabled == false) {
		pr_err("%s main panel is disabled");
		return -1;
	}

	if (1 == panel_id) {
		display = get_sec_display();
		if (!display) {
			printk(KERN_INFO "oplus_display_get_panel_serial_number and main display is null");
			return -1;
		}
		if (display->enabled == false) {
			pr_err("%s second panel is disabled");
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
	for (i = 0;i < 10; i++) {
		if(!strcmp(display->panel->oplus_priv.vendor_name, "S6E3XA1")) {
			mutex_lock(&display->display_lock);
			mutex_lock(&display->panel->panel_lock);

			if (display->panel->panel_initialized) {
				if (display->config.panel_mode == DSI_OP_CMD_MODE) {
					dsi_display_clk_ctrl(display->dsi_clk_handle,
							DSI_ALL_CLKS, DSI_CLK_ON);
				}
				 {
					char value[] = { 0x5A, 0x5A };
					ret = mipi_dsi_dcs_write(&display->panel->mipi_device, 0xF0, value, sizeof(value));
				 }
				if (display->config.panel_mode == DSI_OP_CMD_MODE) {
					dsi_display_clk_ctrl(display->dsi_clk_handle,
							DSI_ALL_CLKS, DSI_CLK_OFF);
				}
			}
			mutex_unlock(&display->panel->panel_lock);
			mutex_unlock(&display->display_lock);

			if(ret < 0) {
				ret = scnprintf(buf, PAGE_SIZE,
						"Get panel serial number failed, reason:%d", ret);
				msleep(20);
				continue;
			}
			ret = dsi_display_read_panel_reg(display, 0xD8, read, 22);
		} else {
			if (!strcmp(display->panel->oplus_priv.vendor_name, "S6E3HC3")) {
				ret = dsi_display_read_panel_reg(display, 0xA1, read, 11);
			} else if (!strcmp(display->panel->oplus_priv.vendor_name, "NT37701")) {
				ret = dsi_display_read_panel_reg(display, 0xA3, read, 8);
			} else if (!strcmp(display->panel->name, "samsung amb655x fhd cmd mode dsc dsi panel")
				|| !strcmp(display->panel->name, "samsung SOFE03F dsc cmd mode panel")) {
				ret = dsi_display_read_panel_reg(display, 0xA1, read, 18);
			} else {
				ret = dsi_display_read_panel_reg(display, 0xA1, read, 17);
			}
		}
		if(ret < 0) {
			ret = scnprintf(buf, PAGE_SIZE,
					"Get panel serial number failed, reason:%d", ret);
			msleep(20);
			continue;
		}

		/*  0xA1			   11th		12th	13th	14th	15th
		 *  HEX				0x32		0x0C	0x0B	0x29	0x37
		 *  Bit		   [D7:D4][D3:D0] [D5:D0] [D5:D0] [D5:D0] [D5:D0]
		 *  exp			  3	  2	   C	   B	   29	  37
		 *  Yyyy,mm,dd	  2014   2m	  12d	 11h	 41min   55sec
		 *  panel_rnum.data[24:21][20:16] [15:8]  [7:0]
		 *  panel_rnum:precise_time					  [31:24] [23:16] [reserved]
		*/
		if (!strcmp(display->panel->name, "samsung amb655xl08 amoled fhd+ panel")) {
			panel_serial_info.reg_index = 11;
		} else if (!strcmp(display->panel->name, "samsung ams643ye01 amoled fhd+ panel")) {
			panel_serial_info.reg_index = 7;
		} else if (!strcmp(display->panel->oplus_priv.vendor_name, "S6E3HC3")) {
			panel_serial_info.reg_index = 4;
		} else if (!strcmp(display->panel->oplus_priv.vendor_name, "NT37701")) {
			panel_serial_info.reg_index = 0;
		} else if (!strcmp(display->panel->oplus_priv.vendor_name, "S6E3XA1")) {
			panel_serial_info.reg_index = 15;
		} else if (!strcmp(display->panel->name, "samsung amb655x fhd cmd mode dsc dsi panel")
			|| !strcmp(display->panel->name, "samsung SOFE03F dsc cmd mode panel")) {
			panel_serial_info.reg_index = 11;
		} else {
			panel_serial_info.reg_index = 10;
		}
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

		ret = scnprintf(panel_rnum->serial_number, PAGE_SIZE, "Get panel serial number: %llx\n",serial_number);
		break;
	}

	return ret;
}


int oplus_display_panel_set_audio_ready(void *data) {
	uint32_t *audio_ready = data;

	oplus_display_audio_ready = (*audio_ready);
	printk("%s oplus_display_audio_ready = %d\n", __func__, oplus_display_audio_ready);

	return 0;
}

int oplus_display_panel_dump_info(void *data) {
	int ret = 0;
	struct dsi_display * temp_display;
	struct display_timing_info *timing_info = data;

	temp_display = get_main_display();

	if (temp_display == NULL) {
		printk(KERN_INFO "oplus_display_dump_info and main display is null");
		ret = -1;
		return ret;
	}

	if(temp_display->modes == NULL) {
		printk(KERN_INFO "oplus_display_dump_info and display modes is null");
		ret = -1;
		return ret;
	}

	timing_info->h_active = temp_display->modes->timing.h_active;
	timing_info->v_active = temp_display->modes->timing.v_active;
	timing_info->refresh_rate = temp_display->modes->timing.refresh_rate;
	timing_info->clk_rate_hz_l32 = (uint32_t)(temp_display->modes->timing.clk_rate_hz & 0x00000000FFFFFFFF);
	timing_info->clk_rate_hz_h32 = (uint32_t)(temp_display->modes->timing.clk_rate_hz >> 32);

	return 0;
}

int oplus_display_panel_get_dsc(void *data) {
	int ret = 0;
	uint32_t *reg_read = data;
	unsigned char read[30];

	if(get_oplus_display_power_status() == OPLUS_DISPLAY_POWER_ON) {
		if(get_main_display() == NULL) {
			printk(KERN_INFO "oplus_display_get_panel_dsc and main display is null");
			ret = -1;
			return ret;
		}

		ret = dsi_display_read_panel_reg(get_main_display(), 0x03, read, 1);
		if (ret < 0) {
			printk(KERN_ERR  "%s read panel dsc reg error = %d!\n", __func__, ret);
			ret = -1;
		} else {
			(*reg_read) = read[0];
			ret = 0;
		}
	} else {
		printk(KERN_ERR	 "%s but now display panel status is not on\n", __func__);
		ret = -1;
	}

	return ret;
}

int oplus_display_panel_get_closebl_flag(void *data)
{
	uint32_t *closebl_flag = data;

	(*closebl_flag) = lcd_closebl_flag;
	printk(KERN_INFO "oplus_display_get_closebl_flag = %d\n", lcd_closebl_flag);

	return 0;
}

int oplus_display_panel_set_closebl_flag(void *data)
{
	uint32_t *closebl = data;

	pr_err("lcd_closebl_flag = %d\n", (*closebl));
	if (1 != (*closebl))
		lcd_closebl_flag = 0;
	pr_err("oplus_display_set_closebl_flag = %d\n", lcd_closebl_flag);

	return 0;
}
int oplus_big_endian_copy(void *dest, void *src, int count)
{
	int index = 0, knum = 0, rc = 0;
	uint32_t *u_dest = (uint32_t*) dest;
	char *u_src = (char*) src;

	if (dest == NULL || src == NULL) {
		printk("%s null pointer\n", __func__);
		return -EINVAL;
	}

	if (dest == src) {
		return rc;
	}

	while (count > 0) {
		u_dest[index] = ((u_src[knum] << 24) | (u_src[knum+1] << 16) | (u_src[knum+2] << 8) | u_src[knum+3]);
		index += 1;
		knum += 4;
		count = count - 1;
	}

	return rc;
}

int oplus_display_panel_get_reg(void *data)
{
	struct dsi_display *display = get_main_display();
	struct panel_reg_get *panel_reg = data;
	uint32_t u32_bytes = sizeof(uint32_t)/sizeof(char);

	if (!display) {
		return -EINVAL;
	}

	mutex_lock(&display->display_lock);

	u32_bytes = oplus_rx_len%u32_bytes ? (oplus_rx_len/u32_bytes + 1) : oplus_rx_len/u32_bytes;
	oplus_big_endian_copy(panel_reg->reg_rw, oplus_rx_reg, u32_bytes);
	panel_reg->lens = oplus_rx_len;

	mutex_unlock(&display->display_lock);

	return 0;
}

int oplus_display_panel_set_reg(void *data)
{
	char reg[PANEL_TX_MAX_BUF] = {0x0};
	char payload[PANEL_TX_MAX_BUF] = {0x0};
	u32 index = 0, value = 0;
	int ret = 0;
	int len = 0;
	struct dsi_display *display = get_main_display();
	struct panel_reg_rw *reg_rw = data;

	if (!display || !display->panel) {
		pr_err("debug for: %s %d\n", __func__, __LINE__);
		return -EFAULT;
	}

	if (reg_rw->lens > PANEL_REG_MAX_LENS) {
		pr_err("error: wrong input reg len\n");
		return -EINVAL;
	}

	if (reg_rw->rw_flags == REG_READ) {
		value = reg_rw->cmd;
		len = reg_rw->lens;
		dsi_display_read_panel_reg(get_main_display(), value, reg, len);

		for (index=0; index < len; index++) {
			printk("reg[%d] = %x ", index, reg[index]);
		}
		mutex_lock(&display->display_lock);
		memcpy(oplus_rx_reg, reg, PANEL_TX_MAX_BUF);
		oplus_rx_len = len;
		mutex_unlock(&display->display_lock);
		return 0;
	}

	if (reg_rw->rw_flags == REG_WRITE) {
		memcpy(payload, reg_rw->value, reg_rw->lens);
		reg[0] = reg_rw->cmd;
		len = reg_rw->lens;
		for (index=0; index < len; index++) {
			reg[index + 1] = payload[index];
		}

		if(get_oplus_display_power_status() == OPLUS_DISPLAY_POWER_ON) {
				/* enable the clk vote for CMD mode panels */
			mutex_lock(&display->display_lock);
			mutex_lock(&display->panel->panel_lock);

			if (display->panel->panel_initialized) {
				if (display->config.panel_mode == DSI_OP_CMD_MODE) {
					dsi_display_clk_ctrl(display->dsi_clk_handle,
							DSI_ALL_CLKS, DSI_CLK_ON);
				}
				ret = mipi_dsi_dcs_write(&display->panel->mipi_device, reg[0],
							 payload, len);

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
		return 0;
	}
	printk("%s error: please check the args!\n", __func__);
	return -1;
}

int oplus_display_panel_notify_blank(void *data)
{
	struct msm_drm_notifier notifier_data;
	int blank;
	uint32_t *temp_save_user = data;
	int temp_save = (*temp_save_user);

	printk(KERN_INFO "%s oplus_display_notify_panel_blank = %d\n", __func__, temp_save);

	if(temp_save == 1) {
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
	return 0;
}

int oplus_display_panel_get_spr(void *data)
{
	uint32_t *spr_mode_user = data;

	printk(KERN_INFO "oplus_display_get_spr = %d\n", spr_mode);
	*spr_mode_user = spr_mode;

	return 0;
}

int oplus_display_panel_set_spr(void *data)
{
	uint32_t *temp_save_user = data;
	int temp_save = (*temp_save_user);

	printk(KERN_INFO "%s oplus_display_set_spr = %d\n", __func__, temp_save);

	__oplus_display_set_spr(temp_save);
	if(get_oplus_display_power_status() == OPLUS_DISPLAY_POWER_ON) {
		if(get_main_display() == NULL) {
			printk(KERN_INFO "oplus_display_set_spr and main display is null");
			return 0;
		}

		dsi_display_spr_mode(get_main_display(), spr_mode);
	} else {
		printk(KERN_ERR	 "%s oplus_display_set_spr = %d, but now display panel status is not on\n", __func__, temp_save);
	}
	return 0;
}

int oplus_display_panel_get_roundcorner(void *data)
{
	uint32_t *round_corner = data;
	struct dsi_display *display = get_main_display();
	bool roundcorner = true;

	if (display && display->name &&
	    !strcmp(display->name, "qcom,mdss_dsi_oplus19101boe_nt37800_1080_2400_cmd"))
		roundcorner = false;

	*round_corner = roundcorner;

	return 0;
}

int oplus_display_panel_get_dynamic_osc_clock(void *data)
{
	struct dsi_display *display = get_main_display();
	uint32_t *osc_clock = data;

	if (!display) {
		pr_err("failed for: %s %d\n", __func__, __LINE__);
		return -EINVAL;
	}

	mutex_lock(&display->display_lock);

	*osc_clock = dynamic_osc_clock;
	pr_debug("%s: read dsi clk rate %d\n", __func__,
			dynamic_osc_clock);

	mutex_unlock(&display->display_lock);

	return 0;
}

int oplus_display_panel_set_dynamic_osc_clock(void *data)
{
	struct dsi_display *display = get_main_display();
	uint32_t *osc_clk_user = data;
	int osc_clk = *osc_clk_user;
	int rc = 0;

	if (!display) {
		pr_err("failed for: %s %d\n", __func__, __LINE__);
		return -EINVAL;
	}

	if(get_oplus_display_power_status() != OPLUS_DISPLAY_POWER_ON) {
		printk(KERN_ERR"%s display panel in off status\n", __func__);
		return -EFAULT;
	}

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
	if (rc)
		pr_err("Failed to configure osc dynamic clk\n");

	if (display->config.panel_mode == DSI_OP_CMD_MODE) {
		rc = dsi_display_clk_ctrl(display->dsi_clk_handle,
				DSI_CORE_CLK, DSI_CLK_OFF);
	}
	dynamic_osc_clock = osc_clk;

unlock:
	mutex_unlock(&display->panel->panel_lock);
	mutex_unlock(&display->display_lock);

	return rc;
}

int oplus_display_get_softiris_color_status(void *data)
{
	struct softiris_color *iris_color_status = data;
	bool color_vivid_status = false;
	bool color_srgb_status = false;
	bool color_softiris_status = false;
	bool color_dual_panel_status = false;
	bool color_dual_brightness_status = false;
	struct dsi_parser_utils *utils = NULL;
	struct dsi_panel *panel = NULL;

	struct dsi_display *display = get_main_display();
	if (!display) {
		pr_err("failed for: %s %d\n", __func__, __LINE__);
		return -EINVAL;
	}

	panel = display->panel;
	if (!panel) {
		pr_err("failed for: %s %d\n", __func__, __LINE__);
		return -EINVAL;
	}

	utils = &panel->utils;
	if (!utils) {
		pr_err("failed for: %s %d\n", __func__, __LINE__);
		return -EINVAL;
	}

	color_vivid_status = utils->read_bool(utils->data, "oplus,color_vivid_status");
	DSI_INFO("oplus,color_vivid_status: %s", color_vivid_status ? "true" : "false");

	color_srgb_status = utils->read_bool(utils->data, "oplus,color_srgb_status");
	DSI_INFO("oplus,color_srgb_status: %s", color_srgb_status ? "true" : "false");

	color_softiris_status = utils->read_bool(utils->data, "oplus,color_softiris_status");
	DSI_INFO("oplus,color_softiris_status: %s", color_softiris_status ? "true" : "false");

	color_dual_panel_status = utils->read_bool(utils->data, "oplus,color_dual_panel_status");
	DSI_INFO("oplus,color_dual_panel_status: %s", color_dual_panel_status ? "true" : "false");

	color_dual_brightness_status = utils->read_bool(utils->data, "oplus,color_dual_brightness_status");
	DSI_INFO("oplus,color_dual_brightness_status: %s", color_dual_brightness_status ? "true" : "false");

	iris_color_status->color_vivid_status = (uint32_t)color_vivid_status;
	iris_color_status->color_srgb_status = (uint32_t)color_srgb_status;
	iris_color_status->color_softiris_status = (uint32_t)color_softiris_status;
	iris_color_status->color_dual_panel_status = (uint32_t)color_dual_panel_status;
	iris_color_status->color_dual_brightness_status = (uint32_t)color_dual_brightness_status;
	return 0;
}

int oplus_display_panel_get_id2(void)
{
	struct dsi_display *display = get_main_display();
	int ret = 0;
	unsigned char read[30];

	if(get_oplus_display_power_status() == OPLUS_DISPLAY_POWER_ON) {
		if(display == NULL) {
			printk(KERN_INFO "oplus_display_get_panel_id and main display is null");
			return 0;
		}

		if ((!strcmp(display->panel->oplus_priv.vendor_name, "S6E3HC3")) || (!strcmp(display->panel->oplus_priv.vendor_name, "AMB670YF01"))) {
			ret = dsi_display_read_panel_reg(display, 0xDB, read, 1);
			if (ret < 0) {
				pr_err("failed to read DB ret=%d\n", ret);
				return -EINVAL;
			}
			ret = (int)read[0];
		}
	} else {
		printk(KERN_ERR	 "%s oplus_display_get_panel_id, but now display panel status is not on\n", __func__);
		return 0;
	}

	return ret;
}

int oplus_display_panel_hbm_lightspot_check(void)
{
	int rc = 0;
	char value[] = { 0xE0 };
	char value1[] = { 0x0F, 0xFF };
	struct dsi_display *display = get_main_display();
	struct mipi_dsi_device *mipi_device;

	if (!display || !display->panel) {
		pr_err("failed for: %s %d\n", __func__, __LINE__);
		return -EINVAL;
	}

	if (get_oplus_display_power_status() != OPLUS_DISPLAY_POWER_ON) {
		printk(KERN_ERR"%s display panel in off status\n", __func__);
		return -EFAULT;
	}

	mipi_device = &display->panel->mipi_device;

	mutex_lock(&display->display_lock);
	mutex_lock(&display->panel->panel_lock);

	if (!dsi_panel_initialized(display->panel)) {
		pr_err("%s, dsi_panel_initialized failed\n", __func__);
		rc = -EINVAL;
		goto unlock;
	}

	rc = dsi_display_cmd_engine_enable(display);

	if (rc) {
		pr_err("%s, cmd engine enable failed\n", __func__);
		rc = -EINVAL;
		goto unlock;
	}

	/* enable the clk vote for CMD mode panels */
	if (display->config.panel_mode == DSI_OP_CMD_MODE) {
		dsi_display_clk_ctrl(display->dsi_clk_handle,
				     DSI_CORE_CLK, DSI_CLK_ON);
	}

	rc = mipi_dsi_dcs_write(mipi_device, 0x53, value, sizeof(value));
	usleep_range(1000, 1100);
	rc = mipi_dsi_dcs_write(mipi_device, 0x51, value1, sizeof(value1));
	usleep_range(1000, 1100);
	pr_err("[%s] hbm_lightspot_check successfully\n",  display->panel->oplus_priv.vendor_name);

	if (display->config.panel_mode == DSI_OP_CMD_MODE) {
		rc = dsi_display_clk_ctrl(display->dsi_clk_handle,
					  DSI_CORE_CLK, DSI_CLK_OFF);
	}

	dsi_display_cmd_engine_disable(display);

unlock:

	mutex_unlock(&display->panel->panel_lock);
	mutex_unlock(&display->display_lock);
	return 0;
}

#ifdef OPLUS_BUG_STABILITY
int oplus_display_get_dither_status(void *buf)
{
	uint32_t *dither_enable = buf;
	*dither_enable = oplus_dither_enable;

	return 0;
}

int oplus_display_set_dither_status(void *buf)
{
	uint32_t *dither_enable = buf;
	oplus_dither_enable = *dither_enable;
	pr_err("debug for %s, buf = [%s], oplus_dither_enable = %d\n",
	       __func__, buf, oplus_dither_enable);

	return 0;
}
#endif

int oplus_display_panel_opec_control(bool enable)
{
	int rc = 0;
	struct dsi_display *display = get_main_display();
	struct dsi_panel *panel = display->panel;

	if (!display || !panel) {
		DSI_ERR("Invalid params\n");
		return -EINVAL;
	}

	if (enable) {
		if (!strcmp(panel->oplus_priv.vendor_name, "AMB670YF01")) {
			if (panel->panel_id2 == 0x07 || panel->panel_id2 == 0x08) {
				rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_OPEC_ON_ID7);
				if (rc) {
					DSI_ERR("[%s] failed to send DSI_CMD_SET_OPEC_ON_ID7 cmds, rc=%d\n",
						panel->name, rc);
				}
			} else if (panel->panel_id2 >= 0x09 && panel->panel_id2 != 0x15) {
				rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_OPEC_ON);
				if (rc) {
					DSI_ERR("[%s] failed to send DSI_CMD_SET_OPEC_ON cmds, rc=%d\n",
						panel->name, rc);
				}
			}
		}
	}

	return rc;
}

void oplus_display_panel_enable(void)
{
	bool opec_enable = true;
	struct dsi_display *display = get_main_display();
	struct dsi_panel *panel = display->panel;

	if (!display || !panel) {
		DSI_ERR("Invalid params\n");
		return;
	}

	if (!strcmp(panel->oplus_priv.vendor_name, "AMB670YF01")) {
		oplus_display_panel_opec_control(opec_enable);
		dsi_panel_loading_effect_mode_unlock(panel, PANEL_LOADING_EFFECT_OFF);
	}

	return;
}

int oplus_display_get_dp_support(void *buf)
{
	struct dsi_display *display = NULL;
	struct dsi_panel *d_panel = NULL;
	uint32_t *dp_support = buf;

	display = get_main_display();
	if (!display) {
		printk(KERN_INFO "oplus_display_get_dp_support error get main display is null");
		return -EINVAL;
	}

	d_panel = display->panel;
	if (!d_panel) {
		printk(KERN_INFO "oplus_display_get_dp_support error get main panel is null");
		return -EINVAL;
	}

	if ((!strcmp(d_panel->oplus_priv.vendor_name, "S6E3HC3"))
		|| (!strcmp(d_panel->oplus_priv.vendor_name, "AMB670YF01"))
		|| (!strcmp(d_panel->oplus_priv.vendor_name, "NT37701"))) {
		*dp_support = true;
	} else {
		*dp_support = false;
	}

	return 0;
}


int oplus_display_get_cabc_status(void *buf)
{
	uint32_t *cabc_status = buf;
	struct dsi_display *display = NULL;
	struct dsi_panel *panel = NULL;

	display = get_main_display();
	if (!display) {
		DSI_ERR("No display device\n");
		return -ENODEV;
	}

	panel = display->panel;
	if (!panel) {
		DSI_ERR("No panel device\n");
		return -ENODEV;
	}

	if(panel->oplus_priv.cabc_enabled) {
		*cabc_status = oplus_cabc_status;
	} else {
		*cabc_status = OPLUS_DISPLAY_CABC_OFF;
	}
	return 0;
}

int oplus_display_set_cabc_status(void *buf)
{
	int rc = 0;
	uint32_t *cabc_status = buf;
	struct dsi_display *display = NULL;
	struct dsi_panel *panel = NULL;

	display = get_main_display();
	if (!display) {
		DSI_ERR("No display device\n");
		return -ENODEV;
	}

	panel = display->panel;
	if (!panel) {
		DSI_ERR("No panel device\n");
		return -ENODEV;
	}

	if (!panel->oplus_priv.cabc_enabled) {
		DSI_ERR("This project don't support cabc\n");
		return -EFAULT;
	}

	if (*cabc_status >= OPLUS_DISPLAY_CABC_UNKNOW) {
		DSI_ERR("Unknow cabc status = [%d]\n", *cabc_status);
		return -EINVAL;
	}

	if(get_oplus_display_power_status() == OPLUS_DISPLAY_POWER_ON) {
			if (*cabc_status == OPLUS_DISPLAY_CABC_OFF) {
				rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_CABC_OFF);
				if (rc) {
					DSI_ERR("[%s] failed to send DSI_CMD_CABC_OFF cmds, rc=%d\n",
							panel->name, rc);
				}
			} else if (*cabc_status == OPLUS_DISPLAY_CABC_UI) {
				rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_CABC_UI);
				if (rc) {
					DSI_ERR("[%s] failed to send DSI_CMD_CABC_UI cmds, rc=%d\n",
							panel->name, rc);
				}
			} else if (*cabc_status == OPLUS_DISPLAY_CABC_IMAGE) {
				rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_CABC_IMAGE);
				if (rc) {
					DSI_ERR("[%s] failed to send DSI_CMD_CABC_IMAGE cmds, rc=%d\n",
							panel->name, rc);
				}
			}  else if (*cabc_status == OPLUS_DISPLAY_CABC_VIDEO) {
				rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_CABC_VIDEO);
				if (rc) {
					DSI_ERR("[%s] failed to send DSI_CMD_CABC_VIDEO cmds, rc=%d\n",
							panel->name, rc);
				}
			}
		oplus_cabc_status = *cabc_status;
		pr_err("debug for %s, buf = [%s], oplus_cabc_status = %d\n",
				__func__, buf, oplus_cabc_status);
	} else {
		pr_err("debug for %s, buf = [%s], but display panel status is not on!\n",
				__func__, *cabc_status);
	}
	return rc;
}

int oplus_display_get_dre_status(void *buf)
{
	uint32_t *dre_status = buf;
	struct dsi_display *display = NULL;
	struct dsi_panel *panel = NULL;

	display = get_main_display();
	if (!display) {
		DSI_ERR("No display device\n");
		return -ENODEV;
	}

	panel = display->panel;
	if (!panel) {
		DSI_ERR("No panel device\n");
		return -ENODEV;
	}

	if(panel->oplus_priv.dre_enabled) {
		*dre_status = oplus_dre_status;
	} else {
		*dre_status = OPLUS_DISPLAY_DRE_OFF;
	}
	return 0;
}

int oplus_display_set_dre_status(void *buf)
{
	int rc = 0;
	uint32_t *dre_status = buf;
	struct dsi_display *display = NULL;
	struct dsi_panel *panel = NULL;

	display = get_main_display();
	if (!display) {
		DSI_ERR("No display device\n");
		return -ENODEV;
	}

	panel = display->panel;
	if (!panel) {
		DSI_ERR("No panel device\n");
		return -ENODEV;
	}

	if(!panel->oplus_priv.dre_enabled) {
		DSI_ERR("This project don't support dre\n");
		return -EFAULT;
	}

	if (*dre_status >= OPLUS_DISPLAY_DRE_UNKNOW) {
		DSI_ERR("Unknow DRE status = [%d]\n", *dre_status);
		return -EINVAL;
	}

	if(get_oplus_display_power_status() == OPLUS_DISPLAY_POWER_ON) {
		if (*dre_status == OPLUS_DISPLAY_DRE_ON) {
			/* if(mtk)  */
			/*	disp_aal_set_dre_en(0);   MTK AAL api */
		} else {
			/* if(mtk) */
			/*	disp_aal_set_dre_en(1);  MTK AAL api */
		}
		oplus_dre_status = *dre_status;
		pr_err("debug for %s, buf = [%s], oplus_dre_status = %d\n",
				__func__, buf, oplus_dre_status);
	} else {
		pr_err("debug for %s, buf = [%s], but display panel status is not on!\n",
				__func__, *dre_status);
	}
	return rc;
}

