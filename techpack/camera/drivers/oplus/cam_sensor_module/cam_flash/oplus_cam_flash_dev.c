// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2019, The Linux Foundation. All rights reserved.
 */

#include <linux/module.h>
#ifdef OPLUS_FEATURE_CAMERA_COMMON

#include <linux/proc_fs.h>
#include <linux/time.h>
#include <linux/rtc.h>
struct cam_flash_ctrl *vendor_flash_ctrl = NULL;
struct cam_flash_ctrl *front_flash_ctrl = NULL;
#endif
#include "cam_flash_dev.h"
#include "cam_flash_soc.h"
#include "cam_flash_core.h"
#include "cam_common_util.h"
#ifdef OPLUS_FEATURE_CAMERA_COMMON

#include "cam_res_mgr_api.h"
#endif
#include "oplus_cam_flash_dev.h"

#ifdef OPLUS_FEATURE_CAMERA_COMMON

volatile static int flash_mode;
volatile static int pre_flash_mode;
static ssize_t flash_on_off(struct cam_flash_ctrl *flash_ctrl)
{
	int rc = 1;
	struct timespec ts;
	struct rtc_time tm;
	struct cam_flash_frame_setting flash_data;
	memset(&flash_data, 0, sizeof(flash_data));

	getnstimeofday(&ts);
	rtc_time_to_tm(ts.tv_sec, &tm);
	pr_info("flash_mode %d,%d-%02d-%02d %02d:%02d:%02d.%09lu UTC\n",
		flash_mode,
		tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
		tm.tm_hour, tm.tm_min, tm.tm_sec, ts.tv_nsec);

	if (pre_flash_mode == flash_mode)
		return rc;


	if (pre_flash_mode == 5 && flash_mode == 0) {
		pr_err("camera is opened,not to set flashlight off");
		return rc;
	}

	pre_flash_mode = flash_mode;

	switch (flash_mode) {
	case 0:
		flash_data.led_current_ma[0] = 0;
		flash_data.led_current_ma[1] = 0;
		cam_flash_off(flash_ctrl);
		flash_ctrl->flash_state = CAM_FLASH_STATE_INIT;
		break;

	case 1:
		flash_data.led_current_ma[0] = 110;
		flash_data.led_current_ma[1] = 110;
#ifdef OPLUS_FEATURE_CAMERA_COMMON


		if (vendor_flash_ctrl->flash_current != 0) {
			flash_data.led_current_ma[0] = vendor_flash_ctrl->flash_current;
			flash_data.led_current_ma[1] = vendor_flash_ctrl->flash_current;
		}

#endif
		cam_flash_on(flash_ctrl, &flash_data, 0);
		break;

	case 2:
		flash_data.led_current_ma[0] = 1000;
		flash_data.led_current_ma[1] = 1000;
		cam_flash_on(flash_ctrl, &flash_data, 1);
		break;

	case 3:
		flash_data.led_current_ma[0] = 60;
		flash_data.led_current_ma[1] = 60;
		cam_flash_on(flash_ctrl, &flash_data, 0);
		break;

	default:
		break;
	}

	return rc;
}

static ssize_t flash_proc_write(struct file *filp, const char __user *buff,
				size_t len, loff_t *data)
{
	char buf[8] = {0};
	int rc = 0;

	if (len > 8)
		len = 8;

	if (copy_from_user(buf, buff, len)) {
		pr_err("proc write error.\n");
		return -EFAULT;
	}

	flash_mode = simple_strtoul(buf, NULL, 10);
	rc = flash_on_off(vendor_flash_ctrl);

	if (rc < 0)
		pr_err("%s flash write failed %d\n", __func__, __LINE__);

	return len;
}
static ssize_t flash_proc_read(struct file *filp, char __user *buff,
			       size_t len, loff_t *data)
{
	char value[2] = {0};
	snprintf(value, sizeof(value), "%d", flash_mode);
	return simple_read_from_buffer(buff, len, data, value, 1);
}

static const struct file_operations led_fops = {
	.owner		= THIS_MODULE,
	.read		= flash_proc_read,
	.write		= flash_proc_write,
};

static int flash_proc_init(struct cam_flash_ctrl *flash_ctl)
{
	int ret = 0;
	char proc_flash[16] = "qcom_flash";
	char strtmp[] = "0";
	struct proc_dir_entry *proc_entry;

	if (flash_ctl->flash_name == NULL) {
		pr_err("%s get flash name is NULL %d\n", __func__, __LINE__);
		return -1;

	} else {
		if (strcmp(flash_ctl->flash_name, "pmic") != 0) {
			pr_err("%s get flash name is PMIC ,so return\n", __func__);
			return -1;
		}
	}

	if (flash_ctl->soc_info.index > 0) {
		sprintf(strtmp, "%d", flash_ctl->soc_info.index);
		strcat(proc_flash, strtmp);
	}

	proc_entry = proc_create_data(proc_flash, 0666, NULL, &led_fops, NULL);

	if (proc_entry == NULL) {
		ret = -ENOMEM;
		pr_err("[%s]: Error! Couldn't create qcom_flash proc entry\n", __func__);
	}

	vendor_flash_ctrl = flash_ctl;
	return ret;
}


static ssize_t cam_flash_switch_store(struct device *dev,
				      struct device_attribute *attr, const char *buf,
				      size_t count)
{
	int rc = 0;
	struct cam_flash_ctrl *data = dev_get_drvdata(dev);

	int enable = 0;

	if (kstrtoint(buf, 0, &enable)) {
		pr_err("get val error.\n");
		rc = -EINVAL;
	}

	CAM_ERR(CAM_FLASH, "echo data = %d ", enable);

	flash_mode = enable;
	rc = flash_on_off(data);

	return count;
}

static ssize_t cam_flash_switch_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	return snprintf(buf, 5, "%d\n", flash_mode);
	//return simple_read_from_buffer(buff, 10, buf, value,1);
}

static DEVICE_ATTR(fswitch, 0660, cam_flash_switch_show,
		   cam_flash_switch_store);
#endif

void oplus_cam_flash_proc_init(struct cam_flash_ctrl *flash_ctl,
			       struct platform_device *pdev)
{

	if (flash_proc_init(flash_ctl) < 0)
		device_create_file(&pdev->dev, &dev_attr_fswitch);
}


