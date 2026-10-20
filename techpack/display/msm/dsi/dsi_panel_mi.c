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

#define to_dsi_display(x) container_of(x, struct dsi_display, host)

static struct dsi_read_config g_dsi_read_cfg;
struct dsi_panel *g_panel;

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
