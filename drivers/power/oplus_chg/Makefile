
oplus_chg-obj-y := oplus_chg_core.o
oplus_chg-obj-y += oplus_chg_sysfs.o
oplus_chg-obj-y += oplus_chg_intf.o
oplus_chg-obj-y += oplus_adapter.o
oplus_chg-obj-y += oplus_charger.o
oplus_chg-obj-y += oplus_gauge.o
oplus_chg-obj-y += oplus_short.o
oplus_chg-obj-y += oplus_warp.o
oplus_chg-obj-y += oplus_debug_info.o
oplus_chg-obj-y += oplus_chg_gki.o
oplus_chg-obj-y += oplus_chg_ic.o
oplus_chg-obj-y += oplus_chg_wls.o
oplus_chg-obj-y += oplus_chg_comm.o
oplus_chg-obj-y += oplus_chg_voter.o
oplus_chg-obj-y += oplus_chg_cfg.o
oplus_chg-obj-y += oplus_chg_strategy.o

oplus_chg-obj-y += adapter_ic/oplus_stm.o

oplus_chg-obj-y += charger_ic/oplus_sm8350.o
oplus_chg-obj-y += charger_ic/oplus_hl7227.o
oplus_chg-obj-y += charger_ic/oplus_pm8350b.o
oplus_chg-obj-y += charger_ic/oplus_short_ic.o

oplus_chg-obj-y += wireless_ic/oplus_p9415.o

oplus_chg-obj-y += gauge_ic/oplus_bq27541.o

oplus_chg-obj-y += warp_ic/oplus_rk826.o
oplus_chg-obj-y += warp_ic/oplus_op10.o
oplus_chg-obj-y += warp_ic/oplus_warp_fw.o

oplus_chg-obj-y += wireless/wls_chg_fast.o
oplus_chg-obj-y += wireless/wls_chg_normal.o
oplus_chg-obj-y += wireless/wls_chg_rx.o

oplus_chg-objs = $(oplus_chg-obj-y)

ccflags-y += -DCONFIG_OPLUS_CHG_GKI_SUPPORT -DOPLUS_CHG_REG_DUMP_ENABLE

ifeq ($(strip $(CONFIG_CHG_FOR_OP9_PRO)), y)
	ccflags-y += -DOPLUS_CHG_OP_DEF
else ifeq ($(strip $(CONFIG_OPLUS_CHG_OOS)), y)
	ccflags-y += -DOPLUS_CHG_OP_DEF
endif

obj-$(CONFIG_OPLUS_CHG) = oplus_chg.o
