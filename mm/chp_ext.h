// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2023 Oplus. All rights reserved.
 */
#ifndef _CHP_EXT_H_
#define _CHP_EXT_H_

#ifdef CONFIG_CONT_PTE_HUGEPAGE
#include <linux/mm.h>
struct chp_ext_order {
	union {
		struct {
			unsigned int order : 8;
			unsigned int magic : 16;
			unsigned int type : 8;
		};
		unsigned int nr;
	};
};

enum chp_ext_type {
	CHP_EXT_ZSMALLOC = 0,
	CHP_EXT_GPU,
	CHP_EXT_DMABUF,

	NR_CHP_EXT_TYPES,
};

enum chp_ext_cmd {
	CHP_EXT_CMD_KERNEL_SUPPORT_CHP = 0,
	CHP_EXT_CMD_CHP_POOL,
};

static inline struct page *alloc_chp_ext_wrapper(gfp_t gfp_mask, int type)
{
	struct chp_ext_order ceo = {
		.order = HPAGE_CONT_PTE_ORDER,
		.magic = THP_SWAP_PRIO_MAGIC,
		.type = type,
	};

	return alloc_pages(gfp_mask, ceo.nr);
}

static inline void __free_pages_ext(struct page *page, unsigned int order)
{
	if (unlikely(order == HPAGE_CONT_PTE_ORDER && PageContExtAlloc(page)))
		put_page(page);
	else
		__free_pages(page, order);
}

static inline bool is_chp_ext_pages(struct page *page, unsigned int order)
{
	return (order == HPAGE_CONT_PTE_ORDER && PageContExtAlloc(page));
}

static inline bool chp_enabled_ext(void)
{
	struct sysinfo si = {
		.procs		= THP_SWAP_PRIO_MAGIC,
		.totalhigh	= CHP_EXT_CMD_KERNEL_SUPPORT_CHP,
	};

	si_meminfo(&si);
	return si.freehigh;
}

static inline struct huge_page_pool *chp_pool_ext(void)
{
	struct sysinfo si = {
		.procs		= THP_SWAP_PRIO_MAGIC,
		.totalhigh	= CHP_EXT_CMD_CHP_POOL,
	};

	si_meminfo(&si);
	return (struct huge_page_pool *)si.freehigh;
}

static inline int chp_pool_pages_ext(struct huge_page_pool *pool)
{
	int pages;

	pages = (pool->count[HPAGE_POOL_CMA] +
		 pool->count[HPAGE_POOL_BUDDY]) * HPAGE_CONT_PTE_NR;
	pages -= min(pages / 2, pool->high * HPAGE_CONT_PTE_NR);
	return pages;
}
#endif /* CONFIG_CONT_PTE_HUGEPAGE */
#endif /* _CHP_EXT_H_ */
