#!/system/bin/sh
MODDIR=${0%/*}
LOG=/data/adb/mtd_off/mount.log
mkdir -p /data/adb/mtd_off
log(){ echo "$(date '+%H:%M:%S') $*" >> $LOG; }
for i in $(seq 1 60); do [ "$(getprop sys.boot_completed)" = "1" ] && break; sleep 2; done
sleep 10
cur=$(getprop init.svc.vendor.mtdservice-1-3)
if [ "$cur" = "running" ]; then
  stop vendor.mtdservice-1-3 2>/dev/null
  log "stopped service (was running)"
fi
