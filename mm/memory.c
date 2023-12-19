// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/mm/memory.c
 *
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 */

/*
 * demand-loading started 01.12.91 - seems it is high on the list of
 * things wanted, and it should be easy to implement. - Linus
 */

/*
 * Ok, demand-loading was easy, shared pages a little bit tricker. Shared
 * pages started 02.12.91, seems to work. - Linus.
 *
 * Tested sharing by executing about 30 /bin/sh: under the old kernel it
 * would have taken more than the 6M I have free, but it worked well as
 * far as I could see.
 *
 * Also corrected some "invalidate()"s - I wasn't doing enough of them.
 */

/*
 * Real VM (paging to/from disk) started 18.12.91. Much more work and
 * thought has to go into this. Oh, well..
 * 19.12.91  -  works, somewhat. Sometimes I get faults, don't know why.
 *		Found it. Everything seems to work now.
 * 20.12.91  -  Ok, making the swap-device changeable like the root.
 */

/*
 * 05.04.94  -  Multi-page memory management added for v1.1.
 *              Idea by Alex Bligh (alex@cconcepts.co.uk)
 *
 * 16.07.99  -  Support of BIGMEM added by Gerhard Wichert, Siemens AG
 *		(Gerhard.Wichert@pdb.siemens.de)
 *
 * Aug/Sep 2004 Changed to four level page tables (Andi Kleen)
 */

#include <linux/kernel_stat.h>
#include <linux/mm.h>
#include <linux/sched/mm.h>
#include <linux/sched/coredump.h>
#include <linux/sched/numa_balancing.h>
#include <linux/sched/task.h>
#include <linux/hugetlb.h>
#include <linux/mman.h>
#include <linux/swap.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/memremap.h>
#include <linux/ksm.h>
#include <linux/rmap.h>
#include <linux/export.h>
#include <linux/delayacct.h>
#include <linux/init.h>
#include <linux/pfn_t.h>
#include <linux/writeback.h>
#include <linux/memcontrol.h>
#include <linux/mmu_notifier.h>
#include <linux/swapops.h>
#include <linux/elf.h>
#include <linux/gfp.h>
#include <linux/migrate.h>
#include <linux/string.h>
#include <linux/dma-debug.h>
#include <linux/debugfs.h>
#include <linux/userfaultfd_k.h>
#include <linux/dax.h>
#include <linux/oom.h>
#include <linux/numa.h>

#include <trace/events/kmem.h>

#include <asm/io.h>
#include <asm/mmu_context.h>
#include <asm/pgalloc.h>
#include <linux/uaccess.h>
#include <asm/tlb.h>
#include <asm/tlbflush.h>
#include <asm/pgtable.h>

#include "internal.h"

#if defined(LAST_CPUPID_NOT_IN_PAGE_FLAGS) && !defined(CONFIG_COMPILE_TEST)
#warning Unfortunate NUMA and NUMA Balancing config, growing page-frame for last_cpupid.
#endif

#ifndef CONFIG_NEED_MULTIPLE_NODES
/* use the per-pgdat data instead for discontigmem - mbligh */
unsigned long max_mapnr;
EXPORT_SYMBOL(max_mapnr);

struct page *mem_map;
EXPORT_SYMBOL(mem_map);
#endif

/*
 * A number of key systems in x86 including ioremap() rely on the assumption
 * that high_memory defines the upper bound on direct map memory, then end
 * of ZONE_NORMAL.  Under CONFIG_DISCONTIG this means that max_low_pfn and
 * highstart_pfn must be the same; there must be no gap between ZONE_NORMAL
 * and ZONE_HIGHMEM.
 */
void *high_memory;
EXPORT_SYMBOL(high_memory);

/*
 * Randomize the address space (stacks, mmaps, brk, etc.).
 *
 * ( When CONFIG_COMPAT_BRK=y we exclude brk from randomization,
 *   as ancient (libc5 based) binaries can segfault. )
 */
int randomize_va_space __read_mostly =
#ifdef CONFIG_COMPAT_BRK
					1;
#else
					2;
#endif

#ifndef arch_faults_on_old_pte
static inline bool arch_faults_on_old_pte(void)
{
	/*
	 * Those arches which don't have hw access flag feature need to
	 * implement their own helper. By default, "true" means pagefault
	 * will be hit on old pte.
	 */
	return true;
}
#endif

static int __init disable_randmaps(char *s)
{
	randomize_va_space = 0;
	return 1;
}
__setup("norandmaps", disable_randmaps);

unsigned long zero_pfn __read_mostly;
EXPORT_SYMBOL(zero_pfn);

unsigned long highest_memmap_pfn __read_mostly;

/*
 * CONFIG_MMU architectures set up ZERO_PAGE in their paging_init()
 */
static int __init init_zero_pfn(void)
{
	zero_pfn = page_to_pfn(ZERO_PAGE(0));
	return 0;
}
early_initcall(init_zero_pfn);

/*
 * Only trace rss_stat when there is a 512kb cross over.
 * Smaller changes may be lost unless every small change is
 * crossing into or returning to a 512kb boundary.
 */
#define TRACE_MM_COUNTER_THRESHOLD 128

void mm_trace_rss_stat(struct mm_struct *mm, int member, long count,
		       long value)
{
	long thresh_mask = ~(TRACE_MM_COUNTER_THRESHOLD - 1);

	/* Threshold roll-over, trace it */
	if ((count & thresh_mask) != ((count - value) & thresh_mask))
		trace_rss_stat(mm, member, count);
}
EXPORT_SYMBOL_GPL(mm_trace_rss_stat);

#if defined(SPLIT_RSS_COUNTING)

void sync_mm_rss(struct mm_struct *mm)
{
	int i;

	for (i = 0; i < NR_MM_COUNTERS; i++) {
		if (current->rss_stat.count[i]) {
			add_mm_counter(mm, i, current->rss_stat.count[i]);
			current->rss_stat.count[i] = 0;
		}
	}
	current->rss_stat.events = 0;
}

static void add_mm_counter_fast(struct mm_struct *mm, int member, int val)
{
	struct task_struct *task = current;

	if (likely(task->mm == mm))
		task->rss_stat.count[member] += val;
	else
		add_mm_counter(mm, member, val);
}
#define inc_mm_counter_fast(mm, member) add_mm_counter_fast(mm, member, 1)
#define dec_mm_counter_fast(mm, member) add_mm_counter_fast(mm, member, -1)

/* sync counter once per 64 page faults */
#define TASK_RSS_EVENTS_THRESH	(64)
static void check_sync_rss_stat(struct task_struct *task)
{
	if (unlikely(task != current))
		return;
	if (unlikely(task->rss_stat.events++ > TASK_RSS_EVENTS_THRESH))
		sync_mm_rss(task->mm);
}
#else /* SPLIT_RSS_COUNTING */

#define inc_mm_counter_fast(mm, member) inc_mm_counter(mm, member)
#define dec_mm_counter_fast(mm, member) dec_mm_counter(mm, member)

static void check_sync_rss_stat(struct task_struct *task)
{
}

#endif /* SPLIT_RSS_COUNTING */

/*
 * Note: this doesn't free the actual pages themselves. That
 * has been handled earlier when unmapping all the memory regions.
 */
static void free_pte_range(struct mmu_gather *tlb, pmd_t *pmd,
			   unsigned long addr)
{
	pgtable_t token = pmd_pgtable(*pmd);
	pmd_clear(pmd);
	pte_free_tlb(tlb, token, addr);
	mm_dec_nr_ptes(tlb->mm);
}

static inline void free_pmd_range(struct mmu_gather *tlb, pud_t *pud,
				unsigned long addr, unsigned long end,
				unsigned long floor, unsigned long ceiling)
{
	pmd_t *pmd;
	unsigned long next;
	unsigned long start;

	start = addr;
	pmd = pmd_offset(pud, addr);
	do {
		next = pmd_addr_end(addr, end);
		if (pmd_none_or_clear_bad(pmd))
			continue;
		free_pte_range(tlb, pmd, addr);
	} while (pmd++, addr = next, addr != end);

	start &= PUD_MASK;
	if (start < floor)
		return;
	if (ceiling) {
		ceiling &= PUD_MASK;
		if (!ceiling)
			return;
	}
	if (end - 1 > ceiling - 1)
		return;

	pmd = pmd_offset(pud, start);
	pud_clear(pud);
	pmd_free_tlb(tlb, pmd, start);
	mm_dec_nr_pmds(tlb->mm);
}

static inline void free_pud_range(struct mmu_gather *tlb, p4d_t *p4d,
				unsigned long addr, unsigned long end,
				unsigned long floor, unsigned long ceiling)
{
	pud_t *pud;
	unsigned long next;
	unsigned long start;

	start = addr;
	pud = pud_offset(p4d, addr);
	do {
		next = pud_addr_end(addr, end);
		if (pud_none_or_clear_bad(pud))
			continue;
		free_pmd_range(tlb, pud, addr, next, floor, ceiling);
	} while (pud++, addr = next, addr != end);

	start &= P4D_MASK;
	if (start < floor)
		return;
	if (ceiling) {
		ceiling &= P4D_MASK;
		if (!ceiling)
			return;
	}
	if (end - 1 > ceiling - 1)
		return;

	pud = pud_offset(p4d, start);
	p4d_clear(p4d);
	pud_free_tlb(tlb, pud, start);
	mm_dec_nr_puds(tlb->mm);
}

static inline void free_p4d_range(struct mmu_gather *tlb, pgd_t *pgd,
				unsigned long addr, unsigned long end,
				unsigned long floor, unsigned long ceiling)
{
	p4d_t *p4d;
	unsigned long next;
	unsigned long start;

	start = addr;
	p4d = p4d_offset(pgd, addr);
	do {
		next = p4d_addr_end(addr, end);
		if (p4d_none_or_clear_bad(p4d))
			continue;
		free_pud_range(tlb, p4d, addr, next, floor, ceiling);
	} while (p4d++, addr = next, addr != end);

	start &= PGDIR_MASK;
	if (start < floor)
		return;
	if (ceiling) {
		ceiling &= PGDIR_MASK;
		if (!ceiling)
			return;
	}
	if (end - 1 > ceiling - 1)
		return;

	p4d = p4d_offset(pgd, start);
	pgd_clear(pgd);
	p4d_free_tlb(tlb, p4d, start);
}

/*
 * This function frees user-level page tables of a process.
 */
void free_pgd_range(struct mmu_gather *tlb,
			unsigned long addr, unsigned long end,
			unsigned long floor, unsigned long ceiling)
{
	pgd_t *pgd;
	unsigned long next;

	/*
	 * The next few lines have given us lots of grief...
	 *
	 * Why are we testing PMD* at this top level?  Because often
	 * there will be no work to do at all, and we'd prefer not to
	 * go all the way down to the bottom just to discover that.
	 *
	 * Why all these "- 1"s?  Because 0 represents both the bottom
	 * of the address space and the top of it (using -1 for the
	 * top wouldn't help much: the masks would do the wrong thing).
	 * The rule is that addr 0 and floor 0 refer to the bottom of
	 * the address space, but end 0 and ceiling 0 refer to the top
	 * Comparisons need to use "end - 1" and "ceiling - 1" (though
	 * that end 0 case should be mythical).
	 *
	 * Wherever addr is brought up or ceiling brought down, we must
	 * be careful to reject "the opposite 0" before it confuses the
	 * subsequent tests.  But what about where end is brought down
	 * by PMD_SIZE below? no, end can't go down to 0 there.
	 *
	 * Whereas we round start (addr) and ceiling down, by different
	 * masks at different levels, in order to test whether a table
	 * now has no other vmas using it, so can be freed, we don't
	 * bother to round floor or end up - the tests don't need that.
	 */

	addr &= PMD_MASK;
	if (addr < floor) {
		addr += PMD_SIZE;
		if (!addr)
			return;
	}
	if (ceiling) {
		ceiling &= PMD_MASK;
		if (!ceiling)
			return;
	}
	if (end - 1 > ceiling - 1)
		end -= PMD_SIZE;
	if (addr > end - 1)
		return;
	/*
	 * We add page table cache pages with PAGE_SIZE,
	 * (see pte_free_tlb()), flush the tlb if we need
	 */
	tlb_change_page_size(tlb, PAGE_SIZE);
	pgd = pgd_offset(tlb->mm, addr);
	do {
		next = pgd_addr_end(addr, end);
		if (pgd_none_or_clear_bad(pgd))
			continue;
		free_p4d_range(tlb, pgd, addr, next, floor, ceiling);
	} while (pgd++, addr = next, addr != end);
}

void free_pgtables(struct mmu_gather *tlb, struct vm_area_struct *vma,
		unsigned long floor, unsigned long ceiling)
{
	while (vma) {
		struct vm_area_struct *next = vma->vm_next;
		unsigned long addr = vma->vm_start;

		/*
		 * Hide vma from rmap and truncate_pagecache before freeing
		 * pgtables
		 */
		vm_write_begin(vma);
		unlink_anon_vmas(vma);
		vm_write_end(vma);
		unlink_file_vma(vma);

		if (is_vm_hugetlb_page(vma)) {
			hugetlb_free_pgd_range(tlb, addr, vma->vm_end,
				floor, next ? next->vm_start : ceiling);
		} else {
			/*
			 * Optimization: gather nearby vmas into one call down
			 */
			while (next && next->vm_start <= vma->vm_end + PMD_SIZE
			       && !is_vm_hugetlb_page(next)) {
				vma = next;
				next = vma->vm_next;
				vm_write_begin(vma);
				unlink_anon_vmas(vma);
				vm_write_end(vma);
				unlink_file_vma(vma);
			}
			free_pgd_range(tlb, addr, vma->vm_end,
				floor, next ? next->vm_start : ceiling);
		}
		vma = next;
	}
}

int __pte_alloc(struct mm_struct *mm, pmd_t *pmd)
{
	spinlock_t *ptl;
	pgtable_t new = pte_alloc_one(mm);
	if (!new)
		return -ENOMEM;

	/*
	 * Ensure all pte setup (eg. pte page lock and page clearing) are
	 * visible before the pte is made visible to other CPUs by being
	 * put into page tables.
	 *
	 * The other side of the story is the pointer chasing in the page
	 * table walking code (when walking the page table without locking;
	 * ie. most of the time). Fortunately, these data accesses consist
	 * of a chain of data-dependent loads, meaning most CPUs (alpha
	 * being the notable exception) will already guarantee loads are
	 * seen in-order. See the alpha page table accessors for the
	 * smp_read_barrier_depends() barriers in page table walking code.
	 */
	smp_wmb(); /* Could be smp_wmb__xxx(before|after)_spin_lock */

	ptl = pmd_lock(mm, pmd);
	if (likely(pmd_none(*pmd))) {	/* Has another populated it ? */
		mm_inc_nr_ptes(mm);
		pmd_populate(mm, pmd, new);
		new = NULL;
	}
	spin_unlock(ptl);
	if (new)
		pte_free(mm, new);
	return 0;
}

int __pte_alloc_kernel(pmd_t *pmd)
{
	pte_t *new = pte_alloc_one_kernel(&init_mm);
	if (!new)
		return -ENOMEM;

	smp_wmb(); /* See comment in __pte_alloc */

	spin_lock(&init_mm.page_table_lock);
	if (likely(pmd_none(*pmd))) {	/* Has another populated it ? */
		pmd_populate_kernel(&init_mm, pmd, new);
		new = NULL;
	}
	spin_unlock(&init_mm.page_table_lock);
	if (new)
		pte_free_kernel(&init_mm, new);
	return 0;
}

static inline void init_rss_vec(int *rss)
{
	memset(rss, 0, sizeof(int) * NR_MM_COUNTERS);
}

static inline void add_mm_rss_vec(struct mm_struct *mm, int *rss)
{
	int i;

	if (current->mm == mm)
		sync_mm_rss(mm);
	for (i = 0; i < NR_MM_COUNTERS; i++)
		if (rss[i])
			add_mm_counter(mm, i, rss[i]);
}

/*
 * This function is called to print an error when a bad pte
 * is found. For example, we might have a PFN-mapped pte in
 * a region that doesn't allow it.
 *
 * The calling function must still handle the error.
 */
static void print_bad_pte(struct vm_area_struct *vma, unsigned long addr,
			  pte_t pte, struct page *page)
{
	pgd_t *pgd = pgd_offset(vma->vm_mm, addr);
	p4d_t *p4d = p4d_offset(pgd, addr);
	pud_t *pud = pud_offset(p4d, addr);
	pmd_t *pmd = pmd_offset(pud, addr);
	struct address_space *mapping;
	pgoff_t index;
	static unsigned long resume;
	static unsigned long nr_shown;
	static unsigned long nr_unshown;

	/*
	 * Allow a burst of 60 reports, then keep quiet for that minute;
	 * or allow a steady drip of one report per second.
	 */
	if (nr_shown == 60) {
		if (time_before(jiffies, resume)) {
			nr_unshown++;
			return;
		}
		if (nr_unshown) {
			pr_alert("BUG: Bad page map: %lu messages suppressed\n",
				 nr_unshown);
			nr_unshown = 0;
		}
		nr_shown = 0;
	}
	if (nr_shown++ == 0)
		resume = jiffies + 60 * HZ;

	mapping = vma->vm_file ? vma->vm_file->f_mapping : NULL;
	index = linear_page_index(vma, addr);

	pr_alert("BUG: Bad page map in process %s  pte:%08llx pmd:%08llx\n",
		 current->comm,
		 (long long)pte_val(pte), (long long)pmd_val(*pmd));
	if (page)
		dump_page(page, "bad pte");
	pr_alert("addr:%px vm_flags:%08lx anon_vma:%px mapping:%px index:%lx\n",
		 (void *)addr, READ_ONCE(vma->vm_flags), vma->anon_vma, mapping, index);
	pr_alert("file:%pD fault:%ps mmap:%ps readpage:%ps\n",
		 vma->vm_file,
		 vma->vm_ops ? vma->vm_ops->fault : NULL,
		 vma->vm_file ? vma->vm_file->f_op->mmap : NULL,
		 mapping ? mapping->a_ops->readpage : NULL);
	dump_stack();
	add_taint(TAINT_BAD_PAGE, LOCKDEP_NOW_UNRELIABLE);
}

/*
 * __vm_normal_page -- This function gets the "struct page" associated with
 * a pte.
 *
 * "Special" mappings do not wish to be associated with a "struct page" (either
 * it doesn't exist, or it exists but they don't want to touch it). In this
 * case, NULL is returned here. "Normal" mappings do have a struct page.
 *
 * There are 2 broad cases. Firstly, an architecture may define a pte_special()
 * pte bit, in which case this function is trivial. Secondly, an architecture
 * may not have a spare pte bit, which requires a more complicated scheme,
 * described below.
 *
 * A raw VM_PFNMAP mapping (ie. one that is not COWed) is always considered a
 * special mapping (even if there are underlying and valid "struct pages").
 * COWed pages of a VM_PFNMAP are always normal.
 *
 * The way we recognize COWed pages within VM_PFNMAP mappings is through the
 * rules set up by "remap_pfn_range()": the vma will have the VM_PFNMAP bit
 * set, and the vm_pgoff will point to the first PFN mapped: thus every special
 * mapping will always honor the rule
 *
 *	pfn_of_page == vma->vm_pgoff + ((addr - vma->vm_start) >> PAGE_SHIFT)
 *
 * And for normal mappings this is false.
 *
 * This restricts such mappings to be a linear translation from virtual address
 * to pfn. To get around this restriction, we allow arbitrary mappings so long
 * as the vma is not a COW mapping; in that case, we know that all ptes are
 * special (because none can have been COWed).
 *
 *
 * In order to support COW of arbitrary special mappings, we have VM_MIXEDMAP.
 *
 * VM_MIXEDMAP mappings can likewise contain memory with or without "struct
 * page" backing, however the difference is that _all_ pages with a struct
 * page (that is, those where pfn_valid is true) are refcounted and considered
 * normal pages by the VM. The disadvantage is that pages are refcounted
 * (which can be slower and simply not an option for some PFNMAP users). The
 * advantage is that we don't have to follow the strict linearity rule of
 * PFNMAP mappings in order to support COWable mappings.
 *
 */
struct page *_vm_normal_page(struct vm_area_struct *vma, unsigned long addr,
			      pte_t pte, unsigned long vma_flags)
{
	unsigned long pfn = pte_pfn(pte);

	if (IS_ENABLED(CONFIG_ARCH_HAS_PTE_SPECIAL)) {
		if (likely(!pte_special(pte)))
			goto check_pfn;
		if (vma->vm_ops && vma->vm_ops->find_special_page)
			return vma->vm_ops->find_special_page(vma, addr);
		if (vma_flags & (VM_PFNMAP | VM_MIXEDMAP))
			return NULL;
		if (is_zero_pfn(pfn))
			return NULL;
		if (pte_devmap(pte))
			return NULL;

		print_bad_pte(vma, addr, pte, NULL);
		return NULL;
	}

	/* !CONFIG_ARCH_HAS_PTE_SPECIAL case follows: */
	/*
	 * This part should never get called when CONFIG_SPECULATIVE_PAGE_FAULT
	 * is set. This is mainly because we can't rely on vm_start.
	 */

	if (unlikely(vma_flags & (VM_PFNMAP|VM_MIXEDMAP))) {
		if (vma_flags & VM_MIXEDMAP) {
			if (!pfn_valid(pfn))
				return NULL;
			goto out;
		} else {
			unsigned long off;
			off = (addr - vma->vm_start) >> PAGE_SHIFT;
			if (pfn == vma->vm_pgoff + off)
				return NULL;
			if (!is_cow_mapping(vma_flags))
				return NULL;
		}
	}

	if (is_zero_pfn(pfn))
		return NULL;

check_pfn:
	if (unlikely(pfn > highest_memmap_pfn)) {
		print_bad_pte(vma, addr, pte, NULL);
		return NULL;
	}

	/*
	 * NOTE! We still have PageReserved() pages in the page tables.
	 * eg. VDSO mappings can cause them to exist.
	 */
out:
	return pfn_to_page(pfn);
}
EXPORT_SYMBOL(_vm_normal_page);

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
struct page *vm_normal_page_pmd(struct vm_area_struct *vma, unsigned long addr,
				pmd_t pmd)
{
	unsigned long pfn = pmd_pfn(pmd);

	/*
	 * There is no pmd_special() but there may be special pmds, e.g.
	 * in a direct-access (dax) mapping, so let's just replicate the
	 * !CONFIG_ARCH_HAS_PTE_SPECIAL case from vm_normal_page() here.
	 */
	if (unlikely(vma->vm_flags & (VM_PFNMAP|VM_MIXEDMAP))) {
		if (vma->vm_flags & VM_MIXEDMAP) {
			if (!pfn_valid(pfn))
				return NULL;
			goto out;
		} else {
			unsigned long off;
			off = (addr - vma->vm_start) >> PAGE_SHIFT;
			if (pfn == vma->vm_pgoff + off)
				return NULL;
			if (!is_cow_mapping(vma->vm_flags))
				return NULL;
		}
	}

	if (pmd_devmap(pmd))
		return NULL;
	if (is_zero_pfn(pfn))
		return NULL;
	if (unlikely(pfn > highest_memmap_pfn))
		return NULL;

	/*
	 * NOTE! We still have PageReserved() pages in the page tables.
	 * eg. VDSO mappings can cause them to exist.
	 */
out:
	return pfn_to_page(pfn);
}
#endif

/*
 * copy one vm_area from one task to the other. Assumes the page tables
 * already present in the new task to be cleared in the whole range
 * covered by this vma.
 */

static inline unsigned long
copy_one_pte(struct mm_struct *dst_mm, struct mm_struct *src_mm,
		pte_t *dst_pte, pte_t *src_pte, struct vm_area_struct *vma,
		unsigned long addr, int *rss)
{
	unsigned long vm_flags = vma->vm_flags;
	pte_t pte = *src_pte;
	struct page *page;

	/* pte contains position in swap or file, so copy. */
	if (unlikely(!pte_present(pte))) {
		swp_entry_t entry = pte_to_swp_entry(pte);

		if (likely(!non_swap_entry(entry))) {
			if (swap_duplicate(entry) < 0)
				return entry.val;

			/* make sure dst_mm is on swapoff's mmlist. */
			if (unlikely(list_empty(&dst_mm->mmlist))) {
				spin_lock(&mmlist_lock);
				if (list_empty(&dst_mm->mmlist))
					list_add(&dst_mm->mmlist,
							&src_mm->mmlist);
				spin_unlock(&mmlist_lock);
			}
			rss[MM_SWAPENTS]++;
		} else if (is_migration_entry(entry)) {
			page = migration_entry_to_page(entry);

			rss[mm_counter(page)]++;

			if (is_write_migration_entry(entry) &&
					is_cow_mapping(vm_flags)) {
				/*
				 * COW mappings require pages in both
				 * parent and child to be set to read.
				 */
				make_migration_entry_read(&entry);
				pte = swp_entry_to_pte(entry);
				if (pte_swp_soft_dirty(*src_pte))
					pte = pte_swp_mksoft_dirty(pte);
				set_pte_at(src_mm, addr, src_pte, pte);
			}
		} else if (is_device_private_entry(entry)) {
			page = device_private_entry_to_page(entry);

			/*
			 * Update rss count even for unaddressable pages, as
			 * they should treated just like normal pages in this
			 * respect.
			 *
			 * We will likely want to have some new rss counters
			 * for unaddressable pages, at some point. But for now
			 * keep things as they are.
			 */
			get_page(page);
			rss[mm_counter(page)]++;
			page_dup_rmap(page, false);

			/*
			 * We do not preserve soft-dirty information, because so
			 * far, checkpoint/restore is the only feature that
			 * requires that. And checkpoint/restore does not work
			 * when a device driver is involved (you cannot easily
			 * save and restore device driver state).
			 */
			if (is_write_device_private_entry(entry) &&
			    is_cow_mapping(vm_flags)) {
				make_device_private_entry_read(&entry);
				pte = swp_entry_to_pte(entry);
				set_pte_at(src_mm, addr, src_pte, pte);
			}
		}
		goto out_set_pte;
	}

	/*
	 * If it's a COW mapping, write protect it both
	 * in the parent and the child
	 */
	if (is_cow_mapping(vm_flags) && pte_write(pte)) {
		ptep_set_wrprotect(src_mm, addr, src_pte);
		pte = pte_wrprotect(pte);
	}

	/*
	 * If it's a shared mapping, mark it clean in
	 * the child
	 */
	if (vm_flags & VM_SHARED)
		pte = pte_mkclean(pte);
	pte = pte_mkold(pte);

	page = vm_normal_page(vma, addr, pte);
	if (page) {
		get_page(page);
		page_dup_rmap(page, false);
		rss[mm_counter(page)]++;
	} else if (pte_devmap(pte)) {
		page = pte_page(pte);
	}

out_set_pte:
	set_pte_at(dst_mm, addr, dst_pte, pte);
	return 0;
}

#ifdef CONFIG_CONT_PTE_HUGEPAGE
static inline bool cow_cont_pte_user_page(struct page *dst, struct page *src,
		struct vm_fault *vmf)
{
	unsigned long addr = vmf->address & HPAGE_CONT_PTE_MASK;
	int i;

	for (i = 0; i < HPAGE_CONT_PTE_NR; i++) {
		copy_user_highpage(dst + i, src + i, addr + i * PAGE_SIZE, vmf->vma);
	}

	return true;
}

#endif

#ifdef CONFIG_CONT_PTE_HUGEPAGE
static inline int
copy_present_cont_pte(struct mm_struct *dst_mm, struct vm_area_struct *src_vma,
		 pte_t *dst_pte, pte_t *src_pte, unsigned long addr, int *rss)
{
	struct mm_struct *src_mm = src_vma->vm_mm;
	unsigned long vm_flags = src_vma->vm_flags;
	pte_t pte = *src_pte;
	struct page *page;

	page = vm_normal_page(src_vma, addr, pte);
	if (page) {
		if (is_huge_zero_page(page)) {
			mm_get_huge_zero_page(dst_mm);
			goto out_zero_page;
		}

		get_page(page);
		page_dup_rmap(page, true);
		rss[mm_counter(page)] += HPAGE_CONT_PTE_NR;
	}

out_zero_page:
	/*
	 * If it's a COW mapping, write protect it both
	 * in the parent and the child
	 */
	if (is_cow_mapping(vm_flags) && pte_write(pte)) {
		cont_pte_set_huge_pte_wrprotect(src_mm, addr, src_pte);
		pte = pte_wrprotect(pte);
	}

	/*
	 * If it's a shared mapping, mark it clean in
	 * the child
	 */
	if (vm_flags & VM_SHARED)
		pte = pte_mkclean(pte);
	pte = pte_mkold(pte);

#if 0
	if (!userfaultfd_wp(dst_vma))
		pte = pte_clear_uffd_wp(pte);
#endif
	cont_pte_set_huge_pte_at(dst_mm, addr, dst_pte, pte);
	return 0;
}
#endif

static int copy_pte_range(struct mm_struct *dst_mm, struct mm_struct *src_mm,
		   pmd_t *dst_pmd, pmd_t *src_pmd, struct vm_area_struct *vma,
		   unsigned long addr, unsigned long end)
{
	pte_t *orig_src_pte, *orig_dst_pte;
	pte_t *src_pte, *dst_pte;
	spinlock_t *src_ptl, *dst_ptl;
	int progress = 0;
	int rss[NR_MM_COUNTERS];
#ifdef CONFIG_CONT_PTE_HUGEPAGE
	int ret = 0;
#endif
	swp_entry_t entry = (swp_entry_t){0};

again:
	init_rss_vec(rss);

	dst_pte = pte_alloc_map_lock(dst_mm, dst_pmd, addr, &dst_ptl);
	if (!dst_pte)
		return -ENOMEM;
	src_pte = pte_offset_map(src_pmd, addr);
	src_ptl = pte_lockptr(src_mm, src_pmd);
	spin_lock_nested(src_ptl, SINGLE_DEPTH_NESTING);
	orig_src_pte = src_pte;
	orig_dst_pte = dst_pte;
	arch_enter_lazy_mmu_mode();

	do {
		/*
		 * We are holding two locks at this point - either of them
		 * could generate latencies in another task on another CPU.
		 */
		if (progress >= 32) {
			progress = 0;
			if (need_resched() ||
			    spin_needbreak(src_ptl) || spin_needbreak(dst_ptl))
				break;
		}
		if (pte_none(*src_pte)) {
			progress++;
			continue;
		}
#ifdef CONFIG_CONT_PTE_HUGEPAGE
		if (pte_present(*src_pte) && pte_cont(*src_pte)) {
			unsigned long next = pte_cont_addr_end(addr, end);
			if (WARN_ON_ONCE(next - addr != HPAGE_CONT_PTE_SIZE)) {
				/* use bit 60 to count this path only once */
				if (!(atomic64_read(&perf_stat.cp_cont_pte_split_count) & (1UL << 60)))
					atomic64_set(&perf_stat.cp_cont_pte_split_count,
						atomic64_read(&perf_stat.cp_cont_pte_split_count) | (1UL << 60));
				CHP_BUG_ON(1);
				ret = -EAGAIN;
			} else {
#if CONFIG_CHP_ABMORMAL_PTES_DEBUG
				{volatile bool __maybe_unused x = cont_pte_trans_huge(src_pte, CORRUPT_CONT_PTE_REASON_CP_PTE_RANGE1);}
#endif
				ret = copy_present_cont_pte(dst_mm, vma, dst_pte, src_pte,
						addr, rss);
			}
			if (unlikely(ret == -EAGAIN)) {
				atomic64_inc(&perf_stat.cp_cont_pte_split_count);
				__split_huge_cont_pte(dst_mm->mmap, src_pte, addr, false, NULL, src_ptl);
			}
		} else
#endif
		entry.val = copy_one_pte(dst_mm, src_mm, dst_pte, src_pte,
							vma, addr, rss);
		if (entry.val)
			break;
		progress += 8;
#ifdef CONFIG_CONT_PTE_HUGEPAGE
		if (pte_present(*src_pte) && pte_cont(*src_pte)) {
#if CONFIG_CHP_ABMORMAL_PTES_DEBUG
			{volatile bool __maybe_unused x = cont_pte_trans_huge(src_pte, CORRUPT_CONT_PTE_REASON_CP_PTE_RANGE2);}
#endif
			/* "do while()" will do "dst_pte++", "src_pte++" and "addr + PAGE_SIZE" */
			dst_pte += HPAGE_CONT_PTE_NR - 1;
			src_pte += HPAGE_CONT_PTE_NR - 1;
			addr += HPAGE_CONT_PTE_SIZE - PAGE_SIZE;
		}
#endif
	} while (dst_pte++, src_pte++, addr += PAGE_SIZE, addr != end);

	arch_leave_lazy_mmu_mode();
	spin_unlock(src_ptl);
	pte_unmap(orig_src_pte);
	add_mm_rss_vec(dst_mm, rss);
	pte_unmap_unlock(orig_dst_pte, dst_ptl);
	cond_resched();

	if (entry.val) {
		if (add_swap_count_continuation(entry, GFP_KERNEL) < 0)
			return -ENOMEM;
		progress = 0;
	}
	if (addr != end)
		goto again;
	return 0;
}

static inline int copy_pmd_range(struct mm_struct *dst_mm, struct mm_struct *src_mm,
		pud_t *dst_pud, pud_t *src_pud, struct vm_area_struct *vma,
		unsigned long addr, unsigned long end)
{
	pmd_t *src_pmd, *dst_pmd;
	unsigned long next;

	dst_pmd = pmd_alloc(dst_mm, dst_pud, addr);
	if (!dst_pmd)
		return -ENOMEM;
	src_pmd = pmd_offset(src_pud, addr);
	do {
		next = pmd_addr_end(addr, end);
		if (is_swap_pmd(*src_pmd) || pmd_trans_huge(*src_pmd)
			|| pmd_devmap(*src_pmd)) {
			int err;
			VM_BUG_ON_VMA(next-addr != HPAGE_PMD_SIZE, vma);
			err = copy_huge_pmd(dst_mm, src_mm,
					    dst_pmd, src_pmd, addr, vma);
			if (err == -ENOMEM)
				return -ENOMEM;
			if (!err)
				continue;
			/* fall through */
		}
		if (pmd_none_or_clear_bad(src_pmd))
			continue;
		if (copy_pte_range(dst_mm, src_mm, dst_pmd, src_pmd,
						vma, addr, next))
			return -ENOMEM;
	} while (dst_pmd++, src_pmd++, addr = next, addr != end);
	return 0;
}

static inline int copy_pud_range(struct mm_struct *dst_mm, struct mm_struct *src_mm,
		p4d_t *dst_p4d, p4d_t *src_p4d, struct vm_area_struct *vma,
		unsigned long addr, unsigned long end)
{
	pud_t *src_pud, *dst_pud;
	unsigned long next;

	dst_pud = pud_alloc(dst_mm, dst_p4d, addr);
	if (!dst_pud)
		return -ENOMEM;
	src_pud = pud_offset(src_p4d, addr);
	do {
		next = pud_addr_end(addr, end);
		if (pud_trans_huge(*src_pud) || pud_devmap(*src_pud)) {
			int err;

			VM_BUG_ON_VMA(next-addr != HPAGE_PUD_SIZE, vma);
			err = copy_huge_pud(dst_mm, src_mm,
					    dst_pud, src_pud, addr, vma);
			if (err == -ENOMEM)
				return -ENOMEM;
			if (!err)
				continue;
			/* fall through */
		}
		if (pud_none_or_clear_bad(src_pud))
			continue;
		if (copy_pmd_range(dst_mm, src_mm, dst_pud, src_pud,
						vma, addr, next))
			return -ENOMEM;
	} while (dst_pud++, src_pud++, addr = next, addr != end);
	return 0;
}

static inline int copy_p4d_range(struct mm_struct *dst_mm, struct mm_struct *src_mm,
		pgd_t *dst_pgd, pgd_t *src_pgd, struct vm_area_struct *vma,
		unsigned long addr, unsigned long end)
{
	p4d_t *src_p4d, *dst_p4d;
	unsigned long next;

	dst_p4d = p4d_alloc(dst_mm, dst_pgd, addr);
	if (!dst_p4d)
		return -ENOMEM;
	src_p4d = p4d_offset(src_pgd, addr);
	do {
		next = p4d_addr_end(addr, end);
		if (p4d_none_or_clear_bad(src_p4d))
			continue;
		if (copy_pud_range(dst_mm, src_mm, dst_p4d, src_p4d,
						vma, addr, next))
			return -ENOMEM;
	} while (dst_p4d++, src_p4d++, addr = next, addr != end);
	return 0;
}

int copy_page_range(struct mm_struct *dst_mm, struct mm_struct *src_mm,
		struct vm_area_struct *vma)
{
	pgd_t *src_pgd, *dst_pgd;
	unsigned long next;
	unsigned long addr = vma->vm_start;
	unsigned long end = vma->vm_end;
	struct mmu_notifier_range range;
	bool is_cow;
	int ret;

	/*
	 * Don't copy ptes where a page fault will fill them correctly.
	 * Fork becomes much lighter when there are big shared or private
	 * readonly mappings. The tradeoff is that copy_page_range is more
	 * efficient than faulting.
	 */
	if (!(vma->vm_flags & (VM_HUGETLB | VM_PFNMAP | VM_MIXEDMAP)) &&
			!vma->anon_vma)
		return 0;

	if (is_vm_hugetlb_page(vma))
		return copy_hugetlb_page_range(dst_mm, src_mm, vma);

	if (unlikely(vma->vm_flags & VM_PFNMAP)) {
		/*
		 * We do not free on error cases below as remove_vma
		 * gets called on error from higher level routine
		 */
		ret = track_pfn_copy(vma);
		if (ret)
			return ret;
	}

	/*
	 * We need to invalidate the secondary MMU mappings only when
	 * there could be a permission downgrade on the ptes of the
	 * parent mm. And a permission downgrade will only happen if
	 * is_cow_mapping() returns true.
	 */
	is_cow = is_cow_mapping(vma->vm_flags);

	if (is_cow) {
		mmu_notifier_range_init(&range, MMU_NOTIFY_PROTECTION_PAGE,
					0, vma, src_mm, addr, end);
		mmu_notifier_invalidate_range_start(&range);
	}

	ret = 0;
	dst_pgd = pgd_offset(dst_mm, addr);
	src_pgd = pgd_offset(src_mm, addr);
	do {
		next = pgd_addr_end(addr, end);
		if (pgd_none_or_clear_bad(src_pgd))
			continue;
		if (unlikely(copy_p4d_range(dst_mm, src_mm, dst_pgd, src_pgd,
					    vma, addr, next))) {
			ret = -ENOMEM;
			break;
		}
	} while (dst_pgd++, src_pgd++, addr = next, addr != end);

	if (is_cow)
		mmu_notifier_invalidate_range_end(&range);
	return ret;
}

/* Whether we should zap all COWed (private) pages too */
static inline bool should_zap_cows(struct zap_details *details)
{
	/* By default, zap all pages */
	if (!details)
		return true;

	/* Or, we zap COWed pages only if the caller wants to */
	return !details->check_mapping;
}

static unsigned long zap_pte_range(struct mmu_gather *tlb,
				struct vm_area_struct *vma, pmd_t *pmd,
				unsigned long addr, unsigned long end,
				struct zap_details *details)
{
	struct mm_struct *mm = tlb->mm;
	int force_flush = 0;
	int rss[NR_MM_COUNTERS];
	spinlock_t *ptl;
	pte_t *start_pte;
	pte_t *pte;
	swp_entry_t entry;

	tlb_change_page_size(tlb, PAGE_SIZE);
again:
	init_rss_vec(rss);
	start_pte = pte_offset_map_lock(mm, pmd, addr, &ptl);
	pte = start_pte;
	flush_tlb_batched_pending(mm);
	arch_enter_lazy_mmu_mode();
#ifdef CONFIG_CONT_PTE_HUGEPAGE
again_pte:
#endif
	do {
		pte_t ptent = *pte;
		if (pte_none(ptent))
			continue;

		if (need_resched())
			break;

		if (pte_present(ptent)) {
			struct page *page;

			page = vm_normal_page(vma, addr, ptent);
			if (unlikely(details) && page) {
				/*
				 * unmap_shared_mapping_pages() wants to
				 * invalidate cache without truncating:
				 * unmap shared but keep private pages.
				 */
				if (details->check_mapping &&
				    details->check_mapping != page_rmapping(page))
					continue;
			}
#ifdef CONFIG_CONT_PTE_HUGEPAGE
			if (pte_cont(ptent)) {
				unsigned long next = pte_cont_addr_end(addr, end);
#if CONFIG_CHP_ABMORMAL_PTES_DEBUG
				{volatile bool __maybe_unused x = cont_pte_trans_huge(pte, CORRUPT_CONT_PTE_REASON_ZAP_PTE_RANGE);}
#endif
				if (next - addr != HPAGE_CONT_PTE_SIZE) {
					__split_huge_cont_pte(vma, pte, addr, false, NULL, ptl);
				/*
				 * After splitting cont-pte
				 * we need to process pte again.
				 */
				goto again_pte;

				} else {
					cont_pte_huge_ptep_get_and_clear(mm, addr, pte);

					tlb_remove_cont_pte_tlb_entry(tlb, pte, addr);
					if (unlikely(!page))
						continue;

					if (is_huge_zero_page(page)) {
						tlb_remove_page_size(tlb, page, HPAGE_CONT_PTE_SIZE);
						goto cont_next;
					}

					rss[mm_counter(page)] -= HPAGE_CONT_PTE_NR;
					page_remove_rmap(page, true);
					if (unlikely(page_mapcount(page) < 0))
						print_bad_pte(vma, addr, ptent, page);

					tlb_remove_page_size(tlb, page, HPAGE_CONT_PTE_SIZE);
				}
cont_next:
				/* "do while()" will do "pte++" and "addr + PAGE_SIZE" */
				pte += (next - PAGE_SIZE - (addr & PAGE_MASK))/PAGE_SIZE;
				addr = next - PAGE_SIZE;
				continue;
			}
#endif

			ptent = ptep_get_and_clear_full(mm, addr, pte,
							tlb->fullmm);
			tlb_remove_tlb_entry(tlb, pte, addr);
			if (unlikely(!page))
				continue;

			if (!PageAnon(page)) {
				if (pte_dirty(ptent)) {
					force_flush = 1;
					set_page_dirty(page);
				}
				if (pte_young(ptent) &&
				    likely(!(vma->vm_flags & VM_SEQ_READ)))
					mark_page_accessed(page);
			}
			rss[mm_counter(page)]--;
			page_remove_rmap(page, false);
			if (unlikely(page_mapcount(page) < 0))
				print_bad_pte(vma, addr, ptent, page);
			if (unlikely(__tlb_remove_page(tlb, page))) {
				force_flush = 1;
				addr += PAGE_SIZE;
				break;
			}
			continue;
		}

		entry = pte_to_swp_entry(ptent);
		if (non_swap_entry(entry) && is_device_private_entry(entry)) {
			struct page *page = device_private_entry_to_page(entry);

			if (unlikely(details && details->check_mapping)) {
				/*
				 * unmap_shared_mapping_pages() wants to
				 * invalidate cache without truncating:
				 * unmap shared but keep private pages.
				 */
				if (details->check_mapping !=
				    page_rmapping(page))
					continue;
			}

			pte_clear_not_present_full(mm, addr, pte, tlb->fullmm);
			rss[mm_counter(page)]--;
			page_remove_rmap(page, false);
			put_page(page);
			continue;
		}

		if (!non_swap_entry(entry)) {
			/* Genuine swap entry, hence a private anon page */
			if (!should_zap_cows(details))
				continue;
			rss[MM_SWAPENTS]--;
		} else if (is_migration_entry(entry)) {
			struct page *page;

			page = migration_entry_to_page(entry);
			if (details && details->check_mapping &&
			    details->check_mapping != page_rmapping(page))
				continue;
			rss[mm_counter(page)]--;
		}
		if (unlikely(!free_swap_and_cache(entry)))
			print_bad_pte(vma, addr, ptent, NULL);
		pte_clear_not_present_full(mm, addr, pte, tlb->fullmm);
	} while (pte++, addr += PAGE_SIZE, addr != end);

	add_mm_rss_vec(mm, rss);
	arch_leave_lazy_mmu_mode();

	/* Do the actual TLB flush before dropping ptl */
	if (force_flush)
		tlb_flush_mmu_tlbonly(tlb);
	pte_unmap_unlock(start_pte, ptl);

	/*
	 * If we forced a TLB flush (either due to running out of
	 * batch buffers or because we needed to flush dirty TLB
	 * entries before releasing the ptl), free the batched
	 * memory too. Restart if we didn't do everything.
	 */
	if (force_flush) {
		force_flush = 0;
		tlb_flush_mmu(tlb);
	}

	if (addr != end) {
		cond_resched();
		goto again;
	}

	return addr;
}

static inline unsigned long zap_pmd_range(struct mmu_gather *tlb,
				struct vm_area_struct *vma, pud_t *pud,
				unsigned long addr, unsigned long end,
				struct zap_details *details)
{
	pmd_t *pmd;
	unsigned long next;

	pmd = pmd_offset(pud, addr);
	do {
		next = pmd_addr_end(addr, end);
		if (is_swap_pmd(*pmd) || pmd_trans_huge(*pmd) || pmd_devmap(*pmd)) {
			if (next - addr != HPAGE_PMD_SIZE)
				__split_huge_pmd(vma, pmd, addr, false, NULL);
			else if (zap_huge_pmd(tlb, vma, pmd, addr))
				goto next;
			/* fall through */
		} else if (details && details->single_page &&
			   PageTransCompound(details->single_page) &&
			   next - addr == HPAGE_PMD_SIZE && pmd_none(*pmd)) {
			spinlock_t *ptl = pmd_lock(tlb->mm, pmd);
			/*
			 * Take and drop THP pmd lock so that we cannot return
			 * prematurely, while zap_huge_pmd() has cleared *pmd,
			 * but not yet decremented compound_mapcount().
			 */
			spin_unlock(ptl);
		}

		/*
		 * Here there can be other concurrent MADV_DONTNEED or
		 * trans huge page faults running, and if the pmd is
		 * none or trans huge it can change under us. This is
		 * because MADV_DONTNEED holds the mmap_sem in read
		 * mode.
		 */
		if (pmd_none_or_trans_huge_or_clear_bad(pmd))
			goto next;
		next = zap_pte_range(tlb, vma, pmd, addr, next, details);
next:
		cond_resched();
	} while (pmd++, addr = next, addr != end);

	return addr;
}

static inline unsigned long zap_pud_range(struct mmu_gather *tlb,
				struct vm_area_struct *vma, p4d_t *p4d,
				unsigned long addr, unsigned long end,
				struct zap_details *details)
{
	pud_t *pud;
	unsigned long next;

	pud = pud_offset(p4d, addr);
	do {
		next = pud_addr_end(addr, end);
		if (pud_trans_huge(*pud) || pud_devmap(*pud)) {
			if (next - addr != HPAGE_PUD_SIZE) {
				VM_BUG_ON_VMA(!rwsem_is_locked(&tlb->mm->mmap_sem), vma);
				split_huge_pud(vma, pud, addr);
			} else if (zap_huge_pud(tlb, vma, pud, addr))
				goto next;
			/* fall through */
		}
		if (pud_none_or_clear_bad(pud))
			continue;
		next = zap_pmd_range(tlb, vma, pud, addr, next, details);
next:
		cond_resched();
	} while (pud++, addr = next, addr != end);

	return addr;
}

static inline unsigned long zap_p4d_range(struct mmu_gather *tlb,
				struct vm_area_struct *vma, pgd_t *pgd,
				unsigned long addr, unsigned long end,
				struct zap_details *details)
{
	p4d_t *p4d;
	unsigned long next;

	p4d = p4d_offset(pgd, addr);
	do {
		next = p4d_addr_end(addr, end);
		if (p4d_none_or_clear_bad(p4d))
			continue;
		next = zap_pud_range(tlb, vma, p4d, addr, next, details);
	} while (p4d++, addr = next, addr != end);

	return addr;
}

void unmap_page_range(struct mmu_gather *tlb,
			     struct vm_area_struct *vma,
			     unsigned long addr, unsigned long end,
			     struct zap_details *details)
{
	pgd_t *pgd;
	unsigned long next;

	BUG_ON(addr >= end);
	vm_write_begin(vma);
	tlb_start_vma(tlb, vma);
	pgd = pgd_offset(vma->vm_mm, addr);
	do {
		next = pgd_addr_end(addr, end);
		if (pgd_none_or_clear_bad(pgd))
			continue;
		next = zap_p4d_range(tlb, vma, pgd, addr, next, details);
	} while (pgd++, addr = next, addr != end);
	tlb_end_vma(tlb, vma);
	vm_write_end(vma);
}


static void unmap_single_vma(struct mmu_gather *tlb,
		struct vm_area_struct *vma, unsigned long start_addr,
		unsigned long end_addr,
		struct zap_details *details)
{
	unsigned long start = max(vma->vm_start, start_addr);
	unsigned long end;

	if (start >= vma->vm_end)
		return;
	end = min(vma->vm_end, end_addr);
	if (end <= vma->vm_start)
		return;

	if (vma->vm_file)
		uprobe_munmap(vma, start, end);

	if (unlikely(vma->vm_flags & VM_PFNMAP))
		untrack_pfn(vma, 0, 0);

	if (start != end) {
		if (unlikely(is_vm_hugetlb_page(vma))) {
			/*
			 * It is undesirable to test vma->vm_file as it
			 * should be non-null for valid hugetlb area.
			 * However, vm_file will be NULL in the error
			 * cleanup path of mmap_region. When
			 * hugetlbfs ->mmap method fails,
			 * mmap_region() nullifies vma->vm_file
			 * before calling this function to clean up.
			 * Since no pte has actually been setup, it is
			 * safe to do nothing in this case.
			 */
			if (vma->vm_file) {
				i_mmap_lock_write(vma->vm_file->f_mapping);
				__unmap_hugepage_range_final(tlb, vma, start, end, NULL);
				i_mmap_unlock_write(vma->vm_file->f_mapping);
			}
		} else
			unmap_page_range(tlb, vma, start, end, details);
	}
}

/**
 * unmap_vmas - unmap a range of memory covered by a list of vma's
 * @tlb: address of the caller's struct mmu_gather
 * @vma: the starting vma
 * @start_addr: virtual address at which to start unmapping
 * @end_addr: virtual address at which to end unmapping
 *
 * Unmap all pages in the vma list.
 *
 * Only addresses between `start' and `end' will be unmapped.
 *
 * The VMA list must be sorted in ascending virtual address order.
 *
 * unmap_vmas() assumes that the caller will flush the whole unmapped address
 * range after unmap_vmas() returns.  So the only responsibility here is to
 * ensure that any thus-far unmapped pages are flushed before unmap_vmas()
 * drops the lock and schedules.
 */
void unmap_vmas(struct mmu_gather *tlb,
		struct vm_area_struct *vma, unsigned long start_addr,
		unsigned long end_addr)
{
	struct mmu_notifier_range range;

	mmu_notifier_range_init(&range, MMU_NOTIFY_UNMAP, 0, vma, vma->vm_mm,
				start_addr, end_addr);
	mmu_notifier_invalidate_range_start(&range);
	for ( ; vma && vma->vm_start < end_addr; vma = vma->vm_next)
		unmap_single_vma(tlb, vma, start_addr, end_addr, NULL);
	mmu_notifier_invalidate_range_end(&range);
}

/**
 * zap_page_range - remove user pages in a given range
 * @vma: vm_area_struct holding the applicable pages
 * @start: starting address of pages to zap
 * @size: number of bytes to zap
 *
 * Caller must protect the VMA list
 */
void zap_page_range(struct vm_area_struct *vma, unsigned long start,
		unsigned long size)
{
	struct mmu_notifier_range range;
	struct mmu_gather tlb;

	lru_add_drain();
	mmu_notifier_range_init(&range, MMU_NOTIFY_CLEAR, 0, vma, vma->vm_mm,
				start, start + size);
	tlb_gather_mmu(&tlb, vma->vm_mm, start, range.end);
	update_hiwater_rss(vma->vm_mm);
	mmu_notifier_invalidate_range_start(&range);
	for ( ; vma && vma->vm_start < range.end; vma = vma->vm_next)
		unmap_single_vma(&tlb, vma, start, range.end, NULL);
	mmu_notifier_invalidate_range_end(&range);
	tlb_finish_mmu(&tlb, start, range.end);
}

/**
 * zap_page_range_single - remove user pages in a given range
 * @vma: vm_area_struct holding the applicable pages
 * @address: starting address of pages to zap
 * @size: number of bytes to zap
 * @details: details of shared cache invalidation
 *
 * The range must fit into one VMA.
 */
static void zap_page_range_single(struct vm_area_struct *vma, unsigned long address,
		unsigned long size, struct zap_details *details)
{
	struct mmu_notifier_range range;
	struct mmu_gather tlb;

	lru_add_drain();
	mmu_notifier_range_init(&range, MMU_NOTIFY_CLEAR, 0, vma, vma->vm_mm,
				address, address + size);
	tlb_gather_mmu(&tlb, vma->vm_mm, address, range.end);
	update_hiwater_rss(vma->vm_mm);
	mmu_notifier_invalidate_range_start(&range);
	unmap_single_vma(&tlb, vma, address, range.end, details);
	mmu_notifier_invalidate_range_end(&range);
	tlb_finish_mmu(&tlb, address, range.end);
}

/**
 * zap_vma_ptes - remove ptes mapping the vma
 * @vma: vm_area_struct holding ptes to be zapped
 * @address: starting address of pages to zap
 * @size: number of bytes to zap
 *
 * This function only unmaps ptes assigned to VM_PFNMAP vmas.
 *
 * The entire address range must be fully contained within the vma.
 *
 */
void zap_vma_ptes(struct vm_area_struct *vma, unsigned long address,
		unsigned long size)
{
	if (address < vma->vm_start || address + size > vma->vm_end ||
	    		!(vma->vm_flags & VM_PFNMAP))
		return;

	zap_page_range_single(vma, address, size, NULL);
}
EXPORT_SYMBOL_GPL(zap_vma_ptes);

pte_t *__get_locked_pte(struct mm_struct *mm, unsigned long addr,
			spinlock_t **ptl)
{
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;

	pgd = pgd_offset(mm, addr);
	p4d = p4d_alloc(mm, pgd, addr);
	if (!p4d)
		return NULL;
	pud = pud_alloc(mm, p4d, addr);
	if (!pud)
		return NULL;
	pmd = pmd_alloc(mm, pud, addr);
	if (!pmd)
		return NULL;

	VM_BUG_ON(pmd_trans_huge(*pmd));
	return pte_alloc_map_lock(mm, pmd, addr, ptl);
}

/*
 * This is the old fallback for page remapping.
 *
 * For historical reasons, it only allows reserved pages. Only
 * old drivers should use this, and they needed to mark their
 * pages reserved for the old functions anyway.
 */
static int insert_page(struct vm_area_struct *vma, unsigned long addr,
			struct page *page, pgprot_t prot)
{
	struct mm_struct *mm = vma->vm_mm;
	int retval;
	pte_t *pte;
	spinlock_t *ptl;

	retval = -EINVAL;
	if (PageAnon(page) || PageSlab(page) || page_has_type(page))
		goto out;
	retval = -ENOMEM;
	flush_dcache_page(page);
	pte = get_locked_pte(mm, addr, &ptl);
	if (!pte)
		goto out;
	retval = -EBUSY;
	if (!pte_none(*pte))
		goto out_unlock;

	/* Ok, finally just insert the thing.. */
	get_page(page);
	inc_mm_counter_fast(mm, mm_counter_file(page));
	page_add_file_rmap(page, false);
	set_pte_at(mm, addr, pte, mk_pte(page, prot));

	retval = 0;
out_unlock:
	pte_unmap_unlock(pte, ptl);
out:
	return retval;
}

/**
 * vm_insert_page - insert single page into user vma
 * @vma: user vma to map to
 * @addr: target user address of this page
 * @page: source kernel page
 *
 * This allows drivers to insert individual pages they've allocated
 * into a user vma.
 *
 * The page has to be a nice clean _individual_ kernel allocation.
 * If you allocate a compound page, you need to have marked it as
 * such (__GFP_COMP), or manually just split the page up yourself
 * (see split_page()).
 *
 * NOTE! Traditionally this was done with "remap_pfn_range()" which
 * took an arbitrary page protection parameter. This doesn't allow
 * that. Your vma protection will have to be set up correctly, which
 * means that if you want a shared writable mapping, you'd better
 * ask for a shared writable mapping!
 *
 * The page does not need to be reserved.
 *
 * Usually this function is called from f_op->mmap() handler
 * under mm->mmap_sem write-lock, so it can change vma->vm_flags.
 * Caller must set VM_MIXEDMAP on vma if it wants to call this
 * function from other places, for example from page-fault handler.
 *
 * Return: %0 on success, negative error code otherwise.
 */
int vm_insert_page(struct vm_area_struct *vma, unsigned long addr,
			struct page *page)
{
	if (addr < vma->vm_start || addr >= vma->vm_end)
		return -EFAULT;
	if (!page_count(page))
		return -EINVAL;
	if (!(vma->vm_flags & VM_MIXEDMAP)) {
		BUG_ON(down_read_trylock(&vma->vm_mm->mmap_sem));
		BUG_ON(vma->vm_flags & VM_PFNMAP);
		vma->vm_flags |= VM_MIXEDMAP;
	}
	return insert_page(vma, addr, page, vma->vm_page_prot);
}
EXPORT_SYMBOL(vm_insert_page);

/*
 * __vm_map_pages - maps range of kernel pages into user vma
 * @vma: user vma to map to
 * @pages: pointer to array of source kernel pages
 * @num: number of pages in page array
 * @offset: user's requested vm_pgoff
 *
 * This allows drivers to map range of kernel pages into a user vma.
 *
 * Return: 0 on success and error code otherwise.
 */
static int __vm_map_pages(struct vm_area_struct *vma, struct page **pages,
				unsigned long num, unsigned long offset)
{
	unsigned long count = vma_pages(vma);
	unsigned long uaddr = vma->vm_start;
	int ret, i;

	/* Fail if the user requested offset is beyond the end of the object */
	if (offset >= num)
		return -ENXIO;

	/* Fail if the user requested size exceeds available object size */
	if (count > num - offset)
		return -ENXIO;

	for (i = 0; i < count; i++) {
		ret = vm_insert_page(vma, uaddr, pages[offset + i]);
		if (ret < 0)
			return ret;
		uaddr += PAGE_SIZE;
	}

	return 0;
}

/**
 * vm_map_pages - maps range of kernel pages starts with non zero offset
 * @vma: user vma to map to
 * @pages: pointer to array of source kernel pages
 * @num: number of pages in page array
 *
 * Maps an object consisting of @num pages, catering for the user's
 * requested vm_pgoff
 *
 * If we fail to insert any page into the vma, the function will return
 * immediately leaving any previously inserted pages present.  Callers
 * from the mmap handler may immediately return the error as their caller
 * will destroy the vma, removing any successfully inserted pages. Other
 * callers should make their own arrangements for calling unmap_region().
 *
 * Context: Process context. Called by mmap handlers.
 * Return: 0 on success and error code otherwise.
 */
int vm_map_pages(struct vm_area_struct *vma, struct page **pages,
				unsigned long num)
{
	return __vm_map_pages(vma, pages, num, vma->vm_pgoff);
}
EXPORT_SYMBOL(vm_map_pages);

/**
 * vm_map_pages_zero - map range of kernel pages starts with zero offset
 * @vma: user vma to map to
 * @pages: pointer to array of source kernel pages
 * @num: number of pages in page array
 *
 * Similar to vm_map_pages(), except that it explicitly sets the offset
 * to 0. This function is intended for the drivers that did not consider
 * vm_pgoff.
 *
 * Context: Process context. Called by mmap handlers.
 * Return: 0 on success and error code otherwise.
 */
int vm_map_pages_zero(struct vm_area_struct *vma, struct page **pages,
				unsigned long num)
{
	return __vm_map_pages(vma, pages, num, 0);
}
EXPORT_SYMBOL(vm_map_pages_zero);

static vm_fault_t insert_pfn(struct vm_area_struct *vma, unsigned long addr,
			pfn_t pfn, pgprot_t prot, bool mkwrite)
{
	struct mm_struct *mm = vma->vm_mm;
	pte_t *pte, entry;
	spinlock_t *ptl;

	pte = get_locked_pte(mm, addr, &ptl);
	if (!pte)
		return VM_FAULT_OOM;
	if (!pte_none(*pte)) {
		if (mkwrite) {
			/*
			 * For read faults on private mappings the PFN passed
			 * in may not match the PFN we have mapped if the
			 * mapped PFN is a writeable COW page.  In the mkwrite
			 * case we are creating a writable PTE for a shared
			 * mapping and we expect the PFNs to match. If they
			 * don't match, we are likely racing with block
			 * allocation and mapping invalidation so just skip the
			 * update.
			 */
			if (pte_pfn(*pte) != pfn_t_to_pfn(pfn)) {
				WARN_ON_ONCE(!is_zero_pfn(pte_pfn(*pte)));
				goto out_unlock;
			}
			entry = pte_mkyoung(*pte);
			entry = maybe_mkwrite(pte_mkdirty(entry),
							vma->vm_flags);
			if (ptep_set_access_flags(vma, addr, pte, entry, 1))
				update_mmu_cache(vma, addr, pte);
		}
		goto out_unlock;
	}

	/* Ok, finally just insert the thing.. */
	if (pfn_t_devmap(pfn))
		entry = pte_mkdevmap(pfn_t_pte(pfn, prot));
	else
		entry = pte_mkspecial(pfn_t_pte(pfn, prot));

	if (mkwrite) {
		entry = pte_mkyoung(entry);
		entry = maybe_mkwrite(pte_mkdirty(entry), vma->vm_flags);
	}

	set_pte_at(mm, addr, pte, entry);
	update_mmu_cache(vma, addr, pte); /* XXX: why not for insert_page? */

out_unlock:
	pte_unmap_unlock(pte, ptl);
	return VM_FAULT_NOPAGE;
}

/**
 * vmf_insert_pfn_prot - insert single pfn into user vma with specified pgprot
 * @vma: user vma to map to
 * @addr: target user address of this page
 * @pfn: source kernel pfn
 * @pgprot: pgprot flags for the inserted page
 *
 * This is exactly like vmf_insert_pfn(), except that it allows drivers to
 * to override pgprot on a per-page basis.
 *
 * This only makes sense for IO mappings, and it makes no sense for
 * COW mappings.  In general, using multiple vmas is preferable;
 * vmf_insert_pfn_prot should only be used if using multiple VMAs is
 * impractical.
 *
 * Context: Process context.  May allocate using %GFP_KERNEL.
 * Return: vm_fault_t value.
 */
vm_fault_t vmf_insert_pfn_prot(struct vm_area_struct *vma, unsigned long addr,
			unsigned long pfn, pgprot_t pgprot)
{
	/*
	 * Technically, architectures with pte_special can avoid all these
	 * restrictions (same for remap_pfn_range).  However we would like
	 * consistency in testing and feature parity among all, so we should
	 * try to keep these invariants in place for everybody.
	 */
	BUG_ON(!(vma->vm_flags & (VM_PFNMAP|VM_MIXEDMAP)));
	BUG_ON((vma->vm_flags & (VM_PFNMAP|VM_MIXEDMAP)) ==
						(VM_PFNMAP|VM_MIXEDMAP));
	BUG_ON((vma->vm_flags & VM_PFNMAP) && is_cow_mapping(vma->vm_flags));
	BUG_ON((vma->vm_flags & VM_MIXEDMAP) && pfn_valid(pfn));

	if (addr < vma->vm_start || addr >= vma->vm_end)
		return VM_FAULT_SIGBUS;

	if (!pfn_modify_allowed(pfn, pgprot))
		return VM_FAULT_SIGBUS;

	track_pfn_insert(vma, &pgprot, __pfn_to_pfn_t(pfn, PFN_DEV));

	return insert_pfn(vma, addr, __pfn_to_pfn_t(pfn, PFN_DEV), pgprot,
			false);
}
EXPORT_SYMBOL(vmf_insert_pfn_prot);

/**
 * vmf_insert_pfn - insert single pfn into user vma
 * @vma: user vma to map to
 * @addr: target user address of this page
 * @pfn: source kernel pfn
 *
 * Similar to vm_insert_page, this allows drivers to insert individual pages
 * they've allocated into a user vma. Same comments apply.
 *
 * This function should only be called from a vm_ops->fault handler, and
 * in that case the handler should return the result of this function.
 *
 * vma cannot be a COW mapping.
 *
 * As this is called only for pages that do not currently exist, we
 * do not need to flush old virtual caches or the TLB.
 *
 * Context: Process context.  May allocate using %GFP_KERNEL.
 * Return: vm_fault_t value.
 */
vm_fault_t vmf_insert_pfn(struct vm_area_struct *vma, unsigned long addr,
			unsigned long pfn)
{
	return vmf_insert_pfn_prot(vma, addr, pfn, vma->vm_page_prot);
}
EXPORT_SYMBOL(vmf_insert_pfn);

static bool vm_mixed_ok(struct vm_area_struct *vma, pfn_t pfn)
{
	/* these checks mirror the abort conditions in vm_normal_page */
	if (vma->vm_flags & VM_MIXEDMAP)
		return true;
	if (pfn_t_devmap(pfn))
		return true;
	if (pfn_t_special(pfn))
		return true;
	if (is_zero_pfn(pfn_t_to_pfn(pfn)))
		return true;
	return false;
}

static vm_fault_t __vm_insert_mixed(struct vm_area_struct *vma,
		unsigned long addr, pfn_t pfn, bool mkwrite)
{
	pgprot_t pgprot = vma->vm_page_prot;
	int err;

	BUG_ON(!vm_mixed_ok(vma, pfn));

	if (addr < vma->vm_start || addr >= vma->vm_end)
		return VM_FAULT_SIGBUS;

	track_pfn_insert(vma, &pgprot, pfn);

	if (!pfn_modify_allowed(pfn_t_to_pfn(pfn), pgprot))
		return VM_FAULT_SIGBUS;

	/*
	 * If we don't have pte special, then we have to use the pfn_valid()
	 * based VM_MIXEDMAP scheme (see vm_normal_page), and thus we *must*
	 * refcount the page if pfn_valid is true (hence insert_page rather
	 * than insert_pfn).  If a zero_pfn were inserted into a VM_MIXEDMAP
	 * without pte special, it would there be refcounted as a normal page.
	 */
	if (!IS_ENABLED(CONFIG_ARCH_HAS_PTE_SPECIAL) &&
	    !pfn_t_devmap(pfn) && pfn_t_valid(pfn)) {
		struct page *page;

		/*
		 * At this point we are committed to insert_page()
		 * regardless of whether the caller specified flags that
		 * result in pfn_t_has_page() == false.
		 */
		page = pfn_to_page(pfn_t_to_pfn(pfn));
		err = insert_page(vma, addr, page, pgprot);
	} else {
		return insert_pfn(vma, addr, pfn, pgprot, mkwrite);
	}

	if (err == -ENOMEM)
		return VM_FAULT_OOM;
	if (err < 0 && err != -EBUSY)
		return VM_FAULT_SIGBUS;

	return VM_FAULT_NOPAGE;
}

vm_fault_t vmf_insert_mixed(struct vm_area_struct *vma, unsigned long addr,
		pfn_t pfn)
{
	return __vm_insert_mixed(vma, addr, pfn, false);
}
EXPORT_SYMBOL(vmf_insert_mixed);

/*
 *  If the insertion of PTE failed because someone else already added a
 *  different entry in the mean time, we treat that as success as we assume
 *  the same entry was actually inserted.
 */
vm_fault_t vmf_insert_mixed_mkwrite(struct vm_area_struct *vma,
		unsigned long addr, pfn_t pfn)
{
	return __vm_insert_mixed(vma, addr, pfn, true);
}
EXPORT_SYMBOL(vmf_insert_mixed_mkwrite);

/*
 * maps a range of physical memory into the requested pages. the old
 * mappings are removed. any references to nonexistent pages results
 * in null mappings (currently treated as "copy-on-access")
 */
static int remap_pte_range(struct mm_struct *mm, pmd_t *pmd,
			unsigned long addr, unsigned long end,
			unsigned long pfn, pgprot_t prot)
{
	pte_t *pte, *mapped_pte;
	spinlock_t *ptl;
	int err = 0;

	mapped_pte = pte = pte_alloc_map_lock(mm, pmd, addr, &ptl);
	if (!pte)
		return -ENOMEM;
	arch_enter_lazy_mmu_mode();
	do {
		BUG_ON(!pte_none(*pte));
		if (!pfn_modify_allowed(pfn, prot)) {
			err = -EACCES;
			break;
		}
		set_pte_at(mm, addr, pte, pte_mkspecial(pfn_pte(pfn, prot)));
		pfn++;
	} while (pte++, addr += PAGE_SIZE, addr != end);
	arch_leave_lazy_mmu_mode();
	pte_unmap_unlock(mapped_pte, ptl);
	return err;
}

static inline int remap_pmd_range(struct mm_struct *mm, pud_t *pud,
			unsigned long addr, unsigned long end,
			unsigned long pfn, pgprot_t prot)
{
	pmd_t *pmd;
	unsigned long next;
	int err;

	pfn -= addr >> PAGE_SHIFT;
	pmd = pmd_alloc(mm, pud, addr);
	if (!pmd)
		return -ENOMEM;
	VM_BUG_ON(pmd_trans_huge(*pmd));
	do {
		next = pmd_addr_end(addr, end);
		err = remap_pte_range(mm, pmd, addr, next,
				pfn + (addr >> PAGE_SHIFT), prot);
		if (err)
			return err;
	} while (pmd++, addr = next, addr != end);
	return 0;
}

static inline int remap_pud_range(struct mm_struct *mm, p4d_t *p4d,
			unsigned long addr, unsigned long end,
			unsigned long pfn, pgprot_t prot)
{
	pud_t *pud;
	unsigned long next;
	int err;

	pfn -= addr >> PAGE_SHIFT;
	pud = pud_alloc(mm, p4d, addr);
	if (!pud)
		return -ENOMEM;
	do {
		next = pud_addr_end(addr, end);
		err = remap_pmd_range(mm, pud, addr, next,
				pfn + (addr >> PAGE_SHIFT), prot);
		if (err)
			return err;
	} while (pud++, addr = next, addr != end);
	return 0;
}

static inline int remap_p4d_range(struct mm_struct *mm, pgd_t *pgd,
			unsigned long addr, unsigned long end,
			unsigned long pfn, pgprot_t prot)
{
	p4d_t *p4d;
	unsigned long next;
	int err;

	pfn -= addr >> PAGE_SHIFT;
	p4d = p4d_alloc(mm, pgd, addr);
	if (!p4d)
		return -ENOMEM;
	do {
		next = p4d_addr_end(addr, end);
		err = remap_pud_range(mm, p4d, addr, next,
				pfn + (addr >> PAGE_SHIFT), prot);
		if (err)
			return err;
	} while (p4d++, addr = next, addr != end);
	return 0;
}

/**
 * remap_pfn_range - remap kernel memory to userspace
 * @vma: user vma to map to
 * @addr: target user address to start at
 * @pfn: physical address of kernel memory
 * @size: size of map area
 * @prot: page protection flags for this mapping
 *
 * Note: this is only safe if the mm semaphore is held when called.
 *
 * Return: %0 on success, negative error code otherwise.
 */
int remap_pfn_range(struct vm_area_struct *vma, unsigned long addr,
		    unsigned long pfn, unsigned long size, pgprot_t prot)
{
	pgd_t *pgd;
	unsigned long next;
	unsigned long end = addr + PAGE_ALIGN(size);
	struct mm_struct *mm = vma->vm_mm;
	unsigned long remap_pfn = pfn;
	int err;

	/*
	 * Physically remapped pages are special. Tell the
	 * rest of the world about it:
	 *   VM_IO tells people not to look at these pages
	 *	(accesses can have side effects).
	 *   VM_PFNMAP tells the core MM that the base pages are just
	 *	raw PFN mappings, and do not have a "struct page" associated
	 *	with them.
	 *   VM_DONTEXPAND
	 *      Disable vma merging and expanding with mremap().
	 *   VM_DONTDUMP
	 *      Omit vma from core dump, even when VM_IO turned off.
	 *
	 * There's a horrible special case to handle copy-on-write
	 * behaviour that some programs depend on. We mark the "original"
	 * un-COW'ed pages by matching them up with "vma->vm_pgoff".
	 * See vm_normal_page() for details.
	 */
	if (is_cow_mapping(vma->vm_flags)) {
		if (addr != vma->vm_start || end != vma->vm_end)
			return -EINVAL;
		vma->vm_pgoff = pfn;
	}

	err = track_pfn_remap(vma, &prot, remap_pfn, addr, PAGE_ALIGN(size));
	if (err)
		return -EINVAL;

	vma->vm_flags |= VM_IO | VM_PFNMAP | VM_DONTEXPAND | VM_DONTDUMP;

	BUG_ON(addr >= end);
	pfn -= addr >> PAGE_SHIFT;
	pgd = pgd_offset(mm, addr);
	flush_cache_range(vma, addr, end);
	do {
		next = pgd_addr_end(addr, end);
		err = remap_p4d_range(mm, pgd, addr, next,
				pfn + (addr >> PAGE_SHIFT), prot);
		if (err)
			break;
	} while (pgd++, addr = next, addr != end);

	if (err)
		untrack_pfn(vma, remap_pfn, PAGE_ALIGN(size));

	return err;
}
EXPORT_SYMBOL(remap_pfn_range);

/**
 * vm_iomap_memory - remap memory to userspace
 * @vma: user vma to map to
 * @start: start of area
 * @len: size of area
 *
 * This is a simplified io_remap_pfn_range() for common driver use. The
 * driver just needs to give us the physical memory range to be mapped,
 * we'll figure out the rest from the vma information.
 *
 * NOTE! Some drivers might want to tweak vma->vm_page_prot first to get
 * whatever write-combining details or similar.
 *
 * Return: %0 on success, negative error code otherwise.
 */
int vm_iomap_memory(struct vm_area_struct *vma, phys_addr_t start, unsigned long len)
{
	unsigned long vm_len, pfn, pages;

	/* Check that the physical memory area passed in looks valid */
	if (start + len < start)
		return -EINVAL;
	/*
	 * You *really* shouldn't map things that aren't page-aligned,
	 * but we've historically allowed it because IO memory might
	 * just have smaller alignment.
	 */
	len += start & ~PAGE_MASK;
	pfn = start >> PAGE_SHIFT;
	pages = (len + ~PAGE_MASK) >> PAGE_SHIFT;
	if (pfn + pages < pfn)
		return -EINVAL;

	/* We start the mapping 'vm_pgoff' pages into the area */
	if (vma->vm_pgoff > pages)
		return -EINVAL;
	pfn += vma->vm_pgoff;
	pages -= vma->vm_pgoff;

	/* Can we fit all of the mapping? */
	vm_len = vma->vm_end - vma->vm_start;
	if (vm_len >> PAGE_SHIFT > pages)
		return -EINVAL;

	/* Ok, let it rip */
	return io_remap_pfn_range(vma, vma->vm_start, pfn, vm_len, vma->vm_page_prot);
}
EXPORT_SYMBOL(vm_iomap_memory);

static int apply_to_pte_range(struct mm_struct *mm, pmd_t *pmd,
				     unsigned long addr, unsigned long end,
				     pte_fn_t fn, void *data)
{
	pte_t *pte;
	int err;
	spinlock_t *uninitialized_var(ptl);

	pte = (mm == &init_mm) ?
		pte_alloc_kernel(pmd, addr) :
		pte_alloc_map_lock(mm, pmd, addr, &ptl);
	if (!pte)
		return -ENOMEM;

	BUG_ON(pmd_huge(*pmd));

	arch_enter_lazy_mmu_mode();

	do {
		err = fn(pte++, addr, data);
		if (err)
			break;
	} while (addr += PAGE_SIZE, addr != end);

	arch_leave_lazy_mmu_mode();

	if (mm != &init_mm)
		pte_unmap_unlock(pte-1, ptl);
	return err;
}

static int apply_to_pmd_range(struct mm_struct *mm, pud_t *pud,
				     unsigned long addr, unsigned long end,
				     pte_fn_t fn, void *data)
{
	pmd_t *pmd;
	unsigned long next;
	int err;

	BUG_ON(pud_huge(*pud));

	pmd = pmd_alloc(mm, pud, addr);
	if (!pmd)
		return -ENOMEM;
	do {
		next = pmd_addr_end(addr, end);
		err = apply_to_pte_range(mm, pmd, addr, next, fn, data);
		if (err)
			break;
	} while (pmd++, addr = next, addr != end);
	return err;
}

static int apply_to_pud_range(struct mm_struct *mm, p4d_t *p4d,
				     unsigned long addr, unsigned long end,
				     pte_fn_t fn, void *data)
{
	pud_t *pud;
	unsigned long next;
	int err;

	pud = pud_alloc(mm, p4d, addr);
	if (!pud)
		return -ENOMEM;
	do {
		next = pud_addr_end(addr, end);
		err = apply_to_pmd_range(mm, pud, addr, next, fn, data);
		if (err)
			break;
	} while (pud++, addr = next, addr != end);
	return err;
}

static int apply_to_p4d_range(struct mm_struct *mm, pgd_t *pgd,
				     unsigned long addr, unsigned long end,
				     pte_fn_t fn, void *data)
{
	p4d_t *p4d;
	unsigned long next;
	int err;

	p4d = p4d_alloc(mm, pgd, addr);
	if (!p4d)
		return -ENOMEM;
	do {
		next = p4d_addr_end(addr, end);
		err = apply_to_pud_range(mm, p4d, addr, next, fn, data);
		if (err)
			break;
	} while (p4d++, addr = next, addr != end);
	return err;
}

/*
 * Scan a region of virtual memory, filling in page tables as necessary
 * and calling a provided function on each leaf page table.
 */
int apply_to_page_range(struct mm_struct *mm, unsigned long addr,
			unsigned long size, pte_fn_t fn, void *data)
{
	pgd_t *pgd;
	unsigned long next;
	unsigned long end = addr + size;
	int err;

	if (WARN_ON(addr >= end))
		return -EINVAL;

	pgd = pgd_offset(mm, addr);
	do {
		next = pgd_addr_end(addr, end);
		err = apply_to_p4d_range(mm, pgd, addr, next, fn, data);
		if (err)
			break;
	} while (pgd++, addr = next, addr != end);

	return err;
}
EXPORT_SYMBOL_GPL(apply_to_page_range);

#ifdef CONFIG_SPECULATIVE_PAGE_FAULT
static bool pte_spinlock(struct vm_fault *vmf)
{
	bool ret = false;
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	pmd_t pmdval;
#endif

	/* Check if vma is still valid */
	if (!(vmf->flags & FAULT_FLAG_SPECULATIVE)) {
		vmf->ptl = pte_lockptr(vmf->vma->vm_mm, vmf->pmd);
		spin_lock(vmf->ptl);
		return true;
	}

	local_irq_disable();
	if (vma_has_changed(vmf))
		goto out;

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	/*
	 * We check if the pmd value is still the same to ensure that there
	 * is not a huge collapse operation in progress in our back.
	 */
	pmdval = READ_ONCE(*vmf->pmd);
	if (!pmd_same(pmdval, vmf->orig_pmd))
		goto out;
#endif

	vmf->ptl = pte_lockptr(vmf->vma->vm_mm, vmf->pmd);
	if (unlikely(!spin_trylock(vmf->ptl)))
		goto out;

	if (vma_has_changed(vmf)) {
		spin_unlock(vmf->ptl);
		goto out;
	}

	ret = true;
out:
	local_irq_enable();
	return ret;
}

#ifdef CONFIG_CONT_PTE_HUGEPAGE
bool pte_map_lock(struct vm_fault *vmf)
#else
static bool pte_map_lock(struct vm_fault *vmf)
#endif
{
	bool ret = false;
	pte_t *pte;
	spinlock_t *ptl;
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	pmd_t pmdval;
#endif

	if (!(vmf->flags & FAULT_FLAG_SPECULATIVE)) {
		vmf->pte = pte_offset_map_lock(vmf->vma->vm_mm, vmf->pmd,
					       vmf->address, &vmf->ptl);
		return true;
	}

	/*
	 * The first vma_has_changed() guarantees the page-tables are still
	 * valid, having IRQs disabled ensures they stay around, hence the
	 * second vma_has_changed() to make sure they are still valid once
	 * we've got the lock. After that a concurrent zap_pte_range() will
	 * block on the PTL and thus we're safe.
	 */
	local_irq_disable();
	if (vma_has_changed(vmf))
		goto out;

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	/*
	 * We check if the pmd value is still the same to ensure that there
	 * is not a huge collapse operation in progress in our back.
	 */
	pmdval = READ_ONCE(*vmf->pmd);
	if (!pmd_same(pmdval, vmf->orig_pmd))
		goto out;
#endif

	/*
	 * Same as pte_offset_map_lock() except that we call
	 * spin_trylock() in place of spin_lock() to avoid race with
	 * unmap path which may have the lock and wait for this CPU
	 * to invalidate TLB but this CPU has irq disabled.
	 * Since we are in a speculative patch, accept it could fail
	 */
	ptl = pte_lockptr(vmf->vma->vm_mm, vmf->pmd);
	pte = pte_offset_map(vmf->pmd, vmf->address);
	if (unlikely(!spin_trylock(ptl))) {
		pte_unmap(pte);
		goto out;
	}

	if (vma_has_changed(vmf)) {
		pte_unmap_unlock(pte, ptl);
		goto out;
	}

	vmf->pte = pte;
	vmf->ptl = ptl;
	ret = true;
out:
	local_irq_enable();
	return ret;
}
#else
static inline bool pte_spinlock(struct vm_fault *vmf)
{
	vmf->ptl = pte_lockptr(vmf->vma->vm_mm, vmf->pmd);
	spin_lock(vmf->ptl);
	return true;
}

#ifdef CONFIG_CONT_PTE_HUGEPAGE
inline bool pte_map_lock(struct vm_fault *vmf)
#else
static inline bool pte_map_lock(struct vm_fault *vmf)
#endif
{
	vmf->pte = pte_offset_map_lock(vmf->vma->vm_mm, vmf->pmd,
				       vmf->address, &vmf->ptl);
	return true;
}
#endif /* CONFIG_SPECULATIVE_PAGE_FAULT */

/*
 * handle_pte_fault chooses page fault handler according to an entry which was
 * read non-atomically.  Before making any commitment, on those architectures
 * or configurations (e.g. i386 with PAE) which might give a mix of unmatched
 * parts, do_swap_page must check under lock before unmapping the pte and
 * proceeding (but do_wp_page is only called after already making such a check;
 * and do_anonymous_page can safely check later on).
 *
 * pte_unmap_same() returns:
 *	0			if the PTE are the same
 *	VM_FAULT_PTNOTSAME	if the PTE are different
 *	VM_FAULT_RETRY		if the VMA has changed in our back during
 *				a speculative page fault handling.
 */
static inline int pte_unmap_same(struct vm_fault *vmf)
{
	int ret = 0;

#if defined(CONFIG_SMP) || defined(CONFIG_PREEMPT)
	if (sizeof(pte_t) > sizeof(unsigned long)) {
		if (pte_spinlock(vmf)) {
			if (!pte_same(*vmf->pte, vmf->orig_pte))
				ret = VM_FAULT_PTNOTSAME;
			spin_unlock(vmf->ptl);
		} else
			ret = VM_FAULT_RETRY;
	}
#endif
	pte_unmap(vmf->pte);
	return ret;
}

static inline bool cow_user_page(struct page *dst, struct page *src,
				 struct vm_fault *vmf)
{
	bool ret;
	void *kaddr;
	void __user *uaddr;
	bool locked = false;
	struct vm_area_struct *vma = vmf->vma;
	struct mm_struct *mm = vma->vm_mm;
	unsigned long addr = vmf->address;

	debug_dma_assert_idle(src);

	if (likely(src)) {
#ifdef CONFIG_CONT_PTE_HUGEPAGE
	if (ContPteHugePage(src) && ContPteHugePage(dst))
		return cow_cont_pte_user_page(dst, src, vmf);
#endif
		copy_user_highpage(dst, src, addr, vma);
		return true;
	}

	/*
	 * If the source page was a PFN mapping, we don't have
	 * a "struct page" for it. We do a best-effort copy by
	 * just copying from the original user address. If that
	 * fails, we just zero-fill it. Live with it.
	 */
	kaddr = kmap_atomic(dst);
	uaddr = (void __user *)(addr & PAGE_MASK);

	/*
	 * On architectures with software "accessed" bits, we would
	 * take a double page fault, so mark it accessed here.
	 */
	if (arch_faults_on_old_pte() && !pte_young(vmf->orig_pte)) {
		pte_t entry;

		vmf->pte = pte_offset_map_lock(mm, vmf->pmd, addr, &vmf->ptl);
		locked = true;
		if (!likely(pte_same(*vmf->pte, vmf->orig_pte))) {
			/*
			 * Other thread has already handled the fault
			 * and we don't need to do anything. If it's
			 * not the case, the fault will be triggered
			 * again on the same address.
			 */
			ret = false;
			goto pte_unlock;
		}

		entry = pte_mkyoung(vmf->orig_pte);
		if (ptep_set_access_flags(vma, addr, vmf->pte, entry, 0))
			update_mmu_cache(vma, addr, vmf->pte);
	}

	/*
	 * This really shouldn't fail, because the page is there
	 * in the page tables. But it might just be unreadable,
	 * in which case we just give up and fill the result with
	 * zeroes.
	 */
	if (__copy_from_user_inatomic(kaddr, uaddr, PAGE_SIZE)) {
		if (locked)
			goto warn;

		/* Re-validate under PTL if the page is still mapped */
		vmf->pte = pte_offset_map_lock(mm, vmf->pmd, addr, &vmf->ptl);
		locked = true;
		if (!likely(pte_same(*vmf->pte, vmf->orig_pte))) {
			/* The PTE changed under us. Retry page fault. */
			ret = false;
			goto pte_unlock;
		}

		/*
		 * The same page can be mapped back since last copy attampt.
		 * Try to copy again under PTL.
		 */
		if (__copy_from_user_inatomic(kaddr, uaddr, PAGE_SIZE)) {
			/*
			 * Give a warn in case there can be some obscure
			 * use-case
			 */
warn:
			WARN_ON_ONCE(1);
			clear_page(kaddr);
		}
	}

	ret = true;

pte_unlock:
	if (locked)
		pte_unmap_unlock(vmf->pte, vmf->ptl);
	kunmap_atomic(kaddr);
	flush_dcache_page(dst);

	return ret;
}

static gfp_t __get_fault_gfp_mask(struct vm_area_struct *vma)
{
	struct file *vm_file = vma->vm_file;

	if (vm_file)
		return mapping_gfp_mask(vm_file->f_mapping) | __GFP_FS | __GFP_IO;

	/*
	 * Special mappings (e.g. VDSO) do not have any file so fake
	 * a default GFP_KERNEL for them.
	 */
	return GFP_KERNEL;
}

/*
 * Notify the address space that the page is about to become writable so that
 * it can prohibit this or wait for the page to get into an appropriate state.
 *
 * We do this without the lock held, so that it can sleep if it needs to.
 */
static vm_fault_t do_page_mkwrite(struct vm_fault *vmf)
{
	vm_fault_t ret;
	struct page *page = vmf->page;
	unsigned int old_flags = vmf->flags;

	vmf->flags = FAULT_FLAG_WRITE|FAULT_FLAG_MKWRITE;

	if (vmf->vma->vm_file &&
	    IS_SWAPFILE(vmf->vma->vm_file->f_mapping->host))
		return VM_FAULT_SIGBUS;

	ret = vmf->vma->vm_ops->page_mkwrite(vmf);
	/* Restore original flags so that caller is not surprised */
	vmf->flags = old_flags;
	if (unlikely(ret & (VM_FAULT_ERROR | VM_FAULT_NOPAGE)))
		return ret;
	if (unlikely(!(ret & VM_FAULT_LOCKED))) {
		lock_page(page);
		if (!page->mapping) {
			unlock_page(page);
			return 0; /* retry */
		}
		ret |= VM_FAULT_LOCKED;
	} else
		VM_BUG_ON_PAGE(!PageLocked(page), page);
	return ret;
}

/*
 * Handle dirtying of a page in shared file mapping on a write fault.
 *
 * The function expects the page to be locked and unlocks it.
 */
static vm_fault_t fault_dirty_shared_page(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	struct address_space *mapping;
	struct page *page = vmf->page;
	bool dirtied;
	bool page_mkwrite = vma->vm_ops && vma->vm_ops->page_mkwrite;

	dirtied = set_page_dirty(page);
	VM_BUG_ON_PAGE(PageAnon(page), page);
	/*
	 * Take a local copy of the address_space - page.mapping may be zeroed
	 * by truncate after unlock_page().   The address_space itself remains
	 * pinned by vma->vm_file's reference.  We rely on unlock_page()'s
	 * release semantics to prevent the compiler from undoing this copying.
	 */
	mapping = page_rmapping(page);
	unlock_page(page);

	if (!page_mkwrite)
		file_update_time(vma->vm_file);

	/*
	 * Throttle page dirtying rate down to writeback speed.
	 *
	 * mapping may be NULL here because some device drivers do not
	 * set page.mapping but still dirty their pages
	 *
	 * Drop the mmap_sem before waiting on IO, if we can. The file
	 * is pinning the mapping, as per above.
	 */
	if ((dirtied || page_mkwrite) && mapping) {
		struct file *fpin;

		fpin = maybe_unlock_mmap_for_io(vmf, NULL);
		balance_dirty_pages_ratelimited(mapping);
		if (fpin) {
			fput(fpin);
			return VM_FAULT_RETRY;
		}
	}

	return 0;
}

/*
 * Handle write page faults for pages that can be reused in the current vma
 *
 * This can happen either due to the mapping being with the VM_SHARED flag,
 * or due to us being the last reference standing to the page. In either
 * case, all we need to do here is to mark the page as writable and update
 * any related book-keeping.
 */
static inline void wp_page_reuse(struct vm_fault *vmf)
	__releases(vmf->ptl)
{
	struct vm_area_struct *vma = vmf->vma;
	struct page *page = vmf->page;
	pte_t entry;
	/*
	 * Clear the pages cpupid information as the existing
	 * information potentially belongs to a now completely
	 * unrelated process.
	 */
	if (page)
		page_cpupid_xchg_last(page, (1 << LAST_CPUPID_SHIFT) - 1);

	flush_cache_page(vma, vmf->address, pte_pfn(vmf->orig_pte));
	entry = pte_mkyoung(vmf->orig_pte);
	entry = maybe_mkwrite(pte_mkdirty(entry), vmf->vma_flags);
	if (ptep_set_access_flags(vma, vmf->address, vmf->pte, entry, 1))
		update_mmu_cache(vma, vmf->address, vmf->pte);
	pte_unmap_unlock(vmf->pte, vmf->ptl);
}

/*
 * Handle the case of a page which we actually need to copy to a new page.
 *
 * Called with mmap_sem locked and the old page referenced, but
 * without the ptl held.
 *
 * High level logic flow:
 *
 * - Allocate a page, copy the content of the old page to the new one.
 * - Handle book keeping and accounting - cgroups, mmu-notifiers, etc.
 * - Take the PTL. If the pte changed, bail out and release the allocated page
 * - If the pte is still the way we remember it, update the page table and all
 *   relevant references. This includes dropping the reference the page-table
 *   held to the old page, as well as updating the rmap.
 * - In any case, unlock the PTL and drop the reference we took to the old page.
 */
static vm_fault_t wp_page_copy(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	struct mm_struct *mm = vma->vm_mm;
	struct page *old_page = vmf->page;
	struct page *new_page = NULL;
	pte_t entry;
	int page_copied = 0;
	struct mem_cgroup *memcg;
	struct mmu_notifier_range range;
	int ret = VM_FAULT_OOM;
#ifdef CONFIG_CONT_PTE_HUGEPAGE
	unsigned long haddr = vmf->address & HPAGE_CONT_PTE_MASK;
	struct page *basepages[HPAGE_CONT_PTE_NR] = {NULL, };
	int i;
#endif
	if (unlikely(anon_vma_prepare(vma)))
		goto out;

	if (is_zero_pfn(pte_pfn(vmf->orig_pte))) {
		new_page = alloc_zeroed_user_highpage_movable(vma,
							      vmf->address);
		if (!new_page)
			goto out;
#ifdef CONFIG_CONT_PTE_HUGEPAGE
		CHP_BUG_ON(PageCont(new_page));
		CHP_BUG_ON(PageContRefill(new_page));
#endif
	} else {
#ifdef CONFIG_CONT_PTE_HUGEPAGE
		if (pte_cont(vmf->orig_pte)) {
			gfp_t gfp_mask = (GFP_TRANSHUGE_LIGHT | __GFP_KSWAPD_RECLAIM) & ~__GFP_MOVABLE & ~__GFP_COMP;
			old_page = compound_head(old_page);

			if (is_huge_zero_page(old_page))
				gfp_mask |= __GFP_ZERO;
			new_page = alloc_cont_pte_hugepage(gfp_mask);
			if (new_page) {
				for (i = 0; i < HPAGE_CONT_PTE_NR; i++)
					SetPageCont(&new_page[i]);
				prep_compound_page(new_page, HPAGE_CONT_PTE_ORDER);
				prep_transhuge_page(new_page);
				if (is_huge_zero_page(old_page))
					goto no_copy;
			} else {
				int nr_populated;
				/* copy huge page to 16 basepages */
				nr_populated = alloc_pages_bulk_array(GFP_HIGHUSER_MOVABLE, HPAGE_CONT_PTE_NR, basepages);

				/* when bulk alloc failed, continue alloc_page_vma */
				if (nr_populated != HPAGE_CONT_PTE_NR) {
					for (i = nr_populated; i < HPAGE_CONT_PTE_NR; i++) {
						basepages[i] = alloc_page_vma(GFP_HIGHUSER_MOVABLE, vma, vmf->address);
						if (!basepages[i]) {
							if (i) {
								for (i--; i >= 0; i--)
									put_page(basepages[i]);
							}

							goto out;
						}
					}
				}
			}
		} else
#endif
		new_page = alloc_page_vma(GFP_HIGHUSER_MOVABLE, vma,
						vmf->address);
#ifdef CONFIG_CONT_PTE_HUGEPAGE
		if (!new_page && !basepages[0])
			goto out;
		if (basepages[0]) {
			for (i = 0; i < HPAGE_CONT_PTE_NR; i++) {
				copy_user_highpage(basepages[i], old_page + i, haddr + i * PAGE_SIZE, vmf->vma);
				if (mem_cgroup_try_charge_delay(basepages[i], mm, GFP_KERNEL, &memcg, false)) {
					if (i) {
						for (i--; i >= 0; i--)
							mem_cgroup_cancel_charge(basepages[i], memcg, false);
					}
					goto out_free_new;
				}
				__SetPageUptodate(basepages[i]);
			}
			goto copy_done;
		}
#else
		if (!new_page)
			goto out;
#endif
		if (!cow_user_page(new_page, old_page, vmf)) {
			/*
			 * COW failed, if the fault was solved by other,
			 * it's fine. If not, userspace would re-fault on
			 * the same address and we will handle the fault
			 * from the second attempt.
			 */
			put_page(new_page);
			if (old_page)
				put_page(old_page);
			return 0;
		}
	}

#ifdef CONFIG_CONT_PTE_HUGEPAGE
no_copy:
	if (mem_cgroup_try_charge_delay(new_page, mm, GFP_KERNEL, &memcg, ContPteHugePage(new_page)))
#else
	if (mem_cgroup_try_charge_delay(new_page, mm, GFP_KERNEL, &memcg, false))
#endif
		goto out_free_new;
	__SetPageUptodate(new_page);
#ifdef CONFIG_CONT_PTE_HUGEPAGE
copy_done:
	if (pte_cont(vmf->orig_pte))
		mmu_notifier_range_init(&range, MMU_NOTIFY_CLEAR, 0, vma, mm,
					haddr, haddr + HPAGE_CONT_PTE_SIZE);
	else
#endif
		mmu_notifier_range_init(&range, MMU_NOTIFY_CLEAR, 0, vma, mm,
					vmf->address & PAGE_MASK,
					(vmf->address & PAGE_MASK) + PAGE_SIZE);
	mmu_notifier_invalidate_range_start(&range);

	/*
	 * Re-check the pte - we dropped the lock
	 */
	if (!pte_map_lock(vmf)) {
		ret = VM_FAULT_RETRY;
		goto out_uncharge;
	}
#ifdef CONFIG_CONT_PTE_HUGEPAGE
#if CONFIG_CHP_ABMORMAL_PTES_DEBUG
	{volatile bool __maybe_unused x = cont_pte_trans_huge(vmf->pte, CORRUPT_CONT_PTE_REASON_WP_PAGE_CP1);}
#endif
	if (likely(pte_same(*vmf->pte, vmf->orig_pte) && !pte_cont(vmf->orig_pte)) ||
		   (pte_cont(vmf->orig_pte) && cont_pte_readonly(vmf))) {
#else
	if (likely(pte_same(*vmf->pte, vmf->orig_pte))) {
#endif
		if (old_page) {
			if (!PageAnon(old_page)) {
#ifdef CONFIG_CONT_PTE_HUGEPAGE
					if (!pte_cont(vmf->orig_pte)) {
#endif
						dec_mm_counter_fast(mm,
								mm_counter_file(old_page));
						inc_mm_counter_fast(mm, MM_ANONPAGES);
#ifdef CONFIG_CONT_PTE_HUGEPAGE
					} else {
						/* FIXME: file hugepage cow?*/
						CHP_BUG_ON(!is_huge_zero_page(old_page));

						if (!is_huge_zero_page(old_page))
							add_mm_counter_fast(mm, mm_counter_file(old_page), -HPAGE_CONT_PTE_NR);
						add_mm_counter_fast(mm, MM_ANONPAGES, HPAGE_CONT_PTE_NR);
					}
#endif
			}
		} else {
#ifdef CONFIG_CONT_PTE_HUGEPAGE
			/* cont pte should be only on data */
			CHP_BUG_ON(pte_cont(vmf->orig_pte));
#endif
			inc_mm_counter_fast(mm, MM_ANONPAGES);
		}

#ifdef CONFIG_CONT_PTE_HUGEPAGE
		if (pte_cont(vmf->orig_pte)) {
			pte_t *ptep = vmf->pte - (vmf->address - haddr)/PAGE_SIZE;
#if CONFIG_CHP_ABMORMAL_PTES_DEBUG
			{volatile bool __maybe_unused x = cont_pte_trans_huge(vmf->pte, CORRUPT_CONT_PTE_REASON_WP_PAGE_CP2);}
#endif
			for (i = 0; i < HPAGE_CONT_PTE_NR; i++) {
				flush_cache_page(vma, haddr + PAGE_SIZE * i,
					pte_pfn(vmf->orig_pte) - (vmf->address - haddr)/PAGE_SIZE + i);
			}
			if (new_page) {
				entry = mk_pte(new_page, vmf->vma_page_prot);
				entry = maybe_mkwrite(pte_mkdirty(entry), vmf->vma_flags);
				entry = pte_mkcont(entry);
				cont_pte_set_huge_pte_at(vma->vm_mm, haddr, ptep, entry);
				page_add_new_anon_rmap(new_page, vma, haddr, true);
				mem_cgroup_commit_charge(new_page, memcg, false, true);
				__lru_cache_add_active_or_unevictable(new_page, vmf->vma_flags);
				atomic64_inc(&thp_cow);
			} else {
				for (i = 0; i < HPAGE_CONT_PTE_NR; i++) {
					entry = mk_pte(basepages[i], vmf->vma_page_prot);
					entry = maybe_mkwrite(pte_mkdirty(entry), vmf->vma_flags);
					ptep_clear_flush_notify(vma, haddr + PAGE_SIZE * i, ptep + i);
					__page_add_new_anon_rmap(basepages[i], vma, haddr + PAGE_SIZE * i, false);
					mem_cgroup_commit_charge(basepages[i], memcg, false, false);
					__lru_cache_add_active_or_unevictable(basepages[i], vmf->vma_flags);
					set_pte_at_notify(mm, haddr + PAGE_SIZE * i, ptep + i, entry);
				}
				atomic64_inc(&thp_cow_fallback);
			}
			goto done_pte_update;
		}
#endif

		flush_cache_page(vma, vmf->address, pte_pfn(vmf->orig_pte));
		entry = mk_pte(new_page, vmf->vma_page_prot);
		entry = maybe_mkwrite(pte_mkdirty(entry), vmf->vma_flags);
		/*
		 * Clear the pte entry and flush it first, before updating the
		 * pte with the new entry. This will avoid a race condition
		 * seen in the presence of one thread doing SMC and another
		 * thread doing COW.
		 */
		ptep_clear_flush_notify(vma, vmf->address, vmf->pte);
		__page_add_new_anon_rmap(new_page, vma, vmf->address, false);
		mem_cgroup_commit_charge(new_page, memcg, false, false);
		__lru_cache_add_active_or_unevictable(new_page, vmf->vma_flags);
		/*
		 * We call the notify macro here because, when using secondary
		 * mmu page tables (such as kvm shadow page tables), we want the
		 * new page to be mapped directly into the secondary page table.
		 */
		set_pte_at_notify(mm, vmf->address, vmf->pte, entry);
		update_mmu_cache(vma, vmf->address, vmf->pte);
#ifdef CONFIG_CONT_PTE_HUGEPAGE
done_pte_update:
#endif
		if (old_page && !is_huge_zero_page(old_page)) {
			/*
			 * Only after switching the pte to the new page may
			 * we remove the mapcount here. Otherwise another
			 * process may come and find the rmap count decremented
			 * before the pte is switched to the new page, and
			 * "reuse" the old page writing into it while our pte
			 * here still points into it and can be read by other
			 * threads.
			 *
			 * The critical issue is to order this
			 * page_remove_rmap with the ptp_clear_flush above.
			 * Those stores are ordered by (if nothing else,)
			 * the barrier present in the atomic_add_negative
			 * in page_remove_rmap.
			 *
			 * Then the TLB flush in ptep_clear_flush ensures that
			 * no process can access the old page before the
			 * decremented mapcount is visible. And the old page
			 * cannot be reused until after the decremented
			 * mapcount is visible. So transitively, TLBs to
			 * old page will be flushed before it can be reused.
			 */
			page_remove_rmap(old_page, pte_cont(vmf->orig_pte));
		}

		/* Free the old page.. */
		new_page = old_page;
		page_copied = 1;
	} else {
		/* eg: !pte_same */
#ifdef CONFIG_CONT_PTE_HUGEPAGE
		if (basepages[0]) {
			for (i = 0; i < HPAGE_CONT_PTE_NR; i++)
				mem_cgroup_cancel_charge(basepages[i], memcg, false);
		} else
			mem_cgroup_cancel_charge(new_page, memcg, ContPteHugePage(new_page));
#else

		mem_cgroup_cancel_charge(new_page, memcg, false);
#endif
	}
	/*
	 * 1.if cow, put old_page.
	 * 2.if !pte_same, put alloc new_page.
	*/
	if (new_page)
		put_page(new_page);

#ifdef CONFIG_CONT_PTE_HUGEPAGE
	/*
	 * if !pte_same and fallback alloc,
	 * put HPAGE_CONT_PTE_NR basepages.
	 */
	if (!page_copied && basepages[0]) {
		for (i = 0; i < HPAGE_CONT_PTE_NR; i++)
			put_page(basepages[i]);
	}
#endif

	pte_unmap_unlock(vmf->pte, vmf->ptl);
	/*
	 * No need to double call mmu_notifier->invalidate_range() callback as
	 * the above ptep_clear_flush_notify() did already call it.
	 */
	mmu_notifier_invalidate_range_only_end(&range);
	if (old_page && !is_huge_zero_page(old_page)) {
		/*
		 * Don't let another task, with possibly unlocked vma,
		 * keep the mlocked page.
		 */
		if (page_copied && (vmf->vma_flags & VM_LOCKED)) {
			lock_page(old_page);	/* LRU manipulation */
			if (PageMlocked(old_page))
				munlock_vma_page(old_page);
			unlock_page(old_page);
		}
		put_page(old_page);
	}
	return page_copied ? VM_FAULT_WRITE : 0;
out_uncharge:
#ifdef CONFIG_CONT_PTE_HUGEPAGE
	if (basepages[0]) {
		for (i = 0; i < HPAGE_CONT_PTE_NR; i++)
			mem_cgroup_cancel_charge(basepages[i], memcg, false);
	} else
		mem_cgroup_cancel_charge(new_page, memcg, ContPteHugePage(new_page));
#else
		mem_cgroup_cancel_charge(new_page, memcg, false);
#endif
out_free_new:
#ifdef CONFIG_CONT_PTE_HUGEPAGE
	if (basepages[0]) {
		for (i = 0; i < HPAGE_CONT_PTE_NR; i++)
			put_page(basepages[i]);
		goto out;
	}
#endif
	put_page(new_page);
out:
	if (old_page)
		put_page(old_page);
	return ret;
}

/**
 * finish_mkwrite_fault - finish page fault for a shared mapping, making PTE
 *			  writeable once the page is prepared
 *
 * @vmf: structure describing the fault
 *
 * This function handles all that is needed to finish a write page fault in a
 * shared mapping due to PTE being read-only once the mapped page is prepared.
 * It handles locking of PTE and modifying it.
 *
 * The function expects the page to be locked or other protection against
 * concurrent faults / writeback (such as DAX radix tree locks).
 *
 * Return: %VM_FAULT_WRITE on success, %0 when PTE got changed before
 * we acquired PTE lock.
 */
vm_fault_t finish_mkwrite_fault(struct vm_fault *vmf)
{
	WARN_ON_ONCE(!(vmf->vma_flags & VM_SHARED));
	if (!pte_map_lock(vmf))
		return VM_FAULT_RETRY;
	/*
	 * We might have raced with another page fault while we released the
	 * pte_offset_map_lock.
	 */
	if (!pte_same(*vmf->pte, vmf->orig_pte)) {
		pte_unmap_unlock(vmf->pte, vmf->ptl);
		return VM_FAULT_NOPAGE;
	}
	wp_page_reuse(vmf);
	return 0;
}

/*
 * Handle write page faults for VM_MIXEDMAP or VM_PFNMAP for a VM_SHARED
 * mapping
 */
static vm_fault_t wp_pfn_shared(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;

	if (vma->vm_ops && vma->vm_ops->pfn_mkwrite) {
		vm_fault_t ret;

		pte_unmap_unlock(vmf->pte, vmf->ptl);
		vmf->flags |= FAULT_FLAG_MKWRITE;
		ret = vma->vm_ops->pfn_mkwrite(vmf);
		if (ret & (VM_FAULT_ERROR | VM_FAULT_NOPAGE))
			return ret;
		return finish_mkwrite_fault(vmf);
	}
	wp_page_reuse(vmf);
	return VM_FAULT_WRITE;
}

static vm_fault_t wp_page_shared(struct vm_fault *vmf)
	__releases(vmf->ptl)
{
	struct vm_area_struct *vma = vmf->vma;
	vm_fault_t ret = VM_FAULT_WRITE;

	get_page(vmf->page);

	if (vma->vm_ops && vma->vm_ops->page_mkwrite) {
		vm_fault_t tmp;

		pte_unmap_unlock(vmf->pte, vmf->ptl);
		tmp = do_page_mkwrite(vmf);
		if (unlikely(!tmp || (tmp &
				      (VM_FAULT_ERROR | VM_FAULT_NOPAGE)))) {
			put_page(vmf->page);
			return tmp;
		}
		tmp = finish_mkwrite_fault(vmf);
		if (unlikely(tmp & (VM_FAULT_ERROR | VM_FAULT_NOPAGE))) {
			unlock_page(vmf->page);
			put_page(vmf->page);
			return tmp;
		}
	} else {
		wp_page_reuse(vmf);
		lock_page(vmf->page);
	}
	ret |= fault_dirty_shared_page(vmf);
	put_page(vmf->page);

	return ret;
}
#ifdef CONFIG_CONT_PTE_HUGEPAGE
static bool wp_cont_pte_hugepage_reuse(struct vm_fault *vmf)
{
	struct page *head = compound_head(vmf->page);
	unsigned long haddr = vmf->address & HPAGE_CONT_PTE_MASK;
	pte_t *ptep = vmf->pte - (vmf->address - haddr)/PAGE_SIZE;

	WARN_ON(PageKsm(head));

	if (!trylock_page(head)) {
		get_page(head);
		spin_unlock(vmf->ptl);
		lock_page(head);
		spin_lock(vmf->ptl);
		if (unlikely(!pte_same(*vmf->pte, vmf->orig_pte)) || !cont_pte_readonly(vmf)) {
			/*
			 * don't spin_unlock, we'll do it
			 * in the return.
			*/
			unlock_page(head);
			put_page(head);
			return false;
		}
		put_page(head);
	}

	/* make sure we are the last reference of the whole page and its part */
	if (cont_pte_readonly(vmf) && reuse_swap_cont_pte_page(head, NULL)) {
		pte_t entry;

		entry = pte_mkyoung(READ_ONCE(*ptep));
		entry = maybe_mkwrite(pte_mkdirty(entry), vmf->vma_flags);
		cont_pte_huge_ptep_get_and_clear(vmf->vma->vm_mm, haddr, ptep);
		cont_pte_set_huge_pte_at(vmf->vma->vm_mm, haddr, ptep, entry);

		unlock_page(head);
		pte_unmap_unlock(vmf->pte, vmf->ptl);
		return true;
	}

	/*
	 * don't spin_unlock, we'll do it in the return.
	 */
	unlock_page(head);
	//count_vm_event(PGREUSE);

	return false;
}
#endif

/*
 * This routine handles present pages, when users try to write
 * to a shared page. It is done by copying the page to a new address
 * and decrementing the shared-page counter for the old page.
 *
 * Note that this routine assumes that the protection checks have been
 * done by the caller (the low-level page fault routine in most cases).
 * Thus we can safely just mark it writable once we've done any necessary
 * COW.
 *
 * We also mark the page dirty at this point even though the page will
 * change only once the write actually happens. This avoids a few races,
 * and potentially makes it more efficient.
 *
 * We enter with non-exclusive mmap_sem (to exclude vma changes,
 * but allow concurrent faults), with pte both mapped and locked.
 * We return with mmap_sem still held, but pte unmapped and unlocked.
 */
static vm_fault_t do_wp_page(struct vm_fault *vmf)
	__releases(vmf->ptl)
{
	struct vm_area_struct *vma = vmf->vma;

	vmf->page = _vm_normal_page(vma, vmf->address, vmf->orig_pte,
					vmf->vma_flags);
	if (!vmf->page) {
		/*
		 * VM_MIXEDMAP !pfn_valid() case, or VM_SOFTDIRTY clear on a
		 * VM_PFNMAP VMA.
		 *
		 * We should not cow pages in a shared writeable mapping.
		 * Just mark the pages writable and/or call ops->pfn_mkwrite.
		 */
		if ((vmf->vma_flags & (VM_WRITE|VM_SHARED)) ==
				     (VM_WRITE|VM_SHARED))
			return wp_pfn_shared(vmf);

		pte_unmap_unlock(vmf->pte, vmf->ptl);
		return wp_page_copy(vmf);
	}

	/*
	 * Take out anonymous pages first, anonymous shared vmas are
	 * not dirty accountable.
	 */
	if (PageAnon(vmf->page)) {
		struct page *page = vmf->page;
#ifdef CONFIG_CONT_PTE_HUGEPAGE
		if (ContPteHugePage(page) && pte_cont(vmf->orig_pte)) {
#if CONFIG_CHP_ABMORMAL_PTES_DEBUG
			{volatile bool __maybe_unused x = cont_pte_trans_huge(vmf->pte, CORRUPT_CONT_PTE_REASON_DO_WP_PAGE);}
#endif
			if (!wp_cont_pte_hugepage_reuse(vmf)) {
				bool _pte_same = pte_same(*vmf->pte, vmf->orig_pte);
				bool _cont_pte_readonly = cont_pte_readonly(vmf);
				bool _page_count = page_count(page);

				if (unlikely(!_pte_same || !_cont_pte_readonly)) {
					pte_unmap_unlock(vmf->pte, vmf->ptl);

					atomic64_inc(&perf_stat.wp_reuse_fail_count[WP_REUSE_FAIL_TOTAL]);
					if (!_pte_same)
						atomic64_inc(&perf_stat.wp_reuse_fail_count[PTE_NO_SAME]);
					if (!_cont_pte_readonly)
						atomic64_inc(&perf_stat.wp_reuse_fail_count[PTE_NO_READONLY]);
					if (!_page_count)
						atomic64_inc(&perf_stat.wp_reuse_fail_count[ZERO_REF_COUNT]);

					pr_err_ratelimited("@%s:%d comm:%s pid:%d page:%lx ContPteHugePage:%d "
							   "within_cont_pte_cma:%d pte_same:%d cont_pte_readonly:%d @\n",
								__func__, __LINE__, current->comm, current->pid,
								(unsigned long)page, ContPteHugePage(page),
								within_cont_pte_cma(page_to_pfn(page)),
								_pte_same, _cont_pte_readonly);
					return 0;
				} else
					goto copy;
			}
			return VM_FAULT_WRITE;
		}
#endif

		/* PageKsm() doesn't necessarily raise the page refcount */
		if (PageKsm(page) || page_count(page) != 1)
			goto copy;
		if (!trylock_page(page))
			goto copy;
		if (PageKsm(page) || page_mapcount(page) != 1 || page_count(page) != 1) {
			unlock_page(page);
			goto copy;
		}
		/*
		 * Ok, we've got the only map reference, and the only
		 * page count reference, and the page is locked,
		 * it's dark out, and we're wearing sunglasses. Hit it.
		 */
		unlock_page(page);
		wp_page_reuse(vmf);
		return VM_FAULT_WRITE;
	} else if (unlikely((vmf->vma_flags & (VM_WRITE|VM_SHARED)) ==
					(VM_WRITE|VM_SHARED))) {
		return wp_page_shared(vmf);
	}
copy:
	/*
	 * Ok, we need to copy. Oh, well..
	 */
#ifdef CONFIG_CONT_PTE_HUGEPAGE
		CHP_BUG_ON(!page_count(vmf->page));
#endif
	get_page(vmf->page);

	pte_unmap_unlock(vmf->pte, vmf->ptl);
	return wp_page_copy(vmf);
}

static void unmap_mapping_range_vma(struct vm_area_struct *vma,
		unsigned long start_addr, unsigned long end_addr,
		struct zap_details *details)
{
	zap_page_range_single(vma, start_addr, end_addr - start_addr, details);
}

static inline void unmap_mapping_range_tree(struct rb_root_cached *root,
					    struct zap_details *details)
{
	struct vm_area_struct *vma;
	pgoff_t vba, vea, zba, zea;

	vma_interval_tree_foreach(vma, root,
			details->first_index, details->last_index) {

		vba = vma->vm_pgoff;
		vea = vba + vma_pages(vma) - 1;
		zba = details->first_index;
		if (zba < vba)
			zba = vba;
		zea = details->last_index;
		if (zea > vea)
			zea = vea;

		unmap_mapping_range_vma(vma,
			((zba - vba) << PAGE_SHIFT) + vma->vm_start,
			((zea - vba + 1) << PAGE_SHIFT) + vma->vm_start,
				details);
	}
}

/**
 * unmap_mapping_page() - Unmap single page from processes.
 * @page: The locked page to be unmapped.
 *
 * Unmap this page from any userspace process which still has it mmaped.
 * Typically, for efficiency, the range of nearby pages has already been
 * unmapped by unmap_mapping_pages() or unmap_mapping_range().  But once
 * truncation or invalidation holds the lock on a page, it may find that
 * the page has been remapped again: and then uses unmap_mapping_page()
 * to unmap it finally.
 */
void unmap_mapping_page(struct page *page)
{
	struct address_space *mapping = page->mapping;
	struct zap_details details = { };

	VM_BUG_ON(!PageLocked(page));
	VM_BUG_ON(PageTail(page));

	details.check_mapping = mapping;
	details.first_index = page->index;
	details.last_index = page->index + hpage_nr_pages(page) - 1;
	details.single_page = page;

	i_mmap_lock_write(mapping);
	if (unlikely(!RB_EMPTY_ROOT(&mapping->i_mmap.rb_root)))
		unmap_mapping_range_tree(&mapping->i_mmap, &details);
	i_mmap_unlock_write(mapping);
}

/**
 * unmap_mapping_pages() - Unmap pages from processes.
 * @mapping: The address space containing pages to be unmapped.
 * @start: Index of first page to be unmapped.
 * @nr: Number of pages to be unmapped.  0 to unmap to end of file.
 * @even_cows: Whether to unmap even private COWed pages.
 *
 * Unmap the pages in this address space from any userspace process which
 * has them mmaped.  Generally, you want to remove COWed pages as well when
 * a file is being truncated, but not when invalidating pages from the page
 * cache.
 */
void unmap_mapping_pages(struct address_space *mapping, pgoff_t start,
		pgoff_t nr, bool even_cows)
{
	struct zap_details details = { };

	details.check_mapping = even_cows ? NULL : mapping;
	details.first_index = start;
	details.last_index = start + nr - 1;
	if (details.last_index < details.first_index)
		details.last_index = ULONG_MAX;

	i_mmap_lock_write(mapping);
	if (unlikely(!RB_EMPTY_ROOT(&mapping->i_mmap.rb_root)))
		unmap_mapping_range_tree(&mapping->i_mmap, &details);
	i_mmap_unlock_write(mapping);
}

/**
 * unmap_mapping_range - unmap the portion of all mmaps in the specified
 * address_space corresponding to the specified byte range in the underlying
 * file.
 *
 * @mapping: the address space containing mmaps to be unmapped.
 * @holebegin: byte in first page to unmap, relative to the start of
 * the underlying file.  This will be rounded down to a PAGE_SIZE
 * boundary.  Note that this is different from truncate_pagecache(), which
 * must keep the partial page.  In contrast, we must get rid of
 * partial pages.
 * @holelen: size of prospective hole in bytes.  This will be rounded
 * up to a PAGE_SIZE boundary.  A holelen of zero truncates to the
 * end of the file.
 * @even_cows: 1 when truncating a file, unmap even private COWed pages;
 * but 0 when invalidating pagecache, don't throw away private data.
 */
void unmap_mapping_range(struct address_space *mapping,
		loff_t const holebegin, loff_t const holelen, int even_cows)
{
	pgoff_t hba = holebegin >> PAGE_SHIFT;
	pgoff_t hlen = (holelen + PAGE_SIZE - 1) >> PAGE_SHIFT;

	/* Check for overflow. */
	if (sizeof(holelen) > sizeof(hlen)) {
		long long holeend =
			(holebegin + holelen + PAGE_SIZE - 1) >> PAGE_SHIFT;
		if (holeend & ~(long long)ULONG_MAX)
			hlen = ULONG_MAX - hba + 1;
	}

	unmap_mapping_pages(mapping, hba, hlen, even_cows);
}
EXPORT_SYMBOL(unmap_mapping_range);
#if defined(CONFIG_CONT_PTE_HUGEPAGE)

extern int free_swap_slot(swp_entry_t entry);

static inline struct swap_cluster_info *lock_cluster(struct swap_info_struct *si,
						     unsigned long offset)
{
	struct swap_cluster_info *ci;

	ci = si->cluster_info;
	if (ci) {
		ci += offset / HPAGE_CONT_PTE_NR;
		spin_lock(&ci->lock);
	}
	return ci;
}

static inline void unlock_cluster(struct swap_cluster_info *ci)
{
	if (ci)
		spin_unlock(&ci->lock);
}

static inline void __set_cluster_doublemap(struct swap_cluster_info *ci)
{
	if (!(ci->flags & (CLUSTER_FLAG_DOUBLE_MAP | CLUSTER_FLAG_FREE))) {
		ci->flags |= CLUSTER_FLAG_DOUBLE_MAP;
		swap_cluster_double_mapped++;
	}
}

static inline void set_cluster_doublemap(swp_entry_t fault_entry)
{
	struct swap_info_struct *si = swp_swap_info(fault_entry);
	unsigned long offset = swp_offset(fault_entry);
	struct swap_cluster_info *ci;

	CHP_BUG_ON(!is_thp_swap(si));

	ci = lock_cluster(si, offset);
	__set_cluster_doublemap(ci);
	if (ci->flags & CLUSTER_FLAG_FREE)
		pr_err("@@@FIXME%s-%d %s-%d swapin for free cluster, offs:%lx\n",
			__func__, __LINE__, current->comm, current->pid, offset);
	unlock_cluster(ci);
}

static inline bool is_cont_pte_swap(swp_entry_t fault_entry, pte_t *ptep, swp_entry_t *entries,
				    struct vm_fault *vmf, bool *pte_changed)
{
	int i;
	struct swap_info_struct *si = swp_swap_info(fault_entry);
	unsigned long offset = swp_offset(fault_entry);
	struct swap_cluster_info *ci;
	bool ret = true;

	if (vmf && pte_changed)
		*pte_changed = !pte_same(*vmf->pte, vmf->orig_pte);

	if (!is_thp_swap(si) || (pte_changed && *pte_changed))
		return false;

	ci = lock_cluster(si, offset);

	if (ci->flags & CLUSTER_FLAG_DOUBLE_MAP) {
		ret = false;
		goto out;
	}

	for (i = 0; i < HPAGE_CONT_PTE_NR; i++) {
		pte_t pte = READ_ONCE(*(ptep + i));
		if (pte_none(pte) || pte_present(pte)) {
			ret = false;
			goto out;
		}

		entries[i] = pte_to_swp_entry(pte);
		if (unlikely(non_swap_entry(entries[i]))) {
			ret = false;
			goto out;
		}
		if (!is_thp_swap(swp_swap_info(entries[i]))) {
			ret = false;
			pr_err("@@@@%s-%d current:%s-%d i:%d non-thp-swap\n",
					__func__, __LINE__, current->comm, current->pid, i);
			goto out;
		}
		if ((swp_offset(entries[i]) % HPAGE_CONT_PTE_NR) != i) {
			ret = false;
			pr_err("@@@@%s-%d current:%s-%d-%d i:%d non-aligned-offset:%lx val:%lx prev-val:%lx ptep:%lx ptes:%lx-%lx-%lx-%lx-%lx-%lx-%lx-%lx-%lx-%lx-%lx-%lx-%lx-%lx-%lx-%lx\n",
					__func__, __LINE__, current->comm, current->pid, current->tgid, i, swp_offset(entries[i]), entries[i].val,
					i > 0 ? entries[i - 1].val : -1UL,
					(unsigned long)ptep,
					*(ptep + 0), *(ptep + 1), *(ptep + 2), *(ptep + 3),  *(ptep + 4), *(ptep + 5), *(ptep + 6), *(ptep + 7),
					*(ptep + 8), *(ptep + 9), *(ptep + 10), *(ptep + 11),  *(ptep + 12), *(ptep + 13), *(ptep + 14), *(ptep + 15));
			goto out;
		}
	}
out:
	CHP_BUG_ON(ret && (ci->flags & CLUSTER_FLAG_FREE));
	unlock_cluster(ci);
	return ret;
}

extern bool is_swap_slot_cache_enabled(void);

static inline unsigned char swap_count(unsigned char ent)
{
	return ent & ~SWAP_HAS_CACHE;
}

static int cont_pte_swapcache_prepare(swp_entry_t *pentry)
{
	struct swap_info_struct *p;
	struct swap_cluster_info *ci;
	unsigned long offset;
	unsigned char count[HPAGE_CONT_PTE_NR];
	unsigned char has_cache[HPAGE_CONT_PTE_NR];
	int err = -EINVAL;
	int i;

	p = get_swap_device(*pentry);
	if (!p)
		goto out;

	offset = swp_offset(*pentry);

	CHP_BUG_ON(!IS_ALIGNED(offset, HPAGE_CONT_PTE_NR));

	ci = lock_cluster(p, offset);
	CHP_BUG_ON(!ci);

	for (i = 0; i < HPAGE_CONT_PTE_NR; i++) {
		count[i] = p->swap_map[offset + i];

		/*
		 * swapin_readahead() doesn't check if a swap entry is valid, so the
		 * swap entry could be SWAP_MAP_BAD. Check here with lock held.
		 */
		if (unlikely(swap_count(count[i]) == SWAP_MAP_BAD)) {
			err = -ENOENT;
			goto unlock_out;
		}

		has_cache[i] = count[i] & SWAP_HAS_CACHE;
		count[i] &= ~SWAP_HAS_CACHE;

		/* set SWAP_HAS_CACHE if there is no cache and entry is used */
		if (!has_cache[i] && count[i])
			has_cache[i] = SWAP_HAS_CACHE;
		else if (has_cache[i])		/* someone else added cache */
			has_cache[i] = EEXIST;
		else {				/* no users remaining */
			err = -ENOENT;
			pr_err_ratelimited("@%s:%d comm:%s offset:%lu swap_map[%lu]:0x%02x@ swap_map[%lu]:0x%02x@\n",
				__func__, __LINE__, current->comm, offset, offset, p->swap_map[offset], offset + i,
				p->swap_map[offset + i]);
			goto unlock_out;
		}

		if (has_cache[i] != has_cache[0]) {
			pr_err("@%s:%d comm:%s count[%d]:%d has_cache[%d]:%s has_cache[0]:%s "
				"offset:%lu swap_map[%lu]:0x%02x@ swap_map[%lu]:0x%02x@\n",
				__func__, __LINE__, current->comm, i, count[i], i,
				has_cache[i] == SWAP_HAS_CACHE ? "SWAP_HAS_CACHE" : "EEXIST",
				has_cache[0] == SWAP_HAS_CACHE ? "SWAP_HAS_CACHE" : "EEXIST",
				offset, offset, p->swap_map[offset], offset + i, p->swap_map[offset + i]);
			err = -ENOENT;
			goto unlock_out;
		}
	}

	if (has_cache[0] == SWAP_HAS_CACHE) {
		err = 0;
		for (i = 0; i < HPAGE_CONT_PTE_NR; i++)
			WRITE_ONCE(p->swap_map[offset+i], count[i] | SWAP_HAS_CACHE);
	} else {
		err = -EEXIST;
	}

unlock_out:
	unlock_cluster(ci);
out:
	if (p)
		put_swap_device(p);
	return err;
}

/* copied */
static bool swap_count_continued(struct swap_info_struct *si,
				 pgoff_t offset, unsigned char count)
{
	struct page *head;
	struct page *page;
	unsigned char *map;
	bool ret;

	head = vmalloc_to_page(si->swap_map + offset);
	if (page_private(head) != SWP_CONTINUED) {
		CHP_BUG_ON(count & COUNT_CONTINUED);
		return false;		/* need to add count continuation */
	}

	spin_lock(&si->cont_lock);
	offset &= ~PAGE_MASK;
	page = list_next_entry(head, lru);
	map = kmap_atomic(page) + offset;

	if (count == SWAP_MAP_MAX)	/* initial increment from swap_map */
		goto init_map;		/* jump over SWAP_CONT_MAX checks */

	if (count == (SWAP_MAP_MAX | COUNT_CONTINUED)) { /* incrementing */
		/*
		 * Think of how you add 1 to 999
		 */
		while (*map == (SWAP_CONT_MAX | COUNT_CONTINUED)) {
			kunmap_atomic(map);
			page = list_next_entry(page, lru);
			CHP_BUG_ON(page == head);
			map = kmap_atomic(page) + offset;
		}
		if (*map == SWAP_CONT_MAX) {
			kunmap_atomic(map);
			page = list_next_entry(page, lru);
			if (page == head) {
				ret = false;	/* add count continuation */
				goto out;
			}
			map = kmap_atomic(page) + offset;
init_map:		*map = 0;		/* we didn't zero the page */
		}
		*map += 1;
		kunmap_atomic(map);
		while ((page = list_prev_entry(page, lru)) != head) {
			map = kmap_atomic(page) + offset;
			*map = COUNT_CONTINUED;
			kunmap_atomic(map);
		}
		ret = true;			/* incremented */

	} else {				/* decrementing */
		/*
		 * Think of how you subtract 1 from 1000
		 */
		CHP_BUG_ON(count != COUNT_CONTINUED);
		while (*map == COUNT_CONTINUED) {
			kunmap_atomic(map);
			page = list_next_entry(page, lru);
			CHP_BUG_ON(page == head);
			map = kmap_atomic(page) + offset;
		}
		CHP_BUG_ON(*map == 0);
		*map -= 1;
		if (*map == 0)
			count = 0;
		kunmap_atomic(map);
		while ((page = list_prev_entry(page, lru)) != head) {
			map = kmap_atomic(page) + offset;
			*map = SWAP_CONT_MAX | count;
			count = COUNT_CONTINUED;
			kunmap_atomic(map);
		}
		ret = count == COUNT_CONTINUED;
	}
out:
	spin_unlock(&si->cont_lock);
	return ret;
}

/* copied */
static unsigned char __swap_entry_free_locked(struct swap_info_struct *p,
					      unsigned long offset,
					      unsigned char usage)
{
	unsigned char count;
	unsigned char has_cache;

	count = p->swap_map[offset];

	has_cache = count & SWAP_HAS_CACHE;
	count &= ~SWAP_HAS_CACHE;

	if (usage == SWAP_HAS_CACHE) {
		VM_BUG_ON(!has_cache);
		has_cache = 0;
	} else if (count == SWAP_MAP_SHMEM) {
		/*
		 * Or we could insist on shmem.c using a special
		 * swap_shmem_free() and free_shmem_swap_and_cache()...
		 */
		count = 0;
	} else if ((count & ~COUNT_CONTINUED) <= SWAP_MAP_MAX) {
		if (count == COUNT_CONTINUED) {
			if (swap_count_continued(p, offset, count))
				count = SWAP_MAP_MAX | COUNT_CONTINUED;
			else
				count = SWAP_MAP_MAX;
		} else
			count--;
	}

	usage = count | has_cache;
	if (usage)
		WRITE_ONCE(p->swap_map[offset], usage);
	else
		WRITE_ONCE(p->swap_map[offset], SWAP_HAS_CACHE);

	return usage;
}

static void cont_pte_swap_entry_free(struct swap_info_struct *p,
		swp_entry_t entries[])
{
	struct swap_cluster_info *ci;
	unsigned long offset = swp_offset(entries[0]);
	unsigned char usage[HPAGE_CONT_PTE_NR];
	int i;

	ci = lock_cluster(p, offset);
	for (i = 0; i < HPAGE_CONT_PTE_NR; i++)
		usage[i] = __swap_entry_free_locked(p, offset + i, 1);
	unlock_cluster(ci);

	for (i = 0; i < HPAGE_CONT_PTE_NR; i++) {
		if (!usage[i])
			free_swap_slot(entries[i]);
	}
}

static struct page *read_swap_cache_thp(swp_entry_t *pentry, gfp_t gfp_mask,
			struct vm_area_struct *vma, unsigned long addr,
			bool *new_page_allocated, int *ret)
{
	struct swap_info_struct *si;
	struct swap_cluster_info *ci;
	struct page *page;
	void *shadow = NULL;
	swp_entry_t entry = *pentry;
	unsigned long haddr = addr & HPAGE_CONT_PTE_MASK;
	unsigned long hoffset = (addr - haddr)/PAGE_SIZE;
	int i;

	*ret = RET_STATUS_OTHER_FAIL;
	*new_page_allocated = false;

	for (;;) {
		int err;
		/*
		 * First check the swap cache.  Since this is normally
		 * called after lookup_swap_cache() failed, re-calling
		 * that would confuse statistics.
		 */
		si = get_swap_device(entry);
		if (!si) {
			*ret = RET_STATUS_NO_SWP_INFO;
			return NULL;
		}
		ci = si->cluster_info;
		page = find_get_page(swap_address_space(entry),
				     swp_offset(entry) + hoffset);
		put_swap_device(si);
		if (page) {
			*ret = RET_STATUS_HIT_SWPCACHE;
			return page;
		}
		if (!ci) {
			*ret = RET_STATUS_NO_CLUSTER_INFO;
			return NULL;
		}
		/*
		 * Just skip read ahead for unused swap slot.
		 * During swap_off when swap_slot_cache is disabled,
		 * we have to handle the race between putting
		 * swap entry in swap cache and marking swap slot
		 * as SWAP_HAS_CACHE.  That's done in later part of code or
		 * else swap_off will be aborted if we return NULL.
		 */
		if (!__swp_swapcount(entry) && is_swap_slot_cache_enabled()) {
			*ret = RET_STATUS_ZERO_SWPCOUNT;
			return NULL;
		}

		/*
		 * Get a new page to read into from swap.  Allocate it now,
		 * before marking swap_map SWAP_HAS_CACHE, when -EEXIST will
		 * cause any racers to loop around until we add it to cache.
		 */
		page = alloc_cont_pte_hugepage((GFP_TRANSHUGE_LIGHT | __GFP_RECLAIM | __GFP_MEMALLOC) & ~__GFP_MOVABLE & ~__GFP_COMP);
		if (unlikely(!page)) {
			*ret = RET_STATUS_ALLOC_THP_FAIL;
			return NULL;
		}

		for (i = 0; i < HPAGE_CONT_PTE_NR; i++)
			SetPageCont(&page[i]);
		prep_compound_page(page, HPAGE_CONT_PTE_ORDER);
		prep_transhuge_page(page);

		/*
		 * Swap entry may have been freed since our caller observed it.
		 */
		err = cont_pte_swapcache_prepare(pentry);
		if (!err) {
			pgoff_t offset = swp_offset(entry);

			ci += offset / HPAGE_CONT_PTE_NR;
			spin_lock(&ci->lock);
			ci->flags |= CLUSTER_FLAG_HUGE;
			spin_unlock(&ci->lock);

			break;
		}

		__SetPageUptodate(page);
		put_page(page);
		if (err != -EEXIST) {
			*ret = RET_STATUS_SWPCACHE_RPEPARE_FAIL;
			return NULL;
		}

		/*
		 * We might race against __delete_from_swap_cache(), and
		 * stumble across a swap_map entry whose SWAP_HAS_CACHE
		 * has not yet been cleared.  Or race against another
		 * __read_swap_cache_async(), which has set SWAP_HAS_CACHE
		 * in swap_map, but not yet added its page to swap cache.
		 */
		schedule_timeout_uninterruptible(1);
	}

	/*
	 * The swap entry is ours to swap in. Prepare the new page.
	 */

	__SetPageLocked(page);
	__SetPageSwapBacked(page);

	/* May fail (-ENOMEM) if XArray node allocation failed. */
	if (add_to_swap_cache(page, entry, gfp_mask & GFP_RECLAIM_MASK)) {
		*ret = RET_STATUS_ADD_TO_SWPCACHE_FAIL;
		put_swap_page(page, entry);
		goto fail_unlock;
	}

	/*
	 * NOTE: In kernel-5.4, the memcg charge was moved below the
	 * do_swap_page->ksm_might_need_to_copy  function!
	 */

	if (shadow)
		workingset_refault(page, shadow);

	/* Caller will initiate read into locked page */
	SetPageWorkingset(page);
	lru_cache_add(page);
	*new_page_allocated = true;
	*ret = RET_STATUS_ALLOC_THP_SUCCESS;
	return page + hoffset;

fail_unlock:
	unlock_page(page);
	put_page(page);
	return NULL;
}

static int read_thp_no_swapcache_fallback(swp_entry_t *pentry,
		gfp_t gfp_mask, struct vm_fault *vmf, struct page **basepages)
{
	int i;
	int ret = 0;
	unsigned long offset;
	//void *shadow = NULL;
	struct blk_plug plug;
	struct page *page;
	swp_entry_t entry, hentry = pentry[0];
	//struct vm_area_struct *vma = vmf->vma;
	unsigned long start_offset = swp_offset(hentry);
	unsigned long end_offset = start_offset + HPAGE_CONT_PTE_NR - 1;
	unsigned long addr, haddr = vmf->address & HPAGE_CONT_PTE_MASK;
	int nr_populated;

	/* step1: alloc HPAGE_CONT_PTE_NR normal pages */
	nr_populated = alloc_pages_bulk_array(gfp_mask, HPAGE_CONT_PTE_NR, basepages);

	/* when bulk alloc failed, continue alloc_page_vma */
	if (nr_populated != HPAGE_CONT_PTE_NR) {
		for (i = nr_populated; i < HPAGE_CONT_PTE_NR; i++) {
			addr = haddr + i * PAGE_SIZE;
			basepages[i] = alloc_page_vma(gfp_mask, vmf->vma, addr);
			if (!basepages[i]) {
				ret = -ENOMEM;
				goto out;
			}
		}
	}

	/* step2: do the sync read */
	blk_start_plug(&plug);
	for (offset = start_offset; offset <= end_offset; offset++) {
		page = basepages[offset - start_offset];
		entry = pentry[offset - start_offset];

		__SetPageLocked(page);
		__SetPageSwapBacked(page);
		set_page_private(page, entry.val);

		/*
		 * NOTE: In kernel-5.4, the memcg charge was moved below the
		 * do_swap_page->ksm_might_need_to_copy  function!
		 */

		//shadow = get_shadow_from_swap_cache(entry);
		//if (shadow)
		//	workingset_refault(page, shadow);

		lru_cache_add(page);

#ifndef CONFIG_CONT_PTE_HUGEPAGE_64K_ZRAM
		swap_readpage(page, true);
#endif
	}

#ifdef CONFIG_CONT_PTE_HUGEPAGE_64K_ZRAM
	/*Read pages to basepages from swap once.
	 *first page of basepages : SetPageContFallback flag and
	 *page->freelist points to basepages
	 */
	SetPageContFallback(basepages[0]);
	basepages[0]->freelist = basepages;
	swap_readpage(basepages[0], true);
#endif

	blk_finish_plug(&plug);

#ifdef CONFIG_CONT_PTE_HUGEPAGE_64K_ZRAM
	ClearPageContFallback(basepages[0]);
#endif

	return 0;
out:
	if (i) {
		for (i--; i >= 0; i--)
			put_page(basepages[i]);
	}
	return ret;
}

extern void unlock_nr_pages(struct page **page, int nr);
#endif /* CONFIG_CONT_PTE_HUGEPAGE */

/*
 * We enter with non-exclusive mmap_sem (to exclude vma changes,
 * but allow concurrent faults), and pte mapped but not yet locked.
 * We return with pte unmapped and unlocked.
 *
 * We return with the mmap_sem locked or unlocked in the same cases
 * as does filemap_fault().
 */
vm_fault_t do_swap_page(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	struct page *page = NULL, *swapcache;
	struct mem_cgroup *memcg;
	swp_entry_t entry;
	pte_t pte;
	int locked;
	int exclusive = 0;
	vm_fault_t ret;

#ifdef CONFIG_CONT_PTE_HUGEPAGE
	int i;
	unsigned long haddr = vmf->address & HPAGE_CONT_PTE_MASK;
	pte_t *ptep = vmf->pte - (vmf->address - haddr)/PAGE_SIZE;
	swp_entry_t cont_pte_entries[HPAGE_CONT_PTE_NR];
	bool is_thp_swpin = false;
	int result;
	bool is_thp_fallback_swpin = false;
	struct page *basepages[HPAGE_CONT_PTE_NR] = { NULL };
	/* only critical native tasks have hugepage */
	gfp_t critical_gfp = is_critical_native(current) ?	___GFP_DIRECT_RECLAIM : 0;
#endif

	ret = pte_unmap_same(vmf);
	if (ret) {
		/*
		 * If pte != orig_pte, this means another thread did the
		 * swap operation in our back.
		 * So nothing else to do.
		 */
		if (ret == VM_FAULT_PTNOTSAME)
			ret = 0;
		goto out;
	}

	entry = pte_to_swp_entry(vmf->orig_pte);
	if (unlikely(non_swap_entry(entry))) {
		if (is_migration_entry(entry)) {
			migration_entry_wait(vma->vm_mm, vmf->pmd,
					     vmf->address);
		} else if (is_device_private_entry(entry)) {
			vmf->page = device_private_entry_to_page(entry);
			ret = vmf->page->pgmap->ops->migrate_to_ram(vmf);
		} else if (is_hwpoison_entry(entry)) {
			ret = VM_FAULT_HWPOISON;
		} else {
			print_bad_pte(vma, vmf->address, vmf->orig_pte, NULL);
			ret = VM_FAULT_SIGBUS;
		}
		goto out;
	}


	delayacct_set_flag(DELAYACCT_PF_SWAPIN);
	page = lookup_swap_cache(entry, vma, vmf->address);
	swapcache = page;

	if (!page) {
		struct swap_info_struct *si = swp_swap_info(entry);

		if (si->flags & SWP_SYNCHRONOUS_IO &&
				__swap_count(entry) == 1) {
			/* skip swapcache */
#ifdef CONFIG_CONT_PTE_HUGEPAGE
			bool suitable = false;
			bool thp_swappable = false;
			bool pte_changed = false;
#endif

#ifdef CONFIG_CONT_PTE_HUGEPAGE
			if (is_thp_swap(si)) {
				CHP_BUG_ON(vma->vm_flags & VM_SHARED);
				suitable = transhuge_cont_pte_vma_suitable(vma, haddr);
				if (!suitable) {
					set_cluster_doublemap(entry);
					pr_err("@@@@%s-%d current:%s-%d vma:%llx-%llx fault addr:%llx vma-not-eligible doublemap eligible:%d, ptep:%lx ptes:%lx-%lx-%lx-%lx-%lx-%lx-%lx-%lx-%lx-%lx-%lx-%lx-%lx-%lx-%lx-%lx\n",
						__func__, __LINE__, current->comm, current->pid, vma->vm_start, vma->vm_end, vmf->address,
						vma_is_chp_anonymous(vma),
						(unsigned long)ptep,
						*(ptep + 0), *(ptep + 1), *(ptep + 2), *(ptep + 3),  *(ptep + 4), *(ptep + 5), *(ptep + 6), *(ptep + 7),
						*(ptep + 8), *(ptep + 9), *(ptep + 10), *(ptep + 11),  *(ptep + 12), *(ptep + 13), *(ptep + 14), *(ptep + 15));
				} else {
					/*
					 * zap_pte_range and copy_pte_range only hold ptl, we need
					 * to atomically check 16 swap entries as they are modify-
					 * ing pte swap entries one by one
					 */
					if (!pte_map_lock(vmf)) {
						ret = VM_FAULT_RETRY;
						goto out;
					}
					thp_swappable = is_cont_pte_swap(entry, ptep, cont_pte_entries, vmf, &pte_changed);
					pte_unmap_unlock(vmf->pte, vmf->ptl);
				}
			}
			if (suitable && thp_swappable) {
				count_vm_chp_event(THP_SWPIN_NO_SWAPCACHE_ENTRY);
				if (critical_gfp)
					count_vm_chp_event(THP_SWPIN_CRITICAL_ENTRY);

				page = alloc_cont_pte_hugepage((GFP_TRANSHUGE_LIGHT | __GFP_KSWAPD_RECLAIM | critical_gfp) & ~__GFP_MOVABLE & ~__GFP_COMP);
				if (page) {
					int i;
					count_vm_chp_event(THP_SWPIN_NO_SWAPCACHE_ALLOC_SUCCESS);
					for (i = 0; i < HPAGE_CONT_PTE_NR; i++)
						SetPageCont(&page[i]);
					prep_compound_page(page, HPAGE_CONT_PTE_ORDER);
					prep_transhuge_page(page);
					__SetPageLocked(page);
					__SetPageSwapBacked(page);
					set_page_private(page, cont_pte_entries[0].val);

					/*
					 * NOTE: In kernel-5.4, the memcg charge was moved below the
					 * do_swap_page->ksm_might_need_to_copy  function!
					 */

#if 0
					shadow = get_shadow_from_swap_cache(cont_pte_entries[0]);
					if (shadow)
						workingset_refault(page, shadow);
#endif
					lru_cache_add(page);
					swap_readpage(page, true);
					exclusive |= RMAP_COMPOUND;
					is_thp_swpin = true;
					goto alloc_page_done;
				}
				if (critical_gfp)
					count_vm_chp_event(THP_SWPIN_CRITICAL_FALLBACK);

				count_vm_chp_event(THP_SWPIN_NO_SWAPCACHE_ALLOC_FAIL);

				/* process thp no swapcache fallback */
				{
					is_thp_fallback_swpin = true;
					count_vm_chp_event(THP_SWPIN_NO_SWAPCACHE_FALLBACK_ENTRY);
					result = read_thp_no_swapcache_fallback(&cont_pte_entries[0],
							GFP_HIGHUSER_MOVABLE, vmf, basepages);
					if (result == -ENOMEM) {
						ret = VM_FAULT_OOM;
						count_vm_chp_event(THP_SWPIN_NO_SWAPCACHE_FALLBACK_ALLOC_FAIL);
					} else if (!result) {
						count_vm_chp_event(THP_SWPIN_NO_SWAPCACHE_FALLBACK_ALLOC_SUCCESS);
						page = basepages[vmf->pte - ptep];

						goto alloc_page_done;
					}
				}
			} else
#endif
			{
#ifdef CONFIG_CONT_PTE_HUGEPAGE
			/* other threads have probably set ptes for this process */
			if (pte_changed) {
				ret = 0;
				goto out;
			}
			if (is_thp_swap(si)) {
				set_cluster_doublemap(entry);
				if (suitable) {
					pr_err("@@@@%s-%d current:%s-%d-%d vma:%llx-%llx fault addr:%llx entry val:%lx offset:%lx pte-unchanged doublemap eligible:%d, ptep:%lx ptes:%lx-%lx-%lx-%lx-%lx-%lx-%lx-%lx-%lx-%lx-%lx-%lx-%lx-%lx-%lx-%lx\n",
						__func__, __LINE__, current->comm, current->pid, current->tgid, vma->vm_start, vma->vm_end, vmf->address,
						entry.val, swp_offset(entry), vma_is_chp_anonymous(vma),
						(unsigned long)ptep,
						*(ptep + 0), *(ptep + 1), *(ptep + 2), *(ptep + 3),  *(ptep + 4), *(ptep + 5), *(ptep + 6), *(ptep + 7),
						*(ptep + 8), *(ptep + 9), *(ptep + 10), *(ptep + 11),  *(ptep + 12), *(ptep + 13), *(ptep + 14), *(ptep + 15));
				}
			}
#endif
			page = alloc_page_vma(GFP_HIGHUSER_MOVABLE | __GFP_CMA
			      | __GFP_OFFLINABLE, vma,
			      vmf->address);
			if (page) {
#ifdef CONFIG_CONT_PTE_HUGEPAGE
				CHP_BUG_ON(PageCont(page));
				CHP_BUG_ON(PageContRefill(page));
#endif
				__SetPageLocked(page);
				__SetPageSwapBacked(page);
				set_page_private(page, entry.val);

				/*
				 * NOTE: In kernel-5.4, the memcg charge was moved below the
				 * do_swap_page->ksm_might_need_to_copy  function!
				 */

				lru_cache_add_anon(page);
				swap_readpage(page, true);
			}
			}
		} else if (vmf->flags & FAULT_FLAG_SPECULATIVE) {
			/*
			 * Don't try readahead during a speculative page fault
			 * as the VMA's boundaries may change in our back.
			 * If the page is not in the swap cache and synchronous
			 * read is disabled, fall back to the regular page fault
			 * mechanism.
			 */
			delayacct_clear_flag(DELAYACCT_PF_SWAPIN);
			ret = VM_FAULT_RETRY;
			goto out;
		} else {
#if defined(CONFIG_CONT_PTE_HUGEPAGE)
			bool suitable = false;
			bool thp_swappable = false;
			bool pte_changed = false;
			if (is_thp_swap(si)) {
				CHP_BUG_ON(vma->vm_flags & VM_SHARED);
				suitable = transhuge_cont_pte_vma_suitable(vma, haddr);
				if (!suitable) {
					set_cluster_doublemap(entry);
#if CONFIG_CHP_ABMORMAL_PTES_DEBUG
					{volatile bool __maybe_unused x = commit_chp_abnormal_ptes_record(DOUBLE_MAP_REASON_DO_SWAP_PAGE1);}
#endif
					pr_err("@@@@%s-%d current:%s-%d vma:%llx-%llx fault addr:%llx vma-not-eligible doublemap eligible:%d ptep:%lx ptes:%lx-%lx-%lx-%lx-%lx-%lx-%lx-%lx-%lx-%lx-%lx-%lx-%lx-%lx-%lx-%lx\n",
						__func__, __LINE__, current->comm, current->pid, vma->vm_start, vma->vm_end, vmf->address,
						vma_is_chp_anonymous(vma),
						(unsigned long)ptep,
						*(ptep + 0), *(ptep + 1), *(ptep + 2), *(ptep + 3),  *(ptep + 4), *(ptep + 5), *(ptep + 6), *(ptep + 7),
						*(ptep + 8), *(ptep + 9), *(ptep + 10), *(ptep + 11),  *(ptep + 12), *(ptep + 13), *(ptep + 14), *(ptep + 15));
				} else {
					/*
					 * zap_pte_range and copy_pte_range only hold ptl, we need
					 * to atomically check 16 swap entries as they are modify-
					 * ing pte swap entries one by one
					 */
					if (!pte_map_lock(vmf)) {
						ret = VM_FAULT_RETRY;
						goto out;
					}

					thp_swappable = is_cont_pte_swap(entry, ptep, cont_pte_entries, vmf, &pte_changed);
					pte_unmap_unlock(vmf->pte, vmf->ptl);
				}
			}
			if (suitable && thp_swappable) {
				bool new_page_allocated = false;
				if (critical_gfp)
					count_vm_chp_event(THP_SWPIN_CRITICAL_ENTRY);

				count_vm_chp_event(THP_SWPIN_SWAPCACHE_ENTRY);
				page = read_swap_cache_thp(&cont_pte_entries[0], GFP_HIGHUSER_MOVABLE,
					vma, vmf->address, &new_page_allocated, &result);
				if (new_page_allocated) {
					swap_readpage(compound_head(page), true);
					count_vm_chp_event(THP_SWPIN_SWAPCACHE_ALLOC_SUCCESS);
				} else {
					count_vm_chp_event(THP_SWPIN_SWAPCACHE_PREPARE_FAIL);
					atomic64_inc(&perf_stat.thp_read_swpcache_ret_status_stat[result]);
					/*
					 * process thp swapin with swapcache fallback(only handles
					 * fallbacks where thp fail to be allocated).
					*/
					if (result == RET_STATUS_ALLOC_THP_FAIL) {
						if (critical_gfp)
							count_vm_chp_event(THP_SWPIN_CRITICAL_FALLBACK);
						/*
						 * Similar to sync swapin, the pages is not added
						 * to the swap cache, and cow operations are not
						 * even required.
						 */
						is_thp_fallback_swpin = true;
						count_vm_chp_event(THP_SWPIN_SWAPCACHE_FALLBACK_ENTRY);
						result = read_thp_no_swapcache_fallback(&cont_pte_entries[0],
								GFP_HIGHUSER_MOVABLE, vmf, basepages);
						if (result == -ENOMEM) {
							ret = VM_FAULT_OOM;
							count_vm_chp_event(THP_SWPIN_SWAPCACHE_FALLBACK_ALLOC_FAIL);
						} else if (!result) {
							count_vm_chp_event(THP_SWPIN_SWAPCACHE_FALLBACK_ALLOC_SUCCESS);
							page = basepages[vmf->pte - ptep];

							goto alloc_page_done;
						}
					}
				}

				if (page && PageCompound(page))
					is_thp_swpin = true;
			} else
#endif
			{
#ifdef CONFIG_CONT_PTE_HUGEPAGE
			/* other threads have probably set ptes for this process */
			if (pte_changed) {
				ret = 0;
				goto out;
			}
			if (is_thp_swap(si)) {
#if CONFIG_CHP_ABMORMAL_PTES_DEBUG
				{volatile bool __maybe_unused x = commit_chp_abnormal_ptes_record(DOUBLE_MAP_REASON_DO_SWAP_PAGE2);}
#endif
				set_cluster_doublemap(entry);
				if (suitable) {
					pr_err("@@@@%s-%d current:%s-%d vma:%llx-%llx fault addr:%llx pte-unchanged doublemap eligible:%d ptep:%lx ptes:%lx-%lx-%lx-%lx-%lx-%lx-%lx-%lx-%lx-%lx-%lx-%lx-%lx-%lx-%lx-%lx\n",
							__func__, __LINE__, current->comm, current->pid, vma->vm_start, vma->vm_end, vmf->address,
							vma_is_chp_anonymous(vma),
							(unsigned long)ptep,
							*(ptep + 0), *(ptep + 1), *(ptep + 2), *(ptep + 3),  *(ptep + 4), *(ptep + 5), *(ptep + 6), *(ptep + 7),
							*(ptep + 8), *(ptep + 9), *(ptep + 10), *(ptep + 11),  *(ptep + 12), *(ptep + 13), *(ptep + 14), *(ptep + 15));
				}
			}
#endif
			page = swapin_readahead(entry, GFP_HIGHUSER_MOVABLE,
						vmf);
#ifdef CONFIG_CONT_PTE_HUGEPAGE
			CHP_BUG_ON(page && PageCont(page));
			CHP_BUG_ON(page && PageContRefill(page));
#endif
		}

			swapcache = page;
		}
#ifdef CONFIG_CONT_PTE_HUGEPAGE
alloc_page_done:
#endif

		if (!page) {
			/*
			 * Back out if the VMA has changed in our back during
			 * a speculative page fault or if somebody else
			 * faulted in this pte while we released the pte lock.
			 */
			if (!pte_map_lock(vmf)) {
				delayacct_clear_flag(DELAYACCT_PF_SWAPIN);
				ret = VM_FAULT_RETRY;
				goto out;
			}

			if (likely(pte_same(*vmf->pte, vmf->orig_pte)))
				ret = VM_FAULT_OOM;
			delayacct_clear_flag(DELAYACCT_PF_SWAPIN);
			goto unlock;
		}

		/* Had to read the page from swap area: Major fault */
		ret = VM_FAULT_MAJOR;
		count_vm_event(PGMAJFAULT);
		count_memcg_event_mm(vma->vm_mm, PGMAJFAULT);
	} else if (PageHWPoison(page)) {
		/*
		 * hwpoisoned dirty swapcache pages are kept for killing
		 * owner processes (which may be unknown at hwpoison time)
		 */
		ret = VM_FAULT_HWPOISON;
		delayacct_clear_flag(DELAYACCT_PF_SWAPIN);
		goto out_release;
	}
#ifdef CONFIG_CONT_PTE_HUGEPAGE
	if (!swapcache && is_thp_fallback_swpin)
		locked = lock_nr_pages_or_retry(basepages, vma->vm_mm, vmf->flags, HPAGE_CONT_PTE_NR);
	else
#endif
		locked = lock_page_or_retry(page, vma->vm_mm, vmf->flags);

	delayacct_clear_flag(DELAYACCT_PF_SWAPIN);
	if (!locked) {
		ret |= VM_FAULT_RETRY;
		goto out_release;
	}

	/*
	 * Make sure try_to_free_swap or reuse_swap_page or swapoff did not
	 * release the swapcache from under us.  The page pin, and pte_same
	 * test below, are not enough to exclude that.  Even if it is still
	 * swapcache, we need to check that the page's swap has not changed.
	 */
	if (unlikely((!PageSwapCache(page) ||
			page_private(page) != entry.val)) && swapcache)
		goto out_page;

	page = ksm_might_need_to_copy(page, vma, vmf->address);
	if (unlikely(!page)) {
		ret = VM_FAULT_OOM;
		page = swapcache;
		goto out_page;
	}

#ifdef CONFIG_CONT_PTE_HUGEPAGE
	if (basepages[0]) {
		for (i = 0; i < HPAGE_CONT_PTE_NR; i++) {
			if (mem_cgroup_try_charge_delay(basepages[i], vma->vm_mm, GFP_KERNEL, &memcg, false)) {
				if (i) {
					for (i--; i >= 0; i--)
						mem_cgroup_cancel_charge(basepages[i], memcg, false);
				}
				ret = VM_FAULT_OOM;
				goto out_page;
			}
		}
	} else {
		if (mem_cgroup_try_charge_delay(compound_head(page), vma->vm_mm, GFP_KERNEL,
					&memcg, ContPteHugePage(page))) {
			ret = VM_FAULT_OOM;
			goto out_page;
		}
	}
#else
	if (mem_cgroup_try_charge_delay(page, vma->vm_mm, GFP_KERNEL,
					&memcg, false)) {
		ret = VM_FAULT_OOM;
		goto out_page;
	}
#endif

	/*
	 * Back out if the VMA has changed in our back during a speculative
	 * page fault or if somebody else already faulted in this pte.
	 */
	if (!pte_map_lock(vmf)) {
		ret = VM_FAULT_RETRY;
		goto out_cancel_cgroup;
	}
	if (unlikely(!pte_same(*vmf->pte, vmf->orig_pte)))
		goto out_nomap;

	if (unlikely(!PageUptodate(page))) {
		ret = VM_FAULT_SIGBUS;
		goto out_nomap;
	}

	/*
	 * The page isn't present yet, go ahead with the fault.
	 *
	 * Be careful about the sequence of operations here.
	 * To get its accounting right, reuse_swap_page() must be called
	 * while the page is counted on swap but not yet in mapcount i.e.
	 * before page_add_anon_rmap() and swap_free(); try_to_free_swap()
	 * must be called after the swap_free(), or it will never succeed.
	 */
#ifdef CONFIG_CONT_PTE_HUGEPAGE
	if (ContPteHugePage(page)) {
		struct page *head =  compound_head(page);
		/* goto non-SPF, also avoid race with concurrent swapin */
		if (!is_cont_pte_swap(entry, ptep, cont_pte_entries, NULL, NULL) ||
			!transhuge_cont_pte_vma_suitable(vma, haddr)) {
			if (is_thp_swpin) {
				if (!(vmf->flags & FAULT_FLAG_SPECULATIVE) &&
				    vmf->flags & FAULT_FLAG_ALLOW_RETRY) {
#if CONFIG_NON_SPF_FAULT_RETRY_DEBUG
					atomic64_inc(&perf_stat.non_sfp_fault_retry_cnt[SWPIN_CHP_FAULT_RETRY]);
#endif
					if (!(vmf->flags & FAULT_FLAG_RETRY_NOWAIT))
						mmap_read_unlock(vma->vm_mm);
				}
				ret = VM_FAULT_RETRY;
				goto out_nomap;
			} else {
				goto basepages;
			}
		}
		if (!is_thp_swpin && swapcache)
			atomic64_inc(&thp_swpin_hit_swapcache);
		add_mm_counter_fast(vma->vm_mm, MM_ANONPAGES, HPAGE_CONT_PTE_NR);
		add_mm_counter_fast(vma->vm_mm, MM_SWAPENTS, -HPAGE_CONT_PTE_NR);
		pte = mk_pte(head, vmf->vma_page_prot);
		if (vmf->flags & FAULT_FLAG_WRITE) {
			bool reuse = swapcache ? reuse_swap_cont_pte_page(head, NULL) : true;
			if (reuse) {
				pte = maybe_mkwrite(pte_mkdirty(pte), vmf->vma_flags);
				vmf->flags &= ~FAULT_FLAG_WRITE;
				ret |= VM_FAULT_WRITE;
				exclusive = RMAP_EXCLUSIVE;
			}
		}
		flush_icache_page(vma, head);
		if (pte_swp_soft_dirty(vmf->orig_pte))
			pte = pte_mksoft_dirty(pte);
		pte = pte_mkcont(pte);
		cont_pte_set_huge_pte_at(vma->vm_mm, haddr, ptep, pte);
		vmf->orig_pte = READ_ONCE(*vmf->pte);

		do_page_add_anon_rmap(head, vma, haddr, exclusive | RMAP_COMPOUND);
		mem_cgroup_commit_charge(head, memcg, true, true);
		cont_pte_swap_entry_free(swp_swap_info(entry), cont_pte_entries);
		goto pte_set_done;
	} else if (is_thp_fallback_swpin) {
		unsigned long addr;
		pte_t *_ptep;
		struct page *sub_page;
		/* goto non-SPF, also avoid race with concurrent swapin */
		if (!is_cont_pte_swap(entry, ptep, cont_pte_entries, NULL, NULL) ||
			!transhuge_cont_pte_vma_suitable(vma, haddr)) {
			pr_err_ratelimited("FIXME: doublemap %s:%d\n", __func__, __LINE__);

			if (!(vmf->flags & FAULT_FLAG_SPECULATIVE) &&
			    vmf->flags & FAULT_FLAG_ALLOW_RETRY) {
#if CONFIG_NON_SPF_FAULT_RETRY_DEBUG
				atomic64_inc(&perf_stat.non_sfp_fault_retry_cnt[SWPIN_FALLBACK_FAULT_RETRY]);
#endif
				if (!(vmf->flags & FAULT_FLAG_RETRY_NOWAIT))
					mmap_read_unlock(vma->vm_mm);
			}
			ret = VM_FAULT_RETRY;
			goto out_nomap;
		}

		add_mm_counter_fast(vma->vm_mm, MM_ANONPAGES, HPAGE_CONT_PTE_NR);
		add_mm_counter_fast(vma->vm_mm, MM_SWAPENTS, -HPAGE_CONT_PTE_NR);
		for (i = 0; i < HPAGE_CONT_PTE_NR; i++) {
			addr = haddr + i * PAGE_SIZE;
			_ptep = ptep + i;
			sub_page = basepages[i];
			pte = mk_pte(sub_page, vmf->vma_page_prot);

			/* no swapcache no cow */
			if (vmf->flags & FAULT_FLAG_WRITE) {
				pte = maybe_mkwrite(pte_mkdirty(pte), vmf->vma_flags);
				ret |= VM_FAULT_WRITE;
				exclusive = RMAP_EXCLUSIVE;
			}
			flush_icache_page(vma, sub_page);
			if (pte_swp_soft_dirty(vmf->orig_pte))
				pte = pte_mksoft_dirty(pte);
			set_pte_at(vma->vm_mm, addr, _ptep, pte);

			do_page_add_anon_rmap(sub_page, vma, addr, exclusive);
			mem_cgroup_commit_charge(sub_page, memcg, true, false);
		}
		cont_pte_swap_entry_free(swp_swap_info(entry), cont_pte_entries);
		goto pte_set_done;
	}

basepages:
#endif

	inc_mm_counter_fast(vma->vm_mm, MM_ANONPAGES);
	dec_mm_counter_fast(vma->vm_mm, MM_SWAPENTS);
	pte = mk_pte(page, vmf->vma_page_prot);
	if ((vmf->flags & FAULT_FLAG_WRITE) && reuse_swap_page(page, NULL)) {
		pte = maybe_mkwrite(pte_mkdirty(pte), vmf->vma_flags);
		vmf->flags &= ~FAULT_FLAG_WRITE;
		ret |= VM_FAULT_WRITE;
		exclusive = RMAP_EXCLUSIVE;
	}
	flush_icache_page(vma, page);
	if (pte_swp_soft_dirty(vmf->orig_pte))
		pte = pte_mksoft_dirty(pte);
	set_pte_at(vma->vm_mm, vmf->address, vmf->pte, pte);
	arch_do_swap_page(vma->vm_mm, vma, vmf->address, pte, vmf->orig_pte);
	vmf->orig_pte = pte;

	/* ksm created a completely new copy */
	if (unlikely(page != swapcache && swapcache)) {
		__page_add_new_anon_rmap(page, vma, vmf->address, false);
		mem_cgroup_commit_charge(page, memcg, false, false);
		__lru_cache_add_active_or_unevictable(page, vmf->vma_flags);
	} else {
		do_page_add_anon_rmap(page, vma, vmf->address, exclusive);
		mem_cgroup_commit_charge(page, memcg, true, false);
		activate_page(page);
	}

	swap_free(entry);
#ifdef CONFIG_CONT_PTE_HUGEPAGE
pte_set_done:
#endif
	if (mem_cgroup_swap_full(page) ||
	    (vmf->vma_flags & VM_LOCKED) || PageMlocked(page))
		try_to_free_swap(page);
#ifdef CONFIG_CONT_PTE_HUGEPAGE
	if (!swapcache && is_thp_fallback_swpin)
		unlock_nr_pages(basepages, HPAGE_CONT_PTE_NR);
	else
#endif
		unlock_page(page);
	if (page != swapcache && swapcache) {
		/*
		 * Hold the lock to avoid the swap entry to be reused
		 * until we take the PT lock for the pte_same() check
		 * (to avoid false positives from pte_same). For
		 * further safety release the lock after the swap_free
		 * so that the swap count won't change under a
		 * parallel locked swapcache.
		 */
		unlock_page(swapcache);
		put_page(swapcache);
	}

#ifdef CONFIG_CONT_PTE_HUGEPAGE
	/* no swapcache no cow */
	if (!is_thp_fallback_swpin && vmf->flags & FAULT_FLAG_WRITE) {
#else
	if (vmf->flags & FAULT_FLAG_WRITE) {
#endif
		ret |= do_wp_page(vmf);
		if (ret & VM_FAULT_ERROR)
			ret &= VM_FAULT_ERROR;
		goto out;
	}

	/* No need to invalidate - it was non-present before */
	update_mmu_cache(vma, vmf->address, vmf->pte);
unlock:
	pte_unmap_unlock(vmf->pte, vmf->ptl);
out:
	return ret;
out_nomap:
	pte_unmap_unlock(vmf->pte, vmf->ptl);
out_cancel_cgroup:
#ifdef CONFIG_CONT_PTE_HUGEPAGE
	if (basepages[0] && memcg) {
		for (i = 0; i < HPAGE_CONT_PTE_NR; i++)
			mem_cgroup_cancel_charge(basepages[i], memcg, false);
	} else
		mem_cgroup_cancel_charge(compound_head(page), memcg, ContPteHugePage(page));
#else
	mem_cgroup_cancel_charge(page, memcg, false);
#endif
out_page:
#ifdef CONFIG_CONT_PTE_HUGEPAGE
	if (!swapcache && is_thp_fallback_swpin)
		unlock_nr_pages(basepages, HPAGE_CONT_PTE_NR);
	else
#endif
		unlock_page(page);
out_release:
#ifdef CONFIG_CONT_PTE_HUGEPAGE
	if (!swapcache && is_thp_fallback_swpin) {
		for (i = 0; i < HPAGE_CONT_PTE_NR; i++)
			put_page(basepages[i]);
	} else
#endif
		put_page(page);
	if (page != swapcache && swapcache) {
		unlock_page(swapcache);
		put_page(swapcache);
	}
	return ret;
}

#ifdef CONFIG_CONT_PTE_HUGEPAGE
void clear_huge_page(struct page *page,
		     unsigned long addr_hint, unsigned int pages_per_huge_page);

static vm_fault_t __do_huge_cont_pte_anonymous_page(struct vm_fault *vmf,
			struct page *page)
{
	struct vm_area_struct *vma = vmf->vma;
	unsigned long haddr = vmf->address & HPAGE_CONT_PTE_MASK;
	vm_fault_t ret = 0;
	pte_t *ptep;
	struct mem_cgroup *memcg;

	VM_BUG_ON_PAGE(!PageCompound(page), page);

	if (mem_cgroup_try_charge_delay(page, vma->vm_mm, GFP_KERNEL, &memcg,
				true)) {
		/*
		 * NOTE: The __put_compound_page function
		 * checks the PageUptodate flag!
		 */
		__SetPageUptodate(page);
		put_page(page);
		count_vm_event(THP_FAULT_FALLBACK);
		return VM_FAULT_FALLBACK;
	}

	clear_huge_page(page, vmf->address, HPAGE_CONT_PTE_NR);
	/*
	 * The memory barrier inside __SetPageUptodate makes sure that
	 * clear_huge_page writes become visible before the set_pmd_at()
	 * write.
	 */
	__SetPageUptodate(page);

	if (!pte_map_lock(vmf)) {
		ret = VM_FAULT_RETRY;
		goto release;
	}

	ptep = vmf->pte - (vmf->address - haddr)/PAGE_SIZE;
	if (unlikely(!pte_none(*vmf->pte))) {
		goto unlock_release;
	} else if (!cont_pte_none(ptep)) {
		ret = VM_FAULT_FALLBACK;
		goto unlock_release;
	} else {
		pte_t entry;
		ret = check_stable_address_space(vma->vm_mm);
		if (ret)
			goto unlock_release;

		/* Deliver the page fault to userland */
		if (userfaultfd_missing(vma)) {
			pte_unmap_unlock(vmf->pte, vmf->ptl);
			put_page(page);
			return handle_userfault(vmf, VM_UFFD_MISSING);
		}

		entry = pte_mkwrite(pte_mkdirty(mk_pte(page, vma->vm_page_prot)));
		entry = pte_mkyoung(entry);
		entry = pte_mkcont(entry);
		entry = pte_mkdirty(entry);
		cont_pte_set_huge_pte_at(vma->vm_mm, haddr, ptep, entry);
		__page_add_new_anon_rmap(page, vma, haddr, true);
		mem_cgroup_commit_charge(page, memcg, false, true);
		lru_cache_add_active_or_unevictable(page, vma);
		add_mm_counter(vma->vm_mm, MM_ANONPAGES, HPAGE_CONT_PTE_NR);
		pte_unmap_unlock(vmf->pte, vmf->ptl);
		count_vm_event(THP_FAULT_ALLOC);
		count_memcg_event_mm(vma->vm_mm, THP_FAULT_ALLOC);
	}

	return 0;
unlock_release:
	pte_unmap_unlock(vmf->pte, vmf->ptl);
release:
	mem_cgroup_cancel_charge(page, memcg, true);
	put_page(page);
	return ret;

}

#define transparent_hugepage_use_zero_page()				\
	(transparent_hugepage_flags &					\
	 (1<<TRANSPARENT_HUGEPAGE_USE_ZERO_PAGE_FLAG))

static vm_fault_t do_huge_cont_pte_anonymous_page(struct vm_fault *vmf)
{
	int i;
	pte_t *ptep;
	struct page *page;
	struct vm_area_struct *vma = vmf->vma;
	unsigned long haddr = vmf->address & HPAGE_CONT_PTE_MASK;
	gfp_t gfp_mask = (GFP_TRANSHUGE_LIGHT | __GFP_KSWAPD_RECLAIM | __GFP_ZERO) & ~__GFP_MOVABLE & ~__GFP_COMP;

	if (!transhuge_cont_pte_vma_suitable(vma, haddr))
		return VM_FAULT_FALLBACK;
	if (unlikely(anon_vma_prepare(vma)))
		return VM_FAULT_OOM;

	if (!(vmf->flags & FAULT_FLAG_WRITE) &&
			!mm_forbids_zeropage(vma->vm_mm) &&
			transparent_hugepage_use_zero_page()) {
		struct page *zero_page;
		vm_fault_t ret;

		zero_page = mm_get_huge_zero_page(vma->vm_mm);

		if (unlikely(!zero_page)) {
			count_vm_event(THP_FAULT_FALLBACK);
			return VM_FAULT_FALLBACK;
		}
		if (!pte_map_lock(vmf))
			return VM_FAULT_RETRY;
		ret = 0;
		ptep = vmf->pte - (vmf->address - haddr)/PAGE_SIZE;
		if (!pte_none(*vmf->pte)) {
			pte_unmap_unlock(vmf->pte, vmf->ptl);
			return 0;
		} else if (!cont_pte_none(ptep)) {
			pte_unmap_unlock(vmf->pte, vmf->ptl);
			return VM_FAULT_FALLBACK;
		} else {
			ret = check_stable_address_space(vma->vm_mm);
			if (ret) {
				pte_unmap_unlock(vmf->pte, vmf->ptl);
			} else if (userfaultfd_missing(vma)) {
				pte_unmap_unlock(vmf->pte, vmf->ptl);
				ret = handle_userfault(vmf, VM_UFFD_MISSING);
				VM_BUG_ON(ret & VM_FAULT_FALLBACK);
			} else {
				set_cont_pte_huge_zero_page(vma->vm_mm, vma,
						   vmf->address, vmf->pte, zero_page);
				pte_unmap_unlock(vmf->pte, vmf->ptl);
			}
		}

		return ret;
	}

	CHP_BUG_ON(vma->vm_flags & VM_SHARED);
	page = alloc_cont_pte_hugepage(gfp_mask);
	if (unlikely(!page)) {
		count_vm_event(THP_FAULT_FALLBACK);
		count_vm_chp_event(THP_DO_ANON_PAGES_FALLBACK);
		return VM_FAULT_FALLBACK;
	}

	count_vm_chp_event(THP_DO_ANON_PAGES);

	for (i = 0; i < HPAGE_CONT_PTE_NR; i++)
		SetPageCont(&page[i]);
	prep_compound_page(page, HPAGE_CONT_PTE_ORDER);
	prep_transhuge_page(page);
	return __do_huge_cont_pte_anonymous_page(vmf, page);
}

static inline vm_fault_t create_huge_cont_pte(struct vm_fault *vmf)
{
	if (vma_is_anonymous(vmf->vma))
		return do_huge_cont_pte_anonymous_page(vmf);
	return VM_FAULT_FALLBACK;
}
#endif

/*
 * We enter with non-exclusive mmap_sem (to exclude vma changes,
 * but allow concurrent faults), and pte mapped but not yet locked.
 * We return with mmap_sem still held, but pte unmapped and unlocked.
 */
static vm_fault_t do_anonymous_page(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	struct mem_cgroup *memcg;
	struct page *page;
	vm_fault_t ret = 0;
	pte_t entry;

	/* File mapping without ->vm_ops ? */
	if (vmf->vma_flags & VM_SHARED)
		return VM_FAULT_SIGBUS;

	/*
	 * Use pte_alloc() instead of pte_alloc_map().  We can't run
	 * pte_offset_map() on pmds where a huge pmd might be created
	 * from a different thread.
	 *
	 * pte_alloc_map() is safe to use under down_write(mmap_sem) or when
	 * parallel threads are excluded by other means.
	 *
	 * Here we only have down_read(mmap_sem).
	 */
	if (pte_alloc(vma->vm_mm, vmf->pmd))
		return VM_FAULT_OOM;

	/* See the comment in pte_alloc_one_map() */
	if (unlikely(pmd_trans_unstable(vmf->pmd)))
		return 0;
#ifdef CONFIG_CONT_PTE_HUGEPAGE
	/* Try cont pte first */
	if (__transparent_hugepage_enabled(vma)) {
		ret = create_huge_cont_pte(vmf);
		if (!(ret & VM_FAULT_FALLBACK))
			return ret;
		/*
		 * NOTE: For the fallback to small page process,
		 * we must reset ret to 0. Otherwise, some checking
		 * errors will occur, such as the gup code.
		 * see faultin_page
		 *	->handle_mm_fault
		*/
		if (ret & VM_FAULT_FALLBACK)
			ret = 0;
	}
#endif

	/* Use the zero-page for reads */
	if (!(vmf->flags & FAULT_FLAG_WRITE) &&
			!mm_forbids_zeropage(vma->vm_mm)) {
		entry = pte_mkspecial(pfn_pte(my_zero_pfn(vmf->address),
						vmf->vma_page_prot));
		if (!pte_map_lock(vmf))
			return VM_FAULT_RETRY;
		if (!pte_none(*vmf->pte))
			goto unlock;
		ret = check_stable_address_space(vma->vm_mm);
		if (ret)
			goto unlock;
		/*
		 * Don't call the userfaultfd during the speculative path.
		 * We already checked for the VMA to not be managed through
		 * userfaultfd, but it may be set in our back once we have lock
		 * the pte. In such a case we can ignore it this time.
		 */
		if (vmf->flags & FAULT_FLAG_SPECULATIVE)
			goto setpte;
		/* Deliver the page fault to userland, check inside PT lock */
		if (userfaultfd_missing(vma)) {
			pte_unmap_unlock(vmf->pte, vmf->ptl);
			return handle_userfault(vmf, VM_UFFD_MISSING);
		}
		goto setpte;
	}

	/* Allocate our own private page. */
	if (unlikely(anon_vma_prepare(vma)))
		goto oom;
	page = alloc_zeroed_user_highpage_movable(vma, vmf->address);
	if (!page)
		goto oom;
#ifdef CONFIG_CONT_PTE_HUGEPAGE
	CHP_BUG_ON(PageCont(page));
	CHP_BUG_ON(PageContRefill(page));
#endif

	if (mem_cgroup_try_charge_delay(page, vma->vm_mm, GFP_KERNEL, &memcg,
					false))
		goto oom_free_page;

	/*
	 * The memory barrier inside __SetPageUptodate makes sure that
	 * preceeding stores to the page contents become visible before
	 * the set_pte_at() write.
	 */
	__SetPageUptodate(page);

	entry = mk_pte(page, vmf->vma_page_prot);
	if (vmf->vma_flags & VM_WRITE)
		entry = pte_mkwrite(pte_mkdirty(entry));

	if (!pte_map_lock(vmf)) {
		ret = VM_FAULT_RETRY;
		goto release;
	}
	if (!pte_none(*vmf->pte))
		goto unlock_and_release;

	ret = check_stable_address_space(vma->vm_mm);
	if (ret)
		goto unlock_and_release;

	/* Deliver the page fault to userland, check inside PT lock */
	if (!(vmf->flags & FAULT_FLAG_SPECULATIVE) &&
				userfaultfd_missing(vma)) {
		pte_unmap_unlock(vmf->pte, vmf->ptl);
		mem_cgroup_cancel_charge(page, memcg, false);
		put_page(page);
		return handle_userfault(vmf, VM_UFFD_MISSING);
	}

	inc_mm_counter_fast(vma->vm_mm, MM_ANONPAGES);
	__page_add_new_anon_rmap(page, vma, vmf->address, false);
	mem_cgroup_commit_charge(page, memcg, false, false);
	__lru_cache_add_active_or_unevictable(page, vmf->vma_flags);
setpte:
	set_pte_at(vma->vm_mm, vmf->address, vmf->pte, entry);

	/* No need to invalidate - it was non-present before */
	update_mmu_cache(vma, vmf->address, vmf->pte);
unlock:
	pte_unmap_unlock(vmf->pte, vmf->ptl);
	return ret;
unlock_and_release:
	pte_unmap_unlock(vmf->pte, vmf->ptl);
release:
	mem_cgroup_cancel_charge(page, memcg, false);
	put_page(page);
	return ret;
oom_free_page:
	put_page(page);
oom:
	return VM_FAULT_OOM;
}

/*
 * The mmap_sem must have been held on entry, and may have been
 * released depending on flags and vma->vm_ops->fault() return value.
 * See filemap_fault() and __lock_page_retry().
 */
static vm_fault_t __do_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	vm_fault_t ret;

	/*
	 * Preallocate pte before we take page_lock because this might lead to
	 * deadlocks for memcg reclaim which waits for pages under writeback:
	 *				lock_page(A)
	 *				SetPageWriteback(A)
	 *				unlock_page(A)
	 * lock_page(B)
	 *				lock_page(B)
	 * pte_alloc_pne
	 *   shrink_page_list
	 *     wait_on_page_writeback(A)
	 *				SetPageWriteback(B)
	 *				unlock_page(B)
	 *				# flush A, B to clear the writeback
	 */
	if (pmd_none(*vmf->pmd) && !vmf->prealloc_pte) {
		vmf->prealloc_pte = pte_alloc_one(vmf->vma->vm_mm);
		if (!vmf->prealloc_pte)
			return VM_FAULT_OOM;
		smp_wmb(); /* See comment in __pte_alloc() */
	}

	ret = vma->vm_ops->fault(vmf);
	if (unlikely(ret & (VM_FAULT_ERROR | VM_FAULT_NOPAGE | VM_FAULT_RETRY |
			    VM_FAULT_DONE_COW)))
		return ret;

	if (unlikely(PageHWPoison(vmf->page))) {
		struct page *page = vmf->page;
		vm_fault_t poisonret = VM_FAULT_HWPOISON;
		if (ret & VM_FAULT_LOCKED) {
			if (page_mapped(page))
				unmap_mapping_pages(page_mapping(page),
						    page->index, 1, false);
			/* Retry if a clean page was removed from the cache. */
			if (invalidate_inode_page(page))
				poisonret = VM_FAULT_NOPAGE;
			unlock_page(page);
		}
		put_page(page);
		vmf->page = NULL;
		return poisonret;
	}

	if (unlikely(!(ret & VM_FAULT_LOCKED)))
		lock_page(vmf->page);
	else
		VM_BUG_ON_PAGE(!PageLocked(vmf->page), vmf->page);

	return ret;
}

/*
 * The ordering of these checks is important for pmds with _PAGE_DEVMAP set.
 * If we check pmd_trans_unstable() first we will trip the bad_pmd() check
 * inside of pmd_none_or_trans_huge_or_clear_bad(). This will end up correctly
 * returning 1 but not before it spams dmesg with the pmd_clear_bad() output.
 */
#ifdef CONFIG_CONT_PTE_HUGEPAGE
int pmd_devmap_trans_unstable(pmd_t *pmd)
#else
static int pmd_devmap_trans_unstable(pmd_t *pmd)
#endif
{
	return pmd_devmap(*pmd) || pmd_trans_unstable(pmd);
}

static vm_fault_t pte_alloc_one_map(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;

	if (!pmd_none(*vmf->pmd) || (vmf->flags & FAULT_FLAG_SPECULATIVE))
		goto map_pte;
	if (vmf->prealloc_pte) {
		vmf->ptl = pmd_lock(vma->vm_mm, vmf->pmd);
		if (unlikely(!pmd_none(*vmf->pmd))) {
			spin_unlock(vmf->ptl);
			goto map_pte;
		}

		mm_inc_nr_ptes(vma->vm_mm);
		pmd_populate(vma->vm_mm, vmf->pmd, vmf->prealloc_pte);
		spin_unlock(vmf->ptl);
		vmf->prealloc_pte = NULL;
	} else if (unlikely(pte_alloc(vma->vm_mm, vmf->pmd))) {
		return VM_FAULT_OOM;
	}
map_pte:
	/*
	 * If a huge pmd materialized under us just retry later.  Use
	 * pmd_trans_unstable() via pmd_devmap_trans_unstable() instead of
	 * pmd_trans_huge() to ensure the pmd didn't become pmd_trans_huge
	 * under us and then back to pmd_none, as a result of MADV_DONTNEED
	 * running immediately after a huge pmd fault in a different thread of
	 * this mm, in turn leading to a misleading pmd_trans_huge() retval.
	 * All we have to ensure is that it is a regular pmd that we can walk
	 * with pte_offset_map() and we can do that through an atomic read in
	 * C, which is what pmd_trans_unstable() provides.
	 */
	if (pmd_devmap_trans_unstable(vmf->pmd))
		return VM_FAULT_NOPAGE;

	/*
	 * At this point we know that our vmf->pmd points to a page of ptes
	 * and it cannot become pmd_none(), pmd_devmap() or pmd_trans_huge()
	 * for the duration of the fault.  If a racing MADV_DONTNEED runs and
	 * we zap the ptes pointed to by our vmf->pmd, the vmf->ptl will still
	 * be valid and we will re-check to make sure the vmf->pte isn't
	 * pte_none() under vmf->ptl protection when we return to
	 * alloc_set_pte().
	 */
	if (!pte_map_lock(vmf))
		return VM_FAULT_RETRY;

	return 0;
}

#ifdef CONFIG_TRANSPARENT_HUGE_PAGECACHE
static void deposit_prealloc_pte(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;

	pgtable_trans_huge_deposit(vma->vm_mm, vmf->pmd, vmf->prealloc_pte);
	/*
	 * We are going to consume the prealloc table,
	 * count that as nr_ptes.
	 */
	mm_inc_nr_ptes(vma->vm_mm);
	vmf->prealloc_pte = NULL;
}

static vm_fault_t do_set_pmd(struct vm_fault *vmf, struct page *page)
{
	struct vm_area_struct *vma = vmf->vma;
	bool write = vmf->flags & FAULT_FLAG_WRITE;
	unsigned long haddr = vmf->address & HPAGE_PMD_MASK;
	pmd_t entry;
	int i;
	vm_fault_t ret;

	if (!transhuge_vma_suitable(vma, haddr))
		return VM_FAULT_FALLBACK;

	ret = VM_FAULT_FALLBACK;
	page = compound_head(page);

	/*
	 * Archs like ppc64 need additonal space to store information
	 * related to pte entry. Use the preallocated table for that.
	 */
	if (arch_needs_pgtable_deposit() && !vmf->prealloc_pte) {
		vmf->prealloc_pte = pte_alloc_one(vma->vm_mm);
		if (!vmf->prealloc_pte)
			return VM_FAULT_OOM;
		smp_wmb(); /* See comment in __pte_alloc() */
	}

	vmf->ptl = pmd_lock(vma->vm_mm, vmf->pmd);
	if (unlikely(!pmd_none(*vmf->pmd)))
		goto out;

	for (i = 0; i < HPAGE_PMD_NR; i++)
		flush_icache_page(vma, page + i);

	entry = mk_huge_pmd(page, vmf->vma_page_prot);
	if (write)
		entry = maybe_pmd_mkwrite(pmd_mkdirty(entry), vma);

	add_mm_counter(vma->vm_mm, mm_counter_file(page), HPAGE_PMD_NR);
	page_add_file_rmap(page, true);
	/*
	 * deposit and withdraw with pmd lock held
	 */
	if (arch_needs_pgtable_deposit())
		deposit_prealloc_pte(vmf);

	set_pmd_at(vma->vm_mm, haddr, vmf->pmd, entry);

	update_mmu_cache_pmd(vma, haddr, vmf->pmd);

	/* fault is handled */
	ret = 0;
	count_vm_event(THP_FILE_MAPPED);
out:
	spin_unlock(vmf->ptl);
	return ret;
}
#else
static vm_fault_t do_set_pmd(struct vm_fault *vmf, struct page *page)
{
	BUILD_BUG();
	return 0;
}
#endif

#ifdef CONFIG_CONT_PTE_HUGEPAGE
static bool inline dump_cont_pte_around(struct page *head, pte_t *ptep)
{
	int i;
	int same = true;

	for (i = 0; i < HPAGE_CONT_PTE_NR; i++) {
		pte_t pte = READ_ONCE(*(ptep + i));
		pr_err("@@@FIXME:%s i:%d pte none:%d present:%d page:%lx head:%lx\n",
				__func__, i, pte_none(pte), pte_present(pte),
				pte_present(pte) ? (unsigned long)pte_page(pte) : 0,
				(unsigned long)head );

		if (pte_present(pte)) {
			if (compound_head(pte_page(pte)) != head)
				same = false;
		}
	}

	return same;
}

#define cont_pte_vmf_dump(vmf, reason) \
do { \
	struct vm_area_struct *vma = vmf->vma; \
	const char *name = vma->vm_file->f_path.dentry ? (const char *)vma->vm_file->f_path.dentry->d_name.name : "NULL"; \
	pr_err("%s %s %d: filename:%s inode:%ld process:%s aligned:%d index:%lx-%lx " \
		"vm_pgoff:%lx fault address:%lx vma:%lx-%lx r:%d w:%d x:%d mw:%d flags:%lx\n", \
		reason, __func__, __LINE__, name, vma->vm_file->f_inode->i_ino, \
		current->comm, transhuge_cont_pte_vma_aligned(vma), \
		vmf->page ? vmf->page->index : -1UL, vmf->pgoff, vma->vm_pgoff, \
		(unsigned long)vmf->address, (unsigned long)vma->vm_start, (unsigned long)vma->vm_end, \
		!!(vma->vm_flags & VM_READ), !!(vma->vm_flags & VM_WRITE), !!(vma->vm_flags & VM_EXEC), \
		!!(vma->vm_flags & VM_MAYWRITE), vma->vm_flags);\
} while (0)

vm_fault_t do_set_pte(struct vm_fault *vmf, struct mem_cgroup *memcg,
		struct page *page, unsigned long addr)
{
	struct vm_area_struct *vma = vmf->vma;
	bool write = vmf->flags & FAULT_FLAG_WRITE;
	pte_t entry;

	flush_icache_page(vma, page);
	entry = mk_pte(page, vmf->vma_page_prot);
	if (write)
		entry = maybe_mkwrite(pte_mkdirty(entry), vmf->vma_flags);

	if (vmf->flags & FAULT_FLAG_PREFAULT_OLD)
		entry = pte_mkold(entry);

	/* copy-on-write page */
	if (write && !(vmf->vma_flags & VM_SHARED)) {
		inc_mm_counter_fast(vma->vm_mm, MM_ANONPAGES);
		__page_add_new_anon_rmap(page, vma, addr, false);
		mem_cgroup_commit_charge(page, memcg, false, false);
		__lru_cache_add_active_or_unevictable(page, vmf->vma_flags);
	} else {
		inc_mm_counter_fast(vma->vm_mm, mm_counter_file(page));
		page_add_file_rmap(page, false);
	}
	set_pte_at(vma->vm_mm, addr, vmf->pte, entry);

	/* no need to invalidate: a not-present page won't be cached */
	update_mmu_cache(vma, vmf->address, vmf->pte);

	return 0;
}

#endif

/**
 * alloc_set_pte - setup new PTE entry for given page and add reverse page
 * mapping. If needed, the fucntion allocates page table or use pre-allocated.
 *
 * @vmf: fault environment
 * @memcg: memcg to charge page (only for private mappings)
 * @page: page to map
 *
 * Caller must take care of unlocking vmf->ptl, if vmf->pte is non-NULL on
 * return.
 *
 * Target users are page handler itself and implementations of
 * vm_ops->map_pages.
 *
 * Return: %0 on success, %VM_FAULT_ code in case of error.
 */
vm_fault_t alloc_set_pte(struct vm_fault *vmf, struct mem_cgroup *memcg,
		struct page *page)
{
	struct vm_area_struct *vma = vmf->vma;
	bool write = vmf->flags & FAULT_FLAG_WRITE;
	pte_t entry;
	vm_fault_t ret;

	if (pmd_none(*vmf->pmd) && PageTransCompound(page) &&
			IS_ENABLED(CONFIG_TRANSPARENT_HUGE_PAGECACHE)) {
		/* THP on COW? */
		VM_BUG_ON_PAGE(memcg, page);

		ret = do_set_pmd(vmf, page);
		if (ret != VM_FAULT_FALLBACK)
			return ret;
	}

	if (!vmf->pte) {
		ret = pte_alloc_one_map(vmf);
		if (ret)
			return ret;
	}

	/* Re-check under ptl */
	if (unlikely(!pte_none(*vmf->pte)))
		return VM_FAULT_NOPAGE;

	flush_icache_page(vma, page);
	entry = mk_pte(page, vmf->vma_page_prot);
	if (write)
		entry = maybe_mkwrite(pte_mkdirty(entry), vmf->vma_flags);

	if (vmf->flags & FAULT_FLAG_PREFAULT_OLD)
		entry = pte_mkold(entry);

	/* copy-on-write page */
	if (write && !(vmf->vma_flags & VM_SHARED)) {
		inc_mm_counter_fast(vma->vm_mm, MM_ANONPAGES);
		__page_add_new_anon_rmap(page, vma, vmf->address, false);
		mem_cgroup_commit_charge(page, memcg, false, false);
		__lru_cache_add_active_or_unevictable(page, vmf->vma_flags);
	} else {
		inc_mm_counter_fast(vma->vm_mm, mm_counter_file(page));
		page_add_file_rmap(page, false);
	}
	set_pte_at(vma->vm_mm, vmf->address, vmf->pte, entry);

	/* no need to invalidate: a not-present page won't be cached */
	update_mmu_cache(vma, vmf->address, vmf->pte);

	return 0;
}

/**
 * finish_fault - finish page fault once we have prepared the page to fault
 *
 * @vmf: structure describing the fault
 *
 * This function handles all that is needed to finish a page fault once the
 * page to fault in is prepared. It handles locking of PTEs, inserts PTE for
 * given page, adds reverse page mapping, handles memcg charges and LRU
 * addition.
 *
 * The function expects the page to be locked and on success it consumes a
 * reference of a page being mapped (for the PTE which maps it).
 *
 * Return: %0 on success, %VM_FAULT_ code in case of error.
 */
vm_fault_t finish_fault(struct vm_fault *vmf)
{
	struct page *page;
	vm_fault_t ret = 0;

	/* Did we COW the page? */
	if ((vmf->flags & FAULT_FLAG_WRITE) &&
	    !(vmf->vma_flags & VM_SHARED))
		page = vmf->cow_page;
	else
		page = vmf->page;

	/*
	 * check even for read faults because we might have lost our CoWed
	 * page
	 */
	if (!(vmf->vma->vm_flags & VM_SHARED))
		ret = check_stable_address_space(vmf->vma->vm_mm);

#ifdef CONFIG_CONT_PTE_HUGEPAGE
	if (pmd_none(*vmf->pmd) && !(vmf->flags & FAULT_FLAG_SPECULATIVE)) {
		struct vm_area_struct *vma = vmf->vma;

		if (PageTransCompound(page)) {
			ret = do_set_pmd(vmf, page);
			if (ret != VM_FAULT_FALLBACK)
				return ret;
		}

		if (vmf->prealloc_pte) {
			vmf->ptl = pmd_lock(vma->vm_mm, vmf->pmd);
			if (likely(pmd_none(*vmf->pmd))) {
				mm_inc_nr_ptes(vma->vm_mm);
				pmd_populate(vma->vm_mm, vmf->pmd, vmf->prealloc_pte);
				vmf->prealloc_pte = NULL;
			}
			spin_unlock(vmf->ptl);
		} else if (unlikely(pte_alloc(vma->vm_mm, vmf->pmd))) {
			return VM_FAULT_OOM;
		}
	}

	/* See comment in handle_pte_fault() */
	if (pmd_devmap_trans_unstable(vmf->pmd))
		return 0;

	if (!pte_map_lock(vmf))
		return VM_FAULT_RETRY;

	ret = 0;

	CHP_BUG_ON(vmf->page && PageCont(vmf->page) && !vmf_may_cont_pte(vmf));
	if (ContPteHugePage(page)) {
		unsigned long off = vmf->pgoff & (HPAGE_CONT_PTE_NR - 1);

		if (vmf->flags & FAULT_FLAG_CONT_PTE) {
			pte_t *ptep = vmf->pte - off;

			CHP_BUG_ON(!IS_ALIGNED((unsigned long)ptep, sizeof(pte_t) * HPAGE_CONT_PTE_NR));
			CHP_BUG_ON(vmf->page && ((page_to_pfn(vmf->page) & (HPAGE_CONT_PTE_NR - 1)) != off));
			if (likely(cont_pte_none(ptep))) {
				do_set_cont_pte(vmf, compound_head(page));
			} else {
				/*
				 * assure subpage is mapped, otherwise, VM_FAULT_NOPAGE will result in
				 * endless loop for the app
				 */
				if (pte_none(*vmf->pte)) {
					bool same = dump_cont_pte_around(compound_head(page), ptep);
					cont_pte_vmf_dump(vmf, "pte_none");
					if (same)
						goto doublemap;

					CHP_BUG_ON(1);
				}
				ret = VM_FAULT_NOPAGE;
			}
		} else { /*double mapped */
doublemap:
			cont_pte_pagefault_dump(vmf, "DOUBLE-mapped");
			if (likely(pte_none(*vmf->pte)))
				do_set_pte(vmf, vmf->memcg, page, vmf->address);
			else
				ret = VM_FAULT_NOPAGE;
		}

		pte_unmap_unlock(vmf->pte, vmf->ptl);
		return ret;
	}

	/* Re-check under ptl */
	if (likely(pte_none(*vmf->pte)))
		do_set_pte(vmf, vmf->memcg, page, vmf->address);
	else
		ret = VM_FAULT_NOPAGE;

	pte_unmap_unlock(vmf->pte, vmf->ptl);
#else

	if (!ret)
		ret = alloc_set_pte(vmf, vmf->memcg, page);
	if (vmf->pte)
		pte_unmap_unlock(vmf->pte, vmf->ptl);

#endif
	return ret;
}

/*
 * If architecture emulates "accessed" or "young" bit without HW support,
 * there is no much gain with fault_around.
 */
static unsigned long fault_around_bytes __read_mostly =
#ifndef __HAVE_ARCH_PTEP_SET_ACCESS_FLAGS
	PAGE_SIZE;
#else
	rounddown_pow_of_two(65536);
#endif

#ifdef CONFIG_DEBUG_FS
static int fault_around_bytes_get(void *data, u64 *val)
{
	*val = fault_around_bytes;
	return 0;
}

/*
 * fault_around_bytes must be rounded down to the nearest page order as it's
 * what do_fault_around() expects to see.
 */
static int fault_around_bytes_set(void *data, u64 val)
{
	if (val / PAGE_SIZE > PTRS_PER_PTE)
		return -EINVAL;
	if (val > PAGE_SIZE)
		fault_around_bytes = rounddown_pow_of_two(val);
	else
		fault_around_bytes = PAGE_SIZE; /* rounddown_pow_of_two(0) is undefined */
	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(fault_around_bytes_fops,
		fault_around_bytes_get, fault_around_bytes_set, "%llu\n");

static int __init fault_around_debugfs(void)
{
	debugfs_create_file_unsafe("fault_around_bytes", 0644, NULL, NULL,
				   &fault_around_bytes_fops);
	return 0;
}
late_initcall(fault_around_debugfs);
#endif

/*
 * do_fault_around() tries to map few pages around the fault address. The hope
 * is that the pages will be needed soon and this will lower the number of
 * faults to handle.
 *
 * It uses vm_ops->map_pages() to map the pages, which skips the page if it's
 * not ready to be mapped: not up-to-date, locked, etc.
 *
 * This function is called with the page table lock taken. In the split ptlock
 * case the page table lock only protects only those entries which belong to
 * the page table corresponding to the fault address.
 *
 * This function doesn't cross the VMA boundaries, in order to call map_pages()
 * only once.
 *
 * fault_around_bytes defines how many bytes we'll try to map.
 * do_fault_around() expects it to be set to a power of two less than or equal
 * to PTRS_PER_PTE.
 *
 * The virtual address of the area that we map is naturally aligned to
 * fault_around_bytes rounded down to the machine page size
 * (and therefore to page order).  This way it's easier to guarantee
 * that we don't cross page table boundaries.
 */
static vm_fault_t do_fault_around(struct vm_fault *vmf)
{
	unsigned long address = vmf->address, nr_pages, mask;
	pgoff_t start_pgoff = vmf->pgoff;
	pgoff_t end_pgoff;
	int off;
	vm_fault_t ret = 0;
#if defined(CONFIG_CONT_PTE_HUGEPAGE) && !CONFIG_CONT_PTE_FAULT_AROUND
		if (vmf_may_cont_pte(vmf))
			return 0;
#endif

	nr_pages = READ_ONCE(fault_around_bytes) >> PAGE_SHIFT;
	mask = ~(nr_pages * PAGE_SIZE - 1) & PAGE_MASK;

	vmf->address = max(address & mask, vmf->vma->vm_start);
	off = ((address - vmf->address) >> PAGE_SHIFT) & (PTRS_PER_PTE - 1);
	start_pgoff -= off;

	/*
	 *  end_pgoff is either the end of the page table, the end of
	 *  the vma or nr_pages from start_pgoff, depending what is nearest.
	 */
	end_pgoff = start_pgoff -
		((vmf->address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1)) +
		PTRS_PER_PTE - 1;
	end_pgoff = min3(end_pgoff, vma_pages(vmf->vma) + vmf->vma->vm_pgoff - 1,
			start_pgoff + nr_pages - 1);

	if (pmd_none(*vmf->pmd)) {
		vmf->prealloc_pte = pte_alloc_one(vmf->vma->vm_mm);
		if (!vmf->prealloc_pte)
			goto out;
		smp_wmb(); /* See comment in __pte_alloc() */
	}
#ifdef CONFIG_CONT_PTE_HUGEPAGE
	if (vmf_may_cont_pte(vmf)) {
		end_pgoff = min(ALIGN_DOWN(start_pgoff + HPAGE_CONT_PTE_NR, HPAGE_CONT_PTE_NR) - 1, end_pgoff);
		return cont_pte_filemap_around(vmf, start_pgoff, end_pgoff, address);
	}
#endif

	vmf->vma->vm_ops->map_pages(vmf, start_pgoff, end_pgoff);

	/* Huge page is mapped? Page fault is solved */
	if (pmd_trans_huge(*vmf->pmd)) {
		ret = VM_FAULT_NOPAGE;
		goto out;
	}

	/* ->map_pages() haven't done anything useful. Cold page cache? */
	if (!vmf->pte)
		goto out;

	/* check if the page fault is solved */
	vmf->pte -= (vmf->address >> PAGE_SHIFT) - (address >> PAGE_SHIFT);
	if (!pte_none(*vmf->pte))
		ret = VM_FAULT_NOPAGE;
	pte_unmap_unlock(vmf->pte, vmf->ptl);
out:
	vmf->address = address;
	vmf->pte = NULL;
	return ret;
}

static vm_fault_t do_read_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	vm_fault_t ret = 0;

	/*
	 * Let's call ->map_pages() first and use ->fault() as fallback
	 * if page by the offset is not ready to be mapped (cold cache or
	 * something).
	 */
	if (vma->vm_ops->map_pages && fault_around_bytes >> PAGE_SHIFT > 1) {
		ret = do_fault_around(vmf);
		if (ret)
			return ret;
	}

	ret = __do_fault(vmf);
	if (unlikely(ret & (VM_FAULT_ERROR | VM_FAULT_NOPAGE | VM_FAULT_RETRY)))
		return ret;

	ret |= finish_fault(vmf);
	unlock_page(vmf->page);
	if (unlikely(ret & (VM_FAULT_ERROR | VM_FAULT_NOPAGE | VM_FAULT_RETRY)))
		put_page(vmf->page);
	return ret;
}

static vm_fault_t do_cow_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	vm_fault_t ret;

	if (unlikely(anon_vma_prepare(vma)))
		return VM_FAULT_OOM;

	vmf->cow_page = alloc_page_vma(GFP_HIGHUSER_MOVABLE, vma, vmf->address);
	if (!vmf->cow_page)
		return VM_FAULT_OOM;

	if (mem_cgroup_try_charge_delay(vmf->cow_page, vma->vm_mm, GFP_KERNEL,
				&vmf->memcg, false)) {
		put_page(vmf->cow_page);
		return VM_FAULT_OOM;
	}

	ret = __do_fault(vmf);
	if (unlikely(ret & (VM_FAULT_ERROR | VM_FAULT_NOPAGE | VM_FAULT_RETRY)))
		goto uncharge_out;
	if (ret & VM_FAULT_DONE_COW)
		return ret;
#ifdef CONFIG_CONT_PTE_HUGEPAGE
		if (ContPteHugePage(vmf->page))
			cont_pte_pagefault_dump(vmf, "COW");
#endif

	copy_user_highpage(vmf->cow_page, vmf->page, vmf->address, vma);
	__SetPageUptodate(vmf->cow_page);

	ret |= finish_fault(vmf);
	unlock_page(vmf->page);
	put_page(vmf->page);
	if (unlikely(ret & (VM_FAULT_ERROR | VM_FAULT_NOPAGE | VM_FAULT_RETRY)))
		goto uncharge_out;
	return ret;
uncharge_out:
	mem_cgroup_cancel_charge(vmf->cow_page, vmf->memcg, false);
	put_page(vmf->cow_page);
	return ret;
}

static vm_fault_t do_shared_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	vm_fault_t ret, tmp;

	ret = __do_fault(vmf);
	if (unlikely(ret & (VM_FAULT_ERROR | VM_FAULT_NOPAGE | VM_FAULT_RETRY)))
		return ret;

	/*
	 * Check if the backing address space wants to know that the page is
	 * about to become writable
	 */
	if (vma->vm_ops->page_mkwrite) {
		unlock_page(vmf->page);
		tmp = do_page_mkwrite(vmf);
		if (unlikely(!tmp ||
				(tmp & (VM_FAULT_ERROR | VM_FAULT_NOPAGE)))) {
			put_page(vmf->page);
			return tmp;
		}
	}

	ret |= finish_fault(vmf);
	if (unlikely(ret & (VM_FAULT_ERROR | VM_FAULT_NOPAGE |
					VM_FAULT_RETRY))) {
		unlock_page(vmf->page);
		put_page(vmf->page);
		return ret;
	}

	ret |= fault_dirty_shared_page(vmf);
	return ret;
}

/*
 * We enter with non-exclusive mmap_sem (to exclude vma changes,
 * but allow concurrent faults).
 * The mmap_sem may have been released depending on flags and our
 * return value.  See filemap_fault() and __lock_page_or_retry().
 * If mmap_sem is released, vma may become invalid (for example
 * by other thread calling munmap()).
 */
static vm_fault_t do_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	struct mm_struct *vm_mm = vma->vm_mm;
	vm_fault_t ret;

	/*
	 * The VMA was not fully populated on mmap() or missing VM_DONTEXPAND
	 */
	if (!vma->vm_ops->fault) {
		/*
		 * If we find a migration pmd entry or a none pmd entry, which
		 * should never happen, return SIGBUS
		 */
		if (unlikely(!pmd_present(*vmf->pmd)))
			ret = VM_FAULT_SIGBUS;
		else {
			vmf->pte = pte_offset_map_lock(vmf->vma->vm_mm,
						       vmf->pmd,
						       vmf->address,
						       &vmf->ptl);
			/*
			 * Make sure this is not a temporary clearing of pte
			 * by holding ptl and checking again. A R/M/W update
			 * of pte involves: take ptl, clearing the pte so that
			 * we don't have concurrent modification by hardware
			 * followed by an update.
			 */
			if (unlikely(pte_none(*vmf->pte)))
				ret = VM_FAULT_SIGBUS;
			else
				ret = VM_FAULT_NOPAGE;

			pte_unmap_unlock(vmf->pte, vmf->ptl);
		}
	} else if (!(vmf->flags & FAULT_FLAG_WRITE))
		ret = do_read_fault(vmf);
	else if (!(vmf->vma_flags & VM_SHARED))
		ret = do_cow_fault(vmf);
	else
		ret = do_shared_fault(vmf);

	/* preallocated pagetable is unused: free it */
	if (vmf->prealloc_pte) {
		pte_free(vm_mm, vmf->prealloc_pte);
		vmf->prealloc_pte = NULL;
	}
	return ret;
}

static int numa_migrate_prep(struct page *page, struct vm_area_struct *vma,
				unsigned long addr, int page_nid,
				int *flags)
{
	get_page(page);

	count_vm_numa_event(NUMA_HINT_FAULTS);
	if (page_nid == numa_node_id()) {
		count_vm_numa_event(NUMA_HINT_FAULTS_LOCAL);
		*flags |= TNF_FAULT_LOCAL;
	}

	return mpol_misplaced(page, vma, addr);
}

static vm_fault_t do_numa_page(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	struct page *page = NULL;
	int page_nid = NUMA_NO_NODE;
	int last_cpupid;
	int target_nid;
	bool migrated = false;
	pte_t pte, old_pte;
	bool was_writable = pte_savedwrite(vmf->orig_pte);
	int flags = 0;

	/*
	 * The "pte" at this point cannot be used safely without
	 * validation through pte_unmap_same(). It's of NUMA type but
	 * the pfn may be screwed if the read is non atomic.
	 */
	if (!pte_spinlock(vmf))
		return VM_FAULT_RETRY;
	if (unlikely(!pte_same(*vmf->pte, vmf->orig_pte))) {
		pte_unmap_unlock(vmf->pte, vmf->ptl);
		goto out;
	}

	/*
	 * Make it present again, Depending on how arch implementes non
	 * accessible ptes, some can allow access by kernel mode.
	 */
	old_pte = ptep_modify_prot_start(vma, vmf->address, vmf->pte);
	pte = pte_modify(old_pte, vmf->vma_page_prot);
	pte = pte_mkyoung(pte);
	if (was_writable)
		pte = pte_mkwrite(pte);
	ptep_modify_prot_commit(vma, vmf->address, vmf->pte, old_pte, pte);
	update_mmu_cache(vma, vmf->address, vmf->pte);

	page = _vm_normal_page(vma, vmf->address, pte, vmf->vma_flags);
	if (!page) {
		pte_unmap_unlock(vmf->pte, vmf->ptl);
		return 0;
	}

	/* TODO: handle PTE-mapped THP */
	if (PageCompound(page)) {
		pte_unmap_unlock(vmf->pte, vmf->ptl);
		return 0;
	}

	/*
	 * Avoid grouping on RO pages in general. RO pages shouldn't hurt as
	 * much anyway since they can be in shared cache state. This misses
	 * the case where a mapping is writable but the process never writes
	 * to it but pte_write gets cleared during protection updates and
	 * pte_dirty has unpredictable behaviour between PTE scan updates,
	 * background writeback, dirty balancing and application behaviour.
	 */
	if (!pte_write(pte))
		flags |= TNF_NO_GROUP;

	/*
	 * Flag if the page is shared between multiple address spaces. This
	 * is later used when determining whether to group tasks together
	 */
	if (page_mapcount(page) > 1 && (vmf->vma_flags & VM_SHARED))
		flags |= TNF_SHARED;

	last_cpupid = page_cpupid_last(page);
	page_nid = page_to_nid(page);
	target_nid = numa_migrate_prep(page, vma, vmf->address, page_nid,
			&flags);
	pte_unmap_unlock(vmf->pte, vmf->ptl);
	if (target_nid == NUMA_NO_NODE) {
		put_page(page);
		goto out;
	}

	/* Migrate to the requested node */
	migrated = migrate_misplaced_page(page, vmf, target_nid);
	if (migrated) {
		page_nid = target_nid;
		flags |= TNF_MIGRATED;
	} else
		flags |= TNF_MIGRATE_FAIL;

out:
	if (page_nid != NUMA_NO_NODE)
		task_numa_fault(last_cpupid, page_nid, 1, flags);
	return 0;
}

static inline vm_fault_t create_huge_pmd(struct vm_fault *vmf)
{
#ifndef CONFIG_CONT_PTE_HUGEPAGE
	if (vma_is_anonymous(vmf->vma))
		return do_huge_pmd_anonymous_page(vmf);
	if (vmf->vma->vm_ops->huge_fault)
		return vmf->vma->vm_ops->huge_fault(vmf, PE_SIZE_PMD);
#endif
	return VM_FAULT_FALLBACK;
}

/* `inline' is required to avoid gcc 4.1.2 build error */
static inline vm_fault_t wp_huge_pmd(struct vm_fault *vmf, pmd_t orig_pmd)
{
	if (vma_is_anonymous(vmf->vma))
		return do_huge_pmd_wp_page(vmf, orig_pmd);
	if (vmf->vma->vm_ops->huge_fault)
		return vmf->vma->vm_ops->huge_fault(vmf, PE_SIZE_PMD);

	/* COW handled on pte level: split pmd */
	VM_BUG_ON_VMA(vmf->vma_flags & VM_SHARED, vmf->vma);
	__split_huge_pmd(vmf->vma, vmf->pmd, vmf->address, false, NULL);

	return VM_FAULT_FALLBACK;
}

static inline bool vma_is_accessible(struct vm_area_struct *vma)
{
	return vma->vm_flags & (VM_READ | VM_EXEC | VM_WRITE);
}

static vm_fault_t create_huge_pud(struct vm_fault *vmf)
{
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	/* No support for anonymous transparent PUD pages yet */
	if (vma_is_anonymous(vmf->vma))
		return VM_FAULT_FALLBACK;
	if (vmf->vma->vm_ops->huge_fault)
		return vmf->vma->vm_ops->huge_fault(vmf, PE_SIZE_PUD);
#endif /* CONFIG_TRANSPARENT_HUGEPAGE */
	return VM_FAULT_FALLBACK;
}

static vm_fault_t wp_huge_pud(struct vm_fault *vmf, pud_t orig_pud)
{
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	/* No support for anonymous transparent PUD pages yet */
	if (vma_is_anonymous(vmf->vma))
		return VM_FAULT_FALLBACK;
	if (vmf->vma->vm_ops->huge_fault)
		return vmf->vma->vm_ops->huge_fault(vmf, PE_SIZE_PUD);
#endif /* CONFIG_TRANSPARENT_HUGEPAGE */
	return VM_FAULT_FALLBACK;
}

/*
 * These routines also need to handle stuff like marking pages dirty
 * and/or accessed for architectures that don't do it in hardware (most
 * RISC architectures).  The early dirtying is also good on the i386.
 *
 * There is also a hook called "update_mmu_cache()" that architectures
 * with external mmu caches can use to update those (ie the Sparc or
 * PowerPC hashed page tables that act as extended TLBs).
 *
 * We enter with non-exclusive mmap_sem (to exclude vma changes, but allow
 * concurrent faults).
 *
 * The mmap_sem may have been released depending on flags and our return value.
 * See filemap_fault() and __lock_page_or_retry().
 */
static vm_fault_t handle_pte_fault(struct vm_fault *vmf)
{
	pte_t entry;
	vm_fault_t ret = 0;

	if (unlikely(pmd_none(*vmf->pmd))) {
		/*
		 * In the case of the speculative page fault handler we abort
		 * the speculative path immediately as the pmd is probably
		 * in the way to be converted in a huge one. We will try
		 * again holding the mmap_sem (which implies that the collapse
		 * operation is done).
		 */
		if (vmf->flags & FAULT_FLAG_SPECULATIVE)
			return VM_FAULT_RETRY;
		/*
		 * Leave __pte_alloc() until later: because vm_ops->fault may
		 * want to allocate huge page, and if we expose page table
		 * for an instant, it will be difficult to retract from
		 * concurrent faults and from rmap lookups.
		 */
		vmf->pte = NULL;
	} else if (!(vmf->flags & FAULT_FLAG_SPECULATIVE)) {
		/* See comment in pte_alloc_one_map() */
		if (pmd_devmap_trans_unstable(vmf->pmd))
			return 0;
		/*
		 * A regular pmd is established and it can't morph into a huge
		 * pmd from under us anymore at this point because we hold the
		 * mmap_sem read mode and khugepaged takes it in write mode.
		 * So now it's safe to run pte_offset_map().
		 * This is not applicable to the speculative page fault handler
		 * but in that case, the pte is fetched earlier in
		 * handle_speculative_fault().
		 */
		vmf->pte = pte_offset_map(vmf->pmd, vmf->address);
		vmf->orig_pte = *vmf->pte;

		/*
		 * some architectures can have larger ptes than wordsize,
		 * e.g.ppc44x-defconfig has CONFIG_PTE_64BIT=y and
		 * CONFIG_32BIT=y, so READ_ONCE cannot guarantee atomic
		 * accesses.  The code below just needs a consistent view
		 * for the ifs and we later double check anyway with the
		 * ptl lock held. So here a barrier will do.
		 */
		barrier();
		if (pte_none(vmf->orig_pte)) {
			pte_unmap(vmf->pte);
			vmf->pte = NULL;
		}
	}

	if (!vmf->pte) {
		if (vma_is_anonymous(vmf->vma))
			return do_anonymous_page(vmf);
		else
			return do_fault(vmf);
	}

	if (!pte_present(vmf->orig_pte))
		return do_swap_page(vmf);

	if (pte_protnone(vmf->orig_pte) && vma_is_accessible(vmf->vma))
		return do_numa_page(vmf);

	if (!pte_spinlock(vmf))
		return VM_FAULT_RETRY;
	entry = vmf->orig_pte;
	if (unlikely(!pte_same(*vmf->pte, entry)))
		goto unlock;
	if (vmf->flags & FAULT_FLAG_WRITE) {
		if (!pte_write(entry))
			return do_wp_page(vmf);
		entry = pte_mkdirty(entry);
	}

	entry = pte_mkyoung(entry);
	if (ptep_set_access_flags(vmf->vma, vmf->address, vmf->pte, entry,
				vmf->flags & FAULT_FLAG_WRITE)) {
		update_mmu_cache(vmf->vma, vmf->address, vmf->pte);
	} else {
		/*
		 * This is needed only for protection faults but the arch code
		 * is not yet telling us if this is a protection fault or not.
		 * This still avoids useless tlb flushes for .text page faults
		 * with threads.
		 */
		if (vmf->flags & FAULT_FLAG_WRITE)
			flush_tlb_fix_spurious_fault(vmf->vma, vmf->address);
		if (vmf->flags & FAULT_FLAG_SPECULATIVE)
			ret = VM_FAULT_RETRY;
	}
unlock:
	pte_unmap_unlock(vmf->pte, vmf->ptl);
	return ret;
}

/*
 * By the time we get here, we already hold the mm semaphore
 *
 * The mmap_sem may have been released depending on flags and our
 * return value.  See filemap_fault() and __lock_page_or_retry().
 */
static vm_fault_t __handle_mm_fault(struct vm_area_struct *vma,
		unsigned long address, unsigned int flags)
{
	struct vm_fault vmf = {
		.vma = vma,
		.address = address & PAGE_MASK,
		.flags = flags,
		.pgoff = linear_page_index(vma, address),
		.gfp_mask = __get_fault_gfp_mask(vma),
		.vma_flags = vma->vm_flags,
		.vma_page_prot = vma->vm_page_prot,
	};
	unsigned int dirty = flags & FAULT_FLAG_WRITE;
	struct mm_struct *mm = vma->vm_mm;
	pgd_t *pgd;
	p4d_t *p4d;
	vm_fault_t ret;

	pgd = pgd_offset(mm, address);
	p4d = p4d_alloc(mm, pgd, address);
	if (!p4d)
		return VM_FAULT_OOM;

	vmf.pud = pud_alloc(mm, p4d, address);
	if (!vmf.pud)
		return VM_FAULT_OOM;
	if (pud_none(*vmf.pud) && __transparent_hugepage_enabled(vma)) {
		ret = create_huge_pud(&vmf);
		if (!(ret & VM_FAULT_FALLBACK))
			return ret;
	} else {
		pud_t orig_pud = *vmf.pud;

		barrier();
		if (pud_trans_huge(orig_pud) || pud_devmap(orig_pud)) {

			/* NUMA case for anonymous PUDs would go here */

			if (dirty && !pud_write(orig_pud)) {
				ret = wp_huge_pud(&vmf, orig_pud);
				if (!(ret & VM_FAULT_FALLBACK))
					return ret;
			} else {
				huge_pud_set_accessed(&vmf, orig_pud);
				return 0;
			}
		}
	}

	vmf.pmd = pmd_alloc(mm, vmf.pud, address);
	if (!vmf.pmd)
		return VM_FAULT_OOM;

#ifdef CONFIG_SPECULATIVE_PAGE_FAULT
	vmf.sequence = raw_read_seqcount(&vma->vm_sequence);
#endif
	if (pmd_none(*vmf.pmd) && __transparent_hugepage_enabled(vma)) {
		ret = create_huge_pmd(&vmf);
		if (!(ret & VM_FAULT_FALLBACK))
			return ret;
	} else {
		pmd_t orig_pmd = *vmf.pmd;

		barrier();
		if (unlikely(is_swap_pmd(orig_pmd))) {
			VM_BUG_ON(thp_migration_supported() &&
					  !is_pmd_migration_entry(orig_pmd));
			if (is_pmd_migration_entry(orig_pmd))
				pmd_migration_entry_wait(mm, vmf.pmd);
			return 0;
		}
		if (pmd_trans_huge(orig_pmd) || pmd_devmap(orig_pmd)) {
			if (pmd_protnone(orig_pmd) && vma_is_accessible(vma))
				return do_huge_pmd_numa_page(&vmf, orig_pmd);

			if (dirty && !pmd_write(orig_pmd)) {
				ret = wp_huge_pmd(&vmf, orig_pmd);
				if (!(ret & VM_FAULT_FALLBACK))
					return ret;
			} else {
				huge_pmd_set_accessed(&vmf, orig_pmd);
				return 0;
			}
		}
	}

	return handle_pte_fault(&vmf);
}

#ifdef CONFIG_SPECULATIVE_PAGE_FAULT

#ifndef CONFIG_ARCH_HAS_PTE_SPECIAL
/* This is required by vm_normal_page() */
#error "Speculative page fault handler requires CONFIG_ARCH_HAS_PTE_SPECIAL"
#endif
/*
 * vm_normal_page() adds some processing which should be done while
 * hodling the mmap_sem.
 */

/*
 * Tries to handle the page fault in a speculative way, without grabbing the
 * mmap_sem.
 * When VM_FAULT_RETRY is returned, the vma pointer is valid and this vma must
 * be checked later when the mmap_sem has been grabbed by calling
 * can_reuse_spf_vma().
 * This is needed as the returned vma is kept in memory until the call to
 * can_reuse_spf_vma() is made.
 */
int __handle_speculative_fault(struct mm_struct *mm, unsigned long address,
			       unsigned int flags, struct vm_area_struct **vma)
{
	struct vm_fault vmf = {
		.address = address,
	};
	pgd_t *pgd, pgdval;
	p4d_t *p4d, p4dval;
	pud_t pudval;
	int seq, ret;

	/* Clear flags that may lead to release the mmap_sem to retry */
	flags &= ~(FAULT_FLAG_ALLOW_RETRY|FAULT_FLAG_KILLABLE);
	flags |= FAULT_FLAG_SPECULATIVE;

	*vma = get_vma(mm, address);
	if (!*vma)
		return VM_FAULT_RETRY;
	vmf.vma = *vma;

	/* rmb <-> seqlock,vma_rb_erase() */
	seq = raw_read_seqcount(&vmf.vma->vm_sequence);
	if (seq & 1)
		return VM_FAULT_RETRY;

	/*
	 * __anon_vma_prepare() requires the mmap_sem to be held
	 * because vm_next and vm_prev must be safe. This can't be guaranteed
	 * in the speculative path.
	 */
	if (unlikely((vma_is_anonymous(vmf.vma) && !vmf.vma->anon_vma) ||
		(!vma_is_anonymous(vmf.vma) &&
			!(vmf.vma->vm_flags & VM_SHARED) &&
			(flags & FAULT_FLAG_WRITE) &&
			!vmf.vma->anon_vma)))
		return VM_FAULT_RETRY;

	vmf.vma_flags = READ_ONCE(vmf.vma->vm_flags);
	vmf.vma_page_prot = READ_ONCE(vmf.vma->vm_page_prot);

	/* Can't call userland page fault handler in the speculative path */
	if (unlikely(vmf.vma_flags & VM_UFFD_MISSING))
		return VM_FAULT_RETRY;

	if (vmf.vma_flags & VM_GROWSDOWN || vmf.vma_flags & VM_GROWSUP)
		/*
		 * This could be detected by the check address against VMA's
		 * boundaries but we want to trace it as not supported instead
		 * of changed.
		 */
		return VM_FAULT_RETRY;

	if (address < READ_ONCE(vmf.vma->vm_start)
	    || READ_ONCE(vmf.vma->vm_end) <= address)
		return VM_FAULT_RETRY;

	if (!arch_vma_access_permitted(vmf.vma, flags & FAULT_FLAG_WRITE,
				       flags & FAULT_FLAG_INSTRUCTION,
				       flags & FAULT_FLAG_REMOTE))
		goto out_segv;

	/* This is one is required to check that the VMA has write access set */
	if (flags & FAULT_FLAG_WRITE) {
		if (unlikely(!(vmf.vma_flags & VM_WRITE)))
			goto out_segv;
	} else if (unlikely(!(vmf.vma_flags & (VM_READ|VM_EXEC|VM_WRITE))))
		goto out_segv;

#ifdef CONFIG_NUMA
	struct mempolicy *pol;

	/*
	 * MPOL_INTERLEAVE implies additional checks in
	 * mpol_misplaced() which are not compatible with the
	 *speculative page fault processing.
	 */
	pol = __get_vma_policy(vmf.vma, address);
	if (!pol)
		pol = get_task_policy(current);
	if (!pol)
		if (pol && pol->mode == MPOL_INTERLEAVE)
			return VM_FAULT_RETRY;
#endif

	/*
	 * Do a speculative lookup of the PTE entry.
	 */
	local_irq_disable();
	pgd = pgd_offset(mm, address);
	pgdval = READ_ONCE(*pgd);
	if (pgd_none(pgdval) || unlikely(pgd_bad(pgdval)))
		goto out_walk;

	p4d = p4d_offset(pgd, address);
	if (pgd_val(READ_ONCE(*pgd)) != pgd_val(pgdval))
		goto out_walk;
	p4dval = READ_ONCE(*p4d);
	if (p4d_none(p4dval) || unlikely(p4d_bad(p4dval)))
		goto out_walk;

	vmf.pud = pud_offset(p4d, address);
	if (p4d_val(READ_ONCE(*p4d)) != p4d_val(p4dval))
		goto out_walk;
	pudval = READ_ONCE(*vmf.pud);
	if (pud_none(pudval) || unlikely(pud_bad(pudval)))
		goto out_walk;

	/* Huge pages at PUD level are not supported. */
	if (unlikely(pud_trans_huge(pudval)))
		goto out_walk;

	vmf.pmd = pmd_offset(vmf.pud, address);
	if (pud_val(READ_ONCE(*vmf.pud)) != pud_val(pudval))
		goto out_walk;
	vmf.orig_pmd = READ_ONCE(*vmf.pmd);
	/*
	 * pmd_none could mean that a hugepage collapse is in progress
	 * in our back as collapse_huge_page() mark it before
	 * invalidating the pte (which is done once the IPI is catched
	 * by all CPU and we have interrupt disabled).
	 * For this reason we cannot handle THP in a speculative way since we
	 * can't safely indentify an in progress collapse operation done in our
	 * back on that PMD.
	 * Regarding the order of the following checks, see comment in
	 * pmd_devmap_trans_unstable()
	 */
	if (unlikely(pmd_devmap(vmf.orig_pmd) ||
		     pmd_none(vmf.orig_pmd) || pmd_trans_huge(vmf.orig_pmd) ||
		     is_swap_pmd(vmf.orig_pmd)))
		goto out_walk;

	/*
	 * The above does not allocate/instantiate page-tables because doing so
	 * would lead to the possibility of instantiating page-tables after
	 * free_pgtables() -- and consequently leaking them.
	 *
	 * The result is that we take at least one !speculative fault per PMD
	 * in order to instantiate it.
	 */

	vmf.pte = pte_offset_map(vmf.pmd, address);
	if (pmd_val(READ_ONCE(*vmf.pmd)) != pmd_val(vmf.orig_pmd)) {
		pte_unmap(vmf.pte);
		vmf.pte = NULL;
		goto out_walk;
	}
	vmf.orig_pte = READ_ONCE(*vmf.pte);
	barrier(); /* See comment in handle_pte_fault() */
	if (pte_none(vmf.orig_pte)) {
		pte_unmap(vmf.pte);
		vmf.pte = NULL;
	}

	vmf.pgoff = linear_page_index(vmf.vma, address);
	vmf.gfp_mask = __get_fault_gfp_mask(vmf.vma);
	vmf.sequence = seq;
	vmf.flags = flags;

	local_irq_enable();

	/*
	 * We need to re-validate the VMA after checking the bounds, otherwise
	 * we might have a false positive on the bounds.
	 */
	if (read_seqcount_retry(&vmf.vma->vm_sequence, seq))
		return VM_FAULT_RETRY;

	mem_cgroup_enter_user_fault();
	ret = handle_pte_fault(&vmf);
	mem_cgroup_exit_user_fault();

	/*
	 * If there is no need to retry, don't return the vma to the caller.
	 */
	if (ret != VM_FAULT_RETRY) {
		check_sync_rss_stat(current);
		if (vma_is_anonymous(vmf.vma))
			count_vm_event(SPECULATIVE_PGFAULT_ANON);
		else
			count_vm_event(SPECULATIVE_PGFAULT_FILE);
		put_vma(vmf.vma);
		*vma = NULL;
	}

	/*
	 * The task may have entered a memcg OOM situation but
	 * if the allocation error was handled gracefully (no
	 * VM_FAULT_OOM), there is no need to kill anything.
	 * Just clean up the OOM state peacefully.
	 */
	if (task_in_memcg_oom(current) && !(ret & VM_FAULT_OOM))
		mem_cgroup_oom_synchronize(false);
	return ret;

out_walk:
	local_irq_enable();
	return VM_FAULT_RETRY;

out_segv:
	/*
	 * We don't return VM_FAULT_RETRY so the caller is not expected to
	 * retrieve the fetched VMA.
	 */
	put_vma(vmf.vma);
	*vma = NULL;
	return VM_FAULT_SIGSEGV;
}

/*
 * This is used to know if the vma fetch in the speculative page fault handler
 * is still valid when trying the regular fault path while holding the
 * mmap_sem.
 * The call to put_vma(vma) must be made after checking the vma's fields, as
 * the vma may be freed by put_vma(). In such a case it is expected that false
 * is returned.
 */
bool can_reuse_spf_vma(struct vm_area_struct *vma, unsigned long address)
{
	bool ret;

	ret = !RB_EMPTY_NODE(&vma->vm_rb) &&
		vma->vm_start <= address && address < vma->vm_end;
	put_vma(vma);
	return ret;
}
#endif /* CONFIG_SPECULATIVE_PAGE_FAULT */

/*
 * By the time we get here, we already hold the mm semaphore
 *
 * The mmap_sem may have been released depending on flags and our
 * return value.  See filemap_fault() and __lock_page_or_retry().
 */
vm_fault_t handle_mm_fault(struct vm_area_struct *vma, unsigned long address,
		unsigned int flags)
{
	vm_fault_t ret;

	__set_current_state(TASK_RUNNING);

	count_vm_event(PGFAULT);
	count_memcg_event_mm(vma->vm_mm, PGFAULT);

	/* do counter updates before entering really critical section. */
	check_sync_rss_stat(current);

	if (!arch_vma_access_permitted(vma, flags & FAULT_FLAG_WRITE,
					    flags & FAULT_FLAG_INSTRUCTION,
					    flags & FAULT_FLAG_REMOTE))
		return VM_FAULT_SIGSEGV;

	/*
	 * Enable the memcg OOM handling for faults triggered in user
	 * space.  Kernel faults are handled more gracefully.
	 */
	if (flags & FAULT_FLAG_USER)
		mem_cgroup_enter_user_fault();

	if (unlikely(is_vm_hugetlb_page(vma)))
		ret = hugetlb_fault(vma->vm_mm, vma, address, flags);
	else
		ret = __handle_mm_fault(vma, address, flags);

	if (flags & FAULT_FLAG_USER) {
		mem_cgroup_exit_user_fault();
		/*
		 * The task may have entered a memcg OOM situation but
		 * if the allocation error was handled gracefully (no
		 * VM_FAULT_OOM), there is no need to kill anything.
		 * Just clean up the OOM state peacefully.
		 */
		if (task_in_memcg_oom(current) && !(ret & VM_FAULT_OOM))
			mem_cgroup_oom_synchronize(false);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(handle_mm_fault);

#ifndef __PAGETABLE_P4D_FOLDED
/*
 * Allocate p4d page table.
 * We've already handled the fast-path in-line.
 */
int __p4d_alloc(struct mm_struct *mm, pgd_t *pgd, unsigned long address)
{
	p4d_t *new = p4d_alloc_one(mm, address);
	if (!new)
		return -ENOMEM;

	smp_wmb(); /* See comment in __pte_alloc */

	spin_lock(&mm->page_table_lock);
	if (pgd_present(*pgd))		/* Another has populated it */
		p4d_free(mm, new);
	else
		pgd_populate(mm, pgd, new);
	spin_unlock(&mm->page_table_lock);
	return 0;
}
#endif /* __PAGETABLE_P4D_FOLDED */

#ifndef __PAGETABLE_PUD_FOLDED
/*
 * Allocate page upper directory.
 * We've already handled the fast-path in-line.
 */
int __pud_alloc(struct mm_struct *mm, p4d_t *p4d, unsigned long address)
{
	pud_t *new = pud_alloc_one(mm, address);
	if (!new)
		return -ENOMEM;

	smp_wmb(); /* See comment in __pte_alloc */

	spin_lock(&mm->page_table_lock);
#ifndef __ARCH_HAS_5LEVEL_HACK
	if (!p4d_present(*p4d)) {
		mm_inc_nr_puds(mm);
		p4d_populate(mm, p4d, new);
	} else	/* Another has populated it */
		pud_free(mm, new);
#else
	if (!pgd_present(*p4d)) {
		mm_inc_nr_puds(mm);
		pgd_populate(mm, p4d, new);
	} else	/* Another has populated it */
		pud_free(mm, new);
#endif /* __ARCH_HAS_5LEVEL_HACK */
	spin_unlock(&mm->page_table_lock);
	return 0;
}
#endif /* __PAGETABLE_PUD_FOLDED */

#ifndef __PAGETABLE_PMD_FOLDED
/*
 * Allocate page middle directory.
 * We've already handled the fast-path in-line.
 */
int __pmd_alloc(struct mm_struct *mm, pud_t *pud, unsigned long address)
{
	spinlock_t *ptl;
	pmd_t *new = pmd_alloc_one(mm, address);
	if (!new)
		return -ENOMEM;

	smp_wmb(); /* See comment in __pte_alloc */

	ptl = pud_lock(mm, pud);
#ifndef __ARCH_HAS_4LEVEL_HACK
	if (!pud_present(*pud)) {
		mm_inc_nr_pmds(mm);
		pud_populate(mm, pud, new);
	} else	/* Another has populated it */
		pmd_free(mm, new);
#else
	if (!pgd_present(*pud)) {
		mm_inc_nr_pmds(mm);
		pgd_populate(mm, pud, new);
	} else /* Another has populated it */
		pmd_free(mm, new);
#endif /* __ARCH_HAS_4LEVEL_HACK */
	spin_unlock(ptl);
	return 0;
}
#endif /* __PAGETABLE_PMD_FOLDED */

int follow_invalidate_pte(struct mm_struct *mm, unsigned long address,
			  struct mmu_notifier_range *range, pte_t **ptepp,
			  pmd_t **pmdpp, spinlock_t **ptlp)
{
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *ptep;

	pgd = pgd_offset(mm, address);
	if (pgd_none(*pgd) || unlikely(pgd_bad(*pgd)))
		goto out;

	p4d = p4d_offset(pgd, address);
	if (p4d_none(*p4d) || unlikely(p4d_bad(*p4d)))
		goto out;

	pud = pud_offset(p4d, address);
	if (pud_none(*pud) || unlikely(pud_bad(*pud)))
		goto out;

	pmd = pmd_offset(pud, address);
	VM_BUG_ON(pmd_trans_huge(*pmd));

	if (pmd_huge(*pmd)) {
		if (!pmdpp)
			goto out;

		if (range) {
			mmu_notifier_range_init(range, MMU_NOTIFY_CLEAR, 0,
						NULL, mm, address & PMD_MASK,
						(address & PMD_MASK) + PMD_SIZE);
			mmu_notifier_invalidate_range_start(range);
		}
		*ptlp = pmd_lock(mm, pmd);
		if (pmd_huge(*pmd)) {
			*pmdpp = pmd;
			return 0;
		}
		spin_unlock(*ptlp);
		if (range)
			mmu_notifier_invalidate_range_end(range);
	}

	if (pmd_none(*pmd) || unlikely(pmd_bad(*pmd)))
		goto out;

	if (range) {
		mmu_notifier_range_init(range, MMU_NOTIFY_CLEAR, 0, NULL, mm,
					address & PAGE_MASK,
					(address & PAGE_MASK) + PAGE_SIZE);
		mmu_notifier_invalidate_range_start(range);
	}
	ptep = pte_offset_map_lock(mm, pmd, address, ptlp);
	if (!pte_present(*ptep))
		goto unlock;
	*ptepp = ptep;
	return 0;
unlock:
	pte_unmap_unlock(ptep, *ptlp);
	if (range)
		mmu_notifier_invalidate_range_end(range);
out:
	return -EINVAL;
}

/**
 * follow_pte - look up PTE at a user virtual address
 * @mm: the mm_struct of the target address space
 * @address: user virtual address
 * @ptepp: location to store found PTE
 * @ptlp: location to store the lock for the PTE
 *
 * On a successful return, the pointer to the PTE is stored in @ptepp;
 * the corresponding lock is taken and its location is stored in @ptlp.
 * The contents of the PTE are only stable until @ptlp is released;
 * any further use, if any, must be protected against invalidation
 * with MMU notifiers.
 *
 * Only IO mappings and raw PFN mappings are allowed.  The mmap semaphore
 * should be taken for read.
 *
 * KVM uses this function.  While it is arguably less bad than ``follow_pfn``,
 * it is not a good general-purpose API.
 *
 * Return: zero on success, -ve otherwise.
 */
int follow_pte(struct mm_struct *mm, unsigned long address,
	       pte_t **ptepp, spinlock_t **ptlp)
{
	return follow_invalidate_pte(mm, address, NULL, ptepp, NULL, ptlp);
}
EXPORT_SYMBOL_GPL(follow_pte);

/**
 * follow_pfn - look up PFN at a user virtual address
 * @vma: memory mapping
 * @address: user virtual address
 * @pfn: location to store found PFN
 *
 * Only IO mappings and raw PFN mappings are allowed.
 *
 * This function does not allow the caller to read the permissions
 * of the PTE.  Do not use it.
 *
 * Return: zero and the pfn at @pfn on success, -ve otherwise.
 */
int follow_pfn(struct vm_area_struct *vma, unsigned long address,
	unsigned long *pfn)
{
	int ret = -EINVAL;
	spinlock_t *ptl;
	pte_t *ptep;

	if (!(vma->vm_flags & (VM_IO | VM_PFNMAP)))
		return ret;

	ret = follow_pte(vma->vm_mm, address, &ptep, &ptl);
	if (ret)
		return ret;
	*pfn = pte_pfn(*ptep);
	pte_unmap_unlock(ptep, ptl);
	return 0;
}
EXPORT_SYMBOL(follow_pfn);

#ifdef CONFIG_HAVE_IOREMAP_PROT
int follow_phys(struct vm_area_struct *vma,
		unsigned long address, unsigned int flags,
		unsigned long *prot, resource_size_t *phys)
{
	int ret = -EINVAL;
	pte_t *ptep, pte;
	spinlock_t *ptl;

	if (!(vma->vm_flags & (VM_IO | VM_PFNMAP)))
		goto out;

	if (follow_pte(vma->vm_mm, address, &ptep, &ptl))
		goto out;
	pte = *ptep;

	if ((flags & FOLL_WRITE) && !pte_write(pte))
		goto unlock;

	*prot = pgprot_val(pte_pgprot(pte));
	*phys = (resource_size_t)pte_pfn(pte) << PAGE_SHIFT;

	ret = 0;
unlock:
	pte_unmap_unlock(ptep, ptl);
out:
	return ret;
}

int generic_access_phys(struct vm_area_struct *vma, unsigned long addr,
			void *buf, int len, int write)
{
	resource_size_t phys_addr;
	unsigned long prot = 0;
	void __iomem *maddr;
	int offset = addr & (PAGE_SIZE-1);

	if (follow_phys(vma, addr, write, &prot, &phys_addr))
		return -EINVAL;

	maddr = ioremap_prot(phys_addr, PAGE_ALIGN(len + offset), prot);
	if (!maddr)
		return -ENOMEM;

	if (write)
		memcpy_toio(maddr + offset, buf, len);
	else
		memcpy_fromio(buf, maddr + offset, len);
	iounmap(maddr);

	return len;
}
EXPORT_SYMBOL_GPL(generic_access_phys);
#endif

/*
 * Access another process' address space as given in mm.  If non-NULL, use the
 * given task for page fault accounting.
 */
int __access_remote_vm(struct task_struct *tsk, struct mm_struct *mm,
		unsigned long addr, void *buf, int len, unsigned int gup_flags)
{
	struct vm_area_struct *vma;
	void *old_buf = buf;
	int write = gup_flags & FOLL_WRITE;

	if (down_read_killable(&mm->mmap_sem))
		return 0;

	/* ignore errors, just check how much was successfully transferred */
	while (len) {
		int bytes, ret, offset;
		void *maddr;
		struct page *page = NULL;

		ret = get_user_pages_remote(tsk, mm, addr, 1,
				gup_flags, &page, &vma, NULL);
		if (ret <= 0) {
#ifndef CONFIG_HAVE_IOREMAP_PROT
			break;
#else
			/*
			 * Check if this is a VM_IO | VM_PFNMAP VMA, which
			 * we can access using slightly different code.
			 */
			vma = find_vma(mm, addr);
			if (!vma || vma->vm_start > addr)
				break;
			if (vma->vm_ops && vma->vm_ops->access)
				ret = vma->vm_ops->access(vma, addr, buf,
							  len, write);
			if (ret <= 0)
				break;
			bytes = ret;
#endif
		} else {
			bytes = len;
			offset = addr & (PAGE_SIZE-1);
			if (bytes > PAGE_SIZE-offset)
				bytes = PAGE_SIZE-offset;

			maddr = kmap(page);
			if (write) {
				copy_to_user_page(vma, page, addr,
						  maddr + offset, buf, bytes);
				set_page_dirty_lock(page);
			} else {
				copy_from_user_page(vma, page, addr,
						    buf, maddr + offset, bytes);
			}
			kunmap(page);
			put_page(page);
		}
		len -= bytes;
		buf += bytes;
		addr += bytes;
	}
	up_read(&mm->mmap_sem);

	return buf - old_buf;
}

/**
 * access_remote_vm - access another process' address space
 * @mm:		the mm_struct of the target address space
 * @addr:	start address to access
 * @buf:	source or destination buffer
 * @len:	number of bytes to transfer
 * @gup_flags:	flags modifying lookup behaviour
 *
 * The caller must hold a reference on @mm.
 *
 * Return: number of bytes copied from source to destination.
 */
int access_remote_vm(struct mm_struct *mm, unsigned long addr,
		void *buf, int len, unsigned int gup_flags)
{
	return __access_remote_vm(NULL, mm, addr, buf, len, gup_flags);
}

/*
 * Access another process' address space.
 * Source/target buffer must be kernel space,
 * Do not walk the page table directly, use get_user_pages
 */
int access_process_vm(struct task_struct *tsk, unsigned long addr,
		void *buf, int len, unsigned int gup_flags)
{
	struct mm_struct *mm;
	int ret;

	mm = get_task_mm(tsk);
	if (!mm)
		return 0;

	ret = __access_remote_vm(tsk, mm, addr, buf, len, gup_flags);

	mmput(mm);

	return ret;
}
EXPORT_SYMBOL_GPL(access_process_vm);

/*
 * Print the name of a VMA.
 */
void print_vma_addr(char *prefix, unsigned long ip)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;

	/*
	 * we might be running from an atomic context so we cannot sleep
	 */
	if (!down_read_trylock(&mm->mmap_sem))
		return;

	vma = find_vma(mm, ip);
	if (vma && vma->vm_file) {
		struct file *f = vma->vm_file;
		char *buf = (char *)__get_free_page(GFP_NOWAIT);
		if (buf) {
			char *p;

			p = file_path(f, buf, PAGE_SIZE);
			if (IS_ERR(p))
				p = "?";
			printk("%s%s[%lx+%lx]", prefix, kbasename(p),
					vma->vm_start,
					vma->vm_end - vma->vm_start);
			free_page((unsigned long)buf);
		}
	}
	up_read(&mm->mmap_sem);
}

#if defined(CONFIG_PROVE_LOCKING) || defined(CONFIG_DEBUG_ATOMIC_SLEEP)
void __might_fault(const char *file, int line)
{
	/*
	 * Some code (nfs/sunrpc) uses socket ops on kernel memory while
	 * holding the mmap_sem, this is safe because kernel memory doesn't
	 * get paged out, therefore we'll never actually fault, and the
	 * below annotations will generate false positives.
	 */
	if (uaccess_kernel())
		return;
	if (pagefault_disabled())
		return;
	__might_sleep(file, line, 0);
#if defined(CONFIG_DEBUG_ATOMIC_SLEEP)
	if (current->mm)
		might_lock_read(&current->mm->mmap_sem);
#endif
}
EXPORT_SYMBOL(__might_fault);
#endif

#if defined(CONFIG_TRANSPARENT_HUGEPAGE) || defined(CONFIG_HUGETLBFS)
/*
 * Process all subpages of the specified huge page with the specified
 * operation.  The target subpage will be processed last to keep its
 * cache lines hot.
 */
static inline void process_huge_page(
	unsigned long addr_hint, unsigned int pages_per_huge_page,
	void (*process_subpage)(unsigned long addr, int idx, void *arg),
	void *arg)
{
	int i, n, base, l;
	unsigned long addr = addr_hint &
		~(((unsigned long)pages_per_huge_page << PAGE_SHIFT) - 1);

	/* Process target subpage last to keep its cache lines hot */
	might_sleep();
	n = (addr_hint - addr) / PAGE_SIZE;
	if (2 * n <= pages_per_huge_page) {
		/* If target subpage in first half of huge page */
		base = 0;
		l = n;
		/* Process subpages at the end of huge page */
		for (i = pages_per_huge_page - 1; i >= 2 * n; i--) {
			cond_resched();
			process_subpage(addr + i * PAGE_SIZE, i, arg);
		}
	} else {
		/* If target subpage in second half of huge page */
		base = pages_per_huge_page - 2 * (pages_per_huge_page - n);
		l = pages_per_huge_page - n;
		/* Process subpages at the begin of huge page */
		for (i = 0; i < base; i++) {
			cond_resched();
			process_subpage(addr + i * PAGE_SIZE, i, arg);
		}
	}
	/*
	 * Process remaining subpages in left-right-left-right pattern
	 * towards the target subpage
	 */
	for (i = 0; i < l; i++) {
		int left_idx = base + i;
		int right_idx = base + 2 * l - 1 - i;

		cond_resched();
		process_subpage(addr + left_idx * PAGE_SIZE, left_idx, arg);
		cond_resched();
		process_subpage(addr + right_idx * PAGE_SIZE, right_idx, arg);
	}
}

static void clear_gigantic_page(struct page *page,
				unsigned long addr,
				unsigned int pages_per_huge_page)
{
	int i;
	struct page *p = page;

	might_sleep();
	for (i = 0; i < pages_per_huge_page;
	     i++, p = mem_map_next(p, page, i)) {
		cond_resched();
		clear_user_highpage(p, addr + i * PAGE_SIZE);
	}
}

static void clear_subpage(unsigned long addr, int idx, void *arg)
{
	struct page *page = arg;

	clear_user_highpage(page + idx, addr);
}

void clear_huge_page(struct page *page,
		     unsigned long addr_hint, unsigned int pages_per_huge_page)
{
	unsigned long addr = addr_hint &
		~(((unsigned long)pages_per_huge_page << PAGE_SHIFT) - 1);

	if (unlikely(pages_per_huge_page > MAX_ORDER_NR_PAGES)) {
		clear_gigantic_page(page, addr, pages_per_huge_page);
		return;
	}

	process_huge_page(addr_hint, pages_per_huge_page, clear_subpage, page);
}

static void copy_user_gigantic_page(struct page *dst, struct page *src,
				    unsigned long addr,
				    struct vm_area_struct *vma,
				    unsigned int pages_per_huge_page)
{
	int i;
	struct page *dst_base = dst;
	struct page *src_base = src;

	for (i = 0; i < pages_per_huge_page; ) {
		cond_resched();
		copy_user_highpage(dst, src, addr + i*PAGE_SIZE, vma);

		i++;
		dst = mem_map_next(dst, dst_base, i);
		src = mem_map_next(src, src_base, i);
	}
}

struct copy_subpage_arg {
	struct page *dst;
	struct page *src;
	struct vm_area_struct *vma;
};

static void copy_subpage(unsigned long addr, int idx, void *arg)
{
	struct copy_subpage_arg *copy_arg = arg;

	copy_user_highpage(copy_arg->dst + idx, copy_arg->src + idx,
			   addr, copy_arg->vma);
}

void copy_user_huge_page(struct page *dst, struct page *src,
			 unsigned long addr_hint, struct vm_area_struct *vma,
			 unsigned int pages_per_huge_page)
{
	unsigned long addr = addr_hint &
		~(((unsigned long)pages_per_huge_page << PAGE_SHIFT) - 1);
	struct copy_subpage_arg arg = {
		.dst = dst,
		.src = src,
		.vma = vma,
	};

	if (unlikely(pages_per_huge_page > MAX_ORDER_NR_PAGES)) {
		copy_user_gigantic_page(dst, src, addr, vma,
					pages_per_huge_page);
		return;
	}

	process_huge_page(addr_hint, pages_per_huge_page, copy_subpage, &arg);
}

long copy_huge_page_from_user(struct page *dst_page,
				const void __user *usr_src,
				unsigned int pages_per_huge_page,
				bool allow_pagefault)
{
	void *src = (void *)usr_src;
	void *page_kaddr;
	unsigned long i, rc = 0;
	unsigned long ret_val = pages_per_huge_page * PAGE_SIZE;
	struct page *subpage = dst_page;

	for (i = 0; i < pages_per_huge_page;
	     i++, subpage = mem_map_next(subpage, dst_page, i)) {
		if (allow_pagefault)
			page_kaddr = kmap(subpage);
		else
			page_kaddr = kmap_atomic(subpage);
		rc = copy_from_user(page_kaddr,
				(const void __user *)(src + i * PAGE_SIZE),
				PAGE_SIZE);
		if (allow_pagefault)
			kunmap(subpage);
		else
			kunmap_atomic(page_kaddr);

		ret_val -= (PAGE_SIZE - rc);
		if (rc)
			break;

		flush_dcache_page(subpage);

		cond_resched();
	}
	return ret_val;
}
#endif /* CONFIG_TRANSPARENT_HUGEPAGE || CONFIG_HUGETLBFS */

#if USE_SPLIT_PTE_PTLOCKS && ALLOC_SPLIT_PTLOCKS

static struct kmem_cache *page_ptl_cachep;

void __init ptlock_cache_init(void)
{
	page_ptl_cachep = kmem_cache_create("page->ptl", sizeof(spinlock_t), 0,
			SLAB_PANIC, NULL);
}

bool ptlock_alloc(struct page *page)
{
	spinlock_t *ptl;

	ptl = kmem_cache_alloc(page_ptl_cachep, GFP_KERNEL);
	if (!ptl)
		return false;
	page->ptl = ptl;
	return true;
}

void ptlock_free(struct page *page)
{
	kmem_cache_free(page_ptl_cachep, page->ptl);
}
#endif
