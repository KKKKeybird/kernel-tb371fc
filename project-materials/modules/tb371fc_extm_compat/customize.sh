#!/system/bin/sh
ui_print '- TB371FC: native ExtendM ABI permissions'
ui_print '- Install together with the matching kernel; no live swap reset.'
set_perm_recursive "$MODPATH" 0 0 0755 0644
set_perm "$MODPATH/post-fs-data.sh" 0 0 0755

# TB371FC package: restore executable bits after generic installer defaults.
set_perm "$MODPATH/post-fs-data.sh" 0 0 0755
