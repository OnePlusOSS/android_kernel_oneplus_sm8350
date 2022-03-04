// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include "../touchpanel_common.h"
#include "touchpanel_autotest.h"
#include "../touch_comon_api/touch_comon_api.h"
#include <linux/seq_file.h>
#include <linux/delay.h>
#include <linux/uaccess.h>
#include <linux/syscalls.h>
#include <linux/proc_fs.h>

/*******Start of LOG TAG Declear**********************************/
#ifdef TPD_DEVICE
#undef TPD_DEVICE
#define TPD_DEVICE "touchpanel_autotest"
#else
#define TPD_DEVICE "touchpanel_autotest"
#endif
/*******End of LOG TAG Declear***********************************/
/**
 * tp_test_write - instead of vfs_write,save test result to memory
 * @data_start: pointer to memory buffer
 * @max_count: max length for memory buffer
 * @buf: new buffer
 * @count: count of new buffer
 * @pos: pos of current length for memory buffer
 * we can using this function to get item offset form index item
 * Returning parameter number(success) or negative errno(failed)
 */
ssize_t tp_test_write(void *data_start, size_t max_count,
		      const char *buf, size_t count, ssize_t *pos)
{
	ssize_t ret = 0;
	char *p = NULL;

	if (!data_start) {
		return -1;
	}

	if (!buf) {
		return -1;
	}

	if (*pos >= max_count) {
		TPD_INFO("%s: pos:%ld is out of memory\n", *pos, __func__);
		return -1;
	}

	p = (char *)data_start + *pos;

	memcpy(p, buf, count);
	*pos += count;

	return ret;
}

EXPORT_SYMBOL(tp_test_write);

/**
 * search_for_item_offset - get each item offset form test limit fw
 * @fw: pointer to fw
 * @item_cnt: max item number
 * @item_index: item index
 * we can using this function to get item offset form index item
 * Returning parameter number(success) or negative errno(failed)
 */
uint32_t search_for_item_offset(const struct firmware *fw, int item_cnt,
				uint8_t item_index)
{
	int i = 0;
	uint32_t item_offset = 0;
	struct auto_test_item_header *item_header = NULL;
	uint32_t *p_item_offset = (uint32_t *)(fw->data + 16);

	/*check the matched item offset*/
	for (i = 0; i < item_cnt; i++) {
		item_header = (struct auto_test_item_header *)(fw->data + p_item_offset[i]);

		if (item_header->item_bit == item_index) {
			item_offset = p_item_offset[i];
		}
	}

	return item_offset;
}

/**
 * getpara_for_item - get parameter from item
 * @fw: pointer to fw
 * @item_index: item index
 * @para_num: parameter number
 * we can using this function to get parameter form index item
 * Returning pointer to parameter buffer
 */
int32_t *getpara_for_item(const struct firmware *fw, uint8_t item_index,
			  uint32_t *para_num)
{
	uint32_t item_offset = 0;
	int i = 0;
	uint32_t item_cnt = 0;
	struct auto_test_item_header *item_header = NULL;
	struct auto_test_header *test_header = NULL;
	int32_t *p_buffer = NULL;
	test_header = (struct auto_test_header *)fw->data;

	/*step1: check item index is support or not*/
	if (!(test_header->test_item & (1 << item_index))) {
		TPD_INFO("item_index:%d is not support\n", item_index);
		return NULL;
	}

	/*step2: get max item*/
	for (i = 0; i < 8 * sizeof(test_header->test_item); i++) {
		if ((test_header->test_item >> i) & 0x01) {
			item_cnt++;
		}
	}

	/*step3: find item_index offset from the limit img*/
	item_offset = search_for_item_offset(fw, item_cnt, item_index);

	if (item_offset == 0) {
		TPD_INFO("search for item limit offset failed\n");
		return NULL;
	}

	/*step4: check the item magic is support or not*/
	item_header = (struct auto_test_item_header *)(fw->data + item_offset);

	if (item_header->item_magic != Limit_ItemMagic && item_header->item_magic != Limit_ItemMagic_V2) {
		TPD_INFO("test item: %d magic number(%4x) is wrong\n", item_index,
			 item_header->item_magic);
		return NULL;
	}

	/*step5: get the parameter from the limit img*/
	if (item_header->para_num == 0) {
		TPD_INFO("item: %d has %d no parameter\n", item_index, item_header->para_num);
		return NULL;

	} else {
		p_buffer = (int32_t *)(fw->data + item_offset + sizeof(struct
				       auto_test_item_header));

		for (i = 0; i < item_header->para_num; i++) {
			TPD_INFO("item: %d has parameter:%d\n", item_index, p_buffer[i]);
		}
	}

	*para_num = item_header->para_num;
	return p_buffer;
}
EXPORT_SYMBOL(getpara_for_item);

/**
 * get_info_for_item - get all infomation from item
 * @fw: pointer to fw
 * @item_index: item index
 * we can using this function to get infomation form index item
 * Returning pointer to test_item_info buffer
 */
struct test_item_info *get_test_item_info(const struct firmware *fw,
		uint8_t item_index)
{
	uint32_t item_offset = 0;
	int i = 0;
	uint32_t item_cnt = 0;
	struct auto_test_item_header *item_header = NULL;
	struct auto_test_header *test_header = NULL;
	int32_t *p_buffer = NULL;

	/*result: test_item_info */
	struct test_item_info *p = NULL;

	p = tp_kzalloc(sizeof(*p), GFP_KERNEL);

	if (!p) {
		return NULL;
	}

	/*step1: check item index is support or not*/
	test_header = (struct auto_test_header *)fw->data;

	if (!(test_header->test_item & (1 << item_index))) {
		TPD_INFO("item_index:%d is not support\n", item_index);
		goto ERROR;
	}

	/*step2: get max item*/
	for (i = 0; i < 8 * sizeof(test_header->test_item); i++) {
		if ((test_header->test_item >> i) & 0x01) {
			item_cnt++;
		}
	}

	/*step3: find item_index offset from the limit img*/
	item_offset = search_for_item_offset(fw, item_cnt, item_index);

	if (item_offset == 0) {
		TPD_INFO("search for item limit offset failed\n");
		goto ERROR;
	}

	/*get item_offset*/
	p->item_offset = item_offset;

	/*step4: check the item magic is support or not*/
	item_header = (struct auto_test_item_header *)(fw->data + item_offset);

	if (item_header->item_magic != Limit_ItemMagic && item_header->item_magic != Limit_ItemMagic_V2) {
		TPD_INFO("test item: %d magic number(%4x) is wrong\n", item_index,
			 item_header->item_magic);
		goto ERROR;
	}

	/*get item_header*/
	p->item_magic = item_header->item_magic;
	p->item_size = item_header->item_size;
	p->item_bit = item_header->item_bit;
	p->item_limit_type = item_header->item_limit_type;
	p->top_limit_offset = item_header->top_limit_offset;
	p->floor_limit_offset = item_header->floor_limit_offset;

	/*step5: get the parameter from the limit img*/
	if (item_header->para_num == 0) {
		TPD_INFO("item: %d has %d no parameter\n", item_index, item_header->para_num);
		goto ERROR;

	} else {
		p_buffer = (int32_t *)(fw->data + item_offset + sizeof(struct
				       auto_test_item_header));

		for (i = 0; i < item_header->para_num; i++) {
			TPD_INFO("item: %d has parameter:%d\n", item_index, p_buffer[i]);
		}
	}

	/*get item para number and para buffer*/
	p->para_num = item_header->para_num;
	p->p_buffer = p_buffer;

	return p;

ERROR:
	tp_kfree((void **)&p);
	return NULL;
}
EXPORT_SYMBOL(get_test_item_info);

/**
 * save_test_result - save test result to file
 * @p_auto_testdata: pointer to auto_testdata
 * @data: test result data
 * @limit_type:  limit type
 * we can using this function to save test result to file
 * Returning parameter number(success) or negative errno(failed)
 */
int save_test_result(struct auto_testdata *p_auto_testdata,
		     short  *data, int data_size,
		     enum limit_type limit_type, char  *limit_name)
{
	uint8_t  data_buf[64] = {0};
	int ret = 0;
	int i = 0;

	if (!data || !p_auto_testdata || !limit_name) {
		ret = -1;
		TPD_INFO("%s: data is null\n", __func__);
		return ret;
	}

	if (IS_ERR_OR_NULL(p_auto_testdata->fp)) {
		ret = -1;
		TPD_INFO("%s: file is null\n", __func__);
		return ret;
	}

	snprintf(data_buf, 64, "[%s]\n", limit_name);
	tp_test_write(p_auto_testdata->fp, p_auto_testdata->length, data_buf,
		      strlen(data_buf), p_auto_testdata->pos);

	if (limit_type == LIMIT_TYPE_TX_RX_DATA) {
		if (data_size < p_auto_testdata->rx_num * p_auto_testdata->tx_num) {
			return -1;
		}
		for (i = 0; i < p_auto_testdata->rx_num * p_auto_testdata->tx_num; i++) {
			snprintf(data_buf, 64, "%d,", data[i]);
			tp_test_write(p_auto_testdata->fp, p_auto_testdata->length, data_buf,
				      strlen(data_buf), p_auto_testdata->pos);

			if (!((i + 1) % p_auto_testdata->rx_num) && (i != 0)) {
				snprintf(data_buf, 64, "\n");
				tp_test_write(p_auto_testdata->fp, p_auto_testdata->length, data_buf,
					      strlen(data_buf), p_auto_testdata->pos);
			}
		}

		snprintf(data_buf, 64, "\n");
		tp_test_write(p_auto_testdata->fp, p_auto_testdata->length, data_buf,
			      strlen(data_buf), p_auto_testdata->pos);

	} else if (limit_type == LIMIT_TYPE_SLEF_TX_RX_DATA) {
		if (data_size < p_auto_testdata->rx_num + p_auto_testdata->tx_num) {
			return -1;
		}
		for (i = 0; i < p_auto_testdata->rx_num + p_auto_testdata->tx_num; i++) {
			snprintf(data_buf, 64, "%d,", data[i]);
			tp_test_write(p_auto_testdata->fp, p_auto_testdata->length, data_buf,
				      strlen(data_buf), p_auto_testdata->pos);
		}

		snprintf(data_buf, 64, "\n");
		tp_test_write(p_auto_testdata->fp, p_auto_testdata->length, data_buf,
			      strlen(data_buf), p_auto_testdata->pos);
	}

	return ret;
}
EXPORT_SYMBOL(save_test_result);

static int tp_test_limit_switch(struct touchpanel_data *ts)
{
	char *p_node = NULL;
	char *postfix = "_AGING";
	uint8_t copy_len = 0;

	if (!ts) {
		TP_INFO(ts->tp_index, "ts is NULL\n");
		return -1;
	}

	ts->panel_data.aging_test_limit_name = tp_devm_kzalloc(ts->dev, MAX_FW_NAME_LENGTH, GFP_KERNEL);
	if (ts->panel_data.aging_test_limit_name == NULL) {
		TP_INFO(ts->tp_index, "[TP]panel_data.test_limit_name kzalloc error\n");
		return -1;
	}

	/**change **.img to **_AGING.img*/
	p_node	= strstr(ts->panel_data.test_limit_name, ".");
	if (p_node == NULL) {
		TP_INFO(ts->tp_index, "p_node strstr error!\n");
		goto EXIT;
	}

	copy_len = p_node - ts->panel_data.test_limit_name;
	memcpy(ts->panel_data.aging_test_limit_name, ts->panel_data.test_limit_name, copy_len);
	strlcat(ts->panel_data.aging_test_limit_name, postfix, MAX_LIMIT_DATA_LENGTH_COM);
	strlcat(ts->panel_data.aging_test_limit_name, p_node, MAX_LIMIT_DATA_LENGTH_COM);
	TP_INFO(ts->tp_index, "aging_test_limit_name is %s\n", ts->panel_data.aging_test_limit_name);
	return 0;

EXIT:
	tp_devm_kfree(ts->dev, (void **)&ts->panel_data.aging_test_limit_name, MAX_FW_NAME_LENGTH);

	return -1;
}

static int request_test_limit(const struct firmware **fw,
			      char *test_limit_name, struct device *device)
{
	int ret = 0;
	int retry = 5;

	do {
		ret = request_firmware(fw, test_limit_name, device);

		if (!ret) {
			break;
		}
	} while ((ret < 0) && (--retry > 0));

	TPD_INFO("retry times %d\n", 5 - retry);
	return ret;
}

static int request_real_test_limit(struct touchpanel_data *ts,
				   const struct firmware **fw, char *test_limit_name, struct device *device)
{
	int ret = 0;

	if (AGING_TEST_MODE == ts->aging_mode) {
		ret = tp_test_limit_switch(ts);
		if (ret < 0) {
			return ret;
		}
		ret = request_test_limit(fw, ts->panel_data.aging_test_limit_name, device);
		if (ret < 0) {
			ret = request_test_limit(fw, test_limit_name, device);
		}
		tp_devm_kfree(ts->dev, (void **)&ts->panel_data.aging_test_limit_name, MAX_FW_NAME_LENGTH);
	} else {
		ret = request_test_limit(fw, test_limit_name, device);
	}

	return ret;
}

void tp_limit_read(struct seq_file *s, struct touchpanel_data *ts)
{
	int ret = 0;
	const struct firmware *fw = NULL;
	struct auto_test_header *ph = NULL;
	int m = 0, i = 0, j = 0, item_cnt = 0;
	struct auto_test_item_header *item_head = NULL;
	uint32_t *p_item_offset = NULL;
	int32_t *p_data32 = NULL;

	ret =  request_real_test_limit(ts, &fw, ts->panel_data.test_limit_name, ts->dev);

	if (ret < 0) {
		TPD_INFO("Request firmware failed - %s (%d)\n", ts->panel_data.test_limit_name,
			 ret);
		seq_printf(s, "Request failed, Check the path %s\n",
			   ts->panel_data.test_limit_name);
		return;
	}

	ph = (struct auto_test_header *)(fw->data);
	p_item_offset = (uint32_t *)(fw->data + 16);

	if ((ph->magic1 != Limit_MagicNum1) || (ph->magic2 != Limit_MagicNum2)) {
		TPD_INFO("limit image is not generated by oplus\n");
		seq_printf(s, "limit image is not generated by oplus\n");
		return;
	}

	TPD_INFO("magic1:%x,magic2:%x,test_item:%llu\n", ph->magic1, ph->magic2,
		 ph->test_item);

	for (i = 0; i < 8 * sizeof(ph->test_item); i++) {
		if ((ph->test_item >> i) & 0x01) {
			item_cnt++;
		}
	}

	TPD_INFO("item_cnt :%d\n", item_cnt);

	if (!item_cnt) {
		TPD_INFO("limit image has no test item\n");
		seq_printf(s, "limit image has no test item\n");
	}

	for (m = 0; m < item_cnt; m++) {
		item_head = (struct auto_test_item_header *)(fw->data + p_item_offset[m]);
		TPD_INFO("item %d[size %d, limit type %d,top limit %d, floor limit %d para num %d] :\n",
			 item_head->item_bit, item_head->item_size, item_head->item_limit_type,
			 item_head->top_limit_offset, item_head->floor_limit_offset,
			 item_head->para_num);

		if (item_head->item_magic != Limit_ItemMagic && item_head->item_magic != Limit_ItemMagic_V2) {
			seq_printf(s, "item: %d limit data has some problem\n", item_head->item_bit);
			continue;
		}

		seq_printf(s, "item[%d]:", m);

		if (item_head->item_limit_type == LIMIT_TYPE_NO_DATA) {
			seq_printf(s, "no limit data\n");

		} else if (item_head->item_limit_type == LIMIT_TYPE_CERTAIN_DATA) {
			p_data32 = (int32_t *)(fw->data + item_head->top_limit_offset);
			seq_printf(s, "top limit data: %d\n", *p_data32);
			p_data32 = (int32_t *)(fw->data + item_head->floor_limit_offset);
			seq_printf(s, "floor limit data: %d\n", *p_data32);

		} else if (item_head->item_limit_type == LIMIT_TYPE_EACH_NODE_DATA) {
			seq_printf(s, "raw top data: \n");
			p_data32 = (int32_t *)(fw->data + item_head->top_limit_offset);

			for (i = 0; i < (ts->hw_res.tx_num * ts->hw_res.rx_num); i++) {
				if (i % ts->hw_res.rx_num == 0) {
					seq_printf(s, "\n[%2d] ", (i / ts->hw_res.rx_num));
				}

				seq_printf(s, "%4d, ", p_data32[i]);
				TPD_DEBUG("%d, ", p_data32[i]);
			}

			seq_printf(s, "\n\ngap raw top data: \n");
			p_data32 = (int32_t *)(fw->data + item_head->top_limit_offset + 4 *
					       ts->hw_res.tx_num * ts->hw_res.rx_num);

			for (i = 0; i < (ts->hw_res.tx_num * ts->hw_res.rx_num); i++) {
				if (i % ts->hw_res.rx_num == 0) {
					seq_printf(s, "\n[%2d] ", (i / ts->hw_res.rx_num));
				}

				seq_printf(s, "%4d, ", p_data32[i]);
				TPD_DEBUG("%d, ", p_data32[i]);
			}

			seq_printf(s, "\n\nraw floor data: \n");
			p_data32 = (int32_t *)(fw->data + item_head->floor_limit_offset);

			for (i = 0; i < (ts->hw_res.tx_num * ts->hw_res.rx_num); i++) {
				if (i % ts->hw_res.rx_num == 0) {
					seq_printf(s, "\n[%2d] ", (i / ts->hw_res.rx_num));
				}

				seq_printf(s, "%4d, ", p_data32[i]);
				TPD_DEBUG("%d, ", p_data32[i]);
			}

			seq_printf(s, "\n\ngap raw floor data: \n");
			p_data32 = (int32_t *)(fw->data + item_head->floor_limit_offset + 4 *
					       ts->hw_res.tx_num * ts->hw_res.rx_num);

			for (i = 0; i < (ts->hw_res.tx_num * ts->hw_res.rx_num); i++) {
				if (i % ts->hw_res.rx_num == 0) {
					seq_printf(s, "\n[%2d] ", (i / ts->hw_res.rx_num));
				}

				seq_printf(s, "%4d, ", p_data32[i]);
				TPD_DEBUG("%d, ", p_data32[i]);
			}

		} else if (item_head->item_limit_type == LIMIT_TYPE_TX_RX_DATA) {
			seq_printf(s, "raw top data: \n");
			p_data32 = (int32_t *)(fw->data + item_head->top_limit_offset);

			for (i = 0; i < (ts->hw_res.tx_num * ts->hw_res.rx_num); i++) {
				if (i % ts->hw_res.rx_num == 0) {
					seq_printf(s, "\n[%2d] ", (i / ts->hw_res.rx_num));
				}

				seq_printf(s, "%4d, ", p_data32[i]);
				TPD_DEBUG("%d, ", p_data32[i]);
			}

			seq_printf(s, "\n\nraw floor data: \n");
			p_data32 = (int32_t *)(fw->data + item_head->floor_limit_offset);

			for (i = 0; i < (ts->hw_res.tx_num * ts->hw_res.rx_num); i++) {
				if (i % ts->hw_res.rx_num == 0) {
					seq_printf(s, "\n[%2d] ", (i / ts->hw_res.rx_num));
				}

				seq_printf(s, "%4d, ", p_data32[i]);
				TPD_DEBUG("%d, ", p_data32[i]);
			}

		} else if (item_head->item_limit_type == LIMIT_TYPE_SLEF_TX_RX_DATA) {
			seq_printf(s, "tx + rx data: \n");
			p_data32 = (int32_t *)(fw->data + item_head->top_limit_offset);

			for (i = 0; i < (ts->hw_res.tx_num + ts->hw_res.rx_num); i++) {
				if (i % ts->hw_res.rx_num == 0) {
					seq_printf(s, "\n[%2d] ", (i / ts->hw_res.rx_num));
				}

				seq_printf(s, "%4d, ", p_data32[i]);
				TPD_DEBUG("%d, ", p_data32[i]);
			}

		} else if (item_head->item_limit_type == LIMIT_TYPE_SLEF_TX_RX_DATA_DOUBLE) {
			seq_printf(s, "tx floor data: \n");
			p_data32 = (int32_t *)(fw->data + item_head->top_limit_offset);
			seq_printf(s, "\n[ 0] ");

			for (i = 0; i < ts->hw_res.tx_num; i++) {
				seq_printf(s, "%4d, ", p_data32[i]);
				TPD_DEBUG("%d, ", p_data32[i]);
			}

			seq_printf(s, "\n\ntx top data: \n");
			p_data32 = (int32_t *)(fw->data + item_head->top_limit_offset + 4 *
					       ts->hw_res.tx_num);
			seq_printf(s, "\n[ 1] ");

			for (i = 0; i < ts->hw_res.tx_num; i++) {
				seq_printf(s, "%4d, ", p_data32[i]);
				TPD_DEBUG("%d, ", p_data32[i]);
			}

			seq_printf(s, "\n\nrx floor data: \n");
			p_data32 = (int32_t *)(fw->data + item_head->top_limit_offset + 2 * 4 *
					       ts->hw_res.tx_num);
			seq_printf(s, "\n[ 2] ");

			for (i = 0; i < ts->hw_res.rx_num; i++) {
				seq_printf(s, "%4d, ", p_data32[i]);
				TPD_DEBUG("%d, ", p_data32[i]);
			}

			seq_printf(s, "\n\nrx top data: \n");
			p_data32 = (int32_t *)(fw->data + item_head->top_limit_offset + 2 * 4 *
					       ts->hw_res.tx_num + 4 * ts->hw_res.rx_num);
			seq_printf(s, "\n[ 3] ");

			for (i = 0; i <  ts->hw_res.rx_num; i++) {
				seq_printf(s, "%4d, ", p_data32[i]);
				TPD_DEBUG("%d, ", p_data32[i]);
			}
		}

		p_data32 = (int32_t *)(fw->data + p_item_offset[m] + sizeof(
					       struct auto_test_item_header));

		if (item_head->para_num) {
			seq_printf(s, "\n\nparameter:");

			for (j = 0; j < item_head->para_num; j++) {
				seq_printf(s, "%d, ", p_data32[j]);
			}

			seq_printf(s, "\n");
		}

		seq_printf(s, "\n");
	}

	release_firmware(fw);
}
EXPORT_SYMBOL(tp_limit_read);

int tp_auto_test(struct seq_file *s, void *v)
{
	struct touchpanel_data *ts = s->private;
	int ret = 0;
	int error_count = 0;

	if (!ts) {
		return 0;
	}

	if (!ts->ts_ops) {
		seq_printf(s, "Not support auto-test proc node\n");
		return 0;
	}

	if (!ts->engineer_ops) {
		seq_printf(s, "Not support auto-test proc node\n");
		return 0;
	}

	if (!ts->engineer_ops->auto_test) {
		seq_printf(s, "Not support auto-test proc node\n");
		return 0;
	}

	/*if resume not completed, do not do screen on test*/
	if (ts->suspend_state != TP_SPEEDUP_RESUME_COMPLETE) {
		seq_printf(s, "Not in resume state\n");
		return 0;
	}

	/*step1:disable_irq && get mutex locked*/
	if (ts->int_mode == BANNABLE) {
		disable_irq_nosync(ts->irq);
	}

	mutex_lock(&ts->mutex);

	if (ts->esd_handle_support) {
		esd_handle_switch(&ts->esd_info, false);
	}

	/*step2:malloc space to store test data*/
	/*set buffer pos to first position every time*/
	ts->com_test_data.result_cur_len = 0;

	if (!ts->com_test_data.result_flag) {
		ts->com_test_data.result_max_len = PAGE_SIZE * 15;/*60k*/
		ts->com_test_data.result_data = kvzalloc(ts->com_test_data.result_max_len,
						GFP_KERNEL);

		if (!ts->com_test_data.result_data) {
			TP_INFO(ts->tp_index, "%s kvzalloc failed\n", __func__);
			ts->com_test_data.result_flag = 0;

		} else {
			ts->com_test_data.result_flag = 1;
			TP_INFO(ts->tp_index, "%s kvzalloc data ok\n", __func__);
		}
	}

	/*step3:request test limit data from userspace*/
	ret =  request_real_test_limit(ts, &ts->com_test_data.limit_fw,
				       ts->panel_data.test_limit_name, ts->dev);

	if (ret < 0) {
		TP_INFO(ts->tp_index, "Request firmware failed - %s (%d)\n",
			ts->panel_data.test_limit_name, ret);
		seq_printf(s, "No limit IMG\n");
		mutex_unlock(&ts->mutex);

		if (ts->int_mode == BANNABLE) {
			enable_irq(ts->irq);
		}

		return 0;
	}

	ts->in_test_process = true;

	error_count = ts->engineer_ops->auto_test(s, ts);

	/*step5: release test limit firmware*/

	release_firmware(ts->com_test_data.limit_fw);

	/*step6: return to normal mode*/
	ts->ts_ops->reset(ts->chip_data);
	operate_mode_switch(ts);

	/*step7: unlock the mutex && enable irq trigger*/
	mutex_unlock(&ts->mutex);

	if (ts->int_mode == BANNABLE) {
		enable_irq(ts->irq);
	}

	ts->in_test_process = false;
	return 0;
}

int tp_black_screen_test(struct file *file, char __user *buffer, size_t count,
			 loff_t *ppos)
{
	int ret = 0;
	int retry = 20;
	int msg_size = MESSAGE_SIZE;
	int error_count = 0;

	struct touchpanel_data *ts = PDE_DATA(file_inode(file));

	TP_INFO(ts->tp_index, "%s %ld %lld\n", __func__, count, *ppos);

	if (!ts || !ts->gesture_test.flag) {
		return 0;
	}

	ts->gesture_test.message = kzalloc(msg_size, GFP_KERNEL);

	if (!ts->gesture_test.message) {
		TP_INFO(ts->tp_index, "failed to alloc memory\n");
		return 0;
	}

	ts->gesture_test.message_size = msg_size;

	/* step1:wait until tp is in sleep, then sleep 500ms to make sure tp is in gesture mode*/
	do {
		if (ts->is_suspended) {
			msleep(500);
			break;
		}

		msleep(200);
	} while (--retry);

	TP_INFO(ts->tp_index, "%s retry times %d\n", __func__, retry);

	if (retry == 0 && !ts->is_suspended) {
		snprintf(ts->gesture_test.message, msg_size - 1, "1 errors: not in sleep ");
		goto OUT;
	}

	mutex_lock(&ts->mutex);

	/*step2:malloc space to store test data*/
	/*set buffer pos to first position every time*/
	ts->com_test_data.bs_result_cur_len = 0;

	if (!ts->com_test_data.bs_result_flag) {
		ts->com_test_data.bs_result_max_len = PAGE_SIZE * 15;/*60k*/
		ts->com_test_data.bs_result_data = kvzalloc(ts->com_test_data.bs_result_max_len,
						   GFP_KERNEL);

		if (!ts->com_test_data.bs_result_data) {
			TP_INFO(ts->tp_index, "%s kvzalloc failed\n", __func__);
			ts->com_test_data.bs_result_flag = 0;

		} else {
			ts->com_test_data.bs_result_flag = 1;
			TP_INFO(ts->tp_index, "%s kvzalloc data ok\n", __func__);
		}
	}

	/*step3:request test limit data from userspace*/
	ret =  request_real_test_limit(ts, &ts->com_test_data.limit_fw,
				       ts->panel_data.test_limit_name, ts->dev);

	if (ret < 0) {
		TP_INFO(ts->tp_index, "Request firmware failed - %s (%d)\n",
			ts->panel_data.test_limit_name, ret);
		mutex_unlock(&ts->mutex);
		goto OUT;
	}

	/*step4:black_screen_test*/
	ts->in_test_process = true;

	if (ts->engineer_ops->black_screen_test) {
		error_count = ts->engineer_ops->black_screen_test(&ts->gesture_test, ts);

	} else {
		TP_INFO(ts->tp_index, "black_screen_test not support\n");
		snprintf(ts->gesture_test.message, msg_size - 1,
			 "1 errors:not support gesture test");
		error_count = -1;
	}

	ts->in_test_process = false;

	/*step5:release test limit firmware*/
	release_firmware(ts->com_test_data.limit_fw);
	mutex_unlock(&ts->mutex);

OUT:
	ts->gesture_test.flag = false;
	ts->gesture_enable = ts->gesture_test.gesture_backup;

	if (!ts->auto_test_force_pass_support) {
		ret = simple_read_from_buffer(buffer, count, ppos, ts->gesture_test.message,
					      strlen(ts->gesture_test.message));

	} else {
		ret = simple_read_from_buffer(buffer, count, ppos, "0", 1);
	}

	tp_kfree((void **)&ts->gesture_test.message);
	return ret;
}

int tp_auto_test_result(struct seq_file *s, void *v)
{
	struct touchpanel_data *ts = s->private;

	TP_INFO(ts->tp_index, "%s:s->size:%d,s->count:%d\n", __func__, s->size,
		s->count);
	mutex_lock(&ts->mutex);

	/*the result data is big than one page, so do twice.*/
	if (s->size <= (ts->com_test_data.result_cur_len)) {
		s->count = s->size;
		mutex_unlock(&ts->mutex);
		return 0;
	}

	if (ts->com_test_data.result_flag) {
		if (ts->com_test_data.result_data) {
			if (ts->com_test_data.result_cur_len) {
				seq_write(s, ts->com_test_data.result_data, ts->com_test_data.result_cur_len);
			}

			kvfree(ts->com_test_data.result_data);
			TP_INFO(ts->tp_index, "%s:free data ok\n", __func__);
			ts->com_test_data.result_flag = 0;
		}

	} else {
		TP_INFO(ts->tp_index, "%s:it must do auto test frist\n", __func__);
		seq_printf(s, "it must do auto test frist\n");
	}

	mutex_unlock(&ts->mutex);

	return 0;
}

int tp_black_screen_result(struct seq_file *s, void *v)
{
	struct touchpanel_data *ts = s->private;

	TP_INFO(ts->tp_index, "%s:s->size:%d,s->count:%d\n", __func__, s->size,
		s->count);

	mutex_lock(&ts->mutex);

	/*the result data is big than one page, so do twice.*/
	if (s->size <= (ts->com_test_data.bs_result_cur_len)) {
		s->count = s->size;
		mutex_unlock(&ts->mutex);
		return 0;
	}

	if (ts->com_test_data.bs_result_flag) {
		if (ts->com_test_data.bs_result_data) {
			if (ts->com_test_data.bs_result_cur_len) {
				seq_write(s, ts->com_test_data.bs_result_data,
					  ts->com_test_data.bs_result_cur_len);
			}

			kvfree(ts->com_test_data.bs_result_data);
			TP_INFO(ts->tp_index, "%s:free data ok\n", __func__);
			ts->com_test_data.bs_result_flag = 0;
		}

	} else {
		TP_INFO(ts->tp_index, "%s:it must do auto test frist\n", __func__);
		seq_printf(s, "it must do auto test frist\n");
	}

	mutex_unlock(&ts->mutex);

	return 0;
}
