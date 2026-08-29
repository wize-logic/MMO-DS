#!/bin/sh
# The composed soundtracks, static and live.
set -eu

ROOT=${1:?usage: soundtrack_test.sh <mmo-root> [build-dir] [engine-dir]}
BUILD=${2:-}
ENGINE=${3:-}

MAP="$ROOT/SOUNDTRACKS"
FONTMAP="$ROOT/SOUNDFONTS"
TOOL="$ROOT/tools/soundtrack.py"
FONTTOOL="$ROOT/tools/soundfont.py"
PLAN="$ROOT/launcher/launch_plan.c"
HPATCH="$ROOT/mods/openmmo/patches/include/sound_system.h.patch"
PPATCH="$ROOT/mods/openmmo/patches/src/sound_playback.c.patch"
SPATCH="$ROOT/mods/openmmo/patches/src/sound.c.patch"
SE_PIN='pc_lab: audio seq=1350 player=SE1 kind=se samples=32823 peak=24184 voices_left=0 fnv=12d0485e940eff1d'

fail=0
ok()  { echo "  ok   $1"; }
bad() { echo "  FAIL $1"; echo "       $2"; fail=1; }
tmpgen=$(mktemp)
trap 'rm -f "$tmpgen"' EXIT

echo "the soundtrack switch holds together:"

for f in "$MAP" "$FONTMAP" "$TOOL" "$FONTTOOL" "$PLAN" "$HPATCH" "$PPATCH" "$SPATCH"; do
    if [ ! -f "$f" ]; then
        echo "soundtrack: SKIP (no $f)"
        exit 0
    fi
done

rows=$(grep -c '^SEQ_' "$MAP" || true)
if [ "$rows" -ge 100 ]; then
    ok "the map still holds a soundtrack ($rows rows)"
else
    bad "the map still holds a soundtrack" "$MAP has $rows rows"
fi
dup=$(awk '/^SEQ_/ { print $1 }' "$MAP" | sort | uniq -d | head -1)
if [ -z "$dup" ]; then
    ok "no track is mapped twice"
else
    bad "no track is mapped twice" "$dup has two rows"
fi
if grep -q 'heartgold=SEQ_GS_' "$MAP" && grep -q 'blackwhite=SEQ_BGM_' "$MAP"; then
    ok "both slots are composed from their own game's names"
else
    bad "both slots are composed from their own game's names" \
        "a heartgold cell without SEQ_GS_ or a blackwhite cell without SEQ_BGM_"
fi
fontrows=$(grep -c '^[0-9]' "$FONTMAP" || true)
if [ "$fontrows" -ge 60 ] && grep -q 'heartgold=' "$FONTMAP" \
   && grep -q 'blackwhite=.*@' "$FONTMAP"; then
    ok "the font map still carries both games ($fontrows programs)"
else
    bad "the font map still carries both games" \
        "$FONTMAP has $fontrows rows, or a slot column is gone"
fi

if grep -Fq 'strcmp(key, "soundtrack")' "$PLAN" \
   && grep -Fq 'strcmp(key, "soundfont")' "$PLAN" \
   && grep -Fq '"sound_%s_%s"' "$PLAN"; then
    ok "the launcher still takes both sound keys"
else
    bad "the launcher still takes both sound keys" \
        "$PLAN no longer parses soundtrack/soundfont or builds pair names"
fi
npkg=0
for d in "$ROOT"/mods/sound_*_*/; do
    [ -d "$d" ] || continue
    pkg=$(basename "$d")
    npkg=$((npkg + 1))
    grep -Fq "id = \"$pkg\"" "$d/mod.toml" 2>/dev/null || {
        bad "every sound package is a package" "$d has no matching mod.toml id"
    }
done
if [ "$npkg" -eq 8 ]; then
    ok "the eight soundtrack/soundfont pairs are all packages"
else
    bad "the eight soundtrack/soundfont pairs are all packages" \
        "found $npkg of 8 sound_*_* directories"
fi

if [ -f "$ROOT/tools/gen_soundtables.py" ] \
   && python3 "$ROOT/tools/gen_soundtables.py" "$ROOT" "$tmpgen" >/dev/null 2>&1 \
   && cmp -s "$tmpgen" "$ROOT/src/soundtables.gen.h"; then
    ok "the compiled sound tables match the two maps"
else
    bad "the compiled sound tables match the two maps" \
        "regenerate: python3 mmo/tools/gen_soundtables.py"
fi
if grep -Fq 'soundcompose.c' "$ROOT/Makefile" \
   && grep -Fq 'mmo_soundcompose_ensure' "$ROOT/launcher/launcher.c"; then
    ok "the launcher still composes a player's pair at Play"
else
    bad "the launcher still composes a player's pair at Play" \
        "soundcompose is not built in, or start_play no longer asks it"
fi
if grep -Fq '0x1EBC00' "$HPATCH"; then
    ok "the sound heap patch still adds the soundtrack's room"
else
    bad "the sound heap patch still adds the soundtrack's room" \
        "$HPATCH no longer sizes the heap 0x1EBC00"
fi
if grep -Fq 'Sound_GetBankIDFromSequenceID(seqID)' "$PPATCH"; then
    ok "a field track still starts through its own bank"
else
    bad "a field track still starts through its own bank" \
        "$PPATCH no longer names the track's bank"
fi
if grep -Fq 'NNS_SND_ARC_LOAD_WAVE | NNS_SND_ARC_LOAD_BANK' "$SPATCH"; then
    ok "a switched-to track still brings its bank along"
else
    bad "a switched-to track still brings its bank along" \
        "$SPATCH no longer loads the bank with the waves"
fi

FUSED=
ROM=
if [ -n "$BUILD" ] && [ -x "$BUILD/fused/pokeplatinum" ]; then
    FUSED="$BUILD/fused/pokeplatinum"
fi
if [ -n "$ENGINE" ] && [ -f "$ENGINE/build/rom/pokeplatinum.us.nds" ]; then
    ROM="$ENGINE/build/rom/pokeplatinum.us.nds"
fi
if [ -z "$FUSED" ] || [ -z "$ROM" ]; then
    echo "soundtrack: SKIP live half (no fused build or ROM)"
    [ "$fail" -eq 0 ] && echo "soundtrack: all checks passed"
    exit "$fail"
fi

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

for d in "$ROOT"/mods/sound_*_*/; do
    pkg=$(basename "$d")
    SDAT="$d/replace/data/sound/pl_sound_data.sdat"
    IDS="$d/music.txt"
    REPL="$d/replaced.txt"
    if [ ! -f "$SDAT" ] || [ ! -f "$IDS" ] || [ ! -f "$REPL" ]; then
        echo "soundtrack: SKIP $pkg (not composed here; make -C mmo soundtrack)"
        continue
    fi
    echo "every $pkg track plays through the lab:"
    total=$(wc -l < "$IDS")
    batches=$(awk 'NR % 60 == 1 { n++ } END { print n }' "$IDS")
    b=1
    while [ "$b" -le "$batches" ] && [ "$b" -le 9 ]; do
        list=$(awk -v b="$b" 'int((NR - 1) / 60) + 1 == b { printf "%s%s", sep, $1; sep="," }' sep= "$IDS")
        env -u ENGINE_DIR OPENMMO_ASSERT=warn OPENMMO_HUD=0 \
            PC_ROM="$ROM" PC_SAVE=none PC_PACE=0 PC_FRAMES=5 \
            PC_MODS_DIR="$ROOT/mods" PC_MODS="$pkg" \
            PC_LAB_AUDIO="1350,$list" \
            "$FUSED" >"$tmp/$pkg.$b.out" 2>"$tmp/$pkg.$b.err" && rc=0 || rc=$?
        done_line=$(grep 'pc_lab: audio done' "$tmp/$pkg.$b.err" || echo none)
        if [ "$rc" -ne 0 ]; then
            bad "$pkg batch $b returns" "exit $rc: $(tail -n 1 "$tmp/$pkg.$b.err")"
        elif ! printf '%s\n' "$done_line" | grep -q 'failed=0'; then
            bad "$pkg batch $b plays clean" "$done_line"
        elif ! printf '%s\n' "$done_line" | grep -q ' silent=0 '; then
            bad "$pkg batch $b leaves nothing silent" "$done_line"
        elif ! grep -Fq "$SE_PIN" "$tmp/$pkg.$b.err"; then
            bad "$pkg batch $b keeps the sound effects byte-stable" \
                "$(grep 'audio seq=1350 ' "$tmp/$pkg.$b.err" || echo 'no pin line')"
        else
            ok "$pkg batch $b/$batches plays, releases, and holds the SE pin"
        fi
        b=$((b + 1))
    done
    ok "$pkg covered its $total musical sequences ($(wc -l < "$REPL") replaced)"
done

# The two compose implementations, held together: the launcher's C engine
# composes one cross pair (font pass, track pass and refont all exercised)
# and every musical sequence must render to the same mixer hash as the
# python-composed package. Sources fall back to the decomp trees the other
# import tests already read; SKIPs when a piece is missing.
LAUNCH="$BUILD/openmmo-launch"
PT_SRC="${OPENMMO_SOUND_PT:-$ENGINE/build/rom/res/sound/pl_sound_data.sdat}"
HG_SRC="${OPENMMO_SOUND_HG:-$ENGINE/../pokeheartgold/files/data/sound/gs_sound_data.sdat}"
BW_SRC="${OPENMMO_SOUND_BW:-$ENGINE/../pokeblack/baserom.nds}"
PYPKG="$ROOT/mods/sound_hg_bw/replace/data/sound/pl_sound_data.sdat"
if [ ! -x "$LAUNCH" ] || [ ! -f "$PT_SRC" ] || [ ! -f "$HG_SRC" ] \
   || [ ! -f "$BW_SRC" ] || [ ! -f "$PYPKG" ] || [ -z "$FUSED" ]; then
    echo "soundtrack: SKIP the two-implementation differential (launcher,"
    echo "            a source cartridge, or the python package is missing)"
else
    echo "the launcher's compose matches the python pipeline:"
    ctmp=$(mktemp -d)
    mkdir -p "$ctmp/mods/cdiff/replace/data/sound"
    printf 'id = "cdiff"\nname = "x"\nversion = "1.0.0"\nauthors = ["x"]\nrequires = []\nload_after = []\n' \
        > "$ctmp/mods/cdiff/mod.toml"
    if ! "$LAUNCH" --compose-pair 1 2 "$PT_SRC" "$HG_SRC" "$BW_SRC" \
            "$ctmp/mods/cdiff/replace/data/sound/pl_sound_data.sdat" \
            >"$ctmp/compose.log" 2>&1; then
        bad "the C compose runs" "$(tail -1 "$ctmp/compose.log")"
    else
        b=1
        : > "$ctmp/c.fnv"; : > "$ctmp/py.fnv"
        while [ "$b" -le 4 ]; do
            list=$(awk -v b="$b" 'int((NR - 1) / 60) + 1 == b { printf "%s%s", sep, $1; sep="," }' sep= "$ROOT/mods/sound_hg_bw/music.txt")
            [ -z "$list" ] && break
            env -u ENGINE_DIR OPENMMO_ASSERT=warn OPENMMO_HUD=0 \
                PC_ROM="$ROM" PC_SAVE=none PC_PACE=0 PC_FRAMES=5 \
                PC_MODS_DIR="$ctmp/mods" PC_MODS=cdiff \
                PC_LAB_AUDIO="$list" "$FUSED" >/dev/null 2>>"$ctmp/c.err"
            env -u ENGINE_DIR OPENMMO_ASSERT=warn OPENMMO_HUD=0 \
                PC_ROM="$ROM" PC_SAVE=none PC_PACE=0 PC_FRAMES=5 \
                PC_MODS_DIR="$ROOT/mods" PC_MODS=sound_hg_bw \
                PC_LAB_AUDIO="$list" "$FUSED" >/dev/null 2>>"$ctmp/py.err"
            b=$((b + 1))
        done
        sed -n 's/.*audio seq=\([0-9]*\) .*fnv=\(.*\)/\1 \2/p' "$ctmp/c.err" | sort -n > "$ctmp/c.fnv"
        sed -n 's/.*audio seq=\([0-9]*\) .*fnv=\(.*\)/\1 \2/p' "$ctmp/py.err" | sort -n > "$ctmp/py.fnv"
        nseq=$(wc -l < "$ctmp/c.fnv")
        if [ "$nseq" -ge 200 ] && cmp -s "$ctmp/c.fnv" "$ctmp/py.fnv"; then
            ok "every one of $nseq sequences renders identically both ways"
        else
            bad "every sequence renders identically both ways" \
                "$(diff "$ctmp/c.fnv" "$ctmp/py.fnv" | head -3)"
        fi
    fi
    rm -rf "$ctmp"
fi

[ "$fail" -eq 0 ] && echo "soundtrack: all checks passed"
exit "$fail"
