#!/system/bin/sh
MODDIR=$(readlink -f "${0%/*}")
exec su -M -c "sh '$MODDIR/components/tb371fc_pps_temp_test/action.sh'"
