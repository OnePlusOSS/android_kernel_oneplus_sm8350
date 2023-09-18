#define pr_fmt(fmt) "OPLUS_CHG[PMIC]: %s[%d]: " fmt, __func__, __LINE__

#include <linux/delay.h>
#ifdef CONFIG_OPLUS_CHG_OOS
#include <linux/oem/oplus_chg.h>
#else
#include <linux/oplus_chg.h>
#endif
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_platform.h>
#include <linux/iio/consumer.h>
#include <linux/regmap.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/rtc.h>
#include <linux/proc_fs.h>
#include <linux/iio/consumer.h>
#include <linux/kthread.h>
#include "oplus_sm8350.h"
#include "../oplus_charger.h"
#include "../oplus_gauge.h"
#include "../oplus_warp.h"
#include "../oplus_short.h"
#include "../charger_ic/oplus_short_ic.h"
#include "../oplus_adapter.h"
#include "../oplus_chg_module.h"
#include "../oplus_thermal.h"

static bool usb_online;
static bool usb_present;
static bool wls_online;
static bool probe_done;
static bool pon_by_chg = false;
static bool disable_usb_chg;

struct oplus_chg_chip *g_oplus_chip = NULL;
static int qpnp_get_prop_charger_voltage_now(void);
static int oplus_enable_haptic_boost(struct battery_chg_dev *dev, bool enable);
static int get_usb_temp(struct battery_chg_dev *bcdev);
static int oplus_chg_get_batt_temp(struct battery_chg_dev *bcdev, int *temp);
static int oplus_chg_get_skin_temp(struct battery_chg_dev *bcdev, int *skin_temp);
static int oplus_chg_get_batt_current(struct battery_chg_dev *bcdev, int *batt_current);
static int oplus_chg_get_real_soc(struct battery_chg_dev *bcdev, int *soc);
static void oplus_disconnect_vbus(bool enable);
static void oplus_update_usb_type(void);
static int smbchg_usb_suspend_enable(void);
static int smbchg_usb_suspend_disable(void);
static int smbchg_charging_disable(void);
static bool oplus_set_otg_switch_status(bool enable);
static int oplus_otg_ap_enable(bool enable);
#ifndef CONFIG_OPLUS_CHG_OOS
int oplus_get_usb_status(void);
#endif

__maybe_unused static bool is_usb_ocm_available(struct battery_chg_dev *dev)
{
	if (!dev->ocm_list[OCM_TYPE_USB].ocm)
		dev->ocm_list[OCM_TYPE_USB].ocm = oplus_chg_mod_get_by_name("usb");
	return !!dev->ocm_list[OCM_TYPE_USB].ocm;
}

__maybe_unused static bool is_wls_ocm_available(struct battery_chg_dev *dev)
{
	if (!dev->ocm_list[OCM_TYPE_WLS].ocm)
		dev->ocm_list[OCM_TYPE_WLS].ocm = oplus_chg_mod_get_by_name("wireless");
	return !!dev->ocm_list[OCM_TYPE_WLS].ocm;
}

__maybe_unused static bool is_batt_ocm_available(struct battery_chg_dev *dev)
{
	if (!dev->ocm_list[OCM_TYPE_BATTERY].ocm)
		dev->ocm_list[OCM_TYPE_BATTERY].ocm = oplus_chg_mod_get_by_name("battery");
	return !!dev->ocm_list[OCM_TYPE_BATTERY].ocm;
}

__maybe_unused static bool is_comm_ocm_available(struct battery_chg_dev *dev)
{
	if (!dev->ocm_list[OCM_TYPE_COMM].ocm)
		dev->ocm_list[OCM_TYPE_COMM].ocm = oplus_chg_mod_get_by_name("common");
	return !!dev->ocm_list[OCM_TYPE_COMM].ocm;
}

static int oplus_dev_match_name(struct device *dev, void *name)
{
	if (dev->of_node != NULL && !strcmp(dev->of_node->name, name)) {
		pr_info("dev %s found!!\n", name);
		return 1;
	} else {
		pr_debug("name = %s\n", dev->of_node->name);
	}

	return 0;
}

static struct regmap *oplus_chg_get_pm8350_regmap(struct battery_chg_dev *bcdev)
{
	struct device_node *node = bcdev->dev->of_node;
	int device_depth;
	int regmap_device_depth;
	char *path_name;
	struct device *dev_temp;
	int i;
	int rc;

	if (bcdev->pm8350_regmap != NULL)
		return bcdev->pm8350_regmap;

	rc = of_property_read_u32(node, "oplus,device-depth", &device_depth);
	if (rc < 0) {
		pr_err("can't get oplus,device-depth, rc=%d\n", rc);
		return NULL;
	}

	dev_temp = bcdev->dev;
	for (i = 0; i < device_depth; i++) {
		if (dev_temp->parent == NULL) {
			pr_err("can't find %s parent\n", dev_temp->of_node->name);
			return NULL;
		}
		dev_temp = dev_temp->parent;
	}

	regmap_device_depth = of_property_count_strings(node, "oplus,pm8350-path-name");
	if (regmap_device_depth <= 0) {
		pr_err("read oplus,pm8350-path-name error, rc=%d\n", regmap_device_depth);
		return NULL;
	}

	for (i = 0; i < regmap_device_depth; i++) {
		rc = of_property_read_string_index(node, "oplus,pm8350-path-name", i,
						   (const char **)&path_name);
		if (rc < 0) {
			pr_err("Cannot parse oplus,pm8350-path-name: %d\n", rc);
			return NULL;
		}
		dev_temp = device_find_child(dev_temp, path_name, oplus_dev_match_name);
		if (dev_temp == NULL) {
			pr_err("dev %s not found!!", path_name);
			return NULL;
		}
	}

	bcdev->pm8350_regmap = dev_get_regmap(dev_temp, NULL);

	return bcdev->pm8350_regmap;
}

static void oplus_otg_init_status_func(struct work_struct *work)
{
	printk(KERN_ERR "!!!!oplus_otg_init_status_func\n");
	oplus_otg_ap_enable(true);
}

static bool oplus_chg_is_wls_online(struct battery_chg_dev *dev)
{
	union oplus_chg_mod_propval pval;
	int rc;

	if (!is_wls_ocm_available(dev)) {
		pr_debug("wls ocm not found\n");
		return false;
	}
	rc = oplus_chg_mod_get_property(dev->ocm_list[OCM_TYPE_WLS].ocm, OPLUS_CHG_PROP_ONLINE, &pval);
	if (rc < 0)
		return false;

	return !!pval.intval;
}

static bool oplus_chg_is_usb_online(struct battery_chg_dev *dev)
{
	union oplus_chg_mod_propval pval;
	int rc;

	if (!is_usb_ocm_available(dev)) {
		pr_debug("usb ocm not found\n");
		return false;
	}
	rc = oplus_chg_mod_get_property(dev->ocm_list[OCM_TYPE_USB].ocm, OPLUS_CHG_PROP_ONLINE, &pval);
	if (rc < 0)
		return false;

	return !!pval.intval;
}
static bool oplus_chg_is_wls_present(struct battery_chg_dev *dev)
{
	union oplus_chg_mod_propval pval;
	int rc;

	if (!is_wls_ocm_available(dev)) {
		pr_debug("wls ocm not found\n");
		return false;
	}
	rc = oplus_chg_mod_get_property(dev->ocm_list[OCM_TYPE_WLS].ocm, OPLUS_CHG_PROP_PRESENT, &pval);
	if (rc < 0)
		return false;

	return !!pval.intval;
}

static int battery_chg_write(struct battery_chg_dev *bcdev, void *data,
				int len)
{
	int rc;

	/*
	 * When the subsystem goes down, it's better to return the last
	 * known values until it comes back up. Hence, return 0 so that
	 * pmic_glink_write() is not attempted until pmic glink is up.
	 */
	if (atomic_read(&bcdev->state) == PMIC_GLINK_STATE_DOWN) {
		pr_debug("glink state is down\n");
		return 0;
	}

	if (bcdev->debug_battery_detected && bcdev->block_tx)
		return 0;

	mutex_lock(&bcdev->rw_lock);
	reinit_completion(&bcdev->ack);
	rc = pmic_glink_write(bcdev->client, data, len);
	if (!rc) {
		rc = wait_for_completion_timeout(&bcdev->ack,
					msecs_to_jiffies(BC_WAIT_TIME_MS));
		if (!rc) {
			pr_err("Error, timed out sending message\n");
			mutex_unlock(&bcdev->rw_lock);
			return -ETIMEDOUT;
		}

		rc = 0;
	}
	mutex_unlock(&bcdev->rw_lock);
	return rc;
}

static int write_property_id(struct battery_chg_dev *bcdev,
			struct ocm_state *pst, u32 prop_id, u32 val)
{
	struct battery_charger_req_msg req_msg = { { 0 } };

	req_msg.property_id = prop_id;
	req_msg.battery_id = 0;
	req_msg.value = val;
	req_msg.hdr.owner = MSG_OWNER_BC;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = pst->opcode_set;

	pr_debug("psy: %s prop_id: %u val: %u\n", pst->name,
		req_msg.property_id, val);

	return battery_chg_write(bcdev, &req_msg, sizeof(req_msg));
}

static int read_property_id(struct battery_chg_dev *bcdev,
			struct ocm_state *pst, u32 prop_id)
{
	struct battery_charger_req_msg req_msg = { { 0 } };

	req_msg.property_id = prop_id;
	req_msg.battery_id = 0;
	req_msg.value = 0;
	req_msg.hdr.owner = MSG_OWNER_BC;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = pst->opcode_get;

	pr_debug("psy: %s prop_id: %u\n", pst->name,
		req_msg.property_id);

	return battery_chg_write(bcdev, &req_msg, sizeof(req_msg));
}

static void battery_chg_notify_enable(struct battery_chg_dev *bcdev)
{
	struct battery_charger_set_notify_msg req_msg = { { 0 } };
	int rc;

	/* Send request to enable notification */
	req_msg.hdr.owner = MSG_OWNER_BC;
	req_msg.hdr.type = MSG_TYPE_NOTIFY;
	req_msg.hdr.opcode = BC_SET_NOTIFY_REQ;

	rc = battery_chg_write(bcdev, &req_msg, sizeof(req_msg));
	if (rc < 0)
		pr_err("Failed to enable notification rc=%d\n", rc);
}

static int oplus_vbus_regulator_enable(struct battery_chg_dev *bcdev, bool enable)
{
	int rc = 0;
	struct ocm_state *pst = NULL;

	if(!probe_done)
		return 0;

	pst = &bcdev->ocm_list[OCM_TYPE_USB];

	rc = write_property_id(bcdev, pst, USB_OTG_DCDC_EN, enable);
	if (rc < 0) {
		pr_err("Set haptic boost %s error, rc=%d\n",
			enable ? "enable" : "disable", rc);
		return rc;
	}

	return rc;
}

bool oplus_get_pon_chg(void)
{
	return pon_by_chg;
}

void oplus_set_pon_chg(bool flag)
{
	pr_err("oplus_set_pon_chg set to[%d]!\n", flag);
	pon_by_chg = flag;
}

static bool oplus_set_otg_switch_status(bool enable)
{
	struct oplus_chg_chip *oplus_chip = g_oplus_chip;
	struct battery_chg_dev *bcdev = NULL;
	int rc = 0;
	struct ocm_state *pst = NULL;

	if(!probe_done)
		return false;

	if (!oplus_chip) {
		pr_err("oplus_chg_chip is NULL!\n");
		return false;
	}
	bcdev = oplus_chip->pmic_spmi.bcdev_chip;
	if (!bcdev) {
		pr_err("bcdev is NULL!\n");
		return false;
	}
	pst = &bcdev->ocm_list[OCM_TYPE_USB];

	rc = write_property_id(bcdev, pst, USB_OTG_SWITCH, enable);
	if (rc < 0) {
		pr_err(" Set otg status %s error rc=%d\n",
			enable ? "enable" : "disable", rc);
		return false;
	} else {
		pr_debug("Set otg status %s ok, rc=%d\n",
			enable ? "enable" : "disable", rc);
		return true;
	}
}

static int oplus_otg_ap_enable(bool enable)
{
	struct oplus_chg_chip *oplus_chip = g_oplus_chip;
	struct battery_chg_dev *bcdev = NULL;
	int rc = 0;
	struct ocm_state *pst = NULL;

	if(!probe_done)
		return 0;

	if (!oplus_chip) {
		pr_err("oplus_chg_chip is NULL!\n");
		return 0;
	}
	bcdev = oplus_chip->pmic_spmi.bcdev_chip;
	if (!bcdev) {
		pr_err("bcdev is NULL!\n");
		return 0;
	}

	pst = &bcdev->ocm_list[OCM_TYPE_USB];

	rc = write_property_id(bcdev, pst, USB_OTG_AP_ENABLE, enable);
	if (rc < 0) {
		pr_err("AP set otg status %s error, rc=%d\n",
			enable ? "enable" : "disable", rc);
		return rc;
	}

	return rc;
}

static void oplus_otg_enable_work(struct work_struct *work)
{
	struct battery_chg_dev *bcdev = container_of(work,
				struct battery_chg_dev, otg_enable_work);
	struct ocm_state *pst = NULL;

	if (bcdev == NULL) {
		pr_err("battery_chg_dev is NULL\n");
		return;
	}

	if (bcdev->otg_type == OTG_EXTERNAL_BOOST) {
		pr_err("%s, Use external boost!!", __func__);
		oplus_enable_haptic_boost(bcdev, true);
		pst = &bcdev->ocm_list[OCM_TYPE_USB];
		if (is_usb_ocm_available(bcdev))
			oplus_chg_mod_event(pst->ocm, pst->ocm, OPLUS_CHG_EVENT_OTG_ENABLE);
	} else if (bcdev->otg_type == OTG_DEFAULT_DCDC) {
		pr_err("%s, Use default DCDC!!", __func__);
		oplus_vbus_regulator_enable(bcdev, true);
	} else {
		pr_err("OTG typec invalid!!");
	}
}

static void oplus_otg_disable_work(struct work_struct *work)
{
	struct battery_chg_dev *bcdev = container_of(work,
				struct battery_chg_dev, otg_disable_work);
	struct ocm_state *pst = NULL;

	if (bcdev == NULL) {
		pr_err("battery_chg_dev is NULL\n");
		return;
	}

	if (bcdev->otg_type == OTG_EXTERNAL_BOOST) {
		pr_err("%s, Use external boost!!", __func__);
		pst = &bcdev->ocm_list[OCM_TYPE_USB];
		if (is_usb_ocm_available(bcdev))
			oplus_chg_mod_event(pst->ocm, pst->ocm, OPLUS_CHG_EVENT_OTG_DISABLE);
		oplus_enable_haptic_boost(bcdev, false);
	} else if (bcdev->otg_type == OTG_DEFAULT_DCDC) {
		pr_err("%s, Use default DCDC!!", __func__);
		oplus_vbus_regulator_enable(bcdev, false);
	} else {
		pr_err("OTG typec invalid!!");
	}
}

#define OTG_SKIN_TEMP_HIGH 450
#define OTG_SKIN_TEMP_MAX 540
static void oplus_otg_status_check_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct battery_chg_dev *bcdev = container_of(dwork,
				struct battery_chg_dev, otg_status_check_work);
	struct oplus_chg_chip *chip = g_oplus_chip;
	int rc;
	int skin_temp = 0, batt_current = 0, real_soc = 0;

	if (bcdev == NULL) {
		pr_err("battery_chg_dev is NULL\n");
		return;
	}

	rc = oplus_chg_get_skin_temp(bcdev, &skin_temp);
	if (rc < 0) {
		pr_err("Error get skin temp!, rc = %d\n", rc);
		return;
	}

	rc = oplus_chg_get_batt_current(bcdev, &batt_current);
	if (rc < 0) {
		pr_err("Error get batt current!, rc = %d\n", rc);
		return;
	}

	rc = oplus_chg_get_real_soc(bcdev, &real_soc);
	if (rc < 0) {
		pr_err("Error get real soc!, rc = %d\n", rc);
		return;
	}

	if (((batt_current > 1750) && (skin_temp > OTG_SKIN_TEMP_HIGH)) ||
		(batt_current > 2500) || (skin_temp > OTG_SKIN_TEMP_MAX) ||
		((real_soc < 10) && (batt_current > 1750))) {
		if (!bcdev->otg_prohibited) {
			oplus_vbus_regulator_enable(bcdev, false);
			bcdev->otg_prohibited = true;
			pr_err("OTG prohibited, batt_current = %d, skin_temp = %d\n", batt_current, skin_temp);
		}
	}

	if (!chip->otg_online) {
		if (bcdev->otg_prohibited) {
			bcdev->otg_prohibited = false;
		}
		pr_err("otg_online is false, exit\n");
		return;
	}

	schedule_delayed_work(&bcdev->otg_status_check_work, msecs_to_jiffies(1000));
}

static int oplus_chg_get_real_soc(struct battery_chg_dev *bcdev, int *soc)
{
	int rc;
	union oplus_chg_mod_propval pval;

	if (!is_batt_ocm_available(bcdev)) {
		pr_err("batt ocm not found\n");
		return -ENODEV;
	}
	rc = oplus_chg_mod_get_property(bcdev->ocm_list[OCM_TYPE_BATTERY].ocm, OPLUS_CHG_PROP_REAL_CAPACITY, &pval);
	if (rc < 0)
		return rc;
	*soc = pval.intval;

	return 0;
}

static int oplus_chg_get_skin_temp(struct battery_chg_dev *bcdev, int *skin_temp)
{
	int rc;
	union oplus_chg_mod_propval pval;

	if (!is_comm_ocm_available(bcdev)) {
		pr_err("comm ocm not found\n");
		return -ENODEV;
	}
	rc = oplus_chg_mod_get_property(bcdev->ocm_list[OCM_TYPE_COMM].ocm, OPLUS_CHG_PROP_SKIN_TEMP, &pval);
	if (rc < 0)
		return rc;
	*skin_temp = pval.intval;

	return 0;
}

static int oplus_chg_get_batt_current(struct battery_chg_dev *bcdev, int *batt_current)
{
	int rc;
	union oplus_chg_mod_propval pval;

	if (!is_batt_ocm_available(bcdev)) {
		pr_err("batt ocm not found\n");
		return -ENODEV;
	}
	rc = oplus_chg_mod_get_property(bcdev->ocm_list[OCM_TYPE_BATTERY].ocm, OPLUS_CHG_PROP_CURRENT_NOW, &pval);
	if (rc < 0)
		return rc;
#ifdef CONFIG_OPLUS_CHG_OOS
	*batt_current = pval.intval / 1000;
#else
	*batt_current = pval.intval;
#endif

	return 0;
}

static int oplus_chg_get_fastchg_status(struct battery_chg_dev *bcdev, int *fastchg_on)
{
	int rc;
	union oplus_chg_mod_propval pval;

	if (!is_usb_ocm_available(bcdev)) {
		pr_err("usb ocm not found\n");
		return -ENODEV;
	}
	rc = oplus_chg_mod_get_property(bcdev->ocm_list[OCM_TYPE_USB].ocm, OPLUS_CHG_PROP_FASTCHG_STATUS, &pval);
	if (rc < 0)
		return rc;
	*fastchg_on = pval.intval;

	return 0;
}

static int oplus_chg_get_batt_temp(struct battery_chg_dev *bcdev, int *temp)
{
	int rc;
	union oplus_chg_mod_propval pval;

	if (!is_batt_ocm_available(bcdev)) {
		pr_err("batt ocm not found\n");
		return -ENODEV;
	}
	rc = oplus_chg_mod_get_property(bcdev->ocm_list[OCM_TYPE_BATTERY].ocm, OPLUS_CHG_PROP_TEMP, &pval);
	if (rc < 0)
		return rc;
	*temp = pval.intval;

	return 0;
}
#ifdef OPLUS_CHG_OP_DEF
static void msm_vreg_ldo_enable(struct battery_chg_dev *bcdev)
{
	const char *supply_name;
	int ret;

	supply_name = "vreg_s1c";
	bcdev->current_uA = 50000;
	bcdev->vreg_default_vol = 1920000;
	if (!bcdev) {
		pr_err("bcdev %s\n",__func__);
		return;
	}

	bcdev->vreg_ldo = regulator_get(bcdev->dev, supply_name);
	if (IS_ERR_OR_NULL(bcdev->vreg_ldo)) {
		ret = PTR_ERR(bcdev->vreg_ldo);
		pr_err("regulator_get(%s) failed for %s, ret=%d\n", supply_name,
				supply_name, ret);
		return;
	}

	ret = regulator_set_voltage(bcdev->vreg_ldo, bcdev->vreg_default_vol,
				bcdev->vreg_default_vol);
	if (ret) {
			pr_err("regulator_set_voltage %s failed, ret=%d\n",
				supply_name, ret);
	}

	ret = regulator_set_load(bcdev->vreg_ldo, bcdev->current_uA);
	if (ret < 0) {
		pr_err("regulator_set_load %s failed, ret=%d\n",
			supply_name, ret);
	}

	ret = regulator_enable(bcdev->vreg_ldo);
	if (ret) {
		pr_err("regulator_enable %s failed, ret=%d\n", supply_name,
			ret);
	}
}
#endif

static int oplus_chg_hw_detect(struct battery_chg_dev *bcdev, int *hw_detect)
{
	struct oplus_chg_ic_dev *typec_ic;
	struct oplus_chg_ic_typec_ops *typec_ic_ops;
	int rc;

	if (bcdev == NULL) {
		pr_err("battery_chg_dev is NULL\n");
		return -ENODEV;
	}

	typec_ic = of_get_oplus_chg_ic(bcdev->dev->of_node, "oplus,typec_ic");
	if (typec_ic == NULL) {
		pr_err("typec_ic not found!\n");
		*hw_detect = 0;
		return -ENODEV;
	}

	typec_ic_ops = typec_ic->dev_ops;
	rc = typec_ic_ops->typec_get_hw_detect(typec_ic, hw_detect);

	return rc;
}

static int oplus_chg_vbus_status(struct battery_chg_dev *bcdev, int *vbus_rising)
{
	struct oplus_chg_ic_dev *typec_ic;
	struct oplus_chg_ic_typec_ops *typec_ic_ops;
	int rc;

	if (bcdev == NULL) {
		pr_err("battery_chg_dev is NULL\n");
		return -ENODEV;
	}

	typec_ic = of_get_oplus_chg_ic(bcdev->dev->of_node, "oplus,typec_ic");
	if (typec_ic == NULL) {
		pr_err("typec_ic not found!\n");
		*vbus_rising = 0;
		return -ENODEV;
	}

	typec_ic_ops = typec_ic->dev_ops;
	rc = typec_ic_ops->typec_get_vbus_status(typec_ic, vbus_rising);

	return rc;
}

static void oplus_vbus_ctrl_gpio_request(struct battery_chg_dev *bcdev)
{
	int rc;

	if (!gpio_is_valid(bcdev->vbus_ctrl)) {
		pr_err("Vbus ctrl gpio invalid!!");
		return;
	}

	rc = gpio_request(bcdev->vbus_ctrl, "VbusCtrl");
	if (rc) {
		pr_err("gpio_request failed for %d rc=%d\n",
				bcdev->vbus_ctrl, rc);
	}
}

static int oplus_usbtemp_iio_init(struct battery_chg_dev *bcdev)
{
	int rc;
	struct device_node *node = bcdev->dev->of_node;

	rc = of_property_match_string(node, "io-channel-names",
			"gpio1_voltage");
	if (rc >= 0) {
		bcdev->iio.op_connector_temp_chan = iio_channel_get(bcdev->dev,
				"gpio1_voltage");
		if (IS_ERR(bcdev->iio.op_connector_temp_chan)) {
			rc = PTR_ERR(bcdev->iio.op_connector_temp_chan);
			if (rc != -EPROBE_DEFER)
				dev_err(bcdev->dev,
				"op_connector_temp_chan channel unavailable,%ld\n",
				rc);
			bcdev->iio.op_connector_temp_chan = NULL;
			return rc;
		}
	}

	rc = of_property_match_string(node, "io-channel-names",
			"gpio3_voltage");
	if (rc >= 0) {
		bcdev->iio.op_connector_temp_chan_sec = iio_channel_get(bcdev->dev,
				"gpio3_voltage");
		if (IS_ERR(bcdev->iio.op_connector_temp_chan_sec)) {
			rc = PTR_ERR(bcdev->iio.op_connector_temp_chan_sec);
			if (rc != -EPROBE_DEFER)
				dev_err(bcdev->dev,
				"op_connector_temp_chan_sec channel unavailable,%ld\n",
				rc);
			bcdev->iio.op_connector_temp_chan_sec = NULL;
			return rc;
		}
	}

	return 0;
}

#ifndef CONFIG_OPLUS_CHG_OOS
static void oplus_set_usb_status(int status)
{
	if (g_oplus_chip)
		g_oplus_chip->usb_status = g_oplus_chip->usb_status | status;
}

static void oplus_clear_usb_status(int status)
{
	if (g_oplus_chip)
		g_oplus_chip->usb_status = g_oplus_chip->usb_status & (~status);
}

int oplus_get_usb_status(void)
{
	if (g_oplus_chip)
		return g_oplus_chip->usb_status;
	return 0;
}
#endif

#ifdef OPLUS_CHG_OP_DEF
static int oplus_vph_iio_init(struct battery_chg_dev *bcdev)
{
	int rc;
	struct device_node *node = bcdev->dev->of_node;

	rc = of_property_match_string(node, "io-channel-names",
			"vph_pwr_vol");
	bcdev->op_vph_vol_chan = NULL;
	if (rc >= 0) {
		bcdev->op_vph_vol_chan = iio_channel_get(bcdev->dev,
				"vph_pwr_vol");
		if (IS_ERR(bcdev->op_vph_vol_chan)) {
			rc = PTR_ERR(bcdev->op_vph_vol_chan);
			if (rc != -EPROBE_DEFER)
				dev_err(bcdev->dev,
				"vph_pwr_vol channel unavailable,%ld\n",
				rc);
			bcdev->op_vph_vol_chan = NULL;
			return rc;
		}
	}
	return 0;
}
#endif

#define USB_CONNECTOR_DEFAULT_TEMP 25
static int get_usb_temp(struct battery_chg_dev *bcdev)
{
	int ret, i, result, temp = 0, step_value = 0, temp_1 = 0, temp_2 = 0;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		pr_err("oplus_chg_chip is NULL!\n");
		return 0;
	}

	if (bcdev->iio.op_connector_temp_chan) {
		ret = iio_read_channel_processed(
				bcdev->iio.op_connector_temp_chan,
				&result);
		if (ret < 0) {
			pr_err("Error in reading IIO channel data, rc=%d\n",
					ret);
			return USB_CONNECTOR_DEFAULT_TEMP;
		}
		bcdev->connector_voltage = 18*result/10000;
	} else {
		pr_err("op_connector_temp_chan no found!\n");
		return USB_CONNECTOR_DEFAULT_TEMP;
	}
	for (i = ARRAY_SIZE(con_volt_30k) - 1; i >= 0; i--) {
		if (con_volt_30k[i] >= bcdev->connector_voltage)
			break;
		else if (i == 0)
			break;
	}

	pr_debug("connector_voltage_01:%04dmv,connector_temp_02:%03d\n",
				bcdev->connector_voltage,
				con_temp_30k[i]);

	temp_1 = con_temp_30k[i];
	/*remove usb temp 125 degree begin*/
	if (temp_1 == 125)
            temp_1 = 25;
	/*remove usb temp 125 degree end*/
	chip->usb_temp_l = temp_1;

	if (bcdev->iio.op_connector_temp_chan_sec) {
		ret = iio_read_channel_processed(
				bcdev->iio.op_connector_temp_chan_sec,
				&result);
		if (ret < 0) {
			pr_err("Error in reading sec IIO channel data, rc=%d\n",
					ret);
			return USB_CONNECTOR_DEFAULT_TEMP;
		}
		bcdev->connector_voltage = 18*result/10000;
	} else {
		pr_err("op_connector_temp_chan no found!\n");
		return -ENODATA;
	}
	for (i = ARRAY_SIZE(con_volt_30k) - 1; i >= 0; i--) {
		if (con_volt_30k[i] >= bcdev->connector_voltage)
			break;
		else if (i == 0)
			break;
	}

	pr_debug("connector_voltage_02:%04dmv,connector_temp_02:%03d\n",
				bcdev->connector_voltage,
				con_temp_30k[i]);
	temp_2 = con_temp_30k[i];
	/*remove usb temp 125 degree begin*/
	if (temp_2 == 125)
            temp_2 = 25;
        /*remove usb temp 125 degree end*/
	chip->usb_temp_r = temp_2;

	if (temp_1 >= temp_2)
		temp = temp_1;
	else
		temp = temp_2;

	step_value = temp - bcdev->connector_temp;

	/*WR for temperature value(70~85) and steep filter, use last value*/
	if (((temp >= 70) && (temp <= 85)) || (step_value >= 10)) {
		bcdev->filter_count++;
		if (bcdev->filter_count <= 3) {
			pr_info("con_temp=(%d) pre_temp=(%d) filter_count(%d),filter not report!\n",
				temp,
				bcdev->connector_temp,
				bcdev->filter_count);
			return bcdev->connector_temp;
		}
		bcdev->filter_count = 0;
	} else
		bcdev->filter_count = 0;

	return temp;
}

#define THIRD_LOOP_ENTER_MINI_THRESHOLD 35
#define THIRD_LOOP_ENTER_MAX_THRESHOLD 60
#define THIRD_INTERVAL_MINI_THRESHOLD 8
#define THIRD_INTERVAL_MAX_THRESHOLD 20
static void oplus_connector_temp_check_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct battery_chg_dev *bcdev = container_of(dwork,
				struct battery_chg_dev, connector_check_work);
	int batt_temp = 0, interval_temp = 0;
	int i = 0, rc;
	static int count = 5;
	static int count_temp125 = 3;
	static bool check_pass;

	oplus_chg_vbus_status(bcdev, &bcdev->vbus_present);
	if (!bcdev->vbus_present) {
		if (!check_pass && count --) {
			schedule_delayed_work(&bcdev->connector_check_work,
				msecs_to_jiffies(1500));
			pr_err("Vbus not ready when protect algorithm runs, retry!! count: %d", count);
		} else
			pr_err("Vbus not present, return!!");
		check_pass = false;
		return;
	}

	bcdev->connector_temp = get_usb_temp(bcdev);
	rc = oplus_chg_get_batt_temp(bcdev, &batt_temp);
	if (rc < 0) {
		pr_err("batt temp read error, return!!");
		return;
	}
	interval_temp = bcdev->connector_temp - batt_temp/10;
	count = 5;
	check_pass = true;

	if (!bcdev->count_run) {/*count run state keep count_total not change*/
		if (bcdev->connector_temp >= 33) {// 33
			bcdev->count_total = 1500 / 50;
		} else {
			bcdev->count_total = 1500 / 300;
		}
	}

	pr_debug("connector_temp:%d,batt_temp:%d,interval_temp:%d,count_total:%d,count_run:%d,connector_short:%d\n",
			bcdev->connector_temp,
			batt_temp,
			interval_temp,
			bcdev->count_total,
			bcdev->count_run,
			bcdev->connector_short);
	/*error:EOC bit not set! cause connector_temp=125*/
	if (bcdev->connector_temp == 125) {
		for (i = 0; i <= 9; i++) {
			msleep(100);
			pr_debug("EOC error!temp abormal delay count:%d\n", i);
		}
		if (count_temp125 --) {
			pr_err("connector_temp 125, retry!! count: %d", count_temp125);
			schedule_delayed_work(&bcdev->connector_check_work,
				msecs_to_jiffies(100));
			return; /*rerun check again*/
		}
	}
	count_temp125 = 3;

	if (bcdev->connector_temp >= 60) { /* >=60 */
		pr_info("connector_temp=%d,connector_short=%d\n",
				bcdev->connector_temp,
				bcdev->connector_short);
		oplus_disconnect_vbus(true);
	} else {/*20<= ? < 60*/
		if ((interval_temp >= 14) && (bcdev->connector_temp >= 45)) {
			/*interval > 14 && connector > 45*/
			pr_info("interval_temp=%d,connector_temp=%d\n",
					interval_temp,
					bcdev->connector_temp);
			oplus_disconnect_vbus(true);
			return;
		} else if (((interval_temp >= 8) &&
			(bcdev->connector_temp >= 20)) ||
			(bcdev->connector_temp >= 40)) {
		/*interval >=8 && connector >=20  or connector >= 40 enter*/
			if (bcdev->count_run <= bcdev->count_total) {
			/*time out count*/
				if (bcdev->count_run == 0)
					bcdev->pre_temp = bcdev->connector_temp;

				/* time out check MAX=count_total*/
				if (bcdev->count_run > 0) {
					bcdev->current_temp = bcdev->connector_temp;
					if ((bcdev->current_temp -
						bcdev->pre_temp) >= 3) { /* 3 degree per 1.5 senconds*/
						bcdev->connector_short = true;
						pr_info("cout_run=%d,short=%d\n",
							bcdev->count_run,
							bcdev->connector_short);
						oplus_disconnect_vbus(true);
						return;
					}
				}

				bcdev->count_run++;/*count ++*/

				if (bcdev->count_run > bcdev->count_total) {
					bcdev->count_run = 0;
					pr_debug("count reset!\n");
				}
			}
		} else {/*connector <20 or connector < 40 && interval < 8*/
			if (bcdev->count_run)/* high temp cold down count reset.*/
				bcdev->count_run = 0;
			pr_debug("connector_temp:%d\n", bcdev->connector_temp);
		}
	}

	if (bcdev->connector_temp < 33)
		schedule_delayed_work(&bcdev->connector_check_work, msecs_to_jiffies(300));
	else if ((bcdev->connector_temp >= 33) && (bcdev->connector_temp < 60))
		/*time need optimize depend on test*/
		schedule_delayed_work(&bcdev->connector_check_work, msecs_to_jiffies(50));
	else
		pr_debug("connector_temp:%d\n", bcdev->connector_temp);
}

#define connector_RECOVERY_CHECK_INTERVAL 3000 //ms
static void oplus_connector_recovery_charge_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct battery_chg_dev *bcdev = container_of(dwork,
				struct battery_chg_dev, connector_recovery_work);
	struct oplus_chg_chip *oplus_chip = g_oplus_chip;
	int batt_temp = 0, interval_temp = 0;
	int rc;

	if (!oplus_chip->disconnect_vbus)
		return;

	bcdev->connector_temp = get_usb_temp(bcdev);
	rc = oplus_chg_get_batt_temp(bcdev, &batt_temp);
	if (rc < 0) {
		pr_err("batt temp read error, return!!");
		return;
	}
	interval_temp = bcdev->connector_temp - batt_temp/10;

	pr_debug("disconnect_vbus=%d, hw_detect=%d, connector_temp=%d, interval_temp=%d",
			oplus_chip->disconnect_vbus, bcdev->hw_detect, bcdev->connector_temp, interval_temp);

	oplus_chg_hw_detect(bcdev, &bcdev->hw_detect);
	if ((bcdev->hw_detect == 0) && (bcdev->connector_temp <= 50) && (interval_temp <= 10)) {
		pr_info("Recovery Charge!\n");
		oplus_disconnect_vbus(false);
		bcdev->connector_short = false;
		return;
	}

	schedule_delayed_work(&bcdev->connector_recovery_work,
		msecs_to_jiffies(connector_RECOVERY_CHECK_INTERVAL));
}

#define VBUS_FORCE_RETURN 0
static void oplus_disconnect_vbus(bool enable)
{
	struct oplus_warp_chip *chip = g_warp_chip;
	struct oplus_chg_chip *oplus_chip = g_oplus_chip;
	struct battery_chg_dev *bcdev = NULL;
	int rc;
	char *recovered[2] = { "USB_CONTAMINANT=RECOVERED", NULL };
	char *detected[2] = { "USB_CONTAMINANT=DETECTED", NULL };

	if (VBUS_FORCE_RETURN) {
		pr_err("oplus_disconnect_vbus force return!\n");
		return;
	}
#ifdef OPLUS_CHG_OP_DEF
	if (get_eng_version() == HIGH_TEMP_AGING) {
		pr_err("Aging Test,disconnect_vbus force return!\n");
		return;
	}
#endif
	if(!probe_done)
		return;

	if (!oplus_chip) {
		pr_err("oplus_chg_chip is NULL!\n");
		return;
	}
	bcdev = oplus_chip->pmic_spmi.bcdev_chip;
	if (!bcdev) {
		pr_err("bcdev is NULL!\n");
		return;
	}

	if (!gpio_is_valid(bcdev->vbus_ctrl))
		return;
	if (!enable) {
		gpio_set_value(bcdev->vbus_ctrl, 0);
		smbchg_usb_suspend_disable();
		oplus_chip->disconnect_vbus = false;
		kobject_uevent_env(&bcdev->dev->kobj, KOBJ_CHANGE, recovered);
#ifndef CONFIG_OPLUS_CHG_OOS
		oplus_clear_usb_status(USB_TEMP_HIGH);
#endif
		pr_info("usb connector cool(%d), Vbus connected! Sent uevent %s\n",
				bcdev->connector_temp, recovered[0]);
		return;
	}

	kobject_uevent_env(&bcdev->dev->kobj, KOBJ_CHANGE, detected);
#ifndef CONFIG_OPLUS_CHG_OOS
	oplus_set_usb_status(USB_TEMP_HIGH);
#endif
	pr_info("usb connector hot(%d),Vbus disconnected! Sent uevent %s\n",
			bcdev->connector_temp, detected[0]);
	rc = oplus_chg_get_fastchg_status(bcdev, &bcdev->fastchg_on);
	if (rc < 0) {
		pr_err("Error get fastchg status!, rc = %d\n", rc);
		return;
	}

	if (!chip || !chip->vops) {
		pr_err("chip or vops is null\n");
	}

	if (bcdev->fastchg_on && chip && chip->vops) {
		chip->vops->reset_mcu(chip);
		usleep_range(2000, 2001);
		chip->vops->set_switch_mode(chip, NORMAL_CHARGER_MODE);
		oplus_warp_set_fastchg_allow(false);
	}
	smbchg_usb_suspend_enable();
	msleep(20);

	gpio_set_value(bcdev->vbus_ctrl, 1);
	oplus_chip->disconnect_vbus = true;

	/* CC set sink only mode */
	//vote(bcdev->otg_toggle_votable, HOT_PROTECT_VOTER, 0, 0);

	/* Charge recovery monitor */

	schedule_delayed_work(&bcdev->connector_recovery_work,
		msecs_to_jiffies(1000));
}
static int opchg_get_charger_type(void);

static void oplus_update_usb_type(void)
{
	struct oplus_chg_chip *oplus_chip = g_oplus_chip;
	struct battery_chg_dev *bcdev = NULL;

	if (!oplus_chip) {
		pr_err("oplus_chg_chip is NULL!\n");
		return;
	}
	bcdev = oplus_chip->pmic_spmi.bcdev_chip;
	if (!bcdev) {
		pr_err("bcdev is NULL!\n");
		return;
	}

	schedule_work(&bcdev->usb_type_work);
}

static void oplus_charger_type_check_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct battery_chg_dev *bcdev = container_of(dwork,
				struct battery_chg_dev, check_charger_type_work);
	static int count = 0;
	int charger_type = POWER_SUPPLY_TYPE_UNKNOWN;

	charger_type = opchg_get_charger_type();
	pr_info("Recovery Charge!\n");

	chg_err("chip->charger_type[%d],count:%d\n", charger_type, count);
	if (charger_type == POWER_SUPPLY_TYPE_UNKNOWN && (count < 6)) {
		count++;
		schedule_delayed_work(&bcdev->check_charger_type_work,
				msecs_to_jiffies(500));
	} else {
		oplus_chg_wake_update_work();
		count = 0;
	}

}

static void oplus_charge_status_check_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct battery_chg_dev *bcdev = container_of(dwork,
				struct battery_chg_dev, charge_status_check_work);
	union oplus_chg_mod_propval pval;
	struct oplus_chg_chip *chip = g_oplus_chip;
	bool usb_present = false;
	int rc;

	if(!probe_done)
		return;
	
	if (!chip) {
		pr_err("chip is NULL!\n");
		return;
	}

	if (is_usb_ocm_available(bcdev)) {
		rc = oplus_chg_mod_get_property(bcdev->ocm_list[OCM_TYPE_USB].ocm,
			OPLUS_CHG_PROP_PRESENT, &pval);
		if (rc == 0)
			usb_present = !!pval.intval;
	}

	if (usb_present && !chip->mmi_chg) {
		pr_err("mmi_chg disable charge\n");
		smbchg_charging_disable();
	}
}

static void battery_chg_subsys_up_work(struct work_struct *work)
{
	struct battery_chg_dev *bcdev = container_of(work,
					struct battery_chg_dev, subsys_up_work);

	battery_chg_notify_enable(bcdev);
}

static void battery_chg_state_cb(void *priv, enum pmic_glink_state state)
{
	struct battery_chg_dev *bcdev = priv;

	pr_debug("state: %d\n", state);

	atomic_set(&bcdev->state, state);
	if (state == PMIC_GLINK_STATE_UP)
		schedule_work(&bcdev->subsys_up_work);
}

static bool validate_message(struct battery_charger_resp_msg *resp_msg,
				size_t len)
{
	if (len != sizeof(*resp_msg)) {
		pr_err("Incorrect response length %zu for opcode %#x\n", len,
			resp_msg->hdr.opcode);
		return false;
	}

	if (resp_msg->ret_code) {
		pr_err("Error in response for opcode %#x prop_id %u, rc=%d\n",
			resp_msg->hdr.opcode, resp_msg->property_id,
			(int)resp_msg->ret_code);
		return false;
	}

	return true;
}

#define MODEL_DEBUG_BOARD	"Debug_Board"
static void handle_message(struct battery_chg_dev *bcdev, void *data,
				size_t len)
{
	struct battery_charger_resp_msg *resp_msg = data;
	struct battery_model_resp_msg *model_resp_msg = data;
	struct wireless_fw_check_resp *fw_check_msg;
	struct wireless_fw_push_buf_resp *fw_resp_msg;
	struct wireless_fw_update_status *fw_update_msg;
	struct wireless_fw_get_version_resp *fw_ver_msg;
	struct ocm_state *pst;
	bool ack_set = false;

	switch (resp_msg->hdr.opcode) {
	case BC_BATTERY_STATUS_GET:
		pst = &bcdev->ocm_list[OCM_TYPE_BATTERY];

		/* Handle model response uniquely as it's a string */
		if (pst->model && len == sizeof(*model_resp_msg)) {
			memcpy(pst->model, model_resp_msg->model, MAX_STR_LEN);
			ack_set = true;
			bcdev->debug_battery_detected = !strcmp(pst->model,
					MODEL_DEBUG_BOARD);
			break;
		}

		/* Other response should be of same type as they've u32 value */
		if (validate_message(resp_msg, len) &&
		    resp_msg->property_id < pst->prop_count) {
			pst->prop[resp_msg->property_id] = resp_msg->value;
			ack_set = true;
		}

		break;
	case BC_USB_STATUS_GET:
		pst = &bcdev->ocm_list[OCM_TYPE_USB];
		if (validate_message(resp_msg, len) &&
		    resp_msg->property_id < pst->prop_count) {
			pst->prop[resp_msg->property_id] = resp_msg->value;
			ack_set = true;
		}

		break;
	case BC_WLS_STATUS_GET:
		pst = &bcdev->ocm_list[OCM_TYPE_WLS];
		if (validate_message(resp_msg, len) &&
		    resp_msg->property_id < pst->prop_count) {
			pst->prop[resp_msg->property_id] = resp_msg->value;
			ack_set = true;
		}

		break;
	case BC_BATTERY_STATUS_SET:
	case BC_USB_STATUS_SET:
	case BC_WLS_STATUS_SET:
		if (validate_message(data, len))
			ack_set = true;

		break;
	case BC_SET_NOTIFY_REQ:
		/* Always ACK response for notify request */
		ack_set = true;
		break;
	case BC_WLS_FW_CHECK_UPDATE:
		if (len == sizeof(*fw_check_msg)) {
			fw_check_msg = data;
			if (fw_check_msg->ret_code == 1)
				bcdev->wls_fw_update_reqd = true;
			ack_set = true;
		} else {
			pr_err("Incorrect response length %zu for wls_fw_check_update\n",
				len);
		}
		break;
	case BC_WLS_FW_PUSH_BUF_RESP:
		if (len == sizeof(*fw_resp_msg)) {
			fw_resp_msg = data;
			if (fw_resp_msg->fw_update_status == 1)
				complete(&bcdev->fw_buf_ack);
		} else {
			pr_err("Incorrect response length %zu for wls_fw_push_buf_resp\n",
				len);
		}
		break;
	case BC_WLS_FW_UPDATE_STATUS_RESP:
		if (len == sizeof(*fw_update_msg)) {
			fw_update_msg = data;
			if (fw_update_msg->fw_update_done == 1)
				complete(&bcdev->fw_update_ack);
		} else {
			pr_err("Incorrect response length %zu for wls_fw_update_status_resp\n",
				len);
		}
		break;
	case BC_WLS_FW_GET_VERSION:
		if (len == sizeof(*fw_ver_msg)) {
			fw_ver_msg = data;
			bcdev->wls_fw_version = fw_ver_msg->fw_version;
			ack_set = true;
		} else {
			pr_err("Incorrect response length %zu for wls_fw_get_version\n",
				len);
		}
		break;
	default:
		pr_err("Unknown opcode: %u\n", resp_msg->hdr.opcode);
		break;
	}

	if (ack_set)
		complete(&bcdev->ack);
}

extern void oplus_chg_gki_set_usb_type(enum power_supply_type type);

static void battery_chg_update_usb_type_work(struct work_struct *work)
{
	struct battery_chg_dev *bcdev = container_of(work,
					struct battery_chg_dev, usb_type_work);
	struct oplus_chg_chip *chip = g_oplus_chip;

	struct ocm_state *pst = &bcdev->ocm_list[OCM_TYPE_USB];
	int rc;

	rc = read_property_id(bcdev, pst, USB_ADAP_TYPE);
	if (rc < 0) {
		pr_err("Failed to read USB_ADAP_TYPE rc=%d\n", rc);
		return;
	}

	pr_debug("usb_adap_type: %u\n", pst->prop[USB_ADAP_TYPE]);

	switch (pst->prop[USB_ADAP_TYPE]) {
	case POWER_SUPPLY_USB_TYPE_SDP:
		oplus_chg_gki_set_usb_type(POWER_SUPPLY_TYPE_USB);
		break;
	case POWER_SUPPLY_USB_TYPE_DCP:
	case POWER_SUPPLY_USB_TYPE_APPLE_BRICK_ID:
		oplus_chg_gki_set_usb_type(POWER_SUPPLY_TYPE_USB_DCP);
		break;
	case POWER_SUPPLY_USB_TYPE_CDP:
		oplus_chg_gki_set_usb_type(POWER_SUPPLY_TYPE_USB_CDP);
		break;
	case POWER_SUPPLY_USB_TYPE_ACA:
		oplus_chg_gki_set_usb_type(POWER_SUPPLY_TYPE_USB_ACA);
		break;
	case POWER_SUPPLY_USB_TYPE_C:
		oplus_chg_gki_set_usb_type(POWER_SUPPLY_TYPE_USB_TYPE_C);
		break;
	case POWER_SUPPLY_USB_TYPE_PD:
	case POWER_SUPPLY_USB_TYPE_PD_DRP:
	case POWER_SUPPLY_USB_TYPE_PD_PPS:
		if (chip && chip->usb_enum_status)
			oplus_chg_gki_set_usb_type(POWER_SUPPLY_TYPE_USB);
		else
			oplus_chg_gki_set_usb_type(POWER_SUPPLY_TYPE_USB_PD);
		break;
	default:
		if (!oplus_chg_is_usb_online(bcdev))
			oplus_chg_gki_set_usb_type(POWER_SUPPLY_TYPE_UNKNOWN);
		break;
	}
}

static void handle_notification(struct battery_chg_dev *bcdev, void *data,
				size_t len)
{
	struct battery_charger_notify_msg *notify_msg = data;
	struct oplus_chg_chip *chip = g_oplus_chip;
	struct ocm_state *pst = NULL;

	if (len != sizeof(*notify_msg)) {
		pr_err("Incorrect response length %zu\n", len);
		return;
	}

	if (!chip) {
		pr_err("oplus_chg_chip is NULL!\n");
		return;
	}
	pr_debug("notification: %#x\n", notify_msg->notification);

	switch (notify_msg->notification) {
	case BC_BATTERY_STATUS_GET:
	case BC_GENERIC_NOTIFY:
		if (is_batt_ocm_available(bcdev))
			pst = &bcdev->ocm_list[OCM_TYPE_BATTERY];
		break;
	case BC_USB_STATUS_GET:
		if (is_usb_ocm_available(bcdev))
			pst = &bcdev->ocm_list[OCM_TYPE_USB];
		schedule_work(&bcdev->usb_type_work);
		break;
	case BC_WLS_STATUS_GET:
		if (is_wls_ocm_available(bcdev))
			pst = &bcdev->ocm_list[OCM_TYPE_WLS];
		break;
	case BC_USB_STATUS_ONLINE:
		bcdev->count_run = 0;
		if (gpio_is_valid(bcdev->vbus_ctrl))
			schedule_delayed_work(&bcdev->connector_check_work,
				msecs_to_jiffies(1000));
#ifdef OPLUS_CHG_OP_DEF
		if (oplus_warp_get_ffc_chg_start())
			smbchg_charging_disable();
#endif
		usb_online = true;
		oplus_chg_wake_update_work();
		break;
	case BC_USB_STATUS_OFFLINE:
		bcdev->count_run = 0;
		usb_online = false;
		chip->svid_verified = false;
		chip->usb_enum_status = false;
		if (is_usb_ocm_available(bcdev))
			pst = &bcdev->ocm_list[OCM_TYPE_USB];
		if (probe_done)
			schedule_delayed_work(&bcdev->usb_disconnect_work, msecs_to_jiffies(500));
		oplus_chg_wake_update_work();
		break;
	case BC_WLS_STATUS_ONLINE:
		wls_online = true;
		/*pst = &bcdev->ocm_list[OCM_TYPE_WLS];
		if (is_wls_ocm_available(bcdev))
			oplus_chg_global_event(pst->ocm, OPLUS_CHG_EVENT_ONLINE); */
		break;
	case BC_WLS_STATUS_OFFLINE:
		wls_online = false;
		chip->svid_verified = false;
		/*pst = &bcdev->ocm_list[OCM_TYPE_WLS];
		if (is_wls_ocm_available(bcdev))
			oplus_chg_global_event(pst->ocm, OPLUS_CHG_EVENT_OFFLINE);*/
		break;
	case BC_USB_STATUS_PRESENT:
		usb_present = true;
		chip->chg_config_init = true;
		pst = &bcdev->ocm_list[OCM_TYPE_USB];
		if (is_usb_ocm_available(bcdev))
			oplus_chg_mod_event(pst->ocm, pst->ocm, OPLUS_CHG_EVENT_PRESENT);
		schedule_delayed_work(&bcdev->charge_status_check_work,
			msecs_to_jiffies(500));
		break;
	case BC_USB_STATUS_NO_PRESENT:
		usb_present = false;
		chip->svid_verified = false;
		chip->usb_enum_status = false;
		if (is_usb_ocm_available(bcdev))
			pst = &bcdev->ocm_list[OCM_TYPE_USB];
		if (probe_done)
			schedule_delayed_work(&bcdev->usb_disconnect_work, msecs_to_jiffies(500));
		oplus_chg_wake_update_work();
		break;
	case BC_USB_STATUS_APSD_DONE:
		pst = &bcdev->ocm_list[OCM_TYPE_USB];
		if (is_usb_ocm_available(bcdev))
			oplus_chg_global_event(pst->ocm, OPLUS_CHG_EVENT_APSD_DONE);
		oplus_chg_wake_update_work();
		schedule_delayed_work(&bcdev->check_charger_type_work,
			msecs_to_jiffies(500));
		break;
	case BC_OTG_ENABLE:
		schedule_work(&bcdev->otg_enable_work);
		if (bcdev->otg_type == OTG_DEFAULT_DCDC)
			schedule_delayed_work(&bcdev->otg_status_check_work, 0);
		chip->otg_online = true;
		if (is_batt_ocm_available(bcdev))
			pst = &bcdev->ocm_list[OCM_TYPE_BATTERY];
		pr_err("OTG enabled!\n");
		break;
	case BC_OTG_DISABLE:
		schedule_work(&bcdev->otg_disable_work);
		chip->otg_online = false;
		if (is_batt_ocm_available(bcdev))
			pst = &bcdev->ocm_list[OCM_TYPE_BATTERY];
		pr_err("OTG disabled!\n");
		break;
	case BC_OPLUS_SVID:
#ifdef OPLUS_CHG_OP_DEF
		if (chip->svid_verified && chip->is_oplus_svid)
			return;
		chip->svid_verified = true;
		chip->is_oplus_svid = true;
		chip->pd_swarp = true;
#endif
		pst = &bcdev->ocm_list[OCM_TYPE_USB];
		if (is_usb_ocm_available(bcdev))
			oplus_chg_global_event(pst->ocm, OPLUS_CHG_EVENT_SWARP_ONLINE);
		if (is_usb_ocm_available(bcdev))
			oplus_chg_mod_event(pst->ocm, pst->ocm, OPLUS_CHG_EVENT_PRESENT);
		oplus_chg_wake_update_work();
		pr_info("SWARP adapter connected!\n");
		break;
	case BC_OTHER_SVID:
#ifdef OPLUS_CHG_OP_DEF
		if (chip->svid_verified)
			return;
		chip->svid_verified = true;
		chip->is_oplus_svid = false;
#endif
		pst = &bcdev->ocm_list[OCM_TYPE_USB];
		if (is_usb_ocm_available(bcdev))
			oplus_chg_mod_event(pst->ocm, pst->ocm, OPLUS_CHG_EVENT_PRESENT);
		oplus_chg_wake_update_work();
		pr_info("other pd adapter connected!\n");
		break;
	default:
		break;
	}

	if (pst && pst->ocm)
		oplus_chg_mod_changed(pst->ocm);
}

static int battery_chg_callback(void *priv, void *data, size_t len)
{
	struct pmic_glink_hdr *hdr = data;
	struct battery_chg_dev *bcdev = priv;

	pr_debug("owner: %u type: %u opcode: %#x len: %zu\n", hdr->owner,
		hdr->type, hdr->opcode, len);

	if (hdr->opcode == BC_NOTIFY_IND)
		handle_notification(bcdev, data, len);
	else
		handle_message(bcdev, data, len);

	return 0;
}

#if 0
static int battery_psy_set_charge_current(struct battery_chg_dev *bcdev,
					int val)
{
	int rc;
	u32 fcc_ua;

	if (!bcdev->num_thermal_levels)
		return 0;

	if (bcdev->num_thermal_levels < 0) {
		pr_err("Incorrect num_thermal_levels\n");
		return -EINVAL;
	}

	if (val < 0 || val > bcdev->num_thermal_levels)
		return -EINVAL;

	fcc_ua = bcdev->thermal_levels[val];

	rc = write_property_id(bcdev, &bcdev->ocm_list[OCM_TYPE_BATTERY],
				BATT_CHG_CTRL_LIM, fcc_ua);
	if (!rc) {
		bcdev->curr_thermal_level = val;
		pr_debug("Set FCC to %u uA\n", fcc_ua);
	}

	return rc;
}
#endif

static ssize_t moisture_detection_en_store(struct class *c,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	int rc;
	bool val;

	if (kstrtobool(buf, &val))
		return -EINVAL;

	rc = write_property_id(bcdev, &bcdev->ocm_list[OCM_TYPE_USB],
				USB_MOISTURE_DET_EN, val);
	if (rc < 0)
		return rc;

	return count;
}

static ssize_t moisture_detection_en_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct ocm_state *pst = &bcdev->ocm_list[OCM_TYPE_USB];
	int rc;

	rc = read_property_id(bcdev, pst, USB_MOISTURE_DET_EN);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n",
			pst->prop[USB_MOISTURE_DET_EN]);
}
static CLASS_ATTR_RW(moisture_detection_en);

static ssize_t moisture_detection_status_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct ocm_state *pst = &bcdev->ocm_list[OCM_TYPE_USB];
	int rc;

	rc = read_property_id(bcdev, pst, USB_MOISTURE_DET_STS);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n",
			pst->prop[USB_MOISTURE_DET_STS]);
}
static CLASS_ATTR_RO(moisture_detection_status);

static struct attribute *battery_class_attrs[] = {
	&class_attr_moisture_detection_status.attr,
	&class_attr_moisture_detection_en.attr,
	NULL,
};
ATTRIBUTE_GROUPS(battery_class);

#ifdef CONFIG_DEBUG_FS
static void battery_chg_add_debugfs(struct battery_chg_dev *bcdev)
{
	int rc;
	struct dentry *dir, *file;

	dir = debugfs_create_dir("battery_charger", NULL);
	if (IS_ERR(dir)) {
		rc = PTR_ERR(dir);
		pr_err("Failed to create charger debugfs directory, rc=%d\n",
			rc);
		return;
	}

	file = debugfs_create_bool("block_tx", 0600, dir, &bcdev->block_tx);
	if (IS_ERR(file)) {
		rc = PTR_ERR(file);
		pr_err("Failed to create block_tx debugfs file, rc=%d\n",
			rc);
		goto error;
	}

	bcdev->debugfs_dir = dir;

	return;
error:
	debugfs_remove_recursive(dir);
}
#else
static void battery_chg_add_debugfs(struct battery_chg_dev *bcdev) { }
#endif

static int battery_chg_parse_dt(struct battery_chg_dev *bcdev)
{
	struct device_node *node = bcdev->dev->of_node;
	struct ocm_state *pst = &bcdev->ocm_list[OCM_TYPE_BATTERY];
	enum of_gpio_flags flags;
	int i, rc, len;
	u32 prev, val;

	of_property_read_string(node, "qcom,wireless-fw-name",
				&bcdev->wls_fw_name);

	rc = of_property_count_elems_of_size(node, "qcom,thermal-mitigation",
						sizeof(u32));
	if (rc <= 0)
		return 0;

	len = rc;

	rc = read_property_id(bcdev, pst, BATT_CHG_CTRL_LIM_MAX);
	if (rc < 0)
		return rc;

	prev = pst->prop[BATT_CHG_CTRL_LIM_MAX];

	for (i = 0; i < len; i++) {
		rc = of_property_read_u32_index(node, "qcom,thermal-mitigation",
						i, &val);
		if (rc < 0)
			return rc;

		if (val > prev) {
			pr_err("Thermal levels should be in descending order\n");
			bcdev->num_thermal_levels = -EINVAL;
			return 0;
		}

		prev = val;
	}

	bcdev->thermal_levels = devm_kcalloc(bcdev->dev, len + 1,
					sizeof(*bcdev->thermal_levels),
					GFP_KERNEL);
	if (!bcdev->thermal_levels)
		return -ENOMEM;

	/*
	 * Element 0 is for normal charging current. Elements from index 1
	 * onwards is for thermal mitigation charging currents.
	 */

	bcdev->thermal_levels[0] = pst->prop[BATT_CHG_CTRL_LIM_MAX];

	rc = of_property_read_u32_array(node, "qcom,thermal-mitigation",
					&bcdev->thermal_levels[1], len);
	if (rc < 0) {
		pr_err("Error in reading qcom,thermal-mitigation, rc=%d\n", rc);
		return rc;
	}

	bcdev->num_thermal_levels = len;

	rc = of_property_read_u32(node, "oplus,otg_type", &bcdev->otg_type);
	if (rc < 0) {
		pr_err("can't get ic type, rc=%d\n", rc);
		return rc;
	}
	pr_info("otg type: %d\n", bcdev->otg_type);

	bcdev->vbus_ctrl = of_get_named_gpio_flags(node,
					"op,vbus-ctrl-gpio", 0, &flags);

	bcdev->wls_boost_soft_start = of_property_read_bool(node, "oplus,wls_boost_soft_start");

	return 0;
}

static int battery_chg_ship_mode(struct notifier_block *nb, unsigned long code,
		void *unused)
{
	struct battery_charger_ship_mode_req_msg msg = { { 0 } };
	struct battery_chg_dev *bcdev = container_of(nb, struct battery_chg_dev,
						     reboot_notifier);
	int rc;

	if (!bcdev->ship_mode_en)
		return NOTIFY_DONE;

	msg.hdr.owner = MSG_OWNER_BC;
	msg.hdr.type = MSG_TYPE_REQ_RESP;
	msg.hdr.opcode = BC_SHIP_MODE_REQ_SET;
	msg.ship_mode_type = SHIP_MODE_PMIC;

	if (code == SYS_POWER_OFF) {
		rc = battery_chg_write(bcdev, &msg, sizeof(msg));
		if (rc < 0)
			pr_emerg("Failed to write ship mode: %d\n", rc);
	}

	return NOTIFY_DONE;
}

/**********************************************************************
 * battery charge ops *
 **********************************************************************/
static void dump_regs(void)
{
	if(!probe_done)
		return;
	return;
}

static int smbchg_kick_wdt(void)
{
	if(!probe_done)
		return 0;
	return 0;
}

static int smbchg_set_fastchg_current_raw(int current_ma)
{
	int rc = 0;
	int prop_id = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct ocm_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if(!probe_done)
		return 0;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return -1;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	if (oplus_chg_is_wls_present(bcdev) || oplus_chg_is_wls_online(bcdev)) {
		pr_debug("wls present, exit\n");
		return 0;
	}
	pst = &bcdev->ocm_list[OCM_TYPE_BATTERY];

	if (chip->mmi_chg == 0) {
		pr_err("mmi_chg, set fcc to 0\n");
		current_ma = 0;
	}

	prop_id = BATT_CHG_CTRL_LIM;
	rc = write_property_id(bcdev, pst, prop_id, current_ma * 1000);
	if (rc)
		chg_err("set fcc to %d mA fail, rc=%d\n", current_ma, rc);
	else
		chg_err("set fcc to %d mA\n", current_ma);

	return rc;
}

static int smbchg_set_wls_boost_en(bool enable)
{
	int rc = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct ocm_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if(!probe_done)
		return 0;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return -1;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->ocm_list[OCM_TYPE_WLS];

	if (enable) {
		rc = write_property_id(bcdev, pst, WLS_BOOST_EN, 1);
	} else {
		rc = write_property_id(bcdev, pst, WLS_BOOST_EN, 0);
	}
	if (rc) {
		chg_err("set wls boost %s faile, rc=%d\n", enable ? "enable" : "disable", rc);
	} else {
		chg_err("set wls boost %s\n", enable ? "enable" : "disable");
	}
	return rc;
}

static int oplus_enable_haptic_boost(struct battery_chg_dev *bcdev, bool enable)
{
	int rc = 0;
	struct ocm_state *pst = NULL;

	if(!probe_done)
		return 0;

	pst = &bcdev->ocm_list[OCM_TYPE_USB];

	rc = write_property_id(bcdev, pst, USB_OTG_EN, enable);
	if (rc < 0) {
		pr_err("Set haptic boost %s error, rc=%d\n",
			enable ? "enable" : "disable", rc);
		return rc;
	}

	return rc;
}

bool qpnp_get_prop_vbus_collapse_status(void)
{
	int rc = 0;
	bool collapse_status = false;
	struct battery_chg_dev *bcdev = NULL;
	struct ocm_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if(!probe_done)
		return 0;

	if (!chip) {
		return false;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->ocm_list[OCM_TYPE_USB];

	rc = read_property_id(bcdev, pst, USB_VBUS_COLLAPSE_STATUS);
	if (rc < 0) {
		chg_err("read usb vbus_collapse_status fail, rc=%d\n", rc);
		return false;
	}
	collapse_status = pst->prop[USB_VBUS_COLLAPSE_STATUS];
	chg_err("read usb vbus_collapse_status[%d]\n",
			collapse_status);
	// return collapse_status; //TODO: nick.hu
	return false;
}

static int oplus_chg_set_input_current_with_no_aicl(int current_ma)
{
	int rc = 0;
	int prop_id = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct ocm_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if(!probe_done)
		return 0;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return -1;
	}
	if (chip->mmi_chg == 0 && current_ma != 0) {
		chg_debug("mmi_chg, return\n");
		return rc;
	}
	if (disable_usb_chg && current_ma != 0) {
		chg_debug("disable usb charge, return\n");
		return 0;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->ocm_list[OCM_TYPE_USB];

	prop_id = USB_INPUT_CURR_LIMIT;
	rc = write_property_id(bcdev, pst, prop_id, current_ma * 1000);
	if (rc)
		chg_err("set icl to %d mA fail, rc=%d\n", current_ma, rc);
	else
		chg_err("set icl to %d mA\n", current_ma);

	return rc;
}

static void smbchg_set_aicl_point(int vol)
{
	if(!probe_done)
		return;
	/*do nothing*/
}

static int usb_icl[] = {
	300, 500, 900, 1200, 1350, 1500, 1750, 2000, 3000,
};

static int oplus_chg_set_input_current(int current_ma)
{
	int rc = 0, i = 0;
	int chg_vol = 0;
	int aicl_point = 0;
	int prop_id = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct ocm_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if(!probe_done)
		return 0;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return -1;
	}

	if (chip->mmi_chg == 0 && current_ma != 0) {
		chg_debug("mmi_chg, return\n");
		return rc;
	}
	if (disable_usb_chg && current_ma != 0) {
		chg_debug("disable usb charge, return\n");
		return 0;
	}

	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->ocm_list[OCM_TYPE_USB];
	prop_id = USB_INPUT_CURR_LIMIT;

	chg_debug("usb input max current limit=%d setting %02x\n", current_ma, i);
	if (chip->batt_volt > 4100) {
		aicl_point = 4550;
	} else {
		aicl_point = 4500;
	}

	if (current_ma < 500) {
		i = 0;
		goto aicl_end;
	}

	i = 1; /* 500 */
	rc = write_property_id(bcdev, pst, prop_id, usb_icl[i] * 1000);
	if (rc) {
		chg_err("set icl to %d mA fail, rc=%d\n", usb_icl[i], rc);
	} else {
		chg_err("set icl to %d mA\n", usb_icl[i]);
	}
	usleep_range(50000, 51000);
	if (qpnp_get_prop_vbus_collapse_status() == true) {
		pr_debug("use 500 here\n");
		goto aicl_boost_back;
	}
	chg_vol = qpnp_get_prop_charger_voltage_now();
	if (chg_vol < aicl_point) {
		pr_debug("use 500 here\n");
		goto aicl_end;
	} else if (current_ma < 900)
		goto aicl_end;

	i = 2; /* 900 */
	rc = write_property_id(bcdev, pst, prop_id, usb_icl[i] * 1000);
	if (rc) {
		chg_err("set icl to %d mA fail, rc=%d\n", usb_icl[i], rc);
	} else {
		chg_err("set icl to %d mA\n", usb_icl[i]);
	}
	usleep_range(50000, 51000);
	if (qpnp_get_prop_vbus_collapse_status() == true) {
		i = i - 1;
		goto aicl_boost_back;
	}
	chg_vol = qpnp_get_prop_charger_voltage_now();
	if (chg_vol < aicl_point) {
		i = i - 1;
		goto aicl_pre_step;
	} else if (current_ma < 1200)
		goto aicl_end;

	i = 3; /* 1200 */
	rc = write_property_id(bcdev, pst, prop_id, usb_icl[i] * 1000);
	if (rc) {
		chg_err("set icl to %d mA fail, rc=%d\n", usb_icl[i], rc);
	} else {
		chg_err("set icl to %d mA\n", usb_icl[i]);
	}
	usleep_range(90000, 91000);
	if (qpnp_get_prop_vbus_collapse_status() == true) {
		i = i - 1;
		goto aicl_boost_back;
	}
	chg_vol = qpnp_get_prop_charger_voltage_now();
	if (chg_vol < aicl_point) {
		i = i - 1;
		goto aicl_pre_step;
	}

	i = 4; /* 1350 */
	rc = write_property_id(bcdev, pst, prop_id, usb_icl[i] * 1000);
	if (rc) {
		chg_err("set icl to %d mA fail, rc=%d\n", usb_icl[i], rc);
	} else {
		chg_err("set icl to %d mA\n", usb_icl[i]);
	}
	usleep_range(130000, 131000);
	if (qpnp_get_prop_vbus_collapse_status() == true) {
		i = i - 2;
		goto aicl_boost_back;
	}
	chg_vol = qpnp_get_prop_charger_voltage_now();
	if (chg_vol < aicl_point) {
		i = i - 2;
		goto aicl_pre_step;
	}

	i = 5; /* 1500 */
	rc = write_property_id(bcdev, pst, prop_id, usb_icl[i] * 1000);
	if (rc) {
		chg_err("set icl to %d mA fail, rc=%d\n", usb_icl[i], rc);
	} else {
		chg_err("set icl to %d mA\n", usb_icl[i]);
	}
	usleep_range(90000, 91000);
	if (qpnp_get_prop_vbus_collapse_status() == true) {
		i = i - 3;
		goto aicl_boost_back;
	}
	chg_vol = qpnp_get_prop_charger_voltage_now();
	if (chg_vol < aicl_point) {
		i = i - 3; /*We DO NOT use 1.2A here*/
		goto aicl_pre_step;
	} else if (current_ma < 1500) {
		i = i - 2; /*We use 1.2A here*/
		goto aicl_end;
	} else if (current_ma < 2000)
		goto aicl_end;

	i = 6; /* 1750 */
	rc = write_property_id(bcdev, pst, prop_id, usb_icl[i] * 1000);
	if (rc) {
		chg_err("set icl to %d mA fail, rc=%d\n", usb_icl[i], rc);
	} else {
		chg_err("set icl to %d mA\n", usb_icl[i]);
	}
	usleep_range(50000, 51000);
	if (qpnp_get_prop_vbus_collapse_status() == true) {
		i = i - 3;
		goto aicl_boost_back;
	}
	chg_vol = qpnp_get_prop_charger_voltage_now();
	if (chg_vol < aicl_point) {
		i = i - 3; /*1.2*/
		goto aicl_pre_step;
	}

	i = 7; /* 2000 */
	rc = write_property_id(bcdev, pst, prop_id, usb_icl[i] * 1000);
	if (rc) {
		chg_err("set icl to %d mA fail, rc=%d\n", usb_icl[i], rc);
	} else {
		chg_err("set icl to %d mA\n", usb_icl[i]);
	}
	usleep_range(50000, 51000);
	if (qpnp_get_prop_vbus_collapse_status() == true) {
		i = i - 2;
		goto aicl_boost_back;
	}
	chg_vol = qpnp_get_prop_charger_voltage_now();
	if (chg_vol < aicl_point) {
		i =  i - 2; /*1.5*/
		goto aicl_pre_step;
	} else if (current_ma < 3000)
		goto aicl_end;

	i = 8; /* 3000 */
	rc = write_property_id(bcdev, pst, prop_id, usb_icl[i] * 1000);
	if (rc) {
		chg_err("set icl to %d mA fail, rc=%d\n", usb_icl[i], rc);
	} else {
		chg_err("set icl to %d mA\n", usb_icl[i]);
	}
	usleep_range(90000, 91000);
	if (qpnp_get_prop_vbus_collapse_status() == true) {
		i = i - 1;
		goto aicl_boost_back;
	}
	chg_vol = qpnp_get_prop_charger_voltage_now();
	if (chg_vol < aicl_point) {
		i = i - 1;
		goto aicl_pre_step;
	} else if (current_ma >= 3000)
		goto aicl_end;

aicl_pre_step:
	rc = write_property_id(bcdev, pst, prop_id, usb_icl[i] * 1000);
	if (rc) {
		chg_err("set icl to %d mA fail, rc=%d\n", usb_icl[i], rc);
	} else {
		chg_err("set icl to %d mA\n", usb_icl[i]);
	}
	chg_debug("usb input max current limit aicl chg_vol=%d j[%d]=%d sw_aicl_point:%d aicl_pre_step\n", chg_vol, i, usb_icl[i], aicl_point);
	goto aicl_return;
aicl_end:
	rc = write_property_id(bcdev, pst, prop_id, usb_icl[i] * 1000);
	if (rc) {
		chg_err("set icl to %d mA fail, rc=%d\n", usb_icl[i], rc);
	} else {
		chg_err("set icl to %d mA\n", usb_icl[i]);
	}
	chg_debug("usb input max current limit aicl chg_vol=%d j[%d]=%d sw_aicl_point:%d aicl_end\n", chg_vol, i, usb_icl[i], aicl_point);
	goto aicl_return;
aicl_boost_back:
	rc = write_property_id(bcdev, pst, prop_id, usb_icl[i] * 1000);
	if (rc) {
		chg_err("set icl to %d mA fail, rc=%d\n", usb_icl[i], rc);
	} else {
		chg_err("set icl to %d mA\n", usb_icl[i]);
	}
	chg_debug("usb input max current limit aicl chg_vol=%d j[%d]=%d sw_aicl_point:%d aicl_boost_back\n", chg_vol, i, usb_icl[i], aicl_point);
	goto aicl_return;
aicl_return:
	chip->input_current_limit_ma = current_ma;
	return rc;
}

static int smbchg_float_voltage_set(int vfloat_mv)
{
	int rc = 0;
	int prop_id = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct ocm_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if(!probe_done)
		return 0;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return -1;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->ocm_list[OCM_TYPE_BATTERY];

	prop_id = BATT_VOLT_MAX;
	rc = write_property_id(bcdev, pst, prop_id, vfloat_mv * 1000);
	if (rc)
		chg_err("set fv to %d mV fail, rc=%d\n", vfloat_mv, rc);
	else
		chg_err("set fv to %d mV\n", vfloat_mv);

	return rc;
}

static int smbchg_term_current_set(int term_current)
{
	int rc = 0;

	if(!probe_done)
		return 0;
#if 0
	u8 val_raw = 0;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (term_current < 0 || term_current > 750)
		term_current = 150;

	val_raw = term_current / 50;
	rc = smblib_masked_write(&chip->pmic_spmi.smb5_chip->chg, TCCC_CHARGE_CURRENT_TERMINATION_CFG_REG,
			TCCC_CHARGE_CURRENT_TERMINATION_SETTING_MASK, val_raw);
	if (rc < 0)
		chg_err("Couldn't write TCCC_CHARGE_CURRENT_TERMINATION_CFG_REG rc=%d\n", rc);
#endif
	return rc;
}

static int smbchg_charging_enable(void)
{
	int rc = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct ocm_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if(!probe_done)
		return 0;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return -1;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->ocm_list[OCM_TYPE_BATTERY];

	pr_err("wkcs: enable charge, mmi_chg=%d\n", chip->mmi_chg);
	rc = write_property_id(bcdev, pst, BATT_CHG_EN, 1);
	if (rc)
		chg_err("set enable charging fail, rc=%d\n", rc);

	return rc;
}

static int smbchg_charging_disable(void)
{
	int rc = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct ocm_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if(!probe_done)
		return 0;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return -1;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	if (oplus_chg_is_wls_present(bcdev) || oplus_chg_is_wls_online(bcdev)) {
		pr_debug("wls present, exit\n");
		return 0;
	}
	pst = &bcdev->ocm_list[OCM_TYPE_BATTERY];

	pr_err("wkcs: disable charge\n");
	rc = write_property_id(bcdev, pst, BATT_CHG_EN, 0);
	if (rc)
		chg_err("set disable charging fail, rc=%d\n", rc);

	return rc;
}

static int smbchg_get_charge_enable(void)
{
	int rc = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct ocm_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if(!probe_done)
		return 0;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return -1;
	}

	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->ocm_list[OCM_TYPE_BATTERY];

	rc = read_property_id(bcdev, pst, BATT_CHG_EN);
	if (rc) {
		chg_err("set disable charging fail, rc=%d\n", rc);
		return rc;
	}
	if (pst->prop[BATT_CHG_EN])
		chg_err("get charge enable[%d]\n", pst->prop[BATT_CHG_EN]);

	return pst->prop[BATT_CHG_EN];
}

static int smbchg_usb_suspend_enable(void)
{
	int rc = 0;
	int prop_id = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct ocm_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if(!probe_done)
		return 0;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return -1;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->ocm_list[OCM_TYPE_USB];

	prop_id = USB_CHG_EN;
	rc = write_property_id(bcdev, pst, prop_id, 0);
	if (rc)
		chg_err("set suspend fail, rc=%d\n", rc);
	else
		chg_err("set chg suspend\n");

	return rc;
}

static int smbchg_usb_suspend_disable(void)
{
	int rc = 0;
	int prop_id = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct ocm_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if(!probe_done)
		return 0;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return -1;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->ocm_list[OCM_TYPE_USB];

	prop_id = USB_CHG_EN;
	rc = write_property_id(bcdev, pst, prop_id, 1);
	if (rc)
		chg_err("set unsuspend to fail, rc=%d\n", rc);
	else
		chg_err("set chg unsuspend\n");

	return rc;
}

static int oplus_chg_get_charge_counter(void)
{
       int rc = 0;
       int prop_id = 0;
       struct battery_chg_dev *bcdev = NULL;
       struct ocm_state *pst = NULL;
       struct oplus_chg_chip *chip = g_oplus_chip;

       if(!probe_done)
               return 0;

       if (!chip) {
               return -1;
       }
       bcdev = chip->pmic_spmi.bcdev_chip;
       pst = &bcdev->ocm_list[OCM_TYPE_BATTERY];

       prop_id = BATT_CHG_COUNTER;
       rc = read_property_id(bcdev, pst, prop_id);
       if (rc < 0) {
               chg_err("read battery curr fail, rc=%d\n", rc);
               return prop_id;
       }

       return prop_id;
}

static int smbchg_usb_get_input_current_limit(int *curr_ua)
{
	int rc = 0;
	int prop_id = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct ocm_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if(!probe_done) {
		*curr_ua = 0;
		return 0;
	}

	if (!chip) {
		chg_err("chip is NULL!\n");
		return -ENODEV;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->ocm_list[OCM_TYPE_USB];

	prop_id = USB_INPUT_CURR_LIMIT;
	rc = read_property_id(bcdev, pst, prop_id);
	if (rc)
		chg_err("can't read input current limit, rc=%d\n", rc);
	else
		*curr_ua = pst->prop[OCM_TYPE_USB];

	return rc;
}

static int smbchg_debug_disable_chg(bool disable)
{
	int rc = 0;

	if (disable)
		rc = smbchg_usb_suspend_enable();
	else
		rc = smbchg_usb_suspend_disable();

	disable_usb_chg = disable;

	return rc;
}

static int oplus_chg_hw_init(void)
{
	int boot_mode = get_boot_mode();

	if(!probe_done)
		return 0;

	if (boot_mode != MSM_BOOT_MODE__RF && boot_mode != MSM_BOOT_MODE__WLAN) {
		smbchg_usb_suspend_disable();
	} else {
		smbchg_usb_suspend_enable();
	}

	smbchg_charging_enable();

	return 0;
}

static int smbchg_set_rechg_vol(int rechg_vol)
{
	if(!probe_done)
		return 0;

	return 0;
}

static int smbchg_reset_charger(void)
{
	if(!probe_done)
		return 0;

	return 0;
}

static int smbchg_read_full(void)
{
#if 0
	int rc = 0;
	u8 stat = 0;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!oplus_chg_is_usb_present())
		return 0;

	rc = smblib_read(&chip->pmic_spmi.smb5_chip->chg, BATTERY_CHARGER_STATUS_1_REG, &stat);
	if (rc < 0) {
		chg_err("Couldn't read BATTERY_CHARGER_STATUS_1 rc=%d\n", rc);
		return 0;
	}
	stat = stat & BATTERY_CHARGER_STATUS_MASK;

	if (stat == TERMINATE_CHARGE || stat == INHIBIT_CHARGE)
		return 1;
#endif
	return 0;
}

static int smbchg_otg_enable(void)
{
	if(!probe_done)
		return 0;

	return 0;
}

static int smbchg_otg_disable(void)
{
	if(!probe_done)
		return 0;

	return 0;
}

static int oplus_set_chging_term_disable(void)
{
	if(!probe_done)
		return 0;

	return 0;
}

static bool qcom_check_charger_resume(void)
{
	if(!probe_done)
		return true;

	return true;
}

static int smbchg_get_chargerid_volt(void)
{
	if(!probe_done)
		return 0;

	return 0;
}

#ifdef OPLUS_CHG_OP_DEF
int smbchg_get_vph_voltage(void)
{
	int ret, result;
	struct battery_chg_dev *bcdev = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!probe_done)
		return 0;
	bcdev = chip->pmic_spmi.bcdev_chip;
	if (bcdev->op_vph_vol_chan) {
		ret = iio_read_channel_processed(
				bcdev->op_vph_vol_chan,
				&result);
		bcdev->vph_vol = result/1000;
	} else {
		pr_err("op_vph_vol_chan no found!\n");
		bcdev->vph_vol = -EINVAL;
		return -ENODATA;
	}
	return bcdev->vph_vol;
}

int smbchg_get_hw_detect_status(void)
{
	struct battery_chg_dev *bcdev = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!probe_done)
		return 0;

	if (!chip) {
		pr_err("chip null\n");
		return 0;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
		if (!bcdev) {
		pr_err("bcdev null\n");
		return 0;
	}
	oplus_chg_hw_detect(bcdev, &bcdev->hw_detect);
	return bcdev->hw_detect;
}
#endif

static void smbchg_set_chargerid_switch_val(int value)
{
	if(!probe_done)
		return;

	return;
}


static int smbchg_get_chargerid_switch_val(void)
{
	if(!probe_done)
		return -1;

	return -1;
}

static bool smbchg_need_to_check_ibatt(void)
{
	if(!probe_done)
		return false;

	return false;
}

static int smbchg_get_chg_current_step(void)
{
	if(!probe_done)
		return 25;

	return 25;
}

static int opchg_get_charger_type(void)
{
	int rc = 0;
	int prop_id = 0;
	static int charger_type = POWER_SUPPLY_TYPE_UNKNOWN;
	struct battery_chg_dev *bcdev = NULL;
	struct ocm_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if(!probe_done)
		return 0;

	if (!chip) {
		return POWER_SUPPLY_TYPE_UNKNOWN;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->ocm_list[OCM_TYPE_USB];

	prop_id = USB_ADAP_TYPE;
	rc = read_property_id(bcdev, pst, prop_id);
	if (rc < 0) {
		chg_err("read usb charger_type fail, rc=%d\n", rc);
		return charger_type;
	}
	chip->oplus_usb_type = pst->prop[prop_id];
	switch (pst->prop[prop_id]) {
		case POWER_SUPPLY_USB_TYPE_UNKNOWN:
			charger_type = POWER_SUPPLY_TYPE_UNKNOWN;
			break;
		case POWER_SUPPLY_USB_TYPE_SDP:
			charger_type = POWER_SUPPLY_TYPE_USB;
			break;
		case POWER_SUPPLY_USB_TYPE_CDP:
			charger_type = POWER_SUPPLY_TYPE_USB_CDP;
			break;
		case POWER_SUPPLY_USB_TYPE_DCP:
			charger_type = POWER_SUPPLY_TYPE_USB_DCP;
			break;
		default:
			charger_type = POWER_SUPPLY_TYPE_USB_DCP;
			break;
	}

	return charger_type;
}

static int qpnp_get_prop_charger_voltage_now(void)
{
	int rc = 0;
	int prop_id = 0;
	static int vbus_volt = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct ocm_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if(!probe_done)
		return 0;

	if (!chip) {
		return 0;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->ocm_list[OCM_TYPE_USB];

	prop_id = WLS_VOLT_NOW;
	rc = read_property_id(bcdev, pst, prop_id);
	if (rc < 0) {
		chg_err("read usb vbus_volt fail, rc=%d\n", rc);
		return vbus_volt;
	}
	vbus_volt = pst->prop[prop_id] / 1000;

	return vbus_volt;
}

static int oplus_get_ibus_current(void)
{
	int rc = 0;
	int prop_id = 0;
	static int ibus = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct ocm_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if(!probe_done)
		return 0;

	if (!chip) {
		return -1;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->ocm_list[OCM_TYPE_USB];

	prop_id = USB_CURR_NOW;
	rc = read_property_id(bcdev, pst, prop_id);
	if (rc < 0) {
		chg_err("read battery curr fail, rc=%d\n", rc);
		return ibus;
	}
	ibus = DIV_ROUND_CLOSEST((int)pst->prop[prop_id], 1000);

	return ibus;
}

static bool oplus_chg_is_usb_present(void)
{
	int rc = 0;
	int prop_id = 0;
	bool vbus_rising = false;
	struct battery_chg_dev *bcdev = NULL;
	struct ocm_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if(!probe_done)
		return false;

	if (!chip) {
		return false;
	}
	if (disable_usb_chg)
		return false;
	if (usb_present)
		return true;
#ifdef OPLUS_CHG_OP_DEF
	if (chip->svid_verified)
		return true;
#endif

	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->ocm_list[OCM_TYPE_USB];

	prop_id = USB_ONLINE;
	rc = read_property_id(bcdev, pst, prop_id);
	if (rc < 0) {
		chg_err("read usb vbus_rising fail, rc=%d\n", rc);
		return false;
	}
	vbus_rising = pst->prop[prop_id];
	if (true == oplus_get_pon_chg() && false == vbus_rising) {
		oplus_set_pon_chg(false);
	}
	if (!vbus_rising && oplus_warp_get_fastchg_started()) {
		if (qpnp_get_prop_charger_voltage_now() > 2000 || chip->vbatt_num == 2) {
			pr_err("USBIN_PLUGIN_RT_STS_BIT low but fastchg started true and (chg vol > 2V or SWARP)\n");
			vbus_rising = true;
		}
	}

	return vbus_rising;
}

static int qpnp_get_battery_voltage(void)
{
	return 3800;//Not use anymore
}

#if 0
static int get_boot_mode(void)
{
	return 0;
}
#endif

static int smbchg_get_boot_reason(void)
{
	return 0;
}

static int oplus_chg_get_shutdown_soc(void)
{
	struct battery_chg_dev *bcdev = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;
	struct regmap *regmap = NULL;
	unsigned int value;
	int rc;

	if (!chip)
		return -ENODEV;

	bcdev = chip->pmic_spmi.bcdev_chip;

	regmap = oplus_chg_get_pm8350_regmap(bcdev);
	if (regmap == NULL) {
		pr_err("pm8350 regmap not found\n");
		return -ENODEV;
	}

	rc = regmap_read(regmap, 0x088d, &value);
	if (rc < 0) {
		pr_err("can't read backup soc, rc=%d\n", rc);
		return rc;
	}
	pr_info("oplus_chg_get_shutdown_soc:0x%x,%d\n", value, value);

	return value;
}

static int oplus_chg_backup_soc(int backup_soc)
{
	struct battery_chg_dev *bcdev = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;
	struct regmap *regmap = NULL;
	int rc;

	if(!probe_done)
		return -ENODEV;

	if (!chip)
		return -ENODEV;

	bcdev = chip->pmic_spmi.bcdev_chip;

	regmap = oplus_chg_get_pm8350_regmap(bcdev);
	if (regmap == NULL) {
		pr_err("pm8350 regmap not found\n");
		return -ENODEV;
	}

	rc = regmap_update_bits(regmap, 0x088d, 0xff, (u8)backup_soc);
	if (rc < 0) {
		pr_err("can't backup soc, rc=%d\n", rc);
		return rc;
	}

	return 0;
}

static int smbchg_get_aicl_level_ma(void)
{
	return 0;
}

static void smbchg_rerun_aicl(void)
{
	//smbchg_aicl_enable(false);
	/* Add a delay so that AICL successfully clears */
	//msleep(50);
	//smbchg_aicl_enable(true);
}

#ifdef CONFIG_OPLUS_RTC_DET_SUPPORT
static int rtc_reset_check(void)
{
	struct rtc_time tm;
	struct rtc_device *rtc;
	int rc = 0;

	if(!probe_done)
		return 0;

	rtc = rtc_class_open(CONFIG_RTC_HCTOSYS_DEVICE);
	if (rtc == NULL) {
		pr_err("%s: unable to open rtc device (%s)\n",
			__FILE__, CONFIG_RTC_HCTOSYS_DEVICE);
		return 0;
	}

	rc = rtc_read_time(rtc, &tm);
	if (rc) {
		pr_err("Error reading rtc device (%s) : %d\n",
			CONFIG_RTC_HCTOSYS_DEVICE, rc);
		goto close_time;
	}

	rc = rtc_valid_tm(&tm);
	if (rc) {
		pr_err("Invalid RTC time (%s): %d\n",
			CONFIG_RTC_HCTOSYS_DEVICE, rc);
		goto close_time;
	}

	if ((tm.tm_year == 70) && (tm.tm_mon == 0) && (tm.tm_mday <= 1)) {
		chg_debug(": Sec: %d, Min: %d, Hour: %d, Day: %d, Mon: %d, Year: %d  @@@ wday: %d, yday: %d, isdst: %d\n",
			tm.tm_sec, tm.tm_min, tm.tm_hour, tm.tm_mday, tm.tm_mon, tm.tm_year,
			tm.tm_wday, tm.tm_yday, tm.tm_isdst);
		rtc_class_close(rtc);
		return 1;
	}

	chg_debug(": Sec: %d, Min: %d, Hour: %d, Day: %d, Mon: %d, Year: %d  ###  wday: %d, yday: %d, isdst: %d\n",
		tm.tm_sec, tm.tm_min, tm.tm_hour, tm.tm_mday, tm.tm_mon, tm.tm_year,
		tm.tm_wday, tm.tm_yday, tm.tm_isdst);

close_time:
	rtc_class_close(rtc);
	return 0;
}
#endif /* CONFIG_OPLUS_RTC_DET_SUPPORT */

#ifdef CONFIG_OPLUS_SHORT_C_BATT_CHECK
/* This function is getting the dynamic aicl result/input limited in mA.
 * If charger was suspended, it must return 0(mA).
 * It meets the requirements in SDM660 platform.
 */
static int oplus_chg_get_dyna_aicl_result(void)
{
	struct power_supply *usb_psy = NULL;
	union power_supply_propval pval = {0, };

	if(!probe_done)
		return 0;

	usb_psy = power_supply_get_by_name("usb");
	if (usb_psy) {
		power_supply_get_property(usb_psy,
				POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
				&pval);
		return pval.intval / 1000;
	}

	return 1000;
}
#endif /* CONFIG_OPLUS_SHORT_C_BATT_CHECK */

static int oplus_chg_get_charger_subtype(void)
{
	int rc = 0;
	int prop_id = 0;
	static int charg_subtype = CHARGER_SUBTYPE_DEFAULT;
	struct battery_chg_dev *bcdev = NULL;
	struct ocm_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if(!probe_done)
		return 0;

	if (!chip) {
		return CHARGER_SUBTYPE_DEFAULT;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->ocm_list[OCM_TYPE_USB];

	prop_id = USB_ADAP_TYPE;
	rc = read_property_id(bcdev, pst, prop_id);
	if (rc < 0) {
		chg_err("read charger type fail, rc=%d\n", rc);
		if (!chip->charger_exist)
			charg_subtype = CHARGER_SUBTYPE_DEFAULT;
		return charg_subtype;
	}
	switch (pst->prop[prop_id]) {
		case POWER_SUPPLY_USB_TYPE_PD:
		case POWER_SUPPLY_USB_TYPE_PD_DRP:
		case POWER_SUPPLY_USB_TYPE_PD_PPS:
			charg_subtype = CHARGER_SUBTYPE_PD;
			break;
		default:
			charg_subtype = CHARGER_SUBTYPE_DEFAULT;
			break;
	}

	if (charg_subtype == CHARGER_SUBTYPE_DEFAULT) {
		rc = read_property_id(bcdev, pst, USB_ADAP_SUBTYPE);
		if (rc < 0) {
			chg_err("read charger subtype fail, rc=%d\n", rc);
			if (!chip->charger_exist)
				charg_subtype = CHARGER_SUBTYPE_DEFAULT;
			return charg_subtype;
		}
		switch (pst->prop[USB_ADAP_SUBTYPE]) {
			case CHARGER_SUBTYPE_FASTCHG_WARP:
				charg_subtype = CHARGER_SUBTYPE_FASTCHG_WARP;
				break;
			case CHARGER_SUBTYPE_FASTCHG_SWARP:
				charg_subtype = CHARGER_SUBTYPE_FASTCHG_SWARP;
				break;
			case CHARGER_SUBTYPE_QC:
				charg_subtype = CHARGER_SUBTYPE_QC;
				chip->oplus_usb_type = OPLUS_CHG_USB_TYPE_QC2;
				break;
			default:
				charg_subtype = CHARGER_SUBTYPE_DEFAULT;
				break;
		}
	}

	return charg_subtype;
}

static bool oplus_chg_get_pd_type(void)
{
	int rc = 0;
	int prop_id = 0;
	static bool is_pd_type = false;
	struct battery_chg_dev *bcdev = NULL;
	struct ocm_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if(!probe_done)
		return 0;

	if (!chip) {
		return false;
	}
	if (!chip->svid_verified)
		return false;

	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->ocm_list[OCM_TYPE_USB];

	prop_id = USB_ADAP_TYPE;
	rc = read_property_id(bcdev, pst, prop_id);
	if (rc < 0) {
		chg_err("read usb pd_type fail, rc=%d\n", rc);
		if (!chip->charger_exist)
			is_pd_type = false;
		return is_pd_type;
	}
	switch (pst->prop[prop_id]) {
		case POWER_SUPPLY_USB_TYPE_PD:
		case POWER_SUPPLY_USB_TYPE_PD_DRP:
		case POWER_SUPPLY_USB_TYPE_PD_PPS:
			is_pd_type = true;
			break;
		default:
			is_pd_type = false;
			break;
	}

	return is_pd_type;
}

static int oplus_chg_set_pd_config(void)
{
	int rc = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct ocm_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if(!probe_done)
		return 0;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return -1;
	}

	if (!chip->svid_verified || chip->is_oplus_svid) {
		chg_err("svid_verified = %d, is_oplus_svid = %d\n", chip->svid_verified,  chip->is_oplus_svid);
		return -1;
	}

	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->ocm_list[OCM_TYPE_USB];

	if (chip->limits.vbatt_pdqc_to_5v_thr > 0 && chip->charger_volt > 7500 && chip->batt_volt > chip->limits.vbatt_pdqc_to_5v_thr) {
		rc = write_property_id(bcdev, pst, USB_CHG_SET_PDO, 5000);
		if (rc)
			chg_err("set PDO 5V fail, rc=%d\n", rc);
		else
			chg_err("set PDO 5V OK\n");
	} else if (chip->batt_volt < 5000) {//alway to 9
		rc = write_property_id(bcdev, pst, USB_CHG_SET_PDO, 9000);
		if (rc)
			chg_err("set PDO 9V fail, rc=%d\n", rc);
		else
			chg_err("set PDO 9V OK\n");

		rc = write_property_id(bcdev, pst, USB_INPUT_CURR_LIMIT, 2000 * 1000);
		if (rc) {
			chg_err("pd set icl to 2A fail, rc=%d\n", rc);
		} else {
			chg_err("pd set icl to 2A\n");
		}
	}

	return rc;
}

static int oplus_chg_set_qc_config(void)
{
	int rc = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct ocm_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if(!probe_done)
		return 0;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return -1;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->ocm_list[OCM_TYPE_USB];

	if (chip->limits.vbatt_pdqc_to_5v_thr > 0 && chip->charger_volt > 7500 && chip->batt_volt > chip->limits.vbatt_pdqc_to_5v_thr) {
		rc = write_property_id(bcdev, pst, USB_CHG_SET_QC, 5000);
		if (rc)
			chg_err("set QC 5V fail, rc=%d\n", rc);
		else
			chg_err("set QC 5V OK\n");
	} else if (chip->batt_volt < 5000) {//alway to 9
		rc = write_property_id(bcdev, pst, USB_CHG_SET_QC, 9000);
		if (rc)
			chg_err("set QC 9V fail, rc=%d\n", rc);
		else
			chg_err("set QC 9V OK\n");
	}

	return rc;
}
#ifdef OPLUS_CHG_OP_DEF
static int smbchg_lcm_en(bool en)
{
	int rc = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct ocm_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!probe_done)
		return 0;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return -ENODEV;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->ocm_list[OCM_TYPE_USB];
	if (en)
		rc = write_property_id(bcdev, pst, USB_CHG_LCM_EN, 0);
	else
		rc = write_property_id(bcdev, pst, USB_CHG_LCM_EN, 1);
	if (rc < 0)
		pr_err("set lcm to %u error, rc=%d\n", en, rc);
	else
		pr_info("set lcm to %d \n", en);

	return rc;
}
#endif
static int oplus_chg_set_pdo_5v(void)
{
	int rc = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct ocm_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if(!probe_done)
		return 0;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return -1;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->ocm_list[OCM_TYPE_USB];

	rc = write_property_id(bcdev, pst, USB_CHG_SET_QC, 5000);
	if (rc)
		chg_err("set QC 5V fail, rc=%d\n", rc);
	else
		chg_err("set QC 5V OK\n");

	return rc;
}


static int oplus_chg_enable_qc_detect(void)
{
	int rc = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct ocm_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if(!probe_done)
		return 0;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return -1;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->ocm_list[OCM_TYPE_USB];

	rc = write_property_id(bcdev, pst, USB_CHG_SET_QC, 0);

	return rc;
}

#ifndef CONFIG_OPLUS_CHG_OOS
void oplus_turn_off_power_when_adsp_crash(void)
{
#if 0
	struct oplus_chg_chip *chip = g_oplus_chip;
	struct battery_chg_dev *bcdev = NULL;

	if (!chip) {
		printk(KERN_ERR "!!!chip null\n");
		return;
	}

	bcdev = chip->pmic_spmi.bcdev_chip;
	if (bcdev->otg_online == true) {
		bcdev->otg_online = false;
		oplus_wpc_set_booster_en_val(0);
		oplus_wpc_set_ext1_wired_otg_en_val(0);
	}
#endif
}
EXPORT_SYMBOL(oplus_turn_off_power_when_adsp_crash);

void oplus_adsp_crash_recover_work(void)
{
#if 0
	struct oplus_chg_chip *chip = g_oplus_chip;
	struct battery_chg_dev *bcdev = NULL;

	if (!chip) {
		printk(KERN_ERR "!!!chip null\n");
		return;
	}

	bcdev = chip->pmic_spmi.bcdev_chip;
	schedule_delayed_work(&bcdev->adsp_crash_recover_work, round_jiffies_relative(msecs_to_jiffies(1500)));
#endif
}
EXPORT_SYMBOL(oplus_adsp_crash_recover_work);
#endif

static int oplus_input_current_limit_ctrl_by_warp_write(int current_ma)
{
	int rc = 0;
	int real_ibus = 0;
	int temp_curr = 0;

	if(!probe_done)
		return 0;

	real_ibus = oplus_get_ibus_current();
	chg_err(" get input_current = %d\n", real_ibus);

	if (current_ma > real_ibus) {
		for (temp_curr = real_ibus; temp_curr < current_ma; temp_curr += 500) {
			msleep(35);
			rc = oplus_chg_set_input_current_with_no_aicl(temp_curr);
			chg_err("[up] set input_current = %d\n", temp_curr);
		}
	} else {
		for (temp_curr = real_ibus; temp_curr > current_ma; temp_curr -= 500) {
			msleep(35);
			rc = oplus_chg_set_input_current_with_no_aicl(temp_curr);
			chg_err("[down] set input_current = %d\n", temp_curr);
		}
	}

	rc = oplus_chg_set_input_current_with_no_aicl(current_ma);
	return rc;
}

#ifdef OPLUS_CHG_OP_DEF
static int oplus_chg_disconnect_pd(bool disconnect)
{
	int rc = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct ocm_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if(!probe_done)
		return 0;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return -1;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->ocm_list[OCM_TYPE_USB];

	rc = write_property_id(bcdev, pst, USB_DISCONNECT_PD, disconnect);
	if (rc < 0)
		pr_err("can't set pd %s\n", disconnect ? "disconnect" : "connect");
	else
		pr_info("set pd %s\n", disconnect ? "disconnect" : "connect");

	return rc;
}
static void report_abnormal_vol_status(void)
{
	struct battery_chg_dev *bcdev = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;
	//char *detected[2] = { "USB_CONTAMINANT=ABNORMAL_VOLTAGE", NULL };
	char *detected[2] = { "USB_CONTAMINANT=DETECTED", NULL };

	if (!probe_done)
		return;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	if (!bcdev)
		return;
	pr_info("USB_CONTAMINANT=ABNORMAL_VOLTAGE\n");
	kobject_uevent_env(&bcdev->dev->kobj, KOBJ_CHANGE, detected);
}

static void smbchg_apsd_rerun(void)
{
	int rc = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct ocm_state *pst = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!probe_done)
		return;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	if (!bcdev)
		return;
	pst = &bcdev->ocm_list[OCM_TYPE_USB];
	rc = write_property_id(bcdev, pst, USB_CHG_RERUN_APSD, 1);
	if (rc < 0)
		pr_err("apsd rerun error, rc=%d\n", rc);
	else
		pr_info("apsd rerun\n");
}
#endif

static int get_current_time(unsigned long *now_tm_sec)
{
	struct rtc_time tm;
	struct rtc_device *rtc;
	int rc;

	rtc = rtc_class_open(CONFIG_RTC_HCTOSYS_DEVICE);
	if (rtc == NULL) {
		pr_err("%s: unable to open rtc device (%s)\n",
			__FILE__, CONFIG_RTC_HCTOSYS_DEVICE);
		return -EINVAL;
	}

	rc = rtc_read_time(rtc, &tm);
	if (rc) {
		pr_err("Error reading rtc device (%s) : %d\n",
			CONFIG_RTC_HCTOSYS_DEVICE, rc);
		goto close_time;
	}

	rc = rtc_valid_tm(&tm);
	if (rc) {
		pr_err("Invalid RTC time (%s): %d\n",
			CONFIG_RTC_HCTOSYS_DEVICE, rc);
		goto close_time;
	}
	rtc_tm_to_time(&tm, now_tm_sec);

close_time:
	rtc_class_close(rtc);
	return rc;
}

static unsigned long suspend_tm_sec = 0;
static int battery_chg_pm_resume(struct device *dev)
{
	int rc = 0;
	unsigned long resume_tm_sec = 0;
	unsigned long sleep_time = 0;

	if (!g_oplus_chip)
		return 0;

	rc = get_current_time(&resume_tm_sec);
	if (rc || suspend_tm_sec == -1) {
		chg_err("RTC read failed\n");
		sleep_time = 0;
	} else {
		sleep_time = resume_tm_sec - suspend_tm_sec;
	}

	if (sleep_time < 0) {
		sleep_time = 0;
	}

	oplus_chg_soc_update_when_resume(sleep_time);

	return 0;
}

static int battery_chg_pm_suspend(struct device *dev)
{
	if (!g_oplus_chip)
		return 0;

	if (get_current_time(&suspend_tm_sec)) {
		chg_err("RTC read failed\n");
		suspend_tm_sec = -1;
	}

	return 0;
}

/**
 * qti_battery_charger_get_prop() - Gets the property being requested
 *
 * @name: Power supply name
 * @prop_id: Property id to be read
 * @val: Pointer to value that needs to be updated
 *
 * Return: 0 if success, negative on error.
 */
int qti_battery_charger_get_prop(const char *name,
				enum battery_charger_prop prop_id, int *val)
{
	struct oplus_chg_chip *chip = g_oplus_chip;
	struct battery_chg_dev *bcdev;
	struct ocm_state *pst = NULL;
	int rc = 0;

	if (!probe_done)
		return -ENODEV;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return -ENODEV;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->ocm_list[OCM_TYPE_BATTERY];

	switch (pst->prop[prop_id]) {
	case BATTERY_RESISTANCE:
		pst = &bcdev->ocm_list[OCM_TYPE_BATTERY];
		rc = read_property_id(bcdev, pst, BATT_RESISTANCE);
		if (!rc)
			*val = pst->prop[BATT_RESISTANCE];
		break;
	default:
		break;
	}

	return rc;
}
EXPORT_SYMBOL(qti_battery_charger_get_prop);

static const struct dev_pm_ops battery_chg_pm_ops = {
	.resume		= battery_chg_pm_resume,
	.suspend	= battery_chg_pm_suspend,
};

struct oplus_chg_operations  battery_chg_ops = {
	.dump_registers = dump_regs,
	.kick_wdt = smbchg_kick_wdt,
	.hardware_init = oplus_chg_hw_init,
	.charging_current_write_fast = smbchg_set_fastchg_current_raw,
	.set_wls_boost_en = smbchg_set_wls_boost_en,
	.set_aicl_point = smbchg_set_aicl_point,
	.input_current_write = oplus_chg_set_input_current,
	.get_input_current_max = smbchg_usb_get_input_current_limit,
	.debug_disable_chg = smbchg_debug_disable_chg,
	.float_voltage_write = smbchg_float_voltage_set,
	.term_current_set = smbchg_term_current_set,
	.charging_enable = smbchg_charging_enable,
	.charging_disable = smbchg_charging_disable,
	.get_charging_enable = smbchg_get_charge_enable,
	.charger_suspend = smbchg_usb_suspend_enable,
	.charger_unsuspend = smbchg_usb_suspend_disable,
	.set_rechg_vol = smbchg_set_rechg_vol,
	.reset_charger = smbchg_reset_charger,
	.read_full = smbchg_read_full,
	.otg_enable = smbchg_otg_enable,
	.otg_disable = smbchg_otg_disable,
	.set_charging_term_disable = oplus_set_chging_term_disable,
	.check_charger_resume = qcom_check_charger_resume,
	.get_chargerid_volt = smbchg_get_chargerid_volt,
	.set_chargerid_switch_val = smbchg_set_chargerid_switch_val,
	.get_chargerid_switch_val = smbchg_get_chargerid_switch_val,
	.need_to_check_ibatt = smbchg_need_to_check_ibatt,
	.get_chg_current_step = smbchg_get_chg_current_step,
	.get_charger_type = opchg_get_charger_type,
	.get_charger_volt = qpnp_get_prop_charger_voltage_now,
	.get_charger_current = oplus_get_ibus_current,
	.check_chrdet_status = oplus_chg_is_usb_present,
	.get_instant_vbatt = qpnp_get_battery_voltage,
	.get_boot_mode = get_boot_mode,
	.get_boot_reason = smbchg_get_boot_reason,
	.get_rtc_soc = oplus_chg_get_shutdown_soc,
	.set_rtc_soc = oplus_chg_backup_soc,
	.get_aicl_ma = smbchg_get_aicl_level_ma,
	.rerun_aicl = smbchg_rerun_aicl,
#ifdef CONFIG_OPLUS_RTC_DET_SUPPORT
	.check_rtc_reset = rtc_reset_check,
#endif
#ifdef CONFIG_OPLUS_SHORT_C_BATT_CHECK
	.get_dyna_aicl_result = oplus_chg_get_dyna_aicl_result,
#endif
	.get_charger_subtype = oplus_chg_get_charger_subtype,
	.oplus_chg_get_pd_type = oplus_chg_get_pd_type,
	.oplus_chg_pd_setup = oplus_chg_set_pd_config,
	.set_qc_config = oplus_chg_set_qc_config,
	.enable_qc_detect = oplus_chg_enable_qc_detect,
	.input_current_ctrl_by_warp_write = oplus_input_current_limit_ctrl_by_warp_write,
	//.input_current_write_without_aicl = mp2650_input_current_limit_without_aicl,
	//.oplus_chg_wdt_enable = mp2650_wdt_enable,
	.disconnect_vbus = oplus_disconnect_vbus,
	.oplus_chg_get_charge_counter = oplus_chg_get_charge_counter,
	.otg_set_switch = oplus_set_otg_switch_status,
#ifdef OPLUS_CHG_OP_DEF
	.disconnect_pd = oplus_chg_disconnect_pd,
	.pdo_5v = oplus_chg_set_pdo_5v,
	.chg_lcm_en = smbchg_lcm_en,
	.get_vph_volt = smbchg_get_vph_voltage,
	.get_hw_detect = smbchg_get_hw_detect_status,
	.report_vol_status = report_abnormal_vol_status,
	.rerun_apsd = smbchg_apsd_rerun,
	.update_usb_type = oplus_update_usb_type,
#endif
};

static int smbchg_wls_input_enable(struct oplus_chg_ic_dev *ic_dev, bool en)
{
	if(!probe_done)
		return 0;
	// todo;
	return 0;
}

static int smbchg_wls_output_enable(struct oplus_chg_ic_dev *ic_dev, bool en)
{
	int rc = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct ocm_state *pst = NULL;

	if(!probe_done)
		return 0;

	if (ic_dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &bcdev->ocm_list[OCM_TYPE_BATTERY];

	rc = write_property_id(bcdev, pst, BATT_CHG_EN, en ? 1 : 0);
	if (rc)
		pr_err("set %s charging fail, rc=%d\n", en ? "enable": "disable", rc);

	return rc;
}

static int smbchg_wls_set_icl(struct oplus_chg_ic_dev *ic_dev, int icl_ma)
{
	int rc = 0;
	struct ocm_state *pst = NULL;
	struct battery_chg_dev *chip;

	if(!probe_done)
		return 0;

	if (ic_dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &chip->ocm_list[OCM_TYPE_WLS];

	rc = write_property_id(chip, pst, WLS_INPUT_CURR_LIMIT, icl_ma * 1000);
	if (rc < 0)
		pr_err("set wls icl to %u error, rc=%d\n", icl_ma, rc);
	else
		pr_info("set icl to %d mA\n", icl_ma);

	return rc;
}

static int smbchg_wls_set_fcc(struct oplus_chg_ic_dev *ic_dev, int fcc_ma)
{
	int rc = 0;
	int prop_id = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct ocm_state *pst = NULL;

	if(!probe_done)
		return 0;

	if (ic_dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &bcdev->ocm_list[OCM_TYPE_BATTERY];

	prop_id = BATT_CHG_CTRL_LIM;
	rc = write_property_id(bcdev, pst, prop_id, fcc_ma * 1000);
	if (rc)
		pr_err("set fcc to %d mA fail, rc=%d\n", fcc_ma, rc);
	else
		pr_info("set fcc to %d mA\n", fcc_ma);

	return rc;
}

static int smbchg_wls_set_fv(struct oplus_chg_ic_dev *ic_dev, int fv_mv)
{
	int rc = 0;

	if(!probe_done)
		return 0;

	rc = smbchg_float_voltage_set(fv_mv);
	if (rc < 0)
		pr_err("set wls fv to %u error, rc=%d\n", fv_mv, rc);

	return rc;
}

static int smbchg_wls_rechg_vol(struct oplus_chg_ic_dev *ic_dev, int fv_mv)
{
	int rc = 0;

	if(!probe_done)
		return 0;

	return rc;
}

static int smbchg_wls_get_input_curr(struct oplus_chg_ic_dev *ic_dev, int *curr)
{
	int rc = 0;
	struct ocm_state *pst = NULL;
	struct battery_chg_dev *chip;

	if(!probe_done)
		return 0;

	if (ic_dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &chip->ocm_list[OCM_TYPE_WLS];

	rc = read_property_id(chip, pst, WLS_CURR_NOW);
	if (rc < 0) {
		pr_err("get wls input curr error, rc=%d\n", rc);
		return rc;
	}
	*curr = pst->prop[WLS_CURR_NOW] / 1000;

	return rc;
}

static int smbchg_wls_get_input_vol(struct oplus_chg_ic_dev *ic_dev, int *vol)
{
	int rc = 0;
	struct ocm_state *pst = NULL;
	struct battery_chg_dev *chip;

	if(!probe_done)
		return 0;

	if (ic_dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &chip->ocm_list[OCM_TYPE_WLS];

	rc = read_property_id(chip, pst, WLS_VOLT_NOW);
	if (rc < 0) {
		pr_err("set wls input vol error, rc=%d\n", rc);
		return rc;
	}
	*vol = pst->prop[WLS_VOLT_NOW] / 1000;

	return rc;
}

static int smbchg_wls_set_boost_en(struct oplus_chg_ic_dev *ic_dev, bool en)
{
	int rc = 0;
	struct ocm_state *pst = NULL;
	struct battery_chg_dev *chip;

	if(!probe_done)
		return 0;

	if (ic_dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &chip->ocm_list[OCM_TYPE_WLS];

	if(en && chip->wls_boost_soft_start && chip->wls_set_boost_vol != PM8350B_BOOST_VOL_MIN_MV){
		rc = write_property_id(chip, pst, WLS_BOOST_VOLT, PM8350B_BOOST_VOL_MIN_MV);
		if (rc < 0){
			pr_err("set boost vol to PM8350B_BOOST_VOL_MIN_MV error, rc=%d\n", rc);
			return rc;
		}
	}

	rc = write_property_id(chip, pst, WLS_BOOST_EN, en ? 1 : 0);
	if (rc < 0){
		pr_err("set boost %s error, rc=%d\n", en ? "enable" : "disable", rc);
		return rc;
	}

	if(en && chip->wls_boost_soft_start && chip->wls_set_boost_vol != PM8350B_BOOST_VOL_MIN_MV){
		msleep(2);
		rc = write_property_id(chip, pst, WLS_BOOST_VOLT, chip->wls_set_boost_vol);
		if (rc < 0)
			pr_err("set boost vol to %d mV error, rc=%d\n", chip->wls_set_boost_vol, rc);
	}

	return rc;
}

static int smbchg_wls_set_boost_vol(struct oplus_chg_ic_dev *ic_dev, int vol_mv)
{
	int rc = 0;
	struct ocm_state *pst = NULL;
	struct battery_chg_dev *chip;

	if(!probe_done)
		return 0;

	if (ic_dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &chip->ocm_list[OCM_TYPE_WLS];

	rc = write_property_id(chip, pst, WLS_BOOST_VOLT, vol_mv);
	if (rc < 0)
		pr_err("set boost vol to %d mV error, rc=%d\n", vol_mv, rc);
	else
		chip->wls_set_boost_vol = vol_mv;

	return rc;
}

static int smbchg_wls_set_aicl_enable(struct oplus_chg_ic_dev *ic_dev, bool en)
{
	int rc = 0;
	struct ocm_state *pst = NULL;
	struct battery_chg_dev *chip;

	if(!probe_done)
		return 0;

	if (ic_dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &chip->ocm_list[OCM_TYPE_WLS];

	rc = write_property_id(chip, pst, WLS_BOOST_AICL_ENABLE, en);
	if (rc < 0)
		pr_err("can't %s aicl, rc=%d\n", en ? "enable" : " disable", rc);

	return rc;
}

static int smbchg_wls_rerun_aicl(struct oplus_chg_ic_dev *ic_dev)
{
	int rc = 0;
	struct ocm_state *pst = NULL;
	struct battery_chg_dev *chip;

	if(!probe_done)
		return 0;

	if (ic_dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &chip->ocm_list[OCM_TYPE_WLS];

	rc = write_property_id(chip, pst, WLS_BOOST_AICL_RERUN, 1);
	if (rc < 0)
		pr_err("can't rerun aicl, rc=%d\n", rc);

	return rc;
}

static struct oplus_chg_ic_buck_ops pm8350b_dev_ops = {
	.chg_set_input_enable = smbchg_wls_input_enable,
	.chg_set_output_enable = smbchg_wls_output_enable,
	.chg_set_icl = smbchg_wls_set_icl,
	.chg_set_fcc = smbchg_wls_set_fcc,
	.chg_set_fv = smbchg_wls_set_fv,
	.chg_set_rechg_vol = smbchg_wls_rechg_vol,
	.chg_get_icl = NULL,
	.chg_get_input_curr = smbchg_wls_get_input_curr,
	.chg_get_input_vol = smbchg_wls_get_input_vol,
	.chg_set_boost_en = smbchg_wls_set_boost_en,
	.chg_set_boost_vol = smbchg_wls_set_boost_vol,
	.chg_set_boost_curr_limit =  NULL,
	.chg_set_aicl_enable = smbchg_wls_set_aicl_enable,
	.chg_set_aicl_rerun = smbchg_wls_rerun_aicl,
};

static void oplus_chg_usb_disconnect_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct battery_chg_dev *bcdev = container_of(dwork,
				struct battery_chg_dev, usb_disconnect_work);
	struct ocm_state *pst = NULL;

	pst = &bcdev->ocm_list[OCM_TYPE_USB];
	if (!oplus_chg_is_usb_present() && is_usb_ocm_available(bcdev))
		oplus_chg_mod_event(pst->ocm, pst->ocm, OPLUS_CHG_EVENT_OFFLINE);
}

static int battery_chg_probe(struct platform_device *pdev)
{
	struct oplus_chg_chip *oplus_chip;
	struct battery_chg_dev *bcdev;
	struct device *dev = &pdev->dev;
	struct pmic_glink_client_data client_data = { };
	struct device_node *node = pdev->dev.of_node;
	enum oplus_chg_ic_type ic_type;
	int ic_index;
	int rc, i;

	oplus_chip = devm_kzalloc(&pdev->dev, sizeof(*oplus_chip), GFP_KERNEL);
	if (!oplus_chip) {
		pr_err("oplus_chg_chip devm_kzalloc failed.\n");
		return -ENOMEM;
	}

	oplus_chip->dev = &pdev->dev;
	rc = oplus_chg_parse_swarp_dt(oplus_chip);

	if (oplus_gauge_check_chip_is_null()) {
		chg_err("gauge chip null, will do after bettery init.\n");
		return -EPROBE_DEFER;
	}
	oplus_chip->chg_ops = &battery_chg_ops;

	g_oplus_chip = oplus_chip;

	bcdev = devm_kzalloc(&pdev->dev, sizeof(*bcdev), GFP_KERNEL);
	if (!bcdev)
		return -ENOMEM;

	oplus_chip->pmic_spmi.bcdev_chip = bcdev;

	bcdev->ocm_list[OCM_TYPE_BATTERY].name = "battery";
	bcdev->ocm_list[OCM_TYPE_BATTERY].prop_count = BATT_PROP_MAX;
	bcdev->ocm_list[OCM_TYPE_BATTERY].opcode_get = BC_BATTERY_STATUS_GET;
	bcdev->ocm_list[OCM_TYPE_BATTERY].opcode_set = BC_BATTERY_STATUS_SET;
	bcdev->ocm_list[OCM_TYPE_USB].name = "usb";
	bcdev->ocm_list[OCM_TYPE_USB].prop_count = USB_PROP_MAX;
	bcdev->ocm_list[OCM_TYPE_USB].opcode_get = BC_USB_STATUS_GET;
	bcdev->ocm_list[OCM_TYPE_USB].opcode_set = BC_USB_STATUS_SET;
	bcdev->ocm_list[OCM_TYPE_WLS].name = "wireless";
	bcdev->ocm_list[OCM_TYPE_WLS].prop_count = WLS_PROP_MAX;
	bcdev->ocm_list[OCM_TYPE_WLS].opcode_get = BC_WLS_STATUS_GET;
	bcdev->ocm_list[OCM_TYPE_WLS].opcode_set = BC_WLS_STATUS_SET;

	for (i = 0; i < OCM_TYPE_MAX; i++) {
		bcdev->ocm_list[i].prop =
			devm_kcalloc(&pdev->dev, bcdev->ocm_list[i].prop_count,
					sizeof(u32), GFP_KERNEL);
		if (!bcdev->ocm_list[i].prop)
			return -ENOMEM;
	}

	bcdev->ocm_list[OCM_TYPE_BATTERY].model =
		devm_kzalloc(&pdev->dev, MAX_STR_LEN, GFP_KERNEL);
	if (!bcdev->ocm_list[OCM_TYPE_BATTERY].model)
		return -ENOMEM;

	mutex_init(&bcdev->rw_lock);
	init_completion(&bcdev->ack);
	init_completion(&bcdev->fw_buf_ack);
	init_completion(&bcdev->fw_update_ack);

	INIT_WORK(&bcdev->subsys_up_work, battery_chg_subsys_up_work);
	INIT_WORK(&bcdev->usb_type_work, battery_chg_update_usb_type_work);
	INIT_WORK(&bcdev->otg_enable_work, oplus_otg_enable_work);
	INIT_WORK(&bcdev->otg_disable_work, oplus_otg_disable_work);
	INIT_DELAYED_WORK(&bcdev->otg_init_work, oplus_otg_init_status_func);
	INIT_DELAYED_WORK(&bcdev->otg_status_check_work, oplus_otg_status_check_work);
	INIT_DELAYED_WORK(&bcdev->connector_check_work, oplus_connector_temp_check_work);
	INIT_DELAYED_WORK(&bcdev->connector_recovery_work, oplus_connector_recovery_charge_work);
	INIT_DELAYED_WORK(&bcdev->charge_status_check_work, oplus_charge_status_check_work);
	INIT_DELAYED_WORK(&bcdev->check_charger_type_work, oplus_charger_type_check_work);
	INIT_DELAYED_WORK(&bcdev->usb_disconnect_work, oplus_chg_usb_disconnect_work);
	atomic_set(&bcdev->state, PMIC_GLINK_STATE_UP);
	bcdev->dev = dev;

	client_data.id = MSG_OWNER_BC;
	client_data.name = "battery_charger";
	client_data.msg_cb = battery_chg_callback;
	client_data.priv = bcdev;
	client_data.state_cb = battery_chg_state_cb;

	bcdev->client = pmic_glink_register_client(dev, &client_data);
	if (IS_ERR(bcdev->client)) {
		rc = PTR_ERR(bcdev->client);
		if (rc != -EPROBE_DEFER)
			dev_err(dev, "Error in registering with pmic_glink %d\n",
				rc);
		return rc;
	}

	bcdev->reboot_notifier.notifier_call = battery_chg_ship_mode;
	bcdev->reboot_notifier.priority = 255;
	register_reboot_notifier(&bcdev->reboot_notifier);

	rc = battery_chg_parse_dt(bcdev);
	if (rc < 0)
		goto error;

	platform_set_drvdata(pdev, bcdev);
	bcdev->fake_soc = -EINVAL;

	bcdev->battery_class.name = "qcom-battery";
	bcdev->battery_class.class_groups = battery_class_groups;
	rc = class_register(&bcdev->battery_class);
	if (rc < 0) {
		dev_err(dev, "Failed to create battery_class rc=%d\n", rc);
		goto error;
	}

	//oplus_chg_parse_custom_dt(oplus_chip);
	oplus_chg_parse_charger_dt(oplus_chip);
#if 0
	main_psy = power_supply_get_by_name("main");
	if (main_psy) {
		pval.intval = 1000 * oplus_chg_get_fv(oplus_chip);
		power_supply_set_property(main_psy,
				POWER_SUPPLY_PROP_VOLTAGE_MAX,
				&pval);
		pval.intval = 1000 * oplus_chg_get_charging_current(oplus_chip);
		power_supply_set_property(main_psy,
				POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
				&pval);
	}
#endif
	oplus_chg_wake_update_work();

	battery_chg_add_debugfs(bcdev);
	battery_chg_notify_enable(bcdev);
	device_init_wakeup(bcdev->dev, true);

	rc = of_property_read_u32(node, "oplus,ic_type", &ic_type);
	if (rc < 0) {
		pr_err("can't get ic type, rc=%d\n", rc);
		goto reg_ic_err;
	}
	rc = of_property_read_u32(node, "oplus,ic_index", &ic_index);
	if (rc < 0) {
		pr_err("can't get ic index, rc=%d\n", rc);
		goto reg_ic_err;
	}
	bcdev->ic_dev = devm_oplus_chg_ic_register(dev, node->name, ic_index);
	if (!bcdev->ic_dev) {
		rc = -ENODEV;
		pr_err("register %s error\n", node->name);
		goto reg_ic_err;
	}
	bcdev->ic_dev->dev_ops = &pm8350b_dev_ops;
	bcdev->ic_dev->type = ic_type;

	of_platform_populate(node, NULL, NULL, dev);

	oplus_chg_init(oplus_chip);

	oplus_usbtemp_iio_init(bcdev);
#ifdef OPLUS_CHG_OP_DEF
	oplus_vph_iio_init(bcdev);
#endif
	oplus_vbus_ctrl_gpio_request(bcdev);
	if (gpio_is_valid(bcdev->vbus_ctrl))
		schedule_delayed_work(&bcdev->connector_check_work, msecs_to_jiffies(10000));
	schedule_delayed_work(&bcdev->otg_init_work, 0);
	if (bcdev->ocm_list[OCM_TYPE_BATTERY].ocm)
		oplus_chg_mod_changed(bcdev->ocm_list[OCM_TYPE_BATTERY].ocm);
	probe_done = true;

	if (is_usb_ocm_available(bcdev))
		oplus_chg_global_event(bcdev->ocm_list[OCM_TYPE_USB].ocm, OPLUS_CHG_EVENT_ADSP_STARTED);
	else
		pr_err("usb ocm not fount\n");
#ifdef OPLUS_CHG_OP_DEF
	msm_vreg_ldo_enable(bcdev);
#endif
	pon_by_chg = oplus_chg_is_usb_present();
	pr_err("probe success\n");

	return 0;

reg_ic_err:
	device_init_wakeup(bcdev->dev, false);
	debugfs_remove_recursive(bcdev->debugfs_dir);
error:
	pmic_glink_unregister_client(bcdev->client);
	unregister_reboot_notifier(&bcdev->reboot_notifier);
	probe_done = false;
	pr_err("probe error, rc=%d\n", rc);
	return rc;
}

static int battery_chg_remove(struct platform_device *pdev)
{
	struct battery_chg_dev *bcdev = platform_get_drvdata(pdev);
	int rc;

	probe_done = false;
	devm_oplus_chg_ic_unregister(bcdev->dev, bcdev->ic_dev);
	device_init_wakeup(bcdev->dev, false);
	debugfs_remove_recursive(bcdev->debugfs_dir);
	class_unregister(&bcdev->battery_class);
	unregister_reboot_notifier(&bcdev->reboot_notifier);
	rc = pmic_glink_unregister_client(bcdev->client);
	if (rc < 0) {
		pr_err("Error unregistering from pmic_glink, rc=%d\n", rc);
		return rc;
	}

	return 0;
}

static void smbchg_enter_shipmode(struct oplus_chg_chip *chip)
{
	int rc = 0;
	struct battery_chg_dev *bcdev = NULL;
	struct ocm_state *pst = NULL;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return;
	}
	bcdev = chip->pmic_spmi.bcdev_chip;
	pst = &bcdev->ocm_list[OCM_TYPE_BATTERY];

	rc = write_property_id(bcdev, pst, BATT_SET_SHIP_MODE, 1);
	if (rc) {
		chg_err("set ship mode fail, rc=%d\n", rc);
		return;
	}

	chg_debug("power off after 15s\n");
}

static void battery_chg_shutdown(struct platform_device *pdev)
{
#ifdef OPLUS_CHG_OP_DEF
	smbchg_lcm_en(true);
	pr_err("enable lcm mode\n");
#endif
	if (g_oplus_chip && g_oplus_chip->enable_shipmode) {
		smbchg_enter_shipmode(g_oplus_chip);
		msleep(1000);
	}
}

static const struct of_device_id battery_chg_match_table[] = {
	{ .compatible = "oplus,sm8350" },
	{},
};

static struct platform_driver battery_chg_driver = {
	.driver = {
		.name = "oplus_sm8350",
		.of_match_table = battery_chg_match_table,
		.pm	= &battery_chg_pm_ops,
	},
	.probe = battery_chg_probe,
	.remove = battery_chg_remove,
	.shutdown = battery_chg_shutdown,
};

static __init int battery_chg_driver_init(void)
{
	return platform_driver_register(&battery_chg_driver);
}

static __exit void battery_chg_driver_exit(void)
{
	platform_driver_unregister(&battery_chg_driver);
}

oplus_chg_module_register(battery_chg_driver);
