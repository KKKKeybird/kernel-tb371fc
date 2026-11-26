#!/system/bin/sh
set -eu
MODDIR=$(readlink -f "${0%/*}")
exec sh "$MODDIR/components/tb371fc_kernel_updater/action.sh" "$@"
