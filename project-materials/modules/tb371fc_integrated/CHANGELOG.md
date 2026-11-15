# v1.3-test1 — 2026-11-15

- Remove the circular kernel dependency: a supported pre-#25 boot may install
  the integrated module first.
- Keep boot-dependent runtimes dormant until the verified #25 boot is active.
- Update the verified `#25` boot automatically after the first module install
  commits, then reboot only after write-back verification succeeds.
- Retry the automatic kernel flow from the module action button when installation
  or network validation stops before a boot write.
- Clamp MM8013 charge_counter to charge_full only at the 99-100% endpoint.
- Retry the initial battery sample during boot instead of immediately failing.
- Fix an independent original MiSound NPE when no audio-effect backend is
  available; the real headset-settings page now opens without a null dropdown.
- Restore the missing `ro.vendor.audio.dolby.dax.support` property so the page
  reports the already-active Dolby DAP effect instead of hiding Dolby support.

# v1.2-test1 — 2026-11-15

- Hide MiSound's unsupported Dirac graphic-equalizer entrance.
- Safely reject direct DolbyEqActivity intents when Dirac is unavailable.
- Preserve Dolby Atmos/DAP and the Lenovo original vendor audio calibration.
- Change only three MiSound DEX methods; 43,160 other method bodies are identical.

# v1.1-test1 — 2026-11-15

- 新增事件驱动的 NVT 四方向边缘防误触适配；显示旋转和回屏回调触发，无定时方向轮询。退出只恢复自身仍拥有的配置。
- 精确识别 Lenovo Precision Pen 3 并复用小米数字笔图标；不改蓝牙设备类型、配对资料或协议栈。只替换 Settings 的 classes6.dex，其他原方法保留。
- 新组件支持停用/卸载文件事件回退，Settings 版本不符时拒绝覆盖。
- 修正整合模块升级时首次迁移记录可能被覆盖、安装失败后容量组件恢复路径可能选错的问题。
- 性能模式原生请求未取得有效句柄，Dolby 控制预检未通过；不加入自动启动或虚假能力标志。现有护眼矩阵链路已验证正常并保留。

# v1.0-test1 — 2026-11-14

- 首次生成根编排整合模块，接管七个已发行补丁及四档充电组件。
- 旧模块迁移只写 `disable`，保留目录、下载和可恢复状态。
- 容量统计先就绪，再启动充电桥；PPS保持手动且默认关闭。
- 内核更新保持双击确认，小核心包不携带 boot 载荷。
