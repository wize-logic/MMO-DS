#!/bin/sh
# A trainer name wider than the engine's name field, and a
# glyph the ROM font does not have.
#
#   1. a sixteen-glyph name reads back as sixteen from the String and seven from
#      the field. Either number moving is the failure: sixteen in the field is
#      the buffer overrun this closed, seven in the String is the wall still up;
#   2. a short name is untouched, source, field and String all agree, because
#      a wide-name path that changes what the single-player game already did is
#      a regression however well it handles long names;
#   3. the glyph hook is on the drawing path and the engine's own text never
#      needs it. FontManager_TryLoadGlyph used to substitute '?' in silence for
#      any charcode past the font, and underflow to glyph index 0xFFFF for
#      charcode 0. The count of glyphs drawn is what tells "nothing was
#      substituted" apart from "the hook is not being called at all", and
#      OPENMMO_FONT=fatal turns the first substitution into a non-zero exit.
set -eu

ROOT=${1:?usage: name_test.sh <mmo-root> <build-dir> <engine-dir>}
BUILD=${2:?usage: name_test.sh <mmo-root> <build-dir> <engine-dir>}
ENGINE=${3:?usage: name_test.sh <mmo-root> <build-dir> <engine-dir>}

FUSED="$BUILD/fused/pokeplatinum"
ROM="${PC_ROM:-$ENGINE/build/rom/pokeplatinum.us.nds}"
SETTLE="$ENGINE/pc/replays/lab-settle.txt"

if [ ! -x "$FUSED" ]; then
    echo "name: SKIP (no fused build: run \`make -C mmo fused\`)"
    exit 0
fi
if [ ! -f "$ROM" ] || [ ! -f "$SETTLE" ]; then
    echo "name: SKIP (no ROM or input script under $ENGINE)"
    exit 0
fi

fail=0
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

ok()  { echo "  ok   $1"; }
bad() { echo "  FAIL $1"; fail=1; }

# The lab's own `name` verb clamps to seven characters before the engine sees
# it, so the wide name is configured instead and lands through the engine's own
# TrainerInfo_SetName at the moment the recipe mints the save.
printf 'name SHORT\n' > "$tmp/min.lab"

# Sixteen glyphs, eleven of them outside ASCII. Every one of them has a glyph in
# the ROM font, that is the point of the generated src/charcode_glyphs.gen.h, 
# so this also exercises the accented set end to end.
WIDE='Ünïcödé Wandérer'

# A save per boot, never a shared one. A second boot onto an existing save
# reaches the game by the continue path and leaves by a route that does not run
# the port's atexit reports, so the run would look silent rather than clean.
boot() {
    which=$1
    shift
    env OPENMMO_NAME_REPORT=1 OPENMMO_FONT_REPORT=1 OPENMMO_FONT=fatal \
        OPENMMO_ASSERT=warn "$@" \
        PC_ROM="$ROM" PC_SAVE="$tmp/$which.sav" PC_LAB="$tmp/min.lab" \
        PC_LAB_AT=1800 PC_FRAMES=2600 PC_PACE=0 PC_INPUT="$SETTLE" \
        "$FUSED"
}

echo "a name wider than the engine's name field:"

rc=0
boot wide OPENMMO_PLAYER_NAME="$WIDE" > "$tmp/wide.log" 2>&1 || rc=$?
if [ "$rc" -ne 0 ]; then
    bad "the game boots with a sixteen-glyph name (exit $rc)"
    sed -n '$p' "$tmp/wide.log"
    exit 1
fi

line=$(grep 'openmmo: trainer name' "$tmp/wide.log" | tail -1)
case "$line" in
    *"source 16, field 7, engine string 16"*)
        ok "sixteen glyphs go in, seven stay in the field, sixteen come back out" ;;
    *)
        bad "sixteen glyphs go in, seven stay in the field, sixteen come back out ($line)" ;;
esac

if grep -q 'openmmo: player name .*, 16 glyphs$' "$tmp/wide.log"; then
    ok "every glyph in an accented name is one the ROM font can draw"
else
    bad "every glyph in an accented name is one the ROM font can draw ($(grep 'player name' "$tmp/wide.log" | tail -1))"
fi

rc=0
boot short > "$tmp/short.log" 2>&1 || rc=$?
if [ "$rc" -ne 0 ]; then
    bad "the game boots with no name configured (exit $rc)"
    sed -n '$p' "$tmp/short.log"
    exit 1
fi

line=$(grep 'openmmo: trainer name' "$tmp/short.log" | tail -1)
case "$line" in
    *"source 5, field 5, engine string 5"*)
        ok "a name that already fits is stored and returned exactly as before" ;;
    *)
        bad "a name that already fits is stored and returned exactly as before ($line)" ;;
esac

# The glyph hook. OPENMMO_FONT=fatal was set on both boots above, so reaching
# here at all means nothing was substituted; the count says the hook was on the
# path while that was true.
drawn=$(grep 'openmmo: font .* glyphs drawn' "$tmp/short.log" | tail -1 \
        | sed 's/.*font \([0-9]*\) glyphs drawn.*/\1/')
subs=$(grep 'openmmo: font .* glyphs drawn' "$tmp/short.log" | tail -1 \
        | sed 's/.*drawn, \([0-9]*\) substitutions.*/\1/')
if [ -n "$drawn" ] && [ "$drawn" -gt 0 ] && [ "$subs" = "0" ]; then
    ok "the engine drew $drawn glyphs and none needed a substitute"
else
    bad "the engine drew $drawn glyphs and none needed a substitute ($subs substituted)"
fi

# The font's own ceiling, so a ROM whose font is smaller than the character map
# is reported here rather than as mysterious question marks. The half-width band
# runs to charcode 0x01EA = 490, so anything below that cannot draw the table.
for n in $(grep 'openmmo: font glyphs' "$tmp/short.log" | sed 's/.*glyphs //'); do
    if [ "$n" -lt 490 ]; then
        bad "every font can draw the whole half-width band (one has $n glyphs, needs 490)"
        continue
    fi
    ok "the font can draw the whole half-width band ($n glyphs, needs 490)"
done

if [ "$fail" -eq 0 ]; then
    echo "name: all checks passed"
else
    echo "name: FAILED"
fi
exit "$fail"
