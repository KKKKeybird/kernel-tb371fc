#!/system/bin/sh
MODDIR=${0%/*}
while [ "$(getprop sys.boot_completed)" != "1" ]; do sleep 2; done
[ -e "$MODDIR/disable" ] && exit 0
[ -e "$MODDIR/remove" ] && exit 0
# Sanitize an old 144Hz choice once at boot; preserve subsequent valid choices.
for spec in system:peak_refresh_rate secure:user_refresh_rate secure:miui_refresh_rate; do
  namespace=${spec%:*}
  key=${spec#*:}
  value=$(settings get "$namespace" "$key")
  case "$value" in
    144|144.0|144.000*) settings put "$namespace" "$key" 120 ;;
  esac
done
value=$(settings get system min_refresh_rate)
case "$value" in 144|144.0|144.000*) settings delete system min_refresh_rate ;; esac
