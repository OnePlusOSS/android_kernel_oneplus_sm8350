#include <linux/proc_fs.h>
static int val;
static int restart_level;/*system original val*/
struct delayed_work op_restart_modem_work;


static ssize_t proc_restart_level_all_read(struct file *p_file,
    char __user *puser_buf, size_t count, loff_t *p_offset)
{
    ssize_t len = 0;

    len = copy_to_user(puser_buf, val?"1":"0", 1);
    pr_info("the restart level switch is:%d\n", val);
    return len;
}

static ssize_t proc_restart_level_all_write(struct file *p_file,
    const char __user *puser_buf,
    size_t count, loff_t *p_offset)
{
    char  *subsysname [] = {
        "ipa_fws",
        "cvpss" ,
        "wlan" ,
        "trustedvm" ,
        "a660_zap" ,
        "venus"  ,
        "modem" ,
        "adsp",
        "cdsp",
        "slpi",
        "spss"
    };

    int i = 0;
    char temp[2] = {0};
    struct subsys_device *subsys;
    int rc;

    if (copy_from_user(temp, puser_buf, 1))
        return -EFAULT;

    rc = kstrtoint(temp, 0, &val);
    if (rc != 0)
        return -EINVAL;

    cancel_delayed_work_sync(&op_restart_modem_work);

    for (i = 0 ; i < ARRAY_SIZE(subsysname); i++) {
        subsys = find_subsys_device(subsysname[i]);
        if (subsys) {
            if (val==1)
                subsys->restart_level = RESET_SOC;
            else
                subsys->restart_level = RESET_SUBSYS_COUPLED;
        }
    }
    pr_info("write the restart level switch to :%d\n", val);
    return count;
}

static const struct file_operations restart_level_all_operations = {
    .read = proc_restart_level_all_read,
    .write = proc_restart_level_all_write,
};

static void init_restart_level_all_node(void)
{
    if (!proc_create("restart_level_all", 0644, NULL,
             &restart_level_all_operations)){
        pr_err("%s : Failed to register proc interface\n", __func__);
    }
}

static void op_restart_modem_work_fun(struct work_struct *work)
{
    struct subsys_device *subsys = find_subsys_device("modem");

	if (!subsys)
		return;
    subsys->restart_level = restart_level;
    pr_err("%s:level=%d\n", __func__,subsys->restart_level);
}

int op_restart_modem_init(void)
{
    INIT_DELAYED_WORK(&op_restart_modem_work, op_restart_modem_work_fun);
    return 0;
}

int op_restart_modem(void)
{
    struct subsys_device *subsys = find_subsys_device("modem");

    if (!subsys)
        return -ENODEV;
    pr_err("%s:level=%d\n", __func__,subsys->restart_level);
    restart_level = subsys->restart_level;
    subsys->restart_level = RESET_SUBSYS_COUPLED;
    if (subsystem_restart("modem") == -ENODEV)
        pr_err("%s: SSR call failed\n", __func__);

    schedule_delayed_work(&op_restart_modem_work,
            msecs_to_jiffies(10*1000));
    return 0;
}
EXPORT_SYMBOL(op_restart_modem);

int oem_restart_modem_init(void)
{
    op_restart_modem_init();
	init_restart_level_all_node();
    return 0;
}
