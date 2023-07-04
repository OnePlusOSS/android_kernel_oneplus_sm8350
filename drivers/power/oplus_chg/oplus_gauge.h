/* SPDX-License-Identifier: GPL-2.0-only  */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef _OPLUS_GAUGE_H_
#define _OPLUS_GAUGE_H_

#include <linux/i2c.h>
#include <linux/power_supply.h>

struct oplus_gauge_chip {
	struct i2c_client *client;
	struct device *dev;
	struct oplus_gauge_operations *gauge_ops;
	struct power_supply *batt_psy;
	int device_type;
	int device_type_for_warp;
};

struct oplus_gauge_operations {
	int (*get_battery_mvolts)(void);
	int (*get_battery_fc)(void);
	int (*get_battery_qm)(void);
	int (*get_battery_pd)(void);
	int (*get_battery_rcu)(void);
	int (*get_battery_rcf)(void);
	int (*get_battery_fcu)(void);
	int (*get_battery_fcf)(void);
	int (*get_battery_sou)(void);
	int (*get_battery_do0)(void);
	int (*get_battery_doe)(void);
	int (*get_battery_trm)(void);
	int (*get_battery_pc)(void);
	int (*get_battery_qs)(void);
	int (*get_battery_temperature)(void);
	int (*get_batt_remaining_capacity)(void);
	int (*get_battery_soc)(void);
	int (*get_average_current)(void);
	int (*get_battery_fcc)(void);
	int (*get_battery_cc)(void);
	int (*get_battery_soh)(void);
	bool (*get_battery_authenticate)(void);
	bool (*get_battery_hmac)(void);
	void (*set_battery_full)(bool);
	int (*get_prev_battery_mvolts) (void);
	int (*get_prev_battery_temperature) (void);
	int (*get_prev_battery_soc) (void);
	int (*get_prev_average_current) (void);
	int (*get_prev_batt_remaining_capacity)(void);
	int (*get_battery_mvolts_2cell_max) (void);
	int (*get_battery_mvolts_2cell_min) (void);
	int (*get_prev_battery_mvolts_2cell_max) (void);
	int (*get_prev_battery_mvolts_2cell_min) (void);
	int (*get_prev_batt_fcc)(void);
	int (*update_battery_dod0) (void);
	int (*update_soc_smooth_parameter) (void);
	int (*get_battery_cb_status) (void);
	int (*get_gauge_i2c_err) (void);
	void (*clear_gauge_i2c_err) (void);
	int (*get_passdchg) (int *val);
	int (*dump_register) (void);
};

/****************************************
 * oplus_gauge_init - initialize oplus_gauge_chip
 * @chip: pointer to the oplus_gauge_cip
 * @clinet: i2c client of the chip
 *
 * Returns: 0 - success; -1/errno - failed
 ****************************************/
void oplus_gauge_init(struct oplus_gauge_chip *chip);

int oplus_gauge_get_batt_mvolts(void);
int oplus_gauge_get_batt_fc(void);
int oplus_gauge_get_batt_qm(void);
int oplus_gauge_get_batt_pd(void);
int oplus_gauge_get_batt_rcu(void);
int oplus_gauge_get_batt_rcf(void);
int oplus_gauge_get_batt_fcu(void);
int oplus_gauge_get_batt_fcf(void);
int oplus_gauge_get_batt_sou(void);
int oplus_gauge_get_batt_do0(void);
int oplus_gauge_get_batt_doe(void);
int oplus_gauge_get_batt_trm(void);
int oplus_gauge_get_batt_pc(void);
int oplus_gauge_get_batt_qs(void);
int oplus_gauge_get_batt_mvolts_2cell_max(void);
int oplus_gauge_get_batt_mvolts_2cell_min(void);

int oplus_gauge_get_batt_temperature(void);
int oplus_gauge_get_batt_soc(void);
int oplus_gauge_get_batt_current(void);
int oplus_gauge_get_remaining_capacity(void);
int oplus_gauge_get_device_type(void);
int oplus_gauge_get_device_type_for_warp(void);

int oplus_gauge_get_batt_fcc(void);

int oplus_gauge_get_batt_cc(void);
int oplus_gauge_get_batt_soh(void);
bool oplus_gauge_get_batt_hmac(void);
bool oplus_gauge_get_batt_authenticate(void);
void oplus_gauge_set_batt_full(bool);
bool oplus_gauge_check_chip_is_null(void);

int oplus_gauge_get_prev_batt_mvolts(void);
int oplus_gauge_get_prev_batt_mvolts_2cell_max(void);
int oplus_gauge_get_prev_batt_mvolts_2cell_min(void);
int oplus_gauge_get_prev_batt_temperature(void);
int oplus_gauge_get_prev_batt_soc(void);
int oplus_gauge_get_prev_batt_current(void);
int oplus_gauge_get_prev_remaining_capacity(void);
int oplus_gauge_get_prev_batt_fcc(void);
int oplus_gauge_update_battery_dod0(void);
int oplus_gauge_update_soc_smooth_parameter(void);
int oplus_gauge_get_battery_cb_status(void);
int oplus_gauge_get_i2c_err(void);
void oplus_gauge_clear_i2c_err(void);
int oplus_gauge_get_passedchg(int *val);
int oplus_gauge_dump_register(void);

#if defined(CONFIG_OPLUS_CHARGER_MTK6763) || defined(CONFIG_OPLUS_CHARGER_MTK6771)
extern int oplus_fuelgauged_init_flag;
extern struct power_supply *oplus_batt_psy;
extern struct power_supply *oplus_usb_psy;
extern struct power_supply *oplus_ac_psy;
#endif /* CONFIG_OPLUS_CHARGER_MTK6763 */
#endif /* _OPLUS_GAUGE_H */
