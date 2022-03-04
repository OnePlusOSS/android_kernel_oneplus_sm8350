// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 Oplus. All rights reserved.
 */
#include <linux/sched.h>
#include <linux/sysctl.h>
#include <linux/topology.h>
#include <../kernel/sched/sched.h>
#include <../kernel/sched/walt.h>
#include <linux/sched/cpufreq.h>
#include <linux/sched_assist/sched_assist_common.h>
#include <linux/sched_assist/sched_assist_slide.h>

#define UX_LOAD_WINDOW 8000000
#define DEFAULT_INPUT_BOOST_DURATION 1000
extern struct ux_sched_cputopo ux_sched_cputopo;
u64 ux_task_load[NR_CPUS] = {0};
u64 ux_load_ts[NR_CPUS] = {0};
int sysctl_slide_boost_enabled = 0;
int sysctl_boost_task_threshold = 51;
int sysctl_frame_rate = 60;

void sched_assist_adjust_slide_param(unsigned int *maxtime)
{
/* give each scene with default boost value */
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
	unsigned int maxtime = 0, factor = 0;
#ifdef CONFIG_OPLUS_SYSTEM_KERNEL_QCOM
	unsigned int window_size = sched_ravg_window / NSEC_PER_MSEC;
#else
	unsigned int window_size = walt_ravg_window / NSEC_PER_MSEC;
#endif
	u64 timeline = 0, freq_exec_load = 0, freq_ravg_load = 0;
	u64 wakeclock = p->last_wake_ts;

	if (wallclock < wakeclock)
		return 0;

	sched_assist_adjust_slide_param(&maxtime);
	timeline = wallclock - wakeclock;
	factor = window_size / maxtime;
	freq_exec_load = timeline * factor;
#ifdef CONFIG_OPLUS_SYSTEM_KERNEL_QCOM
	if (freq_exec_load > sched_ravg_window)
		freq_exec_load = sched_ravg_window;
#else
	if (freq_exec_load > walt_ravg_window)
		freq_exec_load = walt_ravg_window;
#endif
	freq_ravg_load = (p->ravg.prev_window + p->ravg.curr_window) << 1;
#ifdef CONFIG_OPLUS_SYSTEM_KERNEL_QCOM
	if (freq_ravg_load > sched_ravg_window)
		freq_ravg_load = sched_ravg_window;
#else
	if (freq_ravg_load > walt_ravg_window)
		freq_ravg_load = walt_ravg_window;
#endif
	return max(freq_exec_load, freq_ravg_load);
}

void _slide_find_start_cpu(struct root_domain *rd, struct task_struct *p, int *start_cpu)
{
#ifdef CONFIG_OPLUS_SYSTEM_KERNEL_QCOM
	if (task_util(p) >= sysctl_boost_task_threshold ||
		scale_demand(p->ravg.sum) >= sysctl_boost_task_threshold) {
#else
	if (READ_ONCE(p->se.avg.util_avg) >= sysctl_boost_task_threshold ||
		scale_demand(p->ravg.sum) >= sysctl_boost_task_threshold) {
#endif
		if (ux_sched_cputopo.cls_nr > 2)
			*start_cpu = cpumask_first(&ux_sched_cputopo.sched_cls[1].cpus);
	} else {
		*start_cpu = rd->max_cap_orig_cpu == -1 ?
			rd->max_cap_orig_cpu : rd->min_cap_orig_cpu;
	}
}

u64 _slide_get_boost_load(int cpu)
{
	u64 timeline = 0;
#ifdef CONFIG_OPLUS_SYSTEM_KERNEL_QCOM
	u64 wallclock = sched_ktime_clock();
#else
	u64 wallclock = walt_ktime_clock();
#endif

	if ((sched_assist_scene(SA_SLIDE) || sched_assist_scene(SA_INPUT) ||
			sched_assist_scene(SA_LAUNCHER_SI)) && ux_task_load[cpu]) {
		timeline = wallclock - ux_load_ts[cpu];
		if (timeline >= UX_LOAD_WINDOW)
			ux_task_load[cpu] = 0;

		return ux_task_load[cpu];
	}
	return 0;
}

#if defined(OPLUS_FEATURE_SCHED_ASSIST) && defined(CONFIG_SCHED_WALT)
#ifdef CONFIG_OPLUS_SYSTEM_KERNEL_QCOM
#define SCHED_CPUFREQ_RESET (1U << 7)
#define SCHED_CPUFREQ_BOOST (1U << 9)
#else
#define SCHED_CPUFREQ_WALT (1U << 4)
#define SCHED_CPUFREQ_RESET (1U << 7)
#define SCHED_CPUFREQ_BOOST (1U << 9)
#endif
#endif /* OPLUS_FEATURE_SCHED_ASSIST */
void adjust_sched_assist_input_ctrl(void)
{
	if (!sysctl_input_boost_enabled)
		return;

	if (sysctl_slide_boost_enabled) {
		sysctl_input_boost_enabled = 0;
		sched_assist_input_boost_duration = 0;
		return;
	}

	if (jiffies_to_msecs(jiffies) >= sched_assist_input_boost_duration) {
		sched_assist_input_boost_duration = 0;
		sysctl_input_boost_enabled = 0;
	}
}
void slide_calc_boost_load(struct rq *rq, unsigned int *flag, int cpu)
{
#ifdef CONFIG_OPLUS_SYSTEM_KERNEL_QCOM
	u64 wallclock = sched_ktime_clock();
#else
	u64 wallclock = walt_ktime_clock();
#endif
	adjust_sched_assist_input_ctrl();
	if (sched_assist_scene(SA_SLIDE) || sched_assist_scene(SA_LAUNCHER_SI) || sched_assist_scene(SA_INPUT)) {
		if (rq->curr && (is_heavy_ux_task(rq->curr) || rq->curr->sched_class == &rt_sched_class)
				&& !oplus_task_misfit(rq->curr, rq->cpu)) {
			ux_task_load[cpu] = calc_freq_ux_load(rq->curr, wallclock);
			ux_load_ts[cpu] = wallclock;
			*flag |= (SCHED_CPUFREQ_WALT | SCHED_CPUFREQ_BOOST);
		} else if (ux_task_load[cpu] && !test_task_ux(rq->curr)) {
			ux_task_load[cpu] = 0;
			*flag |= (SCHED_CPUFREQ_WALT | SCHED_CPUFREQ_BOOST);
		}
	}
}

int sched_frame_rate_handler(struct ctl_table *table, int write, void __user *buffer, size_t *lenp, loff_t *ppos)
{
	int ret;

	if (write && *ppos)
		*ppos = 0;

	ret = proc_dointvec(table, write, buffer, lenp, ppos);

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
	sched_assist_input_boost_duration  = jiffies_to_msecs(jiffies) + DEFAULT_INPUT_BOOST_DURATION / sysctl_sched_assist_ib_duration_coedecay;
#ifndef CONFIG_MACH_MT6877
	sysctl_boost_task_threshold = 102;
#endif
out:
	mutex_unlock(&sa_boost_mutex);
	return result;
}
void oplus_get_cpu_util_mtk(int cpu, u64 *walt_cpu_util, int *boosted)
{
	if (sysctl_sched_assist_enabled && (sched_assist_scene(SA_SLIDE) ||
			sched_assist_scene(SA_LAUNCHER_SI) ||
			sched_assist_scene(SA_INPUT)) && ux_task_load[cpu]) {
		*boosted = true;
		*walt_cpu_util = cpu_rq(cpu)->prev_runnable_sum;
		slide_set_boost_load(walt_cpu_util, cpu);
		*walt_cpu_util <<= SCHED_CAPACITY_SHIFT;
#ifdef CONFIG_OPLUS_SYSTEM_KERNEL_QCOM
		do_div(*walt_cpu_util, sched_ravg_window);
#else
		do_div(*walt_cpu_util, walt_ravg_window);
#endif
	}
}
