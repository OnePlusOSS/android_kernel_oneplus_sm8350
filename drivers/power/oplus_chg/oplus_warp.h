/* SPDX-License-Identifier: GPL-2.0-only  */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef _OPLUS_WARP_H_
#define _OPLUS_WARP_H_

#include <linux/workqueue.h>
#include <linux/version.h>
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0))
#include <linux/wakelock.h>
#endif
#include <linux/timer.h>
#include <linux/slab.h>
#ifdef CONFIG_OPLUS_CHG_OOS
#include <linux/oem/project_info.h>
#include <linux/oem/oplus_chg.h>
#else
#include <soc/oplus/device_info.h>
#include <soc/oplus/system/oplus_project.h>
#include <linux/oplus_chg.h>
#endif
#include <linux/firmware.h>
#ifdef OPLUS_CHG_OP_DEF
#include "oplus_op_def.h"
#endif

#define OPLUS_WARP_MCU_HWID_UNKNOW   -1
#define OPLUS_WARP_MCU_HWID_STM8S	0
#define OPLUS_WARP_MCU_HWID_N76E		1
#define OPLUS_WARP_ASIC_HWID_RK826	2
#define OPLUS_WARP_ASIC_HWID_OP10	3
#define OPLUS_WARP_ASIC_HWID_RT5125   4
#define OPLUS_WARP_ASIC_HWID_NON_EXIST 5

enum {
	WARP_CHARGER_MODE,
	HEADPHONE_MODE,
	NORMAL_CHARGER_MODE,
};

enum {
	FW_ERROR_DATA_MODE,
	FW_NO_CHECK_MODE,
	FW_CHECK_MODE,
};

enum {
	WARP_MAX_CURRENT_NO_LIMIT,
	WARP_MAX_CURRENT_LIMIT_2A,
	WARP_MAX_CURRENT_LIMIT_OTHER,
};
enum {
	FASTCHG_CHARGER_TYPE_UNKOWN,
	PORTABLE_PIKAQIU_1 = 0x31,
	PORTABLE_PIKAQIU_2 = 0x32,
	PORTABLE_50W = 0x33,
	PORTABLE_20W_1 = 0X34,
	PORTABLE_20W_2 = 0x35,
	PORTABLE_20W_3 = 0x36,
};

enum e_fastchg_power{
	FASTCHG_POWER_UNKOWN,
	FASTCHG_POWER_5V4A_5V6A_WARP,
	FASTCHG_POWER_11V3A_FLASHCHARGER,
	FASTCHG_POWER_10V5A_SINGLE_BAT_SWARP,
	FASTCHG_POWER_10V5A_TWO_BAT_SWARP,
	FASTCHG_POWER_10V6P5A_TWO_BAT_SWARP,
	FASTCHG_POWER_OTHER,
};

enum {
	BAT_TEMP_NATURAL = 0,
	BAT_TEMP_HIGH0,
	BAT_TEMP_HIGH1,
	BAT_TEMP_HIGH2,
	BAT_TEMP_HIGH3,
	BAT_TEMP_HIGH4,
	BAT_TEMP_HIGH5,
	BAT_TEMP_LOW0,
	BAT_TEMP_LOW1,
	BAT_TEMP_LOW2,
	BAT_TEMP_LITTLE_COOL,
	BAT_TEMP_COOL,
	BAT_TEMP_NORMAL_LOW,
	BAT_TEMP_NORMAL_HIGH,
	BAT_TEMP_LITTLE_COLD,
	BAT_TEMP_EXIT,
};

enum {
	FASTCHG_TEMP_RANGE_INIT = 0,
	FASTCHG_TEMP_RANGE_LITTLE_COLD,/*0 ~ 5*/
	FASTCHG_TEMP_RANGE_COOL,/*5 ~ 12*/
	FASTCHG_TEMP_RANGE_LITTLE_COOL, /*12 `16*/
	FASTCHG_TEMP_RANGE_NORMAL_LOW, /*16-25*/
	FASTCHG_TEMP_RANGE_NORMAL_HIGH, /*25-43*/
};


struct warp_gpio_control {
	int switch1_gpio;
	int switch1_ctr1_gpio;
	int switch2_gpio;
	int switch3_gpio;
	int reset_gpio;
	int clock_gpio;
	int data_gpio;
	int warp_mcu_id_gpio;
	int warp_asic_id_gpio;
	int data_irq;
	struct pinctrl *pinctrl;

	struct pinctrl_state *gpio_switch1_act_switch2_act;
	struct pinctrl_state *gpio_switch1_sleep_switch2_sleep;
	struct pinctrl_state *gpio_switch1_act_switch2_sleep;
	struct pinctrl_state *gpio_switch1_sleep_switch2_act;
	struct pinctrl_state *gpio_switch1_ctr1_act;
	struct pinctrl_state *gpio_switch1_ctr1_sleep;

	struct pinctrl_state *gpio_clock_active;
	struct pinctrl_state *gpio_clock_sleep;
	struct pinctrl_state *gpio_data_active;
	struct pinctrl_state *gpio_data_sleep;
	struct pinctrl_state *gpio_reset_active;
	struct pinctrl_state *gpio_reset_sleep;
	struct pinctrl_state *gpio_warp_mcu_id_default;
	struct pinctrl_state *gpio_warp_asic_id_active;
	struct pinctrl_state *gpio_warp_asic_id_sleep;
};

#ifdef OPLUS_CHG_OP_DEF

struct oplus_warp_chip;

extern int oplus_warp_probe_done;
struct oplus_chg_asic {
	struct i2c_client *client;
	struct device *dev;
	void *data;
	struct oplus_warp_operations *vops;
	struct list_head list;
	bool (* is_used)(struct i2c_client *);
	int (* init_proc_warp_fw_check)(void);
#ifdef CONFIG_OPLUS_CHG_OOS
	void (*register_warp_devinfo)(struct oplus_warp_chip *chip);
#else
	void (*register_warp_devinfo)(void);
#endif

	bool batt_type_4400mv;
	int warp_fw_type;
	int warp_reply_mcu_bits;
	const unsigned char *firmware_data;
	unsigned int fw_data_count;
	int fw_data_version;

	int warp_cool_bat_volt;
	int warp_little_cool_bat_volt;
	int warp_normal_bat_volt;
	int warp_warm_bat_volt;
	int warp_cool_bat_suspend_volt;
	int warp_little_cool_bat_suspend_volt;
	int warp_normal_bat_suspend_volt;
	int warp_warm_bat_suspend_volt;
};

#endif

struct oplus_warp_chip {
	struct i2c_client *client;
	struct device *dev;
	struct oplus_warp_operations *vops;
	struct warp_gpio_control warp_gpio;
	struct delayed_work fw_update_work;
	struct delayed_work fw_update_work_fix;
	struct delayed_work fastchg_work;
	struct delayed_work delay_reset_mcu_work;
	struct delayed_work check_charger_out_work;
	struct work_struct warp_watchdog_work;
	struct timer_list watchdog;
#ifdef OPLUS_CHG_OP_DEF
	struct delayed_work asic_init_work;
	struct list_head asic_list;
	struct mutex asic_list_lock;
	int mcu_hwid_type;
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0))
	struct wake_lock warp_wake_lock;
#else
	struct wakeup_source *warp_ws;
#endif

	struct power_supply *batt_psy;
#ifdef OPLUS_CHG_OP_DEF
	struct oplus_chg_mod *comm_ocm;
#endif
	int pcb_version;
	bool allow_reading;
	bool fastchg_started;
	bool fastchg_ing;
	bool fastchg_allow;
	bool fastchg_to_normal;
	bool fastchg_to_warm;
#ifdef OPLUS_CHG_OP_DEF
	bool ffc_chg_start;
	bool fastchg_ignore_event;
#endif
	bool fastchg_low_temp_full;
	bool btb_temp_over;
	bool fastchg_dummy_started;
	bool need_to_up;
	bool have_updated;
	bool mcu_update_ing;
	bool mcu_boot_by_gpio;
	const unsigned char *firmware_data;
	unsigned int fw_data_count;
	int fw_mcu_version;
	int fw_data_version;
	int adapter_update_real;
	int adapter_update_report;
	int dpdm_switch_mode;
	bool support_warp_by_normal_charger_path;
/* Add for warp batt 4.40*/
	bool batt_type_4400mv;
	bool warp_fw_check;
	int warp_fw_type;
	int fw_update_flag;
	struct manufacture_info manufacture_info;
	bool warp_fw_update_newmethod;
	char *fw_path;
	struct mutex pinctrl_mutex;
	int warp_temp_cur_range;
	int warp_little_cool_temp;
	int warp_cool_temp;
	int warp_little_cold_temp;
	int warp_normal_low_temp;
	int warp_little_cool_temp_default;
	int warp_cool_temp_default;
	int warp_little_cold_temp_default;
	int warp_normal_low_temp_default;
	int warp_low_temp;
	int warp_high_temp;
	int warp_low_soc;
	int warp_high_soc;
	int warp_cool_bat_volt;
	int warp_little_cool_bat_volt;
	int warp_normal_bat_volt;
	int warp_warm_bat_volt;
	int warp_cool_bat_suspend_volt;
	int warp_little_cool_bat_suspend_volt;
	int warp_normal_bat_suspend_volt;
	int warp_warm_bat_suspend_volt;
	int warp_chg_current_now;
	int fast_chg_type;
	bool disable_adapter_output;// 0--warp adapter output normal,  1--disable warp adapter output
	int set_warp_current_limit;///0--no limit;  1--max current limit 2A
	bool warp_multistep_adjust_current_support;
	int warp_reply_mcu_bits;
	int warp_multistep_initial_batt_temp;
	int warp_strategy_normal_current;
	int warp_strategy1_batt_high_temp0;
	int warp_strategy1_batt_high_temp1;
	int warp_strategy1_batt_high_temp2;
	int warp_strategy1_batt_low_temp2;
	int warp_strategy1_batt_low_temp1;
	int warp_strategy1_batt_low_temp0;
	int warp_strategy1_high_current0;
	int warp_strategy1_high_current1;
	int warp_strategy1_high_current2;
	int warp_strategy1_low_current2;
	int warp_strategy1_low_current1;
	int warp_strategy1_low_current0;
	int warp_strategy2_batt_up_temp1;
	int warp_strategy2_batt_up_down_temp2;
	int warp_strategy2_batt_up_temp3;
	int warp_strategy2_batt_up_down_temp4;
	int warp_strategy2_batt_up_temp5;
	int warp_strategy2_batt_up_temp6;
	int warp_strategy2_high0_current;
	int warp_strategy2_high1_current;
	int warp_strategy2_high2_current;
	int warp_strategy2_high3_current;
	int fastchg_batt_temp_status;
	int warp_batt_over_high_temp;
	int warp_batt_over_low_temp;
	int warp_over_high_or_low_current;
	int warp_strategy_change_count;
	int *warp_current_lvl;
	int warp_current_lvl_cnt;
	int detach_unexpectly;
	bool disable_real_fast_chg;
	bool reset_adapter;
	bool suspend_charger;
	bool temp_range_init;
	bool w_soc_temp_to_mcu;
	int soc_range;
#ifdef OPLUS_CHG_OP_DEF
	unsigned int adapter_sid;
#endif
};

#define MAX_FW_NAME_LENGTH	60
#define MAX_DEVICE_VERSION_LENGTH 16
#define MAX_DEVICE_MANU_LENGTH    60
struct oplus_warp_operations {
	int (*fw_update)(struct oplus_warp_chip *chip);
	int (*fw_check_then_recover)(struct oplus_warp_chip *chip);
	int (*fw_check_then_recover_fix)(struct oplus_warp_chip *chip);
	void (*eint_regist)(struct oplus_warp_chip *chip);
	void (*eint_unregist)(struct oplus_warp_chip *chip);
	void (*set_data_active)(struct oplus_warp_chip *chip);
	void (*set_data_sleep)(struct oplus_warp_chip *chip);
	void (*set_clock_active)(struct oplus_warp_chip *chip);
	void (*set_clock_sleep)(struct oplus_warp_chip *chip);
	void (*set_switch_mode)(struct oplus_warp_chip *chip, int mode);
	int (*get_gpio_ap_data)(struct oplus_warp_chip *chip);
	int (*read_ap_data)(struct oplus_warp_chip *chip);
	void (*reply_mcu_data)(struct oplus_warp_chip *chip, int ret_info, int device_type);
	void (*reply_mcu_data_4bits)(struct oplus_warp_chip *chip,
		int ret_info, int device_type);
	void (*reset_fastchg_after_usbout)(struct oplus_warp_chip *chip);
#ifndef OPLUS_CHG_OP_DEF
	void (*switch_fast_chg)(struct oplus_warp_chip *chip);
#else
	bool (*switch_fast_chg)(struct oplus_warp_chip *chip);
#endif
	void (*reset_mcu)(struct oplus_warp_chip *chip);
	void (*set_mcu_sleep)(struct oplus_warp_chip *chip);
	void (*set_warp_chargerid_switch_val)(struct oplus_warp_chip *chip, int value);
	bool (*is_power_off_charging)(struct oplus_warp_chip *chip);
	int (*get_reset_gpio_val)(struct oplus_warp_chip *chip);
	int (*get_switch_gpio_val)(struct oplus_warp_chip *chip);
	int (*get_ap_clk_gpio_val)(struct oplus_warp_chip *chip);
	int (*get_fw_version)(struct oplus_warp_chip *chip);
	int (*get_clk_gpio_num)(struct oplus_warp_chip *chip);
	int (*get_data_gpio_num)(struct oplus_warp_chip *chip);
	void (*update_temperature_soc)(void);
	int (*check_asic_fw_status)(struct oplus_warp_chip *chip);
#ifdef OPLUS_CHG_DEBUG
	int (*user_fw_upgrade)(struct oplus_warp_chip *chip, u8 *fw_buf, u32 fw_size);
#endif
};

void oplus_warp_init(struct oplus_warp_chip *chip);
void oplus_warp_shedule_fastchg_work(void);
void oplus_warp_read_fw_version_init(struct oplus_warp_chip *chip);
void oplus_warp_fw_update_work_init(struct oplus_warp_chip *chip);
bool oplus_warp_wake_fastchg_work(struct oplus_warp_chip *chip);
void oplus_warp_print_log(void);
void oplus_warp_switch_mode(int mode);
bool oplus_warp_get_allow_reading(void);
bool oplus_warp_get_fastchg_started(void);
bool oplus_warp_get_fastchg_ing(void);
bool oplus_warp_get_fastchg_allow(void);
void oplus_warp_set_fastchg_allow(int enable);
bool oplus_warp_get_fastchg_to_normal(void);
void oplus_warp_set_fastchg_to_normal_false(void);
#ifdef OPLUS_CHG_OP_DEF
bool oplus_warp_get_ffc_chg_start(void);
void oplus_warp_set_ffc_chg_start_false(void);
unsigned int oplus_warp_get_adapter_sid(void);
bool oplus_warp_ignore_event(void);
#endif
bool oplus_warp_get_fastchg_to_warm(void);
void oplus_warp_set_fastchg_to_warm_false(void);
void oplus_warp_set_fastchg_type_unknow(void);
bool oplus_warp_get_fastchg_low_temp_full(void);
void oplus_warp_set_fastchg_low_temp_full_false(void);
bool oplus_warp_get_warp_multistep_adjust_current_support(void);
bool oplus_warp_get_fastchg_dummy_started(void);
void oplus_warp_set_fastchg_dummy_started_false(void);
int oplus_warp_get_adapter_update_status(void);
int oplus_warp_get_adapter_update_real_status(void);
bool oplus_warp_get_btb_temp_over(void);
void oplus_warp_reset_fastchg_after_usbout(void);
#ifndef OPLUS_CHG_OP_DEF
void oplus_warp_switch_fast_chg(void);
#else
bool oplus_warp_switch_fast_chg(void);
#endif
void oplus_warp_reset_mcu(void);
void oplus_warp_set_mcu_sleep(void);
void oplus_warp_set_warp_chargerid_switch_val(int value);
void oplus_warp_set_ap_clk_high(void);
int oplus_warp_get_warp_switch_val(void);
bool oplus_warp_check_chip_is_null(void);
void oplus_warp_battery_update(void);

int oplus_warp_get_uart_tx(void);
int oplus_warp_get_uart_rx(void);
void oplus_warp_uart_init(void);
void oplus_warp_uart_reset(void);
void oplus_warp_set_adapter_update_real_status(int real);
void oplus_warp_set_adapter_update_report_status(int report);
int oplus_warp_get_fast_chg_type(void);
int oplus_warp_get_reply_bits(void);
void oplus_warp_set_disable_adapter_output(bool disable);
void oplus_warp_set_warp_max_current_limit(int current_level);
bool oplus_warp_get_detach_unexpectly(void);
void oplus_warp_set_detach_unexpectly(bool val);
void oplus_warp_set_disable_real_fast_chg(bool val);
void oplus_warp_turn_off_fastchg(void);
int oplus_warp_get_reply_bits(void);
extern int get_warp_mcu_type(struct oplus_warp_chip *chip);
bool opchg_get_mcu_update_state(void);
void oplus_warp_get_warp_chip_handle(struct oplus_warp_chip **chip);
void oplus_warp_reset_temp_range(struct oplus_warp_chip *chip);
bool oplus_warp_get_reset_adapter_st(void);
int oplus_warp_get_reset_active_status(void);
void oplus_warp_fw_update_work_plug_in(void);
int oplus_warp_check_asic_fw_status(void);

#ifdef OPLUS_CHG_DEBUG
int oplus_warp_user_fw_upgrade(u8 *fw_buf, u32 fw_size);
#endif

#ifdef OPLUS_CHG_OP_DEF
bool is_warp_asic_hwid_check_by_i2c(struct oplus_warp_chip *chip);
int oplus_chg_asic_register(struct oplus_chg_asic *asic);
int oplus_chg_asic_unregister(struct oplus_chg_asic *asic);
int oplus_warp_convert_fast_chg_type(int fast_chg_type);
#endif

#endif /* _OPLUS_WARP_H */
