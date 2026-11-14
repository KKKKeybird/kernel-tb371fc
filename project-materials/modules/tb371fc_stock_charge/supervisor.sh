#!/system/bin/sh
MODDIR=${0%/*}
R=/dev/tb371fc_stock_charge
B=/sys/class/power_supply/battery
LOG=/data/adb/tb371fc-stock-charge.log
legacy=/system/lwky/lwky_smart_charge_hyper
[ "$(cat /data/vendor/tb371fc/build-id 2>/dev/null)" = TB371FC-HyperOS3-20261103-full ] || exit 1
case "$(uname -v)" in "#25 SMP PREEMPT "*) ;; *) exit 1;; esac
[ "$(uname -r)" = 4.19.325-perf++ ] || exit 1
[ "$(sha256sum "$legacy" | cut -d' ' -f1)" = b4f4742d0924839958f52d8b1cd6068123b1408f0581728eb66222345746efe8 ] || exit 1
[ ! -f "$MODDIR/disable" ] && [ ! -f "$MODDIR/remove" ] || exit 0
# Do not steal a live instance's lock or touch its vote.
if [ -d "$R" ]; then
    p=$(cat "$R/pid" 2>/dev/null)
    case "$p" in ''|*[!0-9]*) p=0;; esac
    if [ "$p" != 0 ] && [ -r "/proc/$p/cmdline" ] && tr '\000' '\n' < "/proc/$p/cmdline" | grep -F "$MODDIR/supervisor.sh" >/dev/null; then exit 0; fi
    orphan=$(cat "$R/child" 2>/dev/null)
    case "$orphan" in ''|*[!0-9]*) orphan=0;; esac
    if [ "$orphan" != 0 ] && [ -r "/proc/$orphan/cmdline" ] && tr '\000' '\n' < "/proc/$orphan/cmdline" | grep -F tb371fc-charge-bridge >/dev/null; then
        kill -TERM "$orphan" 2>/dev/null
        sleep 2
        kill -0 "$orphan" 2>/dev/null && exit 1
    fi
    # A stale owned lock is recoverable; bp_enable is an independent USER owner.
    [ "$(cat "$B/bp_enable")" = 0 ] && echo 0 > "$B/charge_disable"
    rm -rf "$R"
fi
mkdir -m 700 "$R" || exit 1
echo $$ > "$R/pid"
child=
owns_vote=0
cleanup() {
    trap - EXIT INT TERM HUP
    if [ -n "$child" ]; then kill -TERM "$child" 2>/dev/null; wait "$child" 2>/dev/null; fi
    if [ "$owns_vote" = 1 ] && [ "$(cat "$B/bp_enable" 2>/dev/null)" = 0 ]; then echo 0 > "$B/charge_disable"; fi
    setprop ctl.start lwky_smart_charge
    echo "$(date) stopped; module USER vote released; legacy init service restored" >> "$LOG"
    rm -rf "$R"
}
trap cleanup EXIT
trap 'exit 0' INT TERM HUP
[ -f "$LOG" ] && [ "$(wc -c < "$LOG")" -gt 262144 ] && mv "$LOG" "$LOG.previous"
# Legacy fake thermal/input state must be released through its own Normal mode,
# before first installation. Never clear a real thermal vote here.
if [ "$(cat "$B/input_suspend")" != 0 ] || [ "$(cat "$B/charge_control_limit")" = "$(cat "$B/charge_control_limit_max")" ] || [ "$(cat "$B/bp_enable")" != 0 ] || [ "$(cat "$B/charging_enabled")" != 1 ]; then
    echo "$(date) preflight rejected: select Normal charge before first installation; no thermal reset performed" >> "$LOG"
    exit 1
fi
setprop ctl.stop lwky_smart_charge
sleep 1
[ "$(getprop init.svc.lwky_smart_charge)" = stopped ] || exit 1
owns_vote=1
export CLASSPATH="$MODDIR/charge-bridge.jar"
/system/bin/app_process /system/bin --nice-name=tb371fc-charge-bridge ChargeBridge "$MODDIR" "$R" >> "$LOG" 2>&1 &
child=$!
echo "$child" > "$R/child"
echo "$(date) started supervisor=$$ child=$child" >> "$LOG"
while [ ! -f "$MODDIR/disable" ] && [ ! -f "$MODDIR/remove" ] && [ ! -f "$R/stop" ]; do
    kill -0 "$child" 2>/dev/null || { echo "$(date) bridge exited; restoring legacy" >> "$LOG"; exit 1; }
    if [ "$(getprop init.svc.lwky_smart_charge)" = running ]; then
        setprop ctl.stop lwky_smart_charge
        echo "$(date) stopped unexpected legacy restart" >> "$LOG"
    fi
    sleep 5 &
    wait $! || true
done
