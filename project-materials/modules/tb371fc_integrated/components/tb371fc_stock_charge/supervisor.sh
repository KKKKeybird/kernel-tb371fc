#!/system/bin/sh
MODDIR=$(readlink -f "${0%/*}")
R=/dev/tb371fc_stock_charge
B=/sys/class/power_supply/battery
LOG=/data/adb/tb371fc-stock-charge.log
TARGET=$(readlink -f /product/priv-app/MIUISecurityCenterPad/MIUISecurityCenterPad.apk)
[ "$(cat /data/vendor/tb371fc/build-id 2>/dev/null)" = TB371FC-HyperOS3-20261103-full ] || exit 1
case "$(uname -v)" in "#25 SMP PREEMPT "*) ;; *) exit 1;; esac
[ ! -f "$MODDIR/disable" ] && [ ! -f "$MODDIR/remove" ] || exit 0
if [ -d "$R" ]; then
 p=$(cat "$R/pid" 2>/dev/null)
 case "$p" in ''|*[!0-9]*) p=0;; esac
 if [ "$p" != 0 ] && [ -r "/proc/$p/cmdline" ] && tr '\000' '\n' < "/proc/$p/cmdline" | grep -F /supervisor.sh >/dev/null; then exit 0; fi
 old=$(cat "$R/moddir" 2>/dev/null)
 if [ -n "$old" ] && [ -f "$old/rollback.sh" ]; then sh "$old/rollback.sh" --stale || exit 1;
 else echo 'Unrecognized stale runtime; reboot required' >> "$LOG"; exit 1; fi
fi
mkdir -m 700 "$R" || exit 1
echo $$ > "$R/pid"
echo "$MODDIR" > "$R/moddir"
child=
cleanup() {
 trap - EXIT INT TERM HUP
 if [ -n "$child" ]; then
  kill -TERM "$child" 2>/dev/null
  for attempt in 1 2 3 4 5; do kill -0 "$child" 2>/dev/null || break; sleep 1; done
  kill -0 "$child" 2>/dev/null && kill -KILL "$child" 2>/dev/null
  wait "$child" 2>/dev/null
 fi
 sh "$MODDIR/rollback.sh"
}
trap cleanup EXIT
trap 'exit 0' INT TERM HUP
[ -f "$LOG" ] && [ "$(wc -c < "$LOG")" -gt 262144 ] && mv "$LOG" "$LOG.previous"
[ "$(sha256sum /system/lwky/lwky_smart_charge_hyper | cut -d' ' -f1)" = b4f4742d0924839958f52d8b1cd6068123b1408f0581728eb66222345746efe8 ] || exit 1
[ "$(sha256sum "$TARGET" | cut -d' ' -f1)" = ab7fea0478fcc99b18b1c546efab0ea281f3e744d4ca35d67ba33ad48c26ba6e ] || exit 1
[ "$(sha256sum "$MODDIR/securitycenter.apk" | cut -d' ' -f1)" = "$(cat "$MODDIR/generated-apk.sha256")" ] || exit 1
[ "$(pm path com.miui.securitycenter)" = package:/product/priv-app/MIUISecurityCenterPad/MIUISecurityCenterPad.apk ] || exit 1
if [ "$(cat "$B/input_suspend")" != 0 ] || [ "$(cat "$B/charge_control_limit")" = "$(cat "$B/charge_control_limit_max")" ] || [ "$(cat "$B/bp_enable")" != 0 ] || [ "$(cat "$B/charging_enabled")" != 1 ]; then
 echo "$(date) preflight rejected; select Normal charging first; no thermal vote reset" >> "$LOG"; exit 1
fi
[ ! -d /dev/tb371fc_pps_temp_probe ] || { echo "$(date) PPS experiment active; stock bridge refused" >> "$LOG"; exit 1; }
setprop ctl.stop lwky_smart_charge
sleep 1
[ "$(getprop init.svc.lwky_smart_charge)" = stopped ] || exit 1
touch "$R/owns_vote"
# Runtime-only capability: the module now supplies the missing stock-stop bridge.
getprop persist.vendor.night.charge > "$R/night_before"
touch "$R/owns_night"
/data/adb/ksu/bin/resetprop -n persist.vendor.night.charge true || exit 1
getprop persist.vendor.smartchg > "$R/smartchg_before"
getprop persist.vendor.battery.ai.predict > "$R/ai_before"
touch "$R/owns_smartchg" "$R/owns_ai"
smart=$(cat "$R/smartchg_before")
case "$smart" in ''|*[!0-9]*) smart=0;; esac
/data/adb/ksu/bin/resetprop -n persist.vendor.smartchg "$((smart | 1))" || exit 1
/data/adb/ksu/bin/resetprop -n persist.vendor.battery.ai.predict true || exit 1
chmod 0644 "$MODDIR/securitycenter.apk" || exit 1
chcon u:object_r:system_file:s0 "$MODDIR/securitycenter.apk"
CLASSPATH="$MODDIR/charge-bridge.jar" /system/bin/app_process /system/bin --nice-name=tb371fc-reset-controls ChargeBridge --reset-controls "$MODDIR" "$R" >> "$LOG" 2>&1 || exit 1
touch "$R/owns_ui"
stat -c '%d:%i' "$MODDIR/securitycenter.apk" > "$R/ui_fingerprint"
mount -o bind "$MODDIR/securitycenter.apk" "$TARGET" || exit 1
am force-stop com.miui.securitycenter
export CLASSPATH="$MODDIR/charge-bridge.jar"
/system/bin/app_process /system/bin --nice-name=tb371fc-charge-bridge ChargeBridge "$MODDIR" "$R" >> "$LOG" 2>&1 &
child=$!; echo "$child" > "$R/child"
echo "$(date) supervisor=$$ child=$child; four-mode UI mounted" >> "$LOG"
# The provider hook initializes only the ROM's original charging managers.
# Do not depend on the disabled/unregistered aggregate PowerSaveService.
while [ ! -f "$MODDIR/disable" ] && [ ! -f "$MODDIR/remove" ] && [ ! -f "$R/stop" ]; do
 kill -0 "$child" 2>/dev/null || { echo "$(date) bridge exited; reverting" >> "$LOG"; exit 1; }
 [ "$(getprop init.svc.lwky_smart_charge)" = running ] && setprop ctl.stop lwky_smart_charge
 sleep 5 &
 wait $! || true
done
