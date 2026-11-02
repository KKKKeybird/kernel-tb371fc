#!/system/bin/sh
MODDIR=${0%/*}
sh "$MODDIR/stop.sh"
setprop sys.tb371fc.cover_guard_ready 0
