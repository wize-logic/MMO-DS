#!/bin/sh
# Host-surface text is the ROM font's 16px cell.
set -eu

ROOT=${1:?usage: font_test.sh <mmo-root> [build-dir] [engine-dir]}
BUILD=${2:-}
ENGINE=${3:-}

LIMITS="$ROOT/ENGINE_LIMITS.md"
BOOT="$ROOT/mods/openmmo/src/openmmo_boot.c"
FONT="$ROOT/mods/openmmo/src/openmmo_font.c"

for f in "$LIMITS" "$BOOT" "$FONT"; do
    if [ ! -f "$f" ]; then
        echo "font: SKIP (no $f)"
        exit 0
    fi
done

fail=0
ok()  { echo "  ok   $1"; }
bad() { echo "  FAIL $1"; echo "       $2"; fail=1; }

echo "host-surface text is the ROM font's 16px cell:"

flat=$(tr '\n' ' ' < "$LIMITS" | tr -s ' ')
says() {
    if printf '%s\n' "$flat" | grep -Fq "$2"; then
        ok "$1"
    else
        bad "$1" "ENGINE_LIMITS.md does not say: $2"
    fi
}

says "the page still names the ROM cell" "**16px** cell"
says "the page still files host-surface text as the ROM font" "ROM font"
says "the page still refuses a second 5x7" "not a second 5x7"

cell=$(sed -n 's/^#define OPENMMO_FONT_CELL_H[[:space:]]\{1,\}\([0-9]\{1,\}\).*/\1/p' "$FONT" | head -1)
if [ "$cell" = "16" ]; then
    ok "OPENMMO_FONT_CELL_H is still 16"
else
    bad "OPENMMO_FONT_CELL_H is still 16" \
        "openmmo_font.c defines OPENMMO_FONT_CELL_H as ${cell:-missing}"
fi

if grep -q 'openmmo_font_draw_utf8' "$BOOT"; then
    ok "the HUD still draws through openmmo_font_draw_utf8"
else
    bad "the HUD still draws through openmmo_font_draw_utf8" \
        "openmmo_boot.c no longer calls openmmo_font_draw_utf8"
fi

if grep -q 'unsigned char rows\[7\]' "$BOOT"; then
    bad "the 5x7 table is gone from the game surface" \
        "openmmo_boot.c still has a 7-row bitmap table"
else
    ok "the 5x7 table is gone from the game surface"
fi

if grep -q 'px >= PC_VIDEO_WIDTH' "$FONT"; then
    ok "the ROM blit still clips at PC_VIDEO_WIDTH"
else
    bad "the ROM blit still clips at PC_VIDEO_WIDTH" \
        "openmmo_font.c no longer clips a glyph at PC_VIDEO_WIDTH"
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
    echo "font: SKIP live half (no fused build or ROM)"
    if [ "$fail" -eq 0 ]; then
        echo "font: all checks passed"
    else
        echo "font: FAILED"
    fi
    exit "$fail"
fi

echo "ENGINE_DIR=$ENGINE"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

env OPENMMO_ASSERT=warn OPENMMO_SESSION=1 OPENMMO_SERVER=127.0.0.1:1 \
    PC_ROM="$ROM" PC_SAVE="$tmp/run.sav" PC_FRAMES=200 PC_PACE=0 \
    PC_DUMP_FRAMES="$tmp/hud" PC_DUMP_FROM=200 \
    "$FUSED" > "$tmp/hud.log" 2>&1 && rc=0 || rc=$?

if [ "$rc" -ne 0 ]; then
    bad "a session title still boots (exit $rc)" "see $tmp/hud.log"
    tail -3 "$tmp/hud.log"
elif grep -q 'openmmo: session title' "$tmp/hud.log"; then
    ok "a session title still boots"
else
    bad "a session title still boots" "no 'session title' in the log"
fi

shot=$(printf '%s/hud/frame-%06d.png' "$tmp" 200)
if [ ! -f "$shot" ]; then
    bad "frame 200 of the session title is still there" "no $shot"
else
    counts=$(python3 - "$shot" <<'PIXPY'
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
shadow = bytes((0x30, 0x30, 0x38))
letter = bytes((0xFF, 0x50, 0x50))
sh = lt = 0
ymin = ymax = None
for y in range(min(40, h)):
    row = raw[y * stride + 1:(y + 1) * stride]
    for x in range(w):
        pix = row[x * 3:x * 3 + 3]
        if pix == shadow:
            sh += 1
            if ymin is None or y < ymin:
                ymin = y
            if ymax is None or y > ymax:
                ymax = y
        elif pix == letter:
            lt += 1
            if ymin is None or y < ymin:
                ymin = y
            if ymax is None or y > ymax:
                ymax = y
print("%d %d %s %s" % (sh, lt, ymin if ymin is not None else -1,
                       (ymax - ymin + 1) if ymin is not None else 0))
PIXPY
)
    sh=$(echo "$counts" | awk '{print $1}')
    lt=$(echo "$counts" | awk '{print $2}')
    hpx=$(echo "$counts" | awk '{print $4}')
    # The 5x7 overlay painted FAILED in ff5050 with no 303038 shadow and
    # a 14px (scale-2) letter. ROM glyphs have a shadow and a 16px cell.
    if [ "${sh:-0}" -gt 0 ] && [ "${lt:-0}" -gt 0 ]; then
        ok "the NET line is still ROM-font (shadow $sh, letter $lt, span $hpx)"
    else
        bad "the NET line is still ROM-font" \
            "y0-39: shadow=${sh:-?} letter=${lt:-?} (want both > 0)"
    fi
    if [ "${hpx:-0}" -ge 16 ]; then
        ok "the NET strip is still at least a 16px cell ($hpx)"
    else
        bad "the NET strip is still at least a 16px cell" \
            "letter+shadow span ${hpx:-?} (want >= 16)"
    fi
fi

if [ "$fail" -ne 0 ]; then
    echo "font: FAILED"
    exit 1
fi
echo "font: all checks passed"
exit 0
