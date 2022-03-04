// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 Oplus. All rights reserved.
 */


#include <linux/sched.h>
#include <linux/uaccess.h>
#include <linux/pid.h>
#include <linux/version.h>
#include <linux/sched_assist/sched_assist_common.h>

#define CREATE_TRACE_POINTS
#include "sched_assist_trace.h"

struct task_struct *get_futex_owner_by_pid(u32 owner_tid)
{
	struct task_struct *futex_owner = NULL;

	if (owner_tid > 0 && owner_tid <= PID_MAX_DEFAULT) {
		rcu_read_lock();
		futex_owner = find_task_by_vpid(owner_tid);
		rcu_read_unlock();
		if (futex_owner == NULL)
			ux_warn("failed to find task by pid(curr:%-12s pid:%d)\n", current->comm, owner_tid);
	}

	return futex_owner;
}

struct task_struct *get_futex_owner_by_pid_v2(u32 owner_tid, int *pid, int *tgid)
{
	struct task_struct *futex_owner = NULL;

	if (owner_tid > 0 && owner_tid <= PID_MAX_DEFAULT) {
		futex_owner = find_get_task_by_vpid(owner_tid);

		if (futex_owner == NULL)
			ux_warn("failed to find task by pid(curr:%-12s pid:%d)\n", current->comm, owner_tid);
	}

	return futex_owner;
}

struct task_struct *get_futex_owner(u32 owner_tid)
{
	struct task_struct *futex_owner = NULL;

	if (owner_tid > 0 && owner_tid <= PID_MAX_DEFAULT) {
		rcu_read_lock();
		futex_owner = find_task_by_vpid(owner_tid);
		rcu_read_unlock();
		if (futex_owner == NULL)
			ux_warn("failed to find task by pid(curr:%-12s pid:%d)\n", current->comm, owner_tid);
	}

	return futex_owner;
}

void futex_set_inherit_ux(struct task_struct *owner, struct task_struct *task)
{
	bool is_ux = false;

	is_ux = test_set_inherit_ux(task);

	if (is_ux && owner && !test_task_ux(owner))
		set_inherit_ux(owner, INHERIT_UX_FUTEX, task->ux_depth, task->ux_state);
}

void futex_unset_inherit_ux(struct task_struct *task)
{
	if (test_inherit_ux(task, INHERIT_UX_FUTEX))
		unset_inherit_ux(task, INHERIT_UX_FUTEX);
}

void futex_set_inherit_ux_refs(struct task_struct *owner, struct task_struct *task)
{
	bool is_ux = test_set_inherit_ux(task);

	if (is_ux && owner) {
		bool type = get_ux_state_type(owner);

		if (type == UX_STATE_NONE)
			set_inherit_ux(owner, INHERIT_UX_FUTEX, task->ux_depth, task->ux_state);
		else
			inherit_ux_inc(owner, INHERIT_UX_FUTEX);
	}
}

int futex_set_inherit_ux_refs_v2(struct task_struct *owner, struct task_struct *task)
{
	bool is_ux = test_set_inherit_ux(task);

	if (is_ux && owner) {
		int type = get_ux_state_type(owner);

		if (type == UX_STATE_NONE) {
			set_inherit_ux(owner, INHERIT_UX_FUTEX, task->ux_depth, task->ux_state);
			return 1;
		} else if (type == UX_STATE_INHERIT) {
			inc_inherit_ux_refs(owner, INHERIT_UX_FUTEX);
			return 2;
		}
	}

	return 0;
}

void futex_unset_inherit_ux_refs(struct task_struct *task, int value)
{
	if (test_inherit_ux(task, INHERIT_UX_FUTEX))
		unset_inherit_ux_value(task, INHERIT_UX_FUTEX, value);
}
