#!/system/bin/sh
M=$(readlink -f "${0%/*}")
R=/dev/tb371fc_pen_icon
APK=/system_ext/priv-app/Settings/Settings.apk
BB=/data/adb/ksu/bin/busybox
inode=$(cat "$R/owned-inode" 2>/dev/null)
[ -n "$inode" ] || exit 0
failed=0
while read -r pid namespace; do
    [ "$(readlink "/proc/$pid/ns/mnt" 2>/dev/null)" = "$namespace" ] || continue
    current=$("$BB" nsenter -t "$pid" -m -- stat -c '%d:%i' "$APK" 2>/dev/null)
    [ "$current" = "$inode" ] || continue
    "$BB" nsenter -t "$pid" -m -- "$BB" umount -l "$APK" || failed=1
done < "$R/namespaces"
if [ "$failed" = 0 ]; then
    echo stopped > "$R/status"
    rm -f "$R/owned-inode" "$R/namespaces"
fi
exit "$failed"
