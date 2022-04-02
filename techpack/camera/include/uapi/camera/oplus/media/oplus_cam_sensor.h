/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */
/*
 * Copyright (c) 2016-2020, The Linux Foundation. All rights reserved.
 */

#define FD_DFCT_MAX_NUM 5
#define SG_DFCT_MAX_NUM 299
#define FD_DFCT_NUM_ADDR 0x7678
#define SG_DFCT_NUM_ADDR 0x767A
#define FD_DFCT_ADDR 0x8B00
#define SG_DFCT_ADDR 0x8B10
#define V_ADDR_SHIFT 12
#define H_DATA_MASK 0xFFF80000
#define V_DATA_MASK 0x0007FF80

struct sony_dfct_tbl_t {
	/*---- single static defect ----*/
	int sg_dfct_num;                         /* the number of single static defect*/
	int sg_dfct_addr[SG_DFCT_MAX_NUM];       /* [ u25 ( upper-u13 = x-addr, lower-u12 = y-addr ) ]*/
	/*---- FD static defect ----*/
	int fd_dfct_num;                         /* the number of FD static defect*/
	int fd_dfct_addr[FD_DFCT_MAX_NUM];       /* [ u25 ( upper-u13 = x-addr, lower-u12 = y-addr ) ]*/
} __attribute__((packed));

#define CALIB_DATA_LENGTH         1689
#define WRITE_DATA_MAX_LENGTH     16
#define WRITE_DATA_DELAY          3
#define EEPROM_NAME_LENGTH        64

struct cam_write_eeprom_t {
	unsigned int    cam_id;
	unsigned int    baseAddr;
	unsigned int    calibDataSize;
	unsigned int    isWRP;
	unsigned int    WRPaddr;
	unsigned char calibData[CALIB_DATA_LENGTH];
	char eepromName[EEPROM_NAME_LENGTH];
} __attribute__((packed));

#define EEPROM_CHECK_DATA_MAX_SIZE 196
struct check_eeprom_data_t {
	unsigned int    cam_id;
	unsigned int    checkDataSize;
	unsigned int    startAddr;
	unsigned int    eepromData_checksum;
} __attribute__((packed));

