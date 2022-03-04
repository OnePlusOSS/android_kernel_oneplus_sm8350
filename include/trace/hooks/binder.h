/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM binder
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks
#if !defined(_TRACE_HOOK_BINDER_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_BINDER_H
#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>
/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */
#if defined(CONFIG_TRACEPOINTS) && defined(CONFIG_ANDROID_VENDOR_HOOKS)
struct binder_transaction;
struct task_struct;
DECLARE_HOOK(android_vh_binder_transaction_init,
	TP_PROTO(struct binder_transaction *t),
	TP_ARGS(t));
DECLARE_HOOK(android_vh_binder_set_priority,
	TP_PROTO(struct binder_transaction *t, struct task_struct *task),
	TP_ARGS(t, task));
DECLARE_HOOK(android_vh_binder_restore_priority,
	TP_PROTO(struct binder_transaction *t, struct task_struct *task),
	TP_ARGS(t, task));
DECLARE_HOOK(android_vh_binder_proc_transaction,
	TP_PROTO(struct task_struct *caller_task, struct task_struct *binder_proc_task,
		struct task_struct *binder_th_task, int node_debug_id, unsigned int code, bool pending_async),
	TP_ARGS(caller_task, binder_proc_task, binder_th_task, node_debug_id, code, pending_async));
DECLARE_HOOK(android_vh_binder_new_ref,
	TP_PROTO(struct task_struct *proc, uint32_t ref_desc, int node_debug_id),
	TP_ARGS(proc, ref_desc, node_debug_id));
DECLARE_HOOK(android_vh_binder_del_ref,
	TP_PROTO(struct task_struct *proc, uint32_t ref_desc),
	TP_ARGS(proc, ref_desc));
#else
#define trace_android_vh_binder_transaction_init(t)
#define trace_android_vh_binder_set_priority(t, task)
#define trace_android_vh_binder_restore_priority(t, task)
#define trace_android_vh_binder_proc_transaction(caller_task, binder_proc_task, binder_th_task, node_debug_id, code, pending_async)
#define trace_android_vh_binder_new_ref(proc, ref_desc, node_debug_id)
#define trace_android_vh_binder_del_ref(proc, ref_desc)
#endif
#endif /* _TRACE_HOOK_BINDER_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
