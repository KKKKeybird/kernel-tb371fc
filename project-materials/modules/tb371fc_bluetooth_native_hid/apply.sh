#!/system/bin/sh
set -eu
M=${0%/*}
[ ! -e "$M/disable" ] && [ ! -e "$M/remove" ] || exit 0
OLD=fa15099c63a3095b5f918c989fd3e1945067a2e608213de1810de7baebf5e894
NEW=2b5e48447b097584e0828fafe4656a4c4e959aea97ae3d18574c83b282cf0da6
for T in /apex/com.android.btservices*/lib64/libbluetooth_jni.so; do
[ -f "$T" ] || continue
v=$(sha256sum "$T" | cut -d ' ' -f1)
[ "$v" != "$NEW" ] || continue
[ "$v" = "$OLD" ] || { echo "SKIPPED unfamiliar Bluetooth library $v"; exit 1; }
[ "$(sha256sum "$M/payload/libbluetooth_jni.so" | cut -d ' ' -f1)" = "$NEW" ]
chcon u:object_r:system_lib_file:s0 "$M/payload/libbluetooth_jni.so"
mount --bind "$M/payload/libbluetooth_jni.so" "$T"
[ "$(sha256sum "$T" | cut -d ' ' -f1)" = "$NEW" ]
echo "BOUND_NATIVE_HID_LIBRARY namespace=$(readlink /proc/self/ns/mnt)"
done
