#!/bin/sh
# A name drawn over a player standing in the world, checked
# against the picture the rasterizer actually drew.
#
#   1. The projection agrees with the picture. Boot twice at the same frame, once
#      with a remote avatar and once without, and diff the two polygon lists. The
#      avatar adds exactly one large axis-aligned quad, its billboard, and its
#      bottom centre is where a nameplate's anchor belongs. With the label's lift
#      turned off, the projection must answer that same pixel. This is what says
#      NNS_G3dWorldPosToScrPos is reading the transform the frame was drawn with,
#      including that the field's own projection-matrix bias around the billboard
#      pass (fieldmap.c:704) moves depth and not position.
#   2. The label is painted, and its anchor is on screen. The default lift must
#      put the anchor above the avatar's head rather than off the top of the
#      screen, and the label must actually reach the surface.
#   3. The GLYPHS are the ROM FONT'S, for A NAME that is not ASCII. A label is
#      drawn from engine charcodes through the engine's own font, so a name with
#      accents must draw with no substituted glyph at all, OPENMMO_FONT=fatal
#      stops the run on the first one, which is what makes that checkable.
#   4. An empty NAME still draws. A spawn with no name is still a person; the
#      plate shows "?" rather than vanishing.
#   5. A catalog body still carries the plate. A hiker (gfx 20) is not the
#      trainer model the projection was measured on; the name still lands.
#   6. The plate is white, after something else printed in another COLOUR. Which
#      pixel value of a decompressed glyph is the letter is the last text
#      printer's choice, not a constant, so a caller outside the printer system
#      has to set it. OPENMMO_GLYPH_CLOBBER=1 leaves the table the way character
#      select leaves it and the plate must still be white; OPENMMO_GLYPH_COLORS=0
#      turns the answer off and the same boot must go grey, which is what makes
#      it a check. Counted in pixels off the frame dump, a grey plate and a
#      white one are identical to every other instrument here.
set -eu

ROOT=${1:?usage: label_test.sh <mmo-root> <build-dir> <engine-dir>}
BUILD=${2:?usage: label_test.sh <mmo-root> <build-dir> <engine-dir>}
ENGINE=${3:?usage: label_test.sh <mmo-root> <build-dir> <engine-dir>}

FUSED="$BUILD/fused/pokeplatinum"
ROM="${PC_ROM:-$ENGINE/build/rom/pokeplatinum.us.nds}"
SETTLE="$ENGINE/pc/replays/lab-settle.txt"

if [ ! -x "$FUSED" ]; then
    echo "label: SKIP (no fused build: run \`make -C mmo fused\`)"
    exit 0
fi
if [ ! -f "$ROM" ] || [ ! -f "$SETTLE" ]; then
    echo "label: SKIP (no ROM or input script under $ENGINE)"
    exit 0
fi

fail=0
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

ok()  { echo "  ok   $1"; }
bad() { echo "  FAIL $1"; fail=1; }

printf 'name SHORT\n' > "$tmp/min.lab"

# The frame the comparison is made on. The lab mints its save at 1800 and the
# run ends there, so the last frame the field is up for is 1799.
FRAME=1799

# A save per boot, never a shared one: a second boot onto an existing save
# reaches the game by the continue path and leaves by a route that runs none of
# the port's exit reports.
boot() {
    which=$1
    shift
    env OPENMMO_ASSERT=warn OPENMMO_HUD=0 "$@" \
        PC_ROM="$ROM" PC_SAVE="$tmp/$which.sav" PC_LAB="$tmp/min.lab" \
        PC_LAB_AT=1800 PC_FRAMES=1800 PC_PACE=0 PC_INPUT="$SETTLE" \
        "$FUSED"
}

echo "a name drawn over a player standing in the world:"

# --- 1. the projection against the rasterizer's own polygon list -------------

rc=0
boot alone OPENMMO_LABELS=0 \
    PC_DUMP_POLYS="$FRAME-$FRAME" PC_DUMP_POLYS_FILE="$tmp/alone.polys" \
    > "$tmp/alone.log" 2>&1 || rc=$?
if [ "$rc" -ne 0 ]; then
    bad "the game boots with nobody else on the map (exit $rc)"
    tail -3 "$tmp/alone.log"
    exit 1
fi

rc=0
boot peer OPENMMO_FAKE_ENTITY=1 OPENMMO_FAKE_NAME=Wanderer \
    OPENMMO_LABEL_REPORT=2 OPENMMO_LABEL_LIFT=0 \
    PC_DUMP_POLYS="$FRAME-$FRAME" PC_DUMP_POLYS_FILE="$tmp/peer.polys" \
    > "$tmp/peer.log" 2>&1 || rc=$?
if [ "$rc" -ne 0 ]; then
    bad "the game boots with a remote player on the map (exit $rc)"
    tail -3 "$tmp/peer.log"
    exit 1
fi

# The quad the avatar added, as the rasterizer drew it: the largest axis-aligned
# rectangle in the peer's polygon list that is not in the empty map's.
quad=$(python3 - "$tmp/alone.polys" "$tmp/peer.polys" <<'PY'
import re, sys

def rects(path):
    out = []
    for line in open(path):
        if not line.startswith('   '):
            continue
        vs = [t for t in line.split() if re.fullmatch(r'-?\d+,-?\d+', t)]
        if len(vs) != 4:
            continue
        pts = [tuple(map(int, v.split(','))) for v in vs]
        xs = sorted(set(p[0] for p in pts))
        ys = sorted(set(p[1] for p in pts))
        if len(xs) == 2 and len(ys) == 2:
            out.append((xs[0], xs[1], ys[0], ys[1]))
    return out

alone = set(rects(sys.argv[1]))
extra = [r for r in rects(sys.argv[2]) if r not in alone]
if not extra:
    sys.exit(0)
x0, x1, y0, y1 = max(extra, key=lambda r: (r[1] - r[0]) * (r[3] - r[2]))
print(x0, x1, y0, y1, (x0 + x1) // 2, y1)
PY
)

if [ -z "$quad" ]; then
    bad "the remote avatar puts a quad on screen the empty map does not"
else
    set -- $quad
    qw=$(( $2 - $1 ))
    qh=$(( $4 - $3 ))
    cx=$5
    cy=$6
    if [ "$qw" -ne 32 ]; then
        bad "the added quad is an overworld sprite cell (32 wide, got $qw)"
    else
        ok "the avatar the client spawned is a ${qw}x${qh} billboard at ($cx,$cy)"
    fi

    # The projection, with the label's lift off, so the reported point is the
    # sprite's own world position and nothing else.
    proj=$(grep "frame $FRAME label slot 0" "$tmp/peer.log" | tail -1 \
           | sed 's/.*screen (\([0-9-]*\),\([0-9-]*\)).*/\1 \2/')
    if [ -z "$proj" ]; then
        bad "the label path projected the avatar on frame $FRAME"
    else
        set -- $proj
        dx=$(( $1 - cx )); [ "$dx" -lt 0 ] && dx=$(( -dx ))
        dy=$(( $2 - cy )); [ "$dy" -lt 0 ] && dy=$(( -dy ))
        if [ "$dx" -le 2 ] && [ "$dy" -le 2 ]; then
            ok "the projected anchor ($1,$2) is where the avatar was drawn (off by $dx,$dy)"
        else
            bad "the projected anchor ($1,$2) is where the avatar was drawn (off by $dx,$dy)"
        fi
    fi
fi

# --- 2. the label is painted, above the head, on screen ---------------------

rc=0
boot lift OPENMMO_FAKE_ENTITY=1 OPENMMO_FAKE_NAME=Wanderer \
    OPENMMO_LABEL_REPORT=2 > "$tmp/lift.log" 2>&1 || rc=$?
if [ "$rc" -ne 0 ]; then
    bad "the game boots with the label at its own lift (exit $rc)"
    tail -3 "$tmp/lift.log"
    exit 1
fi

lifted=$(grep "frame $FRAME label slot 0" "$tmp/lift.log" | tail -1 \
         | sed 's/.*screen (\([0-9-]*\),\([0-9-]*\)).*/\2/')
if [ -n "$lifted" ] && [ -n "${cy:-}" ] && [ -n "${qh:-}" ]; then
    clear=$(( cy - qh - lifted ))
    if [ "$lifted" -ge 0 ] && [ "$clear" -ge 0 ] && [ "$clear" -le 12 ]; then
        ok "the default lift puts the anchor $clear pixels above the avatar's head"
    else
        bad "the default lift puts the anchor above the avatar's head (y=$lifted, head=$(( cy - qh )))"
    fi
else
    bad "the default lift puts the anchor above the avatar's head (no projection)"
fi

drawn=$(grep 'openmmo: labels ' "$tmp/lift.log" | tail -1 \
        | sed 's/openmmo: labels \([0-9]*\) drawn.*/\1/')
off=$(grep 'openmmo: labels ' "$tmp/lift.log" | tail -1 \
      | sed 's/.*drawn, \([0-9]*\) projected.*/\1/')
if [ "${drawn:-0}" -gt 0 ] && [ "${off:-1}" -eq 0 ]; then
    ok "the label reached the frame $drawn times, never off screen"
else
    bad "the label reached the frame (drawn ${drawn:-none}, off screen ${off:-none})"
fi

# --- 3. an accented name, through the engine's own font ---------------------

# Sixteen glyphs, eleven of them outside ASCII, the same name the trainer-name
# check uses, so a label and a save field are held to one standard.
rc=0
boot wide OPENMMO_FAKE_ENTITY=1 OPENMMO_FAKE_NAME='Ünïcödé Wandérer' \
    OPENMMO_LABEL_REPORT=1 OPENMMO_FONT_REPORT=1 OPENMMO_FONT=fatal \
    > "$tmp/wide.log" 2>&1 || rc=$?
if [ "$rc" -ne 0 ]; then
    bad "an accented name draws with no substituted glyph (exit $rc)"
    tail -3 "$tmp/wide.log"
else
    subs=$(grep 'openmmo: font .* glyphs drawn' "$tmp/wide.log" | tail -1 \
           | sed 's/.*drawn, \([0-9]*\) substitution.*/\1/')
    wdrawn=$(grep 'openmmo: labels ' "$tmp/wide.log" | tail -1 \
             | sed 's/openmmo: labels \([0-9]*\) drawn.*/\1/')
    if [ "${subs:-1}" -eq 0 ] && [ "${wdrawn:-0}" -gt 0 ]; then
        ok "an accented name draws over a player with no substituted glyph"
    else
        bad "an accented name draws over a player with no substituted glyph (${subs:-?} substituted, ${wdrawn:-0} labels)"
    fi
fi

# --- 4. an empty name still draws, as a visible "?" --------------------------

rc=0
boot empty OPENMMO_FAKE_ENTITY=1 OPENMMO_FAKE_NAME= \
    OPENMMO_LABEL_REPORT=1 > "$tmp/empty.log" 2>&1 || rc=$?
if [ "$rc" -ne 0 ]; then
    bad "an empty name still draws (exit $rc)"
    tail -3 "$tmp/empty.log"
else
    edrawn=$(grep 'openmmo: labels ' "$tmp/empty.log" | tail -1 \
             | sed 's/openmmo: labels \([0-9]*\) drawn.*/\1/')
    if [ "${edrawn:-0}" -gt 0 ] && grep -q 'label slot 0 "?"' "$tmp/empty.log"; then
        ok "an empty name draws as a visible ?"
    else
        bad "an empty name draws as a visible ? (drawn ${edrawn:-none})"
    fi
fi

# --- 5. the name sits on a catalog body, not only the trainer model ---------

rc=0
boot body OPENMMO_FAKE_ENTITY=1 OPENMMO_FAKE_NAME=Wanderer OPENMMO_FAKE_GFX=20 \
    OPENMMO_LABEL_REPORT=1 > "$tmp/body.log" 2>&1 || rc=$?
if [ "$rc" -ne 0 ]; then
    bad "a catalog body still carries a nameplate (exit $rc)"
    tail -3 "$tmp/body.log"
else
    bdrawn=$(grep 'openmmo: labels ' "$tmp/body.log" | tail -1 \
             | sed 's/openmmo: labels \([0-9]*\) drawn.*/\1/')
    if [ "${bdrawn:-0}" -gt 0 ] \
       && grep -q 'appearance slot 0 gfx .* -> 20' "$tmp/body.log" \
       && grep -q 'label slot 0 "Wanderer"' "$tmp/body.log"; then
        ok "a hiker body still has Wanderer over its head"
    else
        bad "a hiker body still has Wanderer over its head (drawn ${bdrawn:-none})"
    fi
fi

# --- 6. the plate is still white after a screen printed in another colour
# ----

count_px() { # PNG RRGGBB -> pixels of that colour on the upper screen
    python3 - "$1" "$2" <<'PIXPY'
import sys, zlib, struct

data = open(sys.argv[1], 'rb').read()
i, w, h, idat = 8, 0, 0, b''
while i < len(data):
    ln = struct.unpack('>I', data[i:i + 4])[0]
    typ = data[i + 4:i + 8]
    if typ == b'IHDR':
        w, h = struct.unpack('>II', data[i + 8:i + 16])
    elif typ == b'IDAT':
        idat += data[i + 8:i + 8 + ln]
    i += 12 + ln

raw = zlib.decompress(idat)
stride = 1 + w * 3
want = bytes(int(sys.argv[2][k:k + 2], 16) for k in (0, 2, 4))

# The upper screen is the first 192 rows, and the plate is drawn there. Filter
# type 0 on every row, so a scanline is just its bytes.
n = 0
for y in range(min(192, h)):
    row = raw[y * stride + 1:(y + 1) * stride]
    for x in range(w):
        if row[x * 3:x * 3 + 3] == want:
            n += 1
print(n)
PIXPY
}

# The letter and its shadow, as openmmo_label.c paints them.
LETTER=F8F8F8
SHADOW=303038

rc=0
boot glyph-on OPENMMO_FAKE_ENTITY=1 OPENMMO_FAKE_NAME=Wanderer \
    OPENMMO_GLYPH_CLOBBER=1 \
    PC_DUMP_FRAMES="$tmp/glyph-on" PC_DUMP_FROM=$FRAME \
    > "$tmp/glyph-on.log" 2>&1 || rc=$?
[ "$rc" -eq 0 ] || bad "the game boots with a clobbered glyph table (exit $rc)"

rc=0
boot glyph-off OPENMMO_FAKE_ENTITY=1 OPENMMO_FAKE_NAME=Wanderer \
    OPENMMO_GLYPH_CLOBBER=1 OPENMMO_GLYPH_COLORS=0 \
    PC_DUMP_FRAMES="$tmp/glyph-off" PC_DUMP_FROM=$FRAME \
    > "$tmp/glyph-off.log" 2>&1 || rc=$?
[ "$rc" -eq 0 ] || bad "the game boots with the answer turned off (exit $rc)"

shot_on=$(printf '%s/glyph-on/frame-%06d.png' "$tmp" "$FRAME")
shot_off=$(printf '%s/glyph-off/frame-%06d.png' "$tmp" "$FRAME")

if [ ! -f "$shot_on" ] || [ ! -f "$shot_off" ]; then
    bad "both clobbered boots dumped frame $FRAME"
else
    lit=$(count_px "$shot_on" "$LETTER")
    lit_off=$(count_px "$shot_off" "$LETTER")
    grey_off=$(count_px "$shot_off" "$SHADOW")

    if [ "${lit:-0}" -gt 0 ]; then
        ok "the plate is still white after a card was printed in another colour ($lit letter pixels)"
    else
        bad "the plate is still white after a card was printed in another colour (no letter pixels)"
    fi

    if [ "${lit_off:-1}" -eq 0 ] && [ "${grey_off:-0}" -gt 0 ]; then
        ok "and with the answer off it is the grey a joined session showed ($grey_off shadow pixels, no letter)"
    else
        bad "and with the answer off it is the grey a joined session showed (${lit_off:-?} letter, ${grey_off:-?} shadow)"
    fi
fi

if [ "$fail" -ne 0 ]; then
    echo "label: FAILED"
    exit 1
fi
echo "label: all checks passed"
