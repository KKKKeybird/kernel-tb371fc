#!/system/bin/sh
# Keep user preferences that MIUI resets when input devices are (re)attached.
# v2: avoid spawning `settings` (a full app_process) every 15s; read the settings
# XML directly (page-cached, microseconds) and only write when the value is wrong.
LOG=/data/adb/touch-prefs.log
DB=/data/system/users/0/settings_system.xml

i=0
while [ "$(getprop sys.boot_completed)" != 1 ] && [ $i -lt 150 ]; do sleep 2; i=$((i+1)); done
sleep 5

set_tap() {
  cur=$1
  command -v settings >/dev/null 2>&1 || return 0
  settings put system touchpad_tap_to_click 1 2>/dev/null
  echo "$(date '+%F %T') touchpad_tap_to_click -> 1 (was ${cur:-missing})" >> "$LOG"
}

# First pass: reliable read via `settings` once at boot, then cheap XML polling.
cur=$(settings get system touchpad_tap_to_click 2>/dev/null)
[ "$cur" = "1" ] || set_tap "$cur"

# Lightweight guard: only reads the XML; writes only when MIUI resets it.
while true; do
  sleep 60
  [ -r "$DB" ] || continue
  v=$(grep -o 'name="touchpad_tap_to_click" value="[^"]*"' "$DB" 2>/dev/null | sed -n 's/.*value="\([^"]*\)".*/\1/p' | head -1)
  if [ -n "$v" ] && [ "$v" != "1" ]; then
    set_tap "$v"
  fi
done
