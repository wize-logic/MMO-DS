#!/usr/bin/env python3
"""Battle sprites for the species a Gen 5 cartridge adds, in this game's own sheet."""
from __future__ import annotations

import argparse
import math
import multiprocessing
import os
import struct
import sys
import zlib
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import porticons  # noqa: E402
import rescramble  # noqa: E402

MMO = Path(__file__).resolve().parent.parent

SPRITE_NARC_BLACK = "a/0/0/4"
POKEGRA_NARC = "poketool/pokegra/pl_pokegra.narc"
HEIGHT_NARC = "poketool/pokegra/height.narc"
# Black's cell banks and animation banks, decompressed, twelve members a
# species by ENGINE id: front charmap, front female charmap, front cells,
# front cell animations, front multi-cells, front multi-cell animations, then
# the same six for the back.
ANIM_NARC = "poketool/pokegra/mmo_anim.narc"
ANIM_STRIDE = 12

# Black: twenty members a species, the front half at +0 and the back at +9,
# the two palettes last. Within a half: +0 still sheet, +2 character map,
# +4 cells, +5 cell animations, +6 multi-cells, +7 multi-cell animations.
BLOCK = 20
FRONT, BACK = 0, 9
PAL_NORMAL, PAL_SHINY = 18, 19
CHARMAP, CELLS, CELL_ANIM, MULTICELLS, MULTICELL_ANIM = 2, 4, 5, 6, 7
MAP_PITCH = 32          # 2D OBJ mapping: a character name is row * 32 + column

# Platinum: six members a species, back female, back male, front female,
# front male, normal palette, shiny palette, and four height bytes in the
# same face/gender order. Both archives end exactly at species 494.
POKEGRA_MEMBERS = 2964
HEIGHT_MEMBERS = 1976
POKEGRA_STRIDE = 6
HEIGHT_STRIDE = 4
FIRST_PORTED = 494
LAST_PORTED = 649
FIRST_SPECIES = 1
# Species whose sprites this engine reads out of pl_otherpoke (BuildPokemonSpriteTemplate's
# case arms), and Spinda, whose spots the engine paints at Platinum's coordinates. They keep
# Platinum's art until their forms are mapped onto Black's blocks past 649.
KEEP_PLATINUM = {201, 327, 351, 386, 412, 413, 421, 422, 423, 479, 487, 492, 493}
FEMALE_STILL, FEMALE_CHARMAP = 1, 3      # +10 and +12 for the back half
MAX_FRAMES = 8                            # official bakes about six a face
BACK_MAGNIFY = 1.0                        # no enlargement: see fit(); the plugin's compositor uses the same
MAX_CANDIDATES = 48                       # keyframes composed per face before the eight are chosen
STRIP_MAGIC = b"MMOA"
# The engine spends 494 and 495 on SPECIES_EGG and SPECIES_BAD_EGG, so inside it
# Victini and Snivy live at 650 and 651 (mmo/include/species_port.h). Their
# sheets and heights go where the engine's own arithmetic will look for those
# ids, and the two egg slots are hole-filled so the appended run has no gap.
ENGINE_ID = {494: 650, 495: 651}

FRAME = 80
# The frame a single battle draws through (mods/openmmo/src/openmmo_spriteframe.c).
# The height byte is chosen for this frame, see fit().
WIDE_W, WIDE_H = 128, 88
SHEET_W, SHEET_H = 2 * FRAME, FRAME       # one pair of frames; a strip stacks pairs
SHEET_BYTES = SHEET_W * SHEET_H // 2
NCGR_HEADER = 0x30
PALETTE_BYTES = 72
SCRAMBLE_MODE = 2       # this game's direction, rescramble.py

OAM_SHAPES = {
    (0, 0): (8, 8), (0, 1): (16, 16), (0, 2): (32, 32), (0, 3): (64, 64),
    (1, 0): (16, 8), (1, 1): (32, 8), (1, 2): (32, 16), (1, 3): (64, 32),
    (2, 0): (8, 16), (2, 1): (8, 32), (2, 2): (16, 32), (2, 3): (32, 64),
}

# NNSG2dAnimationElement: what one animation frame carries besides an index.
ELEMENT_INDEX, ELEMENT_SRT, ELEMENT_T = 0, 1, 2
# NNSG2dAnimationPlayMode 3 and 4 run backwards; 2 and 4 loop.
PLAY_REVERSE = (3, 4)
PLAY_LOOP = (2, 4)
# NNSG2dMCAnimationPlayMode, the low nibble of a node's attribute: whether a
# node's own animation restarts when the multi-cell changes.
MC_NODE_RESET = 0
FX32_ONE = 0x1000


def die(msg):
    sys.stderr.write("portsprites: %s\n" % msg)
    sys.exit(1)


# ---------------------------------------------------------------- containers

def lz11(data):
    """The extended LZ the engine port calls MI_UncompressLZ8 (pc/src/pc_mi.c):
    header byte 0x11, size in the next three, then flag-led runs whose length
    field widens with its top nibble. A bare member is returned as it is."""
    if not data or data[0] != 0x11:
        return data
    size = struct.unpack_from("<I", data, 0)[0] >> 8
    pos, out = 4, bytearray()
    while len(out) < size:
        flags = data[pos]
        pos += 1
        for i in range(8):
            if len(out) >= size:
                break
            if not flags & (0x80 >> i):
                out.append(data[pos])
                pos += 1
                continue
            top = data[pos] >> 4
            if top == 1:
                length = (((data[pos] & 0xF) << 12) | (data[pos + 1] << 4)
                          | (data[pos + 2] >> 4)) + 0x111
                back = (((data[pos + 2] & 0xF) << 8) | data[pos + 3]) + 1
                pos += 4
            elif top == 0:
                length = (((data[pos] & 0xF) << 4) | (data[pos + 1] >> 4)) + 0x11
                back = (((data[pos + 1] & 0xF) << 8) | data[pos + 2]) + 1
                pos += 3
            else:
                length = top + 1
                back = (((data[pos] & 0xF) << 8) | data[pos + 1]) + 1
                pos += 2
            for _ in range(length):
                out.append(out[-back])
    if len(out) != size:
        die("LZ11 member declares %d bytes and yields %d" % (size, len(out)))
    return bytes(out)


def sections(blob, magic):
    """Yield (offset of the section's own data, size) for every section named
    `magic` in a Nitro resource: a 16-byte file header, then sections that each
    start with a 4-byte name and a 4-byte size."""
    off = 16
    while off + 8 <= len(blob):
        name = blob[off:off + 4]
        size = struct.unpack_from("<I", blob, off + 4)[0]
        if size < 8:
            return
        if name == magic:
            yield off + 8, size
        off += size


def read_charmap(member):
    """The character map: (columns, rows, pixels) with pixels a bytearray of one
    palette index per pixel, row-major, columns*8 wide."""
    blob = lz11(member)
    for base, _size in sections(blob, b"RAHC"):
        rows, cols = struct.unpack_from("<HH", blob, base)
        depth, flags = struct.unpack_from("<II", blob, base + 4), None
        depth = depth[0]
        flags = struct.unpack_from("<I", blob, base + 12)[0]
        size, offset = struct.unpack_from("<II", blob, base + 16)
        if depth != 3 or not flags & 1:
            die("a character map that is not a linear 4bpp bitmap: depth %d flags %#x"
                % (depth, flags))
        data = blob[base + offset:base + offset + size]
        width = cols * 8
        pixels = bytearray(len(data) * 2)
        for i, byte in enumerate(data):
            pixels[2 * i] = byte & 0xF
            pixels[2 * i + 1] = byte >> 4
        return cols, rows, width, pixels
    die("no RAHC section in a character member")


def read_cells(member):
    """The cell bank: a list of cells, each a list of OAMs
    (x, y, width, height, character, hflip, vflip)."""
    for base, _size in sections(member, b"KBEC"):
        count, attr = struct.unpack_from("<HH", member, base)
        array, mapping = struct.unpack_from("<II", member, base + 4)
        if mapping != 4:
            die("a cell bank with character mapping %d; this reads 2D (4) only" % mapping)
        cell_size = 16 if attr & 1 else 8
        heads = base + array
        oams_at = heads + count * cell_size
        cells = []
        for i in range(count):
            n, _cattr, off = struct.unpack_from("<HHI", member, heads + i * cell_size)
            oams = []
            for j in range(n):
                a0, a1, a2 = struct.unpack_from("<HHH", member, oams_at + off + j * 6)
                y = a0 & 0xFF
                y = y - 256 if y & 0x80 else y
                x = a1 & 0x1FF
                x = x - 512 if x & 0x100 else x
                w, h = OAM_SHAPES[((a0 >> 14) & 3, (a1 >> 14) & 3)]
                # Every OAM here is affine (attr0 bit 8), so attr1 bits 9..13 are
                # the affine index, not flips. A double-size one (bit 9) has its
                # position stored for the doubled box: the picture sits half a
                # size in from it (NitroSystem g2di_OamUtil.h,
                # NNSi_G2dRemovePositionAdjustmentFromDoubleAffineOBJ). Chimchar's
                # head is one and drew a head's width away from its body.
                affine, double = (a0 >> 8) & 1, (a0 >> 9) & 1
                hf, vf = (0, 0) if affine else ((a1 >> 12) & 1, (a1 >> 13) & 1)
                if affine and double:
                    x += w // 2
                    y += h // 2
                oams.append((x, y, w, h, a2 & 0x3FF, hf, vf))
            cells.append(oams)
        return cells
    die("no KBEC section in a cell member")


def read_multicells(member):
    """The multi-cell bank: a list of multi-cells, each a list of nodes
    (sequence, x, y, attribute)."""
    for base, _size in sections(member, b"KBCM"):
        count = struct.unpack_from("<H", member, base)[0]
        array, nodes = struct.unpack_from("<II", member, base + 4)
        out = []
        for i in range(count):
            n, _anims, off = struct.unpack_from("<HHI", member, base + array + i * 8)
            out.append([struct.unpack_from("<HhhH", member, base + nodes + off + k * 8)
                        for k in range(n)])
        return out
    die("no KBCM section in a multi-cell member")


def read_animations(member):
    """An animation bank (cell or multi-cell): a list of sequences, each
    {mode, loop, frames: [(index, duration, sx, sy, rotation, px, py)]}. Scale
    is fx32, rotation a 16-bit turn; a frame without them reads as identity."""
    blob = lz11(member)
    for base, _size in sections(blob, b"KNBA"):
        count = struct.unpack_from("<H", blob, base)[0]
        seq_at, frame_at, content_at = struct.unpack_from("<III", blob, base + 4)
        seqs = []
        for i in range(count):
            n, loop, kind, mode, frames_off = struct.unpack_from(
                "<HHIII", blob, base + seq_at + i * 16)
            element = kind & 0xFFFF
            frames = []
            for j in range(n):
                content, duration = struct.unpack_from(
                    "<IH", blob, base + frame_at + frames_off + j * 8)
                at = base + content_at + content
                if element == ELEMENT_SRT:
                    index, rot, sx, sy, px, py = struct.unpack_from("<HHiihh", blob, at)
                elif element == ELEMENT_T:
                    index, _pad, px, py = struct.unpack_from("<HHhh", blob, at)
                    sx = sy = FX32_ONE
                    rot = 0
                else:
                    index = struct.unpack_from("<H", blob, at)[0]
                    sx = sy = FX32_ONE
                    rot = px = py = 0
                frames.append((index, duration, sx, sy, rot, px, py))
            seqs.append({"mode": mode, "loop": loop, "frames": frames})
        return seqs
    die("no KNBA section in an animation member")


# --------------------------------------------------------------- composition

def frame_at(seq, t):
    """The frame a sequence shows at tick t from its start."""
    frames = seq["frames"]
    if not frames:
        return None
    if seq["mode"] in PLAY_REVERSE:
        frames = frames[::-1]
    total = sum(f[1] for f in frames)
    if total == 0:
        return frames[0]
    if t >= total:
        if seq["mode"] not in PLAY_LOOP:
            return frames[-1]
        loop_at = min(seq["loop"], len(frames) - 1)
        head = sum(f[1] for f in frames[:loop_at])
        span = total - head
        t = head + (t - head) % span if span > 0 else head
    for f in frames:
        if t < f[1]:
            return f
        t -= f[1]
    return frames[-1]


class Picture:
    """One composed frame: sparse pixels keyed by (x, y) relative to the
    cartridge's own origin, which sits at the feet."""

    def __init__(self):
        self.px = {}
        self.notes = []

    def bbox(self):
        if not self.px:
            return None
        xs = [x for x, _ in self.px]
        ys = [y for _, y in self.px]
        return min(xs), min(ys), max(xs) + 1, max(ys) + 1


def compose_cell(pic, charmap, oams, ox, oy, sx, sy, rot):
    """Draw one cell's OAMs at (ox, oy), through the node's own scale and turn
    about that point. Identity is the common case and is drawn exactly;
    anything else is drawn by inverse mapping every destination pixel so a
    scale above one leaves no holes."""
    cols, rows, width, pixels = charmap
    local = {}
    # The OBJ with the LOWER index is in front, so draw the list back to front
    # and let the earlier ones overwrite. Solosis is a body behind a gel: read
    # front to back it was the gel alone.
    for (x, y, w, h, char, hf, vf) in reversed(oams):
        tw, th = w // 8, h // 8
        for t in range(tw * th):
            tx, ty = t % tw, t // tw
            row, col = char // MAP_PITCH + ty, char % MAP_PITCH + tx
            if row >= rows or col >= cols:
                continue            # past the map: transparent, measured on Hydreigon
            dx = x + ((tw - 1 - tx) * 8 if hf else tx * 8)
            dy = y + ((th - 1 - ty) * 8 if vf else ty * 8)
            base = (row * 8) * width + col * 8
            for k in range(64):
                v = pixels[base + (k // 8) * width + (k % 8)]
                if not v:
                    continue
                px = dx + (7 - k % 8 if hf else k % 8)
                py = dy + (7 - k // 8 if vf else k // 8)
                local[(px, py)] = v
    if not local:
        return
    if sx == FX32_ONE and sy == FX32_ONE and rot == 0:
        for (x, y), v in local.items():
            pic.px[(ox + x, oy + y)] = v
        return
    pic.notes.append("node scaled %.2fx%.2f turned %.1f deg"
                     % (sx / FX32_ONE, sy / FX32_ONE, rot * 360.0 / 65536))
    fx, fy = sx / FX32_ONE, sy / FX32_ONE
    if fx == 0 or fy == 0:
        return
    theta = rot * 2 * math.pi / 65536
    c, s = math.cos(theta), math.sin(theta)
    xs = [x for x, _ in local]
    ys = [y for _, y in local]
    corners = [(x * fx, y * fy) for x in (min(xs), max(xs) + 1) for y in (min(ys), max(ys) + 1)]
    turned = [(x * c - y * s, x * s + y * c) for x, y in corners]
    x0, x1 = math.floor(min(t[0] for t in turned)), math.ceil(max(t[0] for t in turned))
    y0, y1 = math.floor(min(t[1] for t in turned)), math.ceil(max(t[1] for t in turned))
    for X in range(x0, x1 + 1):
        for Y in range(y0, y1 + 1):
            cx, cy = X + 0.5, Y + 0.5
            ux, uy = cx * c + cy * s, -cx * s + cy * c
            v = local.get((math.floor(ux / fx), math.floor(uy / fy)))
            if v:
                pic.px[(ox + X, oy + Y)] = v


def compose(charmap, cells, multicells, cell_anims, mc_anims, t):
    """The picture at tick t of the multi-cell animation's first sequence."""
    pic = Picture()
    if not mc_anims or not mc_anims[0]["frames"]:
        pic.notes.append("no multi-cell animation")
        return pic
    mc_seq = mc_anims[0]
    # Which multi-cell, and how long the previous ones ran: a node whose play
    # mode is RESET starts its own animation over when the multi-cell changes.
    elapsed, mc_frame = 0, mc_seq["frames"][0]
    for f in mc_seq["frames"]:
        if t < elapsed + f[1] or f is mc_seq["frames"][-1]:
            mc_frame = f
            break
        elapsed += f[1]
    index, _d, msx, msy, mrot, mpx, mpy = mc_frame
    if msx != FX32_ONE or msy != FX32_ONE or mrot:
        pic.notes.append("multi-cell frame carries a scale or turn; ignored")
    if index >= len(multicells):
        pic.notes.append("multi-cell animation names multi-cell %d of %d" % (index, len(multicells)))
        return pic
    # Nodes the same way round: node 0's OBJs are the first in the OAM table.
    for (seq_idx, nx, ny, attr) in reversed(multicells[index]):
        if not (attr >> 5) & 1:
            continue                                # node not visible
        if seq_idx >= len(cell_anims):
            pic.notes.append("node names sequence %d of %d" % (seq_idx, len(cell_anims)))
            continue
        local_t = t - elapsed if (attr & 0xF) == MC_NODE_RESET else t
        fr = frame_at(cell_anims[seq_idx], local_t)
        if fr is None:
            continue
        cell, _d, sx, sy, rot, px, py = fr
        if cell >= len(cells):
            pic.notes.append("sequence %d names cell %d of %d" % (seq_idx, cell, len(cells)))
            continue
        compose_cell(pic, charmap, cells[cell], nx + px + mpx, ny + py + mpy, sx, sy, rot)
    return pic


def keyframe_ticks(multicells, cell_anims, mc_anims):
    """Every tick of one period at which the picture can change: each multi-cell
    frame start, and inside each frame every node keyframe, a RESET node's from
    the frame start, a CONTINUE node's from the animation's own start."""
    if not mc_anims or not mc_anims[0]["frames"]:
        return [0], 1
    seq = mc_anims[0]["frames"]
    period = sum(f[1] for f in seq) or 1
    ticks = set()
    start = 0
    for (index, duration, *_rest) in seq:
        if duration == 0:
            continue
        ticks.add(start)
        if index < len(multicells):
            for (seq_idx, _x, _y, attr) in multicells[index]:
                if not (attr >> 5) & 1 or seq_idx >= len(cell_anims):
                    continue
                node = cell_anims[seq_idx]
                starts, at = [], 0
                for f in node["frames"]:
                    starts.append(at)
                    at += f[1]
                total = at or 1
                if (attr & 0xF) == MC_NODE_RESET:
                    base = 0
                    while base < duration:
                        for st in starts:
                            if base + st < duration:
                                ticks.add(start + base + st)
                        base += total
                else:
                    first = (start // total) * total
                    base = first
                    while base < start + duration:
                        for st in starts:
                            if start <= base + st < start + duration:
                                ticks.add(base + st)
                        base += total
        start += duration
    return sorted(t for t in ticks if t < period), period


def face_loop(block, half, female=False):
    """One period of a face as [(picture, ticks it holds)], at most MAX_FRAMES
    long, the first frame the resting picture."""
    charmap = read_charmap(block[half + (FEMALE_CHARMAP if female else CHARMAP)])
    cells = read_cells(block[half + CELLS])
    cell_anims = read_animations(block[half + CELL_ANIM])
    multicells = read_multicells(block[half + MULTICELLS])
    mc_anims = read_animations(block[half + MULTICELL_ANIM])
    ticks, period = keyframe_ticks(multicells, cell_anims, mc_anims)
    if len(ticks) > MAX_CANDIDATES:
        step = len(ticks) / MAX_CANDIDATES
        ticks = [ticks[0]] + sorted({ticks[int(i * step)] for i in range(1, MAX_CANDIDATES)})
    pictures = []
    for t in ticks:
        pic = compose(charmap, cells, multicells, cell_anims, mc_anims, t)
        key = tuple(sorted(pic.px.items()))
        if pictures and pictures[-1][2] == key:
            continue
        pictures.append([pic, t, key])
    if len(pictures) > MAX_FRAMES:
        keep = [pictures[0]]
        for k in range(1, MAX_FRAMES):
            target = period * k / MAX_FRAMES
            best = min((p for p in pictures[1:] if p not in keep), key=lambda p: abs(p[1] - target))
            keep.append(best)
        pictures = sorted(keep, key=lambda p: p[1])
    out = []
    for i, (pic, t, _key) in enumerate(pictures):
        nxt = pictures[i + 1][1] if i + 1 < len(pictures) else period
        out.append((pic, max(1, min(255, nxt - t))))
    return out


def has_female(block):
    return len(block[FEMALE_CHARMAP]) > 0


# ------------------------------------------------------------------- fitting

def palette_colours(member):
    return [struct.unpack_from("<H", member, 40 + i * 2)[0] & 0x7FFF for i in range(16)]


def rgb(c):
    return (c & 31, (c >> 5) & 31, (c >> 10) & 31)


def nearest(colours, r, g, b):
    best, at = None, 0
    for i in range(1, 16):
        cr, cg, cb = rgb(colours[i])
        d = (cr - r) ** 2 + (cg - g) ** 2 + (cb - b) ** 2
        if best is None or d < best:
            best, at = d, i
    return at


def scaled(pic, x0, y0, w, h, factor, colours):
    """Area-average a picture down by `factor`, then back onto its palette."""
    nw, nh = max(1, round(w * factor)), max(1, round(h * factor))
    out = {}
    inv = 1.0 / factor
    for Y in range(nh):
        for X in range(nw):
            sx0, sx1 = X * inv, (X + 1) * inv
            sy0, sy1 = Y * inv, (Y + 1) * inv
            cover = 0.0
            acc = [0.0, 0.0, 0.0]
            for yy in range(math.floor(sy0), math.ceil(sy1)):
                wy = min(sy1, yy + 1) - max(sy0, yy)
                for xx in range(math.floor(sx0), math.ceil(sx1)):
                    wx = min(sx1, xx + 1) - max(sx0, xx)
                    v = pic.px.get((x0 + xx, y0 + yy))
                    if not v:
                        continue
                    a = wx * wy
                    cover += a
                    cr, cg, cb = rgb(colours[v])
                    acc[0] += cr * a
                    acc[1] += cg * a
                    acc[2] += cb * a
            if cover >= 0.5 * inv * inv:
                out[(X, Y)] = nearest(colours, acc[0] / cover, acc[1] / cover, acc[2] / cover)
    return out, nw, nh


def enlarged(pic, x0, y0, w, h, factor):
    """Nearest-neighbour a picture up by `factor`: crisp, the way the hardware
    doubles a back sprite, never a blend."""
    nw, nh = max(1, round(w * factor)), max(1, round(h * factor))
    out = {}
    for Y in range(nh):
        sy = y0 + min(h - 1, int(Y / factor))
        for X in range(nw):
            v = pic.px.get((x0 + min(w - 1, int(X / factor)), sy))
            if v:
                out[(X, Y)] = v
    return out, nw, nh


def fit(frames, colours, magnify=1.0):
    """A face's frames onto 80x80 with one placement: returns ([80x80 rows per frame],
    y_offset, scale factor, notes).
    """
    boxes = [pic.bbox() for pic, _d in frames if pic.bbox()]
    if not boxes:
        return None
    x0 = min(b[0] for b in boxes)
    y0 = min(b[1] for b in boxes)
    x1 = max(b[2] for b in boxes)
    y1 = max(b[3] for b in boxes)
    w, h = x1 - x0, y1 - y0
    # The whole LOOP fits, and the byte is the room below the feet. The engine
    # seats a sprite by its height byte and the seat is byte-invariant: yCenter
    # is y + byte and the feet land at (frame bottom - byte), which is y + 40
    # whatever the byte is. So the byte is free to say how much of the frame
    # lies below the resting feet, and Black's loops need that room: 28.5% of
    # faces have a keyframe that dips under the resting pose (a swoop, a bob;
    # median 3 px, Pidgeotto 19), which a frame seated at the feet cuts off,
    # in play when the dip is deeper than the byte, and in every faint, whose
    # window ends exactly at the feet. Fitting only the resting pose and
    # cropping the travel drew Pidgeotto's flap cut flat at both edges.
    #
    # But the byte is not free, because only half the screens seat by it. A
    # battle adds it to the sprite's y and the feet land at y + 40 however big
    # it is; the summary, the box and the party draw the 80x80 frame at a fixed
    # y and take the sheet as it lies. The cartridge's art is small and centred
    # in its cell with the byte counting the blank rows beneath, so there both
    # readings agree, and seating a loop by its dip (0 for most faces, the loop
    # on the floor of the cell) put every one of those screens twenty pixels
    # low: one face at y 101..143 against the cartridge's 85..123, feet hanging
    # out of the frame.
    #
    # So the byte is the blank rows under a centred resting pose, at the scale
    # the cartridge's frame gives the loop, and never less than the dip, so a
    # keyframe that swoops below the pose keeps its room and nothing is
    # cropped. blackcompose.c's mmo_black_height_byte is this same arithmetic;
    # the import gate diffs the two.
    ref = boxes[0]
    rw, rh = ref[2] - ref[0], ref[3] - ref[1]
    dip, rise = y1 - ref[3], ref[1] - y0
    s_small = min(1.0, FRAME / w, FRAME / h)
    posed = min(FRAME, max(0, int(round(rh * s_small))))
    byte = max((FRAME - posed + 1) // 2, int(round(max(0, dip) * s_small)))
    byte = min(byte, FRAME - 1)
    factor = min(1.0, FRAME / w, (FRAME - byte) / (rh + rise)) if rh + rise else 1.0
    if byte and dip * factor > byte + 0.5:
        factor = byte / dip
    placed = []
    for pic, _d in frames:
        if factor == 1.0:
            placed.append(({(x - x0, y - y0): v for (x, y), v in pic.px.items()}, w, h))
        elif factor > 1.0:
            placed.append(enlarged(pic, x0, y0, w, h, factor))
        else:
            placed.append(scaled(pic, x0, y0, w, h, factor, colours))
    # The resting pose's bottom sits `byte` rows above the frame's bottom; the
    # loop is centred across. Every keyframe then lies inside the frame.
    rbot = int(round((ref[3] - y0) * factor))
    top = (FRAME - byte) - rbot
    left = (FRAME - int(round(w * factor))) // 2
    out = []
    notes = []
    for px, _fw, _fh in placed:
        rows = [bytearray(FRAME) for _ in range(FRAME)]
        for (x, y), v in px.items():
            X, Y = left + x, top + y
            if 0 <= X < FRAME and 0 <= Y < FRAME:
                rows[Y][X] = v
        out.append(rows)
    for pic, _d in frames:
        for n in pic.notes:
            if n not in notes:
                notes.append(n)
    # Front to back, the first word of a sheet is the seed and decodes to
    # background whatever it held: the engine could never show those four
    # pixels, and Game Freak's own sheets never use them.
    if any(out[0][0][:4]):
        out[0][0][0:4] = bytes(4)
        notes.append("top-left word cleared")
    return out, byte, factor, notes


def pairs_of(frames):
    """Frames laid two to a 160x80 pair, pairs stacked: rows of the whole sheet."""
    count = (len(frames) + 1) // 2
    rows = [bytearray(SHEET_W) for _ in range(SHEET_H * count)]
    for k, frame in enumerate(frames):
        left, top = (k % 2) * FRAME, (k // 2) * FRAME
        for y in range(FRAME):
            rows[top + y][left:left + FRAME] = frame[y]
    if len(frames) % 2:
        # An odd last frame repeats itself on the right, so the engine's own
        # frame flip shows the same picture rather than an empty one.
        last = len(frames) - 1
        for y in range(FRAME):
            rows[(last // 2) * FRAME + y][FRAME:] = frames[last][y]
    return rows


def sheet_bytes(rows):
    out = bytearray(len(rows) * SHEET_W // 2)
    for y, row in enumerate(rows):
        base = y * SHEET_W // 2
        for x in range(0, SHEET_W, 2):
            out[base + x // 2] = row[x] | (row[x + 1] << 4)
    return bytes(out)


def trailer(durations):
    """What follows the pixel data of a strip: the frame count and each frame's
    ticks, padded to a word. A plain two-frame sheet carries none."""
    if len(durations) <= 1:
        return b""
    body = STRIP_MAGIC + struct.pack("<H", len(durations)) + bytes(durations)
    return body + bytes((-len(body)) % 4)


def ncgr_header(pairs=1, extra=0):
    """Platinum's own sheet container: RGCN, one RAHC section, 10x20 tiles of
    4bpp stored linear, 6,400 bytes of data at +0x18, times the pairs a strip
    stacks, with `extra` trailer bytes counted into both sizes. check() holds the
    one-pair form to a member the cartridge ships."""
    data = SHEET_BYTES * pairs
    return (b"RGCN" + struct.pack("<HHIHH", 0xFEFF, 0x0100, NCGR_HEADER + data + extra, 0x10, 1)
            + b"RAHC" + struct.pack("<IHHIIIII", 0x20 + data + extra, SHEET_H // 8 * pairs, SHEET_W // 8,
                                    3, 0, 1, data, 0x18))


def ncgr_member(frames, durations, seed):
    rows = pairs_of(frames)
    tail = trailer(durations)
    return (ncgr_header(len(rows) // SHEET_H, len(tail))
            + rescramble.encode(sheet_bytes(rows), SCRAMBLE_MODE, seed) + tail)


def read_strip(member):
    """The reader the plugin mirrors: (frames as 80x80 rows, durations)."""
    data_size, data_off = struct.unpack_from("<II", member, 0x10 + 0x18)
    tiles_y = struct.unpack_from("<H", member, 0x10 + 8)[0]
    plain = rescramble.decode(member[0x10 + 8 + data_off:0x10 + 8 + data_off + data_size], SCRAMBLE_MODE)
    tail = member[0x10 + 8 + data_off + data_size:]
    if tail[:4] == STRIP_MAGIC:
        count = struct.unpack_from("<H", tail, 4)[0]
        durations = list(tail[6:6 + count])
    else:
        count, durations = 2, [0, 0]
    frames = []
    for k in range(count):
        left, top = (k % 2) * FRAME, (k // 2) * FRAME
        rows = []
        for y in range(FRAME):
            base = (top + y) * SHEET_W // 2
            row = bytearray(FRAME)
            for x in range(FRAME):
                b = plain[base + (left + x) // 2]
                row[x] = (b & 0xF) if (left + x) % 2 == 0 else (b >> 4)
            rows.append(row)
        frames.append(rows)
    assert tiles_y * 8 >= (count + 1) // 2 * FRAME
    return frames, durations


def blank_below(plain, frame):
    """Blank rows under a frame of a decoded sheet, the height byte's rule."""
    for row in range(FRAME - 1, -1, -1):
        base = row * SHEET_W // 2
        for x in range(frame * FRAME, frame * FRAME + FRAME):
            byte = plain[base + x // 2]
            if (byte & 0xF) if x % 2 == 0 else (byte >> 4):
                return FRAME - 1 - row
    return FRAME


# -------------------------------------------------------------------- checks

def check(black, plat_pokegra, plat_height, full=False):
    """Prove the writer on Platinum's own members, the strip through its own
    reader, and the composer over the faces. Returns (failures, lines)."""
    failures, lines = [], []
    if len(plat_pokegra) != POKEGRA_MEMBERS or len(plat_height) != HEIGHT_MEMBERS:
        failures.append("%s holds %d members and %s %d; this fill is built on %d and %d"
                        % (POKEGRA_NARC, len(plat_pokegra), HEIGHT_NARC, len(plat_height),
                           POKEGRA_MEMBERS, HEIGHT_MEMBERS))
        return failures, lines
    if plat_pokegra[3][:NCGR_HEADER] != ncgr_header():
        failures.append("the sheet container this writes is not the one Platinum ships")
    agree = 0
    for species in range(1, FIRST_PORTED):
        for k in (1, 3):
            member = plat_pokegra[species * POKEGRA_STRIDE + k]
            if len(member) != NCGR_HEADER + SHEET_BYTES:
                continue
            plain = rescramble.decode(member[NCGR_HEADER:], SCRAMBLE_MODE)
            seed = struct.unpack_from("<H", member, NCGR_HEADER)[0]
            if rescramble.encode(plain, SCRAMBLE_MODE, seed) != member[NCGR_HEADER:]:
                failures.append("species %d member %d does not re-encode to itself" % (species, k))
            byte = plat_height[species * HEIGHT_STRIDE + k]
            if not byte or byte[0] != blank_below(plain, 0):
                failures.append("species %d face %d: height byte %s, blank rows %d"
                                % (species, k, byte.hex() if byte else "-", blank_below(plain, 0)))
            else:
                agree += 1
    lines.append("sheets: %d of Platinum's own re-encode to themselves and seat on "
                 "their height byte" % agree)
    # A strip of Platinum's own Bulbasaur frames, three pairs deep, reads back
    # frame for frame with its durations through the reader the plugin mirrors.
    bulba, _d = read_strip(plat_pokegra[9])
    frames = [bulba[0], bulba[1], bulba[0], bulba[1], bulba[0]]
    back, durations = read_strip(ncgr_member(frames, [6, 54, 6, 96, 12], rescramble.SEED))
    if back != frames or durations != [6, 54, 6, 96, 12]:
        failures.append("a five-frame strip does not read back as it was written")
    lines.append("strips: a five-frame strip of Platinum's own frames reads back frame for frame")
    if len(black) < BLOCK * (LAST_PORTED + 1):
        failures.append("%s holds %d members; %d species need %d"
                        % (SPRITE_NARC_BLACK, len(black), LAST_PORTED + 1, BLOCK * (LAST_PORTED + 1)))
        return failures, lines
    for species in (1, FIRST_PORTED):
        blk = black[BLOCK * species:BLOCK * species + BLOCK]
        if blk[PAL_NORMAL][:40] != plat_pokegra[4][:40] or len(blk[PAL_NORMAL]) != PALETTE_BYTES:
            failures.append("species %d: Black's palette member is not Platinum's container" % species)
    sample = range(FIRST_SPECIES, LAST_PORTED + 1) if full else range(FIRST_SPECIES, LAST_PORTED + 1, 13)
    resolved, noted, frames_hist = 0, 0, Counter()
    for species in sample:
        if species in KEEP_PLATINUM:
            continue
        blk = black[BLOCK * species:BLOCK * species + BLOCK]
        for half in (FRONT, BACK):
            loop = face_loop(blk, half)
            if not loop or not loop[0][0].px:
                failures.append("species %d %s composes to nothing"
                                % (species, "front" if half == FRONT else "back"))
                continue
            bad = [n for pic, _d in loop for n in pic.notes
                   if n.startswith(("node names", "sequence", "multi-cell animation"))]
            if bad:
                failures.append("species %d: %s" % (species, "; ".join(bad[:2])))
            noted += any(pic.notes for pic, _d in loop)
            frames_hist[len(loop)] += 1
            resolved += 1
    lines.append("pictures: %d of %d sampled faces resolve through the animation banks, %d with a "
                 "scaled or turned node; frames a face %s"
                 % (resolved, 2 * len([s for s in sample if s not in KEEP_PLATINUM]), noted,
                    dict(sorted(frames_hist.items()))))
    return failures, lines


# ---------------------------------------------------------------------- fill

_BAKE = {}


def bake_species(species):
    """One species' members, computed in a worker: (species, writes, stats, previews)."""
    black, want_preview = _BAKE["black"], _BAKE["preview"]
    blk = black[BLOCK * species:BLOCK * species + BLOCK]
    colours = palette_colours(blk[PAL_NORMAL])
    engine = ENGINE_ID.get(species, species)
    writes, previews = [], {}
    stats = {"frames": [], "scaled": [], "bytes": 0}
    for half, face in ((BACK, 0), (FRONT, 2)):
        for gender, female in ((1, False), (0, True)):
            if female and not has_female(blk):
                continue
            loop = face_loop(blk, half, female)
            # A back at one and a half times, every species alike: at the
            # cartridge's size it drew very small beside the front, and fitted
            # to the frame every back was the same size and drew huge (owner,
            # 2026-08-31, both). BACK_MAGNIFY is the knob.
            fitted = fit(loop, colours, magnify=BACK_MAGNIFY if half == BACK else 1.0)
            if fitted is None:
                die("species %d composes to nothing; nothing written" % species)
            frames, y_offset, factor, _notes = fitted
            durations = [d for _pic, d in loop]
            if factor < 1.0 and not female:
                stats["scaled"].append((species, "front" if half == FRONT else "back", factor))
            if half == BACK and not female:
                stats.setdefault("magnified", []).append(factor)
            member = ncgr_member(frames, durations, (rescramble.SEED + species * 4 + face + gender) & 0xFFFF)
            stats["bytes"] += len(member)
            stats["frames"].append(len(frames))
            slots = [gender] if female else ([1] if has_female(blk) else [1, 0])
            for g in slots:
                writes.append((POKEGRA_NARC, engine * POKEGRA_STRIDE + face + g, member))
                writes.append((HEIGHT_NARC, engine * HEIGHT_STRIDE + face + g, bytes([y_offset])))
            if want_preview and not female:
                previews[(species, face)] = (frames, durations, colours)
    writes.append((POKEGRA_NARC, engine * POKEGRA_STRIDE + 4, bytes(blk[PAL_NORMAL])))
    writes.append((POKEGRA_NARC, engine * POKEGRA_STRIDE + 5, bytes(blk[PAL_SHINY])))
    for half, at in ((FRONT, 0), (BACK, 6)):
        female = blk[half + FEMALE_CHARMAP]
        for k, data in enumerate((lz11(blk[half + CHARMAP]), lz11(female) if female else b"",
                                  bytes(blk[half + CELLS]), lz11(blk[half + CELL_ANIM]),
                                  bytes(blk[half + MULTICELLS]), lz11(blk[half + MULTICELL_ANIM]))):
            writes.append((ANIM_NARC, engine * ANIM_STRIDE + at + k, data))
    return species, writes, stats, previews


def fill(pkg, black, write, preview=None, species_range=None, jobs=None):
    """Six pokegra members and four height bytes per species: Black's loop for
    every species this engine draws out of pl_pokegra, Platinum's own art left
    alone for the thirteen it does not, and hole-fill under the two egg ids.
    Species are baked in parallel; the parent alone writes."""
    todo = [sp for sp in (species_range or range(FIRST_SPECIES, LAST_PORTED + 1)) if sp not in KEEP_PLATINUM]
    _BAKE.update(black=black, preview=bool(preview))
    workers = max(1, min(jobs or max(1, (os.cpu_count() or 2) - 2), len(todo)))
    if workers == 1:
        results = map(bake_species, todo)
    else:
        pool = multiprocessing.get_context("fork").Pool(workers)
        results = pool.imap_unordered(bake_species, todo)
    scaled_list, sheets, frames_hist, total_bytes, magnified = [], {}, Counter(), 0, []
    for species, writes, stats, previews in results:
        for narc, index, data in writes:
            write(pkg, narc, index, data)
        scaled_list += stats["scaled"]
        magnified += stats.get("magnified", [])
        total_bytes += stats["bytes"]
        for n in stats["frames"]:
            frames_hist[n] += 1
        sheets.update(previews)
    if workers > 1:
        pool.close()
        pool.join()
    for egg in ENGINE_ID:
        for k in range(POKEGRA_STRIDE):
            write(pkg, POKEGRA_NARC, egg * POKEGRA_STRIDE + k, b"")
        for k in range(HEIGHT_STRIDE):
            write(pkg, HEIGHT_NARC, egg * HEIGHT_STRIDE + k, b"\0")
    # No hole in the animation archive either: the eggs, the kept species and
    # species 0 get empty members, which the plugin reads as "no loop here".
    for sp in list(ENGINE_ID) + sorted(KEEP_PLATINUM) + [0]:
        for k in range(ANIM_STRIDE):
            write(pkg, ANIM_NARC, sp * ANIM_STRIDE + k, b"")
    lines = ["sprites: %d faces baked as loops on %d workers, frames a face %s, %.1f MB of sheets; %d "
             "faces scaled to fit 80x80; %d species keep Platinum's art (forms, Spinda)"
             % (sum(frames_hist.values()), workers, dict(sorted(frames_hist.items())), total_bytes / 1e6,
                len(scaled_list), len(KEEP_PLATINUM))]
    if scaled_list:
        lines.append("  scaled: " + ", ".join("%d %s x%.2f" % x for x in sorted(scaled_list)))
    if magnified:
        lines.append("  backs magnified to fill the frame: mean x%.2f, %d of %d at the 2x cap"
                     % (sum(magnified) / len(magnified), sum(1 for f in magnified if f >= 2.0), len(magnified)))
    if preview:
        write_previews(Path(preview), sheets)
        lines.append("  previews in %s" % preview)
    return lines


# ------------------------------------------------------------------ previews

def png(path, width, height, rows, colours):
    """An 8-bit paletted PNG with index 0 transparent, enough for eyes."""
    plte = b"".join(bytes((r << 3 | r >> 2, g << 3 | g >> 2, b << 3 | b >> 2))
                    for r, g, b in (rgb(c) for c in colours))
    raw = b"".join(b"\0" + bytes(row) for row in rows)

    def chunk(kind, body):
        return (struct.pack(">I", len(body)) + kind + body
                + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF))
    path.write_bytes(b"\x89PNG\r\n\x1a\n"
                     + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 3, 0, 0, 0))
                     + chunk(b"PLTE", plte) + chunk(b"tRNS", b"\0")
                     + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def write_previews(where, sheets):
    """One PNG per face with its frames side by side, and a contact sheet of
    every face's first frame in the first species' colours (shape only)."""
    where.mkdir(parents=True, exist_ok=True)
    for face, name in ((2, "fronts"), (0, "backs")):
        keys = sorted(k for k in sheets if k[1] == face)
        cols = 13
        rows_n = (len(keys) + cols - 1) // cols
        grid = [bytearray(cols * FRAME) for _ in range(max(1, rows_n) * FRAME)]
        palette = None
        for n, key in enumerate(keys):
            frames, durations, colours = sheets[key]
            palette = palette or colours
            strip = [bytearray(FRAME * len(frames)) for _ in range(FRAME)]
            for k, frame in enumerate(frames):
                for y in range(FRAME):
                    strip[y][k * FRAME:(k + 1) * FRAME] = frame[y]
            png(where / ("%d_%s.png" % (key[0], "front" if face else "back")),
                FRAME * len(frames), FRAME, strip, colours)
            gx, gy = (n % cols) * FRAME, (n // cols) * FRAME
            for y in range(FRAME):
                grid[gy + y][gx:gx + FRAME] = frames[0][y]
        if palette:
            png(where / (name + ".png"), cols * FRAME, max(1, rows_n) * FRAME, grid, palette)


# ---------------------------------------------------------------------- main

def write_member(pkg, narc, index, data):
    d = pkg / "narc" / narc
    d.mkdir(parents=True, exist_ok=True)
    (d / str(index)).write_bytes(data)


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--rom", required=True, help="a Black or White NDS image")
    ap.add_argument("--engine", help="the pokeplatinum checkout")
    ap.add_argument("--host-rom", help="the built Platinum image")
    ap.add_argument("--package", help="the package to fill (default mmo/mods/imports)")
    ap.add_argument("--check", action="store_true", help="prove and resolve, write nothing")
    ap.add_argument("--preview", help="also write each sheet as a PNG under this directory")
    ap.add_argument("--species", help="only these, as A-B (a fill or a preview)")
    ap.add_argument("--full", action="store_true", help="--check every face rather than one species in thirteen")
    ap.add_argument("--jobs", type=int, help="workers for a fill (default: the cores less two)")
    args = ap.parse_args(argv)

    engine = porticons.engine_dir(args.engine)
    modport = porticons.load_modport(engine)
    rom = modport.NitroRom(Path(args.rom))
    host = Path(args.host_rom) if args.host_rom else engine / "build/rom/pokeplatinum.us.nds"
    if not host.is_file():
        die("no built Platinum image at %s; pass --host-rom" % host)
    plat = modport.NitroRom(host)
    print("portsprites: %s %s into %s %s" % (rom.code, rom.title, plat.code, plat.title))

    black = rom.narc_members(SPRITE_NARC_BLACK)
    failures, lines = check(black, plat.narc_members(POKEGRA_NARC), plat.narc_members(HEIGHT_NARC), args.full)
    for line in lines:
        print("  " + line)
    if failures:
        print("  %d disagreement(s):" % len(failures))
        for line in failures[:10]:
            print("    " + line)
        die("the sheet format or a picture does not hold; nothing written")
    print("  every check holds")
    if args.check:
        return 0
    pkg = Path(args.package) if args.package else MMO / "mods" / "imports"
    species_range = None
    if args.species:
        a, b = args.species.split("-")
        species_range = range(int(a), int(b) + 1)
    for line in fill(pkg, black, write_member, args.preview, species_range, args.jobs):
        print("  " + line)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
