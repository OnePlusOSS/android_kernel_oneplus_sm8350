ifeq ($(CONFIG_BUILD_ARM64_DT_OVERLAY),y)
        dtbo-$(CONFIG_ARCH_LAHAINA) += \
        martini-20820-lahaina-overlay_T0.dtbo \
        martini-20820-lahaina-overlay.dtbo \
        martini-20820-lahaina-v2.1-overlay.dtbo \
        martini-20820-lahaina-v2.1-overlay_T0.dtbo

martini-20820-lahaina-overlay_T0.dtbo-base := martini-lahaina.dtb martini-lahaina-v2.dtb martini-lahaina-v2.1.dtb
martini-20820-lahaina-overlay.dtbo-base := martini-lahaina.dtb martini-lahaina-v2.dtb martini-lahaina-v2.1.dtb
martini-20820-lahaina-v2.1-overlay.dtbo-base := martini-lahaina.dtb martini-lahaina-v2.dtb martini-lahaina-v2.1.dtb
martini-20820-lahaina-v2.1-overlay_T0.dtbo-base := martini-lahaina.dtb martini-lahaina-v2.dtb martini-lahaina-v2.1.dtb
endif

always		:= $(dtb-y)
subdir-y	:= $(dts-dirs)
clean-files	:= *.dtb *.dtbo
