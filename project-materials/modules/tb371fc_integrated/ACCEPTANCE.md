# v1.3-test1 验收记录 — 2026-11-15

设备：`HA1T90V1`，`TB371FC-HyperOS3-20261103-full`，活动槽 `_a`。

| 项目 | 实测结果 |
|---|---|
| 发布 ZIP | `TB371FC-Integrated-v1.3-test1.zip`，34,274,654 字节，SHA-256 `8e6fb23b049cfb8b708b4f0f5f8c4199ba3a3915325c7cfd26ab04f7a650599c` |
| 安装 | 同一发布 ZIP 通过 BakaSU/KernelSU 覆盖安装成功，versionCode 130 |
| boot 安全 | 活动槽 boot SHA-256 为已验证 `#25`：`a9fb2b73e43e8e8bb1b7a8456bf772e6729b68c110ed57bf7f2c1d1cf417b981` |
| 旧内核自动流程 | `kernel-auto.sh` 自测通过：等待模块安装提交、调用 `--auto`、写后校验、最后才调用重启；旧 boot 实刷未在本轮重复降级验证 |
| 当前内核幂等 | `run-kernel.sh --auto` 检测到已是 `#25` 后跳过下载和重复写入 |
| 满电容量 | 原始 `0x10=9011`、`0x12=8196`、SOC=100；新逻辑输出 `8196/8196`、`delta=0`，不再触发 ±3% 校验失败 |
| 容量运行态 | `charge_full_design=10200000`，状态 `design_mah=10200`，守护进程持续运行 |
| 整合运行态 | `sys.tb371fc.integrated=ready` |
| 音质音效 | 页面正常打开，显示“杜比全景声”“杜比音效设置”，无 FATAL |
| MiSound 空值 | 原版 APK 同样可复现三音效均不可用时的 `getValue()==null`；补丁将空值回落为 `none`，并保护初始化、偏好变化和销毁链路 |
| Dirac | 系统无 `libdirac.so`；GEQ 入口继续隐藏，直接启动 `DolbyEqActivity` 不崩溃 |
| Dolby | 恢复缺失的 `ro.vendor.audio.dolby.dax.support=true`；AudioFlinger 仍加载原生 `DAP`/`DAP_game` |
| 电池保护页 | 页面正常打开；10200mAh 与正常/智能/健康/旁路四档均存在，无闪退 |
| 旧模块迁移 | 八个旧模块目录未启用，整合模块单独运行 |

边界：本轮设备已处于 `#25`，没有把 boot 降回旧版本做实际自动刷写演练；
自动安装器使用同一已实机验证过的 boot 写入路径，并增加了安装提交等待与失败不重启保护。
