/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2014 Sergey Senozhatsky.
 */

#ifndef _ZCOMP_H_
#define _ZCOMP_H_

struct zcomp_strm {
	/* compression/decompression buffer */
	void *buffer;
	struct crypto_comp *tfm;
};

/* dynamic per-device compression frontend */
struct zcomp {
	struct zcomp_strm * __percpu *stream;
	const char *name;
	struct hlist_node node;
#ifdef CONFIG_CONT_PTE_HUGEPAGE_64K_ZRAM
	bool is_thp_comp;
#endif
};

int zcomp_cpu_up_prepare(unsigned int cpu, struct hlist_node *node);
int zcomp_cpu_dead(unsigned int cpu, struct hlist_node *node);
ssize_t zcomp_available_show(const char *comp, char *buf);
bool zcomp_available_algorithm(const char *comp);
#ifdef CONFIG_CONT_PTE_HUGEPAGE_64K_ZRAM
struct zcomp *zcomp_create(const char *comp, bool is_thp_comp);
#else
struct zcomp *zcomp_create(const char *comp);
#endif
void zcomp_destroy(struct zcomp *comp);

struct zcomp_strm *zcomp_stream_get(struct zcomp *comp);
void zcomp_stream_put(struct zcomp *comp);

int zcomp_compress(struct zcomp_strm *zstrm,
		const void *src, unsigned int *dst_len);

int zcomp_decompress(struct zcomp_strm *zstrm,
		const void *src, unsigned int src_len, void *dst);
#ifdef CONFIG_CONT_PTE_HUGEPAGE_64K_ZRAM
int zcomp_compress_thp(struct zcomp_strm *zstrm,
		const void *src, unsigned int *dst_len);

int zcomp_decompress_thp(struct zcomp_strm *zstrm,
		const void *src, unsigned int src_len, void *dst);
#endif

bool zcomp_set_max_streams(struct zcomp *comp, int num_strm);
#ifdef CONFIG_CONT_PTE_HUGEPAGE_64K_ZRAM
int zcomp_create_thp_zstrm_buffer(void);
void zcomp_destroy_thp_zstrm_buffer(void);
#endif
#endif /* _ZCOMP_H_ */
