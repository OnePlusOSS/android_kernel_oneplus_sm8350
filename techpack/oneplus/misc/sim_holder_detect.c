#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/fs.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#define SIM_LOG_TAG             "SIM_HOLDER_DET"
#define SIM_DETECT_DIR          "simholder"
#define SIM_HOLDER_DETECT_NAME  "sim_holder_detect"
#define SIM_HOLDER_DET_GPIO     "qcom,sim-holder-det-gpio"

static struct of_device_id sim_holder_det_match[] = {
	{ .compatible = "oneplus,sim_holder_detect" },
	{},
};

struct sim_holder_dev {
	struct device *sim_dev;
	int sim_det_gpio;
	int sim_det_val;
};

static struct sim_holder_dev *g_sim_holder_data = NULL;
static struct proc_dir_entry *g_parent = NULL;
static int sim_det_gpio = -1;

static ssize_t proc_sim_holder_det_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
	int rc = 0;
	char page[25] = {0};
	struct inode *sim_inode = file_inode(file);
	struct sim_holder_dev *sim_holder_det_data = PDE_DATA(sim_inode);

	if (sim_holder_det_data == NULL) {
		pr_err("[%s][line:%d][%s] Get sim holder data error\n", SIM_LOG_TAG, __LINE__, __func__);
		return 0;
	}

	//pr_info("[%s][line:%d] sim_det_gpio: %d\n", SIM_LOG_TAG, __LINE__, sim_det_gpio);

	sim_holder_det_data->sim_det_val = gpio_get_value(sim_det_gpio);
	pr_info("[%s][line:%d] SIM card holder detect value: %d\n", SIM_LOG_TAG,
		__LINE__, sim_holder_det_data->sim_det_val);

	rc = snprintf(page, sizeof(page)-1, "%d\n", sim_holder_det_data->sim_det_val);
	rc = simple_read_from_buffer(user_buf, count, ppos, page, strlen(page));

	return rc;
}

static const struct file_operations sim_holder_det_ops = {
	.owner = THIS_MODULE,
	.open  = simple_open,
	.read  = proc_sim_holder_det_read,
};

static int simcard_holder_parse_dt(struct sim_holder_dev *const sim_holder_data)
{
	int rc = 0;
	struct device_node *node = NULL;
	struct sim_holder_dev *sim_holder_dev = sim_holder_data;

	//pr_info("[%s][line:%d] Enter Parse DTS...\n", SIM_LOG_TAG, __LINE__);

	node = sim_holder_dev->sim_dev->of_node;
	sim_holder_dev->sim_det_gpio = of_get_named_gpio(node, SIM_HOLDER_DET_GPIO, 0);
	if (sim_holder_dev->sim_det_gpio < 0) {
		pr_err("[%s][line:%d][%s] SIM holder detect gpio not specified\n", SIM_LOG_TAG, __LINE__, __func__);
		rc = -1;
		goto err;
	}

	if (gpio_is_valid(sim_holder_data->sim_det_gpio)) {
		pr_info("[%s][line:%d] SIM holder request GPIO: %d\n", SIM_LOG_TAG,
			__LINE__, sim_holder_data->sim_det_gpio);

		rc = gpio_request(sim_holder_data->sim_det_gpio, "sim-holder-det-gpio");
		if (rc) {
			pr_info("[%s][line:%d] Unable to request sim holder detect gpio [%d]\n", SIM_LOG_TAG,
				__LINE__, sim_holder_data->sim_det_gpio);
			rc = -1;
			goto err;
		}

		rc = gpio_direction_input(sim_holder_data->sim_det_gpio);
		if (rc) {
			pr_info("[%s][line:%d] Set input direction for gpio failed [%d]\n", SIM_LOG_TAG,
				__LINE__, sim_holder_data->sim_det_gpio);
			goto err;
		}
	}

	sim_det_gpio = sim_holder_dev->sim_det_gpio;
err:
	if (gpio_is_valid(sim_holder_data->sim_det_gpio))
		gpio_free(sim_holder_data->sim_det_gpio);
	return rc;
}

static int sim_holder_detect_probe(struct platform_device *pdev)
{
	int rc = 0;
	struct sim_holder_dev *sim_holder_det_dev = NULL;
	struct proc_dir_entry *sim_dir = NULL;

	pr_info("[%s][line:%d] Enter SIM Holder Detect Driver Probe...\n",
		SIM_LOG_TAG, __LINE__);
	sim_holder_det_dev = devm_kzalloc(&pdev->dev, sizeof(struct sim_holder_dev), GFP_KERNEL);
	if (sim_holder_det_dev == NULL) {
		pr_err("[%s][line:%d][%s] alloc memory error\n", SIM_LOG_TAG, __LINE__, __func__);
		rc = -ENOMEM;
		goto clear;
	}

	g_sim_holder_data = sim_holder_det_dev;

	sim_holder_det_dev->sim_dev = &pdev->dev;
	platform_set_drvdata(pdev, sim_holder_det_dev);

	/* Parse SIM card holder gpio DTS */
	rc = simcard_holder_parse_dt(sim_holder_det_dev);
	if (rc != 0) {
		pr_err("[%s][line:%d] SIM holder couldn't parse device tree, rc=%d\n",
			SIM_LOG_TAG, __LINE__, rc);
		goto clear;
	}

	/* Create /proc/simholder/sim_holder_detect */
	sim_dir = proc_create_data(SIM_HOLDER_DETECT_NAME, 0644, g_parent,
						&sim_holder_det_ops, sim_holder_det_dev);
	if (!sim_dir) {
		pr_err("[%s][line:%d] proc create sim_holder_detect failed...\n",
			SIM_LOG_TAG, __LINE__);
		rc = -1;
		goto clear;
	}

clear:
	platform_set_drvdata(pdev, NULL);
	devm_kfree(sim_holder_det_dev->sim_dev, sim_holder_det_dev);
	return rc;
}

static int sim_holder_detect_remove(struct platform_device *pdev)
{
	struct sim_holder_dev *sim_holder_det_dev = platform_get_drvdata(pdev);

	pr_info("[%s][line:%d] Enter Detect Driver Remove...\n", SIM_LOG_TAG, __LINE__);

	if (sim_holder_det_dev)
		remove_proc_entry(SIM_HOLDER_DETECT_NAME, g_parent);

	if (gpio_is_valid(sim_holder_det_dev->sim_det_gpio))
		gpio_free(sim_holder_det_dev->sim_det_gpio);

	platform_set_drvdata(pdev, NULL);
	devm_kfree(sim_holder_det_dev->sim_dev, sim_holder_det_dev);
	sim_holder_det_dev = NULL;

	return 0;
}

struct platform_driver sim_holder_detect_driver = {
	.driver = {
		.name = SIM_HOLDER_DETECT_NAME,
		.owner = THIS_MODULE,
		.of_match_table = sim_holder_det_match,
	},
	.probe = sim_holder_detect_probe,
	.remove = sim_holder_detect_remove,
};

static int __init sim_holder_detect_driver_init(void)
{
	int rc = 0;
	pr_info("[%s][line:%d] SIM Card Holder Detect Driver Init...\n", SIM_LOG_TAG, __LINE__);

	/* create dir /proc/simholder */
	if (!g_parent) {
		g_parent = proc_mkdir(SIM_DETECT_DIR, NULL);
		if (!g_parent) {
			pr_err("[%s][line:%d] Can't create simholder proc\n", SIM_LOG_TAG, __LINE__);
			rc = -ENOENT;
		}
	}

	rc = platform_driver_register(&sim_holder_detect_driver);
	if (rc < 0)
		pr_err("[%s][line:%d] SIM card holder detect driver register error, rc=%d\n", SIM_LOG_TAG, __LINE__, rc);

	return rc;
}

static void __exit sim_holder_detect_driver_exit(void)
{
	pr_info("[%s][line:%d] SIM Card Holder Detect Driver Exit...\n", SIM_LOG_TAG, __LINE__);

	if (g_parent) {
		remove_proc_entry(SIM_DETECT_DIR, NULL);
		g_parent = NULL;
	}
	if (g_sim_holder_data)
		g_sim_holder_data = NULL;

	platform_driver_unregister(&sim_holder_detect_driver);
}

module_init(sim_holder_detect_driver_init);
module_exit(sim_holder_detect_driver_exit);

MODULE_DESCRIPTION("Driver For SIM Card Holder Detect");
MODULE_LICENSE("GPL v2");
