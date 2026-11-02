#!/system/bin/sh
# Root tools for the verified built-in ReSukiSU installation.
TB371FC_ROOTBIN=/data/adb/ksu/bin
BB=$TB371FC_ROOTBIN/busybox
[ -x "$BB" ] && [ -x "$TB371FC_ROOTBIN/resetprop" ] || return 1
PATH=$TB371FC_ROOTBIN:$PATH
export PATH BB
