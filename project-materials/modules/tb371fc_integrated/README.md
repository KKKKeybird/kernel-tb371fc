# TB371FC R8 整合维护与兼容修复

面向本项目 `TB371FC-HyperOS3-20261103-full`、Android 15 和 HyperOS
`#25` 内核。根模块统一维护原先分散的账号、竖屏上下分屏、手写笔锁、容量
统计、系统服务兼容和四档充电模块。

## 包含

- 小米账号页的 FindDevice 不可用兜底；不恢复或启用 FindDevice。
- SystemUI 竖屏上下分屏能力。
- 手写笔电量桥的陈旧锁恢复。
- 恢复 MiSound 页面缺失的 Dolby DAP 兼容属性；Dolby 原生效果链不变。
- MM8013 动态容量统计与 10200mAh 设计基准。
- 扬声器旋转/霍尔服务的旧锁兼容。
- 原“电池保护”页面的正常、智能、健康保护和旁路供电四档。
- PPS 温度实验的载荷与手动命令，默认永不运行。
- 首次安装时自动校验、备份并更新 `#25` boot，成功回读后自动重启；小核心
  发布包不带 100MB boot 载荷。

旧独立模块作为回退保留下载。安装本模块会把当前启用且受管的七个旧模块
写成 `disable`，不创建 `remove`，也不调用旧整包卸载。回退脚本只删除本次
迁移新建的 `disable`，并停用整合模块。

## 手动功能

```sh
# 检查PPS实验条件；确定运行仍使用 action.sh，最长120秒并自动回退
su -c "sh /data/adb/modules/tb371fc_integrated/components/tb371fc_pps_temp_test/run-test.sh --check"

# 自动更新失败后可在模块操作页重试
su -c "sh /data/adb/modules/tb371fc_integrated/run-kernel.sh --auto"
```

首次安装会在模块提交成功后自动下载固定载荷；只有哈希、备份、写入和回读
全部通过才会自动重启。断网或任一步校验失败时保持旧 boot。

## 回退

```sh
su -c "sh /data/adb/modules/tb371fc_integrated/tools/rollback-migration.sh \
  --modules-dir /data/adb/modules \
  --state-dir /data/adb/tb371fc-integrated/migration-v1"
```

恢复后重启。原内核备份、FindDevice 隐藏状态、用户充电选项和 Google Play
数据均不由整合模块改写。

## 未验收边界

PPS 充电泵开启仍需合适 PPS 电源和低电量条件；真实夜间预测、预计使用前
充满与跨天周期充满仍需自然使用。旁路不代表物理断开电池。
