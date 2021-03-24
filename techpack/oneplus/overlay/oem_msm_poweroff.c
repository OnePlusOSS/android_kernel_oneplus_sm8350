#define PON_RESTART_REASON_FACTORY		     0x21
#define PON_RESTART_REASON_RF		         0x22
//#define PON_RESTART_BOOTLOADER_RECOVERY		 0x23
#define PON_RESTART_REASON_SBL_DDRTEST	     0x24
#define PON_RESTART_REASON_SBL_DDR_CUS		 0x25
#define PON_RESTART_REASON_MEM_AGING		 0x26

//Define OEM reboot mode magic
#define AGING_MODE      0x77665510
#define FACTORY_MODE    0x77665504
#define RF_MODE         0x77665506
#define KERNEL_MODE     0x7766550d
#define ANDROID_MODE    0x7766550c
#define MODEM_MODE      0x7766550b
#define OEM_PANIC       0x77665518

#define REASON_SBL_DDR_CUS    0x7766550c
#define REASON_SBL_DDRTEST      0x7766550d

#ifndef CONFIG_ARCH_LAHAINA
int oem_get_download_mode(void)
{
	return download_mode && (dload_type & SCM_DLOAD_FULLDUMP);
}
EXPORT_SYMBOL(oem_get_download_mode);

void oem_force_minidump_mode(void)
{
	if (dload_type == SCM_DLOAD_FULLDUMP) {
		pr_err("force minidump mode\n");
		dload_type = SCM_DLOAD_MINIDUMP;
		set_dload_mode(dload_type);
		__raw_writel(EMMC_DLOAD_TYPE, dload_type_addr);
	}
}
EXPORT_SYMBOL(oem_force_minidump_mode);
#endif

void oem_msm_restart_prepare(const char *cmd ,u8 *reason )
{
	if (cmd != NULL) {
		if (!strcmp(cmd, "sbllowmemtest")) {
			pr_info("[op aging mem test] lunch ddr sbllowmemtest!!comm: %s, pid: %d\n"
				, current->comm, current->pid);
			*reason = (u8)PON_RESTART_REASON_SBL_DDR_CUS;
			__raw_writel(REASON_SBL_DDR_CUS, restart_reason);
		} else if (!strcmp(cmd, "sblmemtest")) {//op factory aging test
			pr_info("[op aging mem test] lunch ddr sblmemtest!!comm: %s, pid: %d\n"
				, current->comm, current->pid);
			*reason = (u8)PON_RESTART_REASON_SBL_DDRTEST;
			__raw_writel(REASON_SBL_DDRTEST, restart_reason);
		} else if (!strcmp(cmd, "usermemaging")) {
			pr_info("[op aging mem test] lunch ddr usermemaging!!comm: %s, pid: %d\n"
				, current->comm, current->pid);
			*reason = (u8)PON_RESTART_REASON_MEM_AGING;
			__raw_writel(REASON_SBL_DDRTEST, restart_reason);
		} else if (!strncmp(cmd, "rf", 2)) {
			*reason = (u8)PON_RESTART_REASON_RF;
			__raw_writel(RF_MODE, restart_reason);
		} else if (!strncmp(cmd, "ftm", 3)) {
			*reason = (u8)PON_RESTART_REASON_FACTORY;
			__raw_writel(FACTORY_MODE, restart_reason);

		} else {
			__raw_writel(0x77665501, restart_reason);
		}
	}
}