#!/usr/bin/env python3
"""Turn a HeartGold map's field scripts into scripts this game runs."""

from __future__ import annotations

import re
import struct
from pathlib import Path

SCRIPT_TABLE_END = 0xFD13
MOVEMENT_END = 254
MOVEMENT_RECORD = 4          # a u16 action and a u16 count, in both games

# The movement actions inside a carried block.
MOVE_DELAY = 63              # MOVEMENT_ACTION_DELAY_8


def _move_table() -> tuple[dict[int, int], int, int]:
    pairs: dict[int, int] = {}
    n_hg = n_pl = 0
    path = Path(__file__).resolve().parent.parent / "MOVEACT"
    if path.is_file():
        for line in path.read_text().splitlines():
            m = re.match(r"^mv\s+(\d+)\s+(\d+)\s", line)
            if m:
                pairs[int(m.group(1))] = int(m.group(2))
            m = re.match(r"^tables\s+(\d+)\s+(\d+)", line)
            if m:
                n_hg, n_pl = int(m.group(1)), int(m.group(2))
    return pairs, n_hg, n_pl


_MOVES, MOVE_MAX_HG, MOVE_MAX_PL = _move_table()

# The leaps, as this game's own jumps.
_JUMP_FAR = {"N": 56, "S": 57, "W": 58, "E": 59}
_JUMP_NEAR = {"N": 52, "S": 53, "W": 54, "E": 55}


def _axis(n: int, pos: str, neg: str) -> list[tuple[int, int]]:
    if n == 0:
        return []
    d = pos if n > 0 else neg
    n = abs(n)
    out = []
    if n // 2:
        out.append((_JUMP_FAR[d], n // 2))
    if n % 2:
        out.append((_JUMP_NEAR[d], 1))
    return out


def _hops(*hops: tuple[int, int, str]) -> list[tuple[int, int]]:
    out: list[tuple[int, int]] = []
    for dx, dz, face in hops:
        x = _axis(dx, "E", "W")
        z = _axis(dz, "S", "N")
        out += (z + x) if face in "EW" else (x + z)
    return out


MOVE_LOWER: dict[int, list[tuple[int, int]]] = {
    105: _hops((0, 5, "S"), (4, 0, "E")),
    106: _hops((2, 0, "E"), (-1, 5, "S")),
    107: _hops((3, -1, "E"), (0, 4, "S")),
    108: _hops((2, 5, "S"), (0, 5, "S")),
    109: _hops((3, -1, "E"), (0, 4, "S")),
    110: _hops((2, 4, "S"), (0, 5, "S")),
    111: _hops((0, 2, "S"), (2, 0, "E")),
    112: _hops((4, 0, "E"), (4, 0, "E")),
}


def port_movement(blk: bytes) -> bytes | None:
    """A carried block in this game's actions, or None when the source names
    an action its own table does not have; a block off the end of a
    113-entry table is corruption, not a beat to keep."""
    out = bytearray()
    for off in range(0, len(blk), MOVEMENT_RECORD):
        act, count = struct.unpack_from("<HH", blk, off)
        if act == MOVEMENT_END:
            out += struct.pack("<HH", act, count)
        elif act in _MOVES:
            out += struct.pack("<HH", _MOVES[act], count)
        elif act in MOVE_LOWER:
            for _ in range(count):
                for a, c in MOVE_LOWER[act]:
                    out += struct.pack("<HH", a, c)
        else:
            return None
    return bytes(out)

# How far a fold may run before it is called a loop. The longest Johto script
# that folds is far under this; the cap is what makes a mistake finite.
STEP_CAP = 512

# The STORY, folded.
def _compare(a: int, b: int) -> int:
    return 0 if a < b else (1 if a == b else 2)


STORY_READ = {"CompareVarToValue", "CompareVarToVar", "CheckFlag",
              "CheckTrainerFlag"}

# The SPECIAL variables are not the STORY.
VAR_SPECIAL = 0x8000


def _scratch(v: int) -> bool:
    """A var the fold tracks and this game keeps at the same slot: a special
    (0x8000 and up) or one of the 32 map-local temps at 0x4000, which both
    games memset on every map change and neither saves, HeartGold's
    VAR_TEMP_x4000 is this game's VAR_TEMP_0. Anything else is the story's."""
    return v >= VAR_SPECIAL \
        or MAP_LOCAL_VAR_FIRST <= v < MAP_LOCAL_VAR_FIRST + MAP_LOCAL_VAR_COUNT

# This game's map-local flags: MAP_LOCAL_FLAGS_START..END in its
# generated/vars_flags.txt, 1..64, memset by FieldSystem_ClearLocalFlags on
# every map change before the arrival script runs, and skipped by the mod's
# story report. A ported map's own clock flags live here, one slot per source
# flag per map (Fold._clock_flags).
MAP_LOCAL_FLAG_FIRST = 1
MAP_LOCAL_FLAG_COUNT = 64
# And the map-local vars beside them, 0x4000..0x401F, cleared by the same
# memset: the one place an arrival script may keep the hour, because it runs
# before any script context exists to hold a 0x8000 local (measured
# 2026-09-02: VAR_RESULT there is an assertion in
# FieldSystem_GetScriptMemberPtr and a segfault after it).
MAP_LOCAL_VAR_FIRST = 0x4000
MAP_LOCAL_VAR_COUNT = 32
# The arrival-script table entry that runs on entering a map, in both games
# (include/constants/init_script_types.h).
INIT_SCRIPT_ON_TRANSITION = 2

# dst-first var writes; all but SetOrCopyVar land on a real command here
# (its slot is Unused_02A there, so it is tracked and dropped).
VAR_WRITES = {"SetVar", "CopyVar", "AddVar", "SubVar", "SetOrCopyVar"}
VAR_WRITE_EMITS = {"SetVar", "CopyVar", "AddVar", "SubVar"}

# Writes to a story nothing reads. Dropping them is the same statement as
# folding the reads: the story is frozen where it started.
STORY_WRITE = {
    "SetFlag", "ClearFlag", "SetTrainerFlag", "ClearTrainerFlag",
    "AddVar", "SubVar", "SetVar", "CopyVar", "SetOrCopyVar",
    "SetVarFromValue", "SetVarFromVar",
    # A badge of a story this port does not carry. This game has the
    # command, but its badge 0 is the Coal Badge and the source's is the
    # Zephyr, so a visitor is not handed one; CheckBadge answers none, and
    # the leader who gave it is there to be fought again.
    "GiveBadge",
    # A phone number written into the Pokegear, which this game does not
    # have; the line that says it was registered still plays.
    "RegisterGearNumber",
    # ShowPerson is this game's AddObject (mmo/SCRCMD pairs them on their
    # bodies), and it is still dropped: the source only ever calls it on a
    # person its story had hidden, and in a world where no flag is set
    # every such person is already standing there. AddObject does not ask
    # whether an object exists before creating one, so emitting it would
    # put a second hiker on Route 42 for the scene to walk away with.
    # HidePerson crosses, because hiding somebody who then leaves is the
    # scene's point.
    "ShowPerson",
}

# LEFT BEHIND, for the same reason a story write is dropped: the number in it
# is an index into an archive numbered by the game it came from, and this port
# carries no mapping for it.
DROPPABLE = {"sound", "value"}

# HeartGold's own commands a fold may walk past, each read in that game's
# scrcmd sources before it earned its line. Two shapes and no third:
ANSWERS = {
    "GetTrainerNum", "GetPlayerState", "GetTrcardStars", "GetPartyLeadAlive",
    "GetWeekday", "GetPlayerFacing",
    "CheckRegisteredPhoneNumber", "GetItemPocket", "VermilionGymCanCheck",
    "GetPartyCount", "LoadPhoneDat",
    # A list of lines to choose from is MENU_LIST below, answered with its
    # last row (the way out). Its sibling MenuInit is on the presentation
    # list, the two differ only in which message bank the rows are read
    # from (NULL for the standard one).
    # Writes an object's movement type into a var and nothing else.
    "ScrCmd_574", "TrainerIsDoubleBattle", "GetPlayerCoords",
    # Field_GetTimeOfDay into a var. Where it picks a greeting it is
    # Lowered instead (_time_of_day below) and this line never sees it;
    # the sites that reach it write a temporary the fold cannot carry, and
    # for those zero, morning, is as good as any answer.
    "ScrCmd_379",
    # Whether anything in the party carries Pokerus; the party is the
    # server's and the answer feeds one extra nurse line.
    "PartyHasPokerus",
    # A flag whose number sits in a var (CheckFlagVar) is still a flag of a
    # story that never ran: clear. A Pokeathlon record (ScrCmd_724), a
    # fashion accessory owned (ScrCmd_255, the Goldenrod tunnel shop) and a
    # rematch waiting in the phone book (GetPhoneBookRematch) are all
    # answers about a save this visitor does not have, and zero is what an
    # empty one says.
    "CheckFlagVar", "ScrCmd_724", "ScrCmd_255", "GetPhoneBookRematch",
    # The friend's sprite into an OBJ var: the hero of the other gender
    # (scrcmd_c.c, ScrCmd_GetFriendSprite), and the person wearing that var
    # is one the porter leaves behind anyway. Whether the day-care holds an
    # egg (scrcmd_daycare.c): a visitor left nothing there.
    "GetFriendSprite", "CheckDaycareEgg",
}

# PRESENTATION, dropped the way a lone sound is: the command paints, poses or
# animates and changes nothing a later line reads. The avatar pose pair, the
# healing-machine and save-screen dressing, the touchscreen frame, the
# signpost box, a follower nudge, a hush of the music. Losing one costs the
# flourish and keeps the person.
PRESENTATION = {
    "SetAvatarBits", "UpdateAvatarState",
    "TouchscreenMenuHide", "TouchscreenMenuShow",
    "ShowSaveStats", "ScrCmd_436",
    # The follower's own commands are not here: a Pokemon walks behind the
    # player on a ported map as it does on its own cartridge, and every one
    # of HeartGold's twenty follower commands is carried on an opcode this
    # game never used (mmo/SCRCMD, gen_scripts.OPENMMO_ROWS).
    "StopBGM", "FadeOutBGM",
    "MenuInit", "MenuItemAdd", "PlayCry",
    "EncounterMusic",
    # Reads a var and throws it away; the official debugger's leftover.
    "DebugWatch",
    # Records three heap sizes on 0 and asserts they came back on 1: the
    # official leak check around a scene, and nothing a player sees.
    "ScrCmd_682",
    # A camera shake with four numbers behind it; this game shakes objects,
    # not the screen, so the tremble is lost and the scene keeps its words.
    "ScreenShake",
    # A movement type set on arrival, the old man on Route 34 looks about
    # or stands still by whether the day-care holds an egg. The person keeps
    # the movement his event row gives him.
    "SetObjectMovementType",
    # A Trainer Card statistic bumped on a badge or a record; nothing on the
    # screen and nothing a later line reads.
    "AddSpecialGameStat", "AddSpecialGameStat2",
    # Puts the list up and waits on it. No operands of its own, the answer
    # goes where MenuInitStdGmm said, and that command above is what answers
    # it, so dropping this drops the wait and nothing else.
    "MenuExec",
}

# The signpost family is not a flourish, and calling it one is why no sign in
# Johto or Kanto could be read: a signpost's window is its content.

# The control flow this folds through. Everything else is a straight command.
GOTO, GOTO_IF, CALL, CALL_IF, RETURN, END = (
    "GoTo", "GoToIf", "Call", "CallIf", "Return", "End")
# By OPCODE, not by name, and the reason is the hand-back.
RELEASE_ALL_OP = 97
# `ScriptContext_GetVar` in both games: an operand at or past 0x4000 names a
# variable, below it is the number itself.
VAR_ANY = 0x4000
# The last species a scripted fight may name: the Gen 5 tables' end.
SPECIES_MAX = 649
COMMON_RETURN_OP = 21
END_OP = 2

# A call into the common script bank.
CALL_STD = "CallStd"
STD_MISC_BASE = 2000
STD_PORT_BASE = 20000

# WHO SELLS. A clerk is not a kind of person in either game, it is a script
# that reaches one of two shared routines: `std_pokemart` puts up the ordinary
# shelf, `std_special_mart` a numbered one.
STD_POKEMART = 2048
STD_SPECIAL_MART = 2052
MART_SHOP_VAR = 0x8004

# A gift is this game'S own routine.
STD_TO_PL_COMMON = {2033: 2044, 2008: 2016}

# The one question answered YES. HasSpaceForItem is asked before every gift
# (the GoToIfNoItemSpace macro), and a folded question answers zero, which
# here means "the bag is full", so every gift folded to its refusal. The bag
# is the server's and its answer comes back with the delta; the script's own
# check is told there is room.
ANSWER_YES = {"HasSpaceForItem",
              # A cleared save has entered the Hall of Fame: the story's
              # gates on it are open for a visitor. Its badges are the
              # character's own (LIVE_DEST below): a leader stands to be
              # fought until the server says the badge is held.
              "CheckGameClearFlag"}

# The questions a visitor ANSWERS at RUNTIME, kept as the runtime reads they
# are: each is paired with this game's command of the same body (mmo/SCRCMD
# HAND_ROWS), writes its answer into the operand named here, and the compare
# that follows is kept too, with both arms folded (Fold._fork).
LIVE_DEST = {"CheckBadge": 1, "HasItem": 2, "HasEnoughMoneyImmediate": 0,
             "GetItemQuantity": 1, "GetPartyMonSpecies": 1,
             "PartyMonIsMine": 1, "GetPartySelection": 0}
# HeartGold's key items: the Explorer Kit through the Mystery Egg, the
# Photo Album, GB Sounds and Tidal Bell, the Data Cards, the orbs and the
# Enigma Stone (include/constants/items.h). The Apricorns, the balls and
# the RageCandyBar sit inside that range and are ordinary items.
KEY_ITEMS = set(range(428, 485)) | {501, 502, 503} | set(range(505, 537))
# The one branch a fold keeps besides the yes/no and the fight: a compare of
# a runtime answer, emitted as the compare and the jump, its taken arm folded
# as a tail past the entry's End (_lower_forks). Capped per entry so a script
# that asks four questions in a row stays finite.
FORK_MARK = "fork-branch"
FORK_CAP = 4

# A runtime answer that is neither zero nor yes: GetPartySlotWithFateful-
# Encounter writes the slot of a fateful-encounter Pokemon of a species, and
# 255 for none, which a visitor's party, read for a Shaymin, is taken to
# have none of. (dest operand, value)
ANSWER_VALUE = {"GetPartySlotWithFatefulEncounter": (0, 255)}

# A list of lines to choose from: MenuInitStdGmm opens it, MenuItemAdd rows
# follow (message, column, value), MenuExec waits, and a Switch (CopyVar into
# 0x8008) with one Case (Compare, GoToIf) per row branches on the answer.
MENU_LIST = "MenuInitStdGmm"
MENU_ROW = "MenuItemAdd"
MENU_EXEC = "MenuExec"
MART_COMMANDS = ("MartBuy", "SpecialMartBuy")

# The MAP A WARP names.
WARP = "Warp"
PERSON_TO = "MovePersonFacing"
TRAIN_RIDE = "ScrCmd_722"
DIR_SOUTH = 1

# The one question a fold may keep. HeartGold's GetMenuChoice is the yes/no
# ask: its body shows the fixed two-button touch prompt (ov01_021F6ABC with 3,
# 3) and waits.
YESNO_MARK = "yesno-branch"

# The hour, and the one shape of it worth carrying.
TIME_OF_DAY = "ScrCmd_379"
PL_GETTIMEOFDAY = 438

# This game's own two mart commands, counted the same way out of
# include/data/scripts/scrcmd.h.
PL_MART_COMMON = 327
PL_MART_SPECIAL = 328
COND_EQ, COND_NE = 1, 5                     # sConditionTable, both games
TOD_ARM_CAP = 8

# The line that depends on who is asking. HeartGold's GenderMsgBox reads two
# message ids and prints one of them by the player's gender; its body is
# NPCMsg's with the id picked first.
GENDER_MSG = "GenderMsgBox"
VAR_RESULT = 0x800C
GENDER_MALE = 0                             # PLAYER_GENDER_MALE, both games

# The fight A SCRIPT names.
TRAINER_BATTLE = "TrainerBattle"
BATTLE_WON = "CheckBattleWon"
PL_STARTTRAINERBATTLE = 229
BATTLE_MARK = "battle-branch"

# `sConditionTable[6][3]`, from both games' scrcmd.c, identical, and the
# reason a folded branch is a decision rather than a guess.
CONDITION = (
    (1, 0, 0),   # <
    (0, 1, 0),   # ==
    (0, 0, 1),   # >
    (1, 1, 0),   # <=
    (0, 1, 1),   # >=
    (1, 0, 1),   # !=
)

ROW = re.compile(
    r"^hg\s+(\d+)\s+(\d+|-)\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s*$")


class Command:
    __slots__ = ("hg", "pl", "classes", "hg_name", "pl_name", "widths")

    def __init__(self, hg, pl, classes, hg_name, pl_name, widths):
        self.hg = hg
        self.pl = pl
        self.classes = classes
        self.hg_name = hg_name
        self.pl_name = pl_name
        self.widths = widths


def load_table(path: Path) -> dict[int, Command]:
    """mmo/SCRCMD: every command a port may carry, and what its operands are."""
    if not path.is_file():
        raise SystemExit("portscript: no %s; run tools/gen_scripts.py" % path)
    out: dict[int, Command] = {}
    for line in path.read_text().splitlines():
        m = ROW.match(line)
        if not m:
            continue
        hg = int(m.group(1))
        pl = None if m.group(2) == "-" else int(m.group(2))
        widths = [] if m.group(3) == "-" else [int(x) for x in m.group(3).split(",")]
        classes = [] if m.group(4) == "plain" else m.group(4).split(",")
        # Every row loads: the widths are what walking a script takes, and
        # the drop lists judge by name before any class is asked. Whether a
        # command may be emitted is decided in the fold, a `foreign` class
        # or a `-` pairing refuses there, with the command named. A bare
        # `foreign` covers however many operands the widths say.
        if classes == ["foreign"] and len(widths) > 1:
            classes = ["foreign"] * len(widths)
        if len(widths) != len(classes) and classes:
            continue
        out[hg] = Command(hg, pl, classes, m.group(5), m.group(6), widths)
    return out


def entry_offsets(member: bytes) -> tuple[list[int], int]:
    """Where each script in a member starts, and where the table ends."""
    out: list[int] = []
    p = 0
    while p + 4 <= len(member):
        if struct.unpack_from("<H", member, p)[0] == SCRIPT_TABLE_END:
            return out, p + 2
        off = struct.unpack_from("<i", member, p)[0]
        p += 4
        out.append(p + off)
    return [], len(member)


def movement_block(member: bytes, at: int) -> bytes | None:
    """One applymovement's data: (action, count) pairs up to the terminator."""
    p = at
    while p + MOVEMENT_RECORD <= len(member):
        action = struct.unpack_from("<H", member, p)[0]
        p += MOVEMENT_RECORD
        if action == MOVEMENT_END:
            return member[at:p]
    return None


class Fold:
    """One entry, traced from its start with the story held at its start."""

    def __init__(self, member: bytes, table: dict[int, Command],
                 std_span: int = 0, trainers: dict | None = None,
                 clock: dict | None = None,
                 story: tuple[dict[int, int], dict[int, int]] | None = None,
                 maps: dict | None = None, here: int | None = None):
        self.member = member
        self.table = table
        self.std_span = std_span
        self.trainers = trainers
        # source header -> (this package's header, origin x, origin z), and
        # which source map this member belongs to (WARP, PERSON_TO above).
        self.maps = maps
        self.here = here
        self.origin = (maps[here][1], maps[here][2]) \
            if maps is not None and here in maps else (0, 0)
        # Specials holding a RUNTIME answer (LIVE_DEST): a compare on one is
        # kept, not folded. Shared with the arms this fold forks, and the
        # fork count with them, so the cap is per entry.
        self.live: set[int] = set()
        self.forks = [0]
        self.stack: list[int] = []
        self.seen: set[int] = set()
        # Where the story left every variable and flag (mmo/STORYEND, by
        # number); a var or flag not here reads zero.
        self.story_vars, self.story_flags = story if story else ({}, {})
        # source flag -> this game's map-local flag, shared by every entry of
        # the member so a person's flag and the arrival script agree.
        self.clock = clock
        self.vars: dict[int, int] = {}
        # Flags this walk itself set or cleared. The story is frozen where it
        # started, so a write is dropped, but a script that sets a flag and
        # reads it back a few lines later is asking about its own past, not
        # the story's, and answering "still clear" made three Kanto gym
        # scripts loop: the leader hands over the tm, sets the flag that says
        # so, and tests it to pick the goodbye.
        self.flags: dict[int, int] = dict(self.story_flags)
        self.mart: tuple[bool, int] | None = None
        # The scripted wild fight this entry stages, (species, level), once
        # the walk has reached its WildBattle, the fold ends the scene
        # there and the server deals the fight (static sites, portmap).
        self.static: tuple[int, int] | None = None

    def run(self, start: int, stack: list | None = None,
            seen: set | None = None) -> tuple[list, str]:
        out: list = []
        self.stack = stack if stack is not None else []
        self.seen = seen if seen is not None else set()
        stack, seen = self.stack, self.seen
        cmp_result = 0
        pc = start
        for _ in range(STEP_CAP):
            if pc in seen:
                return [], "a pc reached twice, so the fold does not terminate"
            seen.add(pc)
            if pc < 0 or pc + 2 > len(self.member):
                return [], "a jump outside the member"
            op = struct.unpack_from("<H", self.member, pc)[0]
            cmd = self.table.get(op)
            if cmd is None:
                return [], "command %d has no row in mmo/SCRCMD" % op
            p = pc + 2
            ops = []
            for w in cmd.widths:
                if p + w > len(self.member):
                    return [], "an operand past the end of the member"
                ops.append(int.from_bytes(self.member[p:p + w], "little"))
                p += w
            # A jump operand is relative to the word after itself, and this is
            # where "the word after itself" is.
            after = p

            if cmd.hg_name == END:
                out.append((cmd, ops, None))
                return out, ""
            if cmd.hg_name in STORY_READ:
                if cmd.hg_name == "CompareVarToValue" and ops[0] in self.live:
                    forked = self._fork(cmd, ops, after)
                    if isinstance(forked, str):
                        return [], forked
                    out.extend(forked[0])
                    cmp_result = 0
                    pc = forked[1]
                    continue
                if cmd.hg_name == "CompareVarToVar" \
                        and (ops[0] in self.live or ops[1] in self.live):
                    return [], ("a runtime answer compared against another "
                                "var, which the fold cannot keep")
                if cmd.hg_name == "CompareVarToValue":
                    cmp_result = _compare(self._var(ops[0]), ops[1])
                elif cmd.hg_name == "CompareVarToVar":
                    cmp_result = _compare(self._var(ops[0]), self._var(ops[1]))
                elif cmd.hg_name == "CheckFlag":
                    cmp_result = self.flags.get(ops[0], 0)
                else:
                    cmp_result = 0
                pc = after
                continue
            if cmd.hg_name in STORY_WRITE:
                if cmd.hg_name == "SetFlag":
                    self.flags[ops[0]] = 1
                elif cmd.hg_name == "ClearFlag":
                    self.flags[ops[0]] = 0
                if cmd.hg_name in VAR_WRITES and _scratch(ops[0]):
                    self._write(cmd.hg_name, ops)
                    # A copy of a runtime answer is a runtime answer; any
                    # other write settles the slot.
                    if cmd.hg_name in ("CopyVar", "SetOrCopyVar") \
                            and _scratch(ops[1]) and ops[1] in self.live:
                        self.live.add(ops[0])
                    else:
                        self.live.discard(ops[0])
                    if cmd.hg_name in VAR_WRITE_EMITS:
                        out.append((cmd, ops, None))
                pc = after
                continue
            # The machine beat crosses whole: both games' commands read one
            # ball-count var, and this game's spawns the tray animation,
            # balls, chime and all, against whichever machine prop the
            # equivalence hook vouches for. The count comes from the
            # emitted party count right before it; on a machine the room
            # does not have, the task never starts and the script walks on.
            if cmd.hg_name == "PokeCenAnim":
                play = Command(-1, 571, ["value"], "PokeCenAnim",
                               "PlayPokecenterHealingAnimation", [2])
                out.append((play, [ops[0]], None))
                pc = after
                continue
            # The shelf is the server'S, but the command is this game'S.
            # Read both bodies side by side and they are one routine: the
            # ordinary mart takes an operand neither game uses, counts the
            # badges, walks a nineteen-row table and opens the shop screen
            # on every row at or under the tier. The numbered one indexes an
            # array of lists. So the pairing is by what they do, which is
            # the only oracle a numbered name (ScrCmd_275) leaves open.
            #
            # What the operand means still differs: the numbered shelf is the
            # source's own list, and this game's table at that index is a
            # different shop. The mod's patch on both commands is what settles
            # it, on a ported map the press goes to the server, which owns
            # the bag, the money and both games' shelves, and the number here
            # never chooses anything on its own.
            if cmd.hg_name in ("MartBuy", "SpecialMartBuy"):
                special = cmd.hg_name == "SpecialMartBuy"
                mart = Command(-1, PL_MART_SPECIAL if special else PL_MART_COMMON,
                               ["value"], cmd.hg_name,
                               "PokeMartSpecialties" if special
                               else "PokeMartCommon", [2])
                #
                # And the shop screen is where the scene ends. What follows a
                # mart in the source is a line out of a message bank this port
                # does not carry and a jump back to the buy/sell menu, which is
                # a loop no straight line can hold. It is also not this game's
                # clerk: PokeMartCommonWithGreeting is greeting, mart, release,
                # and the shop screen carries buying, selling and leaving on its
                # own. So the fold ends the scene here, on that shape, with the
                # source's own greeting already said.
                # Release, HAND back, end. The hand-back is not decoration:
                # CallCommonScript pauses its caller until something clears
                # the sub-context flag, and only ReturnCommonScript clears it,
                # so an End alone leaves the clerk's own script paused for
                # the rest of the session, the field never settles, and the
                # player cannot walk away. Outside a common bank the same
                # command clears a flag nobody set and costs nothing.
                out.append((mart, [ops[0]], None))
                for op in (RELEASE_ALL_OP, COMMON_RETURN_OP, END_OP):
                    done = self.table.get(op)
                    if done is None:
                        return [], "command %d has no row in mmo/SCRCMD" % op
                    out.append((done, [], None))
                return out, ""
            # A scripted wild fight is the server'S, and the scene ends
            # Where it starts. The source stages twenty-one of them,
            # Sudowoodo on Route 36, the Snorlax, the Red Gyarados, the
            # birds, the beasts, Mewtwo, as `WildBattle SPECIES, level`
            # after the lines and the movement that lead up to it, and
            # branches on the outcome after. A battle is dealt by the
            # server here, never staged by the client, so the fold keeps
            # the lead-up, ends the scene on the fight's doorstep with the
            # field released, and names the fight (species, level) to the
            # porter, which marks the person as a static site: the press
            # goes to the server as every press on a ported person does,
            # the server starts the fight, and the mod takes the person off
            # the map when it is won (openmmo_static.c). Nothing after the
            # WildBattle crosses: it branches on an outcome the client does
            # not decide. A species or a level the walk cannot name, the
            # Bell Tower's and the Whirl Islands' come out of a variable
            # a version check writes, refuses the entry as before.
            if cmd.hg_name == "WildBattle":
                species = self._var(ops[0]) if ops[0] >= VAR_ANY else ops[0]
                level = self._var(ops[1]) if ops[1] >= VAR_ANY else ops[1]
                if not (1 <= species <= SPECIES_MAX) or not (1 <= level <= 100):
                    return [], ("a scripted fight whose species or level "
                                "the fold cannot name")
                for op in (RELEASE_ALL_OP, END_OP):
                    done = self.table.get(op)
                    if done is None:
                        return [], "command %d has no row in mmo/SCRCMD" % op
                    out.append((done, [], None))
                self.static = (species, level)
                return out, ""
            if cmd.hg_name == "PartyCountNotEgg":
                count = Command(-1, 375, ["value"], "PartyCountNotEgg",
                                "GetPartyCount", [2])
                out.append((count, [ops[0]], None))
                self.vars.pop(ops[0], None)
                pc = after
                continue
            if cmd.hg_name == "GetMenuChoice":
                ask = Command(-1, 62, ["value"], "GetMenuChoice",
                              "ShowYesNoMenu", [2])
                mark = Command(-1, -1, [], YESNO_MARK, "", [])
                out.append((ask, [ops[0]], None))
                out.append((mark, [ops[0]], None))
                self.vars.pop(ops[0], None)
                pc = after
                continue
            if cmd.hg_name == TIME_OF_DAY and self.clock is not None:
                picked = self._clock_flags(ops[0], after)
                if picked is not None:
                    emit, forget, resume = picked
                    out.extend(emit)
                    for v in forget:
                        self.vars.pop(v, None)
                    pc = resume
                    continue
            if cmd.hg_name == TIME_OF_DAY and _scratch(ops[0]):
                picked = self._time_of_day(ops[0], after)
                if picked is not None:
                    emit, forget, resume = picked
                    out.extend(emit)
                    for v in forget:
                        self.vars.pop(v, None)
                    pc = resume
                    continue
            if cmd.hg_name == GENDER_MSG:
                out.extend(self._gender_line(ops[0], ops[1]))
                self.vars.pop(VAR_RESULT, None)
                pc = after
                continue
            if cmd.hg_name == TRAINER_BATTLE:
                first, second, flag_a, flag_b = ops
                if flag_a or flag_b:
                    return [], ("TrainerBattle carries a battle flag this "
                                "port cannot name")
                if second != 0:
                    return [], "TrainerBattle is a double this port does not lower"
                if self.trainers is None or first not in self.trainers:
                    return [], ("TrainerBattle names a trainer this run did "
                                "not carry")
                fight = Command(-1, PL_STARTTRAINERBATTLE, ["value", "value"],
                                TRAINER_BATTLE, "StartTrainerBattle", [2, 2])
                out.append((fight, [self.trainers[first], 0], None))
                out.append((Command(-1, -1, [], BATTLE_MARK, "", []), [], None))
                pc = after
                continue
            if cmd.hg_name == BATTLE_WON:
                # Folded as WON: the line the fold walks is the winner's, and
                # the mark above is where the loser leaves it at runtime.
                if _scratch(ops[0]):
                    self.vars[ops[0]] = 1
                    self.live.discard(ops[0])
                pc = after
                continue
            # The drop lists judge by name and outrank the pairing: a runtime
            # answer stays dropped even where both games have the command,
            # because the world this fold makes answers every question with
            # zero and an emitted read would disagree with a folded branch.
            # A special the dropped command mentioned is unknown from here.
            if cmd.hg_name in LIVE_DEST and cmd.pl is not None:
                live = self._live(cmd, ops)
                if isinstance(live, str):
                    return [], live
                out.extend(live)
                pc = after
                continue
            if cmd.hg_name in ANSWER_VALUE:
                dest, value = ANSWER_VALUE[cmd.hg_name]
                for v in ops:
                    if _scratch(v):
                        self.vars.pop(v, None)
                        self.live.discard(v)
                if _scratch(ops[dest]):
                    self.vars[ops[dest]] = value
                pc = after
                continue
            if cmd.hg_name == MENU_LIST:
                value = self._menu_answer(after)
                for v in ops:
                    if _scratch(v):
                        self.vars.pop(v, None)
                        self.live.discard(v)
                if ops and _scratch(ops[-1]):
                    self.vars[ops[-1]] = value
                pc = after
                continue
            if cmd.hg_name in ANSWER_YES:
                for v in ops[:-1]:
                    if _scratch(v):
                        self.vars.pop(v, None)
                        self.live.discard(v)
                if ops and _scratch(ops[-1]):
                    self.vars[ops[-1]] = 1
                    self.live.discard(ops[-1])
                pc = after
                continue
            if cmd.hg_name in ANSWERS or cmd.hg_name in PRESENTATION:
                for v in ops:
                    if _scratch(v):
                        self.vars.pop(v, None)
                        self.live.discard(v)
                pc = after
                continue
            if cmd.hg_name == WARP and cmd.pl is not None:
                dest = self.maps.get(ops[0]) if self.maps is not None else None
                if dest is None:
                    return [], "Warp to a map this package does not carry"
                hdr, ox, oy = dest
                if ops[2] < ox or ops[3] < oy:
                    return [], ("Warp lands outside the region's cut-out "
                                "(%d,%d against origin %d,%d)"
                                % (ops[2], ops[3], ox, oy))
                out.append((cmd, [hdr, ops[1], ops[2] - ox, ops[3] - oy,
                                  ops[4]], None))
                pc = after
                continue
            if cmd.hg_name == PERSON_TO and cmd.pl is not None:
                ox, oy = self.origin
                if ops[1] < ox or ops[3] < oy:
                    return [], ("%s puts a person outside the region's "
                                "cut-out (%d,%d against origin %d,%d)"
                                % (cmd.hg_name, ops[1], ops[3], ox, oy))
                out.append((cmd, [ops[0], ops[1] - ox, ops[2], ops[3] - oy,
                                  ops[4]], None))
                pc = after
                continue
            if cmd.hg_name == TRAIN_RIDE:
                ride = self._train_ride(ops)
                if isinstance(ride, str):
                    return [], ride
                out.extend(ride)
                pc = after
                continue
            if cmd.pl is None:
                return [], ("%s is HeartGold's own and not on a drop list"
                            % cmd.hg_name)
            if "sound" in cmd.classes:
                if not set(cmd.classes) <= DROPPABLE:
                    return [], ("%s names a sound and does something else too"
                                % cmd.hg_name)
                pc = after
                continue
            if cmd.hg_name == GOTO:
                pc = after + _signed(ops[-1])
                continue
            if cmd.hg_name == CALL:
                stack.append(after)
                pc = after + _signed(ops[-1])
                continue
            if cmd.hg_name == RETURN:
                if not stack:
                    return [], "a return with nothing to return to"
                pc = stack.pop()
                continue
            if cmd.hg_name == CALL_STD:
                sid = ops[0]
                if sid in (STD_POKEMART, STD_SPECIAL_MART):
                    self.mart = (sid == STD_SPECIAL_MART,
                                 self.vars.get(MART_SHOP_VAR, 0))
                if sid in STD_TO_PL_COMMON:
                    out.append((cmd, [STD_TO_PL_COMMON[sid]], None))
                    pc = after
                    continue
                if not (STD_MISC_BASE <= sid < STD_MISC_BASE + self.std_span):
                    return [], ("CallStd %d reaches a std bank this port "
                                "does not carry" % sid)
                out.append((cmd, [STD_PORT_BASE + (sid - STD_MISC_BASE)], None))
                pc = after
                continue
            if cmd.hg_name in (GOTO_IF, CALL_IF):
                cond = ops[0]
                if cond >= len(CONDITION):
                    return [], "condition %d is past the table" % cond
                if CONDITION[cond][cmp_result]:
                    if cmd.hg_name == CALL_IF:
                        stack.append(after)
                    pc = after + _signed(ops[-1])
                else:
                    pc = after
                continue
            # A `story` or `foreign` operand may still cross by VALUE: a
            # number at or above 0x8000 is a special var slot, the same slot
            # in both games, and the command reads or writes it at runtime;
            # so is a map-local temp (_scratch). Anything else is a saved
            # story's number, and stays refused.
            for i, kind in enumerate(cmd.classes):
                if kind in ("story", "foreign"):
                    if not _scratch(ops[i]):
                        return [], ("%s carries a number this port cannot "
                                    "name" % cmd.hg_name)
                    self.vars.pop(ops[i], None)
                    self.live.discard(ops[i])
            # A straight command. Its jump operands are data pointers rather
            # than control flow, an applymovement's block, so the block is
            # carried and the pointer is remade at assembly.
            blocks = []
            for i, kind in enumerate(cmd.classes):
                if kind == "jump":
                    at = after + _signed(ops[i])
                    blk = movement_block(self.member, at)
                    if blk is None:
                        return [], "%s points at data with no end" % cmd.hg_name
                    ported = port_movement(blk)
                    if ported is None:
                        return [], ("%s carries a movement action past the "
                                    "source's own table" % cmd.hg_name)
                    blocks.append((i, ported))
            out.append((cmd, ops, blocks or None))
            pc = after
        return [], "longer than the %d-step cap" % STEP_CAP


    def _child(self) -> "Fold":
        """A fold of one arm: the same member and world, the state so far."""
        child = Fold(self.member, self.table, self.std_span, self.trainers,
                     self.clock, (self.story_vars, self.story_flags),
                     self.maps, self.here)
        child.vars = dict(self.vars)
        child.flags = dict(self.flags)
        child.live = set(self.live)
        child.forks = self.forks
        child.mart = self.mart
        child.static = self.static
        return child

    def _fork(self, cmd, ops: list, after: int):
        """A compare of a runtime answer, kept with both of its arms."""
        nxt = self._decode(after)
        if nxt is None:
            return "a runtime compare at the end of the member"
        ncmd, nops, nafter = nxt
        if ncmd.hg_name != GOTO_IF:
            return ("a runtime answer compared but not branched on (%s "
                    "follows)" % ncmd.hg_name)
        cond = nops[0]
        if cond >= len(CONDITION):
            return "condition %d is past the table" % cond
        again = self._decode(nafter)
        if again is not None and again[0].hg_name in (GOTO_IF, CALL_IF):
            return "a runtime compare branched on twice"
        if self.forks[0] >= FORK_CAP:
            return ("more runtime branches than the fold keeps (%d)"
                    % FORK_CAP)
        self.forks[0] += 1
        target = nafter + _signed(nops[-1])
        child = self._child()
        arm, why = child.run(target, list(self.stack), set(self.seen))
        if why:
            return why
        if child.mart is not None:
            self.mart = child.mart
            self.static = child.static
        mark = Command(-1, -1, [], FORK_MARK, "", [1, 4])
        return [(cmd, ops, None), (mark, [cond, 0], arm)], nafter

    def _live(self, cmd, ops: list):
        """A runtime read emitted as itself, its answer marked live."""
        dest = LIVE_DEST[cmd.hg_name]
        if cmd.hg_name == "HasItem" and not _scratch(ops[0]) \
                and ops[0] in KEY_ITEMS:
            for v in ops:
                if _scratch(v):
                    self.vars.pop(v, None)
                    self.live.discard(v)
            if _scratch(ops[dest]):
                self.vars[ops[dest]] = 1
            return []
        for i, v in enumerate(ops):
            if i != dest and cmd.classes[i] in ("story", "foreign") \
                    and not _scratch(v):
                return "%s carries a number this port cannot name" % cmd.hg_name
        if not _scratch(ops[dest]):
            return "%s answers into a var this port cannot name" % cmd.hg_name
        for i, v in enumerate(ops):
            if i != dest and _scratch(v):
                self.vars.pop(v, None)
                self.live.discard(v)
        self.vars.pop(ops[dest], None)
        self.live.add(ops[dest])
        return [(cmd, ops, None)]

    def _menu_answer(self, at: int) -> int:
        """The row a list menu is answered with: the row whose Case opens a
        mart, else the last row's value (MENU_LIST above)."""
        value = 0
        while True:
            step = self._decode(at)
            if step is None or step[0].hg_name != MENU_ROW:
                break
            value = step[1][2]
            at = step[2]
        # MenuExec, then the Switch: a CopyVar into the switch var, and a
        # Case per row, Compare against the row's value, GoToIf to its arm.
        step = self._decode(at)
        if step is None or step[0].hg_name != MENU_EXEC:
            return value
        step = self._decode(step[2])
        if step is None or step[0].hg_name != "CopyVar":
            return value
        at = step[2]
        while True:
            cmp_ = self._decode(at)
            if cmp_ is None or cmp_[0].hg_name != "CompareVarToValue":
                return value
            br = self._decode(cmp_[2])
            if br is None or br[0].hg_name != GOTO_IF:
                return value
            target = self._decode(br[2] + _signed(br[1][-1]))
            if target is not None and target[0].hg_name in MART_COMMANDS:
                return cmp_[1][1]
            at = br[2]

    def _train_ride(self, ops: list):
        """The magnet train, as a fade, the far platform and a fade back."""
        by = {c.hg_name: c for c in self.table.values() if c.pl is not None}
        fade, wait, warp = by.get("FadeScreen"), by.get("WaitFade"), by.get(WARP)
        if fade is None or wait is None or warp is None:
            return "mmo/SCRCMD no longer pairs the fade or the warp"
        dest = self.maps.get(ops[2]) if self.maps is not None else None
        if dest is None:
            return "the train ride names a platform this package does not carry"
        hdr, ox, oy = dest
        if ops[3] < ox or ops[4] < oy:
            return "the train ride lands outside the region's cut-out"
        return [(fade, [6, 1, 0, 0], None), (wait, [], None),
                (warp, [hdr, 0, ops[3] - ox, ops[4] - oy, DIR_SOUTH], None),
                (fade, [6, 1, 1, 0], None), (wait, [], None)]

    def _decode(self, at: int):
        """One command where it sits: (cmd, ops, the word after it)."""
        if at < 0 or at + 2 > len(self.member):
            return None
        cmd = self.table.get(struct.unpack_from("<H", self.member, at)[0])
        if cmd is None:
            return None
        p = at + 2
        ops = []
        for w in cmd.widths:
            if p + w > len(self.member):
                return None
            ops.append(int.from_bytes(self.member[p:p + w], "little"))
            p += w
        return cmd, ops, p

    def _skip_flourish(self, at: int) -> int:
        """Past anything the fold would drop for painting and nothing else."""
        while True:
            step = self._decode(at)
            if step is None or step[0].hg_name not in PRESENTATION:
                return at
            at = step[2]

    def _time_of_day(self, var: int, at: int):
        """The hour's greeting, carried to runtime instead of folded away."""
        arms: list[tuple[int, int]] = []
        dst = None
        meet = None                         # where every arm jumps
        tail = None                         # where the default lands
        default = None
        for _ in range(TOD_ARM_CAP):
            at = self._skip_flourish(at)
            step = self._decode(at)
            if step is None or step[0].hg_name != "SetVar":
                return None
            if step[1][0] < VAR_SPECIAL or (dst is not None and step[1][0] != dst):
                return None
            dst, value, after_set = step[1][0], step[1][1], step[2]

            probe = self._skip_flourish(after_set)
            step = self._decode(probe)
            if step is None or step[0].hg_name != "CompareVarToValue" \
                    or step[1][0] != var:
                # Nothing compares behind it, so this assignment is the
                # default and the line every arm jumps to starts after it.
                default, tail = value, after_set
                break
            hour, after_cmp = step[1][1], step[2]

            step = self._decode(after_cmp)
            if step is None or step[0].hg_name != GOTO_IF \
                    or step[1][0] != COND_EQ:
                return None
            dest = step[2] + _signed(step[1][-1])
            if any(hour == k for k, _v in arms):
                return None
            arms.append((hour, value))
            if meet is None:
                meet = dest
            elif dest != meet:
                return None
            at = step[2]
        else:
            return None                     # a chain with no end to it
        if not arms or default is None or meet is None or meet != tail:
            return None

        set_cmd = next(c for c in self.table.values() if c.hg_name == "SetVar")
        cmp_cmd = next(c for c in self.table.values()
                       if c.hg_name == "CompareVarToValue")
        br_cmd = next(c for c in self.table.values() if c.hg_name == GOTO_IF)
        ask = Command(-1, PL_GETTIMEOFDAY, ["value"], TIME_OF_DAY,
                      "GetTimeOfDay", [2])
        over = 2 + sum(set_cmd.widths)
        emit = [(set_cmd, [dst, default], None), (ask, [var], None)]
        for hour, value in arms:
            emit.append((cmp_cmd, [var, hour], None))
            emit.append((br_cmd, [COND_NE, over & 0xFFFFFFFF], None))
            emit.append((set_cmd, [dst, value], None))
        return emit, (var, dst), meet

    def _flag_arm(self, at: int, stop: int | None):
        """One arm of a clock test: flag writes and how it ends."""
        writes: dict[int, bool] = {}
        for _ in range(TOD_ARM_CAP):
            if stop is not None and at == stop:
                return writes, ("goto", stop)
            at = self._skip_flourish(at)
            step = self._decode(at)
            if step is None:
                return None
            cmd, ops, after = step
            if cmd.hg_name in ("SetFlag", "ClearFlag"):
                writes[ops[0]] = cmd.hg_name == "SetFlag"
                at = after
                continue
            if cmd.hg_name == END:
                return writes, ("end", at)
            if cmd.hg_name == GOTO:
                return writes, ("goto", after + _signed(ops[-1]))
            return None
        return None

    def _clock_flags(self, var: int, at: int):
        """The hour's cast, carried to runtime: who is out by day, who by night."""
        if self.clock is None:
            return None
        if not (MAP_LOCAL_VAR_FIRST <= var
                < MAP_LOCAL_VAR_FIRST + MAP_LOCAL_VAR_COUNT):
            return None
        buckets: list[int] = []
        meet = None
        p = at
        for _ in range(TOD_ARM_CAP):
            step = self._decode(p)
            if step is None or step[0].hg_name != "CompareVarToValue" \
                    or step[1][0] != var:
                break
            hour, after_cmp = step[1][1], step[2]
            step = self._decode(after_cmp)
            if step is None or step[0].hg_name != GOTO_IF \
                    or step[1][0] != COND_EQ:
                return None
            dest = step[2] + _signed(step[1][-1])
            if meet is None:
                meet = dest
            elif dest != meet:
                return None
            if hour in buckets:
                return None
            buckets.append(hour)
            p = step[2]
        if not buckets or meet is None:
            return None
        day = self._flag_arm(p, None)
        if day is None:
            return None
        day_writes, day_end = day
        night = self._flag_arm(meet, day_end[1] if day_end[0] == "goto"
                               else None)
        if night is None:
            return None
        night_writes, night_end = night
        if day_end[0] != night_end[0]:
            return None
        if day_end[0] == "goto" and day_end[1] != night_end[1]:
            return None
        resume = day_end[1]
        for f in list(day_writes) + list(night_writes):
            if f not in self.clock:
                if len(self.clock) >= MAP_LOCAL_FLAG_COUNT:
                    return None
                self.clock[f] = MAP_LOCAL_FLAG_FIRST + len(self.clock)
        day_set = {f for f, on in day_writes.items() if on}
        night_set = {f for f, on in night_writes.items() if on}

        set_row = next(c for c in self.table.values() if c.hg_name == "SetFlag")
        clr_row = next(c for c in self.table.values()
                       if c.hg_name == "ClearFlag")
        set_f = Command(-1, set_row.pl, ["value"], "SetFlag", "SetFlag", [2])
        clr_f = Command(-1, clr_row.pl, ["value"], "ClearFlag", "ClearFlag", [2])
        cmp_cmd = next(c for c in self.table.values()
                       if c.hg_name == "CompareVarToValue")
        br_cmd = next(c for c in self.table.values() if c.hg_name == GOTO_IF)
        ask = Command(-1, PL_GETTIMEOFDAY, ["value"], TIME_OF_DAY,
                      "GetTimeOfDay", [2])
        emit = [(set_f, [self.clock[f]], None) for f in sorted(day_set)]
        other = ([(clr_f, [self.clock[f]]) for f in sorted(day_set - night_set)]
                 + [(set_f, [self.clock[f]]) for f in sorted(night_set - day_set)])
        if other:
            over = sum(2 + sum(c.widths) for c, _o in other)
            emit.append((ask, [var], None))
            for hour in buckets:
                emit.append((cmp_cmd, [var, hour], None))
                emit.append((br_cmd, [COND_NE, over & 0xFFFFFFFF], None))
                emit.extend((c, o, None) for c, o in other)
        return emit, (var,), resume

    def _gender_line(self, male: int, female: int) -> list:
        """One line of dialogue picked by the player's gender, at runtime."""
        by_name = {c.hg_name: c for c in self.table.values()}
        ask = by_name["GetPlayerGender"]
        say = by_name["NPCMsg"]
        cmp_cmd = by_name["CompareVarToValue"]
        br_cmd = by_name[GOTO_IF]
        over = 2 + sum(say.widths)
        return [
            (ask, [VAR_RESULT], None),
            (cmp_cmd, [VAR_RESULT, GENDER_MALE], None),
            (br_cmd, [COND_NE, over & 0xFFFFFFFF], None),
            (say, [male], None),
            (cmp_cmd, [VAR_RESULT, GENDER_MALE], None),
            (br_cmd, [COND_EQ, over & 0xFFFFFFFF], None),
            (say, [female], None),
        ]

    def _var(self, vid: int) -> int:
        if _scratch(vid):
            return self.vars.get(vid, 0)
        return self.story_vars.get(vid, 0)

    def _write(self, name: str, ops: list) -> None:
        dst, src_op = ops[0], ops[1]
        if name == "SetVar":
            self.vars[dst] = src_op & 0xFFFF
        elif name in ("CopyVar", "SetOrCopyVar"):
            if name == "SetOrCopyVar" and not _scratch(src_op):
                self.vars[dst] = src_op & 0xFFFF
            elif _scratch(src_op) and src_op in self.vars:
                self.vars[dst] = self.vars[src_op]
            elif not _scratch(src_op):
                self.vars[dst] = self.story_vars.get(src_op, 0)
            else:
                self.vars.pop(dst, None)
        elif name in ("AddVar", "SubVar"):
            if dst in self.vars and not _scratch(src_op):
                delta = src_op if name == "AddVar" else -src_op
                self.vars[dst] = (self.vars[dst] + delta) & 0xFFFF
            else:
                self.vars.pop(dst, None)


def _signed(v: int) -> int:
    return v - 0x100000000 if v >= 0x80000000 else v


def assemble(traces: list[list | None]) -> bytes:
    """A script archive of this game's own: a table, the entries, the data."""
    end_op = None
    for tr in traces:
        for cmd, _ops, _b in (tr or []):
            if cmd.hg_name == END:
                end_op = cmd.pl
    if end_op is None:
        end_op = 2                              # SCRCMD_END, in both games

    bodies: list[bytearray] = []
    fixups: list[list[tuple[int, bytes]]] = []
    for tr in traces:
        body = bytearray()
        fix: list[tuple[int, bytes]] = []
        if tr is None:
            body += struct.pack("<H", end_op)
        else:
            for cmd, ops, blocks in tr:
                body += struct.pack("<H", cmd.pl)
                blk = dict(blocks or [])
                for i, w in enumerate(cmd.widths):
                    if i in blk:
                        fix.append((len(body), blk[i]))
                        body += b"\0" * w
                    else:
                        body += ops[i].to_bytes(w, "little")
        bodies.append(body)
        fixups.append(fix)

    table = 4 * len(bodies) + 2
    at = table
    starts = []
    for b in bodies:
        starts.append(at)
        at += len(b)
    # The data every applymovement points at, once each, behind the code.
    data: dict[bytes, int] = {}
    for fix in fixups:
        for _, blk in fix:
            if blk not in data:
                data[blk] = at
                at += len(blk)

    out = bytearray()
    for i, s in enumerate(starts):
        # The offset is relative to the word after this one.
        out += struct.pack("<i", s - (4 * (i + 1)))
    out += struct.pack("<H", SCRIPT_TABLE_END)
    for i, b in enumerate(bodies):
        base = starts[i]
        for at_in_body, blk in fixups[i]:
            # A data pointer is relative to the word after itself too.
            here = base + at_in_body
            struct.pack_into("<i", b, at_in_body, data[blk] - (here + 4))
        out += b
    for blk in data:
        out += blk
    return bytes(out)


# What a folded arrival script may consist of and still run on every entry to
# a ported map: the hour, its flags, and the End. Anything else that survived
# the fold is a scene evaluated in a world where its reason never happened,
# and the map keeps the empty table it has always had.
ARRIVAL_INERT = {END, TIME_OF_DAY, "CompareVarToValue", GOTO_IF,
                 "SetFlag", "ClearFlag"}

# What a scene may end on with a message box up: the waits, the closes, the
# releases and the returns. The no-branch of a lowered question lands on the
# start of this suffix, and the open-box repair inserts just before it.
SAFE_TAIL = {"WaitButton", "CloseMsg", "HoldMsg", "ReleaseAll",
             "RestartCurrentScript", "End"}
MSG_PRINTS = {"NonNPCMsg", "NPCMsg", "NonNPCMsgVar", "NPCMsgVar", "ScrCmd_048"}


def _tail_start(trace: list) -> int:
    i = len(trace)
    while i > 0 and trace[i - 1][0].hg_name in SAFE_TAIL:
        i -= 1
    return i


def _close_open_box(trace: list, table: dict[int, Command],
                    common: bool = False,
                    open_calls: set | None = None,
                    open_start: bool = False) -> list:
    """A box nobody closes is a scene that ends mid-sentence and replays."""
    wait_cmd = table.get(50)
    close_cmd = table.get(53)
    if wait_cmd is None or close_cmd is None or common:
        return trace
    # A dangling HoldMsg first: the official client holds the text for the mart UI to
    # live under, and with the mart refused the held box outlived the
    # scene and painted over the field. The last hold with no close
    # behind it becomes the wait-and-close it was standing in for.
    last_hold = None
    for i, (c, _o, _b) in enumerate(trace):
        if c.hg_name == "HoldMsg":
            last_hold = i
        elif c.hg_name == "CloseMsg":
            last_hold = None
    if last_hold is not None:
        trace = trace[:last_hold] + [(wait_cmd, [], None),
                                     (close_cmd, [], None)] \
            + trace[last_hold + 1:]
    open_ = open_start
    for c, ops, _b in trace:
        if c.hg_name in MSG_PRINTS:
            open_ = True
        elif c.pl == 20 and open_calls is not None \
                and (ops[0] - STD_PORT_BASE) in open_calls:
            open_ = True
        elif c.hg_name == "CloseMsg":
            open_ = False
    if not open_:
        return trace
    tail = _tail_start(trace)
    return trace[:tail] + [(wait_cmd, [], None), (close_cmd, [], None)] \
        + trace[tail:]


def _lower_yesno(trace: list, table: dict[int, Command]) -> list:
    """The runtime no-branch behind every kept question."""
    if not any(c.hg_name == YESNO_MARK for c, _o, _b in trace):
        return trace
    cmp_cmd = next(c for c in table.values() if c.hg_name == "CompareVarToValue")
    br_cmd = next(c for c in table.values() if c.hg_name == "GoToIf")
    size = lambda c: 2 + sum(c.widths)
    out: list = []
    marks: list[tuple[int, int]] = []
    for c, ops, blocks in trace:
        if c.hg_name == YESNO_MARK:
            marks.append((len(out), ops[0]))
            out.append(None)
            out.append(None)
        else:
            out.append((c, ops, blocks))
    # Each mark's placeholder pair is a compare then a branch, so every
    # entry's byte offset is known before the jumps are written.
    offs: list[int] = []
    at = 0
    i = 0
    while i < len(out):
        offs.append(at)
        if out[i] is None:
            at += size(cmp_cmd)
            i += 1
            offs.append(at)
            at += size(br_cmd)
            i += 1
        else:
            at += size(out[i][0])
            i += 1
    # The no-branch lands where the scene lets go: the start of the trailing
    # waits, closes and releases, so a no still releases the field and still
    # closes the question's own box. An entry with no such tail ends at End.
    tail = len(out)
    while tail > 0 and out[tail - 1] is not None \
            and out[tail - 1][0].hg_name in SAFE_TAIL:
        tail -= 1
    for idx, var in marks:
        tgt = tail if tail > idx + 1 else len(out) - 1
        jump = offs[tgt] - (offs[idx + 1] + size(br_cmd))
        out[idx] = (cmp_cmd, [var, 1], None)
        # condition 1 is ==, sConditionTable in both games
        out[idx + 1] = (br_cmd, [1, jump & 0xFFFFFFFF], None)
    return out


def named_trainers(member: bytes, table: dict[int, Command]) -> set[int]:
    """Every trainer a member's scripts fight by name, in the source's numbers."""
    starts, _ = entry_offsets(member)
    out: set[int] = set()
    seen: set[int] = set()
    todo = list(starts)
    while todo:
        pc = todo.pop()
        while 0 <= pc and pc + 2 <= len(member) and pc not in seen:
            seen.add(pc)
            op = struct.unpack_from("<H", member, pc)[0]
            cmd = table.get(op)
            if cmd is None:
                break
            p = pc + 2
            ops = []
            for w in cmd.widths:
                if p + w > len(member):
                    return out
                ops.append(int.from_bytes(member[p:p + w], "little"))
                p += w
            if cmd.hg_name == TRAINER_BATTLE:
                out.add(ops[0])
                if ops[1]:
                    out.add(ops[1])
            if cmd.hg_name in (GOTO, CALL, GOTO_IF, CALL_IF):
                todo.append(p + _signed(ops[-1]))
                if cmd.hg_name == GOTO:
                    break
            if cmd.hg_name in (END, RETURN):
                break
            pc = p
    return out


def _lower_battle(trace: list, table: dict[int, Command]) -> list:
    """The runtime lost-branch behind every kept fight."""
    if not any(c.hg_name == BATTLE_MARK for c, _o, _b in trace):
        return trace
    by = {c.hg_name: c for c in table.values()}
    won, blackout = by[BATTLE_WON], by["WhiteOut"]
    assert won.pl is not None and blackout.pl is not None, \
        "mmo/SCRCMD no longer pairs the fight's ending; rerun gen_scripts"
    cmp_cmd, br_cmd = by["CompareVarToValue"], by[GOTO_IF]
    release, end = table[RELEASE_ALL_OP], table[END_OP]
    size = lambda c: 2 + sum(c.widths)
    out: list = []
    marks: list[int] = []
    for c, ops, blocks in trace:
        if c.hg_name == BATTLE_MARK:
            marks.append(len(out))
            out.extend([None, None, None])
        else:
            out.append((c, ops, blocks))
    offs: list[int] = []
    at = 0
    i = 0
    while i < len(out):
        if out[i] is None:
            for c in (won, cmp_cmd, br_cmd):
                offs.append(at)
                at += size(c)
            i += 3
        else:
            offs.append(at)
            at += size(out[i][0])
            i += 1
    tail_at = at
    for idx in marks:
        jump = tail_at - (offs[idx + 2] + size(br_cmd))
        out[idx] = (won, [VAR_RESULT], None)
        out[idx + 1] = (cmp_cmd, [VAR_RESULT, 0], None)
        out[idx + 2] = (br_cmd, [COND_EQ, jump & 0xFFFFFFFF], None)
    return out + [(blackout, [], None), (release, [], None), (end, [], None)]


def _lower_forks(trace: list, table: dict[int, Command]) -> list:
    """The runtime jump behind every kept compare."""
    if not any(c.hg_name == FORK_MARK for c, _o, _b in trace):
        return trace
    br_cmd = next(c for c in table.values() if c.hg_name == GOTO_IF)
    size = lambda c: 2 + sum(c.widths)
    main: list = []
    arms: list[tuple[int, int, list]] = []
    for c, ops, blocks in trace:
        if c.hg_name == FORK_MARK:
            arms.append((len(main), ops[0], blocks))
            main.append(None)
        else:
            main.append((c, ops, blocks))
    offs: list[int] = []
    at = 0
    for item in main:
        offs.append(at)
        at += size(br_cmd) if item is None else size(item[0])
    arm_at: list[int] = []
    for _idx, _cond, arm in arms:
        arm_at.append(at)
        at += sum(size(c) for c, _o, _b in arm)
    for k, (idx, cond, _arm) in enumerate(arms):
        jump = arm_at[k] - (offs[idx] + size(br_cmd))
        main[idx] = (br_cmd, [cond, jump & 0xFFFFFFFF], None)
    for _idx, _cond, arm in arms:
        main.extend(arm)
    return main


def _finish(trace: list, table: dict[int, Command], common: bool,
            open_calls: set | None, open_start: bool = False) -> list:
    """Every lowering a folded line needs, arms first."""
    out: list = []
    open_ = open_start
    for c, ops, blocks in trace:
        if c.hg_name == FORK_MARK:
            out.append((c, ops, _finish(blocks, table, common, open_calls,
                                        open_)))
            continue
        if c.hg_name in MSG_PRINTS:
            open_ = True
        elif c.pl == 20 and open_calls is not None \
                and (ops[0] - STD_PORT_BASE) in open_calls:
            open_ = True
        elif c.hg_name == "CloseMsg":
            open_ = False
        out.append((c, ops, blocks))
    out = _close_open_box(out, table, common, open_calls, open_start)
    return _lower_forks(_lower_battle(_lower_yesno(out, table), table), table)


def open_entries(traces: list) -> set:
    """Which folded entries end with a message box still open."""
    out: set = set()
    for i, tr in enumerate(traces):
        if not tr:
            continue
        open_ = False
        for c, _o, _b in tr:
            if c.hg_name in MSG_PRINTS:
                open_ = True
            elif c.hg_name in ("CloseMsg", "HoldMsg"):
                open_ = False
        if open_:
            out.add(i)
    return out


def convert(member: bytes, table: dict[int, Command],
            stats: dict, std_span: int = 0,
            common: bool = False,
            open_calls: set | None = None,
            marts: dict | None = None,
            trainers: dict | None = None,
            clock: dict | None = None,
            arrival: dict | None = None,
            story: tuple[dict[int, int], dict[int, int]] | None = None,
            maps: dict | None = None,
            here: int | None = None,
            statics: dict | None = None) -> tuple[bytes, list[bool], set]:
    """One map's scripts, folded: the member, which entries kept, and which end with their box
    open (a common bank's callers need to know).
    """
    starts, _ = entry_offsets(member)
    traces: list[list | None] = []
    kept: list[bool] = []
    # A refused entry in the COMMON bank cannot be a bare End: its caller
    # waits for ReturnCommonScript, and an End that never returns is a
    # shopkeeper the player is stuck in front of forever.
    ret_cmd = table.get(21)
    end_cmd = table.get(2)
    for i, s in enumerate(starts):
        fold = Fold(member, table, std_span, trainers, clock, story, maps, here)
        trace, why = fold.run(s)
        if marts is not None and fold.mart is not None:
            marts[i] = fold.mart
        if statics is not None and fold.static is not None and not why:
            statics[i] = fold.static
        if why:
            if common and ret_cmd is not None and end_cmd is not None:
                traces.append([(ret_cmd, [], None), (end_cmd, [], None)])
            else:
                traces.append(None)
            kept.append(False)
            stats[why.split(", ")[0][:64]] = \
                stats.get(why.split(", ")[0][:64], 0) + 1
        else:
            traces.append(_finish(trace, table, common, open_calls))
            kept.append(True)
    if arrival is not None:
        e = arrival.get("entry")
        tr = traces[e - 1] if e and 1 <= e <= len(traces) else None
        names = [c.hg_name for c, _o, _b in (tr or [])]
        arrival["crosses"] = bool(names) \
            and all(n in ARRIVAL_INERT for n in names) \
            and any(n in ("SetFlag", "ClearFlag") for n in names)
        arrival["names"] = names
    return assemble(traces), kept, open_entries(traces)
