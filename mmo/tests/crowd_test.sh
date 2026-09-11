#!/bin/sh
# How many other players the overworld holds, and whether it
# says so when it is asked for more.
#
#   1. twelve remote players stand on a map with no pool running out, the
#      number 6.7.2 measured, so a change that lowers a capacity fails here;
#   2. a crowd past the client's slot ceiling is refused by the client and named,
#      rather than handed to the engine;
#   3. the traps are loud: take the cull off, give the map an object table too
#      small for the crowd, and the engine's own assertion, the ones this build
#      stopped throwing away, is reported, and stops the run under
#      OPENMMO_ASSERT=fatal;
#   4. a starved texture pool does not pass unremarked: the map load that runs
#      short says so, where before this it said nothing at all;
#   5. the crowd's own Pokemon: twelve peers each carrying a different species
#      seat eight followers and no more, the pool is keyed by graphics id and
#      twelve distinct ones is more of it than a busy map has, and the four
#      that go without still walk;
#   6. and when the object table is what runs short, the followers give way
#      First. A crowd where the last few walk alone is a working game; one where
#      the next object is a NULL write is not.
set -eu

ROOT=${1:?usage: crowd_test.sh <mmo-root> <build-dir> <engine-dir>}
BUILD=${2:?usage: crowd_test.sh <mmo-root> <build-dir> <engine-dir>}
ENGINE=${3:?usage: crowd_test.sh <mmo-root> <build-dir> <engine-dir>}

FUSED="$BUILD/fused/pokeplatinum"
ROM="${PC_ROM:-$ENGINE/build/rom/pokeplatinum.us.nds}"
SETTLE="$ENGINE/pc/replays/lab-settle.txt"
SRC4="${OPENMMO_GEN4_ROM:-$(CDPATH= cd -- "$ROOT/.." && pwd)/roms/pokesoulsilver.nds}"

if [ ! -x "$FUSED" ]; then
    echo "crowd: SKIP (no fused build: run \`make -C mmo fused\`)"
    exit 0
fi
if [ ! -f "$ROM" ] || [ ! -f "$SETTLE" ]; then
    echo "crowd: SKIP (no ROM or input script under $ENGINE)"
    exit 0
fi

fail=0
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

ok()  { echo "  ok   $1"; }
bad() { echo "  FAIL $1"; fail=1; }

printf 'name CROWD\n' > "$tmp/min.lab"

# boot CROWD LOGFILE [VAR=VALUE ...], one lab run with a crowd on the field.
# Echoes the exit status; the caller reads the log.
boot() {
    _crowd=$1; _log=$2; shift 2
    env "$@" \
        OPENMMO_FAKE_CROWD="$_crowd" \
        PC_ROM="$ROM" PC_SAVE="$tmp/min.sav" PC_LAB="$tmp/min.lab" \
        PC_LAB_AT=1800 PC_FRAMES=2600 PC_PACE=0 PC_INPUT="$SETTLE" \
        "$FUSED" > "$_log" 2>&1 && echo 0 || echo $?
}

echo "the overworld holds a crowd, and says so when it cannot:"

# 1 and 2 in one boot: ask for more than the client's ceiling and check both the
# refusal and the number that is actually drawn.
rc=$(boot 64 "$tmp/cap.log")
if [ "$rc" -ne 0 ]; then
    bad "a crowd of twelve boots clean (exit $rc)"
elif grep -q 'crowd of 12 requested at ([0-9-]*,[0-9-]*), 12 live' "$tmp/cap.log"; then
    ok "twelve remote players stand on the map, all twelve drawn"
else
    bad "twelve remote players stand on the map, all twelve drawn"
    grep -i crowd "$tmp/cap.log" | sed 's/^/       /'
fi

if grep -q 'is this client.s slot ceiling' "$tmp/cap.log"; then
    ok "a crowd past the client's ceiling is refused here, not by the engine"
else
    bad "a crowd past the client's ceiling is refused here, not by the engine"
fi

if grep -qE 'engine assertion failed|heap is full|texture pool is full' "$tmp/cap.log"; then
    bad "no engine pool runs out with twelve of them on the map"
    grep -E 'assertion failed|heap is full|texture pool' "$tmp/cap.log" | sed 's/^/       /'
else
    ok "no engine pool runs out with twelve of them on the map"
fi

# A remote PlayerAvatar can wear an object-event body (hiker, gfx 20) and
# still stand. Twelve distinct ids are the visibility cap; one swapped
# sprite is enough to pin the seating path.
rc=$(boot 2 "$tmp/body.log" OPENMMO_FAKE_GFX=20)
if [ "$rc" -eq 0 ] && grep -q 'appearance slot 0 gfx .* -> 20 (live=1)' "$tmp/body.log" \
    && grep -q 'crowd of 2 requested at ([0-9-]*,[0-9-]*), 2 live' "$tmp/body.log" \
    && ! grep -qE 'engine assertion failed|texture pool is full' "$tmp/body.log"; then
    ok "a remote wearing a hiker sprite stays live"
else
    bad "a remote wearing a hiker sprite stays live (exit $rc)"
    grep -E 'appearance|crowd of|assertion|texture pool' "$tmp/body.log" | sed 's/^/       /'
fi

# 3: the crowd with the cull taken off, on an object table too small to hold it.
# The engine's answer is a NULL from MapObjectMan_AddMapObject that
# PlayerAvatar_AddMapObject asserts on and then writes through; before this build
# started listening to GF_ASSERT, that was a silent corruption.
rc=$(boot 8 "$tmp/obj.log" OPENMMO_MAP_OBJECTS=16 OPENMMO_CROWD_NO_CULL=1 OPENMMO_ASSERT=fatal)
if [ "$rc" -ne 0 ] && grep -q 'engine assertion failed' "$tmp/obj.log"; then
    ok "a crowd past the object table stops the run on the engine's own assertion"
else
    bad "a crowd past the object table stops the run on the engine's own assertion (exit $rc)"
    tail -3 "$tmp/obj.log" | sed 's/^/       /'
fi

# 4: starve the texture pool. Which of the traps catches it depends on which of
# the pool's users runs out first and that is the map's business, so this asserts
# only what is ours: the run says something. Before the traps this map load ran
# short in complete silence.
rc=$(boot 0 "$tmp/tex.log" OPENMMO_TEXTURE_SLOTS=4)
if grep -qE '^openmmo: (engine assertion failed|a heap is full|the overworld texture pool is full)' "$tmp/tex.log"; then
    ok "a starved texture pool is reported rather than passing unremarked"
else
    bad "a starved texture pool is reported rather than passing unremarked (exit $rc)"
    tail -3 "$tmp/tex.log" | sed 's/^/       /'
fi

# --- 5 and 6. the crowd's own Pokemon ---------------------------------------
#
# OPENMMO_FAKE_FOLLOWERS gives each synthetic peer a distinct follower graphics
# id, which is the case that costs: a crowd wears one of two trainer models and
# costs the texture pool two slots, and twelve different Pokemon cost it twelve.
PKG="${OPENMMO_FOLLOWER_PKG:-}"
if [ -z "$PKG" ] && [ -f "$SRC4" ] && command -v python3 >/dev/null 2>&1; then
    if python3 "$ROOT/tools/portfollow.py" --rom "$SRC4" \
            --pkg "$tmp/mods/followers" > "$tmp/fill.log" 2>&1; then
        PKG="$tmp/mods/followers"
    fi
fi
if [ -z "$PKG" ]; then
    echo "  SKIP the crowd's own Pokemon (no follower package; set"
    echo "       OPENMMO_FOLLOWER_PKG or OPENMMO_GEN4_ROM)"
else
    MODS_DIR=$(dirname "$PKG")
    MODS_ID=$(basename "$PKG")

    # A SAVE OF ITS OWN, and the reason is check 3 above: that boot is MEANT to
    # abort, and it aborts in the middle of writing the save every boot here
    # shares. The next run over that image dies before the field is up, which
    # reads as a follower fault and is not one.
    fboot() {
        _crowd=$1; _log=$2; shift 2
        env "$@" \
            OPENMMO_FAKE_CROWD="$_crowd" OPENMMO_FAKE_FOLLOWERS=1 \
            PC_MODS_DIR="$MODS_DIR" PC_MODS="$MODS_ID" \
            PC_ROM="$ROM" PC_SAVE="$tmp/follow.sav" PC_LAB="$tmp/min.lab" \
            PC_LAB_AT=1800 PC_FRAMES=2600 PC_PACE=0 PC_INPUT="$SETTLE" \
            "$FUSED" > "$_log" 2>&1 && echo 0 || echo $?
    }

    rc=$(fboot 12 "$tmp/follow.log")
    seated=$(grep -c 'walks with gfx' "$tmp/follow.log" || true)
    if [ "$rc" -eq 0 ] && [ "$seated" -eq 8 ] \
        && grep -q '8 peer followers are drawn already' "$tmp/follow.log" \
        && grep -q 'crowd of 12 requested at ([0-9-]*,[0-9-]*), 12 live' "$tmp/follow.log"; then
        ok "eight of twelve peers walk with a Pokemon, and all twelve still draw"
    else
        bad "eight of twelve peers walk with a Pokemon, and all twelve still draw"
        echo "       exit $rc, $seated seated"
        grep -E 'walks with gfx|drawn already|crowd of' "$tmp/follow.log" \
            | tail -3 | sed 's/^/       /'
    fi

    # The object table, not the pool. 32 slots is short enough that the map's
    # own objects and a crowd of twelve cannot both fit, so the followers are
    # the first thing given up, and players are still drawn afterwards.
    rc=$(fboot 12 "$tmp/followobj.log" OPENMMO_MAP_OBJECTS=32)
    seated=$(grep -c 'walks with gfx' "$tmp/followobj.log" || true)
    live=$(sed -n 's/^openmmo: crowd of 12 requested at ([0-9-]*,[0-9-]*), \([0-9]*\) live/\1/p' \
        "$tmp/followobj.log" | head -1)
    if [ "$rc" -eq 0 ] && [ "$seated" -eq 0 ] \
        && grep -q 'peer followers give way to players' "$tmp/followobj.log" \
        && [ -n "$live" ] && [ "$live" -gt 0 ]; then
        ok "a short object table gives the followers up first, and $live players still draw"
    else
        bad "a short object table gives the followers up first"
        echo "       exit $rc, $seated seated, '$live' players live"
        grep -E 'give way|walks with gfx|crowd of' "$tmp/followobj.log" \
            | tail -3 | sed 's/^/       /'
    fi
fi

[ "$fail" -eq 0 ] || { echo "crowd: FAILED"; exit 1; }
echo "crowd: all checks passed"
