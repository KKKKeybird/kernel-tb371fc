# v1.1（实验版，R8 专用）

- 保持 FindDevice 隐藏，对 `FindDeviceStatusManager.isOpen()` 的 RuntimeException 返回关闭状态。
- 系统启动完成后临时挂载修补的共享 SDK，避免在 PackageManager 启动扫描阶段替换 APK。
- 增加 TB371FC R8 与 micloud-sdk versionCode 20 的安装及启动限制。
- 增加 BakaSU MMRL 订阅及标准 updateJson 更新地址。
- 已验证账号页、昵称和头像；云容量卡片仍未恢复，启动故障根因未明，详见 README。
- 撤回 v1.0：取消隐藏会启动不兼容的 FindDevice 实现并出现 ANR，不提供该版本。
