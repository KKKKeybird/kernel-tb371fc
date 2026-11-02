#!/system/bin/sh
set -eu
MODDIR=${0%/*}
version=$(dumpsys package com.xiaomi.mslgrdp | sed -n 's/.*versionCode=\([0-9]*\).*/\1/p' | head -1)
[ "$version" = 301 ] || exit 1
apk=$(pm path com.xiaomi.mslgrdp | sed -n 's/^package://p' | head -1)
case "$apk" in
 /data/app/*/com.xiaomi.mslgrdp-*/base.apk)
  dir=${apk%/base.apk}/lib/arm64
  mkdir -p "$dir"
  chmod 755 "${dir%/arm64}" "$dir"
  for source in "$MODDIR"/client/lib/arm64/*.so; do
   target=$dir/${source##*/}
   cmp -s "$source" "$target" || cp "$source" "$target"
   chmod 644 "$target"
  done
  restorecon -RF "${dir%/arm64}" >/dev/null 2>&1 || true
  ;;
 /system/priv-app/TB371FCMSLgRdp/MSLgRdp.apk)
  dir=/system/priv-app/TB371FCMSLgRdp/lib/arm64
  for source in "$MODDIR"/client/lib/arm64/*.so; do
   cmp -s "$source" "$dir/${source##*/}" || exit 1
  done
  ;;
 *) exit 1;;
esac
uid=$(dumpsys package com.xiaomi.mslgrdp | sed -n 's/.*android.uid.hyperengine\/\([0-9]*\).*/\1/p' | head -1)
case "$uid" in ''|*[!0-9]*) exit 1;; esac
mkdir -p /data/rootfs
chmod 755 /data/rootfs
for name in .dns .MSLgRdpVersion; do
  [ -e "/data/rootfs/$name" ] || touch "/data/rootfs/$name"
  chown "$uid:$uid" "/data/rootfs/$name"
  chmod 644 "/data/rootfs/$name"
done
printf '301\n' > /data/rootfs/.MSLgRdpVersion
