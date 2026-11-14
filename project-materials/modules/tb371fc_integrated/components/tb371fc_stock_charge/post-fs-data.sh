#!/system/bin/sh
# Stop marks the init service disabled, so class main cannot start the old writer.
[ "$(cat /data/vendor/tb371fc/build-id 2>/dev/null)" = TB371FC-HyperOS3-20261103-full ] || exit 0
case "$(uname -v)" in "#25 SMP PREEMPT "*) ;; *) exit 1;; esac
[ "$(sha256sum /system/lwky/lwky_smart_charge_hyper | cut -d' ' -f1)" = b4f4742d0924839958f52d8b1cd6068123b1408f0581728eb66222345746efe8 ] || exit 0
setprop ctl.stop lwky_smart_charge
