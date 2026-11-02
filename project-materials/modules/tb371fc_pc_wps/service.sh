#!/system/bin/sh
MODDIR=${0%/*}
STATE=/data/adb/tb371fc_pc_wps
PROVISION=/vendor/etc/tb371fc-enhanced/wps/provision-wps.sh

# R1 and R2 deliberately ship the same module.  R2 provides the offline
# provisioner and ext4 assets; R1 remains inert unless a runtime was already
# installed manually.  A missing optional asset set is not a boot failure.
if [ -f "$PROVISION" ]; then
  /system/bin/sh "$PROVISION" || exit 1
fi

if [ ! -f "$STATE/rootfs/etc/passwd" ]; then
  exit 0
fi
if [ -x "$STATE/usr/bin/MSLGd" ] && [ -x "$STATE/kingsoft/wps-office/office6/wps" ]; then
  : # Legacy extracted-directory runtime.
elif [ -f "$STATE/images/usr.ext4" ] && [ -f "$STATE/images/kingsoft.ext4" ]; then
  : # R2 read-only ext4 runtime.
else
  exit 0
fi
exec /system/bin/sh "$MODDIR/supervisor.sh"
