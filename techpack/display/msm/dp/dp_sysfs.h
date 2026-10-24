/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * dp_sysfs.h - Lenovo TB371FC DP display sysfs attributes.
 */

#ifndef _DP_SYSFS_H_
#define _DP_SYSFS_H_

#include <linux/device.h>

/* Publish / remove the Lenovo DP attributes for the DP platform device. */
int dp_sysfs_register(struct device *dev);
void dp_sysfs_unregister(struct device *dev);

/* Implemented in dp_display.c: current DP link lane count. */
unsigned int dp_display_lanes_count(void);

#endif /* _DP_SYSFS_H_ */
