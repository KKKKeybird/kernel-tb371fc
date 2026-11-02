#!/system/bin/sh
# Pure helpers: safe to source in host and device regression tests.
scene_foreground() {
  awk '
    /topResumedActivity|mFocusedApp|ResumedActivity:/ {
      rank=3
      if ($0 ~ /topResumedActivity/) rank=1
      else if ($0 ~ /mFocusedApp/) rank=2
      if (match($0, /[A-Za-z0-9_.$]+\/[A-Za-z0-9_.$]+/)) {
        if (!best || rank<best) { best=rank; app=substr($0,RSTART,RLENGTH) }
      }
    }
    END { print app }
  '
}
scene_for_app() {
  case "${1%%/*}" in
    *genshin*|*mihoyo*|*miHoYo*|*HoYoverse*|*hoyoverse*) echo 20 ;;
    *game*|*tmgp*|*sgame*) echo 9 ;;
    tv.danmaku.bili|com.tencent.qqlive|*bilibili*|*youku*|*qiyi*|*video*) echo 21 ;;
    *) echo 0 ;;
  esac
}
scene_thresholds() {
  case "$1" in
    12) echo '45000 47000 49000' ;;
    9|20) echo '46000 48000 50000' ;;
    8) echo '40000 42000 44000' ;;
    *) echo '39000 41000 43000' ;;
  esac
}
scene_tier() {
  # previous tier, temperature, thresholds; immediate escalation, per-tier
  # 1.5 C hysteresis while descending, including multi-tier cooldown.
  st_prev=$1; st_temp=$2; st_a=$3; st_b=$4; st_c=$5
  st_raw=0
  [ "$st_temp" -lt "$st_a" ] || st_raw=1
  [ "$st_temp" -lt "$st_b" ] || st_raw=2
  [ "$st_temp" -lt "$st_c" ] || st_raw=3
  st_out=$st_prev
  [ "$st_out" -ge 0 ] 2>/dev/null || st_out=0
  if [ "$st_raw" -ge "$st_out" ]; then echo "$st_raw"; return; fi
  while [ "$st_out" -gt "$st_raw" ]; do
    case "$st_out" in
      3) st_exit=$((st_c-1500)) ;;
      2) st_exit=$((st_b-1500)) ;;
      1) st_exit=$((st_a-1500)) ;;
    esac
    [ "$st_temp" -lt "$st_exit" ] || break
    st_out=$((st_out-1))
  done
  echo "$st_out"
}
