#ifndef __OPLUS_DEBUG_INFO__H
#define __OPLUS_DEBUG_INFO__H

#include "oplus_charger.h"

enum oplus_chg_debug_info_notify_type {
	OPLUS_CHG_DEBUG_NOTIFY_TYPE_SOC_JUMP,
	OPLUS_CHG_DEBUG_NOTIFY_TYPE_BATT_FCC,
	OPLUS_CHG_DEBUG_NOTIFY_TYPE_CHG_SLOW,
	OPLUS_CHG_DEBUG_NOTIFY_TYPE_CHG_BREAK,
	OPLUS_CHG_DEBUG_NOTIFY_TYPE_GAUGE_ERROR,
	OPLUS_CHG_DEBUG_NOTIFY_TYPE_WIRELESS,/*add for wireless chg*/
	OPLUS_CHG_DEBUG_NOTIFY_TYPE_RECHG,/*add for rechg counts*/
	OPLUS_CHG_DEBUG_NOTIFY_TYPE_MAX,
};

enum oplus_chg_debug_info_notify_flag {
	OPLUS_NOTIFY_BATT_SOC_CAPCITY_LOAD_JUMP,
	OPLUS_NOTIFY_BATT_UI_SOC_CAPCITY_LOAD_JUMP,
	OPLUS_NOTIFY_BATT_SOC_CAPCITY_JUMP,
	OPLUS_NOTIFY_BATT_UI_SOC_CAPCITY_JUMP,
	OPLUS_NOTIFY_BATT_UI_TO_SOC_CAPCITY_JUMP,
	OPLUS_NOTIFY_CHG_BATT_FULL_NON_100_CAP,//soc end
	OPLUS_NOTIFY_CHG_SLOW_BATT_WARM_TEMP,//slow start
	OPLUS_NOTIFY_CHG_SLOW_BATT_COLD_TEMP,
	OPLUS_NOTIFY_CHG_SLOW_LOW_CHARGE_CURRENT_LONG_TIME,
	OPLUS_NOTIFY_CHG_SLOW_CHG_TYPE_SDP,
	OPLUS_NOTIFY_CHG_SLOW_WARP_NON_START,
	OPLUS_NOTIFY_CHG_SLOW_WARP_ADAPTER_NON_MAX_POWER,
	OPLUS_NOTIFY_CHG_SLOW_COOLDOWN_LONG_TIME,
	OPLUS_NOTIFY_CHG_SLOW_SYS_POWER_CONSUME_HIGH,
	OPLUS_NOTIFY_CHG_SLOW_LED_ON_LONG_TIME,//slow end
	OPLUS_NOTIFY_CHG_SLOW_BATT_NON_AUTH,//ic start
	OPLUS_NOTIFY_CHG_SLOW_CHARGER_OV,
	OPLUS_NOTIFY_GAUGE_SEAL_FAIL,
	OPLUS_NOTIFY_GAUGE_UNSEAL_FAIL,
	OPLUS_NOTIFY_CHG_UNSUSPEND,/*add for unsuspend platPmic*/
	OPLUS_NOTIFY_WARPPHY_ERR,/*add for warpPhy chg*/
	OPLUS_NOTIFY_MCU_UPDATE_FAIL, //ic end
	OPLUS_NOTIFY_CHG_BATT_RECHG,/*add for rechg counts*/
	OPLUS_NOTIFY_BATT_AGING_CAP,
	OPLUS_NOTIFY_CHG_WARP_BREAK,
	OPLUS_NOTIFY_CHG_GENERAL_BREAK,
	OPLUS_NOTIFY_CHARGER_INFO,
	OPLUS_NOTIFY_FAST_CHARGER_TIME,
	OPLUS_NOTIFY_MCU_ERROR_CODE,
	OPLUS_NOTIFY_WIRELESS_BOOTUP,/*add for wireless chg start*/
	OPLUS_NOTIFY_WIRELESS_START_CHG,
	OPLUS_NOTIFY_WIRELESS_WIRELESS_CHG_BREAK,
	OPLUS_NOTIFY_WIRELESS_START_TX,
	OPLUS_NOTIFY_WIRELESS_STOP_TX,
	OPLUS_NOTIFY_WIRELESS_WIRELESS_CHG_END,/*add for wireless chg end*/

	OPLUS_NOTIFY_CHG_MAX_CNT,
};

struct wireless_chg_debug_info {
	int boot_version;
	int rx_version;
	int tx_version;
	int dock_version;
	int adapter_type;
	bool fastchg_ing;
	int vout;
	int iout;
	int rx_temperature;
	int wpc_dischg_status;
	int work_silent_mode;
	int break_count;
	int wpc_chg_err;
	int highest_temp;
	int max_iout;
	int min_cool_down;
	int min_skewing_current;
	int wls_auth_fail;
};/*add for wireless chg*/

enum GAUGE_SEAL_UNSEAL_ERROR{
	OPLUS_GAUGE_SEAL_FAIL,
	OPLUS_GAUGE_UNSEAL_FIAL,
};

extern int oplus_chg_debug_info_init(struct oplus_chg_chip *chip);
extern int oplus_chg_debug_chg_monitor(struct oplus_chg_chip *chip);
extern int oplus_chg_debug_set_cool_down_by_user(int is_cool_down);
extern int oplus_chg_debug_get_cooldown_current(int chg_current_by_tbatt, int chg_current_by_cooldown);
extern int oplus_chg_debug_set_soc_info(struct oplus_chg_chip *chip);
extern void oplus_chg_gauge_seal_unseal_fail(int type);
extern void oplus_chg_warp_mcu_error( int error );
extern void oplus_chg_set_fast_chg_type(int value);
int oplus_chg_get_soh_report(void);
int oplus_chg_get_cc_report(void);
extern void oplus_chg_wireless_error(int error,  struct wireless_chg_debug_info *wireless_param);/*add for wireless chg*/
extern int oplus_chg_unsuspend_plat_pmic(struct oplus_chg_chip *chip);/*add for unsuspend platPmic*/
extern int oplus_chg_warpphy_err(void);/*add for warpPhy chg*/
#endif

