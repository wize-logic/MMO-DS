/* The save blocks nobody was keeping. */

#include <stdio.h>
#include <string.h>

#include "constants/savedata/save_table.h"

#include "field/field_system.h"

#include "field_system.h"
#include "game_options.h"
#include "sound.h"
#include "trainer_info.h"
#include "save_player.h"
#include "savedata.h"

#include "../../../include/client.h"

/* Whether the device is switched on is the story's answer,
 * and it has to be asked again after this file writes the Poketch block. */
extern void openmmo_poketch_seat(SaveData *save, const openmmo_script_state *st);

/* The rival's name, when the block that just landed carries none. */
extern void openmmo_rival_name_default(SaveData *save);

/*
 * The in-game Settings screen: text speed, sound mode, battle scene, battle style and the
 * frame.
 */
#define OPENMMO_SAVE_BLOCK_OPTIONS 0xC0

/*
 * The trainer id, which is the visible IDNo in its low half and the secret id in its high
 * half. Also part of player, and here for the same reason as the options, but with one thing
 * of its own.
 */
#define OPENMMO_SAVE_BLOCK_TRAINER_ID 0xC1

typedef char openmmo_save_blocks_own_ids[
    (OPENMMO_SAVE_BLOCK_OPTIONS >= SAVE_TABLE_ENTRY_MAX &&
     OPENMMO_SAVE_BLOCK_TRAINER_ID >= SAVE_TABLE_ENTRY_MAX) ? 1 : -1];

/*
 * Not a save block, and it rides here because the wire has no op left of its own: the world's
 * date and time, stated by the server on every join.
 */
#define OPENMMO_SAVE_BLOCK_WORLD_CLOCK 0xC2

/*
 * Whether what this client starts a block at is something the server cannot work out for
 * itself, and so has to reach it on the first join that has one rather than waiting for a
 * change.
 */
static int block_is_minted(int id)
{
    return id == OPENMMO_SAVE_BLOCK_TRAINER_ID || id == SAVE_TABLE_ENTRY_MISC;
}

/*
 * A report pass comes round every REPORT_INTERVAL frames, half a second, and for most of these
 * blocks that is the right clock: they change because the player did something, and the change
 * should reach the server while the thing that caused it is still on the screen.
 */
#define HOLD_NONE  0
#define HOLD_STEPS 30 /* ~15 s: a block a single footstep writes */

typedef struct {
    int id;
    int hold;
} kept_block;

/* The blocks this file keeps. Order is the report's order and nothing depends
 * on it. Adding one here is the whole change: the wire, the store and the seat
 * are all keyed by the engine's own SaveTableEntryID. */
static const kept_block kBlocks[] = {
    { SAVE_TABLE_ENTRY_IMAGE_CLIPS, HOLD_NONE },
    { SAVE_TABLE_ENTRY_POFFINS, HOLD_NONE },
    { SAVE_TABLE_ENTRY_SEAL_CASE, HOLD_NONE },
    { SAVE_TABLE_ENTRY_LINK_CONTEST_RECORDS, HOLD_NONE },
    { SAVE_TABLE_ENTRY_POKEDEX, HOLD_NONE },
    /*
     * The Poketch, which is a settings struct with a registry of which apps the player has
     * been given.
     */
    { SAVE_TABLE_ENTRY_POKETCH, HOLD_NONE },
    /*
     * The day care, which is the block the boarders themselves live in: the copy of each one
     * the building took in, the steps it has walked them for and the bill so far.
     */
    { SAVE_TABLE_ENTRY_DAYCARE, HOLD_STEPS },
    /*
     * Everything the world puts in a player's way that is not a wild table: the two roamers
     * and where each of them is standing, the honey trees and how long each has left to shake,
     * the day's Great Marsh and swarm rolls, the trophy garden pair, the Poke Radar chain, and
     * the repel still counting down.
     */
    { SAVE_TABLE_ENTRY_ENCOUNTERS, HOLD_STEPS },
    /*
     * The adventure journal: ten dated pages of where the player went and what they did there.
     */
    { SAVE_TABLE_ENTRY_JOURNAL, HOLD_NONE },
    /*
     * The record book, and the one thing on this list a player reads without opening it: the
     * trainer card takes its score from here, and the counts behind the villa's furniture and
     * the ranking boards are the same rows.
     */
    { SAVE_TABLE_ENTRY_GAME_RECORDS, HOLD_STEPS },
    /* The Battle Frontier: the current and best streak at each of the five
     * buildings, in both modes, with the print and symbol they earned. The
     * whole point of a frontier run is the streak, and starting from zero on
     * every join is the same as never having played one. */
    { SAVE_TABLE_ENTRY_FRONTIER, HOLD_NONE },
    /*
     * The Underground, which is one block for everything earned down there: the spheres and
     * their sizes, the treasure, the goods in the bag and in the storage PC, the traps the
     * player owns and the ones already set on the floor, the secret base with its furniture
     * and boulders, where each buried thing is, and the counters the records screen reads.
     */
    { SAVE_TABLE_ENTRY_UNDERGROUND, HOLD_STEPS },
    /* Everything the engine files under MISC. */
    { SAVE_TABLE_ENTRY_MISC, HOLD_NONE },
    { OPENMMO_SAVE_BLOCK_OPTIONS, HOLD_NONE },
    { OPENMMO_SAVE_BLOCK_TRAINER_ID, HOLD_NONE },
};

#define BLOCK_COUNT ((int)(sizeof kBlocks / sizeof kBlocks[0]))

/*
 * One frame may carry every block this file keeps and the world clock, which is not a block
 * but rides the seat as one (game.h, MMO_SAVE_BLOCK_MAX), so the room asked for here is one
 * more than the count.
 */
typedef char openmmo_save_blocks_fit[
    (BLOCK_COUNT + 1 <= MMO_SAVE_BLOCK_MAX) ? 1 : -1];

static u8 s_shadow[BLOCK_COUNT][MMO_SAVE_BLOCK_BYTES];
static int s_len[BLOCK_COUNT];   /* this build's size for the block, once known */
static int s_wide[BLOCK_COUNT];  /* the block is past the wire's width; skipped */
static int s_owed[BLOCK_COUNT];  /* minted here; the server has yet to hear it */
static int s_hold[BLOCK_COUNT];  /* passes still to sit out after a report */
static int s_watching;
static unsigned s_applied;       /* the seat generation already written */
static int s_reported;

/* Whether the day care may find an egg. It may not, in a session. */
int openmmo_daycare_may_conceive(void)
{
    extern int openmmo_session_configured(void);
    static int said;

    if (!openmmo_session_configured())
        return 1;
    if (!said) {
        printf("openmmo: the day care will not find an egg, the wire has no"
               " way to say a monster has become one\n");
        said = 1;
    }
    return 0;
}

void openmmo_save_blocks_reset(void)
{
    memset(s_shadow, 0, sizeof s_shadow);
    memset(s_len, 0, sizeof s_len);
    memset(s_wide, 0, sizeof s_wide);
    memset(s_owed, 0, sizeof s_owed);
    memset(s_hold, 0, sizeof s_hold);
    s_watching = 0;
    s_applied = 0;
    s_reported = 0;
}

/* Where one block lives in this save, and how wide this build makes it. Every
 * id on the list but one is a save table entry answered by the engine's own
 * table; the options are the part of player this client keeps, and the engine
 * hands those out by themselves. */
static u8 *block_body(SaveData *save, int id, int *out_len)
{
    int len;
    u8 *at;

    if (id == OPENMMO_SAVE_BLOCK_OPTIONS) {
        len = (int)sizeof(Options);
        at = (u8 *)SaveData_GetOptions(save);
    } else if (id == OPENMMO_SAVE_BLOCK_TRAINER_ID) {
        TrainerInfo *info = SaveData_GetTrainerInfo(save);

        len = (int)sizeof(u32);
        at = (info != NULL) ? (u8 *)&info->id : NULL;
    } else {
        len = SaveTableEntry_BodySize(id);
        at = SaveData_SaveTable(save, id);
    }
    if (out_len != NULL)
        *out_len = len;
    return at;
}

/* This build's size for a block, and the pointer to it. Returns NULL for a
 * block the save has no room for or one wider than the wire carries, the
 * second is said once and then remembered, because it would otherwise be said
 * on every frame for the whole session. */
static u8 *block_at(SaveData *save, int i, int *out_len)
{
    void *at;
    int len = 0;

    if (s_wide[i])
        return NULL;
    at = block_body(save, kBlocks[i].id, &len);
    if (len <= 0)
        return NULL;
    if (len > MMO_SAVE_BLOCK_BYTES) {
        printf("openmmo: save block %d is %d bytes, past the wire's %d, "
               " not kept\n", kBlocks[i].id, len, MMO_SAVE_BLOCK_BYTES);
        s_wide[i] = 1;
        return NULL;
    }
    if (at == NULL)
        return NULL;
    s_len[i] = len;
    if (out_len != NULL)
        *out_len = len;
    return at;
}

/* Write the server's seat into the blocks, then take the shadow to be what
 * stands afterwards, including for a block the server did not send, whose
 * engine defaults are then the baseline and are reported the first time the
 * player changes one. */
static void seat_apply(SaveData *save, const openmmo_script_state *st)
{
    int seated = 0;
    int options_seated = 0;
    int i, k;

    /* The world's clock first: it is not one of the blocks below and the
     * engine's RTC cache refreshes within ten frames of it landing, which is
     * before the map this seat arrives ahead of is ever lit. */
    for (k = 0; k < st->block_count; k++) {
        if (st->block[k].id == OPENMMO_SAVE_BLOCK_WORLD_CLOCK) {
            extern void openmmo_world_clock_seat(const u8 *b, int len);

            openmmo_world_clock_seat(st->block[k].data, st->block[k].len);
            break;
        }
    }

    for (i = 0; i < BLOCK_COUNT; i++) {
        int len = 0;
        u8 *at = block_at(save, i, &len);

        if (at == NULL)
            continue;
        s_owed[i] = block_is_minted(kBlocks[i].id);
        for (k = 0; k < st->block_count; k++) {
            if (st->block[k].id != kBlocks[i].id)
                continue;
            /* A block the server holds at a different width is a record from
             * another build. Half a struct written over a live save is worse
             * than none, so it is refused and named. */
            if (st->block[k].len != len) {
                printf("openmmo: save block %d seated at %d bytes, this build"
                       " holds %d, refused\n",
                       kBlocks[i].id, st->block[k].len, len);
                break;
            }
            memcpy(at, st->block[k].data, (size_t)len);
            s_owed[i] = 0;
            if (kBlocks[i].id == OPENMMO_SAVE_BLOCK_OPTIONS)
                options_seated = 1;
            seated++;
            break;
        }
        memcpy(s_shadow[i], at, (size_t)len);
        s_hold[i] = 0;
    }

    /* The one setting in the options block that is not re-read where it is used. */
    if (options_seated)
        Sound_SetPlaybackMode(Options_SoundMode(SaveData_GetOptions(save)));

    /*
     * The device is the story's, not the block's. A character who has been handed the Poketch
     * but whose stored block predates the gift would otherwise have it switched off again a
     * frame into the field, by a block written before the president said a word.
     */
    openmmo_poketch_seat(save, st);

    /* Likewise the rival, whose name is in the block this loop just wrote. */
    openmmo_rival_name_default(save);

    if (st->blocks_dropped)
        printf("openmmo: %d seated save block(s) past this client's caps were"
               " dropped\n", st->blocks_dropped);
    /* The world clock rode the same list and is not one of these, so it is
     * not counted as a block that failed to seat. */
    for (k = 0, i = st->block_count; k < st->block_count; k++) {
        if (st->block[k].id == OPENMMO_SAVE_BLOCK_WORLD_CLOCK)
            i--;
    }
    if (seated || i)
        printf("openmmo: %d save block(s) seated of %d sent\n", seated, i);
    s_applied = st->seq;
    s_watching = 1;
}

/* Every block this client keeps, whole, with no shadow and no hold. */
int openmmo_save_blocks_all(SaveData *save, mmo_save_block *out, int cap)
{
    int n = 0;
    int i;

    if (save == NULL || out == NULL)
        return 0;
    for (i = 0; i < BLOCK_COUNT && n < cap; i++) {
        int len = 0;
        const u8 *at = block_at(save, i, &len);

        if (at == NULL)
            continue;
        out[n].id = kBlocks[i].id;
        out[n].len = len;
        out[n].data = at;
        n++;
    }
    return n;
}

/* Every hold, dropped. */
void openmmo_save_blocks_release_holds(void)
{
    memset(s_hold, 0, sizeof s_hold);
}

/* Which blocks the last collect queued, so a send that succeeded can advance
 * exactly those shadows and one that failed can advance none. */
static int s_queued[BLOCK_COUNT];
static int s_queued_n;

/*
 * One report pass. Called from openmmo_script.c's own tick, on the same countdown and for the
 * same reason: this is the other half of what that op carries, and a second clock over the
 * same packet would only mean two packets where one does.
 */
int openmmo_save_blocks_collect(void *fieldSystemVoid, openmmo_client *c,
                                mmo_save_block *out, int cap)
{
    FieldSystem *fs = fieldSystemVoid;
    const openmmo_script_state *st;
    SaveData *save;
    int n = 0;
    int i;

    s_queued_n = 0;
    if (c == NULL || fs == NULL || fs->saveData == NULL || out == NULL)
        return 0;
    save = fs->saveData;

    /* The seat shares the script state's generation, because it arrives in the
     * same packet: one frame cannot have seated one half and not the other. */
    st = openmmo_client_script_state(c);
    if (st != NULL && st->seated && st->seq != s_applied) {
        seat_apply(save, st);
        return 0;
    }
    if (!s_watching)
        return 0;

    /* Every hold runs down on its own pass, not inside the loop below: that
     * one stops at the wire's cap, and a hold that stopped with it would be
     * held for longer the busier the report was. */
    for (i = 0; i < BLOCK_COUNT; i++) {
        if (s_hold[i] > 0)
            s_hold[i]--;
    }

    for (i = 0; i < BLOCK_COUNT && n < cap; i++) {
        int len = 0;
        const u8 *at;

        if (s_hold[i] > 0)
            continue;
        at = block_at(save, i, &len);
        if (at == NULL)
            continue;
        if (!s_owed[i] && memcmp(at, s_shadow[i], (size_t)len) == 0)
            continue;
        out[n].id = kBlocks[i].id;
        out[n].len = len;
        out[n].data = at;
        s_queued[n] = i;
        n++;
    }
    s_queued_n = n;
    return n;
}

/* The frame went. Take the shadow to be what was sent, and say so once per
 * block so a report that should not have happened can be traced to a block
 * without a second build. */
void openmmo_save_blocks_commit(void *fieldSystemVoid)
{
    FieldSystem *fs = fieldSystemVoid;
    int k;

    if (fs == NULL || fs->saveData == NULL)
        return;
    for (k = 0; k < s_queued_n; k++) {
        int i = s_queued[k];
        int len = s_len[i];
        const u8 *at = block_body(fs->saveData, kBlocks[i].id, NULL);

        if (at == NULL || len <= 0)
            continue;
        memcpy(s_shadow[i], at, (size_t)len);
        s_owed[i] = 0;
        s_hold[i] = kBlocks[i].hold;
        s_reported++;
        printf("openmmo: save block %d reported (%d bytes, %d this session)\n",
               kBlocks[i].id, len, s_reported);
    }
    s_queued_n = 0;
}
