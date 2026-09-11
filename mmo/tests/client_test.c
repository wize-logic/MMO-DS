/* The frame-driven session FSM (client.h). */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

#include "client.h"
#include "codec.h"
#include "session.h"

static int failures;

#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) {                                                             \
            printf("  ok   %s\n", msg);                                         \
        } else {                                                                \
            printf("  FAIL %s\n", msg);                                         \
            failures++;                                                         \
        }                                                                       \
    } while (0)

/*
 * The point captured live from the login server, with a signature minted by openssl over what
 * a ServerHello signature covers, the point, the checksum size and the client's hello
 * timestamp, under the key the client pins.
 */
static const char *SERVER_HELLO_BODY =
    "014100040c38ac53e0f2f0a801f2303e40975ea1dc2a86527fd4e3593fc80d999"
    "e22e5a60557ae5afedb1d820fef8d2e06ec6db3dd1050b7a77057dee7b9829dd4"
    "fe85f747003045022100d84392457183782c259463a1ed49ef3c6a1ac30adbe89"
    "21f6067b63305d5c2a902205ec135670fd02bb9ddb7f3bf0d55e44643abae18d5"
    "d6f02d3eeb80105589e76910";

/* The hello timestamp that vector was signed for. A client that says anything
 * else refuses it, which is the whole point of the timestamp being in there. */
#define HELLO_TS ((s64)1700000000000LL)

static size_t unhex(const char *hex, u8 *out, size_t cap)
{
    size_t n = 0;
    for (const char *p = hex; p[0] && p[1] && n < cap; p += 2) {
        unsigned v;
        sscanf(p, "%2x", &v);
        out[n++] = (u8)v;
    }
    return n;
}

/* A login-hold config. There is no address in one: attach_fd is handed a
 * socket, and a client that connects for itself dials the built-in server. */
static openmmo_config login_cfg(void)
{
    openmmo_config cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.user = "test";
    cfg.pass = "test";
    cfg.mode = OPENMMO_MODE_LOGIN_HOLD;
    return cfg;
}

/* Wire a client's login stream to sv[1] and return the peer end sv[0], set
 * non-blocking so the test's own reads never park. */
static int attach_pair(openmmo_client *c, int *peer)
{
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0)
        return -1;
    openmmo_config cfg = login_cfg();
    /* Before the attach: the handshake begins there, and the ServerHello below
     * was signed for one hello timestamp. */
    openmmo_client_pin_hello_time(c, HELLO_TS);
    if (openmmo_client_attach_fd(c, sv[1], &cfg) != 0) {
        close(sv[0]);
        close(sv[1]);
        return -1;
    }
    *peer = sv[0];
    return 0;
}

/* Pump n times, giving the socketpair a moment to become readable each time. */
static void pump_n(openmmo_client *c, int n)
{
    for (int i = 0; i < n; i++) {
        openmmo_client_pump(c);
        usleep(1000);
    }
}

/* Drain and tally events; returns whether a FAILED event was seen and copies its
 * message out. */
static int saw_failed(openmmo_client *c, char *msg, size_t cap)
{
    int seen = 0;
    openmmo_event ev;
    while (openmmo_client_poll_event(c, &ev)) {
        if (ev.kind == OPENMMO_EV_FAILED) {
            seen = 1;
            snprintf(msg, cap, "%s", ev.message);
        }
    }
    return seen;
}

/* Read whatever the peer has, non-blocking, into buf. */
static size_t read_peer(int peer, u8 *buf, size_t cap)
{
    ssize_t r = recv(peer, buf, cap, MSG_DONTWAIT);
    return r > 0 ? (size_t)r : 0;
}

/* The FSM kicks off the handshake on attach: the first pump flushes a framed
 * ClientHello, and the client is HANDSHAKING. */
static void test_handshake_kickoff(void)
{
    printf("handshake kickoff:\n");
    openmmo_client *c = openmmo_client_new();
    int peer;
    if (!c || attach_pair(c, &peer) != 0) { CHECK(0, "socketpair"); return; }

    pump_n(c, 3);
    u8 buf[256];
    size_t got = read_peer(peer, buf, sizeof buf);

    mmo_rbuf r;
    mmo_rbuf_init(&r, buf, got);
    const u8 *body;
    size_t blen;
    mmo_frame_result fr = mmo_frame_get(&r, &body, &blen);
    CHECK(fr == MMO_FRAME_OK && blen >= 1 && body[0] == MMO_HS_CLIENT_HELLO,
          "first pump flushes a framed ClientHello");
    CHECK(openmmo_client_status(c) == OPENMMO_HANDSHAKING,
          "status is HANDSHAKING after kickoff");

    close(peer);
    openmmo_client_free(c);
}

/* A frame split across two writes is reassembled: the client waits on the first
 * half (no premature failure) and only acts once the whole frame has arrived.
 * The ServerHello here carries a tampered signature, so acting means failing, 
 * loudly, with a message, never a hang. */
static void test_reassembly_and_bad_signature(void)
{
    printf("reassembly + bad-signature handshake:\n");
    openmmo_client *c = openmmo_client_new();
    int peer;
    if (!c || attach_pair(c, &peer) != 0) { CHECK(0, "socketpair"); return; }
    pump_n(c, 2); /* flush ClientHello */

    u8 body[256];
    size_t blen = unhex(SERVER_HELLO_BODY, body, sizeof body);
    body[blen - 20] ^= 0xff; /* flip a byte inside the DER signature */

    mmo_wbuf frame;
    mmo_wbuf_init(&frame);
    mmo_frame_put(&frame, body, blen);

    size_t half = frame.len / 2;
    if (write(peer, frame.data, half) != (ssize_t)half) { CHECK(0, "write half"); }
    pump_n(c, 3);
    char msg[128] = {0};
    CHECK(!saw_failed(c, msg, sizeof msg) && openmmo_client_status(c) == OPENMMO_HANDSHAKING,
          "half a ServerHello frame: still HANDSHAKING, no premature failure");

    if (write(peer, frame.data + half, frame.len - half) != (ssize_t)(frame.len - half))
        { CHECK(0, "write rest"); }
    pump_n(c, 3);
    int failed = saw_failed(c, msg, sizeof msg);
    CHECK(failed && openmmo_client_status(c) == OPENMMO_FAILED,
          "completed frame with a bad signature -> FAILED, with a reason");

    mmo_wbuf_free(&frame);
    close(peer);
    openmmo_client_free(c);
}

/* A link dropped mid-handshake is surfaced as a failure at once, not a timeout,
 * not a hang. This is the 2.10 discipline, now at the API boundary. */
static void test_drop_not_hang(void)
{
    printf("dropped link mid-handshake:\n");
    openmmo_client *c = openmmo_client_new();
    int peer;
    if (!c || attach_pair(c, &peer) != 0) { CHECK(0, "socketpair"); return; }
    pump_n(c, 2);

    close(peer); /* peer vanishes before any ServerHello */
    pump_n(c, 5);

    char msg[128] = {0};
    CHECK(saw_failed(c, msg, sizeof msg) && openmmo_client_status(c) == OPENMMO_FAILED,
          "a dropped link fails the session promptly");

    openmmo_client_free(c);
}

/* A valid, whole ServerHello drives the crypto swap: the client verifies it,
 * installs the session, sends ClientReady, and advances to AUTHENTICATING with
 * the encrypted LoginRequest on the wire. (The reply cannot be checked offline, 
 * that needs the server's key, so the test stops at AUTHENTICATING.) */
static void test_valid_hello_advances_to_auth(void)
{
    printf("valid ServerHello -> AUTHENTICATING:\n");
    openmmo_client *c = openmmo_client_new();
    int peer;
    if (!c || attach_pair(c, &peer) != 0) { CHECK(0, "socketpair"); return; }
    pump_n(c, 2);

    u8 hello[256];
    size_t blen = unhex(SERVER_HELLO_BODY, hello, sizeof hello);
    mmo_wbuf frame;
    mmo_wbuf_init(&frame);
    mmo_frame_put(&frame, hello, blen);
    if (write(peer, frame.data, frame.len) != (ssize_t)frame.len) { CHECK(0, "write hello"); }
    mmo_wbuf_free(&frame);

    pump_n(c, 5);
    CHECK(openmmo_client_status(c) == OPENMMO_AUTHENTICATING,
          "verified ServerHello advances the FSM to AUTHENTICATING");

    /* The peer should have received a ClientReady (0x02) after the ClientHello. */
    u8 buf[1024];
    size_t got = read_peer(peer, buf, sizeof buf);
    int saw_ready = 0;
    mmo_rbuf r;
    mmo_rbuf_init(&r, buf, got);
    const u8 *fb;
    size_t fn;
    while (mmo_frame_get(&r, &fb, &fn) == MMO_FRAME_OK)
        if (fn >= 1 && fb[0] == MMO_HS_CLIENT_READY)
            saw_ready = 1;
    CHECK(saw_ready, "ClientReady was sent after the ServerHello");

    close(peer);
    openmmo_client_free(c);
}

/*
 * The self-tile getter reports the local player's authoritative spawn tile, which only exists
 * once the join burst has placed the player.
 */
static void test_self_tile_unknown_before_join(void)
{
    printf("self tile is unknown before joining a map:\n");
    openmmo_client *c = openmmo_client_new();
    if (!c) { CHECK(0, "alloc"); return; }
    int x = 4242, z = 2424;
    CHECK(openmmo_client_self_tile(c, &x, &z) == -1,
          "a fresh client has no self tile");
    CHECK(x == 4242 && z == 2424, "the out-params are left untouched when unknown");
    CHECK(openmmo_client_self_tile(c, NULL, NULL) == -1,
          "NULL out-params are accepted");
    openmmo_client_free(c);
}

/*
 * A client whose link breaks is restarted, not replaced: the same object goes back into
 * openmmo_client_start, because the modules attached to it hold its pointer.
 */
static void test_restart_hangs_up_the_old_socket(void)
{
    printf("a restart hangs up the session it replaces:\n");
    openmmo_client *c = openmmo_client_new();
    int peer;
    if (!c || attach_pair(c, &peer) != 0) { CHECK(0, "socketpair"); return; }
    pump_n(c, 2); /* flush ClientHello */

    /* End the session with the peer still up, the way a refusal does. */
    u8 body[256];
    size_t blen = unhex(SERVER_HELLO_BODY, body, sizeof body);
    body[blen - 20] ^= 0xff; /* flip a byte inside the DER signature */
    mmo_wbuf frame;
    mmo_wbuf_init(&frame);
    mmo_frame_put(&frame, body, blen);
    if (write(peer, frame.data, frame.len) != (ssize_t)frame.len) { CHECK(0, "write hello"); }
    mmo_wbuf_free(&frame);
    pump_n(c, 3);
    char msg[128] = {0};
    CHECK(saw_failed(c, msg, sizeof msg) && openmmo_client_status(c) == OPENMMO_FAILED,
          "the session ended with the peer still connected");

    /* Drain what the client already sent, so a later read of nothing is the
     * hang-up rather than a buffer that had not been emptied. */
    u8 drain[1024];
    while (read_peer(peer, drain, sizeof drain) > 0)
        ;

    openmmo_config cfg = login_cfg();
    (void)openmmo_client_start(c, &cfg);
    CHECK(recv(peer, drain, sizeof drain, MSG_DONTWAIT) == 0,
          "the restart closed the old socket: the peer reads EOF");

    close(peer);
    openmmo_client_free(c);
}

/*
 * What a redialling caller reads to tell a server that is down, worth another attempt,
 * from one that is up and has said no. The positive path needs the server's key (the
 * LoginResponse is encrypted), so it belongs to the live oracle on 2106; the negative one is
 * what decides whether a broken link is retried at all, and it is checkable here.
 */
static void test_no_login_refusal_until_the_server_sends_one(void)
{
    printf("no login refusal until one arrives:\n");
    openmmo_client *c = openmmo_client_new();
    int peer;
    if (!c) { CHECK(0, "alloc"); return; }
    CHECK(openmmo_client_login_refusal(c) == -1,
          "a client that has not signed in has not been refused");
    CHECK(openmmo_client_login_refusal(NULL) == -1,
          "no client at all is answered rather than dereferenced");

    if (attach_pair(c, &peer) != 0) { CHECK(0, "socketpair"); openmmo_client_free(c); return; }
    pump_n(c, 2);
    close(peer); /* the link dies before any LoginResponse */
    pump_n(c, 5);
    char msg[128] = {0};
    CHECK(saw_failed(c, msg, sizeof msg) && openmmo_client_login_refusal(c) == -1,
          "a dropped link fails without looking like a refusal");

    openmmo_client_free(c);
}

int client_tests_run(void)
{
    failures = 0;
    test_handshake_kickoff();
    test_reassembly_and_bad_signature();
    test_drop_not_hang();
    test_valid_hello_advances_to_auth();
    test_self_tile_unknown_before_join();
    test_restart_hangs_up_the_old_socket();
    test_no_login_refusal_until_the_server_sends_one();
    return failures;
}
