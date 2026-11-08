# TB371FC R8 手写笔电量桥锁修复 v1.0（实验版）

修复 ROM 内 `pen_bridge/supervisor.sh` 无法处理空 PID 或死 PID 陈旧锁的问题。
修复后会在启动时自动清理旧锁并拉起电量桥，避免插拔笔时电量 UI 消失。

仅适用于 TB371FC HyperOS 3 R8 构建 `TB371FC-HyperOS3-20261103-full`。模块只覆盖
一个系统适配脚本，不修改 CTN730、蓝牙、笔输入 relay、充电策略或其他模块。

BakaSU MMRL 订阅：

```
https://raw.githubusercontent.com/KKKKeybird/kernel-tb371fc/main/module-repo/json/modules.json
```

也可以直接下载本次 Release 的 `TB371FC-PenLockFix-v1.0.zip`。
