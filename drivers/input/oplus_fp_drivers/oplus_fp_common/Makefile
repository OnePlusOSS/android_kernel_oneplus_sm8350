obj-$(CONFIG_OPLUS_FINGERPRINT_COMMON)     +=  oplus_fp_common.o

### Platform marco
ifeq ($(CONFIG_OPLUS_FINGERPRINT_QCOM),y)
ccflags-y += -DQCOM_PLATFORM
else ifeq ($(CONFIG_OPLUS_FINGERPRINT_MTK),y)
ccflags-y += -DMTK_PLATFORM
endif
