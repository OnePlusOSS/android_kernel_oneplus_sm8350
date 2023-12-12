#ifndef _OPLUS_SENSITIVE_MEM_ALLOC_H
#define _OPLUS_SENSITIVE_MEM_ALLOC_H

#include <linux/mm_types.h>
#include <linux/mutex.h>
#include <linux/types.h>

enum POOL_MIGRATETYPE{
	POOL_MIGRATETYPE_UNMOVABLE,
	POOL_MIGRATETYPE_TYPES_SIZE
};

struct page_pool {
	int count[POOL_MIGRATETYPE_TYPES_SIZE];
    struct list_head items[POOL_MIGRATETYPE_TYPES_SIZE];
    spinlock_t lock;
    unsigned int order;
    gfp_t gfp_mask;
};

struct page *oplus_page_pool_alloc_pages(unsigned int order, int migratetype);
bool oplus_page_pool_refill(struct page *page, unsigned int order, int migratetype);
#endif /* _OPLUS_SENSITIVE_MEM_ALLOC_H */
