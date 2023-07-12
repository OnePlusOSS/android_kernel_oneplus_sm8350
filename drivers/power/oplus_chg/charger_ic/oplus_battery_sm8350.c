/**********************************************************************
** File: msm_amss\adsp_proc\core\battman\battmngr\externalfg\src\oplus_bq27541.c
** VENDOR_EDIT
** Copyright (C), 2020-2022, OPLUS Mobile Comm Corp., Ltd
**
** Description:
**          for ADSP charging
**
** Version: 1.0
** Date created: 2020-05-20
**
** --------------------------- Revision History: -----------------------------------
* <version>           <date>              <author>                           <desc>
***********************************************************************/

#define pr_fmt(fmt)	"BATTERY_CHG: %s: " fmt, __func__

#ifdef VENDOR_EDIT
#include <linux/delay.h>
#include "oplus_battery_sm8350.h"
#include "../oplus_charger.h"
#include "../oplus_gauge.h"
#include "../oplus_warp.h"
#include "../oplus_short.h"
#include "../oplus_wireless.h"
#include "../charger_ic/oplus_short_ic.h"
#include "../oplus_adapter.h"
//#include "../gauge_ic/oplus_bq27541.h"
//#include "oplus_da9313.h"	//for WPC
//#include "op_charge.h"
#include "../wireless_ic/oplus_nu1619.h"

struct oplus_chg_chip *g_oplus_chip = NULL;
static int qpnp_get_prop_charger_voltage_now(void);
#endif /*VENDOR_EDIT*/

static const int battery_prop_map[BATT_PROP_MAX] = {
	[BATT_STATUS]		= POWER_SUPPLY_PROP_STATUS,
	[BATT_HEALTH]		= POWER_SUPPLY_PROP_HEALTH,
	[BATT_PRESENT]		= POWER_SUPPLY_PROP_PRESENT,
	[BATT_CHG_TYPE]		= POWER_SUPPLY_PROP_CHARGE_TYPE,
	[BATT_CAPACITY]		= POWER_SUPPLY_PROP_CAPACITY,
	[BATT_VOLT_OCV]		= POWER_SUPPLY_PROP_VOLTAGE_OCV,
	[BATT_VOLT_NOW]		= POWER_SUPPLY_PROP_VOLTAGE_NOW,
	[BATT_VOLT_MAX]		= POWER_SUPPLY_PROP_VOLTAGE_MAX,
	[BATT_CURR_NOW]		= POWER_SUPPLY_PROP_CURRENT_NOW,
	[BATT_CHG_CTRL_LIM]	= POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT,
	[BATT_CHG_CTRL_LIM_MAX]	= POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT_MAX,
	[BATT_TEMP]		= POWER_SUPPLY_PROP_TEMP,
	[BATT_TECHNOLOGY]	= POWER_SUPPLY_PROP_TECHNOLOGY,
	[BATT_CHG_COUNTER]	= POWER_SUPPLY_PROP_CHARGE_COUNTER,
	[BATT_CYCLE_COUNT]	= POWER_SUPPLY_PROP_CYCLE_COUNT,
	[BATT_CHG_FULL_DESIGN]	= POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	[BATT_CHG_FULL]		= POWER_SUPPLY_PROP_CHARGE_FULL,
	[BATT_MODEL_NAME]	= POWER_SUPPLY_PROP_MODEL_NAME,
	[BATT_TTF_AVG]		= POWER_SUPPLY_PROP_TIME_TO_FULL_AVG,
	[BATT_TTE_AVG]		= POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG,
	[BATT_POWER_NOW]	= POWER_SUPPLY_PROP_POWER_NOW,
	[BATT_POWER_AVG]	= POWER_SUPPLY_PROP_POWER_AVG,
};

static const int usb_prop_map[USB_PROP_MAX] = {
	[USB_ONLINE]		= POWER_SUPPLY_PROP_ONLINE,
	[USB_VOLT_NOW]		= POWER_SUPPLY_PROP_VOLTAGE_NOW,
	[USB_VOLT_MAX]		= POWER_SUPPLY_PROP_VOLTAGE_MAX,
	[USB_CURR_NOW]		= POWER_SUPPLY_PROP_CURRENT_NOW,
	[USB_CURR_MAX]		= POWER_SUPPLY_PROP_CURRENT_MAX,
	[USB_INPUT_CURR_LIMIT]	= POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	[USB_ADAP_TYPE]		= POWER_SUPPLY_PROP_USB_TYPE,
};

static const int wls_prop_map[WLS_PROP_MAX] = {
	[WLS_ONLINE]		= POWER_SUPPLY_PROP_ONLINE,
	[WLS_VOLT_NOW]		= POWER_SUPPLY_PROP_VOLTAGE_NOW,
	[WLS_VOLT_MAX]		= POWER_SUPPLY_PROP_VOLTAGE_MAX,
	[WLS_CURR_NOW]		= POWER_SUPPLY_PROP_CURRENT_NOW,
	[WLS_CURR_MAX]		= POWER_SUPPLY_PROP_CURRENT_MAX,
};

static int battery_chg_fw_write(struct battery_chg_dev *bcdev, void *data,
				int len)
{
	int rc;

	if (atomic_read(&bcdev->state) == PMIC_GLINK_STATE_DOWN) {
		pr_debug("glink state is down\n");
		return -ENOTCONN;
	}

	reinit_completion(&bcdev->fw_buf_ack);
	rc = pmic_glink_write(bcdev->client, data, len);
	if (!rc) {
		rc = wait_for_completion_timeout(&bcdev->fw_buf_ack,
					msecs_to_jiffies(WLS_FW_WAIT_TIME_MS));
		if (!rc) {
			pr_err("Error, timed out sending message\n");
			return -ETIMEDOUT;
		}

		rc = 0;
	}

	return rc;
}

static int battery_chg_write(struct battery_chg_dev *bcdev, void *data,
				int len)
{
	int rc;

	/*
	 * When the subsystem goes down, it's better to return the last
	 * known values until it comes back up. Hence, return 0 so that
	 * pmic_glink_write() is not attempted until pmic glink is up.
	 */
	if (atomic_read(&bcdev->state) == PMIC_GLINK_STATE_DOWN) {
		pr_debug("glink state is down\n");
		return 0;
	}

	if (bcdev->debug_battery_detected && bcdev->block_tx)
		return 0;

	mutex_lock(&bcdev->rw_lock);
	reinit_completion(&bcdev->ack);
	rc = pmic_glink_write(bcdev->client, data, len);
	if (!rc) {
		rc = wait_for_completion_timeout(&bcdev->ack,
					msecs_to_jiffies(BC_WAIT_TIME_MS));
		if (!rc) {
			pr_err("Error, timed out sending message\n");
			mutex_unlock(&bcdev->rw_lock);
			return -ETIMEDOUT;
		}

		rc = 0;
	}
	mutex_unlock(&bcdev->rw_lock);

	return rc;
}

static int write_property_id(struct battery_chg_dev *bcdev,
			struct psy_state *pst, u32 prop_id, u32 val)
{
	struct battery_charger_req_msg req_msg = { { 0 } };

	req_msg.property_id = prop_id;
	req_msg.battery_id = 0;
	req_msg.value = val;
	req_msg.hdr.owner = MSG_OWNER_BC;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = pst->opcode_set;

	pr_debug("psy: %s prop_id: %u val: %u\n", pst->psy->desc->name,
		req_msg.property_id, val);

	return battery_chg_write(bcdev, &req_msg, sizeof(req_msg));
}

static int read_property_id(struct battery_chg_dev *bcdev,
			struct psy_state *pst, u32 prop_id)
{
	struct battery_charger_req_msg req_msg = { { 0 } };

	req_msg.property_id = prop_id;
	req_msg.battery_id = 0;
	req_msg.value = 0;
	req_msg.hdr.owner = MSG_OWNER_BC;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = pst->opcode_get;

	pr_debug("psy: %s prop_id: %u\n", pst->psy->desc->name,
		req_msg.property_id);

	return battery_chg_write(bcdev, &req_msg, sizeof(req_msg));
}

static int get_property_id(struct psy_state *pst,
			enum power_supply_property prop)
{
	u32 i;

	for (i = 0; i < pst->prop_count; i++)
		if (pst->map[i] == prop)
			return i;

	pr_err("No property id for property %d in psy %s\n", prop,
		pst->psy->desc->name);

	return -ENOENT;
}

static void battery_chg_notify_enable(struct battery_chg_dev *bcdev)
{
	struct battery_charger_set_notify_msg req_msg = { { 0 } };
	int rc;

	/* Send request to enable notification */
	req_msg.hdr.owner = MSG_OWNER_BC;
	req_msg.hdr.type = MSG_TYPE_NOTIFY;
	req_msg.hdr.opcode = BC_SET_NOTIFY_REQ;

	rc = battery_chg_write(bcdev, &req_msg, sizeof(req_msg));
	if (rc < 0)
		pr_err("Failed to enable notification rc=%d\n", rc);
}

static void battery_chg_subsys_up_work(struct work_struct *work)
{
	struct battery_chg_dev *bcdev = container_of(work,
					struct battery_chg_dev, subsys_up_work);

	battery_chg_notify_enable(bcdev);
}

static void battery_chg_state_cb(void *priv, enum pmic_glink_state state)
{
	struct battery_chg_dev *bcdev = priv;

	pr_debug("state: %d\n", state);

	atomic_set(&bcdev->state, state);
	if (state == PMIC_GLINK_STATE_UP)
		schedule_work(&bcdev->subsys_up_work);
}

/**
 * qti_battery_charger_get_prop() - Gets the property being requested
 *
 * @name: Power supply name
 * @prop_id: Property id to be read
 * @val: Pointer to value that needs to be updated
 *
 * Return: 0 if success, negative on error.
 */
int qti_battery_charger_get_prop(const char *name,
				enum battery_charger_prop prop_id, int *val)
{
	struct power_supply *psy;
	struct battery_chg_dev *bcdev;
	struct psy_state *pst;
	int rc = 0;

	if (prop_id >= BATTERY_CHARGER_PROP_MAX)
		return -EINVAL;

	if (strcmp(name, "battery") && strcmp(name, "usb") &&
	    strcmp(name, "wireless"))
		return -EINVAL;

	psy = power_supply_get_by_name(name);
	if (!psy)
		return -ENODEV;

	bcdev = power_supply_get_drvdata(psy);
	if (!bcdev)
		return -ENODEV;

	power_supply_put(psy);

	switch (prop_id) {
	case BATTERY_RESISTANCE:
		pst = &bcdev->psy_list[PSY_TYPE_BATTERY];
		rc = read_property_id(bcdev, pst, BATT_RESISTANCE);
		if (!rc)
			*val = pst->prop[BATT_RESISTANCE];
		break;
	default:
		break;
	}

	return rc;
}
EXPORT_SYMBOL(qti_battery_charger_get_prop);

static bool validate_message(struct battery_charger_resp_msg *resp_msg,
				size_t len)
{
	if (len != sizeof(*resp_msg)) {
		pr_err("Incorrect response length %zu for opcode %#x\n", len,
			resp_msg->hdr.opcode);
		return false;
	}

	if (resp_msg->ret_code) {
		pr_err("Error in response for opcode %#x prop_id %u, rc=%d\n",
			resp_msg->hdr.opcode, resp_msg->property_id,
			(int)resp_msg->ret_code);
		return false;
	}

	return true;
}

#define MODEL_DEBUG_BOARD	"Debug_Board"
static void handle_message(struct battery_chg_dev *bcdev, void *data,
				size_t len)
{
	struct battery_charger_resp_msg *resp_msg = data;
	struct battery_model_resp_msg *model_resp_msg = data;
	struct wireless_fw_check_resp *fw_check_msg;
	struct wireless_fw_push_buf_resp *fw_resp_msg;
	struct wireless_fw_update_status *fw_update_msg;
	struct wireless_fw_get_version_resp *fw_ver_msg;
	struct psy_state *pst;
	bool ack_set = false;

	switch (resp_msg->hdr.opcode) {
	case BC_BATTERY_STATUS_GET:
		pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

		/* Handle model response uniquely as it's a string */
		if (pst->model && len == sizeof(*model_resp_msg)) {
			memcpy(pst->model, model_resp_msg->model, MAX_STR_LEN);
			ack_set = true;
			bcdev->debug_battery_detected = !strcmp(pst->model,
					MODEL_DEBUG_BOARD);
			break;
		}

		/* Other response should be of same type as they've u32 value */
		if (validate_message(resp_msg, len) &&
		    resp_msg->property_id < pst->prop_count) {
			pst->prop[resp_msg->property_id] = resp_msg->value;
			ack_set = true;
		}

		break;
	case BC_USB_STATUS_GET:
		pst = &bcdev->psy_list[PSY_TYPE_USB];
		if (validate_message(resp_msg, len) &&
		    resp_msg->property_id < pst->prop_count) {
			pst->prop[resp_msg->property_id] = resp_msg->value;
			ack_set = true;
		}

		break;
	case BC_WLS_STATUS_GET:
		pst = &bcdev->psy_list[PSY_TYPE_WLS];
		if (validate_message(resp_msg, len) &&
		    resp_msg->property_id < pst->prop_count) {
			pst->prop[resp_msg->property_id] = resp_msg->value;
			ack_set = true;
		}

		break;
	case BC_BATTERY_STATUS_SET:
	case BC_USB_STATUS_SET:
	case BC_WLS_STATUS_SET:
		if (validate_message(data, len))
			ack_set = true;

		break;
	case BC_SET_NOTIFY_REQ:
		/* Always ACK response for notify request */
		ack_set = true;
		break;
	case BC_WLS_FW_CHECK_UPDATE:
		if (len == sizeof(*fw_check_msg)) {
			fw_check_msg = data;
			if (fw_check_msg->ret_code == 1)
				bcdev->wls_fw_update_reqd = true;
			ack_set = true;
		} else {
			pr_err("Incorrect response length %zu for wls_fw_check_update\n",
				len);
		}
		break;
	case BC_WLS_FW_PUSH_BUF_RESP:
		if (len == sizeof(*fw_resp_msg)) {
			fw_resp_msg = data;
			if (fw_resp_msg->fw_update_status == 1)
				complete(&bcdev->fw_buf_ack);
		} else {
			pr_err("Incorrect response length %zu for wls_fw_push_buf_resp\n",
				len);
		}
		break;
	case BC_WLS_FW_UPDATE_STATUS_RESP:
		if (len == sizeof(*fw_update_msg)) {
			fw_update_msg = data;
			if (fw_update_msg->fw_update_done == 1)
				complete(&bcdev->fw_update_ack);
		} else {
			pr_err("Incorrect response length %zu for wls_fw_update_status_resp\n",
				len);
		}
		break;
	case BC_WLS_FW_GET_VERSION:
		if (len == sizeof(*fw_ver_msg)) {
			fw_ver_msg = data;
			bcdev->wls_fw_version = fw_ver_msg->fw_version;
			ack_set = true;
		} else {
			pr_err("Incorrect response length %zu for wls_fw_get_version\n",
				len);
		}
		break;
	default:
		pr_err("Unknown opcode: %u\n", resp_msg->hdr.opcode);
		break;
	}

	if (ack_set)
		complete(&bcdev->ack);
}

static void handle_notification(struct battery_chg_dev *bcdev, void *data,
				size_t len)
{
	struct battery_charger_notify_msg *notify_msg = data;
	struct psy_state *pst = NULL;

	if (len != sizeof(*notify_msg)) {
		pr_err("Incorrect response length %zu\n", len);
		return;
	}

	pr_debug("notification: %#x\n", notify_msg->notification);

	switch (notify_msg->notification) {
	case BC_BATTERY_STATUS_GET:
	case BC_GENERIC_NOTIFY:
		pst = &bcdev->psy_list[PSY_TYPE_BATTERY];
		break;
	case BC_USB_STATUS_GET:
		pst = &bcdev->psy_list[PSY_TYPE_USB];
		break;
	case BC_WLS_STATUS_GET:
		pst = &bcdev->psy_list[PSY_TYPE_WLS];
		break;
	default:
		break;
	}

	if (pst && pst->psy)
		power_supply_changed(pst->psy);
}

static int battery_chg_callback(void *priv, void *data, size_t len)
{
	struct pmic_glink_hdr *hdr = data;
	struct battery_chg_dev *bcdev = priv;

	pr_debug("owner: %u type: %u opcode: %#x len: %zu\n", hdr->owner,
		hdr->type, hdr->opcode, len);

	if (hdr->opcode == BC_NOTIFY_IND)
		handle_notification(bcdev, data, len);
	else
		handle_message(bcdev, data, len);

	return 0;
}

static int wls_psy_get_prop(struct power_supply *psy,
		enum power_supply_property prop,
		union power_supply_propval *pval)
{
	struct battery_chg_dev *bcdev = power_supply_get_drvdata(psy);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_WLS];
	int prop_id, rc;

	pval->intval = -ENODATA;

	prop_id = get_property_id(pst, prop);
	if (prop_id < 0)
		return prop_id;

	rc = read_property_id(bcdev, pst, prop_id);
	if (rc < 0)
		return rc;

	pval->intval = pst->prop[prop_id];

	return 0;
}

static int wls_psy_set_prop(struct power_supply *psy,
		enum power_supply_property prop,
		const union power_supply_propval *pval)
{
	return 0;
}

static int wls_psy_prop_is_writeable(struct power_supply *psy,
		enum power_supply_property prop)
{
	return 0;
}

static enum power_supply_property wls_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_MAX,
};

static const struct power_supply_desc wls_psy_desc = {
	.name			= "wireless",
	.type			= POWER_SUPPLY_TYPE_MAINS,
	.properties		= wls_props,
	.num_properties		= ARRAY_SIZE(wls_props),
	.get_property		= wls_psy_get_prop,
	.set_property		= wls_psy_set_prop,
	.property_is_writeable	= wls_psy_prop_is_writeable,
};

#ifdef VENDOR_EDIT
static int ac_psy_get_prop(struct power_supply *psy,
		enum power_supply_property prop,
		union power_supply_propval *pval)
{
	int rc = 0;

	rc = oplus_ac_get_property(psy, prop, pval);

	return rc;
}

static int ac_psy_set_prop(struct power_supply *psy,
		enum power_supply_property prop,
		const union power_supply_propval *pval)
{
	return 0;
}

static int ac_psy_prop_is_writeable(struct power_supply *psy,
		enum power_supply_property prop)
{
	return 0;
}

static enum power_supply_property ac_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
};

static const struct power_supply_desc ac_psy_desc = {
	.name			= "ac",
	.type			= POWER_SUPPLY_TYPE_MAINS,
	.properties		= ac_props,
	.num_properties		= ARRAY_SIZE(ac_props),
	.get_property		= ac_psy_get_prop,
	.set_property		= ac_psy_set_prop,
	.property_is_writeable	= ac_psy_prop_is_writeable,
};
#endif /*VENDOR_EDIT*/

#ifndef VENDOR_EDIT
static int usb_psy_set_icl(struct battery_chg_dev *bcdev, u32 prop_id, int val)
{
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_USB];
	u32 temp;
	int rc;

	rc = read_property_id(bcdev, pst, USB_ADAP_TYPE);
	if (rc < 0)
		return rc;

	/* Allow this only for SDP and not for other charger types */
	if (pst->prop[USB_ADAP_TYPE] != POWER_SUPPLY_USB_TYPE_SDP)
		return -EINVAL;

	/*
	 * Input current limit (ICL) can be set by different clients. E.g. USB
	 * driver can request for a current of 500/900 mA depending on the
	 * port type. Also, clients like EUD driver can pass 0 or -22 to
	 * suspend or unsuspend the input for its use case.
	 */

	temp = val;
	if (val < 0)
		temp = UINT_MAX;

	rc = write_property_id(bcdev, pst, prop_id, temp);
	if (!rc)
		pr_debug("Set ICL to %u\n", temp);

	return rc;
}
#endif /*VENDOR_EDIT*/

#ifdef VENDOR_EDIT
static void oplus_chg_set_curr_level_to_warpphy(void)
{
	int rc = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	rc = write_property_id(bcdev, pst, BATT_SET_COOL_DOWN, chip->cool_down);
	if (rc) {
		chg_err("set curr level fail, rc=%d\n", rc);
		return;
	}

	chg_debug("ap set curr level[%d] to warpphy\n", chip->cool_down);
}
#endif /*VENDOR_EDIT*/

#ifdef VENDOR_EDIT
static void oplus_warp_handle_warpphy_status(void)
{
	struct oplus_chg_chip *chip = g_oplus_chip;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;
	static int warp_status = 0;
	int rc = 0;
	int intval = 0;

	if (!chip) {
		printk(KERN_ERR "!!!chip null, warpphy non handle status: [%d]\n", intval);
		return;
	}

	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_USB];
	rc = read_property_id(bcdev, pst, USB_WARPPHY_STATUS);
	if (rc < 0) {
		printk(KERN_ERR "!!![warpphy] read warpphy_status fail\n");
		return;
	}
	intval = pst->prop[USB_WARPPHY_STATUS];

	if (warp_status ^ intval) {
		warp_status = intval;
		printk(KERN_ERR "!!![warpphy] warp status: [%d]\n", intval);
		if ((intval & 0xFF) == FAST_NOTIFY_PRESENT) {
			chip->warpphy.fastchg_start = true;
			chip->warpphy.fastchg_to_warm = false;
			chip->warpphy.fastchg_dummy_start = false;
			chip->warpphy.fastchg_to_normal = false;
			chip->warpphy.fastchg_ing = false;
			chip->warpphy.fast_chg_type = ((intval >> 8) & 0xFF);
			printk(KERN_ERR "!!![warpphy] fastchg start: [%d], adapter version: [0x%0x]\n",
				chip->warpphy.fastchg_start, chip->warpphy.fast_chg_type);
			oplus_chg_wake_update_work();
			oplus_chg_set_curr_level_to_warpphy();
		} else if ((intval & 0xFF) == FAST_NOTIFY_DUMMY_START) {
			chip->warpphy.fastchg_start = false;
			chip->warpphy.fastchg_to_warm = false;
			chip->warpphy.fastchg_dummy_start = true;
			chip->warpphy.fastchg_to_normal = false;
			chip->warpphy.fastchg_ing = false;
			chip->warpphy.fast_chg_type = ((intval >> 8) & 0xFF);
			printk(KERN_ERR "!!![warpphy] fastchg dummy start: [%d], adapter version: [0x%0x]\n",
					chip->warpphy.fastchg_dummy_start, chip->warpphy.fast_chg_type);
			oplus_chg_wake_update_work();
		} else if ((intval & 0xFF) == FAST_NOTIFY_ONGOING) {
			chip->warpphy.fastchg_ing = true;
			printk(KERN_ERR "!!!! warpphy fastchg ongoing\n");
		} else if ((intval & 0xFF) == FAST_NOTIFY_FULL) {
			chip->warpphy.fastchg_start = false;
			chip->warpphy.fastchg_to_warm = false;
			chip->warpphy.fastchg_dummy_start = false;
			chip->warpphy.fastchg_to_normal = true;
			chip->warpphy.fastchg_ing = false;
			printk(KERN_ERR "!!![warpphy] fastchg to normal: [%d]\n",
				chip->warpphy.fastchg_to_normal);
			oplus_chg_wake_update_work();
		} else if ((intval & 0xFF) == FAST_NOTIFY_BATT_TEMP_OVER) {
			chip->warpphy.fastchg_start = false;
			chip->warpphy.fastchg_to_warm = true;
			chip->warpphy.fastchg_dummy_start = false;
			chip->warpphy.fastchg_to_normal = false;
			chip->warpphy.fastchg_ing = false;
			printk(KERN_ERR "!!![warpphy] fastchg to warm: [%d]\n",
					chip->warpphy.fastchg_to_warm);
			oplus_chg_wake_update_work();
		} else {
			printk(KERN_ERR "!!![warpphy] non handle status: [%d]\n", intval);
		}
	}
}
#endif /*VENDOR_EDIT*/

static int usb_psy_get_prop(struct power_supply *psy,
		enum power_supply_property prop,
		union power_supply_propval *pval)
{
	struct battery_chg_dev *bcdev = power_supply_get_drvdata(psy);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_USB];
	int prop_id, rc;
#ifdef VENDOR_EDIT
	static int online = 0;
	static int adap_type = 0;
#endif

	pval->intval = -ENODATA;

	prop_id = get_property_id(pst, prop);
	if (prop_id < 0)
		return prop_id;

	rc = read_property_id(bcdev, pst, prop_id);
	if (rc < 0)
		return rc;

	pval->intval = pst->prop[prop_id];

#ifdef VENDOR_EDIT
	if (prop_id == USB_ONLINE) {
		if (online ^ pval->intval) {
			online = pval->intval;
			printk(KERN_ERR "!!!!! usb online: [%d]\n", online);
			oplus_chg_wake_update_work();
		}
	}
	if (prop_id == USB_ADAP_TYPE) {
		if (adap_type ^ pval->intval) {
			adap_type = pval->intval;
			printk(KERN_ERR "!!! usb adap type: [%d]\n", adap_type);
			oplus_chg_wake_update_work();
		}
	}
#endif
#ifdef VENDOR_EDIT
	if (prop_id == USB_ONLINE) {
		oplus_warp_handle_warpphy_status();
	}
#endif /*VENDOR_EDIT*/

	return 0;
}

static int usb_psy_set_prop(struct power_supply *psy,
		enum power_supply_property prop,
		const union power_supply_propval *pval)
{
	struct battery_chg_dev *bcdev = power_supply_get_drvdata(psy);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_USB];
	int prop_id, rc = 0;

	prop_id = get_property_id(pst, prop);
	if (prop_id < 0)
		return prop_id;

	switch (prop) {
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
#ifndef VENDOR_EDIT
		rc = usb_psy_set_icl(bcdev, prop_id, pval->intval);
#endif
		break;
	default:
		break;
	}

	return rc;
}

static int usb_psy_prop_is_writeable(struct power_supply *psy,
		enum power_supply_property prop)
{
	switch (prop) {
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		return 1;
	default:
		break;
	}

	return 0;
}

static enum power_supply_property usb_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	POWER_SUPPLY_PROP_USB_TYPE,
};

static enum power_supply_usb_type usb_psy_supported_types[] = {
	POWER_SUPPLY_USB_TYPE_UNKNOWN,
	POWER_SUPPLY_USB_TYPE_SDP,
	POWER_SUPPLY_USB_TYPE_DCP,
	POWER_SUPPLY_USB_TYPE_CDP,
	POWER_SUPPLY_USB_TYPE_ACA,
	POWER_SUPPLY_USB_TYPE_C,
	POWER_SUPPLY_USB_TYPE_PD,
	POWER_SUPPLY_USB_TYPE_PD_DRP,
	POWER_SUPPLY_USB_TYPE_PD_PPS,
	POWER_SUPPLY_USB_TYPE_APPLE_BRICK_ID,
};

static const struct power_supply_desc usb_psy_desc = {
	.name			= "usb",
	.type			= POWER_SUPPLY_TYPE_USB,
	.properties		= usb_props,
	.num_properties		= ARRAY_SIZE(usb_props),
	.get_property		= usb_psy_get_prop,
	.set_property		= usb_psy_set_prop,
	.usb_types		= usb_psy_supported_types,
	.num_usb_types		= ARRAY_SIZE(usb_psy_supported_types),
	.property_is_writeable	= usb_psy_prop_is_writeable,
};

static int battery_psy_set_charge_current(struct battery_chg_dev *bcdev,
					int val)
{
	int rc;
	u32 fcc_ua;

	if (!bcdev->num_thermal_levels)
		return 0;

	if (bcdev->num_thermal_levels < 0) {
		pr_err("Incorrect num_thermal_levels\n");
		return -EINVAL;
	}

	if (val < 0 || val > bcdev->num_thermal_levels)
		return -EINVAL;

	fcc_ua = bcdev->thermal_levels[val];

	rc = write_property_id(bcdev, &bcdev->psy_list[PSY_TYPE_BATTERY],
				BATT_CHG_CTRL_LIM, fcc_ua);
	if (!rc) {
		bcdev->curr_thermal_level = val;
		pr_debug("Set FCC to %u uA\n", fcc_ua);
	}

	return rc;
}

static int battery_psy_get_prop(struct power_supply *psy,
		enum power_supply_property prop,
		union power_supply_propval *pval)
{
	struct battery_chg_dev *bcdev = power_supply_get_drvdata(psy);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_BATTERY];
	int prop_id, rc;

#ifdef VENDOR_EDIT
	rc = oplus_battery_get_property(psy, prop, pval);
	if (rc == 0)
		return rc;
#endif /*VENDOR_EDIT*/

	pval->intval = -ENODATA;

	prop_id = get_property_id(pst, prop);
	if (prop_id < 0)
		return prop_id;

	rc = read_property_id(bcdev, pst, prop_id);
	if (rc < 0)
		return rc;

	switch (prop) {
	case POWER_SUPPLY_PROP_MODEL_NAME:
		pval->strval = pst->model;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		pval->intval = DIV_ROUND_CLOSEST(pst->prop[prop_id], 100);
		if (IS_ENABLED(CONFIG_QTI_PMIC_GLINK_CLIENT_DEBUG) &&
		   (bcdev->fake_soc >= 0 && bcdev->fake_soc <= 100))
			pval->intval = bcdev->fake_soc;
		break;
	case POWER_SUPPLY_PROP_TEMP:
		pval->intval = DIV_ROUND_CLOSEST((int)pst->prop[prop_id], 10);
		break;
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
		pval->intval = bcdev->curr_thermal_level;
		break;
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT_MAX:
		pval->intval = bcdev->num_thermal_levels;
		break;
	default:
		pval->intval = pst->prop[prop_id];
		break;
	}

	return rc;
}

static int battery_psy_set_prop(struct power_supply *psy,
		enum power_supply_property prop,
		const union power_supply_propval *pval)
{
	struct battery_chg_dev *bcdev = power_supply_get_drvdata(psy);

#ifndef VENDOR_EDIT
	switch (prop) {
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
		return battery_psy_set_charge_current(bcdev, pval->intval);
	default:
		return -EINVAL;
	}

	return 0;
#else /*VENDOR_EDIT*/
	int rc = 0;

	switch (prop) {
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
		return battery_psy_set_charge_current(bcdev, pval->intval);
	default:
		rc = oplus_battery_set_property(psy, prop, pval);
	}

	return rc;
#endif /*VENDOR_EDIT*/
}

static int battery_psy_prop_is_writeable(struct power_supply *psy,
		enum power_supply_property prop)
{
#ifndef VENDOR_EDIT
	switch (prop) {
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
		return 1;
	default:
		break;
	}

	return 0;
#else /*VENDOR_EDIT*/
	int rc = 0;

	switch (prop) {
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
		return 1;
	default:
		rc = oplus_battery_property_is_writeable(psy, prop);
		break;
	}

	return rc;
#endif /*VENDOR_EDIT*/
}

static enum power_supply_property battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_VOLTAGE_OCV,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT,
	POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT_MAX,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CHARGE_COUNTER,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_TIME_TO_FULL_AVG,
	POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG,
	POWER_SUPPLY_PROP_POWER_NOW,
	POWER_SUPPLY_PROP_POWER_AVG,
};

static const struct power_supply_desc batt_psy_desc = {
	.name			= "battery",
	.type			= POWER_SUPPLY_TYPE_BATTERY,
	.properties		= battery_props,
	.num_properties		= ARRAY_SIZE(battery_props),
	.get_property		= battery_psy_get_prop,
	.set_property		= battery_psy_set_prop,
	.property_is_writeable	= battery_psy_prop_is_writeable,
};

static int battery_chg_init_psy(struct battery_chg_dev *bcdev)
{
	struct power_supply_config psy_cfg = {};
	int rc;

	psy_cfg.drv_data = bcdev;
	psy_cfg.of_node = bcdev->dev->of_node;
	bcdev->psy_list[PSY_TYPE_BATTERY].psy =
		devm_power_supply_register(bcdev->dev, &batt_psy_desc,
						&psy_cfg);
	if (IS_ERR(bcdev->psy_list[PSY_TYPE_BATTERY].psy)) {
		rc = PTR_ERR(bcdev->psy_list[PSY_TYPE_BATTERY].psy);
		pr_err("Failed to register battery power supply, rc=%d\n", rc);
		return rc;
	}

	bcdev->psy_list[PSY_TYPE_USB].psy =
		devm_power_supply_register(bcdev->dev, &usb_psy_desc, &psy_cfg);
	if (IS_ERR(bcdev->psy_list[PSY_TYPE_USB].psy)) {
		rc = PTR_ERR(bcdev->psy_list[PSY_TYPE_USB].psy);
		pr_err("Failed to register USB power supply, rc=%d\n", rc);
		return rc;
	}

#ifndef VENDOR_EDIT
	bcdev->psy_list[PSY_TYPE_WLS].psy =
		devm_power_supply_register(bcdev->dev, &wls_psy_desc, &psy_cfg);
	if (IS_ERR(bcdev->psy_list[PSY_TYPE_WLS].psy)) {
		rc = PTR_ERR(bcdev->psy_list[PSY_TYPE_WLS].psy);
		pr_err("Failed to register wireless power supply, rc=%d\n", rc);
		return rc;
	}
#endif

#ifdef VENDOR_EDIT
	bcdev->psy_list[PSY_TYPE_AC].psy =
		devm_power_supply_register(bcdev->dev, &ac_psy_desc, &psy_cfg);
	if (IS_ERR(bcdev->psy_list[PSY_TYPE_AC].psy)) {
		rc = PTR_ERR(bcdev->psy_list[PSY_TYPE_AC].psy);
		pr_err("Failed to register ac power supply, rc=%d\n", rc);
		return rc;
	}
#endif

	return 0;
}

static int wireless_fw_send_firmware(struct battery_chg_dev *bcdev,
					const struct firmware *fw)
{
	struct wireless_fw_push_buf_req msg = {};
	const u8 *ptr;
	u32 i, num_chunks, partial_chunk_size;
	int rc;

	num_chunks = fw->size / WLS_FW_BUF_SIZE;
	partial_chunk_size = fw->size % WLS_FW_BUF_SIZE;

	if (!num_chunks)
		return -EINVAL;

	pr_debug("Updating FW...\n");

	ptr = fw->data;
	msg.hdr.owner = MSG_OWNER_BC;
	msg.hdr.type = MSG_TYPE_REQ_RESP;
	msg.hdr.opcode = BC_WLS_FW_PUSH_BUF_REQ;

	for (i = 0; i < num_chunks; i++, ptr += WLS_FW_BUF_SIZE) {
		msg.fw_chunk_id = i + 1;
		memcpy(msg.buf, ptr, WLS_FW_BUF_SIZE);

		pr_debug("sending FW chunk %u\n", i + 1);
		rc = battery_chg_fw_write(bcdev, &msg, sizeof(msg));
		if (rc < 0)
			return rc;
	}

	if (partial_chunk_size) {
		msg.fw_chunk_id = i + 1;
		memset(msg.buf, 0, WLS_FW_BUF_SIZE);
		memcpy(msg.buf, ptr, partial_chunk_size);

		pr_debug("sending partial FW chunk %u\n", i + 1);
		rc = battery_chg_fw_write(bcdev, &msg, sizeof(msg));
		if (rc < 0)
			return rc;
	}

	return 0;
}

static int wireless_fw_check_for_update(struct battery_chg_dev *bcdev,
					u32 version, size_t size)
{
	struct wireless_fw_check_req req_msg = {};

	bcdev->wls_fw_update_reqd = false;

	req_msg.hdr.owner = MSG_OWNER_BC;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = BC_WLS_FW_CHECK_UPDATE;
	req_msg.fw_version = version;
	req_msg.fw_size = size;

	return battery_chg_write(bcdev, &req_msg, sizeof(req_msg));
}

#define IDT_FW_MAJOR_VER_OFFSET		0x94
#define IDT_FW_MINOR_VER_OFFSET		0x96
static int wireless_fw_update(struct battery_chg_dev *bcdev, bool force)
{
	const struct firmware *fw;
	struct psy_state *pst;
	u32 version;
	u16 maj_ver, min_ver;
	int rc;

	pm_stay_awake(bcdev->dev);

	/*
	 * Check for USB presence. If nothing is connected, check whether
	 * battery SOC is at least 50% before allowing FW update.
	 */
	pst = &bcdev->psy_list[PSY_TYPE_USB];
	rc = read_property_id(bcdev, pst, USB_ONLINE);
	if (rc < 0)
		goto out;

	if (!pst->prop[USB_ONLINE]) {
		pst = &bcdev->psy_list[PSY_TYPE_BATTERY];
		rc = read_property_id(bcdev, pst, BATT_CAPACITY);
		if (rc < 0)
			goto out;

		if ((pst->prop[BATT_CAPACITY] / 100) < 50) {
			pr_err("Battery SOC should be at least 50%% or connect charger\n");
			rc = -EINVAL;
			goto out;
		}
	}

	rc = firmware_request_nowarn(&fw, bcdev->wls_fw_name, bcdev->dev);
	if (rc) {
		pr_err("Couldn't get firmware rc=%d\n", rc);
		goto out;
	}

	if (!fw || !fw->data || !fw->size) {
		pr_err("Invalid firmware\n");
		rc = -EINVAL;
		goto release_fw;
	}

	if (fw->size < SZ_16K) {
		pr_err("Invalid firmware size %zu\n", fw->size);
		rc = -EINVAL;
		goto release_fw;
	}

	maj_ver = le16_to_cpu(*(__le16 *)(fw->data + IDT_FW_MAJOR_VER_OFFSET));
	min_ver = le16_to_cpu(*(__le16 *)(fw->data + IDT_FW_MINOR_VER_OFFSET));
	version = maj_ver << 16 | min_ver;

	if (force)
		version = UINT_MAX;

	pr_debug("FW size: %zu version: %#x\n", fw->size, version);

	rc = wireless_fw_check_for_update(bcdev, version, fw->size);
	if (rc < 0) {
		pr_err("Wireless FW update not needed, rc=%d\n", rc);
		goto release_fw;
	}

	if (!bcdev->wls_fw_update_reqd) {
		pr_warn("Wireless FW update not required\n");
		goto release_fw;
	}

	/* Wait for IDT to be setup by charger firmware */
	msleep(WLS_FW_PREPARE_TIME_MS);

	reinit_completion(&bcdev->fw_update_ack);
	rc = wireless_fw_send_firmware(bcdev, fw);
	if (rc < 0) {
		pr_err("Failed to send FW chunk, rc=%d\n", rc);
		goto release_fw;
	}

	rc = wait_for_completion_timeout(&bcdev->fw_update_ack,
				msecs_to_jiffies(WLS_FW_WAIT_TIME_MS));
	if (!rc) {
		pr_err("Error, timed out updating firmware\n");
		rc = -ETIMEDOUT;
		goto release_fw;
	} else {
		rc = 0;
	}

	pr_info("Wireless FW update done\n");

release_fw:
	release_firmware(fw);
out:
	pm_relax(bcdev->dev);

	return rc;
}

static ssize_t wireless_fw_version_show(struct class *c,
					struct class_attribute *attr,
					char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct wireless_fw_get_version_req req_msg = {};
	int rc;

	req_msg.hdr.owner = MSG_OWNER_BC;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = BC_WLS_FW_GET_VERSION;

	rc = battery_chg_write(bcdev, &req_msg, sizeof(req_msg));
	if (rc < 0) {
		pr_err("Failed to get FW version rc=%d\n", rc);
		return rc;
	}

	return scnprintf(buf, PAGE_SIZE, "%#x\n", bcdev->wls_fw_version);
}
static CLASS_ATTR_RO(wireless_fw_version);

static ssize_t wireless_fw_force_update_store(struct class *c,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	bool val;
	int rc;

	if (kstrtobool(buf, &val) || !val)
		return -EINVAL;

	rc = wireless_fw_update(bcdev, true);
	if (rc < 0)
		return rc;

	return count;
}
static CLASS_ATTR_WO(wireless_fw_force_update);

static ssize_t wireless_fw_update_store(struct class *c,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	bool val;
	int rc;

	if (kstrtobool(buf, &val) || !val)
		return -EINVAL;

	rc = wireless_fw_update(bcdev, false);
	if (rc < 0)
		return rc;

	return count;
}
static CLASS_ATTR_WO(wireless_fw_update);

static ssize_t fake_soc_store(struct class *c, struct class_attribute *attr,
				const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_BATTERY];
	int val;

	if (kstrtoint(buf, 0, &val))
		return -EINVAL;

	bcdev->fake_soc = val;
	pr_debug("Set fake soc to %d\n", val);

	if (IS_ENABLED(CONFIG_QTI_PMIC_GLINK_CLIENT_DEBUG) && pst->psy)
		power_supply_changed(pst->psy);

	return count;
}

static ssize_t fake_soc_show(struct class *c, struct class_attribute *attr,
				char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);

	return scnprintf(buf, PAGE_SIZE, "%d\n", bcdev->fake_soc);
}
static CLASS_ATTR_RW(fake_soc);

static ssize_t wireless_boost_en_store(struct class *c,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	int rc;
	bool val;

	if (kstrtobool(buf, &val))
		return -EINVAL;

	rc = write_property_id(bcdev, &bcdev->psy_list[PSY_TYPE_WLS],
				WLS_BOOST_EN, val);
	if (rc < 0)
		return rc;

	return count;
}

static ssize_t wireless_boost_en_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_WLS];
	int rc;

	rc = read_property_id(bcdev, pst, WLS_BOOST_EN);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n", pst->prop[WLS_BOOST_EN]);
}
static CLASS_ATTR_RW(wireless_boost_en);

static ssize_t moisture_detection_en_store(struct class *c,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	int rc;
	bool val;

	if (kstrtobool(buf, &val))
		return -EINVAL;

	rc = write_property_id(bcdev, &bcdev->psy_list[PSY_TYPE_USB],
				USB_MOISTURE_DET_EN, val);
	if (rc < 0)
		return rc;

	return count;
}

static ssize_t moisture_detection_en_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_USB];
	int rc;

	rc = read_property_id(bcdev, pst, USB_MOISTURE_DET_EN);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n",
			pst->prop[USB_MOISTURE_DET_EN]);
}
static CLASS_ATTR_RW(moisture_detection_en);

static ssize_t moisture_detection_status_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_USB];
	int rc;

	rc = read_property_id(bcdev, pst, USB_MOISTURE_DET_STS);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n",
			pst->prop[USB_MOISTURE_DET_STS]);
}
static CLASS_ATTR_RO(moisture_detection_status);

static ssize_t resistance_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_BATTERY];
	int rc;

	rc = read_property_id(bcdev, pst, BATT_RESISTANCE);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%u\n", pst->prop[BATT_RESISTANCE]);
}
static CLASS_ATTR_RO(resistance);

static ssize_t soh_show(struct class *c, struct class_attribute *attr,
			char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_BATTERY];
	int rc;

	rc = read_property_id(bcdev, pst, BATT_SOH);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n", pst->prop[BATT_SOH]);
}
static CLASS_ATTR_RO(soh);

static ssize_t ship_mode_en_store(struct class *c, struct class_attribute *attr,
				const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);

	if (kstrtobool(buf, &bcdev->ship_mode_en))
		return -EINVAL;

	return count;
}

static ssize_t ship_mode_en_show(struct class *c, struct class_attribute *attr,
				char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);

	return scnprintf(buf, PAGE_SIZE, "%d\n", bcdev->ship_mode_en);
}
static CLASS_ATTR_RW(ship_mode_en);

static struct attribute *battery_class_attrs[] = {
	&class_attr_soh.attr,
	&class_attr_resistance.attr,
	&class_attr_moisture_detection_status.attr,
	&class_attr_moisture_detection_en.attr,
	&class_attr_wireless_boost_en.attr,
	&class_attr_fake_soc.attr,
	&class_attr_wireless_fw_update.attr,
	&class_attr_wireless_fw_force_update.attr,
	&class_attr_wireless_fw_version.attr,
	&class_attr_ship_mode_en.attr,
	NULL,
};
ATTRIBUTE_GROUPS(battery_class);

#ifdef CONFIG_DEBUG_FS
static void battery_chg_add_debugfs(struct battery_chg_dev *bcdev)
{
	int rc;
	struct dentry *dir, *file;

	dir = debugfs_create_dir("battery_charger", NULL);
	if (IS_ERR(dir)) {
		rc = PTR_ERR(dir);
		pr_err("Failed to create charger debugfs directory, rc=%d\n",
			rc);
		return;
	}

	file = debugfs_create_bool("block_tx", 0600, dir, &bcdev->block_tx);
	if (IS_ERR(file)) {
		rc = PTR_ERR(file);
		pr_err("Failed to create block_tx debugfs file, rc=%d\n",
			rc);
		goto error;
	}

	bcdev->debugfs_dir = dir;

	return;
error:
	debugfs_remove_recursive(dir);
}
#else
static void battery_chg_add_debugfs(struct battery_chg_dev *bcdev) { }
#endif

static int battery_chg_parse_dt(struct battery_chg_dev *bcdev)
{
	struct device_node *node = bcdev->dev->of_node;
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_BATTERY];
	int i, rc, len;
	u32 prev, val;

	of_property_read_string(node, "qcom,wireless-fw-name",
				&bcdev->wls_fw_name);

	rc = of_property_count_elems_of_size(node, "qcom,thermal-mitigation",
						sizeof(u32));
	if (rc <= 0)
		return 0;

	len = rc;

	rc = read_property_id(bcdev, pst, BATT_CHG_CTRL_LIM_MAX);
	if (rc < 0)
		return rc;

	prev = pst->prop[BATT_CHG_CTRL_LIM_MAX];

	for (i = 0; i < len; i++) {
		rc = of_property_read_u32_index(node, "qcom,thermal-mitigation",
						i, &val);
		if (rc < 0)
			return rc;

		if (val > prev) {
			pr_err("Thermal levels should be in descending order\n");
			bcdev->num_thermal_levels = -EINVAL;
			return 0;
		}

		prev = val;
	}

	bcdev->thermal_levels = devm_kcalloc(bcdev->dev, len + 1,
					sizeof(*bcdev->thermal_levels),
					GFP_KERNEL);
	if (!bcdev->thermal_levels)
		return -ENOMEM;

	/*
	 * Element 0 is for normal charging current. Elements from index 1
	 * onwards is for thermal mitigation charging currents.
	 */

	bcdev->thermal_levels[0] = pst->prop[BATT_CHG_CTRL_LIM_MAX];

	rc = of_property_read_u32_array(node, "qcom,thermal-mitigation",
					&bcdev->thermal_levels[1], len);
	if (rc < 0) {
		pr_err("Error in reading qcom,thermal-mitigation, rc=%d\n", rc);
		return rc;
	}

	bcdev->num_thermal_levels = len;

	return 0;
}

static int battery_chg_ship_mode(struct notifier_block *nb, unsigned long code,
		void *unused)
{
	struct battery_charger_ship_mode_req_msg msg = { { 0 } };
	struct battery_chg_dev *bcdev = container_of(nb, struct battery_chg_dev,
						     reboot_notifier);
	int rc;

	if (!bcdev->ship_mode_en)
		return NOTIFY_DONE;

	msg.hdr.owner = MSG_OWNER_BC;
	msg.hdr.type = MSG_TYPE_REQ_RESP;
	msg.hdr.opcode = BC_SHIP_MODE_REQ_SET;
	msg.ship_mode_type = SHIP_MODE_PMIC;

	if (code == SYS_POWER_OFF) {
		rc = battery_chg_write(bcdev, &msg, sizeof(msg));
		if (rc < 0)
			pr_emerg("Failed to write ship mode: %d\n", rc);
	}

	return NOTIFY_DONE;
}

/**********************************************************************
 * battery charge ops *
 **********************************************************************/
#ifdef VENDOR_EDIT
static void dump_regs(void)
{
	return;
}

static int smbchg_kick_wdt(void)
{
	return 0;
}

static int smbchg_set_fastchg_current_raw(int current_ma)
{
	int rc = 0;
	int prop_id = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return -1;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	prop_id = get_property_id(pst, POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT);
	rc = write_property_id(bcdev, pst, prop_id, current_ma * 1000);
	if (rc)
		chg_err("set fcc to %d mA fail, rc=%d\n", current_ma, rc);
	else
		chg_err("set fcc to %d mA\n", current_ma);

	return rc;
}

static int smbchg_set_wls_boost_en(bool enable)
{
	int rc = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return -1;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_WLS];

	if (enable) {
		rc = write_property_id(bcdev, pst, WLS_BOOST_EN, 1);
	} else {
		rc = write_property_id(bcdev, pst, WLS_BOOST_EN, 0);
	}
	if (rc) {
		chg_err("set fcc to %d mA fail, rc=%d\n", enable, rc);
	} else {
		chg_err("set fcc to %d mA\n", enable);
	}
	return rc;
}

bool qpnp_get_prop_vbus_collapse_status(void)
{
	int rc = 0;
	bool collapse_status = false;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		return false;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	rc = read_property_id(bcdev, pst, USB_VBUS_COLLAPSE_STATUS);
	if (rc < 0) {
		chg_err("read usb vbus_collapse_status fail, rc=%d\n", rc);
		return false;
	}
	collapse_status = pst->prop[USB_VBUS_COLLAPSE_STATUS];
	chg_err("read usb vbus_collapse_status[%d]\n",
			collapse_status);
	return collapse_status;
}

static int oplus_chg_set_input_current_with_no_aicl(int current_ma)
{
	int rc = 0;
	int prop_id = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return -1;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	prop_id = get_property_id(pst, POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT);
	rc = write_property_id(bcdev, pst, prop_id, current_ma * 1000);
	if (rc)
		chg_err("set icl to %d mA fail, rc=%d\n", current_ma, rc);
	else
		chg_err("set icl to %d mA\n", current_ma);

	return rc;
}

static void smbchg_set_aicl_point(int vol)
{
	/*do nothing*/
}

static int usb_icl[] = {
	300, 500, 900, 1200, 1350, 1500, 1750, 2000, 3000,
};

static int oplus_chg_set_input_current(int current_ma)
{
	int rc = 0, i = 0;
	int chg_vol = 0;
	int aicl_point = 0;
	int prop_id = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return -1;
	}

	if (chip->mmi_chg == 0) {
		chg_debug("mmi_chg, return\n");
		return rc;
	}

	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_USB];
	prop_id = get_property_id(pst, POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT);

	chg_debug("usb input max current limit=%d setting %02x\n", current_ma, i);
	if (chip->batt_volt > 4100) {
		aicl_point = 4550;
	} else {
		aicl_point = 4500;
	}

	if (current_ma < 500) {
		i = 0;
		goto aicl_end;
	}

	i = 1; /* 500 */
	rc = write_property_id(bcdev, pst, prop_id, usb_icl[i] * 1000);
	if (rc) {
		chg_err("set icl to %d mA fail, rc=%d\n", usb_icl[i], rc);
	} else {
		chg_err("set icl to %d mA\n", usb_icl[i]);
	}
	usleep_range(50000, 51000);
	if (qpnp_get_prop_vbus_collapse_status() == true) {
		chg_debug("use 500 here\n");
		goto aicl_boost_back;
	}
	chg_vol = qpnp_get_prop_charger_voltage_now();
	if (chg_vol < aicl_point) {
		chg_debug("use 500 here\n");
		goto aicl_end;
	} else if (current_ma < 900)
		goto aicl_end;

	i = 2; /* 900 */
	rc = write_property_id(bcdev, pst, prop_id, usb_icl[i] * 1000);
	if (rc) {
		chg_err("set icl to %d mA fail, rc=%d\n", usb_icl[i], rc);
	} else {
		chg_err("set icl to %d mA\n", usb_icl[i]);
	}
	usleep_range(50000, 51000);
	if (qpnp_get_prop_vbus_collapse_status() == true) {
		i = i - 1;
		goto aicl_boost_back;
	}
	chg_vol = qpnp_get_prop_charger_voltage_now();
	if (chg_vol < aicl_point) {
		i = i - 1;
		goto aicl_pre_step;
	} else if (current_ma < 1200)
		goto aicl_end;

	i = 3; /* 1200 */
	rc = write_property_id(bcdev, pst, prop_id, usb_icl[i] * 1000);
	if (rc) {
		chg_err("set icl to %d mA fail, rc=%d\n", usb_icl[i], rc);
	} else {
		chg_err("set icl to %d mA\n", usb_icl[i]);
	}
	usleep_range(90000, 91000);
	if (qpnp_get_prop_vbus_collapse_status() == true) {
		i = i - 1;
		goto aicl_boost_back;
	}
	chg_vol = qpnp_get_prop_charger_voltage_now();
	if (chg_vol < aicl_point) {
		i = i - 1;
		goto aicl_pre_step;
	}

	i = 4; /* 1350 */
	rc = write_property_id(bcdev, pst, prop_id, usb_icl[i] * 1000);
	if (rc) {
		chg_err("set icl to %d mA fail, rc=%d\n", usb_icl[i], rc);
	} else {
		chg_err("set icl to %d mA\n", usb_icl[i]);
	}
	usleep_range(130000, 131000);
	if (qpnp_get_prop_vbus_collapse_status() == true) {
		i = i - 2;
		goto aicl_boost_back;
	}
	chg_vol = qpnp_get_prop_charger_voltage_now();
	if (chg_vol < aicl_point) {
		i = i - 2;
		goto aicl_pre_step;
	}

	i = 5; /* 1500 */
	rc = write_property_id(bcdev, pst, prop_id, usb_icl[i] * 1000);
	if (rc) {
		chg_err("set icl to %d mA fail, rc=%d\n", usb_icl[i], rc);
	} else {
		chg_err("set icl to %d mA\n", usb_icl[i]);
	}
	usleep_range(90000, 91000);
	if (qpnp_get_prop_vbus_collapse_status() == true) {
		i = i - 3;
		goto aicl_boost_back;
	}
	chg_vol = qpnp_get_prop_charger_voltage_now();
	if (chg_vol < aicl_point) {
		i = i - 3; /*We DO NOT use 1.2A here*/
		goto aicl_pre_step;
	} else if (current_ma < 1500) {
		i = i - 2; /*We use 1.2A here*/
		goto aicl_end;
	} else if (current_ma < 2000)
		goto aicl_end;

	i = 6; /* 1750 */
	rc = write_property_id(bcdev, pst, prop_id, usb_icl[i] * 1000);
	if (rc) {
		chg_err("set icl to %d mA fail, rc=%d\n", usb_icl[i], rc);
	} else {
		chg_err("set icl to %d mA\n", usb_icl[i]);
	}
	usleep_range(50000, 51000);
	if (qpnp_get_prop_vbus_collapse_status() == true) {
		i = i - 3;
		goto aicl_boost_back;
	}
	chg_vol = qpnp_get_prop_charger_voltage_now();
	if (chg_vol < aicl_point) {
		i = i - 3; /*1.2*/
		goto aicl_pre_step;
	}

	i = 7; /* 2000 */
	rc = write_property_id(bcdev, pst, prop_id, usb_icl[i] * 1000);
	if (rc) {
		chg_err("set icl to %d mA fail, rc=%d\n", usb_icl[i], rc);
	} else {
		chg_err("set icl to %d mA\n", usb_icl[i]);
	}
	usleep_range(50000, 51000);
	if (qpnp_get_prop_vbus_collapse_status() == true) {
		i = i - 2;
		goto aicl_boost_back;
	}
	chg_vol = qpnp_get_prop_charger_voltage_now();
	if (chg_vol < aicl_point) {
		i =  i - 2; /*1.5*/
		goto aicl_pre_step;
	} else if (current_ma < 3000)
		goto aicl_end;

	i = 8; /* 3000 */
	rc = write_property_id(bcdev, pst, prop_id, usb_icl[i] * 1000);
	if (rc) {
		chg_err("set icl to %d mA fail, rc=%d\n", usb_icl[i], rc);
	} else {
		chg_err("set icl to %d mA\n", usb_icl[i]);
	}
	usleep_range(90000, 91000);
	if (qpnp_get_prop_vbus_collapse_status() == true) {
		i = i - 1;
		goto aicl_boost_back;
	}
	chg_vol = qpnp_get_prop_charger_voltage_now();
	if (chg_vol < aicl_point) {
		i = i - 1;
		goto aicl_pre_step;
	} else if (current_ma >= 3000)
		goto aicl_end;

aicl_pre_step:
	rc = write_property_id(bcdev, pst, prop_id, usb_icl[i] * 1000);
	if (rc) {
		chg_err("set icl to %d mA fail, rc=%d\n", usb_icl[i], rc);
	} else {
		chg_err("set icl to %d mA\n", usb_icl[i]);
	}
	chg_debug("usb input max current limit aicl chg_vol=%d j[%d]=%d sw_aicl_point:%d aicl_pre_step\n", chg_vol, i, usb_icl[i], aicl_point);
	goto aicl_return;
aicl_end:
	rc = write_property_id(bcdev, pst, prop_id, usb_icl[i] * 1000);
	if (rc) {
		chg_err("set icl to %d mA fail, rc=%d\n", usb_icl[i], rc);
	} else {
		chg_err("set icl to %d mA\n", usb_icl[i]);
	}
	chg_debug("usb input max current limit aicl chg_vol=%d j[%d]=%d sw_aicl_point:%d aicl_end\n", chg_vol, i, usb_icl[i], aicl_point);
	goto aicl_return;
aicl_boost_back:
	rc = write_property_id(bcdev, pst, prop_id, usb_icl[i] * 1000);
	if (rc) {
		chg_err("set icl to %d mA fail, rc=%d\n", usb_icl[i], rc);
	} else {
		chg_err("set icl to %d mA\n", usb_icl[i]);
	}
	chg_debug("usb input max current limit aicl chg_vol=%d j[%d]=%d sw_aicl_point:%d aicl_boost_back\n", chg_vol, i, usb_icl[i], aicl_point);
	goto aicl_return;
aicl_return:
	return rc;
}

static int smbchg_float_voltage_set(int vfloat_mv)
{
	int rc = 0;
	int prop_id = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return -1;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	prop_id = get_property_id(pst, POWER_SUPPLY_PROP_VOLTAGE_MAX);
	rc = write_property_id(bcdev, pst, prop_id, vfloat_mv);
	if (rc)
		chg_err("set fv to %d mV fail, rc=%d\n", vfloat_mv, rc);
	else
		chg_err("set fv to %d mV\n", vfloat_mv);

	return rc;
}

static int smbchg_term_current_set(int term_current)
{
	int rc = 0;
#if 0
	u8 val_raw = 0;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (term_current < 0 || term_current > 750)
		term_current = 150;

	val_raw = term_current / 50;
	rc = smblib_masked_write(&chip->pmic_spmi.smb5_chip->chg, TCCC_CHARGE_CURRENT_TERMINATION_CFG_REG,
			TCCC_CHARGE_CURRENT_TERMINATION_SETTING_MASK, val_raw);
	if (rc < 0)
		chg_err("Couldn't write TCCC_CHARGE_CURRENT_TERMINATION_CFG_REG rc=%d\n", rc);
#endif
	return rc;
}

static int smbchg_charging_enable(void)
{
	int rc = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return -1;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	rc = write_property_id(bcdev, pst, BATT_CHG_EN, 1);
	if (rc)
		chg_err("set enable charging fail, rc=%d\n", rc);

	return rc;
}

static int smbchg_charging_disable(void)
{
	int rc = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return -1;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	rc = write_property_id(bcdev, pst, BATT_CHG_EN, 0);
	if (rc)
		chg_err("set disable charging fail, rc=%d\n", rc);

	return rc;
}

static int smbchg_get_charge_enable(void)
{
	int rc = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return -1;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	rc = read_property_id(bcdev, pst, BATT_CHG_EN);
	if (rc) {
		chg_err("set disable charging fail, rc=%d\n", rc);
		return rc;
	}
	chg_err("get charge enable[%d]\n", pst->prop[BATT_CHG_EN]);

	return pst->prop[BATT_CHG_EN];
}

static int smbchg_usb_suspend_enable(void)
{
	int rc = 0;
	int prop_id = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return -1;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	prop_id = get_property_id(pst, POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT);
	rc = write_property_id(bcdev, pst, prop_id, 0);
	if (rc)
		chg_err("set suspend fail, rc=%d\n", rc);
	else
		chg_err("set chg suspend\n");

	return rc;
}

static int smbchg_usb_suspend_disable(void)
{
	int rc = 0;
	int prop_id = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return -1;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	prop_id = get_property_id(pst, POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT);
	rc = write_property_id(bcdev, pst, prop_id, 0xFFFFFFFF);
	if (rc)
		chg_err("set unsuspend to fail, rc=%d\n", rc);
	else
		chg_err("set chg unsuspend\n");

	return rc;
}

static int oplus_chg_hw_init(void)
{
	int boot_mode = get_boot_mode();

	if (boot_mode != MSM_BOOT_MODE__RF && boot_mode != MSM_BOOT_MODE__WLAN) {
		smbchg_usb_suspend_disable();
	} else {
		smbchg_usb_suspend_enable();
	}

	oplus_chg_set_input_current_with_no_aicl(500);
	smbchg_charging_enable();

	return 0;
}

static int smbchg_set_rechg_vol(int rechg_vol)
{
	return 0;
}

static int smbchg_reset_charger(void)
{
	return 0;
}

static int smbchg_read_full(void)
{
#if 0
	int rc = 0;
	u8 stat = 0;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!oplus_chg_is_usb_present())
		return 0;

	rc = smblib_read(&chip->pmic_spmi.smb5_chip->chg, BATTERY_CHARGER_STATUS_1_REG, &stat);
	if (rc < 0) {
		chg_err("Couldn't read BATTERY_CHARGER_STATUS_1 rc=%d\n", rc);
		return 0;
	}
	stat = stat & BATTERY_CHARGER_STATUS_MASK;

	if (stat == TERMINATE_CHARGE || stat == INHIBIT_CHARGE)
		return 1;
#endif
	return 0;
}

static int smbchg_otg_enable(void)
{
	return 0;
}

static int smbchg_otg_disable(void)
{
	return 0;
}

static int oplus_set_chging_term_disable(void)
{
	return 0;
}

static bool qcom_check_charger_resume(void)
{
	return true;
}

static int smbchg_get_chargerid_volt(void)
{
	return 0;
}

static void smbchg_set_chargerid_switch_val(int value)
{
	return;
}

static int smbchg_get_chargerid_switch_val(void)
{
	return -1;
}

static bool smbchg_need_to_check_ibatt(void)
{
	return false;
}

static int smbchg_get_chg_current_step(void)
{
	return 25;
}

static int opchg_get_charger_type(void)
{
	int rc = 0;
	int prop_id = 0;
	static int charger_type = POWER_SUPPLY_TYPE_UNKNOWN;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		return POWER_SUPPLY_TYPE_UNKNOWN;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	prop_id = get_property_id(pst, POWER_SUPPLY_PROP_USB_TYPE);
	rc = read_property_id(bcdev, pst, prop_id);
	if (rc < 0) {
		chg_err("read usb charger_type fail, rc=%d\n", rc);
		return charger_type;
	}
	switch (pst->prop[prop_id]) {
		case POWER_SUPPLY_USB_TYPE_UNKNOWN:
			charger_type = POWER_SUPPLY_TYPE_UNKNOWN;
			break;
		case POWER_SUPPLY_USB_TYPE_SDP:
			charger_type = POWER_SUPPLY_TYPE_USB;
			break;
		case POWER_SUPPLY_USB_TYPE_CDP:
			charger_type = POWER_SUPPLY_TYPE_USB_CDP;
			break;
		case POWER_SUPPLY_USB_TYPE_DCP:
			charger_type = POWER_SUPPLY_TYPE_USB_DCP;
			break;
		default:
			charger_type = POWER_SUPPLY_TYPE_USB_DCP;
			break;
	}
	
	return charger_type;
}

static int qpnp_get_prop_charger_voltage_now(void)
{
	int rc = 0;
	int prop_id = 0;
	static int vbus_volt = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		return 0;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	prop_id = get_property_id(pst, POWER_SUPPLY_PROP_VOLTAGE_NOW);
	rc = read_property_id(bcdev, pst, prop_id);
	if (rc < 0) {
		chg_err("read usb vbus_volt fail, rc=%d\n", rc);
		return vbus_volt;
	}
	vbus_volt = pst->prop[prop_id] / 1000;

	return vbus_volt;
}

static int oplus_get_ibus_current(void)
{
	int rc = 0;
	int prop_id = 0;
	static int ibus = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		return -1;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	prop_id = get_property_id(pst, POWER_SUPPLY_PROP_CURRENT_NOW);
	rc = read_property_id(bcdev, pst, prop_id);
	if (rc < 0) {
		chg_err("read battery curr fail, rc=%d\n", rc);
		return ibus;
	}
	ibus = DIV_ROUND_CLOSEST((int)pst->prop[prop_id], 1000);

	return ibus;
}

static bool oplus_chg_is_usb_present(void)
{
	int rc = 0;
	int prop_id = 0;
	bool vbus_rising = false;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		return false;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	prop_id = get_property_id(pst, POWER_SUPPLY_PROP_ONLINE);
	rc = read_property_id(bcdev, pst, prop_id);
	if (rc < 0) {
		chg_err("read usb vbus_rising fail, rc=%d\n", rc);
		return false;
	}
	vbus_rising = pst->prop[prop_id];

	return vbus_rising;
}

static int qpnp_get_battery_voltage(void)
{
	return 3800;//Not use anymore
}

#if 0
static int get_boot_mode(void)
{
	return 0;
}
#endif

static int smbchg_get_boot_reason(void)
{
	return 0;
}

static int oplus_chg_get_shutdown_soc(void)
{
	return 0;
}

static int oplus_chg_backup_soc(int backup_soc)
{
	return 0;
}

static int smbchg_get_aicl_level_ma(void)
{
	return 0;
}

static void smbchg_rerun_aicl(void)
{
	//smbchg_aicl_enable(false);
	/* Add a delay so that AICL successfully clears */
	//msleep(50);
	//smbchg_aicl_enable(true);
}

#ifdef CONFIG_OPLUS_RTC_DET_SUPPORT
static int rtc_reset_check(void)
{
	struct rtc_time tm;
	struct rtc_device *rtc;
	int rc = 0;

	rtc = rtc_class_open(CONFIG_RTC_HCTOSYS_DEVICE);
	if (rtc == NULL) {
		pr_err("%s: unable to open rtc device (%s)\n",
			__FILE__, CONFIG_RTC_HCTOSYS_DEVICE);
		return 0;
	}

	rc = rtc_read_time(rtc, &tm);
	if (rc) {
		pr_err("Error reading rtc device (%s) : %d\n",
			CONFIG_RTC_HCTOSYS_DEVICE, rc);
		goto close_time;
	}

	rc = rtc_valid_tm(&tm);
	if (rc) {
		pr_err("Invalid RTC time (%s): %d\n",
			CONFIG_RTC_HCTOSYS_DEVICE, rc);
		goto close_time;
	}

	if ((tm.tm_year == 70) && (tm.tm_mon == 0) && (tm.tm_mday <= 1)) {
		chg_debug(": Sec: %d, Min: %d, Hour: %d, Day: %d, Mon: %d, Year: %d  @@@ wday: %d, yday: %d, isdst: %d\n",
			tm.tm_sec, tm.tm_min, tm.tm_hour, tm.tm_mday, tm.tm_mon, tm.tm_year,
			tm.tm_wday, tm.tm_yday, tm.tm_isdst);
		rtc_class_close(rtc);
		return 1;
	}

	chg_debug(": Sec: %d, Min: %d, Hour: %d, Day: %d, Mon: %d, Year: %d  ###  wday: %d, yday: %d, isdst: %d\n",
		tm.tm_sec, tm.tm_min, tm.tm_hour, tm.tm_mday, tm.tm_mon, tm.tm_year,
		tm.tm_wday, tm.tm_yday, tm.tm_isdst);

close_time:
	rtc_class_close(rtc);
	return 0;
}
#endif /* CONFIG_OPLUS_RTC_DET_SUPPORT */

#ifdef CONFIG_OPLUS_SHORT_C_BATT_CHECK
/* This function is getting the dynamic aicl result/input limited in mA.
 * If charger was suspended, it must return 0(mA).
 * It meets the requirements in SDM660 platform.
 */
static int oplus_chg_get_dyna_aicl_result(void)
{
	struct power_supply *usb_psy = NULL;
	union power_supply_propval pval = {0, };

	usb_psy = power_supply_get_by_name("usb");
	if (usb_psy) {
		power_supply_get_property(usb_psy,
				POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
				&pval);
		return pval.intval / 1000;
	}

	return 1000;
}
#endif /* CONFIG_OPLUS_SHORT_C_BATT_CHECK */

static int oplus_chg_get_charger_subtype(void)
{
	int rc = 0;
	int prop_id = 0;
	static int charg_subtype = CHARGER_SUBTYPE_DEFAULT;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		return CHARGER_SUBTYPE_DEFAULT;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	prop_id = get_property_id(pst, POWER_SUPPLY_PROP_USB_TYPE);
	rc = read_property_id(bcdev, pst, prop_id);
	if (rc < 0) {
		chg_err("read charger type fail, rc=%d\n", rc);
		if (!chip->charger_exist)
			charg_subtype = CHARGER_SUBTYPE_DEFAULT;
		return charg_subtype;
	}
	switch (pst->prop[prop_id]) {
		case POWER_SUPPLY_USB_TYPE_PD:
		case POWER_SUPPLY_USB_TYPE_PD_DRP:
		case POWER_SUPPLY_USB_TYPE_PD_PPS:
			charg_subtype = CHARGER_SUBTYPE_PD;
			break;
		default:
			charg_subtype = CHARGER_SUBTYPE_DEFAULT;
			break;
	}

	if (charg_subtype == CHARGER_SUBTYPE_DEFAULT) {
		rc = read_property_id(bcdev, pst, USB_ADAP_SUBTYPE);
		if (rc < 0) {
			chg_err("read charger subtype fail, rc=%d\n", rc);
			if (!chip->charger_exist)
				charg_subtype = CHARGER_SUBTYPE_DEFAULT;
			return charg_subtype;
		}
		switch (pst->prop[USB_ADAP_SUBTYPE]) {
			case CHARGER_SUBTYPE_FASTCHG_WARP:
				charg_subtype = CHARGER_SUBTYPE_FASTCHG_WARP;
				break;
			case CHARGER_SUBTYPE_FASTCHG_SWARP:
				charg_subtype = CHARGER_SUBTYPE_FASTCHG_SWARP;
				break;
			case CHARGER_SUBTYPE_QC:
				charg_subtype = CHARGER_SUBTYPE_QC;
				break;
			default:
				charg_subtype = CHARGER_SUBTYPE_DEFAULT;
				break;
		}
	}

	return charg_subtype;
}

static bool oplus_chg_get_pd_type(void)
{
	int rc = 0;
	int prop_id = 0;
	static bool is_pd_type = false;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		return false;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	prop_id = get_property_id(pst, POWER_SUPPLY_PROP_USB_TYPE);
	rc = read_property_id(bcdev, pst, prop_id);
	if (rc < 0) {
		chg_err("read usb pd_type fail, rc=%d\n", rc);
		if (!chip->charger_exist)
			is_pd_type = false;
		return is_pd_type;
	}
	switch (pst->prop[prop_id]) {
		case POWER_SUPPLY_USB_TYPE_PD:
		case POWER_SUPPLY_USB_TYPE_PD_DRP:
		case POWER_SUPPLY_USB_TYPE_PD_PPS:
			is_pd_type = true;
			break;
		default:
			is_pd_type = false;
			break;
	}

	return is_pd_type;
}

static int oplus_chg_set_pd_config(void)
{
	int rc = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return -1;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	if (chip->limits.vbatt_pdqc_to_5v_thr > 0 && chip->charger_volt > 7500 && chip->batt_volt > chip->limits.vbatt_pdqc_to_5v_thr) {
		rc = write_property_id(bcdev, pst, BATT_SET_PDO, 5000);
		if (rc)
			chg_err("set PDO 5V fail, rc=%d\n", rc);
		else
			chg_err("set PDO 5V OK\n");
	} else if (chip->batt_volt < 5000) {//alway to 9
		rc = write_property_id(bcdev, pst, BATT_SET_PDO, 9000);
		if (rc)
			chg_err("set PDO 9V fail, rc=%d\n", rc);
		else
			chg_err("set PDO 9V OK\n");
	}

	return rc;
}

static int oplus_chg_set_qc_config(void)
{
	int rc = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return -1;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	if (chip->limits.vbatt_pdqc_to_5v_thr > 0 && chip->charger_volt > 7500 && chip->batt_volt > chip->limits.vbatt_pdqc_to_5v_thr) {
		rc = write_property_id(bcdev, pst, BATT_SET_QC, 5000);
		if (rc)
			chg_err("set QC 5V fail, rc=%d\n", rc);
		else
			chg_err("set QC 5V OK\n");
	} else if (chip->batt_volt < 5000) {//alway to 9
		rc = write_property_id(bcdev, pst, BATT_SET_QC, 9000);
		if (rc)
			chg_err("set QC 9V fail, rc=%d\n", rc);
		else
			chg_err("set QC 9V OK\n");
	}

	return rc;
}

static int oplus_chg_enable_qc_detect(void)
{
	int rc = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return -1;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	rc = write_property_id(bcdev, pst, BATT_SET_QC, 0);

	return rc;
}

#endif /* VENDOR_EDIT */

#ifdef VENDOR_EDIT
static int oplus_input_current_limit_ctrl_by_warp_write(int current_ma)
{
	int rc = 0;
	int real_ibus = 0;
	int temp_curr = 0;

	real_ibus = oplus_get_ibus_current();
	chg_err(" get input_current = %d\n", real_ibus);

	if (current_ma > real_ibus) {
		for (temp_curr = real_ibus; temp_curr < current_ma; temp_curr += 500) {
			msleep(35);
			rc = oplus_chg_set_input_current_with_no_aicl(temp_curr);
			chg_err("[up] set input_current = %d\n", temp_curr);
		}
	} else {
		for (temp_curr = real_ibus; temp_curr > current_ma; temp_curr -= 500) {
			msleep(35);
			rc = oplus_chg_set_input_current_with_no_aicl(temp_curr);
			chg_err("[down] set input_current = %d\n", temp_curr);
		}
	}

	rc = oplus_chg_set_input_current_with_no_aicl(current_ma);
	return rc;
}
#endif

#ifdef VENDOR_EDIT
struct oplus_chg_operations  battery_chg_ops = {
	.dump_registers = dump_regs,
	.kick_wdt = smbchg_kick_wdt,
	.hardware_init = oplus_chg_hw_init,
	.charging_current_write_fast = smbchg_set_fastchg_current_raw,
	.set_wls_boost_en = smbchg_set_wls_boost_en,
	.set_aicl_point = smbchg_set_aicl_point,
	.input_current_write = oplus_chg_set_input_current,
	.float_voltage_write = smbchg_float_voltage_set,
	.term_current_set = smbchg_term_current_set,
	.charging_enable = smbchg_charging_enable,
	.charging_disable = smbchg_charging_disable,
	.get_charging_enable = smbchg_get_charge_enable,
	.charger_suspend = smbchg_usb_suspend_enable,
	.charger_unsuspend = smbchg_usb_suspend_disable,
	.set_rechg_vol = smbchg_set_rechg_vol,
	.reset_charger = smbchg_reset_charger,
	.read_full = smbchg_read_full,
	.otg_enable = smbchg_otg_enable,
	.otg_disable = smbchg_otg_disable,
	.set_charging_term_disable = oplus_set_chging_term_disable,
	.check_charger_resume = qcom_check_charger_resume,
	.get_chargerid_volt = smbchg_get_chargerid_volt,
	.set_chargerid_switch_val = smbchg_set_chargerid_switch_val,
	.get_chargerid_switch_val = smbchg_get_chargerid_switch_val,
	.need_to_check_ibatt = smbchg_need_to_check_ibatt,
	.get_chg_current_step = smbchg_get_chg_current_step,
	.get_charger_type = opchg_get_charger_type,
	.get_charger_volt = qpnp_get_prop_charger_voltage_now,
	.get_charger_current = oplus_get_ibus_current,
	.check_chrdet_status = oplus_chg_is_usb_present,
	.get_instant_vbatt = qpnp_get_battery_voltage,
	.get_boot_mode = (int (*)(void))get_boot_mode,
	.get_boot_reason = smbchg_get_boot_reason,
	.get_rtc_soc = oplus_chg_get_shutdown_soc,
	.set_rtc_soc = oplus_chg_backup_soc,
	.get_aicl_ma = smbchg_get_aicl_level_ma,
	.rerun_aicl = smbchg_rerun_aicl,
#ifdef CONFIG_OPLUS_RTC_DET_SUPPORT
	.check_rtc_reset = rtc_reset_check,
#endif
#ifdef CONFIG_OPLUS_SHORT_C_BATT_CHECK
	.get_dyna_aicl_result = oplus_chg_get_dyna_aicl_result,
#endif
	.get_charger_subtype = oplus_chg_get_charger_subtype,
	.oplus_chg_get_pd_type = oplus_chg_get_pd_type,
	.oplus_chg_pd_setup = oplus_chg_set_pd_config,
	.set_qc_config = oplus_chg_set_qc_config,
	.enable_qc_detect = oplus_chg_enable_qc_detect,
	.set_curr_level_to_warpphy = oplus_chg_set_curr_level_to_warpphy,
	.input_current_ctrl_by_warp_write = oplus_input_current_limit_ctrl_by_warp_write,
	//.input_current_write_without_aicl = mp2650_input_current_limit_without_aicl,
	//.oplus_chg_wdt_enable = mp2650_wdt_enable,
};
#endif /* VENDOR_EDIT */


/**********************************************************************
 * battery gauge ops *
 **********************************************************************/
#ifdef VENDOR_EDIT
static int fg_bq27541_get_battery_mvolts(void)
{
	int rc = 0;
	int prop_id = 0;
	static int volt = 4000;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		return -1;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	prop_id = get_property_id(pst, POWER_SUPPLY_PROP_VOLTAGE_NOW);
	rc = read_property_id(bcdev, pst, prop_id);
	if (rc < 0) {
		chg_err("read battery volt fail, rc=%d\n", rc);
		return volt;
	}
	volt = DIV_ROUND_CLOSEST(pst->prop[prop_id], 1000);

	return volt;
}

static int fg_bq27541_get_battery_temperature(void)
{
	int rc = 0;
	int prop_id = 0;
	static int temp = 250;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		return -1;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	prop_id = get_property_id(pst, POWER_SUPPLY_PROP_TEMP);
	rc = read_property_id(bcdev, pst, prop_id);
	if (rc < 0) {
		chg_err("read battery temp fail, rc=%d\n", rc);
		return temp;
	}
	temp = DIV_ROUND_CLOSEST((int)pst->prop[prop_id], 10);

	return temp;
}

static int fg_bq27541_get_batt_remaining_capacity(void)
{
	return 0;
}

static int fg_bq27541_get_battery_soc(void)
{
	int rc = 0;
	int prop_id = 0;
	static int soc = 50;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		return -1;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	prop_id = get_property_id(pst, POWER_SUPPLY_PROP_CAPACITY);
	rc = read_property_id(bcdev, pst, prop_id);
	if (rc < 0) {
		chg_err("read battery soc fail, rc=%d\n", rc);
		return soc;
	}
	soc = DIV_ROUND_CLOSEST(pst->prop[prop_id], 100);

	return soc;
}

static int fg_bq27541_get_average_current(void)
{
	int rc = 0;
	int prop_id = 0;
	static int curr = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		return -1;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	prop_id = get_property_id(pst, POWER_SUPPLY_PROP_CURRENT_NOW);
	rc = read_property_id(bcdev, pst, prop_id);
	if (rc < 0) {
		chg_err("read battery curr fail, rc=%d\n", rc);
		return curr;
	}
	curr = DIV_ROUND_CLOSEST((int)pst->prop[prop_id], 1000);

	return curr;
}

static int fg_bq27541_get_battery_fcc(void)
{
	return -1;
}

static int fg_bq27541_get_battery_cc(void)
{
	return -1;
}

static int fg_bq27541_get_battery_soh(void)
{
	return -1;
}

static bool fg_bq27541_get_battery_authenticate(void)
{
	return true;
}

static void fg_bq27541_set_battery_full(bool full)
{
	/*Do nothing*/
}
/*
static int fg_bq27541_get_prev_battery_mvolts(void)
{
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		return -1;
	}

	return chip->batt_volt;
}

static int fg_bq27541_get_prev_battery_temperature(void)
{
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		return -1;
	}

	return chip->temperature;
}

static int fg_bq27541_get_prev_battery_soc(void)
{
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		return -1;
	}

	return chip->soc;
}

static int fg_bq27541_get_prev_average_current(void)
{
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		return -1;
	}

	return chip->icharging;
}

static int fg_bq27541_get_prev_batt_remaining_capacity(void)
{
	return 0;
}
*/
static int fg_bq27541_get_battery_mvolts_2cell_max(void)
{
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		return -1;
	}

	return chip->batt_volt;
}

static int fg_bq27541_get_battery_mvolts_2cell_min(void)
{
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		return -1;
	}

	return chip->batt_volt;
}
/*
static int fg_bq27541_get_prev_battery_mvolts_2cell_max(void)
{
	return 4000;
}

static int fg_bq27541_get_prev_battery_mvolts_2cell_min(void)
{
	return 4000;
}
*/
static int fg_bq28z610_modify_dod0(void)
{
	return 0;
}

static int fg_bq28z610_update_soc_smooth_parameter(void)
{
	return 0;
}

static int fg_bq28z610_get_battery_balancing_status(void)
{
	return 0;
}

static struct oplus_gauge_operations battery_gauge_ops = {
	.get_battery_mvolts = fg_bq27541_get_battery_mvolts,
	.get_battery_temperature = fg_bq27541_get_battery_temperature,
	.get_batt_remaining_capacity = fg_bq27541_get_batt_remaining_capacity,
	.get_battery_soc = fg_bq27541_get_battery_soc,
	.get_average_current = fg_bq27541_get_average_current,
	.get_battery_fcc = fg_bq27541_get_battery_fcc,
	.get_battery_cc = fg_bq27541_get_battery_cc,
	.get_battery_soh = fg_bq27541_get_battery_soh,
	.get_battery_authenticate = fg_bq27541_get_battery_authenticate,
	.set_battery_full = fg_bq27541_set_battery_full,
	.get_prev_battery_mvolts = fg_bq27541_get_battery_mvolts,
	.get_prev_battery_temperature = fg_bq27541_get_battery_temperature,
	.get_prev_battery_soc = fg_bq27541_get_battery_soc,
	.get_prev_average_current = fg_bq27541_get_average_current,
	.get_prev_batt_remaining_capacity = fg_bq27541_get_batt_remaining_capacity,
	.get_battery_mvolts_2cell_max = fg_bq27541_get_battery_mvolts_2cell_max,
	.get_battery_mvolts_2cell_min = fg_bq27541_get_battery_mvolts_2cell_min,
	.get_prev_battery_mvolts_2cell_max = fg_bq27541_get_battery_mvolts_2cell_max,
	.get_prev_battery_mvolts_2cell_min = fg_bq27541_get_battery_mvolts_2cell_min,
	.update_battery_dod0 = fg_bq28z610_modify_dod0,
	.update_soc_smooth_parameter = fg_bq28z610_update_soc_smooth_parameter,
	.get_battery_cb_status = fg_bq28z610_get_battery_balancing_status,
};
#endif /* VENDOR_EDIT */

static int battery_chg_probe(struct platform_device *pdev)
{
#ifdef VENDOR_EDIT
	struct oplus_gauge_chip *gauge_chip;
	struct oplus_chg_chip *oplus_chip;
#endif
	struct battery_chg_dev *bcdev;
	struct device *dev = &pdev->dev;
	struct pmic_glink_client_data client_data = { };
	int rc, i;

#ifdef VENDOR_EDIT
	pr_info("battery_chg_probe start...\n");
	gauge_chip = devm_kzalloc(&pdev->dev, sizeof(*gauge_chip), GFP_KERNEL);
	if (!gauge_chip) {
		pr_err("oplus_gauge_chip devm_kzalloc failed.\n");
		return -ENOMEM;
	}
	gauge_chip->gauge_ops = &battery_gauge_ops;
	oplus_gauge_init(gauge_chip);

	oplus_chip = devm_kzalloc(&pdev->dev, sizeof(*oplus_chip), GFP_KERNEL);
	if (!oplus_chip) {
		pr_err("oplus_chg_chip devm_kzalloc failed.\n");
		return -ENOMEM;
	}

	oplus_chip->dev = &pdev->dev;
	rc = oplus_chg_parse_swarp_dt(oplus_chip);

	if (oplus_gauge_check_chip_is_null()) {
		chg_err("gauge chip null, will do after bettery init.\n");
		return -EPROBE_DEFER;
	}
	oplus_chip->chg_ops = &battery_chg_ops;

	g_oplus_chip = oplus_chip;
#endif /*VENDOR_EDIT*/

	bcdev = devm_kzalloc(&pdev->dev, sizeof(*bcdev), GFP_KERNEL);
	if (!bcdev)
		return -ENOMEM;

#ifdef VENDOR_EDIT
	oplus_chip->pmic_spmi.bcdev_chip = bcdev;
#endif

	bcdev->psy_list[PSY_TYPE_BATTERY].map = battery_prop_map;
	bcdev->psy_list[PSY_TYPE_BATTERY].prop_count = BATT_PROP_MAX;
	bcdev->psy_list[PSY_TYPE_BATTERY].opcode_get = BC_BATTERY_STATUS_GET;
	bcdev->psy_list[PSY_TYPE_BATTERY].opcode_set = BC_BATTERY_STATUS_SET;
	bcdev->psy_list[PSY_TYPE_USB].map = usb_prop_map;
	bcdev->psy_list[PSY_TYPE_USB].prop_count = USB_PROP_MAX;
	bcdev->psy_list[PSY_TYPE_USB].opcode_get = BC_USB_STATUS_GET;
	bcdev->psy_list[PSY_TYPE_USB].opcode_set = BC_USB_STATUS_SET;
	bcdev->psy_list[PSY_TYPE_WLS].map = wls_prop_map;
	bcdev->psy_list[PSY_TYPE_WLS].prop_count = WLS_PROP_MAX;
	bcdev->psy_list[PSY_TYPE_WLS].opcode_get = BC_WLS_STATUS_GET;
	bcdev->psy_list[PSY_TYPE_WLS].opcode_set = BC_WLS_STATUS_SET;

	for (i = 0; i < PSY_TYPE_MAX; i++) {
		bcdev->psy_list[i].prop =
			devm_kcalloc(&pdev->dev, bcdev->psy_list[i].prop_count,
					sizeof(u32), GFP_KERNEL);
		if (!bcdev->psy_list[i].prop)
			return -ENOMEM;
	}

	bcdev->psy_list[PSY_TYPE_BATTERY].model =
		devm_kzalloc(&pdev->dev, MAX_STR_LEN, GFP_KERNEL);
	if (!bcdev->psy_list[PSY_TYPE_BATTERY].model)
		return -ENOMEM;

	mutex_init(&bcdev->rw_lock);
	init_completion(&bcdev->ack);
	init_completion(&bcdev->fw_buf_ack);
	init_completion(&bcdev->fw_update_ack);
	INIT_WORK(&bcdev->subsys_up_work, battery_chg_subsys_up_work);
	atomic_set(&bcdev->state, PMIC_GLINK_STATE_UP);
	bcdev->dev = dev;

	client_data.id = MSG_OWNER_BC;
	client_data.name = "battery_charger";
	client_data.msg_cb = battery_chg_callback;
	client_data.priv = bcdev;
	client_data.state_cb = battery_chg_state_cb;

	bcdev->client = pmic_glink_register_client(dev, &client_data);
	if (IS_ERR(bcdev->client)) {
		rc = PTR_ERR(bcdev->client);
		if (rc != -EPROBE_DEFER)
			dev_err(dev, "Error in registering with pmic_glink %d\n",
				rc);
		return rc;
	}

	bcdev->reboot_notifier.notifier_call = battery_chg_ship_mode;
	bcdev->reboot_notifier.priority = 255;
	register_reboot_notifier(&bcdev->reboot_notifier);

	rc = battery_chg_parse_dt(bcdev);
	if (rc < 0)
		goto error;

	platform_set_drvdata(pdev, bcdev);
	bcdev->fake_soc = -EINVAL;
	rc = battery_chg_init_psy(bcdev);
	if (rc < 0)
		goto error;

	bcdev->battery_class.name = "qcom-battery";
	bcdev->battery_class.class_groups = battery_class_groups;
	rc = class_register(&bcdev->battery_class);
	if (rc < 0) {
		pr_err("Failed to create battery_class rc=%d\n", rc);
		goto error;
	}

#ifdef VENDOR_EDIT
	//oplus_chg_parse_custom_dt(oplus_chip);
	oplus_chg_parse_charger_dt(oplus_chip);
	oplus_chg_init(oplus_chip);
#if 0
	main_psy = power_supply_get_by_name("main");
	if (main_psy) {
		pval.intval = 1000 * oplus_chg_get_fv(oplus_chip);
		power_supply_set_property(main_psy,
				POWER_SUPPLY_PROP_VOLTAGE_MAX,
				&pval);
		pval.intval = 1000 * oplus_chg_get_charging_current(oplus_chip);
		power_supply_set_property(main_psy,
				POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
				&pval);
	}
#endif
	oplus_chg_wake_update_work();
#endif /*VENDOR_EDIT*/

	battery_chg_add_debugfs(bcdev);
	battery_chg_notify_enable(bcdev);
	device_init_wakeup(bcdev->dev, true);

#ifdef VENDOR_EDIT
	pr_info("battery_chg_probe end...\n");
#endif

	return 0;
error:
	pmic_glink_unregister_client(bcdev->client);
	unregister_reboot_notifier(&bcdev->reboot_notifier);
	return rc;
}

static int battery_chg_remove(struct platform_device *pdev)
{
	struct battery_chg_dev *bcdev = platform_get_drvdata(pdev);
	int rc;

	device_init_wakeup(bcdev->dev, false);
	debugfs_remove_recursive(bcdev->debugfs_dir);
	class_unregister(&bcdev->battery_class);
	unregister_reboot_notifier(&bcdev->reboot_notifier);
	rc = pmic_glink_unregister_client(bcdev->client);
	if (rc < 0) {
		pr_err("Error unregistering from pmic_glink, rc=%d\n", rc);
		return rc;
	}

	return 0;
}

#ifdef VENDOR_EDIT
static void smbchg_enter_shipmode(struct oplus_chg_chip *chip)
{
	int rc = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct psy_state *pst = NULL;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	rc = write_property_id(bcdev, pst, BATT_SET_SHIP_MODE, 1);
	if (rc) {
		chg_err("set ship mode fail, rc=%d\n", rc);
		return;
	}

	chg_debug("power off after 15s\n");
}

static void battery_chg_shutdown(struct platform_device *pdev)
{
	if (g_oplus_chip && g_oplus_chip->enable_shipmode) {
		smbchg_enter_shipmode(g_oplus_chip);
		msleep(1000);
	}
}
#endif /* VENDOR_EDIT */

static const struct of_device_id battery_chg_match_table[] = {
	{ .compatible = "qcom,battery-charger" },
	{},
};

static struct platform_driver battery_chg_driver = {
	.driver = {
		.name = "qti_battery_charger",
		.of_match_table = battery_chg_match_table,
	},
	.probe = battery_chg_probe,
	.remove = battery_chg_remove,
#ifdef VENDOR_EDIT
	.shutdown = battery_chg_shutdown,
#endif
};
module_platform_driver(battery_chg_driver);

MODULE_DESCRIPTION("QTI Glink battery charger driver");
MODULE_LICENSE("GPL v2");
