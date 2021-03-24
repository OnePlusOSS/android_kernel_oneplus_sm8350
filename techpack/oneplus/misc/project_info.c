/* For OEM project information
 *such as project name, hardware ID
 */

#include <linux/export.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/sys_soc.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/oem/project_info.h>
#include <linux/soc/qcom/smem.h>
#include <linux/gpio.h>
#include <soc/qcom/socinfo.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/machine.h>
#include <linux/soc/qcom/smem.h>
#include <linux/pstore.h>
#include <linux/oem/device_info.h>
static struct component_info component_info_desc[COMPONENT_MAX];
static struct kobject *project_info_kobj;
static struct project_info *project_info_desc;

int a_board_val = 0;
static struct kobject *component_info;
static ssize_t project_info_get(struct kobject *kobj,
    struct kobj_attribute *attr, char *buf);
static ssize_t component_info_get(struct kobject *kobj,
    struct kobj_attribute *attr, char *buf);
static ssize_t add_component_info(struct kobject *kobj,
    struct kobj_attribute *attr, const char *buf, size_t count);

#define OP_ATTR(_name, _mode, _show, _store) \
    struct kobj_attribute op_attr_##_name = __ATTR(_name, _mode, _show, _store)


static OP_ATTR(project_name, 0444, project_info_get, NULL);
static OP_ATTR(hw_id, 0444, project_info_get, NULL);
static OP_ATTR(rf_id_v1, 0444, project_info_get, NULL);
static OP_ATTR(rf_id_v2, 0444, project_info_get, NULL);
static OP_ATTR(rf_id_v3, 0444, project_info_get, NULL);
static OP_ATTR(ddr_manufacture_info, 0444, project_info_get, NULL);
static OP_ATTR(ddr_row, 0444, project_info_get, NULL);
static OP_ATTR(ddr_column, 0444, project_info_get, NULL);
static OP_ATTR(ddr_fw_version, 0444, project_info_get, NULL);
static OP_ATTR(ddr_reserve_info, 0444, project_info_get, NULL);
static OP_ATTR(secboot_status, 0444, project_info_get, NULL);
static OP_ATTR(platform_id, 0444, project_info_get, NULL);
static OP_ATTR(serialno, 0444, project_info_get, NULL);
static OP_ATTR(aboard_id, 0444, project_info_get, NULL);
static OP_ATTR(add_component, 0200, NULL, add_component_info);

extern char* op_get_lcd_manufacture(void);
extern char* op_get_lcd_version(void);
extern char* op_get_backlight_manufacture(void);
extern char* op_get_backlight_version(void);

char *parse_regs_pc(unsigned long address, int *length)
{
	static char function_name[KSYM_SYMBOL_LEN];
	if (!address)
		return NULL;
	*length = sprint_symbol(function_name, address);

	return function_name;
}

uint8 get_secureboot_fuse_status(void)
{
    void __iomem *oem_config_base;
    uint8 secure_oem_config = 0;

    oem_config_base = ioremap(SECURE_BOOT1, 1);
    if (!oem_config_base)
        return -EINVAL;
    secure_oem_config = __raw_readb(oem_config_base);
    iounmap(oem_config_base);
    pr_debug("secure_oem_config 0x%x\n", secure_oem_config);

    return secure_oem_config;
}

static ssize_t project_info_get(struct kobject *kobj,
                            struct kobj_attribute *attr,
                            char *buf)
{
    size_t size;

    project_info_desc = qcom_smem_get(QCOM_SMEM_HOST_ANY,SMEM_PROJECT_INFO, &size);
    if (IS_ERR_OR_NULL(project_info_desc)) {
        pr_err("%s: get project_info failure\n", __func__);
        return -EINVAL;
    }
    else {
        if (attr == &op_attr_project_name)
            return snprintf(buf, BUF_SIZE, "%s\n",
            project_info_desc->project_name);
        if (attr == &op_attr_hw_id)
            return snprintf(buf, BUF_SIZE, "%d\n",
            project_info_desc->hw_version);
        if (attr == &op_attr_rf_id_v1)
            return snprintf(buf, BUF_SIZE, "%d\n",
            project_info_desc->rf_v1);
        if (attr == &op_attr_rf_id_v2)
            return snprintf(buf, BUF_SIZE, "%d\n",
            project_info_desc->rf_v2);
        if (attr == &op_attr_rf_id_v3)
            return snprintf(buf, BUF_SIZE, "%d\n",
            project_info_desc->rf_v3);
        if (attr == &op_attr_ddr_manufacture_info)
            return snprintf(buf, BUF_SIZE, "%d\n",
            project_info_desc->ddr_manufacture_info);
        if (attr == &op_attr_ddr_row)
            return snprintf(buf, BUF_SIZE, "%d\n",
            project_info_desc->ddr_row);
        if (attr == &op_attr_ddr_column)
            return snprintf(buf, BUF_SIZE, "%d\n",
            project_info_desc->ddr_column);
        if (attr == &op_attr_ddr_fw_version)
            return snprintf(buf, BUF_SIZE, "%d\n",
            project_info_desc->ddr_fw_version);
        if (attr == &op_attr_ddr_reserve_info)
            return snprintf(buf, BUF_SIZE, "%d\n",
            project_info_desc->ddr_reserve_info);
        if (attr == &op_attr_secboot_status)
            return snprintf(buf, BUF_SIZE, "%d\n",
            get_secureboot_fuse_status());
        if (attr == &op_attr_platform_id)
            return snprintf(buf, BUF_SIZE, "%d\n",
            project_info_desc->platform_id);

        if (attr == &op_attr_serialno)
            return snprintf(buf, BUF_SIZE, "0x%x\n",
            socinfo_get_serial_number());

        if (attr == &op_attr_aboard_id)
            return snprintf(buf, BUF_SIZE, "%d\n",a_board_val);
    }

    return -EINVAL;
}

static char *ufs_64gb_pn_list[] =
{
    NULL,
};

static char *ufs_128gb_pn_list[] =
{
    "KLUDG4UHDB-B2D1",
    "HN8T05BZGKX015",
    NULL,
};

static char *ufs_256gb_pn_list[] =
{
    "KLUEG8UHDB-C2D1",
    "HN8T15BZGKX016",
    NULL,
};

static char *ufs_512gb_pn_list[] =
{
    "HN8T25BZGKX017",
    NULL,
};

enum UFS_SIZE {
    UFS_SIZE_64GB,
    UFS_SIZE_128GB,
    UFS_SIZE_256GB,
    UFS_SIZE_512GB,
    UFS_SIZE_UNKNOWN
};

static enum UFS_SIZE ufs_get_size_by_pn(char *pn)
{
    int i = 0;
    while (ufs_64gb_pn_list[i]){
        if (!strncmp(ufs_64gb_pn_list[i], pn ,strlen(ufs_64gb_pn_list[i])))
            return UFS_SIZE_64GB;
        i++;
    }

    i = 0;
    while (ufs_128gb_pn_list[i]){
        if (!strncmp(ufs_128gb_pn_list[i], pn ,strlen(ufs_128gb_pn_list[i])))
            return UFS_SIZE_128GB;
        i++;
    }

    i = 0;
    while (ufs_256gb_pn_list[i]){
        if (!strncmp(ufs_256gb_pn_list[i], pn ,strlen(ufs_256gb_pn_list[i])))
            return UFS_SIZE_256GB;
        i++;
    }

    i = 0;
    while (ufs_512gb_pn_list[i]){
        if (!strncmp(ufs_512gb_pn_list[i], pn ,strlen(ufs_512gb_pn_list[i])))
            return UFS_SIZE_512GB;
        i++;
    }

    return UFS_SIZE_UNKNOWN;
}

static char ufs_vendor_and_rev[32] = {'\0'};
static char ufs_product_id[32] = {'\0'};
/* UFS KLUDG4UHDB-B2D1 SAMSUNG 1302 */
void add_component_ufs(char * buf, size_t count)
{
    char *sptr, *token = NULL;
    enum UFS_SIZE ufs_size;
    sptr = buf;

    token = strsep(&sptr, " ");
    if (!token)
        return;
    if (strlen(token) >= count)
        return;
    strncpy(ufs_product_id, token, sizeof(ufs_product_id));

    token = strstr(sptr, "\n");
    if (token)
        *token = '\0';
    token = strstr(sptr, "\r");
    if (token)
        *token = '\0';
    strncpy(ufs_vendor_and_rev, sptr, sizeof(ufs_vendor_and_rev));

    ufs_size = ufs_get_size_by_pn(ufs_product_id);

    switch (ufs_size) {
    case UFS_SIZE_64GB:
        strncat(ufs_vendor_and_rev, " 64G", 4);
        break;
    case UFS_SIZE_128GB:
        strncat(ufs_vendor_and_rev, " 128G", 5);
        break;
    case UFS_SIZE_256GB:
        strncat(ufs_vendor_and_rev, " 256G", 5);
        break;
    case UFS_SIZE_512GB:
        strncat(ufs_vendor_and_rev, " 512G", 5);
        break;
    case UFS_SIZE_UNKNOWN:
        strncat(ufs_vendor_and_rev, " 0G", 3);
        break;
    }

    push_component_info(UFS, ufs_product_id, ufs_vendor_and_rev);
}

static ssize_t add_component_info(struct kobject *kobj,
    struct kobj_attribute *attr, const char *buf, size_t count)
{
    char *pos;
    char buffer[64];
    if (attr != &op_attr_add_component)
        goto out;

    strncpy(buffer, buf, 64);

    pos = strnstr(buffer, " ", count);
    if (!pos)
        goto out;

    if (!strncmp(buffer, "UFS ", 4))
    {
        add_component_ufs(buffer + 4, count - 4);
    }

out:
    return count;
}

static struct attribute *project_info_sysfs_entries[] = {
    &op_attr_project_name.attr,
    &op_attr_hw_id.attr,
    &op_attr_rf_id_v1.attr,
    &op_attr_rf_id_v2.attr,
    &op_attr_rf_id_v3.attr,
    &op_attr_ddr_manufacture_info.attr,
    &op_attr_ddr_row.attr,
    &op_attr_ddr_column.attr,
    &op_attr_ddr_fw_version.attr,
    &op_attr_ddr_reserve_info.attr,
    &op_attr_secboot_status.attr,
    &op_attr_platform_id.attr,
    &op_attr_serialno.attr,
    &op_attr_aboard_id.attr,
    &op_attr_add_component.attr,
    NULL,
};

static struct attribute_group project_info_attr_group = {
    .attrs  = project_info_sysfs_entries,
};

static OP_ATTR(ddr, 0444, component_info_get, NULL);
static OP_ATTR(emmc, 0444, component_info_get, NULL);
static OP_ATTR(f_camera, 0444, component_info_get, NULL);
static OP_ATTR(second_f_camera, 0444, component_info_get, NULL);
static OP_ATTR(r_camera, 0444, component_info_get, NULL);
static OP_ATTR(second_r_camera, 0444, component_info_get, NULL);
static OP_ATTR(third_r_camera, 0444, component_info_get, NULL);
static OP_ATTR(forth_r_camera, 0444, component_info_get, NULL);
static OP_ATTR(r_ois, 0444, component_info_get, NULL);
static OP_ATTR(second_r_ois, 0444, component_info_get, NULL);
static OP_ATTR(tp, 0444, component_info_get, NULL);
static OP_ATTR(lcd, 0444, component_info_get, NULL);
static OP_ATTR(wcn, 0444, component_info_get, NULL);
static OP_ATTR(l_sensor, 0444, component_info_get, NULL);
static OP_ATTR(g_sensor, 0444, component_info_get, NULL);
static OP_ATTR(m_sensor, 0444, component_info_get, NULL);
static OP_ATTR(gyro, 0444, component_info_get, NULL);
static OP_ATTR(backlight, 0444, component_info_get, NULL);
static OP_ATTR(mainboard, 0444, component_info_get, NULL);
static OP_ATTR(fingerprints, 0444, component_info_get, NULL);
static OP_ATTR(touch_key, 0444, component_info_get, NULL);
static OP_ATTR(ufs, 0444, component_info_get, NULL);
static OP_ATTR(Aboard, 0444, component_info_get, NULL);
static OP_ATTR(nfc, 0444, component_info_get, NULL);
static OP_ATTR(fast_charge, 0444, component_info_get, NULL);
static OP_ATTR(wireless_charge, 0444, component_info_get, NULL);
static OP_ATTR(cpu, 0444, component_info_get, NULL);
static OP_ATTR(rf_version, 0444, component_info_get, NULL);


char *get_component_version(enum COMPONENT_TYPE type)
{
    if (type >= COMPONENT_MAX) {
        pr_err("%s == type %d invalid\n", __func__, type);
        return "N/A";
    }
    return component_info_desc[type].version?:"N/A";
}

char *get_component_manufacture(enum COMPONENT_TYPE type)
{
    if (type >= COMPONENT_MAX) {
        pr_err("%s == type %d invalid\n", __func__, type);
        return "N/A";
    }
    return component_info_desc[type].manufacture?:"N/A";

}

int push_component_info(enum COMPONENT_TYPE type,
    char *version, char *manufacture)
{
    if (type >= COMPONENT_MAX)
        return -ENOMEM;
    component_info_desc[type].version = version;
    component_info_desc[type].manufacture = manufacture;

    return 0;
}
EXPORT_SYMBOL(push_component_info);

int reset_component_info(enum COMPONENT_TYPE type)
{
    if (type >= COMPONENT_MAX)
        return -ENOMEM;
    component_info_desc[type].version = NULL;
    component_info_desc[type].manufacture = NULL;

    return 0;
}
EXPORT_SYMBOL(reset_component_info);


static struct attribute *component_info_sysfs_entries[] = {
    &op_attr_ddr.attr,
    &op_attr_emmc.attr,
    &op_attr_f_camera.attr,
    &op_attr_second_f_camera.attr,
    &op_attr_r_camera.attr,
    &op_attr_second_r_camera.attr,
    &op_attr_third_r_camera.attr,
    &op_attr_forth_r_camera.attr,
    &op_attr_r_ois.attr,
    &op_attr_second_r_ois.attr,
    &op_attr_tp.attr,
    &op_attr_lcd.attr,
    &op_attr_wcn.attr,
    &op_attr_l_sensor.attr,
    &op_attr_g_sensor.attr,
    &op_attr_m_sensor.attr,
    &op_attr_gyro.attr,
    &op_attr_backlight.attr,
    &op_attr_mainboard.attr,
    &op_attr_fingerprints.attr,
    &op_attr_touch_key.attr,
    &op_attr_ufs.attr,
    &op_attr_Aboard.attr,
    &op_attr_nfc.attr,
    &op_attr_fast_charge.attr,
	&op_attr_wireless_charge.attr,
    &op_attr_cpu.attr,
    &op_attr_rf_version.attr,
    NULL,
};

static struct attribute_group component_info_attr_group = {
    .attrs  = component_info_sysfs_entries,
};

static ssize_t component_info_get(struct kobject *kobj,
                            struct kobj_attribute *attr,
                            char *buf)
{
    if (attr == &op_attr_ddr)
        return snprintf(buf, BUF_SIZE, "VER:\t%s\nMANU:\t%s\n",
        get_component_version(DDR),
        get_component_manufacture(DDR));
    if (attr == &op_attr_emmc)
        return snprintf(buf, BUF_SIZE, "VER:\t%s\nMANU:\t%s\n",
        get_component_version(EMMC),
        get_component_manufacture(EMMC));
    if (attr == &op_attr_f_camera)
        return snprintf(buf, BUF_SIZE, "VER:\t%s\nMANU:\t%s\n",
        get_component_version(F_CAMERA),
        get_component_manufacture(F_CAMERA));
    if (attr == &op_attr_second_f_camera)
        return snprintf(buf, BUF_SIZE, "VER:\t%s\nMANU:\t%s\n",
        get_component_version(SECOND_F_CAMERA),
        get_component_manufacture(SECOND_F_CAMERA));
    if (attr == &op_attr_r_camera)
        return snprintf(buf, BUF_SIZE, "VER:\t%s\nMANU:\t%s\n",
        get_component_version(R_CAMERA),
        get_component_manufacture(R_CAMERA));
    if (attr == &op_attr_second_r_camera)
        return snprintf(buf, BUF_SIZE, "VER:\t%s\nMANU:\t%s\n",
        get_component_version(SECOND_R_CAMERA),
        get_component_manufacture(SECOND_R_CAMERA));
    if (attr == &op_attr_third_r_camera)
        return snprintf(buf, BUF_SIZE, "VER:\t%s\nMANU:\t%s\n",
        get_component_version(THIRD_R_CAMERA),
        get_component_manufacture(THIRD_R_CAMERA));
    if (attr == &op_attr_forth_r_camera)
        return snprintf(buf, BUF_SIZE, "VER:\t%s\nMANU:\t%s\n",
        get_component_version(FORTH_R_CAMERA),
        get_component_manufacture(FORTH_R_CAMERA));
    if (attr == &op_attr_r_ois)
        return snprintf(buf, BUF_SIZE, "VER:\t%s\nMANU:\t%s\n",
        get_component_version(R_OIS),
        get_component_manufacture(R_OIS));
    if (attr == &op_attr_second_r_ois)
        return snprintf(buf, BUF_SIZE, "VER:\t%s\nMANU:\t%s\n",
        get_component_version(SECOND_R_OIS),
        get_component_manufacture(SECOND_R_OIS));
    if (attr == &op_attr_tp)
        return snprintf(buf, BUF_SIZE, "VER:\t%s\nMANU:\t%s\n",
        get_component_version(TP),
        get_component_manufacture(TP));
    if (attr == &op_attr_lcd)
        return snprintf(buf, BUF_SIZE, "VER:\t%s\nMANU:\t%s\n",
        get_component_version(LCD),
        get_component_manufacture(LCD));
    if (attr == &op_attr_wcn)
        return snprintf(buf, BUF_SIZE, "VER:\t%s\nMANU:\t%s\n",
        get_component_version(WCN),
        get_component_manufacture(WCN));
    if (attr == &op_attr_l_sensor)
        return snprintf(buf, BUF_SIZE, "VER:\t%s\nMANU:\t%s\n",
        get_component_version(I_SENSOR),
        get_component_manufacture(I_SENSOR));
    if (attr == &op_attr_g_sensor)
        return snprintf(buf, BUF_SIZE, "VER:\t%s\nMANU:\t%s\n",
        get_component_version(G_SENSOR),
        get_component_manufacture(G_SENSOR));
    if (attr == &op_attr_m_sensor)
        return snprintf(buf, BUF_SIZE, "VER:\t%s\nMANU:\t%s\n",
        get_component_version(M_SENSOR),
        get_component_manufacture(M_SENSOR));
    if (attr == &op_attr_gyro)
        return snprintf(buf, BUF_SIZE, "VER:\t%s\nMANU:\t%s\n",
        get_component_version(GYRO),
        get_component_manufacture(GYRO));
    if (attr == &op_attr_backlight)
        return snprintf(buf, BUF_SIZE, "VER:\t%s\nMANU:\t%s\n",
        get_component_version(BACKLIGHT),
        get_component_manufacture(BACKLIGHT));
    if (attr == &op_attr_mainboard)
        return snprintf(buf, BUF_SIZE, "VER:\t%s\nMANU:\t%s\n",
        get_component_version(MAINBOARD),
        get_component_manufacture(MAINBOARD));
    if (attr == &op_attr_fingerprints)
        return snprintf(buf, BUF_SIZE, "VER:\t%s\nMANU:\t%s\n",
        get_component_version(FINGERPRINTS),
        get_component_manufacture(FINGERPRINTS));
    if (attr == &op_attr_touch_key)
        return snprintf(buf, BUF_SIZE, "VER:\t%s\nMANU:\t%s\n",
        get_component_version(TOUCH_KEY),
        get_component_manufacture(TOUCH_KEY));
    if (attr == &op_attr_ufs)
        return snprintf(buf, BUF_SIZE, "VER:\t%s\nMANU:\t%s\n",
        get_component_version(UFS),
        get_component_manufacture(UFS));
    if (attr == &op_attr_Aboard) {
        return snprintf(buf, BUF_SIZE, "VER:\t%s\nMANU:\t%s\n",
        get_component_version(ABOARD),
        get_component_manufacture(ABOARD));
    }
    if (attr == &op_attr_nfc)
        return snprintf(buf, BUF_SIZE, "VER:\t%s\nMANU:\t%s\n",
        get_component_version(NFC),
        get_component_manufacture(NFC));
    if (attr == &op_attr_fast_charge)
        return snprintf(buf, BUF_SIZE, "VER:\t%s\nMANU:\t%s\n",
        get_component_version(FAST_CHARGE),
        get_component_manufacture(FAST_CHARGE));
	if (attr == &op_attr_wireless_charge)
		return snprintf(buf, BUF_SIZE, "VER:\t%s\nMANU:\t%s\n",
		get_component_version(WIRELESS_CHARGE),
		get_component_manufacture(WIRELESS_CHARGE));
    if (attr == &op_attr_cpu)
        return snprintf(buf, BUF_SIZE, "VER:\t%s\nMANU:\t%s\n",
        get_component_version(CPU),
        get_component_manufacture(CPU));
    if (attr == &op_attr_rf_version)
        return snprintf(buf, BUF_SIZE, "VER:\t%s\nMANU:\t%s\n",
        get_component_version(RF_VERSION),
        get_component_manufacture(RF_VERSION));
    return -EINVAL;
}

static int project_info_init_sysfs(void)
{
    int error = 0;

    project_info_kobj = kobject_create_and_add("project_info", NULL);
    if (!project_info_kobj)
        return -ENOMEM;
    error = sysfs_create_group(project_info_kobj, &project_info_attr_group);
    if (error) {
        pr_err("project_info_init_sysfs project_info_attr_group failure\n");
        return error;
    }

    component_info = kobject_create_and_add("component_info",
        project_info_kobj);
    pr_info("project_info_init_sysfs success\n");
    if (!component_info)
        return -ENOMEM;

    error = sysfs_create_group(component_info, &component_info_attr_group);
    if (error) {
        pr_err("project_info_init_sysfs project_info_attr_group failure\n");
        return error;
    }
    return 0;
}

struct ddr_manufacture {
    int id;
    char name[20];
};
//ddr id and ddr name
static char ddr_version[32] = {0};
static char ddr_manufacture[20] = {0};
char ddr_manufacture_and_fw_verion[40] = {0};
static char cpu_type[20] = {0};

struct ddr_manufacture ddr_manufacture_list[] = {
    {1, "Samsung "},
    {2, "Qimonda "},
    {3, "Elpida "},
    {4, "Etpon "},
    {5, "Nanya "},
    {6, "Hynix "},
    {7, "Mosel "},
    {8, "Winbond "},
    {9, "Esmt "},
    {255, "Micron"},
    {0, "Unknown"},
};

struct cpu_list {
    int id;
    char name[20];
};

struct cpu_list cpu_list_msm[] = {
    {321, "SDM845 "},
    {339, "SM8150 "},
    {356, "SM8250 "},
    {415, "SM8350 "},
    {0, "Unknown"},
};

void get_ddr_manufacture_name(void)
{
    uint32 i, length;
    size_t size;

    length = ARRAY_SIZE(ddr_manufacture_list);

    project_info_desc = qcom_smem_get(QCOM_SMEM_HOST_ANY,SMEM_PROJECT_INFO, &size);

    if (IS_ERR_OR_NULL(project_info_desc)) {
        pr_err("%s: get project_info failure\n", __func__);
        return;
    } else {
        for (i = 0; i < length; i++) {
            if (ddr_manufacture_list[i].id ==
                project_info_desc->ddr_manufacture_info) {
                snprintf(ddr_manufacture, sizeof(ddr_manufacture), "%s",
                    ddr_manufacture_list[i].name);
                break;
            }
        }
    }
}

void get_cpu_type(void)
{
    uint32 i, length;
    size_t size;

    length = ARRAY_SIZE(cpu_list_msm);

    project_info_desc = qcom_smem_get(QCOM_SMEM_HOST_ANY,SMEM_PROJECT_INFO, &size);

    if (IS_ERR_OR_NULL(project_info_desc)) {
        pr_err("%s: get project_info failure\n", __func__);
        return;
    } else {
        for (i = 0; i < length; i++) {
            if (cpu_list_msm[i].id ==
                project_info_desc->platform_id) {
                snprintf(cpu_type, sizeof(cpu_type),
                    "%s", cpu_list_msm[i].name);
                break;
            }
        }
    }
}

static char mainboard_version[64] = {0};
static char mainboard_manufacture[8] = {'O',
    'N', 'E', 'P', 'L', 'U', 'S', '\0'};
static char rf_version[16] = {0};
struct main_board_info{
   int  prj_version;
   int  hw_version;
   int  rf_version;
   char  version_name[32];
};



struct main_board_info main_board_info_check[]={
    /*  prj      hw       rf        version*/
    {   12     ,  11     , 11      ,"19825 T0 CN"},
    {   12     ,  11     , 13      ,"19825 T0 IN"},
    {   12     ,  12     , 11      ,"19825 EVT CN"},
    {   12     ,  12     , 13      ,"19825 EVT IN"},
    {   12     ,  32     , 11      ,"19825 EVT TEST CN"},
    {   12     ,  32     , 13      ,"19825 EVT TEST IN"},
    {   12     ,  13     , 11      ,"19825 EVT2 CN"},
    {   12     ,  13     , 13      ,"19825 EVT2 IN"},
    {   12     ,  21     , 11      ,"19825 DVT CN"},
    {   12     ,  21     , 13      ,"19825 DVT IN"},
    {   12     ,  33     , 11      ,"19825 DVT SEC CN"},
    {   12     ,  33     , 13      ,"19825 DVT SEC IN"},
    {   12     ,  23     , 11      ,"19825 PVT QM77043 CN"},
    {   12     ,  23     , 13      ,"19825 PVT QM77043 IN"},
    {   12     ,  22     , 11      ,"19825 PVT CN"},
    {   12     ,  22     , 13      ,"19825 PVT IN"},
    {   12     ,  31     , 11      ,"19825 PVT SEC CN"},
    {   12     ,  31     , 13      ,"19825 PVT SEC IN"},

    {   12     ,  11     , 22      ,"2080A T0 NA"},
    {   12     ,  11     , 21      ,"2080A T0 EU"},
    {   12     ,  11     , 12      ,"2080A T0 TMO"},
    {   12     ,  11     , 23      ,"2080A T0 VZW"},
    {   12     ,  12     , 22      ,"2080A EVT1 NA"},
    {   12     ,  12     , 21      ,"2080A EVT1 EU"},
    {   12     ,  12     , 12      ,"2080A EVT1 TMO"},
    {   12     ,  12     , 23      ,"2080A EVT1 VZW"},
    {   12     ,  32     , 22      ,"2080A EVT1 QORVO NA"},
    {   12     ,  32     , 21      ,"2080A EVT1 QORVO EU"},
    {   12     ,  32     , 12      ,"2080A EVT1 QORVO TMO"},
    {   12     ,  32     , 23      ,"2080A EVT1 QORVO VZW"},
    {   12     ,  13     , 22      ,"2080A EVT2 NA"},
    {   12     ,  13     , 21      ,"2080A EVT2 EU"},
    {   12     ,  13     , 12      ,"2080A EVT2 TMO"},
    {   12     ,  13     , 23      ,"2080A EVT2 VZW"},
    {   12     ,  21     , 22      ,"2080A EVT3 NA"},
    {   12     ,  21     , 21      ,"2080A EVT3 EU"},
    {   12     ,  21     , 12      ,"2080A EVT3 TMO"},
    {   12     ,  21     , 23      ,"2080A EVT3 VZW"},
    {   12     ,  33     , 22      ,"2080A DVT NA"},
    {   12     ,  33     , 21      ,"2080A DVT EU"},
    {   12     ,  33     , 12      ,"2080A DVT TMO"},
    {   12     ,  33     , 23      ,"2080A DVT VZW"},
    {   12     ,  22     , 22      ,"2080A PVT NA"},
    {   12     ,  22     , 21      ,"2080A PVT EU"},
    {   12     ,  22     , 12      ,"2080A PVT TMO"},
    {   12     ,  22     , 23      ,"2080A PVT VZW"},
    {   12     ,  31     , 22      ,"2080A PVT2 NA"},
    {   12     ,  31     , 21      ,"2080A PVT2 EU"},
    {   12     ,  31     , 12      ,"2080A PVT2 TMO"},
    {   12     ,  31     , 23      ,"2080A PVT2 VZW"},

    {   11     ,  11     , 11      ,"19815 T0 CN"},
    {   11     ,  11     , 13      ,"19815 T0 IN"},
    {   11     ,  11     , 22      ,"19815 T0 NA"},
    {   11     ,  11     , 21      ,"19815 T0 EU"},
    {   11     ,  11     , 12      ,"19815 T0 TMO"},
    {   11     ,  12     , 11      ,"19815 EVT1 CN"},
    {   11     ,  12     , 13      ,"19815 EVT1 IN"},
    {   11     ,  12     , 22      ,"19815 EVT1 NA"},
    {   11     ,  12     , 21      ,"19815 EVT1 EU"},
    {   11     ,  12     , 12      ,"19815 EVT1 TMO"},
    {   11     ,  13     , 11      ,"19815 EVT2 CN"},
    {   11     ,  13     , 13      ,"19815 EVT2 IN"},
    {   11     ,  13     , 22      ,"19815 EVT2 NA"},
    {   11     ,  13     , 21      ,"19815 EVT2 EU"},
    {   11     ,  13     , 12      ,"19815 EVT2 TMO"},
    {   11     ,  23     , 11      ,"19815 EVT3 TEST CN"},
    {   11     ,  23     , 13      ,"19815 EVT3 TEST IN"},
    {   11     ,  21     , 11      ,"19815 DVT CN"},
    {   11     ,  21     , 13      ,"19815 DVT IN"},
    {   11     ,  21     , 22      ,"19815 DVT NA"},
    {   11     ,  21     , 21      ,"19815 DVT EU"},
    {   11     ,  21     , 12      ,"19815 DVT TMO"},
    {   11     ,  33     , 11      ,"19815 DVT2 CN"},
    {   11     ,  33     , 13      ,"19815 DVT2 IN"},
    {   11     ,  33     , 22      ,"19815 DVT2 NA"},
    {   11     ,  33     , 21      ,"19815 DVT2 EU"},
    {   11     ,  33     , 12      ,"19815 DVT2 TMO"},
    {   11     ,  32     , 11      ,"19815 DVT SEC CN"},
    {   11     ,  22     , 11      ,"19815 PVT CN"},
    {   11     ,  22     , 13      ,"19815 PVT IN"},
    {   11     ,  22     , 22      ,"19815 PVT NA"},
    {   11     ,  22     , 21      ,"19815 PVT EU"},
    {   11     ,  22     , 12      ,"19815 PVT TMO"},
    {   11     ,  31     , NONDEFINE      ,"19815 PVT SEC"}, //reserve

    {NONDEFINE,NONDEFINE,NONDEFINE,"Unknown"}
};

uint32 get_hw_version(void)
{
    size_t size;

    project_info_desc = qcom_smem_get(QCOM_SMEM_HOST_ANY,SMEM_PROJECT_INFO, &size);

    if (IS_ERR_OR_NULL(project_info_desc)) {
        pr_err("%s: get project_info failure\n", __func__);
        return -1;
    } else {
        pr_err("%s: hw version: %d\n", __func__,
            project_info_desc->hw_version);
        return project_info_desc->hw_version;
    }
    return 0;
}

static int init_project_info(void)
{
    static bool project_info_init_done;
    int ddr_size = 0;
    size_t size;
    int i = 0;
    char *p = NULL;

    if (project_info_init_done)
        return 0;

    project_info_desc = qcom_smem_get(QCOM_SMEM_HOST_ANY,SMEM_PROJECT_INFO, &size);

    if (IS_ERR_OR_NULL(project_info_desc)) {
        pr_err("%s: get project_info failure\n", __func__);
        return PTR_ERR(project_info_desc);
    }
    pr_err("%s: project_name: %s hw_version: %d prj=%d rf_v1: %d rf_v2: %d: rf_v3: %d  paltform_id:%d\n",
        __func__, project_info_desc->project_name,
        project_info_desc->hw_version,
        project_info_desc->prj_version,
        project_info_desc->rf_v1,
        project_info_desc->rf_v2,
        project_info_desc->rf_v3,
        project_info_desc->platform_id);


    p = &main_board_info_check[ARRAY_SIZE(main_board_info_check)-1].version_name[0];

    for( i = 0 ; i < ARRAY_SIZE(main_board_info_check) ; i++ )
    {
        if(project_info_desc->prj_version == main_board_info_check[i].prj_version &&
           project_info_desc->hw_version  == main_board_info_check[i].hw_version &&
          (project_info_desc->rf_v1  == main_board_info_check[i].rf_version ||
           NONDEFINE   == main_board_info_check[i].rf_version ))
        {
           p = &main_board_info_check[i].version_name[0];
           break;
        }
    }

    snprintf(mainboard_version, sizeof(mainboard_version), "%d %d %s %s ",
        project_info_desc->prj_version,project_info_desc->hw_version,
        project_info_desc->project_name, p);

    pr_err("board info: %s\n", mainboard_version);
    push_component_info(MAINBOARD,
        mainboard_version,
        mainboard_manufacture);

    snprintf(rf_version, sizeof(rf_version),  " %d",project_info_desc->rf_v1);
    push_component_info(RF_VERSION, rf_version, mainboard_manufacture);

    get_ddr_manufacture_name();

    /* approximate as ceiling of total pages */
    ddr_size = (totalram_pages() + (1 << 18) + (1 << 17)) >> 18;

    snprintf(ddr_version, sizeof(ddr_version), "size_%dG_r_%d_c_%d",
        ddr_size, project_info_desc->ddr_row,
        project_info_desc->ddr_column);
    snprintf(ddr_manufacture_and_fw_verion,
        sizeof(ddr_manufacture_and_fw_verion),
        "%s%s %u.%u", ddr_manufacture,
        project_info_desc->ddr_reserve_info == 0x05 ? "20nm" :
        (project_info_desc->ddr_reserve_info == 0x06 ? "18nm" : " "),
        project_info_desc->ddr_fw_version >> 16,
        project_info_desc->ddr_fw_version & 0x0000FFFF);
    push_component_info(DDR, ddr_version, ddr_manufacture_and_fw_verion);

    get_cpu_type();
    push_component_info(CPU, cpu_type, "Qualcomm");
    push_component_info(NFC, "SN100", "NXP");
    push_component_info(WCN, "WCN6851", "QualComm");
    push_component_info(LCD, op_get_lcd_version(), op_get_lcd_manufacture());
    push_component_info(BACKLIGHT, op_get_backlight_version(), op_get_backlight_manufacture());
    project_info_init_done = true;

    return 0;
}

static int project_info_probe(struct platform_device *pdev)
{
	int rc = 0;

	rc = init_project_info();
	if(!rc)
		rc = project_info_init_sysfs();

	return rc;
}

static const struct of_device_id project_info_of_match[] = {
    { .compatible = "oem,project_info", },
    {}
};
MODULE_DEVICE_TABLE(of, project_info_of_match);

static struct platform_driver project_info_driver = {
    .driver = {
        .name       = "project_info",
        .owner      = THIS_MODULE,
        .of_match_table = project_info_of_match,
    },
    .probe = project_info_probe,
};

static int __init project_info_init(void)
{
    int ret;

    ret = platform_driver_register(&project_info_driver);
    if (ret)
        pr_err("project_info_driver register failed: %d\n", ret);

    return ret;
}

static void __exit project_info_exit(void)
{
	pr_err("%s exit\n", __func__);
}

MODULE_LICENSE("GPL v2");
module_init(project_info_init);
module_exit(project_info_exit);
