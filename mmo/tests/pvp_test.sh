#!/bin/sh
# The design notes wire table against the server's protocol.
set -eu

ROOT=${1:?usage: pvp_test.sh <mmo-root>}

DOC="$ROOT/PVP.md"
PROTO="$ROOT/../protocols.game/src/main/kotlin/de/fiereu/openmmo/net/game/GameProtocol.kt"

for f in "$DOC" "$PROTO"; do
    if [ ! -f "$f" ]; then
        echo "pvp: SKIP (no $f)"
        exit 0
    fi
done

fail=0
echo "the competitive page and the server's protocol agree:"

# Every binding in GameProtocol.kt as "DIR OP CLASS", the opcode as two
# uppercase hex digits and the class untouched. A bidi binding counts in both
# directions, the way the surfaces reader reads it.
bindings=$(sed -n \
    's/.*\b\(c2s\|s2c\|bidi\)<\([A-Za-z0-9_]*\)>(0[xX]\([0-9A-Fa-f]*\)u.*/\1 \3 \2/p' \
    "$PROTO" |
    awk '{
        op = sprintf("%02X", strtonum("0x" $2))
        if ($1 == "bidi") { print "C2S", op, $3; print "S2C", op, $3 }
        else             { print toupper($1), op, $3 }
    }')

# The page's wire table, one "DIR OP CLASS" a line. A data row is four pipes
# deep with a two-hex-digit second cell; the header and rule rows are not.
rows=$(awk -F'|' '
    /^\| *(c2s|s2c) *\| *[0-9A-Fa-f][0-9A-Fa-f] *\|/ {
        dir = $2; op = $3; cls = $5
        gsub(/^ +| +$/, "", dir); gsub(/^ +| +$/, "", op)
        gsub(/`/, "", cls); sub(/ *\(.*/, "", cls); gsub(/^ +| +$/, "", cls)
        print toupper(dir), toupper(op), cls
    }' "$DOC")

n=$(printf '%s\n' "$rows" | grep -c . || true)
if [ "$n" -lt 20 ]; then
    echo "  FAIL PVP.md's wire table parsed as $n row(s)"
    echo "       that is too few to have parsed; the reader above is broken"
    exit 1
fi

bad=
printf '%s\n' "$rows" | while read -r dir op cls; do
    printf '%s\n' "$bindings" | grep -qx "$dir $op $cls" || echo "$dir $op $cls"
done > "${TMPDIR:-/tmp}/pvp_test.$$"
bad=$(cat "${TMPDIR:-/tmp}/pvp_test.$$")
rm -f "${TMPDIR:-/tmp}/pvp_test.$$"

if [ -z "$bad" ]; then
    echo "  ok   every wire row is bound as the page states ($n row(s))"
else
    echo "  FAIL PVP.md states rows GameProtocol.kt does not bind:"
    printf '%s\n' "$bad" | sed 's/^/       /'
    echo "       the page is what moved; correct it, do not rename around it"
    fail=1
fi

# Eight of the group were renamed out of the battle group they had collided
# with, and old notes, captures and commits still use the old names. The page
# carries the mapping; losing it strands anyone reading them. Every right-hand
# name has to still be a binding, so the table cannot rot into fiction either.
was=$(awk -F'|' '
    /^\| *s2c 4[789CE] *\||^\| *c2s 48 *\||^\| *s2c 7[58] *\|/ {
        old = $3; new = $4
        gsub(/`/, "", old); gsub(/`/, "", new)
        gsub(/^ +| +$/, "", old); gsub(/^ +| +$/, "", new)
        if (old != "" && new != "" && old != new) print new
    }' "$DOC")
nwas=$(printf '%s\n' "$was" | grep -c . || true)
if [ "$nwas" -ne 8 ]; then
    echo "  FAIL PVP.md's rename table has $nwas row(s), not 8"
    echo "       old notes and captures use those names; the mapping has to stay"
    fail=1
else
    lost=
    for cls in $was; do
        printf '%s\n' "$bindings" | grep -q " $cls\$" || lost="$lost $cls"
    done
    if [ -z "$lost" ]; then
        echo "  ok   the page still maps the eight former names to live bindings"
    else
        echo "  FAIL PVP.md's rename table names bindings that are gone:$lost"
        fail=1
    fi
fi

if [ "$fail" -ne 0 ]; then
    echo "pvp: FAILED"
    exit 1
fi
echo "pvp: ok"
