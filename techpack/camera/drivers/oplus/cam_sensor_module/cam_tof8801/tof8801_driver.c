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
/*!
 *  \file tof8801_driver.c - ToF8801 driver
 *  \brief Device driver for measuring Proximity / Distance in mm
 *  from within the AMS-TAOS TMF8801 family of devices.
 */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/interrupt.h>
#include <linux/firmware.h>
#include <linux/irq.h>
#include <linux/device.h>
#include <linux/sysfs.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/miscdevice.h>
#include <linux/platform_device.h>
#include <linux/gpio/consumer.h>
#include <linux/kfifo.h>
#include <linux/input.h>
#include <linux/jiffies.h>
#include <linux/uaccess.h>
#include "tof8801.h"
#include "tof8801_driver.h"
#include "tof_hex_interpreter.h"
#include "tof8801_bootloader.h"
#include "tof8801_app0.h"
#include "tof8801_pdrv.h"
#include <uapi/linux/sched/types.h>
#include <asm/arch_timer.h>


#define tof_debug_info(fmt,...)		pr_err("ams-tof [INFO]  " fmt"\n", ##__VA_ARGS__);
#define tof_debug_error(fmt,...)	 pr_err("ams-tof [ERROR] " fmt"\n", ##__VA_ARGS__);
#define tof_debug_warning(fmt,...)  pr_warn("ams-tof [WARN]  " fmt"\n", ##__VA_ARGS__);

//#define AMS_MUTEX_DEBUG
#ifdef AMS_MUTEX_DEBUG
#define AMS_MUTEX_LOCK(m) { \
    pr_info("%s: Mutex Lock\n", __func__); \
    mutex_lock_interruptible(m); \
  }
#define AMS_MUTEX_UNLOCK(m) { \
    pr_info("%s: Mutex Unlock\n", __func__); \
    mutex_unlock(m); \
  }
#else
#define AMS_MUTEX_LOCK(m) { \
    mutex_lock(m); \
  }
#define AMS_MUTEX_UNLOCK(m) { \
    mutex_unlock(m); \
  }
#endif

//#define AMS_OEM_MUTEX_DEBUG
#ifdef AMS_OEM_MUTEX_DEBUG
#define AMS_MUTEX_OEM_LOCK(m) { \
    pr_info("%s: OEMMutex Lock\n", __func__); \
    mutex_lock_interruptible(m); \
  }
#define AMS_MUTEX_OEM_UNLOCK(m) { \
    pr_info("%s: OEMMutex Unlock\n", __func__); \
    mutex_unlock(m); \
  }
#else
#define AMS_MUTEX_OEM_LOCK(m) { \
    mutex_lock(m); \
  }
#define AMS_MUTEX_OEM_UNLOCK(m) { \
    mutex_unlock(m); \
  }
#endif

/* This is the salt used for decryption on an encrypted sensor */
static char tof_salt_value = TOF8801_BL_DEFAULT_SALT;

static const unsigned long tof_irq_flags[] = {
	IRQ_TYPE_EDGE_RISING,
	IRQ_TYPE_EDGE_FALLING,
	IRQ_TYPE_LEVEL_LOW,
	IRQ_TYPE_LEVEL_HIGH,
};

static struct tof8801_platform_data tof_pdata = {
	.tof_name = "tof8801",
	.fac_calib_data_fname = "tof8801_fac_calib.bin",
	.config_calib_data_fname = "tof8801_config_calib.bin",
	.ram_patch_fname = {
		"tof8801_firmware.bin",
		"tof8801_firmware-1.bin",
		"tof8801_firmware-2.bin",
		NULL,
	},
};

static struct tof_sensor_chip *g_tof_sensor_chip = NULL;
static struct kobject *cam_tof_kobj;
static bool is_alread_probe = 0;
bool g_is_alread_runing = 0;             // tof have start
bool g_is_download_fw = 0;               // fw have download
bool dump_tof_registers = FALSE;



/*
 *
 * Function Declarations
 *
 */
static int tof8801_get_all_regs(struct tof_sensor_chip *tof_chip);
static void tof_ram_patch_callback(const struct firmware *cfg, void *ctx);
static int tof_switch_apps(struct tof_sensor_chip *chip, char req_app_id);
static int tof8801_get_config_calib_data(struct tof_sensor_chip *chip);
static int tof8801_firmware_download(struct tof_sensor_chip *chip, int startup);
static irqreturn_t tof_irq_handler(int irq, void *dev_id);
static int tof8801_enable_interrupts(struct tof_sensor_chip *chip,
				     char int_en_flags);
static int tof8801_app0_poll_irq_thread(void *tof_chip);
static int start_poll_thread(void);
static int tof_power_down_thread(void *arg);

int tof_power_down_thread(void *arg)
{
	int rc = 0;
	struct tof_sensor_chip *chip = g_tof_sensor_chip;

	tof_debug_info("stop irq thread start");
	AMS_MUTEX_OEM_LOCK(&chip->irq_lock);

	if (chip->poll_period != 0 && chip->irq_thread_status == TOF_IRQ_THREAD_START)
		(void)kthread_stop(chip->app0_poll_irq);

	chip->irq_thread_status = TOF_IRQ_THREAD_STOP;
	AMS_MUTEX_OEM_UNLOCK(&chip->irq_lock);
	tof_debug_info("stop irq threadd done");

	AMS_MUTEX_OEM_LOCK(&chip->power_lock);

	if (chip->power_status == TOF_POWER_ON
			&& chip->tof_power_down_thread_exit == TOF_POWER_THREAD_START) {
		rc = tof_power_down();

		if (rc != 0) {
			tof_debug_error("power down failed");
			chip->tof_power_down_thread_exit = TOF_POWER_THREAD_STOP;
			AMS_MUTEX_OEM_UNLOCK(&chip->power_lock);
			return rc;
		}

		tof_debug_info("power down success");
		chip->power_status = TOF_POWER_OFF;

	} else
		tof_debug_info("donot need do power down,power status=%d,thread_exit=%d",
			       chip->power_status, chip->tof_power_down_thread_exit);

	chip->tof_power_down_thread_exit = TOF_POWER_THREAD_STOP;
	AMS_MUTEX_OEM_UNLOCK(&chip->power_lock);
	return 0;
}

struct tof8801_pinctrl_info {
	struct pinctrl *pinctrl;
	struct pinctrl_state *gpio_state_active;
	struct pinctrl_state *gpio_state_suspend;
};
extern uint32_t saved_clkratioQ15[3];


static struct tof8801_pinctrl_info tof8801_pinctrl;

static int tof8801_request_pinctrl(struct device *dev)
{
	struct tof8801_pinctrl_info *device_pctrl = &tof8801_pinctrl;
	device_pctrl->pinctrl = devm_pinctrl_get(dev);

	if (IS_ERR_OR_NULL(device_pctrl->pinctrl)) {
		tof_debug_error("Pinctrl not available");
		device_pctrl->pinctrl = NULL;
		return 0;
	}

	device_pctrl->gpio_state_active =
		pinctrl_lookup_state(device_pctrl->pinctrl,
				     "laser_default");

	if (IS_ERR_OR_NULL(device_pctrl->gpio_state_active)) {
		tof_debug_error("Failed to get the active state pinctrl handle");
		device_pctrl->gpio_state_active = NULL;
		return -EINVAL;
	}

	device_pctrl->gpio_state_suspend
		= pinctrl_lookup_state(device_pctrl->pinctrl,
				       "laser_suspend");

	if (IS_ERR_OR_NULL(device_pctrl->gpio_state_suspend)) {
		tof_debug_error("Failed to get the suspend state pinctrl handle");
		device_pctrl->gpio_state_suspend = NULL;
		return -EINVAL;
	}

	return 0;
}

static int tof8801_enable_pinctrl(struct device *dev)
{
	int rc = 0;

	if (tof8801_pinctrl.pinctrl &&
			tof8801_pinctrl.gpio_state_active) {
		rc = pinctrl_select_state(tof8801_pinctrl.pinctrl,
					  tof8801_pinctrl.gpio_state_active);
		//tof_debug_warning("enable pinctrl rc=%d\n", rc);
	}

	return rc;
}
/*
int tof8801_disable_pinctrl(struct device *dev)
{
    int rc = 0;

    if (tof8801_pinctrl.pinctrl &&
        tof8801_pinctrl.gpio_state_suspend) {
        rc = pinctrl_select_state(tof8801_pinctrl.pinctrl,
            tof8801_pinctrl.gpio_state_suspend);
        tof_debug_info("disable pinctrl rc=%d\n", rc);
    }

    return rc;

}

int tof8801_release_pinctrl(struct device *dev)
{
    if (tof8801_pinctrl.pinctrl)
        devm_pinctrl_put(tof8801_pinctrl.pinctrl);
    tof8801_pinctrl.pinctrl = NULL;

    return 0;
}
*/

#define OPLUS_ATTR(_name, _mode, _show, _store) \
    struct kobj_attribute oplus_attr_##_name = __ATTR(_name, _mode, _show, _store)


/*
 *
 * Function Definitions
 *
 */
static ssize_t program_show(struct kobject *kobj,
			    struct kobj_attribute *attr,
			    char *buf)
{
	struct tof_sensor_chip *chip = g_tof_sensor_chip;

	tof_debug_info("%s\n", __func__);

	return scnprintf(buf, PAGE_SIZE, "%#x\n", (chip->info_rec.record.app_id));
}

static ssize_t program_store(struct kobject *kobj,
			     struct kobj_attribute *attr,
			     const char *buf,
			     size_t count)
{
	struct tof_sensor_chip *chip = g_tof_sensor_chip;
	char req_app_id;
	int error;

	sscanf(buf, "%hhx", &req_app_id);
	tof_debug_info("%s: requested app: %#x\n", __func__, req_app_id);
	AMS_MUTEX_LOCK(&chip->lock);
	error = tof_switch_apps(chip, req_app_id);

	if (error) {
		tof_debug_info("Error switching app: %d\n", error);
		AMS_MUTEX_UNLOCK(&chip->lock);
		return error;
	}

	AMS_MUTEX_UNLOCK(&chip->lock);
	return count;
}

static ssize_t chip_enable_show(struct kobject *kobj,
				struct kobj_attribute *attr,
				char *buf)
{

	struct tof_sensor_chip *chip = g_tof_sensor_chip;
	int state;

	if (g_tof_sensor_chip == NULL) {
		pr_err("ams,tof chip_enable_show failed ,g_tof_sensor_chip is NULL");
		return 0;
	}

// dev = g_tof_sensor_chip->client->dev;
// tof_debug_info( "%s\n", __func__);
	AMS_MUTEX_LOCK(&chip->lock);

	if (!chip->pdata->gpiod_enable) {
		AMS_MUTEX_UNLOCK(&chip->lock);
		return -EIO;
	}

	state = gpiod_get_value(chip->pdata->gpiod_enable) ? 1 : 0;
	AMS_MUTEX_UNLOCK(&chip->lock);
	return scnprintf(buf, PAGE_SIZE, "%d\n", state);

	return 0 ;
}

static ssize_t chip_enable_store(struct kobject *kobj,
				 struct kobj_attribute *attr,
				 const char *buf,
				 size_t count)
{
	int rc = -1;
	unsigned int capture;
	struct tof_sensor_chip *chip = g_tof_sensor_chip;
	tof_debug_info("%s: %s", __func__, buf);

	if (sscanf(buf, "%u", &capture) != 1)
		return -1;

	if (capture) {
		AMS_MUTEX_LOCK(&chip->lock)

		if (g_is_alread_runing) {
			AMS_MUTEX_UNLOCK(&chip->lock);
			return 0;
		}

		g_is_alread_runing = 1;
		AMS_MUTEX_UNLOCK(&chip->lock);
		rc = tof_oem_start();

		if (rc != 0)
			tof_debug_info("start tof failed");

	} else {
		AMS_MUTEX_LOCK(&chip->lock)

		if (g_is_alread_runing == 0) {
			AMS_MUTEX_UNLOCK(&chip->lock);
			return 0;
		}

		g_is_alread_runing = 0;
		AMS_MUTEX_UNLOCK(&chip->lock);
		rc = tof_stop();

		if (rc != 0)
			tof_debug_info("stop tof failed");
	}

	return rc ? -1 : count;
	/*
	  struct tof_sensor_chip *chip = g_tof_sensor_chip;
	  int req_state;
	  int error;
	  tof_debug_info( "%s\n", __func__);
	  error = sscanf(buf, "%d", &req_state);
	  if (error != 1)
	    return -1;
	  AMS_MUTEX_LOCK(&chip->lock);
	  if (!chip->pdata->gpiod_enable) {
	    AMS_MUTEX_UNLOCK(&chip->lock);
	    return -EIO;
	  }
	  if (req_state == 0) {
	    if (chip->info_rec.record.app_id == TOF8801_APP_ID_APP0) {
	      (void)tof8801_app0_capture(chip, 0);
	    }
	    gpiod_set_value(chip->pdata->gpiod_enable, 0);
	    g_is_download_fw = 0;
	  } else {
	    error = tof_hard_reset(chip);
	    if (error) {
	      tof_debug_error( "Error issuing Reset-HARD");
	      AMS_MUTEX_UNLOCK(&chip->lock);
	      return -EIO;
	    }
	  }
	  AMS_MUTEX_UNLOCK(&chip->lock);
	  return count;
	*/
}

static ssize_t driver_debug_show(struct kobject *kobj,
				 struct kobj_attribute *attr,
				 char *buf)
{
	struct tof_sensor_chip *chip = g_tof_sensor_chip;
	tof_debug_info("%s\n", __func__);
	return scnprintf(buf, PAGE_SIZE, "%d\n", chip->driver_debug);
}

static ssize_t driver_debug_store(struct kobject *kobj,
				  struct kobj_attribute *attr,
				  const char *buf,
				  size_t count)
{
	struct tof_sensor_chip *chip = g_tof_sensor_chip;
	int debug;
	tof_debug_info("%s\n", __func__);
	sscanf(buf, "%d", &debug);

	if (debug == 0) {
		chip->driver_debug = 0;
		dump_tof_registers = 0;

	} else {
		chip->driver_debug = 1;
		dump_tof_registers = 1;
	}

	return count;
}

static ssize_t app0_command_show(struct kobject *kobj,
				 struct kobj_attribute *attr,
				 char *buf)
{
	int i;
	unsigned int len = 0;
	struct tof_sensor_chip *chip = g_tof_sensor_chip;
	char *cmd_byte = chip->app0_app.user_cmd.anon_cmd.buf;
	tof_debug_info("%s\n", __func__);
	AMS_MUTEX_LOCK(&chip->lock);

	if (chip->info_rec.record.app_id != TOF8801_APP_ID_APP0) {
		tof_debug_error("%s: Error ToF chip app_id: %#x",
				__func__, chip->info_rec.record.app_id);
		AMS_MUTEX_UNLOCK(&chip->lock);
		return -1;
	}

	for (i = TOF8801_APP0_CMD_IDX; i >= 0; i--)
		len += scnprintf(buf - len, PAGE_SIZE - len, "%#x ", cmd_byte[i]);

	len += scnprintf(buf - len, PAGE_SIZE - len, "\n");
	AMS_MUTEX_UNLOCK(&chip->lock);
	return len;
}

static ssize_t app0_command_store(struct kobject *kobj,
				  struct kobj_attribute *attr,
				  const char *buf,
				  size_t count)
{
	int num = 0;
	int i;
	struct tof_sensor_chip *chip = g_tof_sensor_chip;
	char *cmd_byte;
	char *sub_string = NULL;
	int error;

	if (chip->driver_debug)
		tof_debug_info("%s: %s", __func__, buf);

	AMS_MUTEX_LOCK(&chip->lock);

	if (chip->info_rec.record.app_id != TOF8801_APP_ID_APP0) {
		tof_debug_error("%s: Error ToF chip app_id: %#x",
				__func__, chip->info_rec.record.app_id);
		AMS_MUTEX_UNLOCK(&chip->lock);
		return -1;
	}

	cmd_byte = chip->app0_app.user_cmd.anon_cmd.buf;
	memset(cmd_byte, 0, TOF8801_APP0_MAX_CMD_SIZE); //clear previous command

	for (i = TOF8801_APP0_CMD_IDX; (i >= 0); i--) {
		sub_string = strsep((char **)&buf, " ");

		if (sub_string) {
			num = sscanf(sub_string, "%hhx", (cmd_byte + i));

			if (num == 0)
				break;
		}
	}

	error = tof8801_app0_issue_cmd(chip);
	AMS_MUTEX_UNLOCK(&chip->lock);
	return error ? -1 : count;
}

static ssize_t capture_show(struct kobject *kobj,
			    struct kobj_attribute *attr,
			    char *buf)
{
	struct tof_sensor_chip *chip = g_tof_sensor_chip;
	int len = 0;
	tof_debug_info("%s\n", __func__);
	AMS_MUTEX_LOCK(&chip->lock);

	if (chip->info_rec.record.app_id != TOF8801_APP_ID_APP0) {
		tof_debug_error("%s: Error ToF chip app_id: %#x",
				__func__, chip->info_rec.record.app_id);
		AMS_MUTEX_UNLOCK(&chip->lock);
		return -1;
	}

	len += scnprintf(buf, PAGE_SIZE, "%u\n", chip->app0_app.cap_settings.cmd);
	AMS_MUTEX_UNLOCK(&chip->lock);
	return len;
}

static ssize_t capture_store(struct kobject *kobj,
			     struct kobj_attribute *attr,
			     const char *buf,
			     size_t count)
{
	struct tof_sensor_chip *chip = g_tof_sensor_chip;
	int error = 0;
	unsigned int capture;
	tof_debug_info("%s: %s", __func__, buf);
	AMS_MUTEX_LOCK(&chip->lock);

	if (chip->info_rec.record.app_id != TOF8801_APP_ID_APP0) {
		tof_debug_error("%s: Error ToF chip app_id: %#x",
				__func__, chip->info_rec.record.app_id);
		AMS_MUTEX_UNLOCK(&chip->lock);
		return -1;
	}

	if (sscanf(buf, "%u", &capture) != 1) {
		AMS_MUTEX_UNLOCK(&chip->lock);
		return -1;
	}

	if (capture) {
		if (chip->app0_app.cap_settings.cmd == 0) {
			chip->xtalk_peak  = 0;
			chip->xtalk_count = 0;
			error = tof8801_app0_capture((void *)chip, capture);

		} else {
			AMS_MUTEX_UNLOCK(&chip->lock);
			return -EBUSY;
		}

	} else
		tof8801_app0_capture(chip, 0);

	AMS_MUTEX_UNLOCK(&chip->lock);

	return error ? -1 : count;
}

static ssize_t app0_temp_show(struct kobject *kobj,
			      struct kobj_attribute *attr,
			      char *buf)
{
	unsigned int len = 0;
	struct tof_sensor_chip *chip = g_tof_sensor_chip;
	tof_debug_info("%s\n", __func__);
	AMS_MUTEX_LOCK(&chip->lock);

	if (chip->info_rec.record.app_id != TOF8801_APP_ID_APP0) {
		tof_debug_error("%s: Error ToF chip app_id: %#x",
				__func__, chip->info_rec.record.app_id);
		AMS_MUTEX_UNLOCK(&chip->lock);
		return -1;
	}

	len += scnprintf(buf, PAGE_SIZE, "%d\n", chip->app0_app.last_known_temp);
	AMS_MUTEX_UNLOCK(&chip->lock);
	return len;
}

static ssize_t period_show(struct kobject *kobj,
			   struct kobj_attribute *attr,
			   char *buf)
{
	unsigned int len = 0;
	struct tof_sensor_chip *chip = g_tof_sensor_chip;
	tof_debug_info("%s\n", __func__);
	AMS_MUTEX_LOCK(&chip->lock);

	if (chip->info_rec.record.app_id != TOF8801_APP_ID_APP0) {
		tof_debug_error("%s: Error ToF chip app_id: %#x",
				__func__, chip->info_rec.record.app_id);
		AMS_MUTEX_UNLOCK(&chip->lock);
		return -1;
	}

	len += scnprintf(buf, PAGE_SIZE, "%d\n", chip->app0_app.cap_settings.period);
	AMS_MUTEX_UNLOCK(&chip->lock);
	return len;
}

static ssize_t period_store(struct kobject *kobj,
			    struct kobj_attribute *attr,
			    const char *buf,
			    size_t count)
{
	struct tof_sensor_chip *chip = g_tof_sensor_chip;
	int error = 0;
	unsigned int value = 0;
	tof_debug_info("%s: %s", __func__, buf);
	AMS_MUTEX_LOCK(&chip->lock);

	if (chip->info_rec.record.app_id != TOF8801_APP_ID_APP0) {
		tof_debug_error("%s: Error ToF chip app_id: %#x",
				__func__, chip->info_rec.record.app_id);
		AMS_MUTEX_UNLOCK(&chip->lock);
		return -1;
	}

	if (sscanf(buf, "%u", &value) != 1) {
		AMS_MUTEX_UNLOCK(&chip->lock);
		return -1;
	}

	chip->app0_app.cap_settings.period = (value > 0xFF) ? 0xFF : value;

	if (chip->app0_app.cap_settings.cmd != 0) {
		(void)tof8801_app0_capture((void *)chip, 0);
		error = tof8801_app0_capture((void *)chip, 1);
	}

	AMS_MUTEX_UNLOCK(&chip->lock);
	return error ? -1 : count;
}

static ssize_t iterations_show(struct kobject *kobj,
			       struct kobj_attribute *attr,
			       char *buf)
{
	unsigned int len = 0;
	unsigned int iterations = 0;
	struct tof_sensor_chip *chip = g_tof_sensor_chip;
	tof_debug_info("%s\n", __func__);
	AMS_MUTEX_LOCK(&chip->lock);

	if (chip->info_rec.record.app_id != TOF8801_APP_ID_APP0) {
		tof_debug_error("%s: Error ToF chip app_id: %#x",
				__func__, chip->info_rec.record.app_id);
		AMS_MUTEX_UNLOCK(&chip->lock);
		return -1;
	}

	iterations = 1000 * le16_to_cpup((const __le16 *)
					 chip->app0_app.cap_settings.iterations);
	len += scnprintf(buf, PAGE_SIZE, "%u\n", iterations);
	AMS_MUTEX_UNLOCK(&chip->lock);
	return len;
}

static ssize_t iterations_store(struct kobject *kobj,
				struct kobj_attribute *attr,
				const char *buf,
				size_t count)
{
	struct tof_sensor_chip *chip = g_tof_sensor_chip;
	int error = 0;
	unsigned int value = 0;
	tof_debug_info("%s: %s", __func__, buf);
	AMS_MUTEX_LOCK(&chip->lock);

	if (chip->info_rec.record.app_id != TOF8801_APP_ID_APP0) {
		tof_debug_error("%s: Error ToF chip app_id: %#x",
				__func__, chip->info_rec.record.app_id);
		AMS_MUTEX_UNLOCK(&chip->lock);
		return -1;
	}

	if (sscanf(buf, "%u", &value) != 1) {
		AMS_MUTEX_UNLOCK(&chip->lock);
		return -1;
	}

	// we need to appropriately change the clock iteration counter
	//  when the capture iterations are changed to keep the time acceptable
	tof8801_app0_set_clk_iterations(chip, value);
	// chip takes iterations in 1000s
	value /= 1000;
	*((__le16 *)chip->app0_app.cap_settings.iterations) = cpu_to_le16(value);

	if (chip->app0_app.cap_settings.cmd != 0) {
		(void)tof8801_app0_capture((void *)chip, 0);
		error = tof8801_app0_capture((void *)chip, 1);
	}

	AMS_MUTEX_UNLOCK(&chip->lock);
	return error ? -1 : count;
}

static ssize_t noise_threshold_show(struct kobject *kobj,
				    struct kobj_attribute *attr,
				    char *buf)
{
	unsigned int len = 0;
	struct tof_sensor_chip *chip = g_tof_sensor_chip;
	tof_debug_info("%s\n", __func__);
	AMS_MUTEX_LOCK(&chip->lock);

	if (chip->info_rec.record.app_id != TOF8801_APP_ID_APP0) {
		tof_debug_error("%s: Error ToF chip app_id: %#x",
				__func__, chip->info_rec.record.app_id);
		AMS_MUTEX_UNLOCK(&chip->lock);
		return -1;
	}

	len += scnprintf(buf, PAGE_SIZE, "%d\n",
			 chip->app0_app.cap_settings.noise_thrshld);
	AMS_MUTEX_UNLOCK(&chip->lock);
	return len;
}

static ssize_t noise_threshold_store(struct kobject *kobj,
				     struct kobj_attribute *attr,
				     const char *buf,
				     size_t count)
{
	struct tof_sensor_chip *chip = g_tof_sensor_chip;
	int error = 0;
	tof_debug_info("%s: %s", __func__, buf);
	AMS_MUTEX_LOCK(&chip->lock);

	if (chip->info_rec.record.app_id != TOF8801_APP_ID_APP0) {
		tof_debug_error("%s: Error ToF chip app_id: %#x",
				__func__, chip->info_rec.record.app_id);
		AMS_MUTEX_UNLOCK(&chip->lock);
		return -1;
	}

	if (sscanf(buf, "%hhd", &chip->app0_app.cap_settings.noise_thrshld) != 1) {
		AMS_MUTEX_UNLOCK(&chip->lock);
		return -1;
	}

	if (chip->app0_app.cap_settings.cmd != 0) {
		(void)tof8801_app0_capture((void *)chip, 0);
		error = tof8801_app0_capture((void *)chip, 1);
	}

	AMS_MUTEX_UNLOCK(&chip->lock);
	return error ? -1 : count;
}

static ssize_t capture_delay_show(struct kobject *kobj,
				  struct kobj_attribute *attr,
				  char *buf)
{
	unsigned int len = 0;
	struct tof_sensor_chip *chip = g_tof_sensor_chip;
	tof_debug_info("%s\n", __func__);
	AMS_MUTEX_LOCK(&chip->lock);

	if (chip->info_rec.record.app_id != TOF8801_APP_ID_APP0) {
		tof_debug_error("%s: Error ToF chip app_id: %#x",
				__func__, chip->info_rec.record.app_id);
		AMS_MUTEX_UNLOCK(&chip->lock);
		return -1;
	}

	len += scnprintf(buf, PAGE_SIZE, "%d\n", chip->app0_app.cap_settings.delay);
	AMS_MUTEX_UNLOCK(&chip->lock);
	return len;
}

static ssize_t capture_delay_store(struct kobject *kobj,
				   struct kobj_attribute *attr,
				   const char *buf,
				   size_t count)
{
	struct tof_sensor_chip *chip = g_tof_sensor_chip;
	int error = 0;
	unsigned int value = 0;
	tof_debug_info("%s: %s", __func__, buf);
	AMS_MUTEX_LOCK(&chip->lock);

	if (chip->info_rec.record.app_id != TOF8801_APP_ID_APP0) {
		tof_debug_error("%s: Error ToF chip app_id: %#x",
				__func__, chip->info_rec.record.app_id);
		AMS_MUTEX_UNLOCK(&chip->lock);
		return -1;
	}

	if (sscanf(buf, "%u", &value) != 1) {
		AMS_MUTEX_UNLOCK(&chip->lock);
		return -1;
	}

	chip->app0_app.cap_settings.delay = (value > 0xFF) ? 0xFF : value;

	if (chip->app0_app.cap_settings.cmd != 0) {
		(void)tof8801_app0_capture((void *)chip, 0);
		error = tof8801_app0_capture((void *)chip, 1);
	}

	AMS_MUTEX_UNLOCK(&chip->lock);
	return error ? -1 : count;
}

static ssize_t alg_setting_show(struct kobject *kobj,
				struct kobj_attribute *attr,
				char *buf)
{
	unsigned int len = 0;
	struct tof_sensor_chip *chip = g_tof_sensor_chip;
	tof_debug_info("%s\n", __func__);
	AMS_MUTEX_LOCK(&chip->lock);

	if (chip->info_rec.record.app_id != TOF8801_APP_ID_APP0) {
		tof_debug_error("%s: Error ToF chip app_id: %#x",
				__func__, chip->info_rec.record.app_id);
		AMS_MUTEX_UNLOCK(&chip->lock);
		return -1;
	}

	if (!tof8801_app0_is_v2(chip)) {
		tof_debug_error("%s: Error alg setting not supported in revision: %#x",
				__func__, chip->info_rec.record.app_ver);
		AMS_MUTEX_UNLOCK(&chip->lock);
		return -1;
	}

	len += scnprintf(buf, PAGE_SIZE, "%x\n", chip->app0_app.cap_settings.v2.alg);
	AMS_MUTEX_UNLOCK(&chip->lock);
	return len;
}

static ssize_t alg_setting_store(struct kobject *kobj,
				 struct kobj_attribute *attr,
				 const char *buf,
				 size_t count)
{
	struct tof_sensor_chip *chip = g_tof_sensor_chip;
	int error = 0;
	tof_debug_info("%s: %s", __func__, buf);
	AMS_MUTEX_LOCK(&chip->lock);

	if (chip->info_rec.record.app_id != TOF8801_APP_ID_APP0) {
		tof_debug_error("%s: Error ToF chip app_id: %#x",
				__func__, chip->info_rec.record.app_id);
		AMS_MUTEX_UNLOCK(&chip->lock);
		return -1;
	}

	if (!tof8801_app0_is_v2(chip)) {
		tof_debug_error("%s: Error alg setting not supported in revision: %#x",
				__func__, chip->info_rec.record.app_ver);
		AMS_MUTEX_UNLOCK(&chip->lock);
		return -1;
	}

	if (sscanf(buf, "%hhx", &chip->app0_app.cap_settings.v2.alg) != 1) {
		AMS_MUTEX_UNLOCK(&chip->lock);
		return -1;
	}

	if (chip->app0_app.cap_settings.cmd != 0) {
		(void)tof8801_app0_capture((void *)chip, 0);
		error = tof8801_app0_capture((void *)chip, 1);
	}

	AMS_MUTEX_UNLOCK(&chip->lock);
	return error ? -1 : count;
}

static ssize_t gpio_setting_show(struct kobject *kobj,
				 struct kobj_attribute *attr,
				 char *buf)
{
	unsigned int len = 0;
	struct tof_sensor_chip *chip = g_tof_sensor_chip;
	tof_debug_info("%s\n", __func__);
	AMS_MUTEX_LOCK(&chip->lock);

	if (chip->info_rec.record.app_id != TOF8801_APP_ID_APP0) {
		tof_debug_error("%s: Error ToF chip app_id: %#x",
				__func__, chip->info_rec.record.app_id);
		AMS_MUTEX_UNLOCK(&chip->lock);
		return -1;
	}

	if (!tof8801_app0_is_v2(chip)) {
		tof_debug_error("%s: Error gpio setting not supported in revision: %#x",
				__func__, chip->info_rec.record.app_ver);
		AMS_MUTEX_UNLOCK(&chip->lock);
		return -1;
	}

	len += scnprintf(buf, PAGE_SIZE, "%x\n", chip->app0_app.cap_settings.v2.gpio);
	AMS_MUTEX_UNLOCK(&chip->lock);
	return len;
}

static ssize_t gpio_setting_store(struct kobject *kobj,
				  struct kobj_attribute *attr,
				  const char *buf,
				  size_t count)
{
	struct tof_sensor_chip *chip = g_tof_sensor_chip;
	int error = 0;
	tof_debug_info("%s: %s", __func__, buf);
	AMS_MUTEX_LOCK(&chip->lock);

	if (chip->info_rec.record.app_id != TOF8801_APP_ID_APP0) {
		tof_debug_error("%s: Error ToF chip app_id: %#x",
				__func__, chip->info_rec.record.app_id);
		AMS_MUTEX_UNLOCK(&chip->lock);
		return -1;
	}

	if (!tof8801_app0_is_v2(chip)) {
		tof_debug_error("%s: Error gpio setting not supported in revision: %#x",
				__func__, chip->info_rec.record.app_ver);
		AMS_MUTEX_UNLOCK(&chip->lock);
		return -1;
	}

	if (sscanf(buf, "%hhx", &chip->app0_app.cap_settings.v2.gpio) != 1) {
		AMS_MUTEX_UNLOCK(&chip->lock);
		return -1;
	}

	if (chip->app0_app.cap_settings.cmd != 0) {
		(void)tof8801_app0_capture((void *)chip, 0);
		error = tof8801_app0_capture((void *)chip, 1);
	}

	AMS_MUTEX_UNLOCK(&chip->lock);
	return error ? -1 : count;
}

static ssize_t app0_clk_iterations_show(struct kobject *kobj,
					struct kobj_attribute *attr,
					char *buf)
{
	unsigned int len = 0;
	struct tof_sensor_chip *chip = g_tof_sensor_chip;
	tof_debug_info("%s\n", __func__);
	AMS_MUTEX_LOCK(&chip->lock);

	if (chip->info_rec.record.app_id != TOF8801_APP_ID_APP0) {
		tof_debug_error("%s: Error ToF chip app_id: %#x",
				__func__, chip->info_rec.record.app_id);
		AMS_MUTEX_UNLOCK(&chip->lock);
		return -1;
	}

	if (!tof8801_app0_is_v2(chip)) {
		tof_debug_error("%s: Error clk iterations not supported in revision: %#x",
				__func__, chip->info_rec.record.app_ver);
		AMS_MUTEX_UNLOCK(&chip->lock);
		return -1;
	}

	len += scnprintf(buf, PAGE_SIZE, "%d\n", chip->app0_app.clk_iterations);
	AMS_MUTEX_UNLOCK(&chip->lock);
	return len;
}

static ssize_t app0_clk_iterations_store(struct kobject *kobj,
		struct kobj_attribute *attr,
		const char *buf,
		size_t count)
{
	struct tof_sensor_chip *chip = g_tof_sensor_chip;
	tof_debug_info("%s: %s", __func__, buf);
	AMS_MUTEX_LOCK(&chip->lock);

	if (chip->info_rec.record.app_id != TOF8801_APP_ID_APP0) {
		tof_debug_error("%s: Error ToF chip app_id: %#x",
				__func__, chip->info_rec.record.app_id);
		AMS_MUTEX_UNLOCK(&chip->lock);
		return -1;
	}

	if (!tof8801_app0_is_v2(chip)) {
		tof_debug_error("%s: Error clk iterations not supported in revision: %#x",
				__func__, chip->info_rec.record.app_ver);
		AMS_MUTEX_UNLOCK(&chip->lock);
		return -1;
	}

	if (sscanf(buf, "%u", &chip->app0_app.clk_iterations) != 1) {
		AMS_MUTEX_UNLOCK(&chip->lock);
		return -1;
	}

	AMS_MUTEX_UNLOCK(&chip->lock);
	return count;
}

static ssize_t app0_clk_trim_enable_show(struct kobject *kobj,
		struct kobj_attribute *attr,
		char *buf)
{
	unsigned int len = 0;
	struct tof_sensor_chip *chip = g_tof_sensor_chip;
	tof_debug_info("%s\n", __func__);
	AMS_MUTEX_LOCK(&chip->lock);

	if (chip->info_rec.record.app_id != TOF8801_APP_ID_APP0) {
		tof_debug_error("%s: Error ToF chip app_id: %#x",
				__func__, chip->info_rec.record.app_id);
		AMS_MUTEX_UNLOCK(&chip->lock);
		return -1;
	}

	if (!tof8801_app0_is_v2(chip)) {
		tof_debug_error("%s: Error clk trim not supported in revision: %#x",
				__func__, chip->info_rec.record.app_ver);
		AMS_MUTEX_UNLOCK(&chip->lock);
		return -1;
	}

	len += scnprintf(buf, PAGE_SIZE, "%d\n", chip->app0_app.clk_trim_enable);
	AMS_MUTEX_UNLOCK(&chip->lock);
	return len;
}

static ssize_t app0_clk_trim_enable_store(struct kobject *kobj,
		struct kobj_attribute *attr,
		const char *buf,
		size_t count)
{
	struct tof_sensor_chip *chip = g_tof_sensor_chip;
	tof_debug_info("%s: %s", __func__, buf);
	AMS_MUTEX_LOCK(&chip->lock);

	if (chip->info_rec.record.app_id != TOF8801_APP_ID_APP0) {
		tof_debug_error("%s: Error ToF chip app_id: %#x",
				__func__, chip->info_rec.record.app_id);
		AMS_MUTEX_UNLOCK(&chip->lock);
		return -1;
	}

	if (!tof8801_app0_is_v2(chip)) {
		tof_debug_error("%s: Error clk trim not supported in revision: %#x",
				__func__, chip->info_rec.record.app_ver);
		AMS_MUTEX_UNLOCK(&chip->lock);
		return -1;
	}

	if (sscanf(buf, "%d", &chip->app0_app.clk_trim_enable) != 1) {
		AMS_MUTEX_UNLOCK(&chip->lock);
		return -1;
	}

	AMS_MUTEX_UNLOCK(&chip->lock);
	return count;
}

static ssize_t app0_clk_trim_set_show(struct kobject *kobj,
				      struct kobj_attribute *attr,
				      char *buf)
{
	unsigned int len = 0;
	struct tof_sensor_chip *chip = g_tof_sensor_chip;
	int trim = 0;
	int error = 0;
	tof_debug_info("%s\n", __func__);
	AMS_MUTEX_LOCK(&chip->lock);

	if (chip->info_rec.record.app_id != TOF8801_APP_ID_APP0) {
		tof_debug_error("%s: Error ToF chip app_id: %#x",
				__func__, chip->info_rec.record.app_id);
		AMS_MUTEX_UNLOCK(&chip->lock);
		return -1;
	}

	if (!tof8801_app0_is_v2(chip)) {
		tof_debug_error("%s: Error clk trim not supported in revision: %#x",
				__func__, chip->info_rec.record.app_ver);
		AMS_MUTEX_UNLOCK(&chip->lock);
		return -1;
	}

	error = tof8801_app0_rw_osc_trim(chip, &trim, 0);

	if (error) {
		AMS_MUTEX_UNLOCK(&chip->lock);
		return -1;
	}

	error = tof8801_app0_verify_clock_ratio(chip);

	if (error != 0) {
		tof_debug_error("trim out of range");
		len += scnprintf(buf, PAGE_SIZE, "%d\n", 520);
		AMS_MUTEX_UNLOCK(&chip->lock);
		return -1;
	}

	len += scnprintf(buf, PAGE_SIZE, "%d\n", trim);
	AMS_MUTEX_UNLOCK(&chip->lock);
	return len;
}

static ssize_t app0_clk_trim_set_store(struct kobject *kobj,
				       struct kobj_attribute *attr,
				       const char *buf,
				       size_t count)
{
	struct tof_sensor_chip *chip = g_tof_sensor_chip;
	int trim = 0;
	tof_debug_info("%s: %s", __func__, buf);
	AMS_MUTEX_LOCK(&chip->lock);

	if (sscanf(buf, "%d", &trim) != 1) {
		AMS_MUTEX_UNLOCK(&chip->lock);
		return -1;
	}

	tof_debug_error("%s: trim = 0x%08x\n",   __func__, trim);

	if ((trim > 511) || (trim < 0)) {
		tof_debug_error("%s: Error clk trim setting is out of range [%d,%d]\n",
				__func__, 0, 511);
		AMS_MUTEX_UNLOCK(&chip->lock);
		return -1;
	}

	chip->saved_clk_trim = trim; // cache value even if app0 is not running

	if (chip->info_rec.record.app_id != TOF8801_APP_ID_APP0) {
		tof_debug_error("%s: Caching trim value, ToF chip app_id: %#x",
				__func__, chip->info_rec.record.app_id);
		AMS_MUTEX_UNLOCK(&chip->lock);
		return count;
	}

	if (!tof8801_app0_is_v2(chip)) {
		tof_debug_error("%s: Error clk trim not supported in revision: %#x",
				__func__, chip->info_rec.record.app_ver);
		AMS_MUTEX_UNLOCK(&chip->lock);
		return -1;
	}

	if (tof8801_app0_rw_osc_trim(chip, &trim, 1)) {
		tof_debug_error("%s: Error setting clock trimming\n", __func__);
		AMS_MUTEX_UNLOCK(&chip->lock);
		return -1;
	}

	AMS_MUTEX_UNLOCK(&chip->lock);
	return count;
}

static ssize_t app0_diag_state_mask_show(struct kobject *kobj,
		struct kobj_attribute *attr,
		char *buf)
{
	int len;
	struct tof_sensor_chip *chip = g_tof_sensor_chip;
	tof_debug_info("%s\n", __func__);
	AMS_MUTEX_LOCK(&chip->lock);

	if (chip->info_rec.record.app_id != TOF8801_APP_ID_APP0) {
		tof_debug_error("%s: Error ToF chip app_id: %#x",
				__func__, chip->info_rec.record.app_id);
		AMS_MUTEX_UNLOCK(&chip->lock);
		return -1;
	}

	len = scnprintf(buf, PAGE_SIZE, "%#x\n", chip->app0_app.diag_state_mask);
	AMS_MUTEX_UNLOCK(&chip->lock);
	return len;
}

static ssize_t app0_reflectivity_count_show(struct kobject *kobj,
		struct kobj_attribute *attr,
		char *buf)
{
	int len;
	struct tof_sensor_chip *chip = g_tof_sensor_chip;
	tof_debug_info("%s\n", __func__);
	AMS_MUTEX_LOCK(&chip->lock);

	if (chip->info_rec.record.app_id != TOF8801_APP_ID_APP0) {
		tof_debug_error("%s: Error ToF chip app_id: %#x",
				__func__, chip->info_rec.record.app_id);
		AMS_MUTEX_UNLOCK(&chip->lock);
		return -1;
	}

	len =
		scnprintf(buf, PAGE_SIZE, "object hits: %u\nreference hits: %u\n",
			  chip->app0_app.algo_results_frame.results_frame.results_v2.data.objectHits,
			  chip->app0_app.algo_results_frame.results_frame.results_v2.data.referenceHits);
	AMS_MUTEX_UNLOCK(&chip->lock);
	return len;
}

static ssize_t app0_general_configuration_show(struct kobject *kobj,
		struct kobj_attribute *attr,
		char *buf)
{
	struct tof_sensor_chip *chip = g_tof_sensor_chip;
	int error, i;
	int len = 0;
	tof_debug_info("%s\n", __func__);
	AMS_MUTEX_LOCK(&chip->lock);

	if (chip->info_rec.record.app_id != TOF8801_APP_ID_APP0) {
		tof_debug_error("%s: Error ToF chip app_id: %#x",
				__func__, chip->info_rec.record.app_id);
		AMS_MUTEX_UNLOCK(&chip->lock);
		return -1;
	}

	error = tof8801_app0_get_dataset(chip, GEN_CFG_DATASET);

	if (error) {
		AMS_MUTEX_UNLOCK(&chip->lock);
		return error;
	}

	for (i = 0; i < APP0_GENERAL_CONFIG_RSP_SIZE; i++) {
		len += scnprintf(buf + len, PAGE_SIZE - len, "%#x:%x\n", i,
				 chip->app0_app.dataset.buf[i]);
	}

	AMS_MUTEX_UNLOCK(&chip->lock);
	return len;
}

static ssize_t app0_read_peak_crosstalk_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	unsigned int len = 0;
	struct tof_sensor_chip *chip = g_tof_sensor_chip;

	tof_debug_error("%s\n", __func__);
	AMS_MUTEX_LOCK(&chip->lock);

	if (chip->info_rec.record.app_id != TOF8801_APP_ID_APP0) {
		tof_debug_error("%s: Error ToF chip app_id: %#x",
				__func__, chip->info_rec.record.app_id);
		AMS_MUTEX_UNLOCK(&chip->lock);
		return -EIO;
	}

	tof_debug_error("xtalk=%d \n", chip->xtalk_peak);
	len = scnprintf(buf, PAGE_SIZE, "%u\n", chip->xtalk_peak);

	AMS_MUTEX_UNLOCK(&chip->lock);
	return len;
}

static ssize_t app0_apply_fac_calib_show(struct kobject *kobj,
		struct kobj_attribute *attr,
		char *buf)
{
	struct tof_sensor_chip *chip = g_tof_sensor_chip;
	int i;
	int len = 0;
	char *tmpbuf = (char *)&chip->ext_calib_data.fac_data;
	tof_debug_info("%s\n", __func__);
	AMS_MUTEX_LOCK(&chip->lock);

	if (chip->info_rec.record.app_id != TOF8801_APP_ID_APP0) {
		tof_debug_error("%s: Error ToF chip app_id: %#x",
				__func__, chip->info_rec.record.app_id);
		AMS_MUTEX_UNLOCK(&chip->lock);
		return -1;
	}

	for (i = 0; i < chip->ext_calib_data.size; i++) {
		len += scnprintf(buf + len, PAGE_SIZE - len, "fac_calib[%d]:%02x\n",
				 i, tmpbuf[i]);
	}

	AMS_MUTEX_UNLOCK(&chip->lock);
	return len;
}

static ssize_t app0_apply_fac_calib_store(struct kobject *kobj,
		struct kobj_attribute *attr,
		const char *buf,
		size_t count)
{
	struct tof_sensor_chip *chip = g_tof_sensor_chip;
	int fw_size = 14;
	//unsigned char fw_data[14];
	int i = 0;

	tof_debug_info("%s\n", __func__);
	AMS_MUTEX_LOCK(&chip->lock);

	if (chip->info_rec.record.app_id != TOF8801_APP_ID_APP0) {
		tof_debug_error("%s: Error ToF chip app_id: %#x",
				__func__, chip->info_rec.record.app_id);
		AMS_MUTEX_UNLOCK(&chip->lock);
		return -1;
	}

	chip->ext_calib_data.size = 0;
	chip->alg_info.size = 0;
	/*fw_size = sscanf(buf, "0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x",
	             &fw_data[0], &fw_data[1], &fw_data[2], &fw_data[3],
	             &fw_data[4], &fw_data[5], &fw_data[6], &fw_data[7],
	             &fw_data[8], &fw_data[9], &fw_data[10],&fw_data[11],
	             &fw_data[12],&fw_data[13]);*/
	memcpy((void *)&chip->ext_calib_data.fac_data,
	       buf, fw_size);
	chip->ext_calib_data.size = fw_size;

	/*if (!fw_size) {
	  tof_debug_warning(
	           "factory calibration data not available \'%s\\n",
	           chip->pdata->fac_calib_data_fname);
	  return 0;
	} else {
	  tof_debug_info( "Read in fac_calib file: \'%s\'. size is %d\n",
	           chip->pdata->fac_calib_data_fname,fw_size);
	}*/
	for (i = 0; i < fw_size; i++)
		tof_debug_info("factory calib i=%d data=0x%x", i,
			       chip->ext_calib_data.fac_data.data[i]);

	//set flag to update fac calib on next measure
	chip->app0_app.cal_update.dataFactoryConfig = 1;
	AMS_MUTEX_UNLOCK(&chip->lock);
	return count;
}

static ssize_t app0_apply_config_calib_show(struct kobject *kobj,
		struct kobj_attribute *attr,
		char *buf)
{
	struct tof_sensor_chip *chip = g_tof_sensor_chip;
	int i;
	int len = 0;
	tof_debug_info("%s\n", __func__);
	AMS_MUTEX_LOCK(&chip->lock);

	if (chip->info_rec.record.app_id != TOF8801_APP_ID_APP0) {
		tof_debug_error("%s: Error ToF chip app_id: %#x",
				__func__, chip->info_rec.record.app_id);
		AMS_MUTEX_UNLOCK(&chip->lock);
		return -1;
	}

	for (i = 0; i < chip->config_data.size; i++) {
		len += scnprintf(buf + len, PAGE_SIZE - len, "config[%d]:%02x\n", i,
				 ((char *)&chip->config_data.cfg_data)[i]);
	}

	AMS_MUTEX_UNLOCK(&chip->lock);
	return len;
}

static ssize_t app0_apply_config_calib_store(struct kobject *kobj,
		struct kobj_attribute *attr,
		const char *buf,
		size_t count)
{
	struct tof_sensor_chip *chip = g_tof_sensor_chip;
	int error = 0;
	tof_debug_info("%s\n", __func__);
	AMS_MUTEX_LOCK(&chip->lock);

	if (chip->info_rec.record.app_id != TOF8801_APP_ID_APP0) {
		tof_debug_error("%s: Error ToF chip app_id: %#x",
				__func__, chip->info_rec.record.app_id);
		AMS_MUTEX_UNLOCK(&chip->lock);
		return -1;
	}

	error = tof8801_get_config_calib_data(chip);

	if (error) {
		AMS_MUTEX_UNLOCK(&chip->lock);
		return error;
	}

	//set flag to update config calib data on next measure
	chip->app0_app.cal_update.dataConfiguration = 1;
	AMS_MUTEX_UNLOCK(&chip->lock);
	return count;
}

static ssize_t app0_apply_state_data_show(struct kobject *kobj,
		struct kobj_attribute *attr,
		char *buf)
{
	struct tof_sensor_chip *chip = g_tof_sensor_chip;
	int i;
	int len = 0;
	tof_debug_info("%s\n", __func__);
	AMS_MUTEX_LOCK(&chip->lock);

	if (chip->info_rec.record.app_id != TOF8801_APP_ID_APP0) {
		tof_debug_error("%s: Error ToF chip app_id: %#x",
				__func__, chip->info_rec.record.app_id);
		AMS_MUTEX_UNLOCK(&chip->lock);
		return -1;
	}

	for (i = 0; i < chip->alg_info.size; i++) {
		len += scnprintf(buf + len, PAGE_SIZE - len, "state_data[%d]:%02x\n", i,
				 ((char *)&chip->alg_info.alg_data.data)[i]);
	}

	AMS_MUTEX_UNLOCK(&chip->lock);
	return len;
}

static ssize_t app0_apply_state_data_store(struct kobject *kobj,
		struct kobj_attribute *attr,
		const char *buf,
		size_t count)
{
	struct tof_sensor_chip *chip = g_tof_sensor_chip;
	int error = 0;
	int num = 0;
	char state[11] = {0};
	tof_debug_info("%s\n", __func__);
	AMS_MUTEX_LOCK(&chip->lock);

	if (chip->info_rec.record.app_id != TOF8801_APP_ID_APP0) {
		tof_debug_error("%s: Error ToF chip app_id: %#x",
				__func__, chip->info_rec.record.app_id);
		AMS_MUTEX_UNLOCK(&chip->lock);
		return -1;
	}

	memset(chip->alg_info.alg_data.data, 0, sizeof(chip->alg_info.alg_data));
	num = sscanf(buf, "%hhx %hhx %hhx %hhx %hhx %hhx %hhx %hhx %hhx %hhx %hhx",
		     &state[0], &state[1], &state[2], &state[3],
		     &state[4], &state[5], &state[6], &state[7],
		     &state[8], &state[9], &state[10]);
	memcpy(chip->alg_info.alg_data.data, state, sizeof(chip->alg_info.alg_data));
	chip->alg_info.size = 11;

	if (num != 11) {
		AMS_MUTEX_UNLOCK(&chip->lock);
		return error;
	}

	//set flag to update config calib data on next measure
	chip->app0_app.cal_update.dataAlgorithmState = 1;
	AMS_MUTEX_UNLOCK(&chip->lock);
	return count;
}

static ssize_t program_version_show(struct kobject *kobj,
				    struct kobj_attribute *attr,
				    char *buf)
{
	struct tof_sensor_chip *chip = g_tof_sensor_chip;
	int len = 0;
	tof_debug_info("%s\n", __func__);
	AMS_MUTEX_LOCK(&chip->lock);

	if (chip->info_rec.record.app_id == TOF8801_APP_ID_APP0) {
		len = tof8801_app0_get_version(chip, buf, PAGE_SIZE);

		if (len == 0) {
			AMS_MUTEX_UNLOCK(&chip->lock);
			return -EIO;
		}

	} else {
		len = scnprintf(buf, PAGE_SIZE, "%#hhx-0-0-0\n",
				chip->info_rec.record.app_ver);
	}

	AMS_MUTEX_UNLOCK(&chip->lock);
	return len;
}

static ssize_t registers_show(struct kobject *kobj,
			      struct kobj_attribute *attr,
			      char *buf)
{
	int per_line = 4;
	int len = 0;
	int idx, per_line_idx;
	int bufsize = PAGE_SIZE;
	int error;
	struct tof_sensor_chip *chip = g_tof_sensor_chip;

	tof_debug_info("%s\n", __func__);
	AMS_MUTEX_LOCK(&chip->lock);
	error = tof8801_get_all_regs(chip);

	if (error) {
		AMS_MUTEX_UNLOCK(&chip->lock);
		return error;
	}

	for (idx = 0; idx < MAX_REGS; idx += per_line) {
		len += scnprintf(buf + len, bufsize - len, "%#02x:", idx);

		for (per_line_idx = 0; per_line_idx < per_line; per_line_idx++) {
			len += scnprintf(buf + len, bufsize - len, " ");
			len += scnprintf(buf + len, bufsize - len, "%02x",
					 chip->shadow[idx + per_line_idx]);
		}

		len += scnprintf(buf + len, bufsize - len, "\n");
	}

	AMS_MUTEX_UNLOCK(&chip->lock);
	return len;
}

static ssize_t app0_ctrl_reg_show(struct kobject *kobj,
				  struct kobj_attribute *attr,
				  char *buf)
{
	struct tof_sensor_chip *chip = g_tof_sensor_chip;
	int error, i;
	int len = 0;
	tof_debug_info("%s\n", __func__);
	AMS_MUTEX_LOCK(&chip->lock);

	if (chip->info_rec.record.app_id != TOF8801_APP_ID_APP0) {
		tof_debug_error("%s: Error ToF chip app_id: %#x",
				__func__, chip->info_rec.record.app_id);
		AMS_MUTEX_UNLOCK(&chip->lock);
		return -1;
	}

	//Read out APP0 header info: status, last cmd, TID, register contents, etc
	error = tof_i2c_read(chip->client, TOF8801_APP_ID,
			     chip->app0_app.ctrl_frame.buf,
			     sizeof(chip->app0_app.ctrl_frame.buf));

	if (error) {
		tof_debug_error("%s: Error i2c communication failure: %d", __func__, error);
		AMS_MUTEX_UNLOCK(&chip->lock);
		return error;
	}

	for (i = 0; i < sizeof(chip->app0_app.ctrl_frame.buf); i++) {
		len += scnprintf(buf + len, PAGE_SIZE - len, "%#02x:%02x\n", i,
				 chip->app0_app.ctrl_frame.buf[i]);
	}

	AMS_MUTEX_UNLOCK(&chip->lock);
	return len;
}

static ssize_t register_write_store(struct kobject *kobj,
				    struct kobj_attribute *attr,
				    const char *buf,
				    size_t count)
{
	char preg;
	char pval;
	char pmask = -1;
	int numparams;
	int rc;
	struct tof_sensor_chip *chip = g_tof_sensor_chip;
	tof_debug_info("%s\n", __func__);

	numparams = sscanf(buf, "%hhx:%hhx:%hhx", &preg, &pval, &pmask);

	if ((numparams < 2) || (numparams > 3))
		return -EINVAL;

	if ((numparams >= 1) && (preg < 0))
		return -EINVAL;

	if ((numparams >= 2) && (preg < 0 || preg > 0xff))
		return -EINVAL;

	if ((numparams >= 3) && (pmask < 0 || pmask > 0xff))
		return -EINVAL;

	if (pmask == -1)
		rc = tof_i2c_write(chip->client, preg, &pval, 1);

	else
		rc = tof_i2c_write_mask(chip->client, preg, &pval, pmask);

	return rc ? rc : count;
}

static ssize_t request_ram_patch_store(struct kobject *kobj,
				       struct kobj_attribute *attr,
				       const char *buf,
				       size_t count)
{
	struct tof_sensor_chip *chip = g_tof_sensor_chip;
	int error;

	AMS_MUTEX_OEM_LOCK(&chip->power_lock);

	if (chip->power_status == TOF_POWER_OFF) {
		error = tof_power_up();

		if (error != 0) {
			tof_debug_error("power up failed.\n");
			AMS_MUTEX_OEM_UNLOCK(&chip->power_lock);
			return error;

		} else {
			chip->power_status = TOF_POWER_ON;
			tof_debug_info("power up success.\n");
		}

	} else
		tof_debug_info("tof have power up.\n");

	AMS_MUTEX_OEM_UNLOCK(&chip->power_lock);

	tof_hard_reset(chip);
	return count;
#if 0
	int error = 0;
	tof_debug_info("%s\n", __func__);

	AMS_MUTEX_LOCK(&chip->lock);
	/***** Make firmware download available to user space *****/
	error = tof8801_firmware_download(chip, 0);

	if (error) {
		AMS_MUTEX_UNLOCK(&chip->lock);
		return error;
	}

	/* enable all ToF interrupts on sensor */
	tof8801_enable_interrupts(chip, IRQ_RESULTS | IRQ_DIAG | IRQ_ERROR);

	AMS_MUTEX_UNLOCK(&chip->lock);
#endif
}

static ssize_t app0_get_fac_calib_show(struct kobject *kobj,
				       struct kobj_attribute *attr,
				       char *buf)
{

	struct tof_sensor_chip *chip = g_tof_sensor_chip;
	int error;
	u32 len;
	int i = 0;
	unsigned long start = jiffies;
	int timeout_flag = 0;
	AMS_MUTEX_LOCK(&chip->lock);
	tof_debug_info("%s\n", __func__);

	if (chip->info_rec.record.app_id != TOF8801_APP_ID_APP0) {
		tof_debug_info("%s: Error ToF chip app_id: %#x",
			       __func__, chip->info_rec.record.app_id);
		AMS_MUTEX_UNLOCK(&chip->lock);
		return -1;
	}

	error = tof8801_app0_perform_factory_calibration(chip);

	if (error) {
		tof_debug_error("Error starting factory calibration routine: %d", error);
		AMS_MUTEX_UNLOCK(&chip->lock);
		return 0;
	}

	do {
		//spin here waiting for factory calibration to complete
		AMS_MUTEX_UNLOCK(&chip->lock);
		msleep(100);
		AMS_MUTEX_LOCK(&chip->lock);
		timeout_flag = ((jiffies - start) >= msecs_to_jiffies(
					APP0_FAC_CALIB_MSEC_TIMEOUT));
		tof_debug_info("timeout_flag size: %d,=%d", timeout_flag,
			       tof8801_app0_measure_in_progress(chip));
	} while (!timeout_flag && tof8801_app0_measure_in_progress(chip));

	if (!tof8801_app0_measure_in_progress(chip) &&
			chip->app0_app.cal_update.dataFactoryConfig) {
		// If calib measure complete and was successful
		if (chip->ext_calib_data.size)
			memcpy(buf, (void *)&chip->ext_calib_data.fac_data, chip->ext_calib_data.size);

		len = chip->ext_calib_data.size;
		buf[len] = 0; //output is a string so we need to add null-terminating character
		tof_debug_info("Done performing factory calibration, size: %u", len);

		if (chip->driver_debug) {
			for (i = 0; i < len; i++)
				tof_debug_info("calibration  i=%d data=0x%x", i,
					       chip->ext_calib_data.fac_data.data[i]);
		}

	} else {
		tof_debug_error("Error timeout waiting on factory calibration");
		AMS_MUTEX_UNLOCK(&chip->lock);
		return 0;
	}

	(void)tof8801_app0_capture(chip, 0);
	AMS_MUTEX_UNLOCK(&chip->lock);
	tof_debug_error("factory calibration done");

	return len;
}

static ssize_t app0_tof_output_read(struct file *fp, struct kobject *kobj,
				    struct bin_attribute *attr, char *buf,
				    loff_t off, size_t size)
{
//  struct device *dev = kobj_to_dev(kobj);
	struct tof_sensor_chip *chip = g_tof_sensor_chip;
	int read;
	u32 elem_len;
	AMS_MUTEX_LOCK(&chip->lock);
	elem_len = kfifo_peek_len(&chip->tof_output_fifo);
	tof_debug_info("%s size: %u\n", __func__, (unsigned int) size);

	if (kfifo_len(&chip->tof_output_fifo)) {
		tof_debug_info("fifo read elem_len: %u\n", elem_len);
		read = kfifo_out(&chip->tof_output_fifo, buf, elem_len);
		tof_debug_info("fifo_len: %u\n", kfifo_len(&chip->tof_output_fifo));
		AMS_MUTEX_UNLOCK(&chip->lock);
		return elem_len;

	} else {
		AMS_MUTEX_UNLOCK(&chip->lock);
		return 0;
	}
}

static ssize_t app0_get_distance_show(struct kobject *kobj,
				      struct kobj_attribute *attr, char *buf)
{
	unsigned int len = 0;
	struct tof_sensor_chip *chip = g_tof_sensor_chip;

	AMS_MUTEX_LOCK(&chip->lock);
	msleep(100);
	len = scnprintf(buf, PAGE_SIZE, "%d\n", chip->distance & 0xffff);
	chip->distance = 0;

	AMS_MUTEX_UNLOCK(&chip->lock);
	return len;
}

/****************************************************************************
 * Common Sysfs Attributes
 * **************************************************************************/
/******* READ-WRITE attributes ******/
static OPLUS_ATTR(program, 0644, program_show, program_store);
static OPLUS_ATTR(chip_enable, 0644, chip_enable_show, chip_enable_store);
static OPLUS_ATTR(driver_debug, 0644, driver_debug_show, driver_debug_store);
/******* READ-ONLY attributes ******/
static OPLUS_ATTR(program_version, 0644, program_version_show, NULL);
static OPLUS_ATTR(registers, 0644, registers_show, NULL);
/******* WRITE-ONLY attributes ******/
static OPLUS_ATTR(register_write, 0644, NULL, register_write_store);
static OPLUS_ATTR(request_ram_patch, 0644, NULL, request_ram_patch_store);

/****************************************************************************
 * Bootloader Sysfs Attributes
 * **************************************************************************/
/******* READ-WRITE attributes ******/
/******* READ-ONLY attributes ******/
/******* WRITE-ONLY attributes ******/

/****************************************************************************
 * APP0 Sysfs Attributes
 * *************************************************************************/
/******* READ-WRITE attributes ******/
static OPLUS_ATTR(app0_command, 0644, app0_command_show, app0_command_store);
static OPLUS_ATTR(capture, 0644, capture_show, capture_store);
static OPLUS_ATTR(period, 0644, period_show, period_store);
static OPLUS_ATTR(noise_threshold, 0644, noise_threshold_show,
		  noise_threshold_store);
static OPLUS_ATTR(iterations, 0644, iterations_show, iterations_store);
static OPLUS_ATTR(capture_delay, 0644, capture_delay_show, capture_delay_store);
static OPLUS_ATTR(alg_setting, 0644, alg_setting_show, alg_setting_store);
static OPLUS_ATTR(gpio_setting, 0644, gpio_setting_show, gpio_setting_store);
static OPLUS_ATTR(app0_clk_iterations, 0644, app0_clk_iterations_show,
		  app0_clk_iterations_store);
static OPLUS_ATTR(app0_clk_trim_enable, 0644, app0_clk_trim_enable_show,
		  app0_clk_trim_enable_store);
static OPLUS_ATTR(app0_clk_trim_set, 0644, app0_clk_trim_set_show,
		  app0_clk_trim_set_store);
static OPLUS_ATTR(app0_apply_fac_calib, 0644, app0_apply_fac_calib_show,
		  app0_apply_fac_calib_store);
static OPLUS_ATTR(app0_apply_config_calib, 0644, app0_apply_config_calib_show,
		  app0_apply_config_calib_store);
static OPLUS_ATTR(app0_apply_state_data, 0644, app0_apply_state_data_show,
		  app0_apply_state_data_store);
/******* READ-ONLY attributes ******/
static OPLUS_ATTR(app0_general_configuration, 0644,
		  app0_general_configuration_show, NULL);
static OPLUS_ATTR(app0_ctrl_reg, 0644, app0_ctrl_reg_show, NULL);
static OPLUS_ATTR(app0_temp, 0644, app0_temp_show, NULL);
static OPLUS_ATTR(app0_diag_state_mask, 0644, app0_diag_state_mask_show, NULL);
static OPLUS_ATTR(app0_reflectivity_count, 0644, app0_reflectivity_count_show,
		  NULL);
static OPLUS_ATTR(app0_get_fac_calib, 0644, app0_get_fac_calib_show, NULL);
static OPLUS_ATTR(app0_get_distance, 0644, app0_get_distance_show, NULL);
static OPLUS_ATTR(app0_read_peak_crosstalk, 0644, app0_read_peak_crosstalk_show,
		  NULL);

/******* WRITE-ONLY attributes ******/
/******* READ-ONLY BINARY attributes ******/
static BIN_ATTR_RO(app0_tof_output, 0);


static struct attribute *tof_common_attrs[] = {
	&oplus_attr_program.attr,
	&oplus_attr_chip_enable.attr,
	&oplus_attr_driver_debug.attr,
	&oplus_attr_program_version.attr,
	&oplus_attr_registers.attr,
	&oplus_attr_register_write.attr,
	&oplus_attr_request_ram_patch.attr,
	NULL,
};
static struct attribute *tof_bl_attrs[] = {
	NULL,
};
static struct attribute *tof_app0_attrs[] = {
	&oplus_attr_app0_command.attr,
	&oplus_attr_capture.attr,
	&oplus_attr_period.attr,
	&oplus_attr_iterations.attr,
	&oplus_attr_noise_threshold.attr,
	&oplus_attr_capture_delay.attr,
	&oplus_attr_alg_setting.attr,
	&oplus_attr_gpio_setting.attr,
	&oplus_attr_app0_clk_iterations.attr,
	&oplus_attr_app0_clk_trim_enable.attr,
	&oplus_attr_app0_clk_trim_set.attr,
	&oplus_attr_app0_diag_state_mask.attr,
	&oplus_attr_app0_general_configuration.attr,
	&oplus_attr_app0_ctrl_reg.attr,
	&oplus_attr_app0_temp.attr,
	&oplus_attr_app0_reflectivity_count.attr,
	&oplus_attr_app0_get_fac_calib.attr,
	&oplus_attr_app0_apply_fac_calib.attr,
	&oplus_attr_app0_apply_config_calib.attr,
	&oplus_attr_app0_apply_state_data.attr,
	&oplus_attr_app0_read_peak_crosstalk.attr,
	&oplus_attr_app0_get_distance.attr,
	NULL,
};
static struct bin_attribute *tof_app0_bin_attrs[] = {
	&bin_attr_app0_tof_output,
	NULL,
};
static const struct attribute_group tof_common_group = {
	.attrs = tof_common_attrs,
};
static const struct attribute_group tof_bl_group = {
	.name = "bootloader",
	.attrs = tof_bl_attrs,
};
static const struct attribute_group tof_app0_group = {
	.name = "app0",
	.attrs = tof_app0_attrs,
	.bin_attrs = tof_app0_bin_attrs,
};
static const struct attribute_group *tof_groups[] = {
	&tof_common_group,
	&tof_bl_group,
	&tof_app0_group,
	NULL,
};

/**
 * tof_i2c_read - Read number of bytes starting at a specific address over I2C
 *
 * @client: the i2c client
 * @reg: the i2c register address
 * @buf: pointer to a buffer that will contain the received data
 * @len: number of bytes to read
 */
int tof_i2c_read(struct i2c_client *client, char reg, char *buf, int len)
{
	struct i2c_msg msgs[2];
	int ret = -1;
	struct tof_sensor_chip *chip = i2c_get_clientdata(client);

	msgs[0].flags = 0;
	msgs[0].addr  = client->addr;
	msgs[0].len   = 1;
	msgs[0].buf   = &reg;

	msgs[1].flags = I2C_M_RD;
	msgs[1].addr  = client->addr;
	msgs[1].len   = len;
	msgs[1].buf   = buf;

	AMS_MUTEX_OEM_LOCK(&chip->power_lock);

	if (chip->power_status != TOF_POWER_ON) {
		if (chip->driver_debug == 1)
			tof_debug_error("tof power state is off ,cannot do i2c command");

		AMS_MUTEX_OEM_UNLOCK(&chip->power_lock);
		return 0;
	}

	AMS_MUTEX_OEM_UNLOCK(&chip->power_lock);

	if (is_tof_use_cci_i2c())
		ret = tof_cci_i2c_read(reg, buf, len);

	else
		ret = i2c_transfer(client->adapter, msgs, 2);

	return ret < 0 ? ret : (ret != ARRAY_SIZE(msgs) ? -EIO : 0);
}

/**
 * tof_i2c_write - Write nuber of bytes starting at a specific address over I2C
 *
 * @client: the i2c client
 * @reg: the i2c register address
 * @buf: pointer to a buffer that will contain the data to write
 * @len: number of bytes to write
 */
int tof_i2c_write(struct i2c_client *client, char reg, const char *buf, int len)
{
	u8 *addr_buf;
	struct i2c_msg msg;
	struct tof_sensor_chip *chip = i2c_get_clientdata(client);
	int idx = reg;
	int ret = 0;
	char debug[120];
	u32 strsize = 0;

	addr_buf = kmalloc(len + 1, GFP_KERNEL);

	if (!addr_buf)
		return -ENOMEM;

	addr_buf[0] = reg;
	memcpy(&addr_buf[1], buf, len);
	msg.flags = 0;
	msg.addr = client->addr;
	msg.buf = addr_buf;
	msg.len = len + 1;

	AMS_MUTEX_OEM_LOCK(&chip->power_lock);

	if (chip->power_status != TOF_POWER_ON) {
		if (chip->driver_debug == 1)
			tof_debug_error("tof power state is off ,cannot do i2c command");

		kfree(addr_buf);
		AMS_MUTEX_OEM_UNLOCK(&chip->power_lock);
		return ret;
	}

	AMS_MUTEX_OEM_UNLOCK(&chip->power_lock);


	if (is_tof_use_cci_i2c())
		ret = tof_cci_i2c_write(reg, (char *)buf, len);

	else
		ret = i2c_transfer(client->adapter, &msg, 1);

	if (ret != 1)
		tof_debug_error("i2c_transfer failed: %d msg_len: %u", ret, len);

	if (chip->driver_debug > 1) {
		strsize = scnprintf(debug, sizeof(debug), "i2c_write: ");

		for (idx = 0; (ret == 1) && (idx < msg.len); idx++)
			strsize += scnprintf(debug + strsize, sizeof(debug) - strsize, "%02x ",
					     addr_buf[idx]);

		tof_debug_info("%s", debug);
	}

	kfree(addr_buf);
	return ret < 0 ? ret : (ret != 1 ? -EIO : 0);
}

/**
 * tof_i2c_write_mask - Write a byte to the specified address with a given bitmask
 *
 * @client: the i2c client
 * @reg: the i2c register address
 * @val: byte to write
 * @mask: bitmask to apply to address before writing
 */
int tof_i2c_write_mask(struct i2c_client *client, char reg,
		       const char *val, char mask)
{
	int ret;
	u8 temp;
	struct tof_sensor_chip *chip = i2c_get_clientdata(client);

	ret = tof_i2c_read(client, reg, &temp, 1);
	temp &= ~mask;
	temp |= *val;
	ret = tof_i2c_write(client, reg, &temp, 1);

	chip->shadow[(int)reg] = temp;

	return ret;
}

/**
 * tof8801_get_register - Return a specific register
 *
 * @chip: tof_sensor_chip pointer
 * @value: pointer to value in register
 */
int tof8801_get_register(struct i2c_client *client,
			 char reg, char *value)
{
	return tof_i2c_read(client, reg, value, sizeof(char));
}

/**
 * tof8801_set_register - Set a specific register
 *
 * @chip: tof_sensor_chip pointer
 * @value: value to set in register
 */
int tof8801_set_register(struct i2c_client *client,
			 char reg, const char value)
{
	return tof_i2c_write(client, reg, &value, sizeof(char));
}

/**
 * tof8801_set_register_mask - Set a specific register, with a mask
 *
 * @chip: tof_sensor_chip pointer
 * @value: value to set in register
 * @mask: mask to apply with register, i.e. value=0x1, mask=0x1 = only bit 0 set
 */
int tof8801_set_register_mask(struct i2c_client *client,
			      char reg, const char value, const char mask)
{
	return tof_i2c_write_mask(client, reg, &value, mask);
}

void tof_dump_i2c_regs(struct tof_sensor_chip *chip, char offset, char end)
{
	int per_line = 4;
	int len = 0;
	int idx, per_line_idx;
	char debug[80];

	offset &= ~(per_line - 1); // Byte boundary for nice printing

	while ((end & (per_line - 1)) != (per_line - 1)) end += 1;

	end = (end < offset) ? (offset + per_line) : end;
	tof_debug_info("%s\n", __func__);
	(void) tof_i2c_read(chip->client, offset,
			    &chip->shadow[(int)offset],
			    (end - offset));

	for (idx = offset; idx < end; idx += per_line) {
		memset(debug, 0, sizeof(debug));
		len += scnprintf(debug, sizeof(debug) - len, "%02x: ", idx);

		for (per_line_idx = 0; per_line_idx < per_line; per_line_idx++)
			len += scnprintf(debug + len, sizeof(debug) - len, "%02x ",
					 chip->shadow[idx + per_line_idx]);

		len = 0;
		tof_debug_info("%s", debug);
	}
}

/**
 * tof_standby_operation - Tell the ToF chip to wakeup/standby
 *
 * @client: the i2c client
 */
static int tof_standby_operation(struct i2c_client *client, char oper)
{
	return tof8801_set_register(client, TOF8801_STAT, oper);
}

/**
 * tof_CE_toggle - Hard reset the ToF by toggling the ChipEnable
 *
 * @client: the i2c client
 */
static int tof_CE_toggle(struct i2c_client *client)
{
	struct tof_sensor_chip *chip = i2c_get_clientdata(client);
	int error = 0;

	if (!chip->pdata->gpiod_enable) {
		//not supported in poll mode
		return -EIO;
	}

	error = gpiod_direction_output(chip->pdata->gpiod_enable, 0);

	if (error)
		return error;

	g_is_download_fw = 0;
	error = gpiod_direction_output(chip->pdata->gpiod_enable, 1);
	/* ToF requires 5ms to get i2c back up */
	usleep_range(TOF8801_I2C_WAIT_USEC, TOF8801_I2C_WAIT_USEC + 1);
	return error;
}

/**
 * tof_wait_for_cpu_ready_timeout - Check for CPU ready state in the ToF sensor
 *                                  for a specified period of time
 *
 * @client: the i2c client
 */
int tof_wait_for_cpu_ready_timeout(struct i2c_client *client,
				   unsigned long usec)
{
	int error = 0;
	unsigned long curr = jiffies;

	do {
		error = tof_wait_for_cpu_ready(client);

		if (error == 0)
			return 0;
	} while ((jiffies - curr) < usecs_to_jiffies(usec));

	tof_debug_error("Error timeout (%lu usec) waiting on cpu_ready: %d\n", usec,
			error);
	return -EIO;
}

/**
 * tof_wait_for_cpu_ready - Check for CPU ready state in the ToF sensor
 *
 * @client: the i2c client
 */
int tof_wait_for_cpu_ready(struct i2c_client *client)
{
	int retry = 0;
	int error;
	u8 status;

	//wait for i2c
	usleep_range(TOF8801_I2C_WAIT_USEC, TOF8801_I2C_WAIT_USEC + 1);

	while (retry++ < TOF8801_MAX_WAIT_RETRY) {
		error = tof8801_get_register(client, TOF8801_STAT, &status);

		if (error) {
			tof_debug_error("i2c test failed attempt %d: %d\n", retry, error);
			continue;
		}

		if (TOF8801_STAT_CPU_READY(status)) {
			dev_dbg(&client->dev, "ToF chip CPU is ready");
			return 0;

		} else if (TOF8801_STAT_CPU_SLEEP(status)) {
			tof_debug_info("ToF chip in standby state, waking up");
			tof_standby_operation(client, WAKEUP);
			usleep_range(TOF8801_I2C_WAIT_USEC, TOF8801_I2C_WAIT_USEC + 1);
			error = -EIO;
			continue;

		} else if (TOF8801_STAT_CPU_BUSY(status) &&
				(retry >= TOF8801_MAX_WAIT_RETRY))
			return -EIO;

		usleep_range(TOF8801_WAIT_UDELAY, 2 * TOF8801_WAIT_UDELAY);
	}

//  EXIT_ON_RELEASE_TOF_FAILURE;

	return error;
}

/**
 * tof_wait_for_cpu_startup - Check for CPU ready state in the ToF sensor
 *
 * @client: the i2c client
 */
int tof_wait_for_cpu_startup(struct i2c_client *client)
{
	int retry = 0;
	int CE_retry = 0;
	int error = 0;
	u8 status;
	struct tof_sensor_chip *chip = g_tof_sensor_chip;

	AMS_MUTEX_OEM_LOCK(&chip->power_lock);

	if (chip->power_status != TOF_POWER_ON) {
		tof_debug_error("tof power state is off");
		AMS_MUTEX_OEM_UNLOCK(&chip->power_lock);
		return error;
	}

	AMS_MUTEX_OEM_UNLOCK(&chip->power_lock);

	while (retry++ < TOF8801_MAX_STARTUP_RETRY) {
		tof_debug_error("debug for tof8801_get_register %d\n", retry);
		usleep_range(TOF8801_I2C_WAIT_USEC, TOF8801_I2C_WAIT_USEC + 1);
		error = tof8801_get_register(client, TOF8801_STAT, &status);

		if (error) {
			tof_debug_error("i2c test failed attempt %d: %d\n", retry, error);
			continue;

		} else {
			tof_debug_error("CPU status register: %#04x value: %#04x\n",
					TOF8801_STAT, status);
		}

		if (TOF8801_STAT_CPU_READY(status)) {
			tof_debug_info("ToF chip CPU is ready");
			return 0;

		} else if (TOF8801_STAT_CPU_SLEEP(status)) {
			tof_debug_info("ToF chip in standby state, waking up");
			tof_standby_operation(client, WAKEUP);
			error = -EIO;
			continue;

		} else if (TOF8801_STAT_CPU_BUSY(status) &&
				(retry >= TOF8801_MAX_STARTUP_RETRY)) {
			if ((CE_retry < TOF8801_MAX_STARTUP_RETRY)) {
				tof_debug_info("ToF chip still busy, try toggle CE");

				if (tof_CE_toggle(client))
					return -EIO;

				retry = 0;
				CE_retry++;

			} else
				return -EIO;
		}
	}

	return error;
}

/**
 * tof_init_info_record - initialize info record of currently running app
 *
 * @client: the i2c client
 * @record: pointer to info_record struct where data will be placed
 */
int tof_init_info_record(struct tof_sensor_chip *chip)
{
	int error;
	char data[sizeof(struct record)] = {0};

	error = tof_i2c_read(chip->client, TOF8801_APP_ID,
			     data, TOF8801_INFO_RECORD_SIZE);

	if (error) {
		tof_debug_error("read record failed: %d\n", error);
		goto err;
	}

	memcpy(&chip->info_rec.record, data, sizeof(struct record));

	tof_debug_info("Read info record - Running app_id: %#x.\n",
		       chip->info_rec.record.app_id);

	/* re-initialize apps */
	if (chip->info_rec.record.app_id == TOF8801_APP_ID_BOOTLOADER)
		tof8801_BL_init_app(&chip->BL_app);

	else if (chip->info_rec.record.app_id == TOF8801_APP_ID_APP0)
		tof8801_app0_init_app(&chip->app0_app);

	return 0;
err:
	return error;
}

static int tof_switch_from_bootloader(struct tof_sensor_chip *chip,
				      char req_app_id)
{
	int error = 0;
	char *new_app_id;

	// Try to perform RAM download (if possible)
	tof_debug_error("start to tof8801_firmware_download  \n");
	error = tof8801_firmware_download(chip, 0);

	start_poll_thread();

	tof_debug_error("end to tof8801_firmware_download \n");

	if (error != 0) {
		tof_debug_error("tof_switch_from_bootloader tof_switch_from_bootloader error != 0  \n");

		//This means either there is no firmware, or there was a failure
		error = tof8801_set_register(chip->client, TOF8801_REQ_APP_ID, req_app_id);

		if (error) {
			tof_debug_error("Error setting REQ_APP_ID register.\n");
			error = -EIO;
		}

		error = tof_wait_for_cpu_ready_timeout(chip->client, 100000);

		if (error)
			tof_debug_error("Error waiting for CPU ready flag.\n");

		error = tof_init_info_record(chip);

		if (error)
			tof_debug_error("Error reading info record.\n");
	}

	new_app_id = &chip->info_rec.record.app_id;
	tof_debug_info("Running app_id: 0x%02x\n", *new_app_id);

	switch (*new_app_id) {
	case TOF8801_APP_ID_BOOTLOADER:
		tof_debug_error("Error: application switch failed.\n");
		break;

	case TOF8801_APP_ID_APP0:
		/* enable all ToF interrupts on sensor */
		tof_debug_error("tof_switch_from_bootloader tof8801_enable_interrupts start \n");
		tof8801_enable_interrupts(chip, IRQ_RESULTS | IRQ_DIAG | IRQ_ERROR);
		break;

	case TOF8801_APP_ID_APP1:
		break;

	default:
		tof_debug_error("Error: Unrecognized application.\n");
		return -1;
	}

	return (*new_app_id == req_app_id) ? 0 : -1;
}

int tof_switch_apps(struct tof_sensor_chip *chip, char req_app_id)
{
	int error = 0;
	tof_debug_error("entery tof_switch_apps ,chip->info_rec.record.app_id is %x",
			chip->info_rec.record.app_id);

	if (req_app_id == chip->info_rec.record.app_id) {
		tof_debug_error("don't switch tof_switch_apps ,return ");
		return 0;
	}

	if ((req_app_id != TOF8801_APP_ID_BOOTLOADER) &&
			(req_app_id != TOF8801_APP_ID_APP0)       &&
			(req_app_id != TOF8801_APP_ID_APP1)) {
		tof_debug_error("without cmd  tof_switch_apps ,return ");
		return -1;
	}

	tof_debug_error("chip->info_rec.record.app_id is %x",
			chip->info_rec.record.app_id);

	switch (chip->info_rec.record.app_id) {
	case TOF8801_APP_ID_BOOTLOADER:
		tof_debug_error("tof_switch_from_bootloader start -----");
		error = tof_switch_from_bootloader(chip, req_app_id);

		if (error) {
			/* Hard reset back to bootloader if error */
			gpiod_set_value(chip->pdata->gpiod_enable, 0);
			g_is_download_fw = 0;
			gpiod_set_value(chip->pdata->gpiod_enable, 1);
			tof_debug_error("tof_wait_for_cpu_startup start -----");
			error = tof_wait_for_cpu_startup(chip->client);

			if (error) {
				tof_debug_error("I2C communication failure: %d\n",
						error);
				return error;
			}

			error = tof_init_info_record(chip);

			if (error) {
				tof_debug_error("Read application info record failed.\n");
				return error;
			}

			return -1;
		}

		break;

	case TOF8801_APP_ID_APP0:
		tof_debug_error("switch  TOF8801_APP_ID_APP0 start -----");
		error = tof8801_app0_switch_to_bootloader(chip);
		break;

	case TOF8801_APP_ID_APP1:
		tof_debug_error("switch  TOF8801_APP_ID_APP1 start -----");
		return -1;
		break;

	default:
		tof_debug_error("without any match  cmd");
		error = -1;
		break;
	}

	return error;
}

/**
 * tof_hard_reset - use GPIO Chip Enable to reset the device
 *
 * @tof_chip: tof_sensor_chip pointer
 */
int tof_hard_reset(struct tof_sensor_chip *chip)
{
	int error = 0;
//  int in_app0 = (chip->info_rec.record.app_id == TOF8801_APP_ID_APP0);
//  int in_app0 = 1;

	struct i2c_client *client = tof_pdata.client;
	struct tof_sensor_chip *tof_chip = (struct tof_sensor_chip *)i2c_get_clientdata(
			client);

	if (!chip->pdata->gpiod_enable)
		return -EIO;

	/* toggle CE pin */

#if 0
	tof_chip->saved_clk_trim = UNINITIALIZED_CLK_TRIM_VAL;

	tof8801_app0_default_cap_settings(&tof_chip->app0_app);

	error = tof_init_info_record(chip);

	if (error) {
		tof_debug_error("Read application info record failed.\n");
		return error;
	}

	// If we were in App0 before CE LOW, try to download/switch back to App0
	tof_debug_error("is In_app0 %d \n", in_app0);
	// Need to perform RAM download if CE is toggled
	error = tof_switch_apps(chip, (char)TOF8801_APP_ID_APP0);

	if (error) {
		tof_debug_error("tof_switch_apps result  %d \n", error);
		return error;
	}

//	tof8801_enable_interrupts(tof_chip, IRQ_RESULTS | IRQ_DIAG | IRQ_ERROR);
#endif
#if 1
	error = tof_wait_for_cpu_startup(chip->client);

	if (error) {
		tof_debug_error("I2C communication failure: %d\n", error);
		return error;
	}

	tof_chip->saved_clk_trim = UNINITIALIZED_CLK_TRIM_VAL;
	//read external (manufacturer) configuration data
	error = tof8801_get_config_calib_data(tof_chip);

	if (error)
		tof_debug_error("Error reading config data: %d\n", error);

	tof8801_app0_default_cap_settings(&tof_chip->app0_app);

	error = tof_init_info_record(tof_chip);

	if (error) {
		tof_debug_error("Read application info record failed.\n");
		return error;
	}

	/* enable all ToF interrupts on sensor */
	tof8801_enable_interrupts(tof_chip, IRQ_RESULTS | IRQ_DIAG | IRQ_ERROR);
	error = start_poll_thread();

	if (error) {
		tof_debug_error("start_poll_thread failed.\n");
		return error;
	}

	AMS_MUTEX_LOCK(&tof_chip->lock);

	if (!g_is_download_fw) {
		/***** Make firmware download available to user space *****/
		error = tof8801_firmware_download(tof_chip, 0);

		if (error)
			tof_debug_error("fail to tof8801_firmware_download,rc = %d", error);

	} else
		tof_debug_error("ams-tof fw have download");

	tof8801_enable_interrupts(tof_chip, IRQ_RESULTS | IRQ_DIAG | IRQ_ERROR);

	tof8801_enable_pinctrl(&(client->dev));

	AMS_MUTEX_UNLOCK(&tof_chip->lock);
#endif

	return error;
}

/**
 * tof_get_gpio_config - Get GPIO config from DT
 *
 * @tof_chip: tof_sensor_chip pointer
 */
static int tof_get_gpio_config(struct tof_sensor_chip *tof_chip)
{
	int error;
	struct device *dev;
	struct gpio_desc *gpiod;

	if (!tof_chip->client)
		return -EINVAL;

	dev = &tof_chip->client->dev;

	/* Get the enable line GPIO pin number */
	gpiod = devm_gpiod_get_optional(dev, TOF_GPIO_ENABLE_NAME, GPIOD_OUT_HIGH);

	if (IS_ERR(gpiod)) {
		error = PTR_ERR(gpiod);
		return error;
	}

	tof_chip->pdata->gpiod_enable = gpiod;

	/* Get the interrupt GPIO pin number
	gpiod = devm_gpiod_get_optional(dev, TOF_GPIO_INT_NAME, GPIOD_IN);
	if (IS_ERR(gpiod)) {
	  error = PTR_ERR(gpiod);
	  return error;
	}
	tof_chip->pdata->gpiod_interrupt = gpiod;*/
	return 0;
}

/**
 * tof_ram_patch_callback - The firmware download callback
 *
 * @cfg: the firmware cfg structure
 * @ctx: private data pointer to struct tof_sensor_chip
 */
static void tof_ram_patch_callback(const struct firmware *cfg, void *ctx)
{
	struct tof_sensor_chip *chip = ctx;
	const u8 *line;
	const u8 *line_end;
	int verify = 0;
	int result = 0;
	u32 patch_size = 0;
	u64 fwdl_time = 0;
	struct timespec64 start_ts = {0}, end_ts = {0};
	AMS_MUTEX_LOCK(&chip->lock);

	if (!chip) {
		pr_err("AMS-TOF Error: Ram patch callback NULL context pointer.\n");
		AMS_MUTEX_UNLOCK(&chip->lock);
		return;
	}

	if (!cfg) {
		tof_debug_warning("%s: Warning, firmware not available.\n", __func__);
		goto err_fwdl;
	}

	tof_debug_info("%s: Ram patch in progress... 1th\n", __func__);

	/* Assuming you can only perform ram download while in BL application */
	/* switch back to BL app to perform RAM download */
	if (chip->info_rec.record.app_id != TOF8801_APP_ID_BOOTLOADER) {
		tof_debug_info(
			"Current app_id: %hhx - Switching to bootloader for RAM download",
			chip->info_rec.record.app_id);
		pr_err("AMS-TOF Error: debug for tof_switch_apps start .\n");
		result = tof_switch_apps(chip, (char)TOF8801_APP_ID_BOOTLOADER);

		if (result) {
			tof_debug_info("Error changing to bootloader app: \'%d\'", result);
			goto err_fwdl;
		}
	}

	//Start fwdl timer
	ktime_get_real_ts64(&start_ts);
	/* setup encryption salt */
	result = tof8801_BL_upload_init(chip->client, &chip->BL_app, tof_salt_value);
	tof_debug_info("%s: Ram patch in progress... 2th \n", __func__);

	if (result) {
		tof_debug_info("Error setting upload salt: \'%d\'", result);
		goto err_fwdl;
	}

	//assume we have mutex already
	intelHexInterpreterInitialise();
	tof_debug_info("%s: Ram patch in progress... 3th \n", __func__);
	line = cfg->data;
	line_end = line;

	while ((line_end - cfg->data) < cfg->size) {
		line_end = strchrnul(line, '\n');
		patch_size += ((line_end - line) > INTEL_HEX_MIN_RECORD_SIZE) ?
			      ((line_end - line - INTEL_HEX_MIN_RECORD_SIZE) / 2) : 0;
		result = intelHexHandleRecord(chip->client, &chip->BL_app,

					      line_end - line, line, verify);

		if (result) {
			tof_debug_error("%s: Ram patch failed: %d\n", __func__, result);
			goto err_fwdl;
		}

		line = ++line_end;
	}

	//Stop fwdl timer
	ktime_get_real_ts64(&end_ts);
	fwdl_time = timespec64_sub(end_ts, start_ts).tv_nsec / 1000000; //time in ms
	tof_debug_info(
		"%s: Ram patch complete, patch size: %uK, dl time: %llu ms\n",
		__func__, ((patch_size >> 10) + 1), fwdl_time);
	//wait for i2c
	usleep_range(TOF8801_I2C_WAIT_USEC, TOF8801_I2C_WAIT_USEC + 1);
	/* resync our info record since we just switched apps */
	tof_init_info_record(chip);
	tof_debug_info("laser download fw success\n");
	g_is_download_fw = 1;
err_fwdl:
	release_firmware(cfg);
	complete_all(&chip->ram_patch_in_progress);
	AMS_MUTEX_UNLOCK(&chip->lock);
}

int tof_queue_frame(struct tof_sensor_chip *chip, void *buf, int size)
{
	unsigned int fifo_len;
	unsigned int frame_size;
	int result = kfifo_in(&chip->tof_output_fifo, buf, size);

	if (result == 0) {
		if (chip->driver_debug == 1)
			tof_debug_error("Error: Frame buffer is full, clearing buffer.\n");

		kfifo_reset(&chip->tof_output_fifo);
		tof8801_app0_report_error(chip, ERR_BUF_OVERFLOW, DEV_OK);
		result = kfifo_in(&chip->tof_output_fifo, buf, size);

		if (result == 0)
			tof_debug_error("Error: queueing ToF output frame.\n");
	}

	if (chip->driver_debug == 2) {
		fifo_len = kfifo_len(&chip->tof_output_fifo);
		frame_size = ((char *)buf)[DRV_FRAME_SIZE_LSB] |
			     (((char *)buf)[DRV_FRAME_SIZE_MSB] << 8);
		tof_debug_info("Add frame_id: 0x%x, data_size: %u\n",
			       ((char *)buf)[DRV_FRAME_ID], frame_size);
		tof_debug_info(
			"New fifo len: %u, fifo utilization: %u%%\n",
			fifo_len, (1000 * fifo_len / kfifo_size(&chip->tof_output_fifo)) / 10);
	}

	return (result == size) ? 0 : -1;
}

/**
 * tof_irq_handler - The IRQ handler
 *
 * @irq: interrupt number.
 * @dev_id: private data pointer.
 */
static irqreturn_t tof_irq_handler(int irq, void *dev_id)
{
	struct tof_sensor_chip *tof_chip = (struct tof_sensor_chip *)dev_id;
	char int_stat = 0, DATA1 = 0, DATA2 = 0;
	char appid;
	int error;
	AMS_MUTEX_LOCK(&tof_chip->lock);
	//Go to appropriate IRQ handler depending on the app running
	appid = tof_chip->info_rec.record.app_id;

	switch (appid) {
	case TOF8801_APP_ID_BOOTLOADER:
		goto irq_handled;

	case TOF8801_APP_ID_APP0:
		(void)tof8801_get_register(tof_chip->client, TOF8801_INT_STAT, &int_stat);

		if (tof_chip->driver_debug) {
			(void)tof8801_get_register(tof_chip->client, TOF8801_TEST_REG1, &DATA1);
			(void)tof8801_get_register(tof_chip->client, TOF8801_TEST_REG2, &DATA2);
			tof_debug_info("IRQ stat: %#x 0X1c=%#x 0X1d=%#x \n", int_stat, DATA1, DATA2);
		}

		if (int_stat != 0) {
			//Clear interrupt on ToF chip
			error = tof8801_set_register(tof_chip->client, TOF8801_INT_STAT, int_stat);

			if (error)
				tof8801_app0_report_error(tof_chip, ERR_COMM, DEV_OK);

			tof8801_app0_process_irq(tof_chip, int_stat);
			/* Alert user space of changes */
			sysfs_notify(&tof_chip->client->dev.kobj,
				     tof_app0_group.name,
				     bin_attr_app0_tof_output.attr.name);
		}

		break;

	case TOF8801_APP_ID_APP1:
		goto irq_handled;
	}

irq_handled:
	AMS_MUTEX_UNLOCK(&tof_chip->lock);
	return IRQ_HANDLED;
}

int tof8801_app0_poll_irq_thread(void *tof_chip)
{
	struct tof_sensor_chip *chip = (struct tof_sensor_chip *)tof_chip;
	char meas_cmd = 0;
	int us_sleep = 0;
//  struct sched_param param = { .sched_priority = MAX_RT_PRIO - 1 };

//  sched_setscheduler(chip->app0_poll_irq, SCHED_FIFO, &param);
	AMS_MUTEX_LOCK(&chip->lock);
	// Poll period is interpreted in units of 100 usec
//  us_sleep = chip->poll_period * 100;
	us_sleep = chip->poll_period * 6600;
	tof_debug_info("Starting ToF irq polling thread, period: %u us\n", us_sleep);
	AMS_MUTEX_UNLOCK(&chip->lock);

	while (!kthread_should_stop()) {
		AMS_MUTEX_LOCK(&chip->lock);
		meas_cmd = chip->app0_app.cap_settings.cmd;
		AMS_MUTEX_UNLOCK(&chip->lock);

		if (meas_cmd)
			(void) tof_irq_handler(0, tof_chip);

		usleep_range(us_sleep, us_sleep + us_sleep / 10);
	}

	tof_debug_info("End app0_poll_irq_thread thread \n", us_sleep);
	return 0;
}

/**
 * tof_request_irq - request IRQ for given gpio
 *
 * @tof_chip: tof_sensor_chip pointer
 */
/*
static int tof_request_irq(struct tof_sensor_chip *tof_chip)
{
 int irq = tof_chip->client->irq;
 unsigned long default_trigger = irqd_get_trigger_type(irq_get_irq_data(irq));
 tof_debug_info( "irq: %d, trigger_type: %lu", irq, default_trigger);
 return devm_request_threaded_irq(&tof_chip->client->dev,
                                  tof_chip->client->irq,
                                  NULL, tof_irq_handler,
                                  default_trigger | IRQF_SHARED | IRQF_ONESHOT,
                                  tof_chip->client->name,
                                  tof_chip);
}
*/
/**
 * tof8801_get_all_regs - read all addressable I2C registers from device
 *
 * @tof_chip: tof_sensor_chip pointer
 */
static int tof8801_get_all_regs(struct tof_sensor_chip *tof_chip)
{
	int error;

	memset(tof_chip->shadow, 0, MAX_REGS);
	error = tof_i2c_read(tof_chip->client, TOF8801_APP_ID, tof_chip->shadow,
			     MAX_REGS);

	if (error < 0) {
		tof_debug_error("Read all registers failed: %d\n", error);
		return error;
	}

	return 0;
}

/**
 * tof8801_enable_interrupts - enable specified interrutps
 *
 * @tof_chip: tof_sensor_chip pointer
 * @int_en_flags: OR'd flags of interrupts to enable
 */
static int tof8801_enable_interrupts(struct tof_sensor_chip *chip,
				     char int_en_flags)
{
	char flags;
	int error = tof8801_get_register(chip->client, TOF8801_INT_EN, &flags);
	flags &= TOF8801_INT_MASK;
	flags |= int_en_flags;

	if (error) {
		tof_debug_warning(
			"Can't set tof8801 interrupts\n");
		return error;
	}

	return tof8801_set_register(chip->client, TOF8801_INT_EN, flags);
}

static int tof8801_get_config_calib_data(struct tof_sensor_chip *chip)
{
	int error;
	const struct firmware *config_fw = NULL;
	/* Set current configuration calibration data size to 0*/
	chip->config_data.size = 0;
	///***** Check for available fac_calib to read *****/
	error = request_firmware_direct(&config_fw,
					chip->pdata->config_calib_data_fname,
					&chip->client->dev);

	if (error || !config_fw) {
		tof_debug_warning(
			"configuration calibration data not available \'%s\': %d\n",
			chip->pdata->config_calib_data_fname, error);
		return 0;

	} else {
		tof_debug_info("Read in config_calib file: \'%s\'.\n",
			       chip->pdata->config_calib_data_fname);
	}

	if (config_fw->size > sizeof(chip->config_data.cfg_data)) {
		tof_debug_error(
			"Error: config calibration data size too large %ld > %lu (MAX)\n",
			config_fw->size, sizeof(chip->config_data.cfg_data));
		return 1;
	}

	memcpy((void *)&chip->config_data.cfg_data,
	       config_fw->data, config_fw->size);
	chip->config_data.size = config_fw->size;
	release_firmware(config_fw);
	return 0;
}

static int tof8801_firmware_download(struct tof_sensor_chip *chip, int startup)
{
	int error;
	struct timespec64 start_ts = {0}, end_ts = {0};
	int mutex_locked = mutex_is_locked(&chip->lock);
	int file_idx = 0;
	ktime_get_real_ts64(&start_ts);

	/* Iterate through all Firmware(s) to find one that works. 'Works' here is
	 * defined as running APP0 after FWDL
	 */
	for (file_idx = 0; chip->pdata->ram_patch_fname[file_idx] != NULL; file_idx++) {
		/*** reset completion event that FWDL is starting ***/
		reinit_completion(&chip->ram_patch_in_progress);

		if (mutex_locked)
			AMS_MUTEX_UNLOCK(&chip->lock);

		tof_debug_info("Trying firmware: \'%s\'...\n",
			       chip->pdata->ram_patch_fname[file_idx]);
		/***** Check for available firmware to load *****/
		error = request_firmware_nowait(THIS_MODULE, true,
						chip->pdata->ram_patch_fname[file_idx],
						&chip->client->dev, GFP_KERNEL, chip,
						tof_ram_patch_callback);

		if (error) {
			tof_debug_warning("Firmware not available \'%s\': %d\n",
					  chip->pdata->ram_patch_fname[file_idx], error);
		}

		if (!startup &&
				!wait_for_completion_interruptible_timeout(&chip->ram_patch_in_progress,
						msecs_to_jiffies(TOF_FWDL_TIMEOUT_MSEC))) {
			tof_debug_error("Timeout waiting for Ram Patch \'%s\' Complete",
					chip->pdata->ram_patch_fname[file_idx]);
		}

		if (mutex_locked)
			AMS_MUTEX_LOCK(&chip->lock);

		if (chip->info_rec.record.app_id == TOF8801_APP_ID_APP0) {
			tof_debug_info("APP0 is running");
			// assume we are done if APP0 is running
			break;
		}
	}

	ktime_get_real_ts64(&end_ts);
	tof_debug_error("FWDL callback %lu ms to finish",
			(timespec64_sub(end_ts, start_ts).tv_nsec / 1000000));
	// error if App0 is not running (fwdl failed)
	return (chip->info_rec.record.app_id != TOF8801_APP_ID_APP0) ? -EIO : 0;
}

static int tof_input_dev_open(struct input_dev *dev)
{
	struct tof_sensor_chip *chip = input_get_drvdata(dev);
	int error = 0;
	dev_info(&dev->dev, "%s\n", __func__);
	AMS_MUTEX_LOCK(&chip->lock);

	if (chip->pdata->gpiod_enable
			&& (gpiod_get_value(chip->pdata->gpiod_enable) == 0)) {
		/* enable the chip */
		error = gpiod_direction_output(chip->pdata->gpiod_enable, 1);

		if (error) {
			dev_err(&dev->dev, "Chip enable failed.\n");
			AMS_MUTEX_UNLOCK(&chip->lock);
			return -EIO;
		}
	}

	AMS_MUTEX_UNLOCK(&chip->lock);
	return error;
}
int start_poll_thread()
{
	struct tof_sensor_chip *chip = g_tof_sensor_chip;
	int error = 0;
	AMS_MUTEX_LOCK(&chip->irq_lock);

	if (chip->irq_thread_status == TOF_IRQ_THREAD_STOP) {
		/*** Use Polled I/O instead of interrupt ***/
		chip->app0_poll_irq = kthread_run(tof8801_app0_poll_irq_thread, (void *)chip,
						  "tof-irq_poll");

		if (IS_ERR(chip->app0_poll_irq)) {
			tof_debug_info("Error starting IRQ polling thread.\n");
			error = PTR_ERR(chip->app0_poll_irq);
			AMS_MUTEX_UNLOCK(&chip->irq_lock)
			return error;
		}

	} else
		tof_debug_error("Error starting IRQ polling thread.,thread already exit \n");

	chip->irq_thread_status = TOF_IRQ_THREAD_START;
	AMS_MUTEX_UNLOCK(&chip->irq_lock);
	return error ;
}
int tof_start_thread(void *tof_chip)
{
	struct tof_sensor_chip *chip = g_tof_sensor_chip;
	int error;
	int value = 900000;

	AMS_MUTEX_OEM_LOCK(&chip->power_lock);

	if (chip->power_status == TOF_POWER_ON) {
		AMS_MUTEX_OEM_UNLOCK(&chip->power_lock);
		tof_debug_error("start tof thread init!");

		error = tof_hard_reset(chip);

		if (error) {
			tof_debug_error("TOF Hrad reset failed !");
			AMS_MUTEX_OEM_UNLOCK(&chip->oem_lock);
			return error;
		}

		AMS_MUTEX_LOCK(&chip->lock);
		chip->app0_app.cap_settings.period = 66;
		//chip->app0_app.cap_settings.v2.alg = 0xA7;
		// we need to appropriately change the clock iteration counter
		//	when the capture iterations are changed to keep the time acceptable
		tof8801_app0_set_clk_iterations(chip, value);
		// chip takes iterations in 1000s
		value /= 1000;
		*((__le16 *)chip->app0_app.cap_settings.iterations) = cpu_to_le16(value);
		//error = tof8801_app0_capture((void *)chip, 1);
		AMS_MUTEX_UNLOCK(&chip->lock);

		tof_debug_error("end tof thread init done !  error = %d  ", error);

	} else {
		tof_debug_error("chip->power_status = %d  ", chip->power_status);
		AMS_MUTEX_OEM_UNLOCK(&chip->power_lock);
	}

	//AMS_MUTEX_OEM_UNLOCK(&chip->oem_lock);

	return 0;

}

int tof_oem_start()
{
	struct tof_sensor_chip *chip = g_tof_sensor_chip;
	/*** Use Polled I/O instead of interrupt ***/
	//int error = 0;
	int state;
	int error = 0;

	if (is_alread_probe == 1) {
		state = gpiod_get_value(chip->pdata->gpiod_enable) ? 1 : 0;

		if (state == 0) {
			gpiod_set_value(chip->pdata->gpiod_enable, 1);
			msleep(10);
			tof_debug_info("need set gpio enable again.\n");

		} else
			tof_debug_info("gpio enable state=%d.\n", state);

		/*gpiod_set_value(chip->pdata->gpiod_enable, 0);
		msleep(10);
		gpiod_set_value(chip->pdata->gpiod_enable, 1);
		msleep(10);*/

		AMS_MUTEX_OEM_LOCK(&chip->power_lock);
		chip->tof_power_down_thread_exit = TOF_POWER_THREAD_STOP;

		if (chip->power_status == TOF_POWER_OFF) {
			error = tof_power_up();

			if (error != 0) {
				tof_debug_error("power up failed.\n");
				AMS_MUTEX_OEM_UNLOCK(&chip->power_lock);
				return error;

			} else {
				chip->power_status = TOF_POWER_ON;
				tof_debug_info("power up success.\n");
			}

		} else
			tof_debug_info("tof have power up.\n");

		AMS_MUTEX_OEM_UNLOCK(&chip->power_lock);

		AMS_MUTEX_OEM_LOCK(&chip->oem_lock);
		tof_debug_info("start tof download-fw thread.\n");

		memset(&chip->app0_app, 0, sizeof(chip->app0_app));
		memset(&chip->BL_app, 0, sizeof(chip->BL_app));
		kfifo_reset(&chip->tof_output_fifo);
		tof_start_thread(chip);
		tof_debug_info("dataAlgorithmState=%d.dataConfiguration=%d.\n",
			       chip->app0_app.cal_update.dataAlgorithmState,
			       chip->app0_app.cal_update.dataConfiguration);
		/*
		chip->downloader_fw = kthread_run(tof_start_thread,(void *)chip,"tof-download-fw");
		if (IS_ERR(chip->downloader_fw)) {
			tof_debug_error("Error starting tof download-fw thread.\n");
			error = PTR_ERR(chip->app0_poll_irq);
			AMS_MUTEX_OEM_UNLOCK(&chip->oem_lock);
			return error;
		}
		*/
		tof_debug_info("end tof download-fw thread.\n");
		AMS_MUTEX_OEM_UNLOCK(&chip->oem_lock);

	}

	return 0;
}


int  tof_stop()
{
	struct tof_sensor_chip *chip = g_tof_sensor_chip;

	if (is_alread_probe == 1) {
		AMS_MUTEX_OEM_LOCK(&chip->oem_lock);
		tof8801_app0_capture(chip, 0);
		tof_debug_info("tof stop capture ");
		AMS_MUTEX_OEM_UNLOCK(&chip->oem_lock);
	}

	AMS_MUTEX_OEM_LOCK(&chip->power_lock);

	if (chip->power_status == TOF_POWER_ON
			&& chip->tof_power_down_thread_exit == TOF_POWER_THREAD_STOP) {
		chip->tof_power_down_thread_exit = TOF_POWER_THREAD_START;
		kthread_run(tof_power_down_thread, chip, "tof_power_down_thread");
		tof_debug_info("tof start power down");

	} else
		tof_debug_info("donot need do power down,power status=%d,thread_exit=%d",
			       chip->power_status, chip->tof_power_down_thread_exit);

	AMS_MUTEX_OEM_UNLOCK(&chip->power_lock);

	return 0;
}

int wait_for_tof_ready()
{

	struct i2c_client *client = tof_pdata.client;
	struct tof_sensor_chip *tof_chip = (struct tof_sensor_chip *)i2c_get_clientdata(
			client);
	int defualt_capture_iterations;

	int error = 0;

	AMS_MUTEX_OEM_LOCK(&tof_chip->power_lock);
	tof_chip->tof_power_down_thread_exit = TOF_POWER_THREAD_STOP;

	if (tof_chip->power_status == TOF_POWER_OFF) {
		error = tof_power_up();

		if (error != 0) {
			tof_debug_error("power up failed.\n");
			AMS_MUTEX_OEM_UNLOCK(&tof_chip->power_lock);
			return error;

		} else {
			tof_chip->power_status = TOF_POWER_ON;
			tof_debug_info("power up success.\n");
		}

	} else
		tof_debug_info("tof have power up.\n");

	AMS_MUTEX_OEM_UNLOCK(&tof_chip->power_lock);


	if (g_tof_sensor_chip != NULL
			&& tof_pdata.client != NULL) {
		is_alread_probe = 0 ;
		gpiod_set_value(g_tof_sensor_chip->pdata->gpiod_enable, 0);
		g_is_download_fw = 0;
		msleep(20);
		gpiod_set_value(g_tof_sensor_chip->pdata->gpiod_enable, 1);
		msleep(20);
		tof_debug_error("wait_for_tof_ready ok.\n");
		/***** Wait until ToF is ready for commands *****/
		error = tof_wait_for_cpu_startup(client);

		if (error) {
			tof_debug_error("I2C communication failure: %d\n", error);
			goto gen_err;
		}

		tof_chip->saved_clk_trim = UNINITIALIZED_CLK_TRIM_VAL;
		tof_chip->fuse_clk_trim  = UNINITIALIZED_CLK_TRIM_VAL;
		//read external (manufacturer) configuration data
		error = tof8801_get_config_calib_data(tof_chip);

		if (error)
			tof_debug_error("Error reading config data: %d\n", error);

		tof8801_app0_default_cap_settings(&tof_chip->app0_app);

		error = tof_init_info_record(tof_chip);

		if (error) {
			tof_debug_error("Read application info record failed.\n");
			goto gen_err;
		}

		/* enable all ToF interrupts on sensor */
		tof8801_enable_interrupts(tof_chip, IRQ_RESULTS | IRQ_DIAG | IRQ_ERROR);

		AMS_MUTEX_LOCK(&tof_chip->lock);
		/***** Make firmware download available to user space *****/
		error = tof8801_firmware_download(tof_chip, 0);

		if (error) {
			AMS_MUTEX_UNLOCK(&tof_chip->lock);
			return error;
		}

		/* enable all ToF interrupts on sensor */
		tof8801_enable_interrupts(tof_chip, IRQ_RESULTS | IRQ_DIAG | IRQ_ERROR);

		AMS_MUTEX_UNLOCK(&tof_chip->lock);

		error = tof8801_request_pinctrl(&(client->dev));

		if (error)
			tof_debug_warning("fail to request pinctrl,rc = %d", error);

		tof8801_enable_pinctrl(&(client->dev));
		// set defualt value
		tof_chip->app0_app.cap_settings.period = TOF_DEFUALT_PERIOD ;
		defualt_capture_iterations = TOF_DEFUALT_ITERATIONS;
		tof8801_app0_set_clk_iterations(tof_chip, defualt_capture_iterations);

		tof_debug_info("Set defualt period = %d clk_iterations = %d \n",
			       tof_chip->app0_app.cap_settings.period,
			       defualt_capture_iterations);
		is_alread_probe = 1 ;
		tof_debug_info("Caemra Tof Probe ok.\n");

	} else
		tof_debug_info("No probe tof8801 i2c device or have probe done ,check it\n");

	AMS_MUTEX_OEM_LOCK(&tof_chip->power_lock);

	if (tof_chip->power_status == TOF_POWER_ON
			&& tof_chip->tof_power_down_thread_exit == TOF_POWER_THREAD_STOP) {
		tof_chip->tof_power_down_thread_exit = TOF_POWER_THREAD_START;
		kthread_run(tof_power_down_thread, tof_chip, "tof_power_down_thread");
		tof_debug_info("tof start power down");

	} else
		tof_debug_info("tof have power down");

	AMS_MUTEX_OEM_UNLOCK(&tof_chip->power_lock);


	return 0;
gen_err:
	AMS_MUTEX_OEM_LOCK(&tof_chip->power_lock);

	if (tof_chip->power_status == TOF_POWER_ON
			&& tof_chip->tof_power_down_thread_exit == TOF_POWER_THREAD_STOP) {
		tof_chip->tof_power_down_thread_exit = TOF_POWER_THREAD_START;
		kthread_run(tof_power_down_thread, tof_chip, "tof_power_down_thread");
		tof_debug_info("tof start power down");

	} else
		tof_debug_info("tof have power down");

	AMS_MUTEX_OEM_UNLOCK(&tof_chip->power_lock);

	return error;
}

static int tof_probe(struct i2c_client *client, const struct i2c_device_id *idp)
{
	struct tof_sensor_chip *tof_chip;
	int error = 0;
	void *poll_prop_ptr = NULL;


	tof_debug_info("I2C Address: %#04x\n", client->addr);

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		tof_debug_error("I2C check functionality failed.\n");
		return -ENXIO;
	}

	tof_chip = devm_kzalloc(&client->dev, sizeof(*tof_chip), GFP_KERNEL);
	g_tof_sensor_chip = tof_chip;

	if (!tof_chip)
		return -ENOMEM;

//  tof_chip->vdd = regulator_get(&client->dev, "laser_vdd");
//  regulator_enable(tof_chip->vdd);

	/***** Setup data structures *****/
	mutex_init(&tof_chip->lock);
	mutex_init(&tof_chip->oem_lock);
	mutex_init(&tof_chip->power_lock);
	mutex_init(&tof_chip->irq_lock);

	client->dev.platform_data = (void *)&tof_pdata;
	tof_chip->client = client;
	tof_chip->pdata = &tof_pdata;
	tof_pdata.client = client;
	tof_chip->power_status = TOF_POWER_OFF;
	tof_chip->irq_thread_status = TOF_IRQ_THREAD_STOP;
	tof_chip->tof_power_down_thread_exit = TOF_POWER_THREAD_STOP;
	i2c_set_clientdata(client, tof_chip);
	/***** Firmware sync structure initialization*****/
	init_completion(&tof_chip->ram_patch_in_progress);
	//initialize kfifo for frame output
	INIT_KFIFO(tof_chip->tof_output_fifo);
	//Setup measure timer
	timer_setup(&tof_chip->meas_timer, tof8801_app0_measure_timer_expiry_callback,
		    0);

#if 0
	setup_timer(&tof_chip->meas_timer,
		    tof8801_app0_measure_timer_expiry_callback,
		    (unsigned long) tof_chip);
#endif
	//Setup input device
	tof_chip->obj_input_dev = devm_input_allocate_device(&client->dev);

	if (tof_chip->obj_input_dev == NULL) {
		tof_debug_error("Error allocating input_dev.\n");
		goto input_dev_alloc_err;
	}

	tof_chip->obj_input_dev->name = tof_chip->pdata->tof_name;
	tof_chip->obj_input_dev->id.bustype = BUS_I2C;
	input_set_drvdata(tof_chip->obj_input_dev, tof_chip);
	tof_chip->obj_input_dev->open = tof_input_dev_open;
	set_bit(EV_ABS, tof_chip->obj_input_dev->evbit);
	input_set_abs_params(tof_chip->obj_input_dev, ABS_DISTANCE, 0, 0xFF, 0, 0);
	error = input_register_device(tof_chip->obj_input_dev);

	if (error) {
		tof_debug_error("Error registering input_dev.\n");
		goto input_reg_err;
	}

	error = tof_get_gpio_config(tof_chip);

	if (error)
		goto gpio_err;

	/***** Set ChipEnable HIGH *****/
	if (tof_chip->pdata->gpiod_enable) {
		/* enable the chip */
		error = gpiod_direction_output(tof_chip->pdata->gpiod_enable, 1);

		if (error) {
			tof_debug_error("Chip enable failed.\n");
			goto gpio_err;
		}
	}

	poll_prop_ptr = (void *)of_get_property(tof_chip->client->dev.of_node,
						TOF_PROP_NAME_POLLIO,
						NULL);
	tof_chip->poll_period = poll_prop_ptr ? be32_to_cpup(poll_prop_ptr) : 0;
	/*
	  if (tof_chip->pdata->gpiod_interrupt) {
	    error = tof_request_irq(tof_chip);
	    if (error) {
	      tof_debug_error( "Interrupt request Failed.\n");
	      goto gen_err;
	    }
	  }

	} else {
	  tof_chip->app0_poll_irq = kthread_run(tof8801_app0_poll_irq_thread,
	                                        (void *)tof_chip,
	                                        "tof-irq_poll");
	  if (IS_ERR(tof_chip->app0_poll_irq)) {
	    tof_debug_error( "Error starting IRQ polling thread.\n");
	    error = PTR_ERR(tof_chip->app0_poll_irq);
	    goto kthread_start_err;
	  }
	}
	*/
#if 0
	/***** Wait until ToF is ready for commands *****/
	error = tof_wait_for_cpu_startup(client);

	if (error) {
		tof_debug_error("I2C communication failure: %d\n", error);
		goto gen_err;
	}

	tof_chip->saved_clk_trim = UNINITIALIZED_CLK_TRIM_VAL;
	//read external (manufacturer) configuration data
	error = tof8801_get_config_calib_data(tof_chip);

	if (error)
		tof_debug_error("Error reading config data: %d\n", error);

	tof8801_app0_default_cap_settings(&tof_chip->app0_app);

	error = tof_init_info_record(tof_chip);

	if (error) {
		tof_debug_error("Read application info record failed.\n");
		goto gen_err;
	}

#endif


	cam_tof_kobj = kobject_create_and_add("tof_control", kernel_kobj);

	error = sysfs_create_groups(cam_tof_kobj, tof_groups);

	if (error) {
		tof_debug_error("Error creating sysfs attribute group.\n");
		goto sysfs_err;
	}

	/* enable all ToF interrupts on sensor */
	tof8801_enable_interrupts(tof_chip, IRQ_RESULTS | IRQ_DIAG | IRQ_ERROR);
	tof_debug_info("Probe ok.\n");

	return 0;

	/***** Failure case(s), unwind and return error *****/
sysfs_err:
	sysfs_remove_groups(cam_tof_kobj, tof_groups);
	/*gen_err:
	  if (tof_chip->poll_period != 0) {
	    (void)kthread_stop(tof_chip->app0_poll_irq);
	  }*/
gpio_err:

	if (tof_chip->pdata->gpiod_enable)
		(void) gpiod_direction_output(tof_chip->pdata->gpiod_enable, 0);

//kthread_start_err:
input_dev_alloc_err:
input_reg_err:
	i2c_set_clientdata(client, NULL);
	tof_debug_info("Probe failed.\n");
	return error;
}

static int tof_suspend(struct device *dev)
{
	struct tof_sensor_chip *chip = g_tof_sensor_chip;
	tof_debug_info("%s\n", __func__);
	AMS_MUTEX_LOCK(&chip->lock);

	if (!chip->pdata->gpiod_enable) {
		AMS_MUTEX_UNLOCK(&chip->lock);
		return -EIO;
	}

	gpiod_set_value(chip->pdata->gpiod_enable, 0);
	g_is_download_fw = 0;
//EXIT_ON_RELEASE_TOF_FAILURE;
	AMS_MUTEX_UNLOCK(&chip->lock);
	return 0;
}

static int tof_resume(struct device *dev)
{
	struct tof_sensor_chip *chip = g_tof_sensor_chip;
	int error = 0;
	tof_debug_info("%s\n", __func__);
	AMS_MUTEX_LOCK(&chip->lock);

//EXIT_ON_ACQUIRE_TOF_FAILURE;
	if (!chip->pdata->gpiod_enable) {
		AMS_MUTEX_UNLOCK(&chip->lock);
		return -EIO;
	}

// do not resume
//  error = tof_hard_reset(chip);
	if (error) {
		tof_debug_error("Error issuing Reset-HARD");
		AMS_MUTEX_UNLOCK(&chip->lock);
		return -EIO;
	}

	AMS_MUTEX_UNLOCK(&chip->lock);
	return 0;
}

static int tof_remove(struct i2c_client *client)
{
	struct tof_sensor_chip *chip = i2c_get_clientdata(client);
	char int_stat = 0;

	if (chip->info_rec.record.app_id == TOF8801_APP_ID_APP0) {
		//Stop any current measurements
		tof8801_app0_capture(chip, 0);
		(void)tof8801_get_register(chip->client, TOF8801_INT_STAT, &int_stat);

		if (int_stat != 0) {
			//Clear any interrupt status
			(void) tof8801_set_register(chip->client,
						    TOF8801_INT_STAT, int_stat);
		}
	}

	if (chip->pdata->gpiod_interrupt) {
		devm_free_irq(&client->dev, client->irq, chip);
		devm_gpiod_put(&client->dev, chip->pdata->gpiod_interrupt);
	}

	AMS_MUTEX_OEM_LOCK(&chip->power_lock);

	if (chip->power_status == TOF_POWER_ON
			&& chip->tof_power_down_thread_exit == TOF_POWER_THREAD_STOP) {
		chip->tof_power_down_thread_exit = TOF_POWER_THREAD_START;
		kthread_run(tof_power_down_thread, chip, "tof_power_down_thread");
		tof_debug_info("tof start power down");

	} else
		tof_debug_info("tof have power down");

	AMS_MUTEX_OEM_UNLOCK(&chip->power_lock);

	if (chip->pdata->gpiod_enable) {
		/* disable the chip */
		(void) gpiod_direction_output(chip->pdata->gpiod_enable, 0);
		devm_gpiod_put(&client->dev, chip->pdata->gpiod_enable);
	}

	sysfs_remove_groups(&client->dev.kobj,
			    (const struct attribute_group **)&tof_groups);
	del_timer_sync(&chip->meas_timer); //delete measure timer
	tof_debug_info("%s\n", __func__);
	i2c_set_clientdata(client, NULL);

	return 0;
}

static struct i2c_device_id tof_idtable[] = {
	{ "tof8801", 0 },
	{}
};
MODULE_DEVICE_TABLE(i2c, tof_idtable);

static const struct dev_pm_ops tof_pm_ops = {
	.suspend = tof_suspend,
	.resume  = tof_resume,
};

static const struct of_device_id tof_of_match[] = {
	{ .compatible = "ams,tof8801" },
	{ }
};
MODULE_DEVICE_TABLE(of, tof_of_match);

static struct i2c_driver tof_driver = {
	.driver = {
		.name = "ams-tof",
		.pm = &tof_pm_ops,
		.of_match_table = of_match_ptr(tof_of_match),
	},
	.id_table = tof_idtable,
	.probe = tof_probe,
	.remove = tof_remove,
};

int cam_tof8801_driver_init(void)
{
	int rc;

	if (is_tof_use_cci_i2c()) {
		rc = platform_driver_register(&tof_pltf_driver);

		if (rc) {
			CAM_ERR(CAM_TOF, "platform_driver_register failed rc = %d", rc);
			return rc;
		}

		tof_registered_driver.platform_driver = 1;
	}

	rc = i2c_add_driver(&tof_driver);

	if (rc) {
		CAM_ERR(CAM_TOF, "i2c_add_driver failed rc = %d", rc);
		return rc;
	}

	tof_registered_driver.i2c_driver = 1;
	return 0;
}

void cam_tof8801_driver_exit(void)
{
	if (tof_registered_driver.i2c_driver) {
		i2c_del_driver(&tof_driver);
		tof_registered_driver.i2c_driver = 0;
	}

	if (tof_registered_driver.platform_driver) {
		platform_driver_unregister(&tof_pltf_driver);
		tof_registered_driver.platform_driver = 0;
	}
}

#if 0
module_i2c_driver(tof_driver);
#else
//module_init(cam_tof8801_driver_init);
//module_exit(cam_tof8801_driver_exit);
#endif

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("AMS-TAOS tmf8801 ToF sensor driver");
MODULE_VERSION("3.11");
