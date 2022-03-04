/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2010 Marco Stornelli <marco.stornelli@gmail.com>
 * Copyright (C) 2011 Kees Cook <keescook@chromium.org>
 * Copyright (C) 2011 Google, Inc.
 */

#ifndef __LINUX_PSTORE_RAM_H__
#define __LINUX_PSTORE_RAM_H__

#include <linux/compiler.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/pstore.h>
#include <linux/types.h>

/*
 * Choose whether access to the RAM zone requires locking or not.  If a zone
 * can be written to from different CPUs like with ftrace for example, then
 * PRZ_FLAG_NO_LOCK is used. For all other cases, locking is required.
 */
#define PRZ_FLAG_NO_LOCK	BIT(0)
/*
 * If a PRZ should only have a single-boot lifetime, this marks it as
 * getting wiped after its contents get copied out after boot.
 */
#define PRZ_FLAG_ZAP_OLD	BIT(1)

struct persistent_ram_buffer;
struct rs_control;

struct persistent_ram_ecc_info {
	int block_size;
	int ecc_size;
	int symsize;
	int poly;
	uint16_t *par;
};

/**
 * struct persistent_ram_zone - Details of a persistent RAM zone (PRZ)
 *                              used as a pstore backend
 *
 * @paddr:	physical address of the mapped RAM area
 * @size:	size of mapping
 * @label:	unique name of this PRZ
 * @type:	frontend type for this PRZ
 * @flags:	holds PRZ_FLAGS_* bits
 *
 * @buffer_lock:
 *	locks access to @buffer "size" bytes and "start" offset
 * @buffer:
 *	pointer to actual RAM area managed by this PRZ
 * @buffer_size:
 *	bytes in @buffer->data (not including any trailing ECC bytes)
 *
 * @par_buffer:
 *	pointer into @buffer->data containing ECC bytes for @buffer->data
 * @par_header:
 *	pointer into @buffer->data containing ECC bytes for @buffer header
 *	(i.e. all fields up to @data)
 * @rs_decoder:
 *	RSLIB instance for doing ECC calculations
 * @corrected_bytes:
 *	ECC corrected bytes accounting since boot
 * @bad_blocks:
 *	ECC uncorrectable bytes accounting since boot
 * @ecc_info:
 *	ECC configuration details
 *
 * @old_log:
 *	saved copy of @buffer->data prior to most recent wipe
 * @old_log_size:
 *	bytes contained in @old_log
 *
 */
struct persistent_ram_zone {
	phys_addr_t paddr;
	size_t size;
	void *vaddr;
	char *label;
	enum pstore_type_id type;
	u32 flags;

	raw_spinlock_t buffer_lock;
	struct persistent_ram_buffer *buffer;
	size_t buffer_size;

	char *par_buffer;
	char *par_header;
	struct rs_control *rs_decoder;
	int corrected_bytes;
	int bad_blocks;
	struct persistent_ram_ecc_info ecc_info;

	char *old_log;
	size_t old_log_size;
};

struct persistent_ram_zone *persistent_ram_new(phys_addr_t start, size_t size,
			u32 sig, struct persistent_ram_ecc_info *ecc_info,
			unsigned int memtype, u32 flags, char *label);
void persistent_ram_free(struct persistent_ram_zone *prz);
void persistent_ram_zap(struct persistent_ram_zone *prz);

int persistent_ram_write(struct persistent_ram_zone *prz, const void *s,
			 unsigned int count);
int persistent_ram_write_user(struct persistent_ram_zone *prz,
			      const void __user *s, unsigned int count);

void persistent_ram_save_old(struct persistent_ram_zone *prz);
size_t persistent_ram_old_size(struct persistent_ram_zone *prz);
void *persistent_ram_old(struct persistent_ram_zone *prz);
void persistent_ram_free_old(struct persistent_ram_zone *prz);
ssize_t persistent_ram_ecc_string(struct persistent_ram_zone *prz,
	char *str, size_t len);

/*
 * Ramoops platform data
 * @mem_size	memory size for ramoops
 * @mem_address	physical memory address to contain ramoops
 */

#define RAMOOPS_FLAG_FTRACE_PER_CPU	BIT(0)

struct ramoops_platform_data {
	unsigned long	mem_size;
	phys_addr_t	mem_address;
	unsigned int	mem_type;
	unsigned long	record_size;
	unsigned long	console_size;
	unsigned long	ftrace_size;
	unsigned long	pmsg_size;
	int		dump_oops;
	u32		flags;
	struct persistent_ram_ecc_info ecc_info;
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_DUMP_DEVICE_INFO)
	unsigned long	 device_info_size;
	unsigned long	 dump_size;
	unsigned long	 rsv01_size;
	unsigned long	 rsv02_size;
	unsigned long	 rsv03_size;
	unsigned long	 rsv04_size;
	unsigned long	 rsv05_size;
#endif
};

#if IS_ENABLED(CONFIG_OPLUS_FEATURE_DUMP_DEVICE_INFO)
struct ramoops_context {
	struct persistent_ram_zone **dprzs;	/* Oops dump zones */
	struct persistent_ram_zone *cprz;	/* Console zone */
	struct persistent_ram_zone **fprzs;	/* Ftrace zones */
	struct persistent_ram_zone *mprz;	/* PMSG zone */
	phys_addr_t phys_addr;
	unsigned long size;
	unsigned int memtype;
	size_t record_size;
	size_t console_size;
	size_t ftrace_size;
	size_t pmsg_size;
	int dump_oops;
	u32 flags;
	struct persistent_ram_ecc_info ecc_info;
	unsigned int max_dump_cnt;
	unsigned int dump_write_cnt;
	/* _read_cnt need clear on ramoops_pstore_open */
	unsigned int dump_read_cnt;
	unsigned int console_read_cnt;
	unsigned int max_ftrace_cnt;
	unsigned int ftrace_read_cnt;
	unsigned int pmsg_read_cnt;

	struct pstore_info pstore;
	struct persistent_ram_zone *devprz;
	struct persistent_ram_zone *dumpprz;
	struct persistent_ram_zone *rsv01prz;
	struct persistent_ram_zone *rsv02prz;
	struct persistent_ram_zone *rsv03prz;
	struct persistent_ram_zone *rsv04prz;
	struct persistent_ram_zone *rsv05prz;
	unsigned int dev_info_cnt;
	unsigned int dump_cnt;
	unsigned int rsv01_cnt;
	unsigned int rsv02_cnt;
	unsigned int rsv03_cnt;
	unsigned int rsv04_cnt;
	unsigned int rsv05_cnt;
	size_t device_info_size;
	size_t dump_size;
	size_t rsv01_size;
	size_t rsv02_size;
	size_t rsv03_size;
	size_t rsv04_size;
	size_t rsv05_size;
};
#endif

#endif
