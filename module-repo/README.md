# TB371FC R8 模块订阅（实验）

此目录提供 BakaSU 支持的 MMRL 自定义模块源。首选安装“R8 整合维护与
兼容修复”；此前的单功能模块作为旧版回退保留，不再继续维护。

在 BakaSU 的模块仓库来源管理中添加：

```
https://raw.githubusercontent.com/KKKKeybird/kernel-tb371fc/main/module-repo/json/modules.json
```

也可填写源目录地址 `https://raw.githubusercontent.com/KKKKeybird/kernel-tb371fc/main/module-repo/`。普通 GitHub 仓库首页返回 HTML，不能替代此 JSON 订阅。

模块下载：

- [v1.0-test1 R8 整合维护与兼容修复](https://github.com/KKKKeybird/kernel-tb371fc/releases/tag/module-integrated-v1.0-test1)

以下为旧版回退，不再推荐新安装：

- [v1.1 小米账号兼容](https://github.com/KKKKeybird/kernel-tb371fc/releases/tag/module-account-compat-v1.1)
- [v1.1 竖屏上下分屏](https://github.com/KKKKeybird/kernel-tb371fc/releases/tag/module-split-compat-v1.1)
- [v1.0 手写笔电量桥锁修复](https://github.com/KKKKeybird/kernel-tb371fc/releases/tag/module-pen-lockfix-v1.0)

- [v1.0 电池容量统计修复](https://github.com/KKKKeybird/kernel-tb371fc/releases/tag/module-battery-stats-v1.0)
- [v1.0 系统服务兼容修复](https://github.com/KKKKeybird/kernel-tb371fc/releases/tag/module-system-compat-v1.0)
- [v2.2-hyperos25 内核更新器](https://github.com/KKKKeybird/kernel-tb371fc/releases/tag/module-kernel-updater-v2.2-hyperos25)
- [v1.1-test3 原厂充电控制与四档电池保护](https://github.com/KKKKeybird/kernel-tb371fc/releases/tag/module-stock-charge-v1.1-test3)

请下载发布附件 ZIP，不能把 GitHub 的整个仓库源码 ZIP 当成模块。

[整合维护与兼容修复](../project-materials/modules/tb371fc_integrated/README.md)：
统一接管账号、上下分屏、手写笔锁、容量统计、系统服务、四档充电和手动内核/PPS
工具。安装时会保留并禁用旧单模块目录，回退脚本可恢复旧模块。

[小米账号模块说明](../project-materials/modules/tb371fc_account_compat/README.md)：
仅用于隐藏 FindDevice 后的账号页兼容，不恢复查找设备。

[上下分屏模块说明](../project-materials/modules/tb371fc_split_compat/README.md)：
仅用于精确匹配的 TB371FC R8 与原版 SystemUI，不改 ROM 镜像。

[手写笔电量桥锁修复](../project-materials/modules/tb371fc_pen_lockfix/README.md)：
修复陈旧锁导致桥接退出，不修改硬件或笔输入链路。

[电池容量统计修复](../project-materials/modules/tb371fc_battery_stats/README.md)：
10200mAh设计基准与动态容量统计，不含PD、快充或旁路策略改动。

[系统服务兼容修复](../project-materials/modules/tb371fc_system_compat/README.md)：
恢复扬声器旋转及空锁服务，不与现有四个正式修复模块重复覆盖文件。

[内核更新器](../project-materials/modules/tb371fc_kernel_updater/README.md)：
只有用户在模块操作页连续确认两次才写入活动槽 boot；订阅更新不会自动刷写。

[原厂充电控制与四档电池保护](../project-materials/modules/tb371fc_stock_charge/README.md)：
保留小米原有智能充电与80%健康保护，在原电池保护页面新增旁路供电。

已安装版本通过 `module.prop` 的 `updateJson` 获取更新；模块安装与更新需要
使用者确认。

本源按 BakaSU 的 [MMRL 解析器](https://github.com/Baka-SU/BakaSU/blob/main/manager/app/src/main/java/org/bakasu/bakasu/data/module/MmrlRepositoryParser.kt) 及 [数据格式](https://github.com/Baka-SU/BakaSU/blob/main/manager/app/src/main/java/org/bakasu/bakasu/data/module/MmrlRepositoryDtos.kt) 编排。部分旧版管理器若没有自定义源入口，可直接下载模块 ZIP 安装。

[PPS充电泵温度实验 v0.3-test](https://github.com/KKKKeybird/kernel-tb371fc/releases/tag/module-pps-temp-test-v0.3)：
**未实测可用性**，尚未证明能启动充电泵或恢复正确温度；安装和开机不自动启用，
仅模块“操作”按钮手动开始有条件的限时实验。不是已验证的快充修复。
