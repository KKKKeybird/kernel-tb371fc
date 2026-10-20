/*
 * Copyright (c) 2019, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 */

#ifndef _DSI_PANEL_MI_H_
#define _DSI_PANEL_MI_H_

#include <linux/types.h>

#include "dsi_defs.h"

struct dsi_panel;
struct dsi_display_mode;

struct dsi_read_config {
	bool is_read;
	struct dsi_panel_cmd_set read_cmd;
	u32 cmds_rlen;
	u8 rbuf[256];
};

int dsi_panel_write_cmd_set(struct dsi_panel *panel,
				struct dsi_panel_cmd_set *cmd_sets);

int dsi_panel_read_cmd_set(struct dsi_panel *panel,
				struct dsi_read_config *read_config);

int dsi_panel_write_mipi_reg(struct dsi_panel *panel, char *buf, size_t count);

ssize_t dsi_panel_read_mipi_reg(struct dsi_panel *panel, char *buf);

int dsi_panel_match_fps_pen_setting(struct dsi_panel *panel,
				struct dsi_display_mode *adj_mode);

#endif /* _DSI_PANEL_MI_H_ */
