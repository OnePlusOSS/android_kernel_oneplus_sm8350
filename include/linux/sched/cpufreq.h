/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SCHED_CPUFREQ_H
#define _LINUX_SCHED_CPUFREQ_H

#include <linux/types.h>

/*
 * Interface between cpufreq drivers and the scheduler:
 */

#define SCHED_CPUFREQ_IOWAIT	(1U << 0)
#define SCHED_CPUFREQ_MIGRATION	(1U << 1)
#define SCHED_CPUFREQ_INTERCLUSTER_MIG	(1U << 3)
#define SCHED_CPUFREQ_WALT	(1U << 4)
#define SCHED_CPUFREQ_PL	(1U << 5)
#define SCHED_CPUFREQ_EARLY_DET	(1U << 6)
#define SCHED_CPUFREQ_CONTINUE	(1U << 8)

#if defined(OPLUS_FEATURE_SCHED_ASSIST) && defined(CONFIG_OPLUS_FEATURE_SCHED_ASSIST)
#define SCHED_CPUFREQ_RESET (1U << 7)
#define SCHED_CPUFREQ_BOOST (1U << 9)
#endif /* defined(OPLUS_FEATURE_SCHED_ASSIST) && defined(CONFIG_OPLUS_FEATURE_SCHED_ASSIST) */

#ifdef CONFIG_CPU_FREQ
struct cpufreq_policy;

enum EM_CLUSTER_TYPE {
	EM_CLUSTER_MIN = 0,
	EM_CLUSTER_MID,
	EM_CLUSTER_MAX,
	EM_CLUSTER_NUM,
};

struct em_map_util_freq {
	int gov_id;
	void (*pgov_map_func)(void *data, unsigned long util, unsigned long freq,
			unsigned long cap, unsigned long *max_util, struct cpufreq_policy *policy,
			bool *need_freq_update);
};

struct cluster_em_map_util_freq {
	struct em_map_util_freq cem_map_util_freq[EM_CLUSTER_NUM];
};

extern struct cluster_em_map_util_freq g_em_map_util_freq;

struct update_util_data {
       void (*func)(struct update_util_data *data, u64 time, unsigned int flags);
};

void cpufreq_add_update_util_hook(int cpu, struct update_util_data *data,
                       void (*func)(struct update_util_data *data, u64 time,
				    unsigned int flags));
void cpufreq_remove_update_util_hook(int cpu);
bool cpufreq_this_cpu_can_update(struct cpufreq_policy *policy);

static inline unsigned long map_util_freq(unsigned long util,
					unsigned long freq, unsigned long cap)
{
	return (freq + (freq >> 2)) * util / cap;
}
#endif /* CONFIG_CPU_FREQ */
extern void default_em_map_util_freq(void *data, unsigned long util, unsigned long freq,
		unsigned long cap, unsigned long *max_util, struct cpufreq_policy *policy,
		bool *need_freq_update);

#endif /* _LINUX_SCHED_CPUFREQ_H */
