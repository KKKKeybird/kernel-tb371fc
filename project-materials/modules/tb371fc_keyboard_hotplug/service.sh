#!/system/bin/sh
MODDIR=${0%/*}
while [ "$(getprop sys.boot_completed)" != 1 ]; do sleep 3; done
if [ -f "$MODDIR/worker.pid" ]; then
  oldpid=$(cat "$MODDIR/worker.pid")
  case "$oldpid" in ''|*[!0-9]*) ;; *)
    if [ -r "/proc/$oldpid/cmdline" ] && tr '\000' ' ' < "/proc/$oldpid/cmdline" | grep -q "$MODDIR/worker.sh"; then exit 0; fi
  esac
fi
/system/bin/sh "$MODDIR/worker.sh" >> "$MODDIR/recovery.log" 2>&1 &
echo $! > "$MODDIR/worker.pid"
