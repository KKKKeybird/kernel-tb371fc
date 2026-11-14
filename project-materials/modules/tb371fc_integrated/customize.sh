SKIPUNZIP=0
ui_print '- TB371FC R8 integrated maintenance module'
[ "$(getprop ro.product.device)" = sheng ] || abort '! Wrong device sheng/TB371FC'
[ "$(getprop ro.build.version.sdk)" = 35 ] || abort '! Android 15 / SDK 35 required'
[ -x /data/adb/ksu/bin/ksud ] || abort '! BakaSU / KernelSU required'
[ -x /data/adb/ksu/bin/busybox ] || abort '! BakaSU BusyBox required'
[ "$(cat /data/vendor/tb371fc/build-id 2>/dev/null)" = TB371FC-HyperOS3-20261103-full ] || abort '! Exact R8 build required'
case "$(uname -v)" in "#25 SMP PREEMPT "*) ;; *) abort '! HyperOS #25 kernel required';; esac

COMP="$MODPATH/components"
STOCK="$COMP/tb371fc_stock_charge"
APK=/product/priv-app/MIUISecurityCenterPad/MIUISecurityCenterPad.apk
BASE_APK_SHA=ab7fea0478fcc99b18b1c546efab0ea281f3e744d4ca35d67ba33ad48c26ba6e
STATE=/data/adb/tb371fc-integrated/migration-v1
R=/dev/tb371fc_stock_charge
OLD_BAT=/data/adb/modules/tb371fc_battery_stats
OLD_STOCK_OWNER=
STOPPED_STOCK=0
STOPPED_BAT=0
PLANNED=

check_sha() {
    file=$1
    expected=$2
    [ -f "$file" ] || abort "! Missing payload: ${file##*/}"
    actual=$(sha256sum "$file" | cut -d' ' -f1)
    [ "$actual" = "$expected" ] || abort "! Payload hash mismatch: ${file##*/}"
}

check_sha "$COMP/tb371fc_account_compat/payload/RtMiCloudSDK.apk" 5cdedef6cf6982f8cf86a82b64e1a419eb956c84fb224a849b5f2adbe8c5ddbf
check_sha "$COMP/tb371fc_split_compat/payload/MiuiSystemUI.apk" 0b243aebb7efe8f5655dbe467d61e0def5fc502005be508028857fa3d57089da
check_sha "$COMP/tb371fc_pps_temp_test/hvdcp_opti.temp-chain-v3-test" c4f7c8921cef1897fa3bcc5134251a4a35887262c31510da5ae093d35f700285
check_sha "$MODPATH/system/etc/tb371fc/core/pen_bridge/supervisor.sh" fa8aa51bd5544f608c4f6b3b63f9e6abf7cca459dfdebe96c8ea5dd6e1b6cd42
check_sha "$MODPATH/system/etc/tb371fc/core/speaker_rotation/rotation.jar" 9f81e6dfc84dd22704d5f8fbded0570a3def3ea16f366073f5f30969c97e8dea
check_sha "$MODPATH/system/etc/tb371fc/core/speaker_rotation/supervisor.sh" 61f38ba48bb3a0045d8d306df5b3a8cd445fab4c9d4adc0c84e20dee9440cd69
check_sha "$MODPATH/system/etc/tb371fc/core/hall_cover/service.sh" 4ddaf20998b6a5cf09654116c03b70fe59712f378346f45cb9238bb8ab7183a5
check_sha "$STOCK/payload/classes3.dex" a8c9c7a81181ab1a0b7b1c68f3d4418c49a3f91af0dcba6ad4138bceed504f1e
[ "$(sha256sum /system/lwky/lwky_smart_charge_hyper | cut -d' ' -f1)" = b4f4742d0924839958f52d8b1cd6068123b1408f0581728eb66222345746efe8 ] || abort '! Unrecognized legacy charge controller'
[ "$(cat /sys/class/power_supply/battery/input_suspend)" = 0 ] || abort '! Select Normal charge before installation'
[ "$(cat /sys/class/power_supply/battery/bp_enable)" = 0 ] || abort '! Another Lenovo protection owner is active'

mkdir -p "$STATE"
printf 'id\texists\tenabled\tversionCode\n' > "$STATE/modules-before.tsv"
PLANNED="$STATE/.newly-disabled.$$"
: > "$PLANNED"
cleanup_migration() {
    status=$?
    if [ "$status" != 0 ]; then
        if [ -f "$STATE/newly-disabled.txt" ]; then
            while IFS= read -r id; do
                [ -n "$id" ] && rm -f "/data/adb/modules/$id/disable"
            done < "$STATE/newly-disabled.txt"
            rm -f "$STATE/newly-disabled.txt"
        fi
        if [ "$STOPPED_BAT" = 1 ] && [ -x "$OLD_BAT/service.sh" ]; then
            sh "$OLD_BAT/service.sh" </dev/null >/dev/null 2>&1 &
        fi
        if [ "$STOPPED_STOCK" = 1 ] && [ -n "$OLD_STOCK_OWNER" ] && [ -x "$OLD_STOCK_OWNER/service.sh" ]; then
            sh "$OLD_STOCK_OWNER/service.sh" </dev/null >/dev/null 2>&1 &
        fi
    fi
    rm -f "$PLANNED"
}
trap cleanup_migration EXIT HUP INT TERM

printf 'version\t1\ncreated\t%s\n' "$(date +%s)" > "$STATE/meta.tsv"
printf '%s\n' \
    security_pc_secure_protect_mode_key="$(settings --user 0 get secure security_pc_secure_protect_mode_key 2>/dev/null)" \
    persist.sys.pc_mode="$(getprop persist.sys.pc_mode)" \
    wmshell_multiwindow_version_code="$(settings get secure wmshell_multiwindow_version_code 2>/dev/null)" \
    persist.vendor.night.charge="$(getprop persist.vendor.night.charge)" \
    persist.vendor.smartchg="$(getprop persist.vendor.smartchg)" \
    persist.vendor.battery.ai.predict="$(getprop persist.vendor.battery.ai.predict)" > "$STATE/user-settings.tsv"

for id in tb371fc_account_compat tb371fc_split_compat tb371fc_pen_lockfix tb371fc_battery_stats tb371fc_pps_temp_test tb371fc_system_compat tb371fc_kernel_updater tb371fc_stock_charge; do
    dir="/data/adb/modules/$id"
    [ -d "$dir" ] || {
        printf '%s\t0\t0\t0\n' "$id" >> "$STATE/modules-before.tsv"
        continue
    }
    code=$(sed -n 's/^versionCode=//p' "$dir/module.prop" 2>/dev/null | head -1)
    if [ -e "$dir/disable" ] || [ -e "$dir/remove" ]; then enabled=0; else enabled=1; fi
    printf '%s\t1\t%s\t%s\n' "$id" "$enabled" "${code:-0}" >> "$STATE/modules-before.tsv"
    [ "$enabled" = 1 ] && printf '%s\n' "$id" >> "$PLANNED"
done
mv "$PLANNED" "$STATE/newly-disabled.txt"
sync

if [ -d "$R" ]; then
    OLD_STOCK_OWNER=$(cat "$R/moddir" 2>/dev/null)
    [ -n "$OLD_STOCK_OWNER" ] && [ -f "$OLD_STOCK_OWNER/service.sh" ] || abort '! Existing charging runtime has no recoverable owner'
    sh "$OLD_STOCK_OWNER/service.sh" --stop || abort '! Existing charging bridge did not stop safely'
    STOPPED_STOCK=1
fi
if [ -d /dev/tb371fc_battery_stats ]; then
    [ -f "$OLD_BAT/daemon.sh" ] || abort '! Existing capacity runtime has no recoverable owner'
    sh "$OLD_BAT/daemon.sh" --stop || abort '! Existing capacity daemon did not stop safely'
    STOPPED_BAT=1
fi
if [ -d /dev/tb371fc_pps_temp_probe ]; then
    [ -f /data/adb/modules/tb371fc_pps_temp_test/rollback.sh ] || abort '! Existing PPS runtime has no recoverable owner'
    su -M -c "sh '/data/adb/modules/tb371fc_pps_temp_test/rollback.sh'" || abort '! Existing PPS experiment did not roll back'
fi

[ "$(sha256sum "$APK" | cut -d' ' -f1)" = "$BASE_APK_SHA" ] || abort '! Exact R8 original Security Center is not visible after stopping its owner'
ui_print '- Generate Security Center from the exact installed R8 APK and verified one-DEX patch'
CLASSPATH="$STOCK/apk-patcher.jar" /system/bin/app_process /system/bin ApkPatch "$APK" "$STOCK/payload/classes3.dex" "$STOCK/securitycenter.apk" || abort '! Security Center generation failed'
sha256sum "$STOCK/securitycenter.apk" | cut -d' ' -f1 > "$STOCK/generated-apk.sha256"
rm -f "$STOCK/payload/classes3.dex"
rmdir "$STOCK/payload" 2>/dev/null || true

PROFILE=/product/etc/device_features/sheng.xml
BASE_PROFILE=c3c07e7f4aac0ab86be080e70e867e546052fa205501ba3a665deb571eb4a944
FIXED_PROFILE=73280c9e56627ee65967290fe3a14fedb0484abd3b17d009382c93a7479643e0
actual=$(sha256sum "$PROFILE" | cut -d' ' -f1)
mkdir -p "$MODPATH/system/product/etc/device_features"
case "$actual" in
    "$BASE_PROFILE") sed 's#<string name="battery_capacity_typ">5000</string>#<string name="battery_capacity_typ">10200</string>#' "$PROFILE" > "$MODPATH/system/product/etc/device_features/sheng.xml";;
    "$FIXED_PROFILE") cp "$PROFILE" "$MODPATH/system/product/etc/device_features/sheng.xml";;
    *) abort '! Unrecognized board feature profile';;
esac
[ "$(sha256sum "$MODPATH/system/product/etc/device_features/sheng.xml" | cut -d' ' -f1)" = "$FIXED_PROFILE" ] || abort '! Capacity profile verification failed'

for file in \
    "$MODPATH/customize.sh" "$MODPATH/post-fs-data.sh" "$MODPATH/service.sh" \
    "$MODPATH/action.sh" "$MODPATH/uninstall.sh" "$MODPATH/run-pps.sh" \
    "$MODPATH/run-kernel.sh" "$MODPATH/tools/migration.sh" \
    "$MODPATH/tools/rollback-migration.sh" \
    "$COMP/tb371fc_account_compat/service.sh" \
    "$COMP/tb371fc_split_compat/service.sh" "$COMP/tb371fc_split_compat/rollback.sh" \
    "$COMP/tb371fc_pen_lockfix/service.sh" \
    "$COMP/tb371fc_battery_stats/service.sh" "$COMP/tb371fc_battery_stats/daemon.sh" \
    "$COMP/tb371fc_battery_stats/gauge.sh" \
    "$COMP/tb371fc_pps_temp_test/action.sh" "$COMP/tb371fc_pps_temp_test/run-test.sh" \
    "$COMP/tb371fc_pps_temp_test/rollback.sh" \
    "$COMP/tb371fc_system_compat/service.sh" \
    "$COMP/tb371fc_stock_charge/service.sh" "$COMP/tb371fc_stock_charge/supervisor.sh" \
    "$COMP/tb371fc_stock_charge/rollback.sh" "$COMP/tb371fc_stock_charge/action.sh" \
    "$COMP/tb371fc_kernel_updater/service.sh" "$COMP/tb371fc_kernel_updater/action.sh" \
    "$MODPATH/system/etc/tb371fc/core/pen_bridge/supervisor.sh" \
    "$MODPATH/system/etc/tb371fc/core/speaker_rotation/supervisor.sh" \
    "$MODPATH/system/etc/tb371fc/core/hall_cover/service.sh"
do
    if ! chown 0:0 "$file" || ! chmod 0755 "$file"; then
        abort "! Cannot set executable permission: ${file##*/}"
    fi
done
for file in \
    "$COMP/tb371fc_account_compat/payload/RtMiCloudSDK.apk" \
    "$COMP/tb371fc_split_compat/payload/MiuiSystemUI.apk" \
    "$COMP/tb371fc_pps_temp_test/hvdcp_opti.temp-chain-v3-test" \
    "$COMP/tb371fc_stock_charge/securitycenter.apk" \
    "$COMP/tb371fc_stock_charge/generated-apk.sha256" \
    "$COMP/tb371fc_stock_charge/charge-bridge.jar" "$COMP/tb371fc_stock_charge/apk-patcher.jar" \
    "$MODPATH/system/etc/tb371fc/core/speaker_rotation/rotation.jar"
do
    if ! chown 0:0 "$file" || ! chmod 0644 "$file"; then
        abort "! Cannot set readable permission: ${file##*/}"
    fi
done

while IFS= read -r id; do
    [ -n "$id" ] || continue
    touch "/data/adb/modules/$id/disable" || abort "! Cannot disable legacy module: $id"
done < "$STATE/newly-disabled.txt"
sync
trap - EXIT HUP INT TERM

ui_print '- Account, split, pen, capacity, system, charging and kernel tools are integrated'
ui_print '- Legacy module folders are retained and disabled; rollback script can re-enable them'
ui_print '- PPS remains manual; kernel update remains manual double-confirmation'
ui_print '- Reboot to activate the integrated module'
