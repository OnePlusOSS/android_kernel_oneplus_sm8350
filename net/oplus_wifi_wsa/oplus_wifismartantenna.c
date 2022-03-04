/******************************************************************************
** Copyright (C), 2019-2029, OPLUS Mobile Comm Corp., Ltd
** All rights reserved.
** File: - oplus_wifismartantenna.c
** Description: wifismartantenna.c (wsa)
**
** Version: 1.0
** Date : 2020/08/14
** Author: XuFenghua
** TAG: OPLUS_FEATURE_WIFI_SMARTANTENNA
** ------------------------------- Revision History: ----------------------------
** <author>		<data>		<version>	<desc>
** ------------------------------------------------------------------------------
** XuFenghua		2020/08/14	1.0		OPLUS_FEATURE_WIFI_SMARTANTENNA
** bingham.fang		2021/06/11	1.1
 *******************************************************************************/

#include <linux/types.h>
#include <linux/ip.h>
#include <linux/netfilter.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/icmp.h>
#include <linux/sysctl.h>
#include <net/route.h>
#include <net/ip.h>
#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/version.h>
#include <net/tcp.h>
#include <linux/random.h>
#include <net/sock.h>
#include <net/dst.h>
#include <linux/file.h>
#include <net/tcp_states.h>
#include <linux/netlink.h>
#include <net/sch_generic.h>
#include <net/pkt_sched.h>
#include <net/netfilter/nf_queue.h>
#include <linux/netfilter/xt_state.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_owner.h>
#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_core.h>
#include <net/netfilter/ipv4/nf_conntrack_ipv4.h>
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pinctrl/consumer.h>
#include <linux/regulator/consumer.h>


#define LOG_TAG "[oplus_wsa] %s line:%d "
#define debug(fmt, args...) printk(LOG_TAG fmt, __FUNCTION__, __LINE__, ##args)

#define WLAN_WSA_GPIO		"wlan-wsa-gpio"

/* gpio number and gpio level*/
static long oplus_wsa_gpiolevel;

struct pinctrl *wsa_pinctl;
struct pinctrl_state *pinctrl_state_high;
struct pinctrl_state *pinctrl_state_low;
struct pinctrl_state *pa_pinctrl_state_high;
struct pinctrl_state *pa_pinctrl_state_low;

struct regulator *vdd_reg;

static struct sock *oplus_wsa_sock;

enum {
	OPLUS_WSA_LOW = 0x13,
	OPLUS_WSA_HIGH = 0x14,
};

static int oplus_switch_to_secondary_antenna()
{

	if (!wsa_pinctl || !pinctrl_state_high || !pa_pinctrl_state_high || !pa_pinctrl_state_low) {
		debug("pinctrl is NULL");
		return -ENOENT;
	}

	debug("%s is high", __func__);
	/* pa switch need to be toggle to 50ohm */
	pinctrl_select_state(wsa_pinctl, pa_pinctrl_state_high);

	/* delay 1 us to wait pa switch */
	udelay(1);

	/* toggle antenna switch to secondary antenna */
	pinctrl_select_state(wsa_pinctl, pinctrl_state_high);

	/* delay 1 us to wait antenna switch */
	udelay(1);

	/* turn pa switch to the path */
	pinctrl_select_state(wsa_pinctl, pa_pinctrl_state_low);

	return 0;
}

static int oplus_switch_to_prime_antenna()
{

	if (!wsa_pinctl || !pinctrl_state_low || !pa_pinctrl_state_high || !pa_pinctrl_state_low) {
		debug("pinctrl is NULL");
		return -ENOENT;
	}

	debug("%s is low", __func__);
	/* pa switch need to be toggle to 50ohm */
	pinctrl_select_state(wsa_pinctl, pa_pinctrl_state_high);

	/* delay 1 us to wait pa switch */
	udelay(1);

	/* toggle antenna switch to prime antenna */
	pinctrl_select_state(wsa_pinctl, pinctrl_state_low);

	/* delay 1 us to wait antenna switch */
	udelay(1);

	/* turn pa switch to the path */
	pinctrl_select_state(wsa_pinctl, pa_pinctrl_state_low);

	return 0;

}

//===platform device start===
/* register for /sys/module/oplus_wifismartantenna/parameters/oplus_wsa */
static int oplus_wsa_sysmodule_ops_set(const char *kmessage, const struct kernel_param *kp)
{
	int len, i;
	char *p;
	if (!kmessage) {
		debug("%s error: kmessage == null!", __func__);
		return -1;
	}
	debug("%s: %s", __func__, kmessage);

	if (kstrtol(kmessage, 10, &oplus_wsa_gpiolevel) ||
			(oplus_wsa_gpiolevel != 0 && oplus_wsa_gpiolevel != 1)) {
		debug("%s error: gpiolevel parsing error!", __func__);
		return -1;
	}

	debug("%s level = %d", __func__, oplus_wsa_gpiolevel);

	if (!wsa_pinctl) {
		debug("pinctrl is NULL");
		return -1;
	}

	if (oplus_wsa_gpiolevel == 1) {
		oplus_switch_to_secondary_antenna();
	} else {
		oplus_switch_to_prime_antenna();
	}

	return 0;
}

static const struct kernel_param_ops oplus_wsa_sysmodule_ops = {
	.set = oplus_wsa_sysmodule_ops_set,
	.get = param_get_int,
};

module_param_cb(oplus_wsa, &oplus_wsa_sysmodule_ops, &oplus_wsa_gpiolevel,
	S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

static int oplus_wsa_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct device *dev;

	debug("%s: enter\n", __func__);

	dev = &pdev->dev;
	if (dev->of_node == NULL) {
		debug("%s: Can't find compatible node in device tree\n", __func__);
		return -ENOENT;
	}

	wsa_pinctl = devm_pinctrl_get(dev);
	if (IS_ERR_OR_NULL(wsa_pinctl)) {
		ret = PTR_ERR(wsa_pinctl);
		debug("%s: Failed to get pinctrl, err = %d\n", __func__, ret);
		wsa_pinctl = NULL;
		goto out;
	}

	pinctrl_state_high = pinctrl_lookup_state(wsa_pinctl, "cnss_wlan_wsa_high");
	if (IS_ERR_OR_NULL(pinctrl_state_high)) {
		ret = PTR_ERR(pinctrl_state_high);
		debug("%s: Fail to get wsa pinctrl high state, ret=%d", __func__, ret);
		pinctrl_state_high = NULL;
		goto out;
	}

	pinctrl_state_low = pinctrl_lookup_state(wsa_pinctl, "cnss_wlan_wsa_low");
	if (IS_ERR_OR_NULL(pinctrl_state_low)) {
		ret = PTR_ERR(pinctrl_state_low);
		debug("%s: Fail to get wsa pinctrl low state, ret=%d", __func__, ret);
		pinctrl_state_low = NULL;
		goto out;
	}

	pa_pinctrl_state_high = pinctrl_lookup_state(wsa_pinctl, "cnss_pa_switch_high");
	if (IS_ERR_OR_NULL(pa_pinctrl_state_high)) {
		ret = PTR_ERR(pa_pinctrl_state_high);
		debug("%s: Fail to get pa switch pinctrl high state, ret=%d", __func__, ret);
		pa_pinctrl_state_high = NULL;
		goto out;
	}

	pa_pinctrl_state_low = pinctrl_lookup_state(wsa_pinctl, "cnss_pa_switch_low");
	if (IS_ERR_OR_NULL(pa_pinctrl_state_low)) {
		ret = PTR_ERR(pa_pinctrl_state_low);
		debug("%s: Fail to get pa switch pinctrl low state, ret=%d", __func__, ret);
		pa_pinctrl_state_low = NULL;
		goto out;
	}

	if (of_find_property(dev->of_node, "vdd-supply", NULL))
	{
		vdd_reg = devm_regulator_get(dev, "vdd");
		if (IS_ERR(vdd_reg)) {
			ret = PTR_ERR(vdd_reg);
			debug("%s: Fail to get vdd-supply, ret=%d", __func__, ret);
		}
	}

	if (vdd_reg) {
		ret = regulator_enable(vdd_reg);
		if (ret < 0) {
			debug("%s: Fail to enable vdd-supply, ret=%d", __func__, ret);
		}
	}

	debug("%s init as low state!", __func__);
	pinctrl_select_state(wsa_pinctl, pinctrl_state_low);
	return 0;
out:
	return ret;
}

static int oplus_wsa_remove(struct platform_device *pdev)
{
	if (wsa_pinctl) {
		devm_pinctrl_put(wsa_pinctl);
	}

	if (vdd_reg) {
		int ret = regulator_disable(vdd_reg);
		if (ret < 0) {
			debug("%s: Fail to disable vdd-supply, ret=%d", __func__, ret);
		}
	}

	return 0;
}

static const struct of_device_id oplus_wsa_dt_ids[] = {
	{ .compatible = "oplus,wlan-wsa" },
	{},
};
MODULE_DEVICE_TABLE(of, oplus_wsa_dt_ids);


static struct platform_driver oplus_wsa_driver = {
	.probe = oplus_wsa_probe,
	.remove = oplus_wsa_remove,
	.driver = {
		.name = "oplus_wsa",
		.of_match_table = of_match_ptr(oplus_wsa_dt_ids),
	},
};
//===platform device end===
//===netlinke part start===
static int oplus_wsa_netlink_rcv_msg(struct sk_buff *skb, struct nlmsghdr *nlh,
			struct netlink_ext_ack *extack)
{
	int ret = 0;

	debug("nlh->nlmsg_type: %d", nlh->nlmsg_type);
	switch (nlh->nlmsg_type) {
		case OPLUS_WSA_HIGH:
			ret = oplus_switch_to_secondary_antenna();
			break;
		case OPLUS_WSA_LOW:
			ret = oplus_switch_to_prime_antenna();
			break;
		default:
			return -EINVAL;
	}

	return ret;
}

static void oplus_wsa_netlink_rcv(struct sk_buff *skb)
{
	debug("%s", __func__);
	netlink_rcv_skb(skb, &oplus_wsa_netlink_rcv_msg);
}

static int oplus_wsa_netlink_init(void)
{
	struct netlink_kernel_cfg cfg = {
		.input = oplus_wsa_netlink_rcv,
	};
	oplus_wsa_sock = netlink_kernel_create(&init_net, NETLINK_OPLUS_WSA, &cfg);
	return !oplus_wsa_sock ? -ENOMEM : 0;
}

static void oplus_wsa_netlink_deinit(void)
{
	netlink_kernel_release(oplus_wsa_sock);
	oplus_wsa_sock = NULL;
}
//===netlinke part end===

//===module part start===
static int __init oplus_wsa_init(void)
{
	int ret = 0;

	platform_driver_register(&oplus_wsa_driver);
	if (oplus_wsa_netlink_init()) {
		debug("%s: cannot init oplus wsa netlink");
	}
	return ret;
}

static void __exit oplus_wsa_fini(void)
{
	platform_driver_unregister(&oplus_wsa_driver);
	oplus_wsa_netlink_deinit();
}

module_init(oplus_wsa_init);
module_exit(oplus_wsa_fini);
