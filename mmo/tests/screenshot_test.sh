#!/bin/sh
# Presentation regressions fail here, not in a stream.
#
#   * native session title, dump-frames digest of frame 200 (HUD + ROM font)
#   * 16:9 attract, both published planes (title margins stay black)
#   * 16:9 session title, both published planes (HUD on a wide frame)
#   * that page presented stacked at --scale 1 and --scale 2 (the 256-column
#     panel, not 340, extra columns do not take layout space)
#   * that page presented fill at 1280x720 and 1600x900 (world left of a
#     second-screen band: no poketch page, so the screens sit apart)
#   * 16:9 Twinleaf bedroom, both published planes (interior extra columns
#     stay black)
set -eu

ROOT=${1:?usage: screenshot_test.sh <mmo-root> [build-dir] [engine-dir]}
BUILD=${2:-}
ENGINE=${3:-}

LIMITS="$ROOT/ENGINE_LIMITS.md"

for f in "$LIMITS"; do
    if [ ! -f "$f" ]; then
        echo "screenshot: SKIP (no $f)"
        exit 0
    fi
done

fail=0
ok()  { echo "  ok   $1"; }
bad() { echo "  FAIL $1"; echo "       $2"; fail=1; }

# dump-frames FNV of the 256x192 dual-screen surface, uppercase.
PIN_TITLE_NATIVE=42CD7020C6C2D4AA
# sha256 of a P6 of one published plane, packed at the frame's own width.
PIN_ATTRACT_A=36640f1b4d46fa0591eb8c66a580c3c55d9c54e7fe99428027d7778db7544763
PIN_SESSION_A=3c5b7a2ecd8d05c24e5b8f9b64c4b56d81351ffb95e7f5b257821e5f116b2565
# Moved 2026-08-20 when field scripts became the client's: the bedroom plays its
# own tv script on entry now, so frame 300 has the cartridge's message box on it
# and the panel is 29,097 lit pixels rather than 18,806. The old digest was a
# bedroom with nothing happening in it, which is not what this map does.
PIN_FIELD_A=99cfd5ddad1e852eadd931c6a89b86a68c807ac315027bb56ea984f8d4cf4f54
# sha256 of the window's own --shot PPM.
PIN_SHOT_S1=25e29c5b1d5a967a33a6c112d630cd4ad409d62e062af9cac7a298fac24923ea
PIN_SHOT_S2=afbc62ee300c66920255f982f8d064a3339f3835aa69376626278afbeb3876b2
# fill layout at two window sizes, same 16:9 session page, screens apart.
PIN_SHOT_FILL_720=548581ac9841a707aa0aee08a71fc18bd6a46313689117d0c6a8634876ab4ac8
PIN_SHOT_FILL_900=2adcfa5bce777522dc30c486063cacd0250ea2a24e84ba57d755a1de4d2a8e89

echo "presentation is a screenshot pin:"

flat=$(tr '\n' ' ' < "$LIMITS" | tr -s ' ')
says() {
    if printf '%s\n' "$flat" | grep -Fq "$2"; then
        ok "$1"
    else
        bad "$1" "ENGINE_LIMITS.md does not say: $2"
    fi
}

says "the page still names the screenshot pin" "screenshot pin"
says "the page still names the session-title digest" "dump-frames"
says "the page still names the two composed sizes" "stacked at --scale 1 and --scale 2"
says "the page still files outdoor fill as the other check" "camera_test.sh"

FUSED=
ROM=
VIEW=
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

if [ -z "$FUSED" ] || [ -z "$ROM" ]; then
    echo "screenshot: SKIP live half (no fused build or ROM)"
    if [ "$fail" -eq 0 ]; then
        echo "screenshot: all checks passed"
    else
        echo "screenshot: FAILED"
    fi
    exit "$fail"
fi

echo "ENGINE_DIR=${ENGINE:-}"
tmp=$(mktemp -d)
TAG=$$
VIEW_A="om-shot-a-${TAG}"
VIEW_S="om-shot-s-${TAG}"
VIEW_F="om-shot-f-${TAG}"
trap 'rm -rf "$tmp"; rm -f /dev/shm/$VIEW_A /dev/shm/$VIEW_S /dev/shm/$VIEW_F /dev/shm/$VIEW_A.status /dev/shm/$VIEW_S.status /dev/shm/$VIEW_F.status /dev/shm/$VIEW_A.hud /dev/shm/$VIEW_S.hud /dev/shm/$VIEW_F.hud' EXIT

# Classify and hash both planes of a leftover view page. Prints
#   ok <label> <w>x<h> frame=<n> A margin=<m> panel=<p> <sha> B ...
# then one HASH A/B line, and exits 1 on a bad page.
hash_page() { # SHM LABEL
    python3 - "$1" "$2" <<'PY'
import hashlib, struct, sys

path, label = sys.argv[1], sys.argv[2]
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

def plane_hash(plane):
    rgb = bytearray()
    m_lit = p_lit = 0
    for y in range(height):
        for x in range(width):
            off = VIEW_HDR + plane * VIEW_PLANE + (y * width + x) * 4
            c = struct.unpack_from("<I", buf, off)[0] & 0x00FFFFFF
            rgb.extend(((c >> 16) & 255, (c >> 8) & 255, c & 255))
            if c == 0:
                continue
            if x < margin or x >= margin + W:
                m_lit += 1
            else:
                p_lit += 1
    data = b"P6\n%d %d\n255\n" % (width, height) + bytes(rgb)
    return hashlib.sha256(data).hexdigest(), m_lit, p_lit

a_h, a_m, a_p = plane_hash(0)
b_h, b_m, b_p = plane_hash(1)
print("ok %s %dx%d frame=%d A margin=%d panel=%d %s B margin=%d panel=%d %s" %
      (label, width, height, frame, a_m, a_p, a_h, b_m, b_p, b_h))
print("HASH A %s" % a_h)
print("HASH B %s" % b_h)
sys.exit(0)
PY
}

expect_hash() { # LABEL WANT GOT
    if [ "$3" = "$2" ]; then
        ok "$1 still hashes $2"
    else
        bad "$1 still hashes $2" "got ${3:-missing}"
    fi
}

echo "a native session title still draws the same frame 200:"
env OPENMMO_ASSERT=warn OPENMMO_SESSION=1 OPENMMO_SERVER=127.0.0.1:1 \
    PC_ROM="$ROM" PC_SAVE="$tmp/title.sav" PC_FRAMES=200 PC_PACE=0 \
    PC_DUMP_FRAMES="$tmp/title" PC_DUMP_FROM=200 \
    "$FUSED" > "$tmp/title.log" 2>&1 && rc=0 || rc=$?
if [ "$rc" -ne 0 ]; then
    bad "a native session title still returns (exit $rc)" \
        "$(tail -n 1 "$tmp/title.log")"
elif ! grep -q 'openmmo: session title' "$tmp/title.log"; then
    bad "a native session title still boots" "no 'session title' in the log"
else
    ok "a native session title still boots"
fi
got=$(awk '$1=="000200"{print $NF}' "$tmp/title/frames.txt" 2>/dev/null || true)
expect_hash "the native session title" "$PIN_TITLE_NATIVE" "$got"

echo "a 16:9 attract still publishes the same black-margin title:"
env OPENMMO_ASSERT=warn \
    PC_ROM="$ROM" PC_SAVE="$tmp/attract.sav" PC_FRAMES=200 PC_PACE=0 \
    PC_ASPECT=16:9 PC_VIEW="$VIEW_A" \
    "$FUSED" > "$tmp/attract.log" 2>&1 && rc=0 || rc=$?
if [ "$rc" -ne 0 ]; then
    bad "a 16:9 attract still returns (exit $rc)" \
        "$(tail -n 1 "$tmp/attract.log")"
elif [ ! -f "/dev/shm/$VIEW_A" ]; then
    bad "a 16:9 attract left a view page" "no /dev/shm/$VIEW_A"
else
    if out=$(hash_page "/dev/shm/$VIEW_A" attract); then
        echo "$out" | sed -n 's/^ok /  /p'
        got_a=$(printf '%s\n' "$out" | awk '$1=="HASH" && $2=="A"{print $3}')
        got_b=$(printf '%s\n' "$out" | awk '$1=="HASH" && $2=="B"{print $3}')
        expect_hash "the 16:9 attract top screen" "$PIN_ATTRACT_A" "$got_a"
    else
        bad "a 16:9 attract still publishes 340x192" "$out"
    fi
fi

echo "a 16:9 session title still publishes the same wide frame:"
env OPENMMO_ASSERT=warn OPENMMO_SESSION=1 OPENMMO_SERVER=127.0.0.1:1 \
    PC_ROM="$ROM" PC_SAVE="$tmp/session.sav" PC_FRAMES=200 PC_PACE=0 \
    PC_ASPECT=16:9 PC_VIEW="$VIEW_S" \
    "$FUSED" > "$tmp/session.log" 2>&1 && rc=0 || rc=$?
if [ "$rc" -ne 0 ]; then
    bad "a 16:9 session title still returns (exit $rc)" \
        "$(tail -n 1 "$tmp/session.log")"
elif ! grep -q 'openmmo: session title' "$tmp/session.log"; then
    bad "a 16:9 session title still boots" "no 'session title' in the log"
elif [ ! -f "/dev/shm/$VIEW_S" ]; then
    bad "a 16:9 session title left a view page" "no /dev/shm/$VIEW_S"
else
    if out=$(hash_page "/dev/shm/$VIEW_S" session); then
        echo "$out" | sed -n 's/^ok /  /p'
        got_a=$(printf '%s\n' "$out" | awk '$1=="HASH" && $2=="A"{print $3}')
        got_b=$(printf '%s\n' "$out" | awk '$1=="HASH" && $2=="B"{print $3}')
        expect_hash "the 16:9 session top screen" "$PIN_SESSION_A" "$got_a"
    else
        bad "a 16:9 session title still publishes 340x192" "$out"
    fi
fi

if [ -z "$VIEW" ]; then
    echo "screenshot: SKIP composed-window half (no openmmo-view)"
elif [ ! -f "/dev/shm/$VIEW_S" ]; then
    bad "the composed window still has a page to present" \
        "no leftover 16:9 session page"
else
    echo "that page presented at two sizes is still the 256-column panel:"
    SDL_VIDEODRIVER=dummy
    export SDL_VIDEODRIVER
    shot() { # SCALE OUT
        timeout 30 "$VIEW" "$VIEW_S" --no-audio --layout stacked \
            --scale "$1" --render-scale 1 --filter nearest --shot "$2" --wait 2000 \
            >"$tmp/shot-$1.out" 2>"$tmp/shot-$1.err" && echo 0 || echo $?
    }
    check_shot() { # SCALE WANT_W WANT_H WANT_HASH
        ppm="$tmp/shot-s$1.ppm"
        rc=$(shot "$1" "$ppm")
        if [ "$rc" -ne 0 ] || [ ! -f "$ppm" ]; then
            bad "stacked --scale $1 still writes a shot (exit $rc)" \
                "$(tail -n 1 "$tmp/shot-$1.err" 2>/dev/null || echo none)"
            return
        fi
        hdr=$(python3 - "$ppm" <<'PY'
import sys
data = open(sys.argv[1], "rb").read()
if not data.startswith(b"P6"):
    print("not-p6")
    raise SystemExit
rest = data.split(b"\n", 2)
wh = rest[1].split()
print("%s %s" % (wh[0].decode(), wh[1].decode()))
PY
)
        w=$(echo "$hdr" | awk '{print $1}')
        h=$(echo "$hdr" | awk '{print $2}')
        if [ "$w" = "$2" ] && [ "$h" = "$3" ]; then
            ok "stacked --scale $1 is still ${2}x${3} (the 256-column panel)"
        else
            bad "stacked --scale $1 is still ${2}x${3} (the 256-column panel)" \
                "got ${w:-?}x${h:-?}"
        fi
        got=$(sha256sum "$ppm" | awk '{print $1}')
        expect_hash "stacked --scale $1" "$4" "$got"
    }
    check_shot 1 256 384 "$PIN_SHOT_S1"
    check_shot 2 512 768 "$PIN_SHOT_S2"

    echo "that page presented fill is the world left of a second-screen band:"
    fill_shot() { # W H OUT
        timeout 30 "$VIEW" "$VIEW_S" --no-audio --layout fill \
            --size "${1}x${2}" --render-scale 1 --filter nearest \
            --shot "$3" --wait 2000 \
            >"$tmp/shot-fill-$1.out" 2>"$tmp/shot-fill-$1.err" && echo 0 || echo $?
    }
    check_fill() { # W H WANT_HASH
        ppm="$tmp/shot-fill-$1x$2.ppm"
        rc=$(fill_shot "$1" "$2" "$ppm")
        if [ "$rc" -ne 0 ] || [ ! -f "$ppm" ]; then
            bad "fill ${1}x${2} still writes a shot (exit $rc)" \
                "$(tail -n 1 "$tmp/shot-fill-$1.err" 2>/dev/null || echo none)"
            return
        fi
        hdr=$(python3 - "$ppm" <<'PY'
import sys
data = open(sys.argv[1], "rb").read()
if not data.startswith(b"P6"):
    print("not-p6")
    raise SystemExit
rest = data.split(b"\n", 2)
wh = rest[1].split()
print("%s %s" % (wh[0].decode(), wh[1].decode()))
PY
)
        w=$(echo "$hdr" | awk '{print $1}')
        h=$(echo "$hdr" | awk '{print $2}')
        if [ "$w" = "$1" ] && [ "$h" = "$2" ]; then
            ok "fill --size ${1}x${2} is still ${1}x${2}"
        else
            bad "fill --size ${1}x${2} is still ${1}x${2}" \
                "got ${w:-?}x${h:-?}"
        fi
        got=$(sha256sum "$ppm" | awk '{print $1}')
        expect_hash "fill ${1}x${2}" "$3" "$got"
    }
    # Hashes filled after the first green run of this layout at these sizes.
    check_fill 1280 720 "$PIN_SHOT_FILL_720"
    check_fill 1600 900 "$PIN_SHOT_FILL_900"
fi

echo "a 16:9 Twinleaf bedroom still publishes the same unfilled margins:"
env OPENMMO_ASSERT=warn OPENMMO_SESSION=1 OPENMMO_SERVER=127.0.0.1:1 \
    OPENMMO_BOOT_WORLD=1 OPENMMO_HUD=0 \
    PC_ROM="$ROM" PC_SAVE="$tmp/field.sav" PC_FRAMES=300 PC_PACE=0 \
    PC_ASPECT=16:9 PC_VIEW="$VIEW_F" \
    "$FUSED" > "$tmp/field.log" 2>&1 && rc=0 || rc=$?
if [ "$rc" -ne 0 ]; then
    bad "a 16:9 field still returns (exit $rc)" \
        "$(tail -n 1 "$tmp/field.log")"
elif ! grep -q 'booting straight into the overworld' "$tmp/field.log"; then
    bad "a 16:9 field still skips to the overworld" \
        "BOOT_WORLD did not print its line"
elif [ ! -f "/dev/shm/$VIEW_F" ]; then
    bad "a 16:9 field left a view page" "no /dev/shm/$VIEW_F"
else
    if out=$(hash_page "/dev/shm/$VIEW_F" field); then
        echo "$out" | sed -n 's/^ok /  /p'
        got_a=$(printf '%s\n' "$out" | awk '$1=="HASH" && $2=="A"{print $3}')
        got_b=$(printf '%s\n' "$out" | awk '$1=="HASH" && $2=="B"{print $3}')
        expect_hash "the 16:9 bedroom top screen" "$PIN_FIELD_A" "$got_a"
    else
        bad "a 16:9 field still publishes 340x192" "$out"
    fi
fi

if [ "$fail" -ne 0 ]; then
    echo "screenshot: FAILED"
    exit 1
fi
echo "screenshot: all checks passed"
exit 0
