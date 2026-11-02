#!/system/bin/sh
MODDIR=${0%/*}
touch "$MODDIR/remove"
if [ -r "$MODDIR/supervisor.lock/pid" ]; then
 p=$(cat "$MODDIR/supervisor.lock/pid")
 case "$p" in ''|*[!0-9]*) exit 0;; esac
 if [ -r "/proc/$p/cmdline" ] && tr '\000' ' ' < "/proc/$p/cmdline" | grep -Fq "$MODDIR/supervisor.sh"; then kill -TERM "$p"; fi
fi
