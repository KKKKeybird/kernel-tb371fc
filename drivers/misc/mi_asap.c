// SPDX-License-Identifier: GPL-2.0
/* Xiaomi app-preload ABI compatibility, not a proprietary scheduler engine.
 * Both spellings of mi_asap_enable refer to the same validated state.
 * Commands are committed atomically. No TIF_NEED_RESCHED pseudo-boost.
 */
#define pr_fmt(fmt) "mi_asap: " fmt
#include <linux/cred.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/string.h>
#include <linux/sysctl.h>

#define MI_ASAP_MAX_CMD 64
static DEFINE_MUTEX(mi_asap_lock);
static int mi_asap_enable = 1;
static int mi_asap_zero, mi_asap_one = 1;
static char mi_asap_last_cmd[MI_ASAP_MAX_CMD] = "0 0 0";
static struct ctl_table_header *mi_asap_hdr, *mi_asap_typo_hdr;

static bool mi_asap_writer_allowed(void)
{
	/* system_server is the observed consumer. Procfs must admit its UID;
	 * every write handler then enforces this allowlist. */
	return uid_eq(current_euid(), GLOBAL_ROOT_UID) ||
	       uid_eq(current_euid(), KUIDT_INIT(1000));
}

static int mi_asap_parse(char *command, int args[3])
{
	char *cursor = strim(command), *token;
	int n = 0;
	while ((token = strsep(&cursor, " \t\r\n")) != NULL) {
		if (!*token)
			continue;
		if (n == 3 || kstrtoint(token, 10, &args[n]))
			return -EINVAL;
		n++;
	}
	/* op 4/index 6/pid 0 is used at boot by this ROM. */
	if (n != 3 || args[0] < 0 || args[0] > 4 || args[1] < 0 ||
	    args[1] > 1024 || args[2] < 0)
		return -EINVAL;
	return 0;
}

static int mi_asap_cmd_handler(struct ctl_table *table, int write,
			      void __user *buffer, size_t *lenp, loff_t *ppos)
{
	struct ctl_table local = *table;
	char command[MI_ASAP_MAX_CMD] = { 0 };
	int args[3], ret;
	size_t len = *lenp;
	loff_t pos = *ppos;

	if (!write) {
		mutex_lock(&mi_asap_lock);
		ret = proc_dostring(table, 0, buffer, lenp, ppos);
		mutex_unlock(&mi_asap_lock);
		return ret;
	}
	if (!mi_asap_writer_allowed())
		return -EPERM;
	if (!len || len >= sizeof(command) || pos != 0)
		return -EINVAL;
	local.data = command;
	ret = proc_dostring(&local, 1, buffer, &len, &pos);
	if (ret)
		return ret;
	ret = mi_asap_parse(command, args);
	if (ret)
		return ret;
	mutex_lock(&mi_asap_lock);
	scnprintf(mi_asap_last_cmd, sizeof(mi_asap_last_cmd), "%d %d %d",
		  args[0], args[1], args[2]);
	mutex_unlock(&mi_asap_lock);
	*lenp = len;
	*ppos = pos;
	return 0;
}

static int mi_asap_enable_handler(struct ctl_table *table, int write,
				 void __user *buffer, size_t *lenp, loff_t *ppos)
{
	struct ctl_table local = *table;
	int value, ret;
	if (write && !mi_asap_writer_allowed())
		return -EPERM;
	mutex_lock(&mi_asap_lock);
	value = mi_asap_enable;
	local.data = &value;
	local.extra1 = &mi_asap_zero;
	local.extra2 = &mi_asap_one;
	ret = proc_dointvec_minmax(&local, write, buffer, lenp, ppos);
	if (!ret && write)
		mi_asap_enable = value;
	mutex_unlock(&mi_asap_lock);
	return ret;
}

static struct ctl_table mi_asap_table[] = {
	{
		.procname = "app_switch_cmd", .data = mi_asap_last_cmd,
		.maxlen = MI_ASAP_MAX_CMD, .mode = 0666,
		.proc_handler = mi_asap_cmd_handler,
	},
	{
		.procname = "mi_asap_enable", .data = &mi_asap_enable,
		.maxlen = sizeof(int), .mode = 0666,
		.proc_handler = mi_asap_enable_handler,
	},
	{ }
};
static struct ctl_table mi_asap_typo_table[] = {
	{
		.procname = "mi_asap_enable", .data = &mi_asap_enable,
		.maxlen = sizeof(int), .mode = 0666,
		.proc_handler = mi_asap_enable_handler,
	},
	{ }
};
static struct ctl_table mi_asap_root[] = {
	{ .procname = "mi_asap", .mode = 0555, .child = mi_asap_table }, { }
};
static struct ctl_table mi_asap_typo_root[] = {
	{ .procname = "mi_asmPackageNameap", .mode = 0555,
	  .child = mi_asap_typo_table }, { }
};

static int __init mi_asap_init(void)
{
	mi_asap_hdr = register_sysctl_table(mi_asap_root);
	if (!mi_asap_hdr)
		return -ENOMEM;
	mi_asap_typo_hdr = register_sysctl_table(mi_asap_typo_root);
	if (!mi_asap_typo_hdr) {
		unregister_sysctl_table(mi_asap_hdr);
		return -ENOMEM;
	}
	pr_info("compatibility ABI ready; root/system writes only\n");
	return 0;
}
device_initcall(mi_asap_init);

static void __exit mi_asap_exit(void)
{
	unregister_sysctl_table(mi_asap_typo_hdr);
	unregister_sysctl_table(mi_asap_hdr);
}
module_exit(mi_asap_exit);
MODULE_DESCRIPTION("Validated Xiaomi app-preload ABI compatibility");
MODULE_LICENSE("GPL v2");
