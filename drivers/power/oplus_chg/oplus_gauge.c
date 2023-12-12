// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include "oplus_gauge.h"
#include "oplus_charger.h"
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/debugfs.h>
#ifdef CONFIG_OPLUS_CHG_OOS
#include <linux/oem/project_info.h>
#else
#include <soc/oplus/system/oplus_project.h>
#endif
#ifdef OPLUS_CHG_OP_DEF
#include "oplus_op_def.h"
#endif


static struct oplus_gauge_chip *g_gauge_chip = NULL;

static int gauge_dbg_tbat = 0;
module_param(gauge_dbg_tbat, int, 0644);
MODULE_PARM_DESC(gauge_dbg_tbat, "debug battery temperature");

static int gauge_dbg_vbat = 0;
module_param(gauge_dbg_vbat, int, 0644);
MODULE_PARM_DESC(gauge_dbg_vbat, "debug battery voltage");

static int gauge_dbg_ibat = 0;
module_param(gauge_dbg_ibat, int, 0644);
MODULE_PARM_DESC(gauge_dbg_ibat, "debug battery current");

#ifdef OPLUS_CHG_OP_DEF
static int gauge_dbg_soc = -1;
module_param(gauge_dbg_soc, int, 0644);
MODULE_PARM_DESC(gauge_dbg_soc, "debug battery soc");
#endif

int oplus_gauge_get_batt_mvolts(void)
{
	if (!g_gauge_chip) {
		return 3800;
	} else {
	 	if (gauge_dbg_vbat != 0) {
        	printk(KERN_ERR "[OPLUS_CHG]%s:debug enabled,voltage gauge_dbg_vbat[%d] \n",  __func__, gauge_dbg_vbat);
			return gauge_dbg_vbat;
			}
		return g_gauge_chip->gauge_ops->get_battery_mvolts();
	}
}

int oplus_gauge_get_batt_fc(void)
{
	if (!g_gauge_chip || !g_gauge_chip->gauge_ops
		|| !g_gauge_chip->gauge_ops->get_battery_fc) {
		return 0;
	} else {
		return g_gauge_chip->gauge_ops->get_battery_fc();
	}
}

int oplus_gauge_get_batt_qm(void)
{
	if (!g_gauge_chip || !g_gauge_chip->gauge_ops
		|| !g_gauge_chip->gauge_ops->get_battery_qm) {
		return 0;
	} else {
		return g_gauge_chip->gauge_ops->get_battery_qm();
	}
}

int oplus_gauge_get_batt_pd(void)
{
	if (!g_gauge_chip || !g_gauge_chip->gauge_ops
		|| !g_gauge_chip->gauge_ops->get_battery_pd) {
		return 0;
	} else {
		return g_gauge_chip->gauge_ops->get_battery_pd();
	}
}

int oplus_gauge_get_batt_rcu(void)
{
	if (!g_gauge_chip || !g_gauge_chip->gauge_ops
		|| !g_gauge_chip->gauge_ops->get_battery_rcu) {
		return 0;
	} else {
		return g_gauge_chip->gauge_ops->get_battery_rcu();
	}
}

int oplus_gauge_get_batt_rcf(void)
{
	if (!g_gauge_chip || !g_gauge_chip->gauge_ops
		|| !g_gauge_chip->gauge_ops->get_battery_rcf) {
		return 0;
	} else {
		return g_gauge_chip->gauge_ops->get_battery_rcf();
	}
}

int oplus_gauge_get_batt_fcu(void)
{
	if (!g_gauge_chip || !g_gauge_chip->gauge_ops
		|| !g_gauge_chip->gauge_ops->get_battery_fcu) {
		return 0;
	} else {
		return g_gauge_chip->gauge_ops->get_battery_fcu();
	}
}

int oplus_gauge_get_batt_fcf(void)
{
	if (!g_gauge_chip || !g_gauge_chip->gauge_ops
		|| !g_gauge_chip->gauge_ops->get_battery_fcf) {
		return 0;
	} else {
		return g_gauge_chip->gauge_ops->get_battery_fcf();
	}
}

int oplus_gauge_get_batt_sou(void)
{
	if (!g_gauge_chip || !g_gauge_chip->gauge_ops
		|| !g_gauge_chip->gauge_ops->get_battery_sou) {
		return 0;
	} else {
		return g_gauge_chip->gauge_ops->get_battery_sou();
	}
}

int oplus_gauge_get_batt_do0(void)
{
	if (!g_gauge_chip || !g_gauge_chip->gauge_ops
		|| !g_gauge_chip->gauge_ops->get_battery_do0) {
		return 0;
	} else {
		return g_gauge_chip->gauge_ops->get_battery_do0();
	}
}

int oplus_gauge_get_batt_doe(void)
{
	if (!g_gauge_chip || !g_gauge_chip->gauge_ops
                || !g_gauge_chip->gauge_ops->get_battery_doe) {
		return 0;
	} else {
		return g_gauge_chip->gauge_ops->get_battery_doe();
	}
}

int oplus_gauge_get_batt_trm(void)
{
	if (!g_gauge_chip || !g_gauge_chip->gauge_ops
                || !g_gauge_chip->gauge_ops->get_battery_trm) {
		return 0;
	} else {
		return g_gauge_chip->gauge_ops->get_battery_trm();
	}
}

int oplus_gauge_get_batt_pc(void)
{
	if (!g_gauge_chip || !g_gauge_chip->gauge_ops
		|| !g_gauge_chip->gauge_ops->get_battery_pc) {
		return 0;
	} else {
		return g_gauge_chip->gauge_ops->get_battery_pc();
	}
}

int oplus_gauge_get_batt_qs(void)
{
	if (!g_gauge_chip || !g_gauge_chip->gauge_ops
		|| !g_gauge_chip->gauge_ops->get_battery_qs) {
		return 0;
	} else {
		return g_gauge_chip->gauge_ops->get_battery_qs();
	}
}

int oplus_gauge_get_batt_mvolts_2cell_max(void)
{
	if(!g_gauge_chip)
		return 3800;
	else
		return g_gauge_chip->gauge_ops->get_battery_mvolts_2cell_max();
}

int oplus_gauge_get_batt_mvolts_2cell_min(void)
{
	if(!g_gauge_chip)
		return 3800;
	else
		return g_gauge_chip->gauge_ops->get_battery_mvolts_2cell_min();
}

int oplus_gauge_get_batt_temperature(void)
{
	int batt_temp = 0;
	if (!g_gauge_chip) {
		return 250;
	} else {
		if (gauge_dbg_tbat != 0) {
			printk(KERN_ERR "[OPLUS_CHG]debug enabled, gauge_dbg_tbat[%d] \n", gauge_dbg_tbat);
			return gauge_dbg_tbat;
		}
		batt_temp = g_gauge_chip->gauge_ops->get_battery_temperature();

		if (get_eng_version() == HIGH_TEMP_AGING) {
			printk(KERN_ERR "[OPLUS_CHG]CONFIG_HIGH_TEMP_VERSION enable here, \
					disable high tbat shutdown \n");
			if (batt_temp > 690)
				batt_temp = 690;
		}
		return batt_temp;
	}
}

int oplus_gauge_get_batt_soc(void)
{
	if (!g_gauge_chip) {
		return -1;
	} else {
#ifdef OPLUS_CHG_OP_DEF
		if (gauge_dbg_soc >= 0)
			return gauge_dbg_soc;
		else
#endif
		return g_gauge_chip->gauge_ops->get_battery_soc();
	}
}

int oplus_gauge_get_batt_current(void)
{
	if (!g_gauge_chip) {
		return 100;
	} else {
		if (gauge_dbg_ibat != 0) {
        	printk(KERN_ERR "[OPLUS_CHG]debug enabled,current gauge_dbg_ibat[%d] \n", gauge_dbg_ibat);
			return gauge_dbg_ibat;
			}
		return g_gauge_chip->gauge_ops->get_average_current();
	}
}

int oplus_gauge_get_remaining_capacity(void)
{
	if (!g_gauge_chip) {
		return 0;
	} else {
		return g_gauge_chip->gauge_ops->get_batt_remaining_capacity();
	}
}

int oplus_gauge_get_device_type(void)
{
	if (!g_gauge_chip) {
		return 0;
	} else {
		return g_gauge_chip->device_type;
	}
}

int oplus_gauge_get_device_type_for_warp(void)
{
	if (!g_gauge_chip) {
		return 0;
	} else {
		return g_gauge_chip->device_type_for_warp;
	}
}

int oplus_gauge_get_batt_fcc(void)
{
	if (!g_gauge_chip) {
		return 0;
	} else {
		return g_gauge_chip->gauge_ops->get_battery_fcc();
	}
}

int oplus_gauge_get_batt_cc(void)
{
	if (!g_gauge_chip) {
		return 0;
	} else {
		return g_gauge_chip->gauge_ops->get_battery_cc();
	}
}

int oplus_gauge_get_batt_soh(void)
{
	if (!g_gauge_chip) {
		return 0;
	} else {
		return g_gauge_chip->gauge_ops->get_battery_soh();
	}
}

bool oplus_gauge_get_batt_hmac(void)
{
	if (!g_gauge_chip) {
		return false;
	} else if (!g_gauge_chip->gauge_ops->get_battery_hmac) {
		return true;
	} else  {
		return g_gauge_chip->gauge_ops->get_battery_hmac();
	}
}

bool oplus_gauge_get_batt_authenticate(void)
{
	if (!g_gauge_chip) {
		return false;
	} else {
		return g_gauge_chip->gauge_ops->get_battery_authenticate();
	}
}

void oplus_gauge_set_batt_full(bool full)
{
	if (g_gauge_chip) {
		g_gauge_chip->gauge_ops->set_battery_full(full);
	}
}

bool oplus_gauge_check_chip_is_null(void)
{
	if (!g_gauge_chip) {
		return true;
	} else {
		return false;
	}
}

void oplus_gauge_init(struct oplus_gauge_chip *chip)
{
	g_gauge_chip = chip;
}

int oplus_gauge_get_prev_batt_mvolts(void)
{
	if (!g_gauge_chip)
		return 3800;
	else {
		if (gauge_dbg_vbat != 0) {
			printk(KERN_ERR "[OPLUS_CHG]%s:debug enabled,voltage gauge_dbg_vbat[%d] \n",  __func__, gauge_dbg_vbat);
			return gauge_dbg_vbat;
		}
		return g_gauge_chip->gauge_ops->get_prev_battery_mvolts();
	}
}

int oplus_gauge_get_prev_batt_mvolts_2cell_max(void)
{
	if(!g_gauge_chip)
		return 3800;
	else{
		if (gauge_dbg_vbat != 0) {
		    printk(KERN_ERR "[OPLUS_CHG]%s: debug enabled,voltage gauge_dbg_vbat[%d] \n", __func__, gauge_dbg_vbat);
		    return gauge_dbg_vbat;
		}
		return g_gauge_chip->gauge_ops->get_prev_battery_mvolts_2cell_max();
		}
}

int oplus_gauge_get_prev_batt_mvolts_2cell_min(void)
{
	if(!g_gauge_chip)
		return 3800;
	else {
		if (gauge_dbg_vbat != 0) {
			printk(KERN_ERR "[OPLUS_CHG]%s:debug enabled,voltage gauge_dbg_vbat[%d] \n",  __func__, gauge_dbg_vbat);
			return gauge_dbg_vbat;
			}
		return g_gauge_chip->gauge_ops->get_prev_battery_mvolts_2cell_min();
    }
}

int oplus_gauge_get_prev_batt_temperature(void)
{
	int batt_temp = 0;
	if (!g_gauge_chip)
		return 250;
	else {
		if (gauge_dbg_tbat != 0) {
			printk(KERN_ERR "[OPLUS_CHG]%s: debug enabled, gauge_dbg_tbat[%d] \n", __func__, gauge_dbg_tbat);
			return gauge_dbg_tbat;
		}
		batt_temp = g_gauge_chip->gauge_ops->get_prev_battery_temperature();

		if (get_eng_version() == HIGH_TEMP_AGING) {
			printk(KERN_ERR "[OPLUS_CHG]CONFIG_HIGH_TEMP_VERSION enable here, \
			disable high tbat shutdown \n");
			if (batt_temp > 690)
				batt_temp = 690;
		}
		return batt_temp;
	}
}

int oplus_gauge_get_prev_batt_soc(void)
{
	if (!g_gauge_chip) {
		return 50;
	} else {
#ifdef OPLUS_CHG_OP_DEF
		if (gauge_dbg_soc >= 0)
			return gauge_dbg_soc;
		else
#endif
		return g_gauge_chip->gauge_ops->get_prev_battery_soc();
	}
}

int oplus_gauge_get_prev_batt_current(void)
{
	if (!g_gauge_chip)
		return 100;
	else {
		if (gauge_dbg_ibat != 0) {
			printk(KERN_ERR "[OPLUS_CHG]%s:debug enabled,current gauge_dbg_ibat[%d] \n", __func__, gauge_dbg_ibat);
			return gauge_dbg_ibat;
		}
		return g_gauge_chip->gauge_ops->get_prev_average_current();
		}
}

int oplus_gauge_get_prev_remaining_capacity(void)
{
	if (!g_gauge_chip) {
		return 0;
	} else {
		return g_gauge_chip->gauge_ops->get_prev_batt_remaining_capacity();
	}
}

int oplus_gauge_update_battery_dod0(void)
{
	if (!g_gauge_chip)
		return 0;
	else
		return g_gauge_chip->gauge_ops->update_battery_dod0();
}

int oplus_gauge_update_soc_smooth_parameter(void)
{
	if (!g_gauge_chip)
		return 0;
	else
		return g_gauge_chip->gauge_ops->update_soc_smooth_parameter();
}

int oplus_gauge_get_battery_cb_status(void)
{
	if (!g_gauge_chip)
		return 0;
	else
		return g_gauge_chip->gauge_ops->get_battery_cb_status();
}

int oplus_gauge_get_i2c_err(void)
{
	if (!g_gauge_chip || !g_gauge_chip->gauge_ops
		|| !g_gauge_chip->gauge_ops->get_gauge_i2c_err) {
		return 0;
	} else {
		return g_gauge_chip->gauge_ops->get_gauge_i2c_err();
	}
}

void oplus_gauge_clear_i2c_err(void)
{
	if (!g_gauge_chip || !g_gauge_chip->gauge_ops
		|| !g_gauge_chip->gauge_ops->clear_gauge_i2c_err) {
		return;
	} else {
		return g_gauge_chip->gauge_ops->clear_gauge_i2c_err();
	}
}

int oplus_gauge_get_prev_batt_fcc(void)
{
	if (!g_gauge_chip) {
		return 0;
	} else {
		return g_gauge_chip->gauge_ops->get_prev_batt_fcc();
	}
}
int oplus_gauge_get_passedchg(int *val)
{
	int ret;
	if (!g_gauge_chip || !g_gauge_chip->gauge_ops
		|| !g_gauge_chip->gauge_ops->get_passdchg) {
		return 0;
	} else {
		ret = g_gauge_chip->gauge_ops->get_passdchg(val);
		if(ret) {
			pr_err("%s: get passedchg error %d\n", __FUNCTION__, ret);
		}
		return ret;
	}
}

int oplus_gauge_dump_register(void)
{
	if (!g_gauge_chip)
		return 0;
	else {
		if (g_gauge_chip->gauge_ops && g_gauge_chip->gauge_ops->dump_register) {
			g_gauge_chip->gauge_ops->dump_register();
		}
		return 0;
	}
}
