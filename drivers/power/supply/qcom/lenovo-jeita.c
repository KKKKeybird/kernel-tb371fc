// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2019 The Linux Foundation. All rights reserved.
 */

#define pr_fmt(fmt) "lenovo_jeita: %s: " fmt, __func__

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_batterydata.h>
#include <linux/power_supply.h>
#include <linux/slab.h>
#include <linux/pmic-voter.h>
#include <linux/backlight.h>
#include "lenovo-jeita.h"
#include "smb5-lib.h"

#define JEITA_VOTER		"LENOVO_JEITA_VOTER"
#define BATT_MAINTAINCE_VOTER		"BATT_MAINTAINCE_VOTER"

#define is_between(left, right, value) \
		(((left) >= (right) && (left) >= (value) \
			&& (value) >= (right)) \
		|| ((left) <= (right) && (left) <= (value) \
			&& (value) <= (right)))

#define JEITA_STEP 8
struct lenovo_jeita_param {
	u32			psy_prop;
	char			*prop_name;
	int			rise_hys;
	int			fall_hys;
};

struct range_data {
	int low_threshold;
	int high_threshold;
	u32 value;
};

struct jeita_fcc_cfg {
	struct lenovo_jeita_param	param;
	struct range_data		fcc_cfg[JEITA_STEP];
	struct range_data		default_fcc_cfg[JEITA_STEP];
};

struct jeita_fv_cfg {
	struct lenovo_jeita_param	param;
	struct range_data		fv_cfg[JEITA_STEP];
};
struct lenovo_jeita_info {
	struct device		*dev;
	ktime_t			jeita_last_update_time;
	bool			config_is_read;
	bool			thermal_cutback;
	bool			sw_jeita_cfg_valid;
	bool			batt_missing;
	bool			taper_fcc;
	int			jeita_fcc_index;
	int			jeita_fv_index;
	int			get_config_retry_count;
	int			batt_maintaince_fv;
	int			recharge_voltage;
	struct jeita_fcc_cfg	*jeita_fcc_config;
	struct jeita_fv_cfg	*jeita_fv_config;
	struct wakeup_source	*lenovo_jeita_ws;

	struct votable		*fcc_votable;
	struct votable		*fv_votable;
	struct votable		*usb_icl_votable;
	struct votable		*dc_suspend_votable;
	struct votable		*chg_disable_votable;
	struct votable		*cp_disable_votable;
	struct power_supply	*batt_psy;
	struct power_supply	*bms_psy;
	struct power_supply	*usb_psy;
	struct power_supply	*dc_psy;
	/* stock SOFT_HOT bookkeeping (struct+0x24/0x28/0x2c/0x30) */
	int			iterm_retry_count;
	int			hot_vbat_flag;
	int			recharge_retry_count;
	bool			usb209_flag;
	struct delayed_work	status_change_work;
	struct delayed_work	get_config_work;
	struct notifier_block	nb;
};

static struct lenovo_jeita_info *the_chip;

#define STEP_CHG_HYSTERISIS_DELAY_US		5000000 /* 5 secs */

#define BATT_HOT_DECIDEGREE_MAX			600
#define GET_CONFIG_DELAY_MS		2000
#define GET_CONFIG_RETRY_COUNT		50
#define WAIT_BATT_ID_READY_MS		200

static bool is_batt_available(struct lenovo_jeita_info *chip)
{
	if (!chip->batt_psy)
		chip->batt_psy = power_supply_get_by_name("battery");

	if (!chip->batt_psy)
		return false;

	return true;
}

static bool is_bms_available(struct lenovo_jeita_info *chip)
{
	if (!chip->bms_psy)
		chip->bms_psy = power_supply_get_by_name("bms");

	if (!chip->bms_psy)
		return false;

	return true;
}

static bool is_input_present(struct lenovo_jeita_info *chip)
{
	int rc = 0, input_present = 0;
	union power_supply_propval pval = {0, };

	if (!chip->usb_psy)
		chip->usb_psy = power_supply_get_by_name("usb");
	if (chip->usb_psy) {
		rc = power_supply_get_property(chip->usb_psy,
				POWER_SUPPLY_PROP_PRESENT, &pval);
		if (rc < 0)
			pr_err("Couldn't read USB Present status, rc=%d\n", rc);
		else
			input_present |= pval.intval;
	}

	if (!chip->dc_psy)
		chip->dc_psy = power_supply_get_by_name("dc");
	if (chip->dc_psy) {
		rc = power_supply_get_property(chip->dc_psy,
				POWER_SUPPLY_PROP_PRESENT, &pval);
		if (rc < 0)
			pr_err("Couldn't read DC Present status, rc=%d\n", rc);
		else
			input_present |= pval.intval;
	}

	if (input_present)
		return true;

	return false;
}

static int read_range_data_from_node(struct device_node *node,
		const char *prop_str, struct range_data *ranges,
		int max_threshold, u32 max_value)
{
	int rc = 0, i, length, per_tuple_length, tuples;

	if (!node || !prop_str || !ranges) {
		pr_err("Invalid parameters passed\n");
		return -EINVAL;
	}

	rc = of_property_count_elems_of_size(node, prop_str, sizeof(u32));
	if (rc < 0) {
		pr_err("Count %s failed, rc=%d\n", prop_str, rc);
		return rc;
	}

	length = rc;

	per_tuple_length = sizeof(struct range_data) / sizeof(u32);

	if (length % per_tuple_length) {
		pr_err("%s length (%d) should be multiple of %d\n",
				prop_str, length, per_tuple_length);
		return -EINVAL;
	}
	tuples = length / per_tuple_length;
	if (tuples > JEITA_STEP) {
		pr_err("too many entries(%d), only %d allowed\n",
				tuples, JEITA_STEP);
		return -EINVAL;
	}

	rc = of_property_read_u32_array(node, prop_str,
			(u32 *)ranges, length);
	if (rc) {
		pr_err("Read %s failed, rc=%d\n", prop_str, rc);
		return rc;
	}

	for (i = 0; i < tuples; i++) {
		if (ranges[i].low_threshold >
				ranges[i].high_threshold) {
			pr_err("%s thresholds should be in ascendant ranges\n",
						prop_str);
			rc = -EINVAL;
			goto clean;
		}

		if (i != 0) {
			if (ranges[i - 1].high_threshold >
					ranges[i].low_threshold) {
				pr_err("%s thresholds should be in ascendant ranges\n",
							prop_str);
				rc = -EINVAL;
				goto clean;
			}
		}

		if (ranges[i].low_threshold > max_threshold)
			ranges[i].low_threshold = max_threshold;
		if (ranges[i].high_threshold > max_threshold)
			ranges[i].high_threshold = max_threshold;
		if (ranges[i].value > max_value)
			ranges[i].value = max_value;
	}

	return rc;
clean:
	memset(ranges, 0, tuples * sizeof(struct range_data));
	return rc;
}

static int get_jeita_setting_from_profile(struct lenovo_jeita_info *chip)
{
	struct device_node *profile_node = chip->dev->of_node;
	u32 max_fv_uv, max_fcc_ma;
	int rc, hysteresis[2] = {0};

	/* stock reads sw-jeita/step charging settings straight from the
	 * qcom,qpnp-smb5 device node (live.dts:4880-4886); there is no
	 * battery-data best-profile lookup in stock. */
	if (!profile_node) {
		pr_err("no device node for jeita settings\n");
		return -ENODEV;
	}

	rc = of_property_read_u32(profile_node, "qcom,max-voltage-uv",
					&max_fv_uv);
	if (rc < 0) {
		pr_err("max-voltage_uv reading failed, rc=%d\n", rc);
		return rc;
	}

	rc = of_property_read_u32(profile_node, "qcom,fastchg-current-ma",
					&max_fcc_ma);
	if (rc < 0) {
		pr_err("max-fastchg-current-ma reading failed, rc=%d\n", rc);
		return rc;
	}

	pr_debug("max-voltage_uv=%d, fastchg-current-ma=%d\n",
			max_fv_uv, max_fcc_ma);

	chip->taper_fcc = of_property_read_bool(profile_node, "qcom,taper-fcc");

	chip->sw_jeita_cfg_valid = true;
	rc = read_range_data_from_node(profile_node,
			"qcom,jeita-fcc-ranges",
			chip->jeita_fcc_config->default_fcc_cfg,
			BATT_HOT_DECIDEGREE_MAX, max_fcc_ma * 1000);
	if (rc < 0) {
		pr_debug("Read qcom,jeita-fcc-ranges failed from battery profile, rc=%d\n",
					rc);
		chip->sw_jeita_cfg_valid = false;
	}

	memcpy(chip->jeita_fcc_config->fcc_cfg,
			chip->jeita_fcc_config->default_fcc_cfg,
			sizeof(struct range_data) * JEITA_STEP);

	rc = of_property_read_u32_array(profile_node,
			"qcom,step-jeita-hysteresis", hysteresis, 2);
	if (!rc) {
		chip->jeita_fcc_config->param.rise_hys = hysteresis[0];
		chip->jeita_fcc_config->param.fall_hys = hysteresis[1];
		pr_debug("jeita-fcc-hys: rise_hys=%u, fall_hys=%u\n",
			hysteresis[0], hysteresis[1]);
	}

	rc = read_range_data_from_node(profile_node,
			"qcom,jeita-fv-ranges",
			chip->jeita_fv_config->fv_cfg,
			BATT_HOT_DECIDEGREE_MAX, max_fv_uv);
	if (rc < 0) {
		pr_err("Read qcom,jeita-fv-ranges failed from battery profile, rc=%d\n",
					rc);
		chip->sw_jeita_cfg_valid = false;
	}

	pr_err("sw_jeita_cfg_valid :%d\n", chip->sw_jeita_cfg_valid);
	return rc;
}

static void get_config_work(struct work_struct *work)
{
	struct lenovo_jeita_info *chip = container_of(work,
			struct lenovo_jeita_info, get_config_work.work);
	int i, rc;

	chip->config_is_read = false;
	rc = get_jeita_setting_from_profile(chip);

	if (rc < 0) {
		if (rc == -ENODEV || rc == -EBUSY) {
			if (chip->get_config_retry_count++
					< GET_CONFIG_RETRY_COUNT) {
				pr_err("bms_psy is not ready, retry: %d\n",
						chip->get_config_retry_count);
				goto reschedule;
			}
		}
	}

	chip->config_is_read = true;
	for (i = 0; i < JEITA_STEP; i++)
		pr_err("jeita-fcc-cfg: %ddecidegree ~ %ddecidegre, %duA\n",
			chip->jeita_fcc_config->fcc_cfg[i].low_threshold,
			chip->jeita_fcc_config->fcc_cfg[i].high_threshold,
			chip->jeita_fcc_config->fcc_cfg[i].value);
	for (i = 0; i < JEITA_STEP; i++)
		pr_err("jeita-fv-cfg: %ddecidegree ~ %ddecidegre, %duV\n",
			chip->jeita_fv_config->fv_cfg[i].low_threshold,
			chip->jeita_fv_config->fv_cfg[i].high_threshold,
			chip->jeita_fv_config->fv_cfg[i].value);

	return;

reschedule:
	schedule_delayed_work(&chip->get_config_work,
			msecs_to_jiffies(GET_CONFIG_DELAY_MS));

}

static int get_val(struct range_data *range, int rise_hys, int fall_hys,
		int current_index, int threshold, int *new_index, int *val)
{
	int i;

	/* W1p (C104, stock 0x8bd2dc8..0x8bd30e8): full stock semantics.
	 * Entry only sets *new_index; *val is left to the caller's initial
	 * value and written on hit. */
	*new_index = -EINVAL;

	/* stock 0x8bd2dd0..0x8bd2dd8: threshold below the lowest range */
	if (threshold < range[0].low_threshold)
		return -ENODATA;

	/* stock: an empty segment (low|high == 0) fails the lookup; a
	 * segment not containing the threshold falls through to the next */
	for (i = 0; i < JEITA_STEP; i++) {
		if (!(range[i].low_threshold | range[i].high_threshold))
			continue;
		if (is_between(range[i].low_threshold,
				range[i].high_threshold, threshold)) {
			*new_index = i;
			*val = range[i].value;
			break;
		}
	}
	if (*new_index == -EINVAL)
		return -ENODATA;

	/* stock 0x8bd2ea0..0x8bd2ea8: no current index yet -> keep lookup */
	if (current_index == -EINVAL)
		return 0;

	/* stock hysteresis 0x8bd2eb0..0x8bd2f14, including the else branch
	 * (new outside current+/-1 also falls back to current) */
	if (*new_index == current_index + 1) {
		if (threshold <
			(range[*new_index].low_threshold + rise_hys)) {
			*new_index = current_index;
			*val = range[current_index].value;
		}
	} else if (*new_index == current_index - 1) {
		if (threshold >
			range[*new_index].high_threshold - fall_hys) {
			*new_index = current_index;
			*val = range[current_index].value;
		}
	} else {
		*new_index = current_index;
		*val = range[current_index].value;
	}
	return 0;
}

static int get_batt_maintaince_fv(struct lenovo_jeita_info *chip){
	int rc = 0;
	struct power_supply *exfg_psy;
	union power_supply_propval exfgpval = {0, };
	int batt_maintaince_fv = 0;
	int recharge_voltage = 0;

	exfg_psy = power_supply_get_by_name("bq27541-0");
	if (exfg_psy)
		rc = power_supply_get_property(exfg_psy,
				POWER_SUPPLY_PROP_GAUGE_VOLTAGE, &exfgpval);
	if (rc < 0) {
		pr_err("Couldn't configure POWER_SUPPLY_PROP_GAUGE_VOLTAGE rc=%d\n",
			rc);
	} else {
		switch (exfgpval.intval) {
			case EXFG_STEP3_VOLTAGE_MV:
				batt_maintaince_fv = EXFG_STEP3_FLOAT_VOLTAGE_MV;
				recharge_voltage = EXFG_STEP3_RECHARGE_VOLTAGE_MV;
				break;
			case EXFG_STEP2_VOLTAGE_MV:
				batt_maintaince_fv  = EXFG_STEP2_FLOAT_VOLTAGE_MV;
				recharge_voltage = EXFG_STEP2_RECHARGE_VOLTAGE_MV;
				break;
			case EXFG_STEP1_VOLTAGE_MV:
				batt_maintaince_fv  = EXFG_STEP1_FLOAT_VOLTAGE_MV;
				recharge_voltage = EXFG_STEP1_RECHARGE_VOLTAGE_MV;
				break;
			case EXFG_STEP0_VOLTAGE_MV:
				batt_maintaince_fv  = EXFG_STEP0_FLOAT_VOLTAGE_MV;
				recharge_voltage = EXFG_STEP0_RECHARGE_VOLTAGE_MV;
				break;
			default:
				break;
		}
		pr_err("current gauge voltage = %d fv= %d recharge_voltage = %d\n",exfgpval.intval,batt_maintaince_fv, recharge_voltage);

		if(batt_maintaince_fv != chip->batt_maintaince_fv){
			chip->batt_maintaince_fv = batt_maintaince_fv;
			chip->recharge_voltage= recharge_voltage;

			rc = 0;
		}
	}
	return rc;
}
/* W1m/W1n: full rebuild of stock status_change_work
 * (0x8bd1c48..0x8bd2978, 0xd30 bytes). handle_jeita and fcc_work are
 * stock-absent (no fcc_work kallsyms symbol; FCC adjustment runs inline
 * via POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT with a mA conversion). */
/* W1m/W1n v2: full stock-parity rebuild of status_change_work.
 * stock: 0x8bd1c48..0x8bd2978 (0xd30 bytes, 88 calls, 15 sections).
 * Section anchors cited per block (Image-b addresses). */
static void status_change_work(struct work_struct *work)
{
	struct lenovo_jeita_info *chip = container_of(work,
			struct lenovo_jeita_info, status_change_work.work);
	union power_supply_propval pval = {0, };
	int rc, temp, fcc_ua = 0, fv_uv = 0, iterm_delta = 0;
	int ibat = 0, vbat = 0, batt_status = 0, usb_209 = 0;
	int fv_result = 0, pd_val = 0, type_val = 0;
	int online = 0, pd2 = 0, pd3 = 0, type3 = 0;

	/* sec1 0x1c70..0x1ca4: batt/bms lazy get */
	if (!chip->batt_psy)
		chip->batt_psy = power_supply_get_by_name("battery");
	if (!chip->batt_psy)
		goto relax_exit;
	if (!chip->bms_psy)
		chip->bms_psy = power_supply_get_by_name("bms");
	if (!chip->bms_psy)
		goto relax_exit;

	/* sec2 0x1ca8..0x1cb4 / 0x1d7c: config_is_read gate */
	if (!chip->config_is_read) {
		if (chip->fv_votable)
			vote(chip->fv_votable, JEITA_VOTER, false, 0);
		if (chip->usb_icl_votable)
			vote(chip->usb_icl_votable, JEITA_VOTER, false, 0);
		if (chip->dc_suspend_votable)
			vote(chip->dc_suspend_votable, JEITA_VOTER, false, 0);
		if (chip->chg_disable_votable)
			vote(chip->chg_disable_votable, JEITA_VOTER, false, 0);
		goto relax_exit;
	}

	/* sec3 0x1cb8..0x1ce4: temp + hq_config_chg_max_fcc */
	ktime_get();
	rc = power_supply_get_property(chip->batt_psy,
			chip->jeita_fcc_config->param.psy_prop, &pval);
	if (rc < 0) {
		pr_err("Couldn't read sw term %d\n", rc);
		goto relax_exit;
	}
	temp = pval.intval;
	rc = hq_config_chg_max_fcc(chip->batt_psy, 8000000);
	if (rc < 0)
		pr_err("Couldn't config max fcc rc=%d\n", rc);

	/* sec4 0x1ce8..0x1d0c: get_val -> jeita_fcc_index */
	rc = get_val(chip->jeita_fcc_config->fcc_cfg,
			chip->jeita_fcc_config->param.rise_hys,
			chip->jeita_fcc_config->param.fall_hys,
			chip->jeita_fcc_index, temp,
			&chip->jeita_fcc_index, &fcc_ua);
	if (rc < 0)
		fcc_ua = 0;

	/* sec5 0x1d10..0x1d54: usb_psy lazy + pd/type */
	if (!chip->usb_psy) {
		chip->usb_psy = power_supply_get_by_name("usb");
		if (!chip->usb_psy) {
			pr_err("usb psy not found\n");
			goto after_usb;
		}
	}
	rc = power_supply_get_property(chip->usb_psy,
			POWER_SUPPLY_PROP_PD_ACTIVE, &pval);
	if (rc < 0)
		pr_err("Couldn't read pd status from usb psy rc=%d\n", rc);
	else
		pd_val = pval.intval;
	rc = power_supply_get_property(chip->usb_psy,
			POWER_SUPPLY_PROP_REAL_TYPE, &pval);
	if (rc < 0)
		pr_err("Couldn't read usb type from usb psy rc=%d\n", rc);
	else
		type_val = pval.intval;

	/* sec6 0x1d58..0x1e0c: fcc_index 150..449 magic */
	if (chip->jeita_fcc_index >= 150 && chip->jeita_fcc_index <= 449) {
		if (type_val == 2)
			fcc_ua = 5999999;
		else if (pd_val == 13)
			fcc_ua = 4499999;
	}
after_usb:

	/* sec7 0x1e10..0x1e6c: thermal level prop204 */
	if (temp >= 500) {
		if (!chip->thermal_cutback) {
			chip->thermal_cutback = 1;
			pval.intval = 1;
			power_supply_set_property(chip->batt_psy,
				POWER_SUPPLY_PROP_CHARGE_DISABLE, &pval);
		}
	} else if (temp >= 10 && temp <= 479) {
		if (chip->thermal_cutback) {
			chip->thermal_cutback = 0;
			pval.intval = 0;
			power_supply_set_property(chip->batt_psy,
				POWER_SUPPLY_PROP_CHARGE_DISABLE, &pval);
		}
	}

	/* sec8 0x1e70..0x1f10: usb type gate -> CHARGE_TERM_CURRENT */
	if (chip->usb_psy) {
		rc = power_supply_get_property(chip->usb_psy,
				POWER_SUPPLY_PROP_PD_ACTIVE, &pval);
		if (rc >= 0) {
			pd3 = pval.intval;
			rc = power_supply_get_property(chip->usb_psy,
					POWER_SUPPLY_PROP_REAL_TYPE, &pval);
			if (rc >= 0)
				type3 = pval.intval;
		}
		if (type3 == 2 || pd3 == 13) {
			if (temp <= 149)
				iterm_delta = -500;
			else if (temp >= 150 && temp <= 449)
				iterm_delta = -1600;
			else if (temp >= 450 && temp <= 499)
				iterm_delta = -100;
		} else {
			iterm_delta = -500;
		}
	}
	pval.intval = iterm_delta;
	rc = power_supply_set_property(chip->batt_psy,
			POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT, &pval);
	if (rc < 0)
		pr_err("Couldn't set term current property rc=%d\n", rc);

	/* sec9 0x1f14..0x1f4c: usb ONLINE/PD gate */
	rc = power_supply_get_property(chip->usb_psy,
			POWER_SUPPLY_PROP_ONLINE, &pval);
	if (rc < 0)
		pr_err("Couldn't read sw term %d\n", rc);
	else
		online = pval.intval;
	rc = power_supply_get_property(chip->usb_psy,
			POWER_SUPPLY_PROP_PD_ACTIVE, &pval);
	if (rc < 0)
		pr_err("Couldn't read sw term %d\n", rc);
	else
		pd2 = pval.intval;
	if (online == 0 && pd2 == 0)
		goto sec12_fcc;

	/* sec10 0x1f50..0x1f9c: FV + CP_DISABLE votables */
	if (!chip->fv_votable) {
		chip->fv_votable = find_votable("FV");
		if (!chip->fv_votable) {
			pr_err("cannt find fv_votable vote\n");
			goto relax_exit;
		}
	}
	fv_result = get_effective_result(chip->fv_votable);
	if (!chip->cp_disable_votable) {
		chip->cp_disable_votable = find_votable("CP_DISABLE");
		if (!chip->cp_disable_votable) {
			pr_err("cannt find cp_disabled_votable vote\n");
			goto relax_exit;
		}
	}
	(void)get_effective_result(chip->cp_disable_votable);

	/* sec11 0x1f9c..0x20bc: ibat/vbat/status/usb209 + iterm1600 chain */
	rc = power_supply_get_property(chip->batt_psy,
			POWER_SUPPLY_PROP_CURRENT_NOW, &pval);
	if (rc < 0)
		pr_err("Couldn't get ibat\n");
	else
		ibat = pval.intval;
	rc = power_supply_get_property(chip->batt_psy,
			POWER_SUPPLY_PROP_VOLTAGE_NOW, &pval);
	if (rc < 0)
		pr_err("Couldn't get vbat\n");
	else
		vbat = pval.intval;
	rc = power_supply_get_property(chip->batt_psy,
			POWER_SUPPLY_PROP_STATUS, &pval);
	if (rc < 0) {
		pr_err("Couldn't get battery status\n");
		goto sec12_fcc;
	}
	batt_status = pval.intval;
	rc = power_supply_get_property(chip->usb_psy,
			POWER_SUPPLY_PROP_ENABLE_SW_TERM, &pval);
	if (rc < 0)
		pr_err("Couldn't read sw term %d\n", rc);
	else
		usb_209 = pval.intval;
	chip->usb209_flag = (usb_209 != 0);

	if (iterm_delta == -1600 && batt_status == 1 &&
	    ibat <= 0x1ab3f0 && vbat > (fv_result - 50000)) {
		if (usb_209)
			goto sec12_fcc;
		pval.intval = 1;
		rc = power_supply_set_property(chip->usb_psy,
				POWER_SUPPLY_PROP_ENABLE_SW_TERM, &pval);
		if (rc < 0) {
			pr_err("Couldn't set term current property rc=%d\n", rc);
			goto sec12_fcc;
		}
		pr_err("iterm = 1600mA, vbat = %d, ibat = %d\n", vbat, ibat);
		goto sec12_fcc;
	}

	if (iterm_delta == -1600 && batt_status == 1) {
		if (usb_209 == 0)
			goto sec12_fcc;
		pval.intval = 0;
		rc = power_supply_set_property(chip->usb_psy,
				POWER_SUPPLY_PROP_ENABLE_SW_TERM, &pval);
		if (rc < 0) {
			pr_err("Couldn't set term current property rc=%d\n", rc);
			goto sec12_fcc;
		}
		pr_err("iterm = 1600mA, vbat = %d, ibat = %d\n", vbat, ibat);
		goto sec12_fcc;
	}

	if (usb_209)
		goto sec12_fcc;
	pval.intval = 1;
	rc = power_supply_set_property(chip->usb_psy,
			POWER_SUPPLY_PROP_ENABLE_SW_TERM, &pval);
	if (rc < 0)
		pr_err("Couldn't set term current property rc=%d\n", rc);

sec12_fcc:
	/* sec12a 0x222c/0x2234: fcc_votable lazy; fail -> 0x2898 */
	if (!chip->fcc_votable) {
		chip->fcc_votable = find_votable("FCC");
		if (!chip->fcc_votable)
			goto err_exit;
	}

	/* sec12b 0x2378: vote(fcc, JEITA_VOTER, fcc_ua!=0, fcc_ua) */
	vote(chip->fcc_votable, JEITA_VOTER, fcc_ua != 0, fcc_ua);

	/* sec12c 0x2390..0x23f8: is_lcd_on -> thermal prop33 */
	if (is_lcd_on()) {
		rc = power_supply_get_property(chip->batt_psy,
				POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT, &pval);
		if (rc < 0) {
			pr_err("Couldn't get thermal level\n");
			goto sec13_fv;
		}
		rc = power_supply_set_property(chip->batt_psy,
				POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT, &pval);
		if (rc < 0)
			pr_err("Couldn't set thermal level\n");
	} else {
		vote(chip->fcc_votable, "THERMAL_DAEMON_VOTER", false, 0);
	}

	/* sec12d 0x23fc..0x2488: batt status + get_effective_result(fcc) +
	 * CP_DISABLE client-vote(ILIM_VOTER) -> vote off */
	rc = power_supply_get_property(chip->batt_psy,
			POWER_SUPPLY_PROP_STATUS, &pval);
	if (rc < 0)
		pr_err("Couldn't get battery status\n");
	batt_status = pval.intval;
	fv_result = get_effective_result(chip->fcc_votable);
	if (fv_result >= 0x2dc6c1 && batt_status == 1 &&
	    get_client_vote(chip->cp_disable_votable, "ILIM_VOTER") == 1) {
		vote(chip->cp_disable_votable, "ILIM_VOTER", false, 0);
		pr_err("cp is disable,try open cp\n");
	}

sec13_fv:
	/* sec13 0x248c..0x25cc: FV get_val + magic + set RECHARGE_SOC(154) */
	rc = get_val(chip->jeita_fv_config->fv_cfg,
			chip->jeita_fv_config->param.rise_hys,
			chip->jeita_fv_config->param.fall_hys,
			chip->jeita_fv_index, temp,
			&chip->jeita_fv_index, &fv_uv);
	if (rc < 0)
		fv_uv = 0;
	rc = power_supply_get_property(chip->usb_psy,
			POWER_SUPPLY_PROP_PD_ACTIVE, &pval);
	if (rc < 0)
		pr_err("Couldn't read pd status from usb psy rc=%d\n", rc);
	else
		pd_val = pval.intval;
	rc = power_supply_get_property(chip->usb_psy,
			POWER_SUPPLY_PROP_REAL_TYPE, &pval);
	if (rc < 0)
		pr_err("Couldn't read usb type from usb psy rc=%d\n", rc);
	else
		type_val = pval.intval;
	if (type_val == 2 || pd_val == 13) {
		if (temp <= 149)
			fv_uv = 4450000;
		else if (temp <= 449)
			fv_uv = 4490000;
		else
			fv_uv = 4200000;
	} else {
		if (temp <= 449)
			fv_uv = 4450000;
		else
			fv_uv = 4200000;
	}
	pval.intval = (fv_uv / 1000) - 100;
	rc = power_supply_set_property(chip->batt_psy,
			POWER_SUPPLY_PROP_RECHARGE_SOC, &pval);
	if (rc < 0)
		pr_err("Couldn't set term current property rc=%d\n", rc);

	/* sec13b 0x25cc..0x2634: chg_disable lazy + batt 17/12 */
	if (!chip->chg_disable_votable) {
		chip->chg_disable_votable = find_votable("CHG_DISABLE");
		if (!chip->chg_disable_votable) {
			pr_err("Couldn't read sw term %d\n", rc);
			goto sec13_tail;
		}
	}
	rc = power_supply_get_property(chip->batt_psy,
			POWER_SUPPLY_PROP_CURRENT_NOW, &pval);
	if (rc < 0) {
		pr_err("Couldn't get ibat\n");
		goto sec13_tail;
	}
	ibat = pval.intval;
	rc = power_supply_get_property(chip->batt_psy,
			POWER_SUPPLY_PROP_VOLTAGE_NOW, &pval);
	if (rc < 0) {
		pr_err("Couldn't get vbat\n");
		goto sec13_tail;
	}
	vbat = pval.intval;
	goto sec14_softhot;

sec13_tail:
	/* 0x2648..0x2708: FV/USB_ICL/DC_SUSPEND lazy + batt VMAX + FV vote */
	chip->fv_votable = find_votable("FV");
	if (!chip->fv_votable)
		goto err_exit;
	chip->usb_icl_votable = find_votable("USB_ICL");
	if (!chip->usb_icl_votable)
		goto tail_usb_dc;
	chip->dc_suspend_votable = find_votable("DC_SUSPEND");
	if (!chip->dc_suspend_votable)
		goto tail_usb_dc;
	rc = power_supply_get_property(chip->batt_psy,
			POWER_SUPPLY_PROP_VOLTAGE_MAX, &pval);
	if (rc)
		goto sec15_fv_vote;
	if (pval.intval == fv_uv)
		goto sec15_fv_vote;
	if (fv_uv < 1)
		goto sec15_fv_vote;
	rc = power_supply_get_property(chip->batt_psy,
			POWER_SUPPLY_PROP_VOLTAGE_NOW, &pval);
	if (rc)
		goto sec15_fv_vote;
	if (pval.intval > fv_uv) {
		vote(chip->usb_icl_votable, JEITA_VOTER, true, 0);
		vote(chip->dc_suspend_votable, JEITA_VOTER, true, 1);
	} else {
		vote(chip->usb_icl_votable, JEITA_VOTER, false, 0);
		vote(chip->dc_suspend_votable, JEITA_VOTER, false, 0);
	}
	goto sec15_fv_vote;

sec14_softhot:
	/* sec14 0x2710..0x2970: SOFT_HOT chain */
	rc = power_supply_get_property(chip->usb_psy,
			POWER_SUPPLY_PROP_ONLINE, &pval);
	if (rc < 0) {
		pr_err("Couldn't read sw term %d\n", rc);
		goto sec13_tail;
	}
	pd_val = pval.intval;
	rc = power_supply_get_property(chip->usb_psy,
			POWER_SUPPLY_PROP_PD_ACTIVE, &pval);
	if (rc < 0)
		goto sec14_print;
	pd2 = pval.intval;
	if (pd_val != 0 || pd2 != 0)
		goto sec14_print;
	vote(chip->chg_disable_votable, "SOFT_HOT_CHG_VOTER", false, 0);
	chip->iterm_retry_count = 0;
	chip->hot_vbat_flag = 0;
	chip->recharge_retry_count = 0;

sec14_print:
	pr_err("temp = %d, ibat = %d, vbat = %d, iterm = %d/%d/%d\n",
		temp, ibat, vbat, chip->iterm_retry_count,
		chip->hot_vbat_flag, chip->recharge_retry_count);
	if (temp < 450 || temp > 499)
		goto sec14_warm;
	if (ibat > 0x8164f || vbat < 0x3fef30)
		goto sec14_hot_check;
	if (chip->hot_vbat_flag == 0)
		goto sec14_ibat_low;

sec14_hot_check:
	if (vbat >= 0x4015fd)
		goto sec13_tail;
	if (chip->hot_vbat_flag == 0)
		goto sec13_tail;
	if (chip->recharge_retry_count < 10) {
		chip->recharge_retry_count++;
		pr_err("chip->recharge_retry_count = %d\n",
			chip->recharge_retry_count);
		goto sec14_hot_check;
	}
	vote(chip->chg_disable_votable, "SOFT_HOT_CHG_VOTER", false, 0);
	chip->hot_vbat_flag = 0;
	chip->recharge_retry_count = 0;
	pr_err("hot environment vbat < recharge_volt\n");
	goto sec13_tail;

sec14_warm:
	if (temp < 10 || temp > 449)
		goto sec13_tail;
	vote(chip->chg_disable_votable, "SOFT_HOT_CHG_VOTER", false, 0);
	chip->iterm_retry_count = 0;
	chip->hot_vbat_flag = 0;
	chip->recharge_retry_count = 0;
	goto sec13_tail;

sec14_ibat_low:
	if (chip->iterm_retry_count < 20) {
		chip->iterm_retry_count++;
		pr_err("chip->iterm_retry_count = %d\n",
			chip->iterm_retry_count);
		goto sec14_hot_check;
	}
	vote(chip->chg_disable_votable, "SOFT_HOT_CHG_VOTER", true, 0);
	pval.intval = 1;
	power_supply_set_property(chip->batt_psy,
			POWER_SUPPLY_PROP_FORCE_5V, &pval);
	chip->iterm_retry_count = 0;
	chip->hot_vbat_flag = 1;
	pr_err("hot environment ibat < 500mA, disable sw term\n");
	goto sec14_hot_check;

sec15_fv_vote:
	/* sec15 0x286c..0x2894: vote(fv) + ktime */
	vote(chip->fv_votable, JEITA_VOTER, fv_uv != 0, fv_uv);
	chip->jeita_last_update_time = ktime_get();

tail_usb_dc:
	/* tail 0x2250..0x22d8: usb/dc PRESENT -> clear usb_icl vote -> relax */
	if (!chip->usb_psy)
		chip->usb_psy = power_supply_get_by_name("usb");
	online = 0;
	if (chip->usb_psy &&
	    power_supply_get_property(chip->usb_psy,
			POWER_SUPPLY_PROP_PRESENT, &pval) >= 0)
		online = pval.intval;
	if (!chip->dc_psy)
		chip->dc_psy = power_supply_get_by_name("dc");
	if (chip->dc_psy &&
	    power_supply_get_property(chip->dc_psy,
			POWER_SUPPLY_PROP_PRESENT, &pval) >= 0)
		online |= pval.intval;
	if (online == 0 && chip->usb_icl_votable)
		vote(chip->usb_icl_votable, JEITA_VOTER, false, 0);
	goto relax_exit;

err_exit:
	/* 0x2898: "Couldn't handle sw jeita rc = %d" then fall through to tail */
	pr_err("Couldn't handle sw jeita rc = %d\n", -EINVAL);
	goto tail_usb_dc;

relax_exit:
	__pm_relax(chip->lenovo_jeita_ws);
}

static int jeita_notifier_call(struct notifier_block *nb,
		unsigned long ev, void *v)
{
	struct power_supply *psy = v;
	struct lenovo_jeita_info*chip = container_of(nb, struct lenovo_jeita_info, nb);

	if (ev != PSY_EVENT_PROP_CHANGED)
		return NOTIFY_OK;

	if ((strcmp(psy->desc->name, "battery") == 0)
			|| (strcmp(psy->desc->name, "usb") == 0)) {
		__pm_stay_awake(chip->lenovo_jeita_ws);
		schedule_delayed_work(&chip->status_change_work, 0);
	}

	return NOTIFY_OK;
}

static int jeita_register_notifier(struct lenovo_jeita_info *chip)
{
	int rc;

	chip->nb.notifier_call = jeita_notifier_call;
	rc = power_supply_reg_notifier(&chip->nb);
	if (rc < 0) {
		pr_err("Couldn't register psy notifier rc = %d\n", rc);
		return rc;
	}

	return 0;
}

int lenovo_jeita_init(struct device *dev, bool lenovo_jeita_en)
{
	int rc;
	struct lenovo_jeita_info* chip;

	if(!lenovo_jeita_en){
		pr_err("lenovo_jeita_en:%d,return\n",lenovo_jeita_en);
		return 0;
	}
	if (the_chip) {
		pr_err("Already initialized\n");
		return -EINVAL;
	}

	chip = devm_kzalloc(dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->lenovo_jeita_ws = wakeup_source_register(dev, "lenovo_jeita");
	if (!chip->lenovo_jeita_ws)
		return -EINVAL;

	chip->dev = dev;
	chip->jeita_fcc_index = -EINVAL;
	chip->jeita_fv_index = -EINVAL;

	chip->jeita_fcc_config = devm_kzalloc(dev,
			sizeof(struct jeita_fcc_cfg), GFP_KERNEL);
	chip->jeita_fv_config = devm_kzalloc(dev,
			sizeof(struct jeita_fv_cfg), GFP_KERNEL);
	if (!chip->jeita_fcc_config || !chip->jeita_fv_config)
		return -ENOMEM;

	chip->jeita_fcc_config->param.psy_prop = POWER_SUPPLY_PROP_TEMP;
	chip->jeita_fcc_config->param.prop_name = "BATT_TEMP";
	chip->jeita_fcc_config->param.rise_hys = 0;
	chip->jeita_fcc_config->param.fall_hys = 0;
	chip->jeita_fv_config->param.psy_prop = POWER_SUPPLY_PROP_TEMP;
	chip->jeita_fv_config->param.prop_name = "BATT_TEMP";
	chip->jeita_fv_config->param.rise_hys = 0;
	chip->jeita_fv_config->param.fall_hys = 0;

	INIT_DELAYED_WORK(&chip->status_change_work, status_change_work);
	INIT_DELAYED_WORK(&chip->get_config_work, get_config_work);

	rc = jeita_register_notifier(chip);
	if (rc < 0) {
		pr_err("Couldn't register psy notifier rc = %d\n", rc);
		goto release_wakeup_source;
	}

	schedule_delayed_work(&chip->get_config_work,
			msecs_to_jiffies(GET_CONFIG_DELAY_MS));

	the_chip = chip;
	pr_err("lenovo_jeita_init success\n");
	
	return 0;

release_wakeup_source:
	wakeup_source_unregister(chip->lenovo_jeita_ws);
	return rc;
}

void lenovo_jeita_deinit(void)
{
	struct lenovo_jeita_info *chip = the_chip;

	if (!chip)
		return;

	cancel_delayed_work_sync(&chip->status_change_work);
	cancel_delayed_work_sync(&chip->get_config_work);
	power_supply_unreg_notifier(&chip->nb);
	wakeup_source_unregister(chip->lenovo_jeita_ws);
	the_chip = NULL;
}
