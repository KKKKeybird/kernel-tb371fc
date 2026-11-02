#!/system/bin/sh
MODDIR=${0%/*}
while [ "$(getprop sys.boot_completed)" != 1 ]; do sleep 3; done
[ -e "$MODDIR/disable" ] && exit 0
[ -e "$MODDIR/remove" ] && exit 0
# Cover late starts without a polling loop or restarting unrelated services.
for name in vendor.mfidoca cit_vendorhal_service citsensor-hal-1-1; do
  stop "$name" 2>/dev/null
done
