#!/system/bin/sh
# lmkd starts during early init with ro.lmk.use_minfree_levels=true on this ROM,
# so it only watches legacy minfree levels and never reacts while zram absorbs pressure.
# Reset the ro prop before/after boot and ask lmkd to re-init (hot reload path from lmkd.rc).
MODDIR=${0%/*}
LOG=$MODDIR/boot-status.txt
umask 077
{
    date
    RP=/data/adb/ksu/bin/resetprop
    [ -x "$RP" ] || RP=$(command -v resetprop)
    if [ -z "$RP" ]; then echo "ERROR: resetprop not found"; exit 1; fi
    "$RP" ro.lmk.use_minfree_levels false || echo "WARN: resetprop failed"
    echo "ro.lmk.use_minfree_levels=$(getprop ro.lmk.use_minfree_levels)"
    # Only ask for reinit when lmkd is already up; otherwise it will read the prop at start.
    if [ -n "$(getprop init.svc.lmkd)" ]; then
        setprop lmkd.reinit 1
        echo "lmkd.reinit requested"
    fi
} > "$LOG" 2>&1
