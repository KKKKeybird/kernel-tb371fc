#!/system/bin/sh
M=${0%/*}
/system/bin/nsenter -t 1 -m -- sh "$M/apply.sh" > "$M/boot.log" 2>&1
