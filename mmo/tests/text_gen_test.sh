#!/bin/sh
# The committed text-bank table against the two text
# archives it stands between.
set -eu

ROOT=${1:?usage: text_gen_test.sh <mmo-root> <engine-dir>}
ENGINE=${2:?usage: text_gen_test.sh <mmo-root> <engine-dir>}

ENGINE_BANKS="$ENGINE/generated/text_banks.txt"
ENGINE_TEXT="$ENGINE/res/text"
SERVER_DECOMP=$("$ROOT/tools/decomp_dir.sh" pokeplatinum 2>/dev/null \
    || echo "")
TABLE="$ROOT/src/idmap_text.gen.h"

if [ ! -f "$ENGINE_BANKS" ] || [ ! -d "$ENGINE_TEXT" ]; then
    echo "text-gen: SKIP (no engine text archive at $ENGINE_TEXT)"
    exit 0
fi
if [ ! -f "$SERVER_DECOMP/generated/text_banks.txt" ]; then
    echo "text-gen: SKIP (no decomp text archive at $SERVER_DECOMP)"
    exit 0
fi

fail=0
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

echo "the text-bank table is what the engine's own text archive says it is:"

python3 "$ROOT/tools/gen_idmap_text.py" \
    "$ENGINE_BANKS" "$ENGINE_TEXT" "$tmp/regen.h" 2> "$tmp/gen.log"

if diff -u "$TABLE" "$tmp/regen.h" > "$tmp/diff.txt" 2>&1; then
    banks=$(grep -c '^    ' "$TABLE")
    echo "  ok   all $banks bank lengths re-derive from the engine's own text source"
else
    echo "  FAIL the table and the engine's text archive have drifted apart"
    head -30 "$tmp/diff.txt"
    echo "       regenerate: python3 mmo/tools/gen_idmap_text.py"
    fail=1
fi

# Twinleaf Town is the row the engine's own build output was read for, so it is
# the one that has to keep saying what that output said.
if grep -q '^ *15, /\* twinleaf_town \*/$' "$TABLE"; then
    echo "  ok   bank 554 is twinleaf_town with 15 messages, as the engine's build numbers it"
else
    echo "  FAIL bank 554 should be twinleaf_town with 15 messages"
    fail=1
fi

# Every id the server's catalogue can emit, against the table the client will
# resolve it with. This is the check the two halves cannot do for themselves.
python3 - "$SERVER_DECOMP" "$TABLE" > "$tmp/cross.txt" 2>&1 <<'PY' || fail=1
import json, os, re, sys

decomp, table = sys.argv[1], sys.argv[2]

sizes = []
names = []
for line in open(table):
    m = re.match(r"\s*(\d+), /\* (\S+)", line)
    if m:
        sizes.append(int(m.group(1)))
        names.append(m.group(2))

order = [l.strip()[len("TEXT_BANK_"):].lower()
         for l in open(os.path.join(decomp, "generated/text_banks.txt")) if l.strip()]

if order != names:
    sys.exit("bank order differs: the two archives are not the same archive")

bad = 0
ids = 0
for index, name in enumerate(order):
    path = os.path.join(decomp, "res/text", name + ".json")
    if not os.path.isfile(path):
        continue  # not authored, so the server mints no id into it
    count = len(json.load(open(path))["messages"])
    ids += count
    if count != sizes[index]:
        print("  FAIL bank %d (%s): server has %d messages, client's table has %d"
              % (index, name, count, sizes[index]))
        bad += 1
        continue
    # The ids are 0 until count in this bank; the client resolves an id when the
    # bank is measurable and the entry is inside it, so those two are the check.
    if sizes[index] == 0:
        print("  FAIL bank %d (%s): the server can send ids the client cannot resolve"
              % (index, name))
        bad += 1

if bad:
    sys.exit("  %d bank(s) the two halves disagree about" % bad)
print("  ok   all %d ids the server's catalogue can send resolve in the client's table" % ids)
PY

cat "$tmp/cross.txt"

if [ "$fail" -eq 0 ]; then
    echo "text-gen: all checks passed"
else
    echo "text-gen: FAILED"
fi
exit $fail
