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
 *  \file tof8801_pdv.c - ToF8801 platform driver
 */

#include "tof8801_pdrv.h"
#include "tof8801_driver.h"

#include "cam_cci_ctrl_interface.h"
#include "camera_main.h"
#include "cam_debug_util.h"
#include "cam_req_mgr_dev.h"


struct cam_tof_ctrl_t *g_tof_ctrl = NULL;
struct cam_tof_registered_driver_t tof_registered_driver = { 0, 0 };

static int cam_tof_init_power_info(struct cam_sensor_power_ctrl_t *power_info)
{
    int rc = 0;

    TOF_ASSERT_PARA(power_info);

    /* tof8801 chip are always powered on by 3v3 supply in the case,
       fill one to avoid api call failure */
    power_info->power_setting_size = 1;
    power_info->power_setting = (struct cam_sensor_power_setting *)
                       kzalloc(sizeof(struct cam_sensor_power_setting), GFP_KERNEL);
    if (!power_info->power_setting)
        return -ENOMEM;

    power_info->power_setting[0].seq_type = SENSOR_VIO;
    power_info->power_setting[0].seq_val = 0;
    power_info->power_setting[0].delay = 0;

    power_info->power_down_setting_size = 1;
    power_info->power_down_setting = (struct cam_sensor_power_setting *)
                       kzalloc(sizeof(struct cam_sensor_power_setting), GFP_KERNEL);
    if (!power_info->power_down_setting)
    {
        rc = -ENOMEM;
        goto free_power_settings;
    }

    power_info->power_down_setting[0].seq_type = SENSOR_VIO;
    power_info->power_down_setting[0].seq_val = 0;
    power_info->power_down_setting[0].delay = 0;

    return rc;

free_power_settings:
    kfree(power_info->power_setting);
    power_info->power_setting = NULL;
    power_info->power_setting_size = 0;
    power_info->power_down_setting = NULL;
    power_info->power_down_setting_size = 0;
    return rc;
}

static int cam_tof_power_up(struct cam_tof_ctrl_t *tof_ctrl)
{
    int rc = 0;
    struct cam_hw_soc_info *soc_info;
    struct cam_sensor_power_ctrl_t *power_info;

    TOF_ASSERT_PARA(tof_ctrl);

    soc_info =	&tof_ctrl->soc_info;
    power_info = &tof_ctrl->power_info;

    /* Parse and fill vreg params for power up settings */
    rc = msm_camera_fill_vreg_params(soc_info, power_info->power_setting,
                                     power_info->power_setting_size);
    if (rc)
    {
        CAM_ERR(CAM_TOF, "failed to fill vreg params for power up rc:%d", rc);
        return rc;
    }

    /* Parse and fill vreg params for power down settings*/
    rc = msm_camera_fill_vreg_params(soc_info, power_info->power_down_setting,
                                     power_info->power_down_setting_size);
    if (rc)
    {
        CAM_ERR(CAM_TOF, "failed to fill vreg params for power down rc:%d", rc);
        return rc;
    }

    power_info->dev = soc_info->dev;

    rc = cam_sensor_core_power_up(power_info, soc_info);
    if (rc)
    {
        CAM_ERR(CAM_TOF, "failed in tof power up rc %d", rc);
        return rc;
    }

    rc = camera_io_init(&tof_ctrl->io_master_info);
    if (rc)
        CAM_ERR(CAM_TOF, "cci_init failed: rc: %d", rc);

    return rc;
}

static int cam_tof_power_down(struct cam_tof_ctrl_t *tof_ctrl)
{
    int rc = 0;
    struct cam_hw_soc_info *soc_info;
    struct cam_sensor_power_ctrl_t *power_info;

    TOF_ASSERT_PARA(tof_ctrl);

    soc_info =	&tof_ctrl->soc_info;
    power_info = &tof_ctrl->power_info;

    rc = cam_sensor_util_power_down(power_info, soc_info);
    if (rc)
    {
        CAM_ERR(CAM_TOF, "power down the core is failed:%d", rc);
        return rc;
    }

    camera_io_release(&tof_ctrl->io_master_info);

    return rc;
}

static int cam_tof_acquire(struct cam_tof_ctrl_t *tof_ctrl)
{
    int rc = 0;
    struct camera_cci_transfer ccit;

    TOF_ASSERT_PARA(tof_ctrl);

    mutex_lock(&tof_ctrl->mutex);

    if (tof_ctrl->state == CAM_TOF_ACQUIRED)
    {
        CAM_ERR(CAM_TOF, "tof already acquired");
        rc = -EALREADY;
        goto unlock_mutex;
    }

    memset(&ccit,0,sizeof(ccit));
    ccit.cmd = CAMERA_CCI_INIT;
    cam_cci_control_interface(&ccit);

    rc = cam_tof_power_up(tof_ctrl);
    if (rc)
    {
        CAM_ERR(CAM_TOF, "fail to power up tof");
        goto unlock_mutex;
    }
    tof_ctrl->state = CAM_TOF_ACQUIRED;

    CAM_INFO(CAM_TOF, "cam_tof_acquire");
unlock_mutex:
    mutex_unlock(&tof_ctrl->mutex);
    return rc;
}

static int cam_tof_release(struct cam_tof_ctrl_t *tof_ctrl)
{
    int rc = 0;
	struct camera_cci_transfer ccit;

    TOF_ASSERT_PARA(tof_ctrl);

    mutex_lock(&tof_ctrl->mutex);

    if (tof_ctrl->state == CAM_TOF_RELEASED)
    {
        CAM_ERR(CAM_TOF, "tof already released\n");
        rc = -EALREADY;
        goto unlock_mutex;
    }

	memset(&ccit,0,sizeof(ccit));
	ccit.cmd = CAMERA_CCI_RELEASE;
	cam_cci_control_interface(&ccit);

//  regulator_put(tof_ctrl->vdd);
    rc = cam_tof_power_down(tof_ctrl);
    if (rc < 0) 
    {
        CAM_ERR(CAM_TOF, "fail to power down tof\n");
        goto unlock_mutex;
    }
    tof_ctrl->state  = CAM_TOF_RELEASED;

    CAM_INFO(CAM_TOF, "cam_tof_release");
unlock_mutex:
    mutex_unlock(&tof_ctrl->mutex);
    return rc;
}

static int cam_tof_init_io_master(struct cam_tof_ctrl_t *tof_ctrl)
{
	struct cam_sensor_cci_client *cci_client = NULL;

    TOF_ASSERT_PARA(tof_ctrl);

    cci_client = kzalloc(sizeof(struct cam_sensor_cci_client), GFP_KERNEL);
    if (!cci_client)
        return -ENOMEM;

    cci_client->cci_i2c_master = MASTER_1;
    cci_client->cci_device = CCI_DEVICE_1;
    cci_client->sid = (0x82>>1);
    cci_client->retries = 3;
    cci_client->id_map = 0;
    cci_client->i2c_freq_mode = I2C_FAST_PLUS_MODE;

    tof_ctrl->io_master_info.master_type = CCI_MASTER;
    tof_ctrl->io_master_info.cci_client = cci_client;

    return 0;
}

static int cam_tof_get_dt_data(struct cam_tof_ctrl_t *tof_ctrl)
{
    int rc = 0;
    struct cam_hw_soc_info *soc_info;
    struct device_node *of_node = NULL;
    struct cam_sensor_cci_client *cci_client = NULL;
    uint32_t slave_id = 0;

    TOF_ASSERT_PARA(tof_ctrl);

    soc_info = &tof_ctrl->soc_info;
    of_node = soc_info->dev->of_node;
    if (!of_node)
    {
        CAM_ERR(CAM_TOF, "of_node is NULL");
        return -EINVAL;
    }

    /* get cci-i2c config data */
    cci_client = tof_ctrl->io_master_info.cci_client;
    rc = of_property_read_u32(of_node, "cci-master", &cci_client->cci_i2c_master);
    if (rc < 0)
    {
        CAM_ERR(CAM_TOF, "failed to read dt node of cci-master , rc= %d", rc);
        return rc;
    }

    rc = of_property_read_u32(of_node, "reg", &slave_id);
    if (rc < 0)
    {
        CAM_ERR(CAM_TOF, "failed to read dt node of reg , rc= %d", rc);
        return rc;
    }
    cci_client->sid = (uint16_t)(slave_id >> 1 & (0xFFFF));
    CAM_ERR(CAM_TOF, "dtsi cci_client->sid= 0x%x", cci_client->sid);

    rc = cam_soc_util_get_dt_properties(soc_info);
    if (rc < 0)
    {
        CAM_ERR(CAM_TOF, "cam_soc_util_get_dt_properties rc %d", rc);
        return rc;
    }

    return rc;
}

static int cam_tof_component_bind(struct device *dev,
	struct device *master_dev, void *data)
{
    int32_t rc = 0;
    struct cam_tof_ctrl_t *tof_ctrl = NULL;
    struct platform_device *pdev = to_platform_device(dev);

    tof_ctrl = kzalloc(sizeof(struct cam_tof_ctrl_t), GFP_KERNEL);
    if (!tof_ctrl)
    {
        CAM_ERR(CAM_TOF, "fail to alloc mem for cam tof ctrl");
        return -ENOMEM;
    }

    tof_ctrl->soc_info.pdev = pdev;
    tof_ctrl->pdev = pdev;
    tof_ctrl->soc_info.dev = &pdev->dev;
    tof_ctrl->soc_info.dev_name = pdev->name;
    tof_ctrl->state = CAM_TOF_RELEASED;
    platform_set_drvdata(pdev, tof_ctrl);
    mutex_init(&(tof_ctrl->mutex));
    mutex_init(&(tof_ctrl->cci_i2c_mutex));

    rc = cam_tof_init_io_master(tof_ctrl);
    if (rc != 0)
    {
        CAM_ERR(CAM_TOF, "fail to alloc mem for cci_client of cam tof ctrl");
        goto free_tof_ctrl;
    }

    rc = cam_tof_get_dt_data(tof_ctrl);
    if (rc)
    {
        CAM_ERR(CAM_TOF, "fail to get data from dt for tof, rc= %d", rc);
        goto free_cci_client;
    }

    rc = cam_tof_init_power_info(&tof_ctrl->power_info);
//  tof_ctrl->vdd = regulator_get(&pdev->dev, "laser_vdd");

    if (rc)
    {
        CAM_ERR(CAM_TOF, "fail to init power info for tof, rc= %d", rc);
        goto free_cci_client;
    }

    /* save the control pointer for device cci-i2c accessing by i2c bus driver */
    g_tof_ctrl = tof_ctrl;
    CAM_INFO(CAM_TOF, "tof8801 probe success");

    return rc;

free_cci_client:
    kfree(tof_ctrl->io_master_info.cci_client);

free_tof_ctrl:
    kfree(tof_ctrl);

    return rc;
}

static void cam_tof_component_unbind(struct device *dev,
	struct device *master_dev, void *data)
{
    struct cam_tof_ctrl_t *tof_ctrl;
    struct cam_sensor_power_ctrl_t *power_info;
    struct cam_hw_soc_info *soc_info;
    struct platform_device *pdev = to_platform_device(dev);

    tof_ctrl = platform_get_drvdata(pdev);
    if (!tof_ctrl)
    {
        CAM_ERR(CAM_TOF, "tof device is NULL");
        return ;
    }

    soc_info = &tof_ctrl->soc_info;
    power_info = &tof_ctrl->power_info;

    kfree(power_info->power_setting);
    kfree(power_info->power_down_setting);
    power_info->power_setting = NULL;
    power_info->power_down_setting = NULL;
    kfree(tof_ctrl->io_master_info.cci_client);
    kfree(tof_ctrl);
    g_tof_ctrl = NULL;

}

const static struct component_ops cam_tof_component_ops = {
	.bind = cam_tof_component_bind,
	.unbind = cam_tof_component_unbind,
};

static int32_t cam_tof_pltf_probe(
	struct platform_device *pdev)
{
	int rc = 0;

	CAM_DBG(CAM_TOF, "Adding tof component");
	rc = component_add(&pdev->dev, &cam_tof_component_ops);
	if (rc)
		CAM_ERR(CAM_TOF, "failed to add component rc: %d", rc);

	return rc;
}

static int cam_tof_pltf_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &cam_tof_component_ops);
	return 0;
}

static const struct of_device_id tof_pltf_dt_match[] = {
    { .compatible = "ams,pltf_tof8801" },
    { }
};

MODULE_DEVICE_TABLE(of, tof_pltf_dt_match);
struct platform_driver tof_pltf_driver = {
    .driver = {
        .name = "ams,pltf_tof8801",
        .owner = THIS_MODULE,
        .of_match_table = tof_pltf_dt_match,
    },
    .probe = cam_tof_pltf_probe,
    .remove = cam_tof_pltf_remove,
};

int is_tof_use_cci_i2c(void)
{
    return 1;
}

int tof_cci_i2c_acquire(void)
{
    if (is_tof_use_cci_i2c())
        return cam_tof_acquire(g_tof_ctrl);
    else
        return 0;
}

int tof_cci_i2c_release(void)
{
    if (is_tof_use_cci_i2c())
        return cam_tof_release(g_tof_ctrl);
    else
        return 0;
}

int tof_power_up(void)
{
        CAM_ERR(CAM_TOF, "do tof_power_up \n");
        if (tof_cci_i2c_acquire())
        {
            CAM_ERR(CAM_TOF, "fail to acquire tof cci i2c.\n");
            return -1;
        }
        return 0;
}

int tof_power_down(void)
{
        CAM_ERR(CAM_TOF, "do tof_power_down \n");
        if (tof_cci_i2c_release())
        {
            CAM_ERR(CAM_TOF, "fail to release tof cci i2c.\n");
            return -1;
        }
        return 0;
}

int tof_cci_i2c_read(char reg, char *buf, int len)
{
    int ret = -1;
    struct camera_cci_transfer ccit;

    TOF_ASSERT_PARA(g_tof_ctrl);
    TOF_ASSERT_PARA(buf);

    mutex_lock(&g_tof_ctrl->cci_i2c_mutex);
#if 0
    g_tof_ctrl->io_master_info.cci_client->cci_device=CCI_DEVICE_1;
    CAM_ERR(CAM_TOF, "sid=0x%x cci_num=%d  %d cci-matser=%d",g_tof_ctrl->io_master_info.cci_client->sid,g_tof_ctrl->io_master_info.cci_client->cci_device,CCI_DEVICE_1,g_tof_ctrl->io_master_info.cci_client->cci_i2c_master);
    ret = camera_io_dev_read_seq(&g_tof_ctrl->io_master_info,
	                             (uint32_t)reg,
	                             (uint8_t *)buf,
	                             CAMERA_SENSOR_I2C_TYPE_BYTE,
	                             CAMERA_SENSOR_I2C_TYPE_BYTE,
	                             len);

#else
    memset(&ccit,0,sizeof(ccit));
    ccit.cmd = CAMERA_CCI_READ;
    ccit.addr = reg;
    ccit.data = buf;
    ccit.count = len;
    ret = cam_cci_control_interface(&ccit);
#endif

    if (ret == 0)
    {
        /* correspond to "ARRAY_SIZE(msgs)" in tof_i2c_read */
        ret = 2;
    }

    mutex_unlock(&g_tof_ctrl->cci_i2c_mutex);
    return ret;
}

int tof_cci_i2c_write(char reg, char *buf, int len)
{
    int ret = -1;
    //int cnt;
    //struct cam_sensor_i2c_reg_setting i2c_reg_setting;
    struct camera_cci_transfer ccit;

    TOF_ASSERT_PARA(g_tof_ctrl);
    TOF_ASSERT_PARA(buf);

    mutex_lock(&g_tof_ctrl->cci_i2c_mutex);
    if (len <= MAX_CCI_WRITE_DATA_NUM)
    {
#if 0

        i2c_reg_setting.addr_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
        i2c_reg_setting.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
        i2c_reg_setting.size = len;
        i2c_reg_setting.delay = 0;

        i2c_reg_setting.reg_setting = (struct cam_sensor_i2c_reg_array *)
                                      (g_tof_ctrl->w_buf);
        for (cnt = 0; cnt < len; cnt++)
        {
            i2c_reg_setting.reg_setting[cnt].reg_addr = reg;
            i2c_reg_setting.reg_setting[cnt].reg_data = *(buf + cnt);
            i2c_reg_setting.reg_setting[cnt].delay = 0;
            i2c_reg_setting.reg_setting[cnt].data_mask = 0;
        }
        g_tof_ctrl->io_master_info.cci_client->cci_device=CCI_DEVICE_1;
        CAM_ERR(CAM_TOF, "sid=0x%x cci_num=%d %d",g_tof_ctrl->io_master_info.cci_client->sid,g_tof_ctrl->io_master_info.cci_client->cci_device,CCI_DEVICE_1);
        ret = camera_io_dev_write_continuous(&g_tof_ctrl->io_master_info,
                                            &i2c_reg_setting,
                                            1);
#else
        memset(&ccit,0,sizeof(ccit));
        ccit.cmd = CAMERA_CCI_WRITE;
        ccit.addr = reg;
        ccit.data = buf;
        ccit.count = len;
        ret = cam_cci_control_interface(&ccit);
#endif

        if (ret == 0)
            ret = 1;

    }
    else
    {
      ret = -1;
      CAM_ERR(CAM_TOF, "Failed, execeed max defined write data len: %d ", len);
    }

    mutex_unlock(&g_tof_ctrl->cci_i2c_mutex);
    return ret;
}

