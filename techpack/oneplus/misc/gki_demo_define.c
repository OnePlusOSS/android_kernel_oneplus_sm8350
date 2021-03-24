/*
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

// define local fine
#include<linux/init.h>
#include<linux/module.h>
#include<linux/kernel.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/uaccess.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/ctype.h>
#include <linux/miscdevice.h>
#include <linux/proc_fs.h>

int demo_define_func(void)
{
       printk("%s\n",__func__);
       return 0;
}
// Export symbol demo_define_func
EXPORT_SYMBOL(demo_define_func);

int demo_define_file(const char *path)
{
	struct file *file;
	int ret;

	if (!path || !*path)
		return -EINVAL;

	file = filp_open(path, O_RDONLY, 0);
	if (IS_ERR(file)) {
		pr_err("filp_open failed\n");
		ret = PTR_ERR(file);
		return ret;
	}
	printk("%s\n",__func__);
   	filp_close(file,NULL);
	return 0;
}

static int __init gki_demo_define_init(void)
{
       printk("%s\n",__func__);
	   demo_define_file("sdcard/");
       return 0;
}
 
static void __exit gki_demo_define_exit(void)
{
       printk("%s\n",__func__);
}
 
module_init(gki_demo_define_init);
module_exit(gki_demo_define_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("gki debug demo driver");


