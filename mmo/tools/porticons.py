#!/usr/bin/env python3
"""porticons.py, the box and party icons of Black's 156 species, into this game's own
archive.
"""

import argparse
import re
import struct
import subprocess
import sys
from pathlib import Path

MMO = Path(__file__).resolve().parent.parent

ICON_NARC_BLACK = "a/0/0/7"
ICON_NARC_PLATINUM = "poketool/icongra/pl_poke_icon.narc"

# The first icon member in either archive: `icon_00000_NCGR` in the engine's own
# generated .naix, and the same six animation members sit in front of it in both.
FIRST_ICON = 7

# Black leaves an empty member between icons; Platinum does not.
BLACK_STRIDE = 2
PLATINUM_STRIDE = 1

ICON_BYTES = 1072

# Where the colours start in an RLCN: sixteen bytes of file header and twenty
# four of section header. Three palettes of sixteen 16-bit colours follow.
PALETTE_DATA = 40
ICON_PALETTES = 3

# The one header byte that differs, and what this game writes there.
HEADER_FLAG_OFFSET = 34
PLATINUM_HEADER_FLAG = 0x00
BLACK_HEADER_FLAG = 0x20

LAST_GEN4_SPECIES = 493
LAST_GEN5_SPECIES = 649

# What Platinum's archive already holds: 540 icons at 7..546, which is species
# 0..493 and then the 46 form icons. So the appended range starts after all of
# them rather than at `7 + species`, which would land on Unown's.
PLATINUM_ICON_MEMBERS = 547

# The shared icon Gen 5 redrew: Jirachi, eighteen pixel bytes different. Named
# so that a second one showing up is a finding rather than a tolerance.
REDRAWN = {385}


def die(msg):
    sys.stdout.flush()
    sys.stderr.write("porticons: %s\n" % msg)
    raise SystemExit(1)


def decomp_dir(name):
    """Whichever checkout of a decompilation this machine reads, by the one rule."""
    out = subprocess.run([str(MMO / "tools" / "decomp_dir.sh"), name],
                         capture_output=True, text=True)
    path = out.stdout.strip()
    if not path:
        die("no %s checkout on this machine" % name)
    return Path(path)


def engine_dir(explicit):
    return Path(explicit) if explicit else decomp_dir("pokeplatinum")


def load_modport(engine):
    sys.path.insert(0, str(engine / "pc"))
    try:
        import modport
    except ImportError:
        die("%s/pc has no modport.py; is that the engine tree?" % engine)
    return modport


def platinum_palette_table(engine):
    """Species -> icon palette, out of the engine's own generated header."""
    header = engine / "build/pc/geninclude/res/pokemon/species_icon_palettes.h"
    if not header.exists():
        die("%s is missing; build the engine's generated headers first" % header)
    species = {}
    running = 0
    for line in (engine / "generated/species.txt").read_text().splitlines():
        line = line.split("//")[0].strip().rstrip(",")
        if not line:
            continue
        if "=" in line:
            key, value = [part.strip() for part in line.split("=", 1)]
            running = int(value, 0) if value.lstrip("-").isdigit() else species[value]
        else:
            key = line
        species[key] = running
        running += 1
    body = header.read_text().split("sPokemonIconPaletteIndex[] = {", 1)[1]
    out = {}
    for match in re.finditer(r"\[(\w+)\]\s*=\s*(\d+)", body):
        name, value = match.group(1), int(match.group(2))
        if name in species and species[name] <= LAST_GEN4_SPECIES:
            out[species[name]] = value
    return out


def decompressed_arm9(rom_path):
    """The ARM9 as the game runs it. The SDK stores it backward-LZ compressed."""
    tree = decomp_dir("pokeblack")
    sys.path.insert(0, str(tree / "tools" / "scripts"))
    try:
        import blz
    except ImportError:
        die("%s has no tools/scripts/blz.py; the ARM9 cannot be decompressed "
            "without it" % tree)
    data = rom_path.read_bytes()
    offset, _entry, _ram, size = struct.unpack_from("<IIII", data, 0x20)
    return blz.decode(data[offset:offset + size])


def find_palette_table(arm9, expected):
    """The one place whose low two bits are Platinum's whole icon palette table."""
    ids = sorted(expected)
    first = expected[ids[0]]
    hits = []
    start = 0
    while True:
        base = arm9.find(bytes([first * 0x11]), start)
        if base < 0:
            break
        start = base + 1
        if base + ids[-1] >= len(arm9):
            continue
        if all((arm9[base + s - ids[0]] & 3) == expected[s] for s in ids):
            hits.append(base - ids[0])
    if not hits:
        die("no table in the ARM9 reproduces Platinum's %d icon palettes; "
            "this cartridge does not carry the one this port needs" % len(expected))
    if len(hits) > 1:
        die("%d tables in the ARM9 reproduce the icon palettes; "
            "the search is not specific enough to pick one" % len(hits))
    return hits[0]


def verify(black_icons, plat_icons, palettes, table, arm9):
    """Hold the two archives against each other over the range they share."""
    identical = redrawn = 0
    failures = []
    for species in range(0, LAST_GEN4_SPECIES + 1):
        bi = FIRST_ICON + BLACK_STRIDE * species
        pi = FIRST_ICON + PLATINUM_STRIDE * species
        if bi >= len(black_icons) or pi >= len(plat_icons):
            failures.append("species %d is past one of the archives" % species)
            continue
        mine, theirs = black_icons[bi], plat_icons[pi]
        if len(mine) != ICON_BYTES or len(theirs) != ICON_BYTES:
            failures.append("species %d is not a %d byte icon on both sides" % (species, ICON_BYTES))
            continue
        if mine[HEADER_FLAG_OFFSET] != BLACK_HEADER_FLAG:
            failures.append("species %d does not carry Black's header flag" % species)
            continue
        if rewrite(mine) == theirs:
            identical += 1
        elif species in REDRAWN:
            redrawn += 1
        else:
            failures.append("species %d differs beyond the header byte and is not "
                            "one of the redrawn ones" % species)
    for species, want in sorted(palettes.items()):
        got = arm9[table + species] & 3
        if got != want:
            failures.append("species %d has icon palette %d in the ARM9 and %d in the decomp"
                            % (species, got, want))
    return identical, redrawn, failures


def rewrite(member):
    """Black's icon as this game's loader wants it: one header byte."""
    out = bytearray(member)
    out[HEADER_FLAG_OFFSET] = PLATINUM_HEADER_FLAG
    return bytes(out)


def render(black_icons, arm9, table):
    rows = []
    for species in range(LAST_GEN4_SPECIES + 1, LAST_GEN5_SPECIES + 1):
        source = FIRST_ICON + BLACK_STRIDE * species
        if source >= len(black_icons) or len(black_icons[source]) != ICON_BYTES:
            die("species %d has no icon in %s" % (species, ICON_NARC_BLACK))
        rows.append((species, source,
                     PLATINUM_ICON_MEMBERS + species - (LAST_GEN4_SPECIES + 1),
                     arm9[table + species] & 3))
    head = [
        "# POKE_ICONS, GENERATED by tools/porticons.py; DO NOT EDIT.",
        "#",
        "# Where the box and party icon of a species Black adds comes from, where it",
        "# goes, and which of the three icon palettes it is drawn with.",
        "#",
        "# The two member columns are DIFFERENT ARCHIVES. `black` is a/0/0/7 on a Gen 5",
        "# cartridge, which puts species N at 7 + 2N; `plat` is this game's own",
        "# poketool/icongra/pl_poke_icon.narc, which puts species N at 7 + N and already",
        "# holds 547 members, so a ported species is APPENDED past all of them rather",
        "# than seated at 7 + N, that member is one of this game's own form icons.",
        "#",
        "# The bytes cross as they are, bar one header byte: Black writes 0x20 at offset",
        "# 34 where this game writes 0x00. 493 of the 494 icons both games have are",
        "# byte-identical after that rewrite, which is what proves both the member",
        "# arithmetic and the rewrite; the one that is not is Jirachi, redrawn.",
        "#",
        "# The palette is not the palette DATA, which is already here and byte-identical",
        "# on both sides. It is which of the three, read out of the cartridge's ARM9 and",
        "# checked against all 494 the engine's own table already answers for.",
        "#",
        "# %d species, %d..%d." % (len(rows), rows[0][0], rows[-1][0]),
        "#",
        "# Rows: <species> <black member> <plat member> <palette>",
        "",
    ]
    body = ["%4d %6d %6d %d" % row for row in rows]
    return "\n".join(head + body) + "\n", rows


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--rom", required=True, help="a Black or White NDS image")
    ap.add_argument("--engine", help="the pokeplatinum checkout to verify against")
    ap.add_argument("--host-rom", help="the built Platinum image (default: the engine's)")
    ap.add_argument("--out", help="where the table goes (default mmo/POKE_ICONS)")
    ap.add_argument("--check", action="store_true", help="verify only, write nothing")
    args = ap.parse_args(argv)

    engine = engine_dir(args.engine)
    modport = load_modport(engine)
    rom = modport.NitroRom(Path(args.rom))
    host = Path(args.host_rom) if args.host_rom else engine / "build/rom/pokeplatinum.us.nds"
    if not host.is_file():
        die("no built Platinum image at %s; pass --host-rom" % host)
    plat = modport.NitroRom(host)
    print("porticons: %s %s against %s %s" % (rom.code, rom.title, plat.code, plat.title))

    black_icons = rom.narc_members(ICON_NARC_BLACK)
    plat_icons = plat.narc_members(ICON_NARC_PLATINUM)
    if len(plat_icons) != PLATINUM_ICON_MEMBERS:
        die("%s holds %d members and this port is built on %d"
            % (ICON_NARC_PLATINUM, len(plat_icons), PLATINUM_ICON_MEMBERS))
    # Not the whole member: this game keeps sixteen palettes in it and the
    # cartridge keeps three. What has to hold is that its three are this game's
    # first three, colour for colour, because that is what lets a ported icon be
    # drawn with a palette that is already here.
    span = slice(PALETTE_DATA, PALETTE_DATA + ICON_PALETTES * 32)
    if black_icons[0][span] != plat_icons[0][span]:
        die("the cartridge's three icon palettes are not this game's first three; "
            "the colours would have to cross too, and this port does not carry them")

    palettes = platinum_palette_table(engine)
    arm9 = decompressed_arm9(Path(args.rom))
    table = find_palette_table(arm9, palettes)
    identical, redrawn, failures = verify(black_icons, plat_icons, palettes, table, arm9)
    print("  icon palettes: one table at ARM9 offset 0x%x reproduces all %d"
          % (table, len(palettes)))
    print("  shared icons: %d byte-identical after the header rewrite, %d redrawn"
          % (identical, redrawn))
    if failures:
        print("  %d disagreement(s):" % len(failures))
        for line in failures[:10]:
            print("    " + line)
        die("the shared range does not hold; nothing written")
    print("  every shared icon and every shared palette agrees")

    text, rows = render(black_icons, arm9, table)
    if args.check:
        return 0
    out = Path(args.out) if args.out else MMO / "POKE_ICONS"
    out.write_text(text)
    print("  wrote %d rows to %s" % (len(rows), out))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
