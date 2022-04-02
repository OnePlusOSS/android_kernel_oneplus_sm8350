// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef _KERNEL_FW_UDPATE_H_
#define _KERNEL_FW_UDPATE_H_

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/firmware.h>

int request_firmware_select(const struct firmware **firmware_p, const char *name,
			    struct device *device);
#endif /*_KERNEL_FW_UDPATE_H_*/
