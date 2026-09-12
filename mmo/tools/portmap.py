#!/usr/bin/env python3
"""Port HeartGold/SoulSilver maps into this game's own field archives."""

from __future__ import annotations

import os
import re
import struct
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import nsbtx                                              # noqa: E402
import portfollow                                         # noqa: E402
import gen_scripts                                        # noqa: E402
import gen_storyend                                       # noqa: E402
import portscript                                         # noqa: E402
import porttrainers                                       # noqa: E402

MMO = Path(__file__).resolve().parent.parent

# HeartGold, and SoulSilver whose map archives are byte-identical to it.
SRC_SDAT_PATHS = ("data/sound/gs_sound_data.sdat",)
SRC_MATRIX = "a/0/4/1"
SRC_AREA = "a/0/4/2"
SRC_TEXSET = "a/0/4/4"
SRC_LAND = "a/0/6/5"
# Two map-prop archives, and which one an area's ids index is measured rather
# than read.
SRC_PROPMODEL = "a/0/4/0"      # 340 NSBMD map props, the outdoor buildings
SRC_PROPROOM = "a/1/4/8"       # 222 NSBMD map props, the indoor furniture
SRC_AREABUILD = "a/0/4/3"      # 104 preload lists: u16 count, then u16 ids
SRC_PROPTEX = "a/0/7/0"        # 104 NSBTX, one per prop set
SRC_EVENTS = "a/0/3/2"         # 491 zone_event members
SRC_SCRIPT = "a/0/1/2"         # 965 scr_seq members, the field scripts
STD_MISC_MEMBER = 3            # scr_seq_0003: the shared routines CallStd names
STD_MISC_MSG = 40              # msg_0040: the bank those routines speak from
SRC_MSG = "a/0/2/7"            # 829 message banks, one per map that talks
SRC_MMODEL = "a/0/8/1"         # 863 NSBTX, the overworld people
# What a prop does: 273 animation archives (NSBTA material glows, NSBCA joint
# swings, NSBTP screen frames), and one 24-byte row per prop model naming up
# to four of them. The row's extra word over this game's 20-byte shape is
# never read by either engine's manager, so a row crosses by dropping it.
SRC_ANIME = "a/1/0/6"
SRC_EFFECT = "a/1/3/4"         # 24 field-move effect models and animations
SRC_CARD = "a/0/4/9"           # the trainer card: 78 members, the badges among them
SRC_ANIMELIST = {SRC_PROPMODEL: "a/1/0/7",   # rows for the outdoor archive
                 SRC_PROPROOM: "a/1/0/8"}    # rows for the indoor archive
# What the world looks like by the clock: the source keeps its light schedules
# as loose ASCII files and picks one per area through the HIGH byte of the
# record's fourth u16 (the low byte is the indoor flag), by its own table: 0
# -> area01, 1 -> area00, 2 -> dun20_01, anything else -> area00
# (AreaDataManager_GetAreaLightArchiveID + the filename table at
# ov01_02206450).
SRC_LIGHT_TABLE = ("data/area00light.txt", "data/area01light.txt",
                   "data/area02light.txt", "data/dun20_01light.txt",
                   "data/dun20_02light.txt")
SRC_LIGHT_PICK = {0: 1, 1: 0, 2: 3}

# This game's, by the names its own decomp gives them.
DST_LAND = "fielddata/land_data/land_data.narc"
DST_MATRIX = "fielddata/mapmatrix/map_matrix.narc"
DST_AREA = "fielddata/areadata/area_data.narc"
DST_TEXSET = "fielddata/areadata/area_map_tex/map_tex_set.narc"
DST_PROPMODEL = "fielddata/build_model/build_model.narc"
DST_AREABUILD = "fielddata/areadata/area_build_model/area_build.narc"
DST_PROPTEX = "fielddata/areadata/area_build_model/areabm_texset.narc"
DST_MSG = "msgdata/pl_msg.narc"
DST_EFFECT = "graphic/hiden_effect.narc"   # this game's 15 field-move effects
DST_CARD = "graphic/trainer_case.narc"     # this game's trainer card and badge case, 66 members
# The two archives an item id indexes besides the text banks; tools/portitems.py
# appends HeartGold's items past this game's 467 to both.
DST_ITEM_DATA = "itemtool/itemdata/pl_item_data.narc"
DST_ITEM_ICON = "itemtool/itemdata/item_icon.narc"
DST_MMODEL = "data/mmodel/mmodel.narc"
DST_MMLIST = "fielddata/mm_list/move_model_list.narc"
# THE entrance props, and how THE two games find one.
PT_DOOR_HINGED = 66     # door01_nsbmd, the ordinary swinging door
PT_DOOR_SLIDING = 70    # pokecenter_door_nsbmd, the automatic one
PT_STAIRS = {"u01": 130, "u02": 425,    # pokecenter_stair_up_left / _up_right
             "d01": 131, "d02": 426}    # ... _down_left / _down_right
PT_PC = 119             # pokecenter_pc_nsbmd, the storage terminal
PC_NAME = re.compile(r"^(counter_)?pc\d+$")

# What the source calls a door. Measured over the 231 carried props that
# animate: 52 names carry one of these tokens and every one of them is a
# door, and the two that do not (`psentry_d`, `d17_tpdr`) are named here
# because a token short enough to catch them catches waterfalls too.
DOOR_NAME = re.compile(r"door|_dr\d|_dor\d|dr\d\d", re.I)
DOOR_ALSO = ("psentry_d", "d17_tpdr")

DST_ANIME = "arc/bm_anime.narc"
DST_ANIMELIST = "arc/bm_anime_list.narc"
DST_LIGHT = "data/arealight.narc"
# THE TERRAIN'S own animation. Water, the shoreline's waves, the flowers in
# the grass: none of it is a prop.
SRC_TANIME = "data/fldtanime.narc"
DST_TANIME = "data/fldtanime.narc"
TANIME_NAME = 16
TANIME_FRAMES = 18
TANIME_ROW = TANIME_NAME + 2 * TANIME_FRAMES
# THE place-NAME signs.
SRC_AREAWIN = "a/1/6/3"
DST_AREAWIN = "arc/area_win_gra.narc"
AREAWIN_MEMBERS = 18
AREAWIN_SHEET = b"RGCN"
AREAWIN_PALETTE = b"RLCN"

# The bank the field's place-name banner reads, and the two constants the
# engine's own MessageBank_Load decodes it with (src/message.c). A bank is a
# u16 count and a u16 seed, then an (offset, length) pair per message with the
# seed folded in, then the characters with a per-message key of their own.
MSG_BANK_LOCATION_NAMES = 433
MSG_TABLE_MUL = 765
MSG_KEY_START = 596947
MSG_KEY_INC = 18749

# The other four archives a MapHeader names.
DST_SCRIPT = "fielddata/script/scr_seq.narc"
DST_EVENTS = "fielddata/eventdata/zone_event.narc"

SCRCMD_END = 2                # data/scripts/scrcmd.h, the third entry
SCRIPT_TABLE_END = 0xFD13     # what the engine's own readers test for

# The order pc_modfs.c's load_cooked_maps scans a row in, and the presentation
# half of a header.
HEADER_FIELDS = (
    "id area preloaded matrix scripts init msg day night wild events "
    "label window weather camera mapType battleBG bike run escape fly").split()

# This game's own camera templates: sCameraTypes[] in overlay005/field_camera.c
# has seventeen, CAMERA_TYPE_DEFAULT through CAMERA_TYPE_UNUSED_16, and the
# source's seventeen are appended after them by the patch mmo/CAMERAS feeds.
PL_CAMERA_TYPES = 17
MAP_LABEL_WINDOW_CITY = 1
MAP_LABEL_WINDOW_TOWN = 2
MAP_LABEL_WINDOW_ROUTE = 3
MAP_LABEL_WINDOW_CAVE = 4
OVERWORLD_WEATHER_CLEAR = 0
CAMERA_TYPE_DEFAULT = 0
CAMERA_TYPE_INTERIOR_ORTHOGRAPHIC = 4
CAMERA_TYPE_CAVE = 12
MAP_TYPE_TOWN_CITY = 1
MAP_TYPE_OUTDOORS = 2
MAP_TYPE_CAVE = 3
MAP_TYPE_INDOORS = 4
BACKGROUND_CITY = 2
BACKGROUND_MOUNTAIN = 4
BACKGROUND_INDOORS_1 = 6
BACKGROUND_CAVE_1 = 9
ENCOUNTERS_NONE = 0xFFFF

# How a ported map presents itself.
KINDS = {
    "city": dict(window=MAP_LABEL_WINDOW_CITY, camera=CAMERA_TYPE_DEFAULT,
                 map_type=MAP_TYPE_TOWN_CITY, battle_bg=BACKGROUND_CITY,
                 bike=1, run=1, escape=0, fly=1, lighting=1),
    "town": dict(window=MAP_LABEL_WINDOW_TOWN, camera=CAMERA_TYPE_DEFAULT,
                 map_type=MAP_TYPE_TOWN_CITY, battle_bg=BACKGROUND_CITY,
                 bike=1, run=1, escape=0, fly=1, lighting=1),
    # Route 205 South, for a road, and Oreburgh Gate for a cave. A cave keeps
    # this game's outdoor lighting index because that is what its own cave
    # areas carry (area_data_053's fourth u16 is 1, the same as a road's); the
    # dark is the map's textures and not a field of the area record.
    "route": dict(window=MAP_LABEL_WINDOW_ROUTE, camera=CAMERA_TYPE_DEFAULT,
                  map_type=MAP_TYPE_OUTDOORS, battle_bg=BACKGROUND_MOUNTAIN,
                  bike=1, run=1, escape=0, fly=1, lighting=1),
    "cave": dict(window=MAP_LABEL_WINDOW_CAVE, camera=CAMERA_TYPE_CAVE,
                 map_type=MAP_TYPE_CAVE, battle_bg=BACKGROUND_CAVE_1,
                 bike=1, run=1, escape=1, fly=0, lighting=1),
    "interior": dict(window=MAP_LABEL_WINDOW_TOWN,
                     camera=CAMERA_TYPE_INTERIOR_ORTHOGRAPHIC,
                     map_type=MAP_TYPE_INDOORS, battle_bg=BACKGROUND_INDOORS_1,
                     bike=0, run=0, escape=0, fly=0, lighting=0),
}

# Which of mmo/MAPS's kind column maps onto which of the above. A kind with no
# row here is one nothing has been ported of yet, and is refused by name rather
# than dressed as a city.
MAP_KINDS = {"city_town": "city", "interior": "interior",
             "route": "route", "cave": "cave"}

# THE BATTLE BACKGROUND is THE SOURCE'S own.
BATTLE_BG = {
    "general": 0, "ocean": 1, "city": 2, "forest": 3, "mountain": 4,
    "snow": 5, "building_1": 6, "building_2": 7, "building_3": 8,
    "cave_1": 9, "cave_2": 10, "cave_3": 11,
    "will": 12, "koga": 13, "bruno": 14, "karen": 15, "lance": 16,
    "distortion_world": 17,
}

# The battle themes a region fights to: wild, trainer, gym leader. HeartGold
# splits these by region, Kanto has its own three, and a package that
# carries a region's maps should carry its fights too.
BATTLE_SEQS = {
    "johto": ("SEQ_GS_VS_NORAPOKE", "SEQ_GS_VS_TRAINER", "SEQ_GS_VS_GYMREADER"),
    "kanto": ("SEQ_GS_VS_NORAPOKE_KANTO", "SEQ_GS_VS_TRAINER_KANTO",
              "SEQ_GS_VS_GYMREADER_KANTO"),
}

# The tracks a trainer's eyes meeting yours plays, one per class and split by
# region for the single class that differs. Which name goes with which class is
# mmo/TRAINER_CLASS's last two columns; this is only the set to carry, taken
# from that file so the two cannot drift.
def encounter_seqs(classes: dict) -> list[str]:
    return sorted({row[3] for row in classes.values()}
                  | {row[4] for row in classes.values()})


# The theme an appended class's fight opens with, where it is the class's own
# rather than the region's.
CLASS_THEME_GYM = {"SEQ_GS_VS_GYMREADER", "SEQ_GS_VS_GYMREADER_KANTO"}
CLASS_THEME_PLAIN = {"-", "SEQ_GS_VS_TRAINER"}


def class_theme_seqs(class_rows: list) -> list[str]:
    return sorted({r[3] for r in class_rows
                   if r[3] not in CLASS_THEME_GYM | CLASS_THEME_PLAIN})


def trainer_class_rows(class_rows: list, music: dict) -> str:
    """One line per appended class: id, gender, prize, theme kind, sequence."""
    out = []
    for pl, gender, prize, seq in class_rows:
        if seq in CLASS_THEME_GYM:
            kind, sid = 1, 0
        elif seq in CLASS_THEME_PLAIN or seq not in music:
            kind, sid = 0, 0
        else:
            kind, sid = 2, music[seq]
        out.append("%d %d %d %d %d\n" % (pl, 1 if gender == "F" else 0,
                                          prize, kind, sid))
    return "".join(out)

# --------------------------------------------------------------------- events
BG_EVENT = 20
OBJECT_EVENT = 32
WARP_EVENT = 12
COORD_EVENT = 16

# The three field-move routines an object may name, the same ids in both
# games (see port_events.script_of).
FIELD_MOVE_CUT = 10000
FIELD_MOVE_STRENGTH = 10002

# THE ITEMS ON THE GROUND.
ITEM_BALL_SPRITE = 87            # SPRITE_MONSTARBALL there, OBJ_EVENT_GFX_ITEM_BALL here
HG_ITEM_BALL_MEMBER = 141        # scr_seq_0141: the 255 ball stubs and their tail
HG_ITEM_BALL_BASE = 7000         # _std_item_ball
HG_HIDDEN_ITEM_BASE = 8000       # _std_hidden_item
HG_ITEM_BANK_COUNT = 256
HG_ITEM_FLAG_FIRST = 0x320       # HIDDEN_ITEMS_FLAG_BASE, 800
HG_ITEM_FLAG_END = 0x520         # one past FLAG_HIDE_ITEMBALL_T26_TM57 (0x51E)
BG_EVENT_HIDDEN_ITEM = 2         # BG_EVENT_TYPE_HIDDEN_ITEM, both games
PORTED_ITEM_FLAGS_START = 3840   # OPENMMO_PORTED_ITEM_FLAGS_START in the patch
PORTED_ITEM_FLAGS_MAX = HG_ITEM_FLAG_END - HG_ITEM_FLAG_FIRST
# STATIC SITES: a person whose script stages a wild fight (Sudowoodo, the
# Snorlax, the birds...) hides behind a flag of this band once the fight is
# won; the mod sets it (openmmo_static.c) and the server keeps its own record
# of the same site. One flag a site, numbered in cook order across the run.
PORTED_STATIC_FLAGS_START = PORTED_ITEM_FLAGS_START + PORTED_ITEM_FLAGS_MAX
PORTED_STATIC_FLAGS_MAX = 32     # OPENMMO_PORTED_STATIC_FLAGS_MAX in the patch
PL_VISIBLE_ITEMS_BASE = 7000     # SCRIPT_ID_OFFSET_VISIBLE_ITEMS
PL_HIDDEN_ITEMS_BASE = 8000      # SCRIPT_ID_OFFSET_HIDDEN_ITEMS
PORTED_BALL_BASE = 21000         # above the common bank at STD_PORT_BASE

# THE APRICORN trees.
HG_APRICORN_TREE = 2800          # std_apricorn_tree
HG_APRICORN_MSG = 23             # msg_0023, the routine's eight lines
APRICORN_BASE = 23000            # above the ball bank
APRICORN_ENTRY_ASK = 0
APRICORN_ENTRY_NONE = 1
APRICORN_ENTRY_NO_BOX = 2
APRICORN_ENTRY_PICKED = 3        # + kind 0..6, red to black
APRICORN_KINDS = 7
APRICORN_FIRST_ITEM = 485        # ITEM_RED_APRICORN
PL_SEQ_FANFA4 = 1158             # this game's item fanfare (SEQ_FANFA4)

# THE FIELD MOVES A PORTED MAP answers for.
HG_FIELDMOVE_MSG = 211           # msg_0211, the field-move lines
FIELDMOVE_BASE = 24000           # above the apricorn bank
FIELDMOVE_ENTRY_HEADBUTT = 0
FIELDMOVE_ENTRY_RUBBLE = 1
FIELDMOVE_ENTRIES = 2
HG_FIELDMOVE_MSG_TREE = 32       # "It's a moderately sized tree. Would you like to use Headbutt?"
HG_FIELDMOVE_MSG_USED = 33       # "{mon} used Headbutt."
HG_FIELDMOVE_MSG_BIG_TREE = 34   # "There's a large, formidable tree ..." (nobody knows the move)
HG_FIELDMOVE_MSG_RUBBLE = 6      # "{item} was in the rubble!"
HG_HEADBUTT_MODEL = 19           # a/1/3/4: the tree the shake draws (BMD0)
HG_HEADBUTT_ANIMS = (17, 18)     # its joint and material animations (BCA0, BMA0)
HG_HEADBUTT_LAND = 0xD0          # the all-trees land member HeartGold headbutts anywhere on
PL_OBSTACLE_HEADBUTT = 3         # StartDestroyObstacleAnimation's new kind (patches/src/scrcmd.c)
PL_COMMON_OBTAIN = 2016          # CommonScript_AddItemQuantityNoLineFeed, std_obtain_item_verbose's stand-in
PL_MOVE_HEADBUTT = 29
PL_VAR_RESULT = 0x800C
PL_VAR_8000 = 0x8000             # SCRIPT_DATA_PARAMETER_0, FieldSystem_SetScriptParameters' first
PL_VAR_8001 = 0x8001
PL_VAR_8004 = 0x8004
PL_VAR_8005 = 0x8005
PL_MAX_PARTY_SIZE = 6
PL_MENU_YES = 0
PL_COND_EQ = 1                   # sConditionTable, both games
PL_COND_NE = 5

# THE BADGES ON THE CARD.
HG_CARD_PAGE = (
    ("page_tiles", 43, b"RGCN"),    # 1024 tiles, 4bpp: faces, frames, the title
    ("page_screen", 52, b"RCSN"),   # both regions' bands (51 is Johto's alone)
    ("page_palette", 28, b"RLCN"),  # the bottom screen's sixteen blocks
    ("badge_char", 46, b"RGCN"),    # 3072 tiles, 1D-mapped, 128-byte units
    ("badge_pal", 30, b"RLCN"),     # a block a badge
    ("badge_cell", 58, b"RECN"),    # fourteen cells a badge, the front first
    ("badge_anim", 59, b"RNAN"),    # a sequence a badge
)
HG_CARD_BADGES = 16              # Johto's eight, then Kanto's
PL_ITEM_BANKS = ("scripts_visible_items", "scripts_hidden_items")
PL_ITEM_TEXT = ("TEXT_BANK_VISIBLE_ITEMS", "TEXT_BANK_HIDDEN_ITEMS")


def ported_item_flag(flag: int) -> int:
    return PORTED_ITEM_FLAGS_START + (flag - HG_ITEM_FLAG_FIRST)


def load_hidden_items() -> dict[int, tuple[int, int]]:
    """mmo/HIDDEN_ITEMS: the index a hidden item's script names -> (item, count)."""
    path = MMO / "HIDDEN_ITEMS"
    if not path.is_file():
        die("no %s; run tools/gen_hidden_items.py" % path)
    out: dict[int, tuple[int, int]] = {}
    for line in path.read_text().splitlines():
        if not line.strip() or line.startswith("#"):
            continue
        idx, item, qty = (int(x) for x in line.split()[:3])
        out[idx] = (item, qty)
    return out

# The engine reads a member into a fixed buffer and asserts on the size, so a
# map with more events than fit is refused here rather than at the load.
EVENTS_BUFFER = 0x800

# An object event with this script id is "no script", and BOTH games test for
# it, and in this game an object that has none is created without ever asking
# about its hide flag, which is not what a ported person wants. So a carried
# person gets script 0, the do-nothing script of the archive beside it, and a
# hide flag of 0, which is a flag nothing sets.
SCRIPT_UNSET = 0xFFFF
SCRIPT_NONE = 0
FLAG_NONE = 0
TRAINER_TYPE_NONE = 0

# A warp with no destination in this package. Its record has to stay where it is,
# every other map's arrival anchor is an index into this array, so it is
# parked on a coordinate no player stands on instead of being removed.
# `MapHeaderData_GetIndexOfWarpEventAtPos` compares x and z and never matches.
WARP_PARKED = 0xFFFF

# `MAX_MAP_OBJECTS_TO_PRELOAD`, and the sentinel `FetchMapObjectsToPreload`
# stops at. The preload list is what sizes the billboard resource heap
# (`ov5_021ECE40` takes count + 3), so a ported map names its own people there
# rather than borrowing a Sinnoh member that was sized for a different cast.
MAX_MAP_OBJECTS_TO_PRELOAD = 24
MAP_OBJECT_PRELOAD_SENTINEL = 0xFFFF

# Where an appended person lands.
FIRST_COOKED_GFX = 276

# A body that never turns. The client dresses every cooked billboard as a
# person unless its row says otherwise: the youngster's walk, four facings of
# a sixteen-texture sheet.
SEQ_STATIC = 66
# The follower's own walk: four facings of a two-frame cycle over the 8
# textures of a follower sheet, carried from the cartridge by the follower
# cook (tools/portfollow.py, SEQ_WALK), and here too, for the Pokemon a map
# Stands on a tile (FOLLOWER_MON_STATIC_*), whose art is that same sheet.
SEQ_FOLLOWER_WALK = portfollow.SEQ_WALK
FOLLOWER_TEXTURES = 8
STATIC_SEQ_BLOB = struct.pack("<IHBB", 1, 0, 0, 0)
WALK_TEXTURES = 16
BILLBOARD_MODEL_GENERIC_32x32 = 0
BILLBOARD_MODEL_GENERIC_64x64 = 5

HG_LAND_HEADER = 0x14   # four sizes, then the tag word
PL_LAND_HEADER = 0x10   # four sizes
TERRAIN_SIZE = 0x800    # 32 x 32 u16, the same on both sides
MAP_TILES = 32          # a matrix cell, in tiles, on both sides
EMPTY_CELL = 0xFFFF

# `MAP_HEADER_EVERYWHERE` on both sides: the map the sea between the places is
# on. HeartGold gives it 291 of the overworld's 492 filled cells, spanning both
# regions, so it is the one header a region's rectangle is not measured over.
HG_HEADER_EVERYWHERE = 0

# `constants/field/map_matrix.h`. The engine reads a matrix into a fixed struct
# and a region wider than this is more of the world than one holds.
MAP_MATRIX_MAX_WIDTH = 30
MAP_MATRIX_MAX_HEIGHT = 30
MAP_MATRIX_MAX_NAME = 16

# A terrain word is a behaviour byte, seven bits nobody here reads, and a
# collision bit. Measured across both games' whole land-data archives:
#
#   platinum  high byte is 0x00 or 0x80 and nothing else, 666 members
#   heartgold high byte takes 38 distinct values over 676 members
CARRIED_BITS = 0x80FF

PL_AREA_DUMMY = 1

# Where A PORTED MAP'S HEADER ID starts. This game's own table is 593 entries
# (`sMapHeaders`, 0..592) and 593 itself is the authored hub's, so 594 is the
# first id a port may answer to.
FIRST_FREE_HEADER = 594

# What this game's built image already holds. A package's appended members must
# be contiguous with these, pc_modfs refuses an "append hole", so a port
# starts at the count and one package holds one map. Overriding these on the
# command line is how a port replaces an existing map instead of adding one.
PL_COUNT = {
    DST_LAND: 666,
    DST_MATRIX: 289,
    DST_AREA: 75,
    DST_TEXSET: 74,
    DST_PROPMODEL: 590,
    DST_AREABUILD: 71,
    DST_PROPTEX: 71,
    DST_SCRIPT: 1124,
    DST_EVENTS: 534,
    DST_MSG: 724,
    DST_MMODEL: 470,
    DST_MMLIST: 16,
    DST_ANIME: 98,
    DST_ANIMELIST: 590,
    DST_LIGHT: 4,
    DST_TANIME: 53,
    DST_AREAWIN: 18,
    DST_ITEM_DATA: 446,
    DST_ITEM_ICON: 711,
    DST_EFFECT: 15,
    DST_CARD: 66,
}

# A map prop is 48 bytes beginning with a u32 model id. The engine indexes its
# loaded-model table by that absolute id, `mapPropModelFiles[mapPropModelID]`
# in area_data.c, and the table is MAX_MAP_PROP_MODEL_FILES entries, so an
# appended model has to land under that or the load writes past it.
PROP_RECORD = 48
PROP_MODEL_ID = 0
# `MAX_MAP_PROP_MODEL_FILES`, as THIS client builds it. The engine's own number
# is 768 and a whole Johto needs 1040 of them, so
# mods/openmmo/patches/include/overlay005/area_data.h.patch raises it to 1152
# and this is the same number read from the other side of that patch,
# tests/mapformat_test.sh is what keeps the two in step.
MAX_MAP_PROP_MODEL_FILES = 1152


def die(msg: str) -> None:
    print("portmap: " + msg, file=sys.stderr)
    raise SystemExit(2)


def load_terrain_map(path: Path) -> dict[int, tuple[int, str]]:
    """value -> (byte to write, verdict), from the committed table."""
    if not path.is_file():
        die("no %s, the whole reason this kind is no longer refused" % path)
    out: dict[int, tuple[int, str]] = {}
    for line in path.read_text().splitlines():
        m = re.match(r"^0x([0-9A-F]{2})\s+0x([0-9A-F]{2})\s+(\S+)", line)
        if m:
            out[int(m.group(1), 16)] = (int(m.group(2), 16), m.group(3))
    if len(out) < 200:
        die("%s has only %d rows; it is not the generated table" % (path, len(out)))
    return out


def load_maps(path: Path) -> dict[str, dict]:
    """name -> the source map's header id, area, matrix, kind, region, name."""
    if not path.is_file():
        die("no %s; run tools/gen_maps.py" % path)
    out = {}
    for line in path.read_text().splitlines():
        m = re.match(r"^hg\s+(\S+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)"
                     r"\s+(-?\d+)\s+(-?\d+)\s+(\S+)\s+(\S+)\s+(\S+)"
                     r"\s+(\S+)\s+(\S+)\s+(-?\d+)\s+(-?\d+)\s+(-?\d+)\s+(.*?)\s*$",
                     line)
        if m:
            out[m.group(1)] = dict(name=m.group(1),
                                   header=int(m.group(2)),
                                   area=int(m.group(3)),
                                   matrix=int(m.group(4)),
                                   events=int(m.group(5)),
                                   script=int(m.group(6)),
                                   msg=int(m.group(7)),
                                   enc=(m.group(8) if m.group(8) != "-"
                                        else None),
                                   kind=m.group(9),
                                   region=m.group(10),
                                   bgm=m.group(11),
                                   bg=(m.group(12) if m.group(12) != "-"
                                       else None),
                                   hdr=int(m.group(13)),
                                   icon=int(m.group(14)),
                                   cam=int(m.group(15)),
                                   place=(m.group(16) if m.group(16) != "-"
                                          else None))
    if not out:
        die("%s has no rows in the sixteen-column shape; regenerate it" % path)
    return out


def load_sprites(path: Path) -> dict[int, tuple[int | None, str]]:
    """HeartGold sprite id -> (mmodel member, the name both headers give it)."""
    if not path.is_file():
        die("no %s; run tools/gen_sprites.py" % path)
    out: dict[int, tuple[int | None, str]] = {}
    for line in path.read_text().splitlines():
        m = re.match(r"^\s*(\d+)\s+(\d+|-)\s+(\S+)\s*$", line)
        if m:
            out[int(m.group(1))] = (
                None if m.group(2) == "-" else int(m.group(2)), m.group(3))
    if not out:
        die("%s has no rows; regenerate it" % path)
    return out


class Scene:
    """What a map's own arrival script does to its people on a save whose story
    is over: the flags it leaves SET (a person under one is not created),
    the people it deletes by number (`HidePerson`) and the ones it creates
    whatever their flag says (`ShowPerson`)."""

    def __init__(self) -> None:
        self.flags: set[int] = set()
        self.hide: set[int] = set()
        self.show: set[int] = set()


def load_scenes(path: Path) -> dict[str, Scene | None]:
    """map name -> its Scene, or None if mmo/MAPSCENES could not read it."""
    if not path.is_file():
        die("no %s; run tools/gen_mapscenes.py" % path)
    out: dict[str, Scene | None] = {}
    for line in path.read_text().splitlines():
        m = re.match(r"^hg\s+(\S+)\s+(.*)$", line)
        if not m:
            continue
        rest = m.group(2).split("#")[0].strip()
        if rest == "?":
            out[m.group(1)] = None
            continue
        sc = Scene()
        for tok in rest.split():
            if tok.startswith("hide="):
                sc.hide.add(int(tok[5:]))
            elif tok.startswith("show="):
                sc.show.add(int(tok[5:]))
            elif "=" in tok:
                sc.flags.add(int(tok.split("=")[1], 0))
        out[m.group(1)] = sc
    return out


def load_walls(path: Path) -> dict[str, tuple[set[int], set[int]]]:
    """map name -> (flags to treat as SET, flags to treat as CLEAR) on top of
    the scene: mmo/MAPWALLS, the hand-written rows for the people the story
    stood in a visitor's way and the ones it kept off stage."""
    out: dict[str, tuple[set[int], set[int]]] = {}
    if not path.is_file():
        return out
    for line in path.read_text().splitlines():
        m = re.match(r"^hg\s+(\S+)\s+(set|clear)\s+(\w+)=(0x[0-9A-Fa-f]+|\d+)",
                     line)
        if not m:
            continue
        row = out.setdefault(m.group(1), (set(), set()))
        row[0 if m.group(2) == "set" else 1].add(int(m.group(4), 0))
    return out


# A member's TEX0 carries the character's name as the artist spelt it, and
# three spell it too tersely for the rules in palette_agrees to see: `lug01`
# for Lugia's object, `pip_n`, and `rbanzaiheroine` contracted to RBANZAIINE.
HAND_PALETTES = {
    "lug_obj01": "lug01",
    "mono_pip": "pip_n",
    "rbanzaiine": "rbanzaiheroine",
}


def palette_agrees(name: str, pal: str) -> bool:
    """Whether an mmodel member's own palette name is the sprite mmo/SPRITES says."""
    a, b = pal.lower(), name.lower()
    if HAND_PALETTES.get(b) == a:
        return True
    # A static follower's member is the species' follower sheet, and every
    # follower sheet calls its palette tsure_poke0 (mmo/SPRITES says why).
    if b.startswith("follower_mon_") and a in ("tsure_poke0", "tsure_poke1"):
        return True
    if a.endswith("_pl"):
        a = a[:-3]
    a = re.sub(r"\.\d+$", "", a)
    return a == b or re.sub(r"_\d+$", "", b) == a


def nsbmd_model_name(blob: bytes) -> str | None:
    """The name a prop's own model block gives it, `door_pc01` for the Pokemon Center's
    automatic door.
    """
    if len(blob) < 0x14 or blob[:4] not in (b"BMD0", b"BTX0"):
        return None
    nblocks = struct.unpack_from("<H", blob, 0x0E)[0]
    mdl = None
    for i in range(nblocks):
        if 0x10 + 4 * i + 4 > len(blob):
            return None
        off = struct.unpack_from("<I", blob, 0x10 + 4 * i)[0]
        if blob[off:off + 4] == b"MDL0":
            mdl = off
    if mdl is None or mdl + 16 > len(blob):
        return None
    base = mdl + 8
    d = base + struct.unpack_from("<H", blob, base + 0x06)[0]
    names = d + struct.unpack_from("<H", blob, d + 2)[0]
    if names + 16 > len(blob):
        return None
    return blob[names:names + 16].split(b"\0")[0].decode("latin1")


def nsbtx_palette_name(blob: bytes) -> str | None:
    """The name a person's own texture block gives its palette."""
    if len(blob) < 0x14 or blob[:4] not in (b"BTX0", b"BMD0"):
        return None
    nblocks = struct.unpack_from("<H", blob, 0x0E)[0]
    tex = None
    for i in range(nblocks):
        if 0x10 + 4 * i + 4 > len(blob):
            return None
        off = struct.unpack_from("<I", blob, 0x10 + 4 * i)[0]
        if blob[off:off + 4] == b"TEX0":
            tex = off
    if tex is None or tex + 0x38 > len(blob):
        return None
    base = tex + struct.unpack_from("<I", blob, tex + 0x34)[0]
    if base + 8 > len(blob) or blob[base + 1] == 0:
        return None
    d = base + struct.unpack_from("<H", blob, base + 0x06)[0]
    names = d + struct.unpack_from("<H", blob, d + 2)[0]
    if names + 16 > len(blob):
        return None
    return blob[names:names + 16].split(b"\0")[0].decode("latin1")


def parse_events(member: bytes) -> dict:
    """One zone_event member as its four lists of raw records."""
    out, p = {}, 0
    for key, size in (("bg", BG_EVENT), ("obj", OBJECT_EVENT),
                      ("warp", WARP_EVENT), ("coord", COORD_EVENT)):
        n, = struct.unpack_from("<I", member, p)
        p += 4
        out[key] = [member[p + i * size:p + (i + 1) * size] for i in range(n)]
        p += n * size
    if p != len(member):
        die("a zone_event member's four lists account for %d of %d bytes"
            % (p, len(member)))
    return out


def build_events(lists: dict) -> bytes:
    out = bytearray()
    for key in ("bg", "obj", "warp", "coord"):
        out += struct.pack("<I", len(lists[key]))
        for rec in lists[key]:
            out += rec
    if len(out) >= EVENTS_BUFFER:
        die("the converted events are %d bytes and the engine reads a member "
            "into a %d-byte buffer" % (len(out), EVENTS_BUFFER))
    return bytes(out)


def parse_matrix(b: bytes) -> dict:
    w, h, has_hdr, has_alt, nlen = b[0], b[1], b[2], b[3], b[4]
    p = 5
    name = b[p:p + nlen].decode("latin1")
    p += nlen
    n = w * h
    if has_hdr:
        hdrs = list(struct.unpack_from("<%dH" % n, b, p))
        p += n * 2
    else:
        hdrs = [None] * n
    if has_alt:
        alt = list(b[p:p + n])
        p += n
    else:
        alt = [0] * n
    land = list(struct.unpack_from("<%dH" % n, b, p))
    return dict(w=w, h=h, name=name, hdrs=hdrs, alt=alt, land=land)


def build_matrix(w: int, h: int, alt: list[int], land: list[int],
                 name: str, hdrs: list[int] | None) -> bytes:
    """A matrix of our own, in the format BOTH games' loaders read, they are the same
    routine, so this needs no conversion, only assembly.
    """
    if len(name) > MAP_MATRIX_MAX_NAME:
        die("matrix name %r is longer than the engine's own bound" % name)
    if w > MAP_MATRIX_MAX_WIDTH or h > MAP_MATRIX_MAX_HEIGHT:
        die("a %dx%d matrix is past the engine's %dx%d"
            % (w, h, MAP_MATRIX_MAX_WIDTH, MAP_MATRIX_MAX_HEIGHT))
    out = bytearray()
    out += bytes([w, h, 1 if hdrs is not None else 0, 1, len(name)])
    out += name.encode("latin1")
    if hdrs is not None:
        out += struct.pack("<%dH" % len(hdrs), *hdrs)
    out += bytes(alt)
    out += struct.pack("<%dH" % len(land), *land)
    return bytes(out)


def prop_model_ids(member: bytes) -> list[int]:
    """Every model a member's map props ask for, in record order."""
    ta, props = struct.unpack_from("<II", member, 0)
    fifth = struct.unpack_from("<I", member, 0x10)[0] >> 16
    base = HG_LAND_HEADER + ta + fifth
    return [struct.unpack_from("<I", member, base + i * PROP_RECORD
                               + PROP_MODEL_ID)[0]
            for i in range(props // PROP_RECORD)]


def convert_land(member: bytes, tmap: dict[int, tuple[int, str]],
                 stats: dict, prop_map: dict[int, int] | None = None) -> bytes:
    """One HeartGold land-data member as this game's loader reads it."""
    ta, props, model, bdhc = struct.unpack_from("<IIII", member, 0)
    tag_word, = struct.unpack_from("<I", member, 0x10)
    tag, fifth = tag_word & 0xFFFF, tag_word >> 16
    if tag != 0x1234:
        die("member does not carry heartgold's 0x1234 tag (got 0x%04X)" % tag)
    if ta != TERRAIN_SIZE:
        die("terrain attributes are %d bytes, not the %d both games use"
            % (ta, TERRAIN_SIZE))

    p = HG_LAND_HEADER
    p += fifth                                   # the fifth section: FIRST, dropped
    terrain = bytearray(member[p:p + ta]);      p += ta
    props_b = bytearray(member[p:p + props]);   p += props
    model_b = member[p:p + model];               p += model
    bdhc_b = member[p:p + bdhc];                 p += bdhc
    if p != len(member):
        die("sections do not account for the member: %d of %d" % (p, len(member)))
    if model_b[:4] != b"BMD0":
        die("the model section is not an NSBMD (%r), the section order is "
            "wrong and every byte after it would be too" % model_b[:4])
    if bdhc_b[:4] != b"BDHC":
        die("the bdhc section is not a BDHC (%r)" % bdhc_b[:4])

    # Translate every tile's behaviour byte. Collision is bit 15 and is carried
    # unchanged: both games write it in the same place and mean the same thing
    # by it, measured on the pair of them, WATER_SEA is passable in 40,261 of
    # 40,385 platinum tiles and 42,949 of 43,097 heartgold ones, so what stops a
    # walker at the shore is the BEHAVIOUR in both, not the bit.
    for i in range(0, ta, 2):
        v = terrain[i] | (terrain[i + 1] << 8)
        beh, coll = v & 0xFF, v & 0x8000
        row = tmap.get(beh)
        if row is None:
            die("terrain byte 0x%02X has no row in TERRAIN_MAP" % beh)
        write, verdict = row
        stats[verdict] = stats.get(verdict, 0) + 1
        if write != beh:
            stats["rewritten"] = stats.get("rewritten", 0) + 1
        nv = (coll | write) & CARRIED_BITS
        terrain[i], terrain[i + 1] = nv & 0xFF, (nv >> 8) & 0xFF

    if prop_map is None:
        props_b, props = bytearray(), 0
    else:
        for i in range(props // PROP_RECORD):
            at = i * PROP_RECORD + PROP_MODEL_ID
            old = struct.unpack_from("<I", props_b, at)[0]
            if old not in prop_map:
                die("a map prop asks for model %d, which this port did not "
                    "carry, the closure is wrong, not the map" % old)
            struct.pack_into("<I", props_b, at, prop_map[old])
            stats["props"] = stats.get("props", 0) + 1

    return (struct.pack("<IIII", ta, props, model, bdhc)
            + bytes(terrain) + bytes(props_b) + model_b + bdhc_b)


def pick_props(placed: list[int], listed: list[int], texset: bytes,
               archives: dict[str, list[bytes]], name: str) -> tuple[str, str]:
    """Which of the source's two prop archives a map's model ids index."""
    have = {n for n in nsbtx.read(texset)["textures"]}

    def covers(ids: set[int], arr: list[bytes]) -> bool:
        want = set()
        for mid in ids:
            if mid >= len(arr):
                return False
            want |= nsbtx.model_texture_names(arr[mid])
        return bool(want) and not (want - have)

    both = set(placed) | set(listed)
    for ids in (both, set(listed), set(placed)):
        for path, arr in archives.items():
            if covers(ids, arr):
                return path, ("the buildings" if path == SRC_PROPMODEL
                              else "the furniture")
    if not both:
        return SRC_PROPMODEL, "nothing, this area carries no prop at all"
    die("%s places props that neither %s nor %s can dress out of its own "
        "prop texture set, something else decides this map's archive and "
        "guessing would put the wrong furniture in it"
        % (name, SRC_PROPMODEL, SRC_PROPROOM))


def convert_events(member: bytes, ox: int, oy: int, scene: Scene | None,
                   gfx_of, headers: dict[int, int], stats: dict,
                   kept: list[bool] | None,
                   trainers: dict[int, int] | None = None,
                   walls: tuple[set[int], set[int]] | None = None,
                   clock: dict[int, int] | None = None,
                   items: dict | None = None,
                   statics: dict[int, tuple[int, int]] | None = None,
                   sites: list | None = None
                   ) -> tuple[bytes, set[int]]:
    """One map's people, signs, doors and triggers as this game reads them."""
    ev = parse_events(member)
    used: set[int] = set()

    def script_of(sid: int) -> int:
        # A TREE, A ROCK, A BOULDER. HeartGold places 48 cut trees, 103
        # rock-smash rocks and 29 strength boulders as objects whose script
        # is a field-move routine of its common bank, std_field_cut and the
        # two after it (include/constants/std_script.h: 10000, 10001,
        # 10002). This game keeps the same three routines at the same three
        # ids, SCRIPT_ID_OFFSET_FIELD_MOVES is 10000 and
        # scripts_field_moves.s lists CutTree, Rock, Boulder first, so the
        # id crosses as it is and the press runs THIS game's routine: its
        # own party check, its own badge, its own animation, and the object
        # gone after. Until 2026-09-01 these fell through to no script and
        # Ilex Forest was closed by a tree nobody could cut. Nothing else
        # of that bank is placed on an object; the rest keeps to the map's
        # own entries.
        if FIELD_MOVE_CUT <= sid <= FIELD_MOVE_STRENGTH:
            stats["field moves"] = stats.get("field moves", 0) + 1
            return sid
        # An Apricorn tree's routine is the generated bank's first entry; the
        # rest of the conversation is the server's answer (APRICORN_BASE).
        if sid == HG_APRICORN_TREE and items is not None and items.get("apricorn"):
            stats["apricorn trees"] = stats.get("apricorn trees", 0) + 1
            return APRICORN_BASE + APRICORN_ENTRY_ASK
        if kept is None or not (1 <= sid <= len(kept)) or not kept[sid - 1]:
            return SCRIPT_NONE
        stats["scripts"] = stats.get("scripts", 0) + 1
        return sid

    bgs = []
    for rec in ev["bg"]:
        r = bytearray(rec)
        sid, kind = struct.unpack_from("<HH", r, 0)
        if (items is not None and kind == BG_EVENT_HIDDEN_ITEM
                and HG_HIDDEN_ITEM_BASE <= sid
                < HG_HIDDEN_ITEM_BASE + HG_ITEM_BANK_COUNT):
            idx = sid - HG_HIDDEN_ITEM_BASE
            if idx not in items["hidden"]:
                stats["hidden_unknown"] = stats.get("hidden_unknown", 0) + 1
                continue
            struct.pack_into("<H", r, 0, items["hidden_base"] + idx)
            stats["hidden"] = stats.get("hidden", 0) + 1
        else:
            struct.pack_into("<H", r, 0, script_of(sid))
        x, z = struct.unpack_from("<ii", r, 4)
        struct.pack_into("<ii", r, 4, x - ox, z - oy)
        bgs.append(bytes(r))

    walled, opened = walls if walls else (set(), set())
    objs = []
    for rec in ev["obj"]:
        r = bytearray(rec)
        oid, sprite = struct.unpack_from("<HH", r, 0)
        flag, sid = struct.unpack_from("<HH", r, 8)
        ball = (items is not None and sprite == ITEM_BALL_SPRITE
                and HG_ITEM_BALL_BASE <= sid
                < HG_ITEM_BALL_BASE + HG_ITEM_BANK_COUNT)
        site = statics.get(sid) if statics else None
        local = FLAG_NONE
        if ball:
            # A ball is remembered by its flag or not at all: one outside
            # the band would be picked up on every visit.
            if not HG_ITEM_FLAG_FIRST <= flag < HG_ITEM_FLAG_END:
                stats["balls_unflagged"] = stats.get("balls_unflagged", 0) + 1
                continue
            local = ported_item_flag(flag)
        elif site is not None:
            if flag in walled:
                stats["dropped_walled"] = stats.get("dropped_walled", 0) + 1
                continue
            if sites is None:
                die("a static site with nowhere to record it")
            if len(sites) >= PORTED_STATIC_FLAGS_MAX:
                die("more static sites than the band holds (%d); widen "
                    "OPENMMO_PORTED_STATIC_FLAGS_MAX in the patch and here"
                    % PORTED_STATIC_FLAGS_MAX)
            local = PORTED_STATIC_FLAGS_START + len(sites)
            sites.append(dict(oid=oid, species=site[0], level=site[1],
                              src_flag=flag, flag=local, sid=sid))
            stats["static sites"] = stats.get("static sites", 0) + 1
        elif scene is not None and oid in scene.hide:
            stats["dropped_hidden"] = stats.get("dropped_hidden", 0) + 1
            continue
        elif clock and flag in clock:
            local = clock[flag]
            stats["kept_clock"] = stats.get("kept_clock", 0) + 1
        elif flag != FLAG_NONE:
            if flag in walled:
                stats["dropped_walled"] = stats.get("dropped_walled", 0) + 1
                continue
            if flag in opened:
                stats["kept_opened"] = stats.get("kept_opened", 0) + 1
            elif scene is None:
                stats["dropped_unread"] = stats.get("dropped_unread", 0) + 1
                continue
            elif flag in scene.flags and oid not in scene.show:
                stats["dropped_hidden"] = stats.get("dropped_hidden", 0) + 1
                continue
        gid = gfx_of(sprite)
        if gid is None:
            stats["dropped_noart"] = stats.get("dropped_noart", 0) + 1
            continue
        used.add(gid)
        struct.pack_into("<H", r, 2, gid)
        struct.pack_into("<H", r, 8, local)
        src_trainer = porttrainers.trainer_of_script(sid)
        if ball:
            struct.pack_into("<H", r, 6, TRAINER_TYPE_NONE)
            struct.pack_into("<H", r, 10,
                             items["ball_base"] + (sid - HG_ITEM_BALL_BASE))
            stats["balls"] = stats.get("balls", 0) + 1
        elif trainers is not None and src_trainer in trainers:
            struct.pack_into("<H", r, 10, porttrainers.script_of_trainer(
                sid, trainers[src_trainer]))
            stats["trainers"] = stats.get("trainers", 0) + 1
        else:
            struct.pack_into("<H", r, 6, TRAINER_TYPE_NONE)
            struct.pack_into("<H", r, 10, script_of(sid))
        x, z = struct.unpack_from("<HH", r, 24)
        struct.pack_into("<HH", r, 24, x - ox, z - oy)
        objs.append(bytes(r))
        stats["people"] = stats.get("people", 0) + 1

    warps = []
    for rec in ev["warp"]:
        r = bytearray(rec)
        x, z, dest, anchor = struct.unpack_from("<4H", r, 0)
        if dest in headers:
            struct.pack_into("<4H", r, 0, x - ox, z - oy, headers[dest], anchor)
            stats["warps"] = stats.get("warps", 0) + 1
        else:
            struct.pack_into("<4H", r, 0, WARP_PARKED, WARP_PARKED, 0, 0)
            stats["warps_parked"] = stats.get("warps_parked", 0) + 1
        warps.append(bytes(r))

    stats["signs"] = stats.get("signs", 0) + len(bgs)
    stats["triggers_dropped"] = stats.get("triggers_dropped", 0) + len(ev["coord"])
    return build_events(dict(bg=bgs, obj=objs, warp=warps, coord=[])), used


def decode_bank(d: bytes) -> list[list[int]]:
    """One message bank as lists of charcodes."""
    n, seed = struct.unpack_from("<HH", d, 0)
    out = []
    for i in range(n):
        o, ln = struct.unpack_from("<II", d, 4 + i * 8)
        k = (seed * MSG_TABLE_MUL * (i + 1)) & 0xFFFF
        kk = k | (k << 16)
        o, ln = o ^ kk, ln ^ kk
        k = ((i + 1) * MSG_KEY_START) & 0xFFFF
        msg = []
        for j in range(ln):
            msg.append(struct.unpack_from("<H", d, o + j * 2)[0] ^ k)
            k = (k + MSG_KEY_INC) & 0xFFFF
        out.append(msg)
    return out


def encode_bank(msgs: list[list[int]], seed: int) -> bytes:
    """The inverse, laid out the way the engine reads it: header, table, text."""
    head = 4 + len(msgs) * 8
    body, table = bytearray(), bytearray()
    for i, msg in enumerate(msgs):
        off = head + len(body)
        k = ((i + 1) * MSG_KEY_START) & 0xFFFF
        for c in msg:
            body += struct.pack("<H", c ^ k)
            k = (k + MSG_KEY_INC) & 0xFFFF
        tk = (seed * MSG_TABLE_MUL * (i + 1)) & 0xFFFF
        tkk = tk | (tk << 16)
        table += struct.pack("<II", off ^ tkk, len(msg) ^ tkk)
    return struct.pack("<HH", len(msgs), seed) + bytes(table) + bytes(body)


def charmap(engine: Path) -> dict[str, int]:
    """character -> charcode, from the engine's own msgenc table."""
    f = engine / "tools" / "msgenc" / "charmap.txt"
    if not f.is_file():
        die("no %s, the label's characters have no authority without it" % f)
    out = {}
    for line in f.read_text(errors="replace").splitlines():
        # Trailing spaces are significant here, the file says so in its own
        # header, and the space character's row is exactly a trailing one.
        # Stripping the line drops the code for ' ' and every place name with
        # two words in it becomes unencodable.
        if line.startswith("//"):
            continue
        line = line.rstrip("\r\n")
        m = re.match(r"^([0-9A-Fa-f]{4})=(.)$", line)
        if m:
            out.setdefault(m.group(2), int(m.group(1), 16))
    return out


def rom_members(rom: Path, path: str) -> list[bytes]:
    return _rom(rom).narc_members(path)


def rom_file(rom: Path, path: str) -> bytes:
    return _rom(rom).file_bytes(path)


_ROMS: dict[str, object] = {}


def _rom(rom: Path):
    key = str(rom)
    if key not in _ROMS:
        sys.path.insert(0, str(_engine_pc()))
        from modport import NitroRom        # noqa: E402
        _ROMS[key] = NitroRom(rom)
    return _ROMS[key]


def _engine_pc() -> Path:
    out = subprocess.run([str(MMO / "tools" / "decomp_dir.sh"), "pokeplatinum"],
                         capture_output=True, text=True)
    return Path(out.stdout.strip()) / "pc"


# A package reaches the running game in one of two shapes.
COOK_FNV_OFFSET = 0xCBF29CE484222325

MEMBER_ROOT = {False: "narc", True: ".cooked/narc"}
FILE_ROOT = {False: "replace", True: ".cooked/fs"}

# The two whole files a ported map needs beside its members.
SDAT_PATH = "data/sound/pl_sound_data.sdat"
MATSHP_PATH = "fielddata/build_model/build_model_matshp.dat"
MATSHP_NO_SHAPES = (0, 0xFFFF)


def write_member(pkg: Path, narc: str, index: int, data: bytes,
                 cooked: bool = False) -> None:
    d = pkg / MEMBER_ROOT[cooked] / narc
    d.mkdir(parents=True, exist_ok=True)
    (d / str(index)).write_bytes(data)


def write_generated(pkg: Path, name: str, text: str) -> None:
    d = pkg / ".cooked" / "generated"
    d.mkdir(parents=True, exist_ok=True)
    (d / name).write_text(text)


def write_package_shell(pkg: Path, ident: str, name: str) -> None:
    """mod.toml and the digest, so the tree is loadable as it stands."""
    (pkg / ".cooked").mkdir(parents=True, exist_ok=True)
    (pkg / ".cooked" / "digest").write_text("v1 %016x\n" % COOK_FNV_OFFSET)
    toml = pkg / "mod.toml"
    if not toml.is_file():
        toml.write_text(
            'id = "%s"\n'
            'name = "%s"\n'
            'version = "1.0.0"\n'
            'authors = ["openmmo"]\n'
            'requires = []\n'
            'load_after = []\n' % (ident, name))


def empty_script_archive() -> bytes:
    """One script that does nothing, in this game's own script container."""
    table = struct.pack("<i", 2) + struct.pack("<H", SCRIPT_TABLE_END)
    return table + struct.pack("<H", SCRCMD_END)


INIT_SCRIPT_ON_FRAME_TABLE = 1     # include/constants/init_script_types.h


def arrival_entry(member: bytes) -> int | None:
    """The 1-based script id a map's own arrival table runs on entry, or None."""
    p = 0
    while p + 5 <= len(member) and member[p] != 0:
        kind = member[p]
        if kind == portscript.INIT_SCRIPT_ON_TRANSITION:
            return struct.unpack_from("<H", member, p + 1)[0]
        p += 5
    return None


def _pl_defines(engine: Path, rel: str, names: tuple) -> list[int]:
    """`#define NAME value` rows out of one of the engine build's headers."""
    path = engine / rel
    if not path.is_file():
        die("no %s; build the engine first" % path)
    text = path.read_text()
    out = []
    for name in names:
        m = re.search(r"^#define\s+%s\s+(\d+)\b" % re.escape(name), text, re.M)
        if not m:
            die("%s does not define %s" % (path, name))
        out.append(int(m.group(1)))
    return out


def _script_op(table: dict, name: str) -> tuple[int, int]:
    """(HeartGold's opcode, this game's) for a command mmo/SCRCMD pairs by name."""
    for c in table.values():
        if c.hg_name == name and c.pl is not None:
            return c.hg, c.pl
    die("mmo/SCRCMD pairs no command named %s" % name)


def _bank_member(bodies: list[bytes | None], body: bytes, table_end: int,
                 tail: int, stub_of) -> bytes:
    """A script member of this game's shape: a table of N entries, this game's
    own bank body moved up behind it verbatim, then one stub per entry after
    that. Every jump in the body is relative to itself, so the body can sit
    anywhere; the stubs are handed the tail's new offset to jump to."""
    n = len(bodies)
    head = n * 4 + 2
    at = head + len(body)
    tail_new = head + (tail - table_end)
    stubs = bytearray()
    offsets = []
    for i, spec in enumerate(bodies):
        offsets.append(at + len(stubs))
        stubs += stub_of(i, spec, at + len(stubs), tail_new)
    out = bytearray()
    for i, off in enumerate(offsets):
        out += struct.pack("<i", off - (i * 4 + 4))
    out += struct.pack("<H", SCRIPT_TABLE_END)
    assert len(out) == head
    out += body
    out += stubs
    return bytes(out)


def item_banks(dest_rom: Path, engine: Path, table: dict, hg_member: bytes,
               hidden: dict[int, tuple[int, int]]) -> dict:
    """The two banks a ported map's items run: this game's own, with HeartGold's entries in
    front. See the ITEMS ON THE GROUND note above.
    """
    vis_i, hid_i = _pl_defines(engine, "build/rom/res/field/scripts/scr_seq.naix",
                               PL_ITEM_BANKS)
    vis_t, hid_t = _pl_defines(engine, "build/pc/geninclude/generated/text_banks.h",
                               PL_ITEM_TEXT)
    hidden_flag_start, = _pl_defines(engine, "build/rom/generated/vars_flags.h",
                                     ("HIDDEN_ITEM_FLAGS_START",))
    pl = rom_members(dest_rom, DST_SCRIPT)
    if max(vis_i, hid_i) >= len(pl):
        die("this game's script archive has %d members and its item banks are "
            "%d and %d" % (len(pl), vis_i, hid_i))
    hg_setvar, setvar = _script_op(table, "SetVar")
    hg_goto, goto = _script_op(table, "GoTo")
    _hg_end, end = _script_op(table, "End")

    def stub(member: bytes, at: int, op_setvar: int, op_goto: int):
        """(var, value) pairs and the jump target of a stub, or None."""
        pairs = []
        p = at
        while p + 6 <= len(member):
            op, = struct.unpack_from("<H", member, p)
            if op == op_setvar:
                pairs.append(struct.unpack_from("<HH", member, p + 2))
                p += 6
            elif op == op_goto and p + 6 <= len(member):
                rel, = struct.unpack_from("<i", member, p + 2)
                return pairs, p + 6 + rel
            else:
                return None
        return None

    # The ball bank: this game's own body, HeartGold's stubs.
    vis = pl[vis_i]
    starts, tend = portscript.entry_offsets(vis)
    first = stub(vis, starts[0], setvar, goto) if starts else None
    if not first or first[0] != [(0x8008, 17), (0x8009, 1)]:
        die("this game's visible-item bank (member %d) no longer opens with "
            "Route 202's Potion stub" % vis_i)
    tail = first[1]
    hstarts, _ = portscript.entry_offsets(hg_member)
    if len(hstarts) != HG_ITEM_BANK_COUNT:
        die("HeartGold's item-ball bank has %d entries, not %d"
            % (len(hstarts), HG_ITEM_BANK_COUNT))
    balls: list = []
    for k, at in enumerate(hstarts):
        st = stub(hg_member, at, hg_setvar, hg_goto)
        if st is None or st[1] != hstarts[-1] or len(st[0]) != 2 \
                or [v for v, _ in st[0]] != [0x8008, 0x8009]:
            if k != HG_ITEM_BANK_COUNT - 1:
                die("HeartGold item-ball entry %d is not a stub onto the "
                    "bank's tail" % k)
            balls.append(None)
            continue
        balls.append((st[0][0][1], st[0][1][1]))

    def ball_stub(_i, spec, at, tail_new):
        if spec is None:
            return struct.pack("<H", end)
        item, qty = spec
        b = struct.pack("<HHH", setvar, 0x8008, item)
        b += struct.pack("<HHH", setvar, 0x8009, qty)
        b += struct.pack("<Hi", goto, tail_new - (at + len(b) + 6))
        return b + struct.pack("<H", end)

    ball_blob = _bank_member(balls, vis[tend:], tend, tail, ball_stub)

    # The hidden bank: one body every entry of this game's own jumps to.
    hid = pl[hid_i]
    hstarts, htend = portscript.entry_offsets(hid)
    if not hstarts or len(set(hstarts)) != 1:
        die("this game's hidden-item bank (member %d) is not one routine "
            "behind every entry" % hid_i)
    hidden_base = PL_HIDDEN_ITEMS_BASE + PORTED_ITEM_FLAGS_START - hidden_flag_start
    rows = [hidden.get(i) for i in range(HG_ITEM_BANK_COUNT)]

    # The three values the engine's own table would have set: the item, the
    # count, and the flag, the entry's index on the band, which is where
    # `Script_GetHiddenItemFlag` lands for the id this stub answers to.
    def hidden_stub(i, spec, at, tail_new):
        if spec is None:
            return struct.pack("<H", end)
        item, qty = spec
        b = struct.pack("<HHH", setvar, 0x8000, item)
        b += struct.pack("<HHH", setvar, 0x8001, qty)
        b += struct.pack("<HHH", setvar, 0x8002, PORTED_ITEM_FLAGS_START + i)
        b += struct.pack("<Hi", goto, tail_new - (at + len(b) + 6))
        return b + struct.pack("<H", end)

    hidden_blob = _bank_member(rows, hid[htend:], htend, hstarts[0], hidden_stub)
    return dict(ball_blob=ball_blob, hidden_blob=hidden_blob,
                ball_text=vis_t, hidden_text=hid_t, ball_base=PORTED_BALL_BASE,
                hidden_base=hidden_base, hidden=hidden,
                balls=sum(1 for b in balls if b is not None),
                members=(vis_i, hid_i), tails=(tail, hstarts[0]))


def apricorn_bank(table: dict) -> bytes:
    """The ten entries of the Apricorn tree's conversation, in this game's
    opcodes, printing HeartGold's own lines (see HG_APRICORN_TREE above)."""
    by_name = {c.hg_name: c for c in table.values() if c.pl is not None}

    def op(name: str, *ops: int) -> bytes:
        c = by_name.get(name)
        if c is None:
            die("mmo/SCRCMD pairs no command named %s for the apricorn bank" % name)
        if len(ops) != len(c.widths):
            die("%s takes %d operand(s), the apricorn bank gave %d"
                % (name, len(c.widths), len(ops)))
        out = struct.pack("<H", c.pl)
        for w, v in zip(c.widths, ops):
            out += int(v).to_bytes(w, "little")
        return out

    def line(msg: int) -> bytes:
        return (op("LockAll") + op("FacePlayer") + op("NPCMsg", msg) + op("WaitButton")
                + op("CloseMsg") + op("ReleaseAll") + op("End"))

    bodies = [line(0), line(2), line(7)]
    for kind in range(APRICORN_KINDS):
        item = APRICORN_FIRST_ITEM + kind
        bodies.append(op("LockAll") + op("FacePlayer")
                      + op("BufferItemName", 1, item) + op("NPCMsg", 1) + op("WaitButton")
                      + op("PlayFanfare", PL_SEQ_FANFA4)
                      + op("BufferPlayersName", 0) + op("BufferItemName", 1, item)
                      + op("NPCMsg", 3) + op("WaitFanfare") + op("WaitButton")
                      + op("NPCMsg", 4) + op("WaitButton")
                      + op("CloseMsg") + op("ReleaseAll") + op("End"))
    head = len(bodies) * 4 + 2
    out = bytearray()
    at = head
    for i, body in enumerate(bodies):
        out += struct.pack("<i", at - (i * 4 + 4))
        at += len(body)
    out += struct.pack("<H", SCRIPT_TABLE_END)
    for body in bodies:
        out += body
    return bytes(out)


def fieldmove_bank(table: dict, pl: dict) -> bytes:
    """The two entries of the field-move bank, in this game's opcodes, printing HeartGold's own
    lines (see HG_FIELDMOVE_MSG above).
    """
    by_name = {c.hg_name: c for c in table.values() if c.pl is not None}

    def op(name: str, *ops: int) -> bytes:
        """A command by the SOURCE's name, through the pairing."""
        c = by_name.get(name)
        if c is None:
            die("mmo/SCRCMD pairs no command named %s for the field-move bank" % name)
        return emit(c.pl, c.widths, name, ops)

    def own(name: str, *ops: int) -> bytes:
        """A command by THIS game's name, one the source has no pair for."""
        if name not in pl:
            die("this game's scrcmd.inc has no macro %s for the field-move bank" % name)
        num, widths = pl[name]
        return emit(num, widths, name, ops)

    def emit(num: int, widths: list, name: str, ops: tuple) -> bytes:
        if len(ops) != len(widths):
            die("%s takes %d operand(s), the field-move bank gave %d"
                % (name, len(widths), len(ops)))
        out = struct.pack("<H", num)
        for w, v in zip(widths, ops):
            out += int(v).to_bytes(w, "little", signed=v < 0)
        return out

    def branch(cond: int, *between: bytes) -> bytes:
        """GoToIf over `between`, onto whatever follows: the jump is relative
        to the word after itself."""
        skip = b"".join(between)
        return op("GoToIf", cond, len(skip)) + skip

    # The tail every path ends on.
    ending = op("CloseMsg") + op("ReleaseAll") + op("End")
    # The two refusals, laid out after the main line so a branch skips forward.
    no_move = op("NPCMsg", HG_FIELDMOVE_MSG_BIG_TREE) + op("WaitButton") + ending
    # The shake, and the wait for it (VAR_0x8005 goes non-zero when it ends).
    wait = op("Wait", 1, PL_VAR_RESULT) + op("CompareVarToValue", PL_VAR_8005, 0)
    loop = wait + op("GoToIf", PL_COND_EQ, -(len(wait) + 2 + 1 + 4))
    shake = (op("CloseMsg")
             + own("FindPartySlotWithMove", PL_VAR_8004, PL_MOVE_HEADBUTT)
             + own("BufferPartyMonNickname", 0, PL_VAR_8004)
             + op("NPCMsg", HG_FIELDMOVE_MSG_USED) + op("CloseMsg")
             + own("PlayHMCutIn", PL_VAR_8004)
             + own("StartDestroyObstacleAnimation", PL_OBSTACLE_HEADBUTT, PL_VAR_8005)
             + op("Wait", 7, PL_VAR_RESULT)
             + loop
             + op("ReleaseAll") + op("End"))
    asked = (op("NPCMsg", HG_FIELDMOVE_MSG_TREE)
             + op("YesNo", PL_VAR_RESULT)
             + op("CompareVarToValue", PL_VAR_RESULT, PL_MENU_YES)
             + branch(PL_COND_NE, shake)
             + ending)
    headbutt = (op("LockAll")
                + own("FindPartySlotWithMove", PL_VAR_RESULT, PL_MOVE_HEADBUTT)
                + op("CompareVarToValue", PL_VAR_RESULT, PL_MAX_PARTY_SIZE)
                + branch(PL_COND_EQ, asked)
                + no_move)
    # The client hands the item and its count over as the script's parameters,
    # which are VAR_0x8000 and VAR_0x8001 (SCRIPT_DATA_PARAMETER_0 up); the
    # obtain routine reads VAR_0x8004 and VAR_0x8005, so the entry copies them
    # first, as this game's own hidden-item stubs do.
    rubble = (op("LockAll")
              + op("CopyVar", PL_VAR_8004, PL_VAR_8000)
              + op("CopyVar", PL_VAR_8005, PL_VAR_8001)
              + op("BufferItemName", 1, PL_VAR_8004)
              + op("NPCMsg", HG_FIELDMOVE_MSG_RUBBLE)
              + op("CallStd", PL_COMMON_OBTAIN)
              + ending)
    bodies = [headbutt, rubble]
    head = len(bodies) * 4 + 2
    out = bytearray()
    at = head
    for i, body in enumerate(bodies):
        out += struct.pack("<i", at - (i * 4 + 4))
        at += len(body)
    out += struct.pack("<H", SCRIPT_TABLE_END)
    for body in bodies:
        out += body
    return bytes(out)


def _ncgr_tiles(member: bytes) -> tuple[bytes, int, int]:
    """An NCGR's tile bytes, and where they sit: (data, offset, size)."""
    if member[:4] != b"RGCN":
        die("a badge sheet that is not an NCGR (%r)" % member[:4])
    p = member.find(b"RAHC")
    _h, _w, _depth, _mapping, _unk, size, dataoff = struct.unpack_from("<HHIIIII", member, p + 8)
    at = p + 8 + dataoff
    return member[at:at + size], at, size


def _nclr_colours(member: bytes) -> tuple[int, int]:
    """Where an NCLR's colours sit: (offset, byte count)."""
    if member[:4] != b"RLCN":
        die("a badge palette that is not an NCLR (%r)" % member[:4])
    p = member.find(b"TTLP")
    datalen = struct.unpack_from("<I", member, p + 16)[0]
    dataoff = struct.unpack_from("<I", member, p + 20)[0]
    return p + 8 + dataoff, datalen


def card_page(hg: list[bytes]) -> list[tuple[str, bytes]]:
    """HeartGold's LEAGUE BADGES page and its badge sprites, member for member."""
    out = []
    for key, idx, magic in HG_CARD_PAGE:
        if idx >= len(hg):
            die("%s holds %d members and the card's %s is member %d" % (SRC_CARD, len(hg), key, idx))
        member = hg[idx]
        if member[:4] != magic:
            die("member %d of %s is %r, and the card's %s wants %r"
                % (idx, SRC_CARD, member[:4], key, magic))
        if key == "badge_anim":
            p = member.find(b"KNBA")
            nseq = struct.unpack_from("<H", member, p + 8)[0]
            if nseq != HG_CARD_BADGES:
                die("%s animation bank holds %d sequences, not one a badge (%d)"
                    % (SRC_CARD, nseq, HG_CARD_BADGES))
        out.append((key, member))
    return out


def init_table_transition(script_id: int) -> bytes:
    """An arrival-script table that runs one script on entry and nothing else:
    the shape 165 of this game's own maps ship, which check_empties proves
    against the image before the first one is written."""
    body = bytes([portscript.INIT_SCRIPT_ON_TRANSITION]) \
        + struct.pack("<HH", script_id, 0) + b"\0"
    return body + bytes(-len(body) % 4)


# THE TRIP VARS.
TRIP_VARS = {0x411E, 0x411F}
TRIP_FRAME_VAR = portscript.MAP_LOCAL_VAR_FIRST + 1     # VAR_TEMP_1: low byte 0x01


def trip_frame_rows(member: bytes, kept: list[bool] | None) -> list[tuple[int, int, int]]:
    """The frame-table rows of a map's arrival table that a visitor keeps:
    (var, value, script), for the rows on a trip var whose scene folded."""
    if not kept:
        return []
    out = []
    p = 0
    while p + 5 <= len(member) and member[p] != 0:
        kind = member[p]
        if kind == INIT_SCRIPT_ON_FRAME_TABLE:
            rel = struct.unpack_from("<i", member, p + 1)[0]
            at = p + 5 + rel
            while at + 6 <= len(member):
                var, value, script = struct.unpack_from("<HHH", member, at)
                if var == 0:
                    break
                if var in TRIP_VARS and 1 <= script <= len(kept) and kept[script - 1]:
                    out.append((TRIP_FRAME_VAR, TRIP_FRAME_VAR, script))
                at += 6
        p += 5
    return out


def init_table_frame(arrival: int | None, rows: list[tuple[int, int, int]]) -> bytes:
    """An arrival table with a frame table: the fixed entry if there is one, then the frame
    entry whose pointer is relative to the word after itself (`ScriptEntry` in both games'
    script macros), the end byte, and the rows, three shorts each, closed by a zero short
    -- right behind it, the way both assemblers lay it out with alignment off.
    """
    body = bytearray()
    if arrival is not None:
        body += bytes([portscript.INIT_SCRIPT_ON_TRANSITION]) \
            + struct.pack("<HH", arrival, 0)
    at = len(body)
    body += bytes([INIT_SCRIPT_ON_FRAME_TABLE]) + bytes(4)
    body += b"\0"
    struct.pack_into("<i", body, at + 1, len(body) - (at + 5))
    for var, value, script in rows:
        body += struct.pack("<HHH", var, value, script)
    body += struct.pack("<H", 0)
    return bytes(body) + bytes(-len(body) % 4)


def empty_init_scripts() -> bytes:
    """The arrival-script table of a map with no arrival scripts."""
    return bytes(4)


def empty_events() -> bytes:
    """A zone_event member with no people, warps, triggers or signs: four
    counts, all zero, and no arrays behind them."""
    return struct.pack("<4I", 0, 0, 0, 0)


def empty_message_bank() -> bytes:
    """A one-message bank whose message is empty, for a map that has no text of
    its own but whose header still has to name a bank."""
    return encode_bank([[0xFFFF]], 1)


def check_empties(rom_members_of, checks: list[tuple[str, str, bytes]]) -> None:
    """Every synthesised member has to be one the destination already ships."""
    for what, narc, blob in checks:
        pool = rom_members_of(narc)
        n = sum(1 for m in pool if m == blob)
        if n == 0:
            die("the %s this builds (%d bytes) is byte-identical to no member "
                "of %s, the shape was guessed, not read" % (what, len(blob), narc))
        print("portmap: %s matches %d of this game's own %s members"
              % (what, n, narc.rsplit("/", 1)[-1]))


def parse_args(argv: list[str]) -> dict:
    """The command line: one package, one destination image, and N maps."""
    opts = dict(rom=None, pkg=None, dest_rom=None, sdat=None, maps=[], over={},
                region=[], header_base=FIRST_FREE_HEADER, music=None,
                scripts=False, trainers=False)
    cur = None
    i = 1
    while i < len(argv):
        a = argv[i]
        if a == "--map":
            if i + 1 >= len(argv):
                die("--map needs a name from mmo/MAPS")
            cur = dict(name=argv[i + 1], header=None, label=None, bgm=None)
            opts["maps"].append(cur)
            i += 2
            continue
        if a in ("--header", "--bgm"):
            if cur is None:
                die("%s belongs to a --map and none has been named yet" % a)
            if i + 1 >= len(argv):
                die("%s needs a number" % a)
            cur[a[2:]] = int(argv[i + 1], 0)
            i += 2
            continue
        if a == "--label":
            if cur is None:
                die("--label belongs to a --map and none has been named yet")
            if i + 1 >= len(argv):
                die("--label needs the place name to draw")
            cur["label"] = argv[i + 1]
            i += 2
            continue
        if a == "--region":
            if i + 1 >= len(argv):
                die("--region needs the word mmo/MAPS uses (johto, kanto)")
            # Several regions in one run, because a package's appended members
            # must start at the built image's counts and run without a gap:
            # two runs would both start there and claim the same numbers. It is
            # also what makes the doors between them work.
            opts["region"] += [r for r in argv[i + 1].split(",") if r]
            i += 2
            continue
        if a == "--header-base":
            if i + 1 >= len(argv):
                die("--header-base needs the first free header id")
            opts["header_base"] = int(argv[i + 1], 0)
            i += 2
            continue
        if a == "--music":
            opts["music"] = True
            i += 1
            continue
        if a == "--scripts":
            opts["scripts"] = True
            i += 1
            continue
        if a == "--trainers":
            opts["trainers"] = True
            i += 1
            continue
        if a == "--sdat":
            if i + 1 >= len(argv):
                die("--sdat needs the sound archive tools/portmusic.py wrote")
            opts["sdat"] = Path(argv[i + 1])
            i += 2
            continue
        if a == "--dest-rom":
            if i + 1 >= len(argv):
                die("--dest-rom needs THIS game's image; the place-name bank "
                    "a label is appended to is the destination's, not the "
                    "cartridge's")
            opts["dest_rom"] = Path(argv[i + 1])
            i += 2
            continue
        if a in ("--matrix", "--area", "--land", "--texset",
                 "--propmodel", "--areabuild", "--proptex",
                 "--script", "--events", "--msg", "--mmodel", "--mmlist"):
            if i + 1 >= len(argv):
                die("%s needs a member number" % a)
            opts["over"][a[2:]] = int(argv[i + 1], 0)
            i += 2
            continue
        if a.startswith("--"):
            die("unknown option %s" % a)
        if opts["rom"] is None:
            opts["rom"] = Path(a)
        elif opts["pkg"] is None:
            opts["pkg"] = Path(a)
        elif not opts["maps"]:
            opts["maps"].append(dict(name=a, header=None, label=None, bgm=None))
            cur = opts["maps"][0]
        else:
            die("unexpected argument %r" % a)
        i += 1
    if opts["rom"] is None or opts["pkg"] is None or (
            not opts["maps"] and not opts["region"]):
        die("usage: portmap.py <rom> <package-dir> [--dest-rom PATH]\n"
            "         --region NAME [--header-base N]\n"
            "         --map NAME [--header N] [--label NAME] [--bgm N] ...\n"
            "         [--sdat PATH | --music] [--scripts] [--trainers]\n"
            "       archive overrides: --matrix/--area/--land/--texset/"
            "--propmodel/\n"
            "         --areabuild/--proptex/--script/--events/--msg/"
            "--mmodel/--mmlist N")
    return opts


class Cursor:
    """Where the next appended member of each archive goes."""

    def __init__(self, over: dict[str, int]):
        self.at = {}
        for narc, count in PL_COUNT.items():
            self.at[narc] = count
        for key, narc in (("land", DST_LAND), ("matrix", DST_MATRIX),
                          ("area", DST_AREA), ("texset", DST_TEXSET),
                          ("propmodel", DST_PROPMODEL),
                          ("areabuild", DST_AREABUILD),
                          ("proptex", DST_PROPTEX), ("script", DST_SCRIPT),
                          ("events", DST_EVENTS), ("msg", DST_MSG),
                          ("mmodel", DST_MMODEL), ("mmlist", DST_MMLIST)):
            if key in over:
                self.at[narc] = over[key]

    def take(self, narc: str, n: int = 1) -> int:
        first = self.at[narc]
        self.at[narc] = first + n
        return first


def region_run(maps: dict[str, dict], regions: list[str],
               base: int) -> list[dict]:
    """Every map of the named regions, with the header id each answers to here."""
    out = []
    skipped: dict[str, int] = {}
    for region in regions:
        rows = sorted((r for r in maps.values() if r["region"] == region),
                      key=lambda r: r["header"])
        if not rows:
            die("no map in mmo/MAPS is in region %r; the column takes the word "
                "HeartGold's own headers use (johto, kanto)" % region)
        for row in rows:
            if row["kind"] not in MAP_KINDS:
                skipped[row["kind"]] = skipped.get(row["kind"], 0) + 1
                continue
            out.append(dict(name=row["name"], header=base + row["header"],
                            label=row["place"], bgm=None))
    if skipped:
        print("portmap: %s leaves out %s, kinds nothing has been ported of"
              % (", ".join(regions), ", ".join("%d %s" % (v, k)
                                               for k, v in sorted(skipped.items()))))
    return out


def port_texture_animation(rom: Path, dest_rom: Path, pkg: Path,
                           cur: "Cursor") -> list[str]:
    """Merge the source's animated-texture table into this game's, frames and all."""
    src = rom_members(rom, SRC_TANIME)
    dst = rom_members(dest_rom, DST_TANIME)
    if len(dst) != PL_COUNT[DST_TANIME]:
        die("this game's %s holds %d members and PL_COUNT says %d"
            % (DST_TANIME, len(dst), PL_COUNT[DST_TANIME]))

    def rows(members: list[bytes], whose: str) -> int:
        n = struct.unpack_from("<I", members[0], 0)[0]
        if len(members[0]) != 4 + n * TANIME_ROW or n != len(members) - 1:
            die("%s %s is not a texture animation table (%d rows, %d bytes, "
                "%d members)" % (whose, SRC_TANIME, n, len(members[0]),
                                 len(members)))
        return n

    npl = rows(dst, "this game's")
    nhg = rows(src, "the source's")
    first = cur.take(DST_TANIME, nhg)
    if first != npl + 1:
        die("%s: the frame members start at %d, not after this game's %d"
            % (DST_TANIME, first, npl))
    write_member(pkg, DST_TANIME, 0,
                 struct.pack("<I", npl + nhg) + dst[0][4:] + src[0][4:],
                 cooked=True)
    names = []
    for i in range(nhg):
        write_member(pkg, DST_TANIME, first + i, src[1 + i], cooked=True)
        o = 4 + i * TANIME_ROW
        names.append(src[0][o:o + TANIME_NAME].split(b"\0")[0]
                     .decode("ascii", "replace"))
    return names


def port_area_windows(rom: Path, dest_rom: Path, pkg: Path,
                      cur: "Cursor") -> int:
    """Append the source's nine place-name boards after this game's nine."""
    src = rom_members(rom, SRC_AREAWIN)
    dst = rom_members(dest_rom, DST_AREAWIN)
    if len(dst) != PL_COUNT[DST_AREAWIN]:
        die("this game's %s holds %d members and PL_COUNT says %d"
            % (DST_AREAWIN, len(dst), PL_COUNT[DST_AREAWIN]))
    if (len(src) != AREAWIN_MEMBERS
            or any(m[:4] != AREAWIN_SHEET for m in src[0::2])
            or any(m[:4] != AREAWIN_PALETTE for m in src[1::2])
            or any(len(a) != len(b) for a, b in zip(src, dst))):
        die("%s is not the source's place-name signs: %d members, %s"
            % (SRC_AREAWIN, len(src), [len(m) for m in src[:4]]))
    first = cur.take(DST_AREAWIN, len(src))
    if first != len(dst):
        die("%s: the boards start at %d, not after this game's %d"
            % (DST_AREAWIN, first, len(dst)))
    for i, m in enumerate(src):
        write_member(pkg, DST_AREAWIN, first + i, m, cooked=True)
    return first // 2


# ---------------------------------------------------------------------
# pokegear
POKEGEAR_NARCS = (
    ("a/1/4/3", "application/pokegear/pgear_gra.narc"),
    ("a/1/4/4", "application/pokegear/map/pgmap_gra.narc"),
    ("a/1/4/5", "application/pokegear/configure/pgconf_gra.narc"),
    ("a/1/4/6", "application/pokegear/phone/pgphone_gra.narc"),
    ("a/1/4/7", "application/pokegear/radio/pgradio_gra.narc"),
    ("a/0/3/7", "fielddata/encountdata/g_enc_data.narc"),
)
POKEGEAR_FILES = (("tel/pmtel_book.dat", "tel/pmtel_book.dat"),)
# The map's, the skins', the radio's, the phone's, the twelve shows', and
# one bank per phone contact.
POKEGEAR_BANKS = ([269, 270, 271, 273] + list(range(409, 421))
                  + list(range(640, 717)))
POKEGEAR_SEQS = (
    "SEQ_GS_RADIO_JINGLE", "SEQ_GS_RADIO_KOMORIUTA", "SEQ_GS_RADIO_MARCH",
    "SEQ_GS_RADIO_UNKNOWN", "SEQ_GS_RADIO_R_101", "SEQ_GS_RADIO_R_201",
    "SEQ_GS_RADIO_TRAINER", "SEQ_GS_RADIO_PT", "SEQ_GS_RADIO_VARIETY",
    "SEQ_GS_OHKIDO_RABO", "SEQ_GS_HUE", "SEQ_GS_AIKOTOBA",
    "SEQ_GS_SENKYO_R", "SEQ_GS_KAIDENPA",
)
POKEGEAR_REGIONS = {"johto": 0, "kanto": 1}


def _heartgold_dir() -> Path | None:
    out = subprocess.run([str(MMO / "tools" / "decomp_dir.sh"), "pokeheartgold"],
                         capture_output=True, text=True)
    p = Path(out.stdout.strip()) if out.stdout.strip() else None
    return p if p is not None and p.is_dir() else None


def pokegear_header_fields(hg: Path) -> dict[str, tuple[int, int, int, int]]:
    """HeartGold map name -> (worldMapX, worldMapY, radioSignal, outgoingCalls),
    off the decompilation's own header table (src/data/map_headers.h)."""
    out: dict[str, tuple[int, int, int, int]] = {}
    text = (hg / "src/data/map_headers.h").read_text()
    for m in re.finditer(r"\[MAP_(\w+)\]\s*=\s*\{(.*?)\n\s*\}", text, re.S):
        name, body = m.group(1).lower(), m.group(2)
        wx = re.search(r"\.worldMapX\s*=\s*(\d+)", body)
        wy = re.search(r"\.worldMapY\s*=\s*(\d+)", body)
        radio = re.search(r"\.radioSignal\s*=\s*(TRUE|FALSE|\d)", body)
        calls = re.search(r"\.outgoingCalls\s*=\s*(TRUE|FALSE|\d)", body)
        out[name] = (int(wx.group(1)) if wx else 0, int(wy.group(1)) if wy else 0,
                     1 if radio and radio.group(1) in ("TRUE", "1") else 0,
                     1 if calls and calls.group(1) in ("TRUE", "1") else 0)
    return out


def pokegear_encounter_members(hg: Path) -> dict[str, int]:
    """ENCDATA name -> member of g_enc_data.narc (encounter_tables_narc.h)."""
    out: dict[str, int] = {}
    text = (hg / "include/encounter_tables_narc.h").read_text()
    for m in re.finditer(r"#define\s+ENCDATA_(\w+)\s+ENCDATA\(_(\d+)\)", text):
        out[m.group(1)] = int(m.group(2))
    return out


def port_pokegear(rom: Path, pkg: Path, opts: dict, maps: dict, msg_src: list,
                  cur, music: dict, box: dict, region_matrix: dict) -> None:
    hg = _heartgold_dir()
    if hg is None:
        print("portmap: no heartgold checkout, so the pokegear is not carried")
        return
    lines = ["# pokegear.txt, GENERATED by tools/portmap.py; where HeartGold's",
             "# device landed in this package (mods/openmmo/src/openmmo_pokegear.c).",
             "#   narc <index> <path>       the archive, whole, at its own path",
             "#   bank <hg bank> <member>   a HeartGold text bank in pl_msg.narc",
             "#   seq <name> <id>           a radio track in the sound archive",
             "#   box <region> <matrix> <x0> <y0> <w> <h>  the region's cut of the plane",
             "#   map <header> <region> <wx> <wy> <radio> <calls> <enc>"]
    for i, (src, dst) in enumerate(POKEGEAR_NARCS):
        dest = pkg / FILE_ROOT[True] / dst
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(rom_file(rom, src))
        lines.append("narc %d %s" % (i, dst))
    for src, dst in POKEGEAR_FILES:
        dest = pkg / FILE_ROOT[True] / dst
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(rom_file(rom, src))
    banks = 0
    for hg_bank in POKEGEAR_BANKS:
        if hg_bank >= len(msg_src):
            continue
        member = cur.take(DST_MSG)
        write_member(pkg, DST_MSG, member, msg_src[hg_bank], cooked=True)
        lines.append("bank %d %d" % (hg_bank, member))
        banks += 1
    # Buena's password answers sit in the Radio Tower's own bank, which the
    # map carried already; the radio reads it through its cooked member.
    for m in opts["maps"]:
        if maps[m["name"]]["msg"] == 66 and m.get("pl_msg") is not None:
            lines.append("bank 66 %d" % m["pl_msg"])
            break
    seqs = 0
    for name in POKEGEAR_SEQS:
        if name in music:
            lines.append("seq %s %d" % (name, music[name]))
            seqs += 1
    for region, (x0, y0, w, h) in sorted(box.items()):
        if region in POKEGEAR_REGIONS:
            lines.append("box %d %d %d %d %d %d"
                         % (POKEGEAR_REGIONS[region], region_matrix[region], x0, y0, w, h))
    fields = pokegear_header_fields(hg)
    encs = pokegear_encounter_members(hg)
    for m in opts["maps"]:
        row = maps[m["name"]]
        wx, wy, radio, calls = fields.get(m["name"], (0, 0, 0, 0))
        enc = encs.get(row["enc"], -1) if row["enc"] else -1
        lines.append("map %d %d %d %d %d %d %d"
                     % (m["header"], POKEGEAR_REGIONS.get(m["region_of"], 0),
                        wx, wy, radio, calls, enc))
    write_generated(pkg, "pokegear.txt", "\n".join(lines) + "\n")
    print("portmap: pokegear: %d archives, %d text banks, %d radio tracks, "
          "%d maps -> .cooked/generated/pokegear.txt"
          % (len(POKEGEAR_NARCS), banks, seqs, len(opts["maps"])))


def port_music(rom: Path, dest_rom: Path, wanted: list[str]
               ) -> tuple[bytes, dict]:
    """Every track a run's maps ask for, appended to this game's sound archive."""
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import portmusic                                       # noqa: E402
    import tempfile                                        # noqa: E402

    def sdat_of(image: Path, paths: tuple[str, ...]) -> Path:
        for path in paths:
            try:
                blob = rom_file(image, path)
            except SystemExit:
                continue
            tmp = Path(tempfile.mkstemp(suffix=".sdat")[1])
            tmp.write_bytes(blob)
            return tmp
        die("%s carries none of %s, --music needs the sound archive out of "
            "each image" % (image, ", ".join(paths)))

    source = portmusic.Sdat(sdat_of(rom, SRC_SDAT_PATHS))
    out = portmusic.Sdat(sdat_of(dest_rom, (SDAT_PATH,)))
    ids: dict[str, int] = {}
    for want in wanted:
        if want not in source.names["SEQ"]:
            die("no sequence called %s in %s, mmo/MAPS names it and the "
                "source archive does not have it" % (want, src))
        i = source.names["SEQ"].index(want)
        seq = len(out.info["SEQ"])
        bank, _ = portmusic.append_closure(source, i, out)
        rec = bytearray(source.info["SEQ"][i])
        out.fat.append(source.fat[source.seq_file(i)])
        struct.pack_into("<H", rec, 0, len(out.fat) - 1)
        struct.pack_into("<H", rec, 4, bank)
        out.info["SEQ"].append(bytes(rec))
        out.names["SEQ"].append(want + "_PORTED")
        ids[want] = seq
    return out.build(), ids


def main(argv: list[str]) -> int:
    opts = parse_args(argv)
    rom, pkg, dest_rom = opts["rom"], opts["pkg"], opts["dest_rom"]

    maps = load_maps(MMO / "MAPS")
    sprites = load_sprites(MMO / "SPRITES")
    scenes = load_scenes(MMO / "MAPSCENES")
    walls = load_walls(MMO / "MAPWALLS")
    tmap = load_terrain_map(MMO / "TERRAIN_MAP")
    # Where the story left every variable and flag, by number: the fold's
    # premise (tools/gen_storyend.py; the scenes above were run from it).
    story = gen_storyend.load(MMO / "STORYEND")
    if not story[0]:
        die("no rows in %s; run tools/gen_storyend.py" % (MMO / "STORYEND"))

    if opts["region"]:
        if opts["maps"]:
            die("--region ports whole regions and --map ports named maps; "
                "one run does one of them")
        opts["maps"] = region_run(maps, opts["region"], opts["header_base"])

    for m in opts["maps"]:
        if m["name"] not in maps:
            die("no map called %r in mmo/MAPS" % m["name"])
        row = maps[m["name"]]
        if row["kind"] not in MAP_KINDS:
            die("%s is a %r and nothing of that kind has been ported: its "
                "presentation fields would be a city's with nothing behind "
                "them" % (m["name"], row["kind"]))
        if m["header"] is None:
            die("%s needs a --header: a map with no header id of its own is "
                "one nothing can address" % m["name"])
        if m["label"] is None:
            m["label"] = row["place"]
        m["header_src"] = row["header"]
        m["area"] = row["area"]
        m["matrix"] = row["matrix"]
        m["events_src"] = row["events"]
        m["hdr_src"] = row["hdr"]
        m["icon"] = row["icon"]
        m["cam"] = row["cam"]
        m["kind"] = row["kind"]
        m["bgm_id"] = m["bgm"]
        m["src_bgm"] = row["bgm"]
        if row["bg"] is not None and row["bg"] not in BATTLE_BG:
            die("%s fights on HeartGold's %s backdrop, which has no slot in "
                "this game's battle_backgrounds.h" % (m["name"], row["bg"]))
        m["battle_bg"] = (BATTLE_BG[row["bg"]] if row["bg"] is not None
                          else KINDS[MAP_KINDS[row["kind"]]]["battle_bg"])
    if len({m["header"] for m in opts["maps"]}) != len(opts["maps"]):
        die("two maps in this package answer to the same header id")
    if len({m["name"] for m in opts["maps"]}) != len(opts["maps"]):
        die("a map is named twice in this run")
    if opts["music"] and opts["sdat"] is not None:
        die("--sdat hands over an archive somebody else built and --music "
            "builds one here; a run does one of them")

    cur = Cursor(opts["over"])
    src_matrices = rom_members(rom, SRC_MATRIX)
    overworld = parse_matrix(src_matrices[0])
    land_src = rom_members(rom, SRC_LAND)
    prop_archives = {SRC_PROPMODEL: rom_members(rom, SRC_PROPMODEL),
                     SRC_PROPROOM: rom_members(rom, SRC_PROPROOM)}
    build_src = rom_members(rom, SRC_AREABUILD)
    area_src = rom_members(rom, SRC_AREA)
    proptex_src = rom_members(rom, SRC_PROPTEX)
    texset_src = rom_members(rom, SRC_TEXSET)
    event_src = rom_members(rom, SRC_EVENTS)
    mmodel_src = rom_members(rom, SRC_MMODEL)
    script_src = rom_members(rom, SRC_SCRIPT) if opts["scripts"] else []
    msg_src = rom_members(rom, SRC_MSG) if opts["scripts"] else []
    cmds = {}
    if opts["scripts"]:
        cmds = portscript.load_table(MMO / "SCRCMD")
        if not cmds:
            die("mmo/SCRCMD pairs nothing; run tools/gen_scripts.py")
        print("portmap: %d script command(s) this port may carry, out of "
              "mmo/SCRCMD" % len(cmds))

    # A warp names the SOURCE map's header id. This is the only thing that turns
    # one into a door that works: source header -> the header this port gave it.
    # A destination that is not in this package has nowhere to go and is parked.
    warp_dest = {m["header_src"]: m["header"] for m in opts["maps"]}

    # ------------------------------------------------------------ pass 1: land
    #
    # One MATRIX for THE whole OVERWORLD, which is the difference between a
    # region and a pile of cities. HeartGold draws Johto the way this game draws
    # Sinnoh: every outdoor map is a stretch of one shared matrix, and the
    # header under the player is read out of the matrix's own header grid as
    # they walk (`FieldMap_ChangeZone`). Cut each map into a matrix of its own
    # and every road out of a town becomes a wall. So a run carrying more than
    # one overworld map cuts the rectangle they all sit in, once, and gives the
    # cells the header ids this port handed out.
    #
    # The rectangle is measured over the maps this run carries and NOT over
    # `everywhere`, the sea that fills the gaps between them: its cells run the
    # whole width of both regions, so counting them would ask for a matrix
    # wider than the engine's own bound and cover Kanto besides. Its cells
    # inside the rectangle still cross, which is what makes the water between
    # two roads water rather than a hole.
    # One MATRIX per region, and that is why two of them cannot be one. Johto
    # is 24x14 cells of the shared overworld and Kanto 23x15, both inside the
    # engine's 30x30; the two together span 45 columns and would fit in
    # nothing. They are still one package, the doors between them only work
    # because both ends are in it, and crossing one is a matrix change, which
    # is what a door has always been.
    for m in opts["maps"]:
        m["region_of"] = maps[m["name"]]["region"]
    overworld_maps = [m for m in opts["maps"] if m["matrix"] == 0]
    box: dict[str, tuple[int, int, int, int]] = {}
    for region in sorted({m["region_of"] for m in overworld_maps}):
        mine = [m for m in overworld_maps if m["region_of"] == region]
        edge = {m["header_src"] for m in mine} - {HG_HEADER_EVERYWHERE}
        cells = [i for i, hid in enumerate(overworld["hdrs"])
                 if hid in edge and overworld["land"][i] != EMPTY_CELL]
        if not cells:
            die("no map of %s owns a cell of the overworld matrix" % region)
        xs = [c % overworld["w"] for c in cells]
        ys = [c // overworld["w"] for c in cells]
        w = max(xs) - min(xs) + 1
        h = max(ys) - min(ys) + 1
        if w > MAP_MATRIX_MAX_WIDTH or h > MAP_MATRIX_MAX_HEIGHT:
            die("%s spans %dx%d cells of the overworld and the engine's matrix "
                "is at most %dx%d, this is more of the world than one matrix "
                "holds" % (region, w, h, MAP_MATRIX_MAX_WIDTH,
                           MAP_MATRIX_MAX_HEIGHT))
        box[region] = (min(xs), min(ys), w, h)

    # Where each map's cells are, and which land members its area needs. A land
    # member is keyed by its AREA as well as its number, because the props
    # inside it are renumbered against the models that area carries; no member
    # of Johto's overworld is in two areas, and this is what keeps that true
    # rather than assumed.
    area_of = {m["header_src"]: m["area"] for m in opts["maps"]}
    area_land: dict[int, list[int]] = {}
    interior_cells: dict[tuple[int, int], dict] = {}

    def want_land(area: int, cid: int) -> None:
        seen = area_land.setdefault(area, [])
        if cid not in seen:
            seen.append(cid)

    region_cells: dict[str, list[tuple[int, int, int]]] = {}
    for region, (x0, y0, w, h) in box.items():
        owners = {m["header_src"] for m in overworld_maps
                  if m["region_of"] == region}
        out: list[tuple[int, int, int]] = []
        for y in range(y0, y0 + h):
            for x in range(x0, x0 + w):
                i = y * overworld["w"] + x
                hid = overworld["hdrs"][i]
                cid = overworld["land"][i]
                if cid == EMPTY_CELL or hid not in owners:
                    out.append((EMPTY_CELL, EMPTY_CELL, 0))
                    continue
                want_land(area_of[hid], cid)
                out.append((cid, hid, overworld["alt"][i]))
        region_cells[region] = out
    for m in opts["maps"]:
        # Every map's area gets a record even when no cell of it crossed,
        # HeartGold has maps that own no cell of the matrix they name, and one
        # of them is in Johto (`unused`). Its header still has to point at an
        # area, and an area with no land is a preload list and a texture set
        # like any other.
        area_land.setdefault(m["area"], [])
        if m["matrix"] == 0:
            continue
        key = (m["matrix"], m["area"])
        if key not in interior_cells:
            own = parse_matrix(src_matrices[m["matrix"]])
            interior_cells[key] = own
            for i in range(own["w"] * own["h"]):
                if own["land"][i] != EMPTY_CELL:
                    want_land(m["area"], own["land"][i])

    # ----------------------------------------------------------- pass 2: areas
    #
    # One AREA RECORD per SOURCE AREA, not per map. An area bank in HeartGold is
    # a stretch of the world and not a map, New Bark, Cherrygrove and the four
    # roads between them are all area 2, and the engine loads the area of the
    # header the player is standing on while drawing the props of every cell
    # around them. Give each map an area of its own and a building one tile over
    # the border stops being loaded; carry the source's own grouping and the
    # props behave exactly as they do in the game they came from.
    prop_map: dict[tuple[str, int], int] = {}
    area_dst: dict[int, dict] = {}
    light_map: dict[int, int] = {}
    for area in sorted(area_land):
        rec = area_src[area]
        src_propset, src_texset, _dummy, _light = struct.unpack("<4H", rec)
        preload = build_src[src_propset]
        n_listed = struct.unpack_from("<H", preload, 0)[0]
        listed = list(struct.unpack_from("<%dH" % n_listed, preload, 2))
        models = list(listed)
        placed = []
        for cid in area_land[area]:
            for mid in prop_model_ids(land_src[cid]):
                placed.append(mid)
                if mid not in models:
                    models.append(mid)
        texset = proptex_src[src_propset]
        which, what = pick_props(placed, listed, texset, prop_archives,
                                 "area %d" % area)
        prop_src = prop_archives[which]
        for mid in models:
            if mid >= len(prop_src):
                die("area %d's preload list names model %d and %s has %d"
                    % (area, mid, which, len(prop_src)))
            if (which, mid) in prop_map:
                continue
            dst = cur.take(DST_PROPMODEL)
            if dst >= MAX_MAP_PROP_MODEL_FILES:
                die("this package needs a prop model at %d and the engine's "
                    "loaded-model table is %d entries, it would be written "
                    "past" % (dst, MAX_MAP_PROP_MODEL_FILES))
            prop_map[(which, mid)] = dst
            write_member(pkg, DST_PROPMODEL, dst, prop_src[mid], cooked=True)
        here = {mid: prop_map[(which, mid)] for mid in models}

        pl_build = cur.take(DST_AREABUILD)
        pl_ptex = cur.take(DST_PROPTEX)
        pl_tex = cur.take(DST_TEXSET)
        pl_area = cur.take(DST_AREA)
        write_member(pkg, DST_AREABUILD, pl_build,
                     struct.pack("<H", len(models))
                     + struct.pack("<%dH" % len(models),
                                   *(here[x] for x in models)), cooked=True)
        # THE PROP texture SET is built, NOT copied. This game binds one set
        # over every prop an area loads and a material whose name is not in it
        # draws with whatever is at texture address 0; HeartGold's models each
        # carry a TEX0 that covers themselves and its shared sets are an
        # override. Goldenrod's happened to cover its buildings. Its Pokemon
        # Center's covers one of the 22 names its furniture asks for, and no
        # member of that archive covers them, so the room came out as white
        # boxes. tools/nsbtx.py takes the union: the source set, then each
        # carried model's own.
        # An area that carries no prop at all has nothing to merge, and its
        # source set is not an NSBTX either, HeartGold writes an empty member
        # for the caves and the sea, and the engine only reads this one when
        # the preload list is non-empty (`AreaDataManager_Load` leaves
        # mapPropTexture NULL otherwise). So that member is carried as it is
        # rather than built out of nothing.
        write_member(pkg, DST_PROPTEX, pl_ptex,
                     nsbtx.merge([texset] + [prop_src[x] for x in models])
                     if models else texset,
                     cooked=True)
        write_member(pkg, DST_TEXSET, pl_tex, texset_src[src_texset],
                     cooked=True)
        # An area is lit by ITS OWN game's schedule: the record's fourth
        # u16 keeps the light pick in its high byte, the source's table
        # turns that into one of five ASCII files, and the file rides as
        # an appended member of this game's lighting archive, whose
        # parser reads the same text. Substituting this game's outdoor
        # file was measured wrong (pastel-teal land after dark).
        pick = SRC_LIGHT_PICK.get(rec[7], 0)
        if pick not in light_map:
            light_map[pick] = cur.take(DST_LIGHT)
            write_member(pkg, DST_LIGHT, light_map[pick],
                         rom_file(rom, SRC_LIGHT_TABLE[pick]), cooked=True)
        write_member(pkg, DST_AREA, pl_area,
                     struct.pack("<4H", pl_build, pl_tex, PL_AREA_DUMMY,
                                 light_map[pick]),
                     cooked=True)
        area_dst[area] = dict(area=pl_area, build=pl_build, ptex=pl_ptex,
                              tex=pl_tex, here=here, which=which, what=what,
                              models=len(models))
        print("portmap: area %d -> record %d, %d prop models from %s (%s), "
              "preload list %d, prop textures %d, texture set %d"
              % (area, pl_area, len(models), which, what, pl_build, pl_ptex,
                 pl_tex))

    # ------------------------------------------------------ pass 3: land data
    stats: dict[str, int] = {}
    land_dst: dict[tuple[int, int], int] = {}
    for area in sorted(area_land):
        here = area_dst[area]["here"]
        for cid in area_land[area]:
            dst = cur.take(DST_LAND)
            land_dst[(area, cid)] = dst
            write_member(pkg, DST_LAND, dst,
                         convert_land(land_src[cid], tmap, stats, here),
                         cooked=True)
    order = sorted(((k, v) for k, v in stats.items()
                    if k not in ("rewritten", "props")), key=lambda kv: -kv[1])
    print("portmap: %d land data members, %d tiles translated, %s"
          % (len(land_dst), sum(v for _, v in order),
             ", ".join("%s %d" % kv for kv in order)))

    # ------------------------------------------------------- pass 4: matrices
    region_matrix: dict[str, int] = {}
    for region, (x0, y0, w, h) in box.items():
        dst = cur.take(DST_MATRIX)
        region_matrix[region] = dst
        land_ids, alts, hdrs = [], [], []
        for cid, hid, alt in region_cells[region]:
            if cid == EMPTY_CELL:
                land_ids.append(EMPTY_CELL)
                alts.append(0)
                hdrs.append(0)
                continue
            land_ids.append(land_dst[(area_of[hid], cid)])
            alts.append(alt)
            hdrs.append(warp_dest[hid])
        write_member(pkg, DST_MATRIX, dst,
                     build_matrix(w, h, alts, land_ids, region, hdrs),
                     cooked=True)
        print("portmap: %s -> one %dx%d matrix at %d, cut from cells "
              "(%d,%d)..(%d,%d) of the shared %dx%d"
              % (region, w, h, dst, x0, y0, x0 + w - 1, y0 + h - 1,
                 overworld["w"], overworld["h"]))
    matrix_dst: dict[tuple[int, int], int] = {}
    for key, own in interior_cells.items():
        _src_matrix, area = key
        dst = cur.take(DST_MATRIX)
        matrix_dst[key] = dst
        ids = [EMPTY_CELL if c == EMPTY_CELL else land_dst[(area, c)]
               for c in own["land"]]
        write_member(pkg, DST_MATRIX, dst,
                     build_matrix(own["w"], own["h"], own["alt"], ids,
                                  own["name"][:8] or "map", None),
                     cooked=True)

    # ------------------------------------------------------- pass 5: trainers
    #
    # Before the people, because what an object event's script id says depends
    # on whether the trainer behind it crossed.
    #
    # Which trainers. Only the ones this run's maps actually place. HeartGold
    # has 738 and Johto and Kanto stand 390 of them on the ground; carrying the
    # rest would be 348 trainers, 348 names and their lines appended for nobody
    # to fight, and every one of them costs a defeated flag out of the block
    # mods/openmmo/patches raises the ceiling of.
    trainer_ids: dict[int, int] = {}
    trainer_region: dict[int, str] = {}
    trainer_source_class: dict[int, int] = {}
    classes: dict = {}
    class_rows: list = []
    if opts["trainers"]:
        if dest_rom is None:
            die("--trainers needs --dest-rom: a trainer is appended to this "
                "game's own trainer archives and its lines to this game's own "
                "message banks, and neither is in the cartridge")
        classes = porttrainers.load_classes(MMO / "TRAINER_CLASS")
        want: list[int] = []
        seen_ev: set[int] = set()
        for m in opts["maps"]:
            if m["events_src"] in seen_ev:
                continue
            seen_ev.add(m["events_src"])
            for rec in parse_events(event_src[m["events_src"]])["obj"]:
                sid, = struct.unpack_from("<H", rec, 10)
                t = porttrainers.trainer_of_script(sid)
                if t is None:
                    continue
                # Which region they stand in, for the one thing that asks:
                # the track their eyes meeting yours plays.
                trainer_region.setdefault(t, m["region_of"])
                if t not in want:
                    want.append(t)
        # And THE trainers A SCRIPT NAMES. A gym leader, an Elite Four
        # member, the rival, a Rocket executive: none of them stands on the
        # ground as std_trainer, their fight is a TrainerBattle line in the
        # map's own scripts, and a fight the fold lowers (portscript,
        # _lower_battle) needs its party carried like anyone else's.
        named = 0
        if opts["scripts"]:
            for m in opts["maps"]:
                row = maps[m["name"]]
                if not (0 <= row["script"] < len(script_src)):
                    continue
                for t in sorted(portscript.named_trainers(
                        script_src[row["script"]], cmds)):
                    trainer_region.setdefault(t, m["region_of"])
                    if t not in want:
                        want.append(t)
                        named += 1
        if named:
            print("portmap: %d trainer(s) fought by name in a script, carried "
                  "beside the ones who stand on the ground" % named)
        want.sort()
        carried = porttrainers.carry(
            lambda path: rom_members(rom, path),
            lambda path: rom_members(dest_rom, path),
            want, classes, decode_bank)
        trainer_ids = carried.ids
        trainer_source_class = carried.source_class
        class_rows = carried.classes
        for narc, index, blob in carried.members:
            write_member(pkg, narc, index, blob, cooked=True)
        for bank, msgs in carried.banks.items():
            write_member(pkg, DST_MSG, bank,
                         encode_bank(msgs, carried.bank_seed[bank]),
                         cooked=True)
        paired = carried.stats.get("class paired", 0)
        print("portmap: %d trainer(s) carried as %d..%d, %d with their own "
              "class, %d with a stand-in, %d line(s) of what they say"
              % (len(trainer_ids), min(trainer_ids.values()),
                 max(trainer_ids.values()),
                 paired, carried.stats.get("class fallback", 0),
                 carried.stats.get("messages", 0)))

    # --------------------------------------------------------- pass 6: people
    # sprite id -> (gfx id, member, textures on the sheet, 64x64?)
    gfx_map: dict[int, tuple[int, int, int, bool, bool]] = {}
    artless: set[int] = set()
    mismatched: dict[int, tuple[int, str]] = {}

    def gfx_of(sprite: int) -> int | None:
        """The appended OBJ_EVENT_GFX id for a source sprite, carrying its art."""
        if sprite in NATIVE_GFX:
            return NATIVE_GFX[sprite]
        if sprite in gfx_map:
            return gfx_map[sprite][0]
        if sprite in artless or sprite in mismatched:
            return None
        if sprite not in sprites:
            die("sprite %d has no row in mmo/SPRITES, the map places "
                "somebody this port cannot even name" % sprite)
        member, name = sprites[sprite]
        if member is None:
            artless.add(sprite)
            return None
        if member >= len(mmodel_src):
            die("mmo/SPRITES sends sprite %d to mmodel member %d and the "
                "cartridge has %d" % (sprite, member, len(mmodel_src)))
        blob = mmodel_src[member]
        pal = nsbtx_palette_name(blob)
        if pal is None:
            die("mmodel member %d (sprite %d, %s) is not an NSBTX with a named "
                "palette" % (member, sprite, name))
        if not palette_agrees(name, pal):
            mismatched[sprite] = (member, pal)
            return None
        # The client dresses a carried person over this game's generic
        # billboard, which comes in 32x32 and 64x64 and nothing else; a
        # sheet of any other size reads past itself and draws as noise
        # twice life-size (the item ball, 16x16, measured 2026-08-31).
        # Until a small frame has a model to wear, its wearer stays home.
        sizes = {(8 << ((p >> 20) & 7), 8 << ((p >> 23) & 7))
                 for p, _e, _d in nsbtx.read(blob)["textures"].values()}
        if not sizes <= {(32, 32), (64, 64)}:
            print("portmap: sprite %d (%s) is drawn at %s and the billboard "
                  "only wears 32x32 or 64x64, nobody carries it"
                  % (sprite, name, ", ".join("%dx%d" % wh
                                             for wh in sorted(sizes))))
            artless.add(sprite)
            return None
        gid = FIRST_COOKED_GFX + len(gfx_map)
        dst = cur.take(DST_MMODEL)
        write_member(pkg, DST_MMODEL, dst, blob, cooked=True)
        follower = name.startswith("FOLLOWER_MON_")
        ntex = len(nsbtx.read(blob)["textures"])
        if follower and ntex != FOLLOWER_TEXTURES:
            die("sprite %d (%s) is a follower sheet of %d textures, not the "
                "%d a follower walk reads" % (sprite, name, ntex,
                                              FOLLOWER_TEXTURES))
        gfx_map[sprite] = (gid, dst, ntex, (64, 64) in sizes, follower)
        return gid

    totals: dict[str, int] = {}
    # Every static site of the run, in cook order: its band flag is its index.
    static_sites: list[dict] = []
    fold_why: dict[str, int] = {}

    # A person the destination game also employs wears its own uniform: the
    # nurse's sprite maps to this game's POKECENTER_NURSE instead of a cooked
    # billboard, so she faces the way her data says, and bows with the anim
    # groups only a native sprite has. One row per verified look-alike.
    NATIVE_GFX = {
        335: 26,   # SPRITE_PCWOMAN1 -> OBJ_EVENT_GFX_POKECENTER_NURSE
        # The boulder and the smashable rock. Both games inherited these
        # slots from Diamond and kept the numbers, HeartGold's SPRITE_ROCK
        # and SPRITE_BREAKROCK are 84 and 85, this game's STRENGTH_BOULDER
        # and ROCK_SMASH are 84 and 85, and both place them for the same
        # two routines (std_field_strength, std_field_rock_smash; front
        # 2's script_of). Native rather than cooked because a boulder is
        # Pushed by engine code that asks for its own gfx id, a rock is
        # smashed by an effect that asks for its own, and both are 16x16
        # sheets the cooked billboard cannot wear. The item ball is the
        # third of that Diamond inheritance, 87 in both, and it is native
        # for the same reason: `RemoveObject` deletes it by its own gfx and
        # the sheet is 16x16.
        84: 84,
        85: 85,
        ITEM_BALL_SPRITE: ITEM_BALL_SPRITE,
    }

    # A carried prop that is one of this game's own, by the name inside its
    # NSBMD: the engine searches its prop tables by its model ids (the
    # healing machine the nurse loads, one day the doors), and the client's
    # equivalence hook answers an appended id with the id it stands for
    # (`.cooked/generated/prop_equiv.txt`; openmmo_propequiv.c).
    PROP_EQUIV = {
        b"machine_pc01": 123,   # a center's healing machine -> pokecenter_healing_machine_nsbmd
        b"kaifuku_lab01": 123,  # the lab variant of the same apparatus
    }

    # THE common bank crosses as a bank. The nurse, the mart greeter and the
    # rest of the routines every town borrows live in one member the source
    # game reaches by CallStd; inlining them here would drag another bank's
    # message ids into maps that cannot read them, so the member is folded
    # Whole, its text bank is carried beside it, and every CallStd in a map's
    # own scripts is rebased to where the client's loader finds the pair
    # (`.cooked/generated/std_banks.txt`; portscript.STD_PORT_BASE).
    std_span = 0
    std_open: set = set()
    items = None
    window_first = None
    # Where A WARP lands. A script's Warp (and the train ride, ScrCmd_722)
    # names the source's header and a tile of that map's matrix: an
    # interior's own, the overworld's shared one. The fold rewrites the
    # header to the one this package gave it and the tile into the region's
    # cut-out, which is what convert_events does to every door below.
    warp_maps: dict[int, tuple[int, int, int]] = {}
    for m in opts["maps"]:
        if m["matrix"] == 0:
            bx, by, _w, _h = box[m["region_of"]]
            warp_maps[m["header_src"]] = (m["header"], bx * MAP_TILES,
                                          by * MAP_TILES)
        else:
            warp_maps[m["header_src"]] = (m["header"], 0, 0)
    print("portmap: %d warp destination(s) a script may name" % len(warp_maps))
    if opts["scripts"] and len(script_src) > STD_MISC_MEMBER:
        std_member = script_src[STD_MISC_MEMBER]
        std_span = len(portscript.entry_offsets(std_member)[0])
        blob, kept, std_open = portscript.convert(std_member, cmds, fold_why,
                                                  std_span, common=True, story=story,
                                                  maps=warp_maps)
        std_scr = cur.take(DST_SCRIPT)
        write_member(pkg, DST_SCRIPT, std_scr, blob, cooked=True)
        if STD_MISC_MSG >= len(msg_src):
            die("the cartridge has no msg member %d for the common script "
                "bank" % STD_MISC_MSG)
        std_msg = cur.take(DST_MSG)
        write_member(pkg, DST_MSG, std_msg, msg_src[STD_MISC_MSG], cooked=True)
        bank_rows = [(portscript.STD_PORT_BASE, std_scr, std_msg, std_span)]
        print("portmap: common script bank: %d of %d entries fold -> script "
              "member %d, text member %d"
              % (sum(kept), len(kept), std_scr, std_msg))
        # The items on the ground, behind this game's own two routines.
        if dest_rom is not None and len(script_src) > HG_ITEM_BALL_MEMBER:
            items = item_banks(dest_rom, _engine_pc().parent, cmds,
                               script_src[HG_ITEM_BALL_MEMBER],
                               load_hidden_items())
            ball_scr = cur.take(DST_SCRIPT)
            write_member(pkg, DST_SCRIPT, ball_scr, items["ball_blob"],
                         cooked=True)
            hid_scr = cur.take(DST_SCRIPT)
            write_member(pkg, DST_SCRIPT, hid_scr, items["hidden_blob"],
                         cooked=True)
            bank_rows.append((items["ball_base"], ball_scr, items["ball_text"],
                              HG_ITEM_BANK_COUNT))
            bank_rows.append((items["hidden_base"], hid_scr,
                              items["hidden_text"], HG_ITEM_BANK_COUNT))
            print("portmap: item banks: %d ball stubs and %d hidden-item stubs "
                  "in front of this game's own routines (members %d and %d, "
                  "tails at %d and %d) -> script members %d and %d, ids %d "
                  "and %d up, flags %d..%d"
                  % (items["balls"], len(items["hidden"]), items["members"][0],
                     items["members"][1], items["tails"][0], items["tails"][1],
                     ball_scr, hid_scr, items["ball_base"], items["hidden_base"],
                     PORTED_ITEM_FLAGS_START,
                     PORTED_ITEM_FLAGS_START + PORTED_ITEM_FLAGS_MAX - 1))
        # THE APRICORN trees' conversation, against HeartGold's own lines.
        if items is not None and HG_APRICORN_MSG < len(msg_src):
            apr_scr = cur.take(DST_SCRIPT)
            write_member(pkg, DST_SCRIPT, apr_scr, apricorn_bank(cmds), cooked=True)
            apr_msg = cur.take(DST_MSG)
            write_member(pkg, DST_MSG, apr_msg, msg_src[HG_APRICORN_MSG], cooked=True)
            bank_rows.append((APRICORN_BASE, apr_scr, apr_msg,
                              APRICORN_ENTRY_PICKED + APRICORN_KINDS))
            items["apricorn"] = True
            print("portmap: apricorn bank: %d entries against HeartGold's msg_%04d "
                  "-> script member %d, text member %d, ids %d up"
                  % (APRICORN_ENTRY_PICKED + APRICORN_KINDS, HG_APRICORN_MSG,
                     apr_scr, apr_msg, APRICORN_BASE))
        # THE FIELD MOVES: the tree's press and the rubble's item, against
        # HeartGold's own field-move lines, and the shake's own model.
        if HG_FIELDMOVE_MSG < len(msg_src):
            pl_cmds = gen_scripts.pl_commands(_engine_pc().parent)
            fm_scr = cur.take(DST_SCRIPT)
            write_member(pkg, DST_SCRIPT, fm_scr, fieldmove_bank(cmds, pl_cmds),
                         cooked=True)
            fm_msg = cur.take(DST_MSG)
            write_member(pkg, DST_MSG, fm_msg, msg_src[HG_FIELDMOVE_MSG], cooked=True)
            bank_rows.append((FIELDMOVE_BASE, fm_scr, fm_msg, FIELDMOVE_ENTRIES))
            print("portmap: field-move bank: %d entries against HeartGold's msg_%04d "
                  "-> script member %d, text member %d, ids %d up"
                  % (FIELDMOVE_ENTRIES, HG_FIELDMOVE_MSG, fm_scr, fm_msg,
                     FIELDMOVE_BASE))
            effects = rom_members(rom, SRC_EFFECT)
            if dest_rom is not None:
                have = len(rom_members(dest_rom, DST_EFFECT))
                if have != PL_COUNT[DST_EFFECT]:
                    die("this game's %s holds %d members and PL_COUNT says %d"
                        % (DST_EFFECT, have, PL_COUNT[DST_EFFECT]))
            if max(HG_HEADBUTT_ANIMS + (HG_HEADBUTT_MODEL,)) >= len(effects):
                die("the cartridge's %s has %d members; the headbutt shake wants "
                    "%d" % (SRC_EFFECT, len(effects), HG_HEADBUTT_MODEL))
            if effects[HG_HEADBUTT_MODEL][:4] != b"BMD0":
                die("%s member %d is not a model (%r)"
                    % (SRC_EFFECT, HG_HEADBUTT_MODEL, effects[HG_HEADBUTT_MODEL][:4]))
            fx_model = cur.take(DST_EFFECT)
            write_member(pkg, DST_EFFECT, fx_model, effects[HG_HEADBUTT_MODEL],
                         cooked=True)
            fx_anim = cur.take(DST_EFFECT, len(HG_HEADBUTT_ANIMS))
            for i, member in enumerate(HG_HEADBUTT_ANIMS):
                write_member(pkg, DST_EFFECT, fx_anim + i, effects[member], cooked=True)
            rows = ["headbutt_model %d\n" % fx_model,
                    "headbutt_anim %d %d\n" % (fx_anim, len(HG_HEADBUTT_ANIMS))]
            # The all-trees land member HeartGold headbutts anywhere on, by
            # every copy this package carries of it (one per area that lays
            # it).
            for (_area, cid), dst in sorted(land_dst.items()):
                if cid == HG_HEADBUTT_LAND:
                    rows.append("headbutt_land %d\n" % dst)
            write_generated(pkg, "field_effects.txt", "".join(rows))
            print("portmap: the headbutt shake: model member %d, animations %d..%d, "
                  "%d all-trees land member(s)"
                  % (fx_model, fx_anim, fx_anim + len(HG_HEADBUTT_ANIMS) - 1,
                     len(rows) - 2))
        # THE BADGES ON THE CARD: HeartGold's own page, for the two ported regions.
        if dest_rom is not None:
            hg_card = rom_members(rom, SRC_CARD)
            pl_card = rom_members(dest_rom, DST_CARD)
            if len(pl_card) != PL_COUNT[DST_CARD]:
                die("this game's %s holds %d members and PL_COUNT says %d"
                    % (DST_CARD, len(pl_card), PL_COUNT[DST_CARD]))
            rows = []
            for key, member in card_page(hg_card):
                m = cur.take(DST_CARD)
                write_member(pkg, DST_CARD, m, member, cooked=True)
                rows.append("%s %d\n" % (key, m))
            write_generated(pkg, "card_badges.txt", "".join(rows))
            print("portmap: the card's LEAGUE BADGES page: %d members appended to %s"
                  % (len(rows), DST_CARD))
            # THE WATER MOVES, and THE flowers: the source's animated textures
            # after this game's, and its signs after this game's.
            names = port_texture_animation(rom, dest_rom, pkg, cur)
            print("portmap: %d animated texture(s) carried after this game's "
                  "%d: %s" % (len(names), PL_COUNT[DST_TANIME] - 1,
                              ", ".join(names)))
            window_first = port_area_windows(rom, dest_rom, pkg, cur)
            print("portmap: %d place-name sign(s) appended to %s; a ported "
                  "header names its own from window %d"
                  % (AREAWIN_MEMBERS // 2, DST_AREAWIN, window_first))
        write_generated(pkg, "std_banks.txt",
                        "".join("%d %d %d %d\n" % row for row in bank_rows))
        # THE ITEMS past THIS game'S TABLE. HeartGold's Apricorn Box, its
        # Apricorns and Kurt's balls are ids this engine clamps to nothing;
        # tools/portitems.py appends their rows and icons and grows the four
        # item text banks, under an oracle over every item both games ship.
        if dest_rom is not None:
            import portitems
            engine_root = _engine_pc().parent
            codes = {v: k for k, v in charmap(engine_root).items()}
            try:
                filled = portitems.fill(
                    lambda path: rom_members(rom, path),
                    lambda path: rom_members(dest_rom, path),
                    Path(subprocess.run([str(MMO / "tools" / "decomp_dir.sh"),
                                         "pokeheartgold"], capture_output=True,
                                        text=True).stdout.strip()),
                    engine_root, codes,
                    lambda: cur.take(DST_ITEM_DATA), lambda: cur.take(DST_ITEM_ICON),
                    lambda narc, index, blob: write_member(pkg, narc, index, blob,
                                                           cooked=True),
                    lambda name, text: write_generated(pkg, name, text),
                    MMO)
            except portitems.Refused as e:
                die("the item fill refused: %s" % e)
            print("portmap: item fill: %d items %d..%d appended (rows, icons, four "
                  "text banks); %d shared rows differ, %d shared icons differ; "
                  "banks %s"
                  % (filled["rows"], filled["first"], filled["last"],
                     len(filled["row_diffs"]), filled["icon_diffs"],
                     ", ".join("%d<-%d" % (pl, hg)
                               for pl, hg in sorted(filled["banks"].items()))))
            if filled["row_diffs"]:
                print("portmap:   rows that differ: %s" % filled["row_diffs"])

    for m in opts["maps"]:
        # The scripts first: what an object event's script id may still say
        # depends on which of its map's entries folded.
        m["script_blob"] = None
        m["kept"] = None
        m["marts"] = {}
        m["clock"] = {}
        m["arrival"] = None
        if opts["scripts"]:
            row = maps[m["name"]]
            if row["script"] >= 0 and row["script"] < len(script_src):
                found: dict = {}
                found_statics: dict = {}
                clock: dict = {}
                # The map's own arrival table names the script that runs on
                # entry; the fold says whether what is left of it may.
                arrival = None
                if 0 <= row["hdr"] < len(script_src):
                    eid = arrival_entry(script_src[row["hdr"]])
                    if eid is not None:
                        arrival = {"entry": eid}
                blob, kept, _ = portscript.convert(
                    script_src[row["script"]], cmds, fold_why, std_span,
                    open_calls=std_open, marts=found,
                    trainers=trainer_ids if opts["trainers"] else None,
                    clock=clock, arrival=arrival, story=story,
                    maps=warp_maps, here=m["header_src"],
                    statics=found_statics)
                m["script_blob"] = blob
                m["kept"] = kept
                m["clock"] = clock
                # PORTMAP_FOLD_LOG=<path>: one line per entry, kept or the
                # reason it was not, the audit a summary count cannot be.
                if os.environ.get("PORTMAP_FOLD_LOG"):
                    starts, _e = portscript.entry_offsets(script_src[row["script"]])
                    with open(os.environ["PORTMAP_FOLD_LOG"], "a") as f:
                        for ei, at in enumerate(starts):
                            why = portscript.Fold(
                                script_src[row["script"]], cmds, std_span,
                                trainer_ids if opts["trainers"] else None,
                                clock, story).run(at)[1]
                            f.write("%s %d %s %s\n" % (
                                m["name"], ei, "kept" if kept[ei] else "end",
                                (why or "").split(", ")[0]))
                if arrival is not None and arrival.get("crosses"):
                    m["arrival"] = arrival["entry"]
                    totals["arrivals"] = totals.get("arrivals", 0) + 1
                    totals["clock flags"] = totals.get("clock flags", 0) \
                        + len(clock)
                m["marts"] = {e + 1: v for e, v in found.items()
                              if e < len(kept) and kept[e]}
                m["statics"] = {e + 1: v for e, v in found_statics.items()
                                if e < len(kept) and kept[e]}
                totals["entries"] = totals.get("entries", 0) + len(kept)
                totals["folded"] = totals.get("folded", 0) + sum(kept)
        if m["matrix"] == 0:
            bx, by, _w, _h = box[m["region_of"]]
            ox, oy = bx * MAP_TILES, by * MAP_TILES
        else:
            ox, oy = 0, 0
        hidden = scenes.get(m["name"])
        estats: dict[str, int] = {}
        sites_before = len(static_sites)
        events, used_gfx = convert_events(
            event_src[m["events_src"]], ox, oy, hidden, gfx_of, warp_dest,
            estats, m["kept"], trainer_ids if opts["trainers"] else None,
            walls.get(m["name"]), m["clock"], items,
            m.get("statics"), static_sites)
        for site in static_sites[sites_before:]:
            site["header"] = m["header"]
            site["name"] = m["name"]
        m["events_blob"] = events
        m["used_gfx"] = used_gfx
        m["estats"] = estats
        m["hidden_unread"] = hidden is None
        for k, v in estats.items():
            totals[k] = totals.get(k, 0) + v
    if totals.get("arrivals"):
        print("portmap: %d arrival script(s) cross, %d clock flag(s) on this "
              "game's map-local slots, %d person(s) created by the hour"
              % (totals["arrivals"], totals["clock flags"],
                 totals.get("kept_clock", 0)))
    if static_sites:
        # The fights the source stages by script, one a
        # line, the ported header, the object (its index in the map's
        # events), the species and level the fold read, the band flag the
        # object hides behind once it is won, the source's own flag and the
        # map's name. The server reads the same sites out of the decomp
        # (PortedTerrainParser) and the two are checked against each other
        # by mmo/tests/mapformat_test.sh.
        rows = ["# header oid species level flag src_flag name\n"]
        for site in static_sites:
            rows.append("%d %d %d %d %d %d %s\n" % (
                site["header"], site["oid"], site["species"], site["level"],
                site["flag"], site["src_flag"], site["name"]))
        write_generated(pkg, "static_sites.txt", "".join(rows))
        # And the same rows as a tracked table, mmo/STATIC_SITES, which is
        # what the server's codegen marks its people from: the fold is the
        # one oracle for which scenes reach their fight, and it needs the
        # cartridge, so the cook writes the answer down where a build
        # without one can read it. mapformat_test.sh holds the two copies
        # together.
        (MMO / "STATIC_SITES").write_text(
            "# STATIC_SITES, GENERATED by tools/portmap.py from the "
            "cartridge; DO NOT EDIT.\n#\n"
            "# The HeartGold people whose script stages a wild fight, as the\n"
            "# fold reads them: a press on one is the server's fight to deal\n"
            "# (StaticEncounterService), the lead-up is the client's folded\n"
            "# scene, and the site hides for good once it is won. Columns:\n"
            "# the ported header, the object's index in its map's events, the\n"
            "# species and level the script names, the client's band flag,\n"
            "# the source's own flag and the map. Regenerate with a cook\n"
            "# (`make -C mmo import IMPORT_ROM=<heartgold.nds> IMPORT_PKG=mods/hgss`).\n"
            + "".join(rows[1:]))
        print("portmap: %d static site(s), a scripted wild fight the server "
              "deals on a press, on flags %d..%d; mmo/STATIC_SITES written"
              % (len(static_sites), PORTED_STATIC_FLAGS_START,
                 PORTED_STATIC_FLAGS_START + len(static_sites) - 1))

    if prop_map:
        rows = []
        roles = []
        # The parts the ported nurse scene plays with, by the names inside
        # their NSBMD: the tray the balls sit on, the ball itself, and the
        # machine piece whose lights the scene switches on
        # (openmmo_healanim.c, derived from the source game's own routine).
        HEAL_ROLES = {b"machine_pc02": "tray", b"pc_mb": "ball",
                      b"machine_pc03": "machine"}
        anime_rows = {which: rom_members(rom, path)
                      for which, path in SRC_ANIMELIST.items()}

        # Which DOOR sounds automatic. The source has no flag for it either;
        # what its automatic doors share is the animation pair the Pokemon
        # Center's own door opens with, so that pair is read off `door_pc01`
        # and every door that opens the same way is called sliding. With no
        # `door_pc01` in the run nothing is, and they all swing.
        slide_anim = None
        for (which, mid), dst in prop_map.items():
            if nsbmd_model_name(prop_archives[which][mid]) == "door_pc01":
                slide_anim = struct.unpack_from("<i", anime_rows[which][mid], 8)[0]

        def pair_of(row: bytes) -> int:
            """How many animations a carried row names."""
            return sum(1 for a in struct.unpack_from("<4i", row, 8)[:4] if a >= 0)

        doors = stairs = pcs = 0
        for (which, mid), dst in sorted(prop_map.items(), key=lambda kv: kv[1]):
            head = prop_archives[which][mid][:0x100]
            for name, pt_id in PROP_EQUIV.items():
                if name in head:
                    rows.append("%d %d %s\n" % (dst, pt_id, name.decode()))
            for name, role in HEAL_ROLES.items():
                if name in head:
                    roles.append("%s %d\n" % (role, dst))

            model = nsbmd_model_name(prop_archives[which][mid])
            if model is None:
                continue
            if model.startswith("stair_pc_") and model[9:] in PT_STAIRS:
                rows.append("%d %d %s\n" % (dst, PT_STAIRS[model[9:]], model))
                stairs += 1
            elif DOOR_NAME.search(model) or model in DOOR_ALSO:
                first = struct.unpack_from("<i", anime_rows[which][mid], 8)[0]
                sliding = slide_anim is not None and first == slide_anim
                rows.append("%d %d %s\n" % (
                    dst, PT_DOOR_SLIDING if sliding else PT_DOOR_HINGED,
                    model))
                doors += 1
            elif PC_NAME.match(model) and pair_of(anime_rows[which][mid]) == 2:
                # A storage terminal boots up and shuts down, which is the
                # two-animation pair this game's own PC carries; the ones
                # named pc02..pc04 are furniture and carry none.
                rows.append("%d %d %s\n" % (dst, PT_PC, model))
                pcs += 1
        if rows:
            write_generated(pkg, "prop_equiv.txt", "".join(rows))
            print("portmap: %d carried prop(s) stand for this game's own "
                  "(%d door(s), %d escalator(s), %d pc(s))"
                  % (len(rows), doors, stairs, pcs))
        if roles:
            write_generated(pkg, "heal_props.txt", "".join(sorted(roles)))
            print("portmap: the healing scene knows its %d part(s)"
                  % len(roles))

        # What THE furniture does crosses with THE furniture. The engine
        # answers "does model N animate" from one 20-byte row per model in
        # `bm_anime_list`, and plays the archives the row names out of
        # `bm_anime`, the machine's lights, a door's swing, a screen's
        # frames. Both are indexed flat, the row archive by MODEL id, so
        # every appended model gets a row at its own id (empty when its
        # source row is empty; pc_modfs refuses a hole) and each archive a
        # row names is appended once and renumbered. The source's extra
        # word (unk4) is dropped: neither engine's manager reads it.
        #
        # A ROW crosses with its own flags. This game ambient-loads every
        # non-deferred row of an AREA's whole build list into the animation
        # manager's slots at area entry; the source's areas span towns and
        # roads at once and their lists need up to 31 slots even deduped,
        # against the 16 the engine ships (its own engine survives on 16 by
        # loading per placed prop). Until 2026-09-02 every carried row was
        # forced deferred to stay under that assert (a non-deferred cook on
        # 2026-08-31 wedged the engine in the assert's halt loop on header
        # 604's area, a black screen at login), which is why no mart sign
        # turned and no flower swayed on a ported map. The client now has
        # 64 slots and skips a row past them instead of asserting
        # (patches/include/overlay005/map_prop_animation.h,
        # patches/src/overlay005/map_prop_animation.c), so the source's own
        # deferred bit decides: an ambient row plays on arrival, a one-shot
        # row waits for its scene. The spare byte still carries the source
        # flags for any reader that wants them.
        src_anime = rom_members(rom, SRC_ANIME)
        carried_anim: dict[int, int] = {}
        animated = 0
        animated_ambient = 0
        for (which, mid), dst in sorted(prop_map.items(),
                                        key=lambda kv: kv[1]):
            row = anime_rows[which][mid]
            arch = struct.unpack_from("<4i", row, 8)
            out = []
            for a in arch:
                if a < 0 or a >= len(src_anime):
                    out.append(-1)
                    continue
                if a not in carried_anim:
                    carried_anim[a] = cur.take(DST_ANIME)
                    write_member(pkg, DST_ANIME, carried_anim[a],
                                 src_anime[a], cooked=True)
                out.append(carried_anim[a])
            flags = row[1]
            spare = 0
            ambient = 0
            if out[0] != -1:
                animated += 1
                spare = flags
                ambient = (flags & 1) == 0
            write_member(pkg, DST_ANIMELIST, dst,
                         struct.pack("<4B4i", row[0], flags, row[2], spare,
                                     *out), cooked=True)
            animated_ambient += ambient
        print("portmap: %d of %d carried props animate (%d ambient, the rest "
              "one-shot), %d animation archive(s) carried"
              % (animated, len(prop_map), animated_ambient, len(carried_anim)))

    # -------------------------------------------------------- pass 7: headers
    #
    # A header names four more archives and every one of them is an index, so a
    # map with no scripts and no text still needs a member in each that opens
    # and stops. This is also where a door finally has somewhere to go: every
    # map in the run has its header id by now, so `warp_dest` is complete.
    music: dict[str, int] = {}
    if opts["music"]:
        if dest_rom is None:
            die("--music needs --dest-rom: the archive a track is appended to "
                "is this game's own")
        wanted = {m["src_bgm"] for m in opts["maps"] if m["src_bgm"] != "-"}
        battle_regions = sorted({m["region_of"] for m in opts["maps"]
                                 if m["region_of"] in BATTLE_SEQS})
        for region in battle_regions:
            wanted.update(BATTLE_SEQS[region])
        if trainer_ids:
            wanted.update(encounter_seqs(classes))
        if class_rows:
            wanted.update(class_theme_seqs(class_rows))
        if opts["region"]:
            wanted.update(POKEGEAR_SEQS)
        blob, music = port_music(rom, dest_rom, sorted(wanted))
        dest = pkg / FILE_ROOT[True] / SDAT_PATH
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(blob)
        print("portmap: %d track(s) -> %s (%d bytes); nothing already in it "
              "moved" % (len(music), SDAT_PATH, len(blob)))
        rows = []
        for m in opts["maps"]:
            seqs = BATTLE_SEQS.get(m["region_of"])
            if seqs is None:
                continue
            rows.append("%d %d %d %d\n" % (m["header"], music[seqs[0]],
                                            music[seqs[1]], music[seqs[2]]))
        if class_rows:
            write_generated(pkg, "trainer_classes.txt",
                            trainer_class_rows(class_rows, music))
            print("portmap: %d class(es) appended whole, %d with a theme of "
                  "their own -> .cooked/generated/trainer_classes.txt"
                  % (len(class_rows), len(class_theme_seqs(class_rows))))
        if rows:
            write_generated(pkg, "battle_bgm.txt", "".join(rows))
            print("portmap: battle themes for %d map(s) -> "
                  ".cooked/generated/battle_bgm.txt" % len(rows))

        # What A TRAINER is heard as, which this game also picks off the class
        # byte, so a carried trainer wearing a stand-in class was announced
        # by the stand-in's theme, and in Kanto by a Johto one. The source's
        # own table decides it instead, by the class HeartGold gave them and
        # the region they stand in; the region is asked because exactly one
        # class answers differently in the two (a Scientist is Rocket in Johto
        # and is not in Kanto).
        rows = []
        for src, dst in sorted(trainer_ids.items(), key=lambda kv: kv[1]):
            row = classes.get(trainer_source_class.get(dst, -1))
            if row is None:
                continue
            name = row[4] if trainer_region.get(src) == "kanto" else row[3]
            if name in music:
                rows.append("%d %d\n" % (dst, music[name]))
        if rows:
            write_generated(pkg, "encounter_bgm.txt", "".join(rows))
            print("portmap: %d trainer(s) are heard as their own class -> "
                  ".cooked/generated/encounter_bgm.txt" % len(rows))

    label_bank = None
    label_seed = 0
    label_ids: dict[str, int] = {}
    cmap: dict[str, int] = {}
    rows = []
    for m in opts["maps"]:
        kind = KINDS[MAP_KINDS[m["kind"]]]
        pl_script = cur.take(DST_SCRIPT, 2)
        pl_events = cur.take(DST_EVENTS)
        pl_msg = cur.take(DST_MSG)
        pl_mm = cur.take(DST_MMLIST)
        m["pl_msg"] = pl_msg

        scripts, inits = empty_script_archive(), empty_init_scripts()
        if dest_rom is not None and not rows:
            check_empties(lambda n: rom_members(dest_rom, n), [
                ("the do-nothing script archive", DST_SCRIPT, scripts),
                ("the empty arrival-script table", DST_SCRIPT, inits),
                ("the one-entry arrival table", DST_SCRIPT,
                 init_table_transition(1)),
            ])
        # The ARRIVAL script stays off the doorstep unless the fold proved it
        # harmless. A map's own init table runs on entry and nearly every
        # one of Johto's branches on the story this port does not carry, so
        # running one would put a cutscene on a doorstep in a world where
        # its reason never happened. The one that crosses is the hour test
        # of Routes 34, 35 and 39, flags, the clock and End, nothing else
        # (portscript.ARRIVAL_INERT), and it crosses because the people
        # it hides and shows are created on this game's map-local flags.
        if m["arrival"] is not None:
            inits = init_table_transition(m["arrival"])
        # THE train'S two platforms run their scene on entry (TRIP_VARS).
        frame_rows = []
        if opts["scripts"]:
            hdr = maps[m["name"]]["hdr"]
            if 0 <= hdr < len(script_src):
                frame_rows = trip_frame_rows(script_src[hdr], m["kept"])
        if frame_rows:
            inits = init_table_frame(m["arrival"], frame_rows)
            totals["trip platforms"] = totals.get("trip platforms", 0) + 1
        write_member(pkg, DST_SCRIPT, pl_script,
                     m["script_blob"] if m["script_blob"] else scripts,
                     cooked=True)
        write_member(pkg, DST_SCRIPT, pl_script + 1, inits, cooked=True)
        write_member(pkg, DST_EVENTS, pl_events, m["events_blob"], cooked=True)
        # THE MAP'S own TEXT, carried whole rather than re-encoded. The banks
        # are the same container and the charmaps are identical, so the bytes
        # already say what they say, and a bank a script indexes has to keep
        # every one of its message numbers, so carrying part of one is not an
        # option anyway.
        src_msg = maps[m["name"]]["msg"]
        write_member(pkg, DST_MSG, pl_msg,
                     msg_src[src_msg] if (opts["scripts"] and 0 <= src_msg
                                          < len(msg_src))
                     else empty_message_bank(), cooked=True)

        # The preload list sizes the billboard resource heap (`count + 3` in
        # `ov5_021ECE40`), so a map names its own people rather than borrowing
        # a Sinnoh member sized for a different cast.
        ids = sorted(m["used_gfx"])
        if len(ids) + 1 > MAX_MAP_OBJECTS_TO_PRELOAD:
            die("%s places %d distinct people and the engine preloads at most "
                "%d" % (m["name"], len(ids), MAX_MAP_OBJECTS_TO_PRELOAD - 1))
        write_member(pkg, DST_MMLIST, pl_mm,
                     struct.pack("<%dH" % (len(ids) + 1), *ids,
                                 MAP_OBJECT_PRELOAD_SENTINEL), cooked=True)

        label_id = 0
        if m["label"] is not None:
            if dest_rom is None:
                die("a place name needs --dest-rom: the location-name bank the "
                    "banner reads belongs to THIS game, and the cartridge does "
                    "not carry one this engine reads")
            if not cmap:
                cmap.update(charmap(_engine_root()))
            missing = sorted({c for c in m["label"] if c not in cmap})
            if missing:
                die("the engine's charmap has no code for %s, a label it "
                    "cannot draw is worse than none"
                    % ", ".join(repr(c) for c in missing))
            if label_bank is None:
                raw = rom_members(dest_rom, DST_MSG)[MSG_BANK_LOCATION_NAMES]
                label_bank = decode_bank(raw)
                label_seed = struct.unpack_from("<H", raw, 2)[0]
            # A city and the rooms inside it draw the same banner, HeartGold
            # gives its Pokemon Center the city's own map section, so a name
            # already appended in this run is reused rather than appended
            # twice under two numbers.
            encoded = [cmap[c] for c in m["label"]] + [0xFFFF]
            if m["label"] in label_ids:
                label_id = label_ids[m["label"]]
            else:
                label_id = len(label_bank)
                label_bank.append(encoded)
                label_ids[m["label"]] = label_id

        bgm = m["bgm_id"]
        if bgm is None:
            bgm = music.get(m["src_bgm"], 0)
        matrix = (region_matrix[m["region_of"]] if m["matrix"] == 0
                  else matrix_dst[(m["matrix"], m["area"])])
        # The sign a map is announced on is its own header's, on the source's
        # board (port_area_windows); the header field is one-based with zero
        # for no sign, which is also what the source's areaIcon 0 means, and
        # an icon past the nine boards is a room that is never announced.
        window = kind["window"]
        if window_first is not None:
            icon = m.get("icon", -1)
            window = (window_first + icon + 1
                      if 1 <= icon < AREAWIN_MEMBERS // 2 else 0)
        # The camera a map is looked at through is its own header's: the
        # source's seventeen templates ride after this game's seventeen
        # (mods/openmmo/patches/src/overlay005/field_camera.c.patch, from
        # mmo/CAMERAS), so a gym HeartGold viewed in perspective from a low
        # angle is not flattened into this game's orthographic room camera.
        camera = kind["camera"]
        if 0 <= m.get("cam", -1) < PL_CAMERA_TYPES:
            camera = PL_CAMERA_TYPES + m["cam"]
        rows.append([m["header"], area_dst[m["area"]]["area"], pl_mm,
                     matrix, pl_script, pl_script + 1, pl_msg,
                     bgm, bgm, ENCOUNTERS_NONE, pl_events, label_id,
                     window, OVERWORLD_WEATHER_CLEAR, camera,
                     kind["map_type"], m["battle_bg"], kind["bike"],
                     kind["run"], kind["escape"], kind["fly"]])
        est = m["estats"]
        print("portmap: %-40s hdr %4d (bank %d map %3d)  matrix %3d area %3d  "
              "%2d people %2d signs %2d doors (%d parked)"
              % (m["name"], m["header"], m["header"] >> 8, m["header"] & 0xFF,
                 matrix, area_dst[m["area"]]["area"], est.get("people", 0),
                 est.get("signs", 0), est.get("warps", 0),
                 est.get("warps_parked", 0)))

    if label_bank is not None:
        write_member(pkg, DST_MSG, MSG_BANK_LOCATION_NAMES,
                     encode_bank(label_bank, label_seed), cooked=True)
        print("portmap: %d place name(s) appended to the location-name bank"
              % len(label_ids))

    # THE material-shape TABLE, grown. A prop in the area's preload list is
    # loaded and textured by that list; what an appended id still needs is a row
    # in `build_model_matshp.dat`, whose locator table is one per model this game
    # shipped, because `MapProp_GetMaterialShapeIDsCount` indexes it by the
    # absolute model id and an id past 589 reads off the end and hangs the field
    # on the first draw.
    #
    # `.cooked/generated/extra_props.txt` was how that was answered, and it is
    # the wrong list for a port. `MapProp_IsCookedExtra` is a guard and a load:
    # `AreaDataManager_Load` walks the same list on every map load and reads each
    # id into the field heap. That is right for an authored prop, which is in no
    # area's preload list and would otherwise never arrive, and wrong for these,
    # which are in one. With a city and one room in a package it loaded both maps'
    # props on either map: 49 models where 31 fit, and the field heap ran out
    # under the first field effect.
    #
    # So the table itself grows instead. The file is opened with `FS_OpenFile`,
    # which is the call that asks `pc_modfs`, so a package can replace it; the
    # appended rows are `{0, 0xFFFF}`, which is not an invention but the row this
    # game already writes for a model with no material shapes.
    if prop_map:
        if dest_rom is None:
            die("--dest-rom is needed to carry a prop: the material-shape table "
                "an appended model id needs a row in is this game's own file")
        raw = rom_file(dest_rom, MATSHP_PATH)
        n_loc, n_ids = struct.unpack_from("<HH", raw, 0)
        if len(raw) != 4 + n_loc * 4 + n_ids * 4:
            die("%s is %d bytes and its two counts want %d"
                % (MATSHP_PATH, len(raw), 4 + n_loc * 4 + n_ids * 4))
        grown = max(prop_map.values()) + 1
        if grown <= n_loc:
            die("the appended props all fall inside this game's own %d "
                "material-shape rows, which cannot be right" % n_loc)
        out = (struct.pack("<HH", grown, n_ids)
               + raw[4:4 + n_loc * 4]
               + struct.pack("<HH", *MATSHP_NO_SHAPES) * (grown - n_loc)
               + raw[4 + n_loc * 4:])
        dest = pkg / FILE_ROOT[True] / MATSHP_PATH
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(out)
        print("portmap: material-shape locators %d -> %d, so an appended prop "
              "is a row rather than a bounds check" % (n_loc, grown))

    # And the second list a ported person needs. The four overlay005 gfx tables
    # are sentinel lists rather than arrays sized by the enum, and the port's
    # own hook answers an id that is in none of them by cloning the youngster's
    # renderer and reading the NSBTX named here, which is why an appended
    # person needs no rebuild.
    if gfx_map:
        static = [(gid, member, big) for gid, member, ntex, big, follower
                  in sorted(gfx_map.values())
                  if ntex < WALK_TEXTURES and not follower]
        followers = [(gid, member) for gid, member, _n, _b, follower
                     in sorted(gfx_map.values()) if follower]
        bb_rows = []
        for gid, member, ntex, big, follower in sorted(gfx_map.values()):
            model = (BILLBOARD_MODEL_GENERIC_64x64 if big
                     else BILLBOARD_MODEL_GENERIC_32x32)
            if follower:
                bb_rows.append("%d %d %d %d\n" % (gid, member, model,
                                                  SEQ_FOLLOWER_WALK))
            elif ntex < WALK_TEXTURES:
                bb_rows.append("%d %d %d %d\n" % (gid, member, model,
                                                  SEQ_STATIC))
            else:
                bb_rows.append("%d %d\n" % (gid, member))
        write_generated(pkg, "billboard_gfx.txt", "".join(bb_rows))
        seq_rows = []
        if static:
            seq_dst = cur.take(DST_MMODEL)
            write_member(pkg, DST_MMODEL, seq_dst, STATIC_SEQ_BLOB, cooked=True)
            seq_rows.append("%d %d 4 1\n" % (SEQ_STATIC, seq_dst))
            print("portmap: %d cooked body(ies) never turn, a static sequence "
                  "at mmodel member %d draws them" % (len(static), seq_dst))
        if followers:
            # The walk is data in the cartridge, not a frame order to invent:
            # the one member that is four facings of a two-frame cycle.
            walk_src, why = portfollow.gf._walk_sequence(mmodel_src)
            if walk_src is None:
                die("the standing Pokemon need the follower walk and %s" % why)
            facings, frames = portfollow.timeline(mmodel_src[walk_src])
            walk_dst = cur.take(DST_MMODEL)
            write_member(pkg, DST_MMODEL, walk_dst, mmodel_src[walk_src],
                         cooked=True)
            seq_rows.append("%d %d %d %d\n" % (SEQ_FOLLOWER_WALK, walk_dst,
                                                facings, frames))
            print("portmap: %d standing Pokemon wear their species' follower "
                  "sheet on the follower walk (mmodel member %d, %d facings "
                  "x %d frames)" % (len(followers), walk_dst, facings, frames))
        if seq_rows:
            write_generated(pkg, "billboard_seq.txt", "".join(seq_rows))

    # The sound archive tools/portmusic.py wrote, if there is one. A --bgm is
    # an index into THIS file: without it in the package the header names a
    # sequence the running game does not have.
    if opts["sdat"] is not None:
        if not opts["sdat"].is_file():
            die("no %s, run tools/portmusic.py first" % opts["sdat"])
        dest = pkg / FILE_ROOT[True] / SDAT_PATH
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(opts["sdat"].read_bytes())
        print("portmap: sound archive -> %s (%d bytes)"
              % (SDAT_PATH, dest.stat().st_size))
    elif not opts["music"] and any(m["bgm_id"] for m in opts["maps"]):
        print("portmap: no --sdat, so the --bgm ids name sequences only if the "
              "package already carries one")

    silent = [r[0] for r in rows if r[7] == 0]
    if silent:
        print("portmap: %d map(s) name sequence 0, which is a sound effect "
              "here and not a track, Sound_PlayBGM asserts twice on every "
              "one of their loads. --music carries each map's own; %s"
              % (len(silent), ", ".join(str(h) for h in silent[:8])
                 + (" ..." if len(silent) > 8 else "")))

    # WHO SELLS, as the porter reads it. Nothing loads this: the bag and the
    # money are the server's, so the shelf is too, and the client's part is one
    # command, the fold pairs the source's mart with this game's own, and the
    # mod's patch on it sends the press across instead of opening a local list.
    #
    # It is written because the server reads THE same scripts out of the decomp
    # to learn which of its npcs sell (PortedTerrainParser.readMartScripts), and
    # two readings of one thing are only worth having if they can be compared.
    # A clerk here that the server does not know greets and sells nothing.
    mart_rows = ["%d %d %d %d\n" % (m["header"], sid, 1 if sp else 0, shop)
                 for m in opts["maps"]
                 for sid, (sp, shop) in sorted(m.get("marts", {}).items())]
    if mart_rows:
        write_generated(pkg, "marts.txt", "".join(mart_rows))
        print("portmap: %d clerk(s) sell, the server opens the shelf"
              % len(mart_rows))

    write_generated(pkg, "cooked_maps.txt",
                    "# " + " ".join(HEADER_FIELDS) + "\n"
                    + "".join(" ".join(str(v) for v in row) + "\n"
                              for row in rows))
    if opts["region"] and opts["scripts"]:
        port_pokegear(rom, pkg, opts, maps, msg_src, cur, music, box, region_matrix)
    write_package_shell(pkg, pkg.name,
                        (" and ".join(opts["region"]) or opts["maps"][0]["name"]
                         ).replace("_", " ").title())
    if not opts["scripts"]:
        print("portmap: scripts not carried (signs do not read and nobody "
              "talks); --scripts folds them")
    if totals.get("dropped_hidden"):
        print("portmap: %d person(s) left behind, hidden by their map's own "
              "arrival script" % totals["dropped_hidden"])
    if totals.get("dropped_walled") or totals.get("kept_opened"):
        print("portmap: %d person(s) left behind and %d kept on mmo/MAPWALLS' "
              "say-so" % (totals.get("dropped_walled", 0),
                          totals.get("kept_opened", 0)))
    if totals.get("dropped_unread"):
        print("portmap: %d person(s) left behind carrying a hide flag on a map "
              "with no row in mmo/MAPSCENES" % totals["dropped_unread"])
    if artless:
        print("portmap: %d sprite(s) HeartGold has no art of its own for, so "
              "the people wearing them stayed behind: %s"
              % (len(artless), ", ".join(sorted(sprites[s][1]
                                                for s in artless))))
    for s, (member, pal) in sorted(mismatched.items()):
        print("portmap: mmo/SPRITES says sprite %d is %s and mmodel member %d "
              "calls its palette %r, the two oracles disagree, so the art is "
              "not carried and nobody wears it"
              % (s, sprites[s][1], member, pal))
    if opts["scripts"]:
        print("portmap: %d script entries, %d folded whole (%.0f%%), %d "
              "person(s) and sign(s) keep a script"
              % (totals.get("entries", 0), totals.get("folded", 0),
                 100.0 * totals.get("folded", 0) / max(1, totals.get("entries", 1)),
                 totals.get("scripts", 0)))
        for why, n in sorted(fold_why.items(), key=lambda kv: -kv[1])[:6]:
            print("portmap:   %5d entries left as End, %s" % (n, why))
    if totals.get("dropped_noart"):
        print("portmap: %d person(s) left behind for want of art they could be "
              "drawn with" % totals["dropped_noart"])
    if totals.get("balls") or totals.get("hidden"):
        print("portmap: %d item ball(s) on the ground and %d hidden item(s) "
              "buried, each remembered by a flag on the ported-item band"
              % (totals.get("balls", 0), totals.get("hidden", 0)))
    if totals.get("apricorn trees"):
        print("portmap: %d apricorn tree(s) answer with the generated bank"
              % totals["apricorn trees"])
    if totals.get("balls_unflagged") or totals.get("hidden_unknown"):
        print("portmap: %d ball(s) left behind with a flag off the band, %d "
              "hidden item(s) with an index mmo/HIDDEN_ITEMS does not know"
              % (totals.get("balls_unflagged", 0),
                 totals.get("hidden_unknown", 0)))
    print("portmap: %d people, %d signs, %d doors (%d of them parked outside "
          "this package), %d triggers dropped"
          % (totals.get("people", 0), totals.get("signs", 0),
             totals.get("warps", 0) + totals.get("warps_parked", 0),
             totals.get("warps_parked", 0), totals.get("triggers_dropped", 0)))
    print("portmap: %d map(s), %d matrices, %d land members, %d areas, "
          "%d prop models, %d people -> %s"
          % (len(rows), len(matrix_dst) + len(region_matrix),
             len(land_dst), len(area_dst), len(prop_map), len(gfx_map), pkg))
    return 0


def _engine_root() -> Path:
    return _engine_pc().parent



if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
