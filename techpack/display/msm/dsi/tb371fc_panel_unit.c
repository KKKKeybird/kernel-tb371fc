// SPDX-License-Identifier: GPL-2.0-only
/*
 * TB371FC NT36532 panel operations, rebuilt from the factory image.
 *
 * TASK-078 P2. The ordering and branches follow the factory panel layer.
 * Only the existing factory-style error prints remain; no unit-local
 * diagnostics are added.
 */

#define pr_fmt(fmt) "tb371fc_panel_unit: " fmt

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/pwm.h>
#include <linux/string.h>
#include <linux/types.h>
#include "dsi_panel.h"
#include "dsi_defs.h"
#include "dsi_panel_mi.h"
#include "tb371fc_panel_unit.h"

extern int lcd_bl_set_led_brightness(int value);
extern void lcd_bl_set_reg(void);
extern int lcd_set_bias(bool enable);
extern bool dc_skip_set_backlight(struct dsi_panel *panel, u32 bl_lvl);

#ifdef CONFIG_TB371FC_PANEL_UNIT_LEGACY
#define dsi_panel_unit_update_backlight dsi_panel_legacy_update_backlight
#define dsi_panel_unit_update_pwm_backlight dsi_panel_legacy_update_pwm_backlight
#else
#define dsi_panel_unit_update_backlight dsi_panel_update_backlight
#define dsi_panel_unit_update_pwm_backlight dsi_panel_update_pwm_backlight
#endif

static int dsi_panel_set_pinctrl_state(struct dsi_panel *panel, bool enable)
{
	struct pinctrl_state *state;
	int rc;

	if (panel->host_config.ext_bridge_mode)
		return 0;

	state = enable ? panel->pinctrl.active : panel->pinctrl.suspend;
	rc = pinctrl_select_state(panel->pinctrl.pinctrl, state);
	if (rc)
		DSI_ERR("[%s] failed to set pin state, rc=%d\n", panel->name, rc);

	return rc;
}

static int dsi_panel_power_on(struct dsi_panel *panel)
{
	struct dsi_panel_reset_config *r_config = &panel->reset_config;
	struct gpio_desc *desc;
	int rc;
	int i;

	rc = dsi_pwr_enable_regulator(&panel->power_info, true);
	if (rc) {
		DSI_ERR("[%s] failed to enable vregs, rc=%d\n", panel->name, rc);
		return rc;
	}

	rc = dsi_panel_set_pinctrl_state(panel, true);
	if (rc) {
		DSI_ERR("[%s] failed to set pinctrl, rc=%d\n", panel->name, rc);
		goto error_disable_vregs;
	}

	lcd_set_bias(true);
	lcd_bl_set_reg();
	usleep_range(10000, 10010);

	if (gpio_is_valid(r_config->disp_en_gpio)) {
		rc = gpiod_direction_output_raw(gpio_to_desc(
						r_config->disp_en_gpio), 1);
		if (rc) {
			DSI_ERR("unable to set dir for disp gpio rc=%d\n", rc);
			goto error_disable_gpio;
		}
	}

	if (r_config->count) {
		rc = gpiod_direction_output_raw(gpio_to_desc(
						r_config->reset_gpio),
						r_config->sequence[0].level);
		if (rc) {
			DSI_ERR("unable to set dir for rst gpio rc=%d\n", rc);
			goto error_disable_gpio;
		}
	}

	for (i = 0; i < r_config->count; i++) {
		desc = gpio_to_desc(r_config->reset_gpio);
		gpiod_set_raw_value(desc, r_config->sequence[i].level);
		if (r_config->sequence[i].sleep_ms)
			usleep_range(r_config->sequence[i].sleep_ms * 1000,
				     r_config->sequence[i].sleep_ms * 1000 + 100);
	}

	if (gpio_is_valid(panel->bl_config.en_gpio)) {
		rc = gpiod_direction_output_raw(gpio_to_desc(
						panel->bl_config.en_gpio), 1);
		if (rc)
			DSI_ERR("unable to set dir for bklt gpio rc=%d\n", rc);
	}

	if (gpio_is_valid(r_config->lcd_mode_sel_gpio)) {
		bool out;

		if (r_config->mode_sel_state == MODE_SEL_DUAL_PORT ||
		    r_config->mode_sel_state == MODE_GPIO_LOW)
			out = false;
		else
			out = true;

		rc = gpiod_direction_output_raw(gpio_to_desc(
						r_config->lcd_mode_sel_gpio),
						out);
		if (rc)
			DSI_ERR("unable to set dir for mode gpio rc=%d\n", rc);
	}

	if (gpio_is_valid(panel->panel_test_gpio)) {
		rc = gpiod_direction_input(gpio_to_desc(panel->panel_test_gpio));
		if (rc)
			DSI_WARN("unable to set dir for panel test gpio rc=%d\n", rc);
	}

	return 0;

error_disable_gpio:
	if (gpio_is_valid(r_config->disp_en_gpio))
		gpiod_set_raw_value(gpio_to_desc(r_config->disp_en_gpio), 0);

	if (gpio_is_valid(panel->bl_config.en_gpio))
		gpiod_set_raw_value(gpio_to_desc(panel->bl_config.en_gpio), 0);

	(void)dsi_panel_set_pinctrl_state(panel, false);

error_disable_vregs:
	(void)dsi_pwr_enable_regulator(&panel->power_info, false);
	return rc;
}

static int dsi_panel_power_off(struct dsi_panel *panel)
{
	struct dsi_panel_reset_config *r_config = &panel->reset_config;
	int rc = 0;

	/* D67 (final): the touch IC shares the panel reset/vddio and the DT
	 * carries no mi,is-tddi-flag / doubleclick wiring, so running this
	 * power-off resets the TDDI/touch FW (measured: fw_status A3 at
	 * suspend+55ms -> 0x00 in doze) and kills the armed DT2W gesture.
	 * The pre-TASK-078 driver never ran power-off (p108_keep_panel_power
	 * early-return) and DT2W worked; keep that proven behavior. The
	 * DSI link and backlight are still disabled normally. */
	return 0;

	if (gpio_is_valid(r_config->disp_en_gpio))
		gpiod_set_raw_value(gpio_to_desc(r_config->disp_en_gpio), 0);

	if (gpio_is_valid(r_config->reset_gpio) &&
	    !panel->reset_gpio_always_on)
		gpiod_set_raw_value(gpio_to_desc(r_config->reset_gpio), 0);

	if (gpio_is_valid(r_config->lcd_mode_sel_gpio))
		gpiod_set_raw_value(gpio_to_desc(r_config->lcd_mode_sel_gpio),
				    0);

	if (gpio_is_valid(panel->panel_test_gpio)) {
		rc = gpiod_direction_input(gpio_to_desc(panel->panel_test_gpio));
		if (rc)
			DSI_WARN("set dir for panel test gpio failed rc=%d\n", rc);
	}

	lcd_set_bias(false);
	usleep_range(100000, 100998);

	rc = dsi_panel_set_pinctrl_state(panel, false);
	if (rc) {
		DSI_ERR("[%s] failed set pinctrl state, rc=%d\n", panel->name, rc);
		return rc;
	}

	rc = dsi_pwr_enable_regulator(&panel->power_info, false);
	if (rc)
		DSI_ERR("[%s] failed to enable vregs, rc=%d\n", panel->name, rc);

	return rc;
}

int dsi_panel_pre_prepare(struct dsi_panel *panel)
{
	int rc;

	if (!panel) {
		DSI_ERR("invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);
	rc = dsi_panel_power_on(panel);
	if (rc)
		DSI_ERR("[%s] panel power on failed, rc=%d\n",
			panel->name, rc);
	mutex_unlock(&panel->panel_lock);
	return rc;
}

int dsi_panel_prepare(struct dsi_panel *panel)
{
	int rc;

	if (!panel) {
		DSI_ERR("invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);
	rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_PRE_ON);
	if (rc)
		DSI_ERR("[%s] failed to send DSI_CMD_SET_PRE_ON cmds, rc=%d\n",
			panel->name, rc);

	mutex_unlock(&panel->panel_lock);
	return rc;
}

int dsi_panel_enable(struct dsi_panel *panel)
{
	int rc;

	if (!panel) {
		DSI_ERR("invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);
	rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_ON);
	if (rc)
		DSI_ERR("[%s] failed to send DSI_CMD_SET_ON cmds, rc=%d\n",
			panel->name, rc);
	else
		panel->panel_initialized = true;
	mutex_unlock(&panel->panel_lock);
	return rc;
}

int dsi_panel_post_enable(struct dsi_panel *panel)
{
	int rc;

	if (!panel) {
		DSI_ERR("invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);
	rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_POST_ON);
	if (rc)
		DSI_ERR("[%s] failed to send DSI_CMD_SET_POST_ON cmds, rc=%d\n",
			panel->name, rc);

	if (panel->name &&
	    (strstr(panel->name, "nt36532 tianma") ||
	     strstr(panel->name, "nt36532 boe"))) {
		rc = dsi_panel_match_fps_pen_setting(panel, panel->cur_mode);
		if (rc)
			DSI_ERR("[%s] failed to update TP fps code setting, rc=%d\n",
				panel->name, rc);
	}
	mutex_unlock(&panel->panel_lock);
	return rc;
}

int dsi_panel_pre_disable(struct dsi_panel *panel)
{
	int rc;

	if (!panel) {
		DSI_ERR("invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);
	rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_PRE_OFF);
	if (rc)
		DSI_ERR("[%s] failed to send DSI_CMD_SET_PRE_OFF cmds, rc=%d\n",
			panel->name, rc);
	mutex_unlock(&panel->panel_lock);
	return rc;
}

int dsi_panel_disable(struct dsi_panel *panel)
{
	int rc = 0;
	struct dsi_panel_mi_cfg *mi_cfg;

	if (!panel) {
		DSI_ERR("invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);
	mi_cfg = &panel->mi_cfg;
	if (!atomic_read(&panel->esd_recovery_pending)) {
		if (dsi_panel_is_type_oled(panel) &&
		    (panel->power_mode == SDE_MODE_DPMS_LP1 ||
		     panel->power_mode == SDE_MODE_DPMS_LP2))
			dsi_pwr_panel_regulator_mode_set(&panel->power_info,
					"ibb", REGULATOR_MODE_STANDBY);

		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_OFF);
		if (rc) {
			pr_warn_ratelimited("[%s] failed to send DSI_CMD_SET_OFF cmds, rc=%d\n",
				panel->name, rc);
			rc = 0;
		}
	} else {
		mi_cfg->unset_doze_brightness = mi_cfg->doze_brightness_state;
	}

	panel->panel_initialized = false;
	panel->power_mode = SDE_MODE_DPMS_OFF;
	mutex_unlock(&panel->panel_lock);
	return rc;
}

int dsi_panel_unprepare(struct dsi_panel *panel)
{
	int rc;

	if (!panel) {
		DSI_ERR("invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);
	rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_POST_OFF);
	if (rc)
		DSI_ERR("[%s] failed to send DSI_CMD_SET_POST_OFF cmds, rc=%d\n",
			panel->name, rc);
	mutex_unlock(&panel->panel_lock);
	return rc;
}

int dsi_panel_post_unprepare(struct dsi_panel *panel)
{
	int rc;

	if (!panel) {
		DSI_ERR("invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);
	rc = dsi_panel_power_off(panel);
	if (rc)
		DSI_ERR("[%s] panel power_Off failed, rc=%d\n", panel->name, rc);
	mutex_unlock(&panel->panel_lock);
	return rc;
}

int dsi_panel_set_lp1(struct dsi_panel *panel)
{
	int rc;

	if (!panel) {
		DSI_ERR("invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);
	if (!panel->panel_initialized) {
		rc = 0;
	} else {
		if (dsi_panel_is_type_oled(panel) &&
		    panel->power_mode != SDE_MODE_DPMS_LP2)
			dsi_pwr_panel_regulator_mode_set(&panel->power_info,
					"ibb", REGULATOR_MODE_IDLE);

		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_LP1);
		if (rc)
			DSI_ERR("[%s] failed to send DSI_CMD_SET_LP1 cmds, rc=%d\n",
				panel->name, rc);
	}
	mutex_unlock(&panel->panel_lock);
	return rc;
}

int dsi_panel_set_lp2(struct dsi_panel *panel)
{
	int rc;

	if (!panel) {
		DSI_ERR("invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);
	if (!panel->panel_initialized) {
		rc = 0;
	} else {
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_LP2);
		if (rc)
			DSI_ERR("[%s] failed to send DSI_CMD_SET_LP2 cmds, rc=%d\n",
				panel->name, rc);
	}
	mutex_unlock(&panel->panel_lock);
	return rc;
}

int dsi_panel_set_nolp(struct dsi_panel *panel)
{
	int rc;

	if (!panel) {
		DSI_ERR("invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);
	if (!panel->panel_initialized) {
		rc = 0;
	} else {
		if (dsi_panel_is_type_oled(panel) &&
		    (panel->power_mode == SDE_MODE_DPMS_LP1 ||
		     panel->power_mode == SDE_MODE_DPMS_LP2))
			dsi_pwr_panel_regulator_mode_set(&panel->power_info,
					"ibb", REGULATOR_MODE_NORMAL);

		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_NOLP);
		if (rc)
			DSI_ERR("[%s] failed to send DSI_CMD_SET_NOLP cmds, rc=%d\n",
				panel->name, rc);
	}
	mutex_unlock(&panel->panel_lock);
	return rc;
}

static int dsi_panel_update_pwm_backlight(struct dsi_panel *panel, u32 bl_lvl)
{
	u32 duty = 0;
	u32 period_ns = 0;
	struct dsi_backlight_config *bl;
	int rc = 0;

	if (!panel) {
		DSI_ERR("invalid params\n");
		return -EINVAL;
	}

	bl = &panel->bl_config;
	if (!bl->pwm_bl) {
		DSI_ERR("pwm device not found\n");
		return -EINVAL;
	}

	period_ns = bl->pwm_period_usecs * NSEC_PER_USEC;
	duty = bl_lvl * period_ns;
	duty /= bl->bl_max_level;

	rc = pwm_config(bl->pwm_bl, duty, period_ns);
	if (rc) {
		DSI_ERR("[%s] failed to change pwm config, rc=%d\n",
			panel->name, rc);
		goto error;
	}

	if (bl_lvl == 0 && bl->pwm_enabled) {
		pwm_disable(bl->pwm_bl);
		bl->pwm_enabled = false;
		return 0;
	}

	if (!bl->pwm_enabled) {
		rc = pwm_enable(bl->pwm_bl);
		if (rc) {
			DSI_ERR("[%s] failed to enable pwm, rc=%d\n",
				panel->name, rc);
			goto error;
		}
		bl->pwm_enabled = true;
	}

error:
	return rc;
}

int dsi_panel_update_backlight(struct dsi_panel *panel, u32 bl_lvl)
{
	struct mipi_dsi_device *dsi;
	struct dsi_panel_mi_cfg *mi_cfg;
	int rc = 0;

	if (!panel || bl_lvl > 0xffff) {
		DSI_ERR("invalid params\n");
		return -EINVAL;
	}

	dsi = &panel->mipi_device;
	mi_cfg = &panel->mi_cfg;

	if (panel->bl_config.bl_inverted_dbv)
		bl_lvl = ((bl_lvl & 0xff) << 8) | (bl_lvl >> 8);

	if (panel->bl_config.bl_dcs_subtype == 0xc2)
		rc = mipi_dsi_dcs_set_display_brightness(dsi, bl_lvl);
	else
		rc = mipi_dsi_dcs_set_display_brightness(dsi, bl_lvl);

	if (rc < 0)
		DSI_ERR("failed to update dcs backlight:%d\n", bl_lvl);

	if (mi_cfg->local_hbm_enabled) {
		if (bl_lvl > 2047 && mi_cfg->dim_fp_dbv_max_in_hbm_flag == false) {
			dsi_panel_tx_cmd_set(panel,
				DSI_CMD_SET_MI_DIM_FP_DBV_MAX_IN_HBM);
			mi_cfg->dim_fp_dbv_max_in_hbm_flag = true;
		} else if (bl_lvl <= 2047 &&
			   mi_cfg->dim_fp_dbv_max_in_hbm_flag == true) {
			dsi_panel_tx_cmd_set(panel,
				DSI_CMD_SET_MI_DIM_FP_DBV_MAX_IN_NORMAL);
			mi_cfg->dim_fp_dbv_max_in_hbm_flag = false;
		}
		if (panel->mi_cfg.local_hbm_cur_status)
			mi_dsi_panel_set_fod_brightness(dsi, bl_lvl);
	}
	return rc;
}

int dsi_panel_set_backlight(struct dsi_panel *panel, u32 bl_lvl)
{
	struct dsi_backlight_config *bl = &panel->bl_config;
	struct dsi_backlight_config *bl_slaver = &panel->bl_slaver_config;
	struct dsi_panel_mi_cfg *mi_cfg = &panel->mi_cfg;
	int rc = 0;

	if (panel->host_config.ext_bridge_mode)
		return 0;

	DSI_DEBUG("backlight type:%d lvl:%d\n", bl->type, bl_lvl);

	if (mi_cfg->dc_type == 2 && mi_cfg->last_bl_level != bl_lvl) {
		mi_cfg->last_bl_level = bl_lvl;
		if (bl_lvl)
			mi_cfg->last_nonzero_bl_level = bl_lvl;
	}

	if (dc_skip_set_backlight(panel, bl_lvl))
		return rc;

	if (0 == bl_lvl && (panel->host_config.cphy_strength ||
			     panel->mi_cfg.panel_id == 0x4C38314100420400)) {
		dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_INSERT_BLACK);
		usleep_range(6 * 1000, 6 * 1000 + 10);
	}

	switch (bl->type) {
	case DSI_BACKLIGHT_WLED:
		if (panel->mi_cfg.panel_id == 0x4C38314100420400) {
			rc = backlight_device_set_brightness(bl->raw_bd, bl_lvl);
			rc = backlight_device_set_brightness(bl_slaver->raw_bd, bl_lvl);
		} else {
			rc = backlight_device_set_brightness(bl->raw_bd, bl_lvl);
		}
		break;
	case DSI_BACKLIGHT_DCS:
		if (mi_cfg->fod_backlight_flag) {
			DSI_INFO("fod_backlight_flag set, skip set backlight %d\n", bl_lvl);
		} else if (mi_cfg->hbm_51_ctrl_flag &&
			   (mi_cfg->fod_hbm_enabled ||
			    (mi_cfg->thermal_hbm_disabled && bl_lvl > 2047 &&
			     mi_cfg->last_bl_level > 0) ||
			    (mi_cfg->hbm_enabled && !mi_cfg->hbm_brightness &&
			     !mi_cfg->thermal_hbm_disabled))) {
			DSI_INFO("fod_hbm_enabled(%d), hbm_enabled(%d), thermal_hbm_disabled(%d), skip set backlight %d\n",
				mi_cfg->fod_hbm_enabled, mi_cfg->hbm_enabled,
				mi_cfg->thermal_hbm_disabled, bl_lvl);
		} else if (mi_cfg->thermal_hbm_disabled && bl_lvl > 2047 &&
			   mi_cfg->last_bl_level == 0) {
			rc = dsi_panel_unit_update_backlight(panel, 2047);
		} else {
			rc = dsi_panel_unit_update_backlight(panel, bl_lvl);
		}
		break;
	case DSI_BACKLIGHT_EXTERNAL:
		rc = lcd_bl_set_led_brightness((int)bl_lvl);
		if (rc)
			DSI_ERR("lcd_bl_set_led_brightness failed, rc=%d\n", rc);
		break;
	case DSI_BACKLIGHT_PWM:
		rc = dsi_panel_unit_update_pwm_backlight(panel, bl_lvl);
		break;
	default:
		DSI_ERR("Backlight type(%d) not supported\n", bl->type);
		rc = -ENOTSUPP;
	}

	if ((mi_cfg->last_bl_level == 0 ||
	     mi_cfg->dimming_state == STATE_DIM_RESTORE) && bl_lvl) {
		if (mi_cfg->panel_on_dimming_delay)
			schedule_delayed_work(&mi_cfg->dimming_enable_delayed_work,
				msecs_to_jiffies(mi_cfg->panel_on_dimming_delay));
		if (mi_cfg->dimming_state == STATE_DIM_RESTORE)
			mi_cfg->dimming_state = STATE_NONE;
	}

	if (mi_cfg->last_bl_level == 0 && bl_lvl &&
	    (panel->host_config.cphy_strength ||
	     panel->mi_cfg.panel_id == 0x4C38314100420400))
		dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_DISABLE_INSERT_BLACK);

	if (bl_lvl > 0 && mi_cfg->last_bl_level == 0 && mi_cfg->dc_type)
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_CRC_OFF);

	if (bl_lvl == 0 && mi_cfg->dc_type)
		mi_cfg->dc_enable = false;

	mi_cfg->last_bl_level = bl_lvl;
	if (bl_lvl)
		mi_cfg->last_nonzero_bl_level = bl_lvl;
	return rc;
}
