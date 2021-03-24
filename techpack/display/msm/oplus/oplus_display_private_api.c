/***************************************************************
** Copyright (C),  2021,  Oplus Mobile Comm Corp.,  Ltd
** File : oplus_display_private_api.c
** Description : oplus display private api implement
** Version : 1.0
** Date : 2020/12/09
** Author :
**
** ------------------------------- Revision History: -----------
**  <author>        <data>        <version >        <desc>
**                 2020/12/09        1.0           Build this moudle
******************************************************************/
#include <linux/device.h>
#include "drm_mipi_dsi.h"
#include "oplus_display_private_api.h"
#include "dsi_panel.h"

#if defined(CONFIG_PXLW_IRIS)
#include "../dsi/iris/dsi_iris5_api.h"
#include "../dsi/iris/dsi_iris5_lightup.h"
#include "../dsi/iris/dsi_iris5_loop_back.h"
#elif defined(CONFIG_PXLW_SOFT_IRIS)
#include "../dsi/iris/dsi_iris5_api.h"
#endif

#define to_dsi_bridge(x)  container_of((x), struct dsi_bridge, base)

extern ssize_t oplus_mipi_dcs_read_buffer(struct mipi_dsi_device *dsi, u8 cmd, const void *data, size_t len);
extern ssize_t oplus_mipi_dcs_write_buffer(struct mipi_dsi_device *dsi, const void *data, size_t len);

typedef enum {
	ToolB = 0,
	ToolA = 1,
	ToolA_HVS30 = 2,
} eTool;

typedef struct {
	char LotID[6];
	int wafer_Start;
	int wafer_End;
	int HVS30;
} LotDBItem;

LotDBItem ANA6705_ToolA_DB[109] = {
	{"K2T7N", 0, 0, 0},
	{"K2T7P", 0, 0, 0},
	{"K2TK4", 0, 0, 0},
	{"K4ART", 1, 12, 1},
	{"K4C07", 0, 0, 1},
	{"K4C0A", 1, 12, 1},
	{"K4C7S", 0, 0, 1},
	{"K4C7T", 0, 0, 1},
	{"K4CCH", 0, 0, 1},
	{"K4CCN", 0, 0, 1},
	{"K4CCP", 0, 0, 1},
	{"K4CJL", 0, 0, 1},
	{"K4CNS", 0, 0, 1},
	{"K4C06", 0, 0, 1},
	{"K4CNW", 0, 0, 1},
	{"K4JGT", 0, 0, 1},
	{"K4F8J", 0, 0, 1},
	{"K4FFA", 0, 0, 1},
	{"K4F4G", 0, 0, 1},
	{"K4C82", 0, 0, 1},
	{"K4CJM", 0, 0, 1},
	{"K4CNT", 0, 0, 1},
	{"K4F0T", 0, 0, 1},
	{"K4F4K", 0, 0, 1},
	{"K4F4N", 0, 0, 1},
	{"K4JA8", 0, 0, 1},
	{"K4JA8", 0, 0, 1},
	{"K4J54", 0, 0, 1},
	{"K4F4P", 0, 0, 1},
	{"K4M9N", 0, 0, 1},
	{"K4J6F", 0, 0, 1},
	{"K4FFC", 0, 0, 1},
	{"K4JQP", 0, 0, 1},
	{"K4K5A", 0, 0, 1},
	{"K4K19", 0, 0, 1},
	{"K4K7L", 0, 0, 1},
	{"K4JW4", 0, 0, 1},
	{"K4MGK", 0, 0, 1},
	{"K4KTR", 0, 0, 1},
	{"K4L07", 0, 0, 1},
	{"K4L07", 0, 0, 1},
	{"K4MGJ", 0, 0, 1},
	{"K4JLA", 0, 0, 1},
	{"K4KTS", 0, 0, 1},
	{"K4MGL", 0, 0, 1},
	{"K4JJS", 0, 0, 1},
	{"K4PYR", 0, 0, 1},
	{"K4PS4", 0, 0, 1},
	{"K4QC2", 0, 0, 1},
	{"K4Q7K", 0, 0, 1},
	{"K4PS5", 0, 0, 1},
	{"K4Q3Q", 0, 0, 1},
	{"K4Q3R", 0, 0, 1},
	{"K4QC0", 0, 0, 1},
	{"K4QHT", 0, 0, 1},
	{"K4QC1", 0, 0, 1},
	{"K4QHW", 0, 0, 1},
	{"K4QMP", 0, 0, 1},
	{"K4QMQ", 0, 0, 1},
	{"K4QMR", 0, 0, 1},
	{"K4Q7L", 0, 0, 1},
	{"K4QRL", 0, 0, 1},
	{"K4QYM", 0, 0, 1},
	{"K4PYQ", 0, 0, 1},
	{"K4QYN", 0, 0, 1},
	{"K4R7A", 0, 0, 1},
	{"K4QRM", 0, 0, 1},
	{"K4R7F", 0, 0, 1},
	{"K4R3L", 0, 0, 1},
	{"K4QYP", 0, 0, 1},
	{"K4R3K", 0, 0, 1},
	{"K4RJ7", 0, 0, 1},
	{"K4R7C", 0, 0, 1},
	{"K4RC8", 0, 0, 1},
	{"K4RNW", 0, 0, 1},
	{"K4RS4", 0, 0, 1},
	{"K4RC9", 0, 0, 1},
	{"K4RJ8", 0, 0, 1},
	{"K4RNS", 0, 0, 1},
	{"K4RNT", 0, 0, 1},
	{"K4RS5", 0, 0, 1},
	{"K4RYL", 0, 0, 1},
	{"K4RYM", 0, 0, 1},
	{"K4S1S", 0, 0, 1},
	{"K4S78", 0, 0, 1},
	{"K4SAY", 0, 0, 1},
	{"K4SHS", 0, 0, 1},
	{"K4SHT", 0, 0, 1},
	{"K4S1T", 0, 0, 1},
	{"K4S77", 0, 0, 1},
	{"K4SC1", 0, 0, 1},
	{"K4SMM", 0, 0, 1},
	{"K4SC0", 0, 0, 1},
	{"K4SRA", 0, 0, 1},
	{"K4TAM", 0, 0, 1},
	{"K4TAN", 0, 0, 1},
	{"K5G14", 0, 0, 1},
	{"K5G16", 0, 0, 1},
	{"K5G15", 0, 0, 1},
	{"K5G4W", 0, 0, 1},
	{"K5G4Y", 0, 0, 1},
	{"K5G8W", 0, 0, 1},
	{"K5G8Y", 0, 0, 1},
	{"K5GFS", 0, 0, 1},
	{"K5GFT", 0, 0, 1},
	{"K5GFW", 0, 0, 1},
	{"K5GL5", 0, 0, 1},
	{"K5GL6", 0, 0, 1},
	{"K5GNY", 0, 0, 1}
};

LotDBItem ANA6706_ToolA_DB[121] = {
	{"K4AN0", 1, 12, 0},
	{"K4AJG", 1, 12, 0},
	{"K4AS4", 1, 12, 0},
	{"K4H99", 0, 0, 0},
	{"K4C4C", 0, 0, 1},
	{"K4H9A", 0, 0, 1},
	{"K4HAC", 0, 0, 1},
	{"K4J55", 0, 0, 1},
	{"K4HAC", 0, 0, 1},
	{"K4HM2", 0, 0, 1},
	{"K4HPW", 0, 0, 1},
	{"K4HYW", 0, 0, 1},
	{"K4J56", 0, 0, 1},
	{"K4J6G", 0, 0, 1},
	{"K4J6H", 0, 0, 1},
	{"K4J6J", 0, 0, 1},
	{"K4JA9", 0, 0, 1},
	{"K4JAA", 0, 0, 1},
	{"K4JLH", 0, 0, 1},
	{"K4JQR", 0, 0, 1},
	{"K4JLG", 0, 0, 1},
	{"K4HJ0", 0, 0, 1},
	{"K4JAF", 0, 0, 1},
	{"K4JGW", 0, 0, 1},
	{"K4JGY", 0, 0, 1},
	{"K4JLF", 0, 0, 1},
	{"K4J29", 0, 0, 1},
	{"K4JAC", 0, 0, 1},
	{"K4JH0", 0, 0, 1},
	{"K4JW7", 0, 0, 1},
	{"K4HS4", 0, 0, 1},
	{"K4HYY", 0, 0, 1},
	{"K4K5G", 0, 0, 1},
	{"K4JLC", 0, 0, 1},
	{"K4KL8", 0, 0, 1},
	{"K4K1G", 0, 0, 1},
	{"K4K5C", 0, 0, 1},
	{"K4JQQ", 0, 0, 1},
	{"K4KG8", 0, 0, 1},
	{"K4KQL", 0, 0, 1},
	{"K4KTT", 0, 0, 1},
	{"K4KG9", 0, 0, 1},
	{"K4L5G", 0, 0, 1},
	{"K4K1C", 0, 0, 1},
	{"K4K5F", 0, 0, 1},
	{"K4K9L", 0, 0, 1},
	{"K4KG6", 0, 0, 1},
	{"K4KQK", 0, 0, 1},
	{"K4KG9", 0, 0, 1},
	{"K4JQS", 0, 0, 1},
	{"K4JW5", 0, 0, 1},
	{"K4KG7", 0, 0, 1},
	{"K4KL9", 0, 0, 1},
	{"K4K9H", 0, 0, 1},
	{"K4L9G", 0, 0, 1},
	{"K4K5H", 0, 0, 1},
	{"K4K9J", 0, 0, 1},
	{"K4K9K", 0, 0, 1},
	{"K4KLA", 0, 0, 1},
	{"K4L1J", 0, 0, 1},
	{"K4L1K", 0, 0, 1},
	{"K4L1L", 0, 0, 1},
	{"K4L5H", 0, 0, 1},
	{"K4L5J", 0, 0, 1},
	{"K4L9H", 0, 0, 1},
	{"K4L9J", 0, 0, 1},
	{"K4LGA", 0, 0, 1},
	{"K4LGC", 0, 0, 1},
	{"K4LKY", 0, 0, 1},
	{"K4LL0", 0, 0, 1},
	{"K4LL1", 0, 0, 1},
	{"K4LPQ", 0, 0, 1},
	{"K4LPR", 0, 0, 1},
	{"K4LPS", 0, 0, 1},
	{"K4LTP", 0, 0, 1},
	{"K4LTQ", 0, 0, 1},
	{"K4LTR", 0, 0, 1},
	{"K4M1F", 0, 0, 1},
	{"K4M1G", 0, 0, 1},
	{"K4M5M", 0, 0, 1},
	{"K4M5N", 0, 0, 1},
	{"K4M5P", 0, 0, 1},
	{"K4M9P", 0, 0, 1},
	{"K4M9Q", 0, 0, 1},
	{"K4MLL", 0, 0, 1},
	{"K4MLM", 0, 0, 1},
	{"K4MLN", 0, 0, 1},
	{"K4MQY", 0, 0, 1},
	{"K4MR0", 0, 0, 1},
	{"K4MWS", 0, 0, 1},
	{"K4MWT", 0, 0, 1},
	{"K4MWW", 0, 0, 1},
	{"K4N2K", 0, 0, 1},
	{"K4N2L", 0, 0, 1},
	{"K4N66", 0, 0, 1},
	{"K4N67", 0, 0, 1},
	{"K4N68", 0, 0, 1},
	{"K4NPW", 0, 0, 1},
	{"K4NPY", 0, 0, 1},
	{"K4NQ0", 0, 0, 1},
	{"K4NTS", 0, 0, 1},
	{"K4NTT", 0, 0, 1},
	{"K4NTW", 0, 0, 1},
	{"K4P1F", 0, 0, 1},
	{"K4P1G", 0, 0, 1},
	{"K4P1H", 0, 0, 1},
	{"K4P51", 0, 0, 1},
	{"K4P52", 0, 0, 1},
	{"K4P53", 0, 0, 1},
	{"K4P8M", 0, 0, 1},
	{"K4P8N", 0, 0, 1},
	{"K4SMN", 0, 0, 1},
	{"K4SMP", 0, 0, 1},
	{"K4SRC", 0, 0, 1},
	{"K4SRF", 0, 0, 1},
	{"K4SY4", 0, 0, 1},
	{"K4SY5", 0, 0, 1},
	{"K4T6T", 0, 0, 1},
	{"K4T6W", 0, 0, 1},
	{"K4TAP", 0, 0, 1},
	{"K4TAQ", 0, 0, 1}
};

LotDBItem ANA6706_ToolB_DB[8] = {
	{"K4A6P", 0, 0, 0},
	{"K4C0C", 0, 0, 0},
	{"K4A85", 0, 0, 0},
	{"K4AF3", 0, 0, 0},
	{"K4AN0", 13, 25, 0},
	{"K4AJG", 13, 25, 0},
	{"K4AS4", 13, 24, 0},
	{"K4HAR", 0, 0, 0},
};

/**********************************************************
* Function:
* 	mipi_dsi_dcs_set_display_brightness_samsung()
* Description:
* 	sets the brightness value of the display
* Parameters:
* 	dsi_display [IN] dsi display struct
* 	brightness  [IN] brightness level
* Returns:
* 	0 on success or a negative error code on failure.
*********************************************************/
int mipi_dsi_dcs_set_display_brightness_samsung(struct dsi_display *dsi_display, u16 brightness)
{
	u8 payload[2] = {brightness >> 8, brightness & 0xff};

	return dsi_display_write_panel_reg(dsi_display, MIPI_DCS_SET_DISPLAY_BRIGHTNESS, payload, 2);
}

#if defined(CONFIG_PXLW_IRIS)
int iris_loop_back_test(struct drm_connector *connector)
{
	int ret = -1;
	struct iris_cfg *pcfg;
	struct dsi_display *dsi_display = NULL;
	struct dsi_bridge *c_bridge;

	DSI_ERR("%s start\n", __func__);

	if ((connector == NULL) || (connector->encoder == NULL)
			|| (connector->encoder->bridge == NULL))
		return -EINVAL;

	c_bridge =  to_dsi_bridge(connector->encoder->bridge);
	dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return -EINVAL;

	if (dsi_display->panel->panel_initialized == true) {
		pcfg = iris_get_cfg();
		mutex_lock(&pcfg->lb_mutex);
		ret = iris_loop_back_validate();
		DSI_ERR("iris_loop_back_validate finish, ret = %d", ret);
		mutex_unlock(&pcfg->lb_mutex);
	}
	DSI_ERR("%s end\n", __func__);
	return ret;
}
#endif

/* ---------------- dsi_panel ----------------*/
int dsi_panel_set_native_loading_effect_mode_nolock(struct dsi_panel *panel, int level)
{
	int rc = 0;

	if (!panel) {
		DSI_ERR("Invalid params\n");
		return -EINVAL;
	}

	if (level == 0) {
		if ((strcmp(panel->name, "samsung amb670yf01 dsc cmd mode panel") == 0)
			&& ((panel->panel_stage_info <= 0x06) || (panel->panel_stage_info == 0x15))) {
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_LOADING_EFFECT_OFF_O);
			if (rc) {
				DSI_ERR("[%s] failed to send DSI_CMD_LOADING_EFFECT_OFF_O cmds, rc=%d\n",
					panel->name, rc);
			} else {
				DSI_INFO("Loading effect o compensation control off \n");
			}
		} else {
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_LOADING_EFFECT_OFF);
			if (rc) {
				DSI_ERR("[%s] failed to send DSI_CMD_LOADING_EFFECT_OFF cmds, rc=%d\n",
					panel->name, rc);
			} else {
				DSI_INFO("Loading effect compensation control off\n");
			}
		}
	} else if (level == 1) {
		if ((strcmp(panel->name, "samsung amb670yf01 dsc cmd mode panel") == 0)
			&& ((panel->panel_stage_info <= 0x06) || (panel->panel_stage_info == 0x15))) {
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_LOADING_EFFECT_ON_1_O);
			if (rc) {
				DSI_ERR("[%s] failed to send DSI_CMD_LOADING_EFFECT_ON_1_O cmds, rc=%d\n",
					panel->name, rc);
			} else {
				DSI_INFO("Loading effect o compensation control on 1 \n");
			}
		} else {
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_LOADING_EFFECT_ON_1);
			if (rc) {
				DSI_ERR("[%s] failed to send DSI_CMD_LOADING_EFFECT_ON_1 cmds, rc=%d\n",
					panel->name, rc);
			} else {
				DSI_INFO("Loading effect compensation control on 1\n");
			}
		}
	} else if (level == 2) {
		if ((strcmp(panel->name, "samsung amb670yf01 dsc cmd mode panel") == 0)
			&& ((panel->panel_stage_info <= 0x06) || (panel->panel_stage_info == 0x15))) {
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_LOADING_EFFECT_ON_2_O);
			if (rc) {
				DSI_ERR("[%s] failed to send DSI_CMD_LOADING_EFFECT_ON_2_O cmds, rc=%d\n",
					panel->name, rc);
			} else {
				DSI_INFO("Loading effect o compensation control on 2 \n");
			}
		} else {
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_LOADING_EFFECT_ON_2);
			if (rc) {
				DSI_ERR("[%s] failed to send DSI_CMD_LOADING_EFFECT_ON_2 cmds, rc=%d\n",
					panel->name, rc);
			} else {
				DSI_INFO("Loading effect compensation control on 2\n");
			}
		}
	}

	return rc;
}

int dsi_panel_set_native_loading_effect_mode(struct dsi_panel *panel, int level)
{
	int rc = 0;

	if (!panel) {
		DSI_ERR("Invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);
	dsi_panel_set_native_loading_effect_mode_nolock(panel, level);
	mutex_unlock(&panel->panel_lock);

	return rc;
}

int dsi_panel_send_opec_command(struct dsi_panel *panel)
{
	int rc = 0;

	if (!panel) {
		DSI_ERR("Invalid params\n");
		return -EINVAL;
	}

	if (strcmp(panel->name, "samsung amb670yf01 dsc cmd mode panel") == 0) {
		if (panel->panel_stage_info == 0x07 || panel->panel_stage_info == 0x08) {
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_OPEC_COMMAND_O);
			if (rc) {
				DSI_ERR("[%s] failed to send DSI_CMD_SET_OPEC_COMMAND_O cmds, rc=%d\n",
					panel->name, rc);
			} else {
				DSI_INFO("Send DSI_CMD_SET_OPEC_COMMAND_O cmds\n");
			}
		} else if (panel->panel_stage_info >= 0x09 && panel->panel_stage_info != 0x15) {
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_OPEC_COMMAND);
			if (rc) {
				DSI_ERR("[%s] failed to send DSI_CMD_SET_OPEC_COMMAND cmds, rc=%d\n",
					panel->name, rc);
			} else {
				DSI_INFO("Send DSI_CMD_SET_OPEC_COMMAND cmds\n");
			}
		}
	}

	return rc;
}

int oplus_dsi_panel_enable(void *dsi_panel)
{
	int rc = 0;
	struct dsi_panel *panel = dsi_panel;

	if (!panel) {
		DSI_ERR("Invalid params\n");
		return -EINVAL;
	}

	dsi_panel_send_opec_command(panel);
	dsi_panel_set_native_loading_effect_mode_nolock(panel, panel->naive_display_loading_effect_mode);

	return rc;
}

/* --------------- dsi_display ---------------*/

void extractLotID(unsigned char* chipID, char *szLotID)
{
	int i;
	unsigned long lotValue = (chipID[0] << 14) + (chipID[1] << 6) + (chipID[2] >> 2);

	szLotID[0] = 'K';
	szLotID[1] = ((long)(lotValue / (36 * 36 * 36)) % 36) + 'A';

	szLotID[2] = ((long)(lotValue / (36 * 36)) % 36) + 'A';
	szLotID[3] = ((long)(lotValue / 36) % 36) + 'A';
	szLotID[4] = (lotValue % 36) + 'A';

	for (i = 1; i < 5; i++) {
		if (szLotID[i] > 90)
			szLotID[i] = (szLotID[i] - 91) + '0';
	}
}

int extractWaferNumber(unsigned char* chipID)
{
	int noWafer;
	noWafer = ((chipID[2] & 0x03) << 3) + (chipID[3] >> 5);
	return noWafer;
}

eTool discrimination_ANA6705_ToolsType(char* szLotID, int WaferNumber)
{
	int i;
	int count = sizeof(ANA6705_ToolA_DB) / sizeof(LotDBItem);
	bool bFound = false;
	eTool toolType;
	for (i = 0; i < count; i++) {
		if (strncmp(szLotID, ANA6705_ToolA_DB[i].LotID, 5) == 0) {
			if (ANA6705_ToolA_DB[i].wafer_Start > 0) {
				if (WaferNumber >= ANA6705_ToolA_DB[i].wafer_Start && WaferNumber <= ANA6705_ToolA_DB[i].wafer_End) {
					bFound = true;
					if (ANA6705_ToolA_DB[i].HVS30)
						toolType = ToolA_HVS30;
					else
						toolType = ToolA;
				}
				break;
			}
			else {
				bFound = true;
				if (ANA6705_ToolA_DB[i].HVS30)
					toolType = ToolA_HVS30;
				else
					toolType = ToolA;

				break;
			}
		}
	}

	if (bFound == false)
		toolType = ToolB;

	return toolType;
}


eTool discrimination_ANA6706_ToolsType(char* szLotID, int WaferNumber)
{
	int i;
	int count = sizeof(ANA6706_ToolA_DB) / sizeof(LotDBItem);
	bool bFound = false;
	eTool toolType;
	for (i = 0; i < count; i++) {
		if (strncmp(szLotID, ANA6706_ToolA_DB[i].LotID, 5) == 0) {
			if (ANA6706_ToolA_DB[i].wafer_Start > 0) {
				if (WaferNumber >= ANA6706_ToolA_DB[i].wafer_Start && WaferNumber <= ANA6706_ToolA_DB[i].wafer_End) {
					bFound = true;
					if (ANA6706_ToolA_DB[i].HVS30)
						toolType = ToolA_HVS30;
					else
						toolType = ToolA;
				}
				break;
			}
			else {
				bFound = true;
				if (ANA6706_ToolA_DB[i].HVS30)
					toolType = ToolA_HVS30;
				else
					toolType = ToolA;

				break;
			}
		}
	}

	if (bFound == false)
		toolType = ToolB;

	return toolType;
}

int dsi_display_back_ToolsType_ANA67061(u8 *buff)
{
	int i;
	int WaferNumber;
	eTool typeTool;
	char szLotID[6] = { 0 };
	unsigned char chipID1[6] = { 0 };

	for(i = 1;i <= 5; i++)
		chipID1[i-1] = buff[i-1];

	// [6706] Chip IDLot IDWafer Number
	extractLotID(chipID1, szLotID);
	WaferNumber = extractWaferNumber(chipID1);
	// LotID Wafer Number Tool Type
	typeTool = discrimination_ANA6706_ToolsType(szLotID, WaferNumber);

	if (typeTool == ToolB)
		DSI_ERR("Result: 6706 LotID: %s \tWaferNo: %d, Tool: Tool-B (%d)\n", szLotID, WaferNumber, typeTool);
	else if (typeTool == ToolA)
		DSI_ERR("Result: 6706 LotID: %s \tWaferNo: %d, Tool: Tool-A (%d)\n", szLotID, WaferNumber, typeTool);
	else if (typeTool == ToolA_HVS30)
		DSI_ERR("Result: 6706 LotID: %s \tWaferNo: %d, Tool: Tool-A HVS 3.0 (%d)\n", szLotID, WaferNumber, typeTool);

	return typeTool;
}

int dsi_display_get_ToolsType_ANA6706(struct drm_connector *connector)
{
	struct dsi_display *dsi_display = NULL;
	struct dsi_bridge *c_bridge;
	DSI_DEBUG("%s start\n", __func__);

	if ((connector == NULL) || (connector->encoder == NULL)
		|| (connector->encoder->bridge == NULL))
		return 0;

	c_bridge =  to_dsi_bridge(connector->encoder->bridge);
	dsi_display = c_bridge->display;
	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return 0;

	DSI_DEBUG("%s end\n", __func__);

	return dsi_display->panel->panel_tool;
}

int dsi_display_get_ddic_coords_X(struct drm_connector *connector)
{
	struct dsi_display *dsi_display = NULL;
	struct dsi_bridge *c_bridge;
	DSI_DEBUG("%s start\n", __func__);

	if ((connector == NULL) || (connector->encoder == NULL)
		|| (connector->encoder->bridge == NULL))
		return 0;

	c_bridge =  to_dsi_bridge(connector->encoder->bridge);
	dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return 0;

	DSI_DEBUG("%s end\n", __func__);

	return dsi_display->panel->ddic_x;
}

int dsi_display_get_ddic_coords_Y(struct drm_connector *connector)
{
	struct dsi_display *dsi_display = NULL;
	struct dsi_bridge *c_bridge;
	DSI_DEBUG("%s start\n", __func__);

	if ((connector == NULL) || (connector->encoder == NULL)
		|| (connector->encoder->bridge == NULL))
		return 0;

	c_bridge =  to_dsi_bridge(connector->encoder->bridge);
	dsi_display = c_bridge->display;
	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return 0;

	DSI_DEBUG("%s end\n", __func__);

	return dsi_display->panel->ddic_y;
}

int dsi_display_get_ddic_check_info(struct drm_connector *connector)
{
	struct dsi_display *dsi_display = NULL;
	struct dsi_bridge *c_bridge;
	int ddic_x = 0;
	int panel_tool = 0;
	DSI_DEBUG("%s start\n", __func__);

	if ((connector == NULL) || (connector->encoder == NULL)
		|| (connector->encoder->bridge == NULL))
		return 0;

	c_bridge =  to_dsi_bridge(connector->encoder->bridge);
	dsi_display = c_bridge->display;
	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return 0;

	ddic_x = dsi_display->panel->ddic_x;
	panel_tool = dsi_display->panel->panel_tool;
/*
	ToolB         0
	ToolA         1
	ToolA_HVS30   2
	*/

	switch (dsi_display->panel->ddic_y) {
	case 2:
		if((strcmp(dsi_display->panel->name, "samsung ana6705 fhd cmd mode dsc dsi panel") == 0) &&
		(ddic_x > 115) && (ddic_x < 186)) {
			dsi_display->panel->ddic_check_info = 1;
		} else
			dsi_display->panel->ddic_check_info = 0;
		break;

	case 3:
		if((strcmp(dsi_display->panel->name, "samsung ana6705 fhd cmd mode dsc dsi panel") == 0) &&
		(ddic_x > 56) && (ddic_x < 245)) {
			dsi_display->panel->ddic_check_info = 1;
		} else if(strcmp(dsi_display->panel->name, "samsung ana6706 dsc cmd mode panel") == 0) {
			if((panel_tool == 0) && (ddic_x > 54) && (ddic_x < 140))
				dsi_display->panel->ddic_check_info = 1;
			if(((panel_tool == 1) || (panel_tool == 2)) && (ddic_x > 32) && (ddic_x < 154))
				dsi_display->panel->ddic_check_info = 1;
		} else
			dsi_display->panel->ddic_check_info = 0;
		break;

	case 4:
		if((strcmp(dsi_display->panel->name, "samsung ana6705 fhd cmd mode dsc dsi panel") == 0) &&
		(ddic_x > 40) && (ddic_x < 261)) {
			dsi_display->panel->ddic_check_info = 1;
		} else if(strcmp(dsi_display->panel->name, "samsung ana6706 dsc cmd mode panel") == 0) {
			if((panel_tool == 0) && (ddic_x > 46) && (ddic_x < 140))
				dsi_display->panel->ddic_check_info = 1;
			if(((panel_tool == 1) || (panel_tool == 2)) && (ddic_x > 24) && (ddic_x < 162))
				dsi_display->panel->ddic_check_info = 1;
		} else
			dsi_display->panel->ddic_check_info = 0;
		break;

	case 5:
		if((strcmp(dsi_display->panel->name, "samsung ana6705 fhd cmd mode dsc dsi panel") == 0) &&
		(ddic_x > 33) && (ddic_x < 268)) {
			dsi_display->panel->ddic_check_info = 1;
		} else if(strcmp(dsi_display->panel->name, "samsung ana6706 dsc cmd mode panel") == 0) {
			if((panel_tool == 0) && (ddic_x > 46) && (ddic_x < 140))
				dsi_display->panel->ddic_check_info = 1;
			if(((panel_tool == 1) || (panel_tool == 2)) && (ddic_x > 23) && (ddic_x < 163))
				dsi_display->panel->ddic_check_info = 1;
		} else
			dsi_display->panel->ddic_check_info = 0;
		break;

	case 6:
		if((strcmp(dsi_display->panel->name, "samsung ana6705 fhd cmd mode dsc dsi panel") == 0) &&
		(ddic_x > 41) && (ddic_x < 261)) {
			dsi_display->panel->ddic_check_info = 1;
		} else if(strcmp(dsi_display->panel->name, "samsung ana6706 dsc cmd mode panel") == 0) {
			if((panel_tool == 0) && (ddic_x > 54) && (ddic_x < 132))
				dsi_display->panel->ddic_check_info = 1;
			if(((panel_tool == 1) || (panel_tool == 2)) && (ddic_x > 30) && (ddic_x < 156))
				dsi_display->panel->ddic_check_info = 1;
		} else
			dsi_display->panel->ddic_check_info = 0;
		break;

	case 7:
		if((strcmp(dsi_display->panel->name, "samsung ana6705 fhd cmd mode dsc dsi panel") == 0) &&
		(ddic_x > 57) && (ddic_x < 245)) {
			dsi_display->panel->ddic_check_info = 1;
		} else if(strcmp(dsi_display->panel->name, "samsung ana6706 dsc cmd mode panel") == 0) {
			if(((panel_tool == 1) || (panel_tool == 2)) && (ddic_x > 45) && (ddic_x < 141))
				dsi_display->panel->ddic_check_info = 1;
		} else
			dsi_display->panel->ddic_check_info = 0;
		break;

	case 8:
		if((strcmp(dsi_display->panel->name, "samsung ana6705 fhd cmd mode dsc dsi panel") == 0) &&
		(ddic_x > 119) && (ddic_x < 183)) {
			dsi_display->panel->ddic_check_info = 1;
		} else
			dsi_display->panel->ddic_check_info = 0;
		break;

	default:
		dsi_display->panel->ddic_check_info = 0;
		break;
	}
	DSI_DEBUG("%s end\n", __func__);

	return dsi_display->panel->ddic_check_info;
}

int dsi_display_read_serial_number(struct dsi_display *dsi_display,
		struct dsi_panel *panel, char *buf, int len)
{
	int rc = 0;
	int count = 0;
	unsigned char panel_ic_v = 0;
	unsigned char register_d6[10] = {0};
	int ddic_x = 0;
	int ddic_y = 0;
	unsigned char code_info = 0;
	unsigned char stage_info = 0;
	unsigned char prodution_info = 0;

	struct dsi_display_mode *mode;

	if (!panel || !panel->cur_mode)
		return -EINVAL;

	rc = dsi_display_cmd_engine_enable(dsi_display);
	if (rc) {
		DSI_ERR("cmd engine enable failed\n");
		return -EINVAL;
	}

	dsi_panel_acquire_panel_lock(panel);
	mode = panel->cur_mode;

	count = mode->priv_info->cmd_sets[DSI_CMD_SET_LEVEL2_KEY_ENABLE].count;
	if (!count) {
		DSI_ERR("This panel does not support level2 key enable command\n");
	} else {
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_LEVEL2_KEY_ENABLE);
		if (rc) {
			DSI_ERR("Failed to send DSI_CMD_SET_LEVEL2_KEY_ENABLE commands\n");
			goto error;
		}
	}

	if(strcmp(panel->name, "samsung ana6706 dsc cmd mode panel") == 0) {
		dsi_display_read_panel_reg(dsi_display, 0xFA, &panel_ic_v, 1);
		panel->panel_ic_v = panel_ic_v & 0x0f;
	}

	if ((strcmp(panel->name, "samsung ana6705 fhd cmd mode dsc dsi panel") == 0)
		|| (strcmp(panel->name, "samsung ana6706 dsc cmd mode panel") == 0)) {
		dsi_display_read_panel_reg(dsi_display, 0xD6, register_d6, 10);

		memcpy(panel->buf_select, register_d6, 10);
		panel->panel_tool = dsi_display_back_ToolsType_ANA67061(register_d6);
		DSI_ERR("reg_d6: %02x %02x %02x %02x %02x %02x %02x\n", register_d6[0], register_d6[1], register_d6[2], register_d6[3], register_d6[4], register_d6[5], register_d6[6]);

		ddic_x = (((register_d6[3] & 0x1f) << 4) | ((register_d6[4] & 0xf0) >> 4));
		ddic_y = (register_d6[4] & 0x0f);
		panel->ddic_x = ddic_x;
		panel->ddic_y = ddic_y;
		DSI_ERR("ddic_x = %d, ddic_y = %d\n", panel->ddic_x, panel->ddic_y);
		len = 14;
	}

	dsi_display_read_panel_reg(dsi_display, 0xA1, buf, len);

	dsi_display_read_panel_reg(dsi_display, 0xDA, &code_info, 1);
	panel->panel_code_info = code_info;
	DSI_ERR("Code info is 0x%X\n", panel->panel_code_info);

	dsi_display_read_panel_reg(dsi_display, 0xDB, &stage_info, 1);
	panel->panel_stage_info = stage_info;
	DSI_ERR("Stage info is 0x%X\n", panel->panel_stage_info);

	dsi_display_read_panel_reg(dsi_display, 0xDC, &prodution_info, 1);
	panel->panel_production_info = prodution_info;
	DSI_ERR("Production info is 0x%X\n", panel->panel_production_info);

	count = mode->priv_info->cmd_sets[DSI_CMD_SET_LEVEL2_KEY_DISABLE].count;
	if (!count) {
		DSI_ERR("This panel does not support level2 key disable command\n");
	} else {
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_LEVEL2_KEY_DISABLE);
		if (rc) {
			DSI_ERR("Failed to send DSI_CMD_SET_LEVEL2_KEY_DISABLE commands\n");
			goto error;
		}
	}

error:
	dsi_panel_release_panel_lock(panel);
	dsi_display_cmd_engine_disable(dsi_display);
	return rc;
}

int dsi_display_get_serial_number(struct drm_connector *connector)
{
	struct dsi_panel *panel = NULL;
	struct dsi_display *dsi_display = NULL;
	struct dsi_bridge *c_bridge;
	int count = 0;
	struct dsi_display_mode *mode;
	char buf[32];
	int panel_year = 0;
	int panel_mon = 0;
	int panel_day = 0;
	int panel_hour = 0;
	int panel_min = 0;
	int panel_sec = 0;
	int panel_msec = 0;
	int panel_msec_int = 0;
	int panel_msec_rem = 0;
	int len = 0;
	int rc = 0;

	DSI_DEBUG("%s start\n", __func__);
	if ((connector == NULL) || (connector->encoder == NULL)
		|| (connector->encoder->bridge == NULL))
		return 0;

	c_bridge =  to_dsi_bridge(connector->encoder->bridge);
	dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return -EINVAL;

	panel = dsi_display->panel;
	mutex_lock(&dsi_display->display_lock);

	if (!dsi_panel_initialized(panel) || !panel->cur_mode)
		goto error;

	len = panel->panel_msec_low_index;
	if (len > sizeof(buf)) {
		DSI_ERR("len is large than buf size!!!\n");
		goto error;
	}

	mode = panel->cur_mode;
	count = mode->priv_info->cmd_sets[DSI_CMD_SET_REGISTER_READ].count;
	if (count) {
		if ((panel->panel_year_index > len) || (panel->panel_mon_index > len)
			|| (panel->panel_day_index > len) || (panel->panel_hour_index > len)
				|| (panel->panel_min_index > len) || (panel->panel_sec_index > len)
						|| (panel->panel_msec_high_index > len)) {
			DSI_ERR("Panel serial number index not corrected.\n");
			goto error;
		}

		rc = dsi_display_clk_ctrl(dsi_display->dsi_clk_handle,
			DSI_ALL_CLKS, DSI_CLK_ON);
		if (rc) {
			DSI_ERR("[%s] failed to enable DSI clocks, rc=%d\n",
				dsi_display->name, rc);
			goto error;
		}

		memset(buf, 0, sizeof(buf));
		dsi_display_read_serial_number(dsi_display, panel, buf, len);
		memcpy(panel->buf_id, buf, 32);
		panel_year = 2011 + ((buf[panel->panel_year_index - 1] >> 4) & 0x0f);
		if (panel_year == 2011)
			panel_year = 0;
		panel_mon = buf[panel->panel_mon_index - 1] & 0x0f;
		if ((panel_mon > 12) || (panel_mon < 1)) {
			DSI_ERR("Panel Mon not corrected.\n");
			panel_mon = 0;
		}
		panel_day = buf[panel->panel_day_index - 1] & 0x3f;
		if ((panel_day > 31) || (panel_day < 1)) {
			DSI_ERR("Panel Day not corrected.\n");
			panel_day = 0;
		}
		panel_hour = buf[panel->panel_hour_index - 1] & 0x3f;
		if ((panel_hour > 23) || (panel_hour < 0)) {
			DSI_ERR("Panel Hour not corrected.\n");
			panel_hour = 0;
		}
		panel_min = buf[panel->panel_min_index - 1] & 0x3f;
		if ((panel_min > 59) || (panel_min < 0)) {
			DSI_ERR("Panel Min not corrected.\n");
			panel_min = 0;
		}
		panel_sec = buf[panel->panel_sec_index - 1] & 0x3f;
		if ((panel_sec > 59) || (panel_sec < 0)) {
			DSI_ERR("Panel sec not corrected.\n");
			panel_sec = 0;
		}
		panel_msec = ((buf[panel->panel_msec_high_index - 1]<<8) | buf[panel->panel_msec_low_index - 1]);
		if ((panel_msec > 9999) || (panel_msec < 0)) {
			DSI_ERR("Panel msec not corrected.\n");
			panel_sec = 0;
		}
		panel_msec_int = panel_msec/10;
		panel_msec_rem = panel_msec%10;
		DSI_ERR("panel_msec is %d , panel_msec_int is %d , panel_msec_rem is %d.\n", panel_msec, panel_msec_int, panel_msec_rem);

		panel->panel_year = panel_year;
		panel->panel_mon = panel_mon;
		panel->panel_day = panel_day;
		panel->panel_hour = panel_hour;
		panel->panel_min = panel_min;
		panel->panel_sec = panel_sec;
		panel->panel_msec = panel_msec;
		panel->panel_msec_int = panel_msec_int;
		panel->panel_msec_rem = panel_msec_rem;

		panel->panel_serial_number = (u64)panel_year * 10000000000 + (u64)panel_mon * 100000000 + (u64)panel_day * 1000000
												 + (u64)panel_hour * 10000 + (u64)panel_min * 100 + (u64)panel_sec;

		rc = dsi_display_clk_ctrl(dsi_display->dsi_clk_handle,
			DSI_ALL_CLKS, DSI_CLK_OFF);
		if (rc) {
			DSI_ERR("[%s] failed to enable DSI clocks, rc=%d\n",
			dsi_display->name, rc);
			goto error;
		}
	} else {
		DSI_ERR("This panel not support serial number.\n");
	}

error:
	mutex_unlock(&dsi_display->display_lock);
	DSI_DEBUG("%s end\n", __func__);
	return 0;
}

int dsi_display_get_serial_number_at(struct drm_connector *connector)
{
	struct dsi_display *dsi_display = NULL;
	struct dsi_bridge *c_bridge;

	DSI_DEBUG("start\n");

	if ((connector == NULL) || (connector->encoder == NULL)
		|| (connector->encoder->bridge == NULL))
		return -EINVAL;

	c_bridge =  to_dsi_bridge(connector->encoder->bridge);
	dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return -EINVAL;

	DSI_DEBUG("end\n");

	return dsi_display->panel->panel_serial_number;
}

int dsi_display_get_serial_number_year(struct drm_connector *connector)
{
	struct dsi_display *dsi_display = NULL;
	struct dsi_bridge *c_bridge;

	DSI_DEBUG("%s start\n", __func__);
	if ((connector == NULL) || (connector->encoder == NULL)
		|| (connector->encoder->bridge == NULL))
		return 0;

	c_bridge =  to_dsi_bridge(connector->encoder->bridge);
	dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return 0;

	DSI_DEBUG("%s end\n", __func__);

	return dsi_display->panel->panel_year;
}

int dsi_display_get_serial_number_mon(struct drm_connector *connector)
{
	struct dsi_display *dsi_display = NULL;
	struct dsi_bridge *c_bridge;
	DSI_DEBUG("%s start\n", __func__);

	if ((connector == NULL) || (connector->encoder == NULL)
		|| (connector->encoder->bridge == NULL))
		return 0;

	c_bridge =  to_dsi_bridge(connector->encoder->bridge);
	dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return 0;

	DSI_DEBUG("%s end\n", __func__);

	return dsi_display->panel->panel_mon;
}

int dsi_display_get_serial_number_day(struct drm_connector *connector)
{
	struct dsi_display *dsi_display = NULL;
	struct dsi_bridge *c_bridge;

	DSI_DEBUG("%s start\n", __func__);

	if ((connector == NULL) || (connector->encoder == NULL)
		|| (connector->encoder->bridge == NULL))
		return 0;

	c_bridge =  to_dsi_bridge(connector->encoder->bridge);
	dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return 0;

	DSI_DEBUG("%s end\n", __func__);

	return dsi_display->panel->panel_day;
}

int dsi_display_get_serial_number_hour(struct drm_connector *connector)
{
	struct dsi_display *dsi_display = NULL;
	struct dsi_bridge *c_bridge;

	DSI_DEBUG("%s start\n", __func__);

	if ((connector == NULL) || (connector->encoder == NULL)
		|| (connector->encoder->bridge == NULL))
		return 0;

	c_bridge =  to_dsi_bridge(connector->encoder->bridge);
	dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return 0;

	DSI_DEBUG("%s end\n", __func__);

	return dsi_display->panel->panel_hour;
}

int dsi_display_get_serial_number_min(struct drm_connector *connector)
{
	struct dsi_display *dsi_display = NULL;
	struct dsi_bridge *c_bridge;

	DSI_DEBUG("%s start\n", __func__);

	if ((connector == NULL) || (connector->encoder == NULL)
		|| (connector->encoder->bridge == NULL))
		return 0;

	c_bridge =  to_dsi_bridge(connector->encoder->bridge);
	dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return 0;

	DSI_DEBUG("%s end\n", __func__);

	return dsi_display->panel->panel_min;
}

int dsi_display_get_serial_number_sec(struct drm_connector *connector)
{
	struct dsi_display *dsi_display = NULL;
	struct dsi_bridge *c_bridge;

	DSI_DEBUG("%s start\n", __func__);

	if ((connector == NULL) || (connector->encoder == NULL)
		|| (connector->encoder->bridge == NULL))
		return 0;

	c_bridge =  to_dsi_bridge(connector->encoder->bridge);
	dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return 0;

	DSI_DEBUG("%s end\n", __func__);

	return dsi_display->panel->panel_sec;
}

int dsi_display_get_serial_number_msec_int(struct drm_connector *connector)
{
	struct dsi_display *dsi_display = NULL;
	struct dsi_bridge *c_bridge;

	DSI_DEBUG("%s start\n", __func__);

	if ((connector == NULL) || (connector->encoder == NULL)
		|| (connector->encoder->bridge == NULL))
		return 0;

	c_bridge =  to_dsi_bridge(connector->encoder->bridge);
	dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return 0;

	DSI_DEBUG("%s end\n", __func__);

	return dsi_display->panel->panel_msec_int;
}

int dsi_display_get_serial_number_msec_rem(struct drm_connector *connector)
{
	struct dsi_display *dsi_display = NULL;
	struct dsi_bridge *c_bridge;

	DSI_DEBUG("%s start\n", __func__);

	if ((connector == NULL) || (connector->encoder == NULL)
		|| (connector->encoder->bridge == NULL))
		return 0;

	c_bridge =  to_dsi_bridge(connector->encoder->bridge);
	dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return 0;

	DSI_DEBUG("%s end\n", __func__);

	return dsi_display->panel->panel_msec_rem;
}

int dsi_display_get_code_info(struct drm_connector *connector)
{
	struct dsi_display *dsi_display = NULL;
	struct dsi_bridge *c_bridge;

	DSI_DEBUG("%s start\n", __func__);

	if ((connector == NULL) || (connector->encoder == NULL)
		|| (connector->encoder->bridge == NULL))
		return 0;

	c_bridge =  to_dsi_bridge(connector->encoder->bridge);
	dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return 0;

	DSI_DEBUG("%s end\n", __func__);

	return dsi_display->panel->panel_code_info;
}

int dsi_display_get_stage_info(struct drm_connector *connector)
{
	struct dsi_display *dsi_display = NULL;
	struct dsi_bridge *c_bridge;

	DSI_DEBUG("%s start\n", __func__);

	if ((connector == NULL) || (connector->encoder == NULL)
		|| (connector->encoder->bridge == NULL))
		return 0;

	c_bridge =  to_dsi_bridge(connector->encoder->bridge);
	dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return 0;

	DSI_DEBUG("%s end\n", __func__);

	return dsi_display->panel->panel_stage_info;
}

int dsi_display_get_production_info(struct drm_connector *connector)
{
	struct dsi_display *dsi_display = NULL;
	struct dsi_bridge *c_bridge;

	DSI_DEBUG("%s start\n", __func__);

	if ((connector == NULL) || (connector->encoder == NULL)
		|| (connector->encoder->bridge == NULL))
		return 0;

	c_bridge =  to_dsi_bridge(connector->encoder->bridge);
	dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return 0;

	DSI_DEBUG("%s end\n", __func__);

	return dsi_display->panel->panel_production_info;
}

int dsi_display_get_panel_ic_v_info(struct drm_connector *connector)
{
	struct dsi_display *dsi_display = NULL;
	struct dsi_bridge *c_bridge;

	DSI_DEBUG("%s start\n", __func__);

	if ((connector == NULL) || (connector->encoder == NULL)
		|| (connector->encoder->bridge == NULL))
		return 0;

	c_bridge =  to_dsi_bridge(connector->encoder->bridge);
	dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return 0;

	DSI_DEBUG("%s end\n", __func__);

	return dsi_display->panel->panel_ic_v;
}

int dsi_display_set_native_loading_effect_mode(struct drm_connector *connector, int level)
{
	struct dsi_display *dsi_display = NULL;
	struct dsi_panel *panel = NULL;
	struct dsi_bridge *c_bridge;
	int rc = 0;

	if ((connector == NULL) || (connector->encoder == NULL)
		|| (connector->encoder->bridge == NULL))
		return -EINVAL;

	c_bridge =  to_dsi_bridge(connector->encoder->bridge);
	dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return -EINVAL;

	panel = dsi_display->panel;

	mutex_lock(&dsi_display->display_lock);

	panel->naive_display_loading_effect_mode = level;

	if (!dsi_panel_initialized(panel)) {
		goto error;
	}

	rc = dsi_display_clk_ctrl(dsi_display->dsi_clk_handle,
			DSI_CORE_CLK, DSI_CLK_ON);
	if (rc) {
		DSI_ERR("[%s] failed to enable DSI core clocks, rc=%d\n",
			dsi_display->name, rc);
		goto error;
	}

	rc = dsi_panel_set_native_loading_effect_mode(panel, level);
	if (rc)
		DSI_ERR("unable to set loading effect mode\n");

	rc = dsi_display_clk_ctrl(dsi_display->dsi_clk_handle,
			DSI_CORE_CLK, DSI_CLK_OFF);
	if (rc) {
		DSI_ERR("[%s] failed to disable DSI core clocks, rc=%d\n",
			dsi_display->name, rc);
		goto error;
	}

error:
	mutex_unlock(&dsi_display->display_lock);
	return rc;
}

int dsi_display_get_native_display_loading_effect_mode(struct drm_connector *connector)
{
	struct dsi_display *dsi_display = NULL;
	struct dsi_bridge *c_bridge;

	if ((connector == NULL) || (connector->encoder == NULL)
		|| (connector->encoder->bridge == NULL))
		return -EINVAL;

	c_bridge =  to_dsi_bridge(connector->encoder->bridge);
	dsi_display = c_bridge->display;

	if ((dsi_display == NULL) || (dsi_display->panel == NULL))
		return -EINVAL;

	return dsi_display->panel->naive_display_loading_effect_mode;
}

/**********************************************************
* Function:
* 	dsi_display_read_panel_reg()
* Description:
* 	read panel registers value
* Parameters:
* 	dsi_display [IN] dsi display struct
* 	registers   [IN] panel registers cmd
*	buf			[IN] read value from panel registers
*	count		[IN] value count
* Returns:
* 	0 on success or a negative error code on failure.
*********************************************************/
int dsi_display_read_panel_reg(struct dsi_display *dsi_display, unsigned char registers, char *buf, size_t count)
{
	size_t err;
	struct mipi_dsi_device *dsi;

	if (!dsi_display || !dsi_display->panel || !dsi_display->panel->cur_mode || !registers || !buf || !count) {
		DSI_ERR("Invalid params\n");
		return -EINVAL;
	}

	dsi = &dsi_display->panel->mipi_device;

	err = oplus_mipi_dcs_read_buffer(dsi, registers, buf, count);
	if(err < 0)
		return err;

	return 0;
}

/**********************************************************
* Function:
* 	dsi_display_write_panel_reg()
* Description:
* 	write panel registers value
* Parameters:
* 	dsi_display [IN] dsi display struct
* 	registers   [IN] panel registers cmd
*	buf			[IN] write value to panel registers
*	count		[IN] value count
* Returns:
* 	0 on success or a negative error code on failure.
*********************************************************/
int dsi_display_write_panel_reg(struct dsi_display *dsi_display, unsigned char registers, char *buf, size_t count)
{
	size_t err;
	size_t size;
	struct mipi_dsi_device *dsi;
	u8 *tx_buf;

	if (!dsi_display || !dsi_display->panel || !dsi_display->panel->cur_mode || !registers || !buf || !count) {
		DSI_ERR("Invalid params\n");
		return -EINVAL;
	}
	if(count > 0) {
		size = 1 + count ;
		tx_buf =  kmalloc(size, GFP_KERNEL);
		if(!tx_buf){
			DSI_ERR("tx_buf kmalloc fail\n");
			return -1;
		}
		tx_buf[0] = (u8)registers;
		memcpy(&tx_buf[1], buf, count);
	} else {
		tx_buf = &registers;
		size = 1;
	}

	dsi = &dsi_display->panel->mipi_device;

	err = oplus_mipi_dcs_write_buffer(dsi, tx_buf, 3);
	if(count > 0)
		kfree(tx_buf);

	return err;
}
