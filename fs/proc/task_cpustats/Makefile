#ifdef OPLUS_FEATURE_TASK_CPUSTATS

ifeq ($(CONFIG_OPLUS_SYSTEM_KERNEL_QCOM), y)
obj-$(CONFIG_OPLUS_CTP) += task_cpustats.o
else
ifeq ($(VERSION).$(PATCHLEVEL), 4.14)
obj-$(CONFIG_OPLUS_CTP) += task_cpustats_mtk_v414.o
else
obj-$(CONFIG_OPLUS_CTP) += task_cpustats.o
endif
endif

obj-$(CONFIG_OPLUS_SCHED) += task_sched_info.o
obj-$(CONFIG_UTILS_MONITOR) += task_load.o
#endif /* OPLUS_FEATURE_TASK_CPUSTATS */
