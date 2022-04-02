ifeq ($(CONFIG_BUILD_ARM64_DT_OVERLAY),y)
dtbo-$(CONFIG_ARCH_LAHAINA) += \
            lemonadep-19815-overlay.dtbo \
            lemonadep-19815-overlay-t0.dtbo \
            lemonade-19825-overlay.dtbo \
            lemonadev-2080a-overlay.dtbo

lemonadep-19815-overlay.dtbo-base := lahaina.dtb lahaina-v2.dtb lahaina-v2.1.dtb
lemonadep-19815-overlay-t0.dtbo-base := lahaina.dtb lahaina-v2.dtb lahaina-v2.1.dtb
lemonade-19825-overlay.dtbo-base := lahaina.dtb lahaina-v2.dtb lahaina-v2.1.dtb
lemonadev-2080a-overlay.dtbo-base := lahaina.dtb lahaina-v2.dtb lahaina-v2.1.dtb
else
dtb-$(CONFIG_ARCH_LAHAINA) +=
        lemonadep-overlay.dtb
endif

always          := $(dtb-y)
subdir-y        := $(dts-dirs)
clean-files     := *.dtb *.dtbo

