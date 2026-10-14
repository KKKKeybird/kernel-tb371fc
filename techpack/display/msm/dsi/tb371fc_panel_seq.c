// SPDX-License-Identifier: GPL-2.0-only
/*
 * tb371fc_panel_seq.c - TB371FC NT36532 in-cell panel power-on timing.
 *
 * TASK-078. The stock driver hard-codes the timing of dsi_panel_power_on()
 * instead of reading it from the dtbo. This unit carries those constants so
 * the rebuilt power-on path reproduces the factory timing literally.
 *
 * Sequence (from the disassembly of the stock dsi_panel_power_on
 * @0xffffff8008f920c0, len 0x340, in the factory 4.19.157 image):
 *
 *   1. printk the power-on banner
 *   2. usleep_range(10000, 10010)                 10 ms
 *   3. branch on panel->display_type
 *        cmd mode   -> panel reset GPIO path
 *        video mode -> TE / reset path
 *   4. an invalid GPIO descriptor yields -22 and bails out
 *   5. gpiod_direction_output_raw(desc, 1)         assert
 *   6. gpio_to_desc loop, 8 GPIO operations
 *   7. dsi_pwr_enable_regulator x2
 *   8. pinctrl_select_state x2
 *   9. usleep_range(..., 1000)                    1 ms
 *  10. lcd_set_bias() + lcd_bl_set_reg()          backlight bias
 *  11. gpiod_direction_input(desc)
 *
 * GPIO numbers, regulator names and pinctrl states are NOT hard-coded here;
 * the stock driver takes all of them from the panel structure, which comes
 * from the dtbo. Only the timing and the order are factory constants.
 */

#define pr_fmt(fmt) "tb371fc_panel: %s: " fmt, __func__

#include <linux/delay.h>
#include <linux/kernel.h>

#include "tb371fc_panel_seq.h"

/*
 * The 10 ms delay the stock driver issues right after the power-on banner,
 * before it touches any GPIO or regulator.
 */
void tb371fc_panel_power_on_initial_delay(void)
{
	usleep_range(TB371FC_PANEL_POWER_ON_DELAY_US_MIN,
		     TB371FC_PANEL_POWER_ON_DELAY_US_MAX);
}
EXPORT_SYMBOL_GPL(tb371fc_panel_power_on_initial_delay);

/*
 * The 1 ms delay the stock driver issues between the pinctrl transitions and
 * the backlight bias calls.
 */
void tb371fc_panel_power_on_backlight_delay(void)
{
	usleep_range(TB371FC_PANEL_SECOND_DELAY_US,
		     TB371FC_PANEL_SECOND_DELAY_US + TB371FC_PANEL_RESET_DELAY_SLACK_US);
}
EXPORT_SYMBOL_GPL(tb371fc_panel_power_on_backlight_delay);

/*
 * One step of the reset pulse train. The stock driver walks the panel's
 * reset sequence (dtbo) and issues usleep_range(sleep_ms * 1000, +100)
 * per group; sleep_ms is 10 for this panel.
 */
void tb371fc_panel_reset_step_delay(unsigned int sleep_ms)
{
	usleep_range(sleep_ms * 1000,
		     sleep_ms * 1000 + TB371FC_PANEL_RESET_DELAY_SLACK_US);
}
EXPORT_SYMBOL_GPL(tb371fc_panel_reset_step_delay);
