// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 Oplus. All rights reserved.
 */


#include <linux/version.h>
#include <linux/sched.h>
#include <linux/list.h>
#include <linux/jiffies.h>
#include <trace/events/sched.h>
#include <../kernel/sched/sched.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <../fs/proc/internal.h>
#include <linux/moduleparam.h>
#include <linux/kallsyms.h>
#include <linux/trace_events.h>
#include <linux/sched_assist/sched_assist_common.h>
#include <linux/sched_assist/sched_assist_slide.h>
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0)) || defined(CONFIG_MMAP_LOCK_OPT)
#include <linux/mm.h>
#include <linux/rwsem.h>
#endif

#include <../kernel/sched/walt/walt.h>

#define THRESHOLD_BOOST 102

int ux_min_sched_delay_granularity;
int ux_max_inherit_exist = 1000;
int ux_max_inherit_granularity = 32;
int ux_min_migration_delay = 10;
int ux_max_over_thresh = 2000;

/* for slide boost */
int sysctl_animation_type = 0;
int sysctl_input_boost_enabled = 0;
int sysctl_sched_assist_ib_duration_coedecay = 1;
u64 sched_assist_input_boost_duration = 0;
int sched_assist_ib_duration_coedecay = 1;
#ifdef CONFIG_OPLUS_FEATURE_AUDIO_OPT
int sysctl_sched_impt_tgid = 0;
#endif

#define S2NS_T 1000000

static int param_ux_debug;
module_param_named(debug, param_ux_debug, uint, 0644);

#define MAX_IMPT_SAVE_PID (2)
pid_t save_impt_tgid[MAX_IMPT_SAVE_PID];
pid_t save_top_app_tgid;
unsigned int top_app_type;
struct cpumask nr_mask;

struct ux_sched_cputopo ux_sched_cputopo;

#ifdef CONFIG_OPLUS_FEATURE_SCHED_SPREAD
DEFINE_PER_CPU(struct task_count_rq, task_lb_count);
void init_rq_cpu(int cpu)
{
	per_cpu(task_lb_count, cpu).ux_low = 0;
	per_cpu(task_lb_count, cpu).ux_high = 0;
	per_cpu(task_lb_count, cpu).top_low = 0;
	per_cpu(task_lb_count, cpu).top_high = 0;
	per_cpu(task_lb_count, cpu).foreground_low = 0;
	per_cpu(task_lb_count, cpu).foreground_high = 0;
	per_cpu(task_lb_count, cpu).background_low = 0;
	per_cpu(task_lb_count, cpu).background_high = 0;
}
#endif

static inline void sched_init_ux_cputopo(void)
{
	int i = 0;

	ux_sched_cputopo.cls_nr = 0;
	for (i = 0; i < num_possible_cpus(); ++i) {
		cpumask_clear(&ux_sched_cputopo.sched_cls[i].cpus);
		ux_sched_cputopo.sched_cls[i].capacity = ULONG_MAX;
	}
}

void update_ux_sched_cputopo(void)
{
	unsigned long prev_cap = 0;
	unsigned long cpu_cap = 0;
	unsigned int cpu = 0;
	int i, insert_idx, cls_nr;
	struct ux_sched_cluster sched_cls;

	/* reset prev cpu topo info */
	sched_init_ux_cputopo();

	/* update new cpu topo info */
	for_each_possible_cpu(cpu) {
		cpu_cap = arch_scale_cpu_capacity(cpu);

		/* add cpu with same capacity into target sched_cls */
		if (cpu_cap == prev_cap) {
			for (i = 0; i < ux_sched_cputopo.cls_nr; ++i) {
				if (cpu_cap == ux_sched_cputopo.sched_cls[i].capacity) {
					cpumask_set_cpu(cpu, &ux_sched_cputopo.sched_cls[i].cpus);
					break;
				}
			}
			continue;
		}

		cpumask_clear(&sched_cls.cpus);
		cpumask_set_cpu(cpu, &sched_cls.cpus);
		sched_cls.capacity = cpu_cap;
		cls_nr = ux_sched_cputopo.cls_nr;

		if (!cls_nr) {
			ux_sched_cputopo.sched_cls[cls_nr] = sched_cls;
		} else {
			for (i = 0; i <= ux_sched_cputopo.cls_nr; ++i) {
				if (sched_cls.capacity < ux_sched_cputopo.sched_cls[i].capacity) {
					insert_idx = i;
					break;
				}
			}

			if (insert_idx == ux_sched_cputopo.cls_nr) {
				ux_sched_cputopo.sched_cls[insert_idx] = sched_cls;
			} else {
				for (; cls_nr > insert_idx; cls_nr--)
					ux_sched_cputopo.sched_cls[cls_nr] = ux_sched_cputopo.sched_cls[cls_nr-1];

				ux_sched_cputopo.sched_cls[insert_idx] = sched_cls;
			}
		}
		ux_sched_cputopo.cls_nr++;
		prev_cap = cpu_cap;
	}

	for (i = 0; i < ux_sched_cputopo.cls_nr; i++)
		ux_debug("update ux sched cpu topology [cls_nr:%d cpus:%*pbl cap:%lu]",
			i, cpumask_pr_args(&ux_sched_cputopo.sched_cls[i].cpus), ux_sched_cputopo.sched_cls[i].capacity);
}

static inline struct task_struct *task_of(struct sched_entity *se)
{
	return container_of(se, struct task_struct, se);
}

static int entity_before(struct sched_entity *a, struct sched_entity *b)
{
	return (s64)(a->vruntime - b->vruntime) < 0;
}

static int entity_over(struct sched_entity *a, struct sched_entity *b)
{
	return (s64)(a->vruntime - b->vruntime) > (s64)ux_max_over_thresh * S2NS_T;
}

extern const struct sched_class fair_sched_class;

/* identify ux only opt in some case, but always keep it's id_type, and wont do inherit through test_task_ux() */
bool test_task_identify_ux(struct task_struct *task, int id_type_ux)
{
	if (id_type_ux == SA_TYPE_ID_CAMERA_PROVIDER) {
		struct task_struct *grp_leader = task->group_leader;

		/* consider provider's HwBinder in configstream */
		if ((task->ux_state & SA_TYPE_LISTPICK) && (grp_leader->ux_state & SA_TYPE_ID_CAMERA_PROVIDER))
			return true;
		return (task->ux_state & SA_TYPE_ID_CAMERA_PROVIDER) && (sysctl_sched_assist_scene & SA_CAMERA);
	} else if (id_type_ux == SA_TYPE_ID_ALLOCATOR_SER) {
		if (task && (task->ux_state & SA_TYPE_ID_ALLOCATOR_SER) && (sysctl_sched_assist_scene & SA_CAMERA))
			return true;
	}

	return false;
}

inline bool test_task_ux(struct task_struct *task)
{
	if (unlikely(!sysctl_sched_assist_enabled))
		return false;

	if (!task)
		return false;

	if (task->sched_class != &fair_sched_class)
		return false;

#ifdef CONFIG_OPLUS_FEATURE_SCHED_SPREAD
	/* during gesture animation, top application should not be optimized except launcher */
	if (sched_assist_scene(SA_ANIM) && save_top_app_tgid &&
			(task->tgid == save_top_app_tgid) && (top_app_type != 1))
		return false;
#endif

	if (task->ux_state & (SA_TYPE_HEAVY | SA_TYPE_LIGHT | SA_TYPE_ANIMATOR | SA_TYPE_LISTPICK))
		return true;

	return false;
}

#ifdef CONFIG_OPLUS_FEATURE_SCHED_SPREAD
#define NR_IMBALANCE_THRESHOLD (24)
void update_rq_nr_imbalance(int cpu)
{
	int total_nr = 0;
	int i = -1;
	int threshold = NR_IMBALANCE_THRESHOLD;

	/* Note: check without holding rq lock */
	for_each_cpu(i, cpu_active_mask) {
		total_nr += cpu_rq(i)->cfs.nr_running;
		if (idle_cpu(i))
			cpumask_clear_cpu(i, &nr_mask);
	}

	if (!idle_cpu(cpu) && (total_nr >= threshold))
		cpumask_set_cpu(cpu, &nr_mask);
	else
		cpumask_clear_cpu(cpu, &nr_mask);
}

bool should_force_spread_tasks(void)
{
	return !cpumask_empty(&nr_mask);
}

#ifdef CONFIG_CGROUP_SCHED
static inline int task_cgroup_id(struct task_struct *task)
{
	struct cgroup_subsys_state *css = task_css(task, cpu_cgrp_id);

	return css ? css->id : -1;
}
#else
static inline int task_cgroup_id(struct task_struct *task)
{
	return -EINVAL;
}
#endif

int task_lb_sched_type(struct task_struct *tsk)
{
	int cgroup_type = task_cgroup_id(tsk);

	if (test_task_ux(tsk))
		return SA_UX;
	else if (cgroup_type == SA_CGROUP_TOP_APP)
		return SA_TOP;
	else if (cgroup_type == SA_CGROUP_FOREGROUD || cgroup_type == SA_CGROUP_DEFAULT)
		return SA_FG;
	else if (cgroup_type == SA_CGROUP_BACKGROUD)
		return SA_BG;

	return -EINVAL;
}

#ifdef CONFIG_SCHED_WALT
bool task_high_load(struct task_struct *tsk)
{
	unsigned int cpu = 0;
	unsigned long capacity = capacity_orig_of(cpu);
	unsigned long max_capacity = cpu_rq(cpu)->rd->max_cpu_capacity.val;
	unsigned int margin;
	int sched_type = task_lb_sched_type(tsk);
	unsigned long load = tsk->wts.demand_scaled;

	if (sched_type == SA_BG)
		load = min(load, tsk->se.avg.util_avg);
	load = clamp(load,
		     uclamp_eff_value(tsk, UCLAMP_MIN),
		     uclamp_eff_value(tsk, UCLAMP_MAX));
	if (capacity == max_capacity)
		return true;

	if (capacity_orig_of(task_cpu(tsk)) > capacity_orig_of(cpu))
		margin = sched_capacity_margin_down[cpu];
	else
		margin = sched_capacity_margin_up[task_cpu(tsk)];

	return capacity * 1024 < load * margin;
}
#endif

void dec_task_lb(struct task_struct *tsk, struct rq *rq,
	int high_load, int task_type)
{
	int cpu = cpu_of(rq);

	if (high_load == SA_HIGH_LOAD) {
		switch (task_type) {
		case SA_UX:
			per_cpu(task_lb_count, cpu).ux_high--;
			break;
		case SA_TOP:
			per_cpu(task_lb_count, cpu).top_high--;
			break;
		case SA_FG:
			per_cpu(task_lb_count, cpu).foreground_high--;
			break;
		case SA_BG:
			per_cpu(task_lb_count, cpu).background_high--;
			break;
		}
	} else if (high_load == SA_LOW_LOAD) {
		switch (task_type) {
		case SA_UX:
			per_cpu(task_lb_count, cpu).ux_low--;
			break;
		case SA_TOP:
			per_cpu(task_lb_count, cpu).top_low--;
			break;
		case SA_FG:
			per_cpu(task_lb_count, cpu).foreground_low--;
			break;
		case SA_BG:
			per_cpu(task_lb_count, cpu).background_low--;
			break;
		}
	}
}

void inc_task_lb(struct task_struct *tsk, struct rq *rq,
	int high_load, int task_type)
{
	int cpu = cpu_of(rq);

	if (high_load == SA_HIGH_LOAD) {
		switch (task_type) {
		case SA_UX:
			per_cpu(task_lb_count, cpu).ux_high++;
			break;
		case SA_TOP:
			per_cpu(task_lb_count, cpu).top_high++;
			break;
		case SA_FG:
			per_cpu(task_lb_count, cpu).foreground_high++;
			break;
		case SA_BG:
			per_cpu(task_lb_count, cpu).background_high++;
			break;
		}
	} else if (high_load == SA_LOW_LOAD) {
		switch (task_type) {
		case SA_UX:
			per_cpu(task_lb_count, cpu).ux_low++;
			break;
		case SA_TOP:
			per_cpu(task_lb_count, cpu).top_low++;
			break;
		case SA_FG:
			per_cpu(task_lb_count, cpu).foreground_low++;
			break;
		case SA_BG:
			per_cpu(task_lb_count, cpu).background_low++;
			break;
		}
	}
}

void update_load_flag(struct task_struct *tsk, struct rq *rq)
{
	int curr_high_load = task_high_load(tsk);
	int curr_task_type = task_lb_sched_type(tsk);

	if (tsk->lb_state != 0) {
		int prev_high_load = tsk->lb_state & 0x1;
		int prev_task_type = (tsk->lb_state >> 1) & 0x7;

		if (prev_high_load == curr_high_load && prev_task_type == curr_task_type)
			return;
		else
			dec_task_lb(tsk, rq, prev_high_load, prev_task_type);
	}
	inc_task_lb(tsk, rq, curr_high_load, curr_task_type);
	tsk->lb_state = (curr_task_type << 1) | curr_high_load;
}

void inc_ld_stats(struct task_struct *tsk, struct rq *rq)
{
	int curr_high_load = tsk->lb_state & 0x1;
	int curr_task_type = (tsk->lb_state >> 1) & 0x7;

	inc_task_lb(tsk, rq, curr_high_load, curr_task_type);
	tsk->ld_flag = 1;
}

void dec_ld_stats(struct task_struct *tsk, struct rq *rq)
{
	int curr_high_load = tsk->lb_state & 0x1;
	int curr_task_type = (tsk->lb_state >> 1) & 0x7;

	tsk->ld_flag = 0;
	dec_task_lb(tsk, rq, curr_high_load, curr_task_type);
}

#define MAX_ADJ_NICE (5)
#define MAX_ADJ_PRIO (DEFAULT_PRIO + MAX_ADJ_NICE - 1)
const int sched_prio_to_weight_bg[MAX_ADJ_NICE] = { 716, 601, 510, 438, 380, };
const int sched_prio_to_wmult_bg[MAX_ADJ_NICE] = { 5998558, 7146368, 8421505, 9805861, 11302546, };

/* keep same as defined in sched/fair.c */
#define OPLUS_WMULT_CONST	(~0U)
#define OPLUS_WMULT_SHIFT	32

/* keep same as defined in sched/fair.c */
static void __oplus_update_inv_weight(struct load_weight *lw)
{
	unsigned long w;

	if (likely(lw->inv_weight))
		return;

	w = scale_load_down(lw->weight);

	if (BITS_PER_LONG > 32 && unlikely(w >= OPLUS_WMULT_CONST))
		lw->inv_weight = 1;
	else if (unlikely(!w))
		lw->inv_weight = OPLUS_WMULT_CONST;
	else
		lw->inv_weight = OPLUS_WMULT_CONST / w;
}

static struct load_weight sa_new_weight(struct sched_entity *se)
{
	struct task_struct *task = NULL;
	struct load_weight lw;
	int sched_type = -1;
	int idx = -1;
	int i = 0;

	if ((sysctl_sched_assist_enabled < 2) || !entity_is_task(se) || sched_assist_scene(SA_CAMERA))
		return se->load;

	task = container_of(se, struct task_struct, se);
	if ((task->static_prio < DEFAULT_PRIO) || (task->static_prio > MAX_ADJ_PRIO) || (task_util(task) < 51))
		return se->load;

	/* skip important process such as audio proc */
	for (; i < MAX_IMPT_SAVE_PID; ++i) {
		if (save_impt_tgid[i] && (task->tgid == save_impt_tgid[i]))
			return se->load;
	}

	/* we only adjust background group's load weight */
	sched_type = task_lb_sched_type(task);
	if (sched_type != SA_BG)
		return se->load;

	idx = task->static_prio - DEFAULT_PRIO;
	lw.weight = scale_load(sched_prio_to_weight_bg[idx]);
	lw.inv_weight = sched_prio_to_wmult_bg[idx];

	return lw;
}

u64 sa_calc_delta(struct sched_entity *se, u64 delta_exec, unsigned long weight, struct load_weight *lw, bool calc_fair)
{
	u64 fact = 0;
	u32 inv_weight = 0;
	int shift = OPLUS_WMULT_SHIFT;
	struct load_weight sa_lw = sa_new_weight(se);

	if (calc_fair && (sa_lw.weight == lw->weight) && (lw->weight == NICE_0_LOAD))
		return delta_exec;

	__oplus_update_inv_weight(lw);

	if (calc_fair) {
		/*_calc_delta(delta, NICE_0_LOAD, &se->load) */
		fact = scale_load_down(weight);
		inv_weight = sa_lw.inv_weight;
	} else {
		/*_calc_delta(slice, sa_lw.weight, load) */
		fact = scale_load_down(sa_lw.weight);
		inv_weight = lw->inv_weight;
	}

	if (unlikely(fact >> 32)) {
		while (fact >> 32) {
			fact >>= 1;
			shift--;
		}
	}

	/* hint to use a 32x32->64 mul */
	fact = (u64)(u32)fact * inv_weight;

	while (fact >> 32) {
		fact >>= 1;
		shift--;
	}

	return mul_u64_u32_shr(delta_exec, fact, shift);
}

bool should_force_adjust_vruntime(struct sched_entity *se)
{
	struct task_struct *se_task = NULL;

	if (sysctl_sched_assist_enabled < 2 || sched_assist_scene(SA_CAMERA))
		return false;

	if (!entity_is_task(se))
		return false;

	se_task = task_of(se);
	/* requeue runnable inherit ux task should be adjusted */
	if (se_task && (se_task->ux_state & SA_TYPE_INHERIT))
		return true;

	return false;
}

void sched_assist_spread_tasks(struct task_struct *p, cpumask_t new_allowed_cpus,
		int order_index, int end_index, int skip_cpu, cpumask_t *cpus, bool strict)
{
	int sched_type = task_lb_sched_type(p);
	int cluster, i;
	cpumask_t visit_cpus;
	int lowest_nr = INT_MAX;
	int lowest_nr_load = INT_MAX;
	int lowest_nr_cpu = -1;
	int prev_cpu = task_cpu(p);
	bool force_spread = false;

	if (sysctl_sched_assist_enabled < 2 || sched_assist_scene(SA_CAMERA))
		return;

	if (num_sched_clusters <= 1 || sched_type == -1)
		return;

	/* add for launch boost */
	if (sched_assist_scene(SA_LAUNCH) && is_heavy_ux_task(p)) {
		strict = true;
		order_index = num_sched_clusters - 1;
		end_index = (p->pid == p->tgid) ? 0 : 1;
	}

	/* in launch or animation scene, force scheduler to spread tasks */
	if (should_force_spread_tasks())
		force_spread = true;
	else
		return;

	for (cluster = 0; cluster < num_sched_clusters; cluster++) {
		cpumask_and(&visit_cpus, &new_allowed_cpus,
				&cpu_array[order_index][cluster]);
		for_each_cpu(i, &visit_cpus) {
			int ux_nr = 0;
			int top_nr = 0;
			int fg_nr = 0;
			int bg_nr = 0;
			int rq_nr = 0;
			int rq_nr_load = 0;

			if (!cpu_active(i) || cpu_isolated(i))
				continue;

			if (is_reserved(i))
				continue;

			if (sched_cpu_high_irqload(i))
				continue;

			if (skip_cpu == i)
				continue;

			ux_nr = per_cpu(task_lb_count, i).ux_high + per_cpu(task_lb_count, i).ux_low;
			top_nr = per_cpu(task_lb_count, i).top_high + per_cpu(task_lb_count, i).top_low;
			fg_nr = per_cpu(task_lb_count, i).foreground_high + per_cpu(task_lb_count, i).foreground_low;
			bg_nr = per_cpu(task_lb_count, i).background_high + per_cpu(task_lb_count, i).background_low;

			if (sched_type == SA_UX)
				rq_nr = ux_nr;
			else if (sched_type == SA_TOP)
				rq_nr = ux_nr + top_nr;
			else if (sched_type == SA_FG)
				rq_nr = ux_nr + top_nr + fg_nr;
			else if (sched_type == SA_BG)
				rq_nr = ux_nr + top_nr + fg_nr + bg_nr;

			rq_nr_load = 1000 * ux_nr + 100 * top_nr + 10 * fg_nr + bg_nr;

			if (rq_nr > lowest_nr)
				continue;

			if (rq_nr == lowest_nr) {
				if (rq_nr_load < lowest_nr_load)
					goto find;
				if (rq_nr_load == lowest_nr_load && i == prev_cpu)
					goto find;

				continue;
			}
find:
			lowest_nr = rq_nr;
			lowest_nr_load = rq_nr_load;
			lowest_nr_cpu = i;
		}

		/* should we visit next cluster? */
		if (strict && cluster >= end_index)
			break;

		if (force_spread)
			continue;

		break;
	}

	if (lowest_nr_cpu != -1)
		cpumask_set_cpu(lowest_nr_cpu, cpus);
}
#endif

inline int get_ux_state_type(struct task_struct *task)
{
	if (!task)
		return UX_STATE_INVALID;

	if (task->sched_class != &fair_sched_class)
		return UX_STATE_INVALID;

	if (task->ux_state & SA_TYPE_INHERIT)
		return UX_STATE_INHERIT;

	if (task->ux_state & (SA_TYPE_HEAVY | SA_TYPE_LIGHT | SA_TYPE_ANIMATOR | SA_TYPE_LISTPICK))
		return UX_STATE_SCHED_ASSIST;

	return UX_STATE_NONE;
}

inline bool test_list_pick_ux(struct task_struct *task)
{
	return (task->ux_state & SA_TYPE_LISTPICK) || (task->ux_state & SA_TYPE_ONCE_UX) ||
		test_task_identify_ux(task, SA_TYPE_ID_ALLOCATOR_SER);
}

void enqueue_ux_thread(struct rq *rq, struct task_struct *p)
{
	struct list_head *pos, *n;
	bool exist = false;

	if (unlikely(!sysctl_sched_assist_enabled))
		return;

	if (!rq || !p || !list_empty(&p->ux_entry))
		return;

	p->enqueue_time = rq->clock;
	if (test_list_pick_ux(p)) {
		list_for_each_safe(pos, n, &rq->ux_thread_list) {
			if (pos == &p->ux_entry) {
				exist = true;
				break;
			}
		}
		if (!exist) {
			list_add_tail(&p->ux_entry, &rq->ux_thread_list);
			get_task_struct(p);
		}
	}
}

#define ux_max_dynamic_granularity ((u64)(64 * S2NS_T))
void dequeue_ux_thread(struct rq *rq, struct task_struct *p)
{
	struct list_head *pos, *n;

	if (!rq || !p)
		return;

	p->enqueue_time = 0;
	if (!list_empty(&p->ux_entry)) {
		u64 now = jiffies_to_nsecs(jiffies);

		list_for_each_safe(pos, n, &rq->ux_thread_list) {
			if (pos == &p->ux_entry) {
				list_del_init(&p->ux_entry);
				if (p->ux_state & SA_TYPE_ONCE_UX)
					p->ux_state &= ~SA_TYPE_ONCE_UX;

				if (get_ux_state_type(p) == UX_STATE_INHERIT &&
					now - p->inherit_ux_start > ux_max_dynamic_granularity)
					p->ux_state &= ~SA_TYPE_LISTPICK;

				put_task_struct(p);
				return;
			}
		}
	}
}

static struct task_struct *pick_first_ux_thread(struct rq *rq)
{
	struct list_head *ux_thread_list = &rq->ux_thread_list;
	struct list_head *pos = NULL;
	struct list_head *n = NULL;
	struct task_struct *temp = NULL;
	struct task_struct *leftmost_task = NULL;

	list_for_each_safe(pos, n, ux_thread_list) {
		temp = list_entry(pos, struct task_struct, ux_entry);
		/*ensure ux task in current rq cpu otherwise delete it*/
		if (unlikely(task_cpu(temp) != rq->cpu)) {
			list_del_init(&temp->ux_entry);
			put_task_struct(temp);
			continue;
		}
		if (unlikely(!test_list_pick_ux(temp))) {
			list_del_init(&temp->ux_entry);
			put_task_struct(temp);
			continue;
		}

		if (leftmost_task == NULL)
			leftmost_task = temp;
		else if (entity_before(&temp->se, &leftmost_task->se))
			leftmost_task = temp;
	}

	return leftmost_task;
}

void pick_ux_thread(struct rq *rq, struct task_struct **p, struct sched_entity **se)
{
	struct task_struct *ori_p = *p;
	struct task_struct *key_task;
	struct sched_entity *key_se;

	if (!rq || !ori_p || !se)
		return;

#ifdef CONFIG_OPLUS_FEATURE_AUDIO_OPT
	if ((*p)->oplus_task_info.im_small)
		return;
#endif
	if ((ori_p->ux_state & SA_TYPE_ANIMATOR) || test_list_pick_ux(ori_p))
		return;

	if (!list_empty(&rq->ux_thread_list)) {
		key_task = pick_first_ux_thread(rq);
		/* in case that ux thread keep running too long */
		if (key_task && entity_over(&key_task->se, &ori_p->se))
			return;

		if (key_task) {
			key_se = &key_task->se;
			if (key_se && (rq->clock >= key_task->enqueue_time) &&
			rq->clock - key_task->enqueue_time >= ((u64)ux_min_sched_delay_granularity * S2NS_T)) {
				*p = key_task;
				*se = key_se;
			}
		}
	}
}

#define INHERIT_UX_SEC_WIDTH   8
#define INHERIT_UX_MASK_BASE   0x00000000ff

#define inherit_ux_offset_of(type) (type * INHERIT_UX_SEC_WIDTH)
#define inherit_ux_mask_of(type) ((u64)(INHERIT_UX_MASK_BASE) << (inherit_ux_offset_of(type)))
#define inherit_ux_get_bits(value, type) ((value & inherit_ux_mask_of(type)) >> inherit_ux_offset_of(type))
#define inherit_ux_value(type, value) ((u64)value << inherit_ux_offset_of(type))


bool test_inherit_ux(struct task_struct *task, int type)
{
	u64 inherit_ux;

	if (!task)
		return false;

	inherit_ux = atomic64_read(&task->inherit_ux);
	return inherit_ux_get_bits(inherit_ux, type) > 0;
}

static bool test_task_exist(struct task_struct *task, struct list_head *head)
{
	struct list_head *pos, *n;

	list_for_each_safe(pos, n, head) {
		if (pos == &task->ux_entry)
			return true;
	}

	return false;
}

inline void inherit_ux_inc(struct task_struct *task, int type)
{
	atomic64_add(inherit_ux_value(type, 1), &task->inherit_ux);
}

inline void inherit_ux_sub(struct task_struct *task, int type, int value)
{
	atomic64_sub(inherit_ux_value(type, value), &task->inherit_ux);
}

static void __inherit_ux_dequeue(struct task_struct *task, int type, int value)
{
	struct rq_flags flags;
	bool exist = false;
	struct rq *rq = NULL;
	u64 inherit_ux = 0;

	rq = task_rq_lock(task, &flags);
	inherit_ux = atomic64_read(&task->inherit_ux);
	if (inherit_ux <= 0) {
		task_rq_unlock(rq, task, &flags);
		return;
	}
	inherit_ux_sub(task, type, value);
	inherit_ux = atomic64_read(&task->inherit_ux);
	if (inherit_ux > 0) {
		task_rq_unlock(rq, task, &flags);
		return;
	}
	task->ux_depth = 0;

	exist = test_task_exist(task, &rq->ux_thread_list);
	if (exist) {
		list_del_init(&task->ux_entry);
		put_task_struct(task);
	}
	task_rq_unlock(rq, task, &flags);
}

void inherit_ux_dequeue(struct task_struct *task, int type)
{
	if (!task || type >= INHERIT_UX_MAX)
		return;

	__inherit_ux_dequeue(task, type, 1);
}
void inherit_ux_dequeue_refs(struct task_struct *task, int type, int value)
{
	if (!task || type >= INHERIT_UX_MAX)
		return;

	__inherit_ux_dequeue(task, type, value);
}

static void __inherit_ux_enqueue(struct task_struct *task, int type, int depth)
{
	struct rq_flags flags;
	bool exist = false;
	struct rq *rq = NULL;

	rq = task_rq_lock(task, &flags);

	if (unlikely(!list_empty(&task->ux_entry))) {
		task_rq_unlock(rq, task, &flags);
		return;
	}

	inherit_ux_inc(task, type);
	task->inherit_ux_start = jiffies_to_nsecs(jiffies);
	task->ux_depth = task->ux_depth > depth + 1 ? task->ux_depth : depth + 1;
	if (task->state == TASK_RUNNING && task->sched_class == &fair_sched_class) {
		exist = test_task_exist(task, &rq->ux_thread_list);
		if (!exist) {
			get_task_struct(task);
			list_add_tail(&task->ux_entry, &rq->ux_thread_list);
		}
	}
	task_rq_unlock(rq, task, &flags);
}

void inherit_ux_enqueue(struct task_struct *task, int type, int depth)
{
	if (!task || type >= INHERIT_UX_MAX)
		return;

	__inherit_ux_enqueue(task, type, depth);
}

inline bool test_task_ux_depth(int ux_depth)
{
	return ux_depth < UX_DEPTH_MAX;
}

inline bool test_set_inherit_ux(struct task_struct *tsk)
{
	return tsk && test_task_ux(tsk) && test_task_ux_depth(tsk->ux_depth);
}

void ux_init_rq_data(struct rq *rq)
{
	if (!rq)
		return;

	INIT_LIST_HEAD(&rq->ux_thread_list);
#ifdef CONFIG_OPLUS_FEATURE_SCHED_SPREAD
	cpumask_clear(&nr_mask);

	per_cpu(task_lb_count, cpu_of(rq)).ux_low = 0;
	per_cpu(task_lb_count, cpu_of(rq)).ux_high = 0;
	per_cpu(task_lb_count, cpu_of(rq)).top_low = 0;
	per_cpu(task_lb_count, cpu_of(rq)).top_high = 0;
	per_cpu(task_lb_count, cpu_of(rq)).foreground_low = 0;
	per_cpu(task_lb_count, cpu_of(rq)).foreground_high = 0;
	per_cpu(task_lb_count, cpu_of(rq)).background_low = 0;
	per_cpu(task_lb_count, cpu_of(rq)).background_high = 0;
#endif
}

int ux_prefer_cpu[NR_CPUS] = {0};
void ux_init_cpu_data(void)
{
	int i = 0;
	int min_cpu = 0, ux_cpu = 0;

	for (; i < nr_cpu_ids; ++i)
		ux_prefer_cpu[i] = -1;

	ux_cpu = cpumask_weight(topology_core_cpumask(min_cpu));
	if (ux_cpu == 0) {
		ux_warn("failed to init ux cpu data\n");
		return;
	}

	for (i = 0; i < nr_cpu_ids && ux_cpu < nr_cpu_ids; ++i)
		ux_prefer_cpu[i] = ux_cpu++;
}

bool test_ux_task_cpu(int cpu)
{
	return (cpu >= ux_prefer_cpu[0]);
}

bool test_ux_prefer_cpu(struct task_struct *tsk, int cpu)
{
	struct root_domain *rd = cpu_rq(smp_processor_id())->rd;

	if (cpu < 0)
		return false;

	if (tsk->pid == tsk->tgid) {
#ifdef CONFIG_OPLUS_SYSTEM_KERNEL_QCOM
		return cpu >= rd->wrd.max_cap_orig_cpu;
#else
		return capacity_orig_of(cpu) >= rd->max_cpu_capacity.val;
#endif /*CONFIG_OPLUS_SYSTEM_KERNEL_QCOM*/
	}

	return (cpu >= ux_prefer_cpu[0]);
}

void find_ux_task_cpu(struct task_struct *tsk, int *target_cpu)
{
	int i = 0;
	int temp_cpu = 0;
	struct rq *rq = NULL;

	for (i = (nr_cpu_ids - 1); i >= 0; --i) {
		temp_cpu = ux_prefer_cpu[i];
		if (temp_cpu <= 0 || temp_cpu >= nr_cpu_ids)
			continue;

		rq = cpu_rq(temp_cpu);
		if (!rq)
			continue;

		if (rq->curr->prio <= MAX_RT_PRIO)
			continue;

		if (!test_task_ux(rq->curr) && cpu_online(temp_cpu) && !cpu_isolated(temp_cpu)
			&& cpumask_test_cpu(temp_cpu, tsk->cpus_ptr)) {
			*target_cpu = temp_cpu;
			return;
		}
	}
}

static inline bool oplus_is_min_capacity_cpu(int cpu)
{
	struct ux_sched_cputopo ux_cputopo = ux_sched_cputopo;
	int cls_nr = ux_cputopo.cls_nr - 1;

	if (unlikely(cls_nr <= 0))
		return false;

	return capacity_orig_of(cpu) == ux_cputopo.sched_cls[0].capacity;
}

#ifndef CONFIG_OPLUS_SYSTEM_KERNEL_QCOM
extern unsigned long capacity_curr_of(int cpu);
#endif
/*
 * taget cpu is
 *  unboost: the one in all domain, with lowest prio running task
 *  boost:   the one in power domain, with lowest prio running task which is not ux
 *  !!this func will ignore task's start cpu
 */
int set_ux_task_cpu_common_by_prio(struct task_struct *task, int *target_cpu, bool boost, bool prefer_idle, unsigned int type)
{
	int i;
	int lowest_prio = INT_MIN;
	unsigned long lowest_prio_max_cap = 0;
	int ret = -EINVAL;

	if (unlikely(!sysctl_sched_assist_enabled))
		return -EINVAL;

	if (!(task->ux_state & SA_TYPE_ANIMATOR)
			&& !test_task_identify_ux(task, SA_TYPE_ID_CAMERA_PROVIDER))
		return -EINVAL;

	if ((*target_cpu < 0) || (*target_cpu >= num_possible_cpus()))
		return -EINVAL;

#ifdef CONFIG_SCHED_WALT
	if (test_task_identify_ux(task, SA_TYPE_ID_CAMERA_PROVIDER)) {
		boost = ((scale_demand(task->wts.sum) >= THRESHOLD_BOOST) ||
			(task_util(task) >= THRESHOLD_BOOST)) ? true : false;
	}
#endif /* CONFIG_SCHED_WALT */

	for_each_cpu(i, cpu_active_mask) {
		unsigned long capacity_curr;
		struct task_struct *curr;
		bool curr_ux = false;

		if (!cpumask_test_cpu(i, task->cpus_ptr) || cpu_isolated(i))
			continue;

		/* avoid placing task into high power cpu and break it's idle state if !prefer_idle */
		if (prefer_idle && idle_cpu(i)) {
			*target_cpu = i;
			return 0;
		}

		curr = cpu_rq(i)->curr;
		/* avoid placing task into cpu with rt */
		if (!curr || !(curr->sched_class == &fair_sched_class))
			continue;

		curr_ux = test_task_ux(curr) || test_task_identify_ux(curr, SA_TYPE_ID_CAMERA_PROVIDER);
		if (curr_ux)
			continue;

		capacity_curr = capacity_curr_of(i);
		if ((curr->prio > lowest_prio) || (boost && (capacity_curr > lowest_prio_max_cap))) {
			lowest_prio = curr->prio;
			lowest_prio_max_cap = capacity_curr;
			*target_cpu = i;
			ret = 0;
		}
	}

	return ret;
}

bool is_sf(struct task_struct *p)
{
	char sf_name[] = "surfaceflinger";

	return (strcmp(p->comm, sf_name) == 0) && (p->pid == p->tgid);
}

void drop_ux_task_cpus(struct task_struct *p, struct cpumask *lowest_mask)
{
	unsigned int cpu = cpumask_first(lowest_mask);
#ifdef CONFIG_SCHED_WALT
	bool sf = false;
#endif

	while (cpu < nr_cpu_ids) {
		/* unlocked access */
		struct task_struct *task = READ_ONCE(cpu_rq(cpu)->curr);

		if ((sysctl_sched_assist_scene & SA_LAUNCH) && (task->ux_state & SA_TYPE_HEAVY))
			cpumask_clear_cpu(cpu, lowest_mask);

		if (test_task_ux(task) || !list_empty(&task->ux_entry) ||
			(test_task_identify_ux(task, SA_TYPE_ID_CAMERA_PROVIDER) && oplus_is_min_capacity_cpu(cpu))) {
			cpumask_clear_cpu(cpu, lowest_mask);
		}

#ifdef CONFIG_SCHED_WALT
		if (sched_assist_scene(SA_SLIDE) || sched_assist_scene(SA_INPUT) || sched_assist_scene(SA_LAUNCHER_SI)) {
			sf = is_sf(p);
			if (sf && is_task_util_over(p, sysctl_boost_task_threshold) && oplus_is_min_capacity_cpu(cpu))
				cpumask_clear_cpu(cpu, lowest_mask);
		}
#endif
		cpu = cpumask_next(cpu, lowest_mask);
	}
}

static inline bool test_sched_assist_ux_type(struct task_struct *task, unsigned int sa_ux_type)
{
	return task->ux_state & sa_ux_type;
}

static inline u64 max_vruntime(u64 max_vruntime, u64 vruntime)
{
	s64 delta = (s64)(vruntime - max_vruntime);

	if (delta > 0)
		max_vruntime = vruntime;

	return max_vruntime;
}

#define MALI_THREAD_NAME "mali-cmar-backe"
#define LAUNCHER_THREAD_NAME "ndroid.launcher"
#define ALLOCATOR_THREAD_NAME "allocator-servi"
#define CAMERA_PROVIDER_NAME "provider@2.4-se"
#define CAMERA_HAL_SERVER_NAME "camerahalserver"

#define CAMERA_MAINTHREAD_NAME "com.oplus.camera"
#define OPLUS_CAMERA_MAINTHREAD_NAME "om.oplus.camera"
#define CAMERA_PREMR_NAME "previewManagerR"
#define CAMERA_PREPT_NAME "PreviewProcessT"
#define CAMERA_HALCONT_NAME "Camera Hal Cont"
#define CAMERA_IMAGEPROC_NAME "ImageProcessThr"

void sched_assist_target_comm(struct task_struct *task)
{
	struct task_struct *grp_leader = task->group_leader;

	if (unlikely(!sysctl_sched_assist_enabled))
		return;

	if (!grp_leader || (get_ux_state_type(task) != UX_STATE_NONE))
		return;

#ifndef CONFIG_OPLUS_SYSTEM_KERNEL_QCOM
	/* mali thread only exist in mtk platform */
	if (strstr(grp_leader->comm, LAUNCHER_THREAD_NAME) && strstr(task->comm, MALI_THREAD_NAME)) {
		task->ux_state |= SA_TYPE_ANIMATOR;
		return;
	}

#define CAMERASERVER_UID 1047
	if (!strncmp(grp_leader->comm, CAMERA_HAL_SERVER_NAME, TASK_COMM_LEN) && (task_uid(task).val == CAMERASERVER_UID)) {
		if (task->sched_class == &fair_sched_class) {
			task->ux_state |= SA_TYPE_ID_CAMERA_PROVIDER;
			return;
		}
	}
#endif

	if (strstr(grp_leader->comm, CAMERA_PROVIDER_NAME) && strstr(task->comm, CAMERA_PROVIDER_NAME)) {
		task->ux_state |= SA_TYPE_ID_CAMERA_PROVIDER;
		return;
	}

	if ((strstr(grp_leader->comm, CAMERA_MAINTHREAD_NAME)
		|| strstr(grp_leader->comm, OPLUS_CAMERA_MAINTHREAD_NAME)) && (strstr(task->comm, CAMERA_PREMR_NAME)
		|| strstr(task->comm, CAMERA_PREPT_NAME)
		|| strstr(task->comm, CAMERA_HALCONT_NAME)
		|| strstr(task->comm, CAMERA_IMAGEPROC_NAME))) {
		task->ux_state |= SA_TYPE_LIGHT;
		return;
	}

	if (!strncmp(grp_leader->comm, ALLOCATOR_THREAD_NAME, TASK_COMM_LEN)
			|| !strncmp(task->comm, ALLOCATOR_THREAD_NAME, TASK_COMM_LEN))
		task->ux_state |= SA_TYPE_ID_ALLOCATOR_SER;
}

#ifdef CONFIG_FAIR_GROUP_SCHED
/* An entity is a task if it doesn't "own" a runqueue */
#define oplus_entity_is_task(se)	(!se->my_q)
#else
#define oplus_entity_is_task(se)	(1)
#endif

void place_entity_adjust_ux_task(struct cfs_rq *cfs_rq, struct sched_entity *se, int initial)
{
	u64 vruntime = cfs_rq->min_vruntime;
	unsigned long thresh = sysctl_sched_latency;
	unsigned long launch_adjust = 0;
	struct task_struct *se_task = NULL;

	if (unlikely(!sysctl_sched_assist_enabled))
		return;

	if (!oplus_entity_is_task(se) || initial)
		return;

	if (sysctl_sched_assist_scene & SA_LAUNCH)
		launch_adjust = sysctl_sched_latency;

	se_task = task_of(se);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0)) || defined(CONFIG_MMAP_LOCK_OPT)
	if (se_task->ux_once) {
		vruntime -= 3 * thresh;
		se->vruntime = vruntime;
		se_task->ux_once = 0;
		return;
	}
#endif

	if (test_sched_assist_ux_type(se_task, SA_TYPE_ANIMATOR)) {
		vruntime -= 3 * thresh + (thresh >> 1);
		se->vruntime = vruntime - (launch_adjust >> 1);
		return;
	}

	if (test_sched_assist_ux_type(se_task, SA_TYPE_LIGHT | SA_TYPE_HEAVY)) {
		vruntime -= 2 * thresh;
		se->vruntime = vruntime - (launch_adjust >> 1);
		return;
	}
#ifdef CONFIG_OPLUS_FEATURE_AUDIO_OPT
	if (!sysctl_sched_impt_tgid && test_task_identify_ux(se_task, SA_TYPE_ID_CAMERA_PROVIDER)) {
#else
	if (test_task_identify_ux(se_task, SA_TYPE_ID_CAMERA_PROVIDER)) {
#endif
		vruntime -= 2 * thresh + (thresh >> 1);
		se->vruntime = vruntime - (launch_adjust >> 1);
		return;
	}

#ifdef CONFIG_OPLUS_FEATURE_SCHED_SPREAD
	if (should_force_adjust_vruntime(se)) {
		se->vruntime = vruntime - (thresh + (thresh >> 1));
		return;
	}
#endif
}

bool should_ux_preempt_wakeup(struct task_struct *wake_task, struct task_struct *curr_task)
{
	bool wake_ux = false;
	bool curr_ux = false;

	if (!sysctl_sched_assist_enabled)
		return false;

	wake_ux = test_task_ux(wake_task) || test_list_pick_ux(wake_task) || test_task_identify_ux(wake_task, SA_TYPE_ID_CAMERA_PROVIDER);
	curr_ux = test_task_ux(curr_task) || test_list_pick_ux(curr_task) || test_task_identify_ux(curr_task, SA_TYPE_ID_CAMERA_PROVIDER);

	/* exit animation ux is set as highest ux which other ux can't preempt and can preempt other ux */
	if (!(sysctl_sched_assist_scene & SA_LAUNCH)) {
		if (is_animation_ux(curr_task))
			return false;
		else if (is_animation_ux(wake_task))
			return true;
	}

	/* ux can preemt cfs */
	if (wake_ux && !curr_ux)
		return true;

	/* animator ux can preemt un-animator */
	if ((wake_task->ux_state & SA_TYPE_ANIMATOR) && !(curr_task->ux_state & SA_TYPE_ANIMATOR))
		return true;

	/* heavy type can be preemt by other type */
	if (wake_ux && !(wake_task->ux_state & SA_TYPE_HEAVY) && (curr_task->ux_state & SA_TYPE_HEAVY))
		return true;

	return false;
}

bool should_ux_task_skip_further_check(struct sched_entity *se)
{
	return oplus_entity_is_task(se) && test_sched_assist_ux_type(task_of(se), SA_TYPE_ANIMATOR);
}

static inline bool is_ux_task_prefer_cpu(struct task_struct *task, int cpu)
{
	struct ux_sched_cputopo ux_cputopo = ux_sched_cputopo;
	int cls_nr = ux_cputopo.cls_nr - 1;

	if (cpu < 0)
		return false;

	/* only one cluster or init failed */
	if (unlikely(cls_nr <= 0))
		return true;

	if (test_sched_assist_ux_type(task, SA_TYPE_HEAVY))
		return capacity_orig_of(cpu) >= ux_cputopo.sched_cls[cls_nr].capacity;

	return true;
}

bool is_task_util_over(struct task_struct *task, int threshold)
{
	bool sum_over = false;
	bool demand_over = false;

	sum_over = scale_demand(task->wts.sum) >= threshold;

#ifdef CONFIG_OPLUS_SYSTEM_KERNEL_QCOM
	demand_over = task_util(task) >= threshold;
#else
	demand_over = false;
#endif

	return sum_over || demand_over;
}

static inline int get_task_cls_for_scene(struct task_struct *task)
{
	struct ux_sched_cputopo ux_cputopo = ux_sched_cputopo;
	int cls_max = ux_cputopo.cls_nr - 1;
	int cls_mid = cls_max - 1;

	/* only one cluster or init failed */
	if (unlikely(cls_max <= 0))
		return 0;

	/* for 2 clusters cpu, mid = max */
	if (cls_mid == 0)
		cls_mid = cls_max;

	/* just change cpu selection for heavy ux task */
	if (!test_sched_assist_ux_type(task, SA_TYPE_HEAVY))
		return 0;

	/* for launch scene, heavy ux task move to max capacity cluster */
	if (sched_assist_scene(SA_LAUNCH))
		return cls_max;

	if ((sched_assist_scene(SA_LAUNCHER_SI) || sched_assist_scene(SA_SLIDE) || sched_assist_scene(SA_INPUT)) &&
		is_task_util_over(task, sysctl_boost_task_threshold))
		return cls_mid;

	return 0;
}

static inline bool is_ux_task_prefer_cpu_for_scene(struct task_struct *task, unsigned int cpu)
{
	struct ux_sched_cputopo ux_cputopo = ux_sched_cputopo;
	int cls_id = ux_cputopo.cls_nr - 1;

	/* only one cluster or init failed */
	if (unlikely(cls_id <= 0))
		return true;

	cls_id = get_task_cls_for_scene(task);
	return capacity_orig_of(cpu) >= ux_cputopo.sched_cls[cls_id].capacity;
}

#ifdef CONFIG_SCHED_WALT
/* 2ms default for 20ms window size scaled to 1024 */
bool sched_assist_task_misfit(struct task_struct *task, int cpu, int flag)
{
	if (unlikely(!sysctl_sched_assist_enabled))
		return false;

	/* for SA_TYPE_ID_CAMERA_PROVIDER */
	if (test_task_identify_ux(task, SA_TYPE_ID_CAMERA_PROVIDER)
		&& is_task_util_over(task, THRESHOLD_BOOST) && oplus_is_min_capacity_cpu(cpu)) {
		return true;
	}

	if (is_task_util_over(task, sysctl_boost_task_threshold)
		&& !is_ux_task_prefer_cpu_for_scene(task, cpu)) {
		return true;
	}

	return false;
}
#endif

bool should_ux_task_skip_cpu(struct task_struct *task, unsigned int cpu)
{
	if (!sysctl_sched_assist_enabled || !test_task_ux(task))
		return false;

	if (!is_ux_task_prefer_cpu_for_scene(task, cpu))
		return true;

	if (!(sysctl_sched_assist_scene & SA_LAUNCH) || !test_sched_assist_ux_type(task, SA_TYPE_HEAVY)) {
		if (cpu_rq(cpu)->rt.rt_nr_running)
			return true;

		/* avoid placing turbo ux into cpu which has animator ux or list ux */
		if (cpu_rq(cpu)->curr && (test_sched_assist_ux_type(cpu_rq(cpu)->curr, SA_TYPE_ANIMATOR)
			|| !list_empty(&cpu_rq(cpu)->ux_thread_list)))
			return true;
	}

	return false;
}

void set_ux_task_to_prefer_cpu_v1(struct task_struct *task, int *orig_target_cpu, bool *cond)
{
	struct rq *rq = NULL;
	struct ux_sched_cputopo ux_cputopo = ux_sched_cputopo;
	int cls_nr = ux_cputopo.cls_nr - 1;
	int cpu = 0;

	if (!sysctl_sched_assist_enabled || !(sysctl_sched_assist_scene & SA_LAUNCH))
		return;

	if (unlikely(cls_nr <= 0))
		return;

	if (is_ux_task_prefer_cpu(task, *orig_target_cpu))
		return;
	*cond = true;
retry:
	for_each_cpu(cpu, &ux_cputopo.sched_cls[cls_nr].cpus) {
		rq = cpu_rq(cpu);
		if (test_sched_assist_ux_type(rq->curr, SA_TYPE_HEAVY))
			continue;

		if (rq->curr->prio < MAX_RT_PRIO)
			continue;

		if (cpu_online(cpu) && !cpu_isolated(cpu) && cpumask_test_cpu(cpu, task->cpus_ptr)) {
			*orig_target_cpu = cpu;
			return;
		}
	}

	cls_nr = cls_nr - 1;
	if (cls_nr > 0)
		goto retry;
}

void set_ux_task_to_prefer_cpu(struct task_struct *task, int *orig_target_cpu)
{
	struct rq *rq = NULL;
	struct ux_sched_cputopo ux_cputopo = ux_sched_cputopo;
	int cls_nr = ux_cputopo.cls_nr - 1;
	int cpu = 0;
	int direction = -1;

	if (!sysctl_sched_assist_enabled || !(sysctl_sched_assist_scene & SA_LAUNCH))
		return;

	if (unlikely(cls_nr <= 0))
		return;

	if (is_ux_task_prefer_cpu(task, *orig_target_cpu))
		return;

	cls_nr = get_task_cls_for_scene(task);
	if (cls_nr != ux_cputopo.cls_nr - 1)
		direction = 1;
retry:
	for_each_cpu(cpu, &ux_cputopo.sched_cls[cls_nr].cpus) {
		rq = cpu_rq(cpu);
		if (test_sched_assist_ux_type(rq->curr, SA_TYPE_HEAVY))
			continue;

		if (rq->curr->prio < MAX_RT_PRIO)
			continue;

		if (cpu_online(cpu) && !cpu_isolated(cpu) && cpumask_test_cpu(cpu, task->cpus_ptr)) {
			*orig_target_cpu = cpu;
			return;
		}
	}

	cls_nr = cls_nr + direction;
	if (cls_nr > 0 && cls_nr < ux_cputopo.cls_nr)
		goto retry;
}

#ifdef CONFIG_OPLUS_FEATURE_SCHED_SPREAD
static void requeue_runnable_task(struct task_struct *p)
{
	bool queued, running;
	struct rq_flags rf;
	struct rq *rq;

	rq = task_rq_lock(p, &rf);
	queued = task_on_rq_queued(p);
	running = task_current(rq, p);

	if (!queued || running) {
		task_rq_unlock(rq, p, &rf);
		return;
	}

	update_rq_clock(rq);
	deactivate_task(rq, p, DEQUEUE_NOCLOCK);
	activate_task(rq, p, ENQUEUE_NOCLOCK);
	resched_curr(rq);

	task_rq_unlock(rq, p, &rf);
}
#endif

void set_inherit_ux(struct task_struct *task, int type, int depth, int inherit_val)
{
	struct rq_flags flags;
	struct rq *rq = NULL;
	int old_state = 0;

	if (!task || type >= INHERIT_UX_MAX)
		return;

	rq = task_rq_lock(task, &flags);

	if (task->sched_class != &fair_sched_class) {
		task_rq_unlock(rq, task, &flags);
		return;
	}

	inherit_ux_inc(task, type);
	task->ux_depth = depth + 1;
	old_state = task->ux_state;
	task->ux_state = (inherit_val & SCHED_ASSIST_UX_MASK) | SA_TYPE_INHERIT;
	/* identify type like allocator ux, keep it, but can not inherit*/
	if (old_state & SA_TYPE_ID_ALLOCATOR_SER)
		task->ux_state |= SA_TYPE_ID_ALLOCATOR_SER;
	if (old_state & SA_TYPE_ID_CAMERA_PROVIDER)
		task->ux_state |= SA_TYPE_ID_CAMERA_PROVIDER;
	task->inherit_ux_start = jiffies_to_nsecs(jiffies);

	if (task->on_rq && (!rq->curr || (!test_task_ux(rq->curr) && rq->curr->prio > 110))) {
		set_once_ux(task);
		enqueue_ux_thread(task_rq(task), task);
	}
	sched_assist_systrace_pid(task->tgid, task->ux_state, "ux_state %d", task->pid);

	task_rq_unlock(rq, task, &flags);

#ifdef CONFIG_OPLUS_FEATURE_SCHED_SPREAD
	/* requeue runnable task to ensure vruntime adjust */
	requeue_runnable_task(task);
#endif
}

void reset_inherit_ux(struct task_struct *inherit_task, struct task_struct *ux_task, int reset_type)
{
	struct rq_flags flags;
	struct rq *rq;
	int reset_depth = 0;
	int reset_inherit = 0;

	if (!inherit_task || !ux_task || reset_type >= INHERIT_UX_MAX)
		return;

	reset_inherit = ux_task->ux_state;
	reset_depth = ux_task->ux_depth;
	/* animator ux is important, so we just reset in this type */
	if (!test_inherit_ux(inherit_task, reset_type) || !test_sched_assist_ux_type(ux_task, SA_TYPE_ANIMATOR))
		return;

	rq = task_rq_lock(inherit_task, &flags);

	inherit_task->ux_depth = reset_depth + 1;
	/* identify type like allocator ux, keep it, but can not inherit  */
	if (reset_inherit & SA_TYPE_ID_ALLOCATOR_SER)
		reset_inherit &= ~SA_TYPE_ID_ALLOCATOR_SER;
	if (reset_inherit & SA_TYPE_ID_CAMERA_PROVIDER)
		reset_inherit &= ~SA_TYPE_ID_CAMERA_PROVIDER;
	inherit_task->ux_state = (inherit_task->ux_state & ~SCHED_ASSIST_UX_MASK) | reset_inherit;

	sched_assist_systrace_pid(inherit_task->tgid, inherit_task->ux_state, "ux_state %d", inherit_task->pid);

	task_rq_unlock(rq, inherit_task, &flags);
}

void unset_inherit_ux_value(struct task_struct *task, int type, int value)
{
	struct rq_flags flags;
	struct rq *rq;
	s64 inherit_ux;

	if (!task || type >= INHERIT_UX_MAX)
		return;

	rq = task_rq_lock(task, &flags);

	inherit_ux_sub(task, type, value);
	inherit_ux = atomic64_read(&task->inherit_ux);
	if (inherit_ux > 0) {
		task_rq_unlock(rq, task, &flags);
		return;
	}
	if (inherit_ux < 0)
		atomic64_set(&(task->inherit_ux), 0);

	task->ux_depth = 0;
	/* identify type like allocator ux, keep it, but can not inherit  */
	task->ux_state &= SA_TYPE_ID_ALLOCATOR_SER | SA_TYPE_ID_CAMERA_PROVIDER;

	sched_assist_systrace_pid(task->tgid, task->ux_state, "ux_state %d", task->pid);

	task_rq_unlock(rq, task, &flags);
}

void unset_inherit_ux(struct task_struct *task, int type)
{
	unset_inherit_ux_value(task, type, 1);
}

void inc_inherit_ux_refs(struct task_struct *task, int type)
{
	struct rq_flags flags;
	struct rq *rq;

	rq = task_rq_lock(task, &flags);
	inherit_ux_inc(task, type);
	task_rq_unlock(rq, task, &flags);
}

#ifndef CONFIG_OPLUS_SYSTEM_KERNEL_QCOM
static unsigned long task_util_mtk(struct task_struct *p, bool flag)
{
#ifdef CONFIG_SCHED_WALT
	if (likely(!walt_disabled && (sysctl_sched_use_walt_task_util || flag)))
		return (p->ravg.demand / (walt_ravg_window >> SCHED_CAPACITY_SHIFT));
#endif
	return READ_ONCE(p->se.avg.util_avg);
}
#endif /* CONFIG_OPLUS_SYSTEM_KERNEL_MTK  */

bool oplus_task_misfit(struct task_struct *p, int cpu)
{
#ifdef CONFIG_SCHED_WALT
#ifdef CONFIG_OPLUS_SYSTEM_KERNEL_QCOM
	int num_mincpu = cpumask_weight(topology_core_cpumask(0));

	if ((scale_demand(p->wts.sum) >= sysctl_boost_task_threshold ||
		task_util(p) >= sysctl_boost_task_threshold) && cpu < num_mincpu)
		return true;
#endif
#endif
	return false;
}
void kick_min_cpu_from_mask(struct cpumask *lowest_mask)
{
	unsigned int cpu = cpumask_first(lowest_mask);

	while (cpu < nr_cpu_ids) {
		if (cpu < ux_prefer_cpu[0])
			cpumask_clear_cpu(cpu, lowest_mask);

		cpu = cpumask_next(cpu, lowest_mask);
	}
}

bool ux_skip_sync_wakeup(struct task_struct *task, int *sync)
{
	bool ret = false;

	if (test_sched_assist_ux_type(task, SA_TYPE_ANIMATOR)) {
		*sync = 0;
		ret = true;
	}

	return ret;
}

/*
 * add for create proc node: proc/pid/task/pid/ux_state
 */
bool is_special_entry(struct dentry *dentry, const char *special_proc)
{
	const unsigned char *name;

	if (!dentry || !special_proc)
		return false;

	name = dentry->d_name.name;
	if (name != NULL && !strncmp(special_proc, name, 32))
		return true;
	else
		return false;
}

static unsigned long __read_mostly mark_addr;

static int _sched_assist_update_tracemark(void)
{
	if (mark_addr)
		return 1;

	mark_addr = kallsyms_lookup_name("tracing_mark_write");

	if (unlikely(!mark_addr))
		return 0;
	return 1;
}

void sched_assist_systrace_pid(pid_t pid, int val, const char *fmt, ...)
{
	char log[256];
	va_list args;
	int len;

	if (likely(!param_ux_debug))
		return;

	if (unlikely(!_sched_assist_update_tracemark()))
		return;

	memset(log, ' ', sizeof(log));
	va_start(args, fmt);
	len = vsnprintf(log, sizeof(log), fmt, args);
	va_end(args);

	if (unlikely(len < 0))
		return;
	else if (unlikely(len == 256))
		log[255] = '\0';

	preempt_disable();
	event_trace_printk(mark_addr, "C|%d|%s|%d\n", pid, log, val);
	preempt_enable();
}

static int proc_ux_state_show(struct seq_file *m, void *v)
{
	struct inode *inode = m->private;
	struct task_struct *p;

	p = get_proc_task(inode);
	if (!p)
		return -ESRCH;

	task_lock(p);
	seq_printf(m, "%d\n", p->ux_state);
	task_unlock(p);
	put_task_struct(p);
	return 0;
}

static int proc_ux_state_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, proc_ux_state_show, inode);
}

static ssize_t proc_ux_state_write(struct file *file, const char __user *buf,
		size_t count, loff_t *ppos)
{
	struct task_struct *task;
	char buffer[PROC_NUMBUF];
	int err, ux_state;

	memset(buffer, 0, sizeof(buffer));

	if (count > sizeof(buffer) - 1)
		count = sizeof(buffer) - 1;

	if (copy_from_user(buffer, buf, count))
		return -EFAULT;

	err = kstrtoint(strstrip(buffer), 0, &ux_state);
	if (err)
		return err;

	task = get_proc_task(file_inode(file));
	if (!task)
		return -ESRCH;

	if (ux_state < 0)
		return -EINVAL;

	if (ux_state == SA_OPT_CLEAR) /* clear all ux type but animator */
		task->ux_state &= ~(SA_TYPE_LISTPICK | SA_TYPE_HEAVY | SA_TYPE_LIGHT);
	else if (ux_state & SA_OPT_SET)  /* set target ux type and clear set opt */
		task->ux_state |= ux_state & (~SA_OPT_SET);
	else if (task->ux_state & ux_state)  /* reset target ux type */
		task->ux_state &= ~ux_state;

	sched_assist_systrace_pid(task->tgid, task->ux_state, "ux_state %d", task->pid);

	put_task_struct(task);
	return count;
}

static ssize_t proc_ux_state_read(struct file *file, char __user *buf,
		size_t count, loff_t *ppos)
{
	char buffer[256];
	struct task_struct *task = NULL;
	int ux_state = -1;
	size_t len = 0;

	task = get_proc_task(file_inode(file));
	if (!task)
		return -ESRCH;

	ux_state = task->ux_state;
	put_task_struct(task);

	len = snprintf(buffer, sizeof(buffer), "pid=%d ux_state=%d inherit=%llx(fu:%d mu:%d rw:%d bi:%d)\n",
		task->pid, ux_state, task->inherit_ux,
		test_inherit_ux(task, INHERIT_UX_FUTEX), test_inherit_ux(task, INHERIT_UX_MUTEX),
		test_inherit_ux(task, INHERIT_UX_RWSEM), test_inherit_ux(task, INHERIT_UX_BINDER));

	return simple_read_from_buffer(buf, count, ppos, buffer, len);
}

const struct file_operations proc_ux_state_operations = {
	.open		= proc_ux_state_open,
	.write		= proc_ux_state_write,
	.read		= proc_ux_state_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

/*
 * add for proc node: proc/sys/kernel/sched_assist_scene
 */
int sysctl_sched_assist_scene_handler(struct ctl_table *table, int write,
	void __user *buffer, size_t *lenp, loff_t *ppos)
{
	int result, save_sa;
	static DEFINE_MUTEX(sa_scene_mutex);

	mutex_lock(&sa_scene_mutex);

	save_sa = sysctl_sched_assist_scene;
	result = proc_dointvec(table, write, buffer, lenp, ppos);

	if (!write)
		goto out;

	if (sysctl_sched_assist_scene == SA_SCENE_OPT_CLEAR)
		goto out;

	if (sysctl_sched_assist_scene & SA_SCENE_OPT_SET)
		save_sa |= sysctl_sched_assist_scene & (~SA_SCENE_OPT_SET);
	else if (save_sa & sysctl_sched_assist_scene)
		save_sa &= ~sysctl_sched_assist_scene;

	sysctl_sched_assist_scene = save_sa;
	sched_assist_systrace(sysctl_sched_assist_scene, "scene");
out:
	mutex_unlock(&sa_scene_mutex);

	return result;
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0)) || defined(CONFIG_MMAP_LOCK_OPT)
static inline bool is_mmap_lock_opt_enabled(void)
{
	return sysctl_uxchain_v2 && !sched_assist_scene(SA_CAMERA);
}

void uxchain_rwsem_wake(struct task_struct *tsk, struct rw_semaphore *sem)
{
	int set_ux_once;

	if (!is_mmap_lock_opt_enabled())
		return;

	if (current->mm) {
		set_ux_once = (sem == &(current->mm->mmap_sem));
		if (set_ux_once)
			tsk->ux_once = 1;
	}
}
void uxchain_rwsem_down(struct rw_semaphore *sem)
{
	if (!is_mmap_lock_opt_enabled())
		return;

	if (current->mm && sem == &(current->mm->mmap_sem)) {
		current->get_mmlock = 1;
		current->get_mmlock_ts = sched_clock();
	}
}

void uxchain_rwsem_up(struct rw_semaphore *sem)
{
	if (current->mm && sem == &(current->mm->mmap_sem) &&
		current->get_mmlock == 1) {
		current->get_mmlock = 0;
	}
}
#endif

bool skip_check_preempt_curr(struct rq *rq, struct task_struct *p, int flags)
{
	u64 wallclock = 0;

	wallclock = sched_clock();
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0)) || defined(CONFIG_MMAP_LOCK_OPT)
	if (!is_mmap_lock_opt_enabled())
		return false;

	if (wallclock - rq->curr->get_mmlock_ts < PREEMPT_DISABLE_RWSEM &&
		rq->curr->get_mmlock &&
		!(p->flags & PF_WQ_WORKER) && !task_has_rt_policy(p))
		return true;
#endif

	return false;
}

#define TOP_APP_GROUP_ID	4
bool im_mali(struct task_struct *p)
{
	return strstr(p->comm, "mali-cmar-backe");
}

bool cgroup_check_set_sched_assist_boost(struct task_struct *p)
{
	return im_mali(p);
}

int get_st_group_id(struct task_struct *task)
{
#if IS_ENABLED(CONFIG_SCHED_TUNE)
	const int subsys_id = schedtune_cgrp_id;
	struct cgroup *grp;

	rcu_read_lock();
	grp = task_cgroup(task, subsys_id);
	rcu_read_unlock();
	return grp->id;
#else
	return 0;
#endif
}

void cgroup_set_sched_assist_boost_task(struct task_struct *p)
{
	if (cgroup_check_set_sched_assist_boost(p)) {
		if (get_st_group_id(p) == TOP_APP_GROUP_ID)
			p->ux_state |= SA_TYPE_HEAVY;
		else
			p->ux_state &= ~SA_TYPE_HEAVY;
	}
}

#define DEFAULT_LIMIT_CORE_USE (4)
#define MAX_LIMIT_PROCESS (5)
#define ULIMIT_PROCESS_TYPE (3)

bool is_limit_task(struct task_struct *task)
{
	return false;
}

static inline bool should_limit_core_use(struct task_struct *task)
{
	return task && is_limit_task(task);
}

bool should_limit_task_skip_cpu(struct task_struct *task, int cpu)
{
	return should_limit_core_use(task) && (cpu >= DEFAULT_LIMIT_CORE_USE);
}

bool set_limit_task_target_cpu(struct task_struct *task, int *target_cpu)
{
	bool res = false;

	if (should_limit_core_use(task)) {
		int i = 0;
		int core_limit = DEFAULT_LIMIT_CORE_USE;
		unsigned int best_nr = UINT_MAX;
		struct cpumask allowed_mask;

		cpumask_clear(&allowed_mask);
		if (core_limit <= 0 || core_limit >= num_possible_cpus())
			return false;

		for (i = 0; i < core_limit; ++i)
			cpumask_set_cpu(i, &allowed_mask);

		if (cpumask_test_cpu(*target_cpu, &allowed_mask))
			return true;
		for_each_cpu_and(i, &allowed_mask, task->cpus_ptr) {
			if (!cpu_online(i) || cpu_isolated(i))
				continue;

			if (cpu_rq(i)->nr_running < best_nr) {
				best_nr = cpu_rq(i)->nr_running;
				*target_cpu = i;
				res = true;
			}
		}
	}

	return res;
}

static ssize_t proc_sched_impt_task_write(struct file *file, const char __user *buf,
		size_t count, loff_t *ppos)
{
	char temp_buf[32];
	char *temp_str, *token;
	char in_str[2][16];
	int cnt, err, pid;
	static DEFINE_MUTEX(impt_thd_mutex);
	static unsigned int save_idx = 0;

	mutex_lock(&impt_thd_mutex);

	memset(temp_buf, 0, sizeof(temp_buf));

	if (count > sizeof(temp_buf) - 1) {
		mutex_unlock(&impt_thd_mutex);
		return -EFAULT;
	}

	if (copy_from_user(temp_buf, buf, count)) {
		mutex_unlock(&impt_thd_mutex);
		return -EFAULT;
	}

	cnt = 0;
	temp_str = strstrip(temp_buf);
	while ((token = strsep(&temp_str, " ")) && *token && (cnt < 2)) {
		strncpy(in_str[cnt], token, sizeof(in_str[cnt]));
		cnt += 1;
	}

	if (cnt != 2) {
		mutex_unlock(&impt_thd_mutex);
		return -EFAULT;
	}

	err = kstrtoint(strstrip(in_str[1]), 0, &pid);
	if (err) {
		mutex_unlock(&impt_thd_mutex);
		return err;
	}

	if (pid <= 0 || pid > PID_MAX_DEFAULT) {
		mutex_unlock(&impt_thd_mutex);
		return -EINVAL;
	}

	/* set top app */
	if (!strncmp(in_str[0], "fg", 2)) {
		save_top_app_tgid = pid;
		top_app_type = 0;
		if (!strncmp(in_str[0], "fgLauncher", 10))
			top_app_type = 1; /* 1 is launcher */
		goto out;
	}

	/* set audio app */
	if (!strncmp(in_str[0], "addAu", 5)) {
		int i = 0;

		for (i = 0; i < MAX_IMPT_SAVE_PID; ++i) {
			if (save_impt_tgid[i] == pid)
				break;
		}

		/* we can't found save_tgid, update it. */
		if (i >= MAX_IMPT_SAVE_PID)
			save_impt_tgid[save_idx++] = pid;

		if (save_idx >= MAX_IMPT_SAVE_PID)
			save_idx = 0;
	} else if (!strncmp(in_str[0], "remAu", 5)) {
		int i = 0;

		for (i = 0; i < MAX_IMPT_SAVE_PID; ++i) {
			if (save_impt_tgid[i] == pid) {
				save_impt_tgid[i] = 0;
				save_idx = i;
				break;
			}
		}
	}

out:
	mutex_unlock(&impt_thd_mutex);
	return count;
}

static ssize_t proc_sched_impt_task_read(struct file *file, char __user *buf,
		size_t count, loff_t *ppos)
{
	char buffer[32];
	size_t len = 0;

	len = snprintf(buffer, sizeof(buffer), "top(%d %u) au(%d %d)\n", save_top_app_tgid, top_app_type, save_impt_tgid[0], save_impt_tgid[1]);

	return simple_read_from_buffer(buf, count, ppos, buffer, len);
}

static const struct file_operations proc_sched_impt_task_fops = {
	.write = proc_sched_impt_task_write,
	.read = proc_sched_impt_task_read,
};

#define OPLUS_SCHEDULER_PROC_DIR	"oplus_scheduler"
#define OPLUS_SCHEDASSIST_PROC_DIR	"sched_assist"
struct proc_dir_entry *d_oplus_scheduler = NULL;
struct proc_dir_entry *d_sched_assist = NULL;
static int __init oplus_sched_assist_init(void)
{
	struct proc_dir_entry *proc_node;

	d_oplus_scheduler = proc_mkdir(OPLUS_SCHEDULER_PROC_DIR, NULL);
	if (!d_oplus_scheduler) {
		ux_err("failed to create proc dir oplus_scheduler\n");
		goto err_dir_scheduler;
	}

	d_sched_assist = proc_mkdir(OPLUS_SCHEDASSIST_PROC_DIR, d_oplus_scheduler);
	if (!d_sched_assist) {
		ux_err("failed to create proc dir sched_assist\n");
		goto err_dir_sa;
	}

	proc_node = proc_create("sched_impt_task", 0666, d_sched_assist, &proc_sched_impt_task_fops);
	if (!proc_node) {
		ux_err("failed to create proc node sched_impt_task\n");
		goto err_node_impt;
	}

	return 0;

err_node_impt:
	remove_proc_entry(OPLUS_SCHEDASSIST_PROC_DIR, NULL);

err_dir_sa:
	remove_proc_entry(OPLUS_SCHEDULER_PROC_DIR, NULL);

err_dir_scheduler:
	return -ENOENT;
}
#ifdef CONFIG_OPLUS_FEATURE_AUDIO_OPT
static inline bool is_sched_boost_group(struct task_struct *tsk)
{
	return (sysctl_sched_impt_tgid != 0 && tsk->tgid == sysctl_sched_impt_tgid);
}

#define ANDROID_PRIORITY_URGENT_AUDIO 101
#define ANDROID_PRIORITY_AUDIO 104
#define FIT_SMALL_THREASH 3
#define RUNNABLE_MIN_THREASH 20000000
#define RUNNING_MIN_THREASH 3000000

inline bool is_small_task(struct task_struct *task)
{
	if (task->oplus_task_info.im_small)
		return true;
	else
		return false;
}

inline bool is_audio_task(struct task_struct *task)
{
	if (((task->prio == ANDROID_PRIORITY_URGENT_AUDIO) || (task->prio == ANDROID_PRIORITY_AUDIO)))
		return true;
	else
		return false;
}

extern void clear_buddies(struct cfs_rq *cfs_rq, struct sched_entity *se);
bool sched_assist_pick_next_task(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	if (cfs_rq->next && oplus_entity_is_task(cfs_rq->next)) {
		struct task_struct *tsk = task_of(cfs_rq->next);

		if (tsk->oplus_task_info.im_small) {
			sched_assist_im_systrace_c(tsk, -1);
			se = cfs_rq->next;
			clear_buddies(cfs_rq, se);
			return true;
		}
	}
	return false;
}

void update_task_sched_stat_common(struct task_struct *tsk, u64 delta_ns, int stats_type)
{
	int i = TASK_INFO_SAMPLE - 1;

	while (i > 0) {
		tsk->oplus_task_info.sa_info[stats_type][i] = tsk->oplus_task_info.sa_info[stats_type][i-1];
		i--;
	}
	tsk->oplus_task_info.sa_info[stats_type][0] = delta_ns;
}

enum {
	TASK_SMALL,
	TASK_TYPE_TOTAL,
};


u64 task_info_sum(struct task_struct *task, int type)
{
	int i;
	u64 sum = 0;

	for (i = 0; i < TASK_INFO_SAMPLE; i++)
		sum += task->oplus_task_info.sa_info[type][i];

	return sum;
}

void try_to_mark_task_type(struct task_struct *tsk, int type)
{
	int i = 0;
	int fit_small = 0;

	switch (type) {
	case TASK_SMALL:
		if ((tsk->oplus_task_info.sa_info[TST_EXEC][TASK_INFO_SAMPLE-1] != 0) && (tsk->oplus_task_info.sa_info[TST_SLEEP][TASK_INFO_SAMPLE-1] != 0)) {
			while (i < TASK_INFO_SAMPLE) {
				if ((tsk->oplus_task_info.sa_info[TST_EXEC][i] < RUNNING_MIN_THREASH)
						&& ((tsk->oplus_task_info.sa_info[TST_EXEC][i] * 5) < tsk->oplus_task_info.sa_info[TST_SLEEP][i]))
					fit_small++;

				if (fit_small >= FIT_SMALL_THREASH) {
					tsk->oplus_task_info.im_small = true;
					return;
				}
				i++;
			}
		}
		tsk->oplus_task_info.im_small = false;
		break;
	default:
		break;
	}
}

void update_sa_task_stats(struct task_struct *tsk, u64 delta_ns, int stats_type)
{
	if (!tsk)
		return;

	if (unlikely(!sysctl_sched_assist_enabled))
		return;

#ifdef CONFIG_CAMERA_OPT
	if (!sysctl_camera_opt_enabled)
		return;

	if (!is_sched_boost_group(tsk)) {
		memset(&tsk->oplus_task_info, 0, sizeof(struct task_info));
		return;
	}
	update_task_sched_stat_common(tsk, delta_ns, stats_type);

	try_to_mark_task_type(tsk, TASK_SMALL);
#endif
}

void sched_assist_update_record(struct task_struct *p, u64 delta_ns, int stats_type)
{
		update_sa_task_stats(p, delta_ns, stats_type);
		sched_assist_im_systrace_c(p, stats_type + 1);
}

void sched_assist_im_systrace_c(struct task_struct *tsk, int tst_type)
{
	if (unlikely(!_sched_assist_update_tracemark()))
		return;

	if (!is_sched_boost_group(tsk))
		return;

	if (likely(!param_ux_debug))
		return;

	preempt_disable();
	if (tst_type != -1) {
		event_trace_printk(mark_addr, "C|10001|short_run_target_%d|%d\n", tsk->pid, tsk->oplus_task_info.im_small ? 1 : 0);
	} else if (tst_type == -1) {
		event_trace_printk(mark_addr, "C|10001|short_run_target_buddy_%d|%d\n", tsk->pid, 1);
		event_trace_printk(mark_addr, "C|10001|short_run_target_buddy_%d|%d\n", tsk->pid, 0);
	}
	preempt_enable();
}
#endif

device_initcall(oplus_sched_assist_init);
