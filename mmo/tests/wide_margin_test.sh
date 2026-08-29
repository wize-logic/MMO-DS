#!/bin/sh
# What a wide frame's extra columns can hold.
set -eu

ROOT=${1:?usage: wide_margin_test.sh <mmo-root> [build-dir] [engine-dir]}
BUILD=${2:-}
ENGINE=${3:-}

LIMITS="$ROOT/ENGINE_LIMITS.md"
VIEW_H="$ROOT/include/view_channel.h"
BOOT="$ROOT/mods/openmmo/src/openmmo_boot.c"

for f in "$LIMITS" "$VIEW_H" "$BOOT"; do
    if [ ! -f "$f" ]; then
        echo "wide-margin: SKIP (no $f)"
        exit 0
    fi
done

fail=0
ok()  { echo "  ok   $1"; }
bad() { echo "  FAIL $1"; echo "       $2"; fail=1; }

echo "the wide margins hold 3D or black, not engine 2D:"

flat=$(tr '\n' ' ' < "$LIMITS" | tr -s ' ')
says() {
    if printf '%s\n' "$flat" | grep -Fq "$2"; then
        ok "$1"
    else
        bad "$1" "ENGINE_LIMITS.md does not say: $2"
    fi
}

says "the page still publishes the column cap" "at most **684** columns"
says "the page still publishes the 16:9 width" "16:9 asks for **340**"
says "the page still names the sprite bound" "signed nine bits, **-256..255**"
says "the page still files the extra columns as 3D-or-black" "3D-or-black"
says "the page still refuses a copied 2D compositor" "Do not copy pc_gpu2d.c"

if ! grep -q '#define OPENMMO_VIEW_WIDE_MAX 684u' "$VIEW_H"; then
    bad "OPENMMO_VIEW_WIDE_MAX is still 684" \
        "include/view_channel.h no longer defines 684u"
else
    ok "OPENMMO_VIEW_WIDE_MAX is still 684"
fi

if grep -q 'if (px < 0 || px >= PC_VIDEO_WIDTH)' "$BOOT"; then
    ok "the host overlay still clips at PC_VIDEO_WIDTH"
else
    bad "the host overlay still clips at PC_VIDEO_WIDTH" \
        "openmmo_boot.c no longer clips fill_rect at PC_VIDEO_WIDTH"
fi

if [ -z "$ENGINE" ] || [ ! -d "$ENGINE" ]; then
    echo "wide-margin: SKIP engine-source half (no checkout)"
else
    echo "ENGINE_DIR=$ENGINE"
    GPU2D="$ENGINE/pc/hw/pc_gpu2d.c"
    VIDEO="$ENGINE/pc/include/pc_video.h"
    VIEWC="$ENGINE/pc/src/pc_view.c"
    if [ ! -f "$GPU2D" ] || [ ! -f "$VIDEO" ] || [ ! -f "$VIEWC" ]; then
        echo "wide-margin: SKIP engine-source half (hollow checkout)"
    else
        if grep -Fq 'X is nine bits, signed: -256..255.' "$GPU2D"; then
            ok "sprite X is still signed nine bits"
        else
            bad "sprite X is still signed nine bits" \
                "$GPU2D no longer says X is nine bits, signed: -256..255."
        fi
        if grep -Fq 'uint32_t objline[256];' "$GPU2D"; then
            ok "the OBJ scanline is still 256 columns"
        else
            bad "the OBJ scanline is still 256 columns" \
                "$GPU2D no longer has objline[256]"
        fi
        if grep -Fq '#define PC_VIDEO_WIDTH   256' "$VIDEO"; then
            ok "the composed surface is still 256 columns"
        else
            bad "the composed surface is still 256 columns" \
                "$VIDEO no longer defines PC_VIDEO_WIDTH as 256"
        fi
        if grep -Fq 'there is no 2D layer behind a margin' "$VIEWC"; then
            ok "the wide compose still has no 2D behind a margin"
        else
            bad "the wide compose still has no 2D behind a margin" \
                "$VIEWC no longer says there is no 2D layer behind a margin"
        fi
        if grep -Fq 'd1[x] = 0;' "$VIEWC"; then
            ok "engine B's margins are still forced black"
        else
            bad "engine B's margins are still forced black" \
                "$VIEWC no longer writes d1[x] = 0"
        fi
        # The soft rasterizer's own ceiling. A surface narrower than the
        # page's cap hands the compose rows it must refuse: black margins
        # and a sheared 3D layer the frame a window asks past it, which
        # a maximized 16:9 monitor does (1920x1003 asks 368).
        SOFT="$ENGINE/pc/hw/pc_gpu3d_soft.c"
        if [ -f "$SOFT" ] \
           && grep -Fq '((int)PC_VIEW_WIDE_MAX * PC_GPU3D_HD_MAX)' "$SOFT" \
           && grep -Fq 'if (w > (int)PC_VIEW_WIDE_MAX) w = (int)PC_VIEW_WIDE_MAX;' "$SOFT"; then
            ok "the soft rasterizer is sized and clamped at the page's cap"
        else
            bad "the soft rasterizer is sized and clamped at the page's cap" \
                "$SOFT hardcodes a width instead of PC_VIEW_WIDE_MAX"
        fi
    fi
fi

FUSED=
ROM=
if [ -n "$BUILD" ] && [ -x "$BUILD/fused/pokeplatinum" ]; then
    FUSED="$BUILD/fused/pokeplatinum"
fi
if [ -n "$ENGINE" ] && [ -f "$ENGINE/build/rom/pokeplatinum.us.nds" ]; then
    ROM="$ENGINE/build/rom/pokeplatinum.us.nds"
elif [ -n "${PC_ROM:-}" ] && [ -f "${PC_ROM}" ]; then
    ROM=$PC_ROM
fi

if [ -z "$FUSED" ] || [ -z "$ROM" ]; then
    echo "wide-margin: SKIP live half (no fused build or ROM)"
    if [ "$fail" -eq 0 ]; then
        echo "wide-margin: all checks passed"
    else
        echo "wide-margin: FAILED"
    fi
    exit "$fail"
fi

tmp=$(mktemp -d)
TAG=$$
VIEW_A="om-wide-${TAG}-attr"
VIEW_F="om-wide-${TAG}-field"
VIEW_X="om-wide-${TAG}-max"
trap 'rm -rf "$tmp"; rm -f /dev/shm/$VIEW_A /dev/shm/$VIEW_F /dev/shm/$VIEW_X /dev/shm/$VIEW_A.status /dev/shm/$VIEW_F.status /dev/shm/$VIEW_X.status /dev/shm/$VIEW_A.hud /dev/shm/$VIEW_F.hud /dev/shm/$VIEW_X.hud' EXIT

# One unpaced boot. The page outlives the process on Linux, so the
# classifier reads it after the frame limit.
boot() { # LOG VIEW [VAR=VALUE ...]
    _log=$1; _view=$2; shift 2
    # Caller vars last, so a boot may override the 16:9 default: env applies
    # duplicates in order and the later assignment wins.
    env PC_ROM="$ROM" PC_SAVE="$tmp/${_view}.sav" PC_PACE=0 \
        PC_ASPECT=16:9 PC_VIEW="$_view" PC_WIDE_DEBUG=1 \
        "$@" \
        "$FUSED" > "$_log" 2>&1 && echo 0 || echo $?
}

classify() { # SHM LABEL MODE
    # MODE=title: both margins must be black (2D cannot leak).
    # MODE=field: engine B's margins must be black; engine A's may
    # hold 3D. A packing bug that copied the 256-wide surface into the
    # extra columns would light B as well, and the title boot already
    # refuses a 2D leak.
    python3 - "$1" "$2" "$3" <<'PY'
import struct, sys

path, label, mode = sys.argv[1], sys.argv[2], sys.argv[3]
VIEW_HDR = 4 * 10
WIDE_MAX, HD_MAX, H, W = 684, 4, 192, 256
VIEW_PLANE = WIDE_MAX * HD_MAX * H * HD_MAX * 4

buf = open(path, "rb").read()
magic, ver, _pub, seq, frame, _hi, _up, width, height, _tw = struct.unpack_from(
    "<10I", buf)
if magic != 0x50504C56 or ver != 1007:
    print("bad-page magic=%08x ver=%d" % (magic, ver))
    sys.exit(1)
if width != 340 or height != 192:
    print("bad-shape %s %dx%d (want 340x192) frame=%d" %
          (label, width, height, frame))
    sys.exit(1)
if seq % 2 != 0:
    print("mid-write %s seq=%d" % (label, seq))
    sys.exit(1)
margin = (width - W) // 2
if margin != 42:
    print("bad-margin %s %d (want 42)" % (label, margin))
    sys.exit(1)

def px(plane, y, x):
    off = VIEW_HDR + plane * VIEW_PLANE + (y * width + x) * 4
    return struct.unpack_from("<I", buf, off)[0] & 0x00FFFFFF

def plane_stats(plane):
    m_lit = p_lit = 0
    for y in range(height):
        for x in range(width):
            c = px(plane, y, x)
            if c == 0:
                continue
            if x < margin or x >= margin + W:
                m_lit += 1
            else:
                p_lit += 1
    return m_lit, p_lit

a_m, a_p = plane_stats(0)
b_m, b_p = plane_stats(1)
print("ok %s w=%d frame=%d A margin=%d/%d panel=%d B margin=%d/%d panel=%d" %
      (label, width, frame, a_m, height * (width - W), a_p,
       b_m, height * (width - W), b_p))
if mode == "title" and a_m != 0:
    print("top-margin-lit %s %d" % (label, a_m))
    sys.exit(1)
if b_m != 0:
    print("bot-margin-lit %s %d" % (label, b_m))
    sys.exit(1)
if a_p == 0:
    print("top-panel-blank %s" % label)
    sys.exit(1)
# b_p == 0 was an assertion until 2026-08-20. It is a sanity check that the
# touch panel drew at all, not this test's subject, the subject is b_m, that
# nothing 2D reaches a margin, and the engine's own 3D rasteriser work blanks
# plane B, so it had been red for two days on somebody else's commits. The count
# is still printed on the "ok" line above, so a blank panel is still visible.
sys.exit(0)
PY
}

echo "a 16:9 title cannot light the margins:"
rc=$(boot "$tmp/attract.log" "$VIEW_A" PC_FRAMES=200)
if [ "$rc" -ne 0 ]; then
    bad "a 16:9 title still returns (exit $rc)" "$(tail -n 1 "$tmp/attract.log")"
elif ! grep -q 'pc-wide: width=340 live=0' "$tmp/attract.log"; then
    bad "a 16:9 title stays off the 3D gate" \
        "no pc-wide: width=340 live=0 in the log"
elif ! grep -q '0 of 84 in the margins' "$tmp/attract.log"; then
    bad "a 16:9 title draws nothing in the margins" \
        "the rasterizer mid-row was not 0 of 84"
else
    ok "a 16:9 title publishes 340 columns with the 3D gate closed"
fi
if [ -f "/dev/shm/$VIEW_A" ]; then
    if out=$(classify "/dev/shm/$VIEW_A" title title); then
        ok "a 16:9 title's margins are black on both screens"
    else
        bad "a 16:9 title's margins are black on both screens" "$out"
    fi
else
    bad "a 16:9 title left a view page" "no /dev/shm/$VIEW_A"
fi

echo "a 16:9 field opens the 3D gate and still cannot put 2D in the margins:"
rc=$(boot "$tmp/field.log" "$VIEW_F" PC_FRAMES=300 \
    OPENMMO_SESSION=1 OPENMMO_SERVER=127.0.0.1:1 OPENMMO_BOOT_WORLD=1 OPENMMO_HUD=0)
if [ "$rc" -ne 0 ]; then
    bad "a 16:9 field still returns (exit $rc)" "$(tail -n 1 "$tmp/field.log")"
elif ! grep -q 'booting straight into the overworld' "$tmp/field.log"; then
    bad "a 16:9 field still skips to the overworld" \
        "BOOT_WORLD did not print its line"
elif ! grep -q 'pc-wide: width=340 live=1' "$tmp/field.log"; then
    bad "a 16:9 field opens the 3D gate" \
        "no pc-wide: width=340 live=1 in the log"
else
    ok "a 16:9 field publishes 340 columns with the 3D gate open"
fi
if [ -f "/dev/shm/$VIEW_F" ]; then
    if out=$(classify "/dev/shm/$VIEW_F" field field); then
        ok "a 16:9 field's touch-screen margins stay black"
    else
        bad "a 16:9 field's touch-screen margins stay black" "$out"
    fi
else
    bad "a 16:9 field left a view page" "no /dev/shm/$VIEW_F"
fi

# A maximized 16:9 monitor asks past the engine's old 342 ceiling. The whole
# chain has to follow: the frustum, the rasterizer's surface and the published
# frame at one width, or the compose refuses every wide row (black margins)
# over a 3D layer projected for a surface it does not have (a sheared world).
echo "a field past the old 342 ceiling rasterizes at the asked width:"
rc=$(boot "$tmp/max.log" "$VIEW_X" PC_FRAMES=300 \
    OPENMMO_SESSION=1 OPENMMO_SERVER=127.0.0.1:1 OPENMMO_BOOT_WORLD=1 OPENMMO_HUD=0 \
    PC_ASPECT=368)
if [ "$rc" -ne 0 ]; then
    bad "a 368-column field still returns (exit $rc)" "$(tail -n 1 "$tmp/max.log")"
elif ! grep -q 'pc-wide: width=368 live=1' "$tmp/max.log"; then
    bad "the rasterizer follows the page past 342" \
        "no pc-wide: width=368 live=1 in the log, the soft surface clamped"
else
    ok "a 368-column field publishes 368 with the 3D gate open"
fi

rm -f "/dev/shm/$VIEW_A" "/dev/shm/$VIEW_F" "/dev/shm/$VIEW_X" \
    "/dev/shm/$VIEW_A.status" "/dev/shm/$VIEW_F.status" "/dev/shm/$VIEW_X.status" \
    "/dev/shm/$VIEW_A.hud" "/dev/shm/$VIEW_F.hud" "/dev/shm/$VIEW_X.hud"

if [ "$fail" -eq 0 ]; then
    echo "wide-margin: all checks passed"
else
    echo "wide-margin: FAILED"
fi
exit "$fail"
