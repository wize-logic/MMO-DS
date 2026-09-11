#!/bin/sh
# The two guards over the engine boundary, checked the only
# way either can be, by breaking them.
set -eu

ROOT=${1:?usage: pincheck_test.sh <mmo-root> <engine-dir>}
ENGINE=${2:-$ROOT/../engine/pokeplatinum}

SURFACE="$ROOT/tools/engine_surface.sh"
SITES="$ROOT/tools/patch_sites.py"

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT INT TERM
M=$tmp/manifest
fail=0

say() {
    if [ "$1" = 0 ]; then echo "  ok   $2"; else echo "  FAIL $2"; fail=1; fi
}

# ---------------------------------------------------------------------------
# The patch-context guard. `patch` finds a hunk by its context and not by the
# line in its `@@` header, so a hunk whose context occurs twice can land on the
# wrong one of them with nothing printed anywhere. These are the shapes.
# ---------------------------------------------------------------------------
echo "the patch-context guard, one shape at a time:"

# Two functions with identical bodies, which is what makes the question real.
cat >"$tmp/twice.c" <<'EOF'
static void a(void)
{
    one();
    two();
}

static void b(void)
{
    one();
    two();
}
EOF

sites() {
    python3 "$SITES" "$1" "$2" >"$tmp/sites.out" 2>&1
}

# One hunk, two candidate sites: the case that has to fail.
cat >"$tmp/ambiguous.patch" <<'EOF'
--- a/twice.c
+++ b/twice.c
@@ -1,4 +1,5 @@
 {
     one();
+    hooked();
     two();
 }
EOF
if sites "$tmp/ambiguous.patch" "$tmp/twice.c"; then
    say 1 "a hunk whose context occurs twice is refused"
    cat "$tmp/sites.out"
else
    grep -q "2 sites" "$tmp/sites.out" &&
        say 0 "a hunk whose context occurs twice is refused, and both sites named" ||
        { say 1 "a hunk whose context occurs twice names its sites"; cat "$tmp/sites.out"; }
fi

# Two candidate sites and two byte-identical hunks: whichever way patch deals
# them out the file ends up the same, so this one passes.
cat >"$tmp/paired.patch" <<'EOF'
--- a/twice.c
+++ b/twice.c
@@ -1,4 +1,5 @@
 {
     one();
+    hooked();
     two();
 }
@@ -7,4 +8,5 @@
 {
     one();
+    hooked();
     two();
 }
EOF
if sites "$tmp/paired.patch" "$tmp/twice.c"; then
    say 0 "two identical hunks over two identical sites are allowed"
else
    say 1 "two identical hunks over two identical sites are allowed"
    cat "$tmp/sites.out"
fi

# The same hook carried out to the function signature: one site, and it passes.
cat >"$tmp/named.patch" <<'EOF'
--- a/twice.c
+++ b/twice.c
@@ -1,5 +1,6 @@
 static void a(void)
 {
     one();
+    hooked();
     two();
 }
EOF
if sites "$tmp/named.patch" "$tmp/twice.c"; then
    say 0 "the same hook carried out to the signature names one site"
else
    say 1 "the same hook carried out to the signature names one site"
    cat "$tmp/sites.out"
fi

# A hunk that lands on an earlier hunk's result matches nowhere in the file as
# it stands. That is legal, and whether it applies is patch's own answer.
cat >"$tmp/stacked.patch" <<'EOF'
--- a/twice.c
+++ b/twice.c
@@ -1,5 +1,6 @@
 static void a(void)
 {
     one();
+    hooked();
     two();
 }
@@ -2,5 +2,6 @@
 {
     one();
     hooked();
+    later();
     two();
 }
EOF
if sites "$tmp/stacked.patch" "$tmp/twice.c"; then
    grep -q "matches nowhere" "$tmp/sites.out" &&
        say 0 "a hunk that lands on an earlier one is reported and not failed" ||
        { say 1 "a hunk that lands on an earlier one is reported"; cat "$tmp/sites.out"; }
else
    say 1 "a hunk that lands on an earlier one is not failed"
    cat "$tmp/sites.out"
fi

if [ ! -d "$ENGINE/include" ]; then
    echo "pincheck: SKIP the manifest half (no engine checkout at $ENGINE)"
    if [ "$fail" -eq 0 ]; then
        echo "pincheck: all checks passed"
    else
        echo "pincheck: FAILED"
    fi
    exit "$fail"
fi

check() {
    sh "$SURFACE" --engine "$ENGINE" --root "$ROOT" --manifest "$M" --check >"$tmp/out" 2>&1
}

echo "the engine surface manifest, broken one way at a time:"

sh "$SURFACE" --engine "$ENGINE" --root "$ROOT" --manifest "$M" --update >/dev/null

# 1. A manifest minted from the engine describes the engine.
if check; then
    n=$(grep -cvE '^[[:space:]]*(#|$)' "$M" || true)
    say 0 "a freshly written manifest checks clean over $n engine paths"
else
    say 1 "a freshly written manifest checks clean"
    cat "$tmp/out"
fi

# 2. The whole point: a path whose content moved is named, and the run fails.
victim=$(grep -vE '^[[:space:]]*(#|$)' "$M" | awk '$2 ~ /^src\// { print $2; exit }')
sed "s|^[0-9a-f]*  $(echo "$victim" | sed 's/[\/&]/\\&/g')\$|$(printf '%064d' 0)  $victim|" "$M" >"$M.x"
mv "$M.x" "$M"
if check; then
    say 1 "a changed engine file fails the check"
else
    if grep -q "$victim" "$tmp/out" && grep -q CHANGED "$tmp/out"; then
        say 0 "a changed engine file is named ($victim) and fails the check"
    else
        say 1 "a changed engine file is named and fails the check"
        cat "$tmp/out"
    fi
fi

# 3. Hashing the wrong bytes is the silent failure. A file we patch is hashed as
# what the compile hands our patch, after strip_asm.py and the engine's own
# pc/patches diff, so for a file the engine itself patches, the manifest hash
# must not be the hash of the file as it sits in the checkout.
sh "$SURFACE" --engine "$ENGINE" --root "$ROOT" --manifest "$M" --update >/dev/null
based=
for f in $(grep -vE '^[[:space:]]*(#|$)' "$M" | awk '$2 ~ /^src\// { print $2 }'); do
    [ -f "$ENGINE/pc/patches/$f.patch" ] || continue
    raw=$(sha256sum <"$ENGINE/$f" | cut -d' ' -f1)
    got=$(grep "  $f\$" "$M" | awk '{ print $1 }')
    based=$f
    if [ "$raw" = "$got" ]; then
        say 1 "$f is hashed as what the compile hands our patch, not as the raw file"
    fi
    break
done
if [ -n "$based" ]; then
    say 0 "$based is hashed after strip_asm and the engine's own diff, not raw"
else
    say 1 "at least one patched file also carries an engine base patch to hash through"
fi

# 4. A path that left the engine is a different failure from one that changed,
# and reads as a different word. --update must also refuse to write that word
# back as if it were a hash: "GONE" recorded once would match a missing file for
# ever after.
printf '%064d  src/a_file_the_engine_does_not_have.c\n' 0 >>"$M"
if check; then
    say 1 "a path the engine no longer has is reported GONE"
else
    grep -q GONE "$tmp/out" && say 0 "a path the engine no longer has is reported GONE" \
        || { say 1 "a path the engine no longer has is reported GONE"; cat "$tmp/out"; }
fi
if sh "$SURFACE" --engine "$ENGINE" --root "$ROOT" --manifest "$M" --update >/dev/null 2>&1; then
    say 1 "--update refuses to record a path it cannot hash"
else
    say 0 "--update refuses to record a path it cannot hash"
fi

# 5. Coverage: a patch nobody hashes is the case this file exists for, so
# dropping an entry has to fail rather than shrink the count quietly. Minted
# from scratch: --update carries forward whatever the old manifest listed, so
# case 4's invented path would otherwise still be here and be the one dropped.
rm -f "$M"
sh "$SURFACE" --engine "$ENGINE" --root "$ROOT" --manifest "$M" --update >/dev/null
dropped=$(grep -vE '^[[:space:]]*(#|$)' "$M" | awk '$2 ~ /^src\// { print $2; exit }')
grep -v "  $dropped\$" "$M" >"$M.x"
mv "$M.x" "$M"
if check; then
    say 1 "a patched file missing from the manifest fails the check"
else
    grep -q "does not cover" "$tmp/out" && grep -q "$dropped" "$tmp/out" &&
        say 0 "a patched file missing from the manifest is named ($dropped)" ||
        { say 1 "a patched file missing from the manifest is named"; cat "$tmp/out"; }
fi

# 6. Half a bump: ENGINE_COMMIT moved and nobody re-minted the manifest. Every
# hash still matches, so this is the one case content hashing cannot see by
# itself, and it is the likeliest way a bump goes wrong.
rm -f "$M"
sh "$SURFACE" --engine "$ENGINE" --root "$ROOT" --manifest "$M" --update >/dev/null
sed 's|^# pin: .*|# pin: 0000000000000000000000000000000000000000|' "$M" >"$M.x"
mv "$M.x" "$M"
if check; then
    say 1 "a manifest written for another commit fails even when every hash matches"
else
    grep -q "different pin" "$tmp/out" &&
        say 0 "a manifest written for another commit fails though every hash matches" ||
        { say 1 "a manifest written for another commit fails"; cat "$tmp/out"; }
fi

# 7. The committed manifest names the commit the tree claims to be pinned to.
# Not a check of the engine, a check that the two committed files agree.
pin=$(grep -vE '^[[:space:]]*(#|$)' "$ROOT/ENGINE_COMMIT" | head -1 | tr -d '[:space:]')
stamped=$(sed -n 's|^#[[:space:]]*pin:[[:space:]]*||p' "$ROOT/ENGINE_SURFACE" | head -1 | tr -d '[:space:]')
if [ "$pin" = "$stamped" ]; then
    say 0 "the committed manifest and ENGINE_COMMIT name the same commit"
else
    say 1 "the committed manifest and ENGINE_COMMIT name the same commit ($stamped vs $pin)"
fi

if [ "$fail" -eq 0 ]; then
    echo "pincheck: all checks passed"
else
    echo "pincheck: FAILED"
fi
exit "$fail"
