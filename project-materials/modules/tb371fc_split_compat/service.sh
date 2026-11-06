#!/system/bin/sh

MODDIR=${0%/*}
BB=/data/adb/ksu/bin/busybox
TARGET=/system_ext/priv-app/MiuiSystemUI/MiuiSystemUI.apk
PATCH=$MODDIR/payload/MiuiSystemUI.apk
BASE_SHA=8bb7b6fc36e2bfbe4f3ee2ae91512c95ee32209e86a71e1437040f3eaf45e5bb
PATCH_SHA=0b243aebb7efe8f5655dbe467d61e0def5fc502005be508028857fa3d57089da
STATE=/data/vendor/tb371fc/split-compat
LOG=$STATE/service.log
HEALTHY=$STATE/healthy
FAILURE=$STATE/failure

mkdir -p "$STATE"
umask 077
exec >>"$LOG" 2>&1
setprop sys.tb371fc.split_compat waiting

fail() {
  printf 'failed: %s\n' "$*"
  setprop sys.tb371fc.split_compat failed
  : >"$FAILURE"
  exit 1
}

n=0
while [ "$(getprop sys.boot_completed)" != 1 ] && [ "$n" -lt 180 ]; do
  sleep 2
  n=$((n + 1))
done
[ "$(getprop sys.boot_completed)" = 1 ] || exit 0

# Leave newer or different ROMs, devices and SystemUI builds untouched.
[ "$(getprop ro.product.device)" = sheng ] || exit 0
[ "$(getprop ro.build.version.sdk)" = 35 ] || exit 0
[ "$(cat /data/vendor/tb371fc/build-id 2>/dev/null)" = "TB371FC-HyperOS3-20261103-full" ] || exit 0
[ "$(getprop ro.bootanim.set_orientation_startup_0)" = "ORIENTATION_90" ] || exit 0
[ -x "$BB" ] && [ -f "$TARGET" ] && [ -f "$PATCH" ] || fail "required file or BusyBox is missing"
[ "$("$BB" sha256sum "$PATCH" | "$BB" awk '{print $1}')" = "$PATCH_SHA" ] ||
  fail "patched payload hash mismatch"

rm -f "$HEALTHY" "$FAILURE"
"$BB" chcon u:object_r:system_file:s0 "$PATCH" || fail "cannot label patched APK"
"$BB" chmod 0644 "$PATCH" || fail "cannot make patched APK readable"

# The watchdog can recover even if this service exits unexpectedly after mounting.
"$BB" setsid sh "$MODDIR/rollback.sh" 180 </dev/null >/dev/null 2>&1 &
printf '%s\n' "$!" >"$STATE/watchdog.pid"

mounted=0
for pid in 1 $("$BB" pidof zygote64 2>/dev/null) $("$BB" pidof zygote 2>/dev/null); do
  [ -d "/proc/$pid" ] || continue
  if "$BB" nsenter -t "$pid" -m -- "$BB" cmp -s "$PATCH" "$TARGET"; then
    mounted=1
    continue
  fi
  actual=$("$BB" nsenter -t "$pid" -m -- "$BB" sha256sum "$TARGET" 2>/dev/null |
    "$BB" awk '{print $1}')
  [ "$actual" = "$BASE_SHA" ] || fail "pid $pid exposes unexpected SystemUI hash $actual"
  "$BB" nsenter -t "$pid" -m -- "$BB" mount -o bind,ro "$PATCH" "$TARGET" ||
    fail "bind mount failed in pid $pid mount namespace"
  "$BB" nsenter -t "$pid" -m -- "$BB" cmp -s "$PATCH" "$TARGET" ||
    fail "bind mount verification failed in pid $pid mount namespace"
  mounted=1
done
[ "$mounted" = 1 ] || fail "no target mount namespaces available"

old_pids=$("$BB" pidof com.android.systemui 2>/dev/null || true)
[ -n "$old_pids" ] && kill -9 $old_pids 2>/dev/null

n=0
stable=0
while [ "$n" -lt 90 ]; do
  sleep 2
  alive=$("$BB" pidof com.android.systemui 2>/dev/null || true)
  version=$(settings get secure wmshell_multiwindow_version_code 2>/dev/null || true)
  if [ -n "$alive" ] && [ "$version" = 7 ]; then
    stable=$((stable + 1))
  else
    stable=0
  fi
  [ "$stable" -ge 5 ] && break
  n=$((n + 1))
done
[ "$stable" -ge 5 ] || fail "SystemUI did not remain healthy with capability 7"

: >"$HEALTHY"
setprop sys.tb371fc.split_compat ready
printf '%s capability=7 SystemUI pid=%s\n' "$(date '+%F %T')" "$alive"
exit 0
