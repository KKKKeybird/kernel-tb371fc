#!/system/bin/sh

MODDIR=${0%/*}
BB=/data/adb/ksu/bin/busybox
TARGET=/system_ext/priv-app/MiuiSystemUI/MiuiSystemUI.apk

for pid in 1 $("$BB" pidof zygote64 2>/dev/null) $("$BB" pidof zygote 2>/dev/null); do
  [ -d "/proc/$pid" ] || continue
  "$BB" nsenter -t "$pid" -m -- "$BB" umount -l "$TARGET" 2>/dev/null || true
done

pids=$("$BB" pidof com.android.systemui 2>/dev/null || true)
[ -n "$pids" ] && kill -9 $pids 2>/dev/null
settings put secure wmshell_multiwindow_version_code 3 2>/dev/null || true
setprop sys.tb371fc.split_compat uninstalled
exit 0
