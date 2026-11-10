#!/system/bin/sh
set -eu
R=/dev/tb371fc_pps_temp_probe
TARGET=/vendor/bin/hvdcp_opti
ORIGINAL=f3175c83a4f27f7f2e99ccc19a3ea893c1cc15dda20a8881073f5ed4a2c2db8a
if [ ! -d "$R" ]; then
    echo 'No PPS experiment is active.'
    sha256sum "$TARGET"
    exit 0
fi
[ "$(readlink /proc/self/ns/mnt)" = "$(readlink /proc/1/ns/mnt)" ] || {
    echo 'Run with su -M in the global mount namespace'; exit 1;
}
if [ -f "$R/pid" ]; then
    pid=$(cat "$R/pid")
    case "$pid" in ''|*[!0-9]*) echo 'Invalid experiment PID'; exit 1;; esac
    if [ "$pid" != "${PPS_PROBE_CLEANUP_PID:-}" ] && [ -r "/proc/$pid/cmdline" ] &&
       tr '\000' '\n' < "/proc/$pid/cmdline" | grep -E '/(tb371fc-pps-temp-v3|tb371fc_pps_temp_test)/run-test[.]sh$' >/dev/null; then
        kill -TERM "$pid"
        for i in 1 2 3 4 5; do
            [ -d "$R" ] || exit 0
            sleep 1
        done
        if [ -r "/proc/$pid/cmdline" ] &&
           tr '\000' '\n' < "/proc/$pid/cmdline" | grep -E '/(tb371fc-pps-temp-v3|tb371fc_pps_temp_test)/run-test[.]sh$' >/dev/null; then
            echo 'Experiment is still stopping; preserving its files'; exit 1
        fi
    fi
fi
[ -f "$R/original-log" ] || { echo 'Missing rollback journal'; exit 1; }
if awk -v t="$TARGET" '$5==t && $4=="/tb371fc_pps_temp_probe/hvdcp_opti" && / - tmpfs / {f=1} END {exit !f}' /proc/self/mountinfo; then
    own_mount=1
elif [ "$(sha256sum "$TARGET" | cut -d ' ' -f 1)" != "$ORIGINAL" ]; then
    echo 'Unexpected binary or foreign mount; refusing to touch it'; exit 1
else
    own_mount=0
fi
setprop ctl.stop vendor.hvdcp_opti
sleep 1
[ "$own_mount" = 0 ] || umount "$TARGET"
old_log=$(cat "$R/original-log")
if [ -n "$old_log" ]; then
    setprop persist.vendor.cp.log_level "$old_log"
else
    /data/adb/ksu/bin/resetprop -p --delete persist.vendor.cp.log_level
fi
setprop ctl.start vendor.hvdcp_opti
[ "$(sha256sum "$TARGET" | cut -d ' ' -f 1)" = "$ORIGINAL" ] || exit 1
rm -rf "$R"
echo 'RESTORED: original hvdcp_opti and log property; no experiment mount remains.'
