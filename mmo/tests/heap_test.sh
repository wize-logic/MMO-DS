#!/bin/sh
# The field's memory, across map changes, with a crowd on it.
#
#   1. it is the same after every map change as after the first, the leak;
#   2. every deletion of a remote-band map object is matched by a freed avatar,
#      which is the mechanism rather than the symptom, and fails louder if the
#      hook stops being reached at all;
#   3. the field heaps are the raised ones and the arena still has room, the
#      APPLICATION raise (mods/openmmo/src/openmmo_heap.c) is taken from the
#      main arena's unused tail precisely so nothing already allocated loses
#      room, and a clamp that silently ate it would show up here.
set -eu

ROOT=${1:?usage: heap_test.sh <mmo-root> <build-dir> <engine-dir>}
BUILD=${2:?usage: heap_test.sh <mmo-root> <build-dir> <engine-dir>}
ENGINE=${3:?usage: heap_test.sh <mmo-root> <build-dir> <engine-dir>}

FUSED="$BUILD/fused/pokeplatinum"
ROM="${PC_ROM:-$ENGINE/build/rom/pokeplatinum.us.nds}"
SETTLE="$ENGINE/pc/replays/lab-settle.txt"

if [ ! -x "$FUSED" ]; then
    echo "heap: SKIP (no fused build: run \`make -C mmo fused\`)"
    exit 0
fi
if [ ! -f "$ROM" ] || [ ! -f "$SETTLE" ]; then
    echo "heap: SKIP (no ROM or input script under $ENGINE)"
    exit 0
fi

fail=0
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

ok()  { echo "  ok   $1"; }
bad() { echo "  FAIL $1"; fail=1; }

printf 'name HEAP\n' > "$tmp/min.lab"

# Jubilife, Canalave and Oreburgh: three outdoor maps of quite different sizes,
# and Canalave is the tightest for FIELD1 of every map measured, so a raise that
# did not land shows here first. Warp 0 on each; the port resolves it through the
# destination's own warp table.
printf '3 0\n33 0\n45 0\n' > "$tmp/maps.txt"

echo "the field's memory holds across map changes:"

rc=0
env OPENMMO_HEAP_REPORT=1 OPENMMO_FAKE_CROWD=12 OPENMMO_ASSERT=warn \
    PC_ROM="$ROM" PC_SAVE="$tmp/min.sav" PC_LAB="$tmp/min.lab" \
    PC_LAB_AT=1800 PC_FRAMES=30000 PC_PACE=0 PC_INPUT="$SETTLE" \
    PC_SWEEP="$tmp/maps.txt" PC_SWEEP_OUT="$tmp/sweep.txt" \
    "$FUSED" > "$tmp/run.log" 2>&1 || rc=$?

if [ "$rc" -ne 0 ]; then
    bad "a crowd of twelve walks three maps (exit $rc)"
    sed -n '$p' "$tmp/run.log"
    exit 1
fi

# Every map the sweep was given loaded. A map that never settles is reported by
# the port and ends the sweep, so a short file is a failure and not a shrug.
maps=$(grep -c ' clean ' "$tmp/sweep.txt" 2>/dev/null || echo 0)
if [ "$maps" -eq 3 ]; then
    ok "three maps load with twelve remote players on them"
else
    bad "three maps load with twelve remote players on them ($maps did)"
fi

# The leak. Every "map settled" line carries FIELD2's free space; with the crowd
# freed properly there is exactly one distinct value among them.
grep 'heaps at map settled' "$tmp/run.log" \
    | sed 's/.*FIELD2 free \([0-9]*\).*/\1/' | sort -u > "$tmp/field2.txt"
distinct=$(wc -l < "$tmp/field2.txt")
if [ "$distinct" -eq 1 ]; then
    ok "FIELD2 free space is the same after every map change ($(cat "$tmp/field2.txt") bytes)"
else
    bad "FIELD2 free space is the same after every map change ($distinct values: $(tr '\n' ' ' < "$tmp/field2.txt"))"
fi

# The mechanism. The counters are cumulative, so the last line is the whole run;
# they must be equal and non-zero, equal-at-zero would mean the delete hook is
# never reached, which is what the leak looked like before it was fixed.
last=$(grep 'openmmo: avatars,' "$tmp/run.log" | tail -1)
deletes=$(echo "$last" | sed 's/.*avatars, \([0-9]*\) remote deletes.*/\1/')
freed=$(echo "$last" | sed 's/.*deletes, \([0-9]*\) freed.*/\1/')
if [ -n "$deletes" ] && [ "$deletes" -gt 0 ] && [ "$deletes" = "$freed" ]; then
    ok "every remote map object deleted took its avatar with it ($deletes)"
else
    bad "every remote map object deleted took its avatar with it ($deletes deleted, $freed freed)"
fi

# The raise, and that it came from the arena rather than from anything already
# allocated: the init line prints what was asked for and what the arena had left
# afterwards. A clamp would have made the two disagree.
init=$(grep 'openmmo: heaps at init' "$tmp/run.log" | tail -1)
if echo "$init" | grep -q 'APPLICATION +0x1a000' \
   && ! grep -q 'the arena has' "$tmp/run.log"; then
    ok "APPLICATION is raised by the full amount, out of the arena's spare"
else
    bad "APPLICATION is raised by the full amount, out of the arena's spare ($init)"
fi

if [ "$fail" -eq 0 ]; then
    echo "heap: all checks passed"
else
    echo "heap: FAILED"
fi
exit "$fail"
