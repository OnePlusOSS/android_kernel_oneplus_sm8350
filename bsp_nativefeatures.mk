##################################################################################
### Copyright (C), 2008-2030, OPLUS Mobile Comm Corp., Ltd
###
### File: - nativefeatures.mk
### Description:
###     feature used in native or kernel module to decopule oem modification
###     feature name must start with OPLUS_FEATURE_xxxxx
###     close or disable a feature just comment it,do not assign null to the variable
### Version: 1.0
### Date: 2020-03-18
###
### ------------------------------- Revision History: ----------------------------
### <author>                        <date>       <version>   <desc>
### ------------------------------------------------------------------------------
##################################################################################

# add feature variable like this : OPLUS_FEATURE_xxxxx = true
# this is not allowed like this OPLUS_FEATURE_xxxxx =
# below is the example
#OPLUS_FEATURE_TEST = yes
# comment the variable if you want to disable it

OPLUS_FEATURE_PMIC_MONITOR = yes
OPLUS_FEATURE_SHUTDOWN_DETECT = yes
OPLUS_FEATURE_FACERECOGNITION = yes
OPLUS_FEATURE_PHOENIX = yes
OPLUS_FEATURE_BINDER_MONITOR = yes
OPLUS_FEATURE_THEIA = yes
OPLUS_FEATURE_AGINGTEST = yes
OPLUS_FEATURE_OPLUSDL = yes
OPLUS_FEATURE_SCHED_ASSIST = yes
OPLUS_FEATURE_HEALTHINFO = yes
OPLUS_FEATURE_MULTI_KSWAPD = yes
OPLUS_FEATURE_FG_IO_OPT = yes
OPLUS_FEATURE_ZRAM_OPT = yes
OPLUS_FEATURE_PROCESS_RECLAIM = yes
OPLUS_FEATURE_MEMORY_ISOLATE = yes
OPLUS_FEATURE_MEMLEAK_DETECT = yes
OPLUS_FEATURE_TASK_CPUSTATS = yes
OPLUS_FEATURE_QCOM_PMICWD = yes
OPLUS_FEATURE_TP_BASIC = yes
OPLUS_FEATURE_TP_BSPFWUPDATE = yes
OPLUS_FEATURE_FINGERPRINT = yes
OPLUS_FEATURE_FINGERPRINTPAY = yes
OPLUS_FEATURE_CRYPTOENG = yes
OPLUS_FEATURE_STORAGE_TOOL = yes
OPLUS_FEATURE_UFS_DRIVER = yes
OPLUS_FEATURE_UFS_SHOW_LATENCY = yes
OPLUS_FEATURE_UFSPLUS = yes
OPLUS_FEATURE_EMMC_SDCARD_OPTIMIZE = yes
OPLUS_FEATURE_EMMC_DRIVER = yes
OPLUS_FEATURE_EXFAT_SUPPORT = yes
OPLUS_FEATURE_SDCARDFS_SUPPORT = yes
OPLUS_FEATURE_POWERINFO_STANDBY = yes
OPLUS_FEATURE_POWERINFO_STANDBY_DEBUG = yes
OPLUS_FEATURE_POWERINFO_RPMH = yes
OPLUS_FEATURE_POWERINFO_FTM = yes
OPLUS_FEATURE_MULTI_FREEAREA = yes
OPLUS_FEATURE_VIRTUAL_RESERVE_MEMORY = yes
OPLUS_FEATURE_LOWMEM_DBG = yes
OPLUS_FEATURE_SENSOR_DRIVER = yes
OPLUS_FEATURE_SENSOR_ALGORITHM = yes
OPLUS_FEATURE_SENSOR_SMEM = yes
OPLUS_FEATURE_SENSOR_FEEDBACK = yes
OPLUS_FEATURE_ELEVATOR_DETECT = yes
OPLUS_FEATURE_ACTIVITY_RECOGNITION = yes
OPLUS_FEATURE_CHG_BASIC = yes
