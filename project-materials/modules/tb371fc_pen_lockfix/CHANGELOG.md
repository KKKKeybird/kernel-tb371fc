# v1.0（实验版，R8 专用）

- 修复空 PID 或死 PID 的 `supervisor.lock` 导致电量桥永久退出的问题。
- 开机后主动清理一次陈旧锁，并在桥接服务停止时启动服务。
- 增加 supervisor 锁恢复日志 `pen_bridge/supervisor.log`。
- 增加模块操作按钮、精确 ROM 与原始脚本指纹校验。
- 不修改 CTN730、蓝牙、笔输入 relay、充电策略或其他模块。
