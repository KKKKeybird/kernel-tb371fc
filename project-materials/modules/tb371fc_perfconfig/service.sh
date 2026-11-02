#!/system/bin/sh
MODDIR=${0%/*}
LOG=/data/adb/perfconfig/mount.log
mkdir -p /data/adb/perfconfig
while [ "$(getprop sys.boot_completed)" != "1" ]; do sleep 1; done
sleep 15
if grep -q "/vendor/etc/perf/perfconfigstore.xml" /proc/mounts; then
  echo "$(date) boot ok: $(sha256sum /vendor/etc/perf/perfconfigstore.xml | cut -d' ' -f1)" >> "$LOG"
  echo "$(date) props: $(grep -c '<Prop ' /vendor/etc/perf/perfconfigstore.xml)" >> "$LOG"
else
  echo "$(date) boot: NOT mounted" >> "$LOG"
fi
