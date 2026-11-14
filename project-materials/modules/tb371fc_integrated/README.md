# TB371FC R8 整合维护与兼容修复

面向本项目 `TB371FC-HyperOS3-20261103-full`、Android 15 和 HyperOS
`#25` 内核。根模块统一维护原先分散的账号、竖屏上下分屏、手写笔锁、容量
统计、系统服务兼容和四档充电模块。

## 包含

- 小米账号页的 FindDevice 不可用兜底；不恢复或启用 FindDevice。
- SystemUI 竖屏上下分屏能力。
- 手写笔电量桥的陈旧锁恢复。
- MM8013 动态容量统计与 10200mAh 设计基准。
- 扬声器旋转/霍尔服务的旧锁兼容。
- 原“电池保护”页面的正常、智能、健康保护和旁路供电四档。
- PPS 温度实验的载荷与手动命令，默认永不运行。
- 内核更新器代码与双击确认逻辑；小核心发布包不带 100MB boot 载荷。

旧独立模块作为回退保留下载。安装本模块会把当前启用且受管的七个旧模块
写成 `disable`，不创建 `remove`，也不调用旧整包卸载。回退脚本只删除本次
迁移新建的 `disable`，并停用整合模块。

## 手动功能

```sh
# 检查PPS实验条件；确定运行仍使用 action.sh，最长120秒并自动回退
su -c "sh /data/adb/modules/tb371fc_integrated/components/tb371fc_pps_temp_test/run-test.sh --check"

# 首次下载固定内核包并准备更新；五分钟内再次运行完成双确认
su -c "sh /data/adb/modules/tb371fc_integrated/run-kernel.sh"
su -c "sh /data/adb/modules/tb371fc_integrated/run-kernel.sh"
```

内核更新不会自动写 boot。没有模块回滚备份时，脚本不会猜测或覆盖。

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
