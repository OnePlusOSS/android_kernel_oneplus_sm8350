// SPDX-License-Identifier: GPL-2.0-only
/*
* Copyright (C) 2020-2022 Oplus. All rights reserved.
*/

#define pr_fmt(fmt) "zram_opt: " fmt

#include <linux/module.h>
#include <trace/hooks/vh_vmscan.h>
#include <linux/swap.h>

static int g_direct_swappiness = 60;
static int g_swappiness = 160;

struct zram_opt_ops {
	void (*zo_set_swappiness)(void *data, int *swappiness);
	void (*zo_set_inactive_ratio)(void *data, unsigned long *inactive_ratio, bool file);
	void (*zo_check_throttle)(void *data, int *throttle);
};

static void zo_set_swappiness(void *data, int *swappiness)
{
	if (current_is_kswapd())
		*swappiness = g_swappiness;
	else
		*swappiness = g_direct_swappiness;

	return;
}

static void zo_set_inactive_ratio(void *data, unsigned long *inactive_ratio, bool file)
{
	if (file)
		*inactive_ratio = min(2UL, *inactive_ratio);
	else
		*inactive_ratio = 1;

	return;
}

static void zo_check_throttle(void *data, int *throttle)
{
	if (current->signal->oom_score_adj <= 0)
		*throttle = 0;

	return;
}

static const struct zram_opt_ops zo_ops  = {
	.zo_set_swappiness      = zo_set_swappiness,
	.zo_set_inactive_ratio  = zo_set_inactive_ratio,
	.zo_check_throttle  = zo_check_throttle,
};

static int __init zram_opt_init(void)
{
	int rc;
	const struct zram_opt_ops *ops = &zo_ops;

	rc = register_trace_android_vh_set_swappiness(ops->zo_set_swappiness, NULL);
	if (rc != 0) {
		pr_err("register_trace_android_vh_set_swappiness failed! rc=%d\n", rc);
		goto out;
	}

	rc = register_trace_android_vh_set_inactive_ratio(ops->zo_set_inactive_ratio, NULL);
	if (rc != 0) {
		pr_err("register_trace_android_vh_set_inactive_ratio failed! rc=%d\n", rc);
		goto error_unregister_trace_init_swap_para;
	}

	rc = register_trace_android_vh_check_throttle(ops->zo_check_throttle, NULL);
	if (rc != 0) {
		pr_err("register_trace_android_vh_check_throttle failed! rc=%d\n", rc);
		goto error_unregister_trace_inactive_ratio;
	}

	return rc;

error_unregister_trace_inactive_ratio:
	unregister_trace_android_vh_set_inactive_ratio(ops->zo_set_inactive_ratio, NULL);
error_unregister_trace_init_swap_para:
	unregister_trace_android_vh_set_swappiness(ops->zo_set_swappiness, NULL);
out:
	return rc;
}

static void __exit zram_opt_exit(void)
{
	const struct zram_opt_ops *ops = &zo_ops;

	unregister_trace_android_vh_set_swappiness(ops->zo_set_swappiness, NULL);
	unregister_trace_android_vh_set_inactive_ratio(ops->zo_set_inactive_ratio, NULL);
	unregister_trace_android_vh_check_throttle(ops->zo_check_throttle, NULL);

	return;
}


module_init(zram_opt_init);
module_exit(zram_opt_exit);

module_param_named(vm_swappiness, g_swappiness, int, S_IRUGO | S_IWUSR);
module_param_named(direct_vm_swappiness, g_direct_swappiness, int, S_IRUGO | S_IWUSR);

MODULE_LICENSE("GPL v2");
