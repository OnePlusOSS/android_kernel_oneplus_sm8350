// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include <linux/spinlock.h>
#include <linux/interrupt.h>

#include "touch_comon_api.h"

unsigned int tp_debug = 0;
EXPORT_SYMBOL(tp_debug);

void tp_disable_irq(struct device *dev, unsigned int irq)
{
	unsigned long irqflags = 0;
	struct touchpanel_data *ts = dev_get_drvdata(dev);
	struct com_api_data *com_api_data = &ts->com_api_data;

	spin_lock_irqsave(&com_api_data->tp_irq_lock, irqflags);

	if (!com_api_data->tp_irq_disable) {
		disable_irq_nosync(irq);
		com_api_data->tp_irq_disable = 1;
	}

	spin_unlock_irqrestore(&com_api_data->tp_irq_lock, irqflags);
}

void tp_enable_irq(struct device *dev, unsigned int irq)
{
	unsigned long irqflags = 0;
	struct touchpanel_data *ts = dev_get_drvdata(dev);
	struct com_api_data *com_api_data = &ts->com_api_data;

	spin_lock_irqsave(&com_api_data->tp_irq_lock, irqflags);

	if (com_api_data->tp_irq_disable) {
		enable_irq(irq);
		com_api_data->tp_irq_disable = 0;
	}

	spin_unlock_irqrestore(&com_api_data->tp_irq_lock, irqflags);
}
