SKIPUNZIP=0
ui_print "- TB371FC R8 pen battery bridge lock fix"
[ -x /data/adb/ksu/bin/busybox ] || abort "! BakaSU / KernelSU is required"
[ "$(getprop ro.product.device)" = "sheng" ] || abort "! Unsupported device identity"
[ "$(getprop ro.build.version.sdk)" = "35" ] || abort "! Requires Android 15 / SDK 35"
[ "$(cat /data/vendor/tb371fc/build-id 2>/dev/null)" = "TB371FC-HyperOS3-20261103-full" ] ||
  abort "! Only this project's TB371FC R8 ROM is supported"

TARGET=/system/etc/tb371fc/core/pen_bridge/supervisor.sh
ORIGINAL_SHA=875f1445f0a93ec6d05b6586e0e9bc6255928ce88fda8c112630208a0e15557b
FIXED_SHA=fa8aa51bd5544f608c4f6b3b63f9e6abf7cca459dfdebe96c8ea5dd6e1b6cd42
BB=/data/adb/ksu/bin/busybox

TARGET_SHA=$("$BB" sha256sum "$TARGET" | "$BB" awk '{print $1}')
if [ "$TARGET_SHA" != "$ORIGINAL_SHA" ]; then
  PREVIOUS=/data/adb/modules/tb371fc_pen_lockfix/module.prop
  PREVIOUS_ID=$(sed -n 's/^id=//p' "$PREVIOUS" 2>/dev/null)
  [ "$TARGET_SHA" = "$FIXED_SHA" ] && [ "$PREVIOUS_ID" = "tb371fc_pen_lockfix" ] ||
    abort "! Pen bridge supervisor fingerprint is not recognized"
fi
[ "$("$BB" sha256sum "$MODPATH/system/etc/tb371fc/core/pen_bridge/supervisor.sh" | "$BB" awk '{print $1}')" = "$FIXED_SHA" ] ||
  abort "! Module payload is damaged"

set_perm "$MODPATH/service.sh" 0 0 0755
set_perm "$MODPATH/action.sh" 0 0 0755
set_perm "$MODPATH/uninstall.sh" 0 0 0755
set_perm "$MODPATH/system/etc/tb371fc/core/pen_bridge/supervisor.sh" 0 0 0755
ui_print "- Exact ROM and supervisor fingerprint verified"
ui_print "- Reboot after installation; the bridge will start at boot"
