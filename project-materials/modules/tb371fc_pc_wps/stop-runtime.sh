#!/system/bin/sh
MODDIR=${0%/*}
. "$MODDIR/root-runtime.sh" || exit 1
set -u
STATE=/data/adb/tb371fc_pc_wps
worker=$(cat "$STATE/run/worker.pid" 2>/dev/null)
ns=$(cat "$STATE/run/namespace" 2>/dev/null)
case "$worker" in ''|*[!0-9]*) worker=0;; esac
case "$ns" in mnt:\[*\]) ;; *) ns=invalid;; esac
boot=$(cat "$STATE/run/boot-id" 2>/dev/null)
if [ "$ns" != invalid ] && [ "$boot" = "$(cat /proc/sys/kernel/random/boot_id)" ]; then
  pids=$(ls -l /proc/[0-9]*/ns/mnt 2>/dev/null | awk -v target="$ns" '$NF==target {for(i=1;i<=NF;i++) if($i ~ /^\/proc\/[0-9]+\/ns\/mnt$/) {split($i,a,"/"); print a[3]}}')
  owned=0
  for pid in $pids; do
    if [ "$(readlink "/proc/$pid/root" 2>/dev/null)" = "$STATE/rootfs" ]; then owned=1; break; fi
    if [ "$pid" = "$worker" ] && grep -q 'tb371fc_pc_wps/run-runtime.sh' "/proc/$pid/cmdline" 2>/dev/null; then owned=1; break; fi
  done
  if [ "$owned" = 1 ]; then
    for pid in $pids; do
      [ "$pid" = "$$" ] && continue
      kill -KILL "$pid" 2>/dev/null || true
    done
  fi
fi

# Loop associations are global even though their mounts live only in the
# private namespace.  Detach only devices recorded by this module and only
# when sysfs still proves that they back our exact image, so a stale pid/file
# can never detach another subsystem's loop device.
for name in usr kingsoft; do
  record="$STATE/run/$name.loop"
  loop=$(cat "$record" 2>/dev/null)
  image="$STATE/images/$name.ext4"
  case "$loop" in
    /dev/block/loop*) number=${loop#/dev/block/loop};;
    *) number=invalid;;
  esac
  case "$number" in ''|*[!0-9]*) loop=invalid;; esac
  if [ "$loop" != invalid ]; then
    block=${loop##*/}
    actual=$(cat "/sys/class/block/$block/loop/backing_file" 2>/dev/null)
    expected=$(readlink -f "$image" 2>/dev/null)
    actual_path=$(readlink -f "$actual" 2>/dev/null)
    if [ -n "$expected" ] && [ "$actual_path" = "$expected" ]; then
      "$BB" losetup -d "$loop" 2>/dev/null || true
    fi
  fi
  rm -f "$record"
done
resetprop sys.mslg.mounted 0
resetprop sys.mslg.restart 0
resetprop sys.mslg.isalive false
