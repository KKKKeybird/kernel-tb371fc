#!/system/bin/sh
set -eu
M=${0%/*}
for p in 1 $(pidof zygote64 zygote com.droidspaces.app droidspaces); do
 [ -d /proc/$p ] || continue
 /system/bin/nsenter -t "$p" -m -- sh -c '
  set -eu
  M=$1; P=$2; T=/sys/fs/selinux/enforce
  grep " /sys/fs/selinux/enforce " /proc/self/mountinfo | grep -q " ro,"
  [ "$(stat -c %d:%i "$T")" = "$(stat -c %d:%i "$M/enforce-zero")" ]
  [ "$(cat "$M/enforce-actual")" = 0 ]
  if (printf 1 > "$T") 2>/dev/null; then exit 21; fi
  if setenforce 1 2>/dev/null; then exit 22; fi
  [ "$(cat "$M/enforce-actual")" = 0 ]
  echo "PASS pid=$P direct_write_blocked fallback_blocked actual_kernel_enforce=0"
 ' sh "$M" "$p"
done
