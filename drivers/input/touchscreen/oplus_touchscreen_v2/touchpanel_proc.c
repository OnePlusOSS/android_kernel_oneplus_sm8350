// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include <linux/types.h>
#include <linux/proc_fs.h>
#include <linux/input/mt.h>
#include <linux/input.h>
#include <linux/gpio.h>
#include <linux/seq_file.h>
#include <linux/delay.h>
#include <asm/uaccess.h>
#include <linux/interrupt.h>
#include <linux/rtc.h>
#include <linux/syscalls.h>
#include <linux/version.h>

#include "touchpanel_common.h"
#include "touchpanel_autotest/touchpanel_autotest.h"
#include "touch_comon_api/touch_comon_api.h"


#ifndef CONFIG_REMOVE_OPLUS_FUNCTION
#ifdef CONFIG_TOUCHPANEL_MTK_PLATFORM
#include<mt-plat/mtk_boot_common.h>
#else
#include <soc/oplus/system/boot_mode.h>
#endif
#endif

/*******Part1:LOG TAG Declear************************/

/*******Part2:declear Area********************************/
#ifndef CONFIG_REMOVE_OPLUS_FUNCTION
extern int register_devinfo(char *name, struct manufacture_info *info);
extern int register_device_proc(char *name, char *version, char *manufacture);
#endif

extern bool is_ftm_boot_mode(struct touchpanel_data *ts);
extern int cur_tp_index;

/*******Part3:Function node Function  Area********************/
/*oplus_optimized_time - For optimized time*/
static ssize_t proc_optimized_time_write(struct file *file,
		const char __user *buffer, size_t count, loff_t *ppos)
{
	int value = 0;
	char buf[5] = {0};
	struct touchpanel_data *ts = PDE_DATA(file_inode(file));

	if (!ts) {
		TPD_INFO("%s error:file_inode.\n", __func__);
		return count;
	}

	tp_copy_from_user(buf, sizeof(buf), buffer, count, 5);

	if (kstrtoint(buf, 10, &value)) {
		TPD_INFO("%s: kstrtoint error\n", __func__);
		return count;
	}

	if (0 == value) {
		ts->total_operate_times = 0;        /*clear total count*/
	}

	return count;
}

static ssize_t proc_optimized_time_read(struct file *file,
					char __user *user_buf, size_t count, loff_t *ppos)
{
	int ret = 0;
	char page[PAGESIZE] = {0};
	struct touchpanel_data *ts = PDE_DATA(file_inode(file));

	if (!ts) {
		snprintf(page, PAGESIZE - 1, "%d\n", -1); /*error handler*/

	} else {
		snprintf(page, PAGESIZE - 1, "%d\n",
			 ts->single_optimized_time * ts->total_operate_times);
	}

	ret = simple_read_from_buffer(user_buf, count, ppos, page, strlen(page));
	return ret;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
static const struct proc_ops proc_optimized_time_fops = {
	.proc_write = proc_optimized_time_write,
	.proc_read  = proc_optimized_time_read,
	.proc_open  = simple_open,
};
#else
static const struct file_operations proc_optimized_time_fops = {
	.write = proc_optimized_time_write,
	.read  = proc_optimized_time_read,
	.open  = simple_open,
	.owner = THIS_MODULE,
};
#endif

/*/proc/touchpanel/kernel_grip_default_para*/
static int tp_grip_default_para_read(struct seq_file *s, void *v)
{
	int ret = 0;
	struct touchpanel_data *ts = s->private;
	const struct firmware *fw = NULL;

	char *p_node = NULL;
	char *grip_config_name = NULL;
	char *postfix = "_sys_edge_touch_config";
	uint8_t copy_len = 0;

	TPD_INFO("%s:s->size:%d,s->count:%d\n", __func__, s->size,
		 s->count);

	if (!ts)
		return 0;

	if (s->size <= (PAGE_SIZE * 4)) {
		s->count = s->size;
		return 0;
	}

	grip_config_name = kzalloc(MAX_FW_NAME_LENGTH, GFP_KERNEL);

	if (grip_config_name == NULL) {
		TPD_INFO("grip_config_name kzalloc error!\n");
		return 0;
	}

	p_node = strstr(ts->panel_data.fw_name, ".");

	if (p_node == NULL) {
		TPD_INFO("p_node strstr error!\n");
		kfree(grip_config_name);
		return 0;
	}

	copy_len = p_node - ts->panel_data.fw_name;
	memcpy(grip_config_name, ts->panel_data.fw_name, copy_len);
	strlcat(grip_config_name, postfix, MAX_FW_NAME_LENGTH);
	strlcat(grip_config_name, p_node, MAX_FW_NAME_LENGTH);
	TPD_INFO("grip_config_name is %s\n", grip_config_name);

	ret = request_firmware(&fw, grip_config_name, ts->dev);

	if (ret < 0) {
		TPD_INFO("Request firmware failed - %s (%d)\n", grip_config_name, ret);
		seq_printf(s, "Request failed, Check the path %s\n", grip_config_name);
		kfree(grip_config_name);
		return 0;
	}

	TPD_INFO("%s Request ok,size is:%d\n", grip_config_name, fw->size);

	if (fw->size > 0) {
		seq_write(s, fw->data, fw->size);
		TPD_INFO("%s:seq_write data ok\n", __func__);
	}

	release_firmware(fw);
	kfree(grip_config_name);
	return ret;
}

static int tp_grip_default_para_open(struct inode *inode, struct file *file)
{
	return single_open(file, tp_grip_default_para_read, PDE_DATA(inode));
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
static const struct proc_ops tp_grip_default_para_fops = {
	.proc_open  = tp_grip_default_para_open,
	.proc_read  = seq_read,
	.proc_release = single_release,
};
#else
static const struct file_operations tp_grip_default_para_fops = {
	.owner = THIS_MODULE,
	.open  = tp_grip_default_para_open,
	.read  = seq_read,
	.release = single_release,
};
#endif

/*tp_index - For read current tp index
 * Output:
 * tp_index: current pointer tp touchpanel_data;
 */
static ssize_t proc_index_control_read(struct file *file, char __user *buffer,
				       size_t count, loff_t *ppos)
{
	uint8_t ret = 0;
	char page[PAGESIZE] = {0};

	TPD_INFO("%s: cur_tp_index = %d.\n", __func__, cur_tp_index);
	snprintf(page, PAGESIZE - 1, "%d\n", cur_tp_index);
	ret = simple_read_from_buffer(buffer, count, ppos, page, strlen(page));

	return ret;
}

/*tp_index - For set current tp index
 * Output:
 * tp_index: current pointer tp touchpanel_data;
 */
static ssize_t proc_index_control_write(struct file *file,
					const char __user *buffer, size_t count, loff_t *ppos)
{
	int tmp = 0;
	char buf[4] = {0};

	tp_copy_from_user(buf, sizeof(buf), buffer, count, 2);

	if (kstrtoint(buf, 10, &tmp)) {
		TPD_INFO("%s: kstrtoint error\n", __func__);
		return count;
	}

	cur_tp_index = tmp;
	return count;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
static const struct proc_ops proc_tp_index_ops = {
	.proc_write = proc_index_control_write,
	.proc_read  = proc_index_control_read,
	.proc_open  = simple_open,
};
#else
static const struct file_operations proc_tp_index_ops = {
	.write = proc_index_control_write,
	.read  = proc_index_control_read,
	.open  = simple_open,
	.owner = THIS_MODULE,
};
#endif

/*debug_level - For touch panel driver debug_level
 * Output:
 * tp_debug:0, LEVEL_BASIC;
 * tp_debug:1, LEVEL_DETAIL;
 * tp_debug:2, LEVEL_DEBUG;
 */
static ssize_t proc_debug_level_read(struct file *file, char __user *buffer,
				     size_t count, loff_t *ppos)
{
	uint8_t ret = 0;
	char page[PAGESIZE] = {0};

	TPD_INFO("%s: tp_debug = %d.\n", __func__, tp_debug);
	snprintf(page, PAGESIZE - 1, "%d", tp_debug);
	ret = simple_read_from_buffer(buffer, count, ppos, page, strlen(page));

	return ret;
}

/*debug_level - For touch panel driver debug_level
 * Input:
 * tp_debug:0, LEVEL_BASIC;
 * tp_debug:1, LEVEL_DETAIL;
 * tp_debug:2, LEVEL_DEBUG;
 */
static ssize_t proc_debug_level_write(struct file *file,
				      const char __user *buffer, size_t count, loff_t *ppos)
{
#ifdef CONFIG_OPLUS_TP_APK
	struct touchpanel_data *ts = PDE_DATA(file_inode(file));
#endif /* end of CONFIG_OPLUS_TP_APK*/

	int tmp = 0;
	char buf[4] = {0};

#ifdef CONFIG_OPLUS_TP_APK

	if (!ts)
		return count;

#endif /* end of CONFIG_OPLUS_TP_APK*/

	tp_copy_from_user(buf, sizeof(buf), buffer, count, 2);

	if (kstrtoint(buf, 10, &tmp)) {
		TPD_INFO("%s: kstrtoint error\n", __func__);
		return count;
	}

	tp_debug = tmp;
#ifdef CONFIG_OPLUS_TP_APK

	if (ts && ts->apk_op && ts->apk_op->apk_debug_set) {
		if (tp_debug == 0)
			ts->apk_op->apk_debug_set(ts->chip_data, false);

		else
			ts->apk_op->apk_debug_set(ts->chip_data, true);
	}

#endif /* end of CONFIG_OPLUS_TP_APK*/

	return count;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
static const struct proc_ops proc_debug_level_ops = {
	.proc_write = proc_debug_level_write,
	.proc_read  = proc_debug_level_read,
	.proc_open  = simple_open,
};
#else
static const struct file_operations proc_debug_level_ops = {
	.write = proc_debug_level_write,
	.read  = proc_debug_level_read,
	.open  = simple_open,
	.owner = THIS_MODULE,
};
#endif

/*double_tap_enable - For black screen gesture
 * Input:
 * gesture_enable = 0 : disable gesture
 * gesture_enable = 1 : enable gesture when ps is far away
 * gesture_enable = 2 : disable gesture when ps is near
 * gesture_enable = 3 : enable single tap gesture when ps is far away
 */
static ssize_t proc_gesture_control_write(struct file *file,
		const char __user *buffer, size_t count, loff_t *ppos)
{
	int value = 0;
	char buf[4] = {0};
	struct touchpanel_data *ts = PDE_DATA(file_inode(file));

	if (!ts)
		return count;

	tp_copy_from_user(buf, sizeof(buf), buffer, count, 2);

	if (kstrtoint(buf, 10, &value)) {
		TP_INFO(ts->tp_index, "%s: kstrtoint error\n", __func__);
		return count;
	}

	if (value > 3 || (ts->gesture_test_support && ts->gesture_test.flag))
		return count;

	mutex_lock(&ts->mutex);

	if (ts->gesture_enable != value) {
		ts->gesture_enable = value;
		TP_INFO(ts->tp_index, "%s: gesture_enable = %d, is_suspended = %d\n", __func__,
			ts->gesture_enable, ts->is_suspended);

		if (ts->is_incell_panel && (ts->suspend_state == TP_RESUME_EARLY_EVENT
					    || ts->disable_gesture_ctrl) && (ts->tp_resume_order == LCD_TP_RESUME)) {
			TP_INFO(ts->tp_index,
				"tp will resume, no need mode_switch in incell panel\n"); /*avoid i2c error or tp rst pulled down in lcd resume*/

		} else if (ts->is_suspended) {
			if (ts->fingerprint_underscreen_support && ts->fp_enable
					&& ts->ts_ops->enable_gesture_mask) {
				ts->ts_ops->enable_gesture_mask(ts->chip_data,
								(ts->gesture_enable & 0x01) == 1);
			} else
				operate_mode_switch(ts);
		}
	} else
		TP_INFO(ts->tp_index, "%s: do not do same operator :%d\n", __func__, value);

	mutex_unlock(&ts->mutex);

	return count;
}

/*double_tap_enable - For black screen gesture
 * Output:
 * gesture_enable = 0 : disable gesture
 * gesture_enable = 1 : enable gesture when ps is far away
 * gesture_enable = 2 : disable gesture when ps is near
 */
static ssize_t proc_gesture_control_read(struct file *file, char __user *buffer,
		size_t count, loff_t *ppos)
{
	int ret = 0;
	char page[PAGESIZE] = {0};
	struct touchpanel_data *ts = PDE_DATA(file_inode(file));

	if (!ts)
		return 0;

	TP_DEBUG(ts->tp_index, "double tap enable is: %d\n", ts->gesture_enable);
	ret = snprintf(page, PAGESIZE - 1, "%d\n", ts->gesture_enable);
	ret = simple_read_from_buffer(buffer, count, ppos, page, strlen(page));

	return ret;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
static const struct proc_ops proc_gesture_control_fops = {
	.proc_write = proc_gesture_control_write,
	.proc_read  = proc_gesture_control_read,
	.proc_open  = simple_open,
};
#else
static const struct file_operations proc_gesture_control_fops = {
	.write = proc_gesture_control_write,
	.read  = proc_gesture_control_read,
	.open  = simple_open,
	.owner = THIS_MODULE,
};
#endif

/*
 *    each bit cant enable or disable each gesture
 *    bit0: 1 for enable bit gesture, 0 for disable bit gesture
 *    bit1: 1 for enable bit gesture, 0 for disable bit gesture
 *    bit2: 1 for enable bit gesture, 0 for disable bit gesture
 */
static ssize_t proc_gesture_control_indep_write(struct file *file,
		const char __user *buffer, size_t count, loff_t *ppos)
{
	int value = 0;
	char buf[9] = {0};
	struct touchpanel_data *ts = PDE_DATA(file_inode(file));

	if (count > 8)
		return count;

	if (!ts)
		return count;

	if (copy_from_user(buf, buffer, count)) {
		TPD_INFO("%s: read proc input error.\n", __func__);
		return count;
	}

	sscanf(buf, "%d", &value);

	TPD_INFO("%s: value is %x.\n", __func__, value);

	mutex_lock(&ts->mutex);

	if (ts->ts_ops->set_gesture_state) {
		ts->gesture_enable_indep = value;
		ts->ts_ops->set_gesture_state(ts->chip_data, value);
	}

	mutex_unlock(&ts->mutex);

	return count;
}

static ssize_t proc_gesture_control_indep_read(struct file *file,
		char __user *user_buf, size_t count, loff_t *ppos)
{
	int ret = 0;
	char page[PAGESIZE] = {0};
	struct touchpanel_data *ts = PDE_DATA(file_inode(file));

	if (!ts)
		return 0;

	if (!ts->ts_ops)
		return 0;

	TPD_DEBUG("gesture gesture_enable_indep is: %x\n", ts->gesture_enable_indep);
	ret = snprintf(page, PAGESIZE - 1, "%x\n", ts->gesture_enable_indep);
	ret = simple_read_from_buffer(user_buf, count, ppos, page, strlen(page));

	return ret;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
static const struct proc_ops proc_gesture_control_indep_fops = {
	.proc_write = proc_gesture_control_indep_write,
	.proc_read  = proc_gesture_control_indep_read,
	.proc_open  = simple_open,
};
#else
static const struct file_operations proc_gesture_control_indep_fops = {
	.write = proc_gesture_control_indep_write,
	.read  = proc_gesture_control_indep_read,
	.open  = simple_open,
	.owner = THIS_MODULE,
};
#endif


/*coordinate - For black screen gesture coordinate*/
static ssize_t proc_coordinate_read(struct file *file, char __user *buffer,
				    size_t count, loff_t *ppos)
{
	int ret = 0;
	char page[PAGESIZE] = {0};
	struct touchpanel_data *ts = PDE_DATA(file_inode(file));

	if (!ts)
		return 0;

	TP_DEBUG(ts->tp_index, "%s:gesture_type = %d\n", __func__,
		 ts->gesture.gesture_type);

	ret = snprintf(page, PAGESIZE - 1,
		       "%d,%d:%d,%d:%d,%d:%d,%d:%d,%d:%d,%d:%d,%d\n", ts->gesture.gesture_type,
		       ts->gesture.Point_start.x, ts->gesture.Point_start.y, ts->gesture.Point_end.x,
		       ts->gesture.Point_end.y,
		       ts->gesture.Point_1st.x,   ts->gesture.Point_1st.y,   ts->gesture.Point_2nd.x,
		       ts->gesture.Point_2nd.y,
		       ts->gesture.Point_3rd.x,   ts->gesture.Point_3rd.y,   ts->gesture.Point_4th.x,
		       ts->gesture.Point_4th.y,
		       ts->gesture.clockwise);
	ret = simple_read_from_buffer(buffer, count, ppos, page, strlen(page));

	return ret;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
static const struct proc_ops proc_coordinate_fops = {
	.proc_read  = proc_coordinate_read,
	.proc_open  = simple_open,
};
#else
static const struct file_operations proc_coordinate_fops = {
	.read  = proc_coordinate_read,
	.open  = simple_open,
	.owner = THIS_MODULE,
};
#endif

#define HIGH_FRAME_RATE "high_frame_rate"
/*game_switch_enable
 * Input:
 * noise_level:0, disable the game_switch;
 * noise_level:a, enable the game_switch;
 */
static ssize_t proc_game_switch_write(struct file *file,
				      const char __user *buffer, size_t count, loff_t *ppos)
{
	int value = 0;
	char buf[100] = {0};
	char *ptr = NULL;
	struct touchpanel_data *ts = PDE_DATA(file_inode(file));

	if (!ts) {
		TPD_INFO("%s: ts is NULL\n", __func__);
		return count;
	}

	if (!ts->ts_ops->mode_switch) {
		TPD_INFO("%s:not support ts_ops->mode_switch callback\n", __func__);
		return count;
	}

	tp_copy_from_user(buf, sizeof(buf), buffer, count, 100);
	ptr = strstr(buf, HIGH_FRAME_RATE);

	if (ptr) {
		if (sscanf(ptr + sizeof(HIGH_FRAME_RATE), "%d%d", &value,
				&ts->high_frame_rate_time)) {
			if (ts->ts_ops->set_high_frame_rate && ts->high_frame_rate_support) {
				mutex_lock(&ts->mutex);
				ts->high_frame_value = value;
				ts->ts_ops->set_high_frame_rate(ts->chip_data, value, ts->high_frame_rate_time);
				mutex_unlock(&ts->mutex);
			}
		}
	}

	if (kstrtoint(buf, 16, &value)) {
		TP_INFO(ts->tp_index, "%s: kstrtoint error\n", __func__);
		return count;
	}

	ts->noise_level = value;

	if (ts->health_monitor_support)
		ts->monitor_data.in_game_mode = value;

	TP_INFO(ts->tp_index, "%s: game_switch value=0x%x\n", __func__, value);

	if (!ts->is_suspended) {
		mutex_lock(&ts->mutex);
		ts->ts_ops->mode_switch(ts->chip_data, MODE_GAME, value > 0);

		if (value > 0) {
			if (!ts->smooth_level_array_support && ts->ts_ops->smooth_lv_set)
				ts->ts_ops->smooth_lv_set(ts->chip_data, ts->smooth_level_default);

			if (!ts->sensitive_level_array_support && ts->ts_ops->sensitive_lv_set)
				ts->ts_ops->sensitive_lv_set(ts->chip_data, ts->sensitive_level_default);

		} else {
			if (!ts->smooth_level_array_support && ts->ts_ops->smooth_lv_set)
				ts->ts_ops->smooth_lv_set(ts->chip_data, 0);

			if (!ts->sensitive_level_array_support && ts->ts_ops->sensitive_lv_set)
				ts->ts_ops->sensitive_lv_set(ts->chip_data, 0);
		}
		mutex_unlock(&ts->mutex);
	} else
		TP_INFO(ts->tp_index, "%s: game_switch_support is_suspended.\n", __func__);
	return count;
}

static ssize_t proc_game_switch_read(struct file *file, char __user *buffer,
				     size_t count, loff_t *ppos)
{
	int ret = 0;
	char page[PAGESIZE] = {0};
	struct touchpanel_data *ts = PDE_DATA(file_inode(file));

	if (!ts) {
		snprintf(page, PAGESIZE - 1, "%d\n", -1); /*no support*/

	} else {
		snprintf(page, PAGESIZE - 100,
			 "%d, high_frame_support:%d, high_frame_value:%d\n",
			 ts->noise_level, ts->high_frame_rate_support, ts->high_frame_value); /*support*/
	}

	ret = simple_read_from_buffer(buffer, count, ppos, page, strlen(page));
	return ret;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
static const struct proc_ops proc_game_switch_fops = {
	.proc_write = proc_game_switch_write,
	.proc_read  = proc_game_switch_read,
	.proc_open  = simple_open,
};
#else
static const struct file_operations proc_game_switch_fops = {
	.write = proc_game_switch_write,
	.read  = proc_game_switch_read,
	.open  = simple_open,
	.owner = THIS_MODULE,
};
#endif

/*irq_depth - For enable or disable irq
 * Output:
 * irq depth;
 * irq gpio state;
 */
static ssize_t proc_get_irq_depth_read(struct file *file, char __user *buffer,
				       size_t count, loff_t *ppos)
{
	int ret = 0;
	char page[PAGESIZE] = {0};
	struct touchpanel_data *ts = PDE_DATA(file_inode(file));
	struct irq_desc *desc = NULL;

	if (!ts)
		return 0;

	desc = irq_to_desc(ts->irq);

	snprintf(page, PAGESIZE - 1, "depth:%d, state:%d\n", desc->depth,
		 gpio_get_value(ts->hw_res.irq_gpio));
	ret = simple_read_from_buffer(buffer, count, ppos, page, strlen(page));
	return ret;
}
/*irq_depth - For enable or disable irq
 * Input:
 * value:1, enable_irq;
 * value:other, disable_irq_nosync;
 */
static ssize_t proc_irq_status_write(struct file *file,
				     const char __user *buffer, size_t count, loff_t *ppos)
{
	int value = 0;
	char buf[4] = {0};
	struct touchpanel_data *ts = PDE_DATA(file_inode(file));

	if (!ts)
		return count;

	tp_copy_from_user(buf, sizeof(buf), buffer, count, 2);

	if (kstrtoint(buf, 10, &value)) {
		TP_INFO(ts->tp_index, "%s: kstrtoint error\n", __func__);
		return count;
	}

	TP_INFO(ts->tp_index, "%s %d, %s ts->irq=%d\n", __func__, value,
		value ? "enable" : "disable", ts->irq);

	if (value == 1)
		enable_irq(ts->irq);

	else
		disable_irq_nosync(ts->irq);

	return count;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
static const struct proc_ops proc_get_irq_depth_fops = {
	.proc_open  = simple_open,
	.proc_read  = proc_get_irq_depth_read,
	.proc_write = proc_irq_status_write,
};
#else
static const struct file_operations proc_get_irq_depth_fops = {
	.owner = THIS_MODULE,
	.open  = simple_open,
	.read  = proc_get_irq_depth_read,
	.write = proc_irq_status_write,
};
#endif

static ssize_t proc_noise_modetest_read(struct file *file, char __user *buffer,
					size_t count, loff_t *ppos)
{
	ssize_t ret = 0;
	char page[PAGESIZE] = {0};
	struct touchpanel_data *ts = PDE_DATA(file_inode(file));

	if (!ts || !ts->ts_ops->get_noise_modetest)
		return 0;

	snprintf(page, PAGESIZE - 1, "%d\n",
		 ts->ts_ops->get_noise_modetest(ts->chip_data));
	ret = simple_read_from_buffer(buffer, count, ppos, page, strlen(page));

	return ret;
}

static ssize_t proc_noise_modetest_write(struct file *file,
		const char __user *buffer, size_t count, loff_t *ppos)
{
	char buf[8] = {0};
	int temp = 0;
	struct touchpanel_data *ts = PDE_DATA(file_inode(file));

	if (!ts)
		return count;

	tp_copy_from_user(buf, sizeof(buf), buffer, count, 2);

	if (kstrtoint(buf, 10, &temp)) {
		TP_INFO(ts->tp_index, "%s: kstrtoint error\n", __func__);
		return count;
	}

	TPD_DETAIL("%s: value = %d\n", __func__, temp);

	mutex_lock(&ts->mutex);

	if (ts->ts_ops->set_noise_modetest)
		ts->ts_ops->set_noise_modetest(ts->chip_data, temp);

	mutex_unlock(&ts->mutex);

	return count;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
static const struct proc_ops proc_noise_modetest_fops = {
	.proc_read  = proc_noise_modetest_read,
	.proc_write = proc_noise_modetest_write,
	.proc_open  = simple_open,
};
#else
static const struct file_operations proc_noise_modetest_fops = {
	.read  = proc_noise_modetest_read,
	.write = proc_noise_modetest_write,
	.open  = simple_open,
	.owner = THIS_MODULE,
};
#endif

/*tp_fw_update - For touch panel fw update
 * Input:
 * firmware_update_type:0, fw update;
 * firmware_update_type:1, fore fw update;
 * firmware_update_type:2, app fw update;
 */
static ssize_t proc_fw_update_write(struct file *file,
				    const char __user *buffer, size_t count, loff_t *ppos)
{
	struct touchpanel_data *ts = PDE_DATA(file_inode(file));
	int val = 0;
	int ret = 0;
	char buf[4] = {0};

	if (!ts)
		return count;

#ifndef REMOVE_OPLUS_FUNCTION
#ifdef CONFIG_TOUCHPANEL_MTK_PLATFORM
	if (ts->boot_mode == KERNEL_POWER_OFF_CHARGING_BOOT) {
		TP_INFO(ts->tp_index,
			"boot mode is MSM_BOOT_MODE__CHARGE,not need update tp firmware\n");
		return count;
	}
#else
	if (ts->boot_mode == MSM_BOOT_MODE__CHARGE) {
		TP_INFO(ts->tp_index,
			"boot mode is MSM_BOOT_MODE__CHARGE,not need update tp firmware\n");
		return count;
	}
#endif
#endif
	tp_copy_from_user(buf, sizeof(buf), buffer, count, 2);

	if (kstrtoint(buf, 10, &val)) {
		TP_INFO(ts->tp_index, "%s: kstrtoint error\n", __func__);
		return count;
	}

	ts->firmware_update_type = val;

	if (!ts->force_update && ts->firmware_update_type != 2)
		ts->force_update = !!val;

	schedule_work(&ts->fw_update_work);

	ret = wait_for_completion_killable_timeout(&ts->fw_complete,
			FW_UPDATE_COMPLETE_TIMEOUT);

	if (ret < 0)
		TP_INFO(ts->tp_index, "kill signal interrupt\n");

#ifdef CONFIG_TOUCHIRQ_UPDATE_QOS

	if (!ts->pm_qos_state) {
		ts->pm_qos_value = PM_QOS_DEFAULT_VALUE;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
		dev_pm_qos_add_request(ts->dev, &ts->pm_qos_req, DEV_PM_QOS_RESUME_LATENCY,
				       ts->pm_qos_value);
#else
		pm_qos_add_request(&ts->pm_qos_req, PM_QOS_CPU_DMA_LATENCY, ts->pm_qos_value);
#endif
		TP_INFO(ts->tp_index, "add qos request in touch driver.\n");
		ts->pm_qos_state = 1;
	}

#endif

	TP_INFO(ts->tp_index, "fw update finished\n");
	return count;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
static const struct proc_ops proc_fw_update_ops = {
	.proc_write = proc_fw_update_write,
	.proc_open = simple_open,
};
#else
static const struct file_operations proc_fw_update_ops = {
	.write = proc_fw_update_write,
	.open = simple_open,
	.owner = THIS_MODULE,
};
#endif

/*oplus_register_info - For i2c debug way
 * Output:
 *first choose register_add and lenght, example: echo 000e,1 > oplus_register_info
 *second read: cat oplus_register_info
 */
static ssize_t proc_register_info_read(struct file *file, char __user *buffer,
				       size_t count, loff_t *ppos)
{
	int ret = 0;
	int i = 0;
	ssize_t num_read_chars = 0;
	char page[PAGESIZE] = {0};
	struct touchpanel_data *ts = PDE_DATA(file_inode(file));

	if (!ts)
		return 0;

	if (ts->reg_info.reg_length < 1 || ts->reg_info.reg_length > 9) {
		TP_INFO(ts->tp_index, "ts->reg_info.reg_length error!\n");
		return 0;
	}

	ts->reg_info.reg_result = kzalloc(ts->reg_info.reg_length * (sizeof(uint16_t)),
					  GFP_KERNEL);

	if (!ts->reg_info.reg_result) {
		TP_INFO(ts->tp_index, "ts->reg_info.reg_result kzalloc error\n");
		return 0;
	}

	if (ts->ts_ops->register_info_read) {
		mutex_lock(&ts->mutex);
		ts->ts_ops->register_info_read(ts->chip_data, ts->reg_info.reg_addr,
					       ts->reg_info.reg_result, ts->reg_info.reg_length);
		mutex_unlock(&ts->mutex);

		for (i = 0; i < ts->reg_info.reg_length; i++) {
			num_read_chars += snprintf(&(page[num_read_chars]), PAGESIZE - 1,
						   "reg_addr(0x%x) = 0x%x\n", ts->reg_info.reg_addr, ts->reg_info.reg_result[i]);
		}

		ret = simple_read_from_buffer(buffer, count, ppos, page, strlen(page));
	}

	tp_kfree((void **)&ts->reg_info.reg_result);
	return ret;
}

/*oplus_register_info - For i2c debug way
 * Input:
 *first choose register_add and lenght, example: echo 000e,1 > oplus_register_info
 *second read: cat oplus_register_info
 */
static ssize_t proc_register_info_write(struct file *file,
					const char __user *buffer, size_t count, loff_t *ppos)
{
	int addr = 0, length = 0;
	char buf[16] = {0};
	struct touchpanel_data *ts = PDE_DATA(file_inode(file));

	if (!ts) {
		TPD_INFO("ts not exist!\n");
		return count;
	}

	tp_copy_from_user(buf, sizeof(buf), buffer, count, 7);

	sscanf(buf, "%x,%d", (uint32_t *)&addr, &length);
	ts->reg_info.reg_addr = (uint16_t)addr;
	ts->reg_info.reg_length = (uint16_t)length;
	TP_INFO(ts->tp_index,
		"ts->reg_info.reg_addr = 0x%x, ts->reg_info.reg_lenght = %d\n",
		ts->reg_info.reg_addr, ts->reg_info.reg_length);

	return count;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
static const struct proc_ops proc_register_info_fops = {
	.proc_open  = simple_open,
	.proc_read  = proc_register_info_read,
	.proc_write = proc_register_info_write,
};
#else
static const struct file_operations proc_register_info_fops = {
	.owner = THIS_MODULE,
	.open  = simple_open,
	.read  = proc_register_info_read,
	.write = proc_register_info_write,
};
#endif

static ssize_t proc_incell_panel_info_read(struct file *file,
		char __user *buffer, size_t count, loff_t *ppos)
{
	uint8_t ret = 0;
	char page[PAGESIZE] = {0};
	struct touchpanel_data *ts = PDE_DATA(file_inode(file));

	snprintf(page, PAGESIZE - 1, "%d", ts->is_incell_panel);
	ret = simple_read_from_buffer(buffer, count, ppos, page, strlen(page));

	return ret;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
static const struct proc_ops proc_incell_panel_fops = {
	.proc_open = simple_open,
	.proc_read = proc_incell_panel_info_read,
};
#else
static const struct file_operations proc_incell_panel_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = proc_incell_panel_info_read,
};
#endif

/*fd_enable - For touch panel face detect
 * Input:
 * fd_enable:0, enable face detect;
 * fd_enable:1, disable face detect;
 */
static ssize_t proc_fd_enable_write(struct file *file,
				    const char __user *buffer, size_t count, loff_t *ppos)
{
	int value = 0;
	char buf[4] = {0};
	struct touchpanel_data *ts = PDE_DATA(file_inode(file));

	if (!ts)
		return count;

	tp_copy_from_user(buf, sizeof(buf), buffer, count, 2);

	if (kstrtoint(buf, 10, &value)) {
		TP_INFO(ts->tp_index, "%s: kstrtoint error\n", __func__);
		return count;
	}

	if (value > 2)
		return count;

	TP_DEBUG(ts->tp_index, "%s value: %d, es_enable :%d\n", __func__, value,
		 ts->fd_enable);

	if (value == ts->fd_enable)
		return count;

	mutex_lock(&ts->mutex);
	ts->fd_enable = value;

	if (!ts->is_suspended && (ts->suspend_state == TP_SPEEDUP_RESUME_COMPLETE)) {
		if (ts->fd_enable) {
			input_event(ts->ps_input_dev, EV_MSC, MSC_RAW, 0);
			input_sync(ts->ps_input_dev);
		}

		ts->ts_ops->mode_switch(ts->chip_data, MODE_FACE_DETECT, ts->fd_enable == 1);
	}

	mutex_unlock(&ts->mutex);

	return count;
}

/*fd_enable - For touch panel face detect
 * Output:
 * fd_enable:0, enable face detect;
 * fd_enable:1, disable face detect;
 */
static ssize_t proc_fd_enable_read(struct file *file, char __user *buffer,
				   size_t count, loff_t *ppos)
{
	int ret = 0;
	char page[PAGESIZE] = {0};
	struct touchpanel_data *ts = PDE_DATA(file_inode(file));

	if (!ts)
		return 0;

	TP_DEBUG(ts->tp_index, "%s value: %d\n", __func__, ts->fd_enable);
	ret = snprintf(page, PAGESIZE - 1, "%d\n", ts->fd_enable);
	ret = simple_read_from_buffer(buffer, count, ppos, page, strlen(page));

	return ret;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
static const struct proc_ops tp_fd_enable_fops = {
	.proc_write = proc_fd_enable_write,
	.proc_read  = proc_fd_enable_read,
	.proc_open  = simple_open,
};
#else
static const struct file_operations tp_fd_enable_fops = {
	.write = proc_fd_enable_write,
	.read  = proc_fd_enable_read,
	.open  = simple_open,
	.owner = THIS_MODULE,
};
#endif

/*event_num - For touch panel face detect input num
 * Output:
 * face detect input num;
 */
static ssize_t proc_event_num_read(struct file *file, char __user *buffer,
				   size_t count, loff_t *ppos)
{
	int ret = 0;
	const char *devname = NULL;
	struct input_handle *handle;
	struct touchpanel_data *ts = PDE_DATA(file_inode(file));

	if (!ts)
		return 0;

	list_for_each_entry(handle, &ts->ps_input_dev->h_list, d_node) {
		if (strncmp(handle->name, "event", 5) == 0) {
			devname = handle->name;
			break;
		}
	}

	if (devname)
		ret = simple_read_from_buffer(buffer, count, ppos, devname, strlen(devname));

	return ret;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
static const struct proc_ops tp_event_num_fops = {
	.proc_read  = proc_event_num_read,
	.proc_open  = simple_open,
};
#else
static const struct file_operations tp_event_num_fops = {
	.read  = proc_event_num_read,
	.open  = simple_open,
	.owner = THIS_MODULE,
};
#endif

/*touch_count - For face detect touch count
 * Output:
 * touch_count:0;
 * touch_count:1-F;
 */
static ssize_t proc_fd_touch_read(struct file *file, char __user *buffer,
				  size_t count, loff_t *ppos)
{
	int ret = 0;
	char page[PAGESIZE] = {0};
	struct touchpanel_data *ts = PDE_DATA(file_inode(file));

	if (!ts)
		return 0;

	TP_DEBUG(ts->tp_index, "%s value: %d\n", __func__, ts->touch_count);
	/*mutex_lock(&ts->mutex);*/
	ret = snprintf(page, PAGESIZE - 1, "%d\n", ts->touch_count & 0x0F);
	/*mutex_unlock(&ts->mutex);*/
	ret = simple_read_from_buffer(buffer, count, ppos, page, strlen(page));

	return ret;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
static const struct proc_ops fd_touch_num_fops = {
	.proc_read  = proc_fd_touch_read,
	.proc_open  = simple_open,
};
#else
static const struct file_operations fd_touch_num_fops = {
	.read  = proc_fd_touch_read,
	.open  = simple_open,
	.owner = THIS_MODULE,
};
#endif

/*fp_enable - For touch panel triger fingerprint
 * Input:
 * fp_enable:0, enable;
 * fp_enable:1, disable;
 */
static ssize_t proc_fp_enable_write(struct file *file,
				    const char __user *buffer, size_t count, loff_t *ppos)
{
	int value = 0;
	char buf[4] = {0};
	struct touchpanel_data *ts = PDE_DATA(file_inode(file));

	if (!ts)
		return count;

	tp_copy_from_user(buf, sizeof(buf), buffer, count, 2);

	if (kstrtoint(buf, 10, &value)) {
		TP_INFO(ts->tp_index, "%s: kstrtoint error\n", __func__);
		return count;
	}

	if (value > 2)
		return count;

	TPD_DETAIL("%s value: %d, fp_enable :%d\n", __func__, value, ts->fp_enable);

	if (value == ts->fp_enable)
		return count;

	mutex_lock(&ts->mutex);
	ts->fp_enable = value;

	if (!ts->fp_enable) {
		ts->fp_info.touch_state = 0;        /*reset touch state*/
	}

	if (!ts->is_suspended && (ts->suspend_state == TP_SPEEDUP_RESUME_COMPLETE))
		ts->ts_ops->enable_fingerprint(ts->chip_data, !!ts->fp_enable);

	else if (ts->is_suspended) {
		if (ts->black_gesture_support && (1 == (ts->gesture_enable & 0x01)))
			ts->ts_ops->enable_fingerprint(ts->chip_data, !!ts->fp_enable);

		else
			operate_mode_switch(ts);
	}

	mutex_unlock(&ts->mutex);

	return count;
}

/*fp_enable - For touch panel triger fingerprint
 * Output:
 * fp_enable:0, enable;
 * fp_enable:1, disable;
 */
static ssize_t proc_fp_enable_read(struct file *file, char __user *buffer,
				   size_t count, loff_t *ppos)
{
	int ret = 0;
	char page[PAGESIZE] = {0};
	struct touchpanel_data *ts = PDE_DATA(file_inode(file));

	if (!ts)
		return 0;

	TP_DEBUG(ts->tp_index, "%s value: %d\n", __func__, ts->fp_enable);
	ret = snprintf(page, PAGESIZE - 1, "%d\n", ts->fp_enable);
	ret = simple_read_from_buffer(buffer, count, ppos, page, strlen(page));

	return ret;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
static const struct proc_ops tp_fp_enable_fops = {
	.proc_write = proc_fp_enable_write,
	.proc_read  = proc_fp_enable_read,
	.proc_open  = simple_open,
};
#else
static const struct file_operations tp_fp_enable_fops = {
	.write = proc_fp_enable_write,
	.read  = proc_fp_enable_read,
	.open  = simple_open,
	.owner = THIS_MODULE,
};
#endif

/*proc/touchpanel/baseline_test*/
static int tp_auto_test_read_func(struct seq_file *s, void *v)
{
	int ret = 0;

	ret = tp_auto_test(s, v);
	return ret;
}

static int baseline_autotest_open(struct inode *inode, struct file *file)
{
	return single_open(file, tp_auto_test_read_func, PDE_DATA(inode));
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
static const struct proc_ops tp_auto_test_proc_fops = {
	.proc_open  = baseline_autotest_open,
	.proc_read  = seq_read,
	.proc_release = single_release,
};
#else
static const struct file_operations tp_auto_test_proc_fops = {
	.owner = THIS_MODULE,
	.open  = baseline_autotest_open,
	.read  = seq_read,
	.release = single_release,
};
#endif

/*black_screen_test - For incell ic black screen test*/
static ssize_t proc_black_screen_test_read(struct file *file,
		char __user *buffer, size_t count, loff_t *ppos)
{
	int ret = 0;

	ret = tp_black_screen_test(file, buffer, count, ppos);
	return ret;
}

static ssize_t proc_black_screen_test_write(struct file *file,
		const char __user *buffer, size_t count, loff_t *ppos)
{
	int value = 0;
	char buf[4] = {0};
	struct touchpanel_data *ts = PDE_DATA(file_inode(file));

	if (!ts)
		return count;

	tp_copy_from_user(buf, sizeof(buf), buffer, count, 2);

	if (kstrtoint(buf, 10, &value)) {
		TP_INFO(ts->tp_index, "%s: kstrtoint error\n", __func__);
		return count;
	}

	TP_INFO(ts->tp_index, "%s %d\n", __func__, value);

	ts->gesture_test.gesture_backup = ts->gesture_enable;
	ts->gesture_enable = true;
	ts->gesture_test.flag = !!value;

	return count;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
static const struct proc_ops proc_black_screen_test_fops = {
	.proc_open  = simple_open,
	.proc_read  = proc_black_screen_test_read,
	.proc_write = proc_black_screen_test_write,
};
#else
static const struct file_operations proc_black_screen_test_fops = {
	.owner = THIS_MODULE,
	.open  = simple_open,
	.read  = proc_black_screen_test_read,
	.write = proc_black_screen_test_write,
};
#endif

/*baseline_result - For GKI auto test result*/
static int tp_auto_test_result_read(struct seq_file *s, void *v)
{
	int ret = 0;

	ret = tp_auto_test_result(s, v);
	return ret;
}

static int tp_auto_test_result_open(struct inode *inode, struct file *file)
{
	return single_open(file, tp_auto_test_result_read, PDE_DATA(inode));
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
static const struct proc_ops tp_auto_test_result_fops = {
	.proc_open  = tp_auto_test_result_open,
	.proc_read  = seq_read,
	.proc_release = single_release,
};
#else
static const struct file_operations tp_auto_test_result_fops = {
	.owner = THIS_MODULE,
	.open  = tp_auto_test_result_open,
	.read  = seq_read,
	.release = single_release,
};
#endif

/*baseline_result - For GKI auto test result*/
static int tp_black_screen_result_read(struct seq_file *s, void *v)
{
	int ret = 0;

	ret = tp_black_screen_result(s, v);
	return ret;
}

static int tp_black_screen_result_open(struct inode *inode, struct file *file)
{
	return single_open(file, tp_black_screen_result_read, PDE_DATA(inode));
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
static const struct proc_ops proc_black_screen_result_fops = {
	.proc_open  = tp_black_screen_result_open,
	.proc_read  = seq_read,
	.proc_release = single_release,
};
#else
static const struct file_operations proc_black_screen_result_fops = {
	.owner = THIS_MODULE,
	.open  = tp_black_screen_result_open,
	.read  = seq_read,
	.release = single_release,
};
#endif

/*limit_enable - For touch panel direction
 * Output:
 * limit_enable:0, VERTICAL_SCREEN;
 * limit_enable:1, LANDSCAPE_SCREEN_90;
 * limit_enable:2, LANDSCAPE_SCREEN_270;
 */
static ssize_t proc_dir_control_read(struct file *file, char __user *user_buf,
				     size_t count, loff_t *ppos)
{
	ssize_t ret = 0;
	char page[PAGESIZE] = {0};
	struct touchpanel_data *ts = PDE_DATA(file_inode(file));

	if (!ts)
		return 0;

	TPD_INFO("limit_enable is: 0x%x\n", ts->limit_enable);
	ret = snprintf(page, PAGESIZE - 1, "%d\n", ts->limit_enable);

	ret = simple_read_from_buffer(user_buf, count, ppos, page, strlen(page));

	return ret;
}

static ssize_t proc_dir_control_write(struct file *file,
				      const char __user *buffer, size_t count, loff_t *ppos)
{
	char buf[8] = {0};
	int temp = 0;
	int ret = 0;
	struct touchpanel_data *ts = PDE_DATA(file_inode(file));

	if (!ts)
		return count;

	if (count > 2)
		return count;

	tp_copy_from_user(buf, sizeof(buf), buffer, count, 2);

	if (kstrtoint(buf, 10, &temp)) {
		TP_INFO(ts->tp_index, "%s: kstrtoint error\n", __func__);
		return count;
	}

	mutex_lock(&ts->mutex);

	ts->limit_enable = temp;
	TPD_INFO("%s: limit_enable = 0x%x\n", __func__, ts->limit_enable);

	if (ts->is_suspended == 0) {
		ret = ts->ts_ops->mode_switch(ts->chip_data, MODE_EDGE, ts->limit_enable);

		if (ret < 0)
			TPD_INFO("%s, Touchpanel operate mode switch failed\n", __func__);
	}


	mutex_unlock(&ts->mutex);

	return count;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
static const struct proc_ops touch_dir_proc_fops = {
	.proc_read  = proc_dir_control_read,
	.proc_write = proc_dir_control_write,
	.proc_open  = simple_open,
};
#else
static const struct file_operations touch_dir_proc_fops = {
	.read  = proc_dir_control_read,
	.write = proc_dir_control_write,
	.open  = simple_open,
	.owner = THIS_MODULE,
};
#endif

static ssize_t proc_rate_white_list_write(struct file *file,
		const char __user *buffer, size_t count, loff_t *ppos)
{
	int value = 0;
	char buf[4] = {0};
	struct touchpanel_data *ts = PDE_DATA(file_inode(file));

	if (count > 4) {
		TPD_INFO("%s:count > 4\n", __func__);
		return count;
	}

	if (!ts) {
		TPD_INFO("%s: ts is NULL\n", __func__);
		return count;
	}

	if (!ts->ts_ops->rate_white_list_ctrl) {
		TPD_INFO("%s:not support ts_ops->rate_white_list_ctrl callback\n", __func__);
		return count;
	}

	tp_copy_from_user(buf, sizeof(buf), buffer, count, 2);

	if (kstrtoint(buf, 10, &value)) {
		TP_INFO(ts->tp_index, "%s: kstrtoint error\n", __func__);
		return count;
	}

	mutex_lock(&ts->mutex);

	ts->rate_ctrl_level = value;

	TPD_INFO("%s: write value=%d\n", __func__, value);

	if (!ts->is_suspended)
		ts->ts_ops->rate_white_list_ctrl(ts->chip_data, value);

	else
		TPD_INFO("%s: TP is_suspended.\n", __func__);

	mutex_unlock(&ts->mutex);

	return count;
}

static ssize_t proc_rate_white_list_read(struct file *file,
		char __user *user_buf, size_t count, loff_t *ppos)
{
	int ret = 0;
	char page[PAGESIZE] = {0};
	struct touchpanel_data *ts = PDE_DATA(file_inode(file));

	if (!ts) {
		snprintf(page, PAGESIZE - 1, "%d\n", -1); /*no support*/

	} else {
		snprintf(page, PAGESIZE - 1, "%d\n", ts->rate_ctrl_level); /*support*/
	}

	ret = simple_read_from_buffer(user_buf, count, ppos, page, strlen(page));
	return ret;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
static const struct proc_ops proc_rate_white_list_fops = {
	.proc_write = proc_rate_white_list_write,
	.proc_read  = proc_rate_white_list_read,
	.proc_open  = simple_open,
};
#else
static const struct file_operations proc_rate_white_list_fops = {
	.write = proc_rate_white_list_write,
	.read  = proc_rate_white_list_read,
	.open  = simple_open,
	.owner = THIS_MODULE,
};
#endif

static ssize_t proc_switch_usb_state_write(struct file *file,
		const char __user *buffer, size_t count, loff_t *ppos)
{
	int usb_state = 0;
	char buf[4] = {0};
	struct touchpanel_data *ts = PDE_DATA(file_inode(file));

	if (count > 4) {
		TPD_INFO("%s:count > 4\n", __func__);
		return count;
	}

	if (!ts) {
		TPD_INFO("%s: ts is NULL\n", __func__);
		return count;
	}

	tp_copy_from_user(buf, sizeof(buf), buffer, count, 2);

	if (kstrtoint(buf, 10, &usb_state)) {
		TP_INFO(ts->tp_index, "%s: kstrtoint error\n", __func__);
		return count;
	}

	if (is_ftm_boot_mode(ts)) {
		TP_INFO(ts->tp_index, "Ftm mode, do not switch usb state\n");
		return count;
	}

	ts->cur_usb_state = usb_state;


	queue_work(ts->charger_pump_wq, &ts->charger_pump_work);

	return count;
}

static ssize_t proc_switch_usb_state_read(struct file *file,
		char __user *user_buf, size_t count, loff_t *ppos)
{
	int ret = 0;
	char page[PAGESIZE] = {0};
	struct touchpanel_data *ts = PDE_DATA(file_inode(file));

	if (!ts) {
		snprintf(page, PAGESIZE - 1, "%d\n", -1); /*no support*/

	} else {
		snprintf(page, PAGESIZE - 1, "%d\n", ts->is_usb_checked); /*support*/
	}

	ret = simple_read_from_buffer(user_buf, count, ppos, page, strlen(page));
	return ret;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
static const struct proc_ops proc_switch_usb_state_fops = {
	.proc_write = proc_switch_usb_state_write,
	.proc_read  = proc_switch_usb_state_read,
	.proc_open  = simple_open,
};
#else
static const struct file_operations proc_switch_usb_state_fops = {
	.write = proc_switch_usb_state_write,
	.read  = proc_switch_usb_state_read,
	.open  = simple_open,
	.owner = THIS_MODULE,
};
#endif

static ssize_t proc_wireless_charge_detect_write(struct file *file,
		const char __user *buffer, size_t count, loff_t *ppos)
{
	int wireless_state = 0;
	char buf[4] = {0};
	struct touchpanel_data *ts = PDE_DATA(file_inode(file));

	if (count > 4) {
		TPD_INFO("%s:count > 4\n", __func__);
		return count;
	}

	if (!ts) {
		TPD_INFO("%s: ts is NULL\n", __func__);
		return count;
	}

	tp_copy_from_user(buf, sizeof(buf), buffer, count, 2);

	if (kstrtoint(buf, 10, &wireless_state)) {
		TP_INFO(ts->tp_index, "%s: kstrtoint error\n", __func__);
		return count;
	}

	if (is_ftm_boot_mode(ts)) {
		TPD_INFO("Ftm mode, do not switch wireless state\n");
		return count;
	}


	if (ts->wireless_charger_support
			&& (ts->is_wireless_checked != wireless_state)) {
		ts->is_wireless_checked = !!wireless_state;
		TPD_INFO("%s: check wireless state : %d, is_suspended: %d\n", __func__,
			 wireless_state, ts->is_suspended);

		if (!ts->is_suspended && (ts->suspend_state == TP_SPEEDUP_RESUME_COMPLETE)
				&& !ts->loading_fw) {
			mutex_lock(&ts->mutex);
			ts->ts_ops->mode_switch(ts->chip_data, MODE_WIRELESS_CHARGE,
						ts->is_wireless_checked);
			mutex_unlock(&ts->mutex);
		}
	}

	return count;
}

static ssize_t proc_wireless_charge_detect_read(struct file *file,
		char __user *user_buf, size_t count, loff_t *ppos)
{
	int ret = 0;
	char page[PAGESIZE] = {0};
	struct touchpanel_data *ts = PDE_DATA(file_inode(file));

	if (!ts) {
		snprintf(page, PAGESIZE - 1, "%d\n", -1); /*no support*/

	} else {
		snprintf(page, PAGESIZE - 1, "%d\n", ts->is_wireless_checked); /*support*/
	}

	ret = simple_read_from_buffer(user_buf, count, ppos, page, strlen(page));
	return ret;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
static const struct proc_ops proc_wireless_charge_detect_fops = {
	.proc_write = proc_wireless_charge_detect_write,
	.proc_read  = proc_wireless_charge_detect_read,
	.proc_open  = simple_open,
};
#else
static const struct file_operations proc_wireless_charge_detect_fops = {
	.write = proc_wireless_charge_detect_write,
	.read  = proc_wireless_charge_detect_read,
	.open  = simple_open,
	.owner = THIS_MODULE,
};
#endif

static ssize_t proc_headset_detect_write(struct file *file,
		const char __user *buffer, size_t count, loff_t *ppos)
{
	int headset_state = 0;
	char buf[4] = {0};
	struct touchpanel_data *ts = PDE_DATA(file_inode(file));

	if (count > 4) {
		TPD_INFO("%s:count > 4\n", __func__);
		return count;
	}

	if (!ts) {
		TPD_INFO("%s: ts is NULL\n", __func__);
		return count;
	}

	tp_copy_from_user(buf, sizeof(buf), buffer, count, 2);

	if (kstrtoint(buf, 10, &headset_state)) {
		TP_INFO(ts->tp_index, "%s: kstrtoint error\n", __func__);
		return count;
	}

	if (is_ftm_boot_mode(ts)) {
		TP_INFO(ts->tp_index, "Ftm mode, do not switch usb state\n");
		return count;
	}

	ts->is_headset_checked = headset_state;

	queue_work(ts->headset_pump_wq, &ts->headset_pump_work);

	return count;
}

static ssize_t proc_headset_detect_read(struct file *file,
					char __user *user_buf, size_t count, loff_t *ppos)
{
	int ret = 0;
	char page[PAGESIZE] = {0};
	struct touchpanel_data *ts = PDE_DATA(file_inode(file));

	if (!ts) {
		snprintf(page, PAGESIZE - 1, "%d\n", -1); /*no support*/

	} else {
		snprintf(page, PAGESIZE - 1, "%d\n", ts->is_headset_checked); /*support*/
	}

	ret = simple_read_from_buffer(user_buf, count, ppos, page, strlen(page));
	return ret;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
static const struct proc_ops proc_headset_detect_fops = {
	.proc_write = proc_headset_detect_write,
	.proc_read  = proc_headset_detect_read,
	.proc_open  = simple_open,
};
#else
static const struct file_operations proc_headset_detect_fops = {
	.write = proc_headset_detect_write,
	.read  = proc_headset_detect_read,
	.open  = simple_open,
	.owner = THIS_MODULE,
};
#endif


static ssize_t proc_aging_test_read(struct file *file, char __user *user_buf,
				    size_t count, loff_t *ppos)
{
	struct touchpanel_data *ts = PDE_DATA(file_inode(file));
	uint8_t ret = 0;
	char page[PAGESIZE] = {0};

	if (!ts)
		return 0;

	if (ts->aging_test_ops) {
		if ((ts->aging_test_ops->start_aging_test)
				&& (ts->aging_test_ops->finish_aging_test))
			ret = 1;
	}

	if (!ret)
		TPD_INFO("%s:no support aging_test2\n", __func__);

	ret = snprintf(page, PAGESIZE - 1, "%d, %d\n", ret, ts->aging_test);
	ret = simple_read_from_buffer(user_buf, count, ppos, page, strlen(page));
	return ret;
}

/*tp_aging_test - For aging test mode
 * Output:
 * tp_aging_test = 0 : disable active
 * tp_aging_test = 1 : enable active
 * tp_aging_test = 2 : enable normal test
 * tp_aging_test = 3 : enable aging test
 */
static ssize_t proc_aging_test_write(struct file *file,
				     const char __user *buffer, size_t count, loff_t *ppos)
{
	struct touchpanel_data *ts = PDE_DATA(file_inode(file));
	int val = 0;
	char buf[4] = {0};

	if (!ts)
		return count;

	if (!ts->aging_test_ops)
		return count;

	if ((!ts->aging_test_ops->start_aging_test)
			|| (!ts->aging_test_ops->finish_aging_test))
		return count;

	if (count > 2)
		return count;

	tp_copy_from_user(buf, sizeof(buf), buffer, count, 2);

	if (kstrtoint(buf, 10, &val)) {
		TP_INFO(ts->tp_index, "%s: kstrtoint error\n", __func__);
		return count;
	}

	TP_INFO(ts->tp_index, "%s:val:%d\n", __func__, val);

	mutex_lock(&ts->mutex);

	if (AGING_TEST_MODE == val || NORMAL_TEST_MODE == val) {
		ts->aging_mode = val;
		goto EXIT;
	}

	ts->aging_test = !!val;

	if (ts->aging_test)
		ts->aging_test_ops->start_aging_test(ts->chip_data);

	else
		ts->aging_test_ops->finish_aging_test(ts->chip_data);

EXIT:
	mutex_unlock(&ts->mutex);

	return count;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
static const struct proc_ops proc_aging_test_ops = {
	.proc_write = proc_aging_test_write,
	.proc_read  = proc_aging_test_read,
	.proc_open = simple_open,
};
#else
static const struct file_operations proc_aging_test_ops = {
	.write = proc_aging_test_write,
	.read  = proc_aging_test_read,
	.open = simple_open,
	.owner = THIS_MODULE,
};
#endif

static ssize_t proc_smooth_level_write(struct file *file,
				       const char __user *buffer, size_t count, loff_t *ppos)
{
	int value = 0, raw_level = 0;
	char buf[5] = {0};
	struct touchpanel_data *ts = PDE_DATA(file_inode(file));

	if (count > 5) {
		TPD_INFO("%s:count > 5\n", __func__);
		return count;
	}

	if (!ts) {
		TPD_INFO("%s: ts is NULL\n", __func__);
		return count;
	}

	if (!ts->ts_ops->smooth_lv_set) {
		TPD_INFO("%s:not support ts_ops->smooth_lv_set callback\n", __func__);
		return count;
	}

	tp_copy_from_user(buf, sizeof(buf), buffer, count, 5);

	if (kstrtoint(buf, 10, &value)) {
		TP_INFO(ts->tp_index, "%s: kstrtoint error\n", __func__);
		return count;
	}

	mutex_lock(&ts->mutex);

	if (ts->smooth_level_array_support) {
		if (value < 0)
			raw_level = -value;

		else {
			if (value < SMOOTH_LEVEL_NUM)
				ts->smooth_level_chosen = value;

			else
				ts->smooth_level_chosen = SMOOTH_LEVEL_NUM - 1;

			if (ts->health_monitor_support && ts->smooth_level_chosen)
				ts->monitor_data.smooth_level_chosen = ts->smooth_level_chosen;

			raw_level = ts->smooth_level_used_array[ts->smooth_level_chosen];
		}

		TPD_INFO("%s: level=%d value=%d\n", __func__, ts->smooth_level_chosen,
			 raw_level);
	}

	if (!ts->is_suspended)
		ts->ts_ops->smooth_lv_set(ts->chip_data, raw_level);

	else
		TPD_INFO("%s: TP is_suspended.\n", __func__);

	mutex_unlock(&ts->mutex);

	return count;
}

static ssize_t proc_smooth_level_read(struct file *file, char __user *user_buf,
				      size_t count, loff_t *ppos)
{
	int ret = 0;
	char page[PAGESIZE] = {0};
	struct touchpanel_data *ts = PDE_DATA(file_inode(file));

	if (ts && ts->smooth_level_array_support)
		snprintf(page, PAGESIZE - 1, "%d\n", ts->smooth_level_chosen);

	else
		snprintf(page, PAGESIZE - 1, "%d\n", -1);

	ret = simple_read_from_buffer(user_buf, count, ppos, page, strlen(page));
	return ret;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
static const struct proc_ops proc_smooth_level_fops = {
	.proc_write = proc_smooth_level_write,
	.proc_read  = proc_smooth_level_read,
	.proc_open  = simple_open,
};
#else
static const struct file_operations proc_smooth_level_fops = {
	.write = proc_smooth_level_write,
	.read  = proc_smooth_level_read,
	.open  = simple_open,
	.owner = THIS_MODULE,
};
#endif

static ssize_t proc_sensitive_level_write(struct file *file,
		const char __user *buffer, size_t count, loff_t *ppos)
{
	int value = 0, raw_level = 0;
	char buf[5] = {0};
	struct touchpanel_data *ts = PDE_DATA(file_inode(file));

	if (count > 5) {
		TPD_INFO("%s:count > 5\n", __func__);
		return count;
	}

	if (!ts) {
		TPD_INFO("%s: ts is NULL\n", __func__);
		return count;
	}

	if (!ts->ts_ops->sensitive_lv_set) {
		TPD_INFO("%s:not support ts_ops->sensitive_lv_set callback\n", __func__);
		return count;
	}

	tp_copy_from_user(buf, sizeof(buf), buffer, count, 5);

	if (kstrtoint(buf, 10, &value)) {
		TP_INFO(ts->tp_index, "%s: kstrtoint error\n", __func__);
		return count;
	}

	mutex_lock(&ts->mutex);

	if (value < 0)
		raw_level = -value;

	else {
		if (value < SENSITIVE_LEVEL_NUM)
			ts->sensitive_level_chosen = value;

		else
			ts->sensitive_level_chosen = SENSITIVE_LEVEL_NUM - 1;

		if (ts->health_monitor_support && ts->sensitive_level_chosen)
			ts->monitor_data.sensitive_level_chosen = ts->sensitive_level_chosen;

		raw_level = ts->sensitive_level_used_array[ts->sensitive_level_chosen];
	}

	TPD_INFO("%s: level=%d value=%d\n", __func__, ts->sensitive_level_chosen,
		 raw_level);

	if (!ts->is_suspended)
		ts->ts_ops->sensitive_lv_set(ts->chip_data, raw_level);

	else
		TPD_INFO("%s: TP is_suspended.\n", __func__);

	mutex_unlock(&ts->mutex);

	return count;
}

static ssize_t proc_sensitive_level_read(struct file *file,
		char __user *user_buf, size_t count, loff_t *ppos)
{
	int ret = 0;
	char page[PAGESIZE] = {0};
	struct touchpanel_data *ts = PDE_DATA(file_inode(file));

	if (!ts || !ts->sensitive_level_array_support)
		snprintf(page, PAGESIZE - 1, "%d\n", -1);

	else
		snprintf(page, PAGESIZE - 1, "%d\n", ts->sensitive_level_chosen);

	ret = simple_read_from_buffer(user_buf, count, ppos, page, strlen(page));
	return ret;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
static const struct proc_ops proc_sensitive_level_fops = {
	.proc_write = proc_sensitive_level_write,
	.proc_read  = proc_sensitive_level_read,
	.proc_open  = simple_open,
};
#else
static const struct file_operations proc_sensitive_level_fops = {
	.write = proc_sensitive_level_write,
	.read  = proc_sensitive_level_read,
	.open  = simple_open,
	.owner = THIS_MODULE,
};
#endif

static int calibrate_fops_read_func(struct seq_file *s, void *v)
{
	struct touchpanel_data *ts = s->private;

	if (!ts->ts_ops->calibrate) {
		TPD_INFO("[TP]no calibration, need back virtual calibration result");
		seq_printf(s, "0 error, calibration and verify success\n");
		return 0;
	}

	disable_irq_nosync(ts->irq);
	mutex_lock(&ts->mutex);

	if (!ts->touch_count)
		ts->ts_ops->calibrate(s, ts->chip_data);

	else
		seq_printf(s, "1 error, release touch on the screen, now has %d touch\n",
			   ts->touch_count);

	mutex_unlock(&ts->mutex);
	enable_irq(ts->irq);

	return 0;
}

static int proc_calibrate_fops_open(struct inode *inode, struct file *file)
{
	return single_open(file, calibrate_fops_read_func, PDE_DATA(inode));
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
static const struct proc_ops proc_calibrate_fops = {
	.proc_open  = proc_calibrate_fops_open,
	.proc_read  = seq_read,
	.proc_release = single_release,
};
#else
static const struct file_operations proc_calibrate_fops = {
	.owner = THIS_MODULE,
	.open  = proc_calibrate_fops_open,
	.read  = seq_read,
	.release = single_release,
};
#endif

static int cal_status_read_func(struct seq_file *s, void *v)
{
	bool cal_needed = false;
	struct touchpanel_data *ts = s->private;

	if (!ts->ts_ops->get_cal_status) {
		TPD_INFO("[TP]no calibration status, need back virtual calibration status");
		seq_printf(s, "0 error, calibration data is ok\n");
		return 0;
	}

	mutex_lock(&ts->mutex);
	cal_needed = ts->ts_ops->get_cal_status(s, ts->chip_data);

	if (cal_needed)
		seq_printf(s, "1 error, need do calibration\n");

	else
		seq_printf(s, "0 error, calibration data is ok\n");

	mutex_unlock(&ts->mutex);

	return 0;
}

static int proc_cal_status_fops_open(struct inode *inode, struct file *file)
{
	return single_open(file, cal_status_read_func, PDE_DATA(inode));
}


#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
static const struct proc_ops proc_cal_status_fops = {
	.proc_open  = proc_cal_status_fops_open,
	.proc_read  = seq_read,
	.proc_release = single_release,
};
#else
static const struct file_operations proc_cal_status_fops = {
	.owner = THIS_MODULE,
	.open  = proc_cal_status_fops_open,
	.read  = seq_read,
	.release = single_release,
};
#endif

/*******Part4:Debug node Function  Area********************/

#ifdef CONFIG_OPLUS_TP_APK
void log_buf_write(struct touchpanel_data *ts, u8 value)
{
	if (ts->log_buf) {
		u16 *head;
		head = (u16 *)(&ts->log_buf[0]);

		if ((*head) == 0)
			(*head) = 1;

		if ((*head) < 1023)
			(*head)++;

		else
			(*head) = 2;

		ts->log_buf[*head] = value;
	}
}
EXPORT_SYMBOL(log_buf_write);

static int log_buf_read(struct touchpanel_data *ts, char *buf, int len)
{
	if (ts->log_buf == NULL)
		return 0;

	if (len > 1024)
		len = 1024;

	memcpy((u8 *)buf, ts->log_buf, len);
	return len;
}


static char apk_charger_sta_read(struct touchpanel_data *ts)
{
	if (ts->apk_op->apk_charger_get) {
		if (ts->apk_op->apk_charger_get(ts->chip_data))
			return '1';

		return '0';
	}

	return '-';
}

static int apk_data_read(struct touchpanel_data *ts, char *buf, int len)
{
	switch (ts->data_now) {
	case BASE_DATA:
		if (ts->apk_op->apk_basedata_get)
			return ts->apk_op->apk_basedata_get(ts->chip_data, buf, len);

		break;

	case DIFF_DATA:
		if (ts->apk_op->apk_diffdata_get)
			return ts->apk_op->apk_diffdata_get(ts->chip_data, buf, len);

		break;

	case DEBUG_INFO:
		return log_buf_read(ts, buf, len);
		break;

	case RAW_DATA:
		if (ts->apk_op->apk_rawdata_get)
			return ts->apk_op->apk_rawdata_get(ts->chip_data, buf, len);

		break;

	case BACK_DATA:
		if (ts->apk_op->apk_backdata_get)
			return ts->apk_op->apk_backdata_get(ts->chip_data, buf, len);

		break;

	default:
		break;
	}

	buf[0] = '-';
	return 1;
}

static char apk_earphone_sta_read(struct touchpanel_data *ts)
{
	if (ts->apk_op->apk_earphone_get) {
		if (ts->apk_op->apk_earphone_get(ts->chip_data))
			return '1';

		return '0';
	}

	return '-';
}

static int apk_gesture_read(struct touchpanel_data *ts, char *buf, int len)
{
	if (ts->apk_op->apk_gesture_get) {
		if (ts->apk_op->apk_gesture_get(ts->chip_data))
			return ts->apk_op->apk_gesture_info(ts->chip_data, buf, len);

		buf[0] = '0';
		return 1;
	}

	buf[0] = '-';
	return 1;
}

static int apk_info_read(struct touchpanel_data *ts, char *buf, int len)
{
	if (ts->apk_op->apk_tp_info_get)
		return ts->apk_op->apk_tp_info_get(ts->chip_data, buf, len);

	buf[0] = '-';
	return 1;
}

static char apk_noise_read(struct touchpanel_data *ts)
{
	if (ts->apk_op->apk_noise_get) {
		if (ts->apk_op->apk_noise_get(ts->chip_data))
			return '1';

		return '0';
	}

	return '-';
}

static char apk_water_read(struct touchpanel_data *ts)
{
	if (ts->apk_op->apk_water_get) {
		if (ts->apk_op->apk_water_get(ts->chip_data) == 1)
			return '1';

		if (ts->apk_op->apk_water_get(ts->chip_data) == 2)
			return '2';

		return '0';
	}

	return '-';
}

static char apk_proximity_read(struct touchpanel_data *ts)
{
	if (ts->apk_op->apk_proximity_dis) {
		int dis;
		dis = ts->apk_op->apk_proximity_dis(ts->chip_data);

		if (dis > 0) {
			if (dis == 1)
				return '1';

			return '2';
		}

		return '0';
	}

	return '-';
}

static char apk_debug_sta(struct touchpanel_data *ts)
{
	if (ts->apk_op->apk_debug_get) {
		if (ts->apk_op->apk_debug_get(ts->chip_data))
			return '1';

		return '0';
	}

	return '-';
}

static char apk_game_read(struct touchpanel_data *ts)
{
	if (ts->apk_op->apk_game_get) {
		if (ts->apk_op->apk_game_get(ts->chip_data))
			return '1';

		return '0';
	}

	return '-';
}

static ssize_t oplus_apk_read(struct file *file,
			      char __user *user_buf,
			      size_t count,
			      loff_t *ppos)
{
	char *buf;
	int len = 0;
	int ret = 0;
	struct touchpanel_data *ts = PDE_DATA(file_inode(file));

	if (!ts) {
		TPD_INFO("ts not exist!\n");
		return -ENODEV;
	}

	if (ts->apk_op == NULL) {
		TPD_INFO("ts apk_op not exist!\n");
		return -ENODEV;
	}

	if (*ppos != 0 || count < 1)
		return 0;

	TPD_INFO("apk read is %c, count is %d.\n", (char)ts->type_now, (int)count);
	buf = kzalloc(count, GFP_KERNEL);

	if (IS_ERR(buf) || buf == NULL) {
		ret = -EFAULT;
		goto read_exit;
	}

	mutex_lock(&ts->mutex);

	switch (ts->type_now) {
	case APK_CHARGER:
		buf[0] = apk_charger_sta_read(ts);
		len = 1;
		break;

	case APK_DATA:
		len = apk_data_read(ts, buf, count);
		break;

	case APK_EARPHONE:
		buf[0] = apk_earphone_sta_read(ts);
		len = 1;
		break;

	case APK_GESTURE:
		len = apk_gesture_read(ts, buf, count);
		break;

	case APK_INFO:
		len = apk_info_read(ts, buf, count);
		break;

	case APK_NOISE:
		buf[0] = apk_noise_read(ts);
		len = 1;
		break;

	case APK_WATER:
		buf[0] = apk_water_read(ts);
		len = 1;
		break;

	case APK_PROXIMITY:
		buf[0] = apk_proximity_read(ts);
		len = 1;
		break;

	case APK_DEBUG_MODE:
		buf[0] = apk_debug_sta(ts);
		len = 1;
		break;

	case APK_GAME_MODE:
		buf[0] = apk_game_read(ts);
		len = 1;
		break;

	default:
		break;
	}

	if (len == 1 && len < count) {
		buf[len] = '\n';
		len++;
	}

	mutex_unlock(&ts->mutex);

	if (copy_to_user(user_buf, buf, len)) {
		TPD_INFO("%s: can not copy the buf.\n", __func__);
		ret = -EFAULT;
		goto read_exit;
	}

	ret = len;
	*ppos += ret;

read_exit:

	if (buf != NULL)
		kfree(buf);

	return ret;
}

static void apk_charger_switch(struct touchpanel_data *ts, char on_off)
{
	if (ts->apk_op->apk_charger_set) {
		if (on_off == '1')
			ts->apk_op->apk_charger_set(ts->chip_data, true);

		else if (on_off == '0')
			ts->apk_op->apk_charger_set(ts->chip_data, false);
	}
}

static void apk_earphone_switch(struct touchpanel_data *ts, char on_off)
{
	if (ts->apk_op->apk_earphone_set) {
		if (on_off == '1')
			ts->apk_op->apk_earphone_set(ts->chip_data, true);

		else if (on_off == '0')
			ts->apk_op->apk_earphone_set(ts->chip_data, false);
	}
}

static void apk_gesture_debug(struct touchpanel_data *ts, char on_off)
{
	if (ts->apk_op->apk_gesture_debug) {
		if (on_off == '1') {
			if (ts->gesture_buf == NULL)
				ts->gesture_buf = kzalloc(1024, GFP_KERNEL);

			if (ts->gesture_buf) {
				ts->gesture_debug_sta = true;
				ts->apk_op->apk_gesture_debug(ts->chip_data, true);

			} else {
				ts->gesture_debug_sta = false;
				ts->apk_op->apk_gesture_debug(ts->chip_data, false);
			}

		} else if (on_off == '0') {
			ts->apk_op->apk_gesture_debug(ts->chip_data, false);

			if (ts->gesture_buf) {
				kfree(ts->gesture_buf);
				ts->gesture_buf = NULL;
			}

			ts->gesture_debug_sta = false;
		}
	}
}

static void apk_noise_switch(struct touchpanel_data *ts, char on_off)
{
	if (ts->apk_op->apk_noise_set) {
		if (on_off == '1')
			ts->apk_op->apk_noise_set(ts->chip_data, true);

		else if (on_off == '0')
			ts->apk_op->apk_noise_set(ts->chip_data, false);
	}
}

static void apk_water_switch(struct touchpanel_data *ts, char on_off)
{
	if (ts->apk_op->apk_water_set) {
		if (on_off == '1')
			ts->apk_op->apk_water_set(ts->chip_data, 1);

		else if (on_off == '2')
			ts->apk_op->apk_water_set(ts->chip_data, 2);

		else if (on_off == '0')
			ts->apk_op->apk_water_set(ts->chip_data, 0);
	}
}

static void apk_proximity_switch(struct touchpanel_data *ts,
				 char on_off)
{
	if (ts->apk_op->apk_proximity_set) {
		if (on_off == '1')
			ts->apk_op->apk_proximity_set(ts->chip_data, true);

		else if (on_off == '0')
			ts->apk_op->apk_proximity_set(ts->chip_data, false);
	}
}

static void apk_debug_switch(struct touchpanel_data *ts, char on_off)
{
	if (ts->apk_op->apk_debug_set) {
		if (on_off == '1') {
			ts->apk_op->apk_debug_set(ts->chip_data, true);

			if (ts->log_buf == NULL)
				ts->log_buf = kzalloc(1024, GFP_KERNEL);

		} else if (on_off == '0') {
			ts->apk_op->apk_debug_set(ts->chip_data, false);

			if (ts->log_buf) {
				kfree(ts->log_buf);
				ts->log_buf = NULL;
			}
		}
	}
}

static void apk_data_type_set(struct touchpanel_data *ts, char ch)
{
	APK_DATA_TYPE type;
	type = (APK_DATA_TYPE)ch;

	switch (type) {
	case BASE_DATA:
	case DIFF_DATA:
	case DEBUG_INFO:
	case RAW_DATA:
	case BACK_DATA:
		ts->data_now = type;

		if (ts->apk_op->apk_data_type_set)
			ts->apk_op->apk_data_type_set(ts->chip_data, ts->data_now);

		break;

	default:
		break;
	}
}

static void apk_game_switch(struct touchpanel_data *ts, char on_off)
{
	if (ts->apk_op->apk_game_set) {
		if (on_off == '1')
			ts->apk_op->apk_game_set(ts->chip_data, true);

		else if (on_off == '0')
			ts->apk_op->apk_game_set(ts->chip_data, false);
	}
}

static ssize_t oplus_apk_write(struct file *file,
			       const char __user *buffer,
			       size_t count,
			       loff_t *ppos)
{
	char *buf;
	APK_SWITCH_TYPE type;
	int ret = count;
	struct touchpanel_data *ts = PDE_DATA(file_inode(file));

	if (!ts) {
		TPD_INFO("ts not exist!\n");
		return -ENODEV;
	}

	if (ts->apk_op == NULL) {
		TPD_INFO("ts apk_op not exist!\n");
		return -ENODEV;
	}

	if (count < 1)
		return 0;


	buf = kzalloc(count, GFP_KERNEL);

	if (IS_ERR(buf) || buf == NULL) {
		ret = -EFAULT;
		goto write_exit;
	}

	if (copy_from_user(buf, buffer, count)) {
		TPD_INFO("%s: can not copy the buf.\n", __func__);
		ret = -EFAULT;
		goto write_exit;
	}

	mutex_lock(&ts->mutex);

	type = (APK_SWITCH_TYPE)buf[0];
	TPD_INFO("apk write type is %c, count is %d.\n", (char)type,
		 (int)count);

	if (count > 1) {
		switch (type) {
		case APK_CHARGER:
			apk_charger_switch(ts, buf[1]);
			break;

		case APK_DATA:
			apk_data_type_set(ts, buf[1]);
			break;

		case APK_EARPHONE:
			apk_earphone_switch(ts, buf[1]);
			break;

		case APK_GESTURE:
			apk_gesture_debug(ts, buf[1]);
			break;

		case APK_INFO: /* read only, do nothing in write.*/
			break;

		case APK_NOISE:
			apk_noise_switch(ts, buf[1]);
			break;

		case APK_PROXIMITY:
			apk_proximity_switch(ts, buf[1]);
			break;

		case APK_WATER:
			apk_water_switch(ts, buf[1]);
			break;

		case APK_DEBUG_MODE:
			apk_debug_switch(ts, buf[1]);
			break;

		case APK_GAME_MODE:
			apk_game_switch(ts, buf[1]);
			break;

		default:
			type = APK_NULL;
			break;
		}
	}

	ts->type_now = type;
	mutex_unlock(&ts->mutex);

write_exit:

	if (buf != NULL)
		kfree(buf);

	return ret;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
static const struct proc_ops proc_oplus_apk_fops = {
	.proc_open = simple_open,
	.proc_read = oplus_apk_read,
	.proc_write = oplus_apk_write,
};
#else
static const struct file_operations proc_oplus_apk_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = oplus_apk_read,
	.write = oplus_apk_write,
};
#endif

#endif /*end of CONFIG_OPLUS_TP_APK*/

/*proc/touchpanel/debug_info/baseline*/
static int tp_baseline_debug_read_func(struct seq_file *s, void *v)
{
	struct touchpanel_data *ts = s->private;
	struct debug_info_proc_operations *debug_info_ops;

	if (!ts)
		return 0;

	debug_info_ops = (struct debug_info_proc_operations *)(ts->debug_info_ops);

	if (!debug_info_ops) {
		TP_INFO(ts->tp_index, "debug_info_ops==NULL");
		return 0;
	}

	if (!debug_info_ops->baseline_read
			&& !debug_info_ops->baseline_blackscreen_read) {
		seq_printf(s, "Not support baseline proc node\n");
		return 0;
	}

	if ((ts->suspend_state != TP_SPEEDUP_RESUME_COMPLETE)
			&& (1 != (ts->gesture_enable & 0x01))) {
		seq_printf(s, "Not in resume over or gesture state\n");
		return 0;
	}

	/*the diff  is big than one page, so do twice.*/
	if (s->size <= (PAGE_SIZE * 2)) {
		s->count = s->size;
		return 0;
	}

	if (ts->int_mode == BANNABLE)
		disable_irq_nosync(ts->irq);

	mutex_lock(&ts->mutex);

	if (ts->is_suspended && ts->gesture_enable) {
		if (debug_info_ops->baseline_blackscreen_read)
			debug_info_ops->baseline_blackscreen_read(s, ts->chip_data);

	} else {
		if (debug_info_ops->baseline_read)
			debug_info_ops->baseline_read(s, ts->chip_data);
	}

	/*step6: return to normal mode*/
	ts->ts_ops->reset(ts->chip_data);

	if (ts->is_suspended == 0)
		tp_touch_btnkey_release(ts->tp_index);

	operate_mode_switch(ts);

	mutex_unlock(&ts->mutex);

	if (ts->int_mode == BANNABLE)
		enable_irq(ts->irq);

	return 0;
}

static int data_baseline_open(struct inode *inode, struct file *file)
{
	return single_open(file, tp_baseline_debug_read_func, PDE_DATA(inode));
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
static const struct proc_ops tp_baseline_data_proc_fops = {
	.proc_open = data_baseline_open,
	.proc_read = seq_read,
	.proc_release = single_release,
};
#else
static const struct file_operations tp_baseline_data_proc_fops = {
	.owner = THIS_MODULE,
	.open = data_baseline_open,
	.read = seq_read,
	.release = single_release,
};
#endif

/*proc/touchpanel/debug_info/delta*/
static int tp_delta_debug_read_func(struct seq_file *s, void *v)
{
	struct touchpanel_data *ts = s->private;
	struct debug_info_proc_operations *debug_info_ops;

	if (!ts)
		return 0;

	debug_info_ops = (struct debug_info_proc_operations *)ts->debug_info_ops;

	if (!debug_info_ops)
		return 0;

	if (!debug_info_ops->delta_read) {
		seq_printf(s, "Not support delta proc node\n");
		return 0;
	}

	if (ts->suspend_state != TP_SPEEDUP_RESUME_COMPLETE) {
		seq_printf(s, "Not in resume over state\n");
		return 0;
	}

	/*the diff  is big than one page, so do twice.*/
	if (s->size <= (PAGE_SIZE * 2)) {
		s->count = s->size;
		return 0;
	}

	if (ts->int_mode == BANNABLE)
		disable_irq_nosync(ts->irq);

	mutex_lock(&ts->mutex);
	debug_info_ops->delta_read(s, ts->chip_data);
	mutex_unlock(&ts->mutex);

	if (ts->int_mode == BANNABLE)
		enable_irq(ts->irq);

	return 0;
}

static int data_delta_open(struct inode *inode, struct file *file)
{
	return single_open(file, tp_delta_debug_read_func, PDE_DATA(inode));
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
static const struct proc_ops tp_delta_data_proc_fops = {
	.proc_open  = data_delta_open,
	.proc_read  = seq_read,
	.proc_release = single_release,
};
#else
static const struct file_operations tp_delta_data_proc_fops = {
	.owner = THIS_MODULE,
	.open  = data_delta_open,
	.read  = seq_read,
	.release = single_release,
};
#endif

/*proc/touchpanel/debug_info/self_delta*/
static int tp_self_delta_debug_read_func(struct seq_file *s, void *v)
{
	struct touchpanel_data *ts = s->private;
	struct debug_info_proc_operations *debug_info_ops;

	if (!ts)
		return 0;

	debug_info_ops = (struct debug_info_proc_operations *)ts->debug_info_ops;

	if (!debug_info_ops)
		return 0;

	if (!debug_info_ops->self_delta_read) {
		seq_printf(s, "Not support self_delta proc node\n");
		return 0;
	}

	if (ts->suspend_state != TP_SPEEDUP_RESUME_COMPLETE) {
		seq_printf(s, "Not in resume over state\n");
		return 0;
	}

	if (ts->int_mode == BANNABLE)
		disable_irq_nosync(ts->irq);

	mutex_lock(&ts->mutex);
	debug_info_ops->self_delta_read(s, ts->chip_data);
	mutex_unlock(&ts->mutex);

	if (ts->int_mode == BANNABLE)
		enable_irq(ts->irq);

	return 0;
}

static int data_self_delta_open(struct inode *inode, struct file *file)
{
	return single_open(file, tp_self_delta_debug_read_func, PDE_DATA(inode));
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
static const struct proc_ops tp_self_delta_data_proc_fops = {
	.proc_open  = data_self_delta_open,
	.proc_read  = seq_read,
	.proc_release = single_release,
};
#else
static const struct file_operations tp_self_delta_data_proc_fops = {
	.owner = THIS_MODULE,
	.open  = data_self_delta_open,
	.read  = seq_read,
	.release = single_release,
};
#endif

/*proc/touchpanel/debug_info/self_raw*/
static int tp_self_raw_debug_read_func(struct seq_file *s, void *v)
{
	struct touchpanel_data *ts = s->private;
	struct debug_info_proc_operations *debug_info_ops;

	if (!ts)
		return 0;

	debug_info_ops = (struct debug_info_proc_operations *)ts->debug_info_ops;

	if (!debug_info_ops)
		return 0;

	if (!debug_info_ops->self_raw_read) {
		seq_printf(s, "Not support self_raw proc node\n");
		return 0;
	}

	if (ts->suspend_state != TP_SPEEDUP_RESUME_COMPLETE) {
		seq_printf(s, "Not in resume over state\n");
		return 0;
	}

	if (ts->int_mode == BANNABLE)
		disable_irq_nosync(ts->irq);

	mutex_lock(&ts->mutex);
	debug_info_ops->self_raw_read(s, ts->chip_data);
	mutex_unlock(&ts->mutex);

	if (ts->int_mode == BANNABLE)
		enable_irq(ts->irq);

	return 0;
}

static int data_self_raw_open(struct inode *inode, struct file *file)
{
	return single_open(file, tp_self_raw_debug_read_func, PDE_DATA(inode));
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
static const struct proc_ops tp_self_raw_data_proc_fops = {
	.proc_open  = data_self_raw_open,
	.proc_read  = seq_read,
	.proc_release = single_release,
};
#else
static const struct file_operations tp_self_raw_data_proc_fops = {
	.owner = THIS_MODULE,
	.open  = data_self_raw_open,
	.read  = seq_read,
	.release = single_release,
};
#endif

/*proc/touchpanel/debug_info/main_register*/
static int tp_main_register_read_func(struct seq_file *s, void *v)
{
	struct touchpanel_data *ts = s->private;
	struct debug_info_proc_operations *debug_info_ops;
	struct monitor_data *monitor_data = &ts->monitor_data;

	if (!ts)
		return 0;

	debug_info_ops = (struct debug_info_proc_operations *)ts->debug_info_ops;

	if (!debug_info_ops)
		return 0;

	if (!debug_info_ops->main_register_read) {
		seq_printf(s, "Not support main_register proc node\n");
		return 0;
	}

	if (ts->suspend_state != TP_SPEEDUP_RESUME_COMPLETE) {
		seq_printf(s, "Not in resume over state\n");
		return 0;
	}

	if (ts->int_mode == BANNABLE)
		disable_irq_nosync(ts->irq);

	mutex_lock(&ts->mutex);
	seq_printf(s, "touch_count:%d\n", ts->touch_count);
	debug_info_ops->main_register_read(s, ts->chip_data);

	if (ts->health_monitor_support && tp_debug == 2) {
		if (monitor_data->fw_version) {
			memset(monitor_data->fw_version, 0, MAX_DEVICE_VERSION_LENGTH);
			strncpy(monitor_data->fw_version, ts->panel_data.manufacture_info.version,
				strlen(ts->panel_data.manufacture_info.version));
		}
	}

	mutex_unlock(&ts->mutex);

	if (ts->int_mode == BANNABLE)
		enable_irq(ts->irq);

	return 0;
}

static int main_register_open(struct inode *inode, struct file *file)
{
	return single_open(file, tp_main_register_read_func, PDE_DATA(inode));
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
static const struct proc_ops tp_main_register_proc_fops = {
	.proc_open  = main_register_open,
	.proc_read  = seq_read,
	.proc_release = single_release,
};
#else
static const struct file_operations tp_main_register_proc_fops = {
	.owner = THIS_MODULE,
	.open  = main_register_open,
	.read  = seq_read,
	.release = single_release,
};
#endif

/*proc/touchpanel/debug_info/reserve*/
static int tp_reserve_read_func(struct seq_file *s, void *v)
{
	struct touchpanel_data *ts = s->private;
	struct debug_info_proc_operations *debug_info_ops;

	if (!ts)
		return 0;

	debug_info_ops = (struct debug_info_proc_operations *)ts->debug_info_ops;

	if (!debug_info_ops)
		return 0;

	if (!debug_info_ops->reserve_read) {
		seq_printf(s, "Not support main_register proc node\n");
		return 0;
	}

	if (ts->suspend_state != TP_SPEEDUP_RESUME_COMPLETE) {
		seq_printf(s, "Not in resume over state\n");
		return 0;
	}

	if (ts->int_mode == BANNABLE)
		disable_irq_nosync(ts->irq);

	mutex_lock(&ts->mutex);
	debug_info_ops->reserve_read(s, ts->chip_data);
	mutex_unlock(&ts->mutex);

	if (ts->int_mode == BANNABLE)
		enable_irq(ts->irq);

	return 0;
}

static int reserve_open(struct inode *inode, struct file *file)
{
	return single_open(file, tp_reserve_read_func, PDE_DATA(inode));
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
static const struct proc_ops tp_reserve_proc_fops = {
	.proc_open  = reserve_open,
	.proc_read  = seq_read,
	.proc_release = single_release,
};
#else
static const struct file_operations tp_reserve_proc_fops = {
	.owner = THIS_MODULE,
	.open  = reserve_open,
	.read  = seq_read,
	.release = single_release,
};
#endif

/*proc/touchpanel/debug_info/data_limit*/
static int tp_limit_data_read_func(struct seq_file *s, void *v)
{
	struct touchpanel_data *ts = s->private;

	if (!ts)
		return 0;

	tp_limit_read(s, ts);

	return 0;
}

static int limit_data_open(struct inode *inode, struct file *file)
{
	return single_open(file, tp_limit_data_read_func, PDE_DATA(inode));
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
static const struct proc_ops tp_limit_data_proc_fops = {
	.proc_open  = limit_data_open,
	.proc_read  = seq_read,
	.proc_release = single_release,
};
#else
static const struct file_operations tp_limit_data_proc_fops = {
	.owner = THIS_MODULE,
	.open  = limit_data_open,
	.read  = seq_read,
	.release = single_release,
};
#endif

/*proc/touchpanel/debug_info/abs_doze*/
static int tp_abs_doze_read_func(struct seq_file *s, void *v)
{
	struct touchpanel_data *ts = s->private;
	struct debug_info_proc_operations *debug_info_ops;

	if (!ts)
		return 0;

	debug_info_ops = (struct debug_info_proc_operations *)ts->debug_info_ops;

	if (!debug_info_ops)
		return 0;

	if (!debug_info_ops->abs_doze_read) {
		seq_printf(s, "Not support main_register proc node\n");
		return 0;
	}

	if (ts->suspend_state != TP_SPEEDUP_RESUME_COMPLETE) {
		seq_printf(s, "Not in resume over state\n");
		return 0;
	}

	if (ts->int_mode == BANNABLE)
		disable_irq_nosync(ts->irq);

	mutex_lock(&ts->mutex);
	debug_info_ops->abs_doze_read(s, ts->chip_data);
	mutex_unlock(&ts->mutex);

	if (ts->int_mode == BANNABLE)
		enable_irq(ts->irq);

	return 0;
}

static int abs_doze_open(struct inode *inode, struct file *file)
{
	return single_open(file, tp_abs_doze_read_func, PDE_DATA(inode));
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
static const struct proc_ops tp_abs_doze_proc_fops = {
	.proc_open  = abs_doze_open,
	.proc_read  = seq_read,
	.proc_release = single_release,
};
#else
static const struct file_operations tp_abs_doze_proc_fops = {
	.owner = THIS_MODULE,
	.open  = abs_doze_open,
	.read  = seq_read,
	.release = single_release,
};
#endif


/*proc/touchpanel/debug_info/snr*/
static ssize_t proc_snr_write(struct file *file, const char __user *buf,
			      size_t count, loff_t *lo)
{
	struct touchpanel_data *ts = PDE_DATA(file_inode(file));
	int tmp = 0, i = 0;
	char buffer[4] = {0};

	if (!ts)
		return count;

	if (!ts->snr_read_support)
		return count;

	if (count > 2)
		return count;

	if (copy_from_user(buffer, buf, count)) {
		TPD_INFO("%s: read proc input error.\n", __func__);
		return count;
	}

	if (1 == sscanf(buffer, "%d", &tmp)) {
		for (i = 0; i < 10; i++)
			ts->snr[i].doing = !!tmp;
	} else
		TPD_DEBUG("invalid content: '%s', length = %zd\n", buf, count);

	TPD_INFO("%s: snr write doing = %d.\n", __func__, ts->snr[0].doing);
	return count;
}

#define SNR_NODE_READ_TIMES 100
static int tp_baseline_snr_read_func(struct seq_file *s, void *v)
{
	struct touchpanel_data *ts = s->private;
	struct debug_info_proc_operations *debug_info_ops = NULL;

	if (!ts)
		return 0;

	if (!ts->snr_read_support)
		return 0;

	debug_info_ops = (struct debug_info_proc_operations *)(ts->debug_info_ops);

	if (!debug_info_ops) {
		TPD_INFO("debug_info_ops == NULL");
		return 0;
	}

	if (!debug_info_ops->delta_snr_read) {
		seq_printf(s, "Not support baseline snr proc node\n");
		return 0;
	}

	if ((ts->suspend_state != TP_SPEEDUP_RESUME_COMPLETE)
			&& (1 != (ts->gesture_enable & 0x01))) {
		seq_printf(s, "Not in resume over or gesture state\n");
		return 0;
	}

	if (s->size <= (PAGE_SIZE * 2)) {
		s->count = s->size;
		return 0;
	}

	if (ts->int_mode == BANNABLE)
		disable_irq_nosync(ts->irq);

	mutex_lock(&ts->mutex);

	if (debug_info_ops->delta_snr_read)
		debug_info_ops->delta_snr_read(s, ts->chip_data, SNR_NODE_READ_TIMES);

	mutex_unlock(&ts->mutex);

	if (ts->int_mode == BANNABLE)
		enable_irq(ts->irq);

	return 0;
}

static int proc_snr_open(struct inode *inode, struct file *file)
{
	return single_open(file, tp_baseline_snr_read_func, PDE_DATA(inode));
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
static const struct proc_ops proc_snr_ops = {
	.proc_open  = proc_snr_open,
	.proc_read = seq_read,
	.proc_write = proc_snr_write,
};
#else
static const struct file_operations proc_snr_ops = {
	.owner = THIS_MODULE,
	.open = proc_snr_open,
	.read = seq_read,
	.write = proc_snr_write,
};
#endif

void tp_freq_hop_work(struct work_struct *work)
{
	struct touchpanel_data *ts = container_of(work, struct touchpanel_data,
				     freq_hop_info.freq_hop_work.work);

	TP_INFO(ts->tp_index, "syna_tcm_freq_hop_work\n");

	if (!ts->is_suspended) {
		TP_INFO(ts->tp_index, "trigger frequency hopping~~~~\n");

		if (!ts->ts_ops->freq_hop_trigger) {
			TP_INFO(ts->tp_index, "%s:not support ts_ops->freq_hop_trigger callback\n",
				__func__);
			return;
		}

		ts->ts_ops->freq_hop_trigger(ts->chip_data);
	}

	if (ts->freq_hop_info.freq_hop_simulating) {
		TP_INFO(ts->tp_index, "queue_delayed_work again\n");
		queue_delayed_work(ts->freq_hop_info.freq_hop_workqueue,
				   &ts->freq_hop_info.freq_hop_work, ts->freq_hop_info.freq_hop_freq * HZ);
	}
}

static void tp_freq_hop_simulate(struct touchpanel_data *ts, int freq_hop_freq)
{
	TP_INFO(ts->tp_index, "%s is called.\n", __func__);
	ts->freq_hop_info.freq_hop_freq = freq_hop_freq;

	if (ts->freq_hop_info.freq_hop_simulating && !freq_hop_freq)
		ts->freq_hop_info.freq_hop_simulating = false;

	else {
		if (!ts->freq_hop_info.freq_hop_simulating) {
			ts->freq_hop_info.freq_hop_simulating = true;
			queue_delayed_work(ts->freq_hop_info.freq_hop_workqueue,
					   &ts->freq_hop_info.freq_hop_work, ts->freq_hop_info.freq_hop_freq * HZ);
		}
	}
}

/*proc/touchpanel/debug_info/freq_hop_simulate*/
static ssize_t proc_freq_hop_write(struct file *file, const char __user *buffer,
				   size_t count, loff_t *ppos)
{
	int value = 0;
	char buf[4] = {0};
	struct touchpanel_data *ts = PDE_DATA(file_inode(file));

	if (!ts) {
		TP_INFO(ts->tp_index, "%s: ts is NULL\n", __func__);
		return count;
	}

	tp_copy_from_user(buf, sizeof(buf), buffer, count, 4);

	if (kstrtoint(buf, 16, &value)) {
		TP_INFO(ts->tp_index, "%s: kstrtoint error\n", __func__);
		return count;
	}

	ts->freq_hop_info.freq_hop_freq = value;

	TP_INFO(ts->tp_index, "%s: freq_hop_simulate value=0x%x\n", __func__, value);

	if (!ts->is_suspended) {
		mutex_lock(&ts->mutex);
		tp_freq_hop_simulate(ts, value);
		mutex_unlock(&ts->mutex);

	} else {
		TP_INFO(ts->tp_index, "%s: freq_hop_simulate is_suspended.\n", __func__);
		ts->freq_hop_info.freq_hop_freq = value;
	}

	return count;
}

static ssize_t proc_freq_hop_read(struct file *file, char __user *buffer,
				  size_t count, loff_t *ppos)
{
	int ret = 0;
	char page[PAGESIZE] = {0};
	struct touchpanel_data *ts = PDE_DATA(file_inode(file));

	if (!ts) {
		snprintf(page, PAGESIZE - 1, "%d\n", -1); /*no support*/

	} else {
		/*support*/
		snprintf(page, PAGESIZE - 1, "%d\n", ts->freq_hop_info.freq_hop_freq);
	}

	ret = simple_read_from_buffer(buffer, count, ppos, page, strlen(page));
	return ret;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
static const struct proc_ops proc_freq_hop_fops = {
	.proc_write = proc_freq_hop_write,
	.proc_read  = proc_freq_hop_read,
	.proc_open  = simple_open,
};
#else
static const struct file_operations proc_freq_hop_fops = {
	.write = proc_freq_hop_write,
	.read  = proc_freq_hop_read,
	.open  = simple_open,
	.owner = THIS_MODULE,
};
#endif

/*******Part5:Register node Function  Area********************/

typedef struct {
	char *name;
	umode_t mode;
	struct proc_dir_entry *node;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
	const struct proc_ops *fops;
#else
	const struct file_operations *fops;
#endif
	void *data;
	bool is_created;/*proc node is creater or not*/
	bool is_support;/*feature is supported or not*/
} tp_proc_node;


/*proc/touchpanel/debug_info*/
static int init_debug_info_proc(struct touchpanel_data *ts)
{
	int ret = 0;
	int i = 0;
	struct proc_dir_entry *prEntry_debug_info = NULL;

	tp_proc_node proc_debug_node[] = {
		{"data_limit", 0666, NULL, &tp_limit_data_proc_fops, ts, false, true},/* show limit data interface*/
		{"baseline", 0666, NULL, &tp_baseline_data_proc_fops, ts, false, true},/* show baseline data interface*/
		{"delta", 0666, NULL, &tp_delta_data_proc_fops, ts, false, true},/* show delta interface*/
		{"self_delta", 0666, NULL, &tp_self_delta_data_proc_fops, ts, false, true},/* show self delta interface*/
		{"self_raw", 0666, NULL, &tp_self_raw_data_proc_fops, ts, false, true},/* show self_raw interface*/
		{"main_register", 0666, NULL, &tp_main_register_proc_fops, ts, false, true},/* show main_register interface*/
		{"reserve", 0666, NULL, &tp_reserve_proc_fops, ts, false, true},/* show reserve interface*/
		{"abs_doze", 0666, NULL, &tp_abs_doze_proc_fops, ts, false, true},/* show abs_doze interface*/
		{
			"snr", 0666, NULL, &proc_snr_ops, ts, false,
			ts->snr_read_support
		},/* show abs_doze interface*/
		{
			"freq_hop_simulate_support", 0666, NULL, &proc_freq_hop_fops, ts, false,
			ts->freq_hop_simulate_support
		},
	};

	TP_INFO(ts->tp_index, "%s entry\n", __func__);

	/*proc/touchpanel/debug_info*/
	prEntry_debug_info = proc_mkdir("debug_info", ts->prEntry_tp);

	if (prEntry_debug_info == NULL) {
		ret = -ENOMEM;
		TP_INFO(ts->tp_index, "%s: Couldn't create debug_info proc entry\n", __func__);
	}

	ts->prEntry_debug_tp = prEntry_debug_info;

	for (i = 0; i < ARRAY_SIZE(proc_debug_node); i++) {
		if (proc_debug_node[i].is_support) {
			proc_debug_node[i].node = proc_create_data(proc_debug_node[i].name,
						  proc_debug_node[i].mode,
						  prEntry_debug_info, proc_debug_node[i].fops, proc_debug_node[i].data);

			if (proc_debug_node[i].node == NULL) {
				proc_debug_node[i].is_created = false;
				TP_INFO(ts->tp_index, "%s: Couldn't create proc/debug_info/%s\n", __func__,
					proc_debug_node[i].name);
				ret = -ENODEV;
			} else
				proc_debug_node[i].is_created = true;
		}
	}

	return ret;
}

/**
 * init_touchpanel_proc - Using for create proc interface
 * @ts: touchpanel_data struct using for common driver
 *
 * we need to set touchpanel_data struct as private_data to those file_inode
 * Returning zero(success) or negative errno(failed)
 */
int init_touchpanel_proc(struct touchpanel_data *ts)
{
	int ret = 0;
	int i = 0;
	struct proc_dir_entry *prEntry_tp = NULL;
	char name[TP_NAME_SIZE_MAX];

	tp_proc_node tp_proc_node[] = {
		{
			"oplus_optimized_time", 0666, NULL, &proc_optimized_time_fops, ts, false,
			ts->optimized_show_support
		},
		{"tp_index", 0666, NULL, &proc_tp_index_ops, ts, false, true},
		{"debug_level", 0644, NULL, &proc_debug_level_ops, ts, false, true},
		{
			"double_tap_enable", 0666, NULL, &proc_gesture_control_fops, ts, false,
			ts->black_gesture_support
		},
		{
			"coordinate", 0444, NULL, &proc_coordinate_fops, ts, false,
			ts->black_gesture_support
		},
		{
			"game_switch_enable", 0666, NULL, &proc_game_switch_fops, ts, false,
			ts->game_switch_support
		},
		{"irq_depth", 0666, NULL, &proc_get_irq_depth_fops, ts, false, true},
		{
			"oplus_tp_noise_modetest", 0664, NULL, &proc_noise_modetest_fops, ts, false,
			ts->noise_modetest_support
		},
		{"tp_fw_update", 0666, NULL, &proc_fw_update_ops, ts, false, true},
		{"oplus_register_info", 0664, NULL, &proc_register_info_fops, ts, false, true},
		{
			"incell_panel", 0664, NULL, &proc_incell_panel_fops, ts, false,
			ts->is_incell_panel
		},
		{
			"fd_enable", 0666, NULL, &tp_fd_enable_fops, ts, false,
			ts->face_detect_support
		},
		{
			"event_num", 0666, NULL, &tp_event_num_fops, ts, false,
			ts->face_detect_support
		},
		{
			"fd_touch_count", 0666, NULL, &fd_touch_num_fops, ts, false,
			ts->face_detect_support
		},
		{
			"fp_enable", 0666, NULL, &tp_fp_enable_fops, ts, false,
			ts->fingerprint_underscreen_support
		},
		{
			"baseline_test", 0666, NULL, &tp_auto_test_proc_fops, ts, false, true
		},
		{
			"black_screen_test", 0666, NULL, &proc_black_screen_test_fops, ts, false,
			ts->gesture_test_support
		},
		{
			"baseline_result", 0666, NULL, &tp_auto_test_result_fops, ts, false, true
		},
		{
			"black_screen_result", 0666, NULL, &proc_black_screen_result_fops, ts, false,
			ts->gesture_test_support
		},
		{
			"oplus_tp_direction", 0666, NULL, &touch_dir_proc_fops, ts, false,
			ts->fw_edge_limit_support
		},
		{
			"report_rate_white_list", 0666, NULL, &proc_rate_white_list_fops, ts, false,
			ts->report_rate_white_list_support
		},
		{
			"charge_detect", 0666, NULL, &proc_switch_usb_state_fops, ts, false,
			ts->charger_pump_support
		},
		{
			"wireless_charge_detect", 0666, NULL, &proc_wireless_charge_detect_fops, ts, false,
			ts->wireless_charger_support
		},
		{
			"headset_detect", 0666, NULL, &proc_headset_detect_fops, ts, false,
			ts->headset_pump_support
		},
		{
			"tp_aging_test", 0666, NULL, &proc_aging_test_ops, ts, false,
			true
		},
		{
			"smooth_level", 0666, NULL, &proc_smooth_level_fops, ts, false,
			ts->smooth_level_array_support
		},
		{
			"sensitive_level", 0666, NULL, &proc_sensitive_level_fops, ts, false,
			ts->sensitive_level_array_support
		},
		{
			"double_tap_enable_indep", 0666, NULL, &proc_gesture_control_indep_fops, ts, false,
			ts->black_gesture_indep_support
		},
		{
			"calibration", 0666, NULL, &proc_calibrate_fops, ts, false,
			ts->auto_test_need_cal_support
		},
		{
			"calibration_status", 0666, NULL, &proc_cal_status_fops, ts, false,
			ts->auto_test_need_cal_support
		},
		/* proc/touchpanel/oplus_apk. Add the new test node for debug and apk. By zhangping 20190402 start*/
#ifdef CONFIG_OPLUS_TP_APK
		{"oplus_apk", 0666, NULL, &proc_oplus_apk_fops, ts, false, true},
#endif /* end of CONFIG_OPLUS_TP_APK*/
	};

	TP_INFO(ts->tp_index, "%s entry\n", __func__);

	/*proc files-step1:/proc/devinfo/tp  (touchpanel device info)*/
#ifndef REMOVE_OPLUS_FUNCTION

	if (ts->tp_index == 0)
		snprintf(name, TP_NAME_SIZE_MAX, "%s", "tp");

	else
		snprintf(name, TP_NAME_SIZE_MAX, "%s%d", "tp", ts->tp_index);

	if (ts->fw_update_app_support)
		register_devinfo(name, &ts->panel_data.manufacture_info);

	else {
		TP_INFO(ts->tp_index, "register_devinfo not defined!\n");
		register_device_proc(name, ts->panel_data.manufacture_info.version,
				     ts->panel_data.manufacture_info.manufacture);
	}

#endif

	/*proc files-step2:/proc/touchpanel*/
	if (ts->tp_index == 0)
		snprintf(name, TP_NAME_SIZE_MAX, "%s", TPD_DEVICE);

	else
		snprintf(name, TP_NAME_SIZE_MAX, "%s%d", TPD_DEVICE, ts->tp_index);

	prEntry_tp = proc_mkdir(name, NULL);

	if (prEntry_tp == NULL) {
		ret = -ENOMEM;
		TP_INFO(ts->tp_index, "%s: Couldn't create TP proc entry\n", __func__);
	}

	ts->prEntry_tp = prEntry_tp;

	for (i = 0; i < ARRAY_SIZE(tp_proc_node); i++) {
		if (tp_proc_node[i].is_support) {
			tp_proc_node[i].node = proc_create_data(tp_proc_node[i].name,
								tp_proc_node[i].mode,
								prEntry_tp, tp_proc_node[i].fops, tp_proc_node[i].data);

			if (tp_proc_node[i].node == NULL) {
				tp_proc_node[i].is_created = false;
				TP_INFO(ts->tp_index, "%s: Couldn't create proc/debug_info/%s\n", __func__,
					tp_proc_node[i].name);
				ret = -ENODEV;
			} else
				tp_proc_node[i].is_created = true;
		}
	}

	/*create debug_info node*/
	init_debug_info_proc(ts);
	return ret;
}

void remove_touchpanel_proc(struct touchpanel_data *ts)
{
	char name[TP_NAME_SIZE_MAX];

	if (ts->tp_index == 0)
		snprintf(name, TP_NAME_SIZE_MAX, "%s", TPD_DEVICE);

	else
		snprintf(name, TP_NAME_SIZE_MAX, "%s%d", TPD_DEVICE, ts->tp_index);

	if (ts && ts->prEntry_tp)
		remove_proc_subtree(name, NULL);
}
