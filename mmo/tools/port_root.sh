#!/bin/sh
# Build an engine-shaped directory whose build/ is ours, so the port's own
# instruments run over one of our binaries.
#
#   port_root.sh <engine-dir> <view-dir> <binary> [oracle]
set -eu

if [ $# -lt 3 ]; then
    echo "usage: port_root.sh <engine-dir> <view-dir> <binary> [oracle]" >&2
    exit 2
fi

engine=$1
view=$2
binary=$3
oracle=${4:-}

if [ ! -x "$binary" ]; then
    echo "port_root: no binary at $binary" >&2
    exit 1
fi

rm -rf "$view"
mkdir -p "$view/build/pc"

for e in "$engine"/*; do
    case $(basename "$e") in
    build) ;;
    *) ln -s "$e" "$view/" ;;
    esac
done

ln -s "$engine/build/rom" "$view/build/rom"
ln -s "$binary" "$view/build/pc/pokeplatinum"
if [ -n "$oracle" ] && [ -x "$oracle" ]; then
    ln -s "$oracle" "$view/build/pc/pcdiff-melon"
fi

# Everything the binary was built beside, because some of it is how the suite
# reads the binary: pc_save.py takes the save layout out of the DWARF in
# build/pc/obj/game, and the objects that describe this binary are the ones next
# to it. Anything already placed above wins.
bindir=$(CDPATH= cd -- "$(dirname -- "$binary")" && pwd)
for e in "$bindir"/*; do
    name=$(basename "$e")
    [ -e "$view/build/pc/$name" ] || ln -s "$e" "$view/build/pc/$name"
done

# The engine's own build fills what a mod build does not produce: geninclude is
# the constants the lab compiles a recipe against, and the other two are the
# link reports the suite reads. All three are written by the engine's build and
# only ever read here.
for r in geninclude unresolved.txt defined.txt; do
    if [ -e "$engine/build/pc/$r" ] && [ ! -e "$view/build/pc/$r" ]; then
        ln -s "$engine/build/pc/$r" "$view/build/pc/$r"
    fi
done

echo "portroot: ok ($view/build/pc/pokeplatinum -> $binary)"
