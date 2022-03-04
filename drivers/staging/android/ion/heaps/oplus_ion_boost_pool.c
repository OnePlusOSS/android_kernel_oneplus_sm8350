
#define pr_fmt(fmt) "boostpool: " fmt

#include <asm/page.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/highmem.h>
#include <linux/mm.h>
#include <linux/scatterlist.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/sizes.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/proc_fs.h>
#include <linux/vmstat.h>
#include <linux/oom.h>
#include <linux/poll.h>
#include <linux/mm.h>
#include <linux/ion.h>
#include <linux/sched.h>
#include <uapi/linux/sched/types.h>
#include <../../../../../kernel/sched/sched.h>

#include <linux/msm_ion.h>
#include "ion_msm_page_pool.h"
#include "ion_msm_system_heap.h"

#include "oplus_ion_boost_pool.h"

#define MAX_BOOST_POOL_HIGH (1024 * 256)
#define K(x) ((x) << (PAGE_SHIFT - 10))
#define M(x) (K(x) >> 10)

static bool boost_pool_enable = true;
static void kcrit_scene_wakeup_lmkd(void);

static inline unsigned int order_to_size(int order)
{
	return PAGE_SIZE << order;
}

static int page_pool_nr_pages(struct ion_msm_page_pool *pool)
{
	if (!pool) {
		pr_err("%s: pool is NULL!\n", __func__);
		return 0;
	}
	return (pool->low_count + pool->high_count) << pool->order;
}

static int boost_pool_nr_pages(struct ion_boost_pool *pool)
{
	int i;
	int count = 0;

	if (!pool) {
		pr_err("%s: pool is NULL!\n", __func__);
		return 0;
	}

	for (i = 0; i < NUM_ORDERS; i++)
		count += page_pool_nr_pages(pool->pools[i]);

	return count;
}

static int fill_boost_page_pool(struct ion_msm_page_pool *pool)
{
	struct page *page;
	struct device *dev = pool->heap_dev;

	if (!pool) {
		pr_err("%s: pool is NULL!\n", __func__);
		return -ENOENT;
	}
	page = ion_msm_page_pool_alloc_pages(pool);
	if (!page)
		return -ENOMEM;

	ion_pages_sync_for_device(dev, page,
				  PAGE_SIZE << pool->order,
				  DMA_BIDIRECTIONAL);

	ion_msm_page_pool_free(pool, page);

	return 0;
}

void boost_pool_dump(struct ion_boost_pool *pool)
{
	int i;

	pr_info("Name:%s: %dMib, low: %dMib, high:%dMib\n",
		pool->name,
		M(boost_pool_nr_pages(pool)),
		M(pool->low),
		M(pool->high));
	for (i = 0; i < NUM_ORDERS; i++) {
		struct ion_msm_page_pool *page_pool = pool->pools[i];

		pr_info("%d order %u highmem pages in boost pool = %lu total\n",
			page_pool->high_count, page_pool->order,
			(PAGE_SIZE << page_pool->order) * page_pool->high_count);
		pr_info("%d order %u lowmem pages in boost pool = %lu total\n",
			page_pool->low_count, page_pool->order,
			(PAGE_SIZE << page_pool->order) * page_pool->low_count);
	}
}

static int boost_pool_kworkthread(void *p)
{
	int i;
	struct ion_boost_pool *pool;
	int ret;

	if (!p) {
		pr_err("%s: p is NULL!\n", __func__);
		return 0;
	}

	pool = (struct ion_boost_pool *)p;
	while (true) {
		ret = wait_event_interruptible(pool->waitq,
					       (pool->wait_flag == 1));
		if (ret < 0)
			continue;

		pool->wait_flag = 0;
		for (i = 0; i < NUM_ORDERS; i++) {
			while (boost_pool_nr_pages(pool) < pool->high) {
				if (fill_boost_page_pool(pool->pools[i]) < 0)
					break;
			}
		}

		for (i = 1; i < NUM_ORDERS; i++) {
			while (page_pool_nr_pages(pool->pools[i]) <
			       LOWORDER_WATER_MASK) {
				if (fill_boost_page_pool(pool->pools[i]) < 0)
					break;
			}
		}
	}

	return 0;
}

struct page_info *boost_pool_allocate(struct ion_boost_pool *pool,
				      unsigned long size,
				      unsigned int max_order)
{
	int i;
	struct page *page;
	struct page_info *info;
	bool from_pool = true;

	if (!pool) {
		pr_err("%s: pool is NULL!\n", __func__);
		return NULL;
	}

	info = kmalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return NULL;
	for (i = 0; i < NUM_ORDERS; i++) {
		if (size < order_to_size(orders[i]))
			continue;
		if (max_order < orders[i])
			continue;

		page = ion_msm_page_pool_alloc(pool->pools[i], &from_pool);
		if (IS_ERR(page))
			continue;

		info->page = page;
		info->order = orders[i];

		info->from_pool = true;
		INIT_LIST_HEAD(&info->list);
		return info;
	}
	kfree(info);

	return NULL;
}

void boost_pool_wakeup_process(struct ion_boost_pool *pool)
{
	if (!boost_pool_enable)
		return;

	if (!pool) {
		pr_err("%s: boost_pool is NULL!\n", __func__);
		return;
	}

	if (!pool->force_stop) {
		pool->wait_flag = 1;
		wake_up_interruptible(&pool->waitq);
	}
}

static void boost_pool_all_free(struct ion_boost_pool *pool, gfp_t gfp_mask,
				int nr_to_scan)
{
	int i;

	if (!pool) {
		pr_err("%s: boost_pool is NULL!\n", __func__);
		return;
	}

	for (i = 0; i < NUM_ORDERS; i++)
		ion_msm_page_pool_shrink(pool->pools[i], gfp_mask, nr_to_scan);
}

int boost_pool_free(struct ion_boost_pool *pool, struct page *page,
		    int order)
{
	if (!boost_pool_enable) {
		boost_pool_all_free(pool, __GFP_HIGHMEM, MAX_POOL_SIZE);
		return -EPERM;
	}

	if (order == 0)
		return -EPERM;

	if ((!pool) || (!page)) {
		pr_err("%s: pool/page is NULL!\n", __func__);
		return -EPERM;
	}

	if (!((order == orders[0]) || (order == orders[1]) || (order == orders[2]))) {
		pr_err("%s: order:%d is error!\n", __func__, order);
		return -EPERM;
	}

	if (boost_pool_nr_pages(pool) < pool->low + (SZ_128M >> PAGE_SHIFT)) {
		ion_msm_page_pool_free(pool->pools[order_to_index(order)], page);
		return 0;
	}

	return -EPERM;
}

int boost_pool_shrink(struct ion_boost_pool *boost_pool,
		      struct ion_msm_page_pool *pool, gfp_t gfp_mask,
		      int nr_to_scan)
{
	int nr_max_free;
	int nr_to_free;
	int nr_total = 0;

	if ((!boost_pool) || (!pool)) {
		pr_err("%s: boostpool/pool is NULL!\n", __func__);
		return 0;
	}

	if (boost_pool->tsk->pid == current->pid)
		return 0;

	if (nr_to_scan == 0)
		return ion_msm_page_pool_shrink(pool, gfp_mask, 0);

	nr_max_free = boost_pool_nr_pages(boost_pool) -
		(boost_pool->high + LOWORDER_WATER_MASK);
	nr_to_free = min(nr_max_free, nr_to_scan);

	if (nr_to_free <= 0)
		return 0;

	nr_total = ion_msm_page_pool_shrink(pool, gfp_mask, nr_to_free);
	return nr_total;
}

void boost_pool_dec_high(struct ion_boost_pool *pool, int nr_pages)
{
	if (unlikely(nr_pages < 0))
		return;

	pool->high = max(pool->low,
			 pool->high - nr_pages);
}

static int boost_pool_proc_show(struct seq_file *s, void *v)
{
	struct ion_boost_pool *boost_pool = s->private;
	int i;

	seq_printf(s, "Name:%s: %dMib, low: %dMib high: %dMib\n",
		   boost_pool->name,
		   M(boost_pool_nr_pages(boost_pool)),
		   M(boost_pool->low),
		   M(boost_pool->high));

	for (i = 0; i < NUM_ORDERS; i++) {
		struct ion_msm_page_pool *pool = boost_pool->pools[i];

		seq_printf(s, "%d order %u highmem pages in boost pool = %lu total\n",
			   pool->high_count, pool->order,
			   (PAGE_SIZE << pool->order) * pool->high_count);
		seq_printf(s, "%d order %u lowmem pages in boost pool = %lu total\n",
			   pool->low_count, pool->order,
			   (PAGE_SIZE << pool->order) * pool->low_count);
	}
	return 0;
}

static int boost_pool_proc_open(struct inode *inode, struct file *file)
{
	struct ion_boost_pool *data = PDE_DATA(inode);

	return single_open(file, boost_pool_proc_show, data);
}

static ssize_t boost_pool_proc_write(struct file *file,
				     const char __user *buf,
				     size_t count, loff_t *ppos)
{
	char buffer[13];
	int err, nr_pages;
	struct ion_boost_pool *boost_pool = PDE_DATA(file_inode(file));

	if (IS_ERR(boost_pool)) {
		pr_err("%s: boost pool is NULL.\n");
		return -EFAULT;
	}

	memset(buffer, 0, sizeof(buffer));
	if (count > sizeof(buffer) - 1)
		count = sizeof(buffer) - 1;
	if (copy_from_user(buffer, buf, count))
		return -EFAULT;

	err = kstrtoint(strstrip(buffer), 0, &nr_pages);
	if (err)
		return err;

	if (nr_pages == 0) {
		pr_info("reset high wm.\n");
		boost_pool->high = boost_pool->low;
		boost_pool->force_stop = 0;
		return count;
	}

	if (nr_pages == -1) {
		pr_info("force stop.\n");
		boost_pool->force_stop = 1;
		return count;
	}

	if (nr_pages < 0 || nr_pages >= MAX_BOOST_POOL_HIGH ||
	    nr_pages <= boost_pool->low)
		return -EINVAL;

	if (boost_pool->usage & ION_FLAG_CAMERA_BUFFER) {
		long mem_avail = si_mem_available();

		pr_info("%s: set high wm => %dMib. current avail => %dMib\n",
			current->comm, M(nr_pages), M(mem_avail));
		kcrit_scene_wakeup_lmkd();
		/* trigger lmkiller */
		boost_pool->force_stop = 0;
		boost_pool->high = nr_pages;
		boost_pool_wakeup_process(boost_pool);
	}

	return count;
}

static const struct file_operations boost_pool_proc_ops = {
	.owner          = THIS_MODULE,
	.open           = boost_pool_proc_open,
	.read           = seq_read,
	.write          = boost_pool_proc_write,
	.llseek         = seq_lseek,
	.release        = single_release,
};

struct ion_boost_pool *boost_pool_create(struct ion_msm_system_heap *heap,
					 unsigned int ion_flag,
					 unsigned int nr_pages,
					 struct proc_dir_entry *root_dir,
					 char *name)
{
	bool boost_flag = true;
	struct task_struct *tsk;
	struct ion_boost_pool *boost_pool;

	if (!root_dir) {
		pr_err("boost_pool dir not exits.\n");
		return NULL;
	}

	boost_pool = kzalloc(sizeof(struct ion_boost_pool) +
			     sizeof(struct ion_msm_page_pool *) * NUM_ORDERS,
			     GFP_KERNEL);

	if (!boost_pool) {
		pr_err("%s: boost_pool is NULL!\n", __func__);
		return NULL;
	}

	if (ion_msm_system_heap_create_pools(heap, boost_pool->pools,
					     false, boost_flag))
		goto free_heap;

	boost_pool->high = nr_pages;
	boost_pool->low = nr_pages;
	boost_pool->name = name;
	boost_pool->usage = ion_flag;

	boost_pool->proc_info = proc_create_data(name, 0666,
						 root_dir,
						 &boost_pool_proc_ops,
						 boost_pool);
	if (IS_ERR(boost_pool->proc_info)) {
		pr_info("Unable to initialise /proc/%s\n",
			name);
		goto destroy_pools;
	} else {
		pr_info("procfs entry /proc/%s allocated. data=%p\n",
			name, boost_pool);
	}

	init_waitqueue_head(&boost_pool->waitq);
	tsk = kthread_create(boost_pool_kworkthread,
						boost_pool,
						"bp_%s", name);
	if (IS_ERR(tsk)) {
		pr_err("%s: kthread_create failed!\n", __func__);
		goto destroy_proc_info;
	}
	boost_pool->tsk = tsk;
	/* set_task_affinity(tsk,end_cpu); */
	wake_up_process(tsk);
	/* pr_info("bind %s on cpu[0-%d].\n", tsk->comm, end_cpu - 1); */
	boost_pool_wakeup_process(boost_pool);

	return boost_pool;
destroy_proc_info:
	proc_remove(boost_pool->proc_info);
destroy_pools:
	ion_msm_system_heap_destroy_pools(boost_pool->pools);
free_heap:
	kfree(boost_pool);
	boost_pool = NULL;
	return NULL;
}

static wait_queue_head_t kcrit_scene_wait;
static int kcrit_scene_flag; //static int kcrit_scene_flag = 0;
static unsigned int kcrit_scene_proc_poll(struct file *file, poll_table *table)
{
	int mask = 0;

	poll_wait(file, &kcrit_scene_wait, table);

	if (kcrit_scene_flag == 1) {
		mask |= POLLIN | POLLRDNORM;
		kcrit_scene_flag = 0;
	}
	return mask;
}

static int kcrit_scene_proc_open(struct inode *inode, struct file *file)
{
	return nonseekable_open(inode, file);
}

static int kcrit_scene_proc_release(struct inode *inode, struct file *file)
{
	return 0;
}

/* TODO add read ops. */
static const struct file_operations kcrit_scene_proc_fops = {
	.owner = THIS_MODULE,
	.open = kcrit_scene_proc_open,
	.release = kcrit_scene_proc_release,
	.poll = kcrit_scene_proc_poll,
};

bool kcrit_scene_init(void)
{
	init_waitqueue_head(&kcrit_scene_wait);
	return proc_create("kcritical_scene", 0444, NULL,
			   &kcrit_scene_proc_fops) != NULL;
}

static void kcrit_scene_wakeup_lmkd(void)
{
	kcrit_scene_flag = 1;
	wake_up_interruptible(&kcrit_scene_wait);
}
module_param_named(debug_boost_pool_enable, boost_pool_enable, bool, 0644);
