/* The rod's roll is the server's; the minigame stays the engine's. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "constants/heap.h"
#include "field/field_system.h"
#include "field_battle_data_transfer.h"
#include "field_system.h"
#include "field_task.h"
#include "overlay005/fishing.h"

#include "../../../include/endpoint.h"
#include "../../../include/client.h"

extern int openmmo_encounter_build_wild(FieldSystem *fs, const openmmo_client *c,
                                        int foe_species, int foe_level,
                                        FieldBattleDTO **out); /* openmmo_encounter.c */
extern int openmmo_underground_active(void);                    /* openmmo_underground.c */

/* Frames the field task waits on the server, at whatever rate the client
 * runs. A verdict is one round trip; the battle behind a reel-in is a round
 * trip and the server's own battle setup. Past these the cast is a nibble
 * that never came, said out loud, rather than a player stood holding a rod. */
#define FISH_VERDICT_FRAMES 600
#define FISH_BATTLE_FRAMES  900

/* How long after the line came back empty a battle the server opens is still this cast's. */
#define FISH_LATE_FRAMES    1800

enum {
    FISH_IDLE = 0,
    FISH_CAST,     /* the request went up; waiting for the verdict */
    FISH_TOLD,     /* the verdict is in (s_bite says which); the minigame plays */
    FISH_HOOKED,   /* the reel-in landed and was reported; waiting for the battle */
    FISH_BATTLE,   /* the server opened it; the DTO is built and waiting to be taken */
    FISH_LATE,     /* the wait ran out; an open that still arrives is this cast's */
};

/* pc/src/pc_video.c's VBlank counter: the clock the field task and an answer
 * that arrives after it is gone share. */
extern unsigned long long pc_irq_frames(void);

static openmmo_client *s_client;
static int s_state;
static int s_bite;
static int s_frames;
static int s_rod;
static unsigned long long s_late_frame;
static FieldBattleDTO *s_dto;

static int session_up(void)
{
    const openmmo_world_state *ws;

    if (s_client == NULL)
        return 0;
    ws = openmmo_client_world_state(s_client);
    return ws != NULL && ws->valid;
}

/* A battle the server opened that nothing here will fight: it is run rather
 * than left standing, the way a held fight that will not start is. A send that
 * fails is said out loud and left to the overworld's own hold, which re-arms
 * on the battle's next queued event. */
static void run_it_closed(void)
{
    if (s_client == NULL)
        return;
    if (openmmo_client_battle_run(s_client) != 0)
        printf("openmmo: fishing: the battle could not be run\n");
}

static void drop_dto(void)
{
    if (s_dto != NULL) {
        FieldBattleDTO_Free(s_dto);
        s_dto = NULL;
    }
}

void openmmo_fishing_attach(openmmo_client *c)
{
    s_client = c;
    s_state = FISH_IDLE;
    s_bite = 0;
    s_frames = 0;
    drop_dto();
}

/* One of the fishing kinds arrived. The cavern's pump calls this for the
 * kinds it does not own; the overworld pump below calls it for all of them. */
void openmmo_fishing_recv(const mmo_underground_talk *msg)
{
    if (msg == NULL)
        return;
    if (msg->kind == MMO_UG_TALK_FISH_VERDICT) {
        if (s_state != FISH_CAST) {
            printf("openmmo: fishing verdict with no cast waiting\n");
            return;
        }
        s_bite = msg->len > 0 && msg->data[0] != 0;
        s_state = FISH_TOLD;
        printf("openmmo: fishing: %s\n", s_bite ? "something bit" : "not even a nibble");
        return;
    }
    printf("openmmo: fishing kind %d ignored\n", msg->kind);
}

/* The cavern drains the talk queue while the player is down there; up here
 * nobody does, so a live cast drains it itself and hands every kind on. */
static void pump(void)
{
    mmo_underground_talk msg;

    if (s_client == NULL || openmmo_underground_active())
        return;
    while (openmmo_client_ug_talk_recv(s_client, &msg)) {
        extern void openmmo_talk_route(const mmo_underground_talk *m);

        openmmo_talk_route(&msg);
    }
}

/* FieldTask_Fishing, state 0: the rod was used. With a session the cast goes
 * up and the task waits; without one the engine rolls as it always did.
 * `rodType` is the engine's enum EncounterFishingRodType, 0..2. */
int openmmo_fishing_cast(FieldSystem *fs, int rodType)
{
    u8 rod = (u8)(rodType + 1);

    (void)fs;
    if (!session_up())
        return 0;
    if (s_state == FISH_LATE) {
        /* The last cast's answer never came and this one supersedes it, so
         * one slow answer cannot leave every cast after it looking late. */
        s_state = FISH_IDLE;
    } else if (s_state != FISH_IDLE) {
        printf("openmmo: fishing cast while a cast is still live (state %d)\n",
               s_state);
        s_state = FISH_IDLE;
        drop_dto();
    }
    if (openmmo_client_ug_talk_send(s_client, MMO_UG_TALK_FISH_CAST, 0, &rod, 1) != 0) {
        printf("openmmo: fishing cast could not be sent; the engine rolls\n");
        return 0;
    }
    s_state = FISH_CAST;
    s_bite = 0;
    s_frames = 0;
    s_rod = rodType;
    printf("openmmo: fishing cast, rod %d, asking the server\n", rod);
    return 1;
}

/* The wait for the verdict: 0 still waiting, 1 a bite, -1 nothing (or the
 * server never said, which is the same thing said late). */
int openmmo_fishing_verdict(FieldSystem *fs)
{
    (void)fs;
    if (s_state == FISH_TOLD)
        return s_bite ? 1 : -1;
    if (s_state != FISH_CAST)
        return -1;
    pump();
    if (s_state == FISH_TOLD)
        return s_bite ? 1 : -1;
    if (++s_frames > FISH_VERDICT_FRAMES) {
        printf("openmmo: fishing: no verdict in %d frames; nothing bit\n",
               FISH_VERDICT_FRAMES);
        s_state = FISH_IDLE;
        return -1;
    }
    return 0;
}

/*
 * The reel-in landed. Says so, and answers 1 when the battle is the server's to open, the
 * task then waits on openmmo_fishing_battle, or 0 when this cast was the engine's own roll
 * and the engine's own DTO is the fight.
 */
int openmmo_fishing_hooked(FieldSystem *fs)
{
    (void)fs;
    if (s_state != FISH_TOLD || !s_bite)
        return 0;
    if (openmmo_client_ug_talk_send(s_client, MMO_UG_TALK_FISH_HOOKED, 0, NULL, 0) != 0) {
        printf("openmmo: fishing: the reel-in could not be reported\n");
        s_state = FISH_IDLE;
        return -1;
    }
    s_state = FISH_HOOKED;
    s_frames = 0;
    printf("openmmo: fishing: reeled in, asking for the battle\n");
    return 1;
}

/* The fish got away, or the line came in early. A server that held a roll is
 * told to drop it; a cast the engine rolled has nothing to tell. */
void openmmo_fishing_lost(FieldSystem *fs)
{
    (void)fs;
    if (s_state == FISH_TOLD && s_bite)
        openmmo_client_ug_talk_send(s_client, MMO_UG_TALK_FISH_LOST, 0, NULL, 0);
    if (s_state != FISH_IDLE)
        printf("openmmo: fishing: the line came back empty\n");
    s_state = FISH_IDLE;
    drop_dto();
}

/* openmmo_boot.c, on the server's battle-open: a reported reel-in takes the
 * battle here instead of starting it from the field, because the fishing
 * task is the field task and starts it itself. Returns 1 when taken. */
int openmmo_fishing_take_battle(FieldSystem *fs, const openmmo_client *c,
                                int foe_species, int foe_level)
{
    FieldBattleDTO *dto = NULL;

    if (s_state == FISH_LATE) {
        if (pc_irq_frames() - s_late_frame > (unsigned long long)FISH_LATE_FRAMES) {
            s_state = FISH_IDLE;
            return 0;
        }
        s_state = FISH_IDLE;
        printf("openmmo: fishing: the battle (species %d at level %d) arrived"
               " after the line came back empty; running it\n",
               foe_species, foe_level);
        run_it_closed();
        return 1;
    }
    if (s_state != FISH_HOOKED)
        return 0;
    if (openmmo_encounter_build_wild(fs, c, foe_species, foe_level, &dto) != 0) {
        /* The server is holding this one open, and the caller takes a 1 for
         * a fight that started, so nothing else would ever answer it. */
        printf("openmmo: fishing: the server's fish cannot be drawn; the line"
               " comes back empty and the battle is run\n");
        s_state = FISH_IDLE;
        run_it_closed();
        return 1;
    }
    drop_dto();
    s_dto = dto;
    s_state = FISH_BATTLE;
    return 1;
}

/* The wait for the battle behind a reported reel-in: 0 still waiting, 1 with
 * the DTO handed over (the caller owns it now), -1 given up. */
int openmmo_fishing_battle(FieldSystem *fs, FieldBattleDTO **out)
{
    (void)fs;
    if (out != NULL)
        *out = NULL;
    if (s_state == FISH_BATTLE) {
        if (out != NULL) {
            *out = s_dto;
            s_dto = NULL;
        }
        s_state = FISH_IDLE;
        return 1;
    }
    if (s_state != FISH_HOOKED)
        return -1;
    pump();
    if (s_state == FISH_BATTLE)
        return openmmo_fishing_battle(fs, out);
    if (s_state != FISH_HOOKED)
        return -1;
    if (++s_frames > FISH_BATTLE_FRAMES) {
        printf("openmmo: fishing: no battle in %d frames; the line comes back empty\n",
               FISH_BATTLE_FRAMES);
        /* Not back to IDLE: the reel-in was reported, so the battle is still
         * the server's to open and an open that lands now is this cast's. */
        s_state = FISH_LATE;
        s_late_frame = pc_irq_frames();
        return -1;
    }
    return 0;
}

int openmmo_fishing_active(void)
{
    return s_state != FISH_IDLE && s_state != FISH_LATE;
}

/*
 * OPENMMO_FISH_AT="<frame>:<rod>[:<casts>]" casts a rod from the field at that frame, which is
 * UseOldRodInField's two lines (item_use_functions.c) without the bag screen in front of them,
 * the way the growth lab uses an item.
 */
static int s_lab_parsed;
static long s_lab_frame = -1;
static int s_lab_rod;
static int s_lab_left;
static long s_frame_count;

void openmmo_fishing_tick(FieldSystem *fs, openmmo_client *c)
{
    (void)c;
    s_frame_count++;
    if (!s_lab_parsed) {
        const char *s = openmmo_dev_env("OPENMMO_FISH_AT");
        int n = 1;

        s_lab_parsed = 1;
        if (s != NULL && sscanf(s, "%ld:%d:%d", &s_lab_frame, &s_lab_rod, &n) >= 2
            && s_lab_rod >= 1 && s_lab_rod <= 3) {
            s_lab_left = n < 1 ? 1 : n;
            printf("openmmo: fishing lab: rod %d at frame %ld, %d cast(s)\n",
                   s_lab_rod, s_lab_frame, s_lab_left);
        } else {
            s_lab_frame = -1;
        }
    }
    if (s_lab_frame >= 0 && s_lab_left > 0 && s_frame_count >= s_lab_frame) {
        if (fs != NULL && fs->task == NULL && FieldSystem_IsRunningFieldMap(fs)
            && !FieldSystem_HasChildProcess(fs) && s_state == FISH_IDLE) {
            void *ctx = FishingContext_Init(fs, HEAP_ID_FIELD1, s_lab_rod - 1);

            FieldSystem_CreateTask(fs, FieldTask_Fishing, ctx);
            s_lab_left--;
            printf("openmmo: fishing lab: rod %d cast at frame %ld, %d left\n",
                   s_lab_rod, s_frame_count, s_lab_left);
            /* The next one waits for this line to come back and a breath
             * after it; a cast while the task is up would be refused anyway. */
            s_lab_frame = s_frame_count + 240;
        } else if (fs != NULL && fs->task != NULL) {
            s_lab_frame = s_frame_count + 240;
        }
    }
}
