// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2020 Oplus. All rights reserved.
 */

#ifndef __OPLUS_CHG_STRATEGY_H__
#define __OPLUS_CHG_STRATEGY_H__

#include <linux/device.h>
#include <linux/of.h>

struct oplus_chg_strategy_data {
	int cool_temp;
	int heat_temp;
	int curr_data;
	int heat_next_index;
	int cool_next_index;
};

struct oplus_chg_strategy_temp_region {
	int index;
	int temp;
};

struct oplus_chg_strategy {
	struct oplus_chg_strategy_data *table_data;
	int table_size;
	int temp_min;
	int temp_max;
	struct oplus_chg_strategy_temp_region temp_region;
	bool initialized;
};

#define CHG_STRATEGY_DATA_TABLE_MAX 12

int read_chg_strategy_data_from_node(struct device_node *node,
				     const char *prop_str,
				     struct oplus_chg_strategy_data *ranges);
int oplus_chg_get_chg_strategy_data_len(struct oplus_chg_strategy_data data[],
					int max_len);
void oplus_chg_strategy_init(struct oplus_chg_strategy *strategy,
			     struct oplus_chg_strategy_data *data,
			     int data_size, int temp);
int oplus_chg_strategy_get_data(
	struct oplus_chg_strategy *strategy,
	struct oplus_chg_strategy_temp_region *temp_region, int temp);

#endif