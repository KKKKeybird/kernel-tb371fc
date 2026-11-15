#!/system/bin/sh
BB=/data/adb/ksu/bin/busybox
[ -x "$BB" ] || exit 1
if [ ! /proc/$$/exe -ef "$BB" ]; then exec "$BB" sh "$0" "$@"; fi
MODDIR=$(readlink -f "${0%/*}")
R=/dev/tb371fc_touch_direction
OWNER=$(readlink -f "$MODDIR/../..")
case "$OWNER" in /data/adb/modules/tb371fc_integrated) ;; *) OWNER="$MODDIR";; esac
umask 077
mkdir -p "$R" || exit 1
owned() {
    case "$1" in ''|*[!0-9]*) return 1;; esac
    [ -r "/proc/$1/cmdline" ] || return 1
    tr '\000' '\n' < "/proc/$1/cmdline" | grep -Fx TouchDirectionBridge >/dev/null &&
        tr '\000' '\n' < "/proc/$1/cmdline" | grep -Fx "$R" >/dev/null
}
if [ "${1:-}" = --stop ]; then
    touch "$R/stop"
    pid=$(cat "$R/pid" 2>/dev/null)
    if owned "$pid"; then
        sleep 1
        owned "$pid" && kill -TERM "$pid"
    fi
    exit 0
fi
[ ! -e "$OWNER/disable" ] && [ ! -e "$OWNER/remove" ] && [ ! -e "$MODDIR/disable" ] && [ ! -e "$MODDIR/remove" ] || exit 0
[ "$(cat /data/vendor/tb371fc/build-id 2>/dev/null)" = TB371FC-HyperOS3-20261103-full ] || exit 1
case "$(uname -v)" in '#25 SMP PREEMPT '*) ;; *) exit 1;; esac
[ -r /proc/panel_direction ] && [ -w /proc/panel_direction ] || exit 1
exec 9>"$R/supervisor.lock"
"$BB" flock -n 9 || exit 0
rm -f "$R/stop"
child=
cleanup() {
    trap - EXIT HUP INT TERM
    if owned "$child"; then kill -TERM "$child"; wait "$child"; fi
    # Also cover a killed Java process. Only undo our last owned cached value.
    last=$(cat "$R/last_written" 2>/dev/null)
    before=$(cat "$R/before" 2>/dev/null)
    case "$(cat /proc/panel_direction 2>/dev/null)" in
        'Vertical Direction!(0 degree)') current=0;;
        'Right Up Direction!(270 degree)') current=1;;
        'Vertical reverse Direction!(180 degree)') current=2;;
        'Left Up Direction!(90 degree)') current=3;;
        *) current=unknown;;
    esac
    case "$before" in 0|1|2|3)
        [ "$current" != "$last" ] || printf '%s\n' "$before" > /proc/panel_direction;;
    esac
    rm -f "$R/pid"
}
trap cleanup EXIT
trap 'exit 0' HUP INT TERM
[ ! -f "$R/bridge.log" ] || [ "$(wc -c < "$R/bridge.log")" -lt 262144 ] || mv "$R/bridge.log" "$R/bridge.previous.log"
rm -f "$R/last_written"
CLASSPATH="$MODDIR/touch-direction.jar" /system/bin/app_process /system/bin TouchDirectionBridge "$R" "$OWNER" "$MODDIR" 9>&- >> "$R/bridge.log" 2>&1 &
child=$!
echo "$child" > "$R/pid"
wait "$child"
