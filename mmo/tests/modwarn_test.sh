#!/usr/bin/env bash
# The mod sources compile with no implicit declaration.
set -u

MMO_ROOT=${1:-}
if [ -z "$MMO_ROOT" ]; then
    echo "usage: modwarn_test.sh <mmo-root>" >&2
    exit 1
fi

# Every mod object is compiled by the fused build, so with no engine checkout
# there is no compile line to read and nothing to check.
ENGINE_TREE=${ENGINE:-${ENGINE_DIR:-$MMO_ROOT/../engine/pokeplatinum}}
if [ ! -d "$ENGINE_TREE/pc" ]; then
    echo "modwarn: SKIP (no engine checkout at $ENGINE_TREE)"
    exit 0
fi

pass=0
fail=0
ok()   { echo "  ok   $1"; pass=$((pass + 1)); }
bad()  { echo "  FAIL $1"; fail=$((fail + 1)); }

srcs=$(find "$MMO_ROOT/mods" -name '*.c' | sort)
if [ -z "$srcs" ]; then
    echo "modwarn: no mod sources found under $MMO_ROOT/mods" >&2
    exit 1
fi

# One real compile line, for whichever mod source the build names first. Every
# mod object is compiled by the same rule, so substituting the path is exact.
first=$(printf '%s\n' "$srcs" | head -1)
first_base=$(basename "$first" .c)
template=$(make -C "$MMO_ROOT" -n fused 2>/dev/null |
    grep -oE "[^&|;]*${first_base}\.c" | grep -E '(^| )(ccache )?[a-z-]*gcc ' | head -1)

if [ -z "$template" ]; then
    echo "modwarn: could not read a compile line for $first_base.c out of \`make -n fused\`" >&2
    echo "modwarn: (an engine or ENGINE_DIR problem, not a mod one, nothing was checked)" >&2
    exit 1
fi

# The compile line the fused build uses names a generated include directory:
# the engine's pc/modinclude.py writes the mod-patched headers there during a
# fused build, and pc_modfs.h, which openmmo_billboard.c and
# openmmo_follow.c read their row structs out of, exists nowhere else.
modinc=$(printf '%s\n' "$template" | tr ' ' '\n' | grep -E '^-I.*/modinclude$' | head -1)
modinc=${modinc#-I}
if [ -n "$modinc" ] && [ ! -f "$modinc/pc_modfs.h" ]; then
    echo "modwarn: $modinc/pc_modfs.h has not been generated" >&2
    echo "modwarn: (the fused build writes it; run \`make -C $MMO_ROOT fused\` first --" >&2
    echo "modwarn: an unbuilt tree, not a mod one, nothing was checked)" >&2
    exit 1
fi

# The substitution below is the whole test, and it is a text substitution: gcc
# under -w reports neither class however they are promoted, so a compile line
# that stopped carrying ` -w `, a flag reordered, -w landing last with no
# space after it, leaves every source compiling quietly and passing. Read it
# once here rather than discovering it as 90 clean files.
case "$template" in
*' -w '*) ;;
*)
    echo "modwarn: the fused compile line no longer carries \` -w \` to remove:" >&2
    echo "modwarn:   $template" >&2
    echo "modwarn: (the promotion below would substitute nothing and every" >&2
    echo "modwarn: source would pass whatever it called, nothing was checked)" >&2
    exit 1 ;;
esac

log=$(mktemp)
trap 'rm -f "$log"' EXIT

echo "the mod sources declare what they call:"
for src in $srcs; do
    base=$(basename "$src" .c)
    line=${template//$first_base/$base}
    # -w goes, or gcc keeps quiet under it; the two classes become errors.
    line=${line// -w / -Werror=implicit-function-declaration -Werror=implicit-int }
    if eval "$line -o /dev/null" > "$log" 2>&1; then
        ok "$(basename "$src")"
        continue
    fi
    out=$(grep -E "$(basename "$src").*error:" "$log" | head -4)
    if [ -n "$out" ]; then
        bad "$(basename "$src")"
        printf '%s\n' "$out" | sed 's/^/       /'
    else
        # A compile that failed with no error line naming this file is not a mod
        # defect and it is not a pass either: it is a header, a toolchain or a
        # generated include, and reading only the grep's output made every one
        # of those look like a clean source.
        bad "$(basename "$src") did not compile at all"
        tail -4 "$log" | sed 's/^/       /'
    fi
done

if [ "$fail" -gt 0 ]; then
    echo "modwarn: $fail file(s) FAILED"
    exit 1
fi
echo "modwarn: all checks passed ($pass file(s))"
