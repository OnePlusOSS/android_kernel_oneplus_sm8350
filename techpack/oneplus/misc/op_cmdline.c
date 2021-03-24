#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/uaccess.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/ctype.h>
#include <linux/miscdevice.h>
#include <linux/proc_fs.h>
#include <linux/oem/boot_mode.h>
#include <asm/setup.h>

#define CMDLINE_BOOT_MODE		"androidboot.ftm_mode"
#define CMDLINE_PRJ_CODENAME	"androidboot.project_codename"
#define CMDLINE_SMALL_BOARD1	"androidboot.small_board_1_absent"
#define CMDLINE_SMALL_BOARD2	"androidboot.small_board_2_absent"
#define CMDLINE_HW_VER			"androidboot.hw_version"
#define CMDLINE_RF_VER			"androidboot.rf_version"
#define CMDLINE_PRJ_VER			"androidboot.prj_version"
#define CMDLINE_LCD_TYPE		"msm_drm.dsi_display0"
#define CMDLINE_LCD_MANUFACTURE		"lcd_manufacture"
#define CMDLINE_LCD_VERSION		"lcd_version"
#define CMDLINE_BACKLIGHT_MANUFACTURE		"backlight_manufacture"
#define CMDLINE_BACKLIGHT_VERSION		"backlight_version"
static char op_cmdline[COMMAND_LINE_SIZE];
module_param_string(op_cmdline, op_cmdline, COMMAND_LINE_SIZE, 0444);

static enum oem_boot_mode boot_mode = MSM_BOOT_MODE_NORMAL;
static char oem_project[32] = {0};
static int small_board_1_absent = 0;
static int small_board_2_absent = 0;
static int hw_version = 0;
static int rf_version = 0;
static int prj_version = 0;
static int lcd_type = 0;
static char lcd_manufacture[32] = {0};
static char backlight_manufacture[32] = {0};
static char lcd_version[32] = {0};
static char backlight_version[32] = {0};

char *enum_ftm_mode[] = {"normal",
						 "fastboot",
						 "recovery",
						 "aging",
						 "ftm_at",
						 "ftm_rf",
						 "charger"
};

/**
 * exported functions
 */
enum oem_boot_mode op_get_boot_mode(void)
{
	return boot_mode;
}
EXPORT_SYMBOL(op_get_boot_mode);

char* op_get_oem_project(void)
{
	return oem_project;
}
EXPORT_SYMBOL(op_get_oem_project);

int op_get_small_board_1_absent(void)
{
	return small_board_1_absent;
}
EXPORT_SYMBOL(op_get_small_board_1_absent);

int op_get_small_board_2_absent(void)
{
	return small_board_2_absent;
}
EXPORT_SYMBOL(op_get_small_board_2_absent);

int op_get_hw_board_version(void)
{
	return hw_version;
}
EXPORT_SYMBOL(op_get_hw_board_version);

int op_get_rf_version(void)
{
	return rf_version;
}
EXPORT_SYMBOL(op_get_rf_version);

int op_get_prj_version(void)
{
	return prj_version;
}
EXPORT_SYMBOL(op_get_prj_version);

int op_get_lcd_type(void)
{
	return lcd_type;
}
EXPORT_SYMBOL(op_get_lcd_type);

char* op_get_lcd_manufacture(void)
{
	return lcd_manufacture;
}
EXPORT_SYMBOL(op_get_lcd_manufacture);

char* op_get_lcd_version(void)
{
	return lcd_version;
}
EXPORT_SYMBOL(op_get_lcd_version);

char* op_get_backlight_manufacture(void)
{
	return backlight_manufacture;
}
EXPORT_SYMBOL(op_get_backlight_manufacture);

char* op_get_backlight_version(void)
{
	return backlight_version;
}
EXPORT_SYMBOL(op_get_backlight_version);
/**
 * cmdline_get_value - find the value of a param in cmdline
 * @str:	point to param of interest
 * @data:	store the value of param
 * @length: sizeof data
 * if param is not found in op_cmdline, NULL is returned,
 * otherwise, a pointer point to the value of the param is returned.
 */
static char * cmdline_get_value(char *str, char *data, size_t length)
{
	char *begin, *end;
	size_t len = length;

	begin = strnstr(op_cmdline, str, strlen(op_cmdline));
	if (!begin)
		return begin;

	/* begin point to the first char after '=' */
	begin += strlen(str) + 1;
	end = strchr(begin, ' ');

	len = min((size_t)(end - begin), len - 1);
	if (!end)
		len = min(strlen(begin), len);

	strncpy(data, begin, len);
	data[len] = '\0';

	return begin;
}

static int __init boot_mode_init(char *str)
{
	char buf[32];
	pr_info("boot_mode_init %s\n", str);

	if (!cmdline_get_value(str, buf, sizeof(buf))) {
		pr_err("%s failed, or param not found\n", __func__);
		return 0;
	}

	if (strncmp(buf, "ftm_at", 6) == 0)
		boot_mode = MSM_BOOT_MODE_FACTORY;
	else if (strncmp(buf, "ftm_rf", 6) == 0)
		boot_mode = MSM_BOOT_MODE_RF;
	else if (strncmp(buf, "ftm_recovery", 12) == 0)
		boot_mode = MSM_BOOT_MODE_RECOVERY;
	else if (strncmp(buf, "ftm_aging", 9) == 0)
		boot_mode = MSM_BOOT_MODE_AGING;

	pr_info("kernel boot_mode = %s[%d]\n",
			enum_ftm_mode[boot_mode], boot_mode);

	return 0;
}


static int __init boot_mode_init_normal(void)
{
	char *substrftm = strnstr(op_cmdline,
		"androidboot.ftm_mode=", strlen(op_cmdline));
	char *substrnormal = strnstr(op_cmdline,
		"androidboot.mode=", strlen(op_cmdline));
	char *substrftmstr = NULL;
	char *substrnormalstr = NULL;

	substrftmstr = substrftm + strlen("androidboot.ftm_mode=");
	substrnormalstr = substrnormal + strlen("androidboot.mode=");

	if (substrftm != NULL && substrftmstr != NULL) {

	} else if (substrnormal != NULL && substrnormalstr != NULL) {
		if (strncmp(substrnormalstr, "recovery", 8) == 0)
			boot_mode = MSM_BOOT_MODE_RECOVERY;
		else if (strncmp(substrnormalstr, "charger", 7) == 0)
			boot_mode = MSM_BOOT_MODE_CHARGE;
	}

	pr_info("kernel normal boot_mode = %s[%d]\n",
	enum_ftm_mode[boot_mode], boot_mode);
	return 0;
}


static int __init get_oem_project_init(char *str)
{
	cmdline_get_value(str, oem_project, strlen(oem_project));
	pr_info("kernel oem_project %s\n", oem_project);
	return 0;
}

static int __init get_small_board_1_absent_init(char *str)
{
	char buf[32];

	if (!cmdline_get_value(str, buf, sizeof(buf))) {
		pr_err("%s failed\n", __func__);
		return 0;
	}

	small_board_1_absent = simple_strtol(buf, NULL, 0);
	pr_info("kernel small_board_1_absent %d\n", small_board_1_absent);
	return 0;
}

static int __init get_small_board_2_absent_init(char *str)
{
	char buf[32];

	if (!cmdline_get_value(str, buf, sizeof(buf))) {
		pr_err("%s failed\n", __func__);
		return 0;
	}

	small_board_2_absent=simple_strtol(buf, NULL, 0);
	pr_info("kernel small_board_2_absent %d\n",small_board_2_absent);
	return 0;
}

static int __init get_hw_version_init(char *str)
{
	char buf[32];

	if (!cmdline_get_value(str, buf, sizeof(buf))) {
		pr_err("%s failed\n", __func__);
		return 0;
	}

	hw_version = simple_strtol(buf, NULL, 0);
	pr_info("kernel hw_version %d\n", hw_version);
	return 0;
}

static int __init get_rf_version_init(char *str)
{
	char buf[32];

	if (!cmdline_get_value(str, buf, sizeof(buf))) {
		pr_err("%s failed\n", __func__);
		return 0;
	}

	rf_version = simple_strtol(buf, NULL, 0);
	pr_info("kernel rf_version %d\n", rf_version);
	return 0;
}

static int __init get_prj_version_init(char *str)
{
	char buf[32];

	if (!cmdline_get_value(str, buf, sizeof(buf))) {
		pr_err("%s failed\n", __func__);
		return 0;
	}

	prj_version = simple_strtol(buf, NULL, 0);
	pr_info("kernel prj_version %d\n", prj_version);
	return 0;
}
static int __init get_dsi_panel_init(char *str)
{
	char buf[42];
	pr_info("dis_panel_init %s\n", str);

	if (!cmdline_get_value(str, buf, sizeof(buf))) {
		pr_err("%s failed, or param not found\n", __func__);
		return 0;
	}

	pr_info("dsi name is is %s\n", buf);
	if (!strcmp(buf, "qcom,mdss_dsi_samsung_ana6706_dsc_cmd:")) {
		lcd_type = 1;
	} else if (!strcmp(buf, "qcom,mdss_dsi_samsung_amb655x_dsc_cmd:")) {
		lcd_type = 2;
	} else if ((!strcmp(buf, "qcom,mdss_dsi_samsung_amb670yf01_dsc_cmd:")) ||
		(!strcmp(buf, "qcom,mdss_dsi_samsung_amb670yf01_o_dsc_cm"))) {
		lcd_type = 3;
	}

	pr_info("lcd_type is %d\n", lcd_type);
	return 0;

}

static int __init get_lcd_manufacture(char *str)
{
	if (!cmdline_get_value(str, lcd_manufacture, sizeof(lcd_manufacture))) {
		pr_err("%s failed, or param not found\n", __func__);
		return 0;
	}

	pr_err("lcd_manufacture %s\n", lcd_manufacture);

	return 0;
}

static int __init get_lcd_version(char *str)
{
	if (!cmdline_get_value(str, lcd_version, sizeof(lcd_version))) {
		pr_err("%s failed, or param not found\n", __func__);
		return 0;
	}

	pr_err("lcd_version %s\n", lcd_version);

	return 0;
}

static int __init get_backlight_manufacture(char *str)
{
	if (!cmdline_get_value(str, backlight_manufacture, sizeof(backlight_manufacture))) {
		pr_err("%s failed, or param not found\n", __func__);
		return 0;
	}

	pr_err("backlight_version %s\n", backlight_manufacture);

	return 0;
}

static int __init get_backlight_version(char *str)
{
	if (!cmdline_get_value(str, backlight_version, sizeof(backlight_version))) {
		pr_err("%s failed, or param not found\n", __func__);
		return 0;
	}

	pr_err("backlight_version %s\n", backlight_version);

	return 0;
}

static int __init op_cmdline_init(void)
{
	pr_info("%s:%s\n", __func__, op_cmdline);

	boot_mode_init(CMDLINE_BOOT_MODE);
	boot_mode_init_normal();
	get_oem_project_init(CMDLINE_PRJ_CODENAME);
	get_small_board_1_absent_init(CMDLINE_SMALL_BOARD1);
	get_small_board_2_absent_init(CMDLINE_SMALL_BOARD2);
	get_hw_version_init(CMDLINE_HW_VER);
	get_rf_version_init(CMDLINE_RF_VER);
	get_prj_version_init(CMDLINE_PRJ_VER);
	get_dsi_panel_init(CMDLINE_LCD_TYPE);
	get_lcd_manufacture(CMDLINE_LCD_MANUFACTURE);
	get_lcd_version(CMDLINE_LCD_VERSION);
	get_backlight_manufacture(CMDLINE_BACKLIGHT_MANUFACTURE);
	get_backlight_version(CMDLINE_BACKLIGHT_VERSION);

	return 0;
}

static void __exit op_cmdline_exit(void)
{
	pr_info("%s exit\n", __func__);
}

MODULE_LICENSE("GPL v2");
module_init(op_cmdline_init);
module_exit(op_cmdline_exit);
