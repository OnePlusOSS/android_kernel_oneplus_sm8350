// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2020 Oplus. All rights reserved.
 */

#define pr_fmt(fmt) "OPLUS_CHG[PMIC]: %s[%d]: " fmt, __func__, __LINE__

#include <linux/delay.h>
#ifdef CONFIG_OPLUS_CHG_OOS
#include <linux/oem/oplus_chg.h>
#else
#include <linux/oplus_chg.h>
#endif
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/device.h>
#include <linux/spmi.h>
#include <linux/regmap.h>
#include "../oplus_charger.h"
#include "../oplus_gauge.h"
#include "../oplus_warp.h"
#include "../oplus_short.h"
#include "../charger_ic/oplus_short_ic.h"
#include "../oplus_adapter.h"
#include "../oplus_chg_module.h"
#include "../oplus_chg_ic.h"

#define DCDC_BASE	0X2700
#define USBIN_BASE	0X2900
#define TYPEC_BASE	0X2B00

#define INT_RT_STS_OFFSET	0x10

#define USBIN_CMD_IL_REG			(USBIN_BASE + 0x54)
#define USBIN_SUSPEND_BIT			BIT(0)

#define DCDC_BST_EN_REG				(DCDC_BASE + 0x50)
#define OTG_EN_BIT				BIT(0)

#define USBIN_PLUGIN_RT_STS_BIT			BIT(0)

#define TYPE_C_MISC_STATUS_REG			(TYPEC_BASE + 0x0B)
#define TYPEC_WATER_DETECTION_STATUS_BIT	BIT(7)
#define SNK_SRC_MODE_BIT			BIT(6)
#define TYPEC_VBUS_ERROR_STATUS_BIT		BIT(4)
#define TYPEC_TCCDEBOUNCE_DONE_STATUS_BIT	BIT(3)
#define CC_ORIENTATION_BIT			BIT(1)
#define CC_ATTACHED_BIT				BIT(0)

#define TYPE_C_CID_STATUS_REG			(TYPEC_BASE + 0x81)
#define CID_STATUS_BIT					BIT(0)

struct pm8350b_typec_dev {
	struct device *dev;
	struct oplus_chg_ic_dev *ic_dev;
	struct regmap *regmap;
	int		ext1_en;
	struct delayed_work	get_regmap_work;

	struct oplus_chg_mod *usb_ocm;
};

static bool is_usb_ocm_available(struct pm8350b_typec_dev *dev)
{
	dev->usb_ocm = oplus_chg_mod_get_by_name("usb");
	return !!dev->usb_ocm;
}

int oplus_reg_read(struct pm8350b_typec_dev *chip, u16 addr, u8 *val)
{
	unsigned int value;
	int rc = 0;

	if (chip == NULL) {
		pr_err("pm8350b_typec_dev is NULL");
		return -ENODEV;
	}

	if (chip->regmap != NULL) {
		rc = regmap_read(chip->regmap, addr, &value);
		if (rc >= 0)
			*val = (u8)value;
	}

	return rc;
}

/*
int oplus_reg_write(struct pm8350b_typec_dev *chip, u16 addr, u8 val)
{
	return regmap_write(chip->regmap, addr, val);
}
*/

int oplus_masked_write(struct pm8350b_typec_dev *chip, u16 addr, u8 mask, u8 val)
{
	if (chip == NULL) {
		pr_err("pm8350b_typec_dev is NULL");
		return -ENODEV;
	}

	if (chip->regmap != NULL)
		return regmap_update_bits(chip->regmap, addr, mask, val);
	else
		return -ENODEV;
}

static int oplus_get_vbus_status(struct oplus_chg_ic_dev *dev,
					 int *vbus_rising)
{
	u8 stat;
	struct pm8350b_typec_dev *chip;
	int rc = 0;

	if (dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	chip = oplus_chg_ic_get_drvdata(dev);

	rc = oplus_reg_read(chip, USBIN_BASE + INT_RT_STS_OFFSET, &stat);
	if (rc < 0) {
		pr_err("Couldn't read USB_INT_RT_STS rc=%d\n", rc);
		return rc;
	}

	if (stat & USBIN_PLUGIN_RT_STS_BIT) {
		*vbus_rising = (bool)(stat & USBIN_PLUGIN_RT_STS_BIT);
	} else
		*vbus_rising = 0;

	return rc;
}

static int oplus_get_prop_typec_cc_orientation(struct oplus_chg_ic_dev *dev,
					 int *orientation)
{
	u8 stat;
	struct pm8350b_typec_dev *chip;
	int rc = 0;

	if (dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	chip = oplus_chg_ic_get_drvdata(dev);

	rc = oplus_reg_read(chip, TYPE_C_MISC_STATUS_REG, &stat);
	if (rc < 0) {
		pr_err("Couldn't read TYPE_C_STATUS_4 rc=%d\n", rc);
		return rc;
	}
	pr_err("TYPE_C_STATUS_4 = 0x%02x\n", stat);

	if (stat & CC_ATTACHED_BIT) {
		*orientation = (bool)(stat & CC_ORIENTATION_BIT) + 1;
	} else
		*orientation = 0;

	return rc;
}

static int oplus_otg_enable(struct oplus_chg_ic_dev *dev, bool enable)
{
	struct pm8350b_typec_dev *chip;

	if (dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	chip = oplus_chg_ic_get_drvdata(dev);
	if (!gpio_is_valid(chip->ext1_en)) {
		pr_err("%s, OTG ctrl gpio invalid!!", __func__);
		return -ENODEV;
	}
	if (enable) {
		if (is_usb_ocm_available(chip))
			oplus_chg_global_event(chip->usb_ocm,
				OPLUS_CHG_EVENT_OTG_ENABLE);
		msleep(50);
		gpio_set_value(chip->ext1_en, 1);
		pr_err("%s, Enable otg switch successfully!!", __func__);
	} else {
		gpio_set_value(chip->ext1_en, 0);
		msleep(50);
		if (is_usb_ocm_available(chip))
			oplus_chg_global_event(chip->usb_ocm,
				OPLUS_CHG_EVENT_OTG_DISABLE);
		pr_err("%s, Disable otg switch successfully!!", __func__);
	}

	return 0;
}

static int oplus_otg_gpio_request(struct pm8350b_typec_dev *chip)
{
	int rc;

	if (gpio_is_valid(chip->ext1_en)) {
		rc = gpio_request(chip->ext1_en, "Ext1En");
		if (rc) {
			pr_err("gpio_request failed for ext1_en rc=%d\n", rc);
			return -EINVAL;
		}
		gpio_direction_output(chip->ext1_en, 0);

		return 0;
	} else {
		pr_err("ext1_en not specified\n");
		return -EINVAL;
	}
}

 static int oplus_get_prop_hw_detect(struct oplus_chg_ic_dev *dev, int *hw_detect)
{
	u8 stat;
	struct pm8350b_typec_dev *chip;
	int rc = 0;

	if (dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(dev);

	rc = oplus_reg_read(chip, TYPE_C_CID_STATUS_REG, &stat);
	if (rc < 0) {
		pr_err("Couldn't read TYPE_C_CID_STATUS_REG rc=%d\n", rc);
		return rc;
	}
	pr_debug("TYPE_C_CID_STATUS_REG = 0x%02x\n", stat);

	if (stat & CID_STATUS_BIT) {
		*hw_detect = (bool)(stat & CID_STATUS_BIT);
	} else
		*hw_detect = 0;

	return rc;
}

#ifdef OPLUS_CHG_REG_DUMP_ENABLE
static int pm8350b_reg_dump(struct oplus_chg_ic_dev *dev)
{
	struct pm8350b_typec_dev *chip;
	unsigned char *data_buf = NULL;
	int rc = 0;

#define REG_DUMP_START 0x2600
#define REG_DUMP_END   0x2dff
#define REG_DUMP_SIZE  (REG_DUMP_END - REG_DUMP_START + 1)

	if (dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(dev);

	if (chip->regmap == NULL) {
		pr_err("pm8350b regmap not found\n");
		return -ENODEV;
	}

	data_buf = kzalloc(REG_DUMP_SIZE, GFP_KERNEL);
	if (data_buf == NULL) {
		pr_err("memory allocation failed\n");
		return -ENOMEM;
	}

	rc = regmap_bulk_read(chip->regmap, REG_DUMP_START, data_buf, REG_DUMP_SIZE);
	if (rc < 0) {
		pr_err("pm8350b read reg errot, addr=0x%04x, len=%d, rc=%d\n",
			REG_DUMP_START, REG_DUMP_SIZE, rc);
		goto out;
	}

#ifdef OPLUS_CHG_REG_DUMP_ENABLE
	print_hex_dump(KERN_ERR, "OPLUS_CHG[PM8350B]: ", DUMP_PREFIX_OFFSET,
			32, 1, data_buf, REG_DUMP_SIZE, false);
#else
	print_hex_dump(KERN_DEBUG, "OPLUS_CHG[PM8350B]: ", DUMP_PREFIX_OFFSET,
			32, 1, data_buf, REG_DUMP_SIZE, false);
#endif
out:
	kfree(data_buf);
	return rc;
}
#endif

static struct oplus_chg_ic_typec_ops pm8350b_dev_ops = {
	.ic_ops = {
#ifdef OPLUS_CHG_REG_DUMP_ENABLE
		.reg_dump = pm8350b_reg_dump,
#endif
	},
	.typec_get_cc_orientation = oplus_get_prop_typec_cc_orientation,
	.otg_enable = oplus_otg_enable,
	.typec_get_hw_detect = oplus_get_prop_hw_detect,
	.typec_get_vbus_status = oplus_get_vbus_status,
};

static int dev_is_pm8350b(struct device *dev, void *name)
{
	if (dev->of_node != NULL && !strcmp(dev->of_node->name, name)) {
		pr_err("dev %s found!!\n", name);
		return 1;
	}

	return 0;
}
struct device *soc_find_pm8350b(struct device *soc_dev)
{
	char *path1[] = {"qcom,spmi-debug", "qcom,spmi-debug", "qcom,pm8350b-debug"};
	char *path2[] = {"qcom,pmic_glink_log", "qcom,spmi_glink_debug", "spmi", "qcom,pm8350b-debug"};
	struct device *dev;
	int i;

	dev = soc_dev;
	for (i = 0; i < (sizeof(path1)/sizeof(char *)); i++) {
		dev = device_find_child(dev, path1[i], dev_is_pm8350b);
		if (dev == NULL) {
			pr_err("dev %s not found!!", path1[i]);
			break;
		} else {
			pr_err("dev %s found!!", path1[i]);
		}
	}

	if (dev == NULL) {
		dev = soc_dev;
		for (i = 0; i < (sizeof(path2)/sizeof(char *)); i++) {
			dev = device_find_child(dev, path2[i], dev_is_pm8350b);
			if (dev == NULL) {
				pr_err("dev %s not found!!", path2[i]);
				return NULL;
			} else {
				pr_err("dev %s found!!", path2[i]);
			}
		}
	}

	pr_err("dev pm8350b-debug found!!\n");
	return dev;
}

static void oplus_get_regmap_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct pm8350b_typec_dev *chip = container_of(dwork,
				struct pm8350b_typec_dev, get_regmap_work);
	struct device *dev;
	struct device *soc_dev;
	static int count = 5;
	soc_dev = chip->dev->parent->parent->parent;

	dev = soc_find_pm8350b(soc_dev);
	if (dev == NULL) {
		if (count --) {
			pr_err("qcom,pm8350b-debug not found, retry count: %d\n", count);
			schedule_delayed_work(&chip->get_regmap_work, msecs_to_jiffies(1000));
		} else {
			pr_err("qcom,pm8350b-debug not found, retry done\n");
		}
		return;
	} else {
		chip->regmap = dev_get_regmap(dev, NULL);
		if (!chip->regmap) {
			pr_err("pm8350b regmap is missing\n");
			return;
		}
	}
}

static int oplus_chg_typec_driver_probe(struct platform_device *pdev)
{
	struct pm8350b_typec_dev *chip;
	struct device_node *node = pdev->dev.of_node;
	enum oplus_chg_ic_type ic_type;
	int ic_index;
	int rc;
	enum of_gpio_flags flags;

	chip = devm_kzalloc(&pdev->dev, sizeof(struct pm8350b_typec_dev), GFP_KERNEL);
	if (!chip) {
		pr_err("failed to allocate memory\n");
		return -ENOMEM;
	}

	chip->dev = &pdev->dev;
	platform_set_drvdata(pdev, chip);

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
	chip->ic_dev = devm_oplus_chg_ic_register(chip->dev, node->name, ic_index);
	if (chip->ic_dev == NULL) {
		rc = -ENODEV;
		pr_err("register %s error\n", node->name);
		goto reg_ic_err;
	}
	chip->ic_dev->dev_ops = &pm8350b_dev_ops;
	chip->ic_dev->type = ic_type;

	chip->ext1_en = of_get_named_gpio_flags(node, "oplus,ext1-otg-en", 0, &flags);
	if (!gpio_is_valid(chip->ext1_en)) {
		pr_err("ext1_en not specified\n");
		goto gpio_err;
	}
	rc = oplus_otg_gpio_request(chip);
	if (rc < 0) {
		pr_err("ext1_en request failed\n");
		goto gpio_err;
	}

	INIT_DELAYED_WORK(&chip->get_regmap_work, oplus_get_regmap_work);
	schedule_delayed_work(&chip->get_regmap_work, 0);

	pr_err("probe success\n");
	return 0;

gpio_err:
	devm_oplus_chg_ic_unregister(chip->dev, chip->ic_dev);
reg_ic_err:
    platform_set_drvdata(pdev, NULL);
	devm_kfree(&pdev->dev, chip);
	return rc;
}

static int oplus_chg_typec_driver_remove(struct platform_device *pdev)
{
	struct pm8350b_typec_dev *chip = platform_get_drvdata(pdev);

	if (!gpio_is_valid(chip->ext1_en))
		gpio_free(chip->ext1_en);
	devm_oplus_chg_ic_unregister(chip->dev, chip->ic_dev);
	platform_set_drvdata(pdev, NULL);
	devm_kfree(&pdev->dev, chip);

	return 0;
}

static const struct of_device_id oplus_chg_typec_match[] = {
	{ .compatible = "oplus,pm8350b-typec" },
	{},
};

static struct platform_driver oplus_chg_typec_driver = {
	.driver = {
		.name = "oplus_typec",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(oplus_chg_typec_match),
	},
	.probe = oplus_chg_typec_driver_probe,
	.remove = oplus_chg_typec_driver_remove,
};

static __init int oplus_chg_typec_driver_init(void)
{
	return platform_driver_register(&oplus_chg_typec_driver);
}

static __exit void oplus_chg_typec_driver_exit(void)
{
	platform_driver_unregister(&oplus_chg_typec_driver);
}

oplus_chg_module_register(oplus_chg_typec_driver);
