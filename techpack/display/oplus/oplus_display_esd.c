/***************************************************************
** Copyright (C),  2021,  oplus Mobile Comm Corp.,  Ltd
** File : oplus_display_esd.c
** Description : oplus esd feature
** Version : 1.0
** Date : 2021/01/14
**
** ------------------------------- Revision History: -----------
**  <author>        <data>        <version >        <desc>
**   XXXXX         2021/01/14        1.0           Build this moudle
******************************************************************/

#include "oplus_display_private_api.h"
#include "oplus_display_esd.h"
#if defined(OPLUS_FEATURE_PXLW_IRIS5)
#include "iris/dsi_iris5_api.h"
#include "iris/dsi_iris5_lightup.h"
#include "iris/dsi_iris5_loop_back.h"
#endif

#include <video/mipi_display.h>

static int esd_black_count = 0;
static int esd_greenish_count = 0;

static int oplus_panel_read_panel_reg(struct dsi_display_ctrl *ctrl,
			     struct dsi_panel *panel, u8 cmd, void *rbuf,  size_t len)
{
	int rc = 0;
	struct dsi_cmd_desc cmdsreq;
	u32 flags = 0;

	if (!panel || !ctrl || !ctrl->ctrl) {
		return -EINVAL;
	}

	if (!dsi_ctrl_validate_host_state(ctrl->ctrl)) {
		return -EINVAL;
	}

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
	return rc;
}

int oplus_display_read_panel_reg(struct dsi_display *display, u8 cmd, void *data,
			       size_t len)
{
	int rc = 0;
	struct dsi_display_ctrl *m_ctrl;

	if (!display || !display->panel || data == NULL) {
		pr_err("%s, Invalid params\n", __func__);
		return -EINVAL;
	}

	m_ctrl = &display->ctrl[display->cmd_master_idx];

	if (display->tx_cmd_buf == NULL) {
		rc = dsi_host_alloc_cmd_tx_buffer(display);

		if (rc) {
			pr_err("%s, failed to allocate cmd tx buffer memory\n", __func__);
			goto done;
		}
	}

	rc = oplus_panel_read_panel_reg(m_ctrl, display->panel, cmd, data, len);
	if (rc < 0) {
		pr_err("%s, [%s] failed to read panel register, rc=%d,cmd=%d\n",
		       __func__,
		       display->name,
		       rc,
		       cmd);
	}

done:
	return rc;
}

static int oplus_mdss_dsi_samsung_amb655x_dsc_panel_check_esd_status(struct dsi_display *display)
{
	int rc = 0;
	unsigned char register1[30] = {0};
	unsigned char register2[30] = {0};
	unsigned char register3[30] = {0};

	rc = oplus_display_read_panel_reg(display, 0x0A, register1, 1);
	if (rc < 0)
	  return 0;

	rc = oplus_display_read_panel_reg(display, 0xB6, register2, 1);
	if (rc < 0)
	  return 0;

	rc = oplus_display_read_panel_reg(display, 0xA2, register3, 5);
	if (rc < 0)
	  return 0;

	if ((register1[0] != 0x9c) || (register2[0] != 0x0a) || (register3[0] != 0x12) || (register3[1] != 0x00)
		|| (register3[2] != 0x00) || (register3[3] != 0x89) || (register3[4] != 0x30)) {
		if ((register1[0] != 0x9c) || (register3[0] != 0x12) || (register3[1] != 0x00)
		  || (register3[2] != 0x00) || (register3[3] != 0x89) || (register3[4] != 0x30))
			esd_black_count++;
		if (register2[0] != 0x0a)
			esd_greenish_count++;
		DSI_ERR("0x0A = %02x, 0xB6 = %02x, 0xA2 = %02x, %02x, %02x, %02x, %02x\n", register1[0], register2[0],
			  register3[0], register3[1], register3[2], register3[3], register3[4]);
		DSI_ERR("black_count=%d, greenish_count=%d, total=%d\n",
			  esd_black_count, esd_greenish_count, esd_black_count + esd_greenish_count);
		rc = -1;
#ifdef OPLUS_BUG_STABILITY
		if (rc <= 0) {
			char payload[200] = "";
			int cnt = 0;
			cnt += scnprintf(payload + cnt, sizeof(payload) - cnt, "ESD:");
			cnt += scnprintf(payload + cnt, sizeof(payload) - cnt, "0x0A = %02x, 0xB6 = %02x, 0xA2 = %02x, %02x, %02x, %02x, %02x",
				register1[0], register2[0], register3[0], register3[1], register3[2], register3[3], register3[4]);
			DRM_ERROR("ESD check failed: %s\n", payload);
			mm_fb_display_kevent(payload, MM_FB_KEY_RATELIMIT_1H, "ESD check failed");
		}
#endif  /*OPLUS_BUG_STABILITY*/
	} else {
		rc = 1;
	}
	return rc;
}

static int oplus_mdss_dsi_boe_nt37701_dsc_panel_check_esd_status(struct dsi_display *display)
{
	int rc = 0;
	unsigned char register1[30] = {0};
	unsigned char register2[30] = {0};
	unsigned char register3[30] = {0};
	struct dsi_panel *panel = display->panel;

	rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_ESD_SWITCH_PAGE);
	if (rc) {
		pr_err("[%s] failed to send DSI_CMD_AOD_LIGHT_MODE cmds, rc=%d\n", panel->name, rc);
	}

	rc = oplus_display_read_panel_reg(display, 0xFA, register3, 1);
	if (rc < 0) {
		DSI_ERR("Read BOE ESD panel register2 0xFA failed, return 0\n");
		return 0;
	}

	rc = oplus_display_read_panel_reg(display, 0x0A, register1, 1);
	if (rc < 0) {
		DSI_ERR("Read BOE ESD panel register1 0x0A failed, return 0\n");
		return 0;
	}

	rc = oplus_display_read_panel_reg(display, 0xAB, register2, 1);
	if (rc < 0) {
		DSI_ERR("Read BOE ESD panel register3 0xAB failed, return 0\n");
		return 0;
	}

	if ((register1[0] == 0x9C) && (register2[0] == 0x00) && (register3[0] == 0x00)) {
		rc = 1;
	} else {
		DSI_ERR("ESD check failed : 0x0A = %02x, 0xAB = %02x, 0xFA = %02x\n", register1[0], register2[0], register3[0]);
		rc = -1;
#ifdef OPLUS_BUG_STABILITY
				if (rc <= 0) {
			char payload[200] = "";
			int cnt = 0;
			cnt += scnprintf(payload + cnt, sizeof(payload) - cnt, "ESD:");
			cnt += scnprintf(payload + cnt, sizeof(payload) - cnt, "0x0A = %02x, 0xFA = %02x, 0xAB = %02x,",
				register1[0], register2[0], register3[0]);
			DRM_ERROR("ESD check failed: %s\n", payload);
			mm_fb_display_kevent(payload, MM_FB_KEY_RATELIMIT_1H, "ESD check failed");
		}
#endif  /*OPLUS_BUG_STABILITY*/
	}

	return rc;
}

static int oplus_mdss_dsi_samsung_amb670yf01_dsc_panel_check_esd_status(struct dsi_display *display)
{
	int rc = 0;
	unsigned char register1[30] = {0};
	unsigned char register2[30] = {0};
#if defined(OPLUS_FEATURE_PXLW_IRIS5)
	struct dsi_cmd_desc cmds;
	unsigned char payload;
	struct dsi_display_ctrl *m_ctrl = &display->ctrl[display->cmd_master_idx];
	struct dsi_panel *panel = display->panel;

	if ((iris_is_chip_supported()) && (iris_is_pt_mode(panel))) {
		rc = iris_get_status();
		if (rc <= 0) {
			DSI_ERR("Iris ESD snow screen error\n");
			return -1;
		}

		memset(&cmds, 0x0, sizeof(cmds));
		payload = 0x0A;
		cmds.msg.type = 0x06;
		cmds.msg.tx_buf = &payload;
		cmds.msg.tx_len = 1;
		cmds.msg.rx_buf = register1;
		cmds.msg.rx_len = 1;
		cmds.msg.flags |= MIPI_DSI_MSG_LASTCOMMAND;

		rc = iris_panel_ctrl_read_reg(m_ctrl, panel, register1, 1, &cmds);
		if (rc <= 0) {
			DSI_ERR("iris_panel_ctrl_read_reg 1 failed, rc=%d\n", rc);
			return rc;
		}

		memset(&cmds, 0x0, sizeof(cmds));
		payload = 0xA2;
		cmds.msg.type = 0x06;
		cmds.msg.tx_buf = &payload;
		cmds.msg.tx_len = 1;
		cmds.msg.rx_buf = register2;
		cmds.msg.rx_len = 1;
		cmds.msg.flags |= MIPI_DSI_MSG_LASTCOMMAND;

		rc = iris_panel_ctrl_read_reg(m_ctrl, panel, register2, 5, &cmds);
		if (rc <= 0) {
			DSI_ERR("iris_panel_ctrl_read_reg 1 failed, rc=%d\n", rc);
			return rc;
		}

		if ((register1[0] != 0x9F && register1[0] != 0x9d) || (register2[0] != 0x11) || (register2[1] != 0x00)
		|| (register2[2] != 0x00) || (register2[3] != 0xAB) || (register2[4] != 0x30)) {
			esd_black_count++;
			DSI_ERR("black_count=%d\n", esd_black_count);
			DSI_ERR("0x0A = %02x, 0xA2 = %02x, %02x, %02x, %02x, %02x\n", register1[0],
			  register2[0], register2[1], register2[2], register2[3], register2[4]);
			rc = -1;
#ifdef OPLUS_BUG_STABILITY
			if (rc <= 0) {
				char payload[200] = "";
				int cnt = 0;
				cnt += scnprintf(payload + cnt, sizeof(payload) - cnt, "ESD:");
				cnt += scnprintf(payload + cnt, sizeof(payload) - cnt, "0x0A = %02x, 0xA2 = %02x, %02x, %02x, %02x, %02x",
					register1[0], register2[0], register2[1], register2[2], register2[3], register2[4]);
				DRM_ERROR("ESD check failed: %s\n", payload);
				mm_fb_display_kevent(payload, MM_FB_KEY_RATELIMIT_1H, "ESD check failed");
			}
#endif  /*OPLUS_BUG_STABILITY*/
		} else {
			rc = 1;
		}
	} else {
#else
	{
#endif
		rc = oplus_display_read_panel_reg(display, 0x0A, register1, 1);
		if (rc < 0)
			return 0;

		rc = oplus_display_read_panel_reg(display, 0xA2, register2, 5);
		if (rc < 0)
			return 0;
	}
	if ((register1[0] != 0x9F && register1[0] != 0x9d) || (register2[0] != 0x11) || (register2[1] != 0x00)
		|| (register2[2] != 0x00) || (register2[3] != 0xAB) || (register2[4] != 0x30)) {
		esd_black_count++;
		DSI_ERR("black_count=%d\n", esd_black_count);
		DSI_ERR("0x0A = %02x, 0xA2 = %02x, %02x, %02x, %02x, %02x\n", register1[0],
			  register2[0], register2[1], register2[2], register2[3], register2[4]);
		rc = -1;
	} else {
		rc = 1;
	}
	return rc;
}

int oplus_display_status_reg_read(struct dsi_display *display)
{
	int rc = 0;
	struct dsi_display_mode *mode;
	struct dsi_panel *panel = NULL;
	int panel_esd_status = 0;
	int count = 0;

	DSI_DEBUG(" ++\n");

	if (display->tx_cmd_buf == NULL) {
		rc = dsi_host_alloc_cmd_tx_buffer(display);
		if (rc) {
			return 0;
		}
	}

	if (!display->panel || !display->panel->cur_mode) {
		DSI_ERR("panel or cur_mode is NULL!\n");
		return 0;
	}

	mode = display->panel->cur_mode;
	panel = display->panel;

	count = mode->priv_info->cmd_sets[DSI_CMD_READ_SAMSUNG_PANEL_REGISTER_ON].count;
	if (!count) {
		DSI_ERR("This panel does not support samsung panel register enable command\n");
	} else {
		if(dsi_panel_tx_cmd_set(panel, DSI_CMD_READ_SAMSUNG_PANEL_REGISTER_ON)){
			DSI_ERR("Failed to send DSI_CMD_READ_SAMSUNG_PANEL_REGISTER_ON commands\n");
			return 0;
		}
	}

	if (!strcmp(panel->oplus_priv.vendor_name, "AMB655X")) {
		panel_esd_status = oplus_mdss_dsi_samsung_amb655x_dsc_panel_check_esd_status(display);
	} else if (!strcmp(panel->oplus_priv.vendor_name, "AMB670YF01")) {
		panel_esd_status = oplus_mdss_dsi_samsung_amb670yf01_dsc_panel_check_esd_status(display);
	} else if (!strcmp(panel->oplus_priv.vendor_name, "NT37701")) {
		panel_esd_status = oplus_mdss_dsi_boe_nt37701_dsc_panel_check_esd_status(display);
	}

	count = mode->priv_info->cmd_sets[DSI_CMD_READ_SAMSUNG_PANEL_REGISTER_OFF].count;
	if (!count)
		DSI_ERR("This panel does not support samsung panel register disable command\n");
	else {
		if(dsi_panel_tx_cmd_set(panel, DSI_CMD_READ_SAMSUNG_PANEL_REGISTER_OFF)) {
			DSI_ERR("Failed to send DSI_CMD_READ_SAMSUNG_PANEL_REGISTER_OFF commands\n");
			return 0;
		}
	}

	return panel_esd_status;
}
