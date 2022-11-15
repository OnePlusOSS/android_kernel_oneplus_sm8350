#define pr_fmt(fmt) "OPLUS_CHG[GKI]: %s[%d]: " fmt, __func__, __LINE__

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/of_platform.h>
#include <linux/power_supply.h>
#ifdef CONFIG_OPLUS_CHG_OOS
#include <linux/oem/oplus_chg.h>
#else
#include <linux/oplus_chg.h>
#endif
#include "oplus_chg_module.h"
#include "oplus_chg_wls.h"

struct oplus_gki_device {
	struct device *dev;
	struct power_supply *usb_psy;
	struct power_supply *batt_psy;
	struct power_supply *wls_psy;
	struct oplus_chg_mod *usb_ocm;
	struct oplus_chg_mod *wls_ocm;
	struct oplus_chg_mod *batt_ocm;
	bool support_wls;

	struct delayed_work status_keep_clean_work;
	struct wakeup_source *status_wake_lock;
	bool status_wake_lock_on;

	int pre_batt_status;
	int pre_wls_online;
};

static bool is_usb_ocm_available(struct oplus_gki_device *dev)
{
	dev->usb_ocm = oplus_chg_mod_get_by_name("usb");
	return !!dev->usb_ocm;
}

static bool is_wls_ocm_available(struct oplus_gki_device *dev)
{
	dev->wls_ocm = oplus_chg_mod_get_by_name("wireless");
	return !!dev->wls_ocm;
}

static bool is_batt_ocm_available(struct oplus_gki_device *dev)
{
	dev->batt_ocm = oplus_chg_mod_get_by_name("battery");
	return !!dev->batt_ocm;
}

static void oplus_chg_wls_status_keep_clean_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_gki_device *ogdev =
		container_of(dwork, struct oplus_gki_device, status_keep_clean_work);
	union oplus_chg_mod_propval temp_val = {0};
	int rc;

	if (!is_wls_ocm_available(ogdev)) {
		pr_err("wireless mod not found\n");
	} else {
		rc = oplus_chg_mod_get_property(
			ogdev->wls_ocm, OPLUS_CHG_PROP_STATUS_KEEP, &temp_val);
		if (rc < 0)
			temp_val.intval = WLS_SK_BY_KERNEL;
		if (temp_val.intval == WLS_SK_BY_HAL) {
			temp_val.intval = WLS_SK_WAIT_TIMEOUT;
			oplus_chg_mod_set_property(
				ogdev->wls_ocm, OPLUS_CHG_PROP_STATUS_KEEP, &temp_val);
			schedule_delayed_work(&ogdev->status_keep_clean_work, msecs_to_jiffies(5000));
			return;
		}

		temp_val.intval = WLS_SK_NULL;
		oplus_chg_mod_set_property(
			ogdev->wls_ocm, OPLUS_CHG_PROP_STATUS_KEEP, &temp_val);
	}
	if (ogdev->status_wake_lock_on) {
		pr_info("release status_wake_lock\n");
		__pm_relax(ogdev->status_wake_lock);
		ogdev->status_wake_lock_on = false;
	}
}

static int wls_psy_get_prop(struct power_supply *psy,
		enum power_supply_property prop,
		union power_supply_propval *pval)
{
	struct oplus_gki_device *ogdev = power_supply_get_drvdata(psy);
	union oplus_chg_mod_propval temp_val = {0};
	int wls_status_keep = WLS_SK_NULL;
	int rc = 0;

	if (!is_wls_ocm_available(ogdev)) {
		pr_err("wireless mod not found\n");
		return -ENODEV;
	}

	switch (prop) {
	case POWER_SUPPLY_PROP_ONLINE:
		rc = oplus_chg_mod_get_property(ogdev->wls_ocm,
			OPLUS_CHG_PROP_STATUS_KEEP, &temp_val);
		if (rc == 0)
			wls_status_keep = temp_val.intval;

		rc = oplus_chg_mod_get_property(
			ogdev->wls_ocm, OPLUS_CHG_PROP_ONLINE_KEEP,
			&temp_val);
		if (rc < 0)
			temp_val.intval = 0;
		rc = oplus_chg_mod_get_property(
			ogdev->wls_ocm, OPLUS_CHG_PROP_PRESENT,
			(union oplus_chg_mod_propval *)pval);
		if (!rc && !!temp_val.intval)
			pval->intval = 1;

		if (wls_status_keep != WLS_SK_NULL) {
			pval->intval = 1;
		} else {
			if (ogdev->pre_wls_online && pval->intval == 0) {
				if (!ogdev->status_wake_lock_on) {
					pr_info("acquire status_wake_lock\n");
					__pm_stay_awake(ogdev->status_wake_lock);
					ogdev->status_wake_lock_on = true;
				}
				ogdev->pre_wls_online = pval->intval;
				temp_val.intval = WLS_SK_BY_KERNEL;
				oplus_chg_mod_set_property(ogdev->wls_ocm,
					OPLUS_CHG_PROP_STATUS_KEEP, &temp_val);
				pval->intval = 1;
				schedule_delayed_work(&ogdev->status_keep_clean_work, msecs_to_jiffies(1000));
			} else {
				ogdev->pre_wls_online = pval->intval;
				if (ogdev->status_wake_lock_on) {
					cancel_delayed_work_sync(&ogdev->status_keep_clean_work);
					schedule_delayed_work(&ogdev->status_keep_clean_work, 0);
				}
			}
		}
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		rc = oplus_chg_mod_get_property(ogdev->wls_ocm,
			OPLUS_CHG_PROP_VOLTAGE_NOW,
			(union oplus_chg_mod_propval *)pval);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		rc = oplus_chg_mod_get_property(ogdev->wls_ocm,
			OPLUS_CHG_PROP_VOLTAGE_MAX,
			(union oplus_chg_mod_propval *)pval);
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		rc = oplus_chg_mod_get_property(ogdev->wls_ocm,
			OPLUS_CHG_PROP_CURRENT_NOW,
			(union oplus_chg_mod_propval *)pval);
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		rc = oplus_chg_mod_get_property(ogdev->wls_ocm,
			OPLUS_CHG_PROP_CURRENT_MAX,
			(union oplus_chg_mod_propval *)pval);
		break;
#ifndef CONFIG_OPLUS_CHG_OOS
	case POWER_SUPPLY_PROP_PRESENT:
		rc = oplus_chg_mod_get_property(ogdev->wls_ocm,
			OPLUS_CHG_PROP_PRESENT,
			(union oplus_chg_mod_propval *)pval);
		break;
#endif
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
#ifndef CONFIG_OPLUS_CHG_OOS
	POWER_SUPPLY_PROP_PRESENT,
#endif
};

static const struct power_supply_desc wls_psy_desc = {
	.name			= "wireless",
	.type			= POWER_SUPPLY_TYPE_WIRELESS,
	.properties		= wls_props,
	.num_properties		= ARRAY_SIZE(wls_props),
	.get_property		= wls_psy_get_prop,
	.set_property		= wls_psy_set_prop,
	.property_is_writeable	= wls_psy_prop_is_writeable,
};

static int usb_psy_get_prop(struct power_supply *psy,
		enum power_supply_property prop,
		union power_supply_propval *pval)
{
	struct oplus_gki_device *ogdev = power_supply_get_drvdata(psy);
	int rc = 0;

	if (!is_usb_ocm_available(ogdev)) {
		pr_err("usb mod not found\n");
		return -ENODEV;
	}

	switch (prop) {
	case POWER_SUPPLY_PROP_ONLINE:
		rc = oplus_chg_mod_get_property(ogdev->usb_ocm,
			OPLUS_CHG_PROP_PRESENT,
			(union oplus_chg_mod_propval *)pval);
		if (pval->intval == 0) {
			rc = oplus_chg_mod_get_property(ogdev->usb_ocm,
				OPLUS_CHG_PROP_ONLINE,
				(union oplus_chg_mod_propval *)pval);
		}
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		rc = oplus_chg_mod_get_property(ogdev->usb_ocm,
			OPLUS_CHG_PROP_VOLTAGE_NOW,
			(union oplus_chg_mod_propval *)pval);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		rc = oplus_chg_mod_get_property(ogdev->usb_ocm,
			OPLUS_CHG_PROP_VOLTAGE_MAX,
			(union oplus_chg_mod_propval *)pval);
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		rc = oplus_chg_mod_get_property(ogdev->usb_ocm,
			OPLUS_CHG_PROP_CURRENT_NOW,
			(union oplus_chg_mod_propval *)pval);
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		rc = oplus_chg_mod_get_property(ogdev->usb_ocm,
			OPLUS_CHG_PROP_CURRENT_MAX,
			(union oplus_chg_mod_propval *)pval);
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		rc = oplus_chg_mod_get_property(ogdev->usb_ocm,
			OPLUS_CHG_PROP_CURRENT_MAX,
			(union oplus_chg_mod_propval *)pval);
		break;
	case POWER_SUPPLY_PROP_USB_TYPE:
		rc = oplus_chg_mod_get_property(ogdev->usb_ocm,
			OPLUS_CHG_PROP_USB_TYPE,
			(union oplus_chg_mod_propval *)pval);
		if (rc == 0)
			pval->intval = ocm_to_psy_usb_type[pval->intval];
		break;
	case POWER_SUPPLY_PROP_TEMP:
		rc = oplus_chg_mod_get_property(ogdev->usb_ocm,
			OPLUS_CHG_PROP_CON_TEMP1,
			(union oplus_chg_mod_propval *)pval);
		break;
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

static int usb_psy_set_prop(struct power_supply *psy,
		enum power_supply_property prop,
		const union power_supply_propval *pval)
{
	struct oplus_gki_device *ogdev = power_supply_get_drvdata(psy);
	int rc = 0;

	if (!is_usb_ocm_available(ogdev)) {
		pr_err("usb mod not found\n");
		return -ENODEV;
	}

	switch (prop) {
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		break;
		rc = oplus_chg_mod_set_property(ogdev->usb_ocm,
			OPLUS_CHG_PROP_CURRENT_MAX,
			(union oplus_chg_mod_propval *)pval);
		break;
	default:
		pr_err("set prop %d is not supported\n", prop);
		return -EINVAL;
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
	POWER_SUPPLY_PROP_TEMP,
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

static struct power_supply_desc usb_psy_desc = {
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

void oplus_chg_gki_set_usb_type(enum power_supply_type type)
{
	usb_psy_desc.type = type;
}

static int battery_psy_get_prop(struct power_supply *psy,
		enum power_supply_property prop,
		union power_supply_propval *pval)
{
	struct oplus_gki_device *ogdev = power_supply_get_drvdata(psy);
	union oplus_chg_mod_propval temp_val = {0};
	bool wls_online = false;
	bool wls_status_keep = false;
	int rc = 0;

	if (!is_batt_ocm_available(ogdev)) {
		pr_err("batt mod not found\n");
		return -ENODEV;
	}

	switch (prop) {
	case POWER_SUPPLY_PROP_STATUS:
		if (is_wls_ocm_available(ogdev)) {
			rc = oplus_chg_mod_get_property(ogdev->wls_ocm,
				OPLUS_CHG_PROP_ONLINE_KEEP, &temp_val);
			if (rc == 0)
				wls_online = temp_val.intval;
			temp_val.intval = 0;
			rc = oplus_chg_mod_get_property(ogdev->wls_ocm,
				OPLUS_CHG_PROP_PRESENT, &temp_val);
			if (rc == 0)
				wls_online |= !!temp_val.intval;
			rc = oplus_chg_mod_get_property(ogdev->wls_ocm,
				OPLUS_CHG_PROP_STATUS_KEEP, &temp_val);
			if (rc == 0)
				wls_status_keep = !!temp_val.intval;
		}
		if (wls_status_keep) {
			pval->intval = ogdev->pre_batt_status;
		} else {
			rc = oplus_chg_mod_get_property(ogdev->batt_ocm,
				OPLUS_CHG_PROP_STATUS,
				(union oplus_chg_mod_propval *)pval);
			if (rc == 0)
				pval->intval = ocm_to_psy_status[pval->intval];
			if (wls_online)
				ogdev->pre_batt_status = pval->intval;
		}
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		rc = oplus_chg_mod_get_property(ogdev->batt_ocm,
			OPLUS_CHG_PROP_HEALTH,
			(union oplus_chg_mod_propval *)pval);
		if (rc == 0)
			pval->intval = ocm_to_psy_health[pval->intval];
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		rc = oplus_chg_mod_get_property(ogdev->batt_ocm,
			OPLUS_CHG_PROP_PRESENT,
			(union oplus_chg_mod_propval *)pval);
		break;
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		rc = oplus_chg_mod_get_property(ogdev->batt_ocm,
			OPLUS_CHG_PROP_CHARGE_TYPE,
			(union oplus_chg_mod_propval *)pval);
		if (rc == 0)
			pval->intval = ocm_to_psy_charge_type[pval->intval];
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		rc = oplus_chg_mod_get_property(ogdev->batt_ocm,
			OPLUS_CHG_PROP_CAPACITY,
			(union oplus_chg_mod_propval *)pval);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_OCV:
		rc = oplus_chg_mod_get_property(ogdev->batt_ocm,
			OPLUS_CHG_PROP_VOLTAGE_OCV,
			(union oplus_chg_mod_propval *)pval);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		rc = oplus_chg_mod_get_property(ogdev->batt_ocm,
			OPLUS_CHG_PROP_VOLTAGE_NOW,
			(union oplus_chg_mod_propval *)pval);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		rc = oplus_chg_mod_get_property(ogdev->batt_ocm,
			OPLUS_CHG_PROP_VOLTAGE_MAX,
			(union oplus_chg_mod_propval *)pval);
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		rc = oplus_chg_mod_get_property(ogdev->batt_ocm,
			OPLUS_CHG_PROP_CURRENT_NOW,
			(union oplus_chg_mod_propval *)pval);
		break;
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
		rc = oplus_chg_mod_get_property(ogdev->batt_ocm,
			OPLUS_CHG_PROP_CHARGE_CONTROL_LIMIT,
			(union oplus_chg_mod_propval *)pval);
		break;
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT_MAX:
		rc = oplus_chg_mod_get_property(ogdev->batt_ocm,
			OPLUS_CHG_PROP_CHARGE_CONTROL_LIMIT_MAX,
			(union oplus_chg_mod_propval *)pval);
		break;
	case POWER_SUPPLY_PROP_TEMP:
		rc = oplus_chg_mod_get_property(ogdev->batt_ocm,
			OPLUS_CHG_PROP_TEMP,
			(union oplus_chg_mod_propval *)pval);
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		rc = oplus_chg_mod_get_property(ogdev->batt_ocm,
			OPLUS_CHG_PROP_TECHNOLOGY,
			(union oplus_chg_mod_propval *)pval);
		if (rc == 0)
			pval->intval = ocm_to_psy_technology[pval->intval];
		break;
	case POWER_SUPPLY_PROP_CHARGE_COUNTER:
		rc = oplus_chg_mod_get_property(ogdev->batt_ocm,
			OPLUS_CHG_PROP_CHARGE_COUNTER,
			(union oplus_chg_mod_propval *)pval);
		break;
	case POWER_SUPPLY_PROP_CYCLE_COUNT:
		rc = oplus_chg_mod_get_property(ogdev->batt_ocm,
			OPLUS_CHG_PROP_CYCLE_COUNT,
			(union oplus_chg_mod_propval *)pval);
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
		rc = oplus_chg_mod_get_property(ogdev->batt_ocm,
			OPLUS_CHG_PROP_CHARGE_FULL_DESIGN,
			(union oplus_chg_mod_propval *)pval);
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL:
		rc = oplus_chg_mod_get_property(ogdev->batt_ocm,
			OPLUS_CHG_PROP_CHARGE_FULL,
			(union oplus_chg_mod_propval *)pval);
		break;
	case POWER_SUPPLY_PROP_MODEL_NAME:
		rc = oplus_chg_mod_get_property(ogdev->batt_ocm,
			OPLUS_CHG_PROP_MODEL_NAME,
			(union oplus_chg_mod_propval *)pval);
		break;
	case POWER_SUPPLY_PROP_TIME_TO_FULL_AVG:
		rc = oplus_chg_mod_get_property(ogdev->batt_ocm,
			OPLUS_CHG_PROP_TIME_TO_FULL_AVG,
			(union oplus_chg_mod_propval *)pval);
		break;
	case POWER_SUPPLY_PROP_TIME_TO_FULL_NOW:
		rc = oplus_chg_mod_get_property(ogdev->batt_ocm,
			OPLUS_CHG_PROP_TIME_TO_FULL_AVG,
			(union oplus_chg_mod_propval *)pval);
		break;
	case POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG:
		rc = oplus_chg_mod_get_property(ogdev->batt_ocm,
			OPLUS_CHG_PROP_TIME_TO_EMPTY_AVG,
			(union oplus_chg_mod_propval *)pval);
		break;
	case POWER_SUPPLY_PROP_POWER_NOW:
		rc = oplus_chg_mod_get_property(ogdev->batt_ocm,
			OPLUS_CHG_PROP_POWER_NOW,
			(union oplus_chg_mod_propval *)pval);
		break;
	case POWER_SUPPLY_PROP_POWER_AVG:
		rc = oplus_chg_mod_get_property(ogdev->batt_ocm,
			OPLUS_CHG_PROP_POWER_AVG,
			(union oplus_chg_mod_propval *)pval);
		break;
#ifndef CONFIG_OPLUS_CHG_OOS
	case POWER_SUPPLY_PROP_CHARGE_NOW:
		rc = oplus_chg_mod_get_property(ogdev->batt_ocm,
			OPLUS_CHG_PROP_CHARGE_NOW,
			(union oplus_chg_mod_propval *)pval);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN:
		rc = oplus_chg_mod_get_property(ogdev->batt_ocm,
			OPLUS_CHG_PROP_VOLTAGE_MIN,
			(union oplus_chg_mod_propval *)pval);
		break;
#endif
	default:
		pr_err("get prop %d is not supported\n", prop);
		return -EINVAL;
	}
	if (rc < 0) {
		pr_err("Couldn't get prop %d rc = %d\n", prop, rc);
		return -ENODATA;
	}

	return rc;
}

static int battery_psy_set_prop(struct power_supply *psy,
		enum power_supply_property prop,
		const union power_supply_propval *pval)
{
	struct oplus_gki_device *ogdev = power_supply_get_drvdata(psy);
	int rc = 0;

	if (!is_batt_ocm_available(ogdev)) {
		pr_err("batt mod not found\n");
		return -ENODEV;
	}

	switch (prop) {
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
		rc = oplus_chg_mod_get_property(ogdev->batt_ocm,
			OPLUS_CHG_PROP_CHARGE_CONTROL_LIMIT,
			(union oplus_chg_mod_propval *)pval);
		break;
	default:
		pr_err("set prop %d is not supported\n", prop);
		return -EINVAL;
	}

	return rc;
}

static int battery_psy_prop_is_writeable(struct power_supply *psy,
		enum power_supply_property prop)
{
	switch (prop) {
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
		return 1;
	default:
		break;
	}

	return 0;
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
	POWER_SUPPLY_PROP_TIME_TO_FULL_NOW,
	POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG,
	POWER_SUPPLY_PROP_POWER_NOW,
	POWER_SUPPLY_PROP_POWER_AVG,
#ifndef CONFIG_OPLUS_CHG_OOS
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_MIN,
#endif
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

static int oplus_chg_gki_init_psy(struct oplus_gki_device *dev)
{
	struct power_supply_config psy_cfg = {};
	int rc;

	psy_cfg.drv_data = dev;
	psy_cfg.of_node = dev->dev->of_node;
	dev->batt_psy = devm_power_supply_register(dev->dev,
			&batt_psy_desc, &psy_cfg);
	if (IS_ERR(dev->batt_psy)) {
		rc = PTR_ERR(dev->batt_psy);
		pr_err("Failed to register battery power supply, rc=%d\n", rc);
		return rc;
	}

	dev->usb_psy = devm_power_supply_register(dev->dev,
			&usb_psy_desc, &psy_cfg);
	if (IS_ERR(dev->usb_psy)) {
		rc = PTR_ERR(dev->usb_psy);
		pr_err("Failed to register USB power supply, rc=%d\n", rc);
		return rc;
	}

	if (dev->support_wls) {
		dev->wls_psy = devm_power_supply_register(dev->dev,
				&wls_psy_desc, &psy_cfg);
		if (IS_ERR(dev->wls_psy)) {
			rc = PTR_ERR(dev->wls_psy);
			pr_err("Failed to register wireless power supply, rc=%d\n", rc);
			return rc;
		}
	}

	return 0;
}

static int oplus_chg_gki_probe(struct platform_device *pdev)
{
	struct oplus_gki_device *gki_dev;
	struct device_node *node = pdev->dev.of_node;
	int rc;

	gki_dev = devm_kzalloc(&pdev->dev, sizeof(struct oplus_gki_device), GFP_KERNEL);
	if (gki_dev == NULL) {
		pr_err("alloc memory error\n");
		return -ENOMEM;
	}

	gki_dev->dev = &pdev->dev;
	platform_set_drvdata(pdev, gki_dev);

	gki_dev->support_wls = of_property_read_bool(node, "oplus,support_wls");

	rc = oplus_chg_gki_init_psy(gki_dev);
	if (rc < 0) {
		pr_err("oplus chg gki psy init error, rc=%d\n", rc);
		return rc;
	}

	INIT_DELAYED_WORK(&gki_dev->status_keep_clean_work, oplus_chg_wls_status_keep_clean_work);
	gki_dev->status_wake_lock = wakeup_source_register(gki_dev->dev, "status_wake_lock");
	gki_dev->status_wake_lock_on = false;

	return rc;
}

static int oplus_chg_gki_remove(struct platform_device *pdev)
{
	// struct oplus_gki_device *gki_dev = platform_get_drvdata(pdev);

	platform_set_drvdata(pdev, NULL);
	return 0;
}

static const struct of_device_id oplus_chg_gki_match[] = {
	{ .compatible = "oplus_chg_gki" },
	{},
};

static struct platform_driver oplus_chg_gki = {
	.driver = {
		.name = "oplus-chg-gki",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(oplus_chg_gki_match),
	},
	.probe = oplus_chg_gki_probe,
	.remove = oplus_chg_gki_remove,
};

static __init int oplus_chg_gki_init(void)
{
	return platform_driver_register(&oplus_chg_gki);
}

static __exit void oplus_chg_gki_exit(void)
{
	platform_driver_unregister(&oplus_chg_gki);
}

oplus_chg_module_register(oplus_chg_gki);
