#!/system/bin/sh
set -eu
M=${0%/*}
exec >> "$M/service.log" 2>&1
[ ! -e "$M/disable" ] && [ ! -e "$M/remove" ] || exit 0
n=0
while [ ! -f /apex/com.android.btservices/lib64/libbluetooth_jni.so ]; do
 n=$((n+1)); [ "$n" -le 60 ] || exit 1
 sleep 1
done
# APEX activation can finish after post-fs-data.  This fallback binds both
# the canonical and versioned paths before a later Bluetooth startup.
sh "$M/apply-live.sh"
p=$(pidof com.android.bluetooth || true)
[ -n "$p" ] || exit 0
loaded=$(awk '/libbluetooth_jni.so/{print $5;exit}' /proc/$p/maps)
expected=$(stat -c %i "$M/payload/libbluetooth_jni.so")
[ -n "$loaded" ] && [ "$loaded" != "$expected" ] || exit 0
state=$(dumpsys -t 3 bluetooth_manager | sed -n 's/^  state: //p' | head -1)
case "$state" in ON|OFF) ;; *) echo DEFER_TRANSITION_IN_PROGRESS; exit 0;; esac
[ "$state" != ON ] || cmd bluetooth_manager disable
sleep 2
p=$(pidof com.android.bluetooth || true)
if [ -n "$p" ] && tr '\000' '\n' < /proc/$p/cmdline | grep -qx com.android.bluetooth; then kill -TERM "$p"; fi
[ "$state" != ON ] || cmd bluetooth_manager enable
echo RELOADED_BLUETOOTH_FOR_NATIVE_HID_COMPATIBILITY
