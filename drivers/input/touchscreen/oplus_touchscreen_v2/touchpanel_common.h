/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef _TOUCHPANEL_COMMON_H_
#define _TOUCHPANEL_COMMON_H_

/*********PART1:Head files**********************/
#include <linux/i2c.h>
#include <linux/firmware.h>

#include "util_interface/touch_interfaces.h"
#include "tp_devices.h"
#include <linux/miscdevice.h>
#include <linux/version.h>


#ifdef CONFIG_TOUCHIRQ_UPDATE_QOS
#include <linux/pm_qos.h>
#define PM_QOS_TOUCH_WAKEUP_VALUE 400
#endif

#ifndef CONFIG_REMOVE_OPLUS_FUNCTION
#include <soc/oplus/device_info.h>
#endif

#if IS_ENABLED(CONFIG_DRM_OPLUS_PANEL_NOTIFY) || IS_ENABLED(CONFIG_DRM_PANEL_NOTIFY)
#include <drm/drm_panel.h>
#endif

#define TP_SUPPORT_MAX 3
#define TP_NAME_SIZE_MAX 25

#define TP_MSG_SIZE_MAX 32

#define EFTM (250)
#define FW_UPDATE_COMPLETE_TIMEOUT  msecs_to_jiffies(40*1000)

/*********PART2:Define Area**********************/

#define PAGESIZE 512
#define MAX_GESTURE_COORD 6
#define MAX_FINGER_NUM 10

#define UNKOWN_GESTURE		0
#define DOU_TAP			1   /* double tap*/
#define UP_VEE			2   /* V*/
#define DOWN_VEE		3   /* ^*/
#define LEFT_VEE		4   /* >*/
#define RIGHT_VEE		5   /* <*/
#define CIRCLE_GESTURE		6   /* O*/
#define DOU_SWIP		7   /* ||*/
#define LEFT2RIGHT_SWIP		8   /* -->*/
#define RIGHT2LEFT_SWIP		9   /* <--*/
#define UP2DOWN_SWIP		10  /* |v*/
#define DOWN2UP_SWIP		11  /* |^*/
#define M_GESTRUE		12  /* M*/
#define W_GESTURE		13  /* W*/
#define FINGER_PRINTDOWN	14
#define FRINGER_PRINTUP		15
#define SINGLE_TAP		16
#define HEART			17
#define S_GESTURE		18

#define HEALTH_REPORT_GRIP          "grip_report"
#define HEALTH_REPORT_BASELINE_ERR  "baseline_err"
#define HEALTH_REPORT_NOISE         "noise_count"
#define HEALTH_REPORT_SHIELD_PALM   "shield_palm"
#define HEALTH_REPORT_SHIELD_EDGE   "shield_edge"
#define HEALTH_REPORT_SHIELD_METAL  "shield_metal"
#define HEALTH_REPORT_SHIELD_WATER  "shield_water"
#define HEALTH_REPORT_SHIELD_ESD    "shield_esd"
#define HEALTH_REPORT_RST_HARD      "hard_rst"
#define HEALTH_REPORT_RST_INST      "inst_rst"
#define HEALTH_REPORT_RST_PARITY    "parity_rst"
#define HEALTH_REPORT_RST_WD        "wd_rst"
#define HEALTH_REPORT_RST_OTHER     "other_rst"

#define FINGERPRINT_DOWN_DETECT 0X0f
#define FINGERPRINT_UP_DETECT 0X1f

#define SUPPORT_SINGLE_TAP 3
/* bit operation */
#define SET_BIT(data, flag) ((data) |= (flag))
#define CLR_BIT(data, flag) ((data) &= ~(flag))
#define CHK_BIT(data, flag) ((data) & (flag))
#define VK_TAB {KEY_MENU, KEY_HOMEPAGE, KEY_BACK, KEY_SEARCH}

#define SET_GESTURE_BIT(state, state_flag, config, config_flag)\
	if (CHK_BIT(state, (1 << state_flag))) {\
		SET_BIT(config, (1 << config_flag));\
	} else {\
		CLR_BIT(config, (1 << config_flag));\
	}


#define TOUCH_BIT_CHECK           0x3FF  /*max support 10 point report.using for detect non-valid points*/
#define MAX_FW_NAME_LENGTH        60
#define MAX_EXTRA_NAME_LENGTH     60

#define MAX_DEVICE_VERSION_LENGTH 16
#define MAX_DEVICE_MANU_LENGTH    16

#define MESSAGE_SIZE              (256)

#define SMOOTH_LEVEL_NUM            6
#define SENSITIVE_LEVEL_NUM         6

#define SYNAPTICS_PREFIX    "SY_"
#define GOODIX_PREFIX       "GT_"
#define FOCAL_PREFIX        "FT_"

#define SMART_GESTURE_THRESHOLD 0x0A
#define SMART_GESTURE_LOW_VALUE 0x05

#define FW_UPDATE_DELAY        msecs_to_jiffies(2*1000)

#define RECORD_POINTS_COUNT 5

/*********PART3:Struct Area**********************/
typedef enum {
	TYPE_ONCELL = 0,   /*such as synaptic s3706*/
	TYPE_TDDI,         /*such as tddi with flash or no flash*/
	TYPE_TDDI_TCM,     /*TCM with flash or no flash*/
	TYPE_IC_MAX,
} tp_ic_type;

typedef enum debug_level {
	LEVEL_BASIC,    /*printk basic tp debug info*/
	LEVEL_DETAIL,   /*printk tp detail log for stress test*/
	LEVEL_DEBUG,    /*printk all tp debug info*/
} tp_debug_level;

typedef enum {
	MODE_NORMAL,
	MODE_SLEEP,
	MODE_EDGE,
	MODE_GESTURE,
	MODE_CHARGE,
	MODE_GAME,
	MODE_PALM_REJECTION,
	MODE_FACE_DETECT,
	MODE_HEADSET,
	MODE_WIRELESS_CHARGE,
} work_mode;

/******For FW area********/
typedef enum {
	FW_NORMAL,     /*fw might update, depend on the fw id*/
	FW_ABNORMAL,   /*fw abnormal, need update*/
} fw_check_state;

typedef enum {
	FW_UPDATE_SUCCESS,
	FW_NO_NEED_UPDATE,
	FW_UPDATE_ERROR,
	FW_UPDATE_FATAL,
} fw_update_state;

struct firmware_headfile {
	const uint8_t *firmware_data;
	size_t firmware_size;
};

/******For IRQ area********/
typedef enum IRQ_TRIGGER_REASON {
	IRQ_IGNORE      = 0x00,
	IRQ_TOUCH       = 0x01,
	IRQ_GESTURE     = 0x02,
	IRQ_BTN_KEY     = 0x04,
	IRQ_EXCEPTION   = 0x08,
	IRQ_FW_CONFIG   = 0x10,
	IRQ_FW_HEALTH   = 0x20,
	IRQ_FW_AUTO_RESET = 0x40,
	IRQ_FACE_STATE    = 0x80,
	IRQ_FINGERPRINT   = 0x0100,
} irq_reason;

typedef enum interrupt_mode {
	BANNABLE,
	UNBANNABLE,
	INTERRUPT_MODE_MAX,
} tp_interrupt_mode;

/******For suspend and resume area********/
typedef enum {
	TP_SUSPEND_EARLY_EVENT,
	TP_SUSPEND_COMPLETE,
	TP_RESUME_EARLY_EVENT,
	TP_RESUME_COMPLETE,
	TP_SPEEDUP_RESUME_COMPLETE,
} suspend_resume_state;

typedef enum switch_mode_type {
	SEQUENCE,
	SINGLE,
} tp_switch_mode;

typedef enum resume_order {
	TP_LCD_RESUME,
	LCD_TP_RESUME,
} tp_resume_order;

typedef enum suspend_order {
	TP_LCD_SUSPEND,
	LCD_TP_SUSPEND,
} tp_suspend_order;

typedef enum lcd_power {
	LCD_POWER_OFF,
	LCD_POWER_ON,
} lcd_power_status;

typedef enum lcd_event_type {
	LCD_CTL_TP_LOAD_FW = 0x10,
	LCD_CTL_RST_ON,
	LCD_CTL_RST_OFF,
	LCD_CTL_TP_FTM,
	LCD_CTL_TP_FPS60,
	LCD_CTL_TP_FPS90,
	LCD_CTL_TP_FPS120,
	LCD_CTL_TP_FPS180,
	LCD_CTL_TP_FPS240,
	LCD_CTL_CS_ON,
	LCD_CTL_CS_OFF,
} lcd_event_type;

typedef enum {
	OEM_VERIFIED_BOOT_STATE_UNLOCKED,
	OEM_VERIFIED_BOOT_STATE_LOCKED,
} oem_verified_boot_state;

/******For INPUT area********/
typedef enum {
	AREA_NOTOUCH,
	AREA_EDGE,
	AREA_CRITICAL,
	AREA_NORMAL,
	AREA_CORNER,
} touch_area;

struct Coordinate {
	int x;
	int y;
};

enum touch_direction {
	VERTICAL_SCREEN,
	LANDSCAPE_SCREEN_90,
	LANDSCAPE_SCREEN_270,
};

struct gesture_info {
	uint32_t gesture_type;
	uint32_t clockwise;
	struct Coordinate Point_start;
	struct Coordinate Point_end;
	struct Coordinate Point_1st;
	struct Coordinate Point_2nd;
	struct Coordinate Point_3rd;
	struct Coordinate Point_4th;
};

struct point_info {
	uint16_t x;
	uint16_t y;
	uint16_t z;
	uint8_t  width_major;
	uint8_t  touch_major;
	uint8_t  status;
	uint8_t  tx_press;
	uint8_t  rx_press;
	uint8_t  tx_er;
	uint8_t  rx_er;
	touch_area type;
};

struct corner_info {
	uint8_t id;
	bool flag;
	struct point_info point;
};

struct touch_major_limit {
	int width_range;
	int height_range;
};

struct fp_underscreen_info {
	uint8_t touch_state;
	uint8_t area_rate;
	uint16_t x;
	uint16_t y;
};

struct specific_resume_data {
	suspend_resume_state suspend_state;
	bool in_test_process;
};

/******For button area********/
typedef enum {
	TYPE_PROPERTIES = 1,   /*using board_properties*/
	TYPE_AREA_SEPRATE,     /*using same IC (button zone &&  touch zone are seprate)*/
	TYPE_DIFF_IC,          /*using diffrent IC (button zone &&  touch zone are seprate)*/
	TYPE_NO_NEED,          /*No need of virtual key process*/
} vk_type;

typedef enum vk_bitmap {
	BIT_reserve    = 0x08,
	BIT_BACK       = 0x04,
	BIT_HOME       = 0x02,
	BIT_MENU       = 0x01,
} vk_bitmap;

struct button_map {
	int width_x;                                        /*width of each key area */
	int height_y;                                       /*height of each key area*/
	struct Coordinate coord_menu;                       /*Menu centre coordinates*/
	struct Coordinate coord_home;                       /*Home centre coordinates*/
	struct Coordinate coord_back;                       /*Back centre coordinates*/
};

/******For HW resource area********/
struct panel_info {
	char    *fw_name;                               /*FW name*/
	char    *test_limit_name;                       /*test limit name*/
	char    *aging_test_limit_name;                 /*aging test limit name*/
	char    *extra;                                 /*for some ic, may need other information*/
	uint32_t tp_fw;                                 /*FW Version Read from IC*/
	tp_dev  tp_type;
	int    vid_len;                                 /*Length of tp name show in  test apk*/
	u32    project_id;
	uint32_t    panel_type[10];
	uint32_t    platform_support_project[10];
	uint32_t    platform_support_project_dir[10];
	char  *platform_support_commandline[10];
	char  *firmware_name[10];
	char  *chip_name[10];                         /*chip name the panel is controlled by*/
	int    project_num;
	int    panel_num;
	int    chip_num;
	struct firmware_headfile  firmware_headfile;
	/*firmware headfile for noflash ic*/
#ifndef CONFIG_REMOVE_OPLUS_FUNCTION
	struct manufacture_info manufacture_info;       /*touchpanel device info*/
#endif
};

struct hw_resource {
	int irq_gpio;                                   /*irq GPIO num*/
	int reset_gpio;                                 /*Reset GPIO*/
	int cs_gpio;                                    /*CS GPIO*/
	int enable_avdd_gpio;                             /*avdd enable GPIO*/
	int enable_vddi_gpio;                             /*vddi enable GPIO*/

	/*TX&&RX Num*/
	int tx_num;
	int rx_num;
	int key_tx;                                     /*the tx num occupied by touchkey*/
	int key_rx;                                     /*the rx num occupied by touchkey*/

	/*power*/
	struct regulator *avdd;                      /*power avdd 2.8-3.3v*/
	struct regulator *vddi;                      /*power vddi 1.8v*/
	uint32_t vdd_volt;                              /*avdd specific volt*/

	/*pinctrl*/
	struct pinctrl          *pinctrl;
	struct pinctrl_state    *pin_set_high;
	struct pinctrl_state    *pin_set_low;
	struct pinctrl_state    *pin_set_nopull;
};

struct resolution_info {
	uint32_t max_x;                                     /*touchpanel width */
	uint32_t max_y;                                     /*touchpanel height*/
	uint32_t LCD_WIDTH;                                 /*LCD WIDTH        */
	uint32_t LCD_HEIGHT;                                /*LCD HEIGHT       */
};

/******For debug node area********/
struct esd_information {
	bool esd_running_flag;
	int  esd_work_time;
	struct mutex             esd_lock;
	struct workqueue_struct *esd_workqueue;
	struct delayed_work      esd_check_work;
};

struct freq_hop_info {
	struct workqueue_struct *freq_hop_workqueue;
	struct delayed_work      freq_hop_work;
	bool freq_hop_simulating;
	int freq_hop_freq;                                  /*save frequency-hopping frequency, trigger frequency-hopping every freq_hop_freq seconds*/
};

struct register_info {
	uint8_t reg_length;
	uint16_t reg_addr;
	uint8_t *reg_result;
};

/******For auto test area********/
struct black_gesture_test {
	bool gesture_backup;                    /*store the gesture enable flag */
	bool flag;                                         /* indicate do black gesture test or not*/
	char *message;                               /* failure information if gesture test failed */
	int message_size;                           /* failure information size if gesture test failed */
};

struct com_test_data {
	const struct firmware *limit_fw;      /*test limit fw*/
	const struct firmware *black_test_fw;      /*test limit fw*/
	void *chip_test_ops;
	/*save auto test result data*/
	void *result_data;
	size_t result_max_len;
	size_t result_flag;
	size_t result_cur_len;
	/*save black screen test result data*/
	void *bs_result_data;
	size_t bs_result_max_len;
	size_t bs_result_flag;
	size_t bs_result_cur_len;
};

/******For health monitor area********/
typedef enum {
	STATE_UP = 0,
	STATE_DOWN,
} down_up_state;

typedef enum {
	ACTION_CLICK = 0,
	ACTION_SWIPE,
} touch_action;

typedef enum {
	TYPE_RECORD_INT = 0,
	TYPE_RECORD_STR,
} value_record_type;

typedef enum {
	TYPE_NONE,
	TYPE_START_RECORD,
	TYPE_END_RECORD,
} grip_time_record_type;

struct point_state_monitor {
	u64 time_counter;
	struct point_info first_point;
	struct point_info last_point;
	unsigned long last_swipe_distance_sq;
	unsigned long max_swipe_distance_sq;
	int jumping_times;
	down_up_state down_up_state;
	touch_action touch_action;
	bool is_down_handled;
};

struct health_value_count {
	struct list_head head;
	value_record_type value_type;
	void *value;
	int count;
};

struct points_record {
	int count;
	struct Coordinate points[RECORD_POINTS_COUNT];
};

struct swipes_record {
	int count;
	struct Coordinate start_points[RECORD_POINTS_COUNT];
	struct Coordinate end_points[RECORD_POINTS_COUNT];
};

struct monitor_data {
	void  *chip_data; /*debug info data*/
	struct debug_info_proc_operations  *debug_info_ops; /*debug info data*/

	u64 boot_time;
	u64 stat_time;
	u64 probe_time;
	u64 max_resume_time;
	u64 max_suspend_time;
	u64 max_fw_update_time;
	char *fw_version;
	const char *tp_ic;
	char *vendor;

	bool health_monitor_support;
	bool kernel_grip_support;
	int max_finger_support;
	int tx_num;
	int rx_num;
	uint32_t max_x;
	uint32_t max_y;
	uint32_t long_swipe_judge_distance;
	uint32_t swipe_broken_judge_distance;
	uint32_t jumping_point_judge_distance;

	int report_rate;
	int report_rate_in_game;
	bool in_game_mode;

	struct point_info *touch_points;
	int touch_num;
	int direction;

	struct point_state_monitor *points_state;

	struct list_head        dead_zone_list;
	struct list_head        condition_zone_list;
	struct list_head        elimination_zone_list;
	struct points_record    dead_zone_points;
	struct points_record    condition_zone_points;
	struct points_record    elimination_zone_points;
	struct points_record    lanscape_elimination_zone_points;
	int elizone_point_tophalf_i;
	int elizone_point_bothalf_i;

	int32_t *jumping_points_count_array;
	int32_t *stuck_points_count_array;
	int32_t *lanscape_stuck_points_count_array;
	int32_t *broken_swipes_count_array;
	struct swipes_record    long_swipes;

	int32_t *jumping_point_delta_data;
	int32_t *stuck_point_delta_data;

	int max_jumping_times;
	int max_touch_num;
	int max_touch_num_in_game;
	int current_touch_num;

	int click_count;
	int swipe_count;
	int32_t *click_count_array;

	u64 touch_timer;
	u64 holding_touch_time;
	u64 total_touch_time;
	u64 *total_touch_time_in_game;
	u64 max_holding_touch_time;

	u64 total_grip_time_no_touch;
	u64 total_grip_time_no_touch_one_sec;
	u64 total_grip_time_no_touch_two_sec;
	u64 total_grip_time_no_touch_three_sec;
	u64 total_grip_time_no_touch_five_sec;
	u64 grip_start_time_no_touch;
	grip_time_record_type grip_time_record_flag;

	u64 screenon_timer;
	u64 total_screenon_time;

	int auto_test_total_times;
	int auto_test_failed_times;

	int blackscreen_test_total_times;
	int blackscreen_test_failed_times;

	int gesture_waiting;
	bool is_gesture_waiting_read;
	u64 gesture_received_time;

	struct list_head        gesture_values_list;
	struct list_head        invalid_gesture_values_list;
	struct list_head        fp_area_rate_list;
	struct list_head        fd_values_list;
	struct list_head        health_report_list;
	struct list_head        bus_errs_list;
	struct list_head        bus_errs_buff_list;
	struct list_head        alloc_err_funcs_list;
	struct list_head        fw_update_result_list;

	unsigned char *bus_buf;
	uint16_t bus_len;

	long alloced_size;
	long max_alloc_err_size;
	long min_alloc_err_size;

	u32 smooth_level_chosen;
	u32 sensitive_level_chosen;
};

struct com_api_data {
	spinlock_t tp_irq_lock;
	int tp_irq_disable;
};

typedef enum {
	CHARGE = 1,
	HEADSET,
	WIRELESS_CHARGE,
} misc_device_type;

/******For debug apk area********/
/*#define CONFIG_OPLUS_TP_APK please define this in arch/arm64/configs*/
#ifdef CONFIG_OPLUS_TP_APK

typedef enum {
	APK_NULL       = 0,
	APK_CHARGER    = 'C',
	APK_DATA       = 'D',
	APK_EARPHONE   = 'E',
	APK_GESTURE    = 'G',
	APK_INFO       = 'I',
	APK_NOISE      = 'N',
	APK_PROXIMITY  = 'P',
	APK_WATER      = 'W',
	APK_DEBUG_MODE = 'd',
	APK_GAME_MODE  = 'g'
} APK_SWITCH_TYPE;

typedef enum {
	DATA_NULL   = 0,
	BASE_DATA   = 'B',
	DIFF_DATA   = 'D',
	DEBUG_INFO  = 'I',
	RAW_DATA    = 'R',
	BACK_DATA   = 'T'
} APK_DATA_TYPE;

typedef struct apk_proc_operations {
	void (*apk_game_set)(void *chip_data, bool on_off);
	bool (*apk_game_get)(void *chip_data);
	void (*apk_debug_set)(void *chip_data, bool on_off);
	bool (*apk_debug_get)(void *chip_data);
	void (*apk_noise_set)(void *chip_data, bool on_off);
	bool (*apk_noise_get)(void *chip_data);
	void (*apk_water_set)(void *chip_data, int type);
	int (*apk_water_get)(void *chip_data);
	void (*apk_proximity_set)(void *chip_data, bool on_off);
	int (*apk_proximity_dis)(void *chip_data);
	void (*apk_gesture_debug)(void *chip_data, bool on_off);
	bool (*apk_gesture_get)(void *chip_data);
	int (*apk_gesture_info)(void *chip_data, char *buf, int len);
	void (*apk_earphone_set)(void *chip_data, bool on_off);
	bool (*apk_earphone_get)(void *chip_data);
	void (*apk_charger_set)(void *chip_data, bool on_off);
	bool (*apk_charger_get)(void *chip_data);
	int (*apk_tp_info_get)(void *chip_data, char *buf, int len);
	void (*apk_data_type_set)(void *chip_data, int type);
	int (*apk_rawdata_get)(void *chip_data, char *buf, int len);
	int (*apk_diffdata_get)(void *chip_data, char *buf, int len);
	int (*apk_basedata_get)(void *chip_data, char *buf, int len);
	int (*apk_backdata_get)(void *chip_data, char *buf, int len);
} APK_OPERATION;

#endif

#define SNR_RESET(snr)  \
	do { \
		snr.max = 0; \
		snr.min = 0; \
		snr.sum = 0; \
		snr.noise = 0; \
	} while (0)

struct touchpanel_snr {
	uint16_t x;
	uint16_t y;
	uint8_t channel_x;
	uint8_t channel_y;
	uint8_t point_status;
	bool doing;
	int max;
	int min;
	int sum;
	int noise;
};

struct aging_test_proc_operations;
struct debug_info_proc_operations;
struct touchpanel_data {
	/******For feature area********/
	bool register_is_16bit;                             /*register is 16bit*/
	bool black_gesture_support;                         /*black_gesture support feature*/
	bool black_gesture_indep_support;                   /*black_gesture indep control support feature*/
	bool charger_pump_support;                          /*charger_pump support feature*/
	bool wireless_charger_support;                      /*wireless_charger support feature*/
	bool headset_pump_support;                          /*headset_pump support feature*/
	bool fw_edge_limit_support;                         /*edge_limit by FW support feature*/
	bool esd_handle_support;                            /*esd handle support feature*/
	bool gesture_test_support;                          /*indicate test black gesture or not*/
	bool game_switch_support;                           /*indicate game switch support or not*/
	bool face_detect_support;                           /*touch porximity function*/
	bool fingerprint_underscreen_support;               /*fingerprint underscreen support*/
	bool sec_long_low_trigger;                          /*samsung s6d7ate ic int feature*/
	bool suspend_gesture_cfg;
	bool auto_test_force_pass_support;                  /*auto test force pass in early project*/
	bool freq_hop_simulate_support;                     /*frequency hopping simulate feature*/
	bool lcd_trigger_load_tp_fw_support;                /*trigger load tp fw by lcd driver after lcd reset*/
	bool fw_update_app_support;                         /*bspFwUpdate is used*/
	bool health_monitor_support;                        /*health_monitor is used*/
	bool irq_trigger_hdl_support;                       /*some no-flash ic (such as TD4330) need irq to trigger hdl*/
	bool noise_modetest_support;                        /*noise mode test is used*/
	bool fw_update_in_probe_with_headfile;
	bool optimized_show_support;                       /*support to show total optimized time*/
	uint32_t single_optimized_time;                    /*single touch optimized time*/
	uint32_t total_operate_times;                      /*record total touch down and up count*/
	struct firmware                 *firmware_in_dts;
	bool kernel_grip_support;                           /*using grip function in kernel touch driver*/
	bool grip_no_driver_support;
	bool high_frame_rate_support;
	uint32_t high_frame_rate_time;
	bool snr_read_support;                              /*feature to support reading snr data*/
	int tp_index;
	/******For FW update area********/
	bool loading_fw;                                    /*touchpanel FW updating*/
	int firmware_update_type;                           /*firmware_update_type: 0=check firmware version 1=force update; 2=for FAE debug*/
	struct completion fw_complete;						/*completion for control fw update*/
	struct work_struct     fw_update_work;              /*using for fw update*/
	/*trigger load tp fw by lcd driver after lcd reset*/
	struct work_struct lcd_trigger_load_tp_fw_work;
	/*trigger laod tp fw by lcd driver after lcd reset*/
	struct workqueue_struct *lcd_trigger_load_tp_fw_wq;

	/******For auto test area********/
	bool in_test_process;                     /*flag whether in test process*/
	struct black_gesture_test gesture_test;  /*screen off gesture test struct*/
	struct com_test_data com_test_data;	/*test comon data*/
	struct engineer_test_operations   *engineer_ops;     /*call_back function*/
	bool auto_test_need_cal_support;

	/******For button key area********/
	/*every bit declear one state of key "reserve(keycode)|home(keycode)|menu(keycode)|back(keycode)"*/
	u8   vk_bitmap;
	vk_type  vk_type;                           /*virtual_key type*/
	struct button_map      button_map;          /*virtual_key button area*/

	/******For irq flow area********/
	uint32_t irq_flags;                      /*irq setting flag*/
	int irq;                                 /*irq num*/
	uint32_t irq_flags_cover;                /*cover irq setting flag*/
	tp_interrupt_mode int_mode;	/*whether interrupt and be disabled*/
	wait_queue_head_t wait;
	bool up_status;
	int touch_report_num;
	struct point_info last_point;
	int last_width_major;
	int point_num;
	char irq_name[TP_NAME_SIZE_MAX];

	/******For gesture area********/
	bool disable_gesture_ctrl;                          /*when lcd_trigger_load_tp_fw start no need to control gesture*/
	int gesture_enable;                                 /*control state of black gesture*/
	struct gesture_info    gesture;                     /*gesture related info*/
	int gesture_enable_indep;                         /*independent control state of black gesture*/

	/******For fingerprint area********/
	int fp_enable;                                      /*underscreen fingerprint enable or not*/
	struct fp_underscreen_info fp_info;	/*tp info used for underscreen fingerprint*/

	/******For pm suspend and resume area********/
	bool bus_ready;                                     /*spi or i2c resume status*/
	int limit_edge;                                     /*control state of limit edge*/
	int is_suspended;                                   /*suspend/resume flow exec flag*/
	suspend_resume_state suspend_state;	/*detail suspend/resume state*/
	tp_resume_order tp_resume_order;
	tp_suspend_order tp_suspend_order;
	bool skip_reset_in_resume;                          /*some incell ic is reset by lcd reset*/

	/*LCD and TP is in one chip,lcd power off in suspend at first, can not operate i2c when tp suspend*/
	bool skip_suspend_operate;
	/******For hw resource area********/
	struct panel_info panel_data;	/*GPIO control(id && pinctrl && tp_type)*/
	struct hw_resource     hw_res;                      /*hw resourc information*/
	struct resolution_info resolution_info;	/*resolution of touchpanel && LCD*/
	/*used for control touch major reporting area*/
	struct touch_major_limit touch_major_limit;


	/******For debug node area********/
	struct esd_information  esd_info;                    /*debug info esd check*/
	struct freq_hop_info    freq_hop_info;                /*debug info freq_hop*/
	/*struct proc_dir_entry of "/proc/touchpanel"*/
	struct proc_dir_entry *prEntry_tp;
	/*struct proc_dir_entry of "/proc/touchpanel/debug_info"*/
	struct proc_dir_entry *prEntry_debug_tp;

	struct debug_info_proc_operations  *debug_info_ops; /*debug info data*/
	struct aging_test_proc_operations  *aging_test_ops;
	struct register_info reg_info;	/*debug node for register length*/
	struct monitor_data    monitor_data;                /*health monitor data*/
	struct touchpanel_snr   snr[MAX_FINGER_NUM];        /*snr data*/

	/******For prevention area********/
	struct mutex		report_mutex;                /*mutex for lock input report flow*/

	/******For comon data area********/
	struct mutex		mutex;                       /*mutex for lock i2c related flow*/
	struct device         *dev;                         /*used for i2c->dev*/
	struct i2c_client     *client;                        /*used for i2c client*/
	struct spi_device     *s_client;                    /*used for spi client*/
	struct input_dev      *input_dev;                   /*used for touch*/
	struct input_dev      *kpd_input_dev;               /*used for key*/
	struct input_dev      *ps_input_dev;                /*used for face dectet*/
	struct oplus_touchpanel_operations *ts_ops;          /*call_back function*/

	void    *chip_data;                                 /*Chip Related data*/
	void    *private_data;                              /*Reserved Private data*/
	tp_ic_type    tp_ic_type;

	/******For other feature area********/
	bool is_incell_panel;                               /*touchpanel is incell*/
	bool is_noflash_ic;                                 /*noflash ic*/
	int palm_enable;                                    /*palm enable or not*/
	int fd_enable;                                        /*face dectet enable or not*/
	int touch_count;                                    /*touch number*/
	int boot_mode;                                      /*boot up mode */
	int view_area_touched;                              /*view area touched flag*/
	int force_update;                                   /*force update flag*/
	int max_num;                                        /*max muti-touch num supportted*/
	int irq_slot;                                       /*debug use, for print all finger's first touch log*/
	uint8_t	report_point_first_enable;               /*reporting points first enable :1 ,disable 0;*/
	int noise_level;                                    /*for game mode control*/
	int high_frame_value;
	int limit_enable;                                   /*control state of limit enable */

	bool report_rate_white_list_support;
	int rate_ctrl_level;
	bool temperature_detect_support;
	struct iio_channel *skin_therm_chan;
	struct hrtimer		temp_timer;
	struct work_struct get_temperature_work;

	/******For fb notify area********/
	struct work_struct     speed_up_work;               /*using for speedup resume*/
	/*using for touchpanel speedup resume wq*/
	struct workqueue_struct *speedup_resume_wq;
#if IS_ENABLED(CONFIG_DRM_OPLUS_PANEL_NOTIFY)
	struct drm_panel *active_panel;
	struct notifier_block fb_notif; /*register to control suspend/resume*/
#elif IS_ENABLED(CONFIG_DRM_PANEL_NOTIFY)
	struct drm_panel *active_panel;
	void *notifier_cookie;
#elif IS_ENABLED(CONFIG_OPLUS_MTK_DRM_GKI_NOTIFY)
	struct notifier_block disp_notifier;
#elif IS_ENABLED(CONFIG_DRM_MSM) || IS_ENABLED(CONFIG_DRM_OPLUS_NOTIFY) || IS_ENABLED(CONFIG_FB)
	struct notifier_block fb_notif;	/*register to control suspend/resume*/
#endif


	/******For usb or headset notify area********/
	bool is_headset_checked;                            /*state of headset for usb*/
	int cur_headset_state;                                /*current state of headset for usb*/

	bool is_usb_checked;                                /*state of charger for tp*/
	int cur_usb_state;                                    /*current state of charger for usb*/
	bool is_wireless_checked;                           /*state of wireless charger*/

	struct workqueue_struct *charger_pump_wq;           /*state of charger or usb*/
	struct work_struct     charger_pump_work;           /*state of charger or usb*/
	struct workqueue_struct
		*key_trigger_wq;            /*state of volume_key trigger*/
	struct work_struct
		key_trigger_work;            /*state of volume_key trigger*/
	struct workqueue_struct *headset_pump_wq;           /*state of headset or usb*/
	struct work_struct     headset_pump_work;           /*state of headset or usb*/
	/******For debug apk area********/
#ifdef CONFIG_OPLUS_TP_APK
	APK_OPERATION *apk_op;
	APK_SWITCH_TYPE type_now;
	APK_DATA_TYPE data_now;
	u8 *log_buf;
	u8 *gesture_buf;
	bool gesture_debug_sta;
#endif
	/******For QOS area********/
#ifdef CONFIG_TOUCHIRQ_UPDATE_QOS
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
	struct dev_pm_qos_request pm_qos_req;
#else
	struct pm_qos_request pm_qos_req;
#endif
	int pm_qos_value;
	int pm_qos_state;
#endif
	/*******i2c spi bus data*****/
	struct interface_data interface_data;
	/*******comon api data*****/
	struct com_api_data com_api_data;

	/******For aging test area********/
	int aging_test;
	int aging_mode;

	/******For smooth sensitive area********/
	bool smooth_level_array_support;
	bool sensitive_level_array_support;
	u32 smooth_level_array[SMOOTH_LEVEL_NUM];
	u32 smooth_level_charging_array[SMOOTH_LEVEL_NUM];
	u32 sensitive_level_array[SENSITIVE_LEVEL_NUM];
	u32 sensitive_level_charging_array[SENSITIVE_LEVEL_NUM];
	u32 *smooth_level_used_array;
	u32 *sensitive_level_used_array;
	u32 smooth_level_chosen;
	u32 sensitive_level_chosen;
	u32 smooth_level_default;
	u32 sensitive_level_default;


	/******For lcd fps area********/
	bool lcd_tp_refresh_support;                      /*lcd nofity tp refresh fps switch*/
	int lcd_fps;                                      /*save lcd refresh*/
	struct work_struct
		tp_refresh_work;            /*using for tp_refresh resume*/
	struct workqueue_struct *tp_refresh_wq;            /*using for tp_refresh wq*/
	bool enable_point_auto_change;
	struct miscdevice misc_device;
	bool misc_opened;
	struct message_list *msg_list;
	int ioc_init_size;
	char *ioc_init_buf;
	u8 en_touch_event_helper;
};

#ifdef CONFIG_OPLUS_TP_APK
void log_buf_write(struct touchpanel_data *ts, u8 value);
#endif

struct engineer_test_operations {
	int (*black_screen_test)(struct black_gesture_test *p,
				 struct touchpanel_data *ts);                 /*message of black gesture test*/
	int (*auto_test)(struct seq_file *s,
			 struct touchpanel_data *ts);         /*message of auto test*/
};

struct oplus_touchpanel_operations {
	int (*get_chip_info)(void *chip_data);	/*return 0:success;other:failed*/
	int (*mode_switch)(void *chip_data, work_mode mode,
			   int flag); /*return 0:success;other:failed*/
	int (*get_touch_points)(void *chip_data, struct point_info *points,
				int max_num);   /*return point bit-map*/
	int (*get_gesture_info)(void *chip_data,
				struct gesture_info *gesture);             /*return 0:success;other:failed*/
	int (*ftm_process)(void	   *chip_data);               /*ftm boot mode process*/
	void (*ftm_process_extra)(void *chip_data);
	int (*get_vendor)(void *chip_data,
			  struct panel_info
			  *panel_data);  /*distingush which panel we use, (TRULY/OFLIM/BIEL/TPK)*/

	int (*reset)(void *chip_data);  /*Reset Touchpanel*/
	int (*reinit_device)(void *chip_data);
	fw_check_state(*fw_check)(void *chip_data,
				  struct resolution_info *resolution_info,
				  struct panel_info *panel_data); /*return < 0 :failed; 0 sucess*/

	/*return 0 normal; return -1:update failed;*/
	fw_update_state(*fw_update)(void *chip_data, const struct firmware *fw,
				    bool force);
	/*return 0:success;other:abnormal, need to jump out*/
	int (*power_control)(void *chip_data, bool enable);

	int (*reset_gpio_control)(void *chip_data, bool enable); /*used for reset gpio*/
	u32(*trigger_reason)(void *chip_data, int gesture_enable, int is_suspended);
	u8(*get_keycode)(void *chip_data); /*get touch-key code*/
	int (*esd_handle)(void *chip_data);
	int (*fw_handle)(void *chip_data);	/*return 0 normal; return -1:update failed;*/
	void (*exit_esd_mode)(void *chip_data);  /*add for s4322 exit esd mode*/
	void (*register_info_read)(void *chip_data, uint16_t register_addr,
				   uint8_t *result, uint8_t length);    /*add for read registers*/
	/*some ic need opearation if resume prepare*/
	int (*speed_up_resume_prepare)(void *chip_data);
	/*some ic need specific opearation in resuming*/
	int (*specific_resume_operate)(void *chip_data,
				       struct specific_resume_data *p_resume_data);
	/*some ic need opearation if resume timed out*/
	void (*resume_timedout_operate)(void *chip_data);

	int (*get_face_state)(void *chip_info);  /*get face detect state*/
	void (*health_report)(void *chip_data,
			      struct monitor_data *mon_data); /*data logger get*/
	void (*enable_fingerprint)(void *chip_data, uint32_t enable);
	void (*enable_gesture_mask)(void *chip_data, uint32_t enable);
	void (*set_touch_direction)(void *chip_data, uint8_t dir);
	uint8_t (*get_touch_direction)(void *chip_data);
	/*get gesture info of fingerprint underscreen when screen on*/
	void (*screenon_fingerprint_info)(void *chip_data,
					  struct fp_underscreen_info *fp_tpinfo);

	void (*freq_hop_trigger)(void *chip_data); /*trigger frequency-hopping*/
	void (*set_noise_modetest)(void *chip_data, bool enable);
	uint8_t (*get_noise_modetest)(void *chip_data);
	/*If the tp ic need do something, use this!*/
	void (*tp_queue_work_prepare)(void *chip_data);

	bool (*tp_irq_throw_away)(void *chip_data);
	void (*rate_white_list_ctrl)(void *chip_data, int value);
	int (*smooth_lv_set)(void *chip_data, int level);
	int (*sensitive_lv_set)(void *chip_data, int level);
	int (*send_temperature)(void *chip_data, int value, bool status);
	int (*tp_refresh_switch)(void *chip_data, int fps);
	void (*set_gesture_state)(void *chip_data, int state);
	int (*get_touch_points_auto)(void *chip_data,
				     struct point_info *points,
				     int max_num,
				     struct resolution_info *resolution_info); /*return point bit-map auto*/

	int (*get_gesture_info_auto)(void *chip_data,
				     struct gesture_info *gesture,
				     struct resolution_info *resolution_info); /*return 0:success;other:failed*/

	void (*screenon_fingerprint_info_auto)(void *chip_data,
					       struct fp_underscreen_info *fp_tpinfo,
					       struct resolution_info
					       *resolution_info); /*get gesture info of fingerprint underscreen when screen on*/
	void (*calibrate)(struct seq_file *s, void *chip_data);
	bool (*get_cal_status)(struct seq_file *s, void *chip_data);
	int (*get_touch_points_help)(void *chip_data,
				     struct point_info *points,
				     int max_num,
				     struct resolution_info *resolution_info); /*return point bit-map auto*/
	int (*set_high_frame_rate)(void *chip_data, int value, int time);
};

struct aging_test_proc_operations {
	void (*start_aging_test)(void *chip_data);
	void (*finish_aging_test)(void *chip_data);
};

struct debug_info_proc_operations {
	void (*delta_read)(struct seq_file *s, void *chip_data);
	void (*key_trigger_delta_read)(void *chip_data);
	void (*self_delta_read)(struct seq_file *s, void *chip_data);
	void (*self_raw_read)(struct seq_file *s, void *chip_data);
	void (*baseline_read)(struct seq_file *s, void *chip_data);
	void (*baseline_blackscreen_read)(struct seq_file *s, void *chip_data);
	void (*main_register_read)(struct seq_file *s, void *chip_data);
	void (*reserve_read)(struct seq_file *s, void *chip_data);
	void (*abs_doze_read)(struct seq_file *s, void *chip_data);
	void (*reserve1)(struct seq_file *s, void *chip_data);
	void (*reserve2)(struct seq_file *s, void *chip_data);
	void (*reserve3)(struct seq_file *s, void *chip_data);
	void (*reserve4)(struct seq_file *s, void *chip_data);
	void (*get_delta_data)(void *chip_data, int32_t *deltadata);
	void (*delta_snr_read)(struct seq_file *s, void *chip_data, uint32_t count);
};

/*********PART3:function or variables for other files**********************/
struct touchpanel_data *common_touch_data_alloc(void);

int  common_touch_data_free(struct touchpanel_data *pdata);
int  register_common_touch_device(struct touchpanel_data *pdata);
void  unregister_common_touch_device(struct touchpanel_data *pdata);
void tp_shutdown(struct touchpanel_data *ts);
void tp_pm_suspend(struct touchpanel_data *ts);
void tp_pm_resume(struct touchpanel_data *ts);

int  tp_powercontrol_vddi(struct hw_resource *hw_res, bool on);
int  tp_powercontrol_avdd(struct hw_resource *hw_res, bool on);

void operate_mode_switch(struct touchpanel_data *ts);
void input_report_key_oplus(struct touchpanel_data *ts, unsigned int code,
			    int value);
void esd_handle_switch(struct esd_information *esd_info, bool on);
void clear_view_touchdown_flag(unsigned int tp_index);
void tp_touch_btnkey_release(unsigned int tp_index);
extern int tp_util_get_vendor(struct hw_resource *hw_res,
			      struct panel_info *panel_data);
extern bool tp_judge_ic_match(char *tp_ic_name);
extern int tp_judge_ic_match_commandline(struct panel_info *panel_data);

extern int request_firmware_select(const struct firmware **firmware_p,
				   const char *name, struct device *device);
bool is_oem_unlocked(void);
int  get_oem_verified_boot_state(void);

#ifndef CONFIG_TOUCHPANEL_NOTIFY
int opticalfp_irq_handler(struct fp_underscreen_info *fp_tpinfo);
#endif

#endif /*_TOUCHPANEL_COMMON_H_*/
