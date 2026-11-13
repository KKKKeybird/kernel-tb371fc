# TB371FC R8 系统服务兼容修复

适用于 `TB371FC-HyperOS3-20261103-full`。

修复两类问题：

- 新内核删除旧 `TFA Profile` 控件后，扬声器旋转服务启动即退出；
- 扬声器与霍尔服务遗留空锁目录时，旧脚本永久拒绝再次启动。

模块只覆盖对应系统服务文件，不接管 SystemUI、账号、电池、手写笔或 boot。检测到独立 `tb371fc_hall_cover` 模块时会放弃霍尔文件覆盖，避免依赖模块加载顺序。

电话和 VoIP 期间仍以 Android Audio Mode 阻止扬声器矩阵切换；存在旧 `TFA Profile` 控件时继续使用原配置名保护。
