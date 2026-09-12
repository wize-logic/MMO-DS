#!/usr/bin/env python3
"""Generate, per HeartGold map, which of its people the default scene hides."""

from __future__ import annotations

import json
import re
import subprocess
import sys
from pathlib import Path

MMO = Path(__file__).resolve().parent.parent

# The conditional forms, as the source spells them: the branch is taken when
# the comparison result satisfies the test. `sConditionTable` in both games'
# scrcmd.c is the same six rows.
CONDITIONS = {
    "Eq": lambda c: c == 0,
    "Ne": lambda c: c != 0,
    "Gt": lambda c: c > 0,
    "Ge": lambda c: c >= 0,
    "Lt": lambda c: c < 0,
    "Le": lambda c: c <= 0,
}
STOPS = ("End",)
MAX_STEPS = 4096

# Every read of the save that an arrival script makes, and what a save that has
# never been played answers. The rule is the same for all of them, every
# variable operand of the command is written 0, and the second column is the
# reason that is the right answer, read from the command's handler in
# src/scrcmd_c.c. A command missing from this table is refused, not guessed.
READS = {
    "GetWeekday": "no calendar: Sunday",
    "ScrCmd_522": "Field_GetHour: no clock, midnight",
    "ScrCmd_379": "Field_GetTimeOfDay: no clock",
    "GetPhoneBookRematch": "no phone book, so no rematch",
    "CheckRegisteredPhoneNumber": "no phone book",
    "GetFriendSprite": "no rival chosen",
    "GetPlayerGender": "the first gender",
    "GetGameVersion": "the first version",
    "ScrCmd_445": "no previous map",
    "HasItem": "an empty bag",
    "GetPartyCount": "an empty party",
    "GetPartyLeadAlive": "an empty party",
    "CheckDaycareEgg": "an empty day care",
    "Random": "no dice: the first outcome",
    "FollowerPokeIsEventTrigger": "an empty party",
    "GetOwnedRotomForms": "no Rotom",
    "MomGiftCheck": "no gift queued",
    "MysteryGift": "no gift waiting",
    "ScrCmd_412": "no party for the Frontier",
    "NopVar490": "a no-op that names a variable",
    "DebugWatch": "a no-op that names a variable",
    "PlayerHasSpecies": "an empty party",
    "ScrCmd_415": "sub_02067398 of an empty save",
}

# Reads a cleared save answers YES: the last variable operand is written 1.
READS_YES = {
    "CheckBadge": "a cleared save holds every badge",
    "CheckGameClearFlag": "the Hall of Fame has been entered",
}

# Commands whose whole effect is one flag, read from their handler.
SETS = {
    "ScrCmd_814": "FLAG_UNK_99A",   # SetFlag99A
}

# Commands an arrival script issues that touch nothing this reads: the gym
# gimmick initialisers write the gym's own save block (src/gymmick_init.c),
# the rest are sound, camera, a spawn point or a Pokegear setting.
NOOPS = {
    "VioletGymInit", "AzaleaGymInit", "EcruteakGymInit", "CianwoodGymInit",
    "VermilionGymInit", "BlackthornGymInit", "FuchsiaGymInit",
    "ViridianGymInit",
    "StopBGM", "SetBikeStateLock", "PalParkAction",
    "LotoIDSet",    # std_init rolling the lottery number
    "ScrCmd_582",   # a special spawn point
    "ScrCmd_804",   # Pokegear_SetMapUnlockLevel
    "SetObjectMovementType", "SetObjectFacing", "MakeObjectVisible",
    # MoveWarp is Field_SetWarpXYPos: it moves a WARP EVENT's coordinates,
    # not the player and not a person (HeartGold's own ScrCmd_MoveWarp, and
    # the opcode sits between SetObjectFacing and MoveBGEvent, which is the
    # same idea for a background event). Nothing it does can hide or show
    # anybody, so this page's question is unaffected by it. NOTE, separately:
    # a map whose arrival script moves a warp keeps the ported warp where the
    # event data put it, which is its own divergence and not this file's.
    "MoveWarp",
}


class Unsupported(Exception):
    pass


def die(msg: str) -> None:
    print("gen_mapscenes: " + msg, file=sys.stderr)
    raise SystemExit(2)


def decomp_dir(name: str) -> Path | None:
    try:
        out = subprocess.run([str(MMO / "tools" / "decomp_dir.sh"), name],
                             capture_output=True, text=True, check=True)
    except (OSError, subprocess.CalledProcessError):
        return None
    return Path(out.stdout.strip())


def parse_asm(text: str) -> tuple[list[tuple[str, list[str]]], dict[str, int]]:
    """A script source as a flat instruction list plus where each label sits."""
    ops: list[tuple[str, list[str]]] = []
    labels: dict[str, int] = {}
    for raw in text.splitlines():
        line = raw.split("//")[0].split("@")[0].strip()
        if not line or line.startswith("#") or line.startswith("."):
            continue
        m = re.match(r"^(\w+):$", line)
        if m:
            labels[m.group(1)] = len(ops)
            continue
        parts = line.split(None, 1)
        args = [a.strip() for a in parts[1].split(",")] if len(parts) > 1 else []
        ops.append((parts[0], args))
    return ops, labels


def script_defs(text: str) -> list[str]:
    """The bank's entry table: `ScrDef <label>` rows, in id order."""
    return re.findall(r"^\s*ScrDef\s+(\w+)", text, re.M)


class Bank:
    """One script source: its instructions, labels, entries and object names."""

    def __init__(self, path: Path, consts: dict[str, int]):
        text = path.read_text()
        self.name = path.stem
        self.ops, self.labels = parse_asm(text)
        self.defs = script_defs(text)
        # The map's own event_<map>.h: its people by number (for HidePerson
        # and ShowPerson) and the handful of named values its scripts use.
        self.consts = consts


class World:
    """The state one map's arrival scripts leave behind."""

    def __init__(self) -> None:
        self.flags: set[str] = set()
        self.vars: dict[str, int] = {}
        self.hide: set[int] = set()
        self.show: set[int] = set()

    def copy(self) -> "World":
        w = World()
        w.flags, w.vars = set(self.flags), dict(self.vars)
        return w

    def value(self, tok: str, consts: dict[str, int]) -> int:
        if tok == "TRUE":
            return 1
        if tok == "FALSE":
            return 0
        if tok.startswith("VAR_"):
            return self.vars.get(tok, 0)
        if tok in consts:
            return consts[tok]
        try:
            return int(tok, 0)
        except ValueError:
            raise Unsupported("value %s" % tok)


def run(bank: Bank, start: int, world: World, std) -> None:
    """Interpret one entry from `start` in a save that was never played."""
    ops, labels = bank.ops, bank.labels
    pc, cmp_result, steps = start, 0, 0
    stack: list[int] = []

    def target(label: str) -> int:
        if label not in labels:
            raise Unsupported("branch to %s" % label)
        return labels[label]

    while 0 <= pc < len(ops):
        steps += 1
        if steps > MAX_STEPS:
            raise Unsupported("did not stop")
        name, args = ops[pc]
        pc += 1
        if name == "SetFlag":
            world.flags.add(args[0])
        elif name == "ClearFlag":
            world.flags.discard(args[0])
        elif name == "SetVar":
            world.vars[args[0]] = world.value(args[1], bank.consts)
        elif name == "Compare":
            if len(args) != 2:
                raise Unsupported(name)
            cmp_result = (world.value(args[0], bank.consts)
                          - world.value(args[1], bank.consts))
        elif name == "GoTo":
            pc = target(args[0])
        elif name == "Call":
            stack.append(pc)
            pc = target(args[0])
        elif name == "Return":
            if not stack:
                return
            pc = stack.pop()
        elif name in STOPS:
            return
        elif name in ("HidePerson", "ShowPerson"):
            if not args[0].startswith("obj_") or args[0] not in bank.consts:
                raise Unsupported("%s of %s" % (name, args[0]))
            (world.hide if name == "HidePerson" else world.show).add(
                bank.consts[args[0]])
        elif name in SETS:
            world.flags.add(SETS[name])
        elif name in READS:
            for a in args:
                if a.startswith("VAR_"):
                    world.vars[a] = 0
        elif name in READS_YES:
            dests = [a for a in args if a.startswith("VAR_")]
            if dests:
                world.vars[dests[-1]] = 1
        elif name == "SetTrainerHouseSprite":
            # HeartGold's own ScrCmd_SetTrainerHouseSprite asks
            # TrainerHouseSet_CheckHasData whether slot <arg0> holds a trainer
            # somebody recorded, and on this page's premise, a save that was
            # never played, no slot does. So it writes 0 to the "is there a
            # trainer here" variable and 0 to VAR_OBJ_<n>, which is the sprite
            # the SPRITE_VAR_<n> body would have worn. Both are determinate,
            # not a guess: there is nothing in the save to read.
            if len(args) != 2 or not args[1].startswith("VAR_"):
                raise Unsupported(name)
            world.vars[args[1]] = 0
        elif name in NOOPS:
            pass
        else:
            m = re.match(r"^(GoTo|Call)If(Set|Unset|Defeated|NotDefeated|"
                         r"Eq|Ne|Gt|Ge|Lt|Le)$", name)
            if not m:
                raise Unsupported(name)
            verb, test = m.groups()
            if test in ("Set", "Unset"):
                taken = (args[0] in world.flags) == (test == "Set")
                dest = args[1]
            elif test in ("Defeated", "NotDefeated"):
                # No trainer has been fought.
                taken = test == "NotDefeated"
                dest = args[1]
            else:
                taken = CONDITIONS[test](cmp_result)
                dest = args[0]
            if taken:
                if verb == "Call":
                    stack.append(pc)
                pc = target(dest)


def arrival_scripts(hdr: Path) -> list[str]:
    """The ON_TRANSITION and ON_LOAD entries, in the order the game runs them."""
    text = hdr.read_text()
    out = []
    for kind in ("OnTransition", "OnLoad"):
        m = re.search(r"InitScriptEntry_%s\s+(\S+)" % kind, text)
        if m:
            out.append(m.group(1))
    return out


class StdBanks:
    """`entry std_x`: a common script named by number, found the way the game
    finds it, `sScriptBankMapping` in src/script_manager.c is a list of
    (first id, bank) rows in descending order and the first row whose id is
    not above the script's is its bank; the entry is the offset past it."""

    def __init__(self, hg: Path, load_bank):
        self.ids: dict[str, int] = {}
        for m in re.finditer(r"#define\s+(_?std_\w+)\s+(\d+)",
                             (hg / "include/constants/std_script.h").read_text()):
            self.ids.setdefault(m.group(1), int(m.group(2)))
        self.rows: list[tuple[int, str]] = []
        table = re.search(r"sScriptBankMapping\[\d+\]\s*=\s*\{(.*?)\};",
                          (hg / "src/script_manager.c").read_text(), re.S)
        if not table:
            die("no sScriptBankMapping in src/script_manager.c")
        for m in re.finditer(r"\{\s*(\w+)\s*,\s*NARC_scr_seq_(scr_seq_\d+)_bin",
                             table.group(1)):
            first = self.ids[m.group(1)] if m.group(1) in self.ids \
                else int(m.group(1))
            self.rows.append((first, m.group(2)))
        self.load_bank = load_bank

    def resolve(self, name: str) -> tuple[Bank, int]:
        if name not in self.ids:
            raise Unsupported("entry %s" % name)
        sid = self.ids[name]
        for first, stem in self.rows:
            if first <= sid:
                bank = self.load_bank(stem)
                if bank is None:
                    raise Unsupported("no source for %s" % stem)
                idx = sid - first
                if idx >= len(bank.defs):
                    raise Unsupported("entry %s past %s" % (name, stem))
                label = bank.defs[idx]
                if label not in bank.labels:
                    raise Unsupported("no body for %s" % label)
                return bank, bank.labels[label]
        raise Unsupported("entry %s in no bank" % name)


def main(argv: list[str]) -> int:
    hg = Path(argv[1]) if len(argv) > 1 else decomp_dir("pokeheartgold")
    out = Path(argv[2]) if len(argv) > 2 else MMO / "MAPSCENES"
    if hg is None:
        die("need a heartgold checkout; pass one or set DECOMP_DIR")
    ids = {}
    for m in re.finditer(r"#define\s+MAP_(\w+)\s+(\d+)",
                         (hg / "include/constants/maps.h").read_text()):
        ids.setdefault(m.group(1), int(m.group(2)))
    # The flag number goes in the table beside its name. A porter reads the
    # cartridge and this file and nothing else, and the eventFlag field it has
    # to compare against is a number.
    flagno = {}
    for m in re.finditer(r"#define\s+(FLAG_\w+)\s+(0x[0-9A-Fa-f]+|\d+)",
                         (hg / "include/constants/flags.h").read_text()):
        flagno.setdefault(m.group(1), int(m.group(2), 0))
    seq = hg / "files/fielddata/script/scr_seq"
    by_stem = {p.stem: p for p in seq.glob("scr_seq_*.s")}
    banks: dict[str, Bank] = {}

    def load_bank(stem: str) -> Bank | None:
        if stem in banks:
            return banks[stem]
        p = by_stem.get(stem)
        if p is None or not p.is_file():
            return None
        # A person's number, for HidePerson/ShowPerson: the map's own
        # event_<map>.h, which is the same list the cartridge's zone_event
        # record carries in its first field.
        consts: dict[str, int] = {}
        ev = seq / ("event_%s.h" % re.sub(r"^scr_seq_\d+_?", "", stem))
        if ev.is_file():
            for m in re.finditer(r"#define\s+(\w+)\s+(\d+)", ev.read_text()):
                consts.setdefault(m.group(1), int(m.group(2)))
        banks[stem] = Bank(p, consts)
        return banks[stem]

    std = StdBanks(hg, load_bank)

    # The save a new game starts from is not all-clear: `std_init` (9600,
    # scr_seq_0149) is the script the game runs once when a new game begins,
    # and it SETS 140-odd hide flags, the rival at Cherrygrove, Lance at
    # the Lake of Rage, Jasmine in her gym, so that the story can clear
    # them one scene at a time. Every map's arrival script runs after it.
    fresh = World()
    try:
        ibank, istart = std.resolve("std_init")
        run(ibank, istart, fresh, std)
    except Unsupported as exc:
        die("std_init did not evaluate: %s" % exc)
    # Then the story, cleared: mmo/STORYEND, on top of the new game.
    storyend = Path(argv[3]) if len(argv) > 3 else MMO / "STORYEND"
    if not storyend.is_file():
        die("no %s; run tools/gen_storyend.py" % storyend)
    end_vars, end_flags = 0, 0
    for line in storyend.read_text().splitlines():
        sm = re.match(r"^(var|flag)\s+(\w+)=0x[0-9A-Fa-f]+\s+(\d+)", line)
        if not sm:
            continue
        if sm.group(1) == "var":
            fresh.vars[sm.group(2)] = int(sm.group(3))
            end_vars += 1
        elif int(sm.group(3)):
            fresh.flags.add(sm.group(2))
            end_flags += 1
        else:
            fresh.flags.discard(sm.group(2))
            end_flags += 1

    events_dir = hg / "files/fielddata/eventdata/zone_event"

    def own_flags(stem: str) -> set[str] | None:
        """The eventFlag of every object the map's zone_event names."""
        path = events_dir / (stem + ".json")
        if not path.is_file():
            return None
        try:
            data = json.loads(path.read_text())
        except ValueError:
            return None
        return {o.get("eventFlag", "") for o in data.get("objects", [])}

    def source_stem(bank_const: str) -> str | None:
        m = re.match(r"NARC_scr_seq_(scr_seq_\d+_\w+?)_bin$", bank_const)
        return m.group(1) if m else None

    rows, evaluated, refused = [], 0, 0
    text = (hg / "src/data/map_headers.h").read_text()
    for m in re.finditer(r"\[MAP_(\w+)\]\s*=\s*\{(.*?)\n\s*\}", text, re.S):
        name, body = m.group(1), m.group(2)
        if name not in ids:
            continue
        hdr_c = re.search(r"\.scriptHeaderBank\s*=\s*(\w+)", body)
        scr_c = re.search(r"\.scriptsBank\s*=\s*(\w+)", body)
        if not hdr_c or not scr_c:
            continue
        # A row names only the flags the map's own people carry: the rest of
        # the save's flags hide nobody here, and the cleared story sets some
        # four hundred of them.
        ev_c = re.search(r"\.eventsBank\s*=\s*NARC_zone_event_(\w+?)_bin", body)
        own = own_flags(ev_c.group(1)) if ev_c else None
        hdr_stem, scr_stem = source_stem(hdr_c.group(1)), source_stem(scr_c.group(1))
        hdr = by_stem.get(hdr_stem) if hdr_stem else None
        bank = load_bank(scr_stem) if scr_stem else None
        if hdr is None or bank is None:
            rows.append((name.lower(), None, "no script source"))
            refused += 1
            continue
        entries = arrival_scripts(hdr)
        world = fresh.copy()
        if not entries:
            if own is not None:
                world.flags &= own
            rows.append((name.lower(), world, ""))
            evaluated += 1
            continue
        try:
            for ent in entries:
                sm = re.match(r"_EV_(\w+?)(?:\s*\+\s*\d+)?$", ent)
                if sm:
                    if sm.group(1) not in bank.labels:
                        raise Unsupported("no body for %s" % sm.group(1))
                    run(bank, bank.labels[sm.group(1)], world, std)
                else:
                    sbank, start = std.resolve(ent)
                    run(sbank, start, world, std)
        except Unsupported as exc:
            rows.append((name.lower(), None, str(exc)))
            refused += 1
            continue
        unknown = sorted(f for f in world.flags if f not in flagno)
        if unknown:
            rows.append((name.lower(), None, "no number for %s" % unknown[0]))
            refused += 1
            continue
        if own is not None:
            world.flags &= own
        rows.append((name.lower(), world, ""))
        evaluated += 1

    rows.sort(key=lambda r: r[0])
    lines = [
        "# MAPSCENES, GENERATED by tools/gen_mapscenes.py; DO NOT EDIT.",
        "#",
        "# Which of a HeartGold map's people its own arrival script leaves",
        "# HIDDEN when it runs on a save whose story has been cleared:",
        "# std_init's new game, then every variable and flag mmo/STORYEND",
        "# says the story leaves behind, every badge held, and no phone",
        "# book, no party, no clock. The generator lists every read it",
        "# answers.",
        "#",
        "# An object event is created when its eventFlag is CLEAR, in both",
        "# games. The numbers cannot cross (they are HeartGold's), so a port",
        "# drops the object instead: tools/portmap.py carries an object whose",
        "# flag is FLAG_NOTHING or is not named on this map's row, and leaves",
        "# behind the ones a flag here hides. A row names only the flags the",
        "# map's own people carry. `hide=N` is a person the script",
        "# deletes by number on load (HidePerson) and `show=N` one it creates",
        "# whatever its flag says (ShowPerson).",
        "#",
        "# `?` is a map whose arrival script does something this does not",
        "# evaluate, a scene on the doorstep, a warp, a read nobody has",
        "# named. It is not a guess and not a translation, see the refusal",
        "# in mmo/MAPFORMATS.md, so a port of such a map carries only the",
        "# people no flag hides at all, and says so.",
        "#",
        "# %d maps evaluated, %d written down as unevaluated."
        % (evaluated, refused),
        "#",
        "# Rows: hg <name> <NAME=value ... hide=N show=N, or - or ? and why>",
        "",
    ]
    for name, world, why in rows:
        if world is None:
            lines.append("hg  %-38s ?  # %s" % (name, why))
            continue
        toks = ["%s=0x%X" % (f, flagno[f]) for f in sorted(world.flags)]
        toks += ["hide=%d" % n for n in sorted(world.hide)]
        toks += ["show=%d" % n for n in sorted(world.show)]
        if not toks:
            lines.append("hg  %-38s -" % name)
        else:
            lines.append("hg  %-38s %s" % (name, " ".join(toks)))
    lines.append("")
    out.write_text("\n".join(lines))
    print("gen_mapscenes: %d maps evaluated, %d unevaluated -> %s"
          % (evaluated, refused, out))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
