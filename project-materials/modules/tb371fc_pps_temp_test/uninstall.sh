#!/system/bin/sh
MODDIR=${0%/*}
su -M -c "sh '$MODDIR/rollback.sh'"
