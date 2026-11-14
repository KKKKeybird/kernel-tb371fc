#!/system/bin/sh
if [ -r /dev/tb371fc_stock_charge/status ]; then
    cat /dev/tb371fc_stock_charge/status
else
    echo 'Bridge inactive. See /data/adb/tb371fc-stock-charge.log; reboot after installation.'
fi
echo "legacy=$(getprop init.svc.lwky_smart_charge)"
tail -10 /data/adb/tb371fc-stock-charge.log 2>/dev/null
