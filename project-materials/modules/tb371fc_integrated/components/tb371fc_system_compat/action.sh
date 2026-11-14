#!/system/bin/sh
set -u
M=${0%/*}

echo 'TB371FC R8 系统服务兼容修复'
echo "kernel=$(uname -r)"
echo "speaker_service=$(getprop init.svc.tb371fc-speaker-rotation)"
echo "hall_service=$(getprop init.svc.tb371fc-hall-cover)"
echo "speaker_status=$(cat /data/vendor/tb371fc/speaker_rotation/status.txt 2>/dev/null)"
tinymix TFA9894_CHAN_SELECTOR 2>/dev/null || true
tinymix 'SpkRot 0 Matrix' 2>/dev/null || true
echo '最近日志：'
tail -20 /data/vendor/tb371fc/speaker_rotation/rotation.log 2>/dev/null || true
