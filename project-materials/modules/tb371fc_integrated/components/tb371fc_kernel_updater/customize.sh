#!/system/bin/sh
[ "$(getprop ro.product.device)" = sheng ] || abort "! 仅支持 sheng/TB371FC"
[ "$(getprop ro.product.device_for_attestation)" = TB371FC ] || abort "! 设备校验失败"
[ "$(getprop ro.tb371fc.build_id)" = TB371FC-HyperOS3-20261103-full ] || abort "! 仅支持 TB371FC HyperOS3 R8"

EXPECTED=a9fb2b73e43e8e8bb1b7a8456bf772e6729b68c110ed57bf7f2c1d1cf417b981
ACTUAL=$(sha256sum "$MODPATH/payload/boot.img")
ACTUAL=${ACTUAL%% *}
[ "$ACTUAL" = "$EXPECTED" ] || abort "! 内核载荷校验失败"

set_perm "$MODPATH/action.sh" 0 0 0755
set_perm "$MODPATH/service.sh" 0 0 0755
set_perm "$MODPATH/uninstall.sh" 0 0 0755
ui_print "- 首次安装时，整合模块会校验载荷、备份活动槽 boot 并自动更新"
ui_print "- 写入和回读校验成功后才会自动重启；任一步失败均不改写 boot"
