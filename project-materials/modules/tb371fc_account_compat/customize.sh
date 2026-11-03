SKIPUNZIP=0
ui_print "- TB371FC HyperOS 3 R8 account compatibility (experimental)"
[ -x /data/adb/ksu/bin/busybox ] || abort "! BakaSU / KernelSU is required"
[ "$(cat /data/vendor/tb371fc/build-id 2>/dev/null)" = "TB371FC-HyperOS3-20261103-full" ] || abort "! Only this project's TB371FC R8 ROM is supported"
[ "$(getprop ro.bootanim.set_orientation_startup_0)" = "ORIENTATION_90" ] || abort "! R8 startup-orientation baseline was not detected"
SDK_CODE=$(dumpsys package com.xiaomi.micloud.sdk | sed -n 's/.*versionCode=\([0-9]*\).*/\1/p' | head -n 1)
[ "$SDK_CODE" = "20" ] || abort "! Requires the original micloud-sdk versionCode 20"
set_perm "$MODPATH/service.sh" 0 0 0755
set_perm "$MODPATH/payload/RtMiCloudSDK.apk" 0 0 0644
ui_print "- Reboot after installation; FindDevice remains hidden"
