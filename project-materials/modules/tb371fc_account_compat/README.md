# TB371FC R8 小米账号兼容修复 v1.1

适用于本项目 TB371FC HyperOS 3 R8 与其原装 micloud-sdk 2.0.0.0（versionCode 20）。需要 BakaSU / KernelSU，脚本使用 `/data/adb/ksu/bin/busybox`。不适用于其他 ROM 或其他 Root 管理方案。

## 解决的问题

R8 隐藏不兼容的“查找设备”后，小米账号页调用 `FindDeviceStatusManager.isOpen()` 时，服务绑定失败会抛出未捕获 RuntimeException，导致页面闪退。模块给这一方法增加异常兜底，服务不可用时返回“未开启”。

“查找设备”保持隐藏；本模块不会恢复其定位、远程锁定或擦除功能。模块在系统启动完成后临时挂载修补后的 SDK 文件，不修改 ROM 发布 ZIP，不写入 system 分区，也不通过 APK 安装器替换系统应用。

## 使用

在 BakaSU / KernelSU 的模块管理页面安装本 ZIP，然后重启。模块加载时会结束一次小米账号进程，之后重新打开账号页即可。

修补的 SDK APK 仅供模块挂载，不要单独点击安装。

状态：`getprop sys.tb371fc.account_compat` 应为 `safe_ready`。

卸载或禁用本模块并重启，可恢复原始系统文件视图。

## 验证与限制

- 已实机验证：查找设备保持隐藏、没有运行进程时，账号页能够打开，昵称、头像与绑定手机信息正常显示，没有复现此前的绑定异常闪退。
- 仅修复上述账号页兼容问题，不是完整“小米服务修复包”。云空间容量卡片仍曾显示加载失败，未验证云服务全部功能。
- 同一设备随后在用户点击一项“重置”提示后出现返回 TWRP 的问题；已记录 `fs_mgr_mount_all`、metadata 解密及 `/data` 挂载失败。根因尚未确定，未证明与本模块有关，也未排除所有关联。本模块不用于修复该启动故障。
- v1.0 的“取消隐藏查找设备”临时方案已撤回，不要分发旧版。

备份日期：2026-11-03。ZIP 不包含账号数据库、登录凭据或个人资料。

## GitHub 订阅及更新

在 BakaSU「模块仓库 → 仓库来源 → 添加」中使用 MMRL 自定义源：

```
https://raw.githubusercontent.com/KKKKeybird/kernel-tb371fc/main/module-repo/json/modules.json
```

普通 GitHub 仓库首页不是订阅 JSON。安装的模块通过 `module.prop` 中的 `updateJson` 获取后续更新；每次安装或更新仍由使用者在管理器中确认。

本次 GitHub 分发补充了安装及启动时的 R8/SDK 版本限制、更新地址。核心 DEX 补丁与实机测试版本相同；分发元数据及版本限制经过本地校验，未在当前无法进入系统的测试平板上重新安装验收。
