#!/usr/bin/env python3
"""Generate the table that turns a HeartGold script command into this game's."""

from __future__ import annotations

import difflib
import re
import subprocess
import sys
from pathlib import Path

MMO = Path(__file__).resolve().parent.parent

HG_MACROS = "asm/macros/script.inc"
PL_MACROS = "asm/macros/scrcmd.inc"
PL_TABLE = "include/data/scripts/scrcmd.h"
HG_TABLE = "src/data/fieldmap/script_cmd_table.h"

# The movement actions an ApplyMovement block is made of are their own table,
# renumbered between the games the same way the commands were, and named the
# same way twice: both trees' movement.inc spell the same macro names around
# each game's own constant. The pairing is written to mmo/MOVEACT and
# tools/portscript.py rewrites every carried block through it.
HG_MOVES = "asm/macros/movement.inc"
HG_MOVE_CONSTS = "include/constants/movements.h"
PL_MOVES = "asm/macros/movement.inc"
PL_MOVE_CONSTS = "build/pc/geninclude/generated/movement_actions.h"

WIDTH = {"byte": 1, "short": 2, "hword": 2, "word": 4, "long": 4, "int": 4}

# A run is kept on this much evidence and no less. Two independent name matches
# inside it, and not one that says it is somewhere else.
MIN_CONFIRMATIONS = 2

# Pairs the two trees spell differently but whose bodies say the same thing,
# each read in both scrcmd sources before it earned its line. HeartGold's
# BufferPlayersName and this game's BufferPlayerName both write the player's
# name into the message template slot their one operand names.
HAND_PAIRS = {"BufferPlayersName": "BufferPlayerName"}

# Pairs that neither a shared name nor a run's position can make, read in both
# scrcmd sources the same way.
HAND_ROWS = {"ScrCmd_374": "HideObject", "MakeObjectVisible": "ShowObject",
             # The two halves of a scripted fight's ending: CheckBattleWon
             # and CheckWonBattle both write IsBattleResultWin(win flag) into
             # the var they name; WhiteOut and BlackOutFromBattle both start
             # the field's black-out task and nothing else.
             "CheckBattleWon": "CheckWonBattle",
             "WhiteOut": "BlackOutFromBattle",
             # The bag's two writes, read in both bodies: item var, count var,
             # the result into a var. GiveItem is Bag_AddItem and AddItem is
             # Bag_TryAddItem; TakeItem is Bag_TakeItem and RemoveItem is
             # Bag_TryRemoveItem. This client's bag is a display of the
             # server's, so either write is reported as a delta and the
             # server's answer is what stays (openmmo_bag.c).
             "GiveItem": "AddItem",
             "TakeItem": "RemoveItem",
             # Item var in, TM-or-HM into a var: Item_IsHMMove/ItemIsTM in
             # both bodies, the two shorts either side of the opcode.
             "ItemIsTMOrHM": "IsItemTMHM",
             # A badge held: the badge number in, PlayerProfile_TestBadgeFlag
             # there and TrainerInfo_HasBadge here into the var. On a ported
             # map the mod answers it from the server's world state instead
             # (patches/src/scrcmd_system_flags.c), because the sixteen
             # badges of Johto and Kanto are not TrainerInfo's eight.
             "CheckBadge": "CheckBadgeAcquired",
             # The bag asked whether it holds a count of an item: item and
             # count in, Bag_HasItem there and Bag_CanRemoveItem here into
             # the var. The two games number their items the same way
             # (tools/gen_idmap_items.py measured it).
             "HasItem": "CheckItem",
             # The money box, one body each side: create the window at a
             # tile column and row, print the balance into it, delete it
             # (scrcmd_moneybox.c against scrcmd_money.c).
             "ShowMoneyBox": "ShowMoney",
             "HideMoneyBox": "HideMoney",
             "UpdateMoneyBox": "UpdateMoneyDisplay",
             # Money asked and money taken. The immediate pair reads a word
             # (GetMoney < amount into a var; SubMoney), the var one a
             # short; TrainerInfo_Money and TrainerInfo_TakeMoney here.
             "HasEnoughMoneyImmediate": "CheckMoney",
             "SubMoneyImmediate": "RemoveMoney",
             "SubMoneyVar": "RemoveMoney2",
             # The party pick behind a name rater or a move tutor: open the
             # party menu in its pick-one mode and wait on the application;
             # read the slot it picked (7 there is rewritten to 255, and
             # 0xFF is what this game's menu answers for nobody); return to
             # the field; and whether the picked Pokemon's ot id is somebody
             # else's, both write false for one's own.
             "PartySelectUI": "SelectMoveTutorPokemon",
             "GetPartySelection": "GetSelectedPartySlot",
             "RestoreOverworld": "ReturnToField",
             "PartyMonIsMine": "CheckIsPartyMonOutsider",
             # A person put down somewhere, facing somewhere: local id, x, y,
             # z and a direction into MapObject_SetPositionFromXYZAndDirection
             # there and MapObject_SetPosDirFromCoords here, the height
             # recomputed by both.
             "MovePersonFacing": "SetPosition",
             # A party Pokemon's nickname into a template slot: a byte for
             # the slot, a var for the party index, BufferBoxMonNickname /
             # StringTemplate_SetNickname on the box half of the Pokemon.
             "BufferPartyMonNick": "BufferPartyMonNickname",
             # A random number under a bound into a var: LCRandom() % modulo
             # there, LCRNG_Next() % upperBound here, and both yield the
             # frame after.
             "Random": "GetRandom"}

# Classes the two macro files cannot say and both bodies do. HeartGold's
# assembler calls these operands `arg0`, so the alignment could only call
# them foreign; the bodies read the number as a local object id:
# ShowPerson is this game's AddObject (create the map's object event by
# local id), and the two visibility commands above take the same id.
HAND_CLASSES = {"ShowPerson": "local", "ScrCmd_374": "local",
                "MakeObjectVisible": "local",
                # Two special vars, read and written at runtime: HeartGold's
                # macro calls them arg0 and arg1, which says nothing.
                "ItemIsTMOrHM": "story,story",
                # The rows paired by hand above, each operand read in both
                # bodies: an item or a badge number is a value (the two games
                # agree on both numberings), a var the answer lands in is
                # `story` (a runtime slot the fold tracks), a person is local.
                "CheckBadge": "value,story",
                "HasItem": "value,value,story",
                "ShowMoneyBox": "value,value",
                "HasEnoughMoneyImmediate": "story,value",
                "SubMoneyImmediate": "value",
                "SubMoneyVar": "story",
                "GetPartySelection": "story",
                "PartyMonIsMine": "story,story",
                "MovePersonFacing": "local,value,value,value,value",
                "BufferPartyMonNick": "value,story",
                "Random": "story,story",
                # HeartGold's movement-type command: a local object id and
                # the type, read in that body (ScriptGetVar, ScriptReadHalfword)
                # and in this game's SetMovementType. The source's sixteen
                # uses set the follower (253) to one of its own gaits, which
                # this game's patched command hands to the follower's own
                # (mods/openmmo/src/openmmo_follow_move.c), or a person to a
                # type both games number the same way.
                "ScrCmd_109": "local,value",
                # The follower's own commands (OPENMMO_ROWS below), each
                # operand read in HeartGold's body: an immediate is a value,
                # a var the answer lands in is `story`.
                "ScrCmd_599": "plain", "ScrCmd_600": "plain",
                "FollowMonFacePlayer": "plain",
                "ToggleFollowingPokemonMovement": "value",
                "WaitFollowingPokemonMovement": "plain",
                "FollowingPokemonMovement": "value",
                "ScrCmd_605": "value,value", "ScrCmd_606": "plain",
                "ScrCmd_607": "plain", "ScrCmd_608": "plain",
                "ScrCmd_609": "plain", "ScrCmd_729": "story",
                "ScrCmd_730": "story", "SetFollowMonInhibitState": "value",
                "ScrCmd_596": "story", "ScrCmd_597": "plain",
                "ScrCmd_598": "value", "FollowerPokeIsEventTrigger": "value,story,story",
                "GetFollowPokePartyIndex": "story"}

# HEARTGOLD'S follower commands, and the opcodes of this game they ride on.
OPENMMO_ROWS = {
    "ScrCmd_599": 0x04, "ScrCmd_600": 0x05, "FollowMonFacePlayer": 0x06,
    "ToggleFollowingPokemonMovement": 0x07,
    "WaitFollowingPokemonMovement": 0x08, "FollowingPokemonMovement": 0x09,
    "ScrCmd_605": 0x0A, "ScrCmd_606": 0x0B, "ScrCmd_607": 0x0C,
    "ScrCmd_608": 0x0D, "ScrCmd_609": 0x0E, "ScrCmd_729": 0x0F,
    "ScrCmd_730": 0x10, "SetFollowMonInhibitState": 0x13,
    "ScrCmd_596": 0x17, "ScrCmd_597": 0x18, "ScrCmd_598": 0x19,
    "FollowerPokeIsEventTrigger": 0x6E, "GetFollowPokePartyIndex": 0x6A,
}

# What this game's own macro calls an operand -> what a port may do with it.
#
#   local     a map-local object id. The port keeps an object's local id, so
#             the number still names the same person.
#   message   an id into the map's own message bank, which the port carries
#             whole, the two games' banks are the same container and their
#             charmaps are identical (2,871 characters, every one at the same
#             code), so a message crosses without transcoding.
#   jump      an offset relative to the word after itself: self-relative, so it
#             means the same thing wherever the script lands.
#   story     a flag or a variable of the source game's story. Its number is
#             meaningless here, and the port does not carry it: a ported region
#             is a world where none of that story has happened, so a read is
#             folded to its initial value and a write is dropped.
#   value     an immediate. It is data, not an index.
OPERAND = {
    "localid": "local", "objectid": "local", "trainerid": "foreign",
    "messageid": "message", "msgid": "message",
    "offset": "jump", "movementoffset": "jump", "dest": "jump",
    "flagid": "story", "flag": "story", "varid": "story", "var": "story",
    "value": "value", "val": "value", "condition": "value",
    "frames": "value", "countdownvarid": "story",
    "seqid": "sound", "sound": "sound",
    # Presentation immediates: a fade's style, pace and colour, a facing. Each
    # is data the command consumes as-is, not an index into anything numbered
    # per game; naming them is what lets FadeScreen's row out of `foreign`.
    "transition": "value", "speed": "value", "direction": "value",
    "color": "value", "type": "value",
    # A message-template buffer index, and an item id, the item list is the
    # same national table at the same numbers in both games, so the id is
    # data here, not an index numbered per game.
    "slot": "value", "templatearg": "value", "item": "value",
    # A bag pocket number, the same eight in both games (POCKET_*).
    "pocket": "value",
    # The signpost window's command, which one macro apiece names `command`
    # and `cmd`. Both games read the byte and store it in the same field of
    # the same object, and the five values line up exactly: 0 nothing,
    # 1 draw, 2 scroll out, 3 scroll in, 4 remove (HeartGold's
    # MAPSIGNCOMMAND_* against this game's SIGNPOST_CMD_*, measured
    # 2026-08-31). Left foreign, the command that opens a signpost was
    # dropped from every ported sign and the script that remained waited on
    # a window nothing had drawn, which is why no signpost in Johto or
    # Kanto could be read.
    "command": "value", "cmd": "value",
    # The rest of the signpost family's operands, whose names belong to it
    # alone in both trees. The message is one of the MAP'S OWN, carried whole
    # beside it; the board index is the picture a map-or-arrow sign puts in
    # the window (ignored for the other two styles, and this game's own
    # picture at that number otherwise); the `out` var is VAR_RESULT, which
    # is 0x800C in both games.
    "message": "message", "map": "value", "out": "value",
}


def die(msg: str) -> None:
    print("gen_scripts: " + msg, file=sys.stderr)
    raise SystemExit(2)


def decomp_dir(name: str) -> Path | None:
    try:
        out = subprocess.run([str(MMO / "tools" / "decomp_dir.sh"), name],
                             capture_output=True, text=True, check=True)
    except (OSError, subprocess.CalledProcessError):
        return None
    return Path(out.stdout.strip())


def macros(path: Path) -> dict[str, tuple[str, list[int], list[str]]]:
    """macro name -> (opcode as written, operand widths, operand names)."""
    out: dict[str, tuple[str, list[int], list[str]]] = {}
    for m in re.finditer(r"^\s*\.macro\s+(\w+)([^\n]*)\n(.*?)^\s*\.endm",
                         path.read_text(), re.S | re.M):
        name, args, body = m.group(1), m.group(2), m.group(3)
        args = [a.strip().lstrip("\\") for a in args.split(",") if a.strip()]
        emits = []
        for d in re.finditer(r"^\s*\.(byte|short|hword|word|long|int)\s+([^\n;]+)",
                             body, re.M):
            emits.append((WIDTH[d.group(1)], d.group(2).strip()))
        if not emits or emits[0][0] != 2:
            continue
        widths = [w for w, _ in emits[1:]]
        # An operand's name is the macro argument it interpolates, so that a
        # `\localID-.-4` and a `\localID` are the same name and different uses.
        names = []
        for _, expr in emits[1:]:
            hit = re.search(r"\\(\w+)", expr)
            base = hit.group(1) if hit else ""
            names.append("offset" if "-.-" in expr else base)
        out[name] = (emits[0][1], widths, names)
    return out


def pl_commands(pl: Path) -> dict[str, tuple[int, list[int]]]:
    """This game's own commands by macro name: the opcode and the operand widths."""
    pl_m = macros(pl / PL_MACROS)
    pl_names = [m.group(1) for m in re.finditer(
        r"^ScriptCommand\(\s*(\w+)\s*,", (pl / PL_TABLE).read_text(), re.M)]
    pl_num = {n: i for i, n in enumerate(pl_names)}
    out: dict[str, tuple[int, list[int]]] = {}
    for name, (op, widths, _names) in pl_m.items():
        i = pl_num.get(op)
        if i is not None:
            out[name] = (i, widths)
    return out


# The movement-action tables' own lengths, measured rather than assumed:
# HeartGold's `gMovementCmdTable` is one .word per action and this game's
# `gMovementActionFuncs` one row per action.
HG_MOVE_TABLE = "asm/unk_data_020FCBD8.s"
PL_MOVE_TABLE = "src/unk_020EDBAC.c"
NUMBERED_MOVE = re.compile(r"^MoveAction_\d+$")

# The two names the trees chose differently for one body, each read in both
# games before it earned its line.
HAND_MOVE_PAIRS = {"NurseJoyBow": "PokecenterNurseBow",
                   "EmoteQuestionMark": "EmoteDoubleExclamationMark"}


def move_tables(hg: Path, pl: Path) -> tuple[int, int]:
    """How many actions each game's dispatch table has."""
    src = (hg / HG_MOVE_TABLE).read_text()
    at = src.index("\ngMovementCmdTable:")
    n_hg = 0
    for line in src[at + 1:].splitlines()[1:]:
        if not re.match(r"^\s*\.word\s+gMovementCmdSteps_", line):
            break
        n_hg += 1
    body = (pl / PL_MOVE_TABLE).read_text()
    body = body.split("gMovementActionFuncs[MAX_MOVEMENT_ACTION]", 1)[1]
    body = body.split("};", 1)[0]
    n_pl = len(re.findall(r"^\s*\[MOVEMENT_ACTION_\w+\]\s*=", body, re.M))
    if n_hg == 0 or n_pl == 0:
        die("a movement action table moved (%s, %s)"
            % (hg / HG_MOVE_TABLE, pl / PL_MOVE_TABLE))
    return n_hg, n_pl


def move_pairs(hg: Path, pl: Path) -> list[tuple[int, int, str]]:
    """Movement action pairs, by the shared macro name."""
    def consts(path: Path) -> dict[str, int]:
        out = {}
        for m in re.finditer(r"(?:#define\s+|^\s*)(MOVEMENT_\w+)\s*=?\s*(\d+)",
                             path.read_text(), re.M):
            out.setdefault(m.group(1), int(m.group(2)))
        return out

    def macro_codes(path: Path, names: dict[str, int]) -> dict[str, int]:
        # HeartGold's macros spell most of their codes as the bare number
        # (`.short 4`) and only a few through constants/movements.h; a reader
        # that took only the named ones paired 18 of 112 and let the rest
        # through on the assumption that the numbering agreed.
        out = {}
        for m in re.finditer(r"^\s*\.macro\s+(\w+)[^\n]*\n\s*\.short\s+(\w+)",
                             path.read_text(), re.M):
            word = m.group(2)
            code = names.get(word)
            if code is None and word.isdigit():
                code = int(word)
            if code is not None:
                out.setdefault(m.group(1), code)
        return out

    n_hg, _n_pl = move_tables(hg, pl)
    hg_codes = macro_codes(hg / HG_MOVES, consts(hg / HG_MOVE_CONSTS))
    pl_codes = macro_codes(pl / PL_MOVES, consts(pl / PL_MOVE_CONSTS))
    pairs = {n: n for n in hg_codes.keys() & pl_codes.keys()
             if not NUMBERED_MOVE.match(n)}
    for h, p in HAND_MOVE_PAIRS.items():
        if h in hg_codes and p in pl_codes:
            pairs[h] = p
    # A macro past the source's own table (EmoteExclamation2 at 153) is a
    # name with no row behind it in its own game and does not cross.
    return sorted((hg_codes[n], pl_codes[p], n)
                  for n, p in pairs.items() if hg_codes[n] < n_hg)


def main(argv: list[str]) -> int:
    hg = Path(argv[1]) if len(argv) > 1 else decomp_dir("pokeheartgold")
    pl = Path(argv[2]) if len(argv) > 2 else decomp_dir("pokeplatinum")
    out = Path(argv[3]) if len(argv) > 3 else MMO / "SCRCMD"
    if hg is None or pl is None:
        die("need both checkouts; pass them or set DECOMP_DIR")

    hg_m = macros(hg / HG_MACROS)
    pl_m = macros(pl / PL_MACROS)

    # This game numbers its commands by their position in the table the engine
    # indexes; the source game writes the number into the macro.
    pl_names = [m.group(1) for m in re.finditer(
        r"^ScriptCommand\(\s*(\w+)\s*,", (pl / PL_TABLE).read_text(), re.M)]
    pl_num = {n: i for i, n in enumerate(pl_names)}
    n_hg = len((hg / HG_TABLE).read_text().split("gScriptCmdTable[] = {", 1)[1]
               .split("};", 1)[0].strip().rstrip(",").split(","))

    def slot(table, n, resolve):
        rows: list[tuple[str, list[int], list[str]] | None] = [None] * n
        for name, (op, widths, names) in table.items():
            i = resolve(op)
            if i is not None and 0 <= i < n and rows[i] is None:
                rows[i] = (name, widths, names)
        return rows

    def as_int(op):
        try:
            return int(op, 0)
        except ValueError:
            return None

    hg_rows = slot(hg_m, n_hg, as_int)
    pl_rows = slot(pl_m, len(pl_names), lambda op: pl_num.get(op))
    if not any(hg_rows) or not any(pl_rows):
        die("neither macro file yielded an opcode; the format moved")

    sig = lambda r: ",".join(str(x) for x in r[1]) if r else "?"
    blocks = difflib.SequenceMatcher(
        None, [sig(r) for r in hg_rows], [sig(r) for r in pl_rows],
        autojunk=False).get_matching_blocks()

    # Every macro name spelt the same in both trees is one assertion about
    # where a command went. They are the evidence and they are also the veto.
    claim = {}
    for i, r in enumerate(hg_rows):
        if r is None:
            continue
        other = HAND_PAIRS.get(r[0], r[0])
        if other in pl_m and pl_m[other][0] in pl_num:
            claim[i] = pl_num[pl_m[other][0]]

    runs, pair = [], {}
    for bl in blocks:
        if not bl.size:
            continue
        off = bl.b - bl.a
        ops = range(bl.a, bl.a + bl.size)
        for_ = [o for o in ops if claim.get(o) == o + off]
        against = [o for o in ops if o in claim and claim[o] != o + off]
        if len(for_) < MIN_CONFIRMATIONS or against:
            continue
        runs.append((bl.a, bl.a + bl.size - 1, off, bl.size, for_, against))
        for o in ops:
            pair[o] = o + off

    # A name spelt identically in both trees outside every kept run is still
    # both oracles, applied to one command: the two teams' word for it agrees
    # And every operand width agrees. A run needs two names because its other
    # members are carried on position alone; a singleton carries nobody else,
    # so one name is the whole claim and the widths are its check.
    singles = []
    for o, p in sorted(claim.items()):
        if o in pair:
            continue
        h, prow = hg_rows[o], pl_rows[p]
        if h is None or prow is None or h[1] != prow[1]:
            continue
        # A numbered name (ScrCmd_258) is a team labelling an unknown by its
        # own game's index; two of those agreeing is a coincidence of counting,
        # not a word two teams chose. Only a chosen word is a claim.
        if re.fullmatch(r"ScrCmd_\d+", h[0]):
            continue
        pair[o] = p
        singles.append(o)

    bodies = []
    for o, r in enumerate(hg_rows):
        if r is None or o in pair or r[0] not in HAND_ROWS:
            continue
        other = HAND_ROWS[r[0]]
        if other not in pl_m or pl_m[other][0] not in pl_num:
            die("HAND_ROWS names %s, which this game's macros do not" % other)
        p = pl_num[pl_m[other][0]]
        prow = pl_rows[p]
        if prow is None or prow[1] != r[1]:
            die("%s and %s do not take the same operands; the hand pair is "
                "stale" % (r[0], other))
        pair[o] = p
        bodies.append(o)

    ours = []
    hg_by_name = {r[0]: o for o, r in enumerate(hg_rows) if r is not None}
    for name, p in OPENMMO_ROWS.items():
        o = hg_by_name.get(name)
        if o is None:
            die("OPENMMO_ROWS names %s, which the source's macros do not" % name)
        prow = pl_rows[p] if p < len(pl_rows) else None
        if prow is None or not prow[0].startswith("ScrCmd_Unused_"):
            die("opcode %d is %s, not one this game leaves unused; %s needs "
                "another slot" % (p, prow[0] if prow else "empty", name))
        if name not in HAND_CLASSES:
            die("%s has no class in HAND_CLASSES" % name)
        # The alignment paired the source's own unused command at the same
        # number with it; that pairing was `foreign` and carried nothing.
        for other, q in list(pair.items()):
            if q == p:
                del pair[other]
        pair[o] = p
        ours.append(o)

    def operand_class(pl_row, hg_row) -> str:
        """What a port may do with each of this command's operands, in order."""
        if not pl_row[1]:
            return "plain"
        kinds = []
        for a, b in zip(pl_row[2], hg_row[2]):
            ka = OPERAND.get(a.lower(), "foreign")
            kb = OPERAND.get(b.lower(), "foreign")
            if ka == "foreign" and kb == "foreign":
                return "foreign"
            if ka != "foreign" and kb != "foreign" and ka != kb:
                return "foreign"
            kinds.append(ka if ka != "foreign" else kb)
        return ",".join(kinds)

    lines = [
        "# SCRCMD, GENERATED by tools/gen_scripts.py; DO NOT EDIT.",
        "#",
        "# A HeartGold field-script command -> this game's, where two oracles",
        "# agree that they are the same command. tools/portscript.py reads it.",
        "#",
        "# THE ALIGNMENT. Both games' assemblers say the exact width of every",
        "# operand, and lining the two sequences of widths up leaves a handful",
        "# of long runs at a constant offset. A run is written down here only",
        "# when at least %d macro names spelt identically in both trees fall"
        % MIN_CONFIRMATIONS,
        "# inside it and NOT ONE contradicts it. The runs kept:",
        "#",
    ]
    for s, e, off, n, for_, against in runs:
        lines.append("#   hg %3d..%-3d -> pl %3d..%-3d  %3d long, confirmed by %d: %s"
                     % (s, e, s + off, e + off, n, len(for_),
                        ", ".join(hg_rows[o][0] for o in for_[:4])
                        + (" ..." if len(for_) > 4 else "")))
    if singles:
        lines.append("#")
        lines.append("# And %d singleton(s), paired on an identically spelt name"
                     % len(singles))
        lines.append("# plus every operand width agreeing: %s"
                     % ", ".join(hg_rows[o][0] for o in singles[:8])
                     + (" ..." if len(singles) > 8 else ""))
    if bodies:
        lines.append("#")
        lines.append("# And %d pair(s) read in both bodies (tools/gen_scripts.py"
                     % len(bodies))
        lines.append("# HAND_ROWS), with every operand width agreeing: %s"
                     % ", ".join("%s -> %s" % (hg_rows[o][0], HAND_ROWS[hg_rows[o][0]])
                                 for o in bodies))
    if ours:
        lines.append("#")
        lines.append("# And %d of the source's follower commands, carried on"
                     % len(ours))
        lines.append("# opcodes this game never used (tools/gen_scripts.py")
        lines.append("# OPENMMO_ROWS); the widths are the source's, and the")
        lines.append("# client's patched body reads them: %s"
                     % ", ".join("%s -> %d" % (hg_rows[o][0], pair[o])
                                 for o in ours))
    lines += [
        "#",
        "# THE OPERAND CLASS says what a port may do with the numbers, and a",
        "# command with a `foreign` one is in no ported script: `local` is a",
        "# map object this port keeps the id of, `message` an id into the map's",
        "# own bank (carried whole, the two charmaps are identical), `jump`",
        "# an offset relative to itself, `story` a flag or variable of a story",
        "# this port does not carry, `sound` a sequence in an archive numbered",
        "# by the game it came from, `value` an immediate, `plain` no operands.",
        "# One class per operand, in order, comma separated.",
        "#",
        "# THE OPERAND WIDTHS are carried so that nothing downstream needs a",
        "# decompilation to walk a script: the porter has a cartridge and this",
        "# table, and that is the whole of what reading one takes.",
        "#",
        "# Rows: hg <op> <pl op> <widths> <class> <heartgold> <platinum>",
        "#",
        "# A row whose pl op is `-` pairs with nothing: it exists so the",
        "# porter knows the command's exact WIDTH and can walk past it when",
        "# tools/portscript.py's drop lists say the command may be dropped.",
        "# One is never emitted into a ported script.",
        "",
    ]
    kept = 0
    for o in sorted(pair):
        h, p = hg_rows[o], pl_rows[pair[o]]
        if h is None or p is None:
            continue
        widths = ",".join(str(w) for w in h[1]) or "-"
        lines.append("hg  %4d %4d  %-9s %-22s %-34s %s"
                     % (o, pair[o], widths,
                        HAND_CLASSES.get(h[0], operand_class(p, h)), h[0],
                        "openmmo:" + p[0] if o in ours else p[0]))
        kept += 1
    unpaired = 0
    for o, r in enumerate(hg_rows):
        if r is None or o in pair:
            continue
        widths = ",".join(str(w) for w in r[1]) or "-"
        kinds = ",".join(OPERAND.get(n.lower(), "foreign") for n in r[2]) \
            if r[1] else "plain"
        lines.append("hg  %4d    -  %-9s %-22s %-34s -"
                     % (o, widths, kinds, r[0]))
        unpaired += 1
    lines.append("")
    out.write_text("\n".join(lines))

    classes: dict[str, int] = {}
    for o in sorted(pair):
        h, p = hg_rows[o], pl_rows[pair[o]]
        if h and p:
            c = operand_class(p, h)
            classes[c] = classes.get(c, 0) + 1
    moves = move_pairs(hg, pl)
    if not moves:
        die("no movement action pairs; a macro file or constants header moved")
    n_hg_moves, n_pl_moves = move_tables(hg, pl)
    mv = [
        "# MOVEACT, GENERATED by tools/gen_scripts.py; DO NOT EDIT.",
        "#",
        "# A HeartGold movement action -> this game's, paired on the macro",
        "# name both trees chose for it (two spelt differently are paired by",
        "# hand, bodies read). tools/portscript.py rewrites every ApplyMovement",
        "# block it carries through this table; the source's own actions past",
        "# the last row are lowered there (MOVE_LOWER), and a block naming an",
        "# action neither knows refuses its whole entry.",
        "#",
        "# Rows: tables <hg count> <pl count>; mv <hg> <pl> <name>",
        "",
        "tables %d %d" % (n_hg_moves, n_pl_moves),
        "",
    ]
    for h, pnum, name in moves:
        mv.append("mv  %4d %4d  %s" % (h, pnum, name))
    mv.append("")
    (out.parent / "MOVEACT").write_text("\n".join(mv))
    print("gen_scripts: %d of %d source commands paired over %d confirmed "
          "run(s) and %d singleton(s); %d width-only rows besides"
          % (kept, n_hg, len(runs), len(singles), unpaired))
    print("gen_scripts: %d movement action(s) paired by name -> %s"
          % (len(moves), out.parent / "MOVEACT"))
    print("gen_scripts: %s -> %s"
          % (", ".join("%d %s" % (v, k) for k, v in sorted(classes.items())), out))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
