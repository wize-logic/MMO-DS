#!/bin/sh
# The fused build can enter a battle from a cold boot
# through the port's lab, and that is how this tree starts a fight.
#
#   1. The six kinds are still the LAB'S. The names are read back out of
#      the engine's own table, so a kind dropped next door fails here
#      before any boot.
#   2. An unknown kind fails loudly. A typo that silently walked the
#      overworld would pin a digest of no fight.
#   3. A short spec fails loudly the same way.
#   4. Each kind starts. One minted save, six boots in parallel, each
#      only as far as "pc_lab: starting battle kind N (...)". Fighting
#      one to the end is still battle_seam_test.sh.
#   5. --use-item FIRES. A rare candy on the level-100 lead reports
#      the use and does not evolve (Garchomp has nowhere to go).
set -eu

ROOT=${1:?usage: battle_lab_test.sh <mmo-root> <build-dir> <engine-dir>}
BUILD=${2:?usage: battle_lab_test.sh <mmo-root> <build-dir> <engine-dir>}
ENGINE=${3:?usage: battle_lab_test.sh <mmo-root> <build-dir> <engine-dir>}

LAB="$ROOT/tools/battle_lab.sh"
FUSED="$BUILD/fused/pokeplatinum"
ROM="${PC_ROM:-$ENGINE/build/rom/pokeplatinum.us.nds}"
SETTLE="$ENGINE/pc/replays/lab-settle.txt"
REPLAY="$ENGINE/pc/replays/lab-battle.txt"
PCLAB="$ENGINE/pc/src/pc_lab.c"
PCARGS="$ENGINE/pc/src/pc_args.c"

fail=0
ok()  { echo "  ok   $1"; }
bad() { echo "  FAIL $1"; fail=1; }

echo "the fused build starts a fight through the port's battle lab:"

# ---------------------------------------------------------------- names
if [ ! -f "$PCLAB" ] || [ ! -f "$PCARGS" ]; then
    echo "  SKIP the lab's own names (no checkout at $ENGINE)"
else
    missing=
    for k in wild legendary trainer safari first tutorial; do
        grep -q "\"$k\"" "$PCLAB" || missing="$missing $k"
    done
    if [ -z "$missing" ]; then
        ok "the engine still names the six kinds"
    else
        bad "the engine still names the six kinds (missing:$missing)"
    fi
    if grep -q 'PC_LAB_BATTLE' "$PCARGS" && grep -q 'PC_LAB_USE_ITEM' "$PCARGS"; then
        ok "the host still exposes --battle and --use-item"
    else
        bad "the host still exposes --battle and --use-item"
    fi
fi

if [ ! -x "$FUSED" ]; then
    echo "battle lab: SKIP (no fused build: run \`make -C mmo fused\`)"
    [ "$fail" -eq 0 ]
    exit 0
fi
if [ ! -f "$ROM" ] || [ ! -f "$SETTLE" ] || [ ! -f "$REPLAY" ]; then
    echo "battle lab: SKIP (no ROM or input script under $ENGINE)"
    [ "$fail" -eq 0 ]
    exit 0
fi
if [ ! -x "$LAB" ]; then
    bad "the driver is at tools/battle_lab.sh"
    echo "battle lab: FAILED"
    exit 1
fi

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# One save for every boot. The fight writes it, so each kind gets a copy.
if ! "$LAB" mint "$ROOT" "$BUILD" "$ENGINE" "$tmp/base.sav" \
        > "$tmp/mint.log" 2>&1; then
    bad "the save lab could not mint a party to fight with"
    tail -3 "$tmp/mint.log" | sed 's/^/       /'
    echo "battle lab: FAILED"
    exit 1
fi
ok "one save mints from a numeric recipe"

# ---------------------------------------------------------------- loud failures
# Parse happens on the first frame, so fifty is plenty.
set +e
"$LAB" fight "$ROOT" "$BUILD" "$ENGINE" "$tmp/base.sav" "notakind 1 2" 2600 50 \
    > "$tmp/unknown.log" 2>&1
unknown_rc=$?
set -e
if [ "$unknown_rc" -eq 2 ] && grep -q "unknown kind 'notakind 1 2'" "$tmp/unknown.log"; then
    ok "an unknown kind exits 2 rather than walking the overworld"
else
    bad "an unknown kind exits 2 rather than walking the overworld (exit $unknown_rc)"
    tail -3 "$tmp/unknown.log" | sed 's/^/       /'
fi

set +e
"$LAB" fight "$ROOT" "$BUILD" "$ENGINE" "$tmp/base.sav" "wild 25" 2600 50 \
    > "$tmp/short.log" 2>&1
short_rc=$?
set -e
if [ "$short_rc" -eq 2 ] && grep -q "wants 2 argument" "$tmp/short.log"; then
    ok "a short spec exits 2 rather than starting a different fight"
else
    bad "a short spec exits 2 rather than starting a different fight (exit $short_rc)"
    tail -3 "$tmp/short.log" | sed 's/^/       /'
fi

# ---------------------------------------------------------------- the six kinds
# Measured: each starts at frame 2600; 3000 frames is enough to see the
# start line and not enough to finish. Parallel because they do not share
# a save. Judged after wait: a background function's fail= would stay
# in the child.
start_fight() {
    _tag=$1
    _spec=$2
    cp "$tmp/base.sav" "$tmp/$_tag.sav"
    set +e
    "$LAB" fight "$ROOT" "$BUILD" "$ENGINE" "$tmp/$_tag.sav" "$_spec" 2600 3000 \
        > "$tmp/$_tag.log" 2>&1
    echo $? > "$tmp/$_tag.rc"
    set -e
}

start_fight wild      "wild 25 5"        &
start_fight legendary "legendary 480 20" &
start_fight trainer   "trainer 1 0 0"    &
start_fight safari    "safari 111 22 30" &
start_fight first     "first 1"          &
start_fight tutorial  "tutorial"         &
wait

check_fight() {
    _tag=$1
    _spec=$2
    _want=$3
    if [ "$(cat "$tmp/$_tag.rc")" -eq 0 ] \
        && grep -q "pc_lab: starting battle $_want" "$tmp/$_tag.log"; then
        ok "$_tag starts ($_spec)"
    else
        bad "$_tag starts ($_spec)"
        tail -3 "$tmp/$_tag.log" | sed 's/^/       /'
    fi
}

check_fight wild      "wild 25 5"         "kind 1 (25,5,0)"
check_fight legendary "legendary 480 20"  "kind 2 (480,20,0)"
check_fight trainer   "trainer 1 0 0"     "kind 3 (1,0,0)"
check_fight safari    "safari 111 22 30"  "kind 4 (111,22,30)"
check_fight first     "first 1"           "kind 5 (1,0,0)"
check_fight tutorial  "tutorial"          "kind 6 (0,0,0)"

# ---------------------------------------------------------------- use-item
cp "$tmp/base.sav" "$tmp/use.sav"
set +e
"$LAB" use "$ROOT" "$BUILD" "$ENGINE" "$tmp/use.sav" "0 50" 2600 3000 \
    > "$tmp/use.log" 2>&1
use_rc=$?
set -e
if [ "$use_rc" -eq 0 ] \
    && grep -q "pc_lab: item 50 on slot 0: level 100 -> 100, species 445" "$tmp/use.log"; then
    ok "a rare candy on the lead is used and does not evolve it"
else
    bad "a rare candy on the lead is used and does not evolve it (exit $use_rc)"
    tail -3 "$tmp/use.log" | sed 's/^/       /'
fi

if [ "$fail" -ne 0 ]; then
    echo "battle lab: FAILED"
    exit 1
fi
echo "battle lab: all checks passed"
