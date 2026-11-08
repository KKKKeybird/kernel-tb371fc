# TB371FC R8 手写笔电量桥锁修复 v1.0

仅适用于本项目 TB371FC HyperOS 3 R8，构建标识
`TB371FC-HyperOS3-20261103-full`。需要 BakaSU / KernelSU。

## 修复内容

原 ROM 的 `pen_bridge/supervisor.sh` 在 `supervisor.lock` 存在但里面没有
有效 PID 时，会误以为另一个桥接进程仍在运行并直接退出。陈旧锁又位于持久化
的 `/data/vendor/tb371fc/pen_bridge`，因此会跨重启保留，导致：

- `tb371fc-pen-bridge` 始终为 `stopped`
- CTN730 和蓝牙虽然正常，但不再发送小米手写笔电量广播
- 插拔笔没有电量 UI

本模块只覆盖一个 ROM 集成脚本，使空 PID、死 PID 和普通陈旧锁能够自动恢复，
并记录恢复原因。开机服务还会清理一次旧锁，并在桥接服务仍停止时启动它。

## 安装与验证

在 BakaSU / KernelSU 模块管理器安装后重启。

```sh
getprop init.svc.tb371fc-pen-bridge
getprop sys.tb371fc.pen_lockfix
cat /data/vendor/tb371fc/pen_bridge/status.txt
```

正常应分别为 `running`、`running`，并显示笔的电量和充电状态。

模块管理器中的“操作”按钮可以立即清理陈旧锁并启动桥接，不必等重启。

## 影响范围

- 只替换 `/system/etc/tb371fc/core/pen_bridge/supervisor.sh`。
- 不修改 CTN730 驱动、I2C、蓝牙配对、触控笔输入 relay 或其他系统服务。
- 原有 supervisor 仍负责启动同一个 `pen-bridge.jar` 和 `pen-relay`。
- 不改变笔的充电策略，也不向硬件写入充电命令。
- ROM、设备或原脚本指纹不匹配时会拒绝安装和启动。

卸载模块并重启即可恢复 ROM 原始脚本。
