// SPDX-License-Identifier: GPL-2.0-only
/*
 * dp_sysfs.c - Lenovo TB371FC DP display sysfs attributes.
 *
 * The Lenovo vendor display HAL (vendor.lenovo.hardware.display@1.0-impl.so)
 * reads/writes three attributes under the DP platform device:
 *
 *   .../ae90000.qcom,dp_display/lanes_count   (ro)
 *   .../ae90000.qcom,dp_display/hpd_sim       (rw)
 *   .../ae90000.qcom,dp_display/prefer_mode   (rw)
 *
 * The stock kernel exposes them (kallsyms show_lanes_count, store_hpd_sim,
 * store_prefer_mode) but the CLO display tree we build from does not, so the
 * stock HAL finds no node. This unit re-adds them; the rest of the DP driver
 * is only wired in (register/unregister + one read accessor).
 *
 * There is exactly one DP display instance, so the writable state lives in a
 * single static struct here; the read-only lane count is pulled from the DP
 * driver through dp_display_lanes_count().
 */

#define pr_fmt(fmt) "dp_sysfs: %s: " fmt, __func__

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sysfs.h>

#include "dp_sysfs.h"

struct dp_sysfs_state {
	struct device *dev;
	unsigned int hpd_sim;
	unsigned int prefer_mode[4];
};

static struct dp_sysfs_state dp_sysfs;

static ssize_t lanes_count_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%u\n", dp_display_lanes_count());
}
static DEVICE_ATTR_RO(lanes_count);

static ssize_t hpd_sim_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%u\n", dp_sysfs.hpd_sim);
}

static ssize_t hpd_sim_store(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t count)
{
	unsigned int val;
	int rc;

	rc = kstrtouint(buf, 10, &val);
	if (rc)
		return rc;

	dp_sysfs.hpd_sim = !!val;
	return count;
}
static DEVICE_ATTR_RW(hpd_sim);

/*
 * prefer_mode carries up to four decimal integers, matching the stock parser
 * (sscanf "%d %d %d %d") and the HAL's "%d %d %d %d" write format. Fields not
 * supplied by the writer keep their previous value.
 */
static ssize_t prefer_mode_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int i, n = 0;

	for (i = 0; i < ARRAY_SIZE(dp_sysfs.prefer_mode); i++)
		n += scnprintf(buf + n, PAGE_SIZE - n, "%u ",
			       dp_sysfs.prefer_mode[i]);
	n += scnprintf(buf + n, PAGE_SIZE - n, "\n");
	return n;
}

static ssize_t prefer_mode_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	unsigned int v[4];
	int rc, i;

	rc = sscanf(buf, "%u %u %u %u", &v[0], &v[1], &v[2], &v[3]);
	if (rc < 1)
		return -EINVAL;

	for (i = 0; i < rc && i < ARRAY_SIZE(dp_sysfs.prefer_mode); i++)
		dp_sysfs.prefer_mode[i] = v[i];

	return count;
}
static DEVICE_ATTR_RW(prefer_mode);

static struct attribute *dp_sysfs_attrs[] = {
	&dev_attr_lanes_count.attr,
	&dev_attr_hpd_sim.attr,
	&dev_attr_prefer_mode.attr,
	NULL,
};

static const struct attribute_group dp_sysfs_group = {
	.attrs = dp_sysfs_attrs,
};

int dp_sysfs_register(struct device *dev)
{
	int rc;

	rc = sysfs_create_group(&dev->kobj, &dp_sysfs_group);
	if (rc) {
		pr_err("failed to create sysfs group: %d\n", rc);
		return rc;
	}

	dp_sysfs.dev = dev;
	pr_info("attributes added (lanes_count/hpd_sim/prefer_mode)\n");
	return 0;
}

void dp_sysfs_unregister(struct device *dev)
{
	if (dp_sysfs.dev != dev)
		return;

	sysfs_remove_group(&dev->kobj, &dp_sysfs_group);
	dp_sysfs.dev = NULL;
}
