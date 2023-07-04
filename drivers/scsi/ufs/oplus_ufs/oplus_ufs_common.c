/**********************************************************************************
* Copyright (c), 2008-2019 , Guangdong OPLUS Mobile Comm Corp., Ltd.
* VENDOR_EDIT
* File: ufs-oplus.c
* Description: UFS GKI
* Version: 1.0
* Date: 2020-08-12
* ------------------------------ Revision History: --------------------------------
* <version>           <date>                <author>                            <desc>
***********************************************************************************/

#include <linux/module.h>
#include <trace/hooks/oplus_ufs.h>
#include <soc/oplus/device_info.h>
#include <linux/proc_fs.h>

int ufsplus_tw_status = 0;
EXPORT_SYMBOL(ufsplus_tw_status);
int ufsplus_hpb_status = 0;
EXPORT_SYMBOL(ufsplus_hpb_status);

void ufs_gen_proc_devinfo_handle(void *data, struct ufs_hba *hba)
{
	int ret = 0;
	static char temp_version[5] = {0};
	static char vendor[9] = {0};
	static char model[17] = {0};

	strncpy(temp_version, hba->sdev_ufs_device->rev, 4);
	strncpy(vendor, hba->sdev_ufs_device->vendor, 8);
	strncpy(model, hba->sdev_ufs_device->model, 16);
	ret = register_device_proc("ufs_version", temp_version, vendor);
	if (ret) {
		printk("Fail register_device_proc ufs_version\n");
	}
	ret = register_device_proc("ufs", model, vendor);
	if (ret) {
		printk("Fail register_device_proc ufs \n");
	}
	ret = register_device_proc_for_ufsplus("ufsplus_status", &ufsplus_hpb_status,&ufsplus_tw_status);
	if (ret) {
		printk("Fail register_device_proc_for_ufsplus\n");
	}
	return;
}

/*
 * Check whether this bio carries any data or not. A NULL bio is allowed.
 */
static inline bool oplus_ufs_bio_has_data(struct bio *bio)
{
	if (bio &&
	    bio->bi_iter.bi_size &&
	    bio_op(bio) != REQ_OP_DISCARD &&
	    bio_op(bio) != REQ_OP_SECURE_ERASE &&
	    bio_op(bio) != REQ_OP_WRITE_ZEROES)
		return true;

	return false;
}

void ufs_latency_hist_handle(void *data, struct ufs_hba *hba , struct ufshcd_lrb *lrbp)
{
	if(lrbp->cmd->request){
		u_int64_t delta_us = ktime_us_delta(lrbp->compl_time_stamp, lrbp->issue_time_stamp);

		if((5000 < delta_us) && oplus_ufs_bio_has_data(lrbp->cmd->request->bio)){
			trace_printk("ufs_io_latency:%06lld us, io_type:%s, LBA:%08x, size:%d\n",
					delta_us, (rq_data_dir(lrbp->cmd->request) == READ) ? "R" : "W",
					(unsigned int)lrbp->cmd->request->bio->bi_iter.bi_sector,
					lrbp->cmd->sdb.length);
		}
	}
		
	return;
}

static int __init
oplus_ufs_common_init(void)
{
	int rc;

	printk("oplus_ufs_common_init");

	rc = register_trace_android_vh_ufs_gen_proc_devinfo(ufs_gen_proc_devinfo_handle, NULL);
	if (rc != 0)
		pr_err("register_trace_android_vh_ufs_gen_proc_devinfo failed! rc=%d\n", rc);
		
	rc = register_trace_android_vh_ufs_latency_hist(ufs_latency_hist_handle, NULL);
	if (rc != 0)
		pr_err("register_trace_android_vh_ufs_latency_hist failed! rc=%d\n", rc);

	return rc;
}

device_initcall(oplus_ufs_common_init);

MODULE_DESCRIPTION("OPLUS ufs driver common");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Tianwen");
