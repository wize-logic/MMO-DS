/* Does our copy of the frame channel still match the game's? */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pc_view.h"      /* the publisher's, from the engine checkout */
#include "view_channel.h" /* ours */

static int bad;

static void cmp(const char *what, unsigned long ours, unsigned long theirs)
{
    if (ours == theirs) {
        printf("  ok   %-28s %lu\n", what, ours);
    } else {
        printf("  DRIFT %-27s ours %lu, publisher %lu\n", what, ours, theirs);
        bad++;
    }
}

/* Both headers describe the same word at the same place, so the pair of
 * offsetof()s is the whole comparison. */
#define CMP_OFF(f) cmp("offsetof " #f, offsetof(struct openmmo_view_shm, f), \
                                       offsetof(struct pc_view_shm, f))

/*
 * The two helpers the header carries are logic and not layout, so agreeing on
 * offsets says nothing about them. Both are run here against the publisher's
 * own versions rather than against numbers written down by hand: the aspect
 * rule over a sweep of window shapes, and the ring reader over the same bytes
 * read through both struct types, which the layout comparison above is exactly
 * what licenses us to do.
 */
static void cmp_aspect(void)
{
    unsigned n, d;
    int mismatch = 0, tried = 0;

    for (d = 1; d <= 48; d++) {
        for (n = 0; n <= 512; n++) {
            int ours = openmmo_view_aspect_width(n, d);
            int theirs = pc_view_aspect_width(n, d);
            tried++;
            if (ours != theirs && mismatch++ == 0)
                printf("  DRIFT aspect_width(%u,%u)      ours %d, publisher %d\n",
                       n, d, ours, theirs);
        }
    }
    /* Zero denominator is the "no viewer has said" case and has its own answer. */
    if (openmmo_view_aspect_width(16, 0) != pc_view_aspect_width(16, 0))
        mismatch++;
    tried++;

    if (mismatch) {
        bad++;
        printf("  DRIFT aspect_width               %d of %d shapes disagree\n",
               mismatch, tried);
    } else {
        printf("  ok   aspect_width                %d shapes agree\n", tried);
    }
}

static void cmp_audio_read(void)
{
    /*
     * One page, read through both types. Big enough that the ring wrap and the fell-behind
     * path are both exercised, which is where a divergence would actually cost something.
     */
    struct openmmo_view_shm *ours;
    struct pc_view_shm *theirs;

    if (sizeof(struct openmmo_view_shm) != sizeof(struct pc_view_shm)) {
        printf("  skip  audio_read                 page sizes differ\n");
        return;
    }
    ours = calloc(1, sizeof *ours);
    static const uint32_t heads[] = { 0, 7, OPENMMO_VIEW_AUDIO_FRAMES - 3,
                                      OPENMMO_VIEW_AUDIO_FRAMES + 11,
                                      3u * OPENMMO_VIEW_AUDIO_FRAMES };
    unsigned i, mismatch = 0;

    if (ours == 0) {
        printf("  DRIFT audio_read                 out of memory\n");
        bad++;
        return;
    }
    theirs = (struct pc_view_shm *)ours;

    ours->magic = OPENMMO_VIEW_MAGIC;
    ours->version = OPENMMO_VIEW_VERSION;
    for (i = 0; i < OPENMMO_VIEW_AUDIO_FRAMES; i++)
        ours->audio[i] = (i * 2654435761u) ^ 0x5bd1e995u;

    for (i = 0; i < sizeof heads / sizeof heads[0]; i++) {
        int16_t a[256], b[256];
        uint32_t ta = 0, tb = 0, da = 0, db = 0;
        unsigned na, nb;

        ours->audio_head = heads[i];
        memset(a, 0, sizeof a);
        memset(b, 0, sizeof b);
        na = openmmo_view_audio_read(ours, &ta, a, 128, &da);
        nb = pc_view_audio_read(theirs, &tb, b, 128, &db);
        if (na != nb || ta != tb || da != db || memcmp(a, b, sizeof a) != 0) {
            printf("  DRIFT audio_read head %-10u ours (%u,%u,%u), "
                   "publisher (%u,%u,%u)\n", heads[i], na, ta, da, nb, tb, db);
            mismatch++;
        }
    }

    if (mismatch) {
        bad++;
    } else {
        printf("  ok   audio_read                  %u ring positions agree\n",
               (unsigned)(sizeof heads / sizeof heads[0]));
    }
    free(ours);
}

int main(void)
{
    printf("viewcheck: our frame channel against the publisher's\n");

    cmp("MAGIC", OPENMMO_VIEW_MAGIC, PC_VIEW_MAGIC);
    cmp("VERSION", OPENMMO_VIEW_VERSION, PC_VIEW_VERSION);
    cmp("W", OPENMMO_VIEW_W, PC_VIEW_W);
    cmp("H", OPENMMO_VIEW_H, PC_VIEW_H);
    cmp("WIDE_MAX", OPENMMO_VIEW_WIDE_MAX, PC_VIEW_WIDE_MAX);
    cmp("HD_MAX", OPENMMO_VIEW_HD_MAX, PC_VIEW_HD_MAX);
    cmp("FRAME_WORDS", OPENMMO_VIEW_FRAME_WORDS, PC_VIEW_FRAME_WORDS);
    cmp("AUDIO_FRAMES", OPENMMO_VIEW_AUDIO_FRAMES, PC_VIEW_AUDIO_FRAMES);
    cmp("AUDIO_RATE", OPENMMO_VIEW_AUDIO_RATE, PC_VIEW_AUDIO_RATE);

    cmp("KEY_A", OPENMMO_VIEW_KEY_A, PC_VIEW_KEY_A);
    cmp("KEY_B", OPENMMO_VIEW_KEY_B, PC_VIEW_KEY_B);
    cmp("KEY_SELECT", OPENMMO_VIEW_KEY_SELECT, PC_VIEW_KEY_SELECT);
    cmp("KEY_START", OPENMMO_VIEW_KEY_START, PC_VIEW_KEY_START);
    cmp("KEY_RIGHT", OPENMMO_VIEW_KEY_RIGHT, PC_VIEW_KEY_RIGHT);
    cmp("KEY_LEFT", OPENMMO_VIEW_KEY_LEFT, PC_VIEW_KEY_LEFT);
    cmp("KEY_UP", OPENMMO_VIEW_KEY_UP, PC_VIEW_KEY_UP);
    cmp("KEY_DOWN", OPENMMO_VIEW_KEY_DOWN, PC_VIEW_KEY_DOWN);
    cmp("KEY_R", OPENMMO_VIEW_KEY_R, PC_VIEW_KEY_R);
    cmp("KEY_L", OPENMMO_VIEW_KEY_L, PC_VIEW_KEY_L);
    cmp("KEY_X", OPENMMO_VIEW_KEY_X, PC_VIEW_KEY_X);
    cmp("KEY_Y", OPENMMO_VIEW_KEY_Y, PC_VIEW_KEY_Y);

    cmp("sizeof page", sizeof(struct openmmo_view_shm),
                       sizeof(struct pc_view_shm));

    CMP_OFF(magic);
    CMP_OFF(version);
    CMP_OFF(publisher);
    CMP_OFF(seq);
    CMP_OFF(frame_lo);
    CMP_OFF(frame_hi);
    CMP_OFF(upper_engine);
    CMP_OFF(width);
    CMP_OFF(height);
    CMP_OFF(touch_wanted);
    CMP_OFF(pix);
    CMP_OFF(in_seq);
    CMP_OFF(in_keys);
    CMP_OFF(in_touch);
    CMP_OFF(in_touch_x);
    CMP_OFF(in_touch_y);
    CMP_OFF(in_aspect_n);
    CMP_OFF(in_aspect_d);
    CMP_OFF(in_viewer_pid);
    CMP_OFF(in_quit);
    CMP_OFF(in_turbo);
    CMP_OFF(audio_rate);
    CMP_OFF(audio_head);
    CMP_OFF(audio);

    cmp_aspect();
    cmp_audio_read();

    if (bad) {
        printf("viewcheck: *** %d field(s) DRIFTED, "
               "reconcile include/view_channel.h before building a viewer ***\n",
               bad);
        return 1;
    }
    printf("viewcheck: ok (channel version %u, %zu-byte page)\n",
           (unsigned)OPENMMO_VIEW_VERSION, sizeof(struct openmmo_view_shm));
    return 0;
}
