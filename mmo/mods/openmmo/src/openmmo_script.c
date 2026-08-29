/* The local script VM's state, and the server's record of it. */

#include <stdio.h>
#include <string.h>

#include "field/field_system.h"
#include "field_overworld_state.h"
#include "savedata.h"
#include "save_player.h"
#include "struct_defs/player_data.h"
#include "vars_flags.h"

#include "../../../include/client.h"

/* The wire's id space must be the engine's. If a bump moves either constant
 * this stops compiling here rather than silently truncating a seat. */
typedef char openmmo_script_flag_fit[
    (MMO_SCRIPT_FLAG_MAX == NUM_FLAGS) ? 1 : -1];
typedef char openmmo_script_var_fit[
    (MMO_SCRIPT_VAR_MAX == NUM_VARS) ? 1 : -1];
typedef char openmmo_script_var_base_fit[
    (MMO_SCRIPT_VAR_BASE == VARS_START) ? 1 : -1];

/* At most this many rows leave in one report. A report is not urgent, the
 * server is being told, not asked, and a burst that does not fit is carried
 * by leaving the shadow un-advanced for the rows that did not go, so the next
 * one finds them again. */
#define REPORT_FLAGS_MAX 192
#define REPORT_VARS_MAX  64

/*
 * Frames between reports. Not every frame: the engine keeps live counters in this block (the
 * Vs Seeker's charge and step count are two), so a per-frame diff turns every step the player
 * takes into a packet beside the step itself.
 */
#define REPORT_INTERVAL 30

static u8 s_shadow_flags[NUM_FLAGS / 8];
static u16 s_shadow_vars[NUM_VARS];
static int s_watching;        /* the shadow is live and diffs are worth sending */
static unsigned s_applied;    /* the seat generation already written to the save */
static int s_reported;        /* rows sent this session, for the one-line summary */
static int s_countdown;       /* frames until the next diff */

/* A flag the VM keeps but the server must not: MAP_LOCAL_FLAGS (1..64) is
 * scratch the engine memsets on every map change, and MAP_LOCAL_VARS
 * (VARS_START..+31) is the same for vars. Sending either would be a packet per
 * door for state that is dead by the time it lands. */
static int flag_is_map_local(int id)
{
    return id >= MAP_LOCAL_FLAGS_START && id <= MAP_LOCAL_FLAGS_END;
}

static int var_is_map_local(int id)
{
    return id >= MAP_LOCAL_VARS_START && id <= MAP_LOCAL_VARS_END;
}

/* Take the shadow to be the block as it stands, without reporting anything. */
static void shadow_sync(const VarsFlags *vf)
{
    memcpy(s_shadow_flags, vf->flags, sizeof s_shadow_flags);
    memcpy(s_shadow_vars, vf->vars, sizeof s_shadow_vars);
}

void openmmo_script_state_reset(void)
{
    /* The save blocks this op also carries are reset with it: they share the
     * seat generation, so a half-reset pair would seat one and not the other. */
    extern void openmmo_save_blocks_reset(void);

    openmmo_save_blocks_reset();
    memset(s_shadow_flags, 0, sizeof s_shadow_flags);
    memset(s_shadow_vars, 0, sizeof s_shadow_vars);
    s_watching = 0;
    s_applied = 0;
    s_reported = 0;
    s_countdown = 0;
}

/*
 * Write the server's seat into the block. The seat is absolute, the server sends everything
 * it holds, so the block is cleared first and rebuilt from it, rather than merged into, and a
 * value the server does not have is a value this character does not have.
 */
static void seat_apply(VarsFlags *vf, const openmmo_script_state *st)
{
    int id;

    if (st->flags_set == 0 && st->vars_set == 0) {
        memset(s_shadow_flags, 0, sizeof s_shadow_flags);
        memset(s_shadow_vars, 0, sizeof s_shadow_vars);
        printf("openmmo: empty seat, keeping the new-game script state and"
               " reporting it\n");
        s_applied = st->seq;
        s_watching = 1;
        return;
    }

    memset(vf->flags, 0, sizeof vf->flags);
    memset(vf->vars, 0, sizeof vf->vars);

    for (id = 1; id < NUM_FLAGS; id++) {
        if (st->flag[id >> 3] & (1u << (id & 7)))
            VarsFlags_SetFlag(vf, (u16)id);
    }
    for (id = 0; id < NUM_VARS; id++)
        vf->vars[id] = st->var[id];

    printf("openmmo: script state seated, %d flag(s), %d var(s)%s\n",
           st->flags_set, st->vars_set,
           st->out_of_range ? " (some ids past this build's tables)" : "");
    if (st->out_of_range)
        printf("openmmo: %d seated id(s) are past NUM_FLAGS/NUM_VARS and were"
               " dropped\n", st->out_of_range);

    shadow_sync(vf);
    s_applied = st->seq;
    s_watching = 1;
}

/* One frame. `fs` is the field the frame already found; `c` the live session.
 * Applies a seat the first frame a save is reachable after it arrives, then, 
 * once every REPORT_INTERVAL frames, reports whatever the VM has written
 * since the last report. */
void openmmo_script_state_tick(void *fieldSystemVoid, openmmo_client *c)
{
    FieldSystem *fs = fieldSystemVoid;
    const openmmo_script_state *st;
    VarsFlags *vf;
    mmo_script_flag flags[REPORT_FLAGS_MAX];
    mmo_script_var vars[REPORT_VARS_MAX];
    mmo_save_block blocks[MMO_SAVE_BLOCK_MAX];
    int nflags = 0, nvars = 0, nblocks = 0;
    int i, bit;

    /* The save blocks this op also carries (openmmo_save_blocks.c). They share
     * this tick because they share the packet: one clock, one frame. */
    extern int openmmo_save_blocks_collect(void *fs, openmmo_client *c,
                                           mmo_save_block *out, int cap);
    extern void openmmo_save_blocks_commit(void *fs);

    if (c == NULL || fs == NULL || fs->saveData == NULL)
        return;
    vf = SaveData_GetVarsFlags(fs->saveData);
    if (vf == NULL)
        return;

    /* A seat is applied the first frame a save is reachable after it arrives.
     * It lands in the join burst, ahead of the field, so this cannot be done
     * where it is read; the generation counter is what makes "already applied"
     * a fact about the seat rather than about when this ran. */
    st = openmmo_client_script_state(c);
    if (st != NULL && st->seated && st->seq != s_applied) {
        seat_apply(vf, st);
        /* State the engine keeps outside this block, carried as synthetic
         * flag rows (client.h). The play path rolls a fresh save every join,
         * so what is not put back here was never given. */
        if (st->running_shoes) {
            PlayerData *pd = FieldOverworldState_GetPlayerData(
                SaveData_GetFieldOverworldState(fs->saveData));

            PlayerData_SetRunningShoes(pd, TRUE);
            printf("openmmo: running shoes seated\n");
        }
        return;
    }
    if (!s_watching)
        return;
    if (--s_countdown > 0)
        return;
    s_countdown = REPORT_INTERVAL;

    for (i = 0; i < (int)sizeof s_shadow_flags && nflags < REPORT_FLAGS_MAX; i++) {
        u8 diff = (u8)(vf->flags[i] ^ s_shadow_flags[i]);

        if (diff == 0)
            continue;
        for (bit = 0; bit < 8 && nflags < REPORT_FLAGS_MAX; bit++) {
            int id = i * 8 + bit;

            if ((diff & (1u << bit)) == 0)
                continue;
            /* Advance the shadow only for a row that goes out, so a batch that
             * hits the cap leaves the rest to be found again next frame. */
            s_shadow_flags[i] = (u8)((s_shadow_flags[i] & ~(1u << bit))
                                     | (vf->flags[i] & (1u << bit)));
            if (id == 0 || flag_is_map_local(id))
                continue;
            flags[nflags].id = (u16)id;
            flags[nflags].on = (u8)((vf->flags[i] >> bit) & 1);
            nflags++;
        }
    }

    for (i = 0; i < NUM_VARS && nvars < REPORT_VARS_MAX; i++) {
        int id = VARS_START + i;

        if (vf->vars[i] == s_shadow_vars[i])
            continue;
        s_shadow_vars[i] = vf->vars[i];
        if (var_is_map_local(id))
            continue;
        vars[nvars].id = (u16)id;
        vars[nvars].value = vf->vars[i];
        nvars++;
    }

    nblocks = openmmo_save_blocks_collect(fs, c, blocks, MMO_SAVE_BLOCK_MAX);

    if (nflags == 0 && nvars == 0 && nblocks == 0)
        return;
    if (openmmo_client_send_script_state(c, flags, nflags, vars, nvars,
                                         blocks, nblocks) != 0)
        return;
    openmmo_save_blocks_commit(fs);
    s_reported += nflags + nvars;
    /* Name the first of each, so a report that should not have happened can be
     * traced to an id without a second build. */
    {
        char fwhere[24] = "", vwhere[24] = "";

        if (nflags)
            snprintf(fwhere, sizeof fwhere, " (from %u)", flags[0].id);
        if (nvars)
            snprintf(vwhere, sizeof vwhere, " (from %u)", vars[0].id);
        printf("openmmo: script wrote %d flag(s)%s and %d var(s)%s"
               " (%d this session)\n",
               nflags, fwhere, nvars, vwhere, s_reported);
    }
}
