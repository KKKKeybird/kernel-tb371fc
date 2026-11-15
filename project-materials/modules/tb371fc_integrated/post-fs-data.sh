#!/system/bin/sh
case "$0" in
    */*) MODDIR=${0%/*};;
    *) MODDIR=$(pwd -P);;
esac
if [ -f "$MODDIR/disable" ] || [ -f "$MODDIR/remove" ]; then exit 0; fi
[ "$(cat /data/vendor/tb371fc/build-id 2>/dev/null)" = TB371FC-HyperOS3-20261103-full ] || exit 0
case "$(uname -v)" in "#25 SMP PREEMPT "*) ;; *) exit 1;; esac
RESETPROP=/data/adb/ksu/bin/resetprop
if [ -x "$RESETPROP" ] && "$RESETPROP" -n ro.vendor.audio.dolby.dax.support true; then
    setprop sys.tb371fc.dolby_dax ready
else
    setprop sys.tb371fc.dolby_dax failed
fi
setprop ctl.stop lwky_smart_charge 2>/dev/null || true
