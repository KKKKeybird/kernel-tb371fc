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

publish installing_kernel "$EXPECTED_VERSION"
if ! sh "$MODULE/run-kernel.sh" --auto; then
    publish failed kernel_update
    exit 1
fi

slot=${TB371FC_KERNEL_SLOT:-$(getprop ro.boot.slot_suffix)}
target=${TB371FC_KERNEL_TARGET:-/dev/block/by-name/boot$slot}
expected=${TB371FC_KERNEL_EXPECTED_SHA:-a9fb2b73e43e8e8bb1b7a8456bf772e6729b68c110ed57bf7f2c1d1cf417b981}
current=$(sha256sum "$target" | awk '{print $1}')
[ "$current" = "$expected" ] || {
    publish failed post_write_verification
    exit 1
}

publish reboot_scheduled "$current"
sync
sleep 2
"$REBOOT"
