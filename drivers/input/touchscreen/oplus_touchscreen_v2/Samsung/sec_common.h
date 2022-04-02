/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef SEC_H
#define SEC_H

/*********PART1:Head files**********************/
#include <linux/firmware.h>
#include <linux/rtc.h>
#include <linux/syscalls.h>
#include <linux/timer.h>
#include <linux/time.h>
#include <linux/of_gpio.h>
#include <linux/delay.h>

#include "../touchpanel_common.h"
#include "../touch_comon_api/touch_comon_api.h"
#include "../touchpanel_autotest/touchpanel_autotest.h"


#define I2C_BURSTMAX                        (256)
#define GRIP_PARAMETER_LEN                  (128)
/*********PART2:Define Area**********************/
/*********PART3:Struct Area**********************/
struct sec_proc_operations {
	void (*calibrate)(struct seq_file *s, void *chip_data);
	void (*verify_calibration)(struct seq_file *s, void *chip_data);
	bool (*get_cal_status)(struct seq_file *s, void *chip_data);
	void (*set_curved_rejsize)(void *chip_data, uint8_t range_size);
	uint8_t (*get_curved_rejsize)(void *chip_data);
	void (*set_grip_handle)(void *chip_data, int para_num, char *buf);
};

enum {
	TYPE_ERROR          = 0x00,
	TYPE_TEST1            = 0x01,
	TYPE_TEST2            = 0x02,
	TYPE_TEST3            = 0x03,
	TYPE_TEST4            = 0x04,
	TYPE_TEST5            = 0x05,
	TYPE_TEST_MAX    = 0xFF,
};

struct sec_auto_test_operations {
	int (*test1)(struct seq_file *s, void *chip_data,
		     struct auto_testdata *sec_testdata,
		     struct test_item_info *p_test_item_info);
	int (*test2)(struct seq_file *s, void *chip_data,
		     struct auto_testdata *sec_testdata,
		     struct test_item_info *p_test_item_info);
	int (*test3)(struct seq_file *s, void *chip_data,
		     struct auto_testdata *sec_testdata,
		     struct test_item_info *p_test_item_info);
	int (*test4)(struct seq_file *s, void *chip_data,
		     struct auto_testdata *sec_testdata,
		     struct test_item_info *p_test_item_info);
	int (*test5)(struct seq_file *s, void *chip_data,
		     struct auto_testdata *sec_testdata,
		     struct test_item_info *p_test_item_info);
	int (*sec_get_verify_result)(void *chip_data);
	int (*sec_auto_test_disable_irq)(void *chip_data, bool enable);
	int (*sec_auto_test_preoperation)(struct seq_file *s, void *chip_data,
					  struct auto_testdata *sec_testdata,
					  struct test_item_info *p_test_item_info);
	int (*sec_auto_test_endoperation)(struct seq_file *s, void *chip_data,
					  struct auto_testdata *sec_testdata,
					  struct test_item_info *p_test_item_info);
};
/*********PART4:function declare*****************/
int sec_create_proc(struct touchpanel_data *ts,
		    struct sec_proc_operations *sec_ops);
int sec_remove_proc(struct touchpanel_data *ts);

void sec_limit_read(struct seq_file *s, struct touchpanel_data *ts);
void sec_raw_device_init(struct touchpanel_data *ts);
void sec_raw_device_release(struct touchpanel_data *ts);
int sec_auto_test(struct seq_file *s,  struct touchpanel_data *ts);

#endif  /*SEC_H*/
