// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/fs.h>

#include <linux/io.h>
#include <linux/of.h>
#ifdef CONFIG_MTK_SECURITY_SW_SUPPORT
#include <sec_boot_lib.h>
#endif
#include <linux/syscalls.h>


#include <soc/oplus/system/oplus_project.h>
#include <soc/oplus/system/oplus_project_oldcdt.h>


extern struct proc_dir_entry *oplus_info;
ProjectInfoCDTType_oldcdt *format = NULL;

/*Add for diff manifest*/
static const char *nfc_feature = "nfc_feature";
static const char *feature_src = "/vendor/etc/nfc/com.oplus.nfc_feature.xml";

static ProjectInfoCDTType_oldcdt projectInfo = {
	.nproject		= 0,
	.nmodem			= 0,
	.noperator		= 0,
	.npcbversion	        = 0,
	.nengversion            = 0,
	.isconfidential         = 1,
};

/*Add for diff manifest*/
static int __init update_feature(void)
{
	mm_segment_t fs;
	fs = get_fs();
	pr_err("%s: oldcdt -- Operator Version [%d]\n", __func__, (get_project()));
	set_fs(KERNEL_DS);
	if (oplus_info) {
			/* 18073 includes 18073,18075,18593
			 * 19011 includes 19011,19305 exclude 19301
			 */
			if (get_project() == 18073 || (get_project() == 19011 && get_Operator_Version() != 80)) {
				proc_symlink(nfc_feature, oplus_info, feature_src);
			}
	}
	set_fs(fs);
	return 0;
}
late_initcall(update_feature);


static int init_project_version_oldcdt(void)
{
	struct device_node *np = NULL;
	int ret = 0;

	if (format) {
		/* pr_err("%s has inited\n", __func__); */
		return 0;
	}
	printk("init_project_version_oldcdt start\n");

	np = of_find_node_by_name(NULL, "oplus_project");
	if (!np) {
		printk("init_project_version error1");
		return -1;
	}

	ret = of_property_read_u32(np, "nProject", &(projectInfo.nproject));
	if (ret) {
		printk("init_project_version error2");
		return -1;
	}

	ret = of_property_read_u32(np, "nModem", &(projectInfo.nmodem));
	if (ret) {
		printk("init_project_version error3");
		return -1;
	}

	ret = of_property_read_u32(np, "nOperator", &(projectInfo.noperator));
	if (ret) {
		printk("init_project_version error4");
		return -1;
	}

	ret = of_property_read_u32(np, "nPCBVersion", &(projectInfo.npcbversion));
	if (ret) {
		printk("init_project_version error5");
		return -1;
	}

	ret = of_property_read_u32(np, "nENGVersion", &(projectInfo.nengversion));
	if (ret) {
		printk("init_project_version error6");
	}

	ret = of_property_read_u32(np, "isConfidential", &(projectInfo.isconfidential));
	if (ret) {
		printk("init_project_version error7");
	}
	format = &projectInfo;
	printk("KE oldcdt Version Info :Project(%d) Modem(%d) Operator(%d) PCB(%d) ENGVersion(%d) Confidential(%d)\n",
		format->nproject, format->nmodem, format->noperator, format->npcbversion, format->nengversion, format->isconfidential);

	return 0;
}


unsigned int get_project_oldcdt(void)
{
	if (format == NULL) {
		oplus_project_init_oldcdt();
	}
	if (format)
		return format->nproject;
	else
		return 0;
}

EXPORT_SYMBOL(get_project_oldcdt);

unsigned int is_project_oldcdt(int project)
{
	return (get_project_oldcdt() == project?1:0);
}

unsigned int get_PCB_Version_oldcdt(void)
{
	if (format)
		return format->npcbversion;
	return 0;
}

unsigned int get_Modem_Version_oldcdt(void)
{
	if (format)
		return format->nmodem;
	return 0;
}

unsigned int get_Operator_Version_oldcdt(void)
{
	if (format == NULL) {
		oplus_project_init_oldcdt();
	}
	if (format)
		return format->noperator;
	return 0;
}

unsigned int get_eng_version_oldcdt(void)
{
	if (format)
		return format->nengversion;
	return 0;
}

bool oplus_daily_build_oldcdt(void)
{
	return false;
}

int is_confidential_oldcdt(void)
{
	if (format)
		return format->isconfidential;
	return 1;
}

/* this module just init for creat files to show which version */
static ssize_t prjVersion_read_proc(struct file *file, char __user *buf,
		size_t count, loff_t *off)
{
	char page[256] = {0};
	int len = 0;

/* modify for compatible of hexadecimal project model */
	if (get_project_oldcdt() > 0x20000) {
		len = sprintf(page, "%X", get_project_oldcdt());
	} else {
		len = sprintf(page, "%d", get_project_oldcdt());
	}
	if (len > *off)
		len -= *off;
	else
		len = 0;

	if (copy_to_user(buf, page, (len < count ? len : count))) {
		return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);
}

static struct file_operations prjVersion_proc_fops = {
	.read = prjVersion_read_proc,
	.write = NULL,
};


static ssize_t pcbVersion_read_proc(struct file *file, char __user *buf,
		size_t count, loff_t *off)
{
	char page[256] = {0};
	int len = 0;

	len = sprintf(page, "%d", get_PCB_Version_oldcdt());

	if (len > *off)
	   len -= *off;
	else
	   len = 0;

	if (copy_to_user(buf, page, (len < count ? len : count))) {
	   return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);
}

static struct file_operations pcbVersion_proc_fops = {
	.read = pcbVersion_read_proc,
};

static ssize_t engVersion_read_proc(struct file *file, char __user *buf,
		size_t count, loff_t *off)
{
	char page[256] = {0};
	int len = 0;

	len = sprintf(page, "%d", get_eng_version_oldcdt());

	if (len > *off)
	   len -= *off;
	else
	   len = 0;

	if (copy_to_user(buf, page, (len < count ? len : count))) {
	   return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);
}

static struct file_operations engVersion_proc_fops = {
	.read = engVersion_read_proc,
};

static ssize_t is_Confidential_read_proc(struct file *file, char __user *buf,
		size_t count, loff_t *off)
{
	char page[256] = {0};
	int len = 0;

	len = sprintf(page, "%d", is_confidential_oldcdt());

	if (len > *off)
	   len -= *off;
	else
	   len = 0;

	if (copy_to_user(buf, page, (len < count ? len : count))) {
	   return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);
}

static struct file_operations isConfidential_proc_fops = {
	.read = is_Confidential_read_proc,
};

static ssize_t operatorName_read_proc(struct file *file, char __user *buf,
		size_t count, loff_t *off)
{
	char page[256] = {0};
	int len = 0;

	len = sprintf(page, "%d", get_Operator_Version_oldcdt());

	if (len > *off)
	   len -= *off;
	else
	   len = 0;

	if (copy_to_user(buf, page, (len < count ? len : count))) {
	   return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);
}

static struct file_operations operatorName_proc_fops = {
	.read = operatorName_read_proc,
};


static ssize_t modemType_read_proc(struct file *file, char __user *buf,
		size_t count, loff_t *off)
{
	char page[256] = {0};
	int len = 0;

	len = sprintf(page, "%d", get_Modem_Version_oldcdt());

	if (len > *off)
	   len -= *off;
	else
	   len = 0;

	if (copy_to_user(buf, page, (len < count ? len : count))) {
	   return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);
}

static struct file_operations modemType_proc_fops = {
	.read = modemType_read_proc,
};

static ssize_t secureType_read_proc(struct file *file, char __user *buf,
		size_t count, loff_t *off)
{
	char page[256] = {0};
	int len = 0;
	#ifndef OPLUS_BUG_STABILITY
	/* Add for MTK secureType node */
	uint32_t secure_oem_config = 0;

		printk(KERN_EMERG "lycan test secure_oem_config 0x%x\n", secure_oem_config);
		#else
#ifdef CONFIG_MTK_SECURITY_SW_SUPPORT
	int secure_oem_config = 0;
	secure_oem_config = sec_schip_enabled();
	printk(KERN_EMERG "lycan test secure_oem_config %d\n", secure_oem_config);
#else
	uint32_t secure_oem_config = 0;
#endif
		#endif /* OPLUS_BUG_STABILITY */

	len = sprintf(page, "%d", secure_oem_config);

	if (len > *off)
	   len -= *off;
	else
	   len = 0;

	if (copy_to_user(buf, page, (len < count ? len : count))) {
	   return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);
}


static struct file_operations secureType_proc_fops = {
	.read = secureType_read_proc,
};

/*Add serialID for fastboot unlock*/
#define SERIALNO_LEN 16
extern char *saved_command_line;
static ssize_t serialID_read_proc(struct file *file, char __user *buf,
				size_t count, loff_t *off)
{
		char page[256] = {0};
		int len = 0;
		char * ptr;
		char serialno[SERIALNO_LEN+1] = {0};

		ptr = strstr(saved_command_line, "androidboot.serialno=");
		ptr += strlen("androidboot.serialno=");
		strncpy(serialno, ptr, SERIALNO_LEN);
		serialno[SERIALNO_LEN] = '\0';

		len = sprintf(page, "0x%s", serialno);
		if (len > *off) {
				len -= *off;
		}
		else {
				len = 0;
		}

		if (copy_to_user(buf, page, (len < count ? len : count))) {
				return -EFAULT;
		}

		*off += len < count ? len : count;
		return (len < count ? len : count);
}

struct file_operations serialID_proc_fops = {
		.read = serialID_read_proc,
};

int oplus_project_init_oldcdt(void)
{
	int ret = 0;
	struct proc_dir_entry *pentry;

	ret = init_project_version_oldcdt();
	if (ret) {
		pr_err("%s init_project fail, ret:%d\n", __func__, ret);
		goto init_project_fail;
	}

	if (oplus_info == NULL) {
		pr_err("oplus_info == NULL, maybe wrong state\n");
		oplus_info =  proc_mkdir("oplusVersion", NULL);
		if (oplus_info == NULL) {
			pr_err("can't create oplus_info proc\n");
			goto ERROR_INIT_VERSION;
		}
		} else {
		pr_err("oplus_info != NULL, going on\n");
	}

	pentry = proc_create("prjVersion", S_IRUGO, oplus_info, &prjVersion_proc_fops);
	if (!pentry) {
		pr_err("create prjVersion proc failed.\n");
	}

	pentry = proc_create("pcbVersion", S_IRUGO, oplus_info, &pcbVersion_proc_fops);
	if (!pentry) {
		pr_err("create pcbVersion proc failed.\n");
	}
	pentry = proc_create("engVersion", S_IRUGO, oplus_info, &engVersion_proc_fops);
	if (!pentry) {
		pr_err("create engVersion proc failed.\n");
	}
	pentry = proc_create("isConfidential", S_IRUGO, oplus_info, &isConfidential_proc_fops);
	if (!pentry) {
		pr_err("create is_confidential proc failed.\n");
	}
	pentry = proc_create("operatorName", S_IRUGO, oplus_info, &operatorName_proc_fops);
	if (!pentry) {
		pr_err("create operatorName proc failed.\n");
	}
	pentry = proc_create("modemType", S_IRUGO, oplus_info, &modemType_proc_fops);
	if (!pentry) {
		pr_err("create modemType proc failed.\n");
	}
	pentry = proc_create("secureType", S_IRUGO, oplus_info, &secureType_proc_fops);
	if (!pentry) {
		pr_err("create secureType proc failed.\n");
	}
/*Add serialID for fastboot unlock*/
	pentry = proc_create("serialID", S_IRUGO, oplus_info, &serialID_proc_fops);
	if (!pentry) {
		pr_err("create serialID proc failed.\n");
	}
	pr_err("%s success\n", __func__);
	return ret;
ERROR_INIT_VERSION:
init_project_fail:
	oplus_info = NULL;
	format = NULL;
	return -ENOENT;
}


MODULE_DESCRIPTION("OPLUS project oldcdt version");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("DJ");
