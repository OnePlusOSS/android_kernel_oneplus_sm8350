/*
 * Copyright (c) 2019 The Linux Foundation. All rights reserved.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/types.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include "oplus_camera_wl2868c.h"

static int ldo_id = 0;
static int is_probe_success = false;

struct i2c_device{
    unsigned short i2c_addr;
    unsigned short chip_addr;
    unsigned short ldoId;
    unsigned short enable_addr;
    unsigned int enable_count[EXT_MAX];
};

struct i2c_device which_ldo_chip[] = {
    {WL2868C_LDO_I2C_ADDR,  WL2868C_CHIP_REV_ADDR,  CAMERA_LDO_WL2868C,  WL2868C_LDO_EN_ADDR, {0}},
    {FAN53870_LDO_I2C_ADDR, FAN53870_CHIP_REV_ADDR, CAMERA_LDO_FAN53870, FAN53870_LDO_EN_ADDR, {0}},
};

/*****************************************************************************
 * GLobal Variable
 *****************************************************************************/
//#define USE_CONTROL_ENGPIO
/***
 * finger also use this ldo, it will use pinctrl;
 * path is kernel/msm-5.4/driver/regulator/wl2868c.c
***/
static struct i2c_client *wl2868c_i2c_client;
#ifdef USE_CONTROL_ENGPIO
static struct pinctrl *wl2868c_pctrl; /* static pinctrl instance */
#endif

struct mutex i2c_control_mutex;

/*****************************************************************************
 * Function Prototype
 *****************************************************************************/
static int wl2868c_dts_probe(struct platform_device *pdev);
static int wl2868c_dts_remove(struct platform_device *pdev);
static int wl2868c_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id);
static int wl2868c_i2c_remove(struct i2c_client *client);

/*****************************************************************************
 * Extern Area
 *****************************************************************************/
int wl2868c_check_ldo_status(void)
{
    return is_probe_success;
}
EXPORT_SYMBOL_GPL(wl2868c_check_ldo_status);

int wl2868c_set_en_ldo(EXT_SELECT ldonum,unsigned int en)
{
    int ret= 0;
    unsigned int value =0;

    if (NULL == wl2868c_i2c_client) {
            WL2868C_PRINT("[wl2868c] wl2868c_i2c_client is null!!\n");
            return -1;
    }

    wl2868c_i2c_client->addr = which_ldo_chip[ldo_id].i2c_addr;
    WL2868C_PRINT("[wl2868c] i2c_addr=0x%x ldo_en=0x%x\n", which_ldo_chip[ldo_id].i2c_addr, which_ldo_chip[ldo_id].enable_addr);
    ret = i2c_smbus_read_byte_data(wl2868c_i2c_client, which_ldo_chip[ldo_id].enable_addr);
    if (ret <0)
    {
        WL2868C_PRINT("[wl2868c] wl2868c_set_en_ldo read error!\n");
        goto out;
    }

    if(ldonum == EXT_NONE) {
        WL2868C_PRINT("[wl2868c] %s ldo setting not found in ldolist!!!\n",__FUNCTION__);
        return -2;
    }

    if(en == 0)
    {
        value = (ret & (~(0x01<<ldonum)));
    }
    else
    {
        if(ldo_id == 0 || which_ldo_chip[ldo_id].ldoId == CAMERA_LDO_WL2868C) {
            value = (ret|(0x01<<ldonum))|0x80;
        }else if (which_ldo_chip[ldo_id].ldoId == CAMERA_LDO_FAN53870) {
            value = (ret|(0x01<<ldonum));
        }
    }

    ret = i2c_smbus_write_byte_data(wl2868c_i2c_client, which_ldo_chip[ldo_id].enable_addr, value);
    if (ret < 0) {
        goto out;
    }
    WL2868C_PRINT("[wl2868c] wl2868c_set_en_ldo enable before:%x after set :%x\n",ret,value);
    return 0;

out:

    WL2868C_PRINT("[wl2868c] wl2868c_set_en_ldo enable error!\n");
    return ret;
}
EXPORT_SYMBOL_GPL(wl2868c_set_en_ldo);
/** wl2868c
   Voutx=0.496v+LDOX_OUT[6:0]*0.008V LDO1/LDO2
   Voutx=1.504v+LDOX_OUT[7:0]*0.008V LDO3~LDO7
===FAN53870
   Voutx=0.800v+(LDOX_OUT[6:0]-99)*0.008V LDO1/LDO2
   Voutx=1.500v+()LDOX_OUT[7:0]-16)*0.008V LDO3~LDO7
*/
int wl2868c_set_ldo_value(EXT_SELECT ldonum,unsigned int value)
{
    int ret = 0;
    unsigned int  Ldo_out =0;
    unsigned char regaddr =0;

    WL2868C_PRINT("[wl2868c] %s enter!!!\n",__FUNCTION__);

    if (NULL == wl2868c_i2c_client) {
        WL2868C_PRINT("[wl2868c] wl2868c_i2c_client is null!!\n");

        return -1;
    }
    if(ldonum >= EXT_MAX)
    {
        WL2868C_PRINT("[wl2868c] error ldonum not support!!!\n");
        return -2;
    }

    switch(ldonum)
    {
        case EXT_LDO1:
        case EXT_LDO2:
            if(ldo_id == 0 || which_ldo_chip[ldo_id].ldoId == CAMERA_LDO_WL2868C) {//WL2868C
                if (value < 496) {
                    WL2868C_PRINT("[WL2868C] error vol!!!\n");
                    ret = -1;
                    goto out;
                } else {
                    Ldo_out = (value - 496)/8;
                }
            } else if (which_ldo_chip[ldo_id].ldoId == CAMERA_LDO_FAN53870) {//FAN53870
                if (value < 800) {
                    WL2868C_PRINT("[FAN53870] error vol!!!\n");
                    ret = -1;
                    goto out;
                } else {
                    Ldo_out = (value - 800)/8 + 99;
                }
            }
        break;
        case EXT_LDO3:
        case EXT_LDO4:
        case EXT_LDO5:
        case EXT_LDO6:
        case EXT_LDO7:
            if(ldo_id == 0 || which_ldo_chip[ldo_id].ldoId == CAMERA_LDO_WL2868C) {//WL2868C
                if(value < 1504)
                {
                    WL2868C_PRINT("[wl2868c] error vol!!!\n");
                    ret = -1;
                    goto out;
                }
                else
                {
                    Ldo_out = (value - 1504)/8;
                }
            } else if (which_ldo_chip[ldo_id].ldoId == CAMERA_LDO_FAN53870) {//FAN53870
                if(value < 1500)
                {
                    WL2868C_PRINT("[wl2868c-FAN53870] error vol!!!\n");
                    ret = -1;
                    goto out;
                }
                else
                {
                    Ldo_out = (value - 1500)/8 + 16;
                }
            }
        break;
        default:
            ret = -1;
            goto out;
        break;
    }

    if(ldo_id == 0 || (which_ldo_chip[ldo_id].ldoId == CAMERA_LDO_WL2868C)) {
        WL2868C_PRINT("[wl2868c] CAMERA_LDO_WL2868C");
        regaddr = ldonum + WL2868C_LDO1_OUT_ADDR;
    } else if (which_ldo_chip[ldo_id].ldoId == CAMERA_LDO_FAN53870) {
        WL2868C_PRINT("[wl2868c] CAMERA_LDO_FAN53870");
        regaddr = ldonum + LDO1_OUT_ADDR;
    }

    WL2868C_PRINT("[wl2868c] ldo_id=%d ldo=%d,value=%d,Ldo_out:%d,regaddr=0x%x\n",ldo_id, ldonum, value, Ldo_out, regaddr);
    wl2868c_i2c_client->addr =  which_ldo_chip[ldo_id].i2c_addr;
    ret = i2c_smbus_write_byte_data(wl2868c_i2c_client, regaddr, Ldo_out);
    if (ret < 0) {
        goto out;
    }
    ret = i2c_smbus_read_byte_data(wl2868c_i2c_client,regaddr);
    if (ret < 0) {
        goto out;
    }
    WL2868C_PRINT("[wl2868c] after write ret=0x%x\n",ret);

    return 0;
out:
    WL2868C_PRINT("[wl2868c] %s error!!!\n",__FUNCTION__);

    return ret;
}
EXPORT_SYMBOL_GPL(wl2868c_set_ldo_value);

int wl2868c_ldo_enable(EXT_SELECT ldonum,unsigned int value)
{
    int ret = 0;

    mutex_lock(&i2c_control_mutex);
    WL2868C_PRINT("%s ldonum:%d set_mv:%d\n", __FUNCTION__, ldonum+1, value);
    if (ldonum >= EXT_LDO1 && ldonum < EXT_MAX) {
        ++which_ldo_chip[ldo_id].enable_count[ldonum];
        WL2868C_PRINT("[wl2868c] %s LDO %d ref count %u", __FUNCTION__, ldonum+1,
                    which_ldo_chip[ldo_id].enable_count[ldonum]);
    }

    ret = wl2868c_set_ldo_value(ldonum, value);
    ret |= wl2868c_set_en_ldo(ldonum, 1);
    if (ret)
    {
        WL2868C_PRINT("[wl2868c] wl2868c_ldo_enable fail!\n");
        goto out;
    }

    mutex_unlock(&i2c_control_mutex);

    return 0;
out:
    WL2868C_PRINT("wl2868c_ldo_enable error!\n");
    if (ldonum >= EXT_LDO1 && ldonum < EXT_MAX) {
        --which_ldo_chip[ldo_id].enable_count[ldonum];
    }
    mutex_unlock(&i2c_control_mutex);

    return ret;
}
EXPORT_SYMBOL_GPL(wl2868c_ldo_enable);

int wl2868c_ldo_disable(EXT_SELECT ldonum,unsigned int value)
{
    int ret = 0;

    mutex_lock(&i2c_control_mutex);
    WL2868C_PRINT("[wl2868c] %s LDO %d ref count %u", __FUNCTION__, ldonum+1,
                    which_ldo_chip[ldo_id].enable_count[ldonum]);

    if ((ldonum >= EXT_LDO1 && ldonum < EXT_MAX) && (which_ldo_chip[ldo_id].enable_count[ldonum] > 0)) {
        --which_ldo_chip[ldo_id].enable_count[ldonum];
        if (which_ldo_chip[ldo_id].enable_count[ldonum] == 0) {
            ret = wl2868c_set_en_ldo(ldonum, 0);
            if (ret < 0) {
                ++which_ldo_chip[ldo_id].enable_count[ldonum];
            }
        }
    }

    mutex_unlock(&i2c_control_mutex);
    return ret;
}
EXPORT_SYMBOL_GPL(wl2868c_ldo_disable);

int fingerprint_ldo_enable(unsigned int ldo_num, unsigned int mv)
{
    int ret = 0;
    switch(ldo_num) {
        case 1:
            ret = wl2868c_ldo_enable(EXT_LDO1, mv);
            break;
        case 2:
            ret = wl2868c_ldo_enable(EXT_LDO2, mv);
            break;
        case 3:
            ret = wl2868c_ldo_enable(EXT_LDO3, mv);
            break;
        case 4:
            ret = wl2868c_ldo_enable(EXT_LDO4, mv);
            break;
        case 5:
            ret = wl2868c_ldo_enable(EXT_LDO5, mv);
            break;
        case 6:
            ret = wl2868c_ldo_enable(EXT_LDO6, mv);
            break;
        case 7:
            ret = wl2868c_ldo_enable(EXT_LDO7, mv);
            break;
        default:
            ret = -EINVAL;
            break;
    }
    WL2868C_PRINT("[wl2868c] %s ,ret = %d\n",__FUNCTION__, ret);
    return ret;
}
EXPORT_SYMBOL_GPL(fingerprint_ldo_enable);

int fingerprint_ldo_disable(unsigned int ldo_num, unsigned int mv)
{
    int ret = 0;
    switch(ldo_num) {
        case 1:
            ret = wl2868c_ldo_disable(EXT_LDO1, mv);
            break;
        case 2:
            ret = wl2868c_ldo_disable(EXT_LDO2, mv);
            break;
        case 3:
            ret = wl2868c_ldo_disable(EXT_LDO3, mv);
            break;
        case 4:
            ret = wl2868c_ldo_disable(EXT_LDO4, mv);
            break;
        case 5:
            ret = wl2868c_ldo_disable(EXT_LDO5, mv);
            break;
        case 6:
            ret = wl2868c_ldo_disable(EXT_LDO6, mv);
            break;
        case 7:
            ret = wl2868c_ldo_disable(EXT_LDO7, mv);
            break;
        default:
            ret = -EINVAL;
            break;
    }
    WL2868C_PRINT("[wl2868c] %s ,ret = %d\n",__FUNCTION__, ret);
    return ret;
}
EXPORT_SYMBOL_GPL(fingerprint_ldo_disable);


/*****************************************************************************
 * Data Structure
 *****************************************************************************/
#ifdef USE_CONTROL_ENGPIO
static const char *wl2868c_state_name[WL2868C_GPIO_STATE_MAX] = {
    "wl2868c_gpio_enp0",
    "wl2868c_gpio_enp1"
};/* DTS state mapping name */
#endif

static const struct of_device_id gpio_of_match[] = {
    { .compatible = "qualcomm,gpio_wl2868c", },
    {},
};

static const struct of_device_id i2c_of_match[] = {
    { .compatible = "qualcomm,i2c_wl2868c", },
    {},
};

static const struct i2c_device_id wl2868c_i2c_id[] = {
    {"WL2868C_I2C", 0},
    {},
};

static struct platform_driver wl2868c_platform_driver = {
    .probe = wl2868c_dts_probe,
    .remove = wl2868c_dts_remove,
    .driver = {
        .name = "WL2868C_DTS",
        .of_match_table = gpio_of_match,
    },
};

static struct i2c_driver wl2868c_i2c_driver = {
/************************************************************
Attention:
Althouh i2c_bus do not use .id_table to match, but it must be defined,
otherwise the probe function will not be executed!
************************************************************/
    .id_table = wl2868c_i2c_id,
    .probe = wl2868c_i2c_probe,
    .remove = wl2868c_i2c_remove,
    .driver = {
        .name = "WL2868C_I2C",
        .of_match_table = i2c_of_match,
    },
};

/*****************************************************************************
 * Function
 *****************************************************************************/
#ifdef USE_CONTROL_ENGPIO
static long wl2868c_set_state(const char *name)
{
    int ret = 0;
    struct pinctrl_state *pState = 0;

    BUG_ON(!wl2868c_pctrl);

    pState = pinctrl_lookup_state(wl2868c_pctrl, name);
    if (IS_ERR(pState)) {
        pr_err("set state '%s' failed\n", name);
        ret = PTR_ERR(pState);
        goto exit;
    }

    /* select state! */
    pinctrl_select_state(wl2868c_pctrl, pState);

exit:
    return ret; /* Good! */
}

void wl2868c_gpio_select_state(WL2868C_GPIO_STATE s)
{
    WL2868C_PRINT("[wl2868c]%s,%d\n",__FUNCTION__,s);

    BUG_ON(!((unsigned int)(s) < (unsigned int)(WL2868C_GPIO_STATE_MAX)));
    wl2868c_set_state(wl2868c_state_name[s]);
}

static long wl2868c_dts_init(struct platform_device *pdev)
{
    int ret = 0;
    struct pinctrl *pctrl;

    /* retrieve */
    pctrl = devm_pinctrl_get(&pdev->dev);
    if (IS_ERR(pctrl)) {
        dev_err(&pdev->dev, "Cannot find disp pinctrl!");
        ret = PTR_ERR(pctrl);
        goto exit;
    }

    wl2868c_pctrl = pctrl;

exit:
    return ret;
}
#endif

static int wl2868c_dts_probe(struct platform_device *pdev)
{
#ifdef USE_CONTROL_ENGPIO
    int ret = 0;

    ret = wl2868c_dts_init(pdev);
    if (ret) {
        WL2868C_PRINT("[wl2868c]wl2868c_dts_probe failed\n");
        return ret;
    }
#endif
    WL2868C_PRINT("[wl2868c] wl2868c_dts_probe success\n");

    return 0;
}

static int wl2868c_dts_remove(struct platform_device *pdev)
{
    platform_driver_unregister(&wl2868c_platform_driver);

    return 0;
}

static int wl2868c_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    int i = 0;
    int chipid = 0;

    if (NULL == client) {
        WL2868C_PRINT("[wl2868c] i2c_client is NULL\n");
        return -1;
    }

    for(i = 0; i < (sizeof(which_ldo_chip) / sizeof(which_ldo_chip[0])); i++) {
        client->addr = which_ldo_chip[i].i2c_addr;
        wl2868c_i2c_client = client;
        //sleep 2ms
        msleep(2);

        chipid = i2c_smbus_read_byte_data(wl2868c_i2c_client, which_ldo_chip[i].chip_addr) & 0xff;
        WL2868C_PRINT("[wl2868c]camera_ldo_i2c_probe addr = 0x%x,chipid:vendorid = 0x%x\n", client->addr, chipid);

        if (chipid == which_ldo_chip[i].ldoId) {
             ldo_id = i;
             is_probe_success = true;
             WL2868C_PRINT("[wl2868c]camera_ldo_i2c_probe, this is %x\n", client->addr, i);
             break;
        }
    }

    client->addr=0x2E;
    mutex_init(&i2c_control_mutex);
    WL2868C_PRINT("[wl2868c]wl2868c_i2c_probe success addr = 0x%x\n", client->addr);
    return 0;
}

static int wl2868c_i2c_remove(struct i2c_client *client)
{
    wl2868c_i2c_client = NULL;
    i2c_unregister_device(client);

    return 0;
}

 static int __init wl2868c_init(void)
{
    if (platform_driver_register(&wl2868c_platform_driver)) {
            WL2868C_PRINT("[wl2868c]Failed to register wl2868c_platform_driver!\n");
            return -1;
    }
    WL2868C_PRINT("begin wl2868c initialization");
    if (i2c_add_driver(&wl2868c_i2c_driver)) {
        WL2868C_PRINT("[wl2868c]Failed to register wl2868c_i2c_driver!\n");
        return -1;
    }

    return 0;
}
module_init(wl2868c_init);

static void __exit wl2868c_exit(void)
{
    platform_driver_unregister(&wl2868c_platform_driver);
    i2c_del_driver(&wl2868c_i2c_driver);
}
module_exit(wl2868c_exit);

MODULE_AUTHOR("xxx");
MODULE_DESCRIPTION("FAN53870-WL2868C CAMERA LDO Driver");
MODULE_LICENSE("GPL v2");
