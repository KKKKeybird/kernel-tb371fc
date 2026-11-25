#!/system/bin/sh
set -eu

M=${0%/*}
PAYLOAD="$M/payload/boot.img"
TARGET_SHA=2825eb9593e6593581c1f42ba52faeac1f6a53f1a3aaf94012ccef7a0b8bc272
CONFIRM="$M/confirm-action"
BACKUPS=/sdcard/TB371FC-Kernel-Backups
LOCK=/dev/tb371fc-kernel-updater.lock

fail() { echo "错误：$*"; exit 1; }
hash() { local result; result=$(sha256sum "$1") || return; echo "${result%% *}"; }
supported_base() {
  case "$1" in
    a0d3a11eb761dbe63d0175dd4ea597a3d26881c8a3ef264ca2b3fe42d886a5a9|a9fb2b73e43e8e8bb1b7a8456bf772e6729b68c110ed57bf7f2c1d1cf417b981|d866525241e212429beda800854cb3cbc1a4c3c4caea800f683b0225da11c43d) return 0;;
    *) return 1;;
  esac
}

[ "$(id -u)" = 0 ] || fail "需要 root"
[ "$(getprop ro.product.device)" = sheng ] &&
[ "$(getprop ro.product.device_for_attestation)" = TB371FC ] || fail "机型不匹配"
[ "$(getprop ro.tb371fc.build_id)" = TB371FC-HyperOS3-20261103-full ] || fail "ROM 不是受支持的 R8"
slot=$(getprop ro.boot.slot_suffix)
case "$slot" in _a|_b) ;; *) fail "活动槽无效";; esac
target="/dev/block/by-name/boot$slot"
[ -b "$target" ] || fail "找不到活动槽 boot"
[ "$(hash "$PAYLOAD")" = "$TARGET_SHA" ] || fail "载荷 SHA-256 错误"

mkdir "$LOCK" 2>/dev/null || fail "另一项内核操作正在进行"
trap 'rmdir "$LOCK"' EXIT
trap 'exit 1' HUP INT TERM
umask 077
current=$(hash "$target")
operation=install
source="$PAYLOAD"
expected="$TARGET_SHA"
record="$M/last-backup$slot"

if [ "$current" = "$TARGET_SHA" ]; then
  if [ ! -f "$record" ]; then
    rm -f "$CONFIRM"
    echo '当前 boot 已是本发布版本，无需写入。该模块没有为此槽记录回退备份。'
    exit 0
  fi
  operation=rollback
  IFS= read -r expected < "$record"
  supported_base "$expected" || fail "回退记录无效"
  source="$BACKUPS/boot$slot-$expected.img"
  [ "$(hash "$source")" = "$expected" ] || fail "回退备份不存在或校验失败"
else
  supported_base "$current" || fail "当前 boot 哈希不在白名单：$current"
fi

boot_id=$(cat /proc/sys/kernel/random/boot_id)
now=$(date +%s)
key="$operation|$slot|$boot_id|$current|$expected"
armed=0
if [ -f "$CONFIRM" ]; then
  saved=$(cat "$CONFIRM")
  when=${saved##*|}
  case "$when" in
    ''|*[!0-9]*) ;;
    *) if [ "${saved%|*}" = "$key" ] && [ "$now" -ge "$when" ] && [ $((now - when)) -le 300 ]; then armed=1; fi;;
  esac
fi
if [ "$armed" = 0 ]; then
  printf '%s|%s\n' "$key" "$now" > "$CONFIRM"
  echo "已准备 $operation，活动槽 $slot。五分钟内再次点击操作才会写入 boot。"
  exit 0
fi
rm -f "$CONFIRM"

if [ "$operation" = install ]; then
  mkdir -p "$BACKUPS"
  backup="$BACKUPS/boot$slot-$current.img"
  if [ ! -e "$backup" ]; then
    tmp=$(mktemp "$BACKUPS/.boot-backup.XXXXXX")
    dd if="$target" of="$tmp" bs=4194304 conv=fsync
    [ "$(hash "$tmp")" = "$current" ] || fail "备份回读失败"
    mv "$tmp" "$backup"
  fi
  [ "$(hash "$backup")" = "$current" ] || fail "既有备份校验失败"
  printf '%s\n' "$current" > "$record"
fi
[ "$(getprop ro.boot.slot_suffix)" = "$slot" ] &&
[ "$(hash "$target")" = "$current" ] || fail "活动槽或 boot 已变化"
[ "$(hash "$source")" = "$expected" ] || fail "镜像已变化"
if ! dd if="$source" of="$target" bs=4194304 conv=fsync || [ "$(hash "$target")" != "$expected" ]; then
  fail "boot 写入或回读失败；请勿重启，使用已校验备份恢复"
fi
echo "完成 $operation：活动槽 $slot，boot SHA-256 $expected"
echo '请手动重启。'
