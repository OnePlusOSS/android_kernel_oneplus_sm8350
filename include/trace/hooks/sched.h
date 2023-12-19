/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM sched
#define TRACE_INCLUDE_PATH trace/hooks
#if !defined(_TRACE_HOOK_SCHED_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_SCHED_H
#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>
/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */
 #if defined(CONFIG_TRACEPOINTS) && defined(CONFIG_ANDROID_VENDOR_HOOKS)
struct task_struct;
DECLARE_RESTRICTED_HOOK(android_rvh_select_task_rq_fair,
	TP_PROTO(struct task_struct *p, int prev_cpu, int sd_flag, int wake_flags, int *new_cpu),
	TP_ARGS(p, prev_cpu, sd_flag, wake_flags, new_cpu), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_select_task_rq_rt,
	TP_PROTO(struct task_struct *p, int prev_cpu, int sd_flag, int wake_flags, int *new_cpu),
	TP_ARGS(p, prev_cpu, sd_flag, wake_flags, new_cpu), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_select_fallback_rq,
	TP_PROTO(int cpu, struct task_struct *p, int *new_cpu),
	TP_ARGS(cpu, p, new_cpu), 1);

struct rq;
DECLARE_HOOK(android_vh_scheduler_tick,
	TP_PROTO(struct rq *rq),
	TP_ARGS(rq));

DECLARE_RESTRICTED_HOOK(android_rvh_enqueue_task,
	TP_PROTO(struct rq *rq, struct task_struct *p),
	TP_ARGS(rq, p), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_dequeue_task,
	TP_PROTO(struct rq *rq, struct task_struct *p),
	TP_ARGS(rq, p), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_can_migrate_task,
	TP_PROTO(struct task_struct *p, int dst_cpu, int *can_migrate),
	TP_ARGS(p, dst_cpu, can_migrate), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_find_lowest_rq,
	TP_PROTO(struct task_struct *p, struct cpumask *local_cpu_mask,
			int *lowest_cpu),
	TP_ARGS(p, local_cpu_mask, lowest_cpu), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_prepare_prio_fork,
	TP_PROTO(struct task_struct *p),
	TP_ARGS(p), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_finish_prio_fork,
	TP_PROTO(struct task_struct *p),
	TP_ARGS(p), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_rtmutex_prepare_setprio,
	TP_PROTO(struct task_struct *p, struct task_struct *pi_task),
	TP_ARGS(p, pi_task), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_set_user_nice,
	TP_PROTO(struct task_struct *p, long *nice, bool *allowed),
	TP_ARGS(p, nice, allowed), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_setscheduler,
	TP_PROTO(struct task_struct *p),
	TP_ARGS(p), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_check_preempt_wakeup,
	TP_PROTO(struct task_struct *p, int *ignore),
	TP_ARGS(p, ignore), 1);
DECLARE_RESTRICTED_HOOK(android_rvh_check_preempt_tick,
	TP_PROTO(struct task_struct *p, unsigned long *ideal_runtime),
	TP_ARGS(p, ideal_runtime), 1);
DECLARE_RESTRICTED_HOOK(android_rvh_find_best_target,
	TP_PROTO(struct task_struct *p, int cpu, int *ignore),
	TP_ARGS(p, cpu, ignore), 1);
struct cpumask;
DECLARE_RESTRICTED_HOOK(android_rvh_cpupri_find_fitness,
	TP_PROTO(struct task_struct *p, struct cpumask *lowest_mask),
	TP_ARGS(p, lowest_mask), 1);
struct sched_group;
DECLARE_RESTRICTED_HOOK(android_rvh_find_busiest_group,
	TP_PROTO(struct sched_group *busiest, struct rq *dst_rq, int *out_balance),
		TP_ARGS(busiest, dst_rq, out_balance), 1);
#else
#define trace_android_rvh_select_task_rq_fair(p, prev_cpu, sd_flag, wake_flags, new_cpu)
#define trace_android_rvh_select_task_rq_rt(p, prev_cpu, sd_flag, wake_flags, new_cpu)
#define trace_android_rvh_select_fallback_rq(cpu, p, dest_cpu)
#define trace_android_vh_scheduler_tick(rq)
#define trace_android_rvh_enqueue_task(rq, p)
#define trace_android_rvh_dequeue_task(rq, p)
#define trace_android_rvh_can_migrate_task(p, dst_cpu, can_migrate)
#define trace_android_rvh_find_lowest_rq(p, local_cpu_mask, lowest_cpu)
#define trace_android_rvh_prepare_prio_fork(p)
#define trace_android_rvh_finish_prio_fork(p)
#define trace_android_rvh_rtmutex_prepare_setprio(p, pi_task)
#define trace_android_rvh_set_user_nice(p, nice)
#define trace_android_rvh_setscheduler(p)
#define trace_android_rvh_check_preempt_wakeup(p, ignore)
#define trace_android_rvh_check_preempt_tick(p, ideal_runtime)
#define trace_android_rvh_find_best_target(p, cpu, ignore)
#define trace_android_rvh_cpupri_find_fitness(p, lowest_mask)
#define trace_android_rvh_find_busiest_group(busiest, dst_rq, out_balance)
#endif

DECLARE_HOOK(android_vh_map_util_freq,
	TP_PROTO(unsigned long util, unsigned long freq,
		unsigned long cap, unsigned long *next_freq),
	TP_ARGS(util, freq, cap, next_freq));

struct em_perf_domain;
DECLARE_HOOK(android_vh_em_pd_energy,
	TP_PROTO(struct em_perf_domain *pd,
		unsigned long max_util, unsigned long sum_util,
		unsigned long *energy),
	TP_ARGS(pd, max_util, sum_util, energy));

/* macro versions of hooks are no longer required */

#endif /* _TRACE_HOOK_SCHED_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
