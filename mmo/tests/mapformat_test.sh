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
# A checkout that is not here still names itself in every path built out of it,
# so a skipped block says which tree it wanted rather than naming a file at the
# root of the filesystem.
PL=${PL:-no-pokeplatinum-checkout}
HG=${HG:-no-pokeheartgold-checkout}

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

# have <file>..., every file named is readable, and $absent holds the first
# that was not. A block reading a decomp tree guards on this and SKIPs alone.
absent=
have() {
    for f in "$@"; do
        if [ ! -f "$f" ]; then absent=$f; return 1; fi
    done
    return 0
}

# The terrain verdict on that page was wrong once because it was measured
# against the frozen submodules while the repo read the maintained trees. The
# guard against that happening again is that the enums the new verdict rests on
# have to still be there, in the tree this run actually read.
check_enum() {
    if [ ! -f "$2" ]; then
        echo "  SKIP ($1; no $2)"
    elif grep -q "$3" "$2"; then
        ok "$1"
    else
        bad "$1 (it is in $2 and does not say so)"
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
void = floor = flat = bit15 = 0
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
    tiles += 1024
    for k in range(1024):
        height, kind = values[2 * k], values[2 * k + 1]
        void += kind == 0x00810001
        floor += kind == 0x00800000
        flat += height == 0
        bit15 += (kind >> 15) & 1
print("models=%d" % models)
print("grids=%d" % grids)
print("tiles=%d" % tiles)
print("void=%.1f" % (100.0 * void / tiles))
print("floor=%.1f" % (100.0 * floor / tiles))
print("flat=%.1f" % (100.0 * flat / tiles))
print("bit15=%.1f" % (100.0 * bit15 / tiles))
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
        same "tiles counted over them" "$(field tiles)" "393216"

        # The whole point of the section: the record is two words per tile,
        # the second word's low byte is the tile's kind, and the two values
        # that dominate it are the void around interiors and plain floor.
        same "Gen 4's collision bit, in Black's second word" "$(field bit15)" "0.0"
        same "Black tiles that are void (0x00810001)" "$(field void)" "79.7"
        same "Black tiles that are floor (0x00800000)" "$(field floor)" "10.6"
        same "Black tiles at height zero" "$(field flat)" "81.9"
    fi
fi


if [ ! -f "$DOC" ]; then
    echo "mapformat: MAPFORMATS.md is missing; the page is what this checks"
    exit 1
fi

# --- terrain attributes: the four-byte step that starts the whole comparison ---
if have "$PL_ATTRS" "$HG_ATTRS"; then
    pl_off=$(awk '/define TERRAIN_ATTRIBUTES_OFFSET/ { print $3 }' "$PL_ATTRS")
    hg_off=$(awk '/define TERRAIN_ATTRIBUTES_OFFSET/ { print $3 }' "$HG_ATTRS")
    same "Platinum's terrain-attribute offset" "$pl_off" "0x10"
    same "HeartGold's terrain-attribute offset" "$hg_off" "0x14"
    pl_sz=$(awk '/define TERRAIN_ATTRIBUTES_SIZE/ { print $3 }' "$PL_ATTRS")
    hg_sz=$(awk '/define TERRAIN_ATTRIBUTES_SIZE/ { print $3 }' "$HG_ATTRS")
    same "Platinum's terrain-attribute block" "$pl_sz" "0x800"
    same "HeartGold's terrain-attribute block" "$hg_sz" "0x800"

    # The behaviour/collision split the page calls free is Platinum's own.
    grep -q 'define TERRAIN_ATTRIBUTES_COLLISION_SHIFT *15' "$PL_ATTRS" \
        && ok "the collision bit is still 15" \
        || bad "Platinum moved TERRAIN_ATTRIBUTES_COLLISION_SHIFT off 15"
    grep -q 'define TERRAIN_ATTRIBUTES_TILE_BEHAVIOR_MASK *0xFF' "$PL_ATTRS" \
        && ok "the behaviour mask is still the low byte" \
        || bad "Platinum moved TERRAIN_ATTRIBUTES_TILE_BEHAVIOR_MASK off 0xFF"

    # This used to be a tripwire waiting for a terrain-behaviour enum to appear,
    # because while neither tree named a value the page's "no oracle" held. Both
    # trees name them now, check_enum above is that fact, so what is left worth
    # holding is where the names live: not beside the attribute layout, which is
    # why the wall looked solid for as long as it did.
    # A value name would be a bare TILE_BEHAVIOR_<something>; the one hit these
    # headers do have is TERRAIN_ATTRIBUTES_TILE_BEHAVIOR_MASK, which is layout.
    # Counted rather than grep -v'd: an inverted grep over an empty pipe exits 0
    # here and would pass for the wrong reason.
    strays=$(cat "$PL_ATTRS" "$HG_ATTRS" 2>/dev/null \
             | grep -c 'TILE_BEHAVIOR_' || true)
    masks=$(cat "$PL_ATTRS" "$HG_ATTRS" 2>/dev/null \
            | grep -c 'TERRAIN_ATTRIBUTES_TILE_BEHAVIOR_MASK' || true)
    same "behaviour names defined beside the attribute layout" \
         "$((strays - masks))" "0"
else
    echo "  SKIP (terrain attributes; no $absent)"
fi

says "0x800 bytes, 32 × 32 \`u16\`" "the page still calls the block 32 by 32"

# --- land data: the header order the page prints, read back out of the loader ---
if have "$PL_LAND"; then
    order=$(sed -n '/LandDataHeader_Load(NARC/,/^}/p' "$PL_LAND" \
            | sed -n 's/.*landDataHeader->\([A-Za-z]*\)Size.*/\1/p' | tr '\n' ' ')
    same "Platinum's land-data header order" \
         "$(echo $order)" "terrainAttributes mapProps mapModel bdhc"
else
    echo "  SKIP (the land-data header order; no $absent)"
fi

# --- matrix: the one ceiling the page says is in the engine, not the data ---
if have "$PL_MATRIX" "$HG_MATRIX"; then
    pl_w=$(awk '/define MAP_MATRIX_MAX_WIDTH/ { print $3 }' "$PL_MATRIX")
    hg_max=$(awk '/define MAP_MATRIX_MAX_SIZE/ { print $3 }' "$HG_MATRIX")
    same "Platinum's matrix width cap" "$pl_w" "30"
    same "HeartGold's matrix cell cap" "$hg_max" "799"
else
    echo "  SKIP (the matrix caps; no $absent)"
fi

says "47 × 17 = 799 cells" "the page still names HeartGold's world matrix"

# --- script commands: the count that says there is no conversion ---
if have "$PL_SCRCMD" "$HG_SCRCMD"; then
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
else
    echo "  SKIP (the script command tables; no $absent)"
fi

# --- map headers: the counts the table publishes ---
if have "$PL_HEADERS" "$HG_HEADERS"; then
    pl_maps=$(grep -c '^ *\[MAP_[A-Z0-9_]*\] *= *{' "$PL_HEADERS")
    hg_maps=$(grep -c '^ *\[MAP_[A-Z0-9_]*\] *= *{' "$HG_HEADERS")
    same "Platinum's map header count" "$pl_maps" "593"
    same "HeartGold's map header count" "$hg_maps" "540"
    # And the band the client will resolve a header in, which is those two
    # counts plus the authored hub sitting in the slot between them. idmap.c
    # refuses anything above it, so a header the porter grows past this bound
    # would arrive and be turned away as if the map did not exist.
    first=$(sed -n 's/^#define MMO_MAP_HEADER_PORTED_FIRST *\([0-9]*\).*/\1/p' \
            "$ROOT/include/idmap.h")
    count=$(sed -n 's/^#define MMO_MAP_HEADER_PORTED_COUNT *\([0-9]*\).*/\1/p' \
            "$ROOT/include/idmap.h")
    same "the ported band starts after the image and its hub" \
        "$first" "$((pl_maps + 1))"
    same "the ported band is HeartGold's whole header table" "$count" "$hg_maps"
else
    echo "  SKIP (the map header counts; no $absent)"
fi

# --- the one field at the same offset, which is the whole map-header claim ---
if have "$PL/include/map_header.h" "$HG/include/map_header.h"; then
    grep -q 'u16 eventsArchiveID;' "$PL/include/map_header.h" \
        && grep -q 'u16 eventsBank;' "$HG/include/map_header.h" \
        && ok "both map headers still carry an events bank" \
        || bad "a map header renamed its events bank; the 0x10 row is stale"
else
    echo "  SKIP (the events bank field; no $absent)"
fi

# --- the three tables a map port reads besides TERRAIN_MAP ---
#
# Same gate, same reason: each is generated out of the HeartGold tree and read
# by tools/portmap.py, and a checkout that renames a map, a sprite or a flag has
# to show up here rather than in a ported map that spawns the wrong person.
for table in MAPS:gen_maps.py SPRITES:gen_sprites.py STORYEND:gen_storyend.py \
             MAPSCENES:gen_mapscenes.py HIDDEN_ITEMS:gen_hidden_items.py \
             CAMERAS:gen_cameras.py
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

# The camera templates cross twice: mmo/CAMERAS for the porter and a C block
# for the engine.
if [ -n "$HG" ] && [ -d "$HG" ] && command -v python3 >/dev/null 2>&1; then
    campatch="$ROOT/mods/openmmo/patches/src/overlay005/field_camera.c.patch"
    camsrc="$ROOT/mods/openmmo/src/openmmo_camera.c"
    camblock="${TMPDIR:-/tmp}/mapfmt.cameras.$$"
    if python3 "$ROOT/tools/gen_cameras.py" "$HG" --c > "$camblock" 2>/dev/null; then
        cammissing=0
        while IFS= read -r camline; do
            [ -z "$camline" ] && continue
            if ! grep -qF -- "$camline" "$campatch" && ! grep -qF -- "$camline" "$camsrc"; then
                cammissing=$((cammissing + 1))
            fi
        done < "$camblock"
        if [ "$cammissing" -eq 0 ]; then
            ok "the camera patch and mod source carry every line of the generated block"
        else
            bad "$cammissing line(s) of the generated camera block are in neither" \
                "the field_camera patch nor openmmo_camera.c; rerun" \
                "tools/gen_cameras.py --c and paste"
        fi
    else
        bad "tools/gen_cameras.py --c refused"
    fi
    rm -f "$camblock"
fi

# --- MAPWALLS: the hand-written rows, checked against the same tree ---
if [ ! -f "$ROOT/MAPWALLS" ]; then
    bad "mmo/MAPWALLS is missing; a map port reads it"
elif [ -z "$HG" ] || [ ! -d "$HG" ] || ! command -v python3 >/dev/null 2>&1; then
    echo "  SKIP (MAPWALLS; no heartgold checkout or python3)"
else
    walls_bad=$(python3 - "$ROOT" "$HG" <<'PY'
import re, sys
root, hg = sys.argv[1], sys.argv[2]
flags = {m.group(1): int(m.group(2), 0) for m in re.finditer(
    r"#define\s+(FLAG_\w+)\s+(0x[0-9A-Fa-f]+|\d+)",
    open(hg + "/include/constants/flags.h").read())}
scenes = {m.group(1) for m in re.finditer(r"^hg\s+(\S+)", open(root + "/MAPSCENES").read(), re.M)}
bad = 0
for line in open(root + "/MAPWALLS"):
    if not line.startswith("hg"):
        continue
    m = re.match(r"^hg\s+(\S+)\s+(set|clear)\s+(\w+)=(0x[0-9A-Fa-f]+|\d+)", line)
    if not m:
        print("unreadable row: " + line.strip()); bad += 1; continue
    if m.group(1) not in scenes:
        print("no such map in MAPSCENES: " + m.group(1)); bad += 1
    if flags.get(m.group(3)) != int(m.group(4), 0):
        print("%s is %s in flags.h, not %s" % (m.group(3), hex(flags.get(m.group(3), -1)), m.group(4))); bad += 1
print(bad)
PY
)
    if [ "$(echo "$walls_bad" | tail -1)" = "0" ]; then
        ok "MAPWALLS names real maps and the flag numbers the tree has"
    else
        echo "$walls_bad" | sed '$d' | sed 's/^/    /'
        bad "MAPWALLS has a row the tree contradicts"
    fi
fi

# --- TERRAIN_MAP: the table that replaced "26 guesses" ---
TMAP="$ROOT/TERRAIN_MAP"
if [ ! -f "$TMAP" ]; then
    bad "mmo/TERRAIN_MAP is missing; the terrain verdict rests on it"
elif ! command -v python3 >/dev/null 2>&1; then
    ok "TERRAIN_MAP left unchecked (no python3 to regenerate it with)"
elif ! have "$PL/include/constants/field/map_tile_behaviors.h" \
            "$HG/include/constants/metatile_behavior.h"; then
    echo "  SKIP (TERRAIN_MAP; no $absent to regenerate it from)"
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

# --- the region port's one rule, written down twice ---
echo "the header rule a ported region rests on is the same on both sides:"

pybase=$(sed -n 's/^FIRST_FREE_HEADER = \([0-9]*\).*/\1/p' "$ROOT/tools/portmap.py")
ktbase=$(sed -n 's/^val portedHeaderBase = \([0-9]*\).*/\1/p' \
         "$ROOT/../codegen/build.gradle.kts")
same "the first header id a port may answer to" "$pybase" "$ktbase"

pykinds=$(sed -n '/^MAP_KINDS = {/,/}/p' "$ROOT/tools/portmap.py" \
          | grep -o '"[a-z_]*":' | tr -d '":' | sort | tr '\n' ' ')
ktkinds=$(sed -n '/private val MAP_TYPES =/,/^        )/p' \
          "$ROOT/../codegen/src/generator/kotlin/de/fiereu/openmmo/codegen/maps/PortedTerrainParser.kt" \
          | grep -o '"[a-z_]*" to' | sed 's/" to//;s/"//' | sort | tr '\n' ' ')
same "the kinds a region port carries" "$(echo $pykinds)" "$(echo $ktkinds)"

# The rule against what the PORTER actually wrote, when a filled package is
# here. `base + the map's own source header` is a sentence in two languages and
# neither reads the other; this is the third reader, and it checks the one
# artifact that decides where a player ends up. SKIPs on a clean checkout,
# where the package tracks a recipe and no bytes.
COOKED="$ROOT/mods/hgss/.cooked/generated/cooked_maps.txt"
if [ ! -f "$COOKED" ] || ! command -v python3 >/dev/null 2>&1; then
    ok "the porter's own header ids left unchecked (no filled package here)"
else
    verdict=$(python3 - "$ROOT" "$COOKED" "$pybase" <<'EOF'
import sys
from pathlib import Path
root, cooked, base = Path(sys.argv[1]), Path(sys.argv[2]), int(sys.argv[3])
kinds = {"city_town", "interior", "route", "cave"}
want = set()
for line in (root / "MAPS").read_text().splitlines():
    f = line.split()
    if len(f) > 12 and f[0] == "hg" and f[10] in ("johto", "kanto") and f[9] in kinds:
        want.add(base + int(f[2]))
got = {int(l.split()[0]) for l in cooked.read_text().splitlines()
       if l and not l.startswith("#")}
print("the rule" if got == want else
      "%d written that the rule does not, %d the rule wants and it did not"
      % (len(got - want), len(want - got)))
EOF
)
    same "the header ids the porter wrote are" "$verdict" "the rule"
fi

# The field-move bank and the tree the headbutt shakes, when a filled package is
# here: the porter writes the bank's row and the effect's members, and the client
# reads both by name (openmmo_stdbank.c, openmmo_headbutt_effect.c). A package
# missing either is one the client would open a tree on and find nothing.
BANKS="$ROOT/mods/hgss/.cooked/generated/std_banks.txt"
EFFECTS="$ROOT/mods/hgss/.cooked/generated/field_effects.txt"
if [ ! -f "$BANKS" ] || [ ! -f "$EFFECTS" ]; then
    ok "the field-move bank left unchecked (no filled package here)"
else
    fmbase=$(sed -n 's/^FIELDMOVE_BASE = \([0-9]*\).*/\1/p' "$ROOT/tools/portmap.py")
    fmrow=$(awk -v b="$fmbase" '$1 == b { print $4 }' "$BANKS")
    same "the field-move bank's entry count in std_banks.txt" "$fmrow" "2"
    fxverdict=$(python3 - "$ROOT" "$EFFECTS" <<'EOF2'
import sys
from pathlib import Path
root, effects = Path(sys.argv[1]), Path(sys.argv[2])
rows = {l.split()[0]: l.split()[1:] for l in effects.read_text().splitlines() if l.strip()}
narc = root / "mods/hgss/.cooked/narc/graphic/hiden_effect.narc"
def magic(i):
    f = narc / str(i)
    return f.read_bytes()[:4] if f.is_file() else b"none"
model = int(rows.get("headbutt_model", ["-1"])[0])
anim, count = (int(x) for x in rows.get("headbutt_anim", ["-1", "0"])[:2])
got = [magic(model)] + [magic(anim + i) for i in range(count)]
print("the tree and its two animations" if got == [b"BMD0", b"BCA0", b"BMA0"]
      else "members %s" % got)
EOF2
)
    same "the headbutt effect's appended members" "$fxverdict" "the tree and its two animations"
fi

# The card's ported page: HeartGold's LEAGUE BADGES page and its badge sprites,
# seven members copied as they are (portmap.card_page; openmmo_card.c reads
# card_badges.txt), each the kind of file its key promises.
CARD="$ROOT/mods/hgss/.cooked/generated/card_badges.txt"
if [ ! -f "$CARD" ]; then
    ok "the card's ported page left unchecked (no filled package here)"
else
    cardverdict=$(python3 - "$ROOT" "$CARD" <<'EOF2'
import sys
from pathlib import Path
root, card = Path(sys.argv[1]), Path(sys.argv[2])
narc = root / "mods/hgss/.cooked/narc/graphic/trainer_case.narc"
want = {"page_tiles": b"RGCN", "page_screen": b"RCSN", "page_palette": b"RLCN",
        "badge_char": b"RGCN", "badge_pal": b"RLCN", "badge_cell": b"RECN", "badge_anim": b"RNAN"}
def magic(i):
    f = narc / str(i)
    return f.read_bytes()[:4] if f.is_file() else b"none"
got = {}
for line in card.read_text().splitlines():
    f = line.split()
    if len(f) == 2:
        got[f[0]] = int(f[1])
missing = [k for k in want if k not in got]
bad = [k for k in want if k in got and magic(got[k]) != want[k]]
print("the page, its palette and the four badge members"
      if not missing and not bad and len(got) == len(want)
      else "missing %s, wrong %s, %d rows" % (missing, bad, len(got)))
EOF2
)
    same "the card's ported page members" "$cardverdict" "the page, its palette and the four badge members"
fi

# And the ceiling the porter checks its appended prop models against is the one
# this client actually builds. The engine's own number is 768; the patch below
# is the only reason a whole region fits, and a porter still believing 768 would
# refuse a map that loads, while one believing more than the patch grants would
# write past an array.
pycap=$(sed -n 's/^MAX_MAP_PROP_MODEL_FILES = \([0-9]*\).*/\1/p' "$ROOT/tools/portmap.py")
ktcap=$(sed -n 's/^+#define MAX_MAP_PROP_MODEL_FILES \([0-9]*\).*/\1/p' \
        "$ROOT/mods/openmmo/patches/include/overlay005/area_data.h.patch")
same "the loaded-model table this client builds" "$pycap" "$ktcap"

# The rectangle a region is cut into has to fit the matrix the engine reads into
# a fixed struct. Johto's is 24x14 and Kanto's would be its own; a bound moving
# under either is what this catches, from the engine's own header.
mw=$(sed -n 's/^#define MAP_MATRIX_MAX_WIDTH *\([0-9]*\).*/\1/p' \
     "$ENGINE/include/constants/field/map_matrix.h" 2>/dev/null)
pymw=$(sed -n 's/^MAP_MATRIX_MAX_WIDTH = \([0-9]*\).*/\1/p' "$ROOT/tools/portmap.py")
if [ -n "$mw" ]; then
    same "the matrix width the porter checks against" "$pymw" "$mw"
else
    ok "matrix width left unchecked (no engine checkout here)"
fi

# --- the wild encounters a ported map serves ---
echo "a ported map's wild encounters rest on two trees agreeing:"

# The species names are read against the source game's numbers, which is right
# by construction for reading its data, and useless unless the destination
# agrees about them, because the id that lands in a table is served to a client
# that looks it up in its own. So: every species both headers name, at the same
# number, or the count of disagreements says so.
if ! command -v python3 >/dev/null 2>&1; then
    ok "the species agreement left unchecked (no python3)"
elif ! have "$HG/include/constants/species.h" "$PL/generated/species.txt"; then
    echo "  SKIP (the species agreement; no $absent)"
else
    agree=$(python3 - "$HG" "$PL" <<'EOF'
import re, sys
from pathlib import Path
def table(path, pattern):
    text = Path(path).read_text(errors="replace")
    return {m.group(1): int(m.group(2)) for m in re.finditer(pattern, text)}
hg = table(sys.argv[1] + "/include/constants/species.h",
           r"#define\s+(SPECIES_\w+)\s+(\d+)")
pl = {}
for i, line in enumerate(Path(sys.argv[2] + "/generated/species.txt").read_text().splitlines()):
    line = line.strip()
    if line:
        pl[line] = i
both = set(hg) & set(pl)
bad = [n for n in both if hg[n] != pl[n]]
print("%d of %d" % (len(both) - len(bad), len(both)))
EOF
)
    same "species both games name, at the same number" \
         "$(echo "$agree" | awk '{print ($1 == $3) ? "all" : "not all"}')" "all"
fi

# And the join. A map meets something when its header names an encounter bank,
# and that name has to be one the source's own encounter file carries, a code
# with no entry is a map that would silently meet nothing.
ENCJSON="$HG/files/fielddata/encountdata/gs_enc_data.json"
if [ ! -f "$ENCJSON" ] || ! command -v python3 >/dev/null 2>&1; then
    ok "the encounter join left unchecked (no encounter data here)"
else
    missing=$(python3 - "$ROOT/MAPS" "$ENCJSON" <<'EOF'
import json, sys
from pathlib import Path
want = set()
for line in Path(sys.argv[1]).read_text().splitlines():
    f = line.split()
    if len(f) > 12 and f[0] == "hg" and f[8] != "-" and f[10] in ("johto", "kanto"):
        want.add(f[8])
have = {e["map"] for e in json.loads(Path(sys.argv[2]).read_text())["encounters"]}
print(len(want - have))
EOF
)
    same "encounter banks a ported map names and the source has no entry for" \
         "$missing" "0"
fi

# --- the script table, and the two oracles under it ---
echo "the script command table is what the two trees say:"
SCRCMD="$ROOT/SCRCMD"
if [ ! -f "$SCRCMD" ]; then
    bad "mmo/SCRCMD is missing; a ported script rests on it"
elif ! command -v python3 >/dev/null 2>&1; then
    ok "SCRCMD left unchecked (no python3 to regenerate it with)"
elif ! have "$HG/include/constants/std_script.h" \
            "$PL/include/data/scripts/scrcmd.h"; then
    echo "  SKIP (SCRCMD; no $absent to regenerate it from)"
else
    gen="${TMPDIR:-/tmp}/mapfmt.scrcmd.$$"
    if python3 "$ROOT/tools/gen_scripts.py" "$HG" "$PL" "$gen" >/dev/null 2>&1
    then
        if cmp -s "$SCRCMD" "$gen"; then
            ok "SCRCMD still matches the two trees it was generated from"
        else
            bad "SCRCMD is stale; rerun tools/gen_scripts.py"
        fi
    else
        bad "tools/gen_scripts.py refused (a run lost its confirmations?)"
    fi
    rm -f "$gen"
fi

# The alignment is only as good as what falsifies it, and what falsifies it is
# a macro name spelt the same in both trees. A run with none is a run of
# matching operand widths and nothing else, which is exactly the state the
# original refusal described, so no row of this table may come from one.
runs=$(grep -c "^#   hg .*confirmed by" "$SCRCMD" 2>/dev/null || echo 0)
same "runs the table was built from" "$(test "$runs" -ge 3 && echo many || echo few)" "many"
unconfirmed=$(awk '/^#   hg .*confirmed by 0:/ { n++ } END { print n + 0 }'               "$SCRCMD" 2>/dev/null)
same "runs kept with nothing confirming them" "$unconfirmed" "0"

# The fold itself, on a script built here rather than read out of a cartridge.
if command -v python3 >/dev/null 2>&1; then
    same "a branch on a story that has not happened folds to, and the movement clamp" \
         "$(python3 "$ROOT/tests/fold_probe.py" "$ROOT" 2>&1)" "folded [4] moves ok"
else
    ok "the fold left unchecked (no python3)"
fi

# The commands a talking person is made of, each by name on both sides. These
# six are the whole of "somebody says a line", and a table that lost one would
# still generate and would carry nobody.
for pair in "45 Message" "53 CloseMessage" "96 LockAll" "97 ReleaseAll" \
            "104 FacePlayer" "50 WaitButton"; do
    set -- $pair
    got=$(awk -v o="$1" '$1 == "hg" && $2 == o { print $NF }' "$SCRCMD")
    same "heartgold command $1 lands on" "${got:-nothing}" "$2"
done

# --- a trainer, and the four numbers that let one cross ---------------------
echo "a ported trainer crosses on numbers both games say:"

TRC="$ROOT/TRAINER_CLASS"
if [ ! -f "$TRC" ]; then
    bad "mmo/TRAINER_CLASS is missing; a ported trainer's class rests on it"
elif ! command -v python3 >/dev/null 2>&1; then
    ok "TRAINER_CLASS left unchecked (no python3 to regenerate it with)"
else
    gen="${TMPDIR:-/tmp}/mapfmt.trclass.$$"
    if python3 "$ROOT/tools/gen_trainer_class.py" "$gen" >/dev/null 2>&1; then
        if cmp -s "$TRC" "$gen"; then
            ok "TRAINER_CLASS still matches the two trees it came from"
        else
            bad "TRAINER_CLASS is stale; rerun tools/gen_trainer_class.py"
        fi
    else
        bad "tools/gen_trainer_class.py refused"
    fi
    rm -f "$gen"
fi

# A class byte indexes three 105-entry tables of this game's, the gender its
# party generator reads, the prize multiplier, and the name a battle prints,
# so a row that sends a trainer past the end of them is an out-of-bounds read
# in three places at once, and the fill would not notice.
classes=$(grep -c '^hg ' "$TRC" 2>/dev/null || :)
if have "$PL/build/rom/generated/trainer_classes.h"; then
    plclasses=$(grep -c 'TRAINER_CLASS_[A-Z_0-9]* *= *[0-9]' \
        "$PL/build/rom/generated/trainer_classes.h")
    over=$(awk -v n="${plclasses:-0}" \
           '$1 == "hg" && $4 != "appended" && $3 + 0 >= n { c++ }
           END { print c + 0 }' "$TRC" 2>/dev/null)
    same "rows sending a trainer past this game's ${plclasses:-?} classes unappended" \
         "${over:-?}" "0"
    runs=$(awk -v n="${plclasses:-0}" \
           '$1 == "hg" && $4 == "appended" { seen[$3 + 0] = 1; k++ }
           END { ok = (k > 0); for (i = n; i < n + k; i++) if (!seen[i]) ok = 0;
                 print ok ? "contiguously" : "with a hole" }' "$TRC" 2>/dev/null)
    same "the appended classes run from ${plclasses:-?}" "${runs:-?}" "contiguously"
else
    echo "  SKIP (the class ceiling; no $absent)"
fi
same "classes the table pairs or stands in for" \
     "$(test "${classes:-0}" -ge 100 && echo many || echo few)" "many"

# Both games' own constants for the two script-id bases. The whole address
# rests on these being the same two numbers, and they are: `_std_npc_trainer`
# 3000 and `_std_npc_trainer_2` 5000 there, SCRIPT_ID_OFFSET_SINGLE_BATTLES and
# _DOUBLE_BATTLES here. A tree that moved one would silently send every ported
# trainer to somebody else's fight.
if ! have "$HG/include/constants/std_script.h" "$PL/include/script_manager.h"
then
    echo "  SKIP (the two script-id bases; no $absent)"
fi
for pair in "SINGLE _std_npc_trainer SCRIPT_ID_OFFSET_SINGLE_BATTLES" \
            "DOUBLE _std_npc_trainer_2 SCRIPT_ID_OFFSET_DOUBLE_BATTLES"; do
    have "$HG/include/constants/std_script.h" "$PL/include/script_manager.h" \
        || continue
    set -- $pair
    hgv=$(sed -n "s/^#define $2  *\([0-9][0-9]*\).*/\1/p" \
          "$HG/include/constants/std_script.h" 2>/dev/null | head -1)
    plv=$(sed -n "s/^#define $3  *\([0-9][0-9]*\).*/\1/p" \
          "$PL/include/script_manager.h" 2>/dev/null | head -1)
    ours=$(sed -n "s/^SCRIPT_ID_$1 = \([0-9]*\).*/\1/p" \
           "$ROOT/tools/porttrainers.py")
    same "the $1-battle script base heartgold says" "${hgv:-?}" "${ours:-?}"
    same "the $1-battle script base this game says" "${plv:-?}" "${ours:-?}"
done

# The defeated-flag remap, which is two halves that have to meet exactly. The
# engine's own block is one flag per trainer it shipped; the patch sends every
# number past that to a block of its own at the top of the array. If the
# porter's idea of "how many this game shipped" and the block's width disagree,
# the first ported trainer beaten sets a flag of this game's story.
if have "$PL/build/rom/generated/vars_flags.h"; then
shipped=$(python3 - "$PL" <<'EOS' 2>/dev/null
import re, sys
from pathlib import Path
h = (Path(sys.argv[1]) / "build/rom/generated/vars_flags.h").read_text()
def at(name):
    m = re.search(r"^\s*%s\s*=\s*(\d+)," % name, h, re.M)
    return int(m.group(1)) if m else None
a, b = at("TRAINER_DEFEATED_FLAGS_START"), at("TRAINER_DEFEATED_FLAGS_END")
print(b - a + 1 if a is not None and b is not None else "?")
EOS
)
ours=$(sed -n 's/^PL_TRAINER_COUNT = \([0-9]*\).*/\1/p' "$ROOT/tools/porttrainers.py")
    same "trainers this game ships a defeated flag for" "${shipped:-?}" "${ours:-?}"
else
    echo "  SKIP (the defeated-flag count; no $absent)"
fi

PATCH=$ROOT/mods/openmmo/patches/include/vars_flags.h.patch
portmax=$(sed -n 's/^+#define OPENMMO_PORTED_TRAINERS_MAX  *\([0-9]*\).*/\1/p' \
          "$PATCH" 2>/dev/null)
synat=$(sed -n 's/^+#define OPENMMO_SYNTHETIC_FLAGS_START  *\([0-9]*\).*/\1/p' \
        "$PATCH" 2>/dev/null)
synmax=$(sed -n 's/^+#define OPENMMO_SYNTHETIC_FLAGS_MAX  *\([0-9]*\).*/\1/p' \
         "$PATCH" 2>/dev/null)
engineflags=$(sed -n 's/^-#define NUM_FLAGS \([0-9]*\).*/\1/p' "$PATCH" 2>/dev/null)
same "the added bands start where this game's flags stopped" \
     "${synat:-?}" "${engineflags:-?}"
wire=$(sed -n 's/^#define MMO_SCRIPT_FLAG_MAX  *\([0-9]*\).*/\1/p' \
       "$ROOT/include/game.h")
itemat=$(sed -n 's/^+#define OPENMMO_PORTED_ITEM_FLAGS_MAX  *\([0-9]*\).*/\1/p' \
        "$PATCH" 2>/dev/null)
staticmax=$(sed -n 's/^+#define OPENMMO_PORTED_STATIC_FLAGS_MAX  *\([0-9]*\).*/\1/p' \
          "$PATCH" 2>/dev/null)
same "game.h's flag ceiling against the patched array" \
     "${wire:-?}" "$(( ${synat:-0} + ${synmax:-0} + ${portmax:-0} + ${itemat:-0} + ${staticmax:-0} ))"
# The static band, the fourth: a person whose script stages a wild fight
# hides behind one of these once the server's fight is won. The porter
# numbers them from where the item band ends, as wide as the patch says.
portstatic=$(python3 -c 'import sys; sys.path.insert(0, "'"$ROOT"'/tools"); \
             import portmap as pm; print(pm.PORTED_STATIC_FLAGS_START, pm.PORTED_STATIC_FLAGS_MAX)' 2>/dev/null)
same "the porter's static band starts where the item band ends" \
     "${portstatic%% *}" "$(( ${synat:-0} + ${synmax:-0} + ${portmax:-0} + ${itemat:-0} ))"
same "the porter's static band is as wide as the patch's" \
     "${portstatic##* }" "${staticmax:-?}"

# The item band, which is two halves that have to meet the same way: the
# porter writes a ported ball's flag as HeartGold's number moved onto the band,
# and the patch says where the band is and how wide. The width is HeartGold's
# own item band, from its hidden-item base to past its last item-ball flag, so
# both ends of that are read out of its tree as well.
portitem=$(sed -n 's/^PORTED_ITEM_FLAGS_START = \([0-9]*\).*/\1/p' \
           "$ROOT/tools/portmap.py")
same "the porter's item band starts where the trainers' band ends" \
     "${portitem:-?}" "$(( ${synat:-0} + ${synmax:-0} + ${portmax:-0} ))"
hgfirst=$(sed -n 's/^HG_ITEM_FLAG_FIRST = \(0x[0-9A-Fa-f]*\).*/\1/p' \
          "$ROOT/tools/portmap.py")
hgend=$(sed -n 's/^HG_ITEM_FLAG_END = \(0x[0-9A-Fa-f]*\).*/\1/p' \
        "$ROOT/tools/portmap.py")
same "the patched band is as wide as HeartGold's item band" \
     "${itemat:-?}" "$(( ${hgend:-0} - ${hgfirst:-0} ))"
if [ -n "$HG" ] && [ -d "$HG" ]; then
    hgbase=$(sed -n 's/^#define HIDDEN_ITEMS_FLAG_BASE  *\([0-9]*\).*/\1/p' \
             "$HG/include/constants/flags.h")
    same "HeartGold's hidden-item flag base is the band's first" \
         "${hgbase:-?}" "$(( ${hgfirst:-0} ))"
    hglast=$(grep '^#define FLAG_HIDE_ITEMBALL_' "$HG/include/constants/flags.h" \
             | awk '{print $3}' | python3 -c 'import sys; print(max(int(x,0) for x in sys.stdin))' 2>/dev/null)
    if [ -n "$hglast" ] && [ "$hglast" -lt "$(( ${hgend:-0} ))" ]; then
        ok "every HeartGold item-ball flag is on the band"
    else
        bad "a HeartGold item-ball flag (${hglast:-?}) is past the band's end"
    fi
else
    echo "  SKIP (item band against heartgold's flags; no checkout)"
fi

# A synthetic id is engine state that lives outside VarsFlags and is diverted
# BY ID on the way in, so one that fell inside the ported block would be swapped
# for a trainer's defeated flag with nothing to report it. The band exists so
# that cannot happen; this is the band holding.
shoes=$(sed -n 's/^#define MMO_SCRIPT_FLAG_RUNNING_SHOES *\([0-9]*\).*/\1/p' \
        "$ROOT/include/client.h")
if [ -n "$shoes" ] && [ "$shoes" -ge "${synat:-0}" ] \
        && [ "$shoes" -lt "$(( ${synat:-0} + ${synmax:-0} ))" ]; then
    ok "the synthetic flag ids sit in the band reserved for them"
else
    bad "synthetic flag id ${shoes:-?} is outside ${synat:-?}..$(( ${synat:-0} + ${synmax:-0} - 1 ))"
fi

# The ceiling has to cover every trainer the cartridge has, not just the ones
# a run happens to place, or the day somebody ports an interior full of them
# the fill lands past the block and says nothing.
hgtrainers=$(grep -c '^#define TRAINER_[A-Z_0-9]*  *[0-9]' \
    "$HG/include/constants/trainers.h" 2>/dev/null || :)
if ! have "$HG/include/constants/trainers.h"; then
    echo "  SKIP (the ported flag block's ceiling; no $absent)"
elif [ "${portmax:-0}" -ge "${hgtrainers:-99999}" ]; then
    ok "the ported flag block holds all ${hgtrainers} of heartgold's trainers"
else
    bad "the ported flag block is ${portmax} and heartgold has ${hgtrainers}"
fi

# mmo/MAPS has gained a column twice and this index did not follow it twice,
# and both times the region check read the kind column and refused every region
# there is. The porter's own reader is the authority on which field is which.
mapsreg=$(sed -n "s/.*'\$1 == \"hg\" \&\& \$\([0-9]*\) == r.*/\1/p" \
          "$ROOT/tools/modport.sh" 2>/dev/null | head -1)
if [ -n "$mapsreg" ] && awk -v f="$mapsreg" \
        '$1 == "hg" && $f == "johto" { found = 1; exit }
         END { exit !found }' "$ROOT/MAPS" 2>/dev/null; then
    ok "modport reads mmo/MAPS field $mapsreg for the region and finds johto"
else
    bad "modport reads mmo/MAPS field ${mapsreg:-?} for the region and no row" \
        "of it says johto"
fi

if [ "$fail" -eq 0 ]; then
    echo "mapformat: all checks passed"
else
    echo "mapformat: $fail check(s) FAILED"
fi
exit "$fail"
