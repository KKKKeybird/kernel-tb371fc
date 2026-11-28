#!/system/bin/sh
set -eu

source=${1:?WPS launcher required}
target=${2:?module overlay path required}
hook='/system/bin/sh "$CODE/cups-log-limit.sh" "$ROOT" || log "Could not apply CUPS log maintenance"'

mkdir -p "${target%/*}"
temporary=$(mktemp "${target}.XXXXXX")
trap 'rm -f -- "$temporary"' EXIT
awk -v hook="$hook" '
    $0 == hook { next }
    /^printf .*MSLGd launch: / { print hook; anchors++ }
    { print }
    END { if (anchors != 1) exit 1 }
' "$source" > "$temporary"
sh -n "$temporary"
chmod 0755 "$temporary"
mv -f -- "$temporary" "$target"
