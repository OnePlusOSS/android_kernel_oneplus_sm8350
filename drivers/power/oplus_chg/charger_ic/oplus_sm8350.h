
#ifndef __SM8350_CHARGER_H
#define __SM8350_CHARGER_H

#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/rpmsg.h>
#include <linux/mutex.h>
#include <linux/power_supply.h>
#include <linux/soc/qcom/pmic_glink.h>
#include <linux/soc/qcom/battery_charger.h>
#ifdef CONFIG_OPLUS_CHG_OOS
#include <linux/oem/oplus_chg.h>
#else
#include <linux/oplus_chg.h>
#endif
#include "../oplus_chg_module.h"
#include "../oplus_chg.h"
#include "../oplus_chg_wls.h"
#include "../oplus_chg_ic.h"

#define MSG_OWNER_BC			32778
#define MSG_TYPE_REQ_RESP		1
#define MSG_TYPE_NOTIFY			2

/* opcode for battery charger */
#define BC_SET_NOTIFY_REQ		0x04
#define BC_NOTIFY_IND			0x07
#define BC_BATTERY_STATUS_GET		0x30
#define BC_BATTERY_STATUS_SET		0x31
#define BC_USB_STATUS_GET		0x32
#define BC_USB_STATUS_SET		0x33
#define BC_WLS_STATUS_GET		0x34
#define BC_WLS_STATUS_SET		0x35
#define BC_SHIP_MODE_REQ_SET		0x36
#define BC_WLS_FW_CHECK_UPDATE		0x40
#define BC_WLS_FW_PUSH_BUF_REQ		0x41
#define BC_WLS_FW_UPDATE_STATUS_RESP	0x42
#define BC_WLS_FW_PUSH_BUF_RESP		0x43
#define BC_WLS_FW_GET_VERSION		0x44
#define BC_GENERIC_NOTIFY		0x80

#define BC_USB_STATUS_ONLINE		0x39
#define BC_USB_STATUS_OFFLINE		0x3a
#define BC_WLS_STATUS_ONLINE		0x3b
#define BC_WLS_STATUS_OFFLINE		0x3c
#define BC_USB_STATUS_PRESENT		0x3d
#define BC_USB_STATUS_APSD_DONE		0x3e
#define BC_USB_STATUS_NO_PRESENT	0x3f
#define BC_OTG_ENABLE			0x46
#define BC_OTG_DISABLE			0x47
#define BC_OPLUS_SVID			0x48
#define BC_OTHER_SVID			0x49

/* Generic definitions */
#define MAX_STR_LEN			128
#define BC_WAIT_TIME_MS			2000//sjc 1K->2K
#define WLS_FW_PREPARE_TIME_MS		300
#define WLS_FW_WAIT_TIME_MS		500
#define WLS_FW_BUF_SIZE			128
#define PM8350B_BOOST_VOL_MIN_MV 4800
#ifndef CONFIG_OPLUS_CHG_OOS
#define USB_TEMP_HIGH				0x01
#endif

enum ocm_type {
	OCM_TYPE_BATTERY,
	OCM_TYPE_USB,
	OCM_TYPE_WLS,
	OCM_TYPE_COMM,
	OCM_TYPE_MAX,
};

enum ship_mode_type {
	SHIP_MODE_PMIC,
	SHIP_MODE_PACK_SIDE,
};

/* property ids */
enum battery_property_id {
	BATT_STATUS,
	BATT_HEALTH,
	BATT_PRESENT,
	BATT_CHG_TYPE,
	BATT_CAPACITY,
	BATT_SOH,
	BATT_VOLT_OCV,
	BATT_VOLT_NOW,
	BATT_VOLT_MAX,
	BATT_CURR_NOW,
	BATT_CHG_CTRL_LIM,
	BATT_CHG_CTRL_LIM_MAX,
	BATT_TEMP,
	BATT_TECHNOLOGY,
	BATT_CHG_COUNTER,
	BATT_CYCLE_COUNT,
	BATT_CHG_FULL_DESIGN,
	BATT_CHG_FULL,
	BATT_MODEL_NAME,
	BATT_TTF_AVG,
	BATT_TTE_AVG,
	BATT_RESISTANCE,
	BATT_POWER_NOW,
	BATT_POWER_AVG,
	BATT_CHG_EN,
	BATT_SET_SHIP_MODE,
	BATT_PROP_MAX,
};

enum usb_property_id {
	USB_ONLINE,
	USB_VOLT_NOW,
	USB_VOLT_MAX,
	USB_CURR_NOW,
	USB_CURR_MAX,
	USB_INPUT_CURR_LIMIT,
	USB_TYPE,
	USB_ADAP_TYPE,
	USB_MOISTURE_DET_EN,
	USB_MOISTURE_DET_STS,
	USB_TEMP,
	USB_REAL_TYPE,
	USB_TYPEC_COMPLIANT,
	USB_CHG_EN,
	USB_CHG_VOL_MIN,
	USB_ADAP_SUBTYPE,
	USB_CHG_SET_PDO,
	USB_CHG_SET_QC,
	USB_VBUS_COLLAPSE_STATUS,
	USB_OTG_EN,
	USB_OTG_DCDC_EN,
	USB_DISCONNECT_PD,
	USB_OTG_AP_ENABLE,
	USB_OTG_SWITCH,
#ifdef OPLUS_CHG_OP_DEF
	USB_CHG_LCM_EN,
	USB_CHG_RERUN_APSD,
#endif
	USB_PROP_MAX,
};

#ifdef VENDOR_EDIT
typedef enum _FASTCHG_STATUS
{
	FAST_NOTIFY_UNKNOW,
	FAST_NOTIFY_PRESENT,
	FAST_NOTIFY_ONGOING,
	FAST_NOTIFY_ABSENT,
	FAST_NOTIFY_FULL,
	FAST_NOTIFY_BAD_CONNECTED,
	FAST_NOTIFY_BATT_TEMP_OVER,
	FAST_NOTIFY_BTB_TEMP_OVER,
	FAST_NOTIFY_DUMMY_START,
	FAST_NOTIFY_ADAPTER_COPYCAT,
}FASTCHG_STATUS;
#endif

enum wireless_property_id {
	WLS_ONLINE,
	WLS_VOLT_NOW,
	WLS_VOLT_MAX,
	WLS_CURR_NOW,
	WLS_CURR_MAX,
	WLS_TYPE,
	WLS_BOOST_EN,
	WLS_INPUT_CURR_LIMIT = 8,
	WLS_BOOST_VOLT = 10,
	WLS_BOOST_AICL_ENABLE,
	WLS_BOOST_AICL_RERUN,
	WLS_PROP_MAX,
};

struct battery_charger_set_notify_msg {
	struct pmic_glink_hdr	hdr;
	u32			battery_id;
	u32			power_state;
	u32			low_capacity;
	u32			high_capacity;
};

struct battery_charger_notify_msg {
	struct pmic_glink_hdr	hdr;
	u32			notification;
};

struct battery_charger_req_msg {
	struct pmic_glink_hdr	hdr;
	u32			battery_id;
	u32			property_id;
	u32			value;
};

struct battery_charger_resp_msg {
	struct pmic_glink_hdr	hdr;
	u32			property_id;
	u32			value;
	u32			ret_code;
};

struct battery_model_resp_msg {
	struct pmic_glink_hdr	hdr;
	u32			property_id;
	char			model[MAX_STR_LEN];
};

struct wireless_fw_check_req {
	struct pmic_glink_hdr	hdr;
	u32			fw_version;
	u32			fw_size;
};

struct wireless_fw_check_resp {
	struct pmic_glink_hdr	hdr;
	u32			ret_code;
};

struct wireless_fw_push_buf_req {
	struct pmic_glink_hdr	hdr;
	u8			buf[WLS_FW_BUF_SIZE];
	u32			fw_chunk_id;
};

struct wireless_fw_push_buf_resp {
	struct pmic_glink_hdr	hdr;
	u32			fw_update_status;
};

struct wireless_fw_update_status {
	struct pmic_glink_hdr	hdr;
	u32			fw_update_done;
};

struct wireless_fw_get_version_req {
	struct pmic_glink_hdr	hdr;
};

struct wireless_fw_get_version_resp {
	struct pmic_glink_hdr	hdr;
	u32			fw_version;
};

struct battery_charger_ship_mode_req_msg {
	struct pmic_glink_hdr	hdr;
	u32			ship_mode_type;
};

struct ocm_state {
	char			*name;
	char			*model;
	struct oplus_chg_mod *ocm;
	u32			*prop;
	u32			prop_count;
	u32			opcode_get;
	u32			opcode_set;
};

enum oplus_otg_type {
	OTG_EXTERNAL_BOOST,
	OTG_DEFAULT_DCDC,
};

struct oplus_chg_iio {
	struct iio_channel	*op_connector_temp_chan;
	struct iio_channel	*op_connector_temp_chan_sec;
};

struct battery_chg_dev {
	struct device			*dev;
	struct oplus_chg_ic_dev *ic_dev;
	struct class			battery_class;
	struct pmic_glink_client	*client;
	struct mutex			rw_lock;
	struct completion		ack;
	struct completion		fw_buf_ack;
	struct completion		fw_update_ack;
	struct ocm_state		ocm_list[OCM_TYPE_MAX];
	struct dentry			*debugfs_dir;
	struct oplus_chg_mod *batt_ocm;
	struct oplus_chg_mod *usb_ocm;
	u32				*thermal_levels;
	const char			*wls_fw_name;
	int				curr_thermal_level;
	int				num_thermal_levels;
	atomic_t			state;
	struct work_struct		subsys_up_work;
	struct work_struct		usb_type_work;
	int				fake_soc;
	bool				block_tx;
	bool				ship_mode_en;
	bool				debug_battery_detected;
	bool				wls_fw_update_reqd;
	enum oplus_otg_type		otg_type;
	u32				wls_fw_version;
	struct notifier_block		reboot_notifier;
	struct work_struct 		otg_enable_work;
	struct work_struct 		otg_disable_work;
	struct delayed_work otg_init_work;
	struct delayed_work otg_status_check_work;
	struct delayed_work connector_check_work;
	struct delayed_work connector_recovery_work;
	struct delayed_work charge_status_check_work;
	struct oplus_chg_iio iio;
	struct delayed_work		check_charger_type_work;
	struct delayed_work		usb_disconnect_work;
#ifdef OPLUS_CHG_OP_DEF
	struct iio_channel	*op_vph_vol_chan;
	int				vph_vol;
	struct regulator 		*vreg_ldo;
	int             		vreg_default_vol;
	int             		current_uA;
#endif
	int				connector_voltage;
	int				filter_count;
	int				connector_temp;
	int				hw_detect;
	int				pre_temp;
	int				count_run;
	int				count_total;
	int				current_temp;
	int				vbus_present;
	bool				connector_short;
	int				fastchg_on;
	int				vbus_ctrl;
	bool				otg_prohibited;
	bool				wls_boost_soft_start;
	int				wls_set_boost_vol;
	struct regmap			*pm8350_regmap;
};

/**********************************************************************
 **********************************************************************/

enum skip_reason {
	REASON_OTG_ENABLED	= BIT(0),
	REASON_FLASH_ENABLED	= BIT(1)
};

struct qcom_pmic {
	struct battery_chg_dev *bcdev_chip;

	/* for complie*/
	bool			otg_pulse_skip_dis;
	int			pulse_cnt;
	unsigned int	therm_lvl_sel;
	bool			psy_registered;
	int			usb_online;

	/* copy from msm8976_pmic begin */
	int			bat_charging_state;
	bool	 		suspending;
	bool			aicl_suspend;
	bool			usb_hc_mode;
	int    		usb_hc_count;
	bool			hc_mode_flag;
	/* copy form msm8976_pmic end */
};

extern struct oplus_warp_chip *g_warp_chip;

bool oplus_get_pon_chg(void);
void oplus_set_pon_chg(bool flag);
#endif /*__SM8350_CHARGER_H*/
