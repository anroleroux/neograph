# Check that src/protocol.h and ui/content/protocol.js define the same PKT_, MSG_,
# FIELD_, NET_OPT_, NET_LEASE_, PWM_, SLIDER_, BUTTON_, VALUE_, PIN_, ADC_, GPI_ and KEYBOARD_
# constants with the same values.
#
#   awk -f make/check-constants.awk src/protocol.h ui/content/protocol.js

function num(s,    i, c, v) {
    if (s !~ /^0[xX]/) return s + 0
    v = 0
    for (i = 3; i <= length(s); i++) {
        c = index("0123456789abcdef", tolower(substr(s, i, 1))) - 1
        v = v * 16 + c
    }
    return v
}

FNR == 1 { file++ }

file == 1 && /^#define (PKT|MSG|FIELD|NET_OPT|NET_LEASE|PWM|SLIDER|BUTTON|VALUE|PIN|ADC|GPI|KEYBOARD)_[A-Z0-9_]+[ \t]/ { c[$2] = num($3) }

file == 2 && /^const (PKT|MSG|FIELD|NET_OPT|NET_LEASE|PWM|SLIDER|BUTTON|VALUE|PIN|ADC|GPI|KEYBOARD)_[A-Z0-9_]+[ \t]*=/ {
    line = $0
    sub(/^const /, "", line); sub(/;.*/, "", line)
    split(line, kv, /[ \t]*=[ \t]*/)
    js[kv[1]] = num(kv[2])
}

END {
    bad = 0
    for (k in c) {
        if (!(k in js)) { print "protocol.js is missing " k > "/dev/stderr"; bad = 1 }
        else if (c[k] != js[k]) { print k ": protocol.h " c[k] ", protocol.js " js[k] > "/dev/stderr"; bad = 1 }
    }
    for (k in js)
        if (!(k in c)) { print "protocol.h is missing " k > "/dev/stderr"; bad = 1 }
    n = 0; for (k in c) n++
    if (n == 0) { print "no constants found in protocol.h" > "/dev/stderr"; bad = 1 }
    if (!bad) print "protocol constants: " n " agree between C and JS"
    exit bad
}
