#!/system/bin/sh
BB=/data/adb/ksu/bin/busybox
[ -x "$BB" ] || exit 1
if [ ! /proc/$$/exe -ef "$BB" ]; then exec "$BB" sh "$0" "$@"; fi
M=$(readlink -f "${0%/*}")
OWNER=$(readlink -f "$M/../..")
R=/dev/tb371fc_pen_icon
CACHE=/data/adb/tb371fc-integrated/pen-icon
APK=/system_ext/priv-app/Settings/Settings.apk
umask 077
mkdir -p "$R" "$CACHE" || exit 1
if [ "${1:-}" = --stop ]; then touch "$R/stop"; exit 0; fi
exec 9>"$R/supervisor.lock"
"$BB" flock -n 9 || exit 0
[ ! -e "$M/disable" ] && [ ! -e "$M/remove" ] && [ ! -e "$OWNER/disable" ] && [ ! -e "$OWNER/remove" ] || exit 0
[ "$(cat /data/vendor/tb371fc/build-id 2>/dev/null)" = TB371FC-HyperOS3-20261103-full ] || exit 1
[ "$(sha256sum "$APK" | cut -d' ' -f1)" = "$(cat "$M/base-apk.sha256")" ] || { echo base_mismatch > "$R/status"; exit 1; }
[ "$(sha256sum "$M/payload/classes6.dex" | cut -d' ' -f1)" = "$(cat "$M/payload/classes6.sha256")" ] || exit 1
rm -f "$R/stop"
child=
cleanup() {
    trap - EXIT HUP INT TERM
    [ -z "$child" ] || { kill -TERM "$child" 2>/dev/null; wait "$child" 2>/dev/null; }
    sh "$M/rollback.sh"
    rm -f "$R/supervisor.pid"
}
trap cleanup EXIT
trap 'exit 0' HUP INT TERM
echo "$$" > "$R/supervisor.pid"
echo generating > "$R/status"
CLASSPATH="$M/apk-patcher.jar" /system/bin/app_process /system/bin ApkPatch "$APK" "$M/payload/classes6.dex" "$CACHE/Settings.apk" 9>&- > "$R/generation.log" 2>&1 || exit 1
chmod 0644 "$CACHE/Settings.apk" || exit 1
chcon u:object_r:system_file:s0 "$CACHE/Settings.apk" || exit 1
stat -c '%d:%i' "$CACHE/Settings.apk" > "$R/owned-inode"
: > "$R/namespaces"
for pid in 1 $("$BB" pidof zygote64) $("$BB" pidof zygote); do
    [ -d "/proc/$pid" ] || continue
    namespace=$(readlink "/proc/$pid/ns/mnt")
    printf '%s %s\n' "$pid" "$namespace" >> "$R/namespaces"
    current=$("$BB" nsenter -t "$pid" -m -- stat -c '%d:%i' "$APK")
    if [ "$current" != "$(cat "$R/owned-inode")" ]; then
        [ "$("$BB" nsenter -t "$pid" -m -- sha256sum "$APK" | cut -d' ' -f1)" = "$(cat "$M/base-apk.sha256")" ] || exit 1
        "$BB" nsenter -t "$pid" -m -- "$BB" mount --bind "$CACHE/Settings.apk" "$APK" || exit 1
    fi
    [ "$("$BB" nsenter -t "$pid" -m -- stat -c '%d:%i' "$APK")" = "$(cat "$R/owned-inode")" ] || exit 1
done
echo ready > "$R/status"
# Wait for file events; no heartbeat, setting polling, or repeated APK generation.
"$BB" inotifyd "$M/on-event.sh" "$OWNER:nyDMx" "$M:nyDMx" "$R:nyDMx" 9>&- &
child=$!
sh "$M/on-event.sh"
wait "$child"
