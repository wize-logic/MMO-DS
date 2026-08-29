#!/usr/bin/env python3
"""Read and merge NSBTX texture sets, so a ported map's props are textured."""

from __future__ import annotations

import struct
import sys
from pathlib import Path

ALL_ZEROES = 0xFF
NO_DIFF = -1
NAME_LEN = 16
NAME_BITS = 128
DICT_HEAD = 8
TEX_UNIT = 8            # u32 texImageParam, u32 extraParam
PAL_UNIT = 4            # u16 offset >> 3, u16 flag
DATA_ALIGN = 8          # every offset in a TEX0 is stored >> 3

# Bytes per pixel numerator/denominator, by the format in texImageParam bits
# 26..28. Format 5 is the 4x4-compressed pair of blocks and is refused: it needs
# a second data section this does not carry, and no prop model in either game's
# archive uses one.
FORMAT_BITS = {1: 8, 2: 2, 3: 4, 4: 8, 6: 8, 7: 16}
FORMAT_COMPRESSED = 5


def die(msg: str) -> None:
    print("nsbtx: " + msg, file=sys.stderr)
    raise SystemExit(2)


# ------------------------------------------------------------------ reading


def find_tex0(blob: bytes) -> int | None:
    """The TEX0 block of an NSBTX or an NSBMD, both of which may carry one."""
    if len(blob) < 0x10 or blob[:4] not in (b"BTX0", b"BMD0"):
        return None
    for i in range(struct.unpack_from("<H", blob, 0x0E)[0]):
        off = struct.unpack_from("<I", blob, 0x10 + 4 * i)[0]
        if blob[off:off + 4] == b"TEX0":
            return off
    return None


def _dict(blob: bytes, base: int) -> tuple[int, int, list[str], int]:
    """(numEntry, offset of the entry data, names, sizeUnit) of one dictionary."""
    num = blob[base + 1]
    data = base + struct.unpack_from("<H", blob, base + 0x06)[0]
    size_unit, ofs_name = struct.unpack_from("<HH", blob, data)
    names = [blob[data + ofs_name + NAME_LEN * i:
                  data + ofs_name + NAME_LEN * (i + 1)].split(b"\0")[0]
             .decode("latin1") for i in range(num)]
    return num, data + 4, names, size_unit


def read(blob: bytes) -> dict:
    """One TEX0 as {textures: {name: (param, extra, data)}, palettes: {...}}."""
    t = find_tex0(blob)
    if t is None:
        return dict(textures={}, palettes={})
    tex_size = struct.unpack_from("<H", blob, t + 0x0C)[0] << 3
    tex_dict = t + struct.unpack_from("<H", blob, t + 0x0E)[0]
    tex_data = t + struct.unpack_from("<I", blob, t + 0x14)[0]
    comp = struct.unpack_from("<H", blob, t + 0x1C)[0] << 3
    pal_size = struct.unpack_from("<H", blob, t + 0x30)[0] << 3
    pal_dict = t + struct.unpack_from("<I", blob, t + 0x34)[0]
    pal_data = t + struct.unpack_from("<I", blob, t + 0x38)[0]
    if comp:
        die("this set has %d bytes of 4x4-compressed texture, which needs a "
            "second data section this does not carry" % comp)

    n, at, names, unit = _dict(blob, tex_dict)
    textures = {}
    for i, name in enumerate(names):
        param, extra = struct.unpack_from("<II", blob, at + i * unit)
        off = (param & 0xFFFF) << 3
        w = 8 << ((param >> 20) & 7)
        h = 8 << ((param >> 23) & 7)
        fmt = (param >> 26) & 7
        if fmt == FORMAT_COMPRESSED:
            die("texture %r is 4x4-compressed" % name)
        if fmt not in FORMAT_BITS:
            die("texture %r has format %d, which has no known size" % (name, fmt))
        ln = w * h * FORMAT_BITS[fmt] // 8
        if off + ln > tex_size:
            die("texture %r runs %d bytes past the texture data" % (name, off + ln - tex_size))
        textures[name] = (param & ~0xFFFF, extra, blob[tex_data + off:tex_data + off + ln])

    n, at, names, unit = _dict(blob, pal_dict)
    # A palette carries no length, so it runs to the next one that starts after
    # it, and the last runs to the end of the palette block, whose size is
    # named. Two names may share an offset; the sorted set of starts is what
    # bounds them.
    starts = sorted({(struct.unpack_from("<H", blob, at + i * unit)[0] << 3)
                     for i in range(n)} | {pal_size})
    palettes = {}
    for i, name in enumerate(names):
        off, flag = struct.unpack_from("<HH", blob, at + i * unit)
        off <<= 3
        end = starts[starts.index(off) + 1]
        palettes[name] = (blob[pal_data + off:pal_data + end], flag)
    return dict(textures=textures, palettes=palettes)


def model_texture_names(blob: bytes) -> set[str]:
    """The texture names an NSBMD's materials ask to be BOUND, which is not the same question
    as what its own TEX0 holds.
    """
    n = struct.unpack_from("<H", blob, 0x0E)[0] if len(blob) >= 0x10 else 0
    mdl = None
    for i in range(n):
        off = struct.unpack_from("<I", blob, 0x10 + 4 * i)[0]
        if blob[off:off + 4] == b"MDL0":
            mdl = off
    if mdl is None:
        return set()
    d = mdl + 8 + struct.unpack_from("<H", blob, mdl + 8 + 0x06)[0]
    model = mdl + struct.unpack_from("<I", blob, d + 4)[0]
    mat = model + struct.unpack_from("<I", blob, model + 8)[0]
    ofs_tex = struct.unpack_from("<H", blob, mat)[0]
    base = mat + ofs_tex
    num = blob[base + 1]
    data = base + struct.unpack_from("<H", blob, base + 0x06)[0]
    _unit, ofs_name = struct.unpack_from("<HH", blob, data)
    at = data + ofs_name
    return {blob[at + NAME_LEN * i:at + NAME_LEN * (i + 1)].split(b"\0")[0]
            .decode("latin1") for i in range(num)}


# ------------------------------------------------- the name dictionary's tree


def _bit(name: bytes, i: int) -> int:
    return 1 if name[i // 8] & (1 << (i % 8)) else 0


def _diff_bit(a: int, b: int, start: int, stop: int, names: list[bytes]) -> int:
    if a == ALL_ZEROES:
        if b == ALL_ZEROES:
            return NO_DIFF
        a, b = b, a
    for i in range(start, stop - 1, -1):
        if b == ALL_ZEROES:
            if _bit(names[a], i):
                return i
        elif _bit(names[a], i) != _bit(names[b], i):
            return i
    return NO_DIFF


class _Node:
    __slots__ = ("leaf", "index", "bit", "entry", "left", "right")

    def __init__(self, leaf, bit, entry, left=None, right=None):
        self.leaf, self.bit, self.entry = leaf, bit, entry
        self.left, self.right, self.index = left, right, 0


def build_tree(names: list[bytes]) -> bytes:
    """The patricia tree a dictionary is looked up through."""
    holder = [_Node(True, 0, ALL_ZEROES)]

    def insert(idx: int) -> None:
        """The C's `Tree_Insert`, with `where` standing in for its `Node **`."""
        where = holder
        bit_idx = NAME_BITS - 1
        while True:
            node = where[0]
            if node.leaf:
                d = _diff_bit(idx, node.entry, bit_idx, 0, names)
                if d == NO_DIFF:
                    die("two entries share the name %r" % names[idx])
            else:
                d = _diff_bit(idx, node.entry, bit_idx, node.bit + 1, names)
                if d == NO_DIFF:
                    bit_idx = node.bit
                    where = _Side(node, "right" if _bit(names[idx], node.bit)
                                  else "left")
                    continue
            leaf = _Node(True, 0, idx)
            where[0] = (_Node(False, d, idx, node, leaf) if _bit(names[idx], d)
                        else _Node(False, d, idx, leaf, node))
            return

    for i in range(len(names)):
        insert(i)
    root = holder[0]

    order, stack = [], [root]
    entry_to_node = {}
    while stack:
        node = stack.pop()
        order.append(node)
        node.index = len(order)
        entry_to_node[node.entry] = node.index
        if not node.right.leaf:
            stack.append(node.right)
        if not node.left.leaf:
            stack.append(node.left)

    out = bytearray(struct.pack("<BBBB", NAME_BITS - 1, 1, 0, 0))
    for node in order:
        def child(c):
            if not c.leaf:
                return c.index
            return 0 if c.entry == ALL_ZEROES else entry_to_node[c.entry]
        out += struct.pack("<BBBB", node.bit, child(node.left), child(node.right),
                           node.entry)
    return bytes(out)


class _Side:
    """A settable reference to one child of a node, so insertion can replace it."""

    __slots__ = ("node", "side")

    def __init__(self, node, side):
        self.node, self.side = node, side

    def __getitem__(self, _):
        return getattr(self.node, self.side)

    def __setitem__(self, _, value):
        setattr(self.node, self.side, value)


def lookup(names: list[bytes], tree: bytes, want: bytes) -> int:
    """A name looked up the way the GAME looks it up, from the bytes written."""
    if len(names) < 16:
        return names.index(want) if want in names else -1
    node = 0

    def nd(i):
        return tree[i * 4], tree[i * 4 + 1], tree[i * 4 + 2], tree[i * 4 + 3]

    bit, left, right, entry = nd(0)
    if left == 0:
        return -1
    node = left
    while True:
        bit, left, right, entry = nd(node)
        nxt = right if _bit(want, bit) else left
        if nxt <= node:
            _, _, _, e = nd(nxt)
            return e if names[e] == want else -1
        node = nxt


# ------------------------------------------------------------------ writing


def _dict_block(names: list[str], entries: list[bytes], unit: int) -> bytes:
    raw = [n.encode("latin1")[:NAME_LEN].ljust(NAME_LEN, b"\0") for n in names]
    tree = build_tree(raw)
    for i, n in enumerate(raw):
        if lookup(raw, tree, n) != i:
            die("the dictionary this built answers %d for %r and not %d, the "
                "tree is wrong and the set is not written"
                % (lookup(raw, tree, n), names[i], i))
    ofs_data = DICT_HEAD + len(tree)
    ofs_name = 4 + len(names) * unit
    size = ofs_data + ofs_name + len(names) * NAME_LEN
    out = bytearray(struct.pack("<BBHHH", 0, len(names), size, 8, ofs_data))
    out += tree
    out += struct.pack("<HH", unit, ofs_name)
    for e in entries:
        out += e
    for n in raw:
        out += n
    return bytes(out)


def merge(blobs: list[bytes]) -> bytes:
    """One NSBTX holding every texture and palette the inputs name."""
    textures, palettes = {}, {}
    for b in blobs:
        got = read(b)
        for k, v in got["textures"].items():
            textures.setdefault(k, v)
        for k, v in got["palettes"].items():
            palettes.setdefault(k, v)
    if not textures:
        die("nothing to merge: none of the inputs carries a TEX0")

    tex_names = sorted(textures)
    pal_names = sorted(palettes)

    tex_blob, tex_entries = bytearray(), []
    for n in tex_names:
        param, extra, data = textures[n]
        tex_blob += b"\0" * ((-len(tex_blob)) % DATA_ALIGN)
        tex_entries.append(struct.pack("<II", param | (len(tex_blob) >> 3), extra))
        tex_blob += data
    tex_blob += b"\0" * ((-len(tex_blob)) % DATA_ALIGN)

    pal_blob, pal_entries = bytearray(), []
    for n in pal_names:
        data, flag = palettes[n]
        pal_blob += b"\0" * ((-len(pal_blob)) % DATA_ALIGN)
        pal_entries.append(struct.pack("<HH", len(pal_blob) >> 3, flag))
        pal_blob += data
    pal_blob += b"\0" * ((-len(pal_blob)) % DATA_ALIGN)

    tex_dict = _dict_block(tex_names, tex_entries, TEX_UNIT)
    pal_dict = _dict_block(pal_names, pal_entries, PAL_UNIT)

    head = 0x3C
    ofs_tex_dict = head
    ofs_pal_dict = ofs_tex_dict + len(tex_dict)
    ofs_tex_data = ofs_pal_dict + len(pal_dict)
    ofs_tex_data += (-ofs_tex_data) % DATA_ALIGN
    ofs_pal_data = ofs_tex_data + len(tex_blob)
    size = ofs_pal_data + len(pal_blob)
    for name, v in (("texture data", ofs_tex_data), ("palette data", ofs_pal_data)):
        if v % DATA_ALIGN:
            die("%s would land at %d, which a TEX0 cannot address" % (name, v))
    if (len(tex_blob) >> 3) > 0xFFFF or (len(pal_blob) >> 3) > 0xFFFF:
        die("the merged set is larger than a TEX0's size fields can name")

    tex = bytearray(b"\0" * head)
    tex[0:4] = b"TEX0"
    struct.pack_into("<I", tex, 0x04, size)
    struct.pack_into("<H", tex, 0x0C, len(tex_blob) >> 3)
    struct.pack_into("<H", tex, 0x0E, ofs_tex_dict)
    struct.pack_into("<I", tex, 0x14, ofs_tex_data)
    struct.pack_into("<H", tex, 0x1C, 0)                 # no 4x4-compressed set
    struct.pack_into("<H", tex, 0x1E, ofs_tex_dict)
    struct.pack_into("<I", tex, 0x24, ofs_pal_data)
    struct.pack_into("<I", tex, 0x28, ofs_pal_data)
    struct.pack_into("<H", tex, 0x30, len(pal_blob) >> 3)
    struct.pack_into("<H", tex, 0x32, 0x8000)
    struct.pack_into("<I", tex, 0x34, ofs_pal_dict)
    struct.pack_into("<I", tex, 0x38, ofs_pal_data)
    tex += tex_dict + pal_dict
    tex += b"\0" * (ofs_tex_data - len(tex))
    tex += tex_blob + pal_blob

    return (b"BTX0" + struct.pack("<IIHH", 0x0100FEFF, 0x14 + len(tex), 0x10, 1)
            + struct.pack("<I", 0x14) + bytes(tex))


def main(argv: list[str]) -> int:
    if len(argv) < 3:
        die("usage: nsbtx.py <out.nsbtx> <in.nsbmd|in.nsbtx>...")
    out = Path(argv[1])
    blobs = [Path(p).read_bytes() for p in argv[2:]]
    data = merge(blobs)
    out.write_bytes(data)
    got = read(data)
    print("nsbtx: %d file(s) -> %d textures, %d palettes, %d bytes"
          % (len(blobs), len(got["textures"]), len(got["palettes"]), len(data)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
