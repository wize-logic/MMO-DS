#!/bin/sh
# The game ticks once per VBlank; present is decoupled.
set -eu

ROOT=${1:?usage: pace_test.sh <mmo-root> [build-dir] [engine-dir]}
BUILD=${2:-}
ENGINE=${3:-}

LIMITS="$ROOT/ENGINE_LIMITS.md"
GEOM="$ROOT/viewer/view_geom.h"
VIEWC="$ROOT/viewer/viewer.c"
CHANH="$ROOT/include/view_channel.h"

for f in "$LIMITS" "$GEOM" "$VIEWC" "$CHANH"; do
    if [ ! -f "$f" ]; then
        echo "pace: SKIP (no $f)"
        exit 0
    fi
done

fail=0
ok()  { echo "  ok   $1"; }
bad() { echo "  FAIL $1"; echo "       $2"; fail=1; }

echo "the game ticks once per VBlank; present is decoupled:"

flat=$(tr '\n' ' ' < "$LIMITS" | tr -s ' ')
says() {
    if printf '%s\n' "$flat" | grep -Fq "$2"; then
        ok "$1"
    else
        bad "$1" "ENGINE_LIMITS.md does not say: $2"
    fi
}

says "the page still keeps the game on VBlank" "game ticks once per VBlank"
says "the page still refuses interpolated presents" "Do not interpolate published frames"
says "the page still files the clock fallback as 60.000 Hz" "clock fallback of **60.000 Hz**"
says "the page still names the engine pacer" "16_666_667 ns"
says "the page still files the pacer as a window-channel sleep" "only then, because"

if grep -Fq 'return (tick % 3u == 0u) ? 16u : 17u;' "$GEOM"; then
    ok "openmmo_view_pace_ms is still 16+17+17"
else
    bad "openmmo_view_pace_ms is still 16+17+17" \
        "$GEOM no longer returns 16 or 17 on tick%3"
fi

if grep -Fq 'next_tick += openmmo_view_pace_ms(tick);' "$VIEWC"; then
    ok "the window still clock-paces through openmmo_view_pace_ms"
else
    bad "the window still clock-paces through openmmo_view_pace_ms" \
        "$VIEWC no longer advances next_tick by openmmo_view_pace_ms"
fi

if grep -Fq 'samples them once a frame' "$CHANH"; then
    ok "the page still samples live input once a frame"
else
    bad "the page still samples live input once a frame" \
        "$CHANH no longer says the game samples input once a frame"
fi

# A published-frame interpolator would have to hold the previous picture
# and blend it. Repeating the last frame is the decoupled present.
if grep -Eiq 'interpolat' "$VIEWC" "$GEOM"; then
    bad "the window still has no published-frame interpolator" \
        "viewer sources name interpolation"
else
    ok "the window still has no published-frame interpolator"
fi

if [ -z "$ENGINE" ] || [ ! -d "$ENGINE" ]; then
    echo "pace: SKIP engine-source half (no checkout)"
else
    echo "ENGINE_DIR=$ENGINE"
    PACER="$ENGINE/pc/src/pc_view.c"
    OS="$ENGINE/pc/src/pc_os_lite.c"
    if [ ! -f "$PACER" ] || [ ! -f "$OS" ]; then
        echo "pace: SKIP engine-source half (hollow checkout)"
    else
        if grep -Fq 'next.tv_nsec += 16666667L;      /* 1/60 s */' "$PACER"; then
            ok "the engine pacer is still 16_666_667 ns (1/60 s)"
        else
            bad "the engine pacer is still 16_666_667 ns (1/60 s)" \
                "$PACER no longer sleeps 16666667 ns"
        fi
        if grep -Fq 'pc_audio_advance(560190u);' "$OS" \
           && grep -Fq '59.8261 frames a second' "$OS"; then
            ok "guest audio is still 560190 cycles at 59.8261 Hz"
        else
            bad "guest audio is still 560190 cycles at 59.8261 Hz" \
                "$OS no longer advances 560190 cycles at 59.8261 Hz"
        fi
    fi
fi

FUSED=
ROM=
VIEW=
DRIVE=
if [ -n "$BUILD" ] && [ -x "$BUILD/fused/pokeplatinum" ]; then
    FUSED="$BUILD/fused/pokeplatinum"
fi
if [ -n "$ENGINE" ] && [ -f "$ENGINE/build/rom/pokeplatinum.us.nds" ]; then
    ROM="$ENGINE/build/rom/pokeplatinum.us.nds"
elif [ -n "${PC_ROM:-}" ] && [ -f "${PC_ROM}" ]; then
    ROM=$PC_ROM
fi
if [ -n "$BUILD" ] && [ -x "$BUILD/openmmo-view" ]; then
    VIEW="$BUILD/openmmo-view"
fi
if [ -n "$BUILD" ] && [ -x "$BUILD/openmmo-viewdrive" ]; then
    DRIVE="$BUILD/openmmo-viewdrive"
fi

echo "a paced fused boot is held to 60 Hz:"

if [ -z "$FUSED" ] || [ -z "$ROM" ]; then
    echo "pace: SKIP fused half (no fused build or ROM)"
else
    tmp=$(mktemp -d)
    VIEWP="om-pace-p-$$"
    VIEWU="om-pace-u-$$"
    trap 'rm -rf "$tmp"; rm -f /dev/shm/$VIEWP /dev/shm/$VIEWU' EXIT
    # The pacer lives in pc_view_publish and does not run without a
    # channel. 120 VBlanks is two seconds of that sleep; unpaced on the
    # same channel is the title-screen work alone.
    start=$(date +%s%N)
    env OPENMMO_ASSERT=warn OPENMMO_HUD=0 \
        PC_ROM="$ROM" PC_SAVE=none PC_PACE=1 PC_FRAMES=120 \
        PC_VIEW="$VIEWP" \
        "$FUSED" >"$tmp/paced.log" 2>&1 && prc=0 || prc=$?
    paced_ns=$(( $(date +%s%N) - start ))
    start=$(date +%s%N)
    env OPENMMO_ASSERT=warn OPENMMO_HUD=0 \
        PC_ROM="$ROM" PC_SAVE=none PC_PACE=0 PC_FRAMES=120 \
        PC_VIEW="$VIEWU" \
        "$FUSED" >"$tmp/unpaced.log" 2>&1 && urc=0 || urc=$?
    unpaced_ns=$(( $(date +%s%N) - start ))
    if [ "$prc" -ne 0 ]; then
        bad "a paced 120-frame boot still returns (exit $prc)" \
            "$(tail -n 1 "$tmp/paced.log")"
    elif [ "$urc" -ne 0 ]; then
        bad "an unpaced 120-frame boot still returns (exit $urc)" \
            "$(tail -n 1 "$tmp/unpaced.log")"
    else
        paced_ms=$(( paced_ns / 1000000 ))
        unpaced_ms=$(( unpaced_ns / 1000000 ))
        echo "  paced ${paced_ms} ms, unpaced ${unpaced_ms} ms"
        # 120 frames at 60 Hz is 2000 ms of sleep plus boot work. Allow
        # 200 ms of clock granularity and scheduling. Unpaced has to
        # beat that, or the pacer is not what is holding the run.
        if [ "$paced_ms" -ge 1800 ]; then
            ok "120 paced VBlanks still take at least 1.8 s"
        else
            bad "120 paced VBlanks still take at least 1.8 s" \
                "paced boot was ${paced_ms} ms"
        fi
        if [ "$unpaced_ms" -lt "$paced_ms" ]; then
            ok "the same 120 VBlanks still finish faster with the pacer off"
        else
            bad "the same 120 VBlanks still finish faster with the pacer off" \
                "unpaced ${unpaced_ms} ms was not under paced ${paced_ms} ms"
        fi
    fi
    rm -rf "$tmp"
    trap - EXIT
fi

echo "a dummy window clock-paces near 60 Hz:"

if [ -z "$VIEW" ] || [ -z "$DRIVE" ]; then
    echo "pace: SKIP window half (no openmmo-view or view-drive)"
else
    tmp=$(mktemp -d)
    CHAN="om-pace-$$"
    trap 'rm -rf "$tmp"; "$DRIVE" unlink "$CHAN" >/dev/null 2>&1 || true' EXIT
    "$DRIVE" publish "$CHAN" >/dev/null
    export SDL_VIDEODRIVER=dummy
    export OPENMMO_VIEW_PACE_REPORT=1
    # Three seconds of presents, then drop the page so the window notices
    # the game ended (it checks every ~2 s) and prints the report.
    "$VIEW" "$CHAN" --no-audio --wait 2000 \
        >"$tmp/out" 2>"$tmp/err" &
    vpid=$!
    sleep 3
    "$DRIVE" unlink "$CHAN" >/dev/null 2>&1 || true
    waited=0
    while kill -0 "$vpid" 2>/dev/null; do
        slept=1
        sleep 1
        waited=$((waited + 1))
        if [ "$waited" -ge 8 ]; then
            kill "$vpid" 2>/dev/null || true
            break
        fi
    done
    wait "$vpid" 2>/dev/null || true
    if out=$(python3 - "$tmp/err" <<'PY'
import re, sys
text = open(sys.argv[1], encoding="utf-8", errors="replace").read()
if "paced by the clock" not in text:
    print("no-clock-pace")
    sys.exit(1)
m = re.search(r"presented (\d+) in (\d+) ms \(clock\)", text)
if not m:
    print("no-report")
    sys.exit(1)
n, dt = int(m.group(1)), int(m.group(2))
if dt < 500 or n < 20:
    print("too-short n=%d dt=%d" % (n, dt))
    sys.exit(1)
hz = 1000.0 * n / dt
print("ok n=%d dt=%d hz=%.2f" % (n, dt, hz))
# 55..65 covers scheduling around 60.000 without accepting the old 61.22
# as a different policy. Dummy present is a few milliseconds; the delay
# is the remainder of 16 or 17.
if hz < 55.0 or hz > 65.0:
    print("hz-out %.2f" % hz)
    sys.exit(1)
sys.exit(0)
PY
    ); then
        ok "a dummy window still clock-paces near 60 Hz ($out)"
    else
        bad "a dummy window still clock-paces near 60 Hz" "$out"
        sed 's/^/       /' "$tmp/err" | tail -n 8
    fi
    rm -rf "$tmp"
    trap - EXIT
fi

if [ "$fail" -eq 0 ]; then
    echo "pace: all checks passed"
else
    echo "pace: FAILED"
fi
exit "$fail"
