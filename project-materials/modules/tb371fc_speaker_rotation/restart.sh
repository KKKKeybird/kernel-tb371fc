#!/system/bin/sh
set -e
M=${0%/*}
pid=$(cat "$M/supervisor.lock/pid" 2>/dev/null || true)
case "$pid" in
 ''|*[!0-9]*) ;;
 *) if [ -r "/proc/$pid/cmdline" ] && tr '\000' ' ' < "/proc/$pid/cmdline" | grep -Fq "$M/supervisor.sh"; then kill -TERM "$pid"; fi;;
esac
for n in 1 2 3 4 5; do
 [ ! -d "$M/supervisor.lock" ] && break
 sleep 1
done
[ ! -d "$M/supervisor.lock" ] || { echo 'Supervisor still exiting; refusing duplicate start'; exit 1; }
sh "$M/service.sh"
