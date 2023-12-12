/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM oplus_ufs
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks
#if !defined(_TRACE_HOOK_OPLUS_UFS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_OPLUS_UFS_H
#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>
#include "../drivers/scsi/ufs/ufshcd.h"
/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */

#if defined(CONFIG_TRACEPOINTS) && defined(CONFIG_ANDROID_VENDOR_HOOKS)
struct ufs_hba;
DECLARE_HOOK(android_vh_ufs_gen_proc_devinfo,
	TP_PROTO(struct ufs_hba *hba),
	TP_ARGS(hba));
DECLARE_HOOK(android_vh_ufs_latency_hist,
	TP_PROTO(struct ufs_hba *hba, struct ufshcd_lrb *lrbp),
	TP_ARGS(hba, lrbp));
DECLARE_HOOK(android_vh_ufs_compl_command,
	TP_PROTO(struct ufs_hba *hba, struct ufshcd_lrb *lrbp),
	TP_ARGS(hba, lrbp));
#else
#define trace_android_vh_ufs_gen_proc_devinfo(hba)
#define trace_android_vh_ufs_latency_hist(hba, lrbp)
#endif
#endif /* _TRACE_HOOK_OPLUS_UFS_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
