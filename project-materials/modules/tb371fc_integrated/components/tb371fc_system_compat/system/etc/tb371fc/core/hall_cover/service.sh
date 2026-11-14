#!/system/bin/sh
MODDIR=${0%/*}
LOCK="$MODDIR/policy.lock"
if ! mkdir "$LOCK" 2>/dev/null; then
 old=$(cat "$LOCK/pid" 2>/dev/null)
 case "$old" in
  ''|*[!0-9]*) ;;
  *)
   if [ -r "/proc/$old/cmdline" ] && tr '\000' ' ' < "/proc/$old/cmdline" | grep -Fq "$MODDIR/service.sh"; then exit 0; fi
   ;;
 esac
 rm -f "$LOCK/pid"; rmdir "$LOCK" 2>/dev/null || exit 0
 mkdir "$LOCK" 2>/dev/null || exit 0
fi
echo $$ > "$LOCK/pid"
child=
cleanup(){
 trap - EXIT HUP INT TERM
 setprop sys.tb371fc.cover_block_wake 0
 setprop sys.tb371fc.cover_closed 0
 [ -z "$child" ] || kill -TERM "$child" 2>/dev/null
 /system/bin/cmd power suppress-ambient-display TB371FC_COVER false >/dev/null 2>&1
 /system/bin/cmd power set-mode 0 >/dev/null 2>&1
 rm -f "$LOCK/pid"; rmdir "$LOCK" 2>/dev/null
}
trap cleanup EXIT
trap 'exit 0' HUP INT TERM
until [ "$(getprop sys.boot_completed)" = 1 ]; do sleep 2; done
[ ! -e "$MODDIR/disable" ] && [ ! -e "$MODDIR/remove" ] || exit 0
[ ! -e "$MODDIR/cover.log" ] || mv "$MODDIR/cover.log" "$MODDIR/cover.previous.log"
# v0.5: keep the policy alive indefinitely; back off when it dies quickly,
# reset the backoff after a stable two-minute run.
backoff=5
while true; do
 [ ! -e "$MODDIR/disable" ] && [ ! -e "$MODDIR/remove" ] || exit 0
 started=$(date +%s)
 CLASSPATH="$MODDIR/cover.jar" /system/bin/app_process /system/bin HallCover "$MODDIR" >> "$MODDIR/cover.log" 2>&1 &
 child=$!; echo "$child" > "$MODDIR/cover.pid"
 wait "$child"; rc=$?; child=
 ran=$(( $(date +%s) - started ))
 echo "SUPERVISOR exit=$rc ran=${ran}s backoff=${backoff}s" >> "$MODDIR/cover.log"
 [ "$ran" -ge 120 ] && backoff=5
 sleep "$backoff"
 backoff=$(( backoff * 2 )); [ "$backoff" -gt 120 ] && backoff=120
done
