/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef _TOUCHPANEL_COMMON_API_H_
#define _TOUCHPANEL_COMMON_API_H_

#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/uaccess.h>

#include "../touchpanel_common.h"

/*******Part0:LOG TAG Declear************************/
extern unsigned int tp_debug;
#define TPD_PRINT_POINT_NUM 150

#ifdef TPD_DEVICE
#undef TPD_DEVICE
#define TPD_DEVICE "touchpanel"
#else
#define TPD_DEVICE "touchpanel"
#endif

#define TPD_INFO(a, arg...)  pr_err("[TP]"TPD_DEVICE ": " a, ##arg)
#define TP_INFO(index, a, arg...)  pr_err("[TP""%x""]"TPD_DEVICE": " a, index, ##arg)

#define TPD_DEBUG(a, arg...)\
	do {\
		if (LEVEL_DEBUG == tp_debug)\
		pr_err("[TP]"TPD_DEVICE ": " a, ##arg);\
	}while(0)

#define TP_DEBUG(index, a, arg...)\
			do {\
				if (LEVEL_DEBUG == tp_debug)\
					pr_err("[TP""%x""]"TPD_DEVICE": " a, index, ##arg);\
			}while(0)

#define TPD_DETAIL(a, arg...)\
	do {\
		if (LEVEL_BASIC != tp_debug)\
			pr_err("[TP]"TPD_DEVICE ": " a, ##arg);\
	}while(0)

#define TP_DETAIL(index, a, arg...)\
			do {\
				if (LEVEL_BASIC != tp_debug)\
					pr_err("[TP""%x""]"TPD_DEVICE": " a, index, ##arg);\
			}while(0)

#define TPD_SPECIFIC_PRINT(count, a, arg...)\
	do {\
		if (count++ == TPD_PRINT_POINT_NUM || LEVEL_DEBUG == tp_debug) {\
			TPD_INFO(TPD_DEVICE ": " a, ##arg);\
			count = 0;\
		}\
	}while(0)

#define TP_SPECIFIC_PRINT(index, count, a, arg...)\
			do {\
				if (count++ == TPD_PRINT_POINT_NUM || LEVEL_DEBUG == tp_debug) {\
					TPD_INFO(TPD_DEVICE"%x"": " a, index, ##arg);\
					count = 0;\
				}\
			}while(0)

#define TPD_DEBUG_NTAG(a, arg...)\
			do {\
				if (tp_debug)\
					printk(a, ##arg);\
			}while(0)

/*******Part1: common api Area********************************/

/**
 * tp_memcpy - tp secure memcpy
 * @dest: Where to copy to
 * @dest_size: sizeof dest space size
 * @src: Where to copy from
 * @src_size: sizeof src space size
 * @count: The size of the area.
 * we can using this function to memcpy some buffer more secureful
 * Returning zero(success) or negative errno(failed)
 */
static inline int tp_memcpy(void *dest, unsigned int dest_size,
			    void *src, unsigned int src_size,
			    unsigned int count)
{
	if (dest == NULL || src == NULL) {
		return -EINVAL;
	}

	if (count > dest_size || count > src_size) {
		TPD_INFO("%s: src_size = %d, dest_size = %d, count = %d\n",
			 __func__, src_size, dest_size, count);
		return -EINVAL;
	}

	memcpy((void *)dest, (void *)src, count);

	return 0;
}

/**
 * tp_const_memcpy - tp secure memcpy
 * @dest: Where to copy to
 * @dest_size: sizeof dest space size
 * @src: Where to copy from
 * @src_size: sizeof src space size
 * @count: The size of the area.
 * we can using this function to memcpy some buffer more secureful
 * Returning zero(success) or negative errno(failed)
 */
static inline int tp_const_memcpy(void *dest, unsigned int dest_size,
				  const void *src, unsigned int src_size,
				  unsigned int count)
{
	if (dest == NULL || src == NULL) {
		return -EINVAL;
	}

	if (count > dest_size || count > src_size) {
		TPD_INFO("%s: src_size = %d, dest_size = %d, count = %d\n",
			 __func__, src_size, dest_size, count);
		return -EINVAL;
	}

	memcpy((void *)dest, (const void *)src, count);

	return 0;
}

struct tp_buffer {
	bool clone;                    /*true:can not free, false:can free*/
	unsigned char *buf;            /*poninter to kmalloc buffer*/
	unsigned int buf_size;        /*size of kmalloc, max size is unsigned int */
	unsigned int data_length;   /*length of data*/
	struct mutex buf_mutex;     /*mutex for this memory buffer*/
};

/**
 * INIT_BUFFER - set is_clone true or false
 * @tp_buffer: struct tp_buffer
 * @is_clone: true:can not free, false:can free
 */
#define TP_INIT_BUFFER(tp_buffer, is_clone) \
	mutex_init(&tp_buffer.buf_mutex); \
	tp_buffer.clone = is_clone

#define TP_LOCK_BUFFER(tp_buffer) \
	mutex_lock(&tp_buffer.buf_mutex)

#define TP_UNLOCK_BUFFER(tp_buffer) \
	mutex_unlock(&tp_buffer.buf_mutex)

#define TP_RELEASE_BUFFER(tp_buffer) \
	do { \
		if (tp_buffer.clone == false) { \
			kfree(tp_buffer.buf); \
			tp_buffer.buf_size = 0; \
			tp_buffer.data_length = 0; \
		} \
	} while (0)

static inline int tp_realloc_mem(struct tp_buffer *buffer, unsigned int size)
{
	int retval;
	unsigned char *temp;

	if (size > buffer->buf_size) {
		temp = buffer->buf;

		buffer->buf = kmalloc(size, GFP_KERNEL);

		if (!(buffer->buf)) {
			TPD_INFO("%s: Failed to allocate memory\n",
				 __func__);
			kfree(temp);
			buffer->buf_size = 0;
			return -ENOMEM;
		}

		retval = tp_const_memcpy(buffer->buf,
					 size,
					 temp,
					 buffer->buf_size,
					 buffer->buf_size);

		if (retval < 0) {
			TPD_INFO("%s: Failed to copy data\n", __func__);
			kfree(temp);
			kfree(buffer->buf);
			buffer->buf_size = 0;
			return retval;
		}

		kfree(temp);
		buffer->buf_size = size;
	}

	return 0;
}

static inline int tp_alloc_mem(struct tp_buffer *buffer, unsigned int size)
{
	if (size > buffer->buf_size) {
		kfree(buffer->buf);
		buffer->buf = kmalloc(size, GFP_KERNEL);

		if (!(buffer->buf)) {
			TPD_INFO("%s: Failed to allocate memory, size %d\n", __func__, size);
			buffer->buf_size = 0;
			buffer->data_length = 0;
			return -ENOMEM;
		}

		buffer->buf_size = size;
	}

	memset(buffer->buf, 0, buffer->buf_size);
	buffer->data_length = 0;

	return 0;
}

static inline void *tp_devm_kzalloc(struct device *dev, size_t size, gfp_t gfp)
{
	void *p;

	p = devm_kzalloc(dev, size, gfp);

	if (!p) {
		TPD_INFO("%s: Failed to allocate memory\n", __func__);
	}

	return p;
}

static inline void tp_devm_kfree(struct device *dev, void **mem, size_t size)
{
	if (*mem != NULL) {
		devm_kfree(dev, *mem);
		*mem = NULL;
	}
}

/**
 * kzalloc - allocate memory. The memory is set to zero.
 * @size: how many bytes of memory are required.
 * @flags: the type of memory to allocate (see kmalloc).
 */

static inline void *tp_kzalloc(size_t size, gfp_t flags)
{
	void *p;

	p = kzalloc(size, flags);

	if (!p) {
		TPD_INFO("%s: Failed to allocate memory\n", __func__);
		/*add for health monitor*/
	}

	return p;
}

static inline void tp_kfree(void **mem)
{
	if (*mem != NULL) {
		kfree(*mem);
		*mem = NULL;
	}
}

/**
 *    vzalloc - allocate virtually contiguous memory with zero fill
 *    @size:    allocation size
 *    Allocate enough pages to cover @size from the page level
 *    allocator and map them into contiguous kernel virtual space.
 *    The memory allocated is set to zero.
 *
 *    For tight control over page level allocator and protection flags
 *    use __vmalloc() instead.
 */

static inline void *tp_vzalloc(unsigned long size)
{
	void *p;

	p = vzalloc(size);

	if (!p) {
		TPD_INFO("%s: Failed to allocate memory\n", __func__);
		/*add for health monitor*/
	}

	return p;
}

static inline void tp_vfree(void **mem)
{
	if (*mem != NULL) {
		vfree(*mem);
		*mem = NULL;
	}
}

/**
 * tp_copy_from_user - tp secure tp_copy_from_user
 * @to: pointer to dest addr
 * @dest_size: sizeof dest space size
 * @from: pointer to src addr
 * @src_size: sizeof src space size
 * @MaxCount: how much size you want to copy_from_user
 * we can using this function to copy_from_user some buffer more secure ful
 * Returning zero(success) or negative errno(failed)
 */

static inline unsigned long tp_copy_from_user(void *to, unsigned long dest_size,
		const void __user *from,
		unsigned long src_size, unsigned long MaxCount)
{
	unsigned long ret = 0;

	if (src_size > MaxCount) {
		TPD_INFO("%s:MaxCount = %lu, src_size = %lu\n",
			 __func__, MaxCount, src_size);
		return src_size;
	}

	if (src_size > dest_size) {
		TPD_INFO("%s:dest_size = %lu, src_size = %lu\n",
			 __func__, dest_size, src_size);
		return src_size;
	}

	if (to == NULL || from == NULL) {
		return src_size;
	}

	ret = copy_from_user(to, from, src_size);

	if (ret) {
		TPD_INFO("%s: read proc input error.\n", __func__);
		return ret;
	}

	return 0;
}
#endif /*_TOUCHPANEL_COMMON_API_H_*/
