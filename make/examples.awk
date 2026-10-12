# Extract the tested examples from PROTOCOL.md into a C header.
#
#   ```hex example=<name>    bytes, two hex digits each   → uint8_t  ex_<name>[]
#   ```fifo example=<name>   words, eight hex digits each → uint32_t fifo_<name>[]
#
# `#` starts a comment. Each kind also gets a table of all its examples so the
# tests can check every one without naming it.

function cname(s) { gsub(/-/, "_", s); return s }

function fail(msg) {
    printf "PROTOCOL.md:%d: %s\n", NR, msg > "/dev/stderr"
    failed = 1
    exit 1
}

BEGIN {
    print "/* Generated from PROTOCOL.md by make/examples.awk. Do not edit. */"
    print "#include <stdint.h>"
    print "#include <stddef.h>"
    nhex = 0; nfifo = 0; kind = ""
}

kind == "" && /^```(hex|fifo) example=[a-z0-9-]+[[:space:]]*$/ {
    kind = ($0 ~ /^```hex/) ? "hex" : "fifo"
    name = $0; sub(/^```[a-z]+ example=/, "", name); sub(/[[:space:]]+$/, "", name)
    if ((kind, name) in seen) fail("duplicate " kind " example name " name)
    seen[kind, name] = 1
    width = (kind == "hex") ? 2 : 8
    count = 0
    printf "static const %s %s_%s[] = {", (kind == "hex") ? "uint8_t" : "uint32_t", \
        (kind == "hex") ? "ex" : "fifo", cname(name)
    next
}

kind != "" && /^```/ {
    print "};"
    if (count == 0) fail("example " name " is empty")
    if (kind == "hex") hexnames[++nhex] = name; else fifonames[++nfifo] = name
    kind = ""
    next
}

kind != "" {
    line = $0
    sub(/#.*/, "", line)
    n = split(line, tok, /[[:space:]]+/)
    for (i = 1; i <= n; i++) {
        if (tok[i] == "") continue
        if (length(tok[i]) != width || tok[i] !~ /^[0-9A-Fa-f]+$/)
            fail("example " name ": '" tok[i] "' is not " width " hex digits")
        printf "%s0x%s", (count++ ? ", " : ""), tok[i]
    }
}

END {
    if (failed) exit 1
    if (kind != "") fail("example " name " is not closed")

    print ""
    print "struct protocol_example { const char *name; const uint8_t *bytes; size_t len; };"
    print "struct fifo_example { const char *name; const uint32_t *words; size_t len; };"
    print ""
    print "static const struct protocol_example protocol_examples[] = {"
    for (i = 1; i <= nhex; i++)
        printf "    { \"%s\", ex_%s, sizeof ex_%s },\n", hexnames[i], cname(hexnames[i]), cname(hexnames[i])
    print "};"
    print "static const struct fifo_example fifo_examples[] = {"
    for (i = 1; i <= nfifo; i++)
        printf "    { \"%s\", fifo_%s, sizeof fifo_%s / sizeof fifo_%s[0] },\n", \
            fifonames[i], cname(fifonames[i]), cname(fifonames[i]), cname(fifonames[i])
    print "};"
    printf "#define PROTOCOL_EXAMPLE_COUNT %d\n", nhex
    printf "#define FIFO_EXAMPLE_COUNT %d\n", nfifo
}
