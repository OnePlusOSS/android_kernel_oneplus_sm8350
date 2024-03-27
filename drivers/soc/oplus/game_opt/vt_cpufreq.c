#include "game_ctrl.h"
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/cpufreq.h>


static atomic_t vt_cpu7_cpuinfo_max_freq = ATOMIC_INIT(0);

void show_cpuinfo_max_freq_hook(struct cpufreq_policy *policy, unsigned int *max_freq)
{
	struct cpufreq_policy *bpolicy;

	if ((!atomic_read(&vt_cpu7_cpuinfo_max_freq)) || (policy->cpu != 7))
		return;

	/*
	 * when vt_cpu7_cpuinfo_max_freq == 1,
	 * return cpu7 cpuinfo_max_freq with cpu4 cpuinfo_max_freq
	 */
	bpolicy = cpufreq_cpu_get(4);
	if (likely(bpolicy)) {
		*max_freq = bpolicy->cpuinfo.max_freq;
		cpufreq_cpu_put(bpolicy);
	}
}

static ssize_t vt_cpu7_cpuinfo_max_freq_proc_write(struct file *file,
			const char __user *buf, size_t count, loff_t *ppos)
{
	char page[32] = {0};
	int ret;
	int vt;

	ret = simple_write_to_buffer(page, sizeof(page) - 1, ppos, buf, count);
	if (ret <= 0)
		return ret;

	ret = sscanf(page, "%d", &vt);
	if (ret != 1)
		return -EINVAL;

	if (vt != 0 && vt != 1)
		return -EINVAL;

	if (atomic_read(&vt_cpu7_cpuinfo_max_freq) == vt)
		return count;

	atomic_set(&vt_cpu7_cpuinfo_max_freq, vt);

	return count;
}

static ssize_t vt_cpu7_cpuinfo_max_freq_proc_read(struct file *file,
			char __user *buf, size_t count, loff_t *ppos)
{
	char page[32] = {0};
	int vt, len;

	vt = atomic_read(&vt_cpu7_cpuinfo_max_freq);
	len = sprintf(page, "%d\n", vt);

	return simple_read_from_buffer(buf, count, ppos, page, len);
}

static const struct file_operations vt_cpu7_cpuinfo_max_freq_proc_ops = {
	.write		= vt_cpu7_cpuinfo_max_freq_proc_write,
	.read		= vt_cpu7_cpuinfo_max_freq_proc_read,
	.llseek		= default_llseek,
};

int vt_cpufreq_init(void)
{
	if (unlikely(!game_opt_dir))
		return -ENOTDIR;

	proc_create_data("fake_cpu7_cpuinfo_max_freq", 0664, game_opt_dir,
			&vt_cpu7_cpuinfo_max_freq_proc_ops, NULL);

	return 0;
}
