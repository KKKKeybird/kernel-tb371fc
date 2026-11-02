#!/system/bin/sh
M=${0%/*}
[ -e "$M/disable" ] && exit 0
[ -e "$M/remove" ] && exit 0
# A one-time pass also covers daemons started by earlier module hooks.
sh "$M/protect-live.sh" >> "$M/boot.log" 2>&1
