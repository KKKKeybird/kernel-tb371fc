#!/system/bin/sh
MODDIR=${0%/*}
[ -e "$MODDIR/disable" ] && exit 0
[ -e "$MODDIR/remove" ] && exit 0
for src in "$MODDIR"/vintf/compatibility_matrix.*.xml; do
 dst=/system/etc/vintf/${src##*/}
 [ -f "$dst" ] || continue
 /system/bin/nsenter -t 1 -m -- mount -o bind "$src" "$dst" || exit 1
done
