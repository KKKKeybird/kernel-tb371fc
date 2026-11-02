#!/system/bin/sh
# Invoked at post-mount, before audio services consume the policy.
MODDIR=${0%/*}
TARGET=/vendor/etc/audio/audio_policy_configuration.xml
BASE=58cb492024a77638c65a95c0780d5d6ff172ca2645e33b3d24f1336b59d51ba0
status() { echo "$(date -u +%Y-%m-%dT%H:%M:%SZ) $*" > "$MODDIR/quad-status.txt"; }
[ ! -e "$MODDIR/disable" ] && [ ! -e "$MODDIR/remove" ] || exit 0
PARAM=
for p in /sys/module/*/parameters/tb371fc_dsp_quad; do
    [ -f "$p" ] || continue
    [ -z "$PARAM" ] || { status MULTIPLE_KERNEL_CONTROLS; exit 1; }
    PARAM=$p
done
[ -n "$PARAM" ] || { status LEGACY_KERNEL_STEREO_PRESERVED; exit 0; }
EXPECTED=$(sha256sum "$MODDIR/audio_policy_configuration.xml")
EXPECTED=${EXPECTED%% *}
CURRENT=$(sha256sum "$TARGET")
CURRENT=${CURRENT%% *}
if [ "$CURRENT" = "$EXPECTED" ] && [ "$(cat "$PARAM")" = Y ]; then
    status ACTIVE
    exit 0
fi
# Refuse to replace an independently changed policy.
[ "$CURRENT" = "$BASE" ] || { status POLICY_BASE_MISMATCH; exit 1; }
mount --bind "$MODDIR/audio_policy_configuration.xml" "$TARGET" || {
    status POLICY_BIND_FAILED; exit 1;
}
if ! echo 1 > "$PARAM" || [ "$(cat "$PARAM")" != Y ]; then
    umount "$TARGET"
    status KERNEL_ENABLE_FAILED
    exit 1
fi
status ACTIVE
