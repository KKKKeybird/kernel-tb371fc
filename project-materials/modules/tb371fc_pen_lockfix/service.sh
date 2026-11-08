#!/system/bin/sh

MODDIR=${0%/*}
BB=/data/adb/ksu/bin/busybox
STATE=/data/vendor/tb371fc/pen_bridge
LOCK=$STATE/supervisor.lock
RUNNING_SUPERVISOR=$STATE/supervisor.sh
SUPERVISOR=/system/etc/tb371fc/core/pen_bridge/supervisor.sh
FIXED_SHA=fa8aa51bd5544f608c4f6b3b63f9e6abf7cca459dfdebe96c8ea5dd6e1b6cd42
LOG=/data/vendor/tb371fc/pen-lockfix.log

mkdir -p /data/vendor/tb371fc
umask 077
exec >>"$LOG" 2>&1
setprop sys.tb371fc.pen_lockfix waiting

n=0
while [ "$(getprop sys.boot_completed)" != 1 ] && [ "$n" -lt 180 ]; do
  sleep 2
  n=$((n + 1))
done
[ "$(getprop sys.boot_completed)" = 1 ] || exit 0

# Do nothing if the overlay or ROM no longer matches this module.
[ "$(getprop ro.product.device)" = sheng ] || exit 0
[ "$(getprop ro.build.version.sdk)" = 35 ] || exit 0
[ "$(cat /data/vendor/tb371fc/build-id 2>/dev/null)" = "TB371FC-HyperOS3-20261103-full" ] || exit 0
[ -x "$BB" ] && [ -f "$SUPERVISOR" ] || exit 1
[ "$("$BB" sha256sum "$SUPERVISOR" | "$BB" awk '{print $1}')" = "$FIXED_SHA" ] ||
  exit 1

old=$(cat "$LOCK/pid" 2>/dev/null)
case "$old" in
  ''|*[!0-9]*)
    rm -f "$LOCK/pid"
    rmdir "$LOCK" 2>/dev/null
    ;;
  *)
    cmdline=$(tr '\000' ' ' <"/proc/$old/cmdline" 2>/dev/null || true)
    case "$cmdline" in
      *"$RUNNING_SUPERVISOR"*|*"$SUPERVISOR"*) ;;
      *)
      rm -f "$LOCK/pid"
      rmdir "$LOCK" 2>/dev/null
      ;;
    esac
    ;;
esac

case "$(getprop init.svc.tb371fc-pen-bridge)" in
  running|starting|restarting)
    setprop sys.tb371fc.pen_lockfix running
    printf '%s bridge already active\n' "$(date '+%F %T')"
    exit 0
    ;;
esac

setprop ctl.start tb371fc-pen-bridge
n=0
while [ "$n" -lt 20 ]; do
  sleep 1
  case "$(getprop init.svc.tb371fc-pen-bridge)" in
    running|starting)
      setprop sys.tb371fc.pen_lockfix running
      printf '%s bridge started\n' "$(date '+%F %T')"
      exit 0
      ;;
  esac
  n=$((n + 1))
done

setprop sys.tb371fc.pen_lockfix failed
printf '%s bridge failed to start\n' "$(date '+%F %T')"
exit 1
