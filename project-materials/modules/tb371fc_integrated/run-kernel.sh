#!/system/bin/sh
set -eu
MODDIR=$(readlink -f "${0%/*}")
K="$MODDIR/components/tb371fc_kernel_updater"
PAYLOAD="$K/payload/boot.img"
URL=https://github.com/KKKKeybird/kernel-tb371fc/releases/download/module-kernel-updater-v2.2-hyperos25/TB371FC-KernelUpdater-v2.2-hyperos25.zip
SHA=a9fb2b73e43e8e8bb1b7a8456bf772e6729b68c110ed57bf7f2c1d1cf417b981
BB=/data/adb/ksu/bin/busybox
if [ ! -f "$PAYLOAD" ]; then
    command -v unzip >/dev/null 2>&1 || { echo '缺少unzip，无法解出内核载荷'; exit 1; }
    mkdir -p "$K/payload"
    TMP="$K/payload/kernel-updater.zip"
    echo '下载已固定版本的内核更新包……'
    "$BB" wget -O "$TMP" "$URL"
    unzip -p "$TMP" payload/boot.img > "$PAYLOAD"
    rm -f "$TMP"
fi
actual=$(sha256sum "$PAYLOAD" | cut -d' ' -f1)
[ "$actual" = "$SHA" ] || { echo "内核载荷哈希错误：$actual"; exit 1; }
exec sh "$K/action.sh"
