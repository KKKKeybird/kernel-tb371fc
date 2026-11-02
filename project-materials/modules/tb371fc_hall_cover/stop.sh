#!/system/bin/sh
MODDIR=${0%/*}
touch "$MODDIR/disable"
setprop sys.tb371fc.cover_block_wake 0
setprop sys.tb371fc.cover_closed 0
if [ -r "$MODDIR/policy.lock/pid" ]; then
 p=$(cat "$MODDIR/policy.lock/pid")
 case "$p" in ''|*[!0-9]*) ;; *)
  if [ -r "/proc/$p/cmdline" ] && tr '\000' ' ' < "/proc/$p/cmdline" | grep -Fq "$MODDIR/service.sh"; then kill -TERM "$p"; fi;;
 esac
fi
if [ -r "$MODDIR/cover.pid" ]; then
 p=$(cat "$MODDIR/cover.pid")
 case "$p" in ''|*[!0-9]*) ;; *)
  if [ -r "/proc/$p/cmdline" ] && tr '\000' ' ' < "/proc/$p/cmdline" | grep -Fq "HallCover $MODDIR"; then kill -TERM "$p"; fi;;
 esac
fi
/system/bin/cmd power suppress-ambient-display TB371FC_COVER false
/system/bin/cmd power set-mode 0
