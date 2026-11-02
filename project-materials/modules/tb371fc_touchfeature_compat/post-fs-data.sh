#!/system/bin/sh
[ -c /dev/xiaomi-touch ] || exit 0
chown 0:1000 /dev/xiaomi-touch
chmod 0660 /dev/xiaomi-touch
