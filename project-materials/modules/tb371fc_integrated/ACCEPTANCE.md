# v1.0-test1 整合模块实机验收 — 2026-11-14

环境：`TB371FC-HyperOS3-20261103-full`、Android 15、HyperOS #25。

| 检查 | 结果 |
|---|---|
| 从旧七个模块迁移 | 通过：先快照，再停旧充电/容量运行态，最后只写旧模块 `disable` |
| 整合包安装与重启 | 通过：`tb371fc_integrated` versionCode 100 自动启动 |
| 根运行态 | 通过：`sys.tb371fc.integrated=ready` |
| 容量统计 | 通过：六个 sysfs 节点由整合 owner 接管，样本 `design_mah=10200` |
| 账号兼容 | 通过：`sys.tb371fc.account_compat=safe_ready`，FindDevice 保持隐藏 |
| 竖屏上下分屏 | 通过：能力值为 7，SystemUI 使用已验证替换包 |
| 手写笔桥 | 通过：`sys.tb371fc.pen_lockfix=running`，ROM 桥服务运行 |
| 扬声器与霍尔 | 通过：两个 ROM init 服务均为 running |
| 四档充电页面 | 通过：正常、智能、健康保护和旁路供电均能实际点选并持久 |
| 亮灭屏旁路 | 通过：亮屏停电池充电，息屏恢复充电，再次亮屏重新旁路 |
| 重复启动 | 通过：同一 boot 内再次执行根 service 不会产生第二个充电桥 |
| PPS 隔离 | 通过：PPS 不随开机启动；无合适 APPS/电量条件时预检拒绝 |
| 内核安全边界 | 通过：小核心包不含 boot 载荷，开机前后 boot SHA 未变化 |
| 迁移回退 | 通过：生成 `ROLLED_BACK`，整合模块禁用，旧七个模块重新启用 |
| 回退后再次迁移 | 通过：重新安装并重启后恢复整合运行态 |

未验收：PPS 充电泵开启、真实夜间预测的跨天行为、所有电源/高负载组合。
旁路不等于物理断开电池。
