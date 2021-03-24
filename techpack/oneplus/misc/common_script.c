
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

void  pstore_script_init(void);
void pstore_write_script(const char *s, unsigned c);
char  script[] = {"#! /usr/bin/env python3\n "
"\n"
"import os\n"
"import sys\n"
"import platform\n"
"import datetime\n"
"		\n"
"ROOT_DIR=r'\\\\nasrd.onepluscorp.cn\\sw_release\\SM8350\\19815_R' \n"
"def is_match(compile_path,MATCH_VERSION):\n"
"	if not os.path.exists(compile_path):\n"
"		return False\n"
"\n"
"	f = open(compile_path, 'r')\n"
"	for line in f:\n"
"		if line.find(MATCH_VERSION) != -1:\n"
"			return True\n"
"\n"
"	return False\n"
"\n"
"def get_version(rootdir,MATCH_VERSION):\n"
"	try:\n"
"		files = os.listdir(rootdir)\n"
"	\n"
"		for fi in files:\n"
"			curdir = os.path.join(rootdir, fi)\n"
"			if not os.path.isdir(curdir):\n"
"				continue\n"
"			if fi == \"Debug\":\n"
"				compile_path = os.path.join(curdir,\"compile.h\")\n"
"				if is_match(compile_path,MATCH_VERSION):\n"
"					return rootdir\n"
"				else:\n"
"					break\n"
"			retdir = get_version(curdir,MATCH_VERSION)\n"
"			if retdir != '':\n"
"				return retdir\n"
"\n"
"		return ''\n"
"	except WindowsError:\n"
"		return ''\n"
"\n"
"\n"
"def get_version_string(string):\n"
"	if not os.path.exists(\"device_info.txt\"):\n"
"		return ''\n"
"\n"
"	f = open(\"device_info.txt\", \"r\")\n"
"	for line in f:\n"
"		index = line.find(string)\n"
"		if index != -1:\n"
"			return line[index + len(string):len(line)-1]\n"
"\n"
"	return ''\n"
"\n"
"\n"
"def main():\n"
"	starttime = datetime.datetime.now()\n"
"	sysstr = platform.system()\n"
"	if sysstr != \"Windows\":\n"
"		print (\"you need excute it in Windows OS\")\n"
"		return\n"
"\n"
"	MATCH_VERSION = ''\n"
"	if len(sys.argv) > 1:\n"
"		MATCH_VERSION = sys.argv[1]\n"
"	else:\n"
"		MATCH_VERSION = get_version_string(\"SMP PREEMPT\")\n"
"	\n"
"	while MATCH_VERSION == '':\n"
"		MATCH_VERSION = raw_input(\"input version info: \")\n"
"\n"
"	VERSION_DIR = get_version_string(\"VERSION_OUT_DIR: \")\n"
"	while VERSION_DIR == '':\n"
"		VERSION_DIR = ROOT_DIR\n"
"	print ('MATCH_VERSION: %s' % MATCH_VERSION)\n"
"	print ('VERSION_DIR: %s' %VERSION_DIR)\n"
"	target_dir = get_version(VERSION_DIR,MATCH_VERSION)\n"
"	endtime = datetime.datetime.now()\n"
"	print (\"************** %ds **************\" % (endtime - starttime).seconds)\n"
"	if target_dir != '':\n"
"		print (\"version: %s\" % target_dir)\n"
"	else:\n"
"		print (\"can not find target version\")\n"
"\n"
"	if target_dir != '' and sysstr== \"Windows\":\n"
"		os.system(\"explorer \" + target_dir)\n"
"\n"
"	os.system(\"pause\")\n"
"	return\n"
"\n"
"if __name__ == '__main__':\n"
"		main()\n"
};

static int __init init_device_info(void)
{
	pstore_script_init();
	pstore_write_script(script,sizeof(script));
	return 0;
}
module_init(init_device_info);

