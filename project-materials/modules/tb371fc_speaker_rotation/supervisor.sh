#!/system/bin/sh
MODDIR=${0%/*}
LOCK="$MODDIR/supervisor.lock"
if ! mkdir "$LOCK" 2>/dev/null; then
 old=$(cat "$LOCK/pid" 2>/dev/null)
 case "$old" in
  ''|*[!0-9]*) ;;
  *)
   if [ -r "/proc/$old/cmdline" ] && tr '\000' ' ' < "/proc/$old/cmdline" | grep -Fq "$MODDIR/supervisor.sh"; then exit 0; fi
   ;;
 esac
 rm -f "$LOCK/pid"
 rmdir "$LOCK" 2>/dev/null || exit 0
 mkdir "$LOCK" 2>/dev/null || exit 0
fi
echo $$ > "$LOCK/pid"
# Keep the rotation service away from the low-memory killer: the device runs
# close to full RAM and an unprivileged app_process gets SIGKILLed otherwise.
echo -900 > /proc/$$/oom_score_adj 2>/dev/null
child=
cleanup() {
 trap - EXIT HUP INT TERM
 [ -z "$child" ] || kill -TERM "$child" 2>/dev/null
 rm -f "$LOCK/pid"
 rmdir "$LOCK" 2>/dev/null
}
trap cleanup EXIT
trap 'exit 0' HUP INT TERM
while [ "$(getprop sys.boot_completed)" != 1 ]; do sleep 3; done
while [ ! -e "$MODDIR/disable" ] && [ ! -e "$MODDIR/remove" ]; do
 size=$(wc -c < "$MODDIR/rotation.log" 2>/dev/null)
 [ "${size:-0}" -lt 262144 ] || mv "$MODDIR/rotation.log" "$MODDIR/rotation.previous.log"
 echo "SESSION $(date -u +%Y-%m-%dT%H:%M:%SZ)" >> "$MODDIR/rotation.log"
 CLASSPATH="$MODDIR/rotation.jar" /system/bin/app_process /system/bin SpeakerRotation "$MODDIR" >> "$MODDIR/rotation.log" 2>&1 &
 child=$!
 echo "$child" > "$MODDIR/rotation.pid"
 wait "$child"
 rc=$?
 child=
 echo "ROTATION_EXIT=$rc $(date -u +%Y-%m-%dT%H:%M:%SZ)"
 sleep 15
done
