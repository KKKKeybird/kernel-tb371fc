#!/system/bin/sh
MODDIR=${0%/*}
/system/bin/sh "$MODDIR/stop-runtime.sh"
resetprop ro.vendor.mslg.rootfs.version rootfs-24.11.22.tgz
# Keep the runtime home and documents. Do not clear WPS or MSLgRdp app data.
