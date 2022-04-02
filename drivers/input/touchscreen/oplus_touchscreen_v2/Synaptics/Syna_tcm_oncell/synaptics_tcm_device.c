// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include <linux/cdev.h>
#include <linux/gpio.h>
#include <linux/kthread.h>
#include <linux/uaccess.h>
#include "synaptics_tcm_oncell.h"

#define CHAR_DEVICE_NAME "tcm"
#define PLATFORM_DRIVER_NAME "synaptics_tcm"
#define CONCURRENT true

#define DEVICE_IOC_MAGIC 's'
#define DEVICE_IOC_RESET _IO(DEVICE_IOC_MAGIC, 0) /* 0x00007300 */
#define DEVICE_IOC_IRQ _IOW(DEVICE_IOC_MAGIC, 1, int) /* 0x40047301 */
#define DEVICE_IOC_RAW _IOW(DEVICE_IOC_MAGIC, 2, int) /* 0x40047302 */
#define DEVICE_IOC_CONCURRENT _IOW(DEVICE_IOC_MAGIC, 3, int) /* 0x40047303 */

static struct device_hcd *g_device_hcd[TP_SUPPORT_MAX] = {NULL};

static void device_capture_touch_report(struct device_hcd *device_hcd,
					unsigned int count)
{
	int retval;
	unsigned char id;
	unsigned int idx;
	unsigned int size;
	unsigned char *data;
	struct syna_tcm_data *tcm_info = device_hcd->tcm_info;
	static bool report;
	static unsigned int offset;
	static unsigned int remaining_size;

	if (count < 2)
		return;

	data = &device_hcd->resp.buf[0];

	if (data[0] != MESSAGE_MARKER)
		return;

	id = data[1];
	size = 0;

	LOCK_BUFFER(device_hcd->report);

	switch (id) {
	case REPORT_TOUCH:
		if (count >= 4)
			remaining_size = le2_to_uint(&data[2]);

		else {
			report = false;
			goto exit;
		}

		retval = syna_tcm_alloc_mem(&device_hcd->report, remaining_size);

		if (retval < 0) {
			pr_err("Failed to allocate memory for device_hcd->report.buf\n");
			report = false;
			goto exit;
		}

		idx = 4;
		size = count - idx;
		offset = 0;
		report = true;
		break;

	case STATUS_CONTINUED_READ:
		if (report == false)
			goto exit;

		if (count >= 2) {
			idx = 2;
			size = count - idx;
		}

		break;

	default:
		goto exit;
	}

	if (size) {
		size = MIN(size, remaining_size);
		retval = tp_memcpy(&device_hcd->report.buf[offset],
				   device_hcd->report.buf_size - offset,
				   &data[idx],
				   count - idx,
				   size);

		if (retval < 0) {
			pr_err("Failed to copy touch report data\n");
			report = false;
			goto exit;

		} else {
			offset += size;
			remaining_size -= size;
			device_hcd->report.data_length += size;
		}
	}

	if (remaining_size)
		goto exit;

	LOCK_BUFFER(tcm_info->report.buffer);

	tcm_info->report.buffer.buf = device_hcd->report.buf;
	tcm_info->report.buffer.buf_size = device_hcd->report.buf_size;
	tcm_info->report.buffer.data_length = device_hcd->report.data_length;

	device_hcd->report_touch(tcm_info);

	UNLOCK_BUFFER(tcm_info->report.buffer);

	report = false;

exit:
	UNLOCK_BUFFER(device_hcd->report);

	return;
}

static int device_capture_touch_report_config(struct device_hcd *device_hcd,
		unsigned int count)
{
	int retval;
	unsigned int size;
	unsigned char *data;
	struct syna_tcm_data *tcm_info = device_hcd->tcm_info;

	if (device_hcd->raw_mode) {
		if (count < 3) {
			pr_err("Invalid write data\n");
			return -EINVAL;
		}

		size = le2_to_uint(&device_hcd->out.buf[1]);

		if (count - 3 < size) {
			pr_err("Incomplete write data\n");
			return -EINVAL;
		}

		if (!size)
			return 0;

		data = &device_hcd->out.buf[3];

	} else {
		size = count - 1;

		if (!size)
			return 0;

		data = &device_hcd->out.buf[1];
	}

	LOCK_BUFFER(tcm_info->config);

	retval = syna_tcm_alloc_mem(&tcm_info->config, size);

	if (retval < 0) {
		pr_err("Failed to allocate memory for tcm_info->config.buf\n");
		UNLOCK_BUFFER(tcm_info->config);
		return retval;
	}

	retval = tp_memcpy(tcm_info->config.buf,
			   tcm_info->config.buf_size,
			   data,
			   size,
			   size);

	if (retval < 0) {
		pr_err("Failed to copy touch report config data\n");
		UNLOCK_BUFFER(tcm_info->config);
		return retval;
	}

	tcm_info->config.data_length = size;

	UNLOCK_BUFFER(tcm_info->config);

	return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
static long device_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
#else
#ifdef HAVE_UNLOCKED_IOCTL
static long device_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
#else
static int device_ioctl(struct inode *inp, struct file *filp, unsigned int cmd,
			unsigned long arg)
#endif
#endif
{
	int retval = 0;
	struct device_hcd *device_hcd  = NULL;
	struct syna_tcm_data *tcm_info = NULL;

	device_hcd = filp->private_data;
	tcm_info = device_hcd->tcm_info;

	pr_info("%s: 0x%x\n", __func__, cmd);

	mutex_lock(&device_hcd->extif_mutex);

	switch (cmd) {
	case DEVICE_IOC_RESET:
		retval = device_hcd->reset(tcm_info);
		break;

	case DEVICE_IOC_IRQ:
		if (arg == 0) {
			if (device_hcd->flag == 1) {
				disable_irq(device_hcd->irq);
				device_hcd->flag = 0;
			}

		} else if (arg == 1) {
			if (device_hcd->flag == 0) {
				enable_irq(device_hcd->irq);
				device_hcd->flag = 1;
			}
		}

		break;

	case DEVICE_IOC_RAW:
		if (arg == 0)
			device_hcd->raw_mode = false;

		else if (arg == 1)
			device_hcd->raw_mode = true;

		break;

	case DEVICE_IOC_CONCURRENT:
		if (arg == 0)
			device_hcd->concurrent = false;

		else if (arg == 1)
			device_hcd->concurrent = true;

		break;

	default:
		retval = -ENOTTY;
		break;
	}

	mutex_unlock(&device_hcd->extif_mutex);

	return retval;
}

static loff_t device_llseek(struct file *filp, loff_t off, int whence)
{
	return -EINVAL;
}

static ssize_t device_read(struct file *filp, char __user *buf,
			   size_t count, loff_t *f_pos)
{
	int retval;
	struct device_hcd *device_hcd  = NULL;
	struct syna_tcm_data *tcm_info = NULL;

	if (count == 0)
		return 0;

	device_hcd = filp->private_data;
	tcm_info = device_hcd->tcm_info;

	mutex_lock(&device_hcd->extif_mutex);

	LOCK_BUFFER(device_hcd->resp);

	if (device_hcd->raw_mode) {
		retval = syna_tcm_alloc_mem(&device_hcd->resp, count);

		if (retval < 0) {
			pr_err("Failed to allocate memory for device_hcd->resp.buf\n");
			UNLOCK_BUFFER(device_hcd->resp);
			goto exit;
		}

		retval = device_hcd->read_message(tcm_info,
						  device_hcd->resp.buf,
						  count);

		if (retval < 0) {
			pr_err("Failed to read message\n");
			UNLOCK_BUFFER(device_hcd->resp);
			goto exit;
		}

	} else {
		if (count != device_hcd->resp.data_length) {
			pr_err("Invalid length information\n");
			UNLOCK_BUFFER(device_hcd->resp);
			retval = -EINVAL;
			goto exit;
		}
	}

	if (copy_to_user(buf, device_hcd->resp.buf, count)) {
		pr_err("Failed to copy data to user space\n");
		UNLOCK_BUFFER(device_hcd->resp);
		retval = -EINVAL;
		goto exit;
	}

	if (!device_hcd->concurrent)
		goto skip_concurrent;

	if (device_hcd->report_touch == NULL) {
		pr_err("Unable to report touch\n");
		device_hcd->concurrent = false;
	}

	if (device_hcd->raw_mode)
		device_capture_touch_report(device_hcd, count);

skip_concurrent:
	UNLOCK_BUFFER(device_hcd->resp);

	retval = count;

exit:
	mutex_unlock(&device_hcd->extif_mutex);

	return retval;
}

static ssize_t device_write(struct file *filp, const char __user *buf,
			    size_t count, loff_t *f_pos)
{
	int retval;
	struct device_hcd *device_hcd  = NULL;
	struct syna_tcm_data *tcm_info = NULL;

	if (count == 0)
		return 0;

	device_hcd = filp->private_data;
	tcm_info = device_hcd->tcm_info;

	mutex_lock(&device_hcd->extif_mutex);

	LOCK_BUFFER(device_hcd->out);

	retval = syna_tcm_alloc_mem(&device_hcd->out, count == 1 ? count + 1 : count);

	if (retval < 0) {
		pr_err("Failed to allocate memory for device_hcd->out.buf\n");
		UNLOCK_BUFFER(device_hcd->out);
		goto exit;
	}

	if (copy_from_user(device_hcd->out.buf, buf, count)) {
		pr_err("Failed to copy data from user space\n");
		UNLOCK_BUFFER(device_hcd->out);
		retval = -EINVAL;
		goto exit;
	}

	LOCK_BUFFER(device_hcd->resp);

	pr_info("%s: cmd 0x%x\n", __func__, device_hcd->out.buf[0]);

	if (device_hcd->raw_mode) {
		retval = device_hcd->write_message(tcm_info,
						   device_hcd->out.buf[0],
						   &device_hcd->out.buf[1],
						   count == 1 ? count : count - 1,
						   NULL,
						   NULL,
						   NULL,
						   0);

	} else {
		mutex_lock(&tcm_info->reset_mutex);
		retval = device_hcd->write_message(tcm_info,
						   device_hcd->out.buf[0],
						   &device_hcd->out.buf[1],
						   count == 1 ? count : count - 1,
						   &device_hcd->resp.buf,
						   &device_hcd->resp.buf_size,
						   &device_hcd->resp.data_length,
						   0);
		mutex_unlock(&tcm_info->reset_mutex);
	}

	if (device_hcd->out.buf[0] == CMD_ERASE_FLASH)
		msleep(500);

	if (retval < 0) {
		pr_err("Failed to write command 0x%02x\n",
		       device_hcd->out.buf[0]);
		UNLOCK_BUFFER(device_hcd->resp);
		UNLOCK_BUFFER(device_hcd->out);
		goto exit;
	}

	if (count && device_hcd->out.buf[0] == CMD_SET_TOUCH_REPORT_CONFIG) {
		retval = device_capture_touch_report_config(device_hcd, count);

		if (retval < 0)
			pr_err("Failed to capture touch report config\n");
	}

	UNLOCK_BUFFER(device_hcd->out);

	if (device_hcd->raw_mode)
		retval = count;

	else
		retval = device_hcd->resp.data_length;

	UNLOCK_BUFFER(device_hcd->resp);

exit:
	mutex_unlock(&device_hcd->extif_mutex);

	return retval;
}

static int device_open(struct inode *inode, struct file *filp)
{
	int retval;
	struct device_hcd *device_hcd =
		container_of(inode->i_cdev, struct device_hcd, char_dev);

	filp->private_data = device_hcd;

	mutex_lock(&device_hcd->extif_mutex);

	if (device_hcd->ref_count < 1) {
		device_hcd->ref_count++;
		retval = 0;
	} else
		retval = -EACCES;

	device_hcd->flag = 1;

	mutex_unlock(&device_hcd->extif_mutex);

	return retval;
}

static int device_release(struct inode *inode, struct file *filp)
{
	struct device_hcd *device_hcd =
		container_of(inode->i_cdev, struct device_hcd, char_dev);

	mutex_lock(&device_hcd->extif_mutex);

	if (device_hcd->ref_count)
		device_hcd->ref_count--;

	mutex_unlock(&device_hcd->extif_mutex);

	return 0;
}

static char *device_devnode(struct device *dev, umode_t *mode)
{
	if (!mode)
		return NULL;

	*mode = (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);

	return kasprintf(GFP_KERNEL, "%s/%s", PLATFORM_DRIVER_NAME,
			 dev_name(dev));
}

static int device_create_class(struct device_hcd *device_hcd)
{
	if (device_hcd->class != NULL)
		return 0;

	device_hcd->class = class_create(THIS_MODULE, PLATFORM_DRIVER_NAME);

	if (IS_ERR(device_hcd->class)) {
		pr_err("Failed to create class\n");
		return -ENODEV;
	}

	device_hcd->class->devnode = device_devnode;

	return 0;
}

static const struct file_operations device_fops = {
	.owner = THIS_MODULE,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
	.unlocked_ioctl = device_ioctl,
#else
#ifdef HAVE_UNLOCKED_IOCTL
	.unlocked_ioctl = device_ioctl,
#ifdef HAVE_COMPAT_IOCTL
	.compat_ioctl = device_ioctl,
#endif
#else
	.ioctl = device_ioctl,
#endif
#endif
	.llseek = device_llseek,
	.read = device_read,
	.write = device_write,
	.open = device_open,
	.release = device_release,
};

static int device_init(struct syna_tcm_data *tcm_info)
{
	int retval;
	dev_t dev_num;
	struct device_hcd *device_hcd = NULL;

	device_hcd = kzalloc(sizeof(*device_hcd), GFP_KERNEL);

	if (!device_hcd) {
		pr_err("Failed to allocate memory for device_hcd\n");
		return -ENOMEM;
	}

	mutex_init(&device_hcd->extif_mutex);
	device_hcd->tp_index = tcm_info->tp_index;

	device_hcd->tcm_info = tcm_info;
	device_hcd->concurrent = CONCURRENT;

	INIT_BUFFER(device_hcd->out, false);
	INIT_BUFFER(device_hcd->resp, false);
	INIT_BUFFER(device_hcd->report, false);

	if (device_hcd->rmidev_major_num) {
		dev_num = MKDEV(device_hcd->rmidev_major_num, 0);
		retval = register_chrdev_region(dev_num, 1,
						PLATFORM_DRIVER_NAME);

		if (retval < 0) {
			pr_err("Failed to register char device\n");
			goto err_register_chrdev_region;
		}

	} else {
		retval = alloc_chrdev_region(&dev_num, 0, 1,
					     PLATFORM_DRIVER_NAME);

		if (retval < 0) {
			pr_err("Failed to allocate char device\n");
			goto err_alloc_chrdev_region;
		}

		device_hcd->rmidev_major_num = MAJOR(dev_num);
	}

	device_hcd->dev_num = dev_num;

	cdev_init(&device_hcd->char_dev, &device_fops);

	retval = cdev_add(&device_hcd->char_dev, dev_num, 1);

	if (retval < 0) {
		pr_err("Failed to add char device\n");
		goto err_add_chardev;
	}

	retval = device_create_class(device_hcd);

	if (retval < 0) {
		pr_err("Failed to create class\n");
		goto err_create_class;
	}

	device_hcd->device = device_create(device_hcd->class, NULL,
					   device_hcd->dev_num, NULL, CHAR_DEVICE_NAME"%d",
					   MINOR(device_hcd->dev_num));

	if (IS_ERR(device_hcd->device)) {
		pr_err("Failed to create device\n");
		retval = -ENODEV;
		goto err_create_device;
	}

	g_device_hcd[device_hcd->tp_index] = device_hcd;
	return 0;

err_create_device:
	class_destroy(device_hcd->class);

err_create_class:
	cdev_del(&device_hcd->char_dev);

err_add_chardev:
	unregister_chrdev_region(dev_num, 1);

err_alloc_chrdev_region:
err_register_chrdev_region:
	RELEASE_BUFFER(device_hcd->report);
	RELEASE_BUFFER(device_hcd->resp);
	RELEASE_BUFFER(device_hcd->out);

	kfree(g_device_hcd[device_hcd->tp_index]);
	g_device_hcd[device_hcd->tp_index] = NULL;

	return retval;
}

struct device_hcd *syna_remote_device_init(struct syna_tcm_data *tcm_info)
{
	device_init(tcm_info);

	return g_device_hcd[tcm_info->tp_index];
}

int syna_remote_device_destory(struct syna_tcm_data *tcm_info)
{
	struct device_hcd *device_hcd = NULL;
	device_hcd = g_device_hcd[tcm_info->tp_index];

	if (!device_hcd)
		return 0;

	device_destroy(device_hcd->class, device_hcd->dev_num);

	class_destroy(device_hcd->class);

	cdev_del(&device_hcd->char_dev);

	unregister_chrdev_region(device_hcd->dev_num, 1);

	RELEASE_BUFFER(device_hcd->report);
	RELEASE_BUFFER(device_hcd->resp);
	RELEASE_BUFFER(device_hcd->out);

	kfree(device_hcd);
	g_device_hcd[tcm_info->tp_index] = NULL;

	return 0;
}

