# TB371FC R8 模块订阅（实验）

此目录提供 BakaSU 支持的 MMRL 自定义模块源。目前包含 R8 小米账号兼容修复、
R8 竖屏上下分屏兼容和手写笔电量桥锁修复三个独立模块，并非通用小米服务修复合集。

在 BakaSU 的模块仓库来源管理中添加：

```
https://raw.githubusercontent.com/KKKKeybird/kernel-tb371fc/main/module-repo/json/modules.json
```

也可填写源目录地址 `https://raw.githubusercontent.com/KKKKeybird/kernel-tb371fc/main/module-repo/`。普通 GitHub 仓库首页返回 HTML，不能替代此 JSON 订阅。

模块下载：

- [v1.1 小米账号兼容](https://github.com/KKKKeybird/kernel-tb371fc/releases/tag/module-account-compat-v1.1)
- [v1.1 竖屏上下分屏](https://github.com/KKKKeybird/kernel-tb371fc/releases/tag/module-split-compat-v1.1)
- [v1.0 手写笔电量桥锁修复](https://github.com/KKKKeybird/kernel-tb371fc/releases/tag/module-pen-lockfix-v1.0)

请下载发布附件 ZIP，不能把 GitHub 的整个仓库源码 ZIP 当成模块。

[小米账号模块说明](../project-materials/modules/tb371fc_account_compat/README.md)：
仅用于隐藏 FindDevice 后的账号页兼容，不恢复查找设备。

[上下分屏模块说明](../project-materials/modules/tb371fc_split_compat/README.md)：
仅用于精确匹配的 TB371FC R8 与原版 SystemUI，不改 ROM 镜像。

[手写笔电量桥锁修复](../project-materials/modules/tb371fc_pen_lockfix/README.md)：
修复陈旧锁导致桥接退出，不修改硬件或笔输入链路。

已安装版本通过 `module.prop` 的 `updateJson` 获取更新；模块安装与更新需要
使用者确认。

本源按 BakaSU 的 [MMRL 解析器](https://github.com/Baka-SU/BakaSU/blob/main/manager/app/src/main/java/org/bakasu/bakasu/data/module/MmrlRepositoryParser.kt) 及 [数据格式](https://github.com/Baka-SU/BakaSU/blob/main/manager/app/src/main/java/org/bakasu/bakasu/data/module/MmrlRepositoryDtos.kt) 编排。部分旧版管理器若没有自定义源入口，可直接下载模块 ZIP 安装。
