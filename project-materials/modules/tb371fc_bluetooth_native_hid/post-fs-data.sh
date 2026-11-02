#!/system/bin/sh
M=${0%/*}
nsenter -t 1 -m -- sh "$M/apply.sh" > "$M/mount.log" 2>&1
