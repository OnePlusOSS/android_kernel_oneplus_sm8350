// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifdef CONFIG_OPLUS_CHARGER_MTK
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/kobject.h>
#include <linux/platform_device.h>
#include <asm/atomic.h>
#include <asm/unaligned.h>
#include <linux/module.h>
#include <linux/xlog.h>
#include <linux/power_supply.h>
#include <linux/gpio.h>
#include <mt-plat/charger_type.h>
#ifdef CONFIG_OPLUS_CHG_OOS
#include <linux/oem/project_info.h>
#else
//#include <soc/oplus/device_info.h>
#endif
#include <linux/proc_fs.h>
#include <linux/init.h>
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
#include <linux/proc_fs.h>
#include <linux/soc/qcom/smem.h>
#endif

#ifdef OPLUS_SHA1_HMAC
#include <linux/random.h>
#endif
#include "../oplus_charger.h"
#include "../oplus_gauge.h"
#include "../oplus_warp.h"
#include "oplus_bq27541.h"
#ifdef OPLUS_CHG_OP_DEF
#include "../oplus_chg_module.h"
#endif

#define GUAGE_ERROR -1
#define GUAGE_OK 0
#define BATT_FULL_ERROR 2
#define XBL_AUTH_DEBUG

static struct chip_bq27541 *gauge_ic = NULL;
static DEFINE_MUTEX(bq27541_i2c_access);
static DEFINE_MUTEX(bq28z610_alt_manufacturer_access);
static oplus_gauge_auth_result auth_data;
static bool get_smem_batt_info(oplus_gauge_auth_result *auth, int kk);
static bool init_gauge_auth(oplus_gauge_auth_result *rst, struct bq27541_authenticate_data *authenticate_data);

static int __debug_temp_mask;

module_param_named(
	debug_temp_mask, __debug_temp_mask, int, 0600
);
#define GAUGE_READ_ERR	0x01
#define GAUGE_WRITE_ERR 0x02
static int gauge_i2c_err = 0;

int __attribute__((weak)) oplus_get_fg_device_type(void)
{
	return 0;
}

void __attribute__((weak)) oplus_set_fg_device_type(int device_type)
{
	return;
}

#ifdef CONFIG_MACH_MT6785
extern bool is_sala_a(void);
#endif

/**********************************************************
  *
  *   [I2C Function For Read/Write bq27541]
  *
  *********************************************************/

int bq27541_read_i2c(int cmd, int *returnData)
{
	int retry = 4;
	if (!gauge_ic->client) {
		pr_err(" gauge_ic->client NULL, return\n");
#ifdef OPLUS_CHG_OP_DEF
		return -1;
#else
		return 0;
#endif
	}
	if (cmd == BQ27541_BQ27411_CMD_INVALID) {
#ifdef OPLUS_CHG_OP_DEF
		return -1;
#else
		return 0;
#endif
	}
	mutex_lock(&bq27541_i2c_access);
	//gauge_ic->client->timing = 300;
	*returnData = i2c_smbus_read_word_data(gauge_ic->client, cmd);
	if(gauge_ic->device_type == DEVICE_ZY0602 || cmd == BQ27541_BQ27411_REG_CNTL) {
		if (*returnData < 0) {
			while(retry > 0) {
				usleep_range(5000, 5000);
				*returnData = i2c_smbus_read_word_data(gauge_ic->client, cmd);
				if (*returnData < 0) {
					retry--;
				} else {
					break;
				}
			}
		}
	}

	mutex_unlock(&bq27541_i2c_access);
	/*pr_err(" cmd = 0x%x, returnData = 0x%x\r\n", cmd, *returnData) ;*/
	if (*returnData < 0) {
		pr_err("%s read err, rc = %d\n", __func__, *returnData);
		gauge_i2c_err |= GAUGE_READ_ERR;
#ifdef OPLUS_CHG_OP_DEF
		return -1;
#else
		return 1;
#endif
	} else {
		return 0;
	}
}

int bq27541_i2c_txsubcmd(int cmd, int writeData)
{
	int rc = 0;

	if (!gauge_ic->client) {
		pr_err(" gauge_ic->client NULL, return\n");
		return 0;
	}
	if (cmd == BQ27541_BQ27411_CMD_INVALID) {
		return 0;
	}
	mutex_lock(&bq27541_i2c_access);
	rc = i2c_smbus_write_word_data(gauge_ic->client, cmd, writeData);
	if (rc < 0) {
		pr_err("%s write err, rc = %d\n", __func__, rc);
		gauge_i2c_err |= GAUGE_WRITE_ERR;
	}
	mutex_unlock(&bq27541_i2c_access);
	return 0;
}

static int bq27541_write_i2c_block(u8 cmd, u8 length, u8 *writeData)
{
	int rc = 0;

	if (!gauge_ic->client) {
		pr_err(" gauge_ic->client NULL, return\n");
		return 0;
	}
	if (cmd == BQ27541_BQ27411_CMD_INVALID) {
		return 0;
	}
	mutex_lock(&bq27541_i2c_access);
	rc = i2c_smbus_write_i2c_block_data(gauge_ic->client, cmd, length, writeData);
	if (rc < 0) {
		pr_err("%s write err, rc = %d\n", __func__, rc);
		gauge_i2c_err |= GAUGE_WRITE_ERR;
	}
	mutex_unlock(&bq27541_i2c_access);
	return 0;
}


static int bq27541_read_i2c_block(u8 cmd, u8 length, u8 *returnData)
{
	int rc = 0;

	if(!gauge_ic->client) {
		pr_err(" gauge_ic->client NULL,return\n");
		return 0;
	}
	if(cmd == BQ27541_BQ27411_CMD_INVALID)
		return 0;
	mutex_lock(&bq27541_i2c_access);
	rc = i2c_smbus_read_i2c_block_data(gauge_ic->client, cmd, length, returnData);
	if (rc < 0) {
		pr_err("%s read err, rc = %d\n", __func__, rc);
		gauge_i2c_err |= GAUGE_READ_ERR;
	}
	mutex_unlock(&bq27541_i2c_access);
	//pr_err(" cmd = 0x%x, returnData = 0x%x\r\n",cmd,*returnData)  ;
	return 0;
}


static int bq27541_read_i2c_onebyte(u8 cmd, u8 *returnData)
{
	int rc;

	if (!gauge_ic->client) {
		pr_err(" gauge_ic->client NULL, return\n");
		return 0;
	}
	if (cmd == BQ27541_BQ27411_CMD_INVALID) {
		return 0;
	}
	mutex_lock(&bq27541_i2c_access);
	rc = i2c_smbus_read_byte_data(gauge_ic->client, cmd);
	mutex_unlock(&bq27541_i2c_access);
	/*pr_err(" cmd = 0x%x, returnData = 0x%x\r\n", cmd, *returnData) ; */
	if (rc < 0) {
		pr_err("%s read err, rc = %d\n", __func__, rc);
		gauge_i2c_err |= GAUGE_READ_ERR;
		*returnData = (u8)rc;
		return 1;
	} else {
		*returnData = 0;
		return 0;
	}
}

static int bq27541_i2c_txsubcmd_onebyte(u8 cmd, u8 writeData)
{
	int rc = 0;

	if (!gauge_ic->client) {
		pr_err(" gauge_ic->client NULL, return\n");
		return 0;
	}
	if (cmd == BQ27541_BQ27411_CMD_INVALID) {
		return 0;
	}
	mutex_lock(&bq27541_i2c_access);
	rc = i2c_smbus_write_byte_data(gauge_ic->client, cmd, writeData);
	if (rc < 0) {
		pr_err("%s write err, rc = %d\n", __func__, rc);
		gauge_i2c_err |= GAUGE_WRITE_ERR;
	}
	mutex_unlock(&bq27541_i2c_access);
	return 0;
}

static int bq27541_get_gauge_i2c_err(void)
{
	if (!gauge_ic) {
		return 0;
	}

	return gauge_i2c_err;
}

static void bq27541_clear_gauge_i2c_err(void)
{
	if (!gauge_ic) {
		 pr_err("%s, gauge_ic is null\n", __func__);
		return;
        }

        gauge_i2c_err = 0;

	return;
}

/* OPLUS 2013-08-24 wangjc Add begin for add adc interface. */
static int bq27541_get_battery_cc(void)    /*  sjc20150105  */
{
	int ret = 0;
	int cc = 0;

	if (!gauge_ic) {
		return 0;
	}
#ifdef OPLUS_CHG_OP_DEF
	if (!gauge_ic->bq_present)
		return 0;
#endif
	if (atomic_read(&gauge_ic->suspended) == 1) {
		return gauge_ic->cc_pre;
	}
	if (oplus_warp_get_allow_reading() == true) {
		ret = bq27541_read_i2c(gauge_ic->cmd_addr.reg_cc, &cc);
		if (ret) {
			dev_err(gauge_ic->dev, "error reading cc.\n");
			return ret;
		}
	} else {
		if (gauge_ic->cc_pre) {
			return gauge_ic->cc_pre;
		} else {
			return 0;
		}
	}
	gauge_ic->cc_pre = cc;
	return cc;
}

static int bq27541_get_battery_fcc(void)	/*  sjc20150105  */
{
	int ret = 0;
	int fcc = 0;

	if (!gauge_ic) {
		return 0;
	}
#ifdef OPLUS_CHG_OP_DEF
	if (!gauge_ic->bq_present)
		return 0;
#endif
	if (atomic_read(&gauge_ic->suspended) == 1) {
		return gauge_ic->fcc_pre;
	}
	if (oplus_warp_get_allow_reading() == true) {
		ret = bq27541_read_i2c(gauge_ic->cmd_addr.reg_fcc, &fcc);
		if (ret) {
			dev_err(gauge_ic->dev, "error reading fcc.\n");
			return ret;
		}
	} else {
		if (gauge_ic->fcc_pre) {
			return gauge_ic->fcc_pre;
		} else {
			return 0;
		}
	}
	gauge_ic->fcc_pre = fcc;
	return fcc;
}


static int bq27541_get_prev_batt_fcc(void)	/*  sjc20150105  */
{
	if (!gauge_ic) {
		return 0;
	}
#ifdef OPLUS_CHG_OP_DEF
	if (!gauge_ic->bq_present)
		return 0;
#endif
	return gauge_ic->fcc_pre;
}

static int bq27541_get_battery_soh(void)	/*  sjc20150105  */
{
	int ret = 0;
	int soh = 0;

	if (!gauge_ic) {
		return 0;
	}
#ifdef OPLUS_CHG_OP_DEF
	if (!gauge_ic->bq_present)
		return 0;
#endif
	if (atomic_read(&gauge_ic->suspended) == 1) {
		return gauge_ic->soh_pre;
	}
	if (oplus_warp_get_allow_reading() == true) {
		ret = bq27541_read_i2c(gauge_ic->cmd_addr.reg_soh, &soh);
		if (ret) {
			dev_err(gauge_ic->dev, "error reading fcc.\n");
			return ret;
		}
	} else {
		if (gauge_ic->soh_pre) {
			return gauge_ic->soh_pre;
		} else {
			return 0;
		}
	}
	gauge_ic->soh_pre = soh;
	return soh;
}

static int bq27541_soc_calibrate(int soc)
{
	unsigned int soc_calib;
	/*int counter_temp = 0; */
/*
	if (!gauge_ic->batt_psy) {
		gauge_ic->batt_psy = power_supply_get_by_name("battery");
		gauge_ic->soc_pre = soc;
	}
*/
	if (!gauge_ic) {
		return 0;
	}
	soc_calib = soc;
	if (soc >= 100) {
		soc_calib = 100;
	} else if (soc < 0) {
		soc_calib = 0;
	}
	gauge_ic->soc_pre = soc_calib;
	/*pr_info("soc:%d, soc_calib:%d\n", soc, soc_calib); */
	return soc_calib;
}

/*
 * Return the battery Voltage in milivolts
 * Or < 0 if something fails.
 */

static void bq27541_cntl_cmd(int subcmd)
{
	bq27541_i2c_txsubcmd(BQ27541_BQ27411_REG_CNTL, subcmd);
}

static int bq28z610_get_2cell_voltage(void);
//static int bq28z610_get_2cell_balance_time(void);

static int bq27541_get_battery_mvolts(void)
{
	int ret = 0;
	int volt = 0;

	if (!gauge_ic) {
		return 0;
	}
#ifdef OPLUS_CHG_OP_DEF
	if (!gauge_ic->bq_present)
		return 0;
#endif
	if (atomic_read(&gauge_ic->suspended) == 1) {
		return gauge_ic->batt_vol_pre;
	}
	if (oplus_warp_get_allow_reading() == true) {
		ret = bq27541_read_i2c(gauge_ic->cmd_addr.reg_volt, &volt);
		if (ret) {
			dev_err(gauge_ic->dev, "error reading voltage, ret:%d\n", ret);
			gauge_ic->batt_cell_max_vol = gauge_ic->max_vol_pre;
			gauge_ic->batt_cell_min_vol = gauge_ic->min_vol_pre;
			return gauge_ic->batt_vol_pre;
		}
		if(gauge_ic->batt_bq28z610) {
			bq28z610_get_2cell_voltage();
			gauge_ic->max_vol_pre = gauge_ic->batt_cell_max_vol;
			gauge_ic->min_vol_pre = gauge_ic->batt_cell_min_vol;
			gauge_ic->batt_vol_pre = gauge_ic->batt_cell_max_vol;
			return gauge_ic->batt_cell_max_vol;
			//bq28z610_get_2cell_balance_time();
		} else {
			gauge_ic->batt_cell_max_vol = volt;
			gauge_ic->batt_cell_min_vol = volt;
			gauge_ic->batt_vol_pre = volt;
			gauge_ic->max_vol_pre = gauge_ic->batt_cell_max_vol;
			gauge_ic->min_vol_pre = gauge_ic->batt_cell_min_vol;
			return volt;
		}
	} else {
		return gauge_ic->batt_vol_pre;
	}
}

static int bq27541_get_battery_fc(void)
{
	int ret = 0;
	int val = 0;

	if (!gauge_ic) {
		return 0;
	}
#ifdef OPLUS_CHG_OP_DEF
	if (!gauge_ic->bq_present)
		return 0;
#endif
	if(gauge_ic->device_type == DEVICE_BQ27541 || gauge_ic->device_type == DEVICE_ZY0602){
		return -1;
	}
	if (atomic_read(&gauge_ic->suspended) == 1) {
		return gauge_ic->fc_pre;
	}
	if (oplus_warp_get_allow_reading() == true) {
		ret = bq27541_read_i2c(gauge_ic->cmd_addr.reg_fc, &val);
		if (ret) {
			dev_err(gauge_ic->dev, "error reading fc, ret:%d\n", ret);
			return gauge_ic->fc_pre;
		}
		if(!gauge_ic->batt_bq28z610){
			gauge_ic->fc_pre = val;
			return val;
		}else{
			return gauge_ic->fc_pre;
		}
	} else {
		return gauge_ic->fc_pre;
	}
}

static int bq27541_get_battery_qm(void)
{
	int ret = 0;
	int val = 0;

	if (!gauge_ic) {
		return 0;
	}
#ifdef OPLUS_CHG_OP_DEF
	if (!gauge_ic->bq_present)
		return 0;
#endif
	if(gauge_ic->device_type == DEVICE_BQ27541 || gauge_ic->device_type == DEVICE_ZY0602){
		return -1;
	}
	if (atomic_read(&gauge_ic->suspended) == 1) {
		return gauge_ic->qm_pre;
	}
	if (oplus_warp_get_allow_reading() == true) {
		ret = bq27541_read_i2c(gauge_ic->cmd_addr.reg_qm, &val);
		if (ret) {
			dev_err(gauge_ic->dev, "error reading qm, ret:%d\n", ret);
			return gauge_ic->qm_pre;
		}
		if(!gauge_ic->batt_bq28z610){
			gauge_ic->qm_pre = val;
			return val;
		}else{
			return gauge_ic->qm_pre;
		}
	} else {
		return gauge_ic->qm_pre;
	}
}

static int bq27541_get_battery_pd(void)
{
	int ret = 0;
	int val = 0;

	if (!gauge_ic) {
		return 0;
	}
#ifdef OPLUS_CHG_OP_DEF
	if (!gauge_ic->bq_present)
		return 0;
#endif
	if(gauge_ic->device_type == DEVICE_BQ27541 || gauge_ic->device_type == DEVICE_ZY0602){
		return -1;
	}
	if (atomic_read(&gauge_ic->suspended) == 1) {
		return gauge_ic->pd_pre;
	}
	if (oplus_warp_get_allow_reading() == true) {
		ret = bq27541_read_i2c(gauge_ic->cmd_addr.reg_pd, &val);
		if (ret) {
			dev_err(gauge_ic->dev, "error reading pd, ret:%d\n", ret);
			return gauge_ic->pd_pre;
		}
		if(!gauge_ic->batt_bq28z610){
			gauge_ic->pd_pre = val;
			return val;
		}else{
			return gauge_ic->pd_pre;
		}
	} else {
		return gauge_ic->pd_pre;
	}
}

static int bq27541_get_battery_rcu(void)
{
	int ret = 0;
	int val = 0;

	if (!gauge_ic) {
		return 0;
	}
#ifdef OPLUS_CHG_OP_DEF
	if (!gauge_ic->bq_present)
		return 0;
#endif
	if(gauge_ic->device_type == DEVICE_BQ27541 || gauge_ic->device_type == DEVICE_ZY0602){
		return -1;
	}
	if (atomic_read(&gauge_ic->suspended) == 1) {
		return gauge_ic->rcu_pre;
	}
	if (oplus_warp_get_allow_reading() == true) {
		ret = bq27541_read_i2c(gauge_ic->cmd_addr.reg_rcu, &val);
		if (ret) {
			dev_err(gauge_ic->dev, "error reading rcu, ret:%d\n", ret);
			return gauge_ic->rcu_pre;
		}
		if(!gauge_ic->batt_bq28z610){
			gauge_ic->rcu_pre = val;
			return val;
		}else{
			return gauge_ic->rcu_pre;
		}
	} else {
		return gauge_ic->rcu_pre;
	}
}

static int bq27541_get_battery_rcf(void)
{
	int ret = 0;
	int val = 0;

	if (!gauge_ic) {
		return 0;
	}
#ifdef OPLUS_CHG_OP_DEF
	if (!gauge_ic->bq_present)
		return 0;
#endif
	if(gauge_ic->device_type == DEVICE_BQ27541 || gauge_ic->device_type == DEVICE_ZY0602){
		return -1;
	}
	if (atomic_read(&gauge_ic->suspended) == 1) {
		return gauge_ic->rcf_pre;
	}
	if (oplus_warp_get_allow_reading() == true) {
		ret = bq27541_read_i2c(gauge_ic->cmd_addr.reg_rcf, &val);
		if (ret) {
			dev_err(gauge_ic->dev, "error reading rcf, ret:%d\n", ret);
			return gauge_ic->rcf_pre;
		}
		if(!gauge_ic->batt_bq28z610){
			gauge_ic->rcf_pre = val;
			return val;
		}else{
			return gauge_ic->rcf_pre;
		}
	} else {
		return gauge_ic->rcf_pre;
	}
}

static int bq27541_get_battery_fcu(void)
{
	int ret = 0;
	int val = 0;

	if (!gauge_ic) {
		return 0;
	}
#ifdef OPLUS_CHG_OP_DEF
	if (!gauge_ic->bq_present)
		return 0;
#endif
	if(gauge_ic->device_type == DEVICE_BQ27541 || gauge_ic->device_type == DEVICE_ZY0602){
		return -1;
	}
	if (atomic_read(&gauge_ic->suspended) == 1) {
		return gauge_ic->fcu_pre;
	}
	if (oplus_warp_get_allow_reading() == true) {
		ret = bq27541_read_i2c(gauge_ic->cmd_addr.reg_fcu, &val);
		if (ret) {
			dev_err(gauge_ic->dev, "error reading fcu, ret:%d\n", ret);
			return gauge_ic->fcu_pre;
		}
		if(!gauge_ic->batt_bq28z610){
			gauge_ic->fcu_pre = val;
			return val;
		}else{
			return gauge_ic->fcu_pre;
		}
	} else {
		return gauge_ic->fcu_pre;
	}
}

static int bq27541_get_battery_fcf(void)
{
	int ret = 0;
	int val = 0;

	if (!gauge_ic) {
		return 0;
	}
#ifdef OPLUS_CHG_OP_DEF
	if (!gauge_ic->bq_present)
		return 0;
#endif
	if(gauge_ic->device_type == DEVICE_BQ27541 || gauge_ic->device_type == DEVICE_ZY0602){
		return -1;
	}
	if (atomic_read(&gauge_ic->suspended) == 1) {
		return gauge_ic->fcf_pre;
	}
	if (oplus_warp_get_allow_reading() == true) {
		ret = bq27541_read_i2c(gauge_ic->cmd_addr.reg_fcf, &val);
		if (ret) {
			dev_err(gauge_ic->dev, "error reading fcf, ret:%d\n", ret);
			return gauge_ic->fcf_pre;
		}
		if(!gauge_ic->batt_bq28z610){
			gauge_ic->fcf_pre = val;
			return val;
		}else{
			return gauge_ic->fcf_pre;
		}
	} else {
		return gauge_ic->fcf_pre;
	}
}

static int bq27541_get_battery_sou(void)
{
	int ret = 0;
	int val = 0;

	if (!gauge_ic) {
		return 0;
	}
#ifdef OPLUS_CHG_OP_DEF
	if (!gauge_ic->bq_present)
		return 0;
#endif
	if(gauge_ic->device_type == DEVICE_BQ27541 || gauge_ic->device_type == DEVICE_ZY0602){
		return -1;
	}
	if (atomic_read(&gauge_ic->suspended) == 1) {
		return gauge_ic->sou_pre;
	}
	if (oplus_warp_get_allow_reading() == true) {
		ret = bq27541_read_i2c(gauge_ic->cmd_addr.reg_sou, &val);
		if (ret) {
			dev_err(gauge_ic->dev, "error reading sou, ret:%d\n", ret);
			return gauge_ic->sou_pre;
		}
		if(!gauge_ic->batt_bq28z610){
			gauge_ic->sou_pre = val;
			return val;
		}else{
			return gauge_ic->sou_pre;
		}
	} else {
		return gauge_ic->sou_pre;
	}
}

static int bq27541_get_battery_do0(void)
{
	int ret = 0;
	int val = 0;

	if (!gauge_ic) {
		return 0;
	}
	if(gauge_ic->device_type == DEVICE_BQ27541 || gauge_ic->device_type == DEVICE_ZY0602){
		return -1;
	}
	if (atomic_read(&gauge_ic->suspended) == 1) {
		return gauge_ic->do0_pre;
	}
	if (oplus_warp_get_allow_reading() == true) {
		ret = bq27541_read_i2c(gauge_ic->cmd_addr.reg_do0, &val);
		if (ret) {
			dev_err(gauge_ic->dev, "error reading do0, ret:%d\n", ret);
			return gauge_ic->do0_pre;
		}
		if(!gauge_ic->batt_bq28z610){
			gauge_ic->do0_pre = val;
			return val;
		}else{
			return gauge_ic->do0_pre;
		}
	} else {
		return gauge_ic->do0_pre;
	}
}

static int bq27541_get_battery_doe(void)
{
	int ret = 0;
	int val = 0;

	if (!gauge_ic) {
		return 0;
	}
#ifdef OPLUS_CHG_OP_DEF
	if (!gauge_ic->bq_present)
		return 0;
#endif
	if(gauge_ic->device_type == DEVICE_BQ27541 || gauge_ic->device_type == DEVICE_ZY0602){
		return -1;
	}
	if (atomic_read(&gauge_ic->suspended) == 1) {
		return gauge_ic->doe_pre;
	}
	if (oplus_warp_get_allow_reading() == true) {
		ret = bq27541_read_i2c(gauge_ic->cmd_addr.reg_doe, &val);
		if (ret) {
			dev_err(gauge_ic->dev, "error reading doe, ret:%d\n", ret);
			return gauge_ic->doe_pre;
		}
		if(!gauge_ic->batt_bq28z610){
			gauge_ic->doe_pre = val;
			return val;
		}else{
			return gauge_ic->doe_pre;
		}
	} else {
		return gauge_ic->doe_pre;
	}
}

static int bq27541_get_battery_trm(void)
{
	int ret = 0;
	int val = 0;

	if (!gauge_ic) {
		return 0;
	}
#ifdef OPLUS_CHG_OP_DEF
	if (!gauge_ic->bq_present)
		return 0;
#endif
	if(gauge_ic->device_type == DEVICE_BQ27541 || gauge_ic->device_type == DEVICE_ZY0602){
		return -1;
	}
	if (atomic_read(&gauge_ic->suspended) == 1) {
		return gauge_ic->trm_pre;
	}
	if (oplus_warp_get_allow_reading() == true) {
		ret = bq27541_read_i2c(gauge_ic->cmd_addr.reg_trm, &val);
		if (ret) {
			dev_err(gauge_ic->dev, "error reading trm, ret:%d\n", ret);
			return gauge_ic->trm_pre;
		}
		if(!gauge_ic->batt_bq28z610){
			gauge_ic->trm_pre = val;
			return val;
		}else{
			return gauge_ic->trm_pre;
		}
	} else {
		return gauge_ic->trm_pre;
	}
}

static int bq27541_get_battery_pc(void)
{
	int ret = 0;
	int val = 0;

	if (!gauge_ic) {
		return 0;
	}
#ifdef OPLUS_CHG_OP_DEF
	if (!gauge_ic->bq_present)
		return 0;
#endif
	if(gauge_ic->device_type == DEVICE_BQ27541 || gauge_ic->device_type == DEVICE_ZY0602){
		return -1;
	}
	if (atomic_read(&gauge_ic->suspended) == 1) {
		return gauge_ic->pc_pre;
	}
	if (oplus_warp_get_allow_reading() == true) {
		ret = bq27541_read_i2c(gauge_ic->cmd_addr.reg_pc, &val);
		if (ret) {
			dev_err(gauge_ic->dev, "error reading pc, ret:%d\n", ret);
			return gauge_ic->pc_pre;
		}
		if(!gauge_ic->batt_bq28z610){
			gauge_ic->pc_pre = val;
			return val;
		}else{
			return gauge_ic->pc_pre;
		}
	} else {
		return gauge_ic->pc_pre;
	}
}

static int bq27541_get_battery_qs(void)
{
	int ret = 0;
	int val = 0;

	if (!gauge_ic) {
		return 0;
	}
#ifdef OPLUS_CHG_OP_DEF
	if (!gauge_ic->bq_present)
		return 0;
#endif
	if(gauge_ic->device_type == DEVICE_BQ27541 || gauge_ic->device_type == DEVICE_ZY0602){
		return -1;
	}
	if (atomic_read(&gauge_ic->suspended) == 1) {
		return gauge_ic->qs_pre;
	}
	if (oplus_warp_get_allow_reading() == true) {
		ret = bq27541_read_i2c(gauge_ic->cmd_addr.reg_qs, &val);
		if (ret) {
			dev_err(gauge_ic->dev, "error reading qs, ret:%d\n", ret);
			return gauge_ic->qs_pre;
		}
		if(!gauge_ic->batt_bq28z610){
			gauge_ic->qs_pre = val;
			return val;
		}else{
			return gauge_ic->qs_pre;
		}
	} else {
		return gauge_ic->qs_pre;
	}
}

static int bq27541_get_battery_mvolts_2cell_max(void)
{
	if(!gauge_ic) {
		return 0;
	}
	return gauge_ic->batt_cell_max_vol;
}

static int bq27541_get_battery_mvolts_2cell_min(void)
{
	if(!gauge_ic) {
		return 0;
	}
	return gauge_ic->batt_cell_min_vol;
}

#define DEVICE_CHEMISTRY_LION		1
#define DEVICE_CHEMISTRY_C2A1		2
#define DEVICE_CHEMISTRY_C2A2		3
#define DEVICE_CHEMISTRY_UNKOWN	99
static int bq28z610_get_device_chemistry(void)
{
	u8 data[4] = {0, 0, 0, 0};
	int ret = 0;

	if (!gauge_ic) {
		return 0;
	}

	if (atomic_read(&gauge_ic->suspended) == 1) {
		return 0;
	}

	if (gauge_ic->batt_bq28z610) {
		if (oplus_warp_get_allow_reading() == true) {
			mutex_lock(&bq28z610_alt_manufacturer_access);
			bq27541_i2c_txsubcmd(BQ28Z610_DEVICE_CHEMISTRY_EN_ADDR,
				BQ28Z610_DEVICE_CHEMISTRY_CMD);
			usleep_range(1000, 1000);
			ret = bq27541_read_i2c_block(BQ28Z610_DEVICE_CHEMISTRY_ADDR,
				BQ28Z610_DEVICE_CHEMISTRY_SIZE, data);
			mutex_unlock(&bq28z610_alt_manufacturer_access);
			if (ret) {
				dev_err(gauge_ic->dev, "error reading operation status.\n");
				return 0;
			}
			dev_info(gauge_ic->dev, "device chemistry: [%c%c%c%c]\n",
				data[0], data[1], data[2], data[3]);
			if (data[0] == 0x4C && data[1] == 0x49 && data[2] == 0x4F && data[3] == 0x4E) {
				return DEVICE_CHEMISTRY_LION;
			} else if (data[0] == 0x43 && data[1] == 0x32 && data[2] == 0x41 && data[3] == 0x31) {
				return DEVICE_CHEMISTRY_C2A1;
			} else if (data[0] == 0x43 && data[1] == 0x32 && data[2] == 0x41 && data[3] == 0x32) {
				return DEVICE_CHEMISTRY_C2A2;
			} else {
				return DEVICE_CHEMISTRY_UNKOWN;
			}
		}
	}
	return 0;
}

static int bq28z610_get_balancing_config(void)
{
	u8 data[4] = {0, 0, 0, 0};
	int ret = 0;
	int balancing_config = 0;
	static int pre_balancing_config = 0;
	static int count = 0;
	unsigned int tmp;

	if (!gauge_ic) {
		return 0;
	}

	if (atomic_read(&gauge_ic->suspended) == 1) {
		return 0;
	}

	if (gauge_ic->batt_bq28z610) {
		if (oplus_warp_get_allow_reading() == true) {
			mutex_lock(&bq28z610_alt_manufacturer_access);
			bq27541_i2c_txsubcmd(BQ28Z610_OPERATION_STATUS_EN_ADDR,
				BQ28Z610_OPERATION_STATUS_CMD);
			usleep_range(1000, 1000);
			ret = bq27541_read_i2c_block(BQ28Z610_OPERATION_STATUS_ADDR,
				BQ28Z610_OPERATION_STATUS_SIZE, data);
			mutex_unlock(&bq28z610_alt_manufacturer_access);
			if (ret) {
				dev_err(gauge_ic->dev, "error reading operation status.\n");
				return pre_balancing_config;
			}
			tmp = ((data[3] << 24 | data[2] << 16 | data[1] << 8 | data[0])
                                & BQ28Z610_BALANCING_CONFIG_BIT) >> 28;
			balancing_config = (int)tmp;
			count++;
			if (balancing_config ^ pre_balancing_config || count >= 10) {
				count = 0;
				dev_info(gauge_ic->dev, "operation status[0x%x], cb28[%d]\n",
					data[3] << 24 | data[2] << 16 | data[1] << 8 | data[0], balancing_config);
			}
			pre_balancing_config = balancing_config;
			return balancing_config;
		} else {
			return pre_balancing_config;
		}
	}
	return 0;
}

#define TEMP_LT_16C		1//-2-3-4-5-6-7
#define TEMP_LT_39C		2//-1-2-3-4-5
#define TEMP_HT_39C		3//-1-2-3-4
#define BATT_TEMP_16C	160
#define BATT_TEMP_39C	390
static int batt_balancing_config = 0;
static int bq28z610_get_battery_balancing_status(void)
{
	return batt_balancing_config;
}
static int bq27541_get_battery_temperature(void)
{
	int ret = 0;
	int temp = 0;
	static int pre_batt_balancing_config = 0;
	static int count = 0;
	static int cb_count = 0;
	static int cb_flag = 0;
	static int temp_status = 0;
	static int delta_temp = 0;
	struct task_struct *t = NULL;

	if (__debug_temp_mask)
		return __debug_temp_mask;
	if (!gauge_ic) {
#ifdef OPLUS_CHG_OP_DEF
		return -400;
#else
		return 0;
#endif
	}
#ifdef OPLUS_CHG_OP_DEF
	if (!gauge_ic->bq_present)
		return -400;
#endif
	if (atomic_read(&gauge_ic->suspended) == 1) {
		return gauge_ic->temp_pre + ZERO_DEGREE_CELSIUS_IN_TENTH_KELVIN;
	}
	if (oplus_warp_get_allow_reading() == true) {
		ret = bq27541_read_i2c(gauge_ic->cmd_addr.reg_temp, &temp);
		if (ret) {
			count++;
			dev_err(gauge_ic->dev, "error reading temperature\n");
			if (count > 1) {
				count = 0;
				gauge_ic->temp_pre = -400 - ZERO_DEGREE_CELSIUS_IN_TENTH_KELVIN;
				return -400;
			} else {
				return gauge_ic->temp_pre + ZERO_DEGREE_CELSIUS_IN_TENTH_KELVIN;
			}
		}
		count = 0;
	} else {
		return gauge_ic->temp_pre + ZERO_DEGREE_CELSIUS_IN_TENTH_KELVIN;
	}

	if (gauge_ic->bq28z610_need_balancing) {
		t = get_current();
		if (t != NULL && !strncmp(t->comm, "tbatt_pwroff", 12))
			goto cb_exit;

		batt_balancing_config = bq28z610_get_balancing_config();
		if (pre_batt_balancing_config == 0 && batt_balancing_config == 1 && temp_status == 0) {
			if (gauge_ic->temp_pre + ZERO_DEGREE_CELSIUS_IN_TENTH_KELVIN <= BATT_TEMP_16C) {
				temp_status = TEMP_LT_16C;
				delta_temp = 10;
			} else if (gauge_ic->temp_pre + ZERO_DEGREE_CELSIUS_IN_TENTH_KELVIN <= BATT_TEMP_39C) {
				temp_status = TEMP_LT_39C;
				delta_temp = 0;
			} else {
				temp_status = TEMP_HT_39C;
				delta_temp = 0;
			}
			printk(KERN_ERR "SJC-TEST: temp_status[%d], delta_temp[%d]\n", temp_status, delta_temp);
		}
		pre_batt_balancing_config = batt_balancing_config;

		if (batt_balancing_config == 1) {

			cb_flag = 1;
			cb_count++;

			if (gauge_ic->bq28z610_device_chem == DEVICE_CHEMISTRY_C2A1
					|| gauge_ic->bq28z610_device_chem == DEVICE_CHEMISTRY_C2A2) {
				if (cb_count >= 4 && temp_status == TEMP_LT_16C) {
					temp = temp - 20;
					printk(KERN_ERR "SJC-TEST C2A1: - 20\n");
				} else if (cb_count >= 3 && temp_status != TEMP_HT_39C) {
					temp = temp - 15;
					printk(KERN_ERR "SJC-TEST C2A1: - 15\n");
				} else if (cb_count >= 2) {
					temp = temp - 10;
					printk(KERN_ERR "SJC-TEST C2A1: - 10\n");
				} else if (cb_count >= 1) {
					temp = temp - 5;
					printk(KERN_ERR "SJC-TEST C2A1: - 5\n");
				}

				if (temp_status == TEMP_LT_16C) {
					if (cb_count >= 4)
						cb_count = 4;
				} else if (temp_status != TEMP_HT_39C) {
					if (cb_count >= 3)
						cb_count = 3;
				} else {
					if (cb_count >= 2)
						cb_count = 2;
				}
			} else {
				if (cb_count >= 6 && temp_status == TEMP_LT_16C) {
					temp = temp - (60 + delta_temp);
					printk(KERN_ERR "SJC-TEST: - %d\n", 60 + delta_temp);
				} else if (cb_count >= 5 && temp_status != TEMP_HT_39C) {
					temp = temp - (50 + delta_temp);
					printk(KERN_ERR "SJC-TEST: - %d\n", 50 + delta_temp);
				} else if (cb_count >= 4) {
					temp = temp - (40 + delta_temp);
					printk(KERN_ERR "SJC-TEST: - %d\n", 40 + delta_temp);
				} else if (cb_count >= 3) {
					temp = temp - (30 + delta_temp);
					printk(KERN_ERR "SJC-TEST: - %d\n", 30 + delta_temp);
				} else if (cb_count >= 2) {
					temp = temp - (20 + delta_temp);
					printk(KERN_ERR "SJC-TEST: - %d\n", 20 + delta_temp);
				} else if (cb_count >= 1) {
					temp = temp - (10 + delta_temp);
					printk(KERN_ERR "SJC-TEST: - %d\n", 10 + delta_temp);
				}

				if (temp_status == TEMP_LT_16C) {
					if (cb_count >= 6)
						cb_count = 6;
				} else if (temp_status != TEMP_HT_39C) {
					if (cb_count >= 5)
						cb_count = 5;
				} else {
					if (cb_count >= 4)
						cb_count = 4;
				}
			}
		} else if (cb_flag == 1 && cb_count > 0) {
			if (gauge_ic->bq28z610_device_chem == DEVICE_CHEMISTRY_C2A1
					|| gauge_ic->bq28z610_device_chem == DEVICE_CHEMISTRY_C2A2) {
				if (cb_count >= 4 && temp_status == TEMP_LT_16C) {
					temp = temp - 20;
					printk(KERN_ERR "SJC-TEST 4 C2A1: - 20\n");
				} else if (cb_count >= 3 && temp_status != TEMP_HT_39C) {
					temp = temp - 15;
					printk(KERN_ERR "SJC-TEST 3 C2A1: - 15\n");
				} else if (cb_count >= 2) {
					temp = temp - 10;
					printk(KERN_ERR "SJC-TEST 2 C2A1: - 10\n");
				} else if (cb_count >= 1) {
					temp = temp - 5;
					printk(KERN_ERR "SJC-TEST 1 C2A1: - 5\n");
				}
			} else {
				if (cb_count >= 6 && temp_status == TEMP_LT_16C) {
					temp = temp - (60 + delta_temp);
					printk(KERN_ERR "SJC-TEST 6: - %d\n", 60 + delta_temp);
				} else if (cb_count >= 5 && temp_status != TEMP_HT_39C) {
					temp = temp - (50 + delta_temp);
					printk(KERN_ERR "SJC-TEST 5: - %d\n", 50 + delta_temp);
				} else if (cb_count >= 4) {
					temp = temp - (40 + delta_temp);
					printk(KERN_ERR "SJC-TEST 4: - %d\n", 40 + delta_temp);
				} else if (cb_count >= 3) {
					temp = temp - (30 + delta_temp);
					printk(KERN_ERR "SJC-TEST 3: - %d\n", 30 + delta_temp);
				} else if (cb_count >= 2) {
					temp = temp - (20 + delta_temp);
					printk(KERN_ERR "SJC-TEST 2: - %d\n", 20 + delta_temp);
				} else if (cb_count >= 1) {
					temp = temp - (10 + delta_temp);
					printk(KERN_ERR "SJC-TEST 1: - %d\n", 10 + delta_temp);
				}
			}

			cb_count--;
		} else {
			cb_count = 0;
			cb_flag = 0;
			temp_status = 0;
			delta_temp = 0;
		}
	}
cb_exit:
	if ((temp + ZERO_DEGREE_CELSIUS_IN_TENTH_KELVIN) > 1000) {
		temp = gauge_ic->temp_pre;
	}
	gauge_ic->temp_pre = temp;
	return temp + ZERO_DEGREE_CELSIUS_IN_TENTH_KELVIN;
}

static int bq27541_get_batt_remaining_capacity(void)
{
	int ret;
	int cap = 0;

	if (!gauge_ic) {
		return 0;
	}
#ifdef OPLUS_CHG_OP_DEF
	if (!gauge_ic->bq_present)
		return 0;
#endif
	if (atomic_read(&gauge_ic->suspended) == 1) {
		return gauge_ic->rm_pre;
	}
	if (oplus_warp_get_allow_reading() == true) {
		ret = bq27541_read_i2c(gauge_ic->cmd_addr.reg_rm, &cap);
		if (ret) {
			dev_err(gauge_ic->dev, "error reading capacity.\n");
			return ret;
		}
		gauge_ic->rm_pre = cap;
		return gauge_ic->rm_pre;
	} else {
		return gauge_ic->rm_pre;
	}
}

static int bq27541_get_prev_batt_remaining_capacity(void)
{
	if (!gauge_ic) {
		return 0;
	}
	return gauge_ic->rm_pre;
}

static int bq27541_get_battery_soc(void)
{
	int ret;
	int soc = 0;

	if (!gauge_ic) {
		return 50;
	}
#ifdef OPLUS_CHG_OP_DEF
	if (!gauge_ic->bq_present)
		return 50;
#endif
	if (atomic_read(&gauge_ic->suspended) == 1) {
		return gauge_ic->soc_pre;
	}
	if (oplus_warp_get_allow_reading() == true) {
		ret = bq27541_read_i2c(gauge_ic->cmd_addr.reg_soc, &soc);
		if (ret) {
			dev_err(gauge_ic->dev, "error reading soc.ret:%d\n", ret);
			goto read_soc_err;
		}
	} else {
		if (gauge_ic->soc_pre) {
			return gauge_ic->soc_pre;
		} else {
			return 0;
		}
	}
	soc = bq27541_soc_calibrate(soc);
	return soc;

read_soc_err:
	if (gauge_ic->soc_pre) {
		return gauge_ic->soc_pre;
	} else {
		return 0;
	}
}

static int bq27541_get_average_current(void)
{
	int ret;
	int curr = 0;

	if (!gauge_ic) {
		return 0;
	}
#ifdef OPLUS_CHG_OP_DEF
	if (!gauge_ic->bq_present)
		return 0;
#endif
	if (atomic_read(&gauge_ic->suspended) == 1) {
		return -gauge_ic->current_pre;
	}
	if (oplus_warp_get_allow_reading() == true) {
		ret = bq27541_read_i2c(gauge_ic->cmd_addr.reg_ai, &curr);
		if (ret) {
			dev_err(gauge_ic->dev, "error reading current.\n");
			return gauge_ic->current_pre;
		}
	} else {
		return -gauge_ic->current_pre;
	}
	/* negative current */
	if (curr&0x8000) {
		curr = -((~(curr-1))&0xFFFF);
	}
	gauge_ic->current_pre = curr;
	return -curr;
}

static int bq27541_sha1_hmac_authenticate(struct bq27541_authenticate_data *authenticate_data);

static bool bq27541_get_battery_hmac(void)
{
	if (!gauge_ic) {
		return true;
	}

#ifdef OPLUS_CHG_OP_DEF
	return true;
#endif

#ifdef CONFIG_MACH_MT6785
/*
 *sala_a battery gauge authentication not complete, return true directly
 */
	if (is_sala_a()) {
		chg_err("[%s]sala_a gauge authentication return true directly\n", __func__);
		return true;
	}
#endif

	if(gauge_ic->batt_bq28z610) {
		//		return bq27541_is_authenticate_OK(gauge_ic);
		get_smem_batt_info(&auth_data, 1);
		if (init_gauge_auth(&auth_data, gauge_ic->authenticate_data))
			return true;
		pr_info("%s:gauge authenticate failed, try again\n");
		get_smem_batt_info(&auth_data, 0);
		return init_gauge_auth(&auth_data, gauge_ic->authenticate_data);
	} else {
		return true;
	}
}

static bool bq27541_get_battery_authenticate(void)
{
	static bool get_temp = false;

	if (!gauge_ic) {
		return true;
	}
#ifdef OPLUS_CHG_OP_DEF
	if (!gauge_ic->bq_present)
		return 0;
#endif

	if (gauge_ic->temp_pre == 0 && get_temp == false) {
		bq27541_get_battery_temperature();
		msleep(10);
		bq27541_get_battery_temperature();
	}
	get_temp = true;
	if (gauge_ic->temp_pre == (-400 - ZERO_DEGREE_CELSIUS_IN_TENTH_KELVIN)) {
		return false;
	} else {
		return true;
	}
}



static int bq27541_get_prev_battery_mvolts(void)
{
	if (!gauge_ic) {
		return 0;
	}
#ifdef OPLUS_CHG_OP_DEF
	if (!gauge_ic->bq_present)
		return 0;
#endif
	return gauge_ic->batt_vol_pre;
}

static int bq27541_get_prev_battery_mvolts_2cell_max(void)
{
	if (!gauge_ic) {
		return 0;
	}
	return gauge_ic->max_vol_pre;
}

static int bq27541_get_prev_battery_mvolts_2cell_min(void)
{
	if (!gauge_ic) {
		return 0;
	}
	return gauge_ic->min_vol_pre;
}

static int bq27541_get_prev_battery_temperature(void)
{
	if (!gauge_ic) {
#ifdef OPLUS_CHG_OP_DEF
		return -400;
#else
		return 0;
#endif
	}
#ifdef OPLUS_CHG_OP_DEF
	if (!gauge_ic->bq_present)
		return -400;
#endif
	return gauge_ic->temp_pre + ZERO_DEGREE_CELSIUS_IN_TENTH_KELVIN;
}

static int bq27541_get_prev_battery_soc(void)
{
	if (!gauge_ic) {
		return 50;
	}
	return gauge_ic->soc_pre;
}

static int bq27541_get_prev_average_current(void)
{
	if (!gauge_ic) {
		return 0;
	}
	return -gauge_ic->current_pre;
}
static void bq27541_set_battery_full(bool full)
{
	/* Do nothing */
}

static void bq28z610_modify_dod0_parameter(struct chip_bq27541 *chip);

static int bq28z610_modify_dod0(void)
{
	if (!gauge_ic) {
		return 0;
	}
	if(gauge_ic->batt_bq28z610) {
		bq28z610_modify_dod0_parameter(gauge_ic);
	}
	return 0;
}

static void bq28z610_modify_soc_smooth_parameter(struct chip_bq27541 *chip);

static int  bq28z610_update_soc_smooth_parameter(void)
{
	if (!gauge_ic) {
		return -1;
	}
#ifdef OPLUS_CHG_OP_DEF
	if (!gauge_ic->bq_present)
		return -200;
#endif
	if(gauge_ic->batt_bq28z610) {
		bq28z610_modify_soc_smooth_parameter(gauge_ic);
	}
	return 0;
}


int bq27541_get_passedchg(int *val)
{
	int rc = -1;
	struct chip_bq27541 *chip;

	u8 value;
	s16 v = 0;

	if (gauge_ic == NULL)
		return rc;

	chip = gauge_ic;

	if (atomic_read(&gauge_ic->suspended) == 1)
		return rc;

	/*enable block data control */
	if(gauge_ic->batt_bq28z610) {
		rc = bq27541_i2c_txsubcmd_onebyte(BQ27411_BLOCK_DATA_CONTROL, 0x00);
		if (rc) {
			pr_err("%s enable block data control fail\n", __func__);
			goto out;
		}
		usleep_range(5000, 5000);

		bq27541_i2c_txsubcmd(BQ27411_DATA_CLASS_ACCESS, 0x0074);
		usleep_range(10000, 10000);
		rc = bq27541_read_i2c_onebyte(0x4E, &value);
		if (rc) {
			pr_err("%s read 0x4E fail\n", __func__);
			goto out;
		}
		v |= value;
		rc = bq27541_read_i2c_onebyte(0x4F, &value);
		if (rc) {
			pr_err("%s read 0x4F fail\n", __func__);
			goto out;
		}
		v |= (value << 8);
	} else {
		rc = bq27541_read_i2c_onebyte(0x6C, &value);
		if (rc) {
			pr_err("%s read 0x6c fail\n", __func__);
			goto out;
		}
		v |= value;
		rc = bq27541_read_i2c_onebyte(0x6D, &value);
		if (rc) {
			pr_err("%s read 0x6d fail\n", __func__);
			goto out;
		}
		v |= (value << 8);
	}

	*val = v;

	if (atomic_read(&gauge_ic->suspended) == 1)
		return -1;
out:
	return rc;
}

static int gauge_reg_dump(void);
static struct oplus_gauge_operations bq27541_gauge_ops = {
	.get_battery_mvolts = bq27541_get_battery_mvolts,
	.get_battery_fc = bq27541_get_battery_fc,
	.get_battery_qm = bq27541_get_battery_qm,
	.get_battery_pd = bq27541_get_battery_pd,
	.get_battery_rcu = bq27541_get_battery_rcu,
	.get_battery_rcf = bq27541_get_battery_rcf,
	.get_battery_fcu = bq27541_get_battery_fcu,
	.get_battery_fcf = bq27541_get_battery_fcf,
	.get_battery_sou = bq27541_get_battery_sou,
	.get_battery_do0 = bq27541_get_battery_do0,
	.get_battery_doe = bq27541_get_battery_doe,
	.get_battery_trm = bq27541_get_battery_trm,
	.get_battery_pc = bq27541_get_battery_pc,
	.get_battery_qs = bq27541_get_battery_qs,
	.get_battery_temperature = bq27541_get_battery_temperature,
	.get_batt_remaining_capacity = bq27541_get_batt_remaining_capacity,
	.get_battery_soc = bq27541_get_battery_soc,
	.get_average_current = bq27541_get_average_current,
	.get_battery_fcc = bq27541_get_battery_fcc,
	.get_prev_batt_fcc = bq27541_get_prev_batt_fcc,
	.get_battery_cc = bq27541_get_battery_cc,
	.get_battery_soh = bq27541_get_battery_soh,
	.get_battery_authenticate = bq27541_get_battery_authenticate,
	.get_battery_hmac = bq27541_get_battery_hmac,
	.set_battery_full = bq27541_set_battery_full,
	.get_prev_battery_mvolts = bq27541_get_prev_battery_mvolts,
	.get_prev_battery_temperature = bq27541_get_prev_battery_temperature,
	.get_prev_battery_soc = bq27541_get_prev_battery_soc,
	.get_prev_average_current = bq27541_get_prev_average_current,
	.get_prev_batt_remaining_capacity   = bq27541_get_prev_batt_remaining_capacity,
	.get_battery_mvolts_2cell_max = bq27541_get_battery_mvolts_2cell_max,
	.get_battery_mvolts_2cell_min = bq27541_get_battery_mvolts_2cell_min,
	.get_prev_battery_mvolts_2cell_max = bq27541_get_prev_battery_mvolts_2cell_max,
	.get_prev_battery_mvolts_2cell_min = bq27541_get_prev_battery_mvolts_2cell_min,
	.update_battery_dod0 = bq28z610_modify_dod0,
	.update_soc_smooth_parameter = bq28z610_update_soc_smooth_parameter,
	.get_battery_cb_status = bq28z610_get_battery_balancing_status,
	.get_gauge_i2c_err = bq27541_get_gauge_i2c_err,
	.clear_gauge_i2c_err = bq27541_clear_gauge_i2c_err,
	.get_passdchg = bq27541_get_passedchg,
	.dump_register = gauge_reg_dump,
};

static void gauge_set_cmd_addr(struct chip_bq27541 *chip, int device_type)
{
	if (device_type == DEVICE_BQ27541 || device_type == DEVICE_ZY0602) {
		chip->cmd_addr.reg_cntl = BQ27541_BQ27411_REG_CNTL;
		chip->cmd_addr.reg_temp = BQ27541_REG_TEMP;
		chip->cmd_addr.reg_volt = BQ27541_REG_VOLT;
		chip->cmd_addr.reg_flags = BQ27541_REG_FLAGS;
		chip->cmd_addr.reg_nac = BQ27541_REG_NAC;
		chip->cmd_addr.reg_fac = BQ27541_REG_FAC;
		chip->cmd_addr.reg_rm = BQ27541_REG_RM;
		chip->cmd_addr.reg_fcc = BQ27541_REG_FCC;
		chip->cmd_addr.reg_ai = BQ27541_REG_AI;
		chip->cmd_addr.reg_si = BQ27541_REG_SI;
		chip->cmd_addr.reg_mli = BQ27541_REG_MLI;
		chip->cmd_addr.reg_ap = BQ27541_REG_AP;
		chip->cmd_addr.reg_soc = BQ27541_REG_SOC;
		chip->cmd_addr.reg_inttemp = BQ27541_REG_INTTEMP;
		chip->cmd_addr.reg_soh = BQ27541_REG_SOH;
		chip->cmd_addr.flag_dsc = BQ27541_FLAG_DSC;
		chip->cmd_addr.flag_fc = BQ27541_FLAG_FC;
		chip->cmd_addr.cs_dlogen = BQ27541_CS_DLOGEN;
		chip->cmd_addr.cs_ss = BQ27541_CS_SS;
		chip->cmd_addr.reg_ar = BQ27541_REG_AR;
		chip->cmd_addr.reg_artte = BQ27541_REG_ARTTE;
		chip->cmd_addr.reg_tte = BQ27541_REG_TTE;
		chip->cmd_addr.reg_ttf = BQ27541_REG_TTF;
		chip->cmd_addr.reg_stte = BQ27541_REG_STTE;
		chip->cmd_addr.reg_mltte = BQ27541_REG_MLTTE;
		chip->cmd_addr.reg_ae = BQ27541_REG_AE;
		chip->cmd_addr.reg_ttecp = BQ27541_REG_TTECP;
		chip->cmd_addr.reg_cc = BQ27541_REG_CC;
		chip->cmd_addr.reg_nic = BQ27541_REG_NIC;
		chip->cmd_addr.reg_icr = BQ27541_REG_ICR;
		chip->cmd_addr.reg_logidx = BQ27541_REG_LOGIDX;
		chip->cmd_addr.reg_logbuf = BQ27541_REG_LOGBUF;
		chip->cmd_addr.reg_dod0 = BQ27541_REG_DOD0;
		chip->cmd_addr.subcmd_cntl_status = BQ27541_SUBCMD_CTNL_STATUS;
		chip->cmd_addr.subcmd_device_type = BQ27541_SUBCMD_DEVCIE_TYPE;
		chip->cmd_addr.subcmd_fw_ver = BQ27541_SUBCMD_FW_VER;
		chip->cmd_addr.subcmd_dm_code = BQ27541_BQ27411_CMD_INVALID;
		chip->cmd_addr.subcmd_prev_macw = BQ27541_SUBCMD_PREV_MACW;
		chip->cmd_addr.subcmd_chem_id = BQ27541_SUBCMD_CHEM_ID;
		chip->cmd_addr.subcmd_set_hib = BQ27541_SUBCMD_SET_HIB;
		chip->cmd_addr.subcmd_clr_hib = BQ27541_SUBCMD_CLR_HIB;
		chip->cmd_addr.subcmd_set_cfg = BQ27541_BQ27411_CMD_INVALID;
		chip->cmd_addr.subcmd_sealed = BQ27541_SUBCMD_SEALED;
		chip->cmd_addr.subcmd_reset = BQ27541_SUBCMD_RESET;
		chip->cmd_addr.subcmd_softreset = BQ27541_BQ27411_CMD_INVALID;
		chip->cmd_addr.subcmd_exit_cfg = BQ27541_BQ27411_CMD_INVALID;
		chip->cmd_addr.subcmd_enable_dlog = BQ27541_SUBCMD_ENABLE_DLOG;
		chip->cmd_addr.subcmd_disable_dlog = BQ27541_SUBCMD_DISABLE_DLOG;
		chip->cmd_addr.subcmd_enable_it = BQ27541_SUBCMD_ENABLE_IT;
		chip->cmd_addr.subcmd_disable_it = BQ27541_SUBCMD_DISABLE_IT;
		chip->cmd_addr.subcmd_hw_ver = BQ27541_SUBCMD_HW_VER;
		chip->cmd_addr.subcmd_df_csum = BQ27541_SUBCMD_DF_CSUM;
		chip->cmd_addr.subcmd_bd_offset = BQ27541_SUBCMD_BD_OFFSET;
		chip->cmd_addr.subcmd_int_offset = BQ27541_SUBCMD_INT_OFFSET;
		chip->cmd_addr.subcmd_cc_ver = BQ27541_SUBCMD_CC_VER;
		chip->cmd_addr.subcmd_ocv = BQ27541_SUBCMD_OCV;
		chip->cmd_addr.subcmd_bat_ins = BQ27541_SUBCMD_BAT_INS;
		chip->cmd_addr.subcmd_bat_rem = BQ27541_SUBCMD_BAT_REM;
		chip->cmd_addr.subcmd_set_slp = BQ27541_SUBCMD_SET_SLP;
		chip->cmd_addr.subcmd_clr_slp = BQ27541_SUBCMD_CLR_SLP;
		chip->cmd_addr.subcmd_fct_res = BQ27541_SUBCMD_FCT_RES;
		chip->cmd_addr.subcmd_cal_mode = BQ27541_SUBCMD_CAL_MODE;
	} else {		/*device_bq27411*/
		chip->cmd_addr.reg_cntl = BQ27411_REG_CNTL;
		chip->cmd_addr.reg_temp = BQ27411_REG_TEMP;
		chip->cmd_addr.reg_volt = BQ27411_REG_VOLT;
		chip->cmd_addr.reg_flags = BQ27411_REG_FLAGS;
		chip->cmd_addr.reg_nac = BQ27411_REG_NAC;
		chip->cmd_addr.reg_fac = BQ27411_REG_FAC;
		chip->cmd_addr.reg_rm = BQ27411_REG_RM;
		chip->cmd_addr.reg_fcc = BQ27411_REG_FCC;
		chip->cmd_addr.reg_ai = BQ27411_REG_AI;
		chip->cmd_addr.reg_si = BQ27411_REG_SI;
		chip->cmd_addr.reg_mli = BQ27411_REG_MLI;
		chip->cmd_addr.reg_ap = BQ27411_REG_AP;
		chip->cmd_addr.reg_soc = BQ27411_REG_SOC;
		chip->cmd_addr.reg_inttemp = BQ27411_REG_INTTEMP;
		chip->cmd_addr.reg_soh = BQ27411_REG_SOH;
		chip->cmd_addr.reg_fc = BQ27411_REG_FC;
		chip->cmd_addr.reg_qm = BQ27411_REG_QM;
		chip->cmd_addr.reg_pd = BQ27411_REG_PD;
		chip->cmd_addr.reg_rcu = BQ27411_REG_RCU;
		chip->cmd_addr.reg_rcf = BQ27411_REG_RCF;
		chip->cmd_addr.reg_fcu = BQ27411_REG_FCU;
		chip->cmd_addr.reg_fcf = BQ27411_REG_FCF;
		chip->cmd_addr.reg_sou = BQ27411_REG_SOU;
		chip->cmd_addr.reg_do0 = BQ27411_REG_DO0;
		chip->cmd_addr.reg_doe = BQ27411_REG_DOE;
		chip->cmd_addr.reg_trm = BQ27411_REG_TRM;
		chip->cmd_addr.reg_pc = BQ27411_REG_PC;
		chip->cmd_addr.reg_qs = BQ27411_REG_QS;
		chip->cmd_addr.flag_dsc = BQ27411_FLAG_DSC;
		chip->cmd_addr.flag_fc = BQ27411_FLAG_FC;
		chip->cmd_addr.cs_dlogen = BQ27411_CS_DLOGEN;
		chip->cmd_addr.cs_ss = BQ27411_CS_SS;
		/*bq27541 external standard cmds*/
		chip->cmd_addr.reg_ar = BQ27541_BQ27411_CMD_INVALID;
		chip->cmd_addr.reg_artte = BQ27541_BQ27411_CMD_INVALID;
		chip->cmd_addr.reg_tte = BQ27541_BQ27411_CMD_INVALID;
		chip->cmd_addr.reg_ttf = BQ27541_BQ27411_CMD_INVALID;
		chip->cmd_addr.reg_stte = BQ27541_BQ27411_CMD_INVALID;
		chip->cmd_addr.reg_mltte = BQ27541_BQ27411_CMD_INVALID;
		chip->cmd_addr.reg_ae = BQ27541_BQ27411_CMD_INVALID;
		chip->cmd_addr.reg_ttecp = BQ27541_BQ27411_CMD_INVALID;
		chip->cmd_addr.reg_cc = BQ27541_BQ27411_CMD_INVALID;
		chip->cmd_addr.reg_nic = BQ27541_BQ27411_CMD_INVALID;
		chip->cmd_addr.reg_icr = BQ27541_BQ27411_CMD_INVALID;
		chip->cmd_addr.reg_logidx = BQ27541_BQ27411_CMD_INVALID;
		chip->cmd_addr.reg_logbuf = BQ27541_BQ27411_CMD_INVALID;
		chip->cmd_addr.reg_dod0 = BQ27541_BQ27411_CMD_INVALID;
		chip->cmd_addr.subcmd_cntl_status = BQ27411_SUBCMD_CNTL_STATUS;
		chip->cmd_addr.subcmd_device_type = BQ27411_SUBCMD_DEVICE_TYPE;
		chip->cmd_addr.subcmd_fw_ver = BQ27411_SUBCMD_FW_VER;
		chip->cmd_addr.subcmd_dm_code = BQ27541_BQ27411_CMD_INVALID;
		chip->cmd_addr.subcmd_prev_macw = BQ27411_SUBCMD_PREV_MACW;
		chip->cmd_addr.subcmd_chem_id = BQ27411_SUBCMD_CHEM_ID;
		chip->cmd_addr.subcmd_set_hib = BQ27411_SUBCMD_SET_HIB;
		chip->cmd_addr.subcmd_clr_hib = BQ27411_SUBCMD_CLR_HIB;
		chip->cmd_addr.subcmd_set_cfg = BQ27541_BQ27411_CMD_INVALID;
		chip->cmd_addr.subcmd_sealed = BQ27411_SUBCMD_SEALED;
		chip->cmd_addr.subcmd_reset = BQ27411_SUBCMD_RESET;
		chip->cmd_addr.subcmd_softreset = BQ27541_BQ27411_CMD_INVALID;
		chip->cmd_addr.subcmd_exit_cfg = BQ27541_BQ27411_CMD_INVALID;
		chip->cmd_addr.subcmd_enable_dlog = BQ27411_SUBCMD_ENABLE_DLOG;
		chip->cmd_addr.subcmd_disable_dlog = BQ27411_SUBCMD_DISABLE_DLOG;
		chip->cmd_addr.subcmd_enable_it = BQ27411_SUBCMD_ENABLE_IT;
		chip->cmd_addr.subcmd_disable_it = BQ27411_SUBCMD_DISABLE_IT;
		/*bq27541 external sub cmds*/
		chip->cmd_addr.subcmd_hw_ver = BQ27541_BQ27411_CMD_INVALID;
		chip->cmd_addr.subcmd_df_csum = BQ27541_BQ27411_CMD_INVALID;
		chip->cmd_addr.subcmd_bd_offset = BQ27541_BQ27411_CMD_INVALID;
		chip->cmd_addr.subcmd_int_offset = BQ27541_BQ27411_CMD_INVALID;
		chip->cmd_addr.subcmd_cc_ver = BQ27541_BQ27411_CMD_INVALID;
		chip->cmd_addr.subcmd_ocv = BQ27541_BQ27411_CMD_INVALID;
		chip->cmd_addr.subcmd_bat_ins = BQ27541_BQ27411_CMD_INVALID;
		chip->cmd_addr.subcmd_bat_rem = BQ27541_BQ27411_CMD_INVALID;
		chip->cmd_addr.subcmd_set_slp = BQ27541_BQ27411_CMD_INVALID;
		chip->cmd_addr.subcmd_clr_slp = BQ27541_BQ27411_CMD_INVALID;
		chip->cmd_addr.subcmd_fct_res = BQ27541_BQ27411_CMD_INVALID;
		chip->cmd_addr.subcmd_cal_mode = BQ27541_BQ27411_CMD_INVALID;
	}
}

#ifndef OPLUS_CHG_OP_DEF
static void bq27541_hw_config(struct chip_bq27541 *chip)
{
	int ret = 0;
	int flags = 0;
	int device_type = 0;
	int fw_ver = 0;

	bq27541_cntl_cmd(BQ27541_BQ27411_SUBCMD_CTNL_STATUS);
	udelay(66);
	bq27541_read_i2c(BQ27541_BQ27411_REG_CNTL, &flags);
	udelay(66);
	ret = bq27541_read_i2c(BQ27541_BQ27411_REG_CNTL, &flags);
	if (ret < 0) {
		chip->device_type = DEVICE_BQ27541;
		pr_err(" error reading register %02x ret = %d\n",
			BQ27541_BQ27411_REG_CNTL, ret);
		return;
	}
	udelay(66);
	bq27541_cntl_cmd(BQ27541_BQ27411_SUBCMD_CTNL_STATUS);
	udelay(66);
	bq27541_cntl_cmd(BQ27541_BQ27411_SUBCMD_DEVICE_TYPE);
	udelay(66);
	bq27541_read_i2c(BQ27541_BQ27411_REG_CNTL, &device_type);
	udelay(66);
	bq27541_cntl_cmd(BQ27541_BQ27411_SUBCMD_CTNL_STATUS);
	udelay(66);
	bq27541_cntl_cmd(BQ27541_BQ27411_SUBCMD_FW_VER);
	udelay(66);
	bq27541_read_i2c(BQ27541_BQ27411_REG_CNTL, &fw_ver);

	if (device_type == DEVICE_TYPE_BQ27411) {
		chip->device_type = DEVICE_BQ27411;
		chip->device_type_for_warp = DEVICE_TYPE_FOR_WARP_BQ27411;
	} else if (device_type == DEVICE_TYPE_ZY0602) {
		chip->device_type = DEVICE_ZY0602;
		chip->device_type_for_warp = DEVICE_TYPE_FOR_WARP_BQ27541;
	} else {
		chip->device_type = DEVICE_BQ27541;
		chip->device_type_for_warp = DEVICE_TYPE_FOR_WARP_BQ27541;
		if (!chip->batt_bq28z610) {
			bq27541_cntl_cmd(BQ27541_BQ27411_SUBCMD_CTNL_STATUS);
			udelay(66);
			bq27541_cntl_cmd(BQ27541_BQ27411_SUBCMD_ENABLE_IT);
		}
	}
	gauge_set_cmd_addr(chip, chip->device_type);
	if (device_type == DEVICE_TYPE_BQ28Z610) {
		chip->cmd_addr.reg_ai = Bq28Z610_REG_TI;
	}
	oplus_set_fg_device_type(chip->device_type);
	dev_err(chip->dev, "DEVICE_TYPE is 0x%02X, FIRMWARE_VERSION is 0x%02X\n",
		device_type, fw_ver);
}
#else /* OPLUS_CHG_OP_DEF */
static void bq27541_hw_config(struct chip_bq27541 *chip)
{
	int ret = 0;
	int flags = 0;
	int device_type = 0;
	int fw_ver = 0;

	bq27541_cntl_cmd(BQ27541_BQ27411_SUBCMD_CTNL_STATUS);
	udelay(66);
	ret = bq27541_read_i2c(BQ27541_BQ27411_REG_CNTL, &flags);
	if (ret != 0) {
		pr_err(" error reading register %02x ret = %d\n",
			BQ27541_BQ27411_REG_CNTL, ret);
		goto error;
	}
	udelay(66);
	ret = bq27541_read_i2c(BQ27541_BQ27411_REG_CNTL, &flags);
	if (ret != 0) {
		pr_err(" error reading register %02x ret = %d\n",
			BQ27541_BQ27411_REG_CNTL, ret);
		goto error;
	}
	pr_err("bq_present, ret=%d, flag=%d\n", ret, flags);
	chip->bq_present = true;
	udelay(66);
	bq27541_cntl_cmd(BQ27541_BQ27411_SUBCMD_CTNL_STATUS);
	udelay(66);
	bq27541_cntl_cmd(BQ27541_BQ27411_SUBCMD_DEVICE_TYPE);
	udelay(66);
	ret = bq27541_read_i2c(BQ27541_BQ27411_REG_CNTL, &device_type);
	if (ret != 0) {
		pr_err(" error reading register %02x ret = %d\n",
			BQ27541_BQ27411_REG_CNTL, ret);
		goto error;
	}
	udelay(66);
	bq27541_cntl_cmd(BQ27541_BQ27411_SUBCMD_CTNL_STATUS);
	udelay(66);
	bq27541_cntl_cmd(BQ27541_BQ27411_SUBCMD_FW_VER);
	udelay(66);
	ret = bq27541_read_i2c(BQ27541_BQ27411_REG_CNTL, &fw_ver);
	if (ret != 0) {
		pr_err(" error reading register %02x ret = %d\n",
			BQ27541_BQ27411_REG_CNTL, ret);
		goto error;
	}

	if (device_type == DEVICE_TYPE_BQ27411) {
		chip->device_type = DEVICE_BQ27411;
		chip->device_type_for_warp = DEVICE_TYPE_FOR_WARP_BQ27411;
	} else if (device_type == DEVICE_TYPE_ZY0602) {
		chip->device_type = DEVICE_ZY0602;
		chip->device_type_for_warp = DEVICE_TYPE_FOR_WARP_BQ27541;
	} else {
		chip->device_type = DEVICE_BQ27541;
		chip->device_type_for_warp = DEVICE_TYPE_FOR_WARP_BQ27541;
		if (!chip->batt_bq28z610) {
			bq27541_cntl_cmd(BQ27541_BQ27411_SUBCMD_CTNL_STATUS);
			udelay(66);
			bq27541_cntl_cmd(BQ27541_BQ27411_SUBCMD_ENABLE_IT);
		}
	}
	gauge_set_cmd_addr(chip, chip->device_type);
	if (device_type == DEVICE_TYPE_BQ28Z610) {
		chip->cmd_addr.reg_ai = Bq28Z610_REG_TI;
	}
	oplus_set_fg_device_type(chip->device_type);
	dev_err(chip->dev, "DEVICE_TYPE is 0x%02X, FIRMWARE_VERSION is 0x%02X\n",
		device_type, fw_ver);
	return;

error:
	chip->device_type = DEVICE_BQ27541;
	chip->bq_present = false;
}
#endif /* OPLUS_CHG_OP_DEF */

static void bq27541_parse_dt(struct chip_bq27541 *chip)
{
	struct device_node *node = chip->dev->of_node;
	int rc = 0;
	chip->modify_soc_smooth = of_property_read_bool(node, "qcom,modify-soc-smooth");
	chip->modify_soc_calibration = of_property_read_bool(node, "qcom,modify-soc-calibration");
	chip->batt_bq28z610 = of_property_read_bool(node, "qcom,batt_bq28z610");
	chip->bq28z610_need_balancing = of_property_read_bool(node, "qcom,bq28z610_need_balancing");
	chip->battery_full_param = of_property_read_bool(node, "qcom,battery-full-param");//only for wite battery full param in guage dirver probe on 7250 platform
	rc = of_property_read_u32(node, "qcom,sha1_key_index", &chip->sha1_key_index);
	if(rc) {
		chip->sha1_key_index = 0;
	}
}

static int sealed(void)
{
	/*    return control_cmd_read(di, CONTROL_STATUS) & (1 << 13);*/
	int value = 0;
	int rc;

	bq27541_cntl_cmd(CONTROL_STATUS);
	/*    bq27541_cntl_cmd(di, CONTROL_STATUS);*/
	usleep_range(10000, 10000);
	rc = bq27541_read_i2c(CONTROL_STATUS, &value);
	if (rc < 0)
		value = 0;
	/*    chg_debug(" REG_CNTL: 0x%x\n", value); */

	if (gauge_ic->device_type == DEVICE_BQ27541 || gauge_ic->device_type == DEVICE_ZY0602) {
		return value & BIT(14);
	} else if (gauge_ic->device_type == DEVICE_BQ27411) {
		return value & BIT(13);
	} else {
		return 1;
	}
}

static int seal(void)
{
	int i = 0;

	if (sealed()) {
		pr_err("bq27541/27411 sealed, return\n");
		return 1;
	}
	bq27541_cntl_cmd(SEAL_SUBCMD);
	usleep_range(10000, 10000);
	for (i = 0;i < SEAL_POLLING_RETRY_LIMIT;i++) {
		if (sealed()) {
			return 1;
		}
		usleep_range(10000, 10000);
	}
	return 0;
}


static int unseal(u32 key)
{
	int i = 0;

	if (!sealed()) {
		goto out;
	}
	if (gauge_ic->device_type == DEVICE_BQ27541 || gauge_ic->device_type == DEVICE_ZY0602) {
		/*    bq27541_write(CONTROL_CMD, key & 0xFFFF, false, di);*/
		bq27541_cntl_cmd(0x1115);
		usleep_range(10000, 10000);
		/*    bq27541_write(CONTROL_CMD, (key & 0xFFFF0000) >> 16, false, di);*/
		bq27541_cntl_cmd(0x1986);
		usleep_range(10000, 10000);
	}
	else if (gauge_ic->device_type == DEVICE_BQ27411) {
		/*    bq27541_write(CONTROL_CMD, key & 0xFFFF, false, di);*/
		bq27541_cntl_cmd(0x8000);
		usleep_range(10000, 10000);
		/*    bq27541_write(CONTROL_CMD, (key & 0xFFFF0000) >> 16, false, di);*/
		bq27541_cntl_cmd(0x8000);
		usleep_range(10000, 10000);
	}
	bq27541_cntl_cmd(0xffff);
	usleep_range(10000, 10000);
	bq27541_cntl_cmd(0xffff);
	usleep_range(10000, 10000);

	while (i < SEAL_POLLING_RETRY_LIMIT) {
		i++;
		if (!sealed()) {
			break;
		}
		usleep_range(10000, 10000);
	}

out:
	chg_debug("bq27541 : i=%d\n", i);

	if (i == SEAL_POLLING_RETRY_LIMIT) {
		pr_err("bq27541 failed\n");
		return 0;
	} else {
		return 1;
	}
}

static int bq27411_write_block_data_cmd(struct chip_bq27541 *chip,
				int block_id, u8 reg_addr, u8 new_value)
{
	int rc = 0;
	u8 old_value = 0, old_csum = 0, new_csum = 0;
	/*u8 new_csum_test = 0, csum_temp = 0;*/

	usleep_range(1000, 1000);
	bq27541_i2c_txsubcmd(BQ27411_DATA_CLASS_ACCESS, block_id);
	usleep_range(10000, 10000);
	rc = bq27541_read_i2c_onebyte(reg_addr, &old_value);
	if (rc) {
		pr_err("%s read reg_addr = 0x%x fail\n", __func__, reg_addr);
		return 1;
	}
	if (old_value == new_value) {
		return 0;
	}
	usleep_range(1000, 1000);
	rc = bq27541_read_i2c_onebyte(BQ27411_CHECKSUM_ADDR, &old_csum);
	if (rc) {
		pr_err("%s read checksum fail\n", __func__);
		return 1;
	}
	usleep_range(1000, 1000);
	bq27541_i2c_txsubcmd_onebyte(reg_addr, new_value);
	usleep_range(1000, 1000);
	new_csum = (old_value + old_csum - new_value) & 0xff;
/*
	csum_temp = (255 - old_csum - old_value) % 256;
	new_csum_test = 255 - ((csum_temp + new_value) % 256);
*/
	usleep_range(1000, 1000);
	bq27541_i2c_txsubcmd_onebyte(BQ27411_CHECKSUM_ADDR, new_csum);
	pr_err("bq27411 write blk_id = 0x%x, addr = 0x%x, old_val = 0x%x, new_val = 0x%x, old_csum = 0x%x, new_csum = 0x%x\n",
		block_id, reg_addr, old_value, new_value, old_csum, new_csum);
	return 0;
}

static int bq27411_read_block_data_cmd(struct chip_bq27541 *chip,
		int block_id, u8 reg_addr)
{
	u8 value = 0;

	usleep_range(1000, 1000);
	bq27541_i2c_txsubcmd(BQ27411_DATA_CLASS_ACCESS, block_id);
	usleep_range(10000, 10000);
	bq27541_read_i2c_onebyte(reg_addr, &value);
	return value;
}

static int bq27411_enable_config_mode(struct chip_bq27541 *chip, bool enable)
{
	int config_mode = 0;
	int i = 0;
	int rc = 0;

	if (enable) {		/*enter config mode*/
		usleep_range(1000, 1000);
		bq27541_cntl_cmd(BQ27411_SUBCMD_SET_CFG);
		usleep_range(1000, 1000);
		for (i = 0; i < BQ27411_CONFIG_MODE_POLLING_LIMIT; i++) {
			rc = bq27541_read_i2c(BQ27411_SUBCMD_CONFIG_MODE, &config_mode);
			if (rc < 0) {
				pr_err("%s i2c read error\n", __func__);
				return 1;
			}
			if (config_mode & BIT(4)) {
				break;
			}
			msleep(50);
		}
	} else {		/* exit config mode */
		usleep_range(1000, 1000);
		bq27541_cntl_cmd(BQ27411_SUBCMD_EXIT_CFG);
		usleep_range(1000, 1000);
		for (i = 0; i < BQ27411_CONFIG_MODE_POLLING_LIMIT; i++) {
			rc = bq27541_read_i2c(BQ27411_SUBCMD_CONFIG_MODE, &config_mode);
			if (rc < 0) {
				pr_err("%s i2c read error\n", __func__);
				return 1;
			}
			if ((config_mode & BIT(4)) == 0) {
				break;
			}
			msleep(50);
		}
	}
	if (i == BQ27411_CONFIG_MODE_POLLING_LIMIT) {
		pr_err("%s fail config_mode = 0x%x, enable = %d\n", __func__, config_mode, enable);
		return 1;
	} else {
		pr_err("%s success i = %d, config_mode = 0x%x, enable = %d\n",
			__func__, i, config_mode, enable);
		return 0;
	}
}

static bool bq27411_check_soc_smooth_parameter(struct chip_bq27541 *chip, bool is_powerup)
{
	int value_read = 0;
	u8 dead_band_val = 0;
	u8 op_cfgb_val = 0;
	u8 dodat_val = 0;
	u8 rc = 0;

	return true;	/*not check because it costs 5.5 seconds */

	msleep(4000);
	if (sealed()) {
		if (!unseal(BQ27411_UNSEAL_KEY)) {
			return false;
		} else {
			msleep(50);
		}
	}
	if (is_powerup) {
		dead_band_val = BQ27411_CC_DEAD_BAND_POWERUP_VALUE;
		op_cfgb_val = BQ27411_OPCONFIGB_POWERUP_VALUE;
		dodat_val = BQ27411_DODATEOC_POWERUP_VALUE;
	} else {	/*shutdown*/
		dead_band_val = BQ27411_CC_DEAD_BAND_SHUTDOWN_VALUE;
		op_cfgb_val = BQ27411_OPCONFIGB_SHUTDOWN_VALUE;
		dodat_val = BQ27411_DODATEOC_SHUTDOWN_VALUE;
	}
	rc = bq27411_enable_config_mode(chip, true);
	if (rc) {
		pr_err("%s enable config mode fail\n", __func__);
		return false;
	}
	/*enable block data control */
	rc = bq27541_i2c_txsubcmd_onebyte(BQ27411_BLOCK_DATA_CONTROL, 0x00);
	if (rc) {
		pr_err("%s enable block data control fail\n", __func__);
		goto check_error;
	}
	usleep_range(5000, 5000);
	/*check cc-dead-band*/
	value_read = bq27411_read_block_data_cmd(chip,
		BQ27411_CC_DEAD_BAND_ID, BQ27411_CC_DEAD_BAND_ADDR);
	if (value_read != dead_band_val) {
		pr_err("%s cc_dead_band error, value_read = 0x%x\n", __func__, value_read);
		goto check_error;
	}
	/*check opconfigB*/
	value_read = bq27411_read_block_data_cmd(chip,
		BQ27411_OPCONFIGB_ID, BQ27411_OPCONFIGB_ADDR);
	if (value_read != op_cfgb_val) {
		pr_err("%s opconfigb error, value_read = 0x%x\n", __func__, value_read);
		goto check_error;
	}
	/*check dodateoc*/
	value_read = bq27411_read_block_data_cmd(chip,
		BQ27411_DODATEOC_ID, BQ27411_DODATEOC_ADDR);
	if (value_read != dodat_val) {
		pr_err("%s dodateoc error, value_read = 0x%x\n", __func__, value_read);
		goto check_error;
	}
	bq27411_enable_config_mode(chip, false);
	return true;

check_error:
	bq27411_enable_config_mode(chip, false);
	return false;
}

//only for wite battery full param in guage dirver probe on 7250 platform
static int bq27441_battery_full_param_write_cmd(struct chip_bq27541 *chip)
{
	u8 reg_data = 0, rc = 0;
	u8 CNTL1_VAL_1[2] = {0x52,0x00};
	u8 CNTL1_VAL_2[2] = {0x00,0x00};
	u8 CNTL1_VAL_3[2] = {0x00,0x00};
	u8 CNTL1_VAL_4[2] = {0x00,0x00};
	u8 CNTL1_VAL_5[2] = {0x00,0x00};
	u8 CNTL1_VAL_6[2] = {0x00,0x00};
	u8 CNTL1_VAL_7[2] = {0x00,0x00};
	u8 CNTL1_VAL_8[2] = {0x00,0x00};
	u8 CNTL1_VAL_9[2] = {0x00,0x00};
	u8 read_buf[5] = {0};
	pr_err("%s begin\n", __func__);

	CNTL1_VAL_1[0] = 0x6C;
	CNTL1_VAL_1[1] = 0x00;
	bq27541_write_i2c_block(0x3E, 2, CNTL1_VAL_1);
	usleep_range(15000,15000);
	pr_err("%s 0x3E -->write [0x%02x][0x%02x]\n", __func__, CNTL1_VAL_1[0], CNTL1_VAL_1[1]);

	rc = bq27541_read_i2c_block(0x40, 2, read_buf);
	pr_err("%s 0x40 -->read [0x%02x][0x%02x]\n", __func__, read_buf[0], read_buf[1]);
	if(read_buf[0] == 0xFF && read_buf[1] == 0xBA){
		rc = bq27541_read_i2c_onebyte(0x60, &reg_data);	
		usleep_range(15000,15000);
		pr_err("%s 0x60 -->read [0x%02x]\n", __func__, reg_data);

		CNTL1_VAL_1[0] = 0xFE;
		CNTL1_VAL_1[1] = 0xCF;
		bq27541_write_i2c_block(0x40, 2, CNTL1_VAL_1);
		pr_err("%s 0x40 -->write [0x%02x][0x%02x]\n", __func__, CNTL1_VAL_1[0], CNTL1_VAL_1[1]);

		CNTL1_VAL_2[0] = 0x00;
		CNTL1_VAL_2[1] = 0x00;
		bq27541_write_i2c_block(0x42, 2, CNTL1_VAL_2);
		pr_err("%s 0x42 -->write [0x%02x][0x%02x]\n", __func__, CNTL1_VAL_2[0], CNTL1_VAL_2[1]);

		CNTL1_VAL_3[0] = 0xFE;
		CNTL1_VAL_3[1] = 0x03;
		bq27541_write_i2c_block(0x44, 2, CNTL1_VAL_3);
		pr_err("%s 0x44 -->write [0x%02x][0x%02x]\n", __func__, CNTL1_VAL_3[0], CNTL1_VAL_3[1]);

		CNTL1_VAL_4[0] = 0x2A;
		CNTL1_VAL_4[1] = 0xD9;
		bq27541_write_i2c_block(0x46, 2, CNTL1_VAL_4);
		pr_err("%s 0x46 -->write [0x%02x][0x%02x]\n", __func__, CNTL1_VAL_4[0], CNTL1_VAL_4[1]);

		CNTL1_VAL_5[0] = 0x08;
		CNTL1_VAL_5[1] = 0x90;
		bq27541_write_i2c_block(0x48, 2, CNTL1_VAL_5);
		pr_err("%s 0x48 -->write [0x%02x][0x%02x]\n", __func__, CNTL1_VAL_5[0], CNTL1_VAL_5[1]);

		CNTL1_VAL_6[0] = 0xDA;
		CNTL1_VAL_6[1] = 0xFA;
		bq27541_write_i2c_block(0x4A, 2, CNTL1_VAL_6);
		pr_err("%s 0x4A -->write [0x%02x][0x%02x]\n", __func__, CNTL1_VAL_6[0], CNTL1_VAL_6[1]);

		CNTL1_VAL_7[0] = 0xD3;
		CNTL1_VAL_7[1] = 0xDE;
		bq27541_write_i2c_block(0x4C, 2, CNTL1_VAL_7);
		pr_err("%s 0x4C -->write [0x%02x][0x%02x]\n", __func__, CNTL1_VAL_7[0], CNTL1_VAL_7[1]);

		CNTL1_VAL_8[0] = 0xC2;
		CNTL1_VAL_8[1] = 0x3D;
		bq27541_write_i2c_block(0x4E, 2, CNTL1_VAL_8);
		pr_err("%s 0x4E -->write [0x%02x][0x%02x]\n", __func__, CNTL1_VAL_8[0], CNTL1_VAL_8[1]);

		CNTL1_VAL_9[0] = 0x7F;
		CNTL1_VAL_9[1] = 0x00;
		bq27541_write_i2c_block(0x50, 2, CNTL1_VAL_9);
		pr_err("%s 0x50 -->write [0x%02x][0x%02x]\n", __func__, CNTL1_VAL_9[0], CNTL1_VAL_9[1]);

		bq27541_i2c_txsubcmd_onebyte(0x60, (0xFFFF - (CNTL1_VAL_1[0] + CNTL1_VAL_1[1] + 
											CNTL1_VAL_2[0] + CNTL1_VAL_2[1] + 
											CNTL1_VAL_3[0] + CNTL1_VAL_3[1] +  
											CNTL1_VAL_4[0] + CNTL1_VAL_4[1] + 
											CNTL1_VAL_5[0] + CNTL1_VAL_5[1] + 
											CNTL1_VAL_6[0] + CNTL1_VAL_6[1] + 
											CNTL1_VAL_7[0] + CNTL1_VAL_7[1] + 
											CNTL1_VAL_8[0] + CNTL1_VAL_8[1] + 
											CNTL1_VAL_9[0] + CNTL1_VAL_9[1])));
		pr_err("%s 0x60 -->write [0x%02x]\n", __func__,(0xFFFF - (
											CNTL1_VAL_1[0] + CNTL1_VAL_1[1] + 
											CNTL1_VAL_2[0] + CNTL1_VAL_2[1] + 
											CNTL1_VAL_3[0] + CNTL1_VAL_3[1] +  
											CNTL1_VAL_4[0] + CNTL1_VAL_4[1] + 
											CNTL1_VAL_5[0] + CNTL1_VAL_5[1] + 
											CNTL1_VAL_6[0] + CNTL1_VAL_6[1] + 
											CNTL1_VAL_7[0] + CNTL1_VAL_7[1] + 
											CNTL1_VAL_8[0] + CNTL1_VAL_8[1] + 
											CNTL1_VAL_9[0] + CNTL1_VAL_9[1])));
		usleep_range(30000,30000);

		CNTL1_VAL_1[0] = 0x52;
		CNTL1_VAL_1[1] = 0x00;
		bq27541_write_i2c_block(0x3E, 2, CNTL1_VAL_1);
		usleep_range(15000,15000);
		pr_err("%s 0x3E -->write [0x%02x][0x%02x]\n", __func__, CNTL1_VAL_1[0], CNTL1_VAL_1[1]);
	
		rc = bq27541_read_i2c_block(0x5B, 2, read_buf);
		pr_err("%s 0x5B -->read [0x%02x][0x%02x]\n", __func__, read_buf[0], read_buf[1]);

		rc = bq27541_read_i2c_onebyte(0x60, &reg_data); 
		usleep_range(15000,15000);
		pr_err("%s 0x60 -->read [0x%02x]\n", __func__, reg_data);

		CNTL1_VAL_1[0] = 0x00;
		CNTL1_VAL_1[1] = 0x8F;
		bq27541_write_i2c_block(0x5B, 2, CNTL1_VAL_1);
		pr_err("%s 0x5B -->write [0x%02x][0x%02x]\n", __func__, CNTL1_VAL_1[0], CNTL1_VAL_1[1]);
		bq27541_i2c_txsubcmd_onebyte(0x60, (read_buf[0] + read_buf[1] + reg_data - CNTL1_VAL_1[0] - CNTL1_VAL_1[1] ));
		pr_err("%s 0x60 -->write [0x%02x]\n", __func__,(read_buf[0] + read_buf[1] + reg_data - CNTL1_VAL_1[0] - CNTL1_VAL_1[1] ));
		usleep_range(15000,15000);

		CNTL1_VAL_1[0] = 0x52;
		CNTL1_VAL_1[1] = 0x00;
		usleep_range(15000,15000);
		bq27541_write_i2c_block(0x3E, 2, CNTL1_VAL_1);
		usleep_range(15000,15000);
		pr_err("%s 0x3E -->write [0x%02x][0x%02x]\n", __func__, CNTL1_VAL_1[0], CNTL1_VAL_1[1]);
	
		rc = bq27541_read_i2c_block(0x5D, 2, read_buf);
		pr_err("%s 0x5D -->read [0x%02x][0x%02x]\n", __func__, read_buf[0], read_buf[1]);

		rc = bq27541_read_i2c_onebyte(0x60, &reg_data);	
		usleep_range(15000,15000);
		pr_err("%s 0x60 -->read [0x%02x]\n", __func__, reg_data);

		CNTL1_VAL_1[0] = 0x10;
		CNTL1_VAL_1[1] = 0xEF;
		bq27541_write_i2c_block(0x5D, 2, CNTL1_VAL_1);
		pr_err("%s 0x5D -->write [0x%02x][0x%02x]\n", __func__, CNTL1_VAL_1[0], CNTL1_VAL_1[1]);
		bq27541_i2c_txsubcmd_onebyte(0x60, (read_buf[0] + read_buf[1] + reg_data - CNTL1_VAL_1[0] - CNTL1_VAL_1[1] ));
		pr_err("%s 0x60 -->write [0x%02x]\n", __func__, (read_buf[0] + read_buf[1] + reg_data - CNTL1_VAL_1[0] - CNTL1_VAL_1[1] ));
		usleep_range(15000,15000);

	
		CNTL1_VAL_1[0] = 0x52;
		CNTL1_VAL_1[1] = 0x00;
		bq27541_write_i2c_block(0x3E, 2, CNTL1_VAL_1);
		usleep_range(15000,15000);
		pr_err("%s 0x3E -->write [0x%02x][0x%02x]\n", __func__, CNTL1_VAL_1[0], CNTL1_VAL_1[1]);

		rc = bq27541_read_i2c_block(0x56, 2, read_buf);
		pr_err("%s 0x56 -->read [0x%02x][0x%02x]\n", __func__, read_buf[0], read_buf[1]);
		rc = bq27541_read_i2c_onebyte(0x60, &reg_data);	
		usleep_range(15000,15000);
		pr_err("%s 0x60 -->read [0x%02x]\n", __func__, reg_data);

		CNTL1_VAL_1[0] = 0x00;
		CNTL1_VAL_1[1] = 0x14;
		bq27541_write_i2c_block(0x56, 2, CNTL1_VAL_1);
		pr_err("%s 0x56 -->write [0x%02x][0x%02x]\n", __func__, CNTL1_VAL_1[0], CNTL1_VAL_1[1]);
		bq27541_i2c_txsubcmd_onebyte(0x60, (read_buf[0] + read_buf[1] + reg_data - CNTL1_VAL_1[0] - CNTL1_VAL_1[1] ));
		pr_err("%s 0x60 -->write [0x%02x]\n", __func__,(read_buf[0] + read_buf[1] + reg_data - CNTL1_VAL_1[0] - CNTL1_VAL_1[1] ));
		usleep_range(15000,15000);

		CNTL1_VAL_1[0] = 0x59;
		CNTL1_VAL_1[1] = 0x00;
		bq27541_write_i2c_block(0x3E, 2, CNTL1_VAL_1);
		usleep_range(15000,15000);
		pr_err("%s 0x3E -->write [0x%02x][0x%02x]\n", __func__, CNTL1_VAL_1[0], CNTL1_VAL_1[1]);

		rc = bq27541_read_i2c_block(0x40, 2, read_buf);
		pr_err("%s 0x40 -->read [0x%02x][0x%02x]\n", __func__, read_buf[0], read_buf[1]);
		rc = bq27541_read_i2c_onebyte(0x60, &reg_data);	
		usleep_range(15000,15000);
		pr_err("%s 0x60 -->read [0x%02x]\n", __func__, reg_data);

		CNTL1_VAL_1[0] = 0x00;
		CNTL1_VAL_1[1] = 0x4b;
		bq27541_write_i2c_block(0x40, 2, CNTL1_VAL_1);
		pr_err("%s 0x40 -->write [0x%02x][0x%02x]\n", __func__, CNTL1_VAL_1[0], CNTL1_VAL_1[1]);
		bq27541_i2c_txsubcmd_onebyte(0x60, (read_buf[0] + read_buf[1] + reg_data - CNTL1_VAL_1[0] - CNTL1_VAL_1[1] ));
		pr_err("%s 0x60 -->write [0x%02x]\n", __func__,(read_buf[0] + read_buf[1] + reg_data - CNTL1_VAL_1[0] - CNTL1_VAL_1[1] ));
		usleep_range(15000,15000);
	}

	CNTL1_VAL_1[0] = 0x59;
	CNTL1_VAL_1[1] = 0x00;
	bq27541_write_i2c_block(0x3E, 2, CNTL1_VAL_1);
	usleep_range(15000,15000);
	pr_err("%s 0x3E -->write [0x%02x][0x%02x]\n", __func__, CNTL1_VAL_1[0], CNTL1_VAL_1[1]);

	rc = bq27541_read_i2c_block(0x40, 2, read_buf);
	pr_err("%s 0x40 -->read [0x%02x][0x%02x]\n", __func__, read_buf[0], read_buf[1]);

	if(read_buf[0] != 0x00 || read_buf[1] != 0x4b){
		rc = bq27541_read_i2c_onebyte(0x60, &reg_data); 
		pr_err("%s 0x60 -->read [0x%02x]\n", __func__, reg_data);
		usleep_range(15000,15000);

		CNTL1_VAL_2[0] = 0x00;
		CNTL1_VAL_2[1] = 0x4B;
		bq27541_write_i2c_block(0x40, 2, CNTL1_VAL_2);
		pr_err("%s 0x40 -->write [0x%02x][0x%02x]\n", __func__, CNTL1_VAL_2[0], CNTL1_VAL_2[1]);

		bq27541_i2c_txsubcmd_onebyte(0x60, (read_buf[0] + read_buf[1] + reg_data - CNTL1_VAL_2[0] - CNTL1_VAL_2[1] ));
		pr_err("%s 0x60 -->write [0x%02x]\n", __func__,(read_buf[0] + read_buf[1] + reg_data - CNTL1_VAL_2[0] - CNTL1_VAL_2[1] ));
		usleep_range(30000,30000);
		pr_err("%s end\n", __func__);
	}

	pr_err("%s end\n", __func__);
	return GUAGE_OK;
}

static int bq27411_write_soc_smooth_parameter(struct chip_bq27541 *chip, bool is_powerup)
{
	int rc = 0;
	u8 dead_band_val = 0;
	u8 op_cfgb_val = 0;
	u8 dodat_val = 0;

	if (is_powerup) {
		dead_band_val = BQ27411_CC_DEAD_BAND_POWERUP_VALUE;
		op_cfgb_val = BQ27411_OPCONFIGB_POWERUP_VALUE;
		dodat_val = BQ27411_DODATEOC_POWERUP_VALUE;
	} else {	/*shutdown */
		dead_band_val = BQ27411_CC_DEAD_BAND_SHUTDOWN_VALUE;
		op_cfgb_val = BQ27411_OPCONFIGB_SHUTDOWN_VALUE;
		dodat_val = BQ27411_DODATEOC_SHUTDOWN_VALUE;
	}

	/*enter config mode */
	rc = bq27411_enable_config_mode(chip, true);
	if (rc) {
		pr_err("%s enable config mode fail\n", __func__);
		return 1;
	}
	/*enable block data control */
	bq27541_i2c_txsubcmd_onebyte(BQ27411_BLOCK_DATA_CONTROL, 0x00);
	
	//only for wite battery full param in guage dirver probe on 7250 platform
	if(chip->battery_full_param)
	{
		usleep_range(5000, 5000);
		rc = bq27441_battery_full_param_write_cmd(chip);
		if(rc == BATT_FULL_ERROR)
			return BATT_FULL_ERROR;
	}

	usleep_range(5000, 5000);
	/* step1: update cc-dead-band */
	rc = bq27411_write_block_data_cmd(chip, BQ27411_CC_DEAD_BAND_ID,
			BQ27411_CC_DEAD_BAND_ADDR, dead_band_val);
	if (rc) {
		pr_err("%s cc_dead_band fail\n", __func__);
		goto exit_config_mode;
	}
	/* step2: update opconfigB */
	rc = bq27411_write_block_data_cmd(chip, BQ27411_OPCONFIGB_ID,
			BQ27411_OPCONFIGB_ADDR, op_cfgb_val);
	if (rc) {
		pr_err("%s opconfigB fail\n", __func__);
		goto exit_config_mode;
	}
	/* step3: update dodateoc */
	rc = bq27411_write_block_data_cmd(chip, BQ27411_DODATEOC_ID,
			BQ27411_DODATEOC_ADDR, dodat_val);
	if (rc) {
		pr_err("%s dodateoc fail\n", __func__);
		goto exit_config_mode;
	}
	bq27411_enable_config_mode(chip, false);
	return 0;

exit_config_mode:
	bq27411_enable_config_mode(chip, false);
	return 1;
}

static int bq27411_modify_soc_smooth_parameter
		(struct chip_bq27541 *chip, bool is_powerup)
{
	int rc = 0;
	bool check_result = false;
	bool tried_again = false;

	if (chip->modify_soc_smooth == false
			|| chip->device_type == DEVICE_BQ27541
			|| chip->device_type == DEVICE_ZY0602) {
		return GUAGE_ERROR;
	}
	pr_err("%s begin\n", __func__);
	if (sealed()) {
		if (!unseal(BQ27411_UNSEAL_KEY)) {
			return GUAGE_ERROR;
		} else {
			msleep(50);
		}
	}

write_parameter:
	rc = bq27411_write_soc_smooth_parameter(chip, is_powerup);
	if(rc == BATT_FULL_ERROR)
		return BATT_FULL_ERROR;//only for wite battery full param in guage dirver probe on 7250 platform
	if (rc && tried_again == false) {
		tried_again = true;
		goto write_parameter;
	} else {
		check_result = bq27411_check_soc_smooth_parameter(chip, is_powerup);
		if (check_result == false && tried_again == false) {
			tried_again = true;
			goto write_parameter;
		}
	}

	usleep_range(1000, 1000);
	if (sealed() == 0) {
		usleep_range(1000, 1000);
		seal();
	}
	pr_err("%s end\n", __func__);
	return GUAGE_OK;
}

static int bq8z610_sealed(void)
{
	int value = 0;
	u8 CNTL1_VAL[BQ28Z610_REG_CNTL1_SIZE] = {0,0,0,0};
	bq27541_i2c_txsubcmd(BQ28Z610_REG_CNTL1, BQ28Z610_SEAL_STATUS);
	usleep_range(10000, 10000);
	bq27541_read_i2c_block(BQ28Z610_REG_CNTL1, BQ28Z610_REG_CNTL1_SIZE, CNTL1_VAL);
	pr_err("%s bq8z610_sealed CNTL1_VAL[0] = %x,CNTL1_VAL[1] = %x,\
		CNTL1_VAL[2] = %x,CNTL1_VAL[3] = %x,\n",
		__func__,CNTL1_VAL[0],CNTL1_VAL[1],CNTL1_VAL[2],CNTL1_VAL[3]);
		value = (CNTL1_VAL[3] & BQ28Z610_SEAL_BIT);
	if(value == BQ28Z610_SEAL_VALUE) {
		pr_err("bq8z610 sealed, value = %x return 1\n",value);
		return 1;
	} else {
		pr_err("bq8z610 sealed, value = %x return 0\n",value);
		return 0;
	}
}

static int bq8z610_seal(void)
{
	int i = 0;

	if (bq8z610_sealed()) {
		pr_err("bq8z610 sealed, return\n");
		return 1;
	}
	bq27541_i2c_txsubcmd(0, BQ28Z610_SEAL_SUBCMD);
	//usleep_range(10000, 10000);
	msleep(1000);
	for (i = 0;i < BQ28Z610_SEAL_POLLING_RETRY_LIMIT;i++) {
		if (bq8z610_sealed()) {
			return 1;
		}
		//bq27541_i2c_txsubcmd(0, BQ28Z610_SEAL_SUBCMD);
		usleep_range(10000, 10000);
	}
	return 0;
}

static int bq8z610_unseal(void)
{
	int i = 0;

	if (!bq8z610_sealed()) {
		goto out;
	}
	bq27541_i2c_txsubcmd(0, BQ28Z610_UNSEAL_SUBCMD1);
	usleep_range(10000, 10000);
	//msleep(100);
	bq27541_i2c_txsubcmd(0, BQ28Z610_UNSEAL_SUBCMD2);
	//usleep_range(10000, 10000);
	msleep(1000);
	while (i < BQ28Z610_SEAL_POLLING_RETRY_LIMIT) {
		i++;
		if (!bq8z610_sealed()) {
			break;
		}
		usleep_range(10000, 10000);
	}

out:
	chg_debug("bq8z610 : i=%d\n", i);
	if (i == BQ28Z610_SEAL_POLLING_RETRY_LIMIT) {
		pr_err("bq8z610 unseal failed\n");
		return 0;
	} else {
		return 1;
	}
}

static int bq28z610_write_flash_busy_wait_i2c_err(struct chip_bq27541 *chip)
{
	//int rc = 0;
	u8 I2C_VAL[BQ28Z610_REG_I2C_SIZE] = {0,0,0};
	u8 I2C_write1[BQ28Z610_REG_I2C_SIZE] = {0x03,0x46,0xA0};

	bq27541_write_i2c_block(BQ28Z610_REG_CNTL1, BQ28Z610_REG_I2C_SIZE, &I2C_write1[0]);
	msleep(100);
	bq27541_i2c_txsubcmd(BQ28Z610_REG_CNTL2, 0x0516);
	msleep(100);
	bq27541_i2c_txsubcmd(BQ28Z610_REG_CNTL1, 0x4603);//physical address is 0x4603
	msleep(100);
	bq27541_read_i2c_block(BQ28Z610_REG_CNTL1, BQ28Z610_REG_I2C_SIZE, I2C_VAL);
	pr_err("%s I2C Configuration I2C_VAL[0] = %x,I2C_VAL[1] = %x,I2C_VAL[2] = %x\n",
		__func__,I2C_VAL[0],I2C_VAL[1],I2C_VAL[2]);
	if(((I2C_VAL[2] << 16) |(I2C_VAL[1] << 8) | I2C_VAL[0]) != 0xA04603) {
		pr_err("%s To change I2C Configuration 0x20 -> 0xA0. ERR.\n", __func__);
		return -1;
	} else {
		pr_err("%s To change I2C Configuration 0x20 -> 0xA0. OK\n", __func__);
	}
	return 0;
}

int bq28z610_write_soc_smooth_parameter(struct chip_bq27541 *chip)
{
	//int rc = 0;
	u8 CNTL1_VAL[BQ28Z610_REG_CNTL1_SIZE] = {0,0,0,0};
	u8 CNTL1_write1[BQ28Z610_REG_CNTL1_SIZE] = {0xF4,0x46,0xdC,0x00};
	//u8 CNTL1_write2[BQ28Z610_REG_CNTL1_SIZE] = {0x08,0x47,0x78,0x00};//120ma
	u8 CNTL1_write2[BQ28Z610_REG_CNTL1_SIZE] = {0x08,0x47,0x96,0x00};//150ma
	u8 CNTL1_write3[BQ28Z610_REG_CNTL1_SIZE] = {0x0C,0x47,0x28,0x00};
	bq27541_write_i2c_block(BQ28Z610_REG_CNTL1, BQ28Z610_REG_CNTL1_SIZE, &CNTL1_write1[0]);
	msleep(100);
	//bq8z610_cntl2_cmd(0x06E9);
	bq27541_i2c_txsubcmd(BQ28Z610_REG_CNTL2, 0x06E9);
	//usleep_range(10000, 5000);
	msleep(100);
	//bq8z610_cntl1_cmd(0x46F4);
	bq27541_i2c_txsubcmd(BQ28Z610_REG_CNTL1, 0x46F4);
	//usleep_range(5000, 5000);
	msleep(100);
	bq27541_read_i2c_block(BQ28Z610_REG_CNTL1, BQ28Z610_REG_CNTL1_SIZE, CNTL1_VAL);
		pr_err("%s Charge Term Taper Current CNTL1_VAL[0] = %x,\
			CNTL1_VAL[1] = %x,CNTL1_VAL[2] = %x,CNTL1_VAL[3] = %x,\n",
			__func__,CNTL1_VAL[0],CNTL1_VAL[1],CNTL1_VAL[2],CNTL1_VAL[3]);
		if((((CNTL1_VAL[1] << 8) | CNTL1_VAL[0]) != 0x46F4)
				|| (((CNTL1_VAL[3] << 8) | CNTL1_VAL[2]) != 0x00DC)) {
			pr_err("%s Charge Term Taper Current 150mA (=0x0096) -> 220mA (=0x00DC). ERR.\n", __func__);
			return -1;
		} else {
			pr_err("%s Charge Term Taper Current  (=0x0096) -> 220mA (=0x00DC). OK\n", __func__);
		}
	bq27541_write_i2c_block(BQ28Z610_REG_CNTL1, BQ28Z610_REG_CNTL1_SIZE, &CNTL1_write2[0]);
	msleep(100);
	//bq8z610_cntl2_cmd(0x06E9);
	//bq27541_i2c_txsubcmd(BQ28Z610_REG_CNTL2, 0x0638);//120ma
	bq27541_i2c_txsubcmd(BQ28Z610_REG_CNTL2, 0x061A);//150ma
	//usleep_range(5000, 5000);
	msleep(100);
	//bq8z610_cntl1_cmd(0x46F4);
	bq27541_i2c_txsubcmd(BQ28Z610_REG_CNTL1, 0x4708);
	//usleep_range(5000, 5000);
	msleep(100);
	bq27541_read_i2c_block(BQ28Z610_REG_CNTL1, BQ28Z610_REG_CNTL1_SIZE, CNTL1_VAL);
	pr_err("%s Dsg Current Threshold CNTL1_VAL[0] = %x,\
		CNTL1_VAL[1] = %x,CNTL1_VAL[2] = %x,CNTL1_VAL[3] = %x,\n",
		__func__,CNTL1_VAL[0],CNTL1_VAL[1],CNTL1_VAL[2],CNTL1_VAL[3]);
		//if((((CNTL1_VAL[1] << 8) | CNTL1_VAL[0]) != 0x4708) || (((CNTL1_VAL[3] << 8) | CNTL1_VAL[2]) != 0x0078))//120ma
	if((((CNTL1_VAL[1] << 8) | CNTL1_VAL[0]) != 0x4708)
			|| (((CNTL1_VAL[3] << 8) | CNTL1_VAL[2]) != 0x0096)) {
		pr_err("%s Dsg Current Threshold 40mA (0x0028) -> 150mA (0x0078) ERR.\n", __func__);
		return -1;
	} else {
		pr_err("%s Dsg Current Threshold 40mA (0x0028) -> 150mA (0x0078) OK\n", __func__);
	}
	bq27541_write_i2c_block(BQ28Z610_REG_CNTL1, BQ28Z610_REG_CNTL1_SIZE, &CNTL1_write3[0]);
	msleep(100);
	bq27541_i2c_txsubcmd(BQ28Z610_REG_CNTL2, 0x0684);
	msleep(100);
	bq27541_i2c_txsubcmd(BQ28Z610_REG_CNTL1, 0x470C);
	msleep(100);
	bq27541_read_i2c_block(BQ28Z610_REG_CNTL1, BQ28Z610_REG_CNTL1_SIZE, CNTL1_VAL);
	pr_err("%s Quit Current CNTL1_VAL[0] = %x,\
		CNTL1_VAL[1] = %x,CNTL1_VAL[2] = %x,CNTL1_VAL[3] = %x,\n",
		__func__,CNTL1_VAL[0],CNTL1_VAL[1],CNTL1_VAL[2],CNTL1_VAL[3]);
	if((((CNTL1_VAL[1] << 8) | CNTL1_VAL[0]) != 0x470C)
			|| (((CNTL1_VAL[3] << 8) | CNTL1_VAL[2]) != 0x0028)) {
		pr_err("%s Quit Current 20mA (0x0014) -> 40mA (0x0028). ERR.\n", __func__);
		return -1;
	} else {
		pr_err("%s Quit Current 20mA (0x0014) -> 40mA (0x0028). OK\n", __func__);
	}
	return 0;
}

static int bq28z610_write_iterm_Taper_parameter(struct chip_bq27541 *chip) {
	u8 CNTL1_VAL[BQ28Z610_REG_CNTL1_SIZE] = {0,0,0,0};
	u8 CNTL1_write1[BQ28Z610_REG_CNTL1_SIZE] = {0xF4,0x46,0x96,0x00};
	bq27541_write_i2c_block(BQ28Z610_REG_CNTL1, BQ28Z610_REG_CNTL1_SIZE, &CNTL1_write1[0]);
	msleep(100);
	bq27541_i2c_txsubcmd(BQ28Z610_REG_CNTL2, 0x062F);
	msleep(100);
	bq27541_i2c_txsubcmd(BQ28Z610_REG_CNTL1, 0x46F4);
	msleep(100);
	bq27541_read_i2c_block(BQ28Z610_REG_CNTL1, BQ28Z610_REG_CNTL1_SIZE, CNTL1_VAL);
	pr_err("%s Charge Term Taper Current CNTL1_VAL[0] = %x,\
		CNTL1_VAL[1] = %x,CNTL1_VAL[2] = %x,CNTL1_VAL[3] = %x,\n",
		__func__,CNTL1_VAL[0],CNTL1_VAL[1],CNTL1_VAL[2],CNTL1_VAL[3]);
	if((((CNTL1_VAL[1] << 8) | CNTL1_VAL[0]) != 0x46F4)
			|| (((CNTL1_VAL[3] << 8) | CNTL1_VAL[2]) != 0x0096)) {
		pr_err("%s Charge Term Taper Current 220mA (=0x00DC) -> 150mA (=0x0096). ERR.\n", __func__);
		return -1;
	} else {
		pr_err("%s Charge Term Taper Current 220mA (=0x00DC) -> 150mA (=0x0096). OK\n", __func__);
	}
	return 0;
}

static void bq28z610_modify_soc_smooth_parameter(struct chip_bq27541 *chip)
{
	int rc = 0;
	bool tried_again = false;

	pr_err("%s begin\n", __func__);
	if (bq8z610_sealed()) {
		if (!bq8z610_unseal()) {
			return;
		} else {
			msleep(50);
		}
	}

write_parameter:
	//rc = bq28z610_write_soc_smooth_parameter(chip);
	rc = bq28z610_write_iterm_Taper_parameter(chip);
	rc = bq28z610_write_flash_busy_wait_i2c_err(chip);
	if (rc && tried_again == false) {
		tried_again = true;
		goto write_parameter;
	}
	usleep_range(1000, 1000);
	if (bq8z610_sealed() == 0) {
		usleep_range(1000, 1000);
		bq8z610_seal();
	}
	pr_err("%s end\n", __func__);
}

static int bq28z610_batt_full_zero_parameter_write_cmd(struct chip_bq27541 *chip)
{
	u8 rc = 0;
	u8 CNTL1_VAL_1[2] = {0x00, 0x00};
	u8 CNTL1_VAL_2[7] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
	u8 read_buf[7] = {0};
	int retry_cnt = 0;

	pr_err("%s begin\n", __func__);
	/*############################################0x0C47#####################################################*/
	/*write 60ma*/
	pr_err("%s write 40ma --> 60ma 003C\n", __func__);

	CNTL1_VAL_1[0] = 0x0A;
	CNTL1_VAL_1[1] = 0x47;
	bq27541_write_i2c_block(0x3E, 2, CNTL1_VAL_1);													/*W*/
	pr_err("%s 0x3E -->write [0x%02x][0x%02x]\n", __func__, CNTL1_VAL_1[0], CNTL1_VAL_1[1]);
	rc = bq27541_read_i2c_block(0x3E, 6, read_buf);													/*R*/
	pr_err("%s 0x3E -->read [0x%02x][0x%02x] [0x%02x][0x%02x] [0x%02x][0x%02x]\n",
			__func__, read_buf[0], read_buf[1], read_buf[2], read_buf[3], read_buf[4], read_buf[5]);
	if (!(read_buf[2] == 0x46 && read_buf[3] == 0x00 && read_buf[4] == 0x3C && read_buf[5] == 0x00)) {
		/*################################################0x9A45################################################*/
		/*write 10s*/
		pr_err("%s write 0x0A45\n", __func__);
recfg_pararm1:
		CNTL1_VAL_1[0] = 0x9A;
		CNTL1_VAL_1[1] = 0x45;
		bq27541_write_i2c_block(0x3E, 2, CNTL1_VAL_1);													/*W*/
		pr_err("%s 0x3E -->write [0x%02x][0x%02x]\n", __func__, CNTL1_VAL_1[0], CNTL1_VAL_1[1]);
		rc = bq27541_read_i2c_block(0x3E, 7, read_buf);													/*R*/
		pr_err("%s 0x3E -->read [0x%02x][0x%02x] [0x%02x][0x%02x][0x%02x][0x%02x][0x%02x]\n",
				__func__, read_buf[0], read_buf[1], read_buf[2], read_buf[3], read_buf[4], read_buf[5], read_buf[6]);

		CNTL1_VAL_2[0] = 0x9A;
		CNTL1_VAL_2[1] = 0x45;
		CNTL1_VAL_2[2] = 0x20;
		CNTL1_VAL_2[3] = 0x1C;
		CNTL1_VAL_2[4] = 0x20;
		CNTL1_VAL_2[5] = 0x1C;
		CNTL1_VAL_2[6] = 0x1E;
		bq27541_write_i2c_block(0x3E, 7, CNTL1_VAL_2);													/*W*/
		pr_err("%s 0x3E -->write [0x%02x][0x%02x] [0x%02x][0x%02x][0x%02x][0x%02x][0x%02x]\n",
				__func__, CNTL1_VAL_2[0], CNTL1_VAL_2[1], CNTL1_VAL_2[2], CNTL1_VAL_2[3], CNTL1_VAL_2[4], CNTL1_VAL_2[5], CNTL1_VAL_2[6]);

		CNTL1_VAL_1[0] = 0xFF - CNTL1_VAL_2[0] - CNTL1_VAL_2[1] - CNTL1_VAL_2[2] - CNTL1_VAL_2[3] - CNTL1_VAL_2[4] - CNTL1_VAL_2[5] - CNTL1_VAL_2[6];
		CNTL1_VAL_1[1] = 0x09;
		bq27541_write_i2c_block(0x60, 2, CNTL1_VAL_1);													/*W*/
		pr_err("%s 0x60 -->write [0x%02x][0x%02x]\n", __func__, CNTL1_VAL_1[0], CNTL1_VAL_1[1]);
		usleep_range(260000,260000);																		/*15ms*/

		CNTL1_VAL_1[0] = 0x9A;/*READ 10S*/
		CNTL1_VAL_1[1] = 0x45;
		bq27541_write_i2c_block(0x3E, 2, CNTL1_VAL_1);													/*W*/
		pr_err("%s 0x3E -->write [0x%02x][0x%02x]\n", __func__, CNTL1_VAL_1[0], CNTL1_VAL_1[1]);
		rc = bq27541_read_i2c_block(0x3E, 7, read_buf);													/*R*/
		pr_err("%s 0x3E -->read [0x%02x][0x%02x] [0x%02x][0x%02x][0x%02x][0x%02x][0x%02x]\n",
				__func__, read_buf[0], read_buf[1], read_buf[2], read_buf[3], read_buf[4], read_buf[5], read_buf[6]);
		/*check param cfg*/
		if (!(read_buf[2] == CNTL1_VAL_2[2] && read_buf[3] == CNTL1_VAL_2[3] && read_buf[4] == CNTL1_VAL_2[4] && read_buf[5] == CNTL1_VAL_2[5] && read_buf[6] == CNTL1_VAL_2[6])) {
			retry_cnt ++;
			if (retry_cnt >= 3) {
				goto param_cf_err;
			}
			pr_err("gauge err recfg_pararm1");
			goto recfg_pararm1;
		}
		retry_cnt = 0;
		usleep_range(5000, 5000);
		/*##############################################0x5846#################################################*/
		/*write 5846*/
		pr_err("%s write 0x5846\n", __func__);
recfg_pararm2:
		CNTL1_VAL_1[0] = 0x58;
		CNTL1_VAL_1[1] = 0x46;
		bq27541_write_i2c_block(0x3E, 2, CNTL1_VAL_1);													/*W*/
		pr_err("%s 0x3E -->write [0x%02x][0x%02x]\n", __func__, CNTL1_VAL_1[0], CNTL1_VAL_1[1]);
		rc = bq27541_read_i2c_block(0x3E, 6, read_buf);													/*R*/
		pr_err("%s 0x3E -->read [0x%02x][0x%02x] [0x%02x][0x%02x] [0x%02x][0x%02x]\n",
				__func__, read_buf[0], read_buf[1], read_buf[2], read_buf[3], read_buf[4], read_buf[5]);

		CNTL1_VAL_2[0] = 0x58;
		CNTL1_VAL_2[1] = 0x46;
		CNTL1_VAL_2[2] = 0x33;
		CNTL1_VAL_2[3] = 0x00;
		CNTL1_VAL_2[4] = 0x27;
		CNTL1_VAL_2[5] = 0x00;
		bq27541_write_i2c_block(0x3E, 6, CNTL1_VAL_2);													/*W*/
		pr_err("%s 0x3E -->write [0x%02x][0x%02x] [0x%02x][0x%02x] [0x%02x][0x%02x]\n",
				__func__, CNTL1_VAL_2[0], CNTL1_VAL_2[1], CNTL1_VAL_2[2], CNTL1_VAL_2[3], CNTL1_VAL_2[4], CNTL1_VAL_2[5]);

		CNTL1_VAL_1[0] = 0xFF - CNTL1_VAL_2[0] - CNTL1_VAL_2[1] - CNTL1_VAL_2[2]  - CNTL1_VAL_2[3] - CNTL1_VAL_2[4]  - CNTL1_VAL_2[5];
		/*CNTL1_VAL_1[0] = 0xE2;*/
		CNTL1_VAL_1[1] = 0x08;
		bq27541_write_i2c_block(0x60, 2, CNTL1_VAL_1);													/*W*/
		pr_err("%s 0x60 -->write [0x%02x][0x%02x]\n", __func__, CNTL1_VAL_1[0], CNTL1_VAL_1[1]);
		usleep_range(260000, 260000);																		/*15ms*/

		CNTL1_VAL_1[0] = 0x58;/*READ 7200S 1C20*/
		CNTL1_VAL_1[1] = 0x46;
		bq27541_write_i2c_block(0x3E, 2, CNTL1_VAL_1);													/*W*/
		pr_err("%s 0x3E -->write [0x%02x][0x%02x]\n", __func__, CNTL1_VAL_1[0], CNTL1_VAL_1[1]);
		rc = bq27541_read_i2c_block(0x3E, 6, read_buf);													/*R*/
		pr_err("%s 0x3E -->read [0x%02x][0x%02x] [0x%02x][0x%02x]\n",
				__func__, read_buf[0], read_buf[1], read_buf[2], read_buf[3], read_buf[4], read_buf[5]);
		/*check param cfg*/
		if (!(read_buf[2] == CNTL1_VAL_2[2] && read_buf[3] == CNTL1_VAL_2[3] && read_buf[4] == CNTL1_VAL_2[4] && read_buf[5] == CNTL1_VAL_2[5])) {
			retry_cnt ++;
			if (retry_cnt >= 3) {
				goto param_cf_err;
			}
			pr_err("gauge err recfg_pararm2");
			goto recfg_pararm2;
		}
		retry_cnt = 0;
		usleep_range(5000, 5000);
		/*############################################0x0A47###################################################*/
		/*write 0A47*/
		pr_err("%s write 0x0A47\n", __func__);
recfg_pararm3:
		CNTL1_VAL_1[0] = 0x0A;
		CNTL1_VAL_1[1] = 0x47;
		bq27541_write_i2c_block(0x3E, 2, CNTL1_VAL_1);													/*W*/
		pr_err("%s 0x3E -->write [0x%02x][0x%02x]\n", __func__, CNTL1_VAL_1[0], CNTL1_VAL_1[1]);
		rc = bq27541_read_i2c_block(0x3E, 6, read_buf);													/*R*/
		pr_err("%s 0x3E -->read [0x%02x][0x%02x] [0x%02x][0x%02x] [0x%02x][0x%02x]\n",
				__func__, read_buf[0], read_buf[1], read_buf[2], read_buf[3], read_buf[4], read_buf[5]);

		CNTL1_VAL_2[0] = 0x0A;
		CNTL1_VAL_2[1] = 0x47;
		CNTL1_VAL_2[2] = 0x46;
		CNTL1_VAL_2[3] = 0x00;
		CNTL1_VAL_2[4] = 0x3C;
		CNTL1_VAL_2[5] = 0x00;
		bq27541_write_i2c_block(0x3E, 6, CNTL1_VAL_2);													/*W*/
		pr_err("%s 0x3E -->write [0x%02x][0x%02x] [0x%02x][0x%02x] [0x%02x][0x%02x]\n",
				__func__, CNTL1_VAL_2[0], CNTL1_VAL_2[1], CNTL1_VAL_2[2], CNTL1_VAL_2[3], CNTL1_VAL_2[4], CNTL1_VAL_2[5]);

		CNTL1_VAL_1[0] = 0xFF - CNTL1_VAL_2[0] - CNTL1_VAL_2[1] - CNTL1_VAL_2[2]  - CNTL1_VAL_2[3] - CNTL1_VAL_2[4]  - CNTL1_VAL_2[5];
		/*CNTL1_VAL_1[0] = 0xE2;*/
		CNTL1_VAL_1[1] = 0x08;
		bq27541_write_i2c_block(0x60, 2, CNTL1_VAL_1);													/*W*/
		pr_err("%s 0x60 -->write [0x%02x][0x%02x]\n", __func__, CNTL1_VAL_1[0], CNTL1_VAL_1[1]);
		usleep_range(260000, 260000);																		/*15ms*/

		CNTL1_VAL_1[0] = 0x0A;/*READ 7200S 1C20*/
		CNTL1_VAL_1[1] = 0x47;
		bq27541_write_i2c_block(0x3E, 2, CNTL1_VAL_1);													/*W*/
		pr_err("%s 0x3E -->write [0x%02x][0x%02x]\n", __func__, CNTL1_VAL_1[0], CNTL1_VAL_1[1]);
		rc = bq27541_read_i2c_block(0x3E, 6, read_buf);													/*R*/
		pr_err("%s 0x3E -->read [0x%02x][0x%02x] [0x%02x][0x%02x]\n",
				__func__, read_buf[0], read_buf[1], read_buf[2], read_buf[3], read_buf[4], read_buf[5]);
		if (!(read_buf[2] == CNTL1_VAL_2[2] && read_buf[3] == CNTL1_VAL_2[3] && read_buf[4] == CNTL1_VAL_2[4] && read_buf[5] == CNTL1_VAL_2[5])) {
			retry_cnt ++;
			if (retry_cnt >= 3) {
				goto param_cf_err;
			}
			pr_err("gauge err recfg_pararm3");
			goto recfg_pararm3;
		}
		usleep_range(5000, 5000);
		pr_err("gauge param cfg susccess\n");
	}
	pr_err("%s end\n", __func__);
	return GUAGE_OK;
param_cf_err:
	pr_err("%s param_cf_err err\n", __func__);
	return GUAGE_ERROR;
}


int bq28z610_batt_full_zero_parameter(void)
{
	int rc = 0;
	bool tried_again = false;
	struct chip_bq27541 *chip;

	chip = gauge_ic;

	if (chip->device_type != DEVICE_BQ27541) {
		pr_err("%s NOT DEVICE_BQ27541\n", __func__);
		return GUAGE_OK;
	}

	if (!chip->batt_bq28z610)
		return GUAGE_OK;

	pr_err("%s begin\n", __func__);
	if (bq8z610_sealed()) {
		if (!bq8z610_unseal()) {
			return GUAGE_OK;
		} else {
			msleep(50);
		}
	}

write_parameter:
	/*rc = bq28z610_write_soc_smooth_parameter(chip);
	rc = bq28z610_write_iterm_Taper_parameter(chip);
	rc = bq28z610_write_flash_busy_wait_i2c_err(chip);
	usleep_range(5000, 5000);
	*/
	rc = bq28z610_batt_full_zero_parameter_write_cmd(chip);
	if (rc && tried_again == false) {
		tried_again = true;
		goto write_parameter;
	}

	bq27541_i2c_txsubcmd(0, BQ28Z610_SEAL_SUBCMD);	/*seal*/
	msleep(1000);
	if (bq8z610_sealed() == 0) {
		usleep_range(1000, 1000);
		bq8z610_seal();
	}
	pr_err("%s end\n", __func__);
	return GUAGE_OK;
}

#define REG_DUMP_SIZE  1024
static int dump_reg[] = {0x08, 0x12, 0x2c};
static int gauge_reg_dump(void) {
	int val = 0;
	int i = 0;
	int l = 0;
	char *pos;
	int sum = 0, ret;
	u8 iv[32]={0};
	char buf[REG_DUMP_SIZE] = {0};
	int len = REG_DUMP_SIZE;

	if (!gauge_ic) {
		return 0;
	}
	if (atomic_read(&gauge_ic->suspended) == 1) {
		pr_err("%s: gauge suspend!\n", __func__);
		return -1;
	}

	pos = buf;
	if (oplus_warp_get_allow_reading() == true) {
		l = sprintf(pos, "%d ", bq27541_get_battery_temperature());
		pos += l;
		sum += l;
		l = sprintf(pos, "/ %d ", bq27541_get_average_current());
		pos += l;
		sum += l;

		for (; i < sizeof(dump_reg) / sizeof(int) && sum < len - 16; i++) {
			ret = bq27541_read_i2c(dump_reg[i], &val);
			if (ret) {
				dev_err(gauge_ic->dev, "[OPLUS_CHG]:error reading regdump, ret:%d\n", ret);
				return -1;
			}
			l = sprintf(pos, "/ %d ", val);
			pos += l;
			sum += l;
		}

		bq27541_i2c_txsubcmd(BQ27411_DATA_CLASS_ACCESS, 0x71);
		usleep_range(10000, 10000);
		bq27541_read_i2c_block(BQ28Z610_REG_CNTL1, 6, iv);
		for (i = 2; i < 6 && sum < len - 16; i++) {
			if ((i % 2) == 0) {
				l = sprintf(pos, "/ %d ", (iv[i + 1] << 8) + iv[i]);
				pos += l;
				sum += l;
			}
		}
		bq27541_i2c_txsubcmd(BQ27411_DATA_CLASS_ACCESS, 0x73);
		usleep_range(10000, 10000);
		bq27541_read_i2c_block(BQ28Z610_REG_CNTL1, 16, iv);
		for (i = 2; i < 16 && sum < len - 16; i++) {
			if (i != 3 && i != 4 && i != 7 && i != 8 && i != 11 && i != 12) {
				if ((i % 2) == 0) {
					l = sprintf(pos, "/ %d ", (iv[i + 1] << 8) + iv[i]);
					pos += l;
					sum += l;
				}
			}
		}
		bq27541_i2c_txsubcmd(BQ27411_DATA_CLASS_ACCESS, 0x74);
		usleep_range(10000, 10000);
		bq27541_read_i2c_block(BQ28Z610_REG_CNTL1, 26, iv);
		for (i = 12; i < 26 && sum < len - 16; i++) {
			if (i != 17 && i != 18) {
				if ((i % 2) == 0) {
					l = sprintf(pos, "/ %d ", (iv[i + 1] << 8) + iv[i]);
					pos += l;
					sum += l;
				}
			}
		}
		bq27541_i2c_txsubcmd(BQ27411_DATA_CLASS_ACCESS, 0x75);
		usleep_range(10000, 10000);
		bq27541_read_i2c_block(BQ28Z610_REG_CNTL1, 12, iv);
		for (i = 2; i < 12 && sum < len - 16; i++) {
			if(i != 3 && i != 5 && i != 6 && i != 8){
				if ((i % 2) == 0) {
					l = sprintf(pos, "/ %d ", (iv[i + 1] << 8) + iv[i]);
					pos += l;
					sum += l;
				}
			}
		}
	}
	printk(KERN_ERR "[OPLUS_CHG]gauge type: %d, regs: %s\n", gauge_ic->device_type,  buf);
	return 0;
}

static int bq8z610_check_gauge_enable(void)
{
	/*    return control_cmd_read(di, CONTROL_STATUS) & (1 << 13);*/
	int value = 0;
	u8 CNTL1_VAL[BQ28Z610_REG_CNTL1_SIZE] = {0,0,0,0};
	bq27541_i2c_txsubcmd(BQ28Z610_REG_CNTL1, BQ28Z610_REG_GAUGE_EN);
	//usleep_range(10000, 10000);
	msleep(1000);
	bq27541_read_i2c_block(BQ28Z610_REG_CNTL1, BQ28Z610_REG_CNTL1_SIZE, CNTL1_VAL);
	pr_err("%s  CNTL1_VAL[0] = %x,CNTL1_VAL[1] = %x,\
		CNTL1_VAL[2] = %x,CNTL1_VAL[3] = %x,\n",
		__func__,CNTL1_VAL[0],CNTL1_VAL[1],CNTL1_VAL[2],CNTL1_VAL[3]);
	value = (CNTL1_VAL[2] & BQ28Z610_GAUGE_EN_BIT);
	if(value == BQ28Z610_GAUGE_EN_BIT) {
		pr_err("bq8z610 gauge_enable, value = %x return 1\n",value);
		return 1;
	} else {
		pr_err("bq8z610 gauge_enable, value = %x return 0\n",value);
		return 0;
	}
}

static int bq28z610_write_dod0_parameter(struct chip_bq27541 *chip)
{
	//bq8z610_cntl1_cmd(0x46F4);
	bq27541_i2c_txsubcmd(BQ28Z610_REG_CNTL1, 0x0021);
	//usleep_range(5000, 5000);
	msleep(1000);
	//bq8z610_cntl1_cmd(0x00DC);
	bq27541_i2c_txsubcmd(BQ28Z610_REG_CNTL1, 0x0021);
	//usleep_range(5000, 5000);
	msleep(2000);
	if(bq8z610_check_gauge_enable() == false) {
		//bq8z610_cntl1_cmd(0x00DC);
		bq27541_i2c_txsubcmd(BQ28Z610_REG_CNTL1, 0x0021);
		//usleep_range(5000, 5000);
		msleep(300);
	}
	return 0;
}

static void bq28z610_modify_dod0_parameter(struct chip_bq27541 *chip)
{
	int rc = 0;

	pr_err("%s begin\n", __func__);
#ifdef OPLUS_CHG_OP_DEF
	if (!gauge_ic->bq_present)
		return;
#endif
	if (bq8z610_sealed()) {
		if (!bq8z610_unseal()) {
			return;
		} else {
			msleep(50);
		}
	}
	rc = bq28z610_write_dod0_parameter(chip);
	usleep_range(1000, 1000);
	if (bq8z610_sealed() == 0) {
		usleep_range(1000, 1000);
		bq8z610_seal();
	}
	pr_err("%s end\n", __func__);
}

static int bq28z610_get_2cell_voltage(void)
{
	u8 cell_vol[BQ28Z610_MAC_CELL_VOLTAGE_SIZE] = {0, 0, 0, 0};

	if (!gauge_ic) {
		return 0;
	}

	mutex_lock(&bq28z610_alt_manufacturer_access);
	bq27541_i2c_txsubcmd(BQ28Z610_MAC_CELL_VOLTAGE_EN_ADDR,
		BQ28Z610_MAC_CELL_VOLTAGE_CMD);
	usleep_range(1000, 1000);
	bq27541_read_i2c_block(BQ28Z610_MAC_CELL_VOLTAGE_ADDR,
		BQ28Z610_MAC_CELL_VOLTAGE_SIZE, cell_vol);
	mutex_unlock(&bq28z610_alt_manufacturer_access);
	gauge_ic->batt_cell_1_vol = (cell_vol[1] << 8) | cell_vol[0];
	gauge_ic->batt_cell_2_vol = (cell_vol[3] << 8) | cell_vol[2];
	if (gauge_ic->batt_cell_1_vol < gauge_ic->batt_cell_2_vol) {
		gauge_ic->batt_cell_max_vol = gauge_ic->batt_cell_2_vol;
		gauge_ic->batt_cell_min_vol = gauge_ic->batt_cell_1_vol;
	} else {
		gauge_ic->batt_cell_max_vol = gauge_ic->batt_cell_1_vol;
		gauge_ic->batt_cell_min_vol = gauge_ic->batt_cell_2_vol;
	}
	/*chg_err("batt_cell_1_vol = %dmV, batt_cell_2_vol = %dmV, batt_cell_max_vol = %dmV\n",
		gauge_ic->batt_cell_1_vol,
		gauge_ic->batt_cell_2_vol,
		gauge_ic->batt_cell_max_vol);*/

	return 0;
}

/*static int bq28z610_get_2cell_balance_time(void)
{
	u8 balance_time[BQ28Z610_MAC_CELL_BALANCE_TIME_SIZE] = {0,0,0,0};

	bq27541_i2c_txsubcmd(BQ28Z610_MAC_CELL_BALANCE_TIME_EN_ADDR, BQ28Z610_MAC_CELL_BALANCE_TIME_CMD);
	usleep_range(1000, 1000);
	bq27541_read_i2c_block(BQ28Z610_MAC_CELL_BALANCE_TIME_ADDR, BQ28Z610_MAC_CELL_BALANCE_TIME_SIZE, balance_time);
	pr_err("Cell_balance_time_remaining1 = %dS, Cell_balance_time_remaining2 = %dS\n", 
	(balance_time[1] << 8) | balance_time[0], (balance_time[3] << 8) | balance_time[2]);

	return 0;
}*/

#ifdef CONFIG_OPLUS_CHARGER_MTK
#define AUTH_MESSAGE_LEN		20
#define AUTH_TAG "ogauge_auth="
static int get_auth_msg(u8 *source, u8 *rst) {
	char *str = NULL;
	int i;
	str = strstr(boot_command_line, AUTH_TAG);
	if (str == NULL) {
		pr_err("Asynchronous authentication is not supported!!!\n");
		return -1;
	}
	pr_info("%s\n", str);
	str += strlen(AUTH_TAG);
	for (i = 0; i < AUTH_MESSAGE_LEN; i++) {
		source[i] = (str[2 * i ] - 64) | ((str[2 * i + 1] - 64) << 4);
		pr_info("source index %d = %x\n", i, source[i]);
	}
	str += AUTH_MESSAGE_LEN * 2;
	for (i = 0; i < AUTH_MESSAGE_LEN; i++) {
		rst[i] = (str[2 * i] - 64) | ((str[2 * i + 1] - 64) << 4);
		pr_info("expected index %d = %x\n", i, rst[i]);
	}
	return 0;
}
#endif


#define BLOCKDATACTRL	0X61
#define DATAFLASHBLOCK	0X3F
#define AUTHENDATA		0X40
#define AUTHENCHECKSUM	0X54

static int bq27541_sha1_hmac_authenticate(struct bq27541_authenticate_data *authenticate_data)
{
	int i;
	int ret;
	unsigned char t;
	int len;
	u8 checksum_buf[1] ={0x0};
	u8 authen_cmd_buf[1] = {0x00};
	u8 recv_buf[AUTHEN_MESSAGE_MAX_COUNT]={0x0};

	if (authenticate_data == NULL) {
		pr_err("%s authenticate_data NULL\n", __func__);
		return -1;
	}

	// step 0: produce 20 bytes random data and checksum
	for(i = 0;i < authenticate_data->message_len;i++) {
		checksum_buf[0] = checksum_buf[0] + authenticate_data->message[i];
	}
	checksum_buf[0] = 0xff - (checksum_buf[0]&0xff);

#ifdef XBL_AUTH_DEBUG
	for (i = 0; i < GAUGE_AUTH_MSG_LEN - 3; i = i + 4) {
		pr_info("%s: sending msg[%d]=%x, sending msg[%d]=%x,sending msg[%d]=%x, sending msg[%d]=%x\n", __func__,
			i,authenticate_data->message[i],i+1,authenticate_data->message[i+1],i+2,authenticate_data->message[i+2],i+3,authenticate_data->message[i+3]);
	}
#endif
	// step 1: seal mode->write 0x00 to dataflashblock
	ret = bq27541_i2c_txsubcmd_onebyte(DATAFLASHBLOCK, authen_cmd_buf[0]);

	if(ret < 0) {
		chg_err("%s i2c write error\n",__func__);
		return -1;
	}
	// step 2: write 20 bytes to authendata_reg

	bq27541_write_i2c_block(AUTHENDATA, authenticate_data->message_len, authenticate_data->message);
	msleep(1);
	// step 3: write checksum to authenchecksum_reg for compute
	bq27541_i2c_txsubcmd_onebyte(AUTHENCHECKSUM, checksum_buf[0]);
	msleep(3);
	// step 4: read authendata
	bq27541_read_i2c_block(AUTHENDATA, authenticate_data->message_len, &recv_buf[0]);

	len = authenticate_data->message_len;
	for(i = 0; i < len / 2; i++) {
		t = recv_buf[i];
		recv_buf[i] = recv_buf[len - i - 1];
		recv_buf[len - i - 1] = t;
	}
#ifdef XBL_AUTH_DEBUG
	for (i = 0; i < GAUGE_AUTH_MSG_LEN - 3; i = i + 4) {
		pr_info("%s: hw[%d]=%x,hw[%d]=%x,hw[%d]=%x,hw[%d]=%x\n", __func__,
			i,recv_buf[i],i+1,recv_buf[i+1],i+2,recv_buf[i+2],i+3,recv_buf[i+3]);
	}
#endif
	memcpy(authenticate_data->message, &recv_buf[0], authenticate_data->message_len);
#ifdef XBL_AUTH_DEBUG
	for (i = 0; i < authenticate_data->message_len; i++) {
		pr_err("HW_sha1_hmac_result[0x%x]", authenticate_data->message[i]);
	}
#endif
	return 0;
}

static bool get_smem_batt_info(oplus_gauge_auth_result *auth, int kk) {
#ifdef CONFIG_OPLUS_CHARGER_MTK
	int ret = 0;

	ret = get_auth_msg(auth->msg, auth->rcv_msg);
	if (ret == 0) {
		return true;
	} else {
		return false;
	}
#else
	size_t smem_size;
	void *smem_addr;
	oplus_gauge_auth_info_type *smem_data;
	int i;
	if (NULL == auth) {
		pr_err("%s: invalid parameters\n", __func__);
		return false;
	}

	memset(auth, 0, sizeof(oplus_gauge_auth_result));
	smem_addr = qcom_smem_get(QCOM_SMEM_HOST_ANY, SMEM_RESERVED_BOOT_INFO_FOR_APPS, &smem_size);
	if (IS_ERR(smem_addr)) {
		pr_err("unable to acquire smem SMEM_RESERVED_BOOT_INFO_FOR_APPS entry\n");
		return false;
	} else {
		smem_data = (oplus_gauge_auth_info_type *)smem_addr;
		if (smem_data == ERR_PTR(-EPROBE_DEFER)) {
			smem_data = NULL;
			pr_info("fail to get smem_data\n");
			return false;
		} else {
			if (0 == kk) {
				memcpy(auth, &smem_data->rst_k0, sizeof(oplus_gauge_auth_result));
			} else {
				memcpy(auth, &smem_data->rst_k1, sizeof(oplus_gauge_auth_result));
				pr_info("%s: auth result=%d\n", __func__, auth->result);

#ifdef XBL_AUTH_DEBUG
				for (i = 0; i < GAUGE_AUTH_MSG_LEN - 3; i = i + 4) {
					pr_info("%s: msg[%d]=%x,msg[%d]=%x,msg[%d]=%x,msg[%d]=%x\n", __func__,
						i,auth->msg[i],i+1,auth->msg[i+1],i+2,auth->msg[i+2],i+3,auth->msg[i+3]);
				}
				for (i = 0; i < GAUGE_AUTH_MSG_LEN - 3; i = i + 4) {
					pr_info("%s: rcv_msg[%d]=%x,rcv_msg[%d]=%x,rcv_msg[%d]=%x,rcv_msg[%d]=%x\n", __func__,
						i,auth->rcv_msg[i],i+1,auth->rcv_msg[i+1],i+2,auth->rcv_msg[i+2],i+3,auth->rcv_msg[i+3]);
				}
#endif
			}
		}
	}
	return true;
#endif
}

static bool init_gauge_auth(oplus_gauge_auth_result *rst, struct bq27541_authenticate_data *authenticate_data) {

	int len = GAUGE_AUTH_MSG_LEN < AUTHEN_MESSAGE_MAX_COUNT	? GAUGE_AUTH_MSG_LEN : AUTHEN_MESSAGE_MAX_COUNT; 
	if (NULL == rst || NULL == authenticate_data) {
		pr_err("Gauge authenticate failed\n");
		return false; 
	}
	/*//comment for keeping authenticattion in kernel
	if (rst->result) {
		pr_err("Gauge authenticate succeed from xbl\n");
		return true;
	}
	*/
	memset(authenticate_data, 0, sizeof(struct bq27541_authenticate_data));
	authenticate_data->message_len = len;
	memcpy(authenticate_data->message, rst->msg, len);
	bq27541_sha1_hmac_authenticate(authenticate_data);

	if (strncmp(authenticate_data->message, rst->rcv_msg, len)) {
		pr_err("Gauge authenticate compare failed\n");
		return false;
	} else {
		pr_err("Gauge authenticate succeed\n");
		authenticate_data->result = 1;
		rst->result = 1;
	}
	return true;
}

static void register_gauge_devinfo(struct chip_bq27541 *chip)
{
#ifndef CONFIG_OPLUS_CHG_OOS
	int ret = 0;
	char *version;
	char *manufacture;

	switch (chip->device_type) {
		case DEVICE_BQ27541:
			version = "bq27541";
			manufacture = "TI";
			break;
		case DEVICE_BQ27411:
			version = "bq27411";
			manufacture = "TI";
			break;
		case DEVICE_ZY0602:
			version = "zy0602";
			manufacture = "ZY";
			break;
		default:
			version = "unknown";
			manufacture = "UNKNOWN";
			break;
		}
	ret = register_device_proc("gauge", version, manufacture);
	if (ret) {
		pr_err("register_gauge_devinfo fail\n");
	}
#endif
}

static void bq27541_reset(struct i2c_client *client)
{
	int ui_soc = oplus_chg_get_ui_soc();

	if (bq27541_gauge_ops.get_battery_mvolts() <= 3300
			&& bq27541_gauge_ops.get_battery_mvolts() > 2500
			&& ui_soc == 0
			&& bq27541_gauge_ops.get_battery_temperature() > 150) {
		if (!unseal(BQ27541_UNSEAL_KEY)) {
			pr_err("bq27541 unseal fail !\n");
			return;
		}
		chg_debug("bq27541 unseal OK vol = %d, ui_soc = %d, temp = %d!\n",
			bq27541_gauge_ops.get_battery_mvolts(),
		ui_soc, bq27541_gauge_ops.get_battery_temperature());
		if (gauge_ic->device_type == DEVICE_BQ27541
				|| gauge_ic->device_type == DEVICE_ZY0602) {
			bq27541_cntl_cmd(BQ27541_RESET_SUBCMD);
		} else if (gauge_ic->device_type == DEVICE_BQ27411) {
			bq27541_cntl_cmd(BQ27411_RESET_SUBCMD);  /*  27411  */
		}
		msleep(50);
		if (gauge_ic->device_type == DEVICE_BQ27411) {
			if (!seal()) {
				pr_err("bq27411 seal fail\n");
			}
		}
		msleep(150);
		chg_debug("bq27541_reset, point = %d\r\n",
			bq27541_gauge_ops.get_battery_soc());
	} else if (gauge_ic) {
		bq27411_modify_soc_smooth_parameter(gauge_ic, false);
	}
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
static int bq27541_pm_resume(struct device *dev)
{
	if (!gauge_ic) {
		return 0;
	}
	atomic_set(&gauge_ic->suspended, 0);
	bq27541_get_battery_soc();
	return 0;
}

static int bq27541_pm_suspend(struct device *dev)
{
	if (!gauge_ic) {
		return 0;
	}
	atomic_set(&gauge_ic->suspended, 1);
	return 0;
}

static const struct dev_pm_ops bq27541_pm_ops = {
	.resume = bq27541_pm_resume,
	.suspend = bq27541_pm_suspend,
};

#else /*(LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))*/
static int bq27541_resume(struct i2c_client *client)
{
	if (!gauge_ic) {
		return 0;
	}
	atomic_set(&gauge_ic->suspended, 0);
	bq27541_get_battery_soc();
	return 0;
}

static int bq27541_suspend(struct i2c_client *client, pm_message_t mesg)
{
	if (!gauge_ic) {
		return 0;
	}
	atomic_set(&gauge_ic->suspended, 1);
	return 0;
}
#endif /*(LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))*/

bool oplus_gauge_ic_chip_is_null(void)
{
	if (!gauge_ic) {
		return true;
	} else {
		return false;
	}
}

static int bq27541_driver_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct chip_bq27541 *fg_ic;
	struct oplus_gauge_chip	*chip;
	int rerun_num = 3;
	int rc =0 ;

	fg_ic = kzalloc(sizeof(*fg_ic), GFP_KERNEL);
	if (!fg_ic) {
		dev_err(&client->dev, "failed to allocate device info data\n");
		return -ENOMEM;
	}
	i2c_set_clientdata(client, fg_ic);
	fg_ic->dev = &client->dev;
	fg_ic->client = client;
	atomic_set(&fg_ic->suspended, 0);
	gauge_ic = fg_ic;
	bq27541_parse_dt(fg_ic);
rerun :
	rerun_num--;
	bq27541_hw_config(fg_ic);

	dev_err(&client->dev, "2020 0924 param\n");
	if (!fg_ic->modify_soc_calibration) {
		bq28z610_batt_full_zero_parameter();
	}
/*
	INIT_DELAYED_WORK(&fg_ic->hw_config, bq27541_hw_config);
	schedule_delayed_work(&fg_ic->hw_config, 0);
*/
	fg_ic->soc_pre = 50;
	if(fg_ic->batt_bq28z610) {
		fg_ic->batt_vol_pre = 3800;
		fg_ic->fc_pre = 0;
		fg_ic->qm_pre = 0;
		fg_ic->pd_pre = 0;
		fg_ic->rcu_pre = 0;
		fg_ic->rcf_pre = 0;
		fg_ic->fcu_pre = 0;
		fg_ic->fcf_pre = 0;
		fg_ic->sou_pre = 0;
		fg_ic->do0_pre = 0;
		fg_ic->doe_pre = 0;
		fg_ic->trm_pre = 0;
		fg_ic->pc_pre = 0;
		fg_ic->qs_pre = 0;
	} else {
		fg_ic->batt_vol_pre = 3800;
		fg_ic->fc_pre = 0;
		fg_ic->qm_pre = 0;
		fg_ic->pd_pre = 0;
		fg_ic->rcu_pre = 0;
		fg_ic->rcf_pre = 0;
		fg_ic->fcu_pre = 0;
		fg_ic->fcf_pre = 0;
		fg_ic->sou_pre = 0;
		fg_ic->do0_pre = 0;
		fg_ic->doe_pre = 0;
		fg_ic->trm_pre = 0;
		fg_ic->pc_pre = 0;
		fg_ic->qs_pre = 0;
	}
	fg_ic->max_vol_pre = 3800;
	fg_ic->min_vol_pre = 3800;
	fg_ic->current_pre = 999;
	rc = bq27411_modify_soc_smooth_parameter(fg_ic, true);
	if(rc == BATT_FULL_ERROR && rerun_num > 0)
		goto rerun;//only for wite battery full param in guage dirver probe on 7250 platform
	chip = devm_kzalloc(&client->dev,
		sizeof(struct oplus_gauge_chip), GFP_KERNEL);
	if (!chip) {
		pr_err("kzalloc() failed.\n");
		gauge_ic = NULL;
		return -ENOMEM;
	}
	chip->client = client;
	chip->dev = &client->dev;
	chip->gauge_ops = &bq27541_gauge_ops;
	chip->device_type = gauge_ic->device_type;
	chip->device_type_for_warp = gauge_ic->device_type_for_warp;
	oplus_gauge_init(chip);
	register_gauge_devinfo(fg_ic);
	if (fg_ic->bq28z610_need_balancing)
		fg_ic->bq28z610_device_chem = bq28z610_get_device_chemistry();

	gauge_ic->authenticate_data = devm_kzalloc(&client->dev,
		sizeof(struct bq27541_authenticate_data), GFP_KERNEL);
	if (!gauge_ic->authenticate_data) {
		pr_err("kzalloc() authenticate_data failed.\n");
		gauge_ic = NULL;
		return -ENOMEM;
	}
	chg_debug("bq27541_driver_probe success\n");
	return 0;
}
/**********************************************************
  *
  *   [platform_driver API]
  *
  *********************************************************/


static const struct of_device_id bq27541_match[] = {
	{ .compatible = "oplus,bq27541-battery"},
	{ },
};

static const struct i2c_device_id bq27541_id[] = {
	{ "bq27541-battery", 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, bq27541_id);

static struct i2c_driver bq27541_i2c_driver = {
	.driver = {
	.name = "bq27541-battery",
	.owner	= THIS_MODULE,
	.of_match_table = bq27541_match,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
	.pm = &bq27541_pm_ops,
#endif
	},
	.probe = bq27541_driver_probe,
	.shutdown	= bq27541_reset,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0))
	.resume	 = bq27541_resume,
	.suspend	= bq27541_suspend,
#endif
	.id_table	= bq27541_id,
};
/*----------------------------------------------------------------------------*/
/*static void  bq27541_exit(void)
{
	i2c_del_driver(&bq27541_i2c_driver);
}*/
/*----------------------------------------------------------------------------*/

#ifndef OPLUS_CHG_OP_DEF
module_i2c_driver(bq27541_i2c_driver);
#else
static __init int bq27541_driver_init(void)
{
	return i2c_add_driver(&bq27541_i2c_driver);
}

static __exit void bq27541_driver_exit(void)
{
	i2c_del_driver(&bq27541_i2c_driver);
}

oplus_chg_module_register(bq27541_driver);
#endif

#ifndef MODULE
MODULE_DESCRIPTION("Driver for bq27541 charger chip");
MODULE_LICENSE("GPL v2");
#endif
