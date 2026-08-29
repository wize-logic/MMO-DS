/* Play server movement sequences on map objects. */

#include <stdio.h>
#include <string.h>

#include "field/field_system.h"
#include "generated/movement_actions.h"
#include "map_object.h"
#include "unk_020655F4.h"

#include "../../../include/client.h"
#include "../../../include/game.h"

/* How the caller turns an entity id into the map object it names. Declared
 * the same way in openmmo_boot.c, which owns the seat table the lookup walks. */
typedef MapObject *(*openmmo_move_resolve)(FieldSystem *fs, s64 entity_id,
                                           int is_self, int slot);

/* Sequences that can be in flight at once. A cutscene beat is the player plus
 * however many people the scene moves with them; four is every scene ported so
 * far with room to spare. */
#define OPENMMO_MOVE_JOBS 6

/* Looks a job gets to find its map object before it is given up on. */
#define OPENMMO_MOVE_SEAT_TRIES 120

typedef struct {
    int live;
    unsigned order;             /* arrival number, for same-entity ordering */
    s64 entity_id;
    int is_self;
    int slot;
    u8  flag;
    u8  count;
    u8  actions[MMO_SCRIPT_MOVE_MAX];
    int index;
    int playing;
    int tries;
} move_job;

static openmmo_client *s_client;
static move_job s_job[OPENMMO_MOVE_JOBS];
static unsigned s_order;

void openmmo_script_move_attach(openmmo_client *c)
{
    s_client = c;
    memset(s_job, 0, sizeof s_job);
    s_order = 0;
}

/* Take the sequence the client just parsed and give it a job of its own. */
void openmmo_script_move_mark_pending(void)
{
    const openmmo_script_move *seq;
    move_job *job = NULL;
    int i, oldest = -1;

    if (s_client == NULL)
        return;
    /* Take this arrival's own copy. Reading the client's "last sequence"
     * instead gave every 0x0D in a batch the last one's actions. */
    seq = openmmo_client_script_move_take(s_client);
    if (seq == NULL || !seq->valid || seq->count == 0)
        return;

    for (i = 0; i < OPENMMO_MOVE_JOBS; i++) {
        if (!s_job[i].live) {
            job = &s_job[i];
            break;
        }
        if (oldest < 0 || s_job[i].order < s_job[oldest].order)
            oldest = i;
    }
    if (job == NULL) {
        /* Every slot busy. The oldest is the one closest to finishing and the
         * one a stalled scene left behind, so it is the one to lose. */
        printf("openmmo: script move table full, dropping entity %lld\n",
               (long long)s_job[oldest].entity_id);
        fflush(stdout);
        job = &s_job[oldest];
    }

    memset(job, 0, sizeof *job);
    job->live = 1;
    job->order = ++s_order;
    job->entity_id = seq->entity_id;
    job->is_self = seq->is_self;
    job->slot = seq->slot;
    job->flag = seq->flag;
    job->count = seq->count;
    memcpy(job->actions, seq->actions, seq->count);
    job->tries = OPENMMO_MOVE_SEAT_TRIES;
}

/*
 * True while any job for `entity_id` is still in flight. The seat half asks this before it
 * writes a 0x12 row's tile onto a map object.
 */
int openmmo_script_move_holds(s64 entity_id)
{
    int i;

    for (i = 0; i < OPENMMO_MOVE_JOBS; i++)
        if (s_job[i].live && s_job[i].entity_id == entity_id)
            return 1;
    return 0;
}

/* Whether an earlier job for the same entity is still going, which is what
 * makes two sequences for one person play in the order they were sent. */
static int job_is_blocked(const move_job *job)
{
    int i;

    for (i = 0; i < OPENMMO_MOVE_JOBS; i++) {
        if (!s_job[i].live || &s_job[i] == job)
            continue;
        if (s_job[i].entity_id == job->entity_id && s_job[i].order < job->order)
            return 1;
    }
    return 0;
}

static void job_step(FieldSystem *fs, move_job *job, MapObject *obj)
{
    int act;

    (void)fs;
    if (obj == NULL) {
        if (job->tries > 0) {
            job->tries--;
            return;
        }
        printf("openmmo: script move entity %lld has no map object"
               " (%d byte(s))\n",
               (long long)job->entity_id, (int)job->count);
        fflush(stdout);
        job->live = 0;
        return;
    }

    if (job->playing) {
        if (LocalMapObj_CheckAnimationFinished(obj) != TRUE)
            return;
        job->index++;
    } else {
        job->playing = 1;
        printf("openmmo: script move entity %lld flag %u %d byte(s)%s\n",
               (long long)job->entity_id, (unsigned)job->flag,
               (int)job->count, job->is_self ? " (self)" : "");
        fflush(stdout);
    }

    while (job->index < job->count) {
        act = mmo_game_script_action(job->flag, job->actions[job->index]);
        if (act < 0) {
            printf("openmmo: script move skipped byte 0x%02x (flag %u)\n",
                   (unsigned)job->actions[job->index], (unsigned)job->flag);
            fflush(stdout);
            job->index++;
            continue;
        }
        printf("openmmo: script move entity %lld step %d/%d byte 0x%02x"
               " -> action %d from (%d,%d)\n",
               (long long)job->entity_id, job->index + 1, (int)job->count,
               (unsigned)job->actions[job->index], act,
               MapObject_GetX(obj), MapObject_GetZ(obj));
        fflush(stdout);
        LocalMapObj_SetAnimationCode(obj, (enum MovementAction)act);
        return;
    }

    printf("openmmo: script move finished on entity %lld at (%d,%d) dir %d"
           " movetype %d\n",
           (long long)job->entity_id, MapObject_GetX(obj),
           MapObject_GetZ(obj), MapObject_GetFacingDir(obj),
           (int)MapObject_GetMovementType(obj));
    fflush(stdout);
    job->live = 0;
}

/* Advance every job one action. `resolve` is the caller's map-object lookup,
 * because which object an id names is the field's question and not this
 * file's. */
void openmmo_script_move_pump(FieldSystem *fs, openmmo_move_resolve resolve)
{
    int i;

    if (s_client == NULL || resolve == NULL)
        return;
    for (i = 0; i < OPENMMO_MOVE_JOBS; i++) {
        move_job *job = &s_job[i];

        if (!job->live || job_is_blocked(job))
            continue;
        job_step(fs, job, resolve(fs, job->entity_id, job->is_self, job->slot));
    }
}
