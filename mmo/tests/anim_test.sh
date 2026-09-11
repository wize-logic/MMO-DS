#!/bin/sh
# The design notes against the overlay, the fallback, and the engine
# archive.
set -eu

ROOT=${1:?usage: anim_test.sh <mmo-root> [engine-dir]}
ENGINE=${2:-}

DOC="$ROOT/ANIM.md"
OVERLAY="$ROOT/src/display_overlay.gen.h"
IDMAP="$ROOT/include/idmap.h"
ANIMH="$ROOT/include/battle_anim.h"
LIMITS="$ROOT/ENGINE_LIMITS.md"

for f in "$DOC" "$OVERLAY" "$IDMAP" "$ANIMH" "$LIMITS"; do
    if [ ! -f "$f" ]; then
        echo "anim: SKIP (no $f)"
        exit 0
    fi
done

fail=0
echo "the animation page and the trees it describes agree:"

ok()  { echo "  ok   $1"; }
bad() { echo "  FAIL $1"; echo "       $2"; fail=1; }

flat=$(tr '\n' ' ' < "$DOC" | tr -s ' ')

says() {
    if printf '%s\n' "$flat" | grep -Fq "$2"; then
        ok "$1"
    else
        bad "$1" "ANIM.md does not say: $2"
    fi
}

# Overlay-only move names live at catalogue base 110000 + id, so custom
# ids 1000.. are the { 111xxx rows. Snowscape is 110258, a rename of a
# DS move, and must not count.
custom=$(grep -c '{ 111' "$OVERLAY" || true)
max=$(sed -n 's/^#define MMO_MOVE_MAX[[:space:]]\{1,\}\([0-9]\{1,\}\).*/\1/p' "$IDMAP" | head -1)
pound=$(sed -n 's/^#define MMO_BTL_ANIM_FALLBACK_MOVE[[:space:]]\{1,\}\([0-9]\{1,\}\).*/\1/p' "$ANIMH" | head -1)

if [ "$custom" = 91 ]; then
    ok "the overlay still names 91 custom moves"
else
    bad "the overlay still names 91 custom moves" "display_overlay.gen.h has $custom '{ 111' rows"
fi
says "the page publishes that overlay count" "**91** names at 1000..1096"

# 467 is the engine's own last move (Shadow Force), the last id StartMove
# plays as itself. MMO_MOVE_MAX names the last move both halves name, which
# the Gen 5 port carried past 467 (idmap.h); past the engine's own, the
# animation is a borrowed one, so the page's number is the engine's.
# generated/moves.txt is the engine's move list, one name a line, id = line - 1.
line=$(grep -n '^MOVE_SHADOW_FORCE$' "$ENGINE/generated/moves.txt" | cut -d: -f1 | head -1)
engine_last=$(( ${line:-0} - 1 ))
if [ "$engine_last" = 467 ] && [ "${max:-0}" -ge 467 ]; then
    ok "the engine's last own move is still 467, and idmap names at least that ($max)"
else
    bad "the engine's last own move is still 467, and idmap names at least that" \
        "MOVE_SHADOW_FORCE is ${engine_last:-unreadable}, MMO_MOVE_MAX is $max"
fi
says "the page publishes that last id" "**467**"

if [ "$pound" = 1 ]; then
    ok "the compiled fallback is still Pound"
else
    bad "the compiled fallback is still Pound" "MMO_BTL_ANIM_FALLBACK_MOVE is $pound"
fi
says "the page names Pound as the fallback" "Pound"

# The limits page is what a later run reads first; if it still says
# authoring is unpriced, or drops the pointer, the register is orphaned.
limits=$(tr '\n' ' ' < "$LIMITS" | tr -s ' ')
if printf '%s\n' "$limits" | grep -Fq "priced and not planned"; then
    ok "ENGINE_LIMITS still files authoring as priced and not planned"
else
    bad "ENGINE_LIMITS still files authoring as priced and not planned" \
        "entry 4 no longer says: priced and not planned"
fi
if printf '%s\n' "$limits" | grep -Fq "ANIM.md"; then
    ok "ENGINE_LIMITS still points at the register"
else
    bad "ENGINE_LIMITS still points at the register" \
        "entry 4 no longer names ANIM.md"
fi

if [ -n "$ENGINE" ] && [ -d "$ENGINE/res/moves" ]; then
    anims=$(find "$ENGINE/res/moves" -name 'anim.s' | wc -l)
    # find | wc pads on some wc; strip.
    anims=$(printf '%s' "$anims" | tr -d ' ')
    if [ "$anims" = 468 ]; then
        ok "the engine checkout still has 468 anim.s"
    else
        bad "the engine checkout still has 468 anim.s" "res/moves has $anims anim.s"
    fi
    says "the page publishes that anim.s count" "**468**"

    narc="$ENGINE/build/rom/res/moves/anim_scripts.narc"
    if [ -f "$narc" ]; then
        members=$(python3 -c '
import struct, sys
data = open(sys.argv[1], "rb").read()
if data[:4] != b"NARC":
    sys.exit("not a NARC")
print(struct.unpack_from("<H", data, 0x18)[0])
' "$narc")
        if [ "$members" = 501 ]; then
            ok "the built archive still has 501 members"
        else
            bad "the built archive still has 501 members" \
                "anim_scripts.narc has $members members"
        fi
        says "the page publishes that member count" "**501**"
        says "the page still calls the extra members leftovers" "**33**"
    else
        echo "  skip the built archive (no anim_scripts.narc)"
    fi
else
    echo "  skip the engine checkout (no res/moves)"
fi

if [ "$fail" -eq 0 ]; then
    echo "anim: all checks passed"
else
    echo "anim: FAILED"
fi
exit "$fail"
