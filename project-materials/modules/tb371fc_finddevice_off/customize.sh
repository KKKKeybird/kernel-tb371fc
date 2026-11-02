#!/system/bin/sh

# TB371FC package: restore executable bits after generic installer defaults.
set_perm "$MODPATH/service.sh" 0 0 0755
