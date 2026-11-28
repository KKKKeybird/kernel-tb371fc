#!/system/bin/sh
set -eu

root=${1:?WPS rootfs required}
config="$root/etc/cups/cupsd.conf"
log_dir="$root/var/log/cups"
marker="$root/etc/cups/.tb371fc-cups-logs-cleared"
limit=1048576

[ -f "$config" ] && [ ! -L "$config" ] && [ ! -L "${config%/*}" ] || exit 1
[ ! -L "$log_dir" ] || exit 1

if [ ! -f "$marker" ]; then
    # The WPS launcher calls this before starting MSLGd and CUPS.
    for log_file in "$log_dir"/error_log "$log_dir"/access_log "$log_dir"/page_log \
        "$log_dir"/error_log.* "$log_dir"/access_log.* "$log_dir"/page_log.*; do
        [ -f "$log_file" ] && [ ! -L "$log_file" ] || continue
        [ "$(stat -c %h "$log_file")" = 1 ] || continue
        case "${log_file##*/}" in
            error_log|access_log|page_log) : > "$log_file" ;;
            *) rm -f -- "$log_file" ;;
        esac
    done
    (umask 077; : > "$marker")
fi

# Leave one active limit, including configs with no directive or duplicates.
if ! awk -v limit="$limit" '
    tolower($1) == "maxlogsize" { count++; if ($2 != limit) invalid = 1 }
    END { exit !(count == 1 && !invalid) }
' "$config"; then
    temporary=$(mktemp "${config}.tb371fc.XXXXXX")
    trap 'rm -f -- "$temporary"' EXIT
    cp -p "$config" "$temporary"
    awk -v limit="$limit" '
        tolower($1) != "maxlogsize" { print }
        END { print "MaxLogSize " limit }
    ' "$config" > "$temporary"
    mv -f -- "$temporary" "$config"
fi
