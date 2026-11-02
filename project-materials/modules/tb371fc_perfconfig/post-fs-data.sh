#!/system/bin/sh
# Overlay perf configs: perfconfigstore.xml (props) + perfboostsconfig.xml (boost tables)
MODDIR=${0%/*}
LOG=/data/adb/perfconfig/mount.log
mkdir -p /data/adb/perfconfig
mount_one() {
  SRC="$1"; DST="$2"
  [ -f "$SRC" ] || { echo "$(date) missing $SRC" >> "$LOG"; return 1; }
  [ -f "$DST" ] || { echo "$(date) missing $DST" >> "$LOG"; return 1; }
  if grep -q " $DST " /proc/mounts; then
    echo "$(date) already mounted $DST" >> "$LOG"; return 0
  fi
  if mount --bind "$SRC" "$DST"; then
    echo "$(date) MOUNTED $DST $(sha256sum "$SRC" | cut -d' ' -f1)" >> "$LOG"
  else
    echo "$(date) mount failed $DST rc=$?" >> "$LOG"
  fi
}
mount_one "$MODDIR/files/perfconfigstore.xml" /vendor/etc/perf/perfconfigstore.xml
mount_one "$MODDIR/files/perfboostsconfig.xml" /vendor/etc/perf/perfboostsconfig.xml
