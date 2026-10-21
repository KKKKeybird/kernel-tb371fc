// SPDX-License-Identifier: GPL-2.0-only
/*
 * NXP CTN730 stylus/pen manager driver, TB371FC stock-parity rebuild.
 * Specification source: original firmware Image reverse engineering only.
 */

#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/alarmtimer.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/device.h>
#include <linux/ktime.h>
#include <linux/time.h>
#include <linux/sysfs.h>
#include <drm/drm_panel.h>

extern void nvt_support_pen_set_nfc(bool normal);

#define CTN730_STATUS_TEXT_MAX	27

int tp_state;
static int is_hard_reset;
static struct ctn730_data *ctn730;
static struct i2c_client *client_address;
static struct device_node *ctn730_panel;

static const char * const ctn730_charge_state_text[] = {
	"Unknown", "Charging", "Full", "Notcharging",
};

static const char * const ctn730_status_text[] = {
	"status_unkown", "download_mode", "initialized", "detecting",
	"detected", "deactived", "lost", "version_missmatch",
	"host_request_dischg", "rf_on", "rf_off", "mac_getted",
	"app_chn_enabled", "app_chn_disabled", "app_chn_connected",
	"app_chn_reciced", "need_to_app_chmn_enable",
	"need_to_host_ctrl_enable_send_charging_i",
	"need_to_host_ctrl_disable_send_charging_",
	"need_to_rf_on", "need_to_rf_off", "dl_open_session_ok",
	"dl_flash_ok", "dl_commit_ok", "update_firmware_err",
	"need_dowload_firmware", "bist_err",
};

/*
 * Field layout mirrors the stock driver byte for byte (total 0x578) so
 * that every `[xN, #imm]` in the stock disassembly maps 1:1 onto a named
 * member here. Reserved arrays stand for stock padding; never reorder.
 * Offsets were derived from the stock Image disassembly and are enforced
 * by the _Static_assert block below.
 */
struct ctn730_data {
	struct device *dev;			/* 0x000 */
	struct i2c_client *client;		/* 0x008 */
	struct miscdevice misc;			/* 0x010 (size 0x50) */
	int level;				/* 0x060 */
	int pen_info_attached;			/* 0x064 */
	int pending_event_index;		/* 0x068 */
	u8 mac[6];				/* 0x06c */
	u8 pen_info_buf[8];			/* 0x072 */
	u8 rsv0[6];				/* 0x07a..0x07f */
	void *fw_section_data;			/* 0x080 */
	u32 fw_section_len;			/* 0x088 */
	u32 fw_data_len;			/* 0x08c */
	u32 fw_section_len2;			/* 0x090 */
	u32 fw_section_count;			/* 0x094 */
	u32 fw_data_addr;			/* 0x098 */
	u32 fw_data2_addr;			/* 0x09c */
	u32 fw_crc_val;				/* 0x0a0 */
	u16 fw_len;				/* 0x0a4 */
	u8 rsv1[2];				/* 0x0a6 */
	struct blocking_notifier_head notifier_head;	/* 0x0a8 */
	u8 rsv2[0x40 - sizeof(struct blocking_notifier_head)];
	int rf_on;				/* 0x0e8 */
	int gpio_ven;				/* 0x0ec */
	int gpio_rst;				/* 0x0f0 */
	int gpio_irq;				/* 0x0f4 */
	int irq;				/* 0x0f8 */
	int err_count;				/* 0x0fc */
	u8 rsv3;				/* 0x100 */
	u8 charging;				/* 0x101 */
	u8 has_ext_cmd;				/* 0x102 */
	u8 pen_status;				/* 0x103 */
	u8 enabled;				/* 0x104 */
	u8 fw_ver_sel;				/* 0x105 */
	u8 suspended;				/* 0x106 */
	u8 detected;				/* 0x107 */
	u8 has_mac;				/* 0x108 */
	u8 rsv4[3];				/* 0x109 */
	int pending_event;			/* 0x10c */
	u8 rsv5[8];				/* 0x110..0x117 */
	struct delayed_work set_work;		/* 0x118 */
	struct delayed_work reset_work;		/* 0x1a0 */
	struct delayed_work update_fw_work;	/* 0x228 */
	struct delayed_work rf_on_work;		/* 0x2b0 */
	struct delayed_work check_static_charge_work;	/* 0x338 */
	struct delayed_work check_pen_lost;	/* 0x3c0 */
	struct alarm alarm;			/* 0x448 */
	u8 mac_or_param[8];			/* 0x4c8 */
	u8 pen_uid[8];				/* 0x4d0 */
	u8 rsv6[8];				/* 0x4d8 */
	struct notifier_block drm_nb;		/* 0x4e0 */
	struct mutex bist_mutex;		/* 0x4f8 */
	struct mutex report_info_mutex;		/* 0x518 */
	int fw_status;				/* 0x538 */
	int pen_delay;				/* 0x53c */
	int pen_mode_open;			/* 0x540 */
	u8 report_info;				/* 0x544 */
	u8 need_report_info;			/* 0x545 */
	u8 param[0x22];				/* 0x546..0x567 */
	int cmd_delay;				/* 0x568 */
	u8 version[2];				/* 0x56c */
	u8 rsv7[2];				/* 0x56e */
	ktime_t last_irq_ktime;			/* 0x570 */
};

_Static_assert(offsetof(struct ctn730_data, client) == 0x008, "client");
_Static_assert(offsetof(struct ctn730_data, misc) == 0x010, "misc");
_Static_assert(offsetof(struct ctn730_data, level) == 0x060, "level");
_Static_assert(offsetof(struct ctn730_data, pen_info_attached) == 0x064, "attached");
_Static_assert(offsetof(struct ctn730_data, pending_event_index) == 0x068, "pe_idx");
_Static_assert(offsetof(struct ctn730_data, mac) == 0x06c, "mac");
_Static_assert(offsetof(struct ctn730_data, fw_section_data) == 0x080, "fw_sect");
_Static_assert(offsetof(struct ctn730_data, fw_section_len) == 0x088, "fw_len1");
_Static_assert(offsetof(struct ctn730_data, fw_section_count) == 0x094, "fw_cnt");
_Static_assert(offsetof(struct ctn730_data, fw_crc_val) == 0x0a0, "fw_crc");
_Static_assert(offsetof(struct ctn730_data, fw_len) == 0x0a4, "fw_len");
_Static_assert(offsetof(struct ctn730_data, notifier_head) == 0x0a8, "notifier");
_Static_assert(offsetof(struct ctn730_data, rf_on) == 0x0e8, "rf_on");
_Static_assert(offsetof(struct ctn730_data, gpio_ven) == 0x0ec, "ven");
_Static_assert(offsetof(struct ctn730_data, gpio_rst) == 0x0f0, "rst");
_Static_assert(offsetof(struct ctn730_data, gpio_irq) == 0x0f4, "irq_gpio");
_Static_assert(offsetof(struct ctn730_data, irq) == 0x0f8, "irq");
_Static_assert(offsetof(struct ctn730_data, err_count) == 0x0fc, "err_count");
_Static_assert(offsetof(struct ctn730_data, charging) == 0x101, "charging");
_Static_assert(offsetof(struct ctn730_data, has_ext_cmd) == 0x102, "has_ext_cmd");
_Static_assert(offsetof(struct ctn730_data, pen_status) == 0x103, "pen_status");
_Static_assert(offsetof(struct ctn730_data, enabled) == 0x104, "enabled");
_Static_assert(offsetof(struct ctn730_data, fw_ver_sel) == 0x105, "fw_ver_sel");
_Static_assert(offsetof(struct ctn730_data, suspended) == 0x106, "suspended");
_Static_assert(offsetof(struct ctn730_data, detected) == 0x107, "detected");
_Static_assert(offsetof(struct ctn730_data, has_mac) == 0x108, "has_mac");
_Static_assert(offsetof(struct ctn730_data, pending_event) == 0x10c, "pending_ev");
_Static_assert(offsetof(struct ctn730_data, set_work) == 0x118, "set_work");
_Static_assert(offsetof(struct ctn730_data, reset_work) == 0x1a0, "reset_work");
_Static_assert(offsetof(struct ctn730_data, update_fw_work) == 0x228, "update_fw");
_Static_assert(offsetof(struct ctn730_data, rf_on_work) == 0x2b0, "rf_on_work");
_Static_assert(offsetof(struct ctn730_data, check_static_charge_work) == 0x338, "stat_chg");
_Static_assert(offsetof(struct ctn730_data, check_pen_lost) == 0x3c0, "pen_lost");
_Static_assert(offsetof(struct ctn730_data, alarm) == 0x448, "alarm");
_Static_assert(offsetof(struct ctn730_data, mac_or_param) == 0x4c8, "mac_or_param");
_Static_assert(offsetof(struct ctn730_data, pen_uid) == 0x4d0, "pen_uid");
_Static_assert(offsetof(struct ctn730_data, drm_nb) == 0x4e0, "drm_nb");
_Static_assert(offsetof(struct ctn730_data, bist_mutex) == 0x4f8, "bist_mutex");
_Static_assert(offsetof(struct ctn730_data, report_info_mutex) == 0x518, "rep_mutex");
_Static_assert(offsetof(struct ctn730_data, fw_status) == 0x538, "fw_status");
_Static_assert(offsetof(struct ctn730_data, pen_delay) == 0x53c, "pen_delay");
_Static_assert(offsetof(struct ctn730_data, pen_mode_open) == 0x540, "pen_mode");
_Static_assert(offsetof(struct ctn730_data, report_info) == 0x544, "report_info");
_Static_assert(offsetof(struct ctn730_data, need_report_info) == 0x545, "need_report");
_Static_assert(offsetof(struct ctn730_data, param) == 0x546, "param");
_Static_assert(offsetof(struct ctn730_data, cmd_delay) == 0x568, "cmd_delay");
_Static_assert(offsetof(struct ctn730_data, version) == 0x56c, "version");
_Static_assert(offsetof(struct ctn730_data, last_irq_ktime) == 0x570, "last_irq");
_Static_assert(offsetof(struct ctn730_data, misc.parent) == 0x38, "misc.parent");
_Static_assert(offsetof(struct ctn730_data, misc.this_device) == 0x40, "misc.this_device");
_Static_assert(sizeof(struct ctn730_data) == 0x578, "total size");

static struct lock_class_key report_info_mutex_key;

/*
 * The original registers its uevent() through the misc device's
 * device_type and calls device_create_file() on misc.this_device, so
 * dev_get_drvdata() inside uevent()/attrs yields &data->misc, not the
 * driver-private struct. Convert back with container_of().
 */
static int ctn730_uevent(struct device *dev, struct kobj_uevent_env *env);

static struct device_type ctn730_dev_type = {
	.name = "nfc_charger",
	.uevent = ctn730_uevent,
};

static inline struct ctn730_data *ctn730_from_dev(struct device *dev)
{
	struct miscdevice *misc = dev_get_drvdata(dev);

	if (!misc || misc == (struct miscdevice *)0x10)
		return NULL;
	return container_of(misc, struct ctn730_data, misc);
}

static ssize_t enable_show(struct device *dev, struct device_attribute *attr,
			   char *buf);
static ssize_t enable_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count);
static ssize_t pen_cmd_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count);
static ssize_t reget_mac_store(struct device *dev, struct device_attribute *attr,
			       const char *buf, size_t count);
static ssize_t info_report_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count);
static ssize_t rf_on_store(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count);
static ssize_t param_store(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count);
static ssize_t charge_state_show(struct device *dev,
				 struct device_attribute *attr, char *buf);
static ssize_t mac_show(struct device *dev, struct device_attribute *attr,
			char *buf);
static ssize_t version_show(struct device *dev, struct device_attribute *attr,
			    char *buf);
static ssize_t level_show(struct device *dev, struct device_attribute *attr,
			  char *buf);
static ssize_t attached_show(struct device *dev, struct device_attribute *attr,
			     char *buf);
static ssize_t status_show(struct device *dev, struct device_attribute *attr,
			   char *buf);
static ssize_t cn_test_show(struct device *dev, struct device_attribute *attr,
			    char *buf);

static struct device_attribute dev_attr_enable =
	__ATTR(enable, 0644, enable_show, enable_store);
static struct device_attribute dev_attr_pen_cmd =
	__ATTR(pen_cmd, 0200, NULL, pen_cmd_store);
static struct device_attribute dev_attr_reget_mac =
	__ATTR(reget_mac, 0200, NULL, reget_mac_store);
static struct device_attribute dev_attr_info_report =
	__ATTR(info_report, 0200, NULL, info_report_store);
static struct device_attribute dev_attr_rf_on =
	__ATTR(rf_on, 0200, NULL, rf_on_store);
static struct device_attribute dev_attr_param =
	__ATTR(param, 0200, NULL, param_store);
static struct device_attribute dev_attr_charge_state =
	__ATTR(charge_state, 0444, charge_state_show, NULL);
static struct device_attribute dev_attr_mac =
	__ATTR(mac, 0444, mac_show, NULL);
static struct device_attribute dev_attr_version =
	__ATTR(version, 0444, version_show, NULL);
static struct device_attribute dev_attr_level =
	__ATTR(level, 0444, level_show, NULL);
static struct device_attribute dev_attr_attached =
	__ATTR(attached, 0444, attached_show, NULL);
static struct device_attribute dev_attr_status =
	__ATTR(status, 0444, status_show, NULL);
static struct device_attribute dev_attr_cn_test =
	__ATTR(cn_test, 0444, cn_test_show, NULL);

int check_tp_state(unsigned long enable)
{
	struct ctn730_data *data;

	if (!client_address)
		return 1;
	data = i2c_get_clientdata(client_address);
	if (!data) {
		pr_info("ctn730: %s: failed to get CTN730\n", "check_tp_state");
		return 1;
	}

	if (!(enable & 1)) {
		tp_state = 0;
		pr_info("ctn730: %s: TP is not ready!\n", "check_tp_state");
		return 0;
	}
	tp_state = 1;
	nvt_support_pen_set_nfc(data->pen_info_attached != 1);
	pr_info("ctn730: %s: TP is ready!\n", "check_tp_state");
	return 0;
}

int ctn730_read_reg(struct ctn730_data *data)
{
	int ret = i2c_smbus_read_word_data(data->client, 0);

	if (ret < 0)
		dev_err(&data->client->dev, "%s: err %d\n",
			"ctn730_read_reg", ret);
	return ret;
}

int ctn730_write_reg(struct ctn730_data *data, u16 val)
{
	int ret = i2c_smbus_write_word_data(data->client, 0, val);

	if (ret < 0)
		dev_err(&data->client->dev, "%s: err %d\n",
			"ctn730_write_reg", ret);
	msleep(10);
	return ret;
}

/* stock ctn730_notifier_call_chain @0xffffff8008882b58 (global T):
 * blocking_notifier_call_chain(data + 0xa8, ...) -- the notifier_head IS at
 * offset 0x0a8, asserted above, so &data->notifier_head is the same pointer.
 * Kept as (data, val) for the in-file callers; NULL cookie matches stock's
 * callers in ctn730_parse_message/set_work.
 */
void ctn730_notifier_call_chain(struct ctn730_data *data, unsigned long val)
{
	blocking_notifier_call_chain(&data->notifier_head, val, NULL);
}

/* Exported pen/keyboard-hook API. Stock bodies are these thin wrappers on the
 * global ctn730 (Image-b 0x8882b08 / 0x8882b30) -- no NULL check, matching
 * stock exactly. Consumed in-tree by drivers/input/touchscreen/goodix
 * (goodix_ts_core.c: extern decl + register in stage2_init, unregister in
 * goodix_ts_remove); stock also exports both via EXPORT_SYMBOL.
 */
int ctn730_notifier_register(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&ctn730->notifier_head, nb);
}
EXPORT_SYMBOL(ctn730_notifier_register);

int ctn730_notifier_unregister(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&ctn730->notifier_head, nb);
}
EXPORT_SYMBOL(ctn730_notifier_unregister);

int is_stylus_attached(void)
{
	if (!ctn730)
		return 0;
	return ctn730->pen_info_attached;
}

static void ctn730_uevent_change(struct ctn730_data *data)
{
	/* stock: uevent is sent on misc.this_device, not the i2c client */
	kobject_uevent(&data->misc.this_device->kobj, KOBJ_CHANGE);
}

static int ctn730_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	struct ctn730_data *data = ctn730_from_dev(dev);
	int ret;

	if (!data) {
		pr_info("ctn730: %s: no ctn730_dev\n", "ctn730_uevent");
		return -ENODEV;
	}
	if (data->pending_event == 1) {
		ret = add_uevent_var(env, "WRITEPENDATA=0");
		data->pending_event = 0;
		if (ret)
			dev_err(dev, "failed to add uevent STATUS\n");
	} else if (data->pending_event == 2) {
		ret = add_uevent_var(env, "WRITEPENDATA=1");
		data->pending_event = 0;
		if (ret)
			dev_err(dev, "failed to add uevent STATUS\n");
	}
	ret = add_uevent_var(env, "ATTACHED=%d", data->pen_info_attached);
	if (ret)
		dev_err(dev, "failed to add uevent STATUS\n");
	ret = add_uevent_var(env, "CHARGING_STATE=%s",
			     ctn730_charge_state_text[data->pending_event_index]);
	if (ret)
		dev_err(dev, "failed to add uevent CHARGING_STATE\n");
	if (data->has_mac && data->level != -1) {
		ret = add_uevent_var(env, "LEVEL=%d", data->level);
		if (ret)
			dev_err(dev, "failed to add uevent LEVEL\n");
		data->has_mac = 0;
	}
	return add_uevent_var(env, "MAC=%02X:%02X:%02X:%02X:%02X:%02X",
			      data->mac[5], data->mac[4], data->mac[3],
			      data->mac[2], data->mac[1], data->mac[0]);
}

static int ctn730_drm_notifier_callback(struct notifier_block *nb,
					unsigned long event, void *data)
{
	struct ctn730_data *priv = container_of(nb, struct ctn730_data, drm_nb);
	struct drm_panel_notifier *panel_data = data;
	int *blank;

	if (!priv) {
		pr_info("ctn730: %s: no ctn730_dev\n",
			"ctn730_drm_notifier_callback");
		return -ENODEV;
	}
	if (!panel_data) {
		dev_err(priv->dev, "ctn730: %s: no data\n",
			"ctn730_drm_notifier_callback");
		return NOTIFY_BAD;
	}
	blank = panel_data->data;

	if (event == DRM_PANEL_EARLY_EVENT_BLANK && blank && *blank == 0) {
		mutex_lock(&priv->report_info_mutex);
		priv->need_report_info = 1;
		mutex_unlock(&priv->report_info_mutex);
		pr_info("ctn730: %s: need_report_info:%d report_info:%d\n",
			"ctn730_drm_notifier_callback", priv->need_report_info,
			priv->report_info);
		if (priv->pending_event_index == 1 && priv->charging) {
			priv->fw_status = 0x11;
			queue_delayed_work_on(8, system_wq, &priv->set_work, 0);
		}
	} else if (event == DRM_PANEL_EVENT_BLANK && blank && *blank == 1) {
		mutex_lock(&priv->report_info_mutex);
		priv->need_report_info = 0;
		mutex_unlock(&priv->report_info_mutex);
		pr_info("ctn730: %s: need_report_info:%d report_info:%d\n",
			"ctn730_drm_notifier_callback", priv->need_report_info,
			priv->report_info);
		if (priv->pending_event_index == 1 && priv->charging) {
			priv->fw_status = 0x12;
			queue_delayed_work_on(8, system_wq, &priv->set_work, 0);
		}
	} else {
		/* stock indexes status_text with fw_status (data+0x538) */
		pr_info("ctn730: %s: status: %s have_mac %d,no charging info cmd\n",
			"ctn730_drm_notifier_callback",
			ctn730_status_text[priv->fw_status],
			priv->has_mac);
	}
	return NOTIFY_OK;
}

static int ctn730_i2c_send(struct ctn730_data *data, void *buf, size_t len)
{
	int ret;

	if (!data) {
		pr_info("ctn730: %s: no ctn730_dev\n", "ctn730_i2c_send");
		return -ENODEV;
	}
	pr_debug("ctn730: %s: %*ph\n", "ctn730_i2c_send", (int)len, buf);
	if (!data->enabled)
		return len;

	ret = i2c_transfer_buffer_flags(data->client, buf, len, 0);
	if (ret == (int)len)
		return ret;
	if (ret == -EREMOTEIO) {
		msleep(1);
		ret = i2c_transfer_buffer_flags(data->client, buf, len, 0);
		if (ret == (int)len) {
			pr_info("ctn730: %s: %s : i2c_master_send retried ok\n",
				"ctn730_i2c_send", "ctn730_i2c_send");
			return ret;
		}
		queue_delayed_work_on(8, system_wq, &data->update_fw_work,
				      msecs_to_jiffies(1));
		return -EBUSY;
	}
	if (data->fw_status == 2) {
		queue_delayed_work_on(8, system_wq, &data->update_fw_work,
				      msecs_to_jiffies(25));
	} else if (data->fw_status == 1) {
		queue_delayed_work_on(8, system_wq, &data->rf_on_work,
				      msecs_to_jiffies(25));
	} else {
		queue_delayed_work_on(8, system_wq, &data->set_work,
				      msecs_to_jiffies(25));
	}
	return -EBUSY;
}

static void ctn730_set_ven(struct ctn730_data *data, int value)
{
	if (data->gpio_ven < 0x500)
		gpiod_direction_output_raw(gpio_to_desc(data->gpio_ven), value);
}

static void ctn730_parse_message(struct ctn730_data *data, u8 cmd, u8 count,
				 u8 *payload);

static void set_work(struct work_struct *work)
{
	struct ctn730_data *data = container_of(to_delayed_work(work),
						struct ctn730_data, set_work);
	/*
	 * stock stack buffer at sp+0x4: memset extent is 0xa in every branch
	 * except key 14, which memsets 0x104 (260) bytes before memcpy().
	 * Size the buffer for the largest extent to keep key 14 parity.
	 */
	u8 buf[0x104];
	int ret;

	if (!data->dev) {
		pr_info("ctn730: %s: no ctn730_dev\n", "set_work");
		return;
	}

	/* stock: data+0xe8 != 0 -> "has ext_cmd, skip" */
	if (data->rf_on != 0) {
		pr_info("ctn730: %s: has ext_cmd, skip\n", "set_work");
		return;
	}

	/*
	 * stock: switch (data->fw_status) keys 1..26, jump table at
	 * 0xffffff80097da770, base 0xffffff8008884080.
	 * Keys 3/5/6/7/12/13/21/22/23/24/25 fall through to 0x4328,
	 * i.e. they reach the key 18 stack-clear path but take no command
	 * action and return with the stack cookie check.
	 */
	switch (data->fw_status) {
	case 1:		/* key 1, stock 0xffffff800888409c */
	case 0x1a:	/* key 26, stock 0xffffff800888409c */
		memset(buf, 0, sizeof(buf));
		buf[0] = 0x00;
		buf[1] = 0x01;
		buf[2] = 0x00;
		ret = ctn730_i2c_send(data, buf, 3);
		if (ret >= 0)
			data->detected = 1;	/* stock [x19,#263] */
		break;
	case 2:		/* key 2, stock 0xffffff80088840fc */
		memset(buf, 0, sizeof(buf));
		buf[0] = 0x10;
		buf[1] = 0x02;
		buf[2] = 0xf4;
		buf[3] = 0x01;
		ctn730_i2c_send(data, buf, 4);
		break;
	case 4:		/* key 4, stock 0xffffff8008884134 */
		if (data->charging)
			break;
		memset(buf, 0, sizeof(buf));
		buf[0] = 0x11;
		buf[1] = 0x00;
		ctn730_i2c_send(data, buf, 2);
		data->pen_status = 1;	/* stock [x19,#259] */
		break;
	case 8:		/* key 8, stock 0xffffff8008884170 */
		memset(buf, 0, sizeof(buf));
		buf[0] = 0x06;
		buf[1] = 0x01;
		buf[2] = 0x01;
		mutex_lock(&data->bist_mutex);
		ret = ctn730_i2c_send(data, buf, 3);
		if (ret >= 0)
			data->pen_delay = 1;	/* stock [x19,#1340] */
		mutex_unlock(&data->bist_mutex);
		break;
	case 9:		/* key 9, stock 0xffffff80088841b8 */
		if (data->charging)
			break;
		memset(buf, 0, sizeof(buf));
		buf[0] = 0x06;
		buf[1] = 0x03;
		buf[2] = 0x09;
		buf[3] = 0x10;
		buf[4] = 0x01;
		pr_info("ctn730: %s: %*ph\n", "ctn730_get_mac", 5, buf);
		mutex_lock(&data->bist_mutex);
		ret = ctn730_i2c_send(data, buf, 5);
		if (ret >= 0)
			data->pen_delay = 9;	/* stock [x19,#1340] */
		mutex_unlock(&data->bist_mutex);
		break;
	case 10:	/* key 10, stock 0xffffff8008884230 */
		memset(buf, 0, sizeof(buf));
		buf[0] = 0x10;
		buf[1] = 0x02;
		buf[2] = 0xf4;
		buf[3] = 0x01;
		ctn730_i2c_send(data, buf, 4);
		data->pen_status = 0;	/* stock [x19,#259] = 0 */
		break;
	case 11:	/* key 11, stock 0xffffff8008884080 */
		memset(buf, 0, sizeof(buf));
		buf[0] = 0x06;
		buf[1] = 0x01;
		buf[2] = 0x02;
		mutex_lock(&data->bist_mutex);
		ret = ctn730_i2c_send(data, buf, 3);
		if (ret >= 0)
			data->pen_delay = 2;	/* stock [x19,#1340] */
		mutex_unlock(&data->bist_mutex);
		break;
	case 14:	/* key 14, stock 0xffffff8008884270 */
		memset(buf, 0, 0x104);	/* stock: memset(sp+4, 0, 0x104) */
		buf[0] = 0x23;
		buf[1] = data->cmd_delay + 1;
		buf[2] = 0x01;
		memcpy(&buf[3], data->param, data->cmd_delay);
		ctn730_i2c_send(data, buf, data->cmd_delay + 3);
		break;
	case 15:	/* key 15, stock 0xffffff80088842c0 */
		memset(buf, 0, sizeof(buf));
		buf[0] = 0x21;
		buf[1] = 0x00;
		ctn730_i2c_send(data, buf, 2);
		break;
	case 16:	/* key 16, stock 0xffffff80088842d8 */
		memset(buf, 0, sizeof(buf));
		buf[0] = 0x20;
		buf[1] = 0x00;
		ctn730_i2c_send(data, buf, 2);
		break;
	case 17:	/* key 17, stock 0xffffff80088842f0 */
		memset(buf, 0, sizeof(buf));
		buf[0] = 0x07;
		buf[1] = 0x02;
		buf[2] = 0x00;
		buf[3] = 0x00;
		ret = ctn730_i2c_send(data, buf, 4);
		if (ret >= 0) {
			mutex_lock(&data->report_info_mutex);
			data->pen_mode_open = 1;	/* stock [x19,#1344] */
			mutex_unlock(&data->report_info_mutex);
		}
		break;
	case 18:	/* key 18, stock 0xffffff8008884338 */
	/*
	 * stock keys 3/5/6/7/12/13/21/22/23/24/25 enter this branch after
	 * its stack clear and take no further action before the cookie
	 * check/return.
	 */
	case 3:
	case 5:
	case 6:
	case 7:
	case 12:
	case 13:
	case 21:
	case 22:
	case 23:
	case 24:
	case 25:
		memset(buf, 0, sizeof(buf));
		buf[0] = 0x07;
		buf[1] = 0x02;
		buf[2] = 0x00;
		buf[3] = 0x00;
		ret = ctn730_i2c_send(data, buf, 4);
		if (ret >= 0) {
			mutex_lock(&data->report_info_mutex);
			data->pen_mode_open = 0;
			mutex_unlock(&data->report_info_mutex);
		}
		break;
	case 19:	/* key 19, stock 0xffffff8008884380 */
		memset(buf, 0, sizeof(buf));
		buf[0] = 0x11;
		buf[1] = 0x00;
		ctn730_i2c_send(data, buf, 2);
		break;
	default:
		break;
	}
}

static void reset_work(struct work_struct *work)
{
	struct ctn730_data *data = container_of(to_delayed_work(work),
						struct ctn730_data, reset_work);

	if (!data) {
		pr_info("ctn730: %s: no ctn730_dev\n", "reset_work");
		return;
	}
	pr_info("ctn730: %s: ctn730_dev_hard_reset\n", "reset_work");
	if (data->enabled) {
		pr_info("ctn730: %s: ctn730_disenable\n", "ctn730_disable");
		disable_irq(data->irq);
		if (data->gpio_ven < 0x500) {
			ctn730_set_ven(data, 0);
			data->detected = 0;
		} else {
			pr_info("ctn730: %s: invalid gpio ven_gpio,disable failed\n",
				"ctn730_disable");
		}
		data->enabled = 0;
	}
	msleep(2000);
	pr_info("ctn730: %s: ctn730_enable\n", "ctn730_enable");
	if (!data->enabled) {
		ctn730_set_ven(data, 1);
		enable_irq(data->irq);
		data->enabled = 1;
	}
	if (data->err_count) {
		pm_relax(data->dev);
		data->err_count--;
	}
}

static void update_firmware_work(struct work_struct *work)
{
	struct ctn730_data *data = container_of(to_delayed_work(work),
						struct ctn730_data,
						update_fw_work);
	u8 buf[280];
	unsigned int count, len1, len2, addr;
	u8 *src;

	if (!data) {
		pr_info("ctn730: %s: no ctn730_dev\n", "update_firmware_work");
		return;
	}
	if (!data->enabled) {
		pr_info("ctn730: %s: ctn730_enable\n", "ctn730_enable");
		if (data->gpio_ven < 0x500)
			ctn730_set_ven(data, 1);
		data->enabled = 1;
		enable_irq(data->irq);
		data->err_count = 0;
		return;
	}
	if (data->fw_status - 0x15 > 2)
		return;
	if (data->fw_status == 0x17) {
		data->fw_section_count = 0;
		data->fw_status = 1;
		if (data->fw_section_data)
			vfree(data->fw_section_data);
		queue_delayed_work_on(8, system_wq, &data->set_work,
				      msecs_to_jiffies(25));
		return;
	}
	if (data->fw_status == 1) {
		memset(buf, 0, sizeof(buf));
		buf[0] = 0x03;
		buf[1] = 0x02;
		memcpy(&buf[2], &data->fw_len, sizeof(data->fw_len));
		ctn730_i2c_send(data, buf, 4);
		return;
	}
	pr_info("ctn730: %s: ctn730_disenable\n", "ctn730_disable");
	disable_irq(data->irq);
	if (data->gpio_ven < 0x500) {
		ctn730_set_ven(data, 0);
		data->detected = 0;
	} else {
		pr_info("ctn730: %s: invalid gpio ven_gpio,disable failed\n",
			"ctn730_disable");
	}
	data->enabled = 0;

	count = data->fw_section_count;
	len1 = data->fw_section_len >> 7;
	len2 = data->fw_section_len2 >> 7;
	if (count < len1) {
		src = data->fw_section_data + count * 0x80;
		addr = data->fw_data_addr + count * 0x80;
	} else if (count < len2) {
		src = data->fw_section_data + count * 0x80;
		addr = data->fw_data2_addr + (count - len1) * 0x80;
	} else {
		if (count != len2)
			return;
		memset(buf, 0, sizeof(buf));
		buf[0] = 0x04;
		buf[1] = 0x04;
		buf[2] = (data->fw_crc_val >> 24) & 0xff;
		buf[3] = (data->fw_crc_val >> 16) & 0xff;
		buf[4] = (data->fw_crc_val >> 8) & 0xff;
		buf[5] = data->fw_crc_val & 0xff;
		if (ctn730_i2c_send(data, buf, 6) >= 0)
			data->fw_section_count++;
		return;
	}
	memset(buf, 0, sizeof(buf));
	memcpy(buf + 5, src, 0x80);
	buf[0] = 0x05;
	buf[1] = 0x83;
	buf[2] = addr & 0xff;
	buf[3] = (addr >> 8) & 0xff;
	buf[4] = (addr >> 16) & 0xff;
	if (ctn730_i2c_send(data, buf, 0x85) >= 0)
		data->fw_section_count++;
}

static void rf_on_work(struct work_struct *work)
{
	struct ctn730_data *data = container_of(to_delayed_work(work),
						struct ctn730_data, rf_on_work);
	u8 buf[4];

	if (!data) {
		pr_info("ctn730: %s: no ctn730_dev\n", "rf_on_work");
		return;
	}
	if (!data->suspended && data->gpio_ven < 0x500) {
		ctn730_set_ven(data, 0);
		msleep(5);
		ctn730_set_ven(data, 1);
	}
	if (data->suspended != 1)
		return;
	if (data->fw_status == 2) {
		/* stock: buf = {0x06,0x01,0x01}, bist_mutex, pen_delay = 1 */
		memset(buf, 0, sizeof(buf));
		buf[0] = 0x06;
		buf[1] = 0x01;
		buf[2] = 0x01;
		mutex_lock(&data->bist_mutex);
		if (ctn730_i2c_send(data, buf, 3) >= 0)
			data->pen_delay = 1;
		mutex_unlock(&data->bist_mutex);
	} else if (data->fw_status == 1) {
		/* stock: buf = {0x00,0x01,0x00} -> 0x100 little endian */
		memset(buf, 0, sizeof(buf));
		buf[0] = 0x00;
		buf[1] = 0x01;
		buf[2] = 0x00;
		if (ctn730_i2c_send(data, buf, 3) >= 0)
			data->detected = 1;
	} else {
		if (data->gpio_ven < 0x500) {
			ctn730_set_ven(data, 0);
			msleep(5);
			ctn730_set_ven(data, 1);
		}
		data->detected = 0;
	}
}

static void check_static_charge_work(struct work_struct *work)
{
	struct ctn730_data *data = container_of(to_delayed_work(work),
						struct ctn730_data,
						check_static_charge_work);

	if (!data) {
		pr_info("ctn730: %s: no ctn730_dev\n",
			"check_static_charge_work");
		return;
	}
	/* stock: one 8-byte -1 store over level+attached */
	data->level = -1;
	data->pen_info_attached = -1;
	data->pending_event_index = 0;
	data->charging = 0;
	if (is_hard_reset != 1) {
		memset(data->param, 0, 6);
		memset(data->mac, 0, 8);
		ctn730_notifier_call_chain(data, data->cmd_delay);
		ctn730_uevent_change(data);
		return;
	}
	pr_info("ctn730: %s: check charge work :open pen mode\n",
		"check_static_charge_work");
	nvt_support_pen_set_nfc(true);
	memset(data->param, 0, 6);
	memset(data->mac, 0, 8);
	ctn730_notifier_call_chain(data, data->cmd_delay);
	ctn730_uevent_change(data);
}

static void check_pen_lost(struct work_struct *work)
{
	struct ctn730_data *data = container_of(to_delayed_work(work),
						struct ctn730_data,
						check_pen_lost);

	if (!data) {
		pr_info("ctn730: %s: no ctn730_dev\n", "check_pen_lost");
		return;
	}
	memset(data->mac_or_param, 0, 6);
	memset(data->pen_uid, 0, 8);
	data->charging = 0;
	pr_info("ctn730: %s: enter check :is pen lost\n", "check_pen_lost");
	if (is_hard_reset == 1) {
		pr_info("ctn730: %s: pen_lost :open pen mode\n", "check_pen_lost");
		nvt_support_pen_set_nfc(true);
	}
	ctn730_notifier_call_chain(data, data->cmd_delay);
	ctn730_uevent_change(data);
}

static enum alarmtimer_restart charging_alarmtimer_function(struct alarm *alarm,
							     ktime_t now)
{
	struct ctn730_data *data = container_of(alarm, struct ctn730_data,
						alarm);

	if (!data) {
		pr_info("ctn730: %s: no ctn730_dev\n", "ctn730_dev");
		return ALARMTIMER_NORESTART;
	}
	if (data->charging) {
		pm_stay_awake(data->dev);
		data->err_count++;
	}
	queue_delayed_work_on(8, system_wq, &data->reset_work, 0);
	pr_info("ctn730: %s: charging time out ctn730 reset !!!\n",
		"charging_alarmtimer_function");
	return ALARMTIMER_NORESTART;
}

static irqreturn_t ctn730_dev_irq_handler(int irq, void *dev_id)
{
	struct ctn730_data *data = dev_id;
	u8 head[2];
	u8 payload[256];
	u8 len = 0;
	int ret;
	ktime_t now, delta;
	struct timespec ts;

	if (!data) {
		pr_info("ctn730: %s: no ctn730_dev\n", "ctn730_dev_irq_handler");
		return IRQ_HANDLED;
	}
	if (data->suspended) {
		pr_info("ctn730: %s: suspended, wakeup event!\n",
			"ctn730_dev_irq_handler");
		/* stock passes data+0x40 = misc.this_device */
		pm_wakeup_dev_event(data->misc.this_device, 10000, 0);
		data->suspended = 0;
		msleep(50);
	}
	memset(head, 0, sizeof(head));
	memset(payload, 0, sizeof(payload));
	/* stock checks gpio_irq (data+0xf4) here, not ven */
	if (!gpiod_get_value_cansleep(gpio_to_desc(data->gpio_irq))) {
		pr_info("ctn730: %s: irq_gpio low do not read i2c\n",
			"ctn730_dev_irq_handler");
		goto out;
	}
	ret = i2c_transfer_buffer_flags(data->client, head, 2, I2C_M_RD);
	if (ret < 0) {
		pr_info("ctn730: %s: recieve error %d\n",
			"ctn730_dev_irq_handler", ret);
		msleep(5);
		return IRQ_HANDLED;
	}
	if (head[1]) {
		ret = i2c_transfer_buffer_flags(data->client, payload,
						head[1], I2C_M_RD);
		len = head[1];
	}
	if (head[0] == 0x92 && len == 1 && payload[0] == 0x04) {
		now = ktime_get();
		if (!data->last_irq_ktime) {
			data->last_irq_ktime = now;
		} else {
			delta = ktime_sub(now, data->last_irq_ktime);
			ts = ns_to_timespec(delta);
			/* stock compares the timespec's first field (tv_sec) > 300 */
			if (ts.tv_sec > 300) {
				data->last_irq_ktime = 0;
				is_hard_reset = 1;
				data->err_count++;
				queue_delayed_work_on(8, system_wq,
						      &data->reset_work, 0);
				pr_info("ctn730: %s: ctn730_hard_reset \n",
					"ctn730_dev_irq_handler");
				goto out;
			}
		}
	} else {
		data->last_irq_ktime = 0;
	}
	pr_info("ctn730: %s: recieve %*ph %*ph\n", "ctn730_dev_irq_handler",
		2, head, len, payload);
	if ((head[0] & 0xc0) == 0x40) {
		pr_info("ctn730: %s: wk>enter ctn730_parse_message(),instruction: %d\n",
			"ctn730_parse_message", head[0] & 0x3f);
		ctn730_parse_message(data, head[0], head[0] & 0x3f, payload);
	} else if ((head[0] & 0xc0) == 0x80) {
		pr_info("ctn730: %s: wk>enter ctn730_parse_message(),instruction: %d\n",
			"ctn730_parse_message", head[0] & 0x3f);
		if ((head[0] & 0x3f) <= 0x24)
			ctn730_parse_message(data, head[0], head[0] & 0x3f,
					     payload);
	} else {
		pr_info("ctn730: %s: recieve %02x %02x\n",
			"ctn730_dev_irq_handler", head[0], len);
	}
out:
	msleep(5);
	return IRQ_HANDLED;
}

/*
 * Stock IRQ message dispatch, inlined in the original
 * ctn730_dev_irq_handler (0xffffff80088844e8). Two jump tables keyed on
 * the low 6 bits of the instruction byte: T40 covers 0x43..0x63 and T80
 * covers 0x80..0xa4. head[0] is the instruction byte, head[1] the payload
 * length; payload[0] is the sub-command and payload[1..] its operands.
 */
static void ctn730_parse_message(struct ctn730_data *data, u8 cmd, u8 len,
				 u8 *payload)
{
	u8 sub;
	int idx;

	if ((cmd & 0xc0) == 0x40) {
		sub = cmd & 0x3f;
		pr_info("ctn730: %s: instruction: %d\n",
			"ctn730_parse_message", sub);
		if (sub < 3 || sub > 0x23)
			goto bad;
		goto t40_dispatch;
	} else if ((cmd & 0xc0) == 0x80) {
		sub = cmd & 0x3f;
		pr_info("ctn730: %s: instruction: %d\n",
			"ctn730_parse_message", sub);
		if (sub > 0x24)
			goto bad;
		goto t80_dispatch;
	} else {
		/* 0x8884778: b.ne 0x8884eb0 (NO resched, no print) */
		goto tail;
	}

t40_dispatch:
	switch (sub) {
	case 3:  /* 0x43: 0x8884bec */
		if (payload[0])
			goto t43_err;
		data->fw_status = 21;
		pr_info("ctn730: %s: status %s\n", "ctn730_parse_message",
			ctn730_status_text[21]);
		goto resched_tail;
t43_err:
		data->fw_status = 24;
		pr_info("ctn730: %s: status HOST_CTRL_DL_OPEN_SESSION failed\n",
			"ctn730_parse_message");
		goto resched_tail;
	case 4:  /* 0x44: 0x8884c18 */
		if (payload[0])
			goto t44_err;
		data->fw_status = 23;
		pr_info("ctn730: %s: status %s\n", "ctn730_parse_message",
			ctn730_status_text[23]);
		goto resched_tail;
t44_err:
		data->fw_status = 24;
		pr_info("ctn730: %s: status HOST_CTRL_DL_COMMIT_SESSION failed\n",
			"ctn730_parse_message");
		goto resched_tail;
	case 5:  /* 0x45: 0x8884c44 */
		if (payload[0])
			goto t45_err;
		data->fw_status = 22;
		pr_info("ctn730: %s: status %s\n", "ctn730_parse_message",
			ctn730_status_text[22]);
		goto resched_tail;
t45_err:
		data->fw_status = 24;
		pr_info("ctn730: %s: status HOST_CTRL_DL_WRITE_FLASH failed\n",
			"ctn730_parse_message");
		goto resched_tail;
	case 6:  /* 0x46: 0x8884704 (T40 init_block) */
		goto t40_init_block;
	case 7:  /* 0x47: 0x8884c74 */
		if (payload[0])
			goto tail;
		data->fw_status = 4;
		mutex_lock(&data->report_info_mutex);
		if (data->pen_mode_open) {
			data->pen_mode_open = payload[0];
			mutex_unlock(&data->report_info_mutex);
			goto tail;
		}
		mutex_unlock(&data->report_info_mutex);
		goto tail;
	case 16: /* 0x50: 0x8884ca8 */
		if (payload[0])
			goto tail;
		data->fw_status = 3;
		pr_info("ctn730: %s: status %s\n", "ctn730_parse_message",
			ctn730_status_text[3]);
		queue_delayed_work_on(8, system_wq,
			&data->check_static_charge_work,
			msecs_to_jiffies(500));
		goto tail;
	case 17: /* 0x51: 0x8884cec */
		if (payload[0])
			goto tail;
		data->fw_status = 4;
		pr_info("ctn730: %s: CHG_DISABLE OK\n",
			"ctn730_parse_message");
		goto tail;
	case 32: /* 0x60: 0x88847e0 */
		if (payload[0])
			goto tail;
		data->fw_status = 12;
		pr_info("ctn730: %s: status %s\n", "ctn730_parse_message",
			ctn730_status_text[12]);
		goto tail;
	case 33: /* 0x61: 0x8884808 */
		if (payload[0])
			goto tail;
		data->fw_status = 13;
		pr_info("ctn730: %s: status %s\n", "ctn730_parse_message",
			ctn730_status_text[13]);
		goto tail;
	case 35: /* 0x63: 0x8884830 */
		if (payload[0])
			goto t35_notok;
		data->fw_status = 4;
		pr_info("ctn730: %s: APP_CTRL_CHN_SEND_DATA rsp ok, treat as detected\n",
			"ctn730_parse_message");
		goto tail;
t35_notok:
		data->pen_info_attached = 1;
		kobject_uevent(&data->misc.this_device->kobj, KOBJ_CHANGE);
		goto tail;
	default:
		goto bad;
	}

t80_dispatch:
	switch (sub) {
	case 0:  /* 0x80: 0x888479c */
		if (!payload[0])
			goto t80_init_block;
		if (payload[0] == 1) {
			/* 0x88847ac: csinc eq->wzr+1=1, ne->25 */
			data->fw_status = data->detected ? 25 : 1;
			goto resched_tail;
		}
		if (payload[0] == 2 && !data->detected) {
			data->fw_status = 3;
			pr_info("ctn730: %s: charge_state %s\n",
				"ctn730_parse_message",
				ctn730_charge_state_text[3]);
			goto tail;
		}
		if (payload[0] != data->fw_status) {
			data->fw_status = 1;
			kobject_uevent(&data->misc.this_device->kobj,
				KOBJ_CHANGE);
		}
		goto resched_tail;
	case 17: /* 0x91: 0x8884854 */
		if (!payload[0]) {
			data->fw_status = 9;
			pr_info("ctn730: %s: status %s\n",
				"ctn730_parse_message",
				ctn730_status_text[9]);
			goto tail;
		}
		if (payload[0] != 1)
			goto tail;
		pr_info("ctn730: %s: disable_chg due to overtemp\n",
			"ctn730_parse_message");
		goto tail;
	case 18: /* 0x92: 0x8884870 */
		if (!payload[0]) {
			data->fw_status = 4;
			queue_delayed_work_on(8, system_wq,
				&data->check_static_charge_work,
				msecs_to_jiffies(500));
			data->pen_info_attached = 1;
			data->has_mac = 1;
			memset(data->pen_uid, 0, 8);
			memset(data->mac_or_param, 0, 8);
			if (!data->level) {
				data->level = 1;
				ctn730_notifier_call_chain(data, data->level);
				kobject_uevent(&data->misc.this_device->kobj,
					KOBJ_CHANGE);
			}
			queue_delayed_work_on(8, system_wq,
				&data->check_static_charge_work,
				msecs_to_jiffies(125));
			goto tail;
		}
		if (payload[0] == 1)
			goto app_chn_connected;
		if (payload[0] == 2)
			goto deactived;
		if ((payload[0] & 0xfe) != 4)
			goto tail;
		if (payload[0] == 5) {
			data->last_irq_ktime = 0;
			is_hard_reset = 0;
		}
		goto shared_4_or_5;
	case 20: /* 0x94: 0x888499c */
		if (!payload[0])
			goto app_chn_connected;
		if (payload[0] != 1)
			goto tail;
		if (data->fw_status == 1) {
			data->pen_mode_open = 1;
		} else if (data->fw_status != 2) {
			goto tail;
		} else {
			data->pen_mode_open = 0;
		}
		goto tail;
	case 21: /* 0x95: 0x88849dc */
		if (payload[0] == data->level)
			goto tail;
		idx = payload[0] == 0xff ? -1 : payload[0];
		pr_info("ctn730: %s: level : %d\n",
			"ctn730_parse_message", idx);
		data->level = idx;
		data->has_mac = 1;
		kobject_uevent(&data->misc.this_device->kobj, KOBJ_CHANGE);
		goto tail;
	case 34: /* 0xa2: 0x8885214 */
		if (!payload[0])
			goto app_chn_connected;
		if (payload[0] != 1)
			goto tail;
		data->fw_status = 14;
		pr_info("ctn730: %s: status %s\n", "ctn730_parse_message",
			ctn730_status_text[14]);
		queue_delayed_work_on(8, system_wq, &data->set_work,
			msecs_to_jiffies(13));
		goto tail;
	case 35: /* 0xa3: 0x8884a90 */
		goto tail;
	case 36: /* 0xa4: 0x8884a54 */
		if (payload[0] == 1)
			goto tail;
		if (payload[0])
			goto tail;
		goto tail;
	default:
		goto bad;
	}

shared_4_or_5:
	data->fw_status = 4;
	queue_delayed_work_on(8, system_wq,
		&data->check_static_charge_work, msecs_to_jiffies(125));
	data->pen_info_attached = 1;
	data->has_mac = 1;
	memcpy(data->pen_uid, payload + 1, 7);
	pr_info("ctn730: %s: pen_uid:%*ph\n", "ctn730_parse_message",
		8, data->pen_uid);
	goto tail;

t40_init_block:
	/* 0x8884704 (T40 c6): pen_mode mutex + set_work */
	if (payload[0]) {
		/* 0x8884a9c: charge disable */
		data->fw_status = 26;
		queue_delayed_work_on(8, system_wq, &data->set_work, 0);
		goto tail;
	}
	mutex_lock(&data->report_info_mutex);
	if (data->fw_status == 1) {
		data->fw_status = 9;
		pr_info("ctn730: %s: status %s\n", "ctn730_parse_message",
			ctn730_status_text[9]);
		if (data->fw_status == 2) {
			data->fw_status = 10;
			pr_info("ctn730: %s: status %s\n",
				"ctn730_parse_message",
				ctn730_status_text[10]);
			if (data->fw_status == 9) {
				memcpy(data->mac, data->mac_or_param, 6);
				data->fw_status = 11;
				data->pen_mode_open = 1;
				data->has_mac = 1;
				pr_info("ctn730: %s: mac %pM\n",
					"ctn730_parse_message", data->mac);
				pr_info("ctn730: %s: detect...\n",
					"ctn730_parse_message");
				queue_delayed_work_on(8, system_wq,
					&data->set_work, 0);
			}
		}
	} else if (data->fw_status == 2) {
		data->fw_status = 10;
		pr_info("ctn730: %s: status %s\n", "ctn730_parse_message",
			ctn730_status_text[10]);
		if (data->fw_status == 9) {
			memcpy(data->mac, data->mac_or_param, 6);
			data->fw_status = 11;
			data->pen_mode_open = 1;
			data->has_mac = 1;
			pr_info("ctn730: %s: mac %pM\n",
				"ctn730_parse_message", data->mac);
			pr_info("ctn730: %s: detect...\n",
				"ctn730_parse_message");
			queue_delayed_work_on(8, system_wq,
				&data->set_work, 0);
		}
	} else if (data->fw_status == 9) {
		memcpy(data->mac, data->mac_or_param, 6);
		data->fw_status = 11;
		data->pen_mode_open = 1;
		data->has_mac = 1;
		pr_info("ctn730: %s: mac %pM\n",
			"ctn730_parse_message", data->mac);
		pr_info("ctn730: %s: detect...\n", "ctn730_parse_message");
		queue_delayed_work_on(8, system_wq, &data->set_work, 0);
	} else if (data->fw_status == 4) {
		data->fw_status = 4;
		pr_info("ctn730: %s: status %s\n", "ctn730_parse_message",
			ctn730_status_text[4]);
	}
	mutex_unlock(&data->report_info_mutex);
	if (data->fw_status >= 1 && data->fw_status <= 2) {
		goto tail;
	}
	goto resched_tail;

t80_init_block:
	/* 0x8884d70 (T80 c0 p0==0): pen reset + version */
	data->fw_status = 2;
	data->level = -1;
	data->has_ext_cmd = 0;
	data->version[0] = data->mac_or_param[0];
	data->version[1] = data->mac_or_param[1];
	memset(data->mac, 0, 6);
	memset(data->pen_uid, 0, 8);
	if (!data->fw_ver_sel) {
		char ver[8];

		sprintf(ver, "%02x%02x", data->version[0],
			data->version[1]);
		data->fw_ver_sel = 1;
	}
	ctn730_notifier_call_chain(data, data->level);
	if (!is_hard_reset)
		kobject_uevent(&data->misc.this_device->kobj, KOBJ_CHANGE);
	is_hard_reset = 0;
	pr_info("ctn730: %s: version %02x%02x\n",
		"ctn730_parse_message", data->version[0],
		data->version[1]);
	if (payload[0] == 1) {
		data->fw_status = data->detected ? 25 : 1;
		goto resched_tail;
	}
	goto resched_tail;

app_chn_connected:
	data->fw_status = 6;
	alarm_cancel(&data->alarm);
	memset(data->mac, 0, 8);
	memset(data->pen_uid, 0, 8);
	data->level = -1;
	data->last_irq_ktime = 0;
	is_hard_reset = 0;
	pr_info("ctn730: %s: status %s\n", "ctn730_parse_message",
		ctn730_status_text[6]);
	ctn730_notifier_call_chain(data, data->level);
	queue_delayed_work_on(8, system_wq, &data->check_pen_lost,
		msecs_to_jiffies(15));
	goto resched_tail;

deactived:
	data->fw_status = 4;
	data->pen_info_attached = 0;
	pr_info("ctn730: %s: status %s\n", "ctn730_parse_message",
		ctn730_status_text[4]);
	goto tail;

bad:
	/* 0x8884a70: recieve print -> 0x8884eb0 (NO resched) */
	pr_info("ctn730: %s: recieve %02x %02x\n", "ctn730_parse_message",
		cmd, len);
	goto tail;

tail:
	/* 0x8884eb0: NO resched exit */
	msleep(5);
	return;

resched_tail:
	/* 0x8884e78: fw_status dispatch */
	if (data->fw_status == 2)
		queue_delayed_work_on(8, system_wq, &data->update_fw_work, 0);
	else if (data->fw_status == 1)
		queue_delayed_work_on(8, system_wq, &data->rf_on_work, 0);
	else
		queue_delayed_work_on(8, system_wq, &data->set_work, 0);
	msleep(5);
}
static ssize_t enable_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct ctn730_data *data = ctn730_from_dev(dev);

	if (!data) {
		pr_info("ctn730: %s: no ctn730_dev\n", "enable_show");
		return -ENODEV;
	}
	pr_info("ctn730: %s: wk>enable__show\n", "enable_show");
	return sprintf(buf, "%d\n",
		       gpiod_get_raw_value(gpio_to_desc(data->gpio_ven)));
}

static ssize_t enable_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct ctn730_data *data = ctn730_from_dev(dev);
	u8 cmd[10];
	unsigned long val;

	if (!data) {
		pr_info("ctn730: %s: no ctn730_dev\n", "enable_store");
		return -ENODEV;
	}
	val = simple_strtoul(buf, NULL, 0);
	if (val == 0) {
		pr_info("ctn730: %s: wk>excute--ctn730_disable_chg()\n",
			"enable_store");
		data->rf_on = 0;
		memset(cmd, 0, sizeof(cmd));
		cmd[0] = 0x11;
		cmd[1] = 0x00;
		ctn730_i2c_send(data, cmd, 2);
		data->pen_info_attached = 0;
		if (is_hard_reset == 1) {
			pr_info("ctn730: %s: enable_store :open pen mode\n",
				"enable_store");
			nvt_support_pen_set_nfc(true);
		}
		data->charging = 0;
	} else {
		pr_info("ctn730: %s: wk>ctn730_enable_chg\n", "enable_store");
		memset(cmd, 0, sizeof(cmd));
		cmd[0] = 0x10;
		cmd[1] = 0x02;
		cmd[2] = 0xf4;
		cmd[3] = 0x01;
		ctn730_i2c_send(data, cmd, 4);
	}
	pr_info("ctn730: %s: wk>END--enable_store(),return result>size:%d\n",
		"enable_store", (int)count);
	return count;
}

static ssize_t pen_cmd_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct ctn730_data *data = ctn730_from_dev(dev);

	if (!data) {
		pr_info("ctn730: %s: no ctn730_dev\n", "pen_cmd_store");
		return -ENODEV;
	}
	if (data->fw_status == 4 && data->charging) {
		memcpy(data->param, "\xaa\x55\xcc\x33", 4);
		data->cmd_delay = 4;
		data->fw_status = 0x10;
		queue_delayed_work_on(8, system_wq, &data->set_work, 0);
	} else {
		pr_info("ctn730: %s: status: %s have_mac %d,no pen_cmd\n",
			"pen_cmd_store",
			ctn730_status_text[data->fw_status],
			data->has_mac);
	}
	return count;
}

static ssize_t reget_mac_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct ctn730_data *data = ctn730_from_dev(dev);
	unsigned long val;
	int ret;

	if (!data) {
		pr_info("ctn730: %s: no ctn730_dev\n", "reget_mac_store");
		return -ENODEV;
	}
	ret = kstrtoul(buf, 0, &val);
	if (!ret) {
		data->detected = 0;
		data->fw_status = 4;
		queue_delayed_work_on(8, system_wq, &data->rf_on_work, 0);
	}
	return count;
}

static ssize_t info_report_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct ctn730_data *data = ctn730_from_dev(dev);
	unsigned long val;
	int ret;

	if (!data) {
		pr_info("ctn730: %s: no ctn730_dev\n", "info_report_store");
		return -ENODEV;
	}
	ret = kstrtoul(buf, 0, &val);
	/* stock: cinc w8,w8,eq over 0x11 -> 0x12 when kstrtoul succeeded */
	data->fw_status = ret ? 0x11 : 0x12;
	queue_delayed_work_on(8, system_wq, &data->set_work,
			      msecs_to_jiffies(25));
	return count;
}

static ssize_t rf_on_store(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct ctn730_data *data = ctn730_from_dev(dev);
	unsigned long val;
	int ret;

	if (!data) {
		pr_info("ctn730: %s: no ctn730_dev\n", "rf_on_store");
		return -ENODEV;
	}
	ret = kstrtoul(buf, 0, &val);
	data->rf_on = ret ? 1 : 0;
	queue_delayed_work_on(8, system_wq, &data->rf_on_work,
			      msecs_to_jiffies(3));
	return count;
}

static ssize_t param_store(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct ctn730_data *data = ctn730_from_dev(dev);

	if (!data) {
		pr_info("ctn730: %s: no ctn730_dev\n", "param_store");
		return -ENODEV;
	}
	if (count >= 0x21) {
		pr_info("ctn730: %s: too many input\n", "param_store");
		return count;
	}
	memcpy(data->param, buf, count);
	data->cmd_delay = count;
	data->fw_status = 0x10;
	queue_delayed_work_on(8, system_wq, &data->set_work,
			      msecs_to_jiffies(25));
	return count;
}

static ssize_t charge_state_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct ctn730_data *data = ctn730_from_dev(dev);

	if (!data) {
		pr_info("ctn730: %s: no ctn730_dev\n", "charge_state_show");
		return -ENODEV;
	}
	pr_info("ctn730: %s: wk>charge_state_show\n", "charge_state_show");
	return sprintf(buf, "%s\n",
		       ctn730_charge_state_text[data->pending_event_index]);
}

static ssize_t mac_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct ctn730_data *data = ctn730_from_dev(dev);

	if (!data) {
		pr_info("ctn730: %s: no ctn730_dev\n", "mac_show");
		return -ENODEV;
	}
	pr_info("ctn730: %s: wk>mac_show\n", "mac_show");
	return sprintf(buf, "%02X:%02X:%02X:%02X:%02X:%02X\n",
		       data->mac[5], data->mac[4], data->mac[3],
		       data->mac[2], data->mac[1], data->mac[0]);
}

static ssize_t version_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct ctn730_data *data = ctn730_from_dev(dev);

	if (!data) {
		pr_info("ctn730: %s: no ctn730_dev\n", "version_show");
		return -ENODEV;
	}
	return sprintf(buf, "%02x%02x", data->version[0], data->version[1]);
}

static ssize_t level_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct ctn730_data *data = ctn730_from_dev(dev);

	if (!data) {
		pr_info("ctn730: %s: no ctn730_dev\n", "level_show");
		return -ENODEV;
	}
	pr_info("ctn730: %s: wk>level_show\n", "level_show");
	return sprintf(buf, "%d\n", data->level);
}

static ssize_t attached_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct ctn730_data *data = ctn730_from_dev(dev);

	if (!data) {
		pr_info("ctn730: %s: no ctn730_dev\n", "attached_show");
		return -ENODEV;
	}
	pr_info("ctn730: %s: wk>attached__show\n", "attached_show");
	return sprintf(buf, "%d\n", data->pen_info_attached);
}

static ssize_t status_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct ctn730_data *data = ctn730_from_dev(dev);

	if (!data) {
		pr_info("ctn730: %s: no ctn730_dev\n", "status_show");
		return -ENODEV;
	}
	pr_info("ctn730: %s: wk>status__show\n", "status_show");
	return sprintf(buf, "%s\n",
		       ctn730_status_text[data->fw_status]);
}

static ssize_t cn_test_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct ctn730_data *data = ctn730_from_dev(dev);
	u8 cmd[8];

	if (!data) {
		pr_info("ctn730: %s: no ctn730_dev\n", "cn_test_show");
		return -ENODEV;
	}
	pr_info("ctn730: %s: wk>cn_test__show\n", "cn_test_show");
	memset(cmd, 0, sizeof(cmd));
	cmd[0] = 0x06;
	cmd[1] = 0x01;
	cmd[2] = 0x04;
	mutex_lock(&data->bist_mutex);
	if (ctn730_i2c_send(data, cmd, 3) >= 0)
		data->pen_delay = 4;
	mutex_unlock(&data->bist_mutex);
	return sprintf(buf, "%s\n", ctn730_status_text[data->fw_status]);
}


static struct device_attribute *ctn730_attrs[] = {
	&dev_attr_enable,
	&dev_attr_pen_cmd,
	&dev_attr_reget_mac,
	&dev_attr_info_report,
	&dev_attr_rf_on,
	&dev_attr_param,
	&dev_attr_charge_state,
	&dev_attr_mac,
	&dev_attr_version,
	&dev_attr_level,
	&dev_attr_attached,
	&dev_attr_status,
	&dev_attr_cn_test,
	NULL,
};

static int ctn730_suspend(struct device *dev)
{
	struct ctn730_data *data = dev_get_drvdata(dev);
	u8 buf[4];

	if (!data) {
		pr_info("ctn730: %s: no ctn730_dev\n", "ctn730_suspend");
		return 0;
	}
	data->suspended = 1;
	if (data->report_info && data->fw_status == 4 && data->charging) {
		memset(buf, 0, sizeof(buf));
		buf[0] = 0x07;
		buf[1] = 0x02;
		buf[2] = 0x00;
		buf[3] = 0x00;
		if (ctn730_i2c_send(data, buf, 4) >= 0) {
			mutex_lock(&data->report_info_mutex);
			data->pen_mode_open = 0;
			mutex_unlock(&data->report_info_mutex);
		}
	}
	pr_info("%s\n", "ctn730_suspend");
	return 0;
}

static int ctn730_resume(struct device *dev)
{
	struct ctn730_data *data = dev_get_drvdata(dev);

	if (!data) {
		pr_info("ctn730: %s: no ctn730_dev\n", "ctn730_resume");
		return 0;
	}
	pr_info("%s\n", "ctn730_resume");
	return 0;
}

static int ctn730_remove(struct i2c_client *client)
{
	struct ctn730_data *data = i2c_get_clientdata(client);

	pr_info("wk>ctn730_remove:%s\n", "ctn730_remove");
	if (!data)
		return 0;
	free_irq(data->irq, data);
	misc_deregister(&data->misc);
	gpio_free(data->gpio_rst);
	gpio_free(data->gpio_ven);
	pr_info("ctn730: %s: kfree CTN730\n", "ctn730_remove");
	kfree(data);
	return 0;
}

static int ctn730_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct ctn730_data *data;
	struct device_node *np;
	int count, i, ret;
	u32 irq_gpio;

	pr_info("ctn730: %s: probe device tree start\n", "ctn730_probe");
	data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
	if (!data) {
		pr_info("ctn730: %s: failed to allocate memory for module data\n",
			"ctn730_probe");
		return -ENOMEM;
	}

	if (ctn730)
		pr_info("ctn730: %s: ctn730_dev is not NULL\n", "ctn730_probe");
	client_address = client;
	ctn730 = data;
	data->dev = &client->dev;
	data->client = client;
	i2c_set_clientdata(client, data);

	np = client->dev.of_node;
	if (!np) {
		pr_info("ctn730: %s: no node\n", "ctn730_probe");
		return -ENODEV;
	}
	if (!client->dev.pins) {
		dev_info(client->dev.parent, "pins are not configured\n");
		return -ENODEV;
	}

	ret = of_get_named_gpio(np, "rst-gpios", 0);
	if (ret < 0) {
		dev_info(&client->dev, "RST GPIO error getting from OF node\n");
		return -ENODEV;
	}
	irq_gpio = ret;
	ret = of_get_named_gpio(np, "enable-gpios", 0);
	if (ret < 0) {
		dev_info(&client->dev, "VEN GPIO error getting from OF node\n");
		return -ENODEV;
	}
	data->gpio_ven = ret;
	ret = of_get_named_gpio(np, "interrupt-gpios", 0);
	if (ret < 0) {
		dev_info(&client->dev, "IRQ GPIO error getting from OF node\n");
		return -ENODEV;
	}
	data->gpio_irq = ret;

	count = of_count_phandle_with_args(np, "panel", NULL);
	pr_info("ctn730: %s: panel count:%d \n", "ctn730_probe", count);
	for (i = 0; i < count; i++) {
		struct device_node *panel_np = of_parse_phandle(np, "panel", i);
		struct drm_panel *panel;

		if (!panel_np)
			continue;
		panel = of_drm_find_panel(panel_np);
		if (IS_ERR_OR_NULL(panel)) {
			of_node_put(panel_np);
			continue;
		}
		pr_info("ctn730: %s: find panel\n", "ctn730_probe");
		ctn730_panel = panel_np;
		break;
	}
	if (i == count)
		return -ENODEV;

	pr_info("ctn730: %s: parse device tree success\n", "ctn730_probe");
	ret = gpio_request(irq_gpio, "rst-gpios");
	if (ret == 0) {
		pr_info("ctn730: %s: request rst_gpio %d\n", "ctn730_probe",
			irq_gpio);
	} else {
		pr_info("ctn730: %s: not able to get GPIO rst_gpio\n",
			"ctn730_probe");
		return -ENODEV;
	}
	data->gpio_rst = irq_gpio;

	if (data->gpio_rst < 0x500) {
		pr_info("ctn730: %s: ctn730_rst test!\n", "ctn730_probe");
		gpio_direction_output(data->gpio_rst, 1);
		msleep(20);
		gpio_direction_output(data->gpio_rst, 0);
		msleep(20);
		gpio_direction_output(data->gpio_rst, 1);
		pr_info("ctn730: %s: ctn730_rst test!\n", "ctn730_probe");
	} else {
		return -ENODEV;
	}

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		pr_info("ctn730: %s: need I2C_FUNC_I2C\n", "ctn730_probe");
		return -ENODEV;
	}
	pr_info("ctn730: %s: I2C success\n", "ctn730_probe");

	ret = of_get_named_gpio(np, "nfc_int", 0);
	if (ret < 0) {
		pr_info("ctn730: %s: not able to get GPIO irq_gpio\n",
			"ctn730_probe");
		return -ENODEV;
	}
	data->gpio_irq = ret;
	pr_info("ctn730: %s: Enable to get GPIO irq_gpio\n", "ctn730_probe");

	ret = of_get_named_gpio(np, "nfc_ven", 0);
	if (ret < 0) {
		pr_info("ctn730: %s: not able to get GPIO ven_gpio\n",
			"ctn730_probe");
		return -ENODEV;
	}
	pr_info("ctn730: %s: Enable to get GPIO ven_gpio\n", "ctn730_probe");

	ret = gpiod_to_irq(gpio_to_desc(data->gpio_irq));
	if (ret < 0) {
		pr_info("ctn730: %s: not able to map GPIO irq_gpio to an IRQ\n",
			"ctn730_probe");
		return -ENODEV;
	}
	data->irq = ret;
	pr_info("ctn730: %s: Enable to map GPIO irq_gpio to an IRQ\n",
		"ctn730_probe");
	pr_info("ctn730: %s: request ven_gpio %d\n", "ctn730_probe",
		data->gpio_ven);
	ret = gpio_request(data->gpio_ven, "nfc_ven");
	if (ret < 0) {
		pr_info("ctn730: %s: not able to set irq_gpio as input\n",
			"ctn730_probe");
		return -ENODEV;
	}
	pr_info("ctn730: %s: Enable to set irq_gpio as input\n", "ctn730_probe");
	ret = gpio_direction_output(data->gpio_ven, 0);
	if (ret < 0) {
		pr_info("ctn730: %s: not able to set ven_gpio as output\n",
			"ctn730_probe");
		return -ENODEV;
	}
	pr_info("ctn730: %s: Enable to set ven_gpio as output\n", "ctn730_probe");

	data->misc.minor = MISC_DYNAMIC_MINOR;
	data->misc.name = "ctn730";
	data->misc.fops = NULL;
	data->misc.parent = &client->dev;
	ret = misc_register(&data->misc);
	if (ret) {
		pr_info("ctn730: %s: misc_register failed\n", "ctn730_probe");
		return ret;
	}
	pr_info("ctn730: %s: misc_register success\n", "ctn730_probe");
	data->misc.this_device->type = &ctn730_dev_type;
	for (i = 0; ctn730_attrs[i]; i++)
		device_create_file(data->misc.this_device, ctn730_attrs[i]);

	data->drm_nb.notifier_call = ctn730_drm_notifier_callback;
	if (!ctn730_panel) {
		pr_info("ctn730: %s: drm panel is  NULL\n", "ctn730_probe");
	} else {
		struct drm_panel *panel = of_drm_find_panel(ctn730_panel);

		if (IS_ERR_OR_NULL(panel) ||
		    drm_panel_notifier_register(panel, &data->drm_nb))
			pr_info("ctn730: %s: Unable to register ctn730_drm_notifier\n",
				"ctn730_probe");
	}

	ret = devm_request_threaded_irq(&client->dev, data->irq, NULL,
					ctn730_dev_irq_handler,
					IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					"ctn730_irq", data);
	if (ret) {
		pr_info("ctn730: %s: request_irq failed\n", "ctn730_probe");
		misc_deregister(&data->misc);
		return ret;
	}
	pr_info("ctn730: %s: request_irq successed\n", "ctn730_probe");
	disable_irq(data->irq);
	mutex_init(&data->bist_mutex);
	__mutex_init(&data->report_info_mutex,
		     "&ctn730_dev->report_info_mutex",
		     &report_info_mutex_key);
	irq_set_irq_wake(data->irq, 1);

	INIT_DELAYED_WORK(&data->update_fw_work, update_firmware_work);
	INIT_DELAYED_WORK(&data->rf_on_work, rf_on_work);
	INIT_DELAYED_WORK(&data->check_static_charge_work,
			  check_static_charge_work);
	INIT_DELAYED_WORK(&data->check_pen_lost, check_pen_lost);
	INIT_DELAYED_WORK(&data->set_work, set_work);
	INIT_DELAYED_WORK(&data->reset_work, reset_work);
	/* stock: alarm.function = charging_alarmtimer_function (probe+0x89 = data+0x448) */
	data->alarm.function = charging_alarmtimer_function;

	pr_info("ctn730: %s: ctn730_enable\n", "ctn730_enable");
	if (!data->enabled) {
		if (data->gpio_ven < 0x500)
			gpio_direction_output(data->gpio_ven, 1);
		enable_irq(data->irq);
		data->enabled = 1;
	}
	data->report_info = 1;
	pr_info("ctn730: %s: success\n", "ctn730_probe");
	{
		u8 init_buf[3] = { 0x01, 0x00, 0x00 };

		if (ctn730_i2c_send(data, init_buf, 3) >= 0)
			data->detected = 1;
	}
	pr_info("ctn730: %s: CTN730 successed\n", "ctn730_probe");
	return 0;
}

static const struct i2c_device_id ctn730_id[] = {
	{ "ctn730", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ctn730_id);

static const struct of_device_id ctn730_dt_match[] = {
	{ .compatible = "ctn730" },
	{ .compatible = "nxp,ctn730" },
	{ }
};

static const struct dev_pm_ops ctn730_pm_ops = {
	.suspend = ctn730_suspend,
	.resume = ctn730_resume,
};

static struct i2c_driver ctn730_driver = {
	.driver = {
		.name = "ctn730",
		.owner = THIS_MODULE,
		.of_match_table = ctn730_dt_match,
		.pm = &ctn730_pm_ops,
	},
	.id_table = ctn730_id,
	.probe = ctn730_probe,
	.remove = ctn730_remove,
};

static int __init ctn730_dev_init(void)
{
	return i2c_register_driver(THIS_MODULE, &ctn730_driver);
}

static void __exit ctn730_dev_exit(void)
{
	i2c_del_driver(&ctn730_driver);
}

module_init(ctn730_dev_init);
module_exit(ctn730_dev_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("NXP CTN730 stylus/pen manager for TB371FC");
MODULE_AUTHOR("rebuild");
