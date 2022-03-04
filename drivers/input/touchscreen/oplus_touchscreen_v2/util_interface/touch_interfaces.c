// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include <linux/i2c.h>
#include <linux/spi/spi.h>
#include <linux/delay.h>
#include <linux/uaccess.h>

#include "touch_interfaces.h"
#include "../touchpanel_common.h"
#include "../touch_comon_api/touch_comon_api.h"

#define FIX_I2C_LENGTH   256

#ifdef TPD_DEVICE
#undef TPD_DEVICE
#define TPD_DEVICE "touch_interface"
#else
#define TPD_DEVICE "touch_interface"
#endif

/*******Part1: IIC Area********************************/

/**
 * touch_i2c_continue_read - Using for "read sequence bytes" through IIC
 * @client: Handle to slave device
 * @length: data size we want to read
 * @data: data read from IIC
 *
 * Actully, This function call i2c_transfer for IIC transfer,
 * Returning transfer length(transfer success) or most likely negative errno(transfer error)
 */
int touch_i2c_continue_read(struct i2c_client *client, unsigned short length,
			    unsigned char *data)
{
	int retval;
	unsigned char retry;
	struct i2c_msg msg;

	msg.addr = client->addr;
	msg.flags = I2C_M_RD;
	msg.len = length;
	msg.buf = data;

	for (retry = 0; retry < MAX_I2C_RETRY_TIME; retry++) {
		if (i2c_transfer(client->adapter, &msg, 1) == 1) {
			retval = length;
			break;
		}

		msleep(20);
	}

	if (retry == MAX_I2C_RETRY_TIME) {
		TPD_INFO("%s: I2C read over retry limit\n", __func__);
		retval = -EIO;
	}

	return retval;
}
EXPORT_SYMBOL(touch_i2c_continue_read);

/**
 * touch_i2c_read_block - Using for "read word" through IIC
 * @client: Handle to slave device
 * @addr: addr to write
 * @length: data size we want to send
 * @data: data we want to send
 *
 * Actully, This function call i2c_transfer for IIC transfer,
 * Returning transfer length(transfer success) or most likely negative errno(transfer error)
 */
int touch_i2c_read_block(struct i2c_client *client, u16 addr,
			 unsigned short length, unsigned char *data)
{
	int retval;
	unsigned char buffer[2] = {(addr >> 8) & 0xff, addr & 0xff};
	struct touchpanel_data *ts = i2c_get_clientdata(client);

	if (!ts->interface_data.register_is_16bit) { /* if register is 8bit*/
		retval = touch_i2c_read(client, &buffer[1], 1, data, length);

	} else {
		retval = touch_i2c_read(client, buffer, 2, data, length);
	}

	return retval;
}
EXPORT_SYMBOL(touch_i2c_read_block);

/**
 * touch_i2c_continue_write - Using for "write sequence bytes" through IIC
 * @client: Handle to slave device
 * @length: data size we want to write
 * @data: data write to IIC
 *
 * Actully, This function call i2c_transfer for IIC transfer,
 * Returning transfer length(transfer success) or most likely negative errno(transfer error)
 */
int touch_i2c_continue_write(struct i2c_client *client, unsigned short length,
			     unsigned char *data)
{
	int retval;
	unsigned char retry;
	struct i2c_msg msg;

	msg.addr = client->addr;
	msg.flags = 0;
	msg.buf = data;
	msg.len = length;

	for (retry = 0; retry < MAX_I2C_RETRY_TIME; retry++) {
		if (i2c_transfer(client->adapter, &msg, 1) == 1) {
			retval = length;
			break;
		}

		msleep(20);
	}

	if (retry == MAX_I2C_RETRY_TIME) {
		TPD_INFO("%s: I2C write over retry limit\n", __func__);
		retval = -EIO;
	}

	return retval;
}
EXPORT_SYMBOL(touch_i2c_continue_write);

/**
 * touch_i2c_write_block - Using for "read word" through IIC
 * @client: Handle to slave device
 * @addr: addr to write
 * @length: data size we want to send
 * @data: data we want to send
 *
 * Actully, This function call i2c_transfer for IIC transfer,
 * Returning transfer length(transfer success) or most likely negative errno(transfer error)
 */
int touch_i2c_write_block(struct i2c_client *client, u16 addr,
			  unsigned short length, unsigned char const *data)
{
	int retval;
	unsigned char retry;
	unsigned int total_length = 0;
	struct i2c_msg msg[1];
	struct touchpanel_data *ts = i2c_get_clientdata(client);

	mutex_lock(&ts->interface_data.bus_mutex);

	total_length = length + (ts->interface_data.register_is_16bit ? 2 : 1);

	if (total_length > FIX_I2C_LENGTH) {
		if (ts->interface_data.write_buf_size < total_length) {
			if (ts->interface_data.write_buf) {
				tp_devm_kfree(&client->dev, (void **)&ts->interface_data.write_buf,
					      ts->interface_data.write_buf_size);
				TPD_INFO("write block_1, free once.\n");
			}

			ts->interface_data.write_buf = tp_devm_kzalloc(&client->dev, total_length,
						       GFP_KERNEL | GFP_DMA);

			if (!ts->interface_data.write_buf) {
				ts->interface_data.write_buf_size = 0;
				TPD_INFO("write block_1, kzalloc failed(len:%d, buf_size:%d).\n", total_length,
					 ts->interface_data.write_buf_size);
				mutex_unlock(&ts->interface_data.bus_mutex);
				return -ENOMEM;
			}

			ts->interface_data.write_buf_size = total_length;
			TPD_INFO("write block_1, kzalloc success(len:%d, buf_size:%d).\n", total_length,
				 ts->interface_data.write_buf_size);

		} else {
			memset(ts->interface_data.write_buf, 0, total_length);
		}

	} else {
		if (ts->interface_data.write_buf_size > FIX_I2C_LENGTH) {
			tp_devm_kfree(&client->dev, (void **)&ts->interface_data.write_buf,
				      ts->interface_data.write_buf_size);
			ts->interface_data.write_buf = tp_devm_kzalloc(&client->dev, FIX_I2C_LENGTH,
						       GFP_KERNEL | GFP_DMA);

			if (!ts->interface_data.write_buf) {
				ts->interface_data.write_buf_size = 0;
				TPD_INFO("write block_2, kzalloc failed(len:%d, buf_size:%d).\n", total_length,
					 ts->interface_data.write_buf_size);
				mutex_unlock(&ts->interface_data.bus_mutex);
				return -ENOMEM;
			}

			ts->interface_data.write_buf_size = FIX_I2C_LENGTH;
			TPD_INFO("write block_2, kzalloc success(len:%d, buf_size:%d).\n", total_length,
				 ts->interface_data.write_buf_size);

		} else {
			if (!ts->interface_data.write_buf) {
				ts->interface_data.write_buf = tp_devm_kzalloc(&client->dev, FIX_I2C_LENGTH,
							       GFP_KERNEL | GFP_DMA);

				if (!ts->interface_data.write_buf) {
					ts->interface_data.write_buf_size = 0;
					TPD_INFO("write block_3, kzalloc failed(len:%d, buf_size:%d).\n", total_length,
						 ts->interface_data.write_buf_size);
					mutex_unlock(&ts->interface_data.bus_mutex);
					return -ENOMEM;
				}

				ts->interface_data.write_buf_size = FIX_I2C_LENGTH;
				TPD_INFO("write block_3, kzalloc success(len:%d, buf_size:%d).\n", total_length,
					 ts->interface_data.write_buf_size);

			} else {
				memset(ts->interface_data.write_buf, 0, total_length);
			}
		}
	}

	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].buf = ts->interface_data.write_buf;

	if (!ts->interface_data.register_is_16bit) { /* if register is 8bit*/
		msg[0].len = length + 1;
		msg[0].buf[0] = addr & 0xff;

		memcpy(&ts->interface_data.write_buf[1], &data[0], length);

	} else {
		msg[0].len = length + 2;
		msg[0].buf[0] = (addr >> 8) & 0xff;
		msg[0].buf[1] = addr & 0xff;

		memcpy(&ts->interface_data.write_buf[2], &data[0], length);
	}

	for (retry = 0; retry < MAX_I2C_RETRY_TIME; retry++) {
		if (i2c_transfer(client->adapter, msg, 1) == 1) {
			retval = length;
			break;
		}

		msleep(20);
	}

	if (retry == MAX_I2C_RETRY_TIME) {
		TPD_INFO("%s: I2C write over retry limit\n", __func__);
		retval = -EIO;
	}

	mutex_unlock(&ts->interface_data.bus_mutex);
	return retval;
}
EXPORT_SYMBOL(touch_i2c_write_block);

/**
 * touch_i2c_read_byte - Using for "read word" through IIC
 * @client: Handle to slave device
 * @addr: addr to read
 *
 * Actully, This function call touch_i2c_read_block for IIC transfer,
 * Returning zero(transfer success) or most likely negative errno(transfer error)
 */
int touch_i2c_read_byte(struct i2c_client *client, unsigned short addr)
{
	int retval = 0;
	unsigned char buf[2] = {0};
	struct touchpanel_data *ts = i2c_get_clientdata(client);

	if (!client) {
		dump_stack();
		return -1;
	}

	if (!ts->interface_data.register_is_16bit) {
		buf[0] = addr & 0xff;
		retval = touch_i2c_read(client, buf, 1, buf, 1);

	} else {
		buf[0] = addr >> 8 & 0xff;
		buf[1] = addr & 0xff;
		retval = touch_i2c_read(client, buf, 2, buf, 1);
	}

	if (retval >= 0) {
		retval = buf[0] & 0xff;
	}

	return retval;
}
EXPORT_SYMBOL(touch_i2c_read_byte);

/**
 * touch_i2c_write_byte - Using for "read word" through IIC
 * @client: Handle to slave device
 * @addr: addr to write
 * @data: data we want to send
 *
 * Actully, This function call touch_i2c_write_block for IIC transfer,
 * Returning zero(transfer success) or most likely negative errno(transfer error)
 */
int touch_i2c_write_byte(struct i2c_client *client, unsigned short addr,
			 unsigned char data)
{
	int retval;
	int length_trans = 1;
	unsigned char data_send = data;

	if (!client) {
		dump_stack();
		return -EINVAL;
	}

	retval = touch_i2c_write_block(client, addr, length_trans, &data_send);

	if (retval == length_trans) {
		retval = 0;
	}

	return retval;
}
EXPORT_SYMBOL(touch_i2c_write_byte);

/**
 * touch_i2c_read_word - Using for "read word" through IIC
 * @client: Handle to slave device
 * @addr: addr to write
 * @data: data we want to read
 *
 * Actully, This func call touch_i2c_Read_block for IIC transfer,
 * Returning negative errno else a 16-bit unsigned "word" received from the device.
 */
int touch_i2c_read_word(struct i2c_client *client, unsigned short addr)
{
	int retval;
	unsigned char buf[2] = {0};
	struct touchpanel_data *ts = i2c_get_clientdata(client);

	if (!client) {
		dump_stack();
		return -1;
	}

	if (!ts->interface_data.register_is_16bit) {
		buf[0] = addr & 0xff;
		retval = touch_i2c_read(client, buf, 1, buf, 2);

	} else {
		buf[0] = addr >> 8 & 0xff;
		buf[1] = addr & 0xff;
		retval = touch_i2c_read(client, buf, 2, buf, 2);
	}

	if (retval >= 0) {
		retval = buf[1] << 8 | buf[0];
	}

	return retval;
}
EXPORT_SYMBOL(touch_i2c_read_word);

/**
 * touch_i2c_write_word - Using for "read word" through IIC
 * @client: Handle to slave device
 * @addr: addr to write
 * @data: data we want to send
 *
 * Actully, This function call touch_i2c_write_block for IIC transfer,
 * Returning zero(transfer success) or most likely negative errno(transfer error)
 */
int touch_i2c_write_word(struct i2c_client *client, unsigned short addr,
			 unsigned short data)
{
	int retval;
	int length_trans = 2;
	unsigned char buf[2] = {data & 0xff, (data >> 8) & 0xff};

	if (!client) {
		TPD_INFO("%s: no client.\n", __func__);
		return -EINVAL;
	}

	retval = touch_i2c_write_block(client, addr, length_trans, buf);

	if (retval == length_trans) {
		retval = 0;
	}

	return retval;
}
EXPORT_SYMBOL(touch_i2c_write_word);

/**
 * touch_i2c_read - Using for "read data from ic after writing or not" through IIC
 * @client: Handle to slave device
 * @writebuf: buf to write
 * @writelen: data size we want to send
 * @readbuf:  buf we want save data
 * @readlen:  data size we want to receive
 *
 * Actully, This function call i2c_transfer for IIC transfer,
 * Returning transfer msg length(transfer success) or most likely negative errno(transfer EIO error)
 */
inline int touch_i2c_read(struct i2c_client *client, char *writebuf,
			  unsigned short writelen, char *readbuf, unsigned short readlen)
{
	int retval;
	unsigned char retry;
	struct i2c_msg msg[2];
	struct i2c_msg message;
	struct touchpanel_data *ts = i2c_get_clientdata(client);

	mutex_lock(&ts->interface_data.bus_mutex);

	/*for writebuf buffer min is  FIX_I2C_LENGTH*/
	if (writelen > FIX_I2C_LENGTH) {
		if (ts->interface_data.read_w_buf_size < writelen) {
			if (ts->interface_data.read_w_buffer) {
				tp_devm_kfree(&client->dev, (void **)&ts->interface_data.read_w_buffer,
					      ts->interface_data.read_w_buf_size);
				TPD_INFO("read w block_1, free once.\n");
			}

			ts->interface_data.read_w_buffer = tp_devm_kzalloc(&client->dev, writelen,
							   GFP_KERNEL | GFP_DMA);

			if (!ts->interface_data.read_w_buffer) {
				ts->interface_data.read_w_buf_size = 0;
				TPD_INFO("read w block_1, kzalloc failed(len:%d, buf_size:%d).\n", writelen,
					 ts->interface_data.read_w_buf_size);
				mutex_unlock(&ts->interface_data.bus_mutex);
				return -ENOMEM;
			}

			ts->interface_data.read_w_buf_size = writelen;
			TPD_INFO("read w block_1, kzalloc success(len:%d, buf_size:%d).\n", writelen,
				 ts->interface_data.read_w_buf_size);

		} else {
			memset(ts->interface_data.read_w_buffer, 0, writelen);
		}

	} else {
		if (ts->interface_data.read_w_buf_size > FIX_I2C_LENGTH) {
			tp_devm_kfree(&client->dev, (void **)&ts->interface_data.read_w_buffer,
				      ts->interface_data.read_w_buf_size);
			ts->interface_data.read_w_buffer = tp_devm_kzalloc(&client->dev, FIX_I2C_LENGTH,
							   GFP_KERNEL | GFP_DMA);

			if (!ts->interface_data.read_w_buffer) {
				ts->interface_data.read_w_buf_size = 0;
				TPD_INFO("read w block_2, kzalloc failed(len:%d, buf_size:%d).\n", writelen,
					 ts->interface_data.read_w_buf_size);
				mutex_unlock(&ts->interface_data.bus_mutex);
				return -ENOMEM;
			}

			ts->interface_data.read_w_buf_size = FIX_I2C_LENGTH;
			TPD_INFO("read w block_2, kzalloc success(len:%d, buf_size:%d).\n", writelen,
				 ts->interface_data.read_w_buf_size);

		} else {
			if (!ts->interface_data.read_w_buffer) {
				ts->interface_data.read_w_buffer = tp_devm_kzalloc(&client->dev, FIX_I2C_LENGTH,
								   GFP_KERNEL | GFP_DMA);

				if (!ts->interface_data.read_w_buffer) {
					ts->interface_data.read_w_buf_size = 0;
					TPD_INFO("read w block_3, kzalloc failed(len:%d, buf_size:%d).\n", writelen,
						 ts->interface_data.read_w_buf_size);
					mutex_unlock(&ts->interface_data.bus_mutex);
					return -ENOMEM;
				}

				ts->interface_data.read_w_buf_size = FIX_I2C_LENGTH;
				TPD_INFO("read w block_3, kzalloc success(len:%d, buf_size:%d).\n", writelen,
					 ts->interface_data.read_w_buf_size);

			} else {
				memset(ts->interface_data.read_w_buffer, 0, writelen);
			}
		}
	}

	memcpy(ts->interface_data.read_w_buffer, writebuf, writelen);

	/*for readbuf buffer min is  FIX_I2C_LENGTH*/
	if (readlen > FIX_I2C_LENGTH) {
		if (ts->interface_data.read_buf_size < readlen) {
			if (ts->interface_data.read_buf) {
				tp_devm_kfree(&client->dev, (void **)&ts->interface_data.read_buf,
					      ts->interface_data.read_buf_size);
				TPD_INFO("read block_1, free once.\n");
			}

			ts->interface_data.read_buf = tp_devm_kzalloc(&client->dev, readlen,
						      GFP_KERNEL | GFP_DMA);

			if (!ts->interface_data.read_buf) {
				ts->interface_data.read_buf_size = 0;
				TPD_INFO("read block_1, kzalloc failed(len:%d, buf_size:%d).\n", readlen,
					 ts->interface_data.read_buf_size);
				mutex_unlock(&ts->interface_data.bus_mutex);
				return -ENOMEM;
			}

			ts->interface_data.read_buf_size = readlen;
			TPD_INFO("read block_1, kzalloc success(len:%d, buf_size:%d).\n", readlen,
				 ts->interface_data.read_buf_size);

		} else {
			memset(ts->interface_data.read_buf, 0, readlen);
		}

	} else {
		if (ts->interface_data.read_buf_size > FIX_I2C_LENGTH) {
			tp_devm_kfree(&client->dev, (void **)&ts->interface_data.read_buf,
				      ts->interface_data.read_buf_size);
			ts->interface_data.read_buf = tp_devm_kzalloc(&client->dev, FIX_I2C_LENGTH,
						      GFP_KERNEL | GFP_DMA);

			if (!ts->interface_data.read_buf) {
				ts->interface_data.read_buf_size = 0;
				TPD_INFO("read block_2, kzalloc failed(len:%d, buf_size:%d).\n", readlen,
					 ts->interface_data.read_buf_size);
				mutex_unlock(&ts->interface_data.bus_mutex);
				return -ENOMEM;
			}

			ts->interface_data.read_buf_size = FIX_I2C_LENGTH;
			TPD_INFO("read block_2, kzalloc success(len:%d, buf_size:%d).\n", readlen,
				 ts->interface_data.read_buf_size);

		} else {
			if (!ts->interface_data.read_buf) {
				ts->interface_data.read_buf = tp_devm_kzalloc(&client->dev, FIX_I2C_LENGTH,
							      GFP_KERNEL | GFP_DMA);

				if (!ts->interface_data.read_buf) {
					ts->interface_data.read_buf_size = 0;
					TPD_INFO("read block_3, kzalloc failed(len:%d, buf_size:%d).\n", readlen,
						 ts->interface_data.read_buf_size);
					mutex_unlock(&ts->interface_data.bus_mutex);
					return -ENOMEM;
				}

				ts->interface_data.read_buf_size = FIX_I2C_LENGTH;
				TPD_INFO("read block_3, kzalloc success(len:%d, buf_size:%d).\n", readlen,
					 ts->interface_data.read_buf_size);

			} else {
				memset(ts->interface_data.read_buf, 0, readlen);
			}
		}
	}
	if (writelen > 0) {
		msg[0].addr = client->addr;
		msg[0].flags = 0;
		msg[0].buf = ts->interface_data.read_w_buffer;
		msg[0].len = writelen;

		msg[1].addr = client->addr;
		msg[1].flags = I2C_M_RD;
		msg[1].len = readlen;
		msg[1].buf = ts->interface_data.read_buf;

		for (retry = 0; retry < MAX_I2C_RETRY_TIME; retry++) {
			if (i2c_transfer(client->adapter, msg, 2) == 2) {
				retval = readlen;
				break;
			}

			msleep(20);
		}
	} else {
		message.addr = client->addr;
		message.flags = I2C_M_RD;
		message.len = readlen;
		message.buf = ts->interface_data.read_buf;
		for (retry = 0; retry < MAX_I2C_RETRY_TIME; retry++) {
			if (i2c_transfer(client->adapter, &message, 1) == 1) {
				retval = 1;
				break;
			}

			msleep(20);
		}
	}

	if (retry == MAX_I2C_RETRY_TIME) {
		TPD_INFO("%s: I2C read over retry limit\n", __func__);
		retval = -EIO;
	}

	memcpy(readbuf, ts->interface_data.read_buf, readlen);

	mutex_unlock(&ts->interface_data.bus_mutex);
	return retval;
}
EXPORT_SYMBOL(touch_i2c_read);

/**
 * touch_i2c_write - Using for "write data to ic" through IIC
 * @client: Handle to slave device
 * @writebuf: buf data wo want to send
 * @writelen: data size we want to send
 *
 * Actully, This function call i2c_transfer for IIC transfer,
 * Returning transfer msg length(transfer success) or most likely negative errno(transfer EIO error)
 */
inline int touch_i2c_write(struct i2c_client *client, char *writebuf,
			   unsigned short writelen)
{
	int retval;
	u16 addr;
	struct touchpanel_data *ts = i2c_get_clientdata(client);

	if (!ts->interface_data.register_is_16bit) {
		addr = writebuf[0] & 0xff;
		retval = touch_i2c_write_block(client, addr, writelen - 1, &writebuf[1]);

	} else {
		addr = writebuf[0] << 8 & writebuf[1];
		retval = touch_i2c_write_block(client, addr, writelen - 2, &writebuf[2]);
	}

	return retval;
}
EXPORT_SYMBOL(touch_i2c_write);

/**
 * init_touch_interfaces - Using for Register IIC interface
 * @dev: i2c_client->dev using to alloc memory for dma transfer
 * @flag_register_16bit: bool param to detect whether this device using 16bit IIC address or 8bit address
 *
 * Actully, This function don't have many operation, we just detect device address length && alloc DMA memory for MTK platform
 * Returning zero(sucess) or -ENOMEM(memory alloc failed)
 */
int init_touch_interfaces(struct device *dev, bool flag_register_16bit)
{
	struct touchpanel_data *ts = dev_get_drvdata(dev);

	if (!ts) {
		return -1;
	}

	ts->interface_data.register_is_16bit = flag_register_16bit;
	mutex_init(&ts->interface_data.bus_mutex);

	return 0;
}

int free_touch_interfaces(struct device *dev)
{
	struct touchpanel_data *ts = dev_get_drvdata(dev);

	if (!ts) {
		return -1;
	}

	return 0;
}

inline int touch_spi_sync(struct spi_device *spi, struct spi_message *message)
{
	int ret;
	ret = spi_sync(spi, message);
	return ret;
}

int32_t spi_read_write(struct spi_device *client, uint8_t *buf, size_t len,
		       uint8_t *rbuf, SPI_RW rw)
{
	struct spi_message m;
	struct spi_transfer t = {
		.len    = len,
	};
	u8 *tx_buf = NULL;
	u8 *rx_buf = NULL;
	int status;

	switch (rw) {
	case SPIREAD:
		tx_buf = tp_devm_kzalloc(&client->dev, len + DUMMY_BYTES, GFP_KERNEL | GFP_DMA);

		if (!tx_buf) {
			status = -ENOMEM;
			goto spi_out;
		}

		memset(tx_buf, 0xFF, len + DUMMY_BYTES);
		memcpy(tx_buf, buf, len + DUMMY_BYTES);
		rx_buf = tp_devm_kzalloc(&client->dev, len + DUMMY_BYTES, GFP_KERNEL | GFP_DMA);

		if (!rx_buf) {
			status = -ENOMEM;
			goto spi_out;
		}

		memset(rx_buf, 0xFF, len + DUMMY_BYTES);
		t.tx_buf = tx_buf;
		t.rx_buf = rx_buf;
		t.len    = (len + DUMMY_BYTES);
		break;

	case SPIWRITE:
		tx_buf = tp_devm_kzalloc(&client->dev, len, GFP_KERNEL | GFP_DMA);

		if (!tx_buf) {
			status = -ENOMEM;
			goto spi_out;
		}

		memcpy(tx_buf, buf, len);
		t.tx_buf = tx_buf;
		break;
	}

	spi_message_init(&m);
	spi_message_add_tail(&t, &m);
	status = touch_spi_sync(client, &m);

	if (status == 0) {
		if (rw == SPIREAD) {
			memcpy(rbuf, rx_buf, len + DUMMY_BYTES);
		}
	}

spi_out:

	if (tx_buf) {
		tp_devm_kfree(&client->dev, (void **)&tx_buf, len + DUMMY_BYTES);
	}

	if (rx_buf) {
		tp_devm_kfree(&client->dev, (void **)&rx_buf, len + DUMMY_BYTES);
	}

	return status;
}
EXPORT_SYMBOL(spi_read_write);

/*******************************************************
Description:
    Novatek touchscreen spi read function.

return:
    Executive outcomes. 2---succeed. -5---I/O error
*******************************************************/
int32_t CTP_SPI_READ(struct spi_device *client, uint8_t *buf, uint16_t len)
{
	int32_t ret = -1;
	int32_t retries = 0;
	uint8_t rbuf[SPI_TANSFER_LEN + 1] = {0};

	buf[0] = SPI_READ_MASK(buf[0]);

	while (retries < 5) {
		ret = spi_read_write(client, buf, len, rbuf, SPIREAD);

		if (ret == 0) {
			break;
		}

		retries++;
	}

	if (unlikely(retries == 5)) {
		TPD_INFO("read error, ret=%d\n", ret);
		ret = -EIO;

	} else {
		memcpy((buf + 1), (rbuf + 2), (len - 1));
	}

	return ret;
}
EXPORT_SYMBOL(CTP_SPI_READ);

/*******************************************************
Description:
    Novatek touchscreen spi write function.

return:
    Executive outcomes. 1---succeed. -5---I/O error
*******************************************************/
int32_t CTP_SPI_WRITE(struct spi_device *client, uint8_t *buf, uint16_t len)
{
	int32_t ret = -1;
	int32_t retries = 0;

	buf[0] = SPI_WRITE_MASK(buf[0]);

	while (retries < 5) {
		ret = spi_read_write(client, buf, len, NULL, SPIWRITE);

		if (ret == 0) {
			break;
		}

		retries++;
	}

	if (unlikely(retries == 5)) {
		TPD_INFO("error, ret=%d\n", ret);
		ret = -EIO;
	}

	return ret;
}
EXPORT_SYMBOL(CTP_SPI_WRITE);

/*******************************************************
Description:
    Download fw by spi. Please cheak the fw buf is dma!

return:
    Executive outcomes. 0---succeed. -5---I/O error
*******************************************************/
int spi_write_firmware(struct spi_device *client, u8 *fw, u32 *len_array,
		       u8 array_len)
{
	int ret = 0;
	int retry = 0;
	int i = 0;
	u8 *buf = NULL;
	/*unsigned	cs_change:1;*/
	struct spi_message m;
	struct spi_transfer *t;

	t = tp_devm_kzalloc(&client->dev, sizeof(struct spi_transfer) * array_len,
			    GFP_KERNEL | GFP_DMA);

	if (!t) {
		TPD_INFO("error, no mem!");
		return -ENOMEM;
	}

	spi_message_init(&m);
	/*memset(t, 0, sizeof(t));*/

	buf = fw;

	for (i = 0; i < array_len; i++) {
		t[i].len = len_array[i];
		t[i].tx_buf = buf;
		t[i].cs_change = 1;
		spi_message_add_tail(&t[i], &m);
		/*TPD_INFO("i=%d, len=%d, buf[0]=%x\n", i, len_array[i], buf[0]);*/
		buf = buf + len_array[i];
	}

	while (retry < 5) {
		ret = touch_spi_sync(client, &m);

		if (ret == 0) {
			break;
		}

		retry++;
	}

	if (unlikely(retry == 5)) {
		TPD_INFO("error, ret=%d\n", ret);
	}

	tp_devm_kfree(&client->dev, (void **)&t, sizeof(struct spi_transfer)*array_len);
	return ret;
}
EXPORT_SYMBOL(spi_write_firmware);
