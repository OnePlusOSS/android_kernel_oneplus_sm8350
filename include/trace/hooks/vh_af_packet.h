/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM vh_af_packet
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks
#if !defined(_TRACE_HOOK_AF_PACKET_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_AF_PACKET_H
#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>
/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */
#if defined(CONFIG_TRACEPOINTS) && defined(CONFIG_ANDROID_VENDOR_HOOKS)
struct sock;
struct sk_buff;
struct net_device;
struct packet_type;

DECLARE_HOOK(android_vh_check_dhcp_pkt,
	TP_PROTO(struct sock *sk,
		struct sk_buff *skb,
		struct net_device *dev,
		struct packet_type *pt,
		int *do_drop),
	TP_ARGS(sk, skb, dev, pt, do_drop));
#else
#define android_vh_check_dhcp_pkt(sk, skb, dev, pt, do_drop)
#endif
#endif /* _TRACE_HOOK_AF_PACKET_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
