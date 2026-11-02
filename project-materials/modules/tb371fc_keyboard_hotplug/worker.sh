#!/system/bin/sh
MODDIR=${0%/*}
HID=0018:17EF:6175.0001
BASE=/sys/bus/hid/devices/$HID
DRIVER=/sys/bus/hid/drivers/hid-multitouch
attempts=0
while [ ! -f "$MODDIR/disable" ] && [ ! -f "$MODDIR/remove" ]; do
  sleep 2
  [ -d "$BASE" ] || continue
  for node in "$BASE"/input/input*; do
    [ -r "$node/name" ] || continue
    [ "$(cat "$node/name")" = 'Lenovo Keyboard Pack for Spinel Touchpad' ] || continue
    props=$(cat "$node/properties" 2>/dev/null)
    # Healthy hardware advertises POINTER|BUTTONPAD (5).
    # Confirmed hotplug failure advertises only BUTTONPAD (4).
    if [ "$props" = 5 ]; then attempts=0; continue; fi
    [ "$props" = 4 ] || continue
    [ "$attempts" -lt 3 ] || continue
    [ "$(readlink "$BASE/driver" | sed 's|.*/||')" = hid-multitouch ] || continue
    attempts=$((attempts + 1))
    echo "$(date '+%F %T') recovering $node attempt=$attempts"
    if echo "$HID" > "$DRIVER/unbind"; then
      # Always attempt the corresponding bind immediately after a successful unbind.
      echo "$HID" > "$DRIVER/bind" || {
        sleep 1
        echo "$HID" > "$DRIVER/bind"
      }
    fi
    break
  done
done
