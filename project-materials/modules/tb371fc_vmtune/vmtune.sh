#!/system/bin/sh
# Reversible VM + schedutil tuning for smoother UI under memory pressure
LOG=/data/adb/vmtune/tune.log
mkdir -p /data/adb/vmtune
log() { echo "$(date '+%F %T') $*" >> "$LOG"; }
set_one() { # path value
  [ -w "$1" ] || { log "skip(no-write) $1"; return; }
  v=$(cat "$1" 2>/dev/null)
  echo "$2" > "$1" 2>/dev/null
  n=$(cat "$1" 2>/dev/null)
  log "$1: $v -> $n (want $2)"
}
set_one /proc/sys/vm/watermark_scale_factor 100
set_one /proc/sys/vm/min_free_kbytes 16384
set_one /proc/sys/vm/page-cluster 0
for c in 0 4 7; do
  set_one /sys/devices/system/cpu/cpufreq/policy$c/schedutil/up_rate_limit_us 500
  set_one /sys/devices/system/cpu/cpufreq/policy$c/schedutil/down_rate_limit_us 20000
done
for d in dm-41 sda; do
  set_one /sys/block/$d/queue/read_ahead_kb 1024
done
log "done"
