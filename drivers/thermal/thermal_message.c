// SPDX-License-Identifier: GPL-2.0
/*
 * thermal_message: Xiaomi thermal_message sysfs interface shim
 *
 * HyperOS userspace on this Lenovo platform expects the Xiaomi thermal hub:
 *
 *   /sys/class/thermal/thermal_message/<attr>
 *
 * Producers/consumers found by reversing the shipped ROM:
 *   - com.miui.securitycenter PowerUtils polls board_sensor_temp
 *   - mi_thermald (yudi vendor) uses the full attribute set and watches
 *     sconfig via inotify, switching thermal-engine config files by index
 *   - system_server MiuiMiPerfStubImpl reads sconfig for boost-mode decisions
 *   - PowerKeeper / Joyose read the rest (temp/charge/scene state)
 *
 * This driver provides the complete attribute surface.  Temperatures are
 * derived from the real thermal zones.  Values written by userspace are
 * stored and logged.  cpu_limits additionally applies a per-CPU maximum
 * frequency limit so a scene manager gets a real effect; level indices 1..100 are rejected because their mapping is unknown.
 * A zero or hardware-maximum value releases this driver's cap vote.
 *
 * Scene index values for sconfig (decrypted from the vendor thermal-map.conf):
 *   0 normal, 4 videochat, 7 dolbyvision, 8 phone, 9/13/16 tgame, 12 camera,
 *   17/18 4k, 20 mgame, 21 video, 23 yuanshen, 25 8k, 100 normal-unfold ...
 */

#define pr_fmt(fmt) "thermal_message: " fmt

#include <linux/cpufreq.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/cpu.h>
#include <linux/power_supply.h>
#include <linux/string.h>
#include <linux/thermal.h>
#include <linux/workqueue.h>

#define TM_NAME		"thermal_message"
#define TM_RETRY_MS	500
#define TM_MAX_RETRIES	120
#define TM_DEFAULT_TEMP	30000

enum tm_kind {
	TM_STORE = 0,
	TM_BATT_TEMP,
	TM_SKIN_TEMP,
	TM_CPU_LIMITS,
};

struct tm_attr {
	const char *name;
	enum tm_kind kind;
};

static const struct tm_attr tm_attr_table[] = {
	{ "board_sensor_temp", TM_BATT_TEMP },
	{ "board_temp", TM_BATT_TEMP },
	{ "board_sensor_charge_temp", TM_BATT_TEMP },
	{ "board_sensor_second_temp", TM_SKIN_TEMP },
	{ "board_sensor_other_temp", TM_SKIN_TEMP },
	{ "board_sensor_temp_comp", TM_STORE },
	{ "ambient_sensor", TM_BATT_TEMP },
	{ "ambient_sensor_temp", TM_BATT_TEMP },
	{ "charger_temp", TM_BATT_TEMP },
	{ "display_therm_temp", TM_SKIN_TEMP },
	{ "usb_online", TM_STORE },
	{ "screen_state", TM_STORE },
	{ "sconfig", TM_STORE },
	{ "cpu_limits", TM_CPU_LIMITS },
	{ "cpu_nolimit_temp", TM_STORE },
	{ "boost", TM_STORE },
	{ "balance_mode", TM_STORE },
	{ "temp_aware", TM_STORE },
	{ "temp_state", TM_STORE },
	{ "board_sensor_trip", TM_STORE },
	{ "charger_only_index", TM_STORE },
	{ "atc_enable", TM_STORE },
	{ "flash_state", TM_STORE },
	{ "torch_real_level", TM_STORE },
	{ "ipa_cdev_load", TM_STORE },
	{ "ipa_dump_info", TM_STORE },
	{ "video_mode", TM_STORE },
	{ "super_hdr", TM_STORE },
	{ "cloud_game", TM_STORE },
	{ "wifi_limit", TM_STORE },
	{ "voice_receiver_state", TM_STORE },
};

#define TM_ATTR_COUNT	ARRAY_SIZE(tm_attr_table)

static DEFINE_MUTEX(tm_lock);
static unsigned int tm_cpu_caps[NR_CPUS];
static struct device *tm_dev;
static struct thermal_zone_device *tm_batt_zone;
static struct thermal_zone_device *tm_skin_zone;
static struct delayed_work tm_work;
static int tm_retries;

static struct device_attribute tm_dev_attrs[TM_ATTR_COUNT];
static struct attribute *tm_attr_ptrs[TM_ATTR_COUNT + 1];
static char tm_values[TM_ATTR_COUNT][64] = {
	[0 ... TM_ATTR_COUNT - 1] = "0",
};

static struct thermal_zone_device *tm_find_zone(const char * const *names,
						int nr)
{
	struct thermal_zone_device *zone;
	int i;

	for (i = 0; i < nr; i++) {
		zone = thermal_zone_get_zone_by_name(names[i]);
		if (!IS_ERR(zone))
			return zone;
	}

	return NULL;
}

static int tm_read_temp(struct thermal_zone_device *zone, int *temp)
{
	return zone ? thermal_zone_get_temp(zone, temp) : -ENODEV;
}

static int tm_find_attr(struct device_attribute *attr)
{
	int i;

	for (i = 0; i < TM_ATTR_COUNT; i++)
		if (attr == &tm_dev_attrs[i])
			return i;

	return -1;
}

static ssize_t tm_show(struct device *dev, struct device_attribute *attr,
		       char *buf)
{
	int idx = tm_find_attr(attr), ret, temp;
	struct power_supply *psy;
	union power_supply_propval val = { 0 };
	ssize_t len;

	if (idx < 0)
		return -EINVAL;
	if (tm_attr_table[idx].kind == TM_BATT_TEMP ||
	    tm_attr_table[idx].kind == TM_SKIN_TEMP) {
		ret = tm_read_temp(tm_attr_table[idx].kind == TM_BATT_TEMP ?
				   tm_batt_zone : tm_skin_zone, &temp);
		return ret ? ret : scnprintf(buf, PAGE_SIZE, "%d\n", temp);
	}
	if (!strcmp(tm_attr_table[idx].name, "usb_online")) {
		psy = power_supply_get_by_name("usb");
		if (!psy)
			return -ENODEV;
		ret = power_supply_get_property(psy, POWER_SUPPLY_PROP_ONLINE, &val);
		power_supply_put(psy);
		return ret ? ret : scnprintf(buf, PAGE_SIZE, "%d\n", !!val.intval);
	}
	mutex_lock(&tm_lock);
	len = scnprintf(buf, PAGE_SIZE, "%s\n", tm_values[idx]);
	mutex_unlock(&tm_lock);
	return len;
}

/* A separate cap vote: never overwrite the user's or another service's
 * user_policy.{min,max}. cpufreq re-applies this vote after other updates. */
static int tm_cpufreq_adjust(struct notifier_block *nb, unsigned long event,
			     void *data)
{
	struct cpufreq_policy *policy = data;
	unsigned int cpu, vote, cap = policy->cpuinfo.max_freq;

	if (event != CPUFREQ_ADJUST)
		return NOTIFY_DONE;
	for_each_cpu(cpu, policy->related_cpus) {
		vote = READ_ONCE(tm_cpu_caps[cpu]);
		if (vote && vote < cap)
			cap = vote;
	}
	cpufreq_verify_within_limits(policy, 0, cap);
	return NOTIFY_OK;
}

static struct notifier_block tm_cpufreq_nb = {
	.notifier_call = tm_cpufreq_adjust,
};

static int tm_apply_cpu_limit(const char *buf)
{
	char key[16], text[24], extra;
	unsigned int value;
	int cpu;
	struct cpufreq_policy *policy;

	if (sscanf(buf, "%15s %23s %c", key, text, &extra) != 2 ||
	    strncmp(key, "cpu", 3) || kstrtoint(key + 3, 10, &cpu) ||
	    kstrtouint(text, 10, &value))
		return -EINVAL;
	if (cpu < 0 || cpu >= nr_cpu_ids || !cpu_possible(cpu))
		return -EINVAL;
	/* The original vendor's level table is unknown: do not fake success. */
	if (value && value <= 100)
		return -EOPNOTSUPP;
	policy = cpufreq_cpu_get(cpu);
	if (!policy)
		return -ENODEV;
	if (value >= policy->cpuinfo.max_freq)
		value = 0; /* legacy restore-to-hardware-max releases only our vote */
	else if (value && value < policy->cpuinfo.min_freq)
		value = policy->cpuinfo.min_freq;
	WRITE_ONCE(tm_cpu_caps[cpu], value);
	cpufreq_update_policy(cpu);
	cpufreq_cpu_put(policy);
	return 0;
}

static ssize_t tm_store(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
	int idx = tm_find_attr(attr), ret = 0;
	char value[64];

	if (idx < 0)
		return -EINVAL;
	if (count >= sizeof(value))
		return -E2BIG;
	memcpy(value, buf, count);
	value[count] = '\0';
	mutex_lock(&tm_lock);
	if (tm_attr_table[idx].kind == TM_CPU_LIMITS)
		ret = tm_apply_cpu_limit(strim(value));
	if (!ret)
		strscpy(tm_values[idx], strim(value), sizeof(tm_values[idx]));
	mutex_unlock(&tm_lock);
	return ret ? ret : count;
}

static int tm_register(void)
{
	static const char * const batt_names[] = {
		"battery",
		"bms",
	};
	static const char * const skin_names[] = {
		"skin-msm-therm-usr",
		"skin-therm-usr",
		"quiet-therm-usr",
		"conn-therm-usr",
	};
	struct class *cls;
	int i;

	if (tm_dev)
		return 0;

	tm_batt_zone = tm_find_zone(batt_names, ARRAY_SIZE(batt_names));
	tm_skin_zone = tm_find_zone(skin_names, ARRAY_SIZE(skin_names));
	if (!tm_batt_zone) {
		tm_batt_zone = tm_skin_zone;
		tm_skin_zone = NULL;
	}
	if (!tm_batt_zone)
		return -ENODEV;

	cls = tm_batt_zone->device.class;
	if (!cls || IS_ERR(cls))
		return -ENODEV;

	for (i = 0; i < TM_ATTR_COUNT; i++) {
		struct device_attribute *a = &tm_dev_attrs[i];

		sysfs_attr_init(&a->attr);
		a->attr.name = tm_attr_table[i].name;
		a->attr.mode = (tm_attr_table[i].kind == TM_BATT_TEMP ||
				tm_attr_table[i].kind == TM_SKIN_TEMP ||
				!strcmp(a->attr.name, "usb_online")) ? 0444 : 0644;
		a->show = tm_show;
		a->store = (a->attr.mode & 0200) ? tm_store : NULL;
		tm_attr_ptrs[i] = &a->attr;
	}
	tm_attr_ptrs[TM_ATTR_COUNT] = NULL;

	{
		static struct attribute_group group;

		group.attrs = tm_attr_ptrs;
		{
			static const struct attribute_group *groups[2];

			groups[0] = &group;
			groups[1] = NULL;
			tm_dev = device_create_with_groups(cls, NULL, 0, NULL,
							   groups, TM_NAME);
		}
	}
	if (IS_ERR(tm_dev)) {
		int ret = PTR_ERR(tm_dev);

		tm_dev = NULL;
		return ret;
	}

	pr_info("ready: %zu attrs, battery=%s skin=%s\n", TM_ATTR_COUNT,
		tm_batt_zone->type, tm_skin_zone ? tm_skin_zone->type : "none");
	return 0;
}

static void tm_work_fn(struct work_struct *work)
{
	if (!tm_register())
		return;

	if (++tm_retries < TM_MAX_RETRIES)
		schedule_delayed_work(&tm_work, msecs_to_jiffies(TM_RETRY_MS));
	else
		pr_err("no usable thermal zone, giving up\n");
}

static int __init tm_init(void)
{
	int ret = cpufreq_register_notifier(&tm_cpufreq_nb, CPUFREQ_POLICY_NOTIFIER);

	if (ret)
		return ret;
	INIT_DELAYED_WORK(&tm_work, tm_work_fn);
	schedule_delayed_work(&tm_work, msecs_to_jiffies(1000));

	return 0;
}
late_initcall(tm_init);

static void __exit tm_exit(void)
{
	unsigned int cpu;

	cancel_delayed_work_sync(&tm_work);
	cpufreq_unregister_notifier(&tm_cpufreq_nb, CPUFREQ_POLICY_NOTIFIER);
	for_each_online_cpu(cpu) {
		WRITE_ONCE(tm_cpu_caps[cpu], 0);
		cpufreq_update_policy(cpu);
	}

	if (tm_dev) {
		device_unregister(tm_dev);
		tm_dev = NULL;
	}
}
module_exit(tm_exit);

MODULE_DESCRIPTION("Xiaomi thermal_message sysfs compatibility shim");
MODULE_LICENSE("GPL v2");
