#!/system/bin/sh
set -u
MODDIR=${0%/*}
. "$MODDIR/root-runtime.sh" || exit 1
STATE=/data/adb/tb371fc_pc_wps
mkdir -p "$STATE/run"
chmod 700 "$STATE" "$STATE/run"
LOCK=$STATE/run/supervisor.lock
if ! mkdir "$LOCK" 2>/dev/null; then
  previous=$(cat "$LOCK/pid" 2>/dev/null)
  case "$previous" in ''|*[!0-9]*) previous=0;; esac
  if [ "$previous" != 0 ] && grep -q "$MODDIR/supervisor.sh" "/proc/$previous/cmdline" 2>/dev/null; then
    exit 0
  fi
  rm -f "$LOCK/pid"
  rmdir "$LOCK" 2>/dev/null || exit 1
  mkdir "$LOCK" || exit 1
fi
echo $$ > "$LOCK/pid"
log() { printf '%s %s\n' "$(date '+%F %T')" "$*" >> "$STATE/supervisor.log"; }
watcher=0
cleanup() {
  if [ "$watcher" != 0 ]; then
    kill "$watcher" 2>/dev/null || true
    wait "$watcher" 2>/dev/null || true
  fi
  /system/bin/sh "$MODDIR/stop-runtime.sh"
  rm -f "$LOCK/pid"
  rmdir "$LOCK" 2>/dev/null || true
}
trap cleanup EXIT
trap 'exit 0' INT TERM
while [ "$(getprop sys.boot_completed)" != 1 ]; do
  [ -e "$MODDIR/disable" ] || [ -e "$MODDIR/remove" ] && exit 0
  sleep 2
done
# Shared storage is credential-encrypted. Defer WPS until the first unlock.
# This does not pause pen/keyboard/cover adapters on later screen locks.
while [ "$(getprop sys.user.0.ce_available)" != true ]; do
  [ -e "$MODDIR/disable" ] || [ -e "$MODDIR/remove" ] && exit 0
  sleep 2
done
if [ -x "$STATE/usr/bin/MSLGd" ] && [ -x "$STATE/kingsoft/wps-office/office6/wps" ]; then
  runtime_mode=directory
elif [ -f "$STATE/images/usr.ext4" ] && [ -f "$STATE/images/kingsoft.ext4" ]; then
  runtime_mode=ext4
else
  log 'No complete directory or ext4 runtime; staying inactive.'
  exit 0
fi
/system/bin/sh "$MODDIR/repair-client.sh" || { log 'MSLgRdp client unavailable or unsupported.'; exit 1; }
resetprop ro.vendor.mslg.rootfs.version rootfs-25.04.23.tgz
/system/bin/sh "$MODDIR/frontend-lifecycle.sh" >/dev/null 2>&1 &
watcher=$!
log "Started event-driven frontend watcher $watcher for $runtime_mode runtime."
attempts=0
worker=0
while [ ! -e "$MODDIR/disable" ] && [ ! -e "$MODDIR/remove" ]; do
  if [ "$worker" = 0 ] || ! kill -0 "$worker" 2>/dev/null; then
    attempts=$((attempts+1))
    if [ "$attempts" -gt 3 ]; then log 'Runtime stopped after three failed starts.'; exit 1; fi
    /system/bin/sh "$MODDIR/stop-runtime.sh"
    rm -f /dev/msl/rdp/rdp_socket
    rm -f "$STATE/run/worker.pid" "$STATE/run/namespace"
    "$BB" unshare -m /system/bin/sh "$MODDIR/run-runtime.sh" >/dev/null 2>&1 &
    worker=$!
    log "Starting runtime worker $worker (attempt $attempts)."
    ready=0
    for count in 1 2 3 4 5 6 7 8 9 10; do
      if [ -S /dev/msl/rdp/rdp_socket ] && kill -0 "$worker" 2>/dev/null; then ready=1; break; fi
      sleep 1
    done
    if [ "$ready" = 1 ]; then
      resetprop sys.mslg.mounted 1
      resetprop sys.mslg.restart 1
      resetprop sys.mslg.optimg.setup ''
      log 'RDP socket ready.'
    else
      log 'RDP socket not ready.'
      /system/bin/sh "$MODDIR/stop-runtime.sh"
      worker=0
    fi
  fi
  if [ -s /data/rootfs/.dns ]; then
    if ! cmp -s /data/rootfs/.dns "$STATE/rootfs/etc/resolv.conf"; then
      cat /data/rootfs/.dns > "$STATE/rootfs/etc/resolv.conf"
    fi
  fi
  sleep 5
done
