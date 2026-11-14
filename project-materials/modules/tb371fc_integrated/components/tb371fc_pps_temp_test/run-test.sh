#!/system/bin/sh
# Manual, bounded consumer-only experiment. Never run automatically at boot.
set -eu
case "$0" in */*) KIT=${0%/*};; *) KIT=.;; esac
KIT=$(cd "$KIT" && pwd)
R=/dev/tb371fc_pps_temp_probe
TARGET=/vendor/bin/hvdcp_opti
ORIGINAL=f3175c83a4f27f7f2e99ccc19a3ea893c1cc15dda20a8881073f5ed4a2c2db8a
PATCHED=c4f7c8921cef1897fa3bcc5134251a4a35887262c31510da5ae093d35f700285
U=/sys/class/power_supply/usb
C=/sys/class/power_supply/charge_pump_master
B=/sys/class/power_supply/battery
P=/sys/class/usbpd/usbpd0
ready=1
check() { if ! "$@"; then ready=0; fi; }
soc=$(cat "$B/capacity")
temp=$(cat "$B/temp")
active=$(cat "$U/pd_active")
echo "PPS test preflight: pd_active=$active soc=$soc temp_deciC=$temp"
check test "$(id -u)" = 0
check test "$(readlink /proc/self/ns/mnt)" = "$(readlink /proc/1/ns/mnt)"
check test "$(cat /data/vendor/tb371fc/build-id)" = TB371FC-HyperOS3-20261103-full
check test "$(uname -r)" = 4.19.325-perf++
check test "$(sha256sum "$TARGET" | cut -d ' ' -f 1)" = "$ORIGINAL"
check test "$(sha256sum "$KIT/hvdcp_opti.temp-chain-v3-test" | cut -d ' ' -f 1)" = "$PATCHED"
check test ! -d /dev/tb371fc_stock_charge
check test "$(getprop init.svc.vendor.hvdcp_opti)" = running
check test "$(getprop persist.sys.pc_mode)" = 0
check test "$(cat "$B/status")" = Charging
check test "$active" = 2
check test "$soc" -ge 15
check test "$soc" -le 80
check test "$temp" -ge 150
check test "$temp" -le 400
apdo=0
for n in 1 2 3 4 5 6 7; do
    value=$(cat "$P/pdo$n")
    case "$value" in ''|*[!0-9a-fA-F]*) continue;; esac
    if [ "$((0x$value >> 30))" = 3 ] && [ "$(((0x$value >> 28) & 3))" = 0 ]; then apdo=1; fi
done
check test "$apdo" = 1
if [ "$ready" != 1 ]; then
    echo 'NOT READY: requires PPS/APDO, normal charging, SOC15-80%, battery15-40C, exact R8 and matching binaries.'
    exit 2
fi
echo 'READY (experimental; successful charging is not yet established).'
[ "${1:-}" != --check ] || exit 0
[ ! -d "$R" ] || { echo 'Existing experiment; run rollback.sh first'; exit 1; }
if awk -v t="$TARGET" '$5==t {f=1} END {exit !f}' /proc/self/mountinfo; then
    echo 'Existing binary mount; refusing'; exit 1
fi
mkdir -m 700 "$R"
getprop persist.vendor.cp.log_level > "$R/original-log"
echo $$ > "$R/pid"
mkdir -p /data/adb/tb371fc-pps-temp-test/logs
LOG="/data/adb/tb371fc-pps-temp-test/logs/$(date +%Y%m%d-%H%M%S)"
mkdir -m 700 "$LOG"
exec > "$LOG/session.log" 2>&1
echo "$(date) PPS temperature consumer v3 experiment; maximum 120 seconds"
parent=$$
(
    sleep 120
    if [ -f "$R/pid" ] && [ "$(cat "$R/pid")" = "$parent" ]; then
        kill -TERM "$parent"
    fi
) &
watchdog=$!
cleanup() {
    trap - EXIT HUP INT TERM
    kill "$watchdog" 2>/dev/null || true
    export PPS_PROBE_CLEANUP_PID=$$
    if sh "$KIT/rollback.sh"; then
        echo "$(date) rollback verified"
    else
        echo 'ROLLBACK FAILED: preserve journal and run rollback.sh again; reboot also removes the volatile mount.'
    fi
}
trap cleanup EXIT
trap 'exit 0' HUP INT TERM
cp "$KIT/hvdcp_opti.temp-chain-v3-test" "$R/hvdcp_opti"
chmod 755 "$R/hvdcp_opti"
chcon u:object_r:vendor_hvdcp_exec:s0 "$R/hvdcp_opti"
cat "$P/pdo_h" > "$LOG/source-pdos.txt"
setprop ctl.stop vendor.hvdcp_opti
sleep 1
mount -o bind "$R/hvdcp_opti" "$TARGET"
setprop persist.vendor.cp.log_level 7
setprop ctl.start vendor.hvdcp_opti
sleep 2
if command -v strace >/dev/null; then
    timeout -s INT 4 strace -f -p "$(getprop init.svc_debug_pid.vendor.hvdcp_opti)" \
        -e trace=write -s 1400 -yy -o "$LOG/daemon-writes.strace" || true
fi
end=$(( $(date +%s) + 110 ))
while [ "$(date +%s)" -lt "$end" ]; do
    [ "$(getprop init.svc.vendor.hvdcp_opti)" = running ] || exit 1
    [ "$(cat "$U/present")" = 1 ] && [ "$(cat "$U/pd_active")" = 2 ] || exit 1
    [ "$(getprop persist.sys.pc_mode)" = 0 ] || exit 1
    now_temp=$(cat "$B/temp")
    [ "$now_temp" -ge 100 ] && [ "$now_temp" -le 450 ] || exit 1
    [ "$(cat "$B/capacity")" -lt 85 ] || exit 1
    printf '%s' "$(date +%s)"
    # Do not read cp_isns: that read itself switches the shared TEMP mux.
    for node in "$C/cp_enable" "$C/cp_switcher_en" "$C/cp_status1" "$C/cp_status2" "$C/cp_die_temp" "$U/voltage_now" "$U/input_current_now" "$B/current_now" "$B/voltage_now" "$B/temp"; do
        v=$(cat "$node" 2>/dev/null) || v=NA
        printf ' %s=%s' "${node%/*}/${node##*/}" "$v"
    done
    echo
    sleep 3
done
dmesg | grep -E 'CP:|PPS:' | tail -100 > "$LOG/kernel-cp.txt" || true
