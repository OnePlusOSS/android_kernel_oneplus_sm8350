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
#include <linux/oem/gki_demo.h>

static int __init gki_demo_call_init(void)
{
	demo_define_func();
	printk("%s\n",__func__);
	return 0;
}

static void __exit gki_demo_call_exit(void)
{
	printk("%s\n",__func__);
}

module_init(gki_demo_call_init);
module_exit(gki_demo_call_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("gki debug demo driver");

