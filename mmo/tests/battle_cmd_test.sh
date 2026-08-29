#!/bin/sh
# The design notes against the engine enum and the server list.
set -eu

ROOT=${1:?usage: battle_cmd_test.sh <mmo-root> [engine-dir]}
ENGINE=${2:-}

DOC="$ROOT/BATTLE.md"
HDR="$ENGINE/include/constants/battle/battle_controller.h"
ANIM="$ROOT/include/battle_anim.h"
PROTO="$ROOT/../protocols.game/src/main/kotlin/de/fiereu/openmmo/net/game/GameProtocol.kt"

for f in "$DOC" "$ANIM" "$PROTO"; do
    if [ ! -f "$f" ]; then
        echo "battle cmd: SKIP (no $f)"
        exit 0
    fi
done

fail=0
echo "the presentation page and the trees it describes agree:"

ok()  { echo "  ok   $1"; }
bad() { echo "  FAIL $1"; fail=1; }

# The 67 data rows, one "N name" a line. The separator row has no integer.
rows=$(awk -F'|' '
    /^\| *[0-9]+ *\|/ {
        n = $2; name = $3
        gsub(/^ +| +$/, "", n)
        gsub(/^ +| +$/, "", name)
        print n, name
    }' "$DOC")

got=$(printf '%s\n' "$rows" | wc -l)
if [ "$got" -eq 67 ]; then
    ok "the page lists 67 commands"
else
    bad "the page lists 67 commands (got $got)"
fi

# Lab-yes count, so a re-measure that finds a different 24 fails until the
# page says so.
lab=$(awk -F'|' '
    /^\| *[0-9]+ *\|/ {
        lab = $5
        gsub(/^ +| +$/, "", lab)
        if (lab == "yes") n++
    }
    END { print n + 0 }' "$DOC")
if [ "$lab" -eq 24 ]; then
    ok "the lab fight still accounts for 24 of them"
else
    bad "the lab fight still accounts for 24 of them (page says $lab)"
fi

if [ -n "$ENGINE" ] && [ -f "$HDR" ]; then
    enum=$(awk '
        /enum BattleCommand/ { in_e = 1; next }
        in_e && /}/ { exit }
        in_e {
            sub(/\/\/.*/, "")
            gsub(/[ ,]/, "")
            if ($0 ~ /^BATTLE_COMMAND_/) {
                sub(/^BATTLE_COMMAND_/, "")
                print
            }
        }' "$HDR")
    page=$(printf '%s\n' "$rows" | awk '{ print $2 }')
    if [ "$enum" = "$page" ]; then
        ok "the 67 names match the engine enum in order"
    else
        bad "the 67 names match the engine enum in order"
        printf '%s\n' "$enum" > /tmp/battle-cmd-enum.$$
        printf '%s\n' "$page" > /tmp/battle-cmd-page.$$
        diff /tmp/battle-cmd-enum.$$ /tmp/battle-cmd-page.$$ | head -8 \
            | sed 's/^/       /' || true
        rm -f /tmp/battle-cmd-enum.$$ /tmp/battle-cmd-page.$$
    fi
else
    echo "  skip engine enum (no header under ${ENGINE:-unset})"
fi

# The three lists, uppercase hex, one a line.
listed() {
    sed -n "s/^ *$1: *//p" "$DOC" | head -1 | tr 'a-f' 'A-F' \
        | tr ' ' '\n' | grep -v '^$' | sort -u
}

mapped=$(listed mapped-s2c)
consumed=$(listed consumed-s2c)
unmapped=$(listed unmapped-s2c)

if [ -z "$mapped$consumed$unmapped" ]; then
    bad "the page declares the three s2c lists"
    echo "battle cmd: FAILED"
    exit 1
fi

# Overlap between any two lists is a row that cannot decide.
overlap() {
    printf '%s\n%s\n' "$1" "$2" | sort | uniq -d
}

ov=$(overlap "$mapped" "$consumed")
[ -z "$ov" ] || bad "mapped and consumed overlap:$ov"
ov=$(overlap "$mapped" "$unmapped")
[ -z "$ov" ] || bad "mapped and unmapped overlap:$ov"
ov=$(overlap "$consumed" "$unmapped")
[ -z "$ov" ] || bad "consumed and unmapped overlap:$ov"

# GameProtocol.kt Battle* s2c/bidi bindings. A name is in the battle group when
# it carries Battle anywhere, LinkBattleOpen and LinkBattleData are the duel's
# and belong here, except a matchmaking one, which is named for the battles it
# lists rather than for being one. Without that exclusion a rename out of this
# group passes unnoticed whenever the new name happens to keep the word.
bound=$(sed -n 's/.*\b\(s2c\|bidi\)<\([A-Za-z0-9_]*Battle[A-Za-z0-9_]*\)>(0[xX]\([0-9A-Fa-f]*\)u.*/\2 \3/p' \
            "$PROTO" \
        | awk '$1 !~ /^Matchmaking/ { printf "%02X\n", strtonum("0x" $2) }' \
        | sort -u)

page_all=$(printf '%s\n%s\n%s\n' "$mapped" "$consumed" "$unmapped" \
           | grep -v '^$' | sort -u)

if [ "$bound" = "$page_all" ]; then
    n=$(printf '%s\n' "$bound" | grep -c . || true)
    ok "the three lists partition the $n Battle* s2c bindings"
else
    bad "the three lists partition the Battle* s2c bindings"
    printf '%s\n' "$bound" > /tmp/battle-cmd-bound.$$
    printf '%s\n' "$page_all" > /tmp/battle-cmd-lists.$$
    echo "       only on the server:"
    comm -23 /tmp/battle-cmd-bound.$$ /tmp/battle-cmd-lists.$$ \
        | sed 's/^/         /' || true
    echo "       only on the page:"
    comm -13 /tmp/battle-cmd-bound.$$ /tmp/battle-cmd-lists.$$ \
        | sed 's/^/         /' || true
    rm -f /tmp/battle-cmd-bound.$$ /tmp/battle-cmd-lists.$$
fi

# The mapped command numbers this tree compiles have to be the ones the
# page claims, or a rename here leaves the page describing a different
# vocabulary.
says_num() {
    name=$1
    want=$2
    got=$(sed -n "s/^#define MMO_BTLCMD_$name[[:space:]]\{1,\}\([0-9]\{1,\}\).*/\1/p" \
              "$ANIM" | head -1)
    if [ "$got" = "$want" ]; then
        ok "MMO_BTLCMD_$name is $want"
    else
        bad "MMO_BTLCMD_$name is $want (header has ${got:-none})"
    fi
}

says_num PRINT_ATTACK_MESSAGE 20
says_num SET_MOVE_ANIMATION 22
says_num SHOW_POKEMON 4
says_num RETURN_POKEMON 5
says_num OPEN_CAPTURE_BALL 6
says_num SHOW_PARTY_MENU 18
says_num UPDATE_EXP_GAUGE 25
says_num PLAY_LEVEL_UP_ANIMATION 35
says_num PLAY_FAINTING_SEQUENCE 26

if [ "$fail" -ne 0 ]; then
    echo "battle cmd: FAILED"
    exit 1
fi
echo "battle cmd: all checks passed"
