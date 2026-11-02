#!/system/bin/sh
MODDIR=${0%/*}
[ -r "$MODDIR/worker.pid" ] || exit 0
workerpid=$(cat "$MODDIR/worker.pid")
case "$workerpid" in ''|*[!0-9]*) exit 0 ;; esac
if [ -r "/proc/$workerpid/cmdline" ] && tr '\000' ' ' < "/proc/$workerpid/cmdline" | grep -Fq "$MODDIR/worker.sh"; then
  kill "$workerpid"
fi
