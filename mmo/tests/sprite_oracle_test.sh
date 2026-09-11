#!/bin/sh
# The client's sprite table against the engine's own
# answers, and the engine's behaviour past the end of its data.
#
#   * the battle-sprite path does not bound the species at all. Species 496
#     computes a pl_pokegra member past the archive's last, and 1000 computes one
#     twice as far out; the NARC read that would follow is guarded by nothing but
#     a GF_ASSERT this port used to discard.
#   * the icon path bounds it and is silent. Every species past the national dex
#     resolves to the same placeholder icon, so a Gen 5 party member would draw
#     as a "?" with nothing logged anywhere.
set -eu

ROOT=${1:?usage: sprite_oracle_test.sh <mmo-root> <build-dir> <engine-dir>}
BUILD=${2:?usage: sprite_oracle_test.sh <mmo-root> <build-dir> <engine-dir>}
ENGINE=${3:?usage: sprite_oracle_test.sh <mmo-root> <build-dir> <engine-dir>}

FUSED="$BUILD/fused/pokeplatinum"
CLIENT="$BUILD/openmmo-client"
ROM="${PC_ROM:-$ENGINE/build/rom/pokeplatinum.us.nds}"

if [ ! -x "$FUSED" ] || [ ! -x "$CLIENT" ]; then
    echo "sprite-oracle: SKIP (no fused build: run \`make -C mmo fused\`)"
    exit 0
fi
if [ ! -f "$ROM" ]; then
    echo "sprite-oracle: SKIP (no ROM at $ROM)"
    exit 0
fi

fail=0
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

ok()  { echo "  ok   $1"; }
bad() { echo "  FAIL $1"; fail=1; }

echo "the client's sprite table is the engine's own, asked directly:"

# Its own save: a second boot onto an existing one never runs the port's atexit.
OPENMMO_SPRITE_DUMP=1 PC_ROM="$ROM" PC_SAVE="$tmp/sprite.sav" \
    PC_FRAMES=4 PC_PACE=0 "$FUSED" > "$tmp/engine.log" 2>&1 || {
    echo "sprite-oracle: the fused build did not run to the dump"
    tail -5 "$tmp/engine.log"
    exit 1
}

grep '^sprite ' "$tmp/engine.log" > "$tmp/engine.txt" || true
"$CLIENT" sprite --table > "$tmp/client.txt"

rows=$(wc -l < "$tmp/engine.txt")
if [ "$rows" -lt 6000 ]; then
    bad "the fused build printed $rows sprite rows, which is not a whole table"
elif diff -u "$tmp/engine.txt" "$tmp/client.txt" > "$tmp/diff.txt" 2>&1; then
    ok "all $rows rows agree with BuildPokemonSpriteTemplate, archive and member"
else
    bad "the client's arithmetic and the engine's have diverged"
    head -20 "$tmp/diff.txt"
fi

# Every row the engine produced is one the client accepts, and the reverse: a
# form the client refuses that the engine draws would be a picture we lost.
if [ "$(wc -l < "$tmp/client.txt")" = "$rows" ]; then
    ok "the client draws neither more nor fewer combinations than the engine"
else
    bad "the two tables are different sizes"
fi

# NOTE the run above loads no package.
echo "and the engine's answers past its own data, now that both are bounded:"

# The two checks below are awk over the probe rows, and awk over no rows agrees
# with both of them: `n == 0` is true when nothing was counted and `wrong` is
# unset when nothing was compared. A dump that stopped emitting probes, a
# renamed variable, a boot that never reached the atexit, would read as a
# bounded sprite path rather than as no measurement at all.
probes=$(grep -c '^probe ' "$tmp/engine.log" || true)
if [ "${probes:-0}" -lt 1 ]; then
    bad "the boot probed a species past the engine's data at all (no probe rows)"
fi

# This used to be the other way round: BuildPokemonSpriteTemplate had no
# ceiling at all, species 496 asked for member 2979 of an archive with 2964,
# and the read after it was guarded by one assertion this port had discarded.
if awk '$1 == "probe" && $4 == "pokegra" && $5 >= 2964 { n++ } END { exit !(n == 0) }' \
        "$tmp/engine.log"; then
    ok "no probed species indexes past pl_pokegra's end any more ($probes probed)"
else
    bad "the battle-sprite path runs off the end again, re-read sprite.h"
    grep '^probe ' "$tmp/engine.log" || true
fi

# The icon path's answer for SPECIES_NONE, which is what it substitutes.
none_icon=$(awk '$1 == "icon" && $2 == 0 && $3 == 0 { print $4, $5; exit }' "$tmp/engine.log")
if [ -n "$none_icon" ] && awk -v want="$none_icon" \
        '$1 == "probe" { got = $8 " " $9; if (got != want) wrong = 1 } END { exit wrong }' \
        "$tmp/engine.log"; then
    ok "all $probes probed species draw the placeholder icon, and say nothing"
else
    bad "the icon path no longer clamps to SPECIES_NONE, re-read sprite.h"
    grep '^probe ' "$tmp/engine.log" || true
fi

if [ "$fail" -eq 0 ]; then
    echo "sprite-oracle: all checks passed"
else
    echo "sprite-oracle: FAILED"
fi
exit "$fail"
