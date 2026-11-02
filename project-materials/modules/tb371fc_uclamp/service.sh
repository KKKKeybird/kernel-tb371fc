#!/system/bin/sh
# mksh closes extra FDs on exec; BusyBox ash preserves the flock FD.
BB=/data/adb/ksu/bin/busybox
[ -x "$BB" ] || exit 1
if [ ! /proc/$$/exe -ef "$BB" ]; then exec "$BB" sh "$0" "$@"; fi
MODDIR=${0%/*}
DIR=/data/adb/uclamp
umask 077
mkdir -p "$DIR" || exit 1
exec 9>"$DIR/daemon.lock" || exit 1
/system/bin/flock -n 9 || exit 0
log() { echo "$(date '+%F %T') $*" >> "$DIR/log.txt"; }
trap 'exit 0' HUP INT TERM
until [ "$(getprop sys.boot_completed)" = 1 ]; do /system/bin/sleep 2 9>&-; done
F=/dev/cpuctl/top-app/cpu.uclamp.min
while [ ! -f "$MODDIR/disable" ] && [ ! -f "$MODDIR/remove" ]; do
  cur=$(cat "$F" 2>/dev/null)
  case "$cur" in
    25|25.00) ;;
    *)
      if [ -w "$F" ] && echo 25 > "$F"; then
        after=$(cat "$F")
        case "$after" in 25|25.00) log "verified top-app min=$after (was $cur)";; *) log "ERROR readback=$after";; esac
      else log "ERROR $F not writable"; fi
      ;;
  esac
  /system/bin/sleep 300 9>&-
done
