#!/bin/sh
# The guard on the two pokeplatinum checkouts, run for
# real and then broken one way at a time.
set -eu

ROOT=${1:?usage: datacheck_test.sh <mmo-root> <engine-dir>}
ENGINE=${2:-$ROOT/../engine/pokeplatinum}

TOOL="$ROOT/tools/data_surface.sh"
REPO=$(CDPATH= cd -- "$ROOT/.." && pwd)

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT INT TERM
fail=0

say() {
    if [ "$1" = 0 ]; then echo "  ok   $2"; else echo "  FAIL $2"; fail=1; fi
}

echo "the two pokeplatinum checkouts, compared and then pulled apart:"

# 1. The live comparison over the real trees.
if sh "$TOOL" --engine "$ENGINE" --repo "$REPO" --root "$ROOT" --check >"$tmp/live" 2>&1; then
    say 0 "$(sed -n '1s/^datacheck: //p' "$tmp/live")"
else
    say 1 "the engine checkout and the tree codegen reads hold the same data"
    cat "$tmp/live"
fi

# Two synthetic checkouts, identical, and a tree list over them. res/ has to
# exist or the tool SKIPs, which case 7 checks.
mk() {
    mkdir -p "$1/res/thing" "$1/generated"
    echo alpha >"$1/res/thing/a.json"
    echo beta >"$1/res/thing/b.json"
    echo "one two three" >"$1/generated/table.txt"
}
mk "$tmp/e"
mk "$tmp/d"

cat >"$tmp/sources" <<'EOF'
readers: alpha beta
res/thing  alpha
generated  alpha beta
EOF

cat >"$tmp/gradle" <<'EOF'
jteCodegen {
  register("alpha") {
    inputDirs.from(ndsDataDir)
  }
  register("beta") {
    inputDirs.from(mapRegionSources)
  }
  register("gamma") {
    inputDirs.from(sourceDecompDir)
  }
}
EOF

run() {
    sh "$TOOL" --engine "$tmp/e" --decomp "$tmp/d" --repo "$REPO" --root "$ROOT" \
        --sources "$tmp/sources" --gradle "$tmp/gradle" --check >"$tmp/out" 2>&1
}

# 2. Two checkouts that agree, agree.
if run; then
    say 0 "two checkouts holding the same bytes check clean over 2 trees"
else
    say 1 "two checkouts holding the same bytes check clean"
    cat "$tmp/out"
fi

# 3. The whole reason this exists: one file's content moved on one side only.
echo BETA >"$tmp/d/res/thing/b.json"
if run; then
    say 1 "a file that differs between the checkouts fails the check"
else
    if grep -q 'res/thing/b.json' "$tmp/out" && grep -q DIFFERS "$tmp/out"; then
        say 0 "a file that differs between the checkouts is named and fails"
    else
        say 1 "a file that differs between the checkouts is named and fails"
        cat "$tmp/out"
    fi
fi
echo beta >"$tmp/d/res/thing/b.json"

# 4. A file on one side only. Hashing file-by-file would miss this if the tool
# walked one checkout's listing and looked each up in the other.
echo gamma >"$tmp/e/res/thing/c.json"
if run; then
    say 1 "a file present in only one checkout fails the check"
else
    if grep -q 'res/thing/c.json' "$tmp/out"; then
        say 0 "a file present in only one checkout is named and fails"
    else
        say 1 "a file present in only one checkout is named and fails"
        cat "$tmp/out"
    fi
fi
rm -f "$tmp/e/res/thing/c.json"

# 5. A declared tree that is not in one of the checkouts at all, silence here
# would mean a tree nobody was comparing reading as verified.
rm -rf "$tmp/d/generated"
if run; then
    say 1 "a tree missing from one checkout fails the check"
else
    if grep -q 'MISSING' "$tmp/out" && grep -q generated "$tmp/out"; then
        say 0 "a tree missing from one checkout is named and fails"
    else
        say 1 "a tree missing from one checkout is named and fails"
        cat "$tmp/out"
    fi
fi
mkdir -p "$tmp/d/generated"
echo "one two three" >"$tmp/d/generated/table.txt"
run || { say 1 "the trees are restored"; cat "$tmp/out"; }

# 6. A new generator registered against the DS decomp, whose trees nobody
# declared. This is how an unguarded tree gets in, and it is silent by nature:
# the generator works, both halves build, and nothing compares what it reads.
sed 's|register("gamma")|register("delta")|; s|sourceDecompDir|ndsDataDir|' \
    "$tmp/gradle" >"$tmp/gradle.x"
mv "$tmp/gradle.x" "$tmp/gradle"
if run; then
    say 1 "a new generator reading the DS decomp fails until its trees are declared"
else
    if grep -q 'undeclared: delta' "$tmp/out"; then
        say 0 "a new generator reading the DS decomp is named (delta) and fails"
    else
        say 1 "a new generator reading the DS decomp is named and fails"
        cat "$tmp/out"
    fi
fi

# 7. The other half of that: a reader declared with no tree beside it guards
# nothing, and would read as covered.
sed 's|^readers: alpha beta|readers: alpha beta delta|' "$tmp/sources" >"$tmp/sources.x"
mv "$tmp/sources.x" "$tmp/sources"
if run; then
    say 1 "a declared reader with no tree fails the check"
else
    if grep -q 'no tree lists it' "$tmp/out"; then
        say 0 "a declared reader owning no tree is named and fails"
    else
        say 1 "a declared reader with no tree fails the check"
        cat "$tmp/out"
    fi
fi

# 8. Absent is a SKIP, not a pass: an exit 0 that compared nothing has to say so.
if sh "$TOOL" --engine "$tmp/nowhere" --decomp "$tmp/d" --repo "$REPO" --root "$ROOT" \
    --sources "$tmp/sources" --gradle "$tmp/gradle" --check 2>&1 | grep -q '^datacheck: SKIP'; then
    say 0 "an absent checkout SKIPs and says which one, rather than passing"
else
    say 1 "an absent checkout SKIPs and says which one"
fi

if [ "$fail" -eq 0 ]; then
    echo "datacheck: all checks passed"
else
    echo "datacheck: FAILED"
fi
exit "$fail"
