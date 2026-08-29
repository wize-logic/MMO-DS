#!/bin/sh
# The two field-data worlds, and the page that describes them.
set -eu

ROOT=${1:?usage: world_test.sh <mmo-root> <engine-root>}
ENGINE=${2:-}

DOC="$ROOT/WORLD.md"
SURVEY="$ROOT/tools/world_survey.py"
# The survey takes a directory holding the trees by name, so this asks the
# resolver for one and keeps its parent. That way it follows a machine's
# maintained checkouts and the vendored copies alike, rather than a fixed path
# that only one of the two layouts has.
DECOMP=$("$ROOT/tools/decomp_dir.sh" pokeemerald 2>/dev/null) || DECOMP=
[ -n "$DECOMP" ] && DECOMP=$(dirname "$DECOMP")

if [ ! -f "$DOC" ] || [ ! -f "$SURVEY" ]; then
    echo "world: SKIP (no $DOC or $SURVEY)"
    exit 0
fi
if ! command -v python3 >/dev/null 2>&1; then
    echo "world: SKIP (no python3)"
    exit 0
fi
if [ ! -d "$ENGINE/res/field" ]; then
    echo "world: SKIP (no engine checkout at $ENGINE)"
    exit 0
fi
if [ ! -f "$DECOMP/pokeemerald/data/layouts/layouts.json" ]; then
    echo "world: SKIP (no decomp trees: git submodule update --init --recursive)"
    exit 0
fi

fail=0
echo "the two field-data worlds, and the page that describes them:"

# --- 1. the structure a Sinnoh reader will depend on, asserted over the tree.
out=$(python3 "$SURVEY" --engine "$ENGINE" --decomp "$DECOMP" --check 2>&1) || {
    echo "$out" | sed -n 's/^/  /p'
    echo "  FAIL the survey's own checks did not pass"
    fail=1
}
[ "$fail" -eq 0 ] && echo "  ok   land data, warps and event placement are as a reader will assume"

# --- 2. and the page says what was measured.
facts=$(python3 "$SURVEY" --engine "$ENGINE" --decomp "$DECOMP" --facts)
fact() { printf '%s\n' "$facts" | sed -n "s/^$1 //p"; }

# The page is prose and wraps where it likes, so match it as one line with runs
# of whitespace squeezed: a sentence that reflows is not a drift.
page=$(tr '\n' ' ' <"$DOC" | tr -s ' ')

say() { # say <what> <text...>
    what=$1
    shift
    for want in "$@"; do
        case "$page" in
        *"$want"*) ;;
        *)
            echo "  FAIL WORLD.md does not say $what: expected \"$want\""
            fail=1
            return
            ;;
        esac
    done
    echo "  ok   $what"
}

say "the header and chunk counts" \
    "**$(fact headers)** rows" \
    "**$(fact chunks)** land-data chunks of **$(fact chunk_side)x$(fact chunk_side)** tiles"

say "the terrain plane's layout" \
    "**$(fact terrain_bytes)** bytes per chunk at offset **$(fact terrain_offset)**" \
    "collision at **bit $(fact collision_bit)**" \
    "**$(fact walkable)** walkable tiles, **$(fact blocked)** blocked, **$(fact behaviours)** distinct behaviours"

say "the event counts" \
    "**$(fact warps)** warps, **$(fact objects)** object events, **$(fact bg_events)** background events, **$(fact coord_events)** coordinate triggers" \
    "**$(fact encounters)** tables; **$(fact scripts)** script files"

say "how warps resolve" \
    "All **$(fact warps)** resolve: **$(fact warps_static)** statically and **$(fact warps_dynamic)** to \`MAP_HEADER_DYNAMIC\`"

say "the one modelling difference" \
    "\`$(fact overworld_matrix)\` carries **$(fact overworld_headers)** map headers over **$(fact overworld_width)x$(fact overworld_height)** tiles" \
    "all **$(fact events_placed)** events sit inside their own matrix's bounds"

say "what the other direction costs" \
    "Hoenn | $(fact hoenn_maps) maps, $(fact hoenn_layouts) layouts, **$(fact hoenn_metatiles)** metatiles" \
    "Kanto | $(fact kanto_maps) maps, $(fact kanto_layouts) layouts, **$(fact kanto_metatiles)** metatiles" \
    "**$(fact gba_chunks)** new $(fact chunk_side)x$(fact chunk_side) chunks" \
    "**$(fact model_share)%** of what a land-data chunk *is* is a \`BMD0\` model" \
    "terrain plane is $(fact terrain_share)% of it"

if [ "$fail" -ne 0 ]; then
    echo "world: checks failed"
    exit 1
fi
echo "world: all checks passed"
