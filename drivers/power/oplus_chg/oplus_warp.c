// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */
#include <linux/delay.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>

#include "oplus_charger.h"
#include "oplus_warp.h"
#include "oplus_gauge.h"
#include "oplus_adapter.h"
#include "oplus_debug_info.h"
#ifdef OPLUS_CHG_OP_DEF
#include "oplus_op_def.h"
#include "warp_ic/oplus_warp_fw.h"
#endif

#define WARP_NOTIFY_FAST_PRESENT			0x52
#define WARP_NOTIFY_FAST_ABSENT				0x54
#define WARP_NOTIFY_ALLOW_READING_IIC		0x58
#define WARP_NOTIFY_NORMAL_TEMP_FULL		0x5a
#define WARP_NOTIFY_LOW_TEMP_FULL			0x53
#define WARP_NOTIFY_DATA_UNKNOWN			0x55
#define WARP_NOTIFY_FIRMWARE_UPDATE			0x56
#define WARP_NOTIFY_BAD_CONNECTED			0x59
#define WARP_NOTIFY_TEMP_OVER				0x5c
#define WARP_NOTIFY_ADAPTER_FW_UPDATE		0x5b
#define WARP_NOTIFY_BTB_TEMP_OVER			0x5d
#define WARP_NOTIFY_ADAPTER_MODEL_FACTORY	0x5e

#define WARP_TEMP_RANGE_THD					10

extern int charger_abnormal_log;
extern int enable_charger_log;
#define warp_xlog_printk(num, fmt, ...) \
	do { \
		if (enable_charger_log >= (int)num) { \
			printk(KERN_NOTICE pr_fmt("[OPLUS_CHG][%s]"fmt), __func__, ##__VA_ARGS__);\
	} \
} while (0)

#ifndef OPLUS_CHG_OP_DEF
static struct oplus_warp_chip *g_warp_chip = NULL;
#else
struct oplus_warp_chip *g_warp_chip = NULL;
int oplus_warp_probe_done = -EPROBE_DEFER;
#endif
bool __attribute__((weak)) oplus_get_fg_i2c_err_occured(void)
{
	return false;
}

void __attribute__((weak)) oplus_set_fg_i2c_err_occured(bool i2c_err)
{
	return;
}
int __attribute__((weak)) request_firmware_select(const struct firmware **firmware_p,
		const char *name, struct device *device)
{
	return 1;
}
int __attribute__((weak)) register_devinfo(char *name, struct manufacture_info *info)
{
	return 1;
}
#ifndef OPLUS_CHG_OP_DEF
static int oplus_warp_convert_fast_chg_type(int fast_chg_type);
#endif

#ifdef OPLUS_CHG_OP_DEF
static oplus_chg_swarp_curr_table[CURR_LIMIT_MAX] = {2500, 2000, 3000, 4000, 5000, 6500};
static oplus_chg_warp_curr_table[CURR_LIMIT_MAX] = {3600, 2500, 3000, 4000, 5000, 6000};

static struct oplus_adapter_struct adapter_id_table[] = {
	{ 0x11, 0x11, 25, 50, ADAPTER_TYPE_AC,      CHARGER_TYPE_SWARP },
	{ 0x12, 0x12, 25, 50, ADAPTER_TYPE_AC,      CHARGER_TYPE_SWARP },
	{ 0x13, 0x13, 20, 0,  ADAPTER_TYPE_AC,      CHARGER_TYPE_WARP },
	{ 0x14, 0x14, 30, 65, ADAPTER_TYPE_AC,      CHARGER_TYPE_SWARP },
	{ 0x19, 0x19, 30, 0,  ADAPTER_TYPE_AC,      CHARGER_TYPE_WARP },
	{ 0x21, 0x21, 25, 50, ADAPTER_TYPE_CAR,     CHARGER_TYPE_SWARP },
	{ 0x29, 0x29, 30, 0,  ADAPTER_TYPE_CAR,     CHARGER_TYPE_WARP },
	{ 0x31, 0x31, 25, 50, ADAPTER_TYPE_PB,      CHARGER_TYPE_SWARP },
	{ 0x32, 0x32, 0,  0,  ADAPTER_TYPE_PB,      CHARGER_TYPE_UNKNOWN },
	{ 0x33, 0x33, 25, 50, ADAPTER_TYPE_PB,      CHARGER_TYPE_SWARP },
	{ 0x34, 0x34, 20, 20, ADAPTER_TYPE_PB,      CHARGER_TYPE_NORMAL },
	{ 0x35, 0x35, 0,  0,  ADAPTER_TYPE_PB,      CHARGER_TYPE_NORMAL },
	{ 0x36, 0x36, 0,  0,  ADAPTER_TYPE_PB,      CHARGER_TYPE_NORMAL },
	{ 0x41, 0x41, 30, 0,  ADAPTER_TYPE_AC,      CHARGER_TYPE_WARP },
	{ 0x42, 0x46, 0,  0,  ADAPTER_TYPE_UNKNOWN, CHARGER_TYPE_WARP },
	{ 0x49, 0x4e, 0,  0,  ADAPTER_TYPE_UNKNOWN, CHARGER_TYPE_WARP },
	{ 0x62, 0x66, 0,  0,  ADAPTER_TYPE_UNKNOWN, CHARGER_TYPE_SWARP },
	{ 0x61, 0x61, 30, 65, ADAPTER_TYPE_AC,      CHARGER_TYPE_SWARP },
	{ 0x69, 0x6e, 0,  0,  ADAPTER_TYPE_UNKNOWN, CHARGER_TYPE_SWARP },
	{ },
};

static unsigned int oplus_get_adapter_sid(unsigned char id)
{
	struct oplus_adapter_struct *adapter_info;
	unsigned int sid;

	adapter_info = adapter_id_table;
	while (adapter_info->id_min != 0 && adapter_info->id_max != 0) {
	 	if (adapter_info->id_min > adapter_info->id_max) {
			adapter_info++;
			continue;
		}
		if (id >= adapter_info->id_min && id <= adapter_info->id_max) {
			sid = adapter_info_to_sid(id, adapter_info->power_warp,
				adapter_info->power_swarp, adapter_info->adapter_type,
				adapter_info->adapter_chg_type);
			pr_info("sid = 0x%08x\n", sid);
			return sid;
		}
		adapter_info++;
	}

	chg_err("unsupported adapter ID\n");
	return 0;
}
#endif

static bool oplus_warp_is_battemp_exit(void)
{
	int temp;
	bool high_temp = false, low_temp = false;
	bool status = false;

	temp = oplus_chg_match_temp_for_chging();
	if((g_warp_chip->warp_batt_over_high_temp != -EINVAL) &&  (g_warp_chip->warp_batt_over_low_temp != -EINVAL)){
		high_temp = (temp > g_warp_chip->warp_batt_over_high_temp);
		low_temp = (temp < g_warp_chip->warp_batt_over_low_temp);
		status = (g_warp_chip->fastchg_batt_temp_status == BAT_TEMP_EXIT);

		return ((high_temp || low_temp) && status);
	}else
		return false;
}

void oplus_warp_battery_update()
{
	struct oplus_warp_chip *chip = g_warp_chip;
/*
		if (!chip) {
			chg_err("  g_warp_chip is NULL\n");
			return ;
		}
*/
	if (!chip->batt_psy) {
		chip->batt_psy = power_supply_get_by_name("battery");
	}
	if (chip->batt_psy) {
		power_supply_changed(chip->batt_psy);
	}
}

void oplus_warp_switch_mode(int mode)
{
	if (!g_warp_chip || !g_warp_chip->vops) {
		chg_err("  g_warp_chip is NULL\n");
	} else {
		g_warp_chip->vops->set_switch_mode(g_warp_chip, mode);
	}
}

static void oplus_warp_awake_init(struct oplus_warp_chip *chip)
{
	if (!chip) {
		return;
	}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0))
	wake_lock_init(&chip->warp_wake_lock, WAKE_LOCK_SUSPEND, "warp_wake_lock");
#elif (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 102) && LINUX_VERSION_CODE > KERNEL_VERSION(4, 14, 999))
	chip->warp_ws = wakeup_source_register("warp_wake_lock");
#else
	chip->warp_ws = wakeup_source_register(NULL, "warp_wake_lock");
#endif
}

static void oplus_warp_set_awake(struct oplus_warp_chip *chip, bool awake)
{
	static bool pm_flag = false;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0))
	if(!chip) {
		return;
	}
	if (awake && !pm_flag) {
		pm_flag = true;
		wake_lock(&chip->warp_wake_lock);
	} else if (!awake && pm_flag)  {
		wake_unlock(&chip->warp_wake_lock);
		pm_flag = false;
	}
#else
	if (!chip || !chip->warp_ws) {
		return;
	}
	if (awake && !pm_flag) {
		pm_flag = true;
		__pm_stay_awake(chip->warp_ws);
	} else if (!awake && pm_flag) {
		__pm_relax(chip->warp_ws);
		pm_flag = false;
	}
#endif
}
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0))
static void oplus_warp_watchdog(unsigned long data)
#else
static void oplus_warp_watchdog(struct timer_list *unused)
#endif
{
	struct oplus_warp_chip *chip = g_warp_chip;

	if (!chip) {
		chg_err(" g_warp_chip is NULL\n");
		return;
	}
	chg_err("watchdog bark: cannot receive mcu data\n");
	chip->allow_reading = true;
	chip->fastchg_started = false;
	chip->fastchg_ing = false;
	chip->fastchg_to_normal = false;
	chip->ffc_chg_start = false;
#ifdef OPLUS_CHG_OP_DEF
	chip->fastchg_ignore_event = false;
#endif
	chip->adapter_sid = 0;
	chip->fastchg_to_warm = false;
	chip->fastchg_low_temp_full = false;
	chip->btb_temp_over = false;
	chip->fast_chg_type = FASTCHG_CHARGER_TYPE_UNKOWN;
	charger_abnormal_log = CRITICAL_LOG_WARP_WATCHDOG;
	schedule_work(&chip->warp_watchdog_work);
}

static void oplus_warp_init_watchdog_timer(struct oplus_warp_chip *chip)
{
	if (!chip) {
		chg_err("oplus_warp_chip is not ready\n");
		return;
	}
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0))
	init_timer(&chip->watchdog);
	chip->watchdog.data = (unsigned long)chip;
	chip->watchdog.function = oplus_warp_watchdog;
#else
	timer_setup(&chip->watchdog, oplus_warp_watchdog, 0);
#endif
}

static void oplus_warp_del_watchdog_timer(struct oplus_warp_chip *chip)
{
	if (!chip) {
		chg_err("oplus_warp_chip is not ready\n");
		return;
	}
	del_timer(&chip->watchdog);
}

static void oplus_warp_setup_watchdog_timer(struct oplus_warp_chip *chip, unsigned int ms)
{
	if (!chip) {
		chg_err("oplus_warp_chip is not ready\n");
		return;
	}
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0))
	mod_timer(&chip->watchdog, jiffies+msecs_to_jiffies(25000));
#else
    del_timer(&chip->watchdog);
    chip->watchdog.expires  = jiffies + msecs_to_jiffies(ms);
    add_timer(&chip->watchdog);
#endif
}

static void check_charger_out_work_func(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_warp_chip *chip = container_of(dwork, struct oplus_warp_chip, check_charger_out_work);
	int chg_vol = 0;

	if (!chip->vops) {
		chg_err("vops is null\n");
		return;
	}

	chg_vol = oplus_chg_get_charger_voltage();
	if (chg_vol >= 0 && chg_vol < 2000) {
		chip->vops->reset_fastchg_after_usbout(chip);
		oplus_chg_clear_chargerid_info();
		oplus_warp_battery_update();
		oplus_warp_reset_temp_range(chip);
		warp_xlog_printk(CHG_LOG_CRTI, "charger out, chg_vol:%d\n", chg_vol);
	}
}

static void warp_watchdog_work_func(struct work_struct *work)
{
	struct oplus_warp_chip *chip = container_of(work,
		struct oplus_warp_chip, warp_watchdog_work);
	if (!chip->vops) {
		chg_err("vops is null\n");
		return;
	}

	oplus_chg_set_chargerid_switch_val(0);
	oplus_chg_clear_chargerid_info();
	chip->vops->set_switch_mode(chip, NORMAL_CHARGER_MODE);
	oplus_warp_set_mcu_sleep();
	oplus_chg_set_charger_type_unknown();
	oplus_warp_set_awake(chip, false);
	oplus_warp_reset_temp_range(chip);
}

static void oplus_warp_check_charger_out(struct oplus_warp_chip *chip)
{
	warp_xlog_printk(CHG_LOG_CRTI, "  call\n");
	schedule_delayed_work(&chip->check_charger_out_work,
		round_jiffies_relative(msecs_to_jiffies(3000)));
}

int multistepCurrent[] = {1500, 2000, 3000, 4000, 5000, 6000};

#define WARP_TEMP_OVER_COUNTS	2

static int oplus_warp_set_current_1_temp_normal_range(struct oplus_warp_chip *chip, int vbat_temp_cur)
{
	int ret = 0;

	chip->warp_temp_cur_range = FASTCHG_TEMP_RANGE_NORMAL_HIGH;

	switch (chip->fastchg_batt_temp_status) {
	case BAT_TEMP_NORMAL_HIGH:
		if (vbat_temp_cur > chip->warp_strategy1_batt_high_temp0) {
			chip->fastchg_batt_temp_status = BAT_TEMP_HIGH0;
			ret = chip->warp_strategy1_high_current0;
		} else if (vbat_temp_cur >= chip->warp_normal_low_temp) {
			chip->fastchg_batt_temp_status = BAT_TEMP_NORMAL_HIGH;
			ret = chip->warp_strategy_normal_current;
		} else {
			chip->fastchg_batt_temp_status = BAT_TEMP_NORMAL_LOW;
			chip->warp_temp_cur_range = FASTCHG_TEMP_RANGE_NORMAL_LOW;
			ret = chip->warp_strategy_normal_current;
			oplus_warp_reset_temp_range(chip);
			chip->warp_normal_low_temp += WARP_TEMP_RANGE_THD;
		}
		break;
	case BAT_TEMP_HIGH0:
		if (vbat_temp_cur > chip->warp_strategy1_batt_high_temp1) {
			chip->fastchg_batt_temp_status = BAT_TEMP_HIGH1;
			ret = chip->warp_strategy1_high_current1;
		} else if (vbat_temp_cur < chip->warp_strategy1_batt_low_temp0) {
			chip->fastchg_batt_temp_status = BAT_TEMP_LOW0;
			ret = chip->warp_strategy1_low_current0;
		} else {
			chip->fastchg_batt_temp_status = BAT_TEMP_HIGH0;
			ret = chip->warp_strategy1_high_current0;
		}
		break;
	case BAT_TEMP_HIGH1:
		if (vbat_temp_cur > chip->warp_strategy1_batt_high_temp2) {
			chip->fastchg_batt_temp_status = BAT_TEMP_HIGH2;
			ret = chip->warp_strategy1_high_current2;
		} else if (vbat_temp_cur < chip->warp_strategy1_batt_low_temp1) {
			chip->fastchg_batt_temp_status = BAT_TEMP_LOW1;
			ret = chip->warp_strategy1_low_current1;
		} else {
			chip->fastchg_batt_temp_status = BAT_TEMP_HIGH1;
			ret = chip->warp_strategy1_high_current1;
		}
		break;
	case BAT_TEMP_HIGH2:
		if (chip->warp_batt_over_high_temp != -EINVAL
				&& vbat_temp_cur > chip->warp_batt_over_high_temp) {
			chip->warp_strategy_change_count++;
			if (chip->warp_strategy_change_count >= WARP_TEMP_OVER_COUNTS) {
				chip->warp_strategy_change_count = 0;
				chip->fastchg_batt_temp_status = BAT_TEMP_EXIT;
				ret = chip->warp_over_high_or_low_current;
			}
		} else if (vbat_temp_cur < chip->warp_strategy1_batt_low_temp2) {
			chip->fastchg_batt_temp_status = BAT_TEMP_LOW2;
			ret = chip->warp_strategy1_low_current2;
		} else {
			chip->fastchg_batt_temp_status = BAT_TEMP_HIGH2;
			ret = chip->warp_strategy1_high_current2;;
		}
		break;
	case BAT_TEMP_LOW0:
		if (vbat_temp_cur > chip->warp_strategy1_batt_high_temp0) {
			chip->fastchg_batt_temp_status = BAT_TEMP_HIGH0;
			ret = chip->warp_strategy1_high_current0;
		} else if (vbat_temp_cur < chip->warp_normal_low_temp) {/*T<25C*/
			chip->fastchg_batt_temp_status = BAT_TEMP_NORMAL_LOW;
			chip->warp_temp_cur_range = FASTCHG_TEMP_RANGE_NORMAL_LOW;
			ret = chip->warp_strategy_normal_current;
			oplus_warp_reset_temp_range(chip);
			chip->warp_normal_low_temp += WARP_TEMP_RANGE_THD;
		} else {
			chip->fastchg_batt_temp_status = BAT_TEMP_LOW0;
			ret = chip->warp_strategy1_low_current0;
		}
		break;
	case BAT_TEMP_LOW1:
		if (vbat_temp_cur > chip->warp_strategy1_batt_high_temp1) {
			chip->fastchg_batt_temp_status = BAT_TEMP_HIGH1;
			ret = chip->warp_strategy1_high_current1;
		} else if (vbat_temp_cur < chip->warp_strategy1_batt_low_temp0) {
			chip->fastchg_batt_temp_status = BAT_TEMP_LOW0;
			ret = chip->warp_strategy1_low_current0;
		} else {
			chip->fastchg_batt_temp_status = BAT_TEMP_LOW1;
			ret = chip->warp_strategy1_low_current1;
		}
		break;
	case BAT_TEMP_LOW2:
		if (vbat_temp_cur > chip->warp_strategy1_batt_high_temp2) {
			chip->fastchg_batt_temp_status = BAT_TEMP_HIGH2;
			ret = chip->warp_strategy1_high_current2;
		} else if (vbat_temp_cur < chip->warp_strategy1_batt_low_temp1) {
			chip->fastchg_batt_temp_status = BAT_TEMP_LOW1;
			ret = chip->warp_strategy1_low_current1;
		} else {
			chip->fastchg_batt_temp_status = BAT_TEMP_LOW2;
			ret = chip->warp_strategy1_low_current2;
		}
		break;
	default:
		break;
	}
	warp_xlog_printk(CHG_LOG_CRTI, "the ret: %d, the temp =%d, status = %d\r\n", ret, vbat_temp_cur, chip->fastchg_batt_temp_status);
	return ret;
}

static int oplus_warp_set_current_temp_low_normal_range(struct oplus_warp_chip *chip, int vbat_temp_cur)
{
	int ret = 0;

	if (vbat_temp_cur < chip->warp_normal_low_temp
		&& vbat_temp_cur >= chip->warp_little_cool_temp) { /*16C<=T<25C*/
		chip->warp_temp_cur_range = FASTCHG_TEMP_RANGE_NORMAL_LOW;
		chip->fastchg_batt_temp_status = BAT_TEMP_NORMAL_LOW;
		ret = chip->warp_strategy_normal_current;
	} else {
		if (vbat_temp_cur >= chip->warp_normal_low_temp) {
			chip->warp_normal_low_temp -= WARP_TEMP_RANGE_THD;
			chip->fastchg_batt_temp_status = BAT_TEMP_NORMAL_HIGH;
			ret = chip->warp_strategy_normal_current;
			chip->warp_temp_cur_range = FASTCHG_TEMP_RANGE_NORMAL_HIGH;
		} else {
			chip->fastchg_batt_temp_status = BAT_TEMP_LITTLE_COOL;
			chip->warp_temp_cur_range = FASTCHG_TEMP_RANGE_LITTLE_COOL;
			ret = chip->warp_strategy_normal_current;
			oplus_warp_reset_temp_range(chip);
			chip->warp_little_cool_temp += WARP_TEMP_RANGE_THD;
		}
	}

	return ret;
}

static int oplus_warp_set_current_temp_little_cool_range(struct oplus_warp_chip *chip, int vbat_temp_cur)
{
	int ret = 0;

	if (vbat_temp_cur < chip->warp_little_cool_temp
		&& vbat_temp_cur >= chip->warp_cool_temp) {/*12C<=T<16C*/
		chip->fastchg_batt_temp_status = BAT_TEMP_LITTLE_COOL;
		ret = chip->warp_strategy_normal_current;
		chip->warp_temp_cur_range = FASTCHG_TEMP_RANGE_LITTLE_COOL;
	} else {
		if (vbat_temp_cur >= chip->warp_little_cool_temp) {
			chip->warp_little_cool_temp -= WARP_TEMP_RANGE_THD;
			chip->fastchg_batt_temp_status = BAT_TEMP_NORMAL_LOW;
			ret = chip->warp_strategy_normal_current;
			chip->warp_temp_cur_range = FASTCHG_TEMP_RANGE_NORMAL_LOW;
		} else {
			chip->fastchg_batt_temp_status = BAT_TEMP_COOL;
			chip->warp_temp_cur_range = FASTCHG_TEMP_RANGE_COOL;
			ret = chip->warp_strategy_normal_current;
			oplus_warp_reset_temp_range(chip);
			chip->warp_cool_temp += WARP_TEMP_RANGE_THD;
		}
	}

	return ret;
}

static int oplus_warp_set_current_temp_cool_range(struct oplus_warp_chip *chip, int vbat_temp_cur)
{
	int ret = 0;
	if (chip->warp_batt_over_high_temp != -EINVAL
			&& vbat_temp_cur < chip->warp_batt_over_low_temp) {
		chip->warp_strategy_change_count++;
		if (chip->warp_strategy_change_count >= WARP_TEMP_OVER_COUNTS) {
			chip->warp_strategy_change_count = 0;
			chip->fastchg_batt_temp_status = BAT_TEMP_EXIT;
			ret = chip->warp_over_high_or_low_current;
		}
	} else if (vbat_temp_cur < chip->warp_cool_temp
		&& vbat_temp_cur >= chip->warp_little_cold_temp) {/*5C <=T<12C*/
		chip->warp_temp_cur_range = FASTCHG_TEMP_RANGE_COOL;
		chip->fastchg_batt_temp_status = BAT_TEMP_COOL;
		ret = chip->warp_strategy_normal_current;
	} else {
		if (vbat_temp_cur >= chip->warp_cool_temp) {
			chip->warp_cool_temp -= WARP_TEMP_RANGE_THD;
			chip->fastchg_batt_temp_status = BAT_TEMP_LITTLE_COOL;
			ret = chip->warp_strategy_normal_current;
			chip->warp_temp_cur_range = FASTCHG_TEMP_RANGE_LITTLE_COOL;
		} else {
			chip->fastchg_batt_temp_status = BAT_TEMP_LITTLE_COLD;
			chip->warp_temp_cur_range = FASTCHG_TEMP_RANGE_LITTLE_COLD;
			ret = chip->warp_strategy_normal_current;
			oplus_warp_reset_temp_range(chip);
			chip->warp_little_cold_temp += WARP_TEMP_RANGE_THD;
		}
	}

	return ret;
}

static int oplus_warp_set_current_temp_little_cold_range(struct oplus_warp_chip *chip, int vbat_temp_cur)
{
	int ret = 0;
	if (chip->warp_batt_over_high_temp != -EINVAL
			&& vbat_temp_cur < chip->warp_batt_over_low_temp) {
		chip->warp_strategy_change_count++;
		if (chip->warp_strategy_change_count >= WARP_TEMP_OVER_COUNTS) {
			chip->warp_strategy_change_count = 0;
			chip->fastchg_batt_temp_status = BAT_TEMP_EXIT;
			ret = chip->warp_over_high_or_low_current;
		}
	} else if (vbat_temp_cur < chip->warp_little_cold_temp) { /*0C<=T<5C*/
		chip->warp_temp_cur_range = FASTCHG_TEMP_RANGE_LITTLE_COLD;
		chip->fastchg_batt_temp_status = BAT_TEMP_LITTLE_COLD;
		ret = chip->warp_strategy_normal_current;
	} else {
		chip->fastchg_batt_temp_status = BAT_TEMP_COOL;
		ret = chip->warp_strategy_normal_current;
		chip->warp_temp_cur_range = FASTCHG_TEMP_RANGE_COOL;
		oplus_warp_reset_temp_range(chip);
		chip->warp_little_cold_temp -= WARP_TEMP_RANGE_THD;
	}

	return ret;
}

static int oplus_warp_init_soc_range(struct oplus_warp_chip *chip, int soc)
{
	if (soc >= 0 && soc <= 50) {
		chip->soc_range = 0;
	} else if (soc >= 51 && soc <= 75) {
		chip->soc_range = 1;
	} else if (soc >= 76 && soc <= 85) {
		chip->soc_range = 2;
	} else {
		chip->soc_range = 3;
	}
	chg_err("chip->soc_range[%d], soc[%d]", chip->soc_range, soc);
	return chip->soc_range;
}

static int oplus_warp_init_temp_range(struct oplus_warp_chip *chip, int vbat_temp_cur)
{
	if (vbat_temp_cur < chip->warp_little_cold_temp) { /*0-5C*/
		chip->warp_temp_cur_range = FASTCHG_TEMP_RANGE_LITTLE_COLD;
		chip->fastchg_batt_temp_status = BAT_TEMP_LITTLE_COLD;
	} else if (vbat_temp_cur < chip->warp_cool_temp) { /*5-12C*/
		chip->warp_temp_cur_range = FASTCHG_TEMP_RANGE_COOL;
		chip->fastchg_batt_temp_status = BAT_TEMP_COOL;
	} else if (vbat_temp_cur < chip->warp_little_cool_temp) { /*12-16C*/
		chip->warp_temp_cur_range = FASTCHG_TEMP_RANGE_LITTLE_COOL;
		chip->fastchg_batt_temp_status = BAT_TEMP_LITTLE_COOL;
	} else if (vbat_temp_cur < chip->warp_normal_low_temp) { /*16-25C*/
		chip->warp_temp_cur_range = FASTCHG_TEMP_RANGE_NORMAL_LOW;
		chip->fastchg_batt_temp_status = BAT_TEMP_NORMAL_LOW;
	} else {/*25C-43C*/
		chip->warp_temp_cur_range = FASTCHG_TEMP_RANGE_NORMAL_HIGH;
		chip->fastchg_batt_temp_status = BAT_TEMP_NORMAL_HIGH;
	}
	chg_err("chip->warp_temp_cur_range[%d], vbat_temp_cur[%d]", chip->warp_temp_cur_range, vbat_temp_cur);
	return chip->warp_temp_cur_range;

}

static int oplus_warp_set_current_when_bleow_setting_batt_temp
		(struct oplus_warp_chip *chip, int vbat_temp_cur)
{
	int ret = 0;

	if (chip->warp_temp_cur_range == FASTCHG_TEMP_RANGE_INIT) {
		if (vbat_temp_cur < chip->warp_little_cold_temp) { /*0-5C*/
			chip->warp_temp_cur_range = FASTCHG_TEMP_RANGE_LITTLE_COLD;
			chip->fastchg_batt_temp_status = BAT_TEMP_LITTLE_COLD;
		} else if (vbat_temp_cur < chip->warp_cool_temp) { /*5-12C*/
			chip->warp_temp_cur_range = FASTCHG_TEMP_RANGE_COOL;
			chip->fastchg_batt_temp_status = BAT_TEMP_COOL;
		} else if (vbat_temp_cur < chip->warp_little_cool_temp) { /*12-16C*/
			chip->warp_temp_cur_range = FASTCHG_TEMP_RANGE_LITTLE_COOL;
			chip->fastchg_batt_temp_status = BAT_TEMP_LITTLE_COOL;
		} else if (vbat_temp_cur < chip->warp_normal_low_temp) { /*16-25C*/
			chip->warp_temp_cur_range = FASTCHG_TEMP_RANGE_NORMAL_LOW;
			chip->fastchg_batt_temp_status = BAT_TEMP_NORMAL_LOW;
		} else {/*25C-43C*/
			chip->warp_temp_cur_range = FASTCHG_TEMP_RANGE_NORMAL_HIGH;
			chip->fastchg_batt_temp_status = BAT_TEMP_NORMAL_HIGH;
		}
	}

	switch (chip->warp_temp_cur_range) {
	case FASTCHG_TEMP_RANGE_NORMAL_HIGH:
		ret = oplus_warp_set_current_1_temp_normal_range(chip, vbat_temp_cur);
		break;
	case FASTCHG_TEMP_RANGE_NORMAL_LOW:
		ret = oplus_warp_set_current_temp_low_normal_range(chip, vbat_temp_cur);
		break;
	case FASTCHG_TEMP_RANGE_LITTLE_COOL:
		ret = oplus_warp_set_current_temp_little_cool_range(chip, vbat_temp_cur);
		break;
	case FASTCHG_TEMP_RANGE_COOL:
		ret = oplus_warp_set_current_temp_cool_range(chip, vbat_temp_cur);
		break;
	case FASTCHG_TEMP_RANGE_LITTLE_COLD:
		ret = oplus_warp_set_current_temp_little_cold_range(chip, vbat_temp_cur);
		break;
	default:
		break;
	}

	warp_xlog_printk(CHG_LOG_CRTI, "the ret: %d, the temp =%d, temp_status = %d, temp_range = %d\r\n", 
			ret, vbat_temp_cur, chip->fastchg_batt_temp_status, chip->warp_temp_cur_range);
	return ret;
}

static int oplus_warp_set_current_2_temp_normal_range(struct oplus_warp_chip *chip, int vbat_temp_cur){
	int ret = 0;

	chip->warp_temp_cur_range = FASTCHG_TEMP_RANGE_NORMAL_HIGH;

	switch (chip->fastchg_batt_temp_status) {
	case BAT_TEMP_NORMAL_HIGH:
		if (vbat_temp_cur > chip->warp_strategy2_batt_up_temp1) {
			chip->fastchg_batt_temp_status = BAT_TEMP_HIGH0;
			ret = chip->warp_strategy2_high0_current;
		} else if (vbat_temp_cur >= chip->warp_normal_low_temp) {
			chip->fastchg_batt_temp_status = BAT_TEMP_NORMAL_HIGH;
			ret = chip->warp_strategy_normal_current;
		} else {
			chip->fastchg_batt_temp_status = BAT_TEMP_NORMAL_LOW;
			chip->warp_temp_cur_range = FASTCHG_TEMP_RANGE_NORMAL_LOW;
			ret = chip->warp_strategy_normal_current;
			oplus_warp_reset_temp_range(chip);
			chip->warp_normal_low_temp += WARP_TEMP_RANGE_THD;
		}
		break;
	case BAT_TEMP_HIGH0:
		if (vbat_temp_cur > chip->warp_strategy2_batt_up_temp3) {
			chip->fastchg_batt_temp_status = BAT_TEMP_HIGH1;
			ret = chip->warp_strategy2_high1_current;
		} else if (vbat_temp_cur < chip->warp_normal_low_temp) { /*T<25*/
			chip->fastchg_batt_temp_status = BAT_TEMP_NORMAL_LOW;
			chip->warp_temp_cur_range = FASTCHG_TEMP_RANGE_NORMAL_LOW;
			ret = chip->warp_strategy_normal_current;
			oplus_warp_reset_temp_range(chip);
			chip->warp_normal_low_temp += WARP_TEMP_RANGE_THD;
		} else {
			chip->fastchg_batt_temp_status = BAT_TEMP_HIGH0;
			ret = chip->warp_strategy2_high0_current;
		}
		break;
	case BAT_TEMP_HIGH1:
		if (vbat_temp_cur > chip->warp_strategy2_batt_up_temp5) {
			chip->fastchg_batt_temp_status = BAT_TEMP_HIGH2;
			ret = chip->warp_strategy2_high2_current;
		} else if (vbat_temp_cur < chip->warp_normal_low_temp) { /*T<25*/
			chip->fastchg_batt_temp_status = BAT_TEMP_NORMAL_LOW;
			chip->warp_temp_cur_range = FASTCHG_TEMP_RANGE_NORMAL_LOW;
			ret = chip->warp_strategy_normal_current;
			oplus_warp_reset_temp_range(chip);
			chip->warp_normal_low_temp += WARP_TEMP_RANGE_THD;
		} else {
			chip->fastchg_batt_temp_status = BAT_TEMP_HIGH1;
			ret = chip->warp_strategy2_high1_current;
		}
		break;
	case BAT_TEMP_HIGH2:
		if (vbat_temp_cur > chip->warp_strategy2_batt_up_temp6) {
			chip->fastchg_batt_temp_status = BAT_TEMP_HIGH3;
			ret = chip->warp_strategy2_high3_current;
		} else if (vbat_temp_cur < chip->warp_strategy2_batt_up_down_temp2) {
			chip->fastchg_batt_temp_status = BAT_TEMP_HIGH1;
			ret = chip->warp_strategy2_high1_current;
		} else {
			chip->fastchg_batt_temp_status = BAT_TEMP_HIGH2;
			ret = chip->warp_strategy2_high2_current;
		}
		break;
	case BAT_TEMP_HIGH3:
		if (chip->warp_batt_over_high_temp != -EINVAL
				&& vbat_temp_cur > chip->warp_batt_over_high_temp) {
			chip->warp_strategy_change_count++;
			if (chip->warp_strategy_change_count >= WARP_TEMP_OVER_COUNTS) {
				chip->warp_strategy_change_count = 0;
				chip->fastchg_batt_temp_status = BAT_TEMP_EXIT;
				ret = chip->warp_over_high_or_low_current;
			}
		} else if (vbat_temp_cur < chip->warp_strategy2_batt_up_down_temp4) {
			chip->fastchg_batt_temp_status = BAT_TEMP_HIGH2;
			ret = chip->warp_strategy2_high2_current;
		} else {
			chip->fastchg_batt_temp_status = BAT_TEMP_HIGH3;
			ret = chip->warp_strategy2_high3_current;
		}
		break;
	default:
		break;
	}
	warp_xlog_printk(CHG_LOG_CRTI, "the ret: %d, the temp =%d\r\n", ret, vbat_temp_cur);
	return ret;
}
static int oplus_warp_set_current_when_up_setting_batt_temp
		(struct oplus_warp_chip *chip, int vbat_temp_cur)
{
	int ret = 0;

	if (chip->warp_temp_cur_range == FASTCHG_TEMP_RANGE_INIT) {
		chip->warp_temp_cur_range = FASTCHG_TEMP_RANGE_NORMAL_HIGH;
		chip->fastchg_batt_temp_status = BAT_TEMP_NORMAL_HIGH;
	}

	ret = oplus_warp_set_current_2_temp_normal_range(chip, vbat_temp_cur);

	warp_xlog_printk(CHG_LOG_CRTI, "the ret: %d, the temp =%d, temp_status = %d, temp_range = %d\r\n",
			ret, vbat_temp_cur, chip->fastchg_batt_temp_status, chip->warp_temp_cur_range);
	return ret;
}

int oplus_warp_get_smaller_battemp_cooldown(int ret_batt, int ret_cool){
	int ret_batt_current =0;
	int ret_cool_current = 0;
	int i = 0;
	struct oplus_warp_chip *chip = g_warp_chip;
	int *current_level = NULL;
	int array_len = 0;

	if (g_warp_chip->warp_current_lvl_cnt > 0) {
		current_level = g_warp_chip->warp_current_lvl;
		array_len = g_warp_chip->warp_current_lvl_cnt;
	} else {
		current_level = multistepCurrent;
		array_len = ARRAY_SIZE(multistepCurrent);
	}

	if(ret_batt > 0 && ret_batt < (array_len + 1)
		&& ret_cool > 0 && ret_cool < (array_len + 1)) {
		ret_batt_current =  current_level[ret_batt -1];
		ret_cool_current = current_level[ret_cool -1];
		oplus_chg_debug_get_cooldown_current(ret_batt_current, ret_cool_current);
		ret_cool_current = ret_cool_current < ret_batt_current ? ret_cool_current : ret_batt_current;

		if(ret_cool > 0) {
			if(ret_cool_current < ret_batt_current) {
				/*set flag cool down by user */
				oplus_chg_debug_set_cool_down_by_user(1);
			} else {
				/*clear flag cool down by user */
				oplus_chg_debug_set_cool_down_by_user(0);
			}
		}

		for(i = 0 ; i < array_len; i++) {
			if (current_level[i] == ret_cool_current) {
				if (chip) {
					chip->warp_chg_current_now = ret_cool_current;
				}
				return i + 1;
			}
		}
	}

	return -1;
}

int oplus_warp_get_cool_down_valid(void) {
	int cool_down = oplus_chg_get_cool_down_status();
	if(!g_warp_chip) {
		pr_err("WARP NULL ,return!!");
		return 0;
	}
	if (g_warp_chip->warp_multistep_adjust_current_support == true) {
		if(g_warp_chip->warp_reply_mcu_bits == 7) {
			return cool_down;
		} else {
			if(cool_down > 6 || cool_down < 0)
				cool_down = 6;
		}
	}

	return cool_down;
}

#ifdef OPLUS_CHG_OP_DEF
static bool is_comm_ocm_available(struct oplus_warp_chip *dev)
{
	if (!dev->comm_ocm)
		dev->comm_ocm = oplus_chg_mod_get_by_name("common");
	return !!dev->comm_ocm;
}

static int oplus_chg_get_skin_temp(struct oplus_warp_chip *dev, int *temp)
{
	int rc;
	union oplus_chg_mod_propval pval;
#ifdef OPLUS_CHG_OP_DEF
	struct oplus_chg_chip *chg_chip = oplus_chg_get_chg_struct();
#endif
	if (!is_comm_ocm_available(dev)) {
		pr_err("comm ocm not found\n");
		return -ENODEV;
	}
	rc = oplus_chg_mod_get_property(dev->comm_ocm, OPLUS_CHG_PROP_SKIN_TEMP, &pval);
	if (rc < 0)
		return rc;
	*temp = pval.intval;
#ifdef OPLUS_CHG_OP_DEF
	if (chg_chip->factory_mode) {
		chg_err("factory_mode:%d,real skin_temp:%d\n", chg_chip->factory_mode, *temp);
		*temp = 250;
	}
#endif
	return 0;
}

static int oplus_chg_get_fast_chg_min_curr_level(int level_base, int level_new,
						 unsigned int sid, bool fw_7bit)
{
	int curr_base, curr_new;

	if (fw_7bit) {
		if (level_base >= CURR_LIMIT_7BIT_MAX || level_new >= CURR_LIMIT_7BIT_MAX) {
			pr_err("current limit level error\n");
			return level_base;
		}
		return level_new < level_base ? level_new : level_base;
	} else {
		if (level_base >= CURR_LIMIT_MAX || level_new >= CURR_LIMIT_MAX) {
			pr_err("current limit level error\n");
			return level_base;
		}

		if (sid_to_adapter_chg_type(sid) == CHARGER_TYPE_WARP) {
			curr_base = oplus_chg_warp_curr_table[level_base - 1];
			curr_new = oplus_chg_warp_curr_table[level_new - 1];
		} else if (sid_to_adapter_chg_type(sid) == CHARGER_TYPE_SWARP) {
			curr_base = oplus_chg_swarp_curr_table[level_base - 1];
			curr_new = oplus_chg_swarp_curr_table[level_new - 1];
		} else {
			pr_err("unknown fast chg type(=%d)\n", sid_to_adapter_chg_type(sid));
			return level_base;
		}

		if (curr_base <= curr_new)
			return level_base;
		else
			return level_new;
	}
}

static int cur_max_4bit[] = {0x06, 0x05, 0x04, 0x03};
static int cur_max_7bit[] = {CURR_LIMIT_7BIT_6_3A, CURR_LIMIT_7BIT_5_0A, CURR_LIMIT_7BIT_4_0A, CURR_LIMIT_7BIT_3_0A};
static int oplus_get_allowed_current_max(bool fw_7bit)
{
	int cur_stage = 0;
	struct oplus_chg_chip *chg_chip = oplus_chg_get_chg_struct();

	if (!chg_chip)
		return fw_7bit ? cur_max_7bit[0] : cur_max_4bit[0];

	cur_stage = chg_chip->reconnect_count;
	if (cur_stage > 3)
		cur_stage = 3;
	else if (cur_stage < 0)
		cur_stage = 0;

	return fw_7bit ? cur_max_7bit[cur_stage] : cur_max_4bit[cur_stage];
}

#endif

static void oplus_warp_fastchg_func(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_warp_chip *chip = container_of(dwork, struct oplus_warp_chip, fastchg_work);
	int i = 0;
	int bit = 0;
	int data = 0;
	int ret_info = 0;
	int ret_info_temp = 0;
	int ret_rst = 0;
	static int pre_ret_info = 0;
	static int select_func_flag = 0;
	static bool first_detect_batt_temp = false;
	static bool isnot_power_on = true;
	static bool fw_ver_info = false;
	static bool adapter_fw_ver_info = false;
	static bool data_err = false;
	static bool adapter_model_factory = false;
	int volt = oplus_chg_get_batt_volt();
	int temp = oplus_chg_get_chg_temperature();
	int soc = oplus_chg_get_soc();
	int current_now = oplus_chg_get_icharging();
	int chg_vol = oplus_chg_get_charger_voltage();
	int remain_cap = 0;
	static bool phone_mcu_updated = false;
	static bool normalchg_disabled = false;
#ifdef OPLUS_CHG_OP_DEF
	struct oplus_chg_chip *chg_chip = oplus_chg_get_chg_struct();
	static struct oplus_chg_strategy swarp_led_on_strategy;
	static struct oplus_chg_strategy swarp_general_strategy;
	static struct oplus_chg_strategy swarp_led_off_strategy;
	int skin_temp;
	int rc;
	int ret_info_cool_down = -1;
#endif
/*
	if (!g_adapter_chip) {
		chg_err(" g_adapter_chip NULL\n");
		return;
	}
*/
	if (!chip->vops) {
		chg_err("vops is null\n");
		return;
	}

	usleep_range(2000, 2000);
	if (chip->vops->get_gpio_ap_data(chip) != 1) {
		/*warp_xlog_printk(CHG_LOG_CRTI, "  Shield fastchg irq, return\r\n");*/
		return;
	}

	chip->vops->eint_unregist(chip);
	for (i = 0; i < 7; i++) {
		bit = chip->vops->read_ap_data(chip);
		data |= bit << (6-i);
		if ((i == 2) && (data != 0x50) && (!fw_ver_info)
				&& (!adapter_fw_ver_info) && (!adapter_model_factory)) {	/*data recvd not start from "101"*/
			warp_xlog_printk(CHG_LOG_CRTI, "  data err:0x%x\n", data);
			chip->allow_reading = true;
			if (chip->fastchg_started == true) {
				chip->fastchg_started = false;
				chip->fastchg_to_normal = false;
#ifdef OPLUS_CHG_OP_DEF
				chip->ffc_chg_start = false;
				chip->fastchg_ignore_event = false;
#endif
				chip->fastchg_to_warm = false;
				chip->fastchg_ing = false;
				adapter_fw_ver_info = false;
				/*chip->adapter_update_real = ADAPTER_FW_UPDATE_NONE;*/
				/*chip->adapter_update_report = chip->adapter_update_real;*/
				chip->btb_temp_over = false;
				oplus_set_fg_i2c_err_occured(false);
				oplus_chg_set_chargerid_switch_val(0);
				chip->vops->set_switch_mode(chip, NORMAL_CHARGER_MODE);
				data_err = true;
				if (chip->fastchg_dummy_started) {
					chg_vol = oplus_chg_get_charger_voltage();
					if (chg_vol >= 0 && chg_vol < 2000) {
						chip->fastchg_dummy_started = false;
						oplus_chg_clear_chargerid_info();
						warp_xlog_printk(CHG_LOG_CRTI,
							"chg_vol:%d dummy_started:false\n", chg_vol);
					}
				} else {
					oplus_chg_clear_chargerid_info();
				}
				///del_timer(&chip->watchdog);
				oplus_warp_set_mcu_sleep();
				oplus_warp_del_watchdog_timer(chip);
			}
			oplus_warp_set_awake(chip, false);
			goto out;
		}
	}
	warp_xlog_printk(CHG_LOG_CRTI, " recv data:0x%x, ap:0x%x, mcu:0x%x\n",
		data, chip->fw_data_version, chip->fw_mcu_version);

	if (data == WARP_NOTIFY_FAST_PRESENT) {
		oplus_warp_set_awake(chip, true);
		oplus_set_fg_i2c_err_occured(false);
		chip->need_to_up = 0;
		fw_ver_info = false;
		pre_ret_info = (chip->warp_reply_mcu_bits == 7) ? 0x0c : 0x06;
		adapter_fw_ver_info = false;
		adapter_model_factory = false;
		data_err = false;
		phone_mcu_updated = false;
		normalchg_disabled = false;
		first_detect_batt_temp = true;
		chip->fastchg_batt_temp_status = BAT_TEMP_NATURAL;
		chip->warp_temp_cur_range = FASTCHG_TEMP_RANGE_INIT;
		if (chip->adapter_update_real == ADAPTER_FW_UPDATE_FAIL) {
			chip->adapter_update_real = ADAPTER_FW_UPDATE_NONE;
			chip->adapter_update_report = chip->adapter_update_real;
		}
		if (oplus_warp_get_fastchg_allow() == true) {
			oplus_chg_set_input_current_without_aicl(1200);
			chip->allow_reading = false;
			chip->fastchg_started = true;
			chip->fastchg_ing = false;
			chip->fastchg_dummy_started = false;
			chip->fastchg_to_warm = false;
			chip->btb_temp_over = false;
			chip->reset_adapter = false;
			chip->suspend_charger = false;
		} else {
			chip->allow_reading = false;
			chip->fastchg_dummy_started = true;
			chip->fastchg_started = false;
			chip->fastchg_to_normal = false;
#ifdef OPLUS_CHG_OP_DEF
			chip->ffc_chg_start = false;
			chip->fastchg_ignore_event = false;
#endif
			chip->fastchg_to_warm = false;
			chip->fastchg_ing = false;
			chip->btb_temp_over = false;
			oplus_chg_set_chargerid_switch_val(0);
			chip->vops->set_switch_mode(chip, NORMAL_CHARGER_MODE);
			oplus_warp_set_awake(chip, false);
		}
#ifdef OPLUS_CHG_OP_DEF
		rc = oplus_chg_get_skin_temp(chip, &skin_temp);
		if (rc < 0) {
			chg_err("can't get skin temp\n");
			skin_temp = 250;
		}
		temp = oplus_gauge_get_batt_temperature();

		if (chg_chip) {
			oplus_chg_strategy_init(
				&swarp_led_on_strategy,
				chg_chip->dynamic_config.swarp_chg_led_on_strategy_data,
				oplus_chg_get_chg_strategy_data_len(
					chg_chip->dynamic_config
						.swarp_chg_led_on_strategy_data,
					CHG_STRATEGY_DATA_TABLE_MAX),
				skin_temp);
			if (temp <= 300) {
				oplus_chg_strategy_init(
					&swarp_led_off_strategy,
					chg_chip->dynamic_config.swarp_chg_led_off_strategy_data,
					oplus_chg_get_chg_strategy_data_len(
						chg_chip->dynamic_config
							.swarp_chg_led_off_strategy_data,
						CHG_STRATEGY_DATA_TABLE_MAX),
					skin_temp);
			} else {
				oplus_chg_strategy_init(
					&swarp_led_off_strategy,
					chg_chip->dynamic_config.swarp_chg_led_off_strategy_data_high,
					oplus_chg_get_chg_strategy_data_len(
						chg_chip->dynamic_config
							.swarp_chg_led_off_strategy_data_high,
						CHG_STRATEGY_DATA_TABLE_MAX),
					skin_temp);
			}
		}
#endif
		//mod_timer(&chip->watchdog, jiffies+msecs_to_jiffies(25000));
		oplus_warp_setup_watchdog_timer(chip, 25000);
		if (!isnot_power_on) {
			isnot_power_on = true;
			ret_info = 0x1;
		} else {
			ret_info = 0x2;
		}
	} else if (data == WARP_NOTIFY_FAST_ABSENT) {
		chip->detach_unexpectly = true;
		chip->fastchg_started = false;
		chip->fastchg_to_normal = false;
#ifdef OPLUS_CHG_OP_DEF
		chip->ffc_chg_start = false;
		chip->fastchg_ignore_event = false;
		if (chg_chip != NULL)
			chg_chip->reconnect_count++;
#endif
		chip->fastchg_to_warm = false;
		chip->fastchg_ing = false;
		chip->btb_temp_over = false;
		adapter_fw_ver_info = false;
		adapter_model_factory = false;
		oplus_set_fg_i2c_err_occured(false);
		if (chip->fastchg_dummy_started) {
			chg_vol = oplus_chg_get_charger_voltage();
			if (chg_vol >= 0 && chg_vol < 2000) {
				chip->fastchg_dummy_started = false;
				chip->fast_chg_type = FASTCHG_CHARGER_TYPE_UNKOWN;
#ifdef OPLUS_CHG_OP_DEF
				chip->adapter_sid = 0;
#endif
				oplus_chg_clear_chargerid_info();
				warp_xlog_printk(CHG_LOG_CRTI,
					"chg_vol:%d dummy_started:false\n", chg_vol);
			}
		} else {
			chip->fast_chg_type = FASTCHG_CHARGER_TYPE_UNKOWN;
#ifdef OPLUS_CHG_OP_DEF
			chip->adapter_sid = 0;
#endif
			oplus_chg_clear_chargerid_info();
		}
		warp_xlog_printk(CHG_LOG_CRTI,
			"fastchg stop unexpectly, switch off fastchg\n");
		oplus_chg_set_chargerid_switch_val(0);
		chip->vops->set_switch_mode(chip, NORMAL_CHARGER_MODE);
		//del_timer(&chip->watchdog);
		oplus_warp_set_mcu_sleep();
		oplus_warp_del_watchdog_timer(chip);
		chip->allow_reading = true;
		ret_info = 0x2;
	} else if (data == WARP_NOTIFY_ADAPTER_MODEL_FACTORY) {
		warp_xlog_printk(CHG_LOG_CRTI, " WARP_NOTIFY_ADAPTER_MODEL_FACTORY!\n");
		/*ready to get adapter_model_factory*/
		adapter_model_factory = 1;
#ifdef OPLUS_CHG_OP_DEF
		temp = oplus_gauge_get_batt_temperature();
		if (chg_chip) {
			if (temp < 310) {
				oplus_chg_strategy_init(
					&swarp_general_strategy,
					chg_chip->dynamic_config
						.swarp_general_chg_strategy_data_low,
					oplus_chg_get_chg_strategy_data_len(
						chg_chip->dynamic_config
							.swarp_general_chg_strategy_data_low,
						CHG_STRATEGY_DATA_TABLE_MAX),
					temp);
			} else {
				oplus_chg_strategy_init(
					&swarp_general_strategy,
					chg_chip->dynamic_config
						.swarp_general_chg_strategy_data_high,
					oplus_chg_get_chg_strategy_data_len(
						chg_chip->dynamic_config
							.swarp_general_chg_strategy_data_high,
						CHG_STRATEGY_DATA_TABLE_MAX),
					temp);
			}
		}
#endif
		ret_info = 0x2;
	} else if (adapter_model_factory) {
		warp_xlog_printk(CHG_LOG_CRTI, "WARP_NOTIFY_ADAPTER_MODEL_FACTORY:0x%x, \n", data);
		//chip->fast_chg_type = data;
		if (data == 0) {
			chip->fast_chg_type = CHARGER_SUBTYPE_FASTCHG_WARP;
		} else {
			chip->fast_chg_type = oplus_warp_convert_fast_chg_type(data);
		}
#ifdef OPLUS_CHG_OP_DEF
		chip->adapter_sid = oplus_get_adapter_sid((unsigned char)data);
		if (chg_chip) {
			if (!((chg_chip->vbatt_num == 2) &&
			    (sid_to_adapter_chg_type(chip->adapter_sid) !=
			     CHARGER_TYPE_SWARP)))
				oplus_chg_update_float_voltage_by_fastchg(true);
		}
#endif
		adapter_model_factory = 0;
		if (chip->fast_chg_type == 0x0F
				|| chip->fast_chg_type == 0x1F
				|| chip->fast_chg_type == 0x3F
				|| chip->fast_chg_type == 0x7F) {
			chip->allow_reading = true;
			chip->fastchg_started = false;
			chip->fastchg_to_normal = false;
#ifdef OPLUS_CHG_OP_DEF
			chip->ffc_chg_start = false;
			chip->fastchg_ignore_event = false;
#endif
			chip->fastchg_to_warm = false;
			chip->fastchg_ing = false;
			chip->btb_temp_over = false;
			adapter_fw_ver_info = false;
			oplus_set_fg_i2c_err_occured(false);
			oplus_chg_set_chargerid_switch_val(0);
			chip->vops->set_switch_mode(chip, NORMAL_CHARGER_MODE);
			data_err = true;
		}
	ret_info = 0x2;
	} else if (data == WARP_NOTIFY_ALLOW_READING_IIC) {
		if(chip->fastchg_allow) {
			chip->detach_unexpectly = false;
			chip->fastchg_ing = true;
			chip->allow_reading = true;
			adapter_fw_ver_info = false;
			adapter_model_factory = false;
			soc = oplus_gauge_get_batt_soc();
			oplus_chg_get_charger_voltage();
			if (oplus_get_fg_i2c_err_occured() == false) {
				volt = oplus_gauge_get_batt_mvolts();
				chg_err("0x58 read volt = %d\n", volt);
			}
			if (oplus_get_fg_i2c_err_occured() == false) {
				oplus_gauge_get_batt_temperature();
				if (!chip->temp_range_init) {
					temp = oplus_chg_match_temp_for_chging();
				}
				chip->temp_range_init = false;
			}
			if (oplus_get_fg_i2c_err_occured() == false) {
				current_now = oplus_gauge_get_batt_current();
			}
			if (oplus_get_fg_i2c_err_occured() == false) {
				remain_cap = oplus_gauge_get_remaining_capacity();
				oplus_gauge_get_batt_fcc();
				oplus_gauge_get_batt_fc();
				oplus_gauge_get_batt_qm();
				oplus_gauge_get_batt_pd();
				oplus_gauge_get_batt_rcu();
				oplus_gauge_get_batt_rcf();
				oplus_gauge_get_batt_fcu();
				oplus_gauge_get_batt_fcf();
				oplus_gauge_get_batt_sou();
				oplus_gauge_get_batt_do0();
				oplus_gauge_get_batt_doe();
				oplus_gauge_get_batt_trm();
				oplus_gauge_get_batt_pc();
				oplus_gauge_get_batt_qs();
			}
			oplus_chg_kick_wdt();
			if (chip->support_warp_by_normal_charger_path) {//65w
				if(!normalchg_disabled && chip->fast_chg_type != FASTCHG_CHARGER_TYPE_UNKOWN
					&& chip->fast_chg_type != CHARGER_SUBTYPE_FASTCHG_WARP) {
					oplus_chg_disable_charge();
					oplus_chg_suspend_charger();
					normalchg_disabled = true;
				}
			} else {
				if(!normalchg_disabled) {
					oplus_chg_disable_charge();
					normalchg_disabled = true;
				}
			}
			//don't read
			chip->allow_reading = false;
		}
		warp_xlog_printk(CHG_LOG_CRTI, " volt:%d,temp:%d,soc:%d,current_now:%d,rm:%d, i2c_err:%d\n",
			volt, temp, soc, current_now, remain_cap, oplus_get_fg_i2c_err_occured());
			//mod_timer(&chip->watchdog, jiffies+msecs_to_jiffies(25000));
			oplus_warp_setup_watchdog_timer(chip, 25000);
		if (chip->disable_adapter_output == true) {
			ret_info = (chip->warp_multistep_adjust_current_support
				&& (!(chip->support_warp_by_normal_charger_path
				&& chip->fast_chg_type == CHARGER_SUBTYPE_FASTCHG_WARP)))
				? 0x07 : 0x03;
		} else if (chip->set_warp_current_limit == WARP_MAX_CURRENT_LIMIT_2A
				|| (!(chip->support_warp_by_normal_charger_path
				&& chip->fast_chg_type == CHARGER_SUBTYPE_FASTCHG_WARP)
				&& oplus_chg_get_cool_down_status() >= 1)) {
				ret_info = oplus_warp_get_cool_down_valid();
				pr_info("%s:origin cool_down ret_info=%d\n", __func__, ret_info);
				ret_info = (chip->warp_multistep_adjust_current_support
				&& (!(chip->support_warp_by_normal_charger_path
				&& chip->fast_chg_type == CHARGER_SUBTYPE_FASTCHG_WARP)))
				? ret_info : 0x01;
				pr_info("%s:recheck cool_down ret_info=%d\n", __func__, ret_info);
				warp_xlog_printk(CHG_LOG_CRTI, "ret_info:%d\n", ret_info);
		} else {
			if ((chip->warp_multistep_adjust_current_support
				&& (!(chip->support_warp_by_normal_charger_path
				&&  chip->fast_chg_type == CHARGER_SUBTYPE_FASTCHG_WARP)))) {
				if (chip->warp_reply_mcu_bits == 7) {
					ret_info = 0xC;
				} else {
					ret_info = 0x06;
				}
			} else {
				ret_info =  0x02;
			}
		}

		if (chip->warp_multistep_adjust_current_support
				&& chip->disable_adapter_output == false
				&& (!(chip->support_warp_by_normal_charger_path
				&&  chip->fast_chg_type == CHARGER_SUBTYPE_FASTCHG_WARP))) {
			if (first_detect_batt_temp) {
				if (temp < chip->warp_multistep_initial_batt_temp) {
					select_func_flag = 1;
				} else {
					select_func_flag = 2;
				}
				first_detect_batt_temp = false;
			}
			if (select_func_flag == 1) {
				ret_info_temp = oplus_warp_set_current_when_bleow_setting_batt_temp(chip, temp);
			} else {
				ret_info_temp = oplus_warp_set_current_when_up_setting_batt_temp(chip, temp);
			}
			ret_rst = oplus_warp_get_smaller_battemp_cooldown(ret_info_temp , ret_info);
			if(ret_rst > 0) {
				ret_info = ret_rst;
			}
		}

		if ((chip->warp_multistep_adjust_current_support == true) && (soc > 85)) {
			ret_rst = oplus_warp_get_smaller_battemp_cooldown(pre_ret_info , ret_info);
			if(ret_rst > 0) {
				ret_info = ret_rst;
			}
			pre_ret_info = (ret_info <= 3) ? 3 : ret_info;
		} else if ((chip->warp_multistep_adjust_current_support == true) && (soc > 75)) {
			ret_rst = oplus_warp_get_smaller_battemp_cooldown(pre_ret_info , ret_info);
			if(ret_rst > 0) {
				ret_info = ret_rst;
			}
			pre_ret_info = (ret_info <= 5) ? 5 : ret_info;
		} else {
			pre_ret_info = ret_info;
		}
#ifdef OPLUS_CHG_OP_DEF
		ret_info_cool_down = ret_info;
		if (swarp_general_strategy.initialized) {
			temp = oplus_gauge_get_batt_temperature();
			ret_info = oplus_chg_strategy_get_data(&swarp_general_strategy, &swarp_general_strategy.temp_region, temp);
			chg_err("wkcs: general_strategy ret info=0x%02x\n", ret_info);
		} else {
			if (chip->warp_reply_mcu_bits == 7)
				ret_info = CURR_LIMIT_7BIT_6_3A;
			else
				ret_info = CURR_LIMIT_WARP_6_0A_SWARP_6_5A;
		}
#ifdef CONFIG_OPLUS_CHG_OOS
		if (swarp_led_on_strategy.initialized && chg_chip && chg_chip->led_on) {
			rc = oplus_chg_get_skin_temp(chip, &skin_temp);
			if (rc < 0) {
				chg_err("can't get skin temp\n");
				skin_temp = 250;
			}
			ret_info_temp = oplus_chg_strategy_get_data(
						&swarp_led_on_strategy,
						&swarp_led_on_strategy.temp_region, skin_temp);
			ret_info = oplus_chg_get_fast_chg_min_curr_level(
							ret_info, ret_info_temp, chip->adapter_sid,
							(chip->warp_reply_mcu_bits == 7));
			chg_err("skin_temp=%d, led_on=%d\n", skin_temp,
				chg_chip->led_on);
		} else if (swarp_led_off_strategy.initialized && chg_chip && !chg_chip->led_on) {
			rc = oplus_chg_get_skin_temp(chip, &skin_temp);
			if (rc < 0) {
				chg_err("can't get skin temp\n");
				skin_temp = 250;
			}
			ret_info_temp = oplus_chg_strategy_get_data(
						&swarp_led_off_strategy,
						&swarp_led_off_strategy.temp_region, skin_temp);
			ret_info = oplus_chg_get_fast_chg_min_curr_level(
							ret_info, ret_info_temp, chip->adapter_sid,
							(chip->warp_reply_mcu_bits == 7));
			chg_err("skin_temp=%d, led_on=%d\n", skin_temp,
				chg_chip->led_on);
		} else {
			if (chip->warp_reply_mcu_bits == 7) {
				ret_info = oplus_chg_get_fast_chg_min_curr_level(
					ret_info, CURR_LIMIT_7BIT_6_3A,
					chip->adapter_sid, true);
			} else {
				ret_info = oplus_chg_get_fast_chg_min_curr_level(
					ret_info, CURR_LIMIT_WARP_6_0A_SWARP_6_5A,
					chip->adapter_sid, false);
			}
		}
		if (chg_chip && chg_chip->chg_ctrl_by_camera &&
		    chg_chip->camera_on) {
			ret_info = oplus_chg_get_fast_chg_min_curr_level(
				ret_info,
				chg_chip->limits.fast_current_camera_level,
				chip->adapter_sid,
				(chip->warp_reply_mcu_bits == 7));
		}
		if (chg_chip && chg_chip->chg_ctrl_by_calling &&
		    chg_chip->calling_on) {
			ret_info = oplus_chg_get_fast_chg_min_curr_level(
				ret_info,
				chg_chip->limits.fast_current_calling_level,
				chip->adapter_sid,
				(chip->warp_reply_mcu_bits == 7));
		}
#else
		if (swarp_led_off_strategy.initialized && chg_chip && !chg_chip->led_on) {
			rc = oplus_chg_get_skin_temp(chip, &skin_temp);
			if (rc < 0) {
				chg_err("can't get skin temp\n");
				skin_temp = 250;
			}
			ret_info_temp = oplus_chg_strategy_get_data(
						&swarp_led_off_strategy,
						&swarp_led_off_strategy.temp_region, skin_temp);
			ret_info = oplus_chg_get_fast_chg_min_curr_level(
							ret_info, ret_info_temp, chip->adapter_sid,
							(chip->warp_reply_mcu_bits == 7));
			chg_err("skin_temp=%d, led_on=%d, ret_info=0x%02x\n", skin_temp,
				chg_chip->led_on, ret_info_temp);
		}
		if (ret_info_cool_down > 0) {
			chg_err("cool down ret_info = 0x%02x\n", ret_info_cool_down);
			ret_info = oplus_chg_get_fast_chg_min_curr_level(ret_info,
					ret_info_cool_down, chip->adapter_sid,
					(chip->warp_reply_mcu_bits == 7));
		}
#endif
		ret_info_temp = oplus_get_allowed_current_max(chip->warp_reply_mcu_bits == 7);
		ret_info = oplus_chg_get_fast_chg_min_curr_level(
				ret_info, ret_info_temp, chip->adapter_sid,
				(chip->warp_reply_mcu_bits == 7));
		pre_ret_info = ret_info;
#endif

		warp_xlog_printk(CHG_LOG_CRTI, "temp_range[%d-%d-%d-%d-%d]", chip->warp_low_temp, chip->warp_little_cold_temp,
			chip->warp_cool_temp, chip->warp_little_cool_temp, chip->warp_normal_low_temp, chip->warp_high_temp);
		warp_xlog_printk(CHG_LOG_CRTI, " volt:%d,temp:%d,soc:%d,current_now:%d,rm:%d, i2c_err:%d, ret_info:%d\n",
			volt, temp, soc, current_now, remain_cap, oplus_get_fg_i2c_err_occured(), ret_info);
	} else if (data == WARP_NOTIFY_NORMAL_TEMP_FULL) {
		warp_xlog_printk(CHG_LOG_CRTI, "WARP_NOTIFY_NORMAL_TEMP_FULL\r\n");
#ifdef OPLUS_CHG_OP_DEF
		chip->fastchg_ignore_event = true;
#endif
		oplus_chg_set_chargerid_switch_val(0);
		chip->vops->set_switch_mode(chip, NORMAL_CHARGER_MODE);
		//del_timer(&chip->watchdog);
		oplus_warp_set_mcu_sleep();
		oplus_warp_del_watchdog_timer(chip);
		ret_info = 0x2;
	} else if (data == WARP_NOTIFY_LOW_TEMP_FULL) {
		if (oplus_warp_get_reply_bits() == 7) {
			chip->temp_range_init = true;
			chip->w_soc_temp_to_mcu = true;
			temp = oplus_chg_match_temp_for_chging();
			soc = oplus_gauge_get_batt_soc();
			oplus_warp_init_temp_range(chip, temp);
			oplus_warp_init_soc_range(chip, soc);
			if (chip->warp_temp_cur_range) {
				ret_info = (chip->soc_range << 4) | (chip->warp_temp_cur_range - 1);
			} else {
				ret_info = (chip->soc_range << 4) | 0x0;
			}
		} else {
			warp_xlog_printk(CHG_LOG_CRTI,
				" fastchg low temp full, switch NORMAL_CHARGER_MODE\n");
			oplus_chg_set_chargerid_switch_val(0);
			chip->vops->set_switch_mode(chip, NORMAL_CHARGER_MODE);
			//del_timer(&chip->watchdog);
			oplus_warp_set_mcu_sleep();
			oplus_warp_del_watchdog_timer(chip);
			ret_info = 0x2;
		}
	} else if (data == WARP_NOTIFY_BAD_CONNECTED || data == WARP_NOTIFY_DATA_UNKNOWN) {
		warp_xlog_printk(CHG_LOG_CRTI,
			" fastchg bad connected, switch NORMAL_CHARGER_MODE\n");
		/*usb bad connected, stop fastchg*/
		chip->btb_temp_over = false;	/*to switch to normal mode*/
		oplus_chg_set_chargerid_switch_val(0);
		chip->vops->set_switch_mode(chip, NORMAL_CHARGER_MODE);
		//del_timer(&chip->watchdog);
		oplus_warp_set_mcu_sleep();
		oplus_warp_del_watchdog_timer(chip);
		ret_info = 0x2;
		charger_abnormal_log = CRITICAL_LOG_WARP_BAD_CONNECTED;
	} else if (data == WARP_NOTIFY_TEMP_OVER) {
		/*fastchg temp over 45 or under 20*/
		warp_xlog_printk(CHG_LOG_CRTI,
			" fastchg temp > 45 or < 20, switch NORMAL_CHARGER_MODE\n");
		oplus_chg_set_chargerid_switch_val(0);
		chip->vops->set_switch_mode(chip, NORMAL_CHARGER_MODE);
		//del_timer(&chip->watchdog);
		oplus_warp_set_mcu_sleep();
		oplus_warp_del_watchdog_timer(chip);
		ret_info = 0x2;
	} else if (data == WARP_NOTIFY_BTB_TEMP_OVER) {
		warp_xlog_printk(CHG_LOG_CRTI, "  btb_temp_over\n");
		chip->fastchg_ing = false;
		chip->btb_temp_over = true;
		chip->fastchg_dummy_started = false;
		chip->fastchg_started = false;
		chip->fastchg_to_normal = false;
#ifdef OPLUS_CHG_OP_DEF
		chip->ffc_chg_start = false;
		chip->fastchg_ignore_event = false;
#endif
		chip->fastchg_to_warm = false;
		adapter_fw_ver_info = false;
		adapter_model_factory = false;
		//mod_timer(&chip->watchdog, jiffies + msecs_to_jiffies(25000));
		oplus_warp_setup_watchdog_timer(chip, 25000);
		ret_info = 0x2;
		charger_abnormal_log = CRITICAL_LOG_WARP_BTB;
	} else if (data == WARP_NOTIFY_FIRMWARE_UPDATE) {
		warp_xlog_printk(CHG_LOG_CRTI, " firmware update, get fw_ver ready!\n");
		/*ready to get fw_ver*/
		fw_ver_info = 1;
		ret_info = 0x2;
	} else if (fw_ver_info && chip->firmware_data != NULL) {
		/*get fw_ver*/
		/*fw in local is large than mcu1503_fw_ver*/
		if (!chip->have_updated
				&& chip->firmware_data[chip->fw_data_count- 4] != data) {
			ret_info = 0x2;
			chip->need_to_up = 1;	/*need to update fw*/
			isnot_power_on = false;
		} else {
			ret_info = 0x1;
			chip->need_to_up = 0;	/*fw is already new, needn't to up*/
			adapter_fw_ver_info = true;
		}
		warp_xlog_printk(CHG_LOG_CRTI, "local_fw:0x%x, need_to_up_fw:%d\n",
			chip->firmware_data[chip->fw_data_count- 4], chip->need_to_up);
		fw_ver_info = 0;
	} else if (adapter_fw_ver_info) {
#if 0
		if (g_adapter_chip->adapter_firmware_data[g_adapter_chip->adapter_fw_data_count - 4] > data
			&& (oplus_gauge_get_batt_soc() > 2) && (chip->vops->is_power_off_charging(chip) == false)
			&& (chip->adapter_update_real != ADAPTER_FW_UPDATE_SUCCESS)) {
#else
		if (0) {
#endif
			ret_info = 0x02;
			chip->adapter_update_real = ADAPTER_FW_NEED_UPDATE;
			chip->adapter_update_report = chip->adapter_update_real;
		} else {
			ret_info = 0x01;
			chip->adapter_update_real = ADAPTER_FW_UPDATE_NONE;
			chip->adapter_update_report = chip->adapter_update_real;
		}
		adapter_fw_ver_info = false;
		//mod_timer(&chip->watchdog, jiffies + msecs_to_jiffies(25000));
		oplus_warp_setup_watchdog_timer(chip, 25000);
	} else if (data == WARP_NOTIFY_ADAPTER_FW_UPDATE) {
		oplus_warp_set_awake(chip, true);
		ret_info = 0x02;
		chip->adapter_update_real = ADAPTER_FW_NEED_UPDATE;
		chip->adapter_update_report = chip->adapter_update_real;
		//mod_timer(&chip->watchdog,  jiffies + msecs_to_jiffies(25000));
		oplus_warp_setup_watchdog_timer(chip, 25000);
	} else {
		oplus_chg_set_chargerid_switch_val(0);
		oplus_chg_clear_chargerid_info();
		chip->vops->set_switch_mode(chip, NORMAL_CHARGER_MODE);
		chip->vops->reset_mcu(chip);
		msleep(100);	/*avoid i2c conflict*/
		chip->allow_reading = true;
		chip->fastchg_started = false;
		chip->fastchg_to_normal = false;
#ifdef OPLUS_CHG_OP_DEF
		chip->ffc_chg_start = false;
		chip->fastchg_ignore_event = false;
#endif
		chip->fastchg_to_warm = false;
		chip->fastchg_ing = false;
		chip->btb_temp_over = false;
		adapter_fw_ver_info = false;
		adapter_model_factory = false;
		data_err = true;
		warp_xlog_printk(CHG_LOG_CRTI,
			" data err, set 0x101, data=0x%x switch off fastchg\n", data);
		goto out;
	}

	if (chip->fastchg_batt_temp_status == BAT_TEMP_EXIT) {
		warp_xlog_printk(CHG_LOG_CRTI, "The temperature is lower than 12 du during the fast charging process\n");
		oplus_chg_set_chargerid_switch_val(0);
		chip->vops->set_switch_mode(chip, NORMAL_CHARGER_MODE);
		oplus_warp_set_mcu_sleep();
		oplus_warp_del_watchdog_timer(chip);
		oplus_warp_set_awake(chip, false);
		oplus_chg_unsuspend_charger();
	}

	msleep(2);
	chip->vops->set_data_sleep(chip);
	chip->vops->reply_mcu_data(chip, ret_info, oplus_gauge_get_device_type_for_warp());

out:
	chip->vops->set_data_active(chip);
	chip->vops->set_clock_active(chip);
	usleep_range(10000, 10000);
	chip->vops->set_clock_sleep(chip);
	usleep_range(25000, 25000);
	if (chip->fastchg_batt_temp_status == BAT_TEMP_EXIT) {
		usleep_range(350000, 350000);
		chip->allow_reading = true;
		chip->fastchg_ing = false;
		chip->fastchg_to_normal = false;
#ifdef OPLUS_CHG_OP_DEF
		chip->ffc_chg_start = false;
		chip->fastchg_ignore_event = false;
#endif
		chip->fastchg_started = false;
		if(oplus_warp_is_battemp_exit()) {
			chip->fastchg_to_warm = true;
			chip->fastchg_dummy_started = false;
		} else {
			chip->fastchg_to_warm = false;
			chip->fastchg_dummy_started = true;
		}
	}
	if (data == WARP_NOTIFY_NORMAL_TEMP_FULL || data == WARP_NOTIFY_BAD_CONNECTED || data == WARP_NOTIFY_DATA_UNKNOWN) {
#ifdef OPLUS_CHG_OP_DEF
		usleep_range(1200000, 1200000);
#else
		usleep_range(350000, 350000);
#endif
		chip->allow_reading = true;
		chip->fastchg_ing = false;
		chip->fastchg_to_normal = true;
		chip->fastchg_started = false;
		chip->fastchg_to_warm = false;
#ifdef OPLUS_CHG_OP_DEF
		chip->ffc_chg_start = true;
		chip->fastchg_ignore_event = false;
#endif
		if (data == WARP_NOTIFY_BAD_CONNECTED || data == WARP_NOTIFY_DATA_UNKNOWN)
			charger_abnormal_log = CRITICAL_LOG_WARP_BAD_CONNECTED;
	} else if (data == WARP_NOTIFY_LOW_TEMP_FULL) {
		if (oplus_warp_get_reply_bits() != 7) {
			usleep_range(350000, 350000);
			chip->allow_reading = true;
			chip->fastchg_ing = false;
			chip->fastchg_low_temp_full = true;
			chip->fastchg_to_normal = false;
#ifdef OPLUS_CHG_OP_DEF
			chip->ffc_chg_start = false;
			chip->fastchg_ignore_event = false;
#endif
			chip->fastchg_started = false;
			chip->fastchg_to_warm = false;
		}
	} else if (data == WARP_NOTIFY_TEMP_OVER) {
		usleep_range(350000, 350000);
		chip->fastchg_ing = false;
		chip->fastchg_to_warm = true;
		chip->allow_reading = true;
		chip->fastchg_low_temp_full = false;
		chip->fastchg_to_normal = false;
		chip->fastchg_started = false;
#ifdef OPLUS_CHG_OP_DEF
		chip->ffc_chg_start = false;
		chip->fastchg_ignore_event = false;
#endif
	}
	if (chip->need_to_up) {
		msleep(500);
		//del_timer(&chip->watchdog);
		chip->vops->fw_update(chip);
		chip->need_to_up = 0;
		phone_mcu_updated = true;
		//mod_timer(&chip->watchdog, jiffies + msecs_to_jiffies(25000));
		oplus_warp_setup_watchdog_timer(chip, 25000);
	}
	if ((data == WARP_NOTIFY_FAST_ABSENT || (data_err && !phone_mcu_updated)
			|| data == WARP_NOTIFY_BTB_TEMP_OVER)
			&& (chip->fastchg_dummy_started == false)) {
		oplus_chg_set_charger_type_unknown();
		oplus_chg_wake_update_work();
	} else if (data == WARP_NOTIFY_NORMAL_TEMP_FULL
			|| data == WARP_NOTIFY_TEMP_OVER
			|| data == WARP_NOTIFY_BAD_CONNECTED
			|| data == WARP_NOTIFY_DATA_UNKNOWN
			|| data == WARP_NOTIFY_LOW_TEMP_FULL
			|| chip->fastchg_batt_temp_status == BAT_TEMP_EXIT) {
		if (oplus_warp_get_reply_bits() != 7 || data != WARP_NOTIFY_LOW_TEMP_FULL) {
			oplus_chg_set_charger_type_unknown();
			oplus_warp_check_charger_out(chip);
		}
	} else if (data == WARP_NOTIFY_BTB_TEMP_OVER) {
		oplus_chg_set_charger_type_unknown();
	}

	if (chip->adapter_update_real != ADAPTER_FW_NEED_UPDATE) {
		chip->vops->eint_regist(chip);
	}

	if (chip->adapter_update_real == ADAPTER_FW_NEED_UPDATE) {
		chip->allow_reading = true;
		chip->fastchg_started = false;
		chip->fastchg_to_normal = false;
#ifdef OPLUS_CHG_OP_DEF
		chip->ffc_chg_start = false;
		chip->fastchg_ignore_event = false;
#endif
		chip->fastchg_low_temp_full = false;
		chip->fastchg_to_warm = false;
		chip->fastchg_ing = false;
		//del_timer(&chip->watchdog);
		oplus_warp_del_watchdog_timer(chip);
		oplus_warp_battery_update();
		oplus_adapter_fw_update();
		oplus_warp_set_awake(chip, false);
	} else if ((data == WARP_NOTIFY_FAST_PRESENT)
			|| (data == WARP_NOTIFY_ALLOW_READING_IIC)
			|| (data == WARP_NOTIFY_BTB_TEMP_OVER)) {
		oplus_warp_battery_update();
		if (oplus_warp_get_reset_active_status() != 1
			&& data == WARP_NOTIFY_FAST_PRESENT) {
			chip->allow_reading = true;
			chip->fastchg_started = false;
			chip->fastchg_to_normal = false;
			chip->fastchg_to_warm = false;
			chip->fastchg_ing = false;
			chip->btb_temp_over = false;
			adapter_fw_ver_info = false;
			adapter_model_factory = false;
			chip->fastchg_dummy_started = false;
			oplus_chg_set_charger_type_unknown();
			oplus_chg_clear_chargerid_info();
			oplus_chg_set_chargerid_switch_val(0);
			chip->vops->set_switch_mode(chip, NORMAL_CHARGER_MODE);
			oplus_warp_del_watchdog_timer(chip);
		}
	} else if ((data == WARP_NOTIFY_LOW_TEMP_FULL)
		|| (data == WARP_NOTIFY_FAST_ABSENT)
		|| (data == WARP_NOTIFY_NORMAL_TEMP_FULL)
		|| (data == WARP_NOTIFY_BAD_CONNECTED)
		|| (data == WARP_NOTIFY_DATA_UNKNOWN)
		|| (data == WARP_NOTIFY_TEMP_OVER) || oplus_warp_is_battemp_exit()) {
		if (oplus_warp_get_reply_bits() != 7 || data != WARP_NOTIFY_LOW_TEMP_FULL) {
			if (!oplus_warp_is_battemp_exit()) {
				oplus_warp_reset_temp_range(chip);
			}
			oplus_warp_battery_update();
#ifdef CHARGE_PLUG_IN_TP_AVOID_DISTURB
			charge_plug_tp_avoid_distrub(1, is_oplus_fast_charger);
#endif
			oplus_warp_set_awake(chip, false);
		}
	} else if (data_err) {
		data_err = false;
		oplus_warp_reset_temp_range(chip);
		oplus_warp_battery_update();
#ifdef CHARGE_PLUG_IN_TP_AVOID_DISTURB
		charge_plug_tp_avoid_distrub(1, is_oplus_fast_charger);
#endif
		oplus_warp_set_awake(chip, false);
	}
	if (chip->fastchg_started == false
			&& chip->fastchg_dummy_started == false
			&& chip->fastchg_to_normal == false
			&& chip->fastchg_to_warm == false){
		chip->fast_chg_type = FASTCHG_CHARGER_TYPE_UNKOWN;
#ifdef OPLUS_CHG_OP_DEF
		chip->adapter_sid = 0;
#endif
	}

}

void fw_update_thread(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_warp_chip *chip = container_of(dwork,
		struct oplus_warp_chip, fw_update_work);
	const struct firmware *fw = NULL;
	int ret = 1;
	int retry = 5;
	char version[10];

	if (!chip->vops) {
		chg_err("vops is null\n");
		return;
	}

	if(chip->warp_fw_update_newmethod) {
		if(oplus_is_rf_ftm_mode()) {
			chip->vops->fw_check_then_recover(chip);
			return;
		}
		 do {
			ret = request_firmware_select(&fw, chip->fw_path, chip->dev);
			if (!ret) {
				break;
			}
		} while((ret < 0) && (--retry > 0));
		chg_debug(" retry times %d, chip->fw_path[%s]\n", 5 - retry, chip->fw_path);
		if(!ret) {
			chip->firmware_data =  fw->data;
			chip->fw_data_count =  fw->size;
			chip->fw_data_version = chip->firmware_data[chip->fw_data_count - 4];
			chg_debug("count:0x%x, version:0x%x\n",
				chip->fw_data_count,chip->fw_data_version);
			if(chip->vops->fw_check_then_recover) {
				ret = chip->vops->fw_check_then_recover(chip);
				sprintf(version,"%d", chip->fw_data_version);
				sprintf(chip->manufacture_info.version,"%s", version);
				if (ret == FW_CHECK_MODE) {
					chg_debug("update finish, then clean fastchg_dummy , fastchg_started, watch_dog\n");
					chip->fastchg_dummy_started = false;
					chip->fastchg_started = false;
					chip->allow_reading = true;
					del_timer(&chip->watchdog);
				}
			}
			release_firmware(fw);
			chip->firmware_data = NULL;
		} else {
			chg_debug("%s: fw_name request failed, %d\n", __func__, ret);
		}
	}else {
		ret = chip->vops->fw_check_then_recover(chip);
		if (ret == FW_CHECK_MODE) {
			chg_debug("update finish, then clean fastchg_dummy , fastchg_started, watch_dog\n");
			chip->fastchg_dummy_started = false;
			chip->fastchg_started = false;
			chip->allow_reading = true;
			del_timer(&chip->watchdog);
		}
	}
	chip->mcu_update_ing = false;
	oplus_chg_unsuspend_charger();
	oplus_warp_set_awake(chip, false);
}

void fw_update_thread_fix(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_warp_chip *chip =
		container_of(dwork, struct oplus_warp_chip, fw_update_work_fix);
	const struct firmware *fw = NULL;
	int ret = 1;
	int retry = 5;
	char version[10];

	if (chip->warp_fw_update_newmethod) {
		if (oplus_is_rf_ftm_mode()) {
			chip->vops->fw_check_then_recover_fix(chip);
			return;
		}
		do {
			ret = request_firmware_select(&fw, chip->fw_path,
						      chip->dev);
			if (!ret) {
				break;
			}
		} while ((ret < 0) && (--retry > 0));
		chg_debug(" retry times %d, chip->fw_path[%s]\n", 5 - retry,
			  chip->fw_path);
		if (!ret) {
			chip->firmware_data = fw->data;
			chip->fw_data_count = fw->size;
			chip->fw_data_version =
				chip->firmware_data[chip->fw_data_count - 4];
			chg_debug("count:0x%x, version:0x%x\n",
				  chip->fw_data_count, chip->fw_data_version);
			if (chip->vops->fw_check_then_recover_fix) {
				ret = chip->vops->fw_check_then_recover_fix(
					chip);
				sprintf(version, "%d", chip->fw_data_version);
				sprintf(chip->manufacture_info.version, "%s",
					version);
				if (ret == FW_CHECK_MODE) {
					chg_debug("update finish, then clean fastchg_dummy , fastchg_started, watch_dog\n");
					chip->fastchg_dummy_started = false;
					chip->fastchg_started = false;
					chip->allow_reading = true;
					del_timer(&chip->watchdog);
				}
			}
			release_firmware(fw);
			chip->firmware_data = NULL;
		} else {
			chg_debug("%s: fw_name request failed, %d\n", __func__,
				  ret);
		}
	} else {
		ret = chip->vops->fw_check_then_recover_fix(chip);
		if (ret == FW_CHECK_MODE) {
			chg_debug("update finish, then clean fastchg_dummy , fastchg_started, watch_dog\n");
			chip->fastchg_dummy_started = false;
			chip->fastchg_started = false;
			chip->allow_reading = true;
			del_timer(&chip->watchdog);
		}
	}
	chip->mcu_update_ing = false;
	oplus_chg_unsuspend_charger();
	oplus_warp_set_awake(chip, false);
}

#define FASTCHG_FW_INTERVAL_INIT	   1000	/*  1S     */
void oplus_warp_fw_update_work_init(struct oplus_warp_chip *chip)
{
	INIT_DELAYED_WORK(&chip->fw_update_work, fw_update_thread);
	schedule_delayed_work(&chip->fw_update_work, round_jiffies_relative(msecs_to_jiffies(FASTCHG_FW_INTERVAL_INIT)));
}

void oplus_warp_fw_update_work_plug_in(void)
{
	if (!g_warp_chip)
		return;
	chg_err("%s asic didn't work, update fw!\n", __func__);
	INIT_DELAYED_WORK(&g_warp_chip->fw_update_work_fix, fw_update_thread_fix);
	schedule_delayed_work(&g_warp_chip->fw_update_work_fix, round_jiffies_relative(msecs_to_jiffies(FASTCHG_FW_INTERVAL_INIT)));
}

void oplus_warp_shedule_fastchg_work(void)
{
	if (!g_warp_chip) {
		chg_err(" g_warp_chip is NULL\n");
	} else {
		schedule_delayed_work(&g_warp_chip->fastchg_work, 0);
	}
}
static ssize_t proc_fastchg_fw_update_write(struct file *file, const char __user *buff,
		size_t len, loff_t *data)
{
	struct oplus_warp_chip *chip = PDE_DATA(file_inode(file));
	char write_data[32] = {0};

	if (len > sizeof(write_data)) {
		return -EINVAL;
	}

	if (copy_from_user(&write_data, buff, len)) {
		chg_err("fastchg_fw_update error.\n");
		return -EFAULT;
	}

	if (write_data[0] == '1') {
		chg_err("fastchg_fw_update\n");
		chip->fw_update_flag = 1;
		schedule_delayed_work(&chip->fw_update_work, 0);
	} else {
		chip->fw_update_flag = 0;
		chg_err("Disable fastchg_fw_update\n");
	}

	return len;
}

static ssize_t proc_fastchg_fw_update_read(struct file *file, char __user *buff,
		size_t count, loff_t *off)
{
	struct oplus_warp_chip *chip = PDE_DATA(file_inode(file));
	char page[256] = {0};
	char read_data[32] = {0};
	int len = 0;

	if(chip->fw_update_flag == 1) {
		read_data[0] = '1';
	} else {
		read_data[0] = '0';
	}
	len = sprintf(page,"%s",read_data);
	if(len > *off) {
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


static const struct file_operations fastchg_fw_update_proc_fops = {
	.write = proc_fastchg_fw_update_write,
	.read  = proc_fastchg_fw_update_read,
};

static int init_proc_fastchg_fw_update(struct oplus_warp_chip *chip)
{
	struct proc_dir_entry *p = NULL;

	p = proc_create_data("fastchg_fw_update", 0664, NULL, &fastchg_fw_update_proc_fops,chip);
	if (!p) {
		pr_err("proc_create fastchg_fw_update_proc_fops fail!\n");
	}
	return 0;
}

static int init_warp_proc(struct oplus_warp_chip *chip)
{
	strcpy(chip->manufacture_info.version, "0");
#ifndef OPLUS_CHG_OP_DEF
	if (get_warp_mcu_type(chip) == OPLUS_WARP_MCU_HWID_STM8S) {
		snprintf(chip->fw_path, MAX_FW_NAME_LENGTH, "fastchg/%d/oplus_warp_fw.bin", get_project());
	} else if (get_warp_mcu_type(chip) == OPLUS_WARP_MCU_HWID_N76E) {
		snprintf(chip->fw_path, MAX_FW_NAME_LENGTH, "fastchg/%d/oplus_warp_fw_n76e.bin", get_project());
	} else if (get_warp_mcu_type(chip) == OPLUS_WARP_ASIC_HWID_RK826) {
		snprintf(chip->fw_path, MAX_FW_NAME_LENGTH, "fastchg/%d/oplus_warp_fw_rk826.bin", get_project());
	} else {
		snprintf(chip->fw_path, MAX_FW_NAME_LENGTH, "fastchg/%d/oplus_warp_fw_op10.bin", get_project());
	}
#endif
	memcpy(chip->manufacture_info.manufacture, chip->fw_path, MAX_FW_NAME_LENGTH);
	register_devinfo("fastchg", &chip->manufacture_info);
	init_proc_fastchg_fw_update(chip);
	chg_debug(" version:%s, fw_path:%s\n", chip->manufacture_info.version, chip->fw_path);
	return 0;
}
void oplus_warp_init(struct oplus_warp_chip *chip)
{
	int ret = 0;

#ifndef OPLUS_CHG_OP_DEF
	chip->detach_unexpectly = false;
	chip->allow_reading = true;
	chip->fastchg_started = false;
	chip->fastchg_dummy_started = false;
	chip->fastchg_ing = false;
	chip->fastchg_to_normal = false;
	chip->fastchg_to_warm = false;
	chip->fastchg_allow = false;
	chip->fastchg_low_temp_full = false;
	chip->have_updated = false;
	chip->need_to_up = false;
	chip->btb_temp_over = false;
	chip->adapter_update_real = ADAPTER_FW_UPDATE_NONE;
	chip->adapter_update_report = chip->adapter_update_real;
	chip->mcu_update_ing = true;
	chip->mcu_boot_by_gpio = false;
	chip->dpdm_switch_mode = NORMAL_CHARGER_MODE;
	chip->fast_chg_type = FASTCHG_CHARGER_TYPE_UNKOWN;
	/*chip->batt_psy = power_supply_get_by_name("battery");*/
	chip->disable_adapter_output = false;
	chip->set_warp_current_limit = WARP_MAX_CURRENT_NO_LIMIT;
	chip->reset_adapter = false;
	chip->suspend_charger = false;
	chip->temp_range_init = false;
	chip->w_soc_temp_to_mcu = false;
	oplus_warp_init_watchdog_timer(chip);

	oplus_warp_awake_init(chip);
	INIT_DELAYED_WORK(&chip->fastchg_work, oplus_warp_fastchg_func);
	INIT_DELAYED_WORK(&chip->check_charger_out_work, check_charger_out_work_func);
	INIT_WORK(&chip->warp_watchdog_work, warp_watchdog_work_func);
	g_warp_chip = chip;
#else /* OPLUS_CHG_OP_DEF */
	chip->mcu_update_ing = true;
#endif /* OPLUS_CHG_OP_DEF */
	chip->vops->eint_regist(chip);
	if(chip->warp_fw_update_newmethod) {
		if(oplus_is_rf_ftm_mode()) {
			return;
		}
		INIT_DELAYED_WORK(&chip->fw_update_work, fw_update_thread);
		INIT_DELAYED_WORK(&chip->fw_update_work_fix, fw_update_thread_fix);
		//Alloc fw_name/devinfo memory space

		chip->fw_path = kzalloc(MAX_FW_NAME_LENGTH, GFP_KERNEL);
		if (chip->fw_path == NULL) {
			ret = -ENOMEM;
			chg_err("panel_data.fw_name kzalloc error\n");
			goto manu_fwpath_alloc_err;
		}
		chip->manufacture_info.version = kzalloc(MAX_DEVICE_VERSION_LENGTH, GFP_KERNEL);
		if (chip->manufacture_info.version == NULL) {
			ret = -ENOMEM;
			chg_err("manufacture_info.version kzalloc error\n");
			goto manu_version_alloc_err;
		}
		chip->manufacture_info.manufacture = kzalloc(MAX_DEVICE_MANU_LENGTH, GFP_KERNEL);
		if (chip->manufacture_info.manufacture == NULL) {
			ret = -ENOMEM;
			chg_err("panel_data.manufacture kzalloc error\n");
			goto manu_info_alloc_err;
		}
		init_warp_proc(chip);
		return;

manu_fwpath_alloc_err:
		kfree(chip->fw_path);

manu_info_alloc_err:
		kfree(chip->manufacture_info.manufacture);

manu_version_alloc_err:
		kfree(chip->manufacture_info.version);
	}
	return ;
}

bool oplus_warp_wake_fastchg_work(struct oplus_warp_chip *chip)
{
	return schedule_delayed_work(&chip->fastchg_work, 0);
}

void oplus_warp_print_log(void)
{
	if (!g_warp_chip) {
		return;
	}
	warp_xlog_printk(CHG_LOG_CRTI, "WARP[ %d / %d / %d / %d / %d / %d]\n",
		g_warp_chip->fastchg_allow, g_warp_chip->fastchg_started, g_warp_chip->fastchg_dummy_started,
		g_warp_chip->fastchg_to_normal, g_warp_chip->fastchg_to_warm, g_warp_chip->btb_temp_over);
}

bool oplus_warp_get_allow_reading(void)
{
	if (!g_warp_chip) {
		return true;
	} else {
		if (g_warp_chip->support_warp_by_normal_charger_path
				&& g_warp_chip->fast_chg_type == CHARGER_SUBTYPE_FASTCHG_WARP) {
			return true;
		} else {
			return g_warp_chip->allow_reading;
		}
	}
}

bool oplus_warp_get_fastchg_started(void)
{
	if (!g_warp_chip) {
		return false;
	} else {
		return g_warp_chip->fastchg_started;
	}
}

bool oplus_warp_get_fastchg_ing(void)
{
	if (!g_warp_chip) {
		return false;
	} else {
		return g_warp_chip->fastchg_ing;
	}
}

bool oplus_warp_get_fastchg_allow(void)
{
	if (!g_warp_chip) {
		return false;
	} else {
		return g_warp_chip->fastchg_allow;
	}
}

void oplus_warp_set_fastchg_allow(int enable)
{
	if (!g_warp_chip) {
		return;
	} else {
		g_warp_chip->fastchg_allow = enable;
	}
}

bool oplus_warp_get_fastchg_to_normal(void)
{
	if (!g_warp_chip) {
		return false;
	} else {
		return g_warp_chip->fastchg_to_normal;
	}
}

void oplus_warp_set_fastchg_to_normal_false(void)
{
	if (!g_warp_chip) {
		return;
	} else {
		g_warp_chip->fastchg_to_normal = false;
	}
}

#ifdef OPLUS_CHG_OP_DEF
bool oplus_warp_get_ffc_chg_start(void)
{
	if (!g_warp_chip) {
		return false;
	} else {
		return g_warp_chip->ffc_chg_start;
	}
}

void oplus_warp_set_ffc_chg_start_false(void)
{
	if (!g_warp_chip) {
		return;
	} else {
		g_warp_chip->ffc_chg_start = false;
	}
}
bool oplus_warp_ignore_event(void)
{
	if (!g_warp_chip) {
		return false;
	} else {
		return g_warp_chip->fastchg_ignore_event;
	}
}

#endif

void oplus_warp_set_fastchg_type_unknow(void)
{
	if (!g_warp_chip) {
		return;
	} else {
		g_warp_chip->fast_chg_type = FASTCHG_CHARGER_TYPE_UNKOWN;
#ifdef OPLUS_CHG_OP_DEF
		g_warp_chip->adapter_sid = 0;
#endif
	}
}

bool oplus_warp_get_fastchg_to_warm(void)
{
	if (!g_warp_chip) {
		return false;
	} else {
		return g_warp_chip->fastchg_to_warm;
	}
}

void oplus_warp_set_fastchg_to_warm_false(void)
{
	if (!g_warp_chip) {
		return;
	} else {
		g_warp_chip->fastchg_to_warm = false;
	}
}

bool oplus_warp_get_fastchg_low_temp_full()
{
	if (!g_warp_chip) {
		return false;
	} else {
		return g_warp_chip->fastchg_low_temp_full;
	}
}

void oplus_warp_set_fastchg_low_temp_full_false(void)
{
	if (!g_warp_chip) {
		return;
	} else {
		g_warp_chip->fastchg_low_temp_full = false;
	}
}

bool oplus_warp_get_warp_multistep_adjust_current_support(void)
{
	if (!g_warp_chip) {
		return false;
	} else {
		return g_warp_chip->warp_multistep_adjust_current_support;
	}
}

bool oplus_warp_get_fastchg_dummy_started(void)
{
	if (!g_warp_chip) {
		return false;
	} else {
		return g_warp_chip->fastchg_dummy_started;
	}
}

void oplus_warp_set_fastchg_dummy_started_false(void)
{
	if (!g_warp_chip) {
		return;
	} else {
		g_warp_chip->fastchg_dummy_started = false;
	}
}

int oplus_warp_get_adapter_update_status(void)
{
	if (!g_warp_chip) {
		return ADAPTER_FW_UPDATE_NONE;
	} else {
		return g_warp_chip->adapter_update_report;
	}
}

int oplus_warp_get_adapter_update_real_status(void)
{
	if (!g_warp_chip) {
		return ADAPTER_FW_UPDATE_NONE;
	} else {
		return g_warp_chip->adapter_update_real;
	}
}

bool oplus_warp_get_btb_temp_over(void)
{
	if (!g_warp_chip) {
		return false;
	} else {
		return g_warp_chip->btb_temp_over;
	}
}

void oplus_warp_reset_fastchg_after_usbout(void)
{
	if (!g_warp_chip || !g_warp_chip->vops) {
		return;
	} else {
		g_warp_chip->vops->reset_fastchg_after_usbout(g_warp_chip);
	}
}

#ifndef OPLUS_CHG_OP_DEF
void oplus_warp_switch_fast_chg(void)
{
	if (!g_warp_chip || !g_warp_chip->vops) {
		return;
	} else {
		g_warp_chip->vops->switch_fast_chg(g_warp_chip);
	}
}
#else
bool oplus_warp_switch_fast_chg(void)
{
	if (!g_warp_chip || !g_warp_chip->vops) {
		return false;
	} else {
		return g_warp_chip->vops->switch_fast_chg(g_warp_chip);
	}
}
#endif

void oplus_warp_set_ap_clk_high(void)
{
	if (!g_warp_chip || !g_warp_chip->vops) {
		return;
	} else {
		g_warp_chip->vops->set_clock_sleep(g_warp_chip);
	}
}

void oplus_warp_reset_mcu(void)
{
	if (!g_warp_chip || !g_warp_chip->vops) {
		return;
	} else {
		g_warp_chip->vops->reset_mcu(g_warp_chip);
	}
}

int oplus_warp_check_asic_fw_status(void)
{
	if (!g_warp_chip || !g_warp_chip->vops ||
	    !g_warp_chip->vops->check_asic_fw_status) {
		return -EINVAL;
	} else {
		if (g_warp_chip->vops->check_asic_fw_status)
			return g_warp_chip->vops->check_asic_fw_status(
				g_warp_chip);
		else
			return -EINVAL;
	}
}

void oplus_warp_set_mcu_sleep(void)
{
	if (!g_warp_chip || !g_warp_chip->vops) {
		return;
	} else {
		if (g_warp_chip->vops->set_mcu_sleep)
			g_warp_chip->vops->set_mcu_sleep(g_warp_chip);
	}
}

bool oplus_warp_check_chip_is_null(void)
{
	if (!g_warp_chip) {
		return true;
	} else {
		return false;
	}
}

int oplus_warp_get_warp_switch_val(void)
{
	if (!g_warp_chip || !g_warp_chip->vops) {
		return 0;
	} else {
		return g_warp_chip->vops->get_switch_gpio_val(g_warp_chip);
	}
}

void oplus_warp_uart_init(void)
{
	struct oplus_warp_chip *chip = g_warp_chip;
	if (!chip || !chip->vops) {
		return ;
	} else {
		chip->vops->set_data_active(chip);
		chip->vops->set_clock_sleep(chip);
	}
}

int oplus_warp_get_uart_tx(void)
{
	struct oplus_warp_chip *chip = g_warp_chip;
	if (!chip || !chip->vops) {
		return -1;
	} else {
		return chip->vops->get_clk_gpio_num(chip);
	}
}

int oplus_warp_get_uart_rx(void)
{
	struct oplus_warp_chip *chip = g_warp_chip;
	if (!chip || !chip->vops) {
		return -1;
	} else {
		return chip->vops->get_data_gpio_num(chip);
	}
}


void oplus_warp_uart_reset(void)
{
	struct oplus_warp_chip *chip = g_warp_chip;
	if (!chip || !chip->vops) {
		return ;
	} else {
		chip->vops->eint_regist(chip);
		oplus_chg_set_chargerid_switch_val(0);
		chip->vops->set_switch_mode(chip, NORMAL_CHARGER_MODE);
		chip->vops->reset_mcu(chip);
	}
}

void oplus_warp_set_adapter_update_real_status(int real)
{
	struct oplus_warp_chip *chip = g_warp_chip;
	if (!chip) {
		return ;
	} else {
		chip->adapter_update_real = real;
	}
}

void oplus_warp_set_adapter_update_report_status(int report)
{
	struct oplus_warp_chip *chip = g_warp_chip;
	if (!chip) {
		return ;
	} else {
		chip->adapter_update_report = report;
	}
}

int oplus_warp_get_fast_chg_type(void)
{
	struct oplus_warp_chip *chip = g_warp_chip;
	if (!chip) {
		return FASTCHG_CHARGER_TYPE_UNKOWN;
	} else {
		return chip->fast_chg_type;
	}
}

#ifdef OPLUS_CHG_OP_DEF
unsigned int oplus_warp_get_adapter_sid(void)
{
	struct oplus_warp_chip *chip = g_warp_chip;
	if (!chip) {
		return 0;
	} else {
		return chip->adapter_sid;
	}
}
#endif

#ifdef OPLUS_CHG_OP_DEF
int oplus_warp_convert_fast_chg_type(int fast_chg_type)
#else
static int oplus_warp_convert_fast_chg_type(int fast_chg_type)
#endif
{
	struct oplus_warp_chip *chip = g_warp_chip;
	enum e_fastchg_power fastchg_pwr_type;

	if (!chip)
		return FASTCHG_CHARGER_TYPE_UNKOWN;

	if (chip->support_warp_by_normal_charger_path) {
		fastchg_pwr_type = FASTCHG_POWER_10V6P5A_TWO_BAT_SWARP;
	} else {
		fastchg_pwr_type = FASTCHG_POWER_UNKOWN;
	}

	switch(fast_chg_type) {
		case FASTCHG_CHARGER_TYPE_UNKOWN:
			return fast_chg_type;
			break;

		case 0x11:		/*50w*/
		case 0x12:		/*50w*/
		case 0x21:		/*50w*/
		case 0x31:		/*50w*/
		case 0x33:		/*50w*/
		case 0x62:		/*reserve for swarp*/
			if (fastchg_pwr_type == FASTCHG_POWER_11V3A_FLASHCHARGER
				|| fastchg_pwr_type == FASTCHG_POWER_10V6P5A_TWO_BAT_SWARP)
				return fast_chg_type;
			return CHARGER_SUBTYPE_FASTCHG_WARP;
			break;

		case 0x14:		/*65w*/
		case 0x32:		/*65W*/
		case 0x35:		/*65w*/
		case 0x36:		/*65w*/
		case 0x63:		/*reserve for swarp 2.0*/
		case 0x64:		/*reserve for swarp 2.0*/
		case 0x65:		/*reserve for swarp 2.0*/
		case 0x66:		/*reserve for swarp 2.0*/
		case 0x69:		/*reserve for swarp 2.0*/
		case 0x6A:		/*reserve for swarp 2.0*/
		case 0x6B:		/*reserve for swarp 2.0*/
		case 0x6C:		/*reserve for swarp 2.0*/
		case 0x6D:		/*reserve for swarp 2.0*/
		case 0x6E:		/*reserve for swarp 2.0*/
			if (fastchg_pwr_type == FASTCHG_POWER_11V3A_FLASHCHARGER
				|| fastchg_pwr_type == FASTCHG_POWER_10V6P5A_TWO_BAT_SWARP)
				return fast_chg_type;
			return CHARGER_SUBTYPE_FASTCHG_WARP;
			break;

		case 0x0F:		/*special code*/
		case 0x1F:		/*special code*/
		case 0x3F:		/*special code*/
		case 0x7F:		/*special code*/
			return fast_chg_type;
			break;

	case 0x34:
		if (fastchg_pwr_type == FASTCHG_POWER_10V6P5A_TWO_BAT_SWARP)
			return fast_chg_type;
		return CHARGER_SUBTYPE_FASTCHG_WARP;
	case 0x13:
	case 0x19:
	case 0x29:
	case 0x41:
	case 0x42:
	case 0x43:
	case 0x44:
	case 0x45:
	case 0x46:
		return CHARGER_SUBTYPE_FASTCHG_WARP;
	case 0x61:		/* 11V3A*/
	case 0x49:		/*for 11V3A adapter temp*/
	case 0x4A:		/*for 11V3A adapter temp*/
	case 0x4B:		/*for 11V3A adapter temp*/
	case 0x4C:		/*for 11V3A adapter temp*/
	case 0x4D:		/*for 11V3A adapter temp*/
	case 0x4E:		/*for 11V3A adapter temp*/
		fast_chg_type = 0x61;
		if (fastchg_pwr_type == FASTCHG_POWER_11V3A_FLASHCHARGER
			|| fastchg_pwr_type == FASTCHG_POWER_10V6P5A_TWO_BAT_SWARP)
			return fast_chg_type;
		return CHARGER_SUBTYPE_FASTCHG_WARP;

	default:
		return CHARGER_SUBTYPE_FASTCHG_SWARP;
	}

	return FASTCHG_CHARGER_TYPE_UNKOWN;
}

void oplus_warp_set_disable_adapter_output(bool disable)
{
	struct oplus_warp_chip *chip = g_warp_chip;
	if (!chip) {
		return ;
	} else {
		chip->disable_adapter_output = disable;
	}
	chg_err(" chip->disable_adapter_output:%d\n", chip->disable_adapter_output);
}

void oplus_warp_set_warp_max_current_limit(int current_level)
{
	struct oplus_warp_chip *chip = g_warp_chip;
	if (!chip) {
		return ;
	} else {
		chip->set_warp_current_limit = current_level;
	}
}

void oplus_warp_set_warp_chargerid_switch_val(int value)
{
	struct oplus_warp_chip *chip = g_warp_chip;

	if (!chip || !chip->vops) {
		return;
	} else if (chip->vops->set_warp_chargerid_switch_val) {
		chip->vops->set_warp_chargerid_switch_val(chip, value);
	}
}

int oplus_warp_get_reply_bits(void)
{
	struct oplus_warp_chip *chip = g_warp_chip;

	if (!chip) {
		return 0;
	} else {
		return chip->warp_reply_mcu_bits;
	}
}

void oplus_warp_turn_off_fastchg(void)
{
	struct oplus_warp_chip *chip = g_warp_chip;
	if (!chip || !chip->vops) {
		return;
	}

	chg_err("oplus_warp_turn_off_fastchg\n");
	oplus_chg_set_chargerid_switch_val(0);
	oplus_warp_switch_mode(NORMAL_CHARGER_MODE);
	if (chip->vops->set_mcu_sleep) {
		chip->vops->set_mcu_sleep(chip);

		chip->allow_reading = true;
		chip->fastchg_started = false;
		chip->fastchg_to_normal = false;
#ifdef OPLUS_CHG_OP_DEF
		chip->ffc_chg_start = false;
		chip->fastchg_ignore_event = false;
		chip->adapter_sid = 0;
#endif
		chip->fastchg_to_warm = false;
		chip->fastchg_ing = false;
		chip->btb_temp_over = false;
		chip->fastchg_dummy_started = false;
		chip->fast_chg_type = FASTCHG_CHARGER_TYPE_UNKOWN;

		oplus_chg_clear_chargerid_info();
		oplus_warp_del_watchdog_timer(chip);
		oplus_chg_set_charger_type_unknown();
		oplus_chg_wake_update_work();
		oplus_warp_set_awake(chip, false);
	}
}

bool opchg_get_mcu_update_state(void)
{
	struct oplus_warp_chip *chip = g_warp_chip;
	if (!chip) {
		return false;
	}
	return chip->mcu_update_ing;
}


void oplus_warp_get_warp_chip_handle(struct oplus_warp_chip **chip) {
	*chip = g_warp_chip;
}

void oplus_warp_reset_temp_range(struct oplus_warp_chip *chip)
{
	if (chip != NULL) {
		chip->temp_range_init = false;
		chip->w_soc_temp_to_mcu = false;
		chip->warp_little_cold_temp = chip->warp_little_cold_temp_default;
		chip->warp_cool_temp = chip->warp_cool_temp_default;
		chip->warp_little_cool_temp = chip->warp_little_cool_temp_default;
		chip->warp_normal_low_temp = chip->warp_normal_low_temp_default;
	}
}


bool oplus_warp_get_detach_unexpectly(void)
{
	if (!g_warp_chip) {
		return false;
	} else {
		chg_err("detach_unexpectly = %d\n",g_warp_chip->detach_unexpectly);
		return g_warp_chip->detach_unexpectly;
	}
}

void oplus_warp_set_detach_unexpectly(bool val)
{
	if (!g_warp_chip) {
		return ;
	} else {
		 g_warp_chip->detach_unexpectly = val;
		chg_err("detach_unexpectly = %d\n",g_warp_chip->detach_unexpectly);
	}
}

void oplus_warp_set_disable_real_fast_chg(bool val)
{
	if (!g_warp_chip) {
		return ;
	} else {
		g_warp_chip->disable_real_fast_chg= val;
		chg_err("disable_real_fast_chg = %d\n",g_warp_chip->disable_real_fast_chg);
	}
}

bool oplus_warp_get_reset_adapter_st(void)
{
	if (!g_warp_chip) {
		return false;
	} else {
		return g_warp_chip->reset_adapter;
	}
}

int oplus_warp_get_reset_active_status(void)
{
	int active_level = 0;
	int mcu_hwid_type = OPLUS_WARP_MCU_HWID_UNKNOW;

	if (!g_warp_chip || !g_warp_chip->vops) {
		return -EINVAL;
	} else {
		mcu_hwid_type = get_warp_mcu_type(g_warp_chip);
#ifndef OPLUS_CHG_OP_DEF
		if (mcu_hwid_type == OPLUS_WARP_ASIC_HWID_RK826
			|| mcu_hwid_type == OPLUS_WARP_ASIC_HWID_OP10
			|| mcu_hwid_type == OPLUS_WARP_ASIC_HWID_RT5125) {
#else
		if (mcu_hwid_type == OPLUS_WARP_ASIC_HWID_RK826
			|| mcu_hwid_type == OPLUS_WARP_ASIC_HWID_OP10
			|| mcu_hwid_type == OPLUS_WARP_ASIC_HWID_RT5125
			|| is_warp_asic_hwid_check_by_i2c(g_warp_chip)) {
#endif
			active_level = 1;
		}
		if (active_level == g_warp_chip->vops->get_reset_gpio_val(g_warp_chip)) {
			return 1;
		} else {
			return 0;
		}
	}
}

#ifdef OPLUS_CHG_DEBUG
int oplus_warp_user_fw_upgrade(u8 *fw_buf, u32 fw_size)
{
	struct oplus_warp_chip *chip = g_warp_chip;
	if (!chip || !chip->vops) {
		chg_err("warp chip is NULL\n");
		return -ENODEV;
	}
	if (!chip->vops->user_fw_upgrade) {
		chg_err("can't support user fw upgrade\n");
		return -EINVAL;
	}

	return chip->vops->user_fw_upgrade(chip, fw_buf, fw_size);
}
#endif

#ifdef OPLUS_CHG_OP_DEF
int oplus_chg_asic_register(struct oplus_chg_asic *asic)
{
	struct oplus_warp_chip *chip;

	if (asic ==NULL) {
		chg_err("asic is NULL\n");
		return -ENODEV;
	}

	chip = asic->data;
	mutex_lock(&chip->asic_list_lock);
	list_add(&asic->list, &chip->asic_list);
	mutex_unlock(&chip->asic_list_lock);

	return 0;
}

int oplus_chg_asic_unregister(struct oplus_chg_asic *asic)
{
	struct oplus_warp_chip *chip;

	if (asic ==NULL) {
		chg_err("asic is NULL\n");
		return -ENODEV;
	}

	chip = asic->data;
	mutex_lock(&chip->asic_list_lock);
	list_del(&asic->list);
	mutex_unlock(&chip->asic_list_lock);

	return 0;
}

static void oplus_chg_asic_init_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_warp_chip *chip = container_of(dwork,
				struct oplus_warp_chip, asic_init_work);
	struct oplus_chg_asic *asic;

	mutex_lock(&chip->asic_list_lock);
	if (chip->asic_list.next != &chip->asic_list)
		asic = container_of(chip->asic_list.next, struct oplus_chg_asic, list);
	else
		asic = NULL;
	mutex_unlock(&chip->asic_list_lock);

	if (asic == NULL)
		return;

	chip->client = asic->client;
	if (!asic->is_used(asic->client)) {
		oplus_chg_asic_unregister(asic);
		schedule_delayed_work(&chip->asic_init_work, 0);
		return;
	}

	chip->batt_type_4400mv = asic->batt_type_4400mv;
	chip->warp_fw_type = asic->warp_fw_type;
	chip->warp_reply_mcu_bits = asic->warp_reply_mcu_bits;
	chip->firmware_data = asic->firmware_data;
	chip->fw_data_count = asic->fw_data_count;
	chip->fw_data_version = asic->fw_data_version;

	chip->warp_cool_bat_volt = asic->warp_cool_bat_volt;
	chip->warp_little_cool_bat_volt = asic->warp_little_cool_bat_volt;
	chip->warp_normal_bat_volt = asic->warp_normal_bat_volt;
	chip->warp_warm_bat_volt = asic->warp_warm_bat_volt;
	chip->warp_cool_bat_suspend_volt = asic->warp_cool_bat_suspend_volt;
	chip->warp_little_cool_bat_suspend_volt = asic->warp_little_cool_bat_suspend_volt;
	chip->warp_normal_bat_suspend_volt = asic->warp_normal_bat_suspend_volt;
	chip->warp_warm_bat_suspend_volt = asic->warp_warm_bat_suspend_volt;

	chip->vops = asic->vops;
	chip->fw_mcu_version = 0;

	if (chip->warp_fw_update_newmethod) {
		if (oplus_is_rf_ftm_mode()) {
			oplus_warp_fw_update_work_init(chip);
		}
	} else {
		oplus_warp_fw_update_work_init(chip);
	}

	oplus_warp_init(chip);
#ifdef CONFIG_OPLUS_CHG_OOS
	asic->register_warp_devinfo(chip);
#else
	asic->register_warp_devinfo();
#endif

	asic->init_proc_warp_fw_check();
}

static void oplus_warp_param_init(struct oplus_warp_chip *chip)
{
	chip->detach_unexpectly = false;
	chip->allow_reading = true;
	chip->fastchg_started = false;
	chip->fastchg_dummy_started = false;
	chip->fastchg_ing = false;
	chip->fastchg_to_normal = false;
#ifdef OPLUS_CHG_OP_DEF
	chip->ffc_chg_start = false;
	chip->fastchg_ignore_event = false;
#endif
	chip->adapter_sid = 0;
	chip->fastchg_to_warm = false;
	chip->fastchg_allow = false;
	chip->fastchg_low_temp_full = false;
	chip->have_updated = false;
	chip->need_to_up = false;
	chip->btb_temp_over = false;
	chip->adapter_update_real = ADAPTER_FW_UPDATE_NONE;
	chip->adapter_update_report = chip->adapter_update_real;
	chip->mcu_update_ing = true;
	chip->mcu_boot_by_gpio = false;
	chip->dpdm_switch_mode = NORMAL_CHARGER_MODE;
	chip->fast_chg_type = FASTCHG_CHARGER_TYPE_UNKOWN;
	/*chip->batt_psy = power_supply_get_by_name("battery");*/
	chip->disable_adapter_output = false;
	chip->set_warp_current_limit = WARP_MAX_CURRENT_NO_LIMIT;
	chip->reset_adapter = false;
	chip->temp_range_init = false;
	chip->w_soc_temp_to_mcu = false;
	oplus_warp_init_watchdog_timer(chip);

	oplus_warp_awake_init(chip);
	INIT_DELAYED_WORK(&chip->fastchg_work, oplus_warp_fastchg_func);
	INIT_DELAYED_WORK(&chip->check_charger_out_work, check_charger_out_work_func);
	INIT_WORK(&chip->warp_watchdog_work, warp_watchdog_work_func);
	g_warp_chip = chip;
}

static int oplus_chg_warp_probe(struct platform_device *pdev)
{
	struct oplus_warp_chip *chip;

	chip = devm_kzalloc(&pdev->dev, sizeof(struct oplus_warp_chip), GFP_KERNEL);
	if (chip == NULL) {
		pr_err("alloc memory error\n");
		oplus_warp_probe_done = -ENOMEM;
		return -ENOMEM;
	}

	chip->dev = &pdev->dev;
	platform_set_drvdata(pdev, chip);
	chip->asic_list.prev = &chip->asic_list;
	chip->asic_list.next = &chip->asic_list;
	chip->mcu_hwid_type = OPLUS_WARP_MCU_HWID_UNKNOW;

	oplus_warp_gpio_dt_init(chip);
	opchg_set_clock_sleep(chip);
	oplus_warp_delay_reset_mcu_init(chip);

	chip->pcb_version = g_hw_version;
	chip->warp_fw_check = false;
	mutex_init(&chip->pinctrl_mutex);
	oplus_warp_fw_type_dt(chip);

	mutex_init(&chip->asic_list_lock);
	INIT_DELAYED_WORK(&chip->asic_init_work, oplus_chg_asic_init_work);
	oplus_warp_param_init(chip);
	oplus_warp_probe_done = 0;

	return 0;
}

static int oplus_chg_warp_remove(struct platform_device *pdev)
{
	struct oplus_warp_chip *chip = platform_get_drvdata(pdev);

	devm_kfree(&pdev->dev, chip);
	g_warp_chip = NULL;
	platform_set_drvdata(pdev, NULL);
	return 0;
}

static const struct of_device_id oplus_chg_warp_match[] = {
	{ .compatible = "oplus_chg_warp" },
	{},
};

static struct platform_driver oplus_chg_warp = {
	.driver = {
		.name = "oplus_chg-warp",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(oplus_chg_warp_match),
	},
	.probe = oplus_chg_warp_probe,
	.remove = oplus_chg_warp_remove,
};

static __init int oplus_chg_warp_init(void)
{
	return platform_driver_register(&oplus_chg_warp);
}

static __exit void oplus_chg_warp_exit(void)
{
	platform_driver_unregister(&oplus_chg_warp);
}

oplus_chg_module_register(oplus_chg_warp);
#endif

