#!/system/bin/sh
[ "$(getprop ro.product.device)" = sheng ] &&
[ "$(getprop ro.product.device_for_attestation)" = TB371FC ] || abort "! 仅支持 TB371FC"
[ "$(getprop ro.tb371fc.build_id)" = TB371FC-HyperOS3-20261103-full ] || abort "! 仅支持本项目 HyperOS 3 R8"
case "$(getprop ro.boot.slot_suffix)" in _a|_b) ;; *) abort "! 活动槽无效";; esac
actual=$(sha256sum "$MODPATH/payload/boot.img") || abort "! 缺少 boot 载荷"
[ "${actual%% *}" = 2825eb9593e6593581c1f42ba52faeac1f6a53f1a3aaf94012ccef7a0b8bc272 ] || abort "! boot 哈希不匹配"
set_perm "$MODPATH/action.sh" 0 0 0755
ui_print '- 安装不会写入 boot。需要在操作页五分钟内连续确认两次。'
