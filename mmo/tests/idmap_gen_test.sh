#!/bin/sh
# The committed shared-item table against the two item
# tables it is derived from.
set -eu

ROOT=${1:?usage: idmap_gen_test.sh <mmo-root> <engine-dir>}
ENGINE=${2:?usage: idmap_gen_test.sh <mmo-root> <engine-dir>}

ITEMS_TXT="$ENGINE/generated/items.txt"
ITEM_DATA="$ENGINE/res/items/data"
SERVER_NAMES="$ROOT/../codegen/items/item_names.json"
TABLE="$ROOT/src/idmap_items.gen.h"

if [ ! -f "$ITEMS_TXT" ] || [ ! -d "$ITEM_DATA" ]; then
    echo "idmap-gen: SKIP (no engine item data at $ITEM_DATA)"
    exit 0
fi
if [ ! -f "$SERVER_NAMES" ]; then
    echo "idmap-gen: SKIP (no server item names at $SERVER_NAMES)"
    exit 0
fi

fail=0
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

echo "the shared item table is what the two item tables say it is:"

python3 "$ROOT/tools/gen_idmap_items.py" \
    "$ITEMS_TXT" "$ITEM_DATA" "$SERVER_NAMES" "$tmp/regen.h" 2> "$tmp/gen.log"

if diff -u "$TABLE" "$tmp/regen.h" > "$tmp/diff.txt" 2>&1; then
    rows=$(grep -c '^    ' "$TABLE")
    echo "  ok   all $rows ids re-derive from the engine's and the server's own tables"
else
    echo "  FAIL the table and the two item tables have drifted apart"
    head -30 "$tmp/diff.txt"
    echo "       regenerate: python3 mmo/tools/gen_idmap_items.py"
    fail=1
fi

# The ids the two worlds are known to disagree about, each for its own reason.
# Naming them here means a regeneration that quietly starts translating one, or
# stops translating an ordinary item, fails rather than passes with a different
# row count.
for id in 17 13 137 467; do
    if grep -q "^ *$id, /\*" "$TABLE"; then
        echo "  ok   item $id is shared, as the server's own catalogue has it"
    else
        echo "  FAIL item $id is shared, as the server's own catalogue has it"
        fail=1
    fi
done
for id in 113 426 427; do
    if grep -q "^ *$id, /\*" "$TABLE"; then
        echo "  FAIL item $id is in only one of the two worlds and must not be shared"
        fail=1
    else
        echo "  ok   item $id is in only one of the two worlds and is not shared"
    fi
done

# The twelve mail slots hold different mail in the two worlds. They are shared
# ids all the same, and the generator has to keep saying so out loud.
divergent=$(grep -c '/\* engine .* / server .* \*/' "$TABLE" || true)
if [ "$divergent" -eq 12 ]; then
    echo "  ok   the 12 slots the two worlds fill differently are marked as such"
else
    echo "  FAIL 12 slots should be marked as filled differently, found $divergent"
    fail=1
fi

# The third numbering. A regeneration that used gbaID as the engine id would
# list Potion at 13 and draw a Dusk Ball.
if grep -qE '^[[:space:]]*17, /\* Potion; gba 13' "$TABLE"; then
    echo "  ok   Potion is engine 17 and gba 13"
else
    echo "  FAIL Potion must be engine 17 with gba 13 recorded"
    fail=1
fi
if grep -qE '^[[:space:]]*13, /\* Dusk Ball; no gba' "$TABLE"; then
    echo "  ok   Dusk Ball is engine 13 and has no gba id"
else
    echo "  FAIL Dusk Ball must be engine 13 with no gba id"
    fail=1
fi
if grep -qE '^[[:space:]]*13, /\* Potion' "$TABLE"; then
    echo "  FAIL Potion is listed at its gba id; that is a Dusk Ball"
    fail=1
else
    echo "  ok   Potion is not listed at its gba id"
fi
for pair in \
    "MMO_ITEM_WITNESS_POTION_WIRE      5017" \
    "MMO_ITEM_WITNESS_POTION_ENGINE    17" \
    "MMO_ITEM_WITNESS_POTION_GBA       13" \
    "MMO_ITEM_WITNESS_DUSKBALL_ENGINE  13" \
    "MMO_ITEM_WITNESS_DUSKBALL_GBA     0"
do
    if grep -q "#define $pair" "$TABLE"; then
        echo "  ok   witness $pair"
    else
        echo "  FAIL witness $pair is missing or drifted"
        fail=1
    fi
done

if [ "$fail" -eq 0 ]; then
    echo "idmap-gen: all checks passed"
else
    echo "idmap-gen: FAILED"
fi
exit $fail
