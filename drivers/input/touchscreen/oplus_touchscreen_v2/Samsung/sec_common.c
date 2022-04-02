// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include "sec_common.h"
#include <linux/proc_fs.h>
#include <linux/module.h>

/*******LOG TAG Declear*****************************/
#ifdef TPD_DEVICE
#undef TPD_DEVICE
#define TPD_DEVICE "sec_common"
#else
#define TPD_DEVICE "sec_common"
#endif

/*********** sec tool operate content***********************/
u8 lv1cmd;
static int lv1_readsize;

static ssize_t sec_ts_reg_store(struct device *dev,
				struct device_attribute *attr, const char *buf, size_t size);
static ssize_t sec_ts_regreadsize_store(struct device *dev,
					struct device_attribute *attr, const char *buf, size_t size);
static ssize_t sec_ts_regread_show(struct device *dev,
				   struct device_attribute *attr, char *buf);

static DEVICE_ATTR(sec_ts_reg, (S_IWUSR | S_IWGRP), NULL, sec_ts_reg_store);
static DEVICE_ATTR(sec_ts_regreadsize, (S_IWUSR | S_IWGRP), NULL,
		   sec_ts_regreadsize_store);
static DEVICE_ATTR(sec_ts_regread, S_IRUGO, sec_ts_regread_show, NULL);

static struct attribute *cmd_attributes[] = {
	&dev_attr_sec_ts_reg.attr,
	&dev_attr_sec_ts_regreadsize.attr,
	&dev_attr_sec_ts_regread.attr,
	NULL,
};

static struct attribute_group cmd_attr_group = {
	.attrs = cmd_attributes,
};

static ssize_t sec_ts_reg_store(struct device *dev,
				struct device_attribute *attr, const char *buf, size_t size)
{
	struct touchpanel_data *ts = dev_get_drvdata(dev);

	if (size > 0) {
		mutex_lock(&ts->mutex);
		touch_i2c_write(ts->client, (u8 *)buf, size);
		mutex_unlock(&ts->mutex);
	}

	TPD_DEBUG("%s: 0x%x, 0x%x, size %d\n", __func__, buf[0], buf[1], (int)size);
	return size;
}

static ssize_t sec_ts_regread_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct touchpanel_data *ts = dev_get_drvdata(dev);
	int ret = -1;
	u8 *read_lv1_buff = NULL;
	int length = 0, remain = 0, offset = 0;

	disable_irq_nosync(ts->irq);
	mutex_lock(&ts->mutex);

	read_lv1_buff = tp_devm_kzalloc(dev, lv1_readsize, GFP_KERNEL);

	if (!read_lv1_buff) {
		goto malloc_err;
	}

	remain = lv1_readsize;
	offset = 0;

	do {
		if (remain >= I2C_BURSTMAX) {
			length = I2C_BURSTMAX;

		} else {
			length = remain;
		}

		if (offset == 0) {
			ret = touch_i2c_read_block(ts->client, lv1cmd, length, &read_lv1_buff[offset]);

		} else {
			ret = touch_i2c_read(ts->client, NULL, 0, &read_lv1_buff[offset], length);
		}

		if (ret < 0) {
			TPD_INFO("%s: i2c read %x command, remain =%d\n", __func__, lv1cmd, remain);
			goto i2c_err;
		}

		remain -= length;
		offset += length;
	} while (remain > 0);

	TPD_DEBUG("%s: lv1_readsize = %d\n", __func__, lv1_readsize);
	memcpy(buf, read_lv1_buff, lv1_readsize);

i2c_err:
	tp_devm_kfree(dev, (void **)&read_lv1_buff, lv1_readsize);
malloc_err:
	mutex_unlock(&ts->mutex);
	enable_irq(ts->irq);

	return lv1_readsize;
}

static ssize_t sec_ts_regreadsize_store(struct device *dev,
					struct device_attribute *attr, const char *buf, size_t size)
{
	struct touchpanel_data *ts = dev_get_drvdata(dev);

	mutex_lock(&ts->mutex);

	lv1cmd = buf[0];
	lv1_readsize = ((unsigned int)buf[4] << 24) |
		       ((unsigned int)buf[3] << 16) | ((unsigned int) buf[2] << 8) | ((
					       unsigned int)buf[1] << 0);

	mutex_unlock(&ts->mutex);

	return size;
}
static struct class *g_sec_class[TP_SUPPORT_MAX] = {NULL};

/*create sec debug interfaces*/
void sec_raw_device_init(struct touchpanel_data *ts)
{
	int ret = -1;
	char name[12];
	char dev_name[12];
	struct class *sec_class = NULL;
	struct device *sec_dev = NULL;

	if (!ts->tp_index) {
		snprintf(name, sizeof(name), "%s", "sec");
		snprintf(dev_name, sizeof(dev_name), "%s", "sec_ts");

	} else {
		snprintf(name, sizeof(name), "%s", "sec");
		snprintf(dev_name, sizeof(dev_name), "%s%x", "sec_ts", ts->tp_index);
	}

	sec_class = class_create(THIS_MODULE, name);
	ret = IS_ERR_OR_NULL(sec_class);

	if (ret) {
		TPD_INFO("%s: fail to create class\n", __func__);
		return;
	}

	sec_dev = device_create(sec_class, NULL, 0, ts, dev_name);
	ret = IS_ERR(sec_dev);

	if (ret) {
		TPD_INFO("%s: fail - device_create\n", __func__);
		return;
	}

	ret = sysfs_create_group(&sec_dev->kobj, &cmd_attr_group);

	if (ret < 0) {
		TPD_INFO("%s: fail - sysfs_create_group\n", __func__);
		goto err_sysfs;
	}

	g_sec_class[ts->tp_index] = sec_class;
	TPD_INFO("create debug interface success\n");
	return;
err_sysfs:
	TPD_INFO("%s: fail\n", __func__);
	return;
}
EXPORT_SYMBOL(sec_raw_device_init);

void sec_raw_device_release(struct touchpanel_data *ts)
{
	if (!ts || !g_sec_class[ts->tp_index]) {
		return;
	}

	device_destroy(g_sec_class[ts->tp_index], 0);
	class_destroy(g_sec_class[ts->tp_index]);
	sysfs_remove_group(&ts->dev->kobj, &cmd_attr_group);
	return;
}
EXPORT_SYMBOL(sec_raw_device_release);

/************ sec auto test content*************************/
static int sec_auto_test_irq(struct touchpanel_data *ts,
			     struct sec_auto_test_operations *sec_test_ops,
			     struct auto_testdata *sec_testdata)
{
	int ret = 0;
	int eint_status, eint_count = 0, read_gpio_num = 0;

	ret = sec_test_ops->sec_auto_test_disable_irq(ts->chip_data, true);
	eint_count = 0;
	read_gpio_num = 10;

	while (read_gpio_num--) {
		msleep(5);
		eint_status = gpio_get_value(sec_testdata->irq_gpio);

		if (eint_status == 1) {
			eint_count--;

		} else {
			eint_count++;
		}

		TPD_INFO("%s eint_count = %d  eint_status = %d\n", __func__, eint_count,
			 eint_status);
	}

	return eint_count;
}

int sec_auto_test(struct seq_file *s,  struct touchpanel_data *ts)
{
	uint32_t error_count = 0;
	int ret = 0;

	/*for limit fw*/
	struct auto_test_header *test_head = NULL;
	/*for item limit data*/
	uint32_t *p_data32 = NULL;
	uint32_t item_cnt = 0;
	uint32_t i = 0;

	struct test_item_info *p_test_item_info = NULL;
	struct sec_auto_test_operations *sec_test_ops = NULL;
	struct com_test_data *com_test_data_p = NULL;

	struct auto_testdata sec_testdata = {
		.tx_num = 0,
		.rx_num = 0,
		.fp = NULL,
		.pos = NULL,
		.irq_gpio = -1,
		.tp_fw = 0,
		.fw = NULL,
		.test_item = 0,
	};
	TPD_INFO("%s  is called\n", __func__);

	com_test_data_p = &ts->com_test_data;

	if (!com_test_data_p->limit_fw || !ts || !com_test_data_p->chip_test_ops) {
		TPD_INFO("%s: data is null\n", __func__);
		return -1;
	}

	sec_test_ops = (struct sec_auto_test_operations *)
		       com_test_data_p->chip_test_ops;

	/*decode the limit image*/
	test_head = (struct auto_test_header *)com_test_data_p->limit_fw->data;
	p_data32 = (uint32_t *)(com_test_data_p->limit_fw->data + 16);

	if ((test_head->magic1 != Limit_MagicNum1)
			|| (test_head->magic2 != Limit_MagicNum2)) {
		TPD_INFO("limit image is not generated by oplus\n");
		seq_printf(s, "limit image is not generated by oplus\n");
		return -1;
	}

	TPD_INFO("current test item: %llx\n", test_head->test_item);

	for (i = 0; i < 8 * sizeof(test_head->test_item); i++) {
		if ((test_head->test_item >> i) & 0x01) {
			item_cnt++;
		}
	}

	/*check limit support any item or not*/
	if (!item_cnt) {
		TPD_INFO("no any test item\n");
		error_count++;
		seq_printf(s, "no any test item\n");
		return -1;
	}

	/*init sec_testdata*/
	sec_testdata.fp        = ts->com_test_data.result_data;
	sec_testdata.length    = ts->com_test_data.result_max_len;
	sec_testdata.pos       = &ts->com_test_data.result_cur_len;
	sec_testdata.tx_num    = ts->hw_res.tx_num;
	sec_testdata.rx_num    = ts->hw_res.rx_num;
	sec_testdata.irq_gpio  = ts->hw_res.irq_gpio;
	sec_testdata.tp_fw     = ts->panel_data.tp_fw;
	sec_testdata.fw        = com_test_data_p->limit_fw;
	sec_testdata.test_item = test_head->test_item;

	if (!sec_test_ops->sec_auto_test_preoperation) {
		TPD_INFO("not support syna_test_ops->sec_auto_test_preoperation callback\n");

	} else {
		ret = sec_test_ops->sec_auto_test_preoperation(s, ts->chip_data, &sec_testdata,
				p_test_item_info);

		if (ret < 0) {
			return -1;
		}
	}

	TPD_INFO("%s, step 0: begin to check INT-GND short item\n", __func__);

	ret = sec_auto_test_irq(ts, sec_test_ops, &sec_testdata);

	TPD_INFO("TP EINT PIN direct short test! eint_count ret = %d\n", ret);

	if (ret == 10) {
		TPD_INFO("error :  TP EINT PIN direct short!\n");
		seq_printf(s, "eint_status is low, TP EINT direct short\n");
		return -1;
	}

	ret = sec_test_ops->sec_auto_test_disable_irq(ts->chip_data,
			false);   /*enable interrupt*/

	ret = sec_test_ops->sec_get_verify_result(ts->chip_data);
	TPD_INFO("%s: calibration verify result(0x%02x)\n", __func__, ret);
	seq_printf(s, "calibration verify result(0x%02x)\n", ret);

	p_test_item_info = get_test_item_info(sec_testdata.fw, TYPE_TEST1);

	if (!p_test_item_info) {
		TPD_INFO("item: %d get_test_item_info fail\n", TYPE_TEST1);

	} else {
		ret = sec_test_ops->test1(s, ts->chip_data, &sec_testdata,
					  p_test_item_info);

		if (ret < 0) {
			TPD_INFO("test %d failed! ret is %d\n", TYPE_TEST1, ret);
			error_count++;
			goto ERR_OUT;
		}
	}

	tp_kfree((void **)&p_test_item_info);

	p_test_item_info = get_test_item_info(sec_testdata.fw, TYPE_TEST2);

	if (!p_test_item_info) {
		TPD_INFO("item: %d get_test_item_info fail\n", TYPE_TEST2);

	} else {
		ret = sec_test_ops->test2(s, ts->chip_data, &sec_testdata,
					  p_test_item_info);

		if (ret < 0) {
			TPD_INFO("test %d failed! ret is %d\n", TYPE_TEST2, ret);
			error_count++;
			goto ERR_OUT;
		}
	}

	tp_kfree((void **)&p_test_item_info);

	p_test_item_info = get_test_item_info(sec_testdata.fw, TYPE_TEST3);

	if (!p_test_item_info) {
		TPD_INFO("item: %d get_test_item_info fail\n", TYPE_TEST3);

	} else {
		ret = sec_test_ops->test3(s, ts->chip_data, &sec_testdata,
					  p_test_item_info);

		if (ret < 0) {
			TPD_INFO("synaptics_capacity_test failed! ret is %d\n", ret);
			error_count++;
			goto ERR_OUT;
		}
	}

	tp_kfree((void **)&p_test_item_info);

	p_test_item_info = get_test_item_info(sec_testdata.fw, TYPE_TEST4);

	if (!p_test_item_info) {
		TPD_INFO("item: %d get_test_item_info fail\n", TYPE_TEST4);

	} else {
		ret = sec_test_ops->test4(s, ts->chip_data, &sec_testdata,
					  p_test_item_info);

		if (ret < 0) {
			TPD_INFO("test %d failed! ret is %d\n", TYPE_TEST4, ret);
			error_count++;
			goto ERR_OUT;
		}
	}

	tp_kfree((void **)&p_test_item_info);

	p_test_item_info = get_test_item_info(sec_testdata.fw, TYPE_TEST5);

	if (!p_test_item_info) {
		TPD_INFO("item: %d get_test_item_info fail\n", TYPE_TEST5);

	} else {
		ret = sec_test_ops->test5(s, ts->chip_data, &sec_testdata,
					  p_test_item_info);

		if (ret < 0) {
			TPD_INFO("test %d failed! ret is %d\n", TYPE_TEST5, ret);
			error_count++;
			goto ERR_OUT;
		}
	}

ERR_OUT:
	tp_kfree((void **)&p_test_item_info);

	if (!sec_test_ops->sec_auto_test_endoperation) {
		TPD_INFO("not support sec_auto_test_endoperation callback\n");

	} else {
		sec_test_ops->sec_auto_test_endoperation(s, ts->chip_data, &sec_testdata,
				p_test_item_info);
	}

	seq_printf(s, "FW:0x%llx\n", sec_testdata.tp_fw);
	seq_printf(s, "%d error(s). %s\n", error_count,
		   error_count ? "" : "All test passed.");
	TPD_INFO(" TP auto test %d error(s). %s\n", error_count,
		 error_count ? "" : "All test passed.");
	return error_count;
}
EXPORT_SYMBOL(sec_auto_test);

/*************************************auto test Funtion**************************************/

static int calibrate_fops_read_func(struct seq_file *s, void *v)
{
	struct touchpanel_data *ts = s->private;
	struct sec_proc_operations *sec_ops = (struct sec_proc_operations *)
					      ts->private_data;

	if (!sec_ops->calibrate) {
		return 0;
	}

	disable_irq_nosync(ts->irq);
	mutex_lock(&ts->mutex);

	if (!ts->touch_count) {
		sec_ops->calibrate(s, ts->chip_data);

	} else {
		seq_printf(s, "1 error, release touch on the screen\n");
	}

	mutex_unlock(&ts->mutex);
	enable_irq(ts->irq);

	return 0;
}

static int proc_calibrate_fops_open(struct inode *inode, struct file *file)
{
	return single_open(file, calibrate_fops_read_func, PDE_DATA(inode));
}

static const struct file_operations proc_calibrate_fops = {
	.owner = THIS_MODULE,
	.open  = proc_calibrate_fops_open,
	.read  = seq_read,
	.release = single_release,
};

static int cal_status_read_func(struct seq_file *s, void *v)
{
	bool cal_needed = false;
	struct touchpanel_data *ts = s->private;
	struct sec_proc_operations *sec_ops = (struct sec_proc_operations *)
					      ts->private_data;

	if (!sec_ops->get_cal_status) {
		return 0;
	}

	mutex_lock(&ts->mutex);
	cal_needed = sec_ops->get_cal_status(s, ts->chip_data);

	if (cal_needed) {
		seq_printf(s, "1 error, need do calibration\n");

	} else {
		seq_printf(s, "0 error, calibration data is ok\n");
	}

	mutex_unlock(&ts->mutex);

	return 0;
}

static int proc_cal_status_fops_open(struct inode *inode, struct file *file)
{
	return single_open(file, cal_status_read_func, PDE_DATA(inode));
}

static const struct file_operations proc_cal_status_fops = {
	.owner = THIS_MODULE,
	.open  = proc_cal_status_fops_open,
	.read  = seq_read,
	.release = single_release,
};

static ssize_t proc_curved_control_read(struct file *file,
					char __user *user_buf, size_t count, loff_t *ppos)
{
	ssize_t ret = 0;
	char page[PAGESIZE];
	struct touchpanel_data *ts = PDE_DATA(file_inode(file));
	struct sec_proc_operations *sec_ops = NULL;

	if (!ts) {
		return count;
	}

	sec_ops = (struct sec_proc_operations *)ts->private_data;

	if (!sec_ops->get_curved_rejsize) {
		return count;
	}

	sprintf(page, "%d\n", sec_ops->get_curved_rejsize(ts->chip_data));
	ret = simple_read_from_buffer(user_buf, count, ppos, page, strlen(page));

	return ret;
}

static ssize_t proc_curved_control_write(struct file *file,
		const char __user *buffer, size_t count, loff_t *ppos)
{
	char buf[8] = {0};
	int ret, temp;
	struct touchpanel_data *ts = PDE_DATA(file_inode(file));
	struct sec_proc_operations *sec_ops = NULL;

	if (!ts) {
		return count;
	}

	sec_ops = (struct sec_proc_operations *)ts->private_data;

	if (!sec_ops->set_curved_rejsize) {
		return count;
	}

	if (count > 3) {
		count = 3;
	}

	if (copy_from_user(buf, buffer, count)) {
		TPD_DEBUG("%s: read proc input error.\n", __func__);
		return count;
	}

	sscanf(buf, "%d", &temp);
	mutex_lock(&ts->mutex);
	TPD_INFO("%s: curved range = %d\n", __func__, temp);
	sec_ops->set_curved_rejsize(ts->chip_data, temp);

	if (ts->is_suspended == 0) {
		ret = ts->ts_ops->mode_switch(ts->chip_data, MODE_EDGE, ts->limit_edge);

		if (ret < 0) {
			TPD_INFO("%s, Touchpanel operate mode switch failed\n", __func__);
		}
	}

	mutex_unlock(&ts->mutex);

	return count;
}

static const struct file_operations proc_curved_control_ops = {
	.read  = proc_curved_control_read,
	.write = proc_curved_control_write,
	.open  = simple_open,
	.owner = THIS_MODULE,
};

static ssize_t proc_corner_control_write(struct file *file,
		const char __user *buffer, size_t count, loff_t *ppos)
{
	int ret = 0, para_num = 0;
	char buf[GRIP_PARAMETER_LEN] = {0};
	char para_buf[GRIP_PARAMETER_LEN] = {0};
	struct touchpanel_data *ts = PDE_DATA(file_inode(file));
	struct sec_proc_operations *sec_ops = NULL;

	if (!ts) {
		return count;
	}

	sec_ops = (struct sec_proc_operations *)ts->private_data;

	if (!sec_ops->set_grip_handle) {
		return count;
	}

	if (count > GRIP_PARAMETER_LEN) {
		count = GRIP_PARAMETER_LEN;
	}

	if (copy_from_user(buf, buffer, count)) {
		TPD_DEBUG("%s: read proc input error.\n", __func__);
		return count;
	}

	sscanf(buf, "%d:%s", &para_num, para_buf);
	mutex_lock(&ts->mutex);
	sec_ops->set_grip_handle(ts->chip_data, para_num, para_buf);

	if (ts->is_suspended == 0) {
		ret = ts->ts_ops->mode_switch(ts->chip_data, MODE_EDGE, ts->limit_edge);

		if (ret < 0) {
			TPD_INFO("%s, Touchpanel operate mode switch failed\n", __func__);
		}
	}

	mutex_unlock(&ts->mutex);

	return count;
}

static const struct file_operations proc_corner_control_ops = {
	.write = proc_corner_control_write,
	.open  = simple_open,
	.owner = THIS_MODULE,
};

int sec_create_proc(struct touchpanel_data *ts,
		    struct sec_proc_operations *sec_ops)
{
	int ret = 0;

	/* touchpanel_auto_test interface*/
	struct proc_dir_entry *prEntry_tmp = NULL;
	ts->private_data = sec_ops;

	prEntry_tmp = proc_create_data("calibration", 0666, ts->prEntry_tp,
				       &proc_calibrate_fops, ts);

	if (prEntry_tmp == NULL) {
		ret = -ENOMEM;
		TPD_INFO("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
	}

	prEntry_tmp = proc_create_data("calibration_status", 0666, ts->prEntry_tp,
				       &proc_cal_status_fops, ts);

	if (prEntry_tmp == NULL) {
		ret = -ENOMEM;
		TPD_INFO("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
	}

	prEntry_tmp = proc_create_data("curved_range", 0666, ts->prEntry_tp,
				       &proc_curved_control_ops, ts);

	if (prEntry_tmp == NULL) {
		ret = -ENOMEM;
		TPD_INFO("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
	}

	prEntry_tmp = proc_create_data("grip_handle", 0222, ts->prEntry_tp,
				       &proc_corner_control_ops, ts);

	if (prEntry_tmp == NULL) {
		ret = -ENOMEM;
		TPD_INFO("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
	}

	return ret;
}
EXPORT_SYMBOL(sec_create_proc);

int sec_remove_proc(struct touchpanel_data *ts)
{
	if (!ts) {
		return -EINVAL;
	}

	remove_proc_entry("grip_handle", ts->prEntry_tp);
	remove_proc_entry("curved_range", ts->prEntry_tp);
	remove_proc_entry("calibration_status", ts->prEntry_tp);
	remove_proc_entry("calibration", ts->prEntry_tp);
	return 0;
}
EXPORT_SYMBOL(sec_remove_proc);

MODULE_DESCRIPTION("Touchscreen Samsung Common Interface");
MODULE_LICENSE("GPL");
