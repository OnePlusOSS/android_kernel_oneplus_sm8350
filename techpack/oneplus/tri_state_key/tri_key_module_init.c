#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include "oneplus_tri_key.h"
#include "../../../drivers/extcon/extcon.h"

#define TRI_KEY_TAG                  "[tri_state_key] "
#define TRI_KEY_ERR(fmt, args...)    printk(KERN_ERR TRI_KEY_TAG" %s : "fmt, __FUNCTION__, ##args)
#define TRI_KEY_LOG(fmt, args...)    printk(KERN_INFO TRI_KEY_TAG" %s : "fmt, __FUNCTION__, ##args)
#define TRI_KEY_DEBUG(fmt, args...)\
	do{\
		if (LEVEL_DEBUG == tri_key_debug)\
			printk(KERN_INFO TRI_KEY_TAG " %s: " fmt, __FUNCTION__, ##args);\
	}while(0)

//extern struct platform_driver tristate_dev_driver;
extern struct platform_driver tri_key_platform_driver;
extern struct i2c_driver m1120_i2c_up_driver;
extern struct i2c_driver m1120_i2c_down_driver;
extern struct i2c_driver ist8801_i2c_up_driver;
extern struct i2c_driver ist8801_i2c_down_driver;


static int __init tri_key_module_init(void)
{
	TRI_KEY_LOG("call\n");

	if (platform_driver_register(&tri_key_platform_driver) != 0) {
		TRI_KEY_LOG("unable to add tri_key_platform_driver\n");
		return -1;
	}

	if (i2c_add_driver(&ist8801_i2c_down_driver) != 0){
		TRI_KEY_LOG("unable to add ist8801_i2c_down_driver\n");
		return -1;
	}

	if (i2c_add_driver(&ist8801_i2c_up_driver) != 0){
		TRI_KEY_LOG("unable to add ist8801_i2c_up_driver\n");
		return -1;
	}

	if (i2c_add_driver(&m1120_i2c_down_driver) != 0){
		TRI_KEY_LOG("unable to add m1120_down_driver\n");
		return -1;
	}

	if (i2c_add_driver(&m1120_i2c_up_driver) != 0){
		TRI_KEY_LOG("unable to add m1120_up_driver\n");
		return -1;
	}

/*
	if (platform_driver_register(&tristate_dev_driver) != 0) {
		TRI_KEY_LOG("unable to add tristate_dev_driver\n");
		return -1;
	}
*/
	return 0;
}

late_initcall(tri_key_module_init);

static void __exit tri_key_module_exit(void)
{
	TRI_KEY_LOG("call\n");

	i2c_del_driver(&ist8801_i2c_down_driver);

	i2c_del_driver(&ist8801_i2c_up_driver);

	i2c_del_driver(&m1120_i2c_down_driver);

	i2c_del_driver(&m1120_i2c_up_driver);

	platform_driver_unregister(&tri_key_platform_driver);

	//platform_driver_unregister(&tristate_dev_driver);

}
module_exit(tri_key_module_exit);
MODULE_DESCRIPTION("oem tri_state_key driver");
MODULE_LICENSE("GPL v2");



