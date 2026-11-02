#!/system/bin/sh
MODDIR=${0%/*}
/data/adb/ksu/bin/resetprop -n persist.sys.smartpower.limit.max.refresh.rate 120
# Also support root frameworks where a dedicated product partition is not auto-mounted.
SRC="$MODDIR/system/product/etc/device_features/sheng.xml"
TARGET=/product/etc/device_features/sheng.xml
[ -f "$SRC" ] && [ -f "$TARGET" ] || exit 0
/system/bin/nsenter -t 1 -m -- mount -o bind "$SRC" "$TARGET"
