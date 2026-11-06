SKIPUNZIP=0
ui_print "- TB371FC HyperOS 3 R8 top-bottom split compatibility (experimental)"
[ -x /data/adb/ksu/bin/busybox ] || abort "! BakaSU / KernelSU is required"
[ "$(getprop ro.product.device)" = "sheng" ] || abort "! Unsupported device identity"
[ "$(getprop ro.build.version.sdk)" = "35" ] || abort "! Requires Android 15 / SDK 35"
[ "$(cat /data/vendor/tb371fc/build-id 2>/dev/null)" = "TB371FC-HyperOS3-20261103-full" ] ||
  abort "! Only this project's TB371FC R8 ROM is supported"
[ "$(getprop ro.bootanim.set_orientation_startup_0)" = "ORIENTATION_90" ] ||
  abort "! R8 startup-orientation baseline was not detected"

TARGET=/system_ext/priv-app/MiuiSystemUI/MiuiSystemUI.apk
BASE_SHA=8bb7b6fc36e2bfbe4f3ee2ae91512c95ee32209e86a71e1437040f3eaf45e5bb
PATCH_SHA=0b243aebb7efe8f5655dbe467d61e0def5fc502005be508028857fa3d57089da
BB=/data/adb/ksu/bin/busybox

[ -f "$TARGET" ] || abort "! SystemUI target is missing"
[ "$("$BB" sha256sum "$TARGET" | "$BB" awk '{print $1}')" = "$BASE_SHA" ] ||
  abort "! Original SystemUI APK fingerprint does not match this module"
[ "$("$BB" sha256sum "$MODPATH/payload/MiuiSystemUI.apk" | "$BB" awk '{print $1}')" = "$PATCH_SHA" ] ||
  abort "! Module payload is damaged"

set_perm "$MODPATH/service.sh" 0 0 0755
set_perm "$MODPATH/rollback.sh" 0 0 0755
set_perm "$MODPATH/action.sh" 0 0 0755
set_perm "$MODPATH/uninstall.sh" 0 0 0755
set_perm "$MODPATH/payload/MiuiSystemUI.apk" 0 0 0644
ui_print "- Exact ROM and SystemUI fingerprint verified"
ui_print "- Reboot after installation; SystemUI will restart once to load the patch"
