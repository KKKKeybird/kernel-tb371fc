#!/system/bin/sh
MODDIR=${0%/*}
LOG="$MODDIR/service.log"

log() { printf '%s %s\n' "$(date '+%F %T')" "$*" >> "$LOG"; }

recover_lock() {
  lock=$1
  needle=$2
  [ -d "$lock" ] || return 0
  pid=$(cat "$lock/pid" 2>/dev/null)
  case "$pid" in
    ''|*[!0-9]*) ;;
    *)
      if [ -r "/proc/$pid/cmdline" ] &&
         tr '\000' ' ' < "/proc/$pid/cmdline" | grep -Fq "$needle"; then
        return 1
      fi
      ;;
  esac
  rm -f "$lock/pid"
  rmdir "$lock" 2>/dev/null && log "recovered stale lock $lock"
  return 0
}

until [ "$(getprop sys.boot_completed)" = 1 ]; do sleep 2; done
[ -e "$MODDIR/disable" ] && exit 0
[ -e "$MODDIR/remove" ] && exit 0

# The ROM service may already have attempted the old implementation before
# module service stage. Restart it against the overlaid files.
stop tb371fc-speaker-rotation 2>/dev/null
sleep 1
recover_lock /data/vendor/tb371fc/speaker_rotation/supervisor.lock \
  /data/vendor/tb371fc/speaker_rotation/supervisor.sh || true
start tb371fc-speaker-rotation 2>/dev/null
sleep 3

# Do not disturb a healthy cover policy. Only recover and start it when the
# init service is already stopped.
if [ "$(getprop init.svc.tb371fc-hall-cover)" != running ]; then
  recover_lock /data/vendor/tb371fc/hall_cover/policy.lock \
    /data/vendor/tb371fc/hall_cover/service.sh || true
  start tb371fc-hall-cover 2>/dev/null
fi

log "speaker=$(getprop init.svc.tb371fc-speaker-rotation) hall=$(getprop init.svc.tb371fc-hall-cover) kernel=$(uname -r)"
