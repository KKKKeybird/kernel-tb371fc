#!/system/bin/sh
M=${0%/*}
{
  echo "time=$(date '+%F %T')"
  echo "kernel=$(uname -r)"
  echo "slot=$(getprop ro.boot.slot_suffix)"
  echo "build=$(getprop ro.tb371fc.build_id)"
} > "$M/status.txt"
