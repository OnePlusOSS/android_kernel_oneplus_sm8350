/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020 Oplus. All rights reserved.
 */


#ifndef _OPLUS_SCHED_SLIDE_H_
#define _OPLUS_SCHED_SLIDE_H_

#include <linux/version.h>
#include "sched_assist_common.h"

extern unsigned int sched_ravg_window;
extern unsigned int walt_scale_demand_divisor;
#define scale_demand(d) ((d)/walt_scale_demand_divisor)

extern int sysctl_slide_boost_enabled;
extern int sysctl_boost_task_threshold;
extern int sysctl_frame_rate;

extern bool oplus_task_misfit(struct task_struct *p, int cpu);
extern bool _slide_task_misfit(struct task_struct *p, int cpu);
extern u64 _slide_get_boost_load(int cpu);
void _slide_find_start_cpu(struct root_domain *rd, struct task_struct *p, int *start_cpu);

extern void slide_calc_boost_load(struct rq *rq, unsigned int *flag, int cpu);
extern int sched_frame_rate_handler(struct ctl_table *table, int write, void __user *buffer, size_t *lenp, loff_t *ppos);
extern int sysctl_sched_slide_boost_handler(struct ctl_table *table, int write, void __user *buffer, size_t *lenp, loff_t *ppos);
extern int sysctl_sched_animation_type_handler(struct ctl_table *table, int write, void __user *buffer, size_t *lenp, loff_t *ppos);
extern int sysctl_sched_boost_task_threshold_handler(struct ctl_table *table, int write, void __user *buffer, size_t *lenp, loff_t *ppos);
extern int sysctl_sched_assist_input_boost_ctrl_handler(struct ctl_table *table, int write, void __user *buffer, size_t *lenp, loff_t *ppos);

static inline bool slide_task_misfit(struct task_struct *p, int cpu)
{
	return (sched_assist_scene(SA_SLIDE) ||  sched_assist_scene(SA_LAUNCHER_SI) || sched_assist_scene(SA_INPUT)) &&
		is_heavy_ux_task(p) && oplus_task_misfit(p, cpu);
}

static inline void slide_set_start_cpu(struct root_domain *rd, struct task_struct *p, int *start_cpu)
{
	if ((sched_assist_scene(SA_SLIDE) ||  sched_assist_scene(SA_LAUNCHER_SI) ||
			sched_assist_scene(SA_INPUT)) && is_heavy_ux_task(p))
		_slide_find_start_cpu(rd, p, start_cpu);
}

static inline void slide_set_task_cpu(struct task_struct *p, int *best_energy_cpu)
{
	if (slide_task_misfit(p, *best_energy_cpu))
		find_ux_task_cpu(p, best_energy_cpu);
}

static inline void slide_set_boost_load(u64 *load, int cpu)
{
	u64 tmpload = *load;

	if (sched_assist_scene(SA_SLIDE) ||  sched_assist_scene(SA_LAUNCHER_SI) || sched_assist_scene(SA_INPUT)) {
		tmpload = max_t(u64, tmpload, _slide_get_boost_load(cpu));
		*load = tmpload;
	}
}

#endif /* _OPLUS_SCHED_SLIDE_H_ */
