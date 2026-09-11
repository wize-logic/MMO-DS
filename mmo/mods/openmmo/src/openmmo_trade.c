/* The trade offer, and the engine's own trade scene. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "constants/heap.h"
#include "field/field_system.h"
#include "field_system.h"
#include "field_bgm.h"
#include "field_task.h"
#include "field_transition.h"
#include "heap.h"
#include "sound.h"
#include "sound_playback.h"
#include "savedata/save_table.h"
#include "save_player.h"
#include "trainer_info.h"

#include "../../../include/charcode.h"
#include "../../../include/endpoint.h"
#include "../../../include/client.h"
#include "../../../include/game.h"

/* HEAP_ID_COMMUNICATION, shared with the link contest,
 * which wants the same heap for the same dispatcher. */
extern int openmmo_comm_heap_take(u32 size);

extern int openmmo_hud_windowed(void);
extern FieldSystem *pc_lab_field_system(void);
extern void CommCmd_Callback(int netId, int cmd, int size, void *data);
extern BOOL sub_0203DBF0(FieldTask *taskMan);
extern void sub_0203DDDC(FieldTask *taskMan);

static openmmo_client *s_client;

/* The standing offer, held for a settled field like the duel's. */
static char s_offer_from[OPENMMO_ENTITY_NAME_MAX];
static int s_offered;
static int s_asked;

/* --- the pipe ------------------------------------------------------------- * */

static int s_pipe;      /* the scene's comm world is ours */
static u8  s_own_party[1424]; /* the last party chunk this chair sent */
static int s_own_party_live;
static int s_net_id;    /* this chair, 0 or 1, from the table's OPEN */
static int s_scene;     /* the wrapper field task is alive */
static int s_seat_wait; /* table open; waiting for a settled field */
static int s_aborting;  /* the peer is gone; the polite goodbye was staged */

/* One delivery: a scene command from either chair, in arrival order. The
 * loopback and the relay share the queue so ordering can never diverge, 
 * a stale or duplicated intent byte can half-cancel a trade (the handlers
 * pair raw bytes), so this queue is exact or it is wrong. */
typedef struct {
    u8  from;
    s16 cmd;
    u16 len;
    u8  data[MMO_TRADE_COMM_MAX];
} TradeMsg;

#define TRADE_Q 32
static TradeMsg s_q[TRADE_Q];
static int s_q_head;
static int s_q_count;

/* The CommTiming barriers, pipe-owned: a marker each way per number, and
 * the released number is the last one both chairs reached, the same
 * semantics the DS protocol has (commands 16/17), including the rule that
 * a number is never re-armed. */
static u8 s_sync_sent[32];
static u8 s_sync_seen[32];
static int s_sync_state = -1;

/* The authority mirror: the last cursor byte this side sent (command 23),
 * which is what a command-24-value-2 latches as the offered slot. */
static int s_own_cursor = -1;

/* The other chair's TrainerInfo, built from the table's OPEN. */
static TrainerInfo *s_peer_info;

extern int openmmo_dialog_ask_local(FieldSystem *fs, const char *utf8,
                                    void (*answer)(int yes));

static void offer_answered(int yes)
{
    s_offered = 0;
    s_asked = 0;
    if (s_client == NULL)
        return;
    printf("openmmo: trade offer %s\n", yes ? "accepted" : "declined");
    openmmo_client_trade_action(s_client, yes ? MMO_TRADE_ACTION_ACCEPT
                                              : MMO_TRADE_ACTION_CANCEL);
}

/* The scene (or its wind-down) is up: the guest owns both screens. The
 * window reads this through the hud page's guest_busy and lower fields. */
int openmmo_trade_scene_up(void)
{
    return s_scene;
}

void openmmo_trade_attach(openmmo_client *c)
{
    s_client = c;
    s_offered = 0;
    s_asked = 0;
    s_pipe = 0;
    s_scene = 0;
    s_seat_wait = 0;
    s_aborting = 0;
    s_peer_info = NULL;
}

/* Another player asked; hold the question for a settled field.
 * OPENMMO_TRADE_ACCEPT=1 answers yes without asking, the headless rig's
 * door, the same seam the duel's OPENMMO_DUEL is, and only that: a windowed
 * player is always asked on the box. */
void openmmo_trade_offer(const char *name)
{
    const char *aa = openmmo_dev_env("OPENMMO_TRADE_ACCEPT");

    snprintf(s_offer_from, sizeof s_offer_from, "%s", name != NULL ? name : "");
    if (aa != NULL && aa[0] != '\0' && aa[0] != '0') {
        printf("openmmo: trade offer from %s auto-accepted\n", s_offer_from);
        s_offered = 0;
        s_asked = 0;
        openmmo_client_trade_action(s_client, MMO_TRADE_ACTION_ACCEPT);
        return;
    }
    s_offered = 1;
    s_asked = 0;
}

/* ---- the shims' first question ------------------------------------------ */

int openmmo_trade_pipe_up(void)
{
    return s_pipe;
}

int openmmo_trade_net_id(void)
{
    return s_pipe ? s_net_id : -1;
}

int openmmo_trade_player_connected(int netId)
{
    return s_pipe && (netId == 0 || netId == 1);
}

int openmmo_trade_queued(int cmd)
{
    int i;

    if (!s_pipe)
        return 0;
    for (i = 0; i < s_q_count; i++)
        if (s_q[(s_q_head + i) % TRADE_Q].cmd == cmd)
            return 1;
    return 0;
}

TrainerInfo *openmmo_trade_trainer_info(int netId)
{
    FieldSystem *fs;

    if (!s_pipe)
        return NULL;
    if (netId != s_net_id)
        return s_peer_info;
    fs = pc_lab_field_system();
    if (fs == NULL || fs->saveData == NULL)
        return NULL;
    return SaveData_GetTrainerInfo(fs->saveData);
}

/* ---- queue and delivery -------------------------------------------------- */

static void trade_queue(int from, int cmd, const void *data, int size)
{
    TradeMsg *m;

    if (size < 0 || size > MMO_TRADE_COMM_MAX) {
        printf("openmmo: trade scene message %d of %d bytes refused\n", cmd,
               size);
        return;
    }
    if (s_q_count >= TRADE_Q) {
        /* Ordering is correctness here; a dropped byte pairs the next round
         * wrongly. Say so loudly, the queue is sized past any real burst. */
        printf("openmmo: trade scene queue overflow at command %d\n", cmd);
        return;
    }
    m = &s_q[(s_q_head + s_q_count) % TRADE_Q];
    m->from = (u8)from;
    m->cmd = (s16)cmd;
    m->len = (u16)size;
    if (size > 0)
        memcpy(m->data, data, (size_t)size);
    s_q_count++;
}

/* One send from the engine while the pipe is up: the local echo the
 * handlers depend on, the relay to the other chair, and the authority
 * mirror for the two bytes that mean something outside the scene. */
int openmmo_trade_send(int cmd, const void *data, int size)
{
    const u8 *bytes = data;

    if (!s_pipe)
        return 0;
    if (cmd == 22 && size == (int)sizeof s_own_party) {
        /* The whole-party chunk this chair sends at the table's entry; the
         * polite goodbye replays it as the peer's, so a screen that shows
         * the stand-in shows real records rather than 0xFF ones (the
         * summary screen asserts on an unterminated ot name). */
        memcpy(s_own_party, data, sizeof s_own_party);
        s_own_party_live = 1;
    }
    if (cmd == 23 && size >= 1) {
        if (s_own_cursor < 0)
            printf("openmmo: trade table interactive\n");
        s_own_cursor = bytes[0];
    }
    if (cmd == 24 && size >= 1) {
        if (bytes[0] == 2 && s_own_cursor >= 0 && s_own_cursor < 6) {
            printf("openmmo: trade scene offers slot %d\n", s_own_cursor);
            openmmo_client_trade_select(s_client, s_own_cursor);
        } else if (bytes[0] == 3) {
            printf("openmmo: trade scene confirms\n");
            openmmo_client_trade_action(s_client, MMO_TRADE_ACTION_CONFIRM);
        }
        /* 1 (a polite cancel) is not sent up: the joint exit still needs
         * the relay, so the server hears the cancel at scene teardown. */
    }
    trade_queue(s_net_id, cmd, data, size);
    openmmo_client_trade_comm_send(s_client, MMO_TRADE_CHANNEL_COMMAND, cmd,
                                   data, size);
    return 1;
}

/* ---- the barriers -------------------------------------------------------- */

static void sync_mark(u8 *bits, int no)
{
    bits[(no & 0xFF) >> 3] |= (u8)(1u << (no & 7));
}

static int sync_marked(const u8 *bits, int no)
{
    return (bits[(no & 0xFF) >> 3] >> (no & 7)) & 1;
}

static void sync_release_check(int no)
{
    if (sync_marked(s_sync_sent, no) && sync_marked(s_sync_seen, no))
        s_sync_state = no;
}

int openmmo_trade_sync_start(int syncNo)
{
    if (!s_pipe)
        return 0;
    sync_mark(s_sync_sent, syncNo);
    openmmo_client_trade_comm_send(s_client, MMO_TRADE_CHANNEL_SYNC, syncNo,
                                   NULL, 0);
    sync_release_check(syncNo);
    return 1;
}

int openmmo_trade_sync_state(int syncState)
{
    if (!s_pipe)
        return -1;
    return s_sync_state == syncState ? 1 : 0;
}

/* ---- the peer going away ------------------------------------------------- * */
static void trade_abort_synthesize(void)
{
    static const u8 zero[1424] = { 0 };
    const openmmo_party *p;
    u8 party[1424];
    u8 byte;
    int peer = s_net_id ^ 1;
    int i;

    if (s_aborting)
        return;
    s_aborting = 1;
    printf("openmmo: trade peer is gone; staging the polite goodbye\n");

    /* Whatever barrier the scene arms from here on is already met. */
    memset(s_sync_seen, 0xFF, sizeof s_sync_seen);
    for (i = 0; i < 256; i++)
        if (sync_marked(s_sync_sent, i))
            sync_release_check(i);

    byte = 3; /* the stagger seed net id 0 would have sent */
    trade_queue(0, 31, &byte, 1);
    trade_queue(peer, 32, zero, 14);
    /* The party exchange: this chair's own records stand in for the
     * peer's. Real records, not filler, a 0xFF party has no EOS on any
     * name, and the first screen to print one asserts. */
    if (s_own_party_live)
        memcpy(party, s_own_party, sizeof party);
    else
        memset(party, 0, sizeof party);
    p = openmmo_client_party(s_client);
    (void)p;
    trade_queue(peer, 22, party, 1424);
    byte = 0;
    trade_queue(peer, 27, &byte, 1);
    trade_queue(peer, 28, zero, 136);
    trade_queue(peer, 29, zero, 1000);
    byte = 1; /* the standing "I want to leave" */
    trade_queue(peer, 24, &byte, 1);
}

/* The scene's own watchdog lands here (CommManager_SetCommError patch). */
int openmmo_trade_comm_error(int error)
{
    if (!s_pipe)
        return 0;
    printf("openmmo: trade scene comm error %d\n", error);
    trade_abort_synthesize();
    return 1;
}

/* ---- the settlement mirror ----------------------------------------------- * */
void openmmo_trade_scene_settled(void)
{
    printf("openmmo: trade scene settled; the server's word is on its way\n");
}

/* ---- seating the scene --------------------------------------------------- */

static void peer_info_build(void)
{
    const openmmo_trade *tr = openmmo_client_trade(s_client);
    mmo_charcode name[OPENMMO_ENTITY_NAME_MAX];

    if (s_peer_info == NULL)
        s_peer_info = Heap_Alloc(HEAP_ID_FIELD3, TrainerInfo_Size());
    if (s_peer_info == NULL)
        return;
    TrainerInfo_Init(s_peer_info);
    /* The engine's name field is seven glyphs and must end in CHAR_EOS, 
     * a name that fills the field without one asserts in String_Concat
     * (the union list hit the same wall). Truncate, then terminate. */
    mmo_utf8_to_charcode(tr->peer, name, OPENMMO_ENTITY_NAME_MAX);
    name[7] = (mmo_charcode)MMO_CHAR_EOS;
    TrainerInfo_SetName(s_peer_info, (const charcode_t *)name);
    TrainerInfo_SetGender(s_peer_info, tr->peer_gender ? 1 : 0);
    /* A stable id derived from the name, so the scene's id readouts do not
     * show a different stranger every session. */
    {
        u32 id = 0x4D4D4Fu;
        const char *c;

        for (c = tr->peer; *c != '\0'; c++)
            id = id * 31u + (u8)*c;
        TrainerInfo_SetID(s_peer_info, id);
    }
}

static void pipe_open(void)
{
    const openmmo_trade *tr = openmmo_client_trade(s_client);
    /*
     * The command dispatcher allocates its table on the comm boot's own heap, which this port
     * never creates.
     */
    static int s_comm_heap;

    if (!s_comm_heap) {
        s_comm_heap = openmmo_comm_heap_take(0x7080);
    }

    s_net_id = tr->role ? 1 : 0;
    s_q_head = 0;
    s_q_count = 0;
    memset(s_sync_sent, 0, sizeof s_sync_sent);
    memset(s_sync_seen, 0, sizeof s_sync_seen);
    s_sync_state = -1;
    s_own_cursor = -1;
    s_aborting = 0;
    peer_info_build();
    s_pipe = 1;
    printf("openmmo: trade pipe up, chair %d, across from %s\n", s_net_id,
           tr->peer);
}

static void pipe_close(void)
{
    if (!s_pipe)
        return;
    s_pipe = 0;
    s_q_count = 0;
    s_own_party_live = 0;
    if (s_peer_info != NULL) {
        Heap_Free(s_peer_info);
        s_peer_info = NULL;
    }
    printf("openmmo: trade pipe down\n");
}

typedef struct {
    int state;
} TradeSeat;

static BOOL trade_scene_task(FieldTask *task)
{
    FieldSystem *fs = FieldTask_GetFieldSystem(task);
    TradeSeat *seat = FieldTask_GetEnv(task);

    (void)fs;
    switch (seat->state) {
    case 0:
        /* The official client's caller (the comm club script) fades the field out before
         * the trade sequence tears the map down; a server-opened trade has
         * no script in front of it, so the fade is here. */
        seat->state = 1;
        FieldTransition_FadeOut(task);
        return FALSE;

    case 1:
        /* The child runs the whole session: table, animation, evolutions,
         * and back to the table until a cancel. We resume when it pops. */
        seat->state = 2;
        sub_0203DDDC(task);
        return FALSE;

    case 2:
        /* The scene is over. The server is told if the table still stands
         * (a completed trade already closed nothing, tables survive a
         * settlement, so a walk-away is the only thing said here). */
        if (openmmo_client_trade(s_client)->open) {
            printf("openmmo: trade scene left; cancelling the table\n");
            openmmo_client_trade_action(s_client, MMO_TRADE_ACTION_CANCEL);
        }
        pipe_close();
        /* The trade screens' music out first, so the field's own track
         * below starts from silence rather than on top of it. */
        Sound_FadeOutBGM(0, 30);
        seat->state = 3;
        return FALSE;

    case 3:
        if (Sound_IsFadeActive())
            return FALSE;
        /* The trade screens took the sound scene; hand it back empty so
         * the map start replays the field's own BGM, the climb out of
         * the Underground does exactly this on its way back up. Without
         * it the map came back to the trade music (or its silence). */
        Sound_SetScene(SOUND_SCENE_NONE);
        FieldBGM_ClearOverride(fs);
        /*
         * The map back, the way the link-battle return does it: the transition helper starts
         * the field process and waits for it, a bare FieldSystem_StartFieldMap left the task
         * done before the map was, and the fade below never ran on a map that was not there
         * yet (the stuck black screen the first playtest found).
         */
        FieldTransition_StartMap(task);
        seat->state = 4;
        return FALSE;

    case 4:
        /* The official client's caller fades the screen back in itself after the
         * sequence returns; server-opened scenes have nobody behind them,
         * so the fade is ours, the same line encounter.c's patch adds for
         * a duel. */
        FieldTransition_FadeIn(task);
        seat->state = 5;
        return FALSE;

    case 5:
        s_scene = 0;
        Heap_Free(seat);
        printf("openmmo: trade scene done; field back\n");
        return TRUE;
    }
    return FALSE;
}

static void scene_seat(FieldSystem *fs)
{
    TradeSeat *seat = Heap_Alloc(HEAP_ID_FIELD3, sizeof(TradeSeat));

    if (seat == NULL) {
        printf("openmmo: trade scene would not allocate\n");
        openmmo_client_trade_action(s_client, MMO_TRADE_ACTION_CANCEL);
        return;
    }
    seat->state = 0;
    pipe_open();
    s_scene = 1;
    s_seat_wait = 0;
    FieldSystem_CreateTask(fs, trade_scene_task, seat);
    printf("openmmo: trade scene seated\n");
}

/* ---- the per-frame pump -------------------------------------------------- */

/* Relayed traffic and barrier markers, drained whether or not the scene has
 * finished seating, a marker can arrive while the fade is still running. */
static void pump_wire(void)
{
    mmo_trade_comm msg;

    while (openmmo_client_trade_comm_recv(s_client, &msg)) {
        if (!s_pipe)
            continue;
        if (msg.channel == MMO_TRADE_CHANNEL_SYNC) {
            sync_mark(s_sync_seen, msg.cmd & 0xFF);
            sync_release_check(msg.cmd & 0xFF);
            continue;
        }
        trade_queue(s_net_id ^ 1, msg.cmd, msg.data, msg.len);
    }
}

/* Deliveries into the engine's own dispatcher, in arrival order. */
static void pump_deliver(void)
{
    while (s_pipe && s_scene && s_q_count > 0) {
        TradeMsg *m = &s_q[s_q_head];

        s_q_head = (s_q_head + 1) % TRADE_Q;
        s_q_count--;
        CommCmd_Callback(m->from, m->cmd, m->len, m->data);
    }
}

/* Ask the held question, and seat the scene when the store says the table
 * opened. Called on a settled field, every frame. */
void openmmo_trade_pump(FieldSystem *fs)
{
    const openmmo_trade *tr;
    char line[OPENMMO_ENTITY_NAME_MAX + 40];

    if (fs == NULL || s_client == NULL)
        return;

    if (s_offered && !s_asked) {
        snprintf(line, sizeof line, "%s wants to trade!",
                 s_offer_from[0] ? s_offer_from : "Someone");
        if (openmmo_dialog_ask_local(fs, line, offer_answered))
            s_asked = 1;
        return;
    }

    tr = openmmo_client_trade(s_client);
    if (tr->open && !s_scene && !s_seat_wait) {
        s_seat_wait = 1;
        printf("openmmo: trade table open; waiting for a settled field\n");
    }
    if (s_seat_wait && tr->open) {
        if (fs->task == NULL && FieldSystem_IsRunningFieldMap(fs)
            && !FieldSystem_HasChildProcess(fs))
            scene_seat(fs);
    } else if (s_seat_wait) {
        s_seat_wait = 0;
    }
}

/* Every frame, settled or not: the wire drains, the peer's absence is
 * noticed, and the scene is fed. Called from the boot's client tick. */
void openmmo_trade_tick(void)
{
    if (s_client == NULL)
        return;
    pump_wire();
    if (s_pipe && s_scene && !openmmo_client_trade(s_client)->open
        && !s_aborting) {
        /* The server closed the table under a live scene: the peer left. */
        trade_abort_synthesize();
    }
    pump_deliver();
}
