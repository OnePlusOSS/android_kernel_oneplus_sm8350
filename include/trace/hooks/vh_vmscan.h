/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM vh_vmscan
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks
#if !defined(_TRACE_HOOK_VMSCAN_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_VMSCAN_H
#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>
/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */
#if defined(CONFIG_TRACEPOINTS) && defined(CONFIG_ANDROID_VENDOR_HOOKS)
DECLARE_HOOK(android_vh_set_swappiness,
	TP_PROTO(int *swappiness),
	TP_ARGS(swappiness));

DECLARE_HOOK(android_vh_set_inactive_ratio,
	TP_PROTO(unsigned long *inactive_ratio,
		bool file),
	TP_ARGS(inactive_ratio, file));

DECLARE_HOOK(android_vh_check_throttle,
	TP_PROTO(int *throttle),
	TP_ARGS(throttle));

DECLARE_HOOK(android_vh_check_process_reclaimer,
	TP_PROTO(int *is_process_reclaimer),
	TP_ARGS(is_process_reclaimer));
#else
#define trace_android_vh_set_swappiness(swappiness)
#define trace_android_vh_set_inactive_ratio(inactive_ratio, file)
#define trace_android_vh_check_throttle(throttle)
#define trace_android_vh_check_process_reclaimer(task)
#endif
#endif /* _TRACE_HOOK_VMSCAN_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
