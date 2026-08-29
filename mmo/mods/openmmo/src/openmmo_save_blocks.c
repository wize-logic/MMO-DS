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

/* Whether a block's engine default is a fresh roll rather than a fixed
 * struct, and so has to reach the server on the first join that has one. */
static int block_is_minted(int id)
{
    return id == OPENMMO_SAVE_BLOCK_TRAINER_ID;
}

/* The blocks this file keeps. Order is the report's order and nothing depends
 * on it. Adding one here is the whole change: the wire, the store and the seat
 * are all keyed by the engine's own SaveTableEntryID. */
static const int kBlocks[] = {
    SAVE_TABLE_ENTRY_IMAGE_CLIPS,
    SAVE_TABLE_ENTRY_POFFINS,
    SAVE_TABLE_ENTRY_SEAL_CASE,
    SAVE_TABLE_ENTRY_LINK_CONTEST_RECORDS,
    SAVE_TABLE_ENTRY_POKEDEX,
    OPENMMO_SAVE_BLOCK_OPTIONS,
    OPENMMO_SAVE_BLOCK_TRAINER_ID,
};

#define BLOCK_COUNT ((int)(sizeof kBlocks / sizeof kBlocks[0]))

/* One frame may carry every block this file keeps; the wire's own cap is what
 * would stop it, so say here that it does not. */
typedef char openmmo_save_blocks_fit[
    (BLOCK_COUNT <= MMO_SAVE_BLOCK_MAX) ? 1 : -1];

static u8 s_shadow[BLOCK_COUNT][MMO_SAVE_BLOCK_BYTES];
static int s_len[BLOCK_COUNT];   /* this build's size for the block, once known */
static int s_wide[BLOCK_COUNT];  /* the block is past the wire's width; skipped */
static int s_owed[BLOCK_COUNT];  /* minted here; the server has yet to hear it */
static int s_watching;
static unsigned s_applied;       /* the seat generation already written */
static int s_reported;

void openmmo_save_blocks_reset(void)
{
    memset(s_shadow, 0, sizeof s_shadow);
    memset(s_len, 0, sizeof s_len);
    memset(s_wide, 0, sizeof s_wide);
    memset(s_owed, 0, sizeof s_owed);
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
    at = block_body(save, kBlocks[i], &len);
    if (len <= 0)
        return NULL;
    if (len > MMO_SAVE_BLOCK_BYTES) {
        printf("openmmo: save block %d is %d bytes, past the wire's %d, "
               " not kept\n", kBlocks[i], len, MMO_SAVE_BLOCK_BYTES);
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

    for (i = 0; i < BLOCK_COUNT; i++) {
        int len = 0;
        u8 *at = block_at(save, i, &len);

        if (at == NULL)
            continue;
        s_owed[i] = block_is_minted(kBlocks[i]);
        for (k = 0; k < st->block_count; k++) {
            if (st->block[k].id != kBlocks[i])
                continue;
            /* A block the server holds at a different width is a record from
             * another build. Half a struct written over a live save is worse
             * than none, so it is refused and named. */
            if (st->block[k].len != len) {
                printf("openmmo: save block %d seated at %d bytes, this build"
                       " holds %d, refused\n",
                       kBlocks[i], st->block[k].len, len);
                break;
            }
            memcpy(at, st->block[k].data, (size_t)len);
            s_owed[i] = 0;
            if (kBlocks[i] == OPENMMO_SAVE_BLOCK_OPTIONS)
                options_seated = 1;
            seated++;
            break;
        }
        memcpy(s_shadow[i], at, (size_t)len);
    }

    /* The one setting in the options block that is not re-read where it is used. */
    if (options_seated)
        Sound_SetPlaybackMode(Options_SoundMode(SaveData_GetOptions(save)));

    if (st->blocks_dropped)
        printf("openmmo: %d seated save block(s) past this client's caps were"
               " dropped\n", st->blocks_dropped);
    if (seated || st->block_count)
        printf("openmmo: %d save block(s) seated of %d sent\n",
               seated, st->block_count);
    s_applied = st->seq;
    s_watching = 1;
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

    for (i = 0; i < BLOCK_COUNT && n < cap; i++) {
        int len = 0;
        const u8 *at = block_at(save, i, &len);

        if (at == NULL)
            continue;
        if (!s_owed[i] && memcmp(at, s_shadow[i], (size_t)len) == 0)
            continue;
        out[n].id = kBlocks[i];
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
        const u8 *at = block_body(fs->saveData, kBlocks[i], NULL);

        if (at == NULL || len <= 0)
            continue;
        memcpy(s_shadow[i], at, (size_t)len);
        s_owed[i] = 0;
        s_reported++;
        printf("openmmo: save block %d reported (%d bytes, %d this session)\n",
               kBlocks[i], len, s_reported);
    }
    s_queued_n = 0;
}
