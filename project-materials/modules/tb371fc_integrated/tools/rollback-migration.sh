#!/system/bin/sh
set -eu
case "$0" in
    */*) ROOT=$(readlink -f "${0%/*}/..");;
    *) ROOT=$(readlink -f "$(pwd -P)/..");;
esac
MODULES_DIR=
STATE_DIR=
while [ "$#" -gt 0 ]; do
    case "$1" in
        --modules-dir) [ "$#" -ge 2 ] || exit 2; MODULES_DIR=$2; shift 2;;
        --state-dir) [ "$#" -ge 2 ] || exit 2; STATE_DIR=$2; shift 2;;
        *) echo "Unknown argument: $1"; exit 2;;
    esac
done
[ "$MODULES_DIR" = /data/adb/modules ] || { echo 'Refusing unexpected modules directory'; exit 1; }
[ -f "$STATE_DIR/newly-disabled.txt" ] || { echo 'Migration record missing'; exit 1; }
[ ! -L "$STATE_DIR/newly-disabled.txt" ] || { echo 'Migration record is a symlink'; exit 1; }

while IFS= read -r id; do
    case "$id" in
        tb371fc_account_compat|tb371fc_split_compat|tb371fc_pen_lockfix|tb371fc_battery_stats|tb371fc_pps_temp_test|tb371fc_system_compat|tb371fc_kernel_updater|tb371fc_stock_charge|'')
            ;;
        *) echo "Refusing unknown migration id: $id"; exit 1;;
    esac
done < "$STATE_DIR/newly-disabled.txt"

touch "$ROOT/disable"
printf 'ROLLBACK_PENDING\n' > "$STATE_DIR/rollback-phase"
if [ -x "$ROOT/components/tb371fc_stock_charge/service.sh" ]; then
    sh "$ROOT/components/tb371fc_stock_charge/service.sh" --stop || true
fi
if [ -x "$ROOT/components/tb371fc_battery_stats/daemon.sh" ]; then
    sh "$ROOT/components/tb371fc_battery_stats/daemon.sh" --stop || true
fi

while IFS= read -r id; do
    case "$id" in
        tb371fc_account_compat|tb371fc_split_compat|tb371fc_pen_lockfix|tb371fc_battery_stats|tb371fc_pps_temp_test|tb371fc_system_compat|tb371fc_kernel_updater|tb371fc_stock_charge)
            [ -d "$MODULES_DIR/$id" ] && rm -f "$MODULES_DIR/$id/disable"
            ;;
        '') ;;
        *) echo "Refusing unknown migration id: $id"; exit 1;;
    esac
done < "$STATE_DIR/newly-disabled.txt"

printf 'ROLLED_BACK\n' > "$STATE_DIR/rollback-phase"
echo 'Integrated module disabled; legacy modules from this migration are enabled again.'
echo 'Reboot to restore the original overlays and component services.'
