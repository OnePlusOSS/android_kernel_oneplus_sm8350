// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015-2017, 2019 The Linux Foundation. All rights reserved.
 */

#define pr_fmt(fmt) "OPLUS_CHG[CFG]: %s[%d]: " fmt, __func__, __LINE__

#include <linux/errno.h>
#include <crypto/hash.h>
#include <crypto/skcipher.h>
#include <linux/crypto.h>
#include "oplus_chg_comm.h"
#include "oplus_chg_wls.h"
#include "oplus_chg_cfg.h"

// static char *public_key = "zrZiJ1+Mvphg+OHdttQCChLilekc0h4Wm7gsdqA4vXbFxguexN8Zo9eU2wK/N83H6yMrZi8R+c+vmKBn6wzTe02PIYgp82RxI/z8kIyb4zsc4zJ1oMN6RAxTjKAAZliUIMGA2oSua2SXHMwB3/dftTw1lBoHP4Cwb7I8LtmSDO0";

struct sdesc {
	struct shash_desc shash;
	char ctx[];
};

static struct sdesc *init_sdesc(struct crypto_shash *alg)
{
	struct sdesc *sdesc;
	int size;

	size = sizeof(struct shash_desc) + crypto_shash_descsize(alg);
	sdesc = kmalloc(size, GFP_KERNEL);
	if (!sdesc)
		return ERR_PTR(-ENOMEM);
	sdesc->shash.tfm = alg;
	return sdesc;
}

static int calc_hash(struct crypto_shash *alg, const unsigned char *data,
		     unsigned int datalen, unsigned char *digest)
{
	struct sdesc *sdesc;
	int ret;

	sdesc = init_sdesc(alg);
	if (IS_ERR(sdesc)) {
		pr_info("can't alloc sdesc\n");
		return PTR_ERR(sdesc);
	}

	ret = crypto_shash_digest(&sdesc->shash, data, datalen, digest);
	kfree(sdesc);
	return ret;
}

int oplus_chg_check_cfg_data(void *buf)
{
	struct oplus_chg_cfg_head *cfg_head;
	struct oplus_chg_param_head *param_head;
	struct crypto_shash *alg;
	char *hash_alg_name = "sha256";
	unsigned char digest[32];
	int rc;

	if (buf == NULL) {
		pr_err("data buf is null\n");
		return -EINVAL;
	}

	cfg_head = (struct oplus_chg_cfg_head *)buf;

	if (cfg_head->magic != OPLUS_CHG_CFG_MAGIC ||
	    cfg_head->head_size != sizeof(struct oplus_chg_cfg_head)) {
		return -EINVAL;
	}
	param_head = (struct oplus_chg_param_head *)((unsigned char *)buf + cfg_head->param_index[OPLUS_CHG_USB_PARAM]);
	if (param_head->size != 0) {
		pr_err("usb charge parameter length error, len=%d\n", param_head->size);
		return -EINVAL;
	}
	param_head = (struct oplus_chg_param_head *)((unsigned char *)buf + cfg_head->param_index[OPLUS_CHG_WLS_PARAM]);
	if (param_head->size != sizeof(struct oplus_chg_wls_dynamic_config)) {
		pr_err("wireless charge parameter length error, len=%d\n", param_head->size);
		return -EINVAL;
	}
	param_head = (struct oplus_chg_param_head *)((unsigned char *)buf + cfg_head->param_index[OPLUS_CHG_COMM_PARAM]);
	if (param_head->size != sizeof(struct oplus_chg_comm_config)) {
		pr_err("common parameter length error, len=%d\n", param_head->size);
		return -EINVAL;
	}
	param_head = (struct oplus_chg_param_head *)((unsigned char *)buf + cfg_head->param_index[OPLUS_CHG_BATT_PARAM]);
	if (param_head->size != 0) {
		pr_err("battery parameter length error, len=%d\n", param_head->size);
		return -EINVAL;
	}

	alg = crypto_alloc_shash(hash_alg_name, 0, 0);
	if (IS_ERR(alg)) {
		pr_info("can't alloc alg %s\n", hash_alg_name);
		return PTR_ERR(alg);
	}
	rc = calc_hash(alg, (unsigned char *)buf + cfg_head->head_size, cfg_head->size, digest);

	crypto_free_shash(alg);

	return 0;
}

void *oplus_chg_get_param(void *buf, enum oplus_chg_param_type type)
{
	struct oplus_chg_cfg_head *cfg_head;
	struct oplus_chg_param_head *param_head;

	cfg_head = (struct oplus_chg_cfg_head *)buf;
	param_head = (struct oplus_chg_param_head *)((unsigned char *)buf + cfg_head->param_index[type]);

	if (param_head->magic != OPLUS_CHG_CFG_MAGIC)
		return NULL;
	if (param_head->size == 0)
		return NULL;
	if (param_head->type != type)
		return NULL;

	switch (type) {
	case OPLUS_CHG_USB_PARAM:
		if (param_head->size != 0) {
			pr_err("usb charge parameter length error, len=%d\n", param_head->size);
			return NULL;
		}
		break;
	case OPLUS_CHG_WLS_PARAM:
		if (param_head->size != sizeof(struct oplus_chg_wls_dynamic_config)) {
			pr_err("wireless charge parameter length error, len=%d\n", param_head->size);
			return NULL;
		}
		break;
	case OPLUS_CHG_COMM_PARAM:
		if (param_head->size != sizeof(struct oplus_chg_comm_config)) {
			pr_err("common parameter length error, len=%d\n", param_head->size);
			return NULL;
		}
		break;
	case OPLUS_CHG_BATT_PARAM:
		if (param_head->size != 0) {
			pr_err("battery parameter length error, len=%d\n", param_head->size);
			return NULL;
		}
		break;
	case OPLUS_CHG_PARAM_MAX:
		return NULL;
	}

	return (void *)param_head->data;
}

int load_word_val_by_buf(u8 *buf, int index, int *val)
{
	*val = le32_to_cpu(*((int *)(buf + index)));
	return index + 4;
}
