# MM8013C06 register mapping follows this exact ROM's OEM driver exports.
# 0x10: remaining capacity; 0x12: compensated full capacity; 0x3c: design.
# No control/data register writes: w1 in i2ctransfer selects the read address.
read_word() {
    bytes=$(i2ctransfer -f -y 1 w1@0x55 "$1" r2 2>/dev/null) || return 1
    set -- $bytes
    [ "$#" = 2 ] || return 1
    case "$1:$2" in 0x??:0x??) ;; *) return 1;; esac
    echo $(( $1 + ($2 << 8) ))
}
valid_sample() {
    [ "$design" -ge 7000 ] && [ "$design" -le 15000 ] || return 1
    [ "$full" -ge 3000 ] && [ "$full" -le "$((design * 11 / 10))" ] || return 1
    [ "$remaining" -ge 0 ] && [ "$remaining" -le "$((full * 105 / 100))" ] || return 1
    [ "$soc" -ge 0 ] && [ "$soc" -le 100 ] || return 1
    difference=$((remaining * 100 / full - soc))
    [ "$difference" -ge -3 ] && [ "$difference" -le 3 ]
}
read_sample() {
    remaining=$(read_word 0x10) || return 1
    full=$(read_word 0x12) || return 1
    design=$(read_word 0x3c) || return 1
    soc=$(read_word 0x2c) || return 1
    valid_sample
}
