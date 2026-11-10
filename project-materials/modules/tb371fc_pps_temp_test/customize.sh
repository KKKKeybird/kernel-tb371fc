SKIPUNZIP=0
ui_print '! 未实测可用性：PPS充电泵温度实验 v0.3'
[ "$(cat /data/vendor/tb371fc/build-id 2>/dev/null)" = TB371FC-HyperOS3-20261103-full ] || abort '! Requires exact R8 ROM'
[ "$(uname -r)" = 4.19.325-perf++ ] || abort '! Wrong kernel'
[ -x /data/adb/ksu/bin/ksud ] || abort '! KernelSU / BakaSU required'
[ "$(sha256sum /vendor/bin/hvdcp_opti | cut -d ' ' -f 1)" = f3175c83a4f27f7f2e99ccc19a3ea893c1cc15dda20a8881073f5ed4a2c2db8a ] || abort '! Unknown or already mounted charging daemon'
[ "$(sha256sum "$MODPATH/hvdcp_opti.temp-chain-v3-test" | cut -d ' ' -f 1)" = c4f7c8921cef1897fa3bcc5134251a4a35887262c31510da5ae093d35f700285 ] || abort '! Damaged experimental payload'
for f in action.sh run-test.sh rollback.sh uninstall.sh; do set_perm "$MODPATH/$f" 0 0 0755; done
ui_print '- No boot service; installation does not activate the experiment'
ui_print '- Run module action manually with PPS, SOC15-80%, normal charging'
ui_print '- Maximum120s test, automatic rollback, native thermal protections retained'
