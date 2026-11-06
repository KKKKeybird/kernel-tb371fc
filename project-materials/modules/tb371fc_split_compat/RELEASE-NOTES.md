# TB371FC R8 竖屏上下分屏兼容 v1.0（实验版）

恢复 TB371FC HyperOS 3 R8 的竖屏上下分屏。模块仅改变 SystemUI 上下分屏
设备白名单的结果，不修改设备代号、旋转、分辨率或 ROM 镜像。

## 适用条件

- TB371FC，设备代号 `sheng`，Android 15 / SDK 35。
- 系统构建标识 `TB371FC-HyperOS3-20261103-full`。
- 启动方向属性 `ORIENTATION_90`。
- 原版 `MiuiSystemUI.apk` SHA-256 严格匹配。

## 安装与更新

BakaSU MMRL 订阅：

```
https://raw.githubusercontent.com/KKKKeybird/kernel-tb371fc/main/module-repo/json/modules.json
```

也可以直接下载本次 Release 的
`TB371FC-SplitCompat-v1.0.zip` 并在 BakaSU / KernelSU 中安装。安装后重启。

## 已知限制

- 这是 SystemUI 关键进程补丁；不同 ROM 或 OTA 后的 SystemUI 会被指纹校验拒绝。
- 模块不修复相机、性能、账号或其他系统功能。
- 若启动后 180 秒内 SystemUI 未能稳定且能力值未达到 7，模块会自动卸载补丁并禁用。
