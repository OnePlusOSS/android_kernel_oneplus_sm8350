ifeq ($(CONFIG_OPLUS_CHARGER_MTK),y)

	ifeq ($(CONFIG_MTK_HAFG_20),y)
		obj-y	+= oplus_battery_mtk_fg_20.o
	else ifeq ($(CONFIG_OPLUS_CHARGER_MTK6763),y)
		obj-y	+= oplus_battery_mtk6763R.o
	else ifeq ($(CONFIG_OPLUS_CHARGER_MTK6771),y)
		obj-y	+= oplus_bq24190.o
		obj-y	+= oplus_bq25890h.o
		obj-y   += oplus_bq25601d.o
		obj-y   += smb1351-charger.o
	else ifeq ($(CONFIG_OPLUS_CHARGER_MTK6769),y)
		MTK_PLATFORM := $(subst ",,$(CONFIG_MTK_PLATFORM))
		subdir-ccflags-y += -Werror
		subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/include
		subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat/$(MTK_PLATFORM)/include
		subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/base/power/include
		subdir-ccflags-$(CONFIG_USB_MU3D_DRV) += -I$(srctree)/drivers/misc/mediatek/mu3d/drv
		subdir-ccflags-$(CONFIG_USB_MTK_HDRC) += -I$(srctree)/drivers/misc/mediatek/usb20/
		subdir-ccflags-$(CONFIG_MTK_PMIC_CHIP_MT6355) += -I$(srctree)/drivers/misc/mediatek/pmic/include/mt6355
		subdir-ccflags-$(CONFIG_MTK_PMIC_CHIP_MT6356) += -I$(srctree)/drivers/misc/mediatek/pmic/include/mt6356
		subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/pmic/include
		subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/typec/tcpc/inc
		subdir-ccflags-y += -I$(srctree)/drivers/power/supply/mediatek/misc/
		subdir-ccflags-y += -I$(srctree)/drivers/power/supply/mediatek/battery/
		obj-$(CONFIG_CHARGER_BQ2589X) += oplus_bq2589x_charger.o
		obj-$(CONFIG_CHARGER_BQ2591X) += oplus_bq2591x_charger.o
		obj-y	+= oplus_battery_mtk6769.o
	else ifeq ($(CONFIG_OPLUS_CHARGER_MTK6779),y)
		MTK_PLATFORM := $(subst ",,$(CONFIG_MTK_PLATFORM))
		subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat/$(MTK_PLATFORM)/include
		subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/pmic/include
		subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/typec/tcpc/inc
		subdir-ccflags-y += -I$(srctree)/drivers/power/supply/mediatek/misc/
		obj-y	+= oplus_battery_mtk6779.o
	else ifeq ($(CONFIG_MACH_MT6785),y)
		MTK_PLATFORM := $(subst ",,$(CONFIG_MTK_PLATFORM))
		subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat/$(MTK_PLATFORM)/include
		subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/pmic/include
		subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/typec/tcpc/inc
		subdir-ccflags-y += -I$(srctree)/drivers/power/supply/mediatek/misc/
		subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/thermal/$(MTK_PLATFORM)/inc
		obj-y	+= oplus_mp2650.o
		obj-y	+= oplus_da9313.o
		obj-y   += oplus_usbtemp.o
		obj-y   += oplus_battery_mtk6785.o
	else ifeq ($(CONFIG_OPLUS_CHARGER_MTK6779Q),y)
		MTK_PLATFORM := $(subst ",,$(CONFIG_MTK_PLATFORM))
		subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat/$(MTK_PLATFORM)/include
		subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/pmic/include
		subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/typec/tcpc/inc
		subdir-ccflags-y += -I$(srctree)/drivers/power/supply/mediatek/misc/
		obj-y	+= oplus_battery_mtk6779Q.o
		obj-y	+= oplus_bq25910.o
	else ifeq ($(CONFIG_OPLUS_CHARGER_MTK6885),y)
                MTK_PLATFORM := $(subst ",,$(CONFIG_MTK_PLATFORM))
                subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat/$(MTK_PLATFORM)/include
                subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/pmic/include
                subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/typec/tcpc/inc
                subdir-ccflags-y += -I$(srctree)/drivers/power/supply/mediatek/misc/
		obj-y   += oplus_battery_mtk6885R.o
	else ifeq ($(CONFIG_OPLUS_CHARGER_MTK6889),y)
		MTK_PLATFORM := $(subst ",,$(CONFIG_MTK_PLATFORM))
		subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat/$(MTK_PLATFORM)/include
		subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/pmic/include
		subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/typec/tcpc/inc
		subdir-ccflags-y += -I$(srctree)/drivers/power/supply/mediatek/misc/
		obj-y	+= oplus_mp2650.o
		obj-y   += oplus_da9313.o
		obj-y   += oplus_usbtemp.o
		obj-y   += oplus_battery_mtk6889R.o
	else ifeq ($(CONFIG_OPLUS_CHARGER_MTK6873),y)
                MTK_PLATFORM := $(subst ",,$(CONFIG_MTK_PLATFORM))
                subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat/$(MTK_PLATFORM)/include
                subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/pmic/include
                subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/typec/tcpc/inc
                subdir-ccflags-y += -I$(srctree)/drivers/power/supply/mediatek/misc/
		obj-y   += oplus_battery_mtk6873R.o
		obj-y	+= oplus_bq25601d.o
	else ifeq ($(CONFIG_OPLUS_CHARGER_MTK6853),y)
		MTK_PLATFORM := $(subst ",,$(CONFIG_MTK_PLATFORM))
		subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat/$(MTK_PLATFORM)/include
		subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat
		subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/pmic/include
		subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/typec/tcpc/inc
		subdir-ccflags-y += -I$(srctree)/drivers/power/supply/mediatek/misc/
		obj-y   += oplus_battery_mtk6853R.o
		obj-y	+= oplus_bq25910.o
		obj-y	+= oplus_mp2650.o
		obj-y   += oplus_da9313.o
		obj-y	+= oplus_max77932b.o
	else
		//obj-y	+= oplus_battery_mtk.o
	endif
	//obj-y	+= oplus_bq24196.o

else ifeq ($(CONFIG_OPLUS_MSM8953N_CHARGER),y)
	obj-y	+= oplus_battery_msm8953_N.o
else ifeq ($(CONFIG_OPLUS_MSM8953_CHARGER),y)
	obj-y	+= oplus_battery_msm8953.o
else ifeq ($(CONFIG_OPLUS_MSM8998_CHARGER),y)
	obj-y	+= oplus_battery_msm8998.o
else ifeq ($(CONFIG_OPLUS_MSM8998O_CHARGER),y)
	obj-y	+= oplus_battery_msm8998_O.o
else ifeq ($(CONFIG_OPLUS_SDM845_CHARGER),y)
	obj-y	+= oplus_battery_sdm845.o
	obj-y	+= oplus_bq25882.o oplus_da9313.o
else ifeq ($(CONFIG_OPLUS_SDM670_CHARGER),y)
	obj-y	+= oplus_battery_sdm670.o
	obj-y	+= oplus_bq25882.o oplus_da9313.o
else ifeq ($(CONFIG_OPLUS_SDM670P_CHARGER),y)
	obj-y	+= oplus_battery_sdm670P.o
	obj-y	+= oplus_bq25882.o oplus_da9313.o
else ifeq ($(CONFIG_OPLUS_SDM670Q_CHARGER),y)
	obj-y	+= oplus_battery_sdm670Q.o
	obj-y	+= oplus_bq25882.o oplus_da9313.o
else ifeq ($(CONFIG_OPLUS_SM8150_CHARGER),y)
	obj-y	+= oplus_bq25882.o
	obj-y	+= oplus_da9313.o
	obj-y	+= oplus_battery_msm8150.o
else ifeq ($(CONFIG_OPLUS_SM8150_PRO_CHARGER),y)
	obj-y	+= oplus_mp2650.o
	obj-y	+= oplus_da9313.o
	obj-y	+= oplus_battery_msm8150_pro.o
else ifeq ($(CONFIG_OPLUS_SM6125_CHARGER),y)
	obj-y   += oplus_battery_sm6125P.o
else ifeq ($(CONFIG_OPLUS_SM7150_CHARGER),y)
	obj-y   += oplus_battery_sm7150_P.o
else ifeq ($(CONFIG_OPLUS_SM7250_CHARGER),y)
	//obj-y	+= oplus_bq25882.o
	obj-y	+= oplus_da9313.o
	obj-y   += oplus_battery_msm7250_Q.o
	obj-y	+= oplus_mp2650.o
else ifeq ($(CONFIG_OPLUS_SM7250R_CHARGER),y)
	obj-y	+= oplus_da9313.o
	obj-y   += oplus_battery_msm7250_R.o
	obj-y	+= oplus_mp2650.o
	obj-y   += oplus_usbtemp.o
else ifeq ($(CONFIG_OPLUS_SM8250_CHARGER),y)
	obj-y	+= oplus_da9313.o
	obj-y	+= oplus_battery_msm8250.o
	obj-y	+= oplus_mp2650.o
	obj-y   += oplus_usbtemp.o

else ifeq ($(CONFIG_OPLUS_SM8150R_CHARGER),y)
	obj-y	+= oplus_da9313.o
	obj-y	+= oplus_mp2650.o
	obj-y	+= oplus_battery_msm8150Q.o
else
	obj-y	+= oplus_battery_msm8976.o
endif 

	obj-y   += oplus_short_ic.o
