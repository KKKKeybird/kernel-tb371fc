#!/system/bin/sh
MODDIR=${0%/*}
if [ "${1:-}" = --stop ]; then
    [ -d /dev/tb371fc_stock_charge ] || exit 0
    touch /dev/tb371fc_stock_charge/stop
    i=0
    while [ -d /dev/tb371fc_stock_charge ] && [ "$i" -lt 15 ]; do sleep 1; i=$((i+1)); done
    [ ! -d /dev/tb371fc_stock_charge ]
    exit $?
fi
while [ "$(getprop sys.boot_completed)" != 1 ]; do sleep 2; done
[ ! -f "$MODDIR/disable" ] && [ ! -f "$MODDIR/remove" ] || exit 0
sh "$MODDIR/supervisor.sh" </dev/null >/dev/null 2>&1 &
