#include <linux/mm.h>
#include <linux/sched/mm.h>
#include <linux/module.h>
#include <linux/gfp.h>
#include <linux/kernel_stat.h>
#include <linux/swap.h>
#include <linux/pagemap.h>
#include <linux/init.h>
#include <linux/highmem.h>
#include <linux/vmpressure.h>
#include <linux/vmstat.h>
#include <linux/file.h>
#include <linux/writeback.h>
#include <linux/blkdev.h>
#include <linux/mm_inline.h>
#include <linux/backing-dev.h>
#include <linux/rmap.h>
#include <linux/topology.h>
#include <linux/cpu.h>
#include <linux/cpuset.h>
#include <linux/compaction.h>
#include <linux/notifier.h>
#include <linux/rwsem.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/freezer.h>
#include <linux/memcontrol.h>
#include <linux/delayacct.h>
#include <linux/sysctl.h>
#include <linux/oom.h>
#include <linux/prefetch.h>
#include <linux/printk.h>
#include <linux/dax.h>
#include <linux/psi.h>
#include <linux/swapops.h>
#include <linux/cpufreq.h>
#include <linux/proc_fs.h>

#if defined(OPLUS_FEATURE_PROCESS_RECLAIM) && defined(CONFIG_PROCESS_RECLAIM_ENHANCE)
#include <linux/process_mm_reclaim.h>
#endif

#include "internal.h"

#define KSHRINK_LRUVECD_HIGH (0x1000)  //16Mbytes

#define PG_nolockdelay (__NR_PAGEFLAGS + 2)
#define SetPageNoLockDelay(page) set_bit(PG_nolockdelay, &(page)->flags)
#define TestPageNoLockDelay(page) test_bit(PG_nolockdelay, &(page)->flags)
#define TestClearPageNoLockDelay(page) test_and_clear_bit(PG_nolockdelay, &(page)->flags)
#define ClearPageNoLockDelay(page) clear_bit(PG_nolockdelay, &(page)->flags)

#define PG_skiped_lock (__NR_PAGEFLAGS + 3)
#define SetPageSkipedLock(page) set_bit(PG_skiped_lock, &(page)->flags)
#define ClearPageSkipedLock(page) clear_bit(PG_skiped_lock, &(page)->flags)
#define PageSkipedLock(page) test_bit(PG_skiped_lock, &(page)->flags)
#define TestClearPageSkipedLock(page) test_and_clear_bit(PG_skiped_lock, &(page)->flags)

extern int unforce_reclaim_pages_from_list(struct list_head *page_list);
bool async_shrink_lruvecd_setup = false;
static struct task_struct *kshrink_lruvecd_tsk = NULL;
static int kshirnk_lruvecd_pid;
static atomic_t kshrink_lruvecd_runnable = ATOMIC_INIT(0);
unsigned long kshrink_lruvecd_pages = 0;
unsigned long kshrink_lruvecd_pages_max = 0;
unsigned long kshrink_lruvecd_handle_pages = 0;
wait_queue_head_t kshrink_lruvecd_wait;
spinlock_t l_inactive_lock;
LIST_HEAD(lru_inactive);

static is_kshrink_lruvecd(struct task_struct *tsk)
{
	return (kshirnk_lruvecd_pid == tsk->pid);
}

static void add_to_lruvecd_inactive_list(struct page *page)
{
	list_move(&page->lru, &lru_inactive);

	/* account how much pages in lru_inactive */
	kshrink_lruvecd_pages += hpage_nr_pages(page);
	if (kshrink_lruvecd_pages > kshrink_lruvecd_pages_max)
		kshrink_lruvecd_pages_max = kshrink_lruvecd_pages;
}

bool wakeup_kshrink_lruvecd(struct list_head *page_list)
{
	struct page *page, *next;
	bool kshrink_lruvecd_is_full = false;
	bool pages_should_be_reclaim = false;
	LIST_HEAD(tmp_lru_inactive);

	if (unlikely(!async_shrink_lruvecd_setup))
		return true;

	if (list_empty(page_list))
		return true;

	if (unlikely(kshrink_lruvecd_pages > KSHRINK_LRUVECD_HIGH))
		kshrink_lruvecd_is_full = true;

	list_for_each_entry_safe(page, next, page_list, lru) {
		ClearPageNoLockDelay(page);
		if (unlikely(TestClearPageSkipedLock(page))) {
			/* trylock failed and been skiped  */
			ClearPageActive(page);
			if (!kshrink_lruvecd_is_full)
				list_move(&page->lru, &tmp_lru_inactive);
		}
	}

	if (unlikely(!list_empty(&tmp_lru_inactive))) {
		spin_lock_irq(&l_inactive_lock);
		list_for_each_entry_safe(page, next, &tmp_lru_inactive, lru) {
			if (likely(!kshrink_lruvecd_is_full)) {
				pages_should_be_reclaim = true;
				add_to_lruvecd_inactive_list(page);
			}
		}
		spin_unlock_irq(&l_inactive_lock);
	}

	if (kshrink_lruvecd_is_full || !pages_should_be_reclaim)
		return true;

	if (atomic_read(&kshrink_lruvecd_runnable) == 1)
		return true;

	atomic_set(&kshrink_lruvecd_runnable, 1);
	wake_up_interruptible(&kshrink_lruvecd_wait);

	return true;
}

void set_shrink_lruvecd_cpus(void)
{
	struct cpumask mask;
	struct cpumask *cpumask = &mask;
	pg_data_t *pgdat = NODE_DATA(0);
	unsigned int cpu = 0, cpufreq_max_tmp = 0;
	struct cpufreq_policy *policy_max;
	static bool set_slabd_cpus_success = false;

	if (unlikely(!async_shrink_lruvecd_setup))
		return;

	if (likely(set_slabd_cpus_success))
		return;

	for_each_possible_cpu(cpu) {
		struct cpufreq_policy *policy = cpufreq_cpu_get(cpu);

		if (policy == NULL)
			continue;

		if (policy->cpuinfo.max_freq >= cpufreq_max_tmp) {
			cpufreq_max_tmp = policy->cpuinfo.max_freq;
			policy_max = policy;
		}
	}

	cpumask_copy(cpumask, cpumask_of_node(pgdat->node_id));
	cpumask_andnot(cpumask, cpumask, policy_max->related_cpus);

	if (!cpumask_empty(cpumask)) {
		set_cpus_allowed_ptr(kshrink_lruvecd_tsk, cpumask);
		set_slabd_cpus_success = true;
	}
}

static int kshrink_lruvecd(void *p)
{
	pg_data_t *pgdat;
	LIST_HEAD(tmp_lru_inactive);
	struct page *page, *next;
	struct list_head;

	/*
	 * Tell the memory management that we're a "memory allocator",
	 * and that if we need more memory we should get access to it
	 * regardless (see "__alloc_pages()"). "kswapd" should
	 * never get caught in the normal page freeing logic.
	 *
	 * (Kswapd normally doesn't need memory anyway, but sometimes
	 * you need a small amount of memory in order to be able to
	 * page out something else, and this flag essentially protects
	 * us from recursively trying to free more memory as we're
	 * trying to free the first piece of memory in the first place).
	 */
	pgdat = (pg_data_t *)p;

	current->flags |= PF_MEMALLOC | PF_SWAPWRITE | PF_KSWAPD;
	set_freezable();

	while (!kthread_should_stop()) {
		wait_event_freezable(kshrink_lruvecd_wait,
			(atomic_read(&kshrink_lruvecd_runnable) == 1));

		set_shrink_lruvecd_cpus();
retry_reclaim:

		spin_lock_irq(&l_inactive_lock);
		if (list_empty(&lru_inactive)) {
			spin_unlock_irq(&l_inactive_lock);
			atomic_set(&kshrink_lruvecd_runnable, 0);
			continue;
		}
		list_for_each_entry_safe(page, next, &lru_inactive, lru) {
			list_move(&page->lru, &tmp_lru_inactive);
			kshrink_lruvecd_pages -= hpage_nr_pages(page);
			kshrink_lruvecd_handle_pages += hpage_nr_pages(page);
		}
		spin_unlock_irq(&l_inactive_lock);

		unforce_reclaim_pages_from_list(&tmp_lru_inactive);
		goto retry_reclaim;
	}
	current->flags &= ~(PF_MEMALLOC | PF_SWAPWRITE | PF_KSWAPD);

	return 0;
}

void setpage_reclaim_trylock(struct page *page)
{
	if (unlikely(!async_shrink_lruvecd_setup))
		return;
	ClearPageSkipedLock(page);

	if (unlikely(is_kshrink_lruvecd(current))) {
		ClearPageNoLockDelay(page);
		return;
	}

	SetPageNoLockDelay(page);
}

bool page_reclaim_trylock_fail(struct page *page)
{
	ClearPageNoLockDelay(page);  /* trylock sucessfully */
	if (unlikely(!async_shrink_lruvecd_setup))
		return false;

	if (unlikely(is_kshrink_lruvecd(current))) {
		ClearPageNoLockDelay(page);
		return false;
	}

	if (PageSkipedLock(page))
		return true; /*page trylock failed and been skipped*/

	return false; /*page trylock successfully and  clear bit by should_page_skip_lock*/
}

void clearpage_reclaim_trylock(struct page *page)
{
	ClearPageNoLockDelay(page);
	ClearPageSkipedLock(page);
}

bool reclaim_page_trylock(struct page *page, struct rw_semaphore *sem,
				bool *got_lock)
{
	bool ret = false;

	if (unlikely(!async_shrink_lruvecd_setup))
		return ret;

	if (TestClearPageNoLockDelay(page)) {
		ret = true;

		if (sem == NULL)
			return ret;

		if (down_read_trylock(sem)) {   /* return 1 successful */
			*got_lock = true;

		} else {
			SetPageSkipedLock(page);  /* trylock failed and skipped */
			*got_lock = false;
		}

		return ret;
	}
	return ret;
}

static int kshrink_lruvecd_show(struct seq_file *m, void *arg)
{
	seq_printf(m,
		  "10async_shrink_lruvecd_setup:     %s\n"
		   "kshrink_lruvecd_pages:     %lu\n"
		   "kshrink_lruvecd_handle_pages:     %lu\n"
		   "kshrink_lruvecd_pages_max:     %lu\n"
		   "kshirnk_lruvecd_pid:     %d\n",
		   async_shrink_lruvecd_setup ? "enable" : "disable",
		   kshrink_lruvecd_pages,
		   kshrink_lruvecd_handle_pages,
		   kshrink_lruvecd_pages_max,
		   kshirnk_lruvecd_pid);
	seq_putc(m, '\n');

	return 0;
}

static int kshrink_lruvecd_open(struct inode *inode, struct file *file)
{
    return single_open(file, kshrink_lruvecd_show, NULL);
}

static const struct file_operations kshrink_lruvecd_operations = {
	.open		= kshrink_lruvecd_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static int async_shrink_lruvecd_init(void)
{
    	struct proc_dir_entry *pentry;
	pg_data_t *pgdat = NODE_DATA(0);
	int ret;

	init_waitqueue_head(&kshrink_lruvecd_wait);
	spin_lock_init(&l_inactive_lock);

	kshrink_lruvecd_tsk = kthread_run(kshrink_lruvecd, pgdat, "kshrink_lruvecd");
	if (IS_ERR_OR_NULL(kshrink_lruvecd_tsk)) {
		pr_err("Failed to start kshrink_lruvecd on node 0\n");
		ret = PTR_ERR(kshrink_lruvecd_tsk);
		kshrink_lruvecd_tsk = NULL;
		return ret;
	}

	kshirnk_lruvecd_pid = kshrink_lruvecd_tsk->pid;
	pentry = proc_create("kshrink_lruvecd_status", 0400, NULL, &kshrink_lruvecd_operations);
	async_shrink_lruvecd_setup = true;
	return 0;
}
module_init(async_shrink_lruvecd_init);
