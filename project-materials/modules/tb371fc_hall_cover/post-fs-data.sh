#!/system/bin/sh
set -eu
MODDIR=${0%/*}
TARGET=/system/framework/services.jar
setprop sys.tb371fc.cover_block_wake 0
setprop sys.tb371fc.cover_closed 0
setprop sys.tb371fc.cover_guard_ready 0
[ ! -e "$MODDIR/disable" ] && [ ! -e "$MODDIR/remove" ] || exit 0
CURRENT=$(sha256sum "$TARGET" | cut -d' ' -f1)
case "$CURRENT" in
 d1ee7be94a7c6cfe0a1dedf1ed3a56fdd5c8d5c1a137f64072d3e974ccf73abe|fdc9b2c4263d95449bfb2eaea0b316c4290e15f98eaf28a10975f005ade3056b|0e1b38b97418afad44df3b25bbb43560ac6b32f4fd9f70747c01380f6897dc43) ;;
 *) echo "SKIPPED unsupported services.jar $CURRENT" > "$MODDIR/mount.log"; exit 0;;
esac
[ "$(sha256sum "$MODDIR/payload/services.jar" | cut -d' ' -f1)" = 0e1b38b97418afad44df3b25bbb43560ac6b32f4fd9f70747c01380f6897dc43 ]
/data/adb/ksu/bin/resetprop -n ro.bootanim.set_orientation_startup_0 ORIENTATION_90
chcon u:object_r:system_file:s0 "$MODDIR/payload/services.jar"
/system/bin/nsenter -t 1 -m -- mount -o bind "$MODDIR/payload/services.jar" "$TARGET"
setprop sys.tb371fc.cover_guard_ready 1
echo 'MOUNTED cover wake guard + existing startup orientation 90' > "$MODDIR/mount.log"
