#!/bin/sh
# Every ceiling this build raised above the engine's own,
# and what the build does when one is taken away again.
#
#   1. each field heap names the size the engine asked for beside the size it was
#      created at, printed from inside the engine's own Heap_Create site, so a
#      lowered default changes the number and a patch that stops applying loses
#      the line entirely, which is the silent failure 6.7.8 is about;
#   2. a heap this build does not raise is created at the engine's size, so the
#      hook is not quietly adding room everywhere;
#   3. the object table is read back through MapObjectMan_GetMaxObjects, the
#      engine's own accessor over the table it built, 64 here would mean our
#      number never reached it;
#   4. a starved field heap stops the process on the heap trap rather than
#      corrupting: FIELD1 and FIELD2 each shrunk past what a map load needs, and
#      each must say "a heap is full" and abort. This is the one trap in
#      openmmo_crowd.c that nothing else in the suite has ever fired, the
#      texture check in crowd_test.sh accepts whichever of the three catches it.
set -eu

ROOT=${1:?usage: ceiling_test.sh <mmo-root> <build-dir> <engine-dir>}
BUILD=${2:?usage: ceiling_test.sh <mmo-root> <build-dir> <engine-dir>}
ENGINE=${3:?usage: ceiling_test.sh <mmo-root> <build-dir> <engine-dir>}

FUSED="$BUILD/fused/pokeplatinum"
ROM="${PC_ROM:-$ENGINE/build/rom/pokeplatinum.us.nds}"
SETTLE="$ENGINE/pc/replays/lab-settle.txt"

# The inventory. Each is the default in mods/openmmo/src/, and lowering one there
# without lowering it here is what this check is for.
FIELD1_EXTRA=$((0x14000))
FIELD2_EXTRA=$((0x4000))
MAP_OBJECTS=80
OVERWORLD_ANIMS=88

if [ ! -x "$FUSED" ]; then
    echo "ceiling: SKIP (no fused build: run \`make -C mmo fused\`)"
    exit 0
fi
if [ ! -f "$ROM" ] || [ ! -f "$SETTLE" ]; then
    echo "ceiling: SKIP (no ROM or input script under $ENGINE)"
    exit 0
fi

fail=0
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

ok()  { echo "  ok   $1"; }
bad() { echo "  FAIL $1"; fail=1; }

printf 'name CEILING\n' > "$tmp/min.lab"

# boot TAG [VAR=VALUE ...], one lab run into the field with a crowd on it.
# Echoes the exit status; the caller reads "$tmp/TAG.log". Each boot gets its own
# save: a second boot onto an existing one takes a different path into the field.
boot() {
    _tag=$1; shift
    env "$@" \
        OPENMMO_HEAP_REPORT=1 OPENMMO_FAKE_CROWD=12 \
        PC_ROM="$ROM" PC_SAVE="$tmp/$_tag.sav" PC_LAB="$tmp/min.lab" \
        PC_LAB_AT=1800 PC_FRAMES=2600 PC_PACE=0 PC_INPUT="$SETTLE" \
        "$FUSED" > "$tmp/$_tag.log" 2>&1 && echo 0 || echo $?
}

# "openmmo: field heap FIELD1, engine 0xc4000, built 0xd8000 (+0x14000)"
# -> the built size minus the size the engine asked for, in decimal.
heap_delta() {
    _line=$(grep -m1 "field heap $1," "$2" || true)
    [ -n "$_line" ] || { echo none; return; }
    _eng=$(echo "$_line" | sed 's/.*engine \(0x[0-9a-f]*\).*/\1/')
    _got=$(echo "$_line" | sed 's/.*built \(0x[0-9a-f]*\).*/\1/')
    echo $(( _got - _eng ))
}

echo "every ceiling this build raised is the size the engine was built with:"

rc=$(boot base)
if [ "$rc" -ne 0 ]; then
    bad "the field comes up with twelve remote players on it (exit $rc)"
    tail -3 "$tmp/base.log" | sed 's/^/       /'
    echo "ceiling: FAILED"
    exit 1
fi

got=$(heap_delta FIELD1 "$tmp/base.log")
if [ "$got" = "$FIELD1_EXTRA" ]; then
    ok "FIELD1 is created $FIELD1_EXTRA bytes above the size the engine asked for"
else
    bad "FIELD1 is created $FIELD1_EXTRA bytes above the engine's (it is $got)"
fi

got=$(heap_delta FIELD2 "$tmp/base.log")
if [ "$got" = "$FIELD2_EXTRA" ]; then
    ok "FIELD2 is created $FIELD2_EXTRA bytes above the size the engine asked for"
else
    bad "FIELD2 is created $FIELD2_EXTRA bytes above the engine's (it is $got)"
fi

# FIELD3 is in the hook's table and deliberately unraised. If this moves, the
# adjustment is being applied to heaps nobody measured.
got=$(heap_delta FIELD3 "$tmp/base.log")
if [ "$got" = "0" ]; then
    ok "FIELD3, which this build does not raise, is the engine's own size"
else
    bad "FIELD3, which this build does not raise, is the engine's own size (it is $got)"
fi

# The engine's own accessor over the table it built, not our capacity function.
pools=$(grep -m1 'openmmo: pools,' "$tmp/base.log" || true)
built=$(echo "$pools" | sed -n 's/.*map objects \([0-9]*\) built.*/\1/p')
if [ "$built" = "$MAP_OBJECTS" ]; then
    ok "the map object table the engine built holds $MAP_OBJECTS, not its own 64"
else
    bad "the map object table the engine built holds $MAP_OBJECTS, not its own 64 (it holds ${built:-nothing it said})"
fi

anims=$(echo "$pools" | sed -n 's/.*anims \([0-9]*\) asked.*/\1/p')
if [ "$anims" = "$OVERWORLD_ANIMS" ]; then
    ok "the field asks for $OVERWORLD_ANIMS animation managers, not the engine's 80"
else
    bad "the field asks for $OVERWORLD_ANIMS animation managers, not the engine's 80 (it asks ${anims:-nothing it said})"
fi

echo "a field heap with no room stops the run instead of corrupting:"

# Shrink each field heap past what the map load needs and check the trap in
# openmmo_crowd.c is what ends the run.
for h in "FIELD1 -0x80000" "FIELD2 -0x18000"; do
    heap=${h% *}
    by=${h#* }
    rc=$(boot "starve_$heap" "OPENMMO_HEAP_${heap}_EXTRA=$by")
    if [ "$rc" -ne 0 ] && grep -q '^openmmo: a heap is full' "$tmp/starve_$heap.log"; then
        ok "$heap shrunk by $by stops on the heap trap (exit $rc)"
    else
        bad "$heap shrunk by $by stops on the heap trap (exit $rc)"
        tail -3 "$tmp/starve_$heap.log" | sed 's/^/       /'
    fi
done

[ "$fail" -eq 0 ] || { echo "ceiling: FAILED"; exit 1; }
echo "ceiling: all checks passed"
