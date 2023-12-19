#/************************************************************************************
#** File: - Makefile
#** Copyright (C), 2008-2020, OPLUS Mobile Comm Corp., Ltd
#**
#** Description:
#**      1. Add for routerboost
#**
#** Version: 1.0
#** Date :   2021-04-09
#** TAG  :   OPLUS_FEATURE_WIFI_ROUTERBOOST
#**
#** ---------------------Revision History: ---------------------
#**  <author>                      <data>     <version >   <desc>
#** ---------------------------------------------------------------
#**
#************************************************************************************/

#
# Makefile for the netfilter modules on top of IPv4.
#
KBUILD_OPTIONS += CONFIG_OPLUS_FEATURE_WIFI_ROUTERBOOST=m
KBUILD_OPTIONS += OPLUS_OUT_OF_TREE_KO=y

KERNEL_SRC ?= /lib/modules/$(shell uname -r)/build
M ?= $(shell pwd)

modules modules_install clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) $(KBUILD_OPTIONS) W=1 $(@)
