#!/system/bin/sh
set -eu
MODDIR=${0%/*}
. "$MODDIR/root-runtime.sh" || exit 1
STATE=/data/adb/tb371fc_pc_wps
ROOT=$STATE/rootfs
if [ -f "$MODDIR/config/99-tb371fc-common-fonts.conf" ]; then
  cp "$MODDIR/config/99-tb371fc-common-fonts.conf" "$ROOT/etc/fonts/conf.d/99-tb371fc-common-fonts.conf"
  chmod 644 "$ROOT/etc/fonts/conf.d/99-tb371fc-common-fonts.conf"
fi
"$BB" mount --make-rprivate /
echo $$ > "$STATE/run/worker.pid"
readlink /proc/self/ns/mnt > "$STATE/run/namespace"
cat /proc/sys/kernel/random/boot_id > "$STATE/run/boot-id"
mkdir -p "$ROOT/usr" "$ROOT/opt/kingsoft"
if [ -f "$STATE/images/usr.ext4" ] && [ -f "$STATE/images/kingsoft.ext4" ]; then
  rm -f "$STATE/run/usr.loop" "$STATE/run/kingsoft.loop"
  usr_loop=$("$BB" losetup -f)
  "$BB" losetup -r "$usr_loop" "$STATE/images/usr.ext4"
  printf '%s\n' "$usr_loop" > "$STATE/run/usr.loop"
  "$BB" mount -t ext4 -o ro,noload,nodev "$usr_loop" "$ROOT/usr"
  kingsoft_loop=$("$BB" losetup -f)
  "$BB" losetup -r "$kingsoft_loop" "$STATE/images/kingsoft.ext4"
  printf '%s\n' "$kingsoft_loop" > "$STATE/run/kingsoft.loop"
  "$BB" mount -t ext4 -o ro,noload,nodev "$kingsoft_loop" "$ROOT/opt/kingsoft"
else
  mount --bind "$STATE/usr" "$ROOT/usr"
  mount --bind "$STATE/kingsoft" "$ROOT/opt/kingsoft"
fi
mount --bind /dev "$ROOT/dev"
mount --bind /proc "$ROOT/proc"
mount --bind /sys "$ROOT/sys"
mkdir -p /dev/msl/rdp "$ROOT/tmp/msl/rdp" "$ROOT/tablet"
chmod 755 /dev/msl
chmod 1777 /dev/msl/rdp "$ROOT/tmp" "$ROOT/tmp/msl/rdp"
mount --bind /dev/msl/rdp "$ROOT/tmp/msl/rdp"
# Preserve Android ownership while making existing media-owned files editable.
mkdir -p "$ROOT/android-shared"
mount --bind /data/media/0 "$ROOT/android-shared"
chroot "$ROOT" /usr/bin/bindfs -o nonempty --force-user=7100 --force-group=1023 --perms=u+rwX --create-for-user=1023 --create-for-group=1023 --chown-ignore --chgrp-ignore --chmod-ignore /android-shared /tablet
if [ ! -d "$ROOT/tablet/HyperEngine" ]; then
  mkdir "$ROOT/tablet/HyperEngine"
  chown 7100:1023 "$ROOT/tablet/HyperEngine"
  chmod 2770 "$ROOT/tablet/HyperEngine"
fi
if [ -f "$STATE/debug" ]; then
  chroot "$ROOT" /bin/su -s /bin/sh root -c 'umask 0002; export TMPDIR=/tmp/msl/rdp; exec /usr/bin/MSLGd' >> "$STATE/runtime.log" 2>&1
else
  chroot "$ROOT" /bin/su -s /bin/sh root -c 'umask 0002; export TMPDIR=/tmp/msl/rdp; exec /usr/bin/MSLGd' >/dev/null 2>&1
fi
