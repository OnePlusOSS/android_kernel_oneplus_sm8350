/* drivers/misc/cs_press/cs_press_f61.h*/
/************************************************************
 * Copyright 2018 OPLUS Mobile Comm Corp., Ltd.
 * All rights reserved.
 *
 * Description  : driver for chip sea IC
 * History      : ( ID, Date, Author, Description)
 * Data         : 2018/11/03
 ************************************************************/

#ifndef _CS_PRESS_H
#define _CS_PRESS_H

//#include <soc/oplus/device_info.h>

#define I2C_CHECK_SCHEDULE
/*IIC REG*/
#define IIC_EEPROM       0x00
#define IIC_HANDSHAKE    0x01
#define IIC_CMD          0x02
#define IIC_WAKE_UP      0x03
#define IIC_FW_VER       0x10
#define IIC_KEY_STATUS   0xAB
#define IIC_WORK_MODE    0x56
#define IIC_PRESSURE_CFG 0x5B

#define IIC_DEBUG_MODE2             0xFB
#define IIC_DEBUG_READY2            0xFC
#define IIC_DEBUG2_DATA             0xFD

#define IIC_MCU_ID                  0x0B
#define IIC_PROJECT_ID              0x0F
#define IIC_FW_VER                  0x10

#define IIC_RESETCMD    0xf17c

#define CS_FWID_LEN     4         /* firmware image length */
#define CS_FW_LENPOS    0x0c
#define CS_FW_VERPOS    0x08
#define CS_FW_STARTPOS  0x100

#define CS_MCU_ID_LEN               0x8
#define CS_PROJECT_ID_LEN           0x2
#define CS_FW_VER_LEN               0x4

#define IIC_MAX_TRSANFER            5
#define IIC_CHECK_RETRY_TIMES       5

#define DEBUG_CLEAR_MODE            0x00
#define DEBUG_RAW_MODE              0x10
#define DEBUG_DIFF_MODE             0x20
#define DEBUG_WRITE_COEF            0x30
#define DEBUG_READ_COEF             0x31
#define DEBUG_WRITE_THRESHOLD       0x32
#define DEBUG_READ_THRESHOLD        0x33

#define FW_UPDATE_MAX_LEN           (64 * 1024)
#define CHECK_DELAY_TIME            (20000)

#define MAX_DEVICE_VERSION_LENGTH   25
#define MAX_DEVICE_MANU_LENGTH      10
#define MAX_DEVICE_FW_PATH_LENGTH   40

enum report_mode {
    MODE_REPORT_NONE,
    MODE_REPORT_KEY,
    MODE_REPORT_TOUCH,
    MODE_REPORT_POWER,
    MODE_REPORT_HOME,
    MODE_REPORT_BACK,
    MODE_REPORT_MAX,
};

enum scan_mode {
    SCAN_MODE_AUTO,
    SCAN_MODE_100HZ,
    SCAN_MODE_10HZ,
    SCAN_MODE_SLEEP,
    SCAN_MODE_MAX,
};

struct point {
    uint16_t x;
    uint16_t y;
};

struct cs_device {
    int                 irq;                //press irq number
    uint32_t            irq_flag;           //irq trigger flasg
    int                 irq_gpio;           //irq gpio num
    int                 reset_gpio;         //Reset gpio
    int                 wd_gpio;            //watch dog gpio
    int                 test_rst_gpio;      //reset gpio for test
    int                 test_chk_gpio;      //check wd state for test
    struct regulator    *vdd_2v8;           //press power
    struct i2c_client   *client;            //i2c client
    struct device       *dev;               //the device structure
    struct mutex        mutex;              //mutex for control operate flow
    struct mutex        i2c_mutex;          //mutex for control i2c opearation
    struct input_dev    *input_dev;         //input device for report event
    struct miscdevice   cs_misc;            //misc device
    int                 report_mode;        //shows which event should report
    struct point        current_point;      //point this key want to report
    #ifdef I2C_CHECK_SCHEDULE
    struct delayed_work i2c_check_worker;
    #endif
    bool                in_probe;
    bool                in_i2c_check;
    bool                fw_update_app_support;

    //struct manufacture_info manufacture_info;       /*touchpanel device info*/
    struct firmware *firmware_in_dts;
};

#endif/*_CS_PRESS_H*/
