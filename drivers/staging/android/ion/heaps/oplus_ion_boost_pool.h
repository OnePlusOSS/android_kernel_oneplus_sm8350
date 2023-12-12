
#ifndef _ION_BOOST_POOL_H
#define _ION_BOOST_POOL_H

#include <linux/kthread.h>
#include <linux/types.h>

#include <linux/msm_ion.h>
#include "ion_msm_system_heap.h"

#define LOWORDER_WATER_MASK (64 * 4)
#define MAX_POOL_SIZE (128 * 64 * 4)

struct ion_boost_pool {
	char *name;
	struct task_struct *tsk, *prefill_tsk;
	int camera_low;
	int low, high, origin;
	pid_t camera_pid;
	unsigned long usage;
	unsigned int wait_flag, prefill_wait_flag;
	bool force_stop, prefill;
	struct mutex prefill_mutex;
	wait_queue_head_t waitq, prefill_waitq;
	struct proc_dir_entry *proc_info, *proc_low_info, *proc_stat, *proc_pid;
	struct ion_msm_page_pool *pools[0];
};

struct page_info *boost_pool_allocate(struct ion_boost_pool *pool,
				      unsigned long size,
				      unsigned int max_order);
int boost_pool_free(struct ion_boost_pool *pool, struct page *page,
		    int order);
int boost_pool_shrink(struct ion_boost_pool *boost_pool,
		      struct ion_msm_page_pool *pool, gfp_t gfp_mask,
		      int nr_to_scan);
struct ion_boost_pool *boost_pool_create(struct ion_msm_system_heap *heap,
					 unsigned int ion_flag,
					 unsigned int nr_pages,
					 struct proc_dir_entry *root_dir,
					 char *name);
void boost_pool_wakeup_process(struct ion_boost_pool *pool);
void boost_pool_dec_high(struct ion_boost_pool *pool, int nr_pages);
void boost_pool_dump(struct ion_boost_pool *pool);

bool kcrit_scene_init(void);
#endif /* _ION_SMART_POOL_H */
