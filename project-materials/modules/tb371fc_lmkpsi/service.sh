#!/system/bin/sh
# Safety net: if some vendor step rewrites the prop or restarts lmkd in legacy mode,
# re-apply once boot completes and once more 60s later (then exit).
MODDIR=${0%/*}
LOG=$MODDIR/boot-status.txt
umask 077
until [ "$(getprop sys.boot_completed)" = "1" ]; do sleep 5; done
sleep 30
RP=/data/adb/ksu/bin/resetprop
[ -x "$RP" ] || RP=$(command -v resetprop)
for i in 1 2; do
    cur=$(getprop ro.lmk.use_minfree_levels)
    if [ "$cur" != "false" ]; then
        "$RP" ro.lmk.use_minfree_levels false
        setprop lmkd.reinit 1
        echo "$(date) re-applied psi mode (was $cur)" >> "$LOG"
    fi
    sleep 60
done
echo "$(date) lmkpsi service done; use_minfree_levels=$(getprop ro.lmk.use_minfree_levels)" >> "$LOG"
