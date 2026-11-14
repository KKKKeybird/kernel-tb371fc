#!/system/bin/sh
MODDIR=$(readlink -f "${0%/*}")
R=/dev/tb371fc_stock_charge
B=/sys/class/power_supply/battery
T=$(readlink -f /product/priv-app/MIUISecurityCenterPad/MIUISecurityCenterPad.apk)
LOG=/data/adb/tb371fc-stock-charge.log
[ -d "$R" ] || exit 0
[ "$(cat "$R/moddir" 2>/dev/null)" = "$MODDIR" ] || exit 1
owned_ui() {
 [ -e "$R/owns_ui" ] || return 1
 awk -v t="$T" '$5==t {found=1} END{exit !found}' /proc/self/mountinfo || return 1
 [ "$(stat -c '%d:%i' "$T")" = "$(cat "$R/ui_fingerprint" 2>/dev/null)" ]
}
if [ -e "$R/owns_vote" ] && [ "$(cat "$B/bp_enable" 2>/dev/null)" = 0 ]; then echo 0 > "$B/charge_disable"; fi
if owned_ui; then
 am force-stop com.miui.securitycenter
 umount -l "$T" || { echo "$(date) UI rollback failed; retain runtime" >> "$LOG"; exit 1; }
fi
[ "$(sha256sum "$T" | cut -d' ' -f1)" = ab7fea0478fcc99b18b1c546efab0ea281f3e744d4ca35d67ba33ad48c26ba6e ] || { echo "$(date) original APK not restored; runtime retained" >> "$LOG"; exit 1; }
failed=0
if [ -e "$R/owns_night" ]; then
 old=$(cat "$R/night_before" 2>/dev/null)
 if [ -n "$old" ]; then /data/adb/ksu/bin/resetprop -n persist.vendor.night.charge "$old" || failed=1;
 else /data/adb/ksu/bin/resetprop -n --delete persist.vendor.night.charge || failed=1; fi
fi
for entry in smartchg ai; do
 if [ -e "$R/owns_$entry" ]; then
  case "$entry" in smartchg) prop=persist.vendor.smartchg;; ai) prop=persist.vendor.battery.ai.predict;; esac
  old=$(cat "$R/${entry}_before" 2>/dev/null)
  if [ -n "$old" ]; then /data/adb/ksu/bin/resetprop -n "$prop" "$old" || failed=1;
  else /data/adb/ksu/bin/resetprop -n --delete "$prop" || failed=1; fi
 fi
done
[ "$failed" = 0 ] || { echo "$(date) property rollback incomplete; runtime retained" >> "$LOG"; exit 1; }
if [ ! -f "$R/update_stop" ]; then
 mode=$(settings --user 0 get secure security_pc_secure_protect_mode_key)
 if [ "$mode" = 3 ]; then settings --user 0 put secure security_pc_secure_protect_mode_key 0; mode=0; fi
 case "$mode" in 0|1|2) setprop persist.sys.pc_mode "$mode";; esac
 [ "$(cat "$B/bp_enable" 2>/dev/null)" = 0 ] && setprop ctl.start lwky_smart_charge
fi
echo "$(date) stopped; stock UI and original night capability restored; module vote released" >> "$LOG"
rm -rf "$R"
