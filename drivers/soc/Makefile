# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the Linux Kernel SOC specific device drivers.
#

obj-$(CONFIG_ARCH_ACTIONS)	+= actions/
obj-$(CONFIG_SOC_ASPEED)	+= aspeed/
obj-$(CONFIG_ARCH_AT91)		+= atmel/
obj-y				+= bcm/
obj-$(CONFIG_ARCH_DOVE)		+= dove/
obj-$(CONFIG_MACH_DOVE)		+= dove/
obj-y				+= fsl/
obj-$(CONFIG_ARCH_GEMINI)	+= gemini/
obj-$(CONFIG_ARCH_MXC)		+= imx/
obj-$(CONFIG_ARCH_IXP4XX)	+= ixp4xx/
obj-$(CONFIG_SOC_XWAY)		+= lantiq/
obj-y				+= mediatek/
obj-y				+= amlogic/
obj-y				+= qcom/
obj-y				+= renesas/
obj-$(CONFIG_ARCH_ROCKCHIP)	+= rockchip/
obj-$(CONFIG_SOC_SAMSUNG)	+= samsung/
obj-y				+= sunxi/
obj-$(CONFIG_ARCH_TEGRA)	+= tegra/
obj-y				+= ti/
obj-$(CONFIG_ARCH_U8500)	+= ux500/
obj-$(CONFIG_PLAT_VERSATILE)	+= versatile/
obj-y				+= xilinx/
obj-$(CONFIG_ARCH_ZX)		+= zte/
#ifdef VENDOR_EDIT
obj-$(CONFIG_OPLUS_SYSTEM_KERNEL)	+= oplus/system/
#obj-y				+= oplus/device_info/
#ifdef OPLUS_FEATURE_SENSOR_SMEM
obj-y						+= oplus/sensor/
#endif
#endif /* VENDOR_EDIT */

#ifdef OPLUS_FEATURE_POWERINFO_STANDBY
obj-$(CONFIG_OPLUS_WAKELOCK_PROFILER) += oplus/oplus_wakelock/
#endif /* OPLUS_FEATURE_POWERINFO_STANDBY */

#ifdef OPLUS_FEATURE_TP_BASIC
obj-$(CONFIG_OPLUS_DEVICE_IFNO)  +=  oplus/device_info/
#endif /* OPLUS_FEATURE_TP_BASIC */

#ifdef OPLUS_FEATURE_THERMAL_HORAE
obj-$(CONFIG_HORAE_THERMAL_SHELL) += oplus/thermal/
#endif /* OPLUS_FEATURE_THERMAL_HORAE */

#ifdef OPLUS_FEATURE_ESIM
obj-y += oplus/misc/
#endif /* OPLUS_FEATURE_ESIM */
#ifdef OPLUS_FEATURE_HEALTHINFO
obj-$(CONFIG_OPLUS_HEALTHINFO)    +=  oplus/healthinfo/
#endif /* OPLUS_FEATURE_HEALTHINFO */
#ifdef OPLUS_FEATURE_MEMLEAK_DETECT
#Add for svelte logging
obj-$(CONFIG_SVELTE) +=  oplus/svelte/
#endif /* OPLUS_FEATURE_MEMLEAK_DETECT */
#ifdef OPLUS_FEATURE_LOWMEM_DBG
obj-$(CONFIG_OPLUS_FEATURE_LOWMEM_DBG) += oplus/lowmem_dbg/
#endif /* OPLUS_FEATURE_LOWMEM_DBG */

#ifdef OPLUS_FEATURE_IM
#ted.lin@optimization add for im module
obj-$(CONFIG_OPLUS_FEATURE_IM) += oplus/im/
#endif /* OPLUS_FEATURE_IM */

#ifdef OPLUS_FEATURE_MIDAS
obj-$(CONFIG_OPLUS_FEATURE_MIDAS)    += oplus/midas/
#endif

#ifdef OPLUS_FEATURE_CPUFREQ_BOUNCING
obj-$(CONFIG_OPLUS_FEATURE_CPUFREQ_BOUNCING) += oplus/cpufreq_bouncing/
#endif

#ifdef OPLUS_FEATURE_TPD
obj-$(CONFIG_OPLUS_FEATURE_TPD) += oplus/tpd/
#endif

#Add for NandSwap
obj-$(CONFIG_NANDSWAP)		+= oplus/oplus_nandswap/

#ifdef OPLUS_FEATURE_SUGOV_POWER_EFFIENCY
# Add for cpu effiency optimize by chengzhangtao on 2021/10/08
obj-$(CONFIG_OPLUS_FEATURE_SUGOV_POWER_EFFIENCY) += oplus/cpufreq_effiency/
#endif /* OPLUS_FEATURE_SUGOV_POWER_EFFIENCY */

#ifdef OPLUS_FEATURE_OCH
obj-$(CONFIG_OPLUS_FEATURE_OCH) += oplus/cpufreq_health/
#endif

#ifdef OPLUS_FEATURE_GAME_OPT
obj-$(CONFIG_OPLUS_FEATURE_GAME_OPT) += oplus/game_opt/
#endif /* OPLUS_FEATURE_GAME_OPT */