# 模块兼容性审查（2026-11-13）

## 已装模块与订阅模块

- account_compat：修改RtMiCloudSDK；安装依赖R8 build-id、启动方向和SDK20。源码、Root和ROM目标不变，未发现直接内核代码冲突；新内核启动后需登录页回归。
- split_compat：挂载精确SystemUI补丁；SystemUI、启动方向、挂载实现及Root未改变，未发现直接代码冲突；分屏功能仍需候选内核实机验证。
- pen_lockfix：替换pen_bridge/supervisor.sh；与CTN730驱动不是同一层修复，均保留，不应互相替代。需吸附、拔笔、电量显示与息屏回归。
- battery_stats：临时挂载battery/bms容量节点，读取MM8013；内核电量计驱动、I2C接口与uname-r配置未变。其设计基准及采样逻辑不变，候选内核上仍需检查模块加载/卸载和HAL读数。
- pps_temp_test：未安装，仍为未实测可用性的手动实验。目标hvdcp文件未修改；这次音频合入不修复CP读温，也不能把旧实验升级为已验证。以后启用需重新做条件检查与A/B。
- meta-overlayfs v1.3.1：OverlayFS及KSU树、manual hooks保持原值，未替换成上游KSU LKM形态。须验证启动挂载、模块禁用、卸载和重启恢复。
- Droidspaces：已装模块为ROM原生后端注册，保持SYSVIPC/网络和Root配置；功能需要候选内核启动后验证。
- Yurikey / PlayIntegrityFix：未改Android运行时及其载荷，Root实现保持，但不能由源码不变推导认证结果必然不变；需要运行时回归。

## 音频归档模块与ROM集成代码

仓库audio_compat及平板实际/vendor/etc/mixer_paths_qrd.xml均在默认/特定输出路径引用
TFA_CHIP_SELECTOR值0/1/3/2/4。新控件补齐了这个接口，应验证路由；不可把新增注册等同于音质已验收。

speaker_rotation通过控制名TFA9894_CHAN_SELECTOR、SpkRot 0 Matrix和tb371fc_dsp_quad参数调用，
没有在上述调用点写死ALSA numid；这些原有控制名/参数和ADM矩阵实现均保留。
新增TFA_CHIP_SELECTOR与旧STEREO_SLOTS不是同一控制，不用一个替代另一个。

## 打包风险及处理

原CI repack.sh采用上游v2.1原厂ramdisk。整合分支已禁止运行这个重打包步骤，仅构建/上传Image和编译日志。
若后续需要R8测试boot，应基于当前平板的R8 boot保留ramdisk及启动环境，另行制作并验证。
本次未刷机，所有“源码未发现冲突”均不表示已经通过候选内核的运行时测试。
