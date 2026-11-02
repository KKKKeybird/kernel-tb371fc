#!/system/bin/sh
MODDIR=${0%/*}
until [ "$(getprop sys.boot_completed)" = "1" ]; do sleep 2; done
sleep 3
[ -e "$MODDIR/disable" ] && exit 0
[ -e "$MODDIR/remove" ] && exit 0
if [ -w /proc/support_pen ]; then
  printf '1,0\n' > /proc/support_pen
  cat /proc/support_pen > "$MODDIR/pen-init.log"
fi
