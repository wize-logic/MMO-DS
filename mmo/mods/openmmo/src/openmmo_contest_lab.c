/* A link Super Contest with nobody at the keyboard. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "field/field_system.h"
#include "field_system.h"

#include "../../../include/client.h"
#include "../../../include/endpoint.h"

/* The run itself, the same entry the debug menu uses, with
 * the link flag that makes it a group's contest rather than a solo one. */
extern int openmmo_contest_start(FieldSystem *fs, FieldTask *caller, int rank,
                                 int type, int competition, int slot, int link);

/* The pipe the seat opens, and whether it is up. The
 * pipe going down is also how this file hears that the contest is over,
 * openmmo_contest_link_ended closes it from the run's own teardown. */
extern int openmmo_contest_link_open(void);
extern int openmmo_contest_pipe_up(void);

/* pc/src/pc_input.c: the live keypad and pen. `keys` is the PAD_Read-positive
 * mask; the pen coordinates are lower-screen pixels. */
extern void pc_input_live(unsigned keys, int touch_on, unsigned x, unsigned y);

/* CONTEST_COMPETITION_LINK_OR_OFFICIAL: all three rounds, in order. Restated
 * rather than pulling constants/contests.h in for one name. */
#define LAB_COMPETITION 2

#define PAD_A 0x0001u

enum {
    LAB_OFF = 0,
    LAB_WAIT_FIELD,   /* parsed, waiting for the frame and a settled field */
    LAB_ASKED,        /* the queue request is out */
    LAB_SEATED,       /* the pipe is up and the run has been started */
    LAB_DONE,
};

/*
 * Every button the three rounds put under the pen, as the centre of the rect the round tests
 * against.
 */
static const struct { unsigned x, y; } PEN_POINT[] = {
    {  64,  52 }, { 192,  52 }, {  64, 148 }, { 192, 148 },
    {  40,  68 }, { 128,  68 }, { 215,  68 }, { 127, 164 },
    { 128,  40 }, { 128, 136 }, {  48,  80 }, { 208,  80 },
};

/* One tap and the A behind it, in frames from the top of a cycle. */
#define PEN_TOUCH   0
#define PEN_RELEASE 7
#define PEN_PRESS   10
#define PEN_CLEAR   13
#define PEN_PERIOD  16

static openmmo_client *s_client;
static int s_state;
static int s_rank, s_type, s_slot;
static unsigned long s_at = 3000;
static unsigned long s_frame;
static unsigned long s_asked_at;
static unsigned long s_seated_at;
static int s_pen = 1;
static unsigned long s_period = PEN_PERIOD;
static int s_box;
static int s_last_have = -1;
static int s_start_tries;

/* How long a seated seat waits for a settled frame to start its contest on.
 * Ten seconds of played time, which is far longer than anything the field does
 * on its own and short enough that the seats still waiting are told. */
#define CONTEST_START_FRAMES 600

void openmmo_contest_lab_attach(openmmo_client *c)
{
    s_client = c;
}

static void lab_parse(void)
{
    /* Doors, not reports: they make the game enter a contest it was never
     * asked to enter and hold the pen through it, so a release reads them as
     * unset (include/endpoint.h). */
    const char *s = openmmo_dev_env("OPENMMO_LAB_CONTEST_LINK");
    const char *at = openmmo_dev_env("OPENMMO_LAB_CONTEST_AT");
    const char *pen = openmmo_dev_env("OPENMMO_LAB_CONTEST_PEN");
    const char *period = openmmo_dev_env("OPENMMO_LAB_CONTEST_PERIOD");

    if (s == NULL || s[0] == '\0')
        return;
    if (sscanf(s, "%d %d %d", &s_rank, &s_type, &s_slot) != 3) {
        printf("openmmo: contest lab, OPENMMO_LAB_CONTEST_LINK wants"
               " '<rank> <type> <party slot>', got '%s'\n", s);
        return;
    }
    if (at != NULL && at[0] != '\0')
        s_at = strtoul(at, NULL, 0);
    if (pen != NULL && pen[0] == '0')
        s_pen = 0;
    if (period != NULL && period[0] != '\0') {
        s_period = strtoul(period, NULL, 0);
        if (s_period < PEN_CLEAR + 2)
            s_period = PEN_CLEAR + 2;
    }
    s_state = LAB_WAIT_FIELD;
    printf("openmmo: contest lab, will ask for a rank %d type %d link contest"
           " with party slot %d at frame %lu\n", s_rank, s_type, s_slot + 1,
           s_at);
}

/* The pen, while a contest is up. Runs on the cycle above and nothing else
 * touches the pad, so a run that reaches a round that wants one answers it. */
static void lab_pen(void)
{
    unsigned long t;

    if (!s_pen)
        return;
    t = (s_frame - s_seated_at) % s_period;
    if (t == PEN_TOUCH) {
        pc_input_live(0, 1, PEN_POINT[s_box].x, PEN_POINT[s_box].y);
        s_box = (s_box + 1) % (int)(sizeof PEN_POINT / sizeof *PEN_POINT);
    } else if (t == PEN_RELEASE) {
        pc_input_live(0, 0, 0, 0);
    } else if (t == PEN_PRESS) {
        /* The move menu has a second screen behind it that the menu gives no
         * hint of: which judge should rate the performance. A is what answers
         * it, and it is also what clears the standings between rounds. */
        pc_input_live(PAD_A, 0, 0, 0);
    } else if (t == PEN_CLEAR) {
        pc_input_live(0, 0, 0, 0);
    }
}

/* Called once a frame from openmmo_mod_frame, with the field and whether it is
 * settled. `settled` is what the contest needs: FieldSystem_CreateTask asserts
 * against a task already running, and Contest_Init wants a save and a map. */
void openmmo_contest_lab_frame(FieldSystem *fs, int settled)
{
    static int parsed;
    const openmmo_contest *ct;

    if (!parsed) {
        parsed = 1;
        lab_parse();
    }
    if (s_state == LAB_OFF || s_state == LAB_DONE)
        return;

    s_frame++;
    if (s_client == NULL)
        return;

    switch (s_state) {
    case LAB_WAIT_FIELD:
        if (s_frame < s_at || !settled)
            return;
        if (openmmo_client_contest_queue(s_client, s_rank, s_type, s_slot) != 0) {
            printf("openmmo: contest lab, the queue request would not send\n");
            s_state = LAB_DONE;
            return;
        }
        s_asked_at = s_frame;
        s_state = LAB_ASKED;
        printf("openmmo: contest lab, asked at frame %lu\n", s_frame);
        return;

    case LAB_ASKED:
        ct = openmmo_client_contest(s_client);
        if (ct == NULL)
            return;
        if (ct->valid) {
            /*
             * The seat can arrive on a frame the field is not idle on, twenty seconds pass
             * between the ask and the answer, and a peer arriving or a script starting is
             * enough.
             */
            if (!settled) {
                if (++s_start_tries > CONTEST_START_FRAMES) {
                    printf("openmmo: contest lab, the field would not settle"
                           " in %d frames, so this seat leaves rather than"
                           " holding the others\n", CONTEST_START_FRAMES);
                    openmmo_client_contest_leave(s_client);
                    s_state = LAB_DONE;
                }
                return;
            }
            if (!openmmo_contest_link_open()) {
                printf("openmmo: contest lab, the seat arrived but the pipe"
                       " would not open\n");
                openmmo_client_contest_leave(s_client);
                s_state = LAB_DONE;
                return;
            }
            if (!openmmo_contest_start(fs, NULL, ct->rank, ct->type,
                                       LAB_COMPETITION, s_slot, 1)) {
                printf("openmmo: contest lab, the contest would not start\n");
                openmmo_client_contest_leave(s_client);
                s_state = LAB_DONE;
                return;
            }
            s_seated_at = s_frame;
            s_box = 0;
            s_state = LAB_SEATED;
            return;
        }
        if (!ct->queued) {
            printf("openmmo: contest lab, the queue request was refused\n");
            s_state = LAB_DONE;
            return;
        }
        if (ct->queued_want > 0 && ct->queued_have != s_last_have) {
            s_last_have = ct->queued_have;
            printf("openmmo: contest lab, queued (%d of %d waiting)\n",
                   ct->queued_have, ct->queued_want);
        }
        /* Nothing here gives up: the lobby holds a short group for twenty
         * seconds and a headless run passes that in a few thousand frames, so
         * a wait that goes on is a finding rather than a timeout to swallow.
         * Say so every ten thousand frames instead. */
        if ((s_frame - s_asked_at) % 10000 == 0)
            printf("openmmo: contest lab, still queued after %lu frames\n",
                   s_frame - s_asked_at);
        return;

    case LAB_SEATED:
        if (!openmmo_contest_pipe_up()) {
            /* openmmo_contest_link_ended closed it from the run's teardown,
             * which is the contest being over and reported. */
            printf("openmmo: contest lab, done at frame %lu (%lu frames of"
                   " contest)\n", s_frame, s_frame - s_seated_at);
            pc_input_live(0, 0, 0, 0);
            s_state = LAB_DONE;
            return;
        }
        lab_pen();
        if ((s_frame - s_seated_at) % 20000 == 0)
            printf("openmmo: contest lab, %lu frames in, still running\n",
                   s_frame - s_seated_at);
        return;
    }
}
