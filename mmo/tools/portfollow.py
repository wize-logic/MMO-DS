#!/usr/bin/env python3
"""Fill a content package with the follower Pokemon out of a HeartGold image."""

from __future__ import annotations

import importlib.util
import shutil
import struct
import sys
import tempfile
from pathlib import Path

MMO = Path(__file__).resolve().parent.parent

sys.path.insert(0, str(MMO / "tools"))
import nsbtx                                                    # noqa: E402

# The generator owns the follower table and all three of its oracles. Importing
# it rather than re-reading mmo/FOLLOWERS is the point: a fill and the table's
# own gate then cannot disagree about what a follower is.
_spec = importlib.util.spec_from_file_location(
    "gen_followers", MMO / "tools" / "gen_followers.py")
gf = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(gf)

# Where an appended follower lands, and it is not `portmap.py`'s 276.
FOLLOWER_GFX_BASE = 1024

# The destination archive and what the built image already holds of it. An
# append hole is a boot error, so members run from here without a gap;
# portmap.py's PL_COUNT is where this number comes from and the two have to
# agree, which --dest-rom checks when one is in hand.
DST_MMODEL = "data/mmodel/mmodel.narc"
PL_MMODEL_COUNT = 470

# Past the engine's own frame sequences, which stop at BILLBOARD_FRAME_SEQ_
# VS_SEEKER = 22 and are sentinel-scanned, so an id above them finds nothing in
# the engine's table and reaches the port's hook instead.
SEQ_WALK = 64
SEQ_WALK_SHINY = 65

# enum BillboardModel, from the engine's own overlay005. Both already have rows
# in its model table, which is why neither needs planting: 32x32 is what a
# person is drawn on and 64x64 is what OBJ_EVENT_GFX_REGIGIGAS walks on.
BILLBOARD_MODEL_GENERIC_32x32 = 0
BILLBOARD_MODEL_GENERIC_64x64 = 5

COOK_FNV_OFFSET = 0xCBF29CE484222325


def die(msg: str) -> None:
    print("portfollow: " + msg, file=sys.stderr)
    raise SystemExit(1)


def shiny_sequence(blob: bytes) -> bytes:
    """The same walk, read through the second palette."""
    n = struct.unpack_from("<I", blob, 0)[0]
    need = 4 + 4 * n
    if len(blob) < need:
        die("the walk sequence is %d bytes and its own count says it needs %d"
            % (len(blob), need))
    head = blob[:4 + 3 * n]
    tail = blob[4 + 4 * n:]
    return head + bytes([1]) * n + tail


def timeline(blob: bytes) -> tuple[int, int]:
    """(facings, frames per facing), read off the sequence's own step list."""
    n = struct.unpack_from("<I", blob, 0)[0]
    starts = struct.unpack_from("<%dH" % n, blob, 4)
    tex = list(blob[4 + 2 * n:4 + 3 * n])

    groups = [tex[i:i + 4] for i in range(0, n, 4)]
    if n % 4 or not all(len(g) == 4 and g[0] == g[2] and g[1] == g[3]
                        and g[0] != g[1] for g in groups):
        die("the walk sequence is not four-step facings of a two-frame cycle "
            "(%d steps, textures %s)" % (n, tex))

    step = starts[1] - starts[0] if n > 1 else 1
    if any(starts[i + 1] - starts[i] != step for i in range(n - 1)):
        die("the walk sequence's steps are not evenly spaced (%s)"
            % (starts,))

    facings = len(groups)
    return facings, step * 4


def fill(rom: Path, pkg: Path, hg: Path, shiny: bool,
         dest_rom: Path | None) -> int:
    sys.path.insert(0, str(gf._engine_pc() or ""))
    try:
        from modport import NitroRom                            # noqa: E402
    except ImportError:
        die("no NitroRom in the engine port; this reads a cartridge with the "
            "engine's own reader and does not carry one of its own")

    rows, expansion, base, lo, hi, spare = gf.build(hg)
    image = NitroRom(rom)
    mmodel = image.narc_members(gf.SRC_MMODEL)
    tp = image.narc_members(gf.SRC_TP_PARAM)

    if len(tp) != len(expansion):
        die("%s holds %d records and the follower tables want %d"
            % (gf.SRC_TP_PARAM, len(tp), len(expansion)))

    first_member = PL_MMODEL_COUNT
    if dest_rom is not None:
        have = len(NitroRom(dest_rom).narc_members(DST_MMODEL))
        if have != PL_MMODEL_COUNT:
            print("portfollow: the destination image holds %d mmodel members, "
                  "not %d; appending from %d" % (have, PL_MMODEL_COUNT, have))
        first_member = have

    seq_member, seq_why = gf._walk_sequence(mmodel)
    if seq_member is None:
        die(seq_why)
    facings, frames = timeline(mmodel[seq_member])

    # Every member the fill will write, checked before any of it is staged.
    art = []
    for sid in sorted(expansion):
        member = gf._member_for(rows, expansion, sid)
        if member >= len(mmodel):
            die("sprite %d wants mmodel member %d and the image has %d"
                % (sid, member, len(mmodel)))
        size = gf._shape(nsbtx, mmodel[member])
        if isinstance(size, str):
            die("sprite %d (member %d) is not follower art: %s"
                % (sid, member, size))
        flag = tp[sid - lo][1]
        if bool(flag) != (size == 64):
            die("sprite %d (member %d): the art is %dx%d and %s byte 1 says %d"
                % (sid, member, size, size, gf.SRC_TP_PARAM, flag))
        art.append((sid, member, size))

    stage = Path(tempfile.mkdtemp(prefix="portfollow."))
    try:
        narc = stage / ".cooked/narc" / DST_MMODEL
        narc.mkdir(parents=True, exist_ok=True)
        gen = stage / ".cooked/generated"
        gen.mkdir(parents=True, exist_ok=True)

        gfx_rows = []
        at = first_member
        for i, (sid, member, size) in enumerate(art):
            (narc / str(at)).write_bytes(mmodel[member])
            model = (BILLBOARD_MODEL_GENERIC_64x64 if size == 64
                     else BILLBOARD_MODEL_GENERIC_32x32)
            gfx_rows.append((FOLLOWER_GFX_BASE + i, at, model, SEQ_WALK))
            if shiny:
                # The same member, read through the shiny sequence. A second
                # band rather than interleaved, so a client turns a normal id
                # into its shiny one by adding the count and nothing else.
                gfx_rows.append((FOLLOWER_GFX_BASE + len(art) + i, at, model,
                                 SEQ_WALK_SHINY))
            at += 1

        seq_rows = []
        walk_at = at
        (narc / str(walk_at)).write_bytes(mmodel[seq_member])
        seq_rows.append((SEQ_WALK, walk_at, facings, frames))
        at += 1
        if shiny:
            (narc / str(at)).write_bytes(shiny_sequence(mmodel[seq_member]))
            seq_rows.append((SEQ_WALK_SHINY, at, facings, frames))
            at += 1

        (gen / "billboard_gfx.txt").write_text(
            "".join("%d %d %d %d\n" % row for row in sorted(gfx_rows)))
        (gen / "billboard_seq.txt").write_text(
            "".join("%d %d %d %d\n" % row for row in seq_rows))
        (stage / ".cooked" / "digest").write_text(
            "v1 %016x\n" % COOK_FNV_OFFSET)

        toml = pkg / "mod.toml"
        if toml.is_file():
            (stage / "mod.toml").write_text(toml.read_text())
        else:
            (stage / "mod.toml").write_text(
                'id = "%s"\n'
                'name = "Follower Pokemon"\n'
                'version = "1.0.0"\n'
                'authors = ["openmmo"]\n'
                'requires = []\n'
                'load_after = []\n' % pkg.name)

        # Published in one move, so a refusal above leaves nothing behind.
        pkg.mkdir(parents=True, exist_ok=True)
        if (pkg / ".cooked").exists():
            shutil.rmtree(pkg / ".cooked")
        shutil.move(str(stage / ".cooked"), str(pkg / ".cooked"))
        if not toml.is_file():
            shutil.move(str(stage / "mod.toml"), str(toml))
    finally:
        shutil.rmtree(stage, ignore_errors=True)

    small = sum(1 for _, _, size in art if size == 32)
    large = len(art) - small
    written = at - first_member
    payload = sum(len(mmodel[m]) for _, m, _ in art)
    print("portfollow: %d follower sprites (%d small, %d large) from %s"
          % (len(art), small, large, image.code))
    print("portfollow: mmodel members %d..%d (%d), %.1f MB of art"
          % (first_member, at - 1, written, payload / 1e6))
    print("portfollow: gfx %d..%d%s, walk sequence %d = %d facings of %d frames"
          % (FOLLOWER_GFX_BASE, FOLLOWER_GFX_BASE + len(gfx_rows) - 1,
             " (the second half is the shiny coat)" if shiny else "",
             SEQ_WALK, facings, frames))
    print("portfollow: -> %s" % pkg)
    return 0


def main(argv: list[str]) -> int:
    rom = pkg = dest_rom = hg = None
    shiny = True

    i = 1
    while i < len(argv):
        a = argv[i]
        if a == "--rom" and i + 1 < len(argv):
            rom = Path(argv[i + 1]); i += 2
        elif a == "--pkg" and i + 1 < len(argv):
            pkg = Path(argv[i + 1]); i += 2
        elif a == "--dest-rom" and i + 1 < len(argv):
            dest_rom = Path(argv[i + 1]); i += 2
        elif a == "--heartgold" and i + 1 < len(argv):
            hg = Path(argv[i + 1]); i += 2
        elif a == "--no-shiny":
            shiny = False; i += 1
        else:
            die("unknown argument %r; see the header of this file" % a)

    if rom is None or pkg is None:
        die("usage: portfollow.py --rom <nds> --pkg <dir> [--no-shiny] "
            "[--dest-rom <nds>] [--heartgold <dir>]")
    if rom.is_dir():
        die("%s is a directory, a decompilation checkout is not a source. "
            "Content crosses from a cartridge image the player owns" % rom)
    if not rom.is_file():
        die("no cartridge at %s" % rom)
    if dest_rom is not None and not dest_rom.is_file():
        die("no destination image at %s" % dest_rom)

    if hg is None:
        hg = gf.decomp_dir("pokeheartgold")
    if hg is None or not hg.is_dir():
        die("need a heartgold checkout for the follower table; pass "
            "--heartgold or set DECOMP_DIR")

    return fill(rom, pkg, hg, shiny, dest_rom)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
