/* The frame-driven session FSM behind client.h. */
#include "client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>
#include <time.h>

#include "net.h"
#include "platform.h"
#include "codec.h"
#include "crypto.h"
#include "session.h"
#include "login.h"
#include "token.h"
#include "handoff.h"
#include "endpoint.h"
#include "game.h"
#include "entity.h"
#include "follower.h"
#include "idmap.h"
#include "region.h"
#include "appearance.h"
#include "battle_anim.h"

/* Finer internal phases than the coarse status the API exposes. */
typedef enum {
    P_IDLE = 0,
    P_LOGIN_CONNECT,    /* TCP connect to the login server in flight */
    P_LOGIN_HELLO,      /* ClientHello sent, awaiting ServerHello */
    P_LOGIN_AUTH,       /* LoginRequest sent, awaiting LoginResponse */
    P_LOGIN_HELD,       /* AUTHED, holding the idle login session */
    P_HANDOFF_LIST,     /* RequestGameServerList sent, awaiting GameServerList */
    P_HANDOFF_NODES,    /* JoinGameServer sent, awaiting GameServerNodes */
    P_GAME_CONNECT,     /* TCP connect to the game server in flight */
    P_GAME_HELLO,       /* game ClientHello sent, awaiting ServerHello */
    P_GAME_JOIN,        /* JoinPacket sent, awaiting JoinResponse */
    P_GAME_CHARLIST,    /* RequestCharacters sent, awaiting CharactersList */
    P_GAME_CREATE,      /* CreateCharacter sent, awaiting the refreshed list */
    P_GAME_PICK,        /* list in hand; the host picks or creates */
    P_GAME_DELETE,      /* DeleteCharacter sent, awaiting the result */
    P_CREATED,          /* the server added the character; session held */
    P_GAME_SELECT,      /* SelectCharacter sent, awaiting the world-state block + LoadMap */
    P_GAME_ENTER,       /* RequestPlayer sent, awaiting RenderScreen -> in-world */
    P_IN_GAME,          /* standing in the map */
    P_FAILED,           /* terminal failure */
    P_DEAD              /* terminal, torn down */
} phase;

#define EVQ_CAP 32
/* The inflate scratch: one whole decoded game frame. Matches the server's
 * inflate ceiling (network/.../CompressionDecoder.kt DEFAULT_MAX_INFLATED). */
#define OPENMMO_GBUF_CAP ((size_t)0xFFFF * 16)

/*
 * A preloaded/loaded map's server-side scene and grid, keyed by (bank,map) inside the current
 * region.
 */
typedef struct {
    int valid;
    int bank, map;
    int width, height;
    int weather, lighting, map_type, encounter_type;
    int connection_count;
    mmo_map_connection connections[MMO_MAP_MAX_CONNECTIONS];
} mmo_map_grid;

/* Enough for a spawn map and its preloaded neighbourhood (preload depth 2). */
#define MMO_MAP_REG_MAX 32

struct openmmo_client {
    /*
     * Configuration, copied on start. The server's address is not among it: endpoint.h holds
     * the one this build dials, and it is decoded into a frame for the length of a connect and
     * wiped after (see connect_login and connect_game_server).
     */
    u16          login_port;
    u16          game_port;
    char         user[64];
    char         pass[128];
    /* This session signed in with a saved token rather than a password, so a
     * refusal means the token is spent and not that anyone typed anything
     * wrong. */
    int          used_token;
    /* The state byte of the last LoginResponse that refused this session, or -1
     * before one has. A caller retrying a broken link needs to tell a server
     * that is down from one that answered and said no. */
    int          login_refusal;
    openmmo_mode mode;
    int          local_scripts;  /* this client runs the field scripts itself */
    int          timeout_s;
    int          allow_undrawable_region;
    char         create_name[MMO_TEXT_BYTES(MMO_CHAR_NAME_MAX)];
    int          create_gender;
    int          create_region;
    int          create_skin_region;
    u16          create_skin_mask;
    u16          create_skin_word[MMO_SKIN_SLOTS];
    int          create_name_overflow;
    int          char_count_before;
    char         select_name[MMO_TEXT_BYTES(MMO_CHAR_NAME_MAX)];
    int          select_index;
    int          select_region;
    int          select_how;
    mmo_character_list chars;
    int          have_chars;
    char         create_error[128];
    char         delete_error[128];

    phase              ph;
    openmmo_status     status;
    mmo_net            net;
    mmo_session        sess;
    /* Nonzero pins every ClientHello's timestamp instead of reading the clock,
     * so an offline fixture can be signed for one (openmmo_client_pin_hello_time). */
    s64                pinned_hello_time;
    mmo_game_stream    gstream;
    mmo_gs_nodes       nodes;
    mmo_buf            acc;        /* frame accumulator across pumps */
    time_t             deadline;   /* wall-clock budget for the current wait */

    /*
     * The round-trip clock. One KeepAlive (0xC2) is in flight at a time, the server echoes it
     * byte for byte, and the difference is what a player feels: the socket, plus however deep
     * this session's mailbox was.
     */
    s64  ping_sent_ms;
    s64  ping_next_ms;
    int  ping_rtt_ms;
    u8   ping_token;
    u8   ping_inflight;

    /* Carried from JoinResponse to the in-world terminal, where the JOINED event
     * is finally emitted. */
    s32 join_playtime, join_reward_points, join_balance;

    /*
     * Remote-entity presence. The reconcile model owns the compact slot table and the per-
     * frame tile walk; in-game frames feed its targets and each pump ticks it once, draining
     * the resulting SPAWN/STEP/TURN/DESPAWN into the event queue.
     */
    openmmo_entity_mgr ent;
    u32 self_entity_id;
    int have_self;
    /*
     * The local player's own spawn tile, captured from the self LoadEntity in the same burst
     * as self_entity_id.
     */
    int self_x, self_z;
    int have_self_tile;
    int self_dir;         /* engine DIR_* the self LoadEntity placed the player facing */
    /*
     * What the server says the local player is riding: the transportation byte from its own
     * spawn and from every EntityTransportation (0x28) after it.
     */
    int self_mount;
    /* A server-driven warp is in flight: set when the MapTransition signal arrives
     * in-world and cleared at the arrival terminal, where it selects a WARP event
     * over a JOINED one. Between the two the enter-window handler is reused to
     * consume the same arrival burst a fresh join does. */
    int warping;
    /* The server's authoritative overworld state, accumulated across the
     * select/resync world-state block and marked valid when LoadMap terminates
     * it. A host seats the session from this on JOINED. */
    openmmo_world_state ws;
    /* The server-owned progression store (see openmmo_story_store): the world-flag
     * table, the set story flags and the story variables, seeded from the
     * world-state block and updated by live StoryFlagUpdate deltas. Read-only to a
     * host; the client never writes it back to the server. */
    openmmo_story_store story;
    /* The local script VM's own flags and vars (see openmmo_script_state).
     * Seated by the server's 0xCB on join; the deltas after that run the other
     * way, because the VM writing them is this client's. */
    openmmo_script_state script_state;
    /* What the server said about the last save this client offered (0xCE).
     * `import_answer.status` is MMO_IMPORT_STATUS_NONE until one arrives. */
    openmmo_import_answer import_answer;
    /* The server-owned party (see openmmo_party), seated from every party
     * container the server sends and never written back. */
    openmmo_party party;
    /* The server-owned PC (see openmmo_storage), seated from every PC container.
     * `mon` is allocated on the first one that carries a record and freed with
     * the client; `ct_scratch` holds one packet's worth of decoded records, which
     * is too large to stand on the stack. */
    openmmo_storage storage;
    /* Who the server says is boarding at the day care (see openmmo_storage),
     * seated from every day care container. Two slots, so `mon` is allocated at
     * OPENMMO_DAYCARE_MAX rather than the PC's 660. */
    openmmo_storage daycare;
    /* The server-owned bag (see openmmo_bag), seated from every 0x40 snapshot
     * and every 0x42 stack update, and never written back. */
    openmmo_bag bag;
    /* The server-owned mart (see openmmo_shop), seated from every 0x23
     * catalog and never written back. */
    openmmo_shop shop;
    /* The last dialog box (see openmmo_dialog), seated from every 0x21. */
    openmmo_dialog dialog;
    /* The last scripted movement sequence (see openmmo_script_move). */
    openmmo_script_move script_move;
    /* Sequences that have arrived and not yet been taken. */
    openmmo_script_move script_move_q[OPENMMO_SCRIPT_MOVE_QUEUE];
    int script_move_head, script_move_live;
    /* Map / cutscene npcs (see openmmo_npc_store). */
    openmmo_npc_store npcs;
    /* The server-owned objective store (see openmmo_objectives). */
    openmmo_objectives objectives;
    /* The server-owned friends list (see openmmo_friends). */
    openmmo_friends friends;
    /* The server-owned guild (see openmmo_guild). */
    openmmo_guild guild;
    /* The server-owned mailbox (see openmmo_mail). */
    openmmo_mail mail;
    /* The server-owned link (see openmmo_link). */
    openmmo_link link;
    /* The last server-driven UI outside dialog (see openmmo_ui). */
    openmmo_ui ui;
    /* The last digest / transfer / stream / image (see openmmo_sync). */
    openmmo_sync sync;
    u8 *sync_xfer;
    size_t sync_xfer_cap;
    /* The last rental offer, tournament page, bracket and board
     * (see openmmo_compete). */
    openmmo_compete compete;
    openmmo_gm gm;
    mmo_monster *ct_scratch;
    /*
     * Where the session stands between the overworld and a battle. The server drives every
     * transition (see apply_self_presence); the client only owes it the one acknowledgement
     * that says the overworld is back.
     */
    openmmo_battle_state battle;
    openmmo_battle_field field;
    openmmo_battle_anims anims;
    /* The move offer the server is waiting on, held as the event a host reads it
     * from. Only one is ever open, the server sends the next move a level up
     * taught once this one is answered, so a single slot is the whole model.
     * `have_move_learn` is 0 when nothing is owed. */
    openmmo_event move_learn;
    int have_move_learn;
    /*
     * The challenge the server is holding in front of this player, and the native link battle
     * an accepted one opens. Only one of each is ever open: the server refuses a second
     * challenge while one stands, and a client is in at most one battle.
     */
    mmo_duel_invite duel;
    int have_duel;
    /* The trade table and the trade link shelf, each the last word the server
     * sent. The goods never live in either: a settled trade or a bought
     * monster arrives as a container resend, a bought stack as a bag update. */
    openmmo_trade trade;
    mmo_trade_comm trade_comm[OPENMMO_TRADE_COMM_QUEUE];
    int trade_comm_head;
    int trade_comm_count;
    int trade_comm_dropped;
    openmmo_gtl gtl;
    openmmo_link_battle link_battle;
    mmo_link_battle_data link_blob[OPENMMO_LINK_QUEUE];
    int link_head;
    int link_count;
    int link_dropped;
    /*
     * The Underground's own second half, queued the same way and for the same reason: the
     * cavern's menus are driven by an engine frame, not by the event queue, and a
     * conversation's traffic is bursty (a whole trainer-case record crosses in one command).
     */
    mmo_underground_talk ug_talk[OPENMMO_UG_TALK_QUEUE];
    int ug_talk_head;
    int ug_talk_count;
    int ug_talk_dropped;
    /* The link contest's group and its relay, queued for the same reason. A
     * contest is bursty in the same way: a whole party of four monsters and a
     * recorded Chatot cry each cross in one command. */
    openmmo_contest contest;
    mmo_contest_comm contest_q[OPENMMO_CONTEST_QUEUE];
    int contest_head;
    int contest_count;
    int contest_dropped;
    /* The evolution the server is waiting on, held the same way. A second prompt
     * replaces the open one rather than queueing behind it: the game client's
     * evolution screen disposes of the one it is showing when a new one arrives. */
    openmmo_event evolution;
    int have_evolution;
    /* The server-owned breeding surface: the incubator slots it last sent and the
     * last forecast it answered a pairing with. Neither is ever computed here.
     * `hatched` is filled by seat_party with the party slots whose egg flag the
     * server has just cleared, for the live path to raise events from. */
    openmmo_incubators incubators;
    openmmo_breeding_forecast forecast;
    int hatched[OPENMMO_PARTY_MAX];
    int hatched_count;
    /* The preloaded-map registry (see mmo_map_grid): the current map and its
     * neighbours, refreshed from every LoadMap and reset on a warp. `region` is the
     * region all entries share (a connection stays inside one region). */
    mmo_map_grid maps[MMO_MAP_REG_MAX];
    int nmaps;
    int map_region;
    /*
     * Scratch for decoding one game frame. The S->C inflater is stateful, so every compressed
     * frame in the join-to-world burst must be inflated in full even when its body is ignored;
     * this holds the largest such frame.
     */
    u8 *gbuf;
    size_t gbuf_cap;

    openmmo_event evq[EVQ_CAP];
    int           evq_head, evq_tail;

    openmmo_event chat_log[OPENMMO_CHAT_LOG];
    int           chat_head, chat_count;
};

/* --- event queue ------------------------------------------------------- */

static openmmo_event *evq_push(openmmo_client *c, openmmo_event_kind kind)
{
    int next = (c->evq_tail + 1) % EVQ_CAP;
    if (next == c->evq_head)
        return NULL; /* full: drop rather than block (should never happen) */
    openmmo_event *e = &c->evq[c->evq_tail];
    memset(e, 0, sizeof *e);
    e->kind = kind;
    c->evq_tail = next;
    return e;
}

static void set_status(openmmo_client *c, openmmo_status s)
{
    if (c->status == s)
        return;
    c->status = s;
    openmmo_event *e = evq_push(c, OPENMMO_EV_STATUS);
    if (e)
        e->status = s;
}

/* Enter a terminal failure state, queueing the reason. */
static void fail(openmmo_client *c, const char *fmt_msg)
{
    c->ph = P_FAILED;
    set_status(c, OPENMMO_FAILED);
    openmmo_event *e = evq_push(c, OPENMMO_EV_FAILED);
    if (e)
        snprintf(e->message, sizeof e->message, "%s", fmt_msg);
}

/* A live session (authed or in-game) was closed by the peer: not a failure to
 * connect, but the end of a session that was up. */
static void disconnected(openmmo_client *c, const char *why)
{
    c->ph = P_DEAD;
    set_status(c, OPENMMO_DISCONNECTED);
    openmmo_event *e = evq_push(c, OPENMMO_EV_DISCONNECTED);
    if (e)
        snprintf(e->message, sizeof e->message, "%s", why);
}

/* --- timers ------------------------------------------------------------ */

/* Milliseconds since the epoch. gettimeofday is what the rest of this library
 * already uses for a timestamp (session.c, main.c) and is the one call both
 * hosts have; nothing here needs it to be monotonic, only to be the same clock
 * either side of a round trip. */
static s64 now_ms(void)
{
    struct timeval tv;

    gettimeofday(&tv, NULL);
    return (s64)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

/* mmo_plat_seconds, not time(): the fused binary links the DS network
 * library, whose own time() answers 0 for the whole run (platform.h), so
 * these deadlines had never once fired inside the game, a hung connect sat
 * forever, which an auto-rejoin between wifi outages cannot afford. */
static void arm_deadline(openmmo_client *c)
{
    c->deadline = (time_t)mmo_plat_seconds() + c->timeout_s;
}

/* --- framing ----------------------------------------------------------- */

/* Move everything the socket has received into the frame accumulator. */
static void drain_rx(openmmo_client *c)
{
    size_t avail = mmo_net_available(&c->net);
    while (avail > 0) {
        u8 chunk[4096];
        size_t take = avail < sizeof chunk ? avail : sizeof chunk;
        size_t got = mmo_net_recv(&c->net, chunk, take);
        if (got == 0)
            break;
        mmo_buf *b = &c->acc;
        /* grow acc and append */
        if (b->len + got > b->cap) {
            size_t want = b->cap ? b->cap : 256;
            while (want < b->len + got)
                want *= 2;
            u8 *p = realloc(b->data, want);
            if (!p)
                return; /* out of memory: leave what we have; a wait will time out */
            b->data = p;
            b->cap = want;
        }
        memcpy(b->data + b->len, chunk, got);
        b->len += got;
        avail -= got;
    }
}

/* Try to pull one whole frame out of the accumulator without blocking. Returns
 * MMO_FRAME_OK with body/blen set (valid until the next call), MMO_FRAME_SHORT
 * if not enough has arrived yet, or MMO_FRAME_BAD on a malformed length. */
static mmo_frame_result next_frame(openmmo_client *c, const u8 **body, size_t *blen)
{
    mmo_rbuf r;
    mmo_rbuf_init(&r, c->acc.data + c->acc.head, c->acc.len - c->acc.head);
    mmo_frame_result fr = mmo_frame_get(&r, body, blen);
    if (fr == MMO_FRAME_OK) {
        c->acc.head += r.pos;
        /* compact once fully consumed so head does not grow without bound */
        if (c->acc.head == c->acc.len)
            c->acc.head = c->acc.len = 0;
    }
    return fr;
}

/* --- handshake steps --------------------------------------------------- */

/* Begin the session handshake on the (already connected) socket: generate the
 * ephemeral keypair, queue a framed ClientHello, and move to the hello-wait
 * phase named by `next`. Resets the session and inflater for a fresh stream. */
static void begin_handshake(openmmo_client *c, phase next, openmmo_status st)
{
    memset(&c->sess, 0, sizeof c->sess);
    mmo_game_stream_init(&c->gstream);
    mmo_net_read_reset(&c->acc);

    mmo_wbuf out;
    mmo_wbuf_init(&out);
    if (mmo_session_start_at(&c->sess, &out, c->pinned_hello_time) != 0) {
        mmo_wbuf_free(&out);
        fail(c, c->sess.errmsg[0] ? c->sess.errmsg : "handshake: no entropy");
        return;
    }
    if (mmo_net_send(&c->net, out.data, out.len) != 0) {
        mmo_wbuf_free(&out);
        fail(c, "handshake: could not queue ClientHello");
        return;
    }
    mmo_wbuf_free(&out);
    c->ph = next;
    set_status(c, st);
    arm_deadline(c);
}

/* On a ServerHello: verify + derive, queue the framed ClientReady. Returns 0 on
 * success (crypto installed), -1 having already failed the client. */
static int on_server_hello(openmmo_client *c, const u8 *body, size_t blen)
{
    mmo_wbuf ready;
    mmo_wbuf_init(&ready);
    if (mmo_session_on_server_hello(&c->sess, body, blen, &ready) != 0) {
        mmo_wbuf_free(&ready);
        fail(c, c->sess.errmsg[0] ? c->sess.errmsg : "handshake: ServerHello rejected");
        return -1;
    }
    int rc = mmo_net_send(&c->net, ready.data, ready.len);
    mmo_wbuf_free(&ready);
    if (rc != 0) {
        fail(c, "handshake: could not queue ClientReady");
        return -1;
    }
    return 0;
}

/* --- application sends ------------------------------------------------- */

static void send_login_request(openmmo_client *c)
{
    char pwhex[41];
    u8 token[MMO_LOGIN_TOKEN_MAX];
    size_t tokenlen = 0;

    mmo_wbuf body;
    mmo_wbuf_init(&body);
    /*
     * A password when there is one, and it always asks to be remembered, so the next session
     * has a token and the player types it once. With no password, the token is the credential:
     * that is the case where nothing was typed, which is the whole point of keeping one.
     */
    if (c->pass[0] == '\0')
        tokenlen = mmo_token_load(c->user, token, sizeof token);
    c->used_token = tokenlen > 0;
    /* Nothing to sign in with. */
    if (!c->used_token && c->pass[0] == '\0') {
        mmo_wbuf_free(&body);
        fail(c, c->user[0] == '\0'
                ? "no account name was given."
                : "no password was given, and this device has no saved "
                  "sign-in for that account.");
        return;
    }
    if (c->used_token) {
        mmo_login_write_token_request(&body, c->user, token, tokenlen);
    } else {
        mmo_sha1_hex(c->pass, strlen(c->pass), pwhex);
        mmo_login_write_request(&body, c->user, pwhex, 1);
    }

    mmo_wbuf frame;
    mmo_wbuf_init(&frame);
    mmo_session_send_app(&c->sess.crypto, MMO_LOGIN_OP_REQUEST,
                         body.data, body.len, &frame);
    mmo_wbuf_free(&body);
    if (frame.err || mmo_net_send(&c->net, frame.data, frame.len) != 0) {
        mmo_wbuf_free(&frame);
        fail(c, "login: could not send LoginRequest");
        return;
    }
    mmo_wbuf_free(&frame);
    c->ph = P_LOGIN_AUTH;
    set_status(c, OPENMMO_AUTHENTICATING);
    arm_deadline(c);
}

/* Send one login-stream RPC body under the encrypted envelope and move to `next`. */
static void send_login_rpc(openmmo_client *c, u8 opcode,
                           const mmo_wbuf *body, phase next, const char *ctx)
{
    mmo_wbuf frame;
    mmo_wbuf_init(&frame);
    mmo_session_send_app(&c->sess.crypto, opcode, body->data, body->len, &frame);
    if (frame.err || mmo_net_send(&c->net, frame.data, frame.len) != 0) {
        mmo_wbuf_free(&frame);
        fail(c, ctx);
        return;
    }
    mmo_wbuf_free(&frame);
    c->ph = next;
    set_status(c, OPENMMO_REQUESTING_GAME);
    arm_deadline(c);
}

static void request_game_list(openmmo_client *c)
{
    mmo_wbuf body;
    mmo_wbuf_init(&body);
    mmo_handoff_write_reqlist(&body);
    send_login_rpc(c, MMO_LOGIN_OP_REQ_GS_LIST, &body, P_HANDOFF_LIST,
                   "handoff: could not request the game-server list");
    mmo_wbuf_free(&body);
}

static void send_join_packet(openmmo_client *c)
{
    mmo_wbuf body;
    mmo_wbuf_init(&body);
    mmo_game_write_join(&body, c->nodes.user_id, c->nodes.token, c->nodes.token_len);

    mmo_wbuf frame;
    mmo_wbuf_init(&frame);
    mmo_game_send(&c->sess.crypto, MMO_GAME_OP_JOIN, body.data, body.len, &frame);
    mmo_wbuf_free(&body);
    if (frame.err || mmo_net_send(&c->net, frame.data, frame.len) != 0) {
        mmo_wbuf_free(&frame);
        fail(c, "join: could not send JoinPacket");
        return;
    }
    mmo_wbuf_free(&frame);
    c->ph = P_GAME_JOIN;
    arm_deadline(c);
}

/* Frame one game packet through the session envelope and queue it. Returns 0, or
 * -1 having failed the client with `ctx`. */
static int send_game_packet(openmmo_client *c, u8 opcode,
                            const u8 *body, size_t blen, const char *ctx)
{
    mmo_wbuf frame;
    mmo_wbuf_init(&frame);
    mmo_game_send(&c->sess.crypto, opcode, body, blen, &frame);
    int bad = frame.err || mmo_net_send(&c->net, frame.data, frame.len) != 0;
    mmo_wbuf_free(&frame);
    if (bad) {
        fail(c, ctx);
        return -1;
    }
    return 0;
}

/* How long a ping may go unanswered before it is written off. */
#define MMO_PING_INTERVAL_MS 2000
#define MMO_PING_TIMEOUT_MS  6000

static void ping_tick(openmmo_client *c)
{
    s64 now;

    if (c->ph != P_IN_GAME) {
        c->ping_inflight = 0;
        return;
    }

    now = now_ms();
    if (c->ping_inflight) {
        if (now - c->ping_sent_ms < MMO_PING_TIMEOUT_MS)
            return;
        c->ping_inflight = 0;
        c->ping_rtt_ms = MMO_PING_TIMEOUT_MS;
    }
    if (c->ping_next_ms != 0 && now < c->ping_next_ms)
        return;

    {
        mmo_wbuf body;
        int rc;

        mmo_wbuf_init(&body);
        c->ping_token++;
        mmo_game_write_keepalive(&body, c->ping_token);
        rc = body.err ? -1
                      : send_game_packet(c, MMO_GAME_OP_KEEPALIVE, body.data,
                                         body.len,
                                         "ping: could not send KeepAlive");
        mmo_wbuf_free(&body);
        /* A ping that would not go out is not a session failure, the socket's
         * own error path already owns that, so it is simply not counted. */
        if (rc == 0) {
            c->ping_sent_ms = now;
            c->ping_inflight = 1;
        }
        c->ping_next_ms = now + MMO_PING_INTERVAL_MS;
    }
}

int openmmo_client_latency_ms(const openmmo_client *c)
{
    if (c == NULL || c->ph != P_IN_GAME)
        return -1;
    return c->ping_rtt_ms;
}

/* Tear down the login socket and open the game socket for the advertised
 * target. The host is what login handed back; the login address is only the
 * fallback for an advertisement that names none, which is why it is decoded
 * here at all. The build's game port still overrides the advertised one. */
static void connect_game_server(openmmo_client *c)
{
    char game_host[64];
    char login_host[OPENMMO_ENDPOINT_HOST_MAX];
    char why[128];
    int picked;

    mmo_net_close(&c->net);
    mmo_net_read_reset(&c->acc);
    openmmo_endpoint_host(login_host, sizeof login_host);
    picked = mmo_handoff_pick_host(&c->nodes, login_host, game_host,
                                   sizeof game_host, why, sizeof why);
    openmmo_endpoint_forget(login_host, sizeof login_host);
    /*
     * The development seam beside OPENMMO_GAMEPORT: a build from this tree dials the game
     * server named here instead of the advertised one, and a release reads it as unset
     * (endpoint.h's rule: it changes where the program goes).
     */
    {
        const char *gh = openmmo_dev_env("OPENMMO_GAMEHOST");

        if (gh != NULL && gh[0] != '\0' && strlen(gh) < sizeof game_host) {
            snprintf(game_host, sizeof game_host, "%s", gh);
            picked = 0;
        }
    }
    if (picked != 0) {
        fail(c, why[0] ? why : "handoff: no game-server address");
        return;
    }
    u16 port = c->game_port ? c->game_port
                            : (c->nodes.node_port ? c->nodes.node_port
                                                  : c->nodes.port);
    if (mmo_net_connect(&c->net, game_host, port) != 0) {
        fail(c, c->net.errmsg[0] ? c->net.errmsg : "join: could not connect the game server");
        return;
    }
    c->ph = P_GAME_CONNECT;
    set_status(c, OPENMMO_JOINING_GAME);
    arm_deadline(c);
}

/* --- inbound frame handlers -------------------------------------------- */

static void handle_login_response(openmmo_client *c, const u8 *body, size_t blen)
{
    u8 plain[1024];
    size_t n = mmo_session_recv_app(&c->sess.crypto, body, blen, plain, sizeof plain);
    if (n == (size_t)-1) { fail(c, "login: reply failed checksum/decrypt"); return; }
    /*
     * The credentials packet comes before the response it belongs to, so this is not the reply
     * yet: take the token and keep waiting for one.
     */
    if (n >= 1 && plain[0] == MMO_LOGIN_OP_CREDENTIALS) {
        u8 token[MMO_LOGIN_TOKEN_MAX];
        size_t tokenlen = mmo_login_read_credentials(plain + 1, n - 1,
                                                     token, sizeof token);

        if (tokenlen > 0 && mmo_token_save(c->user, token, tokenlen) != 0)
            fprintf(stderr, "openmmo: the sign-in token could not be saved;"
                            " this account will want its password again\n");
        arm_deadline(c);
        return;
    }
    if (n < 1 || plain[0] != MMO_LOGIN_OP_RESPONSE) {
        fail(c, "login: unexpected reply opcode"); return;
    }
    int state = mmo_login_read_response_state(plain + 1, n - 1);
    if (state != MMO_LOGIN_AUTHED) {
        char why[128];

        c->login_refusal = state;

        /*
         * A token the server will not take is spent: drop it so the next start asks for a
         * password instead of failing the same way for ever. Nothing is retried here, because
         * this client has no password to retry with; that is why it was using a token.
         */
        if (state == MMO_LOGIN_INVALID_SAVED_CREDENTIALS && c->used_token) {
            mmo_token_clear();
            fail(c, "the saved sign-in has expired. Enter your password to "
                    "sign in again.");
            return;
        }
        mmo_login_explain_refusal(state, why, sizeof why);
        fail(c, why);
        return;
    }

    if (c->mode == OPENMMO_MODE_LOGIN_HOLD) {
        c->ph = P_LOGIN_HELD;
        set_status(c, OPENMMO_AUTHED);
    } else {
        set_status(c, OPENMMO_AUTHED);
        request_game_list(c);
    }
}

static void handle_gs_list(openmmo_client *c, const u8 *body, size_t blen)
{
    u8 plain[2048];
    size_t n = mmo_session_recv_app(&c->sess.crypto, body, blen, plain, sizeof plain);
    if (n == (size_t)-1) { fail(c, "handoff: GameServerList failed checksum/decrypt"); return; }
    if (n < 1 || plain[0] != MMO_LOGIN_OP_GS_LIST) {
        fail(c, "handoff: expected GameServerList"); return;
    }
    mmo_gameserver srv;
    if (mmo_handoff_read_serverlist(plain + 1, n - 1, &srv) != 0 || !srv.found) {
        fail(c, "handoff: no joinable game server advertised"); return;
    }
    mmo_wbuf join;
    mmo_wbuf_init(&join);
    mmo_handoff_write_join(&join, srv.id);
    send_login_rpc(c, MMO_LOGIN_OP_JOIN_GS, &join, P_HANDOFF_NODES,
                   "handoff: could not request a game ticket");
    mmo_wbuf_free(&join);
}

static void handle_gs_nodes(openmmo_client *c, const u8 *body, size_t blen)
{
    u8 plain[2048];
    size_t n = mmo_session_recv_app(&c->sess.crypto, body, blen, plain, sizeof plain);
    if (n == (size_t)-1) { fail(c, "handoff: GameServerNodes failed checksum/decrypt"); return; }
    if (n < 1 || plain[0] != MMO_LOGIN_OP_GS_NODES) {
        fail(c, "handoff: expected GameServerNodes"); return;
    }
    if (mmo_handoff_read_nodes(plain + 1, n - 1, &c->nodes) != 0) {
        fail(c, "handoff: malformed GameServerNodes"); return;
    }
    if (c->nodes.login_state != MMO_LOGIN_AUTHED) {
        fail(c, "handoff: ticket refused"); return;
    }
    connect_game_server(c);
}

/* Decode one whole game frame into c->gbuf, advancing the stateful inflater.
 * Returns the body length and writes the opcode, or (size_t)-1 on failure. */
static size_t recv_game_frame(openmmo_client *c, const u8 *body, size_t blen, u8 *opcode)
{
    return mmo_game_recv(&c->sess.crypto, &c->gstream, body, blen,
                         opcode, c->gbuf, c->gbuf_cap);
}

/* JoinResponse accepted: the account is in, but the client is not yet standing in
 * a map. Stash the stats for the eventual JOINED event and start the character-
 * select round-trip by asking for the character list. */
static void handle_join_response(openmmo_client *c, const u8 *body, size_t blen)
{
    u8 opcode;
    size_t n = recv_game_frame(c, body, blen, &opcode);
    if (n == (size_t)-1) { fail(c, "join: JoinResponse failed checksum/decrypt"); return; }
    if (opcode != MMO_GAME_OP_JOIN_RSP) { fail(c, "join: unexpected game opcode"); return; }
    mmo_join_response jr;
    if (mmo_game_read_join_response(c->gbuf, n, &jr) != 0) {
        fail(c, "join: malformed JoinResponse"); return;
    }
    if (!jr.can_join) { fail(c, "join: server rejected the join"); return; }

    c->join_playtime = jr.playtime;
    c->join_reward_points = jr.reward_points;
    c->join_balance = jr.balance;

    if (send_game_packet(c, MMO_GAME_OP_REQ_CHARS, NULL, 0,
                         "join: could not request the character list") != 0)
        return;
    c->ph = P_GAME_CHARLIST;
    arm_deadline(c);
}

/* Fill `out` from the session's create fields. Returns 0, or -1 after failing
 * the session when a choice cannot go on the wire. */
static int fill_create_request(openmmo_client *c, mmo_create_character *out)
{
    memset(out, 0, sizeof *out);
    out->name = c->create_name;
    out->gender = c->create_gender;
    out->starting_region = c->create_region < 0
                               ? mmo_region_default()
                               : c->create_region;
    out->appearance.region_selection_index =
        c->create_skin_region < 0 ? out->starting_region : c->create_skin_region;

    if (c->create_name_overflow) {
        fail(c, "create: a name is longer than 32 characters");
        return -1;
    }
    if (c->create_name[0] == '\0') {
        fail(c, "create: a name is required");
        return -1;
    }
    if (c->create_gender != 0 && c->create_gender != 1) {
        fail(c, "create: gender is 0 (male) or 1 (female)");
        return -1;
    }
    if (!c->allow_undrawable_region &&
        !mmo_region_is_selectable(out->starting_region)) {
        char why[128];
        if (mmo_region_explain_undrawable(out->starting_region, why, sizeof why) < 0)
            snprintf(why, sizeof why, "a character cannot be made in %s.",
                     mmo_region_name(out->starting_region));
        fail(c, why);
        return -1;
    }

    for (int i = 0; i < MMO_SKIN_SLOTS; i++) {
        if ((c->create_skin_mask & (u16)(1u << i)) == 0)
            continue;
        out->appearance.slot[i].present = 1;
        out->appearance.slot[i].type =
            (u16)(c->create_skin_word[i] & MMO_SKIN_TYPE_MASK);
        out->appearance.slot[i].color =
            (u8)((c->create_skin_word[i] >> MMO_SKIN_COLOR_SHIFT) &
                 MMO_SKIN_COLOR_MASK);
    }
    return 0;
}

static void send_create_character(openmmo_client *c)
{
    mmo_create_character req;
    if (fill_create_request(c, &req) != 0)
        return;

    mmo_wbuf body;
    mmo_wbuf_init(&body);
    if (mmo_game_write_create_character(&body, &req) != 0) {
        mmo_wbuf_free(&body);
        fail(c, "create: could not encode the character");
        return;
    }
    int rc = send_game_packet(c, MMO_GAME_OP_CREATE_CHAR, body.data, body.len,
                              "create: could not send CreateCharacter");
    mmo_wbuf_free(&body);
    if (rc != 0)
        return;
    c->ph = P_GAME_CREATE;
    arm_deadline(c);
}

static void fire_characters(openmmo_client *c, const mmo_character_ref *ref)
{
    openmmo_event *e = evq_push(c, OPENMMO_EV_CHARACTERS);

    if (!e)
        return;
    e->character.count = ref->count;
    e->character.id = ref->id;
    snprintf(e->character.name, sizeof e->character.name, "%s",
             c->create_name);
    e->character.gender = c->create_gender;
    e->character.region = c->create_region < 0
                              ? mmo_region_default()
                              : c->create_region;
    if (c->have_chars && c->chars.held > 0) {
        int i, newest = c->chars.held - 1;

        e->character.id = c->chars.entry[newest].id;
        for (i = 0; i < c->chars.held; i++) {
            if (c->create_name[0] != '\0'
                && strcasecmp(c->chars.entry[i].name, c->create_name) == 0) {
                e->character.id = c->chars.entry[i].id;
                break;
            }
        }
    }
}

static int store_character_list(openmmo_client *c, const u8 *plain, size_t n,
                                int count)
{
    mmo_character_list list;

    memset(&list, 0, sizeof list);
    if (count > 0 && mmo_game_read_character_list(plain, n, &list) != 0)
        return -1;
    c->chars = list;
    c->have_chars = 1;
    return 0;
}

static int send_select_character(openmmo_client *c, int pick)
{
    mmo_wbuf sel;

    if (pick < 0 || pick >= c->chars.held)
        return -1;
    c->ws.character_id = c->chars.entry[pick].id;
    snprintf(c->ws.name, sizeof c->ws.name, "%s", c->chars.entry[pick].name);
    if (c->chars.entry[pick].gender == 0 || c->chars.entry[pick].gender == 1)
        c->ws.gender = c->chars.entry[pick].gender;

    mmo_wbuf_init(&sel);
    mmo_game_write_select_character(&sel, c->chars.entry[pick].id, 0);
    {
        int rc = send_game_packet(c, MMO_GAME_OP_SELECT_CHAR, sel.data, sel.len,
                                  "join: could not select a character");
        mmo_wbuf_free(&sel);
        if (rc != 0)
            return -1;
    }
    c->ph = P_GAME_SELECT;
    arm_deadline(c);
    return 0;
}

/* The account's list, one character shorter. */
static void handle_delete_result(openmmo_client *c, const u8 *body, size_t blen)
{
    u8 opcode;
    size_t n = recv_game_frame(c, body, blen, &opcode);
    int result = MMO_DELETE_REFUSED;
    s64 id = 0;

    if (n == (size_t)-1) {
        fail(c, "delete: DeleteCharacterResult failed checksum/decrypt");
        return;
    }
    if (opcode != MMO_GAME_OP_DELETE_RESULT) {
        fail(c, "delete: expected the deletion result");
        return;
    }
    if (mmo_game_read_delete_result(c->gbuf, n, &result, &id) != 0) {
        fail(c, "delete: malformed DeleteCharacterResult");
        return;
    }

    if (result != MMO_DELETE_OK) {
        /* Refused is not a broken session: the list is still whole and the
         * host is still picking. The list has to be published again anyway,
         * a screen that went into a wait for this round trip has nothing else
         * to come back on. */
        mmo_character_ref ref;

        memset(&ref, 0, sizeof ref);
        ref.count = c->chars.held;
        if (c->chars.held > 0)
            ref.id = c->chars.entry[0].id;
        snprintf(c->delete_error, sizeof c->delete_error,
                 "delete: the server would not delete that character");
        c->ph = P_GAME_PICK;
        fire_characters(c, &ref);
        return;
    }

    c->delete_error[0] = '\0';
    if (send_game_packet(c, MMO_GAME_OP_REQ_CHARS, NULL, 0,
                         "delete: could not request the character list") != 0)
        return;
    c->ph = P_GAME_CHARLIST;
    arm_deadline(c);
}

/*
 * The character list. Join walks every entry and picks by name, index, region or, with none
 * of those, the first row of the now-ordered list.
 */
static void handle_characters_list(openmmo_client *c, const u8 *body, size_t blen)
{
    u8 opcode;
    size_t n = recv_game_frame(c, body, blen, &opcode);
    if (n == (size_t)-1) { fail(c, "join: CharactersList failed checksum/decrypt"); return; }
    if (opcode != MMO_GAME_OP_CHARS_LIST) { fail(c, "join: expected the character list"); return; }

    mmo_character_ref ref;
    if (mmo_game_read_first_character(c->gbuf, n, &ref) != 0) {
        fail(c, "join: malformed CharactersList");
        return;
    }

    if (c->ph == P_GAME_CREATE) {
        if (ref.count <= c->char_count_before) {
            /*
             * A refused create is answered with the unchanged list: the packet carries no
             * field for a reason, so the reason is inferred here.
             */
            int region_ok = mmo_region_is_selectable(c->create_region < 0
                                                         ? mmo_region_default()
                                                         : c->create_region);
            const char *why = region_ok
                                  ? "create: that name is already taken"
                                  : "create: the server did not add a character in that region";
            if (c->select_how == OPENMMO_SELECT_HOLD) {
                snprintf(c->create_error, sizeof c->create_error, "%s",
                         region_ok ? "That name is already taken."
                                   : "No character can be made in that region.");
                c->ph = P_GAME_PICK;
                fire_characters(c, &ref);
                return;
            }
            fail(c, why);
            return;
        }
        c->create_error[0] = '\0';
        if (store_character_list(c, c->gbuf, n, ref.count) != 0) {
            fail(c, "create: could not walk the new character list");
            return;
        }
        fire_characters(c, &ref);
        c->ph = (c->select_how == OPENMMO_SELECT_HOLD) ? P_GAME_PICK : P_CREATED;
        return;
    }

    if (c->mode == OPENMMO_MODE_CREATE_CHAR) {
        c->char_count_before = ref.count;
        send_create_character(c);
        return;
    }

    if (c->select_how == OPENMMO_SELECT_HOLD) {
        if (store_character_list(c, c->gbuf, n, ref.count) != 0) {
            fail(c, "join: could not walk the character list");
            return;
        }
        c->create_error[0] = '\0';
        fire_characters(c, &ref);
        c->ph = P_GAME_PICK;
        return;
    }

    if (ref.count == 0) {
        fail(c, "join: this account has no character yet");
        return;
    }

    if (c->select_how == OPENMMO_SELECT_MANY) {
        fail(c, "join: say one of --character, --index or --region");
        return;
    }
    if (c->select_how == OPENMMO_SELECT_BAD) {
        fail(c, "join: unknown region");
        return;
    }

    if (store_character_list(c, c->gbuf, n, ref.count) != 0) {
        fail(c, "join: could not walk the character list");
        return;
    }

    char pick_err[96];
    const char *want_name =
        c->select_how == OPENMMO_SELECT_NAME ? c->select_name : NULL;
    int want_index =
        c->select_how == OPENMMO_SELECT_INDEX ? c->select_index : -1;
    int want_region =
        c->select_how == OPENMMO_SELECT_REGION ? c->select_region : -1;
    int pick = mmo_game_pick_character(&c->chars, want_name, want_index,
                                       want_region, pick_err, sizeof pick_err);
    if (pick < 0) {
        char msg[128];
        snprintf(msg, sizeof msg, "join: %s",
                 pick_err[0] ? pick_err : "could not pick a character");
        fail(c, msg);
        return;
    }

    /* The row we asked to be. SelectedCharacter (s2c 0x04) confirms the same
     * CharacterInfo once the server accepts; until then this is who we are. */
    send_select_character(c, pick);
}

/*
 * Seat one ScriptState body (0xCB) into the local script VM's store. The seat the server sends
 * on join is absolute, every flag and var it holds, so it replaces the store rather than
 * merging into it.
 */
/* The server's one answer to a save this client offered. Recorded, never acted
 * on here: what happens next is the caller's, the CLI prints it, and the
 * window's front door shows it beside the row the player pressed. */
static void seat_import_answer(openmmo_client *c, size_t n)
{
    openmmo_import_answer *a = &c->import_answer;

    if (mmo_game_read_offline_result(c->gbuf, n, &a->status,
                                     a->message, sizeof a->message,
                                     &a->notes[0][0], sizeof a->notes[0],
                                     MMO_IMPORT_NOTE_MAX,
                                     &a->nnotes, &a->nnotes_sent,
                                     &a->wants_chain) != 0) {
        a->status = MMO_IMPORT_STATUS_NONE;
        a->nnotes = 0;
        a->nnotes_sent = 0;
        a->wants_chain = 0;
        return;
    }
    /*
     * A status this build has no word for reads as no answer at all, which is what a caller
     * waiting on one already handles.
     */
    if (a->status < 0 || a->status >= MMO_IMPORT_STATUS_COUNT)
        a->status = MMO_IMPORT_STATUS_NONE;
}

static void seat_script_state(openmmo_client *c, size_t n)
{
    /* Static, not automatic: a whole seat is 9.9 KB of scratch and this runs on
     * the engine's own stack. Safe because a seat is decoded on the one thread
     * that pumps the socket, and nothing keeps a pointer past this function. */
    static mmo_script_flag flags[MMO_SCRIPT_FLAG_MAX];
    static mmo_script_var vars[MMO_SCRIPT_VAR_MAX];
    mmo_save_block blocks[MMO_SAVE_BLOCK_MAX];
    int nflags = 0, fstored = 0, nvars = 0, vstored = 0;
    int nblocks = 0, bstored = 0;

    if (mmo_game_read_script_state(c->gbuf, n,
                                   flags, (int)(sizeof flags / sizeof flags[0]),
                                   &nflags, &fstored,
                                   vars, (int)(sizeof vars / sizeof vars[0]),
                                   &nvars, &vstored,
                                   blocks, MMO_SAVE_BLOCK_MAX,
                                   &nblocks, &bstored) != 0)
        return;

    unsigned seq = c->script_state.seq + 1;

    memset(&c->script_state, 0, sizeof c->script_state);
    c->script_state.seated = 1;
    c->script_state.seq = seq;
    c->script_state.out_of_range = (nflags - fstored) + (nvars - vstored);

    /*
     * The blocks are copied out of the frame here: the reader left them pointing into
     * `c->gbuf`, which the next packet reuses, and the field this seats into does not exist
     * yet, a seat lands ahead of the map.
     */
    c->script_state.blocks_dropped = nblocks - bstored;
    for (int i = 0; i < bstored; i++) {
        int at = c->script_state.block_count;

        if (blocks[i].len > MMO_SAVE_BLOCK_BYTES) {
            c->script_state.blocks_dropped++;
            continue;
        }
        c->script_state.block[at].id = blocks[i].id;
        c->script_state.block[at].len = blocks[i].len;
        if (blocks[i].len > 0)
            memcpy(c->script_state.block[at].data, blocks[i].data,
                   (size_t)blocks[i].len);
        c->script_state.block_count++;
    }

    for (int i = 0; i < fstored; i++) {
        int id = flags[i].id;
        if (id == MMO_SCRIPT_FLAG_RUNNING_SHOES) {
            /* A synthetic id: engine state outside VarsFlags, carried in the
             * same record. Not a table mismatch. */
            c->script_state.running_shoes = flags[i].on ? 1 : 0;
            continue;
        }
        if (id >= MMO_SCRIPT_FLAG_BADGE_BASE
            && id < MMO_SCRIPT_FLAG_BADGE_BASE + MMO_SCRIPT_BADGE_COUNT) {
            if (flags[i].on)
                c->script_state.badges |= 1u << (id - MMO_SCRIPT_FLAG_BADGE_BASE);
            continue;
        }
        if (id < 0 || id >= MMO_SCRIPT_FLAG_MAX) {
            c->script_state.out_of_range++;
            continue;
        }
        if (flags[i].on) {
            c->script_state.flag[id >> 3] |= (u8)(1u << (id & 7));
            c->script_state.flags_set++;
        }
    }
    for (int i = 0; i < vstored; i++) {
        int id = (int)vars[i].id - MMO_SCRIPT_VAR_BASE;
        if ((int)vars[i].id == MMO_SCRIPT_VAR_RESPAWN) {
            /* Synthetic, like the shoes: the engine's black-out warp id. */
            c->script_state.respawn = vars[i].value;
            continue;
        }
        if (id < 0 || id >= MMO_SCRIPT_VAR_MAX) {
            c->script_state.out_of_range++;
            continue;
        }
        c->script_state.var[id] = vars[i].value;
        if (vars[i].value != 0)
            c->script_state.vars_set++;
    }
}

/* Set or clear one story flag in the store's set-flag list. Only set flags are
 * held, so a set adds the id (if absent) and a clear swap-removes it. An overflow
 * of the held set is recorded in flag_overflow rather than dropped silently. */
static void story_set_flag(openmmo_story_store *s, int flag_id, int enabled)
{
    for (int i = 0; i < s->set_flag_count; i++) {
        if (s->set_flag[i] == flag_id) {
            if (!enabled)
                s->set_flag[i] = s->set_flag[--s->set_flag_count];
            return;
        }
    }
    if (!enabled)
        return;                          /* clearing a flag that was not set */
    if (s->set_flag_count >= OPENMMO_STORY_FLAG_MAX) {
        s->flag_overflow++;
        return;
    }
    s->set_flag[s->set_flag_count++] = flag_id;
}

/* Fold one monster into `d`, crossing the id map on the way: the species is the
 * only field a Gen-3 id makes unreadable, and an unmapped one is counted rather
 * than guessed. The party and the PC hold the same record, so both seat one
 * through here. */
static void seat_mon(openmmo_party_mon *d, const mmo_monster *m)
{
    const char *why = NULL;
    memset(d, 0, sizeof *d);
    d->id = m->id;
    d->dex_id = m->dex_id;
    d->species = mmo_id_species_from_server(m->dex_id, &why);
    d->slot = m->slot;
    d->level = m->level;
    d->hp = m->hp;
    d->xp = m->xp;
    d->egg = m->egg;
    snprintf(d->nickname, sizeof d->nickname, "%s", m->nickname);
    snprintf(d->ot, sizeof d->ot, "%s", m->ot);
    for (int i = 0; i < 4; i++) {
        d->move_id[i] = m->move_id[i];
        d->move_pp[i] = m->move_pp[i];
        d->move_pp_up[i] = m->move_pp_up[i];
        /* An empty slot is 0 on the wire and stays 0; a move the id map has no
         * verified engine correspondence for is counted, not drawn as some other
         * move. */
        d->move[i] = 0;
        if (m->move_id[i]) {
            why = NULL;
            d->move[i] = mmo_id_move_from_server(m->move_id[i], &why);
            if (d->move[i] == MMO_ID_NONE)
                d->move_unmapped++;
        }
    }
    for (int i = 0; i < 6; i++) {
        d->ev[i] = m->ev[i];
        d->iv[i] = m->iv[i];
    }
    for (int i = 0; i < MMO_MON_CONDITIONS; i++)
        d->cond[i] = m->cond[i];
    d->sheen = m->sheen;
    d->ribbons_super = m->ribbons_super;
    /* Straight across as an unsigned word: the record's field is signed on the
     * wire, but it is a personality, and the nature both sides read out of it
     * is `(u32)seed % 25` rather than a signed remainder. */
    d->seed = (u32)m->seed;
    d->nature = m->nature;
    d->friendship = m->friendship;
    /* The item it is carrying, crossed here with every other id. */
    d->held_item = m->held_item;
    d->held_item_engine = 0;
    if (m->held_item) {
        why = NULL;
        /* MMO_ID_NONE is 0, which is exactly "carrying nothing" to the engine,
         * so a refused crossing needs no second branch. */
        d->held_item_engine = mmo_id_item_from_server((u16)m->held_item, &why);
    }
    d->form = m->form;
    d->shiny = (m->rarity & (MMO_RARITY_SHINY | MMO_RARITY_SECRET_SHINY)) != 0;
    d->ability_slot = m->ability_slot;
    d->ability_unrenderable = m->ability_slot == MMO_ABILITY_SLOT_HIDDEN;
    /* The place is crossed here, with every other id: the record carries the
     * server's region/bank/map and the engine wants its own header. A record
     * with no place, or one naming a map this engine does not draw, leaves -1
     * and the seat gives the engine what official gives for an unknown place. */
    d->caught_map_header = -1;
    if (m->caught_map >= 0) {
        why = NULL;
        d->caught_map_header = mmo_id_map_header_from_server(
            m->caught_region, m->caught_bank, m->caught_map, &why);
    }
    /* Straight across, with no id map: a label is an index into the engine's own
     * location names and the save it was read from is a save this engine wrote,
     * so the number already means what it says. */
    d->caught_location_label = m->caught_location_label;
    d->caught_at = m->caught_at;
    /* The engine's own condition word, straight across for the same reason: it
     * is the engine's number to begin with and the server keeps it as one. */
    d->status = m->status;
}

/* Recount what the engine cannot draw, over the party as it now stands. Counted
 * rather than tracked as members are seated: a merge rewrites one slot in place,
 * and a count kept incrementally through that drifts. */
static void party_count_unmapped(openmmo_party *p)
{
    p->untranslatable = 0;
    p->unmapped_moves = 0;
    p->hidden_abilities = 0;
    for (int i = 0; i < p->count; i++) {
        if (p->mon[i].species == MMO_ID_NONE)
            p->untranslatable++;
        p->unmapped_moves += p->mon[i].move_unmapped;
        p->hidden_abilities += p->mon[i].ability_unrenderable;
    }
}

/*
 * Apply a party container to the party the client holds, the way the game client applies one:
 * a delete empties the container, `hasChange` replaces it before the records land, and without
 * that flag each record is merged into what is already there, keyed by the monster's own id.
 */
static int seat_party(openmmo_client *c, size_t n)
{
    mmo_pokemon_container ct;
    mmo_monster mons[OPENMMO_PARTY_MAX];
    openmmo_party *p = &c->party;

    if (mmo_game_read_pokemon_container(c->gbuf, n, &ct, mons,
                                        OPENMMO_PARTY_MAX) != 0) {
        p->malformed++;
        p->valid = 1;
        return 0;
    }
    if (ct.container != MMO_CONTAINER_PARTY)
        return 0;                         /* a PC or box container: not the party */

    /* Which members were eggs before this container was applied. A hatch is the
     * server clearing that flag on a monster the client already held, and it
     * usually arrives on a container that replaces the party outright, so the
     * comparison has to be against a snapshot rather than against what is left. */
    s64 was_egg[OPENMMO_PARTY_MAX];
    int was_egg_count = 0;
    for (int i = 0; i < p->count; i++)
        if (p->mon[i].egg)
            was_egg[was_egg_count++] = p->mon[i].id;
    c->hatched_count = 0;

    p->valid = 1;
    p->trailing = ct.trailing;
    if (ct.deleted) {
        p->count = 0;
        p->total = 0;
        party_count_unmapped(p);
        return 1;
    }
    if (ct.has_change)
        p->count = 0;
    p->total = ct.total;
    for (int i = 0; i < ct.count; i++) {
        int slot = -1;
        for (int j = 0; j < p->count; j++) {
            if (p->mon[j].id == mons[i].id) {
                slot = j;
                break;
            }
        }
        if (slot < 0) {
            if (p->count >= OPENMMO_PARTY_MAX)
                continue;                 /* the engine's party cap; count kept above */
            slot = p->count++;
        }
        seat_mon(&p->mon[slot], &mons[i]);
    }
    for (int i = 0; i < p->count; i++) {
        if (p->mon[i].egg)
            continue;
        for (int j = 0; j < was_egg_count; j++) {
            if (was_egg[j] == p->mon[i].id) {
                c->hatched[c->hatched_count++] = i;
                break;
            }
        }
    }
    party_count_unmapped(p);
    /* The party summary in the world-state snapshot is this party, so a host that
     * reads either sees the same thing. */
    c->ws.party_count = p->count;
    c->ws.party_untranslatable = p->untranslatable;
    for (int i = 0; i < OPENMMO_PARTY_MAX; i++)
        c->ws.party_species[i] = i < p->count ? p->mon[i].species : 0;
    return 1;
}

/* Fold one already-decoded stack into `d`, crossing the id map on the way: an
 * unmapped item is counted rather than guessed. */
static void seat_bag_stack(openmmo_bag_stack *d, const mmo_item_stack *s)
{
    const char *why = NULL;
    d->object_id = s->object_id;
    d->item_id = s->item_id;
    d->quantity = s->quantity;
    d->engine_id = mmo_id_item_from_server(s->item_id, &why);
}

static void bag_count_unmapped(openmmo_bag *b)
{
    b->untranslatable = 0;
    for (int i = 0; i < b->count; i++)
        if (b->stack[i].engine_id == MMO_ID_NONE)
            b->untranslatable++;
}

static void bag_sync_ws(openmmo_client *c)
{
    c->ws.item_count = c->bag.total;
    c->ws.item_untranslatable = c->bag.untranslatable;
}

/* Find a held stack by the id the update keys on. Object id wins when the
 * wire carried one; otherwise the item id, which is how a snapshot with
 * entity 0 still merges. */
static int bag_find(const openmmo_bag *b, s64 object_id, u16 item_id)
{
    for (int i = 0; i < b->count; i++) {
        if (object_id != 0 && b->stack[i].object_id == object_id)
            return i;
        if (object_id == 0 && b->stack[i].item_id == item_id)
            return i;
    }
    return -1;
}

static void bag_remove_at(openmmo_bag *b, int at)
{
    if (at < 0 || at >= b->count)
        return;
    if (at + 1 < b->count)
        b->stack[at] = b->stack[b->count - 1];
    b->count--;
}

/* Apply an ItemStacks snapshot (0x40) to the bag the client holds. replace
 * wipes it first, the way itemStacksPacket is written; without that flag
 * each stack is merged by object id. Quantity 0 removes. Returns 1 if the
 * bag is the server's picture after this packet. */
static int seat_bag_snapshot(openmmo_client *c, size_t n)
{
    mmo_item_stack scratch[OPENMMO_BAG_MAX];
    int total = 0, replace = 0;
    int got = mmo_game_read_item_stacks(c->gbuf, n, scratch, OPENMMO_BAG_MAX,
                                        &total, &replace);
    openmmo_bag *b = &c->bag;

    if (got < 0) {
        b->malformed++;
        b->valid = 1;
        return 0;
    }
    b->valid = 1;
    b->total = total;
    if (replace) {
        b->count = 0;
        b->dropped = 0;
    }
    if (total > got)
        b->dropped += total - got;
    for (int i = 0; i < got; i++) {
        if (scratch[i].quantity <= 0) {
            bag_remove_at(b, bag_find(b, scratch[i].object_id, scratch[i].item_id));
            continue;
        }
        int at = replace ? -1 : bag_find(b, scratch[i].object_id, scratch[i].item_id);
        if (at < 0) {
            if (b->count >= OPENMMO_BAG_MAX) {
                b->dropped++;
                continue;
            }
            at = b->count++;
        }
        seat_bag_stack(&b->stack[at], &scratch[i]);
    }
    bag_count_unmapped(b);
    bag_sync_ws(c);
    return 1;
}

/* Apply a single-stack update (0x42). A body that is a battle-side add
 * rather than an item stack is left alone. Quantity 0 removes. */
static int seat_bag_update(openmmo_client *c, size_t n)
{
    mmo_item_stack one;
    int rc = mmo_game_read_item_stack_update(c->gbuf, n, &one);
    openmmo_bag *b = &c->bag;

    if (rc < 0) {
        b->malformed++;
        b->valid = 1;
        return 0;
    }
    if (rc == 0)
        return 0;
    b->valid = 1;
    if (one.quantity <= 0) {
        bag_remove_at(b, bag_find(b, one.object_id, one.item_id));
    } else {
        int at = bag_find(b, one.object_id, one.item_id);
        if (at < 0) {
            if (b->count >= OPENMMO_BAG_MAX) {
                b->dropped++;
                bag_count_unmapped(b);
                b->total = b->count;
                bag_sync_ws(c);
                return 1;
            }
            at = b->count++;
        }
        seat_bag_stack(&b->stack[at], &one);
    }
    b->total = b->count;
    bag_count_unmapped(b);
    bag_sync_ws(c);
    return 1;
}

/* Apply a dialog box (s2c 0x21). A close clears the open flag; a
 * resolvable text id is split into the bank/entry the engine loader
 * takes. An id this engine has no string for is kept as the wire
 * number and named as unresolvable, never rewritten. */
static int seat_dialog(openmmo_client *c, size_t n)
{
    mmo_dialog_action box;
    openmmo_dialog *d = &c->dialog;
    const char *why = NULL;
    u16 bank = 0, entry = 0;

    if (mmo_game_read_dialog_action(c->gbuf, n, &box) != 0) {
        d->malformed++;
        d->valid = 1;
        return 0;
    }
    d->valid = 1;
    d->flags = box.flags;
    d->action_type = box.action_type;
    d->text_id = box.text_id;
    d->entity_id = box.entity_id;
    d->context_value = box.context_value;
    d->arg_count = box.arg_count;
    d->close = box.close;
    d->why = NULL;
    d->resolved = 0;
    d->bank = 0;
    d->entry = 0;
    d->choice_count = box.choice_count;
    d->choice_bank = box.choice_bank;
    if (box.choice_count > 0)
        memcpy(d->choices, box.choices,
               (size_t)box.choice_count * sizeof box.choices[0]);
    d->strvar_count = box.strvar_count;
    if (box.strvar_count > 0)
        memcpy(d->strvar, box.strvar,
               (size_t)box.strvar_count * sizeof box.strvar[0]);
    if (box.close) {
        d->open = 0;
        d->awaiting = 0;
        return 1;
    }
    if (box.text_id == 0) {
        /* A 0x23 or 0x31 menu may have no question string. A yes/no
         * without a line is still a prompt that owes a reply. */
        if (box.action_type == (s8)MMO_DIALOG_ACTION_YESNO
            || box.action_type == (s8)MMO_DIALOG_ACTION_MENU
            || box.action_type == (s8)MMO_DIALOG_ACTION_LIST) {
            d->open = 1;
            d->awaiting = 1;
            return 1;
        }
        d->open = 0;
        d->awaiting = 0;
        return 1;
    }
    d->awaiting = 1;
    if (mmo_id_text_from_server((u32)box.text_id, &bank, &entry, &why) != 0) {
        d->open = 1;
        d->why = why;
        return 1;
    }
    d->open = 1;
    d->resolved = 1;
    d->bank = bank;
    d->entry = entry;
    return 1;
}

static int script_move_is_self(const openmmo_client *c, s64 id)
{
    if (id == -1)
        return 1;
    if (c->ws.character_id != 0 && id == c->ws.character_id)
        return 1;
    if (c->self_entity_id != 0 && (u32)id == c->self_entity_id)
        return 1;
    return 0;
}

static int seat_script_move(openmmo_client *c, size_t n)
{
    mmo_script_move seq;
    openmmo_script_move *s = &c->script_move;
    int i, mapped = 0;

    if (mmo_game_read_script_move(c->gbuf, n, &seq) != 0) {
        s->malformed++;
        s->valid = 1;
        return 0;
    }
    s->valid = 1;
    s->entity_id = seq.entity_id;
    s->flag = seq.flag;
    s->count = seq.count;
    memcpy(s->actions, seq.actions, seq.count);
    s->is_self = script_move_is_self(c, seq.entity_id);
    s->slot = -1;
    if (!s->is_self && seq.entity_id != -2)
        s->slot = openmmo_entity_slot_of(&c->ent, (u32)seq.entity_id);
    for (i = 0; i < seq.count; i++) {
        if (mmo_game_script_action(seq.flag, seq.actions[i]) >= 0)
            mapped++;
    }
    s->mapped = mapped;
    s->unknown = seq.count - mapped;

    /* Queue a copy of its own, so a later 0x0D in the same batch cannot take
     * this one's actions with it. */
    if (c->script_move_live < OPENMMO_SCRIPT_MOVE_QUEUE) {
        int slot = (c->script_move_head + c->script_move_live)
                   % OPENMMO_SCRIPT_MOVE_QUEUE;
        c->script_move_q[slot] = *s;
        c->script_move_live++;
    } else {
        s->dropped++;
    }
    return 1;
}

static void objective_upsert(openmmo_objectives *o, s8 id, s32 value, s16 count)
{
    int i;

    for (i = 0; i < o->count; i++) {
        if (o->entry[i].id == id) {
            o->entry[i].value = value;
            o->entry[i].count = count;
            return;
        }
    }
    if (o->count >= OPENMMO_OBJECTIVE_MAX) {
        o->dropped++;
        return;
    }
    o->entry[o->count].id = id;
    o->entry[o->count].value = value;
    o->entry[o->count].count = count;
    o->count++;
}

/* Apply a bulk objective list (s2c 0xD3). The official client zeros every definition
 * whose id is not in the packet, so this replaces the store. */
static int seat_objective_bulk(openmmo_client *c, size_t n)
{
    mmo_objective scratch[OPENMMO_OBJECTIVE_MAX];
    openmmo_objectives *o = &c->objectives;
    int i, nent = 0;

    if (mmo_game_read_objective_bulk(c->gbuf, n, scratch,
                                     OPENMMO_OBJECTIVE_MAX, &nent) != 0) {
        o->malformed++;
        o->valid = 1;
        return 0;
    }
    o->valid = 1;
    o->count = 0;
    o->dropped = 0;
    for (i = 0; i < nent; i++)
        objective_upsert(o, scratch[i].id, scratch[i].value, scratch[i].count);
    return 1;
}

static int ui_page_count(const openmmo_ui *u)
{
    int i, n = 0;

    for (i = 0; i < MMO_UI_MENU_TYPES; i++)
        if (u->page_valid[i])
            n++;
    return n;
}

static void ui_fill_event(const openmmo_ui *u, openmmo_event *e)
{
    if (!e)
        return;
    e->ui.opcode = u->last_op;
    e->ui.scale = u->scale_valid ? u->scale : -1;
    e->ui.pages = ui_page_count(u);
    e->ui.names = u->names_valid ? u->names.count : 0;
    e->ui.options = u->options_valid ? u->options.count : 0;
    e->ui.rows = u->list_valid ? u->list.count : 0;
    e->ui.confirm = (u->confirm_valid && u->confirm.visible);
    e->ui.prompt = u->prompt_open;
}

/* Apply one HUD / prompt / list packet. Returns 1 if the store
 * changed, 0 if the body was refused. */
static int seat_ui(openmmo_client *c, u8 opcode, size_t n)
{
    openmmo_ui *u = &c->ui;

    switch (opcode) {
    case MMO_GAME_OP_MENU_VISIBILITY: {
        mmo_menu_visibility vis;

        if (mmo_game_read_menu_visibility(c->gbuf, n, &vis) != 0) {
            u->malformed++;
            u->valid = 1;
            return 0;
        }
        u->valid = 1;
        u->last_op = opcode;
        u->menu_visible = vis.enabled;
        u->menu_type = vis.enabled ? vis.menu_type : 0;
        return 1;
    }
    case MMO_GAME_OP_MENU_PAGE: {
        mmo_menu_page page;
        int slot;

        if (mmo_game_read_menu_page(c->gbuf, n, &page) != 0) {
            u->malformed++;
            u->valid = 1;
            return 0;
        }
        slot = (int)page.menu_type;
        u->valid = 1;
        u->last_op = opcode;
        u->page_valid[slot] = 1;
        u->page_present[slot] = page.present;
        u->page_len[slot] = page.len;
        if (page.len > 0)
            memcpy(u->page[slot], page.bytes, (size_t)page.len);
        return 1;
    }
    case MMO_GAME_OP_NAME_CHOICES:
        if (mmo_game_read_name_choices(c->gbuf, n, &u->names) != 0) {
            u->malformed++;
            u->valid = 1;
            return 0;
        }
        u->valid = 1;
        u->last_op = opcode;
        u->names_valid = 1;
        return 1;
    case MMO_GAME_OP_OPTION_LIST:
        if (mmo_game_read_option_list(c->gbuf, n, &u->options) != 0) {
            u->malformed++;
            u->valid = 1;
            return 0;
        }
        u->valid = 1;
        u->last_op = opcode;
        u->options_valid = 1;
        return 1;
    case MMO_GAME_OP_LIST_WINDOW:
        if (mmo_game_read_list_window(c->gbuf, n, &u->list) != 0) {
            u->malformed++;
            u->valid = 1;
            return 0;
        }
        u->valid = 1;
        u->last_op = opcode;
        u->list_valid = 1;
        return 1;
    case MMO_GAME_OP_CONFIRM_PROMPT:
        if (mmo_game_read_confirm_prompt(c->gbuf, n, &u->confirm) != 0) {
            u->malformed++;
            u->valid = 1;
            return 0;
        }
        u->valid = 1;
        u->last_op = opcode;
        u->confirm_valid = 1;
        return 1;
    case MMO_GAME_OP_MENU_PROMPT_OPEN:
        if (mmo_game_read_menu_prompt_open(c->gbuf, n, &u->prompt) != 0) {
            u->malformed++;
            u->valid = 1;
            return 0;
        }
        u->valid = 1;
        u->last_op = opcode;
        u->prompt_open = 1;
        return 1;
    case MMO_GAME_OP_MENU_PROMPT_CLOSE: {
        s8 kind = 0;

        if (mmo_game_read_menu_prompt_close(c->gbuf, n, &kind) != 0) {
            u->malformed++;
            u->valid = 1;
            return 0;
        }
        u->valid = 1;
        u->last_op = opcode;
        u->prompt_open = 0;
        u->prompt.kind = kind;
        return 1;
    }
    case MMO_GAME_OP_VIEW_SCALE: {
        s8 scale = 0;

        if (mmo_game_read_view_scale(c->gbuf, n, &scale) != 0) {
            u->malformed++;
            u->valid = 1;
            return 0;
        }
        u->valid = 1;
        u->last_op = opcode;
        u->scale_valid = 1;
        u->scale = scale;
        return 1;
    }
    default:
        return 0;
    }
}

static void sync_fill_event(const openmmo_sync *s, openmmo_event *e)
{
    if (!e)
        return;
    e->sync.opcode = s->last_op;
    e->sync.digest = s->digest_valid;
    e->sync.transfer_done = s->transfer_done;
    e->sync.stream_done = s->stream_done;
    e->sync.image_done = s->image_last;
    e->sync.plain_len = s->transfer_open_ok ? s->transfer_plain_len : 0;
}

static void compete_fill_event(const openmmo_compete *cp, openmmo_event *e)
{
    if (!cp || !e)
        return;
    e->compete.opcode = cp->last_op;
    e->compete.rentals = cp->rentals.monster_count
                             ? cp->rentals.monster_count
                             : cp->rentals.preview_count;
    e->compete.tourneys = cp->page.count;
    e->compete.matchups = cp->matchups.count;
    e->compete.board_rows = cp->board.count;
    e->compete.entered = cp->count.entered;
}

/* Seat one of the five competitive s2c packets. A body the reader refuses
 * is counted and the store is left as it was: these five carry no state the
 * rest of the session depends on, so a bad one is a lost page rather than a
 * reason to drop the link. */
static int seat_compete(openmmo_client *c, u8 opcode, size_t n)
{
    openmmo_compete *cp = &c->compete;

    switch (opcode) {
    case MMO_GAME_OP_RENTAL_SET: {
        mmo_rentals set;

        if (mmo_game_read_rentals(c->gbuf, n, &set) != 0)
            break;
        cp->rentals = set;
        cp->rentals_valid = 1;
        cp->valid = 1;
        cp->last_op = opcode;
        return 1;
    }
    case MMO_GAME_OP_TOURNEY_PAGE: {
        mmo_tourney_page page;

        if (mmo_game_read_tourney_page(c->gbuf, n, &page) != 0)
            break;
        cp->page = page;
        cp->page_valid = 1;
        cp->valid = 1;
        cp->last_op = opcode;
        return 1;
    }
    case MMO_GAME_OP_TOURNEY_COUNT: {
        mmo_tourney_count tc;

        if (mmo_game_read_tourney_count(c->gbuf, n, &tc) != 0)
            break;
        cp->count = tc;
        cp->count_valid = 1;
        cp->valid = 1;
        cp->last_op = opcode;
        return 1;
    }
    case MMO_GAME_OP_TOURNEY_MATCHUPS: {
        mmo_matchups m;

        if (mmo_game_read_matchups(c->gbuf, n, &m) != 0)
            break;
        cp->matchups = m;
        cp->matchups_valid = 1;
        cp->valid = 1;
        cp->last_op = opcode;
        return 1;
    }
    case MMO_GAME_OP_SCORE_BOARD: {
        mmo_score_board b;

        if (mmo_game_read_score_board(c->gbuf, n, &b) != 0)
            break;
        cp->board = b;
        cp->board_valid = 1;
        cp->valid = 1;
        cp->last_op = opcode;
        return 1;
    }
    default:
        return 0;
    }
    cp->malformed++;
    cp->valid = 1;
    return 0;
}

static void gm_fill_event(const openmmo_gm *g, openmmo_event *e)
{
    if (!g || !e)
        return;
    e->gm.opcode = g->last_op;
    e->gm.found = g->lookup.found;
    e->gm.variant = (int)(u8)g->panel.variant;
    e->gm.rows = g->panel.row_count;
    e->gm.cleared = g->entry.clear != 0;
}

/* Seat one of the three staff s2c. None of them is addressed to an ordinary
 * player and none carries state the session depends on, so a body the reader
 * refuses is counted and the store is left as it was. */
static int seat_gm(openmmo_client *c, u8 opcode, size_t n)
{
    openmmo_gm *g = &c->gm;

    switch (opcode) {
    case MMO_GAME_OP_GM_LOOKUP: {
        mmo_gm_lookup lk;

        if (mmo_game_read_gm_lookup(c->gbuf, n, &lk) != 0)
            break;
        g->lookup = lk;
        g->lookup_valid = 1;
        g->valid = 1;
        g->last_op = opcode;
        return 1;
    }
    case MMO_GAME_OP_GM_PANEL: {
        mmo_gm_panel panel;

        if (mmo_game_read_gm_panel(c->gbuf, n, &panel) != 0)
            break;
        g->panel = panel;
        g->panel_valid = 1;
        g->valid = 1;
        g->last_op = opcode;
        return 1;
    }
    case MMO_GAME_OP_GM_PANEL_ENTRY: {
        mmo_gm_panel_entry entry;

        if (mmo_game_read_gm_panel_entry(c->gbuf, n, &entry) != 0)
            break;
        g->entry = entry;
        g->entry_valid = 1;
        g->valid = 1;
        g->last_op = opcode;
        return 1;
    }
    default:
        return 0;
    }
    g->malformed++;
    g->valid = 1;
    return 0;
}

static void sync_free_xfer(openmmo_client *c)
{
    free(c->sync_xfer);
    c->sync_xfer = NULL;
    c->sync_xfer_cap = 0;
}

static int sync_send_empty(openmmo_client *c, u8 opcode)
{
    mmo_wbuf body;
    int rc;

    mmo_wbuf_init(&body);
    mmo_game_write_digest_empty(&body);
    rc = body.err
             ? (fail(c, "sync: could not frame digest reply"), -1)
             : send_game_packet(c, opcode, body.data, body.len,
                                "sync: could not send digest reply");
    mmo_wbuf_free(&body);
    return rc;
}

static int sync_finish_transfer(openmmo_client *c)
{
    openmmo_sync *s = &c->sync;
    size_t got = 0;

    if (!c->sync_xfer || s->transfer_got < s->transfer_size)
        return 0;
    if (mmo_game_transfer_open(c->sync_xfer, (size_t)s->transfer_got,
                               s->transfer_plain, sizeof s->transfer_plain,
                               &got) == 0) {
        s->transfer_open_ok = 1;
        s->transfer_plain_len = (int)got;
    } else {
        size_t keep = (size_t)s->transfer_got;

        if (keep > sizeof s->transfer_plain)
            keep = sizeof s->transfer_plain;
        memcpy(s->transfer_plain, c->sync_xfer, keep);
        mmo_game_sync_xor(s->transfer_plain, keep);
        s->transfer_open_ok = 0;
        s->transfer_plain_len = (int)keep;
    }
    s->transfer_done = 1;
    sync_free_xfer(c);
    return 1;
}

/* Apply one digest / transfer / stream / image packet. Returns 1 if
 * the store changed, 0 if the body was refused. */
static int seat_sync(openmmo_client *c, u8 opcode, size_t n)
{
    openmmo_sync *s = &c->sync;

    switch (opcode) {
    case MMO_GAME_OP_DIGEST:
    case MMO_GAME_OP_DIGEST_BATCH: {
        mmo_digest d;

        if (mmo_game_read_digest(c->gbuf, n, &d) != 0
            || d.len > sizeof s->digest) {
            s->malformed++;
            s->valid = 1;
            return 0;
        }
        s->valid = 1;
        s->last_op = opcode;
        s->digest_valid = 1;
        s->digest_op = opcode;
        s->digest_bits = d.bit_count;
        s->digest_len = d.len;
        if (d.len)
            memcpy(s->digest, d.bytes, d.len);
        (void)sync_send_empty(c, opcode);
        return 1;
    }
    case MMO_GAME_OP_TRANSFER_BEGIN: {
        mmo_transfer_begin b;

        if (mmo_game_read_transfer_begin(c->gbuf, n, &b) != 0
            || b.size < 0 || (u32)b.size > MMO_SYNC_XFER_MAX
            || b.sig_len > sizeof s->transfer_sig) {
            s->malformed++;
            s->valid = 1;
            return 0;
        }
        sync_free_xfer(c);
        if (b.size > 0) {
            c->sync_xfer = (u8 *)malloc((size_t)b.size);
            if (!c->sync_xfer) {
                s->malformed++;
                s->valid = 1;
                return 0;
            }
            c->sync_xfer_cap = (size_t)b.size;
        }
        s->valid = 1;
        s->last_op = opcode;
        s->transfer_valid = 1;
        s->transfer_id = b.id;
        s->transfer_size = b.size;
        s->transfer_sig_len = b.sig_len;
        if (b.sig_len)
            memcpy(s->transfer_sig, b.sig, b.sig_len);
        s->transfer_got = 0;
        s->transfer_done = 0;
        s->transfer_open_ok = 0;
        s->transfer_plain_len = 0;
        return 1;
    }
    case MMO_GAME_OP_TRANSFER_APPEND: {
        mmo_transfer_append a;

        if (mmo_game_read_transfer_append(c->gbuf, n, &a) != 0
            || !s->transfer_valid || s->transfer_done
            || (size_t)s->transfer_got + a.len > c->sync_xfer_cap) {
            s->malformed++;
            s->valid = 1;
            return 0;
        }
        if (a.len && c->sync_xfer)
            memcpy(c->sync_xfer + s->transfer_got, a.data, a.len);
        s->valid = 1;
        s->last_op = opcode;
        s->transfer_got += a.len;
        sync_finish_transfer(c);
        return 1;
    }
    case MMO_GAME_OP_STREAM_CHUNK: {
        mmo_stream_chunk ch;

        if (mmo_game_read_stream_chunk(c->gbuf, n, &ch) != 0) {
            s->malformed++;
            s->valid = 1;
            return 0;
        }
        if (!s->stream_valid || s->stream_id != ch.id || s->stream_done) {
            s->stream_id = ch.id;
            s->stream_got = 0;
            s->stream_done = 0;
            s->stream_len = 0;
        }
        if ((size_t)s->stream_got + ch.len > sizeof s->stream) {
            s->malformed++;
            s->valid = 1;
            return 0;
        }
        if (ch.len)
            memcpy(s->stream + s->stream_got, ch.data, ch.len);
        s->valid = 1;
        s->last_op = opcode;
        s->stream_valid = 1;
        s->stream_got += ch.len;
        if (ch.last) {
            s->stream_len = s->stream_got;
            s->stream_done = 1;
        }
        return 1;
    }
    case MMO_GAME_OP_IMAGE_CHUNK: {
        mmo_image_chunk im;

        if (mmo_game_read_image_chunk(c->gbuf, n, &im) != 0) {
            s->malformed++;
            s->valid = 1;
            return 0;
        }
        s->valid = 1;
        s->last_op = opcode;
        s->image_valid = 1;
        s->image_ctrl = im.ctrl;
        if (!im.present) {
            s->image_len = 0;
            s->image_last = 0;
            return 1;
        }
        if (im.chunk_index == 0)
            s->image_len = 0;
        if ((size_t)s->image_len + im.len > sizeof s->image) {
            s->malformed++;
            return 0;
        }
        if (im.len)
            memcpy(s->image + s->image_len, im.data, im.len);
        s->image_type = im.image_type;
        s->image_len += im.len;
        s->image_last = im.last;
        return 1;
    }
    default:
        return 0;
    }
}

/* Apply one objective (s2c 0xD4). Upserts by id. */
static int seat_objective(openmmo_client *c, size_t n, mmo_objective *got)
{
    mmo_objective one;
    openmmo_objectives *o = &c->objectives;

    if (mmo_game_read_objective(c->gbuf, n, &one) != 0) {
        o->malformed++;
        o->valid = 1;
        return 0;
    }
    o->valid = 1;
    objective_upsert(o, one.id, one.value, one.count);
    if (got)
        *got = one;
    return 1;
}

static void friend_copy(openmmo_friend *dst, const mmo_friend *src)
{
    memset(dst, 0, sizeof *dst);
    dst->player = src->player;
    dst->unknown = src->unknown;
    dst->online = src->online;
    snprintf(dst->name, sizeof dst->name, "%s", src->name);
    dst->unk0 = src->unk0;
    dst->last_seen = src->last_seen;
    dst->kind = src->kind;
    dst->packed_slots = src->packed_slots;
    memcpy(dst->sprite, src->sprite, sizeof dst->sprite);
}

static int friend_find(const openmmo_friends *f, s64 player)
{
    int i;

    for (i = 0; i < f->count; i++) {
        if (f->entry[i].player == player)
            return i;
    }
    return -1;
}

static int friend_upsert(openmmo_friends *f, const mmo_friend *src)
{
    int i = friend_find(f, src->player);

    if (i >= 0) {
        friend_copy(&f->entry[i], src);
        return 0;
    }
    if (f->count >= OPENMMO_FRIEND_MAX) {
        f->dropped++;
        return -1;
    }
    friend_copy(&f->entry[f->count], src);
    f->count++;
    return 1;
}

/* Apply a friend list (s2c 0x63). mode 0 replaces the store, as
 * official clears the map first; any other mode upserts. */
static int seat_friend_list(openmmo_client *c, size_t n, u8 *mode_out)
{
    mmo_friend scratch[OPENMMO_FRIEND_MAX];
    openmmo_friends *f = &c->friends;
    int i, nent = 0;
    u8 mode = 0;

    if (mmo_game_read_friend_list(c->gbuf, n, scratch,
                                  OPENMMO_FRIEND_MAX, &nent, &mode) != 0) {
        f->malformed++;
        f->valid = 1;
        return 0;
    }
    f->valid = 1;
    if (mode == 0) {
        f->count = 0;
        f->dropped = 0;
    }
    for (i = 0; i < nent; i++)
        friend_upsert(f, &scratch[i]);
    if (mode_out)
        *mode_out = mode;
    return 1;
}

/* Apply one friend (s2c 0x64). Upserts by player id. */
static int seat_friend_insert(openmmo_client *c, size_t n, mmo_friend *got)
{
    mmo_friend one;
    openmmo_friends *f = &c->friends;

    if (mmo_game_read_friend(c->gbuf, n, &one) != 0) {
        f->malformed++;
        f->valid = 1;
        return 0;
    }
    f->valid = 1;
    friend_upsert(f, &one);
    if (got)
        *got = one;
    return 1;
}

/* Drop one friend (s2c 0x65). Missing is not an error. */
static int seat_friend_delete(openmmo_client *c, size_t n, s64 *got, char *name,
                              size_t name_cap)
{
    openmmo_friends *f = &c->friends;
    s64 player = 0;
    int i;

    if (mmo_game_read_friend_delete(c->gbuf, n, &player) != 0) {
        f->malformed++;
        f->valid = 1;
        return 0;
    }
    f->valid = 1;
    i = friend_find(f, player);
    if (i >= 0) {
        if (name && name_cap)
            snprintf(name, name_cap, "%s", f->entry[i].name);
        if (i + 1 < f->count)
            memmove(&f->entry[i], &f->entry[i + 1],
                    (size_t)(f->count - i - 1) * sizeof f->entry[0]);
        f->count--;
    }
    if (got)
        *got = player;
    return 1;
}

/* Flip one friend's online bit (s2c 0x66). Missing is not an error. */
static int seat_friend_online(openmmo_client *c, size_t n, s64 *got, u8 *online,
                              char *name, size_t name_cap)
{
    openmmo_friends *f = &c->friends;
    s64 player = 0;
    u8 bit = 0;
    int i;

    if (mmo_game_read_friend_online(c->gbuf, n, &player, &bit) != 0) {
        f->malformed++;
        f->valid = 1;
        return 0;
    }
    f->valid = 1;
    i = friend_find(f, player);
    if (i >= 0) {
        f->entry[i].online = bit;
        if (name && name_cap)
            snprintf(name, name_cap, "%s", f->entry[i].name);
    }
    if (got)
        *got = player;
    if (online)
        *online = bit;
    return 1;
}

static int friends_online_count(const openmmo_friends *f)
{
    int i, n = 0;

    for (i = 0; i < f->count; i++) {
        if (f->entry[i].online)
            n++;
    }
    return n;
}

static void guild_member_copy(openmmo_guild_member *dst, const mmo_guild_member *src)
{
    memset(dst, 0, sizeof *dst);
    dst->rank = src->rank;
    dst->entity_id = src->entity_id;
    dst->joined_at = src->joined_at;
    dst->online = src->online;
    snprintf(dst->name, sizeof dst->name, "%s", src->appearance.name);
    dst->unk0 = src->appearance.unk0;
    dst->last_seen = src->appearance.last_seen;
    dst->kind = src->appearance.kind;
    dst->packed_slots = src->appearance.packed_slots;
    memcpy(dst->sprite, src->appearance.sprite, sizeof dst->sprite);
}

static int guild_find(const openmmo_guild *g, s64 id)
{
    int i;

    for (i = 0; i < g->member_count; i++) {
        if (g->member[i].entity_id == id)
            return i;
    }
    return -1;
}

static int guild_upsert(openmmo_guild *g, const mmo_guild_member *src)
{
    int i = guild_find(g, src->entity_id);

    if (i >= 0) {
        guild_member_copy(&g->member[i], src);
        return 0;
    }
    if (g->member_count >= OPENMMO_GUILD_MEMBER_MAX) {
        g->dropped++;
        return -1;
    }
    guild_member_copy(&g->member[g->member_count], src);
    g->member_count++;
    return 1;
}

static void guild_drop_at(openmmo_guild *g, int i)
{
    if (i < 0 || i >= g->member_count)
        return;
    if (i + 1 < g->member_count)
        memmove(&g->member[i], &g->member[i + 1],
                (size_t)(g->member_count - i - 1) * sizeof g->member[0]);
    g->member_count--;
}

static int guild_online_count(const openmmo_guild *g)
{
    int i, n = 0;

    for (i = 0; i < g->member_count; i++) {
        if (g->member[i].online)
            n++;
    }
    return n;
}

static void guild_fill_event(openmmo_client *c, openmmo_event *e)
{
    e->guild.in_guild = c->guild.in_guild;
    e->guild.member_count = c->guild.member_count;
    e->guild.online = guild_online_count(&c->guild);
    e->guild.guild_id = c->guild.profile.guild_id;
    e->guild.added = 0;
    e->guild.removed = 0;
    snprintf(e->guild.name, sizeof e->guild.name, "%s", c->guild.profile.name);
    snprintf(e->guild.tag, sizeof e->guild.tag, "%s", c->guild.profile.tag);
    e->guild.member[0] = '\0';
}

/* Apply membership (s2c 0x80). A false flag clears the roster. */
static int seat_guild_membership(openmmo_client *c, size_t n)
{
    mmo_guild_profile prof;
    openmmo_guild *g = &c->guild;
    int in = 0;

    if (mmo_game_read_guild_membership(c->gbuf, n, &in, &prof) != 0) {
        g->malformed++;
        g->valid = 1;
        return 0;
    }
    g->valid = 1;
    g->in_guild = in;
    if (in)
        g->profile = prof;
    else {
        memset(&g->profile, 0, sizeof g->profile);
        g->member_count = 0;
        g->dropped = 0;
    }
    return 1;
}

/* Apply a profile (s2c 0x81). Does not change the roster. */
static int seat_guild_profile(openmmo_client *c, size_t n)
{
    mmo_guild_profile prof;
    openmmo_guild *g = &c->guild;

    if (mmo_game_read_guild_profile(c->gbuf, n, &prof) != 0) {
        g->malformed++;
        g->valid = 1;
        return 0;
    }
    g->valid = 1;
    g->in_guild = 1;
    g->profile = prof;
    return 1;
}

/* Apply a member list (s2c 0x88). replace 1 clears first. */
static int seat_guild_members(openmmo_client *c, size_t n, u8 *repl_out)
{
    mmo_guild_member scratch[OPENMMO_GUILD_MEMBER_MAX];
    openmmo_guild *g = &c->guild;
    int i, nent = 0;
    u8 repl = 0;

    if (mmo_game_read_guild_members(c->gbuf, n, scratch,
                                    OPENMMO_GUILD_MEMBER_MAX, &nent, &repl) != 0) {
        g->malformed++;
        g->valid = 1;
        return 0;
    }
    g->valid = 1;
    if (repl) {
        g->member_count = 0;
        g->dropped = 0;
    }
    for (i = 0; i < nent; i++)
        guild_upsert(g, &scratch[i]);
    if (repl_out)
        *repl_out = repl;
    return 1;
}

/* Apply one member (s2c 0x83). */
static int seat_guild_member_add(openmmo_client *c, size_t n, mmo_guild_member *got)
{
    mmo_guild_member one;
    openmmo_guild *g = &c->guild;

    if (mmo_game_read_guild_member(c->gbuf, n, &one) != 0) {
        g->malformed++;
        g->valid = 1;
        return 0;
    }
    g->valid = 1;
    guild_upsert(g, &one);
    if (got)
        *got = one;
    return 1;
}

/* Apply a rank change (s2c 0x84). Missing is not an error. */
static int seat_guild_rank_change(openmmo_client *c, size_t n, s64 *got, u8 *rank,
                                  char *name, size_t name_cap)
{
    openmmo_guild *g = &c->guild;
    s64 id = 0;
    u8 rk = 0;
    int i;

    if (mmo_game_read_guild_rank_change(c->gbuf, n, &id, &rk) != 0) {
        g->malformed++;
        g->valid = 1;
        return 0;
    }
    g->valid = 1;
    i = guild_find(g, id);
    if (i >= 0) {
        g->member[i].rank = rk;
        if (name && name_cap)
            snprintf(name, name_cap, "%s", g->member[i].name);
    }
    if (got)
        *got = id;
    if (rank)
        *rank = rk;
    return 1;
}

/* Drop one member (s2c 0x85). Missing is not an error. */
static int seat_guild_member_drop(openmmo_client *c, size_t n, s64 *got,
                                  char *name, size_t name_cap)
{
    openmmo_guild *g = &c->guild;
    s64 id = 0;
    int i;

    if (mmo_game_read_guild_member_drop(c->gbuf, n, &id) != 0) {
        g->malformed++;
        g->valid = 1;
        return 0;
    }
    g->valid = 1;
    i = guild_find(g, id);
    if (i >= 0) {
        if (name && name_cap)
            snprintf(name, name_cap, "%s", g->member[i].name);
        guild_drop_at(g, i);
    }
    if (id != 0 && id == c->ws.character_id) {
        g->in_guild = 0;
        memset(&g->profile, 0, sizeof g->profile);
        g->member_count = 0;
    }
    if (got)
        *got = id;
    return 1;
}

/* Flip one member's online bit (s2c 0x86). Missing is not an error. */
static int seat_guild_presence(openmmo_client *c, size_t n, s64 *got, u8 *online,
                               char *name, size_t name_cap)
{
    openmmo_guild *g = &c->guild;
    s64 id = 0;
    u8 bit = 0;
    int i;

    if (mmo_game_read_guild_presence(c->gbuf, n, &id, &bit) != 0) {
        g->malformed++;
        g->valid = 1;
        return 0;
    }
    g->valid = 1;
    i = guild_find(g, id);
    if (i >= 0) {
        g->member[i].online = bit;
        if (name && name_cap)
            snprintf(name, name_cap, "%s", g->member[i].name);
    }
    if (got)
        *got = id;
    if (online)
        *online = bit;
    return 1;
}

/* Apply the activity log (s2c 0x89). */
static int seat_guild_log(openmmo_client *c, size_t n)
{
    mmo_guild_log_entry scratch[OPENMMO_GUILD_LOG_MAX];
    openmmo_guild *g = &c->guild;
    int nent = 0;
    s16 total = 0;

    if (mmo_game_read_guild_log(c->gbuf, n, scratch,
                                OPENMMO_GUILD_LOG_MAX, &nent, &total) != 0) {
        g->malformed++;
        g->valid = 1;
        return 0;
    }
    g->valid = 1;
    g->log_valid = 1;
    g->log_total = total;
    g->log_count = nent;
    memcpy(g->log, scratch, (size_t)nent * sizeof scratch[0]);
    return 1;
}

static void mail_fill_event(openmmo_client *c, openmmo_event *e)
{
    const openmmo_mail *m = &c->mail;

    e->mail.sent = m->listed_sent;
    e->mail.count = m->count;
    e->mail.inbox = m->inbox;
    e->mail.outbox = m->sent;
    e->mail.result = m->result;
    e->mail.mail_id = m->have_detail ? m->detail.mail_id : 0;
    e->mail.is_detail = m->have_detail;
    e->mail.subject[0] = '\0';
    e->mail.other[0] = '\0';
    if (m->have_detail) {
        snprintf(e->mail.subject, sizeof e->mail.subject, "%s",
                 m->detail.subject);
        snprintf(e->mail.other, sizeof e->mail.other, "%s",
                 m->detail.sender[0] ? m->detail.sender : m->detail.recipient);
    }
}

/* Apply a mailbox page (s2c 0x97). Replaces the held page. */
static int seat_mail_page(openmmo_client *c, size_t n)
{
    mmo_mail scratch[OPENMMO_MAIL_MAX];
    openmmo_mail *m = &c->mail;
    int nent = 0;
    s16 page = 0;
    u8 sent = 0;

    if (mmo_game_read_mail_page(c->gbuf, n, scratch,
                                OPENMMO_MAIL_MAX, &nent, &page, &sent) != 0) {
        m->malformed++;
        m->valid = 1;
        return 0;
    }
    m->valid = 1;
    m->listed_sent = sent;
    m->page = page;
    m->count = nent;
    m->dropped = 0;
    memcpy(m->entry, scratch, (size_t)nent * sizeof scratch[0]);
    return 1;
}

/* Apply the three mailbox counts (s2c 0x98). */
static int seat_mail_counts(openmmo_client *c, size_t n)
{
    openmmo_mail *m = &c->mail;
    s16 inbox = 0, seen = 0, sent = 0;

    if (mmo_game_read_mail_counts(c->gbuf, n, &inbox, &seen, &sent) != 0) {
        m->malformed++;
        m->valid = 1;
        return 0;
    }
    m->valid = 1;
    m->inbox = inbox;
    m->inbox_seen = seen;
    m->sent = sent;
    return 1;
}

/* Apply a compose result (s2c 0x96). */
static int seat_mail_result(openmmo_client *c, size_t n)
{
    openmmo_mail *m = &c->mail;
    u8 code = 0;

    if (mmo_game_read_mail_result(c->gbuf, n, &code) != 0) {
        m->malformed++;
        m->valid = 1;
        return 0;
    }
    m->valid = 1;
    m->result = (int)code;
    m->result_seq++;
    return 1;
}

/* Apply one opened letter (s2c 0x99). */
static int seat_mail_detail(openmmo_client *c, size_t n)
{
    openmmo_mail *m = &c->mail;
    mmo_mail one;
    int present = 0;
    u8 sent = 0;

    memset(&one, 0, sizeof one);
    if (mmo_game_read_mail_detail(c->gbuf, n, &one, &present, &sent) != 0) {
        m->malformed++;
        m->valid = 1;
        return 0;
    }
    m->valid = 1;
    m->have_detail = present;
    if (present) {
        m->detail = one;
        m->detail_sent = sent ? 1 : 0;
    }
    return 1;
}

static void link_fill_event(openmmo_client *c, openmmo_event *e)
{
    e->link.present = c->link.present;
    e->link.count = c->link.count;
    e->link.leader = c->link.leader;
    e->link.added = 0;
    e->link.removed = 0;
    e->link.name[0] = '\0';
}

/* Apply a link snapshot (s2c 0xD0). present 0 clears the roster. */
static int seat_link_snapshot(openmmo_client *c, size_t n)
{
    mmo_link_member scratch[OPENMMO_LINK_MAX];
    openmmo_link *l = &c->link;
    int nent = 0, present = 0;
    s64 leader = 0;

    if (mmo_game_read_link_snapshot(c->gbuf, n, scratch,
                                    OPENMMO_LINK_MAX, &nent,
                                    &present, &leader) != 0) {
        l->malformed++;
        l->valid = 1;
        return 0;
    }
    l->valid = 1;
    l->present = present;
    l->leader = present ? leader : 0;
    l->count = present ? nent : 0;
    l->dropped = 0;
    if (present)
        memcpy(l->member, scratch, (size_t)nent * sizeof scratch[0]);
    return 1;
}

/* Apply one link member (s2c 0xD1). */
static int seat_link_add(openmmo_client *c, size_t n, mmo_link_member *got)
{
    openmmo_link *l = &c->link;
    mmo_link_member one;
    int i;

    if (mmo_game_read_link_member(c->gbuf, n, &one) != 0) {
        l->malformed++;
        l->valid = 1;
        return 0;
    }
    l->valid = 1;
    l->present = 1;
    for (i = 0; i < l->count; i++) {
        if (l->member[i].entity_id == one.entity_id) {
            l->member[i] = one;
            if (got)
                *got = one;
            return 1;
        }
    }
    if (l->count >= OPENMMO_LINK_MAX) {
        l->dropped++;
        if (got)
            *got = one;
        return 1;
    }
    l->member[l->count++] = one;
    if (got)
        *got = one;
    return 1;
}

/* Apply a link drop (s2c 0xD2). */
static int seat_link_remove(openmmo_client *c, size_t n, s64 *removed,
                            char *name, size_t name_cap)
{
    openmmo_link *l = &c->link;
    s64 gone = 0, leader = 0;
    int i;

    if (mmo_game_read_link_remove(c->gbuf, n, &gone, &leader) != 0) {
        l->malformed++;
        l->valid = 1;
        return 0;
    }
    l->valid = 1;
    l->leader = leader;
    for (i = 0; i < l->count; i++) {
        if (l->member[i].entity_id != gone)
            continue;
        if (name && name_cap)
            snprintf(name, name_cap, "%s", l->member[i].name);
        if (i + 1 < l->count)
            memmove(&l->member[i], &l->member[i + 1],
                    (size_t)(l->count - i - 1) * sizeof l->member[0]);
        l->count--;
        break;
    }
    if (l->count == 0)
        l->present = 0;
    if (removed)
        *removed = gone;
    return 1;
}

/* Apply a new captain (s2c 0xDA). */
static int seat_link_leader(openmmo_client *c, size_t n)
{
    openmmo_link *l = &c->link;
    s64 leader = 0;

    if (mmo_game_read_link_leader(c->gbuf, n, &leader) != 0) {
        l->malformed++;
        l->valid = 1;
        return 0;
    }
    l->valid = 1;
    l->leader = leader;
    return 1;
}

/* Apply a mart catalog (s2c 0x23). A close clears the shelf; an open
 * replaces it. */
static int seat_shop(openmmo_client *c, size_t n)
{
    mmo_shop_item scratch[OPENMMO_SHOP_MAX];
    mmo_shop_catalog cat;
    openmmo_shop *s = &c->shop;
    int i;

    if (mmo_game_read_shop_catalog(c->gbuf, n, &cat, scratch, OPENMMO_SHOP_MAX) != 0) {
        s->malformed++;
        s->valid = 1;
        return 0;
    }
    s->valid = 1;
    s->open = cat.open;
    s->kind = cat.kind;
    s->flags = cat.flags;
    s->currency_kind = cat.currency_kind;
    s->npc_entity_id = cat.has_npc ? cat.npc_entity_id : 0;
    s->total = cat.total;
    s->dropped = cat.open && cat.total > OPENMMO_SHOP_MAX
                     ? cat.total - OPENMMO_SHOP_MAX : 0;
    s->count = 0;
    s->untranslatable = 0;
    if (!cat.open) {
        memset(s->item, 0, sizeof s->item);
        return 1;
    }
    s->count = cat.count;
    for (i = 0; i < s->count; i++) {
        const char *why = NULL;
        s->item[i].item_id = scratch[i].item_id;
        s->item[i].stock = scratch[i].stock;
        s->item[i].price = scratch[i].price;
        s->item[i].engine_id = mmo_id_item_from_server(scratch[i].item_id, &why);
        if (s->item[i].engine_id == MMO_ID_NONE)
            s->untranslatable++;
    }
    return 1;
}

/* The most records one container packet carries: the count is a byte. A PC of
 * 660 therefore arrives in several, the first replacing the container and the
 * rest merging into it. */
#define STORAGE_RECORDS_PER_PACKET 255

/*
 * Apply one flat container to a store the client holds, on the same terms the party is applied
 * on: a delete empties it, `hasChange` replaces it before the records land, and without that
 * flag each record is merged in, keyed by the monster's own id.
 */
static int seat_container(openmmo_client *c, size_t n, int want,
                          openmmo_storage *s, int cap)
{
    mmo_pokemon_container ct;

    if (!c->ct_scratch) {
        c->ct_scratch = calloc(STORAGE_RECORDS_PER_PACKET, sizeof *c->ct_scratch);
        if (!c->ct_scratch)
            return 0;
    }
    if (mmo_game_read_pokemon_container(c->gbuf, n, &ct, c->ct_scratch,
                                        STORAGE_RECORDS_PER_PACKET) != 0) {
        /* A body this reader cannot walk names no container, so it is counted
         * against the store the caller asked about and nowhere else, counting
         * it in every store would report one malformed packet three times. */
        if (want == MMO_CONTAINER_PC) {
            s->malformed++;
            s->valid = 1;
        }
        return 0;
    }
    if (ct.container != want)
        return 0;                         /* somebody else's container */

    s->valid = 1;
    s->trailing = ct.trailing;
    if (ct.deleted) {
        s->count = 0;
        s->total = 0;
        return 1;
    }
    if (!s->mon) {
        s->mon = calloc((size_t)cap, sizeof *s->mon);
        if (!s->mon) {
            s->malformed++;
            return 0;
        }
    }
    if (ct.has_change) {
        s->count = 0;
        s->dropped = 0;
    }
    s->total = ct.total;
    for (int i = 0; i < ct.count; i++) {
        int at = -1;
        for (int j = 0; j < s->count; j++) {
            if (s->mon[j].id == c->ct_scratch[i].id) {
                at = j;
                break;
            }
        }
        if (at < 0) {
            if (s->count >= cap) {
                s->dropped++;
                continue;
            }
            at = s->count++;
        }
        seat_mon(&s->mon[at], &c->ct_scratch[i]);
    }
    /* Slot order, so the list reads the way the boxes are drawn. Insertion sort:
     * a merge touches a handful of entries in an already-ordered list. */
    for (int i = 1; i < s->count; i++) {
        openmmo_party_mon held = s->mon[i];
        int j = i - 1;
        while (j >= 0 && s->mon[j].slot > held.slot) {
            s->mon[j + 1] = s->mon[j];
            j--;
        }
        s->mon[j + 1] = held;
    }
    return 1;
}

static int seat_storage(openmmo_client *c, size_t n)
{
    return seat_container(c, n, MMO_CONTAINER_PC, &c->storage,
                          OPENMMO_STORAGE_MAX);
}

static int seat_daycare(openmmo_client *c, size_t n)
{
    return seat_container(c, n, MMO_CONTAINER_DAYCARE, &c->daycare,
                          OPENMMO_DAYCARE_MAX);
}

/* Apply the incubator slots the server sent. Wear is the server's count, not a
 * local tally: the client holds what it was last told. */
static int seat_incubators(openmmo_client *c, size_t n)
{
    mmo_incubators in;

    if (mmo_game_read_incubators(c->gbuf, n, &in) != 0)
        return 0;
    c->incubators.valid = 1;
    c->incubators.total = in.total;
    c->incubators.count = in.count;
    for (int i = 0; i < in.count; i++) {
        c->incubators.slot[i].id = in.slot[i].id;
        c->incubators.slot[i].uses_left = in.slot[i].uses_left;
    }
    return 1;
}

/* Apply a breeding forecast. Every number in it is the server's arithmetic; the
 * ids cross the 4.1 map so a host can draw them, and nothing else is touched. */
static int seat_forecast(openmmo_client *c, size_t n)
{
    mmo_breeding_forecast f;
    openmmo_breeding_forecast *out = &c->forecast;

    if (mmo_game_read_breeding_forecast(c->gbuf, n, &f) != 0)
        return 0;

    memset(out, 0, sizeof *out);
    out->valid = 1;
    out->parent[0] = f.parent[0];
    out->parent[1] = f.parent[1];
    out->has_preview = f.has_preview;
    if (!f.has_preview)
        return 1;

    const char *why = NULL;
    out->species = f.species;
    out->engine_species = mmo_id_species_from_server((u16)f.species, &why);
    if (out->engine_species == MMO_ID_NONE)
        out->engine_species = 0;
    out->form = f.form;
    out->stat_count = f.stat_count;
    for (int i = 0; i < f.stat_count; i++) {
        out->stat[i].guaranteed = f.stat[i].guaranteed;
        out->stat[i].item_id = f.stat[i].item_id;
        out->stat[i].outcome_count = f.stat[i].outcome_count;
        for (int j = 0; j < f.stat[i].outcome_count; j++) {
            out->stat[i].outcome[j].value = f.stat[i].outcome[j].value;
            out->stat[i].outcome[j].chance = f.stat[i].outcome[j].chance;
            out->stat[i].outcome[j].label = f.stat[i].outcome[j].label;
        }
    }
    out->shininess_count = f.shininess_count;
    for (int i = 0; i < f.shininess_count; i++)
        out->shininess[i] = f.shininess[i];
    out->move_count = f.move_count;
    for (int i = 0; i < f.move_count; i++) {
        out->move_id[i] = f.move_id[i];
        out->move_source[i] = f.move_source[i];
        u16 ds = f.move_id[i] > 0
                     ? mmo_id_move_from_server((u16)f.move_id[i], &why)
                     : MMO_ID_NONE;
        out->move[i] = ds == MMO_ID_NONE ? 0 : ds;
    }
    out->ot = f.ot;
    out->nature = f.nature;
    out->gender_selectable = f.gender_selectable;
    out->gender_cost[0] = f.gender_cost[0];
    out->gender_cost[1] = f.gender_cost[1];
    return 1;
}

/*
 * Fold one already-decoded world-state frame (in c->gbuf, length n) into the session's world-
 * state snapshot.
 */
static void seat_world_state(openmmo_client *c, u8 opcode, size_t n)
{
    switch (opcode) {
    case MMO_GAME_OP_WORLD_FLAG_RESET: {
        /* The flag table leads the block and resets the whole progression store:
         * a login or resync re-sends the flags and vars in full, so clear the
         * prior set before seating the new one, then inflate the table into it. */
        memset(&c->story, 0, sizeof c->story);
        c->ws.flag_count = 0;
        int count = 0;
        if (mmo_game_read_world_flag_reset(c->gbuf, n,
                (u8 *)c->story.flag_block, OPENMMO_WORLD_FLAG_BLOCK,
                c->story.flag_block_len, OPENMMO_WORLD_FLAG_GROUPS, &count) == 0)
            c->story.flag_group_count = count;
        break;
    }
    case MMO_GAME_OP_POKEMON_CONTAINER:
        seat_party(c, n);
        seat_storage(c, n);
        seat_daycare(c, n);
        break;
    case MMO_GAME_OP_EGG_INCUBATORS:
        seat_incubators(c, n);
        break;
    case MMO_GAME_OP_LOCAL_PLAYER_STATE: {
        mmo_local_player_state ps;
        if (mmo_game_read_local_player_state(c->gbuf, n, &ps) != 0)
            break;
        c->ws.region = ps.region;
        c->ws.map_id = ps.map_id;
        c->ws.x = ps.x;
        c->ws.y = ps.y;
        c->ws.z = ps.z;
        c->ws.money = ps.money;
        c->ws.gender = ps.gender;
        c->ws.var_count = ps.var_count;
        c->ws.badge_count = ps.badge_count;
        memcpy(c->ws.badges, ps.badges, sizeof c->ws.badges);
        /* The species list here is a summary of a party the client does not have
         * yet. The party container carries the party itself and arrives later in
         * the same block, so this only fills the summary until it does, once the
         * party is held, the container is what the summary reflects. */
        if (!c->party.valid) {
            c->ws.party_count = ps.party_count;
            c->ws.party_untranslatable = 0;
            for (int i = 0; i < ps.party_count; i++) {
                const char *why = NULL;
                u16 sp = mmo_id_species_from_server(ps.party_dex[i], &why);
                c->ws.party_species[i] = sp;
                if (sp == MMO_ID_NONE)
                    c->ws.party_untranslatable++;
            }
        }
        /* Seat the story variables into the store (GBA 0x4000+key, keyed by the
         * wire offset). The store is the server's; the client only reads it. */
        c->story.region = ps.region;
        c->story.var_count = 0;
        for (int i = 0; i < ps.var_stored; i++) {
            int k = ps.var_key[i];
            if (k < 0 || k >= OPENMMO_STORY_VARS)
                continue;
            if (!c->story.var_present[k])
                c->story.var_count++;
            c->story.var[k] = ps.var_val[i];
            c->story.var_present[k] = 1;
        }
        break;
    }
    case MMO_GAME_OP_STORY_FLAG: {
        int flag_id = 0, enabled = 0;
        if (mmo_game_read_story_flag(c->gbuf, n, NULL, &flag_id, &enabled) == 0) {
            story_set_flag(&c->story, flag_id, enabled);
            c->ws.flag_count = c->story.set_flag_count;
        }
        break;
    }
    case MMO_GAME_OP_SCRIPT_STATE:
        seat_script_state(c, n);
        break;
    case MMO_GAME_OP_OBJECTIVE_BULK:
        seat_objective_bulk(c, n);
        break;
    case MMO_GAME_OP_OBJECTIVE:
        seat_objective(c, n, NULL);
        break;
    case MMO_GAME_OP_MENU_VISIBILITY:
    case MMO_GAME_OP_MENU_PAGE:
    case MMO_GAME_OP_NAME_CHOICES:
    case MMO_GAME_OP_OPTION_LIST:
    case MMO_GAME_OP_LIST_WINDOW:
    case MMO_GAME_OP_MENU_PROMPT_OPEN:
    case MMO_GAME_OP_MENU_PROMPT_CLOSE:
    case MMO_GAME_OP_CONFIRM_PROMPT:
    case MMO_GAME_OP_VIEW_SCALE:
        seat_ui(c, opcode, n);
        break;
    case MMO_GAME_OP_DIGEST:
    case MMO_GAME_OP_DIGEST_BATCH:
    case MMO_GAME_OP_TRANSFER_BEGIN:
    case MMO_GAME_OP_TRANSFER_APPEND:
    case MMO_GAME_OP_STREAM_CHUNK:
    case MMO_GAME_OP_IMAGE_CHUNK:
        seat_sync(c, opcode, n);
        break;
    case MMO_GAME_OP_RENTAL_SET:
    case MMO_GAME_OP_TOURNEY_PAGE:
    case MMO_GAME_OP_TOURNEY_COUNT:
    case MMO_GAME_OP_TOURNEY_MATCHUPS:
    case MMO_GAME_OP_SCORE_BOARD:
        seat_compete(c, opcode, n);
        break;
    case MMO_GAME_OP_GM_LOOKUP:
    case MMO_GAME_OP_GM_PANEL:
    case MMO_GAME_OP_GM_PANEL_ENTRY:
        seat_gm(c, opcode, n);
        break;
    case MMO_GAME_OP_FRIEND_LIST:
        seat_friend_list(c, n, NULL);
        break;
    case MMO_GAME_OP_FRIEND_INSERT:
        seat_friend_insert(c, n, NULL);
        break;
    case MMO_GAME_OP_FRIEND_DELETE:
        seat_friend_delete(c, n, NULL, NULL, 0);
        break;
    case MMO_GAME_OP_FRIEND_ONLINE:
        seat_friend_online(c, n, NULL, NULL, NULL, 0);
        break;
    case MMO_GAME_OP_GUILD_MEMBERSHIP:
        seat_guild_membership(c, n);
        break;
    case MMO_GAME_OP_GUILD_PROFILE:
        seat_guild_profile(c, n);
        break;
    case MMO_GAME_OP_GUILD_MEMBER_ADD:
        seat_guild_member_add(c, n, NULL);
        break;
    case MMO_GAME_OP_GUILD_RANK_CHANGE:
        seat_guild_rank_change(c, n, NULL, NULL, NULL, 0);
        break;
    case MMO_GAME_OP_GUILD_MEMBER_DROP:
        seat_guild_member_drop(c, n, NULL, NULL, 0);
        break;
    case MMO_GAME_OP_GUILD_PRESENCE:
        seat_guild_presence(c, n, NULL, NULL, NULL, 0);
        break;
    case MMO_GAME_OP_GUILD_MEMBERS:
        seat_guild_members(c, n, NULL);
        break;
    case MMO_GAME_OP_GUILD_LOG:
        seat_guild_log(c, n);
        break;
    case MMO_GAME_OP_MAIL_PAGE:
        seat_mail_page(c, n);
        break;
    case MMO_GAME_OP_MAIL_COUNTS:
        seat_mail_counts(c, n);
        break;
    case MMO_GAME_OP_MAIL_RESULT:
        seat_mail_result(c, n);
        break;
    case MMO_GAME_OP_MAIL_DETAIL:
        seat_mail_detail(c, n);
        break;
    case MMO_GAME_OP_LINK_SNAPSHOT:
        seat_link_snapshot(c, n);
        break;
    case MMO_GAME_OP_LINK_ADD:
        seat_link_add(c, n, NULL);
        break;
    case MMO_GAME_OP_LINK_REMOVE:
        seat_link_remove(c, n, NULL, NULL, 0);
        break;
    case MMO_GAME_OP_LINK_LEADER:
        seat_link_leader(c, n);
        break;
    case MMO_GAME_OP_SELECT_CHAR: {
        mmo_character ch;

        if (mmo_game_read_selected_character(c->gbuf, n, &ch) != 0)
            break;
        if (ch.id != 0)
            c->ws.character_id = ch.id;
        if (ch.name[0] != '\0')
            snprintf(c->ws.name, sizeof c->ws.name, "%s", ch.name);
        if (ch.gender == 0 || ch.gender == 1)
            c->ws.gender = ch.gender;
        break;
    }
    case MMO_GAME_OP_ITEM_STACKS:
        seat_bag_snapshot(c, n);
        break;
    case MMO_GAME_OP_ITEM_STACK_UPDATE:
        seat_bag_update(c, n);
        break;
    case MMO_GAME_OP_REGISTERED_SEAT: {
        u16 reg;
        /* Absolute, 0 included: a stale register is cleared by the seat, the
         * same contract the script-state seat keeps. */
        if (mmo_game_read_registered_item(c->gbuf, n, &reg) == 0) {
            c->bag.registered_item = reg;
            c->bag.registered_valid = 1;
        } else {
            c->bag.malformed++;
        }
        break;
    }
    case MMO_GAME_OP_LOCAL_CHAR_DELTA: {
        mmo_local_character_delta d;
        if (mmo_game_read_local_character_delta(c->gbuf, n, &d) == 0 && d.has_money)
            c->ws.money = d.money;
        break;
    }
    default:
        break; /* consumed to keep the stream in step; nothing seated */
    }
}

/* --- the preloaded-map registry ---------------------------------------- * */

/* Forget the whole registry, called when the map set changes wholesale (a fresh
 * join or a warp), since a warp's deleteCache throws the server-side cache away. */
static void map_reg_reset(openmmo_client *c)
{
    c->nmaps = 0;
    memset(c->maps, 0, sizeof c->maps);
}

/*
 * Upsert a parsed LoadMap into the registry, keyed by (bank,map). A repeat load overwrites; a
 * new map appends until the table is full, after which the extra is dropped (logged by the
 * caller's live harness, never silently rendered).
 */
static mmo_map_grid *register_map(openmmo_client *c, const mmo_load_map *lm)
{
    if (lm->region_id != c->map_region) {
        /* The region changed under us; the old neighbourhood no longer applies. */
        map_reg_reset(c);
        c->map_region = lm->region_id;
    }
    mmo_map_grid *g = NULL;
    for (int i = 0; i < c->nmaps; i++) {
        if (c->maps[i].bank == lm->bank_id && c->maps[i].map == lm->map_id) {
            g = &c->maps[i];
            break;
        }
    }
    if (!g) {
        if (c->nmaps >= MMO_MAP_REG_MAX)
            return NULL;
        g = &c->maps[c->nmaps++];
    }
    g->valid = 1;
    g->bank = lm->bank_id;
    g->map = lm->map_id;
    g->width = lm->width;
    g->height = lm->height;
    g->weather = lm->weather;
    g->lighting = lm->lighting;
    g->map_type = lm->map_type;
    g->encounter_type = lm->encounter_type;
    g->connection_count = lm->connection_count;
    for (int i = 0; i < lm->connection_count; i++)
        g->connections[i] = lm->connections[i];
    return g;
}

static const mmo_map_grid *find_map(const openmmo_client *c, int bank, int map)
{
    for (int i = 0; i < c->nmaps; i++)
        if (c->maps[i].valid && c->maps[i].bank == bank && c->maps[i].map == map)
            return &c->maps[i];
    return NULL;
}

/* Seat the current map's scene into the world snapshot from a just-parsed primary
 * LoadMap and announce it, so a host applies the server's weather/lighting rather
 * than the local map file's. Fires before the JOINED or WARP that shares the load. */
static void seat_current_scene(openmmo_client *c, const mmo_load_map *lm)
{
    c->ws.map_is_nds = lm->is_nds;
    c->ws.weather = lm->weather;
    c->ws.lighting = lm->lighting;
    c->ws.map_type = lm->map_type;
    c->ws.encounter_type = lm->encounter_type;
    c->ws.map_width = lm->width;
    c->ws.map_height = lm->height;

    openmmo_event *e = evq_push(c, OPENMMO_EV_MAP);
    if (!e)
        return;
    e->map.region = lm->region_id;
    e->map.bank = lm->bank_id;
    e->map.map = lm->map_id;
    e->map.is_nds = lm->is_nds;
    e->map.weather = lm->weather;
    e->map.lighting = lm->lighting;
    e->map.map_type = lm->map_type;
    e->map.encounter_type = lm->encounter_type;
    e->map.width = lm->width;
    e->map.height = lm->height;
}

/* Server Direction ordinals (common/enums/Direction.kt), as they ride the wire and
 * key a map's connection edges. */
enum { WIRE_DOWN = 0, WIRE_UP = 1, WIRE_LEFT = 2, WIRE_RIGHT = 3 };

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* A seamless map-edge crossing the server ran without a LoadMap. */
static void predict_edge_crossing(openmmo_client *c, const mmo_map_grid *tgt,
                                  const mmo_map_connection *conn, int wire,
                                  int from_x, int from_z, int ds_dir)
{
    int ex, ez;
    if (wire == WIRE_LEFT)       ex = tgt->width - 1;
    else if (wire == WIRE_RIGHT) ex = 0;
    else                         ex = clampi(from_x - conn->offset, 0, tgt->width - 1);
    if (wire == WIRE_UP)         ez = tgt->height - 1;
    else if (wire == WIRE_DOWN)  ez = 0;
    else                         ez = clampi(from_z - conn->offset, 0, tgt->height - 1);

    c->ws.bank_id = tgt->bank;
    c->ws.map_id = tgt->map;
    c->ws.x = ex;
    c->ws.z = ez;
    c->self_x = ex;
    c->self_z = ez;
    c->self_dir = ds_dir;
    c->have_self_tile = 1;

    /* The server left the old map's presence group and streams the new map's peers
     * in-world, so forget the old ones (mirrors a warp's entity reset). */
    openmmo_client_reset_entities(c);

    mmo_load_map lm = { .region_id = c->ws.region, .bank_id = tgt->bank,
                        .map_id = tgt->map, .is_nds = 0, .weather = tgt->weather,
                        .lighting = tgt->lighting, .map_type = tgt->map_type,
                        .encounter_type = tgt->encounter_type,
                        .width = tgt->width, .height = tgt->height };
    seat_current_scene(c, &lm);           /* MAP before WARP, as on a full warp */

    openmmo_event *e = evq_push(c, OPENMMO_EV_WARP);
    if (!e)
        return;
    e->warp.region = c->ws.region;
    e->warp.bank = tgt->bank;
    e->warp.map = tgt->map;
    e->warp.x = ex;
    e->warp.z = ez;
    e->warp.dir = ds_dir;
    e->warp.seamless = 1;
}

/*
 * If a just-sent move steps off the current map's edge into a connection, predict the crossing
 * the server is about to run.
 */
static void try_edge_crossing(openmmo_client *c, int from_x, int from_z, int ds_dir)
{
    const mmo_map_grid *cur = find_map(c, c->ws.bank_id, c->ws.map_id);
    if (!cur || cur->width <= 0 || cur->height <= 0)
        return;                           /* current map unknown or NDS: no GBA edge */

    int wire = mmo_game_dir_from_ds(ds_dir);
    int tox = from_x, toz = from_z;
    switch (wire) {
    case WIRE_RIGHT: tox++; break;
    case WIRE_LEFT:  tox--; break;
    case WIRE_DOWN:  toz++; break;
    case WIRE_UP:    toz--; break;
    }
    if (tox >= 0 && tox < cur->width && toz >= 0 && toz < cur->height)
        return;                           /* still inside the current map */

    const mmo_map_connection *conn = NULL;
    for (int i = 0; i < cur->connection_count; i++) {
        if (cur->connections[i].direction == wire) {
            conn = &cur->connections[i];
            break;
        }
    }
    if (!conn)
        return;                           /* edge with no neighbour: a wall, not a crossing */

    const mmo_map_grid *tgt = find_map(c, conn->target_bank, conn->target_map);
    if (!tgt || tgt->width <= 0 || tgt->height <= 0)
        return;                           /* neighbour not held: recover via snap-back */

    predict_edge_crossing(c, tgt, conn, wire, from_x, from_z, ds_dir);
}

/*
 * The world-state block streams in after SelectCharacter: WorldFlagTableReset,
 * LocalPlayerState, story flags, the pokemon containers and the bag snapshot, each folded into
 * the session snapshot, then a LoadMap that terminates the block and asks the client to re-
 * request its player.
 */
static void apply_chat(openmmo_client *c, u8 opcode, size_t n);

static void handle_select_window(openmmo_client *c, const u8 *body, size_t blen)
{
    u8 opcode;
    size_t n = recv_game_frame(c, body, blen, &opcode);
    if (n == (size_t)-1) { fail(c, "join: world-state frame failed to decode"); return; }

    seat_world_state(c, opcode, n);
    apply_chat(c, opcode, n);

    if (opcode != MMO_GAME_OP_LOAD_MAP)
        return; /* part of the block, not the trigger */

    /* LoadMap closes the block: the snapshot is complete and a host may seat it. */
    mmo_load_map lm;
    if (mmo_game_read_load_map(c->gbuf, n, &lm) == 0) {
        /* The server may place a character in a world this build has no maps
         * for, every account that predates one-world Sinnoh has such a
         * character. There is nothing to draw, so say which region it is and
         * what is missing rather than joining onto an empty screen. */
        if (!mmo_region_is_drawable(lm.region_id) && !c->allow_undrawable_region) {
            char why[128];
            mmo_region_explain_undrawable(lm.region_id, why, sizeof why);
            fail(c, why);
            return;
        }
        c->ws.region = lm.region_id;
        c->ws.bank_id = lm.bank_id;
        c->ws.map_id = lm.map_id;
        map_reg_reset(c);
        c->map_region = lm.region_id;
        register_map(c, &lm);
        seat_current_scene(c, &lm);
    }
    c->ws.valid = 1;
    /* LocalPlayerState already named the spawn tile. Use it as the from-tile
     * until the self LoadEntity arrives, and still use it if that packet is
     * dropped. Field z is the server's y. */
    if (!c->have_self_tile) {
        c->self_x = c->ws.x;
        c->self_z = c->ws.y;
        c->have_self_tile = 1;
    }

    /* Say who runs a field script, before asking for the player: the server
     * starts this map's entry script on that request, and it must already know
     * whether this client is going to run the same scene itself. Ordered on the
     * stream, so there is no window where both would. */
    {
        mmo_wbuf own;
        mmo_wbuf_init(&own);
        mmo_game_write_script_owner(&own, c->local_scripts);
        int rc = own.err
                     ? (fail(c, "join: could not frame ClientScriptOwnership"), -1)
                     : send_game_packet(c, MMO_GAME_OP_SCRIPT_OWNER,
                                        own.data, own.len,
                                        "join: could not send ClientScriptOwnership");
        mmo_wbuf_free(&own);
        if (rc != 0)
            return;
    }

    if (send_game_packet(c, MMO_GAME_OP_REQ_PLAYER, NULL, 0,
                         "join: could not request the player") != 0)
        return;
    c->ph = P_GAME_ENTER;
    arm_deadline(c);
}

static void apply_presence(openmmo_client *c, u8 opcode, size_t n);

/* After RequestPlayer the server places the avatar and streams entities, then a
 * RenderScreen(true), the "you are standing in the map" signal. That is the join
 * FSM's terminal; the JOINED event fires here, not at JoinResponse. */
static void handle_enter_window(openmmo_client *c, const u8 *body, size_t blen)
{
    u8 opcode;
    size_t n = recv_game_frame(c, body, blen, &opcode);
    if (n == (size_t)-1) { fail(c, "join: player-load frame failed to decode"); return; }

    /* SelectedCharacter follows the world-state LoadMap, so it lands in this
     * window. Seat it the same way the select window would. The friends
     * list is sent in this same burst, before RenderScreen, so JOINED
     * already holds it. */
    if (opcode == MMO_GAME_OP_SELECT_CHAR
        || opcode == MMO_GAME_OP_FRIEND_LIST
        || opcode == MMO_GAME_OP_FRIEND_INSERT
        || opcode == MMO_GAME_OP_FRIEND_DELETE
        || opcode == MMO_GAME_OP_FRIEND_ONLINE
        || opcode == MMO_GAME_OP_GUILD_MEMBERSHIP
        || opcode == MMO_GAME_OP_GUILD_PROFILE
        || opcode == MMO_GAME_OP_GUILD_MEMBER_ADD
        || opcode == MMO_GAME_OP_GUILD_RANK_CHANGE
        || opcode == MMO_GAME_OP_GUILD_MEMBER_DROP
        || opcode == MMO_GAME_OP_GUILD_PRESENCE
        || opcode == MMO_GAME_OP_GUILD_MEMBERS
        || opcode == MMO_GAME_OP_GUILD_LOG
        || opcode == MMO_GAME_OP_MAIL_PAGE
        || opcode == MMO_GAME_OP_MAIL_COUNTS
        || opcode == MMO_GAME_OP_MAIL_RESULT
        || opcode == MMO_GAME_OP_MAIL_DETAIL
        || opcode == MMO_GAME_OP_MENU_VISIBILITY
        || opcode == MMO_GAME_OP_MENU_PAGE
        || opcode == MMO_GAME_OP_NAME_CHOICES
        || opcode == MMO_GAME_OP_OPTION_LIST
        || opcode == MMO_GAME_OP_LIST_WINDOW
        || opcode == MMO_GAME_OP_MENU_PROMPT_OPEN
        || opcode == MMO_GAME_OP_MENU_PROMPT_CLOSE
        || opcode == MMO_GAME_OP_CONFIRM_PROMPT
        || opcode == MMO_GAME_OP_VIEW_SCALE
        || opcode == MMO_GAME_OP_DIGEST
        || opcode == MMO_GAME_OP_DIGEST_BATCH
        || opcode == MMO_GAME_OP_TRANSFER_BEGIN
        || opcode == MMO_GAME_OP_TRANSFER_APPEND
        || opcode == MMO_GAME_OP_STREAM_CHUNK
        || opcode == MMO_GAME_OP_IMAGE_CHUNK)
        seat_world_state(c, opcode, n);

    /*
     * The player's own LoadEntity arrives first in this burst; remember its id and spawn tile
     * so the in-game stream never spawns a second avatar for ourselves, and so the local
     * player has an authoritative from-tile to move off of.
     */
    if (opcode == MMO_GAME_OP_LOAD_ENTITY && !c->have_self
        && c->ws.character_id != 0) {
        mmo_load_entity le;
        if (mmo_game_read_load_entity(c->gbuf, n, &le) == 0
            && le.entity_id == (u32)c->ws.character_id) {
            c->self_entity_id = le.entity_id;
            c->have_self = 1;
            /* Server (x,y) -> field (x,z), the axis convention entity.c and the
             * send path both use. */
            c->self_x = le.x;
            c->self_z = le.y;
            c->self_dir = mmo_game_dir_to_ds((u8)le.facing);
            c->have_self_tile = 1;
        }
    }

    /* The server preloads the spawn map's neighbours right after the primary map,
     * so their LoadMaps arrive in this burst. Fold them into the registry (no MAP
     * event, they are not the current map) so a later edge crossing has their
     * dimensions and connections to place the player with. */
    if (opcode == MMO_GAME_OP_LOAD_MAP) {
        mmo_load_map lm;
        if (mmo_game_read_load_map(c->gbuf, n, &lm) == 0)
            register_map(c, &lm);
        return;
    }

    /*
     * After the self LoadEntity, the burst carries a LoadEntity for every player already
     * standing on the map (the newcomer's presence snapshot), then RenderScreen.
     */
    apply_presence(c, opcode, n);
    apply_chat(c, opcode, n);

    if (opcode != MMO_GAME_OP_RENDER || n < 1 || c->gbuf[0] == 0)
        return; /* self LoadEntity, NPCs, presence, awaited but not the terminal */

    c->ph = P_IN_GAME;

    /* This same window ends both a fresh join and the arrival leg of a warp. A warp
     * kept the coarse status at IN_GAME throughout (only the fine phase left it), so
     * it emits a WARP carrying the server's placement rather than a second JOINED. */
    if (c->warping) {
        c->warping = 0;
        if (c->have_self_tile) { c->ws.x = c->self_x; c->ws.z = c->self_z; }
        openmmo_event *e = evq_push(c, OPENMMO_EV_WARP);
        if (e) {
            e->warp.region = c->ws.region;
            e->warp.bank = c->ws.bank_id;
            e->warp.map = c->ws.map_id;
            e->warp.x = c->self_x;
            e->warp.z = c->self_z;
            e->warp.dir = c->self_dir;
        }
        return;
    }

    set_status(c, OPENMMO_IN_GAME);
    openmmo_event *e = evq_push(c, OPENMMO_EV_JOINED);
    if (e) {
        e->join.playtime = c->join_playtime;
        e->join.reward_points = c->join_reward_points;
        e->join.balance = c->join_balance;
    }
}

/*
 * A positional move addressed to our OWN entity id is the server correcting the local player:
 * it never echoes our accepted steps (only observers get those), so a self-addressed move is
 * always a reject/snap-back.
 */
static void self_correct(openmmo_client *c, int bank, int map, int x, int y, int dir)
{
    if (bank >= 0 && (bank != c->ws.bank_id || map != c->ws.map_id)) {
        c->ws.bank_id = bank;
        c->ws.map_id = map;
        const mmo_map_grid *g = find_map(c, bank, map);
        if (g) {
            mmo_load_map lm = { .region_id = c->ws.region, .bank_id = bank,
                                .map_id = map, .is_nds = 0, .weather = g->weather,
                                .lighting = g->lighting, .map_type = g->map_type,
                                .encounter_type = g->encounter_type,
                                .width = g->width, .height = g->height };
            seat_current_scene(c, &lm);
        }
    }

    c->self_x = x;
    c->self_z = y;
    c->have_self_tile = 1;

    openmmo_event *e = evq_push(c, OPENMMO_EV_SELF_CORRECT);
    if (!e)
        return; /* queue full: the host is not draining; drop rather than block */
    e->entity.slot = -1;
    e->entity.localid = -1;
    e->entity.x = x;
    e->entity.z = y;
    e->entity.dir = mmo_game_dir_to_ds((u8)dir);
}

/* Feed one already-decoded presence frame (in c->gbuf, length n) into the reconcile model. */
static int entity_is_self(const openmmo_client *c, u32 id)
{
    return c->self_entity_id != 0 && id == c->self_entity_id;
}

static int npc_index(const openmmo_npc_store *s, s64 id)
{
    int i;

    if (!s || id == 0)
        return -1;
    for (i = 0; i < OPENMMO_NPC_MAX; i++)
        if (s->npc[i].live && s->npc[i].entity_id == id)
            return i;
    return -1;
}

static int npc_upsert(openmmo_npc_store *s, const mmo_npc_spawn *sp)
{
    int i, free_i = -1;

    if (!s || !sp || sp->entity_id == 0)
        return -1;
    s->valid = 1;
    i = npc_index(s, sp->entity_id);
    if (i < 0) {
        for (i = 0; i < OPENMMO_NPC_MAX; i++) {
            if (!s->npc[i].live) {
                free_i = i;
                break;
            }
        }
        if (free_i < 0) {
            s->dropped++;
            return -1;
        }
        i = free_i;
        s->count++;
    }
    memset(&s->npc[i], 0, sizeof s->npc[i]);
    s->npc[i].entity_id = sp->entity_id;
    s->npc[i].graphics_id = sp->graphics_id;
    s->npc[i].x = sp->x;
    s->npc[i].y = sp->y;
    s->npc[i].facing = mmo_game_dir_to_ds((u8)sp->facing);
    s->npc[i].movement = sp->movement;
    s->npc[i].live = 1;
    return i;
}

static int npc_remove(openmmo_npc_store *s, s64 id)
{
    int i = npc_index(s, id);

    if (i < 0)
        return 0;
    memset(&s->npc[i], 0, sizeof s->npc[i]);
    s->count--;
    if (s->count < 0)
        s->count = 0;
    return 1;
}

static void remember_self_tile(openmmo_client *c, int x, int y, int facing)
{
    c->have_self = 1;
    c->self_x = x;
    c->self_z = y;
    c->self_dir = mmo_game_dir_to_ds((u8)facing);
    c->have_self_tile = 1;
}

static void apply_presence(openmmo_client *c, u8 opcode, size_t n)
{
    switch (opcode) {
    case MMO_GAME_OP_LOAD_ENTITY: {
        mmo_load_entity le;
        if (mmo_game_read_load_entity(c->gbuf, n, &le) != 0)
            break;
        /* A warp clears have_self but keeps the id. Matching on the flag
         * alone spawned a second Lucas at the new map's tile. */
        if (entity_is_self(c, le.entity_id)) {
            remember_self_tile(c, le.x, le.y, le.facing);
            /* A character who logged out on the water comes back on it: the
             * server derives the mount from the tile it puts them on and sends
             * it here, in the same field a change arrives in later. */
            c->self_mount = le.transportation;
            if (openmmo_entity_slot_of(&c->ent, le.entity_id) >= 0)
                openmmo_entity_despawn(&c->ent, le.entity_id);
            break;
        }
        /*
         * Gender picks the trainer model. A kept SkinSet that names a catalog body (type >=
         * 512) is seated as that object-event sprite; anything else in the SkinSet is recorded
         * and not drawn.
         */
        openmmo_entity_appearance ap;
        int body_gfx;

        memset(&ap, 0, sizeof ap);
        ap.gender = (uint8_t)(le.gender & 1);
        ap.skins = le.appearance;
        /* Which picture walks behind them. The dex id is the official client's own field and
         * the three beside it are ours (flags & 0x20); a packet without them
         * asks for the ordinary male coat, which is the picture the official client draws. */
        if (le.has_follower) {
            int fg = mmo_follower_gfx(le.follower_dex, le.follower_form,
                                      le.follower_gender, le.follower_shiny);

            /* A species with no follower art, or one no package carries, is a
             * peer with nobody behind them rather than a peer with the wrong
             * Pokemon behind them. */
            if (fg >= 0) {
                ap.has_follower = 1;
                ap.follower_gfx = fg;
            }
        }
        body_gfx = mmo_appearance_resolve(ap.gender, &le.appearance);
        if (body_gfx != mmo_appearance_gender_gfx(ap.gender)) {
            ap.has_body = 1;
            ap.gfx = body_gfx;
        }
        if (le.name_prefix[0] != '\0')
            snprintf(ap.name, sizeof ap.name, "[%s]%s", le.name_prefix, le.name);
        else
            snprintf(ap.name, sizeof ap.name, "%s", le.name);
        openmmo_entity_spawn(&c->ent, le.entity_id, le.x, le.y,
                             mmo_game_dir_to_ds((u8)le.facing), ap);
        break;
    }
    case MMO_GAME_OP_GBA_MOVE: {
        mmo_gba_move gm;
        if (mmo_game_read_gba_move(c->gbuf, n, &gm) != 0)
            break;
        if (entity_is_self(c, gm.entity_id)) {
            self_correct(c, gm.bank_id, gm.map_id, gm.x, gm.y, gm.direction);
            break;
        }
        openmmo_entity_set_target(&c->ent, gm.entity_id, gm.x, gm.y,
                                  mmo_game_dir_to_ds((u8)gm.direction), gm.movement_mode);
        break;
    }
    case MMO_GAME_OP_ENTITY_MOVE: {
        mmo_entity_move em;
        if (mmo_game_read_entity_move(c->gbuf, n, &em) != 0)
            break;
        if (entity_is_self(c, em.entity_id)) {
            self_correct(c, -1, -1, em.x, em.y, em.direction); /* NDS move: no bank/map */
            break;
        }
        openmmo_entity_set_target(&c->ent, em.entity_id, em.x, em.y,
                                  mmo_game_dir_to_ds((u8)em.direction), -1);
        break;
    }
    case MMO_GAME_OP_NPC_SPAWN: {
        mmo_npc_spawn sp;
        if (mmo_game_read_npc_spawn(c->gbuf, n, &sp) != 0) {
            c->npcs.malformed++;
            c->npcs.valid = 1;
            break;
        }
        npc_upsert(&c->npcs, &sp);
        break;
    }
    case MMO_GAME_OP_ENTITY_SNAP: {
        mmo_entity_snap sn;
        s64 id64;
        int ni;

        if (mmo_game_read_entity_id64(c->gbuf, n, &id64) == 0
            && (ni = npc_index(&c->npcs, id64)) >= 0) {
            if (mmo_game_read_entity_snap(c->gbuf, n, &sn) != 0)
                break;
            c->npcs.npc[ni].x = sn.x;
            c->npcs.npc[ni].y = sn.y;
            c->npcs.npc[ni].facing = mmo_game_dir_to_ds((u8)sn.direction);
            break;
        }
        if (mmo_game_read_entity_snap(c->gbuf, n, &sn) != 0)
            break;
        if (entity_is_self(c, sn.entity_id)) {
            self_correct(c, sn.bank_id, sn.map_id, sn.x, sn.y, sn.direction);
            break;
        }
        /* A whole pose, not a step: the game client drops the entity's queued
         * walk and places it, so this is a set-down rather than a target to
         * walk to. Walking one was right for the one-tile hops a cutscene
         * uses and a slide through the scenery for anything longer. */
        openmmo_entity_place(&c->ent, sn.entity_id, sn.x, sn.y,
                             mmo_game_dir_to_ds((u8)sn.direction));
        break;
    }
    case MMO_GAME_OP_ENTITY_TURN: {
        u32 id; int facing;
        if (mmo_game_read_face_turn(c->gbuf, n, &id, &facing) != 0)
            break;
        if (facing == -1) {
            if (c->have_self_tile)
                openmmo_entity_face_toward(&c->ent, id, c->self_x, c->self_z);
        } else if (facing >= 0 && facing <= 3) {
            openmmo_entity_face(&c->ent, id, mmo_game_dir_to_ds((u8)facing));
        }
        break;
    }
    case MMO_GAME_OP_TRANSPORTATION: {
        u32 id; int mount;
        if (mmo_game_read_transportation(c->gbuf, n, &id, &mount) != 0)
            break;
        /* Ours is the only one that changes anything today. A peer's mount is
         * a remote sprite this client has no path to swap mid-session (its body
         * is seated once, at the spawn), so it is dropped rather than half
         * applied. */
        if (entity_is_self(c, id))
            c->self_mount = mount;
        break;
    }
    case MMO_GAME_OP_ENTITY_LEAVE: {
        s64 id64;
        u32 id;

        if (mmo_game_read_entity_id64(c->gbuf, n, &id64) == 0
            && npc_remove(&c->npcs, id64))
            break;
        if (mmo_game_read_entity_leave(c->gbuf, n, &id) == 0
            && !entity_is_self(c, id))
            openmmo_entity_despawn(&c->ent, id);
        break;
    }
    case MMO_GAME_OP_ENTITY_DESPAWN: {
        u32 id;
        if (mmo_game_read_entity_despawn(c->gbuf, n, &id) == 0
            && !entity_is_self(c, id))
            openmmo_entity_despawn(&c->ent, id);
        break;
    }
    default:
        break; /* not a presence packet, later phases own it */
    }
}

static openmmo_battle_field *field_of(openmmo_client *c, u32 entity_id)
{
    int i;
    if (entity_id == 0)
        return NULL;
    for (i = 0; i < c->field.n_mons; i++)
        if (c->field.mon[i].entity_id == entity_id)
            return &c->field;
    if (c->field.n_mons >= OPENMMO_BATTLE_MONS)
        return &c->field;
    i = c->field.n_mons++;
    memset(&c->field.mon[i], 0, sizeof c->field.mon[i]);
    c->field.mon[i].entity_id = entity_id;
    c->field.mon[i].hp = -1;
    c->field.mon[i].max_hp = -1;
    c->field.mon[i].faint = -1;
    return &c->field;
}

static int field_index(openmmo_client *c, u32 entity_id)
{
    int i;
    if (!field_of(c, entity_id))
        return -1;
    for (i = 0; i < c->field.n_mons; i++)
        if (c->field.mon[i].entity_id == entity_id)
            return i;
    return -1;
}

static void apply_delta_to_field(openmmo_client *c, const mmo_battle_entity_delta *d)
{
    int i = field_index(c, d->entity_id);
    if (i < 0)
        return;
    if (d->hp >= 0)
        c->field.mon[i].hp = d->hp;
    if (d->faint >= 0)
        c->field.mon[i].faint = d->faint;
    if (d->species >= 0)
        c->field.mon[i].species = d->species;
    if (d->xp_level >= 0)
        c->field.mon[i].level = d->xp_level;
    if (d->level >= 0)
        c->field.mon[i].level = d->level;
    if (d->xp >= 0)
        c->field.mon[i].xp = d->xp;
    if (d->have_stats) {
        c->field.mon[i].have_stats = 1;
        memcpy(c->field.mon[i].stats, d->stats, sizeof d->stats);
        /* A level-up is stats without an experience group. A catch
         * delta carries both on a brand-new entity. */
        if (!(d->mask & MMO_BATTLE_DELTA_EXPERIENCE))
            c->field.leveled = 1;
    }
}

static void apply_move_to_field(openmmo_client *c, const mmo_battle_move_event *mv)
{
    int t, s, i;
    field_of(c, mv->source_entity);
    for (t = 0; t < mv->n_targets; t++) {
        field_of(c, mv->target[t].entity_id);
        for (s = 0; s < mv->target[t].n_subs; s++) {
            const mmo_battle_sub_event *sub = &mv->target[t].sub[s];
            u32 id = mv->target[t].entity_id;
            if (sub->type != MMO_BATTLE_SUB_HP)
                continue;
            i = field_index(c, id);
            if (i >= 0) {
                c->field.mon[i].hp = sub->hp;
                if (sub->hp < 1)
                    c->field.mon[i].faint = 1;
            }
        }
    }
}

static void apply_switch_to_field(openmmo_client *c, const mmo_battle_switch_in *sw)
{
    int i;
    if (sw->entity_id == 0)
        return;
    i = field_index(c, sw->entity_id);
    if (i < 0)
        return;
    c->field.mon[i].species = sw->species;
    c->field.mon[i].level = sw->level;
    if (sw->full_block) {
        c->field.mon[i].hp = sw->hp;
        c->field.mon[i].max_hp = sw->max_hp;
    }
}

static void apply_battle_event(openmmo_client *c, u8 opcode, size_t n)
{
    openmmo_event *e;
    openmmo_event ev;
    memset(&ev, 0, sizeof ev);
    ev.kind = OPENMMO_EV_BATTLE_EVENT;
    ev.battle.opcode = opcode;

    switch (opcode) {
    case MMO_GAME_OP_BATTLE_ENTITY_DELTA: {
        mmo_battle_entity_delta d;
        if (mmo_game_read_battle_entity_delta(c->gbuf, n, &d) != 0)
            return;
        apply_delta_to_field(c, &d);
        ev.battle.entity_id = d.entity_id;
        ev.battle.kind = (int)d.mask;
        ev.battle.hp = d.hp;
        break;
    }
    case MMO_GAME_OP_BATTLE_MOVE_EVENT: {
        mmo_battle_move_event mv;
        if (mmo_game_read_battle_move_event(c->gbuf, n, &mv) != 0)
            return;
        apply_move_to_field(c, &mv);
        ev.battle.entity_id = mv.source_entity;
        ev.battle.move = mv.source_move;
        ev.battle.kind = mv.kind;
        ev.battle.n_targets = mv.n_targets;
        if (mv.n_targets > 0 && mv.target[0].n_subs > 0
            && mv.target[0].sub[0].type == MMO_BATTLE_SUB_HP)
            ev.battle.hp = mv.target[0].sub[0].hp;
        c->anims.n = mmo_battle_map_move(&mv, c->anims.cmd,
                                         MMO_BATTLE_ANIM_MAX);
        if (c->anims.n < 0)
            c->anims.n = 0;
        if (c->anims.n > MMO_BATTLE_ANIM_MAX)
            c->anims.n = MMO_BATTLE_ANIM_MAX;
        break;
    }
    case MMO_GAME_OP_BATTLE_SWITCH_IN: {
        mmo_battle_switch_in sw;
        if (mmo_game_read_battle_switch_in(c->gbuf, n, &sw) != 0)
            return;
        apply_switch_to_field(c, &sw);
        c->field.switch_owed = 0;
        ev.battle.entity_id = sw.entity_id;
        ev.battle.kind = sw.new_slot;
        ev.battle.hp = sw.hp;
        c->anims.n = mmo_battle_map_switch(&sw, c->anims.cmd,
                                           MMO_BATTLE_ANIM_MAX);
        if (c->anims.n < 0)
            c->anims.n = 0;
        break;
    }
    case MMO_GAME_OP_BATTLE_SLOT_FLAG: {
        mmo_battle_slot_flag fl;
        if (mmo_game_read_battle_slot_flag(c->gbuf, n, &fl) != 0)
            return;
        ev.battle.kind = fl.immediate;
        ev.battle.hp = fl.slot;
        if (!fl.immediate) {
            c->field.switch_owed = 1;
            c->anims.n = mmo_battle_map_switch_prompt(c->anims.cmd,
                                                      MMO_BATTLE_ANIM_MAX);
            if (c->anims.n < 0)
                c->anims.n = 0;
        } else {
            c->field.switch_owed = 0;
        }
        break;
    }
    case MMO_GAME_OP_BATTLE_LIST_EVENT: {
        mmo_battle_list_event le;
        if (mmo_game_read_battle_list_event(c->gbuf, n, &le) != 0)
            return;
        ev.battle.kind = le.sub_kind;
        ev.battle.hp = le.value;
        if (le.sub_kind == MMO_BATTLE_LIST_CATCH) {
            c->field.caught = 1;
            c->field.caught_item = le.value;
            ev.battle.entity_id = c->field.caught_id;
            c->anims.n = mmo_battle_map_catch(le.value, c->field.caught_id,
                                              c->anims.cmd,
                                              MMO_BATTLE_ANIM_MAX);
            if (c->anims.n < 0)
                c->anims.n = 0;
        }
        break;
    }
    case MMO_GAME_OP_SOCIAL_LIST_ADD: {
        mmo_monster mon;
        if (mmo_game_read_monster(c->gbuf, n, &mon) != 0)
            return;
        c->field.caught_id = (u32)mon.id;
        c->field.caught_species = mon.dex_id;
        ev.battle.entity_id = (u32)mon.id;
        ev.battle.kind = mon.dex_id;
        ev.battle.hp = mon.hp;
        break;
    }
    case MMO_GAME_OP_BATTLE_STAT_COUNTERS: {
        mmo_battle_stat_counters sc;
        if (mmo_game_read_battle_stat_counters(c->gbuf, n, &sc) != 0)
            return;
        c->field.xp_gained = sc.base;
        ev.battle.entity_id = sc.entity_id;
        ev.battle.kind = sc.base;
        c->anims.n = mmo_battle_map_reward(sc.entity_id, sc.base,
                                           c->field.leveled, c->anims.cmd,
                                           MMO_BATTLE_ANIM_MAX);
        if (c->anims.n < 0)
            c->anims.n = 0;
        break;
    }
    case MMO_GAME_OP_BATTLE_BULK_STATE: {
        mmo_battle_bulk_state bs;
        if (mmo_game_read_battle_bulk_state(c->gbuf, n, &bs) != 0)
            return;
        c->field.ended = 1;
        c->field.prize = bs.prize;
        ev.battle.kind = bs.phase;
        ev.battle.hp = bs.prize;
        break;
    }
    case MMO_GAME_OP_BATTLE_QUEUED_EVENT: {
        mmo_battle_queued q;
        if (mmo_game_read_battle_queued(c->gbuf, n, &q) != 0)
            return;
        c->field.prompt = q.flag;
        ev.battle.kind = q.value;
        break;
    }
    case MMO_GAME_OP_BATTLE_SLOT_EVENT: {
        mmo_battle_slot_event sl;
        if (mmo_game_read_battle_slot_event(c->gbuf, n, &sl) != 0)
            return;
        ev.battle.kind = sl.event_type;
        break;
    }
    default:
        return;
    }

    c->field.n_events++;
    e = evq_push(c, OPENMMO_EV_BATTLE_EVENT);
    if (e)
        e->battle = ev.battle;
}

/* The battle scene's two doors, both of them the server's to open. */
static void apply_self_presence(openmmo_client *c, int status)
{
    if (status == MMO_PRESENCE_IN_BATTLE) {
        if (c->battle == OPENMMO_BATTLE_NONE) {
            c->battle = OPENMMO_BATTLE_ENTERING;
            memset(&c->field, 0, sizeof c->field);
            memset(&c->anims, 0, sizeof c->anims);
        }
        return;
    }
    if (status != MMO_PRESENCE_OVERWORLD || c->battle == OPENMMO_BATTLE_NONE)
        return;

    c->battle = OPENMMO_BATTLE_NONE;
    if (send_game_packet(c, MMO_GAME_OP_MAP_LOADED_ACK, NULL, 0,
                         "battle: could not acknowledge the overworld") != 0)
        return;
    evq_push(c, OPENMMO_EV_BATTLE_END);
}

/*
 * In-world presence: decode one frame and feed it to the reconcile model, unless it is a
 * server-driven warp.
 */
static void handle_in_game(openmmo_client *c, const u8 *body, size_t blen)
{
    u8 opcode;
    size_t n = recv_game_frame(c, body, blen, &opcode);
    if (n == (size_t)-1) { disconnected(c, "in-game frame failed to decode"); return; }

    apply_chat(c, opcode, n);

    if (opcode == MMO_GAME_OP_KEEPALIVE) {
        /* Our own ping, echoed. Only the token we are waiting on counts: a late
         * answer to a written-off ping would otherwise be credited to the one
         * after it and read as a link that never recovered. */
        u8 token = 0;
        if (c->ping_inflight
            && mmo_game_read_keepalive(c->gbuf, n, &token) == 0
            && token == c->ping_token) {
            s64 rtt = now_ms() - c->ping_sent_ms;

            c->ping_inflight = 0;
            if (rtt >= 0 && rtt <= MMO_PING_TIMEOUT_MS)
                c->ping_rtt_ms = (int)rtt;
        }
        return;
    }

    if (opcode == MMO_GAME_OP_MAP_TRANSITION) {
        /*
         * The map is changing under us: the server has already left the old map's presence
         * group, so forget its entities (and any queued events for them) before the new map's
         * stream arrives. The self tile is re-seated by the arrival's own LoadEntity, so drop
         * it too.
         */
        c->warping = 1;
        c->have_self = 0;
        c->have_self_tile = 0;
        openmmo_client_reset_entities(c);
        map_reg_reset(c);
        return;
    }

    if (c->warping && opcode == MMO_GAME_OP_LOAD_MAP) {
        /*
         * The primary destination map. Seat the placement and scene the server chose, then re-
         * request the player to end the transition, the arrival burst (self LoadEntity, NPCs,
         * peers, RenderScreen(true)) follows in the enter window, exactly as it does on a
         * fresh join.
         */
        mmo_load_map lm;
        if (mmo_game_read_load_map(c->gbuf, n, &lm) == 0) {
            c->ws.region = lm.region_id;
            c->ws.bank_id = lm.bank_id;
            c->ws.map_id = lm.map_id;
            register_map(c, &lm);
            seat_current_scene(c, &lm);
        }
        if (send_game_packet(c, MMO_GAME_OP_REQ_PLAYER, NULL, 0,
                             "warp: could not request the player") != 0)
            return;
        c->ph = P_GAME_ENTER;
        arm_deadline(c);
        return;
    }

    /* A LoadMap arriving in-world (not during a warp) is a neighbour the server
     * preloaded after a seamless edge crossing, hold it for the next crossing. */
    if (opcode == MMO_GAME_OP_LOAD_MAP) {
        mmo_load_map lm;
        if (mmo_game_read_load_map(c->gbuf, n, &lm) == 0)
            register_map(c, &lm);
        return;
    }

    /* Weather the server changes under the player, independent of a map load. */
    if (opcode == MMO_GAME_OP_WEATHER_MODE) {
        int mode = 0, enabled = 0;
        if (mmo_game_read_map_weather_mode(c->gbuf, n, &mode, &enabled) == 0) {
            openmmo_event *e = evq_push(c, OPENMMO_EV_WEATHER);
            if (e) { e->weather.source = 0; e->weather.mode = mode;
                     e->weather.enabled = enabled; }
        }
        return;
    }
    if (opcode == MMO_GAME_OP_WEATHER_CONTROL) {
        int effect = 0;
        if (mmo_game_read_weather_control(c->gbuf, n, &effect) == 0) {
            openmmo_event *e = evq_push(c, OPENMMO_EV_WEATHER);
            if (e) { e->weather.source = 1; e->weather.effect = effect; }
        }
        return;
    }

    /* Which scene the server holds an entity in. For the local player it is the
     * battle scene's door, both ways; for anyone else it is a peer's state, which
     * nothing here draws yet. */
    if (opcode == MMO_GAME_OP_ENTITY_PRESENCE) {
        mmo_entity_presence pr;
        /*
         * entity_is_self, not have_self: the flag says the spawn tile has been re-seated, and
         * a warp clears it while keeping the id.
         */
        if (mmo_game_read_entity_presence(c->gbuf, n, &pr) == 0 &&
            entity_is_self(c, pr.entity_id))
            apply_self_presence(c, pr.status);
        return;
    }

    /*
     * The server opened a battle. The overworld no longer decides encounters, so a wild battle
     * begins here, because the server sent it, rather than from a local grass-step RNG roll.
     */
    if (opcode == MMO_GAME_OP_BATTLE_FIELD_STATE) {
        mmo_battle_field_state bf;
        if (mmo_game_read_battle_field_state(c->gbuf, n, &bf) == 0) {
            c->battle = OPENMMO_BATTLE_ACTIVE;
            memset(&c->field, 0, sizeof c->field);
            c->field.valid = 1;
            c->anims.n = 0;
            openmmo_event *e = evq_push(c, OPENMMO_EV_ENCOUNTER);
            if (e) {
                e->encounter.wild = bf.wild;
                e->encounter.background = bf.background;
                e->encounter.foe_species = 0;
                e->encounter.foe_level = 0;
                if (mmo_game_read_battle_foe(c->gbuf, n, &bf) == 0) {
                    e->encounter.foe_species = bf.foe_species;
                    e->encounter.foe_level = bf.foe_level;
                }
            }
        }
        return;
    }

    /* The ordered event stream that drives the presenter. A body this side
     * cannot walk is dropped rather than guessed: the inflater stays in step
     * either way, and a later packet with a known shape still lands. */
    if (opcode == MMO_GAME_OP_BATTLE_ENTITY_DELTA
        || opcode == MMO_GAME_OP_BATTLE_MOVE_EVENT
        || opcode == MMO_GAME_OP_BATTLE_SWITCH_IN
        || opcode == MMO_GAME_OP_BATTLE_STAT_COUNTERS
        || opcode == MMO_GAME_OP_BATTLE_BULK_STATE
        || opcode == MMO_GAME_OP_BATTLE_QUEUED_EVENT
        || opcode == MMO_GAME_OP_BATTLE_SLOT_EVENT
        || opcode == MMO_GAME_OP_BATTLE_SLOT_FLAG
        || opcode == MMO_GAME_OP_BATTLE_LIST_EVENT
        || (opcode == MMO_GAME_OP_SOCIAL_LIST_ADD
            && c->battle == OPENMMO_BATTLE_ACTIVE)) {
        apply_battle_event(c, opcode, n);
        return;
    }

    /* The server changed a monster container under the player, a heal at a
     * Center, a caught monster, a level. The party is the server's, so the client
     * takes what it is sent and tells the host to redraw. */
    if (opcode == MMO_GAME_OP_POKEMON_CONTAINER) {
        if (seat_party(c, n)) {
            openmmo_event *e = evq_push(c, OPENMMO_EV_PARTY);
            if (e) { e->party.count = c->party.count;
                     e->party.total = c->party.total; }
            /* An egg the server has stopped calling an egg has hatched. The
             * client did not count a step or decide anything: the flag came off
             * the record it was sent. */
            for (int i = 0; i < c->hatched_count; i++) {
                const openmmo_party_mon *m = &c->party.mon[c->hatched[i]];
                openmmo_event *h = evq_push(c, OPENMMO_EV_EGG_HATCH);
                if (!h)
                    break;
                h->hatch.monster_id = m->id;
                h->hatch.party_index = c->hatched[i];
                h->hatch.species = m->dex_id;
                h->hatch.engine_species = m->species;
            }
        }
        if (seat_storage(c, n)) {
            openmmo_event *e = evq_push(c, OPENMMO_EV_STORAGE);
            if (e) { e->storage.count = c->storage.count;
                     e->storage.total = c->storage.total; }
        }
        /* The day care raises no event of its own: nothing draws it, and the
         * one reader it has (the box sync, which binds a boarder's server id to
         * the slot the game is holding it in) reads the store on its own tick
         * the way it already reads the PC. */
        seat_daycare(c, n);
        return;
    }

    /* The server replaced the bag, or changed one stack. The bag is the
     * server's, so the client takes what it is sent and tells the host to
     * redraw. A 0x42 that is a battle-side add (no item tag) is left alone. */
    if (opcode == MMO_GAME_OP_ITEM_STACKS) {
        if (seat_bag_snapshot(c, n)) {
            openmmo_event *e = evq_push(c, OPENMMO_EV_BAG);
            if (e) { e->bag.count = c->bag.count;
                     e->bag.total = c->bag.total; }
        }
        return;
    }
    if (opcode == MMO_GAME_OP_ITEM_STACK_UPDATE) {
        if (seat_bag_update(c, n)) {
            openmmo_event *e = evq_push(c, OPENMMO_EV_BAG);
            if (e) { e->bag.count = c->bag.count;
                     e->bag.total = c->bag.total; }
        }
        return;
    }

    /* The server opened or closed a mart. The shelf is the server's, so
     * the client takes what it is sent. Buy and sell are intents; the
     * bag and cash that follow a settlement are what then change. */
    if (opcode == MMO_GAME_OP_SHOP_CATALOG) {
        if (seat_shop(c, n)) {
            openmmo_event *e = evq_push(c, OPENMMO_EV_SHOP);
            if (e) {
                e->shop.open = c->shop.open;
                e->shop.count = c->shop.count;
                e->shop.total = c->shop.total;
            }
        }
        return;
    }

    /* The server opened or closed a dialog box. The string is the
     * client's: this takes the text id and tells the host to draw it. */
    if (opcode == MMO_GAME_OP_DIALOG_ACTION) {
        if (seat_dialog(c, n)) {
            openmmo_event *e = evq_push(c, OPENMMO_EV_DIALOG);
            if (e) {
                e->dialog.open = c->dialog.open;
                e->dialog.close = c->dialog.close;
                e->dialog.in_dialog = openmmo_client_in_dialog(c);
                e->dialog.action_type = c->dialog.action_type;
                e->dialog.text_id = c->dialog.text_id;
                e->dialog.resolved = c->dialog.resolved;
                e->dialog.bank = c->dialog.bank;
                e->dialog.entry = c->dialog.entry;
                e->dialog.choice_count = c->dialog.choice_count;
            }
        }
        return;
    }

    /* The server sent objective progress. A bulk replaces the
     * store; a single upserts. The join's empty 0xD3 already
     * seated it; this is a live change. */
    if (opcode == MMO_GAME_OP_OBJECTIVE_BULK) {
        if (seat_objective_bulk(c, n)) {
            openmmo_event *e = evq_push(c, OPENMMO_EV_OBJECTIVE);
            if (e) {
                e->objective.replace = 1;
                e->objective.count = c->objectives.count;
                e->objective.id = 0;
                e->objective.value = 0;
                e->objective.tally = 0;
            }
        }
        return;
    }
    if (opcode == MMO_GAME_OP_OBJECTIVE) {
        mmo_objective one;
        if (seat_objective(c, n, &one)) {
            openmmo_event *e = evq_push(c, OPENMMO_EV_OBJECTIVE);
            if (e) {
                e->objective.replace = 0;
                e->objective.count = c->objectives.count;
                e->objective.id = one.id;
                e->objective.value = one.value;
                e->objective.tally = one.count;
            }
        }
        return;
    }

    /* The server sent the friends list or a live delta. The join's
     * 0x63 already seated it; this is a live change. */
    if (opcode == MMO_GAME_OP_FRIEND_LIST) {
        u8 mode = 0;

        if (seat_friend_list(c, n, &mode)) {
            openmmo_event *e = evq_push(c, OPENMMO_EV_FRIENDS);
            if (e) {
                e->friends.replace = (mode == 0);
                e->friends.count = c->friends.count;
                e->friends.online = friends_online_count(&c->friends);
                e->friends.player = 0;
                e->friends.added = 0;
                e->friends.removed = 0;
                e->friends.name[0] = '\0';
            }
        }
        return;
    }
    if (opcode == MMO_GAME_OP_FRIEND_INSERT) {
        mmo_friend one;

        if (seat_friend_insert(c, n, &one)) {
            openmmo_event *e = evq_push(c, OPENMMO_EV_FRIENDS);
            if (e) {
                e->friends.replace = 0;
                e->friends.count = c->friends.count;
                e->friends.online = friends_online_count(&c->friends);
                e->friends.player = one.player;
                e->friends.added = 1;
                e->friends.removed = 0;
                snprintf(e->friends.name, sizeof e->friends.name, "%s",
                         one.name);
            }
        }
        return;
    }
    if (opcode == MMO_GAME_OP_FRIEND_DELETE) {
        s64 player = 0;
        char name[MMO_TEXT_BYTES(MMO_CHAR_NAME_MAX)];

        name[0] = '\0';
        if (seat_friend_delete(c, n, &player, name, sizeof name)) {
            openmmo_event *e = evq_push(c, OPENMMO_EV_FRIENDS);
            if (e) {
                e->friends.replace = 0;
                e->friends.count = c->friends.count;
                e->friends.online = friends_online_count(&c->friends);
                e->friends.player = player;
                e->friends.added = 0;
                e->friends.removed = 1;
                snprintf(e->friends.name, sizeof e->friends.name, "%s", name);
            }
        }
        return;
    }
    if (opcode == MMO_GAME_OP_FRIEND_ONLINE) {
        s64 player = 0;
        u8 bit = 0;
        char name[MMO_TEXT_BYTES(MMO_CHAR_NAME_MAX)];

        name[0] = '\0';
        if (seat_friend_online(c, n, &player, &bit, name, sizeof name)) {
            openmmo_event *e = evq_push(c, OPENMMO_EV_FRIENDS);
            if (e) {
                e->friends.replace = 0;
                e->friends.count = c->friends.count;
                e->friends.online = friends_online_count(&c->friends);
                e->friends.player = player;
                e->friends.added = 0;
                e->friends.removed = 0;
                snprintf(e->friends.name, sizeof e->friends.name, "%s", name);
            }
        }
        return;
    }

    /* The server sent guild membership or a live delta. The join's
     * 0x80 already seated it; this is a live change. */
    if (opcode == MMO_GAME_OP_GUILD_MEMBERSHIP) {
        if (seat_guild_membership(c, n)) {
            openmmo_event *e = evq_push(c, OPENMMO_EV_GUILD);
            if (e)
                guild_fill_event(c, e);
        }
        return;
    }
    if (opcode == MMO_GAME_OP_GUILD_PROFILE) {
        if (seat_guild_profile(c, n)) {
            openmmo_event *e = evq_push(c, OPENMMO_EV_GUILD);
            if (e)
                guild_fill_event(c, e);
        }
        return;
    }
    if (opcode == MMO_GAME_OP_GUILD_MEMBERS) {
        if (seat_guild_members(c, n, NULL)) {
            openmmo_event *e = evq_push(c, OPENMMO_EV_GUILD);
            if (e)
                guild_fill_event(c, e);
        }
        return;
    }
    if (opcode == MMO_GAME_OP_GUILD_MEMBER_ADD) {
        mmo_guild_member one;

        if (seat_guild_member_add(c, n, &one)) {
            openmmo_event *e = evq_push(c, OPENMMO_EV_GUILD);
            if (e) {
                guild_fill_event(c, e);
                e->guild.added = 1;
                snprintf(e->guild.member, sizeof e->guild.member, "%s",
                         one.appearance.name);
            }
        }
        return;
    }
    if (opcode == MMO_GAME_OP_GUILD_RANK_CHANGE) {
        char name[MMO_TEXT_BYTES(MMO_CHAR_NAME_MAX)];

        name[0] = '\0';
        if (seat_guild_rank_change(c, n, NULL, NULL, name, sizeof name)) {
            openmmo_event *e = evq_push(c, OPENMMO_EV_GUILD);
            if (e) {
                guild_fill_event(c, e);
                snprintf(e->guild.member, sizeof e->guild.member, "%s", name);
            }
        }
        return;
    }
    if (opcode == MMO_GAME_OP_GUILD_MEMBER_DROP) {
        char name[MMO_TEXT_BYTES(MMO_CHAR_NAME_MAX)];

        name[0] = '\0';
        if (seat_guild_member_drop(c, n, NULL, name, sizeof name)) {
            openmmo_event *e = evq_push(c, OPENMMO_EV_GUILD);
            if (e) {
                guild_fill_event(c, e);
                e->guild.removed = 1;
                snprintf(e->guild.member, sizeof e->guild.member, "%s", name);
            }
        }
        return;
    }
    if (opcode == MMO_GAME_OP_GUILD_PRESENCE) {
        char name[MMO_TEXT_BYTES(MMO_CHAR_NAME_MAX)];

        name[0] = '\0';
        if (seat_guild_presence(c, n, NULL, NULL, name, sizeof name)) {
            openmmo_event *e = evq_push(c, OPENMMO_EV_GUILD);
            if (e) {
                guild_fill_event(c, e);
                snprintf(e->guild.member, sizeof e->guild.member, "%s", name);
            }
        }
        return;
    }
    if (opcode == MMO_GAME_OP_GUILD_LOG) {
        if (seat_guild_log(c, n)) {
            openmmo_event *e = evq_push(c, OPENMMO_EV_GUILD);
            if (e)
                guild_fill_event(c, e);
        }
        return;
    }

    if (opcode == MMO_GAME_OP_MAIL_PAGE) {
        if (seat_mail_page(c, n)) {
            openmmo_event *e = evq_push(c, OPENMMO_EV_MAIL);
            if (e)
                mail_fill_event(c, e);
        }
        return;
    }
    if (opcode == MMO_GAME_OP_MAIL_COUNTS) {
        if (seat_mail_counts(c, n)) {
            openmmo_event *e = evq_push(c, OPENMMO_EV_MAIL);
            if (e)
                mail_fill_event(c, e);
        }
        return;
    }
    if (opcode == MMO_GAME_OP_MAIL_RESULT) {
        if (seat_mail_result(c, n)) {
            openmmo_event *e = evq_push(c, OPENMMO_EV_MAIL);
            if (e)
                mail_fill_event(c, e);
        }
        return;
    }
    if (opcode == MMO_GAME_OP_MAIL_DETAIL) {
        if (seat_mail_detail(c, n)) {
            openmmo_event *e = evq_push(c, OPENMMO_EV_MAIL);
            if (e)
                mail_fill_event(c, e);
        }
        return;
    }

    if (opcode == MMO_GAME_OP_LINK_SNAPSHOT) {
        if (seat_link_snapshot(c, n)) {
            openmmo_event *e = evq_push(c, OPENMMO_EV_LINK);
            if (e)
                link_fill_event(c, e);
        }
        return;
    }
    if (opcode == MMO_GAME_OP_LINK_ADD) {
        mmo_link_member one;

        if (seat_link_add(c, n, &one)) {
            openmmo_event *e = evq_push(c, OPENMMO_EV_LINK);
            if (e) {
                link_fill_event(c, e);
                e->link.added = 1;
                snprintf(e->link.name, sizeof e->link.name, "%s", one.name);
            }
        }
        return;
    }
    if (opcode == MMO_GAME_OP_LINK_REMOVE) {
        char name[MMO_TEXT_BYTES(MMO_CHAR_NAME_MAX)];

        name[0] = '\0';
        if (seat_link_remove(c, n, NULL, name, sizeof name)) {
            openmmo_event *e = evq_push(c, OPENMMO_EV_LINK);
            if (e) {
                link_fill_event(c, e);
                e->link.removed = 1;
                snprintf(e->link.name, sizeof e->link.name, "%s", name);
            }
        }
        return;
    }
    if (opcode == MMO_GAME_OP_MENU_VISIBILITY
        || opcode == MMO_GAME_OP_MENU_PAGE
        || opcode == MMO_GAME_OP_NAME_CHOICES
        || opcode == MMO_GAME_OP_OPTION_LIST
        || opcode == MMO_GAME_OP_LIST_WINDOW
        || opcode == MMO_GAME_OP_MENU_PROMPT_OPEN
        || opcode == MMO_GAME_OP_MENU_PROMPT_CLOSE
        || opcode == MMO_GAME_OP_CONFIRM_PROMPT
        || opcode == MMO_GAME_OP_VIEW_SCALE) {
        if (seat_ui(c, opcode, n)) {
            openmmo_event *e = evq_push(c, OPENMMO_EV_UI);
            if (e)
                ui_fill_event(&c->ui, e);
        }
        return;
    }
    if (opcode == MMO_GAME_OP_DIGEST
        || opcode == MMO_GAME_OP_DIGEST_BATCH
        || opcode == MMO_GAME_OP_TRANSFER_BEGIN
        || opcode == MMO_GAME_OP_TRANSFER_APPEND
        || opcode == MMO_GAME_OP_STREAM_CHUNK
        || opcode == MMO_GAME_OP_IMAGE_CHUNK) {
        if (seat_sync(c, opcode, n)) {
            openmmo_event *e = evq_push(c, OPENMMO_EV_SYNC);
            if (e)
                sync_fill_event(&c->sync, e);
        }
        return;
    }
    if (opcode == MMO_GAME_OP_RENTAL_SET
        || opcode == MMO_GAME_OP_TOURNEY_PAGE
        || opcode == MMO_GAME_OP_TOURNEY_COUNT
        || opcode == MMO_GAME_OP_TOURNEY_MATCHUPS
        || opcode == MMO_GAME_OP_SCORE_BOARD) {
        if (seat_compete(c, opcode, n)) {
            openmmo_event *e = evq_push(c, OPENMMO_EV_COMPETE);
            if (e)
                compete_fill_event(&c->compete, e);
        }
        return;
    }
    if (opcode == MMO_GAME_OP_GM_LOOKUP
        || opcode == MMO_GAME_OP_GM_PANEL
        || opcode == MMO_GAME_OP_GM_PANEL_ENTRY) {
        if (seat_gm(c, opcode, n)) {
            openmmo_event *e = evq_push(c, OPENMMO_EV_GM);
            if (e)
                gm_fill_event(&c->gm, e);
        }
        return;
    }

    if (opcode == MMO_GAME_OP_LINK_LEADER) {
        if (seat_link_leader(c, n)) {
            openmmo_event *e = evq_push(c, OPENMMO_EV_LINK);
            if (e)
                link_fill_event(c, e);
        }
        return;
    }

    /* The server sent a scripted movement sequence. The host plays
     * it on the named map object; a byte this client does not map
     * is skipped, not guessed. */
    if (opcode == MMO_GAME_OP_SCRIPT_MOVE) {
        if (seat_script_move(c, n)) {
            openmmo_event *e = evq_push(c, OPENMMO_EV_SCRIPT_MOVE);
            if (e == NULL) {
                /* The host takes a queued sequence when it drains the event.
                 * With no event there is nothing to take it, so give the slot
                 * back rather than leak it, eight of those would wedge every
                 * later cutscene. */
                if (c->script_move_live > 0)
                    c->script_move_live--;
            }
            if (e) {
                e->script_move.entity_id = c->script_move.entity_id;
                e->script_move.flag = c->script_move.flag;
                e->script_move.count = c->script_move.count;
                e->script_move.is_self = c->script_move.is_self;
                e->script_move.slot = c->script_move.slot;
                e->script_move.mapped = c->script_move.mapped;
            }
        }
        return;
    }

    /* The server set or released the script lock. Movement sent while
     * this is on is a leak the server would snap back. */
    if (opcode == MMO_GAME_OP_DIALOG_STATE) {
        int on = 0;
        if (mmo_game_read_dialog_state(c->gbuf, n, &on) == 0) {
            c->dialog.valid = 1;
            c->dialog.locked = on;
            openmmo_event *e = evq_push(c, OPENMMO_EV_DIALOG);
            if (e) {
                e->dialog.open = c->dialog.open;
                e->dialog.close = c->dialog.close;
                e->dialog.in_dialog = openmmo_client_in_dialog(c);
                e->dialog.action_type = c->dialog.action_type;
                e->dialog.text_id = c->dialog.text_id;
                e->dialog.resolved = c->dialog.resolved;
                e->dialog.bank = c->dialog.bank;
                e->dialog.entry = c->dialog.entry;
                e->dialog.choice_count = c->dialog.choice_count;
            }
        }
        return;
    }

    /* The server set the cash balance. The number is absolute, not a delta
     * to add: shop settlement and a blackout both send the balance after
     * the change. The join's LocalPlayerState already seated it; this is
     * a live replacement. */
    if (opcode == MMO_GAME_OP_LOCAL_CHAR_DELTA) {
        mmo_local_character_delta d;
        if (mmo_game_read_local_character_delta(c->gbuf, n, &d) == 0 && d.has_money) {
            c->ws.money = d.money;
            openmmo_event *e = evq_push(c, OPENMMO_EV_MONEY);
            if (e)
                e->money.money = d.money;
        }
        return;
    }

    /* The server answered a pairing. Which egg two parents make is entirely the
     * server's arithmetic, this holds the answer and tells the host to draw it. */
    if (opcode == MMO_GAME_OP_BREEDING_FORECAST) {
        if (seat_forecast(c, n)) {
            openmmo_event *e = evq_push(c, OPENMMO_EV_BREEDING_FORECAST);
            if (e) {
                e->breeding.has_preview = c->forecast.has_preview;
                e->breeding.species = c->forecast.species;
                e->breeding.gender_selectable = c->forecast.gender_selectable;
            }
        }
        return;
    }

    /* The incubator slots and what is left of each one's wear. */
    if (opcode == MMO_GAME_OP_EGG_INCUBATORS) {
        if (seat_incubators(c, n)) {
            openmmo_event *e = evq_push(c, OPENMMO_EV_EGG_INCUBATORS);
            if (e) { e->incubators.count = c->incubators.count;
                     e->incubators.total = c->incubators.total; }
        }
        return;
    }

    /* A party monster gained a move. A slot means the server already put it
     * there; no slot means the moveset was full and it is waiting for an answer,
     * which is the one case the client has to hold on to. */
    /* Another player has offered a battle. Nothing starts here: the server is
     * holding the offer until this client answers it. */
    if (opcode == MMO_GAME_OP_DUEL_INVITE) {
        mmo_duel_invite inv;
        if (mmo_game_read_duel_invite(c->gbuf, n, &inv) == 0) {
            c->duel = inv;
            c->have_duel = 1;
            openmmo_event *e = evq_push(c, OPENMMO_EV_DUEL_INVITE);
            if (e) {
                e->duel.outcome = -1;
                e->duel.flags = inv.flags;
                e->duel.request_type = inv.request_type;
                snprintf(e->duel.name, sizeof e->duel.name, "%s", inv.name);
            }
        }
        return;
    }

    /* The answer to a challenge this client made. */
    if (opcode == MMO_GAME_OP_DUEL_OUTCOME) {
        int packed = 0;
        if (mmo_game_read_duel_outcome(c->gbuf, n, &packed) == 0) {
            openmmo_event *e = evq_push(c, OPENMMO_EV_DUEL_OUTCOME);
            if (e) {
                e->duel.outcome = packed;
                e->duel.flags = 0;
                e->duel.request_type = 0;
                e->duel.name[0] = '\0';
            }
        }
        return;
    }

    /* The peer's offered record (s2c 0x52). A new pick unmakes both
     * confirmations, mirroring the server's own rule: what was agreed to is a
     * pair of monsters, and either one changing unmakes the agreement. */
    if (opcode == MMO_GAME_OP_TRADE_ENTRY) {
        mmo_monster mon;
        if (mmo_game_read_monster(c->gbuf, n, &mon) == 0) {
            seat_mon(&c->trade.peer_mon, &mon);
            c->trade.have_peer_mon = 1;
            c->trade.peer_confirmed = 0;
            c->trade.self_confirmed = 0;
            openmmo_event *e = evq_push(c, OPENMMO_EV_TRADE);
            if (e) {
                e->trade.state = 0;
                e->trade.entry = 1;
                snprintf(e->trade.peer, sizeof e->trade.peer, "%s",
                         c->trade.peer);
            }
        }
        return;
    }

    /* Where the trade table stands (s2c 0x6A, ours). An OPEN reseats the
     * whole store; DONE and CANCELLED close it, and the party resend that
     * follows a DONE is the truth of what was traded. */
    if (opcode == MMO_GAME_OP_TRADE_STATE) {
        mmo_trade_state st;
        if (mmo_game_read_trade_state(c->gbuf, n, &st) == 0) {
            if (st.state == MMO_TRADE_STATE_OPEN) {
                memset(&c->trade, 0, sizeof c->trade);
                c->trade.open = 1;
                c->trade.self_slot = -1;
                c->trade.role = st.role;
                c->trade.peer_gender = st.peer_gender;
                snprintf(c->trade.peer, sizeof c->trade.peer, "%s", st.peer);
                /* A fresh table starts with an empty relay queue: nothing
                 * from a previous scene may leak into this one. */
                c->trade_comm_head = 0;
                c->trade_comm_count = 0;
                c->trade_comm_dropped = 0;
            } else if (st.state == MMO_TRADE_STATE_PEER_OK) {
                c->trade.peer_confirmed = 1;
            } else if (st.state == MMO_TRADE_STATE_DONE) {
                /* The table survives a settlement, the official client's scene walks
                 * straight back to it, so only the agreement resets. */
                c->trade.self_slot = -1;
                c->trade.self_confirmed = 0;
                c->trade.peer_confirmed = 0;
                c->trade.have_peer_mon = 0;
            } else if (st.state == MMO_TRADE_STATE_CANCELLED) {
                c->trade.open = 0;
            }
            c->trade.last_state = st.state;
            /* A trade offer answered or overtaken is no longer pending. */
            if (c->have_duel && c->duel.request_type == 1 &&
                st.state != MMO_TRADE_STATE_PEER_OK)
                c->have_duel = 0;
            openmmo_event *e = evq_push(c, OPENMMO_EV_TRADE);
            if (e) {
                e->trade.state = st.state;
                e->trade.entry = 0;
                snprintf(e->trade.peer, sizeof e->trade.peer, "%s",
                         st.peer[0] != '\0' ? st.peer : c->trade.peer);
            }
        }
        return;
    }

    /* One relayed message of the engine's trade scene (bidi 0xBF). Queued,
     * not evented: the scene drains it on its own frame, the way a link
     * battle's blobs are drained. */
    if (opcode == MMO_GAME_OP_TRADE_COMM) {
        mmo_trade_comm msg;
        if (mmo_game_read_trade_comm(c->gbuf, n, &msg) == 0) {
            if (c->trade_comm_count >= OPENMMO_TRADE_COMM_QUEUE) {
                c->trade_comm_dropped++;
            } else {
                int at = (c->trade_comm_head + c->trade_comm_count) %
                         OPENMMO_TRADE_COMM_QUEUE;
                c->trade_comm[at] = msg;
                c->trade_comm_count++;
            }
        }
        return;
    }

    /* The trade link session opened (s2c 0xDC). The flags are official category
     * switches with nothing here to switch; the arrival is the meaning. */
    if (opcode == MMO_GAME_OP_GTL_FLAGS) {
        mmo_gtl_flags flags;
        if (mmo_game_read_gtl_flags(c->gbuf, n, &flags) == 0) {
            c->gtl.session_open = 1;
            c->gtl.session_ts = flags.timestamp_ms;
            openmmo_event *e = evq_push(c, OPENMMO_EV_GTL);
            if (e) {
                e->gtl.opcode = opcode;
                e->gtl.rows = c->gtl.count;
                e->gtl.total = c->gtl.total;
            }
        }
        return;
    }

    /* One page of the shelf (s2c 0x9B). The rows replace the held page
     * whole; a monster row is seated the way a party record is, so what the
     * board shows is what a trade would deliver. */
    if (opcode == MMO_GAME_OP_GTL_SEARCH_PAGE) {
        mmo_gtl_page page;
        if (mmo_game_read_gtl_page(c->gbuf, n, &page) == 0) {
            c->gtl.request_id = page.request_id;
            c->gtl.kind = page.kind;
            c->gtl.page = page.page;
            c->gtl.total = page.total;
            c->gtl.count = page.count;
            for (int i = 0; i < page.count; i++) {
                const mmo_gtl_row *src = &page.rows[i];
                openmmo_gtl_row *dst = &c->gtl.row[i];
                memset(dst, 0, sizeof *dst);
                dst->listing_id = src->listing_id;
                dst->kind = src->kind;
                dst->price = src->price;
                dst->listed_at = src->listed_at;
                dst->expires_at = src->expires_at;
                dst->quantity = src->quantity;
                dst->item_id = src->item_id;
                dst->have_mon = src->have_mon;
                if (src->have_mon) {
                    seat_mon(&dst->mon, &src->mon);
                    for (int s = 0; s < MMO_MON_STATS; s++)
                        dst->stats[s] = src->stats[s];
                }
                dst->own_state = src->own_state;
                dst->own_remaining = src->own_remaining;
                dst->own_unclaimed = src->own_unclaimed;
            }
            c->gtl.quote_count = page.quote_count;
            for (int i = 0; i < page.quote_count; i++) {
                c->gtl.quote[i].item_id = page.quotes[i].item_id;
                c->gtl.quote[i].price = page.quotes[i].price;
            }
            openmmo_event *e = evq_push(c, OPENMMO_EV_GTL);
            if (e) {
                e->gtl.opcode = opcode;
                e->gtl.rows = page.count;
                e->gtl.total = page.total;
            }
        }
        return;
    }

    /* The trade log came back (s2c 0x5E): this character's fills, newest
     * first, held whole for the window. */
    if (opcode == MMO_GAME_OP_GTL_LOG) {
        mmo_gtl_log log;
        if (mmo_game_read_gtl_log(c->gbuf, n, &log) == 0) {
            c->gtl.log = log;
            c->gtl.log_rows = log.count;
            openmmo_event *e = evq_push(c, OPENMMO_EV_GTL);
            if (e) {
                e->gtl.opcode = opcode;
                e->gtl.rows = log.count;
                e->gtl.total = c->gtl.total;
            }
        }
        return;
    }

    /* One shelf verb's answer (s2c 0xAF, ours). The store keeps the last
     * one and bumps a sequence so the window can toast each exactly once. */
    if (opcode == MMO_GAME_OP_GTL_RESULT) {
        mmo_gtl_result result;
        if (mmo_game_read_gtl_result(c->gbuf, n, &result) == 0) {
            c->gtl.result = result;
            c->gtl.result_seq++;
            openmmo_event *e = evq_push(c, OPENMMO_EV_GTL);
            if (e) {
                e->gtl.opcode = opcode;
                e->gtl.rows = result.code;
                e->gtl.total = result.b;
            }
        }
        return;
    }

    /* The server seated a native link battle. The party on the wire is the
     * Opponent's; this client's own is the one it already holds. */
    if (opcode == MMO_GAME_OP_LINK_BATTLE_OPEN) {
        mmo_link_battle_open open;
        mmo_monster mons[OPENMMO_PARTY_MAX];

        if (mmo_game_read_link_battle_open(c->gbuf, n, &open, mons,
                                           OPENMMO_PARTY_MAX) == 0) {
            memset(&c->link_battle, 0, sizeof c->link_battle);
            c->link_battle.valid = 1;
            c->link_battle.battle_id = open.battle_id;
            c->link_battle.net_id = open.net_id;
            snprintf(c->link_battle.peer_name, sizeof c->link_battle.peer_name,
                     "%s", open.peer_name);
            c->link_battle.peer_gender = open.peer_gender ? 1 : 0;
            c->link_battle.peer_body_gfx =
                mmo_appearance_resolve(c->link_battle.peer_gender,
                                       open.has_appearance ? &open.appearance : NULL);
            c->link_battle.party.valid = 1;
            c->link_battle.party.total = open.total;
            for (int i = 0; i < open.count; i++)
                seat_mon(&c->link_battle.party.mon[i], &mons[i]);
            c->link_battle.party.count = open.count;
            party_count_unmapped(&c->link_battle.party);
            /* A seat is a fresh fight: whatever the last one left queued is not
             * part of it. */
            c->link_head = 0;
            c->link_count = 0;
            c->link_dropped = 0;
            c->have_duel = 0;
            openmmo_event *e = evq_push(c, OPENMMO_EV_LINK_BATTLE);
            if (e) {
                e->link_battle.battle_id = open.battle_id;
                e->link_battle.net_id = open.net_id;
                e->link_battle.count = open.count;
                snprintf(e->link_battle.peer_name, sizeof e->link_battle.peer_name,
                         "%s", open.peer_name);
            }
        }
        return;
    }

    /* One blob of the running link battle. The payload is the engine's own and
     * is not read here; a LEAVE is the one kind this side acts on, because it
     * is the fight ending under the presenter. */
    if (opcode == MMO_GAME_OP_LINK_BATTLE_DATA) {
        mmo_link_battle_data blob;
        if (mmo_game_read_link_battle_data(c->gbuf, n, &blob) == 0
            && c->link_battle.valid
            && blob.battle_id == c->link_battle.battle_id) {
            if (blob.kind == MMO_LINK_KIND_LEAVE) {
                memset(&c->link_battle, 0, sizeof c->link_battle);
                c->link_head = 0;
                c->link_count = 0;
                evq_push(c, OPENMMO_EV_LINK_BATTLE_END);
            } else if (c->link_count < OPENMMO_LINK_QUEUE) {
                int slot = (c->link_head + c->link_count) % OPENMMO_LINK_QUEUE;
                c->link_blob[slot] = blob;
                c->link_count++;
            } else {
                c->link_dropped++;
            }
        }
        return;
    }

    /*
     * The link contest: the queue filling, the seat, and the relay. The DATA and SYNC kinds
     * are the engine's own traffic and are not read here, the mod drains this queue on the
     * contest's frame and hands each to the engine's command table.
     */
    if (opcode == MMO_GAME_OP_CONTEST_DOWN) {
        mmo_contest_comm msg;

        if (mmo_game_read_contest_comm(c->gbuf, n, &msg) != 0)
            return;
        switch (msg.kind) {
        case MMO_CONTEST_KIND_WAITING:
            c->contest.queued = 1;
            c->contest.valid = 0;
            c->contest.queued_have = msg.len > 0 ? msg.data[0] : 0;
            c->contest.queued_want = msg.len > 1 ? msg.data[1] : 0;
            evq_push(c, OPENMMO_EV_CONTEST);
            return;
        case MMO_CONTEST_KIND_CANCEL:
            /* The server will not queue this client, or has dropped it. */
            memset(&c->contest, 0, sizeof c->contest);
            c->contest_head = 0;
            c->contest_count = 0;
            evq_push(c, OPENMMO_EV_CONTEST);
            return;
        case MMO_CONTEST_KIND_SEAT: {
            mmo_contest_seat seat;

            if (mmo_game_read_contest_seat(msg.data, msg.len, &seat) != 0)
                return;
            /* A seat outside the contest it describes is a frame this client
             * cannot act on: every later message is keyed by that number. */
            if (msg.seat < 0 || msg.seat >= seat.humans)
                return;
            memset(&c->contest, 0, sizeof c->contest);
            c->contest.valid = 1;
            c->contest.session_id = msg.session_id;
            c->contest.seat = msg.seat;
            c->contest.humans = seat.humans;
            c->contest.rank = seat.rank;
            c->contest.type = seat.type;
            for (int i = 0; i < seat.humans; i++) {
                snprintf(c->contest.contestant[i].name,
                         sizeof c->contest.contestant[i].name, "%s",
                         seat.seat[i].name);
                c->contest.contestant[i].gender = seat.seat[i].gender ? 1 : 0;
                c->contest.contestant[i].story_cleared =
                    (seat.seat[i].flags & MMO_CONTEST_SEAT_STORY_CLEARED) != 0;
                c->contest.contestant[i].national_dex =
                    (seat.seat[i].flags & MMO_CONTEST_SEAT_NATIONAL_DEX) != 0;
            }
            c->contest_head = 0;
            c->contest_count = 0;
            c->contest_dropped = 0;
            evq_push(c, OPENMMO_EV_CONTEST);
            return;
        }
        default:
            break;
        }
        if (!c->contest.valid || msg.session_id != c->contest.session_id)
            return;
        if (msg.kind == MMO_CONTEST_KIND_LEAVE) {
            /* Remembered rather than dropped: the engine's own answer to a
             * console that went away is CommSys_IsPlayerConnected turning
             * false for it, and every barrier still standing has to stop
             * waiting for that seat. */
            if (msg.seat >= 0 && msg.seat < OPENMMO_CONTEST_SEATS)
                c->contest.contestant[msg.seat].gone = 1;
            evq_push(c, OPENMMO_EV_CONTEST);
        }
        if (c->contest_count < OPENMMO_CONTEST_QUEUE) {
            int slot = (c->contest_head + c->contest_count)
                       % OPENMMO_CONTEST_QUEUE;
            c->contest_q[slot] = msg;
            c->contest_count++;
        } else {
            c->contest_dropped++;
        }
        return;
    }

    /* One message of an underground conversation. Everything about the two
     * menus is the engine's, so nothing here is read: the mod drains this queue
     * on the cavern's own frame and hands each one to the engine's command
     * table (or, for a RESULT, to its own TalkEvent). */
    if (opcode == MMO_GAME_OP_UNDERGROUND_TALK) {
        mmo_underground_talk msg;

        if (mmo_game_read_underground_talk(c->gbuf, n, &msg) == 0) {
            if (c->ug_talk_count < OPENMMO_UG_TALK_QUEUE) {
                int slot = (c->ug_talk_head + c->ug_talk_count)
                           % OPENMMO_UG_TALK_QUEUE;
                c->ug_talk[slot] = msg;
                c->ug_talk_count++;
            } else {
                c->ug_talk_dropped++;
            }
        }
        return;
    }

    if (opcode == MMO_GAME_OP_MOVE_LEARN_PROMPT) {
        mmo_move_learn ml;
        if (mmo_game_read_move_learn(c->gbuf, n, &ml) == 0) {
            openmmo_event *e = evq_push(c, OPENMMO_EV_MOVE_LEARN);
            openmmo_event ev;
            memset(&ev, 0, sizeof ev);
            ev.kind = OPENMMO_EV_MOVE_LEARN;
            ev.move_learn.monster_id = ml.monster_id;
            ev.move_learn.slot = ml.slot;
            ev.move_learn.move_id = ml.move_id;
            ev.move_learn.engine_move_id = -1;
            if (ml.move_id > 0) {
                const char *why = NULL;
                u16 ds = mmo_id_move_from_server((u16)ml.move_id, &why);
                if (ds != MMO_ID_NONE)
                    ev.move_learn.engine_move_id = (int)ds;
            }
            ev.move_learn.party_index = -1;
            for (int i = 0; i < c->party.count; i++) {
                if (c->party.mon[i].id == ml.monster_id) {
                    ev.move_learn.party_index = i;
                    break;
                }
            }
            if (ml.slot == MMO_MOVE_LEARN_NO_SLOT) {
                c->move_learn = ev;
                c->have_move_learn = 1;
            }
            if (e)
                *e = ev;
        }
        return;
    }

    /* The server has decided a monster evolves. Nothing about the species is
     * decided here and nothing is applied to the party: what the client holds
     * keeps the species it had until the container comes back. The prompt is held
     * because the server is waiting for an answer. */
    if (opcode == MMO_GAME_OP_EVOLUTION_PROMPT) {
        mmo_evolution ev0;
        if (mmo_game_read_evolution(c->gbuf, n, &ev0) == 0) {
            openmmo_event *e = evq_push(c, OPENMMO_EV_EVOLUTION);
            openmmo_event ev;
            memset(&ev, 0, sizeof ev);
            ev.kind = OPENMMO_EV_EVOLUTION;
            ev.evolution.monster_id = ev0.monster_id;
            ev.evolution.species = ev0.species;
            ev.evolution.cancelable = ev0.cancelable;
            if (ev0.species > 0) {
                const char *why = NULL;
                u16 ds = mmo_id_species_from_server((u16)ev0.species, &why);
                if (ds != MMO_ID_NONE)
                    ev.evolution.engine_species = ds;
            }
            /* The game client resolves the id against every container, so the PC
             * is searched too rather than assuming a party monster. */
            ev.evolution.party_index = -1;
            ev.evolution.storage_index = -1;
            for (int i = 0; i < c->party.count; i++) {
                if (c->party.mon[i].id == ev0.monster_id) {
                    ev.evolution.party_index = i;
                    ev.evolution.from_species = c->party.mon[i].dex_id;
                    break;
                }
            }
            if (ev.evolution.party_index < 0 && c->storage.mon) {
                for (int i = 0; i < c->storage.count; i++) {
                    if (c->storage.mon[i].id == ev0.monster_id) {
                        ev.evolution.storage_index = i;
                        ev.evolution.from_species = c->storage.mon[i].dex_id;
                        break;
                    }
                }
            }
            c->evolution = ev;
            c->have_evolution = 1;
            if (e)
                *e = ev;
        }
        return;
    }

    /* The server re-seated the local script VM (0xCB). It sends this once per
     * join, ahead of the map, so the scene the engine builds is the one this
     * character's progress earned. A host writes the store into the engine's
     * own VarsFlags block on the event. */
    if (opcode == MMO_GAME_OP_SCRIPT_STATE) {
        seat_script_state(c, n);
        if (c->script_state.seated)
            (void)evq_push(c, OPENMMO_EV_SCRIPT_STATE);
        return;
    }

    /* The one answer to a save this client offered (0xCE). Recorded and not
     * acted on: what happens next belongs to whoever asked, the CLI prints
     * it, the game prints it beside the row the player pressed. */
    if (opcode == MMO_GAME_OP_OFFLINE_RESULT) {
        seat_import_answer(c, n);
        return;
    }

    /* A story flag the server set or cleared under the player (a script advancing
     * progression). Progression is server-owned: update the store to match, then
     * surface the change so a host can re-evaluate story-conditional content. */
    if (opcode == MMO_GAME_OP_STORY_FLAG) {
        int region = 0, flag_id = 0, enabled = 0;
        if (mmo_game_read_story_flag(c->gbuf, n, &region, &flag_id, &enabled) == 0) {
            story_set_flag(&c->story, flag_id, enabled);
            c->ws.flag_count = c->story.set_flag_count;
            openmmo_event *e = evq_push(c, OPENMMO_EV_STORY_FLAG);
            if (e) { e->story.region = region; e->story.flag_id = flag_id;
                     e->story.enabled = enabled; }
        }
        return;
    }

    apply_presence(c, opcode, n);
}

/* A ChatMessage the server delivered, a player line, a join notice, a
 * command reply. Seat it in the ring and surface it; a window draws from
 * the event and the ring. */
static void apply_chat(openmmo_client *c, u8 opcode, size_t n)
{
    mmo_chat ch;
    openmmo_event *e;
    int slot;

    if (opcode != MMO_GAME_OP_CHAT)
        return;
    if (mmo_game_read_chat(c->gbuf, n, &ch) != 0)
        return;

    if (c->chat_count < OPENMMO_CHAT_LOG) {
        slot = (c->chat_head + c->chat_count) % OPENMMO_CHAT_LOG;
        c->chat_count++;
    } else {
        slot = c->chat_head;
        c->chat_head = (c->chat_head + 1) % OPENMMO_CHAT_LOG;
    }
    e = &c->chat_log[slot];
    memset(e, 0, sizeof *e);
    e->kind = OPENMMO_EV_CHAT;
    e->chat.type = ch.type;
    e->chat.language = ch.language;
    e->chat.sender_id = ch.sender_id;
    snprintf(e->chat.sender, sizeof e->chat.sender, "%s", ch.sender);
    snprintf(e->chat.text, sizeof e->chat.text, "%s", ch.text);

    e = evq_push(c, OPENMMO_EV_CHAT);
    if (e)
        e->chat = c->chat_log[slot].chat;
}

/* Advance the reconcile model one frame and drain its render events into the
 * queue, one client event per model event. Called once per pump while in-game, so
 * the model's frame cadence tracks the host's frame rate. */
static void tick_entities(openmmo_client *c)
{
    openmmo_entity_event evs[OPENMMO_ENTITY_NETID_CEIL];
    int m = openmmo_entity_tick(&c->ent, evs, OPENMMO_ENTITY_NETID_CEIL);
    for (int i = 0; i < m; i++) {
        openmmo_entity_event *me = &evs[i];
        openmmo_event_kind k;
        switch (me->kind) {
        case OPENMMO_ENTITY_EV_SPAWN:   k = OPENMMO_EV_ENTITY_SPAWN;   break;
        case OPENMMO_ENTITY_EV_STEP:    k = OPENMMO_EV_ENTITY_STEP;    break;
        case OPENMMO_ENTITY_EV_TURN:    k = OPENMMO_EV_ENTITY_TURN;    break;
        case OPENMMO_ENTITY_EV_DESPAWN: k = OPENMMO_EV_ENTITY_DESPAWN; break;
        case OPENMMO_ENTITY_EV_PLACE:   k = OPENMMO_EV_ENTITY_PLACE;   break;
        default: continue;
        }
        openmmo_event *e = evq_push(c, k);
        if (!e)
            break; /* queue full: the host is not draining; drop rather than block */
        e->entity.slot = me->slot;
        e->entity.localid = me->localid;
        e->entity.x = me->x;
        e->entity.z = me->z;
        e->entity.dir = me->dir;
        e->entity.speed = me->speed;
        e->entity.gender = me->appearance.gender;
        e->entity.version = me->appearance.version;
        e->entity.has_body = me->appearance.has_body;
        e->entity.has_follower = me->appearance.has_follower;
        e->entity.follower_gfx = me->appearance.follower_gfx;
        e->entity.gfx = me->appearance.gfx;
        memcpy(e->entity.name, me->appearance.name, sizeof e->entity.name);
    }
}

/* Dispatch one whole inbound frame to the handler for the current phase. */
static void dispatch_frame(openmmo_client *c, const u8 *body, size_t blen)
{
    switch (c->ph) {
    case P_LOGIN_HELLO:
        if (on_server_hello(c, body, blen) == 0)
            send_login_request(c);
        break;
    case P_LOGIN_AUTH:    handle_login_response(c, body, blen); break;
    case P_HANDOFF_LIST:  handle_gs_list(c, body, blen); break;
    case P_HANDOFF_NODES: handle_gs_nodes(c, body, blen); break;
    case P_GAME_HELLO:
        if (on_server_hello(c, body, blen) == 0)
            send_join_packet(c);
        break;
    case P_GAME_JOIN:     handle_join_response(c, body, blen); break;
    case P_GAME_CHARLIST: handle_characters_list(c, body, blen); break;
    case P_GAME_CREATE:   handle_characters_list(c, body, blen); break;
    case P_GAME_DELETE:   handle_delete_result(c, body, blen); break;
    case P_GAME_SELECT:   handle_select_window(c, body, blen); break;
    case P_GAME_ENTER:    handle_enter_window(c, body, blen); break;
    case P_IN_GAME:       handle_in_game(c, body, blen); break;
    default:
        /* A frame arriving while held with no consumer yet: ignore it. */
        break;
    }
}

/* --- lifecycle --------------------------------------------------------- */

openmmo_client *openmmo_client_new(void)
{
    openmmo_client *c = calloc(1, sizeof *c);
    if (!c)
        return NULL;
    c->gbuf_cap = OPENMMO_GBUF_CAP;
    c->gbuf = malloc(c->gbuf_cap);
    if (!c->gbuf) {
        free(c);
        return NULL;
    }
    mmo_net_init(&c->net);
    c->ph = P_IDLE;
    c->login_refusal = -1;
    c->status = OPENMMO_DISCONNECTED;
    /* Zero is a real status (landed), so the "no answer yet" one is set here
     * rather than left to the calloc. */
    c->import_answer.status = MMO_IMPORT_STATUS_NONE;
    return c;
}

void openmmo_client_free(openmmo_client *c)
{
    if (!c)
        return;
    mmo_net_close(&c->net);
    mmo_net_read_reset(&c->acc);
    free(c->storage.mon);
    free(c->daycare.mon);
    free(c->ct_scratch);
    free(c->sync_xfer);
    free(c->gbuf);
    free(c);
}

/* Copy the caller's config into the client's own storage. */
static void adopt_config(openmmo_client *c, const openmmo_config *cfg)
{
    snprintf(c->user, sizeof c->user, "%s", cfg->user ? cfg->user : "");
    snprintf(c->pass, sizeof c->pass, "%s", cfg->pass ? cfg->pass : "");
    /* Not the caller's to choose: this client has one server. */
    c->login_port = openmmo_endpoint_login_port();
    c->game_port = openmmo_endpoint_game_port();
    c->mode = cfg->mode;
    c->timeout_s = cfg->phase_timeout_s > 0 ? cfg->phase_timeout_s : 10;
    c->allow_undrawable_region = cfg->allow_undrawable_region;
    c->local_scripts = cfg->local_scripts;
    snprintf(c->create_name, sizeof c->create_name,
             "%s", cfg->create_name ? cfg->create_name : "");
    c->create_name_overflow =
        cfg->create_name &&
        mmo_utf16_units(cfg->create_name) > MMO_CHAR_NAME_MAX;
    c->create_gender = cfg->create_gender;
    c->create_region = cfg->create_region;
    c->create_skin_region = cfg->create_skin_region;
    c->create_skin_mask = cfg->create_skin_mask;
    memcpy(c->create_skin_word, cfg->create_skin_word, sizeof c->create_skin_word);
    c->char_count_before = -1;
    /* Nothing measured yet. Zero would read as a perfect link. */
    c->ping_rtt_ms = -1;
    c->ping_inflight = 0;
    c->ping_next_ms = 0;
    snprintf(c->select_name, sizeof c->select_name,
             "%s", cfg->select_name ? cfg->select_name : "");
    c->select_index = cfg->select_index;
    c->select_region = cfg->select_region;
    c->select_how = cfg->select_how;
    memset(&c->chars, 0, sizeof c->chars);
    c->have_chars = 0;
    c->create_error[0] = '\0';
    c->delete_error[0] = '\0';
    c->login_refusal = -1;
    c->self_mount = MMO_TRANSPORT_NONE;
    c->evq_head = c->evq_tail = 0;
    c->chat_head = c->chat_count = 0;
    /* The library caps at the engine's 8-slot remote band; the fused build's glue
     * culls further against the current map's texture headroom (2.5.9). */
    openmmo_entity_mgr_init(&c->ent, OPENMMO_ENTITY_NETID_CEIL);
    c->have_self = 0;
    c->self_entity_id = 0;
    c->have_self_tile = 0;
    c->self_x = c->self_z = 0;
    c->self_dir = 0;
    c->warping = 0;
    memset(&c->ws, 0, sizeof c->ws);
    memset(&c->story, 0, sizeof c->story);
    memset(&c->party, 0, sizeof c->party);
    memset(&c->bag, 0, sizeof c->bag);
    memset(&c->shop, 0, sizeof c->shop);
    memset(&c->dialog, 0, sizeof c->dialog);
    memset(&c->script_move, 0, sizeof c->script_move);
    memset(c->script_move_q, 0, sizeof c->script_move_q);
    c->script_move_head = 0;
    c->script_move_live = 0;
    memset(&c->npcs, 0, sizeof c->npcs);
    memset(&c->objectives, 0, sizeof c->objectives);
    memset(&c->friends, 0, sizeof c->friends);
    memset(&c->guild, 0, sizeof c->guild);
    memset(&c->mail, 0, sizeof c->mail);
    c->mail.result = -1;
    memset(&c->ui, 0, sizeof c->ui);
    /* A restart drops a half-open transfer with it: the concat is this
     * session's, and appending the next one onto it would report the
     * previous session's bytes as this one's. */
    sync_free_xfer(c);
    memset(&c->sync, 0, sizeof c->sync);
    memset(&c->compete, 0, sizeof c->compete);
    memset(&c->move_learn, 0, sizeof c->move_learn);
    c->have_move_learn = 0;
    memset(&c->evolution, 0, sizeof c->evolution);
    c->have_evolution = 0;
    memset(&c->duel, 0, sizeof c->duel);
    c->have_duel = 0;
    memset(&c->trade, 0, sizeof c->trade);
    c->trade.self_slot = -1;
    c->trade_comm_head = 0;
    c->trade_comm_count = 0;
    c->trade_comm_dropped = 0;
    memset(&c->gtl, 0, sizeof c->gtl);
    c->gtl.log_rows = -1;
    memset(&c->link_battle, 0, sizeof c->link_battle);
    c->link_head = 0;
    c->link_count = 0;
    c->link_dropped = 0;
    c->ug_talk_head = 0;
    c->ug_talk_count = 0;
    c->ug_talk_dropped = 0;
    memset(&c->incubators, 0, sizeof c->incubators);
    memset(&c->forecast, 0, sizeof c->forecast);
    c->hatched_count = 0;
    /* Keep the PC's allocation across a restart and drop only what it held: the
     * next session refills it from the world-state block. */
    openmmo_party_mon *held = c->storage.mon;
    memset(&c->storage, 0, sizeof c->storage);
    c->storage.mon = held;
    openmmo_party_mon *boarders = c->daycare.mon;
    memset(&c->daycare, 0, sizeof c->daycare);
    c->daycare.mon = boarders;
}

int openmmo_client_start(openmmo_client *c, const openmmo_config *cfg)
{
    if (!c || !cfg)
        return -1;
    /*
     * A start is also a restart: the same client object is dialled again after a link breaks.
     */
    mmo_net_close(&c->net);
    adopt_config(c, cfg);
    mmo_net_read_reset(&c->acc);

    {
        char host[OPENMMO_ENDPOINT_HOST_MAX];
        int rc;

        openmmo_endpoint_host(host, sizeof host);
        rc = mmo_net_connect(&c->net, host, c->login_port);
        openmmo_endpoint_forget(host, sizeof host);
        if (rc != 0) {
            fail(c, c->net.errmsg[0] ? c->net.errmsg : "connect failed");
            return -1;
        }
    }
    c->ph = P_LOGIN_CONNECT;
    set_status(c, OPENMMO_CONNECTING);
    arm_deadline(c);
    return 0;
}

int openmmo_client_attach_fd(openmmo_client *c, int fd, const openmmo_config *cfg)
{
    if (!c || !cfg || c->ph != P_IDLE)
        return -1;
    adopt_config(c, cfg);
    mmo_net_read_reset(&c->acc);
    mmo_net_attach(&c->net, fd);
    begin_handshake(c, P_LOGIN_HELLO, OPENMMO_HANDSHAKING);
    return 0;
}

int openmmo_client_login_refusal(const openmmo_client *c)
{
    return c ? c->login_refusal : -1;
}

void openmmo_client_pin_hello_time(openmmo_client *c, s64 timestamp)
{
    if (c)
        c->pinned_hello_time = timestamp;
}

int openmmo_client_flush(openmmo_client *c, int ms)
{
    size_t left;

    if (!c)
        return 0;
    left = mmo_net_drain(&c->net, ms);
    if (left == 0)
        return 0;
    fprintf(stderr, "openmmo: %u byte(s) never left for the server before the"
                    " disconnect\n", (unsigned)left);
    return -1;
}

void openmmo_client_disconnect(openmmo_client *c)
{
    if (!c)
        return;
    mmo_net_close(&c->net);
    mmo_net_read_reset(&c->acc);
    c->ph = P_DEAD;
    set_status(c, OPENMMO_DISCONNECTED);
}

int openmmo_client_send_move(openmmo_client *c, int from_x, int from_z,
                             int dir, int running)
{
    return openmmo_client_send_move_tiles(c, from_x, from_z, dir, running, 1);
}

int openmmo_client_send_move_tiles(openmmo_client *c, int from_x, int from_z,
                                   int dir, int running, int tiles)
{
    if (!c || c->ph != P_IN_GAME)
        return -1;
    if (openmmo_client_in_dialog(c))
        return -1;
    if (tiles < 1)
        tiles = 1;
    else if (tiles > 3)
        tiles = 3;

    mmo_wbuf body;
    mmo_wbuf_init(&body);
    mmo_game_write_movement(&body, (s16)from_x, (s16)from_z,
                            mmo_game_dir_from_ds(dir), running, tiles);
    int rc = body.err
                 ? (fail(c, "move: could not frame MovementPacket"), -1)
                 : send_game_packet(c, MMO_GAME_OP_MOVEMENT, body.data, body.len,
                                    "move: could not send MovementPacket");
    mmo_wbuf_free(&body);

    /* The server never echoes an accepted step, so the from-tile has to move
     * here or the next send is a desync. `tiles` tiles in `dir`, the caller
     * moved that far, so this has to. An off-edge step then predicts the
     * crossing the server will run (it sends no LoadMap). */
    if (rc == 0) {
        int nx = from_x, nz = from_z;

        switch (dir) {
        case 0: nz -= tiles; break; /* NORTH */
        case 1: nz += tiles; break; /* SOUTH */
        case 2: nx -= tiles; break; /* WEST */
        case 3: nx += tiles; break; /* EAST */
        default: break;
        }
        c->self_x = nx;
        c->self_z = nz;
        c->self_dir = dir;
        c->have_self_tile = 1;
        try_edge_crossing(c, from_x, from_z, dir);
    }
    return rc;
}

int openmmo_client_send_face(openmmo_client *c, int dir)
{
    if (!c || c->ph != P_IN_GAME)
        return -1;
    if (openmmo_client_in_dialog(c))
        return -1;

    mmo_wbuf body;
    mmo_wbuf_init(&body);
    mmo_game_write_face(&body, mmo_game_dir_from_ds(dir));
    int rc = body.err
                 ? (fail(c, "face: could not frame FaceDirectionPacket"), -1)
                 : send_game_packet(c, MMO_GAME_OP_FACE, body.data, body.len,
                                    "face: could not send FaceDirectionPacket");
    mmo_wbuf_free(&body);
    if (rc == 0)
        c->self_dir = dir;
    return rc;
}

/* Report what a local script wrote. */
int openmmo_client_send_script_state(openmmo_client *c,
                                     const mmo_script_flag *flags, int nflags,
                                     const mmo_script_var *vars, int nvars,
                                     const mmo_save_block *blocks, int nblocks)
{
    if (!c || c->ph != P_IN_GAME)
        return -1;
    if (nflags <= 0 && nvars <= 0 && nblocks <= 0)
        return 0;

    mmo_wbuf body;
    mmo_wbuf_init(&body);
    int rc;
    if (mmo_game_write_script_state(&body, flags, nflags, vars, nvars,
                                    blocks, nblocks) != 0)
        rc = (fail(c, "script: could not frame ScriptState"), -1);
    else
        rc = send_game_packet(c, MMO_GAME_OP_SCRIPT_STATE, body.data, body.len,
                              "script: could not send ScriptState");
    mmo_wbuf_free(&body);
    return rc;
}

/* Offer a whole save file, in pieces. */
#define OFFLINE_REPORT_CHUNK 4096

/* One blob up this channel, in numbered pieces. Which blob it is is the blob's
 * own business: both formats open with a length-prefixed four-byte magic, so
 * the far end knows what it has joined before it reads a field of either, and
 * neither end needs a second opcode to say so. */
static int send_offline_blob(openmmo_client *c, const u8 *blob, size_t len)
{
    size_t sent = 0;
    int sequence = 0;

    if (!c || c->ph != P_IN_GAME)
        return -1;
    if (blob == NULL || len == 0)
        return -1;

    c->import_answer.status = MMO_IMPORT_STATUS_NONE;
    c->import_answer.message[0] = '\0';
    c->import_answer.nnotes = 0;
    c->import_answer.nnotes_sent = 0;
    c->import_answer.wants_chain = 0;

    while (sent < len) {
        size_t take = len - sent;
        mmo_wbuf body;
        int rc;

        if (take > OFFLINE_REPORT_CHUNK)
            take = OFFLINE_REPORT_CHUNK;
        mmo_wbuf_init(&body);
        if (mmo_game_write_offline_report(&body, sequence, sent + take >= len,
                                          blob + sent, take) != 0)
            rc = (fail(c, "import: could not frame OfflineSaveReport"), -1);
        else
            rc = send_game_packet(c, MMO_GAME_OP_OFFLINE_REPORT, body.data,
                                  body.len,
                                  "import: could not send OfflineSaveReport");
        mmo_wbuf_free(&body);
        if (rc != 0)
            return rc;
        sent += take;
        sequence++;
    }
    return 0;
}

int openmmo_client_send_offline_report(openmmo_client *c,
                                       const u8 *report, size_t len)
{
    return send_offline_blob(c, report, len);
}

int openmmo_client_send_offline_chain(openmmo_client *c,
                                      const u8 *chain, size_t len)
{
    return send_offline_blob(c, chain, len);
}

int openmmo_client_send_offline_export(openmmo_client *c,
                                       const u8 *blob, size_t len)
{
    return send_offline_blob(c, blob, len);
}

const openmmo_import_answer *openmmo_client_import_answer(const openmmo_client *c)
{
    return c ? &c->import_answer : NULL;
}

int openmmo_client_send_script_warp(openmmo_client *c, int header, int x, int z,
                                    int dir)
{
    if (!c || c->ph != P_IN_GAME)
        return -1;

    mmo_wbuf body;
    mmo_wbuf_init(&body);
    mmo_game_write_script_warp(&body, (s32)header, (s16)x, (s16)z,
                               mmo_game_dir_from_ds(dir));
    int rc = body.err
                 ? (fail(c, "script: could not frame ScriptWarpArrived"), -1)
                 : send_game_packet(c, MMO_GAME_OP_SCRIPT_WARP, body.data,
                                    body.len,
                                    "script: could not send ScriptWarpArrived");
    mmo_wbuf_free(&body);
    return rc;
}

int openmmo_client_send_pokemon_release(openmmo_client *c, s64 id)
{
    if (!c || c->ph != P_IN_GAME || id == 0)
        return -1;

    mmo_wbuf body;
    mmo_wbuf_init(&body);
    mmo_game_write_pokemon_release(&body, id);
    int rrc = body.err
                  ? (fail(c, "storage: could not frame PokemonRelease"), -1)
                  : send_game_packet(c, MMO_GAME_OP_POKEMON_RELEASE, body.data,
                                     body.len,
                                     "storage: could not send PokemonRelease");
    mmo_wbuf_free(&body);
    return rrc;
}

int openmmo_client_send_script_grant(openmmo_client *c, int dex, int level,
                                     int hp, int container, int slot,
                                     u32 seed, u32 iv_bits, int shiny,
                                     const uint8_t *nick_utf16le,
                                     size_t nick_bytes)
{
    if (!c || c->ph != P_IN_GAME)
        return -1;

    mmo_wbuf body;
    mmo_wbuf_init(&body);
    mmo_game_write_script_grant(&body, (u16)dex, (u8)level, (s16)hp,
                                (u8)container, (s16)slot, seed, iv_bits,
                                (u8)(shiny ? 1 : 0),
                                nick_utf16le, nick_bytes);
    int rc = body.err
                 ? (fail(c, "script: could not frame ScriptGrantPokemon"), -1)
                 : send_game_packet(c, MMO_GAME_OP_SCRIPT_GRANT, body.data,
                                    body.len,
                                    "script: could not send ScriptGrantPokemon");
    mmo_wbuf_free(&body);
    return rc;
}

int openmmo_client_send_battle_outcome(openmmo_client *c,
                                       const mmo_battle_mon_outcome *mons,
                                       int n)
{
    if (!c || c->ph != P_IN_GAME || mons == NULL || n <= 0)
        return -1;

    mmo_wbuf body;
    mmo_wbuf_init(&body);
    mmo_game_write_battle_outcome(&body, mons, n);
    int rc = body.err
                 ? (fail(c, "battle: could not frame BattleOutcome"), -1)
                 : send_game_packet(c, MMO_GAME_OP_BATTLE_OUTCOME, body.data,
                                    body.len,
                                    "battle: could not send BattleOutcome");
    mmo_wbuf_free(&body);
    return rc;
}

const mmo_duel_invite *openmmo_client_duel_pending(const openmmo_client *c)
{
    if (!c || !c->have_duel)
        return NULL;
    return &c->duel;
}

int openmmo_client_reply_duel(openmmo_client *c, int accepted)
{
    if (!c || c->ph != P_IN_GAME || !c->have_duel)
        return -1;

    mmo_wbuf body;
    mmo_wbuf_init(&body);
    mmo_game_write_duel_response(&body, accepted, "");
    int rc = body.err
                 ? (fail(c, "duel: could not frame InGameChallengeResponse"), -1)
                 : send_game_packet(c, MMO_GAME_OP_DUEL_RESPONSE, body.data,
                                    body.len,
                                    "duel: could not send InGameChallengeResponse");
    mmo_wbuf_free(&body);
    /* The offer is spent either way: the server has one answer and will not ask
     * again, so holding it would let a host answer twice. */
    if (rc == 0)
        c->have_duel = 0;
    return rc;
}

const openmmo_link_battle *openmmo_client_link_battle(const openmmo_client *c)
{
    if (!c)
        return NULL;
    return &c->link_battle;
}

/* --- the link Super Contest ----------------------------------------------- */

const openmmo_contest *openmmo_client_contest(const openmmo_client *c)
{
    static const openmmo_contest none;
    return c ? &c->contest : &none;
}

/* One frame up the c2s half of the pair. `seat` is this client's own where the
 * kind names one, and -1 where it does not: the server stamps the sender on
 * every frame it relays back down, so nothing here has to be believed. */
static int contest_send(openmmo_client *c, int kind, int session_id, int seat,
                        const void *data, int len)
{
    mmo_wbuf body;
    int rc;

    mmo_wbuf_init(&body);
    if (mmo_game_write_contest_comm(&body, kind, session_id, seat, data, len)
        != 0) {
        mmo_wbuf_free(&body);
        fail(c, "contest: could not frame ContestComm");
        return -1;
    }
    rc = send_game_packet(c, MMO_GAME_OP_CONTEST_UP, body.data, body.len,
                          "contest: could not send ContestComm");
    mmo_wbuf_free(&body);
    return rc;
}

int openmmo_client_contest_queue(openmmo_client *c, int rank, int type,
                                 int party_slot)
{
    u8 want[3];

    if (!c || c->ph != P_IN_GAME)
        return -1;
    if (rank < 0 || rank > 0xff || type < 0 || type > 0xff)
        return -1;
    if (party_slot < 0 || party_slot >= OPENMMO_PARTY_MAX)
        return -1;

    memset(&c->contest, 0, sizeof c->contest);
    c->contest.queued = 1;
    c->contest.rank = rank;
    c->contest.type = type;
    c->contest.party_slot = party_slot;
    c->contest_head = 0;
    c->contest_count = 0;
    c->contest_dropped = 0;

    want[0] = (u8)rank;
    want[1] = (u8)type;
    want[2] = (u8)party_slot;
    return contest_send(c, MMO_CONTEST_KIND_QUEUE, 0, -1, want, sizeof want);
}

int openmmo_client_contest_cancel(openmmo_client *c)
{
    if (!c || c->ph != P_IN_GAME || !c->contest.queued)
        return -1;
    memset(&c->contest, 0, sizeof c->contest);
    return contest_send(c, MMO_CONTEST_KIND_CANCEL, 0, -1, NULL, 0);
}

int openmmo_client_contest_leave(openmmo_client *c)
{
    int session, seat;

    if (!c || c->ph != P_IN_GAME || !c->contest.valid)
        return -1;
    session = c->contest.session_id;
    seat = c->contest.seat;
    memset(&c->contest, 0, sizeof c->contest);
    c->contest_head = 0;
    c->contest_count = 0;
    return contest_send(c, MMO_CONTEST_KIND_LEAVE, session, seat, NULL, 0);
}

int openmmo_client_contest_send(openmmo_client *c, int kind,
                                const void *data, int len)
{
    if (!c || c->ph != P_IN_GAME || !c->contest.valid)
        return -1;
    /* Only the two relayed kinds go out this way. The rest are this client
     * saying something about itself and have their own calls, so a caller that
     * reaches for one here has confused the two halves. */
    if (kind != MMO_CONTEST_KIND_DATA && kind != MMO_CONTEST_KIND_SYNC)
        return -1;
    if (len < 0 || len > MMO_CONTEST_DATA_MAX || (len > 0 && data == NULL))
        return -1;
    return contest_send(c, kind, c->contest.session_id, c->contest.seat,
                        data, len);
}

int openmmo_client_contest_result(openmmo_client *c, const u8 *placement, int n)
{
    if (!c || c->ph != P_IN_GAME || !c->contest.valid)
        return -1;
    if (placement == NULL || n < 2 || n > OPENMMO_CONTEST_SEATS)
        return -1;
    return contest_send(c, MMO_CONTEST_KIND_RESULT, c->contest.session_id,
                        c->contest.seat, placement, n);
}

int openmmo_client_contest_recv(openmmo_client *c, mmo_contest_comm *out)
{
    if (!c || !out || c->contest_count <= 0)
        return 0;
    *out = c->contest_q[c->contest_head];
    c->contest_head = (c->contest_head + 1) % OPENMMO_CONTEST_QUEUE;
    c->contest_count--;
    return 1;
}

int openmmo_client_contest_dropped(const openmmo_client *c)
{
    return c ? c->contest_dropped : 0;
}

int openmmo_client_link_send(openmmo_client *c, int kind,
                             const void *data, int len)
{
    if (!c || c->ph != P_IN_GAME || !c->link_battle.valid)
        return -1;
    if (len < 0 || len > MMO_LINK_BLOB_MAX || (len > 0 && data == NULL))
        return -1;

    mmo_wbuf body;
    mmo_wbuf_init(&body);
    mmo_game_write_link_battle_data(&body, c->link_battle.battle_id, kind,
                                    (const u8 *)data, len);
    int rc = body.err
                 ? (fail(c, "link battle: could not frame LinkBattleData"), -1)
                 : send_game_packet(c, MMO_GAME_OP_LINK_BATTLE_DATA, body.data,
                                    body.len,
                                    "link battle: could not send LinkBattleData");
    mmo_wbuf_free(&body);
    /* Reporting the result, or walking out, is this side's end of the fight. */
    if (rc == 0 && (kind == MMO_LINK_KIND_RESULT || kind == MMO_LINK_KIND_LEAVE)) {
        memset(&c->link_battle, 0, sizeof c->link_battle);
        c->link_head = 0;
        c->link_count = 0;
        evq_push(c, OPENMMO_EV_LINK_BATTLE_END);
    }
    return rc;
}

int openmmo_client_link_recv(openmmo_client *c, mmo_link_battle_data *out)
{
    if (!c || out == NULL || c->link_count <= 0)
        return 0;
    *out = c->link_blob[c->link_head];
    c->link_head = (c->link_head + 1) % OPENMMO_LINK_QUEUE;
    c->link_count--;
    return 1;
}

int openmmo_client_link_dropped(const openmmo_client *c)
{
    return c ? c->link_dropped : 0;
}

int openmmo_client_ug_talk_send(openmmo_client *c, int kind, s64 entity_id,
                                const void *data, int len)
{
    if (!c || c->ph != P_IN_GAME)
        return -1;
    if (len < 0 || len > MMO_UG_TALK_MAX || (len > 0 && data == NULL))
        return -1;

    mmo_wbuf body;
    mmo_wbuf_init(&body);
    mmo_game_write_underground_talk(&body, kind, entity_id,
                                    (const u8 *)data, len);
    int rc = body.err
                 ? (fail(c, "underground: could not frame UndergroundTalk"), -1)
                 : send_game_packet(c, MMO_GAME_OP_UNDERGROUND_TALK, body.data,
                                    body.len,
                                    "underground: could not send UndergroundTalk");
    mmo_wbuf_free(&body);
    return rc;
}

int openmmo_client_ug_talk_recv(openmmo_client *c, mmo_underground_talk *out)
{
    if (!c || out == NULL || c->ug_talk_count <= 0)
        return 0;
    *out = c->ug_talk[c->ug_talk_head];
    c->ug_talk_head = (c->ug_talk_head + 1) % OPENMMO_UG_TALK_QUEUE;
    c->ug_talk_count--;
    return 1;
}

int openmmo_client_ug_talk_dropped(const openmmo_client *c)
{
    return c ? c->ug_talk_dropped : 0;
}

u32 openmmo_client_entity_at(const openmmo_client *c, int x, int z)
{
    if (!c)
        return 0;
    for (int i = 0; i < c->ent.cap; i++) {
        const openmmo_entity_slot *s = &c->ent.slots[i];

        /* The rendered tile, not the target one: the question an A press asks
         * is about the avatar the player is looking at, and a peer whose next
         * step has been received but not yet drawn is still standing where the
         * screen shows them. A slot on its way out is nobody. */
        if (!s->used || s->pending_despawn || s->id == 0)
            continue;
        if (s->rx == x && s->rz == z)
            return s->id;
    }
    return 0;
}

int openmmo_client_entity_by_id(const openmmo_client *c, u32 id,
                                openmmo_event *out)
{
    if (!c || !out || id == 0)
        return 0;
    for (int i = 0; i < c->ent.cap; i++) {
        const openmmo_entity_slot *s = &c->ent.slots[i];

        if (!s->used || s->pending_despawn || s->id != id)
            continue;
        memset(out, 0, sizeof *out);
        out->kind = OPENMMO_EV_ENTITY_SPAWN;
        out->entity.slot = i;
        out->entity.localid = OPENMMO_ENTITY_LOCALID_BASE + i;
        out->entity.x = s->rx;
        out->entity.z = s->rz;
        out->entity.dir = s->rdir;
        out->entity.gender = s->appearance.gender;
        out->entity.version = s->appearance.version;
        out->entity.has_body = s->appearance.has_body;
        out->entity.has_follower = s->appearance.has_follower;
        out->entity.follower_gfx = s->appearance.follower_gfx;
        out->entity.gfx = s->appearance.gfx;
        memcpy(out->entity.name, s->appearance.name, sizeof out->entity.name);
        return 1;
    }
    return 0;
}

int openmmo_client_send_bag_delta(openmmo_client *c, int item, int delta)
{
    if (!c || c->ph != P_IN_GAME || delta == 0)
        return -1;

    mmo_wbuf body;
    mmo_wbuf_init(&body);
    mmo_game_write_bag_delta(&body, (u16)item, (s16)delta);
    int rc = body.err
                 ? (fail(c, "bag: could not frame BagDelta"), -1)
                 : send_game_packet(c, MMO_GAME_OP_BAG_DELTA, body.data,
                                    body.len, "bag: could not send BagDelta");
    mmo_wbuf_free(&body);
    return rc;
}

int openmmo_client_send_registered_item(openmmo_client *c, int item)
{
    if (!c || c->ph != P_IN_GAME || item < 0 || item > 0xFFFF)
        return -1;

    mmo_wbuf body;
    mmo_wbuf_init(&body);
    mmo_game_write_registered_item(&body, (u16)item);
    int rc = body.err
                 ? (fail(c, "bag: could not frame RegisteredItem"), -1)
                 : send_game_packet(c, MMO_GAME_OP_REGISTERED_ITEM, body.data,
                                    body.len, "bag: could not send RegisteredItem");
    mmo_wbuf_free(&body);
    /* The local copy follows the report, so a reseat mid-session does not
     * put an old register back. */
    if (rc == 0) {
        c->bag.registered_item = (u16)item;
        c->bag.registered_valid = 1;
    }
    return rc;
}

int openmmo_client_send_money_delta(openmmo_client *c, int delta)
{
    if (!c || c->ph != P_IN_GAME || delta == 0)
        return -1;

    mmo_wbuf body;
    mmo_wbuf_init(&body);
    mmo_game_write_money_delta(&body, (s32)delta);
    int rc = body.err
                 ? (fail(c, "money: could not frame MoneyDelta"), -1)
                 : send_game_packet(c, MMO_GAME_OP_MONEY_DELTA, body.data,
                                    body.len, "money: could not send MoneyDelta");
    mmo_wbuf_free(&body);
    return rc;
}

int openmmo_client_send_chat(openmmo_client *c, const char *text)
{
    return openmmo_client_send_chat_mode(c, MMO_CHAT_NORMAL, text);
}

int openmmo_client_send_chat_mode(openmmo_client *c, int mode, const char *text)
{
    if (!c || c->ph != P_IN_GAME || !text)
        return -1;
    /* The whisper's mode carries a target and has its own frame; refusing it
     * here keeps this the channel path and openmmo_client_send_whisper the
     * addressed one. */
    if (mode == MMO_CHAT_WHISPER)
        return -1;

    mmo_wbuf body;
    mmo_wbuf_init(&body);
    mmo_game_write_chat_mode(&body, mode, text);
    int rc = body.err
                 ? (fail(c, "chat: could not frame ChatMessageSendPacket"), -1)
                 : send_game_packet(c, MMO_GAME_OP_CHAT_SEND, body.data, body.len,
                                    "chat: could not send ChatMessageSendPacket");
    mmo_wbuf_free(&body);
    return rc;
}

int openmmo_client_send_battle_chat(openmmo_client *c, const char *text)
{
    if (!c || c->ph != P_IN_GAME || !text || !text[0])
        return -1;

    mmo_wbuf body;
    mmo_wbuf_init(&body);
    mmo_game_write_battle_chat(&body, text);
    int rc = body.err
                 ? (fail(c, "battle chat: could not frame"), -1)
                 : send_game_packet(c, MMO_GAME_OP_BATTLE_CHAT_SEND, body.data,
                                    body.len, "battle chat: could not send");
    mmo_wbuf_free(&body);
    return rc;
}

/* The slot range a container addresses. All three come from the game client: it
 * sizes container 0 at 660 and container 1 at the party's six, and the day care
 * building has two. */
static int container_slots(int container)
{
    if (container == OPENMMO_CONTAINER_PC)
        return OPENMMO_STORAGE_MAX;
    if (container == OPENMMO_CONTAINER_PARTY)
        return OPENMMO_PARTY_MAX;
    if (container == OPENMMO_CONTAINER_DAYCARE)
        return OPENMMO_DAYCARE_MAX;
    return 0;
}

int openmmo_client_move_pokemon_batch(openmmo_client *c,
                                      const mmo_pokemon_move *moves, int n)
{
    if (!c || c->ph != P_IN_GAME || moves == NULL)
        return -1;
    for (int i = 0; i < n; i++) {
        if (moves[i].from_slot < 0
            || moves[i].from_slot >= container_slots(moves[i].from_container))
            return -1;
        if (moves[i].to_slot < 0
            || moves[i].to_slot >= container_slots(moves[i].to_container))
            return -1;
    }

    mmo_wbuf body;
    mmo_wbuf_init(&body);
    int rc = mmo_game_write_pokemon_move(&body, moves, n);
    if (rc == 0)
        rc = body.err
                 ? (fail(c, "storage: could not frame PokemonMovePacket"), -1)
                 : send_game_packet(c, MMO_GAME_OP_POKEMON_MOVE, body.data,
                                    body.len,
                                    "storage: could not send PokemonMovePacket");
    mmo_wbuf_free(&body);
    return rc;
}

int openmmo_client_move_pokemon(openmmo_client *c, int from_container,
                                int from_slot, int to_container, int to_slot)
{
    mmo_pokemon_move mv;

    mv.from_container = (u8)from_container;
    mv.from_slot = (s16)from_slot;
    mv.to_container = (u8)to_container;
    mv.to_slot = (s16)to_slot;
    return openmmo_client_move_pokemon_batch(c, &mv, 1);
}

openmmo_battle_state openmmo_client_battle_state(const openmmo_client *c)
{
    return c ? c->battle : OPENMMO_BATTLE_NONE;
}

const openmmo_battle_field *openmmo_client_battle_field(const openmmo_client *c)
{
    return c ? &c->field : NULL;
}

const openmmo_battle_anims *openmmo_client_battle_anims(const openmmo_client *c)
{
    return c ? &c->anims : NULL;
}

int openmmo_client_battle_select(openmmo_client *c, const mmo_battle_select *sel)
{
    if (!c || !sel || c->ph != P_IN_GAME || c->battle != OPENMMO_BATTLE_ACTIVE)
        return -1;

    mmo_wbuf body;
    mmo_wbuf_init(&body);
    if (mmo_game_write_battle_select(&body, sel) != 0) {
        mmo_wbuf_free(&body);
        return -1;
    }
    int rc = body.err
                 ? (fail(c, "battle: could not frame BattleActionSelectPacket"), -1)
                 : send_game_packet(c, MMO_GAME_OP_BATTLE_ACTION_SELECT, body.data,
                                    body.len,
                                    "battle: could not send BattleActionSelectPacket");
    mmo_wbuf_free(&body);
    return rc;
}

int openmmo_client_battle_run(openmmo_client *c)
{
    mmo_battle_select sel;

    memset(&sel, 0, sizeof sel);
    sel.action = MMO_BATTLE_ACTION_RUN;
    return openmmo_client_battle_select(c, &sel);
}

const openmmo_event *openmmo_client_move_learn_pending(const openmmo_client *c)
{
    if (!c || !c->have_move_learn)
        return NULL;
    return &c->move_learn;
}

int openmmo_client_reply_move_learn(openmmo_client *c, int slot)
{
    if (!c || c->ph != P_IN_GAME || !c->have_move_learn)
        return -1;
    if (slot != OPENMMO_MOVE_LEARN_NO_SLOT &&
        (slot < 0 || slot >= OPENMMO_MOVE_SLOTS))
        return -1;

    mmo_wbuf body;
    mmo_wbuf_init(&body);
    int rc = mmo_game_write_move_learn_reply(&body, c->move_learn.move_learn.monster_id,
                                             slot, c->move_learn.move_learn.move_id);
    if (rc == 0)
        rc = body.err
                 ? (fail(c, "move learn: could not frame MoveLearnReplyPacket"), -1)
                 : send_game_packet(c, MMO_GAME_OP_MOVE_LEARN_REPLY, body.data,
                                    body.len,
                                    "move learn: could not send MoveLearnReplyPacket");
    mmo_wbuf_free(&body);
    /* The offer is spent either way: a refused send has torn the session down,
     * and a sent one is the server's to answer. */
    if (rc == 0)
        c->have_move_learn = 0;
    return rc;
}

const openmmo_event *openmmo_client_evolution_pending(const openmmo_client *c)
{
    if (!c || !c->have_evolution)
        return NULL;
    return &c->evolution;
}

int openmmo_client_reply_evolution(openmmo_client *c, int accept)
{
    if (!c || c->ph != P_IN_GAME || !c->have_evolution)
        return -1;
    /* Whether the player may stop it is the server's call, carried on the prompt.
     * The game client hides its own cancel button on a prompt without it, so a
     * decline is one this side would never compose. */
    if (!accept && !c->evolution.evolution.cancelable)
        return -1;

    mmo_wbuf body;
    mmo_wbuf_init(&body);
    int rc = mmo_game_write_evolution_reply(&body,
                                            c->evolution.evolution.monster_id,
                                            accept);
    if (rc == 0)
        rc = body.err
                 ? (fail(c, "evolution: could not frame "
                            "EvolutionPromptResponsePacket"), -1)
                 : send_game_packet(c, MMO_GAME_OP_EVOLUTION_REPLY, body.data,
                                    body.len,
                                    "evolution: could not send "
                                    "EvolutionPromptResponsePacket");
    mmo_wbuf_free(&body);
    if (rc == 0)
        c->have_evolution = 0;
    return rc;
}

const openmmo_incubators *openmmo_client_incubators(const openmmo_client *c)
{
    return c ? &c->incubators : NULL;
}

const openmmo_breeding_forecast *
openmmo_client_breeding_forecast(const openmmo_client *c)
{
    return c ? &c->forecast : NULL;
}

int openmmo_client_breeding_preview(openmmo_client *c, s64 own_id,
                                    s64 partner_id, int gender)
{
    if (!c || c->ph != P_IN_GAME)
        return -1;

    mmo_wbuf body;
    mmo_wbuf_init(&body);
    int rc = mmo_game_write_breeding_assign(&body, own_id, partner_id, gender);
    if (rc == 0)
        rc = body.err
                 ? (fail(c, "breeding: could not frame AssignBreedingSlotPacket"), -1)
                 : send_game_packet(c, MMO_GAME_OP_BREEDING_ASSIGN, body.data,
                                    body.len,
                                    "breeding: could not send AssignBreedingSlotPacket");
    mmo_wbuf_free(&body);
    return rc;
}

int openmmo_client_breeding_submit(openmmo_client *c, int session, s64 own_id,
                                   s64 partner_id, int gender, int item_key)
{
    if (!c || c->ph != P_IN_GAME)
        return -1;

    s64 parents[MMO_BREED_PARENTS];
    parents[0] = own_id;
    parents[1] = partner_id;

    mmo_wbuf body;
    mmo_wbuf_init(&body);
    int rc = mmo_game_write_breeding_submit(&body, session, parents, gender,
                                            item_key);
    if (rc == 0)
        rc = body.err
                 ? (fail(c, "breeding: could not frame SubmitBreedingPartyPacket"), -1)
                 : send_game_packet(c, MMO_GAME_OP_BREEDING_SUBMIT, body.data,
                                    body.len,
                                    "breeding: could not send SubmitBreedingPartyPacket");
    mmo_wbuf_free(&body);
    return rc;
}

int openmmo_client_self_tile(const openmmo_client *c, int *x, int *z)
{
    if (!c || !c->have_self_tile)
        return -1;
    if (x) *x = c->self_x;
    if (z) *z = c->self_z;
    return 0;
}

const openmmo_world_state *openmmo_client_world_state(const openmmo_client *c)
{
    if (!c || !c->ws.valid)
        return NULL;
    return &c->ws;
}

const openmmo_story_store *openmmo_client_story_store(const openmmo_client *c)
{
    return c ? &c->story : NULL;
}

const openmmo_script_state *openmmo_client_script_state(const openmmo_client *c)
{
    return c ? &c->script_state : NULL;
}

const openmmo_party *openmmo_client_party(const openmmo_client *c)
{
    return c ? &c->party : NULL;
}

const openmmo_storage *openmmo_client_storage(const openmmo_client *c)
{
    return c ? &c->storage : NULL;
}

const openmmo_storage *openmmo_client_daycare(const openmmo_client *c)
{
    return c ? &c->daycare : NULL;
}

const openmmo_bag *openmmo_client_bag(const openmmo_client *c)
{
    return c ? &c->bag : NULL;
}

const openmmo_shop *openmmo_client_shop(const openmmo_client *c)
{
    return c ? &c->shop : NULL;
}

const openmmo_dialog *openmmo_client_dialog(const openmmo_client *c)
{
    return c ? &c->dialog : NULL;
}

const openmmo_script_move *openmmo_client_script_move(const openmmo_client *c)
{
    return c ? &c->script_move : NULL;
}

const openmmo_script_move *openmmo_client_script_move_take(openmmo_client *c)
{
    const openmmo_script_move *seq;

    if (c == NULL || c->script_move_live <= 0)
        return NULL;
    seq = &c->script_move_q[c->script_move_head];
    c->script_move_head = (c->script_move_head + 1) % OPENMMO_SCRIPT_MOVE_QUEUE;
    c->script_move_live--;
    return seq;
}

const openmmo_npc_store *openmmo_client_npcs(const openmmo_client *c)
{
    return c ? &c->npcs : NULL;
}

const openmmo_objectives *openmmo_client_objectives(const openmmo_client *c)
{
    return c ? &c->objectives : NULL;
}

const openmmo_friends *openmmo_client_friends(const openmmo_client *c)
{
    return c ? &c->friends : NULL;
}

int openmmo_client_add_friend(openmmo_client *c, const char *name)
{
    mmo_wbuf body;
    int rc;

    if (!c || c->ph != P_IN_GAME || !name || !name[0])
        return -1;

    mmo_wbuf_init(&body);
    mmo_game_write_friend_name(&body, name);
    rc = body.err
             ? (fail(c, "friends: could not frame add"), -1)
             : send_game_packet(c, MMO_GAME_OP_FRIEND_ADD, body.data, body.len,
                                "friends: could not send add");
    mmo_wbuf_free(&body);
    return rc;
}

int openmmo_client_remove_friend(openmmo_client *c, const char *name)
{
    mmo_wbuf body;
    int rc;

    if (!c || c->ph != P_IN_GAME || !name || !name[0])
        return -1;

    mmo_wbuf_init(&body);
    mmo_game_write_friend_name(&body, name);
    rc = body.err
             ? (fail(c, "friends: could not frame remove"), -1)
             : send_game_packet(c, MMO_GAME_OP_FRIEND_REMOVE, body.data,
                                body.len, "friends: could not send remove");
    mmo_wbuf_free(&body);
    return rc;
}

int openmmo_client_is_friend(const openmmo_client *c, const char *name)
{
    const openmmo_friends *f;
    int i;

    if (!c || !name || !name[0])
        return 0;
    f = &c->friends;
    for (i = 0; i < f->count; i++) {
        if (strcasecmp(f->entry[i].name, name) == 0)
            return 1;
    }
    return 0;
}

int openmmo_client_block(openmmo_client *c, const char *name)
{
    mmo_wbuf body;
    int rc;

    if (!c || c->ph != P_IN_GAME || !name || !name[0])
        return -1;

    mmo_wbuf_init(&body);
    mmo_game_write_block(&body, name, "--");
    rc = body.err
             ? (fail(c, "block: could not frame"), -1)
             : send_game_packet(c, MMO_GAME_OP_BLOCK, body.data, body.len,
                                "block: could not send");
    mmo_wbuf_free(&body);
    return rc;
}

int openmmo_client_trade_request(openmmo_client *c, const char *name)
{
    mmo_wbuf body;
    int rc;

    if (!c || c->ph != P_IN_GAME || !name || !name[0])
        return -1;

    mmo_wbuf_init(&body);
    mmo_game_write_trade_request(&body, name);
    rc = body.err
             ? (fail(c, "trade: could not frame request"), -1)
             : send_game_packet(c, MMO_GAME_OP_TRADE_REQUEST, body.data,
                                body.len, "trade: could not send request");
    mmo_wbuf_free(&body);
    return rc;
}

const openmmo_trade *openmmo_client_trade(const openmmo_client *c)
{
    /*
     * Designated, and the reason is that this went wrong: written positionally it was two
     * fields short of the struct, so `role` took the empty name, `peer_gender` took the -1
     * that belongs to `self_slot`, and the sentinel for "no trade" claimed party slot 0 was on
     * the table.
     */
    static const openmmo_trade none = { .self_slot = -1 };
    return c != NULL ? &c->trade : &none;
}

int openmmo_client_trade_action(openmmo_client *c, int action)
{
    mmo_wbuf body;
    int rc;

    if (!c || c->ph != P_IN_GAME)
        return -1;

    /* The server does not echo a confirm back to its sender, so this side of
     * the agreement is written here. Everything else the 0x6A answers say. */
    if (action == MMO_TRADE_ACTION_CONFIRM && c->trade.open)
        c->trade.self_confirmed = 1;

    mmo_wbuf_init(&body);
    mmo_game_write_trade_action(&body, action);
    rc = body.err
             ? (fail(c, "trade: could not frame action"), -1)
             : send_game_packet(c, MMO_GAME_OP_TRADE_ACTION, body.data,
                                body.len, "trade: could not send action");
    mmo_wbuf_free(&body);
    return rc;
}

int openmmo_client_trade_select(openmmo_client *c, int slot)
{
    mmo_wbuf body;
    int rc;

    if (!c || c->ph != P_IN_GAME || !c->trade.open || slot < 0 ||
        slot >= OPENMMO_PARTY_MAX)
        return -1;

    c->trade.self_slot = slot;
    c->trade.self_confirmed = 0;
    c->trade.peer_confirmed = 0;

    mmo_wbuf_init(&body);
    mmo_game_write_trade_select(&body, slot);
    rc = body.err
             ? (fail(c, "trade: could not frame the pick"), -1)
             : send_game_packet(c, MMO_GAME_OP_TRADE_SELECT, body.data,
                                body.len, "trade: could not send the pick");
    mmo_wbuf_free(&body);
    return rc;
}

int openmmo_client_trade_comm_send(openmmo_client *c, int channel, int cmd,
                                   const void *data, int len)
{
    mmo_wbuf body;
    int rc;

    if (!c || c->ph != P_IN_GAME || len < 0 || len > MMO_TRADE_COMM_MAX)
        return -1;

    mmo_wbuf_init(&body);
    mmo_game_write_trade_comm(&body, channel, cmd, data, len);
    rc = body.err
             ? (fail(c, "trade: could not frame scene traffic"), -1)
             : send_game_packet(c, MMO_GAME_OP_TRADE_COMM, body.data,
                                body.len, "trade: could not send scene traffic");
    mmo_wbuf_free(&body);
    return rc;
}

int openmmo_client_trade_comm_recv(openmmo_client *c, mmo_trade_comm *out)
{
    if (!c || c->trade_comm_count < 1)
        return 0;
    *out = c->trade_comm[c->trade_comm_head];
    c->trade_comm_head = (c->trade_comm_head + 1) % OPENMMO_TRADE_COMM_QUEUE;
    c->trade_comm_count--;
    return 1;
}

int openmmo_client_trade_comm_dropped(const openmmo_client *c)
{
    return c != NULL ? c->trade_comm_dropped : 0;
}

const openmmo_gtl *openmmo_client_gtl(const openmmo_client *c)
{
    static const openmmo_gtl none = { 0 };
    return c != NULL ? &c->gtl : &none;
}

int openmmo_client_gtl_open(openmmo_client *c)
{
    mmo_wbuf body;
    int rc;

    if (!c || c->ph != P_IN_GAME)
        return -1;

    mmo_wbuf_init(&body);
    mmo_game_write_gtl_open(&body, c->gtl.session_ts);
    rc = body.err
             ? (fail(c, "gtl: could not frame the open"), -1)
             : send_game_packet(c, MMO_GAME_OP_GTL_OPEN, body.data, body.len,
                                "gtl: could not send the open");
    mmo_wbuf_free(&body);
    return rc;
}

int openmmo_client_gtl_search(openmmo_client *c, int kind, int sort, int page,
                              const mmo_gtl_search *filter)
{
    mmo_wbuf body;
    int rc;

    if (!c || c->ph != P_IN_GAME || page < 0)
        return -1;
    if (kind != MMO_GTL_KIND_POKEMON && kind != MMO_GTL_KIND_ITEM &&
        kind != MMO_GTL_KIND_OWN)
        return -1;
    if (sort < MMO_GTL_SORT_NEWEST || sort > MMO_GTL_SORT_PRICE_DESC)
        return -1;

    c->gtl.next_request++;

    mmo_wbuf_init(&body);
    mmo_game_write_gtl_search_req(&body, c->gtl.next_request & 0x7F, kind,
                                  sort, page, filter);
    rc = body.err
             ? (fail(c, "gtl: could not frame the search"), -1)
             : send_game_packet(c, MMO_GAME_OP_GTL_SEARCH_REQ, body.data,
                                body.len, "gtl: could not send the search");
    mmo_wbuf_free(&body);
    return rc;
}

int openmmo_client_gtl_list_mon(openmmo_client *c, s64 mon_id, s32 price)
{
    mmo_wbuf body;
    int rc;

    if (!c || c->ph != P_IN_GAME || price < 1 || price > 999999)
        return -1;

    mmo_wbuf_init(&body);
    mmo_game_write_gtl_create_mon(&body, mon_id, price);
    rc = body.err
             ? (fail(c, "gtl: could not frame the listing"), -1)
             : send_game_packet(c, MMO_GAME_OP_GTL_CREATE, body.data,
                                body.len, "gtl: could not send the listing");
    mmo_wbuf_free(&body);
    return rc;
}

int openmmo_client_gtl_list_item(openmmo_client *c, u16 item_id, s32 quantity,
                                 s32 price)
{
    mmo_wbuf body;
    int rc;

    if (!c || c->ph != P_IN_GAME || quantity < 1 || price < 1 ||
        price > 999999)
        return -1;

    mmo_wbuf_init(&body);
    mmo_game_write_gtl_create_item(&body, item_id, quantity, price);
    rc = body.err
             ? (fail(c, "gtl: could not frame the listing"), -1)
             : send_game_packet(c, MMO_GAME_OP_GTL_CREATE, body.data,
                                body.len, "gtl: could not send the listing");
    mmo_wbuf_free(&body);
    return rc;
}

int openmmo_client_gtl_cancel(openmmo_client *c, s64 listing_id)
{
    mmo_wbuf body;
    int rc;

    if (!c || c->ph != P_IN_GAME)
        return -1;

    mmo_wbuf_init(&body);
    mmo_game_write_gtl_cancel(&body, listing_id);
    rc = body.err
             ? (fail(c, "gtl: could not frame the take-back"), -1)
             : send_game_packet(c, MMO_GAME_OP_GTL_CANCEL, body.data,
                                body.len, "gtl: could not send the take-back");
    mmo_wbuf_free(&body);
    return rc;
}

int openmmo_client_gtl_buy(openmmo_client *c, s64 listing_id, int quantity)
{
    mmo_wbuf body;
    int rc;

    if (!c || c->ph != P_IN_GAME || quantity < 1)
        return -1;

    mmo_wbuf_init(&body);
    mmo_game_write_gtl_buy(&body, listing_id, quantity);
    rc = body.err
             ? (fail(c, "gtl: could not frame the purchase"), -1)
             : send_game_packet(c, MMO_GAME_OP_GTL_BUY, body.data, body.len,
                                "gtl: could not send the purchase");
    mmo_wbuf_free(&body);
    return rc;
}

int openmmo_client_gtl_claim(openmmo_client *c, const s64 *listing_ids,
                             int count)
{
    mmo_wbuf body;
    int rc;

    if (!c || c->ph != P_IN_GAME || !listing_ids || count < 1 || count > 255)
        return -1;

    mmo_wbuf_init(&body);
    mmo_game_write_gtl_claim(&body, listing_ids, count);
    rc = body.err
             ? (fail(c, "gtl: could not frame the claim"), -1)
             : send_game_packet(c, MMO_GAME_OP_GTL_CLAIM, body.data, body.len,
                                "gtl: could not send the claim");
    mmo_wbuf_free(&body);
    return rc;
}

int openmmo_client_gtl_price(openmmo_client *c, s64 listing_id, s32 new_price)
{
    mmo_wbuf body;
    int rc;

    if (!c || c->ph != P_IN_GAME || new_price < 1 || new_price > 999999)
        return -1;

    mmo_wbuf_init(&body);
    mmo_game_write_gtl_price(&body, listing_id, new_price);
    rc = body.err
             ? (fail(c, "gtl: could not frame the price change"), -1)
             : send_game_packet(c, MMO_GAME_OP_GTL_PRICE, body.data, body.len,
                                "gtl: could not send the price change");
    mmo_wbuf_free(&body);
    return rc;
}

int openmmo_client_gtl_market(openmmo_client *c, u16 item_id, int quantity,
                              s32 budget)
{
    mmo_wbuf body;
    int rc;

    if (!c || c->ph != P_IN_GAME || quantity < 1 || budget < 1)
        return -1;

    mmo_wbuf_init(&body);
    mmo_game_write_gtl_market_buy(&body, item_id, quantity, budget);
    rc = body.err
             ? (fail(c, "gtl: could not frame the market buy"), -1)
             : send_game_packet(c, MMO_GAME_OP_GTL_MARKET, body.data, body.len,
                                "gtl: could not send the market buy");
    mmo_wbuf_free(&body);
    return rc;
}

int openmmo_client_gtl_log(openmmo_client *c)
{
    if (!c || c->ph != P_IN_GAME)
        return -1;
    return send_game_packet(c, MMO_GAME_OP_GTL_LOG_REQ, NULL, 0,
                            "gtl: could not ask for the log");
}

int openmmo_client_send_whisper(openmmo_client *c, const char *name,
                                const char *text)
{
    mmo_wbuf body;
    int rc;

    if (!c || c->ph != P_IN_GAME || !name || !text || !text[0])
        return -1;

    mmo_wbuf_init(&body);
    mmo_game_write_whisper(&body, name, text);
    rc = body.err
             ? (fail(c, "whisper: could not frame"), -1)
             : send_game_packet(c, MMO_GAME_OP_CHAT_SEND, body.data, body.len,
                                "whisper: could not send");
    mmo_wbuf_free(&body);
    return rc;
}

const openmmo_guild *openmmo_client_guild(const openmmo_client *c)
{
    return c ? &c->guild : NULL;
}

int openmmo_client_guild_create(openmmo_client *c, const char *name,
                                const char *tag)
{
    mmo_wbuf body;
    int rc;

    if (!c || c->ph != P_IN_GAME || !name || !name[0] || !tag || !tag[0])
        return -1;

    mmo_wbuf_init(&body);
    mmo_game_write_guild_create(&body, name, tag);
    rc = body.err
             ? (fail(c, "guild: could not frame create"), -1)
             : send_game_packet(c, MMO_GAME_OP_GUILD_CREATE, body.data,
                                body.len, "guild: could not send create");
    mmo_wbuf_free(&body);
    return rc;
}

int openmmo_client_guild_invite(openmmo_client *c, const char *name)
{
    mmo_wbuf body;
    int rc;

    if (!c || c->ph != P_IN_GAME || !name || !name[0])
        return -1;

    mmo_wbuf_init(&body);
    mmo_game_write_guild_invite(&body, name);
    rc = body.err
             ? (fail(c, "guild: could not frame invite"), -1)
             : send_game_packet(c, MMO_GAME_OP_GUILD_INVITE, body.data,
                                body.len, "guild: could not send invite");
    mmo_wbuf_free(&body);
    return rc;
}

int openmmo_client_guild_leave(openmmo_client *c)
{
    if (!c || c->ph != P_IN_GAME)
        return -1;
    return send_game_packet(c, MMO_GAME_OP_GUILD_LEAVE, NULL, 0,
                            "guild: could not send leave");
}

int openmmo_client_guild_disband(openmmo_client *c)
{
    mmo_wbuf body;
    int rc;

    if (!c || c->ph != P_IN_GAME)
        return -1;

    mmo_wbuf_init(&body);
    mmo_game_write_guild_disband(&body, 1, c->guild.profile.guild_id);
    rc = body.err
             ? (fail(c, "guild: could not frame disband"), -1)
             : send_game_packet(c, MMO_GAME_OP_GUILD_DISBAND, body.data,
                                body.len, "guild: could not send disband");
    mmo_wbuf_free(&body);
    return rc;
}

int openmmo_client_guild_kick(openmmo_client *c, s64 member)
{
    mmo_wbuf body;
    int rc;

    if (!c || c->ph != P_IN_GAME)
        return -1;

    mmo_wbuf_init(&body);
    mmo_game_write_guild_kick(&body, member);
    rc = body.err
             ? (fail(c, "guild: could not frame kick"), -1)
             : send_game_packet(c, MMO_GAME_OP_GUILD_KICK, body.data,
                                body.len, "guild: could not send kick");
    mmo_wbuf_free(&body);
    return rc;
}

int openmmo_client_guild_rank(openmmo_client *c, s64 member, u8 rank)
{
    mmo_wbuf body;
    int rc;

    if (!c || c->ph != P_IN_GAME)
        return -1;

    mmo_wbuf_init(&body);
    mmo_game_write_guild_rank(&body, member, rank);
    rc = body.err
             ? (fail(c, "guild: could not frame rank"), -1)
             : send_game_packet(c, MMO_GAME_OP_GUILD_RANK, body.data,
                                body.len, "guild: could not send rank");
    mmo_wbuf_free(&body);
    return rc;
}

int openmmo_client_guild_motd(openmmo_client *c, const char *text)
{
    mmo_wbuf body;
    int rc;

    if (!c || c->ph != P_IN_GAME || !text)
        return -1;

    mmo_wbuf_init(&body);
    mmo_game_write_guild_motd(&body, text);
    rc = body.err
             ? (fail(c, "guild: could not frame motd"), -1)
             : send_game_packet(c, MMO_GAME_OP_GUILD_MOTD, body.data,
                                body.len, "guild: could not send motd");
    mmo_wbuf_free(&body);
    return rc;
}

int openmmo_client_guild_log(openmmo_client *c, s16 page)
{
    mmo_wbuf body;
    int rc;

    if (!c || c->ph != P_IN_GAME)
        return -1;

    mmo_wbuf_init(&body);
    mmo_game_write_guild_log_req(&body, page);
    rc = body.err
             ? (fail(c, "guild: could not frame log"), -1)
             : send_game_packet(c, MMO_GAME_OP_GUILD_LOG_REQ, body.data,
                                body.len, "guild: could not send log");
    mmo_wbuf_free(&body);
    return rc;
}

const openmmo_mail *openmmo_client_mail(const openmmo_client *c)
{
    return c ? &c->mail : NULL;
}

int openmmo_client_mail_send(openmmo_client *c, const char *recipient,
                             const char *subject, const char *body)
{
    mmo_wbuf pkt;
    int rc;

    if (!c || c->ph != P_IN_GAME || !recipient || !recipient[0]
        || !subject || !body)
        return -1;

    mmo_wbuf_init(&pkt);
    mmo_game_write_mail_compose(&pkt, recipient, subject, body);
    rc = pkt.err
             ? (fail(c, "mail: could not frame send"), -1)
             : send_game_packet(c, MMO_GAME_OP_MAIL_COMPOSE, pkt.data,
                                pkt.len, "mail: could not send");
    mmo_wbuf_free(&pkt);
    return rc;
}

int openmmo_client_mail_page(openmmo_client *c, s16 page, int sent)
{
    mmo_wbuf pkt;
    int rc;

    if (!c || c->ph != P_IN_GAME)
        return -1;

    mmo_wbuf_init(&pkt);
    mmo_game_write_mail_page_req(&pkt, page, sent);
    rc = pkt.err
             ? (fail(c, "mail: could not frame page"), -1)
             : send_game_packet(c, MMO_GAME_OP_MAIL_PAGE_REQ, pkt.data,
                                pkt.len, "mail: could not request page");
    mmo_wbuf_free(&pkt);
    return rc;
}

int openmmo_client_mail_read(openmmo_client *c, s64 mail_id)
{
    mmo_wbuf pkt;
    int rc;

    if (!c || c->ph != P_IN_GAME)
        return -1;

    mmo_wbuf_init(&pkt);
    mmo_game_write_mail_detail_req(&pkt, mail_id);
    rc = pkt.err
             ? (fail(c, "mail: could not frame read"), -1)
             : send_game_packet(c, MMO_GAME_OP_MAIL_DETAIL_REQ, pkt.data,
                                pkt.len, "mail: could not read");
    mmo_wbuf_free(&pkt);
    return rc;
}

int openmmo_client_mail_delete(openmmo_client *c, s64 mail_id, s16 page)
{
    mmo_wbuf pkt;
    int rc;

    if (!c || c->ph != P_IN_GAME)
        return -1;

    mmo_wbuf_init(&pkt);
    mmo_game_write_mail_delete(&pkt, mail_id, page);
    rc = pkt.err
             ? (fail(c, "mail: could not frame delete"), -1)
             : send_game_packet(c, MMO_GAME_OP_MAIL_DELETE, pkt.data,
                                pkt.len, "mail: could not delete");
    mmo_wbuf_free(&pkt);
    return rc;
}

const openmmo_ui *openmmo_client_ui(const openmmo_client *c)
{
    return c ? &c->ui : NULL;
}

const openmmo_sync *openmmo_client_sync(const openmmo_client *c)
{
    return c ? &c->sync : NULL;
}

const openmmo_compete *openmmo_client_compete(const openmmo_client *c)
{
    return c ? &c->compete : NULL;
}

const openmmo_gm *openmmo_client_gm(const openmmo_client *c)
{
    return c ? &c->gm : NULL;
}

/* The two staff c2s. Neither is answered by this server; they are sent so
 * that the layouts are exercised end to end rather than only decoded. */
static int gm_send(openmmo_client *c, u8 opcode, mmo_wbuf *body,
                   const char *frame_err, const char *send_err)
{
    int rc;

    rc = body->err ? (fail(c, frame_err), -1)
                   : send_game_packet(c, opcode, body->data, body->len,
                                      send_err);
    mmo_wbuf_free(body);
    return rc;
}

int openmmo_client_admin_note_add(openmmo_client *c, s64 target,
                                  const char *text)
{
    mmo_wbuf body;

    if (!c || c->ph != P_IN_GAME)
        return -1;
    mmo_wbuf_init(&body);
    mmo_game_write_admin_note_add(&body, target, text);
    return gm_send(c, MMO_GAME_OP_ADMIN_NOTE, &body,
                   "note: could not frame the add",
                   "note: could not send the add");
}

int openmmo_client_admin_note_delete(openmmo_client *c, s64 target,
                                     s64 note_id)
{
    mmo_wbuf body;

    if (!c || c->ph != P_IN_GAME)
        return -1;
    mmo_wbuf_init(&body);
    mmo_game_write_admin_note_delete(&body, target, note_id);
    return gm_send(c, MMO_GAME_OP_ADMIN_NOTE, &body,
                   "note: could not frame the delete",
                   "note: could not send the delete");
}

int openmmo_client_moderation_confirm(openmmo_client *c, s64 target)
{
    mmo_wbuf body;

    if (!c || c->ph != P_IN_GAME)
        return -1;
    mmo_wbuf_init(&body);
    mmo_game_write_moderation_confirm(&body, target);
    return gm_send(c, MMO_GAME_OP_MOD_CONFIRM, &body,
                   "moderation: could not frame the confirm",
                   "moderation: could not send the confirm");
}

/* The five empty-bodied requests of the group. Each is its opcode and
 * nothing else, so one helper writes all five. */
static int compete_send_empty(openmmo_client *c, u8 opcode, const char *what)
{
    if (!c || c->ph != P_IN_GAME)
        return -1;
    return send_game_packet(c, opcode, NULL, 0, what);
}

int openmmo_client_queue_join(openmmo_client *c)
{
    return compete_send_empty(c, MMO_GAME_OP_QUEUE_JOIN,
                              "queue: could not send join");
}

int openmmo_client_queue_leave(openmmo_client *c)
{
    return compete_send_empty(c, MMO_GAME_OP_QUEUE_LEAVE,
                              "queue: could not send leave");
}

int openmmo_client_queue_cancel(openmmo_client *c)
{
    return compete_send_empty(c, MMO_GAME_OP_QUEUE_CANCEL,
                              "queue: could not send cancel");
}

int openmmo_client_tourney_teleport(openmmo_client *c)
{
    return compete_send_empty(c, MMO_GAME_OP_TOURNEY_TELEPORT,
                              "tourney: could not accept the teleport");
}

int openmmo_client_coop_board(openmmo_client *c)
{
    return compete_send_empty(c, MMO_GAME_OP_COOP_SCORE_REQ,
                              "board: could not ask for the co-op board");
}

static int compete_send(openmmo_client *c, u8 opcode, mmo_wbuf *body,
                        const char *frame_err, const char *send_err)
{
    int rc;

    rc = body->err ? (fail(c, frame_err), -1)
                   : send_game_packet(c, opcode, body->data, body->len,
                                      send_err);
    mmo_wbuf_free(body);
    return rc;
}

int openmmo_client_queue_tier(openmmo_client *c, s32 tier_id)
{
    mmo_wbuf body;

    if (!c || c->ph != P_IN_GAME)
        return -1;
    mmo_wbuf_init(&body);
    mmo_game_write_queue_action_tier(&body, tier_id);
    return compete_send(c, MMO_GAME_OP_QUEUE_ACTION, &body,
                        "queue: could not frame the tier action",
                        "queue: could not send the tier action");
}

int openmmo_client_queue_target(openmmo_client *c, s64 target, s16 slot)
{
    mmo_wbuf body;

    if (!c || c->ph != P_IN_GAME)
        return -1;
    mmo_wbuf_init(&body);
    mmo_game_write_queue_action_target(&body, target, slot);
    return compete_send(c, MMO_GAME_OP_QUEUE_ACTION, &body,
                        "queue: could not frame the target action",
                        "queue: could not send the target action");
}

int openmmo_client_queue_teleport(openmmo_client *c)
{
    mmo_wbuf body;

    if (!c || c->ph != P_IN_GAME)
        return -1;
    mmo_wbuf_init(&body);
    mmo_game_write_queue_action_teleport(&body);
    return compete_send(c, MMO_GAME_OP_QUEUE_ACTION, &body,
                        "queue: could not frame the teleport action",
                        "queue: could not send the teleport action");
}

int openmmo_client_queue_langs(openmmo_client *c, const s8 *langs, int count)
{
    mmo_wbuf body;

    if (!c || c->ph != P_IN_GAME || (count > 0 && !langs))
        return -1;
    mmo_wbuf_init(&body);
    mmo_game_write_queue_langs(&body, langs, count);
    return compete_send(c, MMO_GAME_OP_QUEUE_LANGS, &body,
                        "queue: could not frame the language list",
                        "queue: could not send the language list");
}

int openmmo_client_tier_select(openmmo_client *c, s8 slot, s8 tier)
{
    mmo_wbuf body;

    if (!c || c->ph != P_IN_GAME)
        return -1;
    mmo_wbuf_init(&body);
    mmo_game_write_tier_select(&body, slot, tier);
    return compete_send(c, MMO_GAME_OP_TIER_SELECT, &body,
                        "tier: could not frame the selection",
                        "tier: could not send the selection");
}

int openmmo_client_queue_signup(openmmo_client *c, const s8 *queues,
                                const s8 *slots, int count)
{
    mmo_wbuf body;

    if (!c || c->ph != P_IN_GAME || count < 1 || !queues)
        return -1;
    mmo_wbuf_init(&body);
    if (mmo_game_write_queue_signup(&body, queues, slots, count) != 0) {
        mmo_wbuf_free(&body);
        return -1;
    }
    return compete_send(c, MMO_GAME_OP_QUEUE_SIGNUP, &body,
                        "queue: could not frame the signup",
                        "queue: could not send the signup");
}

int openmmo_client_tourney_signup(openmmo_client *c, s64 tourney_id, s8 slot)
{
    mmo_wbuf body;

    if (!c || c->ph != P_IN_GAME)
        return -1;
    mmo_wbuf_init(&body);
    mmo_game_write_tourney_signup(&body, tourney_id, slot);
    return compete_send(c, MMO_GAME_OP_QUEUE_SIGNUP, &body,
                        "queue: could not frame the tournament signup",
                        "queue: could not send the tournament signup");
}

int openmmo_client_tourney_register(openmmo_client *c, int register_)
{
    mmo_wbuf body;

    if (!c || c->ph != P_IN_GAME)
        return -1;
    mmo_wbuf_init(&body);
    mmo_game_write_tourney_register(&body, register_);
    return compete_send(c, MMO_GAME_OP_TOURNEY_REGISTER, &body,
                        "tourney: could not frame the registration",
                        "tourney: could not send the registration");
}

int openmmo_client_tourney_view(openmmo_client *c, s8 tourney_id, int active,
                                s16 tab)
{
    mmo_wbuf body;

    if (!c || c->ph != P_IN_GAME)
        return -1;
    mmo_wbuf_init(&body);
    mmo_game_write_tourney_view(&body, tourney_id, active, tab);
    return compete_send(c, MMO_GAME_OP_TOURNEY_VIEW, &body,
                        "tourney: could not frame the view request",
                        "tourney: could not send the view request");
}

int openmmo_client_score_board(openmmo_client *c, s8 category)
{
    mmo_wbuf body;

    if (!c || c->ph != P_IN_GAME)
        return -1;
    mmo_wbuf_init(&body);
    mmo_game_write_score_board_req(&body, category);
    return compete_send(c, MMO_GAME_OP_SCORE_BOARD_REQ, &body,
                        "board: could not frame the request",
                        "board: could not send the request");
}

const openmmo_link *openmmo_client_link(const openmmo_client *c)
{
    return c ? &c->link : NULL;
}

int openmmo_client_link_invite(openmmo_client *c, const char *name)
{
    mmo_wbuf body;
    int rc;

    if (!c || c->ph != P_IN_GAME || !name || !name[0])
        return -1;

    mmo_wbuf_init(&body);
    mmo_game_write_link_invite(&body, name);
    rc = body.err
             ? (fail(c, "link: could not frame invite"), -1)
             : send_game_packet(c, MMO_GAME_OP_LINK_INVITE, body.data,
                                body.len, "link: could not send invite");
    mmo_wbuf_free(&body);
    return rc;
}

int openmmo_client_link_leave(openmmo_client *c)
{
    if (!c || c->ph != P_IN_GAME)
        return -1;
    return send_game_packet(c, MMO_GAME_OP_LINK_LEAVE, NULL, 0,
                            "link: could not send leave");
}

int openmmo_client_link_kick(openmmo_client *c, s64 member)
{
    mmo_wbuf body;
    int rc;

    if (!c || c->ph != P_IN_GAME)
        return -1;

    mmo_wbuf_init(&body);
    mmo_game_write_link_id(&body, member);
    rc = body.err
             ? (fail(c, "link: could not frame kick"), -1)
             : send_game_packet(c, MMO_GAME_OP_LINK_KICK, body.data,
                                body.len, "link: could not send kick");
    mmo_wbuf_free(&body);
    return rc;
}

int openmmo_client_link_captain(openmmo_client *c, s64 member)
{
    mmo_wbuf body;
    int rc;

    if (!c || c->ph != P_IN_GAME)
        return -1;

    mmo_wbuf_init(&body);
    mmo_game_write_link_id(&body, member);
    rc = body.err
             ? (fail(c, "link: could not frame captain"), -1)
             : send_game_packet(c, MMO_GAME_OP_LINK_CAPTAIN, body.data,
                                body.len, "link: could not send captain");
    mmo_wbuf_free(&body);
    return rc;
}

int openmmo_client_in_dialog(const openmmo_client *c)
{
    return c && (c->dialog.locked || c->dialog.awaiting);
}

int openmmo_client_reply_dialog(openmmo_client *c, u8 response)
{
    if (!c || c->ph != P_IN_GAME || !c->dialog.awaiting)
        return -1;

    mmo_wbuf body;
    mmo_wbuf_init(&body);
    mmo_game_write_dialog_reply(&body, (u8)c->dialog.flags, response);
    int rc = body.err
                 ? (fail(c, "dialog: could not frame reply"), -1)
                 : send_game_packet(c, MMO_GAME_OP_DIALOG_REPLY, body.data,
                                    body.len, "dialog: could not send reply");
    mmo_wbuf_free(&body);
    if (rc == 0) {
        c->dialog.awaiting = 0;
        c->dialog.open = 0;
    }
    return rc;
}

int openmmo_client_interact_tile(openmmo_client *c)
{
    if (!c || c->ph != P_IN_GAME)
        return -1;
    if (openmmo_client_in_dialog(c))
        return -1;

    return send_game_packet(c, MMO_GAME_OP_TILE_INTERACT, NULL, 0,
                            "interact: could not send TileInteract");
}

int openmmo_client_interact_entity(openmmo_client *c, s64 entity_id,
                                   s64 token)
{
    if (!c || c->ph != P_IN_GAME)
        return -1;
    if (openmmo_client_in_dialog(c))
        return -1;

    mmo_wbuf body;
    mmo_wbuf_init(&body);
    mmo_game_write_entity_interact(&body, entity_id, token);
    int rc = body.err
                 ? (fail(c, "interact: could not frame EntityInteract"), -1)
                 : send_game_packet(c, MMO_GAME_OP_ENTITY_INTERACT, body.data,
                                    body.len,
                                    "interact: could not send EntityInteract");
    mmo_wbuf_free(&body);
    return rc;
}

int openmmo_client_shop_buy(openmmo_client *c, s16 item_id, s16 quantity)
{
    if (!c || c->ph != P_IN_GAME)
        return -1;

    mmo_wbuf body;
    mmo_wbuf_init(&body);
    mmo_game_write_shop_buy(&body, item_id, quantity);
    int rc = body.err
                 ? (fail(c, "shop: could not frame buy"), -1)
                 : send_game_packet(c, MMO_GAME_OP_SHOP_BUY, body.data, body.len,
                                    "shop: could not send buy");
    mmo_wbuf_free(&body);
    return rc;
}

int openmmo_client_shop_sell(openmmo_client *c, s64 item_entity_id,
                             s16 quantity)
{
    if (!c || c->ph != P_IN_GAME)
        return -1;

    mmo_wbuf body;
    mmo_wbuf_init(&body);
    mmo_game_write_shop_sell(&body, item_entity_id, quantity);
    int rc = body.err
                 ? (fail(c, "shop: could not frame sell"), -1)
                 : send_game_packet(c, MMO_GAME_OP_SHOP_SELL, body.data, body.len,
                                    "shop: could not send sell");
    mmo_wbuf_free(&body);
    return rc;
}

int openmmo_client_use_item(openmmo_client *c, u16 item_id,
                            s64 target_entity_id)
{
    if (!c || c->ph != P_IN_GAME)
        return -1;

    mmo_wbuf body;
    mmo_wbuf_init(&body);
    mmo_game_write_item_use(&body, item_id, target_entity_id,
                            (s32)MMO_ITEM_USE_TRAILER);
    int rc = body.err
                 ? (fail(c, "item: could not frame use"), -1)
                 : send_game_packet(c, MMO_GAME_OP_ITEM_USE, body.data, body.len,
                                    "item: could not send use");
    mmo_wbuf_free(&body);
    return rc;
}

int openmmo_story_flag_is_set(const openmmo_story_store *s, int flag_id)
{
    if (!s)
        return 0;
    for (int i = 0; i < s->set_flag_count; i++)
        if (s->set_flag[i] == flag_id)
            return 1;
    return 0;
}

int openmmo_story_var_get(const openmmo_story_store *s, int var_id, s16 *out)
{
    if (!s)
        return 0;
    int k = var_id >= 0x4000 ? var_id - 0x4000 : var_id;
    if (k < 0 || k >= OPENMMO_STORY_VARS || !s->var_present[k])
        return 0;
    if (out)
        *out = s->var[k];
    return 1;
}

int openmmo_world_flag_bit(const openmmo_story_store *s, int group, int bit)
{
    if (!s || group < 0 || group >= OPENMMO_WORLD_FLAG_GROUPS || bit < 0)
        return -1;
    int byte = bit >> 3;
    if (byte >= s->flag_block_len[group])
        return -1;
    return (s->flag_block[group][byte] >> (bit & 7)) & 1;
}

int openmmo_client_live_entities(const openmmo_client *c, openmmo_event *out, int max_out)
{
    openmmo_entity_event evs[OPENMMO_ENTITY_NETID_CEIL];
    int n, i;

    if (!c || !out || max_out <= 0)
        return 0;
    n = openmmo_entity_snapshot(&c->ent, evs, OPENMMO_ENTITY_NETID_CEIL);
    if (n > max_out)
        n = max_out;
    for (i = 0; i < n; i++) {
        memset(&out[i], 0, sizeof out[i]);
        out[i].kind = OPENMMO_EV_ENTITY_SPAWN;
        out[i].entity.slot = evs[i].slot;
        out[i].entity.localid = evs[i].localid;
        out[i].entity.x = evs[i].x;
        out[i].entity.z = evs[i].z;
        out[i].entity.dir = evs[i].dir;
        out[i].entity.speed = evs[i].speed;
        out[i].entity.gender = evs[i].appearance.gender;
        out[i].entity.version = evs[i].appearance.version;
        out[i].entity.has_body = evs[i].appearance.has_body;
        out[i].entity.has_follower = evs[i].appearance.has_follower;
        out[i].entity.follower_gfx = evs[i].appearance.follower_gfx;
        out[i].entity.gfx = evs[i].appearance.gfx;
        memcpy(out[i].entity.name, evs[i].appearance.name,
               sizeof out[i].entity.name);
    }
    return n;
}

const mmo_character_list *openmmo_client_characters(const openmmo_client *c)
{
    static const mmo_character_list empty;
    return c ? &c->chars : &empty;
}

int openmmo_client_picking(const openmmo_client *c)
{
    return c && c->ph == P_GAME_PICK;
}

int openmmo_client_pick_character(openmmo_client *c, int index)
{
    if (!c || c->ph != P_GAME_PICK)
        return -1;
    return send_select_character(c, index);
}

int openmmo_client_transportation(const openmmo_client *c)
{
    return c ? c->self_mount : MMO_TRANSPORT_NONE;
}

int openmmo_client_body_gfx(const openmmo_client *c)
{
    int i;
    int gender;

    if (!c)
        return mmo_appearance_gender_gfx(0);
    gender = c->ws.gender;
    if (c->ws.character_id != 0) {
        for (i = 0; i < c->chars.held; i++) {
            if (c->chars.entry[i].id != c->ws.character_id)
                continue;
            if (c->chars.entry[i].gender == 0 || c->chars.entry[i].gender == 1)
                gender = c->chars.entry[i].gender;
            return mmo_appearance_resolve(gender, &c->chars.entry[i].appearance);
        }
    }
    return mmo_appearance_resolve(gender, NULL);
}

int openmmo_client_create_character(openmmo_client *c,
                                    const mmo_create_character *in)
{
    int i;

    if (!c || c->ph != P_GAME_PICK || !in || !in->name)
        return -1;
    if (in->name[0] == '\0' ||
        mmo_utf16_units(in->name) > MMO_CHAR_NAME_MAX)
        return -1;
    snprintf(c->create_name, sizeof c->create_name, "%s", in->name);
    c->create_name_overflow = 0;
    c->create_gender = in->gender;
    c->create_region = in->starting_region;
    c->create_skin_region = in->appearance.region_selection_index;
    c->create_skin_mask = 0;
    memset(c->create_skin_word, 0, sizeof c->create_skin_word);
    for (i = 0; i < MMO_SKIN_SLOTS; i++) {
        if (!in->appearance.slot[i].present)
            continue;
        c->create_skin_mask |= (u16)(1u << i);
        c->create_skin_word[i] = (u16)((in->appearance.slot[i].type
                                        & MMO_SKIN_TYPE_MASK)
                                       | ((in->appearance.slot[i].color
                                           & MMO_SKIN_COLOR_MASK)
                                          << MMO_SKIN_COLOR_SHIFT));
    }
    c->create_error[0] = '\0';
    send_create_character(c);
    return (c->ph == P_GAME_CREATE) ? 0 : -1;
}

const char *openmmo_client_create_error(const openmmo_client *c)
{
    if (!c || c->create_error[0] == '\0')
        return NULL;
    return c->create_error;
}

int openmmo_client_delete_character(openmmo_client *c, s64 character_id)
{
    mmo_wbuf body;
    int rc;

    /* Only from the list, and only for a row this account actually holds. The
     * server checks both again, it has to, since nothing stops a different
     * client, but refusing a stale id here keeps the lobby out of a round
     * trip it already knows the answer to. */
    if (!c || c->ph != P_GAME_PICK || character_id == 0)
        return -1;
    {
        int i, held = 0;

        for (i = 0; i < c->chars.held; i++) {
            if (c->chars.entry[i].id == character_id)
                held = 1;
        }
        if (!held)
            return -1;
    }

    mmo_wbuf_init(&body);
    mmo_game_write_delete_character(&body, character_id);
    rc = send_game_packet(c, MMO_GAME_OP_DELETE_CHAR, body.data, body.len,
                          "delete: could not send DeleteCharacter");
    mmo_wbuf_free(&body);
    if (rc != 0)
        return -1;
    c->delete_error[0] = '\0';
    c->ph = P_GAME_DELETE;
    arm_deadline(c);
    return 0;
}

const char *openmmo_client_delete_error(const openmmo_client *c)
{
    if (!c || c->delete_error[0] == '\0')
        return NULL;
    return c->delete_error;
}

void openmmo_client_reset_entities(openmmo_client *c)
{
    if (!c)
        return;
    openmmo_entity_reset(&c->ent);
    memset(&c->npcs, 0, sizeof c->npcs);

    /* Drop any queued ENTITY_* events: they name avatars in the torn-down object
     * table. Compact the ring in place, keeping every other event in order. */
    int r = c->evq_head, w = c->evq_head;
    while (r != c->evq_tail) {
        openmmo_event_kind k = c->evq[r].kind;
        if (k != OPENMMO_EV_ENTITY_SPAWN && k != OPENMMO_EV_ENTITY_STEP &&
            k != OPENMMO_EV_ENTITY_TURN && k != OPENMMO_EV_ENTITY_DESPAWN) {
            if (w != r)
                c->evq[w] = c->evq[r];
            w = (w + 1) % EVQ_CAP;
        }
        r = (r + 1) % EVQ_CAP;
    }
    c->evq_tail = w;
}

/* Is the phase a bounded wait that a silent server should be able to time out? */
static int is_wait_phase(phase p)
{
    switch (p) {
    case P_LOGIN_CONNECT: case P_LOGIN_HELLO: case P_LOGIN_AUTH:
    case P_HANDOFF_LIST:  case P_HANDOFF_NODES:
    case P_GAME_CONNECT:  case P_GAME_HELLO:  case P_GAME_JOIN:
    case P_GAME_CHARLIST: case P_GAME_CREATE: case P_GAME_DELETE:
    case P_GAME_SELECT: case P_GAME_ENTER:
        return 1;
    default:
        return 0;
    }
}

void openmmo_client_pump(openmmo_client *c)
{
    if (!c || c->ph == P_IDLE || c->ph == P_FAILED || c->ph == P_DEAD)
        return;

    mmo_net_pump(&c->net);

    /* A connecting phase completes when the socket reaches CONNECTED. */
    if (c->ph == P_LOGIN_CONNECT || c->ph == P_GAME_CONNECT) {
        if (c->net.state == MMO_NET_CONNECTED) {
            begin_handshake(c,
                            c->ph == P_LOGIN_CONNECT ? P_LOGIN_HELLO : P_GAME_HELLO,
                            c->ph == P_LOGIN_CONNECT ? OPENMMO_HANDSHAKING
                                                     : OPENMMO_JOINING_GAME);
        } else if (c->net.state == MMO_NET_ERROR || c->net.state == MMO_NET_CLOSED) {
            fail(c, c->net.errmsg[0] ? c->net.errmsg : "connect failed");
            return;
        }
    }

    /* A dead socket in any live phase ends the session as itself: a drop while
     * connecting/handshaking is a failure to establish; a drop once authed or
     * in-game is the close of a session that was up. */
    if (c->net.state == MMO_NET_CLOSED || c->net.state == MMO_NET_ERROR) {
        const char *why = c->net.errmsg[0] ? c->net.errmsg :
                          (c->net.state == MMO_NET_CLOSED ? "server closed the connection"
                                                          : "connection lost");
        if (c->ph == P_LOGIN_HELD || c->ph == P_CREATED || c->ph == P_GAME_PICK
            || c->ph == P_IN_GAME)
            disconnected(c, why);
        else if (c->ph != P_FAILED && c->ph != P_DEAD)
            fail(c, why);
        return;
    }

    /*
     * One ping every two seconds while a world session is up, and the next one only after the
     * last has been answered or given up on.
     */
    ping_tick(c);

    /* Consume every whole frame that has arrived, stepping the FSM per frame. */
    drain_rx(c);
    for (;;) {
        const u8 *body;
        size_t blen;
        mmo_frame_result fr = next_frame(c, &body, &blen);
        if (fr == MMO_FRAME_SHORT)
            break;
        if (fr == MMO_FRAME_BAD) {
            fail(c, "malformed frame from server");
            return;
        }
        phase before = c->ph;
        dispatch_frame(c, body, blen);
        if (c->ph == P_FAILED || c->ph == P_DEAD)
            return;
        /* A phase that connected out to a new socket has no more frames here. */
        if (c->ph == P_GAME_CONNECT && before != P_GAME_CONNECT)
            return;
    }

    /* Advance the presence model one frame and surface its render events. Ticked
     * here, once per pump, so remote avatars keep interpolating between the sparse
     * server updates rather than only on the frames a packet happens to arrive. */
    if (c->ph == P_IN_GAME)
        tick_entities(c);

    /* A wait that has outlived its budget on a silent-but-live link fails loudly
     * rather than hanging forever. */
    if (is_wait_phase(c->ph) && (time_t)mmo_plat_seconds() > c->deadline)
        fail(c, "timed out waiting for the server");
}

int openmmo_client_poll_event(openmmo_client *c, openmmo_event *ev)
{
    if (!c || c->evq_head == c->evq_tail)
        return 0;
    *ev = c->evq[c->evq_head];
    c->evq_head = (c->evq_head + 1) % EVQ_CAP;
    return 1;
}

int openmmo_client_chat(const openmmo_client *c, openmmo_event *out, int max_out)
{
    int n, i;

    if (!c || !out || max_out <= 0)
        return 0;
    n = c->chat_count;
    if (n > max_out)
        n = max_out;
    for (i = 0; i < n; i++)
        out[i] = c->chat_log[(c->chat_head + i) % OPENMMO_CHAT_LOG];
    return n;
}

openmmo_status openmmo_client_status(const openmmo_client *c)
{
    return c ? c->status : OPENMMO_DISCONNECTED;
}

const char *openmmo_status_name(openmmo_status s)
{
    switch (s) {
    case OPENMMO_DISCONNECTED:    return "disconnected";
    case OPENMMO_CONNECTING:      return "connecting";
    case OPENMMO_HANDSHAKING:     return "handshaking";
    case OPENMMO_AUTHENTICATING:  return "authenticating";
    case OPENMMO_AUTHED:          return "authed";
    case OPENMMO_REQUESTING_GAME: return "requesting game server";
    case OPENMMO_JOINING_GAME:    return "joining game";
    case OPENMMO_IN_GAME:         return "in game";
    case OPENMMO_FAILED:          return "failed";
    }
    return "?";
}
