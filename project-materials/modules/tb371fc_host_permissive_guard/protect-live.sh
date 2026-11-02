#!/system/bin/sh
set -eu
M=${0%/*}
# Existing processes may have private snapshots of the mount tree.
# At boot post-fs-data protects PID 1 before zygote / apps are created.
for p in 1 $(pidof zygote64 zygote com.droidspaces.app droidspaces); do
 [ -d /proc/$p ] || continue
 /system/bin/nsenter -t "$p" -m -- sh "$M/apply.sh"
done
