#!/system/bin/sh
MODDIR=${0%/*}
[ -e "$MODDIR/disable" ] && exit 0
[ -e "$MODDIR/remove" ] && exit 0
# supervisor.sh owns the singleton lock and recovery, including concurrent starts.
sh "$MODDIR/supervisor.sh" >> "$MODDIR/supervisor.log" 2>&1 </dev/null &
