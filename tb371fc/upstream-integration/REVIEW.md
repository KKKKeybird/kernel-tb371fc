# 上游v2.2逐提交合入审查

基线：3e667a1fcf37a634a533d991888ad1694e5b8694；上游：c3df39c507c11eebb124224bd34e3ab701b6d1f3。
采用三方整合最终状态，逐叶提交审查，避免中间版本的固件预热缺陷。

1. `7d593a3be51c` T-90 P1: revert nxp34663 increments, remove tag/pr_fmt prefixes, level resets

   采纳：移除非原厂nxp增量；保留本地旋转/混音实现。

2. `bd21d5ba8f8b` T-90 P2: nxp34663 plop chain revert (tfa.h/tfa98xx.h/tfa98xx.c), C-class ours-only print deletions (p236/p245/p249/p269/[NXP]/chan_sel), stock corpus ours-only 158 residual (bare-017 mapping pending)

   采纳最终效果：移除plop路径及非必要打印，不删除本地旋转扩展。

3. `7cb6fda612da` T-90 P3: delete all ours-only prints (tfa98xx.c/tfa_dsp.c/tfa_init.c), corpus ours-only down to 14 non-print residue (identifiers/DT/funcnames)

   采纳：清理非原厂打印；不删除本地暖启动/路由功能。

4. `1abc6a7638c3` T-90 P4: review G2 fix (manstate/retry residue), G4 companion cleanup (unused i2cbus/ret vars, unused info cb), tfa unit zero-warning

   采纳最终修正版：清理中间实现遗留，保留本地启动后路由重应用。

5. `7396a8ed43fa` T-90 F1/F3: review verdict execution - delete SmartPA Mute/SPK ID/nxp vendor-id/spk-id-pin (F1), delete misc_device RPC architecture face 13 fns + device nodes (F3), align dev_set_name to stock %s.00%2x + err print, tfa_reset PRINT_ASSERT x3 out (stock none), version string v6.7.14, __FILE__ fmacro-prefix-map to vendor layout; corpus ours-only=0

   采纳：按上游删除旧调试控件和misc节点；设备普通播放/旋转未命中，41个音频映射库及配置未发现字符串引用；通话等未覆盖路径仍需回归。

6. `6f015c1f9de9` T-90 W90-F4: stock spelling rename batch (11 pairs verified: reg/mem read-write, dsp_partial, features_from_cnt x2, set_ops_defaults, show_current_state, send_chan_sel_to_dsp + header decls); dsp_msg_read pair split (burst=stock name, wrapper=ours inline); G4 receipt companion read_data_from_hostdsp

   适配采纳：同步函数重命名，并更新本地并发旋转工作函数的调用。

7. `f67cb05bc592` T-90 D1-a: chan_sel path 9 stock print restorations (set_ctl: 4*9894/call-disabled/same-angle/set-selector-from/set-chan-sel, get_ctl: flag, send: no-need-send x2 + end-flip), byte-level decorator match (%s(): form), rotation.c end-flip replaces ours pr_debug per stock, change_chan_sel silent per stock asm

   适配采纳：打印修正；保持本地硬件寄存器校验，不能按相同缓存角度跳过。

8. `c92dd65c48e5` T-90 D1-a fix: review FAIL corrections - get_ctl reverted to stock 5-instr accessor (no loop/print), set-chan-sel fmt+args aligned to stock (baked s, csel sucess/fail), rotation vol-loop err print removed (stock refs=1 get_target only)

   部分适配：上游getter与打印采纳；缓存角度提前返回不采纳，以保留profile重载后的路由重应用。

9. `7498423cb2af` T-90 D1-b: 12 stock print restorations (L007 dsp_init skip, L008 sample-rate, L014 FW-API check branch, L021 Calibration value relocated to tfa_start per stock anchor, L022 force-start, L026/L027 probe rev+addr, L038 keep-powerdown, L039/L040 standby, L041 warning-reset, L091 i2c-bus-id) + C-class Misc-errors merged print retained (stock hosts=F2) per review D1 verdict

   采纳最终打印及位置修正，保留本地启动和路由语义。

10. `e64ed47d3593` T-90 F2-fix: FW API check rewritten per stock tfaGetFwApiVersion dual-path (review correction: 0xfe probe was wrong) - now: container subversion sscanf->minor gate, APIV marker check, ops mem_read XMEM 0x209 (non-ext-dsp path), 4-seg compare vs container bytes 24..27; ext-dsp write_dsp path deferred to W90-F2 behavioral batch

   采纳：API版本检查双路径修正，不保留中间错误probe。

11. `023b3c326a20` T-90 F2-fix: sscanf fmt string literal fix (double-quote artifact)

   采纳：sscanf格式字符串修正。

12. `13a848ef139d` T-90 D1-c: review D1-b verdict fixes - L008 sample-rate print added via printk (bare tfa98xx: prefix, bypass pr_fmt per stock), sucess/fail print moved into send loop per-device (stock @0x170bc-0x170d8 csel per-send semantics)

   采纳最终打印修正；设备切换仍使用本地并行工作队列。

13. `4af2b8240f67` T-90 D1-d: L008 sample-rate print decorator fix (stale-object catch: pr_fmt single decorator now applied; pr_err replaces printk per review correction - stock has %s(): decorator) + L014 __FILE__->func (F2fix leftover)

   采纳：格式/函数名修正。

14. `ba42f5dfc0ae` T-90 W90-F2 behavioral batch: tfa_dev_set_tdm_bitwidth restored per stock @0x6b08 (16/24/32 gate + TDMNBCK/TDMSLLN/TDMSSIZE via TFA_SET_BF, unsupported->Bad_Parameter), tfaGetFwApiVersion dual-path per stock @0x50f8 (XMEM 0x209 mem_read + ext-dsp msg {0,0x80,0xfe} deferred to write_dsp implementation), tfa_device bitwidth field + hw_params store + tfaRunStartup call site (stock @0x5b3c-0x5b50 daimap==TDM gate), cn file TDM config absent confirmed (TDM config source = driver not cnt)

   采纳：TDM位宽配置和固件API读取；保留本地声道数/矩阵逻辑。

15. `df153d07156a` T-90 F2 API print level + backslash fix (stock \x017 pr_debug; literal backslash-n artifact)

   采纳：日志级别与反斜杠修正。

16. `5be0734b7a83` T-90 W90-F2 batch 2: tfa_is_94_N2_device (rev==0x94 && subver>0x1a per stock @0x4ec8) + tfa2_manual_mtp_cpy (ext_dsp gate + get_mtpb wait + reg_read + flag-based reg_write order per stock @0x18b0)

   采纳：N2识别和MTP操作顺序修正。

17. `ecb825ed6855` T-90 W90-D117: TDM bitwidth path aligned to stock (gate + coordinates + RMW shape)

   采纳：最终按芯片版本分派的TDM位宽寄存器路径。

18. `92734d2a8a9d` T-90 W90-D118(partial): register TFA_CHIP_SELECTOR stereo control per stock

   采纳：新增TFA_CHIP_SELECTOR，与本地TFA9894_STEREO_SLOTS并存。

19. `20c79aff35da` T-90 W90-D120+D121: rev 0x94 flags = MULTI_MIC|TDM only (stock 0x108, no SKIP_INTERRUPTS; runtime SKIP from probe irq-gpio check untouched); chan_sel slot registered unconditionally per stock container_loaded 0x15a3c..0x15ad8 (both component and codec forms); per review verdict REVIEW-VERDICT-FLAGS-D117-D120122-20261101.md §2/§3; U2 already satisfied by D118 (global selector stored before mutex_lock, stock 0x16c90); object-level check passed: helper inlined to stock-shaped read/bic/insert/write with -100-err propagation (cmn w0,#100 tolerant path, mov w8,#-100;sub w1,w8,w0 print arg), bf constant folding (TDME 0x2000->reg 0x20/pos0, NBCK 0x2103->mask 0xf@0x21, SLLN 0x2204->mask 0x1f@0x22), jump table cmp #34 + br x11 same shape as stock 0x6bcc

   采纳：rev94 flags和控制注册修正，保留本地通话profile保护。

20. `a24e0c456fc2` T-90 W90-D123: SPK-ID strap container selection per stock -- probe requests hard-wired gpio 1171 (tfa98xx_spk_id, stock 0x13630; probe fails on request error, stock 0x1365c), reads raw level via two gpio_to_desc round trips (stock 0x13664-0x1367c shape), parks it in named spk_id_level field (stock tfa+0x3C0), frees the line and prints stock dev_info with constant 71 (0x1369c); tail selector picks fw_name_second(tfa98xx_QS.cnt) on level==0 else fw_name_first(tfa98xx_YC.cnt) (stock 0x14b44 cbz; both charp 0644 verbatim __param entries, defaults from stock .data ABS64 relocs); pointer parked in tfa98xx->fw_name member (stock tfa+0x328); consumers (container_loaded prints, dsp-load request_firmware) use the per-device name; p249 prewarm covers both stock names; stock-side device runs YC (GPIO1171=1 verified on HA1ZXE2M) so this fixes 4/4 amps getting the wrong container (ours-only=0, stock-only corpus 94->90)

   适配采纳最终变体选择；不单独采用会预热未内建YC的中间形式。

21. `175b9825d670` T-90 W90-D123 final: SPK-ID strap container selection (GPIO1171 per stock 0x13630/0x14b44) + bootloop fix -- root cause chain pinned by the restored kmsg_tb371_log flight recorder: p249 prewarming fw_name_first (tfa98xx_YC.cnt, not in CONFIG_EXTRA_FIRMWARE) triggered the firmware UMH fallback on the kernel_init thread before userland exists, stalling init past the 10s wlan MHI mission-mode window; cnss TIMEOUT(3) fired a restart_level=SYSTEM subsystem restart = bootloop (secondary panic: kb_i2c_suspend in the reboot device_shutdown path, pre-existing). Fixes: (1) p249 prewarms only the built-in name fw_name_second (tfa98xx_QS.cnt, CONFIG_EXTRA_FIRMWARE); (2) tfa98xx_YC.cnt added to CONFIG_EXTRA_FIRMWARE (sha256 8e7d4a1c4d9587382150cd949e202100d64e17447719a6b6efdb7611ffb952f5, copy of /vendor/firmware/tfa98xx_YC.cnt) so the per-device YC container resolves built-in regardless of firmware_class.path (droidspaces overrode it to /mnt/Droidspaces/alpine/lib/firmware) and ueventd directory list; (3) kmsg_tb371_log flight recorder re-wired (Kconfig default y, §8.4 resident diagnostic - file survived in dev history worktree d110-f10-wt, was dropped from the Kconfig in the current tree). Verified live: spk_gpio flag=1 x4 -> loaded tfa98xx_YC.cnt size 26245 -> 4x codec registered (TFA) -> sound card + TFA_CHIP_SELECTOR/TFA9894_CHAN_SELECTOR present; param face fw_name_first/fw_name_second/pcm_sample_format=0; stock-parity deviation (built-in YC vs stock filesystem load) logged as registered deviation for review

   适配采纳：YC/QS按GPIO选择及最终预热方式；同时为#24配置内建YC，使用相对固件链接。临时飞行记录器不引入。

22. `52c7e597cc22` TASK-091: kb_i2c_suspend NULL guard (review-approved robustness deviation) -- vendor Spruce entries kb_hid_suspend/kb_hid_resume guard on g_client but kb_i2c_suspend did not; abnormal shutdown before kb probe completion (g_client NULL, i2c-hid-core.c:143) dereferenced it in i2c_hid_set_power (NULL+0xd8, panic captured by kmsg_tb371_log seq 713049 during the D-123 bootloop forensics); guard placed before the mcu_resume_gpio side effect, matching the kb_hid_suspend guard position; note: NULL host is g_client (source-verified), review verdict text said g_ihid -- corrected in receipt

   采纳：kb_i2c_suspend空指针保护；本地KEY_POWER误触过滤保持。

23. `09ccd57b9e83` Merge dev: T-90 audio tfa98xx full stock alignment (D117-D123) + TASK-091 kb guard + YC container built-in

   作为合并历史保留，内容按上述叶提交逐项审查，不重复应用。

24. `286b13393c1a` strip(release): remove kmsg_tb371_log flight recorder from main (TASK-058 precedent; dev-branch resident diagnostic per AGENTS 8.4; recoverable from dev history)

   发布诊断移除的最终效果已满足；本地没有该飞行记录器，无需反向增加。

25. `c3df39c507c1` docs: v2.2 release notes (audio TFA9894 stock alignment + issue #1 fix + bootloop fix, build #789)

   保留上游v2.2说明并另行记录本分支合入情况，构建仍沿用本地Root和#24配置。

## 实机证据边界

#24设备旧控件/节点存在。用户播放和旋转测试期间，探针未记录旧接口调用；tinymix只读自检捕获4次getter，证明探针正常。41个已映射音频相关库及配置未找到旧名称引用。未覆盖通话、杜比切换、耳机与蓝牙路径，不能声称所有闭源组件绝无依赖。

## 保持不变

Root及钩子、CTN730、键盘状态帧过滤、zram/ExtendM、NVT接口、thermal_message、mi_asap、ADM矩阵与LL/ULL路径、相对头文件链接及所有模块订阅均保留。充电、相机、GPU代码不改。

## 当前验证

源码冲突已处理；未向平板刷入候选内核。编译与候选内核上的功能回归尚未完成，不等同于已验收可用。
