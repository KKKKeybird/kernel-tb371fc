#!/system/bin/sh
MODDIR=${0%/*}
# Stop the restart owner before its child, including direct uninstall invocation.
touch "$MODDIR/remove"
if [ -r "$MODDIR/supervisor.lock/pid" ]; then
 supervisorpid=$(cat "$MODDIR/supervisor.lock/pid")
 case "$supervisorpid" in ''|*[!0-9]*) ;; *)
  if [ -r "/proc/$supervisorpid/cmdline" ] && tr '\000' ' ' < "/proc/$supervisorpid/cmdline" | grep -Fq "$MODDIR/supervisor.sh"; then kill -TERM "$supervisorpid"; fi
 esac
fi
if [ -r "$MODDIR/bridge.pid" ]; then
  bridgepid=$(cat "$MODDIR/bridge.pid")
  case "$bridgepid" in ''|*[!0-9]*) ;; *)
    if [ -r "/proc/$bridgepid/cmdline" ] && tr '\000' ' ' < "/proc/$bridgepid/cmdline" | grep -Fq 'PenBridge'; then kill "$bridgepid"; fi
  esac
fi
if [ -f "$MODDIR/original-first-connect" ]; then
  original=$(cat "$MODDIR/original-first-connect")
  case "$original" in null|'') settings delete system stylus_first_connect ;; *) settings put system stylus_first_connect "$original" ;; esac
fi
