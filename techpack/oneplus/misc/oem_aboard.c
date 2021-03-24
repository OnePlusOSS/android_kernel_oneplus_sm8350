/* For OEM project information
 *such as project name, hardware ID
 */

#include <linux/export.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/sys_soc.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/oem/project_info.h>
#include <linux/soc/qcom/smem.h>
#include <linux/gpio.h>
#include <soc/qcom/socinfo.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/machine.h>
#include <linux/soc/qcom/smem.h>
#include <linux/pstore.h>
#include <linux/oem/device_info.h>
#define WRONG_VERSION 99
#define MAX_ABOARD_VERSION 9
static struct project_info *project_info_desc;

int a_board_val = 0;
int gpio_valueh_0 = 0;
int gpio_valueh_1 = 0;
int gpio_valuel_0 = 0;
int gpio_valuel_1 = 0;

static int op_aboard_read_gpio(int gpionumber);

static char mainboard_manufacture[8] = {'O',
    'N', 'E', 'P', 'L', 'U', 'S', '\0'};
static char Aboard_version[20] = {0};
struct a_board_version{

       int  version;
       char   name[20];
};
struct a_board_version a_board_version_string_arry_gpio[MAX_ABOARD_VERSION]={

    {11,   "NULL"},
    {22,   "QAT5515"},
    {12,   "QAT3555"},
    {WRONG_VERSION,   ""},
};
struct aboard_data {
    int aboard_gpio_0;
    int aboard_gpio_1;
    int support_aboard_gpio_0;
    int support_aboard_gpio_1;
    struct pinctrl                      *pinctrl;
    struct pinctrl_state                *pinctrl_state_active;
    struct pinctrl_state                *pinctrl_state_sleep;
    struct pinctrl_state                *pinctrl_state_suspend;
    struct device *dev;
};
static struct aboard_data *data;

int tri_state_value(int valueh, int valuel){
    return ((valueh != valuel) ? 1 : ((valueh == 0) ? 2 : 3));
}

static int op_aboard_request_named_gpio(const char *label, int *gpio)
{
    struct device *dev = data->dev;
    struct device_node *np = dev->of_node;
    int rc = of_get_named_gpio(np, label, 0);

    if (rc < 0) {
        dev_err(dev, "failed to get '%s'\n", label);
        *gpio = rc;
        return rc;
    }
    *gpio = rc;

    rc = devm_gpio_request(dev, *gpio, label);
    if (rc) {
        dev_err(dev, "failed to request gpio %d\n", *gpio);
        return rc;
    }

    dev_info(dev, "%s gpio: %d\n", label, *gpio);
    return 0;
}

static int op_aboard_read_gpio(int gpionumber)
{
    int gpio = 0;

    if(gpionumber == 0) {
        if(data->support_aboard_gpio_0 == 1)
            gpio = gpio_get_value(data->aboard_gpio_0);
    } else if (gpionumber == 1) {
        if(data->support_aboard_gpio_1 == 1)
            gpio = gpio_get_value(data->aboard_gpio_1);
    } else {
	pr_err("input wrong gpio number: %d\n", gpionumber);
	return 0;
    }
    return gpio;
}

static int oem_aboard_probe(struct platform_device *pdev)
{
    int rc = 0;
    int i = 0;
    size_t size;
    struct device *dev = &pdev->dev;

    data = kzalloc(sizeof(struct aboard_data), GFP_KERNEL);
    if (!data) {
        pr_err("%s: failed to allocate memory\n", __func__);
        rc = -ENOMEM;
        goto exit;
    }

    data->dev = dev;
    project_info_desc = qcom_smem_get(QCOM_SMEM_HOST_ANY,SMEM_PROJECT_INFO, &size);

    if ( data == NULL || IS_ERR_OR_NULL(project_info_desc))
    {
        pr_err("%s: get project_info failure\n", __func__);
        return -1 ;
    }
    rc = op_aboard_request_named_gpio("oem,aboard-gpio-0",&data->aboard_gpio_0);
    if (rc) {
        pr_err("%s: oem,aboard-gpio-0 fail\n", __func__);
    }else{
       data->support_aboard_gpio_0 = 1;
    }

    rc = op_aboard_request_named_gpio("oem,aboard-gpio-1",&data->aboard_gpio_1);
    if (rc) {
        pr_err("%s: oem,aboard-gpio-1 fail\n", __func__);
    }else{
       data->support_aboard_gpio_1 = 1;
    }

    data->pinctrl = devm_pinctrl_get((data->dev));
    if (IS_ERR_OR_NULL(data->pinctrl)) {
        rc = PTR_ERR(data->pinctrl);
        pr_err("%s pinctrl error!\n",__func__);
        goto err_pinctrl_get;
    }

    data->pinctrl_state_active = pinctrl_lookup_state(data->pinctrl, "oem_aboard_active");

    if (IS_ERR_OR_NULL(data->pinctrl_state_active)) {
        rc = PTR_ERR(data->pinctrl_state_active);
        pr_err("%s pinctrl state active error!\n",__func__);
        goto err_pinctrl_lookup;
    }

    if (data->pinctrl) {
        rc = pinctrl_select_state(data->pinctrl,data->pinctrl_state_active);
    }
    if(data->support_aboard_gpio_0 == 1)
        gpio_direction_input(data->aboard_gpio_0);
    if(data->support_aboard_gpio_1 == 1)
        gpio_direction_input(data->aboard_gpio_1);
    gpio_valueh_0 = op_aboard_read_gpio(0);
    gpio_valueh_1 = op_aboard_read_gpio(1);

    data->pinctrl_state_suspend = pinctrl_lookup_state(data->pinctrl, "oem_aboard_suspend");

    if (IS_ERR_OR_NULL(data->pinctrl_state_suspend)) {
        rc = PTR_ERR(data->pinctrl_state_suspend);
        pr_err("%s pinctrl state suspend error!\n",__func__);
        goto err_pinctrl_lookup;
    }

    if (data->pinctrl) {
        rc = pinctrl_select_state(data->pinctrl,data->pinctrl_state_suspend);
    }
    if(data->support_aboard_gpio_0 == 1)
        gpio_direction_input(data->aboard_gpio_0);
    if(data->support_aboard_gpio_1 == 1)
        gpio_direction_input(data->aboard_gpio_1);
    gpio_valuel_0 = op_aboard_read_gpio(0);
    gpio_valuel_1 = op_aboard_read_gpio(1);

    data->pinctrl_state_sleep = pinctrl_lookup_state(data->pinctrl, "oem_aboard_sleep");

    if (data->pinctrl && !IS_ERR_OR_NULL(data->pinctrl_state_sleep)) {
        rc = pinctrl_select_state(data->pinctrl,data->pinctrl_state_sleep);
    }
    a_board_val = 10 * tri_state_value(gpio_valueh_0,gpio_valuel_0) + tri_state_value(gpio_valueh_1,gpio_valuel_1);
    for (i = 0;i < MAX_ABOARD_VERSION;i++) {
	if (a_board_version_string_arry_gpio[i].version == a_board_val) {
	    snprintf(Aboard_version, sizeof(Aboard_version), "%d %s",
	    a_board_val, a_board_version_string_arry_gpio[i].name);

	    push_component_info(ABOARD, Aboard_version, mainboard_manufacture);
            break;
	} else if (a_board_version_string_arry_gpio[i].version == WRONG_VERSION) {
	    pr_err("No match aboard name!\n");
	    break;
	}
    }
    pr_err("%s: probe finish!\n", __func__);
    return 0;

err_pinctrl_lookup:
    devm_pinctrl_put(data->pinctrl);
err_pinctrl_get:
    data->pinctrl = NULL;
    kfree(data);
exit:
    pr_err("%s: probe Fail!\n", __func__);

    return rc;
}

static const struct of_device_id aboard_of_match[] = {
    { .compatible = "oem,aboard", },
    {}
};
MODULE_DEVICE_TABLE(of, aboard_of_match);

static struct platform_driver aboard_driver = {
    .driver = {
        .name       = "op_aboard",
        .owner      = THIS_MODULE,
        .of_match_table = aboard_of_match,
    },
    .probe = oem_aboard_probe,
};

static int __init oem_aboard_init(void)
{
    int ret;

    ret = platform_driver_register(&aboard_driver);
    if (ret)
        pr_err("aboard_driver register failed: %d\n", ret);

    return ret;
}

static void __exit oem_aboard_exit(void)
{
    pr_err("%s exit\n", __func__);
}

MODULE_LICENSE("GPL v2");
module_init(oem_aboard_init);
module_exit(oem_aboard_exit);
