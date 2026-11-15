#!/system/bin/sh
echo "geq_guard=$(cat /dev/tb371fc_geq_guard/status 2>/dev/null || echo inactive)"
echo "property=$(getprop sys.tb371fc.geq_guard)"
tail -20 /dev/tb371fc_geq_guard/generation.log 2>/dev/null || true
