// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2020 Oplus. All rights reserved.
 */

#define pr_fmt(fmt) "OPLUS_CHG[CORE]: %s[%d]: " fmt, __func__, __LINE__

#include <linux/ctype.h>
#include <linux/device.h>
#ifdef CONFIG_OPLUS_CHG_OOS
#include <linux/oem/oplus_chg.h>
#else
#include <linux/oplus_chg.h>
#endif
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/power_supply.h>

#define OPLUS_CHG_MOD_ATTR(_name)					\
{									\
	.attr = { .name = #_name },					\
	.show = oplus_chg_mod_show_property,				\
	.store = oplus_chg_mod_store_property,				\
}

#define OPLUS_CHG_MOD_EXTERN_ATTR(_name)				\
{									\
	.attr = { .name = #_name },					\
	.show = oplus_chg_mod_show_exten_property,			\
	.store = oplus_chg_mod_store_exten_property,			\
}

static struct device_attribute oplus_chg_mod_attrs[];

static const char * const oplus_chg_mod_type_text[] = {
	"Common", "USB", "Wireless", "Battery", "Mains"
};

static const char * const oplus_chg_mod_charge_type_text[] = {
	"Unknown", "N/A", "Trickle", "Fast", "Standard", "Adaptive", "Custom"
};

static const char * const oplus_chg_mod_status_text[] = {
	"Unknown", "Charging", "Discharging", "Not charging", "Full"
};

static const char * const oplus_chg_mod_usb_type_text[] = {
	"Unknown", "SDP", "DCP", "CDP", "ACA", "C",
	"PD", "PD_DRP", "PPS", "OCP", "QC2", "QC3",
	"WARP", "SWARP",
};

static const char * const oplus_chg_mod_wls_type_text[] = {
	"Unknown", "BPP", "EPP", "EPP-PLUS",
	"WARP", "SWARP", "PD-65W", "TRX",
};

static const char * const oplus_chg_mod_trx_status_text[] = {
	"enable", "charging", "disable",
};

static const char * const oplus_chg_mod_health_text[] = {
	"Unknown", "Good", "Overheat", "Dead", "Over voltage",
	"Unspecified failure", "Cold", "Watchdog timer expire",
	"Safety timer expire", "Over current", "Warm", "Cool", "Hot"
};

static const char * const oplus_chg_mod_technology_text[] = {
	"Unknown", "NiMH", "Li-ion", "Li-poly", "LiFe", "NiCd",
	"LiMn"
};

static const char * const oplus_chg_mod_temp_region_text[] = {
	"cold", "little-cold", "cool", "little-cool",
	"pre-normal", "normal", "warm", "hot", "invalid",
};

int ocm_to_psy_status[] = {
	POWER_SUPPLY_STATUS_UNKNOWN,
	POWER_SUPPLY_STATUS_CHARGING,
	POWER_SUPPLY_STATUS_DISCHARGING,
	POWER_SUPPLY_STATUS_NOT_CHARGING,
	POWER_SUPPLY_STATUS_FULL,
};
EXPORT_SYMBOL_GPL(ocm_to_psy_status);

int ocm_to_psy_charge_type[] = {
	POWER_SUPPLY_CHARGE_TYPE_UNKNOWN,
	POWER_SUPPLY_CHARGE_TYPE_NONE,
	POWER_SUPPLY_CHARGE_TYPE_TRICKLE,
	POWER_SUPPLY_CHARGE_TYPE_FAST,
	POWER_SUPPLY_CHARGE_TYPE_STANDARD,
	POWER_SUPPLY_CHARGE_TYPE_ADAPTIVE,
	POWER_SUPPLY_CHARGE_TYPE_CUSTOM,
};
EXPORT_SYMBOL_GPL(ocm_to_psy_charge_type);

int ocm_to_psy_health[] = {
	POWER_SUPPLY_HEALTH_UNKNOWN,
	POWER_SUPPLY_HEALTH_GOOD,
	POWER_SUPPLY_HEALTH_OVERHEAT,
	POWER_SUPPLY_HEALTH_DEAD,
	POWER_SUPPLY_HEALTH_OVERVOLTAGE,
	POWER_SUPPLY_HEALTH_UNSPEC_FAILURE,
	POWER_SUPPLY_HEALTH_COLD,
	POWER_SUPPLY_HEALTH_WATCHDOG_TIMER_EXPIRE,
	POWER_SUPPLY_HEALTH_SAFETY_TIMER_EXPIRE,
	POWER_SUPPLY_HEALTH_OVERCURRENT,
	POWER_SUPPLY_HEALTH_WARM,
	POWER_SUPPLY_HEALTH_COOL,
	POWER_SUPPLY_HEALTH_HOT,
};
EXPORT_SYMBOL_GPL(ocm_to_psy_health);

int ocm_to_psy_technology[] = {
	POWER_SUPPLY_TECHNOLOGY_UNKNOWN,
	POWER_SUPPLY_TECHNOLOGY_NiMH,
	POWER_SUPPLY_TECHNOLOGY_LION,
	POWER_SUPPLY_TECHNOLOGY_LIPO,
	POWER_SUPPLY_TECHNOLOGY_LiFe,
	POWER_SUPPLY_TECHNOLOGY_NiCd,
	POWER_SUPPLY_TECHNOLOGY_LiMn,
};
EXPORT_SYMBOL_GPL(ocm_to_psy_technology);

int ocm_to_psy_scope[] = {
	POWER_SUPPLY_SCOPE_UNKNOWN,
	POWER_SUPPLY_SCOPE_SYSTEM,
	POWER_SUPPLY_SCOPE_DEVICE,
};
EXPORT_SYMBOL_GPL(ocm_to_psy_scope);

int ocm_to_psy_capacity_level[] = {
	POWER_SUPPLY_CAPACITY_LEVEL_UNKNOWN,
	POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL,
	POWER_SUPPLY_CAPACITY_LEVEL_LOW,
	POWER_SUPPLY_CAPACITY_LEVEL_NORMAL,
	POWER_SUPPLY_CAPACITY_LEVEL_HIGH,
	POWER_SUPPLY_CAPACITY_LEVEL_FULL,
};
EXPORT_SYMBOL_GPL(ocm_to_psy_capacity_level);

int ocm_to_psy_usb_type[] = {
	POWER_SUPPLY_USB_TYPE_UNKNOWN,
	POWER_SUPPLY_USB_TYPE_SDP,
	POWER_SUPPLY_USB_TYPE_DCP,
	POWER_SUPPLY_USB_TYPE_CDP,
	POWER_SUPPLY_USB_TYPE_ACA,
	POWER_SUPPLY_USB_TYPE_C,
	POWER_SUPPLY_USB_TYPE_PD,
	POWER_SUPPLY_USB_TYPE_PD_DRP,
	POWER_SUPPLY_USB_TYPE_PD_PPS,
	POWER_SUPPLY_USB_TYPE_APPLE_BRICK_ID,
	POWER_SUPPLY_USB_TYPE_DCP,
	POWER_SUPPLY_USB_TYPE_DCP,
	POWER_SUPPLY_USB_TYPE_DCP,
	POWER_SUPPLY_USB_TYPE_DCP,
};
EXPORT_SYMBOL_GPL(ocm_to_psy_usb_type);

static ssize_t oplus_chg_mod_show_property(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	ssize_t ret;
	struct oplus_chg_mod *ocm = dev_get_drvdata(dev);
	enum oplus_chg_mod_property ocm_prop = attr - oplus_chg_mod_attrs;
	union oplus_chg_mod_propval value;

	if (ocm_prop == OPLUS_CHG_PROP_TYPE) {
		value.intval = ocm->desc->type;
	} else {
		ret = oplus_chg_mod_get_property(ocm, ocm_prop, &value);

		if (ret < 0) {
			if (ret == -ENODATA)
				pr_debug("driver has no data for `%s' property\n",
					attr->attr.name);
			else if (ret != -ENODEV && ret != -EAGAIN)
				dev_err_ratelimited(dev,
					"driver failed to report `%s' property: %zd\n",
					attr->attr.name, ret);
			return ret;
		}
	}

	switch (ocm_prop) {
	case OPLUS_CHG_PROP_TYPE:
		ret = sprintf(buf, "%s\n",
			      oplus_chg_mod_type_text[value.intval]);
		break;
	case OPLUS_CHG_PROP_STATUS:
		ret = sprintf(buf, "%s\n",
			      oplus_chg_mod_status_text[value.intval]);
		break;
	case OPLUS_CHG_PROP_CHARGE_TYPE:
		ret = sprintf(buf, "%s\n",
			      oplus_chg_mod_charge_type_text[value.intval]);
		break;
	case OPLUS_CHG_PROP_HEALTH:
		ret = sprintf(buf, "%s\n",
			      oplus_chg_mod_health_text[value.intval]);
		break;
	case OPLUS_CHG_PROP_TECHNOLOGY:
		ret = sprintf(buf, "%s\n",
			      oplus_chg_mod_technology_text[value.intval]);
		break;
	case OPLUS_CHG_PROP_USB_TYPE:
		ret = sprintf(buf, "%s\n",
			      oplus_chg_mod_usb_type_text[value.intval]);
		break;
	case OPLUS_CHG_PROP_WLS_TYPE:
		ret = sprintf(buf, "%s\n",
			      oplus_chg_mod_wls_type_text[value.intval]);
		break;
	case OPLUS_CHG_PROP_TRX_STATUS:
		ret = sprintf(buf, "%s\n",
			      oplus_chg_mod_trx_status_text[value.intval]);
		break;
	case OPLUS_CHG_PROP_TEMP_REGION:
		ret = sprintf(buf, "%s\n",
			      oplus_chg_mod_temp_region_text[value.intval]);
		break;
	case OPLUS_CHG_PROP_MODEL_NAME:
		ret = sprintf(buf, "%s\n", value.strval);
		break;
	case OPLUS_CHG_PROP_ADAPTER_SID:
		ret = sprintf(buf, "%u\n", (unsigned int)value.intval);
		break;
	default:
		ret = sprintf(buf, "%d\n", value.intval);
	}

	return ret;
}

static ssize_t oplus_chg_mod_store_property(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t count)
{
	ssize_t ret;
	struct oplus_chg_mod *ocm = dev_get_drvdata(dev);
	enum oplus_chg_mod_property ocm_prop = attr - oplus_chg_mod_attrs;
	union oplus_chg_mod_propval value;

	switch (ocm_prop) {
	default:
		ret = -EINVAL;
	}

	/*
	 * If no match was found, then check to see if it is an integer.
	 * Integer values are valid for enums in addition to the text value.
	 */
	if (ret < 0) {
		long long_val;

		ret = kstrtol(buf, 10, &long_val);
		if (ret < 0)
			return ret;

		ret = long_val;
	}

	value.intval = ret;

	ret = oplus_chg_mod_set_property(ocm, ocm_prop, &value);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t oplus_chg_mod_show_exten_property(struct device *dev,
					  struct device_attribute *attr,
					  char *buf) {
	ssize_t ret = -ENODEV;
	struct oplus_chg_mod *ocm = dev_get_drvdata(dev);
	enum oplus_chg_mod_property ocm_prop = attr - oplus_chg_mod_attrs;
	int i;

	for (i = 0; i < ocm->desc->num_exten_properties; i++) {
		if ((ocm_prop == ocm->desc->exten_properties[i].exten_prop) ||
		    (ocm->desc->exten_properties[i].show != NULL))
			ret = ocm->desc->exten_properties[i].show(dev, attr, buf);
	}

	return ret;
}

static ssize_t oplus_chg_mod_store_exten_property(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t count) {
	ssize_t ret = -ENODEV;
	struct oplus_chg_mod *ocm = dev_get_drvdata(dev);
	enum oplus_chg_mod_property ocm_prop = attr - oplus_chg_mod_attrs;
	int i;

	for (i = 0; i < ocm->desc->num_exten_properties; i++) {
		if ((ocm_prop == ocm->desc->exten_properties[i].exten_prop) &&
		    (ocm->desc->exten_properties[i].store != NULL))
			ret = ocm->desc->exten_properties[i].store(dev, attr, buf, count);
	}

	if (ret < 0)
		return ret;

	return count;
}

static struct device_attribute oplus_chg_mod_attrs[] = {
	OPLUS_CHG_MOD_ATTR(type),
	OPLUS_CHG_MOD_ATTR(status),
	OPLUS_CHG_MOD_ATTR(online),
	OPLUS_CHG_MOD_ATTR(present),
	OPLUS_CHG_MOD_ATTR(voltage_now),
	OPLUS_CHG_MOD_ATTR(voltage_max),
	OPLUS_CHG_MOD_ATTR(voltage_min),
	OPLUS_CHG_MOD_ATTR(current_now),
	OPLUS_CHG_MOD_ATTR(current_max),
	OPLUS_CHG_MOD_ATTR(input_current_now),
	OPLUS_CHG_MOD_ATTR(usb_type),
	OPLUS_CHG_MOD_ATTR(fastchg_status),
	OPLUS_CHG_MOD_ATTR(adapter_sid),
	OPLUS_CHG_MOD_ATTR(adapter_type),
	OPLUS_CHG_MOD_ATTR(temp_region),
	OPLUS_CHG_MOD_ATTR(con_temp1),
	OPLUS_CHG_MOD_ATTR(con_temp2),
	OPLUS_CHG_MOD_ATTR(chg_enable),
	OPLUS_CHG_MOD_ATTR(otg_mode),
	OPLUS_CHG_MOD_ATTR(max_w_power),
	OPLUS_CHG_MOD_ATTR(trx_voltage_now),
	OPLUS_CHG_MOD_ATTR(trx_current_now),
	OPLUS_CHG_MOD_ATTR(trx_status),
	OPLUS_CHG_MOD_ATTR(trx_online),
	OPLUS_CHG_MOD_ATTR(wireless_type),
	OPLUS_CHG_MOD_ATTR(deviated),
	OPLUS_CHG_MOD_ATTR(force_type),
	OPLUS_CHG_MOD_ATTR(status_delay),
	OPLUS_CHG_MOD_ATTR(path_ctrl),
	OPLUS_CHG_MOD_ATTR(quiet_mode),
	OPLUS_CHG_MOD_ATTR(vrect_now),
	OPLUS_CHG_MOD_ATTR(trx_power_en),
	OPLUS_CHG_MOD_ATTR(trx_power_vol),
	OPLUS_CHG_MOD_ATTR(trx_power_curr_limit),
	OPLUS_CHG_MOD_ATTR(capacity),
	OPLUS_CHG_MOD_ATTR(real_capacity),
	OPLUS_CHG_MOD_ATTR(charge_type),
	OPLUS_CHG_MOD_ATTR(cell_num),
	OPLUS_CHG_MOD_ATTR(model_name),
	OPLUS_CHG_MOD_ATTR(temp),
	OPLUS_CHG_MOD_ATTR(health),
	OPLUS_CHG_MOD_ATTR(technology),
	OPLUS_CHG_MOD_ATTR(cycle_count),
	OPLUS_CHG_MOD_ATTR(voltage_ocv),
	OPLUS_CHG_MOD_ATTR(charge_control_limit),
	OPLUS_CHG_MOD_ATTR(charge_control_limit_max),
	OPLUS_CHG_MOD_ATTR(charge_counter),
	OPLUS_CHG_MOD_ATTR(charge_full_design),
	OPLUS_CHG_MOD_ATTR(charge_full),
	OPLUS_CHG_MOD_ATTR(time_to_full_avg),
	OPLUS_CHG_MOD_ATTR(time_to_full_now),
	OPLUS_CHG_MOD_ATTR(time_tto_empty_avg),
	OPLUS_CHG_MOD_ATTR(power_now),
	OPLUS_CHG_MOD_ATTR(power_avg),
	OPLUS_CHG_MOD_ATTR(capacity_level),
	OPLUS_CHG_MOD_ATTR(ship_mode),
	OPLUS_CHG_MOD_ATTR(factory_mode),
	OPLUS_CHG_MOD_ATTR(tx_power),
	OPLUS_CHG_MOD_ATTR(rx_power),
	OPLUS_CHG_MOD_ATTR(voltage_now_cell1),
	OPLUS_CHG_MOD_ATTR(voltage_now_cell2),
	OPLUS_CHG_MOD_ATTR(mmi_charging_enable),
	OPLUS_CHG_MOD_ATTR(typec_cc_orientation),
	OPLUS_CHG_MOD_ATTR(hw_detect),
	OPLUS_CHG_MOD_ATTR(fod_cal),
	OPLUS_CHG_MOD_ATTR(skin_temp),
	OPLUS_CHG_MOD_ATTR(batt_chg_enable),
	OPLUS_CHG_MOD_ATTR(online_keep),
	OPLUS_CHG_MOD_ATTR(connect_disable),
	OPLUS_CHG_MOD_ATTR(remaining_capacity),
	OPLUS_CHG_MOD_ATTR(call_on),
	OPLUS_CHG_MOD_ATTR(camera_on),
	OPLUS_CHG_MOD_ATTR(otg_switch),
	OPLUS_CHG_MOD_ATTR(battery_notify_code),
#ifdef OPLUS_CHG_REG_DUMP_ENABLE
	OPLUS_CHG_MOD_ATTR(reg_dump),
#endif
	OPLUS_CHG_MOD_ATTR(status_keep),
#ifndef CONFIG_OPLUS_CHG_OOS
	OPLUS_CHG_MOD_ATTR(authenticate),
	OPLUS_CHG_MOD_ATTR(battery_cc),
	OPLUS_CHG_MOD_ATTR(battery_fcc),
	OPLUS_CHG_MOD_ATTR(battery_rm),
	OPLUS_CHG_MOD_ATTR(battery_soh),
#ifdef CONFIG_OPLUS_CALL_MODE_SUPPORT
	OPLUS_CHG_MOD_ATTR(call_mode),
#endif
	OPLUS_CHG_MOD_ATTR(charge_technology),
#ifdef CONFIG_OPLUS_CHIP_SOC_NODE
	OPLUS_CHG_MOD_ATTR(chip_soc),
#endif
#ifdef CONFIG_OPLUS_SMART_CHARGER_SUPPORT
	OPLUS_CHG_MOD_ATTR(cool_down),
#endif
	OPLUS_CHG_MOD_ATTR(fast_charge),
#ifdef CONFIG_OPLUS_SHORT_C_BATT_CHECK
#ifdef CONFIG_OPLUS_SHORT_USERSPACE
	OPLUS_CHG_MOD_ATTR(short_c_limit_chg),
	OPLUS_CHG_MOD_ATTR(short_c_limit_rechg),
	OPLUS_CHG_MOD_ATTR(charge_term_current),
	OPLUS_CHG_MOD_ATTR(input_current_settled),
#endif
#endif
#ifdef CONFIG_OPLUS_SHORT_HW_CHECK
	OPLUS_CHG_MOD_ATTR(short_c_hw_feature),
	OPLUS_CHG_MOD_ATTR(short_c_hw_status),
#endif
#ifdef CONFIG_OPLUS_SHORT_IC_CHECK
	OPLUS_CHG_MOD_ATTR(short_ic_otp_status),
	OPLUS_CHG_MOD_ATTR(short_ic_volt_thresh),
	OPLUS_CHG_MOD_ATTR(short_ic_otp_value),
#endif
	OPLUS_CHG_MOD_ATTR(voocchg_ing),
	OPLUS_CHG_MOD_ATTR(otg_online),
	OPLUS_CHG_MOD_ATTR(usb_status),
	OPLUS_CHG_MOD_ATTR(fast_chg_type),
	OPLUS_CHG_MOD_ATTR(usbtemp_volt_l),
	OPLUS_CHG_MOD_ATTR(usbtemp_volt_r),
	OPLUS_CHG_MOD_ATTR(tx_voltage_now),
	OPLUS_CHG_MOD_ATTR(tx_current_now),
	OPLUS_CHG_MOD_ATTR(cp_voltage_now),
	OPLUS_CHG_MOD_ATTR(cp_current_now),
	OPLUS_CHG_MOD_ATTR(wireless_mode),
	OPLUS_CHG_MOD_ATTR(color_wls_type),
	OPLUS_CHG_MOD_ATTR(cep_info),
	OPLUS_CHG_MOD_ATTR(real_type),
	OPLUS_CHG_MOD_ATTR(charge_now),
#endif /* CONFIG_OPLUS_CHG_OOS */

	/* extended attr */
	OPLUS_CHG_MOD_EXTERN_ATTR(upgrade_firmware),
	OPLUS_CHG_MOD_EXTERN_ATTR(charge_parameter),
	OPLUS_CHG_MOD_EXTERN_ATTR(voltage_now_cell),
	OPLUS_CHG_MOD_EXTERN_ATTR(path_current),
	OPLUS_CHG_MOD_EXTERN_ATTR(ftm_test),
	OPLUS_CHG_MOD_EXTERN_ATTR(mutual_cmd),
	OPLUS_CHG_MOD_EXTERN_ATTR(aging_ffc_data),
};

static struct attribute *
__oplus_chg_mod_attrs[ARRAY_SIZE(oplus_chg_mod_attrs) + 1];

static umode_t oplus_chg_mod_attr_is_visible(struct kobject *kobj,
					   struct attribute *attr,
					   int attrno)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct oplus_chg_mod *ocm = dev_get_drvdata(dev);
	umode_t mode = S_IRUSR | S_IRGRP | S_IROTH;
	int i;

	if (attrno == OPLUS_CHG_PROP_TYPE) {
		return mode;
	} else if (attrno < OPLUS_CHG_PROP_MAX) {
		for (i = 0; i < ocm->desc->num_properties; i++) {
			int property = ocm->desc->properties[i];

			if (property == attrno) {
				if (ocm->desc->property_is_writeable &&
				ocm->desc->property_is_writeable(ocm, property) > 0)
					mode |= S_IWUSR;

				return mode;
			}
		}
	} else {
		for (i = 0; i < ocm->desc->num_exten_properties; i++) {
			int property = ocm->desc->exten_properties[i].exten_prop;

			if (property == attrno) {
				if (ocm->desc->property_is_writeable &&
				ocm->desc->property_is_writeable(ocm, property) > 0)
					mode |= S_IWUSR;

				return mode;
			}
		}
	}

	return 0;
}

static struct attribute_group oplus_chg_mod_attr_group = {
	.attrs = __oplus_chg_mod_attrs,
	.is_visible = oplus_chg_mod_attr_is_visible,
};

static const struct attribute_group *oplus_chg_mod_attr_groups[] = {
	&oplus_chg_mod_attr_group,
	NULL,
};

void oplus_chg_mod_init_attrs(struct device_type *dev_type)
{
	int i;

	dev_type->groups = oplus_chg_mod_attr_groups;

	for (i = 0; i < ARRAY_SIZE(oplus_chg_mod_attrs); i++)
		__oplus_chg_mod_attrs[i] = &oplus_chg_mod_attrs[i].attr;
}

static char *kstruprdup(const char *str, gfp_t gfp)
{
	char *ret, *ustr;

	ustr = ret = kmalloc(strlen(str) + 1, gfp);

	if (!ret)
		return NULL;

	while (*str)
		*ustr++ = toupper(*str++);

	*ustr = 0;

	return ret;
}

int oplus_chg_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	struct oplus_chg_mod *ocm = dev_get_drvdata(dev);
	int ret = 0, j;
	char *prop_buf;
	char *attrname;

	if (!ocm || !ocm->desc) {
		pr_debug("No oplus chg mod yet\n");
		return ret;
	}

	ret = add_uevent_var(env, "OPLUS_CHG_MOD=%s", ocm->desc->name);
	if (ret)
		return ret;

	prop_buf = (char *)get_zeroed_page(GFP_KERNEL);
	if (!prop_buf)
		return -ENOMEM;

	for (j = 0; j < ocm->desc->uevent_num_properties; j++) {
		struct device_attribute *attr;
		char *line;

		attr = &oplus_chg_mod_attrs[ocm->desc->uevent_properties[j]];

		ret = oplus_chg_mod_show_property(dev, attr, prop_buf);
		if (ret == -ENODEV || ret == -ENODATA) {
			/* When a battery is absent, we expect -ENODEV. Don't abort;
			   send the uevent with at least the the PRESENT=0 property */
			ret = 0;
			continue;
		}

		if (ret < 0)
			goto out;

		line = strchr(prop_buf, '\n');
		if (line)
			*line = 0;

		attrname = kstruprdup(attr->attr.name, GFP_KERNEL);
		if (!attrname) {
			ret = -ENOMEM;
			goto out;
		}

		ret = add_uevent_var(env, "OPLUS_CHG_%s=%s", attrname, prop_buf);
		kfree(attrname);
		if (ret)
			goto out;
	}

out:
	free_page((unsigned long)prop_buf);

	return ret;
}
