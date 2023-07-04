// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */
#include <linux/delay.h>
#include <linux/power_supply.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/of_gpio.h>
#include <linux/kthread.h>
#include <linux/version.h>
#include <linux/reboot.h>
#include <linux/module.h>
#include <linux/sched/clock.h>
#ifdef CONFIG_OPLUS_CHG_OOS
#include <linux/oem/project_info.h>
#else
#include <soc/oplus/system/oplus_project.h>
#endif

#ifdef CONFIG_OPLUS_CHARGER_MTK

//#include <mtk_boot_common.h>
#include <mt-plat/mtk_boot.h>
#include <linux/gpio.h>
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0))
#include <uapi/linux/sched/types.h>
#endif
#else /* CONFIG_OPLUS_CHARGER_MTK */
#include <linux/spinlock.h>
#include <linux/gpio.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/of.h>

#include <linux/bitops.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#include <linux/spmi.h>
#include <linux/printk.h>
#include <linux/ratelimit.h>
#include <linux/debugfs.h>
#include <linux/leds.h>
#include <linux/rtc.h>
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 14, 0))
#include <linux/qpnp/qpnp-adc.h>
#else
#include <uapi/linux/sched/types.h>
#endif
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))
#include <linux/batterydata-lib.h>
#include <linux/of_batterydata.h>
#endif
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 14, 0))
#include <linux/msm_bcl.h>
#endif
#include <linux/ktime.h>
#include <linux/kernel.h>
#endif

#include "oplus_charger.h"
#include "oplus_gauge.h"
#include "oplus_warp.h"
#include "oplus_short.h"
#include "oplus_adapter.h"
#include "charger_ic/oplus_short_ic.h"
#include "oplus_debug_info.h"
#ifndef CONFIG_OPLUS_CHARGER_MTK
#ifndef OPLUS_CHG_OP_DEF
#ifndef WPC_NEW_INTERFACE
#include "oplus_wireless.h"
#include "wireless_ic/oplus_chargepump.h"	//for WPC
#else
#include "oplus_wireless.h"
#endif
#endif /* OPLUS_CHG_OP_DEF */
#endif /* CONFIG_OPLUS_CHARGER_MTK */
static struct oplus_chg_chip *g_charger_chip = NULL;

#define MAX_UI_DECIMAL_TIME 24
#define UPDATE_TIME 1

#define OPLUS_CHG_UPDATE_INTERVAL_SEC 			5
#ifdef OPLUS_CHG_OP_DEF
#define OPLUS_CHG_UPDATE_NO_CHARGE_INTERVAL_SEC 	10
#define VOLT_LOW_DIFF_VALUE 500
#define VOLT_HIGH_DIFF_VALUE 2000
#define VOLT_HIGH_VBUS_VALUE 4000
#define VOLT_LOW_VBUS_VALUE  1000
#endif
/* first run after init 10s */
#define OPLUS_CHG_UPDATE_INIT_DELAY	round_jiffies_relative(msecs_to_jiffies(500))
/* update cycle 5s */
#ifdef OPLUS_CHG_OP_DEF
#define OPLUS_CHG_UPDATE_INTERVAL(slow) \
	round_jiffies_relative(msecs_to_jiffies(\
		((slow) ? OPLUS_CHG_UPDATE_NO_CHARGE_INTERVAL_SEC :\
				OPLUS_CHG_UPDATE_INTERVAL_SEC) *\
		1000))
#else
#define OPLUS_CHG_UPDATE_INTERVAL	round_jiffies_relative(msecs_to_jiffies(OPLUS_CHG_UPDATE_INTERVAL_SEC*1000))
#endif

#define OPLUS_CHG_DEFAULT_CHARGING_CURRENT	512

int enable_charger_log = 2;
int charger_abnormal_log = 0;
int tbatt_pwroff_enable = 1;
extern bool oplus_is_power_off_charging(struct oplus_warp_chip *chip);

#define charger_xlog_printk(num, fmt, ...) \
		do { \
			if (enable_charger_log >= (int)num) { \
				printk(KERN_NOTICE pr_fmt("[OPLUS_CHG][%s]"fmt), __func__, ##__VA_ARGS__); \
			} \
		} while (0)

void oplus_chg_turn_off_charging(struct oplus_chg_chip *chip);
void oplus_chg_turn_on_charging(struct oplus_chg_chip *chip);

static void oplus_chg_smooth_to_soc(struct oplus_chg_chip *chip);
static void oplus_chg_variables_init(struct oplus_chg_chip *chip);
static void oplus_chg_update_work(struct work_struct *work);
static void oplus_chg_reset_adapter_work(struct work_struct *work);
static void oplus_chg_protection_check(struct oplus_chg_chip *chip);
static void oplus_chg_get_battery_data(struct oplus_chg_chip *chip);
static void oplus_chg_check_tbatt_status(struct oplus_chg_chip *chip);
static void oplus_chg_check_tbatt_normal_status(struct oplus_chg_chip *chip);
static void oplus_chg_get_chargerid_voltage(struct oplus_chg_chip *chip);
void oplus_chg_set_input_current_limit(struct oplus_chg_chip *chip);
static void oplus_chg_set_charging_current(struct oplus_chg_chip *chip);
static void oplus_chg_battery_update_status(struct oplus_chg_chip *chip);
static void oplus_chg_pdqc_to_normal(struct oplus_chg_chip *chip);
static void oplus_get_smooth_soc_switch(struct oplus_chg_chip *chip);
static void oplus_chg_pd_config(struct oplus_chg_chip *chip);
static void oplus_chg_qc_config(struct oplus_chg_chip *chip);
#ifndef CONFIG_OPLUS_CHG_OOS
// #ifdef  CONFIG_FB nick.hu
static int fb_notifier_callback(struct notifier_block *nb, unsigned long event, void *data);
// #endif
#endif /* OPLUS_CHG_OP_DEF */
void oplus_chg_ui_soc_decimal_init(void);
void oplus_chg_ui_soc_decimal_deinit(void);

static void oplus_chg_show_ui_soc_decimal(struct work_struct *work);


static int chgr_dbg_vchg = 0;
module_param(chgr_dbg_vchg, int, 0644);
MODULE_PARM_DESC(chgr_dbg_vchg, "debug charger voltage");

static int chgr_dbg_total_time = 0;
module_param(chgr_dbg_total_time, int, 0644);
MODULE_PARM_DESC(chgr_dbg_total_time, "debug charger total time");

/****************************************/
static int reset_mcu_delay = 0;
static bool suspend_charger = false;
static bool vbatt_higherthan_4180mv = false;
static bool vbatt_lowerthan_3300mv = false;

#ifndef CONFIG_OPLUS_CHG_GKI_SUPPORT
enum power_supply_property oplus_usb_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_OTG_SWITCH,
	POWER_SUPPLY_PROP_OTG_ONLINE,
};

enum power_supply_property oplus_ac_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
#ifdef CONFIG_OPLUS_FAST2NORMAL_CHG
	POWER_SUPPLY_PROP_FAST2NORMAL_CHG,
#endif
};

enum power_supply_property oplus_batt_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_MIN,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_AUTHENTICATE,
	POWER_SUPPLY_PROP_CHARGE_TIMEOUT,
	POWER_SUPPLY_PROP_CHARGE_TECHNOLOGY,
	POWER_SUPPLY_PROP_FAST_CHARGE,
	POWER_SUPPLY_PROP_MMI_CHARGING_ENABLE,	/*add for MMI_CHG_TEST*/
#ifdef CONFIG_OPLUS_CHARGER_MTK
	POWER_SUPPLY_PROP_STOP_CHARGING_ENABLE,
	POWER_SUPPLY_PROP_CHARGE_COUNTER,
	POWER_SUPPLY_PROP_CURRENT_MAX,
#endif
#ifndef CONFIG_OPLUS_SDM670_CHARGER
	POWER_SUPPLY_PROP_CHARGE_FULL,
#endif
	POWER_SUPPLY_PROP_BATTERY_FCC,
	POWER_SUPPLY_PROP_BATTERY_SOH,
	POWER_SUPPLY_PROP_BATTERY_CC,
	POWER_SUPPLY_PROP_BATTERY_RM,
	POWER_SUPPLY_PROP_BATTERY_NOTIFY_CODE,
#ifdef CONFIG_OPLUS_SMART_CHARGER_SUPPORT
	POWER_SUPPLY_PROP_COOL_DOWN,
#endif
	POWER_SUPPLY_PROP_ADAPTER_FW_UPDATE,
	POWER_SUPPLY_PROP_WARPCHG_ING,
#ifdef CONFIG_OPLUS_CHECK_CHARGERID_VOLT
	POWER_SUPPLY_PROP_CHARGERID_VOLT,
#endif
#ifdef CONFIG_OPLUS_SHIP_MODE_SUPPORT
	POWER_SUPPLY_PROP_SHIP_MODE,
#endif
#ifdef CONFIG_OPLUS_CALL_MODE_SUPPORT
	POWER_SUPPLY_PROP_CALL_MODE,
#endif
#ifdef CONFIG_OPLUS_SHORT_C_BATT_CHECK
#ifdef CONFIG_OPLUS_SHORT_USERSPACE
	POWER_SUPPLY_PROP_SHORT_C_LIMIT_CHG,
	POWER_SUPPLY_PROP_SHORT_C_LIMIT_RECHG,
	POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT,
	POWER_SUPPLY_PROP_INPUT_CURRENT_SETTLED,
#else
	POWER_SUPPLY_PROP_SHORT_C_BATT_UPDATE_CHANGE,
	POWER_SUPPLY_PROP_SHORT_C_BATT_IN_IDLE,
	POWER_SUPPLY_PROP_SHORT_C_BATT_CV_STATUS,
#endif /*CONFIG_OPLUS_SHORT_USERSPACE*/
#endif
#ifdef CONFIG_OPLUS_SHORT_HW_CHECK
	POWER_SUPPLY_PROP_SHORT_C_HW_FEATURE,
	POWER_SUPPLY_PROP_SHORT_C_HW_STATUS,
#endif
#ifdef CONFIG_OPLUS_SHORT_IC_CHECK
	POWER_SUPPLY_PROP_SHORT_C_IC_OTP_STATUS,
	POWER_SUPPLY_PROP_SHORT_C_IC_VOLT_THRESH,
	POWER_SUPPLY_PROP_SHORT_C_IC_OTP_VALUE,
#endif
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_TIME_TO_FULL_NOW,
};

#ifdef CONFIG_OPLUS_CHARGER_MTK
int oplus_usb_get_property(struct power_supply *psy,
		enum power_supply_property psp,
		union power_supply_propval *val)
		{
	int ret = 0;

	//struct oplus_chg_chip *chip = container_of(psy->desc, struct oplus_chg_chip, usb_psd);
	struct oplus_chg_chip *chip = g_charger_chip;

	if (chip->charger_exist) {
		if ((chip->charger_type == POWER_SUPPLY_TYPE_USB
				|| chip->charger_type == POWER_SUPPLY_TYPE_USB_CDP)
				&& chip->stop_chg == 1) {
			chip->usb_online = true;
			chip->usb_psd.type = POWER_SUPPLY_TYPE_USB;
		}
	} else {
		chip->usb_online = false;
	}

	switch (psp) {
		case POWER_SUPPLY_PROP_CURRENT_MAX:
			val->intval = 500000;
			break;
		case POWER_SUPPLY_PROP_VOLTAGE_MAX:
			val->intval = 5000000;
			break;
		case POWER_SUPPLY_PROP_ONLINE:
			val->intval = chip->usb_online;
			break;
		case POWER_SUPPLY_PROP_OTG_SWITCH:
			val->intval = chip->otg_switch;
			break;
		case POWER_SUPPLY_PROP_OTG_ONLINE:
			val->intval = chip->otg_online;
			break;
		default:
			pr_err("get prop %d is not supported in usb\n", psp);
			ret = -EINVAL;
			break;
	}
	return ret;
}

int oplus_usb_property_is_writeable(struct power_supply *psy,
		enum power_supply_property psp)
{
	int ret = 0;
	switch (psp) {
		case POWER_SUPPLY_PROP_OTG_SWITCH:
			return 1;
		default:
			pr_err("writeable prop %d is not supported in usb\n", psp);
			ret = -EINVAL;
			break;
	}
	return 0;
}

void __attribute__((weak)) oplus_set_otg_switch_status(bool value)
{
	return;
}

int oplus_usb_set_property(struct power_supply *psy,
		enum power_supply_property psp,
		const union power_supply_propval *val)
{
	int ret = 0;
	//struct oplus_chg_chip *chip = container_of(psy->desc, struct oplus_chg_chip, usb_psd);
	struct oplus_chg_chip *chip = g_charger_chip;

	switch (psp) {
		case POWER_SUPPLY_PROP_OTG_SWITCH:
			if (val->intval == 1) {
				chip->otg_switch = true;
				oplus_set_otg_switch_status(true);
			} else {
				chip->otg_switch = false;
				chip->otg_online = false;
				oplus_set_otg_switch_status(false);
			}
			charger_xlog_printk(CHG_LOG_CRTI, "otg_switch: %d\n", chip->otg_switch);
			break;
		default:
			pr_err("set prop %d is not supported in usb\n", psp);
			ret = -EINVAL;
			break;
	}
	return ret;
}

static void usb_update(struct oplus_chg_chip *chip)
{
	if (chip->charger_exist) {
		/*if (chip->charger_type==STANDARD_HOST || chip->charger_type==CHARGING_HOST) {*/
		if (chip->charger_type == POWER_SUPPLY_TYPE_USB
				|| chip->charger_type == POWER_SUPPLY_TYPE_USB_CDP) {
			chip->usb_online = true;
			chip->usb_psd.type = POWER_SUPPLY_TYPE_USB;
		}
	} else {
		chip->usb_online = false;
	}
	power_supply_changed(chip->usb_psy);
}
#endif

int oplus_ac_get_property(struct power_supply *psy,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	int ret = 0;
	//struct oplus_chg_chip *chip = container_of(psy->desc, struct oplus_chg_chip, ac_psd);
	struct oplus_chg_chip *chip = g_charger_chip;

	if (chip->charger_exist) {
		if ((chip->charger_type == POWER_SUPPLY_TYPE_USB_DCP || suspend_charger)
				|| (oplus_warp_get_fastchg_started() == true)
				|| (oplus_warp_get_fastchg_to_normal() == true)
				|| (oplus_warp_get_fastchg_to_warm() == true)
				|| (oplus_warp_get_fastchg_dummy_started() == true)
				|| (oplus_warp_get_adapter_update_status() == ADAPTER_FW_NEED_UPDATE)
				|| (oplus_warp_get_btb_temp_over() == true)) {
			chip->ac_online = true;
		} else {
			chip->ac_online = false;
		}
	} else {
		if ((oplus_warp_get_fastchg_started() == true)
				|| (oplus_warp_get_fastchg_to_normal() == true)
				|| (oplus_warp_get_fastchg_to_warm() == true)
				|| (oplus_warp_get_fastchg_dummy_started() == true)
				|| (oplus_warp_get_adapter_update_status() == ADAPTER_FW_NEED_UPDATE)
				|| (oplus_warp_get_btb_temp_over() == true)
				|| chip->mmi_fastchg == 0) {
			chip->ac_online = true;
		} else {
			chip->ac_online = false;

		}
	}
	switch (psp) {
		case POWER_SUPPLY_PROP_ONLINE:
			val->intval = chip->ac_online;
			break;
#ifdef CONFIG_OPLUS_FAST2NORMAL_CHG
		case POWER_SUPPLY_PROP_FAST2NORMAL_CHG:
			if (oplus_warp_get_fastchg_to_normal() == true
					|| oplus_warp_get_fastchg_to_warm() == true
					|| oplus_warp_get_btb_temp_over() == true
					|| oplus_warp_get_fastchg_low_temp_full() == true) {
				val->intval = 1;
			} else {
				val->intval = 0;
			}
			break;
#endif
		default:
			pr_err("get prop %d is not supported in ac\n", psp);
			ret = -EINVAL;
			break;
	}
	if (chip->ac_online) {
		charger_xlog_printk(CHG_LOG_CRTI, "chg_exist:%d, ac_online:%d\n",
				chip->charger_exist, chip->ac_online);
	}
	return ret;
}


int oplus_battery_property_is_writeable(struct power_supply *psy,
		enum power_supply_property psp)
{
	int rc = 0;

	switch (psp) {
		case POWER_SUPPLY_PROP_MMI_CHARGING_ENABLE:
			rc = 1;
			break;
#ifdef CONFIG_OPLUS_SMOOTH_SOC
		case POWER_SUPPLY_PROP_SMOOTH_SWITCH:
			rc = 1;
			break;
#endif
#ifdef CONFIG_OPLUS_SMART_CHARGER_SUPPORT
		case POWER_SUPPLY_PROP_COOL_DOWN:
			rc = 1;
			break;
		case POWER_SUPPLY_PROP_CURRENT_NOW:
			if (g_charger_chip && g_charger_chip->smart_charging_screenoff) {
				rc = 1;
			} else {
				rc = 0;
			}
			break;
#endif
#ifdef CONFIG_OPLUS_CHARGER_MTK
		case POWER_SUPPLY_PROP_STOP_CHARGING_ENABLE:
			rc = 1;
			break;
#endif
#ifdef CONFIG_OPLUS_SHIP_MODE_SUPPORT
		case POWER_SUPPLY_PROP_SHIP_MODE:
			rc = 1;
			break;
#endif
#ifdef CONFIG_OPLUS_CALL_MODE_SUPPORT
		case POWER_SUPPLY_PROP_CALL_MODE:
			rc = 1;
			break;
#endif
#ifdef CONFIG_OPLUS_SHORT_C_BATT_CHECK
#ifdef CONFIG_OPLUS_SHORT_USERSPACE
		case POWER_SUPPLY_PROP_SHORT_C_LIMIT_CHG:
		case POWER_SUPPLY_PROP_SHORT_C_LIMIT_RECHG:
#else
		case POWER_SUPPLY_PROP_SHORT_C_BATT_UPDATE_CHANGE:
		case POWER_SUPPLY_PROP_SHORT_C_BATT_IN_IDLE:
#endif /*CONFIG_OPLUS_SHORT_USERSPACE*/
			rc = 1;
			break;
#endif
#ifdef CONFIG_OPLUS_SHORT_HW_CHECK
		case POWER_SUPPLY_PROP_SHORT_C_HW_FEATURE:
			rc = 1;
			break;
#endif
#ifdef CONFIG_OPLUS_SHORT_IC_CHECK
		case POWER_SUPPLY_PROP_SHORT_C_IC_VOLT_THRESH:
			rc = 1;
			break;
#endif
#ifdef CONFIG_OPLUS_CHIP_SOC_NODE
		case POWER_SUPPLY_PROP_CHIP_SOC:
			rc = 1;
			break;
#endif
//		pr_err("writeable prop %d is not supported in batt\n", psp);
		default:
			rc = 0;
			break;
	}
	return rc;
}

int oplus_battery_set_property(struct power_supply *psy,
		enum power_supply_property psp,
		const union power_supply_propval *val)
{
	int ret = 0;
	//struct oplus_chg_chip *chip = container_of(psy->desc, struct oplus_chg_chip, battery_psd);
	struct oplus_chg_chip *chip = g_charger_chip;

	switch (psp) {
		case POWER_SUPPLY_PROP_MMI_CHARGING_ENABLE:
			charger_xlog_printk(CHG_LOG_CRTI, "set mmi_chg = [%d].\n", val->intval);
			if (val->intval == 0) {
				if(chip->unwakelock_chg == 1) {
					ret = -EINVAL;
					charger_xlog_printk(CHG_LOG_CRTI,
							"unwakelock testing , this test not allowed.\n");
				} else {
					chip->prop_status = POWER_SUPPLY_STATUS_NOT_CHARGING;
					chip->mmi_chg = 0;
					oplus_chg_turn_off_charging(chip);
					if (oplus_warp_get_fastchg_started() == true) {
						oplus_warp_turn_off_fastchg();
						chip->mmi_fastchg = 0;
					}
				}
			} else {
				if(chip->unwakelock_chg == 1) {
					ret = -EINVAL;
					charger_xlog_printk(CHG_LOG_CRTI,
							"unwakelock testing , this test not allowed.\n");
				} else {
					chip->mmi_chg = 1;
					chip->prop_status = POWER_SUPPLY_STATUS_CHARGING;
					if (chip->mmi_fastchg == 0) {
						oplus_chg_clear_chargerid_info();
					}
					chip->mmi_fastchg = 1;
					oplus_chg_turn_on_charging(chip);
				}
			}
			break;
#ifdef CONFIG_OPLUS_SMOOTH_SOC
		case POWER_SUPPLY_PROP_SMOOTH_SWITCH:
			chip->smooth_switch = val->intval;
			break;
#endif
#ifdef CONFIG_OPLUS_SMART_CHARGER_SUPPORT
		case POWER_SUPPLY_PROP_COOL_DOWN:
			oplus_smart_charge_by_cool_down(chip, val->intval);
			break;
		case POWER_SUPPLY_PROP_CURRENT_NOW:
			if (chip->smart_charging_screenoff) {
				oplus_smart_charge_by_shell_temp(chip, val->intval);
				break;
			} else {
				ret = -EINVAL;
				break;
			}
#endif
#ifdef CONFIG_OPLUS_CHARGER_MTK
		case POWER_SUPPLY_PROP_STOP_CHARGING_ENABLE:
			charger_xlog_printk(CHG_LOG_CRTI, "set stop_chg = [%d].\n", val->intval);
			if (val->intval == 0) {
				chip->stop_chg = 0;
			} else {
				chip->stop_chg = 1;
			}
		break;
#endif
#ifdef CONFIG_OPLUS_SHIP_MODE_SUPPORT
		case POWER_SUPPLY_PROP_SHIP_MODE:
			chip->enable_shipmode = val->intval;
			oplus_gauge_update_soc_smooth_parameter();
			break;
#endif
#ifdef CONFIG_OPLUS_CALL_MODE_SUPPORT
		case POWER_SUPPLY_PROP_CALL_MODE:
			chip->calling_on = val->intval;
			break;
#endif
#ifdef CONFIG_OPLUS_SHORT_C_BATT_CHECK
#ifdef CONFIG_OPLUS_SHORT_USERSPACE
		case POWER_SUPPLY_PROP_SHORT_C_LIMIT_CHG:
			printk(KERN_ERR "[OPLUS_CHG] [short_c_bat] set limit chg[%d]\n", !!val->intval);
			chip->short_c_batt.limit_chg = !!val->intval;
			//for userspace logic
			if (!!val->intval == 0){
				chip->short_c_batt.is_switch_on = 0;
			}
			break;
		case POWER_SUPPLY_PROP_SHORT_C_LIMIT_RECHG:
			printk(KERN_ERR "[OPLUS_CHG] [short_c_bat] set limit rechg[%d]\n", !!val->intval);
			chip->short_c_batt.limit_rechg = !!val->intval;
			break;
#else
		case POWER_SUPPLY_PROP_SHORT_C_BATT_UPDATE_CHANGE:
			printk(KERN_ERR "[OPLUS_CHG] [short_c_batt]: set update change[%d]\n", val->intval);
			oplus_short_c_batt_update_change(chip, val->intval);
			chip->short_c_batt.update_change = val->intval;
			break;

		case POWER_SUPPLY_PROP_SHORT_C_BATT_IN_IDLE:
			printk(KERN_ERR "[OPLUS_CHG] [short_c_batt]: set in idle[%d]\n", !!val->intval);
			chip->short_c_batt.in_idle = !!val->intval;
			break;
#endif /*CONFIG_OPLUS_SHORT_USERSPACE*/
#endif /* CONFIG_OPLUS_SHORT_C_BATT_CHECK */
#ifdef CONFIG_OPLUS_SHORT_HW_CHECK
		case POWER_SUPPLY_PROP_SHORT_C_HW_FEATURE:
			printk(KERN_ERR "[OPLUS_CHG] [short_c_hw_check]: set is_feature_hw_on [%d]\n", val->intval);
			chip->short_c_batt.is_feature_hw_on = val->intval;
			break;
#endif /* CONFIG_OPLUS_SHORT_C_BATT_CHECK */
#ifdef CONFIG_OPLUS_SHORT_IC_CHECK
		case POWER_SUPPLY_PROP_SHORT_C_IC_VOLT_THRESH:
			if (chip) {
				chip->short_c_batt.ic_volt_threshold = val->intval;
				oplus_short_ic_set_volt_threshold(chip);
				//pr_err("%s:[OPLUS_CHG][oplus_short_ic],ic_volt_threshold val->intval[%d]\n", __FUNCTION__, val->intval);
			}
			break;
#endif
		default:
			pr_err("set prop %d is not supported in batt\n", psp);
			ret = -EINVAL;
			break;
	}
	return ret;
}
#endif /* CONFIG_OPLUS_CHG_GKI_SUPPORT */


#define OPLUS_MIDAS_CHG_DEBUG 1
#ifdef OPLUS_MIDAS_CHG_DEBUG
#define	midas_debug(fmt, args...)	\
	pr_notice("[OPLUS_MIDAS_CHG_DEBUG]" fmt, ##args)
#else
#define	midas_debug(fmt, args...)
#endif /* OPLUS_MIDAS_CHG_DEBUG */

static struct oplus_midas_chg {
	int cali_passed_chg;
	int passed_chg;
	int accu_delta;

	int prev_chg_stat;	/* 1--charger-on, 0 -- otherwise */

	unsigned int reset_counts;
} midas_chg;

static void oplus_midas_chg_info(const char *name)
{
	midas_debug("%s: passedchg=%d, realpassedchg=%d,"
		"accu_delta=%d, prev_chg_stat=%d, reset_counts=%u\n",
		name, midas_chg.cali_passed_chg, midas_chg.passed_chg,
		midas_chg.accu_delta, midas_chg.prev_chg_stat, midas_chg.reset_counts);
}

/* TODO: how to determine passedchg is reset precisely ? */
#define	__abs(a, b) ((a > b) ? (a - b) : (b - a))
#define ZEROTH	5
#define COMSUMETH 10
static bool oplus_midas_passedchg_reset(int prev, int val)
{
	if(__abs(val, prev) > COMSUMETH) {
		return true;
	} else if (__abs(val, 0) > ZEROTH) {
		return false;
	}

	if(prev < 0 && val - prev >= ZEROTH) {
			return true;
	}
	if(prev >= 0 && val < prev) {
			return true;
	}
	return false;
}

static void oplus_midas_chg_data_init(void)
{
	int val, ret;
	midas_chg.accu_delta = 0;
	midas_chg.reset_counts = 0;
	if(oplus_warp_get_allow_reading() == true) {
		ret = oplus_gauge_get_passedchg(&val);
		if (ret) {
			pr_err("%s: get passedchg error %d\n", __FUNCTION__, val);
			midas_chg.cali_passed_chg = midas_chg.passed_chg = 0;
		} else {
			midas_chg.cali_passed_chg = midas_chg.passed_chg = val;
		}
	} else {
		pr_err("%s: not allow reading", __FUNCTION__);
	}
}

static void oplus_midas_chg_processing(struct oplus_chg_chip *chip)
{
	static int inited = 0;
	int val, ret = 0;

	if (!inited) {
		oplus_midas_chg_data_init();
		inited = 1;
		return;
	}

	if (chip->charger_exist) {
		midas_chg.prev_chg_stat = 1;
		oplus_midas_chg_info(__FUNCTION__);
		return;
	}

	if(oplus_warp_get_allow_reading() == true) {
		ret = oplus_gauge_get_passedchg(&val);
		if (ret) {
			pr_err("%s: get passedchg error %d\n", __FUNCTION__, val);
			return;
		}
	} else {
		pr_err("%s: not allow reading", __FUNCTION__);
	}
	if (midas_chg.prev_chg_stat) {
		/* re-init passedchg after charge */
		midas_chg.cali_passed_chg = midas_chg.passed_chg = val;
		midas_chg.accu_delta = 0;
		midas_chg.prev_chg_stat = 0;
	} else {
		if (oplus_midas_passedchg_reset(midas_chg.passed_chg, val)) {
			/* handling passedchg reset ... */
			midas_chg.cali_passed_chg += midas_chg.accu_delta;
			midas_chg.reset_counts++;
		} else {
			/* accumulate normally */
			midas_chg.accu_delta = val - midas_chg.passed_chg;
			midas_chg.cali_passed_chg += midas_chg.accu_delta;
		}
		midas_chg.passed_chg = val;
	}

	oplus_midas_chg_info(__FUNCTION__);
}
#ifndef CONFIG_OPLUS_CHG_GKI_SUPPORT
int oplus_battery_get_property(struct power_supply *psy,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	int ret = 0;
	//struct oplus_chg_chip *chip = container_of(psy->desc, struct oplus_chg_chip, battery_psd);
	struct oplus_chg_chip *chip = g_charger_chip;

	switch (psp) {
		case POWER_SUPPLY_PROP_STATUS:
			if (oplus_chg_show_warp_logo_ornot() == 1) {
				if(chip->new_ui_warning_support
					&& (chip->tbatt_status == BATTERY_STATUS__WARM_TEMP && chip->batt_full))
					val->intval = chip->prop_status;
				else
					val->intval = POWER_SUPPLY_STATUS_CHARGING;
			} else if (!chip->authenticate) {
				val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
			} else {
				val->intval = chip->prop_status;
			}
			break;
		case POWER_SUPPLY_PROP_HEALTH:
			val->intval = oplus_chg_get_prop_batt_health(chip);
			break;
		case POWER_SUPPLY_PROP_PRESENT:
			val->intval = chip->batt_exist;
			break;
		case POWER_SUPPLY_PROP_TECHNOLOGY:
			val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
			break;
		case POWER_SUPPLY_PROP_CAPACITY:
			if(chip->warp_show_ui_soc_decimal == true && chip->decimal_control) {
				val->intval = (chip->ui_soc_integer + chip->ui_soc_decimal)/1000;
			} else {
				val->intval = chip->ui_soc;
			}
			if(val->intval > 100) {
				val->intval = 100;
			}

			break;

#ifdef CONFIG_OPLUS_CHIP_SOC_NODE
		case POWER_SUPPLY_PROP_CHIP_SOC:
			val->intval = chip->soc;
			break;
#endif
#ifdef CONFIG_OPLUS_SMOOTH_SOC
		case POWER_SUPPLY_PROP_SMOOTH_SOC:
			val->intval = chip->smooth_soc;
			break;
		case POWER_SUPPLY_PROP_SMOOTH_SWITCH:
			val->intval = chip->smooth_switch;

			break;
#endif
		case POWER_SUPPLY_PROP_VOLTAGE_NOW:
#ifdef CONFIG_OPLUS_CHARGER_MTK
			val->intval = chip->batt_volt;
#else
			val->intval = chip->batt_volt * 1000;
#endif
			break;
		case POWER_SUPPLY_PROP_VOLTAGE_MIN:
#ifdef CONFIG_OPLUS_CHARGER_MTK
			val->intval = chip->batt_volt_min;
#else
			val->intval = chip->batt_volt_min * 1000;
#endif
			break;
		case POWER_SUPPLY_PROP_CURRENT_NOW:
			if (oplus_warp_get_fastchg_started() == true) {
				chip->icharging = oplus_gauge_get_prev_batt_current();
			} else {
				chip->icharging = oplus_gauge_get_batt_current();
			}
			val->intval = chip->icharging;
			break;
		case POWER_SUPPLY_PROP_TEMP:
			if (oplus_warp_get_fastchg_started() == true) {
				val->intval = chip->tbatt_temp - chip->offset_temp;
			} else {
				val->intval = chip->tbatt_temp - chip->offset_temp;
			}
			break;
		case POWER_SUPPLY_PROP_CHARGE_NOW:
			if (oplus_warp_get_fastchg_started() == true && (chip->vbatt_num == 2)
			&& oplus_warp_get_fast_chg_type() != CHARGER_SUBTYPE_FASTCHG_WARP) {
				val->intval = 10000;
			} else {
				val->intval = chip->charger_volt;
			}
			break;
		case POWER_SUPPLY_PROP_AUTHENTICATE:
			val->intval = chip->authenticate;
			break;
		case POWER_SUPPLY_PROP_CHARGE_TIMEOUT:
			val->intval = chip->chging_over_time;
			break;
		case POWER_SUPPLY_PROP_CHARGE_TECHNOLOGY:
			val->intval = chip->warp_project;
			break;
		case POWER_SUPPLY_PROP_FAST_CHARGE:
			val->intval = oplus_chg_show_warp_logo_ornot();
#ifdef CONFIG_OPLUS_CHARGER_MTK
			if (val->intval) {
				charger_xlog_printk(CHG_LOG_CRTI, "warp_logo:%d\n", val->intval);
			}
#endif
			break;
		case POWER_SUPPLY_PROP_MMI_CHARGING_ENABLE:	/*add for MMI_CHG TEST*/
			val->intval = chip->mmi_chg;
			break;
#ifdef CONFIG_OPLUS_CHARGER_MTK
		case POWER_SUPPLY_PROP_STOP_CHARGING_ENABLE:
			val->intval = chip->stop_chg;
			break;
		case POWER_SUPPLY_PROP_CHARGE_COUNTER:
			val->intval = chip->ui_soc * chip->batt_capacity_mah * 1000 / 100;
			break;
		case POWER_SUPPLY_PROP_CURRENT_MAX:
			val->intval = 2000;
			break;
#endif
#ifndef CONFIG_OPLUS_SDM670_CHARGER
		case POWER_SUPPLY_PROP_CHARGE_FULL:
			val->intval = chip->batt_fcc;
			break;
#endif
		case POWER_SUPPLY_PROP_BATTERY_FCC:
			val->intval = chip->batt_fcc;
			break;
		case POWER_SUPPLY_PROP_BATTERY_SOH:
			val->intval = chip->batt_soh;
			break;
		case POWER_SUPPLY_PROP_BATTERY_CC:
			val->intval = chip->batt_cc;
			break;
		case POWER_SUPPLY_PROP_BATTERY_RM:
			if (oplus_warp_get_fastchg_started() == true) {
				chip->batt_rm =  oplus_gauge_get_prev_remaining_capacity() * chip->vbatt_num;
			} else {
				chip->batt_rm =  oplus_gauge_get_remaining_capacity() * chip->vbatt_num;
			}
			val->intval = chip->batt_rm;
			break;
		case POWER_SUPPLY_PROP_BATTERY_NOTIFY_CODE:
			val->intval = chip->notify_code;
			break;
#ifdef CONFIG_OPLUS_SMART_CHARGER_SUPPORT
		case POWER_SUPPLY_PROP_COOL_DOWN:
			val->intval = chip->cool_down;
			break;
#endif
		case POWER_SUPPLY_PROP_ADAPTER_FW_UPDATE:
			val->intval = oplus_warp_get_adapter_update_status();
			break;
		case POWER_SUPPLY_PROP_WARPCHG_ING:
			val->intval = oplus_warp_get_fastchg_ing();
#ifndef CONFIG_OPLUS_CHARGER_MTK
#ifndef WPC_NEW_INTERFACE
			if (!val->intval && chip->wireless_support) {
				val->intval = oplus_wpc_get_fast_charging();
			}
#else
			if (!val->intval && chip->wireless_support) {
				val->intval = oplus_wpc_get_status();
			}
#endif
#endif
			break;
#ifdef CONFIG_OPLUS_CHECK_CHARGERID_VOLT
		case POWER_SUPPLY_PROP_CHARGERID_VOLT:
			val->intval = chip->chargerid_volt;
			break;
#endif
#ifdef CONFIG_OPLUS_SHIP_MODE_SUPPORT
		case POWER_SUPPLY_PROP_SHIP_MODE:
			val->intval = chip->enable_shipmode;
			break;
#endif
#ifdef CONFIG_OPLUS_CALL_MODE_SUPPORT
		case POWER_SUPPLY_PROP_CALL_MODE:
			val->intval = chip->calling_on;
			break;
#endif
#ifdef CONFIG_OPLUS_SHORT_C_BATT_CHECK
#ifdef CONFIG_OPLUS_SHORT_USERSPACE
		case POWER_SUPPLY_PROP_SHORT_C_LIMIT_CHG:
			val->intval = (int)chip->short_c_batt.limit_chg;
			break;
		case POWER_SUPPLY_PROP_SHORT_C_LIMIT_RECHG:
			val->intval = (int)chip->short_c_batt.limit_rechg;
			break;
		case POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT:
			val->intval = chip->limits.iterm_ma;
			break;
		case POWER_SUPPLY_PROP_INPUT_CURRENT_SETTLED:
			val->intval = 2000;
			if (chip && chip->chg_ops->get_dyna_aicl_result) {
				val->intval = chip->chg_ops->get_dyna_aicl_result();
			}
			break;
#else
		case POWER_SUPPLY_PROP_SHORT_C_BATT_UPDATE_CHANGE:
			val->intval = chip->short_c_batt.update_change;
			break;
		case POWER_SUPPLY_PROP_SHORT_C_BATT_IN_IDLE:
			val->intval = (int)chip->short_c_batt.in_idle;
			break;
		case POWER_SUPPLY_PROP_SHORT_C_BATT_CV_STATUS:
			val->intval = (int)oplus_short_c_batt_get_cv_status(chip);
			break;
#endif /*CONFIG_OPLUS_SHORT_USERSPACE*/
#endif /* CONFIG_OPLUS_SHORT_C_BATT_CHECK */
#ifdef CONFIG_OPLUS_SHORT_HW_CHECK
		case POWER_SUPPLY_PROP_SHORT_C_HW_FEATURE:
			val->intval = chip->short_c_batt.is_feature_hw_on;
			break;
		case POWER_SUPPLY_PROP_SHORT_C_HW_STATUS:
			val->intval = chip->short_c_batt.shortc_gpio_status;
			break;
#endif /* CONFIG_OPLUS_SHORT_C_BATT_CHECK */
#ifdef CONFIG_OPLUS_SHORT_IC_CHECK
		case POWER_SUPPLY_PROP_SHORT_C_IC_OTP_STATUS:
			if (chip) {
				val->intval = chip->short_c_batt.ic_short_otp_st;
			}
			break;
		case POWER_SUPPLY_PROP_SHORT_C_IC_VOLT_THRESH:
			if (chip) {
				val->intval = chip->short_c_batt.ic_volt_threshold;
			}
			break;
		case POWER_SUPPLY_PROP_SHORT_C_IC_OTP_VALUE:
			if (chip) {
				val->intval = oplus_short_ic_get_otp_error_value(chip);
			}
			break;
#endif
		default:
			pr_err("get prop %d is not supported in batt\n", psp);
			ret = -EINVAL;
			break;
	}
	return ret;
}
#endif /* CONFIG_OPLUS_CHG_GKI_SUPPORT */

#ifdef CONFIG_OPLUS_CHG_GKI_SUPPORT
static bool is_wls_ocm_available(struct oplus_chg_chip *dev)
{
	if (!dev->wls_ocm)
		dev->wls_ocm = oplus_chg_mod_get_by_name("wireless");
	return !!dev->wls_ocm;
}

static bool is_usb_ocm_available(struct oplus_chg_chip *dev)
{
	if (!dev->usb_ocm)
		dev->usb_ocm = oplus_chg_mod_get_by_name("usb");
	return !!dev->usb_ocm;
}

static bool is_comm_ocm_available(struct oplus_chg_chip *dev)
{
	if (!dev->comm_ocm)
		dev->comm_ocm = oplus_chg_mod_get_by_name("common");
	return !!dev->comm_ocm;
}
static bool is_batt_ocm_available(struct oplus_chg_chip *dev)
{
	if (!dev->batt_ocm)
		dev->batt_ocm = oplus_chg_mod_get_by_name("battery");
	return !!dev->batt_ocm;
}
#endif /* CONFIG_OPLUS_CHG_GKI_SUPPORT */

#ifdef OPLUS_CHG_OP_DEF
static bool oplus_chg_is_wls_online(struct oplus_chg_chip *dev)
{
	union oplus_chg_mod_propval pval;
	int rc;

	if (!is_wls_ocm_available(dev)) {
		// pr_err("wls ocm not found\n");
		return false;
	}
	rc = oplus_chg_mod_get_property(dev->wls_ocm, OPLUS_CHG_PROP_ONLINE, &pval);
	if (rc < 0)
		return false;

	return !!pval.intval;
}

static bool oplus_chg_is_wls_present(struct oplus_chg_chip *dev)
{
	union oplus_chg_mod_propval pval;
	int rc;

	if (!is_wls_ocm_available(dev)) {
		// pr_err("wls ocm not found\n");
		return false;
	}
	rc = oplus_chg_mod_get_property(dev->wls_ocm, OPLUS_CHG_PROP_PRESENT, &pval);
	if (rc < 0)
		return false;

	return !!pval.intval;
}

static enum oplus_chg_wls_type oplus_chg_get_wls_charge_type(struct oplus_chg_chip *dev)
{
	union oplus_chg_mod_propval val;
	enum oplus_chg_wls_type wls_type;
	int rc;

	if (!is_wls_ocm_available(dev)) {
		// pr_err("wls ocm not found\n");
		return OPLUS_CHG_WLS_UNKNOWN;
	}

	rc = oplus_chg_mod_get_property(dev->wls_ocm, OPLUS_CHG_PROP_WLS_TYPE, &val);
	if (rc < 0) {
		pr_err("can't get wls charger voltage, rc=%d\n", rc);
		return OPLUS_CHG_WLS_UNKNOWN;
	}
	wls_type = val.intval;
	return wls_type;
}

static int oplus_chg_get_skin_temp(struct oplus_chg_chip *dev, int *temp)
{
	int rc;
	union oplus_chg_mod_propval pval;

	if (!is_comm_ocm_available(dev)) {
		pr_err("comm ocm not found\n");
		return -ENODEV;
	}
	rc = oplus_chg_mod_get_property(dev->comm_ocm, OPLUS_CHG_PROP_SKIN_TEMP, &pval);
	if (rc < 0)
		return rc;
	*temp = pval.intval;

	return 0;
}

#ifdef CONFIG_OPLUS_CHG_OOS
struct oplus_chg_strategy warp_chg_led_on_strategy;
struct oplus_chg_strategy pd5v_chg_led_on_strategy;
struct oplus_chg_strategy pd9v_chg_led_on_strategy;
#endif /* CONFIG_OPLUS_CHG_OOS */
struct oplus_chg_strategy normal_chg_led_off_strategy;

#endif /* OPLUS_CHG_OP_DEF */

static ssize_t proc_batt_param_noplug_write(struct file *filp,
		const char __user *buf, size_t len, loff_t *data)
{
	return len;
}

static int noplug_temperature = 0;
static int noplug_batt_volt_max = 0;
static int noplug_batt_volt_min = 0;
static ssize_t proc_batt_param_noplug_read(struct file *filp,
		char __user *buff, size_t count, loff_t *off) {
	char page[256] = {0};
	char read_data[128] = {0};
	int len = 0;

	sprintf(read_data, "%d %d %d", noplug_temperature,
		noplug_batt_volt_max, noplug_batt_volt_min);
	len = sprintf(page, "%s", read_data);
	if (len > *off) {
		len -= *off;
	} else {
		len = 0;
	}
	if (copy_to_user(buff, page, (len < count ? len : count))) {
		return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);
}

static const struct file_operations batt_param_noplug_proc_fops = {
	.write = proc_batt_param_noplug_write,
	.read = proc_batt_param_noplug_read,
};

static int init_proc_batt_param_noplug(void)
{
	struct proc_dir_entry *p = NULL;

	p = proc_create("batt_param_noplug", 0664, NULL, &batt_param_noplug_proc_fops);
	if (!p) {
		chg_err("proc_create  fail!\n");
	}
	return 0;
}

static ssize_t proc_tbatt_pwroff_write(struct file *filp,
		const char __user *buf, size_t len, loff_t *data)
{
	char buffer[2] = {0};

	if (len > 2) {
		return -EFAULT;
	}
	if (copy_from_user(buffer, buf, 2)) {
		chg_err("%s:  error.\n", __func__);
		return -EFAULT;
	}
	if (buffer[0] == '0') {
		tbatt_pwroff_enable = 0;
	} else {
		tbatt_pwroff_enable = 1;
		oplus_tbatt_power_off_task_wakeup();
	}
	chg_err("%s:tbatt_pwroff_enable = %d.\n", __func__, tbatt_pwroff_enable);
	return len;
}

static ssize_t proc_tbatt_pwroff_read(struct file *filp,
		char __user *buff, size_t count, loff_t *off)
{
	char page[256] = {0};
	char read_data[3] = {0};
	int len = 0;

	if (tbatt_pwroff_enable == 1) {
		read_data[0] = '1';
	} else {
		read_data[0] = '0';
	}
	read_data[1] = '\0';
	len = sprintf(page, "%s", read_data);
	if (len > *off) {
		len -= *off;
	} else {
		len = 0;
	}
	if (copy_to_user(buff, page, (len < count ? len : count))) {
		return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);
}

static const struct file_operations tbatt_pwroff_proc_fops = {
	.write = proc_tbatt_pwroff_write,
	.read = proc_tbatt_pwroff_read,
};

static int init_proc_tbatt_pwroff(void)
{
	struct proc_dir_entry *p = NULL;

	p = proc_create("tbatt_pwroff", 0664, NULL, &tbatt_pwroff_proc_fops);
	if (!p) {
		chg_err("proc_create  fail!\n");
	}
	return 0;
}

static ssize_t chg_log_write(struct file *filp,
		const char __user *buff, size_t len, loff_t *data)
{
	char write_data[32] = {0};

	if (len > sizeof(write_data)) {
		chg_err("bat_log_write error.\n");
		return -EFAULT;
	}
	if (copy_from_user(&write_data, buff, len)) {
		chg_err("bat_log_write error.\n");
		return -EFAULT;
	}
	if (write_data[0] == '1') {
		charger_xlog_printk(CHG_LOG_CRTI, "enable battery driver log system\n");
		enable_charger_log = 1;
	} else if ((write_data[0] >= '2') &&(write_data[0] <= '9')) {
		charger_xlog_printk(CHG_LOG_CRTI, "enable battery driver log system:2\n");
		enable_charger_log = 2;
	} else {
		charger_xlog_printk(CHG_LOG_CRTI, "Disable battery driver log system\n");
		enable_charger_log = 0;
	}
	return len;
}

static ssize_t chg_log_read(struct file *filp,
		char __user *buff, size_t count, loff_t *off)
{
	char page[256] = {0};
	char read_data[32] = {0};
	int len = 0;

	if (enable_charger_log == 1) {
		read_data[0] = '1';
	} else if (enable_charger_log == 2) {
		read_data[0] = '2';
	} else {
		read_data[0] = '0';
	}
	len = sprintf(page, "%s", read_data);
	if (len > *off) {
		len -= *off;
	} else {
		len = 0;
	}
	if (copy_to_user(buff, page, (len < count ? len : count))) {
		return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);
}

static const struct file_operations chg_log_proc_fops = {
	.write = chg_log_write,
	.read = chg_log_read,
};

static int init_proc_chg_log(void)
{
	struct proc_dir_entry *p = NULL;

	p = proc_create("charger_log", 0664, NULL, &chg_log_proc_fops);
	if (!p) {
		chg_err("proc_create chg_log_proc_fops fail!\n");
	}
	return 0;
}

static void oplus_chg_set_awake(struct oplus_chg_chip *chip, bool awake);

static ssize_t chg_cycle_write(struct file *file,
		const char __user *buff, size_t count, loff_t *ppos)
{
	char proc_chg_cycle_data[16];

	if(count >= 16) {
		count = 16;
	}
	if (copy_from_user(&proc_chg_cycle_data, buff, count)) {
		chg_err("chg_cycle_write error.\n");
		return -EFAULT;
	}
	if (strncmp(proc_chg_cycle_data, "en808", 5) == 0) {
		if(g_charger_chip->unwakelock_chg == 1) {
			charger_xlog_printk(CHG_LOG_CRTI, "unwakelock testing , this test not allowed.\n");
			return -EPERM;
		}
		charger_xlog_printk(CHG_LOG_CRTI, "allow charging.\n");
		g_charger_chip->chg_ops->charger_unsuspend();
		g_charger_chip->chg_ops->charging_enable();
		g_charger_chip->mmi_chg = 1;
		g_charger_chip->stop_chg = 1;
		if (g_charger_chip->dual_charger_support) {
			g_charger_chip->slave_charger_enable = false;
			oplus_chg_set_charging_current(g_charger_chip);
		}
		oplus_chg_set_input_current_limit(g_charger_chip);
	} else if (strncmp(proc_chg_cycle_data, "dis808", 6) == 0) {
		if(g_charger_chip->unwakelock_chg == 1) {
			charger_xlog_printk(CHG_LOG_CRTI, "unwakelock testing , this test not allowed.\n");
			return -EPERM;
		}
		charger_xlog_printk(CHG_LOG_CRTI, "not allow charging.\n");
		g_charger_chip->chg_ops->charging_disable();
		g_charger_chip->chg_ops->charger_suspend();
		g_charger_chip->mmi_chg = 0;
		g_charger_chip->stop_chg = 0;
	} else if (strncmp(proc_chg_cycle_data, "wakelock", 8) == 0) {
		charger_xlog_printk(CHG_LOG_CRTI, "set wakelock.\n");
		g_charger_chip->unwakelock_chg = 0;
		oplus_chg_set_awake(g_charger_chip, true);
		g_charger_chip->chg_ops->charger_unsuspend();
		g_charger_chip->chg_ops->charging_enable();
		g_charger_chip->mmi_chg = 1;
		g_charger_chip->stop_chg = 1;
		g_charger_chip->chg_powersave = false;
		if (g_charger_chip->chg_ops->oplus_chg_wdt_enable)
			g_charger_chip->chg_ops->oplus_chg_wdt_enable(true);
		if (g_charger_chip->mmi_fastchg == 0) {
			oplus_chg_clear_chargerid_info();
		}
		g_charger_chip->mmi_fastchg = 1;
	} else if (strncmp(proc_chg_cycle_data, "unwakelock", 10) == 0) {
		charger_xlog_printk(CHG_LOG_CRTI, "set unwakelock.\n");
		g_charger_chip->chg_ops->charging_disable();
		//g_charger_chip->chg_ops->charger_suspend();
		g_charger_chip->mmi_chg = 0;
		g_charger_chip->stop_chg = 0;
		g_charger_chip->unwakelock_chg = 1;
		g_charger_chip->chg_powersave = true;
		if (oplus_warp_get_fastchg_started() == true) {
			oplus_warp_turn_off_fastchg();
			g_charger_chip->mmi_fastchg = 0;
		}
		oplus_chg_set_awake(g_charger_chip, false);
		if (g_charger_chip->chg_ops->oplus_chg_wdt_enable)
			g_charger_chip->chg_ops->oplus_chg_wdt_enable(false);
	} else if (strncmp(proc_chg_cycle_data, "powersave", 9) == 0) {
		charger_xlog_printk(CHG_LOG_CRTI, "powersave: stop usbtemp monitor, etc.\n");
		g_charger_chip->chg_powersave = true;
	} else if (strncmp(proc_chg_cycle_data, "unpowersave", 11) == 0) {
		charger_xlog_printk(CHG_LOG_CRTI, "unpowersave: start usbtemp monitor, etc.\n");
		g_charger_chip->chg_powersave = false;
	} else {
		return -EFAULT;
	}
	return count;
}

static const struct file_operations chg_cycle_proc_fops = {
	.write = chg_cycle_write,
	.llseek = noop_llseek,
};

static void init_proc_chg_cycle(void)
{
	if (!proc_create("charger_cycle",
			S_IWUSR | S_IWGRP | S_IWOTH,
			NULL, &chg_cycle_proc_fops)) {
		chg_err("proc_create chg_cycle_proc_fops fail!\n");
	}
}
static ssize_t critical_log_read(struct file *filp,
		char __user *buff, size_t count, loff_t *off)
{
	char page[256] = {0};
	char read_data[32] = {0};
	int len = 0;
	//	itoa(charger_abnormal_log, read_data, 10);
	//	sprintf(read_data,"%s",charger_abnormal_log);
	if (charger_abnormal_log >= 10) {
		charger_abnormal_log = 10;
	}
	read_data[0] = '0' + charger_abnormal_log % 10;
	len = sprintf(page, "%s", read_data);
	if (len > *off) {
		len -= *off;
	} else {
		len = 0;
	}
	if (copy_to_user(buff, page, (len < count ? len : count))) {
		return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);
}

static ssize_t critical_log_write(struct file *filp,
		const char __user *buff, size_t len, loff_t *data)
{
	char write_data[32] = {0};
	int critical_log = 0;

	if (len > sizeof(write_data)) {
		return -EINVAL;
	}
	if (copy_from_user(&write_data, buff, len)) {
		pr_err("bat_log_write error.\n");
		return -EFAULT;
	}
	/*	critical_log = atoi(write_data);*/
	/*	sprintf(critical_log,"%d",(void *)write_data);*/
	write_data[len] = '\0';
	if (write_data[len - 1] == '\n') {
		write_data[len - 1] = '\0';
	}
	critical_log = (int)simple_strtoul(write_data, NULL, 10);
	/*	pr_err("%s:data=%s,critical_log=%d\n",__func__,write_data,critical_log);*/
	if (critical_log > 256) {
		critical_log = 256;
	}
	charger_abnormal_log = critical_log;
	return len;
}

static const struct file_operations chg_critical_log_proc_fops = {
	.write = critical_log_write,
	.read = critical_log_read,
};

static void init_proc_critical_log(void)
{
	struct proc_dir_entry *p = NULL;

	p = proc_create("charger_critical_log", 0664, NULL,
		&chg_critical_log_proc_fops);
	if (!p) {
		pr_err("proc_create chg_critical_log_proc_fops fail!\n");
	}
}

#ifdef CONFIG_OPLUS_RTC_DET_SUPPORT
/* wenbin.liu add for det rtc reset */
static ssize_t rtc_reset_read(struct file *filp,
		char __user *buff, size_t count, loff_t *off)
{
	char page[256] = {0};
	char read_data[32] = {0};
	int len = 0;
	int rc = 0;

	if (!g_charger_chip) {
		return -EFAULT;
	} else {
		rc = g_charger_chip->chg_ops->check_rtc_reset();
	}
	if (rc < 0 || rc >1) {
		rc = 0;
	}
	read_data[0] = '0' + rc % 10;
	len = sprintf(page, "%s", read_data);
	if (len > *off) {
		len -= *off;
	} else {
		len = 0;
	}
	if (copy_to_user(buff, page, (len < count ? len : count))) {
		return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);
}

static const struct file_operations rtc_reset_det_fops = {
	.read = rtc_reset_read,
};

static void init_proc_rtc_det(void)
{
	struct proc_dir_entry *p = NULL;

	p = proc_create("rtc_reset", 0664, NULL, &rtc_reset_det_fops);
	if (!p) {
		pr_err("proc_create rtc_reset_det_fops fail!\n");
	}
}
static ssize_t vbat_low_read(struct file *filp,
		char __user *buff, size_t count, loff_t *off)
{
	char page[256] = {0};
	char read_data[32] = {0};
	int len = 0;
	int rc = 0;

	if (!g_charger_chip)
		return -EFAULT;
	if (vbatt_lowerthan_3300mv) {
		rc = 1;
	}
	read_data[0] = '0' + rc % 10;
	len = sprintf(page,"%s",read_data);
	if (len > *off) {
		len -= *off;
	} else {
		len = 0;
	}
	if (copy_to_user(buff,page,(len < count ? len : count))) {
		return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);
}


static const struct file_operations vbat_low_det_fops = {
	.read = vbat_low_read,
};

static void init_proc_vbat_low_det(void)
{
	struct proc_dir_entry *p = NULL;

	p = proc_create("vbat_low", 0664, NULL, &vbat_low_det_fops);
	if (!p) {
		pr_err("proc_create rtc_reset_det_fops fail!\n");
	}
}

#endif /* CONFIG_OPLUS_RTC_DET_SUPPORT */

#ifdef OPLUS_CHG_OP_DEF
static bool oplus_chg_update_slow(struct oplus_chg_chip *chip)
{
	int ibat_sum = 0;
	int ibat_average;
	bool slow;
	int i;

	for (i = 0; i < (ARRAY_SIZE(chip->ibat_save) - 1); i++) {
		chip->ibat_save[i] = chip->ibat_save[i + 1];
		ibat_sum += chip->ibat_save[i];
	}
	chip->ibat_save[i] = chip->icharging;
	ibat_sum += chip->ibat_save[i];
	ibat_average = ibat_sum / ARRAY_SIZE(chip->ibat_save);

	slow = !(oplus_chg_is_wls_present(chip) || chip->charger_exist ||
		 (ibat_average >= 1000) || (chip->soc <= 15) ||
		 (chip->soc >= 90));

	pr_debug("ibat_average = %d, slow = %d\n", ibat_average, slow);

	return slow;
}
#endif

 int oplus_get_vbatt_pdqc_to_9v_thr(void)
{
	struct oplus_chg_chip *chip = g_charger_chip;
	int rc, ret;

	if (chip->dev->of_node) {
		rc = of_property_read_u32(chip->dev->of_node, "qcom,vbatt_pdqc_to_9v_thr",
			&ret);
		if (rc < 0) {
			ret = 4000;
		}
	}else{
		ret = 4000;
	}
	return ret;
}
static ssize_t proc_charger_factorymode_test_write
				(struct file *file, const char __user *buf,
					size_t count, loff_t *lo)
{
	char buffer[2] = { 0 };
	struct oplus_chg_chip *chip = g_charger_chip;

	if (chip == NULL) {
		chg_err("%s: g_charger_chip driver is not ready\n", __func__);
		return -1;
	}

	if (count > 2) {
		return -1;
	}
	if (copy_from_user(buffer, buf, 1)) {
		chg_err("%s: error.\n", __func__);
		return -1;
	}
#ifdef CONFIG_OPLUS_CHG_OOS
	if(buffer[0] == '5'){
		set_eng_version(HIGH_TEMP_AGING);
		chg_err("FactoryAppset set_eng_version=%d\n", get_eng_version());
	}else if(buffer[0] == '4'){
		set_eng_version(HIGH_TEMP_AGING);
		chg_err("auto set_eng_version=%d\n", get_eng_version());
	}else if(buffer[0] == '3'){
		set_eng_version(AGING_TEST_STATUS_DEFAULT);
		chg_err("set_eng_version=%d\n", get_eng_version());
	} else if(buffer[0] == '2'){
#else
	if(buffer[0] == '2'){
#endif
		chip->factory_mode = 1;
		chg_err("chip->factory_mode=%d\n", chip->factory_mode);
	} else if(buffer[0] == '1'){
		chip->limits.vbatt_pdqc_to_9v_thr = 4100;
		chg_err("vbatt_pdqc_to_9v_thr=%d\n", chip->limits.vbatt_pdqc_to_9v_thr);
		oplus_chg_pd_config(chip);
		oplus_chg_qc_config(chip);
	}
	if(buffer[0] == '0'){
		chip->factory_mode = 0;
		chg_err("chip->factory_mode=%d\n", chip->factory_mode);
		chip->limits.vbatt_pdqc_to_9v_thr = oplus_get_vbatt_pdqc_to_9v_thr();
		chg_err("vbatt_pdqc_to_9v_thr=%d\n", chip->limits.vbatt_pdqc_to_9v_thr);
	}

	return count;
}

static const struct file_operations proc_charger_factorymode_test_ops =
{
    .write  = proc_charger_factorymode_test_write,
    .open  = simple_open,
    .owner = THIS_MODULE,
};

static ssize_t proc_hmac_write(struct file *filp,
		const char __user *buf, size_t len, loff_t *data)
{
	struct oplus_chg_chip *chip = g_charger_chip;
	char buffer[2] = {0};

	if (NULL == chip)
		return  -EFAULT;

	if (len > 2) {
		return -EFAULT;
	}

	if (copy_from_user(buffer, buf, 2)) {
		chg_err("%s:  error.\n", __func__);
		return -EFAULT;
	}
	if (buffer[0] == '0') {
		chip->hmac = false;
	} else {
		chip->hmac = true;
	}
	return len;
}

static ssize_t proc_hmac_read(struct file *filp,
		char __user *buff, size_t count, loff_t *off)
{
	struct oplus_chg_chip *chip = g_charger_chip;
	char page[256] = {0};
	char read_data[3] = {0};
	int len = 0;

	if (NULL == chip)
		return  -EFAULT;

	if (true == chip->hmac) {
		read_data[0] = '1';
	} else {
		read_data[0] = '0';
	}
	read_data[1] = '\0';
	len = sprintf(page, "%s", read_data);
	if (len > *off) {
		len -= *off;
	} else {
		len = 0;
	}
	if (copy_to_user(buff, page, (len < count ? len : count))) {
		chg_err("%s: copy_to_user error hmac = %d.\n", __func__, chip->hmac);
		return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);
}

static const struct file_operations hmac_proc_fops = {
	.write = proc_hmac_write,
	.read = proc_hmac_read,
	.owner = THIS_MODULE,
};
static ssize_t proc_charger_input_current_now_read(struct file *filp,
		char __user *buff, size_t count, loff_t *off)
{
	char page[256] = {0};
	char read_data[128] = {0};
	int len = 0;

	struct oplus_chg_chip *chip = g_charger_chip;
	if(!chip) {
		return 0;
	}
	sprintf(read_data, "%d", chip->ibus);
	len = sprintf(page, "%s", read_data);

	if (len > *off) {
		len -= *off;
	} else {
		len = 0;
	}
	if (copy_to_user(buff, page, (len < count ? len : count))) {
		return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);
}

static ssize_t proc_charger_passedchg_read(struct file *filp,
		char __user *buff, size_t count, loff_t *off)
{
	char page[256] = {0};
	char read_data[128] = {0};
	int len = 0;

	struct oplus_chg_chip *chip = g_charger_chip;
	if(!chip) {
		return 0;
	}

	sprintf(read_data, "%d", midas_chg.cali_passed_chg);
	len = sprintf(page, "%s", read_data);

	if (len > *off) {
		len -= *off;
	} else {
		len = 0;
	}
	if (copy_to_user(buff, page, (len < count ? len : count))) {
		return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);
}

static ssize_t proc_charger_passedchg_reset_count_read(struct file *filp,
		char __user *buff, size_t count, loff_t *off)
{
	char page[256] = {0};
	char read_data[128] = {0};
	int len = 0;

	struct oplus_chg_chip *chip = g_charger_chip;
	if(!chip) {
		return 0;
		}
	sprintf(read_data, "%d", midas_chg.reset_counts);
	len = sprintf(page, "%s", read_data);

	if (len > *off) {
		len -= *off;
	} else {
		len = 0;
	}
	if (copy_to_user(buff, page, (len < count ? len : count))) {
		return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);
}
static const struct file_operations proc_charger_input_current_now_ops =
{
	.read  = proc_charger_input_current_now_read,
	.owner = THIS_MODULE,
};

static const struct file_operations proc_charger_passedchg_ops =
{
	.read  = proc_charger_passedchg_read,
	.owner = THIS_MODULE,
};

static const struct file_operations proc_charger_passedchg_reset_count_ops =
{
	.read  = proc_charger_passedchg_reset_count_read,
	.owner = THIS_MODULE,
};

#define PROC_READ_MAX_SIZE 32
#define PROC_READ_PAGE_SIZE 256
static ssize_t proc_integrate_gauge_fcc_flag_read(struct file *filp,
		char __user *buff, size_t count, loff_t *off)
{
	char page[PROC_READ_PAGE_SIZE] = {0};
	char read_data[PROC_READ_MAX_SIZE] = {0};
	int len = 0;

	read_data[0] = '1';
	len = sprintf(page, "%s", read_data);
	if(len > *off) {
		len -= *off;
	} else {
		len = 0;
	}
	if (copy_to_user(buff, page, (len < count ? len : count))) {
		return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
static const struct file_operations proc_integrate_gauge_fcc_flag_ops =
{
	.read  = proc_integrate_gauge_fcc_flag_read,
	.open  = simple_open,
	.owner = THIS_MODULE,
};
#else
static const struct proc_ops proc_integrate_gauge_fcc_flag_ops =
{
	.proc_read  = proc_integrate_gauge_fcc_flag_read,
	.proc_open  = simple_open,
};
#endif


static int init_charger_proc(struct oplus_chg_chip *chip)
{
	int ret = 0;
	struct proc_dir_entry *prEntry_da = NULL;
	struct proc_dir_entry *prEntry_tmp = NULL;

	prEntry_da = proc_mkdir("charger", NULL);
	if (prEntry_da == NULL) {
		ret = -1;
		chg_debug("%s: Couldn't create charger proc entry\n",
			  __func__);
	}

	prEntry_tmp = proc_create_data("charger_factorymode_test", 0666, prEntry_da,
				       &proc_charger_factorymode_test_ops, chip);
	if (prEntry_tmp == NULL) {
		ret = -1;
		chg_debug("%s: Couldn't create proc entry, %d\n", __func__,
			  __LINE__);
	}

	prEntry_tmp = proc_create_data("hmac", 0666, prEntry_da,
				       &hmac_proc_fops, chip);
	if (prEntry_tmp == NULL) {
		ret = -1;
		chg_debug("%s: Couldn't create hmac proc entry, %d\n", __func__,
			  __LINE__);
	}
	prEntry_tmp = proc_create_data("input_current_now", 0666, prEntry_da,
				       &proc_charger_input_current_now_ops, chip);
	prEntry_tmp = proc_create_data("passedchg", 0666, prEntry_da,
						   &proc_charger_passedchg_ops, chip);
	prEntry_tmp = proc_create_data("passedchg_reset_count", 0666, prEntry_da,
					   &proc_charger_passedchg_reset_count_ops, chip);

	prEntry_tmp = proc_create_data("integrate_gauge_fcc_flag", 0664, prEntry_da,
					 &proc_integrate_gauge_fcc_flag_ops, chip);
	if (prEntry_tmp == NULL)
		chg_err("Couldn't create integrate_gauge_fcc_flag proc entry\n");

	return 0;
}

static ssize_t proc_ui_soc_decimal_write(struct file *filp,
               const char __user *buf, size_t len, loff_t *data)
{
	struct oplus_chg_chip *chip = g_charger_chip;
	char buffer[2] = {0};

	if (NULL == chip)
		  return  -EFAULT;

	if (len > 2) {
		  return -EFAULT;
	}

	if (copy_from_user(buffer, buf, 2)) {
		  chg_err("%s:  error.\n", __func__);
		  return -EFAULT;
	}
	if (buffer[0] == '0') {
		  chip->boot_completed = false;
	} else {
		  chip->boot_completed = true;
	}
	pr_err("proc_ui_soc_decimal_write write");
	return len;
}
static ssize_t proc_ui_soc_decimal_read(struct file *filp,
		char __user *buff, size_t count, loff_t *off)
{
	char page[256] = {0};
	char read_data[128] = {0};
	int len = 0;
	int schedule_work = 0;
	int val;
	bool swarp_is_control_by_warp;
	bool control_by_wireless = false;
	struct oplus_chg_chip *chip = g_charger_chip;

	if(!chip) {
		return 0;
	}

	if (chip->warp_show_ui_soc_decimal) {
		swarp_is_control_by_warp = (chip->vbatt_num == 2 && oplus_warp_get_fast_chg_type() == CHARGER_SUBTYPE_FASTCHG_WARP);
#ifndef CONFIG_OPLUS_CHARGER_MTK
#ifndef OPLUS_CHG_OP_DEF
		control_by_wireless = chip->wireless_support && oplus_wpc_get_wireless_charge_start() == true;
#else /* OPLUS_CHG_OP_DEF */
		control_by_wireless = chip->wireless_support && oplus_chg_is_wls_present(chip) == true;
#endif /* OPLUS_CHG_OP_DEF */
#endif
		if(swarp_is_control_by_warp != true
				&&  chip->boot_completed != false
				&& chip->calculate_decimal_time == 0
				&& control_by_wireless == false
				&& oplus_chg_show_warp_logo_ornot() == true) {
			cancel_delayed_work_sync(&chip->ui_soc_decimal_work);
			oplus_chg_ui_soc_decimal_init();
			schedule_work = mod_delayed_work(system_wq, &chip->ui_soc_decimal_work, 0);
		}

		if (control_by_wireless && chip->decimal_control == true) {
			cancel_delayed_work_sync(&g_charger_chip->ui_soc_decimal_work);
			chip->last_decimal_ui_soc = (chip->ui_soc_integer + chip->ui_soc_decimal);
			oplus_chg_ui_soc_decimal_deinit();
			chip->calculate_decimal_time = 0;
			pr_err("[proc_ui_soc_decimal_read]cancel last_decimal_ui_soc:%d", chip->last_decimal_ui_soc);
		}
		val = (chip->ui_soc_integer + chip->ui_soc_decimal) / 10;
		if(chip->decimal_control == false) {
			val = 0;
		}
	} else {
		val = 0;
	}

	sprintf(read_data, "%d, %d", chip->init_decimal_ui_soc / 10, val);
	pr_err("APK successful, %d,%d", chip->init_decimal_ui_soc / 10, val);
	len = sprintf(page, "%s", read_data);

	if (len > *off) {
		len -= *off;
	} else {
		len = 0;
	}
	if (copy_to_user(buff, page, (len < count ? len : count))) {
		return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);
}

static const struct file_operations ui_soc_decimal_ops =
{
    .write  = proc_ui_soc_decimal_write,
    .read = proc_ui_soc_decimal_read,
    .owner = THIS_MODULE,
};

static int init_ui_soc_decimal_proc(struct oplus_chg_chip *chip)
{
	int ret = 0;
	struct proc_dir_entry *prEntry_tmp = NULL;

	prEntry_tmp = proc_create_data("ui_soc_decimal", 0666, NULL,
				       &ui_soc_decimal_ops, chip);
	if (prEntry_tmp == NULL) {
		ret = -1;
		chg_debug("%s: Couldn't create proc entry, %d\n", __func__,
			  __LINE__);
	}
	return 0;
}

void oplus_chg_ui_soc_decimal_init(void)
{
	struct oplus_chg_chip *chip = g_charger_chip;
	if (oplus_warp_get_fastchg_started() == true) {
		chip->batt_rm =  oplus_gauge_get_prev_remaining_capacity() * chip->vbatt_num;
		chip->batt_fcc =  oplus_gauge_get_prev_batt_fcc() * chip->vbatt_num;
	} else {
		chip->batt_rm =  oplus_gauge_get_remaining_capacity() * chip->vbatt_num;
		chip->batt_fcc =  oplus_gauge_get_batt_fcc() * chip->vbatt_num;
	}
	pr_err("[oplus_chg_ui_soc_decimal_init]!!!soc:%d", (int)((chip->batt_rm * 10000) / chip->batt_fcc));

	if(chip->ui_soc == 100) {
		chip->ui_soc_integer =  chip->ui_soc  *1000;
		chip->ui_soc_decimal = 0;
	} else {
		chip->ui_soc_integer =  chip->ui_soc  *1000;
		chip->ui_soc_decimal = chip->batt_rm * 100000 / chip->batt_fcc - (chip->batt_rm *100 / chip->batt_fcc) * 1000;
		if((chip->ui_soc_integer + chip->ui_soc_decimal) > chip->last_decimal_ui_soc && chip->last_decimal_ui_soc != 0) {
			chip->ui_soc_decimal = ((chip->last_decimal_ui_soc % 1000 - 50) > 0) ? (chip->last_decimal_ui_soc % 1000 - 50) : 0;
		}
	}
	chip->init_decimal_ui_soc = chip->ui_soc_integer + chip->ui_soc_decimal;
	if(chip->init_decimal_ui_soc > 100000) {
		chip->init_decimal_ui_soc = 100000;
		chip->ui_soc_integer = 100000;
		chip->ui_soc_decimal = 0;
	}
	chip->decimal_control = true;
	pr_err("[oplus_chg_ui_soc_decimal_init]!!! 2VBUS ui_soc_decimal:%d", chip->ui_soc_integer + chip->ui_soc_decimal);

	chip->calculate_decimal_time = 1;
}
void oplus_chg_ui_soc_decimal_deinit(void)
{
	struct oplus_chg_chip *chip = g_charger_chip;
	chip->ui_soc_integer =(chip->ui_soc_integer + chip->ui_soc_decimal)/1000;
	if(chip->ui_soc_integer != 0) {
		chip->ui_soc = chip->ui_soc_integer;
	}
	chip->decimal_control = false;
	pr_err("[oplus_chg_ui_soc_decimal_deinit] ui_soc:%d", chip->ui_soc);
	chip->ui_soc_integer = 0;
	chip->ui_soc_decimal = 0;
	chip->init_decimal_ui_soc = 0;
}
#define MIN_DECIMAL_CURRENT 2000
static void oplus_chg_show_ui_soc_decimal(struct work_struct *work)
{
	struct oplus_chg_chip *chip = g_charger_chip;
	int speed, icharging;
	int ratio = 1;
	/*update the battery data*/
	if (oplus_warp_get_fastchg_started() == true) {
		chip->batt_rm =  oplus_gauge_get_prev_remaining_capacity() * chip->vbatt_num;
		chip->batt_fcc =  oplus_gauge_get_prev_batt_fcc() * chip->vbatt_num;
		chip->icharging = oplus_gauge_get_batt_current();
	} else {
		chip->batt_rm =  oplus_gauge_get_remaining_capacity() * chip->vbatt_num;
		chip->batt_fcc =  oplus_gauge_get_batt_fcc() * chip->vbatt_num;
		chip->icharging = oplus_gauge_get_batt_current();
	}
	icharging = chip->icharging * (-1);

	/*calculate the speed*/
	if(chip->ui_soc - chip->soc > 5) {
		ratio = 2;
	} else {
		ratio = 1;
	}
	if(icharging > 0) {
		speed = 100000 * icharging * UPDATE_TIME * chip->vbatt_num / (chip->batt_fcc * 3600) / ratio;
		pr_err("[oplus_chg_show_ui_soc_decimal] icharging = %d, batt_fcc :%d", chip->icharging, chip->batt_fcc);
	} else {
		/*speed = chip->ui_soc_decimal_speedmin / ratio;*/
		speed = 0;
		if(chip->batt_full) {
			speed = chip->ui_soc_decimal_speedmin;
		}
	}
	if (speed > 500) {
		speed = 500;
	}
	chip->ui_soc_decimal += speed;
	pr_err("[oplus_chg_ui_soc_decimal]chip->ui_soc_decimal+chip_ui_soc: %d , speed: %d, soc :%d\n ",
		(chip->ui_soc_decimal + chip->ui_soc_integer), speed , ((chip->batt_rm * 10000) / chip->batt_fcc));
	if(chip->ui_soc_integer + chip->ui_soc_decimal >= 100000) {
		chip->ui_soc_integer = 100000;
		chip->ui_soc_decimal = 0;
	}

	if(chip->calculate_decimal_time<= MAX_UI_DECIMAL_TIME) {
		chip->calculate_decimal_time++;
	   	schedule_delayed_work(&chip->ui_soc_decimal_work, msecs_to_jiffies(UPDATE_TIME * 1000));
	} else {
		oplus_chg_ui_soc_decimal_deinit();
	}
}

static int charging_limit_time_show(struct seq_file *seq_filp, void *v)
{
	seq_printf(seq_filp, "%d\n", g_charger_chip->limits.max_chg_time_sec);
	return 0;
}
static int charging_limit_time_open(struct inode *inode, struct file *file)
{
	int ret;
	ret = single_open(file, charging_limit_time_show, NULL);
	return ret;
}

static ssize_t charging_limit_time_write(struct file *filp,
		const char __user *buff, size_t len, loff_t *data)
{
	int limit_time;
	char temp[16];

	if (len > sizeof(temp)) {
		return -EINVAL;
	}
	if (copy_from_user(temp, buff, len)) {
		pr_err("charging_limit_time_write error.\n");
		return -EFAULT;
	}
	sscanf(temp, "%d", &limit_time);
	if (g_charger_chip) {
		g_charger_chip->limits.max_chg_time_sec = limit_time;
		printk(KERN_EMERG"charging_feature:max_chg_time_sec = %d\n",
			g_charger_chip->limits.max_chg_time_sec);
	}
	return len;
}

static const struct file_operations charging_limit_time_fops = {
	.open = charging_limit_time_open,
	.write = charging_limit_time_write,
	.read = seq_read,
};
static int charging_limit_current_show(struct seq_file *seq_filp, void *v)
{
	seq_printf(seq_filp, "%d\n", g_charger_chip->limits.input_current_led_ma_high);
	seq_printf(seq_filp, "%d\n", g_charger_chip->limits.input_current_led_ma_warm);
	seq_printf(seq_filp, "%d\n", g_charger_chip->limits.input_current_led_ma_normal);
	return 0;
}
static int charging_limit_current_open(struct inode *inode, struct file *file)
{
	int ret;
	ret = single_open(file, charging_limit_current_show, NULL);
	return ret;
}

static ssize_t charging_limit_current_write(struct file *filp,
		const char __user *buff, size_t len, loff_t *data)
{
	int limit_current;
	char temp[16];

	if (len > sizeof(temp)) {
		return -EINVAL;
	}
	if (copy_from_user(temp, buff, len)) {
		pr_err("charging_limit_current_write error.\n");
		return -EFAULT;
	}
	sscanf(temp, "%d", &limit_current);
	if (g_charger_chip) {
		g_charger_chip->limits.input_current_led_ma_high = limit_current;
		g_charger_chip->limits.input_current_led_ma_warm = limit_current;
		g_charger_chip->limits.input_current_led_ma_normal = limit_current;
		printk(KERN_EMERG"charging_feature:limit_current = %d\n",limit_current);
	}
	return len;
}

static const struct file_operations charging_limit_current_fops = {
	.open = charging_limit_current_open,
	.write = charging_limit_current_write,
	.read = seq_read,
};

static void init_proc_charging_feature(void)
{
	struct proc_dir_entry *p_time = NULL;
	struct proc_dir_entry *p_current = NULL;

	p_time = proc_create("charging_limit_time", 0664, NULL,
			&charging_limit_time_fops);
	if (!p_time) {
		pr_err("proc_create charging_feature_fops fail!\n");
	}
	p_current = proc_create("charging_limit_current", 0664, NULL,
			&charging_limit_current_fops);
	if (!p_current) {
		pr_err("proc_create charging_feature_fops fail!\n");
	}
}

/*ye.zhang add end*/
static void mmi_adapter_in_work_func(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_chip *chip
		= container_of(dwork, struct oplus_chg_chip, mmi_adapter_in_work);
	chip->mmi_fastchg = 1;
	charger_xlog_printk(CHG_LOG_CRTI, "  mmi_fastchg\n");
}

static void oplus_mmi_fastchg_in(struct oplus_chg_chip *chip)
{
	charger_xlog_printk(CHG_LOG_CRTI, "  call\n");
	schedule_delayed_work(&chip->mmi_adapter_in_work,
	round_jiffies_relative(msecs_to_jiffies(2000)));
}

static void oplus_chg_awake_init(struct oplus_chg_chip *chip)
{
	if (!chip) {
		return;
	}
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0))
	wake_lock_init(&chip->suspend_lock, WAKE_LOCK_SUSPEND, "battery suspend wakelock");

#else
	chip->suspend_ws = wakeup_source_register(NULL, "battery suspend wakelock");
#endif
}

static void oplus_chg_set_awake(struct oplus_chg_chip *chip, bool awake)
{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0))
	if (chip->unwakelock_chg == 1 && awake == true) {
		charger_xlog_printk(CHG_LOG_CRTI,
			"error, unwakelock testing, can not set wakelock.\n");
		return;
	}

	if (awake){
		wake_lock(&chip->suspend_lock);
	} else {
		wake_unlock(&chip->suspend_lock);
	}
#else
	static bool pm_flag = false;

	if (chip->unwakelock_chg == 1 && awake == true) {
		charger_xlog_printk(CHG_LOG_CRTI,
			"error, unwakelock testing, can not set wakelock.\n");
		return;
	}

	if (!chip || !chip->suspend_ws)
		return;

	if (awake && !pm_flag) {
		pm_flag = true;
		__pm_stay_awake(chip->suspend_ws);
	} else if (!awake && pm_flag) {
		__pm_relax(chip->suspend_ws);
		pm_flag = false;
	}
#endif
}

static int __ref shortc_thread_main(void *data)
{
	struct oplus_chg_chip *chip = data;
#ifndef CONFIG_OPLUS_CHG_GKI_SUPPORT
	struct cred *new;
#endif
	int rc = 0;

#ifndef CONFIG_OPLUS_CHG_GKI_SUPPORT
	new = prepare_creds();
	if (!new) {
		chg_err("init err\n");
		rc = -1;
		return rc;
	}
	new->fsuid = new->euid = KUIDT_INIT(1000);
	commit_creds(new);
#endif
	while (!kthread_should_stop()) {
		set_current_state(TASK_RUNNING);
		oplus_chg_short_c_battery_check(chip);
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule();
	}
	return rc;
}

#ifdef OPLUS_CHG_OP_DEF
#ifndef CONFIG_OPLUS_CHG_OOS
static void oplus_chg_led_power_on_report_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_chip *chip = container_of(dwork, struct oplus_chg_chip,
							led_power_on_report_work);
	chip->led_on = true;
	chip->led_on_change = true;
	if (is_usb_ocm_available(chip))
		oplus_chg_anon_mod_event(chip->usb_ocm, OPLUS_CHG_EVENT_LCD_ON);
	if (is_wls_ocm_available(chip))
		oplus_chg_anon_mod_event(chip->wls_ocm, OPLUS_CHG_EVENT_LCD_ON);
}
#endif
#endif

#ifdef OPLUS_CHG_OP_DEF
int oplus_chg_get_charger_voltage(void);
int oplus_chg_get_vph_voltage(void);
int oplus_chg_get_hw_detect_status(void);

enum lcm_en_status {
	LCM_EN_DEAFULT = 1,
	LCM_EN_ENABLE,
	LCM_EN_DISABLE,
};
static void oplus_chg_ctrl_lcm_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_chip *chip
		= container_of(dwork, struct oplus_chg_chip, ctrl_lcm_frequency);
	static int lcm_en_flag = LCM_EN_DEAFULT;

	if (((oplus_chg_get_charger_voltage() > CHG_VUSBIN_VOL_THR))
		|| (oplus_chg_is_wls_online(chip))) {
		if (chip->sw_full) {
			if (lcm_en_flag != LCM_EN_ENABLE) {
				lcm_en_flag = LCM_EN_ENABLE;
				chip->chg_ops->chg_lcm_en(true);
				charger_xlog_printk(CHG_LOG_CRTI, " lcm_en_flag:%d\n", lcm_en_flag);
			}
		} else {
			if (lcm_en_flag != LCM_EN_DISABLE) {
				lcm_en_flag = LCM_EN_DISABLE;
				chip->chg_ops->chg_lcm_en(false);
				charger_xlog_printk(CHG_LOG_CRTI, " lcm_en_flag:%d\n", lcm_en_flag);
			}
		}

		mod_delayed_work(system_highpri_wq, &chip->ctrl_lcm_frequency,
				 OPLUS_CHG_UPDATE_INTERVAL(oplus_chg_update_slow(chip)));
	} else {
			if (lcm_en_flag != LCM_EN_ENABLE) {
				lcm_en_flag = LCM_EN_ENABLE;
				chip->chg_ops->chg_lcm_en(true);
				charger_xlog_printk(CHG_LOG_CRTI, " lcm_en_flag:%d\n", lcm_en_flag);
			}
	}
}

static void oplus_check_abnormal_voltage_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_chip *chip
		= container_of(dwork, struct oplus_chg_chip, check_abnormal_voltage_work);

	if (!chip) {
		dev_err(chip->dev, "chip null\n");
		return;
	}
	oplus_check_ovp_status(chip);
}

static void oplus_recovery_chg_type_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_chip *chg
		= container_of(dwork, struct oplus_chg_chip, recovery_chg_type_work);

	if (!chg) {
		dev_err(chg->dev, "chip null\n");
		return;
	}
	if (chg->hw_detected && chg->abnormal_volt_detected && (chg->charger_volt >= VOLT_HIGH_VBUS_VALUE)) {
		oplus_warp_reset_fastchg_after_usbout();
		oplus_chg_variables_reset(chg, false);
		mdelay(600);
		chg->chg_ops->rerun_apsd();
		mdelay(600);
		chg->chg_redetect_charger_type = true;
		schedule_delayed_work(&chg->update_work, msecs_to_jiffies(1));
		return;
	}
}

#endif

int oplus_chg_init(struct oplus_chg_chip *chip)
{
	int rc = 0;
	char *thread_name = "shortc_thread";

#ifndef CONFIG_OPLUS_CHG_GKI_SUPPORT
	struct power_supply *usb_psy;
	struct power_supply *batt_psy;
#ifndef CONFIG_OPLUS_CHG_OOS
	struct power_supply *ac_psy;
#endif /* CONFIG_OPLUS_CHG_OOS */
#endif /* CONFIG_OPLUS_CHG_GKI_SUPPORT */

	if (!chip->chg_ops) {
		dev_err(chip->dev, "charger operations cannot be NULL\n");
		return -1;
	}
	oplus_chg_variables_init(chip);
	oplus_get_smooth_soc_switch(chip);
	oplus_chg_get_battery_data(chip);
#ifndef CONFIG_OPLUS_CHG_GKI_SUPPORT
	usb_psy = power_supply_get_by_name("usb");
	if (!usb_psy) {
		dev_err(chip->dev, "USB psy not found; deferring probe\n");
		/*return -EPROBE_DEFER;*/
		goto power_psy_reg_failed;
	}
	chip->usb_psy = usb_psy;
#ifndef CONFIG_OPLUS_CHG_OOS
	ac_psy = power_supply_get_by_name("ac");
	if (!ac_psy) {
		dev_err(chip->dev, "ac psy not found; deferring probe\n");
		goto power_psy_reg_failed;
	}
	chip->ac_psy = ac_psy;
#endif
	batt_psy = power_supply_get_by_name("battery");
	if (!batt_psy) {
		dev_err(chip->dev, "battery psy not found; deferring probe\n");
		goto power_psy_reg_failed;
	}
	chip->batt_psy = batt_psy;
#endif


#ifndef CONFIG_OPLUS_CHARGER_MTK
	chip->pmic_spmi.psy_registered = true;
#endif

	g_charger_chip = chip;
	oplus_chg_awake_init(chip);
	chip->shortc_thread = kthread_create(shortc_thread_main, (void *)chip, thread_name);
	if (!chip->shortc_thread) {
		chg_err("Can't create shortc_thread\n");
		rc = -EPROBE_DEFER;
		goto power_psy_reg_failed;
	}
#ifdef OPLUS_CHG_OP_DEF
	mutex_init(&chip->update_work_lock);
	spin_lock_init(&chip->strategy_lock);

	INIT_DELAYED_WORK(&chip->ctrl_lcm_frequency, oplus_chg_ctrl_lcm_work);
	INIT_DELAYED_WORK(&chip->check_abnormal_voltage_work, oplus_check_abnormal_voltage_work);
	INIT_DELAYED_WORK(&chip->recovery_chg_type_work, oplus_recovery_chg_type_work);
#endif
	INIT_DELAYED_WORK(&chip->update_work, oplus_chg_update_work);
	INIT_DELAYED_WORK(&chip->ui_soc_decimal_work, oplus_chg_show_ui_soc_decimal);
	INIT_DELAYED_WORK(&chip->reset_adapter_work, oplus_chg_reset_adapter_work);
#ifdef OPLUS_CHG_OP_DEF
#ifndef CONFIG_OPLUS_CHG_OOS
	INIT_DELAYED_WORK(&chip->led_power_on_report_work, oplus_chg_led_power_on_report_work);
#endif
#endif

#ifndef CONFIG_OPLUS_CHG_OOS
// #ifdef CONFIG_FB nick.hu todo
	chip->chg_fb_notify.notifier_call = fb_notifier_callback;
#ifdef CONFIG_DRM_MSM
	rc = msm_drm_register_client(&chip->chg_fb_notify);
#else
	rc = fb_register_client(&chip->chg_fb_notify);
#endif /*CONFIG_DRM_MSM*/
	if (rc) {
		pr_err("Unable to register chg_fb_notify: %d\n", rc);
	}
// #endif /* CONFIG_FB */
#endif /* CONFIG_OPLUS_CHG_OOS */

	oplus_chg_debug_info_init();
	init_proc_chg_log();
	init_proc_chg_cycle();
	init_proc_critical_log();
	init_proc_tbatt_pwroff();
	init_proc_batt_param_noplug();

#ifdef CONFIG_OPLUS_RTC_DET_SUPPORT
	init_proc_rtc_det();
	init_proc_vbat_low_det();
#endif

	init_proc_charging_feature();
	rc = init_ui_soc_decimal_proc(chip);
	rc = init_charger_proc(chip);
	/*ye.zhang add end*/
#ifdef OPLUS_CHG_OP_DEF
	oplus_chg_get_battery_data(chip);
#endif
	schedule_delayed_work(&chip->update_work, OPLUS_CHG_UPDATE_INIT_DELAY);
	INIT_DELAYED_WORK(&chip->mmi_adapter_in_work, mmi_adapter_in_work_func);
	chip->shell_themal = thermal_zone_get_zone_by_name("shell_back");
	if (IS_ERR(chip->shell_themal)) {
		chg_err("Can't get shell_back\n");
	}
	charger_xlog_printk(CHG_LOG_CRTI, " end\n");
	return 0;

power_psy_reg_failed:
#ifndef CONFIG_OPLUS_CHG_GKI_SUPPORT
if (chip->ac_psy)
		power_supply_unregister(chip->ac_psy);
if (chip->usb_psy)
		power_supply_unregister(chip->usb_psy);
if (chip->batt_psy)
		power_supply_unregister(chip->batt_psy);
#endif /* CONFIG_OPLUS_CHG_GKI_SUPPORT */
	charger_xlog_printk(CHG_LOG_CRTI, " Failed, rc = %d\n", rc);
	return rc;
}


/*--------------------------------------------------------*/
int oplus_chg_parse_swarp_dt(struct oplus_chg_chip *chip)
{
	int rc;
	struct device_node *node = chip->dev->of_node;

	if (!node) {
		dev_err(chip->dev, "device tree info. missing\n");
		return -EINVAL;
	}
	rc = of_property_read_u32(node, "qcom,vbatt_num", &chip->vbatt_num);
	if (rc) {
		chip->vbatt_num = 1;
	}
	rc = of_property_read_u32(node, "qcom,warp_project", &chip->warp_project);
	if (rc < 0) {
		chip->warp_project = 0;
	}
	chip->platform_fg_flag = of_property_read_bool(node, "qcom,platform_fg_flag");
	chg_err("oplus_parse_swarp_dt, chip->vbatt_num = %d,chip->warp_project = %d.\n",
			chip->vbatt_num,chip->warp_project);
	return 0;
}

void oplus_chg_aging_ffc_variable_reset(struct oplus_chg_chip *chip)
{
	chip->limits.ffc1_normal_vfloat_sw_limit = chip->limits.default_ffc1_normal_vfloat_sw_limit;
	chip->limits.ffc1_warm_vfloat_sw_limit = chip->limits.default_ffc1_warm_vfloat_sw_limit;
	chip->limits.ffc2_normal_vfloat_sw_limit = chip->limits.default_ffc2_normal_vfloat_sw_limit;
	chip->limits.ffc2_warm_vfloat_sw_limit = chip->limits.default_ffc2_warm_vfloat_sw_limit;
}

void oplus_chg_get_aging_ffc_offset(struct oplus_chg_chip *chip,
		int *ffc1_offset, int *ffc2_offset)
{
	int batt_cc = 0;

	if (!chip || !ffc1_offset || !ffc2_offset)
		return;

	*ffc1_offset = 0;
	*ffc2_offset = 0;

	if (chip->aging_ffc_version == AGING_FFC_NOT_SUPPORT)
		return;

	if (chip->debug_batt_cc)
		batt_cc = chip->debug_batt_cc;
	else
		batt_cc = chip->batt_cc;

	if (chip->vbatt_num == 2) {
		if (batt_cc >= AGING2_STAGE_CYCLE) {
			*ffc1_offset = AGING2_FFC1_DOUBLE_OFFSET_MV;
			*ffc2_offset = AGING2_FFC2_DOUBLE_OFFSET_MV;
		} else if (batt_cc >= AGING1_STAGE_CYCLE) {
			*ffc1_offset = AGING1_FFC1_DOUBLE_OFFSET_MV;
			*ffc2_offset = AGING1_FFC2_DOUBLE_OFFSET_MV;
		}
	} else {
		if (batt_cc >= AGING2_STAGE_CYCLE) {
			*ffc1_offset = AGING2_FFC1_SINGLE_OFFSET_MV;
			*ffc2_offset = AGING2_FFC2_SINGLE_OFFSET_MV;
		} else if (batt_cc >= AGING1_STAGE_CYCLE) {
			*ffc1_offset = AGING1_FFC1_SINGLE_OFFSET_MV;
			*ffc2_offset = AGING1_FFC2_SINGLE_OFFSET_MV;
		}
	}
}

void oplus_chg_aging_ffc_action(struct oplus_chg_chip *chip, bool ffc1_stage)
{
	int ffc1_voltage_offset = 0;
	int ffc2_voltage_offset = 0;

	if (chip->aging_ffc_version == AGING_FFC_NOT_SUPPORT)
		return;

	oplus_chg_aging_ffc_variable_reset(chip);

	oplus_chg_get_aging_ffc_offset(chip, &ffc1_voltage_offset, &ffc2_voltage_offset);

	chip->limits.ffc1_normal_vfloat_sw_limit = chip->limits.default_ffc1_normal_vfloat_sw_limit + ffc1_voltage_offset;
	chip->limits.ffc1_warm_vfloat_sw_limit = chip->limits.default_ffc1_warm_vfloat_sw_limit + ffc1_voltage_offset;
	chip->limits.ffc2_normal_vfloat_sw_limit = chip->limits.default_ffc2_normal_vfloat_sw_limit + ffc2_voltage_offset;
	chip->limits.ffc2_warm_vfloat_sw_limit = chip->limits.default_ffc2_warm_vfloat_sw_limit + ffc2_voltage_offset;

	chg_err("batt_cc=%d %d [%d %d %d %d]\n", chip->debug_batt_cc, chip->batt_cc,
			chip->limits.ffc1_normal_vfloat_sw_limit,
			chip->limits.ffc1_warm_vfloat_sw_limit,
			chip->limits.ffc2_normal_vfloat_sw_limit,
			chip->limits.ffc2_warm_vfloat_sw_limit);
}

int oplus_chg_parse_charger_dt(struct oplus_chg_chip *chip)
{
	int rc;
	struct device_node *node = chip->dev->of_node;
	int batt_cold_degree_negative;
	int batt_removed_degree_negative;
#ifdef OPLUS_CHG_OP_DEF
	int i;
#endif

	if (!node) {
		dev_err(chip->dev, "device tree info. missing\n");
		return -EINVAL;
	}

	rc = of_property_read_u32(node, "qcom,input_current_charger_ma",
			&chip->limits.input_current_charger_ma);
	if (rc) {
		chip->limits.input_current_charger_ma
			= OPCHG_INPUT_CURRENT_LIMIT_CHARGER_MA;
	}
	rc = of_property_read_u32(node, "qcom,pd_input_current_charger_ma",
			&chip->limits.pd_input_current_charger_ma);
	if (rc) {
		chip->limits.pd_input_current_charger_ma
			= OPCHG_INPUT_CURRENT_LIMIT_CHARGER_MA;
	}
	rc = of_property_read_u32(node, "qcom,qc_input_current_charger_ma",
			&chip->limits.qc_input_current_charger_ma);
	if (rc) {
		chip->limits.qc_input_current_charger_ma
			= OPCHG_INPUT_CURRENT_LIMIT_CHARGER_MA;
	}

	rc = of_property_read_u32(node, "qcom,input_current_usb_ma",
			&chip->limits.input_current_usb_ma);
	if (rc) {
		chip->limits.input_current_usb_ma = OPCHG_INPUT_CURRENT_LIMIT_USB_MA;
	}

	rc = of_property_read_u32(node, "qcom,input_current_cdp_ma",
			&chip->limits.input_current_cdp_ma);
	if (rc) {
		chip->limits.input_current_cdp_ma = OPCHG_INPUT_CURRENT_LIMIT_USB_MA;
	}

	if (get_eng_version() == HIGH_TEMP_AGING) {
		chip->limits.input_current_led_ma = OPCHG_INPUT_CURRENT_LIMIT_CHARGER_MA;
		chg_err(" CONFIG_HIGH_TEMP_VERSION enable here,led on current 2A \n");
	} else {
		rc = of_property_read_u32(node, "qcom,input_current_led_ma",
			&chip->limits.input_current_led_ma);
		if (rc) {
			chip->limits.input_current_led_ma = OPCHG_INPUT_CURRENT_LIMIT_LED_MA;
		}
	}

	if (get_eng_version() == HIGH_TEMP_AGING) {
		chip->limits.input_current_led_ma_high = OPCHG_INPUT_CURRENT_LIMIT_CHARGER_MA;
		chg_err(" CONFIG_HIGH_TEMP_VERSION enable here, led_ma_high on current 2A \n");
	} else {
		rc = of_property_read_u32(node, "qcom,input_current_led_ma_high",
			&chip->limits.input_current_led_ma_high);
		if (rc) {
			chip->limits.input_current_led_ma_high = chip->limits.input_current_led_ma;
		}
	}

	rc = of_property_read_u32(node, "qcom,led_high_bat_decidegc",
			&chip->limits.led_high_bat_decidegc);
	if (rc) {
		chip->limits.led_high_bat_decidegc = 370;
	}

	if (get_eng_version() == HIGH_TEMP_AGING) {
		chip->limits.input_current_led_ma_warm = OPCHG_INPUT_CURRENT_LIMIT_CHARGER_MA;
		chg_err(" CONFIG_HIGH_TEMP_VERSION enable here, led_ma_warm on current 2A \n");
	} else {
		rc = of_property_read_u32(node, "qcom,input_current_led_ma_warm",
			&chip->limits.input_current_led_ma_warm);
		if (rc) {
			chip->limits.input_current_led_ma_warm = chip->limits.input_current_led_ma;
		}
	}

	rc = of_property_read_u32(node, "qcom,led_warm_bat_decidegc",
			&chip->limits.led_warm_bat_decidegc);
	if (rc) {
		chip->limits.led_warm_bat_decidegc = 350;
	}

	if (get_eng_version() == HIGH_TEMP_AGING) {
		chip->limits.input_current_led_ma_normal = OPCHG_INPUT_CURRENT_LIMIT_CHARGER_MA;
		chg_err(" CONFIG_HIGH_TEMP_VERSION enable here, led_ma_normal on current 2A \n");
	} else {
		rc = of_property_read_u32(node, "qcom,input_current_led_ma_normal",
			&chip->limits.input_current_led_ma_normal);
		if (rc) {
			chip->limits.input_current_led_ma_normal = chip->limits.input_current_led_ma;
		}
	}

	rc = of_property_read_u32(node, "qcom,input_current_camera_ma",
			&chip->limits.input_current_camera_ma);
	if (rc) {
		chip->limits.input_current_camera_ma = OPCHG_INPUT_CURRENT_LIMIT_CAMERA_MA;
	}
#ifdef OPLUS_CHG_OP_DEF
	rc = of_property_read_u32(node, "qcom,batt_current_camera_ma",
			&chip->limits.batt_current_camera_ma);
	if (rc) {
		chip->limits.batt_current_camera_ma = OPCHG_BATT_CURRENT_LIMIT_CAMERA_MA;
	}
	rc = of_property_read_u32(node, "qcom,fast_current_camera_level",
			&chip->limits.fast_current_camera_level);
	if (rc) {
		chip->limits.fast_current_camera_level = OPCHG_FAST_CURRENT_LIMIT_CAMERA_LEVEL;
	}
#endif
	chip->limits.iterm_disabled = of_property_read_bool(node, "qcom,iterm_disabled");
	rc = of_property_read_u32(node, "qcom,iterm_ma", &chip->limits.iterm_ma);
	if (rc < 0) {
		chip->limits.iterm_ma = -EINVAL;
	}
	chip->smart_charging_screenoff = of_property_read_bool(node, "qcom,smart_charging_screenoff");
	rc = of_property_read_u32(node, "qcom,input_current_calling_ma",
			&chip->limits.input_current_calling_ma);
	if (rc) {
		chip->limits.input_current_calling_ma = OPCHG_INPUT_CURRENT_LIMIT_CALLING_MA;
	}
#ifdef OPLUS_CHG_OP_DEF
	rc = of_property_read_u32(node, "qcom,batt_current_calling_ma",
			&chip->limits.batt_current_calling_ma);
	if (rc) {
		chip->limits.batt_current_calling_ma = OPCHG_BATT_CURRENT_LIMIT_CALLING_MA;
	}
	rc = of_property_read_u32(node, "qcom,fast_current_calling_level",
			&chip->limits.fast_current_calling_level);
	if (rc) {
		chip->limits.fast_current_calling_level = OPCHG_FAST_CURRENT_LIMIT_CALLING_LEVEL;
	}
#endif
	rc = of_property_read_u32(node, "qcom,recharge-mv",
			&chip->limits.recharge_mv);
	if (rc < 0) {
		chip->limits.recharge_mv = -EINVAL;
	}

	rc = of_property_read_u32(node, "qcom,usb_high_than_bat_decidegc",
			&chip->limits.usb_high_than_bat_decidegc);
	if (rc < 0) {
		chip->limits.usb_high_than_bat_decidegc = 100;
	}
	chg_err("usb_high_than_bat_decidegc:%d\n", chip->limits.usb_high_than_bat_decidegc);

	/*-19C*/
	rc = of_property_read_u32(node, "qcom,removed_bat_decidegc",
			&batt_removed_degree_negative);
	if (rc < 0) {
		chip->limits.removed_bat_decidegc = -19;
	} else {
		chip->limits.removed_bat_decidegc = -batt_removed_degree_negative;
	}
	/*-3~0 C*/

	if (get_eng_version() == HIGH_TEMP_AGING) {
		chg_err(" CONFIG_HIGH_TEMP_VERSION enable here,disable low tbat chg \n");
		batt_cold_degree_negative = 170;
		chip->limits.cold_bat_decidegc = -batt_cold_degree_negative;
	} else {
		chg_err(" CONFIG_HIGH_TEMP_VERSION disabled\n");
		rc = of_property_read_u32(node, "qcom,cold_bat_decidegc", &batt_cold_degree_negative);
		if (rc < 0) {
			chip->limits.cold_bat_decidegc = -EINVAL;
		} else {
			chip->limits.cold_bat_decidegc = -batt_cold_degree_negative;
		}
	}

	rc = of_property_read_u32(node, "qcom,temp_cold_vfloat_mv",
			&chip->limits.temp_cold_vfloat_mv);
	if (rc < 0) {
		chg_err(" temp_cold_vfloat_mv fail\n");
	}
	rc = of_property_read_u32(node, "qcom,temp_cold_fastchg_current_ma",
			&chip->limits.temp_cold_fastchg_current_ma);
	if (rc < 0) {
		chg_err(" temp_cold_fastchg_current_ma fail\n");
	}
	/*0~5 C*/
	rc = of_property_read_u32(node, "qcom,little_cold_bat_decidegc",
			&chip->limits.little_cold_bat_decidegc);
	if (rc < 0) {
		chip->limits.little_cold_bat_decidegc = -EINVAL;
	}
	rc = of_property_read_u32(node, "qcom,temp_little_cold_vfloat_mv",
			&chip->limits.temp_little_cold_vfloat_mv);
	if (rc < 0) {
		chip->limits.temp_little_cold_vfloat_mv = -EINVAL;
	}
	rc = of_property_read_u32(node, "qcom,temp_little_cold_fastchg_current_ma",
			&chip->limits.temp_little_cold_fastchg_current_ma);
	if (rc < 0) {
		chip->limits.temp_little_cold_fastchg_current_ma = -EINVAL;
	}
	rc = of_property_read_u32(node, "qcom,temp_little_cold_fastchg_current_ma_high",
			&chip->limits.temp_little_cold_fastchg_current_ma_high);
	if (rc < 0) {
		chip->limits.temp_little_cold_fastchg_current_ma_high
			= chip->limits.temp_little_cold_fastchg_current_ma;
	}
	rc = of_property_read_u32(node, "qcom,temp_little_cold_fastchg_current_ma_low",
			&chip->limits.temp_little_cold_fastchg_current_ma_low);
	if (rc < 0) {
		chip->limits.temp_little_cold_fastchg_current_ma_low
			= chip->limits.temp_little_cold_fastchg_current_ma;
	}
	rc = of_property_read_u32(node, "qcom,pd_temp_little_cold_fastchg_current_ma_high",
			&chip->limits.pd_temp_little_cold_fastchg_current_ma_high);
	if (rc < 0) {
		chip->limits.pd_temp_little_cold_fastchg_current_ma_high
			= chip->limits.temp_little_cold_fastchg_current_ma_high;
	}
	rc = of_property_read_u32(node, "qcom,pd_temp_little_cold_fastchg_current_ma_low",
			&chip->limits.pd_temp_little_cold_fastchg_current_ma_low);
	if (rc < 0) {
		chip->limits.pd_temp_little_cold_fastchg_current_ma_low
			= chip->limits.temp_little_cold_fastchg_current_ma_low;
	}
	rc = of_property_read_u32(node, "qcom,qc_temp_little_cold_fastchg_current_ma_high",
			&chip->limits.qc_temp_little_cold_fastchg_current_ma_high);
	if (rc < 0) {
		chip->limits.qc_temp_little_cold_fastchg_current_ma_high
			= chip->limits.temp_little_cold_fastchg_current_ma_high;
	}
	rc = of_property_read_u32(node, "qcom,qc_temp_little_cold_fastchg_current_ma_low",
			&chip->limits.qc_temp_little_cold_fastchg_current_ma_low);
	if (rc < 0) {
		chip->limits.qc_temp_little_cold_fastchg_current_ma_low
			= chip->limits.temp_little_cold_fastchg_current_ma_low;
	}

	/*5~12 C*/
	rc = of_property_read_u32(node, "qcom,cool_bat_decidegc",
			&chip->limits.cool_bat_decidegc);
	if (rc < 0) {
		chip->limits.cool_bat_decidegc = -EINVAL;
	}
	rc = of_property_read_u32(node, "qcom,temp_cool_vfloat_mv",
			&chip->limits.temp_cool_vfloat_mv);
	if (rc < 0) {
		chip->limits.temp_cool_vfloat_mv = -EINVAL;
	}
	rc = of_property_read_u32(node, "qcom,temp_cool_fastchg_current_ma_high",
			&chip->limits.temp_cool_fastchg_current_ma_high);
	if (rc < 0) {
		chip->limits.temp_cool_fastchg_current_ma_high = -EINVAL;
	}
	rc = of_property_read_u32(node, "qcom,temp_cool_fastchg_current_ma_low",
			&chip->limits.temp_cool_fastchg_current_ma_low);
	if (rc < 0) {
		chip->limits.temp_cool_fastchg_current_ma_low = -EINVAL;
	}
	rc = of_property_read_u32(node, "qcom,pd_temp_cool_fastchg_current_ma_high",
			&chip->limits.pd_temp_cool_fastchg_current_ma_high);
	if (rc < 0) {
		chip->limits.pd_temp_cool_fastchg_current_ma_high
			= chip->limits.temp_cool_fastchg_current_ma_high;
	}
	rc = of_property_read_u32(node, "qcom,pd_temp_cool_fastchg_current_ma_low",
			&chip->limits.pd_temp_cool_fastchg_current_ma_low);
	if (rc < 0) {
		chip->limits.pd_temp_cool_fastchg_current_ma_low
			= chip->limits.temp_cool_fastchg_current_ma_low;
	}
	rc = of_property_read_u32(node, "qcom,qc_temp_cool_fastchg_current_ma_high",
			&chip->limits.qc_temp_cool_fastchg_current_ma_high);
	if (rc < 0) {
		chip->limits.qc_temp_cool_fastchg_current_ma_high
			= chip->limits.temp_cool_fastchg_current_ma_high;
	}
	rc = of_property_read_u32(node, "qcom,qc_temp_cool_fastchg_current_ma_low",
			&chip->limits.qc_temp_cool_fastchg_current_ma_low);
	if (rc < 0) {
		chip->limits.qc_temp_cool_fastchg_current_ma_low
			= chip->limits.temp_cool_fastchg_current_ma_low;
	}

	/*12~16 C*/
	rc = of_property_read_u32(node, "qcom,little_cool_bat_decidegc",
			&chip->limits.little_cool_bat_decidegc);
	if (rc < 0) {
		chip->limits.little_cool_bat_decidegc = -EINVAL;
	}
	rc = of_property_read_u32(node, "qcom,temp_little_cool_vfloat_mv",
			&chip->limits.temp_little_cool_vfloat_mv);
	if (rc < 0) {
		chip->limits.temp_little_cool_vfloat_mv = -EINVAL;
	}
	rc = of_property_read_u32(node, "qcom,temp_little_cool_fastchg_current_ma",
			&chip->limits.temp_little_cool_fastchg_current_ma);
	if (rc < 0) {
		chip->limits.temp_little_cool_fastchg_current_ma = -EINVAL;
	}

	rc = of_property_read_u32(node, "qcom,temp_little_cool_fastchg_current_ma_high",
			&chip->limits.temp_little_cool_fastchg_current_ma_high);
	if (rc < 0) {
		chip->limits.temp_little_cool_fastchg_current_ma_high = chip->limits.temp_little_cool_fastchg_current_ma;
	}

	rc = of_property_read_u32(node, "qcom,temp_little_cool_fastchg_current_ma_low",
			&chip->limits.temp_little_cool_fastchg_current_ma_low);
	if (rc < 0) {
		chip->limits.temp_little_cool_fastchg_current_ma_low = chip->limits.temp_little_cool_fastchg_current_ma;
	}

	rc = of_property_read_u32(node, "qcom,pd_temp_little_cool_fastchg_current_ma",
			&chip->limits.pd_temp_little_cool_fastchg_current_ma);
	if (rc < 0) {
		chip->limits.pd_temp_little_cool_fastchg_current_ma
			= chip->limits.temp_little_cool_fastchg_current_ma;
	}
	rc = of_property_read_u32(node, "qcom,qc_temp_little_cool_fastchg_current_ma",
			&chip->limits.qc_temp_little_cool_fastchg_current_ma);
	if (rc < 0) {
		chip->limits.qc_temp_little_cool_fastchg_current_ma
			= chip->limits.temp_little_cool_fastchg_current_ma;
	}

	/*16~45 C*/
	rc = of_property_read_u32(node, "qcom,normal_bat_decidegc",
			&chip->limits.normal_bat_decidegc);
	if (rc < 0) {
		chg_err(" normal_bat_decidegc fail\n");
	}
	rc = of_property_read_u32(node, "qcom,temp_normal_fastchg_current_ma",
			&chip->limits.temp_normal_fastchg_current_ma);
	if (rc) {
		chip->limits.temp_normal_fastchg_current_ma = OPCHG_FAST_CHG_MAX_MA;
	}
	rc = of_property_read_u32(node, "qcom,temp_normal_vfloat_mv",
			&chip->limits.temp_normal_vfloat_mv);
	if (rc < 0) {
		chip->limits.temp_normal_vfloat_mv = 4320;
	}
	rc = of_property_read_u32(node, "qcom,pd_temp_normal_fastchg_current_ma",
			&chip->limits.pd_temp_normal_fastchg_current_ma);
	if (rc) {
		chip->limits.pd_temp_normal_fastchg_current_ma = OPCHG_FAST_CHG_MAX_MA;
	}
	rc = of_property_read_u32(node, "qcom,qc_temp_normal_fastchg_current_ma",
			&chip->limits.qc_temp_normal_fastchg_current_ma);
	if (rc) {
		chip->limits.qc_temp_normal_fastchg_current_ma = OPCHG_FAST_CHG_MAX_MA;
	}

	/* 16C ~ 22C */
	rc = of_property_read_u32(node, "qcom,normal_phase1_bat_decidegc",
			&chip->limits.normal_phase1_bat_decidegc);
	if (rc < 0) {
		chg_err(" normal_phase1_bat_decidegc fail\n");
	}

	rc = of_property_read_u32(node, "qcom,temp_normal_phase1_vfloat_mv",
			&chip->limits.temp_normal_phase1_vfloat_mv);
	if (rc < 0) {
		chg_err(" temp_normal_phase1_vfloat_mv fail\n");
	}

	rc = of_property_read_u32(node, "qcom,temp_normal_phase1_fastchg_current_ma",
			&chip->limits.temp_normal_phase1_fastchg_current_ma);
	if (rc < 0) {
		chg_err(" temp_normal_phase1_fastchg_current_ma fail\n");
	}

	rc = of_property_read_u32(node, "qcom,normal_phase2_bat_decidegc",
			&chip->limits.normal_phase2_bat_decidegc);
	if (rc < 0) {
		chg_err(" normal_phase2_bat_decidegc fail\n");
	}

	rc = of_property_read_u32(node, "qcom,temp_normal_phase2_vfloat_mv",
			&chip->limits.temp_normal_phase2_vfloat_mv);
	if (rc < 0) {
		chg_err(" temp_normal_phase2_vfloat_mv fail\n");
	}

        rc = of_property_read_u32(node, "qcom,temp_normal_phase2_fastchg_current_ma_high",
			&chip->limits.temp_normal_phase2_fastchg_current_ma_high);
	if (rc < 0) {
		chg_err(" temp_normal_phase2_fastchg_current_ma_high fail\n");
	}


	rc = of_property_read_u32(node, "qcom,temp_normal_phase2_fastchg_current_ma_low",
			&chip->limits.temp_normal_phase2_fastchg_current_ma_low);
	if (rc < 0) {
		chg_err(" temp_normal_phase2_fastchg_current_ma_low fail\n");
	}

	rc = of_property_read_u32(node, "qcom,normal_phase3_bat_decidegc",
			&chip->limits.normal_phase3_bat_decidegc);
	if (rc < 0) {
		chg_err(" normal_phase3_bat_decidegc fail\n");
	}

	rc = of_property_read_u32(node, "qcom,temp_normal_phase3_vfloat_mv",
			&chip->limits.temp_normal_phase3_vfloat_mv);
	if (rc < 0) {
		chg_err(" temp_normal_phase3_vfloat_mv fail\n");
	}

	rc = of_property_read_u32(node, "qcom,temp_normal_phase3_fastchg_current_ma_high",
			&chip->limits.temp_normal_phase3_fastchg_current_ma_high);
	if (rc < 0) {
		chg_err(" temp_normal_phase3_fastchg_current_ma_high fail\n");
	}

	rc = of_property_read_u32(node, "qcom,temp_normal_phase3_fastchg_current_ma_low",
			&chip->limits.temp_normal_phase3_fastchg_current_ma_low);
	if (rc < 0) {
		chg_err(" temp_normal_phase3_fastchg_current_ma_low fail\n");
	}

	rc = of_property_read_u32(node, "qcom,normal_phase4_bat_decidegc",
			&chip->limits.normal_phase4_bat_decidegc);
	if (rc < 0) {
		chg_err(" normal_phase4_bat_decidegc fail\n");
	}

	rc = of_property_read_u32(node, "qcom,temp_normal_phase4_vfloat_mv",
			&chip->limits.temp_normal_phase4_vfloat_mv);
	if (rc < 0) {
		chg_err(" temp_normal_phase4_vfloat_mv fail\n");
	}

	rc = of_property_read_u32(node, "qcom,temp_normal_phase4_fastchg_current_ma_high",
			&chip->limits.temp_normal_phase4_fastchg_current_ma_high);
	if (rc < 0) {
		chg_err(" temp_normal_phase4_fastchg_current_ma_high fail\n");
	}

	rc = of_property_read_u32(node, "qcom,temp_normal_phase4_fastchg_current_ma_low",
			&chip->limits.temp_normal_phase4_fastchg_current_ma_low);
	if (rc < 0) {
		chg_err(" temp_normal_phase4_fastchg_current_ma_low fail\n");
	}

	rc = of_property_read_u32(node, "qcom,normal_phase5_bat_decidegc",
			&chip->limits.normal_phase5_bat_decidegc);
	if (rc < 0) {
		chg_err(" normal_phase5_bat_decidegc fail\n");
	}

	rc = of_property_read_u32(node, "qcom,temp_normal_phase5_vfloat_mv",
			&chip->limits.temp_normal_phase5_vfloat_mv);
	if (rc < 0) {
		chg_err(" temp_normal_phase5_vfloat_mv fail\n");
	}

	rc = of_property_read_u32(node, "qcom,temp_normal_phase5_fastchg_current_ma",
			&chip->limits.temp_normal_phase5_fastchg_current_ma);
	if (rc < 0) {
		chg_err(" temp_normal_phase5_fastchg_current_ma fail\n");
	}

	rc = of_property_read_u32(node, "qcom,normal_phase6_bat_decidegc",
			&chip->limits.normal_phase6_bat_decidegc);
	if (rc < 0) {
		chip->limits.normal_phase6_bat_decidegc = 420;
	}

	rc = of_property_read_u32(node, "qcom,temp_normal_phase6_vfloat_mv",
			&chip->limits.temp_normal_phase6_vfloat_mv);
	if (rc < 0) {
		chip->limits.temp_normal_phase6_vfloat_mv = chip->limits.temp_normal_phase5_vfloat_mv;
	}

	rc = of_property_read_u32(node, "qcom,temp_normal_phase6_fastchg_current_ma",
			&chip->limits.temp_normal_phase6_fastchg_current_ma);
	if (rc < 0) {
		chip->limits.temp_normal_phase6_fastchg_current_ma = chip->limits.temp_normal_phase5_fastchg_current_ma;
	}

	charger_xlog_printk(CHG_LOG_CRTI,
			"normal_phase1_bat_decidegc = %d, \
			temp_normal_phase1_vfloat_mv = %d, \
			temp_normal_phase1_fastchg_current_ma = %d, \
			normal_phase2_bat_decidegc = %d, \
			temp_normal_phase2_vfloat_mv = %d, \
			temp_normal_phase2_fastchg_current_ma_high = %d, \
			temp_normal_phase2_fastchg_current_ma_low = %d, \
			normal_phase3_bat_decidegc = %d, \
			temp_normal_phase3_vfloat_mv = %d, \
			temp_normal_phase3_fastchg_current_ma_high = %d, \
			temp_normal_phase3_fastchg_current_ma_low = %d, \
			normal_phase4_bat_decidegc = %d, \
			temp_normal_phase4_vfloat_mv = %d, \
			temp_normal_phase4_fastchg_current_ma_high = %d, \
			temp_normal_phase4_fastchg_current_ma_low = %d, \
			normal_phase5_bat_decidegc = %d, \
			temp_normal_phase5_vfloat_mv = %d, \
			temp_normal_phase5_fastchg_current_ma = %d, \
			normal_phase6_bat_decidegc = %d, \
			temp_normal_phase6_vfloat_mv = %d, \
			temp_normal_phase6_fastchg_current_ma = %d\n",
			chip->limits.normal_phase1_bat_decidegc,
			chip->limits.temp_normal_phase1_vfloat_mv,
			chip->limits.temp_normal_phase1_fastchg_current_ma,
			chip->limits.normal_phase2_bat_decidegc,
			chip->limits.temp_normal_phase2_vfloat_mv,
			chip->limits.temp_normal_phase2_fastchg_current_ma_high,
			chip->limits.temp_normal_phase2_fastchg_current_ma_low,
			chip->limits.normal_phase3_bat_decidegc,
			chip->limits.temp_normal_phase3_vfloat_mv,
			chip->limits.temp_normal_phase3_fastchg_current_ma_high,
			chip->limits.temp_normal_phase3_fastchg_current_ma_low,
			chip->limits.normal_phase4_bat_decidegc,
			chip->limits.temp_normal_phase4_vfloat_mv,
			chip->limits.temp_normal_phase4_fastchg_current_ma_high,
			chip->limits.temp_normal_phase4_fastchg_current_ma_low,
			chip->limits.normal_phase5_bat_decidegc,
			chip->limits.temp_normal_phase5_vfloat_mv,
			chip->limits.temp_normal_phase5_fastchg_current_ma,
			chip->limits.normal_phase6_bat_decidegc,
			chip->limits.temp_normal_phase6_vfloat_mv,
			chip->limits.temp_normal_phase6_fastchg_current_ma);

	/*45~55 C*/
	rc = of_property_read_u32(node, "qcom,warm_bat_decidegc",
			&chip->limits.warm_bat_decidegc);
	if (rc < 0) {
		chip->limits.warm_bat_decidegc = -EINVAL;
	}
	rc = of_property_read_u32(node, "qcom,temp_warm_vfloat_mv",
			&chip->limits.temp_warm_vfloat_mv);
	if (rc < 0) {
		chip->limits.temp_warm_vfloat_mv = -EINVAL;
	}
	rc = of_property_read_u32(node, "qcom,temp_warm_fastchg_current_ma",
			&chip->limits.temp_warm_fastchg_current_ma);
	if (rc < 0) {
		chip->limits.temp_warm_fastchg_current_ma = -EINVAL;
	}
	rc = of_property_read_u32(node, "qcom,pd_temp_warm_fastchg_current_ma",
			&chip->limits.pd_temp_warm_fastchg_current_ma);
	if (rc < 0) {
		chip->limits.pd_temp_warm_fastchg_current_ma
			= chip->limits.temp_warm_fastchg_current_ma;
	}
	rc = of_property_read_u32(node, "qcom,qc_temp_warm_fastchg_current_ma",
			&chip->limits.qc_temp_warm_fastchg_current_ma);
	if (rc < 0) {
		chip->limits.qc_temp_warm_fastchg_current_ma
			= chip->limits.temp_warm_fastchg_current_ma;
	}
	rc = of_property_read_u32(node, "qcom,temp_warm_fastchg_current_ma_led_on",
			&chip->limits.temp_warm_fastchg_current_ma_led_on);
	if (rc < 0) {
		chip->limits.temp_warm_fastchg_current_ma_led_on
			= chip->limits.temp_warm_fastchg_current_ma;
	}

	/*>55 C*/
	rc = of_property_read_u32(node, "qcom,hot_bat_decidegc",
			&chip->limits.hot_bat_decidegc);
	if (rc < 0) {
		chip->limits.hot_bat_decidegc = -EINVAL;
	}
	/*offset temperature, only for userspace, default 0*/
	rc = of_property_read_u32(node, "qcom,offset_temp", &chip->offset_temp);
	if (rc < 0) {
		chip->offset_temp = 0;
	}
	/*non standard battery*/
	rc = of_property_read_u32(node, "qcom,non_standard_vfloat_mv",
			&chip->limits.non_standard_vfloat_mv);
	if (rc < 0) {
		chip->limits.non_standard_vfloat_mv = -EINVAL;
	}
	rc = of_property_read_u32(node, "qcom,non_standard_fastchg_current_ma",
			&chip->limits.non_standard_fastchg_current_ma);
	if (rc < 0) {
		chip->limits.non_standard_fastchg_current_ma = -EINVAL;
	}
	/*short circuit battery*/
	rc = of_property_read_u32(node, "qcom,short_c_bat_cv_mv",
			&chip->short_c_batt.short_c_bat_cv_mv);
	if (rc < 0) {
		chip->short_c_batt.short_c_bat_cv_mv = -EINVAL;
	}
	rc = of_property_read_u32(node, "qcom,short_c_bat_vfloat_mv",
			&chip->limits.short_c_bat_vfloat_mv);
	if (rc < 0) {
		chip->limits.short_c_bat_vfloat_mv = -EINVAL;
	}
	rc = of_property_read_u32(node, "qcom,short_c_bat_fastchg_current_ma",
			&chip->limits.short_c_bat_fastchg_current_ma);
	if (rc < 0) {
		chip->limits.short_c_bat_fastchg_current_ma = -EINVAL;
	}
	rc = of_property_read_u32(node, "qcom,short_c_bat_vfloat_sw_limit",
			&chip->limits.short_c_bat_vfloat_sw_limit);
	if (rc < 0) {
		chip->limits.short_c_bat_vfloat_sw_limit = -EINVAL;
	}

	/*vfloat_sw_limit*/
	rc = of_property_read_u32(node, "qcom,non_standard_vfloat_sw_limit",
			&chip->limits.non_standard_vfloat_sw_limit);
	if (rc < 0) {
		chip->limits.non_standard_vfloat_sw_limit = 3960;
	}
	rc = of_property_read_u32(node, "qcom,cold_vfloat_sw_limit",
			&chip->limits.cold_vfloat_sw_limit);
	if (rc < 0) {
		chip->limits.cold_vfloat_sw_limit = 3960;
	}
	rc = of_property_read_u32(node, "qcom,little_cold_vfloat_sw_limit",
			&chip->limits.little_cold_vfloat_sw_limit);
	if (rc < 0) {
		chip->limits.little_cold_vfloat_sw_limit = 4330;
	}
	rc = of_property_read_u32(node, "qcom,cool_vfloat_sw_limit",
			&chip->limits.cool_vfloat_sw_limit);
	if (rc < 0) {
		chip->limits.cool_vfloat_sw_limit = 4330;
	}
	rc = of_property_read_u32(node, "qcom,little_cool_vfloat_sw_limit",
			&chip->limits.little_cool_vfloat_sw_limit);
	if (rc < 0) {
		chip->limits.little_cool_vfloat_sw_limit = 4330;
	}
	rc = of_property_read_u32(node, "qcom,normal_vfloat_sw_limit",
			&chip->limits.normal_vfloat_sw_limit);
	if (rc < 0) {
		chip->limits.normal_vfloat_sw_limit = 4330;
	}
	rc = of_property_read_u32(node, "qcom,warm_vfloat_sw_limit",
			&chip->limits.warm_vfloat_sw_limit);
	if (rc < 0) {
		chip->limits.warm_vfloat_sw_limit = 4060;
	}

	/*vfloat_over_sw_limit*/
	chip->limits.sw_vfloat_over_protect_enable = of_property_read_bool(node,
			"qcom,sw_vfloat_over_protect_enable");
	rc = of_property_read_u32(node, "qcom,non_standard_vfloat_over_sw_limit",
			&chip->limits.non_standard_vfloat_over_sw_limit);
	if (rc < 0) {
		chip->limits.non_standard_vfloat_over_sw_limit = 3980;
	}
	rc = of_property_read_u32(node, "qcom,cold_vfloat_over_sw_limit",
			&chip->limits.cold_vfloat_over_sw_limit);
	if (rc < 0) {
		chip->limits.cold_vfloat_over_sw_limit = 3980;
	}
	rc = of_property_read_u32(node, "qcom,little_cold_vfloat_over_sw_limit",
			&chip->limits.little_cold_vfloat_over_sw_limit);
	if (rc < 0) {
		chip->limits.little_cold_vfloat_over_sw_limit = 4390;
	}
	rc = of_property_read_u32(node, "qcom,cool_vfloat_over_sw_limit",
			&chip->limits.cool_vfloat_over_sw_limit);
	if (rc < 0) {
		chip->limits.cool_vfloat_over_sw_limit = 4390;
	}
	rc = of_property_read_u32(node, "qcom,little_cool_vfloat_over_sw_limit",
			&chip->limits.little_cool_vfloat_over_sw_limit);
	if (rc < 0) {
		chip->limits.little_cool_vfloat_over_sw_limit = 4390;
	}
	rc = of_property_read_u32(node, "qcom,normal_vfloat_over_sw_limit",
			&chip->limits.normal_vfloat_over_sw_limit);
	if (rc < 0) {
		chip->limits.normal_vfloat_over_sw_limit = 4390;
	}
	rc = of_property_read_u32(node, "qcom,warm_vfloat_over_sw_limit",
			&chip->limits.warm_vfloat_over_sw_limit);
	if (rc < 0) {
		chip->limits.warm_vfloat_over_sw_limit = 4080;
		}
	rc = of_property_read_u32(node, "qcom,charger_hv_thr",
			&chip->limits.charger_hv_thr);
	if (rc < 0) {
		chip->limits.charger_hv_thr = 5800;
	}
	rc = of_property_read_u32(node, "qcom,charger_recv_thr",
			&chip->limits.charger_recv_thr);
	if (rc < 0) {
		chip->limits.charger_recv_thr = 5800;
	}
	rc = of_property_read_u32(node, "qcom,charger_lv_thr",
			&chip->limits.charger_lv_thr);
	if (rc < 0) {
		chip->limits.charger_lv_thr = 3400;
	}
	rc = of_property_read_u32(node, "qcom,vbatt_full_thr",
			&chip->limits.vbatt_full_thr);
	if (rc < 0) {
		chip->limits.vbatt_full_thr = 4400;
	}
	rc = of_property_read_u32(node, "qcom,vbatt_hv_thr",
			&chip->limits.vbatt_hv_thr);
	if (rc < 0) {
		chip->limits.vbatt_hv_thr = 4500;
	}
	rc = of_property_read_u32(node, "qcom,vfloat_step_mv",
			&chip->limits.vfloat_step_mv);
	if (rc < 0) {
		chip->limits.vfloat_step_mv = 16;
	}
	rc = of_property_read_u32(node, "qcom,vbatt_power_off",
			&chip->vbatt_power_off);
	if (rc < 0) {
		chip->vbatt_power_off = 3300;
	}
	rc = of_property_read_u32(node, "qcom,vbatt_soc_1",
			&chip->vbatt_soc_1);
	if (rc < 0) {
		chip->vbatt_soc_1 = 3410;
	}
	rc = of_property_read_u32(node, "qcom,normal_vterm_hw_inc",
			&chip->limits.normal_vterm_hw_inc);
	if (rc < 0) {
		chip->limits.normal_vterm_hw_inc = 18;
	}
	rc = of_property_read_u32(node, "qcom,non_normal_vterm_hw_inc",
			&chip->limits.non_normal_vterm_hw_inc);
	if (rc < 0) {
		chip->limits.non_normal_vterm_hw_inc = 18;
	}
	rc = of_property_read_u32(node, "qcom,vbatt_pdqc_to_5v_thr",
			&chip->limits.vbatt_pdqc_to_5v_thr);
	if (rc < 0) {
		chip->limits.vbatt_pdqc_to_5v_thr = -EINVAL;
	}

	rc = of_property_read_u32(node, "qcom,vbatt_pdqc_to_9v_thr",
			&chip->limits.vbatt_pdqc_to_9v_thr);
	if (rc < 0) {
		chip->limits.vbatt_pdqc_to_9v_thr = 4000;
	}

	charger_xlog_printk(CHG_LOG_CRTI,
			"vbatt_power_off = %d, \
			vbatt_soc_1 = %d, \
			normal_vterm_hw_inc = %d, \
			, \
			non_normal_vterm_hw_inc = %d, \
			vbatt_pdqc_to_9v_thr = %d, \
			vbatt_pdqc_to_5v_thr = %d\n",
			chip->vbatt_power_off,
			chip->vbatt_soc_1,
			chip->limits.normal_vterm_hw_inc,
			chip->limits.non_normal_vterm_hw_inc,
			chip->limits.vbatt_pdqc_to_9v_thr,
			chip->limits.vbatt_pdqc_to_5v_thr);

	rc = of_property_read_u32(node, "qcom,ff1_normal_fastchg_ma",
			&chip->limits.ff1_normal_fastchg_ma);
	if (rc) {
		chip->limits.ff1_normal_fastchg_ma = 1000;
	}
	rc = of_property_read_u32(node, "qcom,ff1_warm_fastchg_ma",
			&chip->limits.ff1_warm_fastchg_ma);
	if (rc) {
		chip->limits.ff1_warm_fastchg_ma = chip->limits.ff1_normal_fastchg_ma;
	}
	rc = of_property_read_u32(node, "qcom,ffc2_temp_warm_decidegc",
			&chip->limits.ffc2_temp_warm_decidegc);
	if (rc) {
		chip->limits.ffc2_temp_warm_decidegc = 350;
	}
	rc = of_property_read_u32(node, "qcom,ffc2_temp_high_decidegc",
			&chip->limits.ffc2_temp_high_decidegc);
	if (rc) {
		chip->limits.ffc2_temp_high_decidegc = 400;
	}
	rc = of_property_read_u32(node, "qcom,ffc2_temp_low_decidegc",
			&chip->limits.ffc2_temp_low_decidegc);
	if (rc) {
		chip->limits.ffc2_temp_low_decidegc = 160;
	}
		rc = of_property_read_u32(node, "qcom,ffc2_normal_fastchg_ma",
			&chip->limits.ffc2_normal_fastchg_ma);
	if (rc < 0) {
		chip->limits.ffc2_normal_fastchg_ma = 700;
	}
		rc = of_property_read_u32(node, "qcom,ffc2_warm_fastchg_ma",
			&chip->limits.ffc2_warm_fastchg_ma);
	if (rc < 0) {
		chip->limits.ffc2_warm_fastchg_ma = 750;
	}
		rc = of_property_read_u32(node, "qcom,ffc2_exit_step_ma",
			&chip->limits.ffc2_exit_step_ma);
	if (rc < 0) {
		chip->limits.ffc2_exit_step_ma = 100;
	}
		rc = of_property_read_u32(node, "qcom,ffc2_warm_exit_step_ma",
			&chip->limits.ffc2_warm_exit_step_ma);
	if (rc < 0) {
		chip->limits.ffc2_warm_exit_step_ma = chip->limits.ffc2_exit_step_ma;
	}
	rc = of_property_read_u32(node, "qcom,ff1_exit_step_ma",
			&chip->limits.ff1_exit_step_ma);
	if (rc < 0) {
		chip->limits.ff1_exit_step_ma = 400;
	}
	rc = of_property_read_u32(node, "qcom,ff1_warm_exit_step_ma",
			&chip->limits.ff1_warm_exit_step_ma);
	if (rc < 0) {
		chip->limits.ff1_warm_exit_step_ma = 350;
	}
	rc = of_property_read_u32(node, "qcom,ffc_normal_vfloat_sw_limit",
			&chip->limits.ffc1_normal_vfloat_sw_limit);
	if (rc < 0) {
		chip->limits.ffc1_normal_vfloat_sw_limit = 4450;
	}
	rc = of_property_read_u32(node, "qcom,ffc_warm_vfloat_sw_limit",
			&chip->limits.ffc1_warm_vfloat_sw_limit);
	if (rc < 0) {
		chip->limits.ffc1_warm_vfloat_sw_limit
			= chip->limits.ffc1_normal_vfloat_sw_limit;
	}
	rc = of_property_read_u32(node, "qcom,ffc2_normal_vfloat_sw_limit",
			&chip->limits.ffc2_normal_vfloat_sw_limit);
	if (rc < 0) {
		chip->limits.ffc2_normal_vfloat_sw_limit
			= chip->limits.ffc1_normal_vfloat_sw_limit;
	}
	rc = of_property_read_u32(node, "qcom,ffc2_warm_vfloat_sw_limit",
			&chip->limits.ffc2_warm_vfloat_sw_limit);
	if (rc < 0) {
		chip->limits.ffc2_warm_vfloat_sw_limit
			= chip->limits.ffc2_normal_vfloat_sw_limit;
	}
	rc = of_property_read_u32(node, "qcom,ffc_temp_normal_vfloat_mv",
			&chip->limits.ffc_temp_normal_vfloat_mv);
	if (rc < 0) {
		chip->limits.ffc_temp_normal_vfloat_mv = 4500;
	}
	rc = of_property_read_u32(node, "qcom,ffc1_temp_normal_vfloat_mv",
			&chip->limits.ffc1_temp_normal_vfloat_mv);
	if (rc < 0) {
		chip->limits.ffc1_temp_normal_vfloat_mv
			= chip->limits.ffc_temp_normal_vfloat_mv;
	}
	rc = of_property_read_u32(node, "qcom,ffc2_temp_normal_vfloat_mv",
			&chip->limits.ffc2_temp_normal_vfloat_mv);
	if (rc < 0) {
		chip->limits.ffc2_temp_normal_vfloat_mv
			= chip->limits.ffc_temp_normal_vfloat_mv;
	}
	rc = of_property_read_u32(node, "qcom,ffc_normal_vfloat_over_sw_limit",
			&chip->limits.ffc_normal_vfloat_over_sw_limit);
	if (rc < 0) {
		chip->limits.ffc_normal_vfloat_over_sw_limit = 4500;
	}
	rc = of_property_read_u32(node, "qcom,ffc1_normal_vfloat_over_sw_limit",
			&chip->limits.ffc1_normal_vfloat_over_sw_limit);
	if (rc < 0) {
		chip->limits.ffc1_normal_vfloat_over_sw_limit
			= chip->limits.ffc_normal_vfloat_over_sw_limit;
	}
	rc = of_property_read_u32(node, "qcom,ffc2_normal_vfloat_over_sw_limit",
			&chip->limits.ffc2_normal_vfloat_over_sw_limit);
	if (rc < 0) {
		chip->limits.ffc2_normal_vfloat_over_sw_limit
			= chip->limits.ffc_normal_vfloat_over_sw_limit;
	}

	chip->limits.default_ffc1_normal_vfloat_sw_limit = chip->limits.ffc1_normal_vfloat_sw_limit;
	chip->limits.default_ffc1_warm_vfloat_sw_limit = chip->limits.ffc1_warm_vfloat_sw_limit;
	chip->limits.default_ffc2_normal_vfloat_sw_limit = chip->limits.ffc2_normal_vfloat_sw_limit;
	chip->limits.default_ffc2_warm_vfloat_sw_limit = chip->limits.ffc2_warm_vfloat_sw_limit;

	charger_xlog_printk(CHG_LOG_CRTI,
			"ff1_normal_fastchg_ma = %d, \
			ffc2_temp_warm_decidegc = %d, \
			ffc2_temp_high_decidegc = %d, \
			ffc2_normal_fastchg_ma = %d, \
			chip->limits.ffc2_warm_fastchg_ma = %d, \
			ffc2_exit_step_ma = %d, \
			ffc_normal_vfloat_sw_limit = %d, \
			ffc_warm_vfloat_sw_limit = %d, \
			ffc2_normal_vfloat_sw_limit = %d, \
			ffc2_warm_vfloat_sw_limit = %d, \
			ffc1_temp_normal_vfloat_mv = %d, \
			ffc2_temp_normal_vfloat_mv = %d, \
			ffc_normal_vfloat_over_sw_limit = %d \
			ffc2_temp_low_decidegc = %d \
			limits.ff1_exit_step_ma = %d \
			limits.ff1_warm_exit_step_ma = %d \
			pd_input_current_charger_ma = %d \
			qc_input_current_charger_ma = %d\n",
			chip->limits.ff1_normal_fastchg_ma,
			chip->limits.ffc2_temp_warm_decidegc,
			chip->limits.ffc2_temp_high_decidegc,
			chip->limits.ffc2_normal_fastchg_ma,
			chip->limits.ffc2_warm_fastchg_ma,
			chip->limits.ffc2_exit_step_ma,
			chip->limits.ffc1_normal_vfloat_sw_limit,
			chip->limits.ffc1_warm_vfloat_sw_limit,
			chip->limits.ffc2_normal_vfloat_sw_limit,
			chip->limits.ffc2_warm_vfloat_sw_limit,
			chip->limits.ffc1_temp_normal_vfloat_mv,
			chip->limits.ffc2_temp_normal_vfloat_mv,
			chip->limits.ffc_normal_vfloat_over_sw_limit,
			chip->limits.ffc2_temp_low_decidegc,
			chip->limits.ff1_exit_step_ma,
			chip->limits.ff1_warm_exit_step_ma,
			chip->limits.pd_input_current_charger_ma,
			chip->limits.qc_input_current_charger_ma);

	rc = of_property_read_u32(node, "qcom,default_iterm_ma",
			&chip->limits.default_iterm_ma);
	if (rc < 0) {
		chip->limits.default_iterm_ma = 100;
	}
	rc = of_property_read_u32(node, "qcom,default_temp_normal_fastchg_current_ma",
			&chip->limits.default_temp_normal_fastchg_current_ma);
	if (rc < 0) {
		chip->limits.default_temp_normal_fastchg_current_ma = OPCHG_FAST_CHG_MAX_MA;
	}
	rc = of_property_read_u32(node, "qcom,default_normal_vfloat_sw_limit",
			&chip->limits.default_normal_vfloat_sw_limit);
	if (rc < 0) {
		chip->limits.default_normal_vfloat_sw_limit = 4350;
	}
	rc = of_property_read_u32(node, "qcom,default_temp_normal_vfloat_mv",
			&chip->limits.default_temp_normal_vfloat_mv);
	if (rc < 0) {
		chip->limits.default_temp_normal_vfloat_mv = 4370;
	}
	rc = of_property_read_u32(node, "qcom,default_normal_vfloat_over_sw_limit",
			&chip->limits.default_normal_vfloat_over_sw_limit);
	if (rc < 0) {
		chip->limits.default_normal_vfloat_over_sw_limit = 4373;
	}

	charger_xlog_printk(CHG_LOG_CRTI,
			"default_iterm_ma = %d, \
			default_temp_normal_fastchg_current_ma = %d, \
			default_normal_vfloat_sw_limit = %d, \
			default_temp_normal_vfloat_mv = %d, \
			default_normal_vfloat_over_sw_limit = %d\n",
			chip->limits.default_iterm_ma,
			chip->limits.default_temp_normal_fastchg_current_ma,
			chip->limits.default_normal_vfloat_sw_limit,
			chip->limits.default_temp_normal_vfloat_mv,
			chip->limits.default_normal_vfloat_over_sw_limit);

	rc = of_property_read_u32(node, "qcom,default_temp_little_cool_fastchg_current_ma",
			&chip->limits.default_temp_little_cool_fastchg_current_ma);
	if (rc < 0) {
		chip->limits.default_temp_little_cool_fastchg_current_ma
			= chip->limits.temp_little_cool_fastchg_current_ma;
	}
	rc = of_property_read_u32(node, "qcom,default_little_cool_vfloat_sw_limit",
			&chip->limits.default_little_cool_vfloat_sw_limit);
	if (rc < 0) {
		chip->limits.default_little_cool_vfloat_sw_limit
			= chip->limits.little_cool_vfloat_sw_limit;
	}
	rc = of_property_read_u32(node, "qcom,default_temp_little_cool_vfloat_mv",
			&chip->limits.default_temp_little_cool_vfloat_mv);
	if (rc < 0) {
		chip->limits.default_temp_little_cool_vfloat_mv
			= chip->limits.temp_little_cool_vfloat_mv;
	}
	rc = of_property_read_u32(node, "qcom,default_little_cool_vfloat_over_sw_limit",
			&chip->limits.default_little_cool_vfloat_over_sw_limit);
	if (rc < 0) {
		chip->limits.default_little_cool_vfloat_over_sw_limit
			= chip->limits.little_cool_vfloat_over_sw_limit;
	}

	charger_xlog_printk(CHG_LOG_CRTI,
			"default_temp_little_cool_fastchg_current_ma = %d, \
			default_little_cool_vfloat_sw_limit = %d, \
			default_temp_little_cool_vfloat_mv = %d, \
			default_little_cool_vfloat_over_sw_limit = %d\n",
			chip->limits.default_temp_little_cool_fastchg_current_ma,
			chip->limits.default_little_cool_vfloat_sw_limit,
			chip->limits.default_temp_little_cool_vfloat_mv,
			chip->limits.default_little_cool_vfloat_over_sw_limit);

	chip->limits.default_temp_little_cold_fastchg_current_ma_high
			= chip->limits.temp_little_cold_fastchg_current_ma_high;
	chip->limits.default_temp_little_cold_fastchg_current_ma_low
			= chip->limits.temp_little_cold_fastchg_current_ma_low;
	chip->limits.default_temp_cool_fastchg_current_ma_high
			= chip->limits.temp_cool_fastchg_current_ma_high;
	chip->limits.default_temp_cool_fastchg_current_ma_low
			= chip->limits.temp_cool_fastchg_current_ma_low;
	chip->limits.default_temp_little_cool_fastchg_current_ma_high
			= chip->limits.temp_little_cool_fastchg_current_ma_high;
	chip->limits.default_temp_little_cool_fastchg_current_ma_low
			= chip->limits.temp_little_cool_fastchg_current_ma_low;
	chip->limits.default_temp_warm_fastchg_current_ma
			= chip->limits.temp_warm_fastchg_current_ma;
	chip->limits.default_input_current_charger_ma
			= chip->limits.input_current_charger_ma;
	rc = of_property_read_u32(node, "qcom,batt_capacity_mah",
			&chip->batt_capacity_mah);
	if (rc < 0) {
		chip->batt_capacity_mah = 2000;
	}

	chip->chg_ctrl_by_warp = of_property_read_bool(node, "qcom,chg_ctrl_by_warp");
	chip->chg_ctrl_by_warp_default = of_property_read_bool(node, "qcom,chg_ctrl_by_warp");

	rc = of_property_read_u32(node, "qcom,input_current_warp_ma_normal",
			&chip->limits.input_current_warp_ma_normal);
	if (rc) {
		chip->limits.input_current_warp_ma_normal = 3600;
	}
	rc = of_property_read_u32(node, "qcom,input_current_warp_led_ma_high",
			&chip->limits.input_current_warp_led_ma_high);
	if (rc) {
		chip->limits.input_current_warp_led_ma_high = 1800;
	}
	rc = of_property_read_u32(node, "qcom,input_current_warp_led_ma_warm",
			&chip->limits.input_current_warp_led_ma_warm);
	if (rc) {
		chip->limits.input_current_warp_led_ma_warm = 1800;
	}
	rc = of_property_read_u32(node, "qcom,input_current_warp_led_ma_normal",
			&chip->limits.input_current_warp_led_ma_normal);
	if (rc) {
		chip->limits.input_current_warp_led_ma_normal = 1800;
	}
	rc = of_property_read_u32(node, "qcom,warp_temp_bat_normal_decidegc",
		&chip->limits.warp_normal_bat_decidegc);
	if (rc) {
		chip->limits.warp_normal_bat_decidegc = 340;
	}
	rc = of_property_read_u32(node, "qcom,input_current_warp_ma_warm",
			&chip->limits.input_current_warp_ma_warm);
	if (rc) {
		chip->limits.input_current_warp_ma_warm = 3000;
	}
	rc = of_property_read_u32(node, "qcom,warp_temp_bat_warm_decidegc",
			&chip->limits.warp_warm_bat_decidegc);
	if (rc) {
		chip->limits.warp_warm_bat_decidegc = 380;
	}
	rc = of_property_read_u32(node, "qcom,input_current_warp_ma_high",
			&chip->limits.input_current_warp_ma_high);
	if (rc) {
		chip->limits.input_current_warp_ma_high = 2600;
	}
	rc = of_property_read_u32(node, "qcom,warp_temp_bat_hot_decidegc",
			&chip->limits.warp_high_bat_decidegc);
	if (rc) {
		chip->limits.warp_high_bat_decidegc = 450;
	}
	rc = of_property_read_u32(node, "qcom,charger_current_warp_ma_normal",
			&chip->limits.charger_current_warp_ma_normal);
	if (rc) {
		chip->limits.charger_current_warp_ma_normal = 1000;
	}
	chip->limits.default_input_current_warp_ma_high
			= chip->limits.input_current_warp_ma_high;
	chip->limits.default_input_current_warp_ma_warm
			= chip->limits.input_current_warp_ma_warm;
	chip->limits.default_input_current_warp_ma_normal
			= chip->limits.input_current_warp_ma_normal;
	chip->limits.default_pd_input_current_charger_ma
			= chip->limits.pd_input_current_charger_ma;
	chip->limits.default_qc_input_current_charger_ma
			= chip->limits.qc_input_current_charger_ma;
	chip->suspend_after_full = of_property_read_bool(node, "qcom,suspend_after_full");
	chip->check_batt_full_by_sw = of_property_read_bool(node, "qcom,check_batt_full_by_sw");
	chip->external_gauge = of_property_read_bool(node, "qcom,external_gauge");
	chip->fg_bcl_poll = of_property_read_bool(node, "qcom,fg_bcl_poll_enable");

	chip->wireless_support = of_property_read_bool(node, "qcom,wireless_support");
	chip->wpc_no_chargerpump = of_property_read_bool(node, "qcom,wpc_no_chargerpump");
	chip->chg_ctrl_by_lcd = of_property_read_bool(node, "qcom,chg_ctrl_by_lcd");
	chip->chg_ctrl_by_lcd_default = of_property_read_bool(node, "qcom,chg_ctrl_by_lcd");
	chip->chg_ctrl_by_camera = of_property_read_bool(node, "qcom,chg_ctrl_by_camera");
	chip->bq25890h_flag = of_property_read_bool(node,"qcom,bq25890_flag");
	chip->chg_ctrl_by_calling = of_property_read_bool(node, "qcom,chg_ctrl_by_calling");
	chip->ffc_support = of_property_read_bool(node, "qcom,ffc_support");
	chip->dual_ffc = of_property_read_bool(node, "qcom,dual_ffc");
	chip->new_ui_warning_support = of_property_read_bool(node, "qcom,new_ui_warning_support");
	chip->recharge_after_full = of_property_read_bool(node, "recharge_after_full");
	chip->smooth_switch = of_property_read_bool(node, "qcom,smooth_switch");
	if (get_eng_version() == HIGH_TEMP_AGING
			|| get_eng_version() == AGING
			|| get_eng_version() == FACTORY) {
		chg_err(" HIGH_TEMP_AGING/AGING/FACTORY,disable chg timeout \n");
		chip->limits.max_chg_time_sec = -1;
	} else {
		chip->limits.max_chg_time_sec = chip->batt_capacity_mah / 250 * 3600;
	}

#ifdef OPLUS_CHG_OP_DEF
	chip->support_abnormal_vol_check = of_property_read_bool(node, "qcom,abnormal_volt_check");
	charger_xlog_printk(CHG_LOG_CRTI, "chip->support_abnormal_vol_check:%d\n",
								chip->support_abnormal_vol_check);
#endif
	charger_xlog_printk(CHG_LOG_CRTI,
			"input_current_charger_ma = %d, \
			input_current_usb_ma = %d, \
			input_current_led_ma = %d, \
			input_current_led_ma_normal = %d, \
			input_current_led_ma_warm = %d, \
			input_current_led_ma_high = %d, \
			temp_normal_fastchg_current_ma = %d, \
			temp_normal_vfloat_mv = %d, \
			iterm_ma = %d, \
			recharge_mv = %d, \
			cold_bat_decidegc = %d, \
			temp_cold_vfloat_mv = %d, \
			temp_cold_fastchg_current_ma = %d, \
			little_cold_bat_decidegc = %d, \
			temp_little_cold_vfloat_mv = %d, \
			temp_little_cold_fastchg_current_ma = %d, \
			cool_bat_decidegc = %d, \
			temp_cool_vfloat_mv = %d, \
			temp_cool_fastchg_current_ma_high = %d, \
			temp_cool_fastchg_current_ma_low = %d, \
			little_cool_bat_decidegc = %d, \
			temp_little_cool_vfloat_mv = %d, \
			temp_little_cool_fastchg_current_ma = %d, \
			normal_bat_decidegc = %d, \
			warm_bat_decidegc = %d, \
			temp_warm_vfloat_mv = %d, \
			temp_warm_fastchg_current_ma = %d, \
			hot_bat_decidegc = %d, \
			non_standard_vfloat_mv = %d, \
			non_standard_fastchg_current_ma = %d, \
			max_chg_time_sec = %d, \
			charger_hv_thr = %d, \
			charger_lv_thr = %d, \
			vbatt_full_thr = %d, \
			vbatt_hv_thr = %d, \
			vfloat_step_mv = %d, \
			warp_project = %d, \
			suspend_after_full = %d, \
			ext_gauge = %d, \
			sw_vfloat_enable = %d, \
			chip->limits.temp_little_cold_fastchg_current_ma_low = %d, \
			chip->limits.temp_little_cold_fastchg_current_ma_high = %d, \
			chip->limits.charger_current_warp_ma_normal = %d, \
			chip->ffc_support = %d\
			chip->dual_ffc = %d\
			chip->new_ui_warning_support = %d\
			chip->smooth_switch = %d\n",
			chip->limits.input_current_charger_ma,
			chip->limits.input_current_usb_ma,
			chip->limits.input_current_led_ma,
			chip->limits.input_current_led_ma_normal,
			chip->limits.input_current_led_ma_warm,
			chip->limits.input_current_led_ma_high,
			chip->limits.temp_normal_fastchg_current_ma,
			chip->limits.temp_normal_vfloat_mv,
			chip->limits.iterm_ma,
			chip->limits.recharge_mv,
			chip->limits.cold_bat_decidegc,
			chip->limits.temp_cold_vfloat_mv,
			chip->limits.temp_cold_fastchg_current_ma,
			chip->limits.little_cold_bat_decidegc,
			chip->limits.temp_little_cold_vfloat_mv,
			chip->limits.temp_little_cold_fastchg_current_ma,
			chip->limits.cool_bat_decidegc,
			chip->limits.temp_cool_vfloat_mv,
			chip->limits.temp_cool_fastchg_current_ma_high,
			chip->limits.temp_cool_fastchg_current_ma_low,
			chip->limits.little_cool_bat_decidegc,
			chip->limits.temp_little_cool_vfloat_mv,
			chip->limits.temp_little_cool_fastchg_current_ma,
			chip->limits.normal_bat_decidegc,
			chip->limits.warm_bat_decidegc,
			chip->limits.temp_warm_vfloat_mv,
			chip->limits.temp_warm_fastchg_current_ma,
			chip->limits.hot_bat_decidegc,
			chip->limits.non_standard_vfloat_mv,
			chip->limits.non_standard_fastchg_current_ma,
			chip->limits.max_chg_time_sec,
			chip->limits.charger_hv_thr,
			chip->limits.charger_lv_thr,
			chip->limits.vbatt_full_thr,
			chip->limits.vbatt_hv_thr,
			chip->limits.vfloat_step_mv,
			chip->warp_project,
			chip->suspend_after_full,
			chip->external_gauge,
			chip->limits.sw_vfloat_over_protect_enable,
			chip->limits.temp_little_cold_fastchg_current_ma_low,
			chip->limits.temp_little_cold_fastchg_current_ma_high,
			chip->limits.charger_current_warp_ma_normal,
			chip->ffc_support,
			chip->dual_ffc,
			chip->new_ui_warning_support,
			chip->smooth_switch);

	chip->dual_charger_support = of_property_read_bool(node, "qcom,dual_charger_support");

	rc = of_property_read_u32(node, "qcom,slave_pct", &chip->slave_pct);
	if (rc) {
		chip->slave_pct = 50;
	}

	rc = of_property_read_u32(node, "qcom,slave_chg_enable_ma", &chip->slave_chg_enable_ma);
	if (rc) {
		chip->slave_chg_enable_ma = 2100;
	}

	rc = of_property_read_u32(node, "qcom,slave_chg_disable_ma", &chip->slave_chg_disable_ma);
	if (rc) {
		chip->slave_chg_disable_ma = 1700;
	}

	rc = of_property_read_u32(node, "qcom,usbtemp_batttemp_gap", &chip->usbtemp_batttemp_gap);
	if (rc) {
		chip->usbtemp_batttemp_gap = 17;
	}

	chip->warp_show_ui_soc_decimal = of_property_read_bool(node, "qcom,warp_show_ui_soc_decimal");

	rc = of_property_read_u32(node, "qcom,ui_soc_decimal_speedmin", &chip->ui_soc_decimal_speedmin);
	if (rc) {
		chip->ui_soc_decimal_speedmin = 2;
	}

#ifdef OPLUS_CHG_OP_DEF
	rc = read_chg_strategy_data_from_node(node, "oplus,warp_chg_led_on_strategy_data", chip->dynamic_config.warp_chg_led_on_strategy_data);
	if (rc < 0) {
		chg_err("read oplus,warp_chg_led_on_strategy_data error, rc=%d\n", rc);
	}
	for (i = 0; i < rc; i++) {
		chg_err("warp_chg_led_on_strategy[%d]: %d %d %d %d %d\n", i,
			chip->dynamic_config.warp_chg_led_on_strategy_data[i].cool_temp,
			chip->dynamic_config.warp_chg_led_on_strategy_data[i].heat_temp,
			chip->dynamic_config.warp_chg_led_on_strategy_data[i].curr_data,
			chip->dynamic_config.warp_chg_led_on_strategy_data[i].heat_next_index,
			chip->dynamic_config.warp_chg_led_on_strategy_data[i].cool_next_index);
	}

	rc = read_chg_strategy_data_from_node(node, "oplus,pd9v_chg_led_on_strategy_data", chip->dynamic_config.pd9v_chg_led_on_strategy_data);
	if (rc < 0) {
		chg_err("read oplus,pd9v_chg_led_on_strategy_data error, rc=%d\n", rc);
	}
	for (i = 0; i < rc; i++) {
		chg_err("pd9v_chg_led_on_strategy[%d]: %d %d %d %d %d\n", i,
			chip->dynamic_config.pd9v_chg_led_on_strategy_data[i].cool_temp,
			chip->dynamic_config.pd9v_chg_led_on_strategy_data[i].heat_temp,
			chip->dynamic_config.pd9v_chg_led_on_strategy_data[i].curr_data,
			chip->dynamic_config.pd9v_chg_led_on_strategy_data[i].heat_next_index,
			chip->dynamic_config.pd9v_chg_led_on_strategy_data[i].cool_next_index);
	}

	rc = read_chg_strategy_data_from_node(node, "oplus,pd5v_chg_led_on_strategy_data", chip->dynamic_config.pd5v_chg_led_on_strategy_data);
	if (rc < 0) {
		pr_err("read oplus,pd5v_chg_led_on_strategy_data error, rc=%d\n", rc);
	}
	for (i = 0; i < rc; i++) {
		pr_err("pd5v_chg_led_on_strategy[%d]: %d %d %d %d %d\n", i,
			chip->dynamic_config.pd5v_chg_led_on_strategy_data[i].cool_temp,
			chip->dynamic_config.pd5v_chg_led_on_strategy_data[i].heat_temp,
			chip->dynamic_config.pd5v_chg_led_on_strategy_data[i].curr_data,
			chip->dynamic_config.pd5v_chg_led_on_strategy_data[i].heat_next_index,
			chip->dynamic_config.pd5v_chg_led_on_strategy_data[i].cool_next_index);
	}

	rc = read_chg_strategy_data_from_node(node, "oplus,swarp_chg_led_on_strategy_data", chip->dynamic_config.swarp_chg_led_on_strategy_data);
	if (rc < 0) {
		pr_err("read oplus,swarp_chg_led_on_strategy_data error, rc=%d\n", rc);
	}
	for (i = 0; i < rc; i++) {
		pr_err("swarp_chg_led_on_strategy[%d]: %d %d %d %d %d\n", i,
			chip->dynamic_config.swarp_chg_led_on_strategy_data[i].cool_temp,
			chip->dynamic_config.swarp_chg_led_on_strategy_data[i].heat_temp,
			chip->dynamic_config.swarp_chg_led_on_strategy_data[i].curr_data,
			chip->dynamic_config.swarp_chg_led_on_strategy_data[i].heat_next_index,
			chip->dynamic_config.swarp_chg_led_on_strategy_data[i].cool_next_index);
	}

	rc = read_chg_strategy_data_from_node(node, "oplus,swarp_chg_led_off_strategy_data", chip->dynamic_config.swarp_chg_led_off_strategy_data);
	if (rc < 0) {
		pr_err("read oplus,swarp_chg_led_off_strategy_data error, rc=%d\n", rc);
	}
	for (i = 0; i < rc; i++) {
		pr_err("swarp_chg_led_off_strategy[%d]: %d %d %d %d %d\n", i,
			chip->dynamic_config.swarp_chg_led_off_strategy_data[i].cool_temp,
			chip->dynamic_config.swarp_chg_led_off_strategy_data[i].heat_temp,
			chip->dynamic_config.swarp_chg_led_off_strategy_data[i].curr_data,
			chip->dynamic_config.swarp_chg_led_off_strategy_data[i].heat_next_index,
			chip->dynamic_config.swarp_chg_led_off_strategy_data[i].cool_next_index);
	}

	rc = read_chg_strategy_data_from_node(node, "oplus,swarp_chg_led_off_strategy_data_high", chip->dynamic_config.swarp_chg_led_off_strategy_data_high);
	if (rc < 0) {
		pr_err("read oplus,swarp_chg_led_off_strategy_data_high error, rc=%d\n", rc);
		for (i = 0; i < CHG_STRATEGY_DATA_TABLE_MAX; i++) {
			chip->dynamic_config.swarp_chg_led_off_strategy_data_high[i].cool_temp = chip->dynamic_config.swarp_chg_led_off_strategy_data[i].cool_temp;
			chip->dynamic_config.swarp_chg_led_off_strategy_data_high[i].heat_temp = chip->dynamic_config.swarp_chg_led_off_strategy_data[i].heat_temp;
			chip->dynamic_config.swarp_chg_led_off_strategy_data_high[i].curr_data = chip->dynamic_config.swarp_chg_led_off_strategy_data[i].curr_data;
			chip->dynamic_config.swarp_chg_led_off_strategy_data_high[i].heat_next_index = chip->dynamic_config.swarp_chg_led_off_strategy_data[i].heat_next_index;
			chip->dynamic_config.swarp_chg_led_off_strategy_data_high[i].cool_next_index = chip->dynamic_config.swarp_chg_led_off_strategy_data[i].cool_next_index;
		}
	}
	for (i = 0; i < rc; i++) {
		pr_err("swarp_chg_led_off_strategy_high[%d]: %d %d %d %d %d\n", i,
			chip->dynamic_config.swarp_chg_led_off_strategy_data_high[i].cool_temp,
			chip->dynamic_config.swarp_chg_led_off_strategy_data_high[i].heat_temp,
			chip->dynamic_config.swarp_chg_led_off_strategy_data_high[i].curr_data,
			chip->dynamic_config.swarp_chg_led_off_strategy_data_high[i].heat_next_index,
			chip->dynamic_config.swarp_chg_led_off_strategy_data_high[i].cool_next_index);
	}

	rc = read_chg_strategy_data_from_node(node, "oplus,normal_chg_led_off_strategy_data", chip->dynamic_config.normal_chg_led_off_strategy_data);
	if (rc < 0) {
		pr_err("read oplus,normal_chg_led_off_strategy_data error, rc=%d\n", rc);
	}
	for (i = 0; i < rc; i++) {
		pr_err("normal_chg_led_off_strategy[%d]: %d %d %d %d %d\n", i,
			chip->dynamic_config.normal_chg_led_off_strategy_data[i].cool_temp,
			chip->dynamic_config.normal_chg_led_off_strategy_data[i].heat_temp,
			chip->dynamic_config.normal_chg_led_off_strategy_data[i].curr_data,
			chip->dynamic_config.normal_chg_led_off_strategy_data[i].heat_next_index,
			chip->dynamic_config.normal_chg_led_off_strategy_data[i].cool_next_index);
	}

	rc = read_chg_strategy_data_from_node(node, "oplus,swarp_general_chg_strategy_data_low", chip->dynamic_config.swarp_general_chg_strategy_data_low);
	if (rc < 0) {
		pr_err("read oplus,swarp_general_chg_strategy_data_low error, rc=%d\n", rc);
	}
	for (i = 0; i < rc; i++) {
		pr_err("swarp_general_chg_strategy_low[%d]: %d %d %d %d %d\n", i,
			chip->dynamic_config.swarp_general_chg_strategy_data_low[i].cool_temp,
			chip->dynamic_config.swarp_general_chg_strategy_data_low[i].heat_temp,
			chip->dynamic_config.swarp_general_chg_strategy_data_low[i].curr_data,
			chip->dynamic_config.swarp_general_chg_strategy_data_low[i].heat_next_index,
			chip->dynamic_config.swarp_general_chg_strategy_data_low[i].cool_next_index);
	}

	rc = read_chg_strategy_data_from_node(node, "oplus,swarp_general_chg_strategy_data_high", chip->dynamic_config.swarp_general_chg_strategy_data_high);
	if (rc < 0) {
		pr_err("read oplus,swarp_general_chg_strategy_data_high error, rc=%d\n", rc);
	}
	for (i = 0; i < rc; i++) {
		pr_err("swarp_general_chg_strategy_high[%d]: %d %d %d %d %d\n", i,
			chip->dynamic_config.swarp_general_chg_strategy_data_high[i].cool_temp,
			chip->dynamic_config.swarp_general_chg_strategy_data_high[i].heat_temp,
			chip->dynamic_config.swarp_general_chg_strategy_data_high[i].curr_data,
			chip->dynamic_config.swarp_general_chg_strategy_data_high[i].heat_next_index,
			chip->dynamic_config.swarp_general_chg_strategy_data_high[i].cool_next_index);
	}
#endif /* OPLUS_CHG_OP_DEF */

	charger_xlog_printk(CHG_LOG_CRTI,"dual_charger_support=%d, slave_pct=%d, slave_chg_enable_ma=%d, slave_chg_disable_ma=%d\n",
			chip->dual_charger_support, chip->slave_pct, chip->slave_chg_enable_ma, chip->slave_chg_disable_ma);

	rc = of_property_read_u32(node, "oplus,aging_ffc_version",
			&chip->aging_ffc_version);
	if (rc) {
		chip->aging_ffc_version = AGING_FFC_NOT_SUPPORT;
	}

	return 0;
}


int oplus_chg_get_tbatt_normal_charging_current(struct oplus_chg_chip *chip)
{
	int charging_current = OPLUS_CHG_DEFAULT_CHARGING_CURRENT;

	switch (chip->tbatt_normal_status) {
		case  BATTERY_STATUS__NORMAL_PHASE1:
			charging_current = chip->limits.temp_normal_phase1_fastchg_current_ma;
			break;
		case BATTERY_STATUS__NORMAL_PHASE2:
			if (vbatt_higherthan_4180mv) {
				charging_current = chip->limits.temp_normal_phase2_fastchg_current_ma_low;
			} else {
				charging_current = chip->limits.temp_normal_phase2_fastchg_current_ma_high;
			}
			break;
		case BATTERY_STATUS__NORMAL_PHASE3:
			if (vbatt_higherthan_4180mv) {
				charging_current = chip->limits.temp_normal_phase3_fastchg_current_ma_low;
			} else {
				charging_current = chip->limits.temp_normal_phase3_fastchg_current_ma_high;
			}
			break;
		case BATTERY_STATUS__NORMAL_PHASE4:
			if (vbatt_higherthan_4180mv) {
				charging_current = chip->limits.temp_normal_phase4_fastchg_current_ma_low;
			} else {
				charging_current = chip->limits.temp_normal_phase4_fastchg_current_ma_high;
			}
			break;
		case BATTERY_STATUS__NORMAL_PHASE5:
			charging_current = chip->limits.temp_normal_phase5_fastchg_current_ma;
			break;
		case BATTERY_STATUS__NORMAL_PHASE6:
			charging_current = chip->limits.temp_normal_phase6_fastchg_current_ma;
			break;
		default:
			break;
	}
	return charging_current;
}
static void oplus_chg_set_charging_current(struct oplus_chg_chip *chip)
{
	int charging_current = OPLUS_CHG_DEFAULT_CHARGING_CURRENT;

#ifndef OPLUS_CHG_OP_DEF
#ifndef CONFIG_OPLUS_CHARGER_MTK
#ifndef WPC_NEW_INTERFACE
		if (chip->wireless_support && oplus_wireless_charge_start() == true) {
				chg_err(" test do not set ichging , wireless charge start \n");
				return;
		}
#else
        if (oplus_wpc_get_status() != 0){
				chg_err(" test do not set ichging , wireless charge start \n");
				return;
		}
#endif
#endif
#else /* OPLUS_CHG_OP_DEF */
	if (oplus_chg_is_wls_online(chip)) {
		chg_err(" test do not set ichging , wireless charge start \n");
		return;
	}
#endif /* OPLUS_CHG_OP_DEF */
	switch (chip->tbatt_status) {
		case BATTERY_STATUS__INVALID:
		case BATTERY_STATUS__REMOVED:
		case BATTERY_STATUS__LOW_TEMP:
		case BATTERY_STATUS__HIGH_TEMP:
			return;
		case BATTERY_STATUS__COLD_TEMP:
			charging_current = chip->limits.temp_cold_fastchg_current_ma;
			break;
		case BATTERY_STATUS__LITTLE_COLD_TEMP:
			//charging_current = chip->limits.temp_little_cold_fastchg_current_ma;
			if (vbatt_higherthan_4180mv) {
				charging_current = chip->limits.temp_little_cold_fastchg_current_ma_low;
			} else {
				charging_current = chip->limits.temp_little_cold_fastchg_current_ma_high;
			}
			charger_xlog_printk(CHG_LOG_CRTI,
					"vbatt_higherthan_4180mv [%d], charging_current[%d]\n",
					vbatt_higherthan_4180mv, charging_current);
			break;
		case BATTERY_STATUS__COOL_TEMP:
			if (vbatt_higherthan_4180mv) {
				charging_current = chip->limits.temp_cool_fastchg_current_ma_low;
			} else {
				charging_current = chip->limits.temp_cool_fastchg_current_ma_high;
			}
			charger_xlog_printk(CHG_LOG_CRTI,
					"vbatt_higherthan_4180mv [%d], charging_current[%d]\n",
					vbatt_higherthan_4180mv, charging_current);
			break;
		case BATTERY_STATUS__LITTLE_COOL_TEMP:
			if (chip->dual_charger_support) {
				if (vbatt_higherthan_4180mv) {
					charging_current = chip->limits.temp_little_cool_fastchg_current_ma_low;
				} else {
					charging_current = chip->limits.temp_little_cool_fastchg_current_ma_high;
				}
				charger_xlog_printk(CHG_LOG_CRTI,
						"vbatt_higherthan_4180mv [%d], charging_current[%d]\n",
						vbatt_higherthan_4180mv, charging_current);
			} else {
				charging_current = chip->limits.temp_little_cool_fastchg_current_ma;
			}
			break;
		case BATTERY_STATUS__NORMAL:
			if (chip->dual_charger_support) {
				charging_current = oplus_chg_get_tbatt_normal_charging_current(chip);
			}
			else
				charging_current = chip->limits.temp_normal_fastchg_current_ma;
			break;
		case BATTERY_STATUS__WARM_TEMP:
			charging_current = chip->limits.temp_warm_fastchg_current_ma;
			break;
		default:
			break;
	}
	if (((!chip->authenticate) || (!chip->hmac))
				&& (charging_current > chip->limits.non_standard_fastchg_current_ma)) {
		charging_current = chip->limits.non_standard_fastchg_current_ma;
		charger_xlog_printk(CHG_LOG_CRTI,
			"no high battery, set charging current = %d\n",
			chip->limits.non_standard_fastchg_current_ma);
	}
	if (oplus_short_c_batt_is_prohibit_chg(chip)) {
		if (charging_current > chip->limits.short_c_bat_fastchg_current_ma) {
			charging_current = chip->limits.short_c_bat_fastchg_current_ma;
			charger_xlog_printk(CHG_LOG_CRTI,
				"short circuit battery, set charging current = %d\n",
				chip->limits.short_c_bat_fastchg_current_ma);
		}
	}
	if ((chip->chg_ctrl_by_lcd) && (chip->led_on) &&
				(charging_current > chip->limits.temp_warm_fastchg_current_ma_led_on)) {
		if (chip->tbatt_status == BATTERY_STATUS__WARM_TEMP){
			charging_current = chip->limits.temp_warm_fastchg_current_ma_led_on;
		}
		if (chip->dual_charger_support && chip->tbatt_normal_status == BATTERY_STATUS__NORMAL_PHASE6) {
			charging_current = chip->limits.temp_warm_fastchg_current_ma_led_on;
		}
		charger_xlog_printk(CHG_LOG_CRTI,
				"[BATTERY]LED ON, charging current: %d\n", charging_current);
	}

#ifdef OPLUS_CHG_OP_DEF
	if ((chip->chg_strategy_batt_curr_ma > 0) &&
	    (chip->chg_strategy_batt_curr_ma < charging_current)) {
			charging_current = chip->chg_strategy_batt_curr_ma;
	}
	if ((chip->chg_ctrl_by_camera) && (chip->camera_on)
			&&(charging_current > chip->limits.batt_current_camera_ma)) {
		charging_current = chip->limits.batt_current_camera_ma;
	}
	if ((chip->chg_ctrl_by_calling) && (chip->calling_on)
			&& (charging_current > chip->limits.batt_current_calling_ma)) {
		charging_current = chip->limits.batt_current_calling_ma;
	}
#endif

#ifndef OPLUS_CHG_OP_DEF // nick.hu In some scenarios, the current may be set to 0
	if (charging_current == 0) {
		return;
	}
#endif
	if(charging_current == 0)
		charger_xlog_printk(CHG_LOG_CRTI, "set charging_current = 0\n");
	chip->chg_ops->charging_current_write_fast(charging_current);
}

void oplus_chg_set_input_current_limit(struct oplus_chg_chip *chip)
{
	int current_limit = 0;
	bool is_mcu_fastchg = false;
	is_mcu_fastchg = (oplus_warp_get_fastchg_started()
					&&(chip->vbatt_num != 2 || oplus_warp_get_fast_chg_type() != CHARGER_SUBTYPE_FASTCHG_WARP));

	if(is_mcu_fastchg) {
		chg_err("MCU_READING_iic,return");
		return;
	}
#ifndef OPLUS_CHG_OP_DEF
#ifndef CONFIG_OPLUS_CHARGER_MTK
#ifndef WPC_NEW_INTERFACE
		if (chip->wireless_support && oplus_wireless_charge_start() == true) {
				chg_err(" test do not set ichging , wireless charge start \n");
				return;
		}
#else
        if (oplus_wpc_get_status() != 0){
                chg_err(" test do not set ichging , wireless charge start \n");
                return;
        }
#endif
#endif
#else /* OPLUS_CHG_OP_DEF */
	if (oplus_chg_is_wls_online(chip)) {
		chg_err(" test do not set ichging , wireless charge start \n");
		return;
	}
#endif /* OPLUS_CHG_OP_DEF */
	switch (chip->charger_type) {
		case POWER_SUPPLY_TYPE_UNKNOWN:
			return;
		case POWER_SUPPLY_TYPE_USB:
			current_limit = chip->limits.input_current_usb_ma;
			break;
		case POWER_SUPPLY_TYPE_USB_DCP:
			current_limit = chip->limits.input_current_charger_ma;
#ifdef OPLUS_CHG_OP_DEF
			if (chip->norchg_reconnect_count == 1) {
				pr_info("norchg_reconnect_count = 1\n");
				if (current_limit > 1500)
					current_limit = 1500;
			} else if (chip->norchg_reconnect_count > 1) {
				pr_info("norchg_reconnect_count = %d\n", chip->norchg_reconnect_count);
				if (current_limit > 1000)
					current_limit = 1000;
			}
#endif
			break;
		case POWER_SUPPLY_TYPE_USB_CDP:
			current_limit = chip->limits.input_current_cdp_ma;
			break;
		default:
			return;
	}

	if ((chip->chg_ctrl_by_lcd) && (chip->led_on)) {
		if (!chip->dual_charger_support || (chip->dual_charger_support && chip->charger_volt > 7500)) {
			if (chip->led_temp_status == LED_TEMP_STATUS__HIGH) {
				if (current_limit > chip->limits.input_current_led_ma_high){
					current_limit = chip->limits.input_current_led_ma_high;
				}
			} else if (chip->led_temp_status == LED_TEMP_STATUS__WARM) {
				if (current_limit > chip->limits.input_current_led_ma_warm){
					current_limit = chip->limits.input_current_led_ma_warm;
				}
			} else {
				if (current_limit > chip->limits.input_current_led_ma_normal){
					current_limit = chip->limits.input_current_led_ma_normal;
				}
			}
			charger_xlog_printk(CHG_LOG_CRTI, "[BATTERY]LED STATUS CHANGED, IS ON\n");
		}
		if ((chip->chg_ctrl_by_camera) && (chip->camera_on)
				&& (current_limit > chip->limits.input_current_camera_ma)) {
			current_limit = chip->limits.input_current_camera_ma;
			charger_xlog_printk(CHG_LOG_CRTI, "[BATTERY]CAMERA STATUS CHANGED, IS ON\n");
		}
	} else if ((chip->chg_ctrl_by_camera) && (chip->camera_on)
			&&(current_limit > chip->limits.input_current_camera_ma)) {
		current_limit = chip->limits.input_current_camera_ma;
		charger_xlog_printk(CHG_LOG_CRTI, "[BATTERY]CAMERA STATUS CHANGED, IS ON\n");
	}
	if ((chip->chg_ctrl_by_calling) && (chip->calling_on)
			&& (current_limit > chip->limits.input_current_calling_ma)) {
		current_limit = chip->limits.input_current_calling_ma;
		charger_xlog_printk(CHG_LOG_CRTI, "[BATTERY]calling STATUS CHANGED, IS ON\n");
	}
	if (chip->chg_ctrl_by_warp && chip->vbatt_num == 2
				&& oplus_warp_get_fast_chg_type() == CHARGER_SUBTYPE_FASTCHG_WARP
				&& oplus_warp_get_fastchg_started() == true) {
		if(chip->led_on) {
			if(chip->warp_temp_status == WARP_TEMP_STATUS__HIGH) {
				current_limit = chip->limits.input_current_warp_led_ma_high;
			} else if(chip->warp_temp_status == WARP_TEMP_STATUS__WARM) {
				current_limit = chip->limits.input_current_warp_led_ma_warm;
			} else if(chip->warp_temp_status == WARP_TEMP_STATUS__NORMAL) {
				current_limit = chip->limits.input_current_warp_led_ma_normal;
			}
		} else if (!(chip->chg_ctrl_by_calling && chip->calling_on)) {
			if(chip->warp_temp_status == WARP_TEMP_STATUS__HIGH) {
				current_limit = chip->limits.input_current_warp_ma_high;
			} else if(chip->warp_temp_status == WARP_TEMP_STATUS__WARM) {
				current_limit = chip->limits.input_current_warp_ma_warm;
			} else if(chip->warp_temp_status == WARP_TEMP_STATUS__NORMAL) {
				current_limit = chip->limits.input_current_warp_ma_normal;
			}
		}
		chg_err("chg_ctrl_by_warp,  \
				led_on = %d,\
				calling_on = %d,\
				current_limit[%d], \
				chip->warp_temp_status[%d]\n",
				chip->led_on,
				chip->calling_on,
				current_limit,
				chip->warp_temp_status);
		if ( chip->chg_ops->input_current_ctrl_by_warp_write) {
			chip->chg_ops->input_current_ctrl_by_warp_write(current_limit);
			return;
		}
	}
	if (chip->chg_ctrl_by_cool_down && (current_limit > chip->limits.input_current_cool_down_ma)) {
		current_limit = chip->limits.input_current_cool_down_ma;
	}
#ifndef OPLUS_CHG_OP_DEF
	charger_xlog_printk(CHG_LOG_CRTI,
		" led_on = %d, \
		current_limit = %d, \
		led_temp_status = %d\n",
		chip->led_on,
		current_limit,
		chip->led_temp_status);
#else
	charger_xlog_printk(CHG_LOG_CRTI,
		"led_on = %d, \
		cool_down = %d \
		current_limit = %d, \
		led_temp_status = %d\n",
		chip->led_on,
		chip->chg_ctrl_by_cool_down,
		current_limit,
		chip->led_temp_status);
#endif
	chip->chg_ops->input_current_write(current_limit);
}

static int oplus_chg_get_float_voltage(struct oplus_chg_chip *chip)
{
	int flv = chip->limits.temp_normal_vfloat_mv;

	switch (chip->tbatt_status) {
		case BATTERY_STATUS__INVALID:
		case BATTERY_STATUS__REMOVED:
		case BATTERY_STATUS__LOW_TEMP:
		case BATTERY_STATUS__HIGH_TEMP:
			return flv;
		case BATTERY_STATUS__COLD_TEMP:
			flv = chip->limits.temp_cold_vfloat_mv;
			break;
		case BATTERY_STATUS__LITTLE_COLD_TEMP:
			flv = chip->limits.temp_little_cold_vfloat_mv;
			break;
		case BATTERY_STATUS__COOL_TEMP:
			flv = chip->limits.temp_cool_vfloat_mv;
			break;
		case BATTERY_STATUS__LITTLE_COOL_TEMP:
			flv = chip->limits.temp_little_cool_vfloat_mv;
		break;
		case BATTERY_STATUS__NORMAL:
			flv = chip->limits.temp_normal_vfloat_mv;
			break;
		case BATTERY_STATUS__WARM_TEMP:
			flv = chip->limits.temp_warm_vfloat_mv;
			break;
		default:
			break;
	}
	if (oplus_short_c_batt_is_prohibit_chg(chip)
			&& flv > chip->limits.short_c_bat_vfloat_mv) {
		flv = chip->limits.short_c_bat_vfloat_mv;
	}
	return flv;
}

static void oplus_chg_set_float_voltage(struct oplus_chg_chip *chip)
{
	int flv = oplus_chg_get_float_voltage(chip);

	if (((!chip->authenticate) ||(!chip->hmac)) && (flv > chip->limits.non_standard_vfloat_mv)) {
		flv = chip->limits.non_standard_vfloat_mv;
		charger_xlog_printk(CHG_LOG_CRTI,
			"no authenticate or no hmac battery, set float voltage = %d\n",
			chip->limits.non_standard_vfloat_mv);
	}
	chip->chg_ops->float_voltage_write(flv * chip->vbatt_num);
	chip->limits.vfloat_sw_set = flv;
}

#define VFLOAT_OVER_NUM		2
static void oplus_chg_vfloat_over_check(struct oplus_chg_chip *chip)
{
	if (!chip->mmi_chg) {
		return;
	}
	if (chip->charging_state == CHARGING_STATUS_FULL) {
		return;
	}
	if (oplus_warp_get_allow_reading() == false) {
		return;
	}
	if (chip->check_batt_full_by_sw
			&& (chip->limits.sw_vfloat_over_protect_enable == false)) {
		return;
	}
	if (oplus_warp_get_fastchg_ing() == true) {
		return;
	}
#ifndef CONFIG_OPLUS_CHARGER_MTK
#ifndef OPLUS_CHG_OP_DEF
	if (chip->wireless_support && oplus_wpc_get_ffc_charging() == true) {
		return;
	}
#else /* OPLUS_CHG_OP_DEF */
	if (is_comm_ocm_available(chip) &&
	    oplus_chg_comm_get_ffc_status(chip->comm_ocm) != FFC_DEFAULT) {
		return;
	}
#endif /* OPLUS_CHG_OP_DEF */
#endif
	//if (!((oplus_warp_get_fastchg_to_normal()== true) || (oplus_warp_get_fastchg_to_warm() == true))) {
	if(chip->limits.sw_vfloat_over_protect_enable) {
		if ((chip->batt_volt >= chip->limits.cold_vfloat_over_sw_limit
					&& chip->tbatt_status == BATTERY_STATUS__COLD_TEMP)
				||(chip->batt_volt >= chip->limits.little_cold_vfloat_over_sw_limit
					&& chip->tbatt_status == BATTERY_STATUS__LITTLE_COLD_TEMP)
				||(chip->batt_volt >= chip->limits.cool_vfloat_over_sw_limit
					&& chip->tbatt_status == BATTERY_STATUS__COOL_TEMP)
				||(chip->batt_volt >= chip->limits.little_cool_vfloat_over_sw_limit
					&& chip->tbatt_status == BATTERY_STATUS__LITTLE_COOL_TEMP)
				||(chip->batt_volt >= chip->limits.normal_vfloat_over_sw_limit
					&& chip->tbatt_status == BATTERY_STATUS__NORMAL)
				||(chip->batt_volt >= chip->limits.warm_vfloat_over_sw_limit
					&& chip->tbatt_status == BATTERY_STATUS__WARM_TEMP)
				||(((!chip->authenticate) ||(!chip->hmac))
					&& (chip->batt_volt >= chip->limits.non_standard_vfloat_over_sw_limit))) {
			chip->limits.vfloat_over_counts++;
			if (chip->limits.vfloat_over_counts > VFLOAT_OVER_NUM) {
				chip->limits.vfloat_over_counts = 0;
				chip->limits.vfloat_sw_set -= chip->limits.vfloat_step_mv;
				chip->chg_ops->float_voltage_write(chip->limits.vfloat_sw_set * chip->vbatt_num);
				charger_xlog_printk(CHG_LOG_CRTI,
					"bat_volt:%d, \
					tbatt:%d, \
					sw_vfloat_set:%d\n",
					chip->batt_volt,
					chip->tbatt_status,
					chip->limits.vfloat_sw_set);
			}
		} else {
			chip->limits.vfloat_over_counts = 0;
		}
			return;
	}
}

static void oplus_chg_check_vbatt_higher_than_4180mv(struct oplus_chg_chip *chip)
{
	static bool vol_high_pre = false;
	static int lower_count = 0, higher_count = 0;
	static int tbatt_status_pre = BATTERY_STATUS__NORMAL;

	if (!chip->mmi_chg) {
		vbatt_higherthan_4180mv = false;
		vol_high_pre = false;
		lower_count = 0;
		higher_count = 0;
		return;
	}
	if (oplus_warp_get_fastchg_started() == true) {
		return;
	}
	if (tbatt_status_pre != chip->tbatt_status) {
		tbatt_status_pre = chip->tbatt_status;
		vbatt_higherthan_4180mv = false;
		vol_high_pre = false;
		lower_count = 0;
		higher_count = 0;
	}
	//if (chip->batt_volt >(chip->vbatt_num * 4180)) {
	if (chip->batt_volt > 4180) {
		higher_count++;
		if (higher_count > 2) {
			lower_count = 0;
			higher_count = 3;
			vbatt_higherthan_4180mv = true;
		}
	} else if (vbatt_higherthan_4180mv) {
		//if (chip->batt_volt >(chip->vbatt_num * 4000)) {
		if (chip->batt_volt < 4000) {
			lower_count++;
			if (lower_count > 2) {
				lower_count = 3;
				higher_count = 0;
				vbatt_higherthan_4180mv = false;
			}
		}
	}
	/*chg_err(" tbatt_status:%d,batt_volt:%d,vol_high_pre:%d,vbatt_higherthan_4180mv:%d\n",*/
	/*chip->tbatt_status,chip->batt_volt,vol_high_pre,vbatt_higherthan_4180mv);*/
	if (vol_high_pre != vbatt_higherthan_4180mv) {
		vol_high_pre = vbatt_higherthan_4180mv;
		oplus_chg_set_charging_current(chip);
	}
}

#define TEMP_FFC_COUNTS		2
static void oplus_chg_check_ffc_temp_status(struct oplus_chg_chip *chip)
{
	int batt_temp = chip->temperature;
	OPLUS_CHG_FFC_TEMP_STATUS temp_status = chip->ffc_temp_status;
	static int high_counts = 0, warm_counts = 0, normal_counts = 0;
	static int low_counts = 0;

	if (batt_temp >= chip->limits.ffc2_temp_high_decidegc) {			/*>=40C*/
		//tled_status = FFC_TEMP_STATUS__HIGH;
		high_counts ++;
		if (high_counts >= TEMP_FFC_COUNTS) {
			low_counts = 0;
			high_counts = 0;
			warm_counts = 0;
			normal_counts = 0;
			temp_status = FFC_TEMP_STATUS__HIGH;
		}
	}else if (batt_temp >= chip->limits.ffc2_temp_warm_decidegc) {		/*>=35C && < 40*/
		//temp_status = FFC_TEMP_STATUS__WARM;
		warm_counts ++;
		if (warm_counts >= TEMP_FFC_COUNTS) {
				low_counts = 0;
				high_counts = 0;
						warm_counts = 0;
				normal_counts = 0;
				temp_status = FFC_TEMP_STATUS__WARM;
		}
	} else if (batt_temp >= chip->limits.ffc2_temp_low_decidegc) {		/*>=16C&&<35C*/
		//temp_status = FFC_TEMP_STATUS__NORMAL;
		normal_counts ++;
		if (normal_counts >= TEMP_FFC_COUNTS) {
			low_counts = 0;
			high_counts = 0;
			warm_counts = 0;
			normal_counts = 0;
			temp_status = FFC_TEMP_STATUS__NORMAL;
		}
	} else {															/*<16C*/
		low_counts++;
		if (low_counts >= TEMP_FFC_COUNTS) {
			low_counts = 0;
			high_counts = 0;
			warm_counts = 0;
			normal_counts = 0;
			temp_status = FFC_TEMP_STATUS__LOW;
		}
	}
	if (temp_status != chip->ffc_temp_status) {
	chip->ffc_temp_status = temp_status;
	}
}

void oplus_chg_turn_on_ffc1(struct oplus_chg_chip *chip)
{
	if ((!chip->authenticate) ||(!chip->hmac)) {
		return;
	}
	if (!chip->mmi_chg) {
		return;
	}
	if (oplus_warp_get_allow_reading() == false) {
		return;
	}
	chip->chg_ops->hardware_init();
	if (chip->stop_chg == 0
			&& (chip->charger_type == POWER_SUPPLY_TYPE_USB
			|| chip->charger_type == POWER_SUPPLY_TYPE_USB_CDP)) {
		chip->chg_ops->charger_suspend();
	}
	if (chip->check_batt_full_by_sw) {
		chip->chg_ops->set_charging_term_disable();
	}

	pr_err("oplus_chg_turn_on_ffc1--------\r\n");
	chip->chg_ctrl_by_lcd = false;
	chip->fastchg_to_ffc = true;
	chip->fastchg_ffc_status = 1;
	chip->chg_ctrl_by_warp = false;
	chip->recharge_after_ffc = true;

	oplus_chg_aging_ffc_action(chip, true);

	if (chip->temperature >= chip->limits.ffc2_temp_warm_decidegc) {
		chip->limits.temp_normal_fastchg_current_ma
			= chip->limits.ff1_warm_fastchg_ma;
		chip->limits.temp_little_cool_fastchg_current_ma
			= chip->limits.ff1_warm_fastchg_ma;
	} else {
		chip->limits.temp_normal_fastchg_current_ma
			= chip->limits.ff1_normal_fastchg_ma;
		chip->limits.temp_little_cool_fastchg_current_ma
			= chip->limits.ff1_normal_fastchg_ma;
	}
	chip->limits.normal_vfloat_sw_limit
		= chip->limits.ffc1_normal_vfloat_sw_limit;
	chip->limits.temp_normal_vfloat_mv
		= chip->limits.ffc1_temp_normal_vfloat_mv;
	chip->limits.normal_vfloat_over_sw_limit
		= chip->limits.ffc1_normal_vfloat_over_sw_limit;
	chip->limits.little_cool_vfloat_sw_limit
		= chip->limits.ffc1_normal_vfloat_sw_limit;
	chip->limits.temp_little_cool_vfloat_mv
		= chip->limits.ffc1_temp_normal_vfloat_mv;
	chip->limits.little_cool_vfloat_over_sw_limit
		= chip->limits.ffc1_normal_vfloat_over_sw_limit;
	oplus_chg_check_tbatt_status(chip);
	oplus_chg_set_float_voltage(chip);
	oplus_chg_set_charging_current(chip);
	oplus_chg_set_input_current_limit(chip);
	chip->chg_ops->term_current_set(chip->limits.iterm_ma);
}

void oplus_chg_turn_on_ffc2(struct oplus_chg_chip *chip)
{
	if ((!chip->authenticate) ||(!chip->hmac)) {
		return;
	}
	if (!chip->mmi_chg) {
		return;
	}
	if (oplus_warp_get_allow_reading() == false) {
		return;
	}
	chip->chg_ops->hardware_init();

	if (chip->check_batt_full_by_sw) {
		chip->chg_ops->set_charging_term_disable();
	}

	pr_err("oplus_chg_turn_on_ffc2--------\r\n");
	chip->chg_ctrl_by_lcd = false;
	chip->fastchg_to_ffc = true;
	chip->fastchg_ffc_status = 2;
	chip->chg_ctrl_by_warp = false;
	chip->recharge_after_ffc = true;

	oplus_chg_aging_ffc_action(chip, false);

	if (chip->temperature >= chip->limits.ffc2_temp_warm_decidegc) {
		chip->limits.temp_normal_fastchg_current_ma
			= chip->limits.ffc2_warm_fastchg_ma;
		chip->limits.temp_little_cool_fastchg_current_ma
			= chip->limits.ffc2_warm_fastchg_ma;
	} else {
		chip->limits.temp_normal_fastchg_current_ma
			= chip->limits.ffc2_normal_fastchg_ma;
		chip->limits.temp_little_cool_fastchg_current_ma
			= chip->limits.ffc2_normal_fastchg_ma;
	}

	chip->limits.normal_vfloat_sw_limit
		= chip->limits.ffc2_normal_vfloat_sw_limit;
	chip->limits.temp_normal_vfloat_mv
		= chip->limits.ffc2_temp_normal_vfloat_mv;
	chip->limits.normal_vfloat_over_sw_limit
		= chip->limits.ffc2_normal_vfloat_over_sw_limit;

	chip->limits.little_cool_vfloat_sw_limit
		= chip->limits.ffc2_normal_vfloat_sw_limit;
	chip->limits.temp_little_cool_vfloat_mv
		= chip->limits.ffc2_temp_normal_vfloat_mv;
	chip->limits.little_cool_vfloat_over_sw_limit
		= chip->limits.ffc2_normal_vfloat_over_sw_limit;

	oplus_chg_check_tbatt_status(chip);
	oplus_chg_set_float_voltage(chip);
	oplus_chg_set_charging_current(chip);
	oplus_chg_set_input_current_limit(chip);
	chip->chg_ops->term_current_set(chip->limits.iterm_ma);
}

void oplus_chg_turn_on_charging(struct oplus_chg_chip *chip)
{
#ifdef OPLUS_CHG_OP_DEF
	int rc;
	int skin_temp;
#endif

	if (!chip->authenticate) {
		return;
	}
	if (!chip->mmi_chg) {
		return;
	}
#ifdef OPLUS_CHG_OP_DEF
	if (chip->usb_chg_disable) {
		return;
	}
#endif

	if (oplus_warp_get_allow_reading() == false) {
		return;
	}

	chip->chg_ops->hardware_init();
	if (chip->stop_chg == 0
			&& (chip->charger_type == POWER_SUPPLY_TYPE_USB
				|| chip->charger_type == POWER_SUPPLY_TYPE_USB_CDP)) {
		chip->chg_ops->charger_suspend();
	}
	if (chip->check_batt_full_by_sw) {
		chip->chg_ops->set_charging_term_disable();
	}

#ifdef OPLUS_CHG_OP_DEF
	rc = oplus_chg_get_skin_temp(chip, &skin_temp);
	if (rc < 0) {
		chg_err("can't get skin temp\n");
		skin_temp = 250;
	}
#ifdef CONFIG_OPLUS_CHG_OOS
	oplus_chg_strategy_init(
		&warp_chg_led_on_strategy, chip->dynamic_config.warp_chg_led_on_strategy_data,
		oplus_chg_get_chg_strategy_data_len(
			chip->dynamic_config.warp_chg_led_on_strategy_data,
			CHG_STRATEGY_DATA_TABLE_MAX),
		skin_temp);
	oplus_chg_strategy_init(
		&pd9v_chg_led_on_strategy, chip->dynamic_config.pd9v_chg_led_on_strategy_data,
		oplus_chg_get_chg_strategy_data_len(
			chip->dynamic_config.pd9v_chg_led_on_strategy_data,
			CHG_STRATEGY_DATA_TABLE_MAX),
		skin_temp);
	oplus_chg_strategy_init(
		&pd5v_chg_led_on_strategy, chip->dynamic_config.pd5v_chg_led_on_strategy_data,
		oplus_chg_get_chg_strategy_data_len(
			chip->dynamic_config.pd5v_chg_led_on_strategy_data,
			CHG_STRATEGY_DATA_TABLE_MAX),
		skin_temp);
#endif /* CONFIG_OPLUS_CHG_OOS */
	oplus_chg_strategy_init(
		&normal_chg_led_off_strategy, chip->dynamic_config.normal_chg_led_off_strategy_data,
		oplus_chg_get_chg_strategy_data_len(
			chip->dynamic_config.normal_chg_led_off_strategy_data,
			CHG_STRATEGY_DATA_TABLE_MAX),
		skin_temp);
#endif /* OPLUS_CHG_OP_DEF */

	oplus_chg_check_tbatt_status(chip);
	oplus_chg_set_float_voltage(chip);
	oplus_chg_set_charging_current(chip);
	oplus_chg_set_input_current_limit(chip);

	chip->chg_ops->term_current_set(chip->limits.iterm_ma);
}

void oplus_chg_turn_off_charging(struct oplus_chg_chip *chip)
{
	if (oplus_warp_get_allow_reading() == false) {
		return;
	}

	switch (chip->tbatt_status) {
		case BATTERY_STATUS__INVALID:
		case BATTERY_STATUS__REMOVED:
		case BATTERY_STATUS__LOW_TEMP:
			break;
		case BATTERY_STATUS__HIGH_TEMP:
			break;
		case BATTERY_STATUS__COLD_TEMP:
			break;
		case BATTERY_STATUS__LITTLE_COLD_TEMP:
		case BATTERY_STATUS__COOL_TEMP:
			chip->chg_ops->charging_current_write_fast(chip->limits.temp_cold_fastchg_current_ma);
			msleep(50);
			break;
		case BATTERY_STATUS__LITTLE_COOL_TEMP:
		case BATTERY_STATUS__NORMAL:
			chip->chg_ops->charging_current_write_fast(chip->limits.temp_cool_fastchg_current_ma_high);
			msleep(50);
			chip->chg_ops->charging_current_write_fast(chip->limits.temp_cold_fastchg_current_ma);
			msleep(50);
			break;
		case BATTERY_STATUS__WARM_TEMP:
			chip->chg_ops->charging_current_write_fast(chip->limits.temp_cold_fastchg_current_ma);
			msleep(50);
			break;
		default:
			break;
	}

#ifdef OPLUS_CHG_OP_DEF
#ifdef CONFIG_OPLUS_CHG_OOS
	warp_chg_led_on_strategy.initialized = false;
	pd9v_chg_led_on_strategy.initialized = false;
	pd5v_chg_led_on_strategy.initialized = false;
#endif
	normal_chg_led_off_strategy.initialized = false;
#endif /* OPLUS_CHG_OP_DEF */

	chip->chg_ops->charging_disable();
	charger_xlog_printk(CHG_LOG_CRTI, "[BATTERY] oplus_chg_turn_off_charging,tbatt_status =%d !!\n", chip->tbatt_status);
}
/*
static int oplus_chg_check_suspend_or_disable(struct oplus_chg_chip *chip)
{
	if(chip->suspend_after_full) {
		if(chip->tbatt_status == BATTERY_STATUS__LITTLE_COLD_TEMP
			|| chip->tbatt_status == BATTERY_STATUS__COOL_TEMP
			|| chip->tbatt_status == BATTERY_STATUS__LITTLE_COOL_TEMP
			|| chip->tbatt_status == BATTERY_STATUS__NORMAL) {
			return CHG_SUSPEND;
		} else if(chip->tbatt_status == BATTERY_STATUS__COLD_TEMP) {
			return CHG_DISABLE;
		} else {
			return CHG_NO_SUSPEND_NO_DISABLE;
		}
	} else {
		if(chip->tbatt_status == BATTERY_STATUS__COLD_TEMP
			|| chip->tbatt_status == BATTERY_STATUS__LITTLE_COLD_TEMP) {
			return CHG_DISABLE;
		} else {
			return CHG_NO_SUSPEND_NO_DISABLE;
		}
	}
}
*/

static int oplus_chg_check_suspend_or_disable(struct oplus_chg_chip *chip)
{
	if (chip->suspend_after_full) {
		if ((chip->tbatt_status == BATTERY_STATUS__HIGH_TEMP
			|| chip->tbatt_status == BATTERY_STATUS__WARM_TEMP) && (chip->batt_volt < 4250)) {
			return CHG_DISABLE;
		} else {
			return CHG_SUSPEND;
		}
	} else {
		return CHG_DISABLE;
	}
}


static void oplus_chg_voter_charging_start(struct oplus_chg_chip *chip,
					OPLUS_CHG_STOP_VOTER voter)
{
	chip->chging_on = true;
	chip->stop_voter &= ~(int)voter;
	oplus_chg_turn_on_charging(chip);

	switch (voter) {
		case CHG_STOP_VOTER__FULL:
			chip->charging_state = CHARGING_STATUS_CCCV;
			if (oplus_warp_get_allow_reading() == true) {
				chip->chg_ops->charger_unsuspend();
				chip->chg_ops->charging_enable();
			}
			break;
		case CHG_STOP_VOTER__VCHG_ABNORMAL:
			chip->charging_state = CHARGING_STATUS_CCCV;
			if (oplus_warp_get_allow_reading() == true) {
				chip->chg_ops->charger_unsuspend();
			}
			break;
#ifdef OPLUS_CHG_OP_DEF
		case CHG_STOP_VOTER_BAD_VOL_DIFF:
#endif
		case CHG_STOP_VOTER__BATTTEMP_ABNORMAL:
		case CHG_STOP_VOTER__VBAT_TOO_HIGH:
		case CHG_STOP_VOTER__MAX_CHGING_TIME:
			chip->charging_state = CHARGING_STATUS_CCCV;
			break;
		default:
			break;
	}
}

static void oplus_chg_voter_charging_stop(struct oplus_chg_chip *chip,
					OPLUS_CHG_STOP_VOTER voter)
{
	chip->chging_on = false;
	chip->stop_voter |= (int)voter;

	switch (voter) {
		case CHG_STOP_VOTER__FULL:
			chip->charging_state = CHARGING_STATUS_FULL;
			if (oplus_warp_get_allow_reading() == true) {
				if (oplus_chg_check_suspend_or_disable(chip) == CHG_SUSPEND) {
					chip->chg_ops->charger_suspend();
				} else {
					oplus_chg_turn_off_charging(chip);
				}
			}
			break;
		case CHG_STOP_VOTER__VCHG_ABNORMAL:
			chip->charging_state = CHARGING_STATUS_FAIL;
			chip->total_time = 0;
			if (oplus_warp_get_allow_reading() == true) {
				chip->chg_ops->charger_suspend();
			}
			oplus_chg_turn_off_charging(chip);
			break;
#ifdef OPLUS_CHG_OP_DEF
		case CHG_STOP_VOTER_BAD_VOL_DIFF:
			chip->charging_state = CHARGING_STATUS_FAIL;
			chip->total_time = 0;
			oplus_chg_turn_off_charging(chip);
			if (oplus_warp_get_fastchg_started() == true) {
				chip->chg_ops->charger_suspend();
			}
		break;
#endif
		case CHG_STOP_VOTER__BATTTEMP_ABNORMAL:
		case CHG_STOP_VOTER__VBAT_TOO_HIGH:
			chip->charging_state = CHARGING_STATUS_FAIL;
			chip->total_time = 0;
			oplus_chg_turn_off_charging(chip);
			break;
		case CHG_STOP_VOTER__MAX_CHGING_TIME:
			chip->charging_state = CHARGING_STATUS_FAIL;
			oplus_chg_turn_off_charging(chip);
			break;
		default:
			break;
		}
}

#define HYSTERISIS_DECIDEGC			20
#define HYSTERISIS_DECIDEGC_0C		5
#define TBATT_PRE_SHAKE_INVALID		999
static void battery_temp_anti_shake_handle(struct oplus_chg_chip *chip)
{
	int tbatt_cur_shake = chip->temperature, low_shake = 0, high_shake = 0;
	int low_shake_0c = 0, high_shake_0c = 0;

	if (tbatt_cur_shake > chip->tbatt_pre_shake) {			/*get warmer*/
		low_shake = -HYSTERISIS_DECIDEGC;
		high_shake = 0;
		low_shake_0c = -HYSTERISIS_DECIDEGC_0C;
		high_shake_0c = 0;
	} else if (tbatt_cur_shake < chip->tbatt_pre_shake) {	/*get cooler*/
		low_shake = 0;
		high_shake = HYSTERISIS_DECIDEGC;
		low_shake_0c = 0;
		high_shake_0c = HYSTERISIS_DECIDEGC_0C;
	}
	if (chip->tbatt_status == BATTERY_STATUS__HIGH_TEMP) {										/*>53C*/
		chip->limits.cold_bat_decidegc = chip->anti_shake_bound.cold_bound;						/*-3C*/
		chip->limits.little_cold_bat_decidegc = chip->anti_shake_bound.little_cold_bound;		/*0C*/
		chip->limits.cool_bat_decidegc = chip->anti_shake_bound.cool_bound;						/*5C*/
		chip->limits.little_cool_bat_decidegc = chip->anti_shake_bound.little_cool_bound;		/*12C*/
		chip->limits.normal_bat_decidegc = chip->anti_shake_bound.normal_bound;					/*16C*/
		chip->limits.warm_bat_decidegc = chip->anti_shake_bound.warm_bound;						/*45C*/
		chip->limits.hot_bat_decidegc = chip->anti_shake_bound.hot_bound + low_shake;			/*53C*/
	} else if (chip->tbatt_status == BATTERY_STATUS__LOW_TEMP) {								/*<-3C*/
		chip->limits.cold_bat_decidegc = chip->anti_shake_bound.cold_bound + high_shake;
		chip->limits.little_cold_bat_decidegc = chip->anti_shake_bound.little_cold_bound;
		chip->limits.cool_bat_decidegc = chip->anti_shake_bound.cool_bound;
		chip->limits.little_cool_bat_decidegc = chip->anti_shake_bound.little_cool_bound;
		chip->limits.normal_bat_decidegc = chip->anti_shake_bound.normal_bound;
		chip->limits.warm_bat_decidegc = chip->anti_shake_bound.warm_bound;
		chip->limits.hot_bat_decidegc = chip->anti_shake_bound.hot_bound;
	} else if (chip->tbatt_status == BATTERY_STATUS__COLD_TEMP) {								/*-3C~0C*/
		chip->limits.cold_bat_decidegc = chip->anti_shake_bound.cold_bound + low_shake;
		chip->limits.little_cold_bat_decidegc = chip->anti_shake_bound.little_cold_bound + high_shake_0c;
		chip->limits.cool_bat_decidegc = chip->anti_shake_bound.cool_bound;
		chip->limits.little_cool_bat_decidegc = chip->anti_shake_bound.little_cool_bound;
		chip->limits.normal_bat_decidegc = chip->anti_shake_bound.normal_bound;
		chip->limits.warm_bat_decidegc = chip->anti_shake_bound.warm_bound;
		chip->limits.hot_bat_decidegc = chip->anti_shake_bound.hot_bound;
	} else if (chip->tbatt_status == BATTERY_STATUS__LITTLE_COLD_TEMP) {						/*0C-5C*/
		chip->limits.cold_bat_decidegc = chip->anti_shake_bound.cold_bound;
		chip->limits.little_cold_bat_decidegc = chip->anti_shake_bound.little_cold_bound + low_shake_0c;
		chip->limits.cool_bat_decidegc = chip->anti_shake_bound.cool_bound + high_shake;
		chip->limits.little_cool_bat_decidegc = chip->anti_shake_bound.little_cool_bound;
		chip->limits.normal_bat_decidegc = chip->anti_shake_bound.normal_bound;
		chip->limits.warm_bat_decidegc = chip->anti_shake_bound.warm_bound;
		chip->limits.hot_bat_decidegc = chip->anti_shake_bound.hot_bound;
	} else if (chip->tbatt_status == BATTERY_STATUS__COOL_TEMP) {								/*5C~12C*/
		chip->limits.cold_bat_decidegc = chip->anti_shake_bound.cold_bound;
		chip->limits.little_cold_bat_decidegc = chip->anti_shake_bound.little_cold_bound;
		chip->limits.cool_bat_decidegc = chip->anti_shake_bound.cool_bound + low_shake;
		chip->limits.little_cool_bat_decidegc = chip->anti_shake_bound.little_cool_bound + high_shake;
		chip->limits.normal_bat_decidegc = chip->anti_shake_bound.normal_bound;
		chip->limits.warm_bat_decidegc = chip->anti_shake_bound.warm_bound;
		chip->limits.hot_bat_decidegc = chip->anti_shake_bound.hot_bound;
	} else if (chip->tbatt_status == BATTERY_STATUS__LITTLE_COOL_TEMP) {						/*12C~16C*/
		chip->limits.cold_bat_decidegc = chip->anti_shake_bound.cold_bound;
		chip->limits.little_cold_bat_decidegc = chip->anti_shake_bound.little_cold_bound;
		chip->limits.cool_bat_decidegc = chip->anti_shake_bound.cool_bound;
		chip->limits.little_cool_bat_decidegc = chip->anti_shake_bound.little_cool_bound + low_shake;
		chip->limits.normal_bat_decidegc = chip->anti_shake_bound.normal_bound + high_shake;
		chip->limits.warm_bat_decidegc = chip->anti_shake_bound.warm_bound;
		chip->limits.hot_bat_decidegc = chip->anti_shake_bound.hot_bound;
	} else if (chip->tbatt_status == BATTERY_STATUS__NORMAL) {									/*16C~45C*/
		chip->limits.cold_bat_decidegc = chip->anti_shake_bound.cold_bound;
		chip->limits.little_cold_bat_decidegc = chip->anti_shake_bound.little_cold_bound;
		chip->limits.cool_bat_decidegc = chip->anti_shake_bound.cool_bound;
		chip->limits.little_cool_bat_decidegc = chip->anti_shake_bound.little_cool_bound;
		chip->limits.normal_bat_decidegc = chip->anti_shake_bound.normal_bound;
		chip->limits.warm_bat_decidegc = chip->anti_shake_bound.warm_bound;
		chip->limits.hot_bat_decidegc = chip->anti_shake_bound.hot_bound;
	} else if (chip->tbatt_status == BATTERY_STATUS__WARM_TEMP) {								/*45C~53C*/
		chip->limits.cold_bat_decidegc = chip->anti_shake_bound.cold_bound;
		chip->limits.little_cold_bat_decidegc = chip->anti_shake_bound.little_cold_bound;
		chip->limits.cool_bat_decidegc = chip->anti_shake_bound.cool_bound;
		chip->limits.little_cool_bat_decidegc = chip->anti_shake_bound.little_cool_bound;
		chip->limits.normal_bat_decidegc = chip->anti_shake_bound.normal_bound;
		chip->limits.warm_bat_decidegc = chip->anti_shake_bound.warm_bound + low_shake;
		chip->limits.hot_bat_decidegc = chip->anti_shake_bound.hot_bound + high_shake;
	} else {	/*BATTERY_STATUS__REMOVED								<-19C*/
		chip->limits.cold_bat_decidegc = chip->anti_shake_bound.cold_bound;
		chip->limits.little_cold_bat_decidegc = chip->anti_shake_bound.little_cold_bound;
		chip->limits.cool_bat_decidegc = chip->anti_shake_bound.cool_bound;
		chip->limits.little_cool_bat_decidegc = chip->anti_shake_bound.little_cool_bound;
		chip->limits.normal_bat_decidegc = chip->anti_shake_bound.normal_bound;
		chip->limits.warm_bat_decidegc = chip->anti_shake_bound.warm_bound;
		chip->limits.hot_bat_decidegc = chip->anti_shake_bound.hot_bound;
	}
	chip->tbatt_pre_shake = tbatt_cur_shake;
}


#define TEMP_CNT	1
static bool oplus_chg_check_tbatt_is_good(struct oplus_chg_chip *chip)
{
	static bool ret = true;
	static int temp_counts = 0;
	int batt_temp = chip->temperature;
	OPLUS_CHG_TBATT_STATUS tbatt_status = chip->tbatt_status;

	if (batt_temp > chip->limits.hot_bat_decidegc || batt_temp < chip->limits.cold_bat_decidegc) {
		temp_counts++;
		if (temp_counts >= TEMP_CNT) {
			temp_counts = 0;
			ret = false;
			if (batt_temp <= chip->limits.removed_bat_decidegc) {
				tbatt_status = BATTERY_STATUS__REMOVED;
			} else if (batt_temp > chip->limits.hot_bat_decidegc) {
				tbatt_status = BATTERY_STATUS__HIGH_TEMP;
			} else {
				tbatt_status = BATTERY_STATUS__LOW_TEMP;
			}
		}
	} else {
		temp_counts = 0;
		ret = true;
		if (batt_temp >= chip->limits.warm_bat_decidegc) {						/*45C*/
			tbatt_status = BATTERY_STATUS__WARM_TEMP;
		} else if (batt_temp >= chip->limits.normal_bat_decidegc) {				/*16C*/
			tbatt_status = BATTERY_STATUS__NORMAL;
		} else if (batt_temp >= chip->limits.little_cool_bat_decidegc) {		/*12C*/
			tbatt_status = BATTERY_STATUS__LITTLE_COOL_TEMP;
		} else if (batt_temp >= chip->limits.cool_bat_decidegc) {				/*5C*/
			tbatt_status = BATTERY_STATUS__COOL_TEMP;
		} else if (batt_temp >= chip->limits.little_cold_bat_decidegc) {		/*0C*/
			tbatt_status = BATTERY_STATUS__LITTLE_COLD_TEMP;
		} else if (batt_temp >= chip->limits.cold_bat_decidegc) {				/*-3C*/
			tbatt_status = BATTERY_STATUS__COLD_TEMP;
		} else {
			tbatt_status = BATTERY_STATUS__COLD_TEMP;
		}
	}
	if (tbatt_status == BATTERY_STATUS__REMOVED) {
		chip->batt_exist = false;
	} else {
		chip->batt_exist = true;
	}
	if (chip->tbatt_pre_shake == TBATT_PRE_SHAKE_INVALID) {
		chip->tbatt_pre_shake = batt_temp;
	}
	if (tbatt_status != chip->tbatt_status) {
		if (chip->tbatt_status == BATTERY_STATUS__WARM_TEMP
					|| chip->tbatt_status == BATTERY_STATUS__COLD_TEMP) {
			if (chip->soc != 100 && chip->batt_full == true
					&& chip->charging_state == CHARGING_STATUS_FULL) {
				chip->batt_full = false;
				chip->tbatt_when_full = 200;
				oplus_chg_voter_charging_start(chip, CHG_STOP_VOTER__FULL);
			}
		}
		chip->tbatt_status = tbatt_status;
		vbatt_higherthan_4180mv = false;
		if (oplus_warp_get_allow_reading() == true) {
			oplus_chg_set_float_voltage(chip);
			oplus_chg_set_charging_current(chip);
		}
		battery_temp_anti_shake_handle(chip);
	}
	return ret;
}

static void oplus_chg_check_tbatt_status(struct oplus_chg_chip *chip)
{
	int batt_temp = chip->temperature;
	OPLUS_CHG_TBATT_STATUS tbatt_status = chip->tbatt_status;

	if (batt_temp > chip->limits.hot_bat_decidegc) {					/*53C*/
		tbatt_status = BATTERY_STATUS__HIGH_TEMP;
	} else if (batt_temp >= chip->limits.warm_bat_decidegc) {			/*45C*/
		tbatt_status = BATTERY_STATUS__WARM_TEMP;
	} else if (batt_temp >= chip->limits.normal_bat_decidegc) {			/*16C*/
		tbatt_status = BATTERY_STATUS__NORMAL;
	} else if (batt_temp >= chip->limits.little_cool_bat_decidegc) {	/*12C*/
		tbatt_status = BATTERY_STATUS__LITTLE_COOL_TEMP;
	} else if (batt_temp >= chip->limits.cool_bat_decidegc) {			/*5C*/
		tbatt_status = BATTERY_STATUS__COOL_TEMP;
	} else if (batt_temp >= chip->limits.little_cold_bat_decidegc) {	/*0C*/
		tbatt_status = BATTERY_STATUS__LITTLE_COLD_TEMP;
	} else if (batt_temp >= chip->limits.cold_bat_decidegc) {			/*-3C*/
		tbatt_status = BATTERY_STATUS__COLD_TEMP;
	} else if (batt_temp > chip->limits.removed_bat_decidegc) {			/*-20C*/
		tbatt_status = BATTERY_STATUS__LOW_TEMP;
	} else {
		tbatt_status = BATTERY_STATUS__REMOVED;
	}
	if (tbatt_status == BATTERY_STATUS__REMOVED) {
		chip->batt_exist = false;
	} else {
		chip->batt_exist = true;
	}
	chip->tbatt_status = tbatt_status;
}

static void battery_temp_normal_anti_shake_handle(struct oplus_chg_chip *chip)
{
        int tbatt_normal_cur_shake = chip->temperature, low_shake = 0, high_shake = 0;

        if (tbatt_normal_cur_shake > chip->tbatt_normal_pre_shake) {                  /*get warmer*/
                low_shake = -HYSTERISIS_DECIDEGC;
                high_shake = 0;
        } else if (tbatt_normal_cur_shake < chip->tbatt_normal_pre_shake) {   /*get cooler*/
                low_shake = 0;
                high_shake = HYSTERISIS_DECIDEGC;
        }

	if (chip->tbatt_normal_status == BATTERY_STATUS__NORMAL_PHASE1) {
		chip->limits.normal_phase1_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase1_bound;
		chip->limits.normal_phase2_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase2_bound + high_shake;
		chip->limits.normal_phase3_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase3_bound;
		chip->limits.normal_phase4_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase4_bound;
		chip->limits.normal_phase5_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase5_bound;
		chip->limits.normal_phase6_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase6_bound;
	} else if (chip->tbatt_normal_status == BATTERY_STATUS__NORMAL_PHASE2) {
		chip->limits.normal_phase1_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase1_bound;
		chip->limits.normal_phase2_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase2_bound + low_shake;
		chip->limits.normal_phase3_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase3_bound + high_shake;
		chip->limits.normal_phase4_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase4_bound;
		chip->limits.normal_phase5_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase5_bound;
		chip->limits.normal_phase6_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase6_bound;
	} else if (chip->tbatt_normal_status == BATTERY_STATUS__NORMAL_PHASE3) {
		chip->limits.normal_phase1_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase1_bound;
		chip->limits.normal_phase2_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase2_bound;
		chip->limits.normal_phase3_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase3_bound + low_shake;
		chip->limits.normal_phase4_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase4_bound + high_shake;
		chip->limits.normal_phase5_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase5_bound;
		chip->limits.normal_phase6_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase6_bound;
	} else if (chip->tbatt_normal_status == BATTERY_STATUS__NORMAL_PHASE4) {
		chip->limits.normal_phase1_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase1_bound;
		chip->limits.normal_phase2_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase2_bound;
		chip->limits.normal_phase3_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase3_bound;
		chip->limits.normal_phase4_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase4_bound + low_shake;
		chip->limits.normal_phase5_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase5_bound + high_shake;
		chip->limits.normal_phase6_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase6_bound;
	} else if (chip->tbatt_normal_status == BATTERY_STATUS__NORMAL_PHASE5) {
		chip->limits.normal_phase1_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase1_bound;
		chip->limits.normal_phase2_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase2_bound;
		chip->limits.normal_phase3_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase3_bound;
		chip->limits.normal_phase4_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase4_bound;
		chip->limits.normal_phase5_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase5_bound + low_shake;
		chip->limits.normal_phase6_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase6_bound + high_shake;
	} else if (chip->tbatt_normal_status == BATTERY_STATUS__NORMAL_PHASE6) {
		chip->limits.normal_phase1_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase1_bound;
		chip->limits.normal_phase2_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase2_bound;
		chip->limits.normal_phase3_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase3_bound;
		chip->limits.normal_phase4_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase4_bound;
		chip->limits.normal_phase5_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase5_bound;
		chip->limits.normal_phase6_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase6_bound + low_shake;
	} else {
		/*do nothing*/
	}

	chip->tbatt_normal_pre_shake = tbatt_normal_cur_shake;
}

static void oplus_chg_check_tbatt_normal_status(struct oplus_chg_chip *chip)
{
        int batt_temp = chip->temperature;
        OPLUS_CHG_TBATT_NORMAL_STATUS tbatt_normal_status = chip->tbatt_normal_status;

	if (chip->tbatt_status == BATTERY_STATUS__NORMAL) {
		if (batt_temp >= chip->limits.normal_phase6_bat_decidegc) {
			tbatt_normal_status = BATTERY_STATUS__NORMAL_PHASE6;
		} else if (batt_temp >= chip->limits.normal_phase5_bat_decidegc) {
			tbatt_normal_status = BATTERY_STATUS__NORMAL_PHASE5;
		} else if (batt_temp >= chip->limits.normal_phase4_bat_decidegc) {
			tbatt_normal_status = BATTERY_STATUS__NORMAL_PHASE4;
		} else if (batt_temp >= chip->limits.normal_phase3_bat_decidegc) {
			tbatt_normal_status = BATTERY_STATUS__NORMAL_PHASE3;
		} else if (batt_temp >= chip->limits.normal_phase2_bat_decidegc) {
			tbatt_normal_status = BATTERY_STATUS__NORMAL_PHASE2;
		} else if (batt_temp >= chip->limits.normal_phase1_bat_decidegc) {
			tbatt_normal_status = BATTERY_STATUS__NORMAL_PHASE1;
                } else {
			/*do nothing*/
		}
	}

	if (chip->tbatt_normal_pre_shake == TBATT_PRE_SHAKE_INVALID) {
		chip->tbatt_pre_shake = batt_temp;
	}

	if (chip->tbatt_normal_status != tbatt_normal_status) {
		chip->tbatt_normal_status = tbatt_normal_status;
		if (oplus_warp_get_allow_reading() == true) {
			oplus_chg_set_float_voltage(chip);
			oplus_chg_set_charging_current(chip);
		}
		battery_temp_normal_anti_shake_handle(chip);
		chg_debug("tbatt_normal_status status change, [%d %d %d %d %d %d %d]\n", chip->tbatt_normal_status,
					chip->limits.normal_phase1_bat_decidegc, chip->limits.normal_phase2_bat_decidegc,
					chip->limits.normal_phase3_bat_decidegc, chip->limits.normal_phase4_bat_decidegc,
					chip->limits.normal_phase5_bat_decidegc, chip->limits.normal_phase6_bat_decidegc);
	}
}

#define VCHG_CNT	2
static bool oplus_chg_check_vchg_is_good(struct oplus_chg_chip *chip)
{
	static bool ret = true;
	static int vchg_counts = 0;
	int chg_volt = chip->charger_volt;
	OPLUS_CHG_VCHG_STATUS vchg_status = chip->vchg_status;

#ifndef OPLUS_CHG_OP_DEF
#ifndef CONFIG_OPLUS_CHARGER_MTK
#ifndef WPC_NEW_INTERFACE
        if (chip->wireless_support && oplus_wireless_charge_start() == true) {
                chg_err(" test wireless fastchg charge start\n");
                return true;
        }
#else
        if (oplus_wpc_get_status() != 0){
                chg_err(" test do not set ichging , wireless charge start \n");
                return;
        }
#endif
#endif
#else /* OPLUS_CHG_OP_DEF */
	if (oplus_chg_is_wls_online(chip)) {
		chg_err(" test do not set ichging , wireless charge start \n");
		return true;
	}
#endif /* OPLUS_CHG_OP_DEF */
	if (oplus_warp_get_adapter_update_status() == ADAPTER_FW_NEED_UPDATE) {
		return true;
	}
	if (oplus_warp_get_fastchg_started() == true) {
		return true;
	}
	if (chg_volt > chip->limits.charger_hv_thr) {
		vchg_counts++;
		if (vchg_counts >= VCHG_CNT) {
			vchg_counts = 0;
			ret = false;
			vchg_status = CHARGER_STATUS__VOL_HIGH;
		}
	} else  if (chg_volt <= chip->limits.charger_recv_thr) {
		vchg_counts = 0;
		ret = true;
		vchg_status = CHARGER_STATUS__GOOD;
	}
	if (vchg_status != chip->vchg_status) {
		chip->vchg_status = vchg_status;
	}
	return ret;
}

#define VBAT_CNT	1

static bool oplus_chg_check_vbatt_is_good(struct oplus_chg_chip *chip)
{
	static bool ret = true;
	static int vbat_counts = 0;
	int batt_volt = chip->batt_volt;

	if (batt_volt >= chip->limits.vbatt_hv_thr) {
		vbat_counts++;
		if (vbat_counts >= VBAT_CNT) {
			vbat_counts = 0;
			ret = false;
			chip->vbatt_over = true;
		}
	} else {
		vbat_counts = 0;
		ret = true;
		chip->vbatt_over = false;
	}
	return ret;
}

static bool oplus_chg_check_time_is_good(struct oplus_chg_chip *chip)
{
#ifdef SELL_MODE
	chip->chging_over_time = false;
	printk("oplus_chg_check_time_is_good_sell_mode\n");
	return true;
#endif //SELL_MODE

	if (chip->limits.max_chg_time_sec < 0) {
		chip->chging_over_time = false;
		return true;
	}
	if (chip->total_time >= chip->limits.max_chg_time_sec) {
		chip->total_time = chip->limits.max_chg_time_sec;
		chip->chging_over_time = true;
		return false;
	} else {
		chip->chging_over_time = false;
		return true;
	}
}

#ifndef CONFIG_OPLUS_CHG_OOS
#ifdef OPLUS_CHG_OP_DEF //CONFIG_FB nick.hu todo
#ifdef CONFIG_DRM_MSM
static int fb_notifier_callback(struct notifier_block *nb,
		unsigned long event, void *data)
{
	int blank;
	struct msm_drm_notifier *evdata = data;

	if (!g_charger_chip) {
		return 0;
	}
	if (!evdata || (evdata->id != 0)){
		return 0;
	}

	if (event == MSM_DRM_EARLY_EVENT_BLANK) {
		blank = *(int *)(evdata->data);
		if (blank == MSM_DRM_BLANK_UNBLANK) {
#ifndef OPLUS_CHG_OP_DEF
			g_charger_chip->led_on = true;
			g_charger_chip->led_on_change = true;
#else
			cancel_delayed_work_sync(&g_charger_chip->led_power_on_report_work);
			/* Frequent turning on and off of the screen during the charging scene
			 * may result in slow charging. Adding a 1-minute delay here can effectively
			 * intercept short-lived screen-on events without affecting heat.
			 */
			schedule_delayed_work(&g_charger_chip->led_power_on_report_work, msecs_to_jiffies(60000));
#endif
		} else if (blank == MSM_DRM_BLANK_POWERDOWN) {
#ifdef OPLUS_CHG_OP_DEF
			cancel_delayed_work_sync(&g_charger_chip->led_power_on_report_work);
#endif
			g_charger_chip->led_on = false;
			g_charger_chip->led_on_change = true;
#ifdef OPLUS_CHG_OP_DEF
			if (is_usb_ocm_available(g_charger_chip))
				oplus_chg_anon_mod_event(g_charger_chip->usb_ocm, OPLUS_CHG_EVENT_LCD_OFF);
			if (is_wls_ocm_available(g_charger_chip))
				oplus_chg_anon_mod_event(g_charger_chip->wls_ocm, OPLUS_CHG_EVENT_LCD_OFF);
#endif
		} else {
			pr_err("%s: receives wrong data EARLY_BLANK:%d\n", __func__, blank);
		}
	}
	return 0;
}
#else
static int fb_notifier_callback(struct notifier_block *nb,
		unsigned long event, void *data)
{
	int blank;
	struct fb_event *evdata = data;

	if (!g_charger_chip) {
		return 0;
	}
	if (evdata && evdata->data) {
		if (event == FB_EVENT_BLANK) {
			blank = *(int *)evdata->data;
			if (blank == FB_BLANK_UNBLANK) {
				g_charger_chip->led_on = true;
				g_charger_chip->led_on_change = true;
			} else if (blank == FB_BLANK_POWERDOWN) {
				g_charger_chip->led_on = false;
				g_charger_chip->led_on_change = true;
			}
		}
	}
	return 0;
}
#endif /* CONFIG_DRM_MSM */

void oplus_chg_set_allow_switch_to_fastchg(bool allow)
{
	charger_xlog_printk(CHG_LOG_CRTI, " allow = %d\n", allow);
	if (!g_charger_chip) {
		return;
	} else {
		g_charger_chip->allow_swtich_to_fastchg = allow;
	}
}

void oplus_chg_set_led_status(bool val)
{
	/*Do nothing*/
}
EXPORT_SYMBOL(oplus_chg_set_led_status);
#else
void oplus_chg_set_led_status(bool val)
{
	charger_xlog_printk(CHG_LOG_CRTI, " val = %d\n", val);
	if (!g_charger_chip) {
		return;
	} else {
		g_charger_chip->led_on = val;
		g_charger_chip->led_on_change = true;
	}
}
EXPORT_SYMBOL(oplus_chg_set_led_status);
#endif
#else /* OPLUS_CHG_OP_DEF */
void oplus_chg_set_led_status(bool val)
{
	charger_xlog_printk(CHG_LOG_CRTI, " val = %d\n", val);
	if (!g_charger_chip) {
		return;
	} else {
		g_charger_chip->led_on = val;
		g_charger_chip->led_on_change = true;
	}
}
EXPORT_SYMBOL(oplus_chg_set_led_status);
#endif /* OPLUS_CHG_OP_DEF */

void oplus_chg_set_camera_status(bool val)
{
	if (!g_charger_chip) {
		return;
	} else {
		g_charger_chip->camera_on = val;
	}
}
EXPORT_SYMBOL(oplus_chg_set_camera_status);

#define TLED_CHANGE_COUNTS	4
#define TLED_HYSTERISIS_DECIDEGC	10
static void oplus_chg_check_tled_status(struct oplus_chg_chip *chip)
{
	OPLUS_CHG_TLED_STATUS tled_status = chip->led_temp_status;
	static int high_counts = 0, warm_counts = 0, normal_counts = 0;

	if (chip->temperature > chip->limits.led_high_bat_decidegc_antishake) {		/* >37C */
		high_counts ++;
		if (high_counts >= TLED_CHANGE_COUNTS) {
			high_counts = 0;
			warm_counts = 0;
			normal_counts = 0;
			tled_status = LED_TEMP_STATUS__HIGH;
		}
	} else if (chip->temperature > chip->limits.led_warm_bat_decidegc_antishake) {	/* >35C && <= 37 */
		warm_counts ++;
		if (warm_counts >= TLED_CHANGE_COUNTS) {
			high_counts = 0;
			warm_counts = 0;
			normal_counts = 0;
			tled_status = LED_TEMP_STATUS__WARM;
		}
	} else {
		normal_counts ++;
		if (normal_counts >= TLED_CHANGE_COUNTS) {
			high_counts = 0;
			warm_counts = 0;
			normal_counts = 0;
			tled_status = LED_TEMP_STATUS__NORMAL;
		}
	}
	if (tled_status != chip->led_temp_status) {
		chip->limits.led_warm_bat_decidegc_antishake
				= chip->limits.led_warm_bat_decidegc;
		chip->limits.led_high_bat_decidegc_antishake
				= chip->limits.led_high_bat_decidegc;
		if (tled_status > chip->led_temp_status
				&& tled_status == LED_TEMP_STATUS__WARM){
			chip->limits.led_warm_bat_decidegc_antishake
				= chip->limits.led_warm_bat_decidegc - TLED_HYSTERISIS_DECIDEGC;
		} else if (tled_status > chip->led_temp_status
				&& tled_status == LED_TEMP_STATUS__HIGH){
			chip->limits.led_high_bat_decidegc_antishake
				= chip->limits.led_high_bat_decidegc - TLED_HYSTERISIS_DECIDEGC;
		} else if (tled_status < chip->led_temp_status
				&& tled_status == LED_TEMP_STATUS__NORMAL){
			chip->limits.led_warm_bat_decidegc_antishake
				= chip->limits.led_warm_bat_decidegc + TLED_HYSTERISIS_DECIDEGC;
		} else if (tled_status < chip->led_temp_status
				&& tled_status == LED_TEMP_STATUS__WARM){
			chip->limits.led_high_bat_decidegc_antishake
				= chip->limits.led_high_bat_decidegc + TLED_HYSTERISIS_DECIDEGC;
		}
		chg_debug("tled status change, [%d %d %d %d]\n",
				tled_status,
				chip->led_temp_status,
				chip->limits.led_warm_bat_decidegc_antishake,
				chip->limits.led_high_bat_decidegc_antishake);
		chip->led_temp_change = true;
		chip->led_temp_status = tled_status;
	}
}

static void oplus_chg_check_led_on_ichging(struct oplus_chg_chip *chip)
{
	if (chip->led_on_change || (chip->led_on && chip->led_temp_change)) {
		chip->led_on_change = false;
		chip->led_temp_change = false;
		if (chip->chg_ctrl_by_warp && chip->vbatt_num == 2) {
			if (oplus_warp_get_fastchg_started() == true
					&& oplus_warp_get_fast_chg_type()
					!= CHARGER_SUBTYPE_FASTCHG_WARP) {
				return;
			}
			if (oplus_warp_get_allow_reading() == true
					|| oplus_warp_get_fast_chg_type()
					== CHARGER_SUBTYPE_FASTCHG_WARP) {
				oplus_chg_set_charging_current(chip);
				oplus_chg_set_input_current_limit(chip);
			}
		} else {
			if (oplus_warp_get_fastchg_started() == true) {
				return;
			}
			if (oplus_warp_get_allow_reading() == true) {
				if (chip->dual_charger_support) {
					chip->slave_charger_enable = false;
				}
				oplus_chg_set_charging_current(chip);
				oplus_chg_set_input_current_limit(chip);
			}
		}
	}
}

#define TWARP_COUNTS	2
#define TWARP_HYSTERISIS_DECIDEGC	10

static void oplus_chg_check_warp_temp_status(struct oplus_chg_chip *chip)
{
	int batt_temp = chip->temperature;
	OPLUS_CHG_TBAT_WARP_STATUS tbat_warp_status = chip->warp_temp_status;
	static int high_counts = 0, warm_counts = 0, normal_counts = 0;
	static bool warp_first_set_input_current_flag = false;

	if (chip->vbatt_num != 2
			|| oplus_warp_get_fast_chg_type() != CHARGER_SUBTYPE_FASTCHG_WARP
			|| oplus_warp_get_fastchg_started() == false) {
		warp_first_set_input_current_flag = false;
		return;
	}
	if (batt_temp > chip->limits.warp_high_bat_decidegc) {			/*>45C*/
		if (oplus_warp_get_fastchg_started() == true) {
				chg_err("tbatt > 45, quick out warp");
				oplus_chg_set_chargerid_switch_val(0);
				oplus_warp_switch_mode(NORMAL_CHARGER_MODE);
		}
	} else if (batt_temp > chip->limits.warp_warm_bat_decidegc_antishake) {	/*>38C && <= 45*/
		high_counts ++;
		if(high_counts >= TWARP_COUNTS)
		{
			high_counts = 0;
			warm_counts = 0;
			normal_counts = 0;
			tbat_warp_status = WARP_TEMP_STATUS__HIGH;
		}
	 } else if (batt_temp >= chip->limits.warp_normal_bat_decidegc_antishake) { /*>34C && <= 38*/
		warm_counts ++;
		if (warm_counts >= TWARP_COUNTS)
		{
			high_counts = 0;
			warm_counts = 0;
			normal_counts = 0;
			tbat_warp_status = WARP_TEMP_STATUS__WARM;
		}
	} else {								// < 34
		normal_counts ++;
		if (normal_counts >= TWARP_COUNTS)
		{
			high_counts = 0;
			warm_counts = 0;
			normal_counts = 0;
			tbat_warp_status = WARP_TEMP_STATUS__NORMAL;
		}
	}
	chg_err("tbat_warp_status[%d],chip->warp_temp_status[%d] ",
		tbat_warp_status, chip->warp_temp_status);
	if (warp_first_set_input_current_flag == false) {
		chip->limits.temp_little_cool_fastchg_current_ma
			= chip->limits.charger_current_warp_ma_normal;
		chip->limits.temp_normal_fastchg_current_ma
			= chip->limits.charger_current_warp_ma_normal;
		oplus_chg_set_charging_current(chip);
		chg_err("set charger current ctrl by warp[%d]\n",
			chip->limits.temp_little_cool_fastchg_current_ma);
	}
	if (tbat_warp_status != chip->warp_temp_status
			|| warp_first_set_input_current_flag == false) {
		chip->limits.warp_warm_bat_decidegc_antishake
			= chip->limits.warp_warm_bat_decidegc;
		chip->limits.warp_normal_bat_decidegc_antishake
			= chip->limits.warp_normal_bat_decidegc;
		if (tbat_warp_status > chip->warp_temp_status
				&& tbat_warp_status == WARP_TEMP_STATUS__WARM) {
			chip->limits.warp_normal_bat_decidegc_antishake
				= chip->limits.warp_normal_bat_decidegc - TWARP_HYSTERISIS_DECIDEGC;
		} else if (tbat_warp_status > chip->warp_temp_status
				&& tbat_warp_status == WARP_TEMP_STATUS__HIGH) {
			chip->limits.warp_warm_bat_decidegc_antishake
				= chip->limits.warp_warm_bat_decidegc - TWARP_HYSTERISIS_DECIDEGC;
		} else if (tbat_warp_status < chip->warp_temp_status
				&& tbat_warp_status == WARP_TEMP_STATUS__NORMAL) {
			chip->limits.warp_normal_bat_decidegc_antishake
				= chip->limits.warp_normal_bat_decidegc + TWARP_HYSTERISIS_DECIDEGC;
		} else if (tbat_warp_status < chip->warp_temp_status
				&& tbat_warp_status == WARP_TEMP_STATUS__WARM) {
			chip->limits.warp_warm_bat_decidegc_antishake
				= chip->limits.warp_warm_bat_decidegc + TWARP_HYSTERISIS_DECIDEGC;
		}
		chg_debug("tled status change, [%d %d %d %d]\n",
			tbat_warp_status, chip->warp_temp_status,
		chip->limits.warp_warm_bat_decidegc_antishake,
			chip->limits.warp_normal_bat_decidegc_antishake);
		warp_first_set_input_current_flag = true;
		chip->warp_temp_change = true;
		chip->warp_temp_status = tbat_warp_status;
	}

}

static void oplus_chg_check_warp_ichging(struct oplus_chg_chip *chip)
{
	if (chip->vbatt_num == 2 && chip->warp_temp_change) {
		chip->warp_temp_change = false;
		if (oplus_warp_get_fast_chg_type() == CHARGER_SUBTYPE_FASTCHG_WARP) {
			oplus_chg_set_input_current_limit(chip);
		}
	}
}

static void oplus_chg_check_cool_down_ichging(struct oplus_chg_chip *chip)
{
	chip->cool_down_done = false;
	if (oplus_warp_get_allow_reading() == true) {
		if (chip->dual_charger_support) {
			chip->slave_charger_enable = false;
			oplus_chg_set_charging_current(chip);
		}
		oplus_chg_set_input_current_limit(chip);
	}
}

static void oplus_chg_check_camera_on_ichging(struct oplus_chg_chip *chip)
{
	static bool camera_pre = false;

	if (chip->camera_on != camera_pre) {
		camera_pre = chip->camera_on;
		if (oplus_warp_get_fastchg_started() == true) {
			return;
		}
		if (oplus_warp_get_allow_reading() == true) {
			if (chip->dual_charger_support) {
				chip->slave_charger_enable = false;
				oplus_chg_set_charging_current(chip);
			}
			oplus_chg_set_input_current_limit(chip);
#ifdef OPLUS_CHG_OP_DEF
			oplus_chg_set_charging_current(chip);
#endif
		}
	}
}

static void oplus_chg_check_calling_on_ichging(struct oplus_chg_chip *chip)
{
	static bool calling_pre = false;

	if (chip->calling_on != calling_pre) {
		calling_pre = chip->calling_on;
		if (oplus_warp_get_fastchg_started() == true) {
			return;
		}
		if (oplus_warp_get_allow_reading() == true) {
			if (chip->dual_charger_support) {
				chip->slave_charger_enable = false;
				oplus_chg_set_charging_current(chip);
			}
			oplus_chg_set_input_current_limit(chip);
#ifdef OPLUS_CHG_OP_DEF
			oplus_chg_set_charging_current(chip);
#endif
		}
	}
}

static void oplus_chg_battery_authenticate_check(struct oplus_chg_chip *chip)
{
	static bool charger_exist_pre = false;

	if (charger_exist_pre ^ chip->charger_exist) {
		charger_exist_pre = chip->charger_exist;
		if (chip->charger_exist && !chip->authenticate) {
			chip->authenticate = oplus_gauge_get_batt_authenticate();
#ifdef OPLUS_CHG_OP_DEF
		if (chip->bat_volt_different)
			chip->authenticate = false;
#endif
		}
	}
}

void oplus_chg_variables_reset(struct oplus_chg_chip *chip, bool in)
{
#ifdef OPLUS_CHG_OP_DEF
	int i;
#endif

	if (in) {
		chip->charger_exist = true;
		chip->chging_on = true;
		chip->slave_charger_enable = false;
#ifdef OPLUS_CHG_OP_DEF
		chip->charger_exist_delay = false;
#endif
	} else {
		if(!oplus_chg_show_warp_logo_ornot()) {
			if(chip->decimal_control) {
				cancel_delayed_work_sync(&g_charger_chip->ui_soc_decimal_work);
				chip->last_decimal_ui_soc = (chip->ui_soc_integer + chip->ui_soc_decimal);
				oplus_chg_ui_soc_decimal_deinit();
				pr_err("[oplus_chg_variables_reset]cancel last_decimal_ui_soc:%d", chip->last_decimal_ui_soc);
			}
			chip->calculate_decimal_time = 0;
		}
		chip->allow_swtich_to_fastchg = 1;
		chip->charger_exist = false;
		chip->chging_on = false;
		chip->charger_type = POWER_SUPPLY_TYPE_UNKNOWN;
		chip->real_charger_type = POWER_SUPPLY_TYPE_UNKNOWN;
		vbatt_higherthan_4180mv = false;
		suspend_charger = false;
		chip->pd_swarp = false;
#ifdef OPLUS_CHG_OP_DEF
		chip->is_oplus_svid = false;
		chip->charger_exist_delay = true;
#endif
	}
	chip->qc_abnormal_check_count = 0;
	chip->limits.vbatt_pdqc_to_9v_thr = oplus_get_vbatt_pdqc_to_9v_thr();
	chip->sw_full_count = 0;
	chip->sw_full = false;
	chip->hw_full_by_sw = false;
	/*chip->charger_volt = 5000;*/
	chip->vchg_status = CHARGER_STATUS__GOOD;
#ifdef OPLUS_CHG_OP_DEF
	if (!oplus_chg_is_wls_present(chip))
		chip->batt_full = false;
#else
	chip->batt_full = false;
#endif
	chip->tbatt_when_full = 200;
	chip->recharge_after_ffc = false;
	chip->tbatt_status = BATTERY_STATUS__NORMAL;
	chip->tbatt_pre_shake = TBATT_PRE_SHAKE_INVALID;
	chip->tbatt_normal_status = BATTERY_STATUS__NORMAL_PHASE1;
	chip->tbatt_normal_pre_shake = TBATT_PRE_SHAKE_INVALID;
	chip->vbatt_over = 0;
	chip->total_time = 0;
	chip->chging_over_time = 0;
	chip->in_rechging = 0;
	/*chip->batt_volt = 0;*/
	/*chip->temperature = 0;*/
	chip->stop_voter = 0x00;
	chip->charging_state = CHARGING_STATUS_CCCV;
#ifdef OPLUS_CHG_OP_DEF
	chip->chg_strategy_batt_curr_ma = 0;
	spin_lock(&chip->strategy_lock);
	chip->strategy = NULL;
	spin_unlock(&chip->strategy_lock);
	chip->start_pd_check = false;
	chip->chg_config_init = false;
	for (i = 0; i < ARRAY_SIZE(chip->ibat_save); i++)
		chip->ibat_save[i] = 0;
#ifdef OPLUS_CHG_REG_DUMP_ENABLE
	chip->reg_dump = false;
#endif
#endif
#ifndef SELL_MODE
	if(chip->mmi_fastchg == 0){
		chip->mmi_chg = 0;
	} else {
#ifdef OPLUS_CHG_OP_DEF
		if (!chip->charging_suspend)
			chip->mmi_chg = 1;
#else
		chip->mmi_chg = 1;
#endif
		charger_xlog_printk(CHG_LOG_CRTI, "set mmi_chg = [%d].\n", chip->mmi_chg);
	}
#endif //SELL_MODE
	chip->unwakelock_chg = 0;
	chip->notify_code = 0;
	chip->notify_flag = 0;
	if (!((chip->charger_type == POWER_SUPPLY_TYPE_USB_DCP)
				|| (oplus_warp_get_fastchg_started() == true)
				|| (oplus_warp_get_fastchg_to_warm() == true)
				|| (oplus_warp_get_fastchg_dummy_started() == true)
				|| (oplus_warp_get_adapter_update_status() == ADAPTER_FW_NEED_UPDATE)
				|| (oplus_warp_get_btb_temp_over() == true))) {
		chip->cool_down = 0;
		chip->cool_down_done = false;
	}
	chip->cool_down_force_5v = false;
	chip->limits.cold_bat_decidegc = chip->anti_shake_bound.cold_bound;
	chip->limits.little_cold_bat_decidegc
		= chip->anti_shake_bound.little_cold_bound;
	chip->limits.cool_bat_decidegc = chip->anti_shake_bound.cool_bound;
	chip->limits.little_cool_bat_decidegc
		= chip->anti_shake_bound.little_cool_bound;
	chip->limits.normal_bat_decidegc = chip->anti_shake_bound.normal_bound;
	chip->limits.warm_bat_decidegc = chip->anti_shake_bound.warm_bound;
	chip->limits.hot_bat_decidegc = chip->anti_shake_bound.hot_bound;

	chip->limits.normal_phase1_bat_decidegc
		= chip->tbatt_normal_anti_shake_bound.phase1_bound;
	chip->limits.normal_phase2_bat_decidegc
                = chip->tbatt_normal_anti_shake_bound.phase2_bound;
	chip->limits.normal_phase3_bat_decidegc
                = chip->tbatt_normal_anti_shake_bound.phase3_bound;
	chip->limits.normal_phase4_bat_decidegc
                = chip->tbatt_normal_anti_shake_bound.phase4_bound;
	chip->limits.normal_phase5_bat_decidegc
                = chip->tbatt_normal_anti_shake_bound.phase5_bound;
	chip->limits.normal_phase6_bat_decidegc
                = chip->tbatt_normal_anti_shake_bound.phase6_bound;

	chip->limits.vfloat_over_counts = 0;
#ifdef OPLUS_CHG_OP_DEF
	// chip->check_battery_vol_count = 0;
#endif
	chip->limits.led_warm_bat_decidegc_antishake
		= chip->limits.led_warm_bat_decidegc;
	chip->limits.led_high_bat_decidegc_antishake
		= chip->limits.led_high_bat_decidegc;
	chip->led_temp_change = false;
	chip->limits.warp_warm_bat_decidegc_antishake
		= chip->limits.warp_warm_bat_decidegc;
	chip->limits.warp_normal_bat_decidegc_antishake
		= chip->limits.warp_normal_bat_decidegc;
	chip->warp_temp_change = false;
	if (chip->temperature > chip->limits.led_high_bat_decidegc) {
		chip->led_temp_status = LED_TEMP_STATUS__HIGH;
	}else if (chip->temperature > chip->limits.led_warm_bat_decidegc) {
		chip->led_temp_status = LED_TEMP_STATUS__WARM;
	}else{
		chip->led_temp_status = LED_TEMP_STATUS__NORMAL;
	}
	chip->dod0_counts = 0;
	chip->fastchg_to_ffc = false;
	chip->fastchg_ffc_status = 0;
	chip->chg_ctrl_by_lcd = chip->chg_ctrl_by_lcd_default;
	chip->chg_ctrl_by_warp = chip->chg_ctrl_by_warp_default;
	chip->ffc_temp_status = FFC_TEMP_STATUS__NORMAL;
	chip->warp_temp_status = WARP_TEMP_STATUS__NORMAL;
	chip->limits.iterm_ma = chip->limits.default_iterm_ma;
	chip->limits.temp_normal_fastchg_current_ma
		= chip->limits.default_temp_normal_fastchg_current_ma;
	chip->limits.normal_vfloat_sw_limit
		= chip->limits.default_normal_vfloat_sw_limit;
	chip->limits.temp_normal_vfloat_mv
		= chip->limits.default_temp_normal_vfloat_mv;
	chip->limits.normal_vfloat_over_sw_limit
		= chip->limits.default_normal_vfloat_over_sw_limit;
	chip->limits.temp_little_cool_fastchg_current_ma
		= chip->limits.default_temp_little_cool_fastchg_current_ma;
	chip->limits.little_cool_vfloat_sw_limit
		= chip->limits.default_little_cool_vfloat_sw_limit;
	chip->limits.temp_little_cool_vfloat_mv
		= chip->limits.default_temp_little_cool_vfloat_mv;
	chip->limits.little_cool_vfloat_over_sw_limit
		= chip->limits.default_little_cool_vfloat_over_sw_limit;
	chip->limits.temp_little_cool_fastchg_current_ma_high
		= chip->limits.default_temp_little_cool_fastchg_current_ma_high;
	chip->limits.temp_little_cool_fastchg_current_ma_low
		= chip->limits.default_temp_little_cool_fastchg_current_ma_low;
	chip->limits.temp_little_cold_fastchg_current_ma_high
		= chip->limits.default_temp_little_cold_fastchg_current_ma_high;
	chip->limits.temp_little_cold_fastchg_current_ma_low
		= chip->limits.default_temp_little_cold_fastchg_current_ma_low;
	chip->limits.temp_cool_fastchg_current_ma_high
		= chip->limits.default_temp_cool_fastchg_current_ma_high;
	chip->limits.temp_cool_fastchg_current_ma_low
		= chip->limits.default_temp_cool_fastchg_current_ma_low;
	chip->limits.temp_warm_fastchg_current_ma
		= chip->limits.default_temp_warm_fastchg_current_ma;
	chip->limits.input_current_charger_ma
		= chip->limits.default_input_current_charger_ma;
	chip->limits.input_current_warp_ma_high
		= chip->limits.default_input_current_warp_ma_high;
	chip->limits.input_current_warp_ma_warm
		= chip->limits.default_input_current_warp_ma_warm;
	chip->limits.input_current_warp_ma_normal
		= chip->limits.default_input_current_warp_ma_normal;
	chip->limits.pd_input_current_charger_ma
		= chip->limits.default_pd_input_current_charger_ma;
	chip->limits.qc_input_current_charger_ma
		= chip->limits.default_qc_input_current_charger_ma;
	oplus_chg_aging_ffc_variable_reset(chip);
	reset_mcu_delay = 0;
#ifndef CONFIG_OPLUS_CHARGER_MTK
	chip->pmic_spmi.aicl_suspend = false;
#endif
	oplus_chg_battery_authenticate_check(chip);
	chip->chargerid_volt = 0;
	chip->chargerid_volt_got = false;
	chip->short_c_batt.in_idle = true;//defualt in idle for userspace
	chip->short_c_batt.cv_satus = false;//defualt not in cv chg
	chip->short_c_batt.disable_rechg = false;
	chip->short_c_batt.limit_chg = false;
	chip->short_c_batt.limit_rechg = false;
	chip->chg_ctrl_by_cool_down = false;
	chip->smart_charge_user = SMART_CHARGE_USER_OTHER;
	chip->usbtemp_cool_down = 0;
	chip->pd_chging = false;
}

static void oplus_chg_variables_init(struct oplus_chg_chip *chip)
{
	chip->charger_exist = false;
	chip->chging_on = false;
	chip->charger_type = POWER_SUPPLY_TYPE_UNKNOWN;
	chip->real_charger_type = POWER_SUPPLY_TYPE_UNKNOWN;
	chip->charger_volt = 0;
	chip->vchg_status = CHARGER_STATUS__GOOD;
#ifdef CONFIG_OPLUS_CHG_OOS
	chip->prop_status = POWER_SUPPLY_STATUS_DISCHARGING;
#else
	chip->prop_status = POWER_SUPPLY_STATUS_NOT_CHARGING;
#endif
	chip->sw_full_count = 0;
	chip->sw_full = false;
	chip->hw_full_by_sw = false;
	chip->batt_exist = true;
	chip->batt_full = false;
	chip->tbatt_when_full = 200;
	chip->recharge_after_ffc = false;
	chip->tbatt_status = BATTERY_STATUS__NORMAL;
	chip->tbatt_normal_status = BATTERY_STATUS__NORMAL_PHASE1;
	chip->vbatt_over = 0;
	chip->total_time = 0;
	chip->chging_over_time = 0;
	chip->in_rechging = 0;
	//chip->batt_volt = 3800 * chip->vbatt_num;
	chip->batt_volt = 3800;
	chip->icharging = 0;
	chip->temperature = 250;
	chip->shell_temp = TEMPERATURE_INVALID;
	chip->soc = 0;
	chip->ui_soc = 50;
	chip->notify_code = 0;
	chip->notify_flag = 0;
	chip->cool_down = 0;
	chip->tbatt_pre_shake = TBATT_PRE_SHAKE_INVALID;
	chip->tbatt_normal_pre_shake = TBATT_PRE_SHAKE_INVALID;
	chip->led_on = true;
	chip->camera_on = 0;
	chip->stop_voter = 0x00;
	chip->charging_state = CHARGING_STATUS_CCCV;
	chip->mmi_chg = 1;
	chip->unwakelock_chg = 0;
	chip->chg_powersave = false;
	chip->allow_swtich_to_fastchg = 1;
	chip->stop_chg= 1;
	chip->mmi_fastchg = 1;
	chip->cool_down_done = false;
	chip->healthd_ready = false;
	chip->dischg_flag = false;
	chip->usb_status = 0;
	init_waitqueue_head( &chip->oplus_usbtemp_wq);
	chip->smooth_to_soc_gap = 5;
	chip->smart_charge_user = SMART_CHARGE_USER_OTHER;
	chip->usbtemp_cool_down = 0;
#ifdef CONFIG_OPLUS_CHARGER_MTK
	chip->usb_online = false;
	chip->otg_online = false;
#else
/*	chip->pmic_spmi.usb_online = false;
		IC have init already	*/
#endif
#ifdef OPLUS_CHG_OP_DEF
	chip->svid_verified = false;
	chip->is_oplus_svid = false;
	chip->usb_enum_status = false;
	chip->chg_redetect_charger_type = false;
#ifdef OPLUS_CHG_REG_DUMP_ENABLE
	chip->reg_dump = false;
#endif
#endif
	if(chip->external_gauge) {
		chg_debug("use oplus_gauge_get_batt_authenticate\n");
		chip->authenticate = oplus_gauge_get_batt_authenticate();
#ifdef OPLUS_CHG_OP_DEF
		if (chip->bat_volt_different)
			chip->authenticate = false;
#endif
		chip->hmac = oplus_gauge_get_batt_hmac();
	} else {
		chg_debug("use get_oplus_high_battery_status\n");
		//chip->authenticate = get_oplus_high_battery_status();
		chip->authenticate = oplus_gauge_get_batt_authenticate();
#ifdef OPLUS_CHG_OP_DEF
		if (chip->bat_volt_different)
			chip->authenticate = false;
#endif
		chip->hmac = true;
	}
	if (!chip->authenticate) {
		//chip->chg_ops->charger_suspend();
		chip->chg_ops->charging_disable();
	}
	chip->otg_switch = false;
	chip->ui_otg_switch = false;
	chip->boot_mode = chip->chg_ops->get_boot_mode();
	chip->boot_reason = chip->chg_ops->get_boot_reason();
	chip->anti_shake_bound.cold_bound = chip->limits.cold_bat_decidegc;
	chip->anti_shake_bound.little_cold_bound
		= chip->limits.little_cold_bat_decidegc;
	chip->anti_shake_bound.cool_bound = chip->limits.cool_bat_decidegc;
	chip->anti_shake_bound.little_cool_bound
		= chip->limits.little_cool_bat_decidegc;
	chip->anti_shake_bound.normal_bound = chip->limits.normal_bat_decidegc;
	chip->anti_shake_bound.warm_bound = chip->limits.warm_bat_decidegc;
	chip->anti_shake_bound.hot_bound = chip->limits.hot_bat_decidegc;

	chip->tbatt_normal_anti_shake_bound.phase1_bound
		= chip->limits.normal_phase1_bat_decidegc;
	chip->tbatt_normal_anti_shake_bound.phase2_bound
                = chip->limits.normal_phase2_bat_decidegc;
	chip->tbatt_normal_anti_shake_bound.phase3_bound
                = chip->limits.normal_phase3_bat_decidegc;
	chip->tbatt_normal_anti_shake_bound.phase4_bound
                = chip->limits.normal_phase4_bat_decidegc;
	chip->tbatt_normal_anti_shake_bound.phase5_bound
                = chip->limits.normal_phase5_bat_decidegc;
	chip->tbatt_normal_anti_shake_bound.phase6_bound
                = chip->limits.normal_phase6_bat_decidegc;

	chip->limits.led_warm_bat_decidegc_antishake
		= chip->limits.led_warm_bat_decidegc;
	chip->limits.led_high_bat_decidegc_antishake
		= chip->limits.led_high_bat_decidegc;
	chip->led_temp_change = false;
	chip->limits.warp_warm_bat_decidegc_antishake
		= chip->limits.warp_warm_bat_decidegc;
	chip->limits.warp_normal_bat_decidegc_antishake
		= chip->limits.warp_normal_bat_decidegc;
	chip->warp_temp_change = false;

	if (chip->temperature > chip->limits.led_high_bat_decidegc)
		chip->led_temp_status = LED_TEMP_STATUS__HIGH;
	else if (chip->temperature > chip->limits.led_warm_bat_decidegc)
		chip->led_temp_status = LED_TEMP_STATUS__WARM;
	else
		chip->led_temp_status = LED_TEMP_STATUS__NORMAL;
//	chip->anti_shake_bound.overtemp_bound = chip->limits.overtemp_bat_decidegc;
	chip->limits.vfloat_over_counts = 0;
#ifdef OPLUS_CHG_OP_DEF
	chip->check_battery_vol_count = 0;
	chip->abnormal_volt_detected = false;
#endif
	chip->chargerid_volt = 0;
	chip->chargerid_volt_got = false;
	chip->enable_shipmode = 0;
	chip->dod0_counts = 0;
	chip->fastchg_to_ffc = false;
	chip->fastchg_ffc_status = 0;
	chip->ffc_temp_status = FFC_TEMP_STATUS__NORMAL;
	chip->warp_temp_status = WARP_TEMP_STATUS__NORMAL;
	chip->short_c_batt.err_code = oplus_short_c_batt_err_code_init();
	chip->short_c_batt.is_switch_on = oplus_short_c_batt_chg_switch_init();
	chip->short_c_batt.is_feature_sw_on
		= oplus_short_c_batt_feature_sw_status_init();
	chip->short_c_batt.is_feature_hw_on
		= oplus_short_c_batt_feature_hw_status_init();
	chip->short_c_batt.shortc_gpio_status = 1;
	chip->short_c_batt.disable_rechg = false;
	chip->short_c_batt.limit_chg = false;
	chip->short_c_batt.limit_rechg = false;
	chip->slave_charger_enable = false;
	chip->cool_down_force_5v = false;
	chip->chg_ctrl_by_cool_down = false;

	chip->ui_soc_decimal = 0;
	chip->ui_soc_integer = 0;
	chip->last_decimal_ui_soc = 0;
	chip->decimal_control = false;
	chip->calculate_decimal_time = 0;
	chip->boot_completed = false;
	chip->pd_chging = false;
	chip->pd_swarp = false;
	chip->usbtemp_check = false;
}

static void oplus_chg_fail_action(struct oplus_chg_chip *chip)
{
	chg_err("[BATTERY] BAD Battery status... Charging Stop !!\n");
	chip->charging_state = CHARGING_STATUS_FAIL;
	chip->chging_on = false;
	chip->batt_full = false;
	chip->tbatt_when_full = 200;
	chip->in_rechging = 0;
}

#define D_RECHGING_CNT	5
static void oplus_chg_check_rechg_status(struct oplus_chg_chip *chip)
{
	int recharging_vol;
	int nbat_vol = chip->batt_volt;
	static int rechging_cnt = 0;

	if (chip->tbatt_status == BATTERY_STATUS__COLD_TEMP) {				//4.0
		recharging_vol = oplus_chg_get_float_voltage(chip) - 300;
	} else if (chip->tbatt_status == BATTERY_STATUS__LITTLE_COLD_TEMP) {//4.4
		recharging_vol = oplus_chg_get_float_voltage(chip) - 200;
	} else {
		recharging_vol = oplus_chg_get_float_voltage(chip);//warm 4.1
		if (recharging_vol > chip->limits.temp_normal_vfloat_mv) {
			recharging_vol = chip->limits.temp_normal_vfloat_mv;
		}
		recharging_vol = recharging_vol - chip->limits.recharge_mv;
	}
	if ((!chip->authenticate) ||(!chip->hmac)) {
		recharging_vol = chip->limits.non_standard_vfloat_mv - 400;//3.80
	}
	if (nbat_vol <= recharging_vol) {
		rechging_cnt++;
	} else {
		rechging_cnt = 0;
	}

	/*don't rechg here unless prohibit rechg is false*/
	if (oplus_short_c_batt_is_disable_rechg(chip)) {
		if (rechging_cnt >= D_RECHGING_CNT) {
			charger_xlog_printk(CHG_LOG_CRTI,
				"[Battery] disable rechg! batt_volt = %d, nReChgingVol = %d\r\n",
				nbat_vol, recharging_vol);
			rechging_cnt = D_RECHGING_CNT;
		}
	}
	if (rechging_cnt > D_RECHGING_CNT) {
		charger_xlog_printk(CHG_LOG_CRTI,
			"[Battery] Battery rechg begin! batt_volt = %d, recharging_vol = %d\n",
			nbat_vol, recharging_vol);
		rechging_cnt = 0;
		chip->in_rechging = true;
		oplus_chg_voter_charging_start(chip, CHG_STOP_VOTER__FULL);/*now rechging!*/
	}
}

static void oplus_chg_full_action(struct oplus_chg_chip *chip)
{
	charger_xlog_printk(CHG_LOG_CRTI, "[BATTERY] Battery full !!\n");
	oplus_chg_voter_charging_stop(chip, CHG_STOP_VOTER__FULL);
	/*chip->charging_state = CHARGING_STATUS_FULL;*/
	if(chip->batt_full == false) {
		chip->tbatt_when_full = chip->temperature;
	}
	chip->batt_full = true;
	chip->total_time = 0;
	chip->in_rechging = false;
	chip->limits.vfloat_over_counts = 0;
	oplus_chg_check_rechg_status(chip);
}

#ifndef CONFIG_OPLUS_CHARGER_MTK
extern bool p922x_wpc_get_fw_updating(void);
#endif
void oplus_charger_detect_check(struct oplus_chg_chip *chip)
{
	static bool charger_resumed = true;
#ifdef OPLUS_CHG_OP_DEF
	static int pre_charger_type = POWER_SUPPLY_TYPE_UNKNOWN;
	static bool ac_offline = true;
#endif
#ifdef CONFIG_OPLUS_CHARGER_MTK
	static int charger_flag = 0;
#endif
#ifdef OPLUS_CHG_OP_DEF
	if ((oplus_chg_get_charger_voltage() > CHG_VUSBIN_VOL_THR) && (chip->chg_ops->check_chrdet_status())) {
		ac_offline = false;
#else
	if (chip->chg_ops->check_chrdet_status()) {
#endif
		oplus_chg_set_awake(chip, true);
#ifndef CONFIG_OPLUS_CHARGER_MTK
#ifndef OPLUS_CHG_OP_DEF
		if (chip->wireless_support && oplus_wpc_get_fw_updating() == true) {
			return;
		}
#endif // TODO: nick.hu
#endif /* CONFIG_OPLUS_CHARGER_MTK */
#ifdef OPLUS_CHG_OP_DEF
		if (chip->charger_type == POWER_SUPPLY_TYPE_UNKNOWN || chip->chg_config_init || chip->chg_redetect_charger_type) {
			chip->chg_config_init = false;
			chip->pd_chging = false;
			chip->chg_redetect_charger_type = false;
#else
		if (chip->charger_type == POWER_SUPPLY_TYPE_UNKNOWN) {
#endif
			noplug_temperature = chip->temperature;
			noplug_batt_volt_max = chip->batt_volt_max;
			noplug_batt_volt_min = chip->batt_volt_min;
			oplus_chg_variables_reset(chip, true);
#ifdef CONFIG_OPLUS_CHARGER_MTK
			if(is_meta_mode() == true){
				chip->charger_type = POWER_SUPPLY_TYPE_USB;
				chip->real_charger_type = POWER_SUPPLY_TYPE_USB;
			} else {
				chip->charger_type = chip->chg_ops->get_charger_type();
				if(chip->chg_ops->get_real_charger_type) {
					chip->real_charger_type = chip->chg_ops->get_real_charger_type();
				}
			}
			if((chip->chg_ops->usb_connect)
					&& (chip->charger_type == POWER_SUPPLY_TYPE_USB
					|| chip->charger_type == POWER_SUPPLY_TYPE_USB_CDP)) {
				chip->chg_ops->usb_connect();
				charger_flag = 1;
			}
#else
			chip->charger_type = chip->chg_ops->get_charger_type();
			if(chip->chg_ops->get_real_charger_type) {
				chip->real_charger_type = chip->chg_ops->get_real_charger_type();
			}
#endif
			charger_xlog_printk(CHG_LOG_CRTI, "Charger in 1 charger_type=%d\n",
				chip->charger_type);
			if (oplus_warp_get_fastchg_to_normal() == true
					|| oplus_warp_get_fastchg_to_warm() == true) {
				charger_xlog_printk(CHG_LOG_CRTI,
					"fast_to_normal or to_warm 1,don't turn on charge here\n");

				if (oplus_warp_get_reset_adapter_st()) {
					oplus_chg_unsuspend_charger();
				}
			}
#ifndef CONFIG_OPLUS_CHARGER_MTK
#ifndef OPLUS_CHG_OP_DEF
			else if (oplus_wireless_charge_start() == false) {
#else /* OPLUS_CHG_OP_DEF */
			else if (!oplus_chg_is_wls_online(chip)) {
#endif
				charger_xlog_printk(CHG_LOG_CRTI, "oplus_wireless_charge_start == false\n");
				charger_resumed = chip->chg_ops->check_charger_resume();
				oplus_chg_turn_on_charging(chip);
			}
#else /* CONFIG_OPLUS_CHARGER_MTK */
			else {
				chg_err("enable charging\n");
				charger_resumed = chip->chg_ops->check_charger_resume();
				oplus_chg_turn_on_charging(chip);
			}
#endif
#ifdef OPLUS_CHG_OP_DEF
			if (is_usb_ocm_available(chip)) {
				oplus_chg_mod_event(chip->usb_ocm, chip->usb_ocm,
					OPLUS_CHG_EVENT_ONLINE);
			}
#endif
			/*chg_err("Charger in, charger_type=%d\n", chip->charger_type);*/
		} else {
			if (oplus_warp_get_fastchg_to_normal() == true
					|| oplus_warp_get_fastchg_to_warm() == true) {
				if (oplus_warp_get_reset_adapter_st()) {
					oplus_chg_unsuspend_charger();
				}
				charger_xlog_printk(CHG_LOG_CRTI,
					"fast_to_normal or to_warm 2,don't turn on charge here\n");
				if (oplus_warp_get_reset_adapter_st()) {
					oplus_chg_unsuspend_charger();
				}
			} else if (oplus_warp_get_fastchg_started() == false
					&& charger_resumed == false) {
				charger_resumed = chip->chg_ops->check_charger_resume();
				oplus_chg_turn_on_charging(chip);
			}
#ifdef OPLUS_CHG_OP_DEF
			if (pre_charger_type != chip->charger_type) {
				pre_charger_type = chip->charger_type;
				oplus_chg_set_input_current_limit(chip);
			}
#endif
		}
	} else {
#ifdef OPLUS_CHG_OP_DEF
		pre_charger_type = POWER_SUPPLY_TYPE_UNKNOWN;
#endif
		oplus_chg_variables_reset(chip, false);
		if (!chip->mmi_fastchg) {
			oplus_mmi_fastchg_in(chip);
		}
		oplus_gauge_set_batt_full(false);
#ifdef CONFIG_OPLUS_CHARGER_MTK
		if (chip->chg_ops->usb_disconnect && charger_flag == 1) {
			chip->chg_ops->usb_disconnect();
			charger_flag = 0;
		}
#endif
#ifdef OPLUS_CHG_OP_DEF
		if (is_usb_ocm_available(chip) &&
		    !oplus_warp_get_fastchg_started() && !ac_offline) {
			ac_offline = true;
			oplus_chg_mod_event(chip->usb_ocm, chip->usb_ocm,
					    OPLUS_CHG_EVENT_OFFLINE);
			chip->charging_suspend = false;
			oplus_chg_variables_reset(chip, false);
		}
#endif
		if (chip->chg_ops->get_charging_enable() == true) {
			oplus_chg_turn_off_charging(chip);
		}
		oplus_chg_set_awake(chip, false);
	}
}
static void oplus_get_smooth_soc_switch(struct oplus_chg_chip *chip)
{
/*
	char *substr;
	static int save_num = -1;

	if (save_num == -1) {
		substr = strstr(saved_command_line, "smooth_soc_switch=");
		if(NULL == substr) {
			save_num = 0;
		} else {
			substr += strlen("smooth_soc_switch=");
			if (strncmp(substr, "1", 1) == 0) {
				save_num = 1;
			} else
				save_num = 0;
		}
	}

	chip->smooth_switch = save_num;
	if(chip->vbatt_num == 1)
		chip->smooth_switch = 1;
	else
		chip->smooth_switch = 0;*/
	chg_debug("smooth_switch = %d\n", chip->smooth_switch);
}

#define RETRY_COUNTS	24
static void oplus_chg_get_battery_data(struct oplus_chg_chip *chip)
{
	static int ui_soc_cp_flag = 0;
	static int soc_load = 0;
	int remain_100_thresh = 97;
	static int retry_counts = 0;

	if (oplus_warp_get_fastchg_started() == true) {
		chip->batt_volt = oplus_gauge_get_prev_batt_mvolts();
		chip->batt_volt_max = oplus_gauge_get_prev_batt_mvolts_2cell_max();
		chip->batt_volt_min = oplus_gauge_get_prev_batt_mvolts_2cell_min();
		chip->icharging = oplus_gauge_get_prev_batt_current();
		chip->temperature = oplus_chg_match_temp_for_chging();
		chip->soc = oplus_gauge_get_prev_batt_soc();
		chip->batt_rm = oplus_gauge_get_prev_remaining_capacity() * chip->vbatt_num;
	} else {
		chip->batt_volt = oplus_gauge_get_batt_mvolts();
		chip->batt_volt_max = oplus_gauge_get_batt_mvolts_2cell_max();
		chip->batt_volt_min = oplus_gauge_get_batt_mvolts_2cell_min();
		chip->icharging = oplus_gauge_get_batt_current();
		chip->temperature = oplus_chg_match_temp_for_chging();
		chip->soc = oplus_gauge_get_batt_soc();
		chip->batt_fcc = oplus_gauge_get_batt_fcc() * chip->vbatt_num;
		chip->batt_cc = oplus_gauge_get_batt_cc() * chip->vbatt_num;
		chip->batt_soh = oplus_gauge_get_batt_soh();
		chip->batt_rm = oplus_gauge_get_remaining_capacity() * chip->vbatt_num;
		oplus_midas_chg_processing(chip);
	}
	if (chgr_dbg_vchg != 0) {
		chip->charger_volt = chgr_dbg_vchg;
	} else {
		chip->charger_volt = chip->chg_ops->get_charger_volt();
		if (chip->charger_volt > 3000 && chip->charger_exist) {
			chip->usbtemp_check = true;
			wake_up_interruptible(&chip->oplus_usbtemp_wq);
		} else {
			chip->usbtemp_check = false;
		}
	}
	if ((chip->chg_ops->get_charger_current && oplus_warp_get_allow_reading() == true)
                && (chip->chg_ops->get_charging_enable && (chip->chg_ops->get_charging_enable() == true))) {
		chip->ibus = chip->chg_ops->get_charger_current();
	} else {
		chip->ibus = -1;
	}
	if(chip->smooth_switch ){
		oplus_chg_smooth_to_soc(chip);
	} else {
		chip->smooth_soc = chip->soc;
	}
	if(!chip->healthd_ready && chip->smooth_switch == 1 && retry_counts < RETRY_COUNTS) {
		chg_err(" test gauge soc[%d] \n", chip->soc);
		chip->soc = -1;
	}
	if (ui_soc_cp_flag == 0) {
		if ((chip->soc < 0 || chip->soc > 100 || (chip->chg_ops->get_rtc_soc() == -ENODEV)) && retry_counts < RETRY_COUNTS) {
			charger_xlog_printk(CHG_LOG_CRTI,
				"[Battery]oplus_chg_get_battery_data,\
				chip->soc[%d],retry_counts[%d]\n",
				chip->soc, retry_counts);
			retry_counts++;
			chip->soc = 50;
			goto next;
		}
		ui_soc_cp_flag = 1;
		if( chip->chg_ops->get_rtc_soc() > 100 || (chip->chg_ops->get_rtc_soc() <= 0)){
			soc_load = chip->soc;
		} else {
			soc_load = chip->chg_ops->get_rtc_soc();
		}
		chip->soc_load = soc_load;
		if(chip->smooth_switch)
			chip->smooth_soc = soc_load;
		if ((chip->soc < 0 || chip->soc > 100) && soc_load > 0 && soc_load <= 100) {
			chip->soc = soc_load;
		}
		if ((soc_load != 0) && ((abs(soc_load-chip->soc)) <= 20)) {
			if (chip->suspend_after_full && chip->external_gauge) {
				remain_100_thresh = 95;
			} else if (chip->suspend_after_full && !chip->external_gauge) {
				remain_100_thresh = 94;
			} else if (!chip->suspend_after_full && chip->external_gauge) {
				remain_100_thresh = 97;
			} else if (!chip->suspend_after_full && !chip->external_gauge) {
				remain_100_thresh = 95;
			} else {
				remain_100_thresh = 97;
			}
			if (chip->soc < soc_load &&	chip->smooth_switch == 1) {
				if (soc_load == 100 && chip->soc > remain_100_thresh) {
					chip->ui_soc = soc_load;
				} else {
					chip->ui_soc = soc_load - 1;
				}
			} else {
				chip->ui_soc = soc_load;
			}
		} else {
			chip->ui_soc = chip->soc;
			if (!chip->external_gauge && soc_load == 0 && chip->soc < 5) {
				chip->ui_soc = 0;
			}
		}
		chg_err("[soc ui_soc soc_load smooth_soc smooth_switch] = [%d %d %d %d %d]\n", chip->soc,
			chip->ui_soc, chip->soc_load, chip->smooth_soc, chip->smooth_switch);

	}
	next:
	return;
}

/*need to extend it*/
static void oplus_chg_set_aicl_point(struct oplus_chg_chip *chip)
{
	if (oplus_warp_get_allow_reading() == true) {
		chip->chg_ops->set_aicl_point(chip->batt_volt);
	}
}

#define AICL_DELAY_15MIN	180
static void oplus_chg_check_aicl_input_limit(struct oplus_chg_chip *chip)
{
	static int aicl_delay_count = 0;
#ifdef CONFIG_OPLUS_CHARGER_MTK
	if (chip->charging_state == CHARGING_STATUS_FAIL || chip->batt_full == true
			|| ((chip->tbatt_status != BATTERY_STATUS__NORMAL)
			&& (chip->tbatt_status != BATTERY_STATUS__LITTLE_COOL_TEMP))
			|| chip->ui_soc > 85 || oplus_warp_get_fastchg_started() == true) {
		aicl_delay_count = 0;
		return;
	}
	if (aicl_delay_count > AICL_DELAY_15MIN) {
		aicl_delay_count = 0;
		if (oplus_warp_get_allow_reading() == true) {
			if (chip->dual_charger_support) {
				chip->slave_charger_enable = false;
				oplus_chg_set_charging_current(chip);
			}
			oplus_chg_set_input_current_limit(chip);
		}
	} else {
		aicl_delay_count++;
	}
#else
	if (chip->charging_state == CHARGING_STATUS_FAIL || chip->batt_full == true
			|| ((chip->tbatt_status != BATTERY_STATUS__NORMAL)
			&& (chip->tbatt_status != BATTERY_STATUS__LITTLE_COOL_TEMP))
			|| ((chip->ui_soc > 85) && (chip->pmic_spmi.aicl_suspend == false))
			|| oplus_warp_get_fastchg_started() == true) {
		aicl_delay_count = 0;
		return;
	}
	if (aicl_delay_count > AICL_DELAY_15MIN) {
		aicl_delay_count = 0;
		if (oplus_warp_get_allow_reading() == true) {
			oplus_chg_set_input_current_limit(chip);
		}
	} else if (chip->pmic_spmi.aicl_suspend == true
			&& chip->charger_volt > 4450
			&& chip->charger_volt < 5800) {
		aicl_delay_count = 0;
		if (oplus_warp_get_allow_reading() == true) {
			chip->chg_ops->rerun_aicl();
			oplus_chg_set_input_current_limit(chip);
		}
		charger_xlog_printk(CHG_LOG_CRTI, "chip->charger_volt=%d\n", chip->charger_volt);
	} else {
		aicl_delay_count++;
	}
	if (chip->charger_type == POWER_SUPPLY_TYPE_USB
			|| chip->charger_type == POWER_SUPPLY_TYPE_USB_CDP) {
		chip->pmic_spmi.usb_hc_count++;
		if (chip->pmic_spmi.usb_hc_count >= 3) {
			chip->pmic_spmi.usb_hc_mode = true;
			chip->pmic_spmi.usb_hc_count = 3;
		}
	}
	if (oplus_warp_get_allow_reading() == true
			&& chip->pmic_spmi.usb_hc_mode && !chip->pmic_spmi.hc_mode_flag) {
		oplus_chg_set_input_current_limit(chip);
		chip->pmic_spmi.hc_mode_flag = true;
	}
#endif
}

static void oplus_chg_aicl_check(struct oplus_chg_chip *chip)
{
	if (oplus_warp_get_fastchg_started() == false) {
		oplus_chg_set_aicl_point(chip);
		oplus_chg_check_aicl_input_limit(chip);
	}
}

static void oplus_chg_check_chg_strategy_status(struct oplus_chg_chip *chip)
{
	int skin_temp = 250;
	int curr_ma = 0;
	int rc;

	if (chip->led_on) {
#ifdef CONFIG_OPLUS_CHG_OOS
		if (chip->vbatt_num == 2 &&
		    oplus_warp_get_fast_chg_type() == CHARGER_SUBTYPE_FASTCHG_WARP &&
		    oplus_warp_get_fastchg_started() == true) {
			chip->strategy = &warp_chg_led_on_strategy;
		} else if (chip->chg_ops->oplus_chg_get_pd_type && chip->chg_ops->oplus_chg_get_pd_type()) {
			if (chip->charger_volt > 7500)
				chip->strategy = &pd9v_chg_led_on_strategy;
			else
				chip->strategy = &pd5v_chg_led_on_strategy;
		} else {
			chip->strategy = NULL;
		}
#else
		chip->strategy = NULL;
#endif
	} else {
		if (chip->vbatt_num == 2 &&
		    oplus_warp_get_fast_chg_type() == CHARGER_SUBTYPE_FASTCHG_WARP &&
		    oplus_warp_get_fastchg_started() == true) {
			chip->strategy = &normal_chg_led_off_strategy;
		} else if (chip->chg_ops->oplus_chg_get_pd_type && chip->chg_ops->oplus_chg_get_pd_type()) {
			if (chip->charger_volt > 7500)
				chip->strategy = &normal_chg_led_off_strategy;
			else
				chip->strategy = &normal_chg_led_off_strategy;
		} else if (chip->chg_ops->get_charger_subtype() == CHARGER_SUBTYPE_QC) {
			chip->strategy = &normal_chg_led_off_strategy;
		} else {
			chip->strategy = NULL;
		}
	}

	if (chip->strategy && chip->strategy->initialized) {
		rc = oplus_chg_get_skin_temp(chip, &skin_temp);
		if (rc < 0) {
			chg_err("can't get skin temp\n");
			skin_temp = 250;
		}
	}
	spin_lock(&chip->strategy_lock);
	if (chip->strategy && chip->strategy->initialized)
		curr_ma = oplus_chg_strategy_get_data(
			chip->strategy, &chip->strategy->temp_region,
			skin_temp);
	spin_unlock(&chip->strategy_lock);

	if (chip->chg_strategy_batt_curr_ma != curr_ma) {
		chip->chg_strategy_batt_curr_ma = curr_ma;
		chg_err("skin_temp=%d, led_on=%d, curr=%d\n", skin_temp, chip->led_on, curr_ma);
		oplus_chg_set_charging_current(chip);
	}
}

#define ALLOW_DIFF_VALUE 1000000
#ifdef OPLUS_CHG_OP_DEF
void oplus_check_ovp_status(struct oplus_chg_chip *chg)
{
	int volt_diff = 0;

	if (chg->support_abnormal_vol_check == false) {
		return;
	}
	if (!chg) {
		return;
	}
	if (oplus_chg_is_wls_present(chg))
		return;

	oplus_chg_get_vph_voltage();
	oplus_chg_get_hw_detect_status();
	if (chg->abnormal_volt_detected)
		chg->notify_code |= 1 << NOTIFY_OVP_VOLTAGE_ABNORMAL;
	if (chg->vph_voltage <= 0 || chg->charger_volt <= 0) {
		return;
	}
	if (chg->vph_voltage < chg->charger_volt) {
		return;
	}

	if (chg->hw_detected && ((chg->charger_volt <= VOLT_LOW_VBUS_VALUE) && (chg->charger_volt >= VOLT_HIGH_VBUS_VALUE))) {
		pr_info("chg->hw_detected:%d return\n", chg->hw_detected);
		return;
	}
	chg_err("oplus_check_ovp_status vph:%d,vbus:%d \n", chg->vph_voltage, chg->charger_volt);
	volt_diff = chg->vph_voltage - chg->charger_volt;
	if ((volt_diff > VOLT_LOW_DIFF_VALUE) && (volt_diff < VOLT_HIGH_DIFF_VALUE)
		&& ((chg->charger_volt >= VOLT_LOW_VBUS_VALUE) && (chg->charger_volt <= VOLT_HIGH_VBUS_VALUE))
		&& (!chg->abnormal_volt_detected)) {
		chg->abnormal_volt_detected = true;
		oplus_chg_variables_reset(chg, false);
		chg->notify_code |= 1 << NOTIFY_OVP_VOLTAGE_ABNORMAL;
		if (is_usb_ocm_available(chg))
			oplus_chg_mod_changed(chg->usb_ocm);
		if (is_batt_ocm_available(chg))
			oplus_chg_mod_changed(chg->batt_ocm);
		chg->chg_ops->report_vol_status();
		chg_err("abnormal_volt_detected %d \n", chg->abnormal_volt_detected);
	}
}

static void oplus_check_battery_vol_diff(struct oplus_chg_chip *chg)
{
	int rc = 0;
	int vbat_cell_max = 0;
	int vbat_cell_min = 0;
	union oplus_chg_mod_propval pval;

	//battery_status_pre = op_battery_status_get(chg);
	if (!is_batt_ocm_available(chg)) {
		pr_err("batt_ocm not found\n");
		return;
	}
	rc = oplus_chg_mod_get_property(chg->batt_ocm, OPLUS_CHG_PROP_VOLTAGE_NOW_CELL1, &pval);
	if (rc) {
		pr_err("Couldn't get vbat_max rc=%d\n", rc);
		vbat_cell_max = 3800;
	}
	vbat_cell_max = pval.intval;
	rc = oplus_chg_mod_get_property(chg->batt_ocm, OPLUS_CHG_PROP_VOLTAGE_NOW_CELL2, &pval);
	if (rc) {
		pr_err("Couldn't get vbat_min rc=%d\n", rc);
		vbat_cell_min = 3800;
	}
	vbat_cell_min = pval.intval;
	pr_info("bat vol:(%d,%d)\n", vbat_cell_max, vbat_cell_min);
	if (abs(vbat_cell_max - vbat_cell_min) > ALLOW_DIFF_VALUE) {
		chg->check_battery_vol_count++;
		if (chg->check_battery_vol_count > 5) {
			if (oplus_warp_get_fastchg_started() == true) {
				oplus_warp_turn_off_fastchg();
			}
			oplus_chg_voter_charging_stop(chg, CHG_STOP_VOTER_BAD_VOL_DIFF);
			//chg->chg_ops->charger_suspend();
			oplus_warp_set_fastchg_allow(false);
			chg->bat_volt_different = true;
			chg->notify_code |= 1 << NOTIFY_BAT_VOLTAGE_DIFF;
			chg->authenticate = false;
			chg->check_battery_vol_count = 0;
			pr_info("BATTERY_SOFT_DIFF_VOLTAGE disable chg\n");
		}
	} else {
		if (chg->bat_volt_different) {
			chg->check_battery_vol_count++;
			if (chg->check_battery_vol_count > 5) {
				chg->bat_volt_different = false;
				chg->authenticate = true;
				chg->check_battery_vol_count = 0;
				pr_info("Recovery BATTERY_SOFT_DIFF_VOLTAGE\n");
			}
		} else {
				chg->check_battery_vol_count = 0;
		}
	}
}

#endif
static void oplus_chg_protection_check(struct oplus_chg_chip *chip)
{
	if (false == oplus_chg_check_tbatt_is_good(chip)) {
		chg_err("oplus_chg_check_tbatt_is_good func ,false!\n");
		oplus_chg_voter_charging_stop(chip, CHG_STOP_VOTER__BATTTEMP_ABNORMAL);
	} else {
		if ((chip->stop_voter & CHG_STOP_VOTER__BATTTEMP_ABNORMAL)
				== CHG_STOP_VOTER__BATTTEMP_ABNORMAL) {
			charger_xlog_printk(CHG_LOG_CRTI,
				"oplus_chg_check_tbatt_is_good func ,true! To Normal\n");
			oplus_chg_voter_charging_start(chip, CHG_STOP_VOTER__BATTTEMP_ABNORMAL);
		}
	}
	if (false == oplus_chg_check_vchg_is_good(chip)) {
		chg_err("oplus_chg_check_vchg_is_good func ,false!\n");
		oplus_chg_voter_charging_stop(chip, CHG_STOP_VOTER__VCHG_ABNORMAL);
	} else {
		if ((chip->stop_voter & CHG_STOP_VOTER__VCHG_ABNORMAL)
				== CHG_STOP_VOTER__VCHG_ABNORMAL) {
			charger_xlog_printk(CHG_LOG_CRTI,
				"oplus_chg_check_vchg_is_good func ,true! To Normal\n");
			oplus_chg_voter_charging_start(chip, CHG_STOP_VOTER__VCHG_ABNORMAL);
		}
	}
#ifdef FEATURE_VBAT_PROTECT
	if (false == oplus_chg_check_vbatt_is_good(chip)) {
		chg_err("oplus_chg_check_vbatt_is_good func ,false!\n");
		oplus_chg_voter_charging_stop(chip, CHG_STOP_VOTER__VBAT_TOO_HIGH);
	}
#endif
	if (false == oplus_chg_check_time_is_good(chip)) {
		chg_err("oplus_chg_check_time_is_good func ,false!\n");
		oplus_chg_voter_charging_stop(chip, CHG_STOP_VOTER__MAX_CHGING_TIME);
	}
	oplus_chg_check_vbatt_higher_than_4180mv(chip);
	oplus_chg_vfloat_over_check(chip);
	oplus_chg_check_ffc_temp_status(chip);
	if (chip->chg_ctrl_by_lcd) {
		oplus_chg_check_tled_status(chip);
		oplus_chg_check_led_on_ichging(chip);
	}
	if (chip->chg_ctrl_by_camera) {
		oplus_chg_check_camera_on_ichging(chip);
	}
	if (chip->chg_ctrl_by_calling) {
		oplus_chg_check_calling_on_ichging(chip);
	}
	if (chip->chg_ctrl_by_warp) {
		oplus_chg_check_warp_temp_status(chip);
		oplus_chg_check_warp_ichging(chip);
	}
	if (chip->cool_down_done) {
		oplus_chg_check_cool_down_ichging(chip);
	}
#ifdef OPLUS_CHG_OP_DEF
	oplus_chg_check_chg_strategy_status(chip);
	if (chip->led_on_change) {
		oplus_chg_set_charging_current(chip);
		chip->led_on_change = false;
	}
#endif
}


static void battery_notify_tbat_check(struct oplus_chg_chip *chip)
{
	static int count_removed = 0;
	static int count_high = 0;
	if (BATTERY_STATUS__HIGH_TEMP == chip->tbatt_status) {
		count_high++;
		charger_xlog_printk(CHG_LOG_CRTI,
				"[BATTERY] bat_temp(%d), BATTERY_STATUS__HIGH_TEMP count[%d]\n",
				chip->temperature, count_high);
		if (chip->charger_exist && count_high > 10) {
			count_high = 11;
			chip->notify_code |= 1 << NOTIFY_BAT_OVER_TEMP;
			charger_xlog_printk(CHG_LOG_CRTI, "[BATTERY] bat_temp(%d) > 55'C\n",
				chip->temperature);
		}
	} else {
		count_high = 0;
	}
	if (BATTERY_STATUS__LOW_TEMP == chip->tbatt_status) {
		if (chip->charger_exist) {
			chip->notify_code |= 1 << NOTIFY_BAT_LOW_TEMP;
			charger_xlog_printk(CHG_LOG_CRTI, "[BATTERY] bat_temp(%d) < -10'C\n",
				chip->temperature);
		}
	}
	if (BATTERY_STATUS__REMOVED == chip->tbatt_status) {
		count_removed ++;
		charger_xlog_printk(CHG_LOG_CRTI,
				"[BATTERY] bat_temp(%d), BATTERY_STATUS__REMOVED count[%d]\n",
				chip->temperature, count_removed);
		if (count_removed > 10) {
			count_removed = 11;
			chip->notify_code |= 1 << NOTIFY_BAT_NOT_CONNECT;
			charger_xlog_printk(CHG_LOG_CRTI, "[BATTERY] bat_temp(%d) < -19'C\n",
				chip->temperature);
		}
	} else {
		count_removed = 0;
	}
}

static void battery_notify_authenticate_check(struct oplus_chg_chip *chip)
{
	if (!chip->authenticate) {
		chip->notify_code |= 1 << NOTIFY_BAT_NOT_CONNECT;
		charger_xlog_printk(CHG_LOG_CRTI, "[BATTERY] bat_authenticate is false!\n");
	}
}
static void battery_notify_hmac_check(struct oplus_chg_chip *chip)
{
	if (!chip->hmac) {
		chip->notify_code |= 1 << NOTIFY_BAT_FULL_THIRD_BATTERY;
		charger_xlog_printk(CHG_LOG_CRTI, "[BATTERY] bat_hmac is false!\n");
	}
}

static void battery_notify_vcharger_check(struct oplus_chg_chip *chip)
{
	if (CHARGER_STATUS__VOL_HIGH == chip->vchg_status) {
		chip->notify_code |= 1 << NOTIFY_CHARGER_OVER_VOL;
		charger_xlog_printk(CHG_LOG_CRTI,
			"[BATTERY] check_charger_off_vol(%d) > 5800mV\n", chip->charger_volt);
	}

	if (CHARGER_STATUS__VOL_LOW == chip->vchg_status) {
		chip->notify_code |= 1 << NOTIFY_CHARGER_LOW_VOL;
		charger_xlog_printk(CHG_LOG_CRTI,
			"[BATTERY] check_charger_off_vol(%d) < 3400mV\n", chip->charger_volt);
	}
}

static void battery_notify_vbat_check(struct oplus_chg_chip *chip)
{
	static int count = 0;

	if (true == chip->vbatt_over) {
		count++;
		charger_xlog_printk(CHG_LOG_CRTI,
			"[BATTERY] Battery is over VOL, count[%d] \n", count);
		if (count > 10) {
			count = 11;
			chip->notify_code |= 1 << NOTIFY_BAT_OVER_VOL;
			charger_xlog_printk(CHG_LOG_CRTI,
				"[BATTERY] Battery is over VOL! Notify \n");
		}
	} else {
		count = 0;
		if ((chip->batt_full) && (chip->charger_exist)) {
			if (chip->tbatt_status == BATTERY_STATUS__WARM_TEMP
					&& chip->ui_soc != 100) {
				chip->notify_code |=  1 << NOTIFY_BAT_FULL_PRE_HIGH_TEMP;
			} else if ((chip->tbatt_status == BATTERY_STATUS__COLD_TEMP)
					&& (chip->ui_soc != 100)) {
				chip->notify_code |=  1 << NOTIFY_BAT_FULL_PRE_LOW_TEMP;
			} else if (!chip->authenticate) {
				chip->notify_code |=  1 << NOTIFY_BAT_NOT_CONNECT;
			} else if (!chip->hmac) {
				chip->notify_code |=  1 << NOTIFY_BAT_FULL_THIRD_BATTERY;
			} else {
				if (chip->ui_soc == 100) {
					chip->notify_code |=  1 << NOTIFY_BAT_FULL;
				}
			}
			charger_xlog_printk(CHG_LOG_CRTI,
					"[BATTERY] FULL,tbatt_status:%d,notify_code:%d\n",
				chip->tbatt_status, chip->notify_code);
		}
	}
}

static void battery_notify_max_charging_time_check(struct oplus_chg_chip *chip)
{
	if (true == chip->chging_over_time) {
		chip->notify_code |= 1 << NOTIFY_CHGING_OVERTIME;
		charger_xlog_printk(CHG_LOG_CRTI,
			"[BATTERY] Charging is OverTime!Notify \n");
	}
}

static void battery_notify_short_c_battery_check(struct oplus_chg_chip *chip)
{
	if (chip->short_c_batt.err_code == SHORT_C_BATT_STATUS__CV_ERR_CODE1) {
		chip->notify_code |= 1 << NOTIFY_SHORT_C_BAT_CV_ERR_CODE1;
		charger_xlog_printk(CHG_LOG_CRTI,
			"[BATTERY] battery is short circuit! err_code1!\n");
	}

	if (chip->short_c_batt.err_code == SHORT_C_BATT_STATUS__FULL_ERR_CODE2) {
		chip->notify_code |= 1 << NOTIFY_SHORT_C_BAT_FULL_ERR_CODE2;
		charger_xlog_printk(CHG_LOG_CRTI,
			"[BATTERY] battery is short circuit! err_code2!\n");
		}

	if (chip->short_c_batt.err_code == SHORT_C_BATT_STATUS__FULL_ERR_CODE3) {
		chip->notify_code |= 1 << NOTIFY_SHORT_C_BAT_FULL_ERR_CODE3;
		charger_xlog_printk(CHG_LOG_CRTI,
			"[BATTERY] battery is short circuit! err_code3!\n");
	}

	if (chip->short_c_batt.err_code == SHORT_C_BATT_STATUS__DYNAMIC_ERR_CODE4) {
		chip->notify_code |= 1 << NOTIFY_SHORT_C_BAT_DYNAMIC_ERR_CODE4;
		charger_xlog_printk(CHG_LOG_CRTI,
			"[BATTERY] battery is short circuit! err_code4!\n");
	}

	if (chip->short_c_batt.err_code == SHORT_C_BATT_STATUS__DYNAMIC_ERR_CODE5) {
		chip->notify_code |= 1 << NOTIFY_SHORT_C_BAT_DYNAMIC_ERR_CODE5;
		charger_xlog_printk(CHG_LOG_CRTI,
			"[BATTERY] battery is short circuit! err_code5!\n");
	}
}

static void battery_notify_flag_check(struct oplus_chg_chip *chip)
{
	if (chip->notify_code & (1 << NOTIFY_CHGING_OVERTIME)) {
		chip->notify_flag = NOTIFY_CHGING_OVERTIME;
	} else if (chip->notify_code & (1 << NOTIFY_CHARGER_OVER_VOL)) {
		chip->notify_flag = NOTIFY_CHARGER_OVER_VOL;
	} else if (chip->notify_code & (1 << NOTIFY_CHARGER_LOW_VOL)) {
		chip->notify_flag = NOTIFY_CHARGER_LOW_VOL;
	} else if (chip->notify_code & (1 << NOTIFY_BAT_OVER_TEMP)) {
		chip->notify_flag = NOTIFY_BAT_OVER_TEMP;
	} else if (chip->notify_code & (1 << NOTIFY_BAT_LOW_TEMP)) {
		chip->notify_flag = NOTIFY_BAT_LOW_TEMP;
	} else if (chip->notify_code & (1 << NOTIFY_BAT_NOT_CONNECT)) {
		chip->notify_flag = NOTIFY_BAT_NOT_CONNECT;
	} else if (chip->notify_code & (1 << NOTIFY_BAT_FULL_THIRD_BATTERY)) {
		chip->notify_flag = NOTIFY_BAT_FULL_THIRD_BATTERY;
	} else if (chip->notify_code & (1 << NOTIFY_BAT_OVER_VOL)) {
		chip->notify_flag = NOTIFY_BAT_OVER_VOL;
	} else if (chip->notify_code & (1 << NOTIFY_BAT_FULL_PRE_HIGH_TEMP)) {
		chip->notify_flag = NOTIFY_BAT_FULL_PRE_HIGH_TEMP;
	} else if (chip->notify_code & (1 << NOTIFY_BAT_FULL_PRE_LOW_TEMP)) {
		chip->notify_flag = NOTIFY_BAT_FULL_PRE_LOW_TEMP;
	} else if (chip->notify_code & (1 << NOTIFY_BAT_FULL)) {
		chip->notify_flag = NOTIFY_BAT_FULL;
#ifdef OPLUS_CHG_OP_DEF
	} else if (chip->notify_code & (1 << NOTIFY_BAT_VOLTAGE_DIFF)) {
		chip->notify_flag = NOTIFY_BAT_VOLTAGE_DIFF;
#endif
	} else {
		chip->notify_flag = 0;
	}
}

static void battery_notify_charge_terminal_check(struct oplus_chg_chip *chip)
{
	if (chip->batt_full == true && chip->charger_exist == true){
		chip->notify_code |= 1 << NOTIFY_CHARGER_TERMINAL;
	}
}

static void battery_notify_gauge_i2c_err_check(struct oplus_chg_chip *chip)
{
	if (oplus_gauge_get_i2c_err() > 0) {
		chip->notify_code |= 1 << NOTIFY_GAUGE_I2C_ERR;
	}

	oplus_gauge_clear_i2c_err();
}

static void oplus_chg_battery_notify_check(struct oplus_chg_chip *chip)
{
	chip->notify_code = 0x0000;
	battery_notify_tbat_check(chip);
	battery_notify_authenticate_check(chip);
	battery_notify_hmac_check(chip);
	battery_notify_vcharger_check(chip);
	battery_notify_vbat_check(chip);
	battery_notify_max_charging_time_check(chip);
	battery_notify_short_c_battery_check(chip);
	battery_notify_charge_terminal_check(chip);
	battery_notify_gauge_i2c_err_check(chip);
	battery_notify_flag_check(chip);
}

int oplus_chg_get_prop_batt_health(struct oplus_chg_chip *chip)
{
	int bat_health = POWER_SUPPLY_HEALTH_GOOD;
	bool vbatt_over = chip->vbatt_over;
	OPLUS_CHG_TBATT_STATUS tbatt_status = chip->tbatt_status;

	if (vbatt_over == true) {
		bat_health = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
	} else if (tbatt_status == BATTERY_STATUS__REMOVED) {
		bat_health = POWER_SUPPLY_HEALTH_DEAD;
	} else if (tbatt_status == BATTERY_STATUS__HIGH_TEMP) {
		bat_health = POWER_SUPPLY_HEALTH_OVERHEAT;
	} else if (tbatt_status == BATTERY_STATUS__LOW_TEMP) {
		bat_health = POWER_SUPPLY_HEALTH_COLD;
	} else {
		bat_health = POWER_SUPPLY_HEALTH_GOOD;
	}
	return bat_health;
}

static bool oplus_chg_soc_reduce_slow_when_1(struct oplus_chg_chip *chip)
{
	static int reduce_count = 0;
	int reduce_count_limit = 0;
	int chgr_vbatt_soc_1 = chip->vbatt_soc_1;

	if (chip->batt_exist == false) {
		return false;
	}
	if (chip->charger_exist) {
		reduce_count_limit = 12;
		chgr_vbatt_soc_1 = 3200;/*power off vbat set 3200mv when charging*/
	} else {
		reduce_count_limit = 4;
	}
	if (chip->batt_volt_min < chgr_vbatt_soc_1) {
		reduce_count++;
	} else {
		reduce_count = 0;
	}
	charger_xlog_printk(CHG_LOG_CRTI,
			"batt_vol:%d, batt_volt_min:%d, reduce_count:%d, chgr_vbatt_soc_1[%d]\n",
			chip->batt_volt, chip->batt_volt_min, reduce_count, chgr_vbatt_soc_1);
	if (reduce_count > reduce_count_limit) {
		reduce_count = reduce_count_limit + 1;
		return true;
	} else {
		return false;
	}
}

#define SOC_SYNC_UP_RATE_10S			2
#define SOC_SYNC_UP_RATE_60S			12
#define SOC_SYNC_DOWN_RATE_300S			60
#define SOC_SYNC_DOWN_RATE_150S			30
#define SOC_SYNC_DOWN_RATE_90S			18
#define SOC_SYNC_DOWN_RATE_60S			12
#define SOC_SYNC_DOWN_RATE_45S			9
#define SOC_SYNC_DOWN_RATE_40S			8
#define SOC_SYNC_DOWN_RATE_30S			6
#define SOC_SYNC_DOWN_RATE_15S			3
#define TEN_MINUTES				600
#define CHARGING_STATUS  1
#define DISCHARGING_STATUS  0

static void oplus_chg_smooth_to_soc(struct oplus_chg_chip *chip)
{
	static int time = 0;
	static int capacity = -1;
	static int smooth_diff =-1;
	static int soc_pre = -1;
	static int status = DISCHARGING_STATUS;
	pr_err("[oplus_chg_smooth_to_soc] enter the func");
	if(chip->charger_exist && chip->batt_exist
			&& (CHARGING_STATUS_FAIL != chip->charging_state)
			&& chip->mmi_chg && (chip->stop_chg == 1 || chip->charger_type == 5) && status == DISCHARGING_STATUS){
		status = CHARGING_STATUS;
		time = 0;
		capacity = chip->batt_rm;
		soc_pre = chip->soc;
		smooth_diff = capacity/(2 * chip->soc - chip->smooth_soc);
	} else if(!(chip->charger_exist && chip->batt_exist
			&& chip->mmi_chg && (chip->batt_full || CHARGING_STATUS_FAIL != chip->charging_state)
			&& (chip->stop_chg == 1 || chip->charger_type == 5)) && status == CHARGING_STATUS){
		status = DISCHARGING_STATUS;
		time = 0;
		capacity = -1;
		soc_pre = -1;
		smooth_diff = -1;
	}

	if(status == DISCHARGING_STATUS){
		if(chip->soc >= 96 && chip->soc <= 100 && chip->ui_soc == 100){
				chip->smooth_soc = 100;
			} else{
				if(chip->smooth_soc > chip->soc){
					if(capacity == -1){
						time = 0;
						capacity = chip->batt_rm;
						soc_pre = chip->soc;
						smooth_diff = capacity/chip->smooth_soc;
					}
					if((capacity - chip->batt_rm) >= smooth_diff || ((chip->smooth_soc - chip->soc) > chip->smooth_to_soc_gap)){
							chip->smooth_soc--;
							capacity = chip->batt_rm;
							smooth_diff = capacity/chip->smooth_soc;
					}
					if(chip->soc == 0 ){
						time++;
						if(time >= SOC_SYNC_DOWN_RATE_15S){
							chip->smooth_soc--;
							time = 0;
						}
					}
					pr_err("[oplus_chg_smooth_to_soc] smooth_soc[%d],capacity[%d],smooth_diff[%d]",chip->smooth_soc,capacity,smooth_diff);
				} else {
					chip->smooth_soc =chip->soc;
				}
			}
	} else{
		if(chip->smooth_soc > chip->soc){
			if(soc_pre < chip->soc &&(chip->batt_rm - capacity) >= smooth_diff && chip->smooth_soc < 100 ){
					chip->smooth_soc++;
					capacity = chip->batt_rm;
					soc_pre = chip->soc;
					smooth_diff = capacity/(2 * chip->soc - chip->smooth_soc);
			}
			if(chip->soc < soc_pre){
				chip->smooth_soc--;
				soc_pre = chip->soc;
			}
			pr_err("[oplus_chg_smooth_to_soc]charging smooth_soc[%d],capacity[%d],smooth_diff[%d],soc_pre[%d]",chip->smooth_soc,capacity,smooth_diff,soc_pre);
		} else {
			chip->smooth_soc = chip->soc;
		}
	}
}
static void oplus_chg_update_ui_soc(struct oplus_chg_chip *chip)
{
	static int soc_down_count = 0;
	static int soc_up_count = 0;
	static int ui_soc_pre = 50;
	static int cnt = 0;
	int soc_down_limit = 0;
	int soc_up_limit = 0;
	unsigned long sleep_tm = 0;
	unsigned long soc_reduce_margin = 0;
	bool vbatt_too_low = false;
	vbatt_lowerthan_3300mv = false;

	if (chip->ui_soc == 100) {
		soc_down_limit = SOC_SYNC_DOWN_RATE_300S;
	} else if (chip->ui_soc >= 95) {
		soc_down_limit = SOC_SYNC_DOWN_RATE_150S;
	} else if (chip->ui_soc >= 60) {
		soc_down_limit = SOC_SYNC_DOWN_RATE_60S;
	} else if (chip->charger_exist && chip->ui_soc == 1) {
		soc_down_limit = SOC_SYNC_DOWN_RATE_90S;
	} else {
		soc_down_limit = SOC_SYNC_DOWN_RATE_40S;
	}
	if (chip->batt_exist &&
			(chip->batt_volt_min < chip->vbatt_power_off)
			&& (chip->batt_volt_min > 2500)) {
		soc_down_limit = SOC_SYNC_DOWN_RATE_15S;
		vbatt_too_low = true;
		vbatt_lowerthan_3300mv = true;
		charger_xlog_printk(CHG_LOG_CRTI,
			"batt_volt:%d, batt_volt_min:%d, vbatt_too_low:%d\n",
			chip->batt_volt, chip->batt_volt_min, vbatt_too_low);
	}
	if (chip->batt_full) {
		soc_up_limit = SOC_SYNC_UP_RATE_10S;
	} else {
		soc_up_limit = SOC_SYNC_UP_RATE_10S;
	}

	if (get_eng_version() == HIGH_TEMP_AGING
			|| get_eng_version() == AGING) {
		chg_err(" HIGH_TEMP_AGING/AGING,force ui_soc = smooth_soc \n");
		chip->ui_soc = chip->smooth_soc;
		soc_up_count = -1;
		soc_down_count = -1;
	}

#ifndef CONFIG_OPLUS_CHARGER_MTK
#ifndef OPLUS_CHG_OP_DEF
#ifndef WPC_NEW_INTERFACE
	if ((chip->charger_exist || oplus_wireless_charge_start())
			&& chip->batt_exist && chip->batt_full && chip->mmi_chg && (chip->stop_chg == 1 || chip->charger_type == 5)) {
#else
	if ((chip->charger_exist || oplus_wpc_get_status())
			&& chip->batt_exist && chip->batt_full && chip->mmi_chg && (chip->stop_chg == 1 || chip->charger_type == 5)) {
#endif
#else /* OPLUS_CHG_OP_DEF */
	if ((chip->charger_exist || oplus_chg_is_wls_present(chip))
			&& chip->batt_exist && chip->batt_full && chip->mmi_chg && (chip->stop_chg == 1 || chip->charger_type == 5)) {
#endif /* OPLUS_CHG_OP_DEF */
#else
	if (chip->charger_exist && chip->batt_exist && chip->batt_full && chip->mmi_chg && (chip->stop_chg == 1 || chip->charger_type == 5)) {
#endif
		chip->sleep_tm_sec = 0;
		if (oplus_short_c_batt_is_prohibit_chg(chip)) {
#ifdef CONFIG_OPLUS_CHG_OOS
			chip->prop_status = POWER_SUPPLY_STATUS_DISCHARGING;
#else
			chip->prop_status = POWER_SUPPLY_STATUS_NOT_CHARGING;
#endif
		} else if ((chip->hmac) &&((chip->tbatt_status == BATTERY_STATUS__NORMAL)
				|| (chip->tbatt_status == BATTERY_STATUS__LITTLE_COOL_TEMP)
				|| (chip->tbatt_status == BATTERY_STATUS__COOL_TEMP)
				|| (chip->tbatt_status == BATTERY_STATUS__LITTLE_COLD_TEMP))) {
			soc_down_count = 0;
			soc_up_count++;
			if (soc_up_count >= soc_up_limit) {
				soc_up_count = 0;
				chip->ui_soc++;
			}
			if (chip->ui_soc >= 100) {
				chip->ui_soc = 100;
				chip->prop_status = POWER_SUPPLY_STATUS_FULL;
			} else {
				chip->prop_status = POWER_SUPPLY_STATUS_CHARGING;
			}
		} else {
			chip->prop_status = POWER_SUPPLY_STATUS_FULL;
		}
		if (chip->ui_soc != ui_soc_pre) {
#ifdef OPLUS_CHG_OP_DEF
			if (is_batt_ocm_available(chip))
				oplus_chg_mod_changed(chip->batt_ocm);
#endif
			chg_debug("full [soc ui_soc smooth_soc up_limit] = [%d %d %d %d]\n",
				chip->soc, chip->ui_soc, chip->smooth_soc, soc_up_limit);

		}
#ifndef CONFIG_OPLUS_CHARGER_MTK
#ifndef OPLUS_CHG_OP_DEF
#ifndef WPC_NEW_INTERFACE
	} else if ((chip->charger_exist || oplus_wireless_charge_start()) && chip->batt_exist && (CHARGING_STATUS_FAIL != chip->charging_state)
				&& chip->mmi_chg && (chip->stop_chg == 1 || chip->charger_type == 5)) {
#else
	} else if ((chip->charger_exist || oplus_wpc_get_status()) && chip->batt_exist && (CHARGING_STATUS_FAIL != chip->charging_state)
					&& chip->mmi_chg && (chip->stop_chg == 1 || chip->charger_type == 5)) {
#endif
#else /* OPLUS_CHG_OP_DEF */
	} else if ((chip->charger_exist || oplus_chg_is_wls_present(chip)) && chip->batt_exist && (CHARGING_STATUS_FAIL != chip->charging_state)
				&& chip->mmi_chg && (chip->stop_chg == 1 || chip->charger_type == 5)) {
#endif /* OPLUS_CHG_OP_DEF */
#else
	} else if (chip->charger_exist && chip->batt_exist && (CHARGING_STATUS_FAIL != chip->charging_state)
					&& chip->mmi_chg && (chip->stop_chg == 1 || chip->charger_type == 5)) {
#endif
		chip->sleep_tm_sec = 0;
		chip->prop_status = POWER_SUPPLY_STATUS_CHARGING;
		if (chip->smooth_soc == chip->ui_soc) {
			soc_down_count = 0;
			soc_up_count = 0;
		} else if (chip->smooth_soc > chip->ui_soc) {
			soc_down_count = 0;
			soc_up_count++;
			if (soc_up_count >= soc_up_limit) {
				soc_up_count = 0;
				chip->ui_soc++;
			}
		} else if (chip->smooth_soc < chip->ui_soc) {
			soc_up_count = 0;
			soc_down_count++;
			if (soc_down_count >= soc_down_limit) {
				soc_down_count = 0;
				if(oplus_chg_show_warp_logo_ornot() == false
					|| (chip->warp_show_ui_soc_decimal == false || !chip->decimal_control)) {
					chip->ui_soc--;
				}
			}
		}
		if (chip->ui_soc != ui_soc_pre) {
#ifdef OPLUS_CHG_OP_DEF
			if (is_batt_ocm_available(chip))
				oplus_chg_mod_changed(chip->batt_ocm);
#endif
			chg_debug("full [soc ui_soc smooth_soc down_limit up_limit] = [%d %d %d %d %d]\n", chip->soc, chip->ui_soc, chip->smooth_soc, soc_down_limit, soc_up_limit);

		}
		charger_xlog_printk(CHG_LOG_CRTI, "ui_soc:%d,waiting_for_ffc:%d,fastchg_to_ffc:%d,fastchg_start:%d,chg_type=0x%x\n",
		chip->ui_soc, chip->waiting_for_ffc == false, chip->fastchg_to_ffc == false,
		oplus_warp_get_fastchg_started(), oplus_warp_get_fast_chg_type());
		if (chip->ui_soc == 100
			&& chip->fastchg_to_ffc == false
			&& (oplus_warp_get_fastchg_started() == false
			|| oplus_warp_get_fast_chg_type() == CHARGER_SUBTYPE_FASTCHG_WARP)) {
			if (++cnt >= 12) {
				chip->batt_full = true;

				chip->in_rechging = false;
				chip->limits.vfloat_over_counts = 0;
				oplus_chg_check_rechg_status(chip);
			}
		} else {
			cnt = 0;
		}
	} else {
		cnt = 0;
#ifdef CONFIG_OPLUS_CHG_OOS
		chip->prop_status = POWER_SUPPLY_STATUS_DISCHARGING;
#else
		chip->prop_status = POWER_SUPPLY_STATUS_NOT_CHARGING;
#endif
		soc_up_count = 0;
		if (chip->smooth_soc <= chip->ui_soc || vbatt_too_low) {
			if (soc_down_count > soc_down_limit) {
				soc_down_count = soc_down_limit + 1;
			} else {
				soc_down_count++;
			}
			sleep_tm = chip->sleep_tm_sec;
			if (chip->sleep_tm_sec > 0) {
				soc_reduce_margin = chip->sleep_tm_sec / TEN_MINUTES;
				if (soc_reduce_margin == 0) {
					if ((chip->ui_soc - chip->smooth_soc) > 2) {
						chip->ui_soc--;
						soc_down_count = 0;
						chip->sleep_tm_sec = 0;
					}
				} else if (soc_reduce_margin < (chip->ui_soc - chip->smooth_soc)) {
					chip->ui_soc -= soc_reduce_margin;
					soc_down_count = 0;
					chip->sleep_tm_sec = 0;
				} else if (soc_reduce_margin >= (chip->ui_soc - chip->smooth_soc)) {
					chip->ui_soc = chip->smooth_soc;
					soc_down_count = 0;
					chip->sleep_tm_sec = 0;
				}
			}
			if (soc_down_count >= soc_down_limit && (chip->smooth_soc < chip->ui_soc || vbatt_too_low)) {
				chip->sleep_tm_sec = 0;
				soc_down_count = 0;
				chip->ui_soc--;
			}
		}
	}
	if (chip->ui_soc < 2) {
		cnt = 0;
		if (oplus_chg_soc_reduce_slow_when_1(chip) == true) {
			chip->ui_soc = 0;
		} else {
			chip->ui_soc = 1;
		}
	}
#ifdef OPLUS_CHG_OP_DEF
	if (chip->bat_volt_different && !chip->charger_exist) {
		chip->ui_soc = 0;
	}
#endif
	if (chip->ui_soc != ui_soc_pre) {
#ifdef OPLUS_CHG_OP_DEF
		if (is_batt_ocm_available(chip))
			oplus_chg_mod_changed(chip->batt_ocm);
#endif
		ui_soc_pre = chip->ui_soc;
		chip->chg_ops->set_rtc_soc(chip->ui_soc);
		if (chip->chg_ops->get_rtc_soc() != chip->ui_soc) {
			/*charger_xlog_printk(CHG_LOG_CRTI, "set soc fail:[%d, %d], try again...\n", chip->ui_soc, chip->chg_ops->get_rtc_soc());*/
			chip->chg_ops->set_rtc_soc(chip->ui_soc);
		}
	}
	if(chip->decimal_control) {
		soc_down_count = 0;
		soc_up_count = 0;
	}
}

#ifndef CONFIG_OPLUS_CHG_GKI_SUPPORT
static void fg_update(struct oplus_chg_chip *chip)
{
	static int ui_soc_pre_fg = 50;
	static struct power_supply *bms_psy = NULL;
	if (!bms_psy) {
		bms_psy = power_supply_get_by_name("bms");
		charger_xlog_printk(CHG_LOG_CRTI, "bms_psy null\n");
	}
	if (bms_psy) {
		if (chip->ui_soc != ui_soc_pre_fg) {
			power_supply_changed(bms_psy);
			charger_xlog_printk(CHG_LOG_CRTI,
				"ui_soc:%d, soc:%d, ui_soc_pre:%d \n",
				chip->ui_soc, chip->soc, ui_soc_pre_fg);
		}
		if (chip->ui_soc != ui_soc_pre_fg) {
			ui_soc_pre_fg = chip->ui_soc;
		}
	}
}
#else /* CONFIG_OPLUS_CHG_GKI_SUPPORT */
static void fg_update(struct oplus_chg_chip *chip)
{
	static int ui_soc_pre_fg = 50;

	if (is_batt_ocm_available(chip)) {
		if (chip->ui_soc != ui_soc_pre_fg) {
			oplus_chg_mod_changed(chip->batt_ocm);
			charger_xlog_printk(CHG_LOG_CRTI,
				"ui_soc:%d, soc:%d, ui_soc_pre:%d \n",
				chip->ui_soc, chip->soc, ui_soc_pre_fg);
		}
		if (chip->ui_soc != ui_soc_pre_fg) {
			ui_soc_pre_fg = chip->ui_soc;
		}
	}
}
#endif /* CONFIG_OPLUS_CHG_GKI_SUPPORT */

static void battery_update(struct oplus_chg_chip *chip)
{
#ifdef OPLUS_CHG_OP_DEF
	static bool pre_charger_exist = false;
#endif
	oplus_chg_update_ui_soc(chip);
	if (chip->fg_bcl_poll) {
		fg_update(chip);
	}

#ifndef CONFIG_OPLUS_CHG_GKI_SUPPORT
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
	power_supply_changed(chip->batt_psy);
#else
	power_supply_changed(&chip->batt_psy);
#endif

#ifdef OPLUS_CHG_OP_DEF
	if (pre_charger_exist != chip->charger_exist) {
		if (chip->batt_psy)
			power_supply_changed(chip->batt_psy);
		pre_charger_exist = chip->charger_exist;
		if (!chip->charger_exist) {
			if (is_usb_ocm_available(chip))
				oplus_chg_mod_event(chip->usb_ocm, chip->usb_ocm,
					    OPLUS_CHG_EVENT_OFFLINE);
		}
	}
#endif

#else /* CONFIG_OPLUS_CHG_GKI_SUPPORT */
#ifdef OPLUS_CHG_OP_DEF
	if (pre_charger_exist != chip->charger_exist || (chip->temperature > 450)) {
#else
	if (pre_charger_exist != chip->charger_exist || !chip->fg_bcl_poll) {
#endif
		if (is_batt_ocm_available(chip))
			oplus_chg_mod_changed(chip->batt_ocm);
		pre_charger_exist = chip->charger_exist;
	}
#endif /* CONFIG_OPLUS_CHG_GKI_SUPPORT */
}

static void oplus_chg_battery_update_status(struct oplus_chg_chip *chip)
{
#ifdef CONFIG_OPLUS_CHARGER_MTK
	usb_update(chip);
#endif
	battery_update(chip);
}

#define RESET_MCU_DELAY_30S		6

static void oplus_chg_get_chargerid_voltage(struct oplus_chg_chip *chip)
{
	if (chip->chg_ops->set_chargerid_switch_val == NULL
			|| chip->chg_ops->get_chargerid_switch_val == NULL
			|| chip->chg_ops->get_chargerid_volt == NULL) {
		return;
	} else if (chip->charger_type != POWER_SUPPLY_TYPE_USB_DCP) {
		return;
	}
	if(chip->chg_ops->check_pdphy_ready && chip->chg_ops->check_pdphy_ready() == false){
		chg_err("OPLUS CHG PD_PHY NOT READY");
		return;
	}
	if (reset_mcu_delay > RESET_MCU_DELAY_30S){
		return;
	}
	if (oplus_warp_get_warp_switch_val() == 1) {
		if (chip->chargerid_volt_got == false) {
			chip->chg_ops->set_chargerid_switch_val(1);
#ifdef CONFIG_OPLUS_CHARGER_MTK
			if (oplus_warp_get_fastchg_started() == false){
				oplus_warp_switch_mode(NORMAL_CHARGER_MODE);
			}
			usleep_range(100000, 110000);
#else
			usleep_range(20000, 22000);
#endif /* CONFIG_OPLUS_CHARGER_MTK */
			chip->chargerid_volt = chip->chg_ops->get_chargerid_volt();
			chip->chargerid_volt_got = true;
		} else {
			if (chip->chg_ops->get_chargerid_switch_val() == 0) {
				chip->chg_ops->set_chargerid_switch_val(1);
			} else {
				/* do nothing*/
			}
		}
	} else if (oplus_warp_get_warp_switch_val() == 0) {
		if (chip->chargerid_volt_got == false) {
			chip->chg_ops->set_chargerid_switch_val(1);
			oplus_warp_set_warp_chargerid_switch_val(1);
#ifdef CONFIG_OPLUS_CHARGER_MTK
			usleep_range(100000, 110000);
#else
			usleep_range(20000, 22000);
#endif /* CONFIG_OPLUS_CHARGER_MTK */
			chip->chargerid_volt = chip->chg_ops->get_chargerid_volt();
			chip->chargerid_volt_got = true;
			oplus_warp_set_warp_chargerid_switch_val(0);
			if (chip->warp_project == false) {
				chip->chg_ops->set_chargerid_switch_val(0);
			}
		} else {
			if (chip->chg_ops->get_chargerid_switch_val() == 1) {
				chip->chg_ops->set_chargerid_switch_val(0);
			} else {
				/* do nothing*/
			}
		}
	} else {
		charger_xlog_printk(CHG_LOG_CRTI, "do nothing\n");
	}
}

static void oplus_chg_chargerid_switch_check(struct oplus_chg_chip *chip)
{
	return oplus_chg_get_chargerid_voltage(chip);
}

#define RESET_MCU_DELAY_15S		3

#ifdef OPLUS_CHG_OP_DEF
extern bool oplus_get_pon_chg(void);
extern void oplus_set_pon_chg(bool flag);
#endif
static void oplus_chg_qc_config(struct oplus_chg_chip *chip);
static void oplus_chg_fast_switch_check(struct oplus_chg_chip *chip)
{
	static bool mcu_update = false;
	if (oplus_short_c_batt_is_prohibit_chg(chip)) {
		charger_xlog_printk(CHG_LOG_CRTI, " short_c_battery, return\n");
		return;
	}
	if (chip->mmi_chg == 0) {
		charger_xlog_printk(CHG_LOG_CRTI, " mmi_chg,return\n");
		return;
	}
	if (chip->allow_swtich_to_fastchg == false) {
		charger_xlog_printk(CHG_LOG_CRTI, " allow_swtich_to_fastchg == 0,return\n");
		return;
	}
	if ((!chip->authenticate) ||(!chip->hmac)) {
		charger_xlog_printk(CHG_LOG_CRTI, "non authenticate or hmac,switch return\n");
		return;
	}
	if (chip->notify_flag == NOTIFY_BAT_OVER_VOL) {
		charger_xlog_printk(CHG_LOG_CRTI, " battery over voltage,return\n");
		return;
	}
#ifndef CONFIG_OPLUS_CHARGER_MTK
#ifndef OPLUS_CHG_OP_DEF
#ifdef SUPPORT_WPC
	if (chip->wireless_support && oplus_wireless_charge_start() == true) {
			charger_xlog_printk(CHG_LOG_CRTI, "is in WPC, switch return\n");
			return;
	}
#endif
#else /* OPLUS_CHG_OP_DEF */
	if (oplus_chg_is_wls_present(chip)) {
		charger_xlog_printk(CHG_LOG_CRTI, "is in WPC, switch return\n");
		return;
	}
#endif
#endif
	if(chip->chg_ops->check_pdphy_ready && chip->chg_ops->check_pdphy_ready() == false){
		chg_err("OPLUS CHG PD_PHY NOT READY");
		return;
	}

#ifdef OPLUS_CHG_OP_DEF
	if (chip->reconnect_count > 3) {
		chg_err("reconnect_count more than 3\n");
		return;
	}

	if (chip->usb_enum_status) {
		return;
	}

	if (chip->svid_verified && !chip->is_oplus_svid) {
		return;
	}
#endif

#ifdef OPLUS_CHG_OP_DEF
	if (chip->charger_type == POWER_SUPPLY_TYPE_USB_DCP || chip->is_oplus_svid) {
#else
	if (chip->charger_type == POWER_SUPPLY_TYPE_USB_DCP) {
#endif
		if (true == opchg_get_mcu_update_state() || true == oplus_get_pon_chg()) {
			chg_err("mcu_update need suspend charger to reset adapter\n");
			reset_mcu_delay = 0;
			mcu_update = true;
			oplus_set_pon_chg(false);
			return;
		}
		if (oplus_warp_get_fastchg_started() == false
				&& reset_mcu_delay < RESET_MCU_DELAY_30S) {
			if (mcu_update == true) {
				oplus_chg_suspend_charger();
				msleep(1000);
				oplus_chg_unsuspend_charger();
				mcu_update = false;
				chg_debug("mcu_update, reset\n");
			}
#ifndef OPLUS_CHG_OP_DEF
			oplus_warp_switch_fast_chg();
#else
			if (oplus_warp_switch_fast_chg()) {
				chip->start_pd_check = false;
			} else {
				chip->start_pd_check = true;
			}
#endif
		}
		if (!oplus_warp_get_fastchg_started()
				&& !oplus_warp_get_fastchg_dummy_started()
				&& !oplus_warp_get_fastchg_to_normal()
				&& !oplus_warp_get_fastchg_to_warm()) {
			if (suspend_charger) {
				reset_mcu_delay = RESET_MCU_DELAY_15S;
				suspend_charger = false;
			}
			reset_mcu_delay++;
			if (reset_mcu_delay == RESET_MCU_DELAY_15S) {
				charger_xlog_printk(CHG_LOG_CRTI, "  reset mcu again,suspend here\n");
				if (suspend_charger == false) {
					oplus_chg_reset_adapter();
					suspend_charger = true;
					chg_debug(" fastchg start failed, reset adapter\n");
				} else {
					oplus_warp_set_ap_clk_high();
					oplus_warp_reset_mcu();
				}
			} else if (reset_mcu_delay == RESET_MCU_DELAY_30S
						&& chip->vbatt_num == 2) {
				suspend_charger = false;
				reset_mcu_delay = RESET_MCU_DELAY_30S + 1;
				charger_xlog_printk(CHG_LOG_CRTI, "  RESET_MCU_DELAY_30S\n");
				if (chip->charger_volt <= 7500) {
					oplus_warp_reset_fastchg_after_usbout();
					chip->chg_ops->set_chargerid_switch_val(0);
					if (chip->chg_ops->enable_qc_detect){
						chip->chg_ops->enable_qc_detect();
					}
				}
			}
		} else {
			suspend_charger = false;
			mcu_update = false;
		}
		if(reset_mcu_delay > RESET_MCU_DELAY_30S){
			chip->pd_swarp = false;
		}
	}
}

#define FULL_COUNTS_SW		5
#define FULL_COUNTS_HW		4

static int oplus_chg_check_sw_full(struct oplus_chg_chip *chip)
{
	int vbatt_full_vol_sw = 0;

	if (!chip->charger_exist) {
		chip->sw_full_count = 0;
		chip->sw_full = false;
		return false;
	}

	if (chip->tbatt_status == BATTERY_STATUS__COLD_TEMP) {
		vbatt_full_vol_sw = chip->limits.cold_vfloat_sw_limit;
	} else if (chip->tbatt_status == BATTERY_STATUS__LITTLE_COLD_TEMP) {
		vbatt_full_vol_sw = chip->limits.little_cold_vfloat_sw_limit;
	} else if (chip->tbatt_status == BATTERY_STATUS__COOL_TEMP) {
		vbatt_full_vol_sw = chip->limits.cool_vfloat_sw_limit;
	} else if (chip->tbatt_status == BATTERY_STATUS__LITTLE_COOL_TEMP) {
		vbatt_full_vol_sw = chip->limits.little_cool_vfloat_sw_limit;
	} else if (chip->tbatt_status == BATTERY_STATUS__NORMAL) {
		vbatt_full_vol_sw = chip->limits.normal_vfloat_sw_limit;
	} else if (chip->tbatt_status == BATTERY_STATUS__WARM_TEMP) {
		vbatt_full_vol_sw = chip->limits.warm_vfloat_sw_limit;
	} else {
		chip->sw_full_count = 0;
		chip->sw_full = 0;
		return false;
	}
	if ((!chip->authenticate) || (!chip->hmac)) {
		vbatt_full_vol_sw = chip->limits.non_standard_vfloat_sw_limit;
	}
	if (oplus_short_c_batt_is_prohibit_chg(chip)) {
		vbatt_full_vol_sw = chip->limits.short_c_bat_vfloat_sw_limit;
	}
	/* use SW Vfloat to check */
	if (chip->batt_volt > vbatt_full_vol_sw) {
		if (chip->icharging < 0 && (chip->icharging * -1) <= chip->limits.iterm_ma) {
			chip->sw_full_count++;
			if (chip->sw_full_count > FULL_COUNTS_SW) {
				chip->sw_full_count = 0;
				chip->sw_full = true;
			}
		} else if (chip->icharging >= 0) {
			chip->sw_full_count++;
			if (chip->sw_full_count > FULL_COUNTS_SW * 2) {
				chip->sw_full_count = 0;
				chip->sw_full = true;
				charger_xlog_printk(CHG_LOG_CRTI,
					"[BATTERY] Battery full by sw when icharging>=0!!\n");
			}
		} else {
			chip->sw_full_count = 0;
			chip->sw_full = false;
		}
	} else {
		chip->sw_full_count = 0;
		chip->sw_full = false;
	}
	return chip->sw_full;
}

static int oplus_chg_check_hw_full(struct oplus_chg_chip *chip)
{
	int vbatt_full_vol_hw = 0;
	static int vbat_counts_hw = 0;
	static bool ret_hw = false;

	if (!chip->charger_exist) {
		vbat_counts_hw = 0;
		ret_hw = false;
		chip->hw_full_by_sw = false;
		return false;
	}
	vbatt_full_vol_hw = oplus_chg_get_float_voltage(chip);
	if (chip->tbatt_status == BATTERY_STATUS__COLD_TEMP) {
		vbatt_full_vol_hw = chip->limits.temp_cold_vfloat_mv
			+ chip->limits.non_normal_vterm_hw_inc;
	} else if (chip->tbatt_status == BATTERY_STATUS__LITTLE_COLD_TEMP) {
		vbatt_full_vol_hw = chip->limits.temp_little_cold_vfloat_mv
			+ chip->limits.non_normal_vterm_hw_inc;
	} else if (chip->tbatt_status == BATTERY_STATUS__COOL_TEMP) {
		vbatt_full_vol_hw = chip->limits.temp_cool_vfloat_mv
			+ chip->limits.non_normal_vterm_hw_inc;
	} else if (chip->tbatt_status == BATTERY_STATUS__LITTLE_COOL_TEMP) {
		vbatt_full_vol_hw = chip->limits.temp_little_cool_vfloat_mv
			+ chip->limits.non_normal_vterm_hw_inc;
	} else if (chip->tbatt_status == BATTERY_STATUS__NORMAL) {
		vbatt_full_vol_hw = chip->limits.temp_normal_vfloat_mv
			+ chip->limits.normal_vterm_hw_inc;
	} else if (chip->tbatt_status == BATTERY_STATUS__WARM_TEMP) {
		vbatt_full_vol_hw = chip->limits.temp_warm_vfloat_mv
			+ chip->limits.non_normal_vterm_hw_inc;
	} else {
		vbat_counts_hw = 0;
		ret_hw = 0;
		chip->hw_full_by_sw = false;
		return false;
	}
	if ((!chip->authenticate) || (!chip->hmac)) {
		vbatt_full_vol_hw = chip->limits.non_standard_vfloat_mv
			+ chip->limits.non_normal_vterm_hw_inc;
	}
	if (oplus_short_c_batt_is_prohibit_chg(chip)) {
		vbatt_full_vol_hw = chip->limits.short_c_bat_vfloat_mv
			+ chip->limits.non_normal_vterm_hw_inc;
	}
	/* use HW Vfloat to check */
	if (chip->batt_volt >= vbatt_full_vol_hw) {
		vbat_counts_hw++;
		if (vbat_counts_hw >= FULL_COUNTS_HW) {
			vbat_counts_hw = 0;
			ret_hw = true;
		}
	} else {
		vbat_counts_hw = 0;
		ret_hw = false;
	}

	chip->hw_full_by_sw = ret_hw;
	return ret_hw;
}


#define FFC_VOLT_COUNTS		4
#define FFC_CURRENT_COUNTS	4

static void oplus_chg_ffc_variable_reset(struct oplus_chg_chip *chip)
{
	chip->fastchg_to_ffc = false;
	chip->fastchg_ffc_status = 0;
	chip->chg_ctrl_by_lcd = chip->chg_ctrl_by_lcd_default;
	chip->chg_ctrl_by_warp = chip->chg_ctrl_by_warp_default;
	chip->limits.iterm_ma = chip->limits.default_iterm_ma;
	chip->limits.normal_vfloat_sw_limit = chip->limits.default_normal_vfloat_sw_limit;
	chip->limits.temp_normal_vfloat_mv = chip->limits.default_temp_normal_vfloat_mv;
	chip->limits.normal_vfloat_over_sw_limit = chip->limits.default_normal_vfloat_over_sw_limit;
	chip->limits.temp_normal_fastchg_current_ma = chip->limits.default_temp_normal_fastchg_current_ma;
	chip->limits.temp_little_cool_fastchg_current_ma = chip->limits.default_temp_little_cool_fastchg_current_ma;
	chip->limits.little_cool_vfloat_sw_limit = chip->limits.default_little_cool_vfloat_sw_limit;
	chip->limits.temp_little_cool_vfloat_mv = chip->limits.default_temp_little_cool_vfloat_mv;
	chip->limits.little_cool_vfloat_over_sw_limit = chip->limits.default_little_cool_vfloat_over_sw_limit;
	oplus_chg_aging_ffc_variable_reset(chip);
}


static bool oplus_chg_check_ffc_status(struct oplus_chg_chip *chip)
{
	static int vffc1_counts = 0;
	static int vffc2_counts = 0;
	static int warm_counts = 0;
	static int normal_counts = 0;
	static int ffc_vfloat_sw_limit = 4450;

	if (chip->fastchg_to_ffc == true) {
		if (chip->fastchg_ffc_status == 1) {
			charger_xlog_printk(CHG_LOG_CRTI, "icharging:%d,batt_volt:%d,temperature:%d\n",
										chip->icharging, chip->batt_volt, chip->temperature);
			if (chip->batt_volt >= chip->limits.ffc1_normal_vfloat_sw_limit) {
				vffc1_counts ++;
				if (vffc1_counts >= FFC_VOLT_COUNTS) {
					oplus_chg_turn_on_ffc2(chip);
					charger_xlog_printk(CHG_LOG_CRTI, "batt_volt:%d, ffc1_normal_vfloat_sw_limit:%d, vffc1_counts:%d\n",
											chip->batt_volt, chip->limits.ffc1_normal_vfloat_sw_limit, vffc1_counts);
					return false;
				}
			}
			if (chip->ffc_temp_status == FFC_TEMP_STATUS__NORMAL) {
				if ((chip->icharging * -1) < (chip->limits.ff1_normal_fastchg_ma
						- chip->limits.ff1_exit_step_ma)) {
					normal_counts ++;
					charger_xlog_printk(CHG_LOG_CRTI, "icharging:%d,ff1_normal_fastchg_ma:%d,ff1_exit_step_ma:%d,normal_counts:%d\n",
												chip->icharging, chip->limits.ff1_normal_fastchg_ma, chip->limits.ff1_exit_step_ma, normal_counts);
					if (normal_counts >= FFC_CURRENT_COUNTS) {
						oplus_chg_ffc_variable_reset(chip);
						oplus_chg_turn_on_charging(chip);
						charger_xlog_printk(CHG_LOG_CRTI, "ffc normal fail icharging:%d,ff1_normal_fastchg_ma:%d,ff1_exit_step_ma:%d,normal_counts:%d\n",
												chip->icharging, chip->limits.ff1_normal_fastchg_ma, chip->limits.ff1_exit_step_ma, normal_counts);
						return true;
					}
				} else {
					normal_counts = 0;
				}
			} else if (chip->ffc_temp_status == FFC_TEMP_STATUS__WARM) {
				if ((chip->icharging * -1) < (chip->limits.ff1_warm_fastchg_ma
						- chip->limits.ff1_warm_exit_step_ma)) {
					warm_counts ++;
					if (warm_counts >= FFC_CURRENT_COUNTS) {
						oplus_chg_ffc_variable_reset(chip);
						oplus_chg_turn_on_charging(chip);
						charger_xlog_printk(CHG_LOG_CRTI, "icharging:%d,ff1_warm_exit_step_ma:%d,ff1_warm_fastchg_ma:%d,warm_counts:%d\n",
												chip->icharging, chip->limits.ff1_warm_exit_step_ma, chip->limits.ff1_warm_fastchg_ma, warm_counts);
						return true;
					}
				} else {
					warm_counts = 0;
				}
			} else {
				warm_counts = normal_counts = 0;
				oplus_chg_ffc_variable_reset(chip);
				oplus_chg_turn_on_charging(chip);
				return true;
			}
			return false;
		}
		if (chip->fastchg_ffc_status == 2) {
			if (chip->ffc_temp_status == FFC_TEMP_STATUS__WARM)
				ffc_vfloat_sw_limit = chip->limits.ffc2_warm_vfloat_sw_limit;
			else
				ffc_vfloat_sw_limit = chip->limits.ffc2_normal_vfloat_sw_limit;
			if (chip->batt_volt >= ffc_vfloat_sw_limit) {
				vffc2_counts ++;
				if (vffc2_counts >= FFC_VOLT_COUNTS) {
					oplus_chg_ffc_variable_reset(chip);
					oplus_chg_turn_on_charging(chip);
					charger_xlog_printk(CHG_LOG_CRTI, "batt_volt:%d,ffc_temp_status:%d,ffc_vfloat_sw_limit:%d,vffc2_counts:%d\n",
											chip->batt_volt, chip->ffc_temp_status, ffc_vfloat_sw_limit, vffc2_counts);
					return true;
				}
			}
			if (chip->ffc_temp_status == FFC_TEMP_STATUS__NORMAL) {
				if ((chip->icharging * -1) < (chip->limits.ffc2_normal_fastchg_ma
						- chip->limits.ffc2_exit_step_ma)) {
					normal_counts ++;
					if (normal_counts >= FFC_CURRENT_COUNTS) {
						oplus_chg_ffc_variable_reset(chip);
						oplus_chg_turn_on_charging(chip);
						charger_xlog_printk(CHG_LOG_CRTI, "icharging:%d,ffc2_normal_fastchg_ma:%d,ffc2_exit_step_ma:%d,normal_counts:%d\n",
												chip->icharging, chip->limits.ffc2_normal_fastchg_ma, chip->limits.ffc2_exit_step_ma, normal_counts);
						return true;
					}
				} else {
					normal_counts = 0;
				}
			} else if (chip->ffc_temp_status == FFC_TEMP_STATUS__WARM) {
				if ((chip->icharging * -1) < (chip->limits.ffc2_warm_fastchg_ma
						- chip->limits.ffc2_warm_exit_step_ma)) {
					warm_counts ++;
					if (warm_counts >= FFC_CURRENT_COUNTS) {
						oplus_chg_ffc_variable_reset(chip);
						oplus_chg_turn_on_charging(chip);
						charger_xlog_printk(CHG_LOG_CRTI, "icharging:%d:%d,ffc2_warm_fastchg_ma:%d,ffc2_warm_exit_step_ma:%d,warm_counts:%d\n",
												chip->icharging, chip->limits.ffc2_warm_fastchg_ma, chip->limits.ffc2_warm_exit_step_ma, warm_counts);
						return true;
					}
				} else {
					warm_counts = 0;
				}
			} else {
				warm_counts = normal_counts = 0;
				oplus_chg_ffc_variable_reset(chip);
				oplus_chg_turn_on_charging(chip);
				return true;
			}
		}
		return false;
	}
	vffc1_counts = 0;
	vffc2_counts = 0;
	warm_counts = 0;
	normal_counts = 0;
	return true;
}

static bool oplus_chg_check_vbatt_is_full_by_sw(struct oplus_chg_chip *chip)
{
	bool ret_sw = false;
	bool ret_hw = false;

	if (!chip->check_batt_full_by_sw) {
		return false;
	}

	ret_sw = oplus_chg_check_sw_full(chip);
	ret_hw = oplus_chg_check_hw_full(chip);
	if (ret_sw == true || ret_hw == true) {
		charger_xlog_printk(CHG_LOG_CRTI,
			"[BATTERY] Battery full by sw[%s] !!\n",
			(ret_sw == true) ? "S" : "H");
		return true;
	} else {
		return false;
	}
}

#define FULL_DELAY_COUNTS		5
#define DOD0_COUNTS		(8 * 60 / 5)

static void oplus_chg_check_status_full(struct oplus_chg_chip *chip)
{
	int is_batt_full = 0;
	static int fastchg_present_wait_count = 0;

	if (chip->chg_ctrl_by_warp) {
		if (oplus_warp_get_fastchg_ing() == true
				&& oplus_warp_get_fast_chg_type() != CHARGER_SUBTYPE_FASTCHG_WARP)
			return;
	} else {
		if (oplus_warp_get_fastchg_ing() == true)
			return;
	}

	if (oplus_warp_get_allow_reading() == false) {
		is_batt_full = 0;
		fastchg_present_wait_count = 0;
	} else {
		if (((oplus_warp_get_fastchg_to_normal()== true)
				|| (oplus_warp_get_fastchg_to_warm() == true))
				&& (fastchg_present_wait_count <= FULL_DELAY_COUNTS)) {
			is_batt_full = 0;
			fastchg_present_wait_count++;
			if (fastchg_present_wait_count == FULL_DELAY_COUNTS &&
#ifdef OPLUS_CHG_OP_DEF
			    (chip->chg_ops->get_charging_enable() == false ||
			     oplus_warp_get_ffc_chg_start()) &&
#else
			    (chip->chg_ops->get_charging_enable() == false) &&
#endif
			    chip->charging_state != CHARGING_STATUS_FULL &&
			    chip->charging_state != CHARGING_STATUS_FAIL) {
#ifdef OPLUS_CHG_OP_DEF
				oplus_warp_set_ffc_chg_start_false();
#endif
				if (chip->ffc_support && chip->ffc_temp_status != FFC_TEMP_STATUS__HIGH
						&& chip->ffc_temp_status != FFC_TEMP_STATUS__LOW) {
					if (chip->vbatt_num == 2 && chip->dual_ffc == false) {
#ifdef OPLUS_CHG_OP_DEF
						oplus_chg_turn_on_ffc1(chip);
#else
						oplus_chg_turn_on_ffc2(chip);
#endif
					} else {
						oplus_chg_turn_on_ffc1(chip);
					}
				} else {
					oplus_chg_turn_on_charging(chip);
				}
			}
		} else {
			is_batt_full = chip->chg_ops->read_full();
			chip->hw_full = is_batt_full;
			fastchg_present_wait_count = 0;
		}
	}
	if (oplus_chg_check_ffc_status(chip) == false) {
		return;
	}
#ifndef CONFIG_OPLUS_CHARGER_MTK
#ifndef OPLUS_CHG_OP_DEF
	if (chip->wireless_support == true && oplus_wpc_get_ffc_charging() == true) {
		if (chip->batt_volt > chip->limits.normal_vfloat_sw_limit)
			charger_xlog_printk(CHG_LOG_CRTI, "in wpc ffc charging\n");
		return;
	}
#else /* OPLUS_CHG_OP_DEF */
	if (is_comm_ocm_available(chip) &&
	    oplus_chg_comm_get_ffc_status(chip->comm_ocm) != FFC_DEFAULT) {
		if (chip->batt_volt > chip->limits.normal_vfloat_sw_limit)
			charger_xlog_printk(CHG_LOG_CRTI, "in wpc ffc charging\n");
		return;
	}
#endif /* OPLUS_CHG_OP_DEF */
#endif
	if ((is_batt_full == 1) || (chip->charging_state == CHARGING_STATUS_FULL)
			|| oplus_chg_check_vbatt_is_full_by_sw(chip)) {
		charger_xlog_printk(CHG_LOG_CRTI, "is_batt_full : %d,  chip->charging_state= %d\n", is_batt_full, chip->charging_state);
		oplus_chg_full_action(chip);
		if (chip->tbatt_status == BATTERY_STATUS__LITTLE_COLD_TEMP
				|| chip->tbatt_status == BATTERY_STATUS__COOL_TEMP
				|| chip->tbatt_status == BATTERY_STATUS__LITTLE_COOL_TEMP
				|| chip->tbatt_status == BATTERY_STATUS__NORMAL) {
			oplus_gauge_set_batt_full(true);
		}

		if ((chip->recharge_after_full == true || chip->vbatt_num == 2)
			&& (chip->tbatt_when_full <= 450 && chip->tbatt_when_full >= 0)
			&& (chip->temperature <= 350 && chip->temperature >= 0)) {
			chip->dod0_counts ++;
			if (chip->dod0_counts == DOD0_COUNTS) {
				if (chip->vbatt_num == 2){
					oplus_gauge_update_battery_dod0();
					//chip->in_rechging = true;
					//oplus_chg_voter_charging_start(chip, CHG_STOP_VOTER__FULL);/*now rechging!*/
					charger_xlog_printk(CHG_LOG_CRTI, "oplus_chg_check_status_full,dod0_counts = %d\n", chip->dod0_counts);
				}
				if (chip->recharge_after_full == true && chip->recharge_after_ffc == true) {
					chip->in_rechging = true;
					chip->sw_full_count = 0;
					chip->sw_full = false;
					chip->recharge_after_ffc = false;
					oplus_chg_voter_charging_start(chip, CHG_STOP_VOTER__FULL);
					charger_xlog_printk(CHG_LOG_CRTI, "recharge after full, dod0_counts = %d\n", chip->dod0_counts);
				}
				chip->dod0_counts = DOD0_COUNTS + 1;
			}
		}
	} else if (chip->charging_state == CHARGING_STATUS_FAIL) {
		oplus_chg_fail_action(chip);
	} else {
		chip->charging_state = CHARGING_STATUS_CCCV;
	}
}

static void oplus_chg_kpoc_power_off_check(struct oplus_chg_chip *chip)
{
#ifdef CONFIG_MTK_KERNEL_POWER_OFF_CHARGING
	if (chip->boot_mode == KERNEL_POWER_OFF_CHARGING_BOOT
			|| chip->boot_mode == LOW_POWER_OFF_CHARGING_BOOT) {		/*vbus < 2.5V*/
		if ((chip->chg_ops->check_chrdet_status() == false)
				&& (chip->charger_volt < 2500)) {
			msleep(1000);
			charger_xlog_printk(CHG_LOG_CRTI,"pmic_thread_kthread]Unplug Charger/USB double check!\n");
			if ((chip->chg_ops->check_chrdet_status() == false)
					&& (chip->chg_ops->get_charger_volt() < 2500)) {
				if ((oplus_warp_get_fastchg_to_normal() == false)
						&& (oplus_warp_get_fastchg_to_warm() == false)
						&& (oplus_warp_get_adapter_update_status() != ADAPTER_FW_NEED_UPDATE)
						&& (oplus_warp_get_btb_temp_over() == false)) {
					charger_xlog_printk(CHG_LOG_CRTI,
						"[pmic_thread_kthread]Unplug Charger/USB \
						In Kernel Power Off Charging Mode Shutdown OS!\n");
					chip->chg_ops->set_power_off();
				}
			}
		}
	}
#endif
}

static void oplus_chg_print_log(struct oplus_chg_chip *chip)
{
	if(chip->vbatt_num == 1){
		charger_xlog_printk(CHG_LOG_CRTI,
			" CHGR[ %d / %d / %d / %d / %d ], "
			"BAT[ %d / %d / %d / %d / %d / %d ], "
			"GAUGE[ %d / %d / %d / %d / %d / %d / %d / %d / %d / %d / %d / %d / %d / %d / %d / %d / %d / %d / %d / %d / %d / %d /], "
			"STATUS[ 0x%x / %d / %d / %d / %d / 0x%x ], OTHER[ %d / %d / %d / %d / %d/ %d / %d]\n",
			chip->charger_exist, chip->charger_type, chip->charger_volt,
			chip->prop_status, chip->boot_mode,
			chip->batt_exist, chip->batt_full, chip->chging_on, chip->in_rechging,
			chip->charging_state, chip->total_time,
			chip->temperature, chip->batt_volt, chip->batt_volt_min, chip->icharging,
			chip->ibus, chip->soc, chip->ui_soc, chip->soc_load, chip->batt_rm,
			oplus_gauge_get_batt_fc(),oplus_gauge_get_batt_qm(),
			oplus_gauge_get_batt_pd(),oplus_gauge_get_batt_rcu(),
			oplus_gauge_get_batt_rcf(),oplus_gauge_get_batt_fcu(),
			oplus_gauge_get_batt_fcf(),oplus_gauge_get_batt_sou(),
			oplus_gauge_get_batt_do0(),oplus_gauge_get_batt_doe(),
			oplus_gauge_get_batt_trm(),oplus_gauge_get_batt_pc(),
			oplus_gauge_get_batt_qs(),
			chip->vbatt_over, chip->chging_over_time, chip->vchg_status,
			chip->tbatt_status, chip->stop_voter, chip->notify_code,
			chip->otg_switch, chip->mmi_chg, chip->boot_reason, chip->boot_mode,
			chip->chargerid_volt, chip->chargerid_volt_got, chip->shell_temp);
	}else{
		if (chip->prop_status != POWER_SUPPLY_STATUS_CHARGING) {
			oplus_gauge_dump_register();
		}
		charger_xlog_printk(CHG_LOG_CRTI,
			"CHGR[ %d / %d / %d / %d / %d ],"
			"BAT[ %d / %d / %d / %d / %d / %d ],"
			"GAUGE[ %d / %d / %d / %d / %d / %d / %d / %d / %d ],"
			"STATUS[ 0x%x / %d / %d / %d / %d / 0x%x ],"
			"OTHER[ %d / %d / %d / %d / %d/ %d / %d ],"
			"OTHER2[ %d / %d / %d / %d ]\n",
			chip->charger_exist, chip->charger_type, chip->charger_volt,
			chip->prop_status, chip->boot_mode,
			chip->batt_exist, chip->batt_full, chip->chging_on, chip->in_rechging,
			chip->charging_state, chip->total_time,
			chip->temperature, chip->batt_volt, chip->batt_volt_min, chip->icharging,
			chip->ibus, chip->soc, chip->ui_soc, chip->soc_load, chip->batt_rm,
			chip->vbatt_over, chip->chging_over_time, chip->vchg_status,
			chip->tbatt_status, chip->stop_voter, chip->notify_code,
			chip->otg_switch, chip->mmi_chg, chip->boot_reason, chip->boot_mode,
			chip->chargerid_volt, chip->chargerid_volt_got, chip->shell_temp,
			chip->otg_online, chip->vph_voltage, chip->abnormal_volt_detected, chip->hw_detected);
	}

#ifdef CONFIG_OPLUS_CHARGER_MTK
	if (chip->charger_type == POWER_SUPPLY_TYPE_USB_DCP) {
		oplus_warp_print_log();
	}
#endif
}

#define CHARGER_ABNORMAL_DETECT_TIME	24

static void oplus_chg_critical_log(struct oplus_chg_chip *chip)
{
	static int chg_abnormal_count = 0;

	if (chip->charger_exist) {
		if (chip->stop_voter == 0
				&& chip->charger_type == POWER_SUPPLY_TYPE_USB_DCP
				&& chip->soc <= 75 && chip->icharging >= -20) {
			chg_abnormal_count++;
			if (chg_abnormal_count >= CHARGER_ABNORMAL_DETECT_TIME) {
				chg_abnormal_count = CHARGER_ABNORMAL_DETECT_TIME;
				charger_abnormal_log = CRITICAL_LOG_UNABLE_CHARGING;
			}
			charger_xlog_printk(CHG_LOG_CRTI, " unable charging, count=%d, charger_abnormal_log=%d\n", chg_abnormal_count, charger_abnormal_log);
#ifdef OPLUS_CHG_REG_DUMP_ENABLE
			if (chg_abnormal_count >= 10 && !chip->reg_dump && is_usb_ocm_available(chip)) {
				chip->reg_dump = true;
				oplus_chg_anon_mod_event(chip->usb_ocm, OPLUS_CHG_EVENT_REG_DUMP);
			}
#endif
		} else {
				chg_abnormal_count = 0;
		}
		if ((chip->stop_voter & CHG_STOP_VOTER__BATTTEMP_ABNORMAL)
				== CHG_STOP_VOTER__BATTTEMP_ABNORMAL) {
			charger_abnormal_log = CRITICAL_LOG_BATTTEMP_ABNORMAL;
		} else if ((chip->stop_voter & CHG_STOP_VOTER__VCHG_ABNORMAL)
				== CHG_STOP_VOTER__VCHG_ABNORMAL) {
			charger_abnormal_log = CRITICAL_LOG_VCHG_ABNORMAL;
		} else if ((chip->stop_voter & CHG_STOP_VOTER__VBAT_TOO_HIGH)
				== CHG_STOP_VOTER__VBAT_TOO_HIGH) {
			charger_abnormal_log = CRITICAL_LOG_VBAT_TOO_HIGH;
		} else if ((chip->stop_voter & CHG_STOP_VOTER__MAX_CHGING_TIME)
				== CHG_STOP_VOTER__MAX_CHGING_TIME) {
			charger_abnormal_log = CRITICAL_LOG_CHARGING_OVER_TIME;
		} else {
			/*do nothing*/
		}
	} else if (oplus_warp_get_btb_temp_over() == true
			|| oplus_warp_get_fastchg_to_normal() == true) {
		/*Do not clear 0x5d and 0x59*/
		charger_xlog_printk(CHG_LOG_CRTI, " btb_temp_over or fastchg_to_normal, charger_abnormal_log=%d\n", charger_abnormal_log);
	} else {
		charger_abnormal_log = 0;
	}
}

int oplus_chg_get_curr_time_ms(unsigned long *time_ms)
{
	u64 ts_nsec;

	ts_nsec = local_clock();
	*time_ms = ts_nsec / 1000000;

	return *time_ms;
}

static void oplus_chg_other_thing(struct oplus_chg_chip *chip)
{
	static unsigned long start_chg_time = 0;
	unsigned long cur_chg_time = 0;
	if (oplus_warp_get_fastchg_started() == false) {
		chip->chg_ops->kick_wdt();
		chip->chg_ops->dump_registers();
	}
	if (chip->charger_exist) {
		if (chgr_dbg_total_time != 0) {
			chip->total_time = chgr_dbg_total_time;
		} else {
			if (!chip->total_time) {
				oplus_chg_get_curr_time_ms(&start_chg_time);
				start_chg_time = start_chg_time/1000;
				chip->total_time += OPLUS_CHG_UPDATE_INTERVAL_SEC;
			} else {
				oplus_chg_get_curr_time_ms(&cur_chg_time);
				cur_chg_time = cur_chg_time/1000;
				chip->total_time = OPLUS_CHG_UPDATE_INTERVAL_SEC + cur_chg_time - start_chg_time;
			}
		}
	}
	oplus_chg_debug_chg_monitor(chip);
	oplus_chg_print_log(chip);
	oplus_chg_critical_log(chip);
#ifndef CONFIG_OPLUS_CHARGER_MTK
#ifndef OPLUS_CHG_OP_DEF
#ifndef WPC_NEW_INTERFACE
	if(chip->wireless_support){
        oplus_wpc_print_log();
        chargepump_print_log();
	}
#endif
#endif
#endif
}

#define IBATT_COUNT	10

static void oplus_chg_ibatt_check_and_set(struct oplus_chg_chip *chip)
{
	static int average_current = 0;
	static int ibatt_count = 0;
	static int current_adapt = 0;
	static int pre_tbatt_status = BATTERY_STATUS__INVALID;
	static int fail_count = 0;
	bool set_current_flag = false;
	int recharge_volt = 0;
	int current_limit = 0;
	int current_init = 0;
	int threshold = 0;
	int current_step = 0;

	if ((chip->chg_ops->need_to_check_ibatt
			&& chip->chg_ops->need_to_check_ibatt() == false)
			|| !chip->chg_ops->need_to_check_ibatt) {
		return;
	}
	if (!chip->charger_exist || (oplus_warp_get_fastchg_started() == true)) {
		current_adapt = 0;
		ibatt_count = 0;
		average_current = 0;
		fail_count = 0;
		return;
	}
	if (oplus_short_c_batt_is_prohibit_chg(chip)) {
		return;
	}
	if (current_adapt == 0) {
		if (chip->tbatt_status == BATTERY_STATUS__LITTLE_COLD_TEMP) {
			current_adapt = chip->limits.temp_little_cold_fastchg_current_ma;
			pre_tbatt_status = BATTERY_STATUS__LITTLE_COLD_TEMP;
		} else if (chip->tbatt_status == BATTERY_STATUS__WARM_TEMP) {
			current_adapt = chip->limits.temp_warm_fastchg_current_ma;
			pre_tbatt_status = BATTERY_STATUS__WARM_TEMP;
		} else if (chip->tbatt_status == BATTERY_STATUS__COLD_TEMP) {
			current_adapt = chip->limits.temp_cold_fastchg_current_ma;
			pre_tbatt_status = BATTERY_STATUS__COLD_TEMP;
		} else if (chip->tbatt_status == BATTERY_STATUS__NORMAL) {
			current_adapt = chip->limits.temp_normal_fastchg_current_ma;
			pre_tbatt_status = BATTERY_STATUS__NORMAL;
		} else if (chip->tbatt_status == BATTERY_STATUS__COOL_TEMP) {
			//if ( chip->batt_volt > (chip->vbatt_num * 4180)) {
			if ( chip->batt_volt > 4180) {
				current_adapt = chip->limits.temp_cool_fastchg_current_ma_low;
			} else {
				current_adapt = chip->limits.temp_cool_fastchg_current_ma_high;
			}
			pre_tbatt_status = BATTERY_STATUS__COOL_TEMP;
		} else if (chip->tbatt_status == BATTERY_STATUS__LITTLE_COOL_TEMP) {
			current_adapt = chip->limits.temp_little_cool_fastchg_current_ma;
			pre_tbatt_status = BATTERY_STATUS__LITTLE_COOL_TEMP;
		}
	}
	if (chip->tbatt_status != pre_tbatt_status) {
		current_adapt = 0;
		ibatt_count = 0;
		average_current = 0;
		fail_count = 0;
		return;
	}
	if (chip->tbatt_status == BATTERY_STATUS__LITTLE_COLD_TEMP) {
		recharge_volt = chip->limits.temp_little_cold_vfloat_mv - chip->limits.recharge_mv;
		current_init = chip->limits.temp_little_cold_fastchg_current_ma;
		current_limit = chip->batt_capacity_mah * 15 / 100;
		threshold = 50;
	} else if (chip->tbatt_status == BATTERY_STATUS__WARM_TEMP) {
		recharge_volt = chip->limits.temp_warm_vfloat_mv - chip->limits.recharge_mv;
		current_init = chip->limits.temp_warm_fastchg_current_ma;
		current_limit = chip->batt_capacity_mah * 25 / 100;
		threshold = 50;
	} else if (chip->tbatt_status == BATTERY_STATUS__COLD_TEMP) {
		recharge_volt = chip->limits.temp_cold_vfloat_mv - chip->limits.recharge_mv;
		current_init = chip->limits.temp_cold_fastchg_current_ma;
		current_limit = 350;
		threshold = 50;
	} else if (chip->tbatt_status == BATTERY_STATUS__NORMAL) {
		recharge_volt = chip->limits.temp_normal_vfloat_mv- chip->limits.recharge_mv;
		current_init = chip->limits.temp_normal_fastchg_current_ma;
		if (chip->warp_project) {
			current_limit = 2000;
		} else {
			current_limit = chip->batt_capacity_mah * 65 / 100;
		}
		threshold = 70;
	} else if (chip->tbatt_status == BATTERY_STATUS__COOL_TEMP) {
		recharge_volt = chip->limits.temp_cool_vfloat_mv - chip->limits.recharge_mv;
		if (vbatt_higherthan_4180mv) {
				current_init = chip->limits.temp_cool_fastchg_current_ma_low;
				current_limit = chip->batt_capacity_mah * 15 / 100;
		} else {
				current_init = chip->limits.temp_cool_fastchg_current_ma_high;
				current_limit = chip->batt_capacity_mah * 25 / 100;
		}
		threshold = 50;
	} else if (chip->tbatt_status == BATTERY_STATUS__LITTLE_COOL_TEMP) {
		recharge_volt = chip->limits.temp_little_cool_vfloat_mv - chip->limits.recharge_mv;
		current_init = chip->limits.temp_little_cool_fastchg_current_ma;
		current_limit = chip->batt_capacity_mah * 45 / 100;
		threshold = 70;
	}
	if (chip->batt_volt > recharge_volt || chip->led_on) {
		ibatt_count = 0;
		average_current = 0;
		fail_count = 0;
		return;
	}
	if (oplus_warp_get_allow_reading() == true) {
		current_step = chip->chg_ops->get_chg_current_step();
	} else {
		current_adapt = 0;
		ibatt_count = 0;
		average_current = 0;
		fail_count = 0;
		return;
	}
	if (chip->icharging < 0) {
		ibatt_count++;
		average_current = average_current + chip->icharging;
	}
	/*charge current larger than limit*/
	if ((-1 * chip->icharging) > current_limit) {
		if (current_adapt > current_init) {
			current_adapt = current_init;
		} else {
			current_adapt -= 2 * current_step;
		}
		set_current_flag = true;
		fail_count++;
	} else if (ibatt_count == IBATT_COUNT) {
		average_current = -1 * average_current / ibatt_count;
		threshold += fail_count * current_step;
		if (average_current < current_limit - threshold) {
			current_adapt += current_step;
			set_current_flag = true;
		} else {
			ibatt_count = 0;
			average_current = 0;
		}
	}
	if (set_current_flag == true) {
		if (current_adapt > (current_limit + 100)) {
			current_adapt = current_limit + 100;
		} else if (current_adapt < 103) {/*(512*20%)*/
			current_adapt = 103;
		}
		charger_xlog_printk(CHG_LOG_CRTI,
			"charging_current_write_fast[%d] step[%d]\n",
			current_adapt, current_step);
		chip->chg_ops->charging_current_write_fast(current_adapt);
		ibatt_count = 0;
		average_current = 0;
	}
}

#ifndef OPLUS_CHG_OP_DEF
static void oplus_chg_pd_config(struct oplus_chg_chip *chip)
{
	int ret = 0;

	if(chip->pd_swarp == true){
		return;
	}

	if (chip->charger_type != POWER_SUPPLY_TYPE_USB_DCP) {
		chip->pd_chging = false;
		return;
	}
	if (!chip->chg_ops->oplus_chg_pd_setup
			|| !chip->chg_ops->oplus_chg_get_pd_type) {
		return;
	}

	if (chip->dual_charger_support) {
		if (chip->charger_volt > 7500) {
			chip->pd_chging = true;
		} else {
			chip->pd_chging = false;
		}
	}

	if (chip->pd_chging == false && chip->chg_ops->oplus_chg_get_pd_type() == true
			&& oplus_chg_show_warp_logo_ornot() == false) {
		ret = chip->chg_ops->oplus_chg_pd_setup();
		if (ret >= 0) {
			chip->pd_chging = true;
			chip->limits.temp_little_cool_fastchg_current_ma
				= chip->limits.pd_temp_little_cool_fastchg_current_ma;
			chip->limits.temp_normal_fastchg_current_ma
				= chip->limits.pd_temp_normal_fastchg_current_ma;
			chip->limits.temp_little_cold_fastchg_current_ma_high
				= chip->limits.pd_temp_little_cold_fastchg_current_ma_high;
			chip->limits.temp_little_cold_fastchg_current_ma_low
				= chip->limits.pd_temp_little_cold_fastchg_current_ma_low;
			chip->limits.temp_cool_fastchg_current_ma_high
				= chip->limits.pd_temp_cool_fastchg_current_ma_high;
			chip->limits.temp_cool_fastchg_current_ma_low
				= chip->limits.pd_temp_cool_fastchg_current_ma_low;
			chip->limits.temp_warm_fastchg_current_ma
				= chip->limits.pd_temp_warm_fastchg_current_ma;
			chip->limits.input_current_charger_ma
				= chip->limits.pd_input_current_charger_ma;
			oplus_chg_set_charging_current(chip);
			oplus_chg_set_input_current_limit(chip);
			oplus_chg_enable_burst_mode(true);
			oplus_chg_get_charger_voltage();
		}
	}
}
#else
static void oplus_chg_pd_config(struct oplus_chg_chip *chip)
{
	int ret = 0;

	if (chip->charger_type != POWER_SUPPLY_TYPE_USB_DCP) {
		chip->pd_chging = false;
		return;
	}
	if (!chip->chg_ops->oplus_chg_pd_setup
			|| !chip->chg_ops->oplus_chg_get_pd_type) {
		return;
	}

	if (chip->dual_charger_support) {
		if (chip->charger_volt > 7500) {
			chip->pd_chging = true;
		} else {
			chip->pd_chging = false;
		}
	}

	if (chip->pd_chging == false && chip->chg_ops->oplus_chg_get_pd_type() == true) {
		ret = chip->chg_ops->oplus_chg_pd_setup();
		if (ret >= 0) {
			chip->pd_chging = true;
			chip->limits.temp_little_cool_fastchg_current_ma
				= chip->limits.pd_temp_little_cool_fastchg_current_ma;
			chip->limits.temp_normal_fastchg_current_ma
				= chip->limits.pd_temp_normal_fastchg_current_ma;
			chip->limits.temp_little_cold_fastchg_current_ma_high
				= chip->limits.pd_temp_little_cold_fastchg_current_ma_high;
			chip->limits.temp_little_cold_fastchg_current_ma_low
				= chip->limits.pd_temp_little_cold_fastchg_current_ma_low;
			chip->limits.temp_cool_fastchg_current_ma_high
				= chip->limits.pd_temp_cool_fastchg_current_ma_high;
			chip->limits.temp_cool_fastchg_current_ma_low
				= chip->limits.pd_temp_cool_fastchg_current_ma_low;
			chip->limits.temp_warm_fastchg_current_ma
				= chip->limits.pd_temp_warm_fastchg_current_ma;
			chip->limits.input_current_charger_ma
				= chip->limits.pd_input_current_charger_ma;
			oplus_chg_set_charging_current(chip);
			oplus_chg_set_input_current_limit(chip);
			oplus_chg_enable_burst_mode(true);
		}
	}
}
#endif

static void oplus_chg_pdqc_to_normal(struct oplus_chg_chip *chip)
{
	int ret = 0;
	static int pdqc_9v = false;

	if (!chip->chg_ops->get_charger_subtype) {
		return;
	}
	if (!chip->chg_ops->oplus_chg_pd_setup || !chip->chg_ops->set_qc_config) {
		return;
	}
	if (chip->limits.vbatt_pdqc_to_5v_thr < 0) {
		return;
	}
	if(oplus_chg_show_warp_logo_ornot() == true){
		return;
	}
	if (chip->charger_volt > 7500) {
		pdqc_9v = true;
	} else {
		pdqc_9v = false;
	}
	if (chip->chg_ops->get_charger_subtype() == CHARGER_SUBTYPE_PD) {
		if (chip->batt_volt > chip->limits.vbatt_pdqc_to_5v_thr && pdqc_9v == true) {
			ret = chip->chg_ops->oplus_chg_pd_setup();
			if (ret >= 0) {
				pdqc_9v = false;
				chip->limits.temp_normal_fastchg_current_ma
					= chip->limits.default_temp_normal_fastchg_current_ma;
				chip->limits.temp_little_cool_fastchg_current_ma
					= chip->limits.default_temp_little_cool_fastchg_current_ma;
				chip->limits.temp_little_cool_fastchg_current_ma_high
					= chip->limits.default_temp_little_cool_fastchg_current_ma_high;
				chip->limits.temp_little_cool_fastchg_current_ma_low
					= chip->limits.default_temp_little_cool_fastchg_current_ma_low;
				chip->limits.temp_little_cold_fastchg_current_ma_high
					= chip->limits.default_temp_little_cold_fastchg_current_ma_high;
				chip->limits.temp_little_cold_fastchg_current_ma_low
					= chip->limits.default_temp_little_cold_fastchg_current_ma_low;
				chip->limits.temp_cool_fastchg_current_ma_high
					= chip->limits.default_temp_cool_fastchg_current_ma_high;
				chip->limits.temp_cool_fastchg_current_ma_low
					= chip->limits.default_temp_cool_fastchg_current_ma_low;
				chip->limits.temp_warm_fastchg_current_ma
					= chip->limits.default_temp_warm_fastchg_current_ma;
				chip->limits.input_current_charger_ma
					= chip->limits.default_input_current_charger_ma;
				oplus_chg_set_charging_current(chip);
				oplus_chg_set_input_current_limit(chip);
			}
		}
	} else if (chip->chg_ops->get_charger_subtype() == CHARGER_SUBTYPE_QC) {
		if (chip->batt_volt > chip->limits.vbatt_pdqc_to_5v_thr && pdqc_9v == true) {
			ret = chip->chg_ops->set_qc_config();
			if (ret >= 0) {
				pdqc_9v = false;
				chip->limits.temp_normal_fastchg_current_ma
					= chip->limits.default_temp_normal_fastchg_current_ma;
				chip->limits.temp_little_cool_fastchg_current_ma
					= chip->limits.default_temp_little_cool_fastchg_current_ma;
				chip->limits.temp_little_cool_fastchg_current_ma_high
					= chip->limits.default_temp_little_cool_fastchg_current_ma_high;
				chip->limits.temp_little_cool_fastchg_current_ma_low
					= chip->limits.default_temp_little_cool_fastchg_current_ma_low;
				chip->limits.temp_little_cold_fastchg_current_ma_high
					= chip->limits.default_temp_little_cold_fastchg_current_ma_high;
				chip->limits.temp_little_cold_fastchg_current_ma_low
					= chip->limits.default_temp_little_cold_fastchg_current_ma_low;
				chip->limits.temp_cool_fastchg_current_ma_high
					= chip->limits.default_temp_cool_fastchg_current_ma_high;
				chip->limits.temp_cool_fastchg_current_ma_low
					= chip->limits.default_temp_cool_fastchg_current_ma_low;
				chip->limits.temp_warm_fastchg_current_ma
					= chip->limits.default_temp_warm_fastchg_current_ma;
				chip->limits.input_current_charger_ma
					= chip->limits.default_input_current_charger_ma;
				oplus_chg_set_charging_current(chip);
				oplus_chg_set_input_current_limit(chip);
			}
		}
	}
}
static void oplus_chg_qc_config(struct oplus_chg_chip *chip)
{
	static int qc_chging = false;
	int ret = 0;

	if (qc_chging == true && (chip->charger_volt < 7500)) {
		if (chip->qc_abnormal_check_count > 3) {
		oplus_chg_enable_burst_mode(false);
		chip->chg_ops->pdo_5v();
		chg_err("not config qc dul to abnormal checked\n");
		return;
		}
		chip->qc_abnormal_check_count++;
	} else
			chip->qc_abnormal_check_count = 0;

	if (chip->charger_type != POWER_SUPPLY_TYPE_USB_DCP) {
		qc_chging = false;
		return;
	}
	if (!chip->chg_ops->set_qc_config || !chip->chg_ops->get_charger_subtype)
		return;
	chg_err("chip->charger_type[%d], subtype[%d]\n",
		chip->charger_type, chip->chg_ops->get_charger_subtype());

	if (chip->charger_volt > 7500) {
		qc_chging = true;
	} else {
		qc_chging = false;
	}

	if (qc_chging == false
			&& chip->chg_ops->get_charger_subtype() == CHARGER_SUBTYPE_QC) {
		ret = chip->chg_ops->set_qc_config();
		if (ret >= 0) {
			qc_chging = true;
			chg_err("QC  config success");
#ifdef OPLUS_CHG_OP_DEF
			chip->limits.temp_little_cool_fastchg_current_ma
				= chip->limits.qc_temp_little_cool_fastchg_current_ma;
#endif
			chip->limits.temp_little_cool_fastchg_current_ma_high
				= chip->limits.qc_temp_little_cool_fastchg_current_ma;
			chip->limits.temp_little_cool_fastchg_current_ma_low
				= chip->limits.qc_temp_little_cool_fastchg_current_ma;
			chip->limits.temp_normal_fastchg_current_ma
				= chip->limits.qc_temp_normal_fastchg_current_ma;
			chip->limits.temp_little_cold_fastchg_current_ma_high
				= chip->limits.qc_temp_little_cold_fastchg_current_ma_high;
			chip->limits.temp_little_cold_fastchg_current_ma_low
				= chip->limits.qc_temp_little_cold_fastchg_current_ma_low;
			chip->limits.temp_cool_fastchg_current_ma_high
				= chip->limits.qc_temp_cool_fastchg_current_ma_high;
			chip->limits.temp_cool_fastchg_current_ma_low
				= chip->limits.qc_temp_cool_fastchg_current_ma_low;
			chip->limits.temp_warm_fastchg_current_ma
				= chip->limits.qc_temp_warm_fastchg_current_ma;
			chip->limits.input_current_charger_ma
				= chip->limits.qc_input_current_charger_ma;
			oplus_chg_set_input_current_limit(chip);
			oplus_chg_set_charging_current(chip);
			oplus_chg_enable_burst_mode(true);
			oplus_chg_get_charger_voltage();
		}
	}
}

static void oplus_chg_dual_charger_config(struct oplus_chg_chip *chip)
{
	static int enable_slave_cnt = 0;
	static int disable_slave_cnt = 0;

	if (!chip->dual_charger_support) {
		return;
	}

        if (chip->charger_type != POWER_SUPPLY_TYPE_USB_DCP) {
                return;
        }

	if (chip->chg_ops->get_charger_subtype() == CHARGER_SUBTYPE_DEFAULT) {
		return;
	}


	if (chip->slave_charger_enable == false) {
		if (chip->icharging < 0 && (chip->icharging * -1) > chip->slave_chg_enable_ma) {
			enable_slave_cnt++;
		} else {
			enable_slave_cnt = 0;
		}

		if (enable_slave_cnt >= 3) {
			chg_err("Enable slave charger!!\n");
			chip->slave_charger_enable = true;
			oplus_chg_set_input_current_limit(chip);
			oplus_chg_set_charging_current(chip);
			enable_slave_cnt = 0;
		}
	} else {
		if (chip->slave_charger_enable ==  true) {
			if (chip->icharging < 0 && (chip->icharging * -1) < chip->slave_chg_disable_ma) {
				disable_slave_cnt++;
			} else {
				disable_slave_cnt = 0;
			}

			if (disable_slave_cnt >= 3) {
				chg_err("Disable slave charger!!\n");
				chip->slave_charger_enable = false;
				oplus_chg_set_input_current_limit(chip);
				oplus_chg_set_charging_current(chip);
				disable_slave_cnt = 0;
			}
		}
	}
}

static void oplus_chg_reset_adapter_work(struct work_struct *work) {
	oplus_chg_suspend_charger();
	msleep(1000);
	if (g_charger_chip->mmi_chg) {
		oplus_chg_unsuspend_charger();
		oplus_warp_set_ap_clk_high();
		oplus_warp_reset_mcu();
	}
}

static void oplus_chg_update_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_chip *chip = container_of(dwork, struct oplus_chg_chip, update_work);
#ifdef OPLUS_CHG_OP_DEF
	int shedule_work = 0;
	static int chg_count = 0;
	mutex_lock(&chip->update_work_lock);
#endif

	oplus_charger_detect_check(chip);
	oplus_chg_get_battery_data(chip);

#ifdef OPLUS_CHG_OP_DEF
	oplus_check_battery_vol_diff(chip);
	oplus_check_ovp_status(chip);
#endif
	if (chip->charger_exist) {
		oplus_chg_aicl_check(chip);
		oplus_chg_protection_check(chip);
		oplus_chg_check_tbatt_normal_status(chip);
		oplus_chg_check_status_full(chip);
		oplus_chg_battery_notify_check(chip);
		oplus_chg_pd_config(chip);
		oplus_chg_get_chargerid_voltage(chip);
		oplus_chg_fast_switch_check(chip);
		oplus_chg_chargerid_switch_check(chip);
	}
#ifdef OPLUS_CHG_OP_DEF
	mutex_unlock(&chip->update_work_lock);
	if (chip->charger_exist && chip->charger_type != POWER_SUPPLY_TYPE_USB
		 && (get_eng_version() == HIGH_TEMP_AGING || get_eng_version() == AGING)) {
		chg_count++;
		if (chip->ibus < 1200 && chg_count > 100) {
			charger_xlog_printk(CHG_LOG_CRTI,
				" PQE: led_on = %d, \
				current_limit = %d, \
				led_temp_status = %d\n",
				chip->led_on,
				chip->ibus,
				chip->led_temp_status);
				chip->chg_ops->input_current_write(1800);
				chg_count = 0;
		}
	} else {
		chg_count = 0;
	}
#endif
	oplus_chg_dual_charger_config(chip);
	oplus_chg_qc_config(chip);
	oplus_chg_pdqc_to_normal(chip);
	oplus_chg_ibatt_check_and_set(chip);
	/* oplus_chg_short_c_battery_check(chip); */
	if (chip->shortc_thread != NULL)
		wake_up_process(chip->shortc_thread);
	oplus_chg_battery_update_status(chip);
	oplus_chg_kpoc_power_off_check(chip);
	oplus_chg_other_thing(chip);
	/* run again after interval */
#ifdef OPLUS_CHG_OP_DEF
	if (!timer_pending(&chip->update_work.timer)) {
		shedule_work = mod_delayed_work(
			system_highpri_wq, &chip->update_work,
			OPLUS_CHG_UPDATE_INTERVAL(oplus_chg_update_slow(chip)));
	} else {
		schedule_delayed_work(&chip->update_work,
				      OPLUS_CHG_UPDATE_INTERVAL(oplus_chg_update_slow(chip)));
	}
#else
	schedule_delayed_work(&chip->update_work, OPLUS_CHG_UPDATE_INTERVAL);
#endif
}
void oplus_chg_cancel_update_work_sync(void)
{
	if (!g_charger_chip) {
		return;
	}

	cancel_delayed_work_sync(&g_charger_chip->update_work);
}

void oplus_chg_restart_update_work(void)
{
	if (!g_charger_chip) {
		return;
	}

	schedule_delayed_work(&g_charger_chip->update_work, 0);
}
bool oplus_chg_wake_update_work(void)
{
	int shedule_work = 0;

	if (!g_charger_chip) {
		chg_err(" g_charger_chip NULL,return\n");
		return true;
	}
#ifdef OPLUS_CHG_OP_DEF
	if (!g_charger_chip->update_work.work.func || !g_charger_chip->update_work.timer.function) {
		chg_err("update_work not init\n");
		return true;
	}
#endif
	shedule_work = mod_delayed_work(system_wq, &g_charger_chip->update_work, 0);
	return true;
}

void oplus_chg_reset_adapter(void)
{
	if (!g_charger_chip) {
		return;
	}
	schedule_delayed_work(&g_charger_chip->reset_adapter_work, 0);
}

void oplus_chg_kick_wdt(void)
{
	if (!g_charger_chip) {
		return;
	}
	if (oplus_warp_get_allow_reading() == true) {
		g_charger_chip->chg_ops->kick_wdt();
	}
}

void oplus_chg_disable_charge(void)
{
	if (!g_charger_chip) {
		return;
	}
	if (oplus_warp_get_allow_reading() == true) {
		g_charger_chip->chg_ops->charging_disable();
	}
}

void oplus_chg_suspend_charger(void)
{
	if (!g_charger_chip) {
		return;
	}
	if (oplus_warp_get_allow_reading() == true) {
		g_charger_chip->chg_ops->charger_suspend();
	}
}

void oplus_chg_unsuspend_charger(void)
{
	if (!g_charger_chip) {
		return;
	}
	if (oplus_warp_get_allow_reading() == true) {
		g_charger_chip->chg_ops->charger_unsuspend();
	}
}

int oplus_chg_get_batt_volt(void)
{
	if (!g_charger_chip) {
		return 4000;
	} else {
		return g_charger_chip->batt_volt;
	}
}

int oplus_chg_get_cool_bat_decidegc(void)
{
	if (!g_charger_chip) {
		return -EINVAL;
	} else {
		return g_charger_chip->limits.cool_bat_decidegc;
	}
}

int oplus_chg_get_little_cool_bat_decidegc(void)
{
	if (!g_charger_chip) {
		return -EINVAL;
	} else {
		return g_charger_chip->limits.little_cool_bat_decidegc;
	}
}

int oplus_chg_get_normal_bat_decidegc(void)
{
	if (!g_charger_chip) {
		return -EINVAL;
	} else {
		return g_charger_chip->limits.normal_bat_decidegc;
	}
}

int oplus_chg_get_icharging(void)
{
	if (!g_charger_chip) {
		return 4000;
	} else {
		return g_charger_chip->icharging;
	}
}

int oplus_chg_get_ui_soc(void)
{
	if (!g_charger_chip) {
		return 50;
	} else {
		return g_charger_chip->ui_soc;
	}
}

int oplus_chg_get_soc(void)
{
	if (!g_charger_chip) {
		return 50;
	} else {
		return g_charger_chip->soc;
	}
}

void oplus_chg_soc_update_when_resume(unsigned long sleep_tm_sec)
{
	int new_soc;
	if (!g_charger_chip) {
		return;
	}
	g_charger_chip->sleep_tm_sec = sleep_tm_sec;
	new_soc = oplus_gauge_get_batt_soc();
	if(new_soc != g_charger_chip->soc){
		g_charger_chip->smooth_soc -= (g_charger_chip->soc - new_soc);
	}
	g_charger_chip->soc = new_soc;
	if(g_charger_chip->smooth_switch){
		oplus_chg_smooth_to_soc(g_charger_chip);
	}
	oplus_chg_update_ui_soc(g_charger_chip);
}

void oplus_chg_soc_update(void)
{
	if (!g_charger_chip) {
		return;
	}
	oplus_chg_update_ui_soc(g_charger_chip);
	oplus_chg_debug_set_soc_info(g_charger_chip);
}

int oplus_chg_get_chg_type(void)
{
	if (!g_charger_chip) {
		return POWER_SUPPLY_TYPE_UNKNOWN;
	} else {
		return g_charger_chip->charger_type;
	}
}

int oplus_chg_get_chg_temperature(void)
{
	if (!g_charger_chip) {
		return 250;
	} else {
		return g_charger_chip->temperature;
	}
}

int oplus_chg_get_notify_flag(void)
{
	if (!g_charger_chip) {
		return 0;
	} else {
		return g_charger_chip->notify_flag;
	}
}

int oplus_is_warp_project(void)
{
	if (!g_charger_chip) {
		return 0;
	} else {
		return g_charger_chip->warp_project;
	}
}

int oplus_chg_show_warp_logo_ornot(void)
{
#ifdef OPLUS_CHG_OP_DEF
#endif
	if (!g_charger_chip) {
		return 0;
	}
#ifndef CONFIG_OPLUS_CHARGER_MTK
#ifndef OPLUS_CHG_OP_DEF
	if (g_charger_chip->wireless_support
			&& oplus_wpc_get_adapter_type() == CHARGER_SUBTYPE_FASTCHG_SWARP
			&& g_charger_chip->prop_status != POWER_SUPPLY_STATUS_FULL
			&& (g_charger_chip->stop_voter == CHG_STOP_VOTER__FULL
			|| g_charger_chip->stop_voter == CHG_STOP_VOTER_NONE))
#else /* OPLUS_CHG_OP_DEF */
	if ((oplus_chg_get_wls_charge_type(g_charger_chip) == OPLUS_CHG_WLS_WARP
			|| oplus_chg_get_wls_charge_type(g_charger_chip) == OPLUS_CHG_WLS_SWARP
			|| oplus_chg_get_wls_charge_type(g_charger_chip) == OPLUS_CHG_WLS_PD_65W)
			&& g_charger_chip->prop_status != POWER_SUPPLY_STATUS_FULL
			&& (g_charger_chip->stop_voter == CHG_STOP_VOTER__FULL
			|| g_charger_chip->stop_voter == CHG_STOP_VOTER_NONE))
#endif /* OPLUS_CHG_OP_DEF */
		return 1;
#endif
	if (g_charger_chip->chg_ctrl_by_warp) {
		if (oplus_warp_get_fastchg_started() == true
				&& oplus_warp_get_fast_chg_type() == CHARGER_SUBTYPE_FASTCHG_WARP) {
			//chg_err("show_warp_logo_ornot by warp\n");
			if (g_charger_chip->prop_status != POWER_SUPPLY_STATUS_FULL
					&& (g_charger_chip->stop_voter == CHG_STOP_VOTER__FULL
					|| g_charger_chip->stop_voter == CHG_STOP_VOTER_NONE)) {
				return 1;
			} else {
				return 0;
			}
		}
	}
	if (oplus_warp_get_fastchg_started()) {
		return 1;
	} else if (oplus_warp_get_fastchg_to_normal() == true
			|| oplus_warp_get_fastchg_to_warm() == true
			|| oplus_warp_get_fastchg_dummy_started() == true
			|| oplus_warp_get_adapter_update_status() == ADAPTER_FW_NEED_UPDATE) {
		if (g_charger_chip->prop_status != POWER_SUPPLY_STATUS_FULL
				&&(g_charger_chip->stop_voter == CHG_STOP_VOTER__FULL
				|| g_charger_chip->stop_voter == CHG_STOP_VOTER_NONE)) {
			return 1;
		} else {
			return 0;
		}
	} else {
		return 0;
	}
}

bool get_otg_switch(void)
{
	if (!g_charger_chip) {
		return 0;
	} else {
		return g_charger_chip->otg_switch;
	}
}

bool oplus_chg_get_otg_online(void)
{
	if (!g_charger_chip) {
		return 0;
	} else {
		return g_charger_chip->otg_online;
	}
}

void oplus_chg_set_otg_online(bool online)
{
	if (g_charger_chip) {
		g_charger_chip->otg_online = online;
	}
}

bool oplus_chg_get_batt_full(void)
{
	if (!g_charger_chip) {
		return 0;
	} else {
		return g_charger_chip->batt_full;
	}
}

bool oplus_chg_get_rechging_status(void)
{
	if (!g_charger_chip) {
		return 0;
	} else {
		return g_charger_chip->in_rechging;
	}
}


bool oplus_chg_check_chip_is_null(void)
{
	if (!g_charger_chip) {
		return true;
	} else {
		return false;
	}
}

void oplus_chg_set_charger_type_unknown(void)
{
	if (g_charger_chip) {
		g_charger_chip->charger_type = POWER_SUPPLY_TYPE_UNKNOWN;
		g_charger_chip->real_charger_type = POWER_SUPPLY_TYPE_UNKNOWN;
	}
}

int oplus_chg_get_charger_voltage(void)
{
	if (!g_charger_chip) {
		return -EINVAL;
	} else {
		g_charger_chip->charger_volt = g_charger_chip->chg_ops->get_charger_volt();
	}
	return g_charger_chip->charger_volt;
}
#ifdef OPLUS_CHG_OP_DEF
int oplus_chg_get_vph_voltage(void)
{
	if (!g_charger_chip) {
		return -EINVAL;
	} else {
		g_charger_chip->vph_voltage = g_charger_chip->chg_ops->get_vph_volt();
	}
	return g_charger_chip->vph_voltage;
}
int oplus_chg_get_hw_detect_status(void)
{
	if (!g_charger_chip) {
		return 0;
	} else {
		g_charger_chip->hw_detected = g_charger_chip->chg_ops->get_hw_detect();
	}
	return g_charger_chip->hw_detected;
}
#endif
void oplus_chg_set_chargerid_switch_val(int value)
{
	if (g_charger_chip && g_charger_chip->chg_ops->set_chargerid_switch_val) {
		g_charger_chip->chg_ops->set_chargerid_switch_val(value);
	}
}

void oplus_chg_clear_chargerid_info(void)
{
	if (g_charger_chip && g_charger_chip->chg_ops->set_chargerid_switch_val) {
		g_charger_chip->chargerid_volt = 0;
		g_charger_chip->chargerid_volt_got = false;
	}
}

int oplus_chg_get_cool_down_status(void)
{
	int ret = 0;
	struct oplus_chg_chip *chip = g_charger_chip;

	if (!chip) {
		return 0;
	}

	if (chip->smart_charge_user == SMART_CHARGE_USER_USBTEMP) {
		if (chip->smart_charging_screenoff) {
			chip->usbtemp_cool_down = oplus_convert_current_to_level(chip, USBTEMP_CHARGING_CURRENT_LIMIT);
		} else {
			chip->usbtemp_cool_down = 3;
		}
		if (chip->cool_down) {
			ret = (chip->usbtemp_cool_down < chip->cool_down) ? chip->usbtemp_cool_down : chip->cool_down;
		} else {
			ret = chip->usbtemp_cool_down;
		}
		charger_xlog_printk(CHG_LOG_CRTI, "cool_down=%d,usbtemp_cool_down=%d\n", chip->cool_down, chip->usbtemp_cool_down);
	} else {
		ret = chip->cool_down;
	}

	return ret;
}

struct current_level {
	int level;
	int icharging;
};

struct current_level SWARP_2_0[] = {
	{1, 1000}, {2, 1500}, {3, 2000}, {4, 2500}, {5, 3000},
	{6, 3500}, {7, 4000}, {8, 4500}, {9, 5000}, {10, 5500},
	{11, 6000}, {12, 6300}, {13, 6500}, {14, 7000}, {15, 7500},
	{16, 8000}, {17, 8500}, {18, 9000}, {19, 9500}, {20, 10000},
	{21, 10500}, {22, 11000}, {23, 11500}, {24, 12000}, {25, 12500},
};

struct current_level SWARP_1_0[] = {
	{1, 2000}, {2, 3000}, {3, 4000}, {4, 5000}, {5, 6000},
	{6, 7000}, {7, 8000}, {8, 9000}, {9, 10000}, {10, 1200},
	{11, 12000}, {12, 12600}
};

int find_level_to_current(int val, struct current_level *table, int len)
{
	int i = 0;
	bool find_out_flag = false;

	for(i = 0; i < len; i++) {
		if(table[i].icharging == val) {
			find_out_flag = true;
			break;
		}
		find_out_flag = false;
	}
	if(find_out_flag == true) {
		return table[i].level;
	} else {
		return 0;
	}
}

int oplus_convert_current_to_level(struct oplus_chg_chip *chip, int val)
{
	int level = 1, reply_bits = 0;
	reply_bits = oplus_warp_get_reply_bits();
	if(!val || !reply_bits) {
		return level;
	}
	if(reply_bits == 7) {
		if(chip->vbatt_num == 2) {
			level = find_level_to_current(val, SWARP_2_0, ARRAY_SIZE(SWARP_2_0));
		} else {
			level = find_level_to_current(val, SWARP_1_0, ARRAY_SIZE(SWARP_1_0));
		}
	} else {
		level = val / 1000;
	}
	return level;
}

#define BATT_NTC_CTRL_THRESHOLD_LOW -100
#define BATT_NTC_CTRL_THRESHOLD_HIGH 600
int oplus_chg_override_by_shell_temp(int temp)
{
	if (!g_charger_chip) {
		return 0;
	}
	if (oplus_is_power_off_charging(NULL)) {
		return 0;
	}
	if ((g_charger_chip->charger_exist) && g_charger_chip->smart_charging_screenoff &&
			(temp > BATT_NTC_CTRL_THRESHOLD_LOW) && (temp < BATT_NTC_CTRL_THRESHOLD_HIGH)) {
		charger_xlog_printk(CHG_LOG_CRTI, "charging override by shell temperature\n");
		return 1;
	}
	return 0;
}

int oplus_chg_get_shell_temp(void) {
	int temp_val = 0, rc = -EINVAL;

	if (!g_charger_chip) {
		return TEMPERATURE_INVALID;
	}

	rc = thermal_zone_get_temp(g_charger_chip->shell_themal, &temp_val);
	if (rc) {
		chg_err("thermal_zone_get_temp get error");
		return g_charger_chip->shell_temp;
	}
	g_charger_chip->shell_temp = temp_val / 100;
	return g_charger_chip->shell_temp;
}

void oplus_smart_charge_by_shell_temp(struct oplus_chg_chip *chip, int val) {
	union power_supply_propval pval = {0, };
#ifndef CONFIG_OPLUS_CHG_OOS
#ifdef OPLUS_CHG_OP_DEF
	union oplus_chg_mod_propval opval = {0, };
#endif
#endif
	int subtype = 0, rc = -EINVAL;

	if (!chip) {
		return;
	}

	if (chip->shell_themal) {
		rc = thermal_zone_get_temp(chip->shell_themal, &chip->shell_temp);
		if (rc) {
			chip->shell_temp = (val >> 16) & 0XFFFF;
			chg_err("thermal_zone_get_temp get error");
		} else {
			chip->shell_temp = chip->shell_temp / 100;
		}
	} else {
		chip->shell_temp = (val >> 16) & 0XFFFF;
	}
	val = val & 0XFFFF;

	charger_xlog_printk(CHG_LOG_CRTI, "val->intval = [%04x], shell_temp = [%04x], set shell_temp_down = [%d].\n", val, chip->shell_temp, chip->cool_down);
	if (chip->led_on) {
		return;
	}
	if (!val) {
		if(chip->cool_down != 0){
			chip->cool_down = 0;
			chip->limits.input_current_charger_ma = chip->limits.default_input_current_charger_ma;
			chip->limits.input_current_warp_ma_high = chip->limits.default_input_current_warp_ma_high;
			chip->limits.input_current_warp_ma_warm = chip->limits.default_input_current_warp_ma_warm;
			chip->limits.input_current_warp_ma_normal = chip->limits.default_input_current_warp_ma_normal;
			chip->limits.pd_input_current_charger_ma = chip->limits.default_pd_input_current_charger_ma;
			chip->limits.qc_input_current_charger_ma = chip->limits.default_qc_input_current_charger_ma;
			chip->cool_down_done = true;
			chip->cool_down_force_5v = false;
			chip->chg_ctrl_by_cool_down = false;
		}
		return;
	}
#ifndef CONFIG_OPLUS_CHG_OOS
#ifndef OPLUS_CHG_OP_DEF
	rc = power_supply_get_property(chip->usb_psy, POWER_SUPPLY_PROP_FAST_CHG_TYPE, &pval);
	if (rc < 0) {
		pr_err("Failed to read charging subtype\n");
		return;
	}
#else
	rc = oplus_chg_mod_get_property(chip->usb_ocm, OPLUS_CHG_PROP_FAST_CHG_TYPE, &opval);
	if (rc < 0) {
		pr_err("Failed to read charging subtype\n");
		return;
	}
	pval.intval = opval.intval;
#endif
	pval.intval = CHARGER_SUBTYPE_DEFAULT;
#endif
	subtype = pval.intval;
	charger_xlog_printk(CHG_LOG_CRTI, "get subtype pval->intval = [%d]\n", pval.intval);
	switch(pval.intval) {
	case CHARGER_SUBTYPE_DEFAULT:
		break;
	case CHARGER_SUBTYPE_FASTCHG_WARP:
		chip->limits.input_current_warp_ma_high = val;
		chip->limits.input_current_warp_ma_warm = val;
		chip->limits.input_current_warp_ma_normal = val;
		chip->limits.input_current_cool_down_ma = val;
		chip->cool_down_done = true;
		chip->cool_down_force_5v = true;
		chip->chg_ctrl_by_cool_down = true;
		break;
	case CHARGER_SUBTYPE_FASTCHG_SWARP:
		/*chip->cool_down = val / 1000;*/
		/*for 7bits 25 current steps*/
		chip->cool_down = oplus_convert_current_to_level(chip, val);
		break;
	case CHARGER_SUBTYPE_PD:
		chip->limits.input_current_cool_down_ma = val;
		chip->limits.pd_input_current_charger_ma = val;
		chip->limits.qc_input_current_charger_ma = val;
		chip->cool_down_done = true;
		chip->cool_down_force_5v = false;
		chip->chg_ctrl_by_cool_down = true;
		break;
	case CHARGER_SUBTYPE_QC:
		chip->limits.input_current_cool_down_ma = val;
		chip->limits.pd_input_current_charger_ma = val;
		chip->limits.qc_input_current_charger_ma = val;
		chip->cool_down_done = true;
		chip->cool_down_force_5v = false;
		chip->chg_ctrl_by_cool_down = true;
		break;
	default:
		if (pval.intval > 10) {
			chip->cool_down = oplus_convert_current_to_level(chip, val);
		}
		break;
	}
}

static const int cool_down_current_limit_normal[6] = {1500, 1500, 1500, 1500, 2000, 2000};
static const int cool_down_current_limit_warp[6] = {1000, 1500, 2000, 2500, 3000, 3500};
void oplus_smart_charge_by_cool_down(struct oplus_chg_chip *chip, int val)
{
	OPLUS_CHARGER_SUBTYPE esubtype = CHARGER_SUBTYPE_DEFAULT;

	if (!chip) {
		return;
	}

	if (!val) {
		chip->cool_down = 0;
		esubtype = chip->chg_ops->get_charger_subtype();
		if (CHARGER_SUBTYPE_PD == esubtype)
			chip->limits.input_current_charger_ma = chip->limits.default_pd_input_current_charger_ma;
		else if (CHARGER_SUBTYPE_QC == esubtype)
			chip->limits.input_current_charger_ma = chip->limits.default_qc_input_current_charger_ma;
		else
			chip->limits.input_current_charger_ma = chip->limits.default_input_current_charger_ma;
		chip->limits.input_current_warp_ma_high = chip->limits.default_input_current_warp_ma_high;
		chip->limits.input_current_warp_ma_warm = chip->limits.default_input_current_warp_ma_warm;
		chip->limits.input_current_warp_ma_normal = chip->limits.default_input_current_warp_ma_normal;
		chip->limits.pd_input_current_charger_ma = chip->limits.default_pd_input_current_charger_ma;
		chip->limits.qc_input_current_charger_ma = chip->limits.default_qc_input_current_charger_ma;
		chip->cool_down_done = true;
		charger_xlog_printk(CHG_LOG_CRTI, "val->intval = [%04x], set cool_down = [%d].\n", val, chip->cool_down);
		return;
	}
	chip->cool_down = val;
	if (val > 6) {
		chip->limits.input_current_charger_ma = cool_down_current_limit_normal[6 - 1];
		chip->limits.pd_input_current_charger_ma = cool_down_current_limit_normal[6 - 1];
		chip->limits.qc_input_current_charger_ma = cool_down_current_limit_normal[6 - 1];
		chip->limits.input_current_warp_ma_high = cool_down_current_limit_warp[6 - 1];
		chip->limits.input_current_warp_ma_warm = cool_down_current_limit_warp[6 - 1];
		chip->limits.input_current_warp_ma_normal = cool_down_current_limit_warp[6 - 1];
		chip->cool_down_done = true;
	} else {
		chip->limits.input_current_charger_ma = cool_down_current_limit_normal[val - 1];
		chip->limits.pd_input_current_charger_ma = cool_down_current_limit_normal[val - 1];
		chip->limits.qc_input_current_charger_ma = cool_down_current_limit_normal[val - 1];
		chip->limits.input_current_warp_ma_high = cool_down_current_limit_warp[val - 1];
		chip->limits.input_current_warp_ma_warm = cool_down_current_limit_warp[val - 1];
		chip->limits.input_current_warp_ma_normal = cool_down_current_limit_warp[val - 1];
		chip->cool_down_done = true;
	}

	charger_xlog_printk(CHG_LOG_CRTI, "val->intval = [%04x], set cool_down = [%d].\n", val, chip->cool_down);
}

int oplus_is_rf_ftm_mode(void)
{
	int boot_mode = get_boot_mode();
#ifdef CONFIG_OPLUS_CHARGER_MTK
	if (boot_mode == META_BOOT || boot_mode == FACTORY_BOOT
			|| boot_mode == ADVMETA_BOOT || boot_mode == ATE_FACTORY_BOOT) {
		chg_debug(" boot_mode:%d, return\n",boot_mode);
		return true;
	} else {
		chg_debug(" boot_mode:%d, return false\n",boot_mode);
		return false;
	}
#else
	if(boot_mode == MSM_BOOT_MODE__RF || boot_mode == MSM_BOOT_MODE__WLAN
			|| boot_mode == MSM_BOOT_MODE__FACTORY){
		chg_debug(" boot_mode:%d, return\n",boot_mode);
		return true;
	} else {
		chg_debug(" boot_mode:%d, return false\n",boot_mode);
		return false;
	}
#endif
}

#ifdef CONFIG_OPLUS_CHARGER_MTK
/*
int get_oplus_short_check_fast_to_normal(void)
{
	if (!g_charger_chip) {
		return 0;
	} else {
		return g_charger_chip->short_check_fast_to_normal_flag;
	}

}
*/
int oplus_get_prop_status(void)
{
	if (!g_charger_chip) {
		return 0;
	} else {
		return g_charger_chip->prop_status;
	}
}
#endif

#define OPLUS_TBATT_HIGH_PWROFF_COUNT	(18)
#define OPLUS_TBATT_EMERGENCY_PWROFF_COUNT	(6)

DECLARE_WAIT_QUEUE_HEAD(oplus_tbatt_pwroff_wq);

static int oplus_tbatt_power_off_kthread(void *arg)
{
	int over_temp_count = 0, emergency_count = 0;
	int batt_temp = 0;
	//struct oplus_chg_chip *chip = (struct oplus_chg_chip *)arg;
	struct sched_param param = {.sched_priority = MAX_RT_PRIO-1};

	sched_setscheduler(current, SCHED_FIFO, &param);
	tbatt_pwroff_enable = 1;

	while (!kthread_should_stop()) {
		schedule_timeout_interruptible(round_jiffies_relative(msecs_to_jiffies(5*1000)));
		//chg_err(" tbatt_pwroff_enable:%d over_temp_count[%d] start\n", tbatt_pwroff_enable, over_temp_count);
		if (!tbatt_pwroff_enable) {
			emergency_count = 0;
			over_temp_count = 0;
			wait_event_interruptible(oplus_tbatt_pwroff_wq, tbatt_pwroff_enable == 1);
		}
		if (oplus_warp_get_fastchg_started() == true) {
			batt_temp = oplus_gauge_get_prev_batt_temperature();
		} else {
			batt_temp = oplus_gauge_get_batt_temperature();
		}
		if (batt_temp > OPCHG_PWROFF_EMERGENCY_BATT_TEMP) {
			emergency_count++;
			chg_err(" emergency_count:%d \n", emergency_count);
		} else {
			emergency_count = 0;
		}
		if (batt_temp > OPCHG_PWROFF_HIGH_BATT_TEMP) {
			over_temp_count++;
			chg_err("over_temp_count[%d] \n", over_temp_count);
		} else {
			over_temp_count = 0;
		}
		//chg_err("over_temp_count[%d], chip->temperature[%d]\n", over_temp_count, batt_temp);
		if (get_eng_version() != HIGH_TEMP_AGING) {
			if (over_temp_count >= OPLUS_TBATT_HIGH_PWROFF_COUNT
				|| emergency_count >= OPLUS_TBATT_EMERGENCY_PWROFF_COUNT) {
				chg_err("ERROR: battery temperature is too high, goto power off\n");
				/*msleep(1000);*/
#ifndef CONFIG_OPLUS_CHG_OOS
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))
				machine_power_off();
#else
				kernel_power_off();
#endif
#endif
			}
		}
	}
	return 0;
}

int oplus_tbatt_power_off_task_init(struct oplus_chg_chip *chip)
{
	if (!chip) {
		return -1;
	}
	chip->tbatt_pwroff_task
		= kthread_create(oplus_tbatt_power_off_kthread, chip, "tbatt_pwroff");
	if(chip->tbatt_pwroff_task){
		wake_up_process(chip->tbatt_pwroff_task);
	}else{
		chg_err("ERROR: chip->tbatt_pwroff_task creat fail\n");
		return -1;
	}
	return 0;
}

void oplus_tbatt_power_off_task_wakeup(void)
{
	wake_up_interruptible(&oplus_tbatt_pwroff_wq);
	return;
}

bool oplus_get_chg_powersave(void)
{
	if (!g_charger_chip) {
		return false;
	} else {
		return g_charger_chip->chg_powersave;
	}
}

int oplus_get_chg_unwakelock(void)
{
	if (!g_charger_chip) {
                return 0;
	} else {
		return g_charger_chip->unwakelock_chg;
	}
}

bool oplus_get_vbatt_higherthan_xmv(void)
{
        if (!g_charger_chip) {
                return false;
        } else {
                return vbatt_higherthan_4180mv;
        }
}

void oplus_chg_set_input_current_without_aicl(int current_ma)
{
    if (!g_charger_chip) {
        return ;
    } else {
        if(g_charger_chip->chg_ops->input_current_write_without_aicl && oplus_warp_get_allow_reading() == true) {
            g_charger_chip->chg_ops->input_current_write_without_aicl(current_ma);
            chg_err("current_ma[%d]\n", current_ma);
        }
    }
}

void oplus_chg_config_charger_vsys_threshold(int val)
{
    if (!g_charger_chip) {
        return ;
    } else {
        if(g_charger_chip->chg_ops->set_charger_vsys_threshold && oplus_warp_get_allow_reading() == true) {
            g_charger_chip->chg_ops->set_charger_vsys_threshold(val);
            chg_err("set val[%d]\n", val);
        }
    }
}

void oplus_chg_enable_burst_mode(bool enable)
{
    if (!g_charger_chip) {
        return ;
    } else {
        if(g_charger_chip->chg_ops->enable_burst_mode && oplus_warp_get_allow_reading() == true) {
            g_charger_chip->chg_ops->enable_burst_mode(enable);
            chg_err("set val[%d]\n", enable);
        }
    }
}

int oplus_chg_get_tbatt_status(void)
{
    if (!g_charger_chip) {
        return 0;
    } else {
		return g_charger_chip->tbatt_status;
    }
}

#ifdef OPLUS_CHG_OP_DEF
struct oplus_chg_chip *oplus_chg_get_chg_struct(void)
{
	return g_charger_chip;
}

void oplus_chg_update_float_voltage_by_fastchg(bool fastchg_en)
{
	int flv = 4650;

	if (!g_charger_chip) {
		chg_err("g_charger_chip not found\n");
		return;
	}

	if (fastchg_en) {
		g_charger_chip->chg_ops->float_voltage_write(flv * g_charger_chip->vbatt_num);
		g_charger_chip->limits.vfloat_sw_set = flv;
	} else {
		oplus_chg_set_float_voltage(g_charger_chip);
	}
}
#endif

int oplus_chg_match_temp_for_chging(void)
{
	int batt_temp = 0;
#ifndef OPLUS_CHG_OP_DEF
	int shell_temp = 0;
	int diff = 0;
#endif
	int chging_temp = 0;

	if (!g_charger_chip) {
		return chging_temp;
	}
	if (oplus_warp_get_fastchg_started() == true) {
		batt_temp = oplus_gauge_get_prev_batt_temperature();
	} else {
		batt_temp = oplus_gauge_get_batt_temperature();
	}
#ifndef OPLUS_CHG_OP_DEF
	if (oplus_chg_override_by_shell_temp(batt_temp)) {
		shell_temp = oplus_chg_get_shell_temp();
		diff = shell_temp - batt_temp;
		if(diff < 150 && diff > -150  && batt_temp >= 320) {
			chging_temp = shell_temp;
		} else {
			chging_temp = batt_temp;
		}
	} else {
		chging_temp = batt_temp;
	}
#else
	chging_temp = batt_temp;
#endif
	g_charger_chip->tbatt_temp = batt_temp;

	return chging_temp;
}

