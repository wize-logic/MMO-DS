/* The pinned-key mock server double (mockserver.h). */
#include <stdio.h>
#include <string.h>

#include "mockserver.h"
#include "session.h"
#include "codec.h"
#include "p256.h"

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

/* Pull the single frame body out of a framed wbuf. Returns 0 and points body/n
 * at it, or -1 if the buffer does not hold one whole frame. */
static int one_frame(const mmo_wbuf *w, const u8 **body, size_t *n)
{
    mmo_rbuf r;
    mmo_rbuf_init(&r, w->data, w->len);
    return mmo_frame_get(&r, body, n) == MMO_FRAME_OK ? 0 : -1;
}

/* The frozen ServerHello signature verifies under the test root with our own
 * verifier, the generator's output is self-checking, and a bad regeneration
 * fails here rather than mid-handshake. */
static void test_signature_oracle(void)
{
    printf("frozen ServerHello signature verifies under the test root:\n");
    mmo_mock_server m;
    mmo_mock_server_init(&m, 16);

    mmo_wbuf hello;
    mmo_wbuf_init(&hello);
    CHECK(mmo_mock_server_hello(&m, &hello, MMO_MOCK_HELLO_TS) == 0,
          "the mock has a signature for this profile and timestamp");

    const u8 *body;
    size_t n;
    CHECK(one_frame(&hello, &body, &n) == 0, "ServerHello is one whole frame");

    mmo_rbuf r;
    mmo_rbuf_init(&r, body, n);
    mmo_server_hello sh;
    CHECK(mmo_hs_read_server_hello(&r, &sh) == 0, "ServerHello parses");
    u8 signed_bytes[MMO_HS_SIGNED];
    mmo_hs_signed_bytes(sh.ephemeral_pub, sh.checksum_size, MMO_MOCK_HELLO_TS,
                        signed_bytes);
    CHECK(mmo_p256_ecdsa_verify(mmo_mock_root_pub, signed_bytes, sizeof signed_bytes,
                                sh.signature, sh.siglen) == 1,
          "signature verifies under mmo_mock_root_pub");
    CHECK(mmo_p256_ecdsa_verify(mmo_root_pubkey, signed_bytes, sizeof signed_bytes,
                                sh.signature, sh.siglen) == 0,
          "the same signature does NOT verify under the real pinned root");

    mmo_wbuf_free(&hello);
}

/* The mock cannot sign, so it can only answer the (profile, timestamp) pairs its
 * fixture was minted for. Anything else has to say so rather than hand out a
 * ServerHello signed for some other session. */
static void test_hello_needs_a_minted_fixture(void)
{
    printf("the mock refuses to answer what it was never signed for:\n");
    mmo_mock_server m;
    mmo_wbuf w;

    mmo_mock_server_init(&m, 16);
    mmo_wbuf_init(&w);
    CHECK(mmo_mock_server_hello(&m, &w, MMO_MOCK_HELLO_TS + 1) == -1 && w.len == 0,
          "another hello timestamp is refused, nothing written");
    mmo_wbuf_free(&w);

    mmo_mock_server_init(&m, 7);
    mmo_wbuf_init(&w);
    CHECK(mmo_mock_server_hello(&m, &w, MMO_MOCK_HELLO_TS) == -1 && w.len == 0,
          "a profile the fixture has no signature for is refused");
    mmo_wbuf_free(&w);
}

/* The ServerHello is constant for a given profile, a frozen byte string, the
 * property that makes the mock a stable fixture. */
static void test_hello_is_frozen(void)
{
    printf("ServerHello is byte-identical across sessions:\n");
    mmo_mock_server a, b;
    mmo_mock_server_init(&a, 16);
    mmo_mock_server_init(&b, 16);
    mmo_wbuf wa, wb;
    mmo_wbuf_init(&wa);
    mmo_wbuf_init(&wb);
    mmo_mock_server_hello(&a, &wa, MMO_MOCK_HELLO_TS);
    mmo_mock_server_hello(&b, &wb, MMO_MOCK_HELLO_TS);
    CHECK(wa.len == wb.len && memcmp(wa.data, wb.data, wa.len) == 0,
          "two mocks emit the identical ServerHello");
    mmo_wbuf_free(&wa);
    mmo_wbuf_free(&wb);
}

/* The headline: a real client session completes the handshake against the mock
 * and an encrypted app frame round-trips both ways, keys derived independently. */
static void test_full_handshake_and_app_roundtrip(void)
{
    printf("full handshake + encrypted app round-trip, offline:\n");

    mmo_mock_server m;
    mmo_mock_server_init(&m, 16);

    /* Script a reply: LoginRequest (0x11) -> LoginResponse (0x01) AUTHED. */
    const u8 authed[] = {0x00, 0xDE, 0xAD, 0xBE, 0xEF};
    CHECK(mmo_mock_server_reply(&m, 0x11, 0x01, authed, sizeof authed) == 0,
          "scripted a LoginResponse reply");

    /* Client: real session driver, pointed at the mock's test root. */
    mmo_session c;
    mmo_wbuf hello;
    mmo_wbuf_init(&hello);
    CHECK(mmo_session_start_at(&c, &hello, MMO_MOCK_HELLO_TS) == 0,
          "client sent ClientHello");
    c.root_pub = mmo_mock_root_pub; /* set AFTER start, which memsets it to NULL */

    /* Mock: hand out ServerHello; client verifies it and returns ClientReady. */
    mmo_wbuf shello;
    mmo_wbuf_init(&shello);
    mmo_mock_server_hello(&m, &shello, MMO_MOCK_HELLO_TS);
    const u8 *sh_body;
    size_t sh_n;
    one_frame(&shello, &sh_body, &sh_n);

    mmo_wbuf ready;
    mmo_wbuf_init(&ready);
    int rc = mmo_session_on_server_hello(&c, sh_body, sh_n, &ready);
    CHECK(rc == 0 && c.phase == MMO_SESS_ESTABLISHED,
          "client verified ServerHello and is ESTABLISHED");

    /* Mock consumes ClientReady and derives the mirror-role crypto. */
    const u8 *rd_body;
    size_t rd_n;
    one_frame(&ready, &rd_body, &rd_n);
    CHECK(mmo_mock_server_on_client_ready(&m, rd_body, rd_n) == 0 && m.established,
          "mock consumed ClientReady and is established");

    /* Client -> mock: an encrypted LoginRequest; mock -> client: the reply. */
    const u8 req_body[] = {'t', 'e', 's', 't'};
    mmo_wbuf req;
    mmo_wbuf_init(&req);
    mmo_session_send_app(&c.crypto, 0x11, req_body, sizeof req_body, &req);
    const u8 *req_fb;
    size_t req_fn;
    one_frame(&req, &req_fb, &req_fn);

    mmo_wbuf resp;
    mmo_wbuf_init(&resp);
    int op = mmo_mock_server_on_app(&m, req_fb, req_fn, &resp);
    CHECK(op == 0x11, "mock deciphered the request opcode (0x11)");
    CHECK(resp.len > 0, "mock emitted a scripted reply frame");

    /* Client deciphers the reply and it is exactly the scripted plaintext. */
    const u8 *resp_fb;
    size_t resp_fn;
    CHECK(one_frame(&resp, &resp_fb, &resp_fn) == 0, "reply is one whole frame");
    u8 plain[64];
    size_t plen = mmo_session_recv_app(&c.crypto, resp_fb, resp_fn,
                                       plain, sizeof plain);
    u8 want[1 + sizeof authed];
    want[0] = 0x01;
    memcpy(want + 1, authed, sizeof authed);
    CHECK(plen == sizeof want && memcmp(plain, want, plen) == 0,
          "client deciphered the reply to the exact scripted LoginResponse");

    mmo_wbuf_free(&hello);
    mmo_wbuf_free(&shello);
    mmo_wbuf_free(&ready);
    mmo_wbuf_free(&req);
    mmo_wbuf_free(&resp);
}

/* Fail-safe: a client on the default pinned root rejects the mock. The override
 * is opt-in, so the mock cannot stand in for the real server by accident. */
static void test_default_root_rejects_mock(void)
{
    printf("a client on the pinned root rejects the mock's ServerHello:\n");
    mmo_mock_server m;
    mmo_mock_server_init(&m, 16);
    mmo_wbuf shello;
    mmo_wbuf_init(&shello);
    mmo_mock_server_hello(&m, &shello, MMO_MOCK_HELLO_TS);
    const u8 *sh_body;
    size_t sh_n;
    one_frame(&shello, &sh_body, &sh_n);

    mmo_session c;
    mmo_wbuf hello;
    mmo_wbuf_init(&hello);
    mmo_session_start_at(&c, &hello, MMO_MOCK_HELLO_TS);
    /* root_pub left NULL: the pinned production key governs. */
    mmo_wbuf ready;
    mmo_wbuf_init(&ready);
    int rc = mmo_session_on_server_hello(&c, sh_body, sh_n, &ready);
    CHECK(rc == -1 && c.phase == MMO_SESS_FAILED,
          "the handshake fails under the pinned root");

    mmo_wbuf_free(&hello);
    mmo_wbuf_free(&shello);
    mmo_wbuf_free(&ready);
}

int mockserver_tests_run(void)
{
    failures = 0;
    test_signature_oracle();
    test_hello_needs_a_minted_fixture();
    test_hello_is_frozen();
    test_full_handshake_and_app_roundtrip();
    test_default_root_rejects_mock();
    if (failures)
        printf("mockserver: %d check(s) FAILED\n", failures);
    else
        printf("mockserver: all checks passed\n");
    return failures;
}
