#!/usr/bin/env python3
"""portmoves.py, fill a package with the 92 moves a Gen 5 cartridge adds."""
from __future__ import annotations

import argparse
import struct
import sys
from collections import Counter, defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gen5_tables  # noqa: E402
import porticons  # noqa: E402
import portspecies  # noqa: E402

MMO = Path(__file__).resolve().parent.parent

MOVE_TABLE_NARC = "poketool/waza/pl_waza_tbl.narc"
ANIM_NARC = "wazaeffect/we.arc"
SEQ_NARC = "battle/skill/waza_seq.narc"
MESSAGE_NARC = "msgdata/pl_msg.narc"
BANK_DESCRIPTIONS, BANK_NAMES, BANK_NAMES_UPPER = 646, 647, 648
# "{nickname} used\nMOVE!" and its wild and foe's forms: three entries a move,
# the name inline, read by the battle at move * 3 (BattleDisplay_PrintAttackMessage).
# Not grown, a Gen 5 move printed a blank box (seen 2026-08-31).
BANK_USED_IN_BATTLE = 0
EOS = 0xFFFF
LAST_SHARED_MOVE = 467
USED_FORMS = 3
TEXT_MOVE_DESCRIPTIONS_BLACK = 202          # beside gen5_tables.TEXT_MOVE_NAMES at 203

# This game's own counts. 471 and 501 are not 468: the table carries three
# dummies past Shadow Force and the script archives thirty-three placeholders,
# and the engine's MAX_MOVES is 468 regardless. The fill writes over them.
TABLE_ROM_MEMBERS = 471
ANIM_ROM_MEMBERS = 501
SEQ_ROM_MEMBERS = 501
BANK_ENTRIES = 468
FIRST_PORTED = 468
LAST_PORTED = 559
FIRST_SHARED, LAST_SHARED = 1, 467

ENTRY = "<HBBBBBBHbBBBH"                  # effect class power type acc pp chance range prio flags c.effect c.type pad
ENTRY_BYTES = 16
CLASS_STATUS = 2
GEN5_CLASS = {1: 0, 2: 1, 0: 2}           # Gen 5 physical/special/status -> this game's class
# Gen 5 range byte -> this game's RANGE_* bit, the majority over the shared 467;
# the ten moves that disagree are gen5_tables.RANGE_CHANGED.
GEN5_RANGE = {0: 0, 1: 512, 2: 256, 3: 1024, 4: 8, 5: 4, 6: 32, 7: 16, 8: 64,
              9: 2, 10: 64, 11: 128, 12: 32, 13: 1}
# (Gen 5 bit, this game's bit): contact, protect, magic coat, snatch, mirror move.
FLAG_BITS = ((0, 0), (3, 1), (4, 2), (5, 3), (6, 4))
KINGS_ROCK, HIDES_HP_GAUGES, HIDES_SHADOWS = 0x20, 0x40, 0x80
PRESENTATION = HIDES_HP_GAUGES | HIDES_SHADOWS
EFFECT_HIT, EFFECT_SPLASH, LAST_GEN4_EFFECT = 0, 85, 276

# The disagreements the shared range is known to have, by field. Each is a
# change Gen 5 made and gen5_tables.py names the moves; a different number here
# means the decode moved, not the games.
EXPECTED = {"power": 21, "accuracy": 19, "pp": 8, "chance": 10, "priority": 5,
            "effect": 2, "range": 10, "type": 1, "class": 0,
            "flag contact": 0, "flag protect": 4, "flag magic coat": 16,
            "flag snatch": 12, "flag mirror move": 0}

LINE_CHARS = 23          # the longest line in this game's own 467 descriptions
LINE_CHARS_LOOSE = 25    # allowed only when it saves a sixth line
BOX_LINES = 5
NAME_CHARS = 12          # the longest name this game holds
NEWLINE = 0xE000
TERMINATOR = 0xFFFF
GEN5_NEWLINE = "￾"
APOSTROPHE_FOLD = {"'": "’"}


def die(msg):
    sys.stderr.write("portmoves: %s\n" % msg)
    sys.exit(1)


# ------------------------------------------------------------------- charmap

def charmap(engine):
    """(encode: char -> code, decode: code -> char), from the encoder the game's
    own text was built with. Command rows are not characters and are skipped."""
    path = Path(engine) / "tools" / "msgenc" / "charmap.txt"
    if not path.is_file():
        die("no charmap at %s" % path)
    encode, decode = {}, {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line or line.startswith("//") or "=" not in line:
            continue
        code, char = line.split("=", 1)
        if char.startswith("{"):
            continue
        char = {"\\n": "\n", "\\r": "\r"}.get(char, char)
        if char.startswith("\\x"):
            continue
        code = int(code, 16)
        decode[code] = char
        encode.setdefault(char, code)
    return encode, decode


def decode_text(codes, decode):
    out = []
    for c in codes:
        if c == TERMINATOR:
            break
        if c not in decode:
            return None
        out.append(decode[c])
    return "".join(out)


def encode_text(text, encode, what):
    codes = []
    for char in text:
        char = APOSTROPHE_FOLD.get(char, char)
        code = encode.get(char)
        if code is None:
            die("%r in %s is a character the engine's charmap has no row for" % (char, what))
        codes.append(code)
    return codes + [TERMINATOR]


def wrap(text, width):
    words = text.replace(GEN5_NEWLINE, " ").split()
    lines, cur = [], ""
    for word in words:
        if not cur:
            cur = word
        elif len(cur) + 1 + len(word) <= width:
            cur += " " + word
        else:
            lines.append(cur)
            cur = word
    if cur:
        lines.append(cur)
    return lines


def rewrap(text):
    """This game's box: up to five lines of 23. A description that needs a
    sixth is tried at 25 first; the caller counts what still overflows."""
    lines = wrap(text, LINE_CHARS)
    if len(lines) > BOX_LINES:
        loose = wrap(text, LINE_CHARS_LOOSE)
        if len(loose) <= BOX_LINES:
            lines = loose
    return lines


# --------------------------------------------------------------------- table

def entry(row, donor_flags, kings_rock, contest):
    """This game's 16-byte move entry out of a Gen 5 row."""
    cls = GEN5_CLASS[row["category"]]
    effect = row["effect"]
    if effect > LAST_GEN4_EFFECT:
        effect = EFFECT_HIT if cls != CLASS_STATUS else EFFECT_SPLASH
    if row["range"] not in GEN5_RANGE:
        die("Gen 5 range byte %d has no row in GEN5_RANGE" % row["range"])
    flags = 0
    for gen5_bit, bit in FLAG_BITS:
        if row["flags"] & (1 << gen5_bit):
            flags |= 1 << bit
    if kings_rock(effect, cls):
        flags |= KINGS_ROCK
    flags |= donor_flags & PRESENTATION
    c_effect, c_type = contest(gen5_tables.server_type(row["type"]), cls)
    return struct.pack(ENTRY, effect, cls, row["power"], gen5_tables.server_type(row["type"]),
                       row["accuracy"], row["pp"], row["chance"], GEN5_RANGE[row["range"]],
                       row["priority"], flags, c_effect, c_type, 0)


def unpack(member):
    e, cls, power, ty, acc, pp, chance, rng, prio, flags, c_effect, c_type, _pad = struct.unpack(ENTRY, member)
    return {"effect": e, "class": cls, "power": power, "type": ty, "accuracy": acc, "pp": pp,
            "chance": chance, "range": rng, "priority": prio, "flags": flags,
            "contest": (c_effect, c_type)}


class Rules:
    """The three answers no Gen 5 row can give, measured off this game's own
    table rather than carried: king's rock by effect, contest data by type and
    class, and the battle stub by effect."""

    def __init__(self, table, stubs):
        own = {m: unpack(table[m]) for m in range(FIRST_SHARED, LAST_SHARED + 1)}
        by_effect = defaultdict(list)
        by_type_class = defaultdict(Counter)
        by_class = defaultdict(Counter)
        stub_votes = defaultdict(Counter)
        for m, e in own.items():
            by_effect[e["effect"]].append(bool(e["flags"] & KINGS_ROCK))
            by_type_class[(e["type"], e["class"])][e["contest"]] += 1
            by_class[e["class"]][e["contest"]] += 1
            stub_votes[e["effect"]][stubs[m]] += 1
        self.own = own
        self._kings = {e: sum(v) * 2 > len(v) for e, v in by_effect.items()}
        self._contest = {k: v.most_common(1)[0][0] for k, v in by_type_class.items()}
        self._contest_class = {k: v.most_common(1)[0][0] for k, v in by_class.items()}
        self._stub = {e: v.most_common(1)[0][0] for e, v in stub_votes.items()}
        self.stub_groups = sum(1 for v in stub_votes.values() if sum(v.values()) > 1)
        self.stub_unanimous = sum(1 for v in stub_votes.values() if sum(v.values()) > 1 and len(v) == 1)

    def kings_rock(self, effect, cls):
        if effect in self._kings:
            return self._kings[effect]
        return cls != CLASS_STATUS

    def contest(self, move_type, cls):
        return self._contest.get((move_type, cls)) or self._contest_class[cls]

    def stub(self, effect):
        return self._stub.get(effect) or self._stub[EFFECT_HIT]


# --------------------------------------------------------------------- anims

def read_anims(path):
    """mmo/MOVE_ANIMS: `<gen5 move> <gen4 donor>` per line, comments after #."""
    table = {}
    if not Path(path).is_file():
        return table
    for n, line in enumerate(Path(path).read_text().splitlines(), 1):
        body = line.split("#", 1)[0].split()
        if not body:
            continue
        if len(body) < 2 or not all(x.isdigit() for x in body[:2]):
            die("%s:%d: expected `<gen5 move> <gen4 donor>`, got %r" % (path, n, line))
        table[int(body[0])] = int(body[1])
    return table


def propose_anims(gen5_rows, own, names4, names5):
    """The rule's first cut: same type and class, the nearest power for a
    damaging move, the same effect where a Gen 4 move has it, and the newest
    otherwise. Printed for a person to correct, never applied on its own."""
    lines = ["# MOVE_ANIMS, which Gen 4 animation a Gen 5 move borrows.",
             "#",
             "# <gen5 move>  <gen4 donor>   # <gen5 name> <- <gen4 name>: why",
             "# Proposed by tools/portmoves.py --propose-anims; the hand edits carry",
             "# their own reason. A donor is the script the engine plays under the",
             "# Gen 5 id, bytes for bytes, so the two presentation flag bits follow it."]
    for m in range(FIRST_PORTED, LAST_PORTED + 1):
        row = gen5_rows[m]
        ty, cls = gen5_tables.server_type(row["type"]), GEN5_CLASS[row["category"]]
        cands = [k for k, e in own.items() if e["type"] == ty and e["class"] == cls]
        why = "same type and class"
        if not cands:
            cands = [k for k, e in own.items() if e["class"] == cls]
            why = "same class; no Gen 4 move of that type and class"
        same_effect = [k for k in cands if own[k]["effect"] == row["effect"]]
        if same_effect:
            pick, why = max(same_effect), why + ", same effect"
        elif cls != CLASS_STATUS:
            pick = min(cands, key=lambda k: (abs(own[k]["power"] - row["power"]), -k))
            why += ", nearest power (%d for %d)" % (own[pick]["power"], row["power"])
        else:
            pick, why = max(cands), why + ", newest"
        lines.append("%d %d   # %s <- %s: %s" % (m, pick, names5[m], names4[pick], why))
    return "\n".join(lines) + "\n"



def render_header(anims):
    """The client's copy of what a player's machine cannot work out."""
    lines = ["/* move_port.gen.h, GENERATED by tools/portmoves.py --header; DO NOT EDIT.",
             " *",
             " * Which Gen 4 animation each of the 92 ported moves borrows, and the",
             " * disagreements the shared range is KNOWN to have with this game's own",
             " * table, both of them answers no cartridge can give, so both are frozen",
             " * here for the fill that runs on a player's machine.",
             " *",
             " * The donors are mmo/MOVE_ANIMS, which carries the reason for each by hand.",
             " * The counts are the changes Gen 5 made to moves both games have; the fill",
             " * refuses whole if it measures a different number, because a count that",
             " * moves is a decode that moved. */",
             "",
             "#define MMO_PORTED_MOVE_FIRST   %d" % FIRST_PORTED,
             "#define MMO_PORTED_MOVE_LAST    %d" % LAST_PORTED,
             "#define MMO_PORTED_MOVE_SHARED  %d  /* the last move both games have */"
             % LAST_SHARED,
             "",
             "/* One donor per move from MMO_PORTED_MOVE_FIRST, in order. */",
             "static const unsigned short MMO_PORTED_MOVE_ANIM[] = {"]
    row = "   "
    for m in range(FIRST_PORTED, LAST_PORTED + 1):
        row += " %d," % anims[m]
        if len(row) > 68:
            lines.append(row)
            row = "   "
    if row.strip():
        lines.append(row)
    lines.append("};")
    lines.append("")
    lines.append("/* What the shared range is allowed to disagree about, field by field. */")
    for name, want in sorted(EXPECTED.items()):
        lines.append("#define MMO_PORTED_MOVE_DIFF_%-12s %d"
                     % (name.replace(" ", "_").upper(), want))
    return "\n".join(lines) + "\n"


# -------------------------------------------------------------------- checks

def check(black_rows, black_names, black_descs, plat, engine, anims):
    """Prove the writer on this game's own members and the decode on the shared
    range. Returns (failures, lines)."""
    failures, lines = [], []
    table = plat.narc_members(MOVE_TABLE_NARC)
    anim = plat.narc_members(ANIM_NARC)
    seq = plat.narc_members(SEQ_NARC)
    msg = plat.narc_members(MESSAGE_NARC)
    for path, got, want in ((MOVE_TABLE_NARC, len(table), TABLE_ROM_MEMBERS),
                            (ANIM_NARC, len(anim), ANIM_ROM_MEMBERS),
                            (SEQ_NARC, len(seq), SEQ_ROM_MEMBERS)):
        if got != want:
            failures.append("%s holds %d members and this fill is built on %d" % (path, got, want))
    if failures:
        return failures, lines
    if any(len(table[m]) != ENTRY_BYTES for m in range(len(table))):
        failures.append("a member of %s is not %d bytes" % (MOVE_TABLE_NARC, ENTRY_BYTES))
        return failures, lines
    rules = Rules(table, seq)

    # The shared range, rebuilt out of Black, against this game's own entries.
    counts = Counter()
    kings_agree = contest_agree = 0
    for m in range(FIRST_SHARED, LAST_SHARED + 1):
        own = rules.own[m]
        mine = unpack(entry(black_rows[m], own["flags"], rules.kings_rock, rules.contest))
        for field in ("power", "accuracy", "pp", "chance", "priority", "effect", "range", "type", "class"):
            if mine[field] != own[field]:
                counts[field] += 1
        for (_g5, bit), name in zip(FLAG_BITS, ("contact", "protect", "magic coat", "snatch", "mirror move")):
            if (mine["flags"] ^ own["flags"]) & (1 << bit):
                counts["flag " + name] += 1
        kings_agree += not ((mine["flags"] ^ own["flags"]) & KINGS_ROCK)
        contest_agree += mine["contest"] == own["contest"]
    for field, want in EXPECTED.items():
        if counts[field] != want:
            failures.append("%s: %d shared moves disagree with this game's table and %d were "
                            "measured; a gap that moves has to be written down" % (field, counts[field], want))
    lines.append("moves: the 467 shared entries rebuild this game's own bar the %d changes Gen 5 "
                 "made; king's rock rule right on %d, contest rule right on %d"
                 % (sum(EXPECTED.values()), kings_agree, contest_agree))
    lines.append("stubs: %d of %d effects shared by several moves have one stub" % (rules.stub_unanimous, rules.stub_groups))

    # The text writer: this game's three banks, decoded and encoded back.
    encode, decode = charmap(engine)
    for bank in (BANK_DESCRIPTIONS, BANK_NAMES, BANK_NAMES_UPPER):
        seed, entries = portspecies.read_bank(msg[bank])
        if len(entries) != BANK_ENTRIES:
            failures.append("bank %d holds %d entries and this fill is built on %d" % (bank, len(entries), BANK_ENTRIES))
            continue
        again = []
        for codes in entries:
            text = decode_text(codes, decode)
            if text is None:
                failures.append("bank %d has a code the charmap has no character for" % bank)
                break
            again.append(encode_text(text, encode, "bank %d" % bank))
        if portspecies.write_bank(seed, again) != msg[bank]:
            failures.append("bank %d does not come back byte for byte through the charmap" % bank)
    lines.append("text: the three move banks decode and encode back byte for byte through the engine's charmap")
    for m in range(FIRST_PORTED, LAST_PORTED + 1):
        if len(black_names[m]) > NAME_CHARS:
            failures.append("move %d is %r, longer than any name this game holds" % (m, black_names[m]))
        for char in black_names[m] + black_descs[m].replace(GEN5_NEWLINE, ""):
            if APOSTROPHE_FOLD.get(char, char) not in encode:
                failures.append("move %d uses %r, which the charmap has no row for" % (m, char))
                break

    # The donor table.
    missing = [m for m in range(FIRST_PORTED, LAST_PORTED + 1) if m not in anims]
    if missing:
        failures.append("mmo/MOVE_ANIMS names no animation donor for %d moves (first %s); "
                        "--propose-anims prints a first cut" % (len(missing), missing[:5]))
    bad = [(m, d) for m, d in anims.items() if not FIRST_SHARED <= d <= LAST_SHARED]
    if bad:
        failures.append("mmo/MOVE_ANIMS donors past this game's own moves: %s" % bad[:5])
    return failures, lines


# ---------------------------------------------------------------------- fill

def grow_used_in_battle(used, names):
    """The attack-message bank grown to three entries for every name in `names`:
    each new triple is the last shared move's triple with its name swapped, the
    name found after the line break the three share and before the "!"."""
    last = LAST_SHARED_MOVE
    have = [c for c in names[last] if c != EOS]
    templates = []
    for k in range(USED_FORMS):
        e = used[last * USED_FORMS + k]
        at = e.index(NEWLINE) + 1
        if e[at:at + len(have)] != have:
            die("attack message %d does not carry the name of move %d where expected" % (last * USED_FORMS + k, last))
        templates.append((e[:at], e[at + len(have):]))
    grown = list(used[:(last + 1) * USED_FORMS])
    for m in range(last + 1, len(names)):
        name = [c for c in names[m] if c != EOS]
        for head, tail in templates:
            grown.append(head + name + tail)
    return grown


def fill(pkg, black_rows, black_names, black_descs, plat, engine, anims, write):
    table = plat.narc_members(MOVE_TABLE_NARC)
    anim = plat.narc_members(ANIM_NARC)
    seq = plat.narc_members(SEQ_NARC)
    msg = plat.narc_members(MESSAGE_NARC)
    rules = Rules(table, seq)
    encode, _decode = charmap(engine)
    six = 0
    for m in range(FIRST_PORTED, LAST_PORTED + 1):
        donor = anims[m]
        e = entry(black_rows[m], rules.own[donor]["flags"], rules.kings_rock, rules.contest)
        write(pkg, MOVE_TABLE_NARC, m, e)
        write(pkg, ANIM_NARC, m, bytes(anim[donor]))
        write(pkg, SEQ_NARC, m, bytes(rules.stub(unpack(e)["effect"])))
    banks = {}
    for bank in (BANK_DESCRIPTIONS, BANK_NAMES, BANK_NAMES_UPPER):
        seed, entries = portspecies.read_bank(msg[bank])
        for m in range(FIRST_PORTED, LAST_PORTED + 1):
            if bank == BANK_NAMES:
                text = black_names[m]
            elif bank == BANK_NAMES_UPPER:
                text = black_names[m].upper()
            else:
                lines_ = rewrap(black_descs[m])
                six += len(lines_) > BOX_LINES
                text = "\n".join(lines_)
            entries.append(encode_text(text, encode, "move %d" % m))
        banks[bank] = portspecies.write_bank(seed, entries)
        write(pkg, MESSAGE_NARC, bank, banks[bank])
    seed, used = portspecies.read_bank(msg[BANK_USED_IN_BATTLE])
    _seed, names = portspecies.read_bank(banks[BANK_NAMES])
    write(pkg, MESSAGE_NARC, BANK_USED_IN_BATTLE,
          portspecies.write_bank(seed, grow_used_in_battle(used, names)))
    count = LAST_PORTED + 1 - FIRST_PORTED
    return ["moves: %d table entries, %d animation scripts, %d battle stubs and three banks of %d; "
            "%d descriptions needed a sixth line" % (count, count, count, BANK_ENTRIES + count, six // 1)]


# ---------------------------------------------------------------------- main

def black_moves(rom):
    members = rom.narc_members(gen5_tables.MOVES_NARC)
    rows = {m: gen5_tables.move_row(members[m]) for m in range(1, LAST_PORTED + 1)}
    names = gen5_tables.read_names(rom, gen5_tables.TEXT_MOVE_NAMES, LAST_PORTED + 1, "move names")
    descs = gen5_tables.read_names(rom, TEXT_MOVE_DESCRIPTIONS_BLACK, LAST_PORTED + 1, "move descriptions")
    return rows, names, descs


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--rom", help="a Black or White NDS image (not needed by --header)")
    ap.add_argument("--engine", help="the pokeplatinum checkout")
    ap.add_argument("--host-rom", help="the built Platinum image")
    ap.add_argument("--package", help="the package to fill (default mmo/mods/imports)")
    ap.add_argument("--anims", help="the donor table (default mmo/MOVE_ANIMS)")
    ap.add_argument("--check", action="store_true", help="prove and decode, write nothing")
    ap.add_argument("--propose-anims", action="store_true", help="print a first cut of MOVE_ANIMS and stop")
    ap.add_argument("--header", action="store_true",
                    help="write the client's copy of the donor table and stop")
    ap.add_argument("--out", help="where --header writes (default mmo/src/move_port.gen.h)")
    args = ap.parse_args(argv)

    if args.header:
        anims = read_anims(Path(args.anims) if args.anims else MMO / "MOVE_ANIMS")
        missing = [m for m in range(FIRST_PORTED, LAST_PORTED + 1) if m not in anims]
        if missing:
            die("mmo/MOVE_ANIMS names no donor for %d moves (first %s)"
                % (len(missing), missing[:5]))
        out = Path(args.out) if args.out else MMO / "src" / "move_port.gen.h"
        out.write_text(render_header(anims))
        print("portmoves: %d donors -> %s" % (LAST_PORTED + 1 - FIRST_PORTED, out))
        return 0

    if args.rom is None:
        die("need --rom (a Gen 5 NDS image), or --header to write the client's copy")
    engine = porticons.engine_dir(args.engine)
    modport = porticons.load_modport(engine)
    rom = modport.NitroRom(Path(args.rom))
    host = Path(args.host_rom) if args.host_rom else engine / "build/rom/pokeplatinum.us.nds"
    if not host.is_file():
        die("no built Platinum image at %s; pass --host-rom" % host)
    plat = modport.NitroRom(host)
    print("portmoves: %s %s into %s %s" % (rom.code, rom.title, plat.code, plat.title))
    rows, names, descs = black_moves(rom)

    if args.propose_anims:
        table = plat.narc_members(MOVE_TABLE_NARC)
        rules = Rules(table, plat.narc_members(SEQ_NARC))
        _enc, decode = charmap(engine)
        _seed, bank = portspecies.read_bank(plat.narc_members(MESSAGE_NARC)[BANK_NAMES])
        names4 = {m: decode_text(bank[m], decode) for m in range(FIRST_SHARED, LAST_SHARED + 1)}
        sys.stdout.write(propose_anims(rows, rules.own, names4, names))
        return 0

    anims = read_anims(Path(args.anims) if args.anims else MMO / "MOVE_ANIMS")
    failures, lines = check(rows, names, descs, plat, engine, anims)
    for line in lines:
        print("  " + line)
    if failures:
        print("  %d disagreement(s):" % len(failures))
        for line in failures[:10]:
            print("    " + line)
        die("the shared range or the writer does not hold; nothing written")
    print("  every check holds")
    if args.check:
        return 0
    pkg = Path(args.package) if args.package else MMO / "mods" / "imports"
    for line in fill(pkg, rows, names, descs, plat, engine, anims, portspecies.write_member):
        print("  " + line)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
