#!/system/bin/sh
# No swapoff/reset, backing-file allocation, flush daemon or polling service.
# Run before system_perf_init/zygote, so native system_server can set its quota.
MODDIR=${0%/*}
ZRAM=/sys/block/zram0
LOG=$MODDIR/boot-status.txt
umask 077
{
    date
    if [ ! -r "$ZRAM/idle_stat" ] || [ ! -e "$ZRAM/backing_dev" ]; then
        echo 'Matched kernel ABI absent; left zram untouched.'
        exit 0
    fi
    for attr in idle writeback_limit writeback_limit_enable; do
        if ! chown 0:1000 "$ZRAM/$attr" || ! chmod 0664 "$ZRAM/$attr"; then
            echo "ERROR: could not grant native service access to $attr"
            exit 1
        fi
        ls -ln "$ZRAM/$attr"
    done
    echo 'Native ExtendM permissions ready; initialization and quota are owned by HyperOS.'
} > "$LOG" 2>&1
