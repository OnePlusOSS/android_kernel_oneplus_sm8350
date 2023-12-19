// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */
#define WARP_ASIC_OP10

#include <linux/kernel.h>
#include <linux/uaccess.h>
#include <linux/proc_fs.h>
#include <linux/delay.h>

#ifdef CONFIG_OPLUS_CHARGER_MTK
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/miscdevice.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/kobject.h>
#include <linux/platform_device.h>
#include <asm/atomic.h>

//#include <linux/xlog.h>
//#include <upmu_common.h>
//#include <mt-plat/mtk_gpio.h>
#include <linux/dma-mapping.h>

//#include <mt-plat/battery_meter.h>
#include <linux/module.h>
#ifdef CONFIG_OPLUS_CHG_OOS
#include <linux/oem/project_info.h>
#else
//#include <soc/oplus/device_info.h>
#endif

#else
#include <linux/i2c.h>
#include <linux/debugfs.h>
#include <linux/gpio.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/power_supply.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/bitops.h>
#include <linux/mutex.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#ifdef CONFIG_OPLUS_CHG_OOS
#include <linux/oem/project_info.h>
#else
//#include <soc/oplus/device_info.h>
#endif
#endif
#include "oplus_warp_fw.h"

extern int charger_abnormal_log;
#ifdef OPLUS_CHG_OP_DEF
extern struct oplus_warp_chip *g_warp_chip;
#endif

static int op10_get_fw_old_version(struct oplus_warp_chip *chip, u8 version_info[]);

#ifdef CONFIG_OPLUS_CHARGER_MTK
#define I2C_MASK_FLAG	(0x00ff)
#define I2C_ENEXT_FLAG	(0x0200)
#define I2C_DMA_FLAG	(0xdead2000)
#endif

#define GTP_DMA_MAX_TRANSACTION_LENGTH	255 /* for DMA mode */

#define ERASE_COUNT			959 /*0x0000-0x3BFF*/

#define BYTE_OFFSET			2
#define BYTES_TO_WRITE		16
#define FW_CHECK_FAIL		0
#define FW_CHECK_SUCCESS	1

#define POLYNOMIAL				0x04C11DB7
#define INITIAL_REMAINDER		0xFFFFFFFF
#define FINAL_XOR_VALUE		0xFFFFFFFF

#define WIDTH		(8 * sizeof(u32))
#define TOPBIT		(1U << (WIDTH - 1))
#define REFLECT_DATA(X)			(X)
#define REFLECT_REMAINDER(X)	(X)

#define CMD_SET_ADDR			0x01
#define CMD_XFER_W_DAT		0x02
#define CMD_XFER_R_DATA		0x03
#define CMD_PRG_START			0x05
#define CMD_USER_BOOT			0x06
#define CMD_CHIP_ERASE			0x07
#define CMD_GET_VERSION			0x08
#define CMD_GET_CRC32			0x09
#define CMD_SET_CKSM_LEN		0x0A
#define CMD_DEV_STATUS			0x0B

#define I2C_RW_LEN_MAX			32
#define ONE_WRITE_LEN_MAX		256
#define FW_VERSION_LEN			11

static struct oplus_warp_chip *the_chip = NULL;
struct wakeup_source *op10_update_wake_lock = NULL;

#ifdef CONFIG_OPLUS_CHARGER_MTK
#define GTP_SUPPORT_I2C_DMA		0
#define I2C_MASTER_CLOCK			300

DEFINE_MUTEX(dma_wr_access_op10);

static char gpDMABuf_pa[GTP_DMA_MAX_TRANSACTION_LENGTH] = {0};

#if GTP_SUPPORT_I2C_DMA
static int i2c_dma_write(struct i2c_client *client, u8 addr, s32 len, u8 *txbuf);
static int i2c_dma_read(struct i2c_client *client, u8 addr, s32 len, u8 *txbuf);
static u8 *gpDMABuf_va = NULL;
static dma_addr_t gpDMABuf_pa = 0;
#endif

#if GTP_SUPPORT_I2C_DMA
static int i2c_dma_read(struct i2c_client *client, u8 addr, s32 len, u8 *rxbuf)
{
	int ret;
	s32 retry = 0;
	u8 buffer[1];

	struct i2c_msg msg[2] = {
		{
			.addr = (client->addr & I2C_MASK_FLAG),
			.flags = 0,
			.buf = buffer,
			.len = 1,
			.timing = I2C_MASTER_CLOCK
		},
		{
			.addr = (client->addr & I2C_MASK_FLAG),
			.ext_flag = (client->ext_flag | I2C_ENEXT_FLAG | I2C_DMA_FLAG),
			.flags = I2C_M_RD,
			.buf = (__u8 *)gpDMABuf_pa, /*modified by PengNan*/
			.len = len,
			.timing = I2C_MASTER_CLOCK
		},
	};

	mutex_lock(&dma_wr_access_op10);
	/*buffer[0] = (addr >> 8) & 0xFF;*/
	buffer[0] = addr & 0xFF;
	if (rxbuf == NULL) {
		mutex_unlock(&dma_wr_access_op10);
		return -1;
	}
	/*chg_err("warp dma i2c read: 0x%x, %d bytes(s)\n", addr, len);*/
	for (retry = 0; retry < 5; ++retry) {
		ret = i2c_transfer(client->adapter, &msg[0], 2);
		if (ret < 0) {
			continue;
		}
		memcpy(rxbuf, gpDMABuf_va, len);
		mutex_unlock(&dma_wr_access_op10);
		return 0;
	}
	/*chg_err(" Error: 0x%04X, %d byte(s), err-code: %d\n", addr, len, ret);*/
	mutex_unlock(&dma_wr_access_op10);
	return ret;
}

static int i2c_dma_write(struct i2c_client *client, u8 addr, s32 len, u8 const *txbuf)
{
	int ret = 0;
	s32 retry = 0;
	u8 *wr_buf = gpDMABuf_va;
	struct i2c_msg msg = {
		.addr = (client->addr & I2C_MASK_FLAG),
		.ext_flag = (client->ext_flag | I2C_ENEXT_FLAG | I2C_DMA_FLAG),
		.flags = 0,
		.buf = (__u8 *)gpDMABuf_pa, /*modified by PengNan*/
		.len = 1 + len,
		.timing = I2C_MASTER_CLOCK
	};

	mutex_lock(&dma_wr_access_op10);
	wr_buf[0] = (u8)(addr & 0xFF);
	if (txbuf == NULL) {
		mutex_unlock(&dma_wr_access_op10);
		return -1;
	}
	memcpy(wr_buf + 1, txbuf, len);
	for (retry = 0; retry < 5; ++retry) {
		ret = i2c_transfer(client->adapter, &msg, 1);
		if (ret < 0) {
			continue;
		}
		mutex_unlock(&dma_wr_access_op10);
		return 0;
	}
	/*chg_err(" Error: 0x%04X, %d byte(s), err-code: %d\n", addr, len, ret);*/
	mutex_unlock(&dma_wr_access_op10);
	return ret;
}
#endif /*GTP_SUPPORT_I2C_DMA*/
#endif /*CONFIG_OPLUS_CHARGER_MTK*/

static int oplus_warp_i2c_read(struct i2c_client *client, u8 addr, s32 len, u8 *rxbuf)
{
#ifdef CONFIG_OPLUS_CHARGER_MTK
#if GTP_SUPPORT_I2C_DMA
	return i2c_dma_read(client, addr, len, rxbuf);
#else
	return i2c_smbus_read_i2c_block_data(client, addr, len, rxbuf);
#endif
#else
	return i2c_smbus_read_i2c_block_data(client, addr, len, rxbuf);
#endif
}

static int oplus_warp_i2c_write(struct i2c_client *client, u8 addr, s32 len, u8 const *txbuf)
{
#ifdef CONFIG_OPLUS_CHARGER_MTK
#if GTP_SUPPORT_I2C_DMA
	return i2c_dma_write(client, addr, len, txbuf);
#else
	return i2c_smbus_write_i2c_block_data(client, addr, len, txbuf);
#endif
#else
	return i2c_smbus_write_i2c_block_data(client, addr, len, txbuf);
#endif
}

static int check_flash_idle(struct oplus_warp_chip *chip, u32 try_count)
{
	u8 rx_buf;
	int rc = 0;

	do {
		rx_buf = 0xff;
		rc = oplus_warp_i2c_read(chip->client, CMD_DEV_STATUS,1, &rx_buf);
		if (rc < 0) {
			chg_debug("read CMD_DEV_STATUS error:%0x\n", rx_buf);
			goto i2c_err;
		}
		//chg_debug("the rx_buf=%0x\n", rx_buf);
		if ((rx_buf & 0x01) == 0x0) {// check OP10 flash is idle
			return 0;
		}
		try_count--;
		msleep(20);
	} while (try_count);

i2c_err:
	return -1;
}

static int check_crc32_available(struct oplus_warp_chip *chip, u32 try_count)
{
	u8 rx_buf;
	int rc = 0;

	do {
		rx_buf = 0x0;
		rc = oplus_warp_i2c_read(chip->client, CMD_DEV_STATUS, 1, &rx_buf);
		if (rc < 0) {
			chg_debug("read CMD_DEV_STATUS error:%0x\n", rx_buf);
			goto i2c_err;
		}
		//chg_debug("the rx_buf=%0x\n", rx_buf);
		if ((rx_buf & 0x02) == 0x2) {
			return 0;
		}
		try_count--;
		msleep(20);
	} while (try_count);

i2c_err:
	return -1;
}

u32 crc32_sram(struct oplus_warp_chip *chip)
{
	u32 remainder = INITIAL_REMAINDER;
	u32 byte;
	u8 bit;

	/* Perform modulo-2 division, a byte at a time. */
	for (byte = 0; byte < chip->fw_data_count; ++byte) {
		/* Bring the next byte into the remainder. */
		remainder ^= (REFLECT_DATA(chip->firmware_data[byte]) << (WIDTH - 8));

		/* Perform modulo-2 division, a bit at a time.*/
		for (bit = 8; bit > 0; --bit) {
			/* Try to divide the current data bit. */
			if (remainder & TOPBIT) {
				remainder = (remainder << 1) ^ POLYNOMIAL;
			} else {
				remainder = (remainder << 1);
			}
		}
	}
	/* The final remainder is the CRC result. */
	return (REFLECT_REMAINDER(remainder) ^ FINAL_XOR_VALUE);
}

static bool op10_fw_update_check(struct oplus_warp_chip *chip)
{
	int i = 0;
	int ret = 0;
	u8 fw_version[FW_VERSION_LEN] = {0};
	u8 rx_buf[4] = {0};
	u32 check_status_try_count = 100;
	u32 fw_status_address = 0x4000 - 0x10;
	u32 new_fw_crc32 = 0;

	ret = op10_get_fw_old_version(chip, fw_version);
	if (ret == 1)
		return false;

	/* chip version */
	ret = oplus_warp_i2c_read(chip->client, CMD_GET_VERSION, 1, rx_buf);
	if (ret < 0) {
		chg_err("read CMD_GET_VERSION error:%d\n", ret);
	} else {
		switch (rx_buf[0]) {
		case 0x01:
		case 0x02:
			chg_debug("chip is sy6610:0x%02x\n", rx_buf[0]);
			break;
		case 0x11:
			chg_debug("chip is sy6610c:0x%02x\n", rx_buf[0]);
			break;
		default:
			chg_debug("invalid chip version:0x%02x\n", rx_buf[0]);
		}
	}

	/*read fw status*/
	rx_buf[0] = fw_status_address & 0xFF;
	rx_buf[1] = (fw_status_address >> 8) & 0xFF;
	oplus_warp_i2c_write(chip->client, CMD_SET_ADDR, 2, rx_buf);
	msleep(1);
	memset(rx_buf, 0, 4);
	oplus_warp_i2c_read(chip->client, CMD_XFER_R_DATA, 4, rx_buf);
	chg_debug("fw crc32 status:0x%08x\n", *((u32 *)rx_buf));

	chip->fw_mcu_version = fw_version[FW_VERSION_LEN-4];

	for (i = 0; i < FW_VERSION_LEN; i++) {
		chg_debug("the old version: %0x, the fw version: %0x\n", fw_version[i], chip->firmware_data[chip->fw_data_count - FW_VERSION_LEN + i]);
		if (fw_version[i] != chip->firmware_data[chip->fw_data_count - FW_VERSION_LEN + i])
			return false;
	}

	/*noticefy OP10 to update the CRC32 and check it*/
	*((u32 *)rx_buf) = chip->fw_data_count;
	oplus_warp_i2c_write(chip->client, CMD_SET_CKSM_LEN, 4, rx_buf);
	msleep(5);
	if (check_crc32_available(chip, check_status_try_count) == -1) {
		chg_debug("crc32 is not available, timeout!\n");
		return false;
	}

	/* check crc32 is correct */
	memset(rx_buf, 0, 4);
	oplus_warp_i2c_read(chip->client, CMD_GET_CRC32, 4, rx_buf);
	new_fw_crc32 = crc32_sram(chip);
	chg_debug("fw_data_crc:0x%0x, the read data_crc32:0x%0x\n", new_fw_crc32, *((u32 *)rx_buf));
	if (*((u32 *)rx_buf) != new_fw_crc32) {
		chg_debug("crc32 compare fail!\n");
		return false;
	}

	/* fw update success,jump to new fw */
	/*oplus_warp_i2c_read(chip->client, CMD_USER_BOOT, 1, rx_buf);*/

	return true;
}

#ifdef OPLUS_CHG_DEBUG
static int op10_fw_update_by_buf(struct oplus_warp_chip *chip, u8 *fw_buf, u32 fw_size)
{
	u32 check_status_try_count = 100;//try 2s
	u32 write_done_try_count = 500;//max try 10s
	u8 rx_buf[4] = {0};
	u32 fw_len = 0, fw_offset = 0;
	u32 write_len = 0, write_len_temp = 0, chunk_index = 0, chunk_len = 0;
	u32 new_fw_crc32 = 0;
	int rc = 0;

	chg_debug("start op_fw_update now, fw length is: %d\n", fw_size);
	/* chip erase */
	rc = oplus_warp_i2c_read(chip->client, CMD_CHIP_ERASE, 1, rx_buf);
	if (rc < 0) {
		chg_debug("read CMD_CHIP_ERASE error:%d\n", rc);
		goto update_fw_err;
	}
	msleep(100);

	/* check device status */
	if (check_flash_idle(chip, check_status_try_count) == -1) {
		chg_debug("device is always busy, timeout!\n");
		goto update_fw_err;
	}

	/* start write the fw array */
	fw_len = fw_size;
	fw_offset = 0;
	while (fw_len) {
		write_len = (fw_len < ONE_WRITE_LEN_MAX) ? fw_len : ONE_WRITE_LEN_MAX;

		/* set flash start address */
		*((u32 *)rx_buf) = fw_offset;
		oplus_warp_i2c_write(chip->client, CMD_SET_ADDR, 2, rx_buf);
		msleep(1);

		/* send data which will be written in future */
		chunk_index = 0;
		write_len_temp = write_len;
		while (write_len_temp) {
			chunk_len = (write_len_temp < I2C_RW_LEN_MAX) ? write_len_temp : I2C_RW_LEN_MAX;
			oplus_warp_i2c_write(chip->client, CMD_XFER_W_DAT, chunk_len, fw_buf + fw_offset + chunk_index * I2C_RW_LEN_MAX);
			msleep(1);

			write_len_temp -= chunk_len;
			chunk_index++;
		}
		oplus_warp_i2c_read(chip->client, CMD_PRG_START, 1, rx_buf);
		msleep(5);
		if (check_flash_idle(chip, write_done_try_count) == -1) {
			chg_debug("cannot wait flash write done, timeout!\n");
			goto update_fw_err;
		}

		//chg_debug("current write address: %d,to bw write length:%d\n", fw_offset, write_len);
		fw_offset += write_len;
		fw_len -= write_len;
	}

	/*noticefy OP10 to update the CRC32 and check it*/
	*((u32 *)rx_buf) = fw_size;
	oplus_warp_i2c_write(chip->client, CMD_SET_CKSM_LEN, 4, rx_buf);
	msleep(5);
	if (check_crc32_available(chip, check_status_try_count) == -1) {
		chg_debug("crc32 is not available after flash write done, timeout!\n");
		goto update_fw_err;
	}

	/* check crc32 is correct */
	memset(rx_buf, 0, 4);
	oplus_warp_i2c_read(chip->client, CMD_GET_CRC32, 4, rx_buf);
	new_fw_crc32 = crc32_sram(chip);
	if (*((u32 *)rx_buf) != new_fw_crc32) {
		chg_debug("fw_data_crc:%0x, the read data_crc32: %0x\n", new_fw_crc32, *((u32 *)rx_buf));
		chg_debug("crc32 compare fail!\n");
		goto update_fw_err;
	}

	/* fw update success,jump to new fw */
	oplus_warp_i2c_read(chip->client, CMD_USER_BOOT, 1, rx_buf);

	chip->fw_mcu_version = fw_buf[fw_size - 4];
	chg_debug("success!\n");
	return 0;

update_fw_err:
	charger_abnormal_log = CRITICAL_LOG_WARP_FW_UPDATE_ERR;
	chg_err("fail\n");
	return 1;
}
#endif /* OPLUS_CHG_DEBUG */

static int op10_fw_update(struct oplus_warp_chip *chip)
{
	u32 check_status_try_count = 100;//try 2s
	u32 write_done_try_count = 500;//max try 10s
	u8 rx_buf[4] = {0};
	u32 fw_len = 0, fw_offset = 0;
	u32 write_len = 0, write_len_temp = 0, chunk_index = 0, chunk_len = 0;
	u32 new_fw_crc32 = 0;
	int rc = 0;

	chg_debug("start op_fw_update now, fw length is: %d\n", chip->fw_data_count);

	/* chip erase */
	rc = oplus_warp_i2c_read(chip->client, CMD_CHIP_ERASE, 1, rx_buf);
	if (rc < 0) {
		chg_debug("read CMD_CHIP_ERASE error:%d\n", rc);
		goto update_fw_err;
	}
	msleep(100);

	/* check device status */
	if (check_flash_idle(chip, check_status_try_count) == -1) {
		chg_debug("device is always busy, timeout!\n");
		goto update_fw_err;
	}

	/* start write the fw array */
	fw_len = chip->fw_data_count;
	fw_offset = 0;
	while (fw_len) {
		write_len = (fw_len < ONE_WRITE_LEN_MAX) ? fw_len : ONE_WRITE_LEN_MAX;

		/* set flash start address */
		*((u32 *)rx_buf) = fw_offset;
		oplus_warp_i2c_write(chip->client, CMD_SET_ADDR, 2, rx_buf);
		msleep(1);

		/* send data which will be written in future */
		chunk_index = 0;
		write_len_temp = write_len;
		while (write_len_temp) {
			chunk_len = (write_len_temp < I2C_RW_LEN_MAX) ? write_len_temp : I2C_RW_LEN_MAX;
			oplus_warp_i2c_write(chip->client, CMD_XFER_W_DAT, chunk_len, chip->firmware_data + fw_offset + chunk_index * I2C_RW_LEN_MAX);
			msleep(1);

			write_len_temp -= chunk_len;
			chunk_index++;
		}
		oplus_warp_i2c_read(chip->client, CMD_PRG_START, 1, rx_buf);
		msleep(5);
		if (check_flash_idle(chip, write_done_try_count) == -1) {
			chg_debug("cannot wait flash write done, timeout!\n");
			goto update_fw_err;
		}

		//chg_debug("current write address: %d,to bw write length:%d\n", fw_offset, write_len);
		fw_offset += write_len;
		fw_len -= write_len;
	}

	/*noticefy OP10 to update the CRC32 and check it*/
	*((u32 *)rx_buf) = chip->fw_data_count;
	oplus_warp_i2c_write(chip->client, CMD_SET_CKSM_LEN, 4, rx_buf);
	msleep(5);
	if (check_crc32_available(chip, check_status_try_count) == -1) {
		chg_debug("crc32 is not available after flash write done, timeout!\n");
		goto update_fw_err;
	}

	/* check crc32 is correct */
	memset(rx_buf, 0, 4);
	oplus_warp_i2c_read(chip->client, CMD_GET_CRC32, 4, rx_buf);
	new_fw_crc32 = crc32_sram(chip);
	if (*((u32 *)rx_buf) != new_fw_crc32) {
		chg_debug("fw_data_crc:%0x, the read data_crc32: %0x\n", new_fw_crc32, *((u32 *)rx_buf));
		chg_debug("crc32 compare fail!\n");
		goto update_fw_err;
	}

	/* fw update success,jump to new fw */
	oplus_warp_i2c_read(chip->client, CMD_USER_BOOT, 1, rx_buf);

	chip->fw_mcu_version = chip->fw_data_version;
	chg_debug("success!\n");
	return 0;

update_fw_err:
	charger_abnormal_log = CRITICAL_LOG_WARP_FW_UPDATE_ERR;
	chg_err("fail\n");
	return 1;
}

static int op10_get_fw_old_version(struct oplus_warp_chip *chip, u8 version_info[])
{
	u8 rx_buf[4] = {0};//i = 0;
	u32 fw_version_address = 0;
	u32 check_status_try_count = 100;//try 2s
	u32 fw_len_address = 0x4000 - 8;

	memset(version_info, 0xFF, FW_VERSION_LEN);//clear version info at first

	if (check_flash_idle(chip, check_status_try_count) == -1) {
		chg_debug("cannot get the fw old version because of the device is always busy!\n");
		return 1;
	}

	rx_buf[0] = fw_len_address & 0xFF;
	rx_buf[1] = (fw_len_address >> 8) & 0xFF;
	oplus_warp_i2c_write(chip->client, CMD_SET_ADDR, 2, rx_buf);
	msleep(1);
	oplus_warp_i2c_read(chip->client, CMD_XFER_R_DATA, 4, rx_buf);
	if (*((u32 *)rx_buf) < fw_len_address) {
		fw_version_address = *((u32 *)rx_buf) - FW_VERSION_LEN;
		rx_buf[0] = fw_version_address & 0xFF;
		rx_buf[1] = (fw_version_address >> 8) & 0xFF;
		oplus_warp_i2c_write(chip->client, CMD_SET_ADDR, 2, rx_buf);
		msleep(1);
		oplus_warp_i2c_read(chip->client, CMD_XFER_R_DATA, FW_VERSION_LEN, version_info);
	} else {
		chg_debug("warning:fw length is invalid\n");
	}


	/* below code is used for debug log,pls comment it after this interface test pass */
	/*chg_debug("the fw old version is:\n");
	for (i = 0; i < FW_VERSION_LEN; i++) {
		chg_debug("0x%x,", version_info[i]);
	}
	chg_debug("\n");*/

	return 0;
}

static int op10_get_fw_verion_from_ic(struct oplus_warp_chip *chip)
{
	unsigned char addr_buf[2] = {0x3B, 0xF0};
	unsigned char data_buf[4] = {0};
	int rc = 0;
	int update_result = 0;

	if (oplus_is_power_off_charging(chip) == true || oplus_is_charger_reboot(chip) == true) {
		chip->mcu_update_ing = true;
		update_result = op10_fw_update(chip);
		chip->mcu_update_ing = false;
		if (update_result) {
			msleep(30);
			opchg_set_clock_sleep(chip);
			opchg_set_reset_active(chip);
		}
	} else {
		opchg_set_clock_active(chip);
		chip->mcu_boot_by_gpio = true;
		msleep(10);
		opchg_set_reset_active(chip);
		chip->mcu_update_ing = true;
		msleep(2500);
		chip->mcu_boot_by_gpio = false;
		opchg_set_clock_sleep(chip);

		//first:set address
		rc = oplus_warp_i2c_write(chip->client, 0x01, 2, &addr_buf[0]);
		if (rc < 0) {
			chg_err(" i2c_write 0x01 error\n" );
			return FW_CHECK_FAIL;
		}
		msleep(2);
		oplus_warp_i2c_read(chip->client, 0x03, 4, data_buf);
		//strcpy(ver,&data_buf[0]);
		chg_err("data:%x %x %x %x, fw_ver:%x\n", data_buf[0], data_buf[1], data_buf[2], data_buf[3], data_buf[0]);

		msleep(5);
		chip->mcu_update_ing = false;
		opchg_set_reset_active(chip);
	}
	return data_buf[0];
}

static int op10_fw_check_then_recover(struct oplus_warp_chip *chip)
{
	int update_result = 0;
	int try_count = 5;
	int ret = 0;

	if (!chip->firmware_data) {
		chg_err("op10_fw_data Null, Return\n");
		return FW_ERROR_DATA_MODE;
	} else {
		chg_debug("begin\n");
	}

	if (oplus_is_power_off_charging(chip) == true || oplus_is_charger_reboot(chip) == true) {
		chip->mcu_update_ing = false;
		opchg_set_clock_sleep(chip);
		opchg_set_reset_sleep(chip);
		ret = FW_NO_CHECK_MODE;
	} else {
		opchg_set_clock_active(chip);
		chip->mcu_boot_by_gpio = true;
		msleep(10);
		opchg_set_reset_active_force(chip);
		chip->mcu_update_ing = true;
		msleep(2500);
		chip->mcu_boot_by_gpio = false;
		opchg_set_clock_sleep(chip);
		__pm_stay_awake(op10_update_wake_lock);
		if (op10_fw_update_check(chip) == FW_CHECK_FAIL) {
			chg_debug("firmware update start\n");
			do {
				update_result = op10_fw_update(chip);
				if (!update_result)
					break;
				opchg_set_clock_active(chip);
				chip->mcu_boot_by_gpio = true;
				msleep(10);
				//chip->mcu_update_ing = false;
				opchg_set_reset_active_force(chip);
				//chip->mcu_update_ing = true;
				msleep(2500);
				chip->mcu_boot_by_gpio = false;
				opchg_set_clock_sleep(chip);
			} while ((update_result) && (--try_count > 0));
			chg_debug("firmware update end, retry times %d\n", 5 - try_count);
		} else {
			chip->warp_fw_check = true;
			chg_debug("fw check ok\n");
		}
		__pm_relax(op10_update_wake_lock);
		msleep(5);
		chip->mcu_update_ing = false;
		opchg_set_reset_active(chip);
		ret = FW_CHECK_MODE;
	}
	if (!oplus_warp_get_fastchg_allow())
		opchg_set_reset_sleep(chip);

	return ret;
}

int op10_set_battery_temperature_soc(int temp_bat, int soc_bat)
{
	int ret = 0;
	u8 read_buf[2] = { 0 };
	chg_err("kilody write op10:temp_bat=%d,soc_bat=%d\n", temp_bat, soc_bat);

	read_buf[0] = temp_bat & 0xff;
	read_buf[1] = (temp_bat >> 8) & 0xff;

	ret = oplus_warp_i2c_write(the_chip->client, (u8)0xE, 2, read_buf);
	if (ret < 0) {
			chg_err("op10 write slave ack fail");
			return -1;
		}
	return 0;
}

#ifdef OPLUS_CHG_DEBUG
static int op10_user_fw_upgrade(struct oplus_warp_chip *chip, u8 *fw_buf, u32 fw_size)
{
	int update_result = 0;
	int try_count = 5;

	if (chip == NULL) {
		pr_err("warp chip is NULL\n");
		return -ENODEV;
	}
	if (fw_buf == NULL) {
		pr_err("fw buf is NULL\n");
		return -EINVAL;
	}

	opchg_set_clock_active(chip);
	chip->mcu_boot_by_gpio = true;
	msleep(10);
	opchg_set_reset_active_force(chip);
	chip->mcu_update_ing = true;
	msleep(2500);
	chip->mcu_boot_by_gpio = false;
	opchg_set_clock_sleep(chip);
	__pm_stay_awake(op10_update_wake_lock);
	chg_debug("firmware update start\n");
	do {
		opchg_set_clock_active(chip);
		chip->mcu_boot_by_gpio = true;
		msleep(10);
		chip->mcu_update_ing = false;
		opchg_set_reset_active(chip);
		chip->mcu_update_ing = true;
		msleep(2500);
		chip->mcu_boot_by_gpio = false;
		opchg_set_clock_sleep(chip);
		update_result = op10_fw_update_by_buf(chip, fw_buf, fw_size);
		if (!update_result)
			break;
	} while ((update_result) && (--try_count > 0));
	chg_debug("firmware update end, retry times %d\n", 5 - try_count);
	__pm_relax(op10_update_wake_lock);
	chip->mcu_update_ing = false;
	msleep(5);
	opchg_set_reset_active(chip);

	if (!oplus_warp_get_fastchg_allow())
		opchg_set_reset_sleep(chip);

	return update_result;
}
#endif /* OPLUS_CHG_DEBUG */

void op10_update_temperature_soc(void)
{
	int temp = 0;
	int soc = 0;

	temp = oplus_chg_match_temp_for_chging();
	op10_set_battery_temperature_soc(temp, soc);

	chg_err("kilody in! soc = %d,temp = %d,chging = %d\n", soc, temp, oplus_warp_get_fastchg_ing());
}


struct oplus_warp_operations oplus_op10_ops = {
	.fw_update = op10_fw_update,
	.fw_check_then_recover = op10_fw_check_then_recover,
	.set_switch_mode = opchg_set_switch_mode,
	.eint_regist = oplus_warp_eint_register,
	.eint_unregist = oplus_warp_eint_unregister,
	.set_data_active = opchg_set_data_active,
	.set_data_sleep = opchg_set_data_sleep,
	.set_clock_active = opchg_set_clock_active,
	.set_clock_sleep = opchg_set_clock_sleep,
	.get_gpio_ap_data = opchg_get_gpio_ap_data,
	.read_ap_data = opchg_read_ap_data,
	.reply_mcu_data = opchg_reply_mcu_data,
	.reply_mcu_data_4bits = opchg_reply_mcu_data_4bits,
	.reset_fastchg_after_usbout = reset_fastchg_after_usbout,
	.switch_fast_chg = switch_fast_chg,
	.reset_mcu = opchg_set_reset_active,
	.set_mcu_sleep = opchg_set_reset_sleep,
	.set_warp_chargerid_switch_val = opchg_set_warp_chargerid_switch_val,
	.is_power_off_charging = oplus_is_power_off_charging,
	.get_reset_gpio_val = oplus_warp_get_reset_gpio_val,
	.get_switch_gpio_val = oplus_warp_get_switch_gpio_val,
	.get_ap_clk_gpio_val = oplus_warp_get_ap_clk_gpio_val,
	.get_fw_version = op10_get_fw_verion_from_ic,
	.get_clk_gpio_num = opchg_get_clk_gpio_num,
	.get_data_gpio_num = opchg_get_data_gpio_num,
	.update_temperature_soc = op10_update_temperature_soc,
#ifdef OPLUS_CHG_DEBUG
	.user_fw_upgrade = op10_user_fw_upgrade,
#endif
};

#ifdef CONFIG_OPLUS_CHG_OOS
static void register_warp_devinfo(struct oplus_warp_chip *chip)
{
	static char manu_name[255];
	static char fw_id[255];

	snprintf(fw_id, 255, "0x%x", chip->fw_data_version);

	snprintf(manu_name, 255, "%s", "OP10");

	push_component_info(FAST_CHARGE, fw_id, manu_name);
}
#else
static void register_warp_devinfo(void)
{
	int ret = 0;
	char *version;
	char *manufacture;

	version = "op10";
	manufacture = "SILERGY";

	ret = register_device_proc("warp", version, manufacture);
	if (ret) {
		chg_err(" fail\n");
	}
}
#endif

static void op10_shutdown(struct i2c_client *client)
{
	if (!the_chip) {
		return;
	}
	opchg_set_switch_mode(the_chip, NORMAL_CHARGER_MODE);
	msleep(10);
	if (oplus_warp_get_fastchg_started() == true) {
		opchg_set_clock_sleep(the_chip);
		msleep(10);
		opchg_set_reset_active(the_chip);
	}
	msleep(80);
	return;
}

static ssize_t warp_fw_check_read(struct file *filp, char __user *buff, size_t count, loff_t *off)
{
	char page[256] = {0};
	char read_data[32] = {0};
	int len = 0;

	if (the_chip && the_chip->warp_fw_check == true) {
		read_data[0] = '1';
	} else {
		read_data[0] = '0';
	}
	len = sprintf(page, "%s", read_data);
	if (len > *off) {
		len -= *off;
	} else {
		len = 0;
	}
	if (copy_to_user(buff, page, (len < count ? len : count))) {
		return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);
}

static const struct file_operations warp_fw_check_proc_fops = {
	.read = warp_fw_check_read,
	.llseek = noop_llseek,
};

static int init_proc_warp_fw_check(void)
{
	struct proc_dir_entry *p = NULL;

	p = proc_create("warp_fw_check", 0444, NULL, &warp_fw_check_proc_fops);
	if (!p) {
		chg_err("proc_create warp_fw_check_proc_fops fail!\n");
	}
	return 0;
}

#ifdef OPLUS_CHG_OP_DEF
static bool op10_is_used(struct i2c_client *client)
{
	struct oplus_chg_asic *asic = i2c_get_clientdata(client);
	struct oplus_warp_chip *chip = asic->data;
	u8 value = 0;
	int rc = 0;

	opchg_set_clock_active(chip);
	msleep(10);
	opchg_set_reset_active_force(chip);
	msleep(2500);
	opchg_set_clock_sleep(chip);

	rc = oplus_warp_i2c_read(chip->client, 0x08, 1, &value);
	if (rc < 0) {
		pr_err("op10 read register 0x08 fail, rc = %d\n", rc);
		return false;
	} else {
		if (value == 0x02) {
			pr_err("op10 detected, register 0x08: 0x %x\n", value);
			msleep(5);
			opchg_set_reset_active(chip);
			if (!oplus_warp_get_fastchg_allow())
				opchg_set_reset_sleep(chip);
			chip->mcu_hwid_type = OPLUS_WARP_ASIC_HWID_OP10;
			return true;
		}
	}
	msleep(5);
	opchg_set_reset_active(chip);
	if (!oplus_warp_get_fastchg_allow())
		opchg_set_reset_sleep(chip);

	return false;
}
#endif

static int op10_driver_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct oplus_warp_chip *chip;
#ifdef OPLUS_CHG_OP_DEF
	struct oplus_chg_asic *asic;
	struct device_node *node = client->dev.of_node;
	int rc;
#endif

#ifndef OPLUS_CHG_OP_DEF
	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip) {
		dev_err(&client->dev, "Couldn't allocate memory\n");
		return -ENOMEM;
	}

	chip->client = client;
	chip->dev = &client->dev;
	i2c_set_clientdata(client, chip);
#else
	if (oplus_warp_probe_done != 0) {
		chg_err("oplus_warp_probe_done = %d\n", oplus_warp_probe_done);
		return oplus_warp_probe_done;
	}
	if (g_warp_chip == NULL) {
		chg_err("warp chip is NULL\n");
		return -ENODEV;
	}
	chip = g_warp_chip;
	asic = devm_kzalloc(&client->dev, sizeof(struct oplus_chg_asic), GFP_KERNEL);
	if (!asic) {
		dev_err(&client->dev, "Couldn't allocate memory\n");
		return -ENOMEM;
	}
	asic->data = chip;
	asic->client = client;
	asic->dev = &client->dev;
	i2c_set_clientdata(client, asic);
	asic->vops = &oplus_op10_ops;
	asic->init_proc_warp_fw_check = init_proc_warp_fw_check;
	asic->register_warp_devinfo = register_warp_devinfo;
	asic->is_used = op10_is_used;
	INIT_LIST_HEAD(&asic->list);
#endif

#ifdef CONFIG_OPLUS_CHARGER_MTK
#if GTP_SUPPORT_I2C_DMA
	client->dev.coherent_dma_mask = DMA_BIT_MASK(32);
	gpDMABuf_va = (u8 *)dma_alloc_coherent(&client->dev, GTP_DMA_MAX_TRANSACTION_LENGTH, &gpDMABuf_pa, GFP_KERNEL);
	if (!gpDMABuf_va) {
		chg_err("[Error] Allocate DMA I2C Buffer failed!\n");
	} else {
		chg_debug(" ppp dma_alloc_coherent success\n");
	}
	memset(gpDMABuf_va, 0, GTP_DMA_MAX_TRANSACTION_LENGTH);
#endif
#endif

#ifndef OPLUS_CHG_OP_DEF
	if (get_warp_mcu_type(chip) != OPLUS_WARP_ASIC_HWID_OP10) {
		chg_err("It is not op10\n");
		return -ENOMEM;
	}

	chip->pcb_version = g_hw_version;
	chip->warp_fw_check = false;
	mutex_init(&chip->pinctrl_mutex);

	oplus_warp_fw_type_dt(chip);
	if (chip->batt_type_4400mv) {
		chip->firmware_data = op10_fw_data_4400_warp_ffc_15c;
		chip->fw_data_count = sizeof(op10_fw_data_4400_warp_ffc_15c);
		chip->fw_data_version = op10_fw_data_4400_warp_ffc_15c[chip->fw_data_count - 4];
	} else {//default
		chip->firmware_data = op10_fw_data_4400_warp_ffc_15c;
		chip->fw_data_count = sizeof(op10_fw_data_4400_warp_ffc_15c);
		chip->fw_data_version = op10_fw_data_4400_warp_ffc_15c[chip->fw_data_count - 4];
	}

	if (chip->warp_fw_type == WARP_FW_TYPE_OP10_4400_WARP_FFC_15C) {
		chip->firmware_data = op10_fw_data_4400_warp_ffc_15c;
		chip->fw_data_count = sizeof(op10_fw_data_4400_warp_ffc_15c);
		chip->fw_data_version = op10_fw_data_4400_warp_ffc_15c[chip->fw_data_count - 4];
	}

	switch (chip->warp_fw_type) {
	case WARP_FW_TYPE_OP10_4400_WARP_FFC_15C:
		chip->firmware_data = op10_fw_data_4400_warp_ffc_15c;
		chip->fw_data_count = sizeof(op10_fw_data_4400_warp_ffc_15c);
		chip->fw_data_version = op10_fw_data_4400_warp_ffc_15c[chip->fw_data_count - 4];
		break;
	case WARP_FW_TYPE_OP10_4450_WARP_FFC_5V4A_4BIT:
		chip->firmware_data = op10_fw_data_4450_warp_ffc_5v4a_4bit;
		chip->fw_data_count = sizeof(op10_fw_data_4450_warp_ffc_5v4a_4bit);
		chip->fw_data_version = op10_fw_data_4450_warp_ffc_5v4a_4bit[chip->fw_data_count - 4];
		break;
	case WARP_FW_TYPE_OP10_4250_WARP_FFC_10V6A_4BIT:
		chip->firmware_data = op10_fw_data_4250_warp_ffc_10v6a_4bit;
		chip->fw_data_count = sizeof(op10_fw_data_4250_warp_ffc_10v6a_4bit);
		chip->fw_data_version = op10_fw_data_4250_warp_ffc_10v6a_4bit[chip->fw_data_count - 4];
		break;
	case WARP_FW_TYPE_OP10_4450_WARP_FFC_5V6A_4BIT:
		chip->firmware_data = op10_fw_data_4450_warp_ffc_5v6a_4bit;
		chip->fw_data_count = sizeof(op10_fw_data_4450_warp_ffc_5v6a_4bit);
		chip->fw_data_version = op10_fw_data_4450_warp_ffc_5v6a_4bit[chip->fw_data_count - 4];
	default:
		break;
	}

	chip->vops = &oplus_op10_ops;
	chip->fw_mcu_version = 0;

	oplus_warp_gpio_dt_init(chip);

	opchg_set_clock_sleep(chip);
	oplus_warp_delay_reset_mcu_init(chip);
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0))
	op10_update_wake_lock = wakeup_source_register("op10_update_wake_lock");
#else
	op10_update_wake_lock = wakeup_source_register(NULL, "op10_update_wake_lock");
#endif
	if (chip->warp_fw_update_newmethod) {
		if (oplus_is_rf_ftm_mode()) {
			oplus_warp_fw_update_work_init(chip);
		}
	} else {
		oplus_warp_fw_update_work_init(chip);
	}

	oplus_warp_init(chip);

	register_warp_devinfo();

	init_proc_warp_fw_check();
#else /* OPLUS_CHG_OP_DEF */
	asic->batt_type_4400mv = of_property_read_bool(node, "qcom,oplus_batt_4400mv");

	rc = of_property_read_u32(node, "qcom,warp-fw-type", &asic->warp_fw_type);
	if (rc) {
		asic->warp_fw_type = WARP_FW_TYPE_INVALID;
	}

	chg_debug("oplus_warp_fw_type_dt batt_type_4400 is %d,warp_fw_type = 0x%x\n",
		asic->batt_type_4400mv, asic->warp_fw_type);

	rc = of_property_read_u32(node, "qcom,warp_reply_mcu_bits",
		&asic->warp_reply_mcu_bits);
	if (rc) {
		asic->warp_reply_mcu_bits = 4;
	} else {
		chg_debug("qcom,warp_reply_mcu_bits is %d\n",
			asic->warp_reply_mcu_bits);
	}

	rc = of_property_read_u32(node, "qcom,warp_cool_bat_volt", &asic->warp_cool_bat_volt);
	if (rc) {
		asic->warp_cool_bat_volt = 3450;
	} else {
		chg_debug("qcom,warp_cool_bat_volt is %d\n", asic->warp_cool_bat_volt);
	}

	rc = of_property_read_u32(node, "qcom,warp_little_cool_bat_volt", &asic->warp_little_cool_bat_volt);
	if (rc) {
		asic->warp_little_cool_bat_volt = 3400;
	} else {
		chg_debug("qcom,warp_little_cool_bat_volt is %d\n", asic->warp_little_cool_bat_volt);
	}

	rc = of_property_read_u32(node, "qcom,warp_normal_bat_volt", &asic->warp_normal_bat_volt);
	if (rc) {
		asic->warp_normal_bat_volt = 3350;
	} else {
		chg_debug("qcom,warp_normal_bat_volt is %d\n", asic->warp_normal_bat_volt);
	}

	rc = of_property_read_u32(node, "qcom,warp_warm_bat_volt", &asic->warp_warm_bat_volt);
	if (rc) {
		asic->warp_warm_bat_volt = 3300;
	} else {
		chg_debug("qcom,warp_warm_bat_volt is %d\n", asic->warp_warm_bat_volt);
	}

	rc = of_property_read_u32(node, "qcom,warp_cool_bat_suspend_volt", &asic->warp_cool_bat_suspend_volt);
	if (rc) {
		asic->warp_cool_bat_suspend_volt = 3450;
	} else {
		chg_debug("qcom,warp_cool_bat_suspend_volt is %d\n", asic->warp_cool_bat_suspend_volt);
	}

	rc = of_property_read_u32(node, "qcom,warp_little_cool_bat_suspend_volt", &asic->warp_little_cool_bat_suspend_volt);
	if (rc) {
		asic->warp_little_cool_bat_suspend_volt = 3400;
	} else {
		chg_debug("qcom,warp_little_cool_bat_suspend_volt is %d\n", asic->warp_little_cool_bat_suspend_volt);
	}

	rc = of_property_read_u32(node, "qcom,warp_normal_bat_suspend_volt", &asic->warp_normal_bat_suspend_volt);
	if (rc) {
		asic->warp_normal_bat_suspend_volt = 3350;
	} else {
		chg_debug("qcom,warp_normal_bat_suspend_volt is %d\n", asic->warp_normal_bat_suspend_volt);
	}

	rc = of_property_read_u32(node, "qcom,warp_warm_bat_suspend_volt", &asic->warp_warm_bat_suspend_volt);
	if (rc) {
		asic->warp_warm_bat_suspend_volt = 3300;
	} else {
		chg_debug("qcom,warp_warm_bat_suspend_volt is %d\n", asic->warp_warm_bat_suspend_volt);
	}

	if (asic->batt_type_4400mv) {
		asic->firmware_data = op10_fw_data_4400_warp_ffc_15c;
		asic->fw_data_count = sizeof(op10_fw_data_4400_warp_ffc_15c);
		asic->fw_data_version = op10_fw_data_4400_warp_ffc_15c[asic->fw_data_count - 4];
	} else {//default
		asic->firmware_data = op10_fw_data_4400_warp_ffc_15c;
		asic->fw_data_count = sizeof(op10_fw_data_4400_warp_ffc_15c);
		asic->fw_data_version = op10_fw_data_4400_warp_ffc_15c[asic->fw_data_count - 4];
	}

	if (asic->warp_fw_type == WARP_FW_TYPE_OP10_4400_WARP_FFC_15C) {
		asic->firmware_data = op10_fw_data_4400_warp_ffc_15c;
		asic->fw_data_count = sizeof(op10_fw_data_4400_warp_ffc_15c);
		asic->fw_data_version = op10_fw_data_4400_warp_ffc_15c[asic->fw_data_count - 4];
	}

	switch (asic->warp_fw_type) {
	case WARP_FW_TYPE_OP10_4400_WARP_FFC_15C:
		asic->firmware_data = op10_fw_data_4400_warp_ffc_15c;
		asic->fw_data_count = sizeof(op10_fw_data_4400_warp_ffc_15c);
		asic->fw_data_version = op10_fw_data_4400_warp_ffc_15c[asic->fw_data_count - 4];
		break;
	case WARP_FW_TYPE_OP10_4450_WARP_FFC_5V4A_4BIT:
		asic->firmware_data = op10_fw_data_4450_warp_ffc_5v4a_4bit;
		asic->fw_data_count = sizeof(op10_fw_data_4450_warp_ffc_5v4a_4bit);
		asic->fw_data_version = op10_fw_data_4450_warp_ffc_5v4a_4bit[asic->fw_data_count - 4];
		break;
	case WARP_FW_TYPE_OP10_4250_WARP_FFC_10V6A_4BIT:
		asic->firmware_data = op10_fw_data_4250_warp_ffc_10v6a_4bit;
		asic->fw_data_count = sizeof(op10_fw_data_4250_warp_ffc_10v6a_4bit);
		asic->fw_data_version = op10_fw_data_4250_warp_ffc_10v6a_4bit[asic->fw_data_count - 4];
		break;
	case WARP_FW_TYPE_OP10_4450_WARP_FFC_5V6A_4BIT:
		asic->firmware_data = op10_fw_data_4450_warp_ffc_5v6a_4bit;
		asic->fw_data_count = sizeof(op10_fw_data_4450_warp_ffc_5v6a_4bit);
		asic->fw_data_version = op10_fw_data_4450_warp_ffc_5v6a_4bit[asic->fw_data_count - 4];
		break;
	case WARP_FW_TYPE_OP10_4500_WARP_FFC_6300MA_LEMONADE:
		asic->firmware_data = op10_fw_data_4500_swarp_ffc_6300mA_lemonade;
		asic->fw_data_count = sizeof(op10_fw_data_4500_swarp_ffc_6300mA_lemonade);
		asic->fw_data_version = op10_fw_data_4500_swarp_ffc_6300mA_lemonade[asic->fw_data_count - 4];
		break;
	default:
		break;
	}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0))
	op10_update_wake_lock = wakeup_source_register("op10_update_wake_lock");
#else
	op10_update_wake_lock = wakeup_source_register(NULL, "op10_update_wake_lock");
#endif
#endif /* OPLUS_CHG_OP_DEF */

	the_chip = chip;
#ifdef OPLUS_CHG_OP_DEF
	oplus_chg_asic_register(asic);
	schedule_delayed_work(&chip->asic_init_work, 0);
#endif
	chg_debug("op10 success\n");
	return 0;
}

/**********************************************************
  *
  *   [platform_driver API]
  *
  *********************************************************/
static const struct of_device_id op10_match[] = {
	{ .compatible = "oplus,op10-fastcg"},
	{ .compatible = "oplus,sy6610-fastcg"},
	{},
};

static const struct i2c_device_id op10_id[] = {
	{"op10-fastcg", 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, op10_id);

struct i2c_driver op10_i2c_driver = {
	.driver = {
		.name = "op10-fastcg",
		.owner = THIS_MODULE,
		.of_match_table = op10_match,
	},
	.probe = op10_driver_probe,
	.shutdown = op10_shutdown,
	.id_table = op10_id,
};

#ifndef OPLUS_CHG_OP_DEF
static int __init op10_subsys_init(void)
{
	int ret = 0;
	chg_debug(" init start\n");
	init_hw_version();

	if (i2c_add_driver(&op10_i2c_driver) != 0) {
		chg_err(" failed to register op10 i2c driver.\n");
	} else {
		chg_debug(" Success to register op10 i2c driver.\n");
	}
	return ret;
}

subsys_initcall(op10_subsys_init);
#else
static __init int op10_driver_init(void)
{
	init_hw_version();
	return i2c_add_driver(&op10_i2c_driver);
}

static __exit void op10_driver_exit(void)
{
	i2c_del_driver(&op10_i2c_driver);
}
oplus_chg_module_register(op10_driver);
#endif
#ifndef MODULE
MODULE_DESCRIPTION("Driver for oplus warp op10 fast mcu");
MODULE_LICENSE("GPL v2");
#endif

