#
# Makefile for the touchscreen drivers.
#
subdir-ccflags-$(CONFIG_TOUCHPANEL_MTK_PLATFORM) += -I$(srctree)/drivers/misc/mediatek/include/
subdir-ccflags-$(CONFIG_OPLUS_MTK_DRM_GKI_NOTIFY) += -I$(srctree)/drivers/misc/mediatek/include/


oplus_bsp_tp_comon-y := util_interface/touch_interfaces.o
oplus_bsp_tp_comon-y += touch_comon_api/touch_comon_api.o
oplus_bsp_tp_comon-y += touchpanel_autotest/touchpanel_autotest.o

oplus_bsp_tp_comon-y += touchpanel_common_driver.o
oplus_bsp_tp_comon-y += touchpanel_proc.o

obj-$(CONFIG_TOUCHPANEL_OPLUS) += oplus_bsp_tp_comon.o

oplus_bsp_tp_notify-y += touchpanel_notify/touchpanel_event_notify.o
obj-$(CONFIG_TOUCHPANEL_NOTIFY) += oplus_bsp_tp_notify.o

obj-$(CONFIG_TOUCHPANEL_SYNAPTICS) += Synaptics/
obj-$(CONFIG_TOUCHPANEL_SAMSUNG) += Samsung/
