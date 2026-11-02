#!/system/bin/sh
# Restore Xiaomi props/behavior that Lenovo vendor omits.
# Only props that HyperOS framework actually consumes are set here.
MODDIR=${0%/*}
RP=/data/adb/ksu/bin/resetprop
[ -x "$RP" ] || RP=resetprop

# --- perf: ScrollOptimizer / TurboSched PRendering (default false on this vendor)
$RP ro.vendor.perf.scroll_opt 1
$RP ro.vendor.perf.scroll_opt.heavy_app 2

# --- blur: ViewRootImpl MiBlur texture scale (default 25%%)
$RP -p persist.sys.background_blur_scale 25

# --- app launch preload (asset missing on this port, config provisioned here)
mkdir -p /data/system/app_launch_preload
CFG=/data/system/app_launch_preload/app_launch_preload_pkg_cfg.json
if [ ! -s "$CFG" ]; then
  cat > "$CFG" <<'JSON'
{"apps":[{"packageName":"com.tencent.mm","index":1},{"packageName":"com.tencent.mtt","index":2},{"packageName":"com.miui.notes","index":3},{"packageName":"com.miui.gallery","index":4},{"packageName":"com.android.settings","index":5},{"packageName":"com.taobao.taobao","index":6}]}
JSON
fi
chmod 644 "$CFG"
$RP persist.sys.performance.appLaunchPreload.enable true

mkdir -p /data/adb/perfprops
{
  echo "=== $(date) perfprops boot ==="
  echo "scroll_opt=$(getprop ro.vendor.perf.scroll_opt) heavy_app=$(getprop ro.vendor.perf.scroll_opt.heavy_app)"
  echo "blur_scale=$(getprop persist.sys.background_blur_scale)"
  echo "preload=$(getprop persist.sys.performance.appLaunchPreload.enable) cfg=$(wc -c < $CFG 2>/dev/null)"
} >> /data/adb/perfprops/status.log
