/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * tb371fc_panel_seq.h - TB371FC NT36532 in-cell panel power-on timing,
 * transcribed from the factory driver (TASK-078).
 *
 * Provenance (verified against the factory 4.19.157 image at
 * /home/smith/t31/Image-b):
 *   dsi_panel_power_on @0xffffff8008f920c0  len 0x340
 *
 * What the stock driver hard-codes, and what it reads from the panel
 * structure (i.e. from the dtbo), was established from the disassembly:
 *
 *   hard-coded : 10000/10010 us and 1000 us delays, the branch order,
 *                the 8x gpio_to_desc loop, 2x regulator, 2x pinctrl
 *   from struct : every GPIO number, every regulator name, pinctrl state
 *
 * This header therefore only carries the timing constants. The GPIO and
 * regulator wiring stays with the caller's panel data, exactly as the
 * stock driver does.
 */
#ifndef _TB371FC_PANEL_SEQ_H_
#define _TB371FC_PANEL_SEQ_H_

/*
 * The two delays the stock driver issues literally:
 *   offset 0x80/0x84 : mov w0,#0x2710 / mov w1,#0x271a
 *   offset 0x214     : mov w23,#0x3e8
 */
#define TB371FC_PANEL_POWER_ON_DELAY_US_MIN	10000
#define TB371FC_PANEL_POWER_ON_DELAY_US_MAX	10010
#define TB371FC_PANEL_SECOND_DELAY_US		1000

/*
 * Reset pulse train: the factory image hard-codes 10 ms per step. The
 * stock dtbo agrees (reset-sequence is 5 groups of level + 0x0a).
 */
#define TB371FC_PANEL_RESET_STEP_MS		10

/*
 * The timing helpers the rebuilt power-on path calls. GPIO numbers,
 * regulators and pinctrl states stay with the caller's panel data.
 */
void tb371fc_panel_power_on_initial_delay(void);
void tb371fc_panel_power_on_backlight_delay(void);
void tb371fc_panel_reset_step_delay(unsigned int sleep_ms);

/* Number of GPIO operations in the stock gpio_to_desc loop. */
#define TB371FC_PANEL_GPIO_OP_COUNT		8

/* Number of regulator and pinctrl transitions. */
#define TB371FC_PANEL_REGULATOR_COUNT		2
#define TB371FC_PANEL_PINCTRL_COUNT		2

/*
 * The stock reset pulse train is driven by the panel's reset sequence
 * (dtbo: 5 groups of level + 10 ms), executed as
 * usleep_range(sleep_ms * 1000, +100).
 */
#define TB371FC_PANEL_RESET_DELAY_SLACK_US	100

#endif /* _TB371FC_PANEL_SEQ_H_ */
