/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2017-2018, The Linux Foundation. All rights reserved.
 */

#ifndef _OPLUS_CAM_ACTUATOR_CORE_H_
#define _OPLUS_CAM_ACTUATOR_CORE_H_

#include "cam_actuator_dev.h"


void cam_actuator_i2c_modes_util_oem(
	struct camera_io_master *io_master_info,
	struct i2c_settings_list *i2c_list);

void cam_actuator_poll_setting_update(struct cam_actuator_ctrl_t *a_ctrl);
void cam_actuator_poll_setting_apply(struct cam_actuator_ctrl_t *a_ctrl);

int32_t oplus_cam_actuator_power_up(struct cam_actuator_ctrl_t *a_ctrl);
int32_t cam_actuator_check_firmware(struct cam_actuator_ctrl_t *a_ctrl);
int32_t cam_actuator_update_pid(struct cam_actuator_ctrl_t *a_ctrl);
int RamWriteByte(struct cam_actuator_ctrl_t *a_ctrl,
uint32_t addr, uint32_t data, unsigned short mdelay);


#endif /* _CAM_ACTUATOR_CORE_H_ */
