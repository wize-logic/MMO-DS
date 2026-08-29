#!/bin/sh
# Twelve remotes at native are not a slideshow.
set -eu

ROOT=${1:?usage: crowd_perf_test.sh <mmo-root> [build-dir] [engine-dir]}
BUILD=${2:-}
ENGINE=${3:-}

LIMITS="$ROOT/ENGINE_LIMITS.md"

for f in "$LIMITS"; do
    if [ ! -f "$f" ]; then
        echo "crowd-perf: SKIP (no $f)"
        exit 0
    fi
done

fail=0
ok()  { echo "  ok   $1"; }
bad() { echo "  FAIL $1"; echo "       $2"; fail=1; }

echo "twelve remotes at native are not a slideshow:"

flat=$(tr '\n' ' ' < "$LIMITS" | tr -s ' ')
says() {
    if printf '%s\n' "$flat" | grep -Fq "$2"; then
        ok "$1"
    else
        bad "$1" "ENGINE_LIMITS.md does not say: $2"
    fi
}

says "the page still files twelve remotes as not a slideshow" "not a slideshow"
says "the page still names PC_BENCH as the instrument" "PC_BENCH"
says "the page still keeps twelve visible remotes" "Keep **twelve visible remotes**"
says "the page still names HD 1 as the default" "Default HD is 1"
says "the page still files HD outdoor as under 60 empty" \
     "drops below 60 fps with nobody else"

if [ -z "$ENGINE" ] || [ ! -d "$ENGINE" ]; then
    echo "crowd-perf: SKIP engine-source half (no checkout)"
else
    echo "ENGINE_DIR=$ENGINE"
    BENCHC="$ENGINE/pc/src/pc_bench.c"
    BENCHMK="$ENGINE/pc/Makefile"
    if [ ! -f "$BENCHC" ] || [ ! -f "$BENCHMK" ]; then
        echo "crowd-perf: SKIP engine-source half (hollow checkout)"
    else
        if grep -Fq '3D rasterize' "$BENCHC" \
           && grep -Fq '2D compose' "$BENCHC" \
           && grep -Fq 'elsewhere' "$BENCHC"; then
            ok "the engine bench still names 3D, 2D and elsewhere"
        else
            bad "the engine bench still names 3D, 2D and elsewhere" \
                "$BENCHC no longer prints those spans"
        fi
        if grep -Fq 'PC_BENCH=1' "$BENCHMK"; then
            ok "the engine bench target still sets PC_BENCH"
        else
            bad "the engine bench target still sets PC_BENCH" \
                "$BENCHMK no longer sets PC_BENCH=1"
        fi
    fi
fi

FUSED=
ROM=
SETTLE=
if [ -n "$BUILD" ] && [ -x "$BUILD/fused/pokeplatinum" ]; then
    FUSED="$BUILD/fused/pokeplatinum"
fi
if [ -n "$ENGINE" ] && [ -f "$ENGINE/build/rom/pokeplatinum.us.nds" ]; then
    ROM="$ENGINE/build/rom/pokeplatinum.us.nds"
elif [ -n "${PC_ROM:-}" ] && [ -f "${PC_ROM}" ]; then
    ROM=$PC_ROM
fi
if [ -n "$ENGINE" ] && [ -f "$ENGINE/pc/replays/lab-settle.txt" ]; then
    SETTLE="$ENGINE/pc/replays/lab-settle.txt"
fi

hdr=
if [ -n "$ENGINE" ] && [ -f "$ENGINE/generated/map_headers.txt" ]; then
    hdr=$(awk '/^MAP_HEADER_TWINLEAF_TOWN$/{print NR-1; exit}' \
        "$ENGINE/generated/map_headers.txt")
fi
if [ -z "$hdr" ] && [ -n "$ENGINE" ] \
   && [ -f "$ENGINE/build/pc/geninclude/generated/map_headers.h" ]; then
    hdr=$(sed -n 's/^#define MAP_HEADER_TWINLEAF_TOWN[[:space:]]\{1,\}\([0-9]\{1,\}\).*/\1/p' \
        "$ENGINE/build/pc/geninclude/generated/map_headers.h" | head -1)
fi

if [ -z "$FUSED" ] || [ -z "$ROM" ] || [ -z "$SETTLE" ] || [ -z "$hdr" ]; then
    echo "crowd-perf: SKIP live half (no fused build, ROM, settle script or header id)"
    if [ "$fail" -eq 0 ]; then
        echo "crowd-perf: all checks passed"
    else
        echo "crowd-perf: FAILED"
    fi
    exit "$fail"
fi

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

printf 'name CROWD\nmap %s 116 886 1\n' "$hdr" > "$tmp/mint.lab"
printf 'name STAY\n' > "$tmp/stay.lab"

echo "twelve remotes on Twinleaf Town at native stay above 60 fps:"

env -u ENGINE_DIR OPENMMO_ASSERT=warn OPENMMO_HUD=0 \
    PC_ROM="$ROM" PC_SAVE="$tmp/town.sav" PC_PACE=0 \
    PC_FRAMES=2500 PC_LAB="$tmp/mint.lab" PC_LAB_AT=1800 \
    PC_INPUT="$SETTLE" \
    "$FUSED" >"$tmp/mint.log" 2>&1 && mrc=0 || mrc=$?

if [ "$mrc" -ne 0 ] || ! grep -q 'pc_lab: applied' "$tmp/mint.log"; then
    bad "a Twinleaf Town save still mints (exit $mrc)" \
        "$(tail -n 1 "$tmp/mint.log")"
    echo "crowd-perf: FAILED"
    exit 1
fi

cp -f "$tmp/town.sav" "$tmp/bench.sav"
env -u ENGINE_DIR OPENMMO_ASSERT=warn OPENMMO_HUD=0 \
    OPENMMO_FAKE_CROWD=12 \
    PC_ROM="$ROM" PC_SAVE="$tmp/bench.sav" PC_PACE=0 PC_BENCH=1 \
    PC_FRAMES=3000 PC_LAB="$tmp/stay.lab" PC_LAB_AT=99999 \
    "$FUSED" >"$tmp/bench.log" 2>&1 && brc=0 || brc=$?

if [ "$brc" -ne 0 ]; then
    bad "a native crowd of twelve still returns (exit $brc)" \
        "$(tail -n 1 "$tmp/bench.log")"
elif ! grep -q 'crowd of 12 requested at (116,886), 12 live' "$tmp/bench.log"; then
    bad "twelve remotes still stand on Twinleaf Town" \
        "$(grep -E 'crowd of' "$tmp/bench.log" || echo none)"
elif grep -qE 'engine assertion failed|heap is full|texture pool is full' \
        "$tmp/bench.log"; then
    bad "no engine pool runs out with twelve of them on Twinleaf Town" \
        "$(grep -E 'assertion failed|heap is full|texture pool' "$tmp/bench.log")"
else
    ok "twelve remotes still stand on Twinleaf Town, all twelve live"
    line=$(grep '^pc-bench: [0-9]' "$tmp/bench.log" || true)
    fps=$(printf '%s\n' "$line" | sed -n 's/.*-- \([0-9.][0-9.]*\) fps.*/\1/p')
    if [ -n "$fps" ] && awk -v f="$fps" 'BEGIN { exit !(f + 0 >= 60) }'; then
        ok "twelve remotes at native still run at ${fps} fps unpaced"
    else
        bad "twelve remotes at native still run at or above 60 fps" \
            "${line:-no pc-bench line}"
    fi
fi

if [ "$fail" -eq 0 ]; then
    echo "crowd-perf: all checks passed"
else
    echo "crowd-perf: FAILED"
fi
exit "$fail"
