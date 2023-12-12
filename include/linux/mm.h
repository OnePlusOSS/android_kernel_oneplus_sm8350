/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_MM_H
#define _LINUX_MM_H

#include <linux/errno.h>

#ifdef __KERNEL__

#include <linux/mmdebug.h>
#include <linux/gfp.h>
#include <linux/bug.h>
#include <linux/list.h>
#include <linux/mmzone.h>
#include <linux/rbtree.h>
#include <linux/atomic.h>
#include <linux/debug_locks.h>
#include <linux/mm_types.h>
#include <linux/mmap_lock.h>
#include <linux/range.h>
#include <linux/pfn.h>
#include <linux/percpu-refcount.h>
#include <linux/bit_spinlock.h>
#include <linux/shrinker.h>
#include <linux/resource.h>
#include <linux/page_ext.h>
#include <linux/err.h>
#include <linux/page_ref.h>
#include <linux/memremap.h>
#include <linux/overflow.h>
#include <linux/sizes.h>
#include <linux/android_kabi.h>
#include <linux/android_vendor.h>

struct mempolicy;
struct anon_vma;
struct anon_vma_chain;
struct file_ra_state;
struct user_struct;
struct writeback_control;
struct bdi_writeback;

#if defined(CONFIG_CONT_PTE_HUGEPAGE)
struct swap_info_struct;
#define may_cont_pte android_kabi_reserved1 /* struct inode */
#endif

void init_mm_internals(void);

#ifndef CONFIG_NEED_MULTIPLE_NODES	/* Don't use mapnrs, do it properly */
extern unsigned long max_mapnr;

static inline void set_max_mapnr(unsigned long limit)
{
	max_mapnr = limit;
}
#else
static inline void set_max_mapnr(unsigned long limit) { }
#endif

extern atomic_long_t _totalram_pages;
static inline unsigned long totalram_pages(void)
{
	return (unsigned long)atomic_long_read(&_totalram_pages);
}

static inline void totalram_pages_inc(void)
{
	atomic_long_inc(&_totalram_pages);
}

static inline void totalram_pages_dec(void)
{
	atomic_long_dec(&_totalram_pages);
}

static inline void totalram_pages_add(long count)
{
	atomic_long_add(count, &_totalram_pages);
}

static inline void totalram_pages_set(long val)
{
	atomic_long_set(&_totalram_pages, val);
}

extern void * high_memory;
extern int page_cluster;

#ifdef CONFIG_SYSCTL
extern int sysctl_legacy_va_layout;
#else
#define sysctl_legacy_va_layout 0
#endif

#ifdef CONFIG_HAVE_ARCH_MMAP_RND_BITS
extern const int mmap_rnd_bits_min;
extern const int mmap_rnd_bits_max;
extern int mmap_rnd_bits __read_mostly;
#endif
#ifdef CONFIG_HAVE_ARCH_MMAP_RND_COMPAT_BITS
extern const int mmap_rnd_compat_bits_min;
extern const int mmap_rnd_compat_bits_max;
extern int mmap_rnd_compat_bits __read_mostly;
#endif

#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/processor.h>

/*
 * Architectures that support memory tagging (assigning tags to memory regions,
 * embedding these tags into addresses that point to these memory regions, and
 * checking that the memory and the pointer tags match on memory accesses)
 * redefine this macro to strip tags from pointers.
 * It's defined as noop for arcitectures that don't support memory tagging.
 */
#ifndef untagged_addr
#define untagged_addr(addr) (addr)
#endif

#ifndef __pa_symbol
#define __pa_symbol(x)  __pa(RELOC_HIDE((unsigned long)(x), 0))
#endif

#ifndef __va_function
#define __va_function(x) (x)
#endif

#ifndef __pa_function
#define __pa_function(x) __pa_symbol(x)
#endif

#ifndef page_to_virt
#define page_to_virt(x)	__va(PFN_PHYS(page_to_pfn(x)))
#endif

#ifndef lm_alias
#define lm_alias(x)	__va(__pa_symbol(x))
#endif

/*
 * To prevent common memory management code establishing
 * a zero page mapping on a read fault.
 * This macro should be defined within <asm/pgtable.h>.
 * s390 does this to prevent multiplexing of hardware bits
 * related to the physical page in case of virtualization.
 */
#ifndef mm_forbids_zeropage
#define mm_forbids_zeropage(X)	(0)
#endif

/*
 * On some architectures it is expensive to call memset() for small sizes.
 * If an architecture decides to implement their own version of
 * mm_zero_struct_page they should wrap the defines below in a #ifndef and
 * define their own version of this macro in <asm/pgtable.h>
 */
#if BITS_PER_LONG == 64
/* This function must be updated when the size of struct page grows above 80
 * or reduces below 56. The idea that compiler optimizes out switch()
 * statement, and only leaves move/store instructions. Also the compiler can
 * combine write statments if they are both assignments and can be reordered,
 * this can result in several of the writes here being dropped.
 */
#define	mm_zero_struct_page(pp) __mm_zero_struct_page(pp)
static inline void __mm_zero_struct_page(struct page *page)
{
	unsigned long *_pp = (void *)page;

	 /* Check that struct page is either 56, 64, 72, or 80 bytes */
	BUILD_BUG_ON(sizeof(struct page) & 7);
	BUILD_BUG_ON(sizeof(struct page) < 56);
	BUILD_BUG_ON(sizeof(struct page) > 80);

	switch (sizeof(struct page)) {
	case 80:
		_pp[9] = 0;	/* fallthrough */
	case 72:
		_pp[8] = 0;	/* fallthrough */
	case 64:
		_pp[7] = 0;	/* fallthrough */
	case 56:
		_pp[6] = 0;
		_pp[5] = 0;
		_pp[4] = 0;
		_pp[3] = 0;
		_pp[2] = 0;
		_pp[1] = 0;
		_pp[0] = 0;
	}
}
#else
#define mm_zero_struct_page(pp)  ((void)memset((pp), 0, sizeof(struct page)))
#endif

/*
 * Default maximum number of active map areas, this limits the number of vmas
 * per mm struct. Users can overwrite this number by sysctl but there is a
 * problem.
 *
 * When a program's coredump is generated as ELF format, a section is created
 * per a vma. In ELF, the number of sections is represented in unsigned short.
 * This means the number of sections should be smaller than 65535 at coredump.
 * Because the kernel adds some informative sections to a image of program at
 * generating coredump, we need some margin. The number of extra sections is
 * 1-3 now and depends on arch. We use "5" as safe margin, here.
 *
 * ELF extended numbering allows more than 65535 sections, so 16-bit bound is
 * not a hard limit any more. Although some userspace tools can be surprised by
 * that.
 */
#define MAPCOUNT_ELF_CORE_MARGIN	(5)
#define DEFAULT_MAX_MAP_COUNT	(USHRT_MAX - MAPCOUNT_ELF_CORE_MARGIN)

extern int sysctl_max_map_count;

extern unsigned long sysctl_user_reserve_kbytes;
extern unsigned long sysctl_admin_reserve_kbytes;

extern int sysctl_overcommit_memory;
extern int sysctl_overcommit_ratio;
extern unsigned long sysctl_overcommit_kbytes;

extern int overcommit_ratio_handler(struct ctl_table *, int, void __user *,
				    size_t *, loff_t *);
extern int overcommit_kbytes_handler(struct ctl_table *, int, void __user *,
				    size_t *, loff_t *);

#define nth_page(page,n) pfn_to_page(page_to_pfn((page)) + (n))

/* to align the pointer to the (next) page boundary */
#define PAGE_ALIGN(addr) ALIGN(addr, PAGE_SIZE)

/* test whether an address (unsigned long or pointer) is aligned to PAGE_SIZE */
#define PAGE_ALIGNED(addr)	IS_ALIGNED((unsigned long)(addr), PAGE_SIZE)

#define lru_to_page(head) (list_entry((head)->prev, struct page, lru))

/*
 * Linux kernel virtual memory manager primitives.
 * The idea being to have a "virtual" mm in the same way
 * we have a virtual fs - giving a cleaner interface to the
 * mm details, and allowing different kinds of memory mappings
 * (from shared memory to executable loading to arbitrary
 * mmap() functions).
 */

struct vm_area_struct *vm_area_alloc(struct mm_struct *);
struct vm_area_struct *vm_area_dup(struct vm_area_struct *);
void vm_area_free(struct vm_area_struct *);

#ifndef CONFIG_MMU
extern struct rb_root nommu_region_tree;
extern struct rw_semaphore nommu_region_sem;

extern unsigned int kobjsize(const void *objp);
#endif

/*
 * vm_flags in vm_area_struct, see mm_types.h.
 * When changing, update also include/trace/events/mmflags.h
 */
#define VM_NONE		0x00000000

#define VM_READ		0x00000001	/* currently active flags */
#define VM_WRITE	0x00000002
#define VM_EXEC		0x00000004
#define VM_SHARED	0x00000008

/* mprotect() hardcodes VM_MAYREAD >> 4 == VM_READ, and so for r/w/x bits. */
#define VM_MAYREAD	0x00000010	/* limits for mprotect() etc */
#define VM_MAYWRITE	0x00000020
#define VM_MAYEXEC	0x00000040
#define VM_MAYSHARE	0x00000080

#define VM_GROWSDOWN	0x00000100	/* general info on the segment */
#define VM_UFFD_MISSING	0x00000200	/* missing pages tracking */
#define VM_PFNMAP	0x00000400	/* Page-ranges managed without "struct page", just pure PFN */
#define VM_DENYWRITE	0x00000800	/* ETXTBSY on write attempts.. */
#define VM_UFFD_WP	0x00001000	/* wrprotect pages tracking */

#define VM_LOCKED	0x00002000
#define VM_IO           0x00004000	/* Memory mapped I/O or similar */

					/* Used by sys_madvise() */
#define VM_SEQ_READ	0x00008000	/* App will access data sequentially */
#define VM_RAND_READ	0x00010000	/* App will not benefit from clustered reads */

#define VM_DONTCOPY	0x00020000      /* Do not copy this vma on fork */
#define VM_DONTEXPAND	0x00040000	/* Cannot expand with mremap() */
#define VM_LOCKONFAULT	0x00080000	/* Lock the pages covered when they are faulted in */
#define VM_ACCOUNT	0x00100000	/* Is a VM accounted object */
#define VM_NORESERVE	0x00200000	/* should the VM suppress accounting */
#define VM_HUGETLB	0x00400000	/* Huge TLB Page VM */
#define VM_SYNC		0x00800000	/* Synchronous page faults */
#define VM_ARCH_1	0x01000000	/* Architecture-specific flag */
#define VM_WIPEONFORK	0x02000000	/* Wipe VMA contents in child. */
#define VM_DONTDUMP	0x04000000	/* Do not include in the core dump */

#ifdef CONFIG_MEM_SOFT_DIRTY
# define VM_SOFTDIRTY	0x08000000	/* Not soft dirty clean area */
#else
# define VM_SOFTDIRTY	0
#endif

#define VM_MIXEDMAP	0x10000000	/* Can contain "struct page" and pure PFN pages */
#define VM_HUGEPAGE	0x20000000	/* MADV_HUGEPAGE marked this vma */
#define VM_NOHUGEPAGE	0x40000000	/* MADV_NOHUGEPAGE marked this vma */
#define VM_MERGEABLE	0x80000000	/* KSM may merge identical pages */

#ifdef CONFIG_ARCH_USES_HIGH_VMA_FLAGS
#define VM_HIGH_ARCH_BIT_0	32	/* bit only usable on 64-bit architectures */
#define VM_HIGH_ARCH_BIT_1	33	/* bit only usable on 64-bit architectures */
#define VM_HIGH_ARCH_BIT_2	34	/* bit only usable on 64-bit architectures */
#define VM_HIGH_ARCH_BIT_3	35	/* bit only usable on 64-bit architectures */
#define VM_HIGH_ARCH_BIT_4	36	/* bit only usable on 64-bit architectures */
#define VM_HIGH_ARCH_0	BIT(VM_HIGH_ARCH_BIT_0)
#define VM_HIGH_ARCH_1	BIT(VM_HIGH_ARCH_BIT_1)
#define VM_HIGH_ARCH_2	BIT(VM_HIGH_ARCH_BIT_2)
#define VM_HIGH_ARCH_3	BIT(VM_HIGH_ARCH_BIT_3)
#define VM_HIGH_ARCH_4	BIT(VM_HIGH_ARCH_BIT_4)
#endif /* CONFIG_ARCH_USES_HIGH_VMA_FLAGS */

#ifdef CONFIG_ARCH_HAS_PKEYS
# define VM_PKEY_SHIFT	VM_HIGH_ARCH_BIT_0
# define VM_PKEY_BIT0	VM_HIGH_ARCH_0	/* A protection key is a 4-bit value */
# define VM_PKEY_BIT1	VM_HIGH_ARCH_1	/* on x86 and 5-bit value on ppc64   */
# define VM_PKEY_BIT2	VM_HIGH_ARCH_2
# define VM_PKEY_BIT3	VM_HIGH_ARCH_3
#ifdef CONFIG_PPC
# define VM_PKEY_BIT4  VM_HIGH_ARCH_4
#else
# define VM_PKEY_BIT4  0
#endif
#endif /* CONFIG_ARCH_HAS_PKEYS */

#if defined(CONFIG_X86)
# define VM_PAT		VM_ARCH_1	/* PAT reserves whole VMA at once (x86) */
#elif defined(CONFIG_PPC)
# define VM_SAO		VM_ARCH_1	/* Strong Access Ordering (powerpc) */
#elif defined(CONFIG_PARISC)
# define VM_GROWSUP	VM_ARCH_1
#elif defined(CONFIG_IA64)
# define VM_GROWSUP	VM_ARCH_1
#elif defined(CONFIG_SPARC64)
# define VM_SPARC_ADI	VM_ARCH_1	/* Uses ADI tag for access control */
# define VM_ARCH_CLEAR	VM_SPARC_ADI
#elif !defined(CONFIG_MMU)
# define VM_MAPPED_COPY	VM_ARCH_1	/* T if mapped copy of data (nommu mmap) */
#endif

#if defined(CONFIG_X86_INTEL_MPX)
/* MPX specific bounds table or bounds directory */
# define VM_MPX		VM_HIGH_ARCH_4
#else
# define VM_MPX		VM_NONE
#endif

#ifndef VM_GROWSUP
# define VM_GROWSUP	VM_NONE
#endif

/* Bits set in the VMA until the stack is in its final location */
#define VM_STACK_INCOMPLETE_SETUP	(VM_RAND_READ | VM_SEQ_READ)

#ifndef VM_STACK_DEFAULT_FLAGS		/* arch can override this */
#define VM_STACK_DEFAULT_FLAGS VM_DATA_DEFAULT_FLAGS
#endif

#ifdef CONFIG_STACK_GROWSUP
#define VM_STACK	VM_GROWSUP
#else
#define VM_STACK	VM_GROWSDOWN
#endif

#define VM_STACK_FLAGS	(VM_STACK | VM_STACK_DEFAULT_FLAGS | VM_ACCOUNT)

/*
 * Special vmas that are non-mergable, non-mlock()able.
 * Note: mm/huge_memory.c VM_NO_THP depends on this definition.
 */
#define VM_SPECIAL (VM_IO | VM_DONTEXPAND | VM_PFNMAP | VM_MIXEDMAP)

/* This mask defines which mm->def_flags a process can inherit its parent */
#define VM_INIT_DEF_MASK	VM_NOHUGEPAGE

/* This mask is used to clear all the VMA flags used by mlock */
#define VM_LOCKED_CLEAR_MASK	(~(VM_LOCKED | VM_LOCKONFAULT))

/* Arch-specific flags to clear when updating VM flags on protection change */
#ifndef VM_ARCH_CLEAR
# define VM_ARCH_CLEAR	VM_NONE
#endif
#define VM_FLAGS_CLEAR	(ARCH_VM_PKEY_FLAGS | VM_ARCH_CLEAR)

/*
 * mapping from the currently active vm_flags protection bits (the
 * low four bits) to a page protection mask..
 */
extern pgprot_t protection_map[16];

#define FAULT_FLAG_WRITE	0x01	/* Fault was a write access */
#define FAULT_FLAG_MKWRITE	0x02	/* Fault was mkwrite of existing pte */
#define FAULT_FLAG_ALLOW_RETRY	0x04	/* Retry fault if blocking */
#define FAULT_FLAG_RETRY_NOWAIT	0x08	/* Don't drop mmap_sem and wait when retrying */
#define FAULT_FLAG_KILLABLE	0x10	/* The fault task is in SIGKILL killable region */
#define FAULT_FLAG_TRIED	0x20	/* Second try */
#define FAULT_FLAG_USER		0x40	/* The fault originated in userspace */
#define FAULT_FLAG_REMOTE	0x80	/* faulting for non current tsk/mm */
#define FAULT_FLAG_INSTRUCTION  0x100	/* The fault was during an instruction fetch */
#define FAULT_FLAG_PREFAULT_OLD 0x400   /* Make faultaround ptes old */
/* Speculative fault, not holding mmap_sem */
#define FAULT_FLAG_SPECULATIVE	0x200

#define FAULT_FLAG_TRACE \
	{ FAULT_FLAG_WRITE,		"WRITE" }, \
	{ FAULT_FLAG_MKWRITE,		"MKWRITE" }, \
	{ FAULT_FLAG_ALLOW_RETRY,	"ALLOW_RETRY" }, \
	{ FAULT_FLAG_RETRY_NOWAIT,	"RETRY_NOWAIT" }, \
	{ FAULT_FLAG_KILLABLE,		"KILLABLE" }, \
	{ FAULT_FLAG_TRIED,		"TRIED" }, \
	{ FAULT_FLAG_USER,		"USER" }, \
	{ FAULT_FLAG_REMOTE,		"REMOTE" }, \
	{ FAULT_FLAG_INSTRUCTION,	"INSTRUCTION" }

/*
 * vm_fault is filled by the the pagefault handler and passed to the vma's
 * ->fault function. The vma's ->fault is responsible for returning a bitmask
 * of VM_FAULT_xxx flags that give details about how the fault was handled.
 *
 * MM layer fills up gfp_mask for page allocations but fault handler might
 * alter it if its implementation requires a different allocation context.
 *
 * pgoff should be used in favour of virtual_address, if possible.
 */
struct vm_fault {
	struct vm_area_struct *vma;	/* Target VMA */
	unsigned int flags;		/* FAULT_FLAG_xxx flags */
	gfp_t gfp_mask;			/* gfp mask to be used for allocations */
	pgoff_t pgoff;			/* Logical page offset based on vma */
	unsigned long address;		/* Faulting virtual address */
#ifdef CONFIG_SPECULATIVE_PAGE_FAULT
	unsigned int sequence;
	pmd_t orig_pmd;			/* value of PMD at the time of fault */
#endif
	pmd_t *pmd;			/* Pointer to pmd entry matching
					 * the 'address' */
	pud_t *pud;			/* Pointer to pud entry matching
					 * the 'address'
					 */
	pte_t orig_pte;			/* Value of PTE at the time of fault */

	struct page *cow_page;		/* Page handler may use for COW fault */
	struct mem_cgroup *memcg;	/* Cgroup cow_page belongs to */
	struct page *page;		/* ->fault handlers should return a
					 * page here, unless VM_FAULT_NOPAGE
					 * is set (which is also implied by
					 * VM_FAULT_ERROR).
					 */
	/* These three entries are valid only while holding ptl lock */
	pte_t *pte;			/* Pointer to pte entry matching
					 * the 'address'. NULL if the page
					 * table hasn't been allocated.
					 */
	spinlock_t *ptl;		/* Page table lock.
					 * Protects pte page table if 'pte'
					 * is not NULL, otherwise pmd.
					 */
	pgtable_t prealloc_pte;		/* Pre-allocated pte page table.
					 * vm_ops->map_pages() calls
					 * alloc_set_pte() from atomic context.
					 * do_fault_around() pre-allocates
					 * page table to avoid allocation from
					 * atomic context.
					 */
	unsigned long vma_flags;	/* Speculative Page Fault field */
	pgprot_t vma_page_prot;		/* Speculative Page Fault field */
	ANDROID_VENDOR_DATA(1);
	ANDROID_VENDOR_DATA(2);
};

/* page entry size for vm->huge_fault() */
enum page_entry_size {
	PE_SIZE_PTE = 0,
	PE_SIZE_PMD,
	PE_SIZE_PUD,
};

/*
 * These are the virtual MM functions - opening of an area, closing and
 * unmapping it (needed to keep files on disk up-to-date etc), pointer
 * to the functions called when a no-page or a wp-page exception occurs.
 */
struct vm_operations_struct {
	void (*open)(struct vm_area_struct * area);
	void (*close)(struct vm_area_struct * area);
	int (*split)(struct vm_area_struct * area, unsigned long addr);
	int (*mremap)(struct vm_area_struct * area);
	vm_fault_t (*fault)(struct vm_fault *vmf);
	vm_fault_t (*huge_fault)(struct vm_fault *vmf,
			enum page_entry_size pe_size);
	void (*map_pages)(struct vm_fault *vmf,
			pgoff_t start_pgoff, pgoff_t end_pgoff);
	unsigned long (*pagesize)(struct vm_area_struct * area);

	/* notification that a previously read-only page is about to become
	 * writable, if an error is returned it will cause a SIGBUS */
	vm_fault_t (*page_mkwrite)(struct vm_fault *vmf);

	/* same as page_mkwrite when using VM_PFNMAP|VM_MIXEDMAP */
	vm_fault_t (*pfn_mkwrite)(struct vm_fault *vmf);

	/* called by access_process_vm when get_user_pages() fails, typically
	 * for use by special VMAs that can switch between memory and hardware
	 */
	int (*access)(struct vm_area_struct *vma, unsigned long addr,
		      void *buf, int len, int write);

	/* Called by the /proc/PID/maps code to ask the vma whether it
	 * has a special name.  Returning non-NULL will also cause this
	 * vma to be dumped unconditionally. */
	const char *(*name)(struct vm_area_struct *vma);

#ifdef CONFIG_NUMA
	/*
	 * set_policy() op must add a reference to any non-NULL @new mempolicy
	 * to hold the policy upon return.  Caller should pass NULL @new to
	 * remove a policy and fall back to surrounding context--i.e. do not
	 * install a MPOL_DEFAULT policy, nor the task or system default
	 * mempolicy.
	 */
	int (*set_policy)(struct vm_area_struct *vma, struct mempolicy *new);

	/*
	 * get_policy() op must add reference [mpol_get()] to any policy at
	 * (vma,addr) marked as MPOL_SHARED.  The shared policy infrastructure
	 * in mm/mempolicy.c will do this automatically.
	 * get_policy() must NOT add a ref if the policy at (vma,addr) is not
	 * marked as MPOL_SHARED. vma policies are protected by the mmap_sem.
	 * If no [shared/vma] mempolicy exists at the addr, get_policy() op
	 * must return NULL--i.e., do not "fallback" to task or system default
	 * policy.
	 */
	struct mempolicy *(*get_policy)(struct vm_area_struct *vma,
					unsigned long addr);
#endif
	/*
	 * Called by vm_normal_page() for special PTEs to find the
	 * page for @addr.  This is useful if the default behavior
	 * (using pte_page()) would not find the correct page.
	 */
	struct page *(*find_special_page)(struct vm_area_struct *vma,
					  unsigned long addr);

	ANDROID_KABI_RESERVE(1);
	ANDROID_KABI_RESERVE(2);
	ANDROID_KABI_RESERVE(3);
	ANDROID_KABI_RESERVE(4);
};

#ifdef CONFIG_CONT_PTE_HUGEPAGE
extern bool config_bug_on;

#define CHP_TAG "CHP"
#define CHP_LOG_LVL 1

enum {
	CHP_LOG_VERBOSE = 0,
	CHP_LOG_INFO,
	CHP_LOG_DEBUG,
	CHP_LOG_ERR,
};

static inline char chp_loglvl_to_char(int l)
{
	switch (l) {
	case CHP_LOG_VERBOSE:
		return 'V';
	case CHP_LOG_INFO:
		return 'I';
	case CHP_LOG_DEBUG:
		return 'D';
	case CHP_LOG_ERR:
		return 'E';
	}
	return '?';
}

#define chp_log(l, f, ...) do {						\
	if (l >= CHP_LOG_LVL) 						\
		printk(KERN_INFO "%s %5d %5d %c %-16s: %s:%d "f,	\
		       CHP_TAG, current->tgid, current->pid,		\
		       chp_loglvl_to_char(l), current->comm, __func__,  \
		       __LINE__,  ##__VA_ARGS__);			\
} while (0)

#define chp_loge(f, ...)						\
	chp_log(CHP_LOG_ERR, f, ##__VA_ARGS__)

#define chp_logi(f, ...)						\
	chp_log(CHP_LOG_INFO, f, ##__VA_ARGS__)

#define CHP_BUG_ON(condition) do {					\
	if (unlikely(config_bug_on && condition))			\
		BUG();							\
} while (0)

#define CHP_BUG_ON_EMERGENCY(condition) do {				\
	if (unlikely(condition))					\
		BUG();							\
} while (0)
#endif

#define UNALIGNED_CONT_PTE_WARN WARN_ON

static inline void INIT_VMA(struct vm_area_struct *vma)
{
	INIT_LIST_HEAD(&vma->anon_vma_chain);
#ifdef CONFIG_SPECULATIVE_PAGE_FAULT
	seqcount_init(&vma->vm_sequence);
	atomic_set(&vma->vm_ref_count, 1);
#endif
}

static inline void vma_init(struct vm_area_struct *vma, struct mm_struct *mm)
{
	static const struct vm_operations_struct dummy_vm_ops = {};

	memset(vma, 0, sizeof(*vma));
	vma->vm_mm = mm;
	vma->vm_ops = &dummy_vm_ops;
	INIT_VMA(vma);
}

static inline void vma_set_anonymous(struct vm_area_struct *vma)
{
	vma->vm_ops = NULL;
}

static inline bool vma_is_anonymous(struct vm_area_struct *vma)
{
	return !vma->vm_ops;
}

#ifdef CONFIG_SHMEM
/*
 * The vma_is_shmem is not inline because it is used only by slow
 * paths in userfault.
 */
bool vma_is_shmem(struct vm_area_struct *vma);
#else
static inline bool vma_is_shmem(struct vm_area_struct *vma) { return false; }
#endif

int vma_is_stack_for_current(struct vm_area_struct *vma);

/* flush_tlb_range() takes a vma, not a mm, and can care about flags */
#define TLB_FLUSH_VMA(mm,flags) { .vm_mm = (mm), .vm_flags = (flags) }

struct mmu_gather;
struct inode;

#if !defined(CONFIG_ARCH_HAS_PTE_DEVMAP) || !defined(CONFIG_TRANSPARENT_HUGEPAGE)
static inline int pmd_devmap(pmd_t pmd)
{
	return 0;
}
static inline int pud_devmap(pud_t pud)
{
	return 0;
}
static inline int pgd_devmap(pgd_t pgd)
{
	return 0;
}
#endif

/*
 * FIXME: take this include out, include page-flags.h in
 * files which need it (119 of them)
 */
#include <linux/page-flags.h>
//#include <linux/huge_mm.h>

/*
 * Methods to modify the page usage count.
 *
 * What counts for a page usage:
 * - cache mapping   (page->mapping)
 * - private data    (page->private)
 * - page mapped in a task's page tables, each mapping
 *   is counted separately
 *
 * Also, many kernel routines increase the page count before a critical
 * routine so they can be sure the page doesn't go away from under them.
 */

/*
 * Drop a ref, return true if the refcount fell to zero (the page has no users)
 */
static inline int put_page_testzero(struct page *page)
{
	VM_BUG_ON_PAGE(page_ref_count(page) == 0, page);
	return page_ref_dec_and_test(page);
}

/*
 * Try to grab a ref unless the page has a refcount of zero, return false if
 * that is the case.
 * This can be called when MMU is off so it must not access
 * any of the virtual mappings.
 */
static inline int get_page_unless_zero(struct page *page)
{
	return page_ref_add_unless(page, 1, 0);
}

extern int page_is_ram(unsigned long pfn);

enum {
	REGION_INTERSECTS,
	REGION_DISJOINT,
	REGION_MIXED,
};

int region_intersects(resource_size_t offset, size_t size, unsigned long flags,
		      unsigned long desc);

/* Support for virtually mapped pages */
struct page *vmalloc_to_page(const void *addr);
unsigned long vmalloc_to_pfn(const void *addr);

/*
 * Determine if an address is within the vmalloc range
 *
 * On nommu, vmalloc/vfree wrap through kmalloc/kfree directly, so there
 * is no special casing required.
 */

#ifdef CONFIG_ENABLE_VMALLOC_SAVING
extern bool is_vmalloc_addr(const void *x);
#else
static inline bool is_vmalloc_addr(const void *x)
{
#ifdef CONFIG_MMU
	unsigned long addr = (unsigned long)x;

	return addr >= VMALLOC_START && addr < VMALLOC_END;
#else
	return false;
#endif
}
#endif //CONFIG_ENABLE_VMALLOC_SAVING

#ifndef is_ioremap_addr
#define is_ioremap_addr(x) is_vmalloc_addr(x)
#endif

#ifdef CONFIG_MMU
extern int is_vmalloc_or_module_addr(const void *x);
#else
static inline int is_vmalloc_or_module_addr(const void *x)
{
	return 0;
}
#endif

extern void *kvmalloc_node(size_t size, gfp_t flags, int node);
static inline void *kvmalloc(size_t size, gfp_t flags)
{
	return kvmalloc_node(size, flags, NUMA_NO_NODE);
}
static inline void *kvzalloc_node(size_t size, gfp_t flags, int node)
{
	return kvmalloc_node(size, flags | __GFP_ZERO, node);
}
static inline void *kvzalloc(size_t size, gfp_t flags)
{
	return kvmalloc(size, flags | __GFP_ZERO);
}

static inline void *kvmalloc_array(size_t n, size_t size, gfp_t flags)
{
	size_t bytes;

	if (unlikely(check_mul_overflow(n, size, &bytes)))
		return NULL;

	return kvmalloc(bytes, flags);
}

static inline void *kvcalloc(size_t n, size_t size, gfp_t flags)
{
	return kvmalloc_array(n, size, flags | __GFP_ZERO);
}

extern void kvfree(const void *addr);
extern void kvfree_sensitive(const void *addr, size_t len);

/*
 * Mapcount of compound page as a whole, does not include mapped sub-pages.
 *
 * Must be called only for compound pages or any their tail sub-pages.
 */
static inline int compound_mapcount(struct page *page)
{
	VM_BUG_ON_PAGE(!PageCompound(page), page);
	page = compound_head(page);
	return atomic_read(compound_mapcount_ptr(page)) + 1;
}

/*
 * The atomic page->_mapcount, starts from -1: so that transitions
 * both from it and to it can be tracked, using atomic_inc_and_test
 * and atomic_add_negative(-1).
 */
static inline void page_mapcount_reset(struct page *page)
{
	atomic_set(&(page)->_mapcount, -1);
}

int __page_mapcount(struct page *page);

/*
 * Mapcount of 0-order page; when compound sub-page, includes
 * compound_mapcount().
 *
 * Result is undefined for pages which cannot be mapped into userspace.
 * For example SLAB or special types of pages. See function page_has_type().
 * They use this place in struct page differently.
 */
static inline int page_mapcount(struct page *page)
{
	if (unlikely(PageCompound(page)))
		return __page_mapcount(page);
	return atomic_read(&page->_mapcount) + 1;
}

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
int total_mapcount(struct page *page);
int page_trans_huge_mapcount(struct page *page, int *total_mapcount);
#else
static inline int total_mapcount(struct page *page)
{
	return page_mapcount(page);
}
static inline int page_trans_huge_mapcount(struct page *page,
					   int *total_mapcount)
{
	int mapcount = page_mapcount(page);
	if (total_mapcount)
		*total_mapcount = mapcount;
	return mapcount;
}
#endif

static inline struct page *virt_to_head_page(const void *x)
{
	struct page *page = virt_to_page(x);

	return compound_head(page);
}

void __put_page(struct page *page);

void put_pages_list(struct list_head *pages);

void split_page(struct page *page, unsigned int order);

/*
 * Compound pages have a destructor function.  Provide a
 * prototype for that function and accessor functions.
 * These are _only_ valid on the head of a compound page.
 */
typedef void compound_page_dtor(struct page *);

/* Keep the enum in sync with compound_page_dtors array in mm/page_alloc.c */
enum compound_dtor_id {
	NULL_COMPOUND_DTOR,
	COMPOUND_PAGE_DTOR,
#ifdef CONFIG_HUGETLB_PAGE
	HUGETLB_PAGE_DTOR,
#endif
#if defined(CONFIG_TRANSPARENT_HUGEPAGE) || defined(CONFIG_GKI_OPT_FEATURES)
	TRANSHUGE_PAGE_DTOR,
#endif
	NR_COMPOUND_DTORS,
};
extern compound_page_dtor * const compound_page_dtors[];

static inline void set_compound_page_dtor(struct page *page,
		enum compound_dtor_id compound_dtor)
{
	VM_BUG_ON_PAGE(compound_dtor >= NR_COMPOUND_DTORS, page);
	page[1].compound_dtor = compound_dtor;
}

static inline compound_page_dtor *get_compound_page_dtor(struct page *page)
{
	VM_BUG_ON_PAGE(page[1].compound_dtor >= NR_COMPOUND_DTORS, page);
	return compound_page_dtors[page[1].compound_dtor];
}

static inline unsigned int compound_order(struct page *page)
{
	if (!PageHead(page))
		return 0;
	return page[1].compound_order;
}

static inline void set_compound_order(struct page *page, unsigned int order)
{
	page[1].compound_order = order;
}

/* Returns the number of pages in this potentially compound page. */
static inline unsigned long compound_nr(struct page *page)
{
	return 1UL << compound_order(page);
}

/* Returns the number of bytes in this potentially compound page. */
static inline unsigned long page_size(struct page *page)
{
	return PAGE_SIZE << compound_order(page);
}

/* Returns the number of bits needed for the number of bytes in a page */
static inline unsigned int page_shift(struct page *page)
{
	return PAGE_SHIFT + compound_order(page);
}

void free_compound_page(struct page *page);

#ifdef CONFIG_MMU
/*
 * Do pte_mkwrite, but only if the vma says VM_WRITE.  We do this when
 * servicing faults for write access.  In the normal case, do always want
 * pte_mkwrite.  But get_user_pages can cause write faults for mappings
 * that do not have writing enabled, when used by access_process_vm.
 */
static inline pte_t maybe_mkwrite(pte_t pte, unsigned long vma_flags)
{
	if (likely(vma_flags & VM_WRITE))
		pte = pte_mkwrite(pte);
	return pte;
}

vm_fault_t alloc_set_pte(struct vm_fault *vmf, struct mem_cgroup *memcg,
		struct page *page);
vm_fault_t finish_fault(struct vm_fault *vmf);
vm_fault_t finish_mkwrite_fault(struct vm_fault *vmf);
#endif

#include <linux/huge_mm.h>

/*
 * Multiple processes may "see" the same page. E.g. for untouched
 * mappings of /dev/null, all processes see the same page full of
 * zeroes, and text pages of executables and shared libraries have
 * only one copy in memory, at most, normally.
 *
 * For the non-reserved pages, page_count(page) denotes a reference count.
 *   page_count() == 0 means the page is free. page->lru is then used for
 *   freelist management in the buddy allocator.
 *   page_count() > 0  means the page has been allocated.
 *
 * Pages are allocated by the slab allocator in order to provide memory
 * to kmalloc and kmem_cache_alloc. In this case, the management of the
 * page, and the fields in 'struct page' are the responsibility of mm/slab.c
 * unless a particular usage is carefully commented. (the responsibility of
 * freeing the kmalloc memory is the caller's, of course).
 *
 * A page may be used by anyone else who does a __get_free_page().
 * In this case, page_count still tracks the references, and should only
 * be used through the normal accessor functions. The top bits of page->flags
 * and page->virtual store page management information, but all other fields
 * are unused and could be used privately, carefully. The management of this
 * page is the responsibility of the one who allocated it, and those who have
 * subsequently been given references to it.
 *
 * The other pages (we may call them "pagecache pages") are completely
 * managed by the Linux memory manager: I/O, buffers, swapping etc.
 * The following discussion applies only to them.
 *
 * A pagecache page contains an opaque `private' member, which belongs to the
 * page's address_space. Usually, this is the address of a circular list of
 * the page's disk buffers. PG_private must be set to tell the VM to call
 * into the filesystem to release these pages.
 *
 * A page may belong to an inode's memory mapping. In this case, page->mapping
 * is the pointer to the inode, and page->index is the file offset of the page,
 * in units of PAGE_SIZE.
 *
 * If pagecache pages are not associated with an inode, they are said to be
 * anonymous pages. These may become associated with the swapcache, and in that
 * case PG_swapcache is set, and page->private is an offset into the swapcache.
 *
 * In either case (swapcache or inode backed), the pagecache itself holds one
 * reference to the page. Setting PG_private should also increment the
 * refcount. The each user mapping also has a reference to the page.
 *
 * The pagecache pages are stored in a per-mapping radix tree, which is
 * rooted at mapping->i_pages, and indexed by offset.
 * Where 2.4 and early 2.6 kernels kept dirty/clean pages in per-address_space
 * lists, we instead now tag pages as dirty/writeback in the radix tree.
 *
 * All pagecache pages may be subject to I/O:
 * - inode pages may need to be read from disk,
 * - inode pages which have been modified and are MAP_SHARED may need
 *   to be written back to the inode on disk,
 * - anonymous pages (including MAP_PRIVATE file mappings) which have been
 *   modified may need to be swapped out to swap space and (later) to be read
 *   back into memory.
 */

/*
 * The zone field is never updated after free_area_init_core()
 * sets it, so none of the operations on it need to be atomic.
 */

/* Page flags: | [SECTION] | [NODE] | ZONE | [LAST_CPUPID] | ... | FLAGS | */
#define SECTIONS_PGOFF		((sizeof(unsigned long)*8) - SECTIONS_WIDTH)
#define NODES_PGOFF		(SECTIONS_PGOFF - NODES_WIDTH)
#define ZONES_PGOFF		(NODES_PGOFF - ZONES_WIDTH)
#define LAST_CPUPID_PGOFF	(ZONES_PGOFF - LAST_CPUPID_WIDTH)
#define KASAN_TAG_PGOFF		(LAST_CPUPID_PGOFF - KASAN_TAG_WIDTH)

/*
 * Define the bit shifts to access each section.  For non-existent
 * sections we define the shift as 0; that plus a 0 mask ensures
 * the compiler will optimise away reference to them.
 */
#define SECTIONS_PGSHIFT	(SECTIONS_PGOFF * (SECTIONS_WIDTH != 0))
#define NODES_PGSHIFT		(NODES_PGOFF * (NODES_WIDTH != 0))
#define ZONES_PGSHIFT		(ZONES_PGOFF * (ZONES_WIDTH != 0))
#define LAST_CPUPID_PGSHIFT	(LAST_CPUPID_PGOFF * (LAST_CPUPID_WIDTH != 0))
#define KASAN_TAG_PGSHIFT	(KASAN_TAG_PGOFF * (KASAN_TAG_WIDTH != 0))

/* NODE:ZONE or SECTION:ZONE is used to ID a zone for the buddy allocator */
#ifdef NODE_NOT_IN_PAGE_FLAGS
#define ZONEID_SHIFT		(SECTIONS_SHIFT + ZONES_SHIFT)
#define ZONEID_PGOFF		((SECTIONS_PGOFF < ZONES_PGOFF)? \
						SECTIONS_PGOFF : ZONES_PGOFF)
#else
#define ZONEID_SHIFT		(NODES_SHIFT + ZONES_SHIFT)
#define ZONEID_PGOFF		((NODES_PGOFF < ZONES_PGOFF)? \
						NODES_PGOFF : ZONES_PGOFF)
#endif

#define ZONEID_PGSHIFT		(ZONEID_PGOFF * (ZONEID_SHIFT != 0))

#if SECTIONS_WIDTH+NODES_WIDTH+ZONES_WIDTH > BITS_PER_LONG - NR_PAGEFLAGS
#error SECTIONS_WIDTH+NODES_WIDTH+ZONES_WIDTH > BITS_PER_LONG - NR_PAGEFLAGS
#endif

#define ZONES_MASK		((1UL << ZONES_WIDTH) - 1)
#define NODES_MASK		((1UL << NODES_WIDTH) - 1)
#define SECTIONS_MASK		((1UL << SECTIONS_WIDTH) - 1)
#define LAST_CPUPID_MASK	((1UL << LAST_CPUPID_SHIFT) - 1)
#define KASAN_TAG_MASK		((1UL << KASAN_TAG_WIDTH) - 1)
#define ZONEID_MASK		((1UL << ZONEID_SHIFT) - 1)

static inline enum zone_type page_zonenum(const struct page *page)
{
	return (page->flags >> ZONES_PGSHIFT) & ZONES_MASK;
}

#ifdef CONFIG_ZONE_DEVICE
static inline bool is_zone_device_page(const struct page *page)
{
	return page_zonenum(page) == ZONE_DEVICE;
}
extern void memmap_init_zone_device(struct zone *, unsigned long,
				    unsigned long, struct dev_pagemap *);
#else
static inline bool is_zone_device_page(const struct page *page)
{
	return false;
}
#endif

#ifdef CONFIG_DEV_PAGEMAP_OPS
void __put_devmap_managed_page(struct page *page);
DECLARE_STATIC_KEY_FALSE(devmap_managed_key);
static inline bool put_devmap_managed_page(struct page *page)
{
	if (!static_branch_unlikely(&devmap_managed_key))
		return false;
	if (!is_zone_device_page(page))
		return false;
	switch (page->pgmap->type) {
	case MEMORY_DEVICE_PRIVATE:
	case MEMORY_DEVICE_FS_DAX:
		__put_devmap_managed_page(page);
		return true;
	default:
		break;
	}
	return false;
}

#else /* CONFIG_DEV_PAGEMAP_OPS */
static inline bool put_devmap_managed_page(struct page *page)
{
	return false;
}
#endif /* CONFIG_DEV_PAGEMAP_OPS */

static inline bool is_device_private_page(const struct page *page)
{
	return IS_ENABLED(CONFIG_DEV_PAGEMAP_OPS) &&
		IS_ENABLED(CONFIG_DEVICE_PRIVATE) &&
		is_zone_device_page(page) &&
		page->pgmap->type == MEMORY_DEVICE_PRIVATE;
}

static inline bool is_pci_p2pdma_page(const struct page *page)
{
	return IS_ENABLED(CONFIG_DEV_PAGEMAP_OPS) &&
		IS_ENABLED(CONFIG_PCI_P2PDMA) &&
		is_zone_device_page(page) &&
		page->pgmap->type == MEMORY_DEVICE_PCI_P2PDMA;
}

/* 127: arbitrary random number, small enough to assemble well */
#define page_ref_zero_or_close_to_overflow(page) \
	((unsigned int) page_ref_count(page) + 127u <= 127u)

static inline void get_page(struct page *page)
{
	page = compound_head(page);
	/*
	 * Getting a normal page or the head of a compound page
	 * requires to already have an elevated page->_refcount.
	 */
	VM_BUG_ON_PAGE(page_ref_zero_or_close_to_overflow(page), page);
	page_ref_inc(page);
}

static inline __must_check bool try_get_page(struct page *page)
{
	page = compound_head(page);
	if (WARN_ON_ONCE(page_ref_count(page) <= 0))
		return false;
	page_ref_inc(page);
	return true;
}

static inline void put_page(struct page *page)
{
	page = compound_head(page);

	/*
	 * For devmap managed pages we need to catch refcount transition from
	 * 2 to 1, when refcount reach one it means the page is free and we
	 * need to inform the device driver through callback. See
	 * include/linux/memremap.h and HMM for details.
	 */
	if (put_devmap_managed_page(page))
		return;

	if (put_page_testzero(page))
		__put_page(page);
}

/**
 * put_user_page() - release a gup-pinned page
 * @page:            pointer to page to be released
 *
 * Pages that were pinned via get_user_pages*() must be released via
 * either put_user_page(), or one of the put_user_pages*() routines
 * below. This is so that eventually, pages that are pinned via
 * get_user_pages*() can be separately tracked and uniquely handled. In
 * particular, interactions with RDMA and filesystems need special
 * handling.
 *
 * put_user_page() and put_page() are not interchangeable, despite this early
 * implementation that makes them look the same. put_user_page() calls must
 * be perfectly matched up with get_user_page() calls.
 */
static inline void put_user_page(struct page *page)
{
	put_page(page);
}

void put_user_pages_dirty_lock(struct page **pages, unsigned long npages,
			       bool make_dirty);

void put_user_pages(struct page **pages, unsigned long npages);

#if defined(CONFIG_SPARSEMEM) && !defined(CONFIG_SPARSEMEM_VMEMMAP)
#define SECTION_IN_PAGE_FLAGS
#endif

/*
 * The identification function is mainly used by the buddy allocator for
 * determining if two pages could be buddies. We are not really identifying
 * the zone since we could be using the section number id if we do not have
 * node id available in page flags.
 * We only guarantee that it will return the same value for two combinable
 * pages in a zone.
 */
static inline int page_zone_id(struct page *page)
{
	return (page->flags >> ZONEID_PGSHIFT) & ZONEID_MASK;
}

#ifdef NODE_NOT_IN_PAGE_FLAGS
extern int page_to_nid(const struct page *page);
#else
static inline int page_to_nid(const struct page *page)
{
	struct page *p = (struct page *)page;

	return (PF_POISONED_CHECK(p)->flags >> NODES_PGSHIFT) & NODES_MASK;
}
#endif

#ifdef CONFIG_NUMA_BALANCING
static inline int cpu_pid_to_cpupid(int cpu, int pid)
{
	return ((cpu & LAST__CPU_MASK) << LAST__PID_SHIFT) | (pid & LAST__PID_MASK);
}

static inline int cpupid_to_pid(int cpupid)
{
	return cpupid & LAST__PID_MASK;
}

static inline int cpupid_to_cpu(int cpupid)
{
	return (cpupid >> LAST__PID_SHIFT) & LAST__CPU_MASK;
}

static inline int cpupid_to_nid(int cpupid)
{
	return cpu_to_node(cpupid_to_cpu(cpupid));
}

static inline bool cpupid_pid_unset(int cpupid)
{
	return cpupid_to_pid(cpupid) == (-1 & LAST__PID_MASK);
}

static inline bool cpupid_cpu_unset(int cpupid)
{
	return cpupid_to_cpu(cpupid) == (-1 & LAST__CPU_MASK);
}

static inline bool __cpupid_match_pid(pid_t task_pid, int cpupid)
{
	return (task_pid & LAST__PID_MASK) == cpupid_to_pid(cpupid);
}

#define cpupid_match_pid(task, cpupid) __cpupid_match_pid(task->pid, cpupid)
#ifdef LAST_CPUPID_NOT_IN_PAGE_FLAGS
static inline int page_cpupid_xchg_last(struct page *page, int cpupid)
{
	return xchg(&page->_last_cpupid, cpupid & LAST_CPUPID_MASK);
}

static inline int page_cpupid_last(struct page *page)
{
	return page->_last_cpupid;
}
static inline void page_cpupid_reset_last(struct page *page)
{
	page->_last_cpupid = -1 & LAST_CPUPID_MASK;
}
#else
static inline int page_cpupid_last(struct page *page)
{
	return (page->flags >> LAST_CPUPID_PGSHIFT) & LAST_CPUPID_MASK;
}

extern int page_cpupid_xchg_last(struct page *page, int cpupid);

static inline void page_cpupid_reset_last(struct page *page)
{
	page->flags |= LAST_CPUPID_MASK << LAST_CPUPID_PGSHIFT;
}
#endif /* LAST_CPUPID_NOT_IN_PAGE_FLAGS */
#else /* !CONFIG_NUMA_BALANCING */
static inline int page_cpupid_xchg_last(struct page *page, int cpupid)
{
	return page_to_nid(page); /* XXX */
}

static inline int page_cpupid_last(struct page *page)
{
	return page_to_nid(page); /* XXX */
}

static inline int cpupid_to_nid(int cpupid)
{
	return -1;
}

static inline int cpupid_to_pid(int cpupid)
{
	return -1;
}

static inline int cpupid_to_cpu(int cpupid)
{
	return -1;
}

static inline int cpu_pid_to_cpupid(int nid, int pid)
{
	return -1;
}

static inline bool cpupid_pid_unset(int cpupid)
{
	return 1;
}

static inline void page_cpupid_reset_last(struct page *page)
{
}

static inline bool cpupid_match_pid(struct task_struct *task, int cpupid)
{
	return false;
}
#endif /* CONFIG_NUMA_BALANCING */

#ifdef CONFIG_KASAN_SW_TAGS

/*
 * KASAN per-page tags are stored xor'ed with 0xff. This allows to avoid
 * setting tags for all pages to native kernel tag value 0xff, as the default
 * value 0x00 maps to 0xff.
 */

static inline u8 page_kasan_tag(const struct page *page)
{
	u8 tag;

	tag = (page->flags >> KASAN_TAG_PGSHIFT) & KASAN_TAG_MASK;
	tag ^= 0xff;

	return tag;
}

static inline void page_kasan_tag_set(struct page *page, u8 tag)
{
	tag ^= 0xff;
	page->flags &= ~(KASAN_TAG_MASK << KASAN_TAG_PGSHIFT);
	page->flags |= (tag & KASAN_TAG_MASK) << KASAN_TAG_PGSHIFT;
}

static inline void page_kasan_tag_reset(struct page *page)
{
	page_kasan_tag_set(page, 0xff);
}
#else
static inline u8 page_kasan_tag(const struct page *page)
{
	return 0xff;
}

static inline void page_kasan_tag_set(struct page *page, u8 tag) { }
static inline void page_kasan_tag_reset(struct page *page) { }
#endif

static inline struct zone *page_zone(const struct page *page)
{
	return &NODE_DATA(page_to_nid(page))->node_zones[page_zonenum(page)];
}

static inline pg_data_t *page_pgdat(const struct page *page)
{
	return NODE_DATA(page_to_nid(page));
}

#ifdef SECTION_IN_PAGE_FLAGS
static inline void set_page_section(struct page *page, unsigned long section)
{
	page->flags &= ~(SECTIONS_MASK << SECTIONS_PGSHIFT);
	page->flags |= (section & SECTIONS_MASK) << SECTIONS_PGSHIFT;
}

static inline unsigned long page_to_section(const struct page *page)
{
	return (page->flags >> SECTIONS_PGSHIFT) & SECTIONS_MASK;
}
#endif

static inline void set_page_zone(struct page *page, enum zone_type zone)
{
	page->flags &= ~(ZONES_MASK << ZONES_PGSHIFT);
	page->flags |= (zone & ZONES_MASK) << ZONES_PGSHIFT;
}

static inline void set_page_node(struct page *page, unsigned long node)
{
	page->flags &= ~(NODES_MASK << NODES_PGSHIFT);
	page->flags |= (node & NODES_MASK) << NODES_PGSHIFT;
}

static inline void set_page_links(struct page *page, enum zone_type zone,
	unsigned long node, unsigned long pfn)
{
	set_page_zone(page, zone);
	set_page_node(page, node);
#ifdef SECTION_IN_PAGE_FLAGS
	set_page_section(page, pfn_to_section_nr(pfn));
#endif
}

#ifdef CONFIG_MEMCG
static inline struct mem_cgroup *page_memcg(struct page *page)
{
	return page->mem_cgroup;
}
static inline struct mem_cgroup *page_memcg_rcu(struct page *page)
{
	WARN_ON_ONCE(!rcu_read_lock_held());
	return READ_ONCE(page->mem_cgroup);
}
#else
static inline struct mem_cgroup *page_memcg(struct page *page)
{
	return NULL;
}
static inline struct mem_cgroup *page_memcg_rcu(struct page *page)
{
	WARN_ON_ONCE(!rcu_read_lock_held());
	return NULL;
}
#endif

/*
 * Some inline functions in vmstat.h depend on page_zone()
 */
#include <linux/vmstat.h>

static __always_inline void *lowmem_page_address(const struct page *page)
{
	return page_to_virt(page);
}

#if defined(CONFIG_HIGHMEM) && !defined(WANT_PAGE_VIRTUAL)
#define HASHED_PAGE_VIRTUAL
#endif

#if defined(WANT_PAGE_VIRTUAL)
static inline void *page_address(const struct page *page)
{
	return page->virtual;
}
static inline void set_page_address(struct page *page, void *address)
{
	page->virtual = address;
}
#define page_address_init()  do { } while(0)
#endif

#if defined(HASHED_PAGE_VIRTUAL)
void *page_address(const struct page *page);
void set_page_address(struct page *page, void *virtual);
void page_address_init(void);
#endif

#if !defined(HASHED_PAGE_VIRTUAL) && !defined(WANT_PAGE_VIRTUAL)
#define page_address(page) lowmem_page_address(page)
#define set_page_address(page, address)  do { } while(0)
#define page_address_init()  do { } while(0)
#endif

extern void *page_rmapping(struct page *page);
extern struct anon_vma *page_anon_vma(struct page *page);
extern struct address_space *page_mapping(struct page *page);

extern struct address_space *__page_file_mapping(struct page *);

static inline
struct address_space *page_file_mapping(struct page *page)
{
	if (unlikely(PageSwapCache(page)))
		return __page_file_mapping(page);

	return page->mapping;
}

extern pgoff_t __page_file_index(struct page *page);

/*
 * Return the pagecache index of the passed page.  Regular pagecache pages
 * use ->index whereas swapcache pages use swp_offset(->private)
 */
static inline pgoff_t page_index(struct page *page)
{
	if (unlikely(PageSwapCache(page)))
		return __page_file_index(page);
	return page->index;
}

bool page_mapped(struct page *page);
struct address_space *page_mapping(struct page *page);
struct address_space *page_mapping_file(struct page *page);

/*
 * Return true only if the page has been allocated with
 * ALLOC_NO_WATERMARKS and the low watermark was not
 * met implying that the system is under some pressure.
 */
static inline bool page_is_pfmemalloc(struct page *page)
{
	/*
	 * Page index cannot be this large so this must be
	 * a pfmemalloc page.
	 */
	return page->index == -1UL;
}

/*
 * Only to be called by the page allocator on a freshly allocated
 * page.
 */
static inline void set_page_pfmemalloc(struct page *page)
{
	page->index = -1UL;
}

static inline void clear_page_pfmemalloc(struct page *page)
{
	page->index = 0;
}

/*
 * Can be called by the pagefault handler when it gets a VM_FAULT_OOM.
 */
extern void pagefault_out_of_memory(void);

#define offset_in_page(p)	((unsigned long)(p) & ~PAGE_MASK)

/*
 * Flags passed to show_mem() and show_free_areas() to suppress output in
 * various contexts.
 */
#define SHOW_MEM_FILTER_NODES		(0x0001u)	/* disallowed nodes */

extern void show_free_areas(unsigned int flags, nodemask_t *nodemask);

#ifdef CONFIG_MMU
extern bool can_do_mlock(void);
#else
static inline bool can_do_mlock(void) { return false; }
#endif
extern int user_shm_lock(size_t, struct user_struct *);
extern void user_shm_unlock(size_t, struct user_struct *);

/*
 * Parameter block passed down to zap_pte_range in exceptional cases.
 */
struct zap_details {
	struct address_space *check_mapping;	/* Check page->mapping if set */
	pgoff_t	first_index;			/* Lowest page->index to unmap */
	pgoff_t last_index;			/* Highest page->index to unmap */
	struct page *single_page;		/* Locked page to be unmapped */
};

struct page *_vm_normal_page(struct vm_area_struct *vma, unsigned long addr,
			      pte_t pte, unsigned long vma_flags);
static inline struct page *vm_normal_page(struct vm_area_struct *vma,
					  unsigned long addr, pte_t pte)
{
	return _vm_normal_page(vma, addr, pte, vma->vm_flags);
}

struct page *vm_normal_page_pmd(struct vm_area_struct *vma, unsigned long addr,
				pmd_t pmd);

void zap_vma_ptes(struct vm_area_struct *vma, unsigned long address,
		  unsigned long size);
void zap_page_range(struct vm_area_struct *vma, unsigned long address,
		    unsigned long size);
void unmap_vmas(struct mmu_gather *tlb, struct vm_area_struct *start_vma,
		unsigned long start, unsigned long end);

struct mmu_notifier_range;

void free_pgd_range(struct mmu_gather *tlb, unsigned long addr,
		unsigned long end, unsigned long floor, unsigned long ceiling);
int copy_page_range(struct mm_struct *dst, struct mm_struct *src,
			struct vm_area_struct *vma);
int follow_invalidate_pte(struct mm_struct *mm, unsigned long address,
			  struct mmu_notifier_range *range, pte_t **ptepp,
			  pmd_t **pmdpp, spinlock_t **ptlp);
int follow_pte(struct mm_struct *mm, unsigned long address,
	       pte_t **ptepp, spinlock_t **ptlp);
int follow_pfn(struct vm_area_struct *vma, unsigned long address,
	unsigned long *pfn);
int follow_phys(struct vm_area_struct *vma, unsigned long address,
		unsigned int flags, unsigned long *prot, resource_size_t *phys);
int generic_access_phys(struct vm_area_struct *vma, unsigned long addr,
			void *buf, int len, int write);

#ifdef CONFIG_SPECULATIVE_PAGE_FAULT
static inline void vm_write_begin(struct vm_area_struct *vma)
{
	/*
	 * The reads never spins and preemption
	 * disablement is not required.
	 */
	raw_write_seqcount_begin(&vma->vm_sequence);
}
static inline void vm_write_end(struct vm_area_struct *vma)
{
	raw_write_seqcount_end(&vma->vm_sequence);
}
#else
static inline void vm_write_begin(struct vm_area_struct *vma)
{
}
static inline void vm_write_end(struct vm_area_struct *vma)
{
}
#endif /* CONFIG_SPECULATIVE_PAGE_FAULT */

extern void truncate_pagecache(struct inode *inode, loff_t new);
extern void truncate_setsize(struct inode *inode, loff_t newsize);
void pagecache_isize_extended(struct inode *inode, loff_t from, loff_t to);
void truncate_pagecache_range(struct inode *inode, loff_t offset, loff_t end);
int truncate_inode_page(struct address_space *mapping, struct page *page);
int generic_error_remove_page(struct address_space *mapping, struct page *page);
int invalidate_inode_page(struct page *page);

#ifdef CONFIG_MMU
extern vm_fault_t handle_mm_fault(struct vm_area_struct *vma,
			unsigned long address, unsigned int flags);

#ifdef CONFIG_SPECULATIVE_PAGE_FAULT
extern int __handle_speculative_fault(struct mm_struct *mm,
				      unsigned long address,
				      unsigned int flags,
				      struct vm_area_struct **vma);
static inline int handle_speculative_fault(struct mm_struct *mm,
					   unsigned long address,
					   unsigned int flags,
					   struct vm_area_struct **vma)
{
	/*
	 * Try speculative page fault for multithreaded user space task only.
	 */
	if (!(flags & FAULT_FLAG_USER) || atomic_read(&mm->mm_users) == 1) {
		*vma = NULL;
		return VM_FAULT_RETRY;
	}
	return __handle_speculative_fault(mm, address, flags, vma);
}
extern bool can_reuse_spf_vma(struct vm_area_struct *vma,
			      unsigned long address);
#else
static inline int handle_speculative_fault(struct mm_struct *mm,
					   unsigned long address,
					   unsigned int flags,
					   struct vm_area_struct **vma)
{
	return VM_FAULT_RETRY;
}
static inline bool can_reuse_spf_vma(struct vm_area_struct *vma,
				     unsigned long address)
{
	return false;
}
#endif /* CONFIG_SPECULATIVE_PAGE_FAULT */

extern int fixup_user_fault(struct task_struct *tsk, struct mm_struct *mm,
			    unsigned long address, unsigned int fault_flags,
			    bool *unlocked);
void unmap_mapping_page(struct page *page);
void unmap_mapping_pages(struct address_space *mapping,
		pgoff_t start, pgoff_t nr, bool even_cows);
void unmap_mapping_range(struct address_space *mapping,
		loff_t const holebegin, loff_t const holelen, int even_cows);
#else
static inline vm_fault_t handle_mm_fault(struct vm_area_struct *vma,
		unsigned long address, unsigned int flags)
{
	/* should never happen if there's no MMU */
	BUG();
	return VM_FAULT_SIGBUS;
}
static inline int fixup_user_fault(struct task_struct *tsk,
		struct mm_struct *mm, unsigned long address,
		unsigned int fault_flags, bool *unlocked)
{
	/* should never happen if there's no MMU */
	BUG();
	return -EFAULT;
}
static inline void unmap_mapping_page(struct page *page) { }
static inline void unmap_mapping_pages(struct address_space *mapping,
		pgoff_t start, pgoff_t nr, bool even_cows) { }
static inline void unmap_mapping_range(struct address_space *mapping,
		loff_t const holebegin, loff_t const holelen, int even_cows) { }
#endif

static inline void unmap_shared_mapping_range(struct address_space *mapping,
		loff_t const holebegin, loff_t const holelen)
{
	unmap_mapping_range(mapping, holebegin, holelen, 0);
}

extern int access_process_vm(struct task_struct *tsk, unsigned long addr,
		void *buf, int len, unsigned int gup_flags);
extern int access_remote_vm(struct mm_struct *mm, unsigned long addr,
		void *buf, int len, unsigned int gup_flags);
extern int __access_remote_vm(struct task_struct *tsk, struct mm_struct *mm,
		unsigned long addr, void *buf, int len, unsigned int gup_flags);

long get_user_pages_remote(struct task_struct *tsk, struct mm_struct *mm,
			    unsigned long start, unsigned long nr_pages,
			    unsigned int gup_flags, struct page **pages,
			    struct vm_area_struct **vmas, int *locked);
long get_user_pages(unsigned long start, unsigned long nr_pages,
			    unsigned int gup_flags, struct page **pages,
			    struct vm_area_struct **vmas);
long get_user_pages_locked(unsigned long start, unsigned long nr_pages,
		    unsigned int gup_flags, struct page **pages, int *locked);
long get_user_pages_unlocked(unsigned long start, unsigned long nr_pages,
		    struct page **pages, unsigned int gup_flags);

int get_user_pages_fast(unsigned long start, int nr_pages,
			unsigned int gup_flags, struct page **pages);

int account_locked_vm(struct mm_struct *mm, unsigned long pages, bool inc);
int __account_locked_vm(struct mm_struct *mm, unsigned long pages, bool inc,
			struct task_struct *task, bool bypass_rlim);

/* Container for pinned pfns / pages */
struct frame_vector {
	unsigned int nr_allocated;	/* Number of frames we have space for */
	unsigned int nr_frames;	/* Number of frames stored in ptrs array */
	bool got_ref;		/* Did we pin pages by getting page ref? */
	bool is_pfns;		/* Does array contain pages or pfns? */
	void *ptrs[0];		/* Array of pinned pfns / pages. Use
				 * pfns_vector_pages() or pfns_vector_pfns()
				 * for access */
};

struct frame_vector *frame_vector_create(unsigned int nr_frames);
void frame_vector_destroy(struct frame_vector *vec);
int get_vaddr_frames(unsigned long start, unsigned int nr_pfns,
		     unsigned int gup_flags, struct frame_vector *vec);
void put_vaddr_frames(struct frame_vector *vec);
int frame_vector_to_pages(struct frame_vector *vec);
void frame_vector_to_pfns(struct frame_vector *vec);

static inline unsigned int frame_vector_count(struct frame_vector *vec)
{
	return vec->nr_frames;
}

static inline struct page **frame_vector_pages(struct frame_vector *vec)
{
	if (vec->is_pfns) {
		int err = frame_vector_to_pages(vec);

		if (err)
			return ERR_PTR(err);
	}
	return (struct page **)(vec->ptrs);
}

static inline unsigned long *frame_vector_pfns(struct frame_vector *vec)
{
	if (!vec->is_pfns)
		frame_vector_to_pfns(vec);
	return (unsigned long *)(vec->ptrs);
}

struct kvec;
int get_kernel_pages(const struct kvec *iov, int nr_pages, int write,
			struct page **pages);
int get_kernel_page(unsigned long start, int write, struct page **pages);
struct page *get_dump_page(unsigned long addr);

extern int try_to_release_page(struct page * page, gfp_t gfp_mask);
extern void do_invalidatepage(struct page *page, unsigned int offset,
			      unsigned int length);

void __set_page_dirty(struct page *, struct address_space *, int warn);
int __set_page_dirty_nobuffers(struct page *page);
int __set_page_dirty_no_writeback(struct page *page);
int redirty_page_for_writepage(struct writeback_control *wbc,
				struct page *page);
void account_page_dirtied(struct page *page, struct address_space *mapping);
void account_page_cleaned(struct page *page, struct address_space *mapping,
			  struct bdi_writeback *wb);
int set_page_dirty(struct page *page);
int set_page_dirty_lock(struct page *page);
void __cancel_dirty_page(struct page *page);
static inline void cancel_dirty_page(struct page *page)
{
	/* Avoid atomic ops, locking, etc. when not actually needed. */
	if (PageDirty(page))
		__cancel_dirty_page(page);
}
int clear_page_dirty_for_io(struct page *page);

int get_cmdline(struct task_struct *task, char *buffer, int buflen);

extern unsigned long move_page_tables(struct vm_area_struct *vma,
		unsigned long old_addr, struct vm_area_struct *new_vma,
		unsigned long new_addr, unsigned long len,
		bool need_rmap_locks);
extern unsigned long change_protection(struct vm_area_struct *vma, unsigned long start,
			      unsigned long end, pgprot_t newprot,
			      int dirty_accountable, int prot_numa);
extern int mprotect_fixup(struct vm_area_struct *vma,
			  struct vm_area_struct **pprev, unsigned long start,
			  unsigned long end, unsigned long newflags);

/*
 * doesn't attempt to fault and will return short.
 */
int __get_user_pages_fast(unsigned long start, int nr_pages, int write,
			  struct page **pages);
/*
 * per-process(per-mm_struct) statistics.
 */
static inline unsigned long get_mm_counter(struct mm_struct *mm, int member)
{
	long val = atomic_long_read(&mm->rss_stat.count[member]);

#ifdef SPLIT_RSS_COUNTING
	/*
	 * counter is updated in asynchronous manner and may go to minus.
	 * But it's never be expected number for users.
	 */
	if (val < 0)
		val = 0;
#endif
	return (unsigned long)val;
}

void mm_trace_rss_stat(struct mm_struct *mm, int member, long count,
		       long value);

static inline void add_mm_counter(struct mm_struct *mm, int member, long value)
{
	long count = atomic_long_add_return(value, &mm->rss_stat.count[member]);

	mm_trace_rss_stat(mm, member, count, value);
}

static inline void inc_mm_counter(struct mm_struct *mm, int member)
{
	long count = atomic_long_inc_return(&mm->rss_stat.count[member]);

	mm_trace_rss_stat(mm, member, count, 1);
}

static inline void dec_mm_counter(struct mm_struct *mm, int member)
{
	long count = atomic_long_dec_return(&mm->rss_stat.count[member]);

	mm_trace_rss_stat(mm, member, count, -1);
}

/* Optimized variant when page is already known not to be PageAnon */
static inline int mm_counter_file(struct page *page)
{
	if (PageSwapBacked(page))
		return MM_SHMEMPAGES;
	return MM_FILEPAGES;
}

static inline int mm_counter(struct page *page)
{
	if (PageAnon(page))
		return MM_ANONPAGES;
	return mm_counter_file(page);
}

static inline unsigned long get_mm_rss(struct mm_struct *mm)
{
	return get_mm_counter(mm, MM_FILEPAGES) +
		get_mm_counter(mm, MM_ANONPAGES) +
		get_mm_counter(mm, MM_SHMEMPAGES);
}

static inline unsigned long get_mm_hiwater_rss(struct mm_struct *mm)
{
	return max(mm->hiwater_rss, get_mm_rss(mm));
}

static inline unsigned long get_mm_hiwater_vm(struct mm_struct *mm)
{
	return max(mm->hiwater_vm, mm->total_vm);
}

static inline void update_hiwater_rss(struct mm_struct *mm)
{
	unsigned long _rss = get_mm_rss(mm);

	if ((mm)->hiwater_rss < _rss)
		(mm)->hiwater_rss = _rss;
}

static inline void update_hiwater_vm(struct mm_struct *mm)
{
	if (mm->hiwater_vm < mm->total_vm)
		mm->hiwater_vm = mm->total_vm;
}

static inline void reset_mm_hiwater_rss(struct mm_struct *mm)
{
	mm->hiwater_rss = get_mm_rss(mm);
}

static inline void setmax_mm_hiwater_rss(unsigned long *maxrss,
					 struct mm_struct *mm)
{
	unsigned long hiwater_rss = get_mm_hiwater_rss(mm);

	if (*maxrss < hiwater_rss)
		*maxrss = hiwater_rss;
}

#if defined(SPLIT_RSS_COUNTING)
void sync_mm_rss(struct mm_struct *mm);
#else
static inline void sync_mm_rss(struct mm_struct *mm)
{
}
#endif

#ifndef CONFIG_ARCH_HAS_PTE_DEVMAP
static inline int pte_devmap(pte_t pte)
{
	return 0;
}
#endif

int vma_wants_writenotify(struct vm_area_struct *vma, pgprot_t vm_page_prot);

extern pte_t *__get_locked_pte(struct mm_struct *mm, unsigned long addr,
			       spinlock_t **ptl);
static inline pte_t *get_locked_pte(struct mm_struct *mm, unsigned long addr,
				    spinlock_t **ptl)
{
	pte_t *ptep;
	__cond_lock(*ptl, ptep = __get_locked_pte(mm, addr, ptl));
	return ptep;
}

#ifdef __PAGETABLE_P4D_FOLDED
static inline int __p4d_alloc(struct mm_struct *mm, pgd_t *pgd,
						unsigned long address)
{
	return 0;
}
#else
int __p4d_alloc(struct mm_struct *mm, pgd_t *pgd, unsigned long address);
#endif

#if defined(__PAGETABLE_PUD_FOLDED) || !defined(CONFIG_MMU)
static inline int __pud_alloc(struct mm_struct *mm, p4d_t *p4d,
						unsigned long address)
{
	return 0;
}
static inline void mm_inc_nr_puds(struct mm_struct *mm) {}
static inline void mm_dec_nr_puds(struct mm_struct *mm) {}

#else
int __pud_alloc(struct mm_struct *mm, p4d_t *p4d, unsigned long address);

static inline void mm_inc_nr_puds(struct mm_struct *mm)
{
	if (mm_pud_folded(mm))
		return;
	atomic_long_add(PTRS_PER_PUD * sizeof(pud_t), &mm->pgtables_bytes);
}

static inline void mm_dec_nr_puds(struct mm_struct *mm)
{
	if (mm_pud_folded(mm))
		return;
	atomic_long_sub(PTRS_PER_PUD * sizeof(pud_t), &mm->pgtables_bytes);
}
#endif

#if defined(__PAGETABLE_PMD_FOLDED) || !defined(CONFIG_MMU)
static inline int __pmd_alloc(struct mm_struct *mm, pud_t *pud,
						unsigned long address)
{
	return 0;
}

static inline void mm_inc_nr_pmds(struct mm_struct *mm) {}
static inline void mm_dec_nr_pmds(struct mm_struct *mm) {}

#else
int __pmd_alloc(struct mm_struct *mm, pud_t *pud, unsigned long address);

static inline void mm_inc_nr_pmds(struct mm_struct *mm)
{
	if (mm_pmd_folded(mm))
		return;
	atomic_long_add(PTRS_PER_PMD * sizeof(pmd_t), &mm->pgtables_bytes);
}

static inline void mm_dec_nr_pmds(struct mm_struct *mm)
{
	if (mm_pmd_folded(mm))
		return;
	atomic_long_sub(PTRS_PER_PMD * sizeof(pmd_t), &mm->pgtables_bytes);
}
#endif

#ifdef CONFIG_MMU
static inline void mm_pgtables_bytes_init(struct mm_struct *mm)
{
	atomic_long_set(&mm->pgtables_bytes, 0);
}

static inline unsigned long mm_pgtables_bytes(const struct mm_struct *mm)
{
	return atomic_long_read(&mm->pgtables_bytes);
}

static inline void mm_inc_nr_ptes(struct mm_struct *mm)
{
	atomic_long_add(PTRS_PER_PTE * sizeof(pte_t), &mm->pgtables_bytes);
}

static inline void mm_dec_nr_ptes(struct mm_struct *mm)
{
	atomic_long_sub(PTRS_PER_PTE * sizeof(pte_t), &mm->pgtables_bytes);
}
#else

static inline void mm_pgtables_bytes_init(struct mm_struct *mm) {}
static inline unsigned long mm_pgtables_bytes(const struct mm_struct *mm)
{
	return 0;
}

static inline void mm_inc_nr_ptes(struct mm_struct *mm) {}
static inline void mm_dec_nr_ptes(struct mm_struct *mm) {}
#endif

int __pte_alloc(struct mm_struct *mm, pmd_t *pmd);
int __pte_alloc_kernel(pmd_t *pmd);

/*
 * The following ifdef needed to get the 4level-fixup.h header to work.
 * Remove it when 4level-fixup.h has been removed.
 */
#if defined(CONFIG_MMU) && !defined(__ARCH_HAS_4LEVEL_HACK)

#ifndef __ARCH_HAS_5LEVEL_HACK
static inline p4d_t *p4d_alloc(struct mm_struct *mm, pgd_t *pgd,
		unsigned long address)
{
	return (unlikely(pgd_none(*pgd)) && __p4d_alloc(mm, pgd, address)) ?
		NULL : p4d_offset(pgd, address);
}

static inline pud_t *pud_alloc(struct mm_struct *mm, p4d_t *p4d,
		unsigned long address)
{
	return (unlikely(p4d_none(*p4d)) && __pud_alloc(mm, p4d, address)) ?
		NULL : pud_offset(p4d, address);
}
#endif /* !__ARCH_HAS_5LEVEL_HACK */

static inline pmd_t *pmd_alloc(struct mm_struct *mm, pud_t *pud, unsigned long address)
{
	return (unlikely(pud_none(*pud)) && __pmd_alloc(mm, pud, address))?
		NULL: pmd_offset(pud, address);
}
#endif /* CONFIG_MMU && !__ARCH_HAS_4LEVEL_HACK */

#if USE_SPLIT_PTE_PTLOCKS
#if ALLOC_SPLIT_PTLOCKS
void __init ptlock_cache_init(void);
extern bool ptlock_alloc(struct page *page);
extern void ptlock_free(struct page *page);

static inline spinlock_t *ptlock_ptr(struct page *page)
{
	return page->ptl;
}
#else /* ALLOC_SPLIT_PTLOCKS */
static inline void ptlock_cache_init(void)
{
}

static inline bool ptlock_alloc(struct page *page)
{
	return true;
}

static inline void ptlock_free(struct page *page)
{
}

static inline spinlock_t *ptlock_ptr(struct page *page)
{
	return &page->ptl;
}
#endif /* ALLOC_SPLIT_PTLOCKS */

static inline spinlock_t *pte_lockptr(struct mm_struct *mm, pmd_t *pmd)
{
	return ptlock_ptr(pmd_page(*pmd));
}

static inline bool ptlock_init(struct page *page)
{
	/*
	 * prep_new_page() initialize page->private (and therefore page->ptl)
	 * with 0. Make sure nobody took it in use in between.
	 *
	 * It can happen if arch try to use slab for page table allocation:
	 * slab code uses page->slab_cache, which share storage with page->ptl.
	 */
	VM_BUG_ON_PAGE(*(unsigned long *)&page->ptl, page);
	if (!ptlock_alloc(page))
		return false;
	spin_lock_init(ptlock_ptr(page));
	return true;
}

#else	/* !USE_SPLIT_PTE_PTLOCKS */
/*
 * We use mm->page_table_lock to guard all pagetable pages of the mm.
 */
static inline spinlock_t *pte_lockptr(struct mm_struct *mm, pmd_t *pmd)
{
	return &mm->page_table_lock;
}
static inline void ptlock_cache_init(void) {}
static inline bool ptlock_init(struct page *page) { return true; }
static inline void ptlock_free(struct page *page) {}
#endif /* USE_SPLIT_PTE_PTLOCKS */

static inline void pgtable_init(void)
{
	ptlock_cache_init();
	pgtable_cache_init();
}

static inline bool pgtable_pte_page_ctor(struct page *page)
{
	if (!ptlock_init(page))
		return false;
	__SetPageTable(page);
	inc_zone_page_state(page, NR_PAGETABLE);
	return true;
}

static inline void pgtable_pte_page_dtor(struct page *page)
{
	ptlock_free(page);
	__ClearPageTable(page);
	dec_zone_page_state(page, NR_PAGETABLE);
}

#define pte_offset_map_lock(mm, pmd, address, ptlp)	\
({							\
	spinlock_t *__ptl = pte_lockptr(mm, pmd);	\
	pte_t *__pte = pte_offset_map(pmd, address);	\
	*(ptlp) = __ptl;				\
	spin_lock(__ptl);				\
	__pte;						\
})

#define pte_unmap_unlock(pte, ptl)	do {		\
	spin_unlock(ptl);				\
	pte_unmap(pte);					\
} while (0)

#define pte_alloc(mm, pmd) (unlikely(pmd_none(*(pmd))) && __pte_alloc(mm, pmd))

#define pte_alloc_map(mm, pmd, address)			\
	(pte_alloc(mm, pmd) ? NULL : pte_offset_map(pmd, address))

#define pte_alloc_map_lock(mm, pmd, address, ptlp)	\
	(pte_alloc(mm, pmd) ?			\
		 NULL : pte_offset_map_lock(mm, pmd, address, ptlp))

#define pte_alloc_kernel(pmd, address)			\
	((unlikely(pmd_none(*(pmd))) && __pte_alloc_kernel(pmd))? \
		NULL: pte_offset_kernel(pmd, address))

#if USE_SPLIT_PMD_PTLOCKS

static struct page *pmd_to_page(pmd_t *pmd)
{
	unsigned long mask = ~(PTRS_PER_PMD * sizeof(pmd_t) - 1);
	return virt_to_page((void *)((unsigned long) pmd & mask));
}

static inline spinlock_t *pmd_lockptr(struct mm_struct *mm, pmd_t *pmd)
{
	return ptlock_ptr(pmd_to_page(pmd));
}

static inline bool pgtable_pmd_page_ctor(struct page *page)
{
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	page->pmd_huge_pte = NULL;
#endif
	return ptlock_init(page);
}

static inline void pgtable_pmd_page_dtor(struct page *page)
{
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	VM_BUG_ON_PAGE(page->pmd_huge_pte, page);
#endif
	ptlock_free(page);
}

#define pmd_huge_pte(mm, pmd) (pmd_to_page(pmd)->pmd_huge_pte)

#else

static inline spinlock_t *pmd_lockptr(struct mm_struct *mm, pmd_t *pmd)
{
	return &mm->page_table_lock;
}

static inline bool pgtable_pmd_page_ctor(struct page *page) { return true; }
static inline void pgtable_pmd_page_dtor(struct page *page) {}

#define pmd_huge_pte(mm, pmd) ((mm)->pmd_huge_pte)

#endif

static inline spinlock_t *pmd_lock(struct mm_struct *mm, pmd_t *pmd)
{
	spinlock_t *ptl = pmd_lockptr(mm, pmd);
	spin_lock(ptl);
	return ptl;
}

/*
 * No scalability reason to split PUD locks yet, but follow the same pattern
 * as the PMD locks to make it easier if we decide to.  The VM should not be
 * considered ready to switch to split PUD locks yet; there may be places
 * which need to be converted from page_table_lock.
 */
static inline spinlock_t *pud_lockptr(struct mm_struct *mm, pud_t *pud)
{
	return &mm->page_table_lock;
}

static inline spinlock_t *pud_lock(struct mm_struct *mm, pud_t *pud)
{
	spinlock_t *ptl = pud_lockptr(mm, pud);

	spin_lock(ptl);
	return ptl;
}

extern void __init pagecache_init(void);
extern void free_area_init(unsigned long * zones_size);
extern void __init free_area_init_node(int nid, unsigned long * zones_size,
		unsigned long zone_start_pfn, unsigned long *zholes_size);
extern void free_initmem(void);

/*
 * Free reserved pages within range [PAGE_ALIGN(start), end & PAGE_MASK)
 * into the buddy system. The freed pages will be poisoned with pattern
 * "poison" if it's within range [0, UCHAR_MAX].
 * Return pages freed into the buddy system.
 */
extern unsigned long free_reserved_area(void *start, void *end,
					int poison, const char *s);

#ifdef	CONFIG_HIGHMEM
/*
 * Free a highmem page into the buddy system, adjusting totalhigh_pages
 * and totalram_pages.
 */
extern void free_highmem_page(struct page *page);
#endif

extern void adjust_managed_page_count(struct page *page, long count);
extern void mem_init_print_info(const char *str);

extern void reserve_bootmem_region(phys_addr_t start, phys_addr_t end);

/* Free the reserved page into the buddy system, so it gets managed. */
static inline void __free_reserved_page(struct page *page)
{
	ClearPageReserved(page);
	init_page_count(page);
	__free_page(page);
}

static inline void free_reserved_page(struct page *page)
{
	__free_reserved_page(page);
	adjust_managed_page_count(page, 1);
}

static inline void mark_page_reserved(struct page *page)
{
	SetPageReserved(page);
	adjust_managed_page_count(page, -1);
}

/*
 * Default method to free all the __init memory into the buddy system.
 * The freed pages will be poisoned with pattern "poison" if it's within
 * range [0, UCHAR_MAX].
 * Return pages freed into the buddy system.
 */
static inline unsigned long free_initmem_default(int poison)
{
	extern char __init_begin[], __init_end[];

	return free_reserved_area(&__init_begin, &__init_end,
				  poison, "unused kernel");
}

static inline unsigned long get_num_physpages(void)
{
	int nid;
	unsigned long phys_pages = 0;

	for_each_online_node(nid)
		phys_pages += node_present_pages(nid);

	return phys_pages;
}

#ifdef CONFIG_HAVE_MEMBLOCK_NODE_MAP
/*
 * With CONFIG_HAVE_MEMBLOCK_NODE_MAP set, an architecture may initialise its
 * zones, allocate the backing mem_map and account for memory holes in a more
 * architecture independent manner. This is a substitute for creating the
 * zone_sizes[] and zholes_size[] arrays and passing them to
 * free_area_init_node()
 *
 * An architecture is expected to register range of page frames backed by
 * physical memory with memblock_add[_node]() before calling
 * free_area_init_nodes() passing in the PFN each zone ends at. At a basic
 * usage, an architecture is expected to do something like
 *
 * unsigned long max_zone_pfns[MAX_NR_ZONES] = {max_dma, max_normal_pfn,
 * 							 max_highmem_pfn};
 * for_each_valid_physical_page_range()
 * 	memblock_add_node(base, size, nid)
 * free_area_init_nodes(max_zone_pfns);
 *
 * free_bootmem_with_active_regions() calls free_bootmem_node() for each
 * registered physical page range.  Similarly
 * sparse_memory_present_with_active_regions() calls memory_present() for
 * each range when SPARSEMEM is enabled.
 *
 * See mm/page_alloc.c for more information on each function exposed by
 * CONFIG_HAVE_MEMBLOCK_NODE_MAP.
 */
extern void free_area_init_nodes(unsigned long *max_zone_pfn);
unsigned long node_map_pfn_alignment(void);
unsigned long __absent_pages_in_range(int nid, unsigned long start_pfn,
						unsigned long end_pfn);
extern unsigned long absent_pages_in_range(unsigned long start_pfn,
						unsigned long end_pfn);
extern void get_pfn_range_for_nid(unsigned int nid,
			unsigned long *start_pfn, unsigned long *end_pfn);
extern unsigned long find_min_pfn_with_active_regions(void);
extern void free_bootmem_with_active_regions(int nid,
						unsigned long max_low_pfn);
extern void sparse_memory_present_with_active_regions(int nid);

#endif /* CONFIG_HAVE_MEMBLOCK_NODE_MAP */

#if !defined(CONFIG_HAVE_MEMBLOCK_NODE_MAP) && \
    !defined(CONFIG_HAVE_ARCH_EARLY_PFN_TO_NID)
static inline int __early_pfn_to_nid(unsigned long pfn,
					struct mminit_pfnnid_cache *state)
{
	return 0;
}
#else
/* please see mm/page_alloc.c */
extern int __meminit early_pfn_to_nid(unsigned long pfn);
/* there is a per-arch backend function. */
extern int __meminit __early_pfn_to_nid(unsigned long pfn,
					struct mminit_pfnnid_cache *state);
#endif

#if !defined(CONFIG_FLAT_NODE_MEM_MAP)
void zero_resv_unavail(void);
#else
static inline void zero_resv_unavail(void) {}
#endif

extern void set_dma_reserve(unsigned long new_dma_reserve);
extern void memmap_init_zone(unsigned long, int, unsigned long, unsigned long,
		enum meminit_context, struct vmem_altmap *);
extern void setup_per_zone_wmarks(void);
extern void update_kswapd_threads(void);
extern int __meminit init_per_zone_wmark_min(void);
extern void mem_init(void);
extern void __init mmap_init(void);
extern void show_mem(unsigned int flags, nodemask_t *nodemask);
extern long si_mem_available(void);
extern void si_meminfo(struct sysinfo * val);
extern void si_meminfo_node(struct sysinfo *val, int nid);
#ifdef __HAVE_ARCH_RESERVED_KERNEL_PAGES
extern unsigned long arch_reserved_kernel_pages(void);
#endif

extern __printf(3, 4)
void warn_alloc(gfp_t gfp_mask, nodemask_t *nodemask, const char *fmt, ...);

extern void setup_per_cpu_pageset(void);

extern void zone_pcp_update(struct zone *zone);
extern void zone_pcp_reset(struct zone *zone);

/* page_alloc.c */
extern int kswapd_threads;
extern int min_free_kbytes;
extern int watermark_boost_factor;
extern int watermark_scale_factor;

/* nommu.c */
extern atomic_long_t mmap_pages_allocated;
extern int nommu_shrink_inode_mappings(struct inode *, size_t, size_t);

#ifdef CONFIG_OPLUS_SENSITIVE_MEM_ALLOC_OPT
extern atomic_long_t oplus_sensitive_mem_allocated;
int oplus_sensitive_mem_pages(unsigned int order, int migratetype);
#endif

/* interval_tree.c */
void vma_interval_tree_insert(struct vm_area_struct *node,
			      struct rb_root_cached *root);
void vma_interval_tree_insert_after(struct vm_area_struct *node,
				    struct vm_area_struct *prev,
				    struct rb_root_cached *root);
void vma_interval_tree_remove(struct vm_area_struct *node,
			      struct rb_root_cached *root);
struct vm_area_struct *vma_interval_tree_iter_first(struct rb_root_cached *root,
				unsigned long start, unsigned long last);
struct vm_area_struct *vma_interval_tree_iter_next(struct vm_area_struct *node,
				unsigned long start, unsigned long last);

#define vma_interval_tree_foreach(vma, root, start, last)		\
	for (vma = vma_interval_tree_iter_first(root, start, last);	\
	     vma; vma = vma_interval_tree_iter_next(vma, start, last))

void anon_vma_interval_tree_insert(struct anon_vma_chain *node,
				   struct rb_root_cached *root);
void anon_vma_interval_tree_remove(struct anon_vma_chain *node,
				   struct rb_root_cached *root);
struct anon_vma_chain *
anon_vma_interval_tree_iter_first(struct rb_root_cached *root,
				  unsigned long start, unsigned long last);
struct anon_vma_chain *anon_vma_interval_tree_iter_next(
	struct anon_vma_chain *node, unsigned long start, unsigned long last);
#ifdef CONFIG_DEBUG_VM_RB
void anon_vma_interval_tree_verify(struct anon_vma_chain *node);
#endif

#define anon_vma_interval_tree_foreach(avc, root, start, last)		 \
	for (avc = anon_vma_interval_tree_iter_first(root, start, last); \
	     avc; avc = anon_vma_interval_tree_iter_next(avc, start, last))

/* mmap.c */
extern int __vm_enough_memory(struct mm_struct *mm, long pages, int cap_sys_admin);
extern int __vma_adjust(struct vm_area_struct *vma, unsigned long start,
	unsigned long end, pgoff_t pgoff, struct vm_area_struct *insert,
	struct vm_area_struct *expand, bool keep_locked);
static inline int vma_adjust(struct vm_area_struct *vma, unsigned long start,
	unsigned long end, pgoff_t pgoff, struct vm_area_struct *insert)
{
	return __vma_adjust(vma, start, end, pgoff, insert, NULL, false);
}

extern struct vm_area_struct *__vma_merge(struct mm_struct *mm,
	struct vm_area_struct *prev, unsigned long addr, unsigned long end,
	unsigned long vm_flags, struct anon_vma *anon, struct file *file,
	pgoff_t pgoff, struct mempolicy *mpol, struct vm_userfaultfd_ctx uff,
	const char __user *user, bool keep_locked);

static inline struct vm_area_struct *vma_merge(struct mm_struct *mm,
	struct vm_area_struct *prev, unsigned long addr, unsigned long end,
	unsigned long vm_flags, struct anon_vma *anon, struct file *file,
	pgoff_t off, struct mempolicy *pol, struct vm_userfaultfd_ctx uff,
	const char __user *user)
{
	return __vma_merge(mm, prev, addr, end, vm_flags, anon, file, off,
			   pol, uff, user, false);
}

extern struct anon_vma *find_mergeable_anon_vma(struct vm_area_struct *);
extern int __split_vma(struct mm_struct *, struct vm_area_struct *,
	unsigned long addr, int new_below);
extern int split_vma(struct mm_struct *, struct vm_area_struct *,
	unsigned long addr, int new_below);
extern int insert_vm_struct(struct mm_struct *, struct vm_area_struct *);
extern void __vma_link_rb(struct mm_struct *, struct vm_area_struct *,
	struct rb_node **, struct rb_node *);
extern void unlink_file_vma(struct vm_area_struct *);
extern struct vm_area_struct *copy_vma(struct vm_area_struct **,
	unsigned long addr, unsigned long len, pgoff_t pgoff,
	bool *need_rmap_locks);
extern void exit_mmap(struct mm_struct *);

static inline int check_data_rlimit(unsigned long rlim,
				    unsigned long new,
				    unsigned long start,
				    unsigned long end_data,
				    unsigned long start_data)
{
	if (rlim < RLIM_INFINITY) {
		if (((new - start) + (end_data - start_data)) > rlim)
			return -ENOSPC;
	}

	return 0;
}

extern int mm_take_all_locks(struct mm_struct *mm);
extern void mm_drop_all_locks(struct mm_struct *mm);

extern void set_mm_exe_file(struct mm_struct *mm, struct file *new_exe_file);
extern struct file *get_mm_exe_file(struct mm_struct *mm);
extern struct file *get_task_exe_file(struct task_struct *task);

extern bool may_expand_vm(struct mm_struct *, vm_flags_t, unsigned long npages);
extern void vm_stat_account(struct mm_struct *, vm_flags_t, long npages);

extern bool vma_is_special_mapping(const struct vm_area_struct *vma,
				   const struct vm_special_mapping *sm);
extern struct vm_area_struct *_install_special_mapping(struct mm_struct *mm,
				   unsigned long addr, unsigned long len,
				   unsigned long flags,
				   const struct vm_special_mapping *spec);
/* This is an obsolete alternative to _install_special_mapping. */
extern int install_special_mapping(struct mm_struct *mm,
				   unsigned long addr, unsigned long len,
				   unsigned long flags, struct page **pages);

unsigned long randomize_stack_top(unsigned long stack_top);
unsigned long randomize_page(unsigned long start, unsigned long range);

extern unsigned long get_unmapped_area(struct file *, unsigned long, unsigned long, unsigned long, unsigned long);

extern unsigned long mmap_region(struct file *file, unsigned long addr,
	unsigned long len, vm_flags_t vm_flags, unsigned long pgoff,
	struct list_head *uf);
extern unsigned long do_mmap(struct file *file, unsigned long addr,
	unsigned long len, unsigned long prot, unsigned long flags,
	vm_flags_t vm_flags, unsigned long pgoff, unsigned long *populate,
	struct list_head *uf);
extern int __do_munmap(struct mm_struct *, unsigned long, size_t,
		       struct list_head *uf, bool downgrade);
extern int do_munmap(struct mm_struct *, unsigned long, size_t,
		     struct list_head *uf);
extern int do_madvise(struct task_struct *target_task, struct mm_struct *mm,
		unsigned long start, size_t len_in, int behavior);

static inline unsigned long
do_mmap_pgoff(struct file *file, unsigned long addr,
	unsigned long len, unsigned long prot, unsigned long flags,
	unsigned long pgoff, unsigned long *populate,
	struct list_head *uf)
{
	return do_mmap(file, addr, len, prot, flags, 0, pgoff, populate, uf);
}

#ifdef CONFIG_MMU
extern int __mm_populate(unsigned long addr, unsigned long len,
			 int ignore_errors);
static inline void mm_populate(unsigned long addr, unsigned long len)
{
	/* Ignore errors */
	(void) __mm_populate(addr, len, 1);
}
#else
static inline void mm_populate(unsigned long addr, unsigned long len) {}
#endif

/* These take the mm semaphore themselves */
extern int __must_check vm_brk(unsigned long, unsigned long);
extern int __must_check vm_brk_flags(unsigned long, unsigned long, unsigned long);
extern int vm_munmap(unsigned long, size_t);
extern unsigned long __must_check vm_mmap(struct file *, unsigned long,
        unsigned long, unsigned long,
        unsigned long, unsigned long);

struct vm_unmapped_area_info {
#define VM_UNMAPPED_AREA_TOPDOWN 1
	unsigned long flags;
	unsigned long length;
	unsigned long low_limit;
	unsigned long high_limit;
	unsigned long align_mask;
	unsigned long align_offset;
};

extern unsigned long unmapped_area(struct vm_unmapped_area_info *info);
extern unsigned long unmapped_area_topdown(struct vm_unmapped_area_info *info);

/*
 * Search for an unmapped address range.
 *
 * We are looking for a range that:
 * - does not intersect with any VMA;
 * - is contained within the [low_limit, high_limit) interval;
 * - is at least the desired size.
 * - satisfies (begin_addr & align_mask) == (align_offset & align_mask)
 */
static inline unsigned long
vm_unmapped_area(struct vm_unmapped_area_info *info)
{
	if (info->flags & VM_UNMAPPED_AREA_TOPDOWN)
		return unmapped_area_topdown(info);
	else
		return unmapped_area(info);
}

/* truncate.c */
extern void truncate_inode_pages(struct address_space *, loff_t);
extern void truncate_inode_pages_range(struct address_space *,
				       loff_t lstart, loff_t lend);
extern void truncate_inode_pages_final(struct address_space *);

/* generic vm_area_ops exported for stackable file systems */
extern vm_fault_t filemap_fault(struct vm_fault *vmf);
extern void filemap_map_pages(struct vm_fault *vmf,
		pgoff_t start_pgoff, pgoff_t end_pgoff);
extern vm_fault_t filemap_page_mkwrite(struct vm_fault *vmf);

/* mm/page-writeback.c */
int __must_check write_one_page(struct page *page);
void task_dirty_inc(struct task_struct *tsk);

/* readahead.c */
#define VM_READAHEAD_PAGES	(SZ_512K / PAGE_SIZE)

int force_page_cache_readahead(struct address_space *mapping, struct file *filp,
			pgoff_t offset, unsigned long nr_to_read);

void page_cache_sync_readahead(struct address_space *mapping,
			       struct file_ra_state *ra,
			       struct file *filp,
			       pgoff_t offset,
			       unsigned long size);

void page_cache_async_readahead(struct address_space *mapping,
				struct file_ra_state *ra,
				struct file *filp,
				struct page *pg,
				pgoff_t offset,
				unsigned long size);

extern unsigned long stack_guard_gap;
/* Generic expand stack which grows the stack according to GROWS{UP,DOWN} */
extern int expand_stack(struct vm_area_struct *vma, unsigned long address);

/* CONFIG_STACK_GROWSUP still needs to to grow downwards at some places */
extern int expand_downwards(struct vm_area_struct *vma,
		unsigned long address);
#if VM_GROWSUP
extern int expand_upwards(struct vm_area_struct *vma, unsigned long address);
#else
  #define expand_upwards(vma, address) (0)
#endif

/* Look up the first VMA which satisfies  addr < vm_end,  NULL if none. */
extern struct vm_area_struct * find_vma(struct mm_struct * mm, unsigned long addr);
extern struct vm_area_struct * find_vma_prev(struct mm_struct * mm, unsigned long addr,
					     struct vm_area_struct **pprev);

/* Look up the first VMA which intersects the interval start_addr..end_addr-1,
   NULL if none.  Assume start_addr < end_addr. */
static inline struct vm_area_struct * find_vma_intersection(struct mm_struct * mm, unsigned long start_addr, unsigned long end_addr)
{
	struct vm_area_struct * vma = find_vma(mm,start_addr);

	if (vma && end_addr <= vma->vm_start)
		vma = NULL;
	return vma;
}

static inline unsigned long vm_start_gap(struct vm_area_struct *vma)
{
	unsigned long vm_start = vma->vm_start;

	if (vma->vm_flags & VM_GROWSDOWN) {
		vm_start -= stack_guard_gap;
		if (vm_start > vma->vm_start)
			vm_start = 0;
	}
	return vm_start;
}

static inline unsigned long vm_end_gap(struct vm_area_struct *vma)
{
	unsigned long vm_end = vma->vm_end;

	if (vma->vm_flags & VM_GROWSUP) {
		vm_end += stack_guard_gap;
		if (vm_end < vma->vm_end)
			vm_end = -PAGE_SIZE;
	}
	return vm_end;
}

static inline unsigned long vma_pages(struct vm_area_struct *vma)
{
	return (vma->vm_end - vma->vm_start) >> PAGE_SHIFT;
}

/* Look up the first VMA which exactly match the interval vm_start ... vm_end */
static inline struct vm_area_struct *find_exact_vma(struct mm_struct *mm,
				unsigned long vm_start, unsigned long vm_end)
{
	struct vm_area_struct *vma = find_vma(mm, vm_start);

	if (vma && (vma->vm_start != vm_start || vma->vm_end != vm_end))
		vma = NULL;

	return vma;
}

static inline bool range_in_vma(struct vm_area_struct *vma,
				unsigned long start, unsigned long end)
{
	return (vma && vma->vm_start <= start && end <= vma->vm_end);
}

#ifdef CONFIG_MMU
pgprot_t vm_get_page_prot(unsigned long vm_flags);
void vma_set_page_prot(struct vm_area_struct *vma);
#else
static inline pgprot_t vm_get_page_prot(unsigned long vm_flags)
{
	return __pgprot(0);
}
static inline void vma_set_page_prot(struct vm_area_struct *vma)
{
	vma->vm_page_prot = vm_get_page_prot(vma->vm_flags);
}
#endif

#ifdef CONFIG_NUMA_BALANCING
unsigned long change_prot_numa(struct vm_area_struct *vma,
			unsigned long start, unsigned long end);
#endif

struct vm_area_struct *find_extend_vma(struct mm_struct *, unsigned long addr);
int remap_pfn_range(struct vm_area_struct *, unsigned long addr,
			unsigned long pfn, unsigned long size, pgprot_t);
int vm_insert_page(struct vm_area_struct *, unsigned long addr, struct page *);
int vm_map_pages(struct vm_area_struct *vma, struct page **pages,
				unsigned long num);
int vm_map_pages_zero(struct vm_area_struct *vma, struct page **pages,
				unsigned long num);
vm_fault_t vmf_insert_pfn(struct vm_area_struct *vma, unsigned long addr,
			unsigned long pfn);
vm_fault_t vmf_insert_pfn_prot(struct vm_area_struct *vma, unsigned long addr,
			unsigned long pfn, pgprot_t pgprot);
vm_fault_t vmf_insert_mixed(struct vm_area_struct *vma, unsigned long addr,
			pfn_t pfn);
vm_fault_t vmf_insert_mixed_mkwrite(struct vm_area_struct *vma,
		unsigned long addr, pfn_t pfn);
int vm_iomap_memory(struct vm_area_struct *vma, phys_addr_t start, unsigned long len);

static inline vm_fault_t vmf_insert_page(struct vm_area_struct *vma,
				unsigned long addr, struct page *page)
{
	int err = vm_insert_page(vma, addr, page);

	if (err == -ENOMEM)
		return VM_FAULT_OOM;
	if (err < 0 && err != -EBUSY)
		return VM_FAULT_SIGBUS;

	return VM_FAULT_NOPAGE;
}

#ifndef io_remap_pfn_range
static inline int io_remap_pfn_range(struct vm_area_struct *vma,
				     unsigned long addr, unsigned long pfn,
				     unsigned long size, pgprot_t prot)
{
	return remap_pfn_range(vma, addr, pfn, size, pgprot_decrypted(prot));
}
#endif

static inline vm_fault_t vmf_error(int err)
{
	if (err == -ENOMEM)
		return VM_FAULT_OOM;
	return VM_FAULT_SIGBUS;
}

struct page *follow_page(struct vm_area_struct *vma, unsigned long address,
			 unsigned int foll_flags);

#define FOLL_WRITE	0x01	/* check pte is writable */
#define FOLL_TOUCH	0x02	/* mark page accessed */
#define FOLL_GET	0x04	/* do get_page on page */
#define FOLL_DUMP	0x08	/* give error on hole if it would be zero */
#define FOLL_FORCE	0x10	/* get_user_pages read/write w/o permission */
#define FOLL_NOWAIT	0x20	/* if a disk transfer is needed, start the IO
				 * and return without waiting upon it */
#define FOLL_POPULATE	0x40	/* fault in page */
#define FOLL_SPLIT	0x80	/* don't return transhuge pages, split them */
#define FOLL_HWPOISON	0x100	/* check page is hwpoisoned */
#define FOLL_NUMA	0x200	/* force NUMA hinting page fault */
#define FOLL_MIGRATION	0x400	/* wait for page to replace migration entry */
#define FOLL_TRIED	0x800	/* a retry, previous pass started an IO */
#define FOLL_MLOCK	0x1000	/* lock present pages */
#define FOLL_REMOTE	0x2000	/* we are working on non-current tsk/mm */
#define FOLL_COW	0x4000	/* internal GUP flag */
#define FOLL_ANON	0x8000	/* don't do file mappings */
#define FOLL_LONGTERM	0x10000	/* mapping lifetime is indefinite: see below */
#define FOLL_SPLIT_PMD	0x20000	/* split huge pmd before returning */

/*
 * NOTE on FOLL_LONGTERM:
 *
 * FOLL_LONGTERM indicates that the page will be held for an indefinite time
 * period _often_ under userspace control.  This is contrasted with
 * iov_iter_get_pages() where usages which are transient.
 *
 * FIXME: For pages which are part of a filesystem, mappings are subject to the
 * lifetime enforced by the filesystem and we need guarantees that longterm
 * users like RDMA and V4L2 only establish mappings which coordinate usage with
 * the filesystem.  Ideas for this coordination include revoking the longterm
 * pin, delaying writeback, bounce buffer page writeback, etc.  As FS DAX was
 * added after the problem with filesystems was found FS DAX VMAs are
 * specifically failed.  Filesystem pages are still subject to bugs and use of
 * FOLL_LONGTERM should be avoided on those pages.
 *
 * FIXME: Also NOTE that FOLL_LONGTERM is not supported in every GUP call.
 * Currently only get_user_pages() and get_user_pages_fast() support this flag
 * and calls to get_user_pages_[un]locked are specifically not allowed.  This
 * is due to an incompatibility with the FS DAX check and
 * FAULT_FLAG_ALLOW_RETRY
 *
 * In the CMA case: longterm pins in a CMA region would unnecessarily fragment
 * that region.  And so CMA attempts to migrate the page before pinning when
 * FOLL_LONGTERM is specified.
 */

static inline int vm_fault_to_errno(vm_fault_t vm_fault, int foll_flags)
{
	if (vm_fault & VM_FAULT_OOM)
		return -ENOMEM;
	if (vm_fault & (VM_FAULT_HWPOISON | VM_FAULT_HWPOISON_LARGE))
		return (foll_flags & FOLL_HWPOISON) ? -EHWPOISON : -EFAULT;
	if (vm_fault & (VM_FAULT_SIGBUS | VM_FAULT_SIGSEGV))
		return -EFAULT;
	return 0;
}

typedef int (*pte_fn_t)(pte_t *pte, unsigned long addr, void *data);
extern int apply_to_page_range(struct mm_struct *mm, unsigned long address,
			       unsigned long size, pte_fn_t fn, void *data);


#ifdef CONFIG_PAGE_POISONING
extern bool page_poisoning_enabled(void);
extern void kernel_poison_pages(struct page *page, int numpages, int enable);
#else
static inline bool page_poisoning_enabled(void) { return false; }
static inline void kernel_poison_pages(struct page *page, int numpages,
					int enable) { }
#endif

#ifdef CONFIG_INIT_ON_ALLOC_DEFAULT_ON
DECLARE_STATIC_KEY_TRUE(init_on_alloc);
#else
DECLARE_STATIC_KEY_FALSE(init_on_alloc);
#endif
static inline bool want_init_on_alloc(gfp_t flags)
{
	if (static_branch_unlikely(&init_on_alloc) &&
	    !page_poisoning_enabled())
		return true;
	return flags & __GFP_ZERO;
}

#ifdef CONFIG_INIT_ON_FREE_DEFAULT_ON
DECLARE_STATIC_KEY_TRUE(init_on_free);
#else
DECLARE_STATIC_KEY_FALSE(init_on_free);
#endif
static inline bool want_init_on_free(void)
{
	return static_branch_unlikely(&init_on_free) &&
	       !page_poisoning_enabled();
}

#ifdef CONFIG_DEBUG_PAGEALLOC
extern void init_debug_pagealloc(void);
#else
static inline void init_debug_pagealloc(void) {}
#endif
extern bool _debug_pagealloc_enabled_early;
DECLARE_STATIC_KEY_FALSE(_debug_pagealloc_enabled);

static inline bool debug_pagealloc_enabled(void)
{
	return IS_ENABLED(CONFIG_DEBUG_PAGEALLOC) &&
		_debug_pagealloc_enabled_early;
}

/*
 * For use in fast paths after init_debug_pagealloc() has run, or when a
 * false negative result is not harmful when called too early.
 */
static inline bool debug_pagealloc_enabled_static(void)
{
	if (!IS_ENABLED(CONFIG_DEBUG_PAGEALLOC))
		return false;

	return static_branch_unlikely(&_debug_pagealloc_enabled);
}

#if defined(CONFIG_DEBUG_PAGEALLOC) || defined(CONFIG_ARCH_HAS_SET_DIRECT_MAP)
extern void __kernel_map_pages(struct page *page, int numpages, int enable);

/*
 * When called in DEBUG_PAGEALLOC context, the call should most likely be
 * guarded by debug_pagealloc_enabled() or debug_pagealloc_enabled_static()
 */
static inline void
kernel_map_pages(struct page *page, int numpages, int enable)
{
	__kernel_map_pages(page, numpages, enable);
}
#ifdef CONFIG_HIBERNATION
extern bool kernel_page_present(struct page *page);
#endif	/* CONFIG_HIBERNATION */
#else	/* CONFIG_DEBUG_PAGEALLOC || CONFIG_ARCH_HAS_SET_DIRECT_MAP */
static inline void
kernel_map_pages(struct page *page, int numpages, int enable) {}
#ifdef CONFIG_HIBERNATION
static inline bool kernel_page_present(struct page *page) { return true; }
#endif	/* CONFIG_HIBERNATION */
#endif	/* CONFIG_DEBUG_PAGEALLOC || CONFIG_ARCH_HAS_SET_DIRECT_MAP */

#ifdef __HAVE_ARCH_GATE_AREA
extern struct vm_area_struct *get_gate_vma(struct mm_struct *mm);
extern int in_gate_area_no_mm(unsigned long addr);
extern int in_gate_area(struct mm_struct *mm, unsigned long addr);
#else
static inline struct vm_area_struct *get_gate_vma(struct mm_struct *mm)
{
	return NULL;
}
static inline int in_gate_area_no_mm(unsigned long addr) { return 0; }
static inline int in_gate_area(struct mm_struct *mm, unsigned long addr)
{
	return 0;
}
#endif	/* __HAVE_ARCH_GATE_AREA */

extern bool process_shares_mm(struct task_struct *p, struct mm_struct *mm);

#ifdef CONFIG_SYSCTL
extern int sysctl_drop_caches;
int drop_caches_sysctl_handler(struct ctl_table *, int,
					void __user *, size_t *, loff_t *);
#endif

void drop_slab(void);
void drop_slab_node(int nid);

#ifndef CONFIG_MMU
#define randomize_va_space 0
#else
extern int randomize_va_space;
#endif

const char * arch_vma_name(struct vm_area_struct *vma);
#ifdef CONFIG_MMU
void print_vma_addr(char *prefix, unsigned long rip);
#else
static inline void print_vma_addr(char *prefix, unsigned long rip)
{
}
#endif

void *sparse_buffer_alloc(unsigned long size);
struct page * __populate_section_memmap(unsigned long pfn,
		unsigned long nr_pages, int nid, struct vmem_altmap *altmap);
pgd_t *vmemmap_pgd_populate(unsigned long addr, int node);
p4d_t *vmemmap_p4d_populate(pgd_t *pgd, unsigned long addr, int node);
pud_t *vmemmap_pud_populate(p4d_t *p4d, unsigned long addr, int node);
pmd_t *vmemmap_pmd_populate(pud_t *pud, unsigned long addr, int node);
pte_t *vmemmap_pte_populate(pmd_t *pmd, unsigned long addr, int node);
void *vmemmap_alloc_block(unsigned long size, int node);
struct vmem_altmap;
void *vmemmap_alloc_block_buf(unsigned long size, int node);
void *altmap_alloc_block_buf(unsigned long size, struct vmem_altmap *altmap);
void vmemmap_verify(pte_t *, int, unsigned long, unsigned long);
int vmemmap_populate_basepages(unsigned long start, unsigned long end,
			       int node);
int vmemmap_populate(unsigned long start, unsigned long end, int node,
		struct vmem_altmap *altmap);
void vmemmap_populate_print_last(void);
#ifdef CONFIG_MEMORY_HOTPLUG
void vmemmap_free(unsigned long start, unsigned long end,
		struct vmem_altmap *altmap);
#endif
void register_page_bootmem_memmap(unsigned long section_nr, struct page *map,
				  unsigned long nr_pages);

enum mf_flags {
	MF_COUNT_INCREASED = 1 << 0,
	MF_ACTION_REQUIRED = 1 << 1,
	MF_MUST_KILL = 1 << 2,
	MF_SOFT_OFFLINE = 1 << 3,
};
extern int memory_failure(unsigned long pfn, int flags);
extern void memory_failure_queue(unsigned long pfn, int flags);
extern int unpoison_memory(unsigned long pfn);
extern int get_hwpoison_page(struct page *page);
#define put_hwpoison_page(page)	put_page(page)
extern int sysctl_memory_failure_early_kill;
extern int sysctl_memory_failure_recovery;
extern void shake_page(struct page *p, int access);
extern atomic_long_t num_poisoned_pages __read_mostly;
extern int soft_offline_page(struct page *page, int flags);


/*
 * Error handlers for various types of pages.
 */
enum mf_result {
	MF_IGNORED,	/* Error: cannot be handled */
	MF_FAILED,	/* Error: handling failed */
	MF_DELAYED,	/* Will be handled later */
	MF_RECOVERED,	/* Successfully recovered */
};

enum mf_action_page_type {
	MF_MSG_KERNEL,
	MF_MSG_KERNEL_HIGH_ORDER,
	MF_MSG_SLAB,
	MF_MSG_DIFFERENT_COMPOUND,
	MF_MSG_POISONED_HUGE,
	MF_MSG_HUGE,
	MF_MSG_FREE_HUGE,
	MF_MSG_NON_PMD_HUGE,
	MF_MSG_UNMAP_FAILED,
	MF_MSG_DIRTY_SWAPCACHE,
	MF_MSG_CLEAN_SWAPCACHE,
	MF_MSG_DIRTY_MLOCKED_LRU,
	MF_MSG_CLEAN_MLOCKED_LRU,
	MF_MSG_DIRTY_UNEVICTABLE_LRU,
	MF_MSG_CLEAN_UNEVICTABLE_LRU,
	MF_MSG_DIRTY_LRU,
	MF_MSG_CLEAN_LRU,
	MF_MSG_TRUNCATED_LRU,
	MF_MSG_BUDDY,
	MF_MSG_BUDDY_2ND,
	MF_MSG_DAX,
	MF_MSG_UNKNOWN,
};

#if defined(CONFIG_TRANSPARENT_HUGEPAGE) || defined(CONFIG_HUGETLBFS)
extern void clear_huge_page(struct page *page,
			    unsigned long addr_hint,
			    unsigned int pages_per_huge_page);
extern void copy_user_huge_page(struct page *dst, struct page *src,
				unsigned long addr_hint,
				struct vm_area_struct *vma,
				unsigned int pages_per_huge_page);
extern long copy_huge_page_from_user(struct page *dst_page,
				const void __user *usr_src,
				unsigned int pages_per_huge_page,
				bool allow_pagefault);
#endif /* CONFIG_TRANSPARENT_HUGEPAGE || CONFIG_HUGETLBFS */

#ifdef CONFIG_CONT_PTE_HUGEPAGE
static inline bool vma_is_special_huge(struct vm_area_struct *vma)
{
	return vma_is_dax(vma) || (vma->vm_file &&
  				   (vma->vm_flags & (VM_PFNMAP | VM_MIXEDMAP)));
}

/**
 * thp_nr_pages - The number of regular pages in this huge page.
 * @page: The head page of a huge page.
 */
static inline int thp_nr_pages(struct page *page)
{
        VM_BUG_ON_PGFLAGS(PageTail(page), page);
        if (PageHead(page))
                return 1 << page[1].compound_order;
        return 1;
}
#endif

#ifdef CONFIG_DEBUG_PAGEALLOC
extern unsigned int _debug_guardpage_minorder;
DECLARE_STATIC_KEY_FALSE(_debug_guardpage_enabled);

static inline unsigned int debug_guardpage_minorder(void)
{
	return _debug_guardpage_minorder;
}

static inline bool debug_guardpage_enabled(void)
{
	return static_branch_unlikely(&_debug_guardpage_enabled);
}

static inline bool page_is_guard(struct page *page)
{
	if (!debug_guardpage_enabled())
		return false;

	return PageGuard(page);
}
#else
static inline unsigned int debug_guardpage_minorder(void) { return 0; }
static inline bool debug_guardpage_enabled(void) { return false; }
static inline bool page_is_guard(struct page *page) { return false; }
#endif /* CONFIG_DEBUG_PAGEALLOC */

#if MAX_NUMNODES > 1
void __init setup_nr_node_ids(void);
#else
static inline void setup_nr_node_ids(void) {}
#endif

extern int memcmp_pages(struct page *page1, struct page *page2);

static inline int pages_identical(struct page *page1, struct page *page2)
{
	return !memcmp_pages(page1, page2);
}

extern int want_old_faultaround_pte;

#ifndef CONFIG_MULTIPLE_KSWAPD
static inline void update_kswapd_threads_node(int nid) {}
static inline int multi_kswapd_run(int nid) { return 0; }
static inline void multi_kswapd_stop(int nid) {}
static inline void multi_kswapd_cpu_online(pg_data_t *pgdat,
					const struct cpumask *mask) {}
#endif /* CONFIG_MULTIPLE_KSWAPD */

#ifdef CONFIG_OPLUS_UXMEM_OPT
extern bool is_critical_zeroslowpath_task(struct task_struct *tsk);
#endif

/**
 * seal_check_future_write - Check for F_SEAL_FUTURE_WRITE flag and handle it
 * @seals: the seals to check
 * @vma: the vma to operate on
 *
 * Check whether F_SEAL_FUTURE_WRITE is set; if so, do proper check/handling on
 * the vma flags.  Return 0 if check pass, or <0 for errors.
 */
static inline int seal_check_future_write(int seals, struct vm_area_struct *vma)
{
	if (seals & F_SEAL_FUTURE_WRITE) {
		/*
		 * New PROT_WRITE and MAP_SHARED mmaps are not allowed when
		 * "future write" seal active.
		 */
		if ((vma->vm_flags & VM_SHARED) && (vma->vm_flags & VM_WRITE))
			return -EPERM;

		/*
		 * Since an F_SEAL_FUTURE_WRITE sealed memfd can be mapped as
		 * MAP_SHARED and read-only, take care to not allow mprotect to
		 * revert protections on such mappings. Do this only for shared
		 * mappings. For private mappings, don't need to mask
		 * VM_MAYWRITE as we still want them to be COW-writable.
		 */
		if (vma->vm_flags & VM_SHARED)
			vma->vm_flags &= ~(VM_MAYWRITE);
	}

	return 0;
}

#ifdef CONFIG_CONT_PTE_HUGEPAGE
#define CONFIG_CONT_PTE_HUGEPAGE_DEBUG	1
#define CONFIG_CONT_PTE_FAULT_AROUND	1
#define EROFS_IOERROR_INJECTION		0
#define CONFIG_CONT_PTE_HUGEPAGE_ANON_DEBUG 1
#define CONFIG_POOL_ASYNC_RECLAIM 1
#define CONFIG_REUSE_SWP_ACCOUNT_DEBUG 1
#define CONFIG_MAPPED_WALK_MIDDLE_CONT_PTE_DEBUG 1
#define CONFIG_NON_SPF_FAULT_RETRY_DEBUG 1
#define CONFIG_CONT_PTE_FILE_HUGEPAGE_DISABLE 1
#define CONFIG_PRJ_FORCE_SUPPORT_CHP 0
#define CONFIG_PROCESS_RECLAIM_DEBUG 1
#define CONFIG_CHP_ABMORMAL_PTES_DEBUG 1
#define CONFIG_CHP_SPECIAL_PROCESS_BLACKLIST_ENABLE 0

#if CONFIG_POOL_ASYNC_RECLAIM
#define CONFIG_CONT_PTE_HUGEPAGE_LRU	1
#endif

#if CONFIG_POOL_ASYNC_RECLAIM
#define CONFIG_POOL_DIRECT_RECLAIM	0
#endif

#define HPAGE_CONT_PTE_SHIFT	CONT_PTE_SHIFT
#define HPAGE_CONT_PTE_ORDER	(CONT_PTE_SHIFT-PAGE_SHIFT)
#define HPAGE_CONT_PTE_SIZE	CONT_PTE_SIZE
#define HPAGE_CONT_PTE_MASK	CONT_PTE_MASK
#define HPAGE_CONT_PTE_NR	CONT_PTES

#define THP_SWAP_PRIO_MAGIC (0x1ead)

#define ALIGN_UP(x, align_to) (((x) + ((align_to)-1)) & ~((align_to)-1))

#define CLUSTER_FLAG_DOUBLE_MAP 8 /* This cluster is double-mapped */

/* look_around use +1, ksrhink_lruvecd uses +2/+3 */
#define PG_cont (__NR_PAGEFLAGS + 4)
#define PG_cont_uptodate (PG_cont + 1)
#define PG_cont_iodoing (PG_cont + 2)
#define PG_cont_ioredo_s (PG_cont + 3)
#define PG_cont_ioredo_e (PG_cont + 5)
#define PG_cont_fallback (PG_cont + 6)
#define PG_cont_refill (PG_cont + 7)
#define PG_cont_ext_alloc (PG_cont + 8)

#define PageCont(page) test_bit(PG_cont, &(page)->flags)
#define SetPageCont(page) set_bit(PG_cont, &(page)->flags)
#define ClearPageCont(page) clear_bit(PG_cont, &(page)->flags)
#define TestClearPageCont(page) test_and_clear_bit(PG_cont, &(page)->flags)

#define PageContUptodate(page) test_bit(PG_cont_uptodate, &(page)->flags)
#define SetPageContUptodate(page) set_bit(PG_cont_uptodate, &(page)->flags)
#define ClearPageContUptodate(page) clear_bit(PG_cont_uptodate, &(page)->flags)
#define TestClearPageContUptodate(page) test_and_clear_bit(PG_cont_uptodate, &(page)->flags)

#define PageContIODoing(page) test_bit(PG_cont_iodoing, &(page)->flags)
#define SetPageContIODoing(page) set_bit(PG_cont_iodoing, &(page)->flags)
#define ClearPageContIODoing(page) clear_bit(PG_cont_iodoing, &(page)->flags)
#define TestClearPageContIODoing(page) test_and_clear_bit(PG_cont_iodoing, &(page)->flags)
#define TestSetPageContIODoing(page) test_and_set_bit(PG_cont_iodoing, &(page)->flags)

#define PageContFallback(page) test_bit(PG_cont_fallback, &(page)->flags)
#define SetPageContFallback(page) set_bit(PG_cont_fallback, &(page)->flags)
#define ClearPageContFallback(page) clear_bit(PG_cont_fallback, &(page)->flags)
#define TestClearPageContFallback(page) test_and_clear_bit(PG_cont_fallback, &(page)->flags)

#define PageContRefill(page) test_bit(PG_cont_refill, &(page)->flags)
#define SetPageContRefill(page) set_bit(PG_cont_refill, &(page)->flags)
#define ClearPageContRefill(page) clear_bit(PG_cont_refill, &(page)->flags)
#define TestClearPageContRefill(page) test_and_clear_bit(PG_cont_refill, &(page)->flags)

#define PageContExtAlloc(page) test_bit(PG_cont_ext_alloc, &(page)->flags)
#define SetPageContExtAlloc(page) set_bit(PG_cont_ext_alloc, &(page)->flags)
#define ClearPageContExtAlloc(page) clear_bit(PG_cont_ext_alloc, &(page)->flags)
#define TestClearPageContExtAlloc(page) test_and_clear_bit(PG_cont_ext_alloc, &(page)->flags)


#define SetPageHead(page) set_bit(PG_head, &(page)->flags)

#define NORMAL_HUGE	1
#define JAR_HUGE	2

/* enum fault_flag, in case mainline is going to use 11-14 */
#define FAULT_FLAG_CONT_PTE	(1 << 15)

#define HIT_THP		0
#define HIT_CONT	1
#define HIT_BASEPAGE	2
#define HIT_NOTHING	3

#define THP_MAX_POOL_ALLOC_RETRIES (2)

#if CONFIG_POOL_ASYNC_RECLAIM
enum pool_watermarks {
	POOL_WMARK_MIN,
	POOL_WMARK_LOW,
	POOL_WMARK_HIGH,
	POOL_NR_WMARK
};

#define POOL_USER_ALLOC (1 << 31)
#define POOL_USER_ALLOC_MASK (1 << 31)
/* add new flags to enum pgdat_flags  for pool wakeup kswapd */
#define PGDAT_POOL_USER_ALLOC (PGDAT_RECLAIM_LOCKED + 1)

#define POOL_DIRECT_RECLAIM_NR 5
#define POOL_DIRECT_RECLAIM_PRIORITY 4
#define POOL_FILE_HUGEPAGES_LIMIT  (500 * SZ_1M / PAGE_SIZE)

/* FIXME: temp use for perf debug! */
#define POOL_KSWAPD_RECLAIM 0
#define POOL_DIRECT_RECLAIM 1
#define POOL_RECLAIM_ITEM	2
#define POOL_RECLAIM_SEQ_ITEM	20

#define POOL_DIRECT_RECLAIM_ENTER 0
#define POOL_DIRECT_RECLAIM_SUCCESS 1
#define POOL_DIRECT_RECLAIM_FAIL 2
#define POOL_DIRECT_RECLAIM_ITEM 3

#define POOL_OOM_ENTER 0
#define POOL_OOM_SUCCESS 1
#define POOL_OOM_FAIL 2
#define POOL_OOM_ITEM 3

extern wait_queue_head_t pool_direct_reclaim_wait[MAX_NUMNODES];
#endif

enum hpage_type {
	HPAGE_POOL_CMA,
	HPAGE_POOL_BUDDY,

	NR_HPAGE_POOL_TYPE,
};

#if CONFIG_CONT_PTE_HUGEPAGE_LRU
/*
 * Use the deferred_split field in memcg or node to hold a pointer to
 * lruvec (the split_queue_len in the deferred_split structure is reused
 * as a pointer to this new lruvec), When a cont-pte hugepage is added to
 * an lru, it is added to the lruvec to improve reclaim efficiency.
 *
 * Note:
 * 1.Some of the accounting related to lru is still in some of the
 * original data structures.
 * 2.The chp is short for "cont pte hugepage".
 */
struct chp_lruvec {
	struct lruvec lruvec;
	unsigned long lru_zone_size[MAX_NR_ZONES][NR_LRU_LISTS];
	struct deferred_split *ds; /* Point back to deferred_split */
	unsigned long flags;
};


#define CHP_SWAP_CLUSTER_MAX 256UL

#define LRUVEC_FOR_CHP  1
static inline bool is_chp_lruvec(struct lruvec *lruvec)
{
	struct chp_lruvec *chp_lruvec;
	chp_lruvec = container_of(lruvec, struct chp_lruvec, lruvec);
	return test_bit(LRUVEC_FOR_CHP, &chp_lruvec->flags);
}

#ifndef CONFIG_NEED_MULTIPLE_NODES
extern struct chp_lruvec contig_chp_lruvec;
#define NODE_CHP_LRUVEC(nid)    (&contig_chp_lruvec)
#endif /* CONFIG_NEED_MULTIPLE_NODES */

extern struct mem_cgroup_per_node *chp_lruvec_to_memcg_pn(struct lruvec *lruvec);
#endif

enum vm_chp_event_item {
	CHP_PAGE_ALLOC_SLOWPATH = 0,
	CHP_PAGE_ALLOC_FAILED,

	CHP_MAP_VIP,
	CHP_MADV_FREE_UNALIGNED,
	CHP_MADV_DONTNEED_UNALIGNED,

	CHP_REFILL_WORKER_WAKE_UP,
	CHP_REFILL_WORKER_ALLOC_SUCCESS,

	CHP_REFILL_EXTALLOC,
	CHP_ALLOC_ZSMALLOC,
	CHP_ALLOC_GPU,
	CHP_ALLOC_DMABUF,
	CHP_ALLOC_FROM_BUDDY_POOL,

	THP_DO_ANON_PAGES,
	THP_DO_ANON_PAGES_FALLBACK,

	THP_SWPIN_NO_SWAPCACHE_ENTRY,
	THP_SWPIN_NO_SWAPCACHE_ALLOC_SUCCESS,
	THP_SWPIN_NO_SWAPCACHE_ALLOC_FAIL,
	THP_SWPIN_NO_SWAPCACHE_FALLBACK_ENTRY,
	THP_SWPIN_NO_SWAPCACHE_FALLBACK_ALLOC_SUCCESS,
	THP_SWPIN_NO_SWAPCACHE_FALLBACK_ALLOC_FAIL,
	THP_SWPIN_SWAPCACHE_ENTRY,
	THP_SWPIN_SWAPCACHE_ALLOC_SUCCESS,
	THP_SWPIN_SWAPCACHE_PREPARE_FAIL,
	THP_SWPIN_SWAPCACHE_FALLBACK_ENTRY,
	THP_SWPIN_SWAPCACHE_FALLBACK_ALLOC_SUCCESS,
	THP_SWPIN_SWAPCACHE_FALLBACK_ALLOC_FAIL,

	THP_FILE_ENTRY,
	THP_FILE_ALLOC_SUCCESS,
	THP_FILE_ALLOC_FAIL,

	THP_SWPIN_CRITICAL_ENTRY,
	THP_SWPIN_CRITICAL_FALLBACK,

	NR_VM_CHP_EVENT_ITEMS
};

enum thp_read_swpcache_ret_status {
	RET_STATUS_ALLOC_THP_SUCCESS,
	RET_STATUS_NO_SWP_INFO,
	RET_STATUS_HIT_SWPCACHE,
	RET_STATUS_NO_CLUSTER_INFO,
	RET_STATUS_ZERO_SWPCOUNT,
	RET_STATUS_ALLOC_THP_FAIL,
	RET_STATUS_SWPCACHE_RPEPARE_FAIL,
	RET_STATUS_ADD_TO_SWPCACHE_FAIL,
	RET_STATUS_MEMCG_CHARGE_FAIL,
	RET_STATUS_OTHER_FAIL,
	RET_STATUS_NR,
};

enum WP_REUSE_FAIL_STAT {
	WP_REUSE_FAIL_TOTAL,
	PTE_NO_SAME,
	PTE_NO_READONLY,
	ZERO_REF_COUNT,
	WP_REUSE_FAIL_NR,
};

#if CONFIG_REUSE_SWP_ACCOUNT_DEBUG
enum REUSE_SWP_STAT {
	NORMAL_REUSE_SWP_WB,
	NORMAL_REUSE_SWP_NO_WB,
	NORMAL_REUSE_SWP_WB_ERR,
	CHP_REUSE_SWP_WB,
	CHP_REUSE_SWP_NO_WB,
	CHP_REUSE_SWP_WB_ERR,
	REUSE_SWP_NR,
};
#endif

#if CONFIG_MAPPED_WALK_MIDDLE_CONT_PTE_DEBUG
#define MAPPED_WALK_HIT_SEQ 10
struct mapped_walk_middle_cont_pte_stat {
	unsigned long ori_addr, addr;
	pteval_t pte[HPAGE_CONT_PTE_NR];
	struct page *page;
	unsigned long page_pfn;
	unsigned long pte_pfn;
};
#endif

#if CONFIG_NON_SPF_FAULT_RETRY_DEBUG
enum NON_SFP_FAULT_RETRY_STAT {
	SWPIN_CHP_FAULT_RETRY,
	SWPIN_FALLBACK_FAULT_RETRY,
	FAULT_RETRY_NR,
};
#endif

#if CONFIG_CHP_ABMORMAL_PTES_DEBUG
#define CHP_ABMORMAL_PTES_SEQ 100
#define CORRUPT_CONT_PTE_REASON_MASK			0x00000000ffffffff
#define DOUBLE_MAP_REASON_MASK 				0xffffffff00000000
/* bit:0-31 -> corrupt cont pte */
#define CORRUPT_CONT_PTE_REASON_MADVISE_PAGE_OUT	0x0000000000000001
#define CORRUPT_CONT_PTE_REASON_MADVISE_FREE1		0x0000000000000002
#define CORRUPT_CONT_PTE_REASON_MADVISE_FREE2		0x0000000000000004
#define CORRUPT_CONT_PTE_REASON_CP_PTE_RANGE1		0x0000000000000008
#define CORRUPT_CONT_PTE_REASON_CP_PTE_RANGE2		0x0000000000000010
#define CORRUPT_CONT_PTE_REASON_ZAP_PTE_RANGE		0x0000000000000020
#define CORRUPT_CONT_PTE_REASON_WP_PAGE_CP1		0x0000000000000040
#define CORRUPT_CONT_PTE_REASON_WP_PAGE_CP2		0x0000000000000080
#define CORRUPT_CONT_PTE_REASON_DO_WP_PAGE		0x0000000000000100
#define CORRUPT_CONT_PTE_REASON_PTE_FAULT		0x0000000000000200
#define CORRUPT_CONT_PTE_REASON_CH_PTE_RANGE		0x0000000000000400
#define CORRUPT_CONT_PTE_REASON_PAGE_REFS_ONE		0x0000000000000800
#define CORRUPT_CONT_PTE_REASON_SPLIT_CONT_PTE		0x0000000000001000
#define CORRUPT_CONT_PTE_REASON_PTE_READONLY		0x0000000000002000
#define CORRUPT_CONT_PTE_REASON_PTE_PRERM		0x0000000000004000
/* bit:32-63 -> double map */
#define DOUBLE_MAP_REASON_MADVISE			0x0000000100000000
#define DOUBLE_MAP_REASON_DO_SWAP_PAGE1			0x0000000200000000
#define DOUBLE_MAP_REASON_DO_SWAP_PAGE2			0x0000000400000000
#define DOUBLE_MAP_REASON_SPLIT_VMA			0x0000000800000000
#define DOUBLE_MAP_REASON_MOVE_PTES			0x0000001000000000

struct chp_abnormal_ptes_stat {
	uid_t uid;
	pid_t pid;
	char comm[TASK_COMM_LEN];
	unsigned long reason;
};
#endif /* CONFIG_CHP_ABMORMAL_PTES_DEBUG */

struct cont_pte_huge_page_stat {
	/* reserved last index for alloc fail */
	atomic64_t usage[NR_HPAGE_POOL_TYPE];
	atomic64_t thp_read_swpcache_ret_status_stat[RET_STATUS_NR];
#if CONFIG_POOL_ASYNC_RECLAIM
	atomic64_t kswapd_wakeup_count;
	atomic64_t wmark_count[2];
	atomic64_t direct_reclaim_stat[POOL_DIRECT_RECLAIM_ITEM];
	atomic64_t oom_stat[POOL_OOM_ITEM];
	atomic64_t reclaim_seq[POOL_RECLAIM_ITEM];
	atomic64_t reclaim_count[POOL_RECLAIM_ITEM][POOL_RECLAIM_SEQ_ITEM];
	s64 reclaim_time[POOL_RECLAIM_ITEM][POOL_RECLAIM_SEQ_ITEM];
#endif
	s64 chunk_refill_time;
	atomic64_t chunk_refill_fail_count;
	s64 chunk_refill_first_fail_num;
	atomic64_t cma_steal_count;
	atomic64_t wp_reuse_fail_count[WP_REUSE_FAIL_NR];
#if CONFIG_REUSE_SWP_ACCOUNT_DEBUG
	/* for reuse_swap_page */
	atomic64_t reuse_swp_count[REUSE_SWP_NR];
#endif
	atomic64_t truncate_hit_middle_page_cnt;
	atomic64_t cp_cont_pte_split_count;
#if CONFIG_MAPPED_WALK_MIDDLE_CONT_PTE_DEBUG
	atomic64_t mapped_walk_middle_cont_pte_cnt;
	atomic64_t mapped_walk_start_from_non_head;
	atomic64_t mapped_walk_lastmoment_doublemap;
	struct mapped_walk_middle_cont_pte_stat mapped_walk_stat[MAPPED_WALK_HIT_SEQ];
#endif
#if CONFIG_NON_SPF_FAULT_RETRY_DEBUG
	atomic64_t non_sfp_fault_retry_cnt[FAULT_RETRY_NR];
#endif
#if CONFIG_PROCESS_RECLAIM_DEBUG
	atomic64_t process_reclaim_double_map_cnt;
#endif
#if CONFIG_CHP_ABMORMAL_PTES_DEBUG
		atomic64_t chp_abnormal_ptes_uid_cnt;
		struct chp_abnormal_ptes_stat abps[CHP_ABMORMAL_PTES_SEQ];
#endif
};

extern struct cont_pte_huge_page_stat perf_stat;

struct huge_page_pool {
#if CONFIG_POOL_ASYNC_RECLAIM
	unsigned long wmark[POOL_NR_WMARK];
#endif
	int count[NR_HPAGE_POOL_TYPE];
	int low, high;
	int min_buddy;
	int cma_count;
	unsigned long flags;
	struct list_head items[NR_HPAGE_POOL_TYPE];
	spinlock_t spinlock;
	struct task_struct *refill_worker;
};

extern atomic_long_t cont_pte_double_map_count;
extern atomic64_t thp_swpin_hit_swapcache;
extern atomic64_t thp_cow;
extern atomic64_t thp_cow_fallback;

static inline bool transhuge_cont_pte_addr_suitable(struct vm_area_struct *vma,
						    unsigned long haddr)
{
	return ((haddr >= vma->vm_start) && (haddr + HPAGE_CONT_PTE_SIZE <= vma->vm_end));
}

static inline bool transhuge_cont_pte_vma_aligned(struct vm_area_struct *vma)
{
	return IS_ALIGNED((vma->vm_start >> PAGE_SHIFT) - vma->vm_pgoff, HPAGE_CONT_PTE_NR);

}

extern inline bool transhuge_cont_pte_vma_suitable(struct vm_area_struct *vma,
						   unsigned long haddr);

extern bool reuse_swap_cont_pte_page(struct page *page, int *total_map_swapcount);

#define cont_ptep_clear_flush_young_notify(__vma, __address, __ptep)	\
({									\
	int __young = 0;						\
	unsigned long i;						\
	struct vm_area_struct *___vma = __vma;				\
	unsigned long ___address = __address & HPAGE_CONT_PTE_MASK;	\
	for (i = 0; i < HPAGE_CONT_PTE_NR; i++)				\
		__young |= ptep_test_and_clear_young(___vma,		\
						    ___address +	\
						    i * PAGE_SIZE,	\
						    __ptep + i);	\
	__young |= mmu_notifier_clear_flush_young(___vma->vm_mm,	\
						___address,		\
						___address +		\
						HPAGE_CONT_PTE_SIZE);	\
	__young;							\
})

#define cont_ptep_clear_flush_young_full(__vma, __address, __ptep)	\
({									\
	int __young = 0;						\
	unsigned long i;						\
	struct vm_area_struct *___vma = __vma;				\
	unsigned long ___address = __address & HPAGE_CONT_PTE_MASK;	\
	for (i = 0; i < HPAGE_CONT_PTE_NR; i++)				\
		__young |= ptep_test_and_clear_young(___vma,		\
						    ___address +	\
						    i * PAGE_SIZE,	\
						    __ptep + i);	\
	if (__young)							\
		flush_tlb_range(___vma, ___address,			\
			___address + HPAGE_CONT_PTE_SIZE);		\
})

static inline bool cont_pte_none(pte_t *ptep)
{
	int i;

	for (i = 0; i < HPAGE_CONT_PTE_NR; i++) {
		if (!pte_none(*(ptep + i)))
			return false;
	}

	return true;
}

static inline bool ContPteHugePageHead(struct page *page)
{
	if (PageTail(page))
		return false;

	return PageHead(page)
	    && compound_order(page) == HPAGE_CONT_PTE_ORDER
	    && (is_transparent_hugepage(page) || PageContExtAlloc(page));

}

extern inline bool within_cont_pte_cma(unsigned long pfn);

static inline bool ContPteCMAHugePageHead(struct page *page)
{
	if (!ContPteHugePageHead(page))
		return false;

	return within_cont_pte_cma(page_to_pfn(page));
}

/**
 * cont_pte_nr_pages - The number of regular pages in this cont-pte hugepage.
 * @page: The head page of a cont-pte hugepage.
 *  NOTE: use injudiciously!
 */
static inline int cont_pte_nr_pages(struct page *page)
{
	VM_BUG_ON_PGFLAGS(PageTail(page), page);
	if (PageHead(page)) {
		return compound_nr(page);
	} else if (PageCont(page)) {
		CHP_BUG_ON(!IS_ALIGNED(page_to_pfn(page), HPAGE_CONT_PTE_NR));
		return HPAGE_CONT_PTE_NR;
	}

	return 1;
}

static inline bool ContPteHugePage(struct page *page)
{
	return PageTransCompound(page)
	    && compound_order(compound_head(page)) == HPAGE_CONT_PTE_ORDER
	    && is_transparent_hugepage(page);
}

/*
 * huge file pages for .so, .oat. .odex in erofs are mapped massively, they are likely
 * to be hot
 */
static inline bool ContPteHugePageSkipMassiveMapped(struct page *page)
{
	if (page && PageCont(page) && PageHead(page) && atomic_read(&page->_mapcount) > 20) {
		/*
		 * try to slow down the rmap for massively mapped hugepages, but we
		 * still scan them so that they can be reclaimed if they are really
		 * cold. skip 3/4 rmap.
		 */
		static int scan_pos;

		return (scan_pos++ % 4) != 0;
	}

	return false;
}

static inline bool ContPteHugePageDoubleMap(struct page *page)
{
	int i;
	struct page *head = compound_head(page);

	if (PageAnon(head)) {
		for (i = 0; i < HPAGE_CONT_PTE_NR; i++) {
			if (atomic_read(&head[i]._mapcount) >= 0)
				return true;
		}
	} else {

		if (!PageDoubleMap(head))
			return false;

		for (i = 0; i < HPAGE_CONT_PTE_NR; i++) {
			if (atomic_read(&head[i]._mapcount) !=
					atomic_read(compound_mapcount_ptr(head)))
				return true;
		}

		if (TestClearPageDoubleMap(page))
			atomic_long_dec(&cont_pte_double_map_count);
	}

	return false;
}

extern struct cma *cont_pte_cma;
extern unsigned long swap_cluster_double_mapped;

extern inline bool is_cont_pte_cma(struct cma *cma);

#if CONFIG_CHP_ABMORMAL_PTES_DEBUG
extern inline bool commit_chp_abnormal_ptes_record(unsigned long reason);
extern inline bool cont_pte_trans_huge(pte_t *ptep, unsigned long reason);
#endif

static inline int vmf_may_cont_pte(struct vm_fault *vmf)
{
	struct file *file = vmf->vma->vm_file;
	struct address_space *mapping = file ? file->f_mapping : NULL;
	struct inode *inode = mapping ? mapping->host : NULL;

	return inode ? inode->may_cont_pte : false;
}

static inline bool cont_pte_readonly(struct vm_fault *vmf)
{
	int i, nr = 0;
	unsigned long haddr = vmf->address & HPAGE_CONT_PTE_MASK;
	pte_t *ptep = vmf->pte - (vmf->address - haddr)/PAGE_SIZE;

	/* someone else has changed ptes to non-cont */
	if (!pte_cont(*ptep))
		return false;

	for (i = 0; i < HPAGE_CONT_PTE_NR; i++, ptep++) {
		if (pte_write(*ptep))
			nr++;

#if CONFIG_CHP_ABMORMAL_PTES_DEBUG
		if (!pte_cont(*ptep)) {
			cont_pte_trans_huge(ptep, CORRUPT_CONT_PTE_REASON_PTE_READONLY);
		}
#endif
	}

	if (nr != HPAGE_CONT_PTE_NR && nr != 0) {
		printk(KERN_ERR "@@@Fixme- partially writable in hugepage: %s current:%s-%d i:%d fault addr:%lx vma start:%lx end:%lx\n",
				__func__, current->comm, current->pid, i, vmf->address, vmf->vma->vm_start, vmf->vma->vm_end);
#if CONFIG_CHP_ABMORMAL_PTES_DEBUG
		commit_chp_abnormal_ptes_record(CORRUPT_CONT_PTE_REASON_PTE_PRERM);
#endif
		CHP_BUG_ON(1);
	}

	return !nr;
}

static inline bool vma_is_chp_anonymous(struct vm_area_struct *vma)
{
	return vma->android_kabi_reserved2 == THP_SWAP_PRIO_MAGIC;
}

#define cont_pte_pagefault_dump(vmf, reason) do { \
	struct vm_area_struct *vma = vmf->vma; \
	const char *name = vma->vm_file->f_path.dentry ? (const char *)vma->vm_file->f_path.dentry->d_name.name : "NULL"; \
															\
	pr_debug("%s %s %d: filename:%s inode:%ld process:%s aligned:%d index:%lx-%lx vm_pgoff:%lx vma:%lx-%lx r:%d w:%d x:%d mw:%d flags:%lx\n", \
			reason, __func__, __LINE__, name, vma->vm_file->f_inode->i_ino,  current->comm, transhuge_cont_pte_vma_aligned(vma), \
			vmf->page ? vmf->page->index : -1UL, vmf->pgoff, vma->vm_pgoff, (unsigned long)vma->vm_start, (unsigned long)vma->vm_end, \
			!!(vma->vm_flags & VM_READ), !!(vma->vm_flags & VM_WRITE), !!(vma->vm_flags & VM_EXEC), \
			!!(vma->vm_flags & VM_MAYWRITE), vma->vm_flags);\
	} while (0)

extern unsigned long cont_pte_pool_cma_size;
extern bool cma_chunk_refill_ready;
extern bool supported_oat_hugepage;

extern struct huge_page_pool g_cont_pte_pool;

extern inline bool cont_pte_huge_page_enabled(void);

extern pte_t cont_pte_huge_ptep_get_and_clear(struct mm_struct *mm,
					      unsigned long addr, pte_t *ptep);

extern pte_t cont_pte_huge_ptep_get_and_clear_flush(struct mm_struct *mm,
					      unsigned long addr, pte_t *ptep);

extern void __split_huge_cont_pte(struct vm_area_struct *vma, pte_t *pte,
				  unsigned long address, bool freeze,
				  struct page *page, spinlock_t *ptl);

extern void change_huge_cont_pte(struct vm_area_struct *vma, pte_t *pte,
				 unsigned long addr, pgprot_t newprot,
				 unsigned long cp_flags);

extern void cont_pte_set_huge_pte_wrprotect(struct mm_struct *mm, unsigned long addr,
			      pte_t *ptep);

extern void cont_pte_set_huge_pte_clean(struct mm_struct *mm, unsigned long addr,
			      pte_t *ptep);

extern bool set_cont_pte_huge_zero_page(struct mm_struct *mm,
		struct vm_area_struct *vma, unsigned long faddr, pte_t *pte,
		struct page *zero_page);

extern void split_huge_cont_pte_address(struct vm_area_struct *vma,
					unsigned long address, bool freeze,
					struct page *page);

extern void cont_pte_set_huge_pte_at(struct mm_struct *mm, unsigned long addr,
			      pte_t *ptep, pte_t pte);

extern int find_get_cont_pte_pages(struct address_space *mapping, pgoff_t start,
				   struct page **ret_page);

extern struct page *find_get_entry_may_cont_pte(struct address_space *mapping,
		pgoff_t index);

extern struct file *do_cont_pte_sync_mmap_readahead(struct vm_fault *vmf);

extern struct file *do_cont_pte_async_mmap_readahead(struct vm_fault *vmf, struct page *page);

extern void do_set_cont_pte(struct vm_fault *vmf, struct page *page);

extern void do_set_cont_pte_with_addr(struct vm_fault *vmf,
				struct page *page,
				unsigned long addr);

extern void set_cont_pte_uptodate_and_unlock(struct page *page);

extern void init_cont_endio_spinlock(struct inode *inode);

extern int huge_page_pool_count(struct huge_page_pool *pool, int inx);
extern int cont_pte_pool_total_pages(void);
extern int cont_pte_pool_high(void);
extern struct huge_page_pool *cont_pte_pool(void);
extern bool cont_pte_pool_add(struct page *page);

extern struct page *alloc_cont_pte_hugepage(gfp_t gfp_mask);
extern struct page *alloc_chp_ext(gfp_t gfp_mask, int *order);

extern inline void count_vm_chp_events(enum vm_chp_event_item item, long delta);
extern inline void count_vm_chp_event(enum vm_chp_event_item item);
extern inline void mod_chp_page_state(struct page *page, long delta);

extern void __free_cont_pte_hugepages(struct page *page);

vm_fault_t cont_pte_filemap_around(struct vm_fault *vmf,
				   pgoff_t start_pgoff,
				   pgoff_t end_pgoff,
				   unsigned long fault_addr);

extern inline bool is_thp_swap(struct swap_info_struct *si);

extern bool is_critical_native(struct task_struct *tsk);

#ifdef CONFIG_HYBRIDSWAP
extern bool free_zram_is_ok(void);
#else
static inline free_zram_is_ok(void) { return false; }
#endif

extern inline bool current_is_hybridswapd(void);
extern void chp_uid_blacklist_update(void);
extern unsigned long read_zram_used_pages(int inx);
extern bool handle_chp_prctl_user_addrs(const char __user *name,
					unsigned long start, unsigned long len);
extern void handle_chp_load_elf_binary(const char *filename);
extern inline void handle_chp_get_unmapped_area(struct vm_unmapped_area_info *info,
						struct file *filp,
						unsigned long pgoff);
extern inline bool handle_chp_ext_cmd(struct sysinfo *si);
extern inline bool handle_chp_fs_supported(struct inode *inode);
#endif /* CONFIG_CONT_PTE_HUGEPAGE */

#endif /* __KERNEL__ */
#endif /* _LINUX_MM_H */
