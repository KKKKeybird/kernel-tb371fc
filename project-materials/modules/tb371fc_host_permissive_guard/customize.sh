#!/system/bin/sh

# TB371FC package: restore executable bits after generic installer defaults.
set_perm "$MODPATH/apply.sh" 0 0 0755
set_perm "$MODPATH/busybox" 0 0 0755
set_perm "$MODPATH/post-fs-data.sh" 0 0 0755
set_perm "$MODPATH/protect-live.sh" 0 0 0755
set_perm "$MODPATH/rollback-live.sh" 0 0 0755
set_perm "$MODPATH/service.sh" 0 0 0755
set_perm "$MODPATH/uninstall.sh" 0 0 0755
set_perm "$MODPATH/verify.sh" 0 0 0755
