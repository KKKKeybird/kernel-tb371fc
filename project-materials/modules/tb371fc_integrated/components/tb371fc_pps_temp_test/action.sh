#!/system/bin/sh
MODDIR=${0%/*}
echo '未实测可用性：开始条件检查；符合条件才运行一次限时实验。'
echo '如不符合PPS/电量/温度条件，会拒绝启动。日志：/data/adb/tb371fc-pps-temp-test/logs/'
exec su -M -c "sh '$MODDIR/run-test.sh'"
