#!/system/bin/sh
# Run by init before the kernel-injected KSU post-fs-data action.
set -eu
P=/vendor/etc/tb371fc-enhanced
TAG=$(cut -c1-12 "$P/payload.sha256")
case "$TAG" in ''|*[!0-9a-f]*) exit 1;; esac
[ "${#TAG}" = 12 ] || exit 1
S=/data/adb/tb371fc_rom/20261101-$TAG
umask 077
mkdir -p "$S"
chmod 0700 "$S"
exec >>"$S/seed.log" 2>&1
fail() { echo "FAILED $1"; setprop sys.tb371fc.setup_error "$1"; setprop sys.tb371fc.setup failed; exit 1; }
[ -f "$S/DISABLE" ] && { setprop sys.tb371fc.setup disabled; exit 0; }
[ "$(uname -r)" = 4.19.325-perf++ ] || fail unexpected_kernel
cd "$P"
sha256sum -c payload.sha256 || fail payload_checksum
# Pin the entire executable payload before loading it.
if [ ! -f "$S/payload.ok" ]; then
 mkdir -p "$S/payload"
 unzip -o -q "$P/payload.zip" -d "$S/payload" || fail payload_extract
 (cd "$S/payload" && sha256sum -c SHA256SUMS) || fail extracted_checksum
 : > "$S/payload.ok"
fi
W=$S/payload
EXPECTED=$(cat "$W/ksud.sha256")
if [ -f /data/adb/ksud ]; then
 [ "$(sha256sum /data/adb/ksud | cut -d' ' -f1)" = "$EXPECTED" ] || fail existing_ksud_mismatch
fi
if [ ! -x /data/adb/ksud ] || [ ! -x /data/adb/ksu/bin/busybox ]; then
 chmod 0755 "$W/bin/ksud"
 "$W/bin/ksud" install || fail root_userspace_install
fi
BB=/data/adb/ksu/bin/busybox
[ -x "$BB" ] || fail missing_busybox
# If a previous installation failed, restore BEFORE KSU can load staged modules.
if [ "$(cat "$S/phase" 2>/dev/null || true)" = installing_modules ]; then
 echo rollback_pending > "$S/phase"
 sync
fi
if [ "$(cat "$S/phase" 2>/dev/null || true)" = rollback_pending ]; then
 [ -f "$S/backup.ok" ] || fail missing_rollback_backup
 META=/data/adb/modules/meta-overlayfs
 set +e
 "$BB" mountpoint -q "$META/mnt"
 MOUNTED=$?
 set -e
 case "$MOUNTED" in 0) fail rollback_image_already_mounted;; 1) ;; *) fail rollback_mount_check;; esac
 (cd "$S" && sha256sum -c backup.sha256) || fail rollback_checksum
 if [ -f "$S/prior-modules.img" ]; then
  cp -p "$S/prior-modules.img" "$META/modules.img.restore"
  mv -f "$META/modules.img.restore" "$META/modules.img"
 fi
 while IFS= read -r id; do
  case "$id" in tb371fc_*) ;; *) fail unsafe_rollback_id;; esac
  rm -rf "/data/adb/modules_update/$id" "/data/adb/modules/$id"
 done < "$W/module-ids.txt"
 for n in zz_disk_guard.sh zz_powermodule_stub.sh zz_user_tweaks.sh; do rm -f "/data/adb/service.d/$n"; done
 rm -f /data/adb/tb371fc_powermodule/service.jar /data/adb/tb371fc_powermodule/probe.jar
 "$BB" tar -xpf "$S/prior-files.tar" -C /data/adb || fail restore_prior_files
 echo failed_rolled_back > "$S/phase"
 sync
fi
case "$(cat "$S/phase" 2>/dev/null || true)" in
 failed*|complete) exit 0;;
esac
setprop sys.tb371fc.setup seeded
