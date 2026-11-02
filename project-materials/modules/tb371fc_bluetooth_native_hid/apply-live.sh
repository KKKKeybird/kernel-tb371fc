#!/system/bin/sh
set -eu
M=${0%/*}
for p in 1 $(pidof zygote64 zygote com.android.bluetooth); do
 [ -d /proc/$p ] || continue
 nsenter -t "$p" -m -- sh "$M/apply.sh"
done
