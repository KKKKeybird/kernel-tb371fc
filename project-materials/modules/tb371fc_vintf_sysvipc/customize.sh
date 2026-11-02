#!/system/bin/sh

# TB371FC package: restore executable bits after generic installer defaults.
set_perm "$MODPATH/post-mount.sh" 0 0 0755
