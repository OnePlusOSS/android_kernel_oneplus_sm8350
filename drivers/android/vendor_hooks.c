// SPDX-License-Identifier: GPL-2.0-only
/* vendor_hook.c
 *
 * Android Vendor Hook Support
 *
 * Copyright 2020 Google LLC
 */

#define CREATE_TRACE_POINTS
#include <trace/hooks/vendor_hooks.h>
#include <trace/hooks/sched.h>
#include <trace/hooks/dtask.h>
#include <trace/hooks/net.h>
#include <trace/hooks/binder.h>
#include <trace/hooks/rwsem.h>
#include <trace/hooks/futex.h>
#include <trace/hooks/fpsimd.h>
#include <trace/hooks/topology.h>
#include <trace/hooks/debug.h>
#include <trace/hooks/secureguard.h>
#include <trace/hooks/vh_vmscan.h>
#include <trace/hooks/oplus_ufs.h>
#include <trace/hooks/vh_af_packet.h>
#include <trace/hooks/minidump.h>
#include <trace/hooks/vh_oplus_app_monitor.h>
#include <trace/hooks/wqlockup.h>
#include <trace/hooks/sysrqcrash.h>
/*
 * Export tracepoints that act as a bare tracehook (ie: have no trace event
 * associated with them) to allow external modules to probe them.
 */
EXPORT_TRACEPOINT_SYMBOL_GPL(android_rvh_select_task_rq_fair);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_rvh_select_task_rq_rt);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_rvh_select_fallback_rq);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_scheduler_tick);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_rvh_enqueue_task);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_rvh_dequeue_task);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_rvh_can_migrate_task);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_rvh_find_lowest_rq);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_rvh_rtmutex_prepare_setprio);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_rvh_prepare_prio_fork);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_rvh_finish_prio_fork);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_rvh_set_user_nice);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_rvh_setscheduler);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_mutex_wait_start);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_mutex_wait_finish);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_rwsem_read_wait_start);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_rwsem_read_wait_finish);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_rwsem_write_wait_start);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_rwsem_write_wait_finish);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_sched_show_task);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_ptype_head);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_kfree_skb);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_rvh_sk_alloc);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_rvh_sk_free);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_rvh_nf_conn_alloc);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_rvh_nf_conn_free);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_arch_set_freq_scale);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_binder_transaction_init);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_binder_set_priority);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_binder_restore_priority);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_rwsem_init);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_rwsem_wake);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_rwsem_write_finished);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_alter_rwsem_list_add);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_alter_futex_plist_add);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_is_fpsimd_save);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_ipi_stop);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_secureguard_pre_handle);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_secureguard_post_handle);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_secureguard_exec_block);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_secureguard_mount_block);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_secureguard_send_to_user);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_set_swappiness);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_set_inactive_ratio);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_check_throttle);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_ufs_gen_proc_devinfo);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_ufs_latency_hist);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_check_dhcp_pkt);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_rvh_check_preempt_wakeup);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_rvh_check_preempt_tick);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_rvh_find_best_target);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_rvh_cpupri_find_fitness);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_printk_store);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_show_regs);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_check_process_reclaimer);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_oplus_app_monitor_update);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_wq_lockup_pool);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_sysrq_crash);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_rvh_find_busiest_group);
