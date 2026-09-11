#!/usr/bin/env python3
"""portspecies.py, fill a package with the 156 species a Gen 5 cartridge adds."""

import argparse
import struct
import sys
from pathlib import Path

MMO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(MMO / "tools"))

import gen5_tables  # noqa: E402  (same directory: it owns the cartridge's own tables)
import porticons  # noqa: E402  (and it owns the icon half)
import portsprites  # noqa: E402  (and it owns the icon half)
import portmoves  # noqa: E402  (the 92 moves: table, text, animation, stub)
import portabilities  # noqa: E402  (and the 41 abilities they and the species name)

PERSONAL_NARC = "poketool/personal/pl_personal.narc"
PERSONAL_NARC_BLACK = gen5_tables.PERSONAL_NARC

# The learnset archive pokemon.c indexes by species carries nothing a ported
# species needs, the server owns a monster's moveset and
# openmmo_seat_build_mon overwrites the moves the engine would have chosen,
# but it is read on every seat, so an id past its end is a read off the end
# rather than a missing feature.
LEARNSET_NARC = "poketool/personal/wotbl.narc"
EMPTY_LEARNSET = b"\xff\xff\xff\xff"

# The EVOLUTION archive is carried, since 2026-09-06.
EVOLUTION_NARC = "poketool/personal/evo.narc"
EVOLUTION_NARC_BLACK = gen5_tables.EVOLUTION_NARC
# `MAX_EVOLUTIONS` and `sizeof(SpeciesEvolution)` in the engine's own
# include/struct_defs/species.h. Gen 5 stores the same seven rows in 42 bytes and
# this game pads the member to 44; the two spare bytes are zero in every one of
# this game's 508, which the rebuild below proves rather than assumes.
EVOLUTION_ROWS = 7
EVOLUTION_ROW_SIZE = 6
G4_EVO_SIZE = 44
ICON_NARC = porticons.ICON_NARC_PLATINUM
MESSAGE_NARC = "msgdata/pl_msg.narc"

# TEXT_BANK_SPECIES_NAME in the engine's own generated/text_banks.h. One of the
# thirty-nine banks the ROM build assembles out of other resources, so it exists
# in the built image and in no source tree; mmo/tools/gen_idmap_text.py says the
# same thing from the other side.
SPECIES_NAME_BANK = 412
# The bank beside it: the same names with an English article and a formatting
# tag round them ("a BULBASAUR", "an EKANS"), one entry a species, read by the
# script commands that name a species by id. Grown from the grown name bank.
ARTICLES_BANK = 413

# What each archive holds before a fill, and so where the appended run starts.
# Checked against the images rather than trusted: an archive that grew would put
# every appended member one place out.
# All three species archives hold the same 508, so one appended base serves them.
PERSONAL_MEMBERS = 508
ICON_MEMBERS = porticons.PLATINUM_ICON_MEMBERS
NAME_ENTRIES = 496

# Gen 5's personal entry, by the offsets tools/gen5_tables.py proved.
G5_TYPE1, G5_TYPE2 = 6, 7
G5_CATCH = 8
G5_EV = 10
G5_ITEM1, G5_ITEM2, G5_ITEM3 = 12, 14, 16
G5_GENDER, G5_HATCH, G5_FRIEND, G5_RATE = 18, 19, 20, 21
G5_EGG1, G5_EGG2 = 22, 23
G5_ABILITY1, G5_ABILITY2 = 24, 25
G5_COLOR = 33
G5_BASE_EXP = 34
G5_FLIP_BIT = 0x40
G5_COLOR_MASK = 0x3F

# Gen 4's SpeciesData, from include/struct_defs/species.h. 44 bytes, and the
# colour byte carries the flip in bit 7 where Gen 5 carries it in bit 6.
G4_SIZE = 44
G4_FLIP_BIT = 0x80

# The machine compatibility bitfields, 0x1C..0x2B here and 0x28..0x34 in Gen 5.
# Gen 4 spends 100 of its 128 bits (TM01..TM92 then HM01..HM08) and Gen 5 spends
# 101 of its 104 (TM01..TM95 then HM01..HM06); which bit is which machine is
# gen5_tables.machine_bit, and it is not the order the cartridge's own table is
# in. Both games number a machine's bit by its number.
G4_MACHINES, G5_MACHINES = 0x1C, 0x28
G4_MACHINE_COUNT = 100
G4_TM_COUNT, G4_HM_COUNT = 92, 8

# The EV yield word is six two-bit fields and nothing else. Gen 5 sets a bit
# above them on two species (Diglett and Dugtrio); it is not an EV yield and is
# masked off rather than carried into a field Gen 4 does not have.
EV_MASK = 0x0FFF

MAX_G4_BASE_EXP = 255

LAST_GEN4_SPECIES = porticons.LAST_GEN4_SPECIES
LAST_GEN5_SPECIES = porticons.LAST_GEN5_SPECIES

# The Gen 4 message file's two keys, both the game's own.
MSG_TABLE_KEY = 0x2FD
MSG_STRING_KEY = 0x91BD3
MSG_STRING_STEP = 0x493D
MSG_TERMINATOR = 0xFFFF

# The bytes this fill does not carry, so comparing them would only be checking
# that a constant is a constant. 0x18 is the Great Marsh flee rate and 0x1C..0x2B
# are the TM and HM flags; both are written as zero, and the header says why.
DROPPED = {0x18} | set(range(0x1C, G4_SIZE))

# Of what is carried, the bytes Gen 5 changed the value of, with how many of the
# 493 shared species differ there. A count that moves is a decode that drifted
# rather than one more generation difference, so these are exact and a gap that
# closes fails here until it is written down as closed.
EXPECTED_DIFFS = {
    0x09: 477,  # base experience, rebalanced across almost the whole dex
    0x0C: 5, 0x0D: 1, 0x0E: 8, 0x0F: 5,  # held items Gen 5 changed
}


def die(msg):
    sys.stdout.flush()
    sys.stderr.write("portspecies: %s\n" % msg)
    raise SystemExit(1)


# ------------------------------------------------------------------- personal

def machine_pairs(engine, rom_path):
    """(this game's machine bit, the cartridge's) for every machine both teach."""
    moves = read_move_ids(engine)
    table = gen5_tables.machine_moves(rom_path)
    at = {move: index for index, move in enumerate(table)}
    pairs, unknown = [], []
    for i, name in enumerate(machine_files()):
        teaches = read_item_move(engine, name, moves)
        if teaches in at:
            pairs.append((i, gen5_tables.machine_bit(at[teaches])))
        else:
            unknown.append((name.upper(), teaches))
    return pairs, unknown


def machine_files():
    """This game's machines in bit order: TM01..TM92, then HM01..HM08."""
    return (["tm%02d" % i for i in range(1, G4_TM_COUNT + 1)]
            + ["hm%02d" % i for i in range(1, G4_HM_COUNT + 1)])


def read_move_ids(engine):
    """MOVE_* constant -> id, in the engine's own generated order."""
    out = {}
    path = Path(engine) / "generated" / "moves.txt"
    if not path.is_file():
        die("no %s to read this game's move numbering from" % path)
    for line in path.read_text().splitlines():
        name = line.strip()
        if name.startswith("MOVE_"):
            out[name] = len(out)
    return out


def read_item_move(engine, item, moves):
    path = Path(engine) / "res" / "items" / "data" / (item + ".json")
    if not path.is_file():
        die("no %s; this game's machine list is read off its own item data" % path)
    import json
    teaches = json.loads(path.read_text()).get("teachesMove")
    if teaches not in moves:
        die("%s teaches %r, which is not a move this game numbers" % (item, teaches))
    return moves[teaches]


def bit_of(entry, base, index):
    return (entry[base + index // 8] >> (index % 8)) & 1


def machine_mask(gen5, pairs):
    """This game's 16 machine bytes for one Gen 5 entry, or zeros with no pairs."""
    out = bytearray(G4_SIZE - G4_MACHINES)
    for mine, theirs in pairs:
        if bit_of(gen5, G5_MACHINES, theirs):
            out[mine // 8] |= 1 << (mine % 8)
    return bytes(out)


def synthesise(gen5, machines=()):
    """One Gen 4 SpeciesData, built from a Gen 5 personal entry."""
    out = bytearray(G4_SIZE)
    out[0:6] = gen5[0:6]
    out[6] = gen5_tables.server_type(gen5[G5_TYPE1])
    out[7] = gen5_tables.server_type(gen5[G5_TYPE2])
    out[8] = gen5[G5_CATCH]
    out[9] = min(struct.unpack_from("<H", gen5, G5_BASE_EXP)[0], MAX_G4_BASE_EXP)
    struct.pack_into("<H", out, 10, struct.unpack_from("<H", gen5, G5_EV)[0] & EV_MASK)
    struct.pack_into("<H", out, 12, struct.unpack_from("<H", gen5, G5_ITEM1)[0])
    rare = (struct.unpack_from("<H", gen5, G5_ITEM2)[0]
            or struct.unpack_from("<H", gen5, G5_ITEM3)[0])
    struct.pack_into("<H", out, 14, rare)
    out[16] = gen5[G5_GENDER]
    out[17] = gen5[G5_HATCH]
    out[18] = gen5[G5_FRIEND]
    out[19] = gen5[G5_RATE]
    out[20] = gen5[G5_EGG1]
    out[21] = gen5[G5_EGG2]
    out[22] = gen5[G5_ABILITY1]
    out[23] = gen5[G5_ABILITY2]
    # 0x18, the Great Marsh flee rate, stays zero; see the header.
    out[25] = (gen5[G5_COLOR] & G5_COLOR_MASK) | (
        G4_FLIP_BIT if gen5[G5_COLOR] & G5_FLIP_BIT else 0)
    out[G4_MACHINES:] = machine_mask(gen5, machines)
    return bytes(out)


def check_personal(gen5, gen4):
    """Build every shared species and hold it against this game's own archive."""
    seen = {}
    failures = []
    for species in range(1, LAST_GEN4_SPECIES + 1):
        if species >= len(gen5) or species >= len(gen4):
            failures.append("species %d is past one of the personal archives" % species)
            continue
        mine, theirs = synthesise(gen5[species]), gen4[species]
        if len(theirs) != G4_SIZE:
            failures.append("species %d is %d bytes in this game's archive, not %d"
                            % (species, len(theirs), G4_SIZE))
            continue
        for i in range(G4_SIZE):
            if i in DROPPED:
                continue
            if mine[i] != theirs[i]:
                seen[i] = seen.get(i, 0) + 1
    for offset, count in sorted(seen.items()):
        want = EXPECTED_DIFFS.get(offset)
        if want is None:
            failures.append("byte 0x%02x of the personal entry differs on %d species and "
                            "nothing says it should" % (offset, count))
        elif want != count:
            failures.append("byte 0x%02x differs on %d species and %d were measured"
                            % (offset, count, want))
    for offset in EXPECTED_DIFFS:
        if offset not in seen:
            failures.append("byte 0x%02x was measured as differing and no longer does; "
                            "a gap that closes has to be written down as closed" % offset)
    return failures


def check_machines(gen5, gen4, pairs, engine):
    """Hold every machine both games teach to the 493 species both games hold."""
    failures = []
    total = agree_total = perfect = 0
    worst = []
    for mine, theirs in pairs:
        agree = sum(1 for s in range(1, LAST_GEN4_SPECIES + 1)
                    if bit_of(gen4[s], G4_MACHINES, mine)
                    == bit_of(gen5[s], G5_MACHINES, theirs))
        total += 1
        agree_total += agree
        perfect += agree == LAST_GEN4_SPECIES
        worst.append((agree, mine))
        if agree < LAST_GEN4_SPECIES * MACHINE_FLOOR // 100:
            failures.append("machine bit %d agrees with the cartridge's %d on only %d of "
                            "the %d shared species; the bit order has moved"
                            % (mine, theirs, agree, LAST_GEN4_SPECIES))
    if not total:
        failures.append("no machine is taught by both games, which cannot be right")
        return failures, []
    mean = 100.0 * agree_total / total / LAST_GEN4_SPECIES
    if mean < MACHINE_MEAN_FLOOR:
        failures.append("the machines agree with the cartridge on %.2f%% of the shared "
                        "species and %.2f%% was measured" % (mean, MACHINE_MEAN_FLOOR))
    worst.sort()
    lines = ["machines: %d of this game's %d are taught by both games and agree on %.2f%% of "
             "the shared species, %d of them perfectly"
             % (total, G4_MACHINE_COUNT, mean, perfect)]
    return failures, lines


# The floors the measurement of 2026-09-06 justifies, and nothing more: every
# shared column was at or above 95%, the mean was 99.79%, and a wrong bit order
# put six of them between 43% and 75%. A column that falls through the floor is
# a decode that moved rather than a generation that changed its mind.
MACHINE_FLOOR = 95
MACHINE_MEAN_FLOOR = 99.0


# ---------------------------------------------------------------- evolutions

def engine_species(species):
    """A wire species as the ENGINE numbers it."""
    if species == LAST_GEN4_SPECIES + 1:      # MMO_PORTED_EGG_ID
        return 650                            # MMO_PORTED_VICTINI_ENGINE_ID
    if species == LAST_GEN4_SPECIES + 2:      # MMO_PORTED_BAD_EGG_ID
        return 651                            # MMO_PORTED_SNIVY_ENGINE_ID
    return species


def evolution_member(gen5):
    """One Gen 4 evolution member, built from a Gen 5 one."""
    out = bytearray(G4_EVO_SIZE)
    written = 0
    for i in range(EVOLUTION_ROWS):
        method, param, target = struct.unpack_from("<HHH", gen5, i * EVOLUTION_ROW_SIZE)
        if not method:
            continue
        mapped = gen5_tables.server_method(method)
        if mapped is None:
            continue
        struct.pack_into("<HHH", out, written * EVOLUTION_ROW_SIZE,
                         mapped, param, engine_species(target))
        written += 1
    return bytes(out)


def check_evolutions(gen5, gen4):
    """Rebuild every shared species' evolutions and hold them against this game's."""
    failures = []
    changed = []
    for species in range(1, LAST_GEN4_SPECIES + 1):
        if species >= len(gen5) or species >= len(gen4):
            failures.append("species %d is past one of the evolution archives" % species)
            continue
        theirs = bytes(gen4[species])
        if len(theirs) != G4_EVO_SIZE:
            failures.append("species %d is %d bytes in this game's evolution archive, "
                            "not %d" % (species, len(theirs), G4_EVO_SIZE))
            continue
        if evolution_member(gen5[species]) != theirs:
            changed.append(species)
    want = sorted(EVOLUTION_ROWS_ADDED)
    if changed != want:
        failures.append("the shared range rebuilds every evolution member but %s, and "
                        "%s were measured" % (changed or "none", want))
    return failures


# The shared species whose evolutions Gen 5 CHANGED, so their members cannot come
# back byte for byte and their disagreement is the measurement rather than a
# fault. Feebas gained the Prism Scale trade beside its beauty evolution; nothing
# else in the 493 moved. gen5_tables.EVOLUTION_ROWS_CHANGED names the same one by
# its decomp constant, and these two have to stay in step.
EVOLUTION_ROWS_ADDED = {349}


# ----------------------------------------------------------------- name bank

def read_bank(blob):
    """The Gen 4 message file: an XORed entry table and per-entry XORed text."""
    count, seed = struct.unpack_from("<HH", blob, 0)
    key = (MSG_TABLE_KEY * seed) & 0xFFFF
    out = []
    for i in range(count):
        pair = (key * (i + 1)) & 0xFFFF
        mask = pair | (pair << 16)
        offset, length = struct.unpack_from("<II", blob, 4 + i * 8)
        offset ^= mask
        length ^= mask
        if offset + length * 2 > len(blob):
            die("message %d of the species name bank runs past its end" % i)
        text = struct.unpack_from("<%dH" % length, blob, offset)
        char_key = (MSG_STRING_KEY * (i + 1)) & 0xFFFF
        chars = []
        for char in text:
            chars.append(char ^ char_key)
            char_key = (char_key + MSG_STRING_STEP) & 0xFFFF
        out.append(chars)
    return seed, out


def write_bank(seed, messages):
    """The same file back. Checked by rewriting one unchanged, byte for byte."""
    out = bytearray(4 + len(messages) * 8)
    struct.pack_into("<HH", out, 0, len(messages), seed)
    key = (MSG_TABLE_KEY * seed) & 0xFFFF
    for i, chars in enumerate(messages):
        pair = (key * (i + 1)) & 0xFFFF
        mask = pair | (pair << 16)
        struct.pack_into("<II", out, 4 + i * 8, len(out) ^ mask, len(chars) ^ mask)
        char_key = (MSG_STRING_KEY * (i + 1)) & 0xFFFF
        for char in chars:
            out += struct.pack("<H", char ^ char_key)
            char_key = (char_key + MSG_STRING_STEP) & 0xFFFF
    return bytes(out)


def letter_map(messages, decomp_names):
    """character -> charcode, read off the names the bank already holds."""
    table = {}
    for species, name in sorted(decomp_names.items()):
        if species >= len(messages):
            continue
        chars = messages[species]
        if not chars or chars[-1] != MSG_TERMINATOR:
            die("message %d of the species name bank has no terminator" % species)
        body = chars[:-1]
        if len(body) != len(name):
            die("species %d is %r in the decomp and %d characters in the bank"
                % (species, name, len(body)))
        for letter, code in zip(name, body):
            if table.setdefault(letter, code) != code:
                die("%r is charcode %d in one name and %d in another"
                    % (letter, table[letter], code))
    return table


def encode_name(name, table):
    chars = []
    for letter in name:
        code = table.get(letter)
        if code is None:
            die("%r in %r is a character no shared species name uses, so this "
                "cannot say what the engine would draw for it" % (letter, name))
        chars.append(code)
    chars.append(MSG_TERMINATOR)
    return chars


# --------------------------------------------------------------------- fill

def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--rom", help="a Black or White NDS image")
    ap.add_argument("--header", action="store_true",
                    help="write mmo/src/species_port.gen.h and stop")
    ap.add_argument("--engine", help="the pokeplatinum checkout to verify against")
    ap.add_argument("--host-rom", help="the built Platinum image")
    ap.add_argument("--package", help="the package to fill (default mmo/mods/imports)")
    ap.add_argument("--out", help="where --header writes (default mmo/src/species_port.gen.h)")
    ap.add_argument("--check", action="store_true", help="verify only, write nothing")
    args = ap.parse_args(argv)

    if args.header:
        out = Path(args.out) if args.out else MMO / "src" / "species_port.gen.h"
        machines = None
        if args.rom:
            # The pairing wants both sides: the engine's item data for ours,
            # the cartridge's ARM9 table for theirs. Without a --rom the rest
            # of the header is still written, so a checkout with no cartridge
            # in front of it can still regenerate what it can.
            engine = porticons.engine_dir(args.engine)
            pairs, unknown = machine_pairs(engine, Path(args.rom))
            machines = render_machines(pairs, unknown)
        out.write_text(render_header(read_icon_table(), machines))
        print("portspecies: wrote %s%s" % (out, "" if machines else
                                           " (no --rom: no machine pairing)"))
        return 0
    if not args.rom:
        die("need --rom (a Gen 5 NDS image), or --header to write the client's copy")

    engine = porticons.engine_dir(args.engine)
    modport = porticons.load_modport(engine)
    rom = modport.NitroRom(Path(args.rom))
    host = Path(args.host_rom) if args.host_rom else engine / "build/rom/pokeplatinum.us.nds"
    if not host.is_file():
        die("no built Platinum image at %s; pass --host-rom" % host)
    plat = modport.NitroRom(host)
    print("portspecies: %s %s into %s %s" % (rom.code, rom.title, plat.code, plat.title))

    gen5 = rom.narc_members(PERSONAL_NARC_BLACK)
    gen4 = plat.narc_members(PERSONAL_NARC)
    learnsets = plat.narc_members(LEARNSET_NARC)
    evolutions = plat.narc_members(EVOLUTION_NARC)
    black_evolutions = rom.narc_members(EVOLUTION_NARC_BLACK)
    black_icons = rom.narc_members(porticons.ICON_NARC_BLACK)
    plat_icons = plat.narc_members(ICON_NARC)
    messages_narc = plat.narc_members(MESSAGE_NARC)
    black_sprites = rom.narc_members(portsprites.SPRITE_NARC_BLACK)
    move_rows, move_names, move_descs = portmoves.black_moves(rom)
    ability_names, ability_uppers, ability_descs = portabilities.black_abilities(rom)
    anims = portmoves.read_anims(MMO / "MOVE_ANIMS")
    plat_pokegra = plat.narc_members(portsprites.POKEGRA_NARC)
    plat_height = plat.narc_members(portsprites.HEIGHT_NARC)

    for path, got, want in ((PERSONAL_NARC, len(gen4), PERSONAL_MEMBERS),
                            (LEARNSET_NARC, len(learnsets), PERSONAL_MEMBERS),
                            (EVOLUTION_NARC, len(evolutions), PERSONAL_MEMBERS),
                            (ICON_NARC, len(plat_icons), ICON_MEMBERS)):
        if got != want:
            die("%s holds %d members and this fill is built on %d; every appended "
                "member would be in the wrong place" % (path, got, want))

    failures = check_personal(gen5, gen4)
    print("  personal: every shared species rebuilds this game's own entry, bar "
          "%d fields Gen 5 changed and %d this fill drops"
          % (len(EXPECTED_DIFFS), len(DROPPED)))

    machines, unknown_machines = machine_pairs(engine, Path(args.rom))
    machine_failures, machine_lines = check_machines(gen5, gen4, machines, engine)
    failures += machine_failures
    for line in machine_lines:
        print("  " + line)

    failures += check_evolutions(black_evolutions, evolutions)
    print("  evolutions: %d of %d shared species rebuild this game's own member byte "
          "for byte; the rest are the rows Gen 5 added"
          % (LAST_GEN4_SPECIES - len(EVOLUTION_ROWS_ADDED), LAST_GEN4_SPECIES))

    sprite_failures, sprite_lines = portsprites.check(black_sprites, plat_pokegra, plat_height)
    failures += sprite_failures
    for line in sprite_lines:
        print("  " + line)
    move_failures, move_lines = portmoves.check(move_rows, move_names, move_descs, plat, engine, anims)
    failures += move_failures
    for line in move_lines:
        print("  " + line)
    ability_failures, ability_lines = portabilities.check(
        ability_names, ability_uppers, ability_descs, plat, engine)
    failures += ability_failures
    for line in ability_lines:
        print("  " + line)

    seed, messages = read_bank(messages_narc[SPECIES_NAME_BANK])
    if len(messages) != NAME_ENTRIES:
        failures.append("the species name bank holds %d entries and this fill is "
                        "built on %d" % (len(messages), NAME_ENTRIES))
    if write_bank(seed, messages) != messages_narc[SPECIES_NAME_BANK]:
        failures.append("rewriting the species name bank unchanged does not give the "
                        "same bytes, so this cannot be trusted to write a longer one")
    else:
        print("  names: the bank rewrites unchanged, byte for byte (%d entries)"
              % len(messages))

    icon_rows = read_icon_table()
    names = gen5_tables.read_names(rom, gen5_tables.TEXT_SPECIES_NAMES,
                                   LAST_GEN5_SPECIES + 1, "species names")
    decomp = decomp_species_names(engine)
    table = letter_map(messages, decomp)
    print("  names: %d characters, every one witnessed by a species both games name"
          % len(table))

    if failures:
        print("  %d disagreement(s):" % len(failures))
        for line in failures[:10]:
            print("    " + line)
        die("the shared range does not hold; nothing written")
    print("  every check holds")
    if args.check:
        return 0

    pkg = Path(args.package) if args.package else MMO / "mods" / "imports"
    # Upper case, because the 494 names the bank already holds are: Gen 4 wrote
    # them that way and Gen 5 switched to title case. One bank gets one
    # convention, and it is the one the 494 already there are in. The server's
    # own table folds the same way, for the same reason.
    grown = messages + [encode_name(names[s].upper(), table)
                        for s in range(LAST_GEN4_SPECIES + 1, LAST_GEN5_SPECIES + 1)]
    write_member(pkg, MESSAGE_NARC, SPECIES_NAME_BANK, write_bank(seed, grown))
    write_member(pkg, MESSAGE_NARC, ARTICLES_BANK,
                 write_bank(*grow_articles(messages_narc[ARTICLES_BANK], grown)))
    for species in range(LAST_GEN4_SPECIES + 1, LAST_GEN5_SPECIES + 1):
        i = species - (LAST_GEN4_SPECIES + 1)
        write_member(pkg, PERSONAL_NARC, PERSONAL_MEMBERS + i,
                     synthesise(gen5[species], machines))
        write_member(pkg, LEARNSET_NARC, PERSONAL_MEMBERS + i, EMPTY_LEARNSET)
        write_member(pkg, EVOLUTION_NARC, PERSONAL_MEMBERS + i,
                     evolution_member(black_evolutions[species]))
        source = icon_rows[species][0]
        write_member(pkg, ICON_NARC, ICON_MEMBERS + i,
                     porticons.rewrite(black_icons[source]))
    for line in portsprites.fill(pkg, black_sprites, write_member):
        print("  " + line)
    for line in portmoves.fill(pkg, move_rows, move_names, move_descs, plat, engine, anims, write_member):
        print("  " + line)
    for line in portabilities.fill(pkg, ability_names, ability_uppers, ability_descs,
                                   plat, engine, write_member):
        print("  " + line)
    if unknown_machines:
        print("  %d of this game's %d machines teach a move Gen 5 has none for, so "
              "whether a ported species could learn them is a fact no cartridge "
              "states and every one is zero: %s"
              % (len(unknown_machines), G4_MACHINE_COUNT,
                 ", ".join(n for n, _ in unknown_machines)))
    count = LAST_GEN5_SPECIES - LAST_GEN4_SPECIES
    evolving = sum(1 for s in range(LAST_GEN4_SPECIES + 1, LAST_GEN5_SPECIES + 1)
                   if evolution_member(black_evolutions[s]) != bytes(G4_EVO_SIZE))
    print("  filled %s: %d personal, %d learnset, %d evolution members (%d of them "
          "with somewhere to go), %d icon members and a %d entry name bank"
          % (pkg, count, count, count, evolving, count, len(grown)))
    return 0


def render_machines(pairs, unknown):
    """The client's copy of the machine pairing, for the C fill."""
    out = [
        "",
        "/* This game's 100 machine bits against the cartridge's, matched by the",
        " * move each teaches. -1 is one of ours the cartridge has no machine for,",
        " * and its bit stays zero in a synthesised entry, there is nothing on a",
        " * Black cartridge to read it from. Indexed by OUR bit: TM01..TM92 at",
        " * 0..91, then HM01..HM08 at 92..99. */",
        "#define MMO_PORTED_MACHINES       %d" % (G4_TM_COUNT + G4_HM_COUNT),
        "static const signed char MMO_PORTED_MACHINE_BIT[MMO_PORTED_MACHINES] = {",
    ]
    at = dict(pairs)
    line = "   "
    for mine in range(G4_TM_COUNT + G4_HM_COUNT):
        piece = " %d," % at.get(mine, -1)
        if len(line) + len(piece) > 78:
            out.append(line)
            line = "   "
        line += piece
    out.append(line)
    out.append("};")
    if unknown:
        out.append("")
        out.append("/* Ours with no counterpart there, and the move each teaches:")
        line = " *  "
        for name, move in unknown:
            piece = " %s(%d)" % (name, move)
            if len(line) + len(piece) > 76:
                out.append(line)
                line = " *  "
            line += piece
        out.append(line + " */")
    out.append("")
    return "\n".join(out)


def render_header(rows, machines=None):
    """The client's copy of where the fill put things."""
    first, last = LAST_GEN4_SPECIES + 1, LAST_GEN5_SPECIES
    missing = [s for s in range(first, last + 1) if s not in rows]
    if missing:
        die("mmo/POKE_ICONS has no row for %d species, first %d"
            % (len(missing), missing[0]))
    out = [
        "/* Generated by tools/portspecies.py --header; Do not edit. */",
        "",
        "#define MMO_PORTED_FIRST          %d  /* first species a fill appends */" % first,
        "#define MMO_PORTED_LAST           %d  /* last one */" % last,
        "/* pl_personal, wotbl and evo all held 508, so one base serves all three. */",
        "#define MMO_PORTED_SPECIES_BASE   %d" % PERSONAL_MEMBERS,
        "#define MMO_PORTED_ICON_BASE      %d" % ICON_MEMBERS,
        "/* The name bank keeps its egg names at 494 and 495, so names start after them. */",
        "#define MMO_PORTED_NAME_BASE      %d" % NAME_ENTRIES,
        "",
        "/* One per species from MMO_PORTED_FIRST, in order. */",
        "static const unsigned char MMO_PORTED_ICON_PALETTE[] = {",
    ]
    line = "   "
    for species in range(first, last + 1):
        piece = " %d," % rows[species][2]
        if len(line) + len(piece) > 78:
            out.append(line)
            line = "   "
        line += piece
    out.append(line)
    out.append("};")
    out.append("")
    if machines is not None:
        out.append(machines)
    return "\n".join(out)


def write_member(pkg, narc, index, data):
    d = pkg / "narc" / narc
    d.mkdir(parents=True, exist_ok=True)
    (d / str(index)).write_bytes(data)


def grow_articles(member, names):
    """The articles bank grown to match `names` (the grown species-name bank):
    every existing entry is "a " or "an ", an opening tag, the name, a closing
    tag; a new entry is built the same way, "an" before a vowel."""
    seed, entries = read_bank(member)
    if len(entries) >= len(names):
        return seed, entries
    tag = 65534
    sample = entries[1]
    open_tag = sample[sample.index(tag):sample.index(tag) + 4]
    close_at = len(sample) - 1 - sample[::-1].index(tag)
    close_tag = sample[close_at:close_at + 4]
    letters = {code: chr(ord("A") + code - 299) for code in range(299, 325)}
    a = [c for c in entries[1][:entries[1].index(tag)]]            # "a "
    an = [c for c in entries[23][:entries[23].index(tag)]]         # "an " (EKANS)
    grown = list(entries)
    for i in range(len(entries), len(names)):
        name = [c for c in names[i] if c != 65535]
        first = letters.get(name[0], "") if name else ""
        article = an if first in "AEIOU" else a
        grown.append(article + list(open_tag) + name + list(close_tag) + [65535])
    return seed, grown


def read_icon_table():
    """mmo/POKE_ICONS, which tools/porticons.py wrote and checks."""
    path = MMO / "POKE_ICONS"
    if not path.is_file():
        die("no icon table at %s; run tools/porticons.py first" % path)
    rows = {}
    for line in path.read_text().splitlines():
        line = line.split("#")[0].strip()
        if not line:
            continue
        species, source, dest, palette = [int(p) for p in line.split()]
        rows[species] = (source, dest, palette)
    return rows


def decomp_species_names(engine):
    """Species -> the name this game spells it with, out of the decomp."""
    import json
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
    out = {}
    for constant, dex in species.items():
        if not 1 <= dex <= LAST_GEN4_SPECIES:
            continue
        path = (engine / "res/pokemon"
                / constant.removeprefix("SPECIES_").lower() / "data.json")
        if path.exists():
            out[dex] = json.loads(path.read_text())["pokedex_data"]["en"]["name"]
    return out


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
