#!/system/bin/sh
MODDIR=${0%/*}
STUB=/data/adb/modules/meta-overlayfs/mnt/tb371fc_hal_compat/system/bin/tb371fc_hal_stub.sh
for target in /vendor/bin/fidoca /vendor/bin/hw/vendor.xiaomi.hardware.citvendorservice@1.0-service; do
  [ -e "$target" ] || continue
  if [ -x /system/bin/nsenter ]; then
    /system/bin/nsenter -t 1 -m -- mount -o bind "$STUB" "$target" 2>/dev/null
  else
    mount -o bind "$STUB" "$target" 2>/dev/null
  fi
done
for name in vendor.mfidoca cit_vendorhal_service citsensor-hal-1-1; do
  stop "$name" 2>/dev/null
done
