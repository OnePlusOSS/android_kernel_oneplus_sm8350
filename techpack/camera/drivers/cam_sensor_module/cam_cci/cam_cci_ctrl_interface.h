/* Copyright (c) 2017-2018, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _CAM_CCI_CTRL_INTERFACE_H_
#define _CAM_CCI_CTRL_INTERFACE_H_

//#define USE_CAMERA_CCI_OVERRIDE

extern int32_t cam_cci_control_interface(void*);

enum camera_cci_operations {
    CAMERA_CCI_INIT,
    CAMERA_CCI_RELEASE,
    CAMERA_CCI_READ,
    CAMERA_CCI_WRITE,
};

struct camera_cci_transfer {
    int cmd;
    uint16_t addr;
    uint8_t *data;
    uint16_t count;
};




#endif /* _CAM_CCI_CTRL_INTERFACE_H_ */



