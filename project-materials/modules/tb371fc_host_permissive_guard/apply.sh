#!/system/bin/sh
set -eu
M=${0%/*}
T=/sys/fs/selinux/enforce
Z=$M/enforce-zero
A=$M/enforce-actual
B=$M/busybox
[ ! -e "$M/disable" ] && [ ! -e "$M/remove" ] || exit 0
[ -r "$T" ] || exit 1
# If this namespace already inherits our protected mount, do not stack mounts.
if [ "$(stat -c %d:%i "$T")" = "$(stat -c %d:%i "$Z")" ]; then
 # A bind can propagate into an existing peer namespace before the
 # read-only remount does.  Complete protection in that namespace too.
 "$B" mount -o remount,bind,ro "$Z" "$T"
 "$B" mount -o remount,bind,ro "$A" "$A"
 grep ' /sys/fs/selinux/enforce ' /proc/self/mountinfo | grep -q ' ro,'
 [ "$(cat "$A")" = 0 ]
 exit 0
fi
[ "$(cat "$T")" = 0 ] || setenforce 0
[ "$(cat "$T")" = 0 ]
# Retain a read-only alias of the actual selinuxfs inode for honest verification.
"$B" mount --bind "$T" "$A"
"$B" mount -o remount,bind,ro "$T" "$A"
if ! "$B" mount --bind "$Z" "$T"; then umount "$A"; exit 1; fi
if ! "$B" mount -o remount,bind,ro "$Z" "$T"; then umount "$T"; umount "$A"; exit 1; fi
grep ' /sys/fs/selinux/enforce ' /proc/self/mountinfo | grep -q ' ro,'
[ "$(cat "$A")" = 0 ] && [ "$(cat "$T")" = 0 ]
echo "GUARD_INSTALLED namespace=$(readlink /proc/self/ns/mnt) actual_enforce=$(cat "$A")"
