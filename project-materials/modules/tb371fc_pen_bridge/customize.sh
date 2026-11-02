#!/system/bin/sh

# TB371FC package: restore executable bits after generic installer defaults.
set_perm "$MODPATH/ctn730-monitor" 0 0 0755
set_perm "$MODPATH/pen-relay" 0 0 0755
set_perm "$MODPATH/service.sh" 0 0 0755
set_perm "$MODPATH/supervisor.sh" 0 0 0755
set_perm "$MODPATH/uninstall.sh" 0 0 0755
