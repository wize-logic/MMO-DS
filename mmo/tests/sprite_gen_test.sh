#!/bin/sh
# The committed sprite-index table against the engine
# code it was read out of.
set -eu

ROOT=${1:?usage: sprite_gen_test.sh <mmo-root> <engine-dir>}
ENGINE=${2:?usage: sprite_gen_test.sh <mmo-root> <engine-dir>}

GEN="$ROOT/tools/gen_sprite_index.py"
TABLE="$ROOT/src/sprite_index.gen.h"

SPECIES="generated/species.txt"
GENDERS="generated/genders.txt"
FORMS="include/constants/forms.h"
POKEMON_H="include/pokemon.h"
POKEMON_C="src/pokemon.c"
POKEGRA="build/rom/res/pokemon/pl_pokegra.narc"
OTHERPOKE="build/rom/res/pokemon/pl_otherpoke.narc"

for f in "$SPECIES" "$GENDERS" "$FORMS" "$POKEMON_H" "$POKEMON_C" "$POKEGRA" "$OTHERPOKE"; do
    if [ ! -f "$ENGINE/$f" ]; then
        echo "sprite-gen: SKIP (no engine sprite data at $ENGINE/$f)"
        exit 0
    fi
done

fail=0
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

echo "the sprite index is what the engine's own sprite code says it is:"

python3 "$GEN" "$ENGINE" "$tmp/regen.h" 2> "$tmp/gen.log"

if diff -u "$TABLE" "$tmp/regen.h" > "$tmp/diff.txt" 2>&1; then
    echo "  ok   the table re-derives byte for byte from pokemon.c and the archives"
else
    echo "  FAIL the table and the engine's sprite code have drifted apart"
    head -30 "$tmp/diff.txt"
    echo "       regenerate: python3 mmo/tools/gen_sprite_index.py"
    fail=1
fi

# The stride is the one number both halves depend on: the default arm's own
# `species * 6` and the archive's member count have to agree, or every species
# past the first is drawn with someone else's tiles.
if grep -q "MMO_SPRITE_POKEGRA_MEMBERS   2964" "$TABLE" \
    && grep -q "MMO_SPRITE_POKEGRA_STRIDE    6" "$TABLE"; then
    echo "  ok   pl_pokegra's member count is still six to the species"
else
    echo "  FAIL pl_pokegra's stride and member count are no longer what was measured"
    fail=1
fi

# --- and the generator refuses an engine tree that does not say what it must ---

fake="$tmp/engine"
mkfake() {
    rm -rf "$fake"
    mkdir -p "$fake/generated" "$fake/include/constants" "$fake/src" \
             "$fake/build/rom/res/pokemon"
    cp "$ENGINE/$SPECIES"   "$fake/$SPECIES"
    cp "$ENGINE/$GENDERS"   "$fake/$GENDERS"
    cp "$ENGINE/$FORMS"     "$fake/$FORMS"
    cp "$ENGINE/$POKEMON_H" "$fake/$POKEMON_H"
    cp "$ENGINE/$POKEMON_C" "$fake/$POKEMON_C"
    # The archives are read for two words each; a link keeps this cheap.
    ln -s "$ENGINE/$POKEGRA"   "$fake/$POKEGRA"
    ln -s "$ENGINE/$OTHERPOKE" "$fake/$OTHERPOKE"
}

refuses() {
    if python3 "$GEN" "$fake" "$tmp/broken.h" > /dev/null 2>&1; then
        echo "  FAIL $1"
        fail=1
    else
        echo "  ok   $1"
    fi
}

echo "and the generator refuses an engine tree it cannot read:"

mkfake
sed -i 's|spriteTemplate->character = 138 + (face / 2) + form \* 2;|spriteTemplate->character = 138 + (face / 3) + form * 2;|' "$fake/$POKEMON_C"
refuses "an expression shape it does not know is refused, not approximated"

mkfake
sed -i 's|spriteTemplate->character = species \* 6 + face + (gender != GENDER_FEMALE ? 1 : 0); // ternary must remain to match|spriteTemplate->character = species * 8 + face;|' "$fake/$POKEMON_C"
refuses "a default arm that has changed is refused, because src/sprite.c copies it"

mkfake
sed -i 's|spriteTemplate->character = 150 + (face / 2) + form \* 2;|spriteTemplate->character = 250 + (face / 2) + form * 2;|' "$fake/$POKEMON_C"
refuses "a case reaching past the end of pl_otherpoke is refused"

mkfake
sed -i 's|#define ROTOM_FORM_COUNT 6|#define ROTOM_FORM_COUNT 60|' "$fake/$FORMS"
refuses "a form count that overruns the archive is refused"

mkfake
sed -i 's|#define UNOWN_FORM_COUNT 28||' "$fake/$FORMS"
refuses "a form clamp whose count is missing is refused"

mkfake
sed -i 's|#define FACE_FRONT 2||' "$fake/$POKEMON_H"
refuses "an engine that no longer numbers its faces is refused"

if [ "$fail" -eq 0 ]; then
    echo "sprite-gen: all checks passed"
else
    echo "sprite-gen: FAILED"
fi
exit "$fail"
