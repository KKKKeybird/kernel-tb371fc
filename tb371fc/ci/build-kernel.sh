#!/usr/bin/env bash
# Build the TB371FC kernel with ReSukiSU integrated (built-in, manual hook).
# Usage: build-kernel.sh [a|b]
#   a = baseline v2.1 config (SYSVIPC off)  -> flash first
#   b = same + SYSVIPC + CGROUP_NET_PRIO    -> only for droidspaces testing
set -euo pipefail

VARIANT="${1:-a}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

echo "== variant: $VARIANT =="

cp tb371fc/config-v2.1.txt .config

CFG=(
  --file .config
  --set-str EXTRA_FIRMWARE_DIR "tb371fc/firmware"
  --enable KSU
  --disable KSU_TRACEPOINT_HOOK
  --enable KSU_MANUAL_HOOK
  --enable KSU_MANUAL_HOOK_AUTO_SETUID_HOOK
  --enable KSU_MANUAL_HOOK_AUTO_INITRC_HOOK
  --enable KSU_MANUAL_HOOK_AUTO_INPUT_HOOK
  --enable KSU_MULTI_MANAGER_SUPPORT
  --enable KALLSYMS_ALL
)

if [ "$VARIANT" = "b" ]; then
  CFG+=(--enable SYSVIPC --enable SYSVIPC_SYSCTL --enable CGROUP_NET_PRIO)
fi

scripts/config "${CFG[@]}"

FLAGS=(
  ARCH=arm64
  CROSS_COMPILE=aarch64-linux-gnu-
  CC=clang
  CLANG_TRIPLE=aarch64-linux-gnu-
  AS=aarch64-linux-gnu-as
)
KC=(
  "KCFLAGS=-Wno-error -Wno-error=strict-prototypes -Wno-error=implicit-int -Wno-error=incompatible-pointer-types -Wno-error=date-time -DKSU_SKIP_NOFAULT_COMPAT -include $ROOT/drivers/staging/fw-api/fw/p226_compat.h"
)

make "${FLAGS[@]}" "${KC[@]}" DYNAMIC_SINGLE_CHIP=qca6390 olddefconfig

echo "== resulting config =="
grep -E "^CONFIG_KSU=|^CONFIG_KSU_MANUAL_HOOK=|^CONFIG_KSU_TRACEPOINT_HOOK|^CONFIG_SYSVIPC=|^CONFIG_CGROUP_NET_PRIO=|^CONFIG_MODVERSIONS=|^CONFIG_KALLSYMS_ALL=" .config || true

make -j"$(nproc)" "${FLAGS[@]}" "${KC[@]}" DYNAMIC_SINGLE_CHIP=qca6390 Image

echo "== image =="
ls -l arch/arm64/boot/Image
sha256sum arch/arm64/boot/Image
