/* SPDX-License-Identifier: GPL-2.0 */
/*
* Copyright (C) 2018-2020 Oplus. All rights reserved.
*/

#undef TRACE_SYSTEM
#define TRACE_SYSTEM vh_oplus_kernel2user
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks
#if !defined(_TRACE_HOOK_OPLUS_KERNEL2USER_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_OPLUS_KERNEL2USER_H
#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>
/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */
#if defined(CONFIG_TRACEPOINTS) && defined(CONFIG_ANDROID_VENDOR_HOOKS)
struct sock;

DECLARE_HOOK(android_vh_oplus_handle_retransmit,
	TP_PROTO(const struct sock *sk,
		int type),
	TP_ARGS(sk, type));
#else
#define android_vh_oplus_handle_retransmit(sk, type)
#endif
#endif /* _TRACE_HOOK_OPLUS_KERNEL2USER_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
