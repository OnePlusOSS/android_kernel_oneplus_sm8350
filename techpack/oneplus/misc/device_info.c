
#include <linux/kernel.h>

#include <linux/init.h>

#include <linux/types.h>

#include <linux/pstore.h>
#include <soc/qcom/socinfo.h>

#include <linux/pstore.h>
#include <generated/compile.h>
#include <linux/oem/param_rw.h>
#include <linux/oem/boot_mode.h>
#include <linux/module.h>
#include <linux/moduleparam.h>

#define MAX_ITEM 5
#define MAX_LENGTH 32

enum
{
	serialno = 0,
	hw_version,
	rf_version,
	ddr_manufacture_info,
	pcba_number
};


char oem_serialno[16];
char oem_hw_version[3];
char oem_rf_version[3];
char oem_ddr_manufacture_info[16];
char oem_pcba_number[30];

const char cmdline_info[MAX_ITEM][MAX_LENGTH] =
{
	"androidboot.serialno=",
	"androidboot.hw_version=",
	"androidboot.rf_version=",
	"ddr_manufacture_info=",
	"androidboot.pcba_number=",
};


static int __init device_info_init(void)
{
	int i, j;
	char *substr, *target_str;

	for(i=0; i<MAX_ITEM; i++)
	{
		substr = strstr(boot_command_line, cmdline_info[i]);
		if(substr != NULL)
			substr += strlen(cmdline_info[i]);
		else
			continue;

		if(i == serialno)
			target_str = oem_serialno;
		else if(i == hw_version)
			target_str = oem_hw_version;
		else if(i == rf_version)
			target_str = oem_rf_version;
		else if(i == ddr_manufacture_info)
			target_str = oem_ddr_manufacture_info;
		else if(i == pcba_number)
			target_str = oem_pcba_number;

		for(j=0; substr[j] != ' '; j++)
			target_str[j] = substr[j];
		target_str[j] = '\0';
	}
	return 1;
}

void  pstore_device_info_init(void );
void pstore_write_device_info(const char *s, unsigned c);
extern uint32_t socinfo_get_serial_number(void);

static void write_device_info(const char *key, const char *value)
{
	pstore_write_device_info(key, strlen(key));
	pstore_write_device_info(": ", 2);
	pstore_write_device_info(value, strlen(value));
	pstore_write_device_info("\r\n", 2);
}

static int __init init_device_info(void)
{
	char *ptr = NULL;
	ptr = oem_pcba_number;
	get_param_by_index_and_offset(0, 0x4D, ptr, 28);

	pstore_device_info_init();

	device_info_init();

	write_device_info("hardware version", oem_hw_version);
	write_device_info("rf version", oem_rf_version);
	write_device_info("ddr manufacturer", oem_ddr_manufacture_info);
	write_device_info("pcba number", oem_pcba_number);
	write_device_info("serial number", oem_serialno);

	/*
	scnprintf(oem_serialno, sizeof(oem_serialno), "%x", socinfo_get_serial_number());
	write_device_info("socinfo serial_number", oem_serialno);
	*/

	write_device_info("SMBIP", SMBIP);
	write_device_info("RELEASE", RELEASE);
	write_device_info("WINDOWS_RELEASE", WINDOWS_RELEASE);
	write_device_info("CURRENT_OUT_DIRECTORY", CURRENT_OUT_DIRECTORY);
	write_device_info("IMG_FULL_NAME", IMG_FULL_NAME);
	write_device_info("VERSION_OUT_DIR", VERSION_OUT_DIR);
	write_device_info("VERSION_OUT_DIR_FULL_NAME", VERSION_OUT_DIR_FULL_NAME);
	write_device_info("VERSION_OUT_COMPILE", VERSION_OUT_COMPILE);
	write_device_info("VERSION_OUT_ELF", VERSION_OUT_ELF);

	write_device_info("kernel version", linux_banner);
	write_device_info("boot command", saved_command_line);

	return 0;
}

MODULE_LICENSE("GPL v2");
module_init(init_device_info);

void save_dump_reason_to_device_info(char *reason) {
        write_device_info("dump reason is ", reason);
}
EXPORT_SYMBOL(save_dump_reason_to_device_info);
