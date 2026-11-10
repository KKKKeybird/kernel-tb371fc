#!/system/bin/sh
echo 'TB371FC R8 电池容量统计修复'
if [ -f /dev/tb371fc_battery_stats/status ]; then
    cat /dev/tb371fc_battery_stats/status
else
    echo '当前未运行，请检查模块启用状态和启动日志。'
fi
echo '容量单位：mAh；设计基准10200mAh。满充容量为电量计估算值。'
echo '日志：/data/adb/tb371fc-battery-stats.log'
