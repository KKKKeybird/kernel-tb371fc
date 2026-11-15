#!/system/bin/sh
set -eu
MODDIR=$(readlink -f "${0%/*}")
K="$MODDIR/components/tb371fc_kernel_updater"
PAYLOAD="$K/payload/boot.img"
URL=https://github.com/KKKKeybird/kernel-tb371fc/releases/download/module-kernel-updater-v2.2-hyperos25/TB371FC-KernelUpdater-v2.2-hyperos25.zip
SHA=a9fb2b73e43e8e8bb1b7a8456bf772e6729b68c110ed57bf7f2c1d1cf417b981
BB=/data/adb/ksu/bin/busybox
AUTO=0
for arg in "$@"; do
    case "$arg" in
        --auto) AUTO=1;;
        *) echo "未知参数：$arg"; exit 2;;
    esac
done

if [ "$AUTO" = 1 ]; then
    slot=$(getprop ro.boot.slot_suffix)
    case "$slot" in _a|_b) ;; *) echo "活动槽无效：$slot"; exit 1;; esac
    current=$(sha256sum "/dev/block/by-name/boot$slot" | cut -d' ' -f1)
    if [ "$current" = "$SHA" ]; then
        echo '活动槽已经运行已验证的 HyperOS #25 boot；无需重复写入。'
        exit 0
    fi
fi

if [ ! -f "$PAYLOAD" ]; then
    command -v unzip >/dev/null 2>&1 || { echo '缺少unzip，无法解出内核载荷'; exit 1; }
    mkdir -p "$K/payload"
    TMP="$K/payload/kernel-updater.zip"
    IMG_TMP="$K/payload/boot.img.download"
    rm -f "$TMP" "$IMG_TMP"
    echo '下载已固定版本的内核更新包……'
    "$BB" wget -T 30 -t 3 -O "$TMP" "$URL" || {
        rm -f "$TMP" "$IMG_TMP"
        echo '内核包下载失败或超时；boot 未改写'
        exit 1
    }
    unzip -p "$TMP" payload/boot.img > "$IMG_TMP"
    rm -f "$TMP"
    actual=$(sha256sum "$IMG_TMP" | cut -d' ' -f1)
    [ "$actual" = "$SHA" ] || {
        rm -f "$IMG_TMP"
        echo "内核载荷哈希错误：$actual"
        exit 1
    }
    mv "$IMG_TMP" "$PAYLOAD"
fi
actual=$(sha256sum "$PAYLOAD" | cut -d' ' -f1)
[ "$actual" = "$SHA" ] || { echo "内核载荷哈希错误：$actual"; exit 1; }
if [ "$AUTO" = 1 ]; then
    exec sh "$K/action.sh" --auto
fi
exec sh "$K/action.sh"
