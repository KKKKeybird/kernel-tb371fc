#!/system/bin/sh
[ "$(getprop ro.product.device)" = sheng ] || abort "! 仅支持 sheng/TB371FC"
[ "$(getprop ro.product.device_for_attestation)" = TB371FC ] || abort "! 设备校验失败"
[ "$(getprop ro.tb371fc.build_id)" = TB371FC-HyperOS3-20261103-full ] || abort "! 仅支持 TB371FC HyperOS3 R8"

# An independently installed hall-cover module owns the same path. Keep its
# file ownership instead of relying on module mount order.
if [ -d /data/adb/modules/tb371fc_hall_cover ] && [ ! -e /data/adb/modules/tb371fc_hall_cover/disable ]; then
  rm -f "$MODPATH/system/etc/tb371fc/core/hall_cover/service.sh"
  ui_print "- 检测到独立 hall_cover 模块，跳过霍尔脚本覆盖"
fi

set_perm_recursive "$MODPATH/system" 0 0 0755 0644
set_perm "$MODPATH/system/etc/tb371fc/core/speaker_rotation/supervisor.sh" 0 0 0755
[ ! -f "$MODPATH/system/etc/tb371fc/core/hall_cover/service.sh" ] || \
  set_perm "$MODPATH/system/etc/tb371fc/core/hall_cover/service.sh" 0 0 0755
set_perm "$MODPATH/service.sh" 0 0 0755
set_perm "$MODPATH/action.sh" 0 0 0755
set_perm "$MODPATH/uninstall.sh" 0 0 0755
ui_print "- 只覆盖扬声器旋转服务与无人占用的霍尔锁修复"
ui_print "- 不修改 SystemUI、账号、电池、手写笔或 boot"
