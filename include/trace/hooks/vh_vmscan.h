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

DECLARE_HOOK(android_vh_tune_scan_type,
	TP_PROTO(char *scan_type),
	TP_ARGS(scan_type))

DECLARE_HOOK(android_vh_check_throttle,
	TP_PROTO(int *throttle),
	TP_ARGS(throttle));

DECLARE_HOOK(android_vh_check_process_reclaimer,
	TP_PROTO(int *is_process_reclaimer),
	TP_ARGS(is_process_reclaimer));

DECLARE_HOOK(android_vh_alloc_pages_slowpath,
        TP_PROTO(gfp_t gfp_mask, unsigned int order, unsigned long delta),
        TP_ARGS(gfp_mask, order, delta));

struct cftype;
struct cgroup_subsys_state;
struct cgroup_subsys;
DECLARE_HOOK(android_vh_mem_cgroup_alloc,
	TP_PROTO(struct mem_cgroup *memcg),
	TP_ARGS(memcg));

DECLARE_HOOK(android_vh_mem_cgroup_free,
	TP_PROTO(struct mem_cgroup *memcg),
	TP_ARGS(memcg));

DECLARE_HOOK(android_vh_mem_cgroup_id_remove,
	TP_PROTO(struct mem_cgroup *memcg),
	TP_ARGS(memcg));

DECLARE_HOOK(android_vh_mem_cgroup_css_online,
	TP_PROTO(struct cgroup_subsys_state *css,
        struct mem_cgroup *memcg),
	TP_ARGS(css, memcg));

DECLARE_HOOK(android_vh_mem_cgroup_css_offline,
	TP_PROTO(struct cgroup_subsys_state *css,
        struct mem_cgroup *memcg),
	TP_ARGS(css, memcg));

DECLARE_HOOK(android_vh_meminfo_proc_show,
	TP_PROTO(struct seq_file *m),
	TP_ARGS(m));

DECLARE_HOOK(android_vh_rmqueue,
	TP_PROTO(struct zone *preferred_zone, struct zone *zone,
		unsigned int order, gfp_t gfp_flags,
		unsigned int alloc_flags, int migratetype),
	TP_ARGS(preferred_zone, zone, order,
		gfp_flags, alloc_flags, migratetype));
#else
#define trace_android_vh_set_swappiness(swappiness)
#define trace_android_vh_set_inactive_ratio(inactive_ratio, file)
#define trace_android_vh_check_throttle(throttle)
#define trace_android_vh_check_process_reclaimer(task)
#define trace_android_vh_alloc_pages_slowpath(gfp_mask, order, delta);
#define trace_android_vh_rmqueue(preferred_zone, zone, order, gfp_flags,\
        alloc_flags, migratetype);
#define trace_android_vh_mem_cgroup_alloc(memcg)
#define trace_android_vh_mem_cgroup_free(memcg)
#define trace_android_vh_mem_cgroup_id_remove(memcg)
#define trace_android_vh_mem_cgroup_css_online(css, memcg);
#define trace_android_vh_mem_cgroup_css_offline(css, memcg)
#define trace_android_vh_meminfo_proc_show(m)
#endif
#endif /* _TRACE_HOOK_VMSCAN_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
