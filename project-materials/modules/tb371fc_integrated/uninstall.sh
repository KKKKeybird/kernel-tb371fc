#!/system/bin/sh
case "$0" in
    */*) MODDIR=$(readlink -f "${0%/*}");;
    *) MODDIR=$(pwd -P);;
esac
COMP="$MODDIR/components"
BB=/data/adb/ksu/bin/busybox
STATE=/data/adb/tb371fc-integrated/migration-v1
if [ -f "$STATE/newly-disabled.txt" ] && [ -x "$MODDIR/tools/rollback-migration.sh" ]; then
    sh "$MODDIR/tools/rollback-migration.sh" --modules-dir /data/adb/modules --state-dir "$STATE"
    exit $?
fi
if [ -x "$COMP/tb371fc_stock_charge/service.sh" ]; then
    sh "$COMP/tb371fc_stock_charge/service.sh" --stop
fi
if [ -x "$COMP/tb371fc_battery_stats/daemon.sh" ]; then
    sh "$COMP/tb371fc_battery_stats/daemon.sh" --stop
fi
if [ -e /dev/tb371fc_pps_temp_probe ] && [ -x "$COMP/tb371fc_pps_temp_test/rollback.sh" ]; then
    su -M -c "sh '$COMP/tb371fc_pps_temp_test/rollback.sh'"
fi
for spec in \
    "/system_ext/priv-app/RtMiCloudSDK/RtMiCloudSDK.apk:$COMP/tb371fc_account_compat/payload/RtMiCloudSDK.apk" \
    "/system_ext/priv-app/MiuiSystemUI/MiuiSystemUI.apk:$COMP/tb371fc_split_compat/payload/MiuiSystemUI.apk"
do
    target=${spec%%:*}
    payload=${spec#*:}
    for pid in 1 $("$BB" pidof zygote64 2>/dev/null) $("$BB" pidof zygote 2>/dev/null); do
        [ -d "/proc/$pid" ] || continue
        if "$BB" nsenter -t "$pid" -m -- "$BB" cmp -s "$payload" "$target"; then
            "$BB" nsenter -t "$pid" -m -- "$BB" umount -l "$target" || true
        fi
    done
done
setprop sys.tb371fc.integrated uninstalled
echo '整合模块卸载已停止其私有服务；旧的FindDevice隐藏状态和Google Play数据未改动。重启后系统覆盖消失。'
