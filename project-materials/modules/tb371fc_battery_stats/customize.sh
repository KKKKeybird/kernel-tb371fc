SKIPUNZIP=0
ui_print '- TB371FC R8 dynamic battery capacity v1.0'
[ "$(cat /data/vendor/tb371fc/build-id 2>/dev/null)" = TB371FC-HyperOS3-20261103-full ] || abort '! Exact TB371FC R8 ROM required'
[ "$(uname -r)" = 4.19.325-perf++ ] || abort '! Untested kernel; refusing'
[ "$(getprop ro.product.device)" = sheng ] || abort '! Wrong device identity'
[ "$(getprop ro.build.version.sdk)" = 35 ] || abort '! Android 15 / SDK 35 required'
[ -x /data/adb/ksu/bin/ksud ] || abort '! BakaSU / KernelSU required'
[ -c /dev/i2c-1 ] || abort '! MM8013 bus unavailable'
[ "$(tr -d '\000' < /sys/class/power_supply/bms/device/of_node/compatible)" = mitsumi,mm8013c06 ] || abort '! Wrong gauge variant'
command -v i2ctransfer >/dev/null || abort '! i2ctransfer required'
for supply in bms battery; do
    for property in charge_full charge_full_design charge_counter; do
        [ -f "/sys/class/power_supply/$supply/$property" ] || abort '! Missing capacity interface'
    done
done
set_perm "$MODPATH/service.sh" 0 0 0755
set_perm "$MODPATH/daemon.sh" 0 0 0755
set_perm "$MODPATH/uninstall.sh" 0 0 0755
set_perm "$MODPATH/action.sh" 0 0 0755
ui_print '- Does not change percentage, charging policy, temperatures or gauge parameters'
ui_print '- Design baseline: Lenovo official typical capacity, 10200mAh'
ui_print '- Full/remaining capacity follow validated MM8013 measurements'
ui_print '- Reboot after installation; disable/uninstall to restore native statistics'
