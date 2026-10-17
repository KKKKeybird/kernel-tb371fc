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
 * Build-time switch: define this to force the legacy CLO operations. The
 * runtime default remains the factory-aligned unit.
 */
#ifndef CONFIG_TB371FC_PANEL_UNIT_LEGACY
#define tb371fc_panel_unit_active()	(true)
#else
static inline bool tb371fc_panel_unit_active(void)
{
	return false;
}
#endif

#endif
