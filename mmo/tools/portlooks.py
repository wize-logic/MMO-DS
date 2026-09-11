#!/usr/bin/env python3
"""portlooks.py, the four player looks this engine does not ship, out of the player's own
HeartGold and Black cartridges.
"""

from __future__ import annotations

import argparse
import os
import shutil
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import nsbtx  # noqa: E402
import portsprites as ps  # noqa: E402
import rescramble  # noqa: E402

# --- the catalog's numbers (mmo/include/appearance.h) -----------------------

LOOK_GFX_BASE = 768
LOOK_STRIDE = 16
LOOKS = ("ethan", "lyra", "hilbert", "hilda")     # look 0..3
LOOK_GAME = ("hg", "hg", "bw", "bw")
LOOK_GENDER = (0, 1, 0, 1)

(WALK, BIKE, SURF, FIELD_MOVE, FISHING, SAVE, HEAL, POKETCH, SPRAYDUCK) = range(9)
STATE_NAMES = ("walk", "bike", "surf", "field_move", "fishing", "save",
               "heal", "poketch", "sprayduck")

# The Platinum id each state's sheet stands in for, by gender: what the
# `like` column names. From generated/object_events_gfx.txt.
LIKE = {
    WALK: (0, 97), BIKE: (21, 98), SURF: (178, 179), FIELD_MOVE: (176, 177),
    FISHING: (188, 189), SAVE: (198, 199), HEAL: (200, 201),
    POKETCH: (196, 197), SPRAYDUCK: (180, 181),
}

# Platinum's own mmodel member for the same sheet (field_sprites.naix), the
# template a Black sheet is laid into.
PT_MMODEL = {
    WALK: (90, 91), BIKE: (92, 93), SURF: (159, 160), FIELD_MOVE: (155, 156),
    FISHING: (166, 167), SAVE: (365, 366), HEAL: (367, 368),
    POKETCH: (363, 364), SPRAYDUCK: (157, 158),
}

# HeartGold's mmodel members (include/constants/mmodel.h) and the palette
# name each carries, which is how a wrong cartridge or a wrong member is
# caught before a byte is written.
HG_MMODEL = {
    WALK: ((69, "hero"), (70, "heroine")),
    BIKE: ((71, "cyclehero"), (72, "cycleheroine")),
    SURF: ((73, "swimhero"), (74, "swimheroine")),
    FIELD_MOVE: ((75, "sphero"), (76, "spheroine")),
    FISHING: ((79, "fishinghero"), (80, "fish_heroine")),
    SAVE: ((87, "savehero"), (88, "saveheroine")),
    HEAL: ((89, "banzaihero"), (90, "banzaiheroine")),
    POKETCH: ((85, "pokehero"), (86, "pokeheroine")),
    SPRAYDUCK: ((77, "waterhero"), (78, "waterheroine")),
}
HG_MMODEL_ARC = "a/0/8/1"
HG_TRFGRA = "a/0/5/8"
HG_TRBGRA = "a/0/0/6"
HG_CARD = "a/0/4/9"
HG_CARD_TILES = 44
HG_CARD_SCREEN = (54, 55)
HG_CARD_TX, HG_CARD_TY, HG_CARD_TW, HG_CARD_TH = 23, 6, 5, 9
HG_CARD_INSET = 3          # tiles in from the face box's left edge

# Black's overworld archive: the member for each state by gender, the
# texture name every frame carries, and how its frames (in the member's own
# dictionary order) lay into Platinum's sheet rows.
BW_OW_ARC = "a/0/4/9"
BW_OW = {
    WALK: ((6, 9), "t4x4hero",
           [0, 1, 0, 2, 3, 4, 3, 5, 6, 7, 6, 8, 9, 10, 9, 11,
            12, 13, 12, 14, 15, 16, 15, 17, 18, 19, 18, 20, 21, 22, 21, 23]),
    BIKE: ((7, 10), "t4x4cycle",
           [0, 1, 0, 2, 3, 4, 3, 5, 6, 7, 6, 8, 10, 11, 10, 14,
            12, 12, 13, 13, 12, 13, 9, 15]),
    SURF: ((8, 11), "t4x4swim", [0, 1, 2, 3]),
    FIELD_MOVE: ((152, 153), "t4x4cutinhero", [0, 1, 2, 3]),
    FISHING: ((154, 155), "t4x4hurahero", list(range(16))),
    SAVE: ((138, 139), "t4x4reporthero", [0, 1]),
}
BW_TRFGRA = "a/0/7/2"
BW_TRBGRA = "a/0/6/4"
BW_ENTRY = 8               # members an entry is, in both
BW_BACK_TICKS = (0, 8, 24, 38, 40, 44, 48, 60)   # Platinum's eight cells
BW_BACK_SCALE = 0.75
BW_BACK_TOP = -118         # the highest row any keyframe reaches (feet at 0)
BW_MAX_ENTRIES = 95        # a/0/7/2's count, the cheapest identity check

PT_TRFGRA = "poketool/trgra/trfgra.narc"
PT_TRBGRA = "poketool/trgra/trbgra.narc"
PT_MMODEL_ARC = "data/mmodel/mmodel.narc"
PT_MSG = "msgdata/pl_msg.narc"
PT_CLASS_BANK = 619
PT_CLASS_COUNT = 105
PT_BACK_COUNT = 11
PT_MMODEL_COUNT = 470
CLASS_FILES = 5
FRONT_TILES = 100
BACK_FRAMES = 8
SCAN_W, SCAN_H = 20, 10

CARD_W, CARD_H = 80, 88
NO_BALL = 0x7FFF

STAMP = "v1"
COOK_FNV_OFFSET = 0xCBF29CE484222325


def die(msg: str) -> None:
    print("portlooks: " + msg, file=sys.stderr)
    raise SystemExit(2)


# --- reading an image --------------------------------------------------------

class NitroRom:
    """A cartridge image: NitroFS path -> bytes, and an archive's members."""

    def __init__(self, path: Path):
        self.rom = path.read_bytes()
        if len(self.rom) < 0x200:
            die("%s is too small to be a cartridge image" % path)
        self.code = self.rom[0x0C:0x10].decode("ascii", "replace")
        self.fnt, _, self.fat, _ = struct.unpack_from("<IIII", self.rom, 0x40)
        self.names: dict[str, int] = {}
        self._walk(0xF000, "")

    def _walk(self, dirid: int, prefix: str) -> None:
        rom = self.rom
        off = self.fnt + (dirid & 0xFFF) * 8
        sub, first = struct.unpack_from("<IH", rom, off)
        p = self.fnt + sub
        fid = first
        while True:
            t = rom[p]
            p += 1
            if t == 0:
                break
            ln = t & 0x7F
            name = rom[p:p + ln].decode("ascii", "replace")
            p += ln
            if t & 0x80:
                d = struct.unpack_from("<H", rom, p)[0]
                p += 2
                self._walk(d, prefix + name + "/")
            else:
                self.names[prefix + name] = fid
                fid += 1

    def file(self, path: str) -> bytes:
        if path not in self.names:
            die("no %s in this image (%s)" % (path, self.code))
        fid = self.names[path]
        s, e = struct.unpack_from("<II", self.rom, self.fat + fid * 8)
        return self.rom[s:e]

    def members(self, path: str) -> list[bytes]:
        return narc_members(self.file(path), path)


def narc_members(blob: bytes, what: str) -> list[bytes]:
    if blob[:4] != b"NARC":
        die("%s is not an archive" % what)
    off = 16
    fat: list[tuple[int, int]] = []
    data = None
    while off + 8 <= len(blob):
        tag = blob[off:off + 4]
        size = struct.unpack_from("<I", blob, off + 4)[0]
        if tag == b"BTAF":
            n = struct.unpack_from("<I", blob, off + 8)[0]
            fat = [struct.unpack_from("<II", blob, off + 12 + 8 * i) for i in range(n)]
        elif tag == b"GMIF":
            data = off + 8
        off += size
    if data is None:
        die("%s has no member data" % what)
    return [blob[data + a:data + b] for a, b in fat]


def section(blob: bytes, magic: bytes) -> int:
    """Offset of a Nitro resource section's header (its name), or -1."""
    off = 16
    while off + 8 <= len(blob):
        if blob[off:off + 4] == magic:
            return off
        size = struct.unpack_from("<I", blob, off + 4)[0]
        if size < 8:
            break
        off += size
    return -1


# --- Gen 4 members: character data, palettes, cell banks, screens ------------

def ncgr_data(blob: bytes) -> tuple[int, int]:
    """(offset, size) of an NCGR's character bytes."""
    o = section(blob, b"RAHC")
    if o < 0:
        die("a character member with no RAHC")
    size, off = struct.unpack_from("<II", blob, o + 24)
    return o + 8 + off, size


def ncgr_shape(blob: bytes) -> tuple[int, int, int]:
    o = section(blob, b"RAHC")
    h, w, depth = struct.unpack_from("<HHI", blob, o + 8)
    return h, w, depth


def nclr_data(blob: bytes) -> tuple[int, int]:
    o = section(blob, b"TTLP")
    if o < 0:
        die("a palette member with no TTLP")
    size, off = struct.unpack_from("<II", blob, o + 16)
    return o + 8 + off, size


def nclr_colours(blob: bytes) -> list[int]:
    off, size = nclr_data(blob)
    return [struct.unpack_from("<H", blob, off + i)[0] for i in range(0, size, 2)]


def ncer_cells(blob: bytes) -> tuple[int, list[list[tuple[int, int, int, int, int]]]]:
    """(mapping mode, cells) with each cell a list of OAMs as
    (x, y, tiles wide, tiles tall, first tile), the first tile already
    through the mapping's multiplier, the trap openmmo_card.c names."""
    o = section(blob, b"KBEC")
    if o < 0:
        die("a cell bank with no KBEC")
    ncells, cattr, celldata, mapmode = struct.unpack_from("<HHII", blob, o + 8)
    csize = 16 if cattr & 1 else 8
    cellbase = o + 8 + celldata
    sizes = {0: [(1, 1), (2, 2), (4, 4), (8, 8)],
             1: [(2, 1), (4, 1), (4, 2), (8, 4)],
             2: [(1, 2), (1, 4), (2, 4), (4, 8)]}
    cells = []
    for c in range(ncells):
        n, _attr, oamoff = struct.unpack_from("<HHI", blob, cellbase + c * csize)
        ob = cellbase + ncells * csize + oamoff
        oams = []
        for i in range(n):
            a0, a1, a2 = struct.unpack_from("<HHH", blob, ob + i * 6)
            shape, size = a0 >> 14, a1 >> 14
            if shape == 3:
                continue
            y, x = a0 & 0xFF, a1 & 0x1FF
            if y >= 128:
                y -= 256
            if x >= 256:
                x -= 512
            tw, th = sizes[shape][size]
            oams.append((x, y, tw, th, (a2 & 0x3FF) << mapmode))
        cells.append(oams)
    return mapmode, cells


def tiles_from_canvas(canvas: list[list[int]], oams, count: int, square: int = 80) -> bytes:
    """4bpp character data of `count` tiles, laid out one OAM at a time the
    way the cell bank will read them back. The canvas is square x square,
    palette indices, its origin the sprite's top-left corner (the OAMs are
    signed from the centre)."""
    out = bytearray(count * 32)
    half = square // 2
    for (x, y, tw, th, first) in oams:
        for ty in range(th):
            for tx in range(tw):
                t = first + ty * tw + tx
                if t >= count:
                    continue
                for r in range(8):
                    for c in range(0, 8, 2):
                        px = x + half + tx * 8 + c
                        py = y + half + ty * 8 + r
                        lo = canvas[py][px] if 0 <= px < square and 0 <= py < square else 0
                        hi = canvas[py][px + 1] if 0 <= px + 1 < square and 0 <= py < square else 0
                        out[t * 32 + r * 4 + c // 2] = (lo & 0xF) | ((hi & 0xF) << 4)
    return bytes(out)


def raster_tiles(canvas: list[list[int]], tw: int, th: int) -> bytes:
    """4bpp character data of a tw x th tile raster of the canvas (which may
    be smaller: the rest is colour 0)."""
    out = bytearray(tw * th * 32)
    for ty in range(th):
        for tx in range(tw):
            t = ty * tw + tx
            for r in range(8):
                for c in range(0, 8, 2):
                    px, py = tx * 8 + c, ty * 8 + r
                    lo = canvas[py][px] if py < len(canvas) and px < len(canvas[py]) else 0
                    hi = canvas[py][px + 1] if py < len(canvas) and px + 1 < len(canvas[py]) else 0
                    out[t * 32 + r * 4 + c // 2] = (lo & 0xF) | ((hi & 0xF) << 4)
    return bytes(out)


def with_data(template: bytes, off: int, data: bytes) -> bytes:
    return template[:off] + data + template[off + len(data):]


def blank_canvas(w: int, h: int) -> list[list[int]]:
    return [[0] * w for _ in range(h)]


# --- NSBTX: replacing a template's textures and palette ----------------------

def nsbtx_layout(blob: bytes):
    """[(name, data offset, length)] for the textures in dictionary order and
    (palette data offset, length) for the first palette."""
    t = nsbtx.find_tex0(blob)
    if t is None:
        die("a texture set with no TEX0")
    tex_dict = t + struct.unpack_from("<H", blob, t + 0x0E)[0]
    tex_data = t + struct.unpack_from("<I", blob, t + 0x14)[0]
    pal_size = struct.unpack_from("<H", blob, t + 0x30)[0] << 3
    pal_dict = t + struct.unpack_from("<I", blob, t + 0x34)[0]
    pal_data = t + struct.unpack_from("<I", blob, t + 0x38)[0]
    n, at, names, unit = nsbtx._dict(blob, tex_dict)
    textures = []
    for i, name in enumerate(names):
        param, _extra = struct.unpack_from("<II", blob, at + i * unit)
        off = (param & 0xFFFF) << 3
        w = 8 << ((param >> 20) & 7)
        h = 8 << ((param >> 23) & 7)
        fmt = (param >> 26) & 7
        if fmt != 3 or w != 32 or h != 32:
            die("texture %r is %dx%d format %d; a player sheet is 32x32 4bpp"
                % (name, w, h, fmt))
        textures.append((name, tex_data + off, 512))
    pn, pat, pnames, punit = nsbtx._dict(blob, pal_dict)
    if pn < 1:
        die("a texture set with no palette")
    poff = struct.unpack_from("<H", blob, pat)[0] << 3
    starts = sorted({(struct.unpack_from("<H", blob, pat + i * punit)[0] << 3) for i in range(pn)} | {pal_size})
    plen = starts[starts.index(poff) + 1] - poff
    return textures, (pal_data + poff, plen), pnames


def nsbtx_palette_name(blob: bytes) -> str:
    _t, _p, names = nsbtx_layout(blob)
    return names[0]


# --- the message bank (Platinum's) --------------------------------------------

MSG_TABLE_MUL = 0x2FD
MSG_KEY_START = 0x91BD3
MSG_KEY_INC = 0x493D


def decode_bank(d: bytes) -> tuple[int, list[list[int]]]:
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
    return seed, out


def encode_bank(msgs: list[list[int]], seed: int) -> bytes:
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


# --- Black: the front and the back --------------------------------------------

def bw_front_canvas(arc: list[bytes], entry: int) -> tuple[list[list[int]], list[int]]:
    """The 80x80 picture of one front, and its sixteen colours."""
    b = entry * BW_ENTRY
    cols, rows, width, pixels = ps.read_charmap(arc[b + 1])
    mapmode, cells = ncer_cells(arc[b + 2])
    if mapmode != 1 or not cells:
        die("Black front %d maps its cells a way this does not read" % entry)
    canvas = blank_canvas(80, 80)
    for (x, y, tw, th, first) in reversed(cells[0]):
        for ty in range(th):
            for tx in range(tw):
                # 2D mapping over the 256-wide map: the doubled name is a
                # tile column in a row of 32, and the OAM's next row of
                # tiles is the map's next row
                row, col = (first // ps.MAP_PITCH) + ty, (first % ps.MAP_PITCH) + tx
                if row >= rows or col >= cols:
                    continue
                for r in range(8):
                    for c in range(8):
                        v = pixels[(row * 8 + r) * width + col * 8 + c]
                        if not v:
                            continue
                        px, py = x + 40 + tx * 8 + c, y + 40 + ty * 8 + r
                        if 0 <= px < 80 and 0 <= py < 80:
                            canvas[py][px] = v
    return canvas, nclr_colours(arc[b + 7])[:16]


def bw_back_block(arc: list[bytes], entry: int):
    b = entry * BW_ENTRY
    anim = arc[b + 3]
    return dict(
        charmap=ps.read_charmap(arc[b + 1]),
        cells=ps.read_cells(arc[b + 2]),
        cell_anims=ps.read_animations(ps.lz11(anim)),
        multicells=ps.read_multicells(arc[b + 4]),
        mc_anims=ps.read_animations(arc[b + 5]),
        colours=nclr_colours(arc[b + 7])[:16],
    )


def bw_back_frame(block, tick: int) -> list[list[int]]:
    """Black's back at one tick of its throw, drawn into an 80x80 canvas at
    three quarters, feet-anchored so the tallest keyframe's top is row 0."""
    mc = block["mc_anims"]
    if len(mc) < 2:
        die("Black's back has no throw sequence")
    pic = ps.compose(block["charmap"], block["cells"], block["multicells"],
                     block["cell_anims"], [mc[1]], tick)
    canvas = blank_canvas(80, 80)
    if not pic.px:
        return canvas
    # nearest sampling: every destination pixel asks which source pixel it is
    xs = [x for x, _ in pic.px]
    ys = [y for _, y in pic.px]
    src = {}
    for (x, y), v in pic.px.items():
        src[(x, y)] = v
    for py in range(80):
        sy = BW_BACK_TOP + int(py / BW_BACK_SCALE)
        for px in range(80):
            sx = int((px - 40) / BW_BACK_SCALE)
            # the frame's centre column is the source's x = 0
            v = src.get((sx, sy))
            if v:
                canvas[py][px] = v
    return canvas


def canvas_release_point(canvas: list[list[int]]) -> tuple[int, int]:
    """Where the engine's ball starts flying from: the hand at the end of the
    thrown arm, the right-most ink of the frame's upper half, as an offset
    from the frame's centre."""
    best = None
    for py in range(0, 40):
        for px in range(80):
            if canvas[py][px] and (best is None or px > best[0]):
                best = (px, py)
    if best is None:
        return (0, 0)
    return (best[0] - 40, best[1] - 40)


# --- the package ---------------------------------------------------------------

class Out:
    def __init__(self, pkg: Path):
        self.pkg = pkg
        self.cooked = pkg / ".cooked.new"
        self.members: list[tuple[str, int, bytes]] = []
        self.generated: dict[str, bytes] = {}

    def member(self, narc: str, index: int, data: bytes) -> None:
        self.members.append((narc, index, data))

    def text(self, name: str, s: str) -> None:
        self.generated[name] = s.encode("utf-8")

    def binary(self, name: str, b: bytes) -> None:
        self.generated[name] = b

    def write(self, stamp: str, fills: list[str]) -> None:
        if self.cooked.exists():
            shutil.rmtree(self.cooked)
        for narc, index, data in self.members:
            d = self.cooked / "narc" / narc
            d.mkdir(parents=True, exist_ok=True)
            (d / str(index)).write_bytes(data)
        g = self.cooked / "generated"
        g.mkdir(parents=True, exist_ok=True)
        for name, data in self.generated.items():
            (g / name).write_bytes(data)
        # No cook inputs, so the digest is the hash of nothing: what
        # pc_modfs's staleness check accepts for a ported package.
        (self.cooked / "digest").write_text("v1 %016x\n" % COOK_FNV_OFFSET)
        final = self.pkg / ".cooked"
        if final.exists():
            shutil.rmtree(final)
        self.cooked.rename(final)
        (self.pkg / "mod.toml").write_text(
            'id = "looks"\nname = "Player Looks"\nversion = "1.0.0"\n'
            'authors = ["openmmo"]\nrequires = []\nload_after = []\n')
        (self.pkg / "composed.txt").write_text(stamp + "\n" + "".join(f + "\n" for f in fills))


def others_bank(others: str | None, base: bytes) -> bytes:
    """Bank 619 as the other loaded packages leave it: the highest claim on
    that member among them, else the cartridge's own."""
    if not others or ":" not in others:
        return base
    root, names = others.split(":", 1)
    for name in names.split(","):
        # never its own last fill: that bank already holds the four
        if name == "looks":
            continue
        p = Path(root) / name / ".cooked" / "narc" / PT_MSG / str(PT_CLASS_BANK)
        if p.is_file():
            base = p.read_bytes()
    return base


def fill(hg: NitroRom, bw: NitroRom, pt: NitroRom, pkg: Path,
         mmodel_base: int, class_base: int, back_base: int,
         others: str | None) -> dict:
    if hg.code[:3] not in ("IPK", "IPG"):
        die("%s is not a Heart Gold or Soul Silver cartridge" % hg.code)
    if bw.code[:3] not in ("IRB", "IRA"):
        die("%s is not a Black or White cartridge" % bw.code)
    if pt.code[:3] != "CPU":
        die("%s is not a Platinum cartridge" % pt.code)
    mmodel_base = max(mmodel_base, PT_MMODEL_COUNT)
    class_base = max(class_base, PT_CLASS_COUNT)
    back_base = max(back_base, PT_BACK_COUNT)

    hg_mmodel = hg.members(HG_MMODEL_ARC)
    hg_front = hg.members(HG_TRFGRA)
    hg_back = hg.members(HG_TRBGRA)
    hg_card = hg.members(HG_CARD)
    bw_ow = bw.members(BW_OW_ARC)
    bw_front = bw.members(BW_TRFGRA)
    bw_back = bw.members(BW_TRBGRA)
    pt_mmodel = pt.members(PT_MMODEL_ARC)
    pt_front = pt.members(PT_TRFGRA)
    pt_back = pt.members(PT_TRBGRA)
    pt_msg = pt.members(PT_MSG)
    if len(bw_front) != BW_MAX_ENTRIES * BW_ENTRY:
        die("Black's trainer archive holds %d members, not %d entries of %d"
            % (len(bw_front), BW_MAX_ENTRIES, BW_ENTRY))
    if len(pt_front) != PT_CLASS_COUNT * CLASS_FILES or len(pt_back) != PT_BACK_COUNT * CLASS_FILES:
        die("your Platinum's trainer archives are not the sizes this engine builds")

    out = Out(pkg)
    stats = {}
    gfx_rows = []
    at = mmodel_base

    # --- overworld sheets
    for look in range(4):
        gender = LOOK_GENDER[look]
        if LOOK_GAME[look] == "hg":
            for state in range(9):
                member, want = HG_MMODEL[state][gender]
                blob = hg_mmodel[member]
                have = nsbtx_palette_name(blob)
                if have != want:
                    die("HeartGold member %d is %r, not %r: not the sheet this expects"
                        % (member, have, want))
                out.member(PT_MMODEL_ARC, at, blob)
                gfx_rows.append((LOOK_GFX_BASE + look * LOOK_STRIDE + state, at, LIKE[state][gender]))
                at += 1
                stats["hg sheets"] = stats.get("hg sheets", 0) + 1
        else:
            for state, ((m_member, f_member), want, order) in BW_OW.items():
                src = bw_ow[(m_member, f_member)[gender]]
                textures, _pal, names = nsbtx_layout(src)
                if names[0] != want:
                    die("Black member %d is %r, not %r" % ((m_member, f_member)[gender], names[0], want))
                if max(order) >= len(textures):
                    die("Black's %s sheet has %d frames; %d asked" % (want, len(textures), max(order)))
                template = pt_mmodel[PT_MMODEL[state][gender]]
                t_textures, (poff, plen), _n = nsbtx_layout(template)
                if len(t_textures) != len(order):
                    die("Platinum's %s sheet has %d frames; the layout names %d"
                        % (STATE_NAMES[state], len(t_textures), len(order)))
                s_textures, (spoff, splen), _n = nsbtx_layout(src)
                # dictionary index k of the template is its name ".n": sheet
                # row n-1; the layout says which Black frame that row is
                sheet_rows = sorted(range(len(order)), key=lambda i: str(i + 1))
                blob = bytearray(template)
                for k, (name, toff, tlen) in enumerate(t_textures):
                    row = sheet_rows[k]
                    _sname, soff, slen = s_textures[order[row]]
                    blob[toff:toff + tlen] = src[soff:soff + slen]
                take = min(plen, splen, 32)
                blob[poff:poff + take] = src[spoff:spoff + take]
                out.member(PT_MMODEL_ARC, at, bytes(blob))
                gfx_rows.append((LOOK_GFX_BASE + look * LOOK_STRIDE + state, at, LIKE[state][gender]))
                at += 1
                stats["bw sheets"] = stats.get("bw sheets", 0) + 1

    # --- battle fronts and backs, the card faces, the ball rows
    look_rows = []
    class_rows = []
    for look in range(4):
        gender = LOOK_GENDER[look]
        cls = class_base + look
        back = back_base + look
        ball = None
        if LOOK_GAME[look] == "hg":
            src_c = gender   # Ethan 0, Lyra 1 in both archives
            for k in range(CLASS_FILES):
                blob = hg_front[src_c * CLASS_FILES + k]
                if k == 4:
                    blob = rescan(blob)
                out.member(PT_TRFGRA, cls * CLASS_FILES + k, blob)
            for k in range(CLASS_FILES):
                blob = hg_back[src_c * CLASS_FILES + k]
                if k == 4:
                    blob = rescan(blob)
                out.member(PT_TRBGRA, back * CLASS_FILES + k, blob)
            out.binary("look_card_%d.bin" % look, hg_card_face(hg_card, hg_front, gender))
            look_rows.append((look, cls, back, gender, [NO_BALL] * 12))
        else:
            entry = gender    # Hilbert 0, Hilda 1
            canvas, colours = bw_front_canvas(bw_front, entry)
            tmpl = gender * CLASS_FILES
            mapmode, cells = ncer_cells(pt_front[tmpl + 2])
            ncgr = pt_front[tmpl]
            off, size = ncgr_data(ncgr)
            if size != FRONT_TILES * 32:
                die("Platinum's front is not %d tiles" % FRONT_TILES)
            out.member(PT_TRFGRA, cls * CLASS_FILES + 0,
                       with_data(ncgr, off, tiles_from_canvas(canvas, cells[0], FRONT_TILES)))
            out.member(PT_TRFGRA, cls * CLASS_FILES + 1, with_colours(pt_front[tmpl + 1], colours))
            out.member(PT_TRFGRA, cls * CLASS_FILES + 2, pt_front[tmpl + 2])
            out.member(PT_TRFGRA, cls * CLASS_FILES + 3, pt_front[tmpl + 3])
            out.member(PT_TRFGRA, cls * CLASS_FILES + 4, scan_of(pt_front[tmpl + 4], canvas))
            out.binary("look_card_%d.bin" % look, card_of_canvas(canvas, colours))

            block = bw_back_block(bw_back, entry)
            btmpl = gender * CLASS_FILES
            mapmode, bcells = ncer_cells(pt_back[btmpl + 2])
            if len(bcells) != BACK_FRAMES:
                die("Platinum's back has %d cells, not %d" % (len(bcells), BACK_FRAMES))
            bncgr = pt_back[btmpl]
            boff, bsize = ncgr_data(bncgr)
            if bsize != BACK_FRAMES * FRONT_TILES * 32:
                die("Platinum's back is not %d frames of %d tiles" % (BACK_FRAMES, FRONT_TILES))
            frames = [bw_back_frame(block, t) for t in BW_BACK_TICKS]
            data = bytearray()
            for k, frame in enumerate(frames):
                # A cell names tiles 0..99 of its own frame: the bank is read
                # over a per-frame VRAM transfer of the sheet's k-th hundred.
                # Cells 3..7 also sit their square sixteen to nineteen pixels
                # right of the sprite (the throw leans in); the tiles are
                # laid from the square's own corner and the engine keeps the
                # lean.
                oams = bcells[k]
                minx = min(x for (x, _y, _tw, _th, _f) in oams)
                miny = min(y for (_x, y, _tw, _th, _f) in oams)
                if max(first + tw * th for (_x, _y, tw, th, first) in oams) > FRONT_TILES:
                    die("Platinum's back cell %d names tiles past its frame" % k)
                data += tiles_from_canvas(frame, [(x - (minx + 40), y - (miny + 40), tw, th, first)
                                                  for (x, y, tw, th, first) in oams], FRONT_TILES)
            out.member(PT_TRBGRA, back * CLASS_FILES + 0, with_data(bncgr, boff, bytes(data)))
            out.member(PT_TRBGRA, back * CLASS_FILES + 1, with_colours(pt_back[btmpl + 1], block["colours"]))
            out.member(PT_TRBGRA, back * CLASS_FILES + 2, pt_back[btmpl + 2])
            out.member(PT_TRBGRA, back * CLASS_FILES + 3, pt_back[btmpl + 3])
            out.member(PT_TRBGRA, back * CLASS_FILES + 4, scan_of(pt_back[btmpl + 4], frames[0]))
            # The release point is measured on the frame's square, and the
            # engine draws cell 3's square where its OAMs lean it.
            rx, ry = canvas_release_point(frames[3])
            rx += min(x for (x, _y, _tw, _th, _f) in bcells[3]) + 40
            ry += min(y for (_x, y, _tw, _th, _f) in bcells[3]) + 40
            ball = [NO_BALL, NO_BALL, NO_BALL, NO_BALL, NO_BALL, NO_BALL,
                    rx, ry, NO_BALL, NO_BALL, NO_BALL, NO_BALL]
            look_rows.append((look, cls, back, gender, ball))
        class_rows.append((cls, gender))
        stats["looks"] = stats.get("looks", 0) + 1

    # --- the class names: "Pokemon Trainer", the entry the gender's own class has
    base = others_bank(others, pt_msg[PT_CLASS_BANK])
    seed, names = decode_bank(base)
    if len(names) != class_base:
        die("bank %d holds %d names and the first look's class is %d; the "
            "packages loaded beside this one do not line up" % (PT_CLASS_BANK, len(names), class_base))
    for look in range(4):
        names.append(list(names[LOOK_GENDER[look]]))
    out.member(PT_MSG, PT_CLASS_BANK, encode_bank(names, seed))

    out.text("billboard_gfx.txt",
             "".join("%d %d -1 -1 %d\n" % row for row in gfx_rows))
    out.text("trainer_classes.txt",
             "".join("%d %d 0 0 0\n" % row for row in class_rows))
    out.text("player_looks.txt",
             "".join("%d %d %d %d %s\n" % (look, cls, back, -1 if LOOK_GAME[look] == "bw" else gender,
                                            " ".join(str(v) for v in ball))
                     for (look, cls, back, gender, ball) in look_rows))
    stamp = "%s %d %d %d" % (STAMP, mmodel_base, class_base, back_base)
    out.write(stamp, ["fill %s" % hg.code, "fill %s" % bw.code, "fill %s" % pt.code])
    stats["mmodel"] = at - mmodel_base
    return stats


def rescan(blob: bytes) -> bytes:
    """A HeartGold scan sheet, scrambled the DP way, re-encoded the Platinum
    way this engine's PokemonSprite_Decrypt undoes on every trainer archive."""
    off, size = ncgr_data(blob)
    plain = rescramble.decode(blob[off:off + size], 1)
    return with_data(blob, off, rescramble.encode(plain, 2))


def scan_of(template: bytes, canvas: list[list[int]]) -> bytes:
    """The scan sheet: the picture as a 20x10 raster of tiles, the right half
    empty, scrambled the way the engine reads it."""
    off, size = ncgr_data(template)
    if size != SCAN_W * SCAN_H * 32:
        die("Platinum's scan sheet is not %dx%d tiles" % (SCAN_W, SCAN_H))
    plain = raster_tiles(canvas, SCAN_W, SCAN_H)
    return with_data(template, off, rescramble.encode(plain, 2))


def with_colours(template: bytes, colours: list[int]) -> bytes:
    off, size = nclr_data(template)
    data = b"".join(struct.pack("<H", c) for c in colours[:16])
    if len(data) > size:
        die("a palette member too small for sixteen colours")
    return with_data(template, off, data)


def card_of_canvas(canvas: list[list[int]], colours: list[int]) -> bytes:
    """A card face: sixteen colours, then 110 tiles of 8bpp, the 80x80
    picture at the top of the 80x88 box, indices 1..15 where the card's own
    face palette block is added by the client."""
    tiles = bytearray(CARD_W // 8 * (CARD_H // 8) * 64)
    for py in range(CARD_H):
        for px in range(CARD_W):
            v = canvas[py][px] if py < len(canvas) and px < len(canvas[py]) else 0
            if v:
                t = (py // 8) * (CARD_W // 8) + px // 8
                tiles[t * 64 + (py % 8) * 8 + (px % 8)] = v & 0xF
    pal = b"".join(struct.pack("<H", c) for c in (colours + [0] * 16)[:16])
    return pal + bytes(tiles)


def hg_card_face(arc: list[bytes], front: list[bytes], gender: int) -> bytes:
    """HeartGold's own card picture of Ethan or Lyra, placed in the box where
    the card draws a face, in the class's own front palette."""
    tiles = arc[HG_CARD_TILES]
    h, w, depth = ncgr_shape(tiles)
    off, size = ncgr_data(tiles)
    if depth != 4:
        die("HeartGold's card tiles are not 8bpp")
    tdata = tiles[off:off + size]
    scr = arc[HG_CARD_SCREEN[gender]]
    o = section(scr, b"NRCS")
    if o < 0:
        die("HeartGold's card screen has no NRCS")
    sw, sh, _fmt, ssize = struct.unpack_from("<HHII", scr, o + 8)
    ents = [struct.unpack_from("<H", scr, o + 20 + i)[0] for i in range(0, ssize, 2)]
    pitch = sw // 8
    canvas = blank_canvas(CARD_W, CARD_H)
    blocks = set()
    for ty in range(HG_CARD_TH):
        for tx in range(HG_CARD_TW):
            e = ents[(HG_CARD_TY + ty) * pitch + HG_CARD_TX + tx]
            t, hf, vf = e & 0x3FF, (e >> 10) & 1, (e >> 11) & 1
            if t * 64 + 64 > len(tdata):
                continue
            for r in range(8):
                for c in range(8):
                    v = tdata[t * 64 + (7 - r if vf else r) * 8 + (7 - c if hf else c)]
                    if not v:
                        continue
                    blocks.add(v // 16)
                    canvas[ty * 8 + r][(HG_CARD_INSET + tx) * 8 + c] = v % 16
    # Ethan's picture is drawn in palette block 5 and Lyra's in block 4;
    # each is one block, which is what lets it land in the one slot the card
    # gives a face. The block's colours are not in this archive: the card
    # loads the class's front palette there.
    if len(blocks) != 1:
        die("HeartGold's card face spans palette blocks %s" % sorted(blocks))
    colours = nclr_colours(front[gender * CLASS_FILES + 1])[:16]
    return card_of_canvas(canvas, colours)


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--hg", required=True, type=Path)
    ap.add_argument("--bw", required=True, type=Path)
    ap.add_argument("--pt", required=True, type=Path)
    ap.add_argument("--pkg", required=True, type=Path)
    ap.add_argument("--mmodel-base", type=int, default=PT_MMODEL_COUNT)
    ap.add_argument("--class-base", type=int, default=PT_CLASS_COUNT)
    ap.add_argument("--back-base", type=int, default=PT_BACK_COUNT)
    ap.add_argument("--others", default=None,
                    help="DIR:pkg,pkg, packages loaded beside this one, whose bank 619 this grows")
    a = ap.parse_args(argv)
    for p in (a.hg, a.bw, a.pt):
        if not p.is_file():
            die("no such image: %s" % p)
    stats = fill(NitroRom(a.hg), NitroRom(a.bw), NitroRom(a.pt), a.pkg,
                 a.mmodel_base, a.class_base, a.back_base, a.others)
    print("portlooks: " + ", ".join("%s %d" % kv for kv in sorted(stats.items())))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
