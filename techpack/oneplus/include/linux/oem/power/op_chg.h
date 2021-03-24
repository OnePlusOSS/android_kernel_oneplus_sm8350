#ifndef __OP_CHG__
#define __OP_CHG__

#include <linux/regmap.h>

#define CONFIG_FAST_MCU
#define CONFIG_OP_CHG_DEBUG

#define CHG_TIMEOUT_COUNT               6000 /* 10hr */
#define BATT_SOFT_OVP_MV                4450
#define HEARTBEAT_INTERVAL_MS           6000
#define BATT_TEMP_HYST                  20
#define BATT_NON_TEMP                   (-400)
#define DASH_VALID_TEMP_LOW_THRESHOLD	125
#define DASH_VALID_TEMP_HIG_THRESHOLD	430
#define NON_STANDARD_CHARGER_CHECK_S    100
#define FFC_CHG_STEP_MAX                4

#define DEFAULT_VOTER                "DEFAULT_VOTER"
#define HW_MAX_VOTER                 "HW_MAX_VOTER"
#define FFC_VOTER                    "FFC_VOTER"
#define CHG_MODE_VOTER               "CHG_MODE_VOTER"
#define DISABLE_CHG_VOTER            "DISABLE_CHG_VOTER"

#define DCDC_BASE	0X2700
#define USBIN_BASE	0X2900
#define TYPEC_BASE	0X2B00

#define INT_RT_STS_OFFSET	0x10

#define USBIN_CMD_IL_REG			(USBIN_BASE + 0x54)
#define USBIN_SUSPEND_BIT			BIT(0)

#define DCDC_BST_EN_REG				(DCDC_BASE + 0x50)
#define OTG_EN_BIT				BIT(0)

#define USBIN_PLUGIN_RT_STS_BIT			BIT(0)

#define TYPE_C_MISC_STATUS_REG			(TYPEC_BASE + 0x0B)
#define TYPEC_WATER_DETECTION_STATUS_BIT	BIT(7)
#define SNK_SRC_MODE_BIT			BIT(6)
#define TYPEC_VBUS_ERROR_STATUS_BIT		BIT(4)
#define TYPEC_TCCDEBOUNCE_DONE_STATUS_BIT	BIT(3)
#define CC_ORIENTATION_BIT			BIT(1)
#define CC_ATTACHED_BIT				BIT(0)

#define TYPE_C_CID_STATUS_REG			(TYPEC_BASE + 0x81)
#define CID_STATUS_BIT				BIT(0)

enum temp_region_type {
	BATT_TEMP_COLD = 0,
	BATT_TEMP_LITTLE_COLD,
	BATT_TEMP_COOL,
	BATT_TEMP_LITTLE_COOL,
	BATT_TEMP_PRE_NORMAL,
	BATT_TEMP_NORMAL,
	BATT_TEMP_WARM,
	BATT_TEMP_HOT,
	BATT_TEMP_INVALID,
};

enum ffc_status {
	FFC_DEFAULT = 0,
	FFC_FAST,
	FFC_IDLE,
};

enum ffc_temp_region {
	FFC_TEMP_COOL,
	FFC_TEMP_PRE_NORMAL,
	FFC_TEMP_NORMAL,
	FFC_TEMP_WARM,
	FFC_TEMP_INVALID,
};

enum batt_status_type {
	BATT_STATUS_GOOD,
	BATT_STATUS_BAD_TEMP, /* cold or hot */
	BATT_STATUS_BAD,
	BATT_STATUS_REMOVED, /* on v2.2 only */
	BATT_STATUS_INVALID_v1 = BATT_STATUS_REMOVED,
	BATT_STATUS_INVALID
};

enum charge_mode {
	CHG_MODE_AC_5V,
	CHG_MODE_QC_HV,
	CHG_MODE_QC_LV,
	CHG_MODE_WARP_HV,
	CHG_MODE_WARP_LV,
	CHG_MODE_PD_HV,
	CHG_MODE_PD_LV,
	CHG_MODE_WLS_BPP,
	CHG_MODE_WLS_EPP,
	CHG_MODE_WLS_EPP_PLUS,
	CHG_MODE_MAX,
};

enum dpdm_switch_mode {
	DPDM_SWITCH_MODE_NORMAL,
	DPDM_SWITCH_MODE_FAST,
};

enum chg_protect_status_type {
	PROTECT_CHG_OVP = 1,
	PROTECT_BATT_MISSING,
	PROTECT_CHG_OVERTIME,
	PROTECT_BATT_OVP,
	PROTECT_BATT_TEMP_REGION_HOT,
	PROTECT_BATT_TEMP_REGION_COLD,
	PROTECT_BATT_TEMP_REGION_LITTLE_COLD,
	PROTECT_BATT_TEMP_REGION_COOL,
	PROTECT_BATT_TEMP_REGION_WARM,
	PROTECT_INVALID_CHARGER,
};

enum raw_charger_type {
	RAW_TYPE_NONE,
	RAW_TYPE_UNKNOWN,
	RAW_TYPE_SNK_USB_SDP,
	RAW_TYPE_SNK_USB_OCP,
	RAW_TYPE_SNK_USB_CDP,
	RAW_TYPE_SNK_USB_DCP,
	RAW_TYPE_SNK_USB_FLOAT,
	RAW_TYPE_SNK_TYPEC_DEFAULT,
	RAW_TYPE_SNK_TYPEC_RP_MEDIUM_1P5A,
	RAW_TYPE_SNK_TYPEC_RP_HIGH_3A,
	RAW_TYPE_SNK_USB_QC_2P0,
	RAW_TYPE_SNK_USB_QC_3P0,
	RAW_TYPE_SNK_PD,
	RAW_TYPE_SNK_PPS,
	RAW_TYPE_SRC_TYPEC_POWERCABLE,                 //RD-RA
	RAW_TYPE_SRC_TYPEC_UNORIENTED_DEBUG_ACCESS,    //RD/RD
	RAW_TYPE_SRC_TYPEC_AUDIO_ACCESS,               //RA/RA
	RAW_TYPE_WLS_SRC_BPP,
	RAW_TYPE_WLS_SNK_BPP,
	RAW_TYPE_WLS_SNK_EPP,
	RAW_TYPE_WLS_SNK_PDDE,
	RAW_TYPE_MAX,
};

enum oplus_chg_usb_type {
	OPLUS_CHG_USB_UNKNOWN,
	OPLUS_CHG_USB_SDP,
	OPLUS_CHG_USB_DCP,
	OPLUS_CHG_USB_CDP,
	OPLUS_CHG_USB_OCP,
	OPLUS_CHG_USB_FLOAT,
	OPLUS_CHG_USB_PD,
	OPLUS_CHG_USB_PPS,
	OPLUS_CHG_USB_QC2,
	OPLUS_CHG_USB_QC3,
	OPLUS_CHG_USB_WARP,
	OPLUS_CHG_USB_SWARP,
};

enum oplus_chg_wls_type {
	OPLUS_CHG_WLS_UNKNOWN,
	OPLUS_CHG_WLS_BPP,
	OPLUS_CHG_WLS_EPP,
	OPLUS_CHG_WLS_EPP_PLUS,
	OPLUS_CHG_WLS_WARP,
	OPLUS_CHG_WLS_SWARP,
};

enum oplus_wls_ftm_test_type {
	OPLUS_WLS_FTM_NONE,
	OPLUS_WLS_FTM_UPGRADE_FW,
	OPLUS_WLS_FTM_NORMAL_CHG,
	OPLUS_WLS_FTM_FAST_CHG,
	OPLUS_WLS_FTM_TRX,
	OPLUS_WLS_FTM_FAST_MAX,
};

struct op_chg_cfg {
	bool check_batt_full_by_sw;
	bool support_qc;

	int fv_offset_voltage_mv;
	int little_cold_iterm_ma;
	int sw_iterm_ma;
	int full_count_sw_num;
	int batt_uv_mv;
	int batt_ov_mv;
	int batt_oc_ma;
	int batt_ovd_mv;  //Double cell pressure difference is too large;
	int batt_temp_thr[BATT_TEMP_INVALID - 1];
	int vbatmax_mv[BATT_TEMP_INVALID];
	int usb_vbatdet_mv[BATT_TEMP_INVALID];
	int wls_vbatdet_mv[BATT_TEMP_INVALID];
	int ibatmax_ma[CHG_MODE_MAX][BATT_TEMP_INVALID];
	int ffc_temp_thr[FFC_TEMP_INVALID - 1];
	int usb_ffc_step_max;
	int usb_ffc_fv_mv[FFC_CHG_STEP_MAX];
	int usb_ffc_fcc_ma[FFC_CHG_STEP_MAX][FFC_TEMP_INVALID - 2];
	int usb_ffc_cutoff_ma[FFC_CHG_STEP_MAX][FFC_TEMP_INVALID - 2];
	int wls_ffc_step_max;
	int wls_ffc_fv_mv[FFC_CHG_STEP_MAX];
	int wls_ffc_icl_ma[FFC_CHG_STEP_MAX][FFC_TEMP_INVALID - 2];
	int wls_ffc_fcc_ma[FFC_CHG_STEP_MAX][FFC_TEMP_INVALID - 2];
	int wls_ffc_cutoff_ma[FFC_CHG_STEP_MAX][FFC_TEMP_INVALID - 2];
};

struct op_chg_status {
	int batt_temp_dynamic_thr[BATT_TEMP_INVALID - 1];
	int ffc_temp_dynamic_thr[FFC_TEMP_INVALID - 1];
	enum temp_region_type temp_region;
	enum ffc_temp_region ffc_temp_region;

	bool chg_ovp;
	bool is_power_changed;
	bool chg_done;
	bool recharge_pending;
	bool recharge_status;
	bool time_out;
	bool vbus_uovp;
	bool is_aging_test;
	bool usb_dischg;
	bool wls_dischg;
	bool dpdm_switched;
	bool warp_online;
	bool switch_on_fastchg;
	bool usb_type_changed;
	bool ffc_charging;
	bool qc_chg_configed;

	int vchg_max_mv;
	int vchg_min_mv;
	int ffc_step;

	enum charge_mode chg_mode;
	enum ffc_status ffc_status;
	enum batt_status_type battery_status;
	enum power_supply_usb_type usb_type;
	enum power_supply_wls_type wls_type;
	enum power_supply_op_chg_type warp_type;
};

struct op_chg_struct {
	struct device *dev;
	struct power_supply *main_op_psy;
	struct power_supply *usb_op_psy;
	struct power_supply *batt_op_psy;
	struct power_supply *wls_op_psy;
	struct power_supply *warp_op_psy;
	struct power_supply *bms_op_psy;
	struct power_supply *batt_pmic_op_psy;
	struct power_supply *batt_psy;
	struct power_supply *wls_psy;
	struct power_supply *usb_psy;
	struct notifier_block main_event_nb;
	struct notifier_block batt_event_nb;
	struct notifier_block main_psy_nb;

	struct op_chg_cfg config;
	struct op_chg_status status;

	struct wakeup_source *chg_wake_lock;
	struct mutex sw_dash_lock;

	struct delayed_work check_switch_dash_work;
	struct delayed_work non_standard_charger_check_work;
	struct delayed_work heartbeat_work;
	struct delayed_work re_det_work;
	struct delayed_work re_set_work;
	struct delayed_work check_apsd_work;
	struct delayed_work slow_chg_check_work;
	struct delayed_work rechk_sw_dsh_work;
	struct delayed_work usb_int_irq_work;
	struct work_struct plugin_handle_work;

	struct votable *fv_votable;
	struct votable *fcc_votable;
	struct votable *usb_icl_votable;
	struct votable *wls_icl_votable;

	struct iio_channel *skin_therm_chan;

	int usb_int_gpio;
	int usb_int_irq;

	bool dash_need_check_vbat;
	bool disallow_fast_high_temp;
	bool oem_lcd_is_on;
	bool disable_normal_chg_for_dash;
	bool usb_enum_status;
	bool non_std_chg_present;
	bool is_audio_adapter;
	bool init_irq_done;
	bool usb_online;
	bool wls_online;
	bool usb_present;
	bool wls_present;
	bool chg_wake_lock_on;
	bool fastchg_switch_disable;
	bool boot_usb_present;
	bool re_trigr_dash_done;
	bool use_fake_protect_sts;
	bool slow_charger;
	bool otg_mode;

	int ck_dash_count;
	int non_stand_chg_count;
	int fake_protect_sts;
	int ffc_fcc_count;
	int ffc_fv_count;
};

enum temp_region_type op_chg_get_temp_region(struct op_chg_struct *chg);
bool is_usb_charger_online(struct op_chg_struct *chg);
bool is_wls_charger_online(struct op_chg_struct *chg);
int op_chg_get_batt_temp(struct op_chg_struct *chg);
int op_chg_get_usb_charger_voltage(struct op_chg_struct *chg);
int op_chg_get_wls_charger_voltage(struct op_chg_struct *chg);
int op_chg_get_batt_voltage_now(struct op_chg_struct *chg);
int op_chg_get_batt_current_now(struct op_chg_struct *chg);
int op_chg_get_batt_capacity(struct op_chg_struct *chg);
int op_batt_init(struct op_chg_struct *chg);
int op_batt_deinit(struct op_chg_struct *chg);
int op_chg_switch_ffc(struct power_supply *psy);
const char *op_chg_get_raw_type_str(enum raw_charger_type type);

extern struct op_chg_struct *g_op_chg;
extern struct regmap *pm8350b_regmap;

#endif
