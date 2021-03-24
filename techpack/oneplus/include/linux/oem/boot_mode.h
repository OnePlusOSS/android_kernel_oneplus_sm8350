#ifndef _BOOT_MODE_H_
#define _BOOT_MODE_H_ 1

enum oem_boot_mode {
	MSM_BOOT_MODE_NORMAL,
	MSM_BOOT_MODE_FASTBOOT,
	MSM_BOOT_MODE_RECOVERY,
	MSM_BOOT_MODE_AGING,
	MSM_BOOT_MODE_FACTORY,
	MSM_BOOT_MODE_RF,
	MSM_BOOT_MODE_CHARGE,
};

#ifdef CONFIG_QGKI
enum oem_boot_mode get_boot_mode(void);
enum oem_boot_mode op_get_boot_mode(void);

enum oem_projcet {
	OEM_PROJECT_MAX,
};

int get_oem_project(void);
int get_small_board_1_absent(void);
int get_small_board_2_absent(void);
int get_hw_board_version(void);
int get_rf_version(void);
int get_prj_version(void);

char* op_get_oem_project(void);
int op_get_small_board_1_absent(void);
int op_get_small_board_2_absent(void);
int op_get_hw_board_version(void);
int op_get_rf_version(void);
int op_get_prj_version(void);

#else
__weak enum oem_boot_mode get_boot_mode(void)
{
    return MSM_BOOT_MODE_NORMAL;
}

__weak int get_oem_project(void)
{
	return 0;
}

__weak int get_small_board_1_absent(void)
{
	return 0;
}

__weak int get_small_board_2_absent(void)
{
	return 0;
}

__weak int get_hw_board_version(void)
{
	return 0;
}

__weak int get_rf_version(void)
{
	return 0;
}

__weak int get_prj_version(void)
{
	return 0;
}

#endif //CONFIG_QGKI
#endif //_BOOT_MODE_H_
