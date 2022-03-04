// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 Oplus. All rights reserved.
 */
#include <linux/sched.h>
#include <linux/sysctl.h>
#include <linux/topology.h>
#include <../kernel/sched/sched.h>

#include <linux/sched_assist/sched_assist_common.h>
#include <linux/sched_assist/sched_assist_slide.h>

#define UX_LOAD_WINDOW 8000000
#define DEFAULT_INPUT_BOOST_DURATION 1000
u64 ux_task_load[NR_CPUS] = {0};
u64 ux_load_ts[NR_CPUS] = {0};

int sysctl_slide_boost_enabled = 0;
int sysctl_boost_task_threshold = 51;
int sysctl_frame_rate = 60;

void sched_assist_adjust_slide_param(unsigned int *maxtime)
{
	/*give each scene with default boost value*/
	if (sched_assist_scene(SA_SLIDE)) {
		if (sysctl_frame_rate <= 90)
			*maxtime = 5;
		else if (sysctl_frame_rate <= 120)
			*maxtime = 4;
		else
			*maxtime = 3;
	} else if (sched_assist_scene(SA_INPUT)) {
		if (sysctl_frame_rate <= 90)
			*maxtime = 8;
		else if (sysctl_frame_rate <= 120)
			*maxtime = 7;
		else
			*maxtime = 6;
	}
}

static u64 calc_freq_ux_load(struct task_struct *p, u64 wallclock)
{
	unsigned int maxtime = 5, factor = 0;
	unsigned int window_size = sched_ravg_window / NSEC_PER_MSEC;
	u64 timeline = 0, freq_exec_load = 0, freq_ravg_load = 0;
	u64 wakeclock = p->wts.last_wake_ts;

	if (wallclock < wakeclock)
		return 0;

	sched_assist_adjust_slide_param(&maxtime);
	timeline = wallclock - wakeclock;
	factor = window_size / maxtime;
	freq_exec_load = timeline * factor;
	if (freq_exec_load > sched_ravg_window)
		freq_exec_load = sched_ravg_window;

	freq_ravg_load = (p->wts.prev_window + p->wts.curr_window) << 1;
	if (freq_ravg_load > sched_ravg_window)
		freq_ravg_load = sched_ravg_window;

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

u64 _slide_get_boost_load(int cpu)
{
	u64 wallclock = sched_ktime_clock();
	u64 timeline = 0;
	u64 ret_load = 0;

	if ((sched_assist_scene(SA_SLIDE) || sched_assist_scene(SA_INPUT)) && ux_task_load[cpu]) {
		timeline = wallclock - ux_load_ts[cpu];
		if  (timeline >= UX_LOAD_WINDOW)
			ux_task_load[cpu] = 0;

		ret_load = ux_task_load[cpu];
	}

	return ret_load;
}

void adjust_sched_assist_input_ctrl(void)
{
	static int input_boost_old;

	if (sysctl_input_boost_enabled) {
		if (sysctl_slide_boost_enabled || jiffies_to_msecs(jiffies) >= sched_assist_input_boost_duration) {
			sysctl_input_boost_enabled = 0;
			sched_assist_input_boost_duration = 0;
		}
	}
	if (input_boost_old != sysctl_input_boost_enabled) {
		input_boost_old = sysctl_input_boost_enabled;
		sched_assist_systrace(sysctl_input_boost_enabled, "ux input");
	}
}

void slide_calc_boost_load(struct rq *rq, unsigned int *flag, int cpu)
{
	u64 wallclock = sched_ktime_clock();

	adjust_sched_assist_input_ctrl();
	if (sched_assist_scene(SA_SLIDE) || sched_assist_scene(SA_INPUT)) {
		if (rq->curr && (is_heavy_ux_task(rq->curr) || rq->curr->sched_class == &rt_sched_class)
				&& !_slide_task_misfit(rq->curr, rq->cpu)) {
			ux_task_load[cpu] = calc_freq_ux_load(rq->curr, wallclock);
			ux_load_ts[cpu] = wallclock;
			*flag |= (SCHED_CPUFREQ_WALT | SCHED_CPUFREQ_BOOST);
		} else if (ux_task_load[cpu] != 0) {
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

	if (write)
		sched_assist_systrace(sysctl_frame_rate, "ux frate");

	return ret;
}

int sysctl_sched_assist_input_boost_ctrl_handler(struct ctl_table *table, int write,
	void __user *buffer, size_t *lenp, loff_t *ppos)
{
	int result;
	static DEFINE_MUTEX(sa_boost_mutex);

	mutex_lock(&sa_boost_mutex);
	result = proc_dointvec(table, write, buffer, lenp, ppos);
	if (!write)
		goto out;
	/*orms write just write this proc to tell us update input window*/
	sched_assist_input_boost_duration  = jiffies_to_msecs(jiffies) + DEFAULT_INPUT_BOOST_DURATION / sched_assist_ib_duration_coedecay;
out:
	mutex_unlock(&sa_boost_mutex);
	return result;
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

int sysctl_sched_boost_task_threshold_handler(struct ctl_table *table, int write, void __user *buffer, size_t *lenp, loff_t *ppos)
{
	int ret;

	if (write && *ppos)
		*ppos = 0;

	ret = proc_dointvec(table, write, buffer, lenp, ppos);

	if (write)
		sched_assist_systrace(sysctl_boost_task_threshold, "ux threshold");

	return ret;
}

int sysctl_sched_animation_type_handler(struct ctl_table *table, int write, void __user *buffer, size_t *lenp, loff_t *ppos)
{
	int ret;

	if (write && *ppos)
		*ppos = 0;

	ret = proc_dointvec(table, write, buffer, lenp, ppos);

	if (write)
		sched_assist_systrace(sysctl_animation_type, "ux anima");

	return ret;
}


