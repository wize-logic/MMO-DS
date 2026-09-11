#!/usr/bin/env python3
"""gen5_tables.py, the Gen 5 half of the species and move tables, read out of a Black
cartridge.
"""

import argparse
import json
import struct
import subprocess
import sys
from pathlib import Path

MMO = Path(__file__).resolve().parent.parent
REPO = MMO.parent

# The archives, by the Nitro path the image itself gives them.
PERSONAL_NARC = "a/0/1/6"
MOVES_NARC = "a/0/2/1"
LEARNSET_NARC = "a/0/1/8"
EVOLUTION_NARC = "a/0/1/9"
TEXT_NARC = "a/0/0/2"

# Members of the text archive. Each holds one block of one string per id.
TEXT_SPECIES_NAMES = 70
TEXT_MOVE_NAMES = 203
# Gen 5 breaks a line with U+FFFE, one below the terminator; portmoves.py calls
# the same character GEN5_NEWLINE.
GEN5_NEWLINE = "\ufffe"

# The machine table is in the ARM9, not in the item entries.
MACHINES_AT, MACHINES_BYTES = 0x28, 13

TM_TABLE_ENTRIES = 101
TM_COUNT = 95
HM_COUNT = 6

TEXT_ABILITY_NAMES = 182
# Beside it, found by sweeping the archive for the members holding 165 entries
# and reading them: 183 is the descriptions and 285 the upper case names.
# tools/portabilities.py carries all three into this game's own banks.
TEXT_ABILITY_DESCRIPTIONS = 183

# Where Platinum stops and Black carries on. Both bounds are inclusive.
LAST_GEN4_SPECIES = 493
LAST_GEN5_SPECIES = 649
LAST_GEN4_MOVE = 467
LAST_GEN5_MOVE = 559
LAST_GEN4_ABILITY = 123
LAST_GEN5_ABILITY = 164

# The wire block every server table names an item in: shared item 1 is 5001.
ITEM_REGION_BLOCK = 5000

# Gen 4's ??? type sits at 9 and Gen 5 has no such type, so a Gen 5 type id from
# Fire (9 there) on is one below the server's.
GEN4_MYSTERY_TYPE = 9

# Gen 5 inserted its trade-for-a-species method at 7. Below that the two agree;
# from 8 on a Gen 5 method is one above the server's.
GEN5_TRADE_WITH_SPECIES = 7

# Gen 5's own range ids against the range name Platinum gives the same move.
# Read off the 467 shared moves; every value below is witnessed by at least one.
RANGE_NAMES = {
    0: "RANGE_SINGLE_TARGET",
    1: "RANGE_USER_OR_ALLY",
    2: "RANGE_ALLY",
    3: "RANGE_SINGLE_TARGET_ME_FIRST",
    4: "RANGE_ALL_ADJACENT",
    5: "RANGE_ADJACENT_OPPONENTS",
    6: "RANGE_USER_SIDE",
    7: "RANGE_USER",
    8: "RANGE_FIELD",
    9: "RANGE_RANDOM_OPPONENT",
    10: "RANGE_FIELD",
    11: "RANGE_OPPONENT_SIDE",
    12: "RANGE_USER_SIDE",
    13: "RANGE_SINGLE_TARGET_SPECIAL",
}

# The move generator's own table, kept here so this tool emits the target the
# server enum names rather than a range name the server has never heard of.
TARGET_BY_RANGE = {
    "RANGE_SINGLE_TARGET": "SELECTED",
    "RANGE_SINGLE_TARGET_SPECIAL": "DEPENDS",
    "RANGE_RANDOM_OPPONENT": "RANDOM",
    "RANGE_ADJACENT_OPPONENTS": "BOTH",
    "RANGE_ALL_ADJACENT": "FOES_AND_ALLY",
    "RANGE_USER": "USER",
    "RANGE_USER_SIDE": "USER",
    "RANGE_FIELD": "USER",
    "RANGE_ALLY": "USER",
    "RANGE_OPPONENT_SIDE": "OPPONENTS_FIELD",
    "RANGE_USER_OR_ALLY": "USER_OR_ALLY",
    "RANGE_SINGLE_TARGET_ME_FIRST": "SELECTED_ME_FIRST",
}

# The five Gen 4 move flags that have a Gen 5 bit, and which bit. King's rock,
# and the two flags that only say how the DS draws a move, have none: Gen 5
# stopped flagging king's rock per move and works it out from the effect.
FLAG_BITS = {
    0: "MAKES_CONTACT",
    3: "PROTECT_AFFECTED",
    4: "MAGIC_COAT_AFFECTED",
    5: "SNATCH_AFFECTED",
    6: "MIRROR_MOVE_AFFECTED",
}

# The shared moves whose range Gen 5 changed. Named so that a decode that has
# drifted fails here instead of being read as one more generation difference.
RANGE_CHANGED = {
    "MOVE_METRONOME", "MOVE_MIRROR_MOVE", "MOVE_POISON_GAS", "MOVE_CURSE",
    "MOVE_CONVERSION_2", "MOVE_SLEEP_TALK", "MOVE_ASSIST", "MOVE_MAGIC_COAT",
    "MOVE_SNATCH", "MOVE_COPYCAT",
}

# The shared moves whose flags Gen 5 changed, per flag. Gen 5 made the entry
# hazards and the mood setters reflectable, and a dozen more moves snatchable.
FLAG_CHANGED = {
    "MAGIC_COAT_AFFECTED": {
        "MOVE_WHIRLWIND", "MOVE_ROAR", "MOVE_DISABLE", "MOVE_SPITE",
        "MOVE_SPIKES", "MOVE_FORESIGHT", "MOVE_ENCORE", "MOVE_TORMENT",
        "MOVE_TAUNT", "MOVE_ODOR_SLEUTH", "MOVE_MIRACLE_EYE", "MOVE_EMBARGO",
        "MOVE_HEAL_BLOCK", "MOVE_TOXIC_SPIKES", "MOVE_DEFOG",
        "MOVE_STEALTH_ROCK",
    },
    "PROTECT_AFFECTED": {
        "MOVE_COUNTER", "MOVE_MIRROR_COAT", "MOVE_HAIL", "MOVE_METAL_BURST",
    },
    "SNATCH_AFFECTED": {
        "MOVE_CONVERSION", "MOVE_PSYCH_UP", "MOVE_WISH", "MOVE_RECYCLE",
        "MOVE_IMPRISON", "MOVE_HEALING_WISH", "MOVE_ACUPRESSURE",
        "MOVE_POWER_TRICK", "MOVE_LUCKY_CHANT", "MOVE_AQUA_RING",
        "MOVE_MAGNET_RISE", "MOVE_LUNAR_DANCE",
    },
    "MAKES_CONTACT": set(),
    "MIRROR_MOVE_AFFECTED": set(),
}

# The two shared moves Gen 5 gave a different battle effect.
EFFECT_CHANGED = {"MOVE_GROWTH", "MOVE_TAIL_GLOW"}

# The one shared species whose evolution list Gen 5 lengthened: Feebas gained
# the Prism Scale trade beside its beauty evolution.
EVOLUTION_ROWS_CHANGED = {"SPECIES_FEEBAS"}

# Gen 5 writes a move that never misses as 101 where Gen 4 writes 0.
GEN5_ALWAYS_HITS = 101

# The species name table is upper case for the 493 the server already holds,
# because Gen 4 wrote its names that way. Gen 5 switched to title case.
SPECIES_NAME_CHARSET_DIFFERS = {29, 32, 83}


def die(msg):
    sys.stdout.flush()
    sys.stderr.write("gen5_tables: %s\n" % msg)
    raise SystemExit(1)


def engine_dir(explicit):
    if explicit:
        return Path(explicit)
    out = subprocess.run([str(MMO / "tools" / "decomp_dir.sh"), "pokeplatinum"],
                         capture_output=True, text=True)
    path = out.stdout.strip()
    if not path:
        die("no pokeplatinum checkout; pass --engine")
    return Path(path)


def nitro_rom(rom, engine):
    sys.path.insert(0, str(engine / "pc"))
    try:
        from modport import NitroRom            # noqa: E402
    except ImportError:
        die("%s/pc has no modport.py; is that the engine tree?" % engine)
    return NitroRom(rom)


# ---------------------------------------------------------------- decomp side

def enum_table(engine, name):
    """One of the decomp's generated constant lists, each name against its value."""
    out, running = {}, 0
    path = engine / "generated" / name
    if not path.exists():
        die("%s is missing; is %s an initialised checkout?" % (path, engine))
    for line in path.read_text().splitlines():
        line = line.split("//")[0].strip().rstrip(",")
        if not line:
            continue
        if "=" in line:
            key, value = [part.strip() for part in line.split("=", 1)]
            running = int(value, 0) if value.lstrip("-").isdigit() else out[value]
        else:
            key = line
        out[key] = running
        running += 1
    return out


def decomp_species(engine, ids):
    """Platinum's per species JSON, by national dex id."""
    out = {}
    for constant, dex in ids.items():
        if constant in ("SPECIES_NONE", "SPECIES_EGG", "SPECIES_BAD_EGG"):
            continue
        path = (engine / "res/pokemon"
                / constant.removeprefix("SPECIES_").lower() / "data.json")
        if path.exists():
            out[dex] = (constant, json.loads(path.read_text()))
    return out


def decomp_moves(engine, ids):
    out = {}
    for constant, mid in ids.items():
        if constant in ("MOVE_NONE", "MAX_MOVES"):
            continue
        path = (engine / "res/moves"
                / constant.removeprefix("MOVE_").lower() / "data.json")
        if path.exists():
            out[mid] = (constant, json.loads(path.read_text()))
    return out


# ------------------------------------------------------------------ cartridge

def rol16(key):
    return ((key << 3) | (key >> 13)) & 0xFFFF


def ror16(key):
    return ((key >> 3) | (key << 13)) & 0xFFFF


def decrypt(chars):
    """One Gen 5 string, from its encrypted u16 array."""
    key = chars[-1] ^ 0xFFFF
    for _ in range(len(chars) - 1):
        key = ror16(key)
    out = []
    for char in chars:
        out.append(char ^ key)
        key = rol16(key)
    if out[-1] != 0xFFFF:
        return None
    return "".join(chr(c) for c in out[:-1] if c != 0)


def text_block(member):
    """The first block of a Gen 5 message file, or None if it is not one."""
    if len(member) < 16:
        return None
    blocks, entries = struct.unpack_from("<HH", member, 0)
    size, = struct.unpack_from("<I", member, 4)
    if not (0 < blocks <= 64 and 0 < entries <= 30000):
        return None
    if size not in (len(member), len(member) - 16):
        return None
    base, = struct.unpack_from("<I", member, 12)
    out = []
    for index in range(entries):
        head = base + 4 + index * 8
        if head + 8 > len(member):
            return None
        offset, count, _flags = struct.unpack_from("<IHH", member, head)
        start = base + offset
        if count == 0 or start + count * 2 > len(member):
            return None
        text = decrypt(list(struct.unpack_from("<%dH" % count, member, start)))
        if text is None:
            return None
        out.append(text)
    return out


def read_names(rom, member, count, what):
    block = text_block(rom.narc_members(TEXT_NARC)[member])
    if block is None:
        die("text archive member %d is not a message file" % member)
    if len(block) < count:
        die("%s has %d entries, expected at least %d" % (what, len(block), count))
    return block


def machine_moves(rom_path):
    """The 101 moves Black's machines teach, in the ARM9 table's own order."""
    import porticons  # local: it owns the ARM9 decompressor

    arm9 = porticons.decompressed_arm9(rom_path)
    found = []
    for off in range(0, len(arm9) - TM_TABLE_ENTRIES * 2, 2):
        vals = struct.unpack_from("<%dH" % TM_TABLE_ENTRIES, arm9, off)
        if all(1 <= v <= LAST_GEN5_MOVE for v in vals) and len(set(vals)) == TM_TABLE_ENTRIES:
            found.append((off, list(vals)))
    if not found:
        die("no run of %d distinct move ids in the ARM9; the machine table is "
            "not where this reading says it is" % TM_TABLE_ENTRIES)
    if len(found) > 1:
        die("%d runs of %d distinct move ids in the ARM9, so the machine table "
            "cannot be told from a coincidence" % (len(found), TM_TABLE_ENTRIES))
    return found[0][1]


def machine_bit(table_index):
    """The personal entry's bit for the machine at `table_index` of the table."""
    if table_index < TM_COUNT - 3:                  # TM01..TM92
        return table_index
    if table_index < TM_COUNT - 3 + HM_COUNT:       # HM01..HM06
        return table_index + 3
    return table_index - HM_COUNT                   # TM93..TM95


def personal(member):
    """One species' row of a/0/1/6, as the fields this repo has a use for."""
    ev = struct.unpack_from("<H", member, 10)[0]
    return {
        "hp": member[0], "attack": member[1], "defense": member[2],
        "speed": member[3], "special_attack": member[4],
        "special_defense": member[5],
        "type1": member[6], "type2": member[7],
        "catch_rate": member[8],
        "ev_hp": ev & 3, "ev_attack": (ev >> 2) & 3, "ev_defense": (ev >> 4) & 3,
        "ev_speed": (ev >> 6) & 3, "ev_special_attack": (ev >> 8) & 3,
        "ev_special_defense": (ev >> 10) & 3,
        "item1": struct.unpack_from("<H", member, 12)[0],
        "item2": struct.unpack_from("<H", member, 14)[0],
        "item3": struct.unpack_from("<H", member, 16)[0],
        "gender_ratio": member[18],
        "hatch_cycles": member[19],
        "base_friendship": member[20],
        "exp_rate": member[21],
        "egg_group1": member[22], "egg_group2": member[23],
        "ability1": member[24], "ability2": member[25], "ability_hidden": member[26],
        "escape_rate": member[27],
        "form_count": member[32],
        "body_color": member[33] & 0x3F,
        "flip_sprite": bool(member[33] & 0x40),
        "base_exp_reward": struct.unpack_from("<H", member, 34)[0],
        # The machine compatibility bitfield: 13 bytes for 104 bits, of which
        # 101 are spent. Bit n is machine number n (TM01..TM95 then HM01..HM06),
        # which is not the order the ARM9's own table is in; see machine_bit.
        "machines": bytes(member[MACHINES_AT:MACHINES_AT + MACHINES_BYTES]),
    }


def move_row(member):
    """One move's row of a/0/2/1."""
    return {
        "type": member[0],
        "category": member[2],
        "power": member[3],
        "accuracy": 0 if member[4] == GEN5_ALWAYS_HITS else member[4],
        "pp": member[5],
        "priority": struct.unpack_from("<b", member, 6)[0],
        "chance": max(member[10], member[15], member[27]),
        "effect": struct.unpack_from("<H", member, 16)[0],
        "range": member[20],
        "flags": struct.unpack_from("<H", member, 32)[0],
    }


def learnset(member):
    """A species' level up moves: u16 move, u16 level, until the 0xFFFF pair."""
    out = []
    for offset in range(0, len(member) - 3, 4):
        move, level = struct.unpack_from("<HH", member, offset)
        if move == 0xFFFF:
            break
        out.append((level, move))
    return out


def evolutions(member):
    """A species' seven evolution rows: u16 method, u16 parameter, u16 target."""
    out = []
    for index in range(7):
        method, param, target = struct.unpack_from("<HHH", member, index * 6)
        if method:
            out.append((method, param, target))
    return out


# ---------------------------------------------------------------- translation

def server_type(gen5_type):
    """Gen 5 deleted Gen 4's ??? type at 9, so everything after it moved down."""
    return gen5_type if gen5_type < GEN4_MYSTERY_TYPE else gen5_type + 1


def server_method(gen5_method):
    """Gen 5 inserted trade-for-a-species at 7. None for that one: the server
    has no such method, and inventing an id for it would be inventing the
    mechanic behind it too."""
    if gen5_method < GEN5_TRADE_WITH_SPECIES:
        return gen5_method
    if gen5_method == GEN5_TRADE_WITH_SPECIES:
        return None
    return gen5_method - 1


def move_flags(bits):
    """The server's flag names for a Gen 5 flag word."""
    return sorted(name for bit, name in FLAG_BITS.items() if bits & (1 << bit))


def constant_name(text):
    """A display name as the constant a Kotlin enum entry carries."""
    out = []
    for char in text.upper():
        out.append(char if char.isalnum() else "_")
    name = "_".join(part for part in "".join(out).split("_") if part)
    if name and name[0].isdigit():
        name = "N" + name
    return name


# --------------------------------------------------------------------- oracle

class Check:
    """Counts and names every disagreement, so one report says what happened."""

    def __init__(self):
        self.failures = []
        self.notes = []

    def equal(self, what, who, got, want):
        if got != want:
            self.failures.append("%s %s: cartridge %r, decomp %r" % (who, what, got, want))

    def note(self, line):
        self.notes.append(line)

    def report(self, verbose):
        for line in self.notes:
            print("  " + line)
        if not self.failures:
            print("  every shared id agrees with the decomp")
            return True
        print("  %d disagreement(s) the decode does not explain:" % len(self.failures))
        for line in self.failures[:40 if verbose else 10]:
            print("    " + line)
        if len(self.failures) > (40 if verbose else 10):
            print("    ... and %d more" % (len(self.failures) - (40 if verbose else 10)))
        return False


def verify(rom, engine, verbose):
    """Decode the range both games share and hold it against the decomp."""
    check = Check()
    species_ids = enum_table(engine, "species.txt")
    move_ids = enum_table(engine, "moves.txt")
    item_ids = enum_table(engine, "items.txt")
    types = enum_table(engine, "pokemon_types.txt")
    abilities = enum_table(engine, "abilities.txt")
    exp_rates = enum_table(engine, "exp_rates.txt")
    egg_groups = enum_table(engine, "egg_groups.txt")
    colors = enum_table(engine, "pokemon_colors.txt")
    gender_ratios = enum_table(engine, "gender_ratios.txt")
    effects = enum_table(engine, "move_battle_effects.txt")
    methods = enum_table(engine, "evolution_methods.txt")

    personals = rom.narc_members(PERSONAL_NARC)
    moves = rom.narc_members(MOVES_NARC)
    learnsets = rom.narc_members(LEARNSET_NARC)
    evos = rom.narc_members(EVOLUTION_NARC)
    species_names = read_names(rom, TEXT_SPECIES_NAMES, LAST_GEN5_SPECIES + 1, "species names")
    move_names = read_names(rom, TEXT_MOVE_NAMES, LAST_GEN5_MOVE + 1, "move names")
    ability_names = read_names(rom, TEXT_ABILITY_NAMES, LAST_GEN5_ABILITY + 1, "ability names")

    # ---- species
    rebalanced = {"base_exp_reward": 0, "escape_rate": 0, "held_items": 0}
    learnsets_same = 0
    for dex, (constant, want) in sorted(decomp_species(engine, species_ids).items()):
        got = personal(personals[dex])
        stats, evs = want["base_stats"], want["ev_yields"]
        for field in ("hp", "attack", "defense", "speed", "special_attack",
                      "special_defense"):
            check.equal(field, constant, got[field], stats[field])
            check.equal("ev " + field, constant, got["ev_" + field], evs[field])
        check.equal("catch rate", constant, got["catch_rate"], want["catch_rate"])
        check.equal("hatch cycles", constant, got["hatch_cycles"], want["hatch_cycles"])
        check.equal("friendship", constant, got["base_friendship"],
                    want["base_friendship"])
        check.equal("ability 1", constant, got["ability1"],
                    abilities[want["abilities"][0]])
        check.equal("ability 2", constant, got["ability2"],
                    abilities[want["abilities"][1]])
        check.equal("type 1", constant, server_type(got["type1"]),
                    types[want["types"][0]])
        check.equal("type 2", constant, server_type(got["type2"]),
                    types[want["types"][1]])
        check.equal("growth rate", constant, got["exp_rate"], exp_rates[want["exp_rate"]])
        check.equal("egg group 1", constant, got["egg_group1"],
                    egg_groups[want["egg_groups"][0]])
        check.equal("egg group 2", constant, got["egg_group2"],
                    egg_groups[want["egg_groups"][1]])
        check.equal("gender ratio", constant, got["gender_ratio"],
                    gender_ratios[want["gender_ratio"]])
        check.equal("body colour", constant, got["body_color"],
                    colors[want["body_color"]])
        check.equal("flip", constant, got["flip_sprite"], want["flip_sprite"])
        if got["base_exp_reward"] != want["base_exp_reward"]:
            rebalanced["base_exp_reward"] += 1
        if got["escape_rate"] != want["safari_flee_rate"]:
            rebalanced["escape_rate"] += 1
        if (got["item1"] != item_ids[want["held_items"]["common"]]
                or got["item2"] != item_ids[want["held_items"]["rare"]]):
            rebalanced["held_items"] += 1
        # names, which are the whole oracle for the text decode
        if dex not in SPECIES_NAME_CHARSET_DIFFERS:
            check.equal("name", constant, species_names[dex].upper(),
                        want["pokedex_data"]["en"]["name"].upper())
        # learnsets: the format is the claim, the agreement is the measurement
        mine = [(level, move) for level, move in learnset(learnsets[dex])]
        theirs = [(entry[0], move_ids[entry[1]])
                  for entry in want["learnset"]["by_level"]]
        if mine == theirs:
            learnsets_same += 1
        # evolutions
        rows = evolutions(evos[dex])
        theirs_evo = want.get("evolutions", [])
        if len(rows) != len(theirs_evo):
            if constant not in EVOLUTION_ROWS_CHANGED:
                check.failures.append(
                    "%s evolution rows: cartridge %d, decomp %d"
                    % (constant, len(rows), len(theirs_evo)))
            continue
        for (method, param, target), entry in zip(rows, theirs_evo):
            mapped = server_method(method)
            check.equal("evolution method", constant,
                        None if mapped is None else mapped, methods[entry[0]])
            check.equal("evolution target", constant, target, species_ids[entry[-1]])
            if len(entry) == 3:
                token = entry[1]
                wanted = (token if isinstance(token, int)
                          else item_ids.get(token) or move_ids.get(token)
                          or species_ids.get(token))
                check.equal("evolution parameter", constant, param, wanted)

    # ---- moves
    move_rebalanced = {"power": 0, "accuracy": 0, "pp": 0, "priority": 0, "chance": 0}
    for mid, (constant, want) in sorted(decomp_moves(engine, move_ids).items()):
        got = move_row(moves[mid])
        check.equal("name", constant, move_names[mid], want["name"])
        if constant != "MOVE_CURSE":
            check.equal("type", constant, server_type(got["type"]), types[want["type"]])
        if constant not in EFFECT_CHANGED:
            check.equal("effect", constant, got["effect"], effects[want["effect"]["type"]])
        if constant not in RANGE_CHANGED:
            check.equal("range", constant, RANGE_NAMES[got["range"]], want["range"])
        for bit, name in FLAG_BITS.items():
            if constant in FLAG_CHANGED[name]:
                continue
            check.equal("flag " + name, constant, bool(got["flags"] & (1 << bit)),
                        name in [FLAG_NAME_BY_DECOMP[f] for f in want["flags"]
                                 if f in FLAG_NAME_BY_DECOMP])
        for field, theirs in (("power", want["power"]), ("accuracy", want["accuracy"]),
                              ("pp", want["pp"]), ("priority", want["priority"]),
                              ("chance", want["effect"]["chance"])):
            if got[field] != theirs:
                move_rebalanced[field] += 1

    # ---- abilities, whose names are the only thing the shared range can check.
    # Compared with the word breaks taken out: the decomp splits two of them
    # where the game's own name table does not (Compoundeyes, Lightningrod), and
    # that is a spelling choice rather than a different ability.
    for constant, aid in abilities.items():
        if aid > LAST_GEN4_ABILITY or constant == "ABILITY_NONE":
            continue
        check.equal("ability name", constant,
                    constant_name(ability_names[aid]).replace("_", ""),
                    constant.removeprefix("ABILITY_").replace("_", ""))

    check.note("species compared: %d, moves compared: %d"
               % (len(decomp_species(engine, species_ids)),
                  len(decomp_moves(engine, move_ids))))
    check.note("gen 5 rebalanced, as expected: %d base experience yields, "
               "%d escape rates, %d held item pairs"
               % (rebalanced["base_exp_reward"], rebalanced["escape_rate"],
                  rebalanced["held_items"]))
    check.note("gen 5 rebalanced moves: %s"
               % ", ".join("%d %s" % (n, f) for f, n in sorted(move_rebalanced.items())))
    check.note("learnsets identical to Platinum's: %d of %d"
               % (learnsets_same, len(decomp_species(engine, species_ids))))
    return check.report(verbose)


# Platinum's flag constant against the server enum name the move generator gives
# it, for the five that Gen 5 also carries.
FLAG_NAME_BY_DECOMP = {
    "MOVE_FLAG_MAKES_CONTACT": "MAKES_CONTACT",
    "MOVE_FLAG_CAN_PROTECT": "PROTECT_AFFECTED",
    "MOVE_FLAG_CAN_MAGIC_COAT": "MAGIC_COAT_AFFECTED",
    "MOVE_FLAG_CAN_SNATCH": "SNATCH_AFFECTED",
    "MOVE_FLAG_CAN_MIRROR_MOVE": "MIRROR_MOVE_AFFECTED",
}


# --------------------------------------------------------------------- emit

def build(rom, engine, rom_path):
    """The rows to commit."""
    effects = enum_table(engine, "move_battle_effects.txt")
    methods = enum_table(engine, "evolution_methods.txt")
    method_name = {v: k.removeprefix("EVO_") for k, v in methods.items()}

    personals = rom.narc_members(PERSONAL_NARC)
    moves = rom.narc_members(MOVES_NARC)
    learnsets = rom.narc_members(LEARNSET_NARC)
    evos = rom.narc_members(EVOLUTION_NARC)
    species_names = read_names(rom, TEXT_SPECIES_NAMES, LAST_GEN5_SPECIES + 1, "species names")
    move_names = read_names(rom, TEXT_MOVE_NAMES, LAST_GEN5_MOVE + 1, "move names")
    ability_names = read_names(rom, TEXT_ABILITY_NAMES, LAST_GEN5_ABILITY + 1, "ability names")
    ability_descriptions = read_names(rom, TEXT_ABILITY_DESCRIPTIONS,
                                      LAST_GEN5_ABILITY + 1, "ability descriptions")
    # The machines, which live in the ARM9 rather than on the items the way Gen
    # 4's do. A species' compatibility is a bit per machine in its personal
    # entry, numbered by the machine's number and not by its place in that table
    # (machine_bit). What is emitted is the MOVES a species can be taught and
    # never the machine numbers, because the two games number machines
    # differently from TM01 on, Hone Claws against Focus Punch, and a move
    # id is the one thing that means the same on both sides.
    machine_table = machine_moves(rom_path)

    new_species = range(LAST_GEN4_SPECIES + 1, LAST_GEN5_SPECIES + 1)
    new_moves = range(LAST_GEN4_MOVE + 1, LAST_GEN5_MOVE + 1)

    # The egg a species lays hatches into the root of its family. Gen 5 added no
    # incense baby, so walking the evolution graph back is the whole rule; it is
    # walked over the whole table, because a Gen 5 species can descend from a
    # Gen 4 one (Munna does not, but Mienfoo's family and the Gen 5 stages of
    # older families would).
    parent = {}
    for dex in range(1, LAST_GEN5_SPECIES + 1):
        for _method, _param, target in evolutions(evos[dex]):
            if target and target not in parent:
                parent[target] = dex

    def root(dex):
        seen = {dex}
        while dex in parent and parent[dex] not in seen:
            dex = parent[dex]
            seen.add(dex)
        return dex

    species = []
    dropped_evolutions = []
    dropped_items = []
    for dex in new_species:
        row = personal(personals[dex])
        name = species_names[dex]
        if any(ord(c) > 126 for c in name):
            die("species %d is named %r, which is not ASCII; the name table "
                "convention this writes assumes it is" % (dex, name))
        rows = []
        for method, param, target in evolutions(evos[dex]):
            mapped = server_method(method)
            if mapped is None:
                dropped_evolutions.append((dex, name, target, species_names[target]))
                continue
            if method_name[mapped] in ("TRADE_WITH_HELD_ITEM", "USE_ITEM",
                                       "USE_ITEM_MALE", "USE_ITEM_FEMALE",
                                       "LEVEL_WITH_HELD_ITEM_DAY",
                                       "LEVEL_WITH_HELD_ITEM_NIGHT"):
                param = ITEM_REGION_BLOCK + param
            rows.append({"method": mapped, "param": param, "target": target})
        # Gen 5 holds three item slots, 50%, 5% and 1%, where Gen 4 and
        # SpeciesDef hold two. The 50% one is the common slot and the 5% one is
        # the rare slot, which is the pair Gen 4 means; the 1% slot fills the
        # rare one only when the 5% slot is empty, which is how Pikachu keeps
        # its Light Ball. Eight of Black's species fill all three (Throh, Sawk,
        # Dwebble, Crustle, Trubbish, Garbodor, Foongus, Amoonguss) and their
        # 1% item is dropped, because there is nowhere for a third to go and
        # dropping the rarest is the least of the three losses.
        #
        # The ids need no translation. A Gen 5 held item Gen 4 never had is
        # still an item this server knows, because its catalogue is the Gen 5
        # name table rather than the decomp's, and the ids the two share are the
        # same numbers: 155 is an Oran Berry and 236 a Light Ball in both.
        if row["item2"] and row["item3"]:
            dropped_items.append((dex, name, row["item3"]))
        rare = row["item2"] or row["item3"]
        species.append({
            "id": dex,
            "name": name.upper(),
            "base_stats": {k: row[k] for k in ("hp", "attack", "defense", "speed",
                                               "special_attack", "special_defense")},
            "types": [server_type(row["type1"]), server_type(row["type2"])],
            "catch_rate": row["catch_rate"],
            "base_exp_reward": row["base_exp_reward"],
            "ev_yields": {k: row["ev_" + k] for k in ("hp", "attack", "defense", "speed",
                                                      "special_attack", "special_defense")},
            "held_items": {
                "common": ITEM_REGION_BLOCK + row["item1"] if row["item1"] else 0,
                "rare": ITEM_REGION_BLOCK + rare if rare else 0,
            },
            "gender_ratio": row["gender_ratio"],
            "hatch_cycles": row["hatch_cycles"],
            "base_friendship": row["base_friendship"],
            "exp_rate": row["exp_rate"],
            "egg_groups": [row["egg_group1"], row["egg_group2"]],
            "abilities": [row["ability1"], row["ability2"]],
            "escape_rate": row["escape_rate"],
            "body_color": row["body_color"],
            "flip_sprite": row["flip_sprite"],
            "offspring": root(dex),
            "learnset": [{"level": level, "move": move}
                         for level, move in learnset(learnsets[dex])],
            # Every move a machine can teach this species, as move ids. The
            # server pairs them against its own machines by move; the client's
            # bitfield is written from the same two facts by portspecies.py.
            "machine_moves": sorted(
                move for index, move in enumerate(machine_table)
                if (row["machines"][machine_bit(index) // 8]
                    >> (machine_bit(index) % 8)) & 1),
            "evolutions": rows,
        })

    # Every battle effect a Gen 5 move names that Platinum has no name for gets
    # the name of the first move that uses it, which is how the GBA tree named
    # BATTLE_EFFECT_ABSORB and the rest. A gap in the run is an effect no move
    # in the table uses; it still needs an entry, or every id after it is one
    # out.
    first_user = {}
    for mid in new_moves:
        effect = move_row(moves[mid])["effect"]
        if effect > max(effects.values()):
            first_user.setdefault(effect, move_names[mid])
    new_effects = {}
    taken = set(k.removeprefix("BATTLE_EFFECT_") for k in effects)
    for effect in range(max(effects.values()) + 1, max(first_user, default=0) + 1):
        base = constant_name(first_user[effect]) if effect in first_user else "UNUSED_%d" % effect
        name = base
        while name in taken:
            name = base + "_GEN5"
        taken.add(name)
        new_effects[effect] = name

    move_list = []
    for mid in new_moves:
        row = move_row(moves[mid])
        move_list.append({
            "id": mid,
            "name": move_names[mid],
            "effect": row["effect"],
            "power": row["power"],
            "type": server_type(row["type"]),
            "accuracy": row["accuracy"],
            "pp": row["pp"],
            "chance": row["chance"],
            "target": TARGET_BY_RANGE[RANGE_NAMES[row["range"]]],
            "priority": row["priority"],
            "flags": move_flags(row["flags"]),
        })

    new_abilities = {aid: constant_name(ability_names[aid])
                     for aid in range(LAST_GEN4_ABILITY + 1, LAST_GEN5_ABILITY + 1)}
    # The same 41 as text a screen can print, because the ability an appended
    # species carries had no name anywhere on the client: the engine's three
    # banks stop at 124 and so does mmo/src/display_data.gen.h. tools/gen_
    # display_data.py reads these to grow that table, and tools/portabilities.py
    # fills the ROM banks straight from the cartridge for the screens the engine
    # draws. The description is flat here, one line, Gen 5's own break folded
    # to a space, because the client that reads this table lays out its own
    # text and the official client overlay beside it is flat too. The DS summary box wants
    # two lines of 26 and portabilities.py wraps it there, where that box is.
    ability_text = {
        aid: {"name": ability_names[aid],
              "description": ability_descriptions[aid].replace(GEN5_NEWLINE, " ")}
        for aid in range(LAST_GEN4_ABILITY + 1, LAST_GEN5_ABILITY + 1)}

    # The hidden ability, for the whole dex rather than the Gen 5 half of it.
    # Gen 4's species data has no slot for one, so there is nothing here to
    # overwrite and no reason to take only half a table. Byte 26 is the third
    # ability slot: the two beside it at 24 and 25 matched the decomp 493 times
    # each, and the block after it at 32..39 was read whole on species at both
    # ends of the table, so the offset is pinned from both sides.
    hidden = {}
    for dex in range(1, LAST_GEN5_SPECIES + 1):
        aid = personal(personals[dex])["ability_hidden"]
        if aid > LAST_GEN5_ABILITY:
            die("species %d has hidden ability %d and the table stops at %d"
                % (dex, aid, LAST_GEN5_ABILITY))
        hidden[dex] = aid
    print("  hidden abilities: %d of %d species have one"
          % (sum(1 for v in hidden.values() if v), len(hidden)))
    if dropped_items:
        print("  %d species fill all three held item slots; their 1%% item has "
              "nowhere to go: %s"
              % (len(dropped_items), ", ".join(n for _, n, _ in dropped_items)))

    source = {
        "cartridge": "%s %s" % (rom.code, rom.title),
        "note": "written by mmo/tools/gen5_tables.py; do not edit by hand",
    }
    return (
        {"source": source, "species": species},
        {"source": source, "moves": move_list},
        {"source": source,
         "hidden_abilities": {str(k): v for k, v in sorted(hidden.items())}},
        {"source": source,
         "abilities": {str(k): v for k, v in sorted(new_abilities.items())},
         "ability_text": {str(k): v for k, v in sorted(ability_text.items())},
         "move_effects": {str(k): v for k, v in sorted(new_effects.items())}},
        dropped_evolutions,
    )


def write_json(path, payload):
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=True) + "\n")


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--rom", required=True, help="a Black or White NDS image")
    ap.add_argument("--engine", help="the pokeplatinum checkout to verify against")
    ap.add_argument("--out", help="where the tables go (default codegen/gen5)")
    ap.add_argument("--check", action="store_true", help="verify only, write nothing")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args(argv)

    engine = engine_dir(args.engine)
    rom = nitro_rom(Path(args.rom), engine)
    print("gen5_tables: %s %s, verified against %s" % (rom.code, rom.title, engine))

    if not verify(rom, engine, args.verbose):
        die("the shared range does not agree with the decomp; nothing written")
    if args.check:
        return 0

    out = Path(args.out) if args.out else REPO / "codegen" / "gen5"
    out.mkdir(parents=True, exist_ok=True)
    species, moves, hidden, enums, dropped = build(rom, engine, Path(args.rom))
    write_json(out / "species.json", species)
    write_json(out / "moves.json", moves)
    write_json(out / "hidden_abilities.json", hidden)
    write_json(out / "enums.json", enums)
    print("  wrote %d species, %d moves, %d hidden abilities, %d new abilities and "
          "%d battle effects to %s"
          % (len(species["species"]), len(moves["moves"]),
             len(hidden["hidden_abilities"]), len(enums["abilities"]),
             len(enums["move_effects"]), out))
    for dex, name, target, target_name in dropped:
        print("  dropped %s (%d) -> %s (%d): trade for a species is a method "
              "Gen 4 has no number for" % (name, dex, target_name, target))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
