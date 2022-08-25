// SPDX-License-Identifier: GPL-2.0
/*
 * ION Memory Allocator - buffer interface
 *
 * Copyright (c) 2019, Google, Inc.
 */

#include <linux/mm.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/dma-noncoherent.h>
#include <linux/msm_ion.h>

#define CREATE_TRACE_POINTS
#include "ion_trace.h"
#include "ion_private.h"

#ifdef OPLUS_FEATURE_HEALTHINFO
#if defined(CONFIG_OPLUS_HEALTHINFO) && defined (CONFIG_OPLUS_MEM_MONITOR)
#include <linux/healthinfo/memory_monitor.h>
#endif
#endif /* OPLUS_FEATURE_HEALTHINFO */

#ifdef OPLUS_FEATURE_HEALTHINFO
#ifdef CONFIG_OPLUS_HEALTHINFO
#include <linux/healthinfo/ion.h>
#endif
#endif /* OPLUS_FEATURE_HEALTHINFO */
static atomic_long_t total_heap_bytes;

static void track_buffer_created(struct ion_buffer *buffer)
{
	long total = atomic_long_add_return(buffer->size, &total_heap_bytes);

	trace_ion_stat(buffer->sg_table, buffer->size, total);
}

static void track_buffer_destroyed(struct ion_buffer *buffer)
{
	long total = atomic_long_sub_return(buffer->size, &total_heap_bytes);

	trace_ion_stat(buffer->sg_table, -buffer->size, total);
}


/* this function should only be called while dev->lock is held */
static struct ion_buffer *ion_buffer_create(struct ion_heap *heap,
					    struct ion_device *dev,
					    unsigned long len,
					    unsigned long flags)
{
	struct ion_buffer *buffer;
	int ret;

	buffer = kzalloc(sizeof(*buffer), GFP_KERNEL);
	if (!buffer)
		return ERR_PTR(-ENOMEM);

	buffer->heap = heap;
	buffer->flags = flags;
	buffer->size = len;

	ret = heap->ops->allocate(heap, buffer, len, flags);

	if (ret) {
		if (!(heap->flags & ION_HEAP_FLAG_DEFER_FREE))
			goto err2;

		ion_heap_freelist_drain(heap, 0);
		ret = heap->ops->allocate(heap, buffer, len, flags);
		if (ret)
			goto err2;
	}

	if (!buffer->sg_table) {
		WARN_ONCE(1, "This heap needs to set the sgtable");
		ret = -EINVAL;
		goto err1;
	}

	spin_lock(&heap->stat_lock);
	heap->num_of_buffers++;
	heap->num_of_alloc_bytes += len;
	if (heap->num_of_alloc_bytes > heap->alloc_bytes_wm)
		heap->alloc_bytes_wm = heap->num_of_alloc_bytes;
	if (heap->num_of_buffers == 1) {
		/* This module reference lasts as long as at least one
		 * buffer is allocated from the heap. We are protected
		 * against ion_device_remove_heap() with dev->lock, so we can
		 * safely assume the module reference is going to* succeed.
		 */
		__module_get(heap->owner);
	}
	spin_unlock(&heap->stat_lock);

	INIT_LIST_HEAD(&buffer->attachments);
	mutex_init(&buffer->lock);
	track_buffer_created(buffer);
#ifdef OPLUS_FEATURE_HEALTHINFO
#ifdef CONFIG_OPLUS_HEALTHINFO
	if (ion_cnt_enable)
		atomic_long_add(buffer->size, &ion_total_size);
#endif
#endif /* OPLUS_FEATURE_HEALTHINFO */
	return buffer;

err1:
	heap->ops->free(buffer);
err2:
	kfree(buffer);
	return ERR_PTR(ret);
}

static int ion_clear_pages(struct page **pages, int num, pgprot_t pgprot)
{
	void *addr = vmap(pages, num, VM_MAP, pgprot);

	if (!addr)
		return -ENOMEM;
	memset(addr, 0, PAGE_SIZE * num);
	vunmap(addr);

	return 0;
}

static int ion_sglist_zero(struct scatterlist *sgl, unsigned int nents,
			   pgprot_t pgprot)
{
	int p = 0;
	int ret = 0;
	struct sg_page_iter piter;
	struct page *pages[32];

	for_each_sg_page(sgl, &piter, nents, 0) {
		pages[p++] = sg_page_iter_page(&piter);
		if (p == ARRAY_SIZE(pages)) {
			ret = ion_clear_pages(pages, p, pgprot);
			if (ret)
				return ret;
			p = 0;
		}
	}
	if (p)
		ret = ion_clear_pages(pages, p, pgprot);

	return ret;
}

#ifdef CONFIG_OPLUS_ION_BOOSTPOOL
static inline unsigned int boost_pool_extra_flags(unsigned int heap_id_mask)
{
	unsigned int extra_flags = 0;

	if (heap_id_mask & ION_CAMERA_HEAP_ID)
		extra_flags |= ION_FLAG_CAMERA_BUFFER;

	return extra_flags;
}
#endif /* CONFIG_OPLUS_ION_BOOSTPOOL */

struct ion_buffer *ion_buffer_alloc(struct ion_device *dev, size_t len,
				    unsigned int heap_id_mask,
				    unsigned int flags)
{
	struct ion_buffer *buffer = NULL;
	struct ion_heap *heap;
	char task_comm[TASK_COMM_LEN];
#ifdef OPLUS_FEATURE_HEALTHINFO
#if defined(CONFIG_OPLUS_HEALTHINFO) && defined (CONFIG_OPLUS_MEM_MONITOR)
	unsigned long ionwait_start = jiffies;
#endif
#endif /* OPLUS_FEATURE_HEALTHINFO */

#ifdef CONFIG_OPLUS_ION_BOOSTPOOL
	unsigned int extra_flags = boost_pool_extra_flags(heap_id_mask);
#endif /* CONFIG_OPLUS_ION_BOOSTPOOL */

	if (!dev || !len) {
		return ERR_PTR(-EINVAL);
	}

	if (heap_id_mask & ION_HEAP_SYSTEM) {
		get_task_comm(task_comm, current->group_leader);
		pr_warn_ratelimited("%s: Detected allocation from generic sys heap for task %s-%d\n",
				    __func__, task_comm, current->tgid);
	}

	/*
	 * traverse the list of heaps available in this system in priority
	 * order.  If the heap type is supported by the client, and matches the
	 * request of the caller allocate from it.  Repeat until allocate has
	 * succeeded or all heaps have been tried
	 */
	len = PAGE_ALIGN(len);
	if (!len)
		return ERR_PTR(-EINVAL);

	down_read(&dev->lock);
	plist_for_each_entry(heap, &dev->heaps, node) {
		/* if the caller didn't specify this heap id */
		if (!((1 << heap->id) & heap_id_mask))
			continue;
#ifdef CONFIG_OPLUS_ION_BOOSTPOOL
		if ((1 << heap->id) == ION_SYSTEM_HEAP_ID)
			buffer = ion_buffer_create(heap, dev, len, flags | extra_flags);
		else
			buffer = ion_buffer_create(heap, dev, len, flags);
#else
		buffer = ion_buffer_create(heap, dev, len, flags);
#endif
		if (!IS_ERR(buffer))
			break;
	}
	up_read(&dev->lock);

	if (!buffer)
		return ERR_PTR(-ENODEV);

	if (IS_ERR(buffer))
		return ERR_CAST(buffer);
#ifdef OPLUS_FEATURE_HEALTHINFO
#if defined(CONFIG_OPLUS_HEALTHINFO) && defined (CONFIG_OPLUS_MEM_MONITOR)
	ionwait_monitor(jiffies_to_msecs(jiffies - ionwait_start));
#endif 
#endif /* OPLUS_FEATURE_HEALTHINFO */
	return buffer;
}

int ion_buffer_zero(struct ion_buffer *buffer)
{
	struct sg_table *table;
	pgprot_t pgprot;

	if (!buffer)
		return -EINVAL;

	table = buffer->sg_table;
	if (buffer->flags & ION_FLAG_CACHED)
		pgprot = PAGE_KERNEL;
	else
		pgprot = pgprot_writecombine(PAGE_KERNEL);

	return ion_sglist_zero(table->sgl, table->nents, pgprot);
}
EXPORT_SYMBOL_GPL(ion_buffer_zero);

void ion_buffer_prep_noncached(struct ion_buffer *buffer)
{
	struct scatterlist *sg;
	struct sg_table *table;
	int i;

	if (WARN_ONCE(!buffer || !buffer->sg_table,
		      "%s needs a buffer and a sg_table", __func__) ||
	    buffer->flags & ION_FLAG_CACHED)
		return;

	table = buffer->sg_table;

	for_each_sg(table->sgl, sg, table->orig_nents, i)
		arch_dma_prep_coherent(sg_page(sg), sg->length);
}
EXPORT_SYMBOL_GPL(ion_buffer_prep_noncached);

void ion_buffer_release(struct ion_buffer *buffer)
{
	if (buffer->kmap_cnt > 0) {
		pr_warn_once("%s: buffer still mapped in the kernel\n",
			     __func__);
		ion_heap_unmap_kernel(buffer->heap, buffer);
	}
	buffer->heap->ops->free(buffer);
	spin_lock(&buffer->heap->stat_lock);
	buffer->heap->num_of_buffers--;
	buffer->heap->num_of_alloc_bytes -= buffer->size;
	if (buffer->heap->num_of_buffers == 0)
		module_put(buffer->heap->owner);
	spin_unlock(&buffer->heap->stat_lock);
	/* drop reference to the heap module */

	kfree(buffer);
}

int ion_buffer_destroy(struct ion_device *dev, struct ion_buffer *buffer)
{
	struct ion_heap *heap;

	if (!dev || !buffer) {
		pr_warn("%s: invalid argument\n", __func__);
		return -EINVAL;
	}
#ifdef OPLUS_FEATURE_HEALTHINFO
#ifdef CONFIG_OPLUS_HEALTHINFO
	if (ion_cnt_enable)
		atomic_long_sub(buffer->size, &ion_total_size);
#endif
#endif /* OPLUS_FEATURE_HEALTHINFO */
	heap = buffer->heap;
	track_buffer_destroyed(buffer);

	if (heap->flags & ION_HEAP_FLAG_DEFER_FREE)
		ion_heap_freelist_add(heap, buffer);
	else
		ion_buffer_release(buffer);

	return 0;
}

void *ion_buffer_kmap_get(struct ion_buffer *buffer)
{
	void *vaddr;

	if (buffer->kmap_cnt) {
		buffer->kmap_cnt++;
		return buffer->vaddr;
	}
	vaddr = ion_heap_map_kernel(buffer->heap, buffer);
	if (WARN_ONCE(!vaddr,
		      "heap->ops->map_kernel should return ERR_PTR on error"))
		return ERR_PTR(-EINVAL);
	if (IS_ERR(vaddr))
		return vaddr;
	buffer->vaddr = vaddr;
	buffer->kmap_cnt++;
	return vaddr;
}

void ion_buffer_kmap_put(struct ion_buffer *buffer)
{
	buffer->kmap_cnt--;
	if (!buffer->kmap_cnt) {
		ion_heap_unmap_kernel(buffer->heap, buffer);
		buffer->vaddr = NULL;
	}
}

u64 ion_get_total_heap_bytes(void)
{
	return atomic_long_read(&total_heap_bytes);
}
