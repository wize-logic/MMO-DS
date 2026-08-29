#!/bin/sh
# The differential between the two halves' id tables, run
# for real and then broken one way at a time.
set -eu

ROOT=${1:?usage: tablediff_test.sh <mmo-root> <engine-dir>}
ENGINE=${2:?usage: tablediff_test.sh <mmo-root> <engine-dir>}

TOOL="$ROOT/tools/table_diff.py"

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT INT TERM
fail=0

say() {
    if [ "$1" = 0 ]; then echo "  ok   $2"; else echo "  FAIL $2"; fail=1; fi
}

echo "the id tables the client and the server both hold, walked and then pulled apart:"

# 1. The live walk over the real tables.
if python3 "$TOOL" --engine "$ENGINE" --root "$ROOT" >"$tmp/live" 2>&1; then
    sed -n 's/^  ok   /  ok   /p' "$tmp/live"
else
    if grep -q '^tablediff: SKIP' "$tmp/live"; then
        sed -n '1p' "$tmp/live"
    else
        say 1 "the engine's tables and the server's registries agree where mmo/TABLES says they do"
        cat "$tmp/live"
        exit 1
    fi
fi

# A synthetic pair of halves: two species, one move, one item, one map, one flag.
# Small enough to break precisely, shaped exactly like the real renders.
SERVER="$tmp/repo/codegen/build/generated/source"
MMO="$tmp/repo/mmo"

mk() {
    rm -rf "$tmp/repo"
    mkdir -p "$tmp/engine/generated" "$MMO/src" \
        "$SERVER/pokemon/kotlin/de/fiereu/openmmo/pokemon/generated" \
        "$SERVER/moves/kotlin/de/fiereu/openmmo/moves/generated" \
        "$SERVER/item/kotlin/de/fiereu/openmmo/items/generated" \
        "$SERVER/maps/kotlin/de/fiereu/openmmo/maps/generated"

    printf 'SPECIES_NONE\nSPECIES_ALPHA\nSPECIES_BETA\n' >"$tmp/engine/generated/species.txt"
    printf 'MOVE_NONE\nMOVE_TACKLE\nMAX_MOVES\n' >"$tmp/engine/generated/moves.txt"
    printf 'ITEM_NONE\nITEM_POTION\n' >"$tmp/engine/generated/items.txt"
    printf 'MAP_HEADER_TOWN\nMAP_HEADER_COUNT\n' >"$tmp/engine/generated/map_headers.txt"
    printf 'FLAG_ONE\nFLAG_TWO\n' >"$tmp/engine/generated/vars_flags.txt"

    {
        echo '    reg.register(SpeciesDef(id = 1, name = "ALPHA", baseHp = 1))'
        echo '    reg.register(SpeciesDef(id = 2, name = "BETA", baseHp = 1))'
    } >"$SERVER/pokemon/kotlin/de/fiereu/openmmo/pokemon/generated/GeneratedSpecies.kt"
    echo '    reg.register(MoveDef(id = 1, name = "Tackle", power = 40))' \
        >"$SERVER/moves/kotlin/de/fiereu/openmmo/moves/generated/GeneratedMoves.kt"
    printf '  /** 5001 */\n  val POTION = item("Potion", 300, 0)\n' \
        >"$SERVER/item/kotlin/de/fiereu/openmmo/items/generated/Items.kt"
    echo '    mgr.register(de.fiereu.openmmo.maps.generated.sinnoh.bank0.town)' \
        >"$SERVER/maps/kotlin/de/fiereu/openmmo/maps/generated/GeneratedMaps.kt"

    echo '    1, /* potion */' >"$MMO/src/idmap_items.gen.h"
    echo 'flags  absent all  the story generator reads the GBA decomps only' >"$MMO/TABLES"
}

run() {
    python3 "$TOOL" --engine "$tmp/engine" --root "$MMO" >"$tmp/out" 2>&1
}

# 2. The synthetic pair, untouched: everything agrees and nothing is declared.
mk
if run; then
    say 0 "two halves holding the same tables walk clean over 5 classes"
else
    say 1 "two halves holding the same tables walk clean over 5 classes"
    cat "$tmp/out"
fi

# 3. An id the engine has and the server does not, named as itself.
mk
sed -i '/id = 2/d' "$SERVER/pokemon/kotlin/de/fiereu/openmmo/pokemon/generated/GeneratedSpecies.kt"
if run; then
    say 1 "a species the server's table drops is named"
    cat "$tmp/out"
else
    grep -q 'species: 1 missing.*id 2 (engine BETA' "$tmp/out"
    say $? "a species the server's table drops is named"
fi

# 4. One id, two names: the divergence that is invisible from inside either half.
mk
sed -i 's/name = "BETA"/name = "GAMMA"/' \
    "$SERVER/pokemon/kotlin/de/fiereu/openmmo/pokemon/generated/GeneratedSpecies.kt"
if run; then
    say 1 "one id under two names is named on both sides"
    cat "$tmp/out"
else
    grep -q 'species: 1 named.*engine BETA / server GAMMA' "$tmp/out"
    say $? "one id under two names is named on both sides"
fi

# 5. An id the server has and the engine does not.
mk
echo '    reg.register(MoveDef(id = 2, name = "Stomp", power = 65))' \
    >>"$SERVER/moves/kotlin/de/fiereu/openmmo/moves/generated/GeneratedMoves.kt"
if run; then
    say 1 "a move only the server has is named"
    cat "$tmp/out"
else
    grep -q 'moves: 1 extra.*id 2 (server Stomp' "$tmp/out"
    say $? "a move only the server has is named"
fi

# 6. A gap that has closed but is still written down. A ledger that only ever
#    grows is how a fixed divergence stays "known" forever.
mk
echo 'species  missing  2  beta has no server side' >>"$MMO/TABLES"
if run; then
    say 1 "a declared gap that no longer exists fails as stale"
    cat "$tmp/out"
else
    grep -q 'declares 1 missing that no longer are' "$tmp/out"
    say $? "a declared gap that no longer exists fails as stale"
fi

# 7. A render this cannot read is a failure, not an empty table that agrees with
#    everything. The renders are generated, so their shape can move under us.
mk
echo 'reg.register(SpeciesDef(dexId = 1))' \
    >"$SERVER/pokemon/kotlin/de/fiereu/openmmo/pokemon/generated/GeneratedSpecies.kt"
if run; then
    say 1 "a render whose rows stop parsing fails rather than agreeing"
    cat "$tmp/out"
else
    grep -q 'no species rows in' "$tmp/out"
    say $? "a render whose rows stop parsing fails rather than agreeing"
fi

# 8. A class declared absent that the server has since grown a table for.
mk
mkdir -p "$SERVER/story/kotlin/de/fiereu/openmmo/story/generated/sinnoh"
printf '  const val FLAG_ONE = "sinnoh/FLAG_ONE"\n' \
    >"$SERVER/story/kotlin/de/fiereu/openmmo/story/generated/sinnoh/SinnohFlags.kt"
if run; then
    say 1 "a class declared absent that the server now holds fails"
    cat "$tmp/out"
else
    grep -q 'calls this class absent, but the server holds 1' "$tmp/out"
    say $? "a class declared absent that the server now holds fails"
fi

# 9. No codegen build tree at all: SKIP, and say which half is missing.
mk
rm -rf "$tmp/repo/codegen"
if run; then
    grep -q 'tablediff: SKIP (no codegen renders' "$tmp/out"
    say $? "an unbuilt server SKIPs and says so, rather than passing"
else
    say 1 "an unbuilt server SKIPs and says so, rather than passing"
    cat "$tmp/out"
fi

if [ "$fail" -eq 0 ]; then
    echo "tablediff: all checks passed"
else
    echo "tablediff: FAILED"
fi
exit $fail
