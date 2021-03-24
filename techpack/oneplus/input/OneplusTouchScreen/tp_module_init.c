#include <linux/module.h>
#include "touchpanel_common.h"

extern struct i2c_driver sec_tp_i2c_driver;
extern struct i2c_driver syna_i2c_driver;

/****************** Start of Log Tag Declear and level define*******************************/
#define TPD_DEVICE "tp_module_init"
#define TPD_INFO(a, arg...)  pr_err("[TP]"TPD_DEVICE ": " a, ##arg)
#define TPD_DEBUG(a, arg...)\
	do{\
		if (LEVEL_DEBUG == tp_debug)\
		pr_err("[TP]"TPD_DEVICE ": " a, ##arg);\
	}while(0)

#define TPD_DETAIL(a, arg...)\
	do{\
		if (LEVEL_BASIC != tp_debug)\
		pr_err("[TP]"TPD_DEVICE ": " a, ##arg);\
	}while(0)

#define TPD_DEBUG_NTAG(a, arg...)\
	do{\
		if (tp_debug)\
		printk(a, ##arg);\
	}while(0)
/******************** End of Log Tag Declear and level define*********************************/

/***********************Start of module init and exit****************************/
static int __init tp_driver_init(void)
{
	TPD_INFO("%s is called\n", __func__);
	if (i2c_add_driver(&sec_tp_i2c_driver)!= 0) {
		TPD_INFO("unable to add i2c driver.\n");
		return -1;
	}
	if (i2c_add_driver(&syna_i2c_driver)!= 0) {
		TPD_INFO("unable to add i2c driver.\n");
		return -1;
	}
	return 0;
}

static void __exit tp_driver_exit(void)
{
	i2c_del_driver(&sec_tp_i2c_driver);
	i2c_del_driver(&syna_i2c_driver);
}

late_initcall(tp_driver_init);
module_exit(tp_driver_exit);
/***********************End of module init and exit*******************************/

MODULE_AUTHOR("Samsung Driver");
MODULE_DESCRIPTION("Samsung Electronics TouchScreen driver");
MODULE_LICENSE("GPL v2");

