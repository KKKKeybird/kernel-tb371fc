/*
 * Copyright (c) 2019, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#define pr_fmt(fmt)	"mi-dsi-panel:[%s:%d] " fmt, __func__, __LINE__
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <video/mipi_display.h>

#include "sde_kms.h"
#include "dsi_panel.h"
#include "dsi_display.h"
#include "dsi_ctrl_hw.h"
#include "dsi_parser.h"
#include "dsi_mi_feature.h"
#include "drm_mipi_dsi.h"
#include "../../../../kernel/irq/internals.h"

#include "mi_disp_nvt_alpha_data.h"
#include "mi_disp_lhbm.h"

#define to_dsi_display(x) container_of(x, struct dsi_display, host)

static struct dsi_read_config g_dsi_read_cfg;
struct dsi_panel *g_panel;

static void panelon_cabc_delayed_work(struct work_struct *work)
{
	struct dsi_panel_mi_cfg *mi_cfg = container_of(work,
				struct dsi_panel_mi_cfg, cabc_delayed_work.work);
	struct dsi_panel *dsi_panel = mi_cfg->dsi_panel;
	int rc = 0;

	if (!dsi_panel) {
		pr_err("invalid params\n");
		return;
	}

	mutex_lock(&dsi_panel->panel_lock);

	if (!dsi_panel->panel_initialized || !mi_cfg->last_bl_level) {
		DSI_INFO("Panel not initialized! \n");
		goto exit;
	}

	if (dsi_panel->host_config.phy_type == DSI_PHY_TYPE_CPHY) {
		if (dsi_panel->mi_cfg.cabc_current_status != dsi_panel->mi_cfg.cabc_temp_status){
			switch (dsi_panel->mi_cfg.cabc_temp_status) {
			case DISPPARAM_CABCUI_ON:
				DSI_INFO("cabcuion\n");
				rc = dsi_panel_tx_cmd_set(dsi_panel, DSI_CMD_SET_MI_CABCUION);
				break;
			case DISPPARAM_CABCSTILL_ON:
				DSI_INFO("cabcstillon\n");
				rc = dsi_panel_tx_cmd_set(dsi_panel, DSI_CMD_SET_MI_CABCSTILLON);
				break;
			case DISPPARAM_CABCMOVIE_ON:
				DSI_INFO("cabcmovieon\n");
				dsi_panel_tx_cmd_set(dsi_panel, DSI_CMD_SET_MI_CABCMOVIEON);
				break;
			case DISPPARAM_CABC_OFF:
				DSI_INFO("cabcoff\n");
				rc = dsi_panel_tx_cmd_set(dsi_panel, DSI_CMD_SET_MI_CABCOFF);
				break;
			default:
				DSI_INFO("cabc = 0x%X \n", dsi_panel->mi_cfg.cabc_temp_status);
				break;
			}
			dsi_panel->mi_cfg.cabc_current_status = dsi_panel->mi_cfg.cabc_temp_status;
			if (rc) {
				DSI_ERR("cabc failed rc=%d\n", rc);
			}
		}
	}

exit:
	mutex_unlock(&dsi_panel->panel_lock);
}

static void enter_aod_delayed_work(struct work_struct *work)
{
	struct dsi_panel_mi_cfg *mi_cfg = container_of(work,
				struct dsi_panel_mi_cfg, enter_aod_delayed_work.work);
	struct dsi_panel *panel = mi_cfg->dsi_panel;

	if (!panel)
		return;

	mutex_lock(&panel->panel_lock);

	if (!panel->panel_initialized)
		goto exit;

	if (mi_cfg->local_hbm_enabled) {
		if (panel->power_mode == SDE_MODE_DPMS_LP1 ||
				panel->power_mode == SDE_MODE_DPMS_LP2) {
			if (mi_cfg->fp_status == AUTH_START) {
				if (!mi_cfg->unset_doze_brightness) {
					mi_cfg->unset_doze_brightness = mi_cfg->doze_brightness_state;
				}
				pr_info("delayed_work runing --- set doze brightness\n");
				dsi_panel_set_doze_brightness(panel, mi_cfg->unset_doze_brightness, false);
			}
		}
	}else {
		if (panel->power_mode == SDE_MODE_DPMS_LP1 ||
				panel->power_mode == SDE_MODE_DPMS_LP2) {
			if (mi_cfg->layer_fod_unlock_success || mi_cfg->sysfs_fod_unlock_success) {
				pr_info("[%d,%d]Fod fingerprint unlocked successfully, skip to enter aod mode\n",
					mi_cfg->layer_fod_unlock_success, mi_cfg->sysfs_fod_unlock_success);
				goto exit;
			} else {
				if (!mi_cfg->unset_doze_brightness) {
					mi_cfg->unset_doze_brightness = mi_cfg->doze_brightness_state;
				}
				pr_info("delayed_work runing --- set doze brightness, unset_doze_brightness:%d\n", mi_cfg->unset_doze_brightness);
				if (mi_cfg->layer_aod_flag) {
					dsi_panel_set_doze_brightness(panel, mi_cfg->unset_doze_brightness, false);
					mi_cfg->fod_to_nolp = true;
				}
				else
					pr_info("delayed_work runing --- skip into doze\n");
			}
		}
	}

exit:
	mutex_unlock(&panel->panel_lock);
}


static int dsi_panel_parse_gamma_config(struct dsi_panel *panel,
				struct device_node *of_node)
{
	int rc = 0;
	struct dsi_parser_utils *utils = &panel->utils;
	struct dsi_panel_mi_cfg *mi_cfg = &panel->mi_cfg;

	if (mi_cfg->gamma_update_flag) {
		rc = utils->read_u32(of_node,
				"mi,mdss-dsi-panel-gamma-flash-read-total-param",
				&mi_cfg->gamma_cfg.flash_read_total_param);
		if (rc)
			pr_info("failed to get mi,mdss-dsi-panel-gamma-flash-read-total-param\n");

		rc = utils->read_u32(of_node,
				"mi,mdss-dsi-panel-gamma-flash-read-c1-index",
				&mi_cfg->gamma_cfg.flash_read_c1_index);
		if (rc)
			pr_info("failed to get mi,mdss-dsi-panel-gamma-flash-read-c1-index\n");

		rc = utils->read_u32(of_node,
				"mi,mdss-dsi-panel-gamma-update-c8-index",
				&mi_cfg->gamma_cfg.update_c8_index);
		if (rc)
			pr_info("failed to get mi,mdss-dsi-panel-gamma-update-c8-index\n");

		rc = utils->read_u32(of_node,
				"mi,mdss-dsi-panel-gamma-update-c9-index",
				&mi_cfg->gamma_cfg.update_c9_index);
		if (rc)
			pr_info("failed to get mi,mdss-dsi-panel-gamma-update-c9-index\n");

		rc = utils->read_u32(of_node,
				"mi,mdss-dsi-panel-gamma-update-b3-index",
				&mi_cfg->gamma_cfg.update_b3_index);
		if (rc)
			pr_info("failed to get mi,mdss-dsi-panel-gamma-update-b3-index\n");

		mi_cfg->gamma_cfg.black_setting_flag = utils->read_bool(of_node,
				"mi,mdss-dsi-panel-gamma-black-setting-flag");
		if (!mi_cfg->gamma_cfg.black_setting_flag)
			pr_info("can't get mi,mdss-dsi-panel-gamma-black-setting-flag\n");
	}

	return rc;
}

static int dsi_panel_parse_greenish_gamma_config(struct dsi_panel *panel,
				struct device_node *of_node)
{
	int rc = 0;
	struct dsi_parser_utils *utils = &panel->utils;
	struct dsi_panel_mi_cfg *mi_cfg = &panel->mi_cfg;

	if (mi_cfg->greenish_gamma_update_flag) {
		rc = utils->read_u32(of_node,
				"mi,mdss-dsi-greenish-update-gamma-length",
				&mi_cfg->greenish_gamma_read_len);
		if (rc)
			pr_info("failed to get mi,mdss-dsi-greenish-update-gamma-length\n");

		rc = utils->read_u32(of_node,
				"mi,mdss-dsi-greenish-update-gamma-offset",
				&mi_cfg->greenish_gamma_cfg.greenish_gamma_update_offset);
		if (rc)
			pr_info("failed to get mi,mdss-dsi-greenish-update-gamma-offset\n");

		rc = utils->read_u32(of_node,
				"mi,mdss-dsi-update-gamma-param-count",
				&mi_cfg->greenish_gamma_cfg.greenish_gamma_update_param_count);
		if (rc)
			pr_info("failed to get mi,mdss-dsi-update-gamma-param-count\n");

		rc = utils->read_u32(of_node,
				"mi,mdss-dsi-update-gamma-1st-index",
				&mi_cfg->greenish_gamma_cfg.index_1st_param);
		if (rc)
			pr_info("failed to get mdss-dsi-update-gamma-1st-index\n");

		rc = utils->read_u32(of_node,
				"mi,mdss-dsi-update-gamma-2nd-index",
				&mi_cfg->greenish_gamma_cfg.index_2nd_param);
		if (rc)
			pr_info("failed to get mdss-dsi-update-gamma-2nd-index\n");

		rc = utils->read_u32(of_node,
				"mi,mdss-dsi-update-gamma-3rd-index",
				&mi_cfg->greenish_gamma_cfg.index_3rd_param);
		if (rc)
			pr_info("failed to get mdss-dsi-update-gamma-3rd-index\n");

		rc = utils->read_u32(of_node,
				"mi,mdss-dsi-update-gamma-4th-index",
				&mi_cfg->greenish_gamma_cfg.index_4th_param);
		if (rc)
			pr_info("failed to get mdss-dsi-update-gamma-4th-index\n");

		rc = utils->read_u32(of_node,
				"mi,mdss-dsi-update-gamma-5th-index",
				&mi_cfg->greenish_gamma_cfg.index_5th_param);
		if (rc)
			pr_info("failed to get mdss-dsi-update-gamma-5th-index\n");

		rc = utils->read_u32(of_node,
				"mi,mdss-dsi-update-gamma-6th-index",
				&mi_cfg->greenish_gamma_cfg.index_6th_param);
		if (rc)
			pr_info("failed to get mdss-dsi-update-gamma-6th-index\n");
	}

	return rc;
}

static int dsi_panel_parse_white_point_config(struct dsi_panel *panel,
				struct device_node *of_node)
{
	int rc = 0;
	struct dsi_parser_utils *utils = &panel->utils;
	struct dsi_panel_mi_cfg *mi_cfg = &panel->mi_cfg;

	rc = utils->read_u32(of_node,
			"mi,mdss-dsi-white-point-register-read-length",
			&mi_cfg->wp_reg_read_len);
	if (rc)
		pr_info("failed to get mi,mdss-dsi-white-point-register-read-length\n");

	rc = utils->read_u32(of_node,
			"mi,mdss-dsi-white-point-info-index",
			&mi_cfg->wp_info_index);
	if (rc)
		pr_info("failed to get mi,mdss-dsi-white-point-info-index\n");

	rc = utils->read_u32(of_node,
			"mi,mdss-dsi-white-point-info-length",
			&mi_cfg->wp_info_len);
	if (rc)
		pr_info("failed to get mi,mdss-dsi-white-point-info-length\n");

	return rc;
}

static int dsi_panel_parse_elvss_dimming_config(struct dsi_panel *panel,
				struct device_node *of_node)
{
	int rc = 0;
	struct dsi_parser_utils *utils = &panel->utils;
	struct dsi_panel_mi_cfg *mi_cfg = &panel->mi_cfg;

	rc = utils->read_u32(of_node, "mi,mdss-dsi-elvss-dimming-register-read-length",
					&mi_cfg->elvss_dimming_read_len);
	if (rc)
		pr_err("failed to get mi,mdss-dsi-elvss-dimming-register-read-length\n");

	rc = utils->read_u32(of_node, "mi,mdss-dsi-elvss-dimming-update-hbm-fod-on-index",
					&mi_cfg->update_hbm_fod_on_index);
	if (rc)
		pr_err("failed to get mi,mdss-dsi-elvss-dimming-update-hbm-fod-on-index\n");

	rc = utils->read_u32(of_node, "mi,mdss-dsi-elvss-dimming-update-hbm-fod-off-index",
					&mi_cfg->update_hbm_fod_off_index);
	if (rc)
		pr_err("failed to get mi,mdss-dsi-elvss-dimming-update-hbm-fod-off-index\n");

	return rc;
}

int dsi_panel_parse_mi_config(struct dsi_panel *panel,
				struct device_node *of_node)
{
	int rc = 0;
	struct dsi_parser_utils *utils = &panel->utils;
	struct dsi_panel_mi_cfg *mi_cfg = &panel->mi_cfg;
	u32 length = 0;
	const u32 *arr;

	mi_cfg->dsi_panel = panel;
	g_panel = panel;

	INIT_DELAYED_WORK(&mi_cfg->enter_aod_delayed_work, enter_aod_delayed_work);
	mi_cfg->aod_wakelock = wakeup_source_create("aod_wakelock");
	wakeup_source_add(mi_cfg->aod_wakelock);

	mi_cfg->bl_is_big_endian= utils->read_bool(utils->data,
			"mi,mdss-dsi-bl-dcs-big-endian-type");

	rc = utils->read_u64(utils->data, "mi,panel-id", &mi_cfg->panel_id);
	if (rc) {
		mi_cfg->panel_id = 0;
		pr_info("mi,panel-id not specified\n");
	} else {
		pr_info("mi,panel-id is 0x%llx\n", mi_cfg->panel_id);
	}

	mi_cfg->mi_feature_enabled = utils->read_bool(of_node,
			"mi,feature-enabled");
	if (mi_cfg->mi_feature_enabled) {
		pr_info("mi feature enabled\n");
	} else {
		pr_info("mi feature disabled\n");
		return 0;
	}

	mi_cfg->hbm_51_ctrl_flag = utils->read_bool(utils->data,
		"mi,mdss-dsi-panel-hbm-51-ctrl-flag");
	if (mi_cfg->hbm_51_ctrl_flag) {
		rc = utils->read_u32(of_node,
			"mi,mdss-dsi-panel-hbm-off-51-index", &mi_cfg->hbm_off_51_index);
		if (rc) {
			pr_err("mi,mdss-dsi-panel-hbm-off-51-index not defined,but need\n");
		}
		rc = utils->read_u32(of_node,
			"mi,mdss-dsi-panel-fod-off-51-index", &mi_cfg->fod_off_51_index);
		if (rc) {
			pr_err("mi,mdss-dsi-panel-fod-off-51-index not defined,but need\n");
		}
		mi_cfg->vi_setting_enabled = utils->read_bool(of_node,
			"mi,mdss-dsi-panel-vi-setting-enabled");
		if (mi_cfg->vi_setting_enabled) {
			pr_info("mi vi_setting_enabled = %d\n", mi_cfg->vi_setting_enabled);
			rc = utils->read_u32(of_node,
				"mi,mdss-dsi-panel-vi-switch-threshold", &mi_cfg->vi_switch_threshold);
			if (rc)
				pr_err("mi,mdss-dsi-panel-vi-switch-threshold not defined,but need\n");
		}
		mi_cfg->dynamic_elvss_enabled = utils->read_bool(of_node,
			"mi,mdss-dsi-panel-dynamic-elvss-enabled");
		if (mi_cfg->dynamic_elvss_enabled) {
			pr_info("mi dynamic_elvss_enabled = %d\n", mi_cfg->dynamic_elvss_enabled);
		}
	}

	rc = utils->read_u32(of_node,
		"mi,mdss-dsi-panel-fod-off-b5-index", &mi_cfg->fod_off_b5_index);
	if (rc) {
		pr_err("mi,mdss-dsi-panel-fod-off-b5-index not defined\n");
	}

	mi_cfg->fod_dimlayer_enabled = utils->read_bool(of_node,
		"mi,mdss-dsi-panel-fod-dimlayer-enabled");
	if (mi_cfg->fod_dimlayer_enabled) {
		pr_info("fod dimlayer enabled.\n");
	} else {
		pr_info("fod dimlayer disabled.\n");
	}

	if (mi_cfg->fod_dimlayer_enabled) {
		mi_cfg->prepare_before_fod_hbm_on = utils->read_bool(of_node,
			"mi,mdss-panel-prepare-before-fod-hbm-on");
		if (mi_cfg->prepare_before_fod_hbm_on) {
			pr_info("fod hbm on need prepare.\n");
		} else {
			pr_info("fod hbm on doesn't need prepare.\n");
		}

		mi_cfg->delay_before_fod_hbm_on = utils->read_bool(of_node,
			"mi,mdss-panel-delay-before-fod-hbm-on");
		if (mi_cfg->delay_before_fod_hbm_on) {
			pr_info("delay before fod hbm on.\n");
		}

		mi_cfg->delay_after_fod_hbm_on = utils->read_bool(of_node,
			"mi,mdss-panel-delay-after-fod-hbm-on");
		if (mi_cfg->delay_after_fod_hbm_on) {
			pr_info("delay after fod hbm on.\n");
		}

		mi_cfg->delay_before_fod_hbm_off = utils->read_bool(of_node,
			"mi,mdss-panel-delay-before-fod-hbm-off");
		if (mi_cfg->delay_before_fod_hbm_off) {
			pr_info("delay before fod hbm off.\n");
		}

		mi_cfg->delay_after_fod_hbm_off = utils->read_bool(of_node,
			"mi,mdss-panel-delay-after-fod-hbm-off");
		if (mi_cfg->delay_after_fod_hbm_off) {
			pr_info("delay after fod hbm off.\n");
		}

		mi_cfg->fod_skip_nolp = utils->read_bool(of_node,
			"mi,mdss-panel-fod-skip-nolp");
		if (mi_cfg->fod_skip_nolp) {
			pr_info("j1 fod skip nolp\n");
		}

		rc = utils->read_u32(of_node,
			"mi,mdss-dsi-dimlayer-brightness-alpha-lut-item-count",
			&mi_cfg->brightnes_alpha_lut_item_count);
		if (rc || mi_cfg->brightnes_alpha_lut_item_count <= 0) {
			pr_err("can't get brightnes_alpha_lut_item_count\n");
			mi_cfg->fod_dimlayer_enabled = false;
			goto skip_dimlayer_parse;
		}

		arr = utils->get_property(utils->data,
				"mi,mdss-dsi-dimlayer-brightness-alpha-lut", &length);

		length = length / sizeof(u32);

		pr_info("length: %d\n", length);
		if (!arr || length & 0x1 || length != mi_cfg->brightnes_alpha_lut_item_count * 2) {
			pr_err("read mi,mdss-dsi-dimlayer-brightness-alpha-lut failed\n");
			mi_cfg->fod_dimlayer_enabled = false;
			goto skip_dimlayer_parse;
		}

		mi_cfg->brightness_alpha_lut = (struct brightness_alpha *)kzalloc(length * sizeof(u32), GFP_KERNEL);
		if (!mi_cfg->brightness_alpha_lut) {
			pr_err("no memory for brightnes alpha lut\n");
			mi_cfg->fod_dimlayer_enabled = false;
			goto skip_dimlayer_parse;
		}

		rc = utils->read_u32_array(utils->data, "mi,mdss-dsi-dimlayer-brightness-alpha-lut",
			(u32 *)mi_cfg->brightness_alpha_lut, length);
		if (rc) {
			pr_err("cannot read mi,mdss-dsi-dimlayer-brightness-alpha-lut\n");
			mi_cfg->fod_dimlayer_enabled = false;
			kfree(mi_cfg->brightness_alpha_lut);
			goto skip_dimlayer_parse;
		}
	}

skip_dimlayer_parse:

	mi_cfg->local_hbm_enabled = utils->read_bool(utils->data, "mi,local-hbm-enabled");
	if (mi_cfg->local_hbm_enabled) {
		pr_info("local_hbm_enabled\n");
		mi_cfg->fod_lhbm_87reg_ctrl_flag = utils->read_bool(utils->data,
		"mi,mdss-dsi-panel-fod-lhbm-87reg-ctrl-flag");
		if (mi_cfg->fod_lhbm_87reg_ctrl_flag) {
			rc = utils->read_u32(of_node,
				"mi,mdss-dsi-panel-fod-lhbm-white-1000nit-87reg-index", &mi_cfg->fod_lhbm_white_1000nit_87reg_index);
			if (rc) {
				pr_err("mi,mdss-dsi-panel-fod-lhbm-white-1000nit-87reg-index not defined,but need\n");
			}
			rc = utils->read_u32(of_node,
				"mi,mdss-dsi-panel-fod-lhbm-white-110nit-87reg-index", &mi_cfg->fod_lhbm_white_110nit_87reg_index);
			if (rc) {
				pr_err("mi,mdss-dsi-panel-fod-lhbm-white-110nit-87reg-index not defined,but need\n");
			}
			rc = utils->read_u32(of_node,
				"mi,mdss-dsi-panel-fod-lhbm-green-500nit-87reg-index", &mi_cfg->fod_lhbm_green_500nit_87reg_index);
			if (rc) {
				pr_err("mi,mdss-dsi-panel-fod-lhbm-green-500nit-87reg-index not defined,but need\n");
			}
		}
		mi_cfg->fod_lhbm_b2reg_ctrl_flag = utils->read_bool(utils->data,
		"mi,mdss-dsi-panel-fod-lhbm-87reg-ctrl-flag");
		if (mi_cfg->fod_lhbm_87reg_ctrl_flag) {
			rc = utils->read_u32(of_node,
				"mi,mdss-dsi-panel-fod-lhbm-white-1000nit-b2reg-index", &mi_cfg->fod_lhbm_white_1000nit_b2reg_index);
			if (rc) {
				pr_err("mi,mdss-dsi-panel-fod-lhbm-white-1000nit-b2reg-index not defined,but need\n");
			}
			rc = utils->read_u32(of_node,
				"mi,mdss-dsi-panel-fod-lhbm-white-110nit-b2reg-index", &mi_cfg->fod_lhbm_white_110nit_b2reg_index);
			if (rc) {
				pr_err("mi,mdss-dsi-panel-fod-lhbm-white-110nit-b2reg-index not defined,but need\n");
			}
		}

		rc = utils->read_u32(utils->data, "mi,doze-hbm-dbv-level", &mi_cfg->doze_hbm_dbv_level);
		rc |= utils->read_u32(utils->data, "mi,doze-lbm-dbv-level", &mi_cfg->doze_lbm_dbv_level);
		if (rc) {
			mi_cfg->doze_hbm_dbv_level = 0;
			mi_cfg->doze_lbm_dbv_level = 0;
			pr_info("mi,doze-hbm-dbv-level/mi,doze-lbm-dbv-level not specified\n");
		} else {
			pr_info("mi,doze-hbm-dbv-level = %d mi,doze-lbm-dbv-level = %d\n", mi_cfg->doze_hbm_dbv_level, mi_cfg->doze_lbm_dbv_level);
		}

		mi_cfg->fod_lhbm_green_500nit_update_flag = utils->read_bool(utils->data, "mi,mdss-dsi-panel-fod-lhbm-green-500nit-update-flag");
		if (mi_cfg->fod_lhbm_green_500nit_update_flag) {
			rc = utils->read_u32(of_node,
				"mi,mdss-dsi-panel-fod-lhbm-green-500nit-update-d0-index", &mi_cfg->fod_lhbm_green_500nit_cfg.update_index);
		}

		mi_cfg->fod_lhbm_white_update_flag = utils->read_bool(utils->data, "mi,mdss-dsi-panel-fod-lhbm-white-update-flag");
		if (mi_cfg->fod_lhbm_white_update_flag) {
			rc = utils->read_u32(of_node,
			"mi,mdss-dsi-panel-fod-lhbm-white-1000nit-update-d0-index", &mi_cfg->fod_lhbm_white_cfg[FOD_LHBM_WHITE_1000NIT_GIRON].update_index);
			mi_cfg->fod_lhbm_white_cfg[FOD_LHBM_WHITE_1000NIT_GIROFF].update_index = mi_cfg->fod_lhbm_white_cfg[FOD_LHBM_WHITE_1000NIT_GIRON].update_index;
			rc = utils->read_u32(of_node,
			"mi,mdss-dsi-panel-fod-lhbm-white-110nit-update-d0-index", &mi_cfg->fod_lhbm_white_cfg[FOD_LHBM_WHITE_110NIT_GIRON].update_index);
			mi_cfg->fod_lhbm_white_cfg[FOD_LHBM_WHITE_110NIT_GIROFF].update_index = mi_cfg->fod_lhbm_white_cfg[FOD_LHBM_WHITE_110NIT_GIRON].update_index;
		}
	}

	mi_cfg->nolp_b2reg_ctrl_flag = utils->read_bool(utils->data,
	"mi,mdss-dsi-panel-nolp-b2reg-ctrl-flag");
	if (mi_cfg->nolp_b2reg_ctrl_flag) {
		rc = utils->read_u32(of_node,
			"mi,mdss-dsi-panel-nolp-b2reg-index", &mi_cfg->nolp_b2reg_index);
		if (rc) {
			pr_err("mi,mdss-dsi-panel-nolp-b2reg-index not defined,but need\n");
		}
	}
	mi_cfg->disp_rate_gpio = utils->get_named_gpio(utils->data,
		"mi,mdss-dsi-panel-disp-rate-gpio",0);
	if (gpio_is_valid(mi_cfg->disp_rate_gpio)) {
		rc = gpio_request(mi_cfg->disp_rate_gpio, "disp_rate");
		if (rc) {
			pr_err("request for disp_rate gpio failed, rc=%d\n", rc);
		}
		rc = gpio_direction_output(mi_cfg->disp_rate_gpio, 1);
		if (rc) {
			pr_err("unable to set dir for disp_rate gpio rc=%d\n", rc);
		}
	} else {
		pr_info("panel disp_rate gpio not specified\n");
	}

	if (panel->host_config.phy_type == DSI_PHY_TYPE_CPHY)
		INIT_DELAYED_WORK(&mi_cfg->cabc_delayed_work, panelon_cabc_delayed_work);

	rc = utils->read_u32(of_node,
			"mi,disp-fod-off-dimming-delay", &mi_cfg->fod_off_dimming_delay);
	if (rc) {
		mi_cfg->fod_off_dimming_delay = DEFAULT_FOD_OFF_DIMMING_DELAY;
		pr_info("default fod_off_dimming_delay %d\n", DEFAULT_FOD_OFF_DIMMING_DELAY);
	} else {
		pr_info("fod_off_dimming_delay %d\n", mi_cfg->fod_off_dimming_delay);
	}

	mi_cfg->gamma_update_flag = utils->read_bool(utils->data,
			"mi,mdss-dsi-panel-gamma-update-flag");
	if (mi_cfg->gamma_update_flag) {
		pr_info("mi,mdss-dsi-panel-gamma-update-flag feature is defined\n");

		rc = dsi_panel_parse_gamma_config(panel, of_node);
		if (rc)
			pr_info("failed to parse gamma config\n");
	} else {
		pr_info("mi,mdss-dsi-panel-gamma-update-flag feature not defined\n");
	}

	mi_cfg->greenish_gamma_update_flag = utils->read_bool(utils->data,
			"mi,mdss-dsi-greenish-update-gamma-flag");
	if (mi_cfg->greenish_gamma_update_flag) {
		pr_info("mi,mdss-dsi-greenish-update-gamma-flag feature is defined\n");

		rc = dsi_panel_parse_greenish_gamma_config(panel, of_node);
		if (rc)
			pr_info("failed to parse greenish gamma config\n");
	} else {
		pr_info("mi,mdss-dsi-greenish-update-gamma-flag feature not defined\n");
	}

	mi_cfg->dc_update_flag = utils->read_bool(utils->data,
			"mi,mdss-dsi-panel-dc-update-flag");
	if (mi_cfg->dc_update_flag) {
		pr_info("mi,mdss-dsi-panel-dc-update-flag feature is defined\n");

		rc = utils->read_u32(of_node,
				"mi,mdss-dsi-panel-dc-update-d2-index",
				&mi_cfg->dc_cfg.update_d2_index);
		if (rc)
			pr_info("failed to parse dc config\n");
	} else {
		pr_info("mi,mdss-dsi-panel-dc-update-flag feature not defined\n");
	}

	mi_cfg->dc_update_flag_v2 = utils->read_bool(utils->data,
			"mi,mdss-dsi-panel-dc-update-flag-v2");
	if (mi_cfg->dc_update_flag_v2) {
		pr_info("mi,mdss-dsi-panel-dc-update-flag-v2 feature is defined\n");

		rc = utils->read_u32(of_node,
				"mi,mdss-dsi-panel-dc-on-update-d2-index",
				&mi_cfg->dc_cfg_v2[DC_LUT_D2].update_dc_on_reg_index);
		rc |= utils->read_u32(of_node,
				"mi,mdss-dsi-panel-dc-off-update-d2-index",
				&mi_cfg->dc_cfg_v2[DC_LUT_D2].update_dc_off_reg_index);
		if (rc) {
			mi_cfg->dc_cfg_v2[DC_LUT_D2].update_dc_on_reg_index = -1;
			mi_cfg->dc_cfg_v2[DC_LUT_D2].update_dc_off_reg_index = -1;
			pr_info("failed to parse dc d2 config\n");
		}

		rc = utils->read_u32(of_node,
				"mi,mdss-dsi-panel-dc-on-update-d4-index",
				&mi_cfg->dc_cfg_v2[DC_LUT_D4].update_dc_on_reg_index);
		rc |= utils->read_u32(of_node,
				"mi,mdss-dsi-panel-dc-off-update-d4-index",
				&mi_cfg->dc_cfg_v2[DC_LUT_D4].update_dc_off_reg_index);
		if (rc) {
			mi_cfg->dc_cfg_v2[DC_LUT_D4].update_dc_on_reg_index = -1;
			mi_cfg->dc_cfg_v2[DC_LUT_D4].update_dc_off_reg_index = -1;
			pr_info("failed to parse dc d4 config\n");
		}
	} else {
		pr_info("mi,mdss-dsi-panel-dc-update-flag feature not defined\n");
	}

	mi_cfg->gir_update_flag = utils->read_bool(utils->data,
			"mi,mdss-dsi-panel-gir-update-flag");
	if (mi_cfg->gir_update_flag) {
		pr_info("mi,mdss-dsi-panel-gir-update-flag is defined\n");
		rc = utils->read_u32(utils->data, "mi,mdss-dsi-panel-gir-on-update-b9-index",
				&mi_cfg->gir_cfg.update_index);
		if (rc) {
			mi_cfg->gir_cfg.update_index = -1;
			pr_info("failed to get mi,mdss-dsi-panel-gir-on-update-b9-index\n");
		}
		rc = utils->read_u32(utils->data, "mi,mdss-dsi-panel-gir-on-update-b9-index-2",
				&mi_cfg->gir_cfg.update_index2);
		if (rc) {
		    mi_cfg->gir_cfg.update_index2 = -1;
		    pr_info("failed to get mi,mdss-dsi-panel-gir-on-update-b9-index-2\n");
		}
	} else {
		pr_info("mi,mdss-dsi-panel-gir-update-flag not defined\n");
	}

	mi_cfg->wp_read_enabled= utils->read_bool(utils->data,
				"mi,mdss-dsi-white-point-read-enabled");
	if (mi_cfg->wp_read_enabled) {
		rc = dsi_panel_parse_white_point_config(panel, of_node);
		if (rc)
			pr_info("failed to parse white point config\n");
	} else {
		pr_info("mi white point read not defined\n");
	}

	mi_cfg->elvss_dimming_check_enable = utils->read_bool(of_node,
			"mi,elvss_dimming_check_enable");
	if (mi_cfg->elvss_dimming_check_enable) {
		pr_info("mi,elvss_dimming_check_enable is defined\n");

		rc = dsi_panel_parse_elvss_dimming_config(panel, of_node);
		if (rc)
			pr_info("failed to parse elvss dimming config\n");
	} else {
		pr_info("mi,elvss_dimming_check_enable not defined\n");
	}

	rc = of_property_read_u32(of_node,
			"mi,mdss-dsi-panel-dc-threshold", &mi_cfg->dc_threshold);
	if (rc) {
		mi_cfg->dc_threshold = 440;
		pr_info("default dc backlight threshold is %d\n", mi_cfg->dc_threshold);
	} else {
		pr_info("dc backlight threshold %d \n", mi_cfg->dc_threshold);
	}

	rc = of_property_read_u32(of_node,
			"mi,mdss-dsi-panel-dc-type", &mi_cfg->dc_type);
	if (rc) {
		mi_cfg->dc_type = 1;
		pr_info("default dc backlight type is %d\n", mi_cfg->dc_type);
	} else {
		pr_info("dc backlight type %d \n", mi_cfg->dc_type);
	}
	rc = of_property_read_u32(of_node,
			"mi,mdss-dsi-panel-hbm-brightness", &mi_cfg->hbm_brightness);
	if (rc) {
		mi_cfg->hbm_brightness = 0;
		pr_info("default hbm brightness is %d\n", mi_cfg->hbm_brightness);
	} else {
		pr_info("hbm brightness %d \n", mi_cfg->hbm_brightness);
	}

	rc = of_property_read_u32(of_node,
			"mi,mdss-dsi-panel-max-brightness-clone", &mi_cfg->max_brightness_clone);
	if (rc) {
		mi_cfg->max_brightness_clone = 4095;
		pr_info("default max_brightness_clone is %d\n", mi_cfg->max_brightness_clone);
	} else {
		pr_info("max_brightness_clone is %d \n", mi_cfg->max_brightness_clone);
	}

	if (mi_cfg->dc_type == 0 && mi_cfg->hbm_51_ctrl_flag) {
		rc = of_property_read_u32(of_node,
			"mi,mdss-dsi-panel-fod-on-b2-index", &mi_cfg->fod_on_b2_index);
		if (rc) {
			mi_cfg->fod_on_b2_index = 0;
			pr_info("mi,mdss-dsi-panel-fod-on-b2-index not defined\n");
		}
	}

	mi_cfg->is_tddi_flag = utils->read_bool(of_node,
			"mi,is-tddi-flag");
	if (mi_cfg->is_tddi_flag)
		pr_info("panel is tddi\n");

	mi_cfg->hbm_enabled = false;
	mi_cfg->thermal_hbm_disabled = false;
	mi_cfg->fod_hbm_enabled = false;
	mi_cfg->fod_hbm_layer_enabled = false;
	mi_cfg->doze_brightness_state = DOZE_TO_NORMAL;
	mi_cfg->unset_doze_brightness = DOZE_TO_NORMAL;
	mi_cfg->dimming_state = STATE_NONE;
	mi_cfg->fod_backlight_flag = false;
	mi_cfg->fod_flag = false;
	mi_cfg->in_aod = false;
	mi_cfg->fod_hbm_off_time = ktime_get();
	mi_cfg->fod_backlight_off_time = ktime_get();
	mi_cfg->dc_enable = false;
	mi_cfg->bl_enable = true;
	mi_cfg->panel_dead_flag = false;

	mi_cfg->dither_enabled = false;
	mi_cfg->local_hbm_cur_status = false;
	mi_cfg->fod_lhbm_low_brightness_enabled = false;
	mi_cfg->fp_status = 0;
	mi_cfg->dim_fp_dbv_max_in_hbm_flag = false;

	return rc;
}

int dsi_panel_match_fps_pen_setting(struct dsi_panel *panel,
				struct dsi_display_mode *adj_mode)
{
	int rc =0;
	int retval = 0;
	struct dsi_display_mode_priv_info *priv_info;

	if (!panel || !panel->cur_mode || !panel->cur_mode->priv_info || !adj_mode) {
		pr_err("invalid params\n");
		return -EAGAIN;
	}

	priv_info = panel->cur_mode->priv_info;

	if (!priv_info->cmd_sets[DSI_CMD_SET_DISP_PEN_120HZ].count) {
		pr_debug("DSI_CMD_SET_DISP_PEN_120HZ not defined, return\n");
		return 0;
	}

	/* match fps(120/144/60/30Hz) pen seeting cmd */
	if (adj_mode->timing.refresh_rate == 144)
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_DISP_PEN_144HZ);
	else if (adj_mode->timing.refresh_rate == 120)
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_DISP_PEN_120HZ);
	else if (adj_mode->timing.refresh_rate == 60)
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_DISP_PEN_60HZ);
	else if (adj_mode->timing.refresh_rate == 30)
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_DISP_PEN_30HZ);

	if (rc) {
		pr_err("Failed to send DSI_CMD_SET_DISP_PEN_120HZ command\n");
		retval = -EAGAIN;
		goto error;
	}else
		pr_info("%s: refresh_rate[%d]\n", __func__, adj_mode->timing.refresh_rate);

error:
	return retval;
}

int mi_dsi_update_lhbm_cmd_b2reg(struct dsi_panel *panel, bool dc_status)
{
	struct dsi_display_mode_priv_info *priv_info;
	struct dsi_cmd_desc *cmds = NULL;
	struct dsi_panel_mi_cfg *mi_cfg  = NULL;
	u32 count;
	u8 *tx_buf;
	int retval = 0;

	if (!panel || !panel->cur_mode || !panel->cur_mode->priv_info) {
		pr_err("invalid params\n");
		return -EINVAL;
	}

	mi_cfg = &panel->mi_cfg;
	priv_info = panel->cur_mode->priv_info;

	if (mi_cfg->fod_lhbm_87reg_ctrl_flag == false) {
		pr_info("fod_lhbm_87reg_ctrl_flag = false not suport mi_dsi_update_lhbm_cmd_b2reg\n");
		return 0;
	}

	cmds = priv_info->cmd_sets[DSI_CMD_SET_MI_FOD_LHBM_WHITE_1000NIT].cmds;
	count = priv_info->cmd_sets[DSI_CMD_SET_MI_FOD_LHBM_WHITE_1000NIT].count;
	if (cmds && count >= mi_cfg->fod_lhbm_white_1000nit_b2reg_index) {
		tx_buf = (u8 *)cmds[mi_cfg->fod_lhbm_white_1000nit_b2reg_index].msg.tx_buf;
		if(dc_status == true)
			tx_buf[1] = 0x98;
		else
			tx_buf[1] = 0x18;
	} else {
		pr_err("please check 60hz mi,mdss-dsi-fod-lhbm-while-1000nit-command update parameter index configuration\n");
		retval = -EAGAIN;
		goto error;
	}

	cmds = priv_info->cmd_sets[DSI_CMD_SET_MI_FOD_LHBM_WHITE_110NIT].cmds;
	count = priv_info->cmd_sets[DSI_CMD_SET_MI_FOD_LHBM_WHITE_110NIT].count;
	if (cmds && count >= mi_cfg->fod_lhbm_white_110nit_b2reg_index) {
		tx_buf = (u8 *)cmds[mi_cfg->fod_lhbm_white_110nit_b2reg_index].msg.tx_buf;
		if(dc_status == true)
			tx_buf[1] = 0x98;
		else
			tx_buf[1] = 0x18;
	} else {
		pr_err("please check 60hz mi,mdss-dsi-fod-lhbm-while-110nit-command update parameter index configuration\n");
		retval = -EAGAIN;
		goto error;
	}

error:
	return retval;
}

int mi_dsi_update_nolp_b2reg(struct dsi_panel *panel, bool dc_status)
{
	struct dsi_display_mode_priv_info *priv_info;
	struct dsi_cmd_desc *cmds = NULL;
	struct dsi_panel_mi_cfg *mi_cfg  = NULL;
	u32 count;
	u8 *tx_buf;
	int retval = 0;

	if (!panel || !panel->cur_mode || !panel->cur_mode->priv_info) {
		pr_err("invalid params\n");
		return -EINVAL;
	}

	mi_cfg = &panel->mi_cfg;
	priv_info = panel->cur_mode->priv_info;

	if (mi_cfg->nolp_b2reg_ctrl_flag == false) {
		pr_info("nolp_b2reg_ctrl_flag = false not suport mi_dsi_update_nolp_b2reg\n");
		return 0;
	}

	cmds = priv_info->cmd_sets[DSI_CMD_SET_NOLP].cmds;
	count = priv_info->cmd_sets[DSI_CMD_SET_NOLP].count;
	if (cmds && count >= mi_cfg->nolp_b2reg_index) {
		tx_buf = (u8 *)cmds[mi_cfg->nolp_b2reg_index].msg.tx_buf;
		if(dc_status == true)
			tx_buf[1] = 0x98;
		else
			tx_buf[1] = 0x18;
	} else {
		pr_err("please check 60hz qcom,mdss-dsi-nolp-command update parameter index configuration\n");
		retval = -EAGAIN;
		goto error;
	}

error:
	return retval;
}

int mi_dsi_panel_update_lhbm_white_param(struct dsi_panel *panel, int fod_lhbm_white_state, int cmd_index)
{
	struct fod_lhbm_white_cfg * fod_lhbm_white_cfg;
	struct dsi_display *display = to_dsi_display(panel->host);
	struct dsi_display_mode *mode;
	struct dsi_cmd_desc *cmds;
	int total_modes;
	int retval = 0;
	int i;
	u32 count;
	u8 *tx_buf;
	size_t tx_len;
	u32 param_len;

	fod_lhbm_white_cfg = &panel->mi_cfg.fod_lhbm_white_cfg[fod_lhbm_white_state];

	total_modes = panel->num_display_modes;
	for (i = 0; i < total_modes; i++) {
		mode = &display->modes[i];
		cmds = mode->priv_info->cmd_sets[cmd_index].cmds;
		count = mode->priv_info->cmd_sets[cmd_index].count;
		if (cmds && count >= fod_lhbm_white_cfg->update_index) {
			tx_buf = (u8 *)cmds[fod_lhbm_white_cfg->update_index].msg.tx_buf;
			tx_len = cmds[fod_lhbm_white_cfg->update_index].msg.tx_len;
			param_len = min(sizeof(fod_lhbm_white_cfg->fod_lhbm_white_param), tx_len - 1);
			memcpy(&tx_buf[1], fod_lhbm_white_cfg->fod_lhbm_white_param, param_len);
			fod_lhbm_white_cfg->update_done = true;
		} else {
			pr_err("please check %dhz mi,mdss-dsi-fod-lhbm-white-command update parameter index configuration\n", mode->timing.refresh_rate);
			retval = -EAGAIN;
			goto error;
		}
	}

error:
	return retval;
}

int dsi_panel_write_cmd_set(struct dsi_panel *panel,
				struct dsi_panel_cmd_set *cmd_sets)
{
	int rc = 0, i = 0;
	ssize_t len;
	struct dsi_cmd_desc *cmds;
	u32 count;
	enum dsi_cmd_set_state state;
	struct dsi_display_mode *mode;
	const struct mipi_dsi_host_ops *ops = panel->host->ops;

	if (!panel || !panel->cur_mode)
		return -EINVAL;

	mode = panel->cur_mode;

	cmds = cmd_sets->cmds;
	count = cmd_sets->count;
	state = cmd_sets->state;

	if (count == 0) {
		pr_debug("[%s] No commands to be sent for state\n", panel->name);
		goto error;
	}

	for (i = 0; i < count; i++) {
		if (state == DSI_CMD_SET_STATE_LP)
			cmds->msg.flags |= MIPI_DSI_MSG_USE_LPM;

		if (cmds->last_command)
			cmds->msg.flags |= MIPI_DSI_MSG_LASTCOMMAND;

		len = ops->transfer(panel->host, &cmds->msg);
		if (len < 0) {
			rc = len;
			pr_err("failed to set cmds, rc=%d\n", rc);
			goto error;
		}
		if (cmds->post_wait_ms)
			usleep_range(cmds->post_wait_ms * 1000,
					((cmds->post_wait_ms * 1000) + 10));
		cmds++;
	}
error:
	return rc;
}

int dsi_panel_read_cmd_set(struct dsi_panel *panel,
				struct dsi_read_config *read_config)
{
	struct mipi_dsi_host *host;
	struct dsi_display *display;
	struct dsi_display_ctrl *ctrl;
	struct dsi_cmd_desc *cmds;
	int i, rc = 0, count = 0;
	u32 flags = 0;

	if (panel == NULL || read_config == NULL)
		return -EINVAL;

	host = panel->host;
	if (host) {
		display = to_dsi_display(host);
		if (display == NULL)
			return -EINVAL;
	} else
		return -EINVAL;

	if (!panel->panel_initialized) {
		pr_info("Panel not initialized\n");
		return -EINVAL;
	}

	if (!read_config->is_read) {
		pr_info("read operation was not permitted\n");
		return -EPERM;
	}

	dsi_display_clk_ctrl(display->dsi_clk_handle,
		DSI_ALL_CLKS, DSI_CLK_ON);

	ctrl = &display->ctrl[display->cmd_master_idx];

	rc = dsi_display_cmd_engine_enable(display);
	if (rc) {
		pr_err("cmd engine enable failed\n");
		rc = -EPERM;
		goto exit_ctrl;
	}

	if (display->tx_cmd_buf == NULL) {
		rc = dsi_host_alloc_cmd_tx_buffer(display);
		if (rc) {
			pr_err("failed to allocate cmd tx buffer memory\n");
			goto exit;
		}
	}

	count = read_config->read_cmd.count;
	cmds = read_config->read_cmd.cmds;
	if (count == 0) {
		pr_err("No commands to be sent\n");
		goto exit;
	}
	if (cmds->last_command) {
		cmds->msg.flags |= MIPI_DSI_MSG_LASTCOMMAND;
		flags |= DSI_CTRL_CMD_LAST_COMMAND;
	}
	flags |= (DSI_CTRL_CMD_FETCH_MEMORY | DSI_CTRL_CMD_READ |
		  DSI_CTRL_CMD_CUSTOM_DMA_SCHED);

	memset(read_config->rbuf, 0x0, sizeof(read_config->rbuf));
	cmds->msg.rx_buf = read_config->rbuf;
	cmds->msg.rx_len = read_config->cmds_rlen;

	rc = dsi_ctrl_cmd_transfer(ctrl->ctrl, &(cmds->msg), &flags);
	if (rc <= 0) {
		pr_err("rx cmd transfer failed rc=%d\n", rc);
		goto exit;
	}

	/* for debug log */
	for (i = 0; i < read_config->cmds_rlen; i++)
		pr_debug("[%d] = 0x%02X\n", i, read_config->rbuf[i]);

exit:
	dsi_display_cmd_engine_disable(display);
exit_ctrl:
	dsi_display_clk_ctrl(display->dsi_clk_handle,
		DSI_ALL_CLKS, DSI_CLK_OFF);

	return rc;
}

int dsi_panel_write_mipi_reg(struct dsi_panel *panel,
				char *buf, size_t count)
{
	struct dsi_panel_cmd_set cmd_sets = {0};
	int retval = 0, dlen = 0;
	u32 packet_count = 0;
	char *token, *input_copy, *input_dup = NULL;
	const char *delim = " ";
	char *buffer = NULL;
	u32 buf_size = 0;
	u32 tmp_data = 0;

	mutex_lock(&panel->panel_lock);

	if (!panel || !panel->panel_initialized) {
		pr_err("Panel not initialized!\n");
		retval = -EAGAIN;
		goto exit_unlock;
	}

	pr_debug("input buffer:{%s}\n", buf);

	input_copy = kstrdup(buf, GFP_KERNEL);
	if (!input_copy) {
		retval = -ENOMEM;
		goto exit_unlock;
	}

	input_dup = input_copy;
	/* removes leading and trailing whitespace from input_copy */
	input_copy = strim(input_copy);

	/* Split a string into token */
	token = strsep(&input_copy, delim);
	if (token) {
		retval = kstrtoint(token, 10, &tmp_data);
		if (retval) {
			pr_err("input buffer conversion failed\n");
			goto exit_free0;
		}
		g_dsi_read_cfg.is_read= !!tmp_data;
	}

	/* Removes leading whitespace from input_copy */
	if (input_copy)
		input_copy = skip_spaces(input_copy);
	else
		goto exit_free0;

	token = strsep(&input_copy, delim);
	if (token) {
		retval = kstrtoint(token, 10, &tmp_data);
		if (retval) {
			pr_err("input buffer conversion failed\n");
			goto exit_free0;
		}
		/* D106 (G2 format alignment): stock dsi_panel_mipi_reg_write
		 * (0x8f93ca8-0x8f93cbc) rejects len >= 0x41 with
		 * "read size exceeding the limit 64" - the community-tree
		 * sizeof(rbuf[256]) cap accepted 4x longer reads. */
		if (tmp_data > 64) {
			pr_err("read size exceeding the limit %d\n", 64);
			retval = -EINVAL;
			goto exit_free0;
		}
		g_dsi_read_cfg.cmds_rlen = tmp_data;
	}

	/* Removes leading whitespace from input_copy */
	if (input_copy)
		input_copy = skip_spaces(input_copy);
	else
		goto exit_free0;

	buffer = kzalloc(strlen(input_copy), GFP_KERNEL);
	if (!buffer) {
		retval = -ENOMEM;
		goto exit_free0;
	}

	token = strsep(&input_copy, delim);
	while (token) {
		retval = kstrtoint(token, 16, &tmp_data);
		if (retval) {
			pr_err("input buffer conversion failed\n");
			goto exit_free1;
		}
		pr_debug("buffer[%d] = 0x%02x\n", buf_size, tmp_data);
		buffer[buf_size++] = (tmp_data & 0xff);
		/* Removes leading whitespace from input_copy */
		if (input_copy) {
			input_copy = skip_spaces(input_copy);
			token = strsep(&input_copy, delim);
		} else {
			token = NULL;
		}
	}

	retval = dsi_panel_get_cmd_pkt_count(buffer, buf_size, &packet_count);
	if (!packet_count) {
		pr_err("get pkt count failed!\n");
		if (!retval)
			retval = -EINVAL;
		goto exit_free1;
	}

	retval = dsi_panel_alloc_cmd_packets(&cmd_sets, packet_count);
	if (retval) {
		pr_err("failed to allocate cmd packets, ret=%d\n", retval);
		goto exit_free1;
	}

	retval = dsi_panel_create_cmd_packets(buffer, dlen, packet_count,
						  cmd_sets.cmds);
	if (retval) {
		pr_err("failed to create cmd packets, ret=%d\n", retval);
		goto exit_free2;
	}

	if (g_dsi_read_cfg.is_read) {
		g_dsi_read_cfg.read_cmd = cmd_sets;
		retval = dsi_panel_read_cmd_set(panel, &g_dsi_read_cfg);
		if (retval <= 0) {
			pr_err("[%s]failed to read cmds, rc=%d\n", panel->name, retval);
			goto exit_free3;
		}
	} else {
		g_dsi_read_cfg.read_cmd = cmd_sets;
		retval = dsi_panel_write_cmd_set(panel, &cmd_sets);
		if (retval) {
			pr_err("[%s] failed to send cmds, rc=%d\n", panel->name, retval);
			goto exit_free3;
		}
	}

	pr_debug("[%s]: done!\n", panel->name);
	retval = count;

exit_free3:
	dsi_panel_destroy_cmd_packets(&cmd_sets);
exit_free2:
	dsi_panel_dealloc_cmd_packets(&cmd_sets);
exit_free1:
	kfree(buffer);
exit_free0:
	kfree(input_dup);
exit_unlock:
	mutex_unlock(&panel->panel_lock);
	if (!retval)
		retval = -EINVAL;
	return retval;
}

ssize_t dsi_panel_read_mipi_reg(struct dsi_panel *panel, char *buf)
{
	int i = 0;
	ssize_t count = 0;

	if (!panel) {
		pr_err("invalid params\n");
		return -EAGAIN;
	}

	mutex_lock(&panel->panel_lock);

	if (g_dsi_read_cfg.is_read) {
		for (i = 0; i < g_dsi_read_cfg.cmds_rlen; i++) {
			/* D107 (G2 read format): stock dsi_panel_mipi_reg_read
			 * (0x8f93fb0) prints lowercase hex with a SPACE
			 * separator (fmt "0x%02x " @0x9cb2f1e, last-elt
			 * "0x%02x\n" @0x9aae4a8) - the community layer used
			 * uppercase + comma. */
			if (i == g_dsi_read_cfg.cmds_rlen - 1) {
				count += snprintf(buf + count, PAGE_SIZE - count, "0x%02x\n",
				     g_dsi_read_cfg.rbuf[i]);
			} else {
				count += snprintf(buf + count, PAGE_SIZE - count, "0x%02x ",
				     g_dsi_read_cfg.rbuf[i]);
			}
		}
	}

	mutex_unlock(&panel->panel_lock);

	return count;
}

int dsi_panel_set_doze_brightness(struct dsi_panel *panel,
			int doze_brightness, bool need_panel_lock)
{
	int rc = 0;
	struct dsi_panel_mi_cfg *mi_cfg;
	struct dsi_display *display;
	int cmd_type = DSI_CMD_SET_MAX;

	if (!panel || !panel->host) {
		pr_err("invalid params\n");
		return -EINVAL;
	}

	display = to_dsi_display(panel->host);
	if (!display || !display->drm_dev){
		pr_err("invalid display or drm_dev ptr\n");
		return -EINVAL;
	}

	if (need_panel_lock)
		mutex_lock(&panel->panel_lock);

	mi_cfg = &panel->mi_cfg;

	if (!mi_cfg || (doze_brightness >= DOZE_BRIGHTNESS_MAX)) {
		pr_err("mi_cfg is null or doze_brightness %s\n", get_doze_brightness_name(doze_brightness));
		goto exit;
	}

	if (!panel->panel_initialized) {
		mi_cfg->unset_doze_brightness = doze_brightness;
		pr_info("Panel not initialized! save unset_doze_brightness = %s\n",
				get_doze_brightness_name(mi_cfg->unset_doze_brightness));
		goto exit;
	}

	if (mi_cfg->fod_hbm_enabled || mi_cfg->local_hbm_cur_status) {
		mi_cfg->unset_doze_brightness = doze_brightness;
		pr_info("fod_hbm_enabled/local_hbm_cur_status set, save unset_doze_brightness = %s\n",
			get_doze_brightness_name(mi_cfg->unset_doze_brightness));
		if (mi_cfg->unset_doze_brightness == DOZE_TO_NORMAL) {
			mi_cfg->doze_brightness_state = DOZE_TO_NORMAL;
			mi_cfg->dimming_state = STATE_DIM_BLOCK;
			if (display->drm_conn && display->drm_conn->kdev)
				sysfs_notify(&display->drm_conn->kdev->kobj, NULL, "doze_brightness");
		}

		goto exit;
	}

	if (mi_cfg->in_aod) {
		if (mi_cfg->doze_brightness_state != doze_brightness ||
			mi_cfg->unset_doze_brightness != DOZE_TO_NORMAL) {
			if (mi_cfg->into_aod_pending &&
				!mi_cfg->layer_aod_flag &&
				doze_brightness != DOZE_TO_NORMAL) {
				/* After unlocking the fingerprint, request to enter aod mode,
				 *but there is no aod layer, skip to set doze brightness */
				pr_info("aod layer is not ready, skip to set doze brightness\n");
				rc = -EAGAIN;
			} else {
				if (panel->mi_cfg.panel_id == 0x4C334100420200 || mi_cfg->fod_skip_nolp) {
					if (doze_brightness == DOZE_BRIGHTNESS_HBM) {
						cmd_type = DSI_CMD_SET_MI_DOZE_HBM;
						mi_cfg->aod_backlight = 170;
					} else if (doze_brightness == DOZE_BRIGHTNESS_LBM) {
						cmd_type = DSI_CMD_SET_MI_DOZE_LBM;
						mi_cfg->aod_backlight = 10;
					}
				} else {
					if (doze_brightness == DOZE_BRIGHTNESS_HBM ||
						mi_cfg->unset_doze_brightness == DOZE_BRIGHTNESS_HBM) {
						cmd_type = DSI_CMD_SET_MI_DOZE_HBM;
						mi_cfg->aod_backlight = 170;
					} else if (doze_brightness == DOZE_BRIGHTNESS_LBM ||
						mi_cfg->unset_doze_brightness == DOZE_BRIGHTNESS_LBM) {
						cmd_type = DSI_CMD_SET_MI_DOZE_LBM;
						mi_cfg->aod_backlight = 10;
					}
				}
			}
			if (cmd_type != DSI_CMD_SET_MAX) {
				if (sde_kms_is_suspend_blocked(display->drm_dev)) {
					pr_err("sde_kms is suspended, skip to set doze brightness\n");
					mi_cfg->unset_doze_brightness = doze_brightness;
					rc = -EBUSY;
					goto exit;
				} else {
					rc = dsi_panel_tx_cmd_set(panel, cmd_type);
					if (rc) {
						pr_err("[%s] failed to send DSI_CMD_SET_MI_DOZE_%s cmd, rc=%d\n",
							panel->name, cmd_type == DSI_CMD_SET_MI_DOZE_HBM ? "HBM" : "LBM", rc);
					}
				}
			}

			mi_cfg->dimming_state = STATE_DIM_BLOCK;
			mi_cfg->unset_doze_brightness = DOZE_TO_NORMAL;
			mi_cfg->doze_brightness_state = doze_brightness;
			if (display->drm_conn && display->drm_conn->kdev)
				sysfs_notify(&display->drm_conn->kdev->kobj, NULL, "doze_brightness");
			pr_info("set doze brightness to %s\n", get_doze_brightness_name(doze_brightness));
		} else {
			pr_info("%s has been set, skip\n", get_doze_brightness_name(doze_brightness));
		}
	} else {
		mi_cfg->unset_doze_brightness = doze_brightness;
		if (mi_cfg->unset_doze_brightness != DOZE_TO_NORMAL)
			pr_info("Not in Doze mode! save unset_doze_brightness = %s\n",
					get_doze_brightness_name(doze_brightness));
	}

exit:
	if (need_panel_lock)
		mutex_unlock(&panel->panel_lock);

	return rc;
}


int mi_mipi_dsi_dcs_set_pwm_value(u16 dbv_value)
{
	int rc = 0;

	if(!g_panel) {
		pr_err("invalid params\n");
		return -EINVAL;
	}

	rc = mipi_dsi_dcs_set_display_brightness_big_endian(&g_panel->mipi_device, dbv_value);
	if (rc < 0)
		pr_err("failed to update pwm value:%d\n", dbv_value);

	return 0;
}
EXPORT_SYMBOL(mi_mipi_dsi_dcs_set_pwm_value);




static int mi_dsi_update_lhbm_cmd_87reg(struct dsi_panel *panel,
			enum dsi_cmd_set_type type, int bl_lvl)
{
	struct dsi_display_mode_priv_info *priv_info;
	struct dsi_cmd_desc *cmds = NULL;
	struct dsi_panel_mi_cfg *mi_cfg  = NULL;
	u32 count;
	u32 index;
	u8 *tx_buf;
	int rc = 0;

	if (!panel || !panel->cur_mode || !panel->cur_mode->priv_info) {
		pr_err("invalid params\n");
		return -EINVAL;
	}

	mi_cfg = &panel->mi_cfg;
	priv_info = panel->cur_mode->priv_info;

	switch (type) {
		case DSI_CMD_SET_MI_FOD_LHBM_WHITE_1000NIT:
			index = mi_cfg->fod_lhbm_white_1000nit_87reg_index;
			break;
		case DSI_CMD_SET_MI_FOD_LHBM_WHITE_110NIT:
			index = mi_cfg->fod_lhbm_white_110nit_87reg_index;
			break;
		case DSI_CMD_SET_MI_FOD_LHBM_GREEN_500NIT:
			index = mi_cfg->fod_lhbm_green_500nit_87reg_index;
			break;
		default:
			pr_err("%s panel wrong cmd type!\n", panel->type);
			return -EINVAL;
	}

	pr_info("tpye=%d; bl_lvl=%d; index=%d\n", type, bl_lvl, index);

	cmds = priv_info->cmd_sets[type].cmds;
	count = priv_info->cmd_sets[type].count;
	if (cmds && count >= index) {
		tx_buf = (u8 *)cmds[index].msg.tx_buf;
		if (tx_buf && tx_buf[0] == 0x87) {
			tx_buf[1] = (fpr_alpha_set[bl_lvl] >> 8) & 0x0f;
			tx_buf[2] = fpr_alpha_set[bl_lvl] & 0xff;
			pr_info("panel fpr cmd[0x%02x] = 0x%02x 0x%02x\n", tx_buf[0], tx_buf[1], tx_buf[2]);
		} else {
			if (tx_buf) {
				pr_err("%s panel fpr index = %d, tx_buf[0] = 0x%02X, check cmd[%d] 0x51 index\n",
					panel->type, index, tx_buf[0], type);
			} else {
				pr_err("%s panel tx_buf is NULL pointer\n", panel->type);
			}
			rc = -EINVAL;
		}
	} else {
		pr_err("%s panel fpr cmd[%d] 0x87 index(%d) error\n",
			panel->type, type, index);
		rc = -EINVAL;
	}

	return rc;
}

bool dsi_panel_is_need_tx_cmd(u32 param)
{
	if ((param & 0x0F000000) != DISPPARAM_FOD_BACKLIGHT_ON
		|| (param & 0x0F000000) != DISPPARAM_FOD_BACKLIGHT_OFF
		|| param != DISPPARAM_FOD_UNLOCK_SUCCESS
		|| param != DISPPARAM_SET_THERMAL_HBM_DISABLE
		|| param != DISPPARAM_CLEAR_THERMAL_HBM_DISABLE
		|| (param & 0x0000F000) != DISPPARAM_LOW_BRIGHTNESS_FOD
		|| (param & 0x0000F000) != DISPPARAM_FP_STATUS) {
		return false;
	}else
		return true;
}

int dsi_panel_set_disp_param(struct dsi_panel *panel, u32 param)
{
	int rc = 0;
	uint32_t temp = 0;
	u32 fod_backlight = 0;
	struct dsi_panel_mi_cfg *mi_cfg  = NULL;
	struct dsi_cmd_desc *cmds = NULL;
	struct dsi_display_mode_priv_info *priv_info;
	static u8 backlight_delta = 0;
	u32 resend_backlight;
	u32 count;
	u8 *tx_buf;
	bool is_thermal_call = false;
	u32 fod_lhbm_level = 0;
	bool fod_lhbm_low_brightness_enabled = false;
	bool fod_lhbm_low_brightness_allow = true;
	u32 fp_status = 0;

	if (!panel) {
		pr_err("invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);

	mi_cfg = &panel->mi_cfg;
	if (!mi_cfg->mi_feature_enabled) {
		pr_err("mi feature not enable, exit!\n");
		goto exit;
	}

	if (!panel->panel_initialized
		&& (param & 0x0F000000) != DISPPARAM_FOD_BACKLIGHT_ON
		&& (param & 0x0F000000) != DISPPARAM_FOD_BACKLIGHT_OFF
		&& param != DISPPARAM_FOD_UNLOCK_SUCCESS
		&& param != DISPPARAM_SET_THERMAL_HBM_DISABLE
		&& param != DISPPARAM_CLEAR_THERMAL_HBM_DISABLE
		&& (param & 0x0000F000) != DISPPARAM_LOW_BRIGHTNESS_FOD
		&& (param & 0x0000F000) != DISPPARAM_FP_STATUS) {
		pr_err("Panel not initialized!\n");
		goto exit;
	}

	if ((panel->host_config.phy_type == DSI_PHY_TYPE_CPHY)
		&& (param & 0x700) && param != 0xF00) {
			pr_info("save cabc status!\n");
			panel->mi_cfg.cabc_temp_status = param & 0x700;
			cancel_delayed_work(&mi_cfg->cabc_delayed_work);
			pr_info("delayed_work schedule --- delay cabc mode\n");
			__pm_wakeup_event(mi_cfg->aod_wakelock, DEFAULT_CABC_WRITE_DELAY + 100);
			schedule_delayed_work(&mi_cfg->cabc_delayed_work, msecs_to_jiffies(DEFAULT_CABC_WRITE_DELAY));

			param = param & 0xFFFFF0FF;
			if(!param)
				goto exit;
	}

	/* cur_mode ptr assignment in dsi_display_set_mode func after open drm node */
	if (panel->cur_mode)
		priv_info = panel->cur_mode->priv_info;
	else
		priv_info = NULL;

	if ((param & 0x000F0000) == DISPPARAM_HBM_ON) {
		is_thermal_call = (param & 0x1);
		param = (param & 0xFFFFFFFE);
	}
	if ((param & 0x0000F000) == 0xD000) {
		fod_lhbm_low_brightness_enabled = (param & 0x1);
		param = (param & 0xFFFFFFFE);
	} else if ((param & 0x0000F000) == 0xE000) {
		fp_status = (param & 0x7);
		fod_lhbm_low_brightness_allow = (param & 0x8);
		param = (param & 0xFFFFFFF0);
       }
	if ((param & 0x00F00000) == 0xD00000) {
		fod_backlight = (param & 0x01FFF);
		param = (param & 0x0FF00000);
	}
	if (mi_cfg->local_hbm_enabled) {
		if ((param & 0x000F0000) == 0x20000) {
			fod_lhbm_level = (param & 0xF);
			param = (param & 0xFFFFFFF0);
		}
	}

	temp = param & 0x000000F0;
	switch (temp) {
	case DISPPARAM_CE_ON:
		pr_info("ceon\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_CEON);
		break;
	case DISPPARAM_CE_OFF:
		pr_info("ceoff\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_CEOFF);
		break;
	default:
		break;
	}

	temp = param & 0x00000F00;
	switch (temp) {
	case DISPPARAM_CABCUI_ON:
		pr_info("cabcuion\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_CABCUION);
		break;
	case DISPPARAM_CABCSTILL_ON:
		pr_info("cabcstillon\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_CABCSTILLON);
		break;
	case DISPPARAM_CABCMOVIE_ON:
		pr_info("cabcmovieon\n");
		dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_CABCMOVIEON);
		break;
	case DISPPARAM_CABC_OFF:
		pr_info("cabcoff\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_CABCOFF);
		break;
	case DISPPARAM_SKIN_CE_CABCUI_ON:
		pr_info("skince cabcuion\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_SKINCE_CABCUION);
		break;
	case DISPPARAM_SKIN_CE_CABCSTILL_ON:
		pr_info("skince cabcstillon\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_SKINCE_CABCSTILLON);
		break;
	case DISPPARAM_SKIN_CE_CABCMOVIE_ON:
		pr_info("skince cabcmovieon\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_SKINCE_CABCMOVIEON);
		break;
	case DISPPARAM_SKIN_CE_CABC_OFF:
		pr_info("skince cabcoff\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_SKINCE_CABCOFF);
		break;
	case DISPPARAM_DIMMING_OFF:
		if (mi_cfg->dimming_state != STATE_DIM_BLOCK) {
			pr_info("dimming off\n");
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DIMMINGOFF);
		} else {
			pr_info("skip dimming off due to hbm on\n");
		}
		break;
	case DISPPARAM_DIMMING:
		if (mi_cfg->dimming_state != STATE_DIM_BLOCK) {
			if (ktime_after(ktime_get(), mi_cfg->fod_hbm_off_time)
				&& ktime_after(ktime_get(), mi_cfg->fod_backlight_off_time)) {
				pr_info("dimming on\n");
				dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DIMMINGON);
			} else {
				pr_info("skip dimming on due to hbm off\n");
			}
		} else {
			pr_info("skip dimming on due to hbm on\n");
		}
		break;
	default:
		break;
	}

	temp = param & 0x0000F000;
	switch (temp) {
	case DISPPARAM_ACL_L1:
		pr_info("acl level 1\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_ACL_L1);
		break;
	case DISPPARAM_ACL_L2:
		pr_info("acl level 2\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_ACL_L2);
		break;
	case DISPPARAM_ACL_L3:
		pr_info("acl level 3\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_ACL_L3);
		break;
	case DISPPARAM_ACL_OFF:
		pr_info("acl off\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_ACL_OFF);
		break;
	case DISPPARAM_LOW_BRIGHTNESS_FOD:
		pr_info("DISPPARAM_LOW_BRIGHTNESS_FOD=%d\n", fod_lhbm_low_brightness_enabled);
		mi_cfg->fod_lhbm_low_brightness_enabled = fod_lhbm_low_brightness_enabled;
		break;
	case DISPPARAM_FP_STATUS:
		pr_info("DISPPARAM_FP_STATUS=%d\n", fp_status);
		pr_info("fod_lhbm_low_brightness_allow=%d\n", fod_lhbm_low_brightness_allow);
		mi_cfg->fp_status = fp_status;
		mi_cfg->fod_lhbm_low_brightness_allow = fod_lhbm_low_brightness_allow;
		if  (fp_status == ENROLL_STOP ||
				fp_status == AUTH_STOP ||
				fp_status == HEART_RATE_STOP) {
			mi_disp_set_fod_queue_work(0, false);
		}
		break;
	case DISPPARAM_ROUND_ON:
		pr_info("DISPPARAM_ROUND_ON\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_ROUND_ON);
		break;
	case DISPPARAM_ROUND_OFF:
		pr_info("DISPPARAM_ROUND_OFF\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_ROUND_OFF);
		break;
	default:
		break;
	}

	temp = param & 0x000F0000;
	switch (temp) {
	case DISPPARAM_LCD_HBM_L1_ON:
		pr_info("lcd hbm l1 on\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_DISP_LCD_HBM_L1_ON);
		break;
	case DISPPARAM_LCD_HBM_L2_ON:
		pr_info("lcd hbm  l2 on\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_DISP_LCD_HBM_L2_ON);
		break;
	case DISPPARAM_LCD_HBM_L3_ON:
		pr_info("lcd hbm  l3 on\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_DISP_LCD_HBM_L3_ON);
		break;
	case DISPPARAM_LCD_HBM_OFF:
		pr_info("lcd hbm off\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_DISP_LCD_HBM_OFF);
		break;
	case DISPPARAM_HBM_ON:
		pr_info("hbm on needed, thermal_hbm_disabled = %d, fod_hbm_enabled = %d\n", mi_cfg->thermal_hbm_disabled, mi_cfg->fod_hbm_enabled);
		mi_cfg->hbm_enabled = true;
		if (!mi_cfg->fod_hbm_enabled && !mi_cfg->thermal_hbm_disabled) {
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_HBM_ON);
			pr_info("hbm on\n");
			if (is_thermal_call) {
				pr_info("thermal clear hbm limit, restore previous hbm on\n");
			}
			mi_cfg->dimming_state = STATE_DIM_BLOCK;

		}
		break;
	case DISPPARAM_HBM_OFF:
		if (param & DISPPARAM_THERMAL_SET)
			is_thermal_call = true;
		pr_info("hbm off needed, thermal_hbm_disabled = %d, fod_hbm_enabled = %d\n", mi_cfg->thermal_hbm_disabled, mi_cfg->fod_hbm_enabled);
		if (!mi_cfg->fod_hbm_enabled) {
			if (mi_cfg->hbm_51_ctrl_flag && priv_info) {
				/* restore last backlight value when hbm off */
				cmds = priv_info->cmd_sets[DSI_CMD_SET_MI_HBM_OFF].cmds;
				count = priv_info->cmd_sets[DSI_CMD_SET_MI_HBM_OFF].count;
				if (cmds && count >= mi_cfg->hbm_off_51_index) {
					tx_buf = (u8 *)cmds[mi_cfg->hbm_off_51_index].msg.tx_buf;
					if (tx_buf && tx_buf[0] == 0x51) {
						tx_buf[1] = (mi_cfg->last_bl_level >> 8) & 0x07;
						tx_buf[2] = mi_cfg->last_bl_level & 0xff;
					} else {
						if (tx_buf)
							pr_err("tx_buf[0] = 0x%02X, check 0x51 index\n", tx_buf[0]);
						else
							pr_err("tx_buf is NULL pointer\n");
					}
				} else {
					pr_err("0x51 index(%d) error\n", mi_cfg->hbm_off_51_index);
				}
			}
			if (mi_cfg->vi_setting_enabled) {
				/* if last backlight >= vi_switch_threshold, set VI voltage -3.5V */
				if (mi_cfg->last_bl_level >= mi_cfg->vi_switch_threshold) {
					rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_VI_SETTING_HIGH);
				} else {
					rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_VI_SETTING_LOW);
				}
			}
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_HBM_OFF);
			pr_info("hbm off\n");
			mi_cfg->dimming_state = STATE_DIM_RESTORE;
			if (is_thermal_call) {
				pr_info("thermal set hbm limit, hbm off\n");
			} else
				mi_cfg->hbm_enabled = false;

		}
		break;
	case DISPPARAM_HBM_HDR_ON:
		pr_info("hbm hdr on\n");
		if (!mi_cfg->fod_hbm_enabled && !mi_cfg->thermal_hbm_disabled)
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_HBM_HDR_ON);
		mi_cfg->hbm_enabled = true;
		mi_cfg->dimming_state = STATE_DIM_BLOCK;
		break;
	case DISPPARAM_HBM_HDR_OFF:
		pr_info("hbm hdr off\n");
		if (mi_cfg->hbm_51_ctrl_flag && priv_info) {
			/* restore last backlight value when hbm off */
			cmds = priv_info->cmd_sets[DSI_CMD_SET_MI_HBM_HDR_OFF].cmds;
			count = priv_info->cmd_sets[DSI_CMD_SET_MI_HBM_HDR_OFF].count;
			if (cmds && count >= mi_cfg->hbm_off_51_index) {
				tx_buf = (u8 *)cmds[mi_cfg->hbm_off_51_index].msg.tx_buf;
				if (tx_buf && tx_buf[0] == 0x51) {
					tx_buf[1] = (mi_cfg->last_bl_level >> 8) & 0x07;
					tx_buf[2] = mi_cfg->last_bl_level & 0xff;
					pr_info("DSI_CMD_SET_MI_HBM_HDR_OFF 0x%02X = 0x%02X 0x%02X\n",
							tx_buf[0], tx_buf[1], tx_buf[2]);
				} else {
					if (tx_buf)
						pr_err("tx_buf[0] = 0x%02X, check 0x51 index\n", tx_buf[0]);
					else
						pr_err("tx_buf is NULL pointer\n");
				}
			} else {
				pr_err("0x51 index(%d) error\n", mi_cfg->hbm_off_51_index);
			}
		}
		if (mi_cfg->vi_setting_enabled) {
			/* if last backlight >= vi_switch_threshold, set VI voltage -3.5V */
			if (mi_cfg->last_bl_level >= mi_cfg->vi_switch_threshold) {
				rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_VI_SETTING_HIGH);
			} else {
				rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_VI_SETTING_LOW);
			}
		}
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_HBM_HDR_OFF);
		mi_cfg->dimming_state = STATE_DIM_RESTORE;
		mi_cfg->hbm_enabled = false;
		break;
	case DISPPARAM_HBM_FOD_ON:
		if (mi_cfg->local_hbm_enabled) {
			cancel_delayed_work(&mi_cfg->enter_aod_delayed_work);
			if (fod_lhbm_level == 1) {
				pr_info("lhbm white 1000nit On\n");
				mi_dsi_update_lhbm_cmd_87reg(panel, DSI_CMD_SET_MI_FOD_LHBM_WHITE_1000NIT, mi_cfg->last_bl_level);
				if (panel->power_mode == SDE_MODE_DPMS_LP1 ||panel->power_mode == SDE_MODE_DPMS_LP2){
					switch (mi_cfg->doze_brightness_state) {
						case DOZE_BRIGHTNESS_HBM:
							mi_dsi_update_lhbm_cmd_87reg(panel, DSI_CMD_SET_MI_FOD_LHBM_WHITE_1000NIT, mi_cfg->doze_hbm_dbv_level);
							pr_info("DSI_CMD_SET_MI_FOD_LHBM_WHITE_1000NIT in doze_hbm_dbv_level\n");
							break;
						case DOZE_BRIGHTNESS_LBM:
							mi_dsi_update_lhbm_cmd_87reg(panel, DSI_CMD_SET_MI_FOD_LHBM_WHITE_1000NIT, mi_cfg->doze_lbm_dbv_level);
							pr_info("DSI_CMD_SET_MI_FOD_LHBM_WHITE_1000NIT in doze_lbm_dbv_level\n");
							break;
						default:
							pr_info("DSI_CMD_SET_MI_FOD_LHBM_WHITE_1000NIT defaults\n");
							break;
					}
				}
				if(mi_cfg->gir_enabled){
					rc = mi_dsi_panel_update_lhbm_white_param(panel,FOD_LHBM_WHITE_1000NIT_GIRON, DSI_CMD_SET_MI_FOD_LHBM_WHITE_1000NIT);
				} else {
					rc = mi_dsi_panel_update_lhbm_white_param(panel,FOD_LHBM_WHITE_1000NIT_GIROFF, DSI_CMD_SET_MI_FOD_LHBM_WHITE_1000NIT);
				}
				rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_FOD_LHBM_WHITE_1000NIT);
			} else if (fod_lhbm_level == 2) {
				pr_info("lhbm white 110nit On\n");
				mi_dsi_update_lhbm_cmd_87reg(panel, DSI_CMD_SET_MI_FOD_LHBM_WHITE_110NIT, mi_cfg->last_bl_level);
				if (panel->power_mode == SDE_MODE_DPMS_LP1 ||panel->power_mode == SDE_MODE_DPMS_LP2){
					switch (mi_cfg->doze_brightness_state) {
						case DOZE_BRIGHTNESS_HBM:
							mi_dsi_update_lhbm_cmd_87reg(panel, DSI_CMD_SET_MI_FOD_LHBM_WHITE_1000NIT, mi_cfg->doze_hbm_dbv_level);
							pr_info("DSI_CMD_SET_MI_FOD_LHBM_WHITE_1000NIT in doze_hbm_dbv_level\n");
							break;
						case DOZE_BRIGHTNESS_LBM:
							mi_dsi_update_lhbm_cmd_87reg(panel, DSI_CMD_SET_MI_FOD_LHBM_WHITE_1000NIT, mi_cfg->doze_lbm_dbv_level);
							pr_info("DSI_CMD_SET_MI_FOD_LHBM_WHITE_1000NIT in doze_lbm_dbv_level\n");
							break;
						default:
							pr_info("DSI_CMD_SET_MI_FOD_LHBM_WHITE_1000NIT defaults\n");
							break;
					}
				}

				if(mi_cfg->gir_enabled){
					rc = mi_dsi_panel_update_lhbm_white_param(panel,FOD_LHBM_WHITE_110NIT_GIRON, DSI_CMD_SET_MI_FOD_LHBM_WHITE_110NIT);
				} else {
					rc = mi_dsi_panel_update_lhbm_white_param(panel,FOD_LHBM_WHITE_110NIT_GIROFF, DSI_CMD_SET_MI_FOD_LHBM_WHITE_110NIT);
				}
				rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_FOD_LHBM_WHITE_110NIT);
			} else if (fod_lhbm_level == 3) {
				pr_info("lhbm green 500nit On\n");
				mi_dsi_update_lhbm_cmd_87reg(panel, DSI_CMD_SET_MI_FOD_LHBM_GREEN_500NIT, mi_cfg->last_bl_level);
				rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_FOD_LHBM_GREEN_500NIT);
			}
			mi_cfg->local_hbm_cur_status = true;
		} else {
			pr_info("hbm fod on\n");
			cancel_delayed_work(&mi_cfg->enter_aod_delayed_work);
			if (mi_cfg->fod_on_b2_index && priv_info &&
				((mi_cfg->panel_id >> 8) == 0x4A32004202 ||
				(mi_cfg->panel_id >> 8) == 0x4A3153004202)) {
				pr_info("Fod on b2 index is enabled\n");
				cmds = priv_info->cmd_sets[DSI_CMD_SET_MI_HBM_FOD_ON].cmds;
				count = priv_info->cmd_sets[DSI_CMD_SET_MI_HBM_FOD_ON].count;
				if (cmds && count >= mi_cfg->fod_on_b2_index + 2) {
					tx_buf = (u8 *)cmds[mi_cfg->fod_on_b2_index].msg.tx_buf;
					/* 0xB2(0) reg: if DC on (0xC8); if DC off (0x58) */
					if (tx_buf && tx_buf[0] == 0xB2) {
						if (mi_cfg->dc_enable) {
							tx_buf[1] = 0xC8;
						} else {
							tx_buf[1] = 0x58;
						}
						pr_info("DSI_CMD_SET_MI_HBM_FOD_ON 0x%02X(9) = 0x%02X\n", tx_buf[0], tx_buf[1]);
					} else {
						if (tx_buf)
							pr_err("tx_buf[0] = 0x%02X, check 0xB2 index\n", tx_buf[0]);
						else
							pr_err("tx_buf is NULL pointer\n");
					}
					tx_buf = (u8 *)cmds[mi_cfg->fod_on_b2_index + 2].msg.tx_buf;
					/* 0xB2(9) reg: if DC on use 1 Pulse(0x00); if DC off use 4 Pulse(0x20) */
					if (tx_buf && tx_buf[0] == 0xB2) {
						if (mi_cfg->dc_enable) {
							tx_buf[1] = 0x00;
						} else {
							tx_buf[1] = 0x20;
						}
						pr_info("DSI_CMD_SET_MI_HBM_FOD_ON 0x%02X(9) = 0x%02X\n", tx_buf[0], tx_buf[1]);
					} else {
						if (tx_buf)
							pr_err("tx_buf[0] = 0x%02X, check 0xB2 index\n", tx_buf[0]);
						else
							pr_err("tx_buf is NULL pointer\n");
					}
				} else {
					pr_err("0xB2 index(%d) error\n", mi_cfg->fod_on_b2_index);
				}
			}

			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_HBM_FOD_ON);

			if (mi_cfg->dc_type == 1)
				rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_CRC_OFF);
			mi_cfg->fod_hbm_enabled = true;
			mi_cfg->fod_to_nolp = false;
			mi_cfg->dimming_state = STATE_DIM_BLOCK;
		}
		break;
	case DISPPARAM_HBM_FOD2NORM:
		pr_info("hbm fod to normal mode\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_HBM_FOD2NORM);
		break;
	case DISPPARAM_HBM_FOD_OFF:
		if (mi_cfg->local_hbm_enabled) {
			pr_info("lhbm Off\n");
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_FOD_LHBM_OFF);
			mi_cfg->local_hbm_cur_status = false;

			if (panel->power_mode == SDE_MODE_DPMS_LP1 ||
					panel->power_mode == SDE_MODE_DPMS_LP2) {
				if (mi_cfg->fp_status == AUTH_START) {
					pr_info("delayed_work schedule --- delay enter aod mode\n");
					__pm_wakeup_event(mi_cfg->aod_wakelock, DEFAULT_FOD_OFF_ENTER_AOD_DELAY + 100);
					schedule_delayed_work(&mi_cfg->enter_aod_delayed_work,
						msecs_to_jiffies(DEFAULT_FOD_OFF_ENTER_AOD_DELAY));
				}
			}
		} else {
			pr_info("hbm fod off\n");
			if (!mi_cfg->hbm_enabled) {
				if (mi_cfg->hbm_51_ctrl_flag && priv_info) {
					/* restore last backlight value when fod off */
					cmds = priv_info->cmd_sets[DSI_CMD_SET_MI_HBM_FOD_OFF].cmds;
					count = priv_info->cmd_sets[DSI_CMD_SET_MI_HBM_FOD_OFF].count;
					if (cmds && count >= mi_cfg->fod_off_51_index) {
						tx_buf = (u8 *)cmds[mi_cfg->fod_off_51_index].msg.tx_buf;
						if (tx_buf && tx_buf[0] == 0x51) {
							if (mi_cfg->layer_fod_unlock_success && !mi_cfg->last_bl_level) {
								pr_err("fod hbm off, restore last bl: %d\n", mi_cfg->last_nonzero_bl_level);
								tx_buf[1] = (mi_cfg->last_nonzero_bl_level >> 8) & 0x07;
								tx_buf[2] = mi_cfg->last_nonzero_bl_level & 0xff;
							} else {
								tx_buf[1] = (mi_cfg->last_bl_level >> 8) & 0x07;
								tx_buf[2] = mi_cfg->last_bl_level & 0xff;
							}
							pr_info("DSI_CMD_SET_MI_HBM_FOD_OFF 0x%02X = 0x%02X 0x%02X\n",
									tx_buf[0], tx_buf[1], tx_buf[2]);
						} else {
							if (tx_buf)
								pr_err("tx_buf[0] = 0x%02X, check 0x51 index\n", tx_buf[0]);
							else
								pr_err("tx_buf is NULL pointer\n");
						}
					} else {
						pr_err("0x51 index(%d) error\n", mi_cfg->hbm_off_51_index);
					}
				}
				if (mi_cfg->dc_type == 0 && mi_cfg->fod_off_b5_index && priv_info) {
					cmds = priv_info->cmd_sets[DSI_CMD_SET_MI_HBM_FOD_OFF].cmds;
					count = priv_info->cmd_sets[DSI_CMD_SET_MI_HBM_FOD_OFF].count;
					if (cmds && count >= mi_cfg->fod_off_b5_index) {
						tx_buf = (u8 *)cmds[mi_cfg->fod_off_b5_index].msg.tx_buf;
						if (tx_buf && tx_buf[0] == 0xb5) {
							if (panel->power_mode == SDE_MODE_DPMS_LP1 ||
								panel->power_mode == SDE_MODE_DPMS_LP2) {
								pr_info("Fod off to aod set elvss swire to -3.0V\n");
								tx_buf[1] = 0x19;
							} else if (panel->power_mode == SDE_MODE_DPMS_ON) {
								pr_info("Fod off to normal set elvss swire to -4.5V\n");
								tx_buf[1] = 0x0A;
							}
						}
					}
				}
				if (mi_cfg->vi_setting_enabled) {
					/* if last backlight >= vi_switch_threshold, set VI voltage -3.5V */
					if (mi_cfg->last_bl_level >= mi_cfg->vi_switch_threshold) {
						rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_VI_SETTING_HIGH);
					} else {
						rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_VI_SETTING_LOW);
					}
				}
				rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_HBM_FOD_OFF);
				mi_cfg->dimming_state = STATE_DIM_RESTORE;
			} else {
				if (mi_cfg->thermal_hbm_disabled) {
					pr_info("thermal set hbm limit, do not recovery hbm on\n");
				} else {
					pr_info("recovery hbm on mode\n");
					rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_HBM_ON);
					if (mi_cfg->hbm_brightness)
						rc = dsi_panel_update_backlight(panel, mi_cfg->last_bl_level);
				}
			}
			if (mi_cfg->dc_type == 0 && mi_cfg->dc_enable) {
				rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_AOD_TO_DC_ON);
				if (rc)
					pr_err("[%s] failed to send DSI_CMD_SET_MI_DC_ON cmd, rc=%d\n",
						panel->name, rc);
			}
			mi_cfg->fod_hbm_enabled = false;
			mi_cfg->fod_hbm_off_time = ktime_add_ms(ktime_get(),
					mi_cfg->fod_off_dimming_delay);

			if (panel->power_mode == SDE_MODE_DPMS_LP1 ||
					panel->power_mode == SDE_MODE_DPMS_LP2) {
				if (mi_cfg->layer_fod_unlock_success || mi_cfg->sysfs_fod_unlock_success) {
					pr_info("[%d,%d]Fod fingerprint unlock success, skip into aod mode\n",
						mi_cfg->layer_fod_unlock_success, mi_cfg->sysfs_fod_unlock_success);
				} else {
					pr_info("delayed_work schedule --- delay enter aod mode\n");
					mi_cfg->into_aod_pending = true;
					__pm_wakeup_event(mi_cfg->aod_wakelock, DEFAULT_FOD_OFF_ENTER_AOD_DELAY + 100);
					schedule_delayed_work(&mi_cfg->enter_aod_delayed_work,
						msecs_to_jiffies(DEFAULT_FOD_OFF_ENTER_AOD_DELAY));
				}
			}
		}
		break;
	case DISPPARAM_FOD_UNLOCK_SUCCESS:
		pr_info("Fod fingerprint unlock success\n");
		mi_cfg->sysfs_fod_unlock_success = true;
		mi_cfg->into_aod_pending = false;
		if (panel->power_mode == SDE_MODE_DPMS_LP1 ||
			panel->power_mode == SDE_MODE_DPMS_LP2) {
			cancel_delayed_work(&mi_cfg->enter_aod_delayed_work);
		}
		break;
	case DISPPARAM_FOD_UNLOCK_FAIL:
		pr_info("Fod fingerprint unlock fail\n");
		mi_cfg->sysfs_fod_unlock_success = false;
		mi_cfg->into_aod_pending = false;
		if(mi_cfg->local_hbm_enabled){
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_FOD_LHBM_OFF);
			mi_cfg->local_hbm_cur_status = false;
		}
		if (panel->power_mode == SDE_MODE_DPMS_LP1 ||
				panel->power_mode == SDE_MODE_DPMS_LP2) {
			cancel_delayed_work(&mi_cfg->enter_aod_delayed_work);
			if (mi_cfg->layer_fod_unlock_success) {
				pr_info("layer_fod_unlock_success is true, skip into aod mode\n");
			} else {
				if (!mi_cfg->unset_doze_brightness)
					mi_cfg->unset_doze_brightness = mi_cfg->doze_brightness_state;
				dsi_panel_set_doze_brightness(panel, mi_cfg->unset_doze_brightness, false);
			}
		}
		break;
	case DISPPARAM_DC_ON:
		pr_info("DC on\n");
		if (mi_cfg->dc_type == 0) {
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DC_ON);
			if (rc)
				pr_err("[%s] failed to send DSI_CMD_SET_MI_DC_ON cmd, rc=%d\n",
						panel->name, rc);
			else
				rc = dsi_panel_update_backlight(panel, mi_cfg->last_bl_level);
		}
		if (panel->mi_cfg.panel_id == 0x4C334100420200) {
			mi_dsi_update_lhbm_cmd_b2reg(panel, true);
			mi_dsi_update_nolp_b2reg(panel, true);
		}
		mi_cfg->dc_enable = true;
		break;
	case DISPPARAM_DC_OFF:
		pr_info("DC off\n");
		if (mi_cfg->dc_type == 0) {
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DC_OFF);
			if (rc)
				pr_err("[%s] failed to send DSI_CMD_SET_MI_DC_OFF cmd, rc=%d\n",
						panel->name, rc);
			else
				rc = dsi_panel_update_backlight(panel, mi_cfg->last_bl_level);
		}
		if (panel->mi_cfg.panel_id == 0x4C334100420200) {
			mi_dsi_update_lhbm_cmd_b2reg(panel, false);
			mi_dsi_update_nolp_b2reg(panel, false);
		}
		mi_cfg->dc_enable = false;
		break;
	default:
		break;
	}

	temp = param & 0x00F00000;
	switch (temp) {
	case DISPPARAM_NORMALMODE1:
		pr_info("normal mode1\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_NORMAL1);
		break;
	case DISPPARAM_P3:
		pr_info("dci p3 mode\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_CRC_DCIP3);
		break;
	case DISPPARAM_SRGB:
		pr_info("sRGB\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_SRGB);
		break;
	case DISPPARAM_DOZE_BRIGHTNESS_HBM:
		if (mi_cfg->in_aod) {
			pr_info("doze hbm On\n");
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DOZE_HBM);
			mi_cfg->dimming_state = STATE_DIM_BLOCK;
		}
		break;
	case DISPPARAM_DOZE_BRIGHTNESS_LBM:
		if (mi_cfg->in_aod) {
			pr_info("doze lbm On\n");
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DOZE_LBM);
			mi_cfg->dimming_state = STATE_DIM_BLOCK;
		}
		break;
	case DISPPARAM_DOZE_OFF:
		pr_info("doze Off\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_NOLP);
		break;
	case DISPPARAM_HBM_BACKLIGHT_RESEND:
		dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DIMMINGON);
		backlight_delta++;
		if (mi_cfg->last_bl_level >= panel->bl_config.bl_max_level - 1)
			resend_backlight = mi_cfg->last_bl_level -
				((backlight_delta%2 == 0) ? 1 : 2);
		else
			resend_backlight = mi_cfg->last_bl_level +
				((backlight_delta%2 == 0) ? 1 : 2);

		pr_info("backlight resend: last_bl_level = %d; resend_backlight = %d\n",
				mi_cfg->last_bl_level, resend_backlight);
		rc = dsi_panel_update_backlight(panel, resend_backlight);
		break;
	case DISPPARAM_FOD_BACKLIGHT:
		if (fod_backlight == 0x1000) {
			pr_info("FOD backlight restore last_bl_level=%d\n",
				mi_cfg->last_bl_level);
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DIMMINGOFF);
			if (mi_cfg->dc_enable && mi_cfg->dc_type) {
				pr_info("FOD backlight restore dc_threshold=%d",
				mi_cfg->dc_threshold);
				rc = dsi_panel_update_backlight(panel, mi_cfg->dc_threshold);
			} else {
				pr_info("FOD backlight restore last_bl_level=%d",
				mi_cfg->last_bl_level);
				rc = dsi_panel_update_backlight(panel, mi_cfg->last_bl_level);
			}
		} else if (fod_backlight >= 0) {
			pr_info("FOD backlight set");
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DIMMINGOFF);
			rc = dsi_panel_update_backlight(panel, fod_backlight);
			mi_cfg->fod_target_backlight = fod_backlight;
			mi_cfg->dimming_state = STATE_NONE;
		}
		break;
	case DISPPARAM_CRC_OFF:
		pr_info("crc off\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_CRC_OFF);
		break;
	default:
		break;
	}

	temp = param & 0x0F000000;
	switch (temp) {
	case DISPPARAM_FOD_BACKLIGHT_ON:
		pr_info("fod_backlight_flag on\n");
		mi_cfg->fod_backlight_flag = true;
		break;
	case DISPPARAM_FOD_BACKLIGHT_OFF:
		pr_info("fod_backlight_flag off\n");
		mi_cfg->fod_backlight_flag = false;
		break;
	case DISPPARAM_ELVSS_DIMMING_ON:
		pr_info("elvss dimming on\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_ELVSS_DIMMING_OFF);
		break;
	case DISPPARAM_ELVSS_DIMMING_OFF:
		pr_info("elvss dimming off\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_ELVSS_DIMMING_OFF);
		break;
	case DISPPARAM_FLAT_MODE_ON:
		pr_info("flat mode on\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_FLAT_MODE_ON);
		break;
	case DISPPARAM_FLAT_MODE_OFF:
		pr_info("flat mode off\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_FLAT_MODE_OFF);
		break;
	case DISPPARAM_DITHER_ON:
		pr_info("dither on\n");
		mi_cfg->dither_enabled = true;
		break;
	case DISPPARAM_DITHER_OFF:
		pr_info("dither off\n");
		mi_cfg->dither_enabled = false;
		break;
	case DISPPARAM_SET_THERMAL_HBM_DISABLE:
		pr_info("set thermal hbm disable\n");
		mi_cfg->thermal_hbm_disabled = true;
		break;
	case DISPPARAM_CLEAR_THERMAL_HBM_DISABLE:
		pr_info("clear thermal hbm disable\n");
		mi_cfg->thermal_hbm_disabled = false;
		break;
	default:
		break;
	}

	temp = param & 0xF0000000;
	switch (temp) {
	case DISPPARAM_GIR_ON:
		if (panel->mi_cfg.panel_id == 0x4C334100420200 && panel->mi_cfg.in_aod) {
			DSI_INFO("In AOD, skip gir on \n");
		} else {
			pr_info("request gir on\n");
			mi_cfg->request_gir_status = true;
		}
		break;
	case DISPPARAM_GIR_OFF:
		pr_info("request gir off\n");
		mi_cfg->request_gir_status = false;
		break;
	default:
		break;
	}

exit:
	mutex_unlock(&panel->panel_lock);
	return rc;
}

