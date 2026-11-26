#!/system/bin/sh
set -u

MODULE=${TB371FC_KERNEL_MODULE:-/data/adb/modules/tb371fc_integrated}
STATE=${TB371FC_KERNEL_STATE:-/data/adb/tb371fc-integrated/kernel-auto}
REBOOT=${TB371FC_KERNEL_REBOOT:-reboot}
EXPECTED_VERSION=$(cat "$STATE/request-version" 2>/dev/null || true)

mkdir -p "$STATE" || exit 1
exec >>"$STATE/status.log" 2>&1

publish() {
    printf 'time=%s state=%s detail=%s\n' \
        "$(date '+%F %T')" "$1" "${2:-}" > "$STATE/status.tmp"
    mv "$STATE/status.tmp" "$STATE/status"
}

[ -n "$EXPECTED_VERSION" ] || {
    publish failed missing_expected_version
    exit 1
}

publish waiting_for_install "$EXPECTED_VERSION"
n=0
while [ "$n" -lt 180 ]; do
    installed=
    if [ -f "$MODULE/module.prop" ] &&
       [ ! -e "$MODULE/disable" ] &&
       [ ! -e "$MODULE/remove" ]; then
        installed=$(sed -n 's/^version=//p' "$MODULE/module.prop" | head -1)
    fi
    [ "$installed" = "$EXPECTED_VERSION" ] && break
    sleep 2
    n=$((n + 1))
done
[ "$installed" = "$EXPECTED_VERSION" ] || {
    publish failed install_timeout
    exit 1
}

slot=${TB371FC_KERNEL_SLOT:-$(getprop ro.boot.slot_suffix)}
case "$slot" in _a|_b) ;; *) publish failed invalid_slot; exit 1;; esac
target=${TB371FC_KERNEL_TARGET:-/dev/block/by-name/boot$slot}
expected=${TB371FC_KERNEL_EXPECTED_SHA:-6bec20e4f3ca4ae2dc2d47ed2a55f1f873f6d1f689ccda8ca13ce6081896af64}
current=$(sha256sum "$target" | awk '{print $1}')
if [ "$current" = "$expected" ]; then
    publish already_current "$current"
    exit 0
fi

publish installing_kernel "$EXPECTED_VERSION"
sh "$MODULE/run-kernel.sh" --auto
result=$?
case "$result" in
    0) ;;
    10) publish already_current "$expected"; exit 0;;
    *) publish failed kernel_update; exit 1;;
esac
current=$(sha256sum "$target" | awk '{print $1}')
[ "$current" = "$expected" ] && [ "${TB371FC_KERNEL_SLOT:-$(getprop ro.boot.slot_suffix)}" = "$slot" ] || {
    publish failed post_write_verification
    exit 1
}

publish reboot_scheduled "$current"
sync
sleep 2
"$REBOOT"
