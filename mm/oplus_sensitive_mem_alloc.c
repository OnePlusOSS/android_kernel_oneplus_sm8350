#include <asm/page.h>
#include <linux/err.h>
#include <linux/mm.h>
#include <linux/seq_file.h>
#include <linux/sizes.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/spinlock.h>
#include <linux/gfp.h>
#include <linux/mm_types.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include "oplus_sensitive_mem_alloc.h"

#define OM_VALUE_LEN (32)
#define ALLOC_GFP (__GFP_RETRY_MAYFAIL | __GFP_NOWARN | __GFP_NOMEMALLOC | GFP_KERNEL)
#define P_ORDER (3)
static const unsigned int orders[] = {P_ORDER};
#define NUM_ORDERS ARRAY_SIZE(orders)

static const unsigned int pool_max_count = ((SZ_16M >> PAGE_SHIFT) >> P_ORDER);
static struct page_pool *pools[NUM_ORDERS];
int oplus_page_pool_enabled = 1;
atomic_long_t oplus_sensitive_mem_allocated = ATOMIC_LONG_INIT(0);

static int page_pool_fill(struct page_pool *pool,int migratetype);

static int order_to_index(unsigned int order)
{
	int i;
	for (i = 0; i < NUM_ORDERS; i++) {
		if (order == orders[i])
			return i;
	}
	return -1;
}

struct page_pool *oplus_page_pool_create(gfp_t gfp_mask, unsigned int order)
{
	struct page_pool *pool;
	int i;

	pool = kmalloc(sizeof(*pool), GFP_KERNEL);
	if (!pool)
		return ERR_PTR(-ENOMEM);

	pool->gfp_mask = gfp_mask;
	pool->order = order;
	for (i = 0; i < POOL_MIGRATETYPE_TYPES_SIZE; i++) {
		pool->count[i] = 0;
		INIT_LIST_HEAD(&pool->items[i]);

		pr_info("%s order:%d migratetype:%d count:%d.\n",
			__func__, pool->order, i, pool->count[i]);
	}

	spin_lock_init(&pool->lock);
	return pool;
}

static void page_pool_add(struct page_pool *pool, struct page *page, int migratetype)
{
	unsigned long flags;
	spin_lock_irqsave(&pool->lock, flags);
	list_add_tail(&page->lru, &pool->items[migratetype]);
	pool->count[migratetype]++;
	spin_unlock_irqrestore(&pool->lock, flags);
}

static struct page *page_pool_remove(struct page_pool *pool, int migratetype)
{
	unsigned long flags;
	struct page *page = NULL;

	spin_lock_irqsave(&pool->lock, flags);
	page = list_first_entry_or_null(&pool->items[migratetype], struct page, lru);
	if (page) {
		pool->count[migratetype]--;
		list_del(&page->lru);
	}
	spin_unlock_irqrestore(&pool->lock, flags);

	return page;
}

static int page_pool_fill(struct page_pool *pool, int migratetype)
{
	struct page *page;
	gfp_t gfp_refill = pool->gfp_mask;

	if (NULL == pool) {
		pr_err("%s: pool is NULL!\n", __func__);
		return -EINVAL;
	}

	page = alloc_pages(gfp_refill, pool->order);
	if (NULL == page)
		return -ENOMEM;

	page_pool_add(pool, page, migratetype);
	return true;
}

struct page *oplus_page_pool_alloc_pages(unsigned int order, int migratetype)
{
	struct page *page = NULL;
	struct page_pool *pool = NULL;
	int order_ind = order_to_index(order);

	if (unlikely(!oplus_page_pool_enabled) || ( order_ind == -1) || (migratetype >= POOL_MIGRATETYPE_TYPES_SIZE))
		return NULL;

	pool = pools[order_ind];
	if (pool == NULL)
		return NULL;

	page = page_pool_remove(pool, migratetype);
	if (!page) {
		return NULL;
	}
	atomic_long_add(1<<order, &oplus_sensitive_mem_allocated);
	return page;
}

bool oplus_page_pool_refill(struct page *page, unsigned int order, int migratetype)
{
	struct page_pool *pool;
	struct zone *zone = page_zone(page);
	unsigned long mark = zone->_watermark[WMARK_LOW];
	long free_pages = zone_page_state(zone, NR_FREE_PAGES);
	int order_ind = order_to_index(order);
	free_pages -= zone->nr_reserved_highatomic;

	if (free_pages < mark)
		return false;

	if (unlikely(!oplus_page_pool_enabled) || ( order_ind == -1) || (migratetype >= POOL_MIGRATETYPE_TYPES_SIZE))
		return false;

	pool = pools[order_ind];
	if (pool == NULL)
		return false;

	if (pool->count[migratetype] >= pool_max_count)
		return false;

	page_pool_add(pool, page, migratetype);
	return true;
}

int oplus_sensitive_mem_pages(unsigned int order, int migratetype)
{
	struct page_pool *pool;
	int order_ind = order_to_index(order);
	if (unlikely(!oplus_page_pool_enabled) || ( order_ind == -1) || (migratetype >= POOL_MIGRATETYPE_TYPES_SIZE))
		return 0;

	pool = pools[order_ind];
	if (pool == NULL)
		return 0;
	return pool->count[migratetype] << order;
}

static ssize_t oplus_sensitive_mem_enable_read(struct file *file,
		char __user *buffer, size_t count, loff_t *off)
{
	char kbuf[OM_VALUE_LEN] = {'0'};
	int len;

	len = scnprintf(kbuf, OM_VALUE_LEN - 1, "%d\n", oplus_page_pool_enabled);
	if (kbuf[len - 1] != '\n')
		kbuf[len++] = '\n';

	if (len > *off)
		len -= *off;
	else
		len = 0;

	if (copy_to_user(buffer, kbuf + *off, (len < count ? len : count)))
		return -EFAULT;

	*off += (len < count ? len : count);
	return (len < count ? len : count);
}

static ssize_t oplus_sensitive_mem_enable_write(struct file *file,
		const char __user *buff, size_t len, loff_t *ppos)
{
	char kbuf[OM_VALUE_LEN] = {'0'};
	long val;
	int ret;

	if (len > (OM_VALUE_LEN - 1))
		len = OM_VALUE_LEN - 1;

	if (copy_from_user(&kbuf, buff, len))
		return -EFAULT;
	kbuf[len] = '\0';

	ret = kstrtol(kbuf, 10, &val);
	if (ret)
		return -EINVAL;

	oplus_page_pool_enabled = val ? 1 : 0;
	return len;
}


static const struct file_operations oplus_sensitive_mem_fops = {
		.read	= oplus_sensitive_mem_enable_read,
		.write	= oplus_sensitive_mem_enable_write,
};


static int oplus_page_pool_init(void)
{
	int i,j;
	struct page_pool *pool;
	struct proc_dir_entry *oplus_pool_dir;
	if (unlikely(!oplus_page_pool_enabled))
		return -1;

	for (i = 0; i < NUM_ORDERS; i++)
		pools[i] = oplus_page_pool_create(ALLOC_GFP , orders[i]);

	for (i = 0; i < NUM_ORDERS; i++) {
		pool = pools[i];
		for (j = 0; j < POOL_MIGRATETYPE_TYPES_SIZE; j++) {
			while (pool->count[j] < pool_max_count)
				page_pool_fill(pool, j);
		}
	}
	oplus_pool_dir = proc_mkdir("oplus_pooldebug", NULL);
	if (oplus_pool_dir)
		proc_create("oplus_page_pool", 0666, oplus_pool_dir, &oplus_sensitive_mem_fops);
	else {
		pr_err("%s create oplus_pooldebug dir fail.\n", __func__);
		return -1;
	}
	return 0;
}

module_init(oplus_page_pool_init);
