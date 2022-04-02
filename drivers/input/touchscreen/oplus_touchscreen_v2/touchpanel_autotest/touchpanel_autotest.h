/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef _TOUCHPANEL_AUTOTEST_H_
#define _TOUCHPANEL_AUTOTEST_H_

#include <linux/firmware.h>
#include <linux/syscalls.h>

#define Limit_MagicNum1     0x494D494C
#define Limit_MagicNum2     0x474D4954

#define Limit_MagicNum2_V2  0x32562D54
#define Limit_ItemMagic     0x4F50504F
#define Limit_ItemMagic_V2  0x4F504C53

#define DISABLE_ACTIVE_MODE	0
#define ENBLE_ACTIVE_MODE	1
#define NORMAL_TEST_MODE	2
#define AGING_TEST_MODE		3
#define MAX_LIMIT_DATA_LENGTH_COM         100

enum limit_type {
	LIMIT_TYPE_NO_DATA                 = 0x00,            /*means no limit data*/
	LIMIT_TYPE_CERTAIN_DATA            = 0x01,            /*means all nodes limit data is a certain data*/
	LIMIT_TYPE_EACH_NODE_DATA          = 0x02,            /*means all nodes have it's own limit*/
	LIMIT_TYPE_TX_RX_DATA              = 0x03,            /*means all nodes have it's own limit,format is : tx * rx,horizontal for rx data,vertical for tx data*/
	LIMIT_TYPE_SLEF_TX_RX_DATA         = 0x04,            /*means all nodes have it's own limit,format is : tx + rx*/
	LIMIT_TYPE_SLEF_TX_RX_DATA_DOUBLE  = 0x05,            /*means all nodes have it's own limit,format is : tx + tx + rx + rx*/
	/***************************** Novatek *********************************/
	LIMIT_TYPE_TOP_FLOOR_DATA          = 0x06,     /*means all nodes limit data is a certain data*/
	LIMIT_TYPE_DOZE_FDM_DATA           = 0x07,     /*means all nodes limit data is a certain data*/
	LIMIT_TYPE_TOP_FLOOR_RX_TX_DATA    = 0x08,     /*means all nodes limit data is a certain data*/
	LIMIT_TYPE_INVALID_DATA            = 0xFF,            /*means wrong limit data type*/
};

struct auto_test_header {
	uint32_t magic1;
	uint32_t magic2;
	uint64_t test_item;
};

struct auto_testdata {
	int tx_num;
	int rx_num;
	/*auto test*/
	void *fp;
	size_t length;
	ssize_t *pos;
	/*black screen*/
	void *bs_fp;
	size_t bs_length;
	ssize_t *bs_pos;
	int irq_gpio;
	int key_tx;
	int key_rx;
	uint64_t  tp_fw;
	uint64_t  dev_tp_fw;
	const struct firmware *fw;
	const struct firmware *fw_test;

	uint64_t test_item;
	char *test_list_log;
	int list_write_count;
};

struct auto_test_item_header {
	uint32_t    item_magic;
	uint32_t    item_size;
	uint16_t    item_bit;
	uint16_t    item_limit_type;
	uint32_t    top_limit_offset;
	uint32_t    floor_limit_offset;
	uint32_t    para_num;
};

/*get item information*/
struct test_item_info {
	uint32_t    item_magic;
	uint32_t    item_size;
	uint16_t    item_bit;
	uint16_t    item_limit_type;
	uint32_t    top_limit_offset;
	uint32_t    floor_limit_offset;
	uint32_t    para_num;                /*number of parameter*/
	int32_t     *p_buffer;               /*pointer to item parameter buffer*/
	uint32_t     item_offset;            /*item offset*/
};

uint32_t search_for_item_offset(const struct firmware *fw, int item_cnt,
				uint8_t item_index);
int32_t *getpara_for_item(const struct firmware *fw, uint8_t item_index,
			  uint32_t *para_num);
struct test_item_info *get_test_item_info(const struct firmware *fw,
		uint8_t item_index);
int save_test_result(struct auto_testdata *p_auto_testdata,
		     short  *data, int data_size, enum limit_type limit_type,
		     char  *limit_name);
ssize_t tp_test_write(void *data_start, size_t max_count,
		      const char *buf, size_t count, ssize_t *pos);

void tp_limit_read(struct seq_file *s, struct touchpanel_data *ts);
int tp_auto_test(struct seq_file *s, void *v);
int tp_black_screen_test(struct file *file, char __user *buffer, size_t count,
			 loff_t *ppos);
int tp_auto_test_result(struct seq_file *s, void *v);
int tp_black_screen_result(struct seq_file *s, void *v);

#endif /*_TOUCHPANEL_AUTOTEST_H_*/
