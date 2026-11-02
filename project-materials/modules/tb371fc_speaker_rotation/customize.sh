#!/system/bin/sh

# TB371FC package: restore executable bits after generic installer defaults.
set_perm "$MODPATH/matrix-write" 0 0 0755
set_perm "$MODPATH/post-mount.sh" 0 0 0755
set_perm "$MODPATH/quad-policy.sh" 0 0 0755
set_perm "$MODPATH/restart.sh" 0 0 0755
set_perm "$MODPATH/rollback.sh" 0 0 0755
set_perm "$MODPATH/service.sh" 0 0 0755
set_perm "$MODPATH/supervisor.sh" 0 0 0755
set_perm "$MODPATH/uninstall.sh" 0 0 0755
