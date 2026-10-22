// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2018-2021, The Linux Foundation. All rights reserved.
 */

#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/extcon.h>
#include <linux/power_supply.h>
#include <linux/usb/ch9.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/regmap.h>
#include <linux/ctype.h>

/* PS5169 sequences, stock Image-b .data; starts = the addresses stock
 * gen_dev_set/probe actually dereference. Entry = {reg, val, delay_ms}:
 * delay==0 -> regmap_write, else msleep(delay). */
static bool redriver_exist;

bool get_redriver_exist(void)
{
	return redriver_exist;
}
EXPORT_SYMBOL(get_redriver_exist);

static const u8 ps5169_default_seq[] = {
	0x9d, 0x80, 0x00,	/* reg 0x9d = 0x80 delay 0 */
	0x00, 0x00, 0x0a,	/* reg 0x00 = 0x00 delay 10 */
	0x9d, 0x00, 0x00,	/* reg 0x9d = 0x00 delay 0 */
	0x40, 0x80, 0x00,	/* reg 0x40 = 0x80 delay 0 */
	0x04, 0x44, 0x00,	/* reg 0x04 = 0x44 delay 0 */
	0xa0, 0x02, 0x00,	/* reg 0xa0 = 0x02 delay 0 */
	0x51, 0x87, 0x00,	/* reg 0x51 = 0x87 delay 0 */
	0x50, 0x20, 0x00,	/* reg 0x50 = 0x20 delay 0 */
	0x54, 0x11, 0x00,	/* reg 0x54 = 0x11 delay 0 */
	0x5d, 0x66, 0x00,	/* reg 0x5d = 0x66 delay 0 */
	0x52, 0x50, 0x00,	/* reg 0x52 = 0x50 delay 0 */
	0x55, 0x00, 0x00,	/* reg 0x55 = 0x00 delay 0 */
	0x56, 0x00, 0x00,	/* reg 0x56 = 0x00 delay 0 */
	0x57, 0x00, 0x00,	/* reg 0x57 = 0x00 delay 0 */
	0x58, 0x00, 0x00,	/* reg 0x58 = 0x00 delay 0 */
	0x59, 0x00, 0x00,	/* reg 0x59 = 0x00 delay 0 */
	0x5a, 0x00, 0x00,	/* reg 0x5a = 0x00 delay 0 */
	0x5b, 0x00, 0x00,	/* reg 0x5b = 0x00 delay 0 */
	0x5e, 0x06, 0x00,	/* reg 0x5e = 0x06 delay 0 */
	0x5f, 0x00, 0x00,	/* reg 0x5f = 0x00 delay 0 */
	0x60, 0x00, 0x00,	/* reg 0x60 = 0x00 delay 0 */
	0x61, 0x03, 0x00,	/* reg 0x61 = 0x03 delay 0 */
	0x65, 0x40, 0x00,	/* reg 0x65 = 0x40 delay 0 */
	0x66, 0x00, 0x00,	/* reg 0x66 = 0x00 delay 0 */
	0x67, 0x03, 0x00,	/* reg 0x67 = 0x03 delay 0 */
	0x75, 0x0c, 0x00,	/* reg 0x75 = 0x0c delay 0 */
	0x77, 0x00, 0x00,	/* reg 0x77 = 0x00 delay 0 */
	0x78, 0x7c, 0x00,	/* reg 0x78 = 0x7c delay 0 */
};
#define PS5169_DEFAULT_SEQ_LEN 28
static const u8 ps5169_USB_seq[] = {
	0x40, 0xc0, 0x00,	/* reg 0x40 = 0xc0 delay 0 */
	0x8d, 0x01, 0x00,	/* reg 0x8d = 0x01 delay 0 */
	0x90, 0x01, 0x00,	/* reg 0x90 = 0x01 delay 0 */
	0x00, 0x00, 0x00,	/* reg 0x00 = 0x00 delay 0 */
};
/* stock applies only the first 3 entries (gen_dev_set 0x8a798f0 loop) */
#define PS5169_USB_SEQ_LEN 3
static const u8 ps5169_USB_FLIP_seq[] = {
	0x40, 0xd0, 0x00,	/* reg 0x40 = 0xd0 delay 0 */
	0x8d, 0x01, 0x00,	/* reg 0x8d = 0x01 delay 0 */
	0x90, 0x01, 0x00,	/* reg 0x90 = 0x01 delay 0 */
	0x00, 0x00, 0x00,	/* reg 0x00 = 0x00 delay 0 */
};
/* stock applies only the first 3 entries (gen_dev_set 0x8a798f0 loop) */
#define PS5169_USB_FLIP_SEQ_LEN 3
static const u8 ps5169_USB_remove_seq[] = {
	0x40, 0x80, 0x00,	/* reg 0x40 = 0x80 delay 0 */
	0x8d, 0x00, 0x00,	/* reg 0x8d = 0x00 delay 0 */
	0x90, 0x00, 0x00,	/* reg 0x90 = 0x00 delay 0 */
	0x00, 0x00, 0x00,	/* reg 0x00 = 0x00 delay 0 */
};
/* stock applies only the first 3 entries (gen_dev_set 0x8a798f0 loop) */
#define PS5169_USB_REMOVE_SEQ_LEN 3
static const u8 ps5169_DP_seq[] = {
	0x40, 0xa0, 0x00,	/* reg 0x40 = 0xa0 delay 0 */
	0xa0, 0x00, 0x00,	/* reg 0xa0 = 0x00 delay 0 */
	0xa1, 0x04, 0x00,	/* reg 0xa1 = 0x04 delay 0 */
	0x00, 0x00, 0x00,	/* reg 0x00 = 0x00 delay 0 */
};
/* stock applies only the first 3 entries (gen_dev_set 0x8a798f0 loop) */
#define PS5169_DP_SEQ_LEN 3
static const u8 ps5169_DP_FLIP_seq[] = {
	0x40, 0xb0, 0x00,	/* reg 0x40 = 0xb0 delay 0 */
	0xa0, 0x00, 0x00,	/* reg 0xa0 = 0x00 delay 0 */
	0xa1, 0x04, 0x00,	/* reg 0xa1 = 0x04 delay 0 */
	0x00, 0x00, 0x00,	/* reg 0x00 = 0x00 delay 0 */
};
/* stock applies only the first 3 entries (gen_dev_set 0x8a798f0 loop) */
#define PS5169_DP_FLIP_SEQ_LEN 3
static const u8 ps5169_DP_remove_seq[] = {
	0x40, 0x80, 0x00,	/* reg 0x40 = 0x80 delay 0 */
	0xa0, 0x02, 0x00,	/* reg 0xa0 = 0x02 delay 0 */
	0xa1, 0x00, 0x00,	/* reg 0xa1 = 0x00 delay 0 */
};
/* Stock applies only the first 3 entries (gen_dev_set 0x8a798f0 loop);
 * stock DP_remove rodata @0xa30e060 holds exactly 3 entries. */
#define PS5169_DP_REMOVE_SEQ_LEN 3
static const u8 ps5169_USB_AND_DP_seq[] = {
	0x40, 0xe0, 0x00,	/* reg 0x40 = 0xe0 delay 0 */
	0x8d, 0x01, 0x00,	/* reg 0x8d = 0x01 delay 0 */
	0x90, 0x01, 0x00,	/* reg 0x90 = 0x01 delay 0 */
	0xa0, 0x00, 0x00,	/* reg 0xa0 = 0x00 delay 0 */
	0xa1, 0x04, 0x00,	/* reg 0xa1 = 0x04 delay 0 */
};
#define PS5169_USB_AND_DP_SEQ_LEN 5
static const u8 ps5169_USB_AND_DP_FLIP_seq[] = {
	0x40, 0xf0, 0x00,	/* reg 0x40 = 0xf0 delay 0 */
	0x8d, 0x01, 0x00,	/* reg 0x8d = 0x01 delay 0 */
	0x90, 0x01, 0x00,	/* reg 0x90 = 0x01 delay 0 */
	0xa0, 0x00, 0x00,	/* reg 0xa0 = 0x00 delay 0 */
	0xa1, 0x04, 0x00,	/* reg 0xa1 = 0x04 delay 0 */
};
#define PS5169_USB_AND_DP_FLIP_SEQ_LEN 5
static const u8 ps5169_USB_AND_DP_remove_seq[] = {
	0x40, 0x80, 0x00,	/* reg 0x40 = 0x80 delay 0 */
	0xa0, 0x02, 0x00,	/* reg 0xa0 = 0x02 delay 0 */
	0x8d, 0x00, 0x00,	/* reg 0x8d = 0x00 delay 0 */
	0x90, 0x00, 0x00,	/* reg 0x90 = 0x00 delay 0 */
	0xa1, 0x00, 0x00,	/* reg 0xa1 = 0x00 delay 0 */
};
#define PS5169_USB_AND_DP_REMOVE_SEQ_LEN 5



/* priority: INT_MAX >= x >= 0 */
#define NOTIFIER_PRIORITY		1

/* Registers Address */
#define GEN_DEV_SET_REG			0x00

#define REDRIVER_REG_MAX		0x1f



/* Default Register Value */
#define GEN_DEV_SET_REG_DEFAULT		0xFB

/* Register bits */
/* General Device Settings Register Bits */







/* for type c cable */
enum plug_orientation {
	ORIENTATION_NONE,
	ORIENTATION_CC1,
	ORIENTATION_CC2,
};

/*
 * Three Modes of Operations:
 *  - One/Two ports of USB 3.1 Gen1/Gen2 (Default Mode)
 *  - Two lanes of DisplayPort 1.4 + One port of USB 3.1 Gen1/Gen2
 *  - Four lanes of DisplayPort 1.4
 */
enum operation_mode {
	OP_MODE_USB,	/* One/Two ports of USB */
	OP_MODE_DP,		/* DP 4 Lane and DP 2 Lane */
	OP_MODE_USB_AND_DP, /* One port of USB and DP 2 Lane */
};

/**
 * struct ssusb_redriver - representation of USB re-driver
 * @dev: struct device pointer
 * @regmap: used for I2C communication on accessing registers
 * @client: i2c client structure pointer
 * @config_work: used to configure re-driver
 * @redriver_wq: work queue used for @config_work
 * @usb_psy: structure that holds USB power supply status
 * @host_active: used to indicate USB host mode is enabled or not
 * @vbus_active: used to indicate USB device mode is enabled or not
 * @is_usb3: used to indicate USB3 or not
 * @typec_orientation: used to inditate Type C orientation
 * @op_mode: used to store re-driver operation mode
 * @extcon_usb: external connector used for USB host/device mode
 * @extcon_dp: external connector used for DP
 * @vbus_nb: used for vbus event reception
 * @id_nb: used for id event reception
 * @dp_nb: used for DP event reception
 * @panic_nb: used for panic event reception
 * @debug_root: debugfs entry for this context
 */
struct ssusb_redriver {
	struct device		*dev;
	struct regmap		*regmap;
	struct i2c_client	*client;

	struct work_struct	config_work;
	struct workqueue_struct *redriver_wq;

	struct power_supply	*usb_psy;
	bool host_active;
	bool vbus_active;
	bool is_usb3;
	enum plug_orientation typec_orientation;
	enum operation_mode op_mode;

	struct extcon_dev	*extcon_usb;
	struct extcon_dev	*extcon_dp;
	struct notifier_block	vbus_nb;
	struct notifier_block	id_nb;
	struct notifier_block	dp_nb;

	struct notifier_block	panic_nb;




	struct dentry	*debug_root;
};
/* Apply one stock sequence table. delay_ms != 0 -> msleep(delay), no
 * register write (stock probe loop 0xffffff8008a79018: ldrb delay; cbnz
 * -> msleep; else regmap_write(reg,val)). Returns the first regmap error
 * or 0; a delayed entry cannot fail. */
static int ps5169_apply_seq(struct ssusb_redriver *redriver,
			    const u8 *seq, size_t len)
{
	size_t i;
	/* Stock initialises the result to -EINVAL and keeps the result of
	 * the last write attempt (Image-b 0xa78fe0-0xa79104). */
	int ret = -EINVAL;

	for (i = 0; i < len; i++) {
		u8 reg = seq[i * 3 + 0];
		u8 val = seq[i * 3 + 1];
		u8 delay = seq[i * 3 + 2];

		if (delay) {
			msleep(delay);
			continue;
		}
		ret = regmap_write(redriver->regmap, reg, val);
		if (ret < 0) {
			dev_err(redriver->dev,
				"writing reg 0x%02x failure\n", reg);
			/* Stock prints a second message whose format string
			 * ("%s error: reg=0x%02x, value = 0x%02x\n") lacks
			 * the %s argument in the compiled binary - an upstream
			 * bug that cannot trigger on stock because a failed
			 * chip-id read returns before the sequence runs.
			 * Supply __func__ for the missing argument. */
			dev_err(redriver->dev,
				"%s error: reg=0x%02x, value = 0x%02x\n",
				__func__, reg, val);
		}
	}
	return ret;
}


static void ssusb_redriver_debugfs_entries(struct ssusb_redriver *redriver);

static int redriver_i2c_reg_set(struct ssusb_redriver *redriver,
		u8 reg, u8 val)
{
	int ret;

	ret = regmap_write(redriver->regmap, (unsigned int)reg,
			(unsigned int)val);
	if (ret < 0) {
		dev_err(redriver->dev, "writing reg 0x%02x failure\n", reg);
		return ret;
	}

	dev_dbg(redriver->dev, "writing reg 0x%02x=0x%02x\n", reg, val);

	return 0;
}

/**
 * Handle Re-driver chip operation mode and channel settings.
 *
 * Three Modes of Operations:
 *  - One/Two ports of USB 3.1 Gen1/Gen2 (Default Mode)
 *  - Two lanes of DisplayPort 1.4 + One port of USB 3.1 Gen1/Gen2
 *  - Four lanes of DisplayPort 1.4
 *
 * @redriver - contain redriver status
 * @on - re-driver chip enable or not
 */
static void ssusb_redriver_gen_dev_set(
		struct ssusb_redriver *redriver, bool on)
{
	/* Stock gen_dev_set (Image-b 0x8a79588-0x8a79a48, all call targets
	 * kallsyms-verified): op_mode x orientation x enable selects one of
	 * the hard-coded ps5169 sequence tables; the apply loop writes the
	 * first 3 entries for USB/DP paths (0x8a798f0) and 5 entries for
	 * USB_AND_DP paths (cmp x25,#0x5); an unset orientation prints
	 * "UNKNOWN_CC" with the per-mode helper name and line (rodata
	 * 0x9b8c392 + 0x9b8c3af/0x9b8c3c5/0x9b8c3da, lines 357/373/389),
	 * an unknown op_mode prints "Error: op mode: ..." (0x9b8c324) and
	 * both error paths then print "failure to (%s) the redriver chip,
	 * ..." (0x9b8c34d, %s = ENABLE/DISABLE). Success returns silently
	 * (0x8a79714). */
	const char *mode_name = NULL;
	int cc_line = 0;
	const u8 *seq = NULL;
	size_t len = 0;
	int ret = -EINVAL;

	switch (redriver->op_mode) {
	case OP_MODE_USB:
		mode_name = "redriver_set_USB_mode";
		cc_line = 357;
		if (!on) {
			seq = ps5169_USB_remove_seq;
			len = PS5169_USB_REMOVE_SEQ_LEN;
		} else if (redriver->typec_orientation
				== ORIENTATION_CC2) {
			seq = ps5169_USB_FLIP_seq;
			len = PS5169_USB_FLIP_SEQ_LEN;
		} else if (redriver->typec_orientation
				== ORIENTATION_CC1) {
			seq = ps5169_USB_seq;
			len = PS5169_USB_SEQ_LEN;
		}
		break;
	case OP_MODE_DP:
		mode_name = "redriver_set_DP_mode";
		cc_line = 373;
		if (!on) {
			seq = ps5169_DP_remove_seq;
			len = PS5169_DP_REMOVE_SEQ_LEN;
		} else if (redriver->typec_orientation
				== ORIENTATION_CC2) {
			seq = ps5169_DP_FLIP_seq;
			len = PS5169_DP_FLIP_SEQ_LEN;
		} else if (redriver->typec_orientation
				== ORIENTATION_CC1) {
			seq = ps5169_DP_seq;
			len = PS5169_DP_SEQ_LEN;
		}
		break;
	case OP_MODE_USB_AND_DP:
		mode_name = "redriver_set_USB_AND_DP_mode";
		cc_line = 389;
		if (!on) {
			seq = ps5169_USB_AND_DP_remove_seq;
			len = PS5169_USB_AND_DP_REMOVE_SEQ_LEN;
		} else if (redriver->typec_orientation
				== ORIENTATION_CC2) {
			seq = ps5169_USB_AND_DP_FLIP_seq;
			len = PS5169_USB_AND_DP_FLIP_SEQ_LEN;
		} else if (redriver->typec_orientation
				== ORIENTATION_CC1) {
			seq = ps5169_USB_AND_DP_seq;
			len = PS5169_USB_AND_DP_SEQ_LEN;
		}
		break;
	default:
		dev_err(redriver->dev,
			"Error: op mode: %d, vbus: %d, host: %d.\n",
			redriver->op_mode, redriver->vbus_active,
			redriver->host_active);
		break;
	}

	if (mode_name && !seq) {
		dev_err(redriver->dev, "%s:%d unknow cc orientation\n",
			mode_name, cc_line);
		ret = -EINVAL;
	} else if (seq) {
		ret = ps5169_apply_seq(redriver, seq, len);
	}

	if (ret < 0)
		dev_err(redriver->dev,
			"failure to (%s) the redriver chip, op mode: %d, vbus: %d, host: %d.\n",
			on ? "ENABLE" : "DISABLE",
			redriver->op_mode, redriver->vbus_active,
			redriver->host_active);
}

static void ssusb_redriver_config_work(struct work_struct *w)
{
	struct ssusb_redriver *redriver = container_of(w,
			struct ssusb_redriver, config_work);
	struct extcon_dev *edev = NULL;
	union extcon_property_value val;
	unsigned int extcon_id = EXTCON_NONE;
	int ret = 0;

	dev_dbg(redriver->dev, "%s: USB SS redriver config work\n",
			__func__);

	edev = redriver->extcon_usb;

	if (redriver->vbus_active)
		extcon_id = EXTCON_USB;
	else if (redriver->host_active)
		extcon_id = EXTCON_USB_HOST;

	if (edev && (extcon_id != EXTCON_NONE)
			&& extcon_get_state(edev, extcon_id)) {
		ret = extcon_get_property(edev, extcon_id,
					EXTCON_PROP_USB_SS, &val);
		if (!ret) {
			redriver->is_usb3 = (val.intval != 0);

			dev_dbg(redriver->dev, "SS Lane is used? [%s].\n",
				redriver->is_usb3 ? "true" : "false");
		} else {
			redriver->is_usb3 = true;

			dev_dbg(redriver->dev, "Default true as speed isn't reported.\n");
		}

		if (redriver->is_usb3 || (redriver->op_mode != OP_MODE_USB)) {
			ret = extcon_get_property(edev, extcon_id,
					EXTCON_PROP_USB_TYPEC_POLARITY, &val);
			if (!ret)
				redriver->typec_orientation = val.intval ?
					ORIENTATION_CC2 : ORIENTATION_CC1;
			else if (redriver->op_mode == OP_MODE_USB)
				redriver->typec_orientation = ORIENTATION_NONE;
			else
				dev_err(redriver->dev, "fail to get orientation when has DP.\n");

			ssusb_redriver_gen_dev_set(redriver, true);
		} else {
			dev_dbg(redriver->dev,
				"Disable chip when not in SS USB mode.\n");

			ssusb_redriver_gen_dev_set(redriver, false);
		}

		dev_dbg(redriver->dev, "Type C orientation code is %d.\n",
				redriver->typec_orientation);
	} else if (redriver->op_mode != OP_MODE_USB) {
		/*
		 * USB host stack will be turned off if peer doesn't
		 * support USB communication. PD driver will send
		 * id notification when disable host stack. Update
		 * redriver channel mode when operation mode changed.
		 */
		dev_dbg(redriver->dev,
				"Update redriver operation mode.\n");

		ssusb_redriver_gen_dev_set(redriver, true);
	} else {
		dev_dbg(redriver->dev, "USB Cable is disconnected.\n");

		/* Set back to USB only mode when cable disconnect */
		redriver->op_mode = OP_MODE_USB;

		ssusb_redriver_gen_dev_set(redriver, false);
	}
}

static int ssusb_redriver_dp_notifier(struct notifier_block *nb,
		unsigned long dp_lane, void *ptr)
{
	struct ssusb_redriver *redriver = container_of(nb,
			struct ssusb_redriver, dp_nb);
	enum operation_mode op_mode;

	dev_dbg(redriver->dev,
		"redriver op mode change: %ld event received\n", dp_lane);

	switch (dp_lane) {
	case 0:
		op_mode = OP_MODE_USB;
		break;
	case 2:
		op_mode = OP_MODE_USB_AND_DP;
		break;
	case 4:
		op_mode = OP_MODE_DP;
		break;
	default:
		return 0;
	}

	if (redriver->op_mode == op_mode)
		return 0;

	redriver->op_mode = op_mode;

	queue_work(redriver->redriver_wq, &redriver->config_work);

	return 0;
}

static int ssusb_redriver_vbus_notifier(struct notifier_block *nb,
	unsigned long event, void *ptr)
{
	struct ssusb_redriver *redriver = container_of(nb,
			struct ssusb_redriver, vbus_nb);

	dev_dbg(redriver->dev, "vbus:%ld event received\n", event);

	if (redriver->vbus_active == event)
		return NOTIFY_DONE;

	redriver->vbus_active = event;

	queue_work(redriver->redriver_wq, &redriver->config_work);

	return NOTIFY_DONE;
}

static int ssusb_redriver_id_notifier(struct notifier_block *nb,
	unsigned long event, void *ptr)
{
	struct ssusb_redriver *redriver = container_of(nb,
			struct ssusb_redriver, id_nb);
	bool host_active = (bool)event;

	dev_dbg(redriver->dev, "host_active:%s event received\n",
			host_active ? "true" : "false");

	if (redriver->host_active == host_active)
		return NOTIFY_DONE;

	redriver->host_active = host_active;

	queue_work(redriver->redriver_wq, &redriver->config_work);

	return NOTIFY_DONE;
}

static int ssusb_redriver_extcon_register(struct ssusb_redriver *redriver)
{
	struct device_node *node = redriver->dev->of_node;
	struct extcon_dev *edev;
	int ret = 0;

	if (!of_find_property(node, "extcon", NULL)) {
		dev_err(redriver->dev, "failed to get extcon for redriver\n");
		return 0;
	}

	edev = extcon_get_edev_by_phandle(redriver->dev, 0);
	if (IS_ERR(edev) && PTR_ERR(edev) != -ENODEV) {
		dev_err(redriver->dev, "failed to get phandle for redriver\n");
		return PTR_ERR(edev);
	}

	if (!IS_ERR(edev)) {
		redriver->extcon_usb = edev;

		redriver->vbus_nb.notifier_call = ssusb_redriver_vbus_notifier;
		redriver->vbus_nb.priority = NOTIFIER_PRIORITY;
		ret = extcon_register_notifier(edev, EXTCON_USB,
				&redriver->vbus_nb);
		if (ret < 0) {
			dev_err(redriver->dev,
				"failed to register notifier for redriver\n");
			return ret;
		}

		redriver->id_nb.notifier_call = ssusb_redriver_id_notifier;
		redriver->id_nb.priority = NOTIFIER_PRIORITY;
		ret = extcon_register_notifier(edev, EXTCON_USB_HOST,
				&redriver->id_nb);
		if (ret < 0) {
			dev_err(redriver->dev,
				"failed to register notifier for USB-HOST\n");
			goto err;
		}
	}

	edev = NULL;
	/* Use optional phandle (index 1) for DP lane events */
	if (of_count_phandle_with_args(node, "extcon", NULL) > 1) {
		edev = extcon_get_edev_by_phandle(redriver->dev, 1);
		if (IS_ERR(edev) && PTR_ERR(edev) != -ENODEV) {
			ret = PTR_ERR(edev);
			goto err1;
		}
	}

	if (!IS_ERR_OR_NULL(edev)) {
		redriver->extcon_dp = edev;
		redriver->dp_nb.notifier_call =
				ssusb_redriver_dp_notifier;
		redriver->dp_nb.priority = NOTIFIER_PRIORITY;
		ret = extcon_register_blocking_notifier(edev, EXTCON_DISP_DP,
				&redriver->dp_nb);
		if (ret < 0) {
			dev_err(redriver->dev,
				"failed to register blocking notifier\n");
			goto err1;
		}
	}

	/* Update initial VBUS/ID state from extcon */
	if (extcon_get_state(redriver->extcon_usb, EXTCON_USB))
		ssusb_redriver_vbus_notifier(&redriver->vbus_nb, true,
			redriver->extcon_usb);
	else if (extcon_get_state(redriver->extcon_usb, EXTCON_USB_HOST))
		ssusb_redriver_id_notifier(&redriver->id_nb, true,
				redriver->extcon_usb);

	return 0;

err1:
	if (redriver->extcon_usb)
		extcon_unregister_notifier(redriver->extcon_usb,
			EXTCON_USB_HOST, &redriver->id_nb);
err:
	if (redriver->extcon_usb)
		extcon_unregister_notifier(redriver->extcon_usb,
			EXTCON_USB, &redriver->vbus_nb);
	return ret;
}

static void ssusb_redriver_extcon_unregister(
		struct ssusb_redriver *redriver)
{
	if (redriver->extcon_dp)
		extcon_unregister_blocking_notifier(redriver->extcon_dp,
			EXTCON_DISP_DP, &redriver->dp_nb);
	if (redriver->extcon_usb) {
		extcon_unregister_notifier(redriver->extcon_usb, EXTCON_USB_HOST,
			&redriver->id_nb);
		extcon_unregister_notifier(redriver->extcon_usb, EXTCON_USB,
			&redriver->vbus_nb);
	}
}

static int ssusb_redriver_panic_notifier(struct notifier_block *this,
		unsigned long event, void *ptr)
{
	struct ssusb_redriver *redriver = container_of(this,
			struct ssusb_redriver, panic_nb);

	pr_err("%s: op mode: %d, vbus: %d, host: %d\n", __func__,
		redriver->op_mode, redriver->vbus_active,
		redriver->host_active);

	return NOTIFY_OK;
}

/* TASK-081 D85 (Round3 audit 1.13.5): stock ships this same ssusb-redriver
 * source rebuilt for the HQ PS5169 redriver (stock match_table@0x980c348
 * claims hq_redriver,ps5169, and ten config sequence tables live in
 * stock .data). The onnn,NB7VPQ904M DT path below is kept unchanged;
 * PS5169 devices get the stock hard-coded sequences instead. */
static const struct regmap_config redriver_regmap = {
	.max_register = REDRIVER_REG_MAX,
	.reg_bits = 8,
	.val_bits = 8,
};

static int redriver_i2c_probe(struct i2c_client *client,
			       const struct i2c_device_id *dev_id)
{
	struct ssusb_redriver *redriver;
	union power_supply_propval pval = {0};
	int ret;

	redriver = devm_kzalloc(&client->dev, sizeof(struct ssusb_redriver),
			GFP_KERNEL);
	if (!redriver)
		return -ENOMEM;

	INIT_WORK(&redriver->config_work, ssusb_redriver_config_work);

	redriver->redriver_wq = alloc_ordered_workqueue("redriver_wq",
			WQ_HIGHPRI);
	if (!redriver->redriver_wq) {
		dev_err(&client->dev,
			"%s: Unable to create workqueue redriver_wq\n",
			__func__);
		return -ENOMEM;
	}

	redriver->dev = &client->dev;

	redriver->regmap = devm_regmap_init_i2c(client, &redriver_regmap);
	if (IS_ERR(redriver->regmap)) {
		ret = PTR_ERR(redriver->regmap);
		dev_err(&client->dev,
			"Failed to allocate register map: %d\n", ret);
		goto destroy_wq;
	}

	redriver->client = client;
	i2c_set_clientdata(client, redriver);

	/* Stock probe (Image-b 0x8a78e20) is a single path - no chip-type
	 * check and no of_device_get_match_data call; the match table holds
	 * only "hq_redriver,ps5169" with a NULL data pointer.
	 * 0x8a78efc-0x8a78f98: read the four config echoes
	 * 0xad/0xac/0xae/0xaf, then apply the 28-entry
	 * ps5169_default_seq. */
	static const u8 echo_regs[4] = { 0xad, 0xac, 0xae, 0xaf };
	u8 echo[4];
	unsigned int k;

	for (k = 0; k < 4; k++) {
		unsigned int val_tmp;

		ret = regmap_read(redriver->regmap, echo_regs[k],
				  &val_tmp);
		if (ret < 0) {
			dev_err(redriver->dev,
				"reading reg 0x%02x failure\n",
				echo_regs[k]);
			break;
		}
		echo[k] = (u8)val_tmp;
	}
	if (ret < 0) {
		/* Stock forces -EINVAL regardless of the i2c error
		 * and fails the probe (Image-b 0xa79098-0xa790b8). */
		ret = -EINVAL;
		dev_err(redriver->dev,
			"Failed to get chip id: %d\n", ret);
		goto destroy_wq;
	}
	dev_err(redriver->dev,
		"get chip id: id_1 = 0x%02x, id_2 = 0x%02x, revision_1 = 0x%02x, revision_2 = 0x%02x\n",
		echo[0], echo[1], echo[2], echo[3]);
	redriver_exist = true;

	redriver->host_active = false;
	redriver->vbus_active = false;

	ret = ps5169_apply_seq(redriver, ps5169_default_seq,
			       PS5169_DEFAULT_SEQ_LEN);
	if (ret < 0) {
		dev_err(redriver->dev,
			"Failed to set default mode: %d\n", ret);
		goto destroy_wq;
	}

	/* Set id_state as float by default*/
	redriver->host_active = false;

	/* Set to USB by default */
	redriver->op_mode = OP_MODE_USB;

	redriver->usb_psy = power_supply_get_by_name("usb");
	if (!redriver->usb_psy) {
		dev_warn(&client->dev, "Could not get usb power_supply\n");
		pval.intval = -EINVAL;
	} else {
		power_supply_get_property(redriver->usb_psy,
			POWER_SUPPLY_PROP_PRESENT, &pval);

		/* USB cable is not connected */
		if (!pval.intval)
			ssusb_redriver_gen_dev_set(redriver, false);
	}

	ret = ssusb_redriver_extcon_register(redriver);
	if (ret)
		goto put_psy;

	redriver->panic_nb.notifier_call = ssusb_redriver_panic_notifier;
	atomic_notifier_chain_register(&panic_notifier_list,
			&redriver->panic_nb);

	ssusb_redriver_debugfs_entries(redriver);
	dev_dbg(&client->dev, "USB 3.1 Gen1/Gen2 Re-Driver Probed.\n");

	return 0;

put_psy:
	if (redriver->usb_psy)
		power_supply_put(redriver->usb_psy);

destroy_wq:
	destroy_workqueue(redriver->redriver_wq);

	return ret;
}

static int redriver_i2c_remove(struct i2c_client *client)
{
	struct ssusb_redriver *redriver = i2c_get_clientdata(client);

	debugfs_remove_recursive(redriver->debug_root);
	atomic_notifier_chain_unregister(&panic_notifier_list,
			&redriver->panic_nb);
	ssusb_redriver_extcon_unregister(redriver);
	cancel_work_sync(&redriver->config_work);

	if (redriver->usb_psy)
		power_supply_put(redriver->usb_psy);

	destroy_workqueue(redriver->redriver_wq);
	redriver_exist = false;

	return 0;
}

static void ssusb_redriver_debugfs_entries(
		struct ssusb_redriver *redriver)
{
	redriver->debug_root = debugfs_create_dir("ssusb_redriver", NULL);
	if (!redriver->debug_root) {
		dev_warn(redriver->dev, "Couldn't create debug dir\n");
		return;
	}

}

static int __maybe_unused redriver_i2c_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ssusb_redriver *redriver = i2c_get_clientdata(client);

	dev_dbg(redriver->dev, "%s: SS USB redriver suspend.\n",
			__func__);

	/* Disable redriver chip when USB cable disconnected */
	if ((!redriver->vbus_active && !redriver->host_active &&
	     redriver->op_mode != OP_MODE_DP) ||
	    (redriver->host_active &&
	     redriver->op_mode == OP_MODE_USB_AND_DP))
		ssusb_redriver_gen_dev_set(redriver, false);

	flush_workqueue(redriver->redriver_wq);

	return 0;
}

static int __maybe_unused redriver_i2c_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ssusb_redriver *redriver = i2c_get_clientdata(client);

	dev_dbg(redriver->dev, "%s: SS USB redriver resume.\n",
			__func__);

	if (redriver->host_active &&
	    redriver->op_mode == OP_MODE_USB_AND_DP)
		ssusb_redriver_gen_dev_set(redriver, true);

	flush_workqueue(redriver->redriver_wq);

	return 0;
}

static SIMPLE_DEV_PM_OPS(redriver_i2c_pm, redriver_i2c_suspend,
			 redriver_i2c_resume);

static void redriver_i2c_shutdown(struct i2c_client *client)
{
	struct ssusb_redriver *redriver = i2c_get_clientdata(client);
	int ret;

	/* Set back to USB mode with four channel enabled */
	ret = redriver_i2c_reg_set(redriver, GEN_DEV_SET_REG,
			GEN_DEV_SET_REG_DEFAULT);
	if (ret < 0)
		dev_err(&client->dev,
			"%s: fail to set USB mode with 4 channel enabled.\n",
			__func__);
	else
		dev_dbg(&client->dev,
			"%s: successfully set back to USB mode.\n",
			__func__);
}

static const struct of_device_id redriver_match_table[] = {
	{ .compatible = "hq_redriver,ps5169" },
	{ },
};

static const struct i2c_device_id redriver_i2c_id[] = {
	{ "ssusb redriver", 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, redriver_i2c_id);

static struct i2c_driver redriver_i2c_driver = {
	.driver = {
		.name	= "ssusb redriver",
		.of_match_table	= redriver_match_table,
		.pm	= &redriver_i2c_pm,
	},

	.probe		= redriver_i2c_probe,
	.remove		= redriver_i2c_remove,

	.shutdown	= redriver_i2c_shutdown,

	.id_table	= redriver_i2c_id,
};

module_i2c_driver(redriver_i2c_driver);

MODULE_DESCRIPTION("USB Super Speed Linear Re-Driver Driver");
MODULE_LICENSE("GPL v2");
