#!/system/bin/sh
# mksh closes extra FDs on exec; BusyBox ash preserves the flock FD.
BB=/data/adb/ksu/bin/busybox
[ -x "$BB" ] || exit 1
if [ ! /proc/$$/exe -ef "$BB" ]; then exec "$BB" sh "$0" "$@"; fi
MODDIR=${0%/*}
DIR=/data/adb/scene
umask 077
mkdir -p "$DIR" || exit 1
exec 9>"$DIR/supervisor.lock" || exit 1
/system/bin/flock -n 9 || exit 0
child=
cleanup() {
  trap - EXIT HUP INT TERM
  [ -z "$child" ] || { kill -TERM "$child" 2>/dev/null; wait "$child"; }
}
trap cleanup EXIT
trap 'exit 0' HUP INT TERM
until [ "$(getprop sys.boot_completed)" = 1 ]; do /system/bin/sleep 2 9>&-; done
while [ ! -f "$MODDIR/disable" ] && [ ! -f "$MODDIR/remove" ] && [ ! -f "$DIR/stop" ]; do
  sh "$MODDIR/scene.sh" 9>&- >> "$DIR/scene.out" 2>&1 &
  child=$!; wait "$child"; child=
  /system/bin/sleep 5 9>&-
done
