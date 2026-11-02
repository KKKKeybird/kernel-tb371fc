#!/system/bin/sh
M=${0%/*}
sh "$M/rollback-live.sh"
# This restores the effective default mount-visibility policy; never grants su.
[ ! -x "$M/bluetooth-profile" ] || "$M/bluetooth-profile" restore
