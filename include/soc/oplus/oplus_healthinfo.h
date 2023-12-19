/**********************************************************************************
* Copyright (c)  2008-2015  Guangdong OPLUS Mobile Comm Corp., Ltd
* VENDOR_EDIT
* Description:    OPLUS Healthinfo Monitor
*                          Record Kernel Resourse Abnormal Stat
* Version    : 2.0
* Date       : 2018-11-01
* ------------------------------ Revision History: --------------------------------
* <version>           <date>                <author>                            <desc>
***********************************************************************************/

#ifndef _OPLUS_HEALTHINFO_H_
#define _OPLUS_HEALTHINFO_H_

#include <linux/latencytop.h>
#include <linux/sched.h>
#include <linux/cpumask.h>
#include <linux/cpuidle.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/cpufreq.h>

#ifdef CONFIG_OPLUS_MEM_MONITOR
#include <linux/healthinfo/memory_monitor.h>
#endif /*CONFIG_OPLUS_MEM_MONITOR*/

#define ohm_err(fmt, ...) \
        printk(KERN_ERR "[OHM_ERR][%s]"fmt, __func__, ##__VA_ARGS__)
#define ohm_debug(fmt, ...) \
        printk(KERN_INFO "[OHM_INFO][%s]"fmt, __func__, ##__VA_ARGS__)
#define ohm_debug_deferred(fmt, ...) \
		printk_deferred(KERN_INFO "[OHM_INFO][%s]"fmt, __func__, ##__VA_ARGS__)

#define OHM_FLASH_TYPE_EMC 1
#define OHM_FLASH_TYPE_UFS 2

#define OHM_SCHED_TYPE_MAX 12

#define LATENCY_STRING_FORMAT(BUF, MODULE, SCHED_STAT) sprintf(BUF, \
        #MODULE"_ctrl: %s\n"#MODULE"_logon: %s\n"#MODULE"_trig: %s\n" \
        #MODULE"_delta_ms: %u\n"#MODULE"_low_thresh_ms: %u\n"#MODULE"_high_thresh_ms: %u\n" \
        #MODULE"_all_max_ms: %u\n"#MODULE"_all_high_cnt: %llu\n"#MODULE"_all_low_cnt: %llu\n" \
        #MODULE"_all_total_ms: %llu\n"#MODULE"_all_total_cnt: %llu\n" \
        #MODULE"_fg_max_ms: %u\n"#MODULE"_fg_high_cnt: %llu\n"#MODULE"_fg_low_cnt: %llu\n" \
        #MODULE"_fg_total_ms: %llu\n"#MODULE"_fg_total_cnt: %llu\n" \
        #MODULE"_ux_max_ms: %u\n"#MODULE"_ux_high_cnt: %llu\n"#MODULE"_ux_low_cnt: %llu\n" \
        #MODULE"_ux_total_ms: %llu\n"#MODULE"_ux_total_cnt: %llu\n", \
        SCHED_STAT->ctrl ? "true":"false", \
        SCHED_STAT->logon ? "true":"false", \
        SCHED_STAT->trig ? "true":"false", \
        SCHED_STAT->delta_ms, \
        SCHED_STAT->low_thresh_ms, \
        SCHED_STAT->high_thresh_ms, \
        SCHED_STAT->all.max_ms, \
        SCHED_STAT->all.high_cnt, \
        SCHED_STAT->all.low_cnt, \
        SCHED_STAT->all.total_ms, \
        SCHED_STAT->all.total_cnt, \
        SCHED_STAT->fg.max_ms, \
        SCHED_STAT->fg.high_cnt, \
        SCHED_STAT->fg.low_cnt, \
        SCHED_STAT->fg.total_ms, \
        SCHED_STAT->fg.total_cnt, \
        SCHED_STAT->ux.max_ms, \
        SCHED_STAT->ux.high_cnt, \
        SCHED_STAT->ux.low_cnt, \
        SCHED_STAT->ux.total_ms, \
        SCHED_STAT->ux.total_cnt)
struct io_latency_para{
        bool ctrl;
        bool logon;
        bool trig;

        int low_thresh_ms;
        u64 low_cnt;

        int high_thresh_ms;
        u64 high_cnt;

        u64 total_us;
        u64 emmc_total_us;
        u64 total_cnt;
        u64 fg_low_cnt;
        u64 fg_high_cnt;
        u64 fg_total_ms;
        u64 fg_total_cnt;
        u64 fg_max_delta_ms;
        u64 delta_ms;

        //fg
        u64 iosize_write_count_fg;
        u64 iosize_write_us_fg;
        u64 iosize_500ms_syncwrite_count_fg;
        u64 iosize_200ms_syncwrite_count_fg;
        u64 iosize_500ms_asyncwrite_count_fg;
        u64 iosize_200ms_asyncwrite_count_fg;
        u64 iosize_read_count_fg;
        u64 iosize_read_us_fg;
        u64 iosize_500ms_read_count_fg;
        u64 iosize_200ms_read_count_fg;
        //bg
        u64 iosize_write_count_bg;
        u64 iosize_write_us_bg;
        u64 iosize_2s_asyncwrite_count_bg;
        u64 iosize_500ms_asyncwrite_count_bg;
        u64 iosize_200ms_asyncwrite_count_bg;
        u64 iosize_2s_syncwrite_count_bg;
        u64 iosize_500ms_syncwrite_count_bg;
        u64 iosize_200ms_syncwrite_count_bg;
        u64 iosize_read_count_bg;
        u64 iosize_read_us_bg;
        u64 iosize_2s_read_count_bg;
        u64 iosize_500ms_read_count_bg;
        u64 iosize_200ms_read_count_bg;

		  //4k
        u64 iosize_4k_read_count;
        u64 iosize_4k_read_us;
        u64 iosize_4k_write_count;
        u64 iosize_4k_write_us;
};
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
        OHM_SVM_MON,
        OHM_RLIMIT_MON,
        OHM_ION_MON,
		OHM_MEM_VMA_ALLOC_ERR,
        OHM_TYPE_TOTAL
};
struct rq;
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
        u64 delta_ms;
        struct sched_stat_common all;
        struct sched_stat_common fg;
        struct sched_stat_common ux;
};

struct alloc_wait_para {
	u64 total_alloc_wait_max_order;
	u64 fg_alloc_wait_max_order;
	u64 ux_alloc_wait_max_order;
	struct sched_stat_common total_alloc_wait;
	struct sched_stat_common fg_alloc_wait;
	struct sched_stat_common ux_alloc_wait;
};

struct ion_wait_para {
	struct sched_stat_common ux_ion_wait;
	struct sched_stat_common fg_ion_wait;
	struct sched_stat_common total_ion_wait;
};

enum {
    UIFIRST_TRACE_RUNNABLE = 0,
    UIFIRST_TRACE_DSTATE,
    UIFIRST_TRACE_SSTATE,
    UIFIRST_TRACE_RUNNING,
};

extern int ohm_get_cur_cpuload(bool ctrl);
extern void ohm_action_trig_with_msg(int type, char *msg);

struct brk_accounts_st {
    unsigned long brk_base;
    char comm[TASK_COMM_LEN];
    unsigned long len;
    int reserve_vma;
    int vm_search_two_way;
};

#endif /* _OPLUS_HEALTHINFO_H_*/
