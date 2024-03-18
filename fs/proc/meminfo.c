// SPDX-License-Identifier: GPL-2.0
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/hugetlb.h>
#include <linux/mman.h>
#include <linux/mmzone.h>
#include <linux/memory.h>
#include <linux/proc_fs.h>
#include <linux/percpu.h>
#include <linux/seq_file.h>
#include <linux/swap.h>
#include <linux/vmstat.h>
#include <linux/atomic.h>
#include <linux/vmalloc.h>
#ifdef CONFIG_CMA
#include <linux/cma.h>
#endif
#include <asm/page.h>
#include <asm/pgtable.h>
#include "internal.h"
#ifdef CONFIG_QCOM_MINIDUMP_PANIC_DUMP
#include <soc/qcom/minidump.h>
#include <linux/seq_buf.h>
#endif

//#ifdef OPLUS_FEATURE_HEALTHINFO
#ifdef CONFIG_OPLUS_HEALTHINFO
#include <linux/healthinfo/ion.h>
#endif
//#endif /* OPLUS_FEATURE_HEALTHINFO */

#ifdef OPLUS_FEATURE_HEALTHINFO
#ifdef CONFIG_OPLUS_HEALTHINFO
extern unsigned long gpu_total(void);
#endif
#endif /* OPLUS_FEATURE_HEALTHINFO */
#ifdef CONFIG_HYBRIDSWAP
#include <trace/hooks/vh_vmscan.h>
#endif

void __attribute__((weak)) arch_report_meminfo(struct seq_file *m)
{
}

static void show_val_kb2(struct seq_file *m, const char *s, unsigned long num)
{
	if (m) {
		seq_printf(m, s, num);
	} else {
#ifdef CONFIG_QCOM_MINIDUMP_PANIC_DUMP
		if (md_meminfo_seq_buf)
			seq_buf_printf(md_meminfo_seq_buf, s, num);
#endif
	}
}

static void show_val_kb(struct seq_file *m, const char *s, unsigned long num)
{
	if (m) {
		seq_put_decimal_ull_width(m, s, num << (PAGE_SHIFT - 10), 8);
		seq_write(m, " kB\n", 4);
	} else {
#ifdef CONFIG_QCOM_MINIDUMP_PANIC_DUMP
		if (md_meminfo_seq_buf)
			seq_buf_printf(md_meminfo_seq_buf, "%s : %lld KB\n", s,
					num << (PAGE_SHIFT - 10));
#endif
	}
}

static int meminfo_proc_show(struct seq_file *m, void *v)
{
	struct sysinfo i;
	unsigned long committed;
	long cached;
	long available;
	unsigned long pages[NR_LRU_LISTS];
	unsigned long sreclaimable, sunreclaim;
	int lru;
#ifdef CONFIG_CONT_PTE_HUGEPAGE
	int chp_pool_pages;
#endif
	si_meminfo(&i);
	si_swapinfo(&i);
	committed = percpu_counter_read_positive(&vm_committed_as);

	cached = global_node_page_state(NR_FILE_PAGES) -
			total_swapcache_pages() - i.bufferram;
	if (cached < 0)
		cached = 0;

	for (lru = LRU_BASE; lru < NR_LRU_LISTS; lru++)
		pages[lru] = global_node_page_state(NR_LRU_BASE + lru);

	available = si_mem_available();

#ifdef CONFIG_QCOM_MEM_OFFLINE
	i.totalram = get_totalram_pages_count_inc_offlined();
#endif

	sreclaimable = global_node_page_state(NR_SLAB_RECLAIMABLE);
#ifdef CONFIG_CONT_PTE_HUGEPAGE
	chp_pool_pages = cont_pte_pool_total_pages();
	chp_pool_pages -= min(chp_pool_pages / 2, cont_pte_pool_high());
	sreclaimable += chp_pool_pages;
#endif
	sunreclaim = global_node_page_state(NR_SLAB_UNRECLAIMABLE);

	show_val_kb(m, "MemTotal:       ", i.totalram);
	show_val_kb(m, "MemFree:        ", i.freeram);
	show_val_kb(m, "MemAvailable:   ", available);
	show_val_kb(m, "Buffers:        ", i.bufferram);
	show_val_kb(m, "Cached:         ", cached);
	show_val_kb(m, "SwapCached:     ", total_swapcache_pages());
	show_val_kb(m, "Active:         ", pages[LRU_ACTIVE_ANON] +
					   pages[LRU_ACTIVE_FILE]);
	show_val_kb(m, "Inactive:       ", pages[LRU_INACTIVE_ANON] +
					   pages[LRU_INACTIVE_FILE]);
	show_val_kb(m, "Active(anon):   ", pages[LRU_ACTIVE_ANON]);
	show_val_kb(m, "Inactive(anon): ", pages[LRU_INACTIVE_ANON]);
	show_val_kb(m, "Active(file):   ", pages[LRU_ACTIVE_FILE]);
	show_val_kb(m, "Inactive(file): ", pages[LRU_INACTIVE_FILE]);
	show_val_kb(m, "Unevictable:    ", pages[LRU_UNEVICTABLE]);
	show_val_kb(m, "Mlocked:        ", global_zone_page_state(NR_MLOCK));

#ifdef CONFIG_HIGHMEM
	show_val_kb(m, "HighTotal:      ", i.totalhigh);
	show_val_kb(m, "HighFree:       ", i.freehigh);
	show_val_kb(m, "LowTotal:       ", i.totalram - i.totalhigh);
	show_val_kb(m, "LowFree:        ", i.freeram - i.freehigh);
#endif

#ifndef CONFIG_MMU
	show_val_kb(m, "MmapCopy:       ",
		    (unsigned long)atomic_long_read(&mmap_pages_allocated));
#endif

	show_val_kb(m, "SwapTotal:      ", i.totalswap);
	show_val_kb(m, "SwapFree:       ", i.freeswap);
	show_val_kb(m, "Dirty:          ",
		    global_node_page_state(NR_FILE_DIRTY));
	show_val_kb(m, "Writeback:      ",
		    global_node_page_state(NR_WRITEBACK));
	show_val_kb(m, "AnonPages:      ",
		    global_node_page_state(NR_ANON_MAPPED));
	show_val_kb(m, "Mapped:         ",
		    global_node_page_state(NR_FILE_MAPPED));
	show_val_kb(m, "Shmem:          ", i.sharedram);
	show_val_kb(m, "KReclaimable:   ", sreclaimable +
		    global_node_page_state(NR_KERNEL_MISC_RECLAIMABLE));
	show_val_kb(m, "Slab:           ", sreclaimable + sunreclaim);
	show_val_kb(m, "SReclaimable:   ", sreclaimable);
	show_val_kb(m, "SUnreclaim:     ", sunreclaim);
	show_val_kb2(m, "KernelStack:    %8lu kB\n",
		   global_zone_page_state(NR_KERNEL_STACK_KB));
#ifdef CONFIG_SHADOW_CALL_STACK
	show_val_kb2(m, "ShadowCallStack:%8lu kB\n",
		   global_zone_page_state(NR_KERNEL_SCS_BYTES) / 1024);
#endif
	show_val_kb(m, "PageTables:     ",
		    global_zone_page_state(NR_PAGETABLE));

	show_val_kb(m, "NFS_Unstable:   ",
		    global_node_page_state(NR_UNSTABLE_NFS));
	show_val_kb(m, "Bounce:         ",
		    global_zone_page_state(NR_BOUNCE));
	show_val_kb(m, "WritebackTmp:   ",
		    global_node_page_state(NR_WRITEBACK_TEMP));
	show_val_kb(m, "CommitLimit:    ", vm_commit_limit());
	show_val_kb(m, "Committed_AS:   ", committed);
	show_val_kb2(m, "VmallocTotal:   %8lu kB\n",
		   (unsigned long)VMALLOC_TOTAL >> 10);
	show_val_kb(m, "VmallocUsed:    ", vmalloc_nr_pages());
	show_val_kb(m, "VmallocChunk:   ", 0ul);
	show_val_kb(m, "Percpu:         ", pcpu_nr_pages());

#ifdef CONFIG_MEMORY_FAILURE
	show_val_kb2(m, "HardwareCorrupted: %5lu kB\n",
		   atomic_long_read(&num_poisoned_pages) << (PAGE_SHIFT - 10));
#endif

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
#ifndef CONFIG_CONT_PTE_HUGEPAGE
	show_val_kb(m, "AnonHugePages:  ",
		    global_node_page_state(NR_ANON_THPS) * HPAGE_PMD_NR);
#else
	show_val_kb(m, "AnonHugePages:  ",
		    global_node_page_state(NR_ANON_THPS) * HPAGE_CONT_PTE_NR);
#endif
	show_val_kb(m, "ShmemHugePages: ",
		    global_node_page_state(NR_SHMEM_THPS) * HPAGE_PMD_NR);
	show_val_kb(m, "ShmemPmdMapped: ",
		    global_node_page_state(NR_SHMEM_PMDMAPPED) * HPAGE_PMD_NR);
#ifndef CONFIG_CONT_PTE_HUGEPAGE
	show_val_kb(m, "FileHugePages:  ",
		    global_node_page_state(NR_FILE_THPS) * HPAGE_PMD_NR);
	show_val_kb(m, "FilePmdMapped:  ",
		    global_node_page_state(NR_FILE_PMDMAPPED) * HPAGE_PMD_NR);
#else
	show_val_kb(m, "FileHugePages:  ",
			global_node_page_state(NR_FILE_THPS) * HPAGE_CONT_PTE_NR);
	show_val_kb(m, "FilePmdMapped:  ",
			global_node_page_state(NR_FILE_PMDMAPPED) * HPAGE_CONT_PTE_NR);
	show_val_kb(m, "HugePagePool:   ", cont_pte_pool_total_pages());
	show_val_kb(m, "DoubleMapTHP:   ",
			 atomic_long_read(&cont_pte_double_map_count) * HPAGE_CONT_PTE_NR);
#endif
#endif

#ifdef CONFIG_CMA
	show_val_kb(m, "CmaTotal:       ", totalcma_pages);
	show_val_kb(m, "CmaFree:        ",
		    global_zone_page_state(NR_FREE_CMA_PAGES));
#endif

	if (m) {
		hugetlb_report_meminfo(m);
		arch_report_meminfo(m);
	}
#ifdef OPLUS_FEATURE_HEALTHINFO
#if defined CONFIG_ION && defined CONFIG_OPLUS_HEALTHINFO
	show_val_kb(m, "IonTotalCache:   ", global_zone_page_state(NR_IONCACHE_PAGES));
	show_val_kb(m, "IonTotalUsed:   ", ion_total() >> PAGE_SHIFT);
#ifdef CONFIG_OPLUS_ION_BOOSTPOOL
	show_val_kb(m, "RsvPool:        ", atomic64_read(&boost_pool_pages));
#endif /* CONFIG_OPLUS_ION_BOOSTPOOL */
#endif
#endif /* OPLUS_FEATURE_HEALTHINFO */
#ifdef OPLUS_FEATURE_HEALTHINFO
#ifdef CONFIG_OPLUS_HEALTHINFO
	show_val_kb(m, "GPUTotalUsed:   ", gpu_total() >> PAGE_SHIFT);
#endif
#endif /* OPLUS_FEATURE_HEALTHINFO */
#ifdef CONFIG_HYBRIDSWAP
	trace_android_vh_meminfo_proc_show(m);
#endif
#ifdef CONFIG_OPLUS_SENSITIVE_MEM_ALLOC_OPT
	show_val_kb(m, "OMemFree:    ", (unsigned long)oplus_sensitive_mem_pages(3, 0));
#endif
	return 0;
}

#ifdef CONFIG_QCOM_MINIDUMP_PANIC_DUMP
void md_dump_meminfo(void)
{
	meminfo_proc_show(NULL, NULL);
}
#endif

static int __init proc_meminfo_init(void)
{
	proc_create_single("meminfo", 0, NULL, meminfo_proc_show);
	return 0;
}
fs_initcall(proc_meminfo_init);
