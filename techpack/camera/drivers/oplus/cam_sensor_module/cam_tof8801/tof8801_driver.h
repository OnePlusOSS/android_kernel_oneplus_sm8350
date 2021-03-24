/*
*****************************************************************************
* Copyright by ams AG                                                       *
* All rights are reserved.                                                  *
*                                                                           *
* IMPORTANT - PLEASE READ CAREFULLY BEFORE COPYING, INSTALLING OR USING     *
* THE SOFTWARE.                                                             *
*                                                                           *
* THIS SOFTWARE IS PROVIDED FOR USE ONLY IN CONJUNCTION WITH AMS PRODUCTS.  *
* USE OF THE SOFTWARE IN CONJUNCTION WITH NON-AMS-PRODUCTS IS EXPLICITLY    *
* EXCLUDED.                                                                 *
*                                                                           *
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS       *
* "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT         *
* LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS         *
* FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT  *
* OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,     *
* SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT          *
* LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,     *
* DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY     *
* THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT       *
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE     *
* OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.      *
*****************************************************************************
*/

/***** tof8801_driver.h *****/

#ifndef __TOF8801_DRIVER_H
#define __TOF8801_DRIVER_H

#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/miscdevice.h>
#include <linux/irq.h>
#include <linux/device.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/kfifo.h>
#include <linux/semaphore.h>
#include <linux/jiffies.h>
#include <linux/timer.h>
#include <linux/time.h>
#include <linux/kthread.h>
#include <linux/types.h>
#include "tof8801.h"
#include "tof8801_bootloader.h"
#include "tof8801_app0.h"

#define MAX_REGS 256
#define TOF_GPIO_INT_NAME           "irq"
#define TOF_GPIO_ENABLE_NAME        "enable"
#define TOF_PROP_NAME_POLLIO        "tof,tof_poll_period"
#define TOF_FWDL_TIMEOUT_MSEC       30000
#define TOF_XTALK_PEAK_COUNT        5

#define TOF_DEFUALT_ITERATIONS      900000
#define TOF_DEFUALT_PERIOD          33

//Optimize the electrical logic
enum tof_power_state {
        TOF_POWER_ON,
        TOF_POWER_OFF,
};

enum irq_thread_state {
        TOF_IRQ_THREAD_START,
        TOF_IRQ_THREAD_STOP,
};

enum power_down_thread_state {
        TOF_POWER_THREAD_START,
        TOF_POWER_THREAD_STOP,
};

struct tof8801_platform_data {
  const char *tof_name;
  struct gpio_desc *gpiod_interrupt;
  struct gpio_desc *gpiod_enable;
  struct i2c_client *client;
  const char *fac_calib_data_fname;
  const char *config_calib_data_fname;
  const char *ram_patch_fname[];
};

typedef STRUCT_KFIFO_REC_2(PAGE_SIZE) tof_rec_2_fifo; /* VARIABLE SIZE fifo */

union tof8801_info_record {
  struct record{
    char app_id;
    char app_ver;
    char req_app_id;
    char reserved_1;
    char reserved_2;
    char reserved_3;
    char reserved_4;
    char reserved_5;
  }record;
  char data[sizeof(struct record)];
};

struct tof_sensor_chip {
  struct mutex lock;
  int poll_period;
  int driver_debug;
  int saved_clk_trim;
  struct timer_list meas_timer;
  union tof8801_info_record info_rec;
  tof_rec_2_fifo tof_output_fifo;
  struct input_dev *obj_input_dev;
  struct completion ram_patch_in_progress;
  struct firmware *tof_fw;
  struct tof8801_alg_state_info      alg_info;
  struct tof8801_factory_calib_data  ext_calib_data;
  struct tof8801_configuration_data  config_data;
  struct tof8801_BL_application      BL_app;
  struct tof8801_app0_application    app0_app;
  struct tof8801_platform_data *pdata;
  u8 shadow[MAX_REGS];
  struct i2c_client *client;
  struct input_dev *idev;
  struct task_struct *app0_poll_irq;
  unsigned int xtalk_peak;
  unsigned int xtalk_count;
  struct regulator *vdd;
  struct task_struct *downloader_fw;
  struct mutex oem_lock;
  int    distance;
  int    fuse_clk_trim;
  //Optimize the electrical logic
  struct mutex power_lock;
  struct mutex irq_lock;
  enum tof_power_state   power_status;
  enum irq_thread_state  irq_thread_status;
  enum power_down_thread_state   tof_power_down_thread_exit;
};

extern int tof_i2c_read(struct i2c_client *client, char reg, char *buf, int len);
extern int tof_i2c_write(struct i2c_client *client, char reg, const char *buf, int len);
extern int tof_i2c_write_mask(struct i2c_client *client, char reg, const char *val, char mask);
extern int tof8801_get_register(struct i2c_client *client, char reg, char *value);
extern int tof8801_set_register(struct i2c_client *client, char reg, const char value);
extern int tof8801_set_register_mask(struct i2c_client *client, char reg, const char value, const char mask);
extern int tof_wait_for_cpu_ready(struct i2c_client *client);
extern int tof_wait_for_cpu_startup(struct i2c_client *client);
extern int tof_queue_frame(struct tof_sensor_chip *chip, void *buf, int size);
extern int tof_init_info_record(struct tof_sensor_chip *);
extern int tof_hard_reset(struct tof_sensor_chip *chip);
extern int tof_wait_for_cpu_ready_timeout(struct i2c_client *client, unsigned long usec);
extern void tof_dump_i2c_regs(struct tof_sensor_chip * chip, char offset, char end);
extern int wait_for_tof_ready(void);
extern int tof_stop(void);
extern int tof_oem_start(void);

int  cam_tof8801_driver_init(void);
void cam_tof8801_driver_exit(void);


#endif /* __TOF8801_DRIVER_H */
