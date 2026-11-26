#!/system/bin/sh
MODDIR=${0%/*}
COMP="$MODDIR/components"
echo 'TB371FC R8 整合维护与兼容修复 1.4.1'
echo "integrated=$(getprop sys.tb371fc.integrated)"
echo "battery=$(getprop sys.tb371fc.integrated.battery) $(cat /dev/tb371fc_battery_stats/status 2>/dev/null || true)"
echo "account=$(getprop sys.tb371fc.account_compat)"
echo "split=$(getprop sys.tb371fc.split_compat)"
echo "pen=$(getprop sys.tb371fc.pen_lockfix)"
echo "stock=$(cat /dev/tb371fc_stock_charge/status 2>/dev/null || echo inactive)"
echo "speaker=$(getprop init.svc.tb371fc-speaker-rotation)"
echo "hall=$(getprop init.svc.tb371fc-hall-cover)"
echo "kernel=$(uname -r)"

echo "PPS实验未自动启用；检查条件：su -c 'sh $COMP/tb371fc_pps_temp_test/run-test.sh --check'"
echo "自动内核更新日志：/data/adb/tb371fc-integrated/kernel-auto/status.log"
echo "恢复旧独立模块：su -c 'sh $MODDIR/tools/rollback-migration.sh --modules-dir /data/adb/modules --state-dir /data/adb/tb371fc-integrated/migration-v1'"

echo "touch_direction=$(cat /dev/tb371fc_touch_direction/status 2>/dev/null || echo inactive)"
echo "pen_icon=$(cat /dev/tb371fc_pen_icon/status 2>/dev/null || echo inactive)"
echo "geq_guard=$(cat /dev/tb371fc_geq_guard/status 2>/dev/null || echo inactive)"

exec sh "$MODDIR/run-kernel.sh"
