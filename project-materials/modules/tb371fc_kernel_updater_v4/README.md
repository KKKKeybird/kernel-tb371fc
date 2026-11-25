# TB371FC R8 PPS 修复 v4 内核更新器

适用于本项目 TB371FC HyperOS 3 R8。使用前确认 Bootloader 已解锁、可以通过有线 Fastboot 恢复，并备好当前 boot。

从 [v4 发布页](https://github.com/KKKKeybird/kernel-tb371fc/releases/tag/kernel-r8-charging-v4) 下载模块 ZIP 后，在 BakaSU 安装。在同一次开机内于五分钟内点击模块「操作」两次才会备份并写入活动槽。成功回读后手动重启。已经运行 v4 的设备无需重刷。仅接受已知原 R8 #24、#25 或测试 v3 的 boot 哈希。

本模块为该槽记录过备份时，可再次双确认回退到该备份。备份保存在 `/sdcard/TB371FC-Kernel-Backups/`。如遇写入失败，先恢复已校验的备份再重启。

旧整合模块的安装器可能拒绝新版 boot 的哈希。现有整合模块可继续运行，本次内核使用独立更新器。
