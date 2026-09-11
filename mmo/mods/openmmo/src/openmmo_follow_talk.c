/* What the Pokemon behind you says when you turn round. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bg_window.h"
#include "charcode.h"
#include "constants/field/window.h"
#include "constants/field_base_tiles.h"
#include "constants/heap.h"
#include "constants/items.h"
#include "constants/map_object.h"
#include "constants/menu.h"
#include "constants/overworld_weather.h"
#include "constants/sound.h"
#include "constants/rtc.h"

#include "generated/movement_actions.h"
#include "generated/pokemon_data_params.h"

#include "field/field_system.h"
#include "field_system.h"

#include "field_message.h"
#include "field_overworld_state.h"
#include "field_overworld_weather.h"
#include "field_task.h"
#include "heap.h"
#include "item.h"
#include "map_header.h"
#include "map_header_data.h"
#include "map_object.h"
#include "map_object_move.h"
#include "map_tile_behavior.h"
#include "math_util.h"
#include "menu.h"
#include "message.h"
#include "party.h"
#include "pokemon.h"
#include "render_window.h"
#include "save_player.h"
#include "script_manager.h"
#include "sound_playback.h"
#include "string_gf.h"
#include "string_template.h"
#include "system.h"
#include "unk_020298BC.h"
#include "unk_020655F4.h"
#include "unk_020559DC.h"
#include "vars_flags.h"

#include "pc_modfs.h"

#include "../../../include/follower.h"
#include "../../../include/species_port.h"

/* ------------------------------------------------------------------ *
 *  The package
 * ------------------------------------------------------------------ */

/* tools/portfollow.py plants these; the header member says where each section
 * begins so a cartridge with a different count still lands and this file does
 * no arithmetic it could get wrong. */
#define TALK_PATH        "openmmo/follow_talk.narc"
#define TALK_MAGIC       0x3154464FU     /* 'OFT1' */
#define TALK_VERSION     5

#define TALK_ROW         20              /* one condition row */
#define TALK_REACT_BYTES 52
#define TALK_STEPS       5
#define TALK_STEP_BYTES  8
#define TALK_ANIM_BYTES  80
#define TALK_ANIM_FRAMES 10
#define TALK_ANIM_FRAME  8

/*
 * The candidate order, and it is HeartGold's own (ov02_0224EF94): the world table's first
 * twelve rows, then the map section's thirty, then the rest of the world's.
 */
#define TALK_MAP_ROWS    30
#define TALK_WORLD_ROWS  70
#define TALK_CANDIDATES  (TALK_MAP_ROWS + TALK_WORLD_ROWS)
#define TALK_WORLD_FIRST 12

typedef struct {
    int ok;
    int said_missing;
    u16 version;
    u16 header_base;
    u16 msg_strings;
    u16 cond_base, cond_count;
    u16 react_base, react_count;
    u16 anim_base, anim_count;
    u16 species_base, species_count;
    u16 mapsec_base, mapsec_count;
    u16 msg_base, msg_count;
    /* Four bytes a follower sprite: HeartGold's own tp_param record for it
     * (a/1/4/1), of which the follower's step code reads byte 1 (large) and
     * byte 2 (the walk-dip class), FollowMon_SetObjectParams' second slot. */
    u16 tp_base, tp_count;
    /* One byte a source header: bits 0-1 are HeartGold's own two-bit
     * `followMode` (prevent / height-restrict / allow) and bit 2 is the Bell
     * Tower's refusal of a Diglett. 0xFF is a header the table does not name,
     * and every map of this game's own is one. */
    u16 mode_base, mode_count;
    /* Not members of this archive: the bubble's model, its frame sequence and
     * its fourteen sheets live in `data/mmodel/fldeff.narc`, because that is
     * the archive the field effect manager opens for itself. openmmo_emote.c
     * is what registers them. */
    u16 emote_model, emote_seq, emote_tex, emote_count;
    /* The ball the follower goes into and comes out of: also members of
     * fldeff.narc, HeartGold's a/1/0/3 129 (the ball), 104 (the flash) and
     * 164 (the flash's animation), as ov01_0220329C loads them. */
    u16 ball_model, flash_model, flash_anim;
} TalkPackage;

static TalkPackage g_pkg;

static u16 rd16(const u8 *p)
{
    return (u16)(p[0] | (p[1] << 8));
}

static u32 rd32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

/* The header, read once. A build with no package, or one filled by an older
 * porter, declines by name rather than by drawing nothing: "the follower said
 * nothing" and "no package carries the lines" look identical from the outside
 * and the first headless drive of the walk already cost an afternoon to that. */
static const TalkPackage *talk_package(void)
{
    u8 head[56];

    if (g_pkg.ok) {
        return &g_pkg;
    }
    if (!pc_modfs_member_read(TALK_PATH, 0, head, 0, sizeof head)) {
        if (!g_pkg.said_missing) {
            g_pkg.said_missing = 1;
            printf("openmmo: no follower talk (no package carries %s; fill one"
                   " with tools/portfollow.py)\n", TALK_PATH);
            fflush(stdout);
        }
        return NULL;
    }
    if (rd32(head) != TALK_MAGIC || rd16(head + 4) != TALK_VERSION) {
        if (!g_pkg.said_missing) {
            g_pkg.said_missing = 1;
            printf("openmmo: no follower talk (%s member 0 is magic %08x"
                   " version %u, not %08x/%u)\n", TALK_PATH,
                   (unsigned)rd32(head), (unsigned)rd16(head + 4),
                   (unsigned)TALK_MAGIC, (unsigned)TALK_VERSION);
            fflush(stdout);
        }
        return NULL;
    }
    g_pkg.version = rd16(head + 4);
    g_pkg.header_base = rd16(head + 6);
    g_pkg.msg_strings = rd16(head + 8);
    g_pkg.cond_base = rd16(head + 10);
    g_pkg.cond_count = rd16(head + 12);
    g_pkg.react_base = rd16(head + 14);
    g_pkg.react_count = rd16(head + 16);
    g_pkg.anim_base = rd16(head + 18);
    g_pkg.anim_count = rd16(head + 20);
    g_pkg.species_base = rd16(head + 22);
    g_pkg.species_count = rd16(head + 24);
    g_pkg.mapsec_base = rd16(head + 26);
    g_pkg.mapsec_count = rd16(head + 28);
    g_pkg.msg_base = rd16(head + 30);
    g_pkg.msg_count = rd16(head + 32);
    g_pkg.tp_base = rd16(head + 34);
    g_pkg.tp_count = rd16(head + 36);
    g_pkg.mode_base = rd16(head + 38);
    g_pkg.mode_count = rd16(head + 40);
    g_pkg.emote_model = rd16(head + 42);
    g_pkg.emote_seq = rd16(head + 44);
    g_pkg.emote_tex = rd16(head + 46);
    g_pkg.emote_count = rd16(head + 48);
    g_pkg.ball_model = rd16(head + 50);
    g_pkg.flash_model = rd16(head + 52);
    g_pkg.flash_anim = rd16(head + 54);
    g_pkg.ok = 1;
    printf("openmmo: follower talk, %u condition tables, %u reactions,"
           " %u animations, %u lines, %u emote bubbles\n",
           (unsigned)g_pkg.cond_count, (unsigned)g_pkg.react_count,
           (unsigned)g_pkg.anim_count, (unsigned)g_pkg.msg_strings,
           (unsigned)g_pkg.emote_count);
    fflush(stdout);
    return &g_pkg;
}

/* openmmo_emote.c */
void *openmmo_emote_show(void *mapObject, int emote);
int openmmo_emote_done(void *animManager);
void openmmo_emote_finish(void *animManager);
void openmmo_emote_forget(void);

/* Where the package put the bubble's own members, for openmmo_emote.c. It asks
 * here rather than reading the header itself, so there is one reader of that
 * member and one place a version can move. */
struct openmmo_emote_members {
    int model;
    int sequence;
    int first_sheet;
    int count;
};

int openmmo_follow_talk_emote_members(struct openmmo_emote_members *out);

/* Where a follower may walk, asked of the MAP'S own game. */
int openmmo_follow_talk_map_mode(enum MapHeaderID header);

int openmmo_follow_talk_map_mode(enum MapHeaderID header)
{
    const TalkPackage *pkg = talk_package();
    int at;
    u8 v = 0xFF;

    if (pkg == NULL || pkg->mode_count == 0) {
        return -1;
    }
    at = (int)header - (int)pkg->header_base;
    if (at < 0 || at >= (int)pkg->mode_count) {
        return -1;
    }
    if (!pc_modfs_member_read(TALK_PATH, pkg->mode_base, &v, (unsigned)at, 1)) {
        return -1;
    }
    return v == 0xFF ? -1 : (int)v;
}

/* HeartGold's tp_param record for the follower wearing `gfx`, four bytes as
 * the cartridge keeps them (FollowMon_GetSizeParamBySpecies reads byte 1 of
 * the same record). 0 for anything that is not a follower, or a package
 * filled before the record was carried. */
int openmmo_follow_talk_tp_param(int gfx, unsigned char out[4]);

int openmmo_follow_talk_tp_param(int gfx, unsigned char out[4])
{
    const TalkPackage *pkg = talk_package();
    int sprites = mmo_follower_gfx_count() / 2;
    int at = gfx - mmo_follower_gfx_base();

    if (pkg == NULL || pkg->tp_count == 0 || sprites <= 0) {
        return 0;
    }
    if (at < 0 || at >= sprites * 2) {
        return 0;
    }
    /* The shiny band is the same Pokemon, so it is the same record. */
    at %= sprites;
    if (!pc_modfs_member_read(TALK_PATH, pkg->tp_base, out, (unsigned)at * 4, 4)) {
        return 0;
    }
    return 1;
}

/* Where the package put the ball's own members, for openmmo_follow_fx.c. */
int openmmo_follow_talk_ball_members(int *ball, int *flash, int *flash_anim);

int openmmo_follow_talk_ball_members(int *ball, int *flash, int *flash_anim)
{
    const TalkPackage *pkg = talk_package();

    if (pkg == NULL || pkg->ball_model == 0) {
        return 0;
    }
    *ball = pkg->ball_model;
    *flash = pkg->flash_model;
    *flash_anim = pkg->flash_anim;
    return 1;
}

int openmmo_follow_talk_emote_members(struct openmmo_emote_members *out)
{
    const TalkPackage *pkg = talk_package();

    if (pkg == NULL || out == NULL || pkg->emote_count == 0) {
        return 0;
    }
    out->model = pkg->emote_model;
    out->sequence = pkg->emote_seq;
    out->first_sheet = pkg->emote_tex;
    out->count = pkg->emote_count;
    return 1;
}

/* ------------------------------------------------------------------ *
 *  The context (ov02_0224F058 and the ten routines it calls)
 * ------------------------------------------------------------------ */

/*
 * Byte for byte the buffer HeartGold builds on its own stack, so a condition below can be read
 * straight against the game's offsets.
 */
typedef struct {
    u8 held;         /* 0x00  the follower is holding something */
    u8 pocket;       /* 0x01  which pocket that item lives in, banded */
    u8 hp;           /* 0x02  1 full .. 5 nearly out */
    u8 status;       /* 0x03  1 well, 2 burn, 3 freeze, 4 paralysis, 5 poison, 8 asleep */
    u8 level;        /* 0x04 */
    u8 type1;        /* 0x05  banded, not the type id */
    u8 type2;        /* 0x06 */
    u8 friendship;   /* 0x07  raw 0..255 */
    u8 nature_group; /* 0x08 */
    u8 gender;       /* 0x09  1 male, 2 otherwise */
    u8 category;     /* 0x0A  the per-species byte */
    u8 leaves;       /* 0x0B  five shiny-leaf bits; always 0 here */
    u8 crowd;        /* 0x0C  people standing next to the player */
    u8 hidden;       /* 0x0D  undiscovered hidden items on this map */
    u8 sprite_54;    /* 0x0E  written by the source, read by nothing */
    u8 sprite_55;    /* 0x0F */
    u8 sprite_56;    /* 0x10 */
    u8 weather;      /* 0x11  1 clear, 3 raining, 0 anything else */
    u8 behavior;     /* 0x12  the tile the follower stands on */
    u8 grass;        /* 0x13  1 a tile that meets Pokemon, 2 not */
    u8 time;         /* 0x14  time of day + 1 */
    s8 mood;         /* 0x15 */
    u8 athlon;       /* 0x16  the Pokeathlon stat; 0 here, always */
    u8 facing;       /* 0x17  the follower's own heading, banded */
    u16 header;      /* 0x1A  the map, in the source game's numbering */
} TalkCtx;

/* Which mood the lead is in. HeartGold's FieldSystemUnkSub108, with the same
 * three fields and the same lifetime: reset the moment the lead is a different
 * Pokemon, and gone when the field is. */
static struct {
    int registered;
    u16 species;
    u32 personality;
    s8 mood;
} g_mood;

static Pokemon *lead_of(FieldSystem *fs)
{
    Party *party;
    int n, i;

    if (fs == NULL || fs->saveData == NULL) {
        return NULL;
    }
    party = SaveData_GetParty(fs->saveData);
    if (party == NULL) {
        return NULL;
    }
    n = Party_GetCurrentCount(party);
    for (i = 0; i < n; i++) {
        Pokemon *mon = Party_GetPokemonBySlotIndex(party, i);

        if (mon == NULL || Pokemon_GetValue(mon, MON_DATA_IS_EGG, NULL)) {
            continue;
        }
        if (Pokemon_GetValue(mon, MON_DATA_HP, NULL) > 0) {
            return mon;
        }
    }
    return NULL;
}

/* FieldSystem_UnkSub108_Set. A different lead is a fresh mood, and the same
 * lead keeps the one it has, which is why this asks about the personality
 * and not only the species. */
static void mood_register(Pokemon *mon)
{
    u16 species = (u16)Pokemon_GetValue(mon, MON_DATA_SPECIES, NULL);
    u32 personality = Pokemon_GetValue(mon, MON_DATA_PERSONALITY, NULL);

    if (species == 0) {
        return;
    }
    if (!g_mood.registered || g_mood.species != species
        || g_mood.personality != personality) {
        g_mood.registered = 1;
        g_mood.species = species;
        g_mood.personality = personality;
        g_mood.mood = 0;
    }
}

/* FieldSystem_UnkSub108_MoveMoodTowardsNeutral, one step a field frame, which
 * is where a mood a talk moved goes back to nothing. */
void openmmo_follow_talk_step(void);

void openmmo_follow_talk_step(void)
{
    if (!g_mood.registered) {
        return;
    }
    if (g_mood.mood < 0) {
        g_mood.mood++;
    } else if (g_mood.mood > 0) {
        g_mood.mood--;
    }
}

void openmmo_follow_talk_reset(void);

void openmmo_follow_talk_reset(void)
{
    memset(&g_mood, 0, sizeof g_mood);
    openmmo_emote_forget();
}

/* ov02_0224F820: a pocket, banded. Both games number their pockets the same
 * seven ways, so the band crosses as it is. */
static u8 pocket_band(u32 pocket)
{
    switch (pocket) {
    case POCKET_ITEMS:        return 4;
    case POCKET_MEDICINE:     return 2;
    case POCKET_BALLS:        return 1;
    case POCKET_TMHMS:        return 7;
    case POCKET_BERRIES:      return 6;
    case POCKET_MAIL:         return 5;
    case POCKET_BATTLE_ITEMS: return 3;
    }
    return 8;
}

/* ov02_0224F79C: a type, banded. The band is not the type id and not an
 * ordering of it either, it is the source game's own grouping, carried. */
static u8 type_band(u32 type)
{
    static const u8 band[18] = {
        1, 7, 10, 8, 9, 13, 12, 14, 17, 0, 2, 3, 5, 4, 11, 6, 15, 16
    };

    return type < 18 ? band[type] : 0;
}

/* ov02_02253AC0, the nature grouping, read out of the same overlay. Twenty-five
 * natures into six groups; which six is the table's business and not ours. */
static u8 nature_group(u8 nature)
{
    static const u8 group[25] = {
        1, 2, 3, 4, 5, 2, 1, 3, 4, 5, 2, 3, 1, 4, 5,
        2, 3, 4, 1, 5, 2, 3, 4, 5, 1
    };

    return nature < 25 ? group[nature] : 0;
}

/* ov02_02250594: a friendship band. */
static int friendship_in(int band, int friendship)
{
    switch (band) {
    case 1:  return friendship == 255;
    case 2:  return friendship >= 200 && friendship < 255;
    case 3:  return friendship >= 150 && friendship < 200;
    case 4:  return friendship >= 90 && friendship < 150;
    case 5:  return friendship >= 60 && friendship < 90;
    case 6:  return friendship >= 30 && friendship < 60;
    case 7:  return friendship >= 1 && friendship < 30;
    case 8:  return friendship == 0;
    case 9:  return friendship >= 90;
    case 10: return friendship < 60;
    }
    return 0;
}

/* ov02_02250628: a mood band, and it is signed on both sides. */
static int mood_in(int band, int mood)
{
    switch (band) {
    case 1:  return mood == 127;
    case 2:  return mood >= 100 && mood < 127;
    case 3:  return mood >= 50 && mood < 100;
    case 4:  return mood >= 30 && mood < 50;
    case 5:  return mood > -30 && mood < 30;
    case 6:  return mood > -50 && mood <= -30;
    case 7:  return mood > -127 && mood <= -50;
    case 8:  return mood == -127;
    case 9:  return mood >= 0;
    case 10: return mood <= -1;
    }
    return 0;
}

/* ov02_022506D4: the per-species category, which is an exact match below 250
 * and one of five bands above it. */
static int category_in(int want, int have)
{
    if (want <= 249) {
        return want == have;
    }
    switch (want) {
    case 250: return have <= 19;
    case 251: return have <= 130;
    case 252: return have >= 140 && have <= 149;
    case 253: return have >= 160;
    case 254: return have >= 220;
    }
    return 0;
}

/* ov02_0224F324: everything the context knows about the Pokemon itself. */
static void ctx_from_mon(Pokemon *mon, TalkCtx *c)
{
    u32 item = Pokemon_GetValue(mon, MON_DATA_HELD_ITEM, NULL);
    u32 hp, maxhp, status, level;
    int pct;

    if (item != 0) {
        c->held = 1;
        c->pocket = pocket_band(Item_LoadParam((u16)item, ITEM_PARAM_FIELD_POCKET,
                                               HEAP_ID_FIELD2));
    } else {
        c->held = 0;
        c->pocket = 8;
    }

    hp = Pokemon_GetValue(mon, MON_DATA_HP, NULL);
    maxhp = Pokemon_GetValue(mon, MON_DATA_MAX_HP, NULL);
    pct = maxhp != 0 ? (int)((hp * 100) / maxhp) : 0;
    if (pct == 100) {
        c->hp = 1;
    } else if (pct >= 75) {
        c->hp = 2;
    } else if (pct >= 50) {
        c->hp = 3;
    } else if (pct >= 25) {
        c->hp = 4;
    } else {
        c->hp = 5;
    }

    /* The order matters and is the source's: a badly poisoned Pokemon is
     * "poisoned" before it is anything else, and the sleep counter is asked
     * about after poison and before the three single bits. */
    status = Pokemon_GetValue(mon, MON_DATA_STATUS, NULL);
    if (status & 0x88) {
        c->status = 5;
    } else if (status & 0x07) {
        c->status = 8;
    } else if (status & 0x10) {
        c->status = 2;
    } else if (status & 0x20) {
        c->status = 3;
    } else if (status & 0x40) {
        c->status = 4;
    } else {
        c->status = 1;
    }

    /* The source's own arithmetic, kept whole. Its third arm is unreachable
     * (a level under 48 can never also be over 52) and it is left in rather
     * than tidied away, because the row field that reads this is two bits wide
     * and can never equal any of 4, 5 or 6 anyway, see `cond_pass`. */
    level = Pokemon_GetValue(mon, MON_DATA_LEVEL, NULL);
    if ((int)level + 2 >= 50) {
        c->level = 4;
    } else if ((int)level - 2 > 50) {
        c->level = 5;
    } else {
        c->level = 6;
    }

    c->type1 = type_band(Pokemon_GetValue(mon, MON_DATA_TYPE_1, NULL));
    c->type2 = type_band(Pokemon_GetValue(mon, MON_DATA_TYPE_2, NULL));
    c->friendship = (u8)Pokemon_GetValue(mon, MON_DATA_FRIENDSHIP, NULL);
    c->nature_group = nature_group(Pokemon_GetNature(mon));
    c->gender = Pokemon_GetValue(mon, MON_DATA_GENDER, NULL) == 0 ? 1 : 2;
    /* Five shiny leaves, and this game has none of them, so the whole field is
     * clear and every row that asks about one is refused. */
    c->leaves = 0;
}

/* ov02_0224F4BC: how crowded the tile the player is standing on is. The source
 * also notes three of its own sprite ids while it walks the table; nothing ever
 * reads those three, so they are set the same way and named for what they are. */
static void ctx_from_crowd(FieldSystem *fs, TalkCtx *c)
{
    MapObject *player;
    MapObject *obj = NULL;
    int idx = 0;
    int px, pz;

    c->crowd = 0;
    c->sprite_54 = c->sprite_55 = c->sprite_56 = 0;

    player = MapObjectMan_GetPlayerMapObject(fs->mapObjMan);
    if (player == NULL) {
        return;
    }
    px = MapObject_GetX(player);
    pz = MapObject_GetZ(player);

    while (MapObjectMan_FindObjectWithStatus(fs->mapObjMan, &obj, &idx,
                                          MAP_OBJ_STATUS_0)) {
        u32 gfx = MapObject_GetGraphicsID(obj);
        u32 id;
        int dx, dz;

        if (gfx == 0x54) {
            c->sprite_54 = 1;
            continue;
        }
        if (gfx == 0x55) {
            c->sprite_55 = 1;
            continue;
        }
        if (gfx == 0x56) {
            c->sprite_56 = 1;
            continue;
        }
        dx = px - MapObject_GetX(obj);
        dz = pz - MapObject_GetZ(obj);
        if (dx < -1 || dx > 1 || dz < -1 || dz > 1) {
            continue;
        }
        id = MapObject_GetLocalID(obj);
        if (id == 0xFD || id == 0xFF) {
            continue;   /* the player, and the follower */
        }
        c->crowd++;
    }
}

/* ov02_0224F580: how many hidden items this map is still keeping. HeartGold
 * counts background events of type 2 whose flag is clear; this game says the
 * same thing with its own script-id band, which is what its own field control
 * asks (`SCRIPT_ID_OFFSET_HIDDEN_ITEMS`). */
static void ctx_from_hidden(FieldSystem *fs, TalkCtx *c)
{
    const BgEvent *events = MapHeaderData_GetBgEvents(fs);
    int n = MapHeaderData_GetNumBgEvents(fs);
    VarsFlags *flags;
    int i;

    c->hidden = 0;
    if (events == NULL || n <= 0 || fs->saveData == NULL) {
        return;
    }
    flags = SaveData_GetVarsFlags(fs->saveData);
    if (flags == NULL) {
        return;
    }
    for (i = 0; i < n; i++) {
        u16 script = events[i].script;

        if (script < SCRIPT_ID_OFFSET_HIDDEN_ITEMS) {
            continue;
        }
        if (!VarsFlags_CheckFlag(flags, Script_GetHiddenItemFlag(script))) {
            c->hidden++;
        }
    }
}

/* ov02_0224F5D0. HeartGold asks its own weather enum for exactly two values;
 * this game's enum is not that enum, so the two are named rather than passed
 * through, a raw enum crossing would have made rain out of a sandstorm. */
static void ctx_from_weather(FieldSystem *fs, TalkCtx *c)
{
    int weather = FieldSystem_GetWeather(fs, fs->location != NULL
                                         ? fs->location->mapHeaderID
                                         : (enum MapHeaderID)0);

    if (weather == OVERWORLD_WEATHER_CLEAR) {
        c->weather = 1;
    } else if (weather == OVERWORLD_WEATHER_RAINING) {
        c->weather = 3;
    } else {
        c->weather = 0;
    }
}

/* ov02_0224F5FC: the tile the follower is standing on, not the player's. */
static void ctx_from_tile(FieldSystem *fs, MapObject *follower, TalkCtx *c)
{
    u32 behavior = MapObject_GetCurrTileBehavior(follower);

    c->behavior = (u8)behavior;
    c->grass = (TileBehavior_IsTallGrass((u8)behavior)
                || TileBehavior_IsVeryTallGrass((u8)behavior)) ? 1 : 2;
}

/* ov02_0224F728: which way the follower is looking, in the source's own order.
 * Its four directions are not this game's four, which is the whole reason this
 * is a switch and not an addition. */
static u8 facing_band(int dir)
{
    switch (dir) {
    case 0: return 3;
    case 1: return 4;
    case 2: return 2;
    case 3: return 1;
    }
    return 0;
}

/* ov02_0224F76C: the per-species category byte, out of the package. */
static u8 species_category(const TalkPackage *pkg, int species)
{
    u8 v = 0;

    if (species < 1) {
        return 0;
    }
    /* One byte at (species - 1). A species the table does not reach, every
     * one this game added past HeartGold's 493, reads as no category, and
     * the rows that ask about a category then refuse it. */
    if (!pc_modfs_member_read(TALK_PATH, pkg->species_base, &v,
                              (unsigned)(species - 1), 1)) {
        return 0;
    }
    return v;
}

/* The source game's own map header for where the player is standing, or -1. */
static int source_header(const TalkPackage *pkg, FieldSystem *fs)
{
    int here;

    if (fs->location == NULL) {
        return -1;
    }
    here = (int)fs->location->mapHeaderID - (int)pkg->header_base;
    if (here < 0 || here >= (int)pkg->mapsec_count) {
        return -1;
    }
    return here;
}

static int source_mapsec(const TalkPackage *pkg, int header)
{
    u8 sec = 0xFF;

    if (header < 0) {
        return -1;
    }
    if (!pc_modfs_member_read(TALK_PATH, pkg->mapsec_base, &sec,
                              (unsigned)header, 1)) {
        return -1;
    }
    return sec == 0xFF ? -1 : (int)sec;
}

static void build_ctx(const TalkPackage *pkg, FieldSystem *fs, Pokemon *mon,
                      MapObject *follower, TalkCtx *c)
{
    int header;

    memset(c, 0, sizeof *c);
    ctx_from_mon(mon, c);
    ctx_from_crowd(fs, c);
    ctx_from_hidden(fs, c);
    ctx_from_weather(fs, c);
    ctx_from_tile(fs, follower, c);
    c->time = (u8)(FieldSystem_GetTimeOfDay(fs) + 1);
    c->mood = g_mood.mood;
    /* There is no Pokeathlon here to be good at, so the answer is "none" and
     * the rows that ask are refused. Written rather than left at zero by
     * accident: the design notes names this one. */
    c->athlon = 0;
    c->facing = facing_band(MapObject_GetFacingDir(follower));
    c->category = species_category(
        pkg, mmo_species_port_wire_id(
                 (int)Pokemon_GetValue(mon, MON_DATA_SPECIES, NULL)));
    header = source_header(pkg, fs);
    c->header = header < 0 ? 0xFFFF : (u16)header;
}

/* ------------------------------------------------------------------ *
 *  The condition (ov02_0224F108)
 * ------------------------------------------------------------------ */

/*
 * Every test the source makes, in the source's order, against the source's own bit positions.
 */
static int cond_pass(const u8 *row, const TalkCtx *c, VarsFlags *flags, int chance)
{
    int v;

    (void)flags;

    if (chance >= row[0x11]) {
        return 0;
    }
    /* A HeartGold story flag. The world here is the one after the story, so
     * the flag is set; nothing is looked up, because this client's own flags
     * are a different numbering answering to a different authority. */
    if (rd16(row + 0x12) != 0) {
        /* set: fall through */
    }

    v = row[3] & 0x1F;
    if (v != 0) {
        if (v == 9) {
            if (c->held == 0) {
                return 0;
            }
        } else if (v != c->pocket) {
            return 0;
        }
    }

    if (row[0] != 0 && row[0] != c->hp) {
        return 0;
    }

    v = row[2] >> 5;
    if (v != 0) {
        if (v == 7) {
            if (c->status != 2 && c->status != 3 && c->status != 4
                && c->status != 5 && c->status != 8) {
                return 0;
            }
        } else if (v != c->status) {
            return 0;
        }
    }

    v = rd16(row + 0x0A) & 0x07;
    if (v != 0) {
        if (v == 5) {
            if (c->crowd < 5) {
                return 0;
            }
        } else if (v != c->crowd) {
            return 0;
        }
    }

    v = row[0x10] >> 5;
    if (v != 0) {
        if (v == 4) {
            if (c->hidden < 4) {
                return 0;
            }
        } else if (v != c->hidden) {
            return 0;
        }
    }

    /* Two bits against a level band that is 4, 5 or 6, so a set field can
     * never pass. The shipped table never sets it; the test is here because
     * the table is the cartridge's and a later one might. */
    v = (row[0x10] >> 1) & 0x03;
    if (v != 0 && v != c->level) {
        return 0;
    }

    /* And this one cannot pass at all: every arm of the source's own branch
     * ends in a refusal (0x0224F20E). Kept whole rather than dropped, so a
     * reader comparing the two sees the same shape. */
    v = (row[0x10] >> 3) & 0x03;
    if (v != 0) {
        return 0;
    }

    v = row[4] & 0x1F;
    if (v != 0 && v != c->type1 && v != c->type2) {
        return 0;
    }

    v = rd16(row + 8) & 0x07;
    if (v != 0 && v != c->weather) {
        return 0;
    }

    v = rd16(row + 0x0E);
    if (v != 0 && v != c->behavior) {
        return 0;
    }

    if (row[5] != 0 && row[5] != c->grass) {
        return 0;
    }

    v = rd16(row + 0x0C);
    if (v != 0 && (v - 1) != (int)c->header) {
        return 0;
    }

    v = (rd16(row + 0x0A) >> 3) & 0x07;
    if (v != 0 && v != c->time) {
        return 0;
    }

    v = row[1] & 0x0F;
    if (v != 0 && !mood_in(v, c->mood)) {
        return 0;
    }

    v = (row[4] >> 3) & 0x07;
    if (v != 0 && v != c->athlon) {
        return 0;
    }

    v = row[1] >> 4;
    if (v != 0 && !friendship_in(v, c->friendship)) {
        return 0;
    }

    v = row[2] & 0x07;
    if (v != 0 && v != c->nature_group) {
        return 0;
    }

    if (row[6] != 0 && !category_in(row[6], c->category)) {
        return 0;
    }

    /* The shiny-leaf test. `ov02_02250738` masks the five leaf bits with a
     * table and passes when nothing is left; with no leaves here the mask is
     * always empty, so a row that asks about them passes for the same reason
     * a Pokemon with no leaves passes in the source. */
    if (row[7] != 0 && c->leaves != 0) {
        return 0;
    }

    v = (row[2] >> 3) & 0x03;
    if (v != 0 && v != c->gender) {
        return 0;
    }

    v = rd16(row + 8) >> 13;
    if (v != 0 && v != c->facing) {
        return 0;
    }

    return 1;
}

/* ov02_0224EF94: the first row of the hundred whose every test passes, or 0. */
static int pick_reaction(const TalkPackage *pkg, FieldSystem *fs,
                         const TalkCtx *c, int mapsec)
{
    static u8 world[TALK_WORLD_ROWS * TALK_ROW];
    static u8 map[TALK_MAP_ROWS * TALK_ROW];
    int order[TALK_CANDIDATES];
    int n = 0, i;
    VarsFlags *flags = fs->saveData != NULL
                       ? SaveData_GetVarsFlags(fs->saveData) : NULL;
    int have_map = 0;

    if (!pc_modfs_member_read(TALK_PATH, pkg->cond_base, world, 0,
                              sizeof world)) {
        return 0;
    }
    if (mapsec >= 0 && mapsec + 1 < (int)pkg->cond_count) {
        have_map = pc_modfs_member_read(TALK_PATH,
                                        (unsigned)(pkg->cond_base + 1 + mapsec),
                                        map, 0, sizeof map);
    }
    if (!have_map) {
        memset(map, 0, sizeof map);
    }

    for (i = 0; i < TALK_WORLD_FIRST; i++) {
        order[n++] = TALK_MAP_ROWS + i;
    }
    for (i = 0; i < TALK_MAP_ROWS; i++) {
        order[n++] = i;
    }
    for (i = TALK_WORLD_FIRST; i < TALK_WORLD_ROWS; i++) {
        order[n++] = TALK_MAP_ROWS + i;
    }

    for (i = 0; i < n; i++) {
        int at = order[i];
        const u8 *row = at < TALK_MAP_ROWS
                        ? map + at * TALK_ROW
                        : world + (at - TALK_MAP_ROWS) * TALK_ROW;
        int rid = rd16(row + 0x0A) >> 6;

        if (rid == 0) {
            continue;
        }
        /* The roll is per row and the source takes a fresh one each time; a
         * single roll reused would turn a list of independent chances into one
         * ordered by luck. */
        if (cond_pass(row, c, flags, (int)(LCRNG_Next() % 100))) {
            return rid;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ *
 *  The reaction (ov02_0224F880, ov02_0224F8FC, ov02_02250004)
 * ------------------------------------------------------------------ */

/* One reaction, unpacked. The tail's four bytes at 44..47 are two reaction ids
 * what to run when the question is answered no, and when it is answered
 * yes, and they are read as ids rather than as anything else because that is
 * what Task_FollowMonInteract's own state 2 does with them. */
typedef struct {
    u8 raw[TALK_REACT_BYTES];
    int steps;
} Reaction;

static int reaction_load(const TalkPackage *pkg, int id, Reaction *r)
{
    int i;

    if (id < 1 || id > (int)pkg->react_count) {
        return 0;
    }
    if (!pc_modfs_member_read(TALK_PATH, (unsigned)(pkg->react_base + id - 1),
                              r->raw, 0, TALK_REACT_BYTES)) {
        return 0;
    }
    r->steps = 0;
    for (i = 0; i < TALK_STEPS; i++) {
        if (rd16(r->raw + i * TALK_STEP_BYTES) == 0xFFFF) {
            break;
        }
        r->steps++;
    }
    return 1;
}

#define STEP_ANIM(r, i)  rd16((r)->raw + (i) * TALK_STEP_BYTES + 0)
#define STEP_MSG(r, i)   rd16((r)->raw + (i) * TALK_STEP_BYTES + 2)
#define STEP_SOUND(r, i) rd16((r)->raw + (i) * TALK_STEP_BYTES + 4)
#define STEP_EMOTE(r, i) ((r)->raw[(i) * TALK_STEP_BYTES + 6])
#define STEP_DELAY(r, i) ((r)->raw[(i) * TALK_STEP_BYTES + 7])

#define REACT_ASKS(r)      (rd32((r)->raw + 40) != 0)
#define REACT_ANSWER_NO(r) rd16((r)->raw + 44)
#define REACT_ANSWER_YES(r) rd16((r)->raw + 46)
#define REACT_FRIENDSHIP(r) ((s8)(r)->raw[48])
#define REACT_MOOD(r)       ((s8)(r)->raw[49])
#define REACT_ACCESSORY(r)  ((r)->raw[50])
#define REACT_LEAF(r)       ((r)->raw[51])

/* The two sounds a step can name, and they are the only two the whole table
 * uses: HeartGold's SEQ_SE_END + 1 and + 2, both of which mean "the follower's
 * own cry" in one mode or the other. No sound effect crosses, which the porter
 * refuses to let change quietly. */
#define HG_SEQ_SE_END 2378

/* ------------------------------------------------------------------ *
 *  The task
 * ------------------------------------------------------------------ */

typedef struct {
    Pokemon *mon;
    MapObject *follower;
    MapObjectManager *man;

    Reaction react;
    int step;            /* which of the reaction's steps */
    int sub;             /* the source's own state[0x869] */

    u8 anim[TALK_ANIM_BYTES];
    int anim_frame;      /* state[0x86B] */
    int anim_tick;       /* state[0x86A] */
    int anim_live;

    /* The window, exactly as openmmo_dialog.c raises one. */
    Window window;
    String *str;
    Menu *menu;
    u8 printer;
    u8 added;

    void *emote;         /* the bubble's effect while it is up */
    int done;
} Talk;

static Talk *s_live;

static const WindowTemplate sYesNoWindow = {
    .bgLayer = BG_LAYER_MAIN_3,
    .tilemapLeft = 25,
    .tilemapTop = 13,
    .width = 6,
    .height = 4,
    .palette = FIELD_MESSAGE_PALETTE_INDEX,
    .baseTile = BASE_TILE_YES_NO_MENU,
};

#define MENU_FRAME_TILE (1024 - (18 + 12) - 9)
#define MENU_FRAME_PAL  11
#define TALK_CHARS      1024

/* One line out of the bank, DECRYPTED here. */
#define TALK_MSG_TABLE_MUL 765
#define TALK_MSG_KEY_START 596947
#define TALK_MSG_KEY_INC   18749

static int talk_read_chars(const TalkPackage *pkg, int entry, charcode_t *dst,
                           int cap)
{
    u8 head[4];
    u8 row[8];
    u32 off, len, k, kk;
    int i;

    if (entry < 0 || entry >= (int)pkg->msg_strings) {
        return 0;
    }
    if (!pc_modfs_member_read(TALK_PATH, pkg->msg_base, head, 0, sizeof head)) {
        return 0;
    }
    if (!pc_modfs_member_read(TALK_PATH, pkg->msg_base, row,
                              (unsigned)(4 + entry * 8), sizeof row)) {
        return 0;
    }
    k = ((u32)rd16(head + 2) * TALK_MSG_TABLE_MUL * (u32)(entry + 1)) & 0xFFFF;
    kk = k | (k << 16);
    off = rd32(row) ^ kk;
    len = rd32(row + 4) ^ kk;
    if (len == 0 || (int)len > cap - 1) {
        /* A line longer than the buffer is cut rather than dropped: the window
         * scrolls, and half a sentence is a better report than none. */
        if ((int)len > cap - 1) {
            len = (u32)(cap - 1);
        } else {
            return 0;
        }
    }
    k = ((u32)(entry + 1) * TALK_MSG_KEY_START) & 0xFFFF;
    for (i = 0; i < (int)len; i++) {
        u8 pair[2];

        if (!pc_modfs_member_read(TALK_PATH, pkg->msg_base, pair,
                                  (unsigned)(off + i * 2), 2)) {
            return 0;
        }
        dst[i] = (charcode_t)(rd16(pair) ^ (u16)k);
        k = (k + TALK_MSG_KEY_INC) & 0xFFFF;
    }
    dst[len] = 0xFFFF;
    return 1;
}

/* FollowMon_PlaceholdersSet: the five slots every line of the bank may name. */
static String *talk_line(FieldSystem *fs, Talk *t, const TalkPackage *pkg,
                         int entry)
{
    StringTemplate *tmpl;
    String *raw;
    String *out;
    charcode_t *chars;
    BoxPokemon *box = Pokemon_GetBoxPokemon(t->mon);

    chars = Heap_Alloc(HEAP_ID_FIELD2, TALK_CHARS * sizeof(charcode_t));
    if (chars == NULL) {
        return NULL;
    }
    if (!talk_read_chars(pkg, entry, chars, TALK_CHARS)) {
        printf("openmmo: the follower talk bank has no line %d\n", entry);
        fflush(stdout);
        Heap_Free(chars);
        return NULL;
    }
    raw = String_Init(TALK_CHARS, HEAP_ID_FIELD2);
    out = String_Init(TALK_CHARS, HEAP_ID_FIELD2);
    tmpl = StringTemplate_New(5, 32, HEAP_ID_FIELD2);
    if (raw == NULL || out == NULL || tmpl == NULL) {
        if (raw != NULL) {
            String_Free(raw);
        }
        if (out != NULL) {
            String_Free(out);
        }
        if (tmpl != NULL) {
            StringTemplate_Free(tmpl);
        }
        Heap_Free(chars);
        return NULL;
    }
    String_CopyChars(raw, chars);
    Heap_Free(chars);
    printf("openmmo: the follower's line %d\n", entry);
    fflush(stdout);
    StringTemplate_SetNickname(tmpl, 0, box);
    StringTemplate_SetSpeciesName(tmpl, 1, box);
    StringTemplate_SetPlayerName(tmpl, 2,
                                 SaveData_GetTrainerInfo(fs->saveData));
    StringTemplate_SetLocationName(
        tmpl, 3,
        (u32)MapHeader_GetMapLabelTextID(fs->location != NULL
                                         ? fs->location->mapHeaderID
                                         : (enum MapHeaderID)0));
    StringTemplate_SetItemName(
        tmpl, 4, Pokemon_GetValue(t->mon, MON_DATA_HELD_ITEM, NULL));
    StringTemplate_Format(tmpl, out, raw);
    StringTemplate_Free(tmpl);
    String_Free(raw);
    return out;
}

static void talk_print(FieldSystem *fs, Talk *t, String *str)
{
    const Options *options = SaveData_GetOptions(fs->saveData);

    if (t->str != NULL) {
        String_Free(t->str);
    }
    t->str = str;
    if (!t->added) {
        FieldMessage_AddWindow(fs->bgConfig, &t->window, BG_LAYER_MAIN_3);
        t->added = 1;
    }
    FieldMessage_DrawWindow(&t->window, options);
    t->printer = FieldMessage_Print(&t->window, t->str, options, 1);
}

static void talk_close_window(Talk *t)
{
    if (t->added) {
        Window_EraseMessageBox(&t->window, 0);
        t->added = 0;
    }
    if (t->str != NULL) {
        String_Free(t->str);
        t->str = NULL;
    }
}

/* ov02_02250504: the mood and the friendship the reaction is worth, both
 * clamped the way the source clamps them. */
static void talk_finish_deltas(Talk *t)
{
    int mood = g_mood.mood + REACT_MOOD(&t->react);
    int friendship = (int)Pokemon_GetValue(t->mon, MON_DATA_FRIENDSHIP, NULL)
                     + REACT_FRIENDSHIP(&t->react);
    u8 f;

    if (mood > 127) {
        mood = 127;
    } else if (mood < -127) {
        mood = -127;
    }
    g_mood.mood = (s8)mood;

    if (friendship > 255) {
        friendship = 255;
    } else if (friendship < 0) {
        friendship = 0;
    }
    f = (u8)friendship;
    Pokemon_SetValue(t->mon, MON_DATA_FRIENDSHIP, &f);
}

/*
 * ov02_02250004 with its animation loaded: one frame of the follower's little performance.
 * Returns 1 when the animation is over.
 */
static int talk_anim_frame(Talk *t, u16 sound, int species, int form)
{
    const u8 *frame;
    int face;

    if (t->anim_frame >= TALK_ANIM_FRAMES) {
        return 1;
    }
    frame = t->anim + t->anim_frame * TALK_ANIM_FRAME;
    if (frame[0] == 0xFF) {
        return 1;
    }
    if (t->anim_tick == 0) {
        face = frame[0];
        if (face != 0) {
            /*
             * The frame names a direction, 1..4, in the source's own order, and its order is
             * this game's: both number their face movement actions north, south, west, east,
             * and both number DIR the same way, so `dir` and `MOVEMENT_ACTION_FACE_*` are one
             * number.
             */
            LocalMapObj_SetAnimationCode(
                t->follower,
                (enum MovementAction)(MOVEMENT_ACTION_FACE_NORTH + face - 1));
        }
        /* frame[5] gates the sound: a frame that is not the one carrying the
         * cry stays quiet even though the step names one. */
        if (frame[5] != 0 && sound != 0) {
            /* HeartGold's two modes are 0 and 11 and it passes them
             * straight into its own cry-modulation table; the two games number
             * that table the same way, so 0 is the plain cry and 11 the one a
             * Pokemon in trouble makes. */
            if (sound == HG_SEQ_SE_END + 1) {
                Sound_PlayPokemonCryEx(POKECRY_NORMAL, (u16)species,
                                       0, 100, HEAP_ID_FIELD2, (u8)form);
            } else if (sound == HG_SEQ_SE_END + 2) {
                Sound_PlayPokemonCryEx(POKECRY_PINCH_NORMAL, (u16)species,
                                       0, 100, HEAP_ID_FIELD2, (u8)form);
            }
        }
    }
    t->anim_tick++;
    if (t->anim_tick >= frame[1]) {
        t->anim_tick = 0;
        t->anim_frame++;
    }
    return 0;
}

static void talk_free(Talk *t)
{
    /* A bubble outliving the talk that raised it would hold the resource slots
     * for a map it is no longer on. */
    openmmo_emote_finish(t->emote);
    t->emote = NULL;
    talk_close_window(t);
    if (t->menu != NULL) {
        t->menu = NULL;
    }
    if (t->follower != NULL) {
        /* Give the MOVEMENT action back, or the follower never walks again. */
        sub_020656DC(t->follower);
        MapObject_SetPauseMovementOff(t->follower);
    }
    /* ReleaseAll. */
    if (t->man != NULL) {
        MapObjectMan_UnpauseAllMovement(t->man);
    }
    s_live = NULL;
    Heap_Free(t);
}

/* Start a reaction: load it, and set the interpreter back to its first step.
 * ov02_0224F880 does exactly this and nothing else. */
static int talk_begin(const TalkPackage *pkg, Talk *t, int id)
{
    if (!reaction_load(pkg, id, &t->react)) {
        printf("openmmo: follower talk has no reaction %d\n", id);
        fflush(stdout);
        return 0;
    }
    t->step = 0;
    t->sub = 0;
    t->anim_live = 0;
    printf("openmmo: follower says reaction %d (%d step%s%s)\n", id,
           t->react.steps, t->react.steps == 1 ? "" : "s",
           REACT_ASKS(&t->react) ? ", and asks" : "");
    fflush(stdout);
    return 1;
}

/* Task_FollowMonInteract's states 3 and 4: what the reaction hands over. This
 * game has a fashion case and no shiny leaves, so one of the two lands. */
static void talk_reward(FieldSystem *fs, Talk *t)
{
    u8 accessory = REACT_ACCESSORY(&t->react);
    u8 leaf = REACT_LEAF(&t->react);

    if (accessory != 0) {
        FashionCase *fc = ImageClips_GetFashionCase(
            SaveData_GetImageClips(fs->saveData));

        if (fc != NULL && FashionCase_CanFitAccessoryCount(fc, accessory - 1u, 1)) {
            FashionCase_AddAccessory(fc, accessory - 1u, 1);
            printf("openmmo: the follower handed over accessory %u\n",
                   (unsigned)accessory);
        } else {
            printf("openmmo: the follower's accessory %u would not fit\n",
                   (unsigned)accessory);
        }
        fflush(stdout);
        return;
    }
    if (leaf != 0) {
        /* MON_DATA_SHINY_LEAF_A..E are 183..187 in HeartGold and are not data
         * this game's Pokemon carry at all, so the line plays and the leaf
         * does not land. The design notes names the five reactions this is. */
        printf("openmmo: the follower found shiny leaf %u, which this game has"
               " no room for\n", (unsigned)leaf);
        fflush(stdout);
    }
}

static int start_yesno(FieldSystem *fs, Talk *t)
{
    LoadStandardWindowGraphics(fs->bgConfig, BG_LAYER_MAIN_3,
                               MENU_FRAME_TILE, MENU_FRAME_PAL,
                               STANDARD_WINDOW_SYSTEM, HEAP_ID_FIELD2);
    t->menu = Menu_MakeYesNoChoice(fs->bgConfig, &sYesNoWindow,
                                   MENU_FRAME_TILE, MENU_FRAME_PAL,
                                   HEAP_ID_FIELD2);
    return t->menu != NULL;
}

/* The interpreter, ov02_0224F8FC's switch with the source's own sub-states.
 * Returns 1 when the whole talk is over. */
static int talk_run(FieldSystem *fs, Talk *t, const TalkPackage *pkg)
{
    int species = mmo_species_port_wire_id(
        (int)Pokemon_GetValue(t->mon, MON_DATA_SPECIES, NULL));
    int form = (int)Pokemon_GetValue(t->mon, MON_DATA_FORM, NULL);

    if (t->step >= t->react.steps) {
        return 1;
    }

    switch (t->sub) {
    case 0:
        if (STEP_ANIM(&t->react, t->step) != 0) {
            unsigned member = (unsigned)(pkg->anim_base
                                         + STEP_ANIM(&t->react, t->step) - 1);

            MapObject_SetPauseMovementOff(t->follower);
            if (pc_modfs_member_read(TALK_PATH, member, t->anim, 0,
                                     TALK_ANIM_BYTES)) {
                t->anim_frame = 0;
                t->anim_tick = 0;
                t->anim_live = 1;
                t->sub = 5;
                break;
            }
        }
        t->sub = 1;
        /* fall through: the source falls through too, so a step with no
         * animation reaches its emote on the same frame. */
    case 1:
        if (STEP_EMOTE(&t->react, t->step) != 0) {
            t->emote = openmmo_emote_show(t->follower,
                                          (int)STEP_EMOTE(&t->react, t->step));
            if (t->emote != NULL) {
                MapObject_SetPauseMovementOff(t->follower);
                printf("openmmo: the follower's emote %u\n",
                       (unsigned)STEP_EMOTE(&t->react, t->step));
                fflush(stdout);
                t->sub = 8;
                break;
            }
        }
        t->sub = 2;
        /* fall through */
    case 2:
        if (STEP_MSG(&t->react, t->step) != 0) {
            String *line = talk_line(fs, t, pkg,
                                     STEP_MSG(&t->react, t->step) - 1);

            if (line != NULL) {
                MapObject_SetPauseMovementOn(t->follower);
                talk_print(fs, t, line);
                t->sub = 6;
                break;
            }
        }
        t->sub = 3;
        /* fall through */
    case 3:
        if (STEP_DELAY(&t->react, t->step) != 0) {
            t->anim_tick = 0;
            t->sub = 7;
            break;
        }
        t->sub = 4;
        /* fall through */
    case 4:
        t->step++;
        t->sub = 0;
        break;

    case 5:
        if (talk_anim_frame(t, STEP_SOUND(&t->react, t->step), species, form)) {
            t->anim_live = 0;
            t->sub = 1;
        }
        break;

    case 6:
        if (FieldMessage_FinishedPrinting(t->printer) != TRUE) {
            break;
        }
        /* The last line of a reaction that is about to ask a question, or hand
         * something over, keeps its window: the question goes under it. Every
         * other line waits for a press. */
        if (t->step + 1 < t->react.steps
            || !(REACT_ASKS(&t->react) || REACT_ACCESSORY(&t->react)
                 || REACT_LEAF(&t->react))) {
            if (!(gSystem.pressedKeys & (PAD_BUTTON_A | PAD_BUTTON_B))) {
                break;
            }
            talk_close_window(t);
        }
        t->sub = 3;
        break;

    case 7:
        t->anim_tick++;
        if (t->anim_tick >= STEP_DELAY(&t->react, t->step)) {
            t->step++;
            t->sub = 0;
        }
        break;

    /*
     * The bubble, and the line waits for it. HeartGold's own emote is a task the interaction
     * Calls (`ov01_02203AB4` is a TaskManager_Call), so the parent does not go on until it is
     * over; this is that, on a field task that cannot make a child of one.
     */
    case 8:
        if (openmmo_emote_done(t->emote)) {
            openmmo_emote_finish(t->emote);
            t->emote = NULL;
            t->sub = 2;
        }
        break;
    }
    return 0;
}

static BOOL talk_task(FieldTask *task)
{
    FieldSystem *fs = FieldTask_GetFieldSystem(task);
    Talk *t = FieldTask_GetEnv(task);
    const TalkPackage *pkg = talk_package();
    u32 result;

    if (pkg == NULL || t->follower == NULL || t->mon == NULL) {
        talk_free(t);
        return TRUE;
    }

    switch (task->state) {
    case 0:
        if (talk_run(fs, t, pkg)) {
            task->state = 1;
        }
        break;

    case 1:
        /* ov02_02250504, then whichever tail the reaction has. */
        talk_finish_deltas(t);
        if (REACT_ASKS(&t->react)) {
            if (!start_yesno(fs, t)) {
                printf("openmmo: the follower's yes/no would not allocate\n");
                fflush(stdout);
                task->state = 4;
                break;
            }
            task->state = 2;
            break;
        }
        talk_reward(fs, t);
        task->state = 4;
        break;

    case 2:
        result = Menu_ProcessInput(t->menu);
        if (result == (u32)MENU_NOTHING_CHOSEN) {
            break;
        }
        t->menu = NULL;
        talk_close_window(t);
        {
            /* 0 is yes in the engine's own yes/no. HeartGold reads its answer
             * the same way round: answer 0 takes the first follow-up. */
            int next = (result == 0) ? REACT_ANSWER_NO(&t->react)
                                     : REACT_ANSWER_YES(&t->react);

            if (next == 0 || !talk_begin(pkg, t, next)) {
                task->state = 4;
                break;
            }
        }
        task->state = 3;
        break;

    case 3:
        if (talk_run(fs, t, pkg)) {
            talk_finish_deltas(t);
            talk_reward(fs, t);
            task->state = 4;
        }
        break;

    case 4:
        talk_free(t);
        return TRUE;
    }
    return FALSE;
}

/* ------------------------------------------------------------------ *
 *  The press
 * ------------------------------------------------------------------ */

MapObject *openmmo_follow_object(FieldSystem *fs);

/* An A press facing the follower. 1 when the talk took it. */
int openmmo_follow_talk_try(FieldSystem *fs);

int openmmo_follow_talk_try(FieldSystem *fs)
{
    const TalkPackage *pkg;
    MapObject *follower;
    MapObject *player;
    Pokemon *mon;
    Talk *t;
    TalkCtx ctx;
    int px, pz, dir, rid, header, mapsec;

    if (fs == NULL || fs->mapObjMan == NULL || s_live != NULL) {
        return 0;
    }
    if (fs->task != NULL || !FieldSystem_IsRunningFieldMap(fs)
        || FieldSystem_HasChildProcess(fs)) {
        return 0;
    }
    follower = openmmo_follow_object(fs);
    if (follower == NULL) {
        return 0;
    }
    player = MapObjectMan_GetPlayerMapObject(fs->mapObjMan);
    if (player == NULL) {
        return 0;
    }
    px = MapObject_GetX(player);
    pz = MapObject_GetZ(player);
    dir = MapObject_GetFacingDir(player);
    if (MapObject_GetX(follower) != px + MapObject_GetDxFromDir(dir)
        || MapObject_GetZ(follower) != pz + MapObject_GetDzFromDir(dir)) {
        /* Said, because "the press did not reach here" and "the press reached
         * here and the Pokemon was somewhere else" are the same silence
         * otherwise, and a headless drive can only see the difference in a
         * line. OPENMMO_INTERACT_REPORT is the same switch the peer path uses. */
        /* Plain getenv, like openmmo_boot.c's own read of the same name.
         * It is a diagnostic and not a door, it opens no trap a release must
         * refuse, and tests/settings_test.sh holds every variable to being
         * one or the other, never both. */
        if (getenv("OPENMMO_INTERACT_REPORT") != NULL) {
            printf("openmmo: A at (%d,%d) dir %d, the follower is at (%d,%d),"
                   " not the tile faced (%d,%d)\n", px, pz, dir,
                   MapObject_GetX(follower), MapObject_GetZ(follower),
                   px + MapObject_GetDxFromDir(dir),
                   pz + MapObject_GetDzFromDir(dir));
            fflush(stdout);
        }
        return 0;
    }

    pkg = talk_package();
    if (pkg == NULL) {
        return 0;
    }
    mon = lead_of(fs);
    if (mon == NULL) {
        return 0;
    }
    mood_register(mon);

    header = source_header(pkg, fs);
    mapsec = source_mapsec(pkg, header);
    build_ctx(pkg, fs, mon, follower, &ctx);
    rid = pick_reaction(pkg, fs, &ctx, mapsec);
    if (rid == 0) {
        /* HeartGold asserts here. A hundred rows and none of them answering is
         * a table problem, not a player problem, so it is said and the press
         * is simply not taken. */
        printf("openmmo: the follower has nothing to say (map %d section %d,"
               " mood %d, friendship %u)\n", header, mapsec, (int)ctx.mood,
               (unsigned)ctx.friendship);
        fflush(stdout);
        return 0;
    }

    t = Heap_Alloc(HEAP_ID_FIELD2, sizeof(Talk));
    if (t == NULL) {
        return 0;
    }
    memset(t, 0, sizeof *t);
    t->mon = mon;
    t->follower = follower;
    t->man = fs->mapObjMan;
    if (!talk_begin(pkg, t, rid)) {
        Heap_Free(t);
        return 0;
    }

    /* LockAll, then FollowMonFacePlayer, the two commands the source's own
     * script runs before the interaction (scr_seq_0163). North/south and
     * west/east are the pairs, so the opposite of a direction is its low bit
     * flipped. */
    MapObjectMan_PauseAllMovement(fs->mapObjMan);
    LocalMapObj_SetAnimationCode(
        follower, (enum MovementAction)(MOVEMENT_ACTION_FACE_NORTH + (dir ^ 1)));

    s_live = t;
    FieldSystem_CreateTask(fs, talk_task, t);
    return 1;
}
