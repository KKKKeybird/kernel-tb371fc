#!/system/bin/sh
MODDIR=${0%/*}
LOCK="$MODDIR/supervisor.lock"
LOG="$MODDIR/supervisor.log"

log() {
  printf '%s %s\n' "$(date '+%F %T')" "$*" >>"$LOG"
}

if ! mkdir "$LOCK" 2>/dev/null; then
  old=$(cat "$LOCK/pid" 2>/dev/null)
  case "$old" in
    ''|*[!0-9]*)
      log "recovering stale lock with no valid pid: ${old:-empty}"
      ;;
    *)
      if [ -r "/proc/$old/cmdline" ] &&
         tr '\000' ' ' < "/proc/$old/cmdline" | grep -Fq "$MODDIR/supervisor.sh"; then
        log "supervisor already active as pid $old"
        exit 0
      fi
      log "recovering stale lock for dead pid $old"
      ;;
  esac
  rm -f "$LOCK/pid"
  rmdir "$LOCK" 2>/dev/null || {
    log "cannot remove stale lock directory"
    exit 0
  }
  mkdir "$LOCK" 2>/dev/null || {
    log "lost lock recovery race"
    exit 0
  }
fi

echo $$ > "$LOCK/pid"
child=
cleanup() {
  trap - EXIT HUP INT TERM
  [ -z "$child" ] || kill -TERM "$child" 2>/dev/null
  rm -f "$LOCK/pid"
  rmdir "$LOCK" 2>/dev/null
}
trap cleanup EXIT
trap 'exit 0' HUP INT TERM
while [ "$(getprop sys.boot_completed)" != 1 ]; do sleep 3; done
[ -f "$MODDIR/original-first-connect" ] || settings get system stylus_first_connect > "$MODDIR/original-first-connect"
settings put system stylus_first_connect 1
[ -x "$MODDIR/pen-relay" ] || exit 1
while [ ! -e "$MODDIR/disable" ] && [ ! -e "$MODDIR/remove" ]; do
  size=$(wc -c < "$MODDIR/bridge.log" 2>/dev/null)
  [ "${size:-0}" -lt 262144 ] || mv "$MODDIR/bridge.log" "$MODDIR/bridge.previous.log"
  echo "SESSION $(date -u +%Y-%m-%dT%H:%M:%SZ)" >> "$MODDIR/bridge.log"
  CLASSPATH="$MODDIR/pen-bridge.jar" /system/bin/app_process /system/bin PenBridge "$MODDIR" >> "$MODDIR/bridge.log" 2>&1 &
  child=$!
  echo "$child" > "$MODDIR/bridge.pid"
  wait "$child"
  rc=$?
  child=
  log "bridge exited rc=$rc; restarting"
  sleep 3
done
