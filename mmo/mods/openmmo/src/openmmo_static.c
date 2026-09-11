/*
 * A scripted wild fight's person, taken off the map once the fight is won.
 */

#include <stdio.h>

#include "constants/battle.h"
#include "encounter.h"
#include "field/field_system.h"
#include "field_script_context.h"
#include "field_system.h"
#include "map_object.h"
#include "player_avatar.h"
#include "savedata.h"
#include "script_manager.h"
#include "vars_flags.h"

/* The site faced when the last held fight began: its band flag and its local
 * id, or -1 while no static site is in a fight. */
static int s_site_flag = -1;
static int s_site_localid = -1;

/* A fight one of this game's own scripts staged and the server is dealing. */
enum {
    SCRIPT_SITE_IDLE = 0,
    SCRIPT_SITE_ASKED,
    SCRIPT_SITE_READY,
    SCRIPT_SITE_LATE,
};

/* Frames the paused script waits for the server. */
#define SCRIPT_SITE_FRAMES 180

/* How long after the scene gave up an answer is still recognised as the scene's. */
#define SCRIPT_LATE_FRAMES 3600

/* pc/src/pc_video.c's VBlank counter: the one clock a paused scene and a
 * late answer share. */
extern unsigned long long pc_irq_frames(void);

static int s_script_state;
static int s_script_frames;
static int s_script_foe;
static int s_script_lv;
/* The fight the scene staged itself after the wait, and when. */
static int s_late_foe;
static int s_late_lv;
static unsigned long long s_late_frame;
/* Live only while the script's fight is up: the engine's own result mask for
 * it, which is where the outcome comes from when this file did not build the
 * transfer the wild path reads. */
static const int *s_script_result;

static int in_static_band(u32 flag)
{
    return flag >= OPENMMO_PORTED_STATIC_FLAGS_START
        && flag < OPENMMO_PORTED_STATIC_FLAGS_START + OPENMMO_PORTED_STATIC_FLAGS_MAX;
}

/* The object on the tile in front of the player, if any. */
static MapObject *faced_object(FieldSystem *fs)
{
    MapObject *obj = NULL;
    MapObject *self;
    int idx = 0, x, z;

    if (fs == NULL || fs->mapObjMan == NULL || fs->playerAvatar == NULL)
        return NULL;
    self = PlayerAvatar_GetMapObject(fs->playerAvatar);
    x = PlayerAvatar_GetXPos(fs->playerAvatar);
    z = PlayerAvatar_GetZPos(fs->playerAvatar);
    switch (PlayerAvatar_GetFacingDir(fs->playerAvatar)) {
    case 0: z--; break;
    case 1: z++; break;
    case 2: x--; break;
    case 3: x++; break;
    default: break;
    }
    while (MapObjectMan_FindObjectWithStatus(fs->mapObjMan, &obj, &idx,
                                             MAP_OBJ_STATUS_0) == TRUE) {
        if (obj == self)
            continue;
        if (MapObject_GetX(obj) == x && MapObject_GetZ(obj) == z)
            return obj;
    }
    return NULL;
}

/*
 * The A press on a ported map, asked before the engine runs the person's own scene: a person
 * whose flag is in the static band is a fight the server deals, so the press crosses (the
 * engine's scene, the folded lead-up, still plays; the fight arrives held and starts once the
 * field is free).
 */
int openmmo_static_press_try(FieldSystem *fs)
{
    extern int openmmo_static_press_send(void); /* openmmo_boot.c */
    MapObject *obj = faced_object(fs);
    u32 flag;

    if (obj == NULL)
        return 0;
    flag = MapObject_GetFlag(obj);
    if (!in_static_band(flag))
        return 0;
    if (!openmmo_static_press_send())
        return 0;
    printf("openmmo: static site: the press at flag %u (object %u) goes to the "
           "server\n", flag, MapObject_GetLocalID(obj));
    return 1;
}

/* A held fight is starting: remember the static site the player faces, if
 * that is what they are facing. */
void openmmo_static_battle_started(FieldSystem *fs)
{
    MapObject *obj = faced_object(fs);
    u32 flag;

    s_site_flag = -1;
    s_site_localid = -1;
    if (obj == NULL)
        return;
    flag = MapObject_GetFlag(obj);
    if (!in_static_band(flag))
        return;
    s_site_flag = (int)flag;
    s_site_localid = (int)MapObject_GetLocalID(obj);
    printf("openmmo: static site: the fight at flag %u (object %d) begins\n",
           flag, s_site_localid);
}

/* The server is told a site's fight was won here. Its own instance of a wild
 * fight only ever hears run at the end of one, the fight is fought on this
 * engine, so the win goes before that run and the server hides the site for
 * this character. */
static void tell_the_server_it_was_won(void)
{
    extern int openmmo_static_won_send(void); /* openmmo_boot.c */

    openmmo_static_won_send();
}

/* The held fight ended with the engine's result mask. Won, or the Pokemon
 * caught, takes the person off the map for good; anything else leaves them. */
void openmmo_static_battle_ended(FieldSystem *fs, u32 result)
{
    MapObject *obj;
    int flag = s_site_flag, localid = s_site_localid;
    int scripted = s_script_result != NULL;

    s_site_flag = -1;
    s_site_localid = -1;
    s_script_result = NULL;
    /* One of this game's own sites. The scene that staged it is still running
     * and does its own hiding, the RemoveObject and the flag are the next
     * commands after the fight, so the only thing owed here is the word to
     * the server. */
    if (scripted) {
        if (result == BATTLE_RESULT_WIN || result == BATTLE_RESULT_CAPTURED_MON) {
            tell_the_server_it_was_won();
            printf("openmmo: static site: the script's fight is %s\n",
                   result == BATTLE_RESULT_WIN ? "won" : "a catch");
        } else {
            printf("openmmo: static site: the script's fight ended %u; the "
                   "Pokemon stays\n", result);
        }
        return;
    }
    if (flag < 0 || fs == NULL)
        return;
    if (result != BATTLE_RESULT_WIN && result != BATTLE_RESULT_CAPTURED_MON) {
        printf("openmmo: static site: the fight at flag %d ended %u; the "
               "Pokemon stays\n", flag, result);
        return;
    }
    obj = (fs->mapObjMan != NULL && localid >= 0)
        ? MapObjMan_LocalMapObjByIndex(fs->mapObjMan, localid) : NULL;
    if (obj != NULL && (int)MapObject_GetFlag(obj) == flag) {
        MapObject_SetFlagAndDeleteObject(obj);
    } else if (fs->saveData != NULL) {
        VarsFlags_SetFlag(SaveData_GetVarsFlags(fs->saveData), (u16)flag);
    }
    tell_the_server_it_was_won();
    printf("openmmo: static site: the fight at flag %d is %s; the Pokemon is "
           "gone\n", flag, result == BATTLE_RESULT_WIN ? "won" : "a catch");
}

/*
 * ------------------------------------------------------------------ This game's own sites:
 * the four script commands that stage a fight.
 * ------------------------------------------------------------------
 */

/*
 * The command was reached. Ask the server to deal the fight, by pressing the tile the player
 * is facing, which is the site, because that press is what started this scene.
 */
static int openmmo_static_script_ask(void)
{
    extern int openmmo_static_press_send(void); /* openmmo_boot.c */

    /* A new site's ask supersedes a late one still waiting on its answer. */
    if (s_script_state == SCRIPT_SITE_ASKED || s_script_state == SCRIPT_SITE_READY)
        return 0;
    if (!openmmo_static_press_send())
        return 0;
    s_script_state = SCRIPT_SITE_ASKED;
    s_script_frames = 0;
    s_script_foe = 0;
    s_script_lv = 0;
    printf("openmmo: static site: the script's fight is the server's to deal\n");
    return 1;
}

/*
 * openmmo_boot.c, on the server's battle-open: a script waiting for one takes it here rather
 * than from the field, because the script's own field task is what has to start it. Returns 1
 * when taken.
 */
int openmmo_static_take_battle(int foe_species, int foe_level)
{
    if (s_script_state == SCRIPT_SITE_LATE) {
        if (pc_irq_frames() - s_late_frame > (unsigned long long)SCRIPT_LATE_FRAMES) {
            s_script_state = SCRIPT_SITE_IDLE;
            return 0;
        }
        if (foe_species != s_late_foe || foe_level != s_late_lv)
            return 0;
        s_script_state = SCRIPT_SITE_IDLE;
        printf("openmmo: static site: the server's fight (species %d at level"
               " %d) arrived after the scene staged its own; %s\n",
               foe_species, foe_level,
               s_script_result != NULL ? "the scene answers for it"
                                       : "the scene is over, so it is run");
        return s_script_result != NULL ? 1 : 2;
    }
    if (s_script_state != SCRIPT_SITE_ASKED)
        return 0;
    s_script_foe = foe_species;
    s_script_lv = foe_level;
    s_script_state = SCRIPT_SITE_READY;
    printf("openmmo: static site: the server's fight is species %d at level %d;"
           " the scene starts it\n", foe_species, foe_level);
    return 1;
}

/* The paused script, once a frame: 1 with the server's fight in hand, 0 still
 * waiting, -1 given up and the engine's own fight is the answer. */
static int openmmo_static_script_ready(int *species, int *level)
{
    if (s_script_state == SCRIPT_SITE_READY) {
        if (species != NULL)
            *species = s_script_foe;
        if (level != NULL)
            *level = s_script_lv;
        return 1;
    }
    if (s_script_state != SCRIPT_SITE_ASKED)
        return -1;
    if (++s_script_frames <= SCRIPT_SITE_FRAMES)
        return 0;
    /* Not back to IDLE, or an answer that is only slow would be fought twice;
     * LATE, which a new site's ask supersedes, so one site the server could not
     * resolve leaves no ask stuck open for every site after it. */
    s_script_state = SCRIPT_SITE_LATE;
    s_script_frames = 0;
    s_late_frame = pc_irq_frames();
    printf("openmmo: static site: no fight from the server in %d frames; the "
           "scene stages its own\n", SCRIPT_SITE_FRAMES);
    return -1;
}

/*
 * The script is starting the fight now, on its own task, whether the server dealt it or the
 * wait ran out and the scene named it.
 */
static void openmmo_static_script_took(int *result, int species, int level)
{
    extern void openmmo_encounter_mark_started(void); /* openmmo_encounter.c */

    if (s_script_state == SCRIPT_SITE_LATE) {
        s_late_foe = species;
        s_late_lv = level;
    } else {
        s_script_state = SCRIPT_SITE_IDLE;
    }
    s_script_frames = 0;
    s_site_flag = -1;
    s_site_localid = -1;
    s_script_result = result;
    openmmo_encounter_mark_started();
}

/* The running script fight's result mask, or BATTLE_IN_PROGRESS when there is
 * no such fight. Read while the battle is up, because the script manager that
 * holds the mask goes away with the scene. */
int openmmo_static_script_result(void)
{
    return s_script_result != NULL ? *s_script_result : BATTLE_IN_PROGRESS;
}

/*
 * The four commands themselves. Each is the smallest thing scrcmd.c can hold, a declaration
 * and a call, and this is what they call.
 */
enum {
    STATIC_KIND_WILD = 0,
    STATIC_KIND_LEGENDARY,
    STATIC_KIND_GIRATINA_ORIGIN,
    STATIC_KIND_FATEFUL,
};

static void script_battle_start(ScriptContext *ctx, u16 species, u8 level, int *resultMaskPtr)
{
    switch (ctx->data[0]) {
    case STATIC_KIND_GIRATINA_ORIGIN:
        Encounter_NewVsGiratinaOrigin(ctx->task, species, level, resultMaskPtr, TRUE);
        break;
    case STATIC_KIND_FATEFUL:
        Encounter_NewFatefulVsSpeciesAtLevel(ctx->task, species, level, resultMaskPtr, TRUE);
        break;
    case STATIC_KIND_LEGENDARY:
        Encounter_NewVsSpeciesAtLevel(ctx->task, species, level, resultMaskPtr, TRUE);
        break;
    default:
        Encounter_NewVsSpeciesAtLevel(ctx->task, species, level, resultMaskPtr, FALSE);
        break;
    }
}

static BOOL script_battle_poll(ScriptContext *ctx)
{
    int species = (int)ctx->data[1];
    int level = (int)ctx->data[2];
    int *battleResultMaskPtr = FieldSystem_GetScriptMemberPtr(ctx->fieldSystem, SCRIPT_MANAGER_BATTLE_RESULT);
    int got = openmmo_static_script_ready(&species, &level);

    if (got == 0) {
        return FALSE;
    }
    if (got < 0) {
        /* Nothing came. The species and level the script itself names are what
         * this cartridge would have fought, so fight that rather than leave a
         * player standing in front of a legendary for ever. */
        species = (int)ctx->data[1];
        level = (int)ctx->data[2];
    }
    /* Either way this is the scene's fight, and it is tracked as one: the
     * outcome comes off the mask, the word goes to the server, and an answer
     * that lands after the wait ran out is matched to it rather than fought a
     * second time. */
    openmmo_static_script_took(battleResultMaskPtr, species, level);

    script_battle_start(ctx, (u16)species, (u8)level, battleResultMaskPtr);
    return TRUE;
}

/* TRUE when the server was asked, so the command waits for it. */
static BOOL script_battle_take(ScriptContext *ctx, u16 species, u8 level, int kind)
{
    if (!openmmo_static_script_ask()) {
        return FALSE;
    }

    ctx->data[0] = (u32)kind;
    ctx->data[1] = species;
    ctx->data[2] = level;
    ScriptContext_Pause(ctx, script_battle_poll);
    return TRUE;
}

BOOL openmmo_static_battle_take_wild(ScriptContext *ctx, u16 species, u8 level)
{
    return script_battle_take(ctx, species, level, STATIC_KIND_WILD);
}

BOOL openmmo_static_battle_take_legendary(ScriptContext *ctx, u16 species, u8 level)
{
    return script_battle_take(ctx, species, level, STATIC_KIND_LEGENDARY);
}

BOOL openmmo_static_battle_take_giratina_origin(ScriptContext *ctx, u16 species, u8 level)
{
    return script_battle_take(ctx, species, level, STATIC_KIND_GIRATINA_ORIGIN);
}

BOOL openmmo_static_battle_take_fateful(ScriptContext *ctx, u16 species, u8 level)
{
    return script_battle_take(ctx, species, level, STATIC_KIND_FATEFUL);
}
