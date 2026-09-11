/* The typed one-frame reader (mmo_net_read_frame). */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

#include "net.h"
#include "codec.h"

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

/* A connected AF_UNIX stream pair: sv[0] is the peer we write raw bytes to,
 * sv[1] is adopted by `n` as the client end. */
static int make_pair(mmo_net *n, int *peer)
{
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0)
        return -1;
    *peer = sv[0];
    mmo_net_attach(n, sv[1]);
    return 0;
}

/* Frame a body the way the server would (u16-le length including itself). */
static void framed(mmo_wbuf *w, const void *body, size_t n)
{
    mmo_wbuf_init(w);
    mmo_frame_put(w, body, n);
}

static void test_ok_one_frame(void)
{
    printf("read one whole frame:\n");
    mmo_net n;
    int peer;
    if (make_pair(&n, &peer) != 0) { CHECK(0, "socketpair"); return; }

    mmo_wbuf f;
    framed(&f, "\xAA\xBB\xCC", 3);
    ssize_t w = write(peer, f.data, f.len);
    CHECK(w == (ssize_t)f.len, "peer wrote a full frame");
    mmo_wbuf_free(&f);

    mmo_buf acc;
    memset(&acc, 0, sizeof acc);
    const u8 *body;
    size_t blen;
    mmo_read_result rr = mmo_net_read_frame(&n, &acc, &body, &blen, 50, 1000);
    CHECK(rr == MMO_READ_OK, "result is MMO_READ_OK");
    CHECK(blen == 3 && body && memcmp(body, "\xAA\xBB\xCC", 3) == 0,
          "frame body is the three payload bytes");
    mmo_net_read_reset(&acc);
    close(peer);
    mmo_net_close(&n);
}

static void test_two_frames_back_to_back(void)
{
    printf("read two frames from one buffer:\n");
    mmo_net n;
    int peer;
    if (make_pair(&n, &peer) != 0) { CHECK(0, "socketpair"); return; }

    mmo_wbuf a, b;
    framed(&a, "\x01\x02", 2);
    framed(&b, "\x03\x04\x05", 3);
    write(peer, a.data, a.len);
    write(peer, b.data, b.len);
    mmo_wbuf_free(&a);
    mmo_wbuf_free(&b);

    mmo_buf acc;
    memset(&acc, 0, sizeof acc);
    const u8 *body;
    size_t blen;
    mmo_read_result rr = mmo_net_read_frame(&n, &acc, &body, &blen, 50, 1000);
    CHECK(rr == MMO_READ_OK && blen == 2 && memcmp(body, "\x01\x02", 2) == 0,
          "first read yields the first frame");
    rr = mmo_net_read_frame(&n, &acc, &body, &blen, 50, 1000);
    CHECK(rr == MMO_READ_OK && blen == 3 && memcmp(body, "\x03\x04\x05", 3) == 0,
          "second read yields the second frame from the same accumulator");
    mmo_net_read_reset(&acc);
    close(peer);
    mmo_net_close(&n);
}

static void test_dropped_before_frame(void)
{
    printf("dropped link is not a timeout:\n");
    mmo_net n;
    int peer;
    if (make_pair(&n, &peer) != 0) { CHECK(0, "socketpair"); return; }

    /* Half a frame, then close: the length prefix promises 5 bytes but only 3
     * arrive, so the reader must wait, and then see EOF. */
    u8 partial[3] = { 0x05, 0x00, 0xAA };
    write(peer, partial, sizeof partial);
    close(peer); /* drop the link */

    mmo_buf acc;
    memset(&acc, 0, sizeof acc);
    const u8 *body;
    size_t blen;
    mmo_read_result rr = mmo_net_read_frame(&n, &acc, &body, &blen, 50, 1000);
    CHECK(rr == MMO_READ_DROPPED, "result is MMO_READ_DROPPED, not TIMEOUT");
    CHECK(n.state == MMO_NET_CLOSED, "net state reflects the clean peer close");
    mmo_net_read_reset(&acc);
    mmo_net_close(&n);
}

static void test_timeout_when_silent(void)
{
    printf("silent but live link times out:\n");
    mmo_net n;
    int peer;
    if (make_pair(&n, &peer) != 0) { CHECK(0, "socketpair"); return; }

    /* Peer stays open and sends nothing: a small budget must expire cleanly. */
    mmo_buf acc;
    memset(&acc, 0, sizeof acc);
    const u8 *body;
    size_t blen;
    mmo_read_result rr = mmo_net_read_frame(&n, &acc, &body, &blen, 5, 1000);
    CHECK(rr == MMO_READ_TIMEOUT, "result is MMO_READ_TIMEOUT");
    CHECK(n.state == MMO_NET_CONNECTED, "link is still connected after the timeout");
    mmo_net_read_reset(&acc);
    close(peer);
    mmo_net_close(&n);
}

static void test_malformed_frame(void)
{
    printf("malformed length prefix is rejected:\n");
    mmo_net n;
    int peer;
    if (make_pair(&n, &peer) != 0) { CHECK(0, "socketpair"); return; }

    /* A length field of 1 is < 2 (it must include its own two bytes): malformed. */
    u8 bad[2] = { 0x01, 0x00 };
    write(peer, bad, sizeof bad);

    mmo_buf acc;
    memset(&acc, 0, sizeof acc);
    const u8 *body;
    size_t blen;
    mmo_read_result rr = mmo_net_read_frame(&n, &acc, &body, &blen, 50, 1000);
    CHECK(rr == MMO_READ_BAD, "result is MMO_READ_BAD");
    mmo_net_read_reset(&acc);
    close(peer);
    mmo_net_close(&n);
}

/* What a send does and does not do, and what a close does to what it did not. */
static void test_send_needs_a_drain(void)
{
    printf("a queued send reaches the peer only if it is drained:\n");
    static const u8 msg[] = "the last report";
    u8 got[64];
    mmo_net n;
    int peer;
    ssize_t r;

    if (make_pair(&n, &peer) != 0) { CHECK(0, "socketpair"); return; }
    CHECK(mmo_net_send(&n, msg, sizeof msg) == 0, "send accepts the bytes");
    CHECK(mmo_net_pending(&n) == sizeof msg, "and they are queued, not sent");
    mmo_net_close(&n);
    r = recv(peer, got, sizeof got, MSG_DONTWAIT);
    CHECK(r == 0, "closing without a drain loses them: the peer reads EOF");
    close(peer);

    if (make_pair(&n, &peer) != 0) { CHECK(0, "socketpair"); return; }
    CHECK(mmo_net_send(&n, msg, sizeof msg) == 0, "send accepts them again");
    CHECK(mmo_net_drain(&n, 250) == 0, "the drain empties the queue");
    mmo_net_close(&n);
    r = recv(peer, got, sizeof got, MSG_DONTWAIT);
    CHECK(r == (ssize_t)sizeof msg && memcmp(got, msg, sizeof msg) == 0,
          "and the peer reads exactly what was sent");
    r = recv(peer, got, sizeof got, MSG_DONTWAIT);
    CHECK(r == 0, "then end of stream, with nothing else invented");
    close(peer);
}

/* A peer that has gone ends the wait rather than serving it out. */
static void test_drain_gives_up_on_a_dead_peer(void)
{
    printf("a drain onto a dead peer gives up and says what is left:\n");
    static const u8 msg[] = "the last report";
    mmo_net n;
    int peer;

    if (make_pair(&n, &peer) != 0) { CHECK(0, "socketpair"); return; }
    CHECK(mmo_net_send(&n, msg, sizeof msg) == 0, "queued while the link was live");
    close(peer);
    CHECK(mmo_net_drain(&n, 5000) == sizeof msg,
          "the drain returns the bytes that could not go");
    mmo_net_close(&n);
    CHECK(mmo_net_pending(&n) == 0, "and a closed link has nothing queued");
}

int net_tests_run(void)
{
    failures = 0;
    test_ok_one_frame();
    test_two_frames_back_to_back();
    test_dropped_before_frame();
    test_timeout_when_silent();
    test_malformed_frame();
    test_send_needs_a_drain();
    test_drain_gives_up_on_a_dead_peer();
    if (failures == 0)
        printf("net: all checks passed\n");
    return failures;
}
