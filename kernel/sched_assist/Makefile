obj-y += sched_assist_common.o
obj-y += sched_assist_mutex.o
obj-y += sched_assist_rwsem.o
obj-y += sched_assist_futex.o
obj-y += sched_assist_workqueue.o

#qcom
ifeq ($(CONFIG_OPLUS_SYSTEM_KERNEL_QCOM), y)
ifeq ($(VERSION).$(PATCHLEVEL), 5.4)
obj-y += sched_assist_slide.o
endif
#mtk
else
	obj-y += sched_assist_slide_v1.o
endif
