// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 Oplus. All rights reserved.
 */
#include <linux/sched.h>
#include <linux/sysctl.h>
#include <linux/topology.h>
#include <linux/version.h>
#include <../kernel/sched/sched.h>

#include "sched_assist_common.h"
#include "sched_assist_slide.h"

#define UX_LOAD_WINDOW 8000000
u64 ux_task_load[NR_CPUS] = {0};
u64 ux_load_ts[NR_CPUS] = {0};

int sysctl_slide_boost_enabled = 0;
int sysctl_boost_task_threshold = 51;
int sysctl_frame_rate = 60;

extern bool task_is_sf_group(struct task_struct *p);
u64 tick_change_sf_load(struct task_struct *tsk , u64 timeline)
{
	char comm_now[TASK_COMM_LEN];
	int len = 0;
	int i = 0;
	u64 sf_sum_load = 0;

	if(!tsk || !task_is_sf_group(tsk))
		return timeline;

	memset(comm_now, '\0', sizeof(comm_now));
	strcpy(comm_now, tsk->comm);

	len = strlen(comm_now);
	for(i = 0; i < SF_GROUP_COUNT ;i++) {
		if (!strncmp(comm_now, sf_target[i].val, len))
			sf_target[i].ux_load = timeline;
		sf_sum_load += sf_target[i].ux_load;
	}

	sched_assist_systrace(sf_sum_load, "sf_load_%d", task_cpu(tsk));
	return sf_sum_load;
}

static u64 calc_freq_ux_load(struct task_struct *p, u64 wallclock)
{
	unsigned int maxtime = 0, factor = 0;
	unsigned int window_size = sched_ravg_window / NSEC_PER_MSEC;
	u64 timeline = 0, freq_exec_load = 0, freq_ravg_load = 0;
	u64 wakeclock = p->wts.last_wake_ts;
	if (wallclock < wakeclock)
		return 0;
	if (sysctl_frame_rate <= 90)
		maxtime = 5;
	else if (sysctl_frame_rate <= 120)
		maxtime = 4;
	else
		maxtime = 3;
	timeline = wallclock - wakeclock;
	timeline = tick_change_sf_load(p, timeline);
	factor = window_size / maxtime;
	freq_exec_load = timeline * factor;
	if (freq_exec_load > sched_ravg_window)
		freq_exec_load = sched_ravg_window;
	freq_ravg_load = (p->wts.prev_window + p->wts.curr_window) << 1;
	if (freq_ravg_load > sched_ravg_window)
		freq_ravg_load = sched_ravg_window;
	if (task_is_sf_group(p)) {
        	sched_assist_systrace(timeline, "timeline_true_%d", task_cpu(p));
        	sched_assist_systrace(freq_exec_load, "freq_exec_true_%d", task_cpu(p));
        	sched_assist_systrace(freq_ravg_load, "freq_ravg_true_%d", task_cpu(p));
        }
	return max(freq_exec_load, freq_ravg_load);
}

void _slide_find_start_cpu(struct root_domain *rd, struct task_struct *p, int *start_cpu)
{
	if (task_util(p) >= sysctl_boost_task_threshold ||
		scale_demand(p->wts.sum) >= sysctl_boost_task_threshold) {
		*start_cpu = rd->wrd.mid_cap_orig_cpu == -1 ?
			rd->wrd.max_cap_orig_cpu : rd->wrd.mid_cap_orig_cpu;
	}
}

bool _slide_task_misfit(struct task_struct *p, int cpu)
{
	int num_mincpu = cpumask_weight(topology_core_cpumask(0));
	if ((scale_demand(p->wts.sum) >= sysctl_boost_task_threshold ||
	     task_util(p) >= sysctl_boost_task_threshold) && cpu < num_mincpu)
		return true;
	return false;
}

u64 _slide_get_boost_load(int cpu) {
	u64 wallclock = sched_ktime_clock();
	u64 timeline = 0;
	if (slide_scene() && ux_task_load[cpu]) {
		timeline = wallclock - ux_load_ts[cpu];
		if  (timeline >= UX_LOAD_WINDOW)
			ux_task_load[cpu] = 0;
		return ux_task_load[cpu];
	} else {
		return 0;
	}
}

void slide_calc_boost_load(struct rq *rq, unsigned int *flag, int cpu) {
	u64 wallclock = sched_ktime_clock();

	if (slide_scene()) {
		if(rq->curr && (is_heavy_ux_task(rq->curr) || rq->curr->sched_class == &rt_sched_class) && !oplus_task_misfit(rq->curr, rq->cpu)) {
			ux_task_load[cpu] = calc_freq_ux_load(rq->curr, wallclock);
			ux_load_ts[cpu] = wallclock;
			*flag |= (SCHED_CPUFREQ_WALT | SCHED_CPUFREQ_BOOST);
		}
		else if (ux_task_load[cpu] != 0) {
			ux_task_load[cpu] = 0;
			ux_load_ts[cpu] = wallclock;
			*flag |= (SCHED_CPUFREQ_WALT | SCHED_CPUFREQ_RESET);
		}
	} else {
		ux_task_load[cpu] = 0;
		ux_load_ts[cpu] = 0;
	}
	sched_assist_systrace(ux_task_load[cpu], "ux_load_%d", cpu);
}

int sched_frame_rate_handler(struct ctl_table *table, int write, void __user *buffer, size_t *lenp, loff_t *ppos)
{
	int ret;

	if (write && *ppos)
		*ppos = 0;

	ret = proc_dointvec(table, write, buffer, lenp, ppos);

	return ret;
}

int sysctl_sched_slide_boost_handler(struct ctl_table *table, int write, void __user *buffer, size_t *lenp, loff_t *ppos)
{
	int ret;

	if (write && *ppos)
		*ppos = 0;

	ret = proc_dointvec(table, write, buffer, lenp, ppos);

	if (write)
		sched_assist_systrace(sysctl_slide_boost_enabled, "ux slide");

	return ret;
}

