/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM secureguard
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks
#if !defined(_TRACE_HOOK_SECUREGUARD_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_SECUREGUARD_H
#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>
/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */
#if defined(CONFIG_TRACEPOINTS) && defined(CONFIG_ANDROID_VENDOR_HOOKS)
struct pt_regs;
DECLARE_HOOK(android_vh_secureguard_pre_handle,
	TP_PROTO(struct pt_regs *regs,
		unsigned int scno,
	  unsigned int sc_nr,
		unsigned int *id_buf),
	TP_ARGS(regs, scno, sc_nr, id_buf));
DECLARE_HOOK(android_vh_secureguard_post_handle,
	TP_PROTO(struct pt_regs *regs,
		unsigned int scno,
	  unsigned int sc_nr,
		unsigned int *id_buf),
	TP_ARGS(regs, scno, sc_nr, id_buf));
struct file;
DECLARE_HOOK(android_vh_secureguard_exec_block,
	TP_PROTO(struct file *file,
		int *p_retval),
	TP_ARGS(file, p_retval));
DECLARE_HOOK(android_vh_secureguard_mount_block,
	TP_PROTO(const char *dir_name,
		unsigned long flags,
		int *p_retval),
	TP_ARGS(dir_name, flags, p_retval));
struct kernel_packet_info;
DECLARE_HOOK(android_vh_secureguard_send_to_user,
	TP_PROTO(struct kernel_packet_info *userinfo,
		int *p_retval),
	TP_ARGS(userinfo, p_retval));
#else
#define trace_android_vh_secureguard_pre_handle(regs, scno, sc_nr, id_buf)
#define trace_android_vh_secureguard_post_handle(regs, scno, sc_nr, id_buf)
#define trace_android_vh_secureguard_exec_block(file, p_retval)
#define trace_android_vh_secureguard_mount_block(dir_name, flags, p_retval)
#define trace_android_vh_secureguard_send_to_user(userinfo, p_retval)
#endif
#endif /* _TRACE_HOOK_INPUT_H */
/* This part must be outside protection */
#include <trace/define_trace.h>