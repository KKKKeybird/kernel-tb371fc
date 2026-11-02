#!/system/bin/sh
# Clean offline provisioner. Never replace an existing runtime or user home.
set -eu
BB=/data/adb/ksu/bin/busybox
if [ ! /proc/$$/exe -ef "$BB" ]; then exec "$BB" sh "$0" "$@"; fi
P=/vendor/etc/tb371fc-enhanced/wps
S=/data/adb/tb371fc_pc_wps
T=/data/adb/tb371fc_pc_wps.provisioning
umask 077
exec 8>/data/adb/tb371fc_pc_wps.provision.lock
"$BB" flock -n 8 || exit 0
mkdir -p /data/adb/tb371fc_wps_install
exec >>/data/adb/tb371fc_wps_install/install.log 2>&1
fail() { trap - EXIT; echo "FAILED $1"; setprop sys.tb371fc.wps "failed_$1"; exit 1; }
trap 'result=$?; if [ "$result" != 0 ]; then setprop sys.tb371fc.wps failed_unexpected; fi' EXIT
complete_runtime() {
 [ -f "$1/rootfs/etc/passwd" ] && {
  { [ -x "$1/usr/bin/MSLGd" ] && [ -x "$1/kingsoft/wps-office/office6/wps" ]; } ||
  { [ -f "$1/images/usr.ext4" ] && [ -f "$1/images/kingsoft.ext4" ] && [ -f "$1/provisioned-version" ]; }
 }
}
if complete_runtime "$S"; then
 setprop sys.tb371fc.wps existing_runtime
 exit 0
fi
# Existing incomplete data may contain personal files: do not delete or merge it.
if [ -e "$S" ]; then fail existing_partial_runtime; fi
if [ -L "$T" ]; then fail stage_symlink; fi
if [ -e "$T" ]; then
 [ -f "$T/.tb371fc-clean-staging" ] || fail foreign_staging
 rm -rf "$T"
fi
FREE=$(df -k /data | tail -1 | awk '{print $4}')
[ "$FREE" -ge 8388608 ] || fail need_8gb_free
setprop sys.tb371fc.wps verifying_resources
(cd "$P" && sha256sum -c SHA256SUMS) || fail resource_checksum
# Reuse only our own clean stage after an interrupted first installation.
mkdir -p "$T"
: > "$T/.tb371fc-clean-staging"
mkdir -p "$T/rootfs" "$T/images" "$T/run"
setprop sys.tb371fc.wps extracting_rootfs
"$BB" tar -xzpf "$P/rootfs-clean.tgz" -C "$T/rootfs" || fail rootfs_extract
"$P/zstd" -d -q "$P/usr.ext4.zst" -o "$T/images/usr.ext4" || fail usr_extract
"$P/zstd" -d -q "$P/kingsoft.ext4.zst" -o "$T/images/kingsoft.ext4" || fail program_extract
(cd "$T/images" && sha256sum -c "$P/ext4.sha256") || fail image_checksum
mkdir -p "$T/rootfs/opt/tb371fc-fonts"
"$BB" tar -xzpf "$P/fonts.tgz" -C "$T/rootfs/opt/tb371fc-fonts" || fail fonts_extract
mkdir -p "$T/rootfs/etc/fonts/conf.d"
printf '%s\n' '<?xml version="1.0"?><!DOCTYPE fontconfig SYSTEM "fonts.dtd"><fontconfig><dir>/opt/tb371fc-fonts/share/fonts</dir><dir>/opt/tb371fc-fonts/local/share/fonts</dir><dir>/opt/tb371fc-fonts/share/texmf/fonts/opentype</dir></fontconfig>' > "$T/rootfs/etc/fonts/conf.d/98-tb371fc-font-paths.conf"
mkdir -p "$T/rootfs/opt/kingsoft" "$T/rootfs/usr" "$T/rootfs/etc/fonts/conf.d"
echo rootfs-25.04.23.tgz > "$T/original-rootfs-version"
setprop sys.tb371fc.wps installing_launcher
pm install -r "$P/WPSLauncher.apk" || fail launcher_install
pm path com.xiaomi.wpslauncher >/dev/null || fail launcher_missing
printf '%s\n' 20261101-r2 > "$T/provisioned-version"
chmod 0700 "$T" "$T/run"
sync
mv "$T" "$S"
setprop sys.tb371fc.wps provisioned
echo 'Clean WPS runtime provisioned. Personal data was not copied.'
