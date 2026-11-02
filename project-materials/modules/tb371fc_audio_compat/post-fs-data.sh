#!/system/bin/sh
MODDIR=${0%/*}
# Suppress the bundled Loud APK without deleting user data or repeatedly uninstalling apps.
if [ -d /product/app/loud ]; then
  if [ -x /system/bin/nsenter ]; then
    /system/bin/nsenter -t 1 -m -- mount -o bind "$MODDIR/empty_loud" /product/app/loud 2>/dev/null
  else
    mount -o bind "$MODDIR/empty_loud" /product/app/loud 2>/dev/null
  fi
fi
