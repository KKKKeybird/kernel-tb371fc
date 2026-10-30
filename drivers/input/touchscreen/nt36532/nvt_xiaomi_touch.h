/* SPDX-License-Identifier: GPL-2.0-only */
/* Legacy four-int ABI used by the installed Xiaomi TouchFeature 1.0 HAL.
 * This is deliberately not pipa's 256-int ABI. Only verified NVT commands
 * are exposed; unimplemented modes must not silently report success.
 * Included once by nt36xxx.c after the NVT command helpers.
 */
#include <linux/miscdevice.h>
#include <linux/compat.h>

static DEFINE_MUTEX(nvt_xiaomi_lock);
static bool nvt_xiaomi_ready;

static long nvt_xiaomi_ioctl(struct file *file, unsigned int cmd,
			    unsigned long arg)
{
	int values[4] = { 0 };
	int value, ret = 0;
	void __user *user = (void __user *)arg;

	/* The old HAL passes literal 0x5400..0x5406, without an ioctl size. */
	if (cmd < 0x5400 || cmd > 0x5406)
		return -ENOTTY;
	if (copy_from_user(values, user, sizeof(values)))
		return -EFAULT;
	if (values[0] != 0) /* Touch_Game_Mode only */
		return -EOPNOTSUPP;
	if (cmd == 0x5400 && (values[1] < 0 || values[1] > 1))
		return -EINVAL;

	mutex_lock(&nvt_xiaomi_lock);
	if (!nvt_xiaomi_ready || !nvt_ts) {
		ret = -ENODEV;
		goto out;
	}
	if (cmd == 0x5400 || cmd == 0x5406) {
		value = cmd == 0x5406 ? 0 : values[1];
		/* Do not claim a suspended controller has applied a command. */
		if (!bTouchIsAwake) {
			ret = -EAGAIN;
			goto out;
		}
		if (nvt_ts->game_mode != value) {
			ret = nvt_game_mode_set(value);
			if (ret) {
				if (ret > 0)
					ret = -EIO;
				goto out;
			}
			/* Existing NVT resume/firmware recovery reapplies this field. */
			nvt_ts->game_mode = value;
		}
		values[0] = 0;
	} else if (cmd == 0x5401) {
		values[0] = !!nvt_ts->game_mode;
	} else if (cmd == 0x5402 || cmd == 0x5403) {
		values[0] = 0; /* default/minimum */
	} else if (cmd == 0x5404) {
		values[0] = 1; /* maximum */
	} else {
		values[0] = !!nvt_ts->game_mode;
		values[1] = 0; /* default */
		values[2] = 0; /* minimum */
		values[3] = 1; /* maximum */
	}
	if (copy_to_user(user, values, sizeof(values)))
		ret = -EFAULT;
out:
	mutex_unlock(&nvt_xiaomi_lock);
	return ret;
}

#ifdef CONFIG_COMPAT
static long nvt_xiaomi_compat_ioctl(struct file *file, unsigned int cmd,
				   unsigned long arg)
{
	return nvt_xiaomi_ioctl(file, cmd, (unsigned long)compat_ptr(arg));
}
#endif

static const struct file_operations nvt_xiaomi_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = nvt_xiaomi_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = nvt_xiaomi_compat_ioctl,
#endif
	.llseek = no_llseek,
};

static struct miscdevice nvt_xiaomi_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "xiaomi-touch",
	.fops = &nvt_xiaomi_fops,
	.mode = 0660,
};

static void nvt_xiaomi_register(void)
{
	int ret;

	mutex_lock(&nvt_xiaomi_lock);
	ret = misc_register(&nvt_xiaomi_misc);
	if (!ret)
		nvt_xiaomi_ready = true;
	mutex_unlock(&nvt_xiaomi_lock);
	if (ret)
		pr_err("NVT-ts: Xiaomi touch interface unavailable: %d\n", ret);
}

static void nvt_xiaomi_unregister(void)
{
	bool registered;

	/* Drain commands before the caller removes/free the NVT controller. */
	mutex_lock(&nvt_xiaomi_lock);
	registered = nvt_xiaomi_ready;
	nvt_xiaomi_ready = false;
	mutex_unlock(&nvt_xiaomi_lock);
	if (registered)
		misc_deregister(&nvt_xiaomi_misc);
}
