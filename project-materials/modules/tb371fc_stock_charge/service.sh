#!/system/bin/sh
MODDIR=${0%/*}
if [ "${1:-}" = --stop ]; then
    [ -d /dev/tb371fc_stock_charge ] || exit 0
    R=/dev/tb371fc_stock_charge
    owner=$(cat "$R/pid" 2>/dev/null)
    case "$owner" in ''|*[!0-9]*) owner=0;; esac
    if [ "$owner" = 0 ] || [ ! -r "/proc/$owner/cmdline" ]; then
        old=$(cat "$R/moddir" 2>/dev/null)
        [ -n "$old" ] && [ -f "$old/rollback.sh" ] || exit 1
        child=$(cat "$R/child" 2>/dev/null)
        case "$child" in ''|*[!0-9]*) child=0;; esac
        if [ "$child" != 0 ] && [ -r "/proc/$child/cmdline" ] && tr '\000' '\n' < "/proc/$child/cmdline" | grep -F tb371fc-charge-bridge >/dev/null; then
            kill -TERM "$child" 2>/dev/null; sleep 2
            kill -0 "$child" 2>/dev/null && kill -KILL "$child" 2>/dev/null
        fi
        sh "$old/rollback.sh" --stale
        exit $?
    fi
    touch /dev/tb371fc_stock_charge/stop
    i=0
    while [ -d /dev/tb371fc_stock_charge ] && [ "$i" -lt 15 ]; do sleep 1; i=$((i+1)); done
    [ ! -d /dev/tb371fc_stock_charge ]
    exit $?
fi
while [ "$(getprop sys.boot_completed)" != 1 ]; do sleep 2; done
[ ! -f "$MODDIR/disable" ] && [ ! -f "$MODDIR/remove" ] || exit 0
sh "$MODDIR/supervisor.sh" </dev/null >/dev/null 2>&1 &
