/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2011-2014, 2017-2020, The Linux Foundation. All rights reserved.
 */

#ifndef _RAMDUMP_HEADER
#define _RAMDUMP_HEADER
#include <linux/kernel.h>
#include <linux/firmware.h>
#include <linux/devcoredump.h>
#include <linux/soc/qcom/mdt_loader.h>

struct device;

struct ramdump_segment {
	char *name;
	unsigned long address;
	volatile void __iomem *v_address;
	unsigned long size;
};

#if defined(OPLUS_FEATURE_MODEM_MINIDUMP) && defined(CONFIG_OPLUS_FEATURE_MODEM_MINIDUMP)
//Add for customized subsystem ramdump to skip generate dump cause by SAU
extern bool SKIP_GENERATE_RAMDUMP;
#endif

struct qcom_dump_segment {
	struct list_head node;
	dma_addr_t da;
	void *va;
	size_t size;
};

extern int do_elf_dump(struct list_head *segs, struct device *dev);
extern int do_dump(struct list_head *head, struct device *dev);
extern int do_fw_elf_dump(struct firmware *fw, struct device *dev);

#if IS_ENABLED(CONFIG_MSM_SUBSYSTEM_RESTART)
extern void *create_ramdump_device(const char *dev_name, struct device *parent);
extern void destroy_ramdump_device(void *dev);
extern int do_ramdump(void *handle, struct ramdump_segment *segments,
		int nsegments);
extern int do_elf_ramdump(void *handle, struct ramdump_segment *segments,
		int nsegments);
extern int do_minidump(void *handle, struct ramdump_segment *segments,
		       int nsegments);
extern int do_minidump_elf32(void *handle, struct ramdump_segment *segments,
			     int nsegments);

#else
static inline void *create_ramdump_device(const char *dev_name,
		struct device *parent)
{
	return NULL;
}

static inline void destroy_ramdump_device(void *dev)
{
}

static inline int do_ramdump(void *handle, struct ramdump_segment *segments,
		int nsegments)
{
	return -ENODEV;
}

static inline int do_elf_ramdump(void *handle, struct ramdump_segment *segments,
		int nsegments)
{
	return -ENODEV;
}

static inline int do_minidump(void *handle, struct ramdump_segment *segments,
		       int nsegments)
{
	return -ENODEV;
}

static inline int do_minidump_elf32(void *handle,
			struct ramdump_segment *segments, int nsegments)
{
	return -ENODEV;
}

#endif /* CONFIG_MSM_SUBSYSTEM_RESTART */

#endif
