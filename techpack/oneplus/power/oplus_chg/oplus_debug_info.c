#include <linux/power_supply.h>
#include <linux/mutex.h>
#include <linux/power_supply.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/jiffies.h>
#include <linux/workqueue.h>
#include <linux/rtc.h>
#include <linux/ktime.h>
#include <linux/fs.h>

#ifdef CONFIG_OPLUS_KEVENT_UPLOAD
#include <linux/oplus_kevent.h>
#endif

#include "oplus_warp.h"
#include "oplus_charger.h"

#include "oplus_debug_info.h"

//#define OPLUS_CHG_DEBUG_TEST
#ifdef OPLUS_CHG_DEBUG_TEST
static int fake_soc = -1;
static int fake_ui_soc = -1;
static int aging_cap_test = -1;
#endif

#ifdef CONFIG_OPLUS_KEVENT_UPLOAD
#define OPLUS_CHG_DEBUG_LOG_TAG      "OplusCharger"
#define OPLUS_CHG_DEBUG_EVENT_ID     "charge_monitor"
#endif

struct oplus_chg_debug_info oplus_chg_debug_info;

struct warp_charger_type {
	int chg_type;
	int vol;
	int cur;
	int output_power_type;
};

struct warp_fw_type {
	int fw_type;
	int vol;
	int cur;
};

struct capacity_range {
	int low;
	int high;
};

struct temp_range {
	int low;
	int high;
};

struct input_current_setting {
	int cur;
	int vol;
	int time;
};

#define OPLUS_CHG_WARP_INPUT_CURRENT_MAX_CNT 10
struct warp_charge_strategy {
	struct capacity_range capacity_range;
	struct temp_range temp_range;
	struct input_current_setting input_current[OPLUS_CHG_WARP_INPUT_CURRENT_MAX_CNT];
};

enum {
	OPLUS_FASTCHG_OUTPUT_POWER_20W,
	OPLUS_FASTCHG_OUTPUT_POWER_30W,
	OPLUS_FASTCHG_OUTPUT_POWER_50W,
	OPLUS_FASTCHG_OUTPUT_POWER_65W,
	OPLUS_FASTCHG_OUTPUT_POWER_MAX,
};

#define OPLUS_CHG_START_STABLE_TIME (30) //单位s
#define OPLUS_CHG_DEBUG_FASTCHG_STOP_CNT 6

#define OPLUS_CHG_DEBUG_MAX_SOC  85
#define OPLUS_CHG_DEBUG_WARP_MAX_VOLT  4400

//TODO 部分项目ibus不对
#define LED_ON_HIGH_POWER_CONSUPTION   (4000 * 500) /*单位mW*/
#define LED_OFF_HIGH_POWER_CONSUPTION   (4000 * 300) /*单位mW*/
#define LED_OFF_STABLE_TIME (30) //单位s

#define INPUT_POWER_RATIO   60 //百分比

#define INPUT_CURRENT_DECREASE  (300000)    //单位微安
#define INPUT_POWER_DECREASE  (4000 * 500)    //单位w

#define OPLUS_CHARGERID_VOLT_LOW_THLD        650
#define OPLUS_CHARGERID_VOLT_HIGH_THLD        1250

#define HIGH_POWER_FASTCHG_BATT_VOLT    4224
#define HIGH_POWER_CHG_LOW_TEMP     160
#define HIGH_POWER_CHG_HIGH_TEMP    400

#define OPLUS_CHG_BATT_AGING_CAP_DECREASE 300

#define OPLUS_CHG_BATT_INVALID_CAPACITY  -0x1010101
#define OPLUS_CHG_BATT_S0C_CAPACITY_LOAD_JUMP_NUM      5
#define OPLUS_CHG_BATT_UI_S0C_CAPACITY_LOAD_JUMP_NUM   5
#define OPLUS_CHG_BATT_S0C_CAPACITY_JUMP_NUM           3
#define OPLUS_CHG_BATT_UI_S0C_CAPACITY_JUMP_NUM        5
#define OPLUS_CHG_BATT_UI_TO_S0C_CAPACITY_JUMP_NUM     3

#define OPLUS_CHG_MONITOR_FILE   "/data/oplus_charge/oplus_chg_debug_monitor.txt"
#if 1
#define OPLUS_CHG_BATT_AGING_CHECK_CNT   360
#define OPLUS_CHG_BATT_AGING_CHECK_TIME  (7 * 24 * 60 * 60)
#else
//for test
#define OPLUS_CHG_BATT_AGING_CHECK_CNT   1
#define OPLUS_CHG_BATT_AGING_CHECK_TIME  (1)
#endif


#define OPLUS_CHG_DEBUG_MSG_LEN 2048
char oplus_chg_debug_msg[OPLUS_CHG_DEBUG_MSG_LEN] = "";

enum oplus_chg_debug_info_notify_type {
	OPLUS_CHG_DEBUG_NOTIFY_TYPE_SOC_JUMP,
	OPLUS_CHG_DEBUG_NOTIFY_TYPE_CHG_FULL,
	OPLUS_CHG_DEBUG_NOTIFY_TYPE_BATT_FCC,
	OPLUS_CHG_DEBUG_NOTIFY_TYPE_CHG_SLOW,
	OPLUS_CHG_DEBUG_NOTIFY_TYPE_MAX,
};

enum oplus_chg_debug_info_notify_flag {
	/*电量跳变标志*/
	OPLUS_NOTIFY_BATT_SOC_CAPCITY_LOAD_JUMP,
	OPLUS_NOTIFY_BATT_UI_SOC_CAPCITY_LOAD_JUMP,
	OPLUS_NOTIFY_BATT_SOC_CAPCITY_JUMP,
	OPLUS_NOTIFY_BATT_UI_SOC_CAPCITY_JUMP,
	OPLUS_NOTIFY_BATT_UI_TO_SOC_CAPCITY_JUMP,
	OPLUS_NOTIFY_BATT_SOC_JUMP,                      //5
	/*充电慢标志*/
	OPLUS_NOTIFY_CHG_SLOW_BATT_NON_AUTH,
	OPLUS_NOTIFY_CHG_SLOW_NON_OPLUS_CHARGER,
	OPLUS_NOTIFY_CHG_SLOW_OVERTIME,
	OPLUS_NOTIFY_CHG_SLOW_ADAPTER_INPUT_LOW_POWER,       //线损大
	OPLUS_NOTIFY_CHG_SLOW_WARP_NON_START,            //10
	OPLUS_NOTIFY_CHG_SLOW_WARP_ADAPTER_NON_MAX_POWER,        //非匹配的最大功率快充充电器
	OPLUS_NOTIFY_CHG_SLOW_CHARGER_OV,
	OPLUS_NOTIFY_CHG_SLOW_CHARGER_UV,
	OPLUS_NOTIFY_CHG_SLOW_BATT_COLD_TEMP,
	OPLUS_NOTIFY_CHG_SLOW_BATT_WARM_TEMP,        //15
	OPLUS_NOTIFY_CHG_SLOW_FASTCHG_TO_WARM,
	OPLUS_NOTIFY_CHG_SLOW_CHG_TYPE_SDP,
	OPLUS_NOTIFY_CHG_SLOW_SYS_POWER_CONSUME_HIGH,
	OPLUS_NOTIFY_CHG_SLOW_CHG_POWER_LOW,
	OPLUS_NOTIFY_CHG_SLOW_LOW_CHARGE_CURRENT_LONG_TIME,  //20
	OPLUS_NOTIFY_CHG_SLOW_LED_ON_LONG_TIME,
	OPLUS_NOTIFY_CHG_SLOW_CAM_ON_LONG_TIME,
	OPLUS_NOTIFY_CHG_SLOW_CALLING_LONG_TIME,
	OPLUS_NOTIFY_CHG_SLOW_COOLDOWN_LONG_TIME,
	OPLUS_NOTIFY_CHG_SLOW_CHECK_TIME,            //25

	//充电报满
	OPLUS_NOTIFY_CHG_BATT_FULL_NON_100_CAP,
	OPLUS_NOTIFY_CHG_BATT_FULL_AND_100_CAP,
	OPLUS_NOTIFY_CHG_FULL,

	//电池老化
	OPLUS_NOTIFY_BATT_AGING_CAP,
	OPLUS_NOTIFY_BATT_AGING,

	OPLUS_NOTIFY_CHG_MAX_CNT,
};

enum {
	OPLUS_CHG_NOTIFY_UNKNOW,
	OPLUS_CHG_NOTIFY_ONCE,
	OPLUS_CHG_NOTIFY_REPEAT,
	OPLUS_CHG_NOTIFY_TOTAL,
};

struct oplus_chg_debug_info {
	int initialized;

	//电量跳变
	int pre_soc;
	int cur_soc;
	int pre_ui_soc;
	int cur_ui_soc;
	int soc_load_flag;
	unsigned long sleep_tm_sec;
	int soc_notified_flag;
#define SOC_LOAD_DELAY (60 * 1000)
	struct delayed_work soc_load_dwork;

	//充电慢
	int fast_chg_type;
	int pre_prop_status;
	int chg_start_soc;
	int chg_start_temp;
	int chg_start_time;
	int chg_total_time;
	int total_time;

	int pre_led_state;
	int led_off_start_time;

	int chg_cnt[OPLUS_NOTIFY_CHG_MAX_CNT];

	int notify_type;
	unsigned long long notify_flag;
	struct mutex nflag_lock; 

	struct power_supply *usb_psy;
	struct power_supply *batt_psy;

	int fastchg_stop_cnt;
	int cool_down_by_user;
	int chg_current_by_tbatt;
	int chg_current_by_cooldown;
	int fastchg_input_current;

	struct warp_charge_strategy *warp_charge_strategy;
	int warp_charge_input_current_index;
	int warp_charge_cur_state_chg_time;

	int warp_max_input_volt;
	int warp_max_input_current;

	//电池容量
	int fcc_design;

	//充电报满
	int chg_full_notified_flag;

	struct workqueue_struct *oplus_chg_debug_wq;
#ifdef CONFIG_OPLUS_KEVENT_UPLOAD
	struct kernel_packet_info *dcs_info;
	struct mutex dcs_info_lock;
#define SEND_INFO_DELAY 3000
	struct delayed_work send_info_dwork;
#define SEND_INFO_MAX_CNT 5
	int retry_cnt;
#endif
};

struct oplus_chg_debug_notify_policy {
	int policy;
	int fast_chg_time;   //单位5s
	int normal_chg_time;   //单位5s
	int percent; //百分比
};

#if 0
//TODO 需要移植到dtsi中
struct warp_charge_strategy warp_charge_strategy_30w[] = {
	{
		.capacity_range = {0, 75},
		.temp_range = {120, 160},
		.input_current = {
			{4000, 4450, 0},
			{3000, 4470, 0},
			{2000, 4480, 0}
		}
	},
	{
		.capacity_range = {0, 75},
		.temp_range = {160, 410},
		.input_current = {
			{6000, 4200, 900},
			{5000, 4200, 0},
			{4500, 4450, 0},
			{4000, 4450, 0},
			{3000, 4470, 0},
			{2000, 4480, 0}
		}
	},
	{
		.capacity_range = {0, 75},
		.temp_range = {410, 420},
		.input_current = {
			{4000, 4470, 0},
			{2000, 4480, 0}
		}
	},
	{
		.capacity_range = {0, 75},
		.temp_range = {420, 425},
		.input_current = {
			{4000, 4480, 0}
		}
	},
	{
		.capacity_range = {0, 75},
		.temp_range = {425, 435},
		.input_current = {
			{3000, 4480, 0}
		}
	},
	{
		.capacity_range = {-1, -1},
	}
};

struct warp_charge_strategy warp_charge_strategy_65w[] = {
	{
		.capacity_range = {0, 50},
		.temp_range = {120, 160},
		.input_current = {
			{6000, 4200, 0},
			{4000, 4454, 0},
			{2000, 4494, 0},
			{1500, 4500, 0}
		}
	},
	{
		.capacity_range = {0, 50},
		.temp_range = {160, 370},
		.input_current = {
			{6500, 4200, 0},
			{6000, 4250, 300},
			{5500, 4300, 400},
			{4500, 4454, 780},
			{3500, 4454, 0},
			{2000, 4494, 0},
			{1500, 4500, 0}
		}
	},
	{
		.capacity_range = {0, 50},
		.temp_range = {370, 430},
		.input_current = {
			{6500, 4200, 0},
			{6000, 4250, 300},
			{5500, 4300, 400},
			{4500, 4454, 780},
			{3500, 4454, 0},
			{2000, 4494, 0},
			{1600, 4500, 0}
		}
	},
	{
		.capacity_range = {-1, -1},
	}
};

struct warp_charge_strategy *g_warp_charge_strategy_p[] = {
	NULL,
	warp_charge_strategy_30w, 
	NULL,
	warp_charge_strategy_65w, 
};
#else
struct warp_charge_strategy *g_warp_charge_strategy_p[OPLUS_FASTCHG_OUTPUT_POWER_MAX] = {
	NULL,
	NULL,
	NULL,
	NULL
};
#endif

static struct oplus_chg_debug_notify_policy oplus_chg_debug_notify_policy[] = {
	[OPLUS_NOTIFY_BATT_SOC_CAPCITY_LOAD_JUMP]             = {OPLUS_CHG_NOTIFY_ONCE,          1,    1,      -1},
	[OPLUS_NOTIFY_BATT_UI_SOC_CAPCITY_LOAD_JUMP]          = {OPLUS_CHG_NOTIFY_ONCE,          1,    1,      -1},
	[OPLUS_NOTIFY_BATT_SOC_CAPCITY_JUMP]                  = {OPLUS_CHG_NOTIFY_REPEAT,        1,    1,      -1},
	[OPLUS_NOTIFY_BATT_UI_SOC_CAPCITY_JUMP]               = {OPLUS_CHG_NOTIFY_REPEAT,        1,    1,      -1},
	[OPLUS_NOTIFY_BATT_UI_TO_SOC_CAPCITY_JUMP]            = {OPLUS_CHG_NOTIFY_REPEAT,        1,    1,      -1},

	[OPLUS_NOTIFY_CHG_SLOW_BATT_NON_AUTH]                = {OPLUS_CHG_NOTIFY_ONCE,          6,    6,      -1},
	[OPLUS_NOTIFY_CHG_SLOW_NON_OPLUS_CHARGER]             = {OPLUS_CHG_NOTIFY_ONCE,          6,    6,      -1},
	[OPLUS_NOTIFY_CHG_SLOW_OVERTIME]                     = {OPLUS_CHG_NOTIFY_ONCE,          1,    1,      -1},
	[OPLUS_NOTIFY_CHG_SLOW_ADAPTER_INPUT_LOW_POWER]      = {OPLUS_CHG_NOTIFY_TOTAL,         720,  1440,   50},
	[OPLUS_NOTIFY_CHG_SLOW_WARP_NON_START]               = {OPLUS_CHG_NOTIFY_TOTAL,         720,  1440,   30},
	[OPLUS_NOTIFY_CHG_SLOW_WARP_ADAPTER_NON_MAX_POWER]   = {OPLUS_CHG_NOTIFY_TOTAL,         720,  1440,   50},
	[OPLUS_NOTIFY_CHG_SLOW_CHARGER_OV]                   = {OPLUS_CHG_NOTIFY_TOTAL,         720,  1440,   30},
	[OPLUS_NOTIFY_CHG_SLOW_CHARGER_UV]                   = {OPLUS_CHG_NOTIFY_TOTAL,         720,  1440,   30},
	[OPLUS_NOTIFY_CHG_SLOW_BATT_COLD_TEMP]               = {OPLUS_CHG_NOTIFY_TOTAL,         720,  1440,   30},
	[OPLUS_NOTIFY_CHG_SLOW_BATT_WARM_TEMP]               = {OPLUS_CHG_NOTIFY_TOTAL,         720,  1440,   30},
	[OPLUS_NOTIFY_CHG_SLOW_FASTCHG_TO_WARM]              = {OPLUS_CHG_NOTIFY_REPEAT,          1,     1,   -1},
	[OPLUS_NOTIFY_CHG_SLOW_CHG_TYPE_SDP]                 = {OPLUS_CHG_NOTIFY_TOTAL,         720,  1440,   80},
	[OPLUS_NOTIFY_CHG_SLOW_SYS_POWER_CONSUME_HIGH]       = {OPLUS_CHG_NOTIFY_TOTAL,         720,  1440,   40},
	[OPLUS_NOTIFY_CHG_SLOW_CHG_POWER_LOW]                = {OPLUS_CHG_NOTIFY_TOTAL,         720,  1440,   40},
	[OPLUS_NOTIFY_CHG_SLOW_LOW_CHARGE_CURRENT_LONG_TIME] = {OPLUS_CHG_NOTIFY_TOTAL,         720,  1440,   70},
	[OPLUS_NOTIFY_CHG_SLOW_LED_ON_LONG_TIME]             = {OPLUS_CHG_NOTIFY_TOTAL,         720,  1440,   70},
	[OPLUS_NOTIFY_CHG_SLOW_CAM_ON_LONG_TIME]             = {OPLUS_CHG_NOTIFY_TOTAL,         720,  1440,   50},
	[OPLUS_NOTIFY_CHG_SLOW_CALLING_LONG_TIME]            = {OPLUS_CHG_NOTIFY_TOTAL,         720,  1440,   70},
	[OPLUS_NOTIFY_CHG_SLOW_COOLDOWN_LONG_TIME]           = {OPLUS_CHG_NOTIFY_TOTAL,         720,  1440,   70},
	[OPLUS_NOTIFY_CHG_SLOW_CHECK_TIME]                   = {OPLUS_CHG_NOTIFY_REPEAT,        720,  1440,   -1},

	[OPLUS_NOTIFY_CHG_BATT_FULL_NON_100_CAP]             = {OPLUS_CHG_NOTIFY_ONCE,          3,    3,      -1},
	[OPLUS_NOTIFY_CHG_BATT_FULL_AND_100_CAP]             = {OPLUS_CHG_NOTIFY_ONCE,          3,    3,      -1},

	[OPLUS_NOTIFY_BATT_AGING_CAP]                    = {OPLUS_CHG_NOTIFY_REPEAT,        1,    1,      -1},
};

struct warp_charger_type warp_charger_type_list[] = {
	{0x01, 5000,  4000, OPLUS_FASTCHG_OUTPUT_POWER_20W},
	{0x11, 10000, 5000, OPLUS_FASTCHG_OUTPUT_POWER_50W},
	{0x12, 10000, 5000, OPLUS_FASTCHG_OUTPUT_POWER_50W},
	{0x13, 5000,  4000, OPLUS_FASTCHG_OUTPUT_POWER_20W},
	{0x14, 10000, 6500, OPLUS_FASTCHG_OUTPUT_POWER_65W},
	{0x19, 5000,  6000, OPLUS_FASTCHG_OUTPUT_POWER_30W},
	{0x21, 10000, 5000, OPLUS_FASTCHG_OUTPUT_POWER_50W},
	{0x29, 5000,  6000, OPLUS_FASTCHG_OUTPUT_POWER_30W},
	{0x31, 10000, 5000, OPLUS_FASTCHG_OUTPUT_POWER_50W},
	{0x33, 10000, 5000, OPLUS_FASTCHG_OUTPUT_POWER_50W},
	{0x34, 5000,  4000, OPLUS_FASTCHG_OUTPUT_POWER_20W},
};

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
ssize_t __attribute__((weak)) vfs_read(struct file *file, char __user *buf, size_t count, loff_t *pos)
{
	return -EINVAL;
}

ssize_t __attribute__((weak)) vfs_write(struct file *file, const char __user *buf, size_t count, loff_t *pos)
{
	return -EINVAL;
}

int __attribute__((weak)) filp_close(struct file *filp, fl_owner_t id)
{
	return -EINVAL;
}

struct file __attribute__((weak)) *filp_open(const char *filename, int flags, umode_t mode)
{
	return ERR_PTR(-EINVAL);
}
#endif

static int oplus_chg_debug_info_reset(void);
static int oplus_chg_chg_is_normal_path(struct oplus_chg_chip *chip);
//static unsigned long long oplus_chg_debug_get_notify_flag(bool reset);
static int oplus_chg_debug_notify_flag_is_set(int flag);
//static int oplus_chg_debug_mask_notify_flag(int low, int high);
static int oplus_chg_debug_reset_notify_flag(void);
static int oplus_chg_reset_chg_notify_type(void);
static int oplus_chg_chg_batt_capacity_jump_check(struct oplus_chg_chip *chip);

#ifdef CONFIG_OPLUS_KEVENT_UPLOAD
static int oplus_chg_pack_debug_info(struct oplus_chg_chip *chip)
{
	char log_tag[] = OPLUS_CHG_DEBUG_LOG_TAG;
	char event_id[] = OPLUS_CHG_DEBUG_EVENT_ID;
	int len;

	len = strlen(&oplus_chg_debug_msg[sizeof(struct kernel_packet_info)]);

	if (len) {
		mutex_lock(&oplus_chg_debug_info.dcs_info_lock);
		memset(oplus_chg_debug_info.dcs_info, 0x0, sizeof(struct kernel_packet_info));

		oplus_chg_debug_info.dcs_info->type = 1;
		memcpy(oplus_chg_debug_info.dcs_info->log_tag, log_tag, strlen(log_tag));
		memcpy(oplus_chg_debug_info.dcs_info->event_id, event_id, strlen(event_id));
		oplus_chg_debug_info.dcs_info->payload_length = len + 1;

#ifdef OPLUS_CHG_DEBUG_TEST
		chg_err("%s\n", oplus_chg_debug_info.dcs_info->payload);
#endif

		mutex_unlock(&oplus_chg_debug_info.dcs_info_lock);

		return 0;
	}

	return -1;
}

static int oplus_chg_debug_mask_notify_flag(int low, int high)
{
	unsigned long long mask = -1;
	int bits = sizeof(mask) * 8 - 1;

	mask = (mask >> low) << low;
	mask = (mask << (bits - high)) >> (bits - high);
	mutex_lock(&oplus_chg_debug_info.nflag_lock);
	oplus_chg_debug_info.notify_flag &= ~mask;
	mutex_unlock(&oplus_chg_debug_info.nflag_lock);

	return 0;
}

static void oplus_chg_send_info_dwork(struct work_struct *work)
{
	int ret = 0;

	mutex_lock(&oplus_chg_debug_info.dcs_info_lock);
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0)) \
	|| IS_BUILTIN(CONFIG_OPLUS_CHG)//QGKI
	ret = kevent_send_to_user(oplus_chg_debug_info.dcs_info);
#endif
	mutex_unlock(&oplus_chg_debug_info.dcs_info_lock);
	if ((ret > 0) && (oplus_chg_debug_info.retry_cnt > 0)) {
		queue_delayed_work(oplus_chg_debug_info.oplus_chg_debug_wq,
				&oplus_chg_debug_info.send_info_dwork, msecs_to_jiffies(SEND_INFO_DELAY));
	}
	else {
		//soc jump
		oplus_chg_debug_mask_notify_flag(0, OPLUS_NOTIFY_BATT_SOC_JUMP);

		//slow check
		oplus_chg_debug_mask_notify_flag(OPLUS_NOTIFY_CHG_SLOW_CHECK_TIME, OPLUS_NOTIFY_CHG_SLOW_CHECK_TIME);

		//batt full
		oplus_chg_debug_mask_notify_flag(OPLUS_NOTIFY_CHG_SLOW_CHECK_TIME + 1, OPLUS_NOTIFY_CHG_FULL);

		//batt aging
		oplus_chg_debug_mask_notify_flag(OPLUS_NOTIFY_CHG_FULL + 1, OPLUS_NOTIFY_BATT_AGING);

		oplus_chg_reset_chg_notify_type();
	}

	chg_err("retry_cnt: %d\n", oplus_chg_debug_info.retry_cnt);

	oplus_chg_debug_info.retry_cnt--;
}
#endif

static int oplus_chg_read_filedata(struct timespec *ts)
{
	struct file *filp = NULL;
	mm_segment_t old_fs;
	ssize_t size;
	loff_t offsize = 0;
	char buf[32] = {0};

	filp = filp_open(OPLUS_CHG_MONITOR_FILE, O_RDONLY, 0644);
	if (IS_ERR(filp)) {
		chg_err("open file %s failed[%d].\n", OPLUS_CHG_MONITOR_FILE, PTR_ERR(filp));
		return -1;
	}

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	memset(buf, 0x0, sizeof(buf));
	size = vfs_read(filp, buf, sizeof(buf), &offsize);
	set_fs(old_fs);
	filp_close(filp, NULL);

	if (size <= 0) {
		chg_err("read file size is zero.\n");
		return -1;
	}

	sscanf(buf, "%ld", &ts->tv_sec);

	return 0;
}

static int oplus_chg_write_filedata(struct timespec *ts)
{
	struct file *filp = NULL;
	mm_segment_t old_fs;
	ssize_t size;
	loff_t offsize = 0;
	char buf[32] = {0};

	filp = filp_open(OPLUS_CHG_MONITOR_FILE, O_RDWR | O_CREAT, 0644);
	if (IS_ERR(filp)) {
		chg_err("open file %s failed[%d].\n", OPLUS_CHG_MONITOR_FILE, PTR_ERR(filp));
		return -1;
	}

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	snprintf(buf, sizeof(buf), "%lu", ts->tv_sec);
	size = vfs_write(filp, buf, sizeof(buf), &offsize);
	set_fs(old_fs);
	filp_close(filp, NULL);

	if (size <= 0) {
		chg_err("read file size is zero.\n");
		return -1;
	}

	return 0;
}

#if 0
static void oplus_chg_print_debug_info(struct oplus_chg_chip *chip)
{
	int ret = 0;
	int i;

	if (oplus_chg_debug_info.notify_type)
	{
#ifdef CONFIG_OPLUS_KEVENT_UPLOAD
		if (delayed_work_pending(&oplus_chg_debug_info.send_info_dwork))
			cancel_delayed_work_sync(&oplus_chg_debug_info.send_info_dwork);
		mutex_lock(&oplus_chg_debug_info.dcs_info_lock);
#endif
		memset(oplus_chg_debug_msg, 0x0, sizeof(oplus_chg_debug_msg));

#ifdef CONFIG_OPLUS_KEVENT_UPLOAD
		ret += sizeof(struct kernel_packet_info);
#endif

		ret += snprintf(&oplus_chg_debug_msg[ret], OPLUS_CHG_DEBUG_MSG_LEN - ret,
				"Notify Type: 0x%-4x, ", oplus_chg_debug_info.notify_type);
		ret += snprintf(&oplus_chg_debug_msg[ret], OPLUS_CHG_DEBUG_MSG_LEN - ret,
				"Notify Flag: 0x%-4x, CHG_CNT[ ", oplus_chg_debug_info.notify_flag);
		for(i = 0; i < OPLUS_NOTIFY_CHG_MAX_CNT; i++)
		{
			ret += snprintf(&oplus_chg_debug_msg[ret], OPLUS_CHG_DEBUG_MSG_LEN - ret,
					"%4d ", oplus_chg_debug_info.chg_cnt[i]);
		}
		ret += snprintf(&oplus_chg_debug_msg[ret], OPLUS_CHG_DEBUG_MSG_LEN - ret, "], ");

		ret += snprintf(&oplus_chg_debug_msg[ret], OPLUS_CHG_DEBUG_MSG_LEN - ret,
				"DBG INFO[%3d %3d %3d %3d %3d %4d %4d], ",
				oplus_chg_debug_info.pre_soc,
				oplus_chg_debug_info.cur_soc,
				oplus_chg_debug_info.pre_ui_soc,
				oplus_chg_debug_info.cur_ui_soc,
				oplus_chg_debug_info.chg_start_soc,
				oplus_chg_debug_info.chg_start_time,
				oplus_chg_debug_info.chg_total_time);

		ret += snprintf(&oplus_chg_debug_msg[ret], OPLUS_CHG_DEBUG_MSG_LEN - ret,
				"CHGR[ %d %d %d %d %d ], "
				"BAT[ %d %d %d %d %d %4d ], "
				"GAUGE[ %3d %d %d %4d %7d %3d %3d %3d %3d %4d], "
				"STATUS[ %d %4d %d %d %d 0x%-4x ], "
				"OTHER[ %d %d %d %d %d %d ]",
				chip->charger_exist, chip->charger_type, chip->charger_volt, chip->prop_status, chip->boot_mode,
				chip->batt_exist, chip->batt_full, chip->chging_on, chip->in_rechging, chip->charging_state, oplus_chg_debug_info.total_time,
				chip->temperature, chip->batt_volt, chip->batt_volt_min, chip->icharging, chip->ibus, chip->soc, chip->ui_soc, chip->soc_load, chip->batt_rm, chip->batt_fcc,
				chip->vbatt_over, chip->chging_over_time, chip->vchg_status, chip->tbatt_status, chip->stop_voter, chip->notify_code,
				chip->otg_switch, chip->mmi_chg, chip->boot_reason, chip->boot_mode, chip->chargerid_volt, chip->chargerid_volt_got);

#ifdef CONFIG_OPLUS_KEVENT_UPLOAD
		mutex_unlock(&oplus_chg_debug_info.dcs_info_lock);
		ret = oplus_chg_pack_debug_info(chip);
		if (!ret)
		{
			oplus_chg_debug_info.retry_cnt = SEND_INFO_MAX_CNT;
			queue_delayed_work(oplus_chg_debug_info.oplus_chg_debug_wq, &oplus_chg_debug_info.send_info_dwork, 0);
		}
#else
		chg_err("%s\n", oplus_chg_debug_msg);
#endif

	}
}
#else
static void oplus_chg_print_debug_info(struct oplus_chg_chip *chip)
{
	int ret = 0;
	struct oplus_warp_chip *warp_chip = NULL;
	struct timespec ts;
	struct rtc_time tm;

#ifndef OPLUS_CHG_DEBUG_TEST
	if (oplus_chg_debug_info.notify_type)
#endif
	{
#ifdef CONFIG_OPLUS_KEVENT_UPLOAD
		if (delayed_work_pending(&oplus_chg_debug_info.send_info_dwork))
			cancel_delayed_work_sync(&oplus_chg_debug_info.send_info_dwork);
		mutex_lock(&oplus_chg_debug_info.dcs_info_lock);
#endif
		memset(oplus_chg_debug_msg, 0x0, sizeof(oplus_chg_debug_msg));

		getnstimeofday(&ts);
		rtc_time_to_tm(ts.tv_sec, &tm);

#ifdef CONFIG_OPLUS_KEVENT_UPLOAD
		ret += sizeof(struct kernel_packet_info);

		ret += snprintf(&oplus_chg_debug_msg[ret], OPLUS_CHG_DEBUG_MSG_LEN - ret,
				OPLUS_CHG_DEBUG_EVENT_ID"$$");
#endif

		ret += snprintf(&oplus_chg_debug_msg[ret], OPLUS_CHG_DEBUG_MSG_LEN - ret,
				"type@@0x%x", oplus_chg_debug_info.notify_type);

		ret += snprintf(&oplus_chg_debug_msg[ret], OPLUS_CHG_DEBUG_MSG_LEN - ret,
				"$$flag@@0x%x", oplus_chg_debug_info.notify_flag);

		ret += snprintf(&oplus_chg_debug_msg[ret], OPLUS_CHG_DEBUG_MSG_LEN - ret,
				"$$pre_soc@@%d", oplus_chg_debug_info.pre_soc);
		ret += snprintf(&oplus_chg_debug_msg[ret], OPLUS_CHG_DEBUG_MSG_LEN - ret,
				"$$cur_soc@@%d", oplus_chg_debug_info.cur_soc);
		ret += snprintf(&oplus_chg_debug_msg[ret], OPLUS_CHG_DEBUG_MSG_LEN - ret,
				"$$pre_ui_soc@@%d", oplus_chg_debug_info.pre_ui_soc);
		ret += snprintf(&oplus_chg_debug_msg[ret], OPLUS_CHG_DEBUG_MSG_LEN - ret,
				"$$cur_ui_soc@@%d", oplus_chg_debug_info.cur_ui_soc);
		ret += snprintf(&oplus_chg_debug_msg[ret], OPLUS_CHG_DEBUG_MSG_LEN - ret,
				"$$sleep_time@@%lu", oplus_chg_debug_info.sleep_tm_sec);
		ret += snprintf(&oplus_chg_debug_msg[ret], OPLUS_CHG_DEBUG_MSG_LEN - ret,
				"$$soc_load@@%d", chip->soc_load);
		ret += snprintf(&oplus_chg_debug_msg[ret], OPLUS_CHG_DEBUG_MSG_LEN - ret,
				"$$batt_fcc@@%d", chip->batt_fcc);
		ret += snprintf(&oplus_chg_debug_msg[ret], OPLUS_CHG_DEBUG_MSG_LEN - ret,
				"$$charge_status@@%d", chip->prop_status);
		ret += snprintf(&oplus_chg_debug_msg[ret], OPLUS_CHG_DEBUG_MSG_LEN - ret,
				"$$boot_mode@@%d", chip->boot_mode);
		ret += snprintf(&oplus_chg_debug_msg[ret], OPLUS_CHG_DEBUG_MSG_LEN - ret,
				"$$chg_total_time@@%d", oplus_chg_debug_info.chg_total_time);
		ret += snprintf(&oplus_chg_debug_msg[ret], OPLUS_CHG_DEBUG_MSG_LEN - ret,
				"$$chg_start_temp@@%d", oplus_chg_debug_info.chg_start_temp);
		ret += snprintf(&oplus_chg_debug_msg[ret], OPLUS_CHG_DEBUG_MSG_LEN - ret,
				"$$chg_cur_temp@@%d", chip->temperature);
		ret += snprintf(&oplus_chg_debug_msg[ret], OPLUS_CHG_DEBUG_MSG_LEN - ret,
				"$$chg_start_soc@@%d", oplus_chg_debug_info.chg_start_soc);
		ret += snprintf(&oplus_chg_debug_msg[ret], OPLUS_CHG_DEBUG_MSG_LEN - ret,
				"$$adapter_type@@0x%x", (oplus_chg_debug_info.fast_chg_type << 8) | chip->real_charger_type);
		ret += snprintf(&oplus_chg_debug_msg[ret], OPLUS_CHG_DEBUG_MSG_LEN - ret,
				"$$icharging@@%d", chip->icharging);
		if (oplus_chg_chg_is_normal_path(chip))
		{
			ret += snprintf(&oplus_chg_debug_msg[ret], OPLUS_CHG_DEBUG_MSG_LEN - ret,
					"$$ibus@@%d", chip->ibus);
		}
		else
		{
			ret += snprintf(&oplus_chg_debug_msg[ret], OPLUS_CHG_DEBUG_MSG_LEN - ret,
					"$$ibus@@%d", oplus_chg_debug_info.fastchg_input_current);
		}
		ret += snprintf(&oplus_chg_debug_msg[ret], OPLUS_CHG_DEBUG_MSG_LEN - ret,
				"$$vbat@@%d", chip->batt_volt);
		ret += snprintf(&oplus_chg_debug_msg[ret], OPLUS_CHG_DEBUG_MSG_LEN - ret,
				"$$cool_down@@%d", chip->cool_down);

		ret += snprintf(&oplus_chg_debug_msg[ret], OPLUS_CHG_DEBUG_MSG_LEN - ret,
				"$$other@@time[%d-%d-%d %d:%d:%d], "
				"CHGR[ %d %d %d %d %d %d %d], "
				"BAT[ %d %d %d %d %d %4d ], "
				"GAUGE[ %3d %d %d %4d %7d %3d %3d %3d %3d %4d], "
				"STATUS[ %d %4d %d %d %d 0x%-4x %d %d %d], "
				"OTHER[ %d %d %d %d %d %d ], ",
				tm.tm_year + 1900, tm.tm_mon, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec,
				chip->charger_exist, chip->charger_type, chip->charger_volt, chip->prop_status, chip->boot_mode,oplus_chg_debug_info.chg_current_by_tbatt, oplus_chg_debug_info.chg_current_by_cooldown,
				chip->batt_exist, chip->batt_full, chip->chging_on, chip->in_rechging, chip->charging_state, oplus_chg_debug_info.total_time,
				chip->temperature, chip->batt_volt, chip->batt_volt_min, chip->icharging, chip->ibus, chip->soc, chip->ui_soc, chip->soc_load, chip->batt_rm, chip->batt_fcc,
				chip->vbatt_over, chip->chging_over_time, chip->vchg_status, chip->tbatt_status, chip->stop_voter, chip->notify_code, chip->sw_full, chip->hw_full_by_sw, chip->hw_full,
				chip->otg_switch, chip->mmi_chg, chip->boot_reason, chip->boot_mode, chip->chargerid_volt, chip->chargerid_volt_got);

		oplus_warp_get_warp_chip_handle(&warp_chip);
		if (warp_chip) {
			ret += snprintf(&oplus_chg_debug_msg[ret], OPLUS_CHG_DEBUG_MSG_LEN - ret,
					"WARP[ %d / %d / %d / %d / %d / %d]",
					warp_chip->fastchg_allow, warp_chip->fastchg_started, warp_chip->fastchg_dummy_started,
					warp_chip->fastchg_to_normal, warp_chip->fastchg_to_warm, warp_chip->btb_temp_over);
		}

#ifdef OPLUS_CHG_DEBUG_TEST
		{
			int i;
			ret += snprintf(&oplus_chg_debug_msg[ret], OPLUS_CHG_DEBUG_MSG_LEN - ret,
					", CHG_CNT[");
			for(i = 0; i < OPLUS_NOTIFY_CHG_MAX_CNT; i++)
			{
				ret += snprintf(&oplus_chg_debug_msg[ret], OPLUS_CHG_DEBUG_MSG_LEN - ret,
						"%4d ", oplus_chg_debug_info.chg_cnt[i]);
			}
			ret += snprintf(&oplus_chg_debug_msg[ret], OPLUS_CHG_DEBUG_MSG_LEN - ret,
					"]");
		}
#endif

#ifdef CONFIG_OPLUS_KEVENT_UPLOAD
		mutex_unlock(&oplus_chg_debug_info.dcs_info_lock);

		chg_err("%s\n", &oplus_chg_debug_msg[sizeof(struct kernel_packet_info)]);

		if (oplus_chg_debug_info.notify_type) {
			ret = oplus_chg_pack_debug_info(chip);
			if (!ret) {
				oplus_chg_debug_info.retry_cnt = SEND_INFO_MAX_CNT;
				queue_delayed_work(oplus_chg_debug_info.oplus_chg_debug_wq, &oplus_chg_debug_info.send_info_dwork, 0);
			}
		}
#else
#ifdef OPLUS_CHG_DEBUG_TEST
		chg_err("%s\n", oplus_chg_debug_msg);
#endif
#endif
	}
}
#endif

#if 0
static int oplus_chg_get_warp_max_input_power()
{
	int i;
	int max_input_power = 0;
	int arr_size = sizeof(warp_charger_type_list) / sizeof(warp_charger_type_list[0]);
	int vol,cur;
	struct oplus_warp_chip *warp_chip = NULL;

	oplus_warp_get_warp_chip_handle(&warp_chip);

	if (warp_chip == NULL)
		max_input_power = 0;

	for(i = 0; i < arr_size; i++)
	{
		if (oplus_warp_get_fast_chg_type() == warp_charger_type_list[i].chg_type)
		{
			vol = oplus_chg_debug_info.warp_max_input_volt < warp_charger_type_list[i].vol ?
				oplus_chg_debug_info.warp_max_input_volt : warp_charger_type_list[i].vol;
			cur = oplus_chg_debug_info.warp_max_input_current < warp_charger_type_list[i].cur ?
				oplus_chg_debug_info.warp_max_input_current : warp_charger_type_list[i].cur;
			max_input_power = vol * cur * 8 / 10;
			break;
		}
	}

	return max_input_power;
}
#endif

static int oplus_chg_get_input_power(struct oplus_chg_chip *chip)
{
	int input_power;

	input_power = chip->ibus / 1000 * chip->charger_volt * 8 / 10;
#ifdef OPLUS_CHG_DEBUG_TEST
	chg_err("input_power: %d\n", input_power);
#endif

	return input_power;
}

static int oplus_chg_get_warp_adapter_type_index(struct oplus_chg_chip *chip)
{
	int i;
	int arr_size = sizeof(warp_charger_type_list) / sizeof(warp_charger_type_list[0]);

	for(i = 0; i < arr_size; i++) {
		if (oplus_warp_get_fast_chg_type() == warp_charger_type_list[i].chg_type) {
			break;
		}
	}

	if (i == arr_size)
		return 0;

	return i;
}

static int oplus_chg_get_warp_adapter_is_low_input_power(struct oplus_chg_chip *chip)
{
	int adapter_input_power = 0;
	int warp_adapter_type_index = -1;

	warp_adapter_type_index = oplus_chg_get_warp_adapter_type_index(chip);

	adapter_input_power = warp_charger_type_list[warp_adapter_type_index].vol * warp_charger_type_list[warp_adapter_type_index].cur;

	if (adapter_input_power < (oplus_chg_debug_info.warp_max_input_volt * oplus_chg_debug_info.warp_max_input_current))
		return 1;

	return 0;
}

static int oplus_chg_get_warp_chg_max_input_power(struct oplus_chg_chip *chip)
{
	int warp_adapter_type_index = -1;
	int adapter_input_power = 0;

	warp_adapter_type_index = oplus_chg_get_warp_adapter_type_index(chip);

	adapter_input_power = warp_charger_type_list[warp_adapter_type_index].vol * warp_charger_type_list[warp_adapter_type_index].cur;


	if (adapter_input_power < (oplus_chg_debug_info.warp_max_input_volt * oplus_chg_debug_info.warp_max_input_current))
		return adapter_input_power * 8 / 10;

	return (oplus_chg_debug_info.warp_max_input_volt * oplus_chg_debug_info.warp_max_input_current * 8 / 10);
}

static int oplus_chg_get_warp_input_power(struct oplus_chg_chip *chip)
{
	int i;
	int input_power = 0;
	int vol,cur;
	struct oplus_warp_chip *warp_chip = NULL;
	int mcu_chg_current = 0;

	oplus_warp_get_warp_chip_handle(&warp_chip);
	if (warp_chip == NULL)
		return 0;

	i = oplus_chg_get_warp_adapter_type_index(chip);

	vol = oplus_chg_debug_info.warp_max_input_volt < warp_charger_type_list[i].vol ?
		oplus_chg_debug_info.warp_max_input_volt : warp_charger_type_list[i].vol;
	cur = oplus_chg_debug_info.warp_max_input_current < warp_charger_type_list[i].cur ?
		oplus_chg_debug_info.warp_max_input_current : warp_charger_type_list[i].cur;

	if (warp_chip->warp_chg_current_now > 0)
		cur = cur < warp_chip->warp_chg_current_now ? cur : warp_chip->warp_chg_current_now;

	if (oplus_chg_debug_info.warp_charge_strategy) {
		chg_err("warp strategy input_current_index: %d\n", oplus_chg_debug_info.warp_charge_input_current_index);
		mcu_chg_current = oplus_chg_debug_info.warp_charge_strategy->input_current[oplus_chg_debug_info.warp_charge_input_current_index].cur;
		cur = cur < mcu_chg_current ? cur : mcu_chg_current;
	}

	oplus_chg_debug_info.fastchg_input_current = cur;

	input_power = vol * cur * 8 / 10;
#ifdef OPLUS_CHG_DEBUG_TEST
	chg_err("input_power: %d\n", input_power);
#endif


	return input_power;
}

static int oplus_chg_get_charge_power(struct oplus_chg_chip *chip)
{
	int charge_power;

	charge_power = chip->icharging * chip->batt_volt * chip->vbatt_num;
#ifdef OPLUS_CHG_DEBUG_TEST
	chg_err("charge_pwoer: %d\n", charge_power);
#endif

	return charge_power;
}

static int oplus_chg_reset_chg_cnt(int index)
{
	if ((index < 0) && (index >= OPLUS_NOTIFY_CHG_MAX_CNT))
		return -1;

	mutex_lock(&oplus_chg_debug_info.nflag_lock);
	oplus_chg_debug_info.chg_cnt[index] = 0;
	mutex_unlock(&oplus_chg_debug_info.nflag_lock);

	return 0;
}

static int oplus_chg_set_notify_type(int index)
{
	if ((index < 0) || (index >= OPLUS_CHG_DEBUG_NOTIFY_TYPE_MAX))
		return -1;

	mutex_lock(&oplus_chg_debug_info.nflag_lock);
	oplus_chg_debug_info.notify_type |= (1 << index);
	mutex_unlock(&oplus_chg_debug_info.nflag_lock);

	return 0;
}

static int oplus_chg_debug_notify_type_is_set(int type)
{
	int ret = 0;

	mutex_lock(&oplus_chg_debug_info.nflag_lock);
	if (oplus_chg_debug_info.notify_type & (1 << type)) {
		ret = 1;
	}
	mutex_unlock(&oplus_chg_debug_info.nflag_lock);

	return ret;
}

static int oplus_chg_reset_chg_notify_type(void)
{
	mutex_lock(&oplus_chg_debug_info.nflag_lock);
	oplus_chg_debug_info.notify_type = 0;
	mutex_unlock(&oplus_chg_debug_info.nflag_lock);

	return 0;
}

static int oplus_chg_set_chg_flag(int index)
{
	int rc = 0;
	int policy_time = 1;

	if ((index < 0) || (index >= OPLUS_NOTIFY_CHG_MAX_CNT))
		return -1;

	mutex_lock(&oplus_chg_debug_info.nflag_lock);
	oplus_chg_debug_info.chg_cnt[index]++;

	if ((oplus_chg_debug_info.fast_chg_type == FASTCHG_CHARGER_TYPE_UNKOWN)
			|| (oplus_chg_debug_info.fast_chg_type == CHARGER_SUBTYPE_FASTCHG_WARP)) {
		policy_time = oplus_chg_debug_notify_policy[index].normal_chg_time;
	}
	else {
		policy_time = oplus_chg_debug_notify_policy[index].fast_chg_time;
	}

	switch(oplus_chg_debug_notify_policy[index].policy)
	{
		case OPLUS_CHG_NOTIFY_ONCE:
			if (oplus_chg_debug_info.chg_cnt[index] == policy_time)
			{
				oplus_chg_debug_info.notify_flag |= (1 << index);
			}
			rc = 0;
			break;
		case OPLUS_CHG_NOTIFY_REPEAT:
			if ((oplus_chg_debug_info.chg_cnt[index] % policy_time) == 0)
			{
				oplus_chg_debug_info.notify_flag |= (1 << index);
			}

			if ((index == OPLUS_NOTIFY_CHG_SLOW_CHECK_TIME)
					&& (oplus_chg_debug_info.chg_cnt[index] > policy_time)
					&& ((oplus_chg_debug_info.chg_cnt[index] - policy_time) % (policy_time / 3) == 0))
			{
				oplus_chg_debug_info.notify_flag |= (1 << index);
			}
			rc = 0;
			break;
		case OPLUS_CHG_NOTIFY_TOTAL:
			if (((oplus_chg_debug_info.chg_cnt[index] % (policy_time * oplus_chg_debug_notify_policy[index].percent / 100)) == 0))
			{
				oplus_chg_debug_info.notify_flag |= (1 << index);
			}
			rc = 0;
			break;
		default:
			rc = -1;
	}
#ifdef OPLUS_CHG_DEBUG_TEST
	chg_err("cnt: %d, index: %d, flag: 0x%x\n",
			oplus_chg_debug_info.chg_cnt[index], index, oplus_chg_debug_info.notify_flag);
#endif
	mutex_unlock(&oplus_chg_debug_info.nflag_lock);

	return rc;
}

static int oplus_chg_warp_charge_strategy_init(struct oplus_chg_chip *chip)
{
	int i;
	int index;
	int output_power_type = -1;
	int start_soc, start_temp;
	struct warp_charge_strategy *warp_charge_strategy_p;

	if (oplus_chg_debug_info.warp_charge_strategy != NULL)
	{
		for(i = oplus_chg_debug_info.warp_charge_input_current_index; i < OPLUS_CHG_WARP_INPUT_CURRENT_MAX_CNT; i++) {
			if (chip->batt_volt <= oplus_chg_debug_info.warp_charge_strategy->input_current[i].vol) {
				if (i == oplus_chg_debug_info.warp_charge_input_current_index)
					oplus_chg_debug_info.warp_charge_cur_state_chg_time +=5;

				if (oplus_chg_debug_info.warp_charge_strategy->input_current[i].time > 0) {
					if (oplus_chg_debug_info.warp_charge_cur_state_chg_time >= oplus_chg_debug_info.warp_charge_strategy->input_current[i].time) {
						if (i < (OPLUS_CHG_WARP_INPUT_CURRENT_MAX_CNT - 1)) {
							i++;
						}
					}
				}

				if (i != oplus_chg_debug_info.warp_charge_input_current_index) {
					oplus_chg_debug_info.warp_charge_cur_state_chg_time = 0;
					oplus_chg_debug_info.warp_charge_input_current_index = i;
				}

				break;
			}
		}

		return 0;
	}

	if (!oplus_chg_chg_is_normal_path(chip)) {
		if (oplus_warp_get_fastchg_started()) {
			index = oplus_chg_get_warp_adapter_type_index(chip);
			output_power_type = warp_charger_type_list[index].output_power_type;
		}
	}

	if (output_power_type >= 0) {
		start_soc = chip->soc;
		start_temp = chip->temperature;

		warp_charge_strategy_p = g_warp_charge_strategy_p[output_power_type];

		if (warp_charge_strategy_p) {
			for(i = 0; warp_charge_strategy_p[i].capacity_range.low != -1; i++) {
				if ((warp_charge_strategy_p[i].capacity_range.low < start_soc)
						&& (warp_charge_strategy_p[i].capacity_range.high >= start_soc)
						&& (warp_charge_strategy_p[i].temp_range.low < start_temp)
						&& (warp_charge_strategy_p[i].temp_range.high >= start_temp)) {
					oplus_chg_debug_info.warp_charge_strategy = &warp_charge_strategy_p[i];
					chg_err("output_power_type: %d, capacity_range [%d, %d], temp_range [%d, %d]\n",
							output_power_type,
							warp_charge_strategy_p[i].capacity_range.low,
							warp_charge_strategy_p[i].capacity_range.high,
							warp_charge_strategy_p[i].temp_range.low,
							warp_charge_strategy_p[i].temp_range.high);
					break;
				}
			}
		}
	}

	if (oplus_chg_debug_info.warp_charge_strategy) {
		for(i = oplus_chg_debug_info.warp_charge_input_current_index; i < OPLUS_CHG_WARP_INPUT_CURRENT_MAX_CNT; i++) {
			if (chip->batt_volt <= oplus_chg_debug_info.warp_charge_strategy->input_current[i].vol) {
				oplus_chg_debug_info.warp_charge_input_current_index = i;
				oplus_chg_debug_info.warp_charge_cur_state_chg_time = 5;
				break;
			}
		}
	}

	return 0;
}

#if 0
static int oplus_chg_warp_charge_strategy_post(struct oplus_chg_chip *chip)
{

	if (oplus_chg_debug_info.warp_charge_strategy != NULL)
		return 0;

	if (oplus_chg_chg_is_normal_path(chip))
		return 0;

	if (oplus_warp_get_fastchg_started())
	{
		oplus_chg_debug_info.warp_charge_cur_state_chg_time += 5;
	}
	else
	{
		oplus_chg_debug_info.warp_charge_strategy = NULL;
		oplus_chg_debug_info.warp_charge_input_current_index = 0;
		oplus_chg_debug_info.warp_charge_cur_state_chg_time = 0;
	}

	return 0;
}
#endif

int oplus_chg_debug_set_cool_down_by_user(int is_cool_down)
{
	oplus_chg_debug_info.cool_down_by_user = is_cool_down;
	return 0;
}

int oplus_chg_debug_get_cooldown_current(int chg_current_by_tbatt, int chg_current_by_cooldown)
{
	oplus_chg_debug_info.chg_current_by_tbatt = chg_current_by_tbatt;
	oplus_chg_debug_info.chg_current_by_cooldown = chg_current_by_cooldown;
	return 0;
}

static int oplus_chg_debug_fastchg_monitor_condition(struct oplus_chg_chip *chip)
{
	if (oplus_chg_debug_info.fastchg_stop_cnt < OPLUS_CHG_DEBUG_FASTCHG_STOP_CNT) {
		oplus_chg_debug_info.fastchg_stop_cnt++;
		return 0;
	}

	return 1;
}

static int oplus_chg_chg_is_normal_path(struct oplus_chg_chip *chip)
{
	struct oplus_warp_chip *warp_chip = NULL;

	if (!oplus_warp_get_fastchg_started())
		return 1;

	oplus_warp_get_warp_chip_handle(&warp_chip);
	if (warp_chip == NULL)
		return 1;

	if (warp_chip->support_warp_by_normal_charger_path
			&& (oplus_warp_get_fast_chg_type() == CHARGER_SUBTYPE_FASTCHG_WARP))
		return 1;

	return 0;
}

static int oplus_chg_fastchg_is_allowed(struct oplus_chg_chip *chip)
{
	struct oplus_warp_chip *warp_chip = NULL;

	if (chip->charger_type != POWER_SUPPLY_TYPE_USB_DCP)
		return 0;

	oplus_warp_get_warp_chip_handle(&warp_chip);
	if (warp_chip) {
		if (chip->temperature < warp_chip->warp_low_temp)
			return 0;

		if (chip->temperature > warp_chip->warp_high_temp)
			return 0;

		if (chip->soc < warp_chip->warp_low_soc)
			return 0;

		if (chip->soc > warp_chip->warp_high_soc)
			return 0;

		return 1;
	}
	else {
		return 0;
	}

	return 1;
}

static int oplus_chg_normal_chg_input_power_check(struct oplus_chg_chip *chip)
{
#ifdef CONFIG_OPLUS_SHORT_USERSPACE
#ifndef CONFIG_OPLUS_CHG_GKI_SUPPORT
	int input_current_settled;
	int input_voltage_settled;
	union power_supply_propval pval = {0, };
	int float_voltage;

	power_supply_get_property(oplus_chg_debug_info.usb_psy,
			POWER_SUPPLY_PROP_INPUT_CURRENT_SETTLED, &pval);
	input_current_settled = pval.intval;

	pval.intval = 0;
	power_supply_get_property(oplus_chg_debug_info.usb_psy,
			POWER_SUPPLY_PROP_VOLTAGE_MAX, &pval);
	input_voltage_settled = pval.intval;

	pval.intval = 0;
	power_supply_get_property(oplus_chg_debug_info.batt_psy,
			POWER_SUPPLY_PROP_VOLTAGE_MAX, &pval);
	float_voltage = pval.intval;

	if ((chip->batt_volt * 1000) < float_voltage) {
		if (((input_current_settled * input_voltage_settled / 1000) - (chip->ibus * chip->charger_volt))
				> INPUT_POWER_DECREASE) {
			oplus_chg_set_chg_flag(OPLUS_NOTIFY_CHG_SLOW_ADAPTER_INPUT_LOW_POWER);
		}
	}
	else {
		oplus_chg_reset_chg_cnt(OPLUS_NOTIFY_CHG_SLOW_ADAPTER_INPUT_LOW_POWER);
	}
#endif
#endif
	return 0;
}

static int oplus_chg_normal_chg_consume_power_check(struct oplus_chg_chip *chip)
{
	int input_power;
	int charge_power;

	input_power = oplus_chg_get_input_power(chip); 
	charge_power = oplus_chg_get_charge_power(chip);

	//系统功耗
	if (!chip->led_on) {
		//黑屏
		if ((oplus_chg_debug_info.total_time - oplus_chg_debug_info.led_off_start_time) > LED_OFF_STABLE_TIME) {
			if ((input_power + charge_power) > LED_OFF_HIGH_POWER_CONSUPTION) {
				oplus_chg_set_chg_flag(OPLUS_NOTIFY_CHG_SLOW_SYS_POWER_CONSUME_HIGH);
			}
		}
	}
	else {
		//亮屏
		if ((input_power + charge_power) > LED_ON_HIGH_POWER_CONSUPTION) {
			oplus_chg_set_chg_flag(OPLUS_NOTIFY_CHG_SLOW_SYS_POWER_CONSUME_HIGH);
		}
	}

	return 0;
}

static int oplus_chg_fastchg_chg_input_power_check(struct oplus_chg_chip *chip)
{
	int input_power;
	int charge_power;

	if (chip->led_on)
		return 0;

	if (oplus_chg_debug_info.cool_down_by_user)
		return 0;

	input_power = oplus_chg_get_warp_input_power(chip);
	charge_power = oplus_chg_get_charge_power(chip);

	if (((input_power + charge_power) > INPUT_POWER_DECREASE) && (!chip->led_on)
			&& (chip->batt_volt < HIGH_POWER_FASTCHG_BATT_VOLT)) {
		oplus_chg_set_chg_flag(OPLUS_NOTIFY_CHG_SLOW_ADAPTER_INPUT_LOW_POWER);
	}
	else {
		oplus_chg_reset_chg_cnt(OPLUS_NOTIFY_CHG_SLOW_ADAPTER_INPUT_LOW_POWER);
	}


	return 0;
}


static int oplus_chg_fastchg_consume_power_check(struct oplus_chg_chip *chip)
{
	int input_power;
	int charge_power;

	//系统功耗
	//if (chip->batt_volt < HIGH_POWER_FASTCHG_BATT_VOLT)
	{
		input_power = oplus_chg_get_warp_input_power(chip);
		charge_power = oplus_chg_get_charge_power(chip);

		if (!chip->led_on) {
			//黑屏
			if ((oplus_chg_debug_info.total_time - oplus_chg_debug_info.led_off_start_time) > LED_OFF_STABLE_TIME) {
				if ((input_power + charge_power) > LED_OFF_HIGH_POWER_CONSUPTION) {
					oplus_chg_set_chg_flag(OPLUS_NOTIFY_CHG_SLOW_SYS_POWER_CONSUME_HIGH);
				}
			}
		}
		else {
			if ((input_power + charge_power) > LED_ON_HIGH_POWER_CONSUPTION) {
				oplus_chg_set_chg_flag(OPLUS_NOTIFY_CHG_SLOW_SYS_POWER_CONSUME_HIGH);
			}
		}
	}

	return 0;
}

static int oplus_chg_fastchg_power_limit_check(struct oplus_chg_chip *chip)
{
	int warp_chg_max_input_power;
	int charge_power;

	warp_chg_max_input_power = oplus_chg_get_warp_chg_max_input_power(chip);
	charge_power = oplus_chg_get_charge_power(chip);
#ifdef OPLUS_CHG_DEBUG_TEST
	chg_err("warp_chg_max_input_power = %d, charge_power = %d\n",
			warp_chg_max_input_power, charge_power);
#endif


	if ((-1 * charge_power * 10) < (warp_chg_max_input_power * 5)) {
		oplus_chg_set_chg_flag(OPLUS_NOTIFY_CHG_SLOW_LOW_CHARGE_CURRENT_LONG_TIME);
	}

	return 0;
}

static int oplus_chg_input_power_limit_check(struct oplus_chg_chip *chip)
{
	int float_voltage;
	union power_supply_propval pval = {0, };
	int default_input_current = -1;
	int max_charger_voltage = -1;
	int charge_power = oplus_chg_get_charge_power(chip);
	struct oplus_warp_chip *warp_chip = NULL;

	oplus_warp_get_warp_chip_handle(&warp_chip);
	if (warp_chip) {
		if (chip->charger_type == POWER_SUPPLY_TYPE_USB_DCP) {
			if (warp_chip->fastchg_to_warm) {
				oplus_chg_set_chg_flag(OPLUS_NOTIFY_CHG_SLOW_FASTCHG_TO_WARM);
			}
			else {
				if ((chip->soc < warp_chip->warp_high_soc) && (chip->soc > warp_chip->warp_low_soc)) {
					if (chip->temperature >= warp_chip->warp_high_temp) {
						oplus_chg_set_chg_flag(OPLUS_NOTIFY_CHG_SLOW_BATT_WARM_TEMP);
					}

					if (chip->temperature <= warp_chip->warp_low_temp) {
						oplus_chg_set_chg_flag(OPLUS_NOTIFY_CHG_SLOW_BATT_COLD_TEMP);
					}
				}
			}
		}
	}

	if (oplus_chg_chg_is_normal_path(chip)) {
		power_supply_get_property(oplus_chg_debug_info.batt_psy,
				POWER_SUPPLY_PROP_VOLTAGE_MAX, &pval);
		float_voltage = pval.intval;

		pval.intval = 0;
		power_supply_get_property(oplus_chg_debug_info.usb_psy,
				POWER_SUPPLY_PROP_VOLTAGE_MAX, &pval);
		max_charger_voltage = pval.intval;

		if (chip->batt_volt < float_voltage) {
			switch(chip->real_charger_type)
			{
				case POWER_SUPPLY_TYPE_USB_DCP:
					if (oplus_warp_get_fast_chg_type() == CHARGER_SUBTYPE_FASTCHG_WARP) {
						default_input_current = chip->limits.default_input_current_warp_ma_normal;
					}
					else {
						default_input_current = chip->limits.default_input_current_charger_ma;
					}
					break;
				case POWER_SUPPLY_TYPE_USB_CDP:
					default_input_current = chip->limits.input_current_cdp_ma;
					break;
				case POWER_SUPPLY_TYPE_USB_PD:
					default_input_current = chip->limits.pd_input_current_charger_ma;
					break;
#ifndef CONFIG_OPLUS_CHARGER_MTK
				case POWER_SUPPLY_TYPE_USB_HVDCP:
				case POWER_SUPPLY_TYPE_USB_HVDCP_3:
					//case POWER_SUPPLY_TYPE_USB_HVDCP_3P5:
					default_input_current = chip->limits.qc_input_current_charger_ma;
					break;
#endif
				default:
					default_input_current = -1;
					max_charger_voltage = -1;
					break;
			}

			if ((default_input_current > 0) && (max_charger_voltage > 0)) {
				if ((-1 * charge_power * 10) < (default_input_current * max_charger_voltage * 6)) {
					oplus_chg_set_chg_flag(OPLUS_NOTIFY_CHG_SLOW_LOW_CHARGE_CURRENT_LONG_TIME);
				}
			}
		}
	}
	else {
		if (oplus_warp_get_fastchg_started() == true)
			oplus_chg_fastchg_power_limit_check(chip);
	}


	return 0;
}

static int oplus_chg_chg_slow_check(struct oplus_chg_chip *chip)
{
	struct timespec ts = {0, 0};
	static struct timespec charge_start_ts = {0, 0};

	if ((oplus_chg_debug_info.pre_prop_status != chip->prop_status)
			&& (chip->prop_status != POWER_SUPPLY_STATUS_CHARGING)) {
		if (!chip->charger_exist) {
			oplus_chg_debug_info_reset();
		}
		oplus_chg_debug_info.pre_prop_status = chip->prop_status;
	}

	if (chip->charger_exist) {
		if (!oplus_chg_debug_info.total_time) {
			oplus_chg_debug_info.total_time = 5;
			getnstimeofday(&charge_start_ts);
		}
		else {
			getnstimeofday(&ts);
			oplus_chg_debug_info.total_time = 5 + ts.tv_sec - charge_start_ts.tv_sec;
		}
	}
	else {
		oplus_chg_debug_info.total_time = 0;
	}

	if (chip->charger_exist && (chip->prop_status != POWER_SUPPLY_STATUS_FULL)) {
		//高温限流
		if ((chip->tbatt_status == BATTERY_STATUS__WARM_TEMP)
				|| (chip->tbatt_status == BATTERY_STATUS__HIGH_TEMP)) {
			oplus_chg_set_chg_flag(OPLUS_NOTIFY_CHG_SLOW_BATT_WARM_TEMP);

			if (chip->prop_status != POWER_SUPPLY_STATUS_CHARGING) {
				goto oplus_chg_slow_check_end;
			}
		}

		//低温限流
		if ((chip->tbatt_status == BATTERY_STATUS__COLD_TEMP)
				|| (chip->tbatt_status == BATTERY_STATUS__LOW_TEMP)
				|| (chip->tbatt_status == BATTERY_STATUS__REMOVED)) {
			oplus_chg_set_chg_flag(OPLUS_NOTIFY_CHG_SLOW_BATT_COLD_TEMP);

			if (chip->prop_status != POWER_SUPPLY_STATUS_CHARGING) {
				goto oplus_chg_slow_check_end;
			}
		}

		if (chip->notify_code & (1 << NOTIFY_CHARGER_OVER_VOL)) {
			oplus_chg_set_chg_flag(OPLUS_NOTIFY_CHG_SLOW_CHARGER_OV);

			if (chip->prop_status != POWER_SUPPLY_STATUS_CHARGING) {
				goto oplus_chg_slow_check_end;
			}
		}

		if (chip->notify_code & (1 << NOTIFY_CHARGER_LOW_VOL)) {
			oplus_chg_set_chg_flag(OPLUS_NOTIFY_CHG_SLOW_CHARGER_UV);

			if (chip->prop_status != POWER_SUPPLY_STATUS_CHARGING) {
				goto oplus_chg_slow_check_end;
			}
		}

	}

	if (!(chip->prop_status == POWER_SUPPLY_STATUS_CHARGING)) {
		return 0;
	}

	//detect the charger is fast-charger or not
	//if it's fast-charger, there is not need set unknown when it back to normal charge mode
	if (oplus_warp_get_fastchg_started()) {
		if (oplus_chg_debug_info.fast_chg_type == FASTCHG_CHARGER_TYPE_UNKOWN) {
			oplus_chg_debug_info.fast_chg_type = oplus_warp_get_fast_chg_type();
			chg_err("fast_chg_type = %d\n", oplus_chg_debug_info.fast_chg_type);
		}
	}

	if ((oplus_chg_debug_info.pre_prop_status != chip->prop_status)
			&& (oplus_chg_debug_info.pre_prop_status != POWER_SUPPLY_STATUS_CHARGING)) {
		oplus_chg_debug_info.chg_start_soc = chip->soc;
		oplus_chg_debug_info.chg_start_temp = chip->temperature;
		oplus_chg_debug_info.chg_start_time = oplus_chg_debug_info.total_time;
	}
	oplus_chg_debug_info.pre_prop_status = chip->prop_status;
	oplus_chg_debug_info.chg_total_time = oplus_chg_debug_info.total_time - oplus_chg_debug_info.chg_start_time;

	if ((oplus_chg_debug_info.total_time - oplus_chg_debug_info.chg_start_time) < OPLUS_CHG_START_STABLE_TIME) {
		goto oplus_chg_slow_check_end;
	}

	//电池认证
	if (!chip->authenticate) {
		oplus_chg_set_chg_flag(OPLUS_NOTIFY_CHG_SLOW_BATT_NON_AUTH);
		goto oplus_chg_slow_check_end;
	}

	if (!oplus_chg_chg_is_normal_path(chip)) {
		if (!oplus_chg_debug_fastchg_monitor_condition(chip)) {
			goto oplus_chg_slow_check_end;
		}
	}
	else {
		if (oplus_chg_debug_info.batt_psy) {
			union power_supply_propval pval = {0, };
			power_supply_get_property(oplus_chg_debug_info.batt_psy,
					POWER_SUPPLY_PROP_VOLTAGE_MAX, &pval);
			if (chip->batt_volt > pval.intval) {
				goto oplus_chg_slow_check_end;
			}
		}
	}

	//SDP充电
	if (chip->charger_type == POWER_SUPPLY_TYPE_USB) {
		oplus_chg_set_chg_flag(OPLUS_NOTIFY_CHG_SLOW_CHG_TYPE_SDP);
		goto oplus_chg_slow_check_end;
	}

	if (chip->notify_code & (1 << NOTIFY_CHGING_OVERTIME))
		oplus_chg_set_chg_flag(OPLUS_NOTIFY_CHG_SLOW_OVERTIME);

	if ((oplus_chg_debug_info.pre_led_state != chip->led_on)
			&& (!chip->led_on)) {
		oplus_chg_debug_info.led_off_start_time = oplus_chg_debug_info.total_time;
	}
	oplus_chg_debug_info.pre_led_state = chip->led_on;


	//warp charge strategy init
	oplus_chg_warp_charge_strategy_init(chip);

	//线损检测
	if (oplus_chg_chg_is_normal_path(chip)) {
		oplus_chg_normal_chg_input_power_check(chip);
	}
	else {
		oplus_chg_fastchg_chg_input_power_check(chip);
	}

	//充电器及充电类型检测
	if (chip->warp_project) {
#if 0
		if (!((chip->chargerid_volt <= OPLUS_CHARGERID_VOLT_HIGH_THLD)
					&& (chip->chargerid_volt >= OPLUS_CHARGERID_VOLT_LOW_THLD)))
		{
			//非oplus充电器
			oplus_chg_set_chg_flag(OPLUS_NOTIFY_CHG_SLOW_NON_OPLUS_CHARGER);
		}
		else
#endif
		{
			if ((oplus_chg_fastchg_is_allowed(chip) == 1)
					&& (oplus_warp_get_fastchg_started() == false)) {
				oplus_chg_set_chg_flag(OPLUS_NOTIFY_CHG_SLOW_WARP_NON_START);
			}
			else if ((oplus_warp_get_fastchg_started() == true)) {
				if (oplus_chg_get_warp_adapter_is_low_input_power(chip)) {
					oplus_chg_set_chg_flag(OPLUS_NOTIFY_CHG_SLOW_WARP_ADAPTER_NON_MAX_POWER);
				}
			}
		}
	}

	//系统功耗检测
	if (oplus_chg_chg_is_normal_path(chip)) {
		oplus_chg_normal_chg_consume_power_check(chip);
	}
	else {
		oplus_chg_fastchg_consume_power_check(chip);
	}

	//cool down检测
	if (!oplus_chg_chg_is_normal_path(chip)) {
		if (oplus_chg_debug_info.cool_down_by_user) {
			oplus_chg_set_chg_flag(OPLUS_NOTIFY_CHG_SLOW_COOLDOWN_LONG_TIME);
		}
	}

	//长时间处于限流阶段
#if 0
	if ((chip->tbatt_status == BATTERY_STATUS__WARM_TEMP) || (chip->tbatt_status == BATTERY_STATUS__COLD_TEMP))
	{
		if (chip->tbatt_status == BATTERY_STATUS__WARM_TEMP)
			oplus_chg_set_chg_flag(OPLUS_NOTIFY_CHG_SLOW_BATT_WARM_TEMP);
		if (chip->tbatt_status == BATTERY_STATUS__COLD_TEMP)
			oplus_chg_set_chg_flag(OPLUS_NOTIFY_CHG_SLOW_BATT_COLD_TEMP);
	}
	else if (oplus_warp_get_fastchg_started())
	{
		//TODO
	}
#else
	oplus_chg_input_power_limit_check(chip);
#endif

	//长时间亮屏
	if (chip->led_on) {
		oplus_chg_set_chg_flag(OPLUS_NOTIFY_CHG_SLOW_LED_ON_LONG_TIME);
	}
	//长时间camera打开
	if (chip->camera_on) {
		oplus_chg_set_chg_flag(OPLUS_NOTIFY_CHG_SLOW_CAM_ON_LONG_TIME);
	}
	//长时间calling
	if (chip->calling_on) {
		oplus_chg_set_chg_flag(OPLUS_NOTIFY_CHG_SLOW_CALLING_LONG_TIME);
	}

oplus_chg_slow_check_end:
	oplus_chg_set_chg_flag(OPLUS_NOTIFY_CHG_SLOW_CHECK_TIME);
	if (oplus_chg_debug_notify_flag_is_set(OPLUS_NOTIFY_CHG_SLOW_CHECK_TIME)) {
		oplus_chg_set_notify_type(OPLUS_CHG_DEBUG_NOTIFY_TYPE_CHG_SLOW);
	}
	return 0;
}

static int oplus_chg_chg_full_check(struct oplus_chg_chip *chip)
{
	union power_supply_propval pval = {0, };
	int status, capacity;

	power_supply_get_property(oplus_chg_debug_info.batt_psy,
			POWER_SUPPLY_PROP_STATUS, &pval);
	status = pval.intval;

#if 0
	power_supply_get_property(oplus_chg_debug_info.batt_psy,
			POWER_SUPPLY_PROP_CAPACITY, &pval);
	capacity = pval.intval;
#endif

	capacity = oplus_chg_debug_info.cur_soc;

	if ((status == POWER_SUPPLY_STATUS_FULL) && (capacity != 100)
			&& (!(oplus_chg_debug_info.chg_full_notified_flag & (1 << 0)))) {
		oplus_chg_set_chg_flag(OPLUS_NOTIFY_CHG_BATT_FULL_NON_100_CAP);

		if (oplus_chg_debug_notify_flag_is_set(OPLUS_NOTIFY_CHG_BATT_FULL_NON_100_CAP)) {
			oplus_chg_debug_info.chg_full_notified_flag |= (1 << 0);
			oplus_chg_set_notify_type(OPLUS_CHG_DEBUG_NOTIFY_TYPE_CHG_FULL);
		}
	}
	else if ((status == POWER_SUPPLY_STATUS_FULL) && (capacity == 100)) {
		oplus_chg_debug_info.chg_full_notified_flag &= ~(1 << 0);

		if (!(oplus_chg_debug_info.chg_full_notified_flag & (1 << 1))) {
			oplus_chg_set_chg_flag(OPLUS_NOTIFY_CHG_BATT_FULL_AND_100_CAP);

			if (oplus_chg_debug_notify_flag_is_set(OPLUS_NOTIFY_CHG_BATT_FULL_AND_100_CAP)) {
				oplus_chg_debug_info.chg_full_notified_flag |= (1 << 1);
				oplus_chg_set_notify_type(OPLUS_CHG_DEBUG_NOTIFY_TYPE_CHG_FULL);
			}
		}
	}
	else {
		oplus_chg_debug_info.chg_full_notified_flag = 0;
	}

	return 0;
}

//TODO
static int oplus_chg_chg_batt_aging_check(struct oplus_chg_chip *chip)
{
#ifndef OPLUS_CHG_OP_DEF
	union power_supply_propval pval = {0, };
#endif
	int ret;
	struct timespec ts;
	static struct timespec last_ts = {0, 0};
	struct rtc_time tm;
	static int count = 0;

#ifdef OPLUS_CHG_OP_DEF
	return 0;
#endif

#ifdef OPLUS_CHG_DEBUG_TEST
	if ((aging_cap_test > 0) || (count++ > OPLUS_CHG_BATT_AGING_CHECK_CNT))
#else
		if (count++ > OPLUS_CHG_BATT_AGING_CHECK_CNT)
#endif
		{

			getnstimeofday(&ts);

			if (last_ts.tv_sec == 0) {
				ret = oplus_chg_read_filedata(&last_ts);
				if (ret < 0) {
					ret = oplus_chg_write_filedata(&ts);
					if (!ret) {
						last_ts.tv_sec = ts.tv_sec;
						count = 0;
					}
				}
				return 0;
			}

			rtc_time_to_tm(last_ts.tv_sec, &tm);
			chg_err("last date: %d-%d-%d %d:%d:%d\n",
					tm.tm_year + 1900,
					tm.tm_mon,
					tm.tm_mday,
					tm.tm_hour,
					tm.tm_min,
					tm.tm_sec
			       );

			rtc_time_to_tm(ts.tv_sec, &tm);
			chg_err("current date: %d-%d-%d %d:%d:%d\n",
					tm.tm_year + 1900,
					tm.tm_mon,
					tm.tm_mday,
					tm.tm_hour,
					tm.tm_min,
					tm.tm_sec
			       );


#ifdef OPLUS_CHG_DEBUG_TEST
			if ((aging_cap_test > 0) || (ts.tv_sec - last_ts.tv_sec > OPLUS_CHG_BATT_AGING_CHECK_TIME))
#else
				if (ts.tv_sec - last_ts.tv_sec > OPLUS_CHG_BATT_AGING_CHECK_TIME)
#endif
				{
#if 0
					ret = power_supply_get_property(oplus_chg_debug_info.batt_psy,
							POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN, &pval);
					if (ret < 0)
						return -1;

					oplus_chg_debug_info.fcc_design = pval.intval;
#endif

#ifndef OPLUS_CHG_OP_DEF
					ret = power_supply_get_property(oplus_chg_debug_info.batt_psy,
							POWER_SUPPLY_PROP_BATTERY_FCC, &pval);
					if (ret < 0)
						return -1;
#endif

					ret = oplus_chg_write_filedata(&ts);
					if (!ret) {
						last_ts.tv_sec = ts.tv_sec;
						count = 0;
						oplus_chg_set_chg_flag(OPLUS_NOTIFY_BATT_AGING_CAP);
						if (oplus_chg_debug_notify_flag_is_set(OPLUS_NOTIFY_BATT_AGING_CAP)) {
							oplus_chg_set_notify_type(OPLUS_CHG_DEBUG_NOTIFY_TYPE_BATT_FCC);
#ifdef OPLUS_CHG_DEBUG_TEST
							aging_cap_test = -1;
#endif
						}

					}
				}
				else {
					count = 0;
				}
		}

	return 0;
}

int oplus_chg_debug_set_soc_info(struct oplus_chg_chip *chip)
{
	if (chip->sleep_tm_sec > 0) {
		//suspend
		oplus_chg_debug_info.sleep_tm_sec = chip->sleep_tm_sec;
		oplus_chg_debug_info.cur_soc = chip->soc;
		oplus_chg_debug_info.cur_ui_soc = chip->ui_soc;
	}
	else {
		//resume
		oplus_chg_chg_batt_capacity_jump_check(chip);
	}

	return 0;
}

static void oplus_chg_soc_load_dwork(struct work_struct *work)
{
	if (oplus_chg_debug_info.soc_load_flag & (1 << OPLUS_NOTIFY_BATT_SOC_CAPCITY_LOAD_JUMP)) {
		oplus_chg_set_chg_flag(OPLUS_NOTIFY_BATT_SOC_CAPCITY_LOAD_JUMP);
		if (oplus_chg_debug_notify_flag_is_set(OPLUS_NOTIFY_BATT_SOC_CAPCITY_LOAD_JUMP)) {
			oplus_chg_set_notify_type(OPLUS_CHG_DEBUG_NOTIFY_TYPE_SOC_JUMP);
		}
	}

	if (oplus_chg_debug_info.soc_load_flag & (1 << OPLUS_NOTIFY_BATT_UI_SOC_CAPCITY_LOAD_JUMP)) {
		oplus_chg_set_chg_flag(OPLUS_NOTIFY_BATT_UI_SOC_CAPCITY_LOAD_JUMP);
		if (oplus_chg_debug_notify_flag_is_set(OPLUS_NOTIFY_BATT_UI_SOC_CAPCITY_LOAD_JUMP)) {
			oplus_chg_set_notify_type(OPLUS_CHG_DEBUG_NOTIFY_TYPE_SOC_JUMP);
		}
	}

	oplus_chg_debug_info.soc_load_flag = 0;
}

static int oplus_chg_set_soc_notified_flag(int flag)
{
	oplus_chg_debug_info.soc_notified_flag |= (1 << flag);

	return 0;
}

static int oplus_chg_unset_soc_notified_flag(int flag)
{
	oplus_chg_debug_info.soc_notified_flag &= ~(1 << flag);

	return 0;
}

static int oplus_chg_soc_notified_flag_is_set(int flag)
{
	if (oplus_chg_debug_info.soc_notified_flag & (1 << flag))
		return 1;

	return 0;
}

static int oplus_chg_chg_batt_capacity_jump_check(struct oplus_chg_chip *chip)
{
	if (oplus_chg_debug_info.cur_soc == OPLUS_CHG_BATT_INVALID_CAPACITY) {
		oplus_chg_debug_info.pre_soc = chip->soc;
		oplus_chg_debug_info.cur_soc = chip->soc;
		oplus_chg_debug_info.cur_ui_soc = chip->ui_soc;
		oplus_chg_debug_info.pre_ui_soc = chip->ui_soc;

		if (abs(oplus_chg_debug_info.cur_soc - chip->soc_load) > OPLUS_CHG_BATT_S0C_CAPACITY_LOAD_JUMP_NUM) {
			oplus_chg_debug_info.soc_load_flag |= 1 << OPLUS_NOTIFY_BATT_SOC_CAPCITY_LOAD_JUMP;
		}

		if (abs(oplus_chg_debug_info.cur_ui_soc - chip->soc_load) > OPLUS_CHG_BATT_UI_S0C_CAPACITY_LOAD_JUMP_NUM) {
			oplus_chg_debug_info.soc_load_flag |= 1 << OPLUS_NOTIFY_BATT_UI_SOC_CAPCITY_LOAD_JUMP;
		}

		if (oplus_chg_debug_info.soc_load_flag) {
			//delay upload soc_load jump to wait for oplus_kevent deamon
			queue_delayed_work(oplus_chg_debug_info.oplus_chg_debug_wq,
					&oplus_chg_debug_info.soc_load_dwork, msecs_to_jiffies(SOC_LOAD_DELAY));
		}
	}
	else {
		oplus_chg_debug_info.pre_soc = oplus_chg_debug_info.cur_soc;
		oplus_chg_debug_info.pre_ui_soc = oplus_chg_debug_info.cur_ui_soc;
#ifdef OPLUS_CHG_DEBUG_TEST
		if (fake_soc >= 0) {
			oplus_chg_debug_info.cur_soc = fake_soc;
		}
		else {
			oplus_chg_debug_info.cur_soc = chip->soc;
		}

		if (fake_ui_soc >= 0) {
			oplus_chg_debug_info.cur_ui_soc = fake_ui_soc;
		}
		else {
			oplus_chg_debug_info.cur_ui_soc = chip->ui_soc;
		}
#else
		oplus_chg_debug_info.cur_soc = chip->soc;
		oplus_chg_debug_info.cur_ui_soc = chip->ui_soc;
#endif

		if ((abs(oplus_chg_debug_info.cur_soc - oplus_chg_debug_info.pre_soc) > OPLUS_CHG_BATT_S0C_CAPACITY_JUMP_NUM)
				&& (!oplus_chg_soc_notified_flag_is_set(OPLUS_NOTIFY_BATT_SOC_CAPCITY_JUMP))) {
			oplus_chg_set_chg_flag(OPLUS_NOTIFY_BATT_SOC_CAPCITY_JUMP);
			if (oplus_chg_debug_notify_flag_is_set(OPLUS_NOTIFY_BATT_SOC_CAPCITY_JUMP)) {
				oplus_chg_set_notify_type(OPLUS_CHG_DEBUG_NOTIFY_TYPE_SOC_JUMP);
				oplus_chg_set_soc_notified_flag(OPLUS_NOTIFY_BATT_SOC_CAPCITY_JUMP);
			}
		}
		else if (oplus_chg_debug_info.cur_soc == oplus_chg_debug_info.pre_soc) {
			oplus_chg_unset_soc_notified_flag(OPLUS_NOTIFY_BATT_SOC_CAPCITY_JUMP);
		}

		if ((abs(oplus_chg_debug_info.cur_ui_soc - oplus_chg_debug_info.pre_ui_soc) > OPLUS_CHG_BATT_UI_S0C_CAPACITY_JUMP_NUM)
				&& (!oplus_chg_soc_notified_flag_is_set(OPLUS_NOTIFY_BATT_UI_SOC_CAPCITY_JUMP))) {
			oplus_chg_set_chg_flag(OPLUS_NOTIFY_BATT_UI_SOC_CAPCITY_JUMP);
			if (oplus_chg_debug_notify_flag_is_set(OPLUS_NOTIFY_BATT_UI_SOC_CAPCITY_JUMP)) {
				oplus_chg_set_notify_type(OPLUS_CHG_DEBUG_NOTIFY_TYPE_SOC_JUMP);
				oplus_chg_set_soc_notified_flag(OPLUS_NOTIFY_BATT_UI_SOC_CAPCITY_JUMP);
			}
		}
		else if (oplus_chg_debug_info.cur_ui_soc == oplus_chg_debug_info.pre_ui_soc) {
			oplus_chg_unset_soc_notified_flag(OPLUS_NOTIFY_BATT_UI_SOC_CAPCITY_JUMP);
		}

		if ((abs(oplus_chg_debug_info.cur_ui_soc - oplus_chg_debug_info.cur_soc) > OPLUS_CHG_BATT_UI_TO_S0C_CAPACITY_JUMP_NUM)
				&& (!oplus_chg_soc_notified_flag_is_set(OPLUS_NOTIFY_BATT_UI_TO_SOC_CAPCITY_JUMP))) {
			oplus_chg_set_chg_flag(OPLUS_NOTIFY_BATT_UI_TO_SOC_CAPCITY_JUMP);
			if (oplus_chg_debug_notify_flag_is_set(OPLUS_NOTIFY_BATT_UI_TO_SOC_CAPCITY_JUMP)) {
				oplus_chg_set_notify_type(OPLUS_CHG_DEBUG_NOTIFY_TYPE_SOC_JUMP);
				oplus_chg_set_soc_notified_flag(OPLUS_NOTIFY_BATT_UI_TO_SOC_CAPCITY_JUMP);
			}
		}
		else if (oplus_chg_debug_info.cur_ui_soc == oplus_chg_debug_info.cur_soc) {
			oplus_chg_unset_soc_notified_flag(OPLUS_NOTIFY_BATT_UI_TO_SOC_CAPCITY_JUMP);
		}

		if (!oplus_chg_debug_notify_type_is_set(OPLUS_CHG_DEBUG_NOTIFY_TYPE_SOC_JUMP)) {
			oplus_chg_debug_info.sleep_tm_sec = 0;
		}

	}

	return 0;
}

int oplus_chg_debug_chg_monitor(struct oplus_chg_chip *chip)
{
	int rc = 0;

	if (!oplus_chg_debug_info.initialized)
		return -1;

	if (chip->authenticate) {
		oplus_chg_chg_batt_aging_check(chip);
		oplus_chg_chg_batt_capacity_jump_check(chip);

		oplus_chg_chg_full_check(chip);
	}

	oplus_chg_chg_slow_check(chip);

	oplus_chg_print_debug_info(chip);

	return rc;
}

#if 0
static unsigned long long oplus_chg_debug_get_notify_flag(bool reset)
{
	unsigned long long flag = 0;

	mutex_lock(&oplus_chg_debug_info.nflag_lock);
	flag = oplus_chg_debug_info.notify_flag;
	if (reset)
	{
		oplus_chg_debug_info.notify_flag = 0;
	}
	mutex_unlock(&oplus_chg_debug_info.nflag_lock);

	return flag;
}
#endif

static int oplus_chg_debug_notify_flag_is_set(int flag)
{
	int ret = 0;

	mutex_lock(&oplus_chg_debug_info.nflag_lock);
	if (oplus_chg_debug_info.notify_flag & (1 << flag)) {
		ret = 1;
	}
	mutex_unlock(&oplus_chg_debug_info.nflag_lock);

	return ret;

}
static int oplus_chg_debug_reset_notify_flag(void)
{
	mutex_lock(&oplus_chg_debug_info.nflag_lock);
	oplus_chg_debug_info.notify_flag = 0;
	mutex_unlock(&oplus_chg_debug_info.nflag_lock);

	return 0;
}

static int oplus_chg_debug_info_parse_dt(void)
{
	struct oplus_warp_chip *warp_chip = NULL;
	struct device_node *warp_node = NULL;
	struct device_node *node, *child;
	int ret;
	int i, j, k;
	int count;
	int size;
	u32 val[2];
	char *warp_adpater_name[] = {
		"warp_charge_strategy_20w",
		"warp_charge_strategy_30w",
		"warp_charge_strategy_50w",
		"warp_charge_strategy_65w"
	};

	oplus_warp_get_warp_chip_handle(&warp_chip);
	if (warp_chip == NULL)
		return 0;

	warp_node = warp_chip->dev->of_node;
	if (!warp_node)
		return 0;

	ret = of_property_read_u32(warp_node, "qcom,warp-max-input-volt-support", &oplus_chg_debug_info.warp_max_input_volt);
	if (ret) {
		oplus_chg_debug_info.warp_max_input_volt = 5000;
	}

	ret = of_property_read_u32(warp_node, "qcom,warp-max-input-current-support", &oplus_chg_debug_info.warp_max_input_current);
	if (ret) {
		oplus_chg_debug_info.warp_max_input_current = 4000;
	}

	for(i = 0; i < OPLUS_FASTCHG_OUTPUT_POWER_MAX; i++) {
		node = of_find_node_by_name(warp_node, warp_adpater_name[i]);
		if (!node)
			continue;

		count = 0;
		for_each_child_of_node(node, child)
			count++;

		if (!count)
			continue;

		g_warp_charge_strategy_p[i] = kzalloc((count + 1) * sizeof(struct warp_charge_strategy), GFP_KERNEL);

		j = 0;
		for_each_child_of_node(node, child)
		{
			ret = of_property_read_u32_array(child, "capacity_range", val, 2);
			if (!ret) {
				g_warp_charge_strategy_p[i][j].capacity_range.low = val[0];
				g_warp_charge_strategy_p[i][j].capacity_range.high = val[1];
			}

			ret = of_property_read_u32_array(child, "temp_range", val, 2);
			if (!ret) {
				g_warp_charge_strategy_p[i][j].temp_range.low = val[0];
				g_warp_charge_strategy_p[i][j].temp_range.high = val[1];
			}

			size = of_property_count_elems_of_size(child, "input_current", sizeof(u32));
			if (size >= 0) {
				for(k = 0; (k < (size / 3)) && (k < OPLUS_CHG_WARP_INPUT_CURRENT_MAX_CNT); k++)
				{
					ret = of_property_read_u32_index(child, "input_current", 3 * k, &val[0]);
					if (!ret) {
						g_warp_charge_strategy_p[i][j].input_current[k].cur = val[0];
					}

					ret = of_property_read_u32_index(child, "input_current", 3 * k + 1, &val[0]);
					if (!ret) {
						g_warp_charge_strategy_p[i][j].input_current[k].vol = val[0];
					}

					ret = of_property_read_u32_index(child, "input_current", 3 * k + 2, &val[0]);
					if (!ret) {
						g_warp_charge_strategy_p[i][j].input_current[k].time = val[0];
					}
				}
			}
			j++;
		}

		g_warp_charge_strategy_p[i][count].capacity_range.low = -1;
		g_warp_charge_strategy_p[i][count].capacity_range.high = -1;
	}

	return 0;
}

#ifdef OPLUS_CHG_DEBUG_TEST
static ssize_t charge_monitor_test_write(struct file *filp,
		const char __user *buf, size_t len, loff_t *data)
{
	char tmp_buf[32] = {0};
	int str_len;

	str_len = len;
	if (str_len >= sizeof(tmp_buf))
		str_len = sizeof(tmp_buf) - 1;

	if (copy_from_user(tmp_buf, buf, str_len))
	{
		chg_err("copy_from_user failed.\n");
		return -EFAULT;
	}

	chg_err("%s\n", tmp_buf);

	if (!strncmp(tmp_buf, "soc:", strlen("soc:"))) {
		sscanf(tmp_buf, "soc:%d", &fake_soc);
	}
	else if (!strncmp(tmp_buf, "ui_soc:", strlen("ui_soc:"))) {
		sscanf(tmp_buf, "ui_soc:%d", &fake_ui_soc);
	}
	else if (!strncmp(tmp_buf, "aging_cap:", strlen("aging_cap:"))) {
		sscanf(tmp_buf, "aging_cap:%d", &aging_cap_test);
	}

	return len;
}

static ssize_t charge_monitor_test_read(struct file *filp,
		char __user *buff, size_t count, loff_t *off)
{
	int len = 0;
	char buf[512] = {0};

	sprintf(buf, "fake_soc:%d, fake_ui_soc:%d, aging_cap_test:%d\n",
			fake_soc, fake_ui_soc, aging_cap_test);
	len = strlen(buf);

	if (len > *off)
		len -= *off;
	else
		len = 0;

	if (copy_to_user(buff, buf, (len < count ? len : count))) {
		return -EFAULT;
	}

	*off += len < count ? len : count;
	return (len < count ? len : count);
}

static const struct file_operations charge_monitor_test_fops = {
	.write = charge_monitor_test_write,
	.read = charge_monitor_test_read,
};


static int charge_monitor_test_init(void)
{
	struct proc_dir_entry *proc_dentry = NULL;

	proc_dentry = proc_create("charge_monitor_test", 0664, NULL,
			&charge_monitor_test_fops);
	if (!proc_dentry) {
		pr_err("proc_create charge_monitor_test fops fail!\n");
		return -1;
	}

	return 0;
}
#endif

static int oplus_chg_debug_info_reset(void)
{
	int i;

	chg_err("enter\n");

#if 1
	for(i = OPLUS_NOTIFY_CHG_SLOW_NON_OPLUS_CHARGER; i <= OPLUS_NOTIFY_CHG_SLOW_CHECK_TIME; i++)
	{
		oplus_chg_debug_info.chg_cnt[i] = 0;
	}
#else
	memset(oplus_chg_debug_info.chg_cnt, 0x0, sizeof(oplus_chg_debug_info.chg_cnt));
#endif

	oplus_chg_debug_info.fast_chg_type = FASTCHG_CHARGER_TYPE_UNKOWN;

	oplus_chg_debug_info.fastchg_stop_cnt = 0;
	oplus_chg_debug_info.cool_down_by_user = 0;
	oplus_chg_debug_info.chg_current_by_cooldown = 0;
	oplus_chg_debug_info.chg_current_by_tbatt = 0;
	oplus_chg_debug_info.fastchg_input_current = -1;


	oplus_chg_debug_info.warp_charge_strategy = NULL;
	oplus_chg_debug_info.warp_charge_input_current_index = 0;
	oplus_chg_debug_info.warp_charge_cur_state_chg_time = 0;

	oplus_chg_debug_info.chg_full_notified_flag = 0;

	oplus_chg_debug_reset_notify_flag();
	oplus_chg_reset_chg_notify_type();

	return 0;
}

int oplus_chg_debug_info_init(void)
{
	memset(&oplus_chg_debug_info, 0x0, sizeof(oplus_chg_debug_info));
	memset(&oplus_chg_debug_msg[0], 0x0, sizeof(oplus_chg_debug_msg));

	mutex_init(&oplus_chg_debug_info.nflag_lock);
	oplus_chg_debug_info_reset();

	oplus_chg_debug_info.pre_led_state = 1;
	oplus_chg_debug_info.pre_soc = OPLUS_CHG_BATT_INVALID_CAPACITY;
	oplus_chg_debug_info.cur_soc = OPLUS_CHG_BATT_INVALID_CAPACITY;
	oplus_chg_debug_info.pre_ui_soc = OPLUS_CHG_BATT_INVALID_CAPACITY;
	oplus_chg_debug_info.cur_ui_soc = OPLUS_CHG_BATT_INVALID_CAPACITY;

	oplus_chg_debug_info.usb_psy = power_supply_get_by_name("usb");
	oplus_chg_debug_info.batt_psy = power_supply_get_by_name("battery");

	oplus_chg_debug_info.oplus_chg_debug_wq = create_workqueue("oplus_chg_debug_wq");
#ifdef CONFIG_OPLUS_KEVENT_UPLOAD
	oplus_chg_debug_info.dcs_info = (struct kernel_packet_info *)&oplus_chg_debug_msg[0];
	INIT_DELAYED_WORK(&oplus_chg_debug_info.send_info_dwork, oplus_chg_send_info_dwork);
	mutex_init(&oplus_chg_debug_info.dcs_info_lock);
#endif
	INIT_DELAYED_WORK(&oplus_chg_debug_info.soc_load_dwork, oplus_chg_soc_load_dwork);

	oplus_chg_debug_info_parse_dt();

#ifdef OPLUS_CHG_DEBUG_TEST
	{
		int ret;
		ret = charge_monitor_test_init();
		if (ret < 0)
			return ret;
	}
#endif

	oplus_chg_debug_info.initialized = 1;

	return 0;
}


