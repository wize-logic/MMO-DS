/* Four real players in one Super Contest. */

#include <stdio.h>
#include <string.h>

#include "constants/heap.h"
#include "contest.h"
#include "field/field_system.h"
#include "field_system.h"
#include "heap.h"
#include "save_player.h"
#include "savedata.h"
#include "trainer_info.h"

#include "../../../include/charcode.h"
#include "../../../include/client.h"

/* The engine's own delivery into the command table
 * registered for whatever scene is up. */
extern void CommCmd_Callback(int netId, int cmd, int size, void *data);
extern int CommCmd_PacketSizeOf(int cmd);

/* pc/src/pc_lab.c: the running field, for the one thing this file needs off the
 * save, this player's own TrainerInfo, which the engine asks for by net id
 * like any other seat's. */
extern FieldSystem *pc_lab_field_system(void);

#define PACKET_SIZE_VARIABLE (-1)

/* One queued command, copied. The engine hands a pointer into its own stack or
 * into a static it is about to reuse, so nothing may be kept by reference. */
#define CONTEST_Q 48

typedef struct {
    int cmd;
    int from;                       /* the sender's seat, as the server stamped it */
    int len;
    u8  data[MMO_CONTEST_DATA_MAX];
} ContestMsg;

static openmmo_client *s_client;
static int s_pipe;                  /* a contest is seated and the pipe is up */
static int s_seat;                  /* this client's own net id */
static int s_humans;
static int s_gone[MMO_CONTEST_SEATS];

static ContestMsg s_q[CONTEST_Q];
static int s_q_head;
static int s_q_count;
static int s_q_dropped;
static int s_asked;                 /* the queue request is out, seat not yet in */

/* Whether the contest's own command table is registered yet. */
static int s_table_up;
static int s_asked_frames;          /* frames the lobby's script has been waiting */

/* How long the door waits before giving up on the queue. */
#define CONTEST_ASK_FRAMES 3600

/* The barrier. `s_sync_seen[i]` is the last number seat i announced, and -1
 * before it has announced anything. A barrier is passed when every seat that is
 * still here is sitting on the same number. */
static int s_sync_seen[MMO_CONTEST_SEATS];

/* A TrainerInfo per seat, answered to CommInfo_TrainerInfo. */
static TrainerInfo *s_peer_info[MMO_CONTEST_SEATS];

void openmmo_contest_link_attach(openmmo_client *c)
{
    s_client = c;
}

/* ---- the shims' first question ------------------------------------------ */

int openmmo_contest_pipe_up(void)
{
    return s_pipe;
}

int openmmo_contest_net_id(void)
{
    return s_pipe ? s_seat : -1;
}

/* The engine sweeps the eight slots asking this, and a barrier stops waiting
 * for a seat it answers false for, which is the engine's own way of hearing
 * that a console left, and is why a seat that has gone stays a "no" rather than
 * disappearing. */
int openmmo_contest_player_connected(int netId)
{
    if (!s_pipe || netId < 0 || netId >= s_humans)
        return 0;
    return !s_gone[netId];
}

int openmmo_contest_connected(void)
{
    return s_pipe ? s_humans : 0;
}

int openmmo_contest_queued(int cmd)
{
    int i;

    if (!s_pipe)
        return 0;
    for (i = 0; i < s_q_count; i++)
        if (s_q[(s_q_head + i) % CONTEST_Q].cmd == cmd)
            return 1;
    return 0;
}

/* ---- sending ------------------------------------------------------------- */

/* How wide one command's body is. The fixed-size senders pass no length and the
 * command table is what knows it; asked here rather than inside the queue
 * because the frame that goes out needs the same answer. */
static int contest_body_size(int cmd, const void *data, int size)
{
    if (size == 0 && data != NULL) {
        size = CommCmd_PacketSizeOf(cmd);
        if (size == PACKET_SIZE_VARIABLE)
            size = 0;
    }
    return size;
}

/* One command out. The command id leads the payload, which is how the relay
 * carries a whole command without the server having to know the table. */
int openmmo_contest_send(int cmd, const void *data, int size)
{
    u8 frame[1 + MMO_CONTEST_DATA_MAX];

    if (!s_pipe || s_client == NULL)
        return 0;
    size = contest_body_size(cmd, data, size);
    if (size < 0 || size + 1 > (int)sizeof frame) {
        printf("openmmo: contest, command %d is %d bytes, past this pipe's"
               " %d\n", cmd, size, (int)sizeof frame - 1);
        return 0;
    }
    s_table_up = 1;
    frame[0] = (u8)cmd;
    if (size > 0 && data != NULL)
        memcpy(frame + 1, data, (size_t)size);
    /* No loopback: every handler in the contest's table writes into the slot
     * of the seat that sent, and the engine has already filled its own. */
    if (openmmo_client_contest_send(s_client, MMO_CONTEST_KIND_DATA, frame,
                                    size + 1) != 0)
        return 0;
    return 1;
}

/* ---- the barrier --------------------------------------------------------- */

static void sync_note(int seat, int no)
{
    if (seat >= 0 && seat < MMO_CONTEST_SEATS)
        s_sync_seen[seat] = no;
}

/* 1 when this barrier is ours, so the engine's own CommTiming stays out of it:
 * its state block is not allocated in this port and touching it would be a null
 * dereference rather than a fallback. */
int openmmo_contest_sync_start(int syncNo)
{
    u8 tag;

    if (!s_pipe)
        return 0;
    s_table_up = 1;
    sync_note(s_seat, syncNo);
    tag = (u8)syncNo;
    if (s_client != NULL)
        openmmo_client_contest_send(s_client, MMO_CONTEST_KIND_SYNC, &tag, 1);
    return 1;
}

/* 1 / 0 when this barrier is ours, -1 when it is not. Every seat still here has
 * to be sitting on the same number; a seat that left is one the engine has
 * already been told to stop waiting for. */
int openmmo_contest_sync_state(int syncState)
{
    int i;

    if (!s_pipe)
        return -1;
    for (i = 0; i < s_humans; i++) {
        if (s_gone[i])
            continue;
        if (s_sync_seen[i] != syncState)
            return 0;
    }
    return 1;
}

/* ---- who the others are -------------------------------------------------- */

/* Answered to CommInfo_TrainerInfo while a contest is up. NULL for a seat that
 * is not one, which is what the engine reads as an empty place. */
TrainerInfo *openmmo_contest_trainer_info(int netId)
{
    if (!s_pipe || netId < 0 || netId >= s_humans)
        return NULL;
    if (netId == s_seat) {
        FieldSystem *fs = pc_lab_field_system();

        if (fs == NULL || fs->saveData == NULL)
            return NULL;
        return SaveData_GetTrainerInfo(fs->saveData);
    }
    return s_peer_info[netId];
}

static void peers_free(void)
{
    int i;

    for (i = 0; i < MMO_CONTEST_SEATS; i++) {
        if (s_peer_info[i] != NULL) {
            Heap_Free(s_peer_info[i]);
            s_peer_info[i] = NULL;
        }
    }
}

/* Build a TrainerInfo for one peer out of what the seat carried. Only the two
 * answers the contest reads before it starts are real; the name is put in
 * because the engine's own text does ask for one in a few places, and the rest
 * is left at the zeroes TrainerInfo_Init leaves. */
static void peer_seat(int seat, const openmmo_contest *ct)
{
    TrainerInfo *info = TrainerInfo_New(HEAP_ID_FIELD2);
    mmo_charcode name[TRAINER_NAME_LEN + 2];
    size_t k;

    if (info == NULL)
        return;
    TrainerInfo_Init(info);
    for (k = 0; k < sizeof name / sizeof name[0]; k++)
        name[k] = MMO_CHAR_EOS;
    mmo_utf8_to_charcode(ct->contestant[seat].name, name, TRAINER_NAME_LEN + 1);
    TrainerInfo_SetName(info, name);
    TrainerInfo_SetGender(info, ct->contestant[seat].gender ? 1 : 0);
    s_peer_info[seat] = info;
}

/* ---- the seat and the teardown ------------------------------------------- */

void openmmo_contest_link_reset(void)
{
    int i;

    peers_free();
    memset(s_q, 0, sizeof s_q);
    s_q_head = 0;
    s_q_count = 0;
    s_q_dropped = 0;
    s_pipe = 0;
    s_asked = 0;
    s_asked_frames = 0;
    s_table_up = 0;
    s_seat = 0;
    s_humans = 0;
    memset(s_gone, 0, sizeof s_gone);
    for (i = 0; i < MMO_CONTEST_SEATS; i++)
        s_sync_seen[i] = -1;
}

/* The server seated this client. Everything below the pipe is the engine's from
 * here: `Contest_SetUpLinkContest` asks how many of us there are and which one
 * this is, and every question it asks after that comes back through this file. */
int openmmo_contest_link_open(void)
{
    const openmmo_contest *ct;
    int i;

    if (s_client == NULL)
        return 0;
    ct = openmmo_client_contest(s_client);
    if (ct == NULL || !ct->valid)
        return 0;
    if (s_pipe)
        return 1;

    openmmo_contest_link_reset();
    s_seat = ct->seat;
    s_humans = ct->humans;
    for (i = 0; i < s_humans; i++)
        if (i != s_seat)
            peer_seat(i, ct);
    s_pipe = 1;
    printf("openmmo: link contest %d up, seat %d of %d, rank %d type %d\n",
           ct->session_id, ct->seat, ct->humans, ct->rank, ct->type);
    return 1;
}

void openmmo_contest_link_close(void)
{
    if (!s_pipe)
        return;
    printf("openmmo: link contest down (%d command(s) dropped)\n", s_q_dropped);
    openmmo_contest_link_reset();
}

/*
 * The contest is over, called from the lobby's own teardown command (scrcmd_contests.c's
 * ScrCmd_EndContest, via the patch beside this file) while the Contest is still allocated.
 */
void openmmo_contest_link_ended(void *contestVoid)
{
    const Contest *contest = contestVoid;
    u8 placement[MMO_CONTEST_SEATS];
    int i;

    if (!s_pipe) {
        /* An official or practice contest, which never had a pipe. */
        return;
    }
    if (contest == NULL || s_client == NULL) {
        openmmo_contest_link_close();
        return;
    }
    for (i = 0; i < s_humans && i < MMO_CONTEST_SEATS; i++)
        placement[i] = contest->unk_00.unk_118[i].contestPlacement;
    if (s_humans >= 2) {
        printf("openmmo: link contest ended, this seat placed %d\n",
               placement[s_seat] + 1);
        openmmo_client_contest_result(s_client, placement, s_humans);
    }
    openmmo_contest_link_close();
}

/* ---- receiving ----------------------------------------------------------- */

static void contest_queue(int from, const u8 *frame, int len)
{
    ContestMsg *m;
    int slot;

    if (len < 1)
        return;
    if (len - 1 > MMO_CONTEST_DATA_MAX)
        return;
    if (s_q_count >= CONTEST_Q) {
        if (s_q_dropped++ == 0)
            printf("openmmo: contest, the command queue is full; this client"
                   " is no longer running the same contest\n");
        return;
    }
    slot = (s_q_head + s_q_count) % CONTEST_Q;
    m = &s_q[slot];
    m->cmd = frame[0];
    m->from = from;
    m->len = len - 1;
    if (m->len > 0)
        memcpy(m->data, frame + 1, (size_t)m->len);
    s_q_count++;
}

/* One frame of the contest, taken off the wire. Called every frame a contest is
 * up, before the engine's own tick, so a barrier the engine is about to ask
 * about has already been told what the others said. */
void openmmo_contest_link_pump(void)
{
    mmo_contest_comm msg;
    int n;

    if (!s_pipe || s_client == NULL)
        return;

    while (openmmo_client_contest_recv(s_client, &msg)) {
        switch (msg.kind) {
        case MMO_CONTEST_KIND_DATA:
            contest_queue(msg.seat, msg.data, msg.len);
            break;
        case MMO_CONTEST_KIND_SYNC:
            if (msg.len >= 1)
                sync_note(msg.seat, msg.data[0]);
            break;
        case MMO_CONTEST_KIND_LEAVE:
            if (msg.seat >= 0 && msg.seat < MMO_CONTEST_SEATS)
                s_gone[msg.seat] = 1;
            printf("openmmo: contest, seat %d left\n", msg.seat);
            break;
        default:
            break;
        }
    }

    /* Nothing is delivered until the contest's own command table is up; see
     * s_table_up. The queue is deep enough to hold the opening exchange, which
     * is one command per seat. */
    if (!s_table_up)
        return;

    /* One frame's delivery. The count is taken first so that what a handler
     * sends waits for the next frame, which is where the DS would have put it. */
    n = s_q_count;
    while (n-- > 0 && s_q_count > 0) {
        ContestMsg m = s_q[s_q_head];

        s_q_head = (s_q_head + 1) % CONTEST_Q;
        s_q_count--;
        CommCmd_Callback(m.from, m.cmd, m.len, m.data);
        if (!s_pipe)
            return; /* a handler tore the contest down under us */
    }
}

/* ---- the door ------------------------------------------------------------ * */

/* The engine's own comm-club return codes, which the lobby's script branches
 * on: 0 keeps waiting, 1 is the player backing out, 2 is a group, 3 is an
 * error. Restated rather than included because generated/comm_club_ret_codes.h
 * is a ROM-build header this file has no other reason to pull in. */
#define CONTEST_CLUB_WAIT   0
#define CONTEST_CLUB_CANCEL 1
#define CONTEST_CLUB_OK     2
#define CONTEST_CLUB_ERROR  3

/* COMM_TYPE_CONTEST. */
#define CONTEST_COMM_TYPE 8

int openmmo_contest_club_start(int commType, int contestType, int contestRank,
                               int partySlot)
{
    if (commType != CONTEST_COMM_TYPE)
        return 0;   /* not a contest: the engine's own comm club has it */
    if (s_client == NULL) {
        printf("openmmo: contest, no session, so no link contest\n");
        return 0;   /* and with no server there is nothing to ask */
    }
    if (openmmo_client_contest_queue(s_client, contestRank, contestType,
                                     partySlot) != 0) {
        printf("openmmo: contest, the queue request would not send\n");
        return 0;
    }
    s_asked = 1;
    s_asked_frames = 0;
    printf("openmmo: contest, asked for a rank %d type %d link contest with"
           " party slot %d\n", contestRank, contestType, partySlot);
    return 1;
}

/* Polled every frame while the lobby's script waits. The pipe comes up the
 * moment a seat arrives, so the script's own branch is what starts the contest
 * from there. */
int openmmo_contest_club_result(void)
{
    const openmmo_contest *ct;

    if (!s_asked)
        return CONTEST_CLUB_ERROR;
    if (s_client == NULL)
        return CONTEST_CLUB_ERROR;
    ct = openmmo_client_contest(s_client);
    if (ct == NULL)
        return CONTEST_CLUB_ERROR;
    if (ct->valid) {
        s_asked = 0;
        if (!openmmo_contest_link_open())
            return CONTEST_CLUB_ERROR;
        return CONTEST_CLUB_OK;
    }
    if (!ct->queued) {
        /* Neither queued nor seated: the server refused, and said so. Backing
         * out is what the lobby's script calls a cancel, and it puts the player
         * back on the "join or lead" menu rather than printing an error. */
        s_asked = 0;
        return CONTEST_CLUB_CANCEL;
    }
    /* Still waiting, and the belt under the refusal above: a server that cannot
     * answer at all must not leave the script paused forever. */
    if (++s_asked_frames > CONTEST_ASK_FRAMES) {
        s_asked = 0;
        printf("openmmo: contest, no answer to the queue request in %d"
               " frames; giving the desk back\n", CONTEST_ASK_FRAMES);
        openmmo_client_contest_cancel(s_client);
        return CONTEST_CLUB_ERROR;
    }
    return CONTEST_CLUB_WAIT;
}
