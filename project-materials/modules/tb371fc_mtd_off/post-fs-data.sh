#!/system/bin/sh
MODDIR=${0%/*}
LOG=/data/adb/mtd_off/mount.log
mkdir -p /data/adb/mtd_off
log(){ echo "$(date '+%H:%M:%S') $*" >> $LOG; }
T=/vendor/bin/mtd@1.3
S=$MODDIR/files/noop
chmod 755 $S 2>/dev/null
[ -f "$T" ] || { log "ERR target $T missing"; exit 0; }
# bind in init's mount namespace so the HAL really execs the no-op
if command -v nsenter >/dev/null 2>&1; then
  nsenter -t 1 -m -- mount --bind "$S" "$T" 2>/dev/null && { log "binded(init-ns) noop over $T"; exit 0; }
fi
if mount --bind "$S" "$T" 2>/dev/null; then
  log "binded(self-ns) noop over $T"
else
  log "ERR bind $T"
fi
