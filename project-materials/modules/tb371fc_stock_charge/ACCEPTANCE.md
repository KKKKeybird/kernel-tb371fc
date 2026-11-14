# v1.1-test3 实机验收 — 2026-11-14

范围：`TB371FC-HyperOS3-20261103-full`、Android 15、已验证 HyperOS #25
内核、当前普通 USB-PD 电源。PPS 充电泵开启场景未测试。

| 检查 | 结果 |
|---|---|
| 原厂 `charge_disable` 停充/恢复 | 通过：旁路时电池电流为 0，USB/AC 输入保留；恢复正常后重新充电 |
| 原页面四档与单选控件 | 通过：正常充满、智能充电保护、电池健康保护、旁路供电均显示原生 radio 与 `CheckedTextView` |
| 六次真实点选 | 通过：健康 → 智能 → 正常 → 旁路 → 正常 → 旁路，设置值、选中状态与底层电平均同步 |
| 原生健康保护请求 | 通过：`mode=2`、`smart_limit=80`、`target_source=native`、`native_ready=true`，89–90% 时停充 |
| 亮屏旁路 | 通过：`mode=3`、`hold=true`、`charging_enabled=0`、电池电流 0 |
| 息屏恢复充电 | 通过：保持 `mode=3` 时 `hold=false`、`charging_enabled=1`，电池重新充电 |
| 再次亮屏 | 通过：旁路重新生效，`hold=true`、`charging_enabled=0` |
| 安装器生成 APK | 通过：生成文件权限 `0644`，APK ZIP 校验无错误，覆盖后安全中心进程可启动 |
| 正式候选 ZIP 安装并重启 | 通过：模块 `1.1-test3`/versionCode 13 持久安装，boot_completed 后桥接自动启动 |
| APK 与静态容量 | 通过：安全中心目标指向已知替换包；`sheng.xml` 仅把 5000mAh 修为 10200mAh |
| 禁用模块并重启 | 通过：恢复原 APK SHA、5000mAh 原配置、原属性值、原 `lwky_smart_charge`，无桥接进程 |
| 重新启用并重启 | 通过：四档覆盖、10200mAh、临时原生能力和桥接自动恢复 |
| 重复启动 | 通过：只有一个 `tb371fc-charge-bridge` 实例 |
| 正常停止 | 通过：释放 `charge_disable` 请求、恢复原 APK、原属性和原 init 服务 |
| SIGKILL 子进程 | 通过：监督进程有界检测并执行同一回退，无残留 Runtime |
| 与现有 ROM/模块兼容 | 通过：容量统计、手写笔、分屏、扬声器旋转和内核更新器路径无直接覆盖冲突 |
| 策略测试 | 通过：39 项原生目标、旁路和回差状态转换 |

未验收：真实自然使用下的夜间学习、预计使用前提前充满、跨多天周期充满；
PPS 充电泵开启后的停充/温控；所有充电器与高负载组合。

旁路不是物理断开电池。瞬时峰值或弱电源超过输入能力时，电池仍可能参与供电。
共用 `charge_disable` USER 投票时，不能同时运行另一个直接写该节点的控制器。
