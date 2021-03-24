/**********************************************************************************
* Copyright (c)  2008-2015  OnePlus Mobile Comm Corp., Ltd
* Description:    OnePlus Healthinfo Monitor
*                          Record Kernel Resourse Abnormal Stat
* Version    : 2.0
* Date       : 2019-04-24
* Author     : jared.wu@PSP
* ------------------------------ Revision History: --------------------------------
* <version>           <date>                <author>                            <desc>
* Revision 1.0        2019-04-24       jared.wu@PSP      Created for Healthinfomonitor
***********************************************************************************/

#ifndef _ONEPLUS_HEALTHINFO_H_
#define _ONEPLUS_HEALTHINFO_H_

#include <linux/latencytop.h>
#include <linux/sched.h>
#include <linux/cpumask.h>
#include <linux/cpuidle.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/fs.h>
#include <linux/slab.h>

#include <linux/ratelimit.h>

#ifdef CONFIG_ONEPLUS_MEM_MONITOR
#include <linux/oem/memory_monitor.h>
#endif /*CONFIG_ONEPLUS_MEM_MONITOR*/

#define ohm_err(fmt, ...) \
	printk(KERN_ERR "[OHM_ERR][%s]"fmt, __func__, ##__VA_ARGS__)
#define ohm_debug(fmt, ...) \
	printk(KERN_INFO "[OHM_INFO][%s]"fmt, __func__, ##__VA_ARGS__)

#define OHM_FLASH_TYPE_EMC 1
#define OHM_FLASH_TYPE_UFS 2

#define OHM_SCHED_TYPE_MAX 12
#ifdef CONFIG_ONEPLUS_HEALTHINFO
/*2020-11-27 add for stuck info*/
enum {
    STUCK_TRACE_RUNNABLE = 0,
    STUCK_TRACE_DSTATE,
    STUCK_TRACE_SSTATE,
    STUCK_TRACE_RUNNING,
};
#endif /*CONFIG_ONEPLUS_HEALTHINFO*/
enum {
	/* SCHED_STATS 0 -11 */
	OHM_SCHED_IOWAIT = 0,
	OHM_SCHED_SCHEDLATENCY,
	OHM_SCHED_FSYNC,
	OHM_SCHED_EMMCIO,
	OHM_SCHED_DSTATE,
	OHM_SCHED_TOTAL,
	/* OTHER_TYPE 12 - */
	OHM_CPU_LOAD_CUR = OHM_SCHED_TYPE_MAX,
	OHM_MEM_MON,
	OHM_IOPANIC_MON,
	OHM_TYPE_TOTAL
};

struct sched_stat_common {
		u64 max_ms;
		u64 high_cnt;
		u64 low_cnt;
		u64 total_ms;
		u64 total_cnt;
};

struct sched_stat_para {
        bool ctrl;
        bool logon;
        bool trig;
        int low_thresh_ms;
        int high_thresh_ms;
        u64 low_cnt;
        u64 high_cnt;
        u64 total_ms;
        u64 total_cnt;
        u64 fg_low_cnt;
        u64 fg_high_cnt;
        u64 fg_total_ms;
        u64 fg_total_cnt;
        u64 fg_max_delta_ms;
        u64 delta_ms;
        struct sched_stat_common all;
        struct sched_stat_common fg;
        struct sched_stat_common ux;
};

extern int ohm_get_cur_cpuload(bool ctrl);

#endif /* _ONEPLUS_HEALTHINFO_H_*/
