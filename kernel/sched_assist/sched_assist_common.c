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

#include "sched_assist_common.h"

#ifdef CONFIG_MMAP_LOCK_OPT
#include <linux/mm.h>
#include <linux/rwsem.h>
#endif


int ux_min_sched_delay_granularity;
int ux_max_inherit_exist = 1000;
int ux_max_inherit_granularity = 32;
int ux_min_migration_delay = 10;
int ux_max_over_thresh = 2000;
//for slide boost
int sysctl_animation_type = 0;
int sysctl_input_boost_enabled = 0;
int sysctl_sched_assist_ib_duration_coedecay = 1;
u64 sched_assist_input_boost_duration = 0;

#define S2NS_T 1000000

static int param_ux_debug = 0;
module_param_named(debug, param_ux_debug, uint, 0644);

struct ux_util_record sf_target[SF_GROUP_COUNT] = {
{"surfaceflinger", 0, 0},
{"RenderEngine", 0, 0},
};

pid_t sf_pid = 0;
pid_t re_pid = 0;

bool slide_scene(void)
{
	return sched_assist_scene(SA_SLIDE) || sched_assist_scene(SA_ANIM) || sched_assist_scene(SA_INPUT);
}

struct ux_sched_cputopo ux_sched_cputopo;
static inline void sched_init_ux_cputopo(void)
{
	int i = 0;

	ux_sched_cputopo.cls_nr = 0;
	for (; i < NR_CPUS; ++i) {
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
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
		cpu_cap = arch_scale_cpu_capacity(cpu);
#else
		cpu_cap = arch_scale_cpu_capacity(NULL, cpu);
#endif

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
				for (; cls_nr > insert_idx; cls_nr--) {
					ux_sched_cputopo.sched_cls[cls_nr] = ux_sched_cputopo.sched_cls[cls_nr-1];
				}
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

	if (task->ux_state & (SA_TYPE_HEAVY | SA_TYPE_LIGHT | SA_TYPE_ANIMATOR | SA_TYPE_LISTPICK))
		return true;

	return false;
}

inline int get_ux_state_type(struct task_struct *task)
{
	if (!task) {
		return UX_STATE_INVALID;
	}

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

	if (!rq || !p || !list_empty(&p->ux_entry)) {
		return;
	}

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

void dequeue_ux_thread(struct rq *rq, struct task_struct *p)
{
	struct list_head *pos, *n;

	if (!rq || !p) {
		return;
	}
	p->enqueue_time = 0;
	if (!list_empty(&p->ux_entry)) {
		list_for_each_safe(pos, n, &rq->ux_thread_list) {
			if (pos == &p->ux_entry) {
				list_del_init(&p->ux_entry);
				if (p->ux_state & SA_TYPE_ONCE_UX) {
					p->ux_state &= ~SA_TYPE_ONCE_UX;
				}
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
			continue;
		}

		if (leftmost_task == NULL) {
			leftmost_task = temp;
		} else if (entity_before(&temp->se, &leftmost_task->se)) {
			leftmost_task = temp;
		}
	}

	return leftmost_task;
}

void pick_ux_thread(struct rq *rq, struct task_struct **p, struct sched_entity **se)
{
	struct task_struct *ori_p = *p;
	struct task_struct *key_task;
	struct sched_entity *key_se;

	if (!rq || !ori_p || !se) {
		return;
	}

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
	if (!task) {
		return false;
	}
	inherit_ux = atomic64_read(&task->inherit_ux);
	return inherit_ux_get_bits(inherit_ux, type) > 0;
}

static bool test_task_exist(struct task_struct *task, struct list_head *head)
{
	struct list_head *pos, *n;
	list_for_each_safe(pos, n, head) {
		if (pos == &task->ux_entry) {
			return true;
		}
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
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,9,0)
	unsigned long flags;
#else
	struct rq_flags flags;
#endif
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
	if (!task || type >= INHERIT_UX_MAX) {
		return;
	}
	__inherit_ux_dequeue(task, type, 1);
}
void inherit_ux_dequeue_refs(struct task_struct *task, int type, int value)
{
	if (!task || type >= INHERIT_UX_MAX) {
		return;
	}
	__inherit_ux_dequeue(task, type, value);
}

static void __inherit_ux_enqueue(struct task_struct *task, int type, int depth)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,9,0)
	unsigned long flags;
#else
	struct rq_flags flags;
#endif
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
	if (!task || type >= INHERIT_UX_MAX) {
		return;
	}
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
	if (!rq) {
		return;
	}

	INIT_LIST_HEAD(&rq->ux_thread_list);
}

int ux_prefer_cpu[NR_CPUS] = {0};
void ux_init_cpu_data(void) {
	int i = 0;
	int min_cpu = 0, ux_cpu = 0;

	for (; i < nr_cpu_ids; ++i) {
		ux_prefer_cpu[i] = -1;
	}

	ux_cpu = cpumask_weight(topology_core_cpumask(min_cpu));
	if (ux_cpu == 0) {
		ux_warn("failed to init ux cpu data\n");
		return;
	}

	for (i = 0; i < nr_cpu_ids && ux_cpu < nr_cpu_ids; ++i) {
		ux_prefer_cpu[i] = ux_cpu++;
	}
}

bool test_ux_task_cpu(int cpu) {
	return (cpu >= ux_prefer_cpu[0]);
}

bool test_ux_prefer_cpu(struct task_struct *tsk, int cpu) {
	struct root_domain *rd = cpu_rq(smp_processor_id())->rd;

	if (cpu < 0)
		return false;

	if (tsk->pid == tsk->tgid) {
#ifdef CONFIG_OPLUS_SYSTEM_KERNEL_QCOM
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
		return cpu >= rd->wrd.max_cap_orig_cpu;
#else
		return cpu >= rd->max_cap_orig_cpu;
#endif
#else
		return capacity_orig_of(cpu) >= rd->max_cpu_capacity.val;
#endif /*CONFIG_OPLUS_SYSTEM_KERNEL_QCOM*/
	}

	return (cpu >= ux_prefer_cpu[0]);
}

void find_ux_task_cpu(struct task_struct *tsk, int *target_cpu) {
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

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
		if (!test_task_ux(rq->curr) && cpu_online(temp_cpu) && !cpu_isolated(temp_cpu)
			&& cpumask_test_cpu(temp_cpu, tsk->cpus_ptr)) {
#else
		if (!test_task_ux(rq->curr) && cpu_online(temp_cpu) && !cpu_isolated(temp_cpu)
			&& cpumask_test_cpu(temp_cpu, &tsk->cpus_allowed)) {
#endif
			*target_cpu = temp_cpu;
			return;
		}
	}
	return;
}

static inline bool oplus_is_min_capacity_cpu(int cpu)
{
	struct ux_sched_cputopo ux_cputopo = ux_sched_cputopo;
	int cls_nr = ux_cputopo.cls_nr - 1;

	if (unlikely(cls_nr <= 0))
		return false;

	return capacity_orig_of(cpu) == ux_cputopo.sched_cls[0].capacity;
}

#ifdef CONFIG_SCHED_WALT
/* 2ms default for 20ms window size scaled to 1024 */
#define THRESHOLD_BOOST (102)
bool sched_assist_task_misfit(struct task_struct *task, int cpu, int flag)
{
	bool sum_over = false;
	bool demand_over = false;

	if (unlikely(!sysctl_sched_assist_enabled))
		return false;

	if (!test_task_identify_ux(task, SA_TYPE_ID_CAMERA_PROVIDER))
		return false;

	/* for SA_TYPE_ID_CAMERA_PROVIDER */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
	sum_over = scale_demand(task->wts.sum) >= THRESHOLD_BOOST;
#else
	sum_over = scale_demand(task->ravg.sum) >= THRESHOLD_BOOST;
#endif

#ifdef CONFIG_OPLUS_SYSTEM_KERNEL_QCOM
	demand_over = task_util(task) >= THRESHOLD_BOOST;
#else
	demand_over = false;
#endif

	if ((sum_over || demand_over) && oplus_is_min_capacity_cpu(cpu)) {
		return true;
	}

	return false;
}
#endif

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
	int ret = -1;

	if (unlikely(!sysctl_sched_assist_enabled))
		return -1;

	if (!(task->ux_state & SA_TYPE_ANIMATOR) && !test_task_identify_ux(task, SA_TYPE_ID_CAMERA_PROVIDER))
		return -1;

	if ((*target_cpu < 0) || (*target_cpu >= NR_CPUS))
		return -1;

#ifdef CONFIG_SCHED_WALT
	if (test_task_identify_ux(task, SA_TYPE_ID_CAMERA_PROVIDER)) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
		boost = ((scale_demand(task->wts.sum) >= THRESHOLD_BOOST) ||
			(task_util(task) >= THRESHOLD_BOOST)) ? true : false;
#else /* KERNEL-5-4 */

#ifdef CONFIG_OPLUS_SYSTEM_KERNEL_QCOM
		boost = ((scale_demand(task->ravg.sum) >= THRESHOLD_BOOST) ||
			(task_util(task) >= THRESHOLD_BOOST)) ? true : false;
#else
		boost = (scale_demand(task->ravg.sum) >= THRESHOLD_BOOST);
#endif

#endif /* KERNEL-5-4 */
	}
#endif /* CONFIG_SCHED_WALT */

	for_each_cpu(i, cpu_active_mask) {
		unsigned long capacity_curr;
		struct task_struct *curr;
		bool curr_ux = false;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
		if (!cpumask_test_cpu(i, task->cpus_ptr) || cpu_isolated(i))
#else
		if (!cpumask_test_cpu(i, &task->cpus_allowed) || cpu_isolated(i))
#endif
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

bool is_sf( struct task_struct *p)
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
		if (slide_scene()) {
			sf= is_sf(p);
			if (sf && (cpu < ux_prefer_cpu[0] || (task->ux_state & SA_TYPE_HEAVY)))
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

static inline struct task_struct *task_of(struct sched_entity *se)
{
	return container_of(se, struct task_struct, se);
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

#define CAMERA_MAINTHREAD_NAME "com.oplus.camera"
#define OPLUS_CAMERA_MAINTHREAD_NAME "com.oplus.camera"
#define CAMERA_PREMR_NAME "previewManagerR"
#define CAMERA_PREPT_NAME "PreviewProcessT"
#define CAMERA_HALCONT_NAME "Camera Hal Cont"
#define CAMERA_IMAGEPROC_NAME "ImageProcessThr"
extern pid_t sf_pid;
extern pid_t re_pid;
void sched_assist_target_comm(struct task_struct *task)
{
	struct task_struct *grp_leader = task->group_leader;

	if (unlikely(!sysctl_sched_assist_enabled))
		return;

	if (!grp_leader || (get_ux_state_type(task) != UX_STATE_NONE))
		return;
	if (strstr(task->comm, "surfaceflinger") && strstr(grp_leader->comm, "surfaceflinger")) {
			sf_pid = task->pid;
			return;
	}

	if (strstr(task->comm, "RenderEngine") && strstr(grp_leader->comm, "surfaceflinger")) {
			re_pid = task->pid;
			return;
	}
#ifndef CONFIG_OPLUS_SYSTEM_KERNEL_QCOM
	/* mali thread only exist in mtk platform */
	if (strstr(grp_leader->comm, LAUNCHER_THREAD_NAME) && strstr(task->comm, MALI_THREAD_NAME)) {
		task->ux_state |= SA_TYPE_ANIMATOR;
		return;
	}
#endif

	if (strstr(grp_leader->comm, CAMERA_PROVIDER_NAME) && strstr(task->comm, CAMERA_PROVIDER_NAME)) {
		task->ux_state |= SA_TYPE_ID_CAMERA_PROVIDER;
		return;
	}

	if ((strstr(grp_leader->comm, CAMERA_MAINTHREAD_NAME) || strstr(grp_leader->comm, OPLUS_CAMERA_MAINTHREAD_NAME)) && (strstr(task->comm, CAMERA_PREMR_NAME)
		|| strstr(task->comm, CAMERA_PREPT_NAME)
		|| strstr(task->comm, CAMERA_HALCONT_NAME)
		|| strstr(task->comm, CAMERA_IMAGEPROC_NAME))) {
		task->ux_state |= SA_TYPE_LIGHT;
		return;
	}
	if (!strncmp(grp_leader->comm, ALLOCATOR_THREAD_NAME, TASK_COMM_LEN) || !strncmp(task->comm, ALLOCATOR_THREAD_NAME, TASK_COMM_LEN)) {
		task->ux_state |= SA_TYPE_ID_ALLOCATOR_SER;
	}

	return;
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

#ifdef CONFIG_MMAP_LOCK_OPT
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

	if (test_task_identify_ux(se_task, SA_TYPE_ID_CAMERA_PROVIDER)) {
		vruntime -= 2 * thresh + (thresh >> 1);
		se->vruntime = vruntime - (launch_adjust >> 1);
		return;
	}
}

bool should_ux_preempt_wakeup(struct task_struct *wake_task, struct task_struct *curr_task)
{
	bool wake_ux = false;
	bool curr_ux = false;

	if (!sysctl_sched_assist_enabled)
		return false;

	wake_ux = test_task_ux(wake_task) || test_list_pick_ux(wake_task) || test_task_identify_ux(wake_task, SA_TYPE_ID_CAMERA_PROVIDER);
	curr_ux = test_task_ux(curr_task) || test_list_pick_ux(curr_task) || test_task_identify_ux(curr_task, SA_TYPE_ID_CAMERA_PROVIDER);

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

	if(cpu < 0)
        	return false;

	/* only one cluster or init failed */
	if (unlikely(cls_nr <= 0))
		return true;

	if (test_sched_assist_ux_type(task, SA_TYPE_HEAVY)) {
		return capacity_orig_of(cpu) >= ux_cputopo.sched_cls[cls_nr].capacity;
	}

	return true;
}

bool should_ux_task_skip_cpu(struct task_struct *task, unsigned int cpu)
{
	if (!sysctl_sched_assist_enabled || !test_task_ux(task))
		return false;

	if ((sysctl_sched_assist_scene & SA_LAUNCH) && !is_ux_task_prefer_cpu(task, cpu))
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

void set_ux_task_to_prefer_cpu_v1(struct task_struct *task, int *orig_target_cpu, bool *cond) {
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

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
		if (cpu_online(cpu) && !cpu_isolated(cpu) && cpumask_test_cpu(cpu, task->cpus_ptr)) {
#else
		if (cpu_online(cpu) && !cpu_isolated(cpu) && cpumask_test_cpu(cpu, &task->cpus_allowed)) {
#endif
			*orig_target_cpu = cpu;
			return;
		}
	}

	cls_nr = cls_nr - 1;
	if (cls_nr > 0)
		goto retry;

	return;
}

void set_ux_task_to_prefer_cpu(struct task_struct *task, int *orig_target_cpu) {
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
retry:
	for_each_cpu(cpu, &ux_cputopo.sched_cls[cls_nr].cpus) {
		rq = cpu_rq(cpu);
		if (test_sched_assist_ux_type(rq->curr, SA_TYPE_HEAVY))
			continue;

		if (rq->curr->prio < MAX_RT_PRIO)
			continue;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
		if (cpu_online(cpu) && !cpu_isolated(cpu) && cpumask_test_cpu(cpu, task->cpus_ptr)) {
#else
		if (cpu_online(cpu) && !cpu_isolated(cpu) && cpumask_test_cpu(cpu, &task->cpus_allowed)) {
#endif
			*orig_target_cpu = cpu;
			return;
		}
	}

	cls_nr = cls_nr - 1;
	if (cls_nr > 0)
		goto retry;

	return;
}

void set_inherit_ux(struct task_struct *task, int type, int depth, int inherit_val)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,9,0)
	unsigned long flags;
#else
	struct rq_flags flags;
#endif
	struct rq *rq = NULL;
	int old_state = 0;

	if (!task || type >= INHERIT_UX_MAX) {
		return;
	}

	rq = task_rq_lock(task, &flags);

	if (task->sched_class != &fair_sched_class) {
		task_rq_unlock(rq, task, &flags);
		return;
	}

	inherit_ux_inc(task, type);
	task->ux_depth = depth + 1;
	old_state = task->ux_state;
	task->ux_state = (inherit_val & SCHED_ASSIST_UX_MASK) | SA_TYPE_INHERIT;
	/* identify type like allocator ux, keep it, but can not inherit  */
	if (old_state & SA_TYPE_ID_ALLOCATOR_SER)
		task->ux_state |= SA_TYPE_ID_ALLOCATOR_SER;
	if (old_state & SA_TYPE_ID_CAMERA_PROVIDER)
		task->ux_state |= SA_TYPE_ID_CAMERA_PROVIDER;
	task->inherit_ux_start = jiffies_to_nsecs(jiffies);

	sched_assist_systrace_pid(task->tgid, task->ux_state, "ux_state %d", task->pid);

	task_rq_unlock(rq, task, &flags);
}

void reset_inherit_ux(struct task_struct *inherit_task, struct task_struct *ux_task, int reset_type)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,9,0)
	unsigned long flags;
#else
	struct rq_flags flags;
#endif
	struct rq *rq;
	int reset_depth = 0;
	int reset_inherit = 0;

	if (!inherit_task || !ux_task || reset_type >= INHERIT_UX_MAX) {
		return;
	}

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
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,9,0)
	unsigned long flags;
#else
	struct rq_flags flags;
#endif
	struct rq *rq;
	s64 inherit_ux;

	if (!task || type >= INHERIT_UX_MAX) {
		return;
	}

	rq = task_rq_lock(task, &flags);

	inherit_ux_sub(task, type, value);
	inherit_ux = atomic64_read(&task->inherit_ux);
	if (inherit_ux > 0) {
		task_rq_unlock(rq, task, &flags);
		return;
	}
	if (inherit_ux < 0) {
		atomic64_set(&(task->inherit_ux), 0);
	}
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

void inc_inherit_ux_refs(struct task_struct *task, int type) {
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,9,0)
	unsigned long flags;
#else
	struct rq_flags flags;
#endif
	struct rq *rq;

	rq = task_rq_lock(task, &flags);
	inherit_ux_inc(task, type);
	task_rq_unlock(rq, task, &flags);
}

bool task_is_sf_group(struct task_struct *p)
{
	return (p->pid == sf_pid) || (p->pid == re_pid);
}

void sf_task_util_record(struct task_struct *p)
{
	char comm_now[TASK_COMM_LEN];
	unsigned long walt_util;
	int i = 0;
	int len = 0;
	memset(comm_now, '\0', sizeof(comm_now));
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
	walt_util = p->wts.demand_scaled;
#else
	walt_util = p->ravg.demand_scaled;
#endif
	if (unlikely(task_is_sf_group(p))) {
		strcpy(comm_now, p->comm);
		len = strlen(comm_now);
		for(i = 0; i < SF_GROUP_COUNT ;i++) {
			if (!strncmp(comm_now, sf_target[i].val, len))
				sf_target[i].util = walt_util;
		}
	}
}

bool sf_task_misfit(struct task_struct *p)
{
	unsigned long  util = 0;
	int i;
	if (task_is_sf_group(p)) {
		for (i = 0; i < SF_GROUP_COUNT; i++) {
			util += sf_target[i].util;
		}
		if ((util > sysctl_boost_task_threshold) && slide_scene())
			return true;
		else
			return false;
	}
	return false;
}

bool oplus_task_misfit(struct task_struct *p, int cpu)
{
#ifdef CONFIG_SCHED_WALT
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
	#ifdef CONFIG_OPLUS_SYSTEM_KERNEL_QCOM
	int num_mincpu = cpumask_weight(topology_core_cpumask(0));
	if ((scale_demand(p->wts.sum) >= sysctl_boost_task_threshold ||
		task_util(p) >= sysctl_boost_task_threshold) && cpu < num_mincpu)
	return true;
	#endif
#else
	int num_mincpu = cpumask_weight(topology_core_cpumask(0));
#ifdef CONFIG_OPLUS_SYSTEM_KERNEL_QCOM
	if ((scale_demand(p->ravg.sum) >= sysctl_boost_task_threshold ||
		task_util(p) >= sysctl_boost_task_threshold) && cpu < num_mincpu)
#else
	if ((scale_demand(p->ravg.sum) >= sysctl_boost_task_threshold ||
                READ_ONCE(p->se.avg.util_avg) > sysctl_boost_task_threshold) && cpu < num_mincpu)
#endif
		return true;
#endif
#endif
	return false;
}
void kick_min_cpu_from_mask(struct cpumask *lowest_mask)
{
	unsigned int cpu = cpumask_first(lowest_mask);
	while(cpu < nr_cpu_ids) {
		if (cpu < ux_prefer_cpu[0]) {
			cpumask_clear_cpu(cpu, lowest_mask);
		}
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
bool is_special_entry(struct dentry *dentry, const char* special_proc)
{
	const unsigned char *name;
	if (NULL == dentry || NULL == special_proc)
		return false;

	name = dentry->d_name.name;
	if (NULL != name && !strncmp(special_proc, name, 32))
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
	if (!p) {
		return -ESRCH;
	}
	task_lock(p);
	seq_printf(m, "%d\n", p->ux_state);
	task_unlock(p);
	put_task_struct(p);
	return 0;
}

static int proc_ux_state_open(struct inode* inode, struct file *filp)
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
	if (copy_from_user(buffer, buf, count)) {
		return -EFAULT;
	}

	err = kstrtoint(strstrip(buffer), 0, &ux_state);
	if(err) {
		return err;
	}
	task = get_proc_task(file_inode(file));
	if (!task) {
		return -ESRCH;
	}

	if (ux_state < 0) {
		return -EINVAL;
	}

	if (ux_state == SA_OPT_CLEAR) { /* clear all ux type but animator */
		task->ux_state &= ~(SA_TYPE_LISTPICK | SA_TYPE_HEAVY | SA_TYPE_LIGHT);
	} else if (ux_state & SA_OPT_SET) { /* set target ux type and clear set opt */
		task->ux_state |= ux_state & (~SA_OPT_SET);
	} else if (task->ux_state & ux_state) { /* reset target ux type */
		task->ux_state &= ~ux_state;
	}
	sched_assist_systrace_pid(task->tgid, task->ux_state, "ux_state %d", task->pid);

	put_task_struct(task);
	return count;
}

static ssize_t proc_ux_state_read(struct file* file, char __user *buf,
		size_t count, loff_t *ppos)
{
	char buffer[256];
	struct task_struct *task = NULL;
	int ux_state = -1;
	size_t len = 0;
	task = get_proc_task(file_inode(file));
	if (!task) {
		return -ESRCH;
	}
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

	if (sysctl_sched_assist_scene == SA_SCENE_OPT_CLEAR) {
		goto out;
	}

	if (sysctl_sched_assist_scene & SA_SCENE_OPT_SET) {
		save_sa |= sysctl_sched_assist_scene & (~SA_SCENE_OPT_SET);
	} else if (save_sa & sysctl_sched_assist_scene) {
		save_sa &= ~sysctl_sched_assist_scene;
	}

	sysctl_sched_assist_scene = save_sa;
	sched_assist_systrace(sysctl_sched_assist_scene, "scene");
out:
	mutex_unlock(&sa_scene_mutex);

	return result;
}
#ifdef CONFIG_MMAP_LOCK_OPT
void uxchain_rwsem_wake(struct task_struct *tsk, struct rw_semaphore *sem)
{
	int set_ux_once;

	if (current->mm) {
		set_ux_once = (sem == &(current->mm->mmap_sem));
		if (set_ux_once && sysctl_uxchain_v2)
			tsk->ux_once = 1;
	}
}
void uxchain_rwsem_down(struct rw_semaphore *sem)
{
	if (current->mm && sem == &(current->mm->mmap_sem) && sysctl_uxchain_v2) {
		current->get_mmlock = 1;
		current->get_mmlock_ts = sched_clock();
	}
}

void uxchain_rwsem_up(struct rw_semaphore *sem)
{
	if (current->mm && sem == &(current->mm->mmap_sem) &&
		current->get_mmlock == 1 && sysctl_uxchain_v2) {
		current->get_mmlock = 0;
	}
}
#endif

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
	if(cgroup_check_set_sched_assist_boost(p)) {
		if (get_st_group_id(p) == TOP_APP_GROUP_ID) {
			p->ux_state |= SA_TYPE_HEAVY;
		} else
			p->ux_state &= ~SA_TYPE_HEAVY;
	} else {
		return;
	}
}
