#!/bin/sh
# Which checkout of a decompilation this repo reads.
#
#   decomp_dir.sh <name> [--why]
#   * $DECOMP_DIR/<name>, the maintained checkout, a working tree with its own
#     remote and the day's work in it. `pokeplatinum` there is $ENGINE_DIR, the
#     port this client is built out of. This is where the trees are worked on,
#     so it is where their data is current.
#   * decomp/<name>, a checkout put there by hand. Sinnoh is the exception:
#     it resolves to the engine submodule, which this repo already carries
#     from one remote, which is what a machine that does no decomp work wants.
set -eu

name=${1:?usage: decomp_dir.sh <name> [--why]}
why=0
[ "${2:-}" = "--why" ] && why=1

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
repo=$(CDPATH= cd -- "$root/.." && pwd)

maintained=${DECOMP_DIR:-$repo/decomp}/$name
vendored=$repo/decomp/$name
# Sinnoh's tree is the engine, which this repo carries as a submodule.
if [ "$name" = pokeplatinum ]; then
    vendored=$repo/engine/pokeplatinum
fi

if [ -e "$maintained/.git" ]; then
    [ "$why" = 1 ] && printf '%s: maintained checkout\n' "$name"
    (CDPATH= cd -- "$maintained" && pwd)
    exit 0
fi

# An uninitialized submodule is an empty directory, which is not a tree: a
# generator handed one reads a game with no maps rather than failing.
if [ -d "$vendored" ] && [ -n "$(ls -A "$vendored" 2>/dev/null)" ]; then
    [ "$why" = 1 ] && printf '%s: vendored (no maintained checkout at %s)\n' \
        "$name" "$maintained"
    (CDPATH= cd -- "$vendored" && pwd)
    exit 0
fi

echo "decomp_dir: no checkout of $name, not at $maintained (set DECOMP_DIR)" \
     "and not at $vendored (git submodule update --init --recursive)" >&2
exit 1
