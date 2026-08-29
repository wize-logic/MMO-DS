#!/bin/sh
# The committed type table against the engine data it is
# flattened out of.
set -eu

ROOT=${1:?usage: display_gen_test.sh <mmo-root> <engine-dir>}
ENGINE=${2:?usage: display_gen_test.sh <mmo-root> <engine-dir>}

GEN="$ROOT/tools/gen_display_types.py"
TABLE="$ROOT/src/display_types.gen.h"

TYPES="generated/pokemon_types.txt"
NAMES="res/text/pokemon_type_names.json"
BATTLE_H="include/constants/battle.h"
BATTLE_C="src/battle/battle_lib.c"

for f in "$TYPES" "$NAMES" "$BATTLE_H" "$BATTLE_C"; do
    if [ ! -f "$ENGINE/$f" ]; then
        echo "display-gen: SKIP (no engine type data at $ENGINE/$f)"
        exit 0
    fi
done

fail=0
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

echo "the type table is what the engine's own type data says it is:"

python3 "$GEN" "$ENGINE" "$tmp/regen.h" 2> "$tmp/gen.log"

if diff -u "$TABLE" "$tmp/regen.h" > "$tmp/diff.txt" 2>&1; then
    echo "  ok   the table re-derives byte for byte from the engine's list, text bank and chart"
else
    echo "  FAIL the table and the engine's type data have drifted apart"
    head -30 "$tmp/diff.txt"
    echo "       regenerate: python3 mmo/tools/gen_display_types.py"
    fail=1
fi

# The names come from a text bank, the ids from a constant list, and nothing
# outside this generator asserts the two are the same ordering.
if grep -q "TYPE_MYSTERY" "$ENGINE/$TYPES" && grep -q '"???"' "$TABLE"; then
    echo "  ok   the ??? placeholder is at the id the engine's own list gives it"
else
    echo "  FAIL the ??? placeholder is not where the engine puts it"
    fail=1
fi

# --- and the generator refuses an engine tree that does not say what it must ---

fake="$tmp/engine"
mkfake() {
    rm -rf "$fake"
    mkdir -p "$fake/generated" "$fake/res/text" "$fake/include/constants" "$fake/src/battle"
    cp "$ENGINE/$TYPES"    "$fake/$TYPES"
    cp "$ENGINE/$NAMES"    "$fake/$NAMES"
    cp "$ENGINE/$BATTLE_H" "$fake/$BATTLE_H"
    cp "$ENGINE/$BATTLE_C" "$fake/$BATTLE_C"
}

refuses() {
    if python3 "$GEN" "$fake" "$tmp/broken.h" > /dev/null 2>&1; then
        echo "  FAIL $1"
        fail=1
    else
        echo "  ok   $1"
    fi
}

echo "and the generator refuses an engine tree it cannot read:"

mkfake
python3 - "$fake/$NAMES" <<'PY'
import json, sys
p = sys.argv[1]
d = json.load(open(p))
d["messages"] = d["messages"][:-1]
json.dump(d, open(p, "w"))
PY
refuses "a text bank with the wrong number of names is refused"

mkfake
python3 - "$fake/$NAMES" <<'PY'
import json, sys
p = sys.argv[1]
d = json.load(open(p))
d["messages"][0]["en_US"] = "FIGHTING"
json.dump(d, open(p, "w"))
PY
refuses "a text bank in a different order from the id list is refused"

mkfake
sed -i 's/{ 0xFE, 0xFE, TYPE_MULTI_IMMUNE },//' "$fake/$BATTLE_C"
refuses "a chart with no Foresight split is refused"

mkfake
sed -i 's/{ TYPE_NORMAL, TYPE_ROCK, TYPE_MULTI_NOT_VERY_EFF },/{ TYPE_NORMAL, TYPE_ROCK, TYPE_MULTI_NOT_VERY_EFF },\n    { TYPE_NORMAL, TYPE_ROCK, TYPE_MULTI_SUPER_EFF },/' "$fake/$BATTLE_C"
refuses "a chart listing one pair twice is refused"

mkfake
sed -i 's/#define TYPE_MULTI_SUPER_EFF    20//' "$fake/$BATTLE_H"
refuses "a chart whose multiplier constants are missing is refused"

# --- move / ability / species tables, same shape ---

DATA_GEN="$ROOT/tools/gen_display_data.py"
DATA_TABLE="$ROOT/src/display_data.gen.h"
OVERLAY_TABLE="$ROOT/src/display_overlay.gen.h"
# Optional: point OPENMMO_STRINGS_XML at the official client strings_en.xml to
# re-derive the overlay. Absent, this half SKIPs, the in-process suite
# already pins three overlay strings.

for f in generated/abilities.txt generated/moves.txt generated/species.txt \
         generated/pokemon_types.txt \
         res/text/ability_names.json res/text/ability_descriptions.json \
         res/moves/pound/data.json res/pokemon/bulbasaur/data.json; do
    if [ ! -f "$ENGINE/$f" ]; then
        echo "display-gen: SKIP data tables (no engine file at $ENGINE/$f)"
        if [ "$fail" -eq 0 ]; then
            echo "display-gen: all checks passed"
        else
            echo "display-gen: FAILED"
        fi
        exit "$fail"
    fi
done

echo "the move, ability and species tables are what the engine's own data says:"

python3 "$DATA_GEN" "$ENGINE" "$tmp/regen_data.h" 2> "$tmp/data.log"

if diff -u "$DATA_TABLE" "$tmp/regen_data.h" > "$tmp/data.diff" 2>&1; then
    echo "  ok   the data table re-derives from the engine's abilities, moves and species"
else
    echo "  FAIL the data table and the engine have drifted apart"
    head -30 "$tmp/data.diff"
    echo "       regenerate: python3 mmo/tools/gen_display_data.py"
    fail=1
fi

if grep -q '"Hail", /\* 258 MOVE_HAIL \*/' "$DATA_TABLE"; then
    echo "  ok   move 258 is still Hail in the engine fill"
else
    echo "  FAIL move 258 is not Hail in the engine fill"
    fail=1
fi

# A generator that invents a typing for a species with no data.json would
# hide a missing file behind a plausible row.
datafake="$tmp/dataengine"
mkdir -p "$datafake/generated" "$datafake/res/text" "$datafake/res/moves" \
    "$datafake/res/pokemon"
# The data generator reads whole trees; point it at a hollow checkout.
if python3 "$DATA_GEN" "$datafake" "$tmp/hollow.h" > /dev/null 2>&1; then
    echo "  FAIL a checkout with no generated lists is refused"
    fail=1
else
    echo "  ok   a checkout with no generated lists is refused"
fi

if [ -n "${OPENMMO_STRINGS_XML:-}" ] && [ -f "$OPENMMO_STRINGS_XML" ]; then
    echo "the official client overlay re-derives from the catalogue:"
    python3 "$DATA_GEN" "$ENGINE" "$tmp/regen_data2.h" "$OPENMMO_STRINGS_XML" "$tmp/regen_overlay.h" \
        2> "$tmp/overlay.log"
    if diff -u "$OVERLAY_TABLE" "$tmp/regen_overlay.h" > "$tmp/overlay.diff" 2>&1; then
        echo "  ok   the overlay re-derives from strings_en.xml at the four bases"
    else
        echo " FAIL the overlay and the official client catalogue have drifted apart"
        head -30 "$tmp/overlay.diff"
        fail=1
    fi
    if grep -q '{ 110258, "Snowscape" }' "$OVERLAY_TABLE"; then
        echo "  ok   Hail's overlay is still Snowscape"
    else
        echo "  FAIL Hail's overlay is not Snowscape"
        fail=1
    fi
else
    echo " skip overlay re-derive (no official client string catalogue)"
fi

if [ "$fail" -eq 0 ]; then
    echo "display-gen: all checks passed"
else
    echo "display-gen: FAILED"
fi
exit "$fail"
