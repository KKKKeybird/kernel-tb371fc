# MiSound Dirac GEQ guard

The HyperOS MiSound package advertises Dirac, but the Lenovo-original vendor
audio stack on TB371FC has no `libdirac.so` or matching Dirac effect.
Opening "Graphic equalizer" therefore crashes `com.miui.misound`.

The ROM also omits `ro.vendor.audio.dolby.dax.support`, even though
AudioFlinger has loaded Dolby `DAP`/`DAP_game` effects and the original vendor
effect configuration is intact. The parent module restores this compatibility
property before app processes start. "Xiaomi Sound" in this page is the Dirac
implementation and remains unavailable because the ROM has no Dirac library.

This component generates a patched MiSound APK at runtime from the exact
installed base APK. It replaces only `classes.dex`; resources, manifest,
libraries and all other ZIP members remain unchanged.

- Hides the Graphic equalizer entrances in both MiSound headset layouts.
- Rejects the Dolby EQ activity when the Dirac implementation is absent.
- Keeps the headset settings page usable when every effect backend reports
  unavailable: the empty selection falls back to `none` instead of a null value.
- Leaves Dolby Atmos/DAP and the Lenovo vendor audio tuning untouched.
- Creating `components/tb371fc_geq_guard/disable` unmounts the patch and stops
  the current MiSound process, restoring the original APK view.

The patch was tested with MiSound `15-3.2-3-20251117` versionCode `251117`.
The original APK independently reproduced the all-backends-unavailable NPE,
which confirms it is a ROM compatibility defect rather than a module regression.
Unknown base APK hashes are refused.
