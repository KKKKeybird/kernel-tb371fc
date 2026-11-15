#!/system/bin/sh
M=$(readlink -f "${0%/*}")
OWNER=$(readlink -f "$M/../..")
R=/dev/tb371fc_geq_guard
if [ -e "$M/disable" ] || [ -e "$M/remove" ] ||
   [ -e "$OWNER/disable" ] || [ -e "$OWNER/remove" ] ||
   [ -e "$R/stop" ] || [ ! -d "$M" ]; then
    pid=$(cat "$R/supervisor.pid" 2>/dev/null)
    case "$pid" in ''|*[!0-9]*) exit 0;; esac
    if tr '\000' '\n' < "/proc/$pid/cmdline" 2>/dev/null |
        grep -Fx "$M/service.sh" >/dev/null; then
        kill -TERM "$pid"
    fi
fi
