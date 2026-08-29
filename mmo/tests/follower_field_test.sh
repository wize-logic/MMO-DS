#!/bin/sh
# A Pokemon walking behind the player, in a running
# field.
#
#   1. a party with nothing that can walk gets no follower, and says so, the
#      silent version of this is indistinguishable from the tick never running,
#      which is the bug that cost the most time here.
#   2. a party with a Bulbasaur seats one, at the graphics id the generated
#      table resolves and the package planted.
#   3. it draws: the rasterizer's own polygon list gains a 32x32 quad that the
#      same walk without a follower does not have.
#   4. the height rule bites: a Steelix is one of the 31 large ones and the
#      new-game bedroom is indoors, so it is refused BY NAME rather than drawn
#      through a wall.
set -eu

ROOT=${1:?usage: follower_field_test.sh <mmo-root> <build-dir> <engine-dir>}
BUILD=${2:?usage: follower_field_test.sh <mmo-root> <build-dir> <engine-dir>}
ENGINE=${3:?usage: follower_field_test.sh <mmo-root> <build-dir> <engine-dir>}

FUSED="$BUILD/fused/pokeplatinum"
ROM="${PC_ROM:-$ENGINE/build/rom/pokeplatinum.us.nds}"
SETTLE="$ENGINE/pc/replays/lab-settle.txt"
SRC4="${OPENMMO_GEN4_ROM:-$(CDPATH= cd -- "$ROOT/.." && pwd)/roms/pokesoulsilver.nds}"

# Bulbasaur is the first of the band; Steelix is one of the 31 that are 64x64.
BULBASAUR=1
STEELIX=208

if [ ! -x "$FUSED" ]; then
    echo "follower-field: SKIP (no fused build: run \`make -C mmo fused\`)"
    exit 0
fi
if [ ! -f "$ROM" ] || [ ! -f "$SETTLE" ]; then
    echo "follower-field: SKIP (no ROM or settle replay under $ENGINE)"
    exit 0
fi
if ! command -v python3 >/dev/null 2>&1; then
    echo "follower-field: SKIP (no python3)"
    exit 0
fi

fail=0
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT INT TERM

ok()  { echo "  ok   $1"; }
bad() { echo "  FAIL $1"; fail=1; }

echo "a Pokemon walking behind the player:"

# The package. Reuse one if the caller has it; otherwise fill a throwaway from
# the player's own Gen 4 cartridge, which is what the porter is for.
PKG="${OPENMMO_FOLLOWER_PKG:-}"
if [ -z "$PKG" ]; then
    if [ ! -f "$SRC4" ]; then
        echo "follower-field: SKIP (no follower package and no Gen 4 cartridge"
        echo "                at $SRC4; set OPENMMO_FOLLOWER_PKG or OPENMMO_GEN4_ROM)"
        exit 0
    fi
    if ! python3 "$ROOT/tools/portfollow.py" --rom "$SRC4" \
            --pkg "$tmp/mods/followers" > "$tmp/fill.log" 2>&1; then
        bad "the follower package fills"
        tail -2 "$tmp/fill.log" | sed 's/^/       /'
        exit 1
    fi
    PKG="$tmp/mods/followers"
    ok "filled a follower package ($(sed -n '1s/portfollow: //p' "$tmp/fill.log"))"
fi
MODS_DIR=$(dirname "$PKG")
MODS_ID=$(basename "$PKG")

# Settle, then walk south and back, so the follower steps off the player's tile.
# The added frames start above the settle script's last, which pc-input refuses
# to let decrease.
cp "$SETTLE" "$tmp/walk.txt"
printf '1560 keys DOWN\n1800 keys none\n1860 keys UP\n2100 keys none\n' \
    >> "$tmp/walk.txt"

mint() {
    _w=$1
    printf 'name SHORT\n%s\n' "$2" > "$tmp/$_w.lab"
    env OPENMMO_ASSERT=warn OPENMMO_HUD=0 OPENMMO_LOCAL_SCRIPTS=0 \
        PC_MODS_DIR="$MODS_DIR" PC_MODS="$MODS_ID" \
        PC_ROM="$ROM" PC_SAVE="$tmp/$_w.sav" PC_LAB="$tmp/$_w.lab" \
        PC_LAB_AT=1800 PC_FRAMES=1800 PC_PACE=0 PC_INPUT="$SETTLE" \
        "$FUSED" > "$tmp/$_w.mint.log" 2>&1
}

play() {
    _w=$1
    env OPENMMO_ASSERT=warn OPENMMO_HUD=0 OPENMMO_LABELS=0 \
        OPENMMO_LOCAL_SCRIPTS=0 \
        PC_MODS_DIR="$MODS_DIR" PC_MODS="$MODS_ID" \
        PC_ROM="$ROM" PC_SAVE="$tmp/$_w.sav" \
        PC_FRAMES=2200 PC_PACE=0 PC_INPUT="$tmp/walk.txt" \
        PC_DUMP_POLYS=2199-2199 PC_DUMP_POLYS_FILE="$tmp/$_w.polys" \
        "$FUSED" > "$tmp/$_w.log" 2>&1
}

for w in none bulb steel; do
    case $w in
    none)  recipe="# nobody" ;;
    bulb)  recipe="party $BULBASAUR 5 0" ;;
    steel) recipe="party $STEELIX 40 0" ;;
    esac
    rc=0
    mint "$w" "$recipe" || rc=$?
    if [ "$rc" -ne 0 ]; then
        bad "the lab mints a save for '$w' (exit $rc)"
        tail -2 "$tmp/$w.mint.log" | sed 's/^/       /'
        exit 1
    fi
    rc=0
    play "$w" || rc=$?
    if [ "$rc" -ne 0 ]; then
        bad "the field runs with '$w' (exit $rc)"
        tail -2 "$tmp/$w.log" | sed 's/^/       /'
        exit 1
    fi
done

# --- 1. nobody to walk, said out loud --------------------------------------
if grep -q 'no follower (party holds 0' "$tmp/none.log"; then
    ok "an empty party is refused, and the refusal names the count"
else
    bad "an empty party is refused, and the refusal names the count"
    grep -h 'openmmo: no follower' "$tmp/none.log" | head -1 | sed 's/^/       /'
fi

# --- 2. and a Bulbasaur is seated -------------------------------------------
seated=$(sed -n 's/^openmmo: follower gfx \([0-9]*\) .*/\1/p' "$tmp/bulb.log" \
    | head -1)
want=$(sed -n 's/^#define MMO_FOLLOWER_GFX_BASE *\([0-9]*\).*/\1/p' \
    "$ROOT/src/follower_index.gen.h" | head -1)
if [ -n "$seated" ] && [ "$seated" = "$want" ]; then
    ok "a Bulbasaur is seated at the band's first graphics id ($seated)"
else
    bad "a Bulbasaur is seated at the band's first graphics id"
    echo "       seated '$seated', table says '$want'"
fi

# --- 3. and the rasterizer drew it ------------------------------------------
quad=$(python3 - "$tmp/none.polys" "$tmp/bulb.polys" <<'PY'
import re, sys

def rects(path):
    out = []
    for line in open(path):
        if not line.startswith('   '):
            continue
        vs = [t for t in line.split() if re.fullmatch(r'-?\d+,-?\d+', t)]
        if len(vs) != 4:
            continue
        pts = [tuple(map(int, v.split(','))) for v in vs]
        xs = sorted(set(p[0] for p in pts))
        ys = sorted(set(p[1] for p in pts))
        if len(xs) == 2 and len(ys) == 2:
            out.append((xs[0], ys[0], xs[1], ys[1]))
    return out

base = set(rects(sys.argv[1]))
new = [r for r in rects(sys.argv[2]) if r not in base]
print(max((r[2] - r[0] for r in new), default=0))
PY
)
if [ "$quad" = "32" ]; then
    ok "and the rasterizer drew a 32-wide body that the same walk without one did not"
else
    bad "and the rasterizer drew a 32-wide body (widest new quad: $quad)"
fi

# --- 4. the height rule -----------------------------------------------------
#
# A new game stands in the bedroom, which is indoors, and a Steelix is 64x64.
# The rule that refuses it is ours because Platinum's map
# headers carry no follow mode for HeartGold's to be ported into.
if grep -q 'no follower (too big for this map' "$tmp/steel.log"; then
    ok "a large follower is refused indoors, by name"
elif grep -q 'openmmo: follower gfx' "$tmp/steel.log"; then
    bad "a large follower is refused indoors: it was seated instead"
else
    bad "a large follower is refused indoors, by name"
    grep -h 'openmmo: no follower' "$tmp/steel.log" | head -1 | sed 's/^/       /'
fi

if [ "$fail" -eq 0 ]; then
    echo "follower-field: all checks passed"
else
    echo "follower-field: FAILED"
fi
exit "$fail"
