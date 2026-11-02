#!/system/bin/sh
# mksh closes extra FDs on exec; BusyBox ash preserves the flock FD.
BB=/data/adb/ksu/bin/busybox
[ -x "$BB" ] || exit 1
if [ ! /proc/$$/exe -ef "$BB" ]; then exec "$BB" sh "$0" "$@"; fi
MODDIR=${0%/*}
DIR=/data/adb/scene
TM=/sys/class/thermal/thermal_message
mkdir -p "$DIR" || exit 1
umask 077
exec 9>"$DIR/daemon.lock" || exit 1
/system/bin/flock -n 9 || exit 0
. "$MODDIR/scene-lib.sh" || exit 1
LOG=$DIR/scene.log
log() { echo "$(date '+%m-%d %H:%M:%S') $*" >> "$LOG"; }
cap() {
  echo "cpu0 $1" > "$TM/cpu_limits" &&
  echo "cpu4 $2" > "$TM/cpu_limits" &&
  echo "cpu7 $3" > "$TM/cpu_limits"
}
restore() {
  cap "$(cat /sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq)" \
      "$(cat /sys/devices/system/cpu/cpu4/cpufreq/cpuinfo_max_freq)" \
      "$(cat /sys/devices/system/cpu/cpu7/cpufreq/cpuinfo_max_freq)"
}
cleanup() { trap - EXIT HUP INT TERM; restore; }
trap cleanup EXIT
trap 'exit 0' HUP INT TERM
detect() {
  app=$(dumpsys activity activities 9>&- 2>/dev/null | scene_foreground)
  if dumpsys telephony.registry 9>&- 2>/dev/null | grep -q 'mCallState=[12]'; then
    echo "8 call:$app"; return
  fi
  if dumpsys media.camera 9>&- 2>/dev/null | grep -qE 'Device [^ ]+ is open'; then
    echo "12 camera:$app"; return
  fi
  echo "$(scene_for_app "$app") app:$app"
}
apply() {
  case "$1" in
    1) cap 1804800 2054400 2745600 ;;
    2) cap 1804800 2054400 2361600 ;;
    3) cap 1075200 1478400 1862400 ;;
    *) restore ;;
  esac
}
last_scene=; last_tier=-1; bad_temp=0
while [ ! -f "$DIR/stop" ] && [ ! -f "$MODDIR/disable" ] && [ ! -f "$MODDIR/remove" ]; do
  if [ -r /sys/class/drm/card0-DSI-1/dpms ] &&
     [ "$(cat /sys/class/drm/card0-DSI-1/dpms)" = Off ]; then
    /system/bin/sleep 20 9>&-; continue
  fi
  line=$(detect); scene=${line%% *}
  t=$(cat "$TM/board_sensor_temp" 2>/dev/null)
  valid=1
  case "$t" in ''|*[!0-9-]*) valid=0;; esac
  [ "$valid" = 0 ] || { [ "$t" -ge -40000 ] && [ "$t" -le 150000 ]; } 2>/dev/null || valid=0
  if [ "$valid" = 0 ]; then
    [ "$bad_temp" = 1 ] || log 'ERROR temperature unavailable; retaining last cap'
    bad_temp=1; /system/bin/sleep 6 9>&-; continue
  fi
  bad_temp=0
  set -- $(scene_thresholds "$scene")
  prev=$last_tier
  [ "$scene" = "$last_scene" ] || prev=0
  tier=$(scene_tier "$prev" "$t" "$1" "$2" "$3")
  if [ "$scene" != "$last_scene" ]; then
    if echo "$scene" > "$TM/sconfig"; then
      log "scene=$scene (${line#* })"; last_scene=$scene
    else
      log "ERROR sconfig=$scene"; /system/bin/sleep 6 9>&-; continue
    fi
  fi
  if [ "$tier" != "$last_tier" ]; then
    if apply "$tier"; then
      log "policy scene=$scene tier=$tier temp=$t thr=$1/$2/$3"; last_tier=$tier
    else
      log "ERROR applying tier=$tier (will retry)"
    fi
  fi
  /system/bin/sleep 6 9>&-
done
