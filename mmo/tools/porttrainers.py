#!/usr/bin/env python3
"""Carry HeartGold's trainers into this game's own trainer archives."""

from __future__ import annotations

import re
import struct

import portscript
import sys
from pathlib import Path

# HeartGold's own, by the paths its filesystem.mk gives them.
SRC_TRDATA = "a/0/5/5"
SRC_TRPOKE = "a/0/5/6"
SRC_TRTBL = "a/0/5/7"
SRC_TRTBLOFS = "a/1/3/1"
SRC_MSG = "a/0/2/7"
SRC_MSG_NAMES = 729          # msg_0729, one string per trainer
SRC_MSG_LINES = 728          # msg_0728, one string per message record

DST_TRDATA = "poketool/trainer/trdata.narc"
DST_TRPOKE = "poketool/trainer/trpoke.narc"
DST_TRTBL = "poketool/trmsg/trtbl.narc"
DST_TRTBLOFS = "poketool/trmsg/trtblofs.narc"
DST_MSG = "msgdata/pl_msg.narc"
DST_MSG_NAMES = 618          # TEXT_BANK_NPC_TRAINER_NAMES
DST_MSG_LINES = 617          # TEXT_BANK_NPC_TRAINER_MESSAGES

# A CLASS, APPENDED WHOLE.
SRC_TRFGRA = "a/0/5/8"
DST_TRFGRA = "poketool/trgra/trfgra.narc"
SRC_MSG_CLASSES = 730        # msg_0730, one string per class
DST_MSG_CLASSES = 619        # TEXT_BANK_TRAINER_CLASS_NAMES
PL_CLASS_COUNT = 105
CLASS_FILES = 5

# What this game's built image holds. An appended trainer starts here, the same
# rule the map archives follow, and the same reason: pc_modfs refuses a hole.
PL_TRAINER_COUNT = 928

# Where A TRAINER'S SCRIPT IS, and why the table has to grow.
DST_SCRIPTS = "fielddata/script/scr_seq.narc"
PL_BATTLE_SCRIPTS = 1114        # scr_seq.naix: scripts_battles
PL_APPROACH_ENTRY = 928         # MAX_TRAINERS, and the trainer it would be

# The two numbers both games use to turn an object event's script id into a
# trainer, and the first trainer index both count from.
SCRIPT_ID_SINGLE = 3000
SCRIPT_ID_DOUBLE = 5000
FIRST_TRAINER = 1

TRDATA_RECORD = 20
TRDATA_CLASS = 1             # the byte this rewrites
TRTBL_RECORD = 4

CLASS_ROW = re.compile(
    r"^hg\s+(\d+)\s+(\d+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)"
    r"\s+([MF])\s+(\d+)\s+(\S+)")


def die(msg: str) -> None:
    print("porttrainers: " + msg, file=sys.stderr)
    raise SystemExit(2)


def load_classes(path: Path) -> dict[int, tuple]:
    """mmo/TRAINER_CLASS: HeartGold class -> what this port makes of it."""
    if not path.is_file():
        die("no %s; run tools/gen_trainer_class.py" % path)
    out: dict[int, tuple] = {}
    for line in path.read_text().splitlines():
        m = CLASS_ROW.match(line)
        if m:
            out[int(m.group(1))] = (int(m.group(2)), m.group(3), m.group(4),
                                    m.group(6), m.group(7), m.group(8),
                                    int(m.group(9)), m.group(10))
    if not out:
        die("%s has no rows in the eleven-column shape; regenerate it" % path)
    return out


def trainer_of_script(script: int) -> int | None:
    """The trainer an object event's script id names, or None if it names none.

    The same arithmetic in both games, from both games' own source.
    """
    if SCRIPT_ID_SINGLE <= script < SCRIPT_ID_DOUBLE:
        return script - SCRIPT_ID_SINGLE + FIRST_TRAINER
    if SCRIPT_ID_DOUBLE <= script < SCRIPT_ID_DOUBLE + 2000:
        return script - SCRIPT_ID_DOUBLE + FIRST_TRAINER
    return None


def script_of_trainer(script: int, trainer: int) -> int:
    """The same object event's script id, naming a trainer of this game's."""
    base = SCRIPT_ID_SINGLE if script < SCRIPT_ID_DOUBLE else SCRIPT_ID_DOUBLE
    return base + trainer - FIRST_TRAINER


def offsets_of(ofs_member: bytes, trainer: int) -> int | None:
    """`trtblofs`: where a trainer's message records begin, or None."""
    at = trainer * 2
    if at + 2 > len(ofs_member):
        return None
    return struct.unpack_from("<H", ofs_member, at)[0]


def messages_of(tbl: bytes, ofs: bytes, trainer: int) -> list[tuple[int, int]]:
    """(message type, its index in the bank) for one trainer, in table order."""
    out = []
    at = offsets_of(ofs, trainer)
    if at is None:
        return out
    while at + TRTBL_RECORD <= len(tbl):
        who, kind = struct.unpack_from("<HH", tbl, at)
        if who != trainer:
            break
        out.append((kind, at // TRTBL_RECORD))
        at += TRTBL_RECORD
    return out


class Carried:
    """What a run carried, and everything the caller has to write."""

    def __init__(self) -> None:
        self.ids: dict[int, int] = {}          # HeartGold trainer -> this game's
        # This game's trainer id -> the class HeartGold gave them, kept because
        # the class in `trdata` is the one they are drawn as and the track a
        # trainer is heard as is chosen off the source's own.
        self.source_class: dict[int, int] = {}
        self.members: list[tuple[str, int, bytes]] = []
        self.banks: dict[int, list[list[int]]] = {}   # bank -> its strings
        self.bank_seed: dict[int, int] = {}
        self.stats: dict[str, int] = {}
        # Every class appended: (this game's id, gender M/F, prize per level,
        # the source's own battle theme name or "-").
        self.classes: list[tuple[int, str, int, str]] = []

    def bump(self, key: str, n: int = 1) -> None:
        self.stats[key] = self.stats.get(key, 0) + n


def widen_battle_scripts(member: bytes, last_trainer: int) -> bytes:
    """`scripts_battles` with an entry for every carried trainer."""
    old, after = portscript.entry_offsets(member)
    if len(old) <= PL_APPROACH_ENTRY:
        die("scripts_battles reads as %d entries, fewer than the approach slot "
            "at %d, this is not the member the loader names"
            % (len(old), PL_APPROACH_ENTRY))
    want = max(len(old), last_trainer)
    if want == len(old):
        return member
    grew = (want - len(old)) * 4
    out = bytearray()
    for i in range(want):
        target = (old[i] if i < len(old) else old[0]) + grew
        out += struct.pack("<i", target - 4 * (i + 1))
    out += struct.pack("<H", portscript.SCRIPT_TABLE_END)
    out += member[after:]
    return bytes(out)


def carry_classes(src, dst, classes: dict, decode_bank, s_msg, d_msg,
                  out: "Carried") -> None:
    """Every `appended` class of mmo/TRAINER_CLASS, into this game's archives."""
    rows = sorted((r[0], hg, r) for hg, r in classes.items()
                  if r[1] == "appended")
    if not rows:
        return
    ids = [r[0] for r in rows]
    if ids != list(range(PL_CLASS_COUNT, PL_CLASS_COUNT + len(ids))):
        die("mmo/TRAINER_CLASS appends classes %d..%d with a hole or a start "
            "past %d; regenerate it" % (ids[0], ids[-1], PL_CLASS_COUNT))
    s_gfx = src(SRC_TRFGRA)
    d_gfx = dst(DST_TRFGRA)
    if len(d_gfx) != PL_CLASS_COUNT * CLASS_FILES:
        die("this game's trainer picture archive holds %d members for %d "
            "classes of %d files; an append has to start at the count"
            % (len(d_gfx), PL_CLASS_COUNT, CLASS_FILES))
    raw = d_msg[DST_MSG_CLASSES]
    names = decode_bank(raw)
    if len(names) != PL_CLASS_COUNT:
        die("this game's class-name bank holds %d strings for %d classes"
            % (len(names), PL_CLASS_COUNT))
    src_names = decode_bank(s_msg[SRC_MSG_CLASSES])
    out.bank_seed[DST_MSG_CLASSES] = struct.unpack_from("<H", raw, 2)[0]
    for pl, hg, row in rows:
        first = hg * CLASS_FILES
        if first + CLASS_FILES > len(s_gfx):
            die("HeartGold class %d has no picture: its archive holds %d "
                "classes" % (hg, len(s_gfx) // CLASS_FILES))
        for k in range(CLASS_FILES):
            out.members.append((DST_TRFGRA, pl * CLASS_FILES + k,
                                s_gfx[first + k]))
        if hg >= len(src_names):
            die("HeartGold class %d has no printed name" % hg)
        names.append(src_names[hg])
        out.classes.append((pl, row[5], row[6], row[7]))
    out.banks[DST_MSG_CLASSES] = names
    out.bump("classes appended", len(rows))


def carry(src, dst, want: list[int], classes: dict[int, tuple[int, str, str]],
          decode_bank, first_id: int = PL_TRAINER_COUNT) -> Carried:
    """Every wanted trainer, appended to this game's archives from `first_id`."""
    out = Carried()

    s_data = src(SRC_TRDATA)
    s_poke = src(SRC_TRPOKE)
    s_tbl = src(SRC_TRTBL)[0]
    s_ofs = src(SRC_TRTBLOFS)[0]
    d_data = dst(DST_TRDATA)
    d_poke = dst(DST_TRPOKE)
    d_tbl = bytearray(dst(DST_TRTBL)[0])
    d_ofs = bytearray(dst(DST_TRTBLOFS)[0])

    if len(d_data) != first_id or len(d_poke) != first_id:
        die("this game holds %d trainer headers and %d parties; an append has "
            "to start at the count and they disagree"
            % (len(d_data), len(d_poke)))
    if len(d_ofs) != first_id * 2:
        die("this game's trtblofs is %d bytes for %d trainers; it has to be "
            "two per trainer" % (len(d_ofs), first_id))

    d_msg = dst(DST_MSG)
    raw_names = d_msg[DST_MSG_NAMES]
    raw_lines = d_msg[DST_MSG_LINES]
    names = decode_bank(raw_names)
    lines = decode_bank(raw_lines)
    out.bank_seed[DST_MSG_NAMES] = struct.unpack_from("<H", raw_names, 2)[0]
    out.bank_seed[DST_MSG_LINES] = struct.unpack_from("<H", raw_lines, 2)[0]
    if len(names) != first_id:
        die("this game's trainer-name bank holds %d strings for %d trainers"
            % (len(names), first_id))
    if len(lines) * TRTBL_RECORD != len(d_tbl):
        die("this game's trainer-message bank holds %d strings and its message "
            "table has %d records; a record's own ordinal IS its string, so "
            "the two are the same length or neither can be appended to"
            % (len(lines), len(d_tbl) // TRTBL_RECORD))

    s_msg = src(SRC_MSG)
    src_names = decode_bank(s_msg[SRC_MSG_NAMES])
    src_lines = decode_bank(s_msg[SRC_MSG_LINES])

    carry_classes(src, dst, classes, decode_bank, s_msg, d_msg, out)

    at = first_id
    # The two IDS nobody may have, held open before the carrying starts.
    #
    # A trainer's script entry is its id less one, and entry 928 is
    # `Battles_ApproachingTrainer`, the scene a trainer who sees you plays,
    # at an index the engine has compiled in and which therefore cannot move.
    # So trainer 929 has no script of its own, and nobody may be 929.
    #
    # Held open at the front rather than skipped in the middle, because every
    # one of the four tables below is appended to in step with `at`: a skipped
    # id in the middle would leave trdata, trpoke, trtblofs and the name bank
    # each one row short of where the next trainer's id says they are, and the
    # first symptom is a trainer saying somebody else's line.
    for _ in range(PL_APPROACH_ENTRY + 2 - first_id):
        out.members.append((DST_TRDATA, at, bytes(TRDATA_RECORD)))
        out.members.append((DST_TRPOKE, at, b""))
        names.append([0xFFFF])
        d_ofs += struct.pack("<H", len(d_tbl))
        at += 1

    for t in want:
        if not (0 < t < len(s_data)):
            out.bump("past the source's own trainers")
            continue
        header = bytearray(s_data[t])
        if len(header) != TRDATA_RECORD:
            die("HeartGold trainer %d is %d bytes, not %d"
                % (t, len(header), TRDATA_RECORD))
        row = classes.get(header[TRDATA_CLASS])
        if row is None:
            die("HeartGold class %d has no row in mmo/TRAINER_CLASS; "
                "regenerate it" % header[TRDATA_CLASS])
        out.source_class[at] = header[TRDATA_CLASS]
        header[TRDATA_CLASS] = row[0]
        out.bump("class " + row[1])

        out.members.append((DST_TRDATA, at, bytes(header)))
        out.members.append((DST_TRPOKE, at, s_poke[t]))
        out.ids[t] = at

        names.append(src_names[t] if t < len(src_names) else [0xFFFF])

        # The message records go behind every record this game already had, so
        # a trainer of this game's that has none still walks off the end of its
        # own run and finds nothing, exactly as it does today.
        mine = messages_of(s_tbl, s_ofs, t)
        d_ofs += struct.pack("<H", len(d_tbl))
        for kind, where in mine:
            if len(lines) * TRTBL_RECORD != len(d_tbl):
                die("the message table and its bank came apart at trainer %d "
                    "(%d records, %d strings)"
                    % (t, len(d_tbl) // TRTBL_RECORD, len(lines)))
            d_tbl += struct.pack("<HH", at, kind)
            lines.append(src_lines[where] if where < len(src_lines)
                         else [0xFFFF])
        out.bump("messages", len(mine))
        if not mine:
            out.bump("trainers with nothing to say")
        at += 1

    out.members.append((DST_TRTBL, 0, bytes(d_tbl)))
    out.members.append((DST_TRTBLOFS, 0, bytes(d_ofs)))
    if out.ids:
        out.members.append((DST_SCRIPTS, PL_BATTLE_SCRIPTS,
                            widen_battle_scripts(dst(DST_SCRIPTS)[PL_BATTLE_SCRIPTS],
                                                 max(out.ids.values()))))
    out.banks[DST_MSG_NAMES] = names
    out.banks[DST_MSG_LINES] = lines
    out.bump("trainers", len(out.ids))
    return out
