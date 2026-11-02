#!/system/bin/sh
ui_print 'TB371FC Cover Power Policy: 合盖省电与亮屏前拦截'
ui_print '需完整重启；仅支持已核验的 HyperOS ROM。'
set_perm_recursive "$MODPATH" 0 0 0755 0644
for f in "$MODPATH"/*.sh; do chmod 0755 "$f"; done

# TB371FC package: restore executable bits after generic installer defaults.
set_perm "$MODPATH/post-fs-data.sh" 0 0 0755
set_perm "$MODPATH/service.sh" 0 0 0755
set_perm "$MODPATH/stop.sh" 0 0 0755
set_perm "$MODPATH/uninstall.sh" 0 0 0755
