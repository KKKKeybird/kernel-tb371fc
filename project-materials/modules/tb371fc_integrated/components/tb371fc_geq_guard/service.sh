#!/system/bin/sh
BB=/data/adb/ksu/bin/busybox
[ -x "$BB" ] || exit 1
if [ ! /proc/$$/exe -ef "$BB" ]; then exec "$BB" sh "$0" "$@"; fi
M=$(readlink -f "${0%/*}")
OWNER=$(readlink -f "$M/../..")
case "$OWNER" in /data/adb/modules/tb371fc_integrated) ;; *) OWNER="$M";; esac
R=/dev/tb371fc_geq_guard
CACHE=/data/adb/tb371fc-integrated/geq-guard
APK=/product/app/MiSound/MiSound.apk
BASE_SHA=$(cat "$M/base-apk.sha256")
DEX_SHA=$(cat "$M/payload/classes.sha256")
umask 077
mkdir -p "$R" "$CACHE" || exit 1
if [ "${1:-}" = --stop ]; then
    touch "$R/stop"
    exit 0
fi
exec 9>"$R/supervisor.lock"
"$BB" flock -n 9 || exit 0
[ ! -e "$M/disable" ] && [ ! -e "$M/remove" ] &&
    [ ! -e "$OWNER/disable" ] && [ ! -e "$OWNER/remove" ] || exit 0
[ "$(cat /data/vendor/tb371fc/build-id 2>/dev/null)" = TB371FC-HyperOS3-20261103-full ] || exit 1
[ -f "$APK" ] && [ -f "$M/payload/classes.dex" ] && [ -f "$M/apk-patcher.jar" ] || exit 1
[ "$("$BB" sha256sum "$APK" | "$BB" awk '{print $1}')" = "$BASE_SHA" ] || {
    echo base_mismatch > "$R/status"
    exit 1
}
[ "$("$BB" sha256sum "$M/payload/classes.dex" | "$BB" awk '{print $1}')" = "$DEX_SHA" ] || exit 1

rm -f "$R/stop"
child=
cleanup() {
    trap - EXIT HUP INT TERM
    [ -z "$child" ] || { kill -TERM "$child" 2>/dev/null; wait "$child" 2>/dev/null; }
    if sh "$M/rollback.sh"; then
        rm -f "$R/stop" "$R/status" "$R/generation.log" "$R/owned-inode" \
            "$R/namespaces" "$R/supervisor.pid" "$R/supervisor.lock"
        rmdir "$R" 2>/dev/null || true
    else
        rm -f "$R/supervisor.pid"
    fi
}
trap cleanup EXIT
trap 'exit 0' HUP INT TERM
echo "$$" > "$R/supervisor.pid"
echo generating > "$R/status"
CLASSPATH="$M/apk-patcher.jar" /system/bin/app_process /system/bin MisoundApkPatch \
    "$APK" "$M/payload/classes.dex" "$CACHE/MiSound.apk" 9>&- > "$R/generation.log" 2>&1 ||
    exit 1
generated_dex=$("$BB" unzip -p "$CACHE/MiSound.apk" classes.dex |
    "$BB" sha256sum | "$BB" awk '{print $1}')
[ "$generated_dex" = "$DEX_SHA" ] || exit 1
chmod 0644 "$CACHE/MiSound.apk" || exit 1
chcon u:object_r:system_file:s0 "$CACHE/MiSound.apk" || exit 1
stat -c '%d:%i' "$CACHE/MiSound.apk" > "$R/owned-inode"
: > "$R/namespaces"
for pid in 1 $("$BB" pidof zygote64) $("$BB" pidof zygote); do
    [ -d "/proc/$pid" ] || continue
    namespace=$(readlink "/proc/$pid/ns/mnt")
    printf '%s %s\n' "$pid" "$namespace" >> "$R/namespaces"
    current=$("$BB" nsenter -t "$pid" -m -- stat -c '%d:%i' "$APK")
    if [ "$current" != "$(cat "$R/owned-inode")" ]; then
        [ "$("$BB" nsenter -t "$pid" -m -- sha256sum "$APK" | "$BB" awk '{print $1}')" = "$BASE_SHA" ] ||
            exit 1
        "$BB" nsenter -t "$pid" -m -- "$BB" mount -o bind,ro "$CACHE/MiSound.apk" "$APK" ||
            exit 1
    fi
    [ "$("$BB" nsenter -t "$pid" -m -- stat -c '%d:%i' "$APK")" = "$(cat "$R/owned-inode")" ] ||
        exit 1
done
am force-stop --user 0 com.miui.misound >/dev/null 2>&1 || true
setprop sys.tb371fc.geq_guard ready
echo ready > "$R/status"
"$BB" inotifyd "$M/on-event.sh" "$OWNER:nyDMx" "$M:nyDMx" "$R:nyDMx" 9>&- &
child=$!
sh "$M/on-event.sh"
wait "$child"
