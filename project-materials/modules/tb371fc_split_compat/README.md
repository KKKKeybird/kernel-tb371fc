# TB371FC R8 竖屏上下分屏兼容 v1.1

仅适用于本项目 TB371FC HyperOS 3 R8，系统构建标识为
`TB371FC-HyperOS3-20261103-full`。需要 BakaSU / KernelSU，脚本使用
`/data/adb/ksu/bin/busybox`。

## 解决的问题

当前 ROM 的 SystemUI 只对设备代号 `violin` 开放原生上下分屏，而 TB371FC
内核上报的设备代号是 `sheng`。因此竖屏分屏只有左右布局，出现超高、超窄的
两个窗口。

模块只改变 SystemUI 中 `IS_TOPBOTTOM_SPLIT_DEVICE` 的白名单结果，使系统
已有的上下分屏实现可用。它不修改设备代号、旋转设置、分辨率、桌面 APK 或
其他 SystemUI 功能。

## 使用

在 BakaSU / KernelSU 的模块管理页安装 ZIP，重启一次。模块会：

1. 核对设备、Android 版本、R8 构建标识和原始 SystemUI SHA-256。
2. 在启动完成后，对 init/zygote 命名空间只读挂载修补后的 APK。
3. 重启一次 SystemUI，并等待界面稳定和能力值 `wmshell_multiwindow_version_code=7`。
4. 若 180 秒内没有稳定，自动卸载补丁、恢复原 SystemUI 并禁用模块。

状态：

```
getprop sys.tb371fc.split_compat
```

正常应为 `ready`。日志位于
`/data/vendor/tb371fc/split-compat/`。

模块管理器中的“操作”按钮可立即撤销当前开机的补丁并禁用模块。卸载模块后
重启可完全恢复原始系统文件。

## 兼容性与安全边界

- 原 SystemUI APK SHA-256：
  `8bb7b6fc36e2bfbe4f3ee2ae91512c95ee32209e86a71e1437040f3eaf45e5bb`
- 修补后 APK SHA-256：
  `0b243aebb7efe8f5655dbe467d61e0def5fc502005be508028857fa3d57089da`
- 系统 APK 在模块 ZIP 中不是以普通安装包替换；模块使用启动后的只读 bind mount。
- 原始 APK 或 ROM 标识不匹配时，安装与开机执行都会跳过或中止。
- 更换 ROM、SystemUI 或系统更新前应先禁用本模块，避免旧载荷继续匹配。
- 已安装旧版时可直接从订阅更新；安装检查同时接受原版 APK 和本模块已知修补版 APK。
- 本模块只恢复上下分屏能力。不是相机、性能、账号或小米服务修复包。

## 实机验收

2026-11-07 已由用户在当前平板上完成实机验收：状态栏/导航栏稳定，竖屏可使用
上下分屏，窗口比例正常，并确认日常分屏操作没有发现问题。发布版本沿用同一
补丁载荷；GitHub 包发布后另行执行安装与重启回归。

## GitHub 订阅与更新

在 BakaSU「模块仓库 → 仓库来源 → 添加」中使用：

```
https://raw.githubusercontent.com/KKKKeybird/kernel-tb371fc/main/module-repo/json/modules.json
```

已安装模块通过 `module.prop` 的 `updateJson` 检查后续版本。模块安装与更新
仍由使用者在管理器中确认。
