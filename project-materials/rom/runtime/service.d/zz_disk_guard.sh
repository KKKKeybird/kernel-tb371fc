#!/system/bin/sh
# Disk guard: truncate runaway logs inside the PC-WPS chroot.
CUP=/data/adb/tb371fc_pc_wps/rootfs/var/log/cups
for f in "$CUP/error_log" "$CUP/error_log.0" "$CUP/error_log.1" "$CUP/access_log"; do
  [ -f "$f" ] || continue
  sz=$(stat -c %s "$f" 2>/dev/null)
  [ -n "$sz" ] || continue
  if [ "$sz" -gt 209715200 ]; then
    : > "$f"
  fi
done
# runtime log
RT=/data/adb/tb371fc_pc_wps/run/runtime.log
if [ -f "$RT" ]; then
  sz=$(stat -c %s "$RT" 2>/dev/null)
  if [ -n "$sz" ] && [ "$sz" -gt 104857600 ]; then : > "$RT"; fi
fi
