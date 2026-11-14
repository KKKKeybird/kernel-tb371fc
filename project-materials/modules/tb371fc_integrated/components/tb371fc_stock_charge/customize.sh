SKIPUNZIP=0
ui_print '- TB371FC R8 four-mode stock charging bridge'
[ "$(cat /data/vendor/tb371fc/build-id 2>/dev/null)" = TB371FC-HyperOS3-20261103-full ] || abort '! Exact R8 build required'
case "$(uname -v)" in "#25 SMP PREEMPT "*) ;; *) abort '! Validated HyperOS #25 kernel required';; esac
[ "$(getprop ro.build.version.sdk)" = 35 ] || abort '! Android 15 required'
[ -x /data/adb/ksu/bin/ksud ] || abort '! KernelSU/BakaSU required'
R=/dev/tb371fc_stock_charge
if [ -d "$R" ]; then
    touch "$R/update_stop"
    current=$(cat "$R/moddir" 2>/dev/null)
    [ -n "$current" ] || current=/data/adb/modules/tb371fc_stock_charge
    sh "$current/service.sh" --stop || { rm -f "$R/update_stop"; abort '! Existing charging bridge could not stop safely'; }
fi
APK=/product/priv-app/MIUISecurityCenterPad/MIUISecurityCenterPad.apk
[ "$(sha256sum "$APK" | cut -d' ' -f1)" = ab7fea0478fcc99b18b1c546efab0ea281f3e744d4ca35d67ba33ad48c26ba6e ] || abort '! Exact R8 Security Center required; no patch applied'
[ "$(sha256sum /system/lwky/lwky_smart_charge_hyper | cut -d' ' -f1)" = b4f4742d0924839958f52d8b1cd6068123b1408f0581728eb66222345746efe8 ] || abort '! Unrecognized legacy charging controller'
[ "$(cat /sys/class/power_supply/battery/input_suspend)" = 0 ] || abort '! Select Normal charge before installation'
[ "$(cat /sys/class/power_supply/battery/charging_enabled)" = 1 ] || abort '! Existing stop owner; select Normal charge first'
[ "$(cat /sys/class/power_supply/battery/bp_enable)" = 0 ] || abort '! Another Lenovo protection owner is active'
[ -w /sys/class/power_supply/battery/charge_disable ] || abort '! Stock stop interface unavailable'
ui_print '- Rebuild only the verified UI/charge-adapter DEX; preserve original prediction code'
CLASSPATH="$MODPATH/apk-patcher.jar" /system/bin/app_process /system/bin ApkPatch "$APK" "$MODPATH/payload/classes3.dex" "$MODPATH/securitycenter.apk" || abort '! APK generation failed'
sha256sum "$MODPATH/securitycenter.apk" | cut -d' ' -f1 > "$MODPATH/generated-apk.sha256"
rm -rf "$MODPATH/payload"
# Correct only the board capacity metadata; retain every other feature flag.
PROFILE=/product/etc/device_features/sheng.xml
base=c3c07e7f4aac0ab86be080e70e867e546052fa205501ba3a665deb571eb4a944
fixed=73280c9e56627ee65967290fe3a14fedb0484abd3b17d009382c93a7479643e0
actual=$(sha256sum "$PROFILE" | cut -d' ' -f1)
mkdir -p "$MODPATH/system/product/etc/device_features"
case "$actual" in
 "$base") sed 's#<string name="battery_capacity_typ">5000</string>#<string name="battery_capacity_typ">10200</string>#' "$PROFILE" > "$MODPATH/system/product/etc/device_features/sheng.xml";;
 "$fixed") cp "$PROFILE" "$MODPATH/system/product/etc/device_features/sheng.xml";;
 *) abort '! Unrecognized board feature profile; preserve existing configuration';;
esac
[ "$(sha256sum "$MODPATH/system/product/etc/device_features/sheng.xml" | cut -d' ' -f1)" = "$fixed" ] || abort '! Capacity metadata verification failed'
for file in service.sh supervisor.sh rollback.sh post-fs-data.sh action.sh uninstall.sh; do
    chown 0:0 "$MODPATH/$file" && chmod 0755 "$MODPATH/$file" || abort "! Cannot set executable permission: $file"
done
for file in charge-bridge.jar apk-patcher.jar securitycenter.apk generated-apk.sha256; do
    chown 0:0 "$MODPATH/$file" && chmod 0644 "$MODPATH/$file" || abort "! Cannot set readable permission: $file"
done
ui_print '- Four choices remain in the original Battery protection page'
ui_print '- Native night/prediction/80% capability flags are runtime-only and reversible'
ui_print '- Board capacity metadata: 10200mAh; temperature, PD and JEITA unchanged'
ui_print '- Reboot to apply; disable/uninstall and reboot to restore the original ROM'
