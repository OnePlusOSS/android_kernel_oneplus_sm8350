#
# Makefile for the touchscreen drivers.
#
#obj-$(CONFIG_TOUCHSCREEN) += samsung/
#obj-$(CONFIG_TOUCHSCREEN) += util_interface/
#obj-$(CONFIG_TOUCHSCREEN) += touchpanel_common_driver.o
#obj-$(CONFIG_TOUCHSCREEN) += synaptics/

touchscreen-objs += \
	samsung/s6sy792/sec_drivers_s6sy792.o \
	samsung/sec_common.o \
	util_interface/touch_interfaces.o \
	synaptics/syna_tcm_oncell/synaptics_tcm_oncell.o \
	synaptics/syna_tcm_oncell/synaptics_tcm_device.o \
	synaptics/syna_tcm_oncell/synaptics_tcm_recovery.o \
	synaptics/synaptics_common.o \
	synaptics/synaptics_touch_panel_remote.o \
	tp_module_init.o \
	touchpanel_common_driver.o

obj-$(CONFIG_TOUCHSCREEN)+=touchscreen.o
