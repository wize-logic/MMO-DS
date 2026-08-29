#!/bin/sh
# The committed keyboard layout against the engine tables it
# is flattened out of.
set -eu

ROOT=${1:?usage: osk_gen_test.sh <mmo-root> <engine-dir>}
ENGINE=${2:?usage: osk_gen_test.sh <mmo-root> <engine-dir>}

GEN="$ROOT/tools/gen_osk_layout.py"
TABLE="$ROOT/src/osk_layout.gen.h"
NAMING="src/applications/naming_screen.c"
CHARH="include/constants/charcode.h"
CHARMAP="tools/msgenc/charmap.txt"

for f in "$NAMING" "$CHARH" "$CHARMAP"; do
    if [ ! -f "$ENGINE/$f" ]; then
        echo "osk-gen: SKIP (no engine keyboard at $ENGINE/$f)"
        exit 0
    fi
done

fail=0
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

echo "the keyboard layout is what the engine naming screen says it is:"

python3 "$GEN" "$ENGINE" "$tmp/regen.h" 2> "$tmp/gen.log"

if diff -u "$TABLE" "$tmp/regen.h" > "$tmp/diff.txt" 2>&1; then
    echo "  ok   the table re-derives byte for byte from naming_screen.c"
else
    echo "  FAIL the table and the engine keyboard have drifted apart"
    head -30 "$tmp/diff.txt"
    echo "       regenerate: python3 mmo/tools/gen_osk_layout.py"
    fail=1
fi

if grep -q "OSK_CELL_NUMPAD" "$TABLE" && grep -q "0xE006" "$TABLE"; then
    echo "  ok   the home row carries the numpad button this client added"
else
    echo "  FAIL the committed table has no numpad home-row button"
    fail=1
fi

if grep -q "CHAR_SLASH" "$ENGINE/$NAMING"; then
    echo "  ok   the engine others page still names CHAR_SLASH"
else
    echo "  FAIL naming_screen.c no longer names CHAR_SLASH"
    fail=1
fi

if [ "$fail" -eq 0 ]; then
    echo "osk-gen: all checks passed"
else
    echo "osk-gen: FAILED"
fi
exit "$fail"
