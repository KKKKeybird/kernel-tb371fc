#!/system/bin/sh
# mksh closes extra FDs on exec; BusyBox ash preserves the flock FD.
BB=/data/adb/ksu/bin/busybox
[ -x "$BB" ] || exit 1
if [ ! /proc/$$/exe -ef "$BB" ]; then exec "$BB" sh "$0" "$@"; fi
# The server is the hash-pinned original device artifact. This script/probe
# adds lifecycle safety; it does not claim native hardware power hints.
DIR=/data/adb/tb371fc_powermodule
umask 077
mkdir -p "$DIR" || exit 1
exec 9>"$DIR/daemon.lock" || exit 1
/system/bin/flock -n 9 || exit 0
log() { echo "$(date '+%F %T') $*" >> "$DIR/service.log"; }
healthy() {
  CLASSPATH="$DIR/probe.jar" timeout 8 /system/bin/app_process /system/bin PowerModuleProbe \
    9>&- > "$DIR/probe.log" 2>&1
}
existing_server() {
  for p in $(pidof app_process app_process64); do
    c=$(tr '\000' ' ' < "/proc/$p/cmdline" 2>/dev/null)
    case "$c" in *' PowerModuleStub'|*' PowerModuleStub '*) return 0;; esac
  done
  return 1
}
child=; child_start=
owned_child() {
  [ -n "$child" ] && [ -n "$child_start" ] &&
    [ "$(awk '{print $4 ":" $22}' /proc/$child/stat 2>/dev/null)" = "$$:$child_start" ]
}
trap 'exit 0' HUP INT TERM
while :; do
  if healthy; then
    state=healthy
  elif existing_server; then
    # Never kill adopted old servers: a client may still hold their binder.
    # Only recycle a process which is still this supervisor's direct child.
    if owned_child; then
      kill -TERM "$child" 2>/dev/null
      /system/bin/sleep 2 9>&-
      owned_child && kill -KILL "$child" 2>/dev/null
      state='unhealthy owned server terminated; will recover next pass'
      child=
    else
      state='unhealthy adopted server; not killing clients or creating duplicate'
    fi
  else
    expected=2318400cc746a507b84ea8481dcc2ad6ca7701644b882d3596b266f244af94d9
    actual=$(sha256sum "$DIR/service.jar" | cut -d' ' -f1)
    if [ "$actual" != "$expected" ]; then
      log "ERROR server digest mismatch $actual"; exit 1
    fi
    CLASSPATH="$DIR/service.jar" /system/bin/app_process /system/bin PowerModuleStub \
      9>&- >> "$DIR/server.log" 2>&1 &
    child=$!
    child_start=$(awk '{print $22}' /proc/$child/stat 2>/dev/null)
    state="started pid=$child"
  fi
  [ "$state" = "$last" ] || { log "$state"; last=$state; }
  /system/bin/sleep 60 9>&-
done
