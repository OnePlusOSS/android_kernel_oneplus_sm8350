/* SPDX-License-Identifier: GPL-2.0-only  */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */


#ifndef __OPLUS_warp_FW_H__

#define __OPLUS_warp_FW_H__

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

#include <linux/xlog.h>
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

#endif

#include "../oplus_charger.h"
#include "../oplus_gauge.h"
#include "../oplus_warp.h"


#define OPLUS_warp_RESET_MCU_EN			83
#define OPLUS_warp_MCU_AP_CLK			13
#define OPLUS_warp_MCU_AP_DATA			14
#define OPLUS_warp_SW_CTRL_EVT			42
#define OPLUS_warp_SW_CTRL_DVT			42
#define CUST_EINT_MCU_AP_DATA 			14

#ifndef CONFIG_OPLUS_CHARGER_MTK
enum {
	HW_VERSION_UNKNOWN,
	HW_VERSION__EVT,
	HW_VERSION__DVT,
	HW_VERSION__MCU,
	HW_VERSION__15041,
};
#endif
enum {
	warp_FW_TYPE_INVALID,
	warp_FW_TYPE_STM8S_4350,
	warp_FW_TYPE_STM8S_4400_ORI,
	warp_FW_TYPE_STM8S_4400_AVOID_FAKE_ADAPTER,
	warp_FW_TYPE_STM8S_4400_AVOID_FG_I2C_ERR,
	warp_FW_TYPE_STM8S_4400_AVOID_OVER_TEMP,
	warp_FW_TYPE_STM8S_4400_AVOID_OVER_TEMP_NTC61C,
	warp_FW_TYPE_STM8S_4400_warp_FFC_09C,
	warp_FW_TYPE_STM8S_4400_warp_FFC_15C,
	warp_FW_TYPE_STM8S_4400_warp_FFC_15C_FV4450,
	warp_FW_TYPE_STM8S_4450_FFC = 10,
	warp_FW_TYPE_STM8S_4450_FFC_5V6A,
	warp_FW_TYPE_STM8S_4400_warp_FFC_15C_TI411,
	warp_FW_TYPE_STM8S_4450_FFC_5C_warp,
	warp_FW_TYPE_STM8S_4400_FFC_5V5P9A,
	warp_FW_TYPE_STM8S_4450_FFC_5V6A_warp_4052MA_3BIT,
	warp_FW_TYPE_STM8S_4450_FFC_5V4A_warp_4052MA_3BIT,
	warp_FW_TYPE_STM8S_4400_warp_FFC_15C_18041,
	warp_FW_TYPE_STM8S_4450_warp_FFC_5V6A_19365,
	warp_FW_TYPE_STM8S_4450_FFC_SHORT_RESET_WINDOW,
	warp_FW_TYPE_STM8S_4400_Swarp_3500MA = 0x20,
	warp_FW_TYPE_STM8S_4400_Swarp_1000MA = 0x21,
	warp_FW_TYPE_STM8S_4400_Swarp_5000MA = 0x22,
	warp_FW_TYPE_STM8S_4400_Swarp_6500MA = 0x23,
	warp_FW_TYPE_STM8S_4450_Swarp_6500MA = 0x24,
	warp_FW_TYPE_STM8S_4450_Swarp_6500MA_FV4490 = 0x25,
	warp_FW_TYPE_STM8S_4450_Swarp_6500MA_disableI2C = 0x26,
	warp_FW_TYPE_STM8S_4400_Swarp_6500MA_8250 = 0x27,
	warp_FW_TYPE_STM8S_4450_Swarp_6500MA_8250 = 0x28,
	warp_FW_TYPE_STM8S_4400_Swarp_6500MA_8250_LINK = 0x29,
	warp_FW_TYPE_STM8S_4400_Swarp_6500MA_8250_LINK_LITE = 0x2A,
};

enum {
	warp_FW_TYPE_N76E_INVALID,
	warp_FW_TYPE_N76E_4400_AVOID_OVER_TEMP_NTC61C,
	warp_FW_TYPE_N76E_4400_warp_FFC_15C,
	warp_FW_TYPE_N76E_4400_warp_FFC_15C_FV4450,
};

enum {
	warp_FW_TYPE_RK826_INVALID,
	warp_FW_TYPE_RK826_4400_warp_FFC_15C,
	warp_FW_TYPE_RK826_4450_warp_FFC_5V6A_4BIT,
	warp_FW_TYPE_RK826_4450_Swarp_FFC_5V6A_CHAKA,
	warp_FW_TYPE_RK826_4450_Swarp_FFC_6300MA_HIMA,
	warp_FW_TYPE_RK826_4450_Swarp_FFC_6300MA_HOREE,
	warp_FW_TYPE_RK826_4450_Swarp_FFC_5V6A_DALI,
	warp_FW_TYPE_RK826_4450_Swarp_6300MA = 0x20,
};

enum {
	warp_FW_TYPE_OP10_INVALID,
	warp_FW_TYPE_OP10_4400_warp_FFC_15C,
	warp_FW_TYPE_OP10_4450_warp_FFC_5V4A_4BIT,
	warp_FW_TYPE_OP10_4250_warp_FFC_10V6A_4BIT,
	warp_FW_TYPE_OP10_4450_warp_FFC_5V6A_4BIT,
};

extern int g_hw_version;
extern void init_hw_version(void);
extern int get_warp_mcu_type(struct oplus_warp_chip *chip);
extern bool chargin_hw_init_done_warp;



int oplus_warp_gpio_dt_init(struct oplus_warp_chip *chip);

void opchg_set_clock_active(struct oplus_warp_chip *chip);
void opchg_set_clock_sleep(struct oplus_warp_chip *chip);

void opchg_set_data_active(struct oplus_warp_chip *chip);
void opchg_set_data_sleep(struct oplus_warp_chip *chip);

int opchg_get_gpio_ap_data(struct oplus_warp_chip *chip);
int opchg_read_ap_data(struct oplus_warp_chip *chip);
void opchg_reply_mcu_data(struct oplus_warp_chip *chip, int ret_info,
	int device_type);
void opchg_reply_mcu_data_4bits(struct oplus_warp_chip *chip, int ret_info,
	int device_type);
void opchg_set_reset_active(struct oplus_warp_chip *chip);
void opchg_set_reset_sleep(struct oplus_warp_chip *chip);
void opchg_set_warp_chargerid_switch_val(struct oplus_warp_chip *chip,
	int value);
int oplus_warp_get_reset_gpio_val(struct oplus_warp_chip *chip);
int oplus_warp_get_switch_gpio_val(struct oplus_warp_chip *chip);
int oplus_warp_get_ap_clk_gpio_val(struct oplus_warp_chip *chip);
int opchg_get_clk_gpio_num(struct oplus_warp_chip *chip);
int opchg_get_data_gpio_num(struct oplus_warp_chip *chip);

bool oplus_is_power_off_charging(struct oplus_warp_chip *chip);
bool oplus_is_charger_reboot(struct oplus_warp_chip *chip);

void switch_fast_chg(struct oplus_warp_chip *chip);
void oplus_warp_delay_reset_mcu_init(struct oplus_warp_chip *chip);
extern void opchg_set_switch_mode(struct oplus_warp_chip *chip, int mode);

extern void reset_fastchg_after_usbout(struct oplus_warp_chip *chip);

void oplus_warp_eint_register(struct oplus_warp_chip *chip);
void oplus_warp_eint_unregister(struct oplus_warp_chip *chip);

void oplus_warp_fw_type_dt(struct oplus_warp_chip *chip);

#ifdef warp_MCU_PIC16F
unsigned char Pic16F_firmware_data[] = {
	0x00, 0x00, 0x00, 0x00, 0x00,
};

/* CONFIG_warp_BATTERY_4400MV, fw_ver: 0x30 - 0x7f */

unsigned char Stm8s_firmware_data_4400mv[] = {
	0x00, 0x00, 0x00, 0x00, 0x00,
};

unsigned char Stm8s_fw_data_4400_avoid_fake_adapter[] = {
	0x00, 0x00, 0x00, 0x00, 0x00,
};

unsigned char Stm8s_fw_data_4400_avoid_fg_i2c_err[] = {
	0x00, 0x00, 0x00, 0x00, 0x00,
};

unsigned char Stm8s_fw_data_4400_avoid_over_temp[] = {
	0x00, 0x00, 0x00, 0x00, 0x00,
};

unsigned char Stm8s_fw_data_4400_avoid_over_temp_ntc61c[] = {
	0x00, 0x00, 0x00, 0x00, 0x00,
};

unsigned char Stm8s_fw_data_4400_warp_ffc_09c[] = {
	0x00, 0x00, 0x00, 0x00, 0x00,
};

unsigned char Stm8s_fw_data_4400_warp_ffc_15c[] = {
	0x00, 0x00, 0x00, 0x00, 0x00,
};

unsigned char Stm8s_fw_data_4400_warp_ffc_15c_fv4450[] = {
	0x00, 0x00, 0x00, 0x00, 0x00,
};

unsigned char Stm8s_fw_data_4450_ffc[] = {
	0x00, 0x00, 0x00, 0x00, 0x00,
};

unsigned char Stm8s_fw_data_4450_ffc_5c_warp[] = {
	0x00, 0x00, 0x00, 0x00, 0x00,
};

#ifdef CONFIG_OPLUS_SM8150R_CHARGER
unsigned char Stm8s_fw_data_4450_ffc_5v6a[] = {
	0x00, 0x00, 0x00, 0x00, 0x00,
};
#else
unsigned char Stm8s_fw_data_4450_ffc_5v6a[] = {
	0x00, 0x00, 0x00, 0x00, 0x00,
};
#endif

unsigned char Stm8s_fw_data_4400_warp_ffc_15c_ti411[] = {
	0x00, 0x00, 0x00, 0x00, 0x00,
};

unsigned char Stm8s_fw_data_4400_ffc_5v5p9a[] = {
	0x00, 0x00, 0x00, 0x00, 0x00,
};

unsigned char Stm8s_fw_data_4450_ffc_5v6a_3bit[] = {
	0x00, 0x00, 0x00, 0x00, 0x00,
};

unsigned char Stm8s_fw_data_4450_warp_ffc_5v4a_3bit[] = {
	0x00, 0x00, 0x00, 0x00, 0x00,
};

unsigned char Stm8s_fw_data_4400_warp_ffc_15c_18041[] = {
	0x00, 0x00, 0x00, 0x00, 0x00,
};

unsigned char Stm8s_fw_data_4450_warp_FFC_5V6A_19365[] = {
	0x00, 0x00, 0x00, 0x00, 0x00,
};

unsigned char Stm8s_fw_data_4450_ffc_ShortResetWindow[] = {
	0x00, 0x00, 0x00, 0x00, 0x00,
};

unsigned char Stm8s_fw_data_4400_swarp_3500MA[] = {
	0x00, 0x00, 0x00, 0x00, 0x00,
};

unsigned char Stm8s_fw_data_4400_swarp_1000MA[] = {
	0x00, 0x00, 0x00, 0x00, 0x00,
};

#ifdef CONFIG_OPLUS_SM8150R_CHARGER
unsigned char Stm8s_fw_data_4400_swarp_5000MA[] = {
	0x00, 0x00, 0x00, 0x00, 0x00,
};
#else
unsigned char Stm8s_fw_data_4400_swarp_5000MA[] = {
	0x00, 0x00, 0x00, 0x00, 0x00,
};
#endif

unsigned char Stm8s_fw_data_4400_swarp_6500MA[] = {
	0x00, 0x00, 0x00, 0x00, 0x00,
};

unsigned char Stm8s_fw_data_4450_swarp_6500MA[] = {
	0x00, 0x00, 0x00, 0x00, 0x00,
};

unsigned char Stm8s_fw_data_4450_swarp_6500MA_fv4490[] = {
	0x00, 0x00, 0x00, 0x00, 0x00,
};

unsigned char Stm8s_fw_data_4450_swarp_6500MA_disableI2C[] = {
	0x00, 0x00, 0x00, 0x00, 0x00,
};

unsigned char Stm8s_fw_data_4400_swarp_6500MA_8250[] = {
	0x00, 0x00, 0x00, 0x00, 0x00,
};

unsigned char Stm8s_fw_data_4450_swarp_6500MA_8250[] = {
	0x00, 0x00, 0x00, 0x00, 0x00,
};

unsigned char Stm8s_fw_data_4400_swarp_6500MA_8250_link[] = {
	0x00, 0x00, 0x00, 0x00, 0x00,
};

unsigned char Stm8s_fw_data_4400_swarp_6500MA_8250_link_lite[] = {
	0x00, 0x00, 0x00, 0x00, 0x00,
};

#endif	/* warp_MCU_STM8S */


#ifdef warp_MCU_N76E
unsigned char n76e_fw_data_4400[] = {
	0x00, 0x00, 0x00, 0x00, 0x00,
};

unsigned char n76e_fw_data_4400_ntc61c[] = {
	0x00, 0x00, 0x00, 0x00, 0x00,
};

unsigned char n76e_fw_data_4400_warp_ffc_15c[] = {
	0x00, 0x00, 0x00, 0x00, 0x00,
};

unsigned char n76e_fw_data_4400_warp_ffc_15c_fv4450[] = {
	0x00, 0x00, 0x00, 0x00, 0x00,
};

#endif	/* warp_MCU_N76E */

#ifdef warp_ASIC_RK826

unsigned char rk826_fw_data_4400_warp_ffc_15c[] = {
	0x00, 0x00, 0x00, 0x00, 0x00,
};

unsigned char rk826_fw_data_4450_warp_ffc_5v6a_4bit[] = {
	0x00, 0x00, 0x00, 0x00, 0x00,
};

unsigned char rk826_fw_data_4450_swarp_ffc_5v6a_chaka[] = {
	0x00, 0x00, 0x00, 0x00, 0x00,
};

unsigned char rk826_fw_data_4450_swarp_ffc_6300mA_hima[] = {
	0x00, 0x00, 0x00, 0x00, 0x00,
};

unsigned char rk826_fw_data_4450_swarp_ffc_6300mA_horee[] = {
	0x00, 0x00, 0x00, 0x00, 0x00,
};

unsigned char rk826_fw_data_4450_swarp_ffc_5v6a_dali[] = {
	0x00, 0x00, 0x00, 0x00, 0x00,
};

unsigned char rk826_fw_data_4450_swarp_evt2_4500ma[] = {
	0x00, 0x00, 0x00, 0x00, 0x00,
};

unsigned char rk826_fw_data_4450_swarp_6300ma[] = {
	0x00, 0x00, 0x00, 0x00, 0x00,
};

#endif /* warp_ASIC_RK826 */

#ifdef warp_ASIC_OP10 /* warp_ASIC_OP10 */

unsigned char op10_fw_data_4400_warp_ffc_15c[] = {
	0x00, 0x00, 0x00, 0x00, 0x00,
};

unsigned char op10_fw_data_4450_warp_ffc_5v4a_4bit[] = {
	0x00, 0x00, 0x00, 0x00, 0x00,
};

unsigned char op10_fw_data_4250_warp_ffc_10v6a_4bit[] = {
	0x00, 0x00, 0x00, 0x00, 0x00,
};

unsigned char op10_fw_data_4450_warp_ffc_5v6a_4bit[] = {
	0x00, 0x00, 0x00, 0x00, 0x00,
};
#endif /* warp_ASIC_OP10 */

#endif	/* __OPLUS_warp_FW_H__ */

