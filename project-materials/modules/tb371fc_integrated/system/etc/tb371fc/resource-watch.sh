#!/system/bin/sh
# Initialize WPS on app resume. Droidspaces containers are created by the user.
set -u
ROOT=/system/etc/tb371fc
STATE=/data/vendor/tb371fc
BB=$ROOT/root/busybox
# Preserve the flock FD across exec on Android mksh.
if [ ! /proc/$$/exe -ef "$BB" ]; then exec "$BB" sh "$0" "$@"; fi
mkdir -p "$STATE/resources"
exec >>"$STATE/resources/events.log" 2>&1
exec 9>"$STATE/resources/watch.lock"
"$BB" flock -n 9 || exit 0
until [ "$(getprop sys.user.0.ce_available)" = true ]; do sleep 2; done
initialize_wps(){
 sh "$ROOT/wps/initialize-assets.sh" 9>&- >>"$STATE/resources/wps-init.log" 2>&1 &
}
# Cover an app opened before the watcher started, then block on logd events.
ps -A | grep -q '[c]om.xiaomi.wpslauncher' && initialize_wps
while :; do
 logcat -b events -v brief -s wm_on_resume_called wm_resume_activity am_on_resume_called 9>&- | while IFS= read -r event; do
  case "$event" in
   *com.xiaomi.wpslauncher*) initialize_wps;;
  esac
 done
 sleep 3 9>&-
done
