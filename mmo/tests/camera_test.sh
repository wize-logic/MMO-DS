#!/bin/sh
# The cartridge field camera is the default, and the one
# lever past it is distance.
set -eu

ROOT=${1:?usage: camera_test.sh <mmo-root> [build-dir] [engine-dir]}
BUILD=${2:-}
ENGINE=${3:-}

LIMITS="$ROOT/ENGINE_LIMITS.md"
PATCHDIR="$ROOT/mods/openmmo/patches/src/overlay005"

for f in "$LIMITS"; do
    if [ ! -f "$f" ]; then
        echo "camera: SKIP (no $f)"
        exit 0
    fi
done

fail=0
ok()  { echo "  ok   $1"; }
bad() { echo "  FAIL $1"; echo "       $2"; fail=1; }

echo "the field camera stays the cartridge's:"

flat=$(tr '\n' ' ' < "$LIMITS" | tr -s ' ')
says() {
    if printf '%s\n' "$flat" | grep -Fq "$2"; then
        ok "$1"
    else
        bad "$1" "ENGINE_LIMITS.md does not say: $2"
    fi
}

says "the page still keeps the cartridge camera" "cartridge field camera"
says "the page still keeps official as the default" \
    "100 the built camera is bit-for-bit the cartridge's"
says "the page still names the four-chunk load" "four 32"
says "the page still files outdoor extra columns as filled" "Outdoor geometry fills them"

# The stance is the distance lever and nothing else: it may scale the
# distance and slide the far plane, and it may not reach the angles,
# FOVs or near planes the old decision holds in place. The lever itself is a
# mod source; the patch is the two calls into it.
CAMPATCH="$PATCHDIR/field_camera.c.patch"
CAMSRC="$ROOT/mods/openmmo/src/openmmo_camera.c"
if [ ! -f "$CAMPATCH" ] || [ ! -f "$CAMSRC" ]; then
    bad "the field-camera patch and its mod source exist" "no $CAMPATCH or $CAMSRC"
else
    ok "the field-camera patch and its mod source exist"
    if grep -Fq 'openmmo_camera_scale_distance' "$CAMPATCH" \
       && grep -Fq 'openmmo_camera_distance_percent' "$CAMSRC"; then
        ok "and it is gated on the host's percent"
    else
        bad "and it is gated on the host's percent" \
            "$CAMPATCH never calls openmmo_camera_scale_distance, or" \
            "$CAMSRC never reads openmmo_camera_distance_percent"
    fi
    if { grep -E '^\+' "$CAMPATCH"; cat "$CAMSRC"; } \
       | grep -Eq '\.verticalFov =|\.cameraAngle =|\.nearPlaneDist =|FX32_CONST|F32_DEG_TO_IDX|Camera_SetFOV|Camera_SetAngle'; then
        bad "and it touches only distance and far plane" \
            "a line in $CAMPATCH or $CAMSRC reaches the angle, FOV or near plane"
    else
        ok "and it touches only distance and far plane"
    fi
fi

if [ -z "$ENGINE" ] || [ ! -d "$ENGINE" ]; then
    echo "camera: SKIP engine-source half (no checkout)"
else
    echo "ENGINE_DIR=$ENGINE"
    CAM="$ENGINE/src/overlay005/field_camera.c"
    CAMC="$ENGINE/src/camera.c"
    LOC="$ENGINE/src/location.c"
    QUAD="$ENGINE/include/constants/quadrant.h"
    TILE="$ENGINE/include/constants/field/map.h"
    if [ ! -f "$CAM" ] || [ ! -f "$CAMC" ] || [ ! -f "$LOC" ] \
       || [ ! -f "$QUAD" ] || [ ! -f "$TILE" ]; then
        echo "camera: SKIP engine-source half (hollow checkout)"
    else
        if grep -Fq '.distance = FX32_CONST(666.922119140625),' "$CAM" \
           && grep -Fq '.verticalFov = F32_DEG_TO_IDX(8.0914306640625),' "$CAM"; then
            ok "DEFAULT is still 667 units at 8.09 degrees"
        else
            bad "DEFAULT is still 667 units at 8.09 degrees" \
                "$CAM no longer has the cartridge DEFAULT distance/FOV"
        fi
        if grep -Fq '#define CAMERA_DEFAULT_ASPECT_RATIO (FX32_ONE * 4 / 3)' "$CAMC"; then
            ok "the game camera is still 4:3"
        else
            bad "the game camera is still 4:3" \
                "$CAMC no longer defines CAMERA_DEFAULT_ASPECT_RATIO as 4/3"
        fi
        if grep -Fq '.x = 116,' "$LOC" && grep -Fq '.z = 886,' "$LOC" \
           && grep -Fq 'MAP_HEADER_TWINLEAF_TOWN,' "$LOC"; then
            ok "first-respawn is still Twinleaf Town at 116, 886"
        else
            bad "first-respawn is still Twinleaf Town at 116, 886" \
                "$LOC no longer names Twinleaf Town at 116, 886"
        fi
        if grep -Fq '#define QUADRANT_COUNT 4' "$QUAD"; then
            ok "the land-data load is still four chunks"
        else
            bad "the land-data load is still four chunks" \
                "$QUAD no longer defines QUADRANT_COUNT as 4"
        fi
        if grep -Fq '#define MAP_TILES_COUNT_X 32' "$TILE" \
           && grep -Fq '#define MAP_TILES_COUNT_Z 32' "$TILE"; then
            ok "a land-data chunk is still 32x32 tiles"
        else
            bad "a land-data chunk is still 32x32 tiles" \
                "$TILE no longer defines MAP_TILES_COUNT as 32"
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

# Twinleaf Town's header id, from the checkout's own table, not a
# remembered number. Line 1 of generated/map_headers.txt is 0.
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
    echo "camera: SKIP live half (no fused build, ROM, settle script or header id)"
    if [ "$fail" -eq 0 ]; then
        echo "camera: all checks passed"
    else
        echo "camera: FAILED"
    fi
    exit "$fail"
fi

tmp=$(mktemp -d)
TAG=$$
VIEW="om-cam-${TAG}"
VIEW2="om-cam2-${TAG}"
trap 'rm -rf "$tmp"; rm -f /dev/shm/$VIEW /dev/shm/$VIEW.status /dev/shm/$VIEW2 /dev/shm/$VIEW2.status' EXIT

printf 'name SHORT\nmap %s 116 886 1\n' "$hdr" > "$tmp/town.lab"

# The lab applies at 1800 (the bedroom tv scene has to finish first)
# and exits after the 360-frame settle. 2500 is past that with slack.
env OPENMMO_ASSERT=warn OPENMMO_HUD=0 \
    PC_ROM="$ROM" PC_SAVE="$tmp/town.sav" PC_PACE=0 \
    PC_ASPECT=16:9 PC_VIEW="$VIEW" PC_WIDE_DEBUG=1 \
    PC_FRAMES=2500 PC_LAB="$tmp/town.lab" PC_LAB_AT=1800 \
    PC_INPUT="$SETTLE" PC_LAB_MAPSCAN="$tmp/town.scan" \
    "$FUSED" > "$tmp/town.log" 2>&1 && rc=0 || rc=$?

if [ "$rc" -ne 0 ]; then
    bad "a 16:9 Twinleaf Town still returns (exit $rc)" \
        "$(tail -n 1 "$tmp/town.log")"
elif ! grep -q "map $hdr" "$tmp/town.scan" 2>/dev/null; then
    bad "a 16:9 Twinleaf Town still lands on header $hdr" \
        "mapscan is not map $hdr"
elif ! grep -q 'player 116 886' "$tmp/town.scan"; then
    bad "a 16:9 Twinleaf Town still stands at 116, 886" \
        "mapscan player is not 116 886"
elif ! grep -q 'pc-wide: width=340 live=1' "$tmp/town.log"; then
    bad "a 16:9 Twinleaf Town opens the 3D gate" \
        "no pc-wide: width=340 live=1 in the log"
else
    ok "a 16:9 Twinleaf Town publishes 340 columns with the 3D gate open"
fi

if [ -f "/dev/shm/$VIEW" ]; then
    if out=$(python3 - "/dev/shm/$VIEW" <<'PY'
import struct, sys

path = sys.argv[1]
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
    print("bad-shape %dx%d (want 340x192) frame=%d" % (width, height, frame))
    sys.exit(1)
if seq % 2 != 0:
    print("mid-write seq=%d" % seq)
    sys.exit(1)
margin = (width - W) // 2
if margin != 42:
    print("bad-margin %d (want 42)" % margin)
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
print("ok town w=%d frame=%d A margin=%d/%d panel=%d B margin=%d/%d panel=%d" %
      (width, frame, a_m, height * (width - W), a_p,
       b_m, height * (width - W), b_p))
# Bedroom at the same aspect lights zero extra columns. Outdoor has to
# light a real slice of them, a handful of leftover pixels is not that.
if a_m < 1000:
    print("top-margin-empty %d" % a_m)
    sys.exit(1)
if b_m != 0:
    print("bot-margin-lit %d" % b_m)
    sys.exit(1)
if a_p == 0:
    print("top-panel-blank")
    sys.exit(1)
sys.exit(0)
PY
    ); then
        ok "a 16:9 Twinleaf Town lights the extra columns"
    else
        bad "a 16:9 Twinleaf Town lights the extra columns" "$out"
    fi
else
    bad "a 16:9 Twinleaf Town left a view page" "no /dev/shm/$VIEW"
fi

# The same warp with the camera at the 130 ceiling: the launcher's whole range
# must return, land on the same tile, and publish a different, still nearly-
# full picture, farther out means more town in the same 192 rows, not a frame
# going black past the loaded map.
env OPENMMO_ASSERT=warn OPENMMO_HUD=0 OPENMMO_CAMERA_DISTANCE=130 \
    PC_ROM="$ROM" PC_SAVE="$tmp/far.sav" PC_PACE=0 \
    PC_ASPECT=16:9 PC_VIEW="$VIEW2" PC_WIDE_DEBUG=1 \
    PC_FRAMES=2500 PC_LAB="$tmp/town.lab" PC_LAB_AT=1800 \
    PC_INPUT="$SETTLE" PC_LAB_MAPSCAN="$tmp/far.scan" \
    "$FUSED" > "$tmp/far.log" 2>&1 && rc=0 || rc=$?

if [ "$rc" -ne 0 ]; then
    bad "the far camera still returns (exit $rc)" \
        "$(tail -n 1 "$tmp/far.log")"
elif ! grep -q 'player 116 886' "$tmp/far.scan" 2>/dev/null; then
    bad "the far camera still stands at 116, 886" \
        "mapscan player is not 116 886"
elif [ ! -f "/dev/shm/$VIEW2" ] || [ ! -f "/dev/shm/$VIEW" ]; then
    bad "the far camera left a view page beside the official client's" \
        "one of the two pages is missing"
elif out=$(python3 - "/dev/shm/$VIEW" "/dev/shm/$VIEW2" <<'PY'
import struct, sys

VIEW_HDR = 4 * 10
WIDE_MAX, HD_MAX, H, W = 684, 4, 192, 256

def load(path):
    buf = open(path, "rb").read()
    magic, ver, _pub, _seq, frame, _hi, _up, width, height, _tw = \
        struct.unpack_from("<10I", buf)
    if magic != 0x50504C56:
        print("bad-page %s" % path)
        sys.exit(1)
    return buf, width, height

near, nw, nh = load(sys.argv[1])
far, fw, fh = load(sys.argv[2])
if (nw, nh) != (fw, fh):
    print("shape-differs %dx%d vs %dx%d" % (nw, nh, fw, fh))
    sys.exit(1)

npix = fw * fh
differs = black = 0
for i in range(npix):
    off = VIEW_HDR + i * 4
    a = near[off:off + 3]
    b = far[off:off + 3]
    if a != b:
        differs += 1
    if b == b"\x00\x00\x00":
        black += 1
print("differs=%d black=%d of %d" % (differs, black, npix))
if differs < npix // 4:
    print("far-camera-changed-too-little")
    sys.exit(1)
if black > 2000:
    print("far-camera-frame-going-black")
    sys.exit(1)
sys.exit(0)
PY
    ); then
    ok "the 130% camera publishes a different, still nearly-full town"
else
    bad "the 130% camera publishes a different, still nearly-full town" "$out"
fi

rm -f "/dev/shm/$VIEW" "/dev/shm/$VIEW.status" \
      "/dev/shm/$VIEW2" "/dev/shm/$VIEW2.status"

if [ "$fail" -eq 0 ]; then
    echo "camera: all checks passed"
else
    echo "camera: FAILED"
fi
exit "$fail"
