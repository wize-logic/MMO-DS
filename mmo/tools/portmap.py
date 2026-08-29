#!/usr/bin/env python3
"""Port HeartGold/SoulSilver maps into this game's own field archives."""

from __future__ import annotations

import re
import struct
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import nsbtx                                              # noqa: E402

MMO = Path(__file__).resolve().parent.parent

# HeartGold, and SoulSilver whose map archives are byte-identical to it.
SRC_MATRIX = "a/0/4/1"
SRC_AREA = "a/0/4/2"
SRC_TEXSET = "a/0/4/4"
SRC_LAND = "a/0/6/5"
# Two map-prop archives, and which one an area's ids index is measured rather
# than read.
SRC_PROPMODEL = "a/0/4/0"      # 340 NSBMD map props, the outdoor buildings
SRC_PROPROOM = "a/1/4/8"       # 222 NSBMD map props, the indoor furniture
SRC_AREABUILD = "a/0/4/3"      # 104 preload lists: u16 count, then u16 ids
SRC_PROPTEX = "a/0/7/0"        # 104 NSBTX, one per prop set
SRC_EVENTS = "a/0/3/2"         # 491 zone_event members
SRC_MMODEL = "a/0/8/1"         # 863 NSBTX, the overworld people

# This game's, by the names its own decomp gives them.
DST_LAND = "fielddata/land_data/land_data.narc"
DST_MATRIX = "fielddata/mapmatrix/map_matrix.narc"
DST_AREA = "fielddata/areadata/area_data.narc"
DST_TEXSET = "fielddata/areadata/area_map_tex/map_tex_set.narc"
DST_PROPMODEL = "fielddata/build_model/build_model.narc"
DST_AREABUILD = "fielddata/areadata/area_build_model/area_build.narc"
DST_PROPTEX = "fielddata/areadata/area_build_model/areabm_texset.narc"
DST_MSG = "msgdata/pl_msg.narc"
DST_MMODEL = "data/mmodel/mmodel.narc"
DST_MMLIST = "fielddata/mm_list/move_model_list.narc"

# The bank the field's place-name banner reads, and the two constants the
# engine's own MessageBank_Load decodes it with (src/message.c). A bank is a
# u16 count and a u16 seed, then an (offset, length) pair per message with the
# seed folded in, then the characters with a per-message key of their own.
MSG_BANK_LOCATION_NAMES = 433
MSG_TABLE_MUL = 765
MSG_KEY_START = 596947
MSG_KEY_INC = 18749

# The other four archives a MapHeader names.
DST_SCRIPT = "fielddata/script/scr_seq.narc"
DST_EVENTS = "fielddata/eventdata/zone_event.narc"

SCRCMD_END = 2                # data/scripts/scrcmd.h, the third entry
SCRIPT_TABLE_END = 0xFD13     # what the engine's own readers test for

# The order pc_modfs.c's load_cooked_maps scans a row in, and the presentation
# half of a header.
HEADER_FIELDS = (
    "id area preloaded matrix scripts init msg day night wild events "
    "label window weather camera mapType battleBG bike run escape fly").split()

MAP_LABEL_WINDOW_CITY = 1
MAP_LABEL_WINDOW_TOWN = 2
OVERWORLD_WEATHER_CLEAR = 0
CAMERA_TYPE_DEFAULT = 0
CAMERA_TYPE_INTERIOR_ORTHOGRAPHIC = 4
MAP_TYPE_TOWN_CITY = 1
MAP_TYPE_INDOORS = 4
BACKGROUND_CITY = 2
BACKGROUND_INDOORS_1 = 6
ENCOUNTERS_NONE = 0xFFFF

# How a ported map presents itself.
KINDS = {
    "city": dict(window=MAP_LABEL_WINDOW_CITY, camera=CAMERA_TYPE_DEFAULT,
                 map_type=MAP_TYPE_TOWN_CITY, battle_bg=BACKGROUND_CITY,
                 bike=1, run=1, escape=0, fly=1, lighting=1),
    "town": dict(window=MAP_LABEL_WINDOW_TOWN, camera=CAMERA_TYPE_DEFAULT,
                 map_type=MAP_TYPE_TOWN_CITY, battle_bg=BACKGROUND_CITY,
                 bike=1, run=1, escape=0, fly=1, lighting=1),
    "interior": dict(window=MAP_LABEL_WINDOW_TOWN,
                     camera=CAMERA_TYPE_INTERIOR_ORTHOGRAPHIC,
                     map_type=MAP_TYPE_INDOORS, battle_bg=BACKGROUND_INDOORS_1,
                     bike=0, run=0, escape=0, fly=0, lighting=0),
}

# Which of mmo/MAPS's kind column maps onto which of the above. A kind with no
# row here is one nothing has been ported of yet, and is refused by name rather
# than dressed as a city.
MAP_KINDS = {"city_town": "city", "interior": "interior"}

# --------------------------------------------------------------------- events
BG_EVENT = 20
OBJECT_EVENT = 32
WARP_EVENT = 12
COORD_EVENT = 16

# The engine reads a member into a fixed buffer and asserts on the size, so a
# map with more events than fit is refused here rather than at the load.
EVENTS_BUFFER = 0x800

# An object event with this script id is "no script", and BOTH games test for
# it, and in this game an object that has none is created without ever asking
# about its hide flag, which is not what a ported person wants. So a carried
# person gets script 0, the do-nothing script of the archive beside it, and a
# hide flag of 0, which is a flag nothing sets.
SCRIPT_UNSET = 0xFFFF
SCRIPT_NONE = 0
FLAG_NONE = 0
TRAINER_TYPE_NONE = 0

# A warp with no destination in this package. Its record has to stay where it is,
# every other map's arrival anchor is an index into this array, so it is
# parked on a coordinate no player stands on instead of being removed.
# `MapHeaderData_GetIndexOfWarpEventAtPos` compares x and z and never matches.
WARP_PARKED = 0xFFFF

# `MAX_MAP_OBJECTS_TO_PRELOAD`, and the sentinel `FetchMapObjectsToPreload`
# stops at. The preload list is what sizes the billboard resource heap
# (`ov5_021ECE40` takes count + 3), so a ported map names its own people there
# rather than borrowing a Sinnoh member that was sized for a different cast.
MAX_MAP_OBJECTS_TO_PRELOAD = 24
MAP_OBJECT_PRELOAD_SENTINEL = 0xFFFF

# Where an appended person lands.
FIRST_COOKED_GFX = 276

HG_LAND_HEADER = 0x14   # four sizes, then the tag word
PL_LAND_HEADER = 0x10   # four sizes
TERRAIN_SIZE = 0x800    # 32 x 32 u16, the same on both sides
EMPTY_CELL = 0xFFFF

# A terrain word is a behaviour byte, seven bits nobody here reads, and a
# collision bit. Measured across both games' whole land-data archives:
#
#   platinum  high byte is 0x00 or 0x80 and nothing else, 666 members
#   heartgold high byte takes 38 distinct values over 676 members
CARRIED_BITS = 0x80FF

# The lighting field is not carried. HeartGold packs a byte pair there (0x0101
# on most outdoor records) where Platinum keeps a u16 index into its own
# lighting set archive, so carrying the number would index a set that does not
# mean the same thing. Platinum's own outdoor lighting is used instead, and
# that is a substitution rather than a port, recorded here rather than hidden.
PL_OUTDOOR_LIGHTING = 1
PL_AREA_DUMMY = 1

# What this game's built image already holds. A package's appended members must
# be contiguous with these, pc_modfs refuses an "append hole", so a port
# starts at the count and one package holds one map. Overriding these on the
# command line is how a port replaces an existing map instead of adding one.
PL_COUNT = {
    DST_LAND: 666,
    DST_MATRIX: 289,
    DST_AREA: 75,
    DST_TEXSET: 74,
    DST_PROPMODEL: 590,
    DST_AREABUILD: 71,
    DST_PROPTEX: 71,
    DST_SCRIPT: 1124,
    DST_EVENTS: 534,
    DST_MSG: 724,
    DST_MMODEL: 470,
    DST_MMLIST: 16,
}

# A map prop is 48 bytes beginning with a u32 model id. The engine indexes its
# loaded-model table by that absolute id, `mapPropModelFiles[mapPropModelID]`
# in area_data.c, and the table is MAX_MAP_PROP_MODEL_FILES entries, so an
# appended model has to land under that or the load writes past it.
PROP_RECORD = 48
PROP_MODEL_ID = 0
MAX_MAP_PROP_MODEL_FILES = 768


def die(msg: str) -> None:
    print("portmap: " + msg, file=sys.stderr)
    raise SystemExit(2)


def load_terrain_map(path: Path) -> dict[int, tuple[int, str]]:
    """value -> (byte to write, verdict), from the committed table."""
    if not path.is_file():
        die("no %s, the whole reason this kind is no longer refused" % path)
    out: dict[int, tuple[int, str]] = {}
    for line in path.read_text().splitlines():
        m = re.match(r"^0x([0-9A-F]{2})\s+0x([0-9A-F]{2})\s+(\S+)", line)
        if m:
            out[int(m.group(1), 16)] = (int(m.group(2), 16), m.group(3))
    if len(out) < 200:
        die("%s has only %d rows; it is not the generated table" % (path, len(out)))
    return out


def load_maps(path: Path) -> dict[str, dict]:
    """name -> the source map's header id, area bank, matrix, kind and music."""
    if not path.is_file():
        die("no %s; run tools/gen_maps.py" % path)
    out = {}
    for line in path.read_text().splitlines():
        m = re.match(r"^hg\s+(\S+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)"
                     r"\s+(\S+)\s+(\S+)\s*$", line)
        if m:
            out[m.group(1)] = dict(header=int(m.group(2)),
                                   area=int(m.group(3)),
                                   matrix=int(m.group(4)),
                                   events=int(m.group(5)),
                                   kind=m.group(6), bgm=m.group(7))
    if not out:
        die("%s has no rows in the seven-column shape; regenerate it" % path)
    return out


def load_sprites(path: Path) -> dict[int, tuple[int, str]]:
    """HeartGold sprite id -> (mmodel member, the name both headers give it)."""
    if not path.is_file():
        die("no %s; run tools/gen_sprites.py" % path)
    out = {}
    for line in path.read_text().splitlines():
        m = re.match(r"^\s*(\d+)\s+(\d+|-)\s+(\S+)\s*$", line)
        if m and m.group(2) != "-":
            out[int(m.group(1))] = (int(m.group(2)), m.group(3))
    if not out:
        die("%s has no rows; regenerate it" % path)
    return out


def load_scenes(path: Path) -> dict[str, set[int] | None]:
    """map name -> the flags its arrival script leaves set, or None if unread."""
    if not path.is_file():
        die("no %s; run tools/gen_mapscenes.py" % path)
    out: dict[str, set[int] | None] = {}
    for line in path.read_text().splitlines():
        m = re.match(r"^hg\s+(\S+)\s+(.*)$", line)
        if not m:
            continue
        rest = m.group(2).split("#")[0].strip()
        if rest == "?":
            out[m.group(1)] = None
        elif rest == "-":
            out[m.group(1)] = set()
        else:
            out[m.group(1)] = {int(tok.split("=")[1], 0)
                               for tok in rest.split() if "=" in tok}
    return out


def nsbtx_palette_name(blob: bytes) -> str | None:
    """The name a person's own texture block gives its palette."""
    if len(blob) < 0x14 or blob[:4] not in (b"BTX0", b"BMD0"):
        return None
    nblocks = struct.unpack_from("<H", blob, 0x0E)[0]
    tex = None
    for i in range(nblocks):
        if 0x10 + 4 * i + 4 > len(blob):
            return None
        off = struct.unpack_from("<I", blob, 0x10 + 4 * i)[0]
        if blob[off:off + 4] == b"TEX0":
            tex = off
    if tex is None or tex + 0x38 > len(blob):
        return None
    base = tex + struct.unpack_from("<I", blob, tex + 0x34)[0]
    if base + 8 > len(blob) or blob[base + 1] == 0:
        return None
    d = base + struct.unpack_from("<H", blob, base + 0x06)[0]
    names = d + struct.unpack_from("<H", blob, d + 2)[0]
    if names + 16 > len(blob):
        return None
    return blob[names:names + 16].split(b"\0")[0].decode("latin1")


def parse_events(member: bytes) -> dict:
    """One zone_event member as its four lists of raw records."""
    out, p = {}, 0
    for key, size in (("bg", BG_EVENT), ("obj", OBJECT_EVENT),
                      ("warp", WARP_EVENT), ("coord", COORD_EVENT)):
        n, = struct.unpack_from("<I", member, p)
        p += 4
        out[key] = [member[p + i * size:p + (i + 1) * size] for i in range(n)]
        p += n * size
    if p != len(member):
        die("a zone_event member's four lists account for %d of %d bytes"
            % (p, len(member)))
    return out


def build_events(lists: dict) -> bytes:
    out = bytearray()
    for key in ("bg", "obj", "warp", "coord"):
        out += struct.pack("<I", len(lists[key]))
        for rec in lists[key]:
            out += rec
    if len(out) >= EVENTS_BUFFER:
        die("the converted events are %d bytes and the engine reads a member "
            "into a %d-byte buffer" % (len(out), EVENTS_BUFFER))
    return bytes(out)


def parse_matrix(b: bytes) -> dict:
    w, h, has_hdr, has_alt, nlen = b[0], b[1], b[2], b[3], b[4]
    p = 5
    name = b[p:p + nlen].decode("latin1")
    p += nlen
    n = w * h
    if has_hdr:
        hdrs = list(struct.unpack_from("<%dH" % n, b, p))
        p += n * 2
    else:
        hdrs = [None] * n
    if has_alt:
        alt = list(b[p:p + n])
        p += n
    else:
        alt = [0] * n
    land = list(struct.unpack_from("<%dH" % n, b, p))
    return dict(w=w, h=h, name=name, hdrs=hdrs, alt=alt, land=land)


def build_matrix(w: int, h: int, alt: list[int], land: list[int],
                 name: str) -> bytes:
    """A matrix of our own, in the format BOTH games' loaders read, they are the same
    routine, so this needs no conversion, only assembly.
    """
    if len(name) > 0x20:
        die("matrix name %r is longer than the engine's own bound" % name)
    out = bytearray()
    out += bytes([w, h, 0, 1, len(name)])
    out += name.encode("latin1")
    out += bytes(alt)
    out += struct.pack("<%dH" % len(land), *land)
    return bytes(out)


def prop_model_ids(member: bytes) -> list[int]:
    """Every model a member's map props ask for, in record order."""
    ta, props = struct.unpack_from("<II", member, 0)
    fifth = struct.unpack_from("<I", member, 0x10)[0] >> 16
    base = HG_LAND_HEADER + ta + fifth
    return [struct.unpack_from("<I", member, base + i * PROP_RECORD
                               + PROP_MODEL_ID)[0]
            for i in range(props // PROP_RECORD)]


def convert_land(member: bytes, tmap: dict[int, tuple[int, str]],
                 stats: dict, prop_map: dict[int, int] | None = None) -> bytes:
    """One HeartGold land-data member as this game's loader reads it."""
    ta, props, model, bdhc = struct.unpack_from("<IIII", member, 0)
    tag_word, = struct.unpack_from("<I", member, 0x10)
    tag, fifth = tag_word & 0xFFFF, tag_word >> 16
    if tag != 0x1234:
        die("member does not carry heartgold's 0x1234 tag (got 0x%04X)" % tag)
    if ta != TERRAIN_SIZE:
        die("terrain attributes are %d bytes, not the %d both games use"
            % (ta, TERRAIN_SIZE))

    p = HG_LAND_HEADER
    p += fifth                                   # the fifth section: FIRST, dropped
    terrain = bytearray(member[p:p + ta]);      p += ta
    props_b = bytearray(member[p:p + props]);   p += props
    model_b = member[p:p + model];               p += model
    bdhc_b = member[p:p + bdhc];                 p += bdhc
    if p != len(member):
        die("sections do not account for the member: %d of %d" % (p, len(member)))
    if model_b[:4] != b"BMD0":
        die("the model section is not an NSBMD (%r), the section order is "
            "wrong and every byte after it would be too" % model_b[:4])
    if bdhc_b[:4] != b"BDHC":
        die("the bdhc section is not a BDHC (%r)" % bdhc_b[:4])

    # Translate every tile's behaviour byte. Collision is bit 15 and is carried
    # unchanged: both games write it in the same place and mean the same thing
    # by it, measured on the pair of them, WATER_SEA is passable in 40,261 of
    # 40,385 platinum tiles and 42,949 of 43,097 heartgold ones, so what stops a
    # walker at the shore is the BEHAVIOUR in both, not the bit.
    for i in range(0, ta, 2):
        v = terrain[i] | (terrain[i + 1] << 8)
        beh, coll = v & 0xFF, v & 0x8000
        row = tmap.get(beh)
        if row is None:
            die("terrain byte 0x%02X has no row in TERRAIN_MAP" % beh)
        write, verdict = row
        stats[verdict] = stats.get(verdict, 0) + 1
        if write != beh:
            stats["rewritten"] = stats.get("rewritten", 0) + 1
        nv = (coll | write) & CARRIED_BITS
        terrain[i], terrain[i + 1] = nv & 0xFF, (nv >> 8) & 0xFF

    if prop_map is None:
        props_b, props = bytearray(), 0
    else:
        for i in range(props // PROP_RECORD):
            at = i * PROP_RECORD + PROP_MODEL_ID
            old = struct.unpack_from("<I", props_b, at)[0]
            if old not in prop_map:
                die("a map prop asks for model %d, which this port did not "
                    "carry, the closure is wrong, not the map" % old)
            struct.pack_into("<I", props_b, at, prop_map[old])
            stats["props"] = stats.get("props", 0) + 1

    return (struct.pack("<IIII", ta, props, model, bdhc)
            + bytes(terrain) + bytes(props_b) + model_b + bdhc_b)


def pick_props(placed: list[int], texset: bytes, archives: dict[str, list[bytes]],
               name: str) -> tuple[str, str]:
    """Which of the source's two prop archives a map's model ids index."""
    have = {n for n in nsbtx.read(texset)["textures"]}
    fits = []
    for path, arr in archives.items():
        want = set()
        for mid in set(placed):
            if mid >= len(arr):
                want = None
                break
            want |= nsbtx.model_texture_names(arr[mid])
        if want is not None and want and not (want - have):
            fits.append(path)
    if not fits:
        die("%s places props that neither %s nor %s can dress out of its own "
            "prop texture set, something else decides this map's archive and "
            "guessing would put the wrong furniture in it"
            % (name, SRC_PROPMODEL, SRC_PROPROOM))
    which = fits[0]
    return which, ("the buildings" if which == SRC_PROPMODEL else "the furniture")


def convert_events(member: bytes, ox: int, oy: int, hidden: set[int] | None,
                   gfx_of, headers: dict[int, int],
                   stats: dict) -> tuple[bytes, set[int]]:
    """One map's people, signs, doors and triggers as this game reads them."""
    ev = parse_events(member)
    used: set[int] = set()

    bgs = []
    for rec in ev["bg"]:
        r = bytearray(rec)
        struct.pack_into("<H", r, 0, SCRIPT_NONE)
        x, z = struct.unpack_from("<ii", r, 4)
        struct.pack_into("<ii", r, 4, x - ox, z - oy)
        bgs.append(bytes(r))

    objs = []
    for rec in ev["obj"]:
        r = bytearray(rec)
        sprite, = struct.unpack_from("<H", r, 2)
        flag, = struct.unpack_from("<H", r, 8)
        if flag != FLAG_NONE:
            if hidden is None:
                stats["dropped_unread"] = stats.get("dropped_unread", 0) + 1
                continue
            if flag in hidden:
                stats["dropped_hidden"] = stats.get("dropped_hidden", 0) + 1
                continue
        gid = gfx_of(sprite)
        used.add(gid)
        struct.pack_into("<H", r, 2, gid)
        struct.pack_into("<H", r, 6, TRAINER_TYPE_NONE)
        struct.pack_into("<H", r, 8, FLAG_NONE)
        struct.pack_into("<H", r, 10, SCRIPT_NONE)
        x, z = struct.unpack_from("<HH", r, 24)
        struct.pack_into("<HH", r, 24, x - ox, z - oy)
        objs.append(bytes(r))
        stats["people"] = stats.get("people", 0) + 1

    warps = []
    for rec in ev["warp"]:
        r = bytearray(rec)
        x, z, dest, anchor = struct.unpack_from("<4H", r, 0)
        if dest in headers:
            struct.pack_into("<4H", r, 0, x - ox, z - oy, headers[dest], anchor)
            stats["warps"] = stats.get("warps", 0) + 1
        else:
            struct.pack_into("<4H", r, 0, WARP_PARKED, WARP_PARKED, 0, 0)
            stats["warps_parked"] = stats.get("warps_parked", 0) + 1
        warps.append(bytes(r))

    stats["signs"] = stats.get("signs", 0) + len(bgs)
    stats["triggers_dropped"] = stats.get("triggers_dropped", 0) + len(ev["coord"])
    return build_events(dict(bg=bgs, obj=objs, warp=warps, coord=[])), used


def decode_bank(d: bytes) -> list[list[int]]:
    """One message bank as lists of charcodes."""
    n, seed = struct.unpack_from("<HH", d, 0)
    out = []
    for i in range(n):
        o, ln = struct.unpack_from("<II", d, 4 + i * 8)
        k = (seed * MSG_TABLE_MUL * (i + 1)) & 0xFFFF
        kk = k | (k << 16)
        o, ln = o ^ kk, ln ^ kk
        k = ((i + 1) * MSG_KEY_START) & 0xFFFF
        msg = []
        for j in range(ln):
            msg.append(struct.unpack_from("<H", d, o + j * 2)[0] ^ k)
            k = (k + MSG_KEY_INC) & 0xFFFF
        out.append(msg)
    return out


def encode_bank(msgs: list[list[int]], seed: int) -> bytes:
    """The inverse, laid out the way the engine reads it: header, table, text."""
    head = 4 + len(msgs) * 8
    body, table = bytearray(), bytearray()
    for i, msg in enumerate(msgs):
        off = head + len(body)
        k = ((i + 1) * MSG_KEY_START) & 0xFFFF
        for c in msg:
            body += struct.pack("<H", c ^ k)
            k = (k + MSG_KEY_INC) & 0xFFFF
        tk = (seed * MSG_TABLE_MUL * (i + 1)) & 0xFFFF
        tkk = tk | (tk << 16)
        table += struct.pack("<II", off ^ tkk, len(msg) ^ tkk)
    return struct.pack("<HH", len(msgs), seed) + bytes(table) + bytes(body)


def charmap(engine: Path) -> dict[str, int]:
    """character -> charcode, from the engine's own msgenc table."""
    f = engine / "tools" / "msgenc" / "charmap.txt"
    if not f.is_file():
        die("no %s, the label's characters have no authority without it" % f)
    out = {}
    for line in f.read_text(errors="replace").splitlines():
        # Trailing spaces are significant here, the file says so in its own
        # header, and the space character's row is exactly a trailing one.
        # Stripping the line drops the code for ' ' and every place name with
        # two words in it becomes unencodable.
        if line.startswith("//"):
            continue
        line = line.rstrip("\r\n")
        m = re.match(r"^([0-9A-Fa-f]{4})=(.)$", line)
        if m:
            out.setdefault(m.group(2), int(m.group(1), 16))
    return out


def rom_members(rom: Path, path: str) -> list[bytes]:
    return _rom(rom).narc_members(path)


def rom_file(rom: Path, path: str) -> bytes:
    return _rom(rom).file_bytes(path)


_ROMS: dict[str, object] = {}


def _rom(rom: Path):
    key = str(rom)
    if key not in _ROMS:
        sys.path.insert(0, str(_engine_pc()))
        from modport import NitroRom        # noqa: E402
        _ROMS[key] = NitroRom(rom)
    return _ROMS[key]


def _engine_pc() -> Path:
    out = subprocess.run([str(MMO / "tools" / "decomp_dir.sh"), "pokeplatinum"],
                         capture_output=True, text=True)
    return Path(out.stdout.strip()) / "pc"


# A package reaches the running game in one of two shapes.
COOK_FNV_OFFSET = 0xCBF29CE484222325

MEMBER_ROOT = {False: "narc", True: ".cooked/narc"}
FILE_ROOT = {False: "replace", True: ".cooked/fs"}

# The two whole files a ported map needs beside its members.
SDAT_PATH = "data/sound/pl_sound_data.sdat"
MATSHP_PATH = "fielddata/build_model/build_model_matshp.dat"
MATSHP_NO_SHAPES = (0, 0xFFFF)


def write_member(pkg: Path, narc: str, index: int, data: bytes,
                 cooked: bool = False) -> None:
    d = pkg / MEMBER_ROOT[cooked] / narc
    d.mkdir(parents=True, exist_ok=True)
    (d / str(index)).write_bytes(data)


def write_generated(pkg: Path, name: str, text: str) -> None:
    d = pkg / ".cooked" / "generated"
    d.mkdir(parents=True, exist_ok=True)
    (d / name).write_text(text)


def write_package_shell(pkg: Path, ident: str, name: str) -> None:
    """mod.toml and the digest, so the tree is loadable as it stands."""
    (pkg / ".cooked").mkdir(parents=True, exist_ok=True)
    (pkg / ".cooked" / "digest").write_text("v1 %016x\n" % COOK_FNV_OFFSET)
    toml = pkg / "mod.toml"
    if not toml.is_file():
        toml.write_text(
            'id = "%s"\n'
            'name = "%s"\n'
            'version = "1.0.0"\n'
            'authors = ["openmmo"]\n'
            'requires = []\n'
            'load_after = []\n' % (ident, name))


def empty_script_archive() -> bytes:
    """One script that does nothing, in this game's own script container."""
    table = struct.pack("<i", 2) + struct.pack("<H", SCRIPT_TABLE_END)
    return table + struct.pack("<H", SCRCMD_END)


def empty_init_scripts() -> bytes:
    """The arrival-script table of a map with no arrival scripts."""
    return bytes(4)


def empty_events() -> bytes:
    """A zone_event member with no people, warps, triggers or signs: four
    counts, all zero, and no arrays behind them."""
    return struct.pack("<4I", 0, 0, 0, 0)


def empty_message_bank() -> bytes:
    """A one-message bank whose message is empty, for a map that has no text of
    its own but whose header still has to name a bank."""
    return encode_bank([[0xFFFF]], 1)


def check_empties(rom_members_of, checks: list[tuple[str, str, bytes]]) -> None:
    """Every synthesised member has to be one the destination already ships."""
    for what, narc, blob in checks:
        pool = rom_members_of(narc)
        n = sum(1 for m in pool if m == blob)
        if n == 0:
            die("the %s this builds (%d bytes) is byte-identical to no member "
                "of %s, the shape was guessed, not read" % (what, len(blob), narc))
        print("portmap: %s matches %d of this game's own %s members"
              % (what, n, narc.rsplit("/", 1)[-1]))


def parse_args(argv: list[str]) -> dict:
    """The command line: one package, one destination image, and N maps."""
    opts = dict(rom=None, pkg=None, dest_rom=None, sdat=None, maps=[], over={})
    cur = None
    i = 1
    while i < len(argv):
        a = argv[i]
        if a == "--map":
            if i + 1 >= len(argv):
                die("--map needs a name from mmo/MAPS")
            cur = dict(name=argv[i + 1], header=None, label=None, bgm=None)
            opts["maps"].append(cur)
            i += 2
            continue
        if a in ("--header", "--bgm"):
            if cur is None:
                die("%s belongs to a --map and none has been named yet" % a)
            if i + 1 >= len(argv):
                die("%s needs a number" % a)
            cur[a[2:]] = int(argv[i + 1], 0)
            i += 2
            continue
        if a == "--label":
            if cur is None:
                die("--label belongs to a --map and none has been named yet")
            if i + 1 >= len(argv):
                die("--label needs the place name to draw")
            cur["label"] = argv[i + 1]
            i += 2
            continue
        if a == "--sdat":
            if i + 1 >= len(argv):
                die("--sdat needs the sound archive tools/portmusic.py wrote")
            opts["sdat"] = Path(argv[i + 1])
            i += 2
            continue
        if a == "--dest-rom":
            if i + 1 >= len(argv):
                die("--dest-rom needs THIS game's image; the place-name bank "
                    "a label is appended to is the destination's, not the "
                    "cartridge's")
            opts["dest_rom"] = Path(argv[i + 1])
            i += 2
            continue
        if a in ("--matrix", "--area", "--land", "--texset",
                 "--propmodel", "--areabuild", "--proptex",
                 "--script", "--events", "--msg", "--mmodel", "--mmlist"):
            if i + 1 >= len(argv):
                die("%s needs a member number" % a)
            opts["over"][a[2:]] = int(argv[i + 1], 0)
            i += 2
            continue
        if a.startswith("--"):
            die("unknown option %s" % a)
        if opts["rom"] is None:
            opts["rom"] = Path(a)
        elif opts["pkg"] is None:
            opts["pkg"] = Path(a)
        elif not opts["maps"]:
            opts["maps"].append(dict(name=a, header=None, label=None, bgm=None))
            cur = opts["maps"][0]
        else:
            die("unexpected argument %r" % a)
        i += 1
    if opts["rom"] is None or opts["pkg"] is None or not opts["maps"]:
        die("usage: portmap.py <rom> <package-dir> [--dest-rom PATH] "
            "[--sdat PATH]\n"
            "         --map NAME [--header N] [--label NAME] [--bgm N] ...\n"
            "       archive overrides: --matrix/--area/--land/--texset/"
            "--propmodel/\n"
            "         --areabuild/--proptex/--script/--events/--msg/"
            "--mmodel/--mmlist N")
    return opts


class Cursor:
    """Where the next appended member of each archive goes."""

    def __init__(self, over: dict[str, int]):
        self.at = {}
        for narc, count in PL_COUNT.items():
            self.at[narc] = count
        for key, narc in (("land", DST_LAND), ("matrix", DST_MATRIX),
                          ("area", DST_AREA), ("texset", DST_TEXSET),
                          ("propmodel", DST_PROPMODEL),
                          ("areabuild", DST_AREABUILD),
                          ("proptex", DST_PROPTEX), ("script", DST_SCRIPT),
                          ("events", DST_EVENTS), ("msg", DST_MSG),
                          ("mmodel", DST_MMODEL), ("mmlist", DST_MMLIST)):
            if key in over:
                self.at[narc] = over[key]

    def take(self, narc: str, n: int = 1) -> int:
        first = self.at[narc]
        self.at[narc] = first + n
        return first


def main(argv: list[str]) -> int:
    opts = parse_args(argv)
    rom, pkg, dest_rom = opts["rom"], opts["pkg"], opts["dest_rom"]

    maps = load_maps(MMO / "MAPS")
    sprites = load_sprites(MMO / "SPRITES")
    scenes = load_scenes(MMO / "MAPSCENES")
    tmap = load_terrain_map(MMO / "TERRAIN_MAP")

    for m in opts["maps"]:
        if m["name"] not in maps:
            die("no map called %r in mmo/MAPS" % m["name"])
        row = maps[m["name"]]
        if row["kind"] not in MAP_KINDS:
            die("%s is a %r and nothing of that kind has been ported: its "
                "presentation fields would be a city's with nothing behind "
                "them" % (m["name"], row["kind"]))
        if m["header"] is None:
            die("%s needs a --header: a map with no header id of its own is "
                "one nothing can address" % m["name"])
        m["header_src"] = row["header"]
        m["area"] = row["area"]
        m["matrix"] = row["matrix"]
        m["events_src"] = row["events"]
        m["kind"] = row["kind"]
        m["bgm_id"] = m["bgm"]
        m["src_bgm"] = row["bgm"]
    if len({m["header"] for m in opts["maps"]}) != len(opts["maps"]):
        die("two maps in this package answer to the same header id")

    cur = Cursor(opts["over"])
    src_matrices = rom_members(rom, SRC_MATRIX)
    overworld = parse_matrix(src_matrices[0])
    land_src = rom_members(rom, SRC_LAND)
    prop_archives = {SRC_PROPMODEL: rom_members(rom, SRC_PROPMODEL),
                     SRC_PROPROOM: rom_members(rom, SRC_PROPROOM)}
    build_src = rom_members(rom, SRC_AREABUILD)
    area_src = rom_members(rom, SRC_AREA)
    event_src = rom_members(rom, SRC_EVENTS)
    mmodel_src = rom_members(rom, SRC_MMODEL)

    # Package-wide registries. A model or a person carried for one map is not
    # carried again for the next: the destination member is the identity, and
    # two maps in a city share most of both.
    prop_map: dict[int, int] = {}
    gfx_map: dict[int, tuple[int, int]] = {}   # sprite id -> (gfx id, member)
    # A warp names the source map's header id. This is the only thing that turns
    # one into a door that works: source header -> the header this port gave it.
    # A destination that is not in this package has nowhere to go and is parked.
    warp_dest = {m["header_src"]: m["header"] for m in opts["maps"]}

    def gfx_of(sprite: int) -> int:
        """The appended OBJ_EVENT_GFX id for a source sprite, carrying its art."""
        if sprite in gfx_map:
            return gfx_map[sprite][0]
        if sprite not in sprites:
            die("sprite %d has no mmodel member in mmo/SPRITES, the map "
                "places somebody this port cannot draw" % sprite)
        member, name = sprites[sprite]
        if member >= len(mmodel_src):
            die("mmo/SPRITES sends sprite %d to mmodel member %d and the "
                "cartridge has %d" % (sprite, member, len(mmodel_src)))
        blob = mmodel_src[member]
        pal = nsbtx_palette_name(blob)
        if pal is None:
            die("mmodel member %d (sprite %d, %s) is not an NSBTX with a named "
                "palette" % (member, sprite, name))
        if pal.lower() != name.lower():
            die("mmo/SPRITES says sprite %d is %s and mmodel member %d calls "
                "its palette %r, the two oracles disagree, so the art is "
                "not carried" % (sprite, name, member, pal))
        gid = FIRST_COOKED_GFX + len(gfx_map)
        dst = cur.take(DST_MMODEL)
        write_member(pkg, DST_MMODEL, dst, blob, cooked=True)
        gfx_map[sprite] = (gid, dst)
        return gid

    label_bank = None
    label_seed = 0
    label_ids: dict[str, int] = {}
    rows = []
    for m in opts["maps"]:
        kind = KINDS[MAP_KINDS[m["kind"]]]
        stats: dict[str, int] = {}

        # ---------------------------------------------------------- the plane
        if m["matrix"] == 0:
            cells = [i for i, hid in enumerate(overworld["hdrs"])
                     if hid == m["header_src"] and overworld["land"][i] != EMPTY_CELL]
            if not cells:
                die("map header %d owns no cell of the overworld matrix"
                    % m["header_src"])
            xs = [c % overworld["w"] for c in cells]
            ys = [c // overworld["w"] for c in cells]
            x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys)
            w, h = x1 - x0 + 1, y1 - y0 + 1
            src_cells = [(y * overworld["w"] + x, overworld)
                         for y in range(y0, y1 + 1) for x in range(x0, x1 + 1)]
            own = m["header_src"]
        else:
            own_matrix = parse_matrix(src_matrices[m["matrix"]])
            w, h = own_matrix["w"], own_matrix["h"]
            x0 = y0 = 0
            src_cells = [(i, own_matrix) for i in range(w * h)]
            own = None      # a matrix of its own holds one map: every cell is it

        dst_land = cur.at[DST_LAND]
        chunks, alt, land_ids = [], [], []
        seen: dict[int, int] = {}
        for i, mx in src_cells:
            cid = mx["land"][i]
            if cid == EMPTY_CELL or (own is not None and mx["hdrs"][i] != own):
                land_ids.append(EMPTY_CELL)
                alt.append(0)
                continue
            if cid not in seen:
                seen[cid] = dst_land + len(chunks)
                chunks.append(cid)
            land_ids.append(seen[cid])
            alt.append(mx["alt"][i])
        cur.take(DST_LAND, len(chunks))

        # ------------------------------------------------------------- models
        area_rec = area_src[m["area"]]
        src_propset, src_texset, _dummy, _light = struct.unpack("<4H", area_rec)
        listed = build_src[src_propset]
        n_listed = struct.unpack_from("<H", listed, 0)[0]
        models = list(struct.unpack_from("<%dH" % n_listed, listed, 2))
        placed = []
        for cid in chunks:
            for mid in prop_model_ids(land_src[cid]):
                placed.append(mid)
                if mid not in models:
                    models.append(mid)
        # Which archive these ids index. Measured, not read: the area's own prop
        # texture set is what a prop's materials have to resolve in.
        texset = rom_members(rom, SRC_PROPTEX)[src_propset]
        which, what = pick_props(placed, texset, prop_archives, m["name"])
        prop_src = prop_archives[which]
        print("portmap:   props come from %s (%s), by what its %d placed models "
              "find in prop texture set %d"
              % (which, what, len(set(placed)), src_propset))
        for mid in models:
            if mid >= len(prop_src):
                die("the area's preload list names model %d and %s has %d"
                    % (mid, which, len(prop_src)))
            if (which, mid) in prop_map:
                continue
            dst = cur.take(DST_PROPMODEL)
            if dst >= MAX_MAP_PROP_MODEL_FILES:
                die("this package needs a prop model at %d and the engine's "
                    "loaded-model table is %d entries, it would be written "
                    "past" % (dst, MAX_MAP_PROP_MODEL_FILES))
            prop_map[(which, mid)] = dst
            write_member(pkg, DST_PROPMODEL, dst, prop_src[mid], cooked=True)
        # A model id means nothing without its archive, so the map that
        # renumbers the land data's prop records is keyed by the pair and this
        # is the half of it that belongs to this map.
        here = {mid: prop_map[(which, mid)] for mid in models}

        pl_build = cur.take(DST_AREABUILD)
        pl_ptex = cur.take(DST_PROPTEX)
        write_member(pkg, DST_AREABUILD, pl_build,
                     struct.pack("<H", len(models))
                     + struct.pack("<%dH" % len(models),
                                   *(here[x] for x in models)), cooked=True)
        # The PROP texture set is built, not copied. This game binds one set
        # over every prop an area loads and a material whose name is not in it
        # draws with whatever is at texture address 0; HeartGold's models each
        # carry a TEX0 that covers themselves and its shared sets are an
        # override. Goldenrod's happened to cover its buildings. Its Pokemon
        # Center's covers one of the 22 names its furniture asks for, and no
        # member of that archive covers them, so the room came out as white
        # boxes. tools/nsbtx.py takes the union: the source set, then each
        # carried model's own.
        write_member(pkg, DST_PROPTEX, pl_ptex,
                     nsbtx.merge([texset] + [prop_src[x] for x in models]),
                     cooked=True)

        for n, cid in enumerate(chunks):
            write_member(pkg, DST_LAND, dst_land + n,
                         convert_land(land_src[cid], tmap, stats, here),
                         cooked=True)

        dst_matrix = cur.take(DST_MATRIX)
        write_member(pkg, DST_MATRIX, dst_matrix,
                     build_matrix(w, h, alt, land_ids, m["name"][:8]),
                     cooked=True)

        pl_tex = cur.take(DST_TEXSET)
        pl_area = cur.take(DST_AREA)
        write_member(pkg, DST_TEXSET, pl_tex,
                     rom_members(rom, SRC_TEXSET)[src_texset], cooked=True)
        write_member(pkg, DST_AREA, pl_area,
                     struct.pack("<4H", pl_build, pl_tex, PL_AREA_DUMMY,
                                 kind["lighting"]), cooked=True)

        print("portmap: %s (heartgold header %d) -> %dx%d matrix %d, "
              "land %d..%d, area %d, texture set %d"
              % (m["name"], m["header_src"], w, h, dst_matrix, dst_land,
                 dst_land + len(chunks) - 1, pl_area, pl_tex))
        order = sorted(((k, v) for k, v in stats.items()
                        if k not in ("rewritten", "props")),
                       key=lambda kv: -kv[1])
        print("portmap:   %d tiles translated, %s"
              % (sum(v for k, v in order), ", ".join("%s %d" % kv for kv in order)))
        print("portmap:   %d prop models, %d appended so far, preload list %d, "
              "prop textures %d" % (len(models), len(prop_map), pl_build, pl_ptex))

        # ------------------------------------------------------------- events
        hidden = scenes.get(m["name"])
        estats: dict[str, int] = {}
        events, used_gfx = convert_events(
            event_src[m["events_src"]], x0 * 32, y0 * 32, hidden, gfx_of,
            warp_dest, estats)
        m["events_blob"] = events
        m["used_gfx"] = used_gfx
        m["estats"] = estats
        m["hidden_unread"] = hidden is None

        # ------------------------------------------------------------- header
        m["_alloc"] = dict(area=pl_area, matrix=dst_matrix)

    # A second pass writes what a header names, because the events of one map
    # address another map's header and every one of them has to be allocated
    # first for a door to have somewhere to go.
    for m in opts["maps"]:
        kind = KINDS[MAP_KINDS[m["kind"]]]
        pl_script = cur.take(DST_SCRIPT, 2)
        pl_events = cur.take(DST_EVENTS)
        pl_msg = cur.take(DST_MSG)
        pl_mm = cur.take(DST_MMLIST)

        scripts, inits = empty_script_archive(), empty_init_scripts()
        if dest_rom is not None:
            check_empties(lambda n: rom_members(dest_rom, n), [
                ("the do-nothing script archive", DST_SCRIPT, scripts),
                ("the empty arrival-script table", DST_SCRIPT, inits),
            ])
        write_member(pkg, DST_SCRIPT, pl_script, scripts, cooked=True)
        write_member(pkg, DST_SCRIPT, pl_script + 1, inits, cooked=True)
        write_member(pkg, DST_EVENTS, pl_events, m["events_blob"], cooked=True)
        write_member(pkg, DST_MSG, pl_msg, empty_message_bank(), cooked=True)

        # The preload list sizes the billboard resource heap (`count + 3` in
        # `ov5_021ECE40`), so a map names its own people rather than borrowing
        # a Sinnoh member sized for a different cast.
        ids = sorted(m["used_gfx"])
        if len(ids) + 1 > MAX_MAP_OBJECTS_TO_PRELOAD:
            die("%s places %d distinct people and the engine preloads at most "
                "%d" % (m["name"], len(ids), MAX_MAP_OBJECTS_TO_PRELOAD - 1))
        write_member(pkg, DST_MMLIST, pl_mm,
                     struct.pack("<%dH" % (len(ids) + 1), *ids,
                                 MAP_OBJECT_PRELOAD_SENTINEL), cooked=True)

        label_id = 0
        if m["label"] is not None:
            if dest_rom is None:
                die("--label needs --dest-rom: the location-name bank the "
                    "banner reads belongs to THIS game, and the cartridge does "
                    "not carry one this engine reads")
            cm = charmap(_engine_root())
            missing = sorted({c for c in m["label"] if c not in cm})
            if missing:
                die("the engine's charmap has no code for %s, a label it "
                    "cannot draw is worse than none"
                    % ", ".join(repr(c) for c in missing))
            if label_bank is None:
                raw = rom_members(dest_rom, DST_MSG)[MSG_BANK_LOCATION_NAMES]
                label_bank = decode_bank(raw)
                label_seed = struct.unpack_from("<H", raw, 2)[0]
            # A city and the rooms inside it draw the same banner, HeartGold
            # gives its Pokemon Center the city's own map section, so a name
            # already appended in this run is reused rather than appended
            # twice under two numbers.
            encoded = [cm[c] for c in m["label"]] + [0xFFFF]
            if m["label"] in label_ids:
                label_id = label_ids[m["label"]]
                print("portmap: %s draws the place name already appended as "
                      "text %d" % (m["name"], label_id))
            else:
                label_id = len(label_bank)
                label_bank.append(encoded)
                label_ids[m["label"]] = label_id
                print("portmap: %s place name %r appended to the location-name "
                      "bank as text %d" % (m["name"], m["label"], label_id))

        est = m["estats"]
        print("portmap: %s events -> %d people, %d signs, %d doors "
              "(%d parked), %d triggers dropped%s"
              % (m["name"], est.get("people", 0), est.get("signs", 0),
                 est.get("warps", 0), est.get("warps_parked", 0),
                 est.get("triggers_dropped", 0),
                 ("; %d hidden by the map's own arrival script"
                  % est["dropped_hidden"]) if est.get("dropped_hidden") else ""))
        if m["hidden_unread"] and est.get("dropped_unread"):
            print("portmap:   %d people carry a hide flag and %s has no row in "
                  "mmo/MAPSCENES, so they are left behind rather than guessed"
                  % (est["dropped_unread"], m["name"]))
        if m["used_gfx"]:
            print("portmap:   people drawn by gfx %s, mmodel %s"
                  % (sorted(m["used_gfx"]),
                     sorted(v[1] for v in gfx_map.values()
                            if v[0] in m["used_gfx"])))

        bgm = m["bgm_id"] if m["bgm_id"] is not None else 0
        rows.append([m["header"], m["_alloc"]["area"], pl_mm,
                     m["_alloc"]["matrix"], pl_script, pl_script + 1, pl_msg,
                     bgm, bgm, ENCOUNTERS_NONE, pl_events, label_id,
                     kind["window"], OVERWORLD_WEATHER_CLEAR, kind["camera"],
                     kind["map_type"], kind["battle_bg"], kind["bike"],
                     kind["run"], kind["escape"], kind["fly"]])
        print("portmap: %s -> map header %d (bank %d map %d on the wire), "
              "scripts %d/%d, events %d, text %d, preload %d, bgm %s"
              % (m["name"], m["header"], m["header"] >> 8, m["header"] & 0xFF,
                 pl_script, pl_script + 1, pl_events, pl_msg, pl_mm,
                 bgm if bgm else "none"))

    if label_bank is not None:
        write_member(pkg, DST_MSG, MSG_BANK_LOCATION_NAMES,
                     encode_bank(label_bank, label_seed), cooked=True)

    # The material-shape table, grown. A prop in the area's preload list is
    # loaded and textured by that list; what an appended id still needs is a row
    # in `build_model_matshp.dat`, whose locator table is one per model this game
    # shipped, because `MapProp_GetMaterialShapeIDsCount` indexes it by the
    # absolute model id and an id past 589 reads off the end and hangs the field
    # on the first draw.
    #
    # `.cooked/generated/extra_props.txt` was how that was answered, and it is
    # the wrong list for a port. `MapProp_IsCookedExtra` is a guard and a load:
    # `AreaDataManager_Load` walks the same list on every map load and reads each
    # id into the field heap. That is right for an authored prop, which is in no
    # area's preload list and would otherwise never arrive, and wrong for these,
    # which are in one. With a city and one room in a package it loaded both maps'
    # props on either map: 49 models where 31 fit, and the field heap ran out
    # under the first field effect.
    #
    # So the table itself grows instead. The file is opened with `FS_OpenFile`,
    # which is the call that asks `pc_modfs`, so a package can replace it; the
    # appended rows are `{0, 0xFFFF}`, which is not an invention but the row this
    # game already writes for a model with no material shapes.
    if prop_map:
        if dest_rom is None:
            die("--dest-rom is needed to carry a prop: the material-shape table "
                "an appended model id needs a row in is this game's own file")
        raw = rom_file(dest_rom, MATSHP_PATH)
        n_loc, n_ids = struct.unpack_from("<HH", raw, 0)
        if len(raw) != 4 + n_loc * 4 + n_ids * 4:
            die("%s is %d bytes and its two counts want %d"
                % (MATSHP_PATH, len(raw), 4 + n_loc * 4 + n_ids * 4))
        grown = max(prop_map.values()) + 1
        if grown <= n_loc:
            die("the appended props all fall inside this game's own %d "
                "material-shape rows, which cannot be right" % n_loc)
        out = (struct.pack("<HH", grown, n_ids)
               + raw[4:4 + n_loc * 4]
               + struct.pack("<HH", *MATSHP_NO_SHAPES) * (grown - n_loc)
               + raw[4 + n_loc * 4:])
        dest = pkg / FILE_ROOT[True] / MATSHP_PATH
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(out)
        print("portmap: material-shape locators %d -> %d, so an appended prop "
              "is a row rather than a bounds check" % (n_loc, grown))

    # And the second list a ported person needs. The four overlay005 gfx tables
    # are sentinel lists rather than arrays sized by the enum, and the port's
    # own hook answers an id that is in none of them by cloning the youngster's
    # renderer and reading the NSBTX named here, which is why an appended
    # person needs no rebuild.
    if gfx_map:
        write_generated(pkg, "billboard_gfx.txt",
                        "".join("%d %d\n" % (gid, member)
                                for gid, member in sorted(gfx_map.values())))

    # The sound archive tools/portmusic.py wrote, if there is one. A --bgm is
    # an index into THIS file: without it in the package the header names a
    # sequence the running game does not have.
    if opts["sdat"] is not None:
        if not opts["sdat"].is_file():
            die("no %s, run tools/portmusic.py first" % opts["sdat"])
        dest = pkg / FILE_ROOT[True] / SDAT_PATH
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(opts["sdat"].read_bytes())
        print("portmap: sound archive -> %s (%d bytes)"
              % (SDAT_PATH, dest.stat().st_size))
    elif any(m["bgm_id"] for m in opts["maps"]):
        print("portmap: no --sdat, so the --bgm ids name sequences only if the "
              "package already carries one")

    write_generated(pkg, "cooked_maps.txt",
                    "# " + " ".join(HEADER_FIELDS) + "\n"
                    + "".join(" ".join(str(v) for v in row) + "\n"
                              for row in rows))
    write_package_shell(pkg, pkg.name,
                        opts["maps"][0]["name"].replace("_", " ").title())
    print("portmap: scripts not carried (signs do not read and nobody talks)")
    print("portmap: %d map(s), %d prop models, %d people -> %s"
          % (len(rows), len(prop_map), len(gfx_map), pkg))
    return 0

def _engine_root() -> Path:
    return _engine_pc().parent



if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
