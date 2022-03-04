#include <linux/im/im.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/version.h>

/* default list, empty string means no need to check */
static struct im_target {
	char val[64];
	const char* desc;
	int args[IM_ARGS_NUM];
} im_target [IM_ID_MAX] = {
	{"surfaceflinger", "sf ", {0, 0, 0}},
	{"", "kworker ", {0, 0, 0}},
	{"logd", "logd ", {0, 0, 0}},
	{"logcat", "logcat ", {0, 0, 0}},
	{"", "main ", {0, 0, 0}},
	{"", "enqueue ", {0, 0, 0}},
	{"", "gl ", {0, 0, 0}},
	{"", "vk ", {0, 0, 0}},
	{"composer-servic", "hwc ", {0, 0, 0}},
	{"HwBinder:", "hwbinder ", {0, 0, 0}},
	{"Binder:", "binder ", {0, 0, 0}},
	{"hwuiTask", "hwui ", {0, 0, 0}},
	{"", "render ", {0, 0, 0}},
	{"", "unity_wk", {0, 0, 0}},
	{"UnityMain", "unityM", {0, 0, 0}},
	{"neplus.launcher", "launcher ", {0, 0, 0}},
	{"HwuiTask", "HwuiEx ", {0, 0, 0}},
	{"bitmap thread", "bmt ", {0, 0, 0}},
	{"CrRendererMain", "crender ", {3, 0, 1024}},
	{"kswapd", "kswapd ", {0, 0, 800}},
	{"HeapTaskDaemon", "HeapTaskDaemon ", {0, 0, 800}},
	{"dex2oat", "dex2oat ", {0, 0, 800}},
};

/* ignore list, not set any im_flag */
static char target_ignore_prefix[IM_IG_MAX][64] = {
	"Prober_",
	"DispSync",
	"app",
	"sf",
	"ScreenShotThrea",
	"DPPS_THREAD",
	"LTM_THREAD",
};

static bool debug = false;
module_param_named(debug, debug, bool, 0644);

void im_to_str(int flag, char* desc, int size)
{
	char *base = desc;
	int i;

	for (i = 0; i < IM_ID_MAX; ++i) {
		if (flag & (1 << i)) {
			size_t len = strlen(im_target[i].desc);

			if (len) {
				if (size <= base - desc + len) {
					pr_warn("im tag desc too long\n");
					return;
				}
				strncpy(base, im_target[i].desc, len);
				base += len;
			}
		}
	}
}

static inline bool im_ignore(struct task_struct *task, int idx)
{
	size_t tlen = 0, len = 0;

	tlen = strlen(target_ignore_prefix[idx]);
	if (tlen == 0)
		return false;

	/* NOTE: task->comm has only 16 bytes */
	len = strlen(task->comm);
	if (len < tlen)
		return false;

	if (!strncmp(task->comm, target_ignore_prefix[idx], tlen)) {
		task->im_flag = 0;
		return true;
	}
	return false;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
#define UCLAMP_BUCKET_DELTA DIV_ROUND_CLOSEST(SCHED_CAPACITY_SCALE, UCLAMP_BUCKETS)
static inline unsigned int uclamp_bucket_id(unsigned int clamp_value)
{
	return clamp_value / UCLAMP_BUCKET_DELTA;
}

static inline void uclamp_se_set(struct uclamp_se *uc_se,
	unsigned int value, bool user_defined)
{
	uc_se->value = value;
	uc_se->bucket_id = uclamp_bucket_id(value);
	uc_se->user_defined = user_defined;
}
#endif

static inline void im_special_treat(struct task_struct* task, int idx)
{
#ifdef CONFIG_OPLUS_FEATURE_TPD
	task->tpd_st = im_target[idx].args[0];
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
	uclamp_se_set(&task->uclamp_req[UCLAMP_MIN], im_target[idx].args[1], false); // keep false to allow user define
	uclamp_se_set(&task->uclamp_req[UCLAMP_MAX], im_target[idx].args[2], false);
#endif
	if (debug) {
		pr_warn("tagging: task %s %d set idx %d, args(%d %d %d)\n",
			task->comm, task->pid, idx, im_target[idx].args[0],
			im_target[idx].args[1], im_target[idx].args[2]);
	}
}

static inline void im_tagging(struct task_struct *task, int idx)
{
	size_t tlen = 0, len = 0;

	if (debug)
		pr_warn("tagging: task %s %d\n", task->comm, task->pid);

	tlen = strlen(im_target[idx].val);
	if (tlen == 0)
		return;

	/* NOTE: task->comm has only 16 bytes */
	len = strlen(task->comm);

	/* non restrict tagging for some prefixed tasks*/
	if (len < tlen)
		return;

	/* prefix cases */
	if (!strncmp(task->comm, im_target[idx].val, tlen)) {
		switch (idx) {
		case IM_ID_HWBINDER:
			task->im_flag |= IM_HWBINDER;
			break;
		case IM_ID_BINDER:
			task->im_flag |= IM_BINDER;
			break;
		case IM_ID_HWUI:
			task->im_flag |= IM_HWUI;
			break;
		case IM_ID_HWUI_EX:
			task->im_flag |= IM_HWUI_EX;
			break;
		case IM_ID_KSWAPD:
			task->im_flag |= IM_KSWAPD;
			im_special_treat(task, idx);
			break;
		}
	}

	/* restrict tagging for specific identical tasks */
	if (len != tlen)
		return;

	if (!strncmp(task->comm, im_target[idx].val, len)) {
		switch (idx) {
		case IM_ID_SURFACEFLINGER:
			task->im_flag |= IM_SURFACEFLINGER;
			break;
		case IM_ID_LOGD:
			task->im_flag |= IM_LOGD;
			break;
		case IM_ID_LOGCAT:
			task->im_flag |= IM_LOGCAT;
			break;
		case IM_ID_HWC:
			task->im_flag |= IM_HWC;
			break;
		case IM_ID_LAUNCHER:
			task->im_flag |= IM_LAUNCHER;
			break;
		case IM_ID_RENDER:
			task->im_flag |= IM_RENDER;
			break;
		case IM_ID_UNITY_MAIN:
			task->im_flag |= IM_UNITY_MAIN;
			break;
		case IM_ID_BMT:
			task->im_flag |= IM_BMT;
			break;
		case IM_ID_CRENDER:
			task->im_flag |= IM_CRENDER;
			im_special_treat(task, idx);
			break;
		case IM_ID_HEAPTASKDAEMON:
			task->im_flag |= IM_HEAPTASKDAEMON;
			im_special_treat(task, idx);
			break;
		case IM_ID_DEX2OAT:
			task->im_flag |= IM_DEX2OAT;
			im_special_treat(task, idx);
			break;
		}
	}

	if (debug) {
		pr_warn("tagging: done task %s %d, %d (%d %d %d)\n",
			task->comm, task->pid, task->im_flag,
#ifdef CONFIG_OPLUS_FEATURE_TPD
			task->tpd,
#else
			-1,
#endif
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
			task->uclamp_req[UCLAMP_MIN],
			task->uclamp_req[UCLAMP_MAX]);
#else
			-1,
			-1);
#endif
	}
}

void im_wmi(struct task_struct *task)
{
	struct task_struct *leader = task, *p;
	int i = 0;

	/* check for ignore */
	for (i = 0; i < IM_IG_MAX; ++i)
		if (im_ignore(task, i))
			return;

	/* do the check and initial */
	task->im_flag = 0;
	for (i = 0; i < IM_ID_MAX; ++i)
		im_tagging(task, i);

	/* check leader part */
	rcu_read_lock();
	if (task != task->group_leader)
		leader = find_task_by_vpid(task->tgid);

	if (leader) {
		/* for hwc cases */
		if (im_hwc(leader)) {
			for_each_thread(task, p) {
				if (im_binder_related(p))
					p->im_flag |= IM_HWC;
			}
		}

		/* for sf cases */
		if (im_sf(leader) && im_binder_related(task))
			task->im_flag |= IM_SURFACEFLINGER;
	}
	rcu_read_unlock();
}

void im_set_flag(struct task_struct *task, int flag)
{
	/* for hwui boost purpose */
	im_tagging(current, IM_ID_HWUI);
	if (current->im_flag & IM_HWUI)
		return;

	im_tagging(current, IM_ID_HWUI_EX);
	if (current->im_flag & IM_HWUI_EX)
		return;

	/* set the flag */
	current->im_flag |= flag;

	/* if task with enqueue operation, then it's leader should be main thread */
	if (flag == IM_ENQUEUE) {
		struct task_struct *leader = current;

		rcu_read_lock();
		/* refetch leader */
		if (current != current->group_leader)
			leader = find_task_by_vpid(current->tgid);
		if (leader)
			leader->im_flag |= IM_MAIN;
		rcu_read_unlock();
	}
}

void im_unset_flag(struct task_struct *task, int flag)
{
	task->im_flag &= ~flag;
}

void im_reset_flag(struct task_struct *task)
{
	task->im_flag = 0;
}

void im_tsk_init_flag(void *ptr)
{
	struct task_struct *task = (struct task_struct*) ptr;

	task->im_flag &= ~(IM_HWUI & IM_HWUI_EX);
}
