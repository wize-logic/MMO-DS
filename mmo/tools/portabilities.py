#!/usr/bin/env python3
"""portabilities.py, fill a package with the 41 abilities a Gen 5 cartridge adds."""

import argparse
import sys
from pathlib import Path

MMO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(MMO / "tools"))

import gen5_tables  # noqa: E402
import portmoves  # noqa: E402  (the charmap, the wrap and the apostrophe fold)
import portspecies  # noqa: E402  (the message bank reader and writer)

MESSAGE_NARC = "msgdata/pl_msg.narc"
# TEXT_BANK_ABILITY_NAMES, _UPPERCASE and _DESCRIPTIONS in the engine's own
# generated/text_banks.h.
BANK_NAMES, BANK_NAMES_UPPER, BANK_DESCRIPTIONS = 610, 611, 612
# Black's, in the same archive gen5_tables reads its species and move text from.
# 182 is beside gen5_tables.TEXT_ABILITY_NAMES; the other two were found by
# sweeping the archive for a member holding 165 entries and reading them.
TEXT_NAMES_BLACK = gen5_tables.TEXT_ABILITY_NAMES
TEXT_DESCRIPTIONS_BLACK = 183
TEXT_NAMES_UPPER_BLACK = 285

BANK_ENTRIES = 124
FIRST_SHARED, LAST_SHARED = 1, 123
FIRST_PORTED = 124
LAST_PORTED = gen5_tables.LAST_GEN5_ABILITY

# This game's own box, measured over its 124 descriptions: the longest line is 26
# characters and none of them needs a third line.
LINE_CHARS = 26
BOX_LINES = 2
# The longest name this game holds, so a Gen 5 one that would not fit in the
# space the summary screen gives it is refused rather than drawn over its border.
NAME_CHARS = 12


def die(msg):
    sys.stdout.flush()
    sys.stderr.write("portabilities: %s\n" % msg)
    raise SystemExit(1)


def black_abilities(rom):
    """Black's three ability banks, as lists indexed by ability id."""
    count = LAST_PORTED + 1
    return (gen5_tables.read_names(rom, TEXT_NAMES_BLACK, count, "ability names"),
            gen5_tables.read_names(rom, TEXT_NAMES_UPPER_BLACK, count,
                                   "ability names, upper case"),
            gen5_tables.read_names(rom, TEXT_DESCRIPTIONS_BLACK, count,
                                   "ability descriptions"))


def description(text):
    """A Gen 5 description as this game's two lines, or None if it will not fit."""
    lines = portmoves.wrap(text, LINE_CHARS)
    if len(lines) > BOX_LINES:
        return None
    return "\n".join(lines)


def check(black_names, black_uppers, black_descs, plat, engine):
    """Prove the numbering on the shared range and the writer on this game's own
    banks. Returns (failures, lines)."""
    failures, lines = [], []
    msg = plat.narc_members(MESSAGE_NARC)
    encode, decode = portmoves.charmap(engine)

    banks = {}
    for bank in (BANK_NAMES, BANK_NAMES_UPPER, BANK_DESCRIPTIONS):
        seed, entries = portspecies.read_bank(msg[bank])
        if len(entries) != BANK_ENTRIES:
            failures.append("bank %d holds %d entries and this fill is built on %d"
                            % (bank, len(entries), BANK_ENTRIES))
            continue
        again, texts = [], []
        broken = False
        for codes in entries:
            text = portmoves.decode_text(codes, decode)
            if text is None:
                failures.append("bank %d has a code the charmap has no character for" % bank)
                broken = True
                break
            texts.append(text)
            again.append(portmoves.encode_text(text, encode, "bank %d" % bank))
        if broken:
            continue
        if portspecies.write_bank(seed, again) != msg[bank]:
            failures.append("bank %d does not come back byte for byte through the charmap"
                            % bank)
        banks[bank] = texts
    if len(banks) != 3:
        return failures, lines
    lines.append("text: the three ability banks decode and encode back byte for byte "
                 "through the engine's charmap")

    # Gen 5 appended rather than renumbering. All 123 shared names witness it.
    agree = 0
    for a in range(FIRST_SHARED, LAST_SHARED + 1):
        if black_names[a] == banks[BANK_NAMES][a]:
            agree += 1
        else:
            failures.append("ability %d is %r on the cartridge and %r here, so the two "
                            "numberings are not the same one"
                            % (a, black_names[a], banks[BANK_NAMES][a]))
    lines.append("abilities: all %d ids both games name are spelled identically, which is "
                 "the whole of the claim that Gen 5 appended rather than renumbered" % agree)

    # The upper case bank is this game's own names upper-cased, and Black's is too.
    for a in range(FIRST_SHARED, LAST_SHARED + 1):
        if banks[BANK_NAMES][a].upper() != banks[BANK_NAMES_UPPER][a]:
            failures.append("ability %d's upper case name is %r and not %r, so the bank is "
                            "not the rule this fill carries"
                            % (a, banks[BANK_NAMES_UPPER][a], banks[BANK_NAMES][a].upper()))
    for a in range(FIRST_SHARED, LAST_PORTED + 1):
        if black_uppers[a] != black_names[a].upper():
            failures.append("the cartridge spells ability %d %r in upper case and not %r"
                            % (a, black_uppers[a], black_names[a].upper()))

    # What the fill is about to write, held to the box and to the charmap.
    unwrappable = 0
    for a in range(FIRST_PORTED, LAST_PORTED + 1):
        if len(black_names[a]) > NAME_CHARS:
            failures.append("ability %d is %r, longer than any name this game holds"
                            % (a, black_names[a]))
        if description(black_descs[a]) is None:
            unwrappable += 1
            failures.append("ability %d's description does not fit %d lines of %d: %r"
                            % (a, BOX_LINES, LINE_CHARS, black_descs[a]))
        flat = black_descs[a].replace(portmoves.GEN5_NEWLINE, " ")
        for char in black_names[a] + black_uppers[a] + flat:
            if portmoves.APOSTROPHE_FOLD.get(char, char) not in encode:
                failures.append("ability %d uses %r, which the charmap has no row for"
                                % (a, char))
    if not unwrappable:
        lines.append("abilities: every one of the %d appended names, upper case names and "
                     "descriptions goes through the charmap and fits this game's box"
                     % (LAST_PORTED + 1 - FIRST_PORTED))
    return failures, lines


# ---------------------------------------------------------------- the header

HEADER_PATH = "mods/openmmo/include/openmmo_ability_ids.h"


def render_header():
    """The client's own copy of the ability ids past this game's last."""
    import json

    path = MMO.parent / "codegen" / "gen5" / "enums.json"
    if not path.is_file():
        die("no %s; run mmo/tools/gen5_tables.py --rom <Black> first" % path)
    table = json.loads(path.read_text())["abilities"]
    ids = sorted((int(k), v) for k, v in table.items())
    if ids[0][0] != FIRST_PORTED or ids[-1][0] != LAST_PORTED:
        die("codegen/gen5 names abilities %d..%d and this header is built on %d..%d"
            % (ids[0][0], ids[-1][0], FIRST_PORTED, LAST_PORTED))
    width = max(len(n) for _, n in ids)
    out = [
        "/* Generated by tools/portabilities.py --header; Do not edit. */",
        "#ifndef OPENMMO_ABILITY_IDS_H",
        "#define OPENMMO_ABILITY_IDS_H",
        "",
        "#define OPENMMO_ABILITY_FIRST %d" % FIRST_PORTED,
        "#define OPENMMO_ABILITY_LAST  %d" % LAST_PORTED,
        "",
    ]
    for aid, name in ids:
        out.append("#define OPENMMO_ABILITY_%-*s %d" % (width, name, aid))
    out += ["", "#endif /* OPENMMO_ABILITY_IDS_H */", ""]
    return "\n".join(out)


def fill(pkg, black_names, black_uppers, black_descs, plat, engine, write):
    msg = plat.narc_members(MESSAGE_NARC)
    encode, _decode = portmoves.charmap(engine)
    for bank in (BANK_NAMES, BANK_NAMES_UPPER, BANK_DESCRIPTIONS):
        seed, entries = portspecies.read_bank(msg[bank])
        for a in range(FIRST_PORTED, LAST_PORTED + 1):
            if bank == BANK_NAMES:
                text = black_names[a]
            elif bank == BANK_NAMES_UPPER:
                text = black_uppers[a]
            else:
                text = description(black_descs[a])
                if text is None:
                    die("ability %d's description does not fit this game's box; the "
                        "check that says so should have stopped this" % a)
            entries.append(portmoves.encode_text(text, encode, "ability %d" % a))
        write(pkg, MESSAGE_NARC, bank, portspecies.write_bank(seed, entries))
    count = LAST_PORTED + 1 - FIRST_PORTED
    return ["abilities: %d names, upper case names and descriptions, three banks of %d"
            % (count, BANK_ENTRIES + count)]


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--header", action="store_true",
                    help="write the client's copy of the appended ability ids")
    ap.add_argument("--out", help="where --header writes (default %s)" % HEADER_PATH)
    args = ap.parse_args(argv)
    if not args.header:
        die("this module is run by tools/portspecies.py; --header is its own door")
    out = Path(args.out) if args.out else MMO / HEADER_PATH
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(render_header())
    print("portabilities: wrote %s" % out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
