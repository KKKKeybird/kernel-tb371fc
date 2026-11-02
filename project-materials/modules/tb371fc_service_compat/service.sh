#!/system/bin/sh
MODDIR=${0%/*}
while [ "$(getprop sys.boot_completed)" != 1 ]; do sleep 3; done
[ -e "$MODDIR/disable" ] && exit 0
[ -e "$MODDIR/remove" ] && exit 0
{
  # [2026-10-31] PowerKeeper 已可由自定义 HAL + 官方 APK 正常启动，不再整包禁用；
  # 但保留云控/推送入口禁用，避免云端策略与 mipush 在混合 ROM 上干扰本地行为。
  for comp in \
    com.android.htmlviewer/com.android.settings.cloud.CloudControlBootCompletedReceiver \
    com.android.htmlviewer/com.android.settings.cloud.CloudJobMobileDataService \
    com.android.htmlviewer/com.android.settings.cloud.CloudJobService2 \
    com.android.htmlviewer/com.android.settings.cloud.CloudSettingsProvider \
    com.android.htmlviewer/com.android.settings.cloud.released.UpdateReleaseDataReceive \
    com.miui.powerkeeper/.cloudcontrol.CloudUpdateJobService \
    com.miui.powerkeeper/.cloudcontrol.CloudUpdateReceiver \
    com.miui.powerkeeper/com.xiaomi.mipush.sdk.MessageHandleService \
    com.miui.powerkeeper/com.xiaomi.mipush.sdk.PushMessageHandler; do
    cmd package disable --user 0 "$comp"
  done
} > "$MODDIR/service.log" 2>&1
