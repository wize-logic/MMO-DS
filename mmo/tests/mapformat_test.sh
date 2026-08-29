#!/bin/sh
# The design notes against the trees and the cartridge it
# counted.
set -eu

ROOT=${1:?usage: mapformat_test.sh <mmo-root> [engine-dir]}
REPO=$(CDPATH= cd -- "$ROOT/.." && pwd)
ENGINE=${2:-${ENGINE_DIR:-$ROOT/../engine/pokeplatinum}}
SRC5="${OPENMMO_GEN5_ROM:-$ENGINE/../pokeblack/baserom.nds}"
# The Gen 4 image the porter reads. HeartGold and SoulSilver share these
# archives byte for byte, so either answers.
SRC4="${OPENMMO_GEN4_ROM:-$REPO/roms/pokesoulsilver.nds}"
PORTER="$ROOT/tools/modport.sh"

DOC="$ROOT/MAPFORMATS.md"
if [ ! -f "$DOC" ]; then
    echo "mapformat: SKIP (no $DOC)"
    exit 0
fi
# The maintained checkouts when they are on this machine, the submodules when
# they are not; tools/decomp_dir.sh is the rule and the header line below says
# which tree each count came out of, because a count with no path is how one
# checkout's measurement became a claim about another.
PL=$("$ROOT/tools/decomp_dir.sh" pokeplatinum 2>/dev/null || echo "")
HG=$("$ROOT/tools/decomp_dir.sh" pokeheartgold 2>/dev/null || echo "")

PL_SCRCMD="$PL/include/data/scripts/scrcmd.h"
HG_SCRCMD="$HG/src/data/fieldmap/script_cmd_table.h"
PL_HEADERS="$PL/include/data/map_headers.h"
HG_HEADERS="$HG/src/data/map_headers.h"
PL_ATTRS="$PL/include/constants/field/map.h"
HG_ATTRS="$HG/include/terrain_attributes.h"
PL_MATRIX="$PL/include/constants/field/map_matrix.h"
HG_MATRIX="$HG/include/map_matrix.h"
PL_LAND="$PL/src/overlay005/land_data.c"

fail=0
echo "the map-format page, the trees it counted and the cartridge agree:"
echo "  (platinum $PL)"
echo "  (heartgold $HG)"

ok()  { echo "  ok   $1"; }
bad() { echo "  FAIL $1"; fail=$((fail + 1)); }

# The terrain verdict on that page was wrong once because it was measured
# against the frozen submodules while the repo read the maintained trees. The
# guard against that happening again is that the enums the new verdict rests on
# have to still be there, in the tree this run actually read.
check_enum() {
    if [ -f "$2" ] && grep -q "$3" "$2"; then
        ok "$1"
    else
        bad "$1 (looked in $2)"
    fi
}
check_enum "platinum still names its tile behaviours" \
    "$PL/include/constants/field/map_tile_behaviors.h" "enum TileBehavior"
check_enum "heartgold still names its tile behaviours" \
    "$HG/include/constants/metatile_behavior.h" "enum TILE_BEHAVIOR"
if grep -q "26 guesses about what a tile does" "$DOC" \
        && ! grep -q "That was true of the pinned submodules" "$DOC"; then
    bad "the page's terrain verdict still says a remap table has no oracle"
else
    ok "the page's terrain verdict names the trees it was measured against"
fi

# The page is prose and wraps where it likes, so match it as one line with runs
# of whitespace squeezed: a sentence that reflows is not a drift.
flat=$(tr '\n' ' ' < "$DOC" | tr -s ' ')

says() {
    case "$flat" in
    *"$1"*) ok "$2" ;;
    *)      bad "$2 (the page does not say \"$1\")" ;;
    esac
}

same() {
    if [ "$2" = "$3" ]; then ok "$1 is $3"
    else bad "$1: the page says $3, the checkout says $2"; fi
}

# --- the answer, and what each turned-down kind is turned down for ---
says "answered again on 2026-08-21 with" "the page says the converter exists"
says "Goldenrod City is in the game" "the page names the map that crossed"
says "That is a gap and it is named as one" "the page separates a gap from a refusal"

if grep -q '^    map)' "$PORTER" && grep -q '^    text)' "$PORTER"; then
    ok "the porter driver answers for map and text separately"
else
    bad "tools/modport.sh no longer answers for map and text separately"
fi
if grep -q 'MAPFORMATS.md' "$PORTER"; then
    ok "and both answers point at the measurement"
else
    bad "the porter no longer cites MAPFORMATS.md"
fi
if grep -q 'portmap.py' "$PORTER"; then
    ok "and the map answer names the tool that does port one"
else
    bad "the map answer does not name tools/portmap.py"
fi

# The live half of that: a recipe line asking for a map is turned down. The
# driver identifies the image before it reads a line, so this needs a cartridge,
# any cartridge whose code the line names.
echo "a recipe that asks for a map is turned down:"
if [ ! -f "$SRC5" ]; then
    echo "  SKIP (no cartridge at $SRC5; set OPENMMO_GEN5_ROM)"
elif ! command -v python3 > /dev/null 2>&1; then
    echo "  SKIP (no python3 to run the porter)"
else
    tmp=$(mktemp -d)
    trap 'rm -rf "$tmp"' EXIT
    mkdir -p "$tmp/pkg" "$tmp/out"
    printf 'id = "probe"\n' > "$tmp/pkg/mod.toml"
    printf 'IRBO  map  nimbasa  jubilife\n' > "$tmp/pkg/port.recipe"
    if sh "$PORTER" "$ENGINE" "$tmp/pkg" "$SRC5" "$tmp/out" \
            > "$tmp/log" 2>&1; then
        bad "a map line is turned down"
    elif grep -q "not a kind this driver carries" "$tmp/log" \
            && grep -q "portmap.py" "$tmp/log" \
            && [ -z "$(ls -A "$tmp/out")" ]; then
        ok "a map line is turned down by name, naming the tool, writing nothing"
    else
        bad "a map line is turned down by name, naming the tool, writing nothing"
        tail -3 "$tmp/log"
    fi
fi

# --- which prop archive a map's ids index ---
#
# The page says nothing in the area record decides it and the area's own prop
# texture set does. That is the claim a Pokemon Center full of gym walls came
# from, so it is counted here rather than remembered.
echo "the prop archive a map's ids index is what its texture set can dress:"
if [ ! -f "$SRC4" ]; then
    echo "  SKIP (no Gen 4 cartridge at $SRC4; set OPENMMO_GEN4_ROM)"
elif ! command -v python3 > /dev/null 2>&1 || [ ! -f "$ENGINE/pc/modport.py" ]; then
    echo "  SKIP (no python3 or no engine reader)"
elif [ -z "$HG" ] || [ ! -d "$HG" ]; then
    echo "  SKIP (no heartgold checkout)"
else
    arc=$(python3 - "$ENGINE" "$SRC4" "$HG" "$ROOT" <<'PYEOF'
import re, struct, sys
sys.path.insert(0, sys.argv[1] + "/pc")
sys.path.insert(0, sys.argv[4] + "/tools")
from pathlib import Path
from modport import NitroRom
import nsbtx, portmap

rom = NitroRom(Path(sys.argv[2]))
hg = sys.argv[3]
land = rom.narc_members("a/0/6/5")
mats = rom.narc_members("a/0/4/1")
area = rom.narc_members("a/0/4/2")
ptex = rom.narc_members("a/0/7/0")
arcs = {"field": rom.narc_members("a/0/4/0"), "room": rom.narc_members("a/1/4/8")}
ids = {}
for m in re.finditer(r"#define\s+MAP_(\w+)\s+(\d+)", open(hg + "/include/constants/maps.h").read()):
    ids.setdefault(m.group(1), int(m.group(2)))
tally = {"field": 0, "room": 0, "both": 0, "neither": 0}
for m in re.finditer(r"\[MAP_(\w+)\]\s*=\s*\{(.*?)\n\s*\}",
                     open(hg + "/src/data/map_headers.h").read(), re.S):
    name = m.group(1)
    ab = re.search(r"\.areaDataBank\s*=\s*(\d+)", m.group(2))
    mi = re.search(r"\.matrixId\s*=\s*NARC_map_matrix_map_matrix_(\d+)_", m.group(2))
    if not ab or not mi or name not in ids:
        continue
    try:
        mat = portmap.parse_matrix(mats[int(mi.group(1))])
    except Exception:
        continue
    shared = int(mi.group(1)) == 0
    chunks = {mat["land"][i] for i in range(mat["w"] * mat["h"])
              if mat["land"][i] != 0xFFFF and (not shared or mat["hdrs"][i] == ids[name])}
    placed = set()
    for c in chunks:
        if c < len(land):
            placed |= set(portmap.prop_model_ids(land[c]))
    bank = int(ab.group(1))
    if not placed or bank >= len(area):
        continue
    f0 = struct.unpack("<4H", area[bank])[0]
    if f0 >= len(ptex):
        continue
    have = set(nsbtx.read(ptex[f0])["textures"])
    fits = []
    for k, arr in arcs.items():
        want = set()
        for i in placed:
            if i >= len(arr):
                want = None
                break
            want |= nsbtx.model_texture_names(arr[i])
        if want and not (want - have):
            fits.append(k)
    tally["both" if len(fits) == 2 else (fits[0] if fits else "neither")] += 1
for k in ("field", "room", "both", "neither"):
    print("%s %d" % (k, tally[k]))
PYEOF
    ) || arc=""
    one=$(echo "$arc" | awk '/^field|^room/ {n+=$2} END {print n+0}')
    amb=$(echo "$arc" | awk '/^both|^neither/ {n+=$2} END {print n+0}')
    if [ -n "$one" ] && [ "$one" -ge 400 ] && [ "$amb" -le 5 ]; then
        ok "$one maps are covered by exactly one archive and only $amb are ambiguous"
    else
        bad "the texture set no longer separates the two prop archives ($one clean, $amb ambiguous)"
    fi
fi

# --- the land-data section order, re-derived out of the cartridge ---
echo "the land-data section order is what the cartridge's own events say:"
if [ ! -f "$SRC4" ]; then
    echo "  SKIP (no Gen 4 cartridge at $SRC4; set OPENMMO_GEN4_ROM)"
elif ! command -v python3 > /dev/null 2>&1 || [ ! -f "$ENGINE/pc/modport.py" ]; then
    echo "  SKIP (no python3 or no engine reader)"
elif [ -z "$HG" ] || [ ! -d "$HG" ]; then
    echo "  SKIP (no heartgold checkout)"
else
    ord=$(python3 - "$ENGINE" "$SRC4" "$HG" "$ROOT" <<'PYEOF'
import glob, json, os, re, struct, sys
sys.path.insert(0, sys.argv[1] + "/pc")
sys.path.insert(0, sys.argv[4] + "/tools")
from pathlib import Path
from modport import NitroRom
import portmap

rom = NitroRom(Path(sys.argv[2]))
hg = sys.argv[3]
land = rom.narc_members("a/0/6/5")
mats = rom.narc_members("a/0/4/1")
names, v = {}, 0
for n, val in re.findall(r"(TILE_BEHAVIOR_\w+)\s*(?:=\s*(0x[0-9A-Fa-f]+|\d+))?\s*,",
                         open(hg + "/include/constants/metatile_behavior.h").read()):
    if val:
        v = int(val, 0)
    names[v] = n
    v += 1
warpish = {b for b, n in names.items()
           if any(k in n for k in ("WARP", "DOOR", "STAIR", "ESCALATOR", "LADDER", "ENTRANCE"))}
byev = {os.path.basename(p)[:-5]: p
        for p in glob.glob(hg + "/files/fielddata/eventdata/zone_event/*.json")}


def beh(mat, gx, gz, plus):
    cx, cz = gx // 32, gz // 32
    if cx >= mat["w"] or cz >= mat["h"]:
        return None
    cid = mat["land"][cz * mat["w"] + cx]
    if cid == 0xFFFF or cid >= len(land):
        return None
    m = land[cid]
    if struct.unpack_from("<I", m, 0)[0] != 0x800:
        return None
    fifth = struct.unpack_from("<H", m, 0x12)[0]
    off = 0x14 + (fifth if plus else 0)
    return struct.unpack_from("<H", m, off + 2 * ((gz % 32) * 32 + (gx % 32)))[0] & 0xFF


hit = [0, 0]
tot = 0
for m in re.finditer(r"\[MAP_(\w+)\]\s*=\s*\{(.*?)\n\s*\}",
                     open(hg + "/src/data/map_headers.h").read(), re.S):
    mi = re.search(r"\.matrixId\s*=\s*NARC_map_matrix_map_matrix_(\d+)_", m.group(2))
    ev = re.search(r"\.eventsBank\s*=\s*NARC_zone_event_(\w+)_bin", m.group(2))
    if not mi or not ev or ev.group(1) not in byev:
        continue
    try:
        mat = portmap.parse_matrix(mats[int(mi.group(1))])
    except Exception:
        continue
    for w in json.load(open(byev[ev.group(1)])).get("warps", []):
        a, b = beh(mat, w["x"], w["z"], False), beh(mat, w["x"], w["z"], True)
        if a is None or b is None:
            continue
        tot += 1
        hit[0] += a in warpish
        hit[1] += b in warpish
print("plain %d" % round(100 * hit[0] / tot))
print("fifth %d" % round(100 * hit[1] / tot))
PYEOF
    ) || ord=""
    plain=$(echo "$ord" | awk '/^plain/ {print $2}')
    fifth=$(echo "$ord" | awk '/^fifth/ {print $2}')
    if [ -n "$fifth" ] && [ "$fifth" -ge 95 ] && [ "$plain" -lt "$fifth" ]; then
        ok "a warp lands on a warp tile ${fifth}% of the time behind the fifth section, ${plain}% before it"
    else
        bad "the terrain is no longer behind the fifth section (${plain}% vs ${fifth}%)"
    fi
fi

# --- the events half, re-derived out of the two trees ---
echo "the events half of the page is what the two trees say:"
if [ ! -f "$PL_HEADERS" ] || [ ! -f "$HG_HEADERS" ]; then
    echo "  SKIP (a checkout is missing)"
elif ! command -v python3 > /dev/null 2>&1; then
    echo "  SKIP (no python3)"
else
    ev=$(python3 - "$PL" "$HG" <<'PYEOF'
import re, sys
pl, hg = sys.argv[1], sys.argv[2]

def fields(path, name):
    text = open(path).read()
    m = re.search(r"struct %s \{(.*?)\n\}" % name, text, re.S)
    if not m:
        return None
    out = []
    for line in m.group(1).splitlines():
        line = line.split("//")[0].strip().rstrip(";")
        if not line:
            continue
        parts = line.split()
        if len(parts) < 2:
            continue
        ty = parts[0]
        for nm in " ".join(parts[1:]).split(","):
            nm = nm.strip()
            if not nm:
                continue
            n = 1
            arr = re.search(r"\[(\d+)\]", nm)
            if arr:
                n = int(arr.group(1))
            out.append((ty, n))
    return out

W = {"u8": 1, "s8": 1, "u16": 2, "s16": 2, "u32": 4, "s32": 4, "int": 4,
     "fx32": 4}
pairs = [("BgEvent", 20), ("ObjectEvent", 32), ("WarpEvent", 12),
         ("CoordEvent", 16)]
same = 0
for name, want in pairs:
    a = fields(hg + "/include/map_events_internal.h", name)
    b = fields(pl + "/include/map_header_data.h", name)
    if a is None or b is None:
        continue
    wa = sum(W.get(t, 0) * n for t, n in a)
    wb = sum(W.get(t, 0) * n for t, n in b)
    # Platinum spells its padding out where HeartGold lets the compiler add it,
    # so a record is compared at its aligned width rather than its written one.
    wa += -wa % 4
    wb += -wb % 4
    if wa == wb == want:
        same += 1
print("structs %d" % same)

mt = [l for l in open(pl + "/generated/movement_types.txt") if l.strip()]
import json, glob
worst = -1
for f in glob.glob(hg + "/files/fielddata/eventdata/zone_event/*.json"):
    for o in json.load(open(f)).get("objects", []):
        worst = max(worst, o["movement"])
print("movement %d" % (1 if worst < len(mt) else 0))

def defs(path, prefix):
    return {m.group(1) for m in
            re.finditer(r"#define\s+%s(\w+)\s+\d+" % prefix,
                        open(path).read())}
sp = defs(hg + "/include/constants/sprites.h", "SPRITE_")
mm = defs(hg + "/include/constants/mmodel.h", "MMODEL_")
print("sprites %d" % len(sp & mm))
PYEOF
    ) || ev=""
    same "the four event structs that agree at the same width" \
        "$(echo "$ev" | awk '/^structs/ {print $2}')" 4
    same "every Johto movement value inside Platinum's table" \
        "$(echo "$ev" | awk '/^movement/ {print $2}')" 1
    same "sprite ids with an mmodel member of the same name" \
        "$(echo "$ev" | awk '/^sprites/ {print $2}')" 832
fi

# --- the Gen 5 numbers, re-derived out of the cartridge ---
echo "the Gen 5 half of the page is what the cartridge says:"
if [ ! -f "$SRC5" ]; then
    echo "  SKIP (no Gen 5 cartridge at $SRC5; set OPENMMO_GEN5_ROM)"
elif ! command -v python3 > /dev/null 2>&1; then
    echo "  SKIP (no python3 to run the reader)"
elif [ ! -f "$ENGINE/pc/modport.py" ]; then
    echo "  SKIP (no engine reader at $ENGINE/pc/modport.py)"
else
    probe=$(python3 - "$ENGINE" "$SRC5" <<'PYEOF'
import struct
import sys
from pathlib import Path

sys.path.insert(0, sys.argv[1] + "/pc")
from modport import NitroRom          # the port's reader; never write one here

rom = NitroRom(Path(sys.argv[2]))
print("code=%s" % rom.code)

narcs = [n for n, (off, size) in rom.files.items()
         if size >= 16 and rom.data[off:off + 4] == b"NARC"]
print("narcs=%d" % len(narcs))

# Gen 4's land-data container: four u32 sizes and the sections after them.
containers = 0
for name in narcs:
    try:
        members = rom.narc_members(name)
    except SystemExit:
        continue
    if len(members) < 50:
        continue
    hits = 0
    for m in members:
        if len(m) < 0x10:
            continue
        a, b, c, d = struct.unpack_from("<4I", m, 0)
        if a + b + c + d + 0x10 == len(m):
            hits += 1
    if hits > len(members) // 2:
        containers += 1
print("gen4containers=%d" % containers)

members = rom.narc_members("a/0/0/8")
print("mapmembers=%d" % len(members))

models = grids = tiles = 0
bits = {0: 0, 15: 0, 16: 0, 23: 0}
for m in members:
    sec = struct.unpack_from("<4I", m, 4)
    if m[sec[0]:sec[0] + 4] == b"BMD0":
        models += 1
    if sec[2] - sec[1] != 8196:
        continue
    if struct.unpack_from("<HH", m, sec[1]) != (32, 32):
        continue
    grids += 1
    values = struct.unpack_from("<2048I", m, sec[1] + 4)
    tiles += 2048
    for bit in bits:
        mask = 1 << bit
        bits[bit] += sum(1 for v in values if v & mask)
print("models=%d" % models)
print("grids=%d" % grids)
print("tiles=%d" % tiles)
for bit in sorted(bits):
    print("bit%d=%.1f" % (bit, 100.0 * bits[bit] / tiles))
PYEOF
) || probe=""

    field() { echo "$probe" | sed -n "s/^$1=//p"; }

    if [ -z "$probe" ]; then
        bad "the Gen 5 cartridge reads through the port's own reader"
    else
        same "the cartridge's game code" "$(field code)" "IRBO"
        same "Black's NARC count" "$(field narcs)" "237"
        same "archives holding Platinum's land-data container" \
             "$(field gen4containers)" "0"
        says "**Black has none**" "the page says no archive holds it"
        same "members in a/0/0/8" "$(field mapmembers)" "649"
        same "members whose first section is a model" "$(field models)" "649"
        same "members with a readable 32x32 grid" "$(field grids)" "384"
        same "tiles counted over them" "$(field tiles)" "786432"

        # The whole point of the section: Gen 4's collision bit is dead here,
        # and three others are live at a plausible density with nothing to
        # choose between them.
        same "Gen 4's collision bit, in Black" "$(field bit15)" "0.0"
        same "Black tiles with bit 0 set" "$(field bit0)" "41.3"
        same "Black tiles with bit 16 set" "$(field bit16)" "47.2"
        same "Black tiles with bit 23 set" "$(field bit23)" "52.4"
    fi
fi


for f in "$DOC" "$PL_SCRCMD" "$HG_SCRCMD" "$PL_HEADERS" "$HG_HEADERS" \
         "$PL_ATTRS" "$HG_ATTRS" "$PL_MATRIX" "$HG_MATRIX" "$PL_LAND"; do
    if [ ! -f "$f" ]; then
        echo "mapformat: SKIP (no $f, the decomp half only)"
        if [ "$fail" -eq 0 ]; then
            echo "mapformat: all checks passed (decomp half skipped)"
        else
            echo "mapformat: $fail check(s) FAILED"
        fi
        exit "$fail"
    fi
done

# --- terrain attributes: the four-byte step that starts the whole comparison ---
pl_off=$(awk '/define TERRAIN_ATTRIBUTES_OFFSET/ { print $3 }' "$PL_ATTRS")
hg_off=$(awk '/define TERRAIN_ATTRIBUTES_OFFSET/ { print $3 }' "$HG_ATTRS")
same "Platinum's terrain-attribute offset" "$pl_off" "0x10"
same "HeartGold's terrain-attribute offset" "$hg_off" "0x14"
pl_sz=$(awk '/define TERRAIN_ATTRIBUTES_SIZE/ { print $3 }' "$PL_ATTRS")
hg_sz=$(awk '/define TERRAIN_ATTRIBUTES_SIZE/ { print $3 }' "$HG_ATTRS")
same "Platinum's terrain-attribute block" "$pl_sz" "0x800"
same "HeartGold's terrain-attribute block" "$hg_sz" "0x800"
says "0x800 bytes, 32 × 32 \`u16\`" "the page still calls the block 32 by 32"

# The behaviour/collision split the page calls free is Platinum's own.
grep -q 'define TERRAIN_ATTRIBUTES_COLLISION_SHIFT *15' "$PL_ATTRS" \
    && ok "the collision bit is still 15" \
    || bad "Platinum moved TERRAIN_ATTRIBUTES_COLLISION_SHIFT off 15"
grep -q 'define TERRAIN_ATTRIBUTES_TILE_BEHAVIOR_MASK *0xFF' "$PL_ATTRS" \
    && ok "the behaviour mask is still the low byte" \
    || bad "Platinum moved TERRAIN_ATTRIBUTES_TILE_BEHAVIOR_MASK off 0xFF"

# This used to be a tripwire waiting for a terrain-behaviour enum to appear,
# because while neither tree named a value the page's "no oracle" held.
strays=$(cat "$PL_ATTRS" "$HG_ATTRS" 2>/dev/null \
         | grep -c 'TILE_BEHAVIOR_' || true)
masks=$(cat "$PL_ATTRS" "$HG_ATTRS" 2>/dev/null \
        | grep -c 'TERRAIN_ATTRIBUTES_TILE_BEHAVIOR_MASK' || true)
same "behaviour names defined beside the attribute layout" \
     "$((strays - masks))" "0"

# --- land data: the header order the page prints, read back out of the loader ---
order=$(sed -n '/LandDataHeader_Load(NARC/,/^}/p' "$PL_LAND" \
        | sed -n 's/.*landDataHeader->\([A-Za-z]*\)Size.*/\1/p' | tr '\n' ' ')
same "Platinum's land-data header order" \
     "$(echo $order)" "terrainAttributes mapProps mapModel bdhc"

# --- matrix: the one ceiling the page says is in the engine, not the data ---
pl_w=$(awk '/define MAP_MATRIX_MAX_WIDTH/ { print $3 }' "$PL_MATRIX")
hg_max=$(awk '/define MAP_MATRIX_MAX_SIZE/ { print $3 }' "$HG_MATRIX")
same "Platinum's matrix width cap" "$pl_w" "30"
same "HeartGold's matrix cell cap" "$hg_max" "799"
says "47 × 17 = 799 cells" "the page still names HeartGold's world matrix"

# --- script commands: the count that says there is no conversion ---
pl_cmds=$(grep -c '^ *ScriptCommand(' "$PL_SCRCMD")
hg_cmds=$(sed 's,//.*,,' "$HG_SCRCMD" | grep -c '^ *[A-Za-z_][A-Za-z0-9_]*, *$')
same "Platinum's script command count" "$pl_cmds" "840"
same "HeartGold's script command count" "$hg_cmds" "853"

# Same handler name at the same opcode, compared position for position.
sed -n 's/^ *ScriptCommand( *[A-Za-z0-9_]* *, *ScrCmd_\([A-Za-z0-9_]*\) *).*/\1/p' \
    "$PL_SCRCMD" | tr 'A-Z' 'a-z' > "${TMPDIR:-/tmp}/mapfmt.pl.$$"
sed 's,//.*,,' "$HG_SCRCMD" \
    | sed -n 's/^ *ScrCmd_\([A-Za-z0-9_]*\), *$/\1/p' | tr 'A-Z' 'a-z' \
    > "${TMPDIR:-/tmp}/mapfmt.hg.$$"
agree=$(paste "${TMPDIR:-/tmp}/mapfmt.pl.$$" "${TMPDIR:-/tmp}/mapfmt.hg.$$" \
        | awk -F'\t' '$1 != "" && $1 == $2' | wc -l | tr -d ' ')
rm -f "${TMPDIR:-/tmp}/mapfmt.pl.$$" "${TMPDIR:-/tmp}/mapfmt.hg.$$"
same "opcodes holding the same handler in both" "$agree" "26"

# --- map headers: the counts the table publishes ---
pl_maps=$(grep -c '^ *\[MAP_[A-Z0-9_]*\] *= *{' "$PL_HEADERS")
hg_maps=$(grep -c '^ *\[MAP_[A-Z0-9_]*\] *= *{' "$HG_HEADERS")
same "Platinum's map header count" "$pl_maps" "593"
same "HeartGold's map header count" "$hg_maps" "540"

# --- the one field at the same offset, which is the whole map-header claim ---
grep -q 'u16 eventsArchiveID;' "$PL/include/map_header.h" \
    && grep -q 'u16 eventsBank;' "$HG/include/map_header.h" \
    && ok "both map headers still carry an events bank" \
    || bad "a map header renamed its events bank; the 0x10 row is stale"

# --- the three tables a map port reads besides TERRAIN_MAP ---
#
# Same gate, same reason: each is generated out of the HeartGold tree and read
# by tools/portmap.py, and a checkout that renames a map, a sprite or a flag has
# to show up here rather than in a ported map that spawns the wrong person.
for table in MAPS:gen_maps.py SPRITES:gen_sprites.py MAPSCENES:gen_mapscenes.py
do
    name=${table%%:*}
    tool=${table##*:}
    if [ ! -f "$ROOT/$name" ]; then
        bad "mmo/$name is missing; a map port reads it"
    elif ! command -v python3 >/dev/null 2>&1; then
        ok "$name left unchecked (no python3 to regenerate it with)"
    elif [ -z "$HG" ] || [ ! -d "$HG" ]; then
        echo "  SKIP ($name; no heartgold checkout)"
    else
        gen="${TMPDIR:-/tmp}/mapfmt.$name.$$"
        if python3 "$ROOT/tools/$tool" "$HG" "$gen" >/dev/null 2>&1; then
            if cmp -s "$ROOT/$name" "$gen"; then
                ok "$name still matches the tree it was generated from"
            else
                bad "$name is stale; rerun tools/$tool"
            fi
        else
            bad "tools/$tool refused"
        fi
        rm -f "$gen"
    fi
done

# --- TERRAIN_MAP: the table that replaced "26 guesses" ---
TMAP="$ROOT/TERRAIN_MAP"
if [ ! -f "$TMAP" ]; then
    bad "mmo/TERRAIN_MAP is missing; the terrain verdict rests on it"
elif ! command -v python3 >/dev/null 2>&1; then
    ok "TERRAIN_MAP left unchecked (no python3 to regenerate it with)"
else
    gen="${TMPDIR:-/tmp}/mapfmt.terrain.$$"
    if python3 "$ROOT/tools/gen_terrain_map.py" "$PL" "$HG" "$gen" >/dev/null 2>&1
    then
        if cmp -s "$TMAP" "$gen"; then
            ok "TERRAIN_MAP still matches the two trees it was generated from"
        else
            bad "TERRAIN_MAP is stale; rerun tools/gen_terrain_map.py"
        fi
    else
        bad "tools/gen_terrain_map.py refused (an alias lost its evidence?)"
    fi
    rm -f "$gen"
fi

# Exactly one value is unsafe to carry, and it is the one the page names. This
# is the whole difference between "a table exists" and "a map can cross": a
# second unsafe row would be a new decision nobody has made.
unsafe=$(awk '$3 == "hgonly-unsafe" { print $1 }' "$TMAP" 2>/dev/null | tr '\n' ' ')
same "the values unsafe to carry across" "$(echo $unsafe)" "0x2C"

# Every alias is justified by both trees naming the same number, so a row whose
# two columns disagree means the generator's evidence moved.
badalias=$(awk '$3 ~ /^alias-/ && $1 != $2 { print $1 }' "$TMAP" 2>/dev/null | wc -l | tr -d ' ')
same "aliases that changed a tile's number" "$badalias" "0"

# The table has to cover the maps it exists for. Reading Johto's land data is
# the only check here that touches a cartridge's bytes, so it is skipped rather
# than failed when the extracted filesystem is not on this machine.
HG_LAND="$HG/files/a/0/6/5"
if [ ! -f "$HG_LAND" ] || [ ! -f "$TMAP" ] || ! command -v python3 >/dev/null 2>&1
then
    ok "Johto coverage left unchecked (no extracted a/0/6/5 here)"
else
    miss=$(python3 - "$HG_LAND" "$TMAP" <<'EOF'
import re, struct, sys
from pathlib import Path
d = Path(sys.argv[1]).read_bytes()
off = struct.unpack_from("<H", d, 12)[0]
n = struct.unpack_from("<H", d, off + 8)[0]
ents = [struct.unpack_from("<II", d, off + 12 + i * 8) for i in range(n)]
btnf = off + struct.unpack_from("<I", d, off + 4)[0]
base = btnf + struct.unpack_from("<I", d, btnf + 4)[0] + 8
have = {int(m.group(1), 16) for m in
        (re.match(r"^0x([0-9A-F]{2})\s", l)
         for l in Path(sys.argv[2]).read_text().splitlines()) if m}
used = set()
for s, e in ents:
    ta = d[base + s + 0x14:base + s + 0x14 + 0x800]
    if len(ta) < 0x800:
        continue
    for i in range(0, 0x800, 2):
        used.add((ta[i] | (ta[i + 1] << 8)) & 0xFF)
print(len(used - have))
EOF
)
    same "Johto behaviour bytes with no row in TERRAIN_MAP" "$miss" "0"
fi

if [ "$fail" -eq 0 ]; then
    echo "mapformat: all checks passed"
else
    echo "mapformat: $fail check(s) FAILED"
fi
exit "$fail"
