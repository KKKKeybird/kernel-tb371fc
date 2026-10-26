#!/usr/bin/env bash
# Repack the freshly built Image into a flashable boot.img.
# Base: upstream v2.1 pure-flash boot (stock ramdisk, no Magisk/KSU payload).
# We only swap the kernel payload and append androidboot.selinux=permissive,
# because this HyperOS 3 port needs Permissive to bring up Wi-Fi.
set -euo pipefail

VARIANT="${1:-a}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

mkdir -p out
BASE="out/base-boot-v2.1-pure.img"
if [ ! -f "$BASE" ]; then
  curl -fL --retry 3 -o "$BASE" \
    "https://github.com/smith-dog/kernel-tb371fc/releases/download/v2.1/boot-v2.1-pure-flash.img"
fi

OUT="out/boot-tb371fc-ReSukiSU-${VARIANT}-permissive.img"
python3 tb371fc/tools/repack_boot.py \
  "$BASE" arch/arm64/boot/Image "$OUT" "" "" "androidboot.selinux=permissive"

echo "== output =="
ls -l "$OUT"
sha256sum "$OUT"
echo "== sanity: no forbidden marker strings =="
if strings arch/arm64/boot/Image | grep -iE "t58|hidg ep0|recorder thread started"; then
  echo "WARNING: forbidden marker string found in Image"
else
  echo "OK"
fi
