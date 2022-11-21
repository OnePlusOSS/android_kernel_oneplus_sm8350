/* SPDX-License-Identifier: GPL-2.0-only */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM power

#if !defined(_KGSL_PWRTRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _KGSL_PWRTRACE_H

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE kgsl_pwrtrace

#include <linux/tracepoint.h>

/*
 * Tracepoint for power gpu_frequency
 */
TRACE_EVENT(gpu_frequency,
	TP_PROTO(unsigned int gpu_freq, unsigned int gpu_id),
	TP_ARGS(gpu_freq, gpu_id),
	TP_STRUCT__entry(
		__field(unsigned int, state)
		__field(unsigned int, gpu_id)
	),
	TP_fast_assign(
		__entry->state = gpu_freq;
		__entry->gpu_id = gpu_id;
	),

	TP_printk("gpu_freq=%luKhz gpu_id=%lu",
		(unsigned long)__entry->state,
		(unsigned long)__entry->gpu_id)
);

#endif /* _KGSL_PWRTRACE_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
