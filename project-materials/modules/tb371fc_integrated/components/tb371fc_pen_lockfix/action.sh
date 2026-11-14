#!/system/bin/sh

BB=/data/adb/ksu/bin/busybox
STATE=/data/vendor/tb371fc/pen_bridge
LOCK=$STATE/supervisor.lock
RUNNING_SUPERVISOR=$STATE/supervisor.sh
SUPERVISOR=/system/etc/tb371fc/core/pen_bridge/supervisor.sh

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
  running|starting|restarting) ;;
  *) setprop ctl.start tb371fc-pen-bridge ;;
esac
sleep 3
echo "service=$(getprop init.svc.tb371fc-pen-bridge)"
cat "$STATE/status.txt" 2>/dev/null || true
