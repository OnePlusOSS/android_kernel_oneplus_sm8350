#define pr_fmt(fmt) "OPLUS_CHG[INTF]: %s[%d]: " fmt, __func__, __LINE__

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/of_platform.h>
#include <linux/interrupt.h>
#ifdef CONFIG_OPLUS_CHG_OOS
#include <linux/oem/oplus_chg.h>
#else
#include <linux/oplus_chg.h>
#endif
#include <linux/power_supply.h>
#include "oplus_chg_module.h"
#include "oplus_charger.h"
#include "oplus_gauge.h"
#include "oplus_warp.h"
#include "oplus_short.h"
#include "oplus_adapter.h"
#ifdef OPLUS_CHG_OP_DEF
#include "oplus_op_def.h"
#endif
#include "charger_ic/oplus_short_ic.h"
#include <linux/usb/dwc3-msm.h>

enum capacity_level {
	CAPACITY_LEVEL_UNKNOWN,
	CAPACITY_LEVEL_CRITICAL,
	CAPACITY_LEVEL_LOW,
	CAPACITY_LEVEL_NORMAL,
	CAPACITY_LEVEL_HIGH,
	CAPACITY_LEVEL_FULL,
};

struct oplus_chg_device {
	struct device *dev;
	struct oplus_chg_mod *usb_ocm;
	struct notifier_block usb_changed_nb;
	struct notifier_block usb_event_nb;
	struct notifier_block usb_mod_nb;
	struct oplus_chg_mod *batt_ocm;
	struct oplus_chg_mod *wls_ocm;
	struct oplus_chg_mod *comm_ocm;
	struct notifier_block batt_changed_nb;
	struct notifier_block batt_event_nb;
	struct notifier_block batt_mod_nb;
	struct power_supply *usb_psy;
	struct power_supply *batt_psy;
	struct power_supply *wls_psy;
	struct oplus_chg_ic_dev *typec_ic;
	struct work_struct otg_enable_work;
	struct work_struct otg_disable_work;
	struct delayed_work batt_update_work;
	struct delayed_work connect_check_work;
	struct delayed_work charger_status_check_work;
#ifdef OPLUS_CHG_REG_DUMP_ENABLE
	struct work_struct reg_dump_work;
#endif
#ifndef CONFIG_OPLUS_CHG_OOS
	struct notifier_block ac_changed_nb;
	struct notifier_block ac_event_nb;
	struct notifier_block ac_mod_nb;
	struct oplus_chg_mod *ac_ocm;
#endif /* CONFIG_OPLUS_CHG_OOS */

	atomic_t usb_present;
	atomic_t usb_online;
	atomic_t chg_done;

	bool temp_debug_mode;
	int debug_temp;
	bool soc_debug_mode;
	int debug_soc;
	bool usb_chg_disable;
	bool icon_debounce;
	int batt_update_count;
	unsigned int sid_backup;
	int notify_code;
};

static ATOMIC_NOTIFIER_HEAD(usb_ocm_notifier);
static ATOMIC_NOTIFIER_HEAD(batt_ocm_notifier);
#ifndef CONFIG_OPLUS_CHG_OOS
static ATOMIC_NOTIFIER_HEAD(ac_ocm_notifier);
#endif /* CONFIG_OPLUS_CHG_OOS */

__maybe_unused static bool is_usb_psy_available(struct oplus_chg_device *dev)
{
	if (!dev->usb_psy)
		dev->usb_psy = power_supply_get_by_name("usb");
	return !!dev->usb_psy;
}

__maybe_unused static bool is_wls_psy_available(struct oplus_chg_device *dev)
{
	if (!dev->wls_psy)
		dev->wls_psy = power_supply_get_by_name("wireless");
	return !!dev->wls_psy;
}

__maybe_unused static bool is_batt_psy_available(struct oplus_chg_device *dev)
{
	if (!dev->batt_psy)
		dev->batt_psy = power_supply_get_by_name("battery");
	return !!dev->batt_psy;
}

static bool is_wls_ocm_available(struct oplus_chg_device *dev)
{
	if (!dev->wls_ocm)
		dev->wls_ocm = oplus_chg_mod_get_by_name("wireless");
	return !!dev->wls_ocm;
}

__maybe_unused static bool is_usb_ocm_available(struct oplus_chg_device *dev)
{
	if (!dev->usb_ocm)
		dev->usb_ocm = oplus_chg_mod_get_by_name("usb");
	return !!dev->usb_ocm;
}

__maybe_unused static bool is_comm_ocm_available(struct oplus_chg_device *dev)
{
	if (!dev->comm_ocm)
		dev->comm_ocm = oplus_chg_mod_get_by_name("common");
	return !!dev->comm_ocm;
}

static int oplus_chg_typec_cc_orientation(struct oplus_chg_device *dev, int *orientation)
{
	struct oplus_chg_ic_dev *typec_ic;
	struct oplus_chg_ic_typec_ops *typec_ic_ops;
	int rc;

	if (dev == NULL) {
		pr_err("oplus_chg_device is NULL\n");
		return -ENODEV;
	}

	typec_ic = of_get_oplus_chg_ic(dev->dev->of_node, "oplus,typec_ic");
	if (typec_ic == NULL) {
		pr_err("typec_ic not found!\n");
		*orientation = 0;
		return -ENODEV;
	}

	typec_ic_ops = typec_ic->dev_ops;
	rc = typec_ic_ops->typec_get_cc_orientation(typec_ic, orientation);

	return rc;
}

#ifndef CONFIG_OPLUS_CHG_OOS
#define DISCONNECT			0
#define STANDARD_TYPEC_DEV_CONNECT	BIT(0)
#define OTG_DEV_CONNECT			BIT(1)
static int oplus_get_otg_online_status(struct oplus_chg_device *dev, int *otg_online)
{
	struct oplus_chg_ic_dev *typec_ic;
	struct oplus_chg_ic_typec_ops *typec_ic_ops;
	struct oplus_chg_chip *chip = oplus_chg_get_chg_struct();
	int rc;
	int hw_detect = 0;
	int online = 0;

	if ((dev == NULL) || (chip == NULL)) {
		pr_err("oplus_chg_device or oplus_chg_chip is NULL\n");
		return -ENODEV;
	}

	typec_ic = of_get_oplus_chg_ic(dev->dev->of_node, "oplus,typec_ic");
	if (typec_ic == NULL) {
		pr_err("typec_ic not found!\n");
		return -ENODEV;
	}

	typec_ic_ops = typec_ic->dev_ops;
	rc = typec_ic_ops->typec_get_hw_detect(typec_ic, &hw_detect);

	online = (hw_detect == 1) ? STANDARD_TYPEC_DEV_CONNECT : DISCONNECT;
	online = online | ((chip->otg_online) ? OTG_DEV_CONNECT : DISCONNECT);

	if (online) {
		*otg_online = online;
	} else {
		*otg_online = 0;
	}
	pr_debug("otg_online value is: %d\n", *otg_online);

	return rc;
}
#endif

static int oplus_chg_hw_detect(struct oplus_chg_device *dev, int *hw_detect)
{
	struct oplus_chg_ic_dev *typec_ic;
	struct oplus_chg_ic_typec_ops *typec_ic_ops;
	int rc;

	if (dev == NULL) {
		pr_err("oplus_chg_device is NULL\n");
		return -ENODEV;
	}

	typec_ic = of_get_oplus_chg_ic(dev->dev->of_node, "oplus,typec_ic");
	if (typec_ic == NULL) {
		pr_err("typec_ic not found!\n");
		*hw_detect = 0;
		return -ENODEV;
	}

	typec_ic_ops = typec_ic->dev_ops;
	rc = typec_ic_ops->typec_get_hw_detect(typec_ic, hw_detect);

	return rc;
}

static void oplus_otg_enable_work(struct work_struct *work)
{
	struct oplus_chg_device *dev = container_of(work,
				struct oplus_chg_device, otg_enable_work);

	struct oplus_chg_ic_dev *typec_ic;
	struct oplus_chg_ic_typec_ops *typec_ic_ops;
	int rc;

	if (dev == NULL) {
		pr_err("oplus_chg_device is NULL\n");
		return;
	}

	typec_ic = of_get_oplus_chg_ic(dev->dev->of_node, "oplus,typec_ic");
	if (typec_ic == NULL) {
		pr_err("typec_ic not found!\n");
		return;
	}

	typec_ic_ops = typec_ic->dev_ops;
	rc = typec_ic_ops->otg_enable(typec_ic, true);
}

static void oplus_otg_disable_work(struct work_struct *work)
{
	struct oplus_chg_device *dev = container_of(work,
				struct oplus_chg_device, otg_disable_work);

	struct oplus_chg_ic_dev *typec_ic;
	struct oplus_chg_ic_typec_ops *typec_ic_ops;
	int rc;

	if (dev == NULL) {
		pr_err("oplus_chg_device is NULL\n");
		return;
	}

	typec_ic = of_get_oplus_chg_ic(dev->dev->of_node, "oplus,typec_ic");
	if (typec_ic == NULL) {
		pr_err("typec_ic not found!\n");
		return;
	}

	typec_ic_ops = typec_ic->dev_ops;
	rc = typec_ic_ops->otg_enable(typec_ic, false);
}

#ifdef OPLUS_CHG_REG_DUMP_ENABLE
static void oplus_chg_reg_dump_work(struct work_struct *work)
{
	struct oplus_chg_device *dev = container_of(work,
				struct oplus_chg_device, reg_dump_work);

	struct oplus_chg_ic_dev *typec_ic;

	if (dev == NULL) {
		pr_err("oplus_chg_device is NULL\n");
		return;
	}

	typec_ic = of_get_oplus_chg_ic(dev->dev->of_node, "oplus,typec_ic");
	if (typec_ic == NULL) {
		pr_err("typec_ic not found!\n");
		return;
	}

	(void)oplus_chg_ic_reg_dump(typec_ic);
}
#endif

static void oplus_chg_batt_update_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_device *dev = container_of(dwork,
				struct oplus_chg_device, batt_update_work);

	if (!atomic_read(&dev->usb_present)) {
		dev->batt_update_count = 0;
		return;
	}

	if (dev->batt_update_count < 30) {
		dev->batt_update_count++;
		if (dev->batt_ocm)
			oplus_chg_mod_changed(dev->batt_ocm);
		schedule_delayed_work(&dev->batt_update_work, msecs_to_jiffies(500));
		return;
	}

	dev->batt_update_count = 0;
}

static void oplus_chg_connect_check_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_device *dev = container_of(dwork,
				struct oplus_chg_device, connect_check_work);
	struct oplus_chg_chip *chip = oplus_chg_get_chg_struct();

	if (atomic_read(&dev->usb_online) == 0) {
		dev->sid_backup = 0;
		pr_info("sid_backup clean\n");
		if (chip != NULL) {
			chip->reconnect_count = 0;
			chip->norchg_reconnect_count = 0;
		}
		oplus_warp_reset_fastchg_after_usbout();
	}
	dev->icon_debounce = false;
	if (dev->usb_ocm)
		oplus_chg_mod_changed(dev->usb_ocm);
	pr_info("icon_debounce: false");
}

static void oplus_chg_charger_status_check_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_device *dev = container_of(dwork,
				struct oplus_chg_device, charger_status_check_work);
	struct oplus_chg_chip *chip = oplus_chg_get_chg_struct();

	if (chip == NULL) {
		pr_err("chip is NULL\n");
		return;
	}
	if (!chip->chg_ops->check_chrdet_status()) {
		if (is_usb_ocm_available(dev))
			oplus_chg_mod_event(dev->usb_ocm, dev->usb_ocm, OPLUS_CHG_EVENT_OFFLINE);
		else
			pr_err("usb_ocm not found\n");
		pr_info("send usb offline event\n");
	}
}

#ifdef OPLUS_CHG_DEBUG
#define UPGRADE_START 0
#define UPGRADE_FW    1
#define UPGRADE_END   2
struct oplus_chg_fw_head {
	u8 magic[4];
	int size;
};
static ssize_t oplus_chg_intf_usb_upgrade_fw_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	int ret;

	ret = sprintf(buf, "usb\n");
	return ret;
}

static ssize_t oplus_chg_intf_usb_upgrade_fw_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	u8 temp_buf[sizeof(struct oplus_chg_fw_head)];
	static u8 *fw_buf;
	static int upgrade_step = UPGRADE_START;
	static int fw_index;
	static int fw_size;
	struct oplus_chg_fw_head *fw_head;
	int rc;

start:
	switch (upgrade_step) {
	case UPGRADE_START:
		if (count < sizeof(struct oplus_chg_fw_head)) {
			pr_err("<FW UPDATE>image format error\n");
			return -EINVAL;
		}
		memset(temp_buf, 0, sizeof(struct oplus_chg_fw_head));
		memcpy(temp_buf, buf, sizeof(struct oplus_chg_fw_head));
		fw_head = (struct oplus_chg_fw_head *)temp_buf;
		if (fw_head->magic[0] == 0x02 && fw_head->magic[1] == 0x00 &&
		    fw_head->magic[2] == 0x03 && fw_head->magic[3] == 0x00) {
			fw_size = fw_head->size;
			fw_buf = kzalloc(fw_size, GFP_KERNEL);
			if (fw_buf == NULL) {
				pr_err("<FW UPDATE>alloc fw_buf err\n");
				return -ENOMEM;
			}
			pr_err("<FW UPDATE>image header verification succeeded, fw_size=%d\n", fw_size);
			memcpy(fw_buf, buf + sizeof(struct oplus_chg_fw_head), count - sizeof(struct oplus_chg_fw_head));
			fw_index = count - sizeof(struct oplus_chg_fw_head);
			pr_info("<FW UPDATE>Receiving image, fw_size=%d, fw_index=%d\n", fw_size, fw_index);
			if (fw_index >= fw_size) {
				upgrade_step = UPGRADE_END;
				goto start;
			} else {
				upgrade_step = UPGRADE_FW;
			}
		} else {
			pr_err("<FW UPDATE>image format error\n");
			return -EINVAL;
		}
		break;
	case UPGRADE_FW:
		memcpy(fw_buf + fw_index, buf, count);
		fw_index += count;
		pr_info("<FW UPDATE>Receiving image, fw_size=%d, fw_index=%d\n", fw_size, fw_index);
		if (fw_index >= fw_size) {
			upgrade_step = UPGRADE_END;
			goto start;
		}
		break;
	case UPGRADE_END:
		rc = oplus_warp_user_fw_upgrade(fw_buf, fw_size);
		kfree(fw_buf);
		fw_buf = NULL;
		upgrade_step = UPGRADE_START;
		if (rc < 0) {
			pr_err("<FW UPDATE>fw upgrade err, rc=%d\n", rc);
			return rc;
		}
		break;
	default:
		upgrade_step = UPGRADE_START;
		pr_err("<FW UPDATE>status error\n");
		if (fw_buf != NULL) {
			kfree(fw_buf);
			fw_buf = NULL;
		}
		break;
	}

	return count;
}
#endif /* OPLUS_CHG_DEBUG */

static enum oplus_chg_mod_property oplus_chg_intf_usb_props[] = {
	OPLUS_CHG_PROP_ONLINE,
	OPLUS_CHG_PROP_PRESENT,
	OPLUS_CHG_PROP_VOLTAGE_NOW,
	OPLUS_CHG_PROP_VOLTAGE_MAX,
	OPLUS_CHG_PROP_VOLTAGE_MIN,
	OPLUS_CHG_PROP_CURRENT_NOW,
	OPLUS_CHG_PROP_CURRENT_MAX,
	OPLUS_CHG_PROP_USB_TYPE,
	OPLUS_CHG_PROP_FASTCHG_STATUS,
	OPLUS_CHG_PROP_ADAPTER_SID,
	OPLUS_CHG_PROP_CON_TEMP1,
	OPLUS_CHG_PROP_CON_TEMP2,
	OPLUS_CHG_PROP_CHG_ENABLE,
	OPLUS_CHG_PROP_OTG_MODE,
	OPLUS_CHG_PROP_TYPEC_CC_ORIENTATION,
	OPLUS_CHG_PROP_HW_DETECT,
	OPLUS_CHG_PROP_CONNECT_DISABLE,
	OPLUS_CHG_PROP_OTG_SWITCH,
#ifdef OPLUS_CHG_REG_DUMP_ENABLE
	OPLUS_CHG_PROP_REG_DUMP,
#endif
#ifndef CONFIG_OPLUS_CHG_OOS
	OPLUS_CHG_PROP_OTG_ONLINE,
	OPLUS_CHG_PROP_USB_STATUS,
	OPLUS_CHG_PROP_FAST_CHG_TYPE,
	OPLUS_CHG_PROP_USBTEMP_VOLT_L,
	OPLUS_CHG_PROP_USBTEMP_VOLT_R,
#endif /* CONFIG_OPLUS_CHG_OOS */
};

static enum oplus_chg_mod_property oplus_chg_intf_usb_uevent_props[] = {
	OPLUS_CHG_PROP_ONLINE,
	OPLUS_CHG_PROP_PRESENT,
	OPLUS_CHG_PROP_VOLTAGE_NOW,
	OPLUS_CHG_PROP_CURRENT_NOW,
	OPLUS_CHG_PROP_USB_TYPE,
	OPLUS_CHG_PROP_FASTCHG_STATUS,
	OPLUS_CHG_PROP_ADAPTER_SID,
	OPLUS_CHG_PROP_CHG_ENABLE,
	OPLUS_CHG_PROP_OTG_MODE,
};

#ifdef OPLUS_CHG_DEBUG
static struct oplus_chg_exten_prop oplus_chg_intf_usb_exten_props[] = {
	OPLUS_CHG_EXTEN_RWATTR(OPLUS_CHG_EXTERN_PROP_UPGRADE_FW, oplus_chg_intf_usb_upgrade_fw),
};
#endif

__maybe_unused static const enum oplus_chg_usb_type oplus_type_to_oplus_chg_usb_type[] = {
	OPLUS_CHG_USB_TYPE_UNKNOWN,
	OPLUS_CHG_USB_TYPE_UNKNOWN,
	OPLUS_CHG_USB_TYPE_UNKNOWN,
	OPLUS_CHG_USB_TYPE_UNKNOWN,
	OPLUS_CHG_USB_TYPE_SDP,
	OPLUS_CHG_USB_TYPE_DCP,
	OPLUS_CHG_USB_TYPE_CDP,
	OPLUS_CHG_USB_TYPE_ACA,
	OPLUS_CHG_USB_TYPE_C,
	OPLUS_CHG_USB_TYPE_PD,
	OPLUS_CHG_USB_TYPE_PD_DRP,
	OPLUS_CHG_USB_TYPE_APPLE_BRICK_ID,
};
static int oplus_chg_intf_usb_get_prop(struct oplus_chg_mod *ocm,
			enum oplus_chg_mod_property prop,
			union oplus_chg_mod_propval *pval)
{
	struct oplus_chg_device *oplus_dev = oplus_chg_mod_get_drvdata(ocm);
	struct oplus_chg_chip *chip = oplus_chg_get_chg_struct();
#ifndef CONFIG_OPLUS_CHG_OOS
	union oplus_chg_mod_propval temp_val;
#endif
	unsigned int sid;
	int curr_ua, ibus_ma;
	int rc = 0;
	int boot_mode = get_boot_mode();

	if (!chip) {
		// pr_err("oplus chip is NULL\n");
		return -ENODEV;
	}

	switch (prop) {
	case OPLUS_CHG_PROP_ONLINE:
#ifdef OPLUS_CHG_OP_DEF
		if (oplus_warp_ignore_event() == true) {
			pval->intval = true;
			break;
		}
#endif
		if (chip->usb_chg_disable) {
			pval->intval = false;
		} else {
			pval->intval = atomic_read(&oplus_dev->usb_online) &&
				(chip->charger_exist_delay ? 1 : chip->charger_exist);
			if (!chip->mmi_chg && (chip->charger_volt > CHG_VUSBIN_VOL_THR))
				pval->intval = true;
			if (oplus_dev->icon_debounce)
				pval->intval = true;
			if (boot_mode == MSM_BOOT_MODE__CHARGE && chip->disconnect_vbus == true) {
				pr_info("port protect triggered in power-off mode!");
				pval->intval = true;
			}
		}
		break;
	case OPLUS_CHG_PROP_PRESENT:
		if (chip->usb_chg_disable) {
			pval->intval = false;
		} else {
			/*
			 * The usb present signal is prone to glitches, here we no longer
			 * use usb present, use usb present instead.
			 */
			pval->intval = atomic_read(&oplus_dev->usb_online) &&
				(chip->charger_exist_delay ? 1 : chip->charger_exist);
			if (!chip->mmi_chg && (chip->charger_volt > CHG_VUSBIN_VOL_THR))
				pval->intval = true;
			if (oplus_dev->icon_debounce)
				pval->intval = true;
		}
		break;
	case OPLUS_CHG_PROP_VOLTAGE_NOW:
		pval->intval = chip->charger_volt * 1000;
		if (boot_mode == MSM_BOOT_MODE__CHARGE && chip->disconnect_vbus == true) {
			pr_info("port protect triggered in power-off mode!");
			pval->intval = 5000000;
		}
#ifdef OPLUS_CHG_OP_DEF
		if (chip->abnormal_volt_detected && (boot_mode == MSM_BOOT_MODE__CHARGE))
			pval->intval = 0;
#endif
		break;
	case OPLUS_CHG_PROP_VOLTAGE_MAX:
		pval->intval = 9000000;
		break;
	case OPLUS_CHG_PROP_VOLTAGE_MIN:
		pval->intval = 5000000;
		break;
	case OPLUS_CHG_PROP_CURRENT_NOW:
		pval->intval = chip->ibus * 1000;
		break;
	case OPLUS_CHG_PROP_CURRENT_MAX:
		rc = chip->chg_ops->get_input_current_max(&curr_ua);
		if (rc < 0) {
			rc = 0;
			curr_ua = 0;
		}
		pval->intval = curr_ua;
		break;
	case OPLUS_CHG_PROP_USB_TYPE:
#if 0 /* Do not use oplus's original charger type temporarily */
		if (chip->charger_type < sizeof(oplus_type_to_oplus_chg_usb_type))
			pval->intval = oplus_type_to_oplus_chg_usb_type[chip->charger_type];
		else
			pval->intval = OPLUS_CHG_USB_TYPE_UNKNOWN;
		if ((pval->intval == OPLUS_CHG_USB_TYPE_DCP) &&
		    (chip->chg_ops->get_charger_subtype() == CHARGER_SUBTYPE_QC)) {
			pval->intval = OPLUS_CHG_USB_TYPE_QC2;
		}
#endif

#ifdef OPLUS_CHG_OP_DEF
		if (oplus_warp_ignore_event() == true) {
			pval->intval = OPLUS_CHG_USB_TYPE_SWARP;
			break;
		}
		if (chip->is_oplus_svid && chip->charger_exist) {
			pval->intval = OPLUS_CHG_USB_TYPE_SWARP;
		} else
#endif
		if (oplus_warp_get_fast_chg_type() == CHARGER_SUBTYPE_FASTCHG_WARP) {
			pval->intval = OPLUS_CHG_USB_TYPE_WARP;
		} else if (oplus_warp_get_fast_chg_type() == CHARGER_SUBTYPE_FASTCHG_SWARP) {
			pval->intval = OPLUS_CHG_USB_TYPE_SWARP;
		} else {
			sid = oplus_warp_get_adapter_sid();
			if (sid == 0) {
				if (atomic_read(&oplus_dev->usb_online) == 1 || oplus_dev->icon_debounce)
					sid = oplus_dev->sid_backup;
				else
					oplus_dev->sid_backup = 0;
			} else {
				oplus_dev->sid_backup = sid;
			}
			if (sid == 0) {
				pval->intval = chip->oplus_usb_type;
			} else {
				switch (sid_to_adapter_chg_type(sid))
				{
				case CHARGER_TYPE_UNKNOWN:
				case CHARGER_TYPE_NORMAL:
				case CHARGER_TYPE_WARP:
					pval->intval = OPLUS_CHG_USB_TYPE_WARP;
					break;
				case CHARGER_TYPE_SWARP:
					pval->intval = OPLUS_CHG_USB_TYPE_SWARP;
					break;
				}
			}
		}
		break;
	case OPLUS_CHG_PROP_FASTCHG_STATUS:
		pval->intval = oplus_warp_get_fastchg_started();
		break;
	case OPLUS_CHG_PROP_ADAPTER_SID:
		pval->intval = oplus_warp_get_adapter_sid();
		break;
	case OPLUS_CHG_PROP_CON_TEMP1:
		pval->intval = chip->usb_temp_l*10;
		break;
	case OPLUS_CHG_PROP_CON_TEMP2:
		pval->intval = chip->usb_temp_r*10;
		break;
	case OPLUS_CHG_PROP_CHG_ENABLE:
		if (chip->chg_ops && chip->chg_ops->get_charger_current) {
			ibus_ma = chip->chg_ops->get_charger_current();
		} else {
			ibus_ma = 0;
		}
		pval->intval = !(ibus_ma < 10);
		if (chip->usb_chg_disable == !!pval->intval) {
			printk_ratelimited(KERN_ERR "OPLUS_CHG[INTF]: %s[%d]: usb_chg_disable status does not match ibus, usb_chg_disable=%d, ibus=%d\n",
				__func__, __LINE__, chip->usb_chg_disable, ibus_ma);
		}
		break;
	case OPLUS_CHG_PROP_OTG_MODE:
		pval->intval = chip->otg_online;
		break;
	case OPLUS_CHG_PROP_TYPEC_CC_ORIENTATION:
		rc = oplus_chg_typec_cc_orientation(oplus_dev, &pval->intval);
		break;
	case OPLUS_CHG_PROP_HW_DETECT:
		rc = oplus_chg_hw_detect(oplus_dev, &pval->intval);
		break;
	case OPLUS_CHG_PROP_CONNECT_DISABLE:
		pval->intval = chip->disconnect_vbus;
		break;
	case OPLUS_CHG_PROP_OTG_SWITCH:
		pval->intval = chip->otg_switch;
		break;
#ifdef OPLUS_CHG_REG_DUMP_ENABLE
	case OPLUS_CHG_PROP_REG_DUMP:
		if (oplus_dev->usb_ocm)
			oplus_chg_anon_mod_event(oplus_dev->usb_ocm, OPLUS_CHG_EVENT_REG_DUMP);
		break;
#endif
#ifndef CONFIG_OPLUS_CHG_OOS
	case OPLUS_CHG_PROP_OTG_ONLINE:
		rc = oplus_get_otg_online_status(oplus_dev, &pval->intval);
		break;
	case OPLUS_CHG_PROP_USB_STATUS:
		pval->intval = chip->usb_status;
		break;
	case OPLUS_CHG_PROP_FAST_CHG_TYPE:
		pval->intval = oplus_warp_get_fast_chg_type();
		if (pval->intval == 0)
			pval->intval = chip->chg_ops->get_charger_subtype();
#ifdef OPLUS_CHG_OP_DEF
		if (chip->is_oplus_svid) {
			pval->intval = oplus_warp_get_fast_chg_type();
		} else if (!chip->svid_verified && (pval->intval == CHARGER_SUBTYPE_PD)) {
			pval->intval = CHARGER_SUBTYPE_DEFAULT;
		}
		if (oplus_dev->sid_backup != 0)
			pval->intval = oplus_warp_convert_fast_chg_type(sid_to_adapter_id(oplus_dev->sid_backup));
		if (is_wls_ocm_available(oplus_dev) && (pval->intval == 0)) {
			rc = oplus_chg_mod_get_property(oplus_dev->wls_ocm, OPLUS_CHG_PROP_WLS_TYPE, &temp_val);
			if (rc == 0) {
				if ((temp_val.intval == OPLUS_CHG_WLS_WARP) ||
				    (temp_val.intval == OPLUS_CHG_WLS_SWARP) ||
				    (temp_val.intval == OPLUS_CHG_WLS_PD_65W)) {
					rc = oplus_chg_mod_get_property(oplus_dev->wls_ocm, OPLUS_CHG_PROP_ADAPTER_TYPE, &temp_val);
					if (rc == 0){
						if(temp_val.intval == WLS_ADAPTER_TYPE_PD_65W)
							temp_val.intval = WLS_ADAPTER_TYPE_SWARP;
						pval->intval = temp_val.intval;
					}
				}
			}
		}
#else
		if (pval->intval == 0) {
			if (oplus_wpc_get_adapter_type() == CHARGER_SUBTYPE_FASTCHG_WARP
				|| oplus_wpc_get_adapter_type() == CHARGER_SUBTYPE_FASTCHG_SWARP)
				pval->intval = oplus_wpc_get_adapter_type();
		}
#endif
		break;
	case OPLUS_CHG_PROP_USBTEMP_VOLT_L:
		pval->intval = chip->usbtemp_volt_l;
		break;
	case OPLUS_CHG_PROP_USBTEMP_VOLT_R:
		pval->intval = chip->usbtemp_volt_r;
		break;
#endif /* CONFIG_OPLUS_CHG_OOS */
	default:
		pr_err("get prop %d is not supported\n", prop);
		return -EINVAL;
	}
	if (rc < 0) {
		pr_err("Couldn't get prop %d rc = %d\n", prop, rc);
		return -ENODATA;
	}
	return 0;
}

static int oplus_chg_intf_usb_set_prop(struct oplus_chg_mod *ocm,
			enum oplus_chg_mod_property prop,
			const union oplus_chg_mod_propval *pval)
{
	struct oplus_chg_chip *chip = oplus_chg_get_chg_struct();
	struct oplus_chg_device *oplus_dev = oplus_chg_mod_get_drvdata(ocm);
	union oplus_chg_mod_propval temp_val;
	bool usb_chg_enable = false;
	int rc = 0;

	if (!chip) {
		// pr_err("oplus chip is NULL\n");
		return -ENODEV;
	}

	switch (prop) {
	case OPLUS_CHG_PROP_VOLTAGE_MAX:
		break;
	case OPLUS_CHG_PROP_VOLTAGE_MIN:
		break;
	case OPLUS_CHG_PROP_CURRENT_MAX:
		rc = chip->chg_ops->input_current_write(pval->intval / 1000);
		break;
	case OPLUS_CHG_PROP_CHG_ENABLE:
		if (!pval->intval) {
			if(chip->unwakelock_chg == 1) {
				rc = -EINVAL;
				pr_err("unwakelock testing , this test not allowed.\n");
			} else {
				pr_info("disable charge\n");
				chip->chg_ops->debug_disable_chg(true);
				chip->mmi_chg = 0;
				oplus_chg_turn_off_charging(chip);
				if (oplus_warp_get_fastchg_started() == true) {
					oplus_warp_turn_off_fastchg();
					chip->mmi_fastchg = 0;
				}
				chip->chg_ops->charging_disable();
				chip->usb_chg_disable = true;
			}
		} else {
			if(chip->unwakelock_chg == 1) {
				rc = -EINVAL;
				pr_err("unwakelock testing , this test not allowed.\n");
			} else {
				if (oplus_dev->usb_ocm) {
					rc = oplus_chg_mod_get_property(oplus_dev->usb_ocm,
									OPLUS_CHG_PROP_CHG_ENABLE, &temp_val);
					if (rc == 0)
						usb_chg_enable = temp_val.intval;
					rc = 0;
				}
				if (usb_chg_enable) {
					pr_debug("usb charge is enabled\n");
				} else {
					pr_info("enable charge\n");
					chip->chg_ops->debug_disable_chg(false);
					chip->mmi_chg = 1;
					if (chip->mmi_fastchg == 0) {
						oplus_chg_clear_chargerid_info();
					}
					chip->mmi_fastchg = 1;
					oplus_chg_turn_on_charging(chip);
					chip->usb_chg_disable = false;
				}
			}
		}
		break;
	case OPLUS_CHG_PROP_CONNECT_DISABLE:
		chip->chg_ops->disconnect_vbus((bool)pval->intval);
		pr_err("%s Vbus!\n", (bool)pval->intval ? "Disconnect" : "Connect");
		break;
	case OPLUS_CHG_PROP_OTG_SWITCH:
		chip->otg_switch = (bool)pval->intval && chip->chg_ops->otg_set_switch((bool)pval->intval);
		pr_err("%s set otg switch!\n", chip->otg_switch ? "Enable" : "Disable");
		break;
#ifndef CONFIG_OPLUS_CHG_OOS
	case OPLUS_CHG_PROP_USB_STATUS:
		chip->usb_status = pval->intval;
		break;
#endif
	default:
		pr_err("set prop %d is not supported\n", prop);
		rc = -EINVAL;
		break;
	}

	return rc;
}

static int oplus_chg_intf_usb_prop_is_writeable(struct oplus_chg_mod *ocm,
				enum oplus_chg_mod_property prop)
{
	switch (prop) {
	case OPLUS_CHG_PROP_VOLTAGE_MAX:
	case OPLUS_CHG_PROP_VOLTAGE_MIN:
	case OPLUS_CHG_PROP_CURRENT_MAX:
	case OPLUS_CHG_PROP_CHG_ENABLE:
#ifdef OPLUS_CHG_DEBUG
	case OPLUS_CHG_EXTERN_PROP_UPGRADE_FW:
#endif
	case OPLUS_CHG_PROP_CONNECT_DISABLE:
	case OPLUS_CHG_PROP_OTG_SWITCH:
#ifndef CONFIG_OPLUS_CHG_OOS
	case OPLUS_CHG_PROP_USB_STATUS:
#endif
		return 1;
	default:
		break;
	}

	return 0;
}

static const struct oplus_chg_mod_desc oplus_chg_intf_usb_mod_desc = {
	.name = "usb",
	.type = OPLUS_CHG_MOD_USB,
	.properties = oplus_chg_intf_usb_props,
	.num_properties = ARRAY_SIZE(oplus_chg_intf_usb_props),
	.uevent_properties = oplus_chg_intf_usb_uevent_props,
	.uevent_num_properties = ARRAY_SIZE(oplus_chg_intf_usb_uevent_props),
#ifdef OPLUS_CHG_DEBUG
	.exten_properties = oplus_chg_intf_usb_exten_props,
	.num_exten_properties = ARRAY_SIZE(oplus_chg_intf_usb_exten_props),
#else
	.exten_properties = NULL,
	.num_exten_properties = 0,
#endif
	.get_property = oplus_chg_intf_usb_get_prop,
	.set_property = oplus_chg_intf_usb_set_prop,
	.property_is_writeable	= oplus_chg_intf_usb_prop_is_writeable,
};

static int oplus_chg_intf_usb_event_notifier_call(struct notifier_block *nb,
		unsigned long val, void *v)
{
	struct oplus_chg_device *op_dev = container_of(nb, struct oplus_chg_device, usb_event_nb);
	struct oplus_chg_mod *owner_ocm = v;
	struct oplus_chg_chip *chip = oplus_chg_get_chg_struct();
	int boot_mode = get_boot_mode();

	switch(val) {
	case OPLUS_CHG_EVENT_ONLINE:
		if (owner_ocm == NULL) {
			pr_err("This event(=%d) does not support anonymous sending\n",
				val);
			return NOTIFY_BAD;
		}
		if (!strcmp(owner_ocm->desc->name, "usb")) {
			pr_info("usb online\n");
		if (is_usb_psy_available(op_dev))
			power_supply_changed(op_dev->usb_psy);
		if (is_batt_psy_available(op_dev))
			power_supply_changed(op_dev->batt_psy);
		if (is_wls_psy_available(op_dev))
			power_supply_changed(op_dev->wls_psy);
		}
		if (!strcmp(owner_ocm->desc->name, "wireless")) {
			pr_info("wls online\n");
		}
		break;
	case OPLUS_CHG_EVENT_OFFLINE:
		if (owner_ocm == NULL) {
			pr_err("This event(=%d) does not support anonymous sending\n",
				val);
			return NOTIFY_BAD;
		}
		if (!strcmp(owner_ocm->desc->name, "usb")) {
			pr_info("usb offline\n");
			if (is_usb_psy_available(op_dev))
				power_supply_changed(op_dev->usb_psy);
			if (is_batt_psy_available(op_dev))
				power_supply_changed(op_dev->batt_psy);
			if (is_wls_psy_available(op_dev))
				power_supply_changed(op_dev->wls_psy);
		}
#ifdef OPLUS_CHG_OP_DEF
		if (chip && chip->check_abnormal_voltage_work.work.func)
			schedule_delayed_work(&chip->check_abnormal_voltage_work, msecs_to_jiffies(200));
#endif
		break;
	case OPLUS_CHG_EVENT_PRESENT:
		if (owner_ocm == NULL) {
			pr_err("This event(=%d) does not support anonymous sending\n",
				val);
			return NOTIFY_BAD;
		}
		if (!strcmp(owner_ocm->desc->name, "usb")) {
			pr_info("usb present\n");
		}
#ifdef OPLUS_CHG_OP_DEF
		if (chip && chip->ctrl_lcm_frequency.work.func)
			mod_delayed_work(system_highpri_wq, &chip->ctrl_lcm_frequency,
								50);
		if (chip && chip->recovery_chg_type_work.work.func
			&& chip->abnormal_volt_detected && (boot_mode == MSM_BOOT_MODE__NORMAL))
			schedule_delayed_work(&chip->recovery_chg_type_work, msecs_to_jiffies(800));
#endif
		break;
	case OPLUS_CHG_EVENT_APSD_DONE:
		if (owner_ocm == NULL) {
			pr_err("This event(=%d) does not support anonymous sending\n",
				val);
			return NOTIFY_BAD;
		}
		if (!strcmp(owner_ocm->desc->name, "usb")) {
			pr_info("apsd done\n");
			if (op_dev->usb_ocm) {
				oplus_chg_mod_changed(op_dev->usb_ocm);
			}
		}
		break;
	case OPLUS_CHG_EVENT_SWARP_ONLINE:
		if (owner_ocm == NULL) {
			pr_err("This event(=%d) does not support anonymous sending\n",
				val);
			return NOTIFY_BAD;
		}
		if (!strcmp(owner_ocm->desc->name, "usb")) {
			pr_info("swarp online\n");
			if (op_dev->usb_ocm) {
				oplus_chg_mod_changed(op_dev->usb_ocm);
			}
		}
	default:
		break;
	}

	return NOTIFY_OK;
}

static int oplus_chg_intf_usb_mod_notifier_call(struct notifier_block *nb,
		unsigned long val, void *v)
{
	struct oplus_chg_device *op_dev = container_of(nb, struct oplus_chg_device, usb_mod_nb);
	struct oplus_chg_mod *owner_ocm = v;
	struct oplus_chg_chip *chip = oplus_chg_get_chg_struct();
	int hw_detect = 0;

	if (!chip) {
		// pr_err("oplus chip is NULL\n");
		return NOTIFY_BAD;
	}

	switch(val) {
	case OPLUS_CHG_EVENT_ONLINE:
#ifdef OPLUS_CHG_OP_DEF
		if (oplus_warp_ignore_event() == true)
			break;
#endif
		if (!strcmp(owner_ocm->desc->name, "usb")) {
			pr_info("usb online\n");
			if (atomic_read(&op_dev->usb_online) == 0) {
				atomic_set(&op_dev->usb_online, 1);
				if (op_dev->usb_ocm) {
					oplus_chg_global_event(op_dev->usb_ocm, OPLUS_CHG_EVENT_PRESENT);
					oplus_chg_global_event(op_dev->usb_ocm, val);
					oplus_chg_mod_changed(op_dev->usb_ocm);
				}
			}
		}
		break;
	case OPLUS_CHG_EVENT_OFFLINE:
#ifdef OPLUS_CHG_OP_DEF
		if (oplus_warp_ignore_event() == true) {
			schedule_delayed_work(&op_dev->charger_status_check_work, msecs_to_jiffies(5000));
			break;
		}
#endif
		if (!strcmp(owner_ocm->desc->name, "usb")) {
			pr_info("usb offline\n");
			chip->svid_verified = false;
			chip->is_oplus_svid = false;
			chip->usb_enum_status = false;
			if ((atomic_read(&op_dev->usb_online) != 0) ||(atomic_read(&op_dev->usb_present) != 0)) {
				(void)oplus_chg_hw_detect(op_dev, &hw_detect);
				pr_info("hw_detect=%d, reconnect_count=%d\n", hw_detect, chip->reconnect_count);
				if (hw_detect) {
					op_dev->icon_debounce = true;
					if (chip->reconnect_count == 0)
						chip->norchg_reconnect_count++;
					schedule_delayed_work(&op_dev->connect_check_work, msecs_to_jiffies(2000));
				} else {
					chip->reconnect_count = 0;
					chip->norchg_reconnect_count = 0;
					op_dev->icon_debounce = false;
					pr_info("sid_backup clean\n");
					op_dev->sid_backup = 0;
					oplus_warp_reset_fastchg_after_usbout();
				}
				atomic_set(&op_dev->usb_online, 0);
				atomic_set(&op_dev->usb_present, 0);
				chip->charger_exist_delay = false;
				if (op_dev->usb_ocm) {
					oplus_chg_global_event(op_dev->usb_ocm, val);
					oplus_chg_mod_changed(op_dev->usb_ocm);
				}
			}
		}
		break;
	case OPLUS_CHG_EVENT_PRESENT:
#ifdef OPLUS_CHG_OP_DEF
		if (oplus_warp_ignore_event() == true)
			break;
#endif
		if (!strcmp(owner_ocm->desc->name, "usb")) {
			pr_info("usb present\n");
			if (atomic_read(&op_dev->usb_present) == 0) {
				atomic_set(&op_dev->usb_present, 1);
				if (op_dev->usb_ocm) {
					// oplus_chg_global_event(op_dev->usb_ocm, val);
					oplus_chg_mod_changed(op_dev->usb_ocm);
				}
				op_dev->batt_update_count = 0;
				schedule_delayed_work(&op_dev->batt_update_work,
					msecs_to_jiffies(500));
			}
		}
		break;
	case OPLUS_CHG_EVENT_NO_PRESENT:
#ifdef OPLUS_CHG_OP_DEF
		if (oplus_warp_ignore_event() == true)
			break;
#endif
		if (!strcmp(owner_ocm->desc->name, "usb")) {
			pr_info("usb no present\n");
			chip->svid_verified = false;
			chip->is_oplus_svid = false;
			chip->usb_enum_status = false;
			if (atomic_read(&op_dev->usb_present) != 0) {
				(void)oplus_chg_hw_detect(op_dev, &hw_detect);
				if (hw_detect) {
					op_dev->icon_debounce = true;
					if (chip->reconnect_count == 0)
						chip->norchg_reconnect_count++;
					schedule_delayed_work(&op_dev->connect_check_work, msecs_to_jiffies(2000));
				} else {
					chip->reconnect_count = 0;
					chip->norchg_reconnect_count = 0;
					op_dev->icon_debounce = false;
					op_dev->sid_backup = 0;
				}
				atomic_set(&op_dev->usb_present, 0);
				chip->charger_exist_delay = false;
				if (op_dev->usb_ocm) {
					oplus_chg_global_event(op_dev->usb_ocm,
						OPLUS_CHG_EVENT_OFFLINE);
					oplus_chg_mod_changed(op_dev->usb_ocm);
				}
			}
		}
		break;
	case OPLUS_CHG_EVENT_OTG_ENABLE:
		schedule_work(&op_dev->otg_enable_work);
		break;
	case OPLUS_CHG_EVENT_OTG_DISABLE:
		schedule_work(&op_dev->otg_disable_work);
		break;
	case OPLUS_CHG_EVENT_LCD_ON:
		pr_info("lcd on\n");
		oplus_chg_set_led_status(true);
		break;
	case OPLUS_CHG_EVENT_LCD_OFF:
		pr_info("lcd off\n");
		oplus_chg_set_led_status(false);
		break;
	case OPLUS_CHG_EVENT_CALL_ON:
		pr_info("call on\n");
		chip->calling_on = true;
		break;
	case OPLUS_CHG_EVENT_CALL_OFF:
		pr_info("call off\n");
		chip->calling_on = false;
		break;
	case OPLUS_CHG_EVENT_CAMERA_ON:
		pr_info("camera on\n");
		oplus_chg_set_camera_status(true);
		break;
	case OPLUS_CHG_EVENT_CAMERA_OFF:
		pr_info("camera off\n");
		oplus_chg_set_camera_status(false);
		break;
#ifdef OPLUS_CHG_REG_DUMP_ENABLE
	case OPLUS_CHG_EVENT_REG_DUMP:
		schedule_work(&op_dev->reg_dump_work);
		break;
#endif
	default:
		break;
	}

	return NOTIFY_OK;
}

static void oplus_dwc3_msm_notify_event(enum oplus_dwc3_notify_event event)
{
	struct oplus_chg_chip *chip = oplus_chg_get_chg_struct();

	if (!chip) {
		pr_err("chip is null\n");
		return;
	}

	switch (event) {
		case DWC3_ENUM_DONE:
			pr_info("chip->charger_type[%d]\n", chip->charger_type);
			if (chip->svid_verified == true ||
				chip->charger_type == POWER_SUPPLY_TYPE_USB_CDP ||
				chip->charger_type == POWER_SUPPLY_TYPE_USB) {
					chip->usb_enum_status = true;
					chip->chg_ops->update_usb_type();
					pr_info("Enumeration done\n");
			}
			break;
		default:
			pr_info("other dwc3 event received\n");
			break;
	}
}

static int oplus_chg_intf_usb_changed_notifier_call(struct notifier_block *nb,
		unsigned long val, void *v)
{
	struct oplus_chg_device *op_dev = container_of(nb, struct oplus_chg_device, usb_changed_nb);
	struct oplus_chg_mod *owner_ocm = v;

	switch(val) {
	case OPLUS_CHG_EVENT_CHANGED:
		if (!strcmp(owner_ocm->desc->name, "usb") && is_usb_psy_available(op_dev))
			power_supply_changed(op_dev->usb_psy);
		break;
	default:
		break;
	}

	return NOTIFY_OK;
}

static int oplus_chg_intf_mmi_chg_enable(struct oplus_chg_chip *chip, bool en)
{
	pr_info("set mmi_chg = [%d].\n", en);
	chip->charging_suspend = !en;
	if (!en) {
		if(chip->unwakelock_chg == 1) {
			return -EINVAL;
			pr_err("unwakelock testing , this test not allowed.\n");
		} else {
			chip->mmi_chg = 0;
			oplus_chg_turn_off_charging(chip);
			if (oplus_warp_get_fastchg_started() == true) {
				oplus_warp_turn_off_fastchg();
				chip->mmi_fastchg = 0;
			}
			chip->chg_ops->charging_disable();
		}
	} else {
		if (chip->stop_voter != 0) {
			pr_err("stop_voter = 0x%02x, can't enable charge\n");
			return -EINVAL;
		}
		if(chip->unwakelock_chg == 1) {
			return -EINVAL;
			pr_err("unwakelock testing , this test not allowed.\n");
		} else {
			chip->mmi_chg = 1;
			if (chip->mmi_fastchg == 0) {
				oplus_chg_clear_chargerid_info();
			}
			chip->mmi_fastchg = 1;
			oplus_chg_turn_on_charging(chip);
		}
	}

	return 0;
}

static ssize_t oplus_chg_intf_batt_voltage_now_cell_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	int ret;
	struct oplus_chg_chip *chip = oplus_chg_get_chg_struct();

	if (!chip) {
		pr_err("oplus chip is NULL\n");
		return -ENODEV;
	}

	if (chip->vbatt_num == 2)
		ret = sprintf(buf, "%d,%d\n", chip->batt_volt_max * 1000,
				chip->batt_volt_min * 1000);
	else
		ret = sprintf(buf, "%d\n", chip->batt_volt * 1000);

	return ret;
}

static enum oplus_chg_mod_property oplus_chg_intf_batt_props[] = {
	OPLUS_CHG_PROP_STATUS,
	OPLUS_CHG_PROP_PRESENT,
	OPLUS_CHG_PROP_VOLTAGE_NOW,
	OPLUS_CHG_PROP_VOLTAGE_MAX,
	OPLUS_CHG_PROP_VOLTAGE_MIN,
	OPLUS_CHG_PROP_CURRENT_NOW,
	OPLUS_CHG_PROP_CURRENT_MAX,
	OPLUS_CHG_PROP_TEMP_REGION,
	OPLUS_CHG_PROP_CHG_ENABLE,
	OPLUS_CHG_PROP_CAPACITY,
	OPLUS_CHG_PROP_REAL_CAPACITY,
	OPLUS_CHG_PROP_CHARGE_TYPE,
	OPLUS_CHG_PROP_CELL_NUM,
	OPLUS_CHG_PROP_MODEL_NAME,
	OPLUS_CHG_PROP_TEMP,
	OPLUS_CHG_PROP_HEALTH,
	OPLUS_CHG_PROP_TECHNOLOGY,
	OPLUS_CHG_PROP_CYCLE_COUNT,
	OPLUS_CHG_PROP_VOLTAGE_OCV,
	OPLUS_CHG_PROP_CHARGE_CONTROL_LIMIT,
	OPLUS_CHG_PROP_CHARGE_CONTROL_LIMIT_MAX,
	OPLUS_CHG_PROP_CHARGE_COUNTER,
	OPLUS_CHG_PROP_CHARGE_FULL_DESIGN,
	OPLUS_CHG_PROP_CHARGE_FULL,
	OPLUS_CHG_PROP_TIME_TO_FULL_AVG,
	OPLUS_CHG_PROP_TIME_TO_FULL_NOW,
	OPLUS_CHG_PROP_TIME_TO_EMPTY_AVG,
	OPLUS_CHG_PROP_POWER_NOW,
	OPLUS_CHG_PROP_POWER_AVG,
	OPLUS_CHG_PROP_CAPACITY_LEVEL,
	OPLUS_CHG_PROP_VOLTAGE_NOW_CELL1,
	OPLUS_CHG_PROP_VOLTAGE_NOW_CELL2,
	OPLUS_CHG_PROP_MMI_CHARGING_ENABLE,
	OPLUS_CHG_PROP_REMAINING_CAPACITY,
	OPLUS_CHG_PROP_BATTERY_NOTIFY_CODE,
#ifndef CONFIG_OPLUS_CHG_OOS
	OPLUS_CHG_PROP_AUTHENTICATE,
	OPLUS_CHG_PROP_BATTERY_CC,
	OPLUS_CHG_PROP_BATTERY_FCC,
	OPLUS_CHG_PROP_BATTERY_RM,
	OPLUS_CHG_PROP_BATTERY_SOH,
#ifdef CONFIG_OPLUS_CALL_MODE_SUPPORT
	OPLUS_CHG_PROP_CALL_MODE,
#endif
	OPLUS_CHG_PROP_CHARGE_TECHNOLOGY,
#ifdef CONFIG_OPLUS_CHIP_SOC_NODE
	OPLUS_CHG_PROP_CHIP_SOC,
#endif
#ifdef CONFIG_OPLUS_SMART_CHARGER_SUPPORT
	OPLUS_CHG_PROP_COOL_DOWN,
#endif
	OPLUS_CHG_PROP_FAST_CHARGE,
#ifdef CONFIG_OPLUS_SHORT_C_BATT_CHECK
#ifdef CONFIG_OPLUS_SHORT_USERSPACE
	OPLUS_CHG_PROP_SHORT_C_LIMIT_CHG,
	OPLUS_CHG_PROP_SHORT_C_LIMIT_RECHG,
	OPLUS_CHG_PROP_CHARGE_TERM_CURRENT,
	OPLUS_CHG_PROP_INPUT_CURRENT_SETTLED,
#endif
#endif
#ifdef CONFIG_OPLUS_SHORT_HW_CHECK
	OPLUS_CHG_PROP_SHORT_C_HW_FEATURE,
	OPLUS_CHG_PROP_SHORT_C_HW_STATUS,
#endif
#ifdef CONFIG_OPLUS_SHORT_IC_CHECK
	OPLUS_CHG_PROP_SHORT_IC_OTP_STATUS,
	OPLUS_CHG_PROP_SHORT_IC_VOLT_THRESH,
	OPLUS_CHG_PROP_SHORT_IC_OTP_VALUE,
#endif
	OPLUS_CHG_PROP_VOOCCHG_ING,
	OPLUS_CHG_PROP_SHIP_MODE,
	OPLUS_CHG_PROP_CHARGE_NOW,
#endif /* CONFIG_OPLUS_CHG_OOS */
};

static enum oplus_chg_mod_property oplus_chg_intf_batt_uevent_props[] = {
	OPLUS_CHG_PROP_STATUS,
	OPLUS_CHG_PROP_PRESENT,
	OPLUS_CHG_PROP_VOLTAGE_NOW,
	OPLUS_CHG_PROP_CURRENT_NOW,
	OPLUS_CHG_PROP_TEMP_REGION,
	OPLUS_CHG_PROP_CHG_ENABLE,
	OPLUS_CHG_PROP_CAPACITY,
	OPLUS_CHG_PROP_REAL_CAPACITY,
	OPLUS_CHG_PROP_CHARGE_TYPE,
	OPLUS_CHG_PROP_TEMP,
	OPLUS_CHG_PROP_HEALTH,
	OPLUS_CHG_PROP_TIME_TO_FULL_AVG,
	OPLUS_CHG_PROP_TIME_TO_FULL_NOW,
	OPLUS_CHG_PROP_TIME_TO_EMPTY_AVG,
	OPLUS_CHG_PROP_POWER_NOW,
	OPLUS_CHG_PROP_POWER_AVG,
	OPLUS_CHG_PROP_REMAINING_CAPACITY,
};

static struct oplus_chg_exten_prop oplus_chg_intf_batt_exten_props[] = {
	OPLUS_CHG_EXTEN_ROATTR(OPLUS_CHG_EXTERN_PROP_VOLTAGE_NOW_CELL, oplus_chg_intf_batt_voltage_now_cell),
};

#ifdef CONFIG_OPLUS_CHG_OOS
static int batt_err_code_table_color_to_oos(int notify_code)
{
	int ret = 0;

	if (notify_code & (1 << NOTIFY_CHGING_OVERTIME)) {
		ret = PROTECT_CHG_OVERTIME;
	} else if (notify_code & (1 << NOTIFY_OVP_VOLTAGE_ABNORMAL)) {
		ret = PROTECT_BATT_OVP_VOL_ABNORMAL;
	} else if (notify_code & (1 << NOTIFY_CHARGER_OVER_VOL)) {
		ret = PROTECT_CHG_OVP;/* 1: VCHG > 5.8V     */
	} else if (notify_code & (1 << NOTIFY_CHARGER_LOW_VOL)) {
		ret = 0;/* VCHG < 4.3v*/
	} else if (notify_code & (1 << NOTIFY_BAT_OVER_TEMP)) {
		ret = PROTECT_BATT_TEMP_REGION__HOT;
	} else if (notify_code & (1 << NOTIFY_BAT_LOW_TEMP)) {
		ret = PROTECT_BATT_TEMP_REGION_COLD;/*temp < -2 degree*/
	} else if (notify_code & (1 << NOTIFY_BAT_NOT_CONNECT)) {
		ret = PROTECT_BATT_MISSING;
	} else if (notify_code & (1 << NOTIFY_BAT_FULL_THIRD_BATTERY)) {
		ret = NOTIFY_BAT_FULL_THIRD_BATTERY;
	} else if (notify_code & (1 << NOTIFY_BAT_OVER_VOL)) {
		ret = PROTECT_BATT_OVP;
	} else if (notify_code & (1 << NOTIFY_BAT_FULL_PRE_HIGH_TEMP)) {
		ret = PROTECT_BATT_TEMP_REGION_WARM;
	} else if (notify_code & (1 << NOTIFY_BAT_FULL_PRE_LOW_TEMP)) {
		ret = PROTECT_BATT_TEMP_REGION_LITTLE_COLD; /* 7: -2 < t <=  0 */
	} else if (notify_code & (1 << NOTIFY_BAT_FULL)) {
		ret = 0;
	} else if (notify_code & (1 << NOTIFY_BAT_VOLTAGE_DIFF)) {
		ret = PROTECT_BATT_VOL_DIFF_TOO_LARGE;
	} else {
		ret = 0;
	}
	return ret;
}
#endif

static int oplus_chg_intf_batt_get_prop(struct oplus_chg_mod *ocm,
			enum oplus_chg_mod_property prop,
			union oplus_chg_mod_propval *pval)
{
	struct oplus_chg_device *oplus_dev = oplus_chg_mod_get_drvdata(ocm);
	struct oplus_chg_chip *chip = oplus_chg_get_chg_struct();
	union oplus_chg_mod_propval temp_val = {0, };
	int batt_health = POWER_SUPPLY_HEALTH_UNKNOWN;
	bool wls_online = false;
	bool usb_online = false;
	int rc = 0;

	if (!chip) {
		// pr_err("oplus chip is NULL\n");
		return -ENODEV;
	}

	switch (prop) {
	case OPLUS_CHG_PROP_STATUS:
		if (is_wls_ocm_available(oplus_dev)) {
			rc = oplus_chg_mod_get_property(oplus_dev->wls_ocm, OPLUS_CHG_PROP_PRESENT, &temp_val);
			if (rc == 0)
				wls_online = !!temp_val.intval;
		}
		if (is_usb_ocm_available(oplus_dev)) {
			rc = oplus_chg_mod_get_property(oplus_dev->usb_ocm, OPLUS_CHG_PROP_ONLINE, &temp_val);
			if (rc == 0)
				usb_online = !!temp_val.intval;
		}
		if (wls_online && is_comm_ocm_available(oplus_dev)) {
			pval->intval = oplus_chg_comm_get_batt_status(oplus_dev->comm_ocm);
		} else {
			if (!usb_online && !wls_online) {
#ifdef 	CONFIG_OPLUS_CHG_OOS
				pval->intval = POWER_SUPPLY_STATUS_DISCHARGING;
#else
				pval->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
#endif
			} else if ((oplus_chg_show_warp_logo_ornot() == 1) ||
				   oplus_warp_ignore_event() ||
				   oplus_dev->icon_debounce) {
				if(chip->new_ui_warning_support
					&& (chip->tbatt_status == BATTERY_STATUS__WARM_TEMP && chip->batt_full))
					pval->intval = chip->prop_status;
				else
					pval->intval = POWER_SUPPLY_STATUS_CHARGING;
			} else if (!chip->authenticate) {
#ifdef CONFIG_OPLUS_CHG_OOS
				chip->prop_status = POWER_SUPPLY_STATUS_DISCHARGING;
#else
				chip->prop_status = POWER_SUPPLY_STATUS_NOT_CHARGING;
#endif
			} else {
				pval->intval = chip->prop_status;
			}
		}
		break;
	case OPLUS_CHG_PROP_PRESENT:
		pval->intval = chip->batt_exist;
		break;
	case OPLUS_CHG_PROP_VOLTAGE_NOW:
		pval->intval = chip->batt_volt * 1000;
		break;
	case OPLUS_CHG_PROP_VOLTAGE_MAX:
		pval->intval = chip->batt_volt_max * 1000;
		break;
	case OPLUS_CHG_PROP_VOLTAGE_MIN:
		pval->intval = chip->batt_volt_min * 1000;
		break;
	case OPLUS_CHG_PROP_CURRENT_NOW:
		if (oplus_warp_get_fastchg_started() == true) {
			chip->icharging = oplus_gauge_get_prev_batt_current();
		} else {
			chip->icharging = oplus_gauge_get_batt_current();
		}
#ifdef CONFIG_OPLUS_CHG_OOS
		pval->intval = chip->icharging * 1000;
#else
		pval->intval = chip->icharging;
#endif
		break;
	case OPLUS_CHG_PROP_CURRENT_MAX:
		pval->intval = 3000000;
		break;
	case OPLUS_CHG_PROP_TEMP_REGION:
		pval->intval = OPLUS_CHG_BATT_TEMP_NORMAL;
		break;
	case OPLUS_CHG_PROP_CHG_ENABLE:
		pval->intval = chip->mmi_chg;
		if (is_wls_ocm_available(oplus_dev)) {
			rc = oplus_chg_mod_get_property(oplus_dev->wls_ocm,
							OPLUS_CHG_PROP_BATT_CHG_ENABLE,
							&temp_val);
			if (rc < 0) {
				pr_err("can't get wireless charge, rc=%d\n", rc);
				break;
			}
			pval->intval = (!!pval->intval) && (!!temp_val.intval);
		}
		break;
	case OPLUS_CHG_PROP_CAPACITY:
		if (oplus_dev->soc_debug_mode){
			pval->intval = oplus_dev->debug_soc;
		}else{
			if(chip->warp_show_ui_soc_decimal == true && chip->decimal_control) {
				pval->intval = (chip->ui_soc_integer + chip->ui_soc_decimal)/1000;
			} else {
				pval->intval = chip->ui_soc;
			}
			if(pval->intval > 100) {
				pval->intval = 100;
			}
		}
		break;
	case OPLUS_CHG_PROP_REAL_CAPACITY:
		if (oplus_dev->soc_debug_mode)
			pval->intval = oplus_dev->debug_soc;
		else
			pval->intval = oplus_chg_get_soc();
		break;
	case OPLUS_CHG_PROP_CHARGE_TYPE:
		pval->intval = OPLUS_CHG_CHARGE_TYPE_UNKNOWN;
		break;
	case OPLUS_CHG_PROP_CELL_NUM:
		pval->intval = chip->vbatt_num;
		break;
	case OPLUS_CHG_PROP_MODEL_NAME:
		pval->strval = "OP_4500mAh";
		break;
	case OPLUS_CHG_PROP_TEMP:
		if (oplus_dev->temp_debug_mode)
			pval->intval = oplus_dev->debug_temp;
		else
			pval->intval = chip->temperature - chip->offset_temp;
		break;
	case OPLUS_CHG_PROP_HEALTH:
		if (is_wls_ocm_available(oplus_dev)) {
			rc = oplus_chg_mod_get_property(oplus_dev->wls_ocm, OPLUS_CHG_PROP_PRESENT, &temp_val);
			if (rc == 0)
				wls_online = !!temp_val.intval;
			else
				rc = 0;
		}
		if (is_comm_ocm_available(oplus_dev) && wls_online)
			batt_health = oplus_chg_comm_get_batt_health(oplus_dev->comm_ocm);
		if ((batt_health == POWER_SUPPLY_HEALTH_UNKNOWN) ||
		    (batt_health == POWER_SUPPLY_HEALTH_GOOD)) {
			pval->intval = oplus_chg_get_prop_batt_health(chip);
		} else {
			pval->intval = batt_health;
		}
		break;
	case OPLUS_CHG_PROP_TECHNOLOGY:
		pval->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		break;
	case OPLUS_CHG_PROP_CYCLE_COUNT:
		pval->intval = 0;
		break;
	case OPLUS_CHG_PROP_VOLTAGE_OCV:
		pval->intval = 4000000;
		break;
	case OPLUS_CHG_PROP_CHARGE_CONTROL_LIMIT:
		pval->intval = 2000000;
		break;
	case OPLUS_CHG_PROP_CHARGE_CONTROL_LIMIT_MAX:
		pval->intval = 3000000;
		break;
	case OPLUS_CHG_PROP_CHARGE_COUNTER:
		//pval->intval = chip->chg_ops->oplus_chg_get_charge_counter();
		pval->intval = 2200000; //BSP,temp fix xts
		break;
	case OPLUS_CHG_PROP_CHARGE_FULL_DESIGN:
		pval->intval = chip->batt_fcc * 1000;
		break;
	case OPLUS_CHG_PROP_CHARGE_FULL:
		pval->intval = chip->batt_fcc * 1000;
		break;
	case OPLUS_CHG_PROP_TIME_TO_FULL_AVG:
		pval->intval = 5000;
		break;
	case OPLUS_CHG_PROP_TIME_TO_FULL_NOW:
		pval->intval = 5000;
		break;
	case OPLUS_CHG_PROP_TIME_TO_EMPTY_AVG:
		pval->intval = 5000;
		break;
	case OPLUS_CHG_PROP_POWER_NOW:
		pval->intval = 5000;
		break;
	case OPLUS_CHG_PROP_POWER_AVG:
		pval->intval = 5000;
		break;
	case OPLUS_CHG_PROP_VOLTAGE_NOW_CELL1:
		pval->intval = chip->batt_volt_max * 1000;
		break;
	case OPLUS_CHG_PROP_VOLTAGE_NOW_CELL2:
		pval->intval = chip->batt_volt_min * 1000;
		break;
	case OPLUS_CHG_PROP_MMI_CHARGING_ENABLE:
		pval->intval = chip->mmi_chg;
		break;
	case OPLUS_CHG_PROP_CAPACITY_LEVEL:
		rc = oplus_chg_get_ui_soc();
		if (rc < 0)
			pval->intval = CAPACITY_LEVEL_UNKNOWN;
		else if (rc == 0)
			pval->intval = CAPACITY_LEVEL_CRITICAL;
		else if (rc <= 15)
			pval->intval = CAPACITY_LEVEL_LOW;
		else if (rc <= 85)
			pval->intval = CAPACITY_LEVEL_NORMAL;
		else if (rc < 100)
			pval->intval = CAPACITY_LEVEL_HIGH;
		else if (rc == 100)
			pval->intval = CAPACITY_LEVEL_FULL;
		else
			pval->intval = CAPACITY_LEVEL_NORMAL;
		break;
	case OPLUS_CHG_PROP_REMAINING_CAPACITY:
		pval->intval = chip->batt_rm;
		break;
	case OPLUS_CHG_PROP_BATTERY_NOTIFY_CODE:
#ifdef CONFIG_OPLUS_CHG_OOS
		if (chip->abnormal_volt_detected)
			chip->notify_code |= 1 << NOTIFY_OVP_VOLTAGE_ABNORMAL;
		pval->intval =  batt_err_code_table_color_to_oos(chip->notify_code | oplus_dev->notify_code);
#else
		pval->intval = chip->notify_code | oplus_dev->notify_code;
#endif
		break;
#ifndef CONFIG_OPLUS_CHG_OOS
	case OPLUS_CHG_PROP_AUTHENTICATE:
		pval->intval = chip->authenticate;
		break;
	case OPLUS_CHG_PROP_BATTERY_CC:
		pval->intval = chip->batt_cc;
		break;
	case OPLUS_CHG_PROP_BATTERY_FCC:
		pval->intval = chip->batt_fcc;
		break;
	case OPLUS_CHG_PROP_BATTERY_RM:
		pval->intval = chip->batt_rm;
		break;
	case OPLUS_CHG_PROP_BATTERY_SOH:
		pval->intval = chip->batt_soh;
		break;
#ifdef CONFIG_OPLUS_CALL_MODE_SUPPORT
	case OPLUS_CHG_PROP_CALL_MODE:
		pval->intval = chip->calling_on;
		break;
#endif
	case OPLUS_CHG_PROP_CHARGE_TECHNOLOGY:
		pval->intval = chip->warp_project;
		break;
#ifdef CONFIG_OPLUS_CHIP_SOC_NODE
	case OPLUS_CHG_PROP_CHIP_SOC:
		pval->intval = chip->soc;
		break;
#endif
#ifdef CONFIG_OPLUS_SMART_CHARGER_SUPPORT
	case OPLUS_CHG_PROP_COOL_DOWN:
		pval->intval = chip->cool_down;
		break;
#endif
	case OPLUS_CHG_PROP_FAST_CHARGE:
		pval->intval = oplus_chg_show_warp_logo_ornot();
		if (oplus_dev->sid_backup != 0)
			pval->intval = 1;
		break;
#ifdef CONFIG_OPLUS_SHORT_C_BATT_CHECK
#ifdef CONFIG_OPLUS_SHORT_USERSPACE
	case OPLUS_CHG_PROP_SHORT_C_LIMIT_CHG:
		pval->intval = chip->short_c_batt.limit_chg;
		break;
	case OPLUS_CHG_PROP_SHORT_C_LIMIT_RECHG:
		pval->intval = chip->short_c_batt.limit_rechg;
		break;
	case OPLUS_CHG_PROP_CHARGE_TERM_CURRENT:
		pval->intval = chip->limits.iterm_ma;
		break;
	case OPLUS_CHG_PROP_INPUT_CURRENT_SETTLED:
		pval->intval = 2000;
		if (chip && chip->chg_ops->get_dyna_aicl_result)
			pval->intval = chip->chg_ops->get_dyna_aicl_result();
		break;
#endif
#endif
#ifdef CONFIG_OPLUS_SHORT_HW_CHECK
	case OPLUS_CHG_PROP_SHORT_C_HW_FEATURE:
		pval->intval = chip->short_c_batt.is_feature_hw_on;
		break;
#endif
#ifdef CONFIG_OPLUS_SHORT_IC_CHECK
	case OPLUS_CHG_PROP_SHORT_C_HW_STATUS:
		pval->intval = chip->short_c_batt.shortc_gpio_status;
		break;
#endif
#ifdef CONFIG_OPLUS_SHORT_IC_CHECK
	case OPLUS_CHG_PROP_SHORT_IC_OTP_STATUS:
		pval->intval = chip->short_c_batt.ic_short_otp_st;
		break;
	case OPLUS_CHG_PROP_SHORT_IC_VOLT_THRESH:
		pval->intval = chip->short_c_batt.ic_volt_threshold;
		break;
	case OPLUS_CHG_PROP_SHORT_IC_OTP_VALUE:
		pval->intval = oplus_short_ic_get_otp_error_value(chip);
		break;
#endif
	case OPLUS_CHG_PROP_VOOCCHG_ING:
		pval->intval = oplus_warp_get_fastchg_ing();
		if (is_wls_ocm_available(oplus_dev) && !pval->intval) {
			rc = oplus_chg_mod_get_property(oplus_dev->wls_ocm, OPLUS_CHG_PROP_FASTCHG_STATUS, pval);
			if (rc < 0) {
				rc = 0;
				pval->intval = 0;
			}
		}
		break;
	case OPLUS_CHG_PROP_SHIP_MODE:
		pval->intval = chip->enable_shipmode;
		break;
	case OPLUS_CHG_PROP_CHARGE_NOW:
		pval->intval = chip->charger_volt;
		if ((oplus_warp_get_fastchg_started() == true) &&
		    (chip->vbatt_num == 2) &&
		    (oplus_warp_get_fast_chg_type() != CHARGER_SUBTYPE_FASTCHG_WARP)) {
			pval->intval = 10000;
		}
		break;
#endif /* CONFIG_OPLUS_CHG_OOS */
	default:
		pr_err("get prop %d is not supported\n", prop);
		return -EINVAL;
	}
	if (rc < 0) {
		pr_err("Couldn't get prop %d rc = %d\n", prop, rc);
		return -ENODATA;
	}
	return 0;
}

static int oplus_chg_intf_batt_set_prop(struct oplus_chg_mod *ocm,
			enum oplus_chg_mod_property prop,
			const union oplus_chg_mod_propval *pval)
{
	struct oplus_chg_device *oplus_dev = oplus_chg_mod_get_drvdata(ocm);
	struct oplus_chg_chip *chip = oplus_chg_get_chg_struct();
	union oplus_chg_mod_propval temp_val;
	bool wls_online = false;
	int rc = 0;

	if (!chip) {
		// pr_err("oplus chip is NULL\n");
		return -ENODEV;
	}

	switch (prop) {
	case OPLUS_CHG_PROP_VOLTAGE_MAX:
		break;
	case OPLUS_CHG_PROP_VOLTAGE_MIN:
		break;
	case OPLUS_CHG_PROP_CURRENT_MAX:
		break;
	case OPLUS_CHG_PROP_CHG_ENABLE:
		if (atomic_read(&oplus_dev->usb_online)) {
			rc = oplus_chg_intf_mmi_chg_enable(chip, !!pval->intval);
			if (rc < 0) {
				pr_err("can't %s usb charge, rc=%d\n", !!pval->intval ? "enable" : "disable", rc);
				break;
			}
		}
		if (is_wls_ocm_available(oplus_dev)) {
			rc = oplus_chg_mod_get_property(oplus_dev->wls_ocm, OPLUS_CHG_PROP_ONLINE, &temp_val);
			if (rc == 0)
				wls_online = !!temp_val.intval;
			if (wls_online) {
				rc = oplus_chg_mod_set_property(oplus_dev->wls_ocm,
								OPLUS_CHG_PROP_BATT_CHG_ENABLE,
								pval);
				if (rc < 0) {
					pr_err("can't %s wireless charge, rc=%d\n", !!pval->intval ? "enable" : "disable", rc);
					break;
				}
			}
		}
		break;
	case OPLUS_CHG_PROP_TEMP:
		oplus_dev->temp_debug_mode = true;
		oplus_dev->debug_temp = pval->intval;
		break;
	case OPLUS_CHG_PROP_CAPACITY:
		oplus_dev->soc_debug_mode = true;
		oplus_dev->debug_soc = pval->intval;
		break;
	case OPLUS_CHG_PROP_MMI_CHARGING_ENABLE:
		pr_info("set mmi_chg = [%d].\n", pval->intval);
		rc = oplus_chg_intf_mmi_chg_enable(chip, !!pval->intval);
		break;
	case OPLUS_CHG_PROP_BATTERY_NOTIFY_CODE:
		oplus_dev->notify_code = pval->intval;
		break;
#ifndef CONFIG_OPLUS_CHG_OOS
#ifdef CONFIG_OPLUS_CALL_MODE_SUPPORT
	case OPLUS_CHG_PROP_CALL_MODE:
#ifdef OPLUS_CHG_OP_DEF
		if (!!pval->intval) {
			if (is_usb_ocm_available(oplus_dev))
				oplus_chg_anon_mod_event(oplus_dev->usb_ocm, OPLUS_CHG_EVENT_CALL_ON);
			if (is_wls_ocm_available(oplus_dev))
				oplus_chg_anon_mod_event(oplus_dev->wls_ocm, OPLUS_CHG_EVENT_CALL_ON);
		} else {
			if (is_usb_ocm_available(oplus_dev))
				oplus_chg_anon_mod_event(oplus_dev->usb_ocm, OPLUS_CHG_EVENT_CALL_OFF);
			if (is_wls_ocm_available(oplus_dev))
				oplus_chg_anon_mod_event(oplus_dev->wls_ocm, OPLUS_CHG_EVENT_CALL_OFF);
		}
#else
		chip->calling_on = pval->intval;
#endif
		break;
#endif /* CONFIG_OPLUS_CALL_MODE_SUPPORT */
#ifdef CONFIG_OPLUS_SMART_CHARGER_SUPPORT
	case OPLUS_CHG_PROP_COOL_DOWN:
		oplus_smart_charge_by_cool_down(chip, pval->intval);
		if (is_wls_ocm_available(oplus_dev)) {
			(void)oplus_chg_mod_set_property(oplus_dev->wls_ocm,
							 prop, pval);
		}
		break;
#endif
	case OPLUS_CHG_PROP_SHIP_MODE:
		chip->enable_shipmode = pval->intval;
		oplus_gauge_update_soc_smooth_parameter();
		break;
#ifdef CONFIG_OPLUS_SHORT_C_BATT_CHECK
#ifdef CONFIG_OPLUS_SHORT_USERSPACE
	case OPLUS_CHG_PROP_SHORT_C_LIMIT_CHG:
		chip->short_c_batt.limit_chg = !!pval->intval;
		//for userspace logic
		if (!!pval->intval == false)
			chip->short_c_batt.is_switch_on = 0;
		break;
	case OPLUS_CHG_PROP_SHORT_C_LIMIT_RECHG:
		chip->short_c_batt.limit_rechg = !!pval->intval;
		break;
#endif
#endif
#ifdef CONFIG_OPLUS_SHORT_HW_CHECK
	case OPLUS_CHG_PROP_SHORT_C_HW_FEATURE:
		chip->short_c_batt.is_feature_hw_on = pval->intval;
		break;
#endif
#ifdef CONFIG_OPLUS_SHORT_IC_CHECK
	case OPLUS_CHG_PROP_SHORT_IC_VOLT_THRESH:
		chip->short_c_batt.ic_volt_threshold = !!pval->intval;
		oplus_short_ic_set_volt_threshold(chip);
		break;
#endif
#endif /* CONFIG_OPLUS_CHG_OOS */
	default:
		pr_err("set prop %d is not supported\n", prop);
		rc = -EINVAL;
		break;
	}

	return rc;
}

static int oplus_chg_intf_batt_prop_is_writeable(struct oplus_chg_mod *ocm,
				enum oplus_chg_mod_property prop)
{
	switch (prop) {
	case OPLUS_CHG_PROP_VOLTAGE_MAX:
	case OPLUS_CHG_PROP_VOLTAGE_MIN:
	case OPLUS_CHG_PROP_CURRENT_MAX:
	case OPLUS_CHG_PROP_CHG_ENABLE:
	case OPLUS_CHG_PROP_TEMP:
	case OPLUS_CHG_PROP_CAPACITY:
	case OPLUS_CHG_PROP_MMI_CHARGING_ENABLE:
#ifndef CONFIG_OPLUS_CHG_OOS
#ifdef CONFIG_OPLUS_CALL_MODE_SUPPORT
	case OPLUS_CHG_PROP_CALL_MODE:
#endif
#ifdef CONFIG_OPLUS_SMART_CHARGER_SUPPORT
	case OPLUS_CHG_PROP_COOL_DOWN:
#endif
	case OPLUS_CHG_PROP_SHIP_MODE:
#ifdef CONFIG_OPLUS_SHORT_C_BATT_CHECK
#ifdef CONFIG_OPLUS_SHORT_USERSPACE
	case OPLUS_CHG_PROP_SHORT_C_LIMIT_CHG:
	case OPLUS_CHG_PROP_SHORT_C_LIMIT_RECHG:
#endif
#endif
#ifdef CONFIG_OPLUS_SHORT_HW_CHECK
	case OPLUS_CHG_PROP_SHORT_C_HW_FEATURE:
#endif
#ifdef CONFIG_OPLUS_SHORT_IC_CHECK
	case OPLUS_CHG_PROP_SHORT_IC_VOLT_THRESH:
#endif
#endif /* CONFIG_OPLUS_CHG_OOS */
		return 1;
	default:
		break;
	}

	return 0;
}

static const struct oplus_chg_mod_desc oplus_chg_intf_batt_mod_desc = {
	.name = "battery",
	.type = OPLUS_CHG_MOD_BATTERY,
	.properties = oplus_chg_intf_batt_props,
	.num_properties = ARRAY_SIZE(oplus_chg_intf_batt_props),
	.uevent_properties = oplus_chg_intf_batt_uevent_props,
	.uevent_num_properties = ARRAY_SIZE(oplus_chg_intf_batt_uevent_props),
	.exten_properties = oplus_chg_intf_batt_exten_props,
	.num_exten_properties = ARRAY_SIZE(oplus_chg_intf_batt_exten_props),
	.get_property = oplus_chg_intf_batt_get_prop,
	.set_property = oplus_chg_intf_batt_set_prop,
	.property_is_writeable	= oplus_chg_intf_batt_prop_is_writeable,
};

static int oplus_chg_intf_batt_event_notifier_call(struct notifier_block *nb,
		unsigned long val, void *v)
{
	struct oplus_chg_device *op_dev = container_of(nb, struct oplus_chg_device, batt_event_nb);
	struct oplus_chg_mod *owner_ocm = v;
	struct oplus_chg_chip *chip = oplus_chg_get_chg_struct();

	switch(val) {
	case OPLUS_CHG_EVENT_ONLINE:
	case OPLUS_CHG_EVENT_OFFLINE:
	case OPLUS_CHG_EVENT_PRESENT:
	case OPLUS_CHG_EVENT_NO_PRESENT:
		if (owner_ocm == NULL) {
			pr_err("This event(=%d) does not support anonymous sending\n",
				val);
			return NOTIFY_BAD;
		}
		oplus_chg_soc_update();
		if (op_dev->batt_ocm)
			oplus_chg_mod_changed(op_dev->batt_ocm);
		break;
	case OPLUS_CHG_EVENT_CHARGE_DONE:
		atomic_set(&op_dev->chg_done, 1);
		if (chip)
			chip->batt_full = true;
		break;
	case OPLUS_CHG_EVENT_CLEAN_CHARGE_DONE:
		atomic_set(&op_dev->chg_done, 0);
		if (chip)
			chip->batt_full = false;
		break;
	default:
		break;
	}
	return NOTIFY_OK;
}

static int oplus_chg_intf_batt_mod_notifier_call(struct notifier_block *nb,
		unsigned long val, void *v)
{
	// struct oplus_chg_device *op_dev = container_of(nb, struct oplus_chg_device, wls_mod_nb);

	switch(val) {
	case OPLUS_CHG_EVENT_LCD_ON:
		pr_info("lcd on\n");
		break;
	case OPLUS_CHG_EVENT_LCD_OFF:
		pr_info("lcd off\n");
		break;
	case OPLUS_CHG_EVENT_CALL_ON:
		pr_info("call on\n");
		break;
	case OPLUS_CHG_EVENT_CALL_OFF:
		pr_info("call off\n");
		break;
	default:
		break;
	}

	return NOTIFY_OK;
}

static int oplus_chg_intf_batt_changed_notifier_call(struct notifier_block *nb,
		unsigned long val, void *v)
{
	struct oplus_chg_device *op_dev = container_of(nb, struct oplus_chg_device, batt_changed_nb);
	struct oplus_chg_mod *owner_ocm = v;

	switch(val) {
	case OPLUS_CHG_EVENT_CHANGED:
		if (!strcmp(owner_ocm->desc->name, "battery") && is_batt_psy_available(op_dev))
			power_supply_changed(op_dev->batt_psy);
		break;
	default:
		break;
	}

	return NOTIFY_OK;
}

#ifndef CONFIG_OPLUS_CHG_OOS

static enum oplus_chg_mod_property oplus_chg_intf_ac_props[] = {
	OPLUS_CHG_PROP_ONLINE,

};

static enum oplus_chg_mod_property oplus_chg_intf_ac_uevent_props[] = {
	OPLUS_CHG_PROP_ONLINE,
};

static int oplus_chg_intf_ac_get_prop(struct oplus_chg_mod *ocm,
			enum oplus_chg_mod_property prop,
			union oplus_chg_mod_propval *pval)
{
	// struct oplus_chg_device *oplus_dev = oplus_chg_mod_get_drvdata(ocm);
	struct oplus_chg_chip *chip = oplus_chg_get_chg_struct();
	int rc = 0;

	if (!chip) {
		// pr_err("oplus chip is NULL\n");
		return -ENODEV;
	}

	switch (prop) {
	case OPLUS_CHG_PROP_ONLINE:
		if (chip->charger_exist) {
			if ((chip->charger_type == POWER_SUPPLY_TYPE_USB_DCP)
					|| (chip->charger_type == POWER_SUPPLY_TYPE_UNKNOWN)
					|| (oplus_warp_get_fastchg_started() == true)
					|| (oplus_warp_get_fastchg_to_normal() == true)
					|| (oplus_warp_get_fastchg_to_warm() == true)
					|| (oplus_warp_get_fastchg_dummy_started() == true)
					|| (oplus_warp_get_adapter_update_status() == ADAPTER_FW_NEED_UPDATE)
					|| (oplus_warp_get_btb_temp_over() == true)) {
				chip->ac_online = true;
			} else {
				chip->ac_online = false;
			}
		} else {
			if ((oplus_warp_get_fastchg_started() == true)
					|| (oplus_warp_get_fastchg_to_normal() == true)
					|| (oplus_warp_get_fastchg_to_warm() == true)
					|| (oplus_warp_get_fastchg_dummy_started() == true)
					|| (oplus_warp_get_adapter_update_status() == ADAPTER_FW_NEED_UPDATE)
					|| (oplus_warp_get_btb_temp_over() == true)
					|| chip->mmi_fastchg == 0) {
				chip->ac_online = true;
			} else {
				chip->ac_online = false;
			}
		}

		if (chip->ac_online)
			pr_info("chg_exist:%d, ac_online:%d\n", chip->charger_exist, chip->ac_online);
		pval->intval = chip->ac_online;
		break;
	default:
		pr_err("get prop %d is not supported\n", prop);
		return -EINVAL;
	}
	if (rc < 0) {
		pr_err("Couldn't get prop %d rc = %d\n", prop, rc);
		return -ENODATA;
	}
	return 0;
}

static int oplus_chg_intf_ac_set_prop(struct oplus_chg_mod *ocm,
			enum oplus_chg_mod_property prop,
			const union oplus_chg_mod_propval *pval)
{
	// struct oplus_chg_device *oplus_dev = oplus_chg_mod_get_drvdata(ocm);
	struct oplus_chg_chip *chip = oplus_chg_get_chg_struct();
	int rc = 0;

	if (!chip) {
		// pr_err("oplus chip is NULL\n");
		return -ENODEV;
	}

	switch (prop) {
	default:
		pr_err("set prop %d is not supported\n", prop);
		rc = -EINVAL;
		break;
	}

	return rc;
}

static int oplus_chg_intf_ac_prop_is_writeable(struct oplus_chg_mod *ocm,
				enum oplus_chg_mod_property prop)
{
	switch (prop) {
	default:
		break;
	}

	return 0;
}

static const struct oplus_chg_mod_desc oplus_chg_intf_ac_mod_desc = {
	.name = "ac",
	.type = OPLUS_CHG_MOD_MAIN,
	.properties = oplus_chg_intf_ac_props,
	.num_properties = ARRAY_SIZE(oplus_chg_intf_ac_props),
	.uevent_properties = oplus_chg_intf_ac_uevent_props,
	.uevent_num_properties = ARRAY_SIZE(oplus_chg_intf_ac_uevent_props),
	.exten_properties = NULL,
	.num_exten_properties = 0,
	.get_property = oplus_chg_intf_ac_get_prop,
	.set_property = oplus_chg_intf_ac_set_prop,
	.property_is_writeable	= oplus_chg_intf_ac_prop_is_writeable,
};

static int oplus_chg_intf_ac_event_notifier_call(struct notifier_block *nb,
		unsigned long val, void *v)
{
	return NOTIFY_OK;
}

static int oplus_chg_intf_ac_mod_notifier_call(struct notifier_block *nb,
		unsigned long val, void *v)
{
	return NOTIFY_OK;
}

static int oplus_chg_intf_ac_changed_notifier_call(struct notifier_block *nb,
		unsigned long val, void *v)
{
	return NOTIFY_OK;
}

#endif /* CONFIG_OPLUS_CHG_OOS */

static int oplus_chg_intf_init_mod(struct oplus_chg_device *oplus_dev)
{
	struct oplus_chg_mod_config ocm_cfg = {};
	int rc;

	ocm_cfg.drv_data = oplus_dev;
	ocm_cfg.of_node = oplus_dev->dev->of_node;

	oplus_dev->usb_ocm = oplus_chg_mod_register(oplus_dev->dev,
					   &oplus_chg_intf_usb_mod_desc,
					   &ocm_cfg);
	if (IS_ERR(oplus_dev->usb_ocm)) {
		pr_err("Couldn't register usb ocm\n");
		return PTR_ERR(oplus_dev->usb_ocm);
	}
	oplus_dev->usb_ocm->notifier = &usb_ocm_notifier;
	oplus_dev->usb_mod_nb.notifier_call = oplus_chg_intf_usb_mod_notifier_call;
	rc = oplus_chg_reg_mod_notifier(oplus_dev->usb_ocm, &oplus_dev->usb_mod_nb);
	if (rc) {
		pr_err("register usb mod notifier error, rc=%d\n", rc);
		goto reg_usb_mod_notifier_err;
	}
	oplus_dev->usb_event_nb.notifier_call = oplus_chg_intf_usb_event_notifier_call;
	rc = oplus_chg_reg_event_notifier(&oplus_dev->usb_event_nb);
	if (rc) {
		pr_err("register usb event notifier error, rc=%d\n", rc);
		goto reg_usb_event_notifier_err;
	}
	oplus_dev->usb_changed_nb.notifier_call = oplus_chg_intf_usb_changed_notifier_call;
	rc = oplus_chg_reg_changed_notifier(&oplus_dev->usb_changed_nb);
	if (rc) {
		pr_err("register usb changed notifier error, rc=%d\n", rc);
		goto reg_usb_changed_notifier_err;
	}

	oplus_dev->batt_ocm = oplus_chg_mod_register(oplus_dev->dev,
					   &oplus_chg_intf_batt_mod_desc,
					   &ocm_cfg);
	if (IS_ERR(oplus_dev->batt_ocm)) {
		pr_err("Couldn't register batt ocm\n");
		rc = PTR_ERR(oplus_dev->batt_ocm);
		goto reg_batt_mod_err;
	}
	oplus_dev->batt_ocm->notifier = &batt_ocm_notifier;
	oplus_dev->batt_mod_nb.notifier_call = oplus_chg_intf_batt_mod_notifier_call;
	rc = oplus_chg_reg_mod_notifier(oplus_dev->batt_ocm, &oplus_dev->batt_mod_nb);
	if (rc) {
		pr_err("register batt mod notifier error, rc=%d\n", rc);
		goto reg_batt_mod_notifier_err;
	}
	oplus_dev->batt_event_nb.notifier_call = oplus_chg_intf_batt_event_notifier_call;
	rc = oplus_chg_reg_event_notifier(&oplus_dev->batt_event_nb);
	if (rc) {
		pr_err("register batt event notifier error, rc=%d\n", rc);
		goto reg_batt_event_notifier_err;
	}
	oplus_dev->batt_changed_nb.notifier_call = oplus_chg_intf_batt_changed_notifier_call;
	rc = oplus_chg_reg_changed_notifier(&oplus_dev->batt_changed_nb);
	if (rc) {
		pr_err("register batt changed notifier error, rc=%d\n", rc);
		goto reg_batt_changed_notifier_err;
	}

#ifndef CONFIG_OPLUS_CHG_OOS
	oplus_dev->ac_ocm = oplus_chg_mod_register(oplus_dev->dev,
					   &oplus_chg_intf_ac_mod_desc,
					   &ocm_cfg);
	if (IS_ERR(oplus_dev->ac_ocm)) {
		pr_err("Couldn't register ac ocm\n");
		rc = PTR_ERR(oplus_dev->ac_ocm);
		goto reg_ac_mod_err;
	}
	oplus_dev->ac_ocm->notifier = &ac_ocm_notifier;
	oplus_dev->ac_mod_nb.notifier_call = oplus_chg_intf_ac_mod_notifier_call;
	rc = oplus_chg_reg_mod_notifier(oplus_dev->ac_ocm, &oplus_dev->ac_mod_nb);
	if (rc) {
		pr_err("register ac mod notifier error, rc=%d\n", rc);
		goto reg_ac_mod_notifier_err;
	}
	oplus_dev->ac_event_nb.notifier_call = oplus_chg_intf_ac_event_notifier_call;
	rc = oplus_chg_reg_event_notifier(&oplus_dev->ac_event_nb);
	if (rc) {
		pr_err("register ac event notifier error, rc=%d\n", rc);
		goto reg_ac_event_notifier_err;
	}
	oplus_dev->ac_changed_nb.notifier_call = oplus_chg_intf_ac_changed_notifier_call;
	rc = oplus_chg_reg_changed_notifier(&oplus_dev->ac_changed_nb);
	if (rc) {
		pr_err("register ac changed notifier error, rc=%d\n", rc);
		goto reg_ac_changed_notifier_err;
	}
#endif /* CONFIG_OPLUS_CHG_OOS */

	INIT_WORK(&oplus_dev->otg_enable_work, oplus_otg_enable_work);
	INIT_WORK(&oplus_dev->otg_disable_work, oplus_otg_disable_work);
	return 0;

#ifndef CONFIG_OPLUS_CHG_OOS
reg_ac_changed_notifier_err:
	oplus_chg_unreg_event_notifier(&oplus_dev->ac_event_nb);
reg_ac_event_notifier_err:
	oplus_chg_unreg_mod_notifier(oplus_dev->ac_ocm, &oplus_dev->ac_mod_nb);
reg_ac_mod_notifier_err:
	oplus_chg_mod_unregister(oplus_dev->ac_ocm);
reg_ac_mod_err:
	oplus_chg_unreg_changed_notifier(&oplus_dev->batt_changed_nb);
#endif /* CONFIG_OPLUS_CHG_OOS */
reg_batt_changed_notifier_err:
	oplus_chg_unreg_event_notifier(&oplus_dev->batt_event_nb);
reg_batt_event_notifier_err:
	oplus_chg_unreg_mod_notifier(oplus_dev->batt_ocm, &oplus_dev->batt_mod_nb);
reg_batt_mod_notifier_err:
	oplus_chg_mod_unregister(oplus_dev->batt_ocm);
reg_batt_mod_err:
	oplus_chg_unreg_changed_notifier(&oplus_dev->usb_changed_nb);
reg_usb_changed_notifier_err:
	oplus_chg_unreg_event_notifier(&oplus_dev->usb_event_nb);
reg_usb_event_notifier_err:
	oplus_chg_unreg_mod_notifier(oplus_dev->usb_ocm, &oplus_dev->usb_mod_nb);
reg_usb_mod_notifier_err:
	oplus_chg_mod_unregister(oplus_dev->usb_ocm);

	return rc;
}

static int oplus_chg_intf_probe(struct platform_device *pdev)
{
	struct oplus_chg_device *oplus_dev;
	int rc;

	oplus_dev = devm_kzalloc(&pdev->dev, sizeof(struct oplus_chg_device), GFP_KERNEL);
	if (oplus_dev == NULL) {
		pr_err("alloc memory error\n");
		return -ENOMEM;
	}

	oplus_dev->dev = &pdev->dev;
	platform_set_drvdata(pdev, oplus_dev);

	rc = oplus_chg_intf_init_mod(oplus_dev);
	if (rc < 0)
		pr_err("oplus chg common mod init error, rc=%d\n", rc);

	of_platform_populate(oplus_dev->dev->of_node, NULL, NULL, oplus_dev->dev);
	oplus_dwc3_set_notifier(&oplus_dwc3_msm_notify_event);

	INIT_DELAYED_WORK(&oplus_dev->batt_update_work, oplus_chg_batt_update_work);
	INIT_DELAYED_WORK(&oplus_dev->connect_check_work, oplus_chg_connect_check_work);
	INIT_DELAYED_WORK(&oplus_dev->charger_status_check_work, oplus_chg_charger_status_check_work);
#ifdef OPLUS_CHG_REG_DUMP_ENABLE
	INIT_WORK(&oplus_dev->reg_dump_work, oplus_chg_reg_dump_work);
#endif

	return rc;
}

static int oplus_chg_intf_remove(struct platform_device *pdev)
{
	struct oplus_chg_device *oplus_dev = platform_get_drvdata(pdev);

#ifndef CONFIG_OPLUS_CHG_OOS
	oplus_chg_unreg_changed_notifier(&oplus_dev->ac_changed_nb);
	oplus_chg_unreg_event_notifier(&oplus_dev->ac_event_nb);
	oplus_chg_unreg_mod_notifier(oplus_dev->ac_ocm, &oplus_dev->ac_mod_nb);
	oplus_chg_mod_unregister(oplus_dev->ac_ocm);
#endif /* CONFIG_OPLUS_CHG_OOS */
	oplus_chg_unreg_changed_notifier(&oplus_dev->batt_changed_nb);
	oplus_chg_unreg_event_notifier(&oplus_dev->batt_event_nb);
	oplus_chg_unreg_mod_notifier(oplus_dev->batt_ocm, &oplus_dev->batt_mod_nb);
	oplus_chg_mod_unregister(oplus_dev->batt_ocm);
	oplus_chg_unreg_changed_notifier(&oplus_dev->usb_changed_nb);
	oplus_chg_unreg_event_notifier(&oplus_dev->usb_event_nb);
	oplus_chg_unreg_mod_notifier(oplus_dev->usb_ocm, &oplus_dev->usb_mod_nb);
	oplus_chg_mod_unregister(oplus_dev->usb_ocm);
	platform_set_drvdata(pdev, NULL);
	return 0;
}

static const struct of_device_id oplus_chg_intf_match[] = {
	{ .compatible = "oplus_chg_intf" },
	{},
};

static struct platform_driver oplus_chg_intf = {
	.driver = {
		.name = "oplus-chg-intf",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(oplus_chg_intf_match),
	},
	.probe = oplus_chg_intf_probe,
	.remove = oplus_chg_intf_remove,
};

static __init int oplus_chg_intf_init(void)
{
	return platform_driver_register(&oplus_chg_intf);
}

static __exit void oplus_chg_intf_exit(void)
{
	platform_driver_unregister(&oplus_chg_intf);
}

oplus_chg_module_register(oplus_chg_intf);
