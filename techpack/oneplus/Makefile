#ifeq ($(CONFIG_ARCH_LAHAINA), y)

include $(srctree)/techpack/oneplus/config/oneplus.conf
LINUXINCLUDE    += -include $(srctree)/techpack/oneplus/config/oneplus.h
LINUXINCLUDE    += \
                   -I$(srctree)/techpack/oneplus/include
USERINCLUDE     += -I$(srctree)/techpack/oneplus/include

ifeq ($(CONFIG_QGKI),y)
include $(srctree)/techpack/oneplus/config/coretech.conf
LINUXINCLUDE    += -include $(srctree)/techpack/oneplus/config/coretech.h
endif

obj-y += input/
obj-y += misc/
ifeq ($(CONFIG_QGKI),y)
obj-y += coretech/
endif
obj-y += fs/
obj-y += kernel/
obj-$(CONFIG_TRI_STATE_KEY) += tri_state_key/
obj-y += power/
obj-y += oneplus_healthinfo/
obj-y += mm/
obj-y += opslalib/
obj-$(CONFIG_SLABTRACE_DEBUG) += slabtrace/
ifeq ($(CONFIG_DETECT_HUNG_TASK), y)
obj-$(CONFIG_HUNG_TASK_ENHANCE) += hung_task_enhance/
endif
obj-$(CONFIG_AW8697_HAPTIC) += vibrator/
#endif

$(warning "techpack USERINCLUDE  " $(USERINCLUDE))
$(warning "techpack LINUXINCLUDE " $(LINUXINCLUDE))
