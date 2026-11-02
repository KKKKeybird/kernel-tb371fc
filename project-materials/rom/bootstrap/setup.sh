#!/system/bin/sh
set -eu
P=/vendor/etc/tb371fc-enhanced
TAG=$(cut -c1-12 "$P/payload.sha256")
case "$TAG" in ''|*[!0-9a-f]*) exit 1;; esac
[ "${#TAG}" = 12 ] || exit 1
S=/data/adb/tb371fc_rom/20261101-$TAG
W=$S/payload
K=/data/adb/ksud
BB=/data/adb/ksu/bin/busybox
[ -x "$BB" ] || exit 1
if [ ! /proc/$$/exe -ef "$BB" ]; then exec "$BB" sh "$0" "$@"; fi
umask 077
exec >>"$S/setup.log" 2>&1
exec 9>"$S/setup.lock"
"$BB" flock -n 9 || exit 0
phase() { printf '%s\n' "$1" > "$S/phase.tmp"; mv -f "$S/phase.tmp" "$S/phase"; sync; }
status() { setprop sys.tb371fc.setup "$1"; }
fail() {
 trap - EXIT
 echo "FAILED $1"
 setprop sys.tb371fc.setup_error "$1"
 if [ -f "$S/backup.ok" ]; then phase rollback_pending; else phase failed; fi
 status failed
 exit 1
}
trap 'fail unexpected_command_failure' EXIT
[ "$(getprop sys.boot_completed)" = 1 ] || fail boot_not_complete
if [ -f "$S/DISABLE" ]; then status disabled; trap - EXIT; exit 0; fi
[ -f "$S/payload.ok" ] || fail payload_not_seeded
CURRENT=$(cat "$S/phase" 2>/dev/null || true)
case "$CURRENT" in failed*|rollback_pending) status failed; trap - EXIT; exit 1;; esac
case "$(getprop persist.sys.safemode):$(getprop ro.sys.safemode)" in 1:*|*:1) status safe_mode; trap - EXIT; exit 0;; esac
chmod 0755 "$W/bin/safe-mode"
set +e
"$W/bin/safe-mode"
SAFETY=$?
set -e
case "$SAFETY" in 0) ;; 10) status safe_mode; trap - EXIT; exit 0;; *) fail safe_mode_query_failed;; esac
command -v magisk >/dev/null 2>&1 && fail active_magisk_conflict
if [ "$CURRENT" = complete ]; then status ready; trap - EXIT; exit 0; fi
ATTEMPT=$(cat "$S/attempts" 2>/dev/null || echo 0)
case "$ATTEMPT" in ''|*[!0-9]*) fail invalid_attempt_counter;; esac
[ "$ATTEMPT" -lt 3 ] || fail attempt_limit
echo $((ATTEMPT+1)) > "$S/attempts"
sync
META=/data/adb/modules/meta-overlayfs
meta_ok() {
 [ -f "$META/module.prop" ] &&
 [ "$(sha256sum "$META/module.prop" | cut -d' ' -f1)" = "$(cat "$W/meta-prop.sha256")" ] &&
 [ -f "$META/metainstall.sh" ] &&
 [ ! -e "$META/disable" ] && [ ! -e "$META/remove" ] && [ ! -e "$META/update" ]
}
reboot_stage() {
 phase "$1"
 status "$2"
 sync
 trap - EXIT
 setprop sys.powerctl reboot
 "$BB" sleep 30 9>&-
 trap 'fail reboot_not_executed' EXIT
 exit 1
}
if [ -z "$CURRENT" ]; then
 if [ -e /data/adb/metamodule ] || [ -d "$META" ]; then
  meta_ok || fail unsupported_or_pending_metamodule
 else
  status installing_meta
  "$BB" timeout 180 "$K" module install "$W/dependencies/meta-overlayfs-1.3.1.zip" || fail metamodule_install
  [ -f /data/adb/modules_update/meta-overlayfs/module.prop ] || fail metamodule_not_staged
  reboot_stage meta_staged rebooting_meta
 fi
 CURRENT=meta_staged
fi
if [ "$CURRENT" = meta_staged ]; then
 meta_ok || fail metamodule_not_active
 [ -z "$(find /data/adb/modules_update -mindepth 1 -maxdepth 1 -print 2>/dev/null)" ] || fail unrelated_pending_modules
 status backing_up
 # BusyBox rejects restoring a completely empty tar; this stable file always exists.
 echo modules/meta-overlayfs/module.prop > "$S/backup-paths.txt"
 while IFS= read -r id; do [ ! -d "/data/adb/modules/$id" ] || echo "modules/$id" >> "$S/backup-paths.txt"; done < "$W/module-ids.txt"
 for n in zz_disk_guard.sh zz_powermodule_stub.sh zz_user_tweaks.sh; do
  [ ! -f "/data/adb/service.d/$n" ] || echo "service.d/$n" >> "$S/backup-paths.txt"
 done
 for n in service.jar probe.jar; do
  [ ! -f "/data/adb/tb371fc_powermodule/$n" ] || echo "tb371fc_powermodule/$n" >> "$S/backup-paths.txt"
 done
 IMAGE_BYTES=$(stat -c %s "$META/modules.img")
 FREE_KB=$(df -k /data | tail -1 | awk '{print $4}')
 [ "$FREE_KB" -gt $((IMAGE_BYTES/1024+524288)) ] || fail insufficient_backup_space
 "$BB" tar -cpf "$S/prior-files.tar" -C /data/adb -T "$S/backup-paths.txt"
 cp -p "$META/modules.img" "$S/prior-modules.img"
 (cd "$S" && sha256sum prior-files.tar prior-modules.img > backup.sha256)
 : > "$S/backup.ok"
 phase installing_modules
 status installing_modules
 while IFS= read -r id; do
  "$BB" timeout 180 "$K" module install "$W/modules/$id.zip" || fail module_install
 done < "$W/module-ids.txt"
 mkdir -p /data/adb/service.d /data/adb/tb371fc_powermodule
 for n in zz_disk_guard.sh zz_powermodule_stub.sh zz_user_tweaks.sh; do
  cp -p "$W/runtime/service.d/$n" "/data/adb/service.d/$n.new"
  chmod 0755 "/data/adb/service.d/$n.new"
  mv -f "/data/adb/service.d/$n.new" "/data/adb/service.d/$n"
 done
 for n in service.jar probe.jar; do
  cp -p "$W/runtime/tb371fc_powermodule/$n" "/data/adb/tb371fc_powermodule/$n.new"
  chmod 0600 "/data/adb/tb371fc_powermodule/$n.new"
  mv -f "/data/adb/tb371fc_powermodule/$n.new" "/data/adb/tb371fc_powermodule/$n"
 done
 reboot_stage modules_staged rebooting_modules
fi
if [ "$CURRENT" = modules_staged ]; then
 status verifying
 meta_ok || fail metamodule_disabled
 while IFS= read -r id; do
  for f in update disable remove; do [ ! -e "/data/adb/modules/$id/$f" ] || fail module_not_active; done
 done < "$W/module-ids.txt"
 while IFS='|' read -r id rel expected; do
  found=
  case "$rel" in
   system/vendor/*|system/product/*|system/system_ext/*|system/odm/*|system/oem/*) mapped=${rel#system/};;
   *) mapped=$rel;;
  esac
  for candidate in "/data/adb/modules/$id/$rel" "/data/adb/modules/$id/$mapped" "$META/mnt/$id/$mapped" "$META/mnt/$id/$rel"; do
   if [ -f "$candidate" ] && [ "$(sha256sum "$candidate" | cut -d' ' -f1)" = "$expected" ]; then found=1; break; fi
  done
  [ "$found" = 1 ] || fail effective_file_mismatch
 done < "$W/effective-files.txt"
 while IFS='|' read -r id rel; do
  [ -x "/data/adb/modules/$id/$rel" ] || fail executable_permission_missing
 done < "$W/executable-files.txt"
 phase complete
 status ready
 trap - EXIT
 exit 0
fi
fail interrupted_or_unknown_phase
