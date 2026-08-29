/*
 * What an appended overworld graphics id is allowed to look like, when
 * "a person" is the wrong answer.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "billboard.h"

#include "pc_modfs.h"

/*
 * How many distinct cooked sequences one run may draw with. Every follower on screen shares
 * one (the walk), so this is headroom for a second and a third rather than a number anything
 * approaches.
 */
#define OPENMMO_BILLBOARD_SEQS 8

/* The most facings a cooked sequence may carve its timeline into. Four is what
 * a walking body is (north, south, west, east) and what every sequence in
 * either game's archive uses; the slack is so a stationary or a two-facing one
 * would fit without a rebuild. */
#define OPENMMO_BILLBOARD_FACINGS 8

struct seq_anims {
    int seq;
    BillboardAnim anims[OPENMMO_BILLBOARD_FACINGS + 1]; /* + TABLE_END */
};

static struct seq_anims sSeqs[OPENMMO_BILLBOARD_SEQS];
static int sNSeqs;

/* Build (or find) the facing list for one cooked sequence. */
static const BillboardAnim *anims_for(int seq, int facings, int frames)
{
    struct seq_anims *slot;
    int i;

    for (i = 0; i < sNSeqs; i++) {
        if (sSeqs[i].seq == seq) {
            return sSeqs[i].anims;
        }
    }

    if (facings < 1 || facings > OPENMMO_BILLBOARD_FACINGS || frames < 1) {
        static int said;
        if (!said) {
            said = 1;
            printf("openmmo: cooked billboard sequence %d says %d facing(s) of"
                   " %d frame(s), which is not a timeline this can carve\n",
                   seq, facings, frames);
            fflush(stdout);
        }
        return NULL;
    }

    if (sNSeqs >= OPENMMO_BILLBOARD_SEQS) {
        static int said;
        if (!said) {
            said = 1;
            printf("openmmo: more than %d cooked billboard sequences in one"
                   " run; %d and any after it draw as a person\n",
                   OPENMMO_BILLBOARD_SEQS, seq);
            fflush(stdout);
        }
        return NULL;
    }

    slot = &sSeqs[sNSeqs++];
    slot->seq = seq;
    for (i = 0; i < facings; i++) {
        slot->anims[i].startFrame = i * frames;
        slot->anims[i].endFrame = (i + 1) * frames - 1;
        slot->anims[i].animType = BILLBOARD_ANIM_TYPE_LOOP;
    }
    /* The engine scans to the terminator rather than being told a count. */
    slot->anims[facings].startFrame = 0;
    slot->anims[facings].endFrame = 0;
    slot->anims[facings].animType = BILLBOARD_ANIM_TYPE_TABLE_END;

    printf("openmmo: cooked billboard sequence %d -> %d facing(s) of %d frames"
           " (0..%d)\n", seq, facings, frames, facings * frames - 1);
    fflush(stdout);
    return slot->anims;
}

/*
 * The row for a cooked graphics id, or 0 to let the engine clone the youngster's as it always
 * has.
 */
int openmmo_billboard_row(int gfx_id, int *model, int *seq,
                          const BillboardAnim **anims)
{
    struct pc_modfs_billboard row;
    struct pc_modfs_billboard_seq shape;
    const BillboardAnim *list;

    if (!pc_modfs_billboard(gfx_id, &row)) {
        return 0;
    }
    if (row.model < 0 || row.seq < 0) {
        return 0;
    }
    if (!pc_modfs_billboard_seq(row.seq, &shape)) {
        static int said;
        if (!said) {
            said = 1;
            printf("openmmo: gfx %d asks for billboard sequence %d and no"
                   " package planted one\n", gfx_id, row.seq);
            fflush(stdout);
        }
        return 0;
    }

    list = anims_for(row.seq, shape.facings, shape.frames);
    if (list == NULL) {
        return 0;
    }

    *model = row.model;
    *seq = row.seq;
    *anims = list;
    return 1;
}

/* The mmodel member holding a cooked frame sequence, or -1. */
int openmmo_billboard_seq_member(int seq_id)
{
    struct pc_modfs_billboard_seq shape;

    if (!pc_modfs_billboard_seq(seq_id, &shape)) {
        return -1;
    }
    return shape.member;
}
