#!/system/bin/sh
# mksh closes extra FDs on exec; BusyBox ash preserves the flock FD.
BB=/data/adb/ksu/bin/busybox
[ -x "$BB" ] || exit 1
if [ ! /proc/$$/exe -ef "$BB" ]; then exec "$BB" sh "$0" "$@"; fi
MODDIR=${0%/*}
DIR=/data/adb/finddevice_off
umask 077
mkdir -p "$DIR" || exit 1
exec 9>"$DIR/daemon.lock" || exit 1
/system/bin/flock -n 9 || exit 0
log() { echo "$(date '+%F %T') $*" >> "$DIR/stop.log"; }
trap 'exit 0' HUP INT TERM
until [ "$(getprop sys.boot_completed)" = 1 ]; do /system/bin/sleep 2 9>&-; done
while [ ! -f "$MODDIR/disable" ] && [ ! -f "$MODDIR/remove" ]; do
  if [ -n "$(pidof com.xiaomi.finddevice)" ]; then
    if am force-stop com.xiaomi.finddevice 9>&- 2>/dev/null; then
      log 'force-stop accepted (periodic suppression, not permanent disable)'
    else log 'ERROR force-stop rejected'; fi
  fi
  /system/bin/sleep 60 9>&-
done
