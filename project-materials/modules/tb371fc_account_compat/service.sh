#!/system/bin/sh
MODDIR=${0%/*}
BB=/data/adb/ksu/bin/busybox
TARGET=/system_ext/priv-app/RtMiCloudSDK/RtMiCloudSDK.apk
PATCH=$MODDIR/payload/RtMiCloudSDK.apk
LOG=/data/vendor/tb371fc/account-compat.log
mkdir -p /data/vendor/tb371fc
umask 077
exec >>"$LOG" 2>&1
setprop sys.tb371fc.account_compat waiting
n=0
while [ "$(getprop sys.boot_completed)" != 1 ] && [ "$n" -lt 180 ]; do
  sleep 2
  n=$((n+1))
done
[ "$(getprop sys.boot_completed)" = 1 ] || exit 0
[ "$(settings get secure user_setup_complete 2>/dev/null)" = 1 ] || exit 0
# Leave newer or different ROM/SDK versions untouched.
[ "$(cat /data/vendor/tb371fc/build-id 2>/dev/null)" = "TB371FC-HyperOS3-20261103-full" ] || exit 0
[ "$(getprop ro.bootanim.set_orientation_startup_0)" = "ORIENTATION_90" ] || exit 0
SDK_CODE=$(dumpsys package com.xiaomi.micloud.sdk | sed -n 's/.*versionCode=\([0-9]*\).*/\1/p' | head -n 1)
[ "$SDK_CODE" = "20" ] || exit 0
[ -x "$BB" ] && [ -f "$TARGET" ] && [ -f "$PATCH" ] || exit 1
chcon u:object_r:system_file:s0 "$PATCH" || exit 1
# Do not expose the unsupported implementation or install a modified APK.
# PackageManager reads the original system APK at boot. A late read-only bind
# supplies the guarded library method to subsequently launched app processes.
pm hide --user 0 com.xiaomi.finddevice || exit 1
for pid in 1 $(pidof zygote64 2>/dev/null) $(pidof zygote 2>/dev/null); do
  if "$BB" nsenter -t "$pid" -m -- "$BB" cmp -s "$PATCH" "$TARGET"; then
    continue
  fi
  "$BB" nsenter -t "$pid" -m -- "$BB" mount -o bind,ro "$PATCH" "$TARGET" || {
    setprop sys.tb371fc.account_compat failed
    exit 1
  }
done
am force-stop --user 0 com.xiaomi.account
setprop sys.tb371fc.account_compat safe_ready
printf '%s Account SDK isOpen fallback active; FindDevice remains hidden.\n' "$(date '+%F %T')"
