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
#   4. the height rule bites: a Steelix is one of the 31 large ones (its own
#      cartridge's tp_param says so) and the new-game bedroom is indoors, so
#      it is refused BY NAME rather than drawn through a wall.
#   5. it talks: an a press on the tile it is standing on runs a reaction out of
#      the carried condition table, and the line it prints is that reaction's
#      own, checked against the package rather than against a number typed
#      here, so the interpreter and the table cannot drift apart quietly.
#   6. and an A press somewhere else is refused by name, so "the press never
#      arrived" and "the press arrived and the Pokemon was elsewhere" are not
#      the same silence.
#   7. and the bubble is drawn: the reaction names one of HeartGold's fourteen
#      emotes and a 32-wide quad the same drive did not have a moment earlier
#      appears above the follower. Two boots of one script, which is sound
#      because the run is deterministic; the poly list is the only place a
#      headless run can see the 3D layer at all.
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

# --- 3b. it is there while the door is still opening ------------------------
printf '3 0\n33 0\n45 0\n' > "$tmp/sweep.maps"
rm -f "$tmp/sweep.sav"
env OPENMMO_ASSERT=warn OPENMMO_HUD=0 OPENMMO_LOCAL_SCRIPTS=0 \
    PC_MODS_DIR="$MODS_DIR" PC_MODS="$MODS_ID" \
    PC_ROM="$ROM" PC_SAVE="$tmp/sweep.sav" PC_LAB="$tmp/bulb.lab" \
    PC_LAB_AT=1800 PC_FRAMES=40000 PC_PACE=0 PC_INPUT="$SETTLE" \
    PC_SWEEP="$tmp/sweep.maps" PC_SWEEP_OUT="$tmp/sweep.out" \
    "$FUSED" > "$tmp/sweep.log" 2>&1 || true
maps=$(grep -c ' clean ' "$tmp/sweep.out" 2>/dev/null || echo 0)
seats=$(grep -c 'openmmo: follower gfx .* seated at' "$tmp/sweep.log" || true)
asserts=$(grep -c 'assertion failed' "$tmp/sweep.log" || true)
if [ "$maps" -eq 3 ] && [ "$seats" -eq 3 ]; then
    ok "a follower is seated while each of three arrivals is still running"
else
    bad "a follower is seated while each of three arrivals is still running"
    echo "       $maps map(s), $seats seated" | sed 's/^/ /'
    grep -h 'openmmo: follower gfx' "$tmp/sweep.log" | sed 's/^/       /'
fi
if [ "$asserts" -eq 0 ]; then
    ok "and adding one mid-transition asserts nothing"
else
    bad "and adding one mid-transition asserts nothing ($asserts assertion(s))"
fi
# And it is seated ON the player's tile and HIDDEN, which is the cartridge's
# own placement (FollowMon_CreateMapObject at the player's coordinates, then
# sub_02069DC8(obj, true)): the player's first step out of the door is what
# shows it, on the tile they leave. Nothing of ours puts it anywhere else.
ontop=$(grep -c 'follower gfx .* seated at .*, hidden, on the player' "$tmp/sweep.log" || true)
if [ "$ontop" -eq "$seats" ] && [ "$seats" -gt 0 ]; then
    ok "and every one of them is seated hidden on the player's own tile"
else
    bad "and every one of them is seated hidden on the player's own tile"
    echo "       $ontop of $seats on the player" | sed 's/^/ /'
    grep -h 'openmmo: follower gfx' "$tmp/sweep.log" | sed 's/^/       /'
fi

# --- 4. the height rule -----------------------------------------------------
if grep -q 'no follower (this map takes none' "$tmp/steel.log"; then
    ok "a large follower is refused indoors, by name"
elif grep -q 'openmmo: follower gfx' "$tmp/steel.log"; then
    bad "a large follower is refused indoors: it was seated instead"
else
    bad "a large follower is refused indoors, by name"
    grep -h 'openmmo: no follower' "$tmp/steel.log" | head -1 | sed 's/^/       /'
fi

# --- 5 and 6. talking to it ------------------------------------------------
cp "$SETTLE" "$tmp/talk.txt"
printf '1560 keys DOWN\n1740 keys none\n1770 keys A\n1782 keys none\n1800 keys UP\n1804 keys none\n1900 keys A\n1912 keys none\n' \
    >> "$tmp/talk.txt"
env OPENMMO_ASSERT=warn OPENMMO_HUD=0 OPENMMO_LABELS=0 OPENMMO_LOCAL_SCRIPTS=0 \
    OPENMMO_INTERACT_REPORT=1 \
    PC_MODS_DIR="$MODS_DIR" PC_MODS="$MODS_ID" \
    PC_ROM="$ROM" PC_SAVE="$tmp/bulb.sav" \
    PC_FRAMES=2100 PC_PACE=0 PC_INPUT="$tmp/talk.txt" \
    "$FUSED" > "$tmp/talk.log" 2>&1 || true

said=$(sed -n 's/^openmmo: follower says reaction \([0-9]*\) .*/\1/p' "$tmp/talk.log" | head -1)
line=$(sed -n "s/^openmmo: the follower's line \([0-9]*\)$/\1/p" "$tmp/talk.log" | head -1)
if [ -n "$said" ] && [ -n "$line" ]; then
    # The package's own header says where the reactions start; the reaction says
    # which line its first step prints. Read rather than assumed, so this fails
    # when the interpreter and the table disagree instead of when a constant
    # typed here goes stale.
    want=$(python3 -c '
import struct, sys
from pathlib import Path
narc = Path(sys.argv[1]) / ".cooked/narc/openmmo/follow_talk.narc"
head = (narc / "0").read_bytes()
react_base = struct.unpack_from("<H", head, 14)[0]
row = (narc / str(react_base + int(sys.argv[2]) - 1)).read_bytes()
print(struct.unpack_from("<H", row, 2)[0] - 1)
' "$PKG" "$said")
    if [ "$line" = "$want" ]; then
        ok "an A press at it runs reaction $said and prints that reaction's own line ($line)"
    else
        bad "an A press at it prints the reaction's own line"
        echo "       reaction $said printed line $line, the table says $want"
    fi
else
    bad "an A press at it runs a reaction out of the carried table"
    grep -h "openmmo: .*follower" "$tmp/talk.log" | tail -3 | sed 's/^/       /'
fi

if grep -q "not the tile faced" "$tmp/talk.log"; then
    ok "and an A press anywhere else is refused by name"
else
    bad "and an A press anywhere else is refused by name"
fi

# --- 7. the bubble ----------------------------------------------------------
emote_at() {
    env OPENMMO_ASSERT=warn OPENMMO_HUD=0 OPENMMO_LABELS=0 \
        OPENMMO_LOCAL_SCRIPTS=0 \
        PC_MODS_DIR="$MODS_DIR" PC_MODS="$MODS_ID" \
        PC_ROM="$ROM" PC_SAVE="$tmp/bulb.sav" \
        PC_FRAMES=1950 PC_PACE=0 PC_INPUT="$tmp/talk.txt" \
        PC_DUMP_POLYS="$1-$1" PC_DUMP_POLYS_FILE="$2" \
        "$FUSED" > /dev/null 2>&1
}
emote_at 1880 "$tmp/before.polys" || true
emote_at 1930 "$tmp/during.polys" || true

bubble=$(python3 - "$tmp/before.polys" "$tmp/during.polys" <<'PYEOF'
import re, sys

def rects(path):
    out = []
    try:
        lines = open(path)
    except OSError:
        return out
    for line in lines:
        if not line.startswith('   '):
            continue
        vs = [t for t in line.split() if re.fullmatch(r'-?\d+,-?\d+', t)]
        if len(vs) != 4:
            continue
        pts = [tuple(map(int, v.split(','))) for v in vs]
        xs = sorted(set(p[0] for p in pts))
        ys = sorted(set(p[1] for p in pts))
        if len(xs) == 2 and len(ys) == 2:
            out.append((xs[0], ys[0], xs[1] - xs[0], ys[1] - ys[0]))
    return out

base = set(rects(sys.argv[1]))
new = [r for r in rects(sys.argv[2]) if r not in base]
print(max((r[2] for r in new), default=0))
PYEOF
)
if [ "$bubble" = "32" ]; then
    ok "and the emote bubble is drawn: a 32-wide quad the frame before had not"
else
    bad "and the emote bubble is drawn (widest new quad: $bubble)"
    grep -h "openmmo: the follower's emote\|no emote" "$tmp/talk.log" | head -2 \
        | sed 's/^/       /'
fi

if [ "$fail" -eq 0 ]; then
    echo "follower-field: all checks passed"
else
    echo "follower-field: FAILED"
fi
exit "$fail"
