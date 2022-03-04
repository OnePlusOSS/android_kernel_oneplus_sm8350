// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include <linux/device.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <linux/completion.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>


#include "fts.h"

#include "../st_common.h"

#include "../../util_interface/touch_interfaces.h"


/*******Part0:LOG TAG Declear************************/

/* fun declaration */
static int st80y_mode_switch(void *chip_data, work_mode mode, int flag);
static int fts_chip_initialization(struct fts_ts_info *info, int init_type);
static int fts_esd_handle(void *chip_data);
static int st80y_get_chip_info(void *chip_data);
static int st80y_production_test_initialization(void *chip_data, u8 type);
static int parseBinFile(void *chip_data, u8 *fw_data, int fw_size,
			Firmware *fwData, int keep_cx);
static int flash_burn(void *chip_data, Firmware fw, int force_burn,
		      int keep_cx);
static int readMpFlag(void *chip_data, u8 *mpflag);
static int readCalibraFlag(void *chip_data, u8 *mpflag);
static int requestSyncFrame(void *chip_data, u8 type);
static int initCore(void *chip_data, int reset_gpio);
static int addErrorIntoList(void *chip_data, u8 *event, int size);
static int checkEcho(void *chip_data, u8 *cmd, int size);
static int writeSysCmd(void *chip_data, u8 sys_cmd, u8 *sett, int size);
static int setScanMode(void *chip_data, u8 mode, u8 settings);
static int readConfig(void *chip_data, u16 offset, u8 *outBuf, int len);
static int pollForEvent(void *chip_data, int *event_to_search,
			int event_bytes, u8 *readdata, int time_to_wait);
static int fts_writeReadU8UX(void *chip_data, u8 cmd, AddrSize addrSize,
			     u64 address, u8 *outBuf, int
			     byteToRead, int hasDummyByte);

static int fts_chip_powercycle(struct fts_ts_info *info);
static int fts_writeReadU8UX(void *chip_data, u8 cmd, AddrSize addrSize,
			     u64 address, u8 *outBuf, int
			     byteToRead, int hasDummyByte);

static int fts_system_reset(void *chip_data);
static int fts_disableInterrupt(void *chip_data);
static int fts_disableInterruptNoSync(void *chip_data);
static int fts_resetDisableIrqCount(void *chip_data);
static int fts_enableInterrupt(void *chip_data);

/**
  * Print an array of byte in a HEX string and attach at the beginning a label.
  * The function allocate memory that should be free outside the function itself
  * @param label string to attach at the beginning
  * @param buff pointer to the byte array that should be printed as HEX string
  * @param count size of buff
  * @param result pointer to the array of characters that compose the HEX final
  * string
  * @return pointer to the array of characters that compose the HEX string,
  * (same address of result)
  * @warning result MUST be allocated outside the function and should be
  * big enough to contain the data converted as HEX!
  */
static char *printHex(char *label, u8 *buff, int count, u8 *result)
{
	int i, offset;

	offset = strlen(label);
	strlcpy(result, label, offset + 1); /* +1 for terminator char */

	for (i = 0; i < count; i++) {
		snprintf(&result[offset], 4, "%02X ", buff[i]);
		/* this append automatically a null terminator char */
		offset += 3;
	}

	return result;
}

/**
  * Convert an array of 2 bytes to a u16, src has LSB first (little endian).
  * @param src pointer to the source byte array
  * @param dst pointer to the destination u16.
  * @return OK
  */
static int u8ToU16(u8 *src, u16 *dst)
{
	*dst = (u16)(((src[1] & 0x00FF) << 8) + (src[0] & 0x00FF));
	return OK;
}

/**
  * Convert an array of bytes to a u32, src has LSB first (little endian).
  * @param src array of bytes to convert
  * @param dst pointer to the destination u32 variable.
  * @return OK
  */
static int u8ToU32(u8 *src, u32 *dst)
{
	*dst = (u32)(((src[3] & 0xFF) << 24) + ((src[2] & 0xFF) << 16) + ((
				src[1] & 0xFF) << 8) + (src[0] & 0xFF));
	return OK;
}

/**
  * Convert a value of an id in a bitmask with a 1 in the position of the value
  * of the id
  * @param id Value of the ID to convert
  * @param mask pointer to the bitmask that will be updated with the value of id
  * @param size dimension in bytes of mask
  * @return OK if success or ERROR_OP_NOT_ALLOW if size of mask is not enough to
  * contain ID
  */
static int fromidtomask(u8 id, u8 *mask, int size)
{
	if (((int)((id) / 8)) < size) {
		TPD_DEBUG("%s: ID = %d Index = %d Position = %d !\n", __func__, id,
			  ((int)((id) / 8)), (id % 8));
		mask[((int)((id) / 8))] |= 0x01 << (id % 8);
		return OK;

	} else {
		TPD_INFO("%s: Bitmask too small! Impossible contain ID = %d %d>=%d! ERROR %08X\n",
			 __func__, id, ((int)((id) / 8)), size, ERROR_OP_NOT_ALLOW);
		return ERROR_OP_NOT_ALLOW;
	}
}


/**
  * Clear the FIFO from any event
  * @return OK if success or an error code which specify the type of error
  */
static int flushFIFO(void *chip_data)
{
	int ret;
	u8 sett = SPECIAL_FIFO_FLUSH;
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

	ret = writeSysCmd(info, SYS_CMD_SPECIAL, &sett, 1);    /* flush the FIFO */

	if (ret < OK) {
		TPD_INFO("flushFIFO: ERROR %08X\n", ret);
		return ret;
	}

	TPD_DEBUG("FIFO flushed!\n");
	return OK;
}

/**
  * Check if an error code is related to an I2C failure
  * @param error error code to check
  * @return 1 if the first level error code is I2C related otherwise 0
  */
static int isI2cError(int error)
{
	if (((error & 0x000000FF) >= (ERROR_BUS_R & 0x000000FF)) &&
			((error & 0x000000FF) <= (ERROR_BUS_O & 0x000000FF)))
		return 1;

	else
		return 0;
}


/**
  * Dump in the kernel log some debug info in case of FW hang
  * @param outBuf (optional)pointer to bytes array where to copy the debug info,
  * if NULL the data will just printed on the kernel log
  * @param size dimension in bytes of outBuf,
  * if > ERROR_DUMP_ROW_SIZE*ERROR_DUMP_COL_SIZE, only the first
  * ERROR_DUMP_ROW_SIZE*ERROR_DUMP_COL_SIZE bytes will be copied
  * @return OK if success or an error code which specify the type of error
  */
static int dumpErrorInfo(void *chip_data, u8 *outBuf, int size)
{
	int ret, i;
	u8 data[ERROR_DUMP_ROW_SIZE * ERROR_DUMP_COL_SIZE] = { 0 };
	u32 sign = 0;

	TPD_DEBUG("%s: Starting dump of error info...\n", __func__);


	ret = fts_writeReadU8UX(chip_data, FTS_CMD_FRAMEBUFFER_R, BITS_16,
				ADDR_ERROR_DUMP,
				data, ERROR_DUMP_ROW_SIZE * ERROR_DUMP_COL_SIZE, DUMMY_FRAMEBUFFER);

	if (ret < OK) {
		TPD_INFO("%s: reading data ERROR %08X\n", __func__,
			 ret);
		return ret;

	} else {
		if (outBuf != NULL) {
			sign = size > ERROR_DUMP_ROW_SIZE * ERROR_DUMP_COL_SIZE ? ERROR_DUMP_ROW_SIZE *
			       ERROR_DUMP_COL_SIZE : size;
			memcpy(outBuf, data, sign);
			TPD_DEBUG("%s: error info copied in the buffer!\n", __func__);
		}

		TPD_INFO("%s: Error Info =\n", __func__);
		u8ToU32(data, &sign);

		if (sign != ERROR_DUMP_SIGNATURE)

			TPD_INFO("%s: Wrong Error Signature! Data may be invalid!\n", __func__);

		else

			TPD_INFO("%s: Error Signature OK! Data are valid!\n", __func__);

		for (i = 0; i < ERROR_DUMP_ROW_SIZE * ERROR_DUMP_COL_SIZE; i++) {
			if (i % ERROR_DUMP_COL_SIZE == 0)
				TPD_INFO(KERN_ERR "\n%s: %d) ", __func__, i / ERROR_DUMP_COL_SIZE);

			TPD_INFO("%02X ", data[i]);
		}

		TPD_INFO("\n");

		TPD_DEBUG("%s: dump of error info FINISHED!\n", __func__);
		return OK;
	}
}


/**
  * Implement recovery strategies to be used when an error event is found
  * while polling the FIFO
  * @param event error event found during the polling
  * @param size size of event
  * @return OK if the error event doesn't require any action or the recovery
  * strategy doesn't have any impact in the possible procedure that trigger the
  * error, otherwise return an error code which specify the kind of error
  * encountered. If ERROR_HANDLER_STOP_PROC the calling function must stop!
  */
static int errorHandler(void *chip_data, u8 *event, int size)
{
	int res = OK;
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

	if (event != NULL && size > 1 && event[0] == EVT_ID_ERROR) {
		TPD_INFO("errorHandler: Starting handling...\n");
		addErrorIntoList(info, event, size);

		switch (event[1]) {    /* TODO: write an error log for undefined command subtype 0xBA */
		case EVT_TYPE_ERROR_ESD:    /* esd */
			res = fts_system_reset(info);

			if (res < OK)
				TPD_INFO("errorHandler: Cannot reset the device ERROR %08X\n", res);

			res = (ERROR_HANDLER_STOP_PROC | res);
			break;

		case EVT_TYPE_ERROR_WATCHDOG:    /* watchdog */
			dumpErrorInfo(info, NULL, 0);
			res = fts_system_reset(info);

			if (res < OK)
				TPD_INFO("errorHandler: Cannot reset the device ERROR %08X\n", res);

			res = (ERROR_HANDLER_STOP_PROC | res);
			break;

		case EVT_TYPE_ERROR_ITO_FORCETOGND:
			TPD_INFO("errorHandler: Force Short to GND!\n");
			break;

		case EVT_TYPE_ERROR_ITO_SENSETOGND:
			TPD_INFO("errorHandler: Sense short to GND!\n");
			break;

		case EVT_TYPE_ERROR_ITO_FORCETOVDD:
			TPD_INFO("errorHandler: Force short to VDD!\n");
			break;

		case EVT_TYPE_ERROR_ITO_SENSETOVDD:
			TPD_INFO("errorHandler: Sense short to VDD!\n");
			break;

		case EVT_TYPE_ERROR_ITO_FORCE_P2P:
			TPD_INFO("errorHandler: Force Pin to Pin Short!\n");
			break;

		case EVT_TYPE_ERROR_ITO_SENSE_P2P:
			TPD_INFO("errorHandler: Sense Pin to Pin Short!\n");
			break;

		case EVT_TYPE_ERROR_ITO_FORCEOPEN:
			TPD_INFO("errorHandler: Force Open !\n");
			break;

		case EVT_TYPE_ERROR_ITO_SENSEOPEN:
			TPD_INFO("errorHandler: Sense Open !\n");
			break;

		case EVT_TYPE_ERROR_ITO_KEYOPEN:
			TPD_INFO("errorHandler: Key Open !\n");
			break;

		default:
			TPD_INFO("errorHandler: No Action taken!\n");
			break;
		}

		TPD_INFO("errorHandler: handling Finished! res = %08X\n", res);
		return res;

	} else {
		TPD_INFO("errorHandler: event Null or not correct size! ERROR %08X\n",
			 ERROR_OP_NOT_ALLOW);
		return ERROR_OP_NOT_ALLOW;
	}
}


/**
  * Add an error event into the Error List
  * @param event error event to add
  * @param size size of event
  * @return OK
  */
static int addErrorIntoList(void *chip_data, u8 *event, int size)
{
	int i = 0;
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;
	ErrorList errors;

	TPD_DEBUG("Adding error in to ErrorList...\n");

	memset(&errors, 0, sizeof(errors));
	errors = info->errors;

	memcpy(&errors.list[errors.last_index * FIFO_EVENT_SIZE], event, size);
	i = FIFO_EVENT_SIZE - size;

	if (i > 0) {
		TPD_DEBUG("Filling last %d bytes of the event with zero...\n", i);
		memset(&errors.list[errors.last_index * FIFO_EVENT_SIZE + size], 0, i);
	}

	TPD_DEBUG("Adding error in to ErrorList... FINISHED!\n");

	errors.count += 1;

	if (errors.count > FIFO_DEPTH)
		TPD_INFO("ErrorList is going in overflow... the first %d event(s) were override!\n",
			 errors.count - FIFO_DEPTH);

	errors.last_index = (errors.last_index + 1) % FIFO_DEPTH;

	return OK;
}

/**
  * Reset the Error List setting the count and last_index to 0.
  * @return OK
  */
static int resetErrorList(void *chip_data)
{
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

	info->errors.count = 0;
	info->errors.last_index = 0;
	memset(info->errors.list, 0, FIFO_DEPTH * FIFO_EVENT_SIZE);
	/* if count is not considered is better reset also the list in order to
	  * avoid to read data previously copied into the list */
	return OK;
}

/**
  * Get the number of error events copied into the Error List
  * @return the number of error events into the Error List
  */
static int getErrorListCount(void *chip_data)
{
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

	if (info->errors.count > FIFO_DEPTH)
		return FIFO_DEPTH;

	else
		return info->errors.count;
}


/**
  * Poll the Error List looking for any error types passed in the arguments.
  * Return at the first match!
  * @param list pointer to a list of error types to look for
  * @param size size of list
  * @return error type found if success or ERROR_TIMEOUT
  */
static int pollForErrorType(void *chip_data, u8 *list, int size)
{
	int i = 0, j = 0, find = 0;
	int count = getErrorListCount(chip_data);
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

	TPD_INFO("%s: Starting to poll ErrorList... count = %d\n", __func__, count);

	while (find != 1 && i < count) {
		for (j = 0; j < size; j++) {
			if (list[j] == info->errors.list[i * FIFO_EVENT_SIZE + 1]) {
				find = 1;
				break;
			}
		}

		i++;
	}

	if (find == 1) {
		TPD_INFO("%s: Error Type %02X into ErrorList!\n", __func__, list[j]);
		return list[j];

	} else {
		TPD_INFO("%s: Error Type Not Found into ErrorList! ERROR %08X\n", __func__,
			 ERROR_TIMEOUT);
		return ERROR_TIMEOUT;
	}
}

/****************** New I2C API *********************/

/**
  * Perform a direct bus read
  * @param outBuf pointer of a byte array which should contain the byte read
  *from the IC
  * @param byteToRead number of bytes to read
  * @return OK if success or an error code which specify the type of error
  */
static int fts_read(void *chip_data, u8 *outBuf, int byteToRead)
{
	int ret = 0;
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

	if (info->client == NULL)
		return ERROR_BUS_O;

	ret = touch_i2c_read(info->client, NULL, 0, (__u8 *)outBuf, byteToRead);

	if (ret < 0)
		return ERROR_BUS_WR;

	return OK;
}


/**
  * Perform a bus write followed by a bus read without a stop condition
  * @param cmd byte array containing the command to write
  * @param cmdLength size of cmd
  * @param outBuf pointer of a byte array which should contain the bytes read
  *from the IC
  * @param byteToRead number of bytes to read
  * @return OK if success or an error code which specify the type of error
  */
static int fts_writeRead(void *chip_data, u8 *cmd, int cmdLength, u8 *outBuf,
			 int byteToRead)
{
	int ret = 0;
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

	if (info->client == NULL)
		return ERROR_BUS_O;

	ret = touch_i2c_read(info->client, (__u8 *)cmd, (__u16)cmdLength,
			     (__u8 *)outBuf, byteToRead);

	if (ret < 0)
		return ERROR_BUS_WR;

	return OK;
}

/**
  * Perform a bus write
  * @param cmd byte array containing the command to write
  * @param cmdLength size of cmd
  * @return OK if success or an error code which specify the type of error
  */
static int fts_write(void *chip_data, u8 *cmd, int cmdLength)
{
	int ret = 0;
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

	if (info->client == NULL)
		return ERROR_BUS_O;

	ret = touch_i2c_write(info->client, (__u8 *)cmd, (__u16)cmdLength);

	if (ret < 0)
		return ERROR_BUS_WR;

	return OK;
}

/**
  * Write a FW command to the IC and check automatically the echo event
  * @param cmd byte array containing the command to send
  * @param cmdLength size of cmd
  * @return OK if success, or an error code which specify the type of error
  */
static int fts_writeFwCmd(void *chip_data, u8 *cmd, int cmdLength)
{
	int ret = -1;
	int ret2 = -1;
	int retry = 0;
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

	if (info->client == NULL)
		return ERROR_BUS_O;

	resetErrorList(chip_data);

	fts_disableInterruptNoSync(chip_data);

	while (retry < I2C_RETRY && (ret < OK || ret2 < OK)) {
		ret = touch_i2c_write(info->client, (__u8 *)cmd, (__u16)cmdLength);

		retry++;

		if (ret >= 0)
			ret2 = checkEcho(info, cmd, cmdLength);

		if (ret < OK || ret2 < OK)
			msleep(I2C_WAIT_BEFORE_RETRY);

		/* TPD_INFO("fts_writeCmd: attempt %d\n", retry); */
	}

	fts_enableInterrupt(chip_data);

	if (ret < 0) {
		TPD_INFO("fts_writeFwCmd: ERROR %08X\n", ERROR_BUS_W);
		return ERROR_BUS_W;
	}

	if (ret2 < OK) {
		TPD_INFO("fts_writeFwCmd: check echo ERROR %08X\n", ret2);
		return ret2;
	}

	return OK;
}


/**
  * Perform two bus write and one bus read without any stop condition
  * In case of FTI this function is not supported and the same sequence
  * can be achieved calling fts_write followed by an fts_writeRead.
  * @param writeCmd1 byte array containing the first command to write
  * @param writeCmdLength size of writeCmd1
  * @param readCmd1 byte array containing the second command to write
  * @param readCmdLength size of readCmd1
  * @param outBuf pointer of a byte array which should contain the bytes read
  * from the IC
  * @param byteToRead number of bytes to read
  * @return OK if success or an error code which specify the type of error
  */
static int fts_writeThenWriteRead(void *chip_data,
				  u8 *writeCmd1, int writeCmdLength, u8 *readCmd1,
				  int readCmdLength, u8 *outBuf, int byteToRead)
{
	int ret = -1;
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

	if (info->client == NULL)
		return ERROR_BUS_O;

	ret = touch_i2c_write(info->client, (__u8 *)writeCmd1, (__u16)writeCmdLength);

	if (ret < 0)
		return ERROR_BUS_WR;

	ret = touch_i2c_read(info->client, (__u8 *)readCmd1, (__u16)readCmdLength,
			     (__u8 *)outBuf, byteToRead);

	if (ret < 0)
		return ERROR_BUS_WR;

	return OK;
}

/**
  * Perform a chunked write with one byte op code and 1 to 8 bytes address
  * @param cmd byte containing the op code to write
  * @param addrSize address size in byte
  * @param address the starting address
  * @param data pointer of a byte array which contain the bytes to write
  * @param dataSize size of data
  * @return OK if success or an error code which specify the type of error
  */
/* this function works only if the address is max 8 bytes */
static int fts_writeU8UX(void *chip_data, u8 cmd, AddrSize addrSize,
			 u64 address, u8 *data, int
			 dataSize)
{
	u8 *finalcmd = NULL;
	int remaining = dataSize;
	int towrite = 0, i = 0;
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

	finalcmd = (u8 *)kmalloc(1 + addrSize + WRITE_CHUNK, GFP_KERNEL);

	if (!finalcmd) {
		TPD_INFO("%s: kmalloc failed...ERROR %08X !\n",
			 __func__, -1);

		return -1;
	}

	if (addrSize <= sizeof(u64)) {
		while (remaining > 0) {
			if (remaining >= WRITE_CHUNK) {
				towrite = WRITE_CHUNK;
				remaining -= WRITE_CHUNK;

			} else {
				towrite = remaining;
				remaining = 0;
			}

			finalcmd[0] = cmd;
			TPD_DEBUG("%s: addrSize = %d\n", __func__,
				  addrSize);

			for (i = 0; i < addrSize; i++) {
				finalcmd[i + 1] = (u8)((address >> ((addrSize - 1 - i) * 8)) & 0xFF);
				TPD_INFO("%s: cmd[%d] = %02X\n", __func__, i + 1, finalcmd[i + 1]);
			}

			memcpy(&finalcmd[addrSize + 1], data, towrite);

			if (fts_write(info, finalcmd, 1 + addrSize + towrite) < OK) {
				TPD_INFO("%s: ERROR %08X\n", __func__, ERROR_BUS_W);
				kfree(finalcmd);
				return ERROR_BUS_W;
			}

			address += towrite;

			data += towrite;
		}
	} else
		TPD_INFO("%s: address size bigger than max allowed %ld... ERROR %08X\n",
			 __func__, sizeof(u64), ERROR_OP_NOT_ALLOW);

	kfree(finalcmd);
	return OK;
}

/**
  * Perform a chunked write read with one byte op code and 1 to 8 bytes address
  * and dummy byte support.
  * @param cmd byte containing the op code to write
  * @param addrSize address size in byte
  * @param address the starting address
  * @param outBuf pointer of a byte array which contain the bytes to read
  * @param byteToRead number of bytes to read
  * @param hasDummyByte  if the first byte of each reading is dummy (must be
  * skipped)
  * set to 1, otherwise if it is valid set to 0 (or any other value)
  * @return OK if success or an error code which specify the type of error
  */
static int fts_writeReadU8UX(void *chip_data, u8 cmd, AddrSize addrSize,
			     u64 address, u8 *outBuf, int
			     byteToRead, int hasDummyByte)
{
	u8 *finalcmd = NULL;
	u8 *buff = NULL;/* worst case has dummy byte */
	int remaining = byteToRead;
	int toread = 0, i = 0;
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

	buff = kzalloc(sizeof(u8) * (READ_CHUNK + 1), GFP_KERNEL);

	if (buff == NULL) {
		TPD_INFO("kzalloc buff failed\n");
		return ERROR_BUS_WR;
	}

	finalcmd = (u8 *)kmalloc(1 + addrSize, GFP_KERNEL);

	if (!finalcmd) {
		TPD_INFO("kzalloc buff failed\n");
		kfree(buff);
		return -1;
	}

	while (remaining > 0) {
		if (remaining >= READ_CHUNK) {
			toread = READ_CHUNK;
			remaining -= READ_CHUNK;

		} else {
			toread = remaining;
			remaining = 0;
		}

		finalcmd[0] = cmd;

		for (i = 0; i < addrSize; i++)
			finalcmd[i + 1] = (u8)((address >> ((addrSize - 1 - i) * 8)) & 0xFF);

		if (hasDummyByte == 1) {
			if (fts_writeRead(info, finalcmd, 1 + addrSize, buff, toread + 1) < OK) {
				TPD_INFO("%s: read error... ERROR %08X\n", __func__, ERROR_BUS_WR);
				kfree(buff);
				kfree(finalcmd);
				return ERROR_BUS_WR;
			}

			memcpy(outBuf, buff + 1, toread);

		} else {
			if (fts_writeRead(info, finalcmd, 1 + addrSize, buff, toread) < OK) {
				TPD_INFO("%s: read error... ERROR %08X\n", __func__, ERROR_BUS_WR);
				kfree(buff);
				kfree(finalcmd);
				return ERROR_BUS_WR;
			}

			memcpy(outBuf, buff, toread);
		}

		address += toread;

		outBuf += toread;
	}

	kfree(buff);
	kfree(finalcmd);

	return OK;
}

/**
  * Perform a chunked write followed by a second write with one byte op code
  * for each write and 1 to 8 bytes address (the sum of the 2 address size of
  * the two writes can not exceed 8 bytes)
  * @param cmd1 byte containing the op code of first write
  * @param addrSize1 address size in byte of first write
  * @param cmd2 byte containing the op code of second write
  * @param addrSize2 address size in byte of second write
  * @param address the starting address
  * @param data pointer of a byte array which contain the bytes to write
  * @param dataSize size of data
  * @return OK if success or an error code which specify the type of error
  */
/* this function works only if the sum of two addresses in the two commands is
 * max 8 bytes */
static int fts_writeU8UXthenWriteU8UX(void *chip_data,
				      u8 cmd1, AddrSize addrSize1, u8 cmd2, AddrSize
				      addrSize2, u64 address, u8 *data, int dataSize)
{
	u8 *finalcmd1 = NULL;
	u8 *finalcmd2 = NULL;
	int remaining = dataSize;
	int towrite = 0, i = 0;
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

	finalcmd1 = (u8 *)kmalloc(1 + addrSize1, GFP_KERNEL);

	if (!finalcmd1) {
		TPD_INFO("kzalloc buff failed\n");

		return -1;
	}

	finalcmd2 = (u8 *)kmalloc(1 + addrSize2 + WRITE_CHUNK, GFP_KERNEL);

	if (!finalcmd2) {
		kfree(finalcmd1);
		TPD_INFO("kzalloc buff failed\n");

		return -1;
	}

	while (remaining > 0) {
		if (remaining >= WRITE_CHUNK) {
			towrite = WRITE_CHUNK;
			remaining -= WRITE_CHUNK;

		} else {
			towrite = remaining;
			remaining = 0;
		}

		finalcmd1[0] = cmd1;

		for (i = 0; i < addrSize1; i++)
			finalcmd1[i + 1] = (u8)((address >> ((addrSize1 + addrSize2 - 1 - i) * 8)) &
						0xFF);

		finalcmd2[0] = cmd2;

		for (i = addrSize1; i < addrSize1 + addrSize2; i++)
			finalcmd2[i - addrSize1 + 1] = (u8)((address >> ((addrSize1 + addrSize2 - 1 - i)
							     * 8)) & 0xFF);

		memcpy(&finalcmd2[addrSize2 + 1], data, towrite);

		if (fts_write(info, finalcmd1, 1 + addrSize1) < OK) {
			TPD_INFO("%s: first write error... ERROR %08X\n", __func__, ERROR_BUS_W);
			kfree(finalcmd1);
			kfree(finalcmd2);
			return ERROR_BUS_W;
		}

		if (fts_write(info, finalcmd2, 1 + addrSize2 + towrite) < OK) {
			TPD_INFO("%s: second write error... ERROR %08X\n", __func__, ERROR_BUS_W);
			kfree(finalcmd1);
			kfree(finalcmd2);
			return ERROR_BUS_W;
		}

		address += towrite;

		data += towrite;
	}

	kfree(finalcmd1);
	kfree(finalcmd2);
	return OK;
}

/**
  * Perform a chunked write  followed by a write read with one byte op code
  * and 1 to 8 bytes address for each write and dummy byte support.
  * @param cmd1 byte containing the op code of first write
  * @param addrSize1 address size in byte of first write
  * @param cmd2 byte containing the op code of second write read
  * @param addrSize2 address size in byte of second write    read
  * @param address the starting address
  * @param outBuf pointer of a byte array which contain the bytes to read
  * @param byteToRead number of bytes to read
  * @param hasDummyByte  if the first byte of each reading is dummy (must be
  * skipped) set to 1,
  *  otherwise if it is valid set to 0 (or any other value)
  * @return OK if success or an error code which specify the type of error
  */
/* this function works only if the sum of two addresses in the two commands is
 * max 8 bytes */
static int fts_writeU8UXthenWriteReadU8UX(void *chip_data,
		u8 cmd1, AddrSize addrSize1, u8 cmd2,
		AddrSize addrSize2, u64 address, u8 *outBuf,
		int byteToRead, int hasDummyByte)
{
	u8 *finalcmd1 = NULL;
	u8 *finalcmd2 = NULL;

	u8 *buff = NULL;
	int remaining = byteToRead;
	int toread = 0, i = 0;
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

	buff = kzalloc(sizeof(u8) * (READ_CHUNK + 1), GFP_KERNEL);

	if (buff == NULL) {
		TPD_INFO("kzalloc buff failed\n");
		return ERROR_BUS_WR;
	}

	finalcmd1 = (u8 *)kmalloc(1 + addrSize1, GFP_KERNEL);

	if (!finalcmd1) {
		kfree(buff);
		TPD_INFO("kzalloc buff failed\n");
		return -1;
	}

	finalcmd2 = (u8 *)kmalloc(1 + addrSize2, GFP_KERNEL);

	if (!finalcmd2) {
		kfree(buff);
		kfree(finalcmd1);
		TPD_INFO("kzalloc buff failed\n");
		return -1;
	}

	while (remaining > 0) {
		if (remaining >= READ_CHUNK) {
			toread = READ_CHUNK;
			remaining -= READ_CHUNK;

		} else {
			toread = remaining;
			remaining = 0;
		}


		finalcmd1[0] = cmd1;

		for (i = 0; i < addrSize1; i++)
			finalcmd1[i + 1] = (u8)((address >> ((addrSize1 + addrSize2 - 1 - i) * 8)) &
						0xFF);

		/* TPD_INFO("%s: finalcmd1[%d] =  %02X\n",
		  *     __func__, i+1, finalcmd1[i + 1]); */

		finalcmd2[0] = cmd2;

		for (i = addrSize1; i < addrSize1 + addrSize2; i++)
			finalcmd2[i - addrSize1 + 1] = (u8)((address >> ((addrSize1 + addrSize2 - 1 - i)
							     * 8)) & 0xFF);

		if (fts_write(info, finalcmd1, 1 + addrSize1) < OK) {
			TPD_INFO("%s: first write error... ERROR %08X\n", __func__, ERROR_BUS_W);
			kfree(finalcmd1);
			kfree(finalcmd2);
			kfree(buff);
			return ERROR_BUS_W;
		}

		if (hasDummyByte == 1) {
			if (fts_writeRead(info, finalcmd2, 1 + addrSize2, buff, toread + 1) < OK) {
				TPD_INFO("%s: read error... ERROR %08X\n", __func__, ERROR_BUS_WR);
				kfree(finalcmd1);
				kfree(finalcmd2);
				kfree(buff);
				return ERROR_BUS_WR;
			}

			memcpy(outBuf, buff + 1, toread);

		} else {
			if (fts_writeRead(info, finalcmd2, 1 + addrSize2, buff, toread) < OK) {
				TPD_INFO("%s: read error... ERROR %08X\n", __func__, ERROR_BUS_WR);
				kfree(finalcmd1);
				kfree(finalcmd2);
				kfree(buff);
				return ERROR_BUS_WR;
			}

			memcpy(outBuf, buff, toread);
		}

		address += toread;

		outBuf += toread;
	}

	kfree(finalcmd1);
	kfree(finalcmd2);
	kfree(buff);
	return OK;
}

/**
  * Request to the FW to load the specified Initialization Data
  * @param type type of Initialization data to load @link load_opt Load Host
  * Data Option @endlink
  * @return OK if success or an error code which specify the type of error
  */
static int requestCompensationData(void *chip_data, u8 type)
{
	int ret = ERROR_OP_NOT_ALLOW;
	int retry = 0;
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

	TPD_DEBUG("%s: Requesting compensation data... attemp = %d\n", __func__,
		  retry + 1);

	while (retry < RETRY_COMP_DATA_READ) {
		ret = writeSysCmd(info, SYS_CMD_LOAD_DATA,  &type, 1);

		/* send request to load in memory the Compensation Data */
		if (ret < OK) {
			TPD_INFO("%s: failed at %d attemp!\n", __func__, retry + 1);
			retry += 1;

		} else {
			TPD_DEBUG("%s: Request Compensation data FINISHED!\n", __func__);
			return OK;
		}
	}

	TPD_INFO("%s: Requesting compensation data... ERROR %08X\n", __func__,
		 ret | ERROR_REQU_COMP_DATA);
	return ret | ERROR_REQU_COMP_DATA;
}


/**
  * Read Initialization Data Header and check that the type loaded match
  * with the one previously requested
  * @param type type of Initialization data requested @link load_opt Load Host
  * Data Option @endlink
  * @param header pointer to DataHeader variable which will contain the header
  * @param address pointer to a variable which will contain the updated address
  * to the next data
  * @return OK if success or an error code which specify the type of error
  */
static int readCompensationDataHeader(void *chip_data, u8 type,
				      DataHeader *header, u64 *address)
{
	u64 offset = ADDR_FRAMEBUFFER;
	u8 data[COMP_DATA_HEADER];
	int ret;
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

	ret = fts_writeReadU8UX(info, FTS_CMD_FRAMEBUFFER_R, BITS_16, offset, data,
				COMP_DATA_HEADER, DUMMY_FRAMEBUFFER);

	if (ret < OK) {    /* i2c function have already a retry mechanism */
		TPD_INFO("%s: error while reading data header ERROR %08X\n", __func__, ret);
		return ret;
	}

	TPD_DEBUG("Read Data Header done!\n");

	if (data[0] != HEADER_SIGNATURE) {
		TPD_INFO("%s: The Header Signature was wrong! %02X != %02X ERROR %08X\n",
			 __func__, data[0], HEADER_SIGNATURE, ERROR_WRONG_DATA_SIGN);
		return ERROR_WRONG_DATA_SIGN;
	}

	if (data[1] != type) {
		TPD_INFO("%s: Wrong type found! %02X!=%02X ERROR %08X\n", __func__, data[1],
			 type, ERROR_DIFF_DATA_TYPE);
		return ERROR_DIFF_DATA_TYPE;
	}

	TPD_DEBUG("Type = %02X of Compensation data OK!\n", type);

	header->type = type;

	*address = offset + COMP_DATA_HEADER;

	return OK;
}


/**
  * Read MS Global Initialization data from the buffer such as Cx1
  * @param address pointer to a variable which contain the address from where
  * to read the data and will contain the updated address to the next data
  * @param global pointer to MutualSenseData variable which will contain the MS
  * initialization data
  * @return OK if success or an error code which specify the type of error
  */
static int readMutualSenseGlobalData(void *chip_data, u64 *address,
				     MutualSenseData *global)
{
	u8 data[COMP_DATA_GLOBAL];
	int ret;
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

	TPD_DEBUG("Address for Global data= %08llX\n", *address);

	ret = fts_writeReadU8UX(info, FTS_CMD_FRAMEBUFFER_R, BITS_16, *address, data,
				COMP_DATA_GLOBAL, DUMMY_FRAMEBUFFER);

	if (ret < OK) {
		TPD_INFO("%s: error while reading info data ERROR %08X\n", __func__, ret);
		return ret;
	}

	TPD_DEBUG("Global data Read !\n");

	global->header.force_node = data[0];
	global->header.sense_node = data[1];
	global->cx1 = data[2];
	/* all other bytes are reserved atm */

	TPD_DEBUG("force_len = %d sense_len = %d CX1 = %d\n", global->header.force_node,
		  global->header.sense_node, global->cx1);

	*address += COMP_DATA_GLOBAL;
	return OK;
}


/**
  * Read MS Initialization data for each node from the buffer
  * @param address a variable which contain the address from where to read the
  * data
  * @param node pointer to MutualSenseData variable which will contain the MS
  * initialization data
  * @return OK if success or an error code which specify the type of error
  */
static int readMutualSenseNodeData(void *chip_data, u64 address,
				   MutualSenseData *node)
{
	int ret;
	int size = node->header.force_node * node->header.sense_node;
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

	TPD_DEBUG("Address for Node data = %08llX\n", address);

	node->node_data = (i8 *)kzalloc(size * (sizeof(i8)), GFP_KERNEL);

	if (node->node_data == NULL) {
		TPD_INFO("%s: can not allocate node_data... ERROR %08X", __func__, ERROR_ALLOC);
		return ERROR_ALLOC;
	}

	TPD_DEBUG("Node Data to read %d bytes\n", size);
	ret = fts_writeReadU8UX(info, FTS_CMD_FRAMEBUFFER_R, BITS_16, address,
				node->node_data, size, DUMMY_FRAMEBUFFER);

	if (ret < OK) {
		TPD_INFO("%s: error while reading node data ERROR %08X\n", __func__, ret);
		kfree(node->node_data);
		return ret;
	}

	node->node_data_size = size;

	TPD_DEBUG("Read node data OK!\n");

	return size;
}

/**
  * Perform all the steps to read the necessary info for MS Initialization data
  * from the buffer and store it in a MutualSenseData variable
  * @param type type of MS Initialization data to read @link load_opt Load Host
  * Data Option @endlink
  * @param data pointer to MutualSenseData variable which will contain the MS
  * initialization data
  * @return OK if success or an error code which specify the type of error
  */
static int readMutualSenseCompensationData(void *chip_data, u8 type,
		MutualSenseData *data)
{
	int ret;
	u64 address;
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

	data->node_data = NULL;

	if (!(type == LOAD_CX_MS_TOUCH || type == LOAD_CX_MS_LOW_POWER
			|| type == LOAD_CX_MS_KEY || type == LOAD_CX_MS_FORCE)) {
		TPD_INFO("%s: Choose a MS type of compensation data ERROR %08X\n", __func__,
			 ERROR_OP_NOT_ALLOW);
		return ERROR_OP_NOT_ALLOW;
	}

	ret = requestCompensationData(info, type);

	if (ret < 0) {
		TPD_INFO("%s: ERROR %08X\n", __func__, ERROR_REQU_COMP_DATA);
		return ret | ERROR_REQU_COMP_DATA;
	}

	ret = readCompensationDataHeader(info, type, &(data->header), &address);

	if (ret < 0) {
		TPD_INFO("%s: ERROR %08X\n", __func__, ERROR_COMP_DATA_HEADER);
		return ret | ERROR_COMP_DATA_HEADER;
	}

	ret = readMutualSenseGlobalData(info, &address, data);

	if (ret < 0) {
		TPD_INFO("%s: ERROR %08X\n", __func__, ERROR_COMP_DATA_GLOBAL);
		return ret | ERROR_COMP_DATA_GLOBAL;
	}

	ret = readMutualSenseNodeData(info, address, data);

	if (ret < 0) {
		TPD_INFO("%s: ERROR %08X\n", __func__, ERROR_COMP_DATA_NODE);
		return ret | ERROR_COMP_DATA_NODE;
	}

	return OK;
}

/**
  * Read SS Global Initialization data from the buffer such as Ix1/Cx1 for force
  * and sense
  * @param address pointer to a variable which contain the address from where
  * to read the data and will contain the updated address to the next data
  * @param global pointer to MutualSenseData variable which will contain the SS
  * initialization data
  * @return OK if success or an error code which specify the type of error
  */
static int readSelfSenseGlobalData(void *chip_data, u64 *address,
				   SelfSenseData *global)
{
	int ret;
	u8 data[COMP_DATA_GLOBAL];
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

	TPD_DEBUG("Address for Global data= %08llX\n", *address);
	ret = fts_writeReadU8UX(info, FTS_CMD_FRAMEBUFFER_R, BITS_16, *address, data,
				COMP_DATA_GLOBAL, DUMMY_FRAMEBUFFER);

	if (ret < OK) {
		TPD_INFO("%s: error while reading the data... ERROR %08X\n", __func__, ret);
		return ret;
	}

	TPD_DEBUG("Global data Read !\n");

	global->header.force_node = data[0];
	global->header.sense_node = data[1];
	global->f_ix1 = data[2];
	global->s_ix1 = data[3];
	global->f_cx1 = (i8)data[4];
	global->s_cx1 = (i8)data[5];
	global->f_max_n = data[6];
	global->s_max_n = data[7];

	TPD_DEBUG("force_len = %d sense_len = %d  f_ix1 = %d   s_ix1 = %d   f_cx1 = %d   s_cx1 = %d\n",
		  global->header.force_node, global->header.sense_node,
		  global->f_ix1, global->s_ix1, global->f_cx1, global->s_cx1);
	TPD_DEBUG("max_n = %d   s_max_n = %d\n", global->f_max_n,
		  global->s_max_n);


	*address += COMP_DATA_GLOBAL;

	return OK;
}

/**
  * Read SS Initialization data for each node of force and sense channels from
  * the buffer
  * @param address a variable which contain the address from where to read the
  * data
  * @param node pointer to SelfSenseData variable which will contain the SS
  * initialization data
  * @return OK if success or an error code which specify the type of error
  */
static int readSelfSenseNodeData(void *chip_data, u64 address,
				 SelfSenseData *node)
{
	int size = node->header.force_node * 2 + node->header.sense_node * 2;
	u8 *data = NULL;
	int ret;
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

	data = (u8 *)kmalloc(size * sizeof(u8), GFP_KERNEL);

	if (!data) {
		TPD_INFO("%s: kmalloc failed...ERROR %08X !\n",
			 __func__, -1);
		return -1;
	}

	node->ix2_fm = (u8 *)kzalloc(node->header.force_node * (sizeof(u8)),
				     GFP_KERNEL);

	if (node->ix2_fm == NULL) {
		TPD_INFO("%s: can not allocate memory for ix2_fm... ERROR %08X", __func__,
			 ERROR_ALLOC);
		kfree(data);
		return ERROR_ALLOC;
	}

	node->cx2_fm = (i8 *)kzalloc(node->header.force_node * (sizeof(i8)),
				     GFP_KERNEL);

	if (node->cx2_fm == NULL) {
		TPD_INFO("%s: can not allocate memory for cx2_fm ... ERROR %08X", __func__,
			 ERROR_ALLOC);
		kfree(data);
		kfree(node->ix2_fm);
		return ERROR_ALLOC;
	}

	node->ix2_sn = (u8 *)kzalloc(node->header.sense_node * (sizeof(u8)),
				     GFP_KERNEL);

	if (node->ix2_sn == NULL) {
		TPD_INFO("%s: can not allocate memory for ix2_sn ERROR %08X", __func__,
			 ERROR_ALLOC);
		kfree(data);
		kfree(node->ix2_fm);
		kfree(node->cx2_fm);
		return ERROR_ALLOC;
	}

	node->cx2_sn = (i8 *)kzalloc(node->header.sense_node * (sizeof(i8)),
				     GFP_KERNEL);

	if (node->cx2_sn == NULL) {
		TPD_INFO("%s: can not allocate memory for cx2_sn ERROR %08X", __func__,
			 ERROR_ALLOC);
		kfree(data);
		kfree(node->ix2_fm);
		kfree(node->cx2_fm);
		kfree(node->ix2_sn);
		return ERROR_ALLOC;
	}


	TPD_DEBUG("Address for Node data = %08llX\n", address);

	TPD_DEBUG("Node Data to read %d bytes\n", size);

	ret = fts_writeReadU8UX(info, FTS_CMD_FRAMEBUFFER_R, BITS_16, address, data,
				size, DUMMY_FRAMEBUFFER);

	if (ret < OK) {
		TPD_INFO("%s: error while reading data... ERROR %08X\n", __func__, ret);
		kfree(data);
		kfree(node->ix2_fm);
		kfree(node->cx2_fm);
		kfree(node->ix2_sn);
		kfree(node->cx2_sn);
		return ret;
	}

	TPD_DEBUG("Read node data ok!\n");

	memcpy(node->ix2_fm, data, node->header.force_node);
	memcpy(node->ix2_sn, &data[node->header.force_node], node->header.sense_node);
	memcpy(node->cx2_fm, &data[node->header.force_node + node->header.sense_node],
	       node->header.force_node);
	memcpy(node->cx2_sn, &data[node->header.force_node * 2 +
				   node->header.sense_node], node->header.sense_node);

	kfree(data);
	return OK;
}

/**
  * Perform all the steps to read the necessary info for SS Initialization data
  * from the buffer and store it in a SelfSenseData variable
  * @param type type of SS Initialization data to read @link load_opt Load Host
  * Data Option @endlink
  * @param data pointer to SelfSenseData variable which will contain the SS
  * initialization data
  * @return OK if success or an error code which specify the type of error
  */
static int readSelfSenseCompensationData(void *chip_data, u8 type,
		SelfSenseData *data)
{
	int ret;
	u64 address;
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

	data->ix2_fm = NULL;
	data->cx2_fm = NULL;
	data->ix2_sn = NULL;
	data->cx2_sn = NULL;

	if (!(type == LOAD_CX_SS_TOUCH || type == LOAD_CX_SS_TOUCH_IDLE
			|| type == LOAD_CX_SS_KEY || type == LOAD_CX_SS_FORCE)) {
		TPD_INFO("%s: Choose a SS type of compensation data ERROR %08X\n", __func__,
			 ERROR_OP_NOT_ALLOW);
		return ERROR_OP_NOT_ALLOW;
	}

	ret = requestCompensationData(info, type);

	if (ret < 0) {
		TPD_INFO("%s: error while requesting data... ERROR %08X\n", __func__,
			 ERROR_REQU_COMP_DATA);
		return ret | ERROR_REQU_COMP_DATA;
	}

	ret = readCompensationDataHeader(info, type, &(data->header), &address);

	if (ret < 0) {
		TPD_INFO("%s: error while reading data header... ERROR %08X\n", __func__,
			 ERROR_COMP_DATA_HEADER);
		return ret | ERROR_COMP_DATA_HEADER;
	}

	ret = readSelfSenseGlobalData(info, &address, data);

	if (ret < 0) {
		TPD_INFO("%s: ERROR %08X\n", __func__, ERROR_COMP_DATA_GLOBAL);
		return ret | ERROR_COMP_DATA_GLOBAL;
	}

	ret = readSelfSenseNodeData(info, address, data);

	if (ret < 0) {
		TPD_INFO("%s: ERROR %08X\n", __func__, ERROR_COMP_DATA_NODE);
		return ret | ERROR_COMP_DATA_NODE;
	}

	return OK;
}

/**
  * Read TOT MS Global Initialization data from the buffer such as number of
  * force and sense channels
  * @param address pointer to a variable which contain the address from where
  * to read the data and will contain the updated address to the next data
  * @param global pointer to a variable which will contain the TOT MS
  * initialization data
  * @return OK if success or an error code which specify the type of error
  */
static int readTotMutualSenseGlobalData(void *chip_data, u64 *address,
					TotMutualSenseData *global)
{
	int ret;
	u8 data[COMP_DATA_GLOBAL];
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

	TPD_DEBUG("Address for Global data= %04llX\n", *address);

	ret = fts_writeReadU8UX(info, FTS_CMD_FRAMEBUFFER_R, BITS_16, *address, data,
				COMP_DATA_GLOBAL, DUMMY_FRAMEBUFFER);

	if (ret < OK) {
		TPD_INFO("%s: error while reading info data ERROR %08X\n", __func__, ret);
		return ret;
	}

	TPD_DEBUG("Global data Read !\n");

	global->header.force_node = data[0];
	global->header.sense_node = data[1];
	/* all other bytes are reserved atm */

	TPD_DEBUG("force_len = %d sense_len = %d\n", global->header.force_node,
		  global->header.sense_node);

	*address += COMP_DATA_GLOBAL;
	return OK;
}


/**
  * Read TOT MS Initialization data for each node from the buffer
  * @param address a variable which contain the address from where to read the
  * data
  * @param node pointer to MutualSenseData variable which will contain the TOT
  * MS initialization data
  * @return OK if success or an error code which specify the type of error
  */
static int readTotMutualSenseNodeData(void *chip_data, u64 address,
				      TotMutualSenseData *node)
{
	int ret, i;
	int size = node->header.force_node * node->header.sense_node;
	int toread = size * sizeof(u16);
	u8 *data = NULL;
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

	TPD_DEBUG("Address for Node data = %04llX\n", address);

	data = (u8 *)kmalloc(toread * sizeof(u8), GFP_KERNEL);

	if (!data) {
		TPD_INFO("%s: kmalloc failed...ERROR %08X !\n",
			 __func__, -1);
		return -1;
	}

	node->node_data = (short *)kzalloc(size * (sizeof(short)), GFP_KERNEL);

	if (node->node_data == NULL) {
		TPD_INFO("%s: can not allocate node_data... ERROR %08X", __func__, ERROR_ALLOC);
		kfree(data);
		return ERROR_ALLOC;
	}

	TPD_DEBUG("Node Data to read %d bytes\n", size);

	ret = fts_writeReadU8UX(info, FTS_CMD_FRAMEBUFFER_R, BITS_16, address, data,
				toread, DUMMY_FRAMEBUFFER);

	if (ret < OK) {
		TPD_INFO("%s: error while reading node data ERROR %08X\n", __func__, ret);
		kfree(data);
		kfree(node->node_data);
		return ret;
	}

	node->node_data_size = size;

	for (i = 0; i < size; i++)
		node->node_data[i] = ((short)data[i * 2 + 1]) << 8 | data[i * 2];

	TPD_DEBUG("Read node data OK!\n");

	kfree(data);
	return size;
}

/**
  * Perform all the steps to read the necessary info for TOT MS Initialization
  * data from the buffer and store it in a TotMutualSenseData variable
  * @param type type of TOT MS Initialization data to read @link load_opt Load
  * Host Data Option @endlink
  * @param data pointer to a variable which will contain the TOT MS
  * initialization data
  * @return OK if success or an error code which specify the type of error
  */
static int readTotMutualSenseCompensationData(void *chip_data, u8 type,
		TotMutualSenseData *data)
{
	int ret;
	u64 address;
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

	data->node_data = NULL;

	if (!(type == LOAD_PANEL_CX_TOT_MS_TOUCH
			|| type == LOAD_PANEL_CX_TOT_MS_LOW_POWER
			|| type == LOAD_PANEL_CX_TOT_MS_KEY || type == LOAD_PANEL_CX_TOT_MS_FORCE)) {
		TPD_INFO("%s: Choose a TOT MS type of compensation data ERROR %08X\n", __func__,
			 ERROR_OP_NOT_ALLOW);
		return ERROR_OP_NOT_ALLOW;
	}

	ret = requestCompensationData(info, type);

	if (ret < 0) {
		TPD_INFO("%s: ERROR %08X\n", __func__, ERROR_REQU_COMP_DATA);
		return ret | ERROR_REQU_COMP_DATA;
	}

	ret = readCompensationDataHeader(info, type, &(data->header), &address);

	if (ret < 0) {
		TPD_INFO("%s: ERROR %08X\n", __func__, ERROR_COMP_DATA_HEADER);
		return ret | ERROR_COMP_DATA_HEADER;
	}

	ret = readTotMutualSenseGlobalData(info, &address, data);

	if (ret < 0) {
		TPD_INFO("%s: ERROR %08X\n", __func__, ERROR_COMP_DATA_GLOBAL);
		return ret | ERROR_COMP_DATA_GLOBAL;
	}

	ret = readTotMutualSenseNodeData(info, address, data);

	if (ret < 0) {
		TPD_INFO("%s: ERROR %08X\n", __func__, ERROR_COMP_DATA_NODE);
		return ret | ERROR_COMP_DATA_NODE;
	}

	return OK;
}

/**
  * Read TOT SS Global Initialization data from the buffer such as number of
  * force and sense channels
  * @param address pointer to a variable which contain the address from where
  * to read the data and will contain the updated address to the next data
  * @param global pointer to a variable which will contain the TOT SS
  * initialization data
  * @return OK if success or an error code which specify the type of error
  */
static int readTotSelfSenseGlobalData(void *chip_data, u64 *address,
				      TotSelfSenseData *global)
{
	int ret;
	u8 data[COMP_DATA_GLOBAL];
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

	TPD_DEBUG("Address for Global data= %04llX\n", *address);
	ret = fts_writeReadU8UX(info, FTS_CMD_FRAMEBUFFER_R, BITS_16, *address, data,
				COMP_DATA_GLOBAL, DUMMY_FRAMEBUFFER);

	if (ret < OK) {
		TPD_INFO("%s: error while reading the data... ERROR %08X\n", __func__, ret);
		return ret;
	}

	TPD_DEBUG("Global data Read !\n");

	global->header.force_node = data[0];
	global->header.sense_node = data[1];

	TPD_DEBUG("force_len = %d sense_len = %d\n", global->header.force_node,
		  global->header.sense_node);

	*address += COMP_DATA_GLOBAL;

	return OK;
}

/**
  * Read TOT SS Global Initialization data from the buffer such as number of
  * force and sense channels
  * @param address pointer to a variable which contain the address from where
  * to read the data and will contain the updated address to the next data
  * @param node pointer to a variable which will contain the TOT SS
  * initialization data
  * @return OK if success or an error code which specify the type of error
  */
static int readTotSelfSenseNodeData(void *chip_data, u64 address,
				    TotSelfSenseData *node)
{
	int size = node->header.force_node * 2 + node->header.sense_node * 2;
	int toread = size * 2;    /* *2 2 bytes each node */
	u8 *data = NULL;
	int ret, i, j = 0;
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

	data = (u8 *)kmalloc(toread * sizeof(u8), GFP_KERNEL);

	if (!data) {
		TPD_INFO("%s: kmalloc failed...ERROR %08X !\n",
			 __func__, -1);
		return -1;
	}

	node->ix_fm = (u16 *)kzalloc(node->header.force_node * (sizeof(u16)),
				     GFP_KERNEL);

	if (node->ix_fm == NULL) {
		TPD_INFO("%s: can not allocate memory for ix2_fm... ERROR %08X", __func__,
			 ERROR_ALLOC);
		kfree(data);
		return ERROR_ALLOC;
	}

	node->cx_fm = (short *)kzalloc(node->header.force_node * (sizeof(short)),
				       GFP_KERNEL);

	if (node->cx_fm == NULL) {
		TPD_INFO("%s: can not allocate memory for cx2_fm ... ERROR %08X", __func__,
			 ERROR_ALLOC);
		kfree(data);
		kfree(node->ix_fm);
		return ERROR_ALLOC;
	}

	node->ix_sn = (u16 *)kzalloc(node->header.sense_node * (sizeof(u16)),
				     GFP_KERNEL);

	if (node->ix_sn == NULL) {
		TPD_INFO("%s: can not allocate memory for ix2_sn ERROR %08X", __func__,
			 ERROR_ALLOC);
		kfree(data);
		kfree(node->ix_fm);
		kfree(node->cx_fm);
		return ERROR_ALLOC;
	}

	node->cx_sn = (short *)kzalloc(node->header.sense_node * (sizeof(short)),
				       GFP_KERNEL);

	if (node->cx_sn == NULL) {
		TPD_INFO("%s: can not allocate memory for cx2_sn ERROR %08X", __func__,
			 ERROR_ALLOC);
		kfree(data);
		kfree(node->ix_fm);
		kfree(node->cx_fm);
		kfree(node->ix_sn);
		return ERROR_ALLOC;
	}

	TPD_DEBUG("Address for Node data = %04llX\n", address);

	TPD_DEBUG("Node Data to read %d bytes\n", size);

	ret = fts_writeReadU8UX(info, FTS_CMD_FRAMEBUFFER_R, BITS_16, address, data,
				toread, DUMMY_FRAMEBUFFER);

	if (ret < OK) {
		TPD_INFO("%s: error while reading data... ERROR %08X\n", __func__, ret);
		kfree(data);
		kfree(node->ix_fm);
		kfree(node->cx_fm);
		kfree(node->ix_sn);
		kfree(node->cx_sn);
		return ret;
	}

	TPD_DEBUG("Read node data ok!\n");

	j = 0;

	for (i = 0; i < node->header.force_node; i++) {
		node->ix_fm[i] = ((u16)data[j + 1]) << 8 | data[j];
		j += 2;
	}

	for (i = 0; i < node->header.sense_node; i++) {
		node->ix_sn[i] = ((u16)data[j + 1]) << 8 | data[j];
		j += 2;
	}

	for (i = 0; i < node->header.force_node; i++) {
		node->cx_fm[i] = ((short)data[j + 1]) << 8 | data[j];
		j += 2;
	}

	for (i = 0; i < node->header.sense_node; i++) {
		node->cx_sn[i] = ((short)data[j + 1]) << 8 | data[j];
		j += 2;
	}

	if (j != toread)
		TPD_INFO("%s: parsed a wrong number of bytes %d!=%d\n", __func__, j, toread);

	kfree(data);
	return OK;
}

/**
  * Perform all the steps to read the necessary info for TOT SS Initialization
  * data from the buffer and store it in a TotSelfSenseData variable
  * @param type type of TOT MS Initialization data to read @link load_opt Load
  * Host Data Option @endlink
  * @param data pointer to a variable which will contain the TOT MS
  * initialization data
  * @return OK if success or an error code which specify the type of error
  */
static int readTotSelfSenseCompensationData(void *chip_data, u8 type,
		TotSelfSenseData *data)
{
	int ret;
	u64 address;
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

	data->ix_fm = NULL;
	data->cx_fm = NULL;
	data->ix_sn = NULL;
	data->cx_sn = NULL;

	if (!(type == LOAD_PANEL_CX_TOT_SS_TOUCH
			|| type == LOAD_PANEL_CX_TOT_SS_TOUCH_IDLE || type == LOAD_PANEL_CX_TOT_SS_KEY
			||
			type == LOAD_PANEL_CX_TOT_SS_FORCE)) {
		TPD_INFO("%s: Choose a TOT SS type of compensation data ERROR %08X\n", __func__,
			 ERROR_OP_NOT_ALLOW);
		return ERROR_OP_NOT_ALLOW;
	}

	ret = requestCompensationData(info, type);

	if (ret < 0) {
		TPD_INFO("%s: error while requesting data... ERROR %08X\n", __func__,
			 ERROR_REQU_COMP_DATA);
		return ret | ERROR_REQU_COMP_DATA;
	}

	ret = readCompensationDataHeader(info, type, &(data->header), &address);

	if (ret < 0) {
		TPD_INFO("%s: error while reading data header... ERROR %08X\n", __func__,
			 ERROR_COMP_DATA_HEADER);
		return ret | ERROR_COMP_DATA_HEADER;
	}

	ret = readTotSelfSenseGlobalData(info, &address, data);

	if (ret < 0) {
		TPD_INFO("%s: ERROR %08X\n", __func__, ERROR_COMP_DATA_GLOBAL);
		return ret | ERROR_COMP_DATA_GLOBAL;
	}

	ret = readTotSelfSenseNodeData(info, address, data);

	if (ret < 0) {
		TPD_INFO("%s: ERROR %08X\n", __func__, ERROR_COMP_DATA_NODE);
		return ret | ERROR_COMP_DATA_NODE;
	}

	return OK;
}



/**
  * Read the channels lengths from the config memory
  * @return OK if success or an error code which specify the type of error
  */
static int getChannelsLength(void *chip_data)
{
	int ret;
	u8 data[2];
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

	ret = readConfig(info, ADDR_CONFIG_SENSE_LEN, data, 2);

	if (ret < OK) {
		TPD_INFO("getChannelsLength: ERROR %08X\n", ret);

		return ret;
	}

	info->systemInfo.u8_scrRxLen = (int)data[0];
	info->systemInfo.u8_scrTxLen = (int)data[1];

	TPD_DEBUG("Force_len = %d   Sense_Len = %d\n", info->systemInfo.u8_scrTxLen,
		  info->systemInfo.u8_scrRxLen);

	return OK;
}



/**
  * Read and pack the frame data related to the nodes
  * @param address address in memory when the frame data node start
  * @param size amount of data to read
  * @param frame pointer to an array of bytes which will contain the frame node
  * data
  * @return OK if success or an error code which specify the type of error
  */
static int getFrameData(void *chip_data, u16 address, int size, short *frame)
{
	int i, j, ret;
	u8 *data = (u8 *)kzalloc(size * sizeof(u8), GFP_KERNEL);
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

	if (data == NULL) {
		TPD_INFO("getFrameData: ERROR %08X\n", ERROR_ALLOC);
		return ERROR_ALLOC;
	}

	ret = fts_writeReadU8UX(info, FTS_CMD_FRAMEBUFFER_R, BITS_16, address, data,
				size, DUMMY_FRAMEBUFFER);

	if (ret < OK) {
		TPD_INFO("getFrameData: ERROR %08X\n", ERROR_BUS_R);
		kfree(data);
		return ERROR_BUS_R;
	}

	j = 0;

	for (i = 0; i < size; i += 2) {
		frame[j] = (short)((data[i + 1] << 8) + data[i]);
		j++;
	}

	kfree(data);
	return OK;
}


/**
  * Return the number of Sense Channels (Rx)
  * @return number of Rx channels
  */
static int getSenseLen(void *chip_data)
{
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

	if (info->systemInfo.u8_scrRxLen == 0)
		getChannelsLength(chip_data);

	return info->systemInfo.u8_scrRxLen;
}

/**
  * Return the number of Force Channels (Tx)
  * @return number of Tx channels
  */
static int getForceLen(void *chip_data)
{
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

	if (info->systemInfo.u8_scrTxLen == 0)
		getChannelsLength(chip_data);

	return info->systemInfo.u8_scrTxLen;
}


/********************    New API     **************************/

/**
  * Read a MS Frame from frame buffer memory
  * @param type type of MS frame to read
  * @param frame pointer to MutualSenseFrame variable which will contain the
  * data
  * @return > 0 if success specifying the number of node into the frame or
  * an error code which specify the type of error
  */
static int getMSFrame3(void *chip_data, MSFrameType type,
		       MutualSenseFrame *frame)
{
	u16 offset;
	int ret, force_len, sense_len;
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;


	force_len = getForceLen(info);
	sense_len = getSenseLen(info);

	frame->node_data = NULL;


	TPD_DEBUG("%s: Starting to get frame %02X\n", __func__, type);

	switch (type) {
	case MS_RAW:
		offset = info->systemInfo.u16_msTchRawAddr;
		goto LOAD_NORM;

	case MS_FILTER:
		offset = info->systemInfo.u16_msTchFilterAddr;
		goto LOAD_NORM;

	case MS_STRENGTH:
		offset = info->systemInfo.u16_msTchStrenAddr;
		goto LOAD_NORM;

	case MS_BASELINE:
		offset = info->systemInfo.u16_msTchBaselineAddr;
	LOAD_NORM:

		if (force_len == 0 || sense_len == 0) {
			TPD_INFO("%s: number of channels not initialized ERROR %08X\n", __func__,
				 ERROR_CH_LEN);
			return ERROR_CH_LEN | ERROR_GET_FRAME;
		}

		break;

	case MS_KEY_RAW:
		offset = info->systemInfo.u16_keyRawAddr;
		goto LOAD_KEY;

	case MS_KEY_FILTER:
		offset = info->systemInfo.u16_keyFilterAddr;
		goto LOAD_KEY;

	case MS_KEY_STRENGTH:
		offset = info->systemInfo.u16_keyStrenAddr;
		goto LOAD_KEY;

	case MS_KEY_BASELINE:
		offset = info->systemInfo.u16_keyBaselineAddr;
	LOAD_KEY:

		if (info->systemInfo.u8_keyLen == 0) {
			TPD_INFO("%s: number of channels not initialized ERROR %08X\n", __func__,
				 ERROR_CH_LEN);
			return ERROR_CH_LEN | ERROR_GET_FRAME;
		}

		force_len = 1;
		sense_len = info->systemInfo.u8_keyLen;
		break;

	case FRC_RAW:
		offset = info->systemInfo.u16_frcRawAddr;
		goto LOAD_FRC;

	case FRC_FILTER:
		offset = info->systemInfo.u16_frcFilterAddr;
		goto LOAD_FRC;

	case FRC_STRENGTH:
		offset = info->systemInfo.u16_frcStrenAddr;
		goto LOAD_FRC;

	case FRC_BASELINE:
		offset = info->systemInfo.u16_frcBaselineAddr;
	LOAD_FRC:

		if (force_len == 0) {
			TPD_INFO("%s: number of channels not initialized ERROR %08X\n", __func__,
				 ERROR_CH_LEN);
			return ERROR_CH_LEN | ERROR_GET_FRAME;
		}

		sense_len = 1;
		break;

	default:
		TPD_INFO("%s: Invalid type ERROR %08X\n", __func__,
			 ERROR_OP_NOT_ALLOW | ERROR_GET_FRAME);
		return ERROR_OP_NOT_ALLOW | ERROR_GET_FRAME;
	}

	frame->node_data_size = ((force_len) * sense_len);
	frame->header.force_node = force_len;
	frame->header.sense_node = sense_len;
	frame->header.type = type;

	TPD_DEBUG("%s: Force_len = %d Sense_len = %d Offset = %04X\n", __func__,
		  force_len, sense_len, offset);

	frame->node_data = (short *)kzalloc(frame->node_data_size * sizeof(short),
					    GFP_KERNEL);

	if (frame->node_data == NULL) {
		TPD_INFO("%s: ERROR %08X\n", __func__, ERROR_ALLOC | ERROR_GET_FRAME);
		return ERROR_ALLOC | ERROR_GET_FRAME;
	}

	ret = getFrameData(info, offset, frame->node_data_size * BYTES_PER_NODE,
			   (frame->node_data));

	if (ret < OK) {
		TPD_INFO("%s: ERROR %08X\n", __func__, ERROR_GET_FRAME_DATA);
		kfree(frame->node_data);
		frame->node_data = NULL;
		return ret | ERROR_GET_FRAME_DATA | ERROR_GET_FRAME;
	}

	/* if you want to access one node i,j,
	 * compute the offset like: offset = i*columns + j = > frame[i, j] */

	TPD_DEBUG("Frame acquired!\n");
	return frame->node_data_size;
	/* return the number of data put inside frame */
}

/**
  * Read a SS Frame from frame buffer
  * @param type type of SS frame to read
  * @param frame pointer to SelfSenseFrame variable which will contain the data
  * @return > 0 if success specifying the number of node into frame or an
  *  error code which specify the type of error
  */
int getSSFrame3(void *chip_data, SSFrameType type, SelfSenseFrame *frame)
{
	u16 offset_force, offset_sense;
	int ret;
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

	frame->force_data = NULL;
	frame->sense_data = NULL;

	frame->header.force_node = getForceLen(info);    /* use getForce/SenseLen
													* because introduce a
                                                	* recover mechanism in
                                                	* case of len = 0 */
	frame->header.sense_node = getSenseLen(info);

	if (frame->header.force_node == 0 || frame->header.sense_node == 0) {
		TPD_INFO("%s: number of channels not initialized ERROR %08X\n", __func__,
			 ERROR_CH_LEN);
		return ERROR_CH_LEN | ERROR_GET_FRAME;
	}


	TPD_DEBUG("%s: Starting to get frame %02X\n", __func__, type);

	switch (type) {
	case SS_RAW:
		offset_force = info->systemInfo.u16_ssTchTxRawAddr;
		offset_sense = info->systemInfo.u16_ssTchRxRawAddr;
		break;

	case SS_FILTER:
		offset_force = info->systemInfo.u16_ssTchTxFilterAddr;
		offset_sense = info->systemInfo.u16_ssTchRxFilterAddr;
		break;

	case SS_STRENGTH:
		offset_force = info->systemInfo.u16_ssTchTxStrenAddr;
		offset_sense = info->systemInfo.u16_ssTchRxStrenAddr;
		break;

	case SS_BASELINE:
		offset_force = info->systemInfo.u16_ssTchTxBaselineAddr;
		offset_sense = info->systemInfo.u16_ssTchRxBaselineAddr;
		break;

	case SS_HVR_RAW:
		offset_force = info->systemInfo.u16_ssHvrTxRawAddr;
		offset_sense = info->systemInfo.u16_ssHvrRxRawAddr;
		break;

	case SS_HVR_FILTER:
		offset_force = info->systemInfo.u16_ssHvrTxFilterAddr;
		offset_sense = info->systemInfo.u16_ssHvrRxFilterAddr;
		break;

	case SS_HVR_STRENGTH:
		offset_force = info->systemInfo.u16_ssHvrTxStrenAddr;
		offset_sense = info->systemInfo.u16_ssHvrRxStrenAddr;
		break;

	case SS_HVR_BASELINE:
		offset_force = info->systemInfo.u16_ssHvrTxBaselineAddr;
		offset_sense = info->systemInfo.u16_ssHvrRxBaselineAddr;
		break;

	case SS_PRX_RAW:
		offset_force = info->systemInfo.u16_ssPrxTxRawAddr;
		offset_sense = info->systemInfo.u16_ssPrxRxRawAddr;
		break;

	case SS_PRX_FILTER:
		offset_force = info->systemInfo.u16_ssPrxTxFilterAddr;
		offset_sense = info->systemInfo.u16_ssPrxRxFilterAddr;
		break;

	case SS_PRX_STRENGTH:
		offset_force = info->systemInfo.u16_ssPrxTxStrenAddr;
		offset_sense = info->systemInfo.u16_ssPrxRxStrenAddr;
		break;

	case SS_PRX_BASELINE:
		offset_force = info->systemInfo.u16_ssPrxTxBaselineAddr;
		offset_sense = info->systemInfo.u16_ssPrxRxBaselineAddr;
		break;

	case SS_DETECT_RAW:
		if (info->systemInfo.u8_ssDetScanSet == 0) {
			offset_force = info->systemInfo.u16_ssDetRawAddr;
			offset_sense = 0;
			frame->header.sense_node = 0;

		} else {
			offset_sense = info->systemInfo.u16_ssDetRawAddr;
			offset_force = 0;
			frame->header.force_node = 0;
		}

		break;

	case SS_DETECT_FILTER:
		if (info->systemInfo.u8_ssDetScanSet == 0) {
			offset_force = info->systemInfo.u16_ssDetFilterAddr;
			offset_sense = 0;
			frame->header.sense_node = 0;

		} else {
			offset_sense = info->systemInfo.u16_ssDetFilterAddr;
			offset_force = 0;
			frame->header.force_node = 0;
		}

		break;

	case SS_DETECT_BASELINE:
		if (info->systemInfo.u8_ssDetScanSet == 0) {
			offset_force = info->systemInfo.u16_ssDetBaselineAddr;
			offset_sense = 0;
			frame->header.sense_node = 0;

		} else {
			offset_sense = info->systemInfo.u16_ssDetBaselineAddr;
			offset_force = 0;
			frame->header.force_node = 0;
		}

		break;

	case SS_DETECT_STRENGTH:
		if (info->systemInfo.u8_ssDetScanSet == 0) {
			offset_force = info->systemInfo.u16_ssDetStrenAddr;
			offset_sense = 0;
			frame->header.sense_node = 0;

		} else {
			offset_sense = info->systemInfo.u16_ssDetStrenAddr;
			offset_force = 0;
			frame->header.force_node = 0;
		}

		break;

	default:
		TPD_INFO("%s: Invalid type ERROR %08X\n", __func__,
			 ERROR_OP_NOT_ALLOW | ERROR_GET_FRAME);
		return ERROR_OP_NOT_ALLOW | ERROR_GET_FRAME;
	}

	frame->header.type = type;

	TPD_DEBUG("%s: Force_len = %d Sense_len = %d Offset_force = %04X Offset_sense = %04X\n",
		  __func__, frame->header.force_node,
		  frame->header.sense_node, offset_force, offset_sense);

	frame->force_data = (short *)kzalloc(frame->header.force_node * sizeof(short),
					     GFP_KERNEL);

	if (frame->force_data == NULL) {
		TPD_INFO("%s: can not allocate force_data ERROR %08X\n", __func__,
			 ERROR_ALLOC | ERROR_GET_FRAME);
		return ERROR_ALLOC | ERROR_GET_FRAME;
	}

	frame->sense_data = (short *)kzalloc(frame->header.sense_node * sizeof(short),
					     GFP_KERNEL);

	if (frame->sense_data == NULL) {
		kfree(frame->force_data);
		frame->force_data = NULL;
		TPD_INFO("%s: can not allocate sense_data ERROR %08X\n", __func__,
			 ERROR_ALLOC | ERROR_GET_FRAME);
		return ERROR_ALLOC | ERROR_GET_FRAME;
	}

	ret = getFrameData(info, offset_force,
			   frame->header.force_node * BYTES_PER_NODE, (frame->force_data));

	if (ret < OK) {
		TPD_INFO("%s: error while reading force data ERROR %08X\n",
			 __func__, ERROR_GET_FRAME_DATA);
		kfree(frame->force_data);
		frame->force_data = NULL;
		kfree(frame->sense_data);
		frame->sense_data = NULL;
		return ret | ERROR_GET_FRAME_DATA | ERROR_GET_FRAME;
	}

	ret = getFrameData(info, offset_sense,
			   frame->header.sense_node * BYTES_PER_NODE, (frame->sense_data));

	if (ret < OK) {
		TPD_INFO("%s: error while reading sense data ERROR %08X\n", __func__,
			 ERROR_GET_FRAME_DATA);
		kfree(frame->force_data);
		frame->force_data = NULL;
		kfree(frame->sense_data);
		frame->sense_data = NULL;
		return ret | ERROR_GET_FRAME_DATA | ERROR_GET_FRAME;
	}

	/* if you want to access one node i,j, you should compute the offset
	 * like: offset = i*columns + j = > frame[i, j] */

	TPD_DEBUG("Frame acquired!\n");
	return frame->header.force_node + frame->header.sense_node;
	/* return the number of data put inside frame */
}

/**
  * Take the starting time and save it in a StopWatch variable
  * @param w pointer of a StopWatch struct
  */
static void startStopWatch(StopWatch *w)
{
	ktime_get_ts(&w->start);
}

/**
  * Take the stop time and save it in a StopWatch variable
  * @param w pointer of a StopWatch struct
  */
static void stopStopWatch(StopWatch *w)
{
	ktime_get_ts(&w->end);
}

/**
  * Compute the amount of time spent from when the startStopWatch and then
  * the stopStopWatch were called on the StopWatch variable
  * @param w pointer of a StopWatch struct
  * @return amount of time in ms (the return value is meaningless
  * if the startStopWatch and stopStopWatch were not called before)
  */
static int elapsedMillisecond(StopWatch *w)
{
	int result;

	result = ((w->end.tv_sec - w->start.tv_sec) * 1000)
		 + (w->end.tv_nsec - w->start.tv_nsec) / 1000000;
	return result;
}

/**
  * Initialize core variables of the library.
  * Must be called during the probe before any other lib function
  * @param info pointer to fts_ts_info which contains info about the device and
  * its hw setup
  * @return OK if success or an error code which specify the type of error
  */
static int initCore(void *chip_data, int reset_gpio)
{
	int ret = OK;
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

	TPD_DEBUG("%s: Initialization of the Core...\n", __func__);
	ret |= resetErrorList(info);

	if (ret < OK)
		TPD_DEBUG("%s: Initialization Core ERROR %08X!\n", __func__, ret);

	else
		TPD_DEBUG("%s: Initialization Finished!\n", __func__);

	return ret;
}

/**
  * Perform a system reset of the IC.
  * If the reset pin is associated to a gpio, the function execute an hw reset
  * (toggling of reset pin) otherwise send an hw command to the IC
  * @return OK if success or an error code which specify the type of error
  */
static int fts_system_reset(void *chip_data)
{
	u8 readdata[FIFO_EVENT_SIZE];
	int event_to_search;
	int res = -1;
	int i = 0;
	u8 data[1] = { SYSTEM_RESET_VALUE };
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

	event_to_search = (int)EVT_ID_CONTROLLER_READY;

	fts_disableInterruptNoSync(chip_data);

	TPD_DEBUG("System resetting...\n");

	for (i = 0; i < RETRY_SYSTEM_RESET && res < 0; i++) {
		resetErrorList(chip_data);

		if (info->hw_res->reset_gpio == GPIO_NOT_DEFINED)
			res = fts_writeU8UX(info, FTS_CMD_HW_REG_W, ADDR_SIZE_HW_REG, ADDR_SYSTEM_RESET,
					    data, ARRAY_SIZE(data));

		else {
			gpio_direction_output(info->hw_res->reset_gpio, 0);
			msleep(4);
			gpio_direction_output(info->hw_res->reset_gpio, 1);
			res = OK;
		}

		if (res < OK)
			TPD_INFO("fts_system_reset: ERROR %08X\n", ERROR_BUS_W);

		else {
			res = pollForEvent(info, &event_to_search, 1, readdata, GENERAL_TIMEOUT);

			if (res < OK)
				TPD_INFO("fts_system_reset: ERROR %08X\n", res);
		}
	}

	fts_enableInterrupt(info);

	if (res < OK) {
		TPD_INFO("fts_system_reset...failed after 3 attempts: ERROR %08X\n",
			 (res | ERROR_SYSTEM_RESET_FAIL));
		return res | ERROR_SYSTEM_RESET_FAIL;

	} else {
		TPD_DEBUG("System reset DONE!\n");
		return OK;
	}
}

/**
  * Poll the FIFO looking for a specified event within a timeout. Support a
  * retry mechanism.
  * @param event_to_search pointer to an array of int where each element
  * correspond to a byte of the event to find.
  * If the element of the array has value -1, the byte of the event,
  * in the same position of the element is ignored.
  * @param event_bytes size of event_to_search
  * @param readdata pointer to an array of byte which will contain the event
  * found
  * @param time_to_wait time to wait before going in timeout
  * @return OK if success or an error code which specify the type of error
  */
static int pollForEvent(void *chip_data, int *event_to_search, int event_bytes,
			u8 *readdata, int time_to_wait)
{
	int i, find, retry, count_err;
	int time_to_count;
	int err_handling = OK;
	StopWatch clock;
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

	u8 cmd[1] = { FIFO_CMD_READONE };

	find = 0;
	retry = 0;
	count_err = 0;
	time_to_count = time_to_wait / TIMEOUT_RESOLUTION;

	startStopWatch(&clock);

	while (find != 1 && retry < time_to_count &&
			fts_writeReadU8UX(info, cmd[0], 0, 0, readdata, FIFO_EVENT_SIZE,
					  DUMMY_FIFO) >= OK) {
		/* Log of errors */
		if (readdata[0] == EVT_ID_ERROR) {
			TPD_INFO("ERROR EVENT = %02X %02X %02X %02X %02X %02X %02X %02X\n",
				 readdata[0], readdata[1], readdata[2], readdata[3], readdata[4], readdata[5],
				 readdata[6], readdata[7]);
			count_err++;
			err_handling = errorHandler(info, readdata, FIFO_EVENT_SIZE);

			if ((err_handling & 0xF0FF0000) == ERROR_HANDLER_STOP_PROC) {
				TPD_INFO("pollForEvent: forced to be stopped! ERROR %08X\n", err_handling);
				return err_handling;
			}

		} else {
			if (readdata[0] != EVT_ID_NOEVENT) {
				TPD_INFO("READ EVENT = %02X %02X %02X %02X %02X %02X %02X %02X\n",
					 readdata[0], readdata[1], readdata[2], readdata[3], readdata[4], readdata[5],
					 readdata[6], readdata[7]);
			}

			if (readdata[0] == EVT_ID_CONTROLLER_READY
					&& event_to_search[0] != EVT_ID_CONTROLLER_READY)
				TPD_INFO("pollForEvent: Unmanned Controller Ready Event! Setting reset flags...\n");
		}

		find = 1;

		for (i = 0; i < event_bytes; i++) {
			if (event_to_search[i] != -1 && (int)readdata[i] != event_to_search[i]) {
				find = 0;
				break;
			}
		}

		retry++;
		msleep(TIMEOUT_RESOLUTION);
	}

	stopStopWatch(&clock);

	if ((retry >= time_to_count) && find != 1) {
		TPD_INFO("pollForEvent: ERROR %08X\n", ERROR_TIMEOUT);
		return ERROR_TIMEOUT;

	} else if (find == 1) {
		TPD_INFO("FOUND EVENT = %02X %02X %02X %02X %02X %02X %02X %02X\n",
			 readdata[0], readdata[1], readdata[2], readdata[3], readdata[4], readdata[5],
			 readdata[6], readdata[7]);
		TPD_DEBUG("Event found in %d ms (%d iterations)! Number of errors found = %d\n",
			  elapsedMillisecond(&clock), retry, count_err);
		return count_err;

	} else {
		TPD_INFO("pollForEvent: ERROR %08X\n", ERROR_BUS_R);
		return ERROR_BUS_R;
	}
}

/**
  * Check that the FW sent the echo even after a command was sent
  * @param cmd pointer to an array of byte which contain the command previously
  * sent
  * @param size size of cmd
  * @return OK if success or an error code which specify the type of error
  */
static int checkEcho(void *chip_data, u8 *cmd, int size)
{
	int ret, i;
	int event_to_search[FIFO_EVENT_SIZE];
	u8 readdata[FIFO_EVENT_SIZE];
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;


	if (size < 1) {
		TPD_INFO("checkEcho: Invalid Size = %d not valid! ERROR %08X\n", size,
			 ERROR_OP_NOT_ALLOW);
		return ERROR_OP_NOT_ALLOW;

	} else {
		if ((size + 3) > FIFO_EVENT_SIZE)
			size = FIFO_EVENT_SIZE - 3;

		/* Echo event 0x43 0x01 xx xx xx xx xx fifo_status
		 * therefore command with more than 5 bytes will be trunked */

		event_to_search[0] = EVT_ID_STATUS_UPDATE;
		event_to_search[1] = EVT_TYPE_STATUS_ECHO;

		for (i = 2; i < size + 2; i++)
			event_to_search[i] = cmd[i - 2];

		ret = pollForEvent(info, event_to_search, size + 2, readdata, TIEMOUT_ECHO);

		if (ret < OK) {
			TPD_INFO("checkEcho: Echo Event not found! ERROR %08X\n", ret);
			return ret | ERROR_CHECK_ECHO_FAIL;

		} else if (ret > OK) {
			TPD_INFO("checkEcho: Echo Event found but with some error events before! num_error = %d\n",
				 ret);
			return ERROR_CHECK_ECHO_FAIL;
		}

		TPD_DEBUG("ECHO OK!\n");
		return ret;
	}
}

/**
  * Set a scan mode in the IC
  * @param mode scan mode to set; possible values @link scan_opt Scan Mode
  * Option @endlink
  * @param settings option for the selected scan mode
  * (for example @link active_bitmask Active Mode Bitmask @endlink)
  * @return OK if success or an error code which specify the type of error
  */
static int setScanMode(void *chip_data, u8 mode, u8 settings)
{
	u8 cmd[3] = { FTS_CMD_SCAN_MODE, mode, settings };
	int ret, size = 3;
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

	TPD_DEBUG("%s: Setting scan mode: mode = %02X settings = %02X !\n", __func__,
		  mode, settings);

	if (mode == SCAN_MODE_LOW_POWER)
		size = 2;

	ret = fts_write(info, cmd, size);    /* use write instead of writeFw because
                                	* can be called while the interrupt are
                                	* enabled */

	if (ret < OK) {
		TPD_INFO("%s: write failed...ERROR %08X !\n", __func__, ret);
		return ret | ERROR_SET_SCAN_MODE_FAIL;
	}

	TPD_DEBUG("%s: Setting scan mode OK!\n", __func__);
	return OK;
}

/**
  * Set a feature and its option in the IC
  * @param feat feature to set; possible values @link feat_opt Feature Selection
  * Option @endlink
  * @param settings pointer to an array of byte which store the options for
  * the selected feature (for example the gesture mask to activate
  * @link gesture_opt Gesture IDs @endlink)
  * @param size in bytes of settings
  * @return OK if success or an error code which specify the type of error
  */
int setFeatures(void *chip_data, u8 feat, u8 *settings, int size)
{
	u8 *cmd = NULL;
	int i = 0;
	int ret;
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

	cmd = (u8 *) kmalloc(2 + size, GFP_KERNEL);

	if (!cmd) {
		TPD_INFO("%s: kmalloc failed...ERROR %08X !\n",
			 __func__, -1);
		return -1;
	}

	TPD_DEBUG("%s: Setting feature: feat = %02X !\n", __func__,
		  feat);
	cmd[0] = FTS_CMD_FEATURE;
	cmd[1] = feat;
	TPD_DEBUG("%s: Settings = ", __func__);

	for (i = 0; i < size; i++) {
		cmd[2 + i] = settings[i];
		TPD_DEBUG("%02X ", settings[i]);
	}

	TPD_DEBUG("\n");
	ret = fts_write(info, cmd, 2 + size);    /* use write instead of writeFw because
												* can be called while the interrupts
												* are enabled */

	if (ret < OK) {
		TPD_INFO("%s: write failed...ERROR %08X !\n", __func__, ret);
		kfree(cmd);
		return ret | ERROR_SET_FEATURE_FAIL;
	}

	TPD_DEBUG("%s: Setting feature OK!\n", __func__);
	kfree(cmd);
	return OK;
}

/**
  * Write a system command to the IC
  * @param sys_cmd System Command to execute; possible values
  * @link sys_opt System Command Option @endlink
  * @param sett settings option for the selected system command
  * (@link sys_special_opt Special Command Option @endlink, @link ito_opt
  * ITO Test Option @endlink, @link load_opt Load Host Data Option @endlink)
  * @param size in bytes of settings
  * @return OK if success or an error code which specify the type of error
  */
static int writeSysCmd(void *chip_data, u8 sys_cmd, u8 *sett, int size)
{
	u8 *cmd;
	int ret;
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

	cmd = (u8 *)kmalloc(2 + size, GFP_KERNEL);

	if (!cmd) {
		TPD_INFO("%s: kmalloc failed...ERROR %08X !\n",
			 __func__, -1);
		return -1;
	}

	cmd[0] = FTS_CMD_SYSTEM;
	cmd[1] = sys_cmd;

	TPD_DEBUG("%s: Command = %02X %02X ", __func__, cmd[0], cmd[1]);

	for (ret = 0; ret < size; ret++) {
		cmd[2 + ret] = sett[ret];
		TPD_DEBUG("%02X ", cmd[2 + ret]);
	}

	TPD_DEBUG("\n");
	TPD_DEBUG("%s: Writing Sys command...\n", __func__);

	if (sys_cmd != SYS_CMD_LOAD_DATA)
		ret = fts_writeFwCmd(info, cmd, 2 + size);

	else {
		if (size >= 1)
			ret = requestSyncFrame(info, sett[0]);

		else {
			TPD_INFO("%s: No setting argument! ERROR %08X\n", __func__, ERROR_OP_NOT_ALLOW);
			kfree(cmd);
			return ERROR_OP_NOT_ALLOW;
		}
	}

	if (ret < OK)
		TPD_INFO("%s: ERROR %08X\n", __func__, ret);

	else
		TPD_DEBUG("%s: FINISHED!\n", __func__);

	kfree(cmd);

	return ret;
}

/**
  * Initialize the System Info Struct with default values according to the error
  * found during the reading
  * @param i2cError 1 if there was an I2C error while reading the System Info
  * data from memory, other value if another error occurred
  * @return OK if success or an error code which specify the type of error
  */
int defaultSysInfo(void *chip_data, int i2cError)
{
	int i;
	/* system related info */
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

	TPD_DEBUG("Setting default System Info...\n");

	if (i2cError == 1) {
		info->systemInfo.u16_fwVer = 0xFFFF;
		info->systemInfo.u16_cfgProjectId = 0xFFFF;

		for (i = 0; i < RELEASE_INFO_SIZE; i++)
			info->systemInfo.u8_releaseInfo[i] = 0xFF;

		info->systemInfo.u16_cxVer = 0xFFFF;

	} else {
		info->systemInfo.u16_fwVer = 0x0000;
		info->systemInfo.u16_cfgProjectId = 0x0000;

		for (i = 0; i < RELEASE_INFO_SIZE; i++)
			info->systemInfo.u8_releaseInfo[i] = 0x00;

		info->systemInfo.u16_cxVer = 0x0000;
	}

	info->systemInfo.u8_scrRxLen = 0;
	info->systemInfo.u8_scrTxLen = 0;

	TPD_DEBUG("default System Info DONE!\n");
	return OK;
}

/**
  * Read the System Info data from memory. System Info is loaded automatically
  * after every system reset.
  * @param request if 1, will be asked to the FW to reload the data, otherwise
  * attempt to read it directly from memory
  * @return OK if success or an error code which specify the type of error
  */
int readSysInfo(void *chip_data, int request)
{
	int ret, i, index = 0;
	u8 sett = LOAD_SYS_INFO;
	u8 data[SYS_INFO_SIZE] = { 0 };
	char temp[256] = { 0 };

	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

	if (request == 1) {
		TPD_DEBUG("%s: Requesting System Info...\n", __func__);

		ret = writeSysCmd(info, SYS_CMD_LOAD_DATA, &sett, 1);

		if (ret < OK) {
			TPD_INFO("%s: error while writing the sys cmd ERROR %08X\n", __func__, ret);
			goto FAIL;
		}
	}

	TPD_DEBUG("%s: Reading System Info...\n", __func__);
	ret = fts_writeReadU8UX(info, FTS_CMD_FRAMEBUFFER_R, BITS_16, ADDR_FRAMEBUFFER,
				data, SYS_INFO_SIZE, DUMMY_FRAMEBUFFER);

	if (ret < OK) {
		TPD_INFO("%s: error while reading the system data ERROR %08X\n", __func__, ret);
		goto FAIL;
	}

	TPD_DEBUG("%s: Parsing System Info...\n", __func__);

	if (data[0] != HEADER_SIGNATURE) {
		TPD_INFO("%s: The Header Signature is wrong!  sign: %02X != %02X ERROR %08X\n",
			 __func__, data[0], HEADER_SIGNATURE, ERROR_WRONG_DATA_SIGN);
		ret = ERROR_WRONG_DATA_SIGN;
		goto FAIL;
	}

	if (data[1] != LOAD_SYS_INFO) {
		TPD_INFO("%s: The Data ID is wrong!  ids: %02X != %02X ERROR %08X\n", __func__,
			 data[3], LOAD_SYS_INFO, ERROR_DIFF_DATA_TYPE);
		ret = ERROR_DIFF_DATA_TYPE;
		goto FAIL;
	}

	index += 4;
	u8ToU16(&data[index], &info->systemInfo.u16_apiVer_rev);
	index += 2;
	info->systemInfo.u8_apiVer_minor = data[index++];
	info->systemInfo.u8_apiVer_major = data[index++];
	u8ToU16(&data[index], &info->systemInfo.u16_chip0Ver);
	index += 2;
	u8ToU16(&data[index], &info->systemInfo.u16_chip0Id);
	index += 2;
	u8ToU16(&data[index], &info->systemInfo.u16_chip1Ver);
	index += 2;
	u8ToU16(&data[index], &info->systemInfo.u16_chip1Id);
	index += 2;
	u8ToU16(&data[index], &info->systemInfo.u16_fwVer);
	index += 2;
	TPD_INFO("FW VER = %04X\n", info->systemInfo.u16_fwVer);
	u8ToU16(&data[index], &info->systemInfo.u16_svnRev);
	index += 2;
	TPD_INFO("SVN REV = %04X\n", info->systemInfo.u16_svnRev);
	u8ToU16(&data[index], &info->systemInfo.u16_cfgVer);
	index += 2;
	TPD_INFO("CONFIG VER = %04X\n", info->systemInfo.u16_cfgVer);
	u8ToU16(&data[index], &info->systemInfo.u16_cfgProjectId);
	index += 2;
	TPD_INFO("CONFIG PROJECT ID = %04X\n", info->systemInfo.u16_cfgProjectId);
	u8ToU16(&data[index], &info->systemInfo.u16_cxVer);
	index += 2;
	TPD_INFO("CX VER = %04X\n", info->systemInfo.u16_cxVer);
	u8ToU16(&data[index], &info->systemInfo.u16_cxProjectId);
	index += 2;
	TPD_INFO("CX PROJECT ID = %04X\n", info->systemInfo.u16_cxProjectId);
	info->systemInfo.u8_cfgAfeVer = data[index++];
	info->systemInfo.u8_cxAfeVer =  data[index++];
	info->systemInfo.u8_panelCfgAfeVer = data[index++];
	TPD_INFO("AFE VER: CFG = %02X - CX = %02X - PANEL = %02X\n",
		 info->systemInfo.u8_cfgAfeVer, info->systemInfo.u8_cxAfeVer,
		 info->systemInfo.u8_panelCfgAfeVer);
	info->systemInfo.u8_protocol = data[index++];
	TPD_INFO("Protocol = %02X\n", info->systemInfo.u8_protocol);

	/* index+= 1;
	 * reserved area */
	for (i = 0; i < DIE_INFO_SIZE; i++)
		info->systemInfo.u8_dieInfo[i] = data[index++];

	/* TPD_INFO( "%02X ", systemInfo.u8_dieInfo[i]); */
	/* TPD_INFO( "\n"); */
	TPD_INFO("%s\n", printHex("Die Info =  ", info->systemInfo.u8_dieInfo,
				  DIE_INFO_SIZE, temp));
	memset(temp, 0, 256);

	/* TPD_INFO("Release Info =  "); */
	for (i = 0; i < RELEASE_INFO_SIZE; i++)
		info->systemInfo.u8_releaseInfo[i] = data[index++];

	/* TPD_INFO( "%02X ", systemInfo.u8_releaseInfo[i]); */
	/* TPD_INFO( "\n"); */

	TPD_INFO("%s\n", printHex("Release Info =  ", info->systemInfo.u8_releaseInfo,
				  RELEASE_INFO_SIZE, temp));
	memset(temp, 0, 256);

	u8ToU32(&data[index], &info->systemInfo.u32_fwCrc);
	index += 4;
	u8ToU32(&data[index], &info->systemInfo.u32_cfgCrc);
	index += 4;

	index += 4;    /* skip reserved area */

	/* systemInfo.u8_mpFlag = data[index]; */
	/* TPD_INFO("MP FLAG = %02X\n", systemInfo.u8_mpFlag); */
	index += 4;
	u8ToU32(&data[index], &info->systemInfo.u32_flash_org_info);
	TPD_INFO("Flash Organization Information= 0X%08X \n",
		 info->systemInfo.u32_flash_org_info);

	index += 4; /* +3 remaining from mp flag address */

	info->systemInfo.u8_ssDetScanSet = data[index];
	TPD_INFO("SS Detect Scan Select = %d \n", info->systemInfo.u8_ssDetScanSet);
	index += 4;

	u8ToU16(&data[index], &info->systemInfo.u16_scrResX);
	index += 2;
	u8ToU16(&data[index], &info->systemInfo.u16_scrResY);
	index += 2;
	TPD_INFO("Screen Resolution = %d x %d\n", info->systemInfo.u16_scrResX,
		 info->systemInfo.u16_scrResY);
	info->systemInfo.u8_scrTxLen = data[index++];
	TPD_INFO("TX Len = %d\n", info->systemInfo.u8_scrTxLen);
	info->systemInfo.u8_scrRxLen = data[index++];
	TPD_INFO("RX Len = %d\n", info->systemInfo.u8_scrRxLen);
	info->systemInfo.u8_keyLen = data[index++];
	TPD_INFO("Key Len = %d\n", info->systemInfo.u8_keyLen);
	info->systemInfo.u8_forceLen = data[index++];
	TPD_INFO("Force Len = %d\n", info->systemInfo.u8_forceLen);

	index += 40;    /* skip reserved area */

	u8ToU16(&data[index], &info->systemInfo.u16_dbgInfoAddr);
	index += 2;

	index += 6;    /* skip reserved area */

	u8ToU16(&data[index], &info->systemInfo.u16_msTchRawAddr);
	index += 2;
	u8ToU16(&data[index], &info->systemInfo.u16_msTchFilterAddr);
	index += 2;
	u8ToU16(&data[index], &info->systemInfo.u16_msTchStrenAddr);
	index += 2;
	u8ToU16(&data[index], &info->systemInfo.u16_msTchBaselineAddr);
	index += 2;

	u8ToU16(&data[index], &info->systemInfo.u16_ssTchTxRawAddr);
	index += 2;
	u8ToU16(&data[index], &info->systemInfo.u16_ssTchTxFilterAddr);
	index += 2;
	u8ToU16(&data[index], &info->systemInfo.u16_ssTchTxStrenAddr);
	index += 2;
	u8ToU16(&data[index], &info->systemInfo.u16_ssTchTxBaselineAddr);
	index += 2;

	u8ToU16(&data[index], &info->systemInfo.u16_ssTchRxRawAddr);
	index += 2;
	u8ToU16(&data[index], &info->systemInfo.u16_ssTchRxFilterAddr);
	index += 2;
	u8ToU16(&data[index], &info->systemInfo.u16_ssTchRxStrenAddr);
	index += 2;
	u8ToU16(&data[index], &info->systemInfo.u16_ssTchRxBaselineAddr);
	index += 2;

	u8ToU16(&data[index], &info->systemInfo.u16_keyRawAddr);
	index += 2;
	u8ToU16(&data[index], &info->systemInfo.u16_keyFilterAddr);
	index += 2;
	u8ToU16(&data[index], &info->systemInfo.u16_keyStrenAddr);
	index += 2;
	u8ToU16(&data[index], &info->systemInfo.u16_keyBaselineAddr);
	index += 2;

	u8ToU16(&data[index], &info->systemInfo.u16_frcRawAddr);
	index += 2;
	u8ToU16(&data[index], &info->systemInfo.u16_frcFilterAddr);
	index += 2;
	u8ToU16(&data[index], &info->systemInfo.u16_frcStrenAddr);
	index += 2;
	u8ToU16(&data[index], &info->systemInfo.u16_frcBaselineAddr);
	index += 2;

	u8ToU16(&data[index], &info->systemInfo.u16_ssHvrTxRawAddr);
	index += 2;
	u8ToU16(&data[index], &info->systemInfo.u16_ssHvrTxFilterAddr);
	index += 2;
	u8ToU16(&data[index], &info->systemInfo.u16_ssHvrTxStrenAddr);
	index += 2;
	u8ToU16(&data[index], &info->systemInfo.u16_ssHvrTxBaselineAddr);
	index += 2;

	u8ToU16(&data[index], &info->systemInfo.u16_ssHvrRxRawAddr);
	index += 2;
	u8ToU16(&data[index], &info->systemInfo.u16_ssHvrRxFilterAddr);
	index += 2;
	u8ToU16(&data[index], &info->systemInfo.u16_ssHvrRxStrenAddr);
	index += 2;
	u8ToU16(&data[index], &info->systemInfo.u16_ssHvrRxBaselineAddr);
	index += 2;

	u8ToU16(&data[index], &info->systemInfo.u16_ssPrxTxRawAddr);
	index += 2;
	u8ToU16(&data[index], &info->systemInfo.u16_ssPrxTxFilterAddr);
	index += 2;
	u8ToU16(&data[index], &info->systemInfo.u16_ssPrxTxStrenAddr);
	index += 2;
	u8ToU16(&data[index], &info->systemInfo.u16_ssPrxTxBaselineAddr);
	index += 2;

	u8ToU16(&data[index], &info->systemInfo.u16_ssPrxRxRawAddr);
	index += 2;
	u8ToU16(&data[index], &info->systemInfo.u16_ssPrxRxFilterAddr);
	index += 2;
	u8ToU16(&data[index], &info->systemInfo.u16_ssPrxRxStrenAddr);
	index += 2;
	u8ToU16(&data[index], &info->systemInfo.u16_ssPrxRxBaselineAddr);
	index += 2;

	u8ToU16(&data[index], &info->systemInfo.u16_ssDetRawAddr);
	index += 2;
	u8ToU16(&data[index], &info->systemInfo.u16_ssDetFilterAddr);
	index += 2;
	u8ToU16(&data[index], &info->systemInfo.u16_ssDetStrenAddr);
	index += 2;
	u8ToU16(&data[index], &info->systemInfo.u16_ssDetBaselineAddr);
	index += 2;

	TPD_INFO("Parsed %d bytes!\n", index);

	if (index != SYS_INFO_SIZE) {
		TPD_INFO("%s: index = %d different from %d ERROR %08X\n", __func__, index,
			 SYS_INFO_SIZE, ERROR_OP_NOT_ALLOW);
		return ERROR_OP_NOT_ALLOW;
	}

	readMpFlag(chip_data, &info->systemInfo.u8_mpFlag);

	TPD_INFO("NEW MP FLAG = %02X\n", info->systemInfo.u8_mpFlag);

	readCalibraFlag(chip_data, &info->systemInfo.u8_calibrationFlag);

	TPD_INFO("NEW CALI FLAG = %02X\n", info->systemInfo.u8_calibrationFlag);

	TPD_INFO("System Info Read DONE!\n");
	return OK;

FAIL:
	defaultSysInfo(chip_data, isI2cError(ret));
	return ret;
}

/**
  * Read data from the Config Memory
  * @param offset Starting address in the Config Memory of data to read
  * @param outBuf pointer of a byte array which contain the bytes to read
  * @param len number of bytes to read
  * @return OK if success or an error code which specify the type of error
  */
static int readConfig(void *chip_data, u16 offset, u8 *outBuf, int len)
{
	int ret;
	u64 final_address = offset + ADDR_CONFIG_OFFSET;
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

	TPD_DEBUG("%s: Starting to read config memory at %08llX ...\n", __func__,
		  final_address);
	ret = fts_writeReadU8UX(info, FTS_CMD_CONFIG_R, BITS_16, final_address,
				outBuf, len, DUMMY_CONFIG);

	if (ret < OK) {
		TPD_INFO("%s: Impossible to read Config Memory... ERROR %08X!\n", __func__,
			 ret);
		return ret;
	}

	TPD_DEBUG("%s: Read config memory FINISHED!\n", __func__);
	return OK;
}

/**
  * Disable the interrupt so the ISR of the driver can not be called
  * @return OK if success or an error code which specify the type of error
  */
static int fts_disableInterrupt(void *chip_data)
{
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

	if (info->client != NULL) {
		TPD_DEBUG("Number of disable = %d\n", info->disable_irq_count);

		if (info->disable_irq_count == 0) {
			TPD_DEBUG("Executing Disable...\n");
			disable_irq(info->client->irq);
			info->disable_irq_count++;
		}

		/* disable_irq is re-entrant so it is required to keep track
		 * of the number of calls of this when reenabling */
		TPD_DEBUG("Interrupt Disabled!\n");
		return OK;

	} else {
		TPD_INFO("%s: Impossible get client irq... ERROR %08X\n", __func__,
			 ERROR_OP_NOT_ALLOW);
		return ERROR_OP_NOT_ALLOW;
	}
}


/**
  * Disable the interrupt async so the ISR of the driver can not be called
  * @return OK if success or an error code which specify the type of error
  */
static int fts_disableInterruptNoSync(void *chip_data)
{
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

	if (info->client != NULL) {
		TPD_DEBUG("Number of disable = %d\n", info->disable_irq_count);

		if (info->disable_irq_count == 0) {
			TPD_DEBUG("Executing Disable...\n");
			disable_irq_nosync(info->client->irq);
			info->disable_irq_count++;
		}

		/* disable_irq is re-entrant so it is required to keep track
		 * of the number of calls of this when reenabling */
		TPD_DEBUG("Interrupt No Sync Disabled!\n");
		return OK;

	} else {
		TPD_INFO("%s: Impossible get client irq... ERROR %08X\n", __func__,
			 ERROR_OP_NOT_ALLOW);
		return ERROR_OP_NOT_ALLOW;
	}
}


/**
  * Reset the disable_irq count
  * @return OK
  */
static int fts_resetDisableIrqCount(void *chip_data)
{
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

	info->disable_irq_count = 0;
	return OK;
}

/**
  * Enable the interrupt so the ISR of the driver can be called
  * @return OK if success or an error code which specify the type of error
  */
static int fts_enableInterrupt(void *chip_data)
{
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

	if (info->client != NULL) {
		TPD_DEBUG("Number of re-enable = %d\n", info->disable_irq_count);

		while (info->disable_irq_count > 0) {
			/* loop N times according on the pending number of
			 * disable_irq to truly re-enable the int */
			TPD_DEBUG("Executing Enable...\n");
			enable_irq(info->client->irq);
			info->disable_irq_count--;
		}

		TPD_DEBUG("Interrupt Enabled!\n");
		return OK;

	} else {
		TPD_INFO("%s: Impossible get client irq... ERROR %08X\n", __func__,
			 ERROR_OP_NOT_ALLOW);
		return ERROR_OP_NOT_ALLOW;
	}
}

/**
  *    Check if there is a crc error in the IC which prevent the fw to run.
  *    @return  OK if no CRC error, or a number >OK according the CRC error
  * found
  */
static int fts_crc_check(void *chip_data)
{
	u8 val;
	u8 crc_status;
	int res;
	u8 error_to_search[6] = { EVT_TYPE_ERROR_CRC_CFG_HEAD,
				  EVT_TYPE_ERROR_CRC_CFG,
				  EVT_TYPE_ERROR_CRC_CX,
				  EVT_TYPE_ERROR_CRC_CX_HEAD,
				  EVT_TYPE_ERROR_CRC_CX_SUB,
				  EVT_TYPE_ERROR_CRC_CX_SUB_HEAD
				};

	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

	res = fts_writeReadU8UX(info, FTS_CMD_HW_REG_R, ADDR_SIZE_HW_REG, ADDR_CRC,
				&val, 1, DUMMY_HW_REG);/* read 2 bytes because the first one is a dummy byte! */

	if (res < OK) {
		TPD_INFO("%s: Cannot read crc status ERROR %08X\n", __func__, res);
		return res;
	}

	crc_status = val & CRC_MASK;

	if (crc_status != OK) {    /* CRC error if crc_status!=0 */
		TPD_INFO("%s:CRC ERROR = %02X\n", __func__, crc_status);
		return CRC_CODE;
	}

	TPD_INFO("%s: Verifying if Config CRC Error...\n", __func__);
	res = fts_system_reset(info);

	if (res >= OK) {
		res = pollForErrorType(info, error_to_search, 2);

		if (res < OK) {
			TPD_INFO("%s: No Config CRC Error Found!\n", __func__);
			TPD_INFO("%s: Verifying if Cx CRC Error...\n", __func__);
			res = pollForErrorType(info, &error_to_search[2], 4);

			if (res < OK) {
				TPD_INFO("%s: No Cx CRC Error Found!\n", __func__);
				return OK;

			} else {
				TPD_INFO("%s: Cx CRC Error found! CRC ERROR = %02X\n", __func__, res);
				return CRC_CX;
			}

		} else {
			TPD_INFO("%s: Config CRC Error found! CRC ERROR = %02X\n", __func__, res);
			return CRC_CONFIG;
		}

	} else {
		TPD_INFO("%s: Error while executing system reset! ERROR %08X\n", __func__, res);
		return res;
	}

	return OK;
}

/**
  * Request a host data and use the sync method to understand when the FW load
  * it
  * @param type the type ID of host data to load (@link load_opt Load Host Data
  * Option  @endlink)
  * @return OK if success or an error code which specify the type of error
  */
static int requestSyncFrame(void *chip_data, u8 type)
{
	u8 request[3] = { FTS_CMD_SYSTEM, SYS_CMD_LOAD_DATA, type };
	u8 readdata[DATA_HEADER] = { 0 };
	int ret, retry = 0, retry2 = 0, time_to_count;
	int count, new_count;
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

	TPD_DEBUG("%s: Starting to get a sync frame...\n", __func__);

	while (retry2 < RETRY_MAX_REQU_DATA) {
		TPD_DEBUG("%s: Reading count...\n", __func__);

		ret = fts_writeReadU8UX(info, FTS_CMD_FRAMEBUFFER_R, BITS_16, ADDR_FRAMEBUFFER,
					readdata, DATA_HEADER, DUMMY_FRAMEBUFFER);

		if (ret < OK) {
			TPD_DEBUG("%s: Error while reading count! ERROR %08X\n", __func__,
				  ret | ERROR_REQU_DATA);
			ret |= ERROR_REQU_DATA;
			retry2++;
			continue;
		}

		if (readdata[0] != HEADER_SIGNATURE)
			TPD_INFO("%s: Invalid Signature while reading count! ERROR %08X\n", __func__,
				 ret | ERROR_REQU_DATA);

		count = (readdata[3] << 8) | readdata[2];
		new_count = count;
		TPD_DEBUG("%s: Base count = %d\n", __func__, count);

		TPD_DEBUG("%s: Requesting frame %02X  attempt = %d\n", __func__,  type,
			  retry2 + 1);
		ret = fts_write(info, request, ARRAY_SIZE(request));

		if (ret >= OK) {
			TPD_DEBUG("%s: Polling for new count...\n", __func__);
			time_to_count = TIMEOUT_REQU_DATA / TIMEOUT_RESOLUTION;

			while (count == new_count && retry < time_to_count) {
				ret = fts_writeReadU8UX(info, FTS_CMD_FRAMEBUFFER_R, BITS_16, ADDR_FRAMEBUFFER,
							readdata, DATA_HEADER, DUMMY_FRAMEBUFFER);

				if ((ret >= OK) && (readdata[0] == HEADER_SIGNATURE) && (readdata[1] == type))
					new_count = ((readdata[3] << 8) | readdata[2]);

				else
					TPD_DEBUG("%s: invalid Signature or can not read count... ERROR %08X\n",
						  __func__, ret);

				retry++;
				msleep(TIMEOUT_RESOLUTION);
			}

			if (count == new_count) {
				TPD_INFO("%s: New count not received! ERROR %08X\n", __func__,
					 ERROR_TIMEOUT | ERROR_REQU_DATA);
				ret = ERROR_TIMEOUT | ERROR_REQU_DATA;

			} else {
				TPD_DEBUG("%s: New count found! count = %d! Frame ready!\n", __func__,
					  new_count);
				return OK;
			}
		}

		retry2++;
	}

	TPD_INFO("%s: Request Data failed! ERROR %08X\n", __func__,
		 ret);
	return ret;
}


/**
  * Save MP flag value into the flash
  * @param mpflag Value to write in the MP Flag field
  * @return OK if success or an error code which specify the type of error
  */
static int saveMpFlag(void *chip_data, u8 mpflag)
{
	int ret;
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

	TPD_INFO("%s: Saving MP Flag = %02X\n", __func__, mpflag);
	ret = writeSysCmd(info, SYS_CMD_MP_FLAG, &mpflag, 1);

	if (ret < OK) {
		TPD_INFO("%s: Error while writing MP flag on ram... ERROR %08X\n", __func__,
			 ret);
		return ret;
	}

	mpflag = SAVE_PANEL_CONF;
	ret = writeSysCmd(info, SYS_CMD_SAVE_FLASH, &mpflag, 1);

	if (ret < OK) {
		TPD_INFO("%s: Error while saving MP flag on flash... ERROR %08X\n", __func__,
			 ret);
		return ret;
	}

	ret = readSysInfo(info, 1);

	if (ret < OK) {
		TPD_INFO("%s: Error while refreshing SysInfo... ERROR %08X\n", __func__, ret);
		return ret;
	}

	/* TPD_INFO("%s: Saving MP Flag OK!\n", __func__); */
	return OK;
}

static int  readMpFlag(void *chip_data, u8 *mpflag)
{
	u8 writecmd[3] = { MP_FLAG_READ_CMD1, MP_FLAG_READ_CMD2,
			   MP_FLAG_READ_CMD3
			 };
	u8 writecmd2[3] = { MP_FLAG_READ_CMD4, MP_FLAG_READ_CMD5,
			    MP_FLAG_READ_CMD6
			  };

	u8 readdata;
	int ret = 0;
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

	TPD_INFO("%s: Reading MP Flag\n", __func__);

	ret = fts_write(info, writecmd, ARRAY_SIZE(writecmd));

	if (ret < OK) {
		TPD_INFO("%s: write failed...ERROR %08X !\n", __func__, ret);
		return ret;
	}

	ret = fts_writeRead(info, writecmd2, ARRAY_SIZE(writecmd2), &readdata, 1);

	if (ret < OK) {
		TPD_INFO("%s: write failed...ERROR %08X !\n", __func__, ret);
		return ret;
	}

	if (mpflag)
		* mpflag = readdata;

	return ret;
}

static int readCalibraFlag(void *chip_data, u8 *mpflag)
{
	u8 writecmd[3] = { MP_FLAG_READ_CMD1, MP_FLAG_READ_CMD2,
			   MP_FLAG_READ_CMD3
			 };
	u8 writecmd2[3] = { MP_FLAG_READ_CMD4, MP_FLAG_READ_CMD5,
			    MP_FLAG_READ_CMD7
			  };

	u8 readdata;
	int ret = 0;
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

	TPD_INFO("%s: Reading CALI Flag\n", __func__);

	ret = fts_write(info, writecmd, ARRAY_SIZE(writecmd));

	if (ret < OK) {
		TPD_INFO("%s: write failed...ERROR %08X !\n", __func__, ret);
		return ret;
	}

	ret = fts_writeRead(info, writecmd2, ARRAY_SIZE(writecmd2), &readdata, 1);

	if (ret < OK) {
		TPD_INFO("%s: write failed...ERROR %08X !\n", __func__, ret);
		return ret;
	}

	if (mpflag)
		* mpflag = readdata;

	return ret;
}

/**
  * Save Calibra flag value into the flash
  * @param Calibra Value to write in the Calibra Flag field
  * @return OK if success or an error code which specify the type of error
  */
static int __maybe_unused saveCalibraFlag(void *chip_data, u8 mpflag)
{
	u8 SaveCmd[3] = { MP_FLAG_SAVE_CMD1, MP_FLAG_SAVE_CMD2, MP_FLAG_SAVE_CMD3};
	u8 SetCmd[3] = { MP_FLAG_SET_CMD1, MP_FLAG_SET_CMD2, 0};

	int ret = 0;
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

	SetCmd[2] = mpflag;

	ret = fts_write(info, SetCmd, ARRAY_SIZE(SetCmd));

	if (ret < OK) {
		TPD_INFO("%s: write failed...ERROR %08X !\n", __func__, ret);
		return ret;
	}

	ret = fts_write(info, SaveCmd, ARRAY_SIZE(SaveCmd));

	if (ret < OK) {
		TPD_INFO("%s: write failed...ERROR %08X !\n", __func__, ret);
		return ret;
	}

	return ret;
}

/**
  * Perform the Initialization of the IC
  * @param type type of initialization to do
  * (see @link sys_special_opt Initialization Options (Full or Panel) @endlink)
  * @return OK if success or an error code which specify the type of error
  */
static int st80y_production_test_initialization(void *chip_data, u8 type)
{
	int res = 0;
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

	TPD_INFO("INITIALIZATION Production test is starting...\n");

	if (type != SPECIAL_PANEL_INIT && type != SPECIAL_FULL_PANEL_INIT) {
		TPD_INFO("production_test_initialization: Type incompatible! Type = %02X ERROR %08X\n",
			 type, ERROR_OP_NOT_ALLOW | ERROR_PROD_TEST_INITIALIZATION);
		return ERROR_OP_NOT_ALLOW | ERROR_PROD_TEST_INITIALIZATION;
	}

	res = fts_system_reset(info);

	if (res < 0) {
		TPD_INFO("production_test_initialization: ERROR %08X\n",
			 ERROR_PROD_TEST_INITIALIZATION);
		return res | ERROR_PROD_TEST_INITIALIZATION;
	}

	TPD_INFO("INITIALIZATION command sent... %02X\n", type);

	res = writeSysCmd(info, SYS_CMD_SPECIAL, &type, 1);

	if (res < OK) {
		TPD_INFO("production_test_initialization: ERROR %08X\n",
			 (res | ERROR_PROD_TEST_INITIALIZATION));
		return res | ERROR_PROD_TEST_INITIALIZATION;
	}

	TPD_INFO("Refresh Sys Info...\n");
	res |= readSysInfo(info, 1);    /* need to update the chipInfo in order
                	* to refresh several versions */

	if (res < 0) {
		TPD_INFO("production_test_initialization: read sys info ERROR %08X\n",
			 ERROR_PROD_TEST_INITIALIZATION);
		res = (res | ERROR_PROD_TEST_INITIALIZATION);
	}

	return res;
}

static int fts_chip_initialization(struct fts_ts_info *info, int init_type)
{
	int ret2 = 0;
	int retry;
	int initretrycnt = 0;

	/* initialization error, retry initialization */
	for (retry = 0; retry < RETRY_INIT_BOOT; retry++) {
		ret2 = st80y_production_test_initialization(info, init_type);

		if (ret2 == OK)
			break;

		initretrycnt++;
		TPD_INFO("%s: initialization cycle count = %04d - ERROR %08X\n", __func__,
			 initretrycnt, ret2);
		fts_chip_powercycle(info);
	}

	if (ret2 < OK) {   /* initialization error */
		TPD_INFO("%s: fts initialization failed %d times\n", __func__, RETRY_INIT_BOOT);
		return ret2;
	}

	ret2 = saveMpFlag(info, MP_FLAG_BOOT);

	if (ret2 < OK)
		TPD_INFO("%s:Error while saving MP FLAG! ERROR %08X\n", __func__, ret2);

	else
		TPD_INFO("%s:MP FLAG saving OK!\n", __func__);

	info->cal_needed = true;

	return ret2;
}

/**
  * This function try to attempt to communicate with the IC for the first time
  * during the boot up process in order to read the necessary info for the
  * following stages.
  * The function execute a system reset, read fundamental info (system info)
  * @return OK if success or an error code which specify the type of error
  */
static int fts_init(struct fts_ts_info *info)
{
	int error = 0;

	fts_resetDisableIrqCount(info);

	error = fts_system_reset(info);

	if (error < OK && isI2cError(error)) {
		TPD_INFO("%s: Cannot reset the device! ERROR %08X\n", __func__, error);
		return error;

	} else {
		if (error == (ERROR_TIMEOUT | ERROR_SYSTEM_RESET_FAIL)) {
			TPD_INFO("%s: Setting default Sys INFO!\n", __func__);
			error = defaultSysInfo(info, 0);

		} else {
			error = readSysInfo(info, 0);    /* system reset OK */

			if (error < OK) {
				if (!isI2cError(error))
					error = OK;

				TPD_INFO("%s: Cannot read Sys Info! ERROR %08X\n", __func__, error);
			}
		}
	}

	return error;
}

/* fts powercycle func */
static int fts_chip_powercycle(struct fts_ts_info *info)
{
	int error = 0;

	TPD_INFO("%s: Power Cycle Starting...\n", __func__);

	error = tp_powercontrol_vddi(info->hw_res, false);

	if (error < 0)
		TPD_INFO("%s: Failed to disable DVDD regulator\n", __func__);

	msleep(5);

	error = tp_powercontrol_avdd(info->hw_res, false);

	if (error < 0)
		TPD_INFO("%s: Failed to disable AVDD regulator\n", __func__);

	msleep(10);

	if (info->hw_res->reset_gpio != GPIO_NOT_DEFINED)
		gpio_direction_output(info->hw_res->reset_gpio, 0);

	else
		msleep(300);

	/* in FTI power up first the digital and then the analog */
	error = tp_powercontrol_vddi(info->hw_res, true);

	if (error < 0)
		TPD_INFO("%s: Failed to enable 1v8 regulator\n", __func__);

	msleep(5);

	error = tp_powercontrol_avdd(info->hw_res, true);

	if (error < 0)
		TPD_INFO("%s: Failed to enable 2v8 regulator\n", __func__);

	msleep(10);    /* time needed by the regulators for reaching the regime values */

	if (info->hw_res->reset_gpio != GPIO_NOT_DEFINED) {
		gpio_direction_output(info->hw_res->reset_gpio, 1);
		msleep(50);
	}

	TPD_INFO("%s: Power Cycle Finished! ERROR CODE = %08x\n", __func__, error);

	return error;
}

static int st80y_ftm_process(void *chip_data)
{
	TPD_INFO("%s: go into sleep\n", __func__);

	st80y_get_chip_info(chip_data);
	st80y_mode_switch(chip_data, MODE_SLEEP, true);
	return 0;
}

static int setReportRateMode(void *chip_data, u8 mode)
{
	u8 cmd[3] = { REPOTE_RATE_CMD1, REPOTE_RATE_CMD2, mode };
	int ret = 0;
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

	TPD_DEBUG("%s: Setting mode = %d !\n", __func__, mode);

	ret = fts_write(info, cmd, ARRAY_SIZE(cmd));

	if (ret < OK) {
		TPD_INFO("%s: write failed...ERROR %08X !\n", __func__, ret);
		return ret;
	}

	return ret;
}

static int setGameMode(void *chip_data, u8 mode)
{
	u8 cmd[3] = { REPOTE_RATE_CMD1, REPOTE_RATE_CMD2, mode };
	int ret = 0;
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;


	TPD_DEBUG("%s: Setting mode = %d !\n", __func__, mode);

	info->game_mode = mode;
	ret = fts_write(info, cmd, ARRAY_SIZE(cmd));

	if (ret < OK) {
		TPD_INFO("%s: write failed...ERROR %08X !\n", __func__, ret);
		return ret;
	}

	return ret;
}

static int st80y_set_high_frame_rate(void *chip_data, int value, int time)
{
	u8 cmd[3] = { REPOTE_RATE_CMD1, REPOTE_RATE_CMD3, (u8)(!!value) };
	int ret = 0;
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;


	TPD_DEBUG("%s: Setting high frame mode = %d !\n", __func__, value);

	ret = fts_write(info, cmd, ARRAY_SIZE(cmd));

	if (ret < OK)
		TPD_INFO("%s: write failed...ERROR %08X !\n", __func__, ret);

	return ret;
}

static int setFaceDetectMode(void *chip_data, u8 mode)
{
	u8 cmd_eare_on[4] = { 0xc0, 0x04, 0x01, 0x01};
	u8 cmd_eare_off[4] = {0xc0, 0x04, 0x00, 0x00};
	int ret = 0;
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

	TPD_DEBUG("%s: Setting mode = %d !\n", __func__, mode);

	if (mode)
		ret = fts_write(info, cmd_eare_on, ARRAY_SIZE(cmd_eare_on));

	else
		ret = fts_write(info, cmd_eare_off, ARRAY_SIZE(cmd_eare_off));

	if (ret < OK) {
		TPD_INFO("%s: write failed...ERROR %08X !\n", __func__, ret);
		return ret;
	}

	return ret;
}

/**
  * Enable in the FW the gesture mask to be used in gesture mode
  * @param mask pointer to a byte array which store the gesture mask update
  * that want to be sent to the FW, if NULL, will be used gesture_mask
  * set previously without any changes.
  * @param size dimension in byte of mask. This size can be <=
  * GESTURE_MASK_SIZE.
  * If size < GESTURE_MASK_SIZE the bytes of mask are considering continuos and
  * starting from the less significant byte.
  * @return OK if success or an error code which specify the type of error
  */
static int enableGesture(void *chip_data, u8 *mask, int size)
{
	int i, res;
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

	TPD_DEBUG("Trying to enable gesture...\n");

	if (size <= GESTURE_MASK_SIZE) {
		if (mask != NULL) {
			for (i = 0; i < size; i++)
				info->gesture_mask[i] = info->gesture_mask[i] | mask[i];
		}
		/* back up of the gesture enabled */

		res = setFeatures(chip_data, FEAT_SEL_GESTURE, info->gesture_mask,
				  GESTURE_MASK_SIZE);

		if (res < OK) {
			TPD_INFO("enableGesture: ERROR %08X\n", res);
			goto END;
		}

		TPD_DEBUG("enableGesture DONE!\n");
		res = OK;

	END:
		return res;

	} else {
		TPD_INFO("enableGesture: Size not valid! %d > %d ERROR %08X\n",
			 size, GESTURE_MASK_SIZE, ERROR_OP_NOT_ALLOW);
		return ERROR_OP_NOT_ALLOW;
	}
}

/**
  * Disable in the FW the gesture mask to be used in gesture mode
  * @param mask pointer to a byte array which store the gesture mask update that
  *  want to be sent to the FW, if NULL, all the gestures will be disabled.
  * @param size dimension in byte of mask. This size can be <=
  * GESTURE_MASK_SIZE.
  * If size < GESTURE_MASK_SIZE the bytes of mask are considering continuos and
  * starting from the less significant byte.
  * @return OK if success or an error code which specify the type of error
  */
static int disableGesture(void *chip_data, u8 *mask, int size)
{
	u8 temp;
	int i, res;
	u8 *pointer;
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

	TPD_DEBUG("Trying to disable gesture...\n");

	if (size <= GESTURE_MASK_SIZE) {
		if (mask != NULL) {
			for (i = 0; i < size; i++) {
				temp = info->gesture_mask[i] ^ mask[i];
				/* enabled mask XOR disabled mask */
				info->gesture_mask[i] = temp & info->gesture_mask[i];
				/* temp AND enabled
				  * disable the gestures that are specified and
				  *  previously enabled */
			}

			pointer = info->gesture_mask;

		} else {
			i = 0;    /* if NULL is passed disable all the possible gestures */
			pointer = (u8 *)&i;
		}

		res = setFeatures(chip_data, FEAT_SEL_GESTURE, pointer, GESTURE_MASK_SIZE);

		if (res < OK) {
			TPD_INFO("disableGesture: ERROR %08X\n", res);
			goto END;
		}

		TPD_DEBUG("disableGesture DONE!\n");

		res = OK;

	END:
		return res;

	} else {
		TPD_INFO("disableGesture: Size not valid! %d > %d ERROR %08X\n", size,
			 GESTURE_MASK_SIZE, ERROR_OP_NOT_ALLOW);
		return ERROR_OP_NOT_ALLOW;
	}
}


static int setGestureMode(void *chip_data, int mode)
{
	u8 gesture_mask[GESTURE_MASK_SIZE] = { 0xBF, 0x11, 0x07, 0x00 };
	int ret = 0;

	TPD_DEBUG("%s: Setting mode = %d !\n", __func__, mode);

	gesture_mask[0] = 0x20;/* enable duble tap bit5 */
	gesture_mask[1] = 0x00;
	gesture_mask[2] = 0x00;
	gesture_mask[3] = 0x00;

	if (mode) {
		ret = enableGesture(chip_data, gesture_mask, GESTURE_MASK_SIZE);

		if (ret < OK)
			TPD_INFO("%s: enableGesture failed! ERROR %08X\n", __func__, ret);

		ret = setScanMode(chip_data, SCAN_MODE_LOW_POWER, 0);

		if (ret < OK)
			TPD_INFO("enterGestureMode: enter gesture mode ERROR %08X\n", ret);

	} else {
		gesture_mask[0] = 0;
		gesture_mask[1] = 0;
		gesture_mask[2] = 0;
		gesture_mask[3] = 0x01;
		ret = disableGesture(chip_data, gesture_mask, GESTURE_MASK_SIZE);

		if (ret < OK)
			TPD_INFO("%s: disableGesture failed! ERROR %08X\n", __func__, ret);
	}

	return ret;
}

static int setScanFrequencyMode(void *chip_data, u8 mode)
{
	u8 cmd[3] = { SCAN_FREQUENCY_CMD1, SCAN_FREQUENCY_CMD2, 0 };
	int ret = 0;
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

	TPD_DEBUG("%s: Setting mode = %d !\n", __func__, mode);

	cmd[2] = mode;
	ret = fts_write(info, cmd, ARRAY_SIZE(cmd));

	if (ret < OK) {
		TPD_INFO("%s: write failed...ERROR %08X !\n", __func__, ret);
		return ret;
	}

	return ret;
}

static int setGripCornerMode(void *chip_data, int flag)
{
	u8 cmd[12] = { GRIP_REJECT_CMD1, GRIP_REJECT_CMD2, 0x00, 0x01, 0x8b, 0x05, 0x00, 0x00, 0x7f, 0x07, 0x78, 0x00};
	u8 cmd1[12] = { GRIP_REJECT_CMD1, GRIP_REJECT_CMD2, 0x00, 0x03, 0x8b, 0x05, 0x87, 0x06, 0x7f, 0x07, 0xff, 0x06};
	u8 cmd2[12] = { GRIP_REJECT_CMD1, GRIP_REJECT_CMD2, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf4, 0x01, 0x78, 0x00};
	u8 cmd3[12] = { GRIP_REJECT_CMD1, GRIP_REJECT_CMD2, 0x00, 0x02, 0x00, 0x00, 0x87, 0x06, 0xf4, 0x01, 0xff, 0x06};
	u8 cmd4[12] = { GRIP_REJECT_CMD1, GRIP_REJECT_CMD2, 0x00, 0x02, 0x00, 0x00, 0x0b, 0x05, 0x78, 0x00, 0xff, 0x06};
	u8 cmd5[12] = { GRIP_REJECT_CMD1, GRIP_REJECT_CMD2, 0x00, 0x03, 0x07, 0x07, 0x0b, 0x05, 0x7f, 0x07, 0xff, 0x06};

	int ret = 0;
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

	TPD_DEBUG("%s: Setting mode = %d !\n", __func__, flag);

	if (LANDSCAPE_SCREEN_90 == flag) {
		ret = fts_write(info, cmd2, ARRAY_SIZE(cmd2));

		if (ret < OK) {
			TPD_INFO("%s: write failed...ERROR %08X !\n", __func__, ret);
			return ret;
		}

		ret = fts_write(info, (cmd3), ARRAY_SIZE(cmd3));

		if (ret < OK) {
			TPD_INFO("%s: write failed1...ERROR %08X !\n", __func__, ret);
			return ret;
		}

	} else if (LANDSCAPE_SCREEN_270 == flag) {
		ret = fts_write(info, cmd, ARRAY_SIZE(cmd));

		if (ret < OK) {
			TPD_INFO("%s: write failed...ERROR %08X !\n", __func__, ret);
			return ret;
		}

		ret = fts_write(info, (cmd1), ARRAY_SIZE(cmd1));

		if (ret < OK) {
			TPD_INFO("%s: write failed1...ERROR %08X !\n", __func__, ret);
			return ret;
		}

	} else if (VERTICAL_SCREEN == flag) {
		ret = fts_write(info, cmd4, ARRAY_SIZE(cmd4));

		if (ret < OK) {
			TPD_INFO("%s: write failed...ERROR %08X !\n", __func__, ret);
			return ret;
		}

		ret = fts_write(info, (cmd5), ARRAY_SIZE(cmd5));

		if (ret < OK) {
			TPD_INFO("%s: write failed1...ERROR %08X !\n", __func__, ret);
			return ret;
		}
	}

	return ret;
}

static int setGripMode(void *chip_data, int flag)
{
	int ret = 0;
	u8 settings[4] = { 0 };

	TPD_DEBUG("%s: Setting mode = %d !\n", __func__, flag);

	if (LANDSCAPE_SCREEN_90 == flag)
		settings[0] = 1;

	else if (LANDSCAPE_SCREEN_270 == flag)
		settings[0] = 2;

	else if (VERTICAL_SCREEN == flag)
		settings[0] = 0;

	ret = setFeatures(chip_data, FEAT_SEL_GRIP, settings, 1);

	if (ret < OK) {
		TPD_INFO("%s: write failed...ERROR %08X !\n", __func__, ret);
		return ret;
	}

	setGripCornerMode(chip_data, flag);
	return ret;
}

/* fts mode switch func */
static int st80y_mode_switch(void *chip_data, work_mode mode, int flag)
{
	int ret = 0;
	u8 settings[4] = { 0 };

	switch (mode) {
	case MODE_NORMAL:
		TPD_INFO("%s: enter in normal mode !\n", __func__);
		settings[0] =
			0x01;    /* enable all the possible scans mode supported by the config */
		ret = setScanMode(chip_data, SCAN_MODE_ACTIVE, settings[0]);

		if (ret < OK)
			TPD_INFO("%s error during setting NORMAL_MODE! ERROR %08X\n", __func__, ret);

		break;

	case MODE_SLEEP:
		TPD_INFO("%s: enter in sleep mode !\n", __func__);
		ret = setScanMode(chip_data, SCAN_MODE_ACTIVE, 0x00);

		if (ret < OK)
			TPD_INFO("%s error during setting SLEEP_MODE! ERROR %08X\n", __func__, ret);

		break;

	case MODE_CHARGE:
		TPD_INFO("%s: %s to charge mode.\n", __func__, flag ? "enter" : "exit");

		if (flag)
			settings[0] = 1;

		ret = setFeatures(chip_data, FEAT_SEL_CHARGER, settings, 1);

		if (ret < OK)
			TPD_INFO("%s: error during setting CHARGER_MODE! ERROR %08X\n", __func__, ret);

		break;

	case MODE_GESTURE:
		TPD_INFO("%s: %s to gesture mode.\n", __func__, flag ? "enter" : "exit");

		if (flag)
			ret = setGestureMode(chip_data, 1);

		else
			ret = setGestureMode(chip_data, 0);

		break;

	case MODE_FACE_DETECT:
		TPD_INFO("%s: %s to face detect mode.\n", __func__, flag ? "enter" : "exit");

		if (flag)
			ret = setFaceDetectMode(chip_data, 1);

		else
			ret = setFaceDetectMode(chip_data, 0);

		break;

	case MODE_GAME:
		TPD_INFO("%s: %s to game mode.\n", __func__, flag ? "enter" : "exit");

		if (flag)
			ret = setGameMode(chip_data, 1);

		else
			ret = setGameMode(chip_data, 0);

		break;

	case MODE_EDGE:
		TPD_INFO("%s: grip mode :%d\n", __func__, flag);
		ret = setGripMode(chip_data, flag);
		break;

	default:
		TPD_INFO("%s: unsupport mode.\n", __func__);
	}

	return ret;
}

/* fts get gesture info func */
static int st80y_get_gesture_info(void *chip_data, struct gesture_info *gesture)
{
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;
	int count = 0, i = 0;
	unsigned char data[FIFO_EVENT_SIZE] = { 0 };

	for (count = 0; count < FIFO_DEPTH
			&& info->data[count][0] != EVT_ID_NOEVENT; count++) {
		for (i = 0; i < FIFO_EVENT_SIZE; i++) {
			data[i] = info->data[count][i];/* read data from the buf save in fts_trigger_reason fun */
		}

		if (data[0] == EVT_ID_USER_REPORT
				&& data[1] == EVT_TYPE_USER_GESTURE) {/* comfirm the trigger event is gesture */
			switch (data[2]) { /* classify the different gesture event */
			case GEST_ID_DBLTAP:
				gesture->gesture_type = DOU_TAP;
				TPD_DEBUG("%s: double tap !\n", __func__);
				break;

			case GEST_ID_O:
				gesture->gesture_type = CIRCLE_GESTURE;
				TPD_DEBUG("%s: O !\n", __func__);
				break;

			case GEST_ID_RIGHT_1F:
				gesture->gesture_type = LEFT2RIGHT_SWIP;
				TPD_DEBUG("%s:  -> !\n", __func__);
				break;

			case GEST_ID_LEFT_1F:
				gesture->gesture_type = RIGHT2LEFT_SWIP;
				TPD_DEBUG("%s:  <- !\n", __func__);
				break;

			case GEST_ID_UP_1F:
				gesture->gesture_type = DOWN2UP_SWIP;
				TPD_DEBUG("%s:  UP !\n", __func__);
				break;

			case GEST_ID_DOWN_1F:
				gesture->gesture_type = UP2DOWN_SWIP;
				TPD_DEBUG("%s:  DOWN !\n", __func__);
				break;

			case GEST_ID_CARET:
				gesture->gesture_type = DOWN_VEE;
				TPD_DEBUG("%s:  ^ !\n", __func__);
				break;

			case GEST_ID_LEFTBRACE:
				gesture->gesture_type = RIGHT_VEE;
				TPD_DEBUG("%s:  < !\n", __func__);
				break;

			case GEST_ID_RIGHTBRACE:
				gesture->gesture_type = LEFT_VEE;
				TPD_DEBUG("%s:  > !\n", __func__);
				break;

			case GEST_ID_M:
				gesture->gesture_type = M_GESTRUE;
				TPD_DEBUG("%s: M !\n", __func__);
				break;

			case GEST_ID_W:
				gesture->gesture_type = W_GESTURE;
				TPD_DEBUG("%s:  W !\n", __func__);
				break;

			case GEST_ID_V:
				gesture->gesture_type = UP_VEE;
				TPD_DEBUG("%s:  V !\n", __func__);
				break;

			/*
			case GEST_ID_S:
				gesture->gesture_type = Sgestrue;
				TPD_DEBUG("%s:  S !\n", __func__);
				break;
			*/
			case GEST_ID_SINGLE:
				gesture->gesture_type = SINGLE_TAP;
				TPD_DEBUG("%s:  single !\n", __func__);
				break;

			case GEST_ID_DOU_DOWN:
				gesture->gesture_type = DOU_SWIP;
				TPD_DEBUG("%s:  || !\n", __func__);
				break;

			default:
				gesture->gesture_type = UNKOWN_GESTURE;
				TPD_DEBUG("%s:  No valid GestureID!\n", __func__);
			}
		}
	}

	return 0;
}



/* fts get touch ponit func */
static int st80y_get_touch_points(void *chip_data, struct point_info *points,
				  int max_num)
{
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;
	int count = 0, i = 0;
	unsigned char data[FIFO_EVENT_SIZE] = { 0 };
	int obj_attention = 0;
	unsigned char eventid;
	int touchid;
	u8 touchType;


	for (count = 0; count < FIFO_DEPTH
			&& info->data[count][0] != EVT_ID_NOEVENT; count++) {
		if ((info->data[count][0] & 0xF0) != (EVT_ID_ENTER_POINT & 0xF0)
				&& (info->data[count][0] & 0xF0) != (EVT_ID_MOTION_POINT & 0xF0)
				&& (info->data[count][0] & 0xF0) != (EVT_ID_LEAVE_POINT & 0xF0))
			continue;

		/* read data from the buf save in fts_trigger_reason func */
		for (i = 0; i < FIFO_EVENT_SIZE; i++)
			data[i] = info->data[count][i];

		TPD_DEBUG("%s event get = %02X %02X %02X %02X %02X %02X %02X %02X\n", __func__,
			  data[0],
			  data[1], data[2], data[3], data[4], data[5], data[6], data[7]);
		eventid = data[0];
		touchid = (int)((data[1] & 0xF0) >> 4);
		touchType = data[1] & 0x0F;

		if (touchid >= max_num)   /* only suppurt max_num fingers */
			continue;

		if (((eventid & 0xF0) == (EVT_ID_ENTER_POINT & 0xF0))
				|| ((eventid & 0xF0) == (EVT_ID_MOTION_POINT & 0xF0))) { /* touch down event */
			obj_attention |= (0x0001 << touchid);/* setting touch down flag */
		}

		if (((eventid & 0xF0) == (EVT_ID_LEAVE_POINT & 0xF0))) { /* touch up event */
			obj_attention &= ~(0x01 << touchid);/* setting touch up flag */
		}

		/* points[touchid].x = (((int)data[3] & 0x0F) << 8) | (data[2]); */
		/* filling the point_info struct */
		/* points[touchid].y = ((int)data[4] << 4) | ((data[3] & 0xF0) >> 4); */
		points[touchid].x = (((int)data[3]) << 8) |
				    data[2];/* filling the point_info struct */
		points[touchid].y = ((int)data[5] << 8) | data[4];
		points[touchid].status = 1;

		switch (touchType) {
		case TOUCH_TYPE_FINGER:

		/* TPD_DEBUG(" %s : It is a finger!\n",__func__); */
		case TOUCH_TYPE_GLOVE:

		/* TPD_DEBUG(" %s : It is a glove!\n",__func__); */
		case TOUCH_TYPE_PALM:
			/* TPD_DEBUG(" %s : It is a palm!\n",__func__); */
			points[touchid].z = 0;
			break;

		case TOUCH_TYPE_HOVER:
			/* points[touchid].z = 0; */    /* no pressure */
			break;

		case TOUCH_TYPE_INVALID:
			points[touchid].status = 0;
			break;

		default:
			break;
		}

		points[touchid].touch_major = ((((int)data[0] & 0x0C) << 2) | (((
						       int)data[6] & 0xF0) >> 4));
		points[touchid].width_major = points[touchid].touch_major;
	}

	return obj_attention;
}

/**
  * Event handler for status events (EVT_ID_STATUS_UPDATE)
  * Handle status update events
  */
static void fts_status_event_handler(struct fts_ts_info *info,
				     unsigned char *event)
{
	switch (event[1]) {
	case EVT_TYPE_STATUS_ECHO:
		TPD_INFO("%s :Echo event of command = %02X %02X %02X %02X %02X %02X\n",
			 __func__, event[2], event[3], event[4], event[5], event[6], event[7]);
		break;

	case EVT_TYPE_STATUS_FORCE_CAL:
		switch (event[2]) {
		case 0x00:

			TPD_INFO("%s :Continuous frame drop Force cal = %02X %02X %02X %02X %02X %02X\n",
				 __func__, event[2], event[3], event[4], event[5], event[6], event[7]);
			break;

		case 0x01:
			TPD_INFO("%s :Mutual negative detect Force cal = %02X %02X %02X %02X %02X %02X\n",
				 __func__, event[2], event[3], event[4], event[5], event[6], event[7]);
			break;

		case 0x02:

			TPD_INFO("%s :Mutual calib deviation Force cal = %02X %02X %02X %02X %02X %02X\n",
				 __func__, event[2], event[3], event[4], event[5], event[6], event[7]);
			break;

		case 0x11:
			TPD_INFO("%s :SS negative detect Force cal = %02X %02X %02X %02X %02X %02X\n",
				 __func__, event[2], event[3], event[4], event[5], event[6], event[7]);
			break;

		case 0x12:

			TPD_INFO("%s :SS negative detect Force cal in Low Power mode = %02X %02X %02X %02X %02X %02X\n",
				 __func__, event[2], event[3], event[4], event[5], event[6], event[7]);
			break;

		case 0x13:

			TPD_INFO("%s :SS negative detect Force cal in Idle mode = %02X %02X %02X %02X %02X %02X\n",
				 __func__, event[2], event[3], event[4], event[5], event[6], event[7]);
			break;

		case 0x20:

			TPD_INFO("%s :SS invalid Mutual Strength soft Force cal = %02X %02X %02X %02X %02X %02X\n",
				 __func__, event[2], event[3], event[4], event[5], event[6], event[7]);
			break;

		case 0x21:

			TPD_INFO("%s :SS invalid Self Strength soft Force cal = %02X %02X %02X %02X %02X %02X\n",
				 __func__, event[2], event[3], event[4], event[5], event[6], event[7]);
			break;

		case 0x22:

			TPD_INFO("%s :SS invalid Self Island soft Force cal = %02X %02X %02X %02X %02X %02X\n",
				 __func__, event[2], event[3], event[4], event[5], event[6], event[7]);
			break;

		case 0x30:
			TPD_INFO("%s :MS invalid Mutual Strength soft Force cal = %02X %02X %02X %02X %02X %02X\n",
				 __func__, event[2], event[3], event[4], event[5], event[6], event[7]);
			break;

		case 0x31:
			TPD_INFO("%s :MS invalid Self Strength soft Force cal = %02X %02X %02X %02X %02X %02X\n",
				 __func__, event[2], event[3], event[4], event[5], event[6], event[7]);
			break;

		default:
			TPD_INFO("%s :Force cal = %02X %02X %02X %02X %02X %02X\n",
				 __func__, event[2], event[3], event[4], event[5], event[6], event[7]);
		}

		break;

	case EVT_TYPE_STATUS_FRAME_DROP:
		switch (event[2]) {
		case 0x01:
			TPD_INFO("%s :Frame drop noisy frame = %02X %02X %02X %02X %02X %02X\n",
				 __func__, event[2], event[3], event[4], event[5], event[6], event[7]);
			break;

		case 0x02:
			TPD_INFO("%s :Frame drop bad R = %02X %02X %02X %02X %02X %02X\n",
				 __func__, event[2], event[3], event[4], event[5], event[6], event[7]);
			break;

		case 0x03:
			TPD_INFO("%s :Frame drop invalid processing state = %02X %02X %02X %02X %02X %02X\n",
				 __func__, event[2], event[3], event[4], event[5], event[6], event[7]);
			break;

		default:
			TPD_INFO("%s :Frame drop = %02X %02X %02X %02X %02X %02X\n",
				 __func__, event[2], event[3], event[4], event[5], event[6], event[7]);
		}

		break;

	case EVT_TYPE_STATUS_SS_RAW_SAT:
		if (event[2] == 1)
			TPD_INFO("%s :SS Raw Saturated = %02X %02X %02X %02X %02X %02X\n",
				 __func__, event[2], event[3], event[4], event[5], event[6], event[7]);

		else
			TPD_INFO("%s :SS Raw No more Saturated = %02X %02X %02X %02X %02X %02X\n",
				 __func__, event[2], event[3], event[4], event[5], event[6], event[7]);

		break;

	case EVT_TYPE_STATUS_WATER:
		if (event[2] == 1)
			TPD_INFO("%s :Enter Water mode = %02X %02X %02X %02X %02X %02X\n",
				 __func__, event[2], event[3], event[4], event[5], event[6], event[7]);

		else
			TPD_INFO("%s :Exit Water mode = %02X %02X %02X %02X %02X %02X\n",
				 __func__, event[2], event[3], event[4], event[5], event[6], event[7]);

		break;

	default:
		TPD_INFO("%s :Received unhandled status event = %02X %02X %02X %02X %02X %02X %02X %02X\n",
			 __func__, event[0], event[1], event[2], event[3], event[4], event[5], event[6],
			 event[7]);
		break;
	}
}

/* fts event trigger reason func */
static u32 st80y_trigger_reason(void *chip_data, int gesture_enable,
				int is_suspended)
{
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;
	int error = 0, count = 0, i = 0;
	unsigned char regadd;
	unsigned char data[FIFO_EVENT_SIZE * FIFO_DEPTH] = { 0 };
	unsigned char eventid;
	u8 result = IRQ_IGNORE;
	const unsigned char events_remaining_pos = 7;
	const unsigned char events_remaining_mask = 0x1F;
	unsigned char events_remaining = 0;
	int offset = 0;

	/* read the FIFO and parsing events */
	regadd = FIFO_CMD_READONE;

	memset(info->data, 0, sizeof(info->data));

	/* Read the first FIFO event and the number of events remaining */
	error = fts_writeReadU8UX(info, regadd, 0, 0, data, FIFO_EVENT_SIZE,
				  DUMMY_FIFO);
	events_remaining = data[events_remaining_pos] & events_remaining_mask;
	events_remaining = (events_remaining > FIFO_DEPTH - 1) ? FIFO_DEPTH - 1 :
			   events_remaining;

	/* Drain the rest of the FIFO, up to 31 events */
	if (error == OK && events_remaining > 0)
		error = fts_writeReadU8UX(info, regadd, 0, 0, &data[FIFO_EVENT_SIZE],
					  FIFO_EVENT_SIZE * events_remaining, DUMMY_FIFO);

	if (error != OK)
		TPD_INFO("Error (%08X) while reading from FIFO in fts_event_handler\n", error);

	else {
		for (count = 0; count < events_remaining + 1; count++) {
			offset = count * FIFO_EVENT_SIZE;

			if (data[offset] != EVT_ID_NOEVENT) {
				eventid = data[offset];/* eventId is the main event trigger reason */

				for (i = 0; i < FIFO_EVENT_SIZE; i++) {
					info->data[count][i] = data[i + offset]; /* store the event data */
				}

				TPD_DEBUG("%s event get = %02X %02X %02X %02X %02X %02X %02X %02X\n", __func__,
					  data[0 + offset],
					  data[1 + offset], data[2 + offset], data[3 + offset], data[4 + offset],
					  data[5 + offset], data[6 + offset], data[7 + offset]);

			} else {
				TPD_INFO("%s:data is null,continue\n", __func__);
				continue;
			}

			TPD_DEBUG("eventid = %x,data[offset] = %x, gesture_enable = %d, is_suspended = %d\n",
				  eventid, data[offset], gesture_enable, is_suspended);

			if ((eventid & 0xF0) <= (EVT_ID_ERROR & 0xF0)) {
				if ((eventid & 0xF0) == (EVT_ID_ENTER_POINT & 0xF0)
						|| (eventid & 0xF0) == (EVT_ID_MOTION_POINT & 0xF0)
						|| (eventid & 0xF0) == (EVT_ID_LEAVE_POINT & 0xF0))  /* touch event */
					SET_BIT(result, IRQ_TOUCH);

				else if ((eventid & 0xF0) == (EVT_ID_USER_REPORT & 0xF0)) {
					if (data[offset + 1] == EVT_TYPE_USER_GESTURE && gesture_enable
							&& is_suspended)   /* gesture event */
						SET_BIT(result, IRQ_GESTURE);

				} else if ((eventid & 0xF0) == (EVT_ID_ERROR & 0xF0)) {
					if (data[offset + 1] == EVT_TYPE_ERROR_HARD_FAULT
							|| data[offset + 1] == EVT_TYPE_ERROR_WATCHDOG) { /* other unexcept event */
						dumpErrorInfo(info, NULL, 0);
						SET_BIT(result, IRQ_EXCEPTION);

					} else if (data[offset + 1] == EVT_TYPE_ERROR_ESD) {
						SET_BIT(result, IRQ_EXCEPTION);
						error = fts_esd_handle(info);

						if (error < 0)
							TPD_INFO("%s: esd handle fail !", __func__);
					}

				} else if ((eventid & 0xF0) == (EVT_ID_STATUS_UPDATE & 0xF0)) {
					fts_status_event_handler(info, &data[offset]);

					if (data[offset + 1] == 0x05) {
						SET_BIT(result, IRQ_FACE_STATE);

						if (data[offset + 2] == 0x42) {
							info->proximity_status = 1; /* near */

						} else if (data[offset + 2] == 0x44) {
							info->proximity_status = 0; /* far */
						}
					}

				} else if (eventid == EVT_ID_STATUS_UPDATE) {
					if (data[offset + 1] == 0x0d && data[offset + 2] == 0xff) {
						SET_BIT(result, IRQ_IGNORE);
						TPD_DEBUG("idle to active\n");
					}
				}
			}
		}
	}

	return result;
}

/* fts system reset func */
static int st80y_reset(void *chip_data)
{
	return fts_system_reset(chip_data);
}

/* fts resetgpio setting func */
static int fts_resetgpio_set(struct hw_resource *hw_res, bool on)
{
	if (gpio_is_valid(hw_res->reset_gpio)) {
		TPD_DEBUG("%s: Set the reset_gpio \n", __func__);
		gpio_direction_output(hw_res->reset_gpio, on);
	} else
		return -1;

	return 0;
}

/* fts power control func */
static int st80y_power_control(void *chip_data, bool enable)
{
	int ret = 0;
	struct fts_ts_info *chip_info = (struct fts_ts_info *)chip_data;

	if (true == enable) {
		ret = tp_powercontrol_vddi(chip_info->hw_res, true);

		if (ret)
			return -1;

		msleep(5);
		ret = tp_powercontrol_avdd(chip_info->hw_res, true);

		if (ret)
			return -1;

		msleep(15);
		fts_resetgpio_set(chip_info->hw_res, true);
		msleep(50);

	} else {
		ret = tp_powercontrol_vddi(chip_info->hw_res, false);

		if (ret)
			return -1;

		msleep(5);
		ret = tp_powercontrol_avdd(chip_info->hw_res, false);

		if (ret)
			return -1;

		msleep(10);
		fts_resetgpio_set(chip_info->hw_res, false);
	}

	return ret;
}

/* fts get chip relate info and init the chip func */
static int st80y_get_chip_info(void *chip_data)
{
	struct fts_ts_info *chip_info = (struct fts_ts_info *)chip_data;
	int error = 0;

	/* init the core lib */
	TPD_INFO("%s: Init Core Lib:\n", __func__);
	initCore(chip_info, chip_info->hw_res->reset_gpio);

	/* init the fts relate func */
	TPD_INFO("%s: Init fts fun:\n", __func__);
	error = fts_init(chip_info);

	if (error < OK) {
		TPD_INFO("%s: Cannot initialize the device ERROR %08X\n", __func__, error);
		return -1;
	}

	return 0;
}

/* fts get vendor relate info func */
static int st80y_get_vendor(void *chip_data, struct panel_info *panel_data)
{
	char manu_temp[MAX_DEVICE_MANU_LENGTH] = "ST_";
	struct fts_ts_info *chip_info = (struct fts_ts_info *)chip_data;

	/* get limit data path */
	chip_info->test_limit_name = panel_data->test_limit_name;

	/* get fw path */
	chip_info->fw_name = panel_data->fw_name;

	/* get panel manufacture */
	chip_info->tp_type = panel_data->tp_type;
	strlcat(manu_temp, panel_data->manufacture_info.manufacture,
		MAX_DEVICE_MANU_LENGTH);
	strncpy(panel_data->manufacture_info.manufacture, manu_temp,
		MAX_DEVICE_MANU_LENGTH);
	TPD_DEBUG("%s: chip_info->tp_type = %d, chip_info->test_limit_name = %s, chip_info->fw_name = %s\n",
		  __func__, chip_info->tp_type, chip_info->test_limit_name, chip_info->fw_name);
	return 0;
}

/**
  * Poll the Flash Status Registers after the execution of a command to check
  * if the Flash becomes ready within a timeout
  * @param type register to check according to the previous command sent
  * @return OK if success or an error code which specify the type of error
  */
static int wait_for_flash_ready(void *chip_data, u8 type)
{
	u8 cmd[5] = { FTS_CMD_HW_REG_R, 0x20, 0x00, 0x00, type };

	u8 readdata[2] = { 0 };
	int i, res = -1;
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

	TPD_DEBUG("Waiting for flash ready ...\n");

	for (i = 0; i < FLASH_RETRY_COUNT && res != 0; i++) {
		res = fts_writeRead(info, cmd, ARRAY_SIZE(cmd), readdata, 2);

		if (res < OK)
			TPD_INFO("wait_for_flash_ready: ERROR %08X\n", ERROR_BUS_W);

		else {
			res = readdata[0] & 0x80;
			TPD_DEBUG("flash status = %d\n", res);
		}

		msleep(FLASH_WAIT_BEFORE_RETRY);
	}

	if (i == FLASH_RETRY_COUNT && res != 0) {
		TPD_INFO("Wait for flash TIMEOUT! ERROR %08X\n", ERROR_TIMEOUT);
		return ERROR_TIMEOUT;
	}

	TPD_DEBUG("Flash READY!\n");
	return OK;
}

/**
  * Put the M3 in hold
  * @return OK if success or an error code which specify the type of error
  */
static int hold_m3(void *chip_data)
{
	int ret;
	u8 cmd[1] = { 0x01 };
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

	TPD_DEBUG("Command m3 hold...\n");
	ret = fts_writeU8UX(info, FTS_CMD_HW_REG_W, ADDR_SIZE_HW_REG, ADDR_SYSTEM_RESET,
			    cmd, 1);

	if (ret < OK) {
		TPD_INFO("hold_m3: ERROR %08X\n", ret);
		return ret;
	}

	TPD_DEBUG("Hold M3 DONE!\n");

	return OK;
}

/**
  * Parse the raw data read from a FW file in order to fill properly the fields
  * of a Firmware variable
  * @param fw_data raw FW data loaded from system
  * @param fw_size size of fw_data
  * @param fwData pointer to a Firmware variable which will contain the
  * processed data
  * @param keep_cx if 1, the CX area will be loaded and burnt otherwise will be
  * skipped and the area will be untouched
  * @return OK if success or an error code which specify the type of error
  */
static int parseBinFile(void *chip_data, u8 *fw_data, int fw_size,
			Firmware *fwData, int keep_cx)
{
	int dimension, index = 0;
	u32 temp;
	int res, i;

	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

	/* the file should contain at least the header plus the content_crc */
	if (fw_size < FW_HEADER_SIZE + FW_BYTES_ALLIGN || fw_data == NULL) {
		TPD_INFO("parseBinFile: Read only %d instead of %d... ERROR %08X\n", fw_size,
			 FW_HEADER_SIZE + FW_BYTES_ALLIGN, ERROR_FILE_PARSE);
		res = ERROR_FILE_PARSE;
		goto END;

	} else {
		/* start parsing of bytes */
		u8ToU32(&fw_data[index], &temp);

		if (temp != FW_HEADER_SIGNATURE) {
			TPD_INFO("parseBinFile: Wrong Signature %08X ... ERROR %08X\n", temp,
				 ERROR_FILE_PARSE);
			res = ERROR_FILE_PARSE;
			goto END;
		}

		TPD_DEBUG("parseBinFile: Fw Signature OK!\n");
		index += FW_BYTES_ALLIGN;
		u8ToU32(&fw_data[index], &temp);

		if (temp != FW_FTB_VER) {
			TPD_INFO("parseBinFile: Wrong ftb_version %08X ... ERROR %08X\n", temp,
				 ERROR_FILE_PARSE);
			res = ERROR_FILE_PARSE;
			goto END;
		}

		TPD_DEBUG("parseBinFile: ftb_version OK!\n");
		index += FW_BYTES_ALLIGN;

		if (fw_data[index] != DCHIP_ID_0 || fw_data[index + 1] != DCHIP_ID_1) {
			TPD_INFO("parseBinFile: Wrong target %02X != %02X  %02X != %02X ... ERROR %08X\n",
				 fw_data[index], DCHIP_ID_0, fw_data[index + 1], DCHIP_ID_1, ERROR_FILE_PARSE);
			res = ERROR_FILE_PARSE;
			goto END;
		}

		if ((fw_data[FW_ORG_INFO_OFFSET] == 0x00)
				&& (fw_data[FW_ORG_INFO_OFFSET + 1] == 0x00) &&
				(fw_data[FW_ORG_INFO_OFFSET + 2] == 0x00)
				&& (fw_data[FW_ORG_INFO_OFFSET + 3] == 0x00)) {
			fwData->flash_org_info[0] = FLASH_FW_CODE_COUNT;
			fwData->flash_org_info[1] = FLASH_PANEL_CFG_COUNT;
			fwData->flash_org_info[2] = FLASH_CX_CFG_COUNT;
			fwData->flash_org_info[3] = FLASH_FW_CFG_COUNT;

		} else {
			for (i = 0; i < 4; i++)
				fwData->flash_org_info[i] = fw_data[FW_ORG_INFO_OFFSET + i];
		}

		TPD_INFO("parseBinFile: Flash Organization Info fw_code:%02X,panel_cfg:%02X,cx_cfg:%02X,fw_cfg:%02X\n",
			 fwData->flash_org_info[0], fwData->flash_org_info[1], fwData->flash_org_info[2],
			 fwData->flash_org_info[3]);
		index += FW_BYTES_ALLIGN;
		u8ToU32(&fw_data[index], &temp);
		TPD_INFO("parseBinFile: FILE SVN REV = %08X\n", temp);

		index += FW_BYTES_ALLIGN;
		u8ToU32(&fw_data[index], &temp);
		fwData->fw_ver = temp;
		TPD_INFO("parseBinFile: FILE Fw Version = %04X\n", fwData->fw_ver);

		index += FW_BYTES_ALLIGN;
		u8ToU32(&fw_data[index], &temp);
		fwData->config_id = temp;
		TPD_INFO("parseBinFile: FILE Config Project ID = %08X\n", temp);

		index += FW_BYTES_ALLIGN;
		u8ToU32(&fw_data[index], &temp);
		TPD_INFO("parseBinFile: FILE Config Version = %08X\n", temp);

		index += FW_BYTES_ALLIGN * 2;    /* skip reserved data */

		index += FW_BYTES_ALLIGN;
		TPD_INFO("parseBinFile: File External Release =  ");

		for (i = 0; i < EXTERNAL_RELEASE_INFO_SIZE; i++) {
			fwData->externalRelease[i] = fw_data[index++];
			TPD_INFO("%02X ", fwData->externalRelease[i]);
		}

		TPD_INFO("\n");

		/* index+=FW_BYTES_ALLIGN; */
		u8ToU32(&fw_data[index], &temp);
		fwData->sec0_size = temp;
		TPD_INFO("parseBinFile:  sec0_size = %08X (%d bytes)\n", fwData->sec0_size,
			 fwData->sec0_size);

		index += FW_BYTES_ALLIGN;
		u8ToU32(&fw_data[index], &temp);
		fwData->sec1_size = temp;
		TPD_INFO("parseBinFile:  sec1_size = %08X (%d bytes)\n", fwData->sec1_size,
			 fwData->sec1_size);

		index += FW_BYTES_ALLIGN;
		u8ToU32(&fw_data[index], &temp);
		fwData->sec2_size = temp;
		TPD_INFO("parseBinFile:  sec2_size = %08X (%d bytes)\n", fwData->sec2_size,
			 fwData->sec2_size);

		index += FW_BYTES_ALLIGN;
		u8ToU32(&fw_data[index], &temp);
		fwData->sec3_size = temp;
		TPD_INFO("parseBinFile:  sec3_size = %08X (%d bytes)\n", fwData->sec3_size,
			 fwData->sec3_size);

		index += FW_BYTES_ALLIGN;/* skip header crc */

		dimension = fwData->sec0_size + fwData->sec1_size + fwData->sec2_size +
			    fwData->sec3_size;
		temp = fw_size;

		if (dimension + FW_HEADER_SIZE + FW_BYTES_ALLIGN != temp) {
			TPD_INFO("parseBinFile: Read only %d instead of %d... ERROR %08X\n", fw_size,
				 dimension + FW_HEADER_SIZE +
				 FW_BYTES_ALLIGN, ERROR_FILE_PARSE);
			res = ERROR_FILE_PARSE;
			goto END;
		}

		fwData->data = (u8 *)kzalloc(dimension * sizeof(u8), GFP_KERNEL);

		if (fwData->data == NULL) {
			TPD_INFO("parseBinFile: ERROR %08X\n", ERROR_ALLOC);
			res = ERROR_ALLOC;
			goto END;
		}

		index += FW_BYTES_ALLIGN;
		memcpy(fwData->data, &fw_data[index], dimension);

		if (fwData->sec2_size != 0)
			u8ToU16(&fwData->data[fwData->sec0_size + fwData->sec1_size + FW_CX_VERSION],
				&fwData->cx_ver);

		else {
			TPD_INFO("parseBinFile: Initialize cx_ver to default value!\n");
			fwData->cx_ver = info->systemInfo.u16_cxVer;
		}

		TPD_INFO("parseBinFile: CX Version = %04X\n", fwData->cx_ver);

		fwData->data_size = dimension;

		TPD_DEBUG("READ FW DONE %d bytes!\n", fwData->data_size);
		res = OK;
		goto END;
	}

END:
	kfree(fw_data);
	return res;
}

/**
  * Enable UVLO and Auto Power Down Mode
  * @return OK if success or an error code which specify the type of error
  */
static int flash_enable_uvlo_autopowerdown(void *chip_data)
{
	u8 cmd[6] = { FTS_CMD_HW_REG_W, 0x20, 0x00, 0x00, FLASH_UVLO_ENABLE_CODE0,
		      FLASH_UVLO_ENABLE_CODE1
		    };
	u8 cmd1[6] = { FTS_CMD_HW_REG_W, 0x20, 0x00, 0x00, FLASH_AUTOPOWERDOWN_ENABLE_CODE0,
		       FLASH_AUTOPOWERDOWN_ENABLE_CODE1
		     };
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

	TPD_INFO("%s Command enable uvlo ...\n", __func__);

	if (fts_write(info, cmd, ARRAY_SIZE(cmd)) < OK) {
		TPD_INFO("%s flash_enable_uvlo_autopowerdown: ERROR %08X\n", __func__,
			 ERROR_BUS_W);
		return ERROR_BUS_W;
	}

	if (fts_write(info, cmd1, ARRAY_SIZE(cmd1)) < OK) {
		TPD_INFO("%s flash_enable_uvlo_autopowerdown: ERROR %08X\n", __func__,
			 ERROR_BUS_W);
		return ERROR_BUS_W;
	}

	TPD_INFO("%s Enable uvlo and flash auto power down  DONE!\n", __func__);
	return OK;
}
/**
  * Unlock the flash to be programmed
  * @return OK if success or an error code which specify the type of error
  */
static int flash_unlock(void *chip_data)
{
	u8 cmd[6] = { FTS_CMD_HW_REG_W, 0x20, 0x00, 0x00, FLASH_UNLOCK_CODE0,
		      FLASH_UNLOCK_CODE1
		    };

	u8 cmd1[6] = { FTS_CMD_HW_REG_W, 0x20, 0x00, 0x00, FLASH_UNLOCK_CODE2,
		       FLASH_UNLOCK_CODE3
		     };
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

	TPD_DEBUG("Command unlock ...\n");

	if (fts_write(info, cmd, ARRAY_SIZE(cmd)) < OK) {
		TPD_INFO("flash_unlock: ERROR %08X\n", ERROR_BUS_W);
		return ERROR_BUS_W;
	}

	if (fts_write(info, cmd1, ARRAY_SIZE(cmd1)) < OK) {
		TPD_DEBUG("%s Command unlock: ERROR %08X\n", __func__, ERROR_BUS_W);
		return ERROR_BUS_W;
	}

	TPD_DEBUG("Unlock flash DONE!\n");

	return OK;
}

/**
  * Unlock the flash to be erased
  * @return OK if success or an error code which specify the type of error
  */
static int flash_erase_unlock(void *chip_data)
{
	u8 cmd[6] = { FTS_CMD_HW_REG_W, 0x20, 0x00, 0x00, FLASH_ERASE_UNLOCK_CODE0, FLASH_ERASE_UNLOCK_CODE1 };
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

	TPD_DEBUG("Try to erase unlock flash...\n");

	TPD_DEBUG("Command erase unlock ...\n");

	if (fts_write(info, cmd, ARRAY_SIZE(cmd)) < 0) {
		TPD_INFO("flash_erase_unlock: ERROR %08X\n", ERROR_BUS_W);
		return ERROR_BUS_W;
	}

	TPD_DEBUG("Erase Unlock flash DONE!\n");

	return OK;
}

/**
  * Erase the flash page by page, giving the possibility to skip the CX area and
  * maintain therefore its value
  * @param keep_cx if SKIP_PANEL_INIT the Panel Init pages will be skipped,
  * if > SKIP_PANEL_CX_INIT Cx and Panel Init pages otherwise all the pages will
  * be deleted
  * @return OK if success or an error code which specify the type of error
  */
static int flash_erase_page_by_page(void *chip_data, Firmware fw,
				    ErasePage keep_cx)
{
	u8 status, i = 0;
	u8 cmd1[6] = { FTS_CMD_HW_REG_W, 0x20, 0x00, 0x00, FLASH_ERASE_CODE0 + 1, 0x00 };
	u8 cmd[6] = { FTS_CMD_HW_REG_W, 0x20, 0x00, 0x00, FLASH_ERASE_CODE0, 0xA0 };
	u8 cmd2[9] = { FTS_CMD_HW_REG_W, 0x20, 0x00, 0x01, 0x28, 0xFF, 0xFF, 0xFF, 0xFF };
	u8 mask[4] = { 0 };
	int start = 0, end = 0;
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

	start = (int)(fw.flash_org_info[0] + fw.flash_org_info[1]);
	end = (int)(fw.flash_org_info[0] + fw.flash_org_info[1] + fw.flash_org_info[2]);

	TPD_INFO("%s Cx page start:%d end:%d\n", __func__, start, end - 1);

	for (i = start; i < end && keep_cx >= SKIP_PANEL_CX_INIT; i++) {
		TPD_DEBUG("Skipping erase CX page %d!\n", i);
		fromidtomask(i, mask, 4);
	}

	start = (int)(fw.flash_org_info[0]);
	end = (int)(fw.flash_org_info[0] + fw.flash_org_info[1]);
	TPD_INFO("%s Panel Configuration page start:%d end:%d\n", __func__, start,
		 end - 1);

	for (i = start; i < end && keep_cx >= SKIP_PANEL_INIT; i++) {
		TPD_DEBUG("Skipping erase Panel Init page %d!\n", i);
		fromidtomask(i, mask, 4);
	}

	TPD_DEBUG("Setting the page mask = ");

	for (i = 0; i < 4; i++) {
		cmd2[5 + i] = cmd2[5 + i] & (~mask[i]);
		TPD_DEBUG("%02X ", cmd2[5 + i]);
	}

	TPD_DEBUG("\nWriting page mask...\n");

	if (fts_write(info, cmd2, ARRAY_SIZE(cmd2)) < OK) {
		TPD_INFO("flash_erase_page_by_page: Page mask ERROR %08X\n", ERROR_BUS_W);
		return ERROR_BUS_W;
	}

	if (fts_write(info, cmd1, ARRAY_SIZE(cmd1)) < OK) {
		TPD_INFO("flash_erase_page_by_page: Disable info ERROR %08X\n", ERROR_BUS_W);
		return ERROR_BUS_W;
	}

	TPD_DEBUG("Command erase pages sent ...\n");

	if (fts_write(info, cmd, ARRAY_SIZE(cmd)) < OK) {
		TPD_INFO("flash_erase_page_by_page: Erase ERROR %08X\n", ERROR_BUS_W);
		return ERROR_BUS_W;
	}

	status = wait_for_flash_ready(info, FLASH_ERASE_CODE0);

	if (status != OK) {
		TPD_INFO("flash_erase_page_by_page: ERROR %08X\n", ERROR_FLASH_NOT_READY);
		return status | ERROR_FLASH_NOT_READY;
		/* Flash not ready within the chosen time, better exit! */
	}

	TPD_DEBUG("Erase flash page by page DONE!\n");

	return OK;
}


/**
  * Start the DMA procedure which actually transfer and burn the data loaded
  * from memory into the Flash
  * @return OK if success or an error code which specify the type of error
  */
static int start_flash_dma(void *chip_data)
{
	int status;
	u8 cmd[12] = { FLASH_CMD_WRITE_REGISTER, 0x20, 0x00, 0x00,
		       0x6B, 0x00, 0x40, 0x42, 0x0F, 0x00, 0x00,	FLASH_DMA_CODE1
		     };
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

	/* write the command to erase the flash */
	TPD_DEBUG("Command flash DMA ...\n");

	if (fts_write(info, cmd, ARRAY_SIZE(cmd)) < OK) {
		TPD_INFO("start_flash_dma: ERROR %08X\n", ERROR_BUS_W);
		return ERROR_BUS_W;
	}

	status = wait_for_flash_ready(info, FLASH_DMA_CODE0);

	if (status != OK) {
		TPD_INFO("start_flash_dma: ERROR %08X\n", ERROR_FLASH_NOT_READY);
		return status | ERROR_FLASH_NOT_READY;
		/* Flash not ready within the chosen time, better exit! */
	}

	TPD_DEBUG("flash DMA DONE!\n");

	return OK;
}

/**
  * Copy the FW data that should be burn in the Flash into the memory and then
  * the DMA will take care about burning it into the Flash
  * @param address address in memory where to copy the data, possible values
  * are FLASH_ADDR_CODE, FLASH_ADDR_CONFIG, FLASH_ADDR_CX
  * @param data pointer to an array of byte which contain the data that should
  * be copied into the memory
  * @param size size of data
  * @return OK if success or an error code which specify the type of error
  */
static int fillFlash(void *chip_data, u32 address, u8 *data, int size)
{
	int remaining = size;
	int index = 0;
	int towrite = 0;
	int byteblock = 0;
	int wheel = 0;
	u32 addr = 0;
	int res;
	int delta;
	u8 *buff = NULL;
	u8 buff2[12] = { 0 };
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

	buff = (u8 *)kzalloc((DMA_CHUNK + 5) * sizeof(u8), GFP_KERNEL);

	if (buff == NULL) {
		TPD_INFO("fillFlash: ERROR %08X\n", ERROR_ALLOC);
		return ERROR_ALLOC;
	}

	while (remaining > 0) {
		byteblock = 0;

		addr = 0x00100000;

		while (byteblock < FLASH_CHUNK && remaining > 0) {
			index = 0;

			if (remaining >= DMA_CHUNK) {
				if ((byteblock + DMA_CHUNK) <= FLASH_CHUNK) {
					towrite = DMA_CHUNK;
					remaining -= DMA_CHUNK;
					byteblock += DMA_CHUNK;

				} else {
					delta = FLASH_CHUNK - byteblock;
					towrite = delta;
					remaining -= delta;
					byteblock += delta;
				}

			} else {
				if ((byteblock + remaining) <= FLASH_CHUNK) {
					towrite = remaining;
					byteblock += remaining;
					remaining = 0;

				} else {
					delta = FLASH_CHUNK - byteblock;
					towrite = delta;
					remaining -= delta;
					byteblock += delta;
				}
			}


			buff[index++] = FTS_CMD_HW_REG_W;
			buff[index++] = (u8)((addr & 0xFF000000) >> 24);
			buff[index++] = (u8)((addr & 0x00FF0000) >> 16);
			buff[index++] = (u8)((addr & 0x0000FF00) >> 8);
			buff[index++] = (u8)(addr & 0x000000FF);

			memcpy(&buff[index], data, towrite);

			/* TPD_DEBUG("Command = %02X , address = %02X %02X
			 * , bytes = %d, data =  %02X %02X, %02X %02X\n",
			 * buff[0], buff[1], buff[2], toWrite, buff[3],
			 * buff[4], buff[3 + toWrite-2],
			 * buff[3 + toWrite-1]); */
			if (fts_write(info, buff, index + towrite) < OK) {
				TPD_INFO("fillFlash: ERROR %08X\n", ERROR_BUS_W);
				kfree(buff);
				return ERROR_BUS_W;
			}

			/* msleep(10); */
			addr += towrite;
			data += towrite;
		}

		/* configuring the DMA */
		byteblock = byteblock / 4 - 1;
		index = 0;

		buff2[index++] = FLASH_CMD_WRITE_REGISTER;
		buff2[index++] = 0x20;
		buff2[index++] = 0x00;
		buff2[index++] = 0x00;
		buff2[index++] = FLASH_DMA_CONFIG;
		buff2[index++] = 0x00;
		buff2[index++] = 0x00;

		addr = address + ((wheel * FLASH_CHUNK) / 4);
		buff2[index++] = (u8)((addr & 0x000000FF));
		buff2[index++] = (u8)((addr & 0x0000FF00) >> 8);
		buff2[index++] = (u8)(byteblock & 0x000000FF);
		buff2[index++] = (u8)((byteblock & 0x0000FF00) >> 8);
		buff2[index++] = 0x00;

		TPD_DEBUG("DMA Command = %02X , address = %02X %02X, words =  %02X %02X\n",
			  buff2[0], buff2[8], buff2[7], buff2[10], buff2[9]);

		if (fts_write(info, buff2, index) < OK) {
			TPD_INFO(" Error during filling Flash! ERROR %08X\n", ERROR_BUS_W);
			kfree(buff);
			return ERROR_BUS_W;
		}

		res = start_flash_dma(info);

		if (res < OK) {
			TPD_INFO("Error during flashing DMA! ERROR %08X\n", res);
			kfree(buff);
			return res;
		}

		wheel++;
	}

	kfree(buff);
	return OK;
}


/**
  * Execute the procedure to burn a FW in FTM4/FTI IC
  * @param fw structure which contain the FW to be burnt
  * @param force_burn if >0, the flashing procedure will be forced and executed
  * regardless the additional info, otherwise the FW in the file will be burnt
  * only if it is newer than the one running in the IC
  * @param keep_cx if 1, the function preserve the CX/Panel Init area otherwise
  * will be cleared
  * @return OK if success or an error code which specify the type of error
  */
static int flash_burn(void *chip_data, Firmware fw, int force_burn, int keep_cx)
{
	int res;
	u32 addr_cx = 0x00;
	u32 addr_config = 0x00;
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

	if (!force_burn) {
		for (res = EXTERNAL_RELEASE_INFO_SIZE - 1; res >= 0; res--)
			if (fw.externalRelease[res] != info->systemInfo.u8_releaseInfo[res])
				goto start;

		TPD_INFO("flash_burn: Firmware in the chip matches(or later) the firmware to flash! NO UPDATE ERROR %08X\n",
			 ERROR_FW_NO_UPDATE);
		return ERROR_FW_NO_UPDATE | ERROR_FLASH_BURN_FAILED;

	} else {
		/* burn procedure to update the CX memory, if not present just
		 * skip it if there isn't a new fw release. */
		if (force_burn == CRC_CX && fw.sec2_size == 0) {
			for (res = EXTERNAL_RELEASE_INFO_SIZE - 1; res >= 0; res--) {
				if (fw.externalRelease[res] > info->systemInfo.u8_releaseInfo[res]) {
					force_burn = 0;
					/* Avoid loading the CX because it is missing
					  * in the bin file, it just need to update
					  * to last fw+cfg because a new release */
					goto start;
				}
			}

			TPD_INFO("flash_burn: CRC in CX but fw does not contain CX data! NO UPDATE ERROR %08X\n",
				 ERROR_FW_NO_UPDATE);
			return ERROR_FW_NO_UPDATE | ERROR_FLASH_BURN_FAILED;
		}
	}

	/* programming procedure start */
start:
	TPD_DEBUG("Programming Procedure for flashing started:\n\n");
	addr_config = (u32)((fw.flash_org_info[0] + fw.flash_org_info[1] +
			     fw.flash_org_info[2]) * 0x400);
	addr_cx = (u32)((fw.flash_org_info[0] + fw.flash_org_info[1]) * 0x400);
	TPD_INFO("FTS The addr config:0X%X,CX:0X%X\n", addr_config, addr_cx);

	TPD_DEBUG("1) SYSTEM RESET:\n");
	res = fts_system_reset(info);

	if (res < 0) {
		TPD_INFO("system reset FAILED!\n");

		/* If there is no firmware, there is no controller ready event
		  * and there will be a timeout, we can keep going. But if
		  * there is an I2C error, we must exit.
		  */
		if (res != (ERROR_SYSTEM_RESET_FAIL | ERROR_TIMEOUT))
			return res | ERROR_FLASH_BURN_FAILED;
	} else
		TPD_DEBUG("system reset COMPLETED!\n\n");

	msleep(100); /* required by hw during flash procedure */

	TPD_DEBUG("2) HOLD M3 :\n");
	res = hold_m3(info);

	if (res < OK) {
		TPD_INFO("hold_m3 FAILED!\n");
		return res | ERROR_FLASH_BURN_FAILED;
	} else
		TPD_DEBUG("hold_m3 COMPLETED!\n\n");


	TPD_INFO("%s 3) ENABLE UVLO AND AUTO POWER DOWN MODE :\n", __func__);
	res = flash_enable_uvlo_autopowerdown(info);

	if (res < OK) {
		TPD_DEBUG("%s    flash_enable_uvlo_autopowerdown FAILED!\n", __func__);
		return res | ERROR_FLASH_BURN_FAILED;
	} else
		TPD_DEBUG("%s    flash_enable_uvlo_autopowerdown COMPLETED!\n\n", __func__);

	TPD_DEBUG("%s 4) FLASH UNLOCK:\n", __func__);
	res = flash_unlock(info);

	if (res < OK) {
		TPD_INFO("flash unlock FAILED! ERROR %08X\n", ERROR_FLASH_BURN_FAILED);
		return res | ERROR_FLASH_BURN_FAILED;
	} else
		TPD_DEBUG("flash unlock COMPLETED!\n\n");


	TPD_DEBUG("%s 5) FLASH ERASE UNLOCK:\n", __func__);
	res = flash_erase_unlock(info);

	if (res < 0) {
		TPD_INFO("  flash unlock FAILED! ERROR %08X\n", ERROR_FLASH_BURN_FAILED);
		return res | ERROR_FLASH_BURN_FAILED;
	} else
		TPD_DEBUG("flash unlock COMPLETED!\n\n");

	TPD_DEBUG("%s 6) FLASH ERASE:\n", __func__);

	if (keep_cx > 0) {
		if (fw.sec2_size != 0 && force_burn == CRC_CX)
			res = flash_erase_page_by_page(info, fw, SKIP_PANEL_INIT);

		else
			res = flash_erase_page_by_page(info, fw, SKIP_PANEL_CX_INIT);

	} else {
		/* res = flash_full_erase(); */
		res = flash_erase_page_by_page(info, fw, SKIP_PANEL_INIT);

		if (fw.sec2_size == 0)
			TPD_INFO("WARNING!!! Erasing CX memory but no CX in fw file! touch will not work right after fw update!\n");
	}

	if (res < OK) {
		TPD_INFO("flash erase FAILED! ERROR %08X\n", ERROR_FLASH_BURN_FAILED);
		return res | ERROR_FLASH_BURN_FAILED;
	} else
		TPD_DEBUG("flash erase COMPLETED!\n\n");

	TPD_DEBUG("%s 7) LOAD PROGRAM:\n", __func__);

	res = fillFlash(info, FLASH_ADDR_CODE, &fw.data[0], fw.sec0_size);

	if (res < OK) {
		TPD_INFO("load program ERROR %08X\n", ERROR_FLASH_BURN_FAILED);
		return res | ERROR_FLASH_BURN_FAILED;
	}

	TPD_INFO("load program DONE!\n");

	TPD_DEBUG("%s 8) LOAD CONFIG:\n", __func__);
	res = fillFlash(info, addr_config, &(fw.data[fw.sec0_size]), fw.sec1_size);

	if (res < OK) {
		TPD_INFO("load config ERROR %08X\n", ERROR_FLASH_BURN_FAILED);
		return res | ERROR_FLASH_BURN_FAILED;
	}

	TPD_INFO("load config DONE!\n");

	if (fw.sec2_size != 0 && (force_burn == CRC_CX || keep_cx <= 0)) {
		TPD_DEBUG("%s 8.1) LOAD CX:\n", __func__);
		res = fillFlash(info, addr_cx, &(fw.data[fw.sec0_size + fw.sec1_size]),
				fw.sec2_size);

		if (res < OK) {
			TPD_INFO("load cx ERROR %08X\n", ERROR_FLASH_BURN_FAILED);
			return res | ERROR_FLASH_BURN_FAILED;
		}

		TPD_INFO("load cx DONE!\n");
	}

	TPD_DEBUG("Flash burn COMPLETED!\n\n");

	TPD_DEBUG("%s 9) SYSTEM RESET:\n", __func__);
	res = fts_system_reset(info);

	if (res < 0) {
		TPD_INFO("system reset FAILED! ERROR %08X\n", ERROR_FLASH_BURN_FAILED);
		return res | ERROR_FLASH_BURN_FAILED;
	}

	TPD_DEBUG("system reset COMPLETED!\n\n");


	TPD_DEBUG("%s 10) FINAL CHECK:\n", __func__);
	res = readSysInfo(info, 0);

	if (res < 0) {
		TPD_INFO("flash_burn: Unable to retrieve Chip INFO! ERROR %08X\n",
			 ERROR_FLASH_BURN_FAILED);
		return res | ERROR_FLASH_BURN_FAILED;
	}

	for (res = 0; res < EXTERNAL_RELEASE_INFO_SIZE; res++) {
		if (fw.externalRelease[res] != info->systemInfo.u8_releaseInfo[res]) {
			/* External release is printed during readSysInfo */
			TPD_INFO("Firmware in the chip different from the one that was burn!\n");
			return ERROR_FLASH_BURN_FAILED;
		}
	}

	TPD_DEBUG("Final check OK!\n");

	return OK;
}

/* firmware update check func */
static fw_check_state st80y_fw_check(void *chip_data,
				     struct resolution_info *resolution_info, struct panel_info *panel_data)
{
	int ret;
	int crc_status = 0;
	uint32_t tp_fw;

	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

	TPD_INFO("%s: Fw Check is starting...\n", __func__);

	/* check CRC status */
	ret = fts_crc_check(chip_data);

	if (ret > OK) {
		TPD_INFO("%s: CRC Error or NO FW!\n", __func__);
		crc_status = ret;

	} else {
		crc_status = 0;
		TPD_INFO("%s: NO CRC Error or Impossible to read CRC register!\n", __func__);
	}

	if (crc_status != 0)
		return FW_ABNORMAL;

	/*fw check normal need update TP_FW  && device info*/
	tp_fw = (info->systemInfo.u8_releaseInfo[0]) |
		(info->systemInfo.u8_releaseInfo[1] << 8)
		| (info->systemInfo.u8_releaseInfo[2] << 16) |
		(info->systemInfo.u8_releaseInfo[3] << 24);
	panel_data->tp_fw = tp_fw;

	if (panel_data->manufacture_info.version)
		sprintf(panel_data->manufacture_info.version, "0x%x", panel_data->tp_fw);

	return FW_NORMAL;
}

/* fts flash procedure fun */
static int fts_flashProcedure(void *chip_data, const struct firmware *raw_fw,
			      bool force, int keep_cx)
{
	int res;
	int data_size;
	Firmware fw;
	u8 *data;
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

	/* init the fw struct */
	fw.data = NULL;
	data_size = raw_fw->size;
	data = (u8 *)kzalloc(data_size * sizeof(u8), GFP_KERNEL);
	memcpy(data, (u8 *)raw_fw->data, data_size);

	/* parse the fw data */
	res = parseBinFile(chip_data, data, data_size, &fw, keep_cx);

	if (res < OK) {
		TPD_INFO("%s: readFwFile: impossible parse ERROR %08X\n", __func__,
			 ERROR_MEMH_READ);
		res |= ERROR_MEMH_READ;
		goto updateFw_fail;
	}

	TPD_DEBUG("%s: Fw file parse COMPLETED!\n", __func__);

	/* burn the fw data */
	TPD_DEBUG("%s: Starting flashing procedure...\n", __func__);
	res = flash_burn(info, fw, force, keep_cx);

	if (res < OK && res != (ERROR_FW_NO_UPDATE | ERROR_FLASH_BURN_FAILED)) {
		TPD_INFO("%s: flashProcedure: ERROR %08X\n", __func__, ERROR_FLASH_PROCEDURE);
		res |= ERROR_FLASH_PROCEDURE;
		goto updateFw_fail;
	}

	TPD_DEBUG("%s: flashing procedure Finished!\n", __func__);

updateFw_fail:

	if (fw.data != NULL)
		kfree(fw.data);

	return res;
}

/* fts firmware update func */
static fw_update_state st80y_fw_update(void *chip_data,
				       const struct firmware *raw_fw, bool force)
{
	u8 error_to_search[4] = { EVT_TYPE_ERROR_CRC_CX_HEAD,
				  EVT_TYPE_ERROR_CRC_CX,
				  EVT_TYPE_ERROR_CRC_CX_SUB_HEAD,
				  EVT_TYPE_ERROR_CRC_CX_SUB
				};
	int retval = 0;
	int retval1 = 0;
	int ret;
	int error = 0;
	int init_type = NO_INIT;
	int crc_status = 0;

	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

#if defined(PRE_SAVED_METHOD) || defined (COMPUTE_INIT_METHOD)
	int keep_cx = 1;
#else
	int keep_cx = 0;
#endif

	/* Procedure the fw using retry method */
	TPD_INFO("%s Fw Auto Update is starting...\n", __func__);

	/* check CRC status */
	ret = fts_crc_check(info);

	if (ret > OK) {
		TPD_INFO("%s: CRC Error or NO FW!\n", __func__);
		crc_status = ret;

	} else {
		crc_status = 0;
		TPD_INFO("%s: NO CRC Error or Impossible to read CRC register!\n", __func__);

		if (force == 1)
			crc_status = 1;
	}

	/* Procedure the fw using retry method */
	retval = fts_flashProcedure(info, raw_fw, crc_status, keep_cx);

	if ((retval & 0xFF000000) == ERROR_FLASH_PROCEDURE) {
		TPD_INFO("%s: firmware update failed and retry! ERROR %08X\n", __func__,
			 retval);
		fts_chip_powercycle(info);    /* power reset */
		retval1 = fts_flashProcedure(info, raw_fw, crc_status, keep_cx);

		if ((retval1 & 0xFF000000) == ERROR_FLASH_PROCEDURE) {
			TPD_INFO("%s: firmware update failed again!  ERROR %08X\n", __func__, retval1);
			TPD_INFO("%s: Fw Auto Update Failed!\n", __func__);
			/* return FW_UPDATE_ERROR; */
		}

	} else if (retval == (ERROR_FW_NO_UPDATE | ERROR_FLASH_BURN_FAILED)) {
		/* return FW_NO_NEED_UPDATE; */
	}

	TPD_INFO("%s: Verifying if CX CRC Error...\n", __func__);
	ret = fts_system_reset(info);

	if (ret >= OK) {
		ret = pollForErrorType(info, error_to_search, 4);

		if (ret < OK) {
			TPD_INFO("%s: No Cx CRC Error Found!\n", __func__);
			TPD_INFO("%s: Verifying if Panel CRC Error...\n", __func__);
			error_to_search[0] = EVT_TYPE_ERROR_CRC_PANEL_HEAD;
			error_to_search[1] = EVT_TYPE_ERROR_CRC_PANEL;
			ret = pollForErrorType(info, error_to_search, 2);

			if (ret < OK) {
				TPD_INFO("%s: No Panel CRC Error Found!\n", __func__);
				init_type = NO_INIT;

			} else {
				TPD_INFO("%s: Panel CRC Error FOUND! CRC ERROR = %02X\n", __func__, ret);
				init_type = SPECIAL_PANEL_INIT;
			}

		} else {
			TPD_INFO("%s: Cx CRC Error FOUND! CRC ERROR = %02X\n", __func__, ret);
			/* this path of the code is used only in case there is a
			 * CRC error in code or config which not allow the fw to
			 * compute the CRC in the CX before */
			/* the only way to recover is to have CX in fw file...
			 * */
#ifndef COMPUTE_INIT_METHOD
			TPD_INFO("%s: Try to recovery with CX in fw file...\n", __func__);
			fts_flashProcedure(info, raw_fw, CRC_CX, 0);
			TPD_INFO("%s: Refresh panel init data...\n", __func__);
#else
			TPD_INFO("%s: Select Full Panel Init...\n", __func__);
			init_type = SPECIAL_FULL_PANEL_INIT;
#endif
		}

	} else {
		TPD_INFO("%s: Error while executing system reset! ERROR %08X\n", __func__,
			 ret);        /* better skip initialization because the real state is unknown */
	}

	if (init_type == NO_INIT) {
#if defined(PRE_SAVED_METHOD) || defined(COMPUTE_INIT_METHOD)
#ifdef COMPUTE_INIT_METHOD
		if ((info->systemInfo.u8_cfgAfeVer != info->systemInfo.u8_cxAfeVer)
				|| ((info->systemInfo.u8_mpFlag != MP_FLAG_BOOT)
				    && (info->systemInfo.u8_mpFlag != MP_FLAG_FACTORY))) {
			init_type = SPECIAL_FULL_PANEL_INIT;
			TPD_INFO("%s: Different CX AFE Ver: %02X != %02X or invalid MpFlag = %02X... Execute FULL Panel Init!\n",
				 __func__, info->systemInfo.u8_cfgAfeVer,
				 info->systemInfo.u8_cxAfeVer, info->systemInfo.u8_mpFlag);
		} else
#else
		if ((info->systemInfo.u8_cfgAfeVer != info->systemInfo.u8_cxAfeVer)) {
			init_type = SPECIAL_FULL_PANEL_INIT;
			TPD_INFO("%s: Different CX AFE Ver: %02X != %02X or invalid MpFlag = %02X... Execute FULL Panel Init!\n",
				 __func__, info->systemInfo.u8_cfgAfeVer,
				 info->systemInfo.u8_cxAfeVer, info->systemInfo.u8_mpFlag);
		} else
#endif
#endif
			if (info->systemInfo.u8_cfgAfeVer != info->systemInfo.u8_panelCfgAfeVer) {
				init_type = SPECIAL_PANEL_INIT;
				TPD_INFO("%s: Different Panel AFE Ver: %02X != %02X... Execute Panel Init!\n",
					 __func__, info->systemInfo.u8_cfgAfeVer,
					 info->systemInfo.u8_panelCfgAfeVer);
			} else {
				init_type = NO_INIT;
			}
	}

	if (init_type !=
			NO_INIT) {    /* initialization status not correct or after FW complete update, do initialization. */
		error = fts_chip_initialization(info, init_type);

		if (error < OK)
			TPD_INFO("%s: Cannot initialize the chip ERROR %08X\n", __func__, error);
	}

	if (retval == (ERROR_FW_NO_UPDATE | ERROR_FLASH_BURN_FAILED)) {
		TPD_INFO("%s: no need fW update  ERROR %08X\n", __func__, retval1);
		return FW_NO_NEED_UPDATE;
	}

	if (((retval1 & 0xFF000000) == ERROR_FLASH_PROCEDURE) || (error < OK)) {
		TPD_INFO("%s: Fw Auto Update Failed!\n", __func__);
		return FW_UPDATE_ERROR;
	}

	return FW_UPDATE_SUCCESS;
}



/* fts esd handle func */
static int fts_esd_handle(void *chip_data)
{
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;
	int error = 0;

	TPD_INFO("%s: enter!\n", __func__);

	disable_irq_nosync(info->client->irq);

	fts_chip_powercycle(info);

	error = fts_system_reset(info);

	if (error < OK) {
		TPD_INFO("%s: Cannot restore the device ERROR %08X\n", __func__, error);
		return error;
	}

	enable_irq(info->client->irq);

	return 0;
}

static int st80y_get_face_detect(void *chip_data)
{
	int state = -1;
	struct fts_ts_info *chip_info = (struct fts_ts_info *)chip_data;

	if (chip_info->proximity_status == 0) {
		state = 0;		/* far */

	} else if (chip_info->proximity_status == 1) {
		state = 1;		/* near */
	}

	return state;
}

static void st80y_get_health_info(void *chip_data,
				  struct monitor_data *mon_data)
{
}

static void st80y_set_touch_direction(void *chip_data, uint8_t dir)
{
}

static uint8_t st80y_get_touch_direction(void *chip_data)
{
	return 0;
}


static int setFirstTapIirMode(void *chip_data)
{
	u8 writecmd[8] = { FIRST_TAP_LOCK_CMD1, FIRST_TAP_LOCK_CMD2,
			   FIRST_TAP_LOCK_ENABLE, 0x00, 0x00, ST_MAX_IIR, ST_TOL_X, ST_TOL_Y
			 };
	u8 ReadCmd[2] = { FIRST_TAP_READ_CMD1, FIRST_TAP_READ_CMD2};
	u8 readdata[6] = {0, 0, 0, 0, 0, 0};

	int ret = 0;
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

	if (info->is_usb_checked) {
		if (*info->is_usb_checked) {
			writecmd[6] = ST_CHARGE_TOL_X;
			writecmd[7] = ST_CHARGE_TOL_Y;
		}
	}

	writecmd[3] = info->sensitive_level;
	writecmd[4] = info->smooth_level;

	if (0 == info->smooth_level || 0 == info->sensitive_level) {
		writecmd[3] = 0x0f;
		writecmd[4] = 0x06;
		writecmd[5] = 0x0e;
		writecmd[6] = 0x04;
		writecmd[7] = 0x04;
		writecmd[2] =
			0x00;/*if smooth_level = 0 or sensitive_level =0, cfg for default*/
	}

	ret = fts_write(info, writecmd, ARRAY_SIZE(writecmd));

	if (ret < OK) {
		TPD_INFO("%s: write failed...ERROR %08X !\n", __func__, ret);
		return ret;
	}

	ret = fts_writeRead(info, ReadCmd, ARRAY_SIZE(ReadCmd), readdata,
			    ARRAY_SIZE(readdata));

	if (ret < OK) {
		TPD_INFO("%s: write failed...ERROR %08X !\n", __func__, ret);
		return ret;
	}

	TPD_INFO("FirstTapIir read data :%*ph\n", sizeof(readdata), readdata);
	TPD_INFO("OK sensitive value: %d\n", info->sensitive_level);
	TPD_INFO("OK smooth value: %d\n", info->smooth_level);

	return ret;
}

static int st80y_smooth_lv_set(void *chip_data, int level)
{
	int ret = 0;
	struct fts_ts_info *chip_info = (struct fts_ts_info *)chip_data;

	TPD_DEBUG("%s: value = %d !\n", __func__, level);

	chip_info->smooth_level = level;

	ret = setFirstTapIirMode(chip_data);
	return ret;
}

static int st80y_sensitive_lv_set(void *chip_data, int level)
{
	int ret = 0;
	struct fts_ts_info *chip_info = (struct fts_ts_info *)chip_data;

	TPD_DEBUG("%s: value = %d !\n", __func__, level);

	chip_info->sensitive_level = level;

	ret = setFirstTapIirMode(chip_data);
	return ret;
}

static int st80y_report_refresh_switch(void *chip_data, int fps)
{
	int retval = 0;
	unsigned short send_value = 1;
	struct fts_ts_info *chip_info = (struct fts_ts_info *)chip_data;

	TPD_DEBUG("%s: refresh_switch: %d HZ!\n", __func__, fps);

	if (chip_info == NULL)
		return -1;

	chip_info->display_refresh_rate = fps;

	if (!*chip_info->in_suspend && !chip_info->game_mode) {
		switch (fps) {
		case 60:
			send_value = 0;
			break;

		case 90:
			send_value = 1;
			break;

		case 120:
			send_value = 1;
			break;

		default:
			return 0;
		}

		setReportRateMode(chip_info, send_value);
	}

	return retval;
}

static void st80y_calibrate(struct seq_file *s, void *chip_data)
{
	int ret = -1;
	u8 saveInit = RETRY_INIT_BOOT;
	u8 u8_mpFlag = 0;

	ret = st80y_production_test_initialization(chip_data, (u8)saveInit);

	if (ret < 0) {
		TPD_INFO("%s calibration failed\n", __func__);
		seq_printf(s, "1 error, calibration failed\n");
		return;
	} else
		TPD_INFO("%s calibration successed\n", __func__);

	/*
		ret = st80ySetCalibraFlag(chip_data);
		if (ret < 0) {
			TPD_INFO("%s calibration failed\n", __func__);
			seq_printf(s, "1 error, calibration failed\n");
		} else {
			TPD_INFO("%s calibration successed\n", __func__);
			seq_printf(s, "0 error, calibration successed\n");
		}
	*/
	ret = readMpFlag(chip_data, &u8_mpFlag);
	TPD_INFO("%s:NEW MP FLAG = %02X\n", __func__, u8_mpFlag);

	ret = saveMpFlag(chip_data, MP_FLAG_FACTORY);

	if (ret < OK) {
		TPD_INFO("%s:Error while saving MP FLAG! ERROR %08X\n", __func__, ret);
		seq_printf(s, "1 error, calibration failed\n");

	} else {
		TPD_INFO("%s:MP FLAG saving OK!\n", __func__);
		seq_printf(s, "0 error, calibration successed\n");
	}

	return;
}

static bool st80y_get_cal_status(struct seq_file *s, void *chip_data)
{
	struct fts_ts_info *chip_info = (struct fts_ts_info *)chip_data;

	return chip_info->cal_needed;
}

static void st80y_freq_hop_trigger(void *chip_data)
{
	struct fts_ts_info *chip_info = (struct fts_ts_info *)chip_data;

	TPD_INFO("%s : send cmd to tigger frequency hopping here!!!\n", __func__);

	switch (chip_info->freq_point) {
	case 0:
		TPD_INFO("%s : Hop to frequency : %d\n", __func__, chip_info->freq_point);
		setScanFrequencyMode(chip_info, 0);
		chip_info->freq_point = 2;
		break;

	case 2:
		TPD_INFO("%s : Hop to frequency : %d\n", __func__, chip_info->freq_point);
		setScanFrequencyMode(chip_info, 2);
		chip_info->freq_point = 10;
		break;

	case 10:
		TPD_INFO("%s : Hop to frequency : %d\n", __func__, chip_info->freq_point);
		setScanFrequencyMode(chip_info, 10);
		chip_info->freq_point = 11;
		break;

	case 11:
		TPD_INFO("%s : Hop to frequency : %d\n", __func__, chip_info->freq_point);
		setScanFrequencyMode(chip_info, 11);
		chip_info->freq_point = 4;
		break;

	case 4:
		TPD_INFO("%s : Hop to frequency : %d\n", __func__, chip_info->freq_point);
		setScanFrequencyMode(chip_info, 4);
		chip_info->freq_point = 5;
		break;

	case 5:
		TPD_INFO("%s : Hop to frequency : %d\n", __func__, chip_info->freq_point);
		setScanFrequencyMode(chip_info, 5);
		chip_info->freq_point = 6;
		break;

	case 6:
		TPD_INFO("%s : Hop to frequency : %d\n", __func__, chip_info->freq_point);
		setScanFrequencyMode(chip_info, 6);
		chip_info->freq_point = 7;
		break;

	case 7:
		TPD_INFO("%s : Hop to frequency : %d\n", __func__, chip_info->freq_point);
		setScanFrequencyMode(chip_info, 7);
		chip_info->freq_point = 0;
		break;

	default:
		break;
	}
}

#ifdef CONFIG_OPLUS_TP_APK
static void fts_apk_water_set(void *chip_data, int type)
{
	u8 cmd[3] = { WATER_MODE_CMD1, WATER_MODE_CMD2, 0 };
	int ret = 0;
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

	if (type > 0)
		cmd[2] = 1;

	else
		cmd[2] = 0;

	ret = fts_write(info, cmd, ARRAY_SIZE(cmd));

	if (ret < OK)
		TPD_INFO("%s: write failed...ERROR %08X !\n", __func__, ret);
}

static int fts_apk_water_get(void *chip_data)
{
	TPD_INFO("%s: not support.\n");
	return 0;
}

static void fts_init_oplus_apk_op(struct touchpanel_data *ts)
{
	ts->apk_op = tp_devm_kzalloc(&ts->client->dev, sizeof(APK_OPERATION),
				     GFP_KERNEL);

	if (ts->apk_op) {
		ts->apk_op->apk_water_set = fts_apk_water_set;
		ts->apk_op->apk_water_get = fts_apk_water_get;
	} else
		TPD_INFO("Can not devm_kzalloc apk op.\n");
}
#endif /* end of CONFIG_OPLUS_TP_APK*/


/* fts callback func file ops */
static struct oplus_touchpanel_operations st80y_ops = {
	.ftm_process               = st80y_ftm_process,
	.get_vendor                = st80y_get_vendor,
	.get_chip_info             = st80y_get_chip_info,
	.reset                     = st80y_reset,
	.power_control             = st80y_power_control,
	.fw_check                  = st80y_fw_check,
	.fw_update                 = st80y_fw_update,
	.trigger_reason            = st80y_trigger_reason,
	.get_touch_points          = st80y_get_touch_points,
	.get_gesture_info          = st80y_get_gesture_info,
	.mode_switch               = st80y_mode_switch,
	.get_face_state	  		   = st80y_get_face_detect,
	.health_report             = st80y_get_health_info,
	.set_touch_direction       = st80y_set_touch_direction,
	.get_touch_direction       = st80y_get_touch_direction,
	.smooth_lv_set             = st80y_smooth_lv_set,
	.sensitive_lv_set          = st80y_sensitive_lv_set,
	.tp_refresh_switch		   = st80y_report_refresh_switch,
	.calibrate                 = st80y_calibrate,
	.get_cal_status            = st80y_get_cal_status,
	.freq_hop_trigger		   = st80y_freq_hop_trigger,
	.set_high_frame_rate       = st80y_set_high_frame_rate,
};

/* st80y delta data read func */
static void st80y_delta_read(struct seq_file *s, void *chip_data)
{
	int x = 0, y = 0, z = 0;
	int res = 0;
	int16_t temp_delta = 0;
	struct fts_ts_info *chip_info = (struct fts_ts_info *)chip_data;
	MutualSenseFrame framems;

	if (!chip_info)
		return;

	/* lock the mode to avoid IC enter idle mode */
	setScanMode(chip_data, SCAN_MODE_LOCKED, LOCKED_ACTIVE);
	msleep(WAIT_FOR_FRESH_FRAMES);
	setScanMode(chip_data, SCAN_MODE_ACTIVE, 0x00);
	msleep(WAIT_AFTER_SENSEOFF);
	flushFIFO(chip_data);	/* delete the events related to some
		* touch(allow to call this function
		* while touching the screen without
		* having a flooding of the FIFO) */

	/* get delta data */
	res = getMSFrame3(chip_data, MS_STRENGTH, &framems);

	if (res < 0) {
		TPD_INFO("%s: Error while taking the MS_STRENGTH frame... ERROR %08X\n",
			 __func__, res);
		seq_printf(s, "getMSFrame3 error!\n");
		goto error;
	}

	/* check the Tx and Rx num */
	if (chip_info->hw_res->tx_num != framems.header.force_node
			|| chip_info->hw_res->rx_num != framems.header.sense_node) {
		seq_printf(s, "Tx Rx is not match !\n");
		goto error;
	}

	/* print the data */
	for (x = 0; x < framems.header.force_node; x++) {
		seq_printf(s, "\n[%2d]", x);

		for (y = 0; y < framems.header.sense_node; y++) {
			z = framems.header.sense_node * x + y;
			temp_delta = framems.node_data[z];
			seq_printf(s, "%4d, ", temp_delta);
		}
	}

	seq_printf(s, "\n");

error:
	setScanMode(chip_data, SCAN_MODE_ACTIVE, 0xFF);

	if (framems.node_data != NULL)
		kfree(framems.node_data);
}

/* st80y baseline data read func */
static void st80y_baseline_read(struct seq_file *s, void *chip_data)
{
	int x = 0, y = 0, z = 0;
	int16_t temp_delta = 0;
	int res = 0;
	struct fts_ts_info *chip_info = (struct fts_ts_info *)chip_data;
	MutualSenseFrame framems;

	if (!chip_info)
		return;

	/* lock the mode to avoid IC enter idle mode */
	setScanMode(chip_data, SCAN_MODE_LOCKED, LOCKED_ACTIVE);
	msleep(WAIT_FOR_FRESH_FRAMES);
	setScanMode(chip_data, SCAN_MODE_ACTIVE, 0x00);
	msleep(WAIT_AFTER_SENSEOFF);
	flushFIFO(chip_data);
		/* delete the events related to some
		* touch(allow to call this function
		* while touching the screen without
		* having a flooding of the FIFO) */

	/* get baseline data */
	res = getMSFrame3(chip_data, MS_RAW, &framems);

	if (res < 0) {
		TPD_INFO("%s: Error while taking the MS_RAW frame... ERROR %08X\n", __func__,
			 res);
		seq_printf(s, "getMSFrame3 error!\n");
		goto error;
	}

	/* check the Tx and Rx num */
	if (chip_info->hw_res->tx_num != framems.header.force_node
			|| chip_info->hw_res->rx_num != framems.header.sense_node) {
		seq_printf(s, "Tx Rx is not match !\n");
		goto error;
	}

	/* print the data */
	for (x = 0; x < framems.header.force_node; x++) {
		seq_printf(s, "\n[%2d]", x);

		for (y = 0; y < framems.header.sense_node; y++) {
			z = framems.header.sense_node * x + y;
			temp_delta = framems.node_data[z];
			seq_printf(s, "%4d, ", temp_delta);
		}
	}

	seq_printf(s, "\n");

error:
	setScanMode(chip_data, SCAN_MODE_ACTIVE, 0xFF);

	if (framems.node_data != NULL)
		kfree(framems.node_data);
}

/* st80y self_delta data read func */
static void st80y_self_delta_read(struct seq_file *s, void *chip_data)
{
	int x = 0;
	int res = 0;
	int16_t temp_delta = 0;
	struct fts_ts_info *chip_info = (struct fts_ts_info *)chip_data;
	SelfSenseFrame framess;

	if (!chip_info)
		return;

	/* lock the mode to avoid IC enter idle mode */
	setScanMode(chip_data, SCAN_MODE_LOCKED, LOCKED_ACTIVE);
	msleep(WAIT_FOR_FRESH_FRAMES);
	setScanMode(chip_data, SCAN_MODE_ACTIVE, 0x00);
	msleep(WAIT_AFTER_SENSEOFF);
	flushFIFO(chip_data);
	/* delete the events related to some
	* touch(allow to call this function
	* while touching the screen without
	* having a flooding of the FIFO) */

	/* get self_delta data */
	res = getSSFrame3(chip_data, SS_STRENGTH, &framess);

	if (res < 0) {
		TPD_INFO("%s: Error while taking the SS_STRENGTH frame... ERROR %08X\n",
			 __func__, res);
		seq_printf(s, "getSSFrame3 error!\n");
		goto error;
	}

	/* check the Tx and Rx num */
	if (chip_info->hw_res->tx_num != framess.header.force_node
			|| chip_info->hw_res->rx_num != framess.header.sense_node) {
		seq_printf(s, "Tx Rx is not match !\n");
		goto error;
	}

	/* print the data */
	seq_printf(s, "\n[force_data]");

	for (x = 0; x < framess.header.force_node; x++) {
		seq_printf(s, "\n[%d]", x);
		temp_delta = framess.force_data[x];
		seq_printf(s, "%4d, ", temp_delta);
	}

	seq_printf(s, "\n[sense_data]");

	for (x = 0; x < framess.header.sense_node; x++) {
		temp_delta = framess.sense_data[x];
		seq_printf(s, "%4d, ", temp_delta);
	}

	seq_printf(s, "\n");

error:
	setScanMode(chip_data, SCAN_MODE_ACTIVE, 0xFF);

	if (framess.sense_data != NULL)
		kfree(framess.sense_data);

	if (framess.force_data != NULL)
		kfree(framess.force_data);
}

/* st80y self_raw data read func */
static void st80y_self_raw_read(struct seq_file *s, void *chip_data)
{
	int x = 0;
	int16_t temp_delta = 0;
	int res = 0;
	struct fts_ts_info *chip_info = (struct fts_ts_info *)chip_data;
	SelfSenseFrame framess;

	if (!chip_info)
		return;

	/* lock the mode to avoid IC enter idle mode */
	setScanMode(chip_data, SCAN_MODE_LOCKED, LOCKED_ACTIVE);
	msleep(WAIT_FOR_FRESH_FRAMES);
	setScanMode(chip_data, SCAN_MODE_ACTIVE, 0x00);
	msleep(WAIT_AFTER_SENSEOFF);
	flushFIFO(chip_data);
	/* delete the events related to some
	* touch(allow to call this function
	* while touching the screen without
	* having a flooding of the FIFO) */

	/* get self_raw data */
	res = getSSFrame3(chip_data, SS_RAW, &framess);

	if (res < 0) {
		TPD_INFO("%s: Error while taking the SS_RAW frame... ERROR %08X\n", __func__,
			 res);
		seq_printf(s, "getSSFrame3 error!\n");
		goto error;
	}

	/* check the Tx and Rx num */
	if (chip_info->hw_res->tx_num != framess.header.force_node
			|| chip_info->hw_res->rx_num != framess.header.sense_node) {
		seq_printf(s, "Tx Rx is not match !\n");
		goto error;
	}

	/* print the data */
	seq_printf(s, "\n[force_data]");

	for (x = 0; x < framess.header.force_node; x++) {
		seq_printf(s, "\n[%d]", x);
		temp_delta = framess.force_data[x];
		seq_printf(s, "%4d, ", temp_delta);
	}

	seq_printf(s, "\n[sense_data]");

	for (x = 0; x < framess.header.sense_node; x++) {
		temp_delta = framess.sense_data[x];
		seq_printf(s, "%4d, ", temp_delta);
	}

	seq_printf(s, "\n");

error:
	setScanMode(chip_data, SCAN_MODE_ACTIVE, 0xFF);

	if (framess.sense_data != NULL)
		kfree(framess.sense_data);

	if (framess.force_data != NULL)
		kfree(framess.force_data);
}

/* st80y baseline_blackscreen read func */
static void st80y_baseline_blackscreen_read(struct seq_file *s, void *chip_data)
{
	int x = 0, y = 0, z = 0;
	int16_t temp_delta = 0;
	int res = 0;
	struct fts_ts_info *chip_info = (struct fts_ts_info *)chip_data;
	MutualSenseFrame framems;

	if (!chip_info)
		return;

	/* lock the mode to avoid IC enter idle mode */
	setScanMode(chip_data, SCAN_MODE_LOCKED, LOCKED_LP_ACTIVE);
	msleep(WAIT_FOR_FRESH_FRAMES);
	setScanMode(chip_data, SCAN_MODE_ACTIVE, 0x00);
	msleep(WAIT_AFTER_SENSEOFF);
	flushFIFO(chip_data);
	/* delete the events related to some
	* touch(allow to call this function
	* while touching the screen without
 	* having a flooding of the FIFO) */

	/* get baseline_blackscreen data */
	res = getMSFrame3(chip_data, MS_RAW, &framems);

	if (res < 0) {
		TPD_INFO("%s: Error while taking the MS_RAW frame... ERROR %08X\n", __func__,
			 res);
		seq_printf(s, "getMSFrame3 error!\n");
		goto error;
	}

	/* check the Tx and Rx num */
	if (chip_info->hw_res->tx_num != framems.header.force_node
			|| chip_info->hw_res->rx_num != framems.header.sense_node) {
		seq_printf(s, "Tx Rx is not match !\n");
		goto error;
	}

	/* print the data */
	for (x = 0; x < framems.header.force_node; x++) {
		seq_printf(s, "\n[%2d]", x);

		for (y = 0; y < framems.header.sense_node; y++) {
			z = framems.header.sense_node * x + y;
			temp_delta = framems.node_data[z];
			seq_printf(s, "%4d, ", temp_delta);
		}
	}

	seq_printf(s, "\n");

error:
	setScanMode(chip_data, SCAN_MODE_LOW_POWER, 0xFF);

	if (framems.node_data != NULL)
		kfree(framems.node_data);
}

/* st80y main register read func */
static void st80y_main_register_read(struct seq_file *s, void *chip_data)
{
	char temp[256] = { 0 };
	u8 cal_status = 0;
	int ret = 0;

	struct fts_ts_info *chip_info = (struct fts_ts_info *)chip_data;

	if (!chip_info)
		return;

	TPD_INFO("%s start\n", __func__);
	/*disable irq when read data from IC*/
	seq_printf(s, "====================================================\n");
	seq_printf(s, "FW VER = %04X\n", chip_info->systemInfo.u16_fwVer);
	seq_printf(s, "SVN REV = %04X\n", chip_info->systemInfo.u16_svnRev);
	seq_printf(s, "CONFIG VER = %04X\n", chip_info->systemInfo.u16_cfgVer);
	seq_printf(s, "CONFIG PROJECT ID = %04X\n",
		   chip_info->systemInfo.u16_cfgProjectId);
	seq_printf(s, "CX VER = %04X\n", chip_info->systemInfo.u16_cxVer);
	seq_printf(s, "CX PROJECT ID = %04X\n", chip_info->systemInfo.u16_cxProjectId);
	seq_printf(s, "AFE VER: CFG = %02X - CX = %02X - PANEL = %02X\n",
		   chip_info->systemInfo.u8_cfgAfeVer, chip_info->systemInfo.u8_cxAfeVer,
		   chip_info->systemInfo.u8_panelCfgAfeVer);
	seq_printf(s, "FW VER = %04X\n", chip_info->systemInfo.u16_fwVer);
	seq_printf(s, "%s\n", printHex("Release Info =  ",
				       chip_info->systemInfo.u8_releaseInfo, RELEASE_INFO_SIZE, temp));
	seq_printf(s, "====================================================\n");

	seq_printf(s, "calibration status: 0x%02x\n", chip_info->cal_needed);

	ret = readMpFlag(chip_data, &cal_status);
	TPD_INFO("%s:NEW MP FLAG = %x\n", __func__, cal_status);
	seq_printf(s, "MP FLAG: 0x%02x\n", cal_status);
}

static void st80y_cx2_data(struct seq_file *s, void *chip_data)
{
	int ret = 0;
	MutualSenseData msCompData;
	TotMutualSenseData totCompData;
	int x = 0, y = 0, z = 0;
	int16_t temp_delta = 0;

	struct fts_ts_info *chip_info = (struct fts_ts_info *)chip_data;

	if (!chip_info)
		return;

	msCompData.node_data = NULL;
	totCompData.node_data = NULL;

	/* MS CX TEST */
	TPD_INFO("MS LP CX  are starting...\n");

	ret = readMutualSenseCompensationData(chip_data, LOAD_CX_MS_LOW_POWER,
					      &msCompData);/* read MS compensation data */

	if (ret < 0) {
		TPD_INFO("production_test_data: readMutualSenseCompensationData failed... ERROR %08X\n",
			 ERROR_PROD_TEST_DATA);
		goto EXIT;
	}

	ret = readTotMutualSenseCompensationData(chip_data,
			LOAD_PANEL_CX_TOT_MS_LOW_POWER,
			&totCompData);/* read  TOT MS compensation data */

	if (ret < 0) {
		TPD_INFO("production_test_data: readTotMutualSenseCompensationData failed... ERROR %08X\n",
			 ERROR_PROD_TEST_DATA);
		kfree(msCompData.node_data);
		msCompData.node_data = NULL;
		goto EXIT;
	}

	/* check the Tx and Rx num */
	if (chip_info->hw_res->tx_num != msCompData.header.force_node
			|| chip_info->hw_res->rx_num != msCompData.header.sense_node) {
		seq_printf(s, "Tx Rx is not match !\n");
		goto ERROR;
	}

	seq_printf(s, "\n CX2 RAW\n");

	/* print the data */
	for (x = 0; x < msCompData.header.force_node; x++) {
		seq_printf(s, "\n[%2d]", x);

		for (y = 0; y < msCompData.header.sense_node; y++) {
			z = msCompData.header.sense_node * x + y;
			temp_delta = msCompData.node_data[z];
			seq_printf(s, "%4d, ", temp_delta);
		}
	}

	seq_printf(s, "\n");
ERROR:

	if (msCompData.node_data)
		kfree(msCompData.node_data);

	msCompData.node_data  = NULL;

	if (totCompData.node_data)
		kfree(totCompData.node_data);

	totCompData.node_data  = NULL;
EXIT:
	return;
}

static void st80y_ito_raw(struct seq_file *s, void *chip_data)
{
	int res = OK;
	u8 sett[2] = { 0x00, 0x00 };
	MutualSenseFrame msRawFrame;
	int x = 0, y = 0, z = 0;
	int16_t temp_delta = 0;

	struct fts_ts_info *chip_info = (struct fts_ts_info *)chip_data;

	if (!chip_info)
		return;

	msRawFrame.node_data = NULL;

	TPD_INFO("ITO raw is starting...\n", __func__);
	/*
		res = fts_system_reset();
		if (res < 0) {
			TPD_INFO("%s: ERROR %08X\n", __func__, ERROR_PROD_TEST_ITO);
			goto EXIT;
		}
	*/
	sett[0] = SPECIAL_TUNING_IOFF;
	TPD_DEBUG("Trimming Ioff...\n");
	res = writeSysCmd(chip_info, SYS_CMD_SPECIAL_TUNING, sett, 2);

	if (res < OK) {
		TPD_INFO("production_test_ito: Trimm Ioff ERROR %08X\n",
			 (res | ERROR_PROD_TEST_ITO));
		goto EXIT;
	}

	sett[0] = 0x03;/* change by ST PAUL */
	sett[1] = 0x10;
	TPD_DEBUG("ITO Check command sent...\n");
	res = writeSysCmd(chip_info, SYS_CMD_ITO, sett, 2);

	if (res < OK) {
		TPD_INFO("production_test_ito: ERROR %08X\n", (res | ERROR_PROD_TEST_ITO));
		goto EXIT;
	}

	TPD_DEBUG("ITO Command = OK!\n");
	TPD_INFO("MS RAW ITO ADJ TEST:\n");

	res |= getMSFrame3(chip_data, MS_RAW, &msRawFrame);

	if (res < OK) {
		TPD_INFO("%s: getMSFrame failed... ERROR %08X\n", __func__,
			 ERROR_PROD_TEST_ITO);
		goto EXIT;
	}

	/* check the Tx and Rx num */
	if (chip_info->hw_res->tx_num != msRawFrame.header.force_node
			|| chip_info->hw_res->rx_num != msRawFrame.header.sense_node) {
		seq_printf(s, "Tx Rx is not match !\n");
		goto ERROR;
	}

	seq_printf(s, "\n ITO RAW\n");

	/* print the data */
	for (x = 0; x < msRawFrame.header.force_node; x++) {
		seq_printf(s, "\n[%2d]", x);

		for (y = 0; y < msRawFrame.header.sense_node; y++) {
			z = msRawFrame.header.sense_node * x + y;
			temp_delta = msRawFrame.node_data[z];
			seq_printf(s, "%4d, ", temp_delta);
		}
	}

	seq_printf(s, "\n");


ERROR:

	if (msRawFrame.node_data)
		kfree(msRawFrame.node_data);

	msRawFrame.node_data  = NULL;
EXIT:
	return;
}

static void st80y_reserve_read(struct seq_file *s, void *chip_data)
{
	st80y_cx2_data(s, chip_data);
	st80y_ito_raw(s, chip_data);
	return;
}

/* fts debug node file ops */
static struct debug_info_proc_operations debug_info_proc_ops = {
	.delta_read         = st80y_delta_read,
	.baseline_read      = st80y_baseline_read,
	.self_delta_read    = st80y_self_delta_read,
	.self_raw_read      = st80y_self_raw_read,
	.baseline_blackscreen_read  = st80y_baseline_blackscreen_read,
	.main_register_read = st80y_main_register_read,
	.reserve_read       = st80y_reserve_read,
};

static void st80y_start_aging_test(void *chip_data)
{
	TPD_INFO("%s: not spupport \n", __func__);
}

static void st80y_finish_aging_test(void *chip_data)
{
	TPD_INFO("%s: not spupport \n", __func__);
}

static struct aging_test_proc_operations aging_test_proc_ops = {
	.start_aging_test = st80y_start_aging_test,
	.finish_aging_test = st80y_finish_aging_test,
};

/**
  * Compute the Horizontal adjacent matrix doing the abs of the difference
  * between the column i with the i-1 one. \n
  * Both the original data matrix and the adj matrix are disposed as 1 dimension
  * array one row after the other \n
  * The resulting matrix has one column less than the starting original one \n
  * @param data pointer to the array of signed bytes containing the original
  * data
  * @param row number of rows of the original data
  * @param column number of columns of the original data
  * @param result pointer of a pointer to an array of unsigned bytes which will
  * contain the adj matrix
  * @return OK if success or an error code which specify the type of error
  */
static int computeAdjHoriz(i8 *data, int row, int column, u8 **result)
{
	int i, j;
	int size = row * (column - 1);

	if (column < 2) {
		TPD_INFO("computeAdjHoriz: ERROR %08X\n", ERROR_OP_NOT_ALLOW);
		return ERROR_OP_NOT_ALLOW;
	}

	*result = (u8 *)kzalloc(size * sizeof(u8), GFP_KERNEL);

	if (*result == NULL) {
		TPD_INFO("computeAdjHoriz: ERROR %08X\n", ERROR_ALLOC);
		return ERROR_ALLOC;
	}

	for (i = 0; i < row; i++)
		for (j = 1; j < column; j++)
			*(*result + (i * (column - 1) + (j - 1))) = abs(data[i * column + j] - data[i *
					column + (j - 1)]);

	return OK;
}

/**
  * Compute the Vertical adjacent matrix doing the abs of the difference between
  * the row i with the i-1 one.
  * Both the original data matrix and the adj matrix are disposed as 1 dimension
  * array one row after the other. \n
  * The resulting matrix has one column less than the starting original one \n
  * @param data pointer to the array of signed bytes containing the original
  * data
  * @param row number of rows of the original data
  * @param column number of columns of the original data
  * @param result pointer of a pointer to an array of unsigned bytes which will
  * contain the adj matrix
  * @return OK if success or an error code which specify the type of error
  */
static int computeAdjVert(i8 *data, int row, int column, u8 **result)
{
	int i, j;
	int size = (row - 1) * (column);

	if (row < 2) {
		TPD_INFO("computeAdjVert: ERROR %08X\n", ERROR_OP_NOT_ALLOW);
		return ERROR_OP_NOT_ALLOW;
	}

	*result = (u8 *)kzalloc(size * sizeof(u8), GFP_KERNEL);

	if (*result == NULL) {
		TPD_INFO("computeAdjVert: ERROR %08X\n", ERROR_ALLOC);
		return ERROR_ALLOC;
	}

	for (i = 1; i < row; i++)
		for (j = 0; j < column; j++)
			*(*result + ((i - 1) * column + j)) = abs(data[i * column + j] - data[(i - 1) *
							      column + j]);

	return OK;
}

/**
  * Check that each value of a matrix of u16 doesn't exceed a specific min and
  * Max value  set for each node (these values are included in the interval).
  * The matrixes of data, min and max values are stored as 1 dimension arrays
  * one row after the other.
  * @param data pointer to the array of short containing the data to check
  * @param row number of rows of data
  * @param column number of columns of data
  * @param min pointer to a matrix which specify the minimum value allowed for
  * each node
  * @param max pointer to a matrix which specify the Maximum value allowed for
  * each node
  * @return the number of elements that overcome the specified interval (0 = OK)
  */
static int checkLimitsMapTotalFromU(u16 *data, int row, int column, int *min,
				    int *max)
{
	int i, j;
	int count = 0;

	for (i = 0; i < row; i++) {
		for (j = 0; j < column; j++) {
			if (data[i * column + j] < min[i * column + j]
					|| data[i * column + j] > max[i * column + j]) {
				TPD_INFO("checkLimitsMapTotal: Node[%d,%d] = %d exceed limit [%d, %d]\n", i, j,
					 data[i * column + j], min[i * column + j], max[i * column + j]);
				count++;
			}
		}
	}

	return count;    /* if count is 0 = OK, test completed successfully */
}

static int st80y_production_test_init(struct seq_file *s, void *chip_data,
				      struct auto_testdata *st_testdata,
				      struct test_item_info *p_test_item_info)
{
	int ret = 0;
	u8 cal_status = 0;

	struct fts_ts_info *chip_info = (struct fts_ts_info *)chip_data;

	TPD_INFO("%s: enter\n", __func__);

	ret = readMpFlag(chip_data, &cal_status);
	TPD_INFO("%s:NEW CALI FLAG = %x\n", __func__, cal_status);

	TPD_INFO("%s: calibration verify result(0x%02x)\n", __func__, ret);
	seq_printf(s, "calibration verify result(0x%02x)\n", ret);

	if (chip_info->auto_test_need_cal_support) {
		if ((cal_status != MP_FLAG_FACTORY && ret >= 0)) {
			ret = -1;
			TPD_INFO("error : Not calibration! Please do the calibration first.\n");
			seq_printf(s, "Not calibration! Please do the calibration first.\n");
			return ret;
		}
	}

	return ret;
}

static int st80yCheckLimitsMapAdjHTotal(struct seq_file *s,
					u16 *data, int row, int column, int *max)
{
	int i, j;
	int count = 0;

	/*Ito H test:TX RX-1*/
	for (i = 0; i < row; i++) {
		for (j = 0; j < column - 1; j++) {
			if (data[i * (column - 1) + j] > max[i * column + j]) {
				TPD_INFO("checkLimitsMapAdjTotal: Node[%d,%d] = %d exceed limit > %d\n",
					 i, j, data[i * (column - 1) + j], max[i * column + j]);
				seq_printf(s, "checkLimitsMapAdjTotal: Node[%d,%d] = %d exceed limit > %d\n",
					   i, j, data[i * (column - 1) + j], max[i * column + j]);
				count++;
			}
		}
	}

	return count;	  /* if count is 0 = OK, test completed successfully */
}

static int st80yCheckLimitsMapAdjVTotal(struct seq_file *s,
					u16 *data, int row, int column, int *max)
{
	int i, j;
	int count = 0;

	/*Ito H test:TX-1 RX*/
	for (i = 0; i < row - 1; i++) {
		for (j = 0; j < column; j++) {
			if (data[i * (column) + j] > max[i * column + j]) {
				TPD_INFO("checkLimitsMapAdjTotal: Node[%d,%d] = %d exceed limit > %d\n",
					 i, j, data[i * column + j], max[i * column + j]);
				seq_printf(s, "checkLimitsMapAdjTotal: Node[%d,%d] = %d exceed limit > %d\n",
					   i, j, data[i * column + j], max[i * column + j]);
				count++;
			}
		}
	}

	return count;	  /* if count is 0 = OK, test completed successfully */
}

/**
  * Check that each value of a matrix of short doesn't exceed a specific min and
  *  Max value  set for each node (these values are included in the interval).
  * The matrixes of data, min and max values are stored as 1 dimension arrays
  * one row after the other.
  * @param data pointer to the array of short containing the data to check
  * @param row number of rows of data
  * @param column number of columns of data
  * @param min pointer to a matrix which specify the minimum value allowed for
  * each node
  * @param max pointer to a matrix which specify the Maximum value allowed for
  * each node
  * @return the number of elements that overcome the specified interval (0 = OK)
  */
static int st80yCheckLimitsMapTotal(struct seq_file *s, short *data, int row,
				    int column, int *min, int *max)
{
	int i, j;
	int count = 0;

	for (i = 0; i < row; i++) {
		for (j = 0; j < column; j++) {
			if (data[i * column + j] < min[i * column + j]
					|| data[i * column + j] > max[i * column + j]) {
				TPD_INFO("checkLimitsMapTotal: Node[%d,%d] = %d exceed limit [%d, %d]\n",
					 i, j, data[i * column + j], min[i * column + j], max[i * column + j]);
				seq_printf(s, "checkLimitsMapTotal: Node[%d,%d] = %d exceed limit [%d, %d]\n",
					   i, j, data[i * column + j], min[i * column + j], max[i * column + j]);
				count++;
			}
		}
	}

	return count;    /* if count is 0 = OK, test completed successfully */
}

/**
  * Compute the Horizontal adjacent matrix of short values doing the abs of
  * the difference between the column i with the i-1 one.
  * Both the original data matrix and the adj matrix are disposed as 1 dimension
  *  array one row after the other \n
  * The resulting matrix has one column less than the starting original one \n
  * @param data pointer to the array of signed bytes containing the original
  * data
  * @param row number of rows of the original data
  * @param column number of columns of the original data
  * @param result pointer of a pointer to an array of unsigned bytes which
  * will contain the adj matrix
  * @return OK if success or an error code which specify the type of error
  */
int st80yComputeAdjHorizTotal(short *data, int row, int column, u16 **result)
{
	int i, j;
	int size = row * (column - 1);

	if (column < 2) {
		TPD_INFO("computeAdjHorizTotal: ERROR %08X\n", ERROR_OP_NOT_ALLOW);
		return ERROR_OP_NOT_ALLOW;
	}

	*result = (u16 *)kzalloc(size * sizeof(u16), GFP_KERNEL);

	if (*result == NULL) {
		TPD_INFO("computeAdjHorizTotal: ERROR %08X\n", ERROR_ALLOC);
		return ERROR_ALLOC;
	}

	for (i = 0; i < row; i++)
		for (j = 1; j < column; j++)
			*(*result + (i * (column - 1) + (j - 1))) = abs(data[i * column + j] - data[i *
					column + (j - 1)]);

	return OK;
}

/**
  * Compute the Vertical adjacent matrix of short values doing the abs of
  * the difference between the row i with the i-1 one.
  * Both the original data matrix and the adj matrix are disposed as 1 dimension
  * array one row after the other. \n
  * The resulting matrix has one column less than the starting original one \n
  * @param data pointer to the array of signed bytes containing the original
  * data
  * @param row number of rows of the original data
  * @param column number of columns of the original data
  * @param result pointer of a pointer to an array of unsigned bytes which will
  * contain the adj matrix
  * @return OK if success or an error code which specify the type of error
  */
int st80yComputeAdjVertTotal(short *data, int row, int column, u16 **result)
{
	int i, j;
	int size = (row - 1) * (column);

	if (row < 2) {
		TPD_INFO("computeAdjVertTotal: ERROR %08X\n", ERROR_OP_NOT_ALLOW);
		return ERROR_OP_NOT_ALLOW;
	}

	*result = (u16 *)kzalloc(size * sizeof(u16), GFP_KERNEL);

	if (*result == NULL) {
		TPD_INFO("computeAdjVertTotal: ERROR %08X\n", ERROR_ALLOC);
		return ERROR_ALLOC;
	}

	for (i = 1; i < row; i++)
		for (j = 0; j < column; j++)
			*(*result + ((i - 1) * column + j)) = abs(data[i * column + j] - data[(i - 1) *
							      column + j]);

	return OK;
}

static int st80y_production_test_ito(struct seq_file *s, void *chip_data,
				     struct auto_testdata *st_testdata,
				     struct test_item_info *p_test_item_info)
{
	int res = OK;
	u8 sett[2] = { 0x00, 0x00 };
	MutualSenseFrame msRawFrame;
	u32 *max_limits;
	u32 *min_limits;
	int ret = 0;

	struct fts_ts_info *chip_info = (struct fts_ts_info *)chip_data;

	msRawFrame.node_data = NULL;

	TPD_INFO("ITO Production test is starting...\n", __func__);

	/*get parameter */
	/*max_limits is :MS_RAW_ITO_DATA_ADJ_HORIZONTAL"*/
	/*min_limits is :MS_RAW_ITO_DATA_ADJ_VERTICAL*/
	if (p_test_item_info->para_num) {
		if (p_test_item_info->p_buffer[0]) {
			chip_info->st80ytestdata.itomutualminmaxraw = p_test_item_info->p_buffer[0];

			if (p_test_item_info->item_limit_type == LIMIT_TYPE_TX_RX_DATA) {
				max_limits = (uint32_t *)(st_testdata->fw->data +
							  p_test_item_info->top_limit_offset);
				min_limits = (uint32_t *)(st_testdata->fw->data +
							  p_test_item_info->floor_limit_offset);

			} else {
				TPD_INFO("item: %d get_test_item_info fail\n", LIMIT_TYPE_TX_RX_DATA);
				return -1;
			}
		}

	} else {
		TPD_INFO("item: %d get_test_item_info fail\n", LIMIT_TYPE_TX_RX_DATA);
		return -1;
	}

	res = fts_system_reset(chip_info);

	if (res < 0) {
		TPD_INFO("%s: ERROR %08X\n", __func__, ERROR_PROD_TEST_ITO);
		return res | ERROR_PROD_TEST_ITO;
	}

	sett[0] = SPECIAL_TUNING_IOFF;
	TPD_DEBUG("Trimming Ioff...\n");
	res = writeSysCmd(chip_info, SYS_CMD_SPECIAL_TUNING, sett, 2);

	if (res < OK) {
		TPD_INFO("production_test_ito: Trimm Ioff ERROR %08X\n",
			 (res | ERROR_PROD_TEST_ITO));
		return res | ERROR_PROD_TEST_ITO;
	}

	sett[0] = 0x03;/* change by ST PAUL */
	sett[1] = 0x10;
	TPD_DEBUG("ITO Check command sent...\n");
	res = writeSysCmd(chip_info, SYS_CMD_ITO, sett, 2);

	if (res < OK) {
		TPD_INFO("production_test_ito: ERROR %08X\n", (res | ERROR_PROD_TEST_ITO));
		return res | ERROR_PROD_TEST_ITO;
	}

	TPD_DEBUG("ITO Command = OK!\n");
	TPD_INFO("MS RAW ITO ADJ TEST:\n");

	res |= getMSFrame3(chip_info, MS_RAW, &msRawFrame);

	if (res < OK) {
		TPD_INFO("%s: getMSFrame failed... ERROR %08X\n", __func__,
			 ERROR_PROD_TEST_ITO);
		goto EXIT;
	}

	TPD_INFO("MS RAW ITO save data:\n");

	save_test_result(st_testdata, (short *)msRawFrame.node_data,
			 msRawFrame.node_data_size,
			 LIMIT_TYPE_TX_RX_DATA, "[ITO MS_RAW]");

	TPD_INFO("ITO MS RAW MIN MAX TEST:\n");

	if (chip_info->st80ytestdata.itomutualminmaxraw == 1) {
		if (max_limits && min_limits) {
			res = st80yCheckLimitsMapTotal(s, msRawFrame.node_data,
						       msRawFrame.header.force_node, msRawFrame.header.sense_node, min_limits,
						       max_limits);

			if (res != OK) {
				TPD_INFO("production_test_data: checkLimitsMinMaxEachNodeData failed... ERROR COUNT = %d\n",
					 ret);
				TPD_INFO("ITO MS RAW MAP MIN MAX TEST:.................FAIL\n\n");
				seq_printf(s, "ITO MS RAW MAP MIN MAX TEST FAIL\n");
				goto ERROR;
			} else
				TPD_INFO("ITO MS RAW MAP MIN MAX TEST:.................OK\n");
		}
	} else
		TPD_INFO("ITO MS RAW MAP MIN MAX TEST:.................SKIPPED\n");

ERROR:

	if (msRawFrame.node_data)
		kfree(msRawFrame.node_data);

	msRawFrame.node_data  = NULL;
EXIT:
	return res;
}


static int st80y_production_test_ito_adj(struct seq_file *s, void *chip_data,
		struct auto_testdata *st_testdata,
		struct test_item_info *p_test_item_info)
{
	int res = OK;
	u8 sett[2] = { 0x00, 0x00 };
	MutualSenseFrame msRawFrame;
	u16 *adj = NULL;
	u32 *max_limits;
	u32 *min_limits;
	/*int ret = 0;*/
	struct fts_ts_info *info = (struct fts_ts_info *)chip_data;

	msRawFrame.node_data = NULL;

	TPD_INFO("ITO Production test is starting...\n", __func__);

	/*get parameter */
	/*max_limits is :MS_RAW_ITO_DATA_ADJ_HORIZONTAL"*/
	/*min_limits is :MS_RAW_ITO_DATA_ADJ_VERTICAL*/
	if (p_test_item_info->para_num) {
		if (p_test_item_info->p_buffer[0]) {
			if (p_test_item_info->item_limit_type == LIMIT_TYPE_TX_RX_DATA) {
				max_limits = (uint32_t *)(st_testdata->fw->data +
							  p_test_item_info->top_limit_offset);
				min_limits = (uint32_t *)(st_testdata->fw->data +
							  p_test_item_info->floor_limit_offset);

			} else {
				TPD_INFO("item: %d get_test_item_info fail\n", LIMIT_TYPE_TX_RX_DATA);
				return -1;
			}
		}

	} else {
		TPD_INFO("item: %d get_test_item_info fail\n", LIMIT_TYPE_TX_RX_DATA);
		return -1;
	}

	res = fts_system_reset(info);

	if (res < 0) {
		TPD_INFO("%s: ERROR %08X\n", __func__, ERROR_PROD_TEST_ITO);
		return res | ERROR_PROD_TEST_ITO;
	}

	sett[0] = SPECIAL_TUNING_IOFF;
	TPD_DEBUG("Trimming Ioff...\n");
	res = writeSysCmd(info, SYS_CMD_SPECIAL_TUNING, sett, 2);

	if (res < OK) {
		TPD_INFO("production_test_ito: Trimm Ioff ERROR %08X\n",
			 (res | ERROR_PROD_TEST_ITO));
		return res | ERROR_PROD_TEST_ITO;
	}

	sett[0] = 0x03;/*change by ST PAUL*/
	sett[1] = 0x10;
	TPD_DEBUG("ITO Check command sent...\n");
	res = writeSysCmd(info, SYS_CMD_ITO, sett, 2);

	if (res < OK) {
		TPD_INFO("production_test_ito: ERROR %08X\n", (res | ERROR_PROD_TEST_ITO));
		return res | ERROR_PROD_TEST_ITO;
	}

	TPD_DEBUG("ITO Command = OK!\n");
	TPD_INFO("MS RAW ITO ADJ TEST:\n");

	res |= getMSFrame3(chip_data, MS_RAW, &msRawFrame);

	if (res < OK) {
		TPD_INFO("%s: getMSFrame failed... ERROR %08X\n", __func__,
			 ERROR_PROD_TEST_ITO);
		goto EXIT;
	}

	TPD_INFO("MS RAW ITO save data:\n");

	save_test_result(st_testdata, (short *)msRawFrame.node_data,
			 msRawFrame.node_data_size,
			 LIMIT_TYPE_TX_RX_DATA, "[ITO MS_RAW ADJ]");

	TPD_INFO("MS RAW ITO ADJ HORIZONTAL TEST:\n");
	res = st80yComputeAdjHorizTotal(msRawFrame.node_data,
					msRawFrame.header.force_node, msRawFrame.header.sense_node, &adj);

	if (res < OK) {
		TPD_INFO("%s: computeAdjHoriz failed... ERROR %08X\n", __func__,
			 ERROR_PROD_TEST_ITO);
		goto ERROR;
	}

	res = st80yCheckLimitsMapAdjHTotal(s, adj, msRawFrame.header.force_node,
					   msRawFrame.header.sense_node, max_limits);

	if (res != OK) {
		TPD_INFO("production_test_data: checkLimitsAdj MS RAW ITO ADJH failed... ERROR COUNT = %d\n",
			 res);
		TPD_INFO("MS RAW ITO ADJ HORIZONTAL TEST:.................FAIL\n\n");
		seq_printf(s, "MS RAW ITO ADJ HORIZONTAL TEST FAIL\n");
	} else
		TPD_INFO("MS RAW ITO ADJ HORIZONTAL TEST:.................OK\n");

	kfree(adj);
	adj = NULL;

	TPD_INFO("MS RAW ITO ADJ VERTICAL TEST:\n");
	res = st80yComputeAdjVertTotal(msRawFrame.node_data,
				       msRawFrame.header.force_node, msRawFrame.header.sense_node, &adj);

	if (res < OK)
		TPD_INFO("%s: computeAdjVert failed... ERROR %08X\n", __func__,
			 ERROR_PROD_TEST_ITO);

	res = st80yCheckLimitsMapAdjVTotal(s, adj, msRawFrame.header.force_node,
					   msRawFrame.header.sense_node, min_limits);

	if (res != OK) {
		TPD_INFO("%s: checkLimitsAdj MS RAW ITO ADJV failed... ERROR COUNT = %d\n",
			 __func__, res);
		TPD_INFO("MS RAW ITO ADJ VERTICAL TEST:.................FAIL\n\n");
		seq_printf(s, "MS RAW ITO ADJ VERTICAL TEST FAIL\n");
		res = ERROR_PROD_TEST_ITO;
	} else
		TPD_INFO("MS RAW ITO ADJ VERTICAL TEST:.................OK\n");

	kfree(adj);
	adj = NULL;
	TPD_INFO("ITO TEST OK!\n");
	/*
	TPD_INFO("INITIALIZATION TEST :\n");

	if (chip_info->st80ytestdata.init_type != NO_INIT) {
		res = st80y_production_test_initialization((u8)
				chip_info->st80ytestdata.init_type);

		if (res < 0)
			TPD_INFO("Error during  INITIALIZATION TEST! ERROR %08X\n", res);

		else
			TPD_INFO("INITIALIZATION TEST OK!\n");
	} else
		TPD_INFO("INITIALIZATION TEST :................. SKIPPED\n");

	if (chip_info->st80ytestdata.init_type != NO_INIT) {
		TPD_DEBUG("Cleaning up...\n");
		ret = fts_system_reset();

		if (ret < 0) {
			TPD_INFO("production_test_main: system reset ERROR %08X\n", ret);
			res |= ret;
		}
	}
	*/
ERROR:

	if (msRawFrame.node_data)
		kfree(msRawFrame.node_data);

	msRawFrame.node_data  = NULL;
EXIT:
	return res;
}

static int st80yCheckLimitsMinMax(struct seq_file *s,
				  short *data, int row,
				  int column, int min, int max)
{
	int i, j;
	int count = 0;

	for (i = 0; i < row; i++) {
		for (j = 0; j < column; j++) {
			if (data[i * column + j] < min || data[i * column + j] > max) {
				TPD_INFO("checkLimitsMinMax: Node[%d,%d] = %d exceed limit [%d, %d]\n",
					 i, j, data[i * column + j], min, max);
				seq_printf(s, "checkLimitsMapAdjTotal: Node[%d,%d] = %d exceed limit > %d\n",
					   i, j, data[i * column + j], min, max);
				count++;
			}
		}
	}

	return count;	  /* if count is 0 = OK, test completed successfully */
}

static int st80y_production_test_ms_raw(struct seq_file *s, void *chip_data,
					struct auto_testdata *st_testdata,
					struct test_item_info *p_test_item_info)
{
	int ret = 0;
	MutualSenseFrame msRawFrame;
	int maxraw = 0, minraw = 0;
	u32 *max_limits = NULL;
	u32 *min_limits = NULL;

	struct fts_ts_info *chip_info = (struct fts_ts_info *)chip_data;

	msRawFrame.node_data = NULL;

	/*get parameter */
	/*max_limits is :MS_RAW_ITO_DATA_ADJ_HORIZONTAL"*/
	/*min_limits is :MS_RAW_ITO_DATA_ADJ_VERTICAL*/
	if (p_test_item_info->para_num) {
		if (p_test_item_info->p_buffer[0]) {
			chip_info->st80ytestdata.mutualrawmap = p_test_item_info->p_buffer[0];
			chip_info->st80ytestdata.mutualraw = p_test_item_info->p_buffer[0];

			if (p_test_item_info->item_limit_type == LIMIT_TYPE_TX_RX_DATA) {
				max_limits = (uint32_t *)(st_testdata->fw->data +
							  p_test_item_info->top_limit_offset);
				min_limits = (uint32_t *)(st_testdata->fw->data +
							  p_test_item_info->floor_limit_offset);

			} else {
				chip_info->st80ytestdata.test_ok++;
				TPD_INFO("item: %d get_test_item_info fail\n", LIMIT_TYPE_TX_RX_DATA);
				return -1;
			}
		}

	} else {
		chip_info->st80ytestdata.test_ok++;
		TPD_INFO("item: %d get_test_item_info fail\n", LIMIT_TYPE_TX_RX_DATA);
		return -1;
	}

	TPD_INFO("MS RAW DATA TEST is starting...\n");

	ret = fts_system_reset(chip_info);

	if (ret < 0) {
		TPD_INFO("%s: ERROR %08X\n", __func__, ERROR_PROD_TEST_ITO);
		return ret | ERROR_PROD_TEST_ITO;
	}

	ret = setScanMode(chip_data, SCAN_MODE_LOCKED, LOCKED_ACTIVE);
	msleep(WAIT_FOR_FRESH_FRAMES);
	/*ret |= setScanMode(SCAN_MODE_ACTIVE, 0x00);*/
	/*msleep(WAIT_AFTER_SENSEOFF);*/
	ret |= getMSFrame3(chip_data, MS_RAW, &msRawFrame);

	if (ret < OK) {
		chip_info->st80ytestdata.test_ok++;
		TPD_INFO("production_test_data: getMSFrame failed... ERROR %08X\n",
			 ERROR_PROD_TEST_DATA);
		return ret | ERROR_PROD_TEST_DATA;
	}

	TPD_INFO("MS RAW DATA TEST save data:\n");
	save_test_result(st_testdata, (short *)msRawFrame.node_data,
			 msRawFrame.node_data_size,
			 LIMIT_TYPE_TX_RX_DATA, "[MS_RAW]");

	TPD_INFO("MS RAW MIN MAX TEST:\n");

	if (chip_info->st80ytestdata.mutualraw == 1) {
		/*get ms raw parameter*/
		if (p_test_item_info->para_num && p_test_item_info->p_buffer[0]) {
			minraw = p_test_item_info->p_buffer[1];
			maxraw = p_test_item_info->p_buffer[2];
			TPD_INFO("minraw:%d,maxraw:%d\n", minraw, maxraw);

		} else {
			chip_info->st80ytestdata.test_ok++;
			ret |= ERROR_PROD_TEST_DATA;
			goto ERROR;
		}

		ret = st80yCheckLimitsMinMax(s, msRawFrame.node_data,
					     msRawFrame.header.force_node,
					     msRawFrame.header.sense_node, minraw, maxraw);

		if (ret != OK) {
			TPD_INFO("production_test_data: checkLimitsMinMax MS RAW failed... ERROR COUNT = %d\n",
				 ret);
			TPD_INFO("MS RAW MIN MAX TEST:.................FAIL\n\n");
			seq_printf(s, "MS RAW MIN MAX TEST FAIL\n");
			chip_info->st80ytestdata.test_ok++;
			goto ERROR;
		} else
			TPD_INFO("MS RAW MIN MAX TEST:.................OK\n");
	} else
		TPD_INFO("MS RAW MIN MAX TEST:.................SKIPPED\n");

	TPD_INFO("MS RAW MAP MIN MAX TEST:\n");

	if (chip_info->st80ytestdata.mutualrawmap == 1) {
		if (max_limits && min_limits) {
			ret = st80yCheckLimitsMapTotal(s, msRawFrame.node_data,
						       msRawFrame.header.force_node, msRawFrame.header.sense_node, min_limits,
						       max_limits);

			if (ret != OK) {
				chip_info->st80ytestdata.test_ok++;
				TPD_INFO("production_test_data: checkLimitsMinMaxEachNodeData failed... ERROR COUNT = %d\n",
					 ret);
				TPD_INFO("MS RAW MAP MIN MAX TEST:.................FAIL\n\n");
				seq_printf(s, "MS RAW MAP MIN MAX TEST FAIL\n");
				goto ERROR;
			} else
				TPD_INFO("MS RAW MAP MIN MAX TEST:.................OK\n");
		}
	} else
		TPD_INFO("MS RAW MAP MIN MAX TEST:.................SKIPPED\n");


ERROR:

	if (msRawFrame.node_data)
		kfree(msRawFrame.node_data);

	msRawFrame.node_data  = NULL;

	return ret;
}

static int st80yCheckLimitsMap(struct seq_file *s, i8 *data, int row,
			       int column, int *min, int *max)
{
	int i, j;
	int count = 0;

	for (i = 0; i < row; i++) {
		for (j = 0; j < column; j++) {
			if (data[i * column + j] < min[i * column + j]
					|| data[i * column + j] > max[i * column + j]) {
				TPD_INFO("checkLimitsMap: Node[%d,%d] = %d exceed limit [%d, %d]\n", i, j,
					 data[i * column + j], min[i * column + j], max[i * column + j]);
				seq_printf(s, "checkLimitsMap: Node[%d,%d] = %d exceed limit [%d, %d]\n", i, j,
					   data[i * column + j], min[i * column + j], max[i * column + j]);

				count++;
			}
		}
	}

	return count;    /* if count is 0 = OK, test completed successfully */
}

/**
  * Perform all the tests selected in a TestTodo variable related to MS Init
  * data (touch, keys etc..)
  * @param path_limits name of Production Limit file to load or
  * "NULL" if the limits data should be loaded by a .h file
  * @param stop_on_fail if 1, the test flow stops at the first data check
  * failure otherwise it keeps going performing all the selected test
  * @param todo pointer to a TestToDo variable which select the test to do
  * @return OK if success or an error code which specify the type of error
  */
static int st80y_production_test_ms_cx_lp(struct seq_file *s, void *chip_data,
		struct auto_testdata *st_testdata,
		struct test_item_info *p_test_item_info)
{
	int ret = 0;
	MutualSenseData msCompData;
	TotMutualSenseData totCompData;
	int32_t *cx2_max_limits = NULL;
	int32_t *cx2_min_limits = NULL;

	short 	*short_node_data = NULL;
	int i = 0;

	struct fts_ts_info *chip_info = (struct fts_ts_info *)chip_data;

	msCompData.node_data = NULL;
	totCompData.node_data = NULL;

	/*get parameter */
	/*cx2_max_limits is :MS_TOUCH_LOWPOWER_CX2_MAX*/
	/*cx2_min_limits is :MS_TOUCH_LOWPOWER_CX2_MIN*/
	if (p_test_item_info->para_num) {
		if (p_test_item_info->p_buffer[0]) {
			chip_info->st80ytestdata.mutualcx2lp = p_test_item_info->p_buffer[0];

			if (p_test_item_info->item_limit_type == LIMIT_TYPE_TX_RX_DATA) {
				cx2_max_limits = (uint32_t *)(st_testdata->fw->data +
							      p_test_item_info->top_limit_offset);
				cx2_min_limits = (uint32_t *)(st_testdata->fw->data +
							      p_test_item_info->floor_limit_offset);

			} else {
				chip_info->st80ytestdata.test_ok++;
				TPD_INFO("item: %d get_test_item_info fail\n", LIMIT_TYPE_TX_RX_DATA);
				return -1;
			}
		}

	} else {
		chip_info->st80ytestdata.test_ok++;
		TPD_INFO("item: %d get_test_item_info fail\n", LIMIT_TYPE_TX_RX_DATA);
		return -1;
	}

	/* MS CX TEST */
	TPD_INFO("MS LP CX Testes are starting...\n");

	ret = readMutualSenseCompensationData(chip_data, LOAD_CX_MS_LOW_POWER,
					      &msCompData);/* read MS compensation data */

	if (ret < 0) {
		chip_info->st80ytestdata.test_ok++;
		TPD_INFO("production_test_data: readMutualSenseCompensationData failed... ERROR %08X\n",
			 ERROR_PROD_TEST_DATA);
		return ret | ERROR_PROD_TEST_DATA;
	}

	ret = readTotMutualSenseCompensationData(chip_data,
			LOAD_PANEL_CX_TOT_MS_LOW_POWER,
			&totCompData);/* read  TOT MS compensation data */

	if (ret < 0) {
		chip_info->st80ytestdata.test_ok++;
		TPD_INFO("production_test_data: readTotMutualSenseCompensationData failed... ERROR %08X\n",
			 ERROR_PROD_TEST_DATA);
		kfree(msCompData.node_data);
		msCompData.node_data = NULL;
		return ret | ERROR_PROD_TEST_DATA;
	}

	short_node_data = (short *)kzalloc(msCompData.node_data_size * sizeof(short),
					   GFP_KERNEL);

	if (short_node_data == NULL) {
		TPD_INFO("%s: ERROR %08X\n", __func__, ERROR_ALLOC | ERROR_GET_FRAME);
		goto ERROR;
	}

	for (i = 0 ; i < msCompData.node_data_size; i++)
		short_node_data[i] = msCompData.node_data[i];

	TPD_INFO("MS LP CX save data:\n");
	save_test_result(st_testdata, (short *)short_node_data,
			 msCompData.node_data_size,
			 LIMIT_TYPE_TX_RX_DATA, "MS_TOUCH_LOWPOWER_CX2 MAX MIN");
	kfree(short_node_data);
	short_node_data = NULL;

	TPD_INFO("MS LP CX2 MIN MAX TEST:\n");

	if (chip_info->st80ytestdata.mutualcx2lp == 1) {
		ret = st80yCheckLimitsMap(s, msCompData.node_data, msCompData.header.force_node,
					  msCompData.header.sense_node, cx2_min_limits,
					  cx2_max_limits);/* check the limits */

		if (ret != OK) {
			chip_info->st80ytestdata.test_ok++;
			TPD_INFO("production_test_data: checkLimitsMap MS LP CX2 MIN MAX failed... ERROR COUNT = %d\n",
				 ret);
			TPD_INFO(" MS LP CX2 MIN MAX TEST:.................FAIL\n\n");
			seq_printf(s, "MS LP CX2 MIN MAX TEST FAIL\n");
		} else
			TPD_INFO("MS LP CX2 MIN MAX TEST:.................OK\n\n");
	} else
		TPD_INFO("MS LP CX2 MIN MAX TEST:.................SKIPPED\n\n");

ERROR:

	if (msCompData.node_data)
		kfree(msCompData.node_data);

	msCompData.node_data  = NULL;

	if (totCompData.node_data)
		kfree(totCompData.node_data);

	totCompData.node_data  = NULL;

	return ret;
}

/**
  * Check that each value of a matrix of u8 doesn't exceed a specific Max value
  * set for each node (max value is included in the interval).
  * The matrixes of data and max values are stored as 1 dimension arrays one row
  * after the other.
  * @param data pointer to the array of short containing the data to check
  * @param row number of rows of data
  * @param column number of columns of data
  * @param max pointer to a matrix which specify the Maximum value allowed for
  *each node
  * @return the number of elements that overcome the specified interval (0 = OK)
  */
static int st80yCheckLimitsMapAdjH(struct seq_file *s, u8 *data, int row,
				   int column, int *max)
{
	int i, j;
	int count = 0;

	for (i = 0; i < row; i++) {
		for (j = 0; j < column - 1; j++) {
			if (data[i * (column - 1) + j] > max[i * column + j]) {
				TPD_INFO("checkLimitsMapAdj: Node[%d,%d] = %d exceed limit > %d\n",
					 i, j, data[i * (column - 1) + j], max[i * column + j]);
				seq_printf(s, "checkLimitsMapAdj: Node[%d,%d] = %d exceed limit > %d\n",
					   i, j, data[i * (column - 1) + j], max[i * column + j]);
				count++;
			}
		}
	}

	return count;    /* if count is 0 = OK, test completed successfully */
}

static int st80yCheckLimitsMapAdjV(struct seq_file *s, u8 *data, int row,
				   int column, int *max)
{
	int i, j;
	int count = 0;

	for (i = 0; i < row - 1; i++) {
		for (j = 0; j < column; j++) {
			if (data[i * column + j] > max[i * column + j]) {
				TPD_INFO("checkLimitsMapAdj: Node[%d,%d] = %d exceed limit > %d\n",
					 i, j, data[i * column + j], max[i * column + j]);
				seq_printf(s, "checkLimitsMapAdj: Node[%d,%d] = %d exceed limit > %d\n",
					   i, j, data[i * column + j], max[i * column + j]);
				count++;
			}
		}
	}

	return count;    /* if count is 0 = OK, test completed successfully */
}

static int st80y_production_test_ms_cx_lp_adj(struct seq_file *s,
		void *chip_data,
		struct auto_testdata *st_testdata,
		struct test_item_info *p_test_item_info)
{
	int ret = 0;
	MutualSenseData msCompData;
	TotMutualSenseData totCompData;
	int32_t *cx2_adj_h_limits = NULL;
	int32_t *cx2_adj_v_limits = NULL;
	u8 *adjhor = NULL;
	u8 *adjvert = NULL;
	short 	*short_node_data = NULL;
	int i = 0;

	struct fts_ts_info *chip_info = (struct fts_ts_info *)chip_data;

	msCompData.node_data = NULL;
	totCompData.node_data = NULL;

	/*get parameter */
	/*cx2_adj_h_limits is :MS_TOUCH_LOWPOWER_CX2_ADJ_HORIZONTAL*/
	/*cx2_adj_v_limits is :*MS_TOUCH_LOWPOWER_CX2_ADJ_VERTICAL*/
	if (p_test_item_info->para_num) {
		if (p_test_item_info->p_buffer[0]) {
			chip_info->st80ytestdata.mutualcx2adjlp = p_test_item_info->p_buffer[0];

			if (p_test_item_info->item_limit_type == LIMIT_TYPE_TX_RX_DATA) {
				cx2_adj_h_limits = (uint32_t *)(st_testdata->fw->data +
								p_test_item_info->top_limit_offset);
				cx2_adj_v_limits = (uint32_t *)(st_testdata->fw->data +
								p_test_item_info->floor_limit_offset);

			} else {
				chip_info->st80ytestdata.test_ok++;
				TPD_INFO("item: %d get_test_item_info fail\n", LIMIT_TYPE_TX_RX_DATA);
				return -1;
			}
		}

	} else {
		chip_info->st80ytestdata.test_ok++;
		TPD_INFO("item: %d get_test_item_info fail\n", LIMIT_TYPE_TX_RX_DATA);
		return -1;
	}

	/* MS CX TEST */
	TPD_INFO("MS LP CX Testes are starting...\n");

	ret = readMutualSenseCompensationData(chip_data, LOAD_CX_MS_LOW_POWER,
					      &msCompData);/* read MS compensation data */

	if (ret < 0) {
		chip_info->st80ytestdata.test_ok++;
		TPD_INFO("production_test_data: readMutualSenseCompensationData failed... ERROR %08X\n",
			 ERROR_PROD_TEST_DATA);
		return ret | ERROR_PROD_TEST_DATA;
	}

	ret = readTotMutualSenseCompensationData(chip_data,
			LOAD_PANEL_CX_TOT_MS_LOW_POWER,
			&totCompData);/* read  TOT MS compensation data */

	if (ret < 0) {
		chip_info->st80ytestdata.test_ok++;
		TPD_INFO("production_test_data: readTotMutualSenseCompensationData failed... ERROR %08X\n",
			 ERROR_PROD_TEST_DATA);
		kfree(msCompData.node_data);
		msCompData.node_data = NULL;
		return ret | ERROR_PROD_TEST_DATA;
	}

	short_node_data = (short *)kzalloc(msCompData.node_data_size * sizeof(short),
					   GFP_KERNEL);

	if (short_node_data == NULL) {
		TPD_INFO("%s: ERROR %08X\n", __func__, ERROR_ALLOC | ERROR_GET_FRAME);
		goto ERROR;
	}

	for (i = 0 ; i < msCompData.node_data_size; i++)
		short_node_data[i] = msCompData.node_data[i];

	TPD_INFO("MS LP CX save data:\n");
	save_test_result(st_testdata, (short *)short_node_data,
			 msCompData.node_data_size,
			 LIMIT_TYPE_TX_RX_DATA, "MS_TOUCH_LOWPOWER_CX2 ADJ");
	kfree(short_node_data);
	short_node_data = NULL;

	TPD_INFO("MS LP CX2 ADJ TEST:\n");

	if (chip_info->st80ytestdata.mutualcx2adjlp == 1) {
		/* MS CX2 ADJ HORIZ */
		TPD_INFO("MS LP CX2 ADJ HORIZ TEST:\n");
		ret = computeAdjHoriz(msCompData.node_data, msCompData.header.force_node,
				      msCompData.header.sense_node, &adjhor);

		if (ret < 0) {
			chip_info->st80ytestdata.test_ok++;
			TPD_INFO("production_test_data: computeAdjHoriz failed... ERROR %08X\n",
				 ERROR_PROD_TEST_DATA);
			ret |= ERROR_PROD_TEST_DATA;
			goto ERROR;
		}

		TPD_INFO("MS LP CX2 ADJ HORIZ computed!\n");

		ret = st80yCheckLimitsMapAdjH(s, adjhor, msCompData.header.force_node,
					      msCompData.header.sense_node, cx2_adj_h_limits);

		if (ret != OK) {
			chip_info->st80ytestdata.test_ok++;
			TPD_INFO("production_test_data: checkLimitsMapAdj CX2 ADJH LP failed... ERROR COUNT = %d\n",
				 ret);
			TPD_INFO("MS LP CX2 ADJ HORIZ TEST:.................FAIL\n\n");
			seq_printf(s, "MS LP CX2 ADJ HORIZ TEST FAIL\n");
			goto ERROR1;
		} else
			TPD_INFO("MS LP CX2 ADJ HORIZ TEST:.................OK\n\n");

		kfree(adjhor);
		adjhor = NULL;

		/* MS CX2 ADJ VERT */
		TPD_INFO("MS LP CX2 ADJ VERT TEST:\n");

		ret = computeAdjVert(msCompData.node_data, msCompData.header.force_node,
				     msCompData.header.sense_node, &adjvert);

		if (ret < 0) {
			chip_info->st80ytestdata.test_ok++;
			TPD_INFO("production_test_data: computeAdjVert failed... ERROR %08X\n",
				 ERROR_PROD_TEST_DATA);
			ret |= ERROR_PROD_TEST_DATA;
			goto ERROR;
		}

		TPD_INFO("MS LP CX2 ADJ VERT computed!\n");

		ret = st80yCheckLimitsMapAdjV(s, adjvert, msCompData.header.force_node - 1,
					      msCompData.header.sense_node - 1, cx2_adj_v_limits);

		if (ret != OK) {
			chip_info->st80ytestdata.test_ok++;
			TPD_INFO("production_test_data: checkLimitsMapAdj CX2 ADJV LP failed... ERROR COUNT = %d\n",
				 ret);
			TPD_INFO(" MS LP CX2 ADJ HORIZ TEST:.................FAIL\n\n");
			seq_printf(s, "MS LP CX2 ADJ HORIZ TEST FAIL\n");
			goto ERROR;
		} else
			TPD_INFO("MS LP CX2 ADJ VERT TEST:.................OK\n\n");

		kfree(adjvert);
		adjvert = NULL;
	} else
		TPD_DEBUG("MS LP CX2 ADJ TEST:.................SKIPPED\n\n");

ERROR1:

	if (adjhor)
		kfree(adjhor);

	adjhor = NULL;

ERROR:

	if (msCompData.node_data)
		kfree(msCompData.node_data);

	msCompData.node_data  = NULL;

	if (totCompData.node_data)
		kfree(totCompData.node_data);

	totCompData.node_data  = NULL;

	return ret;
}

static int st80y_production_test_ss_raw(struct seq_file *s, void *chip_data,
					struct auto_testdata *st_testdata,
					struct test_item_info *p_test_item_info)
{
	int ret = 0;
	SelfSenseFrame ssRawFrame;
	int32_t *self_max_limits = NULL;
	int32_t *self_min_limits = NULL;

	int32_t *self_rx_max_limits = NULL;
	int32_t *self_rx_min_limits = NULL;
	uint8_t  data_buf[64];
	int i;

	struct fts_ts_info *chip_info = (struct fts_ts_info *)chip_data;

	ssRawFrame.sense_data = NULL;
	ssRawFrame.force_data = NULL;

	if (p_test_item_info->para_num) {
		if (p_test_item_info->p_buffer[0]) {
			chip_info->st80ytestdata.selfforcerawmap = p_test_item_info->p_buffer[0];
			chip_info->st80ytestdata.selfsenserawmap = p_test_item_info->p_buffer[0];

			if (p_test_item_info->item_limit_type == LIMIT_TYPE_SLEF_TX_RX_DATA) {
				TPD_INFO("get IMIT_TYPE_SLEFRAW_DATA data\n");
				self_max_limits = (int32_t *)(st_testdata->fw->data +
							      p_test_item_info->top_limit_offset);
				self_rx_max_limits = (int32_t *)(st_testdata->fw->data +
								 p_test_item_info->top_limit_offset) + st_testdata->tx_num;
				self_min_limits = (int32_t *)(st_testdata->fw->data +
							      p_test_item_info->floor_limit_offset);
				self_rx_min_limits = (int32_t *)(st_testdata->fw->data +
								 p_test_item_info->floor_limit_offset) + st_testdata->tx_num;

			} else {
				chip_info->st80ytestdata.test_ok++;
				TPD_INFO("item: %d get_test_item_info fail\n", LIMIT_TYPE_SLEF_TX_RX_DATA);
				return ret | ERROR_PROD_TEST_DATA;
			}

		} else {
			chip_info->st80ytestdata.test_ok++;
			TPD_INFO("TYPE_SPECIAL_SELFRAW_MAX_MIN para_num is invalid\n");
			return ret | ERROR_PROD_TEST_DATA;
		}
	}

	/* SS TEST */
	TPD_INFO("SS RAW Testes are starting...\n");

	/************** Self Sense Test **************/

	TPD_INFO("Getting SS Frame...\n");
	ret = setScanMode(chip_data, SCAN_MODE_LOCKED, LOCKED_ACTIVE);
	msleep(WAIT_FOR_FRESH_FRAMES);
	ret |= setScanMode(chip_data, SCAN_MODE_ACTIVE, 0x00);
	msleep(WAIT_AFTER_SENSEOFF);
	ret |= getSSFrame3(chip_data, SS_RAW, &ssRawFrame);

	if (ret < 0) {
		chip_info->st80ytestdata.test_ok++;
		TPD_INFO("production_test_data: getSSFrame failed... ERROR %08X\n",
			 ERROR_PROD_TEST_DATA);
		return ret | ERROR_PROD_TEST_DATA;
	}

	/* SS RAW (PROXIMITY) FORCE TEST */
	TPD_INFO("SS RAW FORCE MAP MIN MAX TEST:\n");

	TPD_INFO("SS RAW FORCE save data:\n");
	memset(data_buf, 0, sizeof(data_buf));

	if (ssRawFrame.force_data) {
		if (!IS_ERR_OR_NULL(st_testdata->fp)) {
			snprintf(data_buf, 64, "\n%s\n", "[SS RAW FORCE]");
			tp_test_write(st_testdata->fp, st_testdata->length,
				      data_buf, strlen(data_buf), st_testdata->pos);
		}

		for (i = 0; i < ssRawFrame.header.force_node; i++) {
			if (!IS_ERR_OR_NULL(st_testdata->fp)) {
				snprintf(data_buf, 64, "%d,", ssRawFrame.force_data[i]);
				tp_test_write(st_testdata->fp, st_testdata->length, data_buf, strlen(data_buf),
					      st_testdata->pos);
			}
		}
	}

	if (ssRawFrame.sense_data) {
		if (!IS_ERR_OR_NULL(st_testdata->fp)) {
			snprintf(data_buf, 64, "\n%s\n", "[SS RAW SENSE]");
			tp_test_write(st_testdata->fp, st_testdata->length,
				      data_buf, strlen(data_buf), st_testdata->pos);
		}

		for (i = 0; i < ssRawFrame.header.sense_node; i++) {
			if (!IS_ERR_OR_NULL(st_testdata->fp)) {
				snprintf(data_buf, 64, "%d,", ssRawFrame.sense_data[i]);
				tp_test_write(st_testdata->fp, st_testdata->length, data_buf, strlen(data_buf),
					      st_testdata->pos);
			}
		}
	}

	if (chip_info->st80ytestdata.selfforcerawmap == 1) {
		ret = st80yCheckLimitsMapTotal(s, ssRawFrame.force_data,
					       ssRawFrame.header.force_node, 1, self_min_limits, self_max_limits);

		if (ret != OK) {
			TPD_INFO("production_test_data: checkLimitsMinMax SS RAW FORCE MAP failed... ERROR COUNT = %d\n",
				 ret);
			TPD_INFO("SS RAW FORCE MAP MIN MAX TEST:.................FAIL\n\n");
			seq_printf(s, "SS RAW FORCE MAP MIN MAX TEST FAIL\n");
			chip_info->st80ytestdata.test_ok++;
			ret = ERROR_PROD_TEST_DATA | ERROR_TEST_CHECK_FAIL;
			goto ERROR;
		} else
			TPD_INFO("SS RAW FORCE MAP MIN MAX TEST:.................OK\n\n");
	} else
		TPD_INFO("SS RAW FORCE MAP MIN MAX TEST:.................SKIPPED\n\n");

	TPD_INFO("SS RAW SENSE MAP MIN MAX TEST:\n");

	if (chip_info->st80ytestdata.selfsenserawmap == 1) {
		ret = st80yCheckLimitsMapTotal(s, ssRawFrame.sense_data, 1,
					       ssRawFrame.header.sense_node, self_rx_min_limits, self_rx_max_limits);

		if (ret != OK) {
			chip_info->st80ytestdata.test_ok++;
			TPD_INFO("production_test_data: checkLimitsMinMax SS RAW SENSE MAP failed... ERROR COUNT = %d\n",
				 ret);
			TPD_INFO("SS RAW SENSE MAP MIN MAX TEST:.................FAIL\n\n");
			seq_printf(s, "SS RAW SENSE MAP MIN MAX TEST FAIL\n");
			ret = ERROR_PROD_TEST_DATA | ERROR_TEST_CHECK_FAIL;
			goto ERROR;
		} else
			TPD_INFO("SS RAW SENSE MAP MIN MAX TEST:.................OK\n\n");
	} else
		TPD_INFO("SS RAW SENSE MAP MIN MAX TEST:.................SKIPPED\n\n");

ERROR:

	if (ssRawFrame.sense_data)
		kfree(ssRawFrame.sense_data);

	ssRawFrame.sense_data = NULL;

	if (ssRawFrame.force_data)
		kfree(ssRawFrame.force_data);

	ssRawFrame.force_data = NULL;
	return ret;
}

/**
  * Perform all the tests selected in a TestTodo variable related to SS Init
  * data (touch, keys etc..)
  * @param path_limits name of Production Limit file to load or
  * "NULL" if the limits data should be loaded by a .h file
  * @param stop_on_fail if 1, the test flow stops at the first data check
  * failure
  * otherwise it keeps going performing all the selected test
  * @param todo pointer to a TestToDo variable which select the test to do
  * @return OK if success or an error code which specify the type of error
  */

static int st80y_production_test_ss_ix_cx(struct seq_file *s, void *chip_data,
		struct auto_testdata *st_testdata,
		struct test_item_info *p_test_item_info)
{
	int ret = 0;
	SelfSenseData ssCompData;
	TotSelfSenseData totCompData;
	int32_t *activeIxMaxLimits = NULL;
	int32_t *activeIxMinLimits = NULL;
	int32_t *rxActiveIxMaxLimits = NULL;
	int32_t *rxActiveIxMinLimits = NULL;
	uint8_t  data_buf[64];
	int i = 0;

	struct fts_ts_info *chip_info = (struct fts_ts_info *)chip_data;

	ssCompData.ix2_fm = NULL;
	ssCompData.ix2_sn = NULL;
	ssCompData.cx2_fm = NULL;
	ssCompData.cx2_sn = NULL;

	totCompData.ix_fm = NULL;
	totCompData.ix_sn = NULL;
	totCompData.cx_fm = NULL;
	totCompData.cx_sn = NULL;

	if (p_test_item_info->para_num) {
		if (p_test_item_info->p_buffer[0]) {
			chip_info->st80ytestdata.selfforceixtotal = p_test_item_info->p_buffer[0];
			chip_info->st80ytestdata.selfsenseixtotal = p_test_item_info->p_buffer[0];

			if (p_test_item_info->item_limit_type == LIMIT_TYPE_SLEF_TX_RX_DATA) {
				TPD_INFO("get IMIT_TYPE_SLEFRAW_DATA data\n");
				/*SS_TOUCH_ACTIVE_TOTAL_IX_FORCE_MAX*/
				/*SS_TOUCH_ACTIVE_TOTAL_IX_FORCE_MIN*/
				activeIxMaxLimits = (int32_t *)(st_testdata->fw->data +
								p_test_item_info->top_limit_offset);
				/*SS_TOUCH_ACTIVE_TOTAL_IX_SENSE_MIN*/
				/*SS_TOUCH_ACTIVE_TOTAL_IX_SENSE_MAX*/
				rxActiveIxMaxLimits = (int32_t *)(st_testdata->fw->data +
								  p_test_item_info->top_limit_offset) + st_testdata->tx_num;
				activeIxMinLimits = (int32_t *)(st_testdata->fw->data +
								p_test_item_info->floor_limit_offset);
				rxActiveIxMinLimits = (int32_t *)(st_testdata->fw->data +
								  p_test_item_info->floor_limit_offset) + st_testdata->tx_num;

			} else {
				chip_info->st80ytestdata.test_ok++;
				TPD_INFO("item: %d get_test_item_info fail\n", LIMIT_TYPE_SLEF_TX_RX_DATA);
				return ret | ERROR_PROD_TEST_DATA;
			}

		} else {
			chip_info->st80ytestdata.test_ok++;
			TPD_INFO("TYPE_SPECIAL_SELFRAW_MAX_MIN para_num is invalid\n");
			return ret | ERROR_PROD_TEST_DATA;
		}
	}

	TPD_INFO("SS IX CX testes are starting...\n");
	ret = readSelfSenseCompensationData(chip_data, LOAD_CX_SS_TOUCH, &ssCompData);

	/* read the SS compensation data */
	if (ret < 0) {
		chip_info->st80ytestdata.test_ok++;
		TPD_INFO("production_test_data: readSelfSenseCompensationData failed... ERROR %08X\n",
			 ERROR_PROD_TEST_DATA);
		return ret | ERROR_PROD_TEST_DATA;
	}

	ret = readTotSelfSenseCompensationData(chip_data, LOAD_PANEL_CX_TOT_SS_TOUCH,
					       &totCompData);

	/* read the TOT SS compensation data */
	if (ret < 0) {
		TPD_INFO("production_test_data: readTotSelfSenseCompensationData failed... ERROR %08X\n",
			 ERROR_PROD_TEST_DATA);
		kfree(ssCompData.ix2_fm);
		kfree(ssCompData.ix2_sn);
		kfree(ssCompData.cx2_fm);
		kfree(ssCompData.cx2_sn);
		chip_info->st80ytestdata.test_ok++;
		return ret | ERROR_PROD_TEST_DATA;
	}

	TPD_INFO("SS IX CX testes save data:\n");
	memset(data_buf, 0, sizeof(data_buf));

	if (totCompData.ix_fm) {
		if (!IS_ERR_OR_NULL(st_testdata->fp)) {
			snprintf(data_buf, 64, "\n%s\n", "[SS IX Force data]");
			tp_test_write(st_testdata->fp, st_testdata->length,
				      data_buf, strlen(data_buf), st_testdata->pos);
		}

		for (i = 0; i < totCompData.header.force_node; i++) {
			if (!IS_ERR_OR_NULL(st_testdata->fp)) {
				snprintf(data_buf, 64, "%d,", totCompData.ix_fm[i]);
				tp_test_write(st_testdata->fp, st_testdata->length, data_buf, strlen(data_buf),
					      st_testdata->pos);
			}
		}
	}

	if (totCompData.ix_sn) {
		if (!IS_ERR_OR_NULL(st_testdata->fp)) {
			snprintf(data_buf, 64, "\n%s\n", "[SS IX Sense data]");
			tp_test_write(st_testdata->fp, st_testdata->length,
				      data_buf, strlen(data_buf), st_testdata->pos);
		}

		for (i = 0; i < totCompData.header.sense_node; i++) {
			if (!IS_ERR_OR_NULL(st_testdata->fp)) {
				snprintf(data_buf, 64, "%d,", totCompData.ix_sn[i]);
				tp_test_write(st_testdata->fp, st_testdata->length, data_buf, strlen(data_buf),
					      st_testdata->pos);
			}
		}
	}

	/************* SS FORCE IX **************/
	/* SS TOTAL FORCE IX */
	TPD_INFO("SS TOTAL IX FORCE MIN MAX TEST:\n");

	if (chip_info->st80ytestdata.selfforceixtotal == 1) {
		ret = checkLimitsMapTotalFromU(totCompData.ix_fm, totCompData.header.force_node,
					       1, activeIxMinLimits, activeIxMaxLimits);/* check the values with thresholds */

		if (ret != OK) {
			TPD_INFO("production_test_data: checkLimitsMap  SS TOTAL IX FORCE failed... ERROR COUNT = %d\n",
				 ret);
			TPD_INFO("SS TOTAL IX FORCE MIN MAX TEST:.................FAIL\n\n");
			seq_printf(s, "SS TOTAL IX FORCE MIN MAX TEST FAIL\n");
			chip_info->st80ytestdata.test_ok++;
			goto ERROR;
		} else
			TPD_INFO("SS TOTAL IX FORCE MIN MAX TEST:.................OK\n\n");
	} else
		TPD_INFO("SS TOTAL IX FORCE MIN MAX TEST:.................SKIPPED\n");

	/* SS TOTAL IX SENSE */
	TPD_INFO("SS TOTAL IX SENSE MIN MAX TEST:\n");

	if (chip_info->st80ytestdata.selfsenseixtotal == 1) {
		ret = checkLimitsMapTotalFromU(totCompData.ix_sn, 1,
					       totCompData.header.sense_node, rxActiveIxMinLimits,
					       rxActiveIxMaxLimits);/* check the values with thresholds */

		if (ret != OK) {
			TPD_INFO("production_test_data: checkLimitsMap SS TOTAL IX SENSE failed... ERROR COUNT = %d\n",
				 ret);
			TPD_INFO("SS TOTAL IX SENSE MIN MAX TEST:.................FAIL\n\n");
			seq_printf(s, "SS TOTAL IX SENSE MIN MAX TEST FAIL\n");
			chip_info->st80ytestdata.test_ok++;
			goto ERROR;
		} else
			TPD_INFO("SS TOTAL IX SENSE MIN MAX TEST:.................OK\n\n");
	} else
		TPD_INFO("SS TOTAL IX SENSE MIN MAX TEST:.................SKIPPED\n");

ERROR:

	if (ssCompData.ix2_fm)
		kfree(ssCompData.ix2_fm);

	ssCompData.ix2_fm = NULL;

	if (ssCompData.ix2_sn)
		kfree(ssCompData.ix2_sn);

	ssCompData.ix2_sn = NULL;

	if (ssCompData.cx2_fm)
		kfree(ssCompData.cx2_fm);

	ssCompData.cx2_fm = NULL;

	if (ssCompData.cx2_sn)
		kfree(ssCompData.cx2_sn);

	ssCompData.cx2_sn = NULL;
	return ret;
}

static int st80y_auto_test_endoperation(struct seq_file *s, void *chip_data,
					struct auto_testdata *st_testdata,
					struct test_item_info *p_test_item_info)
{
	/*int ret = 0;*/
	int res = 0;

	return res;
}

/* st common proc ops */
static struct st_auto_test_operations st_test_ops = {
	.auto_test_preoperation = st80y_production_test_init,
	.test1 = st80y_production_test_ito,
	.test2 = st80y_production_test_ito_adj,
	.test3 = st80y_production_test_ms_raw,
	.test4 = st80y_production_test_ms_cx_lp,
	.test5 = st80y_production_test_ms_cx_lp_adj,
	.test6 = st80y_production_test_ss_raw,
	.test7 = st80y_production_test_ss_ix_cx,
	.auto_test_endoperation = st80y_auto_test_endoperation,
};

static struct engineer_test_operations st_engineer_test_ops = {
	.auto_test                  = st_auto_test,
};

static int st80y_probe(struct i2c_client *client,
		       const struct i2c_device_id *idp)
{
	struct fts_ts_info *info = NULL;
	struct touchpanel_data *ts = NULL;
	int ret = 0;

	TPD_INFO("%s : driver probe begin!\n", __func__);

	/* step1:Alloc chip_info */
	info = kzalloc(sizeof(struct fts_ts_info), GFP_KERNEL);

	if (!info) {
		TPD_INFO("%s : Out of memory... Impossible to allocate struct info!\n",
			 __func__);
		ret = -ENOMEM;
		goto Alloc_chip_info_fail;
	}

	memset(info, 0, sizeof(*info));

	/* step2:Alloc common ts */
	ts = kzalloc(sizeof(struct touchpanel_data), GFP_KERNEL);

	if (!ts) {
		TPD_INFO("%s : Out of memory... Impossible to allocate struct common ts!\n",
			 __func__);
		ret = -ENOMEM;
		goto Alloc_common_ts_fail;
	}

	memset(ts, 0, sizeof(*ts));

	/* step3:building client && dev for callback building */
	info->client = client;
	ts->debug_info_ops = &debug_info_proc_ops;
	ts->aging_test_ops = &aging_test_proc_ops;
	info->dev = &info->client->dev;
	ts->client = client;
	ts->irq = client->irq;
	i2c_set_clientdata(client, ts);
	ts->dev = &client->dev;
	ts->chip_data = info;
	info->hw_res = &ts->hw_res;

	/* step4:file_operations callback building */
	ts->ts_ops = &st80y_ops;
	ts->engineer_ops = &st_engineer_test_ops;
	ts->com_test_data.chip_test_ops = &st_test_ops;

	/* step5:register common touch */
	ret = register_common_touch_device(ts);

	if (ret < 0)
		goto register_common_touch_fail;

	info->tp_index = ts->tp_index;
	info->in_suspend = &ts->is_suspended;
	info->is_usb_checked = &ts->is_usb_checked;
	info->auto_test_need_cal_support = of_property_read_bool(ts->dev->of_node,
					   "auto_test_need_cal_support");
	/*get ts tp_index to chip info*/
	info->tp_index = ts->tp_index;
	/* /< count the number of call to * disable_irq, start with 1 because at* the boot IRQ are already disabled */
	info->disable_irq_count = 1;
	info->monitor_data = &ts->monitor_data;
	/* step6:create st common related proc files */
	/*ret = st_create_proc(ts, info->st_ops);
	if (ret < OK)
		TPD_INFO("%s : Error: can not create st related proc file!\n", __func__);*/

	/* step7:create fts related proc files */
	ret = fts_proc_init(ts);

	if (ret < OK)
		TPD_INFO("%s : Error: can not create fts related proc file!\n", __func__);

	info->bus_ops.fts_read = fts_read;
	info->bus_ops.fts_writeRead = fts_writeRead;
	info->bus_ops.fts_write = fts_write;
	info->bus_ops.fts_writeFwCmd = fts_writeFwCmd;
	info->bus_ops.fts_writeThenWriteRead = fts_writeThenWriteRead;
	info->bus_ops.fts_writeU8UX = fts_writeU8UX;
	info->bus_ops.fts_writeReadU8UX = fts_writeReadU8UX;
	info->bus_ops.fts_writeU8UXthenWriteU8UX = fts_writeU8UXthenWriteU8UX;
	info->bus_ops.fts_writeU8UXthenWriteReadU8UX = fts_writeU8UXthenWriteReadU8UX;
	info->bus_ops.fts_disableInterrupt = fts_disableInterrupt;
	info->bus_ops.fts_enableInterrupt = fts_enableInterrupt;
#ifdef CONFIG_OPLUS_TP_APK
	fts_init_oplus_apk_op(ts);
#endif /* end of CONFIG_OPLUS_TP_APK */

	/* step8:others */

	info->key_mask = 0;


	/* probe success */
	TPD_INFO("%s : Probe Finished!\n", __func__);
	return OK;


register_common_touch_fail:
	i2c_set_clientdata(client, NULL);
	common_touch_data_free(ts);
	ts = NULL;

Alloc_common_ts_fail:

	if (info) {
		kfree(info);
		info = NULL;
	}

Alloc_chip_info_fail:
	TPD_INFO("%s : Probe Failed!\n", __func__);

	return ret;
}

static int st80y_remove(struct i2c_client *client)
{
	struct touchpanel_data *ts = dev_get_drvdata(&(client->dev));

	/* free all */
	if (!ts) {
		/* proc file stuff */
		fts_proc_remove(ts);
		/*st_remove_proc(ts);*/
		common_touch_data_free(ts);
	}

	return OK;
}

static struct of_device_id st80y_of_match_table[] = {
	{
		.compatible = "st,st80y",
	},
	{},
};

static int st80y_i2c_suspend(struct device *dev)
{
	struct touchpanel_data *ts = dev_get_drvdata(dev);

	TPD_INFO("%s: is called gesture_enable =%d\n", __func__, ts->gesture_enable);
	tp_pm_suspend(ts);

	return 0;
}

static int st80y_i2c_resume(struct device *dev)
{
	struct touchpanel_data *ts = dev_get_drvdata(dev);

	TPD_INFO("%s: is called\n", __func__);
	tp_pm_resume(ts);

	return 0;
}

static void st80y_tp_shutdown(struct i2c_client *client)
{
	struct touchpanel_data *ts = i2c_get_clientdata(client);

	TPD_INFO("%s is called\n", __func__);
	tp_shutdown(ts);
}

static const struct i2c_device_id st80y_device_id[] = {
	{ FTS_TS_DRV_NAME, 0 },
	{}
};

static const struct dev_pm_ops tp_pm_ops = {
	.suspend = st80y_i2c_suspend,
	.resume  = st80y_i2c_resume,
};

static struct i2c_driver st80y_i2c_driver = {
	.driver          = {
		.name           = FTS_TS_DRV_NAME,
		.of_match_table = st80y_of_match_table,
		.pm = &tp_pm_ops,
	},
	.probe           = st80y_probe,
	.remove          = st80y_remove,
	.id_table        = st80y_device_id,
	.shutdown        = st80y_tp_shutdown,
};

static int __init st80y_driver_init(void)
{
	TPD_INFO("%s is called\n", __func__);

	if (!tp_judge_ic_match(TPD_DEVICE))
		return -1;

	return i2c_add_driver(&st80y_i2c_driver);
}

static void __exit st80y_driver_exit(void)
{
	i2c_del_driver(&st80y_i2c_driver);
}


#ifdef CONFIG_TOUCHPANEL_LATE_INIT
late_initcall(st80y_driver_init);
#else
module_init(st80y_driver_init);
#endif

module_exit(st80y_driver_exit);

MODULE_DESCRIPTION("STMicroelectronics MultiTouch IC Driver");
MODULE_LICENSE("GPL");
