#include <linux/module.h>
#include "cam_sensor_cmn_header.h"
#include "cam_actuator_core.h"
#include "cam_sensor_util.h"
#include "cam_trace.h"
#include "cam_common_util.h"
#include "cam_packet_util.h"

#include "oplus_cam_actuator_core.h"
void cam_actuator_i2c_modes_util_oem(
	struct camera_io_master *io_master_info,
	struct i2c_settings_list *i2c_list)
{

	uint32_t value;

	if (i2c_list->i2c_settings.reg_setting[0].reg_addr == 0x0204) {
		value = (i2c_list->i2c_settings.reg_setting[0].reg_data & 0xFF00) >> 8;
		i2c_list->i2c_settings.reg_setting[0].reg_data =
			((i2c_list->i2c_settings.reg_setting[0].reg_data & 0xFF) << 8) | value;
		CAM_DBG(CAM_ACTUATOR, "new value %d",
			i2c_list->i2c_settings.reg_setting[0].reg_data);
	}
}

void cam_actuator_poll_setting_update(struct cam_actuator_ctrl_t *a_ctrl)
{

	struct i2c_settings_list *i2c_list = NULL;

	a_ctrl->is_actuator_ready = true;
	memset(&(a_ctrl->poll_register), 0, sizeof(struct cam_sensor_i2c_reg_array));
	list_for_each_entry(i2c_list,
			    &(a_ctrl->i2c_data.init_settings.list_head), list) {
		if (i2c_list->op_code == CAM_SENSOR_I2C_POLL) {
			a_ctrl->poll_register.reg_addr = i2c_list->i2c_settings.reg_setting[0].reg_addr;
			a_ctrl->poll_register.reg_data = i2c_list->i2c_settings.reg_setting[0].reg_data;
			a_ctrl->poll_register.data_mask =
				i2c_list->i2c_settings.reg_setting[0].data_mask;
			a_ctrl->poll_register.delay =
				100; //i2c_list->i2c_settings.reg_setting[0].delay; // The max delay should be 100
			a_ctrl->addr_type = i2c_list->i2c_settings.addr_type;
			a_ctrl->data_type = i2c_list->i2c_settings.data_type;
		}
	}
}

void cam_actuator_poll_setting_apply(struct cam_actuator_ctrl_t *a_ctrl)
{
	int ret = 0;

	if (!a_ctrl->is_actuator_ready) {
		if (a_ctrl->poll_register.reg_addr || a_ctrl->poll_register.reg_data) {
			ret = camera_io_dev_poll(
				      &(a_ctrl->io_master_info),
				      a_ctrl->poll_register.reg_addr,
				      a_ctrl->poll_register.reg_data,
				      a_ctrl->poll_register.data_mask,
				      a_ctrl->addr_type,
				      a_ctrl->data_type,
				      a_ctrl->poll_register.delay);

			if (ret < 0)
				CAM_ERR(CAM_ACTUATOR, "i2c poll apply setting Fail: %d, is_actuator_ready %d",
					ret, a_ctrl->is_actuator_ready);

			else
				CAM_DBG(CAM_ACTUATOR, "is_actuator_ready %d, ret %d", a_ctrl->is_actuator_ready,
					ret);

			a_ctrl->is_actuator_ready = true; //Just poll one time
		}
	}
}

#define UPDATE_REG_SIZE 64
uint32_t WriteValue1[22] = {0x6F, 0x8D, 0x14, 0x60, 0x56, 0x39, 0x48, 0x48,
			    0x76, 0x10, 0x4F, 0x18, 0x81, 0xFF, 0x78, 0x10,
			    0x00, 0x07, 0x06, 0x31, 0xCB, 0x68
			   };
uint32_t WriteValue2[13] = {0x22, 0xE1, 0x80, 0x00, 0x5B, 0x14, 0xBC, 0x12,
			    0x37, 0xFE, 0xDD, 0xDC, 0xCC
			   };

int RamWriteByte(struct cam_actuator_ctrl_t *a_ctrl,
		 uint32_t addr, uint32_t data, unsigned short mdelay)
{
	int32_t rc = 0;
	int retry = 1;
	int i = 0;
	uint32_t read_data = 0;
	struct cam_sensor_i2c_reg_array i2c_write_setting = {
		.reg_addr = addr,
		.reg_data = data,
		.delay = mdelay,
		.data_mask = 0x00,
	};
	struct cam_sensor_i2c_reg_setting i2c_write = {
		.reg_setting = &i2c_write_setting,
		.size = 1,
		.addr_type = CAMERA_SENSOR_I2C_TYPE_BYTE,
		.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE,
		.delay = mdelay,
	};

	if (a_ctrl == NULL) {
		CAM_ERR(CAM_ACTUATOR, "Invalid Args");
		return -EINVAL;
	}

	read_data = 0;
	rc = camera_io_dev_read(
		     &(a_ctrl->io_master_info),
		     addr, &read_data,
		     CAMERA_SENSOR_I2C_TYPE_BYTE, CAMERA_SENSOR_I2C_TYPE_BYTE);

	if (rc < 0) {
		CAM_ERR(CAM_ACTUATOR, "read error");
		//break;
	}

	CAM_ERR(CAM_ACTUATOR,
		"before write data addr:%x data:%x ,will write data is %x", addr, read_data,
		data);

	for (i = 0; i < retry; i++) {
		rc = camera_io_dev_write(&(a_ctrl->io_master_info), &i2c_write);

		if (rc < 0)
			CAM_ERR(CAM_ACTUATOR, "write 0x%04x failed, retry:%d", addr, i + 1);

		else
			break;
	}

	read_data = 0;
	rc = camera_io_dev_read(
		     &(a_ctrl->io_master_info),
		     addr, &read_data,
		     CAMERA_SENSOR_I2C_TYPE_BYTE, CAMERA_SENSOR_I2C_TYPE_BYTE);

	if (rc < 0) {
		CAM_ERR(CAM_ACTUATOR, "write error");
		//break;
	}

	CAM_ERR(CAM_ACTUATOR, "after write data addr:%x data:%x", addr, read_data);
	return rc;
}
int32_t cam_actuator_update_pid_to_v11(struct cam_actuator_ctrl_t *a_ctrl)
{
	int32_t rc = 0;
	uint32_t IDSEL;
	int i, j;
	uint32_t read_data = 0;
	struct cam_sensor_i2c_reg_setting  i2c_reg_settings;
	struct cam_sensor_i2c_reg_array    i2c_reg_arrays[40];
	i2c_reg_settings.addr_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
	i2c_reg_settings.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
	i2c_reg_settings.delay = 20;

	CAM_ERR(CAM_ACTUATOR, "entry cam_actuator_update_pid");

	if (a_ctrl->need_check_pid &&
			0xE4 >> 1 == a_ctrl->io_master_info.cci_client->sid) {

		a_ctrl->io_master_info.cci_client->sid = 0xE6 >> 1;//read from eeporm
		msleep(10);

		//read original data
		for (i = 0; i < UPDATE_REG_SIZE; i++) {
			rc = camera_io_dev_read(
				     &(a_ctrl->io_master_info),
				     0x00 + i, &read_data,
				     CAMERA_SENSOR_I2C_TYPE_BYTE, CAMERA_SENSOR_I2C_TYPE_BYTE);

			if (rc < 0)
				CAM_ERR(CAM_ACTUATOR, "camera_io_dev_read error");

			CAM_ERR(CAM_ACTUATOR, " original addr:%x data:%x", 0x00 + i, read_data);
		}

		rc = camera_io_dev_read(
			     &(a_ctrl->io_master_info),
			     0x06, &IDSEL,
			     CAMERA_SENSOR_I2C_TYPE_BYTE, CAMERA_SENSOR_I2C_TYPE_BYTE);

		if (rc < 0)
			CAM_ERR(CAM_ACTUATOR, "camera_io_dev_read error");

		CAM_ERR(CAM_ACTUATOR, "read IDSEL data addr:0x06 data:%x", IDSEL);

		a_ctrl->io_master_info.cci_client->sid = 0xE4 >> 1;

		if (IDSEL != 0xEE && IDSEL != 0xEF) {
			return 0;//¦Ì¡À?¡ã?¨ª¡ä?¡¤?R4?¨°R9¡ã?¡À?¡ê?2??¨¹?¡À?¨®?¨¹D??¨¢R11¡ã?¡À?
		}

		// Write all data to EEPROM
		RamWriteByte(a_ctrl, 0x98, 0xE2, 0);		// Release Write Protect
		RamWriteByte(a_ctrl, 0x99, 0xAF, 0);		// Release Write Protect


		a_ctrl->io_master_info.cci_client->sid = 0xE6 >> 1;

		for (i = 0; i < 22;) {
			i2c_reg_settings.size = 0;

			for (j = 0; j < 8 && i < 22; j++) {
				i2c_reg_arrays[j].reg_addr = 0x08 + i;
				i2c_reg_arrays[j].reg_data = WriteValue1[i];

				i2c_reg_arrays[j].delay = 0;
				i2c_reg_settings.size++;
				i++;
				CAM_ERR(CAM_ACTUATOR, "addr:%x data:%x", i2c_reg_arrays[j].reg_addr,
					i2c_reg_arrays[j].reg_data);
			}

			i2c_reg_settings.reg_setting = i2c_reg_arrays;
			rc = camera_io_dev_write_continuous(&a_ctrl->io_master_info, &i2c_reg_settings,
							    1);

			if (rc) {
				CAM_ERR(CAM_EEPROM, "write failed rc %d", rc);
				a_ctrl->io_master_info.cci_client->sid = 0xE4 >> 1;
				return rc;
			}

		}

		for (i = 0; i < 13;) {
			i2c_reg_settings.size = 0;

			for (j = 0; j < 8 && i < 13; j++) {
				i2c_reg_arrays[j].reg_addr = 0x23 + i;
				i2c_reg_arrays[j].reg_data = WriteValue2[i];

				i2c_reg_arrays[j].delay = 0;
				i2c_reg_settings.size++;
				i++;
				CAM_ERR(CAM_ACTUATOR, "addr:%x data:%x", i2c_reg_arrays[j].reg_addr,
					i2c_reg_arrays[j].reg_data);
			}

			i2c_reg_settings.reg_setting = i2c_reg_arrays;
			rc = camera_io_dev_write_continuous(&a_ctrl->io_master_info, &i2c_reg_settings,
							    1);

			if (rc) {
				CAM_ERR(CAM_EEPROM, "write failed rc %d", rc);
				a_ctrl->io_master_info.cci_client->sid = 0xE4 >> 1;
				return rc;
			}

		}

		a_ctrl->io_master_info.cci_client->sid = 0xE4 >> 1;
		RamWriteByte(a_ctrl, 0x98, 0x00, 0);
		RamWriteByte(a_ctrl, 0x99, 0x00, 0);

		RamWriteByte(a_ctrl, 0xE0, 0x01, 0);

		a_ctrl->io_master_info.cci_client->sid = 0xE6 >> 1;

		for (i = 0; i < UPDATE_REG_SIZE; i++) {
			rc = camera_io_dev_read(
				     &(a_ctrl->io_master_info),
				     0x00 + i, &read_data,
				     CAMERA_SENSOR_I2C_TYPE_BYTE, CAMERA_SENSOR_I2C_TYPE_BYTE);

			if (rc < 0)
				CAM_ERR(CAM_ACTUATOR, "camera_io_dev_read error");

			CAM_ERR(CAM_ACTUATOR, " final addr:%x data:%x", 0x00 + i, read_data);
		}
	}

	a_ctrl->io_master_info.cci_client->sid = 0xE4 >> 1;

	return rc;
}


int32_t cam_actuator_check_firmware(struct cam_actuator_ctrl_t *a_ctrl)
{
	int32_t cnt = 0;
	int32_t rc = 0;
	uint32_t reg_data = 0;

	if (0xE4 >> 1 == a_ctrl->io_master_info.cci_client->sid) {
		a_ctrl->io_master_info.cci_client->sid = 0xE6 >> 1;

		for (cnt = 0; cnt < 22; cnt++) {
			rc = camera_io_dev_read(
				     &(a_ctrl->io_master_info),
				     0x08 + cnt, &reg_data,
				     CAMERA_SENSOR_I2C_TYPE_BYTE, CAMERA_SENSOR_I2C_TYPE_BYTE);

			if (rc < 0) {
				CAM_ERR(CAM_ACTUATOR, "read PID data error in step %d:rc %d", 0x08 + cnt, rc);
				break;
			}

			if (reg_data != WriteValue1[cnt]) {
				CAM_ERR(CAM_ACTUATOR, "new PID data wrong in step %d:rc %d, reg_data is %x",
					cnt, rc, reg_data);
				rc = -1;
				break;
			}
		}

		for (cnt = 0; cnt < 13; cnt++) {
			rc = camera_io_dev_read(
				     &(a_ctrl->io_master_info),
				     0x23 + cnt, &reg_data,
				     CAMERA_SENSOR_I2C_TYPE_BYTE, CAMERA_SENSOR_I2C_TYPE_BYTE);

			if (rc < 0) {
				CAM_ERR(CAM_ACTUATOR, "read PID data error in step %d:rc %d", 0x23 + cnt, rc);
				break;
			}

			if (reg_data != WriteValue2[cnt]) {
				CAM_ERR(CAM_ACTUATOR, "new PID data wrong in step %d:rc %d, reg_data is %x",
					cnt, rc, reg_data);
				rc = -1;
				break;
			}
		}

	}

	a_ctrl->io_master_info.cci_client->sid = 0xE4 >> 1;

	return rc;
}

int32_t oplus_cam_actuator_power_up(struct cam_actuator_ctrl_t *a_ctrl)
{
	int retry = 2;
	int re = 0;
	int rc = 0;

	if (a_ctrl->need_check_pid &&
			0xE4 >> 1 == a_ctrl->io_master_info.cci_client->sid) {
		for (re = 0; re < retry; re++) {
			rc = cam_actuator_check_firmware(a_ctrl);

			if (rc < 0) {
				//if rc is error ,update the pid eeprom
				CAM_ERR(CAM_ACTUATOR, "start store the pid data!");
				rc = cam_actuator_update_pid_to_v11(a_ctrl);
			}

			if (rc < 0)
				CAM_ERR(CAM_ACTUATOR, "update the pid data error,check the io ctrl!");

			else
				break;
		}
	}

	return rc;
}
