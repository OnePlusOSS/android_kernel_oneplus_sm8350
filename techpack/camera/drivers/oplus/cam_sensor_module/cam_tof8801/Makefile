# SPDX-License-Identifier: GPL-2.0-only

ccflags-y += -Wno-unused-function
ccflags-y += -I$(srctree)/techpack/camera/drivers/cam_utils
ccflags-y += -I$(srctree)/techpack/camera/drivers/cam_cpas/include
ccflags-y += -I$(srctree)/techpack/camera/drivers/cam_sensor_module/cam_sensor_io
ccflags-y += -I$(srctree)/techpack/camera/drivers/cam_sensor_module/cam_sensor_utils
ccflags-y += -I$(srctree)/techpack/camera/drivers/cam_sensor_module/cam_cci
ccflags-y += -I$(srctree)/techpack/camera/drivers/cam_req_mgr
ccflags-y += -I$(srctree)/techpack/camera/drivers/cam_smmu
ccflags-y += -I$(srctree)/techpack/camera/drivers/cam_core
ccflags-y += -I$(srctree)/

obj-$(CONFIG_SPECTRA_OPLUS) += tof8801_pdrv.o \
	tof8801_driver.o \
	tof8801_bootloader.o \
	tof_hex_interpreter.o \
	tof8801_app0.o
