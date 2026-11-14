SKIPUNZIP=0
ui_print '- TB371FC R8 stock charging bridge'
[ "$(cat /data/vendor/tb371fc/build-id 2>/dev/null)" = TB371FC-HyperOS3-20261103-full ] || abort '! Exact TB371FC R8 build required'
[ "$(uname -r)" = 4.19.325-perf++ ] || abort '! Untested kernel'
case "$(uname -v)" in "#25 SMP PREEMPT "*) ;; *) abort '! Validated HyperOS #25 kernel required; update the kernel first';; esac
[ "$(getprop ro.build.version.sdk)" = 35 ] || abort '! Android 15 required'
[ -x /data/adb/ksu/bin/ksud ] || abort '! KernelSU/BakaSU required'
[ "$(sha256sum /system/lwky/lwky_smart_charge_hyper | cut -d' ' -f1)" = b4f4742d0924839958f52d8b1cd6068123b1408f0581728eb66222345746efe8 ] || abort '! Unrecognized legacy charging implementation'
[ "$(cat /sys/class/power_supply/battery/input_suspend)" = 0 ] || abort '! Select Normal charge to release legacy input suspension first'
[ "$(cat /sys/class/power_supply/battery/charging_enabled)" = 1 ] || abort '! Select Normal charge before installing; existing stop owner detected'
[ "$(cat /sys/class/power_supply/battery/bp_enable)" = 0 ] || abort '! Another Lenovo protection owner is active'
[ -w /sys/class/power_supply/battery/charge_disable ] || abort '! Missing stock charge-disable interface'
for file in service.sh supervisor.sh post-fs-data.sh action.sh uninstall.sh; do set_perm "$MODPATH/$file" 0 0 0755; done
set_perm "$MODPATH/charge-bridge.jar" 0 0 0644
ui_print '- Xiaomi Normal / 80% health / immediate bypass -> Lenovo charge_disable'
ui_print '- No APK replacement, USB suspension or thermal-vote override'
ui_print '- Disable/uninstall and reboot restores the original ROM implementation'
