#!/system/bin/sh

MODDIR=${0%/*}
BB=/data/adb/ksu/bin/busybox
TARGET=/system_ext/priv-app/MiuiSystemUI/MiuiSystemUI.apk
BASE_SHA=8bb7b6fc36e2bfbe4f3ee2ae91512c95ee32209e86a71e1437040f3eaf45e5bb
STATE=/data/vendor/tb371fc/split-compat
HEALTHY=$STATE/healthy
LOG=$STATE/rollback.log
timeout=${1:-180}

log() {
  printf '%s %s\n' "$(date '+%F %T')" "$*" >>"$LOG"
}

elapsed=0
while [ "$elapsed" -lt "$timeout" ]; do
  [ -f "$HEALTHY" ] && {
    log "health confirmed; watchdog exiting"
    exit 0
  }
  sleep 2
  elapsed=$((elapsed + 2))
done

log "health timeout or service failure; rolling back this boot"
for pid in 1 $("$BB" pidof zygote64 2>/dev/null) $("$BB" pidof zygote 2>/dev/null); do
  [ -d "/proc/$pid" ] || continue
  "$BB" nsenter -t "$pid" -m -- "$BB" umount -l "$TARGET" 2>/dev/null || true
done

pids=$("$BB" pidof com.android.systemui 2>/dev/null || true)
[ -n "$pids" ] && kill -9 $pids 2>/dev/null
sleep 4

for pid in 1 $("$BB" pidof zygote64 2>/dev/null) $("$BB" pidof zygote 2>/dev/null); do
  [ -d "/proc/$pid" ] || continue
  "$BB" nsenter -t "$pid" -m -- "$BB" umount -l "$TARGET" 2>/dev/null || true
  actual=$("$BB" nsenter -t "$pid" -m -- "$BB" sha256sum "$TARGET" 2>/dev/null |
    "$BB" awk '{print $1}')
  [ "$actual" = "$BASE_SHA" ] || log "pid $pid reports hash $actual"
done

settings put secure wmshell_multiwindow_version_code 3 2>/dev/null || true
setprop sys.tb371fc.split_compat rolled_back

# Prevent a crash loop across future boots. The user can re-enable the module.
[ -d "$MODDIR" ] && : >"$MODDIR/disable"
log "rollback complete; module disabled"
exit 0
