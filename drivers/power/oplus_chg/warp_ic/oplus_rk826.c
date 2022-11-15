// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#define WARP_ASIC_RK826

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

#include <linux/xlog.h>
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
#include <mt-plat/mtk_boot_common.h>

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
#include <linux/oem/boot_mode.h>
#else
//#include <soc/oplus/device_info.h>
#include <soc/oplus/system/boot_mode.h>
#endif
#endif
#include "oplus_warp_fw.h"

extern int charger_abnormal_log;
#ifdef OPLUS_CHG_OP_DEF
extern struct oplus_warp_chip *g_warp_chip;
#endif

//#ifdef CONFIG_OPLUS_CHARGER_MTK
#define I2C_MASK_FLAG	(0x00ff)
#define I2C_ENEXT_FLAG	(0x0200)
#define I2C_DMA_FLAG	(0xdead2000)
//#endif

#define GTP_DMA_MAX_TRANSACTION_LENGTH	255 /* for DMA mode */

#define ERASE_COUNT			959 /*0x0000-0x3BFF*/

#define BYTE_OFFSET			2
#define BYTES_TO_WRITE		16
#define FW_CHECK_FAIL		0
#define FW_CHECK_SUCCESS	1


#define PAGE_UNIT			128
#define TRANSFER_LIMIT		72
#define I2C_ADDR			0x14
#define REG_RESET			0x5140
#define REG_SYS0			0x52C0
#define REG_HOST			0x52C8
#define	REG_SLAVE			0x52CC
#define REG_STATE			0x52C4
#define REG_MTP_SELECT		0x4308
#define REG_MTP_ADDR		0x4300
#define REG_MTP_DATA		0x4304
#define REG_SRAM_BEGIN		0x2000
#define SYNC_FLAG			0x53594E43
#define NOT_SYNC_FLAG		(~SYNC_FLAG)
#define REC_01_FLAG			0x52454301
#define REC_0O_FLAG			0x52454300
#define RESTART_FLAG		0x52455354
#define MTP_SELECT_FLAG	0x000f0001
#define MTP_ADDR_FLAG		0xffff8000
#define SLAVE_IDLE			0x49444C45
#define SLAVE_BUSY			0x42555359
#define SLAVE_ACK			0x41434B00
#define SLAVE_ACK_01		0x41434B01
#define FORCE_UPDATE_FLAG	0xaf1c0b76
#define SW_RESET_FLAG		0X0000fdb9

#define STATE_READY			0x0
#define STATE_SYNC			0x1
#define STATE_REQUEST		0x2
#define STATE_FIRMWARE		0x3
#define STATE_FINISH			0x4

#define FW_CODE_SIZE_START_ADDR  0X4
typedef struct {
	u32 tag;
	u32 length;
	u32 timeout;
	u32 ram_offset;
	u32 fw_crc;
	u32 header_crc;
} struct_req, *pstruct_req;

static struct oplus_warp_chip *the_chip = NULL;
struct wakeup_source *rk826_update_wake_lock = NULL;

#ifdef CONFIG_OPLUS_CHARGER_MTK
#define GTP_SUPPORT_I2C_DMA		0
#define I2C_MASTER_CLOCK			300

DEFINE_MUTEX(dma_wr_access_rk826);

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

	mutex_lock(&dma_wr_access_rk826);
	/*buffer[0] = (addr >> 8) & 0xFF;*/
	buffer[0] = addr & 0xFF;
	if (rxbuf == NULL) {
		mutex_unlock(&dma_wr_access_rk826);
		return -1;
	}
	/*chg_err("warp dma i2c read: 0x%x, %d bytes(s)\n", addr, len);*/
	for (retry = 0; retry < 5; ++retry) {
		ret = i2c_transfer(client->adapter, &msg[0], 2);
		if (ret < 0) {
			continue;
		}
		memcpy(rxbuf, gpDMABuf_va, len);
		mutex_unlock(&dma_wr_access_rk826);
		return 0;
	}
	/*chg_err(" Error: 0x%04X, %d byte(s), err-code: %d\n", addr, len, ret);*/
	mutex_unlock(&dma_wr_access_rk826);
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

	mutex_lock(&dma_wr_access_rk826);
	wr_buf[0] = (u8)(addr & 0xFF);
	if (txbuf == NULL) {
		mutex_unlock(&dma_wr_access_rk826);
		return -1;
	}
	memcpy(wr_buf + 1, txbuf, len);
	for (retry = 0; retry < 5; ++retry) {
		ret = i2c_transfer(client->adapter, &msg, 1);
		if (ret < 0) {
			continue;
		}
		mutex_unlock(&dma_wr_access_rk826);
		return 0;
	}
	/*chg_err(" Error: 0x%04X, %d byte(s), err-code: %d\n", addr, len, ret);*/
	mutex_unlock(&dma_wr_access_rk826);
	return ret;
}
#endif /*GTP_SUPPORT_I2C_DMA*/

static int oplus_i2c_dma_read(struct i2c_client *client, u16 addr, s32 len, u8 *rxbuf)
{
	int ret;
	s32 retry = 0;
	u8 buffer[2] = {0};
	struct i2c_msg msg[2] = {
		{
			.addr = (client->addr & I2C_MASK_FLAG),
			.flags = 0,
			.buf = buffer,
			.len = 2,
		},
		{
			.addr = (client->addr & I2C_MASK_FLAG),
			.flags = I2C_M_RD,
			.buf = (__u8 *)gpDMABuf_pa,   /*modified by PengNan*/
			.len = len,
		},
	};

	mutex_lock(&dma_wr_access_rk826);
	buffer[0] = (u8)(addr & 0xFF);
	buffer[1] = (u8)((addr >> 8) & 0xFF);
	if (rxbuf == NULL) {
		mutex_unlock(&dma_wr_access_rk826);
		return -1;
	}
	//chg_debug("warp dma i2c read: 0x%x, %d bytes(s)\n", addr, len);
	for (retry = 0; retry < 5; ++retry) {
		ret = i2c_transfer(client->adapter, &msg[0], 2);
		if (ret < 0) {
			memcpy(rxbuf, gpDMABuf_pa, len);
			mutex_unlock(&dma_wr_access_rk826);
			return ret;
		}
		memcpy(rxbuf, gpDMABuf_pa, len);
		mutex_unlock(&dma_wr_access_rk826);
		return 0;
	}
	chg_err(" Error: 0x%04X, %d byte(s), err-code: %d\n", addr, len, ret);
	mutex_unlock(&dma_wr_access_rk826);
	return ret;
}

static int oplus_i2c_dma_write(struct i2c_client *client, u16 addr, s32 len, u8 const *txbuf)
{
	int ret = 0;
	s32 retry = 0;
	u8 *wr_buf = gpDMABuf_pa;
	struct i2c_msg msg = {
		.addr = (client->addr & I2C_MASK_FLAG),
		.flags = 0,
		.buf = (__u8 *)gpDMABuf_pa, /*modified by PengNan*/
		.len = 2 + len,
	};

	mutex_lock(&dma_wr_access_rk826);
	wr_buf[0] = (u8)(addr & 0xFF);
	wr_buf[1] = (u8)((addr >> 8) & 0xFF);
	if (txbuf == NULL) {
		mutex_unlock(&dma_wr_access_rk826);
		return -1;
	}
	memcpy(wr_buf + 2, txbuf, len);
	//chg_debug("warp dma i2c write: 0x%x, %d bytes(s)\n", addr, len);
	for (retry = 0; retry < 5; ++retry) {
		ret = i2c_transfer(client->adapter, &msg, 1);
		if (ret < 0) {
			continue;
		}
		mutex_unlock(&dma_wr_access_rk826);
		return 0;
	}
	chg_err(" Error: 0x%04X, %d byte(s), err-code: %d\n", addr, len, ret);
	mutex_unlock(&dma_wr_access_rk826);
	return ret;
}

#else /*CONFIG_OPLUS_CHARGER_MTK*/

DEFINE_MUTEX(dma_wr_access_rk826);
static char gpDMABuf_pa[GTP_DMA_MAX_TRANSACTION_LENGTH] = {0};
static int oplus_i2c_dma_read(struct i2c_client *client, u16 addr, s32 len, u8 *rxbuf)
{
	int ret;
	s32 retry = 0;
	u8 buffer[2] = {0};
	struct i2c_msg msg[2] = {
		{
			.addr = (client->addr & I2C_MASK_FLAG),
			.flags = 0,
			.buf = buffer,
			.len = 2,
		},
		{
			.addr = (client->addr & I2C_MASK_FLAG),
			.flags = I2C_M_RD,
			.buf = (__u8 *)gpDMABuf_pa,   /*modified by PengNan*/
			.len = len,
		},
};
//	chg_debug("kilody in\n");

	mutex_lock(&dma_wr_access_rk826);
	buffer[0] = (u8)(addr & 0xFF);
	buffer[1] = (u8)((addr >> 8) & 0xFF);
	if (rxbuf == NULL) {
		mutex_unlock(&dma_wr_access_rk826);
		return -1;
	}
	//chg_debug("warp dma i2c read: 0x%x, %d bytes(s)\n", addr, len);
	for (retry = 0; retry < 5; ++retry) {
		ret = i2c_transfer(client->adapter, &msg[0], 2);
		if (ret < 0) {
			continue;
		}
		memcpy(rxbuf, gpDMABuf_pa, len);
		mutex_unlock(&dma_wr_access_rk826);
		return 0;
	}
	chg_err(" Error: 0x%04X, %d byte(s), err-code: %d\n", addr, len, ret);
	mutex_unlock(&dma_wr_access_rk826);

//	chg_debug("kilody out\n");
	return ret;
}

static int oplus_i2c_dma_write(struct i2c_client *client, u16 addr, s32 len, u8 const *txbuf)
{
	int ret = 0;
	s32 retry = 0;
	u8 *wr_buf = gpDMABuf_pa;
	struct i2c_msg msg = {
		.addr = (client->addr & I2C_MASK_FLAG),
		.flags = 0,
		.buf = (__u8 *)gpDMABuf_pa, /*modified by PengNan*/
		.len = 2 + len,
	};
//	chg_debug("kilody in\n");

	mutex_lock(&dma_wr_access_rk826);
	wr_buf[0] = (u8)(addr & 0xFF);
	wr_buf[1] = (u8)((addr >> 8) & 0xFF);
	if (txbuf == NULL) {
		mutex_unlock(&dma_wr_access_rk826);
		return -1;
	}
	memcpy(wr_buf + 2, txbuf, len);
	//chg_debug("warp dma i2c write: 0x%x, %d bytes(s)\n", addr, len);
	for (retry = 0; retry < 5; ++retry) {
		ret = i2c_transfer(client->adapter, &msg, 1);
		if (ret < 0) {
			continue;
		}
		mutex_unlock(&dma_wr_access_rk826);
		return 0;
	}
	chg_err(" Error: 0x%04X, %d byte(s), err-code: %d\n", addr, len, ret);
	mutex_unlock(&dma_wr_access_rk826);

//	chg_debug("kilody out\n");
	return ret;
}
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

static bool rk826_fw_update_check(struct oplus_warp_chip *chip)
{
	int ret = 0;
	u8 data_buf[4]= {0};
	u32 mtp_select_flag = MTP_SELECT_FLAG;
	u32 mtp_addr_flag = MTP_ADDR_FLAG;
	u32 i = 0, n = 0;
	u32 j = 0;
	u16 fw_size_tmp[2]={0};
	u16 fw_size;
	u16 code_size;
	bool fw_type_check_result = 0;

	if (!chip->firmware_data) {
		chg_err("rk826_fw_data Null, Return\n");
		return FW_CHECK_FAIL;
	}
	ret = oplus_i2c_dma_write(chip->client, REG_MTP_SELECT, 4, (u8 *)(&mtp_select_flag));
	if (ret < 0) {
		chg_err("write mtp select reg error\n");
		goto fw_update_check_err;
	}

	for (i = FW_CODE_SIZE_START_ADDR, j=0; i < FW_CODE_SIZE_START_ADDR +2; i++, j++) {
		mtp_addr_flag = (MTP_ADDR_FLAG | i);
		ret = oplus_i2c_dma_write(chip->client, REG_MTP_ADDR, 4, (u8 *)(&mtp_addr_flag));
		if (ret < 0) {
			chg_err("write mtp addr error\n");
			goto fw_update_check_err;
		}

		do {
			ret  = oplus_i2c_dma_read(chip->client, REG_MTP_SELECT, 4, data_buf);
			if (ret < 0) {
				chg_err("read mtp select reg error\n");
				goto fw_update_check_err;
			}
		} while (!(data_buf[1] & 0x01));

		ret = oplus_i2c_dma_read(chip->client, REG_MTP_DATA, 4, data_buf);
		if (ret < 0) {
			chg_err("read mtp data error\n");
			goto fw_update_check_err;
		}
		chg_debug("the read FW size data: %d\n", data_buf[0]);
		fw_size_tmp[j]=data_buf[0];
	}
	fw_size=(fw_size_tmp[1]<<8)|fw_size_tmp[0];
	chg_err("fw_size[%d], fw_size_tmp[1]=[%d],fw_size_tmp[0]=%d\n", fw_size, fw_size_tmp[1], fw_size_tmp[0]);
	if((fw_size%128))
		code_size=(fw_size/128+1)*128+128+64;   //128 is page ,64 is extended space
	else
		code_size=(fw_size/128)*128+128+64;   //128 is page ,64 is extended space
	for (i = code_size - 11, n = chip->fw_data_count - 11; i <= code_size - 4; i++, n++) {
		mtp_addr_flag = (MTP_ADDR_FLAG | i);
		ret = oplus_i2c_dma_write(chip->client, REG_MTP_ADDR, 4, (u8 *)(&mtp_addr_flag));
		if (ret < 0) {
			chg_err("write mtp addr error\n");
			goto fw_update_check_err;
		}

		do {
			ret  = oplus_i2c_dma_read(chip->client, REG_MTP_SELECT, 4, data_buf);
			if (ret < 0) {
				chg_err("read mtp select reg error\n");
				goto fw_update_check_err;
			}
		} while (!(data_buf[1] & 0x01));

		ret = oplus_i2c_dma_read(chip->client, REG_MTP_DATA, 4, data_buf);
		if (ret < 0) {
			chg_err("read mtp data error\n");
			goto fw_update_check_err;
		}
		chg_debug("the read compare data: %d\n", data_buf[0]);
		if (i == code_size - 4){
			chip->fw_mcu_version = data_buf[0];
			chg_err("fw_mcu_version :0x%x,data_buf:0x%x\n",chip->fw_mcu_version,data_buf[0]);
		}
		if (data_buf[0] != chip->firmware_data[n]) {
			//chg_err("rk826_fw_data check fail\n");
			//goto fw_update_check_err;
			fw_type_check_result = 1;
		}
	}
	if(fw_type_check_result)
		goto fw_update_check_err;
	return FW_CHECK_SUCCESS;

fw_update_check_err:
	chg_err("rk826_fw_data check fail\n");
	return FW_CHECK_FAIL;
}

u32 js_hash_en(u32 hash, const u8 *buf, u32 len)
{
	u32 i;

	for (i = 0; i < len; i++)
		hash ^= ((hash << 5) + buf[i] + (hash >> 2));
	return hash;
}

u32 js_hash(const u8 *buf, u32 len)
{
	return js_hash_en(0x47C6A7E6, buf, len);
}

int WriteSram(struct oplus_warp_chip *chip, const u8 *buf, u32 size)
{
	u8 offset = 0;
	u16 reg_addr;
	int ret = 0;
	int i = 0;
	int cur_size = 0;
	int try_count = 5;
	u8 readbuf[4] = {0};
	u8 tx_buff[4] = {0};
	u32 rec_0O_flag = REC_0O_FLAG;
	u32 rec_01_flag = REC_01_FLAG;

	while (size) {
		if (size >= TRANSFER_LIMIT) {
			cur_size = TRANSFER_LIMIT;
		} else
			cur_size = size;
		for (i = 0; i < cur_size / 4; i++) {
			reg_addr = REG_SRAM_BEGIN + i * 4;
			memcpy(tx_buff, buf + offset + i * 4, 4);
			ret = oplus_i2c_dma_write(chip->client, reg_addr, 4, tx_buff);
			if (ret < 0) {
				chg_err("write SRAM fail");
				return -1;
			}
		}
		//ret = oplus_i2c_dma_read(chip->client, REG_STATE, 4, readbuf);
		//chg_err("teh REG_STATE1: %d", *(u32*)readbuf);
		ret = oplus_i2c_dma_write(chip->client, REG_HOST, 4, (u8 *)(&rec_0O_flag));
		//mdelay(3);
		//write rec_00 into host
		if (ret < 0) {
			chg_err("write rec_00 into host");
			return -1;
		}
		//read slave
		do {
			ret = oplus_i2c_dma_read(chip->client, REG_STATE, 4, readbuf);
			chg_err(" the try_count: %d, the REG_STATE: %d", try_count, *(u32*)readbuf);
			msleep(10);
			ret = oplus_i2c_dma_read(chip->client, REG_SLAVE, 4, readbuf);
			if (ret < 0) {
				chg_err("read slave ack fail");
				return -1;
			}
			try_count--;
		} while (*(u32 *)readbuf == SLAVE_BUSY);
		chg_debug("the try_count: %d, the readbuf: %x\n", try_count, *(u32 *)readbuf);
		if ((*(u32 *)readbuf != SLAVE_ACK) && (*(u32 *)readbuf != SLAVE_ACK_01)) {
			chg_err(" slave ack fail");
			return -1;
		}

		//write rec_01 into host
		ret = oplus_i2c_dma_write(chip->client, REG_HOST, 4, (u8 *)(&rec_01_flag));
		//write rec_00 into host
		if (ret < 0) {
			chg_err("write rec_00 into host");
			return -1;
		}

		//msleep(50);
		offset += cur_size;
		size -= cur_size;
		try_count = 5;
	}
	return 0;
}

int Download_00_code(struct oplus_warp_chip *chip)
{
	u8 transfer_buf[TRANSFER_LIMIT];
	u32 onetime_size = TRANSFER_LIMIT - 8;
	u32 index = 0;
	u32 offset = 0;
	int ret = 0;
	int size=16384;// erase 16kb

	chg_debug("size: %d\n", size);
	pr_err("%s: erase_rk826_00  start\n", __func__);
	do {
		memset(transfer_buf, 0, TRANSFER_LIMIT);

		if (size >= onetime_size) {
			//memcpy(transfer_buf, buf + offset, onetime_size);
			size-= onetime_size;
			offset += onetime_size;
		} else {
			//memcpy(transfer_buf, buf + offset, size);
			offset += size;
			size = 0;
		}
		*((u32 *)(transfer_buf + onetime_size)) = index;
		*((u32 *)(transfer_buf + onetime_size + 4)) = js_hash(transfer_buf, onetime_size + 4);
		ret = WriteSram(chip, transfer_buf, TRANSFER_LIMIT);
		if (ret != 0) {
			return ret;
		}
		chg_debug("index: %d\n", index);
		index++;
	} while (size);
	pr_err("%s: erase_rk826_00 end\n", __func__);
	return 0;
}

int Download_ff_code(struct oplus_warp_chip *chip)
{
	u8 transfer_buf[TRANSFER_LIMIT];
	u32 onetime_size = TRANSFER_LIMIT - 8;
	u32 index = 0;
	u32 offset = 0;
	int ret = 0;
	int size=16384;// erase 16kb
	chg_debug("size: %d\n", size);
	pr_err("%s: erase_rk826_ff start\n", __func__);
	do {
		memset(transfer_buf, 0xff, TRANSFER_LIMIT);

		if (size >= onetime_size) {
			//memcpy(transfer_buf, buf + offset, onetime_size);
			size-= onetime_size;
			offset += onetime_size;
		} else {
			//memcpy(transfer_buf, buf + offset, size);
			offset += size;
			size = 0;
		}
		*((u32 *)(transfer_buf + onetime_size)) = index;
		*((u32 *)(transfer_buf + onetime_size + 4)) = js_hash(transfer_buf, onetime_size + 4);
		ret = WriteSram(chip, transfer_buf, TRANSFER_LIMIT);
		if (ret != 0) {
			return ret;
		}
		chg_debug("index: %d\n", index);
		index++;
	} while (size);
	pr_err("%s: erase_rk826_ff end\n", __func__);
	return 0;
}
static int rk826_fw_write_00_code(struct oplus_warp_chip *chip)
{
	int ret = 0;
	int iTryCount = 3;
	struct_req req = {0};
	u32 sync_flag = SYNC_FLAG;
	u32 force_update_flag = FORCE_UPDATE_FLAG;
	u32 sw_reset_flag = SW_RESET_FLAG;
	u32 rec_01_flag = REC_01_FLAG;
	u8 read_buf[4] = {0};

	oplus_i2c_dma_write(chip->client, REG_SYS0, 4, (u8 *)(&force_update_flag));
	oplus_i2c_dma_write(chip->client, REG_RESET, 4, (u8 *)(&sw_reset_flag));
	while (iTryCount) {
		ret = oplus_i2c_dma_write(chip->client, REG_HOST, 4, (u8 *)(&sync_flag));
		if (ret < 0) {
			chg_err("write sync failed!");
			goto update_fw_err;
		}
		msleep(10);
		//2.check ~sync
		ret = oplus_i2c_dma_read(chip->client, REG_HOST, 4, read_buf);
		printk("the data: %x, %x, %x, %x\n", read_buf[0], read_buf[1], read_buf[2], read_buf[3]);
		printk("the data: %x, %x, %x, %x\n", *(u8 *)(&sync_flag), *((u8 *)(&sync_flag) + 1), *((u8 *)(&sync_flag) + 2), *((u8 *)(&sync_flag) + 3));

		if (ret < 0) {
			chg_err("read sync failed!");
			goto update_fw_err;
		}
		if (*(u32 *)read_buf != NOT_SYNC_FLAG) {
			chg_err("check ~sync failed!");
			iTryCount--;
			msleep(50);
			continue;
		}
		break;
	}

	if (iTryCount == 0) {
		chg_err("Failed to sync!");
		goto update_fw_err;
	}

	// write rec_01
	ret = oplus_i2c_dma_write(chip->client, REG_HOST, 4, (u8 *)(&rec_01_flag));
	if (ret < 0) {
		chg_err("write rec_01 flag failed!");
		goto update_fw_err;
	}
	msleep(10);

	// read reg_state
	ret = oplus_i2c_dma_read(chip->client, REG_STATE, 4, read_buf);
	if (ret<0) {
		chg_err("write rec_01 flag failed!");
		goto update_fw_err;
	}
	if (*(u32 *)read_buf != STATE_REQUEST) {
		chg_err("Failed to go into request_state!");
		goto update_fw_err;
	}

	// send req
	req.tag = 0x51455220;
	req.ram_offset = 0;
	req.length =  16384;//for erase
	req.timeout = 0;
	req.fw_crc = js_hash(chip->firmware_data, chip->fw_data_count);//for crc hash
	req.header_crc = js_hash((const u8*)&req, sizeof(req) - 4);
	if ((ret = WriteSram(chip, (const u8* )&req, sizeof(req))) != 0) {
		chg_err("failed to send request!err=%d\n", ret);
		goto update_fw_err;
	}
	msleep(10);

	// read state firwware
	ret = oplus_i2c_dma_read(chip->client, REG_STATE, 4, read_buf);
	printk("read state firwware: %x\n", *(u32 *)read_buf);
	if (ret < 0) {
		chg_err("write REG_STATE flag failed!");
		goto update_fw_err;
	}
	if (*(u32 *)read_buf != STATE_FIRMWARE) {
		chg_err("Failed to go into firmware_state");
		goto update_fw_err;
	}

	// send fw
	if ((ret = Download_00_code(chip)) != 0) {
		chg_err("failed to send firmware");
		goto update_fw_err;
	}

	ret = oplus_i2c_dma_read(chip->client, REG_STATE, 4, read_buf);
	if (ret < 0) {
		chg_err("write REG_STATE flag failed!");
		goto update_fw_err;
	}
	if (*(u32 *)read_buf != STATE_FINISH) {
		chg_err("Failed to go into finish_state");
		goto update_fw_err;
	}
	oplus_i2c_dma_write(chip->client, REG_RESET, 4, (u8 *)(&sw_reset_flag));
	chg_debug("success\n");
	return 0;

update_fw_err:
	charger_abnormal_log = CRITICAL_LOG_WARP_FW_UPDATE_ERR;
	chg_err("fail\n");
	return 1;
}
static int rk826_fw_write_ff_code(struct oplus_warp_chip *chip)
{
	int ret = 0;
	int iTryCount = 3;
	struct_req req = {0};
	u32 sync_flag = SYNC_FLAG;
	u32 force_update_flag = FORCE_UPDATE_FLAG;
	u32 sw_reset_flag = SW_RESET_FLAG;
	u32 rec_01_flag = REC_01_FLAG;
	u8 read_buf[4] = {0};

	oplus_i2c_dma_write(chip->client, REG_SYS0, 4, (u8 *)(&force_update_flag));
	oplus_i2c_dma_write(chip->client, REG_RESET, 4, (u8 *)(&sw_reset_flag));
	while (iTryCount) {
		ret = oplus_i2c_dma_write(chip->client, REG_HOST, 4, (u8 *)(&sync_flag));
		if (ret < 0) {
			chg_err("write sync failed!");
			goto update_fw_err;
		}
		msleep(10);
		//2.check ~sync
		ret = oplus_i2c_dma_read(chip->client, REG_HOST, 4, read_buf);
		printk("the data: %x, %x, %x, %x\n", read_buf[0], read_buf[1], read_buf[2], read_buf[3]);
		printk("the data: %x, %x, %x, %x\n", *(u8 *)(&sync_flag), *((u8 *)(&sync_flag) + 1), *((u8 *)(&sync_flag) + 2), *((u8 *)(&sync_flag) + 3));

		if (ret < 0) {
			chg_err("read sync failed!");
			goto update_fw_err;
		}
		if (*(u32 *)read_buf != NOT_SYNC_FLAG) {
			chg_err("check ~sync failed!");
			iTryCount--;
			msleep(50);
			continue;
		}
		break;
	}

	if (iTryCount == 0) {
		chg_err("Failed to sync!");
		goto update_fw_err;
	}

	// write rec_01
	ret = oplus_i2c_dma_write(chip->client, REG_HOST, 4, (u8 *)(&rec_01_flag));
	if (ret < 0) {
		chg_err("write rec_01 flag failed!");
		goto update_fw_err;
	}
	msleep(10);

	// read reg_state
	ret = oplus_i2c_dma_read(chip->client, REG_STATE, 4, read_buf);
	if (ret<0) {
		chg_err("write rec_01 flag failed!");
		goto update_fw_err;
	}
	if (*(u32 *)read_buf != STATE_REQUEST) {
		chg_err("Failed to go into request_state!");
		goto update_fw_err;
	}

	// send req
	req.tag = 0x51455220;
	req.ram_offset = 0;
	req.length =  16384;//for erase
	req.timeout = 0;
	req.fw_crc = js_hash(chip->firmware_data, chip->fw_data_count);//for crc hash
	req.header_crc = js_hash((const u8*)&req, sizeof(req) - 4);
	if ((ret = WriteSram(chip, (const u8* )&req, sizeof(req))) != 0) {
		chg_err("failed to send request!err=%d\n", ret);
		goto update_fw_err;
	}
	msleep(10);

	// read state firwware
	ret = oplus_i2c_dma_read(chip->client, REG_STATE, 4, read_buf);
	printk("read state firwware: %x\n", *(u32 *)read_buf);
	if (ret < 0) {
		chg_err("write REG_STATE flag failed!");
		goto update_fw_err;
	}
	if (*(u32 *)read_buf != STATE_FIRMWARE) {
		chg_err("Failed to go into firmware_state");
		goto update_fw_err;
	}

	// send fw
	if ((ret =  Download_ff_code(chip)) != 0) {
		chg_err("failed to send firmware");
		goto update_fw_err;
	}

	ret = oplus_i2c_dma_read(chip->client, REG_STATE, 4, read_buf);
	if (ret < 0) {
		chg_err("write REG_STATE flag failed!");
		goto update_fw_err;
	}
	if (*(u32 *)read_buf != STATE_FINISH) {
		chg_err("Failed to go into finish_state");
		goto update_fw_err;
	}
	oplus_i2c_dma_write(chip->client, REG_RESET, 4, (u8 *)(&sw_reset_flag));
	chg_debug("success\n");
	return 0;

update_fw_err:
	charger_abnormal_log = CRITICAL_LOG_WARP_FW_UPDATE_ERR;
	chg_err("fail\n");
	return 1;
}
int DownloadFirmware(struct oplus_warp_chip *chip, const u8 *buf, u32 size)
{
	u8 transfer_buf[TRANSFER_LIMIT];
	u32 onetime_size = TRANSFER_LIMIT - 8;
	u32 index = 0;
	u32 offset = 0;
	int ret = 0;

	chg_debug("size: %d\n", size);
	do {
		memset(transfer_buf, 0, TRANSFER_LIMIT);
		if (size >= onetime_size) {
			memcpy(transfer_buf, buf + offset, onetime_size);
			size-= onetime_size;
			offset += onetime_size;
		} else {
			memcpy(transfer_buf, buf + offset, size);
			offset += size;
			size = 0;
		}
		*((u32 *)(transfer_buf + onetime_size)) = index;
		*((u32 *)(transfer_buf + onetime_size + 4)) = js_hash(transfer_buf, onetime_size + 4);
		ret = WriteSram(chip, transfer_buf, TRANSFER_LIMIT);
		if (ret != 0) {
			return ret;
		}
		chg_debug("index: %d\n", index);
		index++;
	} while (size);
	return 0;
}

#ifdef OPLUS_CHG_DEBUG
static int rk826_fw_update_by_buf(struct oplus_warp_chip *chip, u8 *fw_buf, u32 fw_size)
{
	int ret = 0;
	int iTryCount = 3;
	struct_req req = {0};
	u32 sync_flag = SYNC_FLAG;
	u32 force_update_flag = FORCE_UPDATE_FLAG;
	u32 sw_reset_flag = SW_RESET_FLAG;
	u32 rec_01_flag = REC_01_FLAG;
	u8 read_buf[4] = {0};

	oplus_i2c_dma_write(chip->client, REG_SYS0, 4, (u8 *)(&force_update_flag));
	oplus_i2c_dma_write(chip->client, REG_RESET, 4, (u8 *)(&sw_reset_flag));
	while (iTryCount) {
		ret = oplus_i2c_dma_write(chip->client, REG_HOST, 4, (u8 *)(&sync_flag));
		if (ret < 0) {
			chg_err("write sync failed!");
			goto update_fw_err;
		}

		//2.check ~sync
		ret = oplus_i2c_dma_read(chip->client, REG_HOST, 4, read_buf);
		printk("the data: %x, %x, %x, %x\n", read_buf[0], read_buf[1], read_buf[2], read_buf[3]);
		printk("the data: %x, %x, %x, %x\n", *(u8 *)(&sync_flag), *((u8 *)(&sync_flag) + 1), *((u8 *)(&sync_flag) + 2), *((u8 *)(&sync_flag) + 3));

		if (ret < 0) {
			chg_err("read sync failed!");
			goto update_fw_err;
		}
		if (*(u32 *)read_buf != NOT_SYNC_FLAG) {
			chg_err("check ~sync failed!");
			iTryCount--;
			msleep(50);
			continue;
		}
		break;
	}

	if (iTryCount == 0) {
		chg_err("Failed to sync!");
		goto update_fw_err;
	}

	// write rec_01
	ret = oplus_i2c_dma_write(chip->client, REG_HOST, 4, (u8 *)(&rec_01_flag));
	if (ret < 0) {
		chg_err("write rec_01 flag failed!");
		goto update_fw_err;
	}
	msleep(10);

	// read reg_state
	ret = oplus_i2c_dma_read(chip->client, REG_STATE, 4, read_buf);
	if (ret<0) {
		chg_err("write rec_01 flag failed!");
		goto update_fw_err;
	}
	if (*(u32 *)read_buf != STATE_REQUEST) {
		chg_err("Failed to go into request_state!");
		goto update_fw_err;
	}

	// send req
	req.tag = 0x51455220;
	req.ram_offset = 0;
	req.length =  fw_size;
	req.timeout = 0;
	req.fw_crc = js_hash(fw_buf, req.length);
	req.header_crc = js_hash((const u8*)&req, sizeof(req) - 4);
	if ((ret = WriteSram(chip, (const u8* )&req, sizeof(req))) != 0) {
		chg_err("failed to send request!err=%d\n", ret);
		goto update_fw_err;
	}
	msleep(10);

	// read state firwware
	ret = oplus_i2c_dma_read(chip->client, REG_STATE, 4, read_buf);
	printk("read state firwware: %x\n", *(u32 *)read_buf);
	if (ret < 0) {
		chg_err("write REG_STATE flag failed!");
		goto update_fw_err;
	}
	if (*(u32 *)read_buf != STATE_FIRMWARE) {
		chg_err("Failed to go into firmware_state");
		goto update_fw_err;
	}

	// send fw
	if ((ret = DownloadFirmware(chip, fw_buf, fw_size)) != 0) {
		chg_err("failed to send firmware");
		goto update_fw_err;
	}

	ret = oplus_i2c_dma_read(chip->client, REG_STATE, 4, read_buf);
	if (ret < 0) {
		chg_err("write REG_STATE flag failed!");
		goto update_fw_err;
	}
	if (*(u32 *)read_buf != STATE_FINISH) {
		chg_err("Failed to go into finish_state");
		goto update_fw_err;
	}
	oplus_i2c_dma_write(chip->client, REG_RESET, 4, (u8 *)(&sw_reset_flag));
	chip->fw_mcu_version = chip->fw_data_version;
	chg_debug("success\n");
	return 0;

update_fw_err:
	charger_abnormal_log = CRITICAL_LOG_WARP_FW_UPDATE_ERR;
	chg_err("fail\n");
	return 1;
}
#endif /* OPLUS_CHG_DEBUG */

static int rk826_fw_update(struct oplus_warp_chip *chip)
{
	int ret = 0;
	int iTryCount = 3;
	struct_req req = {0};
	u32 sync_flag = SYNC_FLAG;
	u32 force_update_flag = FORCE_UPDATE_FLAG;
	u32 force_dis_update_flag = 0x00000000;
	u32 sw_reset_flag = SW_RESET_FLAG;
	u32 rec_01_flag = REC_01_FLAG;
	u8 read_buf[4] = {0};

	oplus_i2c_dma_write(chip->client, REG_SYS0, 4, (u8 *)(&force_update_flag));
	oplus_i2c_dma_write(chip->client, REG_RESET, 4, (u8 *)(&sw_reset_flag));
	while (iTryCount) {
		ret = oplus_i2c_dma_write(chip->client, REG_HOST, 4, (u8 *)(&sync_flag));
		if (ret < 0) {
			chg_err("write sync failed!");
			goto update_fw_err;
		}
		msleep(10);
		//2.check ~sync
		ret = oplus_i2c_dma_read(chip->client, REG_HOST, 4, read_buf);
		printk("the data: %x, %x, %x, %x\n", read_buf[0], read_buf[1], read_buf[2], read_buf[3]);
		printk("the data: %x, %x, %x, %x\n", *(u8 *)(&sync_flag), *((u8 *)(&sync_flag) + 1), *((u8 *)(&sync_flag) + 2), *((u8 *)(&sync_flag) + 3));

		if (ret < 0) {
			chg_err("read sync failed!");
			goto update_fw_err;
		}
		if (*(u32 *)read_buf != NOT_SYNC_FLAG) {
			chg_err("check ~sync failed!");
			iTryCount--;
			msleep(50);
			continue;
		}
		break;
	}

	if (iTryCount == 0) {
		chg_err("Failed to sync!");
		goto update_fw_err;
	}

	// write rec_01
	ret = oplus_i2c_dma_write(chip->client, REG_HOST, 4, (u8 *)(&rec_01_flag));
	if (ret < 0) {
		chg_err("write rec_01 flag failed!");
		goto update_fw_err;
	}
	msleep(10);

	// read reg_state
	ret = oplus_i2c_dma_read(chip->client, REG_STATE, 4, read_buf);
	if (ret<0) {
		chg_err("write rec_01 flag failed!");
		goto update_fw_err;
	}
	if (*(u32 *)read_buf != STATE_REQUEST) {
		chg_err("Failed to go into request_state!");
		goto update_fw_err;
	}

	// send req
	req.tag = 0x51455220;
	req.ram_offset = 0;
	req.length =  chip->fw_data_count;
	req.timeout = 0;
	req.fw_crc = js_hash(chip->firmware_data, req.length);
	req.header_crc = js_hash((const u8*)&req, sizeof(req) - 4);
	if ((ret = WriteSram(chip, (const u8* )&req, sizeof(req))) != 0) {
		chg_err("failed to send request!err=%d\n", ret);
		goto update_fw_err;
	}
	msleep(10);

	// read state firwware
	ret = oplus_i2c_dma_read(chip->client, REG_STATE, 4, read_buf);
	printk("read state firwware: %x\n", *(u32 *)read_buf);
	if (ret < 0) {
		chg_err("write REG_STATE flag failed!");
		goto update_fw_err;
	}
	if (*(u32 *)read_buf != STATE_FIRMWARE) {
		chg_err("Failed to go into firmware_state");
		goto update_fw_err;
	}

	// send fw
	if ((ret = DownloadFirmware(chip, chip->firmware_data, chip->fw_data_count)) != 0) {
		chg_err("failed to send firmware");
		goto update_fw_err;
	}

	ret = oplus_i2c_dma_read(chip->client, REG_STATE, 4, read_buf);
	if (ret < 0) {
		chg_err("write REG_STATE flag failed!");
		goto update_fw_err;
	}
	if (*(u32 *)read_buf != STATE_FINISH) {
		chg_err("Failed to go into finish_state");
		goto update_fw_err;
	}
	msleep(10);
	oplus_i2c_dma_write(chip->client, REG_SYS0, 4, (u8 *)(&force_dis_update_flag));
	msleep(2);
	oplus_i2c_dma_write(chip->client, REG_RESET, 4, (u8 *)(&sw_reset_flag));
	chip->fw_mcu_version = chip->fw_data_version;
	chg_debug("success\n");
	return 0;

update_fw_err:
	charger_abnormal_log = CRITICAL_LOG_WARP_FW_UPDATE_ERR;
	chg_err("fail\n");
	return 1;
}

static int rk826_get_fw_verion_from_ic(struct oplus_warp_chip *chip)
{
	unsigned char addr_buf[2] = {0x3B, 0xF0};
	unsigned char data_buf[4] = {0};
	int rc = 0;
	int update_result = 0;

	if (oplus_is_power_off_charging(chip) == true || oplus_is_charger_reboot(chip) == true) {
		chip->mcu_update_ing = true;
		update_result = rk826_fw_update(chip);
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

		chip->mcu_update_ing = false;
		msleep(5);
		opchg_set_reset_active(chip);
	}
	return data_buf[0];
}

int rk826_is_rf_ftm_mode(void)
{
	int boot_mode = get_boot_mode();
#ifdef CONFIG_OPLUS_CHARGER_MTK
	if (boot_mode == META_BOOT || boot_mode == FACTORY_BOOT
			|| boot_mode == ADVMETA_BOOT || boot_mode == ATE_FACTORY_BOOT) {
		chg_debug(" boot_mode:%d, return\n",boot_mode);
		return true;
	} else {
		chg_debug(" boot_mode:%d, return false\n",boot_mode);
		return false;
	}
#else
	if(boot_mode == MSM_BOOT_MODE__RF || boot_mode == MSM_BOOT_MODE__WLAN
			|| boot_mode == MSM_BOOT_MODE__FACTORY){
		chg_debug(" boot_mode:%d, return\n",boot_mode);
		return true;
	} else {
		chg_debug(" boot_mode:%d, return false\n",boot_mode);
		return false;
	}
#endif
}

static int rk826_fw_check_then_recover(struct oplus_warp_chip *chip)
{
	int update_result = 0;
	int try_count = 5;
	int ret = 0;
	int rc = 0;
	u8 value_buf[4] = {0};
	int fw_check_err = 0;
	u32 force_dis_update_flag = 0x00000000;
	u32 sw_reset_flag = SW_RESET_FLAG;

	if (rk826_is_rf_ftm_mode() == true) {
		opchg_set_reset_sleep(chip);
		return 0;
	}
	if (!chip->firmware_data) {
		chg_err("rk826_fw_data Null, Return\n");
		return FW_ERROR_DATA_MODE;
	} else {
		chg_debug("begin\n");
	}

	if (oplus_is_power_off_charging(chip) == true || oplus_is_charger_reboot(chip) == true) {
		chip->mcu_update_ing = true;
		opchg_set_reset_active_force(chip);
		msleep(5);
		update_result = rk826_fw_update(chip);
		chip->mcu_update_ing = false;
		if (update_result) {
			msleep(30);
			opchg_set_clock_sleep(chip);
			opchg_set_reset_active_force(chip);
		}
		ret = FW_NO_CHECK_MODE;
	} else {
update_asic_fw:
		opchg_set_clock_active(chip);
		chip->mcu_boot_by_gpio = true;
		msleep(10);
		opchg_set_reset_active_force(chip);
		chip->mcu_update_ing = true;
		msleep(2500);
		chip->mcu_boot_by_gpio = false;
		opchg_set_clock_sleep(chip);
		__pm_stay_awake(rk826_update_wake_lock);
		if (rk826_fw_update_check(chip) == FW_CHECK_FAIL || fw_check_err) {
		chg_debug("firmware update start\n");
		do {
			update_result = rk826_fw_write_00_code(chip);
			update_result = rk826_fw_write_ff_code(chip);
			update_result = rk826_fw_write_00_code(chip);
			update_result = rk826_fw_update(chip);
			update_result = rk826_fw_update(chip);
			if (!update_result)
				break;
		} while ((update_result) && (--try_count > 0));
			chg_debug("firmware update end, retry times %d\n", 5 - try_count);
		} else {
			chip->warp_fw_check = true;
			chg_debug("fw check ok\n");
		}
		if (!update_result) {
			oplus_i2c_dma_write(chip->client, REG_SYS0, 4,
					    (u8 *)(&force_dis_update_flag));
			oplus_i2c_dma_write(chip->client, REG_SLAVE, 4,
					    (u8 *)(&force_dis_update_flag));
			msleep(10);
			oplus_i2c_dma_write(chip->client, REG_RESET, 4,
					    (u8 *)(&sw_reset_flag));
			usleep_range(1000000, 1000000);
			rc = oplus_i2c_dma_read(chip->client, 0x52cc, 4,
						value_buf);
			pr_err("rk826 read register 0x52cc rc = %d\n", rc);
			if ((value_buf[0] == 0x45) && (value_buf[1] == 0x4c) &&
			    (value_buf[2] == 0x44) && (value_buf[3] == 0x49)) {
				pr_info("read 0x52cc success 0x%x,0x%x,0x%x,0x%x",
					value_buf[0], value_buf[1],
					value_buf[2], value_buf[3]);
				fw_check_err++;
				if (fw_check_err > 3)
					goto update_fw_err;
				msleep(1000);
				goto update_asic_fw;
			} else {
				pr_err("rk826 read register 0x52cc fail, rc = %d\n",
				       rc);
				pr_info("rk826 fw upgrade check ok.");
			}
		}
update_fw_err:
		__pm_relax(rk826_update_wake_lock);
		chip->mcu_update_ing = false;
		msleep(5);
		opchg_set_reset_active(chip);
		ret = FW_CHECK_MODE;
	}
	opchg_set_reset_sleep(chip);

	return ret;
}

static int rk826_fw_check_then_recover_fix(struct oplus_warp_chip *chip)
{
	int update_result = 0;
	int try_count = 5;
	int ret = 0;
	int rc = 0;
	u8 value_buf[4] = { 0 };
	int fw_check_err = 0;
	u32 force_dis_update_flag = 0x00000000;
	u32 sw_reset_flag = SW_RESET_FLAG;

	if (rk826_is_rf_ftm_mode() == true) {
		opchg_set_reset_sleep(chip);
		return 0;
	}
	if (!chip->firmware_data) {
		chg_err("rk826_fw_data Null, Return\n");
		return FW_ERROR_DATA_MODE;
	} else {
		chg_debug("begin\n");
	}

	if (oplus_is_power_off_charging(chip) == true ||
	    oplus_is_charger_reboot(chip) == true) {
		chip->mcu_update_ing = true;
		opchg_set_reset_active_force(chip);
		msleep(5);
		update_result = rk826_fw_update(chip);
		chip->mcu_update_ing = false;
		if (update_result) {
			msleep(30);
			opchg_set_clock_sleep(chip);
			opchg_set_reset_active_force(chip);
		}
		ret = FW_NO_CHECK_MODE;
	} else {
	update_asic_fw:
		opchg_set_clock_active(chip);
		chip->mcu_boot_by_gpio = true;
		msleep(10);
		opchg_set_reset_active_force(chip);
		chip->mcu_update_ing = true;
		msleep(2500);
		chip->mcu_boot_by_gpio = false;
		opchg_set_clock_sleep(chip);
		__pm_stay_awake(rk826_update_wake_lock);
		if (rk826_fw_update_check(chip) == FW_CHECK_FAIL ||
		    fw_check_err) {
			chg_debug("firmware update start\n");
			do {
				update_result = rk826_fw_update(chip);
				if (!update_result)
					break;
			} while ((update_result) && (--try_count > 0));
			chg_debug("firmware update end, retry times %d\n",
				  5 - try_count);
		} else {
			chip->warp_fw_check = true;
			chg_debug("fw check ok\n");
		}
		if (!update_result) {
			oplus_i2c_dma_write(chip->client, REG_SYS0, 4,
					    (u8 *)(&force_dis_update_flag));
			oplus_i2c_dma_write(chip->client, REG_SLAVE, 4,
					    (u8 *)(&force_dis_update_flag));
			msleep(10);
			oplus_i2c_dma_write(chip->client, REG_RESET, 4,
					    (u8 *)(&sw_reset_flag));
			usleep_range(1000000, 1000000);
			rc = oplus_i2c_dma_read(chip->client, 0x52cc, 4,
						value_buf);
			pr_err("rk826 read register 0x52cc rc = %d\n", rc);
			if ((value_buf[0] == 0x45) && (value_buf[1] == 0x4c) &&
			    (value_buf[2] == 0x44) && (value_buf[3] == 0x49)) {
				pr_info("read 0x52cc success 0x%x,0x%x,0x%x,0x%x",
					value_buf[0], value_buf[1],
					value_buf[2], value_buf[3]);
				fw_check_err++;
				if (fw_check_err > 3)
					goto update_fw_err;
				msleep(1000);
				goto update_asic_fw;
			} else {
				pr_err("rk826 read register 0x52cc fail, rc = %d\n",
				       rc);
				pr_info("rk826 fw upgrade check ok.");
			}
		}
	update_fw_err:
		__pm_relax(rk826_update_wake_lock);
		chip->mcu_update_ing = false;
		msleep(5);
		opchg_set_reset_active(chip);
		ret = FW_CHECK_MODE;
	}
	opchg_set_reset_sleep(chip);

	return ret;
}

int rk826_asic_fw_status(struct oplus_warp_chip *chip)
{
	u32 force_dis_update_flag = 0x00000000;
	u32 sw_reset_flag = SW_RESET_FLAG;
	u8 value_buf[4] = { 0 };
	int rc = 0;

	if (!chip)
		return -EINVAL;
	oplus_i2c_dma_write(chip->client, REG_SYS0, 4,
			    (u8 *)(&force_dis_update_flag));
	oplus_i2c_dma_write(chip->client, REG_SLAVE, 4,
			    (u8 *)(&force_dis_update_flag));
	msleep(10);
	oplus_i2c_dma_write(chip->client, REG_RESET, 4, (u8 *)(&sw_reset_flag));
	usleep_range(1000000, 1000000);
	rc = oplus_i2c_dma_read(chip->client, 0x52cc, 4, value_buf);
	pr_err("rk826 read register 0x52cc rc = %d\n", rc);
	if ((value_buf[0] == 0x45) && (value_buf[1] == 0x4c) &&
	    (value_buf[2] == 0x44) && (value_buf[3] == 0x49)) {
		pr_info("read 0x52cc success 0x%x,0x%x,0x%x,0x%x", value_buf[0],
			value_buf[1], value_buf[2], value_buf[3]);
		return 0;
	} else {
		pr_err("rk826 read register 0x52cc fail, rc = %d\n", rc);
		return 1;
	}
}

#ifdef OPLUS_CHG_DEBUG
static int rk826_user_fw_upgrade(struct oplus_warp_chip *chip, u8 *fw_buf, u32 fw_size)
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
	__pm_stay_awake(rk826_update_wake_lock);
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
		update_result = rk826_fw_update_by_buf(chip, fw_buf, fw_size);
		if (!update_result)
			break;
	} while ((update_result) && (--try_count > 0));
	chg_debug("firmware update end, retry times %d\n", 5 - try_count);
	__pm_relax(rk826_update_wake_lock);
	chip->mcu_update_ing = false;
	msleep(5);
	opchg_set_reset_active(chip);

	return update_result;
}
#endif /* OPLUS_CHG_DEBUG */

int rk826_set_battery_temperature_soc(int temp_bat, int soc_bat)
{
	int ret = 0;
	u8 read_buf[4] = { 0 };
	chg_err("kilody write rk826:temp_bat=%d,soc_bat=%d\n", temp_bat, soc_bat);

	read_buf[0] = temp_bat & 0xff;
	read_buf[1] = (temp_bat >> 8) & 0xff;
	read_buf[2] = soc_bat & 0xff;
	read_buf[3] = (soc_bat >> 8) & 0xff;

	ret = oplus_i2c_dma_write(the_chip->client, 0x52C4, 4, read_buf);
	if (ret < 0) {
		chg_err("rk826 write slave ack fail");
		return -1;
	}
	return 0;
}

void rk826_update_temperature_soc(void)
{
	int temp = 0;
	int soc = 0;

	soc = oplus_gauge_get_batt_soc();
	temp = oplus_chg_match_temp_for_chging();
	rk826_set_battery_temperature_soc(temp, soc);

	chg_err("kilody in! soc = %d,temp = %d,chging = %d\n", soc, temp, oplus_warp_get_fastchg_ing());
}

struct oplus_warp_operations oplus_rk826_ops = {
	.fw_update = rk826_fw_update,
	.fw_check_then_recover = rk826_fw_check_then_recover,
	.fw_check_then_recover_fix = rk826_fw_check_then_recover_fix,
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
	.get_fw_version = rk826_get_fw_verion_from_ic,
	.get_clk_gpio_num = opchg_get_clk_gpio_num,
	.get_data_gpio_num = opchg_get_data_gpio_num,
	.update_temperature_soc = rk826_update_temperature_soc,
	.check_asic_fw_status = rk826_asic_fw_status,
#ifdef OPLUS_CHG_DEBUG
	.user_fw_upgrade = rk826_user_fw_upgrade,
#endif
};
#ifdef CONFIG_OPLUS_CHG_OOS
static void register_warp_devinfo(struct oplus_warp_chip *chip)
{
	static char manu_name[255];
	static char fw_id[255];

	snprintf(fw_id, 255, "0x%x", chip->fw_data_version);

	snprintf(manu_name, 255, "%s", "RK826");

	push_component_info(FAST_CHARGE, fw_id, manu_name);
}
#else
static void register_warp_devinfo(void)
{
	int ret = 0;
	char *version;
	char *manufacture;

	version = "rk826";
	manufacture = "ROCKCHIP";

	ret = register_device_proc("warp", version, manufacture);
	if (ret) {
		chg_err(" fail\n");
	}
}
#endif
static void rk826_shutdown(struct i2c_client *client)
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

static int get_hwpcb_version(void)
{
#ifndef CONFIG_OPLUS_CHG_OOS
	int pcb;

	pcb = get_PCB_Version();
	printk("pcb version is %d\n",pcb);
	if(pcb > 4) //pcb version 4 is evt1
		return 1;
	else
		return 0;
#else
	return 0;
#endif
}

#ifdef OPLUS_CHG_OP_DEF
static bool rk826_is_used(struct i2c_client *client)
{
	struct oplus_chg_asic *asic = i2c_get_clientdata(client);
	struct oplus_warp_chip *chip = asic->data;
	u8 value_buf[2] = {0};
	u32 value = 0;
	int rc = 0;

	opchg_set_clock_active(chip);
	msleep(10);
	opchg_set_reset_active_force(chip);
	msleep(2500);
	opchg_set_clock_sleep(chip);

	rc = oplus_i2c_dma_read(chip->client, 0x52f8, 2, value_buf);
	if (rc < 0) {
		pr_err("rk826 read register 0x52f8 fail, rc = %d\n", rc);
		return false;
	} else {
		pr_err("register 0x52f8: 0x%x, 0x%x\n", value_buf[0], value_buf[1]);
		value = value_buf[0] | (value_buf[1] << 8);
		pr_err("register 0x52f8: 0x%x\n", value);
		if (value == 0x826A) {
			pr_err("rk826 detected, register 0x52f8: 0x%x\n", value);
			msleep(5);
			opchg_set_reset_active(chip);
			opchg_set_reset_sleep(chip);
			chip->mcu_hwid_type = OPLUS_WARP_ASIC_HWID_RK826;
			return true;
		}
	}
	msleep(5);
	opchg_set_reset_active(chip);
	opchg_set_reset_sleep(chip);

	return false;
}
#endif

static int rk826_driver_probe(struct i2c_client *client, const struct i2c_device_id *id)
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
	asic->vops = &oplus_rk826_ops;
	asic->init_proc_warp_fw_check = init_proc_warp_fw_check;
	asic->register_warp_devinfo = register_warp_devinfo;
	asic->is_used = rk826_is_used;
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
	if (get_warp_mcu_type(chip) != OPLUS_WARP_ASIC_HWID_RK826) {
		chg_err("It is not rk826\n");
		return -ENOMEM;
	}

	chip->pcb_version = g_hw_version;
	chip->warp_fw_check = false;
	mutex_init(&chip->pinctrl_mutex);

	oplus_warp_fw_type_dt(chip);
	if (chip->batt_type_4400mv) {
		chip->firmware_data = rk826_fw_data_4400_warp_ffc_15c;
		chip->fw_data_count = sizeof(rk826_fw_data_4400_warp_ffc_15c);
		chip->fw_data_version = rk826_fw_data_4400_warp_ffc_15c[chip->fw_data_count - 4];
	} else {//default
		chip->firmware_data = rk826_fw_data_4400_warp_ffc_15c;
		chip->fw_data_count = sizeof(rk826_fw_data_4400_warp_ffc_15c);
		chip->fw_data_version = rk826_fw_data_4400_warp_ffc_15c[chip->fw_data_count - 4];
	}

	if (chip->warp_fw_type == WARP_FW_TYPE_RK826_4400_WARP_FFC_15C) {
		chip->firmware_data = rk826_fw_data_4400_warp_ffc_15c;
		chip->fw_data_count = sizeof(rk826_fw_data_4400_warp_ffc_15c);
		chip->fw_data_version = rk826_fw_data_4400_warp_ffc_15c[chip->fw_data_count - 4];
	} else if (chip->warp_fw_type == WARP_FW_TYPE_RK826_4450_WARP_FFC_5V6A_4BIT) {
		chip->firmware_data = rk826_fw_data_4450_warp_ffc_5v6a_4bit;
		chip->fw_data_count = sizeof(rk826_fw_data_4450_warp_ffc_5v6a_4bit);
		chip->fw_data_version = rk826_fw_data_4450_warp_ffc_5v6a_4bit[chip->fw_data_count - 4];
	} else if (chip->warp_fw_type == WARP_FW_TYPE_RK826_4450_SWARP_FFC_5V6A_CHAKA) {
		chip->firmware_data = rk826_fw_data_4450_swarp_ffc_5v6a_chaka;
		chip->fw_data_count = sizeof(rk826_fw_data_4450_swarp_ffc_5v6a_chaka);
		chip->fw_data_version = rk826_fw_data_4450_swarp_ffc_5v6a_chaka[chip->fw_data_count - 4];
	} else if (chip->warp_fw_type == WARP_FW_TYPE_RK826_4450_SWARP_FFC_6300MA_HIMA) {
		chip->firmware_data = rk826_fw_data_4450_swarp_ffc_6300mA_hima;
		chip->fw_data_count = sizeof(rk826_fw_data_4450_swarp_ffc_6300mA_hima);
		chip->fw_data_version = rk826_fw_data_4450_swarp_ffc_6300mA_hima[chip->fw_data_count - 4];
	} else if (chip->warp_fw_type == WARP_FW_TYPE_RK826_4450_SWARP_FFC_6300MA_HOREE) {
		chip->firmware_data = rk826_fw_data_4450_swarp_ffc_6300mA_horee;
		chip->fw_data_count = sizeof(rk826_fw_data_4450_swarp_ffc_6300mA_horee);
		chip->fw_data_version = rk826_fw_data_4450_swarp_ffc_6300mA_horee[chip->fw_data_count - 4];
	} else if (chip->warp_fw_type == WARP_FW_TYPE_RK826_4450_SWARP_6300MA && !get_hwpcb_version()) {
		chip->firmware_data = rk826_fw_data_4450_swarp_6300ma;
		chip->fw_data_count = sizeof(rk826_fw_data_4450_swarp_6300ma);
		chip->fw_data_version = rk826_fw_data_4450_swarp_6300ma[chip->fw_data_count - 4];
	} else if (chip->warp_fw_type == WARP_FW_TYPE_RK826_4450_SWARP_6300MA && get_hwpcb_version()) {
		chip->firmware_data = rk826_fw_data_4450_swarp_evt2_4500ma;
		chip->fw_data_count = sizeof(rk826_fw_data_4450_swarp_evt2_4500ma);
		chip->fw_data_version = rk826_fw_data_4450_swarp_evt2_4500ma[chip->fw_data_count - 4];
	} else if (chip->warp_fw_type == WARP_FW_TYPE_RK826_4450_SWARP_FFC_5V6A_DALI) {
		chip->firmware_data = rk826_fw_data_4450_swarp_ffc_5v6a_dali;
		chip->fw_data_count = sizeof(rk826_fw_data_4450_swarp_ffc_5v6a_dali);
		chip->fw_data_version = rk826_fw_data_4450_swarp_ffc_5v6a_dali[chip->fw_data_count - 4];
	} else if (chip->warp_fw_type == WARP_FW_TYPE_RK826_4500_SWARP_FFC_6300MA_LEMONADE) {
		chip->firmware_data = rk826_fw_data_4500_swarp_ffc_6300mA_lemonade;
		chip->fw_data_count = sizeof(rk826_fw_data_4500_swarp_ffc_6300mA_lemonade);
		chip->fw_data_version = rk826_fw_data_4500_swarp_ffc_6300mA_lemonade[chip->fw_data_count - 4];
	}


	chip->vops = &oplus_rk826_ops;
	chip->fw_mcu_version = 0;

	oplus_warp_gpio_dt_init(chip);
	opchg_set_clock_sleep(chip);
	oplus_warp_delay_reset_mcu_init(chip);
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0))
	rk826_update_wake_lock = wakeup_source_register("rk826_update_wake_lock");
#else
	rk826_update_wake_lock = wakeup_source_register(NULL, "rk826_update_wake_lock");
#endif
	if (chip->warp_fw_update_newmethod) {
		if (oplus_is_rf_ftm_mode()) {
			oplus_warp_fw_update_work_init(chip);
		}
	} else {
		oplus_warp_fw_update_work_init(chip);
	}

	oplus_warp_init(chip);
#ifdef CONFIG_OPLUS_CHG_OOS
	register_warp_devinfo(chip);
#else
	register_warp_devinfo();
#endif

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
		asic->firmware_data = rk826_fw_data_4400_warp_ffc_15c;
		asic->fw_data_count = sizeof(rk826_fw_data_4400_warp_ffc_15c);
		asic->fw_data_version = rk826_fw_data_4400_warp_ffc_15c[asic->fw_data_count - 4];
	} else {//default
		asic->firmware_data = rk826_fw_data_4400_warp_ffc_15c;
		asic->fw_data_count = sizeof(rk826_fw_data_4400_warp_ffc_15c);
		asic->fw_data_version = rk826_fw_data_4400_warp_ffc_15c[asic->fw_data_count - 4];
	}

	if (asic->warp_fw_type == WARP_FW_TYPE_RK826_4400_WARP_FFC_15C) {
		asic->firmware_data = rk826_fw_data_4400_warp_ffc_15c;
		asic->fw_data_count = sizeof(rk826_fw_data_4400_warp_ffc_15c);
		asic->fw_data_version = rk826_fw_data_4400_warp_ffc_15c[asic->fw_data_count - 4];
	} else if (asic->warp_fw_type == WARP_FW_TYPE_RK826_4450_WARP_FFC_5V6A_4BIT) {
		asic->firmware_data = rk826_fw_data_4450_warp_ffc_5v6a_4bit;
		asic->fw_data_count = sizeof(rk826_fw_data_4450_warp_ffc_5v6a_4bit);
		asic->fw_data_version = rk826_fw_data_4450_warp_ffc_5v6a_4bit[asic->fw_data_count - 4];
	} else if (asic->warp_fw_type == WARP_FW_TYPE_RK826_4450_SWARP_FFC_5V6A_CHAKA) {
		asic->firmware_data = rk826_fw_data_4450_swarp_ffc_5v6a_chaka;
		asic->fw_data_count = sizeof(rk826_fw_data_4450_swarp_ffc_5v6a_chaka);
		asic->fw_data_version = rk826_fw_data_4450_swarp_ffc_5v6a_chaka[asic->fw_data_count - 4];
	} else if (asic->warp_fw_type == WARP_FW_TYPE_RK826_4450_SWARP_FFC_6300MA_HIMA) {
		asic->firmware_data = rk826_fw_data_4450_swarp_ffc_6300mA_hima;
		asic->fw_data_count = sizeof(rk826_fw_data_4450_swarp_ffc_6300mA_hima);
		asic->fw_data_version = rk826_fw_data_4450_swarp_ffc_6300mA_hima[asic->fw_data_count - 4];
	} else if (asic->warp_fw_type == WARP_FW_TYPE_RK826_4450_SWARP_FFC_6300MA_HOREE) {
		asic->firmware_data = rk826_fw_data_4450_swarp_ffc_6300mA_horee;
		asic->fw_data_count = sizeof(rk826_fw_data_4450_swarp_ffc_6300mA_horee);
		asic->fw_data_version = rk826_fw_data_4450_swarp_ffc_6300mA_horee[asic->fw_data_count - 4];
	} else if (asic->warp_fw_type == WARP_FW_TYPE_RK826_4450_SWARP_6300MA && !get_hwpcb_version()) {
		asic->firmware_data = rk826_fw_data_4450_swarp_6300ma;
		asic->fw_data_count = sizeof(rk826_fw_data_4450_swarp_6300ma);
		asic->fw_data_version = rk826_fw_data_4450_swarp_6300ma[asic->fw_data_count - 4];
	} else if (asic->warp_fw_type == WARP_FW_TYPE_RK826_4450_SWARP_6300MA && get_hwpcb_version()) {
		asic->firmware_data = rk826_fw_data_4450_swarp_evt2_4500ma;
		asic->fw_data_count = sizeof(rk826_fw_data_4450_swarp_evt2_4500ma);
		asic->fw_data_version = rk826_fw_data_4450_swarp_evt2_4500ma[asic->fw_data_count - 4];
	} else if (asic->warp_fw_type == WARP_FW_TYPE_RK826_4450_SWARP_FFC_5V6A_DALI) {
		asic->firmware_data = rk826_fw_data_4450_swarp_ffc_5v6a_dali;
		asic->fw_data_count = sizeof(rk826_fw_data_4450_swarp_ffc_5v6a_dali);
		asic->fw_data_version = rk826_fw_data_4450_swarp_ffc_5v6a_dali[asic->fw_data_count - 4];
	} else if (asic->warp_fw_type == WARP_FW_TYPE_RK826_4500_SWARP_FFC_6300MA_LEMONADE) {
		asic->firmware_data = rk826_fw_data_4500_swarp_ffc_6300mA_lemonade;
		asic->fw_data_count = sizeof(rk826_fw_data_4500_swarp_ffc_6300mA_lemonade);
		asic->fw_data_version = rk826_fw_data_4500_swarp_ffc_6300mA_lemonade[asic->fw_data_count - 4];
	}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0))
	rk826_update_wake_lock = wakeup_source_register("rk826_update_wake_lock");
#else
	rk826_update_wake_lock = wakeup_source_register(NULL, "rk826_update_wake_lock");
#endif
#endif /* OPLUS_CHG_OP_DEF */
	the_chip = chip;
#ifdef OPLUS_CHG_OP_DEF
	oplus_chg_asic_register(asic);
	schedule_delayed_work(&chip->asic_init_work, 0);
#endif
	chg_debug("rk826 success\n");
	return 0;
}

/**********************************************************
  *
  *   [platform_driver API]
  *
  *********************************************************/
static const struct of_device_id rk826_match[] = {
	{ .compatible = "oplus,rk826-fastcg"},
	{ },
};

static const struct i2c_device_id rk826_id[] = {
	{"rk826-fastcg", 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, rk826_id);

struct i2c_driver rk826_i2c_driver = {
	.driver = {
		.name = "rk826-fastcg",
		.owner        = THIS_MODULE,
		.of_match_table = rk826_match,
	},
	.probe = rk826_driver_probe,
	.shutdown = rk826_shutdown,
	.id_table = rk826_id,
};

#ifndef OPLUS_CHG_OP_DEF
static int __init rk826_subsys_init(void)
{
	int ret = 0;
	chg_debug(" init start\n");
	init_hw_version();

	if (i2c_add_driver(&rk826_i2c_driver) != 0) {
		chg_err(" failed to register rk826 i2c driver.\n");
	} else {
		chg_debug(" Success to register rk826 i2c driver.\n");
	}
	return ret;
}

subsys_initcall(rk826_subsys_init);
#else
static __init int rk826_driver_init(void)
{
	init_hw_version();
	return i2c_add_driver(&rk826_i2c_driver);
}

static __exit void rk826_driver_exit(void)
{
	i2c_del_driver(&rk826_i2c_driver);
}
oplus_chg_module_register(rk826_driver);
#endif
#ifndef MODULE
MODULE_DESCRIPTION("Driver for oplus warp rk826 fast mcu");
MODULE_LICENSE("GPL v2");
#endif

