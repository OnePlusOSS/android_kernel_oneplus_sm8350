/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */
/*
 * Copyright (c) 2016-2019, The Linux Foundation. All rights reserved.
 */

#define CAM_OEM_COMMON_OPCODE_BASE              0x8000
#define CAM_GET_OIS_EIS_HALL                    (CAM_COMMON_OPCODE_BASE + 0xD)
#define CAM_WRITE_CALIBRATION_DATA              (CAM_OEM_COMMON_OPCODE_BASE + 0x2)
#define CAM_CHECK_CALIBRATION_DATA              (CAM_OEM_COMMON_OPCODE_BASE + 0x3)
#define CAM_WRITE_AE_SYNC_DATA                  (CAM_OEM_COMMON_OPCODE_BASE + 0x4)
#define CAM_OEM_IO_CMD                          (CAM_OEM_COMMON_OPCODE_BASE + 0x5)
#define CAM_OEM_GET_ID                          (CAM_OEM_COMMON_OPCODE_BASE + 0x6)
#define CAM_GET_DPC_DATA                        (CAM_OEM_COMMON_OPCODE_BASE + 0x7)

#define CAM_OEM_CMD_READ_DEV                    0
#define CAM_OEM_CMD_WRITE_DEV                   1
#define CAM_OEM_OIS_CALIB                       2
#define CAM_OEM_RW_SIZE_MAX                     128

struct cam_oem_i2c_reg_array {
	unsigned int    reg_addr;
	unsigned int    reg_data;
	unsigned int    delay;
	unsigned int    data_mask;
};

struct cam_oem_rw_ctl {
	signed int                cmd_code;
	unsigned long long        cam_regs_ptr;
	unsigned int              slave_addr;
	unsigned int              reg_data_type;
	signed int                reg_addr_type;
	signed short              num_bytes;
};

/*add for get hall dat for EIS*/
#define HALL_MAX_NUMBER 12
struct ois_hall_type {
	unsigned int    dataNum;
	unsigned int    mdata[HALL_MAX_NUMBER];
	unsigned int    timeStamp;
};

#define VIDIOC_CAM_FTM_POWNER_UP 0
#define VIDIOC_CAM_FTM_POWNER_DOWN 1
#define VIDIOC_CAM_SENSOR_STATR 0x9000
#define VIDIOC_CAM_SENSOR_STOP 0x9001
