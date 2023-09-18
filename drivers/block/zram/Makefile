# SPDX-License-Identifier: GPL-2.0-only
zram-y	:=	zcomp.o zram_drv.o

obj-$(CONFIG_ZRAM)	+=	zram.o
zram-$(CONFIG_HYBRIDSWAP) += hybridswap/hybridmain.o
zram-$(CONFIG_HYBRIDSWAP_SWAPD) += hybridswap/hybridswapd.o
zram-$(CONFIG_HYBRIDSWAP_CORE) += hybridswap/hybridswap.o
