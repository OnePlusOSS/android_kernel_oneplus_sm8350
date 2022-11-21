/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-License-Identifier: GPL-2.0-only */
/*
* Copyright (C) 2018-2020 Oplus. All rights reserved.
*/
#undef TRACE_SYSTEM
#define TRACE_SYSTEM vh_oplus_app_monitor
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks
#if !defined(_TRACE_HOOK_VH_OPLUS_APP_MONITOR_H_) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_VH_OPLUS_APP_MONITOR_H_
#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>
/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */
#if defined(CONFIG_TRACEPOINTS) && defined(CONFIG_ANDROID_VENDOR_HOOKS)
struct sock;
struct sk_buff;


DECLARE_HOOK(android_vh_oplus_app_monitor_update,
	TP_PROTO(struct sock *sk,
		const struct sk_buff *skb,
		int send,
		int retrans),
	TP_ARGS(sk, skb, send, retrans));
#else
#define android_vh_oplus_app_monitor_update(sk, skb, send, retrans)
#endif
#endif /* _TRACE_HOOK_VH_OPLUS_APP_MONITOR_H_ */
/* This part must be outside protection */
#include <trace/define_trace.h>
