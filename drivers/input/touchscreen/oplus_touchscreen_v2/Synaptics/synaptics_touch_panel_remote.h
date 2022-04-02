/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef _SYNAPTICS_REDREMOTE_H_
#define _SYNAPTICS_REDREMOTE_H_

struct rmidev_handle {
	dev_t dev_no;
	unsigned short address;
	unsigned int length;
	struct device dev;
	void *data;
};

struct rmidev_data {
	int ref_count;
	struct cdev main_dev;
	struct class *device_class;
	struct mutex file_mutex;
	struct rmidev_handle *rmi_dev;
	struct remotepanel_data *pdata;
	int rmidev_major_num;
};

struct remotepanel_data {
	struct i2c_client *client;
	struct input_dev *input_dev;
	/*    struct input_dev *kpd;*/
	struct mutex *pmutex;
	int irq_gpio;
	unsigned int irq;
	int *enable_remote;
	int tp_index;
	struct rmidev_data *p_rmidev_data;
};
struct remotepanel_data *remote_alloc_panel_data(void);
int register_remote_device(struct remotepanel_data *pdata);
void unregister_remote_device(struct remotepanel_data *pdata);
#endif  /*synaptics_touch_panel_remote*/
