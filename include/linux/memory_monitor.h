/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (C) 2020-2021 Oplus. All rights reserved.
 */

#ifndef __MEMORY_MONITOR_H__
#define __MEMORY_MONITOR_H__

#include <linux/kernel.h>

struct process_mem {
        char comm[TASK_COMM_LEN];
        int pid;
        int ppid;
        int oom_score_adj;
        unsigned long rss;
        unsigned long rssfile;
        unsigned long swapents_ori;
        unsigned int uid;
        unsigned long ions;
        unsigned long gl_dev;
        unsigned long egl;
};

#ifdef CONFIG_DUMP_TASKS_MEM
void mm_get_all_pmem(int *cnt, struct process_mem *val);
#else
static inline void mm_get_all_pmem(int *cnt, struct process_mem *val) { }
#endif

#endif /* __MEMORY_MONITOR_H__ */
