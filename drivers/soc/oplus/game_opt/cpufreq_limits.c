// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 Oplus. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/cpufreq.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>

#include "game_ctrl.h"

/* To handle cpufreq min/max request */
struct cpu_freq_status {
	unsigned int min;
	unsigned int max;
};

static DEFINE_PER_CPU(struct cpu_freq_status, game_cpu_stats);
static DEFINE_PER_CPU(struct freq_qos_request, qos_req_min);
static DEFINE_PER_CPU(struct freq_qos_request, qos_req_max);

static cpumask_var_t limit_mask_min;
static cpumask_var_t limit_mask_max;

static int freq_qos_request_init(void)
{
	unsigned int cpu;
	int ret;

	struct cpufreq_policy *policy;
	struct freq_qos_request *req;

	for_each_present_cpu(cpu) {
		policy = cpufreq_cpu_get(cpu);
		if (!policy) {
			pr_err("%s: Failed to get cpufreq policy for cpu%d\n",
				__func__, cpu);
			ret = -EINVAL;
			goto cleanup;
		}
		per_cpu(game_cpu_stats, cpu).min = 0;
		req = &per_cpu(qos_req_min, cpu);
		ret = freq_qos_add_request(&policy->constraints, req,
			FREQ_QOS_MIN, FREQ_QOS_MIN_DEFAULT_VALUE);
		if (ret < 0) {
			pr_err("%s: Failed to add min freq constraint (%d)\n",
				__func__, ret);
			cpufreq_cpu_put(policy);
			goto cleanup;
		}

		per_cpu(game_cpu_stats, cpu).max = UINT_MAX;
		req = &per_cpu(qos_req_max, cpu);
		ret = freq_qos_add_request(&policy->constraints, req,
			FREQ_QOS_MAX, FREQ_QOS_MAX_DEFAULT_VALUE);
		if (ret < 0) {
			pr_err("%s: Failed to add max freq constraint (%d)\n",
				__func__, ret);
			cpufreq_cpu_put(policy);
			goto cleanup;
		}

		cpufreq_cpu_put(policy);
	}
	return 0;

cleanup:
	for_each_present_cpu(cpu) {
		req = &per_cpu(qos_req_min, cpu);
		if (req && freq_qos_request_active(req))
			freq_qos_remove_request(req);


		req = &per_cpu(qos_req_max, cpu);
		if (req && freq_qos_request_active(req))
			freq_qos_remove_request(req);

		per_cpu(game_cpu_stats, cpu).min = 0;
		per_cpu(game_cpu_stats, cpu).max = UINT_MAX;
	}
	return ret;
}

static ssize_t set_cpu_min_freq(const char *buf, size_t count)
{
	int i, j, ntokens = 0;
	unsigned int val, cpu;
	const char *cp = buf;
	struct cpu_freq_status *i_cpu_stats;
	struct cpufreq_policy policy;
	struct freq_qos_request *req;

	while ((cp = strpbrk(cp + 1, " :")))
		ntokens++;

	/* CPU:value pair */
	if (!(ntokens % 2))
		return -EINVAL;

	cp = buf;
	cpumask_clear(limit_mask_min);
	for (i = 0; i < ntokens; i += 2) {
		if (sscanf(cp, "%u:%u", &cpu, &val) != 2)
			return -EINVAL;
		if (cpu > (num_present_cpus() - 1))
			return -EINVAL;

		i_cpu_stats = &per_cpu(game_cpu_stats, cpu);

		i_cpu_stats->min = val;
		cpumask_set_cpu(cpu, limit_mask_min);

		cp = strnchr(cp, strlen(cp), ' ');
		cp++;
	}

	/*
	 * Since on synchronous systems policy is shared amongst multiple
	 * CPUs only one CPU needs to be updated for the limit to be
	 * reflected for the entire cluster. We can avoid updating the policy
	 * of other CPUs in the cluster once it is done for at least one CPU
	 * in the cluster
	 */
	get_online_cpus();
	for_each_cpu(i, limit_mask_min) {
		i_cpu_stats = &per_cpu(game_cpu_stats, i);

		if (cpufreq_get_policy(&policy, i))
			continue;

		if (cpu_online(i)) {
			req = &per_cpu(qos_req_min, i);
			if (freq_qos_update_request(req, i_cpu_stats->min) < 0)
				break;
		}

		for_each_cpu(j, policy.related_cpus)
			cpumask_clear_cpu(j, limit_mask_min);
	}
	put_online_cpus();

	return count;
}

static ssize_t cpu_min_freq_proc_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	char page[256] = {0};
	int ret;

	ret = simple_write_to_buffer(page, sizeof(page) - 1, ppos, buf, count);
	if (ret <= 0)
		return ret;

	return set_cpu_min_freq(page, ret);
}

static int cpu_min_freq_show(struct seq_file *m, void *v)
{
	int cpu;

	for_each_present_cpu(cpu)
		seq_printf(m, "%d:%u ", cpu, per_cpu(game_cpu_stats, cpu).min);
	seq_printf(m, "\n");

	return 0;
}

static int cpu_min_freq_proc_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, cpu_min_freq_show, inode);
}

static const struct file_operations cpu_min_freq_proc_ops = {
	.open		= cpu_min_freq_proc_open,
	.write 	= cpu_min_freq_proc_write,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static ssize_t set_cpu_max_freq(const char *buf, size_t count)
{
	int i, j, ntokens = 0;
	unsigned int val, cpu;
	const char *cp = buf;
	struct cpu_freq_status *i_cpu_stats;
	struct cpufreq_policy policy;
	struct freq_qos_request *req;

	while ((cp = strpbrk(cp + 1, " :")))
		ntokens++;

	/* CPU:value pair */
	if (!(ntokens % 2))
		return -EINVAL;

	cp = buf;
	cpumask_clear(limit_mask_max);
	for (i = 0; i < ntokens; i += 2) {
		if (sscanf(cp, "%u:%u", &cpu, &val) != 2)
			return -EINVAL;
		if (cpu > (num_present_cpus() - 1))
			return -EINVAL;

		i_cpu_stats = &per_cpu(game_cpu_stats, cpu);

		i_cpu_stats->max = val;
		cpumask_set_cpu(cpu, limit_mask_max);

		cp = strnchr(cp, strlen(cp), ' ');
		cp++;
	}

	get_online_cpus();
	for_each_cpu(i, limit_mask_max) {
		i_cpu_stats = &per_cpu(game_cpu_stats, i);
		if (cpufreq_get_policy(&policy, i))
			continue;

		if (cpu_online(i)) {
			req = &per_cpu(qos_req_max, i);
			if (freq_qos_update_request(req, i_cpu_stats->max) < 0)
				break;
		}

		for_each_cpu(j, policy.related_cpus)
			cpumask_clear_cpu(j, limit_mask_max);
	}
	put_online_cpus();

	return count;
}

static ssize_t cpu_max_freq_proc_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	char page[256] = {0};
	int ret;

	ret = simple_write_to_buffer(page, sizeof(page) - 1, ppos, buf, count);
	if (ret <= 0)
		return ret;

	return set_cpu_max_freq(page, ret);
}

static int cpu_max_freq_show(struct seq_file *m, void *v)
{
	int cpu;

	for_each_present_cpu(cpu)
		seq_printf(m, "%d:%u ", cpu, per_cpu(game_cpu_stats, cpu).max);
	seq_printf(m, "\n");

	return 0;
}

static int cpu_max_freq_proc_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, cpu_max_freq_show, inode);
}

static const struct file_operations cpu_max_freq_proc_ops = {
	.open		= cpu_max_freq_proc_open,
	.write 	= cpu_max_freq_proc_write,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

int cpufreq_limits_init()
{
	int ret;

	if (unlikely(!game_opt_dir))
		return -ENOTDIR;

	if (!alloc_cpumask_var(&limit_mask_min, GFP_KERNEL))
		return -ENOMEM;

	if (!alloc_cpumask_var(&limit_mask_max, GFP_KERNEL)) {
		free_cpumask_var(limit_mask_min);
		return -ENOMEM;
	}

	ret = freq_qos_request_init();
	if (ret) {
		pr_err("%s: Failed to init qos requests policy for ret=%d\n",
			__func__, ret);
		return ret;
	}

	proc_create_data("cpu_min_freq", 0664, game_opt_dir, &cpu_min_freq_proc_ops, NULL);
	proc_create_data("cpu_max_freq", 0664, game_opt_dir, &cpu_max_freq_proc_ops, NULL);

	return 0;
}
