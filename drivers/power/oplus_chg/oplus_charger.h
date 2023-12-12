/* SPDX-License-Identifier: GPL-2.0-only  */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef _OPLUS_CHARGER_H_
#define _OPLUS_CHARGER_H_

#include <linux/power_supply.h>
#include <linux/workqueue.h>
#include <linux/version.h>
#include <linux/thermal.h>

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0))
#include <linux/wakelock.h>
#endif
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
/* #elif defined CONFIG_FB nick.hu todo*/
#else
#include <linux/notifier.h>
#include <linux/fb.h>
#ifdef CONFIG_DRM_MSM
#include <linux/msm_drm_notify.h>
#endif
#endif

#ifdef CONFIG_OPLUS_CHARGER_MTK
#include <linux/i2c.h>
//#include <mt-plat/battery_meter.h>
#include <mt-plat/mtk_boot.h>
#ifdef CONFIG_OPLUS_CHARGER_MTK6779
#include "charger_ic/oplus_battery_mtk6779.h"
#endif
#ifdef CONFIG_OPLUS_CHARGER_MTK6779Q
#include "charger_ic/oplus_battery_mtk6779Q.h"
#endif
#ifdef CONFIG_OPLUS_CHARGER_MTK6885
#include "charger_ic/oplus_battery_mtk6885R.h"
#endif
#ifdef CONFIG_OPLUS_CHARGER_MTK6889
#include "charger_ic/oplus_battery_mtk6889R.h"
#endif
#ifdef CONFIG_OPLUS_CHARGER_MTK6873
#include "charger_ic/oplus_battery_mtk6873R.h"
#endif
#ifdef CONFIG_OPLUS_CHARGER_MTK6853
#include "charger_ic/oplus_battery_mtk6853R.h"
#endif
#ifdef CONFIG_OPLUS_CHARGER_MTK6769
#include "charger_ic/oplus_battery_mtk6769.h"
#endif
#else /* CONFIG_OPLUS_CHARGER_MTK */
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 14, 0))
#include <linux/qpnp/qpnp-adc.h>
#include <linux/msm_bcl.h>
#endif
#ifdef CONFIG_OPLUS_CHG_OOS
#include <linux/oem/boot_mode.h>
#include <linux/oem/oplus_chg.h>
#else
#include <soc/oplus/system/boot_mode.h>
#include <linux/oplus_chg.h>
#endif
#ifdef CONFIG_OPLUS_MSM8953N_CHARGER
#include "charger_ic/oplus_battery_msm8953_N.h"
#elif defined CONFIG_OPLUS_MSM8953_CHARGER
#include "charger_ic/oplus_battery_msm8953.h"
#elif defined CONFIG_OPLUS_MSM8998_CHARGER
#include "charger_ic/oplus_battery_msm8998.h"
#elif defined CONFIG_OPLUS_MSM8998O_CHARGER
#include "charger_ic/oplus_battery_msm8998_O.h"
#elif defined CONFIG_OPLUS_SDM845_CHARGER
#include "charger_ic/oplus_battery_sdm845.h"
#elif defined CONFIG_OPLUS_SDM670_CHARGER
#include "charger_ic/oplus_battery_sdm670.h"
#elif defined CONFIG_OPLUS_SDM670P_CHARGER
#include "charger_ic/oplus_battery_sdm670P.h"
#elif defined CONFIG_OPLUS_SM8150_CHARGER
#include "charger_ic/oplus_battery_msm8150.h"
#elif defined CONFIG_OPLUS_SM8250_CHARGER
#include "charger_ic/oplus_battery_msm8250.h"
#elif defined CONFIG_OPLUS_SM8150R_CHARGER
#include "charger_ic/oplus_battery_msm8150Q.h"
#elif defined CONFIG_OPLUS_SM8150_PRO_CHARGER
#include "charger_ic/oplus_battery_msm8150_pro.h"
#elif defined CONFIG_OPLUS_SM6125_CHARGER
#include "charger_ic/oplus_battery_sm6125P.h"
#elif defined CONFIG_OPLUS_SM7150_CHARGER
#include "charger_ic/oplus_battery_sm7150_P.h"
#elif defined CONFIG_OPLUS_SDM670Q_CHARGER
#include "charger_ic/oplus_battery_sdm670Q.h"
#elif defined CONFIG_OPLUS_SM7250_CHARGER
#include "charger_ic/oplus_battery_msm7250_Q.h"
#elif defined CONFIG_OPLUS_SM7250R_CHARGER
#include "charger_ic/oplus_battery_msm7250_R.h"
#elif defined CONFIG_OPLUS_SM8350_CHARGER
#include "charger_ic/oplus_sm8350.h"
#else /* CONFIG_OPLUS_MSM8953_CHARGER */
#include "charger_ic/oplus_battery_msm8976.h"
#endif /* CONFIG_OPLUS_MSM8953_CHARGER */
#endif /* CONFIG_OPLUS_CHARGER_MTK */

#ifdef OPLUS_CHG_OP_DEF
#include "oplus_chg_strategy.h"
#endif
#ifdef OPLUS_CHG_OP_DEF
#define CHG_VUSBIN_VOL_THR  3700
#endif
#define CHG_LOG_CRTI 1
#define CHG_LOG_FULL 2

#define OPCHG_PWROFF_HIGH_BATT_TEMP		770
#define OPCHG_PWROFF_EMERGENCY_BATT_TEMP	850

#define OPCHG_INPUT_CURRENT_LIMIT_CHARGER_MA	2000
#define OPCHG_INPUT_CURRENT_LIMIT_USB_MA	500
#define OPCHG_INPUT_CURRENT_LIMIT_CDP_MA	1500
#define OPCHG_INPUT_CURRENT_LIMIT_LED_MA	1200
#define OPCHG_INPUT_CURRENT_LIMIT_CAMERA_MA	1000
#define OPCHG_INPUT_CURRENT_LIMIT_CALLING_MA	1200
#define OPCHG_FAST_CHG_MAX_MA			2000
#ifdef OPLUS_CHG_OP_DEF
#define OPCHG_BATT_CURRENT_LIMIT_CAMERA_MA	500
#define OPCHG_FAST_CURRENT_LIMIT_CAMERA_LEVEL	0x02
#define OPCHG_BATT_CURRENT_LIMIT_CALLING_MA	500
#define OPCHG_FAST_CURRENT_LIMIT_CALLING_LEVEL	0x02
#endif

#define FEATURE_PRINT_CHGR_LOG
#define FEATURE_PRINT_BAT_LOG
#define FEATURE_PRINT_GAUGE_LOG
#define FEATURE_PRINT_STATUS_LOG
/*#define FEATURE_PRINT_OTHER_LOG*/
#define FEATURE_PRINT_VOTE_LOG
#define FEATURE_PRINT_ICHGING_LOG
#define FEATURE_VBAT_PROTECT

#define NOTIFY_CHARGER_OVER_VOL			1
#define NOTIFY_CHARGER_LOW_VOL			2
#define NOTIFY_BAT_OVER_TEMP			3
#define NOTIFY_BAT_LOW_TEMP			4
#define NOTIFY_BAT_NOT_CONNECT			5
#define NOTIFY_BAT_OVER_VOL			6
#define NOTIFY_BAT_FULL				7
#define NOTIFY_CHGING_CURRENT			8
#define NOTIFY_CHGING_OVERTIME			9
#define NOTIFY_BAT_FULL_PRE_HIGH_TEMP		10
#define NOTIFY_BAT_FULL_PRE_LOW_TEMP		11
#define NOTIFY_BAT_FULL_THIRD_BATTERY		14
#define NOTIFY_SHORT_C_BAT_CV_ERR_CODE1		15
#define NOTIFY_SHORT_C_BAT_FULL_ERR_CODE2	16
#define NOTIFY_SHORT_C_BAT_FULL_ERR_CODE3	17
#define NOTIFY_SHORT_C_BAT_DYNAMIC_ERR_CODE4	18
#define NOTIFY_SHORT_C_BAT_DYNAMIC_ERR_CODE5	19
#define	NOTIFY_CHARGER_TERMINAL			20
#define NOTIFY_GAUGE_I2C_ERR			21
#define NOTIFY_BAT_VOLTAGE_DIFF			22
#define NOTIFY_OVP_VOLTAGE_ABNORMAL		23


#define OPLUS_CHG_500_CHARGING_CURRENT	500
#define OPLUS_CHG_900_CHARGING_CURRENT	900
#define OPLUS_CHG_1200_CHARGING_CURRENT	1200
#define OPLUS_CHG_1500_CHARGING_CURRENT	1500
#define OPLUS_CHG_1800_CHARGING_CURRENT	1800
#define OPLUS_CHG_2000_CHARGING_CURRENT	2000
#define OPLUS_CHG_3600_CHARGING_CURRENT	3600

#define SMART_WARP_CHARGER_CURRENT_BIT0 	0X01
#define SMART_WARP_CHARGER_CURRENT_BIT1 	0X02
#define SMART_WARP_CHARGER_CURRENT_BIT2 	0X04
#define SMART_WARP_CHARGER_CURRENT_BIT3 	0X08
#define SMART_WARP_CHARGER_CURRENT_BIT4 	0X10
#define SMART_WARP_CHARGER_CURRENT_BIT5 	0X20
#define SMART_WARP_CHARGER_CURRENT_BIT6 	0X40
#define SMART_WARP_CHARGER_CURRENT_BIT7 	0X80

#define SMART_COMPATIBLE_WARP_CHARGER_CURRENT_BIT0 	0X100
#define SMART_COMPATIBLE_WARP_CHARGER_CURRENT_BIT1 	0X200
#define SMART_COMPATIBLE_WARP_CHARGER_CURRENT_BIT2 	0X400
#define SMART_CHARGE_USER_USBTEMP	1
#define SMART_CHARGE_USER_OTHER		0
#define USBTEMP_CHARGING_CURRENT_LIMIT	3000

#define SMART_NORMAL_CHARGER_500MA 	0X1000
#define SMART_NORMAL_CHARGER_900MA	0X2000
#define SMART_NORMAL_CHARGER_1200MA	0X4000
#define SMART_NORMAL_CHARGER_1500MA	0X8000
#define SMART_NORMAL_CHARGER_2000MA     0X400
#define SMART_NORMAL_CHARGER_9V1500mA	0X800

#define TEMPERATURE_INVALID	-2740

#define chg_debug(fmt, ...) \
        printk(KERN_NOTICE "[OPLUS_CHG][%s]"fmt, __func__, ##__VA_ARGS__)

#define chg_err(fmt, ...) \
        printk(KERN_ERR "[OPLUS_CHG][%s]"fmt, __func__, ##__VA_ARGS__)

typedef enum {
	CHG_NONE = 0,
	CHG_DISABLE,
	CHG_SUSPEND,
}OPLUS_CHG_DISABLE_STATUS;

typedef enum {
	CHG_STOP_VOTER_NONE					=	0,
	CHG_STOP_VOTER__BATTTEMP_ABNORMAL	=	(1 << 0),
	CHG_STOP_VOTER__VCHG_ABNORMAL		=	(1 << 1),
	CHG_STOP_VOTER__VBAT_TOO_HIGH		=	(1 << 2),
	CHG_STOP_VOTER__MAX_CHGING_TIME		=	(1 << 3),
	CHG_STOP_VOTER__FULL				=	(1 << 4),
#ifdef OPLUS_CHG_OP_DEF
	CHG_STOP_VOTER_BAD_VOL_DIFF			=	(1 << 5),
#endif
}OPLUS_CHG_STOP_VOTER;

typedef enum {
	CHARGER_STATUS__GOOD,
	CHARGER_STATUS__VOL_HIGH,
	CHARGER_STATUS__VOL_LOW,
	CHARGER_STATUS__INVALID
}OPLUS_CHG_VCHG_STATUS;

typedef enum {
	BATTERY_STATUS__NORMAL = 0,			/*16C~44C*/
	BATTERY_STATUS__REMOVED,			/*<-20C*/
	BATTERY_STATUS__LOW_TEMP,			/*<-2C*/
	BATTERY_STATUS__HIGH_TEMP,			/*>53C*/
	BATTERY_STATUS__COLD_TEMP,			/*-2C~0C*/
	BATTERY_STATUS__LITTLE_COLD_TEMP,	/*0C~5C*/
	BATTERY_STATUS__COOL_TEMP,			/*5C~12C*/
	BATTERY_STATUS__LITTLE_COOL_TEMP,		/*12C~16C*/
	BATTERY_STATUS__WARM_TEMP,		       /*44C~53C*/
	BATTERY_STATUS__INVALID
}
OPLUS_CHG_TBATT_STATUS;

typedef enum {
        BATTERY_STATUS__NORMAL_PHASE1,	/*16~22C*/
        BATTERY_STATUS__NORMAL_PHASE2,	/*22~34C*/
        BATTERY_STATUS__NORMAL_PHASE3,	/*34~37C*/
        BATTERY_STATUS__NORMAL_PHASE4,	/*37~40C*/
        BATTERY_STATUS__NORMAL_PHASE5,	/*40~42C*/
        BATTERY_STATUS__NORMAL_PHASE6,	/*42~45C*/
}
OPLUS_CHG_TBATT_NORMAL_STATUS;

typedef enum {
	LED_TEMP_STATUS__NORMAL = 0,		/*<=35c*/
	LED_TEMP_STATUS__WARM,				/*>35 && <=37C*/
	LED_TEMP_STATUS__HIGH,				/*>37C*/
}
OPLUS_CHG_TLED_STATUS;

typedef enum {
	FFC_TEMP_STATUS__NORMAL = 0,			/*<=35c*/
	FFC_TEMP_STATUS__WARM,					/*>35 && <=40C*/
	FFC_TEMP_STATUS__HIGH,					/*>40C*/
	FFC_TEMP_STATUS__LOW,					/*<16C*/
}
OPLUS_CHG_FFC_TEMP_STATUS;

typedef enum {
	CRITICAL_LOG_NORMAL = 0,
	CRITICAL_LOG_UNABLE_CHARGING,
	CRITICAL_LOG_BATTTEMP_ABNORMAL,
	CRITICAL_LOG_VCHG_ABNORMAL,
	CRITICAL_LOG_VBAT_TOO_HIGH,
	CRITICAL_LOG_CHARGING_OVER_TIME,
	CRITICAL_LOG_WARP_WATCHDOG,
	CRITICAL_LOG_WARP_BAD_CONNECTED,
	CRITICAL_LOG_WARP_BTB,
	CRITICAL_LOG_WARP_FW_UPDATE_ERR,
}OPLUS_CHG_CRITICAL_LOG;

typedef enum {
	CHARGING_STATUS_CCCV = 0X01,
	CHARGING_STATUS_FULL = 0X02,
	CHARGING_STATUS_FAIL = 0X03,
}OPLUS_CHG_CHARGING_STATUS;

typedef enum {
	CHARGER_SUBTYPE_DEFAULT = 0,
	CHARGER_SUBTYPE_FASTCHG_WARP,
	CHARGER_SUBTYPE_FASTCHG_SWARP,
	CHARGER_SUBTYPE_PD,
	CHARGER_SUBTYPE_QC,
}OPLUS_CHARGER_SUBTYPE;

typedef enum {
	WARP_TEMP_STATUS__NORMAL = 0,	/*<=34c*/
	WARP_TEMP_STATUS__WARM,			/*>34 && <=38C*/
	WARP_TEMP_STATUS__HIGH,			/*>38 && <=45C*/
}OPLUS_CHG_TBAT_WARP_STATUS;

struct tbatt_normal_anti_shake {
	int phase1_bound;
	int phase2_bound;
	int phase3_bound;
	int phase4_bound;
	int phase5_bound;
	int phase6_bound;
};

struct tbatt_anti_shake {
	int cold_bound;
	int little_cold_bound;
	int cool_bound;
	int little_cool_bound;
	int normal_bound;
	int warm_bound;
	int hot_bound;
	int overtemp_bound;
};

struct oplus_chg_limits {
	int input_current_cdp_ma;
	int input_current_charger_ma;
	int pd_input_current_charger_ma;
	int default_pd_input_current_charger_ma;
	int qc_input_current_charger_ma;
	int default_qc_input_current_charger_ma;
	int input_current_usb_ma;
	int input_current_camera_ma;
	int input_current_calling_ma;
#ifdef OPLUS_CHG_OP_DEF
	int batt_current_camera_ma;
	int fast_current_camera_level;
	int batt_current_calling_ma;
	int fast_current_calling_level;
#endif
	int input_current_led_ma;
	int input_current_cool_down_ma;
	int input_current_warp_led_ma_high;
	int input_current_warp_led_ma_warm;
	int input_current_warp_led_ma_normal;
	int warp_high_bat_decidegc;						/*>=45C*/
	int input_current_warp_ma_high;
	int default_input_current_warp_ma_high;
	int warp_warm_bat_decidegc;						/*38C*/
	int warp_warm_bat_decidegc_antishake;			/*38C*/
	int input_current_warp_ma_warm;
	int default_input_current_warp_ma_warm;
	int warp_normal_bat_decidegc;					/*34C*/
	int warp_normal_bat_decidegc_antishake;
	int input_current_warp_ma_normal;				/*<34c*/
	int default_input_current_warp_ma_normal;
	int charger_current_warp_ma_normal;
	int iterm_ma;
	bool iterm_disabled;
	int recharge_mv;
	int usb_high_than_bat_decidegc;				/*10C*/
	int removed_bat_decidegc;						/*-19C*/
	int cold_bat_decidegc;							/*-3C*/
	int temp_cold_vfloat_mv;
	int temp_cold_fastchg_current_ma;
	int little_cold_bat_decidegc;					/*0C*/
	int temp_little_cold_vfloat_mv;
	int temp_little_cold_fastchg_current_ma;
	int temp_little_cold_fastchg_current_ma_high;
	int temp_little_cold_fastchg_current_ma_low;
	int pd_temp_little_cold_fastchg_current_ma_high;
	int pd_temp_little_cold_fastchg_current_ma_low;
	int qc_temp_little_cold_fastchg_current_ma_high;
	int qc_temp_little_cold_fastchg_current_ma_low;
	int cool_bat_decidegc;							/*5C*/
	int temp_cool_vfloat_mv;
	int temp_cool_fastchg_current_ma_high;
	int temp_cool_fastchg_current_ma_low;
	int pd_temp_cool_fastchg_current_ma_high;
	int pd_temp_cool_fastchg_current_ma_low;
	int qc_temp_cool_fastchg_current_ma_high;
	int qc_temp_cool_fastchg_current_ma_low;
	int little_cool_bat_decidegc;					/*12C*/
	int temp_little_cool_vfloat_mv;
	int temp_little_cool_fastchg_current_ma;
	int temp_little_cool_fastchg_current_ma_high;
	int temp_little_cool_fastchg_current_ma_low;
	int pd_temp_little_cool_fastchg_current_ma;
	int qc_temp_little_cool_fastchg_current_ma;
	int normal_bat_decidegc;						/*16C*/
	int temp_normal_fastchg_current_ma;
	int pd_temp_normal_fastchg_current_ma;
	int qc_temp_normal_fastchg_current_ma;

	int normal_phase1_bat_decidegc;       /* 16C ~ 22C */
	int temp_normal_phase1_vfloat_mv;
	int temp_normal_phase1_fastchg_current_ma;
	int normal_phase2_bat_decidegc;       /* 22C ~ 34C */
	int temp_normal_phase2_vfloat_mv;
	int temp_normal_phase2_fastchg_current_ma_high;
	int temp_normal_phase2_fastchg_current_ma_low;
	int normal_phase3_bat_decidegc;       /* 34 ~ 37C */
	int temp_normal_phase3_vfloat_mv;
	int temp_normal_phase3_fastchg_current_ma_high;
	int temp_normal_phase3_fastchg_current_ma_low;
	int normal_phase4_bat_decidegc;       /* 37C ~ 40C */
	int temp_normal_phase4_vfloat_mv;
	int temp_normal_phase4_fastchg_current_ma_high;
	int temp_normal_phase4_fastchg_current_ma_low;
	int normal_phase5_bat_decidegc;       /* 40C ~ 42C */
	int temp_normal_phase5_vfloat_mv;
	int temp_normal_phase5_fastchg_current_ma;
	int normal_phase6_bat_decidegc;       /*42C_45C*/
	int temp_normal_phase6_vfloat_mv;
	int temp_normal_phase6_fastchg_current_ma;

	int temp_normal_vfloat_mv;
	int warm_bat_decidegc;							/*45C*/
	int temp_warm_vfloat_mv;
	int temp_warm_fastchg_current_ma;
	int temp_warm_fastchg_current_ma_led_on;
	int pd_temp_warm_fastchg_current_ma;
	int qc_temp_warm_fastchg_current_ma;
	int hot_bat_decidegc;							/*53C*/
	int non_standard_vfloat_mv;
	int non_standard_fastchg_current_ma;
	int max_chg_time_sec;
	int charger_hv_thr;
	int charger_recv_thr;
	int charger_lv_thr;
	int vbatt_full_thr;
	int vbatt_hv_thr;
	int vfloat_step_mv;
	int vfloat_sw_set;
	int vfloat_over_counts;
	int non_standard_vfloat_sw_limit;				/*sw full*/
	int cold_vfloat_sw_limit;
	int little_cold_vfloat_sw_limit;
	int cool_vfloat_sw_limit;
	int little_cool_vfloat_sw_limit;
	int normal_vfloat_sw_limit;
	int warm_vfloat_sw_limit;
	int led_high_bat_decidegc;						/*>=37C*/
	int led_high_bat_decidegc_antishake;
	int input_current_led_ma_high;
	int led_warm_bat_decidegc;						/*35C*/
	int led_warm_bat_decidegc_antishake;
	int input_current_led_ma_warm;
	int input_current_led_ma_normal;				/*<35c*/
	int short_c_bat_vfloat_mv;
	int short_c_bat_fastchg_current_ma;
	int short_c_bat_vfloat_sw_limit;
	bool sw_vfloat_over_protect_enable;				/*vfloat over check*/
	int non_standard_vfloat_over_sw_limit;
	int cold_vfloat_over_sw_limit;
	int little_cold_vfloat_over_sw_limit;
	int cool_vfloat_over_sw_limit;
	int little_cool_vfloat_over_sw_limit;
	int normal_vfloat_over_sw_limit;
	int warm_vfloat_over_sw_limit;
	int normal_vterm_hw_inc;
	int non_normal_vterm_hw_inc;
	int vbatt_pdqc_to_5v_thr;
	int vbatt_pdqc_to_9v_thr;
	int ff1_normal_fastchg_ma;
	int ff1_warm_fastchg_ma;
	int ff1_exit_step_ma;				/*<=35C,700ma*/
	int ff1_warm_exit_step_ma;
	int ffc2_temp_low_decidegc;			/*<16C*/
	int ffc2_temp_high_decidegc;		/*>=40C*/
	int ffc2_warm_fastchg_ma;			/*35~40C,750ma	*/
	int ffc2_temp_warm_decidegc;		/*35C*/
	int ffc2_normal_fastchg_ma;			/*<=35C,700ma*/
	int ffc2_exit_step_ma;				/*<=35C,700ma*/
	int ffc2_warm_exit_step_ma;
	int ffc1_normal_vfloat_sw_limit;			//4.45V
	int ffc1_warm_vfloat_sw_limit;
	int ffc2_normal_vfloat_sw_limit;
	int ffc2_warm_vfloat_sw_limit;
	int ffc_temp_normal_vfloat_mv;				//4.5v
	int ffc2_temp_warm_vfloat_mv;
	int ffc_temp_warm_vfloat_mv;
	int ffc1_temp_normal_vfloat_mv;				//4.5v
	int ffc2_temp_normal_vfloat_mv;				//4.5v
	int ffc_normal_vfloat_over_sw_limit;		//4.5V
	int ffc_warm_vfloat_over_sw_limit;
	int ffc1_normal_vfloat_over_sw_limit;		//4.5V
	int ffc2_normal_vfloat_over_sw_limit;		//4.5V
	int ffc2_warm_vfloat_over_sw_limit;
	int default_ffc1_normal_vfloat_sw_limit;
	int default_ffc1_warm_vfloat_sw_limit;
	int default_ffc2_normal_vfloat_sw_limit;
	int default_ffc2_warm_vfloat_sw_limit;
	int default_iterm_ma;						/*16~45 default value*/
	int default_temp_normal_fastchg_current_ma;
	int default_normal_vfloat_sw_limit;
	int default_temp_normal_vfloat_mv;
	int default_normal_vfloat_over_sw_limit;
	int default_temp_little_cool_fastchg_current_ma;		//12 ~ 16
	int default_little_cool_vfloat_sw_limit;
	int default_temp_little_cool_vfloat_mv;
	int default_little_cool_vfloat_over_sw_limit;
	int default_temp_little_cool_fastchg_current_ma_high;
	int default_temp_little_cool_fastchg_current_ma_low;
	int default_temp_little_cold_fastchg_current_ma_high;	//0 ~ 5
	int default_temp_little_cold_fastchg_current_ma_low;
	int default_temp_cool_fastchg_current_ma_high;			// 5 ~ 12
	int default_temp_cool_fastchg_current_ma_low;
	int default_temp_warm_fastchg_current_ma;				//44 ~ 53
	int default_input_current_charger_ma;
};

struct battery_data {
	int BAT_STATUS;
	int BAT_HEALTH;
	int BAT_PRESENT;
	int BAT_TECHNOLOGY;
	int  BAT_CAPACITY;
	/* Add for Battery Service*/
	int BAT_batt_vol;
	int BAT_batt_temp;
	/* Add for EM */
	int BAT_TemperatureR;
	int BAT_TempBattVoltage;
	int BAT_InstatVolt;
	int BAT_BatteryAverageCurrent;
	int BAT_BatterySenseVoltage;
	int BAT_ISenseVoltage;
	int BAT_ChargerVoltage;
	int battery_request_poweroff;//low battery in sleep
	int fastcharger;
	int charge_technology;
	/* Dual battery */
	int BAT_MMI_CHG;//for MMI_CHG_TEST
	int BAT_FCC;
	int BAT_SOH;
	int BAT_CC;
};

struct normalchg_gpio_pinctrl {
	int chargerid_switch_gpio;
	int usbid_gpio;
	int usbid_irq;
	int ship_gpio;
	int shortc_gpio;
	int dischg_gpio;
	struct pinctrl *pinctrl;
	struct pinctrl_state *chargerid_switch_active;
	struct pinctrl_state *chargerid_switch_sleep;
	struct pinctrl_state *chargerid_switch_default;
	struct pinctrl_state *usbid_active;
	struct pinctrl_state *usbid_sleep;
	struct pinctrl_state *ship_active;
	struct pinctrl_state *ship_sleep;
	struct pinctrl_state *shortc_active;
	struct pinctrl_state *charger_gpio_as_output1;
	struct pinctrl_state *charger_gpio_as_output2;
	struct pinctrl_state *dischg_enable;
	struct pinctrl_state *dischg_disable;
	struct pinctrl_state *usb_temp_adc;
	struct pinctrl_state *usb_temp_adc_suspend;
	struct pinctrl_state *uart_bias_disable;
	struct pinctrl_state *uart_pull_down;
	struct pinctrl_state *chargerid_adc_default;
};


struct short_c_batt_data {
	int short_c_bat_cv_mv;
	int batt_chging_cycle_threshold;
	int batt_chging_cycles;
	int cv_timer1;
	int full_timer2;
	int full_timer3;
	int full_timer4;
	int full_timer5;
	int cool_temp_rbatt;
	int little_cool_temp_rbatt;
	int normal_temp_rbatt;
	int full_delta_vbatt1_mv;
	int full_delta_vbatt2_mv;
	int ex2_lower_ibatt_ma;
	int ex2_low_ibatt_ma;
	int ex2_high_ibatt_ma;
	int ex2_lower_ibatt_count;
	int ex2_low_ibatt_count;
	int ex2_high_ibatt_count;
	int dyna1_low_avg_dv_mv;
	int dyna1_high_avg_dv_mv;
	int dyna1_delta_dv_mv;
	int dyna2_low_avg_dv_mv;
	int dyna2_high_avg_dv_mv;
	int dyna2_delta_dv_mv;
	int is_recheck_on;
	int is_switch_on;
	int is_feature_sw_on;
	int is_feature_hw_on;
	u8 ic_volt_threshold;
	bool ic_short_otp_st;
	int err_code;
	int update_change;
	bool in_idle;
	bool cv_satus;
	bool disable_rechg;
	bool limit_chg;
	bool limit_rechg;
	bool shortc_gpio_status;
};

#ifdef OPLUS_CHG_OP_DEF
enum oplus_chg_limit_level {
	CURR_LIMIT_WARP_3_6A_SWARP_2_5A = 0x1,
	CURR_LIMIT_WARP_2_5A_SWARP_2_0A,
	CURR_LIMIT_WARP_3_0A_SWARP_3_0A,
	CURR_LIMIT_WARP_4_0A_SWARP_4_0A,
	CURR_LIMIT_WARP_5_0A_SWARP_5_0A,
	CURR_LIMIT_WARP_6_0A_SWARP_6_5A,
	CURR_LIMIT_MAX,
};

enum oplus_chg_linit_level_7bit {
	CURR_LIMIT_7BIT_1_0A = 0x01,
	CURR_LIMIT_7BIT_1_5A,
	CURR_LIMIT_7BIT_2_0A,
	CURR_LIMIT_7BIT_2_5A,
	CURR_LIMIT_7BIT_3_0A,
	CURR_LIMIT_7BIT_3_5A,
	CURR_LIMIT_7BIT_4_0A,
	CURR_LIMIT_7BIT_4_5A,
	CURR_LIMIT_7BIT_5_0A,
	CURR_LIMIT_7BIT_5_5A,
	CURR_LIMIT_7BIT_6_0A,
	CURR_LIMIT_7BIT_6_3A,
	CURR_LIMIT_7BIT_6_5A,
	CURR_LIMIT_7BIT_7_0A,
	CURR_LIMIT_7BIT_7_5A,
	CURR_LIMIT_7BIT_8_0A,
	CURR_LIMIT_7BIT_8_5A,
	CURR_LIMIT_7BIT_9_0A,
	CURR_LIMIT_7BIT_9_5A,
	CURR_LIMIT_7BIT_10_0A,
	CURR_LIMIT_7BIT_10_5A,
	CURR_LIMIT_7BIT_11_0A,
	CURR_LIMIT_7BIT_11_5A,
	CURR_LIMIT_7BIT_12_0A,
	CURR_LIMIT_7BIT_12_5A,
	CURR_LIMIT_7BIT_MAX,
};

struct oplus_chg_dynamic_config {
	struct oplus_chg_strategy_data warp_chg_led_on_strategy_data[CHG_STRATEGY_DATA_TABLE_MAX];
	struct oplus_chg_strategy_data pd9v_chg_led_on_strategy_data[CHG_STRATEGY_DATA_TABLE_MAX];
	struct oplus_chg_strategy_data pd5v_chg_led_on_strategy_data[CHG_STRATEGY_DATA_TABLE_MAX];
	struct oplus_chg_strategy_data swarp_chg_led_on_strategy_data[CHG_STRATEGY_DATA_TABLE_MAX];
	struct oplus_chg_strategy_data swarp_chg_led_off_strategy_data[CHG_STRATEGY_DATA_TABLE_MAX];
	struct oplus_chg_strategy_data swarp_chg_led_off_strategy_data_high[CHG_STRATEGY_DATA_TABLE_MAX];
	struct oplus_chg_strategy_data normal_chg_led_off_strategy_data[CHG_STRATEGY_DATA_TABLE_MAX];
	struct oplus_chg_strategy_data swarp_general_chg_strategy_data_low[CHG_STRATEGY_DATA_TABLE_MAX];
	struct oplus_chg_strategy_data swarp_general_chg_strategy_data_high[CHG_STRATEGY_DATA_TABLE_MAX];
};

void oplus_chg_strategy_init(struct oplus_chg_strategy *strategy,
			     struct oplus_chg_strategy_data *data,
			     int data_size, int temp);
int oplus_chg_strategy_get_data(
	struct oplus_chg_strategy *strategy,
	struct oplus_chg_strategy_temp_region *temp_region, int temp);
int oplus_chg_get_chg_strategy_data_len(struct oplus_chg_strategy_data data[],
					int max_len);
#endif

typedef enum {
	AGING_FFC_NOT_SUPPORT,
	AGING_FFC_V1,
	AGING_FFC_VERSION_MAX
} AGING_FFC_VERSION;

#define AGING1_STAGE_CYCLE	500
#define AGING2_STAGE_CYCLE	1000

#define AGING1_FFC1_SINGLE_OFFSET_MV	10
#define AGING1_FFC2_SINGLE_OFFSET_MV	10
#define AGING2_FFC1_SINGLE_OFFSET_MV	15
#define AGING2_FFC2_SINGLE_OFFSET_MV	15

#define AGING1_FFC1_DOUBLE_OFFSET_MV	15
#define AGING1_FFC2_DOUBLE_OFFSET_MV	10
#define AGING2_FFC1_DOUBLE_OFFSET_MV	30
#define AGING2_FFC2_DOUBLE_OFFSET_MV	20

struct oplus_chg_chip {
	struct i2c_client *client;
	struct device *dev;
	const struct oplus_chg_operations *chg_ops;
#ifndef CONFIG_OPLUS_CHG_GKI_SUPPORT
	struct power_supply *ac_psy;
	struct power_supply_desc ac_psd;
	struct power_supply_config ac_cfg;
	struct power_supply_desc usb_psd;
	struct power_supply_config usb_cfg;
	struct power_supply_desc battery_psd;
	struct power_supply *usb_psy;
	struct power_supply *batt_psy;
#else
	struct oplus_chg_mod *wls_ocm;
	struct oplus_chg_mod *usb_ocm;
	struct oplus_chg_mod *comm_ocm;
	struct oplus_chg_mod *batt_ocm;
#endif
#ifndef CONFIG_OPLUS_CHARGER_MTK
	struct qcom_pmic pmic_spmi;
#endif
#ifdef CONFIG_OPLUS_CHARGER_MTK_CHGIC
	struct mtk_pmic chgic_mtk;
#endif
/*	struct battery_data battery_main	*/
#ifdef OPLUS_CHG_OP_DEF
	struct delayed_work ctrl_lcm_frequency;
	struct delayed_work check_abnormal_voltage_work;
	struct delayed_work recovery_chg_type_work;
#endif
	struct delayed_work update_work;
	struct delayed_work ui_soc_decimal_work;
	struct delayed_work  mmi_adapter_in_work;
	struct delayed_work  reset_adapter_work;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0))
	struct wake_lock suspend_lock;
#else
	struct wakeup_source *suspend_ws;
#endif
	struct oplus_chg_limits limits;
	struct tbatt_anti_shake anti_shake_bound;
	struct tbatt_normal_anti_shake tbatt_normal_anti_shake_bound;
	struct short_c_batt_data short_c_batt;

#ifdef OPLUS_CHG_OP_DEF
	bool svid_verified;
	bool is_oplus_svid;
	bool usb_enum_status;
	bool charger_exist_delay; /* Delay synchronization of the state of charger_exist */
#ifdef OPLUS_CHG_REG_DUMP_ENABLE
	bool reg_dump;
#endif
	int chg_strategy_batt_curr_ma;
	struct oplus_chg_strategy *strategy;
	struct oplus_chg_dynamic_config dynamic_config;
	int factory_mode;
	int reconnect_count;
	struct mutex update_work_lock;
	int norchg_reconnect_count;
	spinlock_t strategy_lock;
	int ibat_save[10];
#ifndef CONFIG_OPLUS_CHG_OOS
	struct delayed_work led_power_on_report_work;
#endif
#endif

	bool wireless_support;
	bool wpc_no_chargerpump;
	bool charger_exist;
#ifdef OPLUS_CHG_OP_DEF
	bool abnormal_volt_detected;
	bool hw_detected;
	bool support_abnormal_vol_check;
	int vph_voltage;
#endif
	int charger_type;
	int real_charger_type;
	int charger_volt;
	int charger_volt_pre;
	int sw_full_count;
	bool sw_full;
	bool hw_full_by_sw;
	bool hw_full;
	int qc_abnormal_check_count;
	int temperature;
	int tbatt_temp;
	int shell_temp;
	int offset_temp;
	int batt_volt;
	int vbatt_num;
	int batt_volt_max;
	int batt_volt_min;
	int vbatt_power_off;
	int vbatt_soc_1;
	int icharging;
	int ibus;
	int soc;
	int ui_soc;
	int smooth_soc;
	int smooth_switch;
	int soc_load;
	int ui_soc_decimal;
	int ui_soc_integer;
	int last_decimal_ui_soc;
	int init_decimal_ui_soc;
	int calculate_decimal_time;
	bool boot_completed;
	bool authenticate;
#ifdef OPLUS_CHG_OP_DEF
	bool bat_volt_different;
#endif
	bool hmac;
	int batt_fcc;
	int batt_cc;
	int batt_soh;
	int batt_rm;
	int batt_capacity_mah;
	int tbatt_pre_shake;
	int tbatt_normal_pre_shake;
	bool batt_exist;
	bool batt_full;
	int  tbatt_when_full;
	bool chging_on;
	bool in_rechging;
	int charging_state;
	int total_time;
	unsigned long sleep_tm_sec;
	bool vbatt_over;
	bool chging_over_time;
	int vchg_status;
	int tbatt_status;
	int tbatt_normal_status;
	int prop_status;
	int stop_voter;
	int notify_code;
	int notify_flag;
	int cool_down;
	int smart_charge_user;
	int usbtemp_cool_down;
	bool usbtemp_check;
	bool led_on;
	bool led_on_change;
	bool led_temp_change;
	int led_temp_status;
	bool warp_temp_change;
	int warp_temp_status;
	bool camera_on;
	bool calling_on;
	bool ac_online;
	bool cool_down_done;
#ifdef	 CONFIG_OPLUS_CHARGER_MTK
	bool usb_online;
#endif
	bool otg_online;
	bool otg_switch;
	bool ui_otg_switch;
	int mmi_chg;
	int debug_batt_cc;
	int aging_ffc_version;
#ifdef OPLUS_CHG_OP_DEF
	bool charging_suspend;
	bool start_pd_check;
	bool chg_config_init;
	bool usb_chg_disable;
	bool chg_redetect_charger_type;
	int pd_config_count;
#endif
	int unwakelock_chg;
	int stop_chg;
	int mmi_fastchg;
	int boot_reason;
	int boot_mode;
	int warp_project;
	bool suspend_after_full;
	bool check_batt_full_by_sw;
	bool external_gauge;
	bool chg_ctrl_by_lcd;
	bool chg_ctrl_by_lcd_default;
	bool chg_ctrl_by_camera;
	bool chg_ctrl_by_cool_down;
	bool bq25890h_flag;
	bool chg_ctrl_by_calling;
	bool chg_ctrl_by_warp;
	bool chg_ctrl_by_warp_default;
	bool fg_bcl_poll;
	bool chg_powersave;
	bool healthd_ready;
// #ifdef CONFIG_FB nick.hu todo
	struct notifier_block chg_fb_notify;
// #endif
	struct normalchg_gpio_pinctrl normalchg_gpio;
	int chargerid_volt;
	bool chargerid_volt_got;
	int enable_shipmode;
	int dod0_counts;
	bool recharge_after_full;
	bool recharge_after_ffc;
	bool ffc_support;
	bool dual_ffc;
	bool new_ui_warning_support;
	bool fastchg_to_ffc;
	bool waiting_for_ffc;
	int fastchg_ffc_status;
	int ffc_temp_status;
	bool allow_swtich_to_fastchg;
	bool platform_fg_flag;
	struct task_struct *shortc_thread;
	int usbtemp_volt;
	int usb_temp;
	int usbtemp_volt_l;
	int usbtemp_volt_r;
	int usb_temp_l;
	int usb_temp_r;
#ifdef OPLUS_CHG_OP_DEF
	int input_current_limit_ma;
	enum oplus_chg_usb_type oplus_usb_type;
#endif
	struct task_struct *tbatt_pwroff_task;
	bool dual_charger_support;
	int slave_pct;
	bool slave_charger_enable;
	bool cool_down_force_5v;
	int slave_chg_enable_ma;
	int slave_chg_disable_ma;
	bool dischg_flag;
	bool smart_charging_screenoff;
	int usb_status;
	int *con_volt;
	int *con_temp;
	int len_array;
	wait_queue_head_t oplus_usbtemp_wq;
	int usbtemp_batttemp_gap;
	int smooth_to_soc_gap;
	int smart_charge_version;
	int ui_soc_decimal_speedmin;
	bool decimal_control;
	bool warp_show_ui_soc_decimal;
	struct thermal_zone_device *shell_themal;
	int swarp_disconnect_count;
	struct device_node *fast_node;
	const struct oplus_chg_operations *sub_chg_ops;
	bool  is_double_charger_support;
	int pd_swarp;
	int pd_chging;
#ifdef OPLUS_CHG_OP_DEF
	int disconnect_vbus;
	int check_battery_vol_count;
#endif
};



struct oplus_chg_operations {
	void (*get_usbtemp_volt)(struct oplus_chg_chip *chip);
	void  (*set_typec_sinkonly)(void);
	bool (*oplus_usbtemp_monitor_condition)(void);
	int (*recovery_usbtemp)(void *data);
	void (*dump_registers)(void);
	int (*kick_wdt)(void);
	int (*hardware_init)(void);
	int (*charging_current_write_fast)(int cur);
	int (*set_wls_boost_en)(bool enable);
	int (*input_current_ctrl_by_warp_write)(int cur);
	void (*set_aicl_point)(int vbatt);
	int (*input_current_write)(int cur);
	int (*get_input_current_max)(int *curr_ua);
	int (*debug_disable_chg)(bool disable);
	int (*float_voltage_write)(int cur);
	int (*term_current_set)(int cur);
	int (*charging_enable)(void);
	int (*charging_disable)(void);
	int (*get_charging_enable)(void);
	int (*charger_suspend)(void);
	int (*charger_unsuspend)(void);
	int (*set_rechg_vol)(int vol);
	int (*reset_charger)(void);
	int (*read_full)(void);
	int (*otg_enable)(void);
	int (*otg_disable)(void);
	int (*set_charging_term_disable)(void);
	bool (*check_charger_resume)(void);
	int (*get_charger_type)(void);
	int (*get_charger_volt)(void);
	int (*get_ibus)(void);
	int (*get_charger_current)(void);
	int (*get_real_charger_type)(void);
	int (*get_chargerid_volt)(void);
	void (*set_chargerid_switch_val)(int value);
	int (*get_chargerid_switch_val)(void);
	bool (*check_chrdet_status)(void);
#ifdef CONFIG_OPLUS_CHG_OOS
	enum oem_boot_mode (*get_boot_mode)(void);
#else
	int (*get_boot_mode)(void);
#endif
	int (*get_boot_reason)(void);
	int (*get_instant_vbatt)(void);
	int (*get_rtc_soc)(void);
	int (*set_rtc_soc)(int val);
	void (*set_power_off)(void);
	void (*usb_connect)(void);
	void (*usb_disconnect)(void);
#ifndef CONFIG_OPLUS_CHARGER_MTK
	int (*get_aicl_ma)(void);
	void(*rerun_aicl)(void);
	int (*tlim_en)(bool);
	int (*set_system_temp_level)(int);
	int(*otg_pulse_skip_disable)(enum skip_reason, bool);
	int(*set_dp_dm)(int);
	int(*calc_flash_current)(void);
#endif
	int (*get_chg_current_step)(void);
	bool (*need_to_check_ibatt)(void);
#ifdef CONFIG_OPLUS_RTC_DET_SUPPORT
	int (*check_rtc_reset)(void);
#endif /* CONFIG_OPLUS_RTC_DET_SUPPORT */
	int (*get_dyna_aicl_result) (void);
	bool (*get_shortc_hw_gpio_status)(void);
	void (*check_is_iindpm_mode) (void);
	bool (*oplus_chg_get_pd_type) (void);
	int (*oplus_chg_pd_setup) (void);
	int (*get_charger_subtype)(void);
	int (*set_qc_config)(void);
	int (*enable_qc_detect)(void);
	int (*input_current_write_without_aicl)(int current_ma);
	int (*set_charger_vsys_threshold)(int val);
	int (*enable_burst_mode)(bool enable);
	void (*oplus_chg_wdt_enable)(bool wdt_enable);
	int (*oplus_chg_set_high_vbus)(bool en);
	int (*oplus_chg_set_hz_mode)(bool en);
	int (*enable_shipmode)(bool en);
	bool (*check_pdphy_ready)(void);
#ifdef OPLUS_CHG_OP_DEF
	void (*disconnect_vbus)(bool enable);
	int (*oplus_chg_get_charge_counter)(void);
	bool (*otg_set_switch)(bool enable);
	int (*disconnect_pd)(bool);
	int(*pdo_5v)(void);
	int (*chg_lcm_en)(bool);
	int (*get_vph_volt)(void);
	int (*get_hw_detect)(void);
	void (*report_vol_status)(void);
	void(*rerun_apsd)(void);
	void (*update_usb_type)(void);
#endif
};


/*********************************************
 * power_supply usb/ac/battery functions
 **********************************************/

#ifndef CONFIG_OPLUS_CHG_GKI_SUPPORT
extern int oplus_usb_get_property(struct power_supply *psy,
	enum power_supply_property psp,
	union power_supply_propval *val);
extern int oplus_usb_property_is_writeable(struct power_supply *psy,
	enum power_supply_property psp);
extern int oplus_usb_set_property(struct power_supply *psy,
	enum power_supply_property psp,
	const union power_supply_propval *val);
extern int oplus_ac_get_property(struct power_supply *psy,
	enum power_supply_property psp,
	union power_supply_propval *val);
extern int oplus_battery_property_is_writeable(struct power_supply *psy,
	enum power_supply_property psp);
extern int oplus_battery_set_property(struct power_supply *psy,
	enum power_supply_property psp,
	const union power_supply_propval *val);
extern int oplus_battery_get_property(struct power_supply *psy,
	enum power_supply_property psp,
	union power_supply_propval *val);
#endif


/*********************************************
 * oplus_chg_init - initialize oplus_chg_chip
 * @chip: pointer to the oplus_chg_chip
 * @clinet: i2c client of the chip
 *
 * Returns: 0 - success; -1/errno - failed
 **********************************************/
int oplus_chg_parse_swarp_dt(struct oplus_chg_chip *chip);
int oplus_chg_parse_charger_dt(struct oplus_chg_chip *chip);

int oplus_chg_init(struct oplus_chg_chip *chip);
void oplus_charger_detect_check(struct oplus_chg_chip *chip);
int oplus_chg_get_prop_batt_health(struct oplus_chg_chip *chip);

bool oplus_chg_wake_update_work(void);
void oplus_chg_soc_update_when_resume(unsigned long sleep_tm_sec);
void oplus_chg_soc_update(void);
int oplus_chg_get_batt_volt(void);
int oplus_chg_get_cool_bat_decidegc(void);
int oplus_chg_get_little_cool_bat_decidegc(void);
int oplus_chg_get_normal_bat_decidegc(void);
int oplus_chg_get_icharging(void);

int oplus_chg_get_ui_soc(void);
int oplus_chg_get_soc(void);
int oplus_chg_get_chg_temperature(void);

void oplus_chg_kick_wdt(void);
void oplus_chg_disable_charge(void);
void oplus_chg_unsuspend_charger(void);
void oplus_chg_suspend_charger(void);

int oplus_chg_get_chg_type(void);

int oplus_chg_get_notify_flag(void);
int oplus_is_warp_project(void);

int oplus_chg_show_warp_logo_ornot(void);

bool get_otg_switch(void);

#ifdef CONFIG_OPLUS_CHARGER_MTK
bool oplus_chg_get_otg_online(void);
void oplus_chg_set_otg_online(bool online);
#endif

bool oplus_chg_get_batt_full(void);
bool oplus_chg_get_rechging_status(void);

bool oplus_chg_check_chip_is_null(void);
void oplus_chg_set_charger_type_unknown(void);
int oplus_chg_get_charger_voltage(void);
int oplus_chg_update_voltage(void);

void oplus_chg_set_chargerid_switch_val(int value);
void oplus_chg_turn_on_charging(struct oplus_chg_chip *chip);
int oplus_chg_get_cool_down_status(void);
void oplus_smart_charge_by_cool_down(struct oplus_chg_chip *chip, int val);
int oplus_convert_current_to_level(struct oplus_chg_chip *chip, int val);
void oplus_smart_charge_by_shell_temp(struct oplus_chg_chip *chip, int val);
int oplus_chg_override_by_shell_temp(int temp);
int oplus_chg_get_shell_temp(void);
void oplus_chg_clear_chargerid_info(void);
#ifndef CONFIG_OPLUS_CHARGER_MTK
void oplus_chg_variables_reset(struct oplus_chg_chip *chip, bool in);
void oplus_chg_external_power_changed(struct power_supply *psy);
#endif
int oplus_is_rf_ftm_mode(void);
int oplus_get_charger_chip_st(void);
void oplus_chg_set_allow_switch_to_fastchg(bool allow);
int oplus_tbatt_power_off_task_init(struct oplus_chg_chip *chip);
void oplus_tbatt_power_off_task_wakeup(void);
bool oplus_get_chg_powersave(void);
int oplus_get_chg_unwakelock(void);
void oplus_chg_set_input_current_without_aicl(int current_ma);
void oplus_chg_config_charger_vsys_threshold(int val);
void oplus_chg_enable_burst_mode(bool enable);
void oplus_chg_set_charger_otg_enable(bool enable);
int oplus_chg_get_tbatt_status(void);
int oplus_chg_get_tbatt_normal_charging_current(struct oplus_chg_chip *chip);
bool oplus_get_vbatt_higherthan_xmv(void);
bool oplus_chg_wake_up_ui_soc_decimal(void);
void oplus_chg_ui_soc_decimal_init(void);
bool oplus_chg_get_boot_completed(void);
int oplus_chg_match_temp_for_chging(void);
void oplus_chg_reset_adapter(void);

#ifdef OPLUS_CHG_OP_DEF
struct oplus_chg_chip *oplus_chg_get_chg_struct(void);
void oplus_chg_cancel_update_work_sync(void);
void oplus_chg_turn_off_charging(struct oplus_chg_chip *chip);
void oplus_chg_set_led_status(bool val);
int oplus_chg_match_temp_for_chging(void);
void oplus_chg_set_camera_status(bool val);
void oplus_chg_update_float_voltage_by_fastchg(bool fastchg_en);
void oplus_check_ovp_status(struct oplus_chg_chip *chg);
#endif

void oplus_chg_get_aging_ffc_offset(struct oplus_chg_chip *chip, int *ffc1_offset, int *ffc2_offset);

#endif /*_OPLUS_CHARGER_H_*/
