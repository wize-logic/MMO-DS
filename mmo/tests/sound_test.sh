#!/bin/sh
# The engine's sequence mixer, with music and sfx scales.
set -eu

ROOT=${1:?usage: sound_test.sh <mmo-root> [build-dir] [engine-dir]}
BUILD=${2:-}
ENGINE=${3:-}

LIMITS="$ROOT/ENGINE_LIMITS.md"
MIXER="$ROOT/mods/openmmo/src/openmmo_mixer.c"
PLAN="$ROOT/launcher/launch_plan.c"
PATCH="$ROOT/mods/openmmo/patches/src/sound.c.patch"

for f in "$LIMITS" "$MIXER" "$PLAN" "$PATCH"; do
    if [ ! -f "$f" ]; then
        echo "sound: SKIP (no $f)"
        exit 0
    fi
done

fail=0
ok()  { echo "  ok   $1"; }
bad() { echo "  FAIL $1"; echo "       $2"; fail=1; }

echo "sound stays on the engine's sequence mixer:"

flat=$(tr '\n' ' ' < "$LIMITS" | tr -s ' ')
says() {
    if printf '%s\n' "$flat" | grep -Fq "$2"; then
        ok "$1"
    else
        bad "$1" "ENGINE_LIMITS.md does not say: $2"
    fi
}

says "the page still keeps the engine mixer" "engine's sequence mixer"
says "the page still names sixteen SPU channels" "Sixteen SPU channels"
says "the page still names eight sequence players" "eight sequence players"
says "the page still files cries as two SPU channels" "channels 14 and 15"
says "the page still files pan as 0-127" "pan 0, 127"
says "the page still refuses a second synthesizer" "Do not add a second synthesizer"
says "the page still refuses 3D position" "Do not invent 3D position"
says "the page still files battle as a pause" "battle pauses field BGM"
says "the page still files a map change as a replace" "map change replaces"
says "the page still files the player mixer as 0-100" "music and sfx 0, 100"

if grep -Fq 'openmmo_mix_player_volume(playerID, volume);' "$PATCH"; then
    ok "Sound_SetPlayerVolume still goes through the mixer"
else
    bad "Sound_SetPlayerVolume still goes through the mixer" \
        "$PATCH no longer calls openmmo_mix_player_volume"
fi

if grep -Fq 'OPENMMO_MUSIC' "$MIXER" && grep -Fq 'OPENMMO_SFX' "$MIXER" \
   && grep -Fq 'PLAYER_FIELD' "$MIXER" && grep -Fq 'PLAYER_SE_1' "$MIXER"; then
    ok "the mixer still scales FIELD/BGM/ME apart from SE/PV"
else
    bad "the mixer still scales FIELD/BGM/ME apart from SE/PV" \
        "$MIXER no longer names both buses"
fi

if grep -Fq 'strcmp(key, "music")' "$PLAN" && grep -Fq 'strcmp(key, "sfx")' "$PLAN"; then
    ok "launcher.cfg still accepts music and sfx"
else
    bad "launcher.cfg still accepts music and sfx" \
        "$PLAN no longer parses music and sfx"
fi

# A second synthesizer would have to live in the window or the mod.
# The ring reader is the host surface; it is not a player.
if grep -Eiq 'vorbis|libogg|\.ogg|openal|sseqj|agbplay' \
        "$ROOT/viewer"/*.c "$ROOT/mods/openmmo/src"/*.c 2>/dev/null; then
    bad "there is still no second synthesizer" \
        "viewer or mod sources name a foreign player"
else
    ok "there is still no second synthesizer"
fi

if [ -z "$ENGINE" ] || [ ! -d "$ENGINE" ]; then
    echo "sound: SKIP engine-source half (no checkout)"
else
    echo "ENGINE_DIR=$ENGINE"
    SPU="$ENGINE/pc/include/pc_spu.h"
    SND="$ENGINE/src/sound.c"
    MAP="$ENGINE/src/field_map_change.c"
    TRN="$ENGINE/src/field_transition.c"
    VOL="$ENGINE/include/constants/sound_volume.h"
    NAIX="$ENGINE/build/pc/geninclude/sound/pl_sound_data.naix"
    if [ ! -f "$SPU" ] || [ ! -f "$SND" ] || [ ! -f "$MAP" ] \
       || [ ! -f "$TRN" ] || [ ! -f "$VOL" ]; then
        echo "sound: SKIP engine-source half (hollow checkout)"
    else
        if grep -Fq '#define PC_SPU_CHANNELS  16' "$SPU"; then
            ok "the SPU is still 16 channels"
        else
            bad "the SPU is still 16 channels" \
                "$SPU no longer defines PC_SPU_CHANNELS 16"
        fi
        if grep -Fq '#define SOUND_VOLUME_MAX 127' "$VOL"; then
            ok "sequence volume is still 0-127"
        else
            bad "sequence volume is still 0-127" \
                "$VOL no longer defines SOUND_VOLUME_MAX 127"
        fi
        if grep -Fq 'WAVE_OUT_PAN_LEFT   0' "$ENGINE/include/sound.h" \
           && grep -Fq 'WAVE_OUT_PAN_RIGHT  127' "$ENGINE/include/sound.h"; then
            ok "sequence pan is still 0-127"
        else
            bad "sequence pan is still 0-127" \
                "$ENGINE/include/sound.h no longer names WAVE_OUT_PAN 0 and 127"
        fi
        if grep -Fq 'Sound_SetBGMPlayerPaused(PLAYER_FIELD, TRUE);' "$SND"; then
            ok "a battle still pauses field BGM"
        else
            bad "a battle still pauses field BGM" \
                "$SND no longer pauses PLAYER_FIELD"
        fi
        if grep -Fq 'Sound_FadeOutBGM(0, 30);' "$MAP"; then
            ok "a map change still fades field BGM"
        else
            bad "a map change still fades field BGM" \
                "$MAP no longer fades BGM"
        fi
        if grep -Fq 'Sound_SetSceneAndPlayBGM(SOUND_SCENE_BATTLE' "$TRN"; then
            ok "a battle still starts as SOUND_SCENE_BATTLE"
        else
            bad "a battle still starts as SOUND_SCENE_BATTLE" \
                "$TRN no longer plays battle BGM"
        fi
        if [ -f "$NAIX" ]; then
            if grep -Fq '#define PLAYER_PV 0' "$NAIX" \
               && grep -Fq '#define PLAYER_FIELD 1' "$NAIX" \
               && grep -Fq '#define PLAYER_ME 2' "$NAIX" \
               && grep -Fq '#define PLAYER_SE_1 3' "$NAIX" \
               && grep -Fq '#define PLAYER_BGM 7' "$NAIX"; then
                ok "the eight sequence players are still the archive's"
            else
                bad "the eight sequence players are still the archive's" \
                    "$NAIX no longer numbers PV/FIELD/ME/SE/BGM"
            fi
        else
            echo "sound: SKIP player-id half (no generated naix)"
        fi
    fi
fi

FUSED=
if [ -n "$BUILD" ] && [ -x "$BUILD/fused/pokeplatinum" ]; then
    FUSED="$BUILD/fused/pokeplatinum"
fi
# $PC_ROM first and the engine's build second, which is the order name_test
# and sprite_oracle_test read them in.
ROM="${PC_ROM:-${ENGINE:+$ENGINE/build/rom/pokeplatinum.us.nds}}"
[ -n "$ROM" ] && [ -f "$ROM" ] || ROM=

echo "the audio lab still pins title against silence:"

if [ -z "$FUSED" ] || [ -z "$ROM" ]; then
    echo "sound: SKIP live half (no fused build or ROM)"
else
    tmp=$(mktemp -d)
    trap 'rm -rf "$tmp"' EXIT
    env -u ENGINE_DIR -u OPENMMO_MUSIC -u OPENMMO_SFX \
        OPENMMO_ASSERT=warn OPENMMO_HUD=0 \
        PC_ROM="$ROM" PC_SAVE=none PC_PACE=0 PC_FRAMES=5 \
        PC_LAB_AUDIO=pin \
        "$FUSED" >"$tmp/pin.out" 2>"$tmp/pin.err" && prc=0 || prc=$?
    if [ "$prc" -ne 0 ]; then
        bad "the audio lab pin still returns (exit $prc)" \
            "$(tail -n 1 "$tmp/pin.err")"
    elif ! grep -Fq 'pc_lab: audio archive seq_count=2133 named=1013' "$tmp/pin.err"; then
        bad "the archive still names 1013 sequences" \
            "$(grep 'audio archive' "$tmp/pin.err" || echo none)"
    elif ! grep -Fq 'pc_lab: audio pin seq=1172 fnv=065ddcf69f6fc88d peak=1570' "$tmp/pin.err"; then
        bad "title still hashes the audio-lab pin" \
            "$(grep 'pin seq=1172' "$tmp/pin.err" || echo none)"
    elif ! grep -Fq 'pc_lab: audio pin seq=1001 fnv=61678e1cf6f040d5 peak=0' "$tmp/pin.err"; then
        bad "field silence is still silent" \
            "$(grep 'pin seq=1001' "$tmp/pin.err" || echo none)"
    else
        ok "title still hashes 065ddcf69f6fc88d and silence is still peak 0"
        ok "the live archive still names 1013 sequences"
    fi

    echo "music=0 silences BGM and leaves an effect:"
    env -u ENGINE_DIR -u OPENMMO_SFX OPENMMO_MUSIC=0 \
        OPENMMO_ASSERT=warn OPENMMO_HUD=0 \
        PC_ROM="$ROM" PC_SAVE=none PC_PACE=0 PC_FRAMES=5 \
        PC_LAB_AUDIO=1172,1505 \
        "$FUSED" >"$tmp/m0.out" 2>"$tmp/m0.err" && mrc=0 || mrc=$?
    if [ "$mrc" -ne 0 ]; then
        bad "music=0 still returns (exit $mrc)" \
            "$(tail -n 1 "$tmp/m0.err")"
    else
        tline=$(grep 'pc_lab: audio seq=1172 ' "$tmp/m0.err" || true)
        sline=$(grep 'pc_lab: audio seq=1505 ' "$tmp/m0.err" || true)
        tpeak=$(printf '%s\n' "$tline" | sed -n 's/.*peak=\([0-9-]*\).*/\1/p')
        speak=$(printf '%s\n' "$sline" | sed -n 's/.*peak=\([0-9-]*\).*/\1/p')
        if [ -n "$tpeak" ] && [ "$tpeak" -le 8 ]; then
            ok "title at music=0 is still silent (peak $tpeak)"
        else
            bad "title at music=0 is still silent" \
                "seq=1172 peak=${tpeak:-missing}"
        fi
        if [ -n "$speak" ] && [ "$speak" -ge 100 ]; then
            ok "an effect at music=0 is still audible (peak $speak)"
        else
            bad "an effect at music=0 is still audible" \
                "seq=1505 peak=${speak:-missing}"
        fi
    fi

    echo "sfx=0 silences an effect and leaves BGM:"
    env -u ENGINE_DIR -u OPENMMO_MUSIC OPENMMO_SFX=0 \
        OPENMMO_ASSERT=warn OPENMMO_HUD=0 \
        PC_ROM="$ROM" PC_SAVE=none PC_PACE=0 PC_FRAMES=5 \
        PC_LAB_AUDIO=1172,1505 \
        "$FUSED" >"$tmp/s0.out" 2>"$tmp/s0.err" && src=0 || src=$?
    if [ "$src" -ne 0 ]; then
        bad "sfx=0 still returns (exit $src)" \
            "$(tail -n 1 "$tmp/s0.err")"
    else
        tline=$(grep 'pc_lab: audio seq=1172 ' "$tmp/s0.err" || true)
        sline=$(grep 'pc_lab: audio seq=1505 ' "$tmp/s0.err" || true)
        tpeak=$(printf '%s\n' "$tline" | sed -n 's/.*peak=\([0-9-]*\).*/\1/p')
        speak=$(printf '%s\n' "$sline" | sed -n 's/.*peak=\([0-9-]*\).*/\1/p')
        if [ -n "$tpeak" ] && [ "$tpeak" -ge 100 ]; then
            ok "title at sfx=0 is still audible (peak $tpeak)"
        else
            bad "title at sfx=0 is still audible" \
                "seq=1172 peak=${tpeak:-missing}"
        fi
        # A keyed-off ADPCM channel can leave a few counts of click.
        # Full scale on this sequence is ~20 000; 64 is 0.2 %.
        if [ -n "$speak" ] && [ "$speak" -le 64 ]; then
            ok "an effect at sfx=0 is still silent (peak $speak)"
        else
            bad "an effect at sfx=0 is still silent" \
                "seq=1505 peak=${speak:-missing}"
        fi
    fi
    rm -rf "$tmp"
    trap - EXIT
fi

if [ "$fail" -eq 0 ]; then
    echo "sound: all checks passed"
else
    echo "sound: FAILED"
fi
exit "$fail"
