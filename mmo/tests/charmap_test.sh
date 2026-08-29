#!/bin/sh
# The generated charcode table against the engine's own
# character map.
set -eu

ROOT=${1:?usage: charmap_test.sh <mmo-root> <engine-dir>}
ENGINE=${2:?usage: charmap_test.sh <mmo-root> <engine-dir>}

CHARMAP="$ENGINE/tools/msgenc/charmap.txt"
TABLE="$ROOT/src/charcode_glyphs.gen.h"

if [ ! -f "$CHARMAP" ]; then
    echo "charmap: SKIP (no character map at $CHARMAP)"
    exit 0
fi

fail=0
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

echo "the charcode table is the engine's character map:"

python3 "$ROOT/tools/gen_charmap.py" --engine "$ENGINE" --out "$tmp/regen.h"

if diff -u "$TABLE" "$tmp/regen.h" > "$tmp/diff.txt" 2>&1; then
    rows=$(grep -c '^    {' "$TABLE")
    echo "  ok   all $rows rows re-derive from tools/msgenc/charmap.txt"
else
    echo "  FAIL the table and the character map have drifted apart"
    head -30 "$tmp/diff.txt"
    echo "       regenerate: python3 mmo/tools/gen_charmap.py --engine \$ENGINE_DIR \\"
    echo "                       --out mmo/src/charcode_glyphs.gen.h"
    fail=1
fi

# The accented Latin set is the reason this table exists: a European username is
# the first thing a server sends that the old table could not spell. Naming the
# range here means a regeneration that quietly loses it fails rather than passes
# with fewer rows.
missing=
for cp in 00C0 00C9 00D1 00DF 00E9 00F1 00FC 00FF 0152 0153 015E 015F; do
    grep -q "{ 0x$cp," "$TABLE" || missing="$missing U+$cp"
done
if [ -z "$missing" ]; then
    echo "  ok   the accented Latin set a European name needs is all present"
else
    echo "  FAIL the accented Latin set a European name needs is all present ($missing)"
    fail=1
fi

if [ "$fail" -eq 0 ]; then
    echo "charmap: all checks passed"
else
    echo "charmap: FAILED"
fi
exit "$fail"
