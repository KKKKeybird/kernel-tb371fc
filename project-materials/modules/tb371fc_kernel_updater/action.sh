#!/system/bin/sh
set -eu

M=${0%/*}
PAYLOAD="$M/payload/boot.img"
PAYLOAD_SHA=a9fb2b73e43e8e8bb1b7a8456bf772e6729b68c110ed57bf7f2c1d1cf417b981
OLD_SHA=a0d3a11eb761dbe63d0175dd4ea597a3d26881c8a3ef264ca2b3fe42d886a5a9
CONFIRM="$M/confirm-action"
LAST="$M/last-backup"

fail() { echo "错误：$*"; exit 1; }
[ "$(id -u)" = 0 ] || fail "需要 root"
[ "$(getprop ro.product.device)" = sheng ] || fail "机型不匹配"
[ "$(getprop ro.tb371fc.build_id)" = TB371FC-HyperOS3-20261103-full ] || fail "ROM 不是受支持的 R8"

slot=$(getprop ro.boot.slot_suffix)
case "$slot" in _a|_b) ;; *) fail "活动槽无效：$slot";; esac
target="/dev/block/by-name/boot$slot"
[ -b "$target" ] || fail "找不到 $target"
[ -f "$PAYLOAD" ] || fail "缺少内核载荷"
payload_hash=$(sha256sum "$PAYLOAD"); payload_hash=${payload_hash%% *}
[ "$payload_hash" = "$PAYLOAD_SHA" ] || fail "载荷哈希错误"

current=$(sha256sum "$target"); current=${current%% *}
operation=install
source=$PAYLOAD
expected=$PAYLOAD_SHA

if [ "$current" = "$PAYLOAD_SHA" ]; then
  operation=rollback
  if [ ! -f "$LAST" ]; then
    rm -f "$CONFIRM"
    echo '当前已经是最新内核；本模块未执行过更新，因此没有可用的模块回滚备份。'
    exit 0
  fi
  source=$(cat "$LAST")
  [ -f "$source" ] || fail "回滚备份不存在：$source"
  expected=$(sha256sum "$source"); expected=${expected%% *}
  [ "$expected" = "$OLD_SHA" ] || fail "回滚备份不是受支持的原始 boot"
elif [ "$current" != "$OLD_SHA" ]; then
  fail "当前 boot 哈希不在安全白名单：$current"
fi

now=$(date +%s)
armed=
if [ -f "$CONFIRM" ]; then
  IFS='|' read -r oldop oldhash oldtime < "$CONFIRM" || true
  if [ "$oldop" = "$operation" ] && [ "$oldhash" = "$current" ] && [ $((now-oldtime)) -le 300 ]; then
    armed=1
  fi
fi

if [ -z "$armed" ]; then
  printf '%s|%s|%s\n' "$operation" "$current" "$now" > "$CONFIRM"
  echo "已准备：$operation，活动槽 $slot"
  echo '请在 5 分钟内再次点击模块操作按钮确认。此次没有写入 boot。'
  exit 0
fi

mkdir -p /sdcard/TB371FC-Kernel-Backups
if [ "$operation" = install ]; then
  prefix=$(printf '%.12s' "$current")
  backup="/sdcard/TB371FC-Kernel-Backups/boot${slot}-${prefix}.img"
  if [ ! -f "$backup" ]; then
    dd if="$target" of="$backup" bs=4194304
    sync
  fi
  backup_hash=$(sha256sum "$backup"); backup_hash=${backup_hash%% *}
  [ "$backup_hash" = "$current" ] || fail "boot 备份校验失败"
  echo "$backup" > "$LAST"
fi

echo "开始执行 $operation：$target"
dd if="$source" of="$target" bs=4194304 conv=fsync
sync
written=$(sha256sum "$target"); written=${written%% *}
[ "$written" = "$expected" ] || fail "写入后校验失败；请勿重启并立即人工恢复"
rm -f "$CONFIRM"
echo "完成：$operation"
echo "当前 boot SHA-256：$written"
echo '请手动重启；模块不会自动重启设备。'
