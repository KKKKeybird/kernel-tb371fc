#!/system/bin/sh
set -eu
MODDIR=${0%/*}
. "$MODDIR/gauge.sh"
R=/dev/tb371fc_battery_stats
LOG=/data/adb/tb371fc-battery-stats.log
# Lenovo's official typical capacity; retained as the Android design baseline.
# The raw 0x3c value remains available for sample validation and diagnostics.
DESIGN_BASELINE_MAH=10200
owned_mount() {
    awk -v t="$1" -v r="/tb371fc_battery_stats/$2" \
        '$5 == t && $4 == r && / - tmpfs / { found=1 } END { exit !found }' /proc/self/mountinfo
}
rollback() {
    failed=0
    for supply in battery bms; do
        for property in charge_counter charge_full_design charge_full; do
            target=$(readlink -f "/sys/class/power_supply/$supply/$property")
            if owned_mount "$target" "$property"; then
                if ! umount "$target"; then failed=1; fi
            fi
        done
    done
    [ "$failed" = 0 ] || return 1
}
active_pid() {
    [ -r "$R/pid" ] || return 1
    pid=$(cat "$R/pid")
    case "$pid" in ''|*[!0-9]*) return 1;; esac
    [ -r "/proc/$pid/cmdline" ] || return 1
    tr '\000' '\n' < "/proc/$pid/cmdline" | grep -E '/tb371fc_battery_stats/daemon[.]sh$' >/dev/null
}
if [ "${1:-}" = --stop ]; then
    if active_pid; then
        kill -TERM "$pid"
        for i in 1 2 3 4 5 6; do
            [ -d "$R" ] || exit 0
            sleep 1
        done
        # Never remove files while the daemon might still publish to them.
        active_pid && exit 1
    fi
    rollback
    rm -rf "$R"
    exit 0
fi
[ "$(cat /data/vendor/tb371fc/build-id 2>/dev/null)" = TB371FC-HyperOS3-20261103-full ] || exit 1
[ "$(uname -r)" = 4.19.325-perf++ ] || exit 1
[ "$(tr -d '\000' < /sys/class/power_supply/bms/device/of_node/compatible)" = mitsumi,mm8013c06 ] || exit 1
[ ! -e "$MODDIR/disable" ] && [ ! -e "$MODDIR/remove" ] || exit 0
mkdir -m 700 "$R.starting" 2>/dev/null || exit 0
owns_runtime=0
cleanup() {
    trap - EXIT HUP INT TERM
    rmdir "$R.starting" 2>/dev/null || true
    [ "$owns_runtime" = 1 ] || return 0
    if rollback; then
        rm -rf "$R"
        echo "$(date) stopped; original sysfs capacity restored" >> "$LOG"
    else
        echo "$(date) ROLLBACK FAILED; retain $R for manual recovery" >> "$LOG"
    fi
}
trap cleanup EXIT
trap 'exit 0' HUP INT TERM
if [ -d "$R" ]; then
    active_pid && exit 0
    rollback
    rm -rf "$R"
fi
# mkdir is also the instance lock. A simultaneous second start exits here.
mkdir -m 700 "$R" || exit 1
owns_runtime=1
echo $$ > "$R/pid"
publish() {
    # Keep inode and length stable without whitespace: Xiaomi uses readLine()
    # then Integer.parseInt() without trim(). Decimal leading zeros work with
    # that reader, Android health, and hvdcp_opti's explicit strtol(base=10).
    # Validated capacity limits keep all values within these eight digits.
    printf '%08d\n' "$((remaining * 1000))" 1<>"$R/charge_counter"
    printf '%08d\n' "$((full * 1000))" 1<>"$R/charge_full"
    printf '%08d\n' "$((DESIGN_BASELINE_MAH * 1000))" 1<>"$R/charge_full_design"
    printf 'time=%s remaining_mah=%s full_mah=%s design_mah=%s gauge_design_mah=%s soc=%s\n' \
        "$(date +%s)" "$remaining" "$full" "$DESIGN_BASELINE_MAH" "$design" "$soc" > "$R/status.tmp"
    mv "$R/status.tmp" "$R/status"
}
read_sample || { echo 'Initial sample rejected' > "$LOG"; exit 1; }
publish
chmod 444 "$R"/charge_*
chcon u:object_r:vendor_sysfs_battery_supply:s0 "$R"/charge_*
# World-readable backing files are needed by the health HAL, not only root.
chmod 755 "$R"
for supply in bms battery; do
    for property in charge_full charge_full_design charge_counter; do
        target=$(readlink -f "/sys/class/power_supply/$supply/$property")
        [ -f "$target" ] || exit 1
        if awk -v t="$target" '$5 == t { found=1 } END { exit !found }' /proc/self/mountinfo; then
            echo "Existing mount: $target; refusing" > "$LOG"
            exit 1
        fi
        mount -o bind "$R/$property" "$target"
    done
done
rmdir "$R.starting"
echo "$(date) running pid=$$ interval=30s; only capacity statistics overridden" > "$LOG"
failures=0
while [ ! -e "$MODDIR/disable" ] && [ ! -e "$MODDIR/remove" ]; do
    if read_sample; then
        failures=0
        publish
    else
        failures=$((failures + 1))
        [ "$failures" -lt 3 ] || { echo 'Three invalid samples; reverting' >> "$LOG"; exit 1; }
    fi
    sleep 30 &
    wait $! || true
done
