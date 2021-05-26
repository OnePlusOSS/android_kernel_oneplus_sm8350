#ifndef __OPLUS_OP_DEF_H__
#define __OPLUS_OP_DEF_H__

#ifdef CONFIG_OPLUS_CHG_OOS

#include <linux/oem/project_info.h>
#include <linux/oem/boot_mode.h>

struct manufacture_info {
	char *version;
	char *manufacture;
};

#define MSM_BOOT_MODE__NORMAL   MSM_BOOT_MODE_NORMAL
#define MSM_BOOT_MODE__FASTBOOT MSM_BOOT_MODE_FASTBOOT
#define MSM_BOOT_MODE__RECOVERY MSM_BOOT_MODE_RECOVERY
#define MSM_BOOT_MODE__FACTORY  MSM_BOOT_MODE_FACTORY
#define MSM_BOOT_MODE__RF       MSM_BOOT_MODE_RF
#define MSM_BOOT_MODE__WLAN     MSM_BOOT_MODE_RF
#define MSM_BOOT_MODE__MOS      MSM_BOOT_MODE_NORMAL
#define MSM_BOOT_MODE__CHARGE   MSM_BOOT_MODE_CHARGE
#define MSM_BOOT_MODE__SILENCE  MSM_BOOT_MODE_NORMAL
#define MSM_BOOT_MODE__SAU      MSM_BOOT_MODE_NORMAL
#define MSM_BOOT_MODE__AGING MSM_BOOT_MODE_AGING
#define MSM_BOOT_MODE__SAFE     MSM_BOOT_MODE_NORMAL

enum chg_protect_status_type {
	PROTECT_CHG_OVP = 1, /* 1: VCHG > 5.8V     */
	PROTECT_BATT_MISSING, /* 2: battery missing */
	PROTECT_CHG_OVERTIME, /* 3: charge overtime */
	PROTECT_BATT_OVP, /* 4: vbat >= 4.5     */
	PROTECT_BATT_TEMP_REGION__HOT,/* 5: 55 < t          */
	PROTECT_BATT_TEMP_REGION_COLD,/* 6:      t <= -3    */
	PROTECT_BATT_TEMP_REGION_LITTLE_COLD, /* 7: -3 < t <=  0  */
	PROTECT_BATT_TEMP_REGION_COOL,/* 8:  0 < t <=  5    */
	PROTECT_BATT_TEMP_REGION_WARM,/* 9: 45 < t <= 55   */
	PROTECT_INVALID_CHARGER,/*10:invalid charger or slow charger*/
	PROTECT_BATT_VOL_DIFF_TOO_LARGE,/*11:dual-cell battery voltage diff too large*/
	PROTECT_BATT_OVP_VOL_ABNORMAL/*12:abnormal vol check*/
};

enum oplus_aging_type {
	AGING_TEST_STATUS_DEFAULT = 0,
	HIGH_TEMP_AGING = 1,
	AUTOMATIC_AGING = 2,
	FACTORY_MODE_AGING = 3,
};

static int is_aging_test = AGING_TEST_STATUS_DEFAULT;

int __attribute__((weak)) get_eng_version()
{
	return is_aging_test;
}

int __attribute__((weak)) set_eng_version(int status)
{
	is_aging_test = status;
	return is_aging_test;
}

#endif /* CONFIG_OPLUS_CHG_OOS */

#define sid_to_adapter_id(sid) ((sid >> 24) & 0xff)
#define sid_to_adapter_power_warp(sid) ((sid >> 16) & 0xff)
#define sid_to_adapter_power_swarp(sid) ((sid >> 8) & 0xff)
#define sid_to_adapter_type(sid) ((enum oplus_adapter_type)((sid >> 4) & 0xf))
#define sid_to_adapter_chg_type(sid) ((enum oplus_adapter_chg_type)(sid & 0xf))
#define adapter_info_to_sid(id, power_warp, power_swarp, adapter_type, adapter_chg_type) \
	(((id & 0xff) << 24) | ((power_warp & 0xff) << 16) | \
	 ((power_swarp & 0xff) << 8) | ((adapter_type & 0xf) << 4) | \
	 (adapter_chg_type & 0xf))

enum oplus_adapter_type {
	ADAPTER_TYPE_UNKNOWN,
	ADAPTER_TYPE_AC,
	ADAPTER_TYPE_CAR,
	ADAPTER_TYPE_PB,  //power bank
};

enum oplus_adapter_chg_type {
	CHARGER_TYPE_UNKNOWN,
	CHARGER_TYPE_NORMAL,
	CHARGER_TYPE_WARP,
	CHARGER_TYPE_SWARP,
};

struct oplus_adapter_struct {
	unsigned char id_min;
	unsigned char id_max;
	unsigned char power_warp;
	unsigned char power_swarp;
	enum oplus_adapter_type adapter_type;
	enum oplus_adapter_chg_type adapter_chg_type;
};

#endif
