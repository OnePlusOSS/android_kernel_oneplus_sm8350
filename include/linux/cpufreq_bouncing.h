// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2019-2021 Oplus. All rights reserved.
 */

#ifndef __CPUFREQ_BOUNCING_H__
#define __CPUFREQ_BOUNCING_H__

#include <linux/cpufreq.h>

void cb_update(struct cpufreq_policy *pol, u64 time);
void cb_reset(int cpu, u64 time);
unsigned int cb_cap(struct cpufreq_policy *pol, unsigned int freq);

#endif /*__CPUFREQ_BOUNCING_H__ */
