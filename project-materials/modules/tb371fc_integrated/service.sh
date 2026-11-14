#!/system/bin/sh
case "$0" in
    */*) MODDIR=$(readlink -f "${0%/*}");;
    *) MODDIR=$(pwd -P);;
esac
COMP="$MODDIR/components"
LOG=/data/adb/tb371fc-integrated.log
STATE=/data/adb/tb371fc-integrated
if [ -f "$MODDIR/disable" ] || [ -f "$MODDIR/remove" ]; then exit 0; fi
[ "$(cat /data/vendor/tb371fc/build-id 2>/dev/null)" = TB371FC-HyperOS3-20261103-full ] || exit 1
case "$(uname -v)" in "#25 SMP PREEMPT "*) ;; *) exit 1;; esac
mkdir -p "$STATE"
BOOT_ID=$(cat /proc/sys/kernel/random/boot_id 2>/dev/null || true)
if [ -n "$BOOT_ID" ] && [ "$(cat "$STATE/started-boot" 2>/dev/null)" = "$BOOT_ID" ]; then
    exit 0
fi
LOCK=/dev/tb371fc_integrated.starting
mkdir -m 700 "$LOCK" 2>/dev/null || exit 0
trap 'rmdir "$LOCK" 2>/dev/null || true' EXIT HUP INT TERM
[ -f "$LOG" ] && [ "$(wc -c < "$LOG")" -gt 262144 ] && mv "$LOG" "$LOG.previous"
echo "$(date '+%F %T') integrated service start" >> "$LOG"
setprop sys.tb371fc.integrated waiting
n=0
while [ "$(getprop sys.boot_completed)" != 1 ] && [ "$n" -lt 180 ]; do
    sleep 2
    n=$((n + 1))
done
[ "$(getprop sys.boot_completed)" = 1 ] || {
    setprop sys.tb371fc.integrated boot_timeout
    exit 1
}

battery_ready() {
    status=/dev/tb371fc_battery_stats/status
    pidfile=/dev/tb371fc_battery_stats/pid
    [ -r "$status" ] && [ -r "$pidfile" ] || return 1
    pid=$(cat "$pidfile")
    case "$pid" in ''|*[!0-9]*) return 1;; esac
    [ -r "/proc/$pid/cmdline" ] || return 1
    tr '\000' '\n' < "/proc/$pid/cmdline" |
        grep -F "$COMP/tb371fc_battery_stats/daemon.sh" >/dev/null || return 1
    sample=$(sed -n 's/^time=\([0-9][0-9]*\).*/\1/p' "$status" | head -1)
    case "$sample" in ''|*[!0-9]*) return 1;; esac
    now=$(date +%s)
    [ $((now - sample)) -le 120 ] || return 1
    for supply in bms battery; do
        for property in charge_counter charge_full charge_full_design; do
            target=$(readlink -f "/sys/class/power_supply/$supply/$property")
            awk -v t="$target" -v r="/tb371fc_battery_stats/$property" '$5 == t && $4 == r && / - tmpfs / { found=1 } END { exit !found }' /proc/1/mountinfo || return 1
        done
    done
    grep -q 'design_mah=10200' "$status"
}

start_component() {
    id=$1
    dir=$2
    script=$3
    if [ -f "$dir/disable" ] || [ -f "$dir/remove" ]; then
        setprop "sys.tb371fc.integrated.$id" disabled
        echo "$(date '+%F %T') $id skipped by component disable marker" >> "$LOG"
        return 1
    fi
    [ -f "$script" ] || {
        setprop "sys.tb371fc.integrated.$id" missing
        echo "$(date '+%F %T') $id missing: $script" >> "$LOG"
        return 1
    }
    sh "$script" >>"$LOG" 2>&1 &
    echo "$(date '+%F %T') $id launch pid=$!" >> "$LOG"
}

start_component battery "$COMP/tb371fc_battery_stats" "$COMP/tb371fc_battery_stats/service.sh"
start_component system "$COMP/tb371fc_system_compat" "$COMP/tb371fc_system_compat/service.sh"
start_component pen "$COMP/tb371fc_pen_lockfix" "$COMP/tb371fc_pen_lockfix/service.sh"
start_component account "$COMP/tb371fc_account_compat" "$COMP/tb371fc_account_compat/service.sh"
start_component split "$COMP/tb371fc_split_compat" "$COMP/tb371fc_split_compat/service.sh"
start_component kernel "$COMP/tb371fc_kernel_updater" "$COMP/tb371fc_kernel_updater/service.sh"

n=0
while [ "$n" -lt 45 ]; do
    if battery_ready; then
        setprop sys.tb371fc.integrated.battery ready
        start_component stock_charge "$COMP/tb371fc_stock_charge" "$COMP/tb371fc_stock_charge/service.sh"
        m=0
        while [ "$m" -lt 20 ]; do
            [ -r /dev/tb371fc_stock_charge/status ] &&
                grep -q '^time=[0-9]' /dev/tb371fc_stock_charge/status && break
            sleep 1
            m=$((m + 1))
        done
        [ -r /dev/tb371fc_stock_charge/status ] || {
            setprop sys.tb371fc.integrated stock_failed
            exit 1
        }
        k=0
        while [ "$k" -lt 150 ]; do
            account_state=$(getprop sys.tb371fc.account_compat)
            split_state=$(getprop sys.tb371fc.split_compat)
            pen_state=$(getprop sys.tb371fc.pen_lockfix)
            account_ok=0; split_ok=0; pen_ok=0
            case "$account_state" in safe_ready|disabled|missing) account_ok=1;; esac
            case "$split_state" in ready|disabled|missing|failed) split_ok=1;; esac
            case "$pen_state" in running|disabled|missing|failed) pen_ok=1;; esac
            if [ "$account_ok" = 1 ] && [ "$split_ok" = 1 ] && [ "$pen_ok" = 1 ] &&
               [ "$(getprop init.svc.tb371fc-speaker-rotation)" = running ] &&
               [ "$(getprop init.svc.tb371fc-hall-cover)" = running ]; then
                if [ "$split_state" = failed ] || [ "$pen_state" = failed ]; then
                    setprop sys.tb371fc.integrated degraded
                else
                    setprop sys.tb371fc.integrated ready
                fi
                break
            fi
            sleep 1
            k=$((k + 1))
        done
        [ "$k" -lt 150 ] || setprop sys.tb371fc.integrated degraded
        echo "$(date '+%F %T') battery ready; stock charge started; component states account=$account_state split=$split_state pen=$pen_state" >> "$LOG"
        [ -n "$BOOT_ID" ] && printf '%s\n' "$BOOT_ID" > "$STATE/started-boot"
        exit 0
    fi
    sleep 2
    n=$((n + 1))
done

setprop sys.tb371fc.integrated.battery failed
setprop sys.tb371fc.integrated stock_blocked_battery
echo "$(date '+%F %T') battery did not become ready; stock charge not started" >> "$LOG"
setprop ctl.start lwky_smart_charge 2>/dev/null || true
exit 1
