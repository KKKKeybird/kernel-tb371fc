#!/system/bin/sh
# mksh closes extra FDs on exec; BusyBox ash preserves the flock FD.
BB=/data/adb/ksu/bin/busybox
[ -x "$BB" ] || exit 1
if [ ! /proc/$$/exe -ef "$BB" ]; then exec "$BB" sh "$0" "$@"; fi
# Kernel-owned advisory lock: reboot/SIGKILL cannot leave a stale directory.
umask 077
mkdir -p /data/adb/zui_color || exit 1
exec 9>/data/adb/zui_color/daemon.lock || exit 1
/system/bin/flock -n 9 || exit 0
trap 'exit 0' HUP INT TERM
# ZUI-semantic colour takeover, EVENT-DRIVEN:
#   logcat -s DisplayFeatureManagerService  is a blocking read; the framework prints
#   "setScreenEffect ..." for every colour/paper setting change, so we react to that
#   instead of polling. Values come from persist.sys.* (mirrored by SystemUI PropObserver).
MODDIR=$(dirname "$0")
LOG=/data/adb/zui_color.log
log(){ echo "$(date '+%m-%d %H:%M:%S') $*" >> "$LOG"; }
last=""
last_rgb=""
last_sat=""

apply(){
  # one getprop fork; `getprop` prints "[key]: [value]" -> two whitespace tokens per line.
  # (mksh `case` with escaped brackets is unreliable here, use parameter expansion.)
  dump=$(getprop | grep -E '^\[persist\.sys\.(color|paper)')
  lvl=; opt=; paper=; plvl=; key=; pending=0
  for tok in $dump; do
    stripped=${tok%"]:"}
    if [ "$stripped" != "$tok" ]; then
      key=${stripped#[}; pending=1; continue
    fi
    if [ "$pending" = 1 ]; then
      val=${tok#[}; val=${val%]}
      case "$key" in
        persist.sys.color.level) lvl=$val ;;
        persist.sys.color.mode) opt=$val ;;
        persist.sys.paper_mode_en) paper=$val ;;
        persist.sys.paper_level) plvl=$val ;;
      esac
      pending=0
    fi
  done
  [ -z "$lvl" ] && lvl=2
  [ -z "$opt" ] && opt=3
  [ -z "$paper" ] && paper=0
  [ -z "$plvl" ] && plvl=50
  cur="$lvl|$opt|$paper|$plvl"
  [ "$cur" = "$last" ] && return
  case "$lvl" in
    1) r=0.9410; g=0.9760; b=1.0000 ;;
    3) r=1.0000; g=0.9570; b=0.9210 ;;
    *) r=1.0000; g=1.0000; b=1.0000 ;;
  esac
  sat=1.00
  [ "$opt" = "1" ] && sat=0.90
  [ "$opt" = "2" ] && sat=1.05
  [ "$opt" = "3" ] && sat=1.22
  if [ "$paper" = "1" ]; then
    b=$(awk -v p="$plvl" 'BEGIN{printf "%.4f", 1-0.55*p/100}')
    g=$(awk -v p="$plvl" 'BEGIN{printf "%.4f", 1-0.15*p/100}')
    sat=0.95
  fi
  # only push what actually changed: each `service call` is ~40-60ms
  key="$r,$g,$b"
  if [ "$key" != "$last_rgb" ]; then
    if ! result=$(service call SurfaceFlinger 1015 i32 1 f $r f 0 f 0 f 0 f 0 f $g f 0 f 0 f 0 f 0 f $b f 0 f 0 f 0 f 0 f 1.0 2>&1); then log "ERROR matrix: $result"; return 1; fi
    case "$result" in *Error*|*Exception*|*Failed*) log "ERROR matrix: $result"; return 1;; esac
    last_rgb=$key
  fi
  if [ "$sat" != "$last_sat" ]; then
    if ! result=$(service call SurfaceFlinger 1022 f $sat 2>&1); then log "ERROR saturation: $result"; return 1; fi
    case "$result" in *Error*|*Exception*|*Failed*) log "ERROR saturation: $result"; return 1;; esac
    last_sat=$sat
  fi
  last="$cur"
  log "apply lvl=$lvl opt=$opt paper=$paper plvl=$plvl -> rgb=$r,$g,$b sat=$sat"
}

until [ "$(getprop sys.boot_completed)" = "1" ]; do sleep 2; done
sleep 5
setprop ctl.stop lwky_color_engine 2>/dev/null
sleep 1
pkill -f lwky_color_engine 2>/dev/null
log "started v0.5 event-driven (logcat source); engine stopped: $(pidof lwky_color_engine 2>/dev/null || echo none)"
apply

while true; do
  { [ -f "$MODDIR/disable" ] || [ -f "$MODDIR/remove" ]; } && { log "disable flag, exit"; exit 0; }
  logcat -b system -v brief -s DisplayFeatureManagerService 2>/dev/null | while read -r line; do
    case "$line" in
      *setScreenEffect*) apply ;;
    esac
  done
  log "logcat stream ended, restarting in 3s"
  /system/bin/sleep 3 9>&-
  last=; last_rgb=; last_sat=
  apply
done
