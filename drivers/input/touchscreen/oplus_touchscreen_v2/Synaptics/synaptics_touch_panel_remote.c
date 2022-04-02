// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/gpio.h>
#include <linux/uaccess.h>
#include <linux/cdev.h>
#include "synaptics_touch_panel_remote.h"

#define CHAR_DEVICE_NAME "rmi"
#define DEVICE_CLASS_NAME "rmidev"
#define DEV_NUMBER 1
#define REG_ADDR_LIMIT 0xFFFF

#define MASK_8BIT 0xFF;
#define SYN_I2C_RETRY_TIMES 3;
#define BUFFER_SIZE 252

static int remote_rmi4_i2c_read(struct rmidev_data *rmidev_data,
				unsigned short addr, unsigned char *data, unsigned short length);
static int remote_rmi4_i2c_write(struct rmidev_data *rmidev_data,
				 unsigned short addr, unsigned char *data, unsigned short length);
static int remote_rmit_set_page(struct rmidev_data *rmidev_data,
				unsigned int address);
static int remote_rmit_put_page(struct rmidev_data *rmidev_data);

static struct i2c_client *remote_rmi4_get_i2c_client(
	struct rmidev_data *rmidev_data);

static struct remotepanel_data *remote_free_panel_data(
	struct remotepanel_data *pdata);

static struct i2c_client *remote_rmi4_get_i2c_client(
	struct rmidev_data *rmidev_data)
{
	struct rmidev_data *dev_data = (struct rmidev_data *)rmidev_data->rmi_dev->data;
	return dev_data->pdata->client;
}

static int remote_rmit_set_page(struct rmidev_data *rmidev_data,
				unsigned int address)
{
	struct i2c_client *i2c_client = remote_rmi4_get_i2c_client(rmidev_data);
	unsigned char retry;
	unsigned char *buf = NULL;
	struct i2c_msg msg[] = {
		{
			.addr = i2c_client->addr,
			.flags = 0,
			.len = 2,
			.buf = buf,
		}
	};

	buf = kzalloc(2, GFP_KERNEL | GFP_DMA);

	if (!buf) {
		pr_err("kzalloc buf failed.\n");
		return -ENOMEM;
	}

	buf[0] = 0xff;
	buf[1] = ((address >> 8) & 0xFF);

	msg[0].buf = buf;

	for (retry = 0; retry < 2; retry++) {
		if (i2c_transfer(i2c_client->adapter, msg, 1) == 1)
			break;

		msleep(20);
	}

	if (retry == 2) {
		kfree(buf);
		buf = NULL;
		return -EIO;
	}

	kfree(buf);
	buf = NULL;

	return 0;
}

static int remote_rmit_put_page(struct rmidev_data *rmidev_data)
{
	struct i2c_client *i2c_client = remote_rmi4_get_i2c_client(rmidev_data);
	unsigned char retry;
	unsigned char *buf = NULL;
	struct i2c_msg msg[] = {
		{
			.addr = i2c_client->addr,
			.flags = 0,
			.len = 2,
			.buf = buf,
		}
	};

	buf = kzalloc(2, GFP_KERNEL | GFP_DMA);

	if (!buf) {
		pr_err("kzalloc buf failed.\n");
		return -ENOMEM;
	}

	buf[0] = 0xff;
	buf[1] = 0x00;

	msg[0].buf = buf;

	for (retry = 0; retry < 2; retry++) {
		if (i2c_transfer(i2c_client->adapter, msg, 1) == 1)
			break;

		msleep(20);
	}

	if (retry == 2) {
		kfree(buf);
		buf = NULL;
		return -EIO;
	}

	kfree(buf);
	buf = NULL;

	return 0;
}

int remote_rmi4_i2c_read(struct rmidev_data *rmidev_data, unsigned short addr,
			 unsigned char *data, unsigned short length)
{
	int retval;
	unsigned char retry;
	unsigned char *buf = NULL;
	unsigned char *read_buf = NULL;
	struct i2c_client *i2c_client = remote_rmi4_get_i2c_client(rmidev_data);
	struct i2c_msg msg[] = {
		{
			.addr = i2c_client->addr,
			.flags = 0,
			.len = 1,
			.buf = buf,
		},
		{
			.addr = i2c_client->addr,
			.flags = I2C_M_RD,
			.len = length,
			.buf = read_buf,
		},
	};

	if (!rmidev_data) {
		pr_err("rmidev_data is null\n");
		return -1;
	}

	buf = kzalloc(1, GFP_KERNEL | GFP_DMA);

	if (!buf) {
		pr_err("kzalloc buf failed.\n");
		return -ENOMEM;
	}

	read_buf = kzalloc(length, GFP_KERNEL | GFP_DMA);

	if (!read_buf) {
		pr_err("kzalloc read_buf failed.\n");
		kfree(buf);
		buf = NULL;
		return -ENOMEM;
	}

	*buf = addr & 0xff;

	msg[0].buf = buf;
	msg[1].buf = read_buf;

	retval = remote_rmit_set_page(rmidev_data, addr);

	if (retval < 0)
		goto exit;

	for (retry = 0; retry < 2; retry++) {
		if (i2c_transfer(i2c_client->adapter, msg, 2) == 2) {
			retval = length;
			break;
		}

		msleep(20);
	}

	if (retry == 2) {
		retval = -EIO;
		goto exit;
	}

	memcpy(data, read_buf, length);

exit:
	kfree(buf);
	buf = NULL;
	kfree(read_buf);
	read_buf = NULL;
	remote_rmit_put_page(rmidev_data);

	return retval;
}

int remote_rmi4_i2c_write(struct rmidev_data *rmidev_data,
			  unsigned short addr, unsigned char *data, unsigned short length)
{
	int retval;
	unsigned char retry;
	unsigned char *buf = NULL;
	struct i2c_client *i2c_client = remote_rmi4_get_i2c_client(rmidev_data);
	struct i2c_msg msg[] = {
		{
			.addr = i2c_client->addr,
			.flags = 0,
			.len = length + 1,
			.buf = buf,
		}
	};

	if (!rmidev_data) {
		pr_err("rmidev_data is null\n");
		return -1;
	}

	buf = kzalloc(length + 1, GFP_KERNEL | GFP_DMA);

	if (buf == NULL) {
		pr_err("buf info kzalloc error\n");
		return -ENOMEM;
	}

	msg[0].buf = buf;
	retval = remote_rmit_set_page(rmidev_data, addr);

	if (retval < 0)
		goto exit;

	buf[0] = addr & 0xff;
	memcpy(&buf[1], &data[0], length);

	for (retry = 0; retry < 2; retry++) {
		if (i2c_transfer(i2c_client->adapter, msg, 1) == 1) {
			retval = length;
			break;
		}

		msleep(20);
	}

	msleep(10);

	if (retry == 2) {
		kfree(buf);
		buf = NULL;
		retval = -EIO;
	}

exit:
	remote_rmit_put_page(rmidev_data);
	kfree(buf);
	buf = NULL;

	return retval;
}

int remote_rmi4_i2c_enable(struct rmidev_data *dev_data, bool enable)
{
	if (enable)
		*(dev_data->pdata->enable_remote) = 0;

	else
		*(dev_data->pdata->enable_remote) = 1;

	return 0;
}


/*
 * rmidev_llseek - used to set up register address
 *
 * @filp: file structure for seek
 * @off: offset
 *   if whence == SEEK_SET,
 *     high 16 bits: page address
 *     low 16 bits: register address
 *   if whence == SEEK_CUR,
 *     offset from current position
 *   if whence == SEEK_END,
 *     offset from end position (0xFFFF)
 * @whence: SEEK_SET, SEEK_CUR, or SEEK_END
 */
static loff_t rmidev_llseek(struct file *filp, loff_t off, int whence)
{
	loff_t newpos;
	struct rmidev_data *dev_data = filp->private_data;

	if (IS_ERR(dev_data)) {
		pr_err("%s: Pointer of char device data is invalid", __func__);
		return -EBADF;
	}


	mutex_lock(&(dev_data->file_mutex));

	switch (whence) {
	case SEEK_SET:
		newpos = off;
		break;

	case SEEK_CUR:
		newpos = filp->f_pos + off;
		break;

	case SEEK_END:
		newpos = REG_ADDR_LIMIT + off;
		break;

	default:
		newpos = -EINVAL;
		goto clean_up;
	}

	if (newpos < 0 || newpos > REG_ADDR_LIMIT) {
		pr_err("%s: New position 0x%04x is invalid\n",
		       __func__, (unsigned int)newpos);
		newpos = -EINVAL;
		goto clean_up;
	}

	filp->f_pos = newpos;

clean_up:
	mutex_unlock(&(dev_data->file_mutex));

	return newpos;
}

/*
 * rmidev_read: - use to read data from rmi device
 *
 * @filp: file structure for read
 * @buf: user space buffer pointer
 * @count: number of bytes to read
 * @f_pos: offset (starting register address)
 */
static ssize_t rmidev_read(struct file *filp, char __user *buf,
			   size_t count, loff_t *f_pos)
{
	ssize_t retval;
	unsigned char *tmpbuf = NULL;
	struct rmidev_data *dev_data = filp->private_data;

	if (IS_ERR(dev_data)) {
		pr_err("%s: Pointer of char device data is invalid", __func__);
		return -EBADF;
	}

	if (count == 0)
		return 0;

	if (count > (REG_ADDR_LIMIT - *f_pos))
		count = REG_ADDR_LIMIT - *f_pos;

	tmpbuf = kzalloc(count + 1, GFP_KERNEL);

	if (tmpbuf == NULL) {
		pr_err("buf info kzalloc error\n");
		return -ENOMEM;
	}

	mutex_lock(dev_data->pdata->pmutex);
	mutex_lock(&(dev_data->file_mutex));

	retval = remote_rmi4_i2c_read(
			 dev_data,
			 *f_pos,
			 tmpbuf,
			 count);

	if (retval < 0)
		goto clean_up;

	if (copy_to_user(buf, tmpbuf, count))
		retval = -EFAULT;

	else
		*f_pos += retval;

clean_up:
	mutex_unlock(&(dev_data->file_mutex));
	mutex_unlock(dev_data->pdata->pmutex);
	kfree(tmpbuf);
	tmpbuf = NULL;

	return retval;
}

/*
 * rmidev_write: - used to write data to rmi device
 *
 * @filep: file structure for write
 * @buf: user space buffer pointer
 * @count: number of bytes to write
 * @f_pos: offset (starting register address)
 */
static ssize_t rmidev_write(struct file *filp, const char __user *buf,
			    size_t count, loff_t *f_pos)
{
	ssize_t retval;
	unsigned char *tmpbuf = NULL;
	struct rmidev_data *dev_data = filp->private_data;

	if (IS_ERR(dev_data)) {
		pr_err("%s: Pointer of char device data is invalid", __func__);
		return -EBADF;
	}

	if (count == 0)
		return 0;

	if (count > (REG_ADDR_LIMIT - *f_pos))
		count = REG_ADDR_LIMIT - *f_pos;

	tmpbuf = kzalloc(count + 1, GFP_KERNEL);

	if (tmpbuf == NULL) {
		pr_err("buf info kzalloc error\n");
		return -ENOMEM;
	}

	if (copy_from_user(tmpbuf, buf, count)) {
		retval = -EFAULT;
		goto clean_up;
	}

	mutex_lock(dev_data->pdata->pmutex);
	mutex_lock(&(dev_data->file_mutex));

	retval = remote_rmi4_i2c_write(
			 dev_data,
			 *f_pos,
			 tmpbuf,
			 count);

	if (retval >= 0)
		*f_pos += retval;

	mutex_unlock(&(dev_data->file_mutex));
	mutex_unlock(dev_data->pdata->pmutex);

clean_up:
	kfree(tmpbuf);
	tmpbuf = NULL;
	return retval;
}

static int rmidev_create_attr(struct rmidev_data *dev_data, bool create)
{
	int retval = 0;

	return retval;
}

/*
 * rmidev_open: enable access to rmi device
 * @inp: inode struture
 * @filp: file structure
 */
static int rmidev_open(struct inode *inp, struct file *filp)
{
	int retval = 0;
	struct rmidev_data *dev_data =
		container_of(inp->i_cdev, struct rmidev_data, main_dev);

	rmidev_create_attr(dev_data, true);

	filp->private_data = dev_data;

	mutex_lock(&(dev_data->file_mutex));
	*(dev_data->pdata->enable_remote) = 1;
	/*remote_rmi4_i2c_enable(false);*/
	pr_err("%s: Attention interrupt disabled\n", __func__);
	disable_irq_nosync(dev_data->pdata->irq);

	if (dev_data->ref_count < 1)
		dev_data->ref_count++;

	else
		retval = -EACCES;

	mutex_unlock(&(dev_data->file_mutex));

	return retval;
}

/*
 * rmidev_release: - release access to rmi device
 * @inp: inode structure
 * @filp: file structure
 */
static int rmidev_release(struct inode *inp, struct file *filp)
{
	struct rmidev_data *dev_data =
		container_of(inp->i_cdev, struct rmidev_data, main_dev);

	rmidev_create_attr(dev_data, false);

	mutex_lock(&(dev_data->file_mutex));

	dev_data->ref_count--;

	if (dev_data->ref_count < 0)
		dev_data->ref_count = 0;

	remote_rmi4_i2c_enable(dev_data, true);
	pr_err("%s: Attention interrupt enabled\n", __func__);
	enable_irq(dev_data->pdata->irq);
	mutex_unlock(&(dev_data->file_mutex));

	return 0;
}

static const struct file_operations rmidev_fops = {
	.owner = THIS_MODULE,
	.llseek = rmidev_llseek,
	.read = rmidev_read,
	.write = rmidev_write,
	.open = rmidev_open,
	.release = rmidev_release,
};

static void rmidev_device_cleanup(struct rmidev_data *dev_data)
{
	dev_t devno;

	if (dev_data) {
		devno = dev_data->main_dev.dev;

		if (dev_data->device_class)
			device_destroy(dev_data->device_class, devno);

		cdev_del(&dev_data->main_dev);
		unregister_chrdev_region(devno, 1);
		remote_free_panel_data(dev_data->pdata);

		pr_err("%s: rmidev device removed\n",  __func__);
	}

	return;
}

static char *rmi_char_devnode(struct device *dev, umode_t *mode)
{
	if (!mode)
		return NULL;

	*mode = (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);

	return kasprintf(GFP_KERNEL, "rmi/%s", dev_name(dev));
}

static int rmidev_create_device_class(struct rmidev_data *dev_data)
{
	dev_data->device_class = class_create(THIS_MODULE, DEVICE_CLASS_NAME);

	if (IS_ERR(dev_data->device_class)) {
		pr_err("%s: Failed to create /dev/%s\n",
		       __func__, CHAR_DEVICE_NAME);
		return -ENODEV;
	}

	dev_data->device_class->devnode = rmi_char_devnode;

	return 0;
}

struct remotepanel_data *remote_alloc_panel_data(void)
{
	return kzalloc(sizeof(struct remotepanel_data), GFP_KERNEL);
}
EXPORT_SYMBOL(remote_alloc_panel_data);

static struct remotepanel_data *remote_free_panel_data(
	struct remotepanel_data *pdata)
{
	if (pdata)
		kfree(pdata);

	pdata = NULL;
	return NULL;
}

/*int rmidev_init_device(void)*/
int register_remote_device(struct remotepanel_data *pdata)
{
	int retval;
	dev_t dev_no;
	struct rmidev_data *dev_data = NULL;
	struct device *device_ptr = NULL;
	struct rmidev_handle *rmidev = NULL;

	if (pdata == NULL) {
		pr_err("%s:pdata is null\n", __func__);
		return -1;
	}

	if (rmidev) {
		pr_err("%s:remote device has register already null\n", __func__);
		return -1;
	}

	rmidev = kzalloc(sizeof(*rmidev), GFP_KERNEL);

	if (!rmidev) {
		retval = -ENOMEM;
		goto err_rmidev;
	}

	dev_data = kzalloc(sizeof(*dev_data), GFP_KERNEL);

	if (!dev_data) {
		retval = -ENOMEM;
		goto err_device_class;
	}

	retval = rmidev_create_device_class(dev_data);

	if (retval < 0)
		goto err_device_class;

	if (dev_data->rmidev_major_num) {
		dev_no = MKDEV(dev_data->rmidev_major_num, DEV_NUMBER);
		retval = register_chrdev_region(dev_no, 1, CHAR_DEVICE_NAME);

		if (retval < 0)
			goto err_device_region;

	} else {
		retval = alloc_chrdev_region(&dev_no, 0, 1, CHAR_DEVICE_NAME);

		if (retval < 0)
			goto err_device_region;

		dev_data->rmidev_major_num = MAJOR(dev_no);
	}

	dev_data->pdata = pdata;
	pdata->p_rmidev_data = dev_data;

	mutex_init(&dev_data->file_mutex);
	dev_data->rmi_dev = rmidev;
	rmidev->data = dev_data;

	cdev_init(&dev_data->main_dev, &rmidev_fops);

	retval = cdev_add(&dev_data->main_dev, dev_no, 1);

	if (retval < 0)
		goto err_char_device;

	dev_set_name(&rmidev->dev, "rmidev%d", MINOR(dev_no));

	device_ptr = device_create(dev_data->device_class, NULL, dev_no,
				   NULL, CHAR_DEVICE_NAME"%d", MINOR(dev_no));

	if (IS_ERR(device_ptr)) {
		dev_err(device_ptr,
			"%s: Failed to create rmi char device\n", __func__);
		retval = -ENODEV;
		goto err_char_device;
	}

	return 0;

err_char_device:
	remote_free_panel_data(dev_data->pdata);
	rmidev_device_cleanup(dev_data);

	unregister_chrdev_region(dev_no, 1);

err_device_region:
	class_destroy(dev_data->device_class);
	kfree(dev_data);
err_device_class:
	kfree(rmidev);
	rmidev = NULL;
err_rmidev:
	return retval;
}
EXPORT_SYMBOL(register_remote_device);

/*void rmidev_remove_device(void)*/
void unregister_remote_device(struct remotepanel_data *pdata)
{
	struct rmidev_data *dev_data = pdata->p_rmidev_data;

	if (!dev_data)
		return;

	if (dev_data) {
		rmidev_device_cleanup(dev_data);
		kfree(dev_data);
	}

	unregister_chrdev_region(dev_data->rmi_dev->dev_no, 1);

	class_destroy(dev_data->device_class);

	kfree(dev_data->rmi_dev);

	return;
}
EXPORT_SYMBOL(unregister_remote_device);
