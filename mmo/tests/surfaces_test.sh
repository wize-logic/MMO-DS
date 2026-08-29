#!/bin/sh
# The design notes declared gaps against the server's
# protocol.
set -eu

ROOT=${1:?usage: surfaces_test.sh <mmo-root>}

DOC="$ROOT/SURFACES.md"
PROTO="$ROOT/../protocols.game/src/main/kotlin/de/fiereu/openmmo/net/game/GameProtocol.kt"

for f in "$DOC" "$PROTO"; do
    if [ ! -f "$f" ]; then
        echo "surfaces: SKIP (no $f)"
        exit 0
    fi
done

fail=0
echo "the surfaces page and the server's protocol still disagree where it says:"

# The opcodes GameProtocol.kt binds in one direction, uppercase hex, one a line.
# `bidi<...>` counts as both, the same way the page's generator reads it.
bound() {
    sed -n "s/.*\b\($1\|bidi\)<[A-Za-z0-9_]*>(0[xX]\([0-9A-Fa-f]*\)u.*/\2/p" "$PROTO" |
        tr 'a-f' 'A-F' | awk '{ printf "%02X\n", strtonum("0x" $0) }' | sort -u
}

# The opcode list on a declared-gap line of the page, one a line.
declared() {
    sed -n "s/^ *$1 *//p" "$DOC" | head -1 | tr ' ' '\n' | grep -v '^$' | sort -u
}

c2s=$(bound c2s)
s2c=$(bound s2c)

# absent <label> <opcode list> <bound list>, every listed opcode must be unbound
absent() {
    label=$1; want=$2; have=$3
    n=0; bad=
    for op in $want; do
        n=$((n + 1))
        if printf '%s\n' "$have" | grep -qx "$op"; then bad="$bad $op"; fi
    done
    if [ -z "$bad" ]; then
        echo "  ok   $label ($n opcode(s) still unimplemented)"
    else
        echo "  FAIL $label"
        echo "       GameProtocol.kt now implements:$bad, update SURFACES.md"
        fail=1
    fi
}

# present <label> <opcode list> <bound list>, every listed opcode must be bound
present() {
    label=$1; want=$2; have=$3
    n=0; bad=
    for op in $want; do
        n=$((n + 1))
        if ! printf '%s\n' "$have" | grep -qx "$op"; then bad="$bad $op"; fi
    done
    if [ -z "$bad" ]; then
        echo "  ok   $label ($n opcode(s) the server still has alone)"
    else
        echo "  FAIL $label"
        echo "       GameProtocol.kt no longer binds:$bad, update SURFACES.md"
        fail=1
    fi
}

cm=$(declared c2s-server-missing)
co=$(declared c2s-server-only)
sm=$(declared s2c-server-missing)
so=$(declared s2c-server-only)

if [ -z "$cm$co$sm$so" ]; then
    echo "  FAIL SURFACES.md declares no gaps at all"
    echo "       the four c2s-/s2c-server-{missing,only} lines are the check"
    exit 1
fi

absent "c2s the official client sends and the server does not read" "$cm" "$c2s"
present "c2s the server reads and the official client cannot send"   "$co" "$c2s"
absent "s2c the official client reads and the server does not send" "$sm" "$s2c"
present "s2c the server sends and the official client cannot read"   "$so" "$s2c"

# The generator counts the same tables the page quotes; a rewrite of
# GameProtocol.kt that broke the parse above would silently pass every check, so
# require it to have found roughly the protocol it is supposed to have found.
nc=$(printf '%s\n' "$c2s" | grep -c . || true)
ns=$(printf '%s\n' "$s2c" | grep -c . || true)
if [ "$nc" -ge 100 ] && [ "$ns" -ge 100 ]; then
    echo "  ok   the protocol parsed ($nc c2s, $ns s2c opcodes bound)"
else
    echo "  FAIL GameProtocol.kt parsed as $nc c2s / $ns s2c opcodes"
    echo "       that is too few to have parsed; the reader above is broken"
    fail=1
fi

if [ "$fail" -ne 0 ]; then
    echo "surfaces: FAILED"
    exit 1
fi
echo "surfaces: all checks passed"
