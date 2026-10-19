/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * TB371FC NT36532 panel operations, rebuilt from the factory image.
 *
 * The unit owns the factory panel-layer operations listed in TASK-078 P2.
 * It has no diagnostics of its own; errors surfaced by the factory flow are
 * reported through the existing DSI_ERR paths.
 */
#ifndef _TB371FC_PANEL_UNIT_H_
#define _TB371FC_PANEL_UNIT_H_

#include "dsi_panel.h"

int dsi_panel_pre_prepare(struct dsi_panel *panel);
int dsi_panel_prepare(struct dsi_panel *panel);
int dsi_panel_enable(struct dsi_panel *panel);
int dsi_panel_post_enable(struct dsi_panel *panel);
int dsi_panel_pre_disable(struct dsi_panel *panel);
int dsi_panel_disable(struct dsi_panel *panel);
int dsi_panel_unprepare(struct dsi_panel *panel);
int dsi_panel_post_unprepare(struct dsi_panel *panel);
int dsi_panel_set_lp1(struct dsi_panel *panel);
int dsi_panel_set_lp2(struct dsi_panel *panel);
int dsi_panel_set_nolp(struct dsi_panel *panel);
int dsi_panel_set_backlight(struct dsi_panel *panel, u32 bl_lvl);
int dsi_panel_update_backlight(struct dsi_panel *panel, u32 bl_lvl);

/*
 * TASK-081 D84: the CONFIG_TB371FC_PANEL_UNIT_LEGACY fallback switch was
 * removed (Round3 audit P1-B). It had no Kconfig entry, inverted guards,
 * and a LEGACY=on build failed to link three ways. The factory-aligned
 * unit is now the only path, matching stock.
 */

#endif
