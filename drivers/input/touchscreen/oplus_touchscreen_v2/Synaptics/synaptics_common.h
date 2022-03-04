/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef SYNAPTICS_H
#define SYNAPTICS_H
#define CONFIG_SYNAPTIC_RED

/*********PART1:Head files**********************/
#include <linux/firmware.h>
#include <linux/rtc.h>
#include <linux/syscalls.h>
#include <linux/timer.h>
#include <linux/time.h>
#include <linux/proc_fs.h>
#include <linux/of_gpio.h>
#include <linux/delay.h>

#include "../touchpanel_common.h"
#include "../touch_comon_api/touch_comon_api.h"
#include "../touchpanel_autotest/touchpanel_autotest.h"


#include "synaptics_firmware_v2.h"

/*********PART2:Define Area**********************/
#define SYNAPTICS_RMI4_PRODUCT_ID_SIZE 10
#define SYNAPTICS_RMI4_PRODUCT_INFO_SIZE 2

#define DIAGONAL_UPPER_LIMIT  1100
#define DIAGONAL_LOWER_LIMIT  900

#define MAX_RESERVE_SIZE 4
#define MAX_LIMIT_NAME_SIZE 16

/*********PART3:Struct Area**********************/
typedef enum {
	BASE_NEGATIVE_FINGER = 0x02,
	BASE_MUTUAL_SELF_CAP = 0x04,
	BASE_ENERGY_RATIO = 0x08,
	BASE_RXABS_BASELINE = 0x10,
	BASE_TXABS_BASELINE = 0x20,
} BASELINE_ERR;

typedef enum {
	SHIELD_PALM = 0x01,
	SHIELD_GRIP = 0x02,
	SHIELD_METAL = 0x04,
	SHIELD_MOISTURE = 0x08,
	SHIELD_ESD = 0x10,
} SHIELD_MODE;

typedef enum {
	RST_HARD = 0x01,
	RST_INST = 0x02,
	RST_PARITY = 0x04,
	RST_WD = 0x08,
	RST_OTHER = 0x10,
} RESET_REASON;

struct health_info {
	uint16_t grip_count;
	uint16_t grip_x;
	uint16_t grip_y;
	uint16_t freq_scan_count;
	uint16_t baseline_err;
	uint16_t curr_freq;
	uint16_t noise_state;
	uint16_t cid_im;
	uint16_t shield_mode;
	uint16_t reset_reason;
};

struct excep_count {
	uint16_t grip_count;
	/*baseline error type*/
	uint16_t neg_finger_count;
	uint16_t cap_incons_count;
	uint16_t energy_ratio_count;
	uint16_t rx_baseline_count;
	uint16_t tx_baseline_count;
	/*noise status*/
	uint16_t noise_count;
	/*shield report fingers*/
	uint16_t shield_palm_count;
	uint16_t shield_edge_count;
	uint16_t shield_metal_count;
	uint16_t shield_water_count;
	uint16_t shield_esd_count;
	/*exception reset count*/
	uint16_t hard_rst_count;
	uint16_t inst_rst_count;
	uint16_t parity_rst_count;
	uint16_t wd_rst_count;
	uint16_t other_rst_count;
};

struct image_header {
	/* 0x00 - 0x0f */
	unsigned char checksum[4];
	unsigned char reserved_04;
	unsigned char reserved_05;
	unsigned char options_firmware_id: 1;
	unsigned char options_contain_bootloader: 1;
	/* only available in s4322 , reserved in other, begin*/
	unsigned char options_guest_code: 1;
	unsigned char options_tddi: 1;
	unsigned char options_reserved: 4;
	/* only available in s4322 , reserved in other ,  end*/
	unsigned char bootloader_version;
	unsigned char firmware_size[4];
	unsigned char config_size[4];
	/* 0x10 - 0x1f */
	unsigned char product_id[SYNAPTICS_RMI4_PRODUCT_ID_SIZE];
	unsigned char package_id[2];
	unsigned char package_id_revision[2];
	unsigned char product_info[SYNAPTICS_RMI4_PRODUCT_INFO_SIZE];
	/* 0x20 - 0x2f */
	/* only available in s4322 , reserved in other, begin*/
	unsigned char bootloader_addr[4];
	unsigned char bootloader_size[4];
	unsigned char ui_addr[4];
	unsigned char ui_size[4];
	/* only available in s4322 , reserved in other ,  end*/
	/* 0x30 - 0x3f */
	unsigned char ds_id[16];
	/* 0x40 - 0x4f */
	/* only available in s4322 , reserved in other, begin*/
	union {
		struct {
			unsigned char dsp_cfg_addr[4];
			unsigned char dsp_cfg_size[4];
			unsigned char reserved_48_4f[8];
		};
	};
	/* only available in s4322 , reserved in other ,  end*/
	/* 0x50 - 0x53 */
	unsigned char firmware_id[4];
};

struct image_header_data {
	bool contains_firmware_id;
	unsigned int firmware_id;
	unsigned int checksum;
	unsigned int firmware_size;
	unsigned int config_size;
	/* only available in s4322 , reserved in other, begin*/
	unsigned int disp_config_offset;
	unsigned int disp_config_size;
	unsigned int bootloader_offset;
	unsigned int bootloader_size;
	/* only available in s4322 , reserved in other ,  end*/
	unsigned char bootloader_version;
	unsigned char product_id[SYNAPTICS_RMI4_PRODUCT_ID_SIZE + 1];
	unsigned char product_info[SYNAPTICS_RMI4_PRODUCT_INFO_SIZE];
};

struct limit_block {
	char name[MAX_LIMIT_NAME_SIZE];
	int mode;
	int reserve[MAX_RESERVE_SIZE]; /*16*/
	int size;
	int16_t data;
};

/*test item for syna oncell ic*/
enum {
	TYPE_ERROR          = 0x00,
	TYPE_TEST1            = 0x01,/*no cbc*/
	TYPE_TEST2            = 0x02,/*with cbc*/
	TYPE_TEST3            = 0x03,
	TYPE_TEST4            = 0x04,
	TYPE_TEST5            = 0x05,
	TYPE_TEST6            = 0x06,
	TYPE_TEST7            = 0x07,
	TYPE_TEST8            = 0x08,
	TYPE_TEST9            = 0x09,
	TYPE_TEST10          = 0x0a,
	TYPE_TEST11          = 0x0b,
	TYPE_TEST12          = 0x0c,
	TYPE_RT_MAX           = 0xFF,
};

struct synaptics_proc_operations {
	void (*set_touchfilter_state)(void *chip_data, uint8_t range_size);
	uint8_t (*get_touchfilter_state)(void *chip_data);
};

struct syna_auto_test_operations {
	int (*test1)(struct seq_file *s, void *chip_data,
		     struct auto_testdata *syna_testdata,
		     struct test_item_info *p_test_item_info);
	int (*test2)(struct seq_file *s, void *chip_data,
		     struct auto_testdata *syna_testdata,
		     struct test_item_info *p_test_item_info);
	int (*test3)(struct seq_file *s, void *chip_data,
		     struct auto_testdata *syna_testdata,
		     struct test_item_info *p_test_item_info);
	int (*test4)(struct seq_file *s, void *chip_data,
		     struct auto_testdata *syna_testdata,
		     struct test_item_info *p_test_item_info);
	int (*test5)(struct seq_file *s, void *chip_data,
		     struct auto_testdata *syna_testdata,
		     struct test_item_info *p_test_item_info);
	int (*test6)(struct seq_file *s, void *chip_data,
		     struct auto_testdata *syna_testdata,
		     struct test_item_info *p_test_item_info);
	int (*test7)(struct seq_file *s, void *chip_data,
		     struct auto_testdata *syna_testdata,
		     struct test_item_info *p_test_item_info);
	int (*test8)(struct seq_file *s, void *chip_data,
		     struct auto_testdata *syna_testdata,
		     struct test_item_info *p_test_item_info);
	int (*test9)(struct seq_file *s, void *chip_data,
		     struct auto_testdata *syna_testdata,
		     struct test_item_info *p_test_item_info);
	int (*test10)(struct seq_file *s, void *chip_data,
		      struct auto_testdata *syna_testdata,
		      struct test_item_info *p_test_item_info);
	int (*test11)(struct seq_file *s, void *chip_data,
		      struct auto_testdata *syna_testdata,
		      struct test_item_info *p_test_item_info);
	int (*syna_auto_test_enable_irq)(void *chip_data, bool enable);
	int (*syna_auto_test_preoperation)(struct seq_file *s, void *chip_data,
					   struct auto_testdata *syna_testdata,
					   struct test_item_info *p_test_item_info);
	int (*syna_auto_test_endoperation)(struct seq_file *s, void *chip_data,
					   struct auto_testdata *syna_testdata,
					   struct test_item_info *p_test_item_info);
};

int  synaptics_create_proc(struct touchpanel_data *ts,
			   struct synaptics_proc_operations *syna_ops);
int synaptics_remove_proc(struct touchpanel_data *ts,
			  struct synaptics_proc_operations *syna_ops);
void synaptics_parse_header(struct image_header_data *header,
			    const unsigned char *fw_image);
int synaptics_parse_header_v2(struct image_info *image_info,
			      const unsigned char *fw_image);
int synaptics_auto_test(struct seq_file *s,  struct touchpanel_data *ts);

#endif  /*SYNAPTICS_H*/
