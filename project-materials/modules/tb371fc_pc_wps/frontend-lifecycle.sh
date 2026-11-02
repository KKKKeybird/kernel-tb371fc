#!/system/bin/sh
set -u
MODDIR=${0%/*}
. "$MODDIR/root-runtime.sh" || exit 1
STATE=/data/adb/tb371fc_pc_wps
PROP=sys.mslg.isalive
PIDFILE=$STATE/run/frontend-watcher.pid
mkdir -p "$STATE/run"
log() { printf '%s %s\n' "$(date '+%F %T')" "$*" >> "$STATE/supervisor.log"; }
previous_pid=$(cat "$PIDFILE" 2>/dev/null)
case "$previous_pid" in ''|*[!0-9]*) previous_pid=0;; esac
if [ "$previous_pid" != 0 ] && kill -0 "$previous_pid" 2>/dev/null \
    && grep -q "$MODDIR/frontend-lifecycle.sh" "/proc/$previous_pid/cmdline" 2>/dev/null; then
  exit 0
fi
cleanup() {
  [ "$(cat "$PIDFILE" 2>/dev/null)" = "$$" ] && rm -f "$PIDFILE"
}
trap cleanup EXIT
trap 'exit 0' INT TERM
echo $$ > "$PIDFILE"

# The WPS client changes this property from true to false when its last PC
# activity is destroyed. resetprop -w blocks in the property service until
# that change occurs; it does not poll processes or dumpsys.
while [ ! -e "$MODDIR/disable" ] && [ ! -e "$MODDIR/remove" ]; do
  previous=$(getprop "$PROP")
  case "$previous" in
    true|false)
      resetprop -w "$PROP" "$previous" || continue
      ;;
    *)
      resetprop -w "$PROP" || continue
      ;;
  esac
  current=$(getprop "$PROP")
  [ "$previous" = true ] && [ "$current" = false ] || continue

  # Match the client's own two-second activity teardown, then reject a rapid
  # reopen with one final state read before stopping the sticky frontend.
  sleep 3
  [ "$(getprop "$PROP")" = false ] || continue
  if dumpsys activity services com.xiaomi.mslgrdp 2>/dev/null | grep -q MultiWindowService; then
    am stopservice -n com.xiaomi.mslgrdp/.multwindow.MultiWindowService >/dev/null 2>&1
    if ! dumpsys activity services com.xiaomi.mslgrdp 2>/dev/null | grep -q MultiWindowService; then
      log 'Stopped Android PC-engine frontend after WPS exit event.'
    else
      log 'Failed to stop Android PC-engine frontend after WPS exit event.'
    fi
  fi
done
