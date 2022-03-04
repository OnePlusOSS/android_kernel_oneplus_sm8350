/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM midas_dev
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks
#if !defined(_TRACE_HOOK_MIDAS_DEV_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_MIDAS_DEV_H
#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>

/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */
#if defined(CONFIG_TRACEPOINTS) && defined(CONFIG_ANDROID_VENDOR_HOOKS)
DECLARE_HOOK(android_vh_midas_record_task_times,
	TP_PROTO(u64 cputime, struct task_struct *p, unsigned int state),
	TP_ARGS(cputime, p, state));
#else
#define trace_android_vh_midas_record_task_times(cputime, p, state)
#endif
#endif /* _TRACE_HOOK_MIDAS_DEV_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
