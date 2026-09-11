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

# Where an appended follower lands, and it is NOT `portmap.py`'s 276.
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

# The TALK'S five sources, and what each one is checked against. Every count
# here was read out of a cartridge rather than a header, and a cartridge that
# disagrees with any of them writes nothing at all, the same rule the art
# already follows, for the same reason: a package half full of the right bytes
# loads and looks like it worked.
SRC_TALK_COND = "a/2/2/0"      # 236 members: 0 is the world's rows, 1..235 one per map section
SRC_TALK_REACT = "a/2/2/1"     # 1023 members of 52 bytes: five steps and a tail
SRC_TALK_ANIM = "a/2/2/2"      # 108 members of 80 bytes: ten frames of eight
SRC_TALK_SPECIES = "a/2/3/1"   # one member, a category byte per species
SRC_TALK_MSG = "a/0/2/7"       # the message archive; member 265 is the follower's lines
HG_TALK_MSG_BANK = 265

# The EMOTE, and every one of these numbers is read out of the game rather
# than guessed.
SRC_EMOTE = "a/1/0/3"          # HeartGold's field-effect archive, 169 members
HG_EMOTE_MODEL = 130           # the quad the bubble is drawn on (BMD0)
HG_EMOTE_TEX_FIRST = 2         # fourteen NSBTX members, one per bubble
HG_EMOTE_SEQ = 150             # the first of fourteen identical sequences
HG_EMOTE_COUNT = 14
HG_EMOTE_TEXTURES_PER = 2      # every bubble is a two-frame flicker

# The ball the follower is recalled into and comes out of, read out of the
# effect that draws it (`ov01_0220329C`, asm/overlay_01_022031C0.s): the ball
# is member 129 (a BMD0), the flash member 104 (a BMD0) and the flash's
# animation member 164 (a BTA0). They go in the same archive as the bubble,
# right after its fourteen sheets.
HG_BALL_MODEL = 129
HG_FLASH_MODEL = 104
HG_FLASH_ANIM = 164
HG_BALL_MEMBERS = ((HG_BALL_MODEL, b"BMD0"), (HG_FLASH_MODEL, b"BMD0"),
                   (HG_FLASH_ANIM, b"BTA0"))

# Where they land. `data/mmodel/fldeff.narc` is the archive the field effect
# manager opens (`FieldEffectManager.OpenArchive`), it holds 201 members in the
# built image, and nothing else this repo ships appends to it, so a follower
# package may take the end of it without agreeing a cursor with anybody.
DST_EMOTE = "data/mmodel/fldeff.narc"
PL_FLDEFF_COUNT = 201

TALK_ROW = 20                  # one condition row
TALK_WORLD_ROWS = 70           # member 0
TALK_MAP_ROWS = 30             # every other member
TALK_MAP_SECTIONS = 235        # and mmo/MAPS' label bank holds the same 235
TALK_REACT_BYTES = 52
TALK_REACT_STEPS = 5
TALK_STEP_BYTES = 8
TALK_ANIM_BYTES = 80
TALK_ANIM_FRAMES = 10
TALK_ANIM_FRAME_BYTES = 8
TALK_SPECIES_MIN = 493         # HeartGold's own species count; the member is padded past it
TALK_EMOTE_MAX = 14            # the interpreter's own ceiling on an emote id

# The only three sounds a step ever names, measured across all 1,023 reactions:
# silence, and the two ways HeartGold asks for the follower's own cry. No sound
# Effect crosses, which is why no sound id has to be translated, if one ever
# did, this is the refusal that would say so.
HG_SEQ_SE_END = 2378
TALK_SOUND_NONE = 0
TALK_SOUND_CRY = HG_SEQ_SE_END + 1
TALK_SOUND_CRY_ALT = HG_SEQ_SE_END + 2

# Where the lines land, and WHY NOT in the message archive.

# The talk's own archive.
DST_TALK = "openmmo/follow_talk.narc"
TALK_MAGIC = 0x3154464F        # 'OFT1'
TALK_VERSION = 5
TALK_HEADER_BYTES = 56
TALK_HEADER_BASE = 594          # portmap.FIRST_FREE_HEADER, and the same rule


def talk_header(cond, react, anim, species, mapsec, msg, tp, mode,
                msg_strings, emote_model, emote_seq, emote_tex,
                emote_count, ball_model, flash_model, flash_anim) -> bytes:
    """Member 0: the magic, and where each section starts and how long it is."""
    base = 1
    parts = []
    for count in (cond, react, anim, species, mapsec, msg, tp, mode):
        parts.append((base, count))
        base += count
    return struct.pack("<IHHH" + "HH" * 8 + "HHHH" + "HHH",
                       TALK_MAGIC, TALK_VERSION, TALK_HEADER_BASE,
                       msg_strings,
                       parts[0][0], parts[0][1],
                       parts[1][0], parts[1][1],
                       parts[2][0], parts[2][1],
                       parts[3][0], parts[3][1],
                       parts[4][0], parts[4][1],
                       parts[5][0], parts[5][1],
                       parts[6][0], parts[6][1],
                       parts[7][0], parts[7][1],
                       emote_model, emote_seq, emote_tex, emote_count,
                       ball_model, flash_model, flash_anim)


def emote_sources(image, dest_rom, after=None) -> tuple[bytes, bytes, list[bytes], int, list[bytes]]:
    """The bubble's model, its sequence and its fourteen sheets, all checked; and the ball's
    three members behind them.
    """
    ms = image.narc_members(SRC_EMOTE)
    need = max(HG_EMOTE_MODEL, HG_EMOTE_SEQ + HG_EMOTE_COUNT,
               HG_EMOTE_TEX_FIRST + HG_EMOTE_COUNT,
               *(m + 1 for m, _ in HG_BALL_MEMBERS))
    if len(ms) < need:
        die("%s holds %d members and the emote wants at least %d"
            % (SRC_EMOTE, len(ms), need))

    model = ms[HG_EMOTE_MODEL]
    if model[:4] != b"BMD0":
        die("%s member %d is %r, not the BMD0 the bubble is drawn on"
            % (SRC_EMOTE, HG_EMOTE_MODEL, model[:4]))

    seq = ms[HG_EMOTE_SEQ]
    n = struct.unpack_from("<I", seq, 0)[0] if len(seq) >= 4 else 0
    if len(seq) != 4 + 4 * n or n != 4:
        die("%s member %d is not a four-step frame sequence (%d steps, %d"
            " bytes)" % (SRC_EMOTE, HG_EMOTE_SEQ, n, len(seq)))
    tex_run = list(seq[4 + 2 * n:4 + 3 * n])
    if tex_run != [0, 1, 0, 1]:
        die("the bubble's sequence flickers between textures %s and the sheets"
            " carry two, the two have to agree" % (tex_run,))
    for i in range(1, HG_EMOTE_COUNT):
        if ms[HG_EMOTE_SEQ + i] != seq:
            die("%s member %d is not the same sequence as member %d; the fill"
                " carries one because the game's fourteen are identical"
                % (SRC_EMOTE, HG_EMOTE_SEQ + i, HG_EMOTE_SEQ))

    sheets = []
    for i in range(HG_EMOTE_COUNT):
        blob = ms[HG_EMOTE_TEX_FIRST + i]
        if blob[:4] != b"BTX0":
            die("%s member %d is %r, not a texture"
                % (SRC_EMOTE, HG_EMOTE_TEX_FIRST + i, blob[:4]))
        got = nsbtx.read(blob)
        if len(got["textures"]) != HG_EMOTE_TEXTURES_PER:
            die("emote %d (%s member %d) holds %d texture(s) and its sequence"
                " asks for %d" % (i, SRC_EMOTE, HG_EMOTE_TEX_FIRST + i,
                                  len(got["textures"]), HG_EMOTE_TEXTURES_PER))
        if len(got["palettes"]) != 1:
            die("emote %d holds %d palettes and the sequence selects one"
                % (i, len(got["palettes"])))
        sheets.append(blob)

    ball = []
    for member, magic in HG_BALL_MEMBERS:
        blob = ms[member]
        if blob[:4] != magic:
            die("%s member %d is %r, not the %s the ball effect draws with"
                % (SRC_EMOTE, member, blob[:4], magic.decode()))
        ball.append(blob)

    first = PL_FLDEFF_COUNT
    if dest_rom is not None:
        have = len(NitroRom(dest_rom).narc_members(DST_EMOTE))
        if have != PL_FLDEFF_COUNT:
            print("portfollow: the destination image holds %d %s members, not"
                  " %d; appending from %d"
                  % (have, DST_EMOTE, PL_FLDEFF_COUNT, have))
        first = have
    if after:
        first = base_after(after, DST_EMOTE, first)
    return model, seq, sheets, first, ball


# Where A FOLLOWER may WALK, in the SOURCE game'S own words.
FOLLOW_MODE_PREVENT = 0
FOLLOW_MODE_HEIGHT_RESTRICT = 1
FOLLOW_MODE_ALLOW = 2
FOLLOW_MODE_MASK = 3
FOLLOW_NO_DIGLETT = 4          # bit 2, ours: the Bell Tower's eleven maps
FOLLOW_MODE_NONE = 0xFF        # a header with no answer; the client keeps its own


def talk_followmodes(hg: Path, headers: int) -> bytes:
    """One byte a source header: its follow mode, and whether Diglett is out."""
    import re

    ids = {}
    for m in re.finditer(r"#define\s+MAP_(\w+)\s+(\d+)",
                         (hg / "include/constants/maps.h").read_text()):
        ids.setdefault(m.group(1), int(m.group(2)))

    # The eleven the source refuses a Diglett on, by name rather than by number.
    bell = {n for n in ids if n.startswith("BELL_TOWER")}
    if not bell:
        die("no BELL_TOWER map in %s, the Diglett rule has nothing to name"
            % hg)

    text = (hg / "src/data/map_headers.h").read_text()
    out = {}
    for m in re.finditer(r"\[MAP_(\w+)\]\s*=\s*\{(.*?)\n\s*\}", text, re.S):
        name, body = m.group(1), m.group(2)
        if name not in ids:
            continue
        mode = re.search(r"\.followMode\s*=\s*MAP_FOLLOWMODE_(\w+)", body)
        if mode is None:
            continue
        word = mode.group(1)
        if word == "PREVENT":
            v = FOLLOW_MODE_PREVENT
        elif word == "HEIGHT_RESTRICT":
            v = FOLLOW_MODE_HEIGHT_RESTRICT
        elif word == "ALLOW":
            v = FOLLOW_MODE_ALLOW
        else:
            die("map %s names follow mode %s, which is not one of the three"
                % (name, word))
        if name in bell:
            v |= FOLLOW_NO_DIGLETT
        out[ids[name]] = v
    if not out:
        die("no map header in %s names a follow mode, this is not a"
            " HeartGold checkout" % hg)

    n = max(max(out), headers - 1) + 1
    return bytes(out.get(i, FOLLOW_MODE_NONE) for i in range(n))


def talk_mapsecs(hg: Path, sections: int) -> bytes:
    """One byte per HeartGold map header: the section it belongs to."""
    import re

    ids = {}
    for m in re.finditer(r"#define\s+MAP_(\w+)\s+(\d+)",
                         (hg / "include/constants/maps.h").read_text()):
        ids.setdefault(m.group(1), int(m.group(2)))
    secs = {}
    for m in re.finditer(r"#define\s+MAPSEC_(\w+)\s+(\d+)",
                         (hg / "include/constants/map_sections.h").read_text()):
        secs.setdefault(m.group(1), int(m.group(2)))

    text = (hg / "src/data/map_headers.h").read_text()
    out = {}
    for m in re.finditer(r"\[MAP_(\w+)\]\s*=\s*\{(.*?)\n\s*\}", text, re.S):
        name, body = m.group(1), m.group(2)
        if name not in ids:
            continue
        mapsec = re.search(r"\.mapsec\s*=\s*MAPSEC_(\w+)", body)
        sec = secs.get(mapsec.group(1)) if mapsec else None
        if sec is None:
            continue
        if sec >= sections:
            die("map %s names section %d and the condition table holds %d"
                % (name, sec, sections))
        out[ids[name]] = sec
    if not out:
        die("no map header in %s names a section, this is not a HeartGold"
            " checkout" % hg)
    n = max(out) + 1
    return bytes(out.get(i, 0xFF) for i in range(n))


def decode_bank(d: bytes) -> list[list[int]]:
    """A message bank as lists of charcodes, portmap.decode_bank's twin."""
    n, seed = struct.unpack_from("<HH", d, 0)
    out = []
    for i in range(n):
        o, ln = struct.unpack_from("<II", d, 4 + i * 8)
        k = (seed * 765 * (i + 1)) & 0xFFFF
        kk = k | (k << 16)
        o, ln = o ^ kk, ln ^ kk
        if o + ln * 2 > len(d):
            die("the follower's message bank does not hold its own line %d" % i)
        out.append([0] * ln)
    return out


def talk_sources(image, msg_banks) -> tuple[list, list, list, list, bytes, int]:
    """The five tables, each checked against what its own siblings say."""
    cond = image.narc_members(SRC_TALK_COND)
    react = image.narc_members(SRC_TALK_REACT)
    anim = image.narc_members(SRC_TALK_ANIM)
    species = image.narc_members(SRC_TALK_SPECIES)

    if len(cond) != TALK_MAP_SECTIONS + 1:
        die("%s holds %d members and the talk wants one world table and %d map"
            " sections" % (SRC_TALK_COND, len(cond), TALK_MAP_SECTIONS))
    if len(cond[0]) != TALK_WORLD_ROWS * TALK_ROW:
        die("%s member 0 is %d bytes, not %d rows of %d"
            % (SRC_TALK_COND, len(cond[0]), TALK_WORLD_ROWS, TALK_ROW))
    for i, m in enumerate(cond[1:], 1):
        if len(m) != TALK_MAP_ROWS * TALK_ROW:
            die("%s member %d is %d bytes, not %d rows of %d"
                % (SRC_TALK_COND, i, len(m), TALK_MAP_ROWS, TALK_ROW))
    for i, m in enumerate(react):
        if len(m) != TALK_REACT_BYTES:
            die("%s member %d is %d bytes, not %d"
                % (SRC_TALK_REACT, i, len(m), TALK_REACT_BYTES))
    for i, m in enumerate(anim):
        if len(m) != TALK_ANIM_BYTES:
            die("%s member %d is %d bytes, not %d"
                % (SRC_TALK_ANIM, i, len(m), TALK_ANIM_BYTES))
    if len(species) != 1 or len(species[0]) < TALK_SPECIES_MIN:
        die("%s holds %d member(s) of %d bytes and the talk wants one of at"
            " least %d" % (SRC_TALK_SPECIES, len(species),
                           len(species[0]) if species else 0, TALK_SPECIES_MIN))

    if HG_TALK_MSG_BANK >= len(msg_banks):
        die("%s holds %d message banks and the follower's lines are bank %d"
            % (SRC_TALK_MSG, len(msg_banks), HG_TALK_MSG_BANK))
    lines = decode_bank(msg_banks[HG_TALK_MSG_BANK])

    # A row's reaction, a reaction's line, its animation and its sound.
    named = set()
    for mi, m in enumerate(cond):
        for r in range(len(m) // TALK_ROW):
            word = struct.unpack_from("<H", m, r * TALK_ROW + 0x0A)[0]
            rid = word >> 6
            if rid == 0:
                continue
            if rid > len(react):
                die("%s member %d row %d names reaction %d and the cartridge"
                    " holds %d" % (SRC_TALK_COND, mi, r, rid, len(react)))
            named.add(rid)
    if not named:
        die("%s names no reaction at all, this is not a follower talk table"
            % SRC_TALK_COND)

    for i, m in enumerate(react):
        for s in range(TALK_REACT_STEPS):
            a, msg, snd = struct.unpack_from("<HHH", m, s * TALK_STEP_BYTES)
            if a == 0xFFFF:
                break
            if a > len(anim):
                die("reaction %d step %d wants animation %d and the cartridge"
                    " holds %d" % (i + 1, s, a, len(anim)))
            if msg > len(lines):
                die("reaction %d step %d wants line %d and the bank holds %d"
                    % (i + 1, s, msg, len(lines)))
            if snd not in (TALK_SOUND_NONE, TALK_SOUND_CRY, TALK_SOUND_CRY_ALT):
                die("reaction %d step %d plays sound %d, which is neither"
                    " silence nor the cry, a sound effect would have to be"
                    " translated and none was measured" % (i + 1, s, snd))
            emote = m[s * TALK_STEP_BYTES + 6]
            if emote > TALK_EMOTE_MAX:
                die("reaction %d step %d wants emote %d and the interpreter's"
                    " own ceiling is %d" % (i + 1, s, emote, TALK_EMOTE_MAX))
        for follow in (struct.unpack_from("<H", m, 44)[0],
                       struct.unpack_from("<H", m, 46)[0]):
            if follow > len(react):
                die("reaction %d answers a question with reaction %d and the"
                    " cartridge holds %d" % (i + 1, follow, len(react)))

    # An animation ends at a 0xFF frame or at the tenth, and both happen: the
    # interpreter checks the index before the terminator (ov02_0224FFD8), and
    # member 34 is exactly the case that would have been refused for filling
    # all ten. What is worth refusing is a facing byte the object cannot be
    # set to, since that one is written straight onto the map object.
    for i, m in enumerate(anim):
        for f in range(TALK_ANIM_FRAMES):
            face = m[f * TALK_ANIM_FRAME_BYTES]
            if face == 0xFF:
                break
            if face > 4:
                die("animation %d frame %d faces %d, and a direction is 0"
                    " (leave it) or 1..4" % (i, f, face))

    return cond, react, anim, species[0], msg_banks[HG_TALK_MSG_BANK], len(lines)



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


def narc_high(pkg: Path, archive: str) -> int:
    """The highest member index a package claims in one archive, or -1."""
    d = pkg / ".cooked/narc" / archive
    high = -1
    if not d.is_dir():
        return -1
    for f in d.iterdir():
        if f.name.isdigit():
            high = max(high, int(f.name))
    return high


def base_after(others: list[Path], archive: str, floor: int) -> int:
    """Where this package's appended members start, beside those others."""
    at = floor
    for other in others:
        if other.name == "followers":
            continue
        high = narc_high(other, archive)
        if high >= at:
            at = high + 1
    return at


def fill(rom: Path, pkg: Path, hg: Path, shiny: bool, talk: bool,
         dest_rom: Path | None, after: list[Path] | None = None) -> int:
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
    if after:
        moved = base_after(after, DST_MMODEL, first_member)
        if moved != first_member:
            print("portfollow: %s starts at %d rather than %d, past %s"
                  % (DST_MMODEL, moved, first_member,
                     ", ".join(p.name for p in after)))
        first_member = moved

    seq_member, seq_why = gf._walk_sequence(mmodel)
    if seq_member is None:
        die(seq_why)
    facings, frames = timeline(mmodel[seq_member])

    # The talk, read and refused before anything is staged, so a cartridge that
    # cannot answer for it costs the art nothing.
    talk_src = None
    talk_secs = b""
    talk_modes = b""
    emote_src = None
    if talk:
        talk_src = talk_sources(image, image.narc_members(SRC_TALK_MSG))
        talk_secs = talk_mapsecs(hg, TALK_MAP_SECTIONS + 1)
        talk_modes = talk_followmodes(hg, len(talk_secs))
        emote_src = emote_sources(image, dest_rom, after)

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
        art.append((sid, member, size, bytes(tp[sid - lo][:4]).ljust(4, b"\0")))

    stage = Path(tempfile.mkdtemp(prefix="portfollow."))
    try:
        narc = stage / ".cooked/narc" / DST_MMODEL
        narc.mkdir(parents=True, exist_ok=True)
        gen = stage / ".cooked/generated"
        gen.mkdir(parents=True, exist_ok=True)

        gfx_rows = []
        at = first_member
        for i, (sid, member, size, _tp) in enumerate(art):
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

        if talk_src is not None:
            cond, react, anim, species, msg, lines = talk_src
            talk_dir = stage / ".cooked/narc" / DST_TALK
            talk_dir.mkdir(parents=True, exist_ok=True)
            emote_model, emote_seq, emote_sheets, emote_first, ball = emote_src
            # The bubble's own members go in the archive the field effect
            # manager opens, in one contiguous run: the model, the sequence,
            # then the fourteen sheets in the source's own order, so an id is
            # the base plus the emote and nothing has to be looked up. The
            # ball's three follow the sheets.
            fx = stage / ".cooked/narc" / DST_EMOTE
            fx.mkdir(parents=True, exist_ok=True)
            (fx / str(emote_first)).write_bytes(emote_model)
            (fx / str(emote_first + 1)).write_bytes(emote_seq)
            for i, sheet in enumerate(emote_sheets):
                (fx / str(emote_first + 2 + i)).write_bytes(sheet)
            ball_first = emote_first + 2 + len(emote_sheets)
            for i, blob in enumerate(ball):
                (fx / str(ball_first + i)).write_bytes(blob)

            # Four bytes a sprite, in the band's own order, so the client
            # turns a graphics id into a record with a subtraction.
            tp_rows = b"".join(t for _sid, _m, _sz, t in art)

            (talk_dir / "0").write_bytes(
                talk_header(len(cond), len(react), len(anim), 1, 1, 1, 1, 1,
                            lines, emote_first, emote_first + 1,
                            emote_first + 2, len(emote_sheets),
                            ball_first, ball_first + 1, ball_first + 2))
            at_talk = 1
            for section in (cond, react, anim, [species], [talk_secs], [msg],
                            [tp_rows], [talk_modes]):
                for blob in section:
                    (talk_dir / str(at_talk)).write_bytes(blob)
                    at_talk += 1

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

    small = sum(1 for _, _, size, _h in art if size == 32)
    large = len(art) - small
    written = at - first_member
    payload = sum(len(mmodel[m]) for _, m, _, _ in art)
    print("portfollow: %d follower sprites (%d small, %d large) from %s"
          % (len(art), small, large, image.code))
    print("portfollow: mmodel members %d..%d (%d), %.1f MB of art"
          % (first_member, at - 1, written, payload / 1e6))
    print("portfollow: gfx %d..%d%s, walk sequence %d = %d facings of %d frames"
          % (FOLLOWER_GFX_BASE, FOLLOWER_GFX_BASE + len(gfx_rows) - 1,
             " (the second half is the shiny coat)" if shiny else "",
             SEQ_WALK, facings, frames))
    if talk_src is not None:
        cond, react, anim, species, msg, lines = talk_src
        print("portfollow: talk, %d condition tables (%d world rows, %d per"
              " map section), %d reactions, %d animations, %d lines"
              % (len(cond), TALK_WORLD_ROWS, TALK_MAP_ROWS, len(react),
                 len(anim), lines))
        print("portfollow: talk tables and lines at %s (%d members)"
              % (DST_TALK, 1 + len(cond) + len(react) + len(anim) + 3))
        print("portfollow: talk map sections for source headers 0..%d,"
              " which answer here at %d..%d"
              % (len(talk_secs) - 1, TALK_HEADER_BASE,
                 TALK_HEADER_BASE + len(talk_secs) - 1))
        prevent = sum(1 for v in talk_modes
                      if v != FOLLOW_MODE_NONE
                      and (v & FOLLOW_MODE_MASK) == FOLLOW_MODE_PREVENT)
        tall = sum(1 for v in talk_modes
                   if v != FOLLOW_MODE_NONE
                   and (v & FOLLOW_MODE_MASK) == FOLLOW_MODE_HEIGHT_RESTRICT)
        print("portfollow: %d of those maps take no follower at all and %d take"
              " only the small ones, the source game's own answer, not ours"
              % (prevent, tall))
        emote_model, emote_seq, emote_sheets, emote_first, ball = emote_src
        print("portfollow: %d emote bubbles at %s members %d..%d (model %d,"
              " sequence %d)"
              % (len(emote_sheets), DST_EMOTE, emote_first,
                 emote_first + 1 + len(emote_sheets), emote_first,
                 emote_first + 1))
        large = sum(1 for _sid, _m, _sz, t in art if t[1])
        dips = sorted(set(t[2] for _sid, _m, _sz, t in art))
        print("portfollow: tp_param carried for %d sprites, %d large, walk-dip"
              " classes %s; the ball is %s members %d..%d"
              % (len(art), large, dips, DST_EMOTE, ball_first, ball_first + 2))
    else:
        print("portfollow: no talk (--no-talk); the follower walks and says"
              " nothing")
    print("portfollow: -> %s" % pkg)
    return 0


def fill_header(out: Path, hg: Path) -> int:
    """The three tables a fill needs that come out of a CHECKOUT, as C.

    WHY THERE IS A SECOND GENERATED HEADER. `follower_index.gen.h` is the
    arithmetic the running client does at every spawn: species in, graphics id
    out. None of it says which member of the source cartridge to COPY, and a
    fill is nothing but copies. Those three answers are read out of the
    decompilation here (`build`'s member map, and the two map-header readers
    above), which is fine for a developer with a checkout and no use at all to a
    player who has neither Python nor one.

    So they are frozen into C the way `soundtables.gen.h` freezes the curated
    sound tables: the RESEARCH stays in this file and needs a checkout, and the
    APPLY half compiles the answer in and needs only the player's cartridge.
    Everything else a fill reads, the art, the sequences, the five talk
    tables, the fourteen bubbles, is already on the cartridge and is copied
    from it, so this is the whole of what cannot be.

    Indexed the way the fill walks them: the member table by sprite ORDINAL
    (sprite id minus the first, which is the same order the package appends in
    and so the same order as the graphics ids), and the two map tables by the
    source game's own map header id.
    """
    rows, expansion, base, lo, hi, _spare = gf.build(hg)
    members = [gf._member_for(rows, expansion, sid)
               for sid in sorted(expansion)]
    modes = talk_followmodes(hg, TALK_MAP_SECTIONS + 1)
    secs = talk_mapsecs(hg, TALK_MAP_SECTIONS + 1)

    def arr(kind, name, count_macro, vals, per):
        body = []
        for i in range(0, len(vals), per):
            body.append("    " + " ".join("%d," % v for v in vals[i:i + per]))
        return ["static const %s %s[%s] = {" % (kind, name, count_macro),
                *body, "};", ""]

    lines = [
        "/* Generated by tools/portfollow.py --fill-header; Do not edit. */",
        "",
        "#define MMO_FOLLOWFILL_SPRITES   %d" % len(members),
        "#define MMO_FOLLOWFILL_HEADERS   %d" % len(modes),
        "#define MMO_FOLLOWFILL_MAPSECS   %d" % len(secs),
        "#define MMO_FOLLOWFILL_DST_FIRST %d"
        "  /* portfollow.py's PL_MMODEL_COUNT */" % PL_MMODEL_COUNT,
        "#define MMO_FOLLOWFILL_SPRITE_LO %d"
        "  /* the first follower sprite id on the source */" % lo,
        "",
    ]
    lines += arr("unsigned short", "kFollowerSrcMember",
                 "MMO_FOLLOWFILL_SPRITES", members, 12)
    lines += arr("unsigned char", "kFollowerMode",
                 "MMO_FOLLOWFILL_HEADERS", list(modes), 20)
    lines += arr("unsigned char", "kFollowerMapSec",
                 "MMO_FOLLOWFILL_MAPSECS", list(secs), 20)
    out.write_text("\n".join(lines))
    print("portfollow: %d source members, %d follow modes, %d map sections"
          " -> %s" % (len(members), len(modes), len(secs), out))
    return 0


def main(argv: list[str]) -> int:
    rom = pkg = dest_rom = hg = None
    shiny = True
    talk = True
    header = None
    after: list[Path] = []

    i = 1
    while i < len(argv):
        a = argv[i]
        if a == "--fill-header":
            header = (Path(argv[i + 1]) if i + 1 < len(argv)
                      else MMO / "src" / "follower_fill.gen.h")
            i += 2 if i + 1 < len(argv) else 1
        elif a == "--rom" and i + 1 < len(argv):
            rom = Path(argv[i + 1]); i += 2
        elif a == "--pkg" and i + 1 < len(argv):
            pkg = Path(argv[i + 1]); i += 2
        elif a == "--dest-rom" and i + 1 < len(argv):
            dest_rom = Path(argv[i + 1]); i += 2
        elif a == "--heartgold" and i + 1 < len(argv):
            hg = Path(argv[i + 1]); i += 2
        elif a == "--after" and i + 1 < len(argv):
            # A package this fill has to allocate around, repeatable. Name
            # exactly the ones that will be loaded beside it: see base_after.
            after.append(Path(argv[i + 1])); i += 2
        elif a == "--no-shiny":
            shiny = False; i += 1
        elif a == "--no-talk":
            talk = False; i += 1
        else:
            die("unknown argument %r; see the header of this file" % a)

    if header is None and (rom is None or pkg is None):
        die("usage: portfollow.py --rom <nds> --pkg <dir> [--no-shiny] "
            "[--no-talk] [--dest-rom <nds>] [--heartgold <dir>] "
            "[--after <pkg-dir>]...\n"
            "   or: portfollow.py --fill-header [out.h] [--heartgold <dir>]")
    if header is not None:
        # The tables only, out of a checkout. No cartridge is read and none is
        # asked for: this is the half a player's machine cannot do, frozen so
        # that it does not have to.
        if hg is None:
            hg = gf.decomp_dir("pokeheartgold")
        if hg is None or not hg.is_dir():
            die("need a heartgold checkout for the fill tables; pass "
                "--heartgold or set DECOMP_DIR")
        return fill_header(header, hg)
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

    return fill(rom, pkg, hg, shiny, talk, dest_rom, after)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
