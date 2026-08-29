/* Record/replay as a byte comparison (trace.h). */
#include <stdio.h>
#include <string.h>

#include "trace.h"
#include "mockserver.h"
#include "session.h"
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

/* A fixed, valid client ephemeral scalar so the captured trace is deterministic. */
static const u8 SEED_PRIV[MMO_P256_SCALAR] = {
    0xa1, 0xb2, 0xc3, 0xd4, 0xe5, 0xf6, 0x07, 0x18,
    0x29, 0x3a, 0x4b, 0x5c, 0x6d, 0x7e, 0x8f, 0x90,
    0x01, 0x12, 0x23, 0x34, 0x45, 0x56, 0x67, 0x78,
    0x89, 0x9a, 0xab, 0xbc, 0xcd, 0xde, 0xef, 0x02,
};
#define SEED_RANDOM ((s64)0x00c0ffee0badf00dLL)
#define SEED_TIME   ((s64)1700000000000LL)

static int one_frame(const mmo_wbuf *w, const u8 **body, size_t *n)
{
    mmo_rbuf r;
    mmo_rbuf_init(&r, w->data, w->len);
    return mmo_frame_get(&r, body, n) == MMO_FRAME_OK ? 0 : -1;
}

/* Drive the seeded client through the mock and capture every frame into `t`.
 * Returns 0 on a clean run. */
static int capture_mock_session(mmo_trace *t)
{
    mmo_mock_server m;
    mmo_mock_server_init(&m, 16);
    const u8 authed[] = {0x00, 0x00, 0x00}; /* state AUTHED + empty trailer */
    if (mmo_mock_server_reply(&m, 0x11, 0x01, authed, sizeof authed) != 0)
        return -1;

    mmo_session c;
    mmo_wbuf hello;
    mmo_wbuf_init(&hello);
    if (mmo_session_start_seeded(&c, &hello, SEED_PRIV, SEED_RANDOM, SEED_TIME) != 0)
        return -1;
    c.root_pub = mmo_mock_root_pub;

    mmo_trace_init(t, SEED_PRIV, SEED_RANDOM, SEED_TIME, 16, "mock login");
    mmo_trace_add(t, MMO_TRACE_C2S, MMO_TRACE_HS, hello.data, hello.len, NULL, 0);

    mmo_wbuf shello;
    mmo_wbuf_init(&shello);
    mmo_mock_server_hello(&m, &shello);
    mmo_trace_add(t, MMO_TRACE_S2C, MMO_TRACE_HS, shello.data, shello.len, NULL, 0);
    const u8 *sh_body;
    size_t sh_n;
    one_frame(&shello, &sh_body, &sh_n);

    mmo_wbuf ready;
    mmo_wbuf_init(&ready);
    int ok = mmo_session_on_server_hello(&c, sh_body, sh_n, &ready) == 0
             && c.phase == MMO_SESS_ESTABLISHED;
    mmo_trace_add(t, MMO_TRACE_C2S, MMO_TRACE_HS, ready.data, ready.len, NULL, 0);
    const u8 *rd_body;
    size_t rd_n;
    one_frame(&ready, &rd_body, &rd_n);
    ok = ok && mmo_mock_server_on_client_ready(&m, rd_body, rd_n) == 0;

    /* One app exchange: an encrypted LoginRequest and the scripted reply. */
    const u8 req_clear[] = {0x11, 't', 'e', 's', 't'};
    mmo_wbuf req;
    mmo_wbuf_init(&req);
    mmo_session_send_app(&c.crypto, req_clear[0], req_clear + 1,
                         sizeof req_clear - 1, &req);
    mmo_trace_add(t, MMO_TRACE_C2S, MMO_TRACE_APP, req.data, req.len,
                  req_clear, sizeof req_clear);
    const u8 *req_fb;
    size_t req_fn;
    one_frame(&req, &req_fb, &req_fn);

    mmo_wbuf resp;
    mmo_wbuf_init(&resp);
    ok = ok && mmo_mock_server_on_app(&m, req_fb, req_fn, &resp) == 0x11;
    u8 resp_clear[1 + sizeof authed];
    resp_clear[0] = 0x01;
    memcpy(resp_clear + 1, authed, sizeof authed);
    mmo_trace_add(t, MMO_TRACE_S2C, MMO_TRACE_APP, resp.data, resp.len,
                  resp_clear, sizeof resp_clear);

    /* Pin the rendered state derived from the S->C app frame(s). */
    mmo_trace_derive_renders(t);

    mmo_wbuf_free(&hello);
    mmo_wbuf_free(&shello);
    mmo_wbuf_free(&ready);
    mmo_wbuf_free(&req);
    mmo_wbuf_free(&resp);
    return ok ? 0 : -1;
}

/* The machinery, hermetic: capture -> serialize -> parse -> replay, byte-clean,
 * and both a serialize round-trip and a tamper check. */
static void test_capture_roundtrip_and_replay(void)
{
    printf("mock session captures, serializes, parses and replays byte-clean:\n");
    mmo_trace t;
    CHECK(capture_mock_session(&t) == 0, "seeded client ran the mock handshake");
    CHECK(t.nrecs == 5, "captured five frames");
    CHECK(t.nrenders == 1 && strcmp(t.renders[0], "login-state=0") == 0,
          "the capture pins the rendered login outcome (AUTHED)");

    char err[256] = {0};
    CHECK(mmo_trace_replay(&t, mmo_mock_root_pub, err, sizeof err) == 0,
          "the captured trace replays byte- and render-clean");
    if (err[0]) printf("       (%s)\n", err);

    /* Serialize, then parse the text back: the parsed trace must replay too. */
    mmo_wbuf text;
    mmo_wbuf_init(&text);
    CHECK(mmo_trace_serialize(&t, &text) == 0, "trace serializes");

    mmo_trace t2;
    err[0] = 0;
    CHECK(mmo_trace_parse((const char *)text.data, text.len, &t2, err, sizeof err) == 0,
          "the serialized text parses back");
    if (err[0]) printf("       (%s)\n", err);
    CHECK(t2.nrecs == t.nrecs &&
          memcmp(t2.eph_priv, t.eph_priv, MMO_P256_SCALAR) == 0 &&
          t2.hello_random == t.hello_random &&
          t2.hello_timestamp == t.hello_timestamp &&
          t2.checksum_size == t.checksum_size,
          "parsed trace matches the captured seed and record count");
    CHECK(t2.nrenders == t.nrenders && strcmp(t2.renders[0], t.renders[0]) == 0,
          "the render pins survive the serialize/parse round trip");
    err[0] = 0;
    CHECK(mmo_trace_replay(&t2, mmo_mock_root_pub, err, sizeof err) == 0,
          "the parsed trace replays byte-clean");
    if (err[0]) printf("       (%s)\n", err);

    /* Tamper: one flipped ciphertext byte in the inbound app frame must fail. */
    mmo_trace t3 = t;
    int last = t3.nrecs - 1; /* the S2C app record */
    t3.recs[last].wire[t3.recs[last].wirelen - 1] ^= 0x01;
    err[0] = 0;
    CHECK(mmo_trace_replay(&t3, mmo_mock_root_pub, err, sizeof err) != 0,
          "a flipped inbound ciphertext byte is caught");

    /* Tamper: one flipped cleartext byte in the outbound app frame must fail. */
    mmo_trace t4 = t;
    int reqrec = 3; /* the C2S app record */
    t4.recs[reqrec].clear[t4.recs[reqrec].clearlen - 1] ^= 0x01;
    err[0] = 0;
    CHECK(mmo_trace_replay(&t4, mmo_mock_root_pub, err, sizeof err) != 0,
          "a flipped outbound cleartext byte is caught");

    /* Tamper: a wrong rendered-state pin must fail even though every byte still
     * matches, the differential 5.5 adds over 5.4's byte comparison. */
    mmo_trace t5 = t;
    snprintf(t5.renders[0], sizeof t5.renders[0], "login-state=2");
    err[0] = 0;
    CHECK(mmo_trace_replay(&t5, mmo_mock_root_pub, err, sizeof err) != 0,
          "a wrong rendered-state pin is caught with the bytes intact");

    mmo_wbuf_free(&text);
}

/* The regression: replay the committed real-server capture under the pinned
 * production root. Located relative to this source file so the test is
 * independent of the working directory. */
static void test_recorded_login_replays(void)
{
    printf("the committed real-server login trace replays byte-clean:\n");

    /* __FILE__ is the absolute compile path .../mmo/tests/trace_test.c; swap the
     * trailing tests/<file> for traces/login-admin.trace. */
    const char *self = __FILE__;
    const char *marker = "/tests/trace_test.c";
    const char *at = strstr(self, marker);
    char path[1024];
    if (at) {
        size_t pre = (size_t)(at - self);
        snprintf(path, sizeof path, "%.*s/traces/login-admin.trace",
                 (int)pre, self);
    } else {
        snprintf(path, sizeof path, "mmo/traces/login-admin.trace");
    }

    char err[256] = {0};
    mmo_trace t;
    int prc = mmo_trace_parse_file(path, &t, err, sizeof err);
    CHECK(prc == 0, "the committed trace parses");
    if (prc != 0) printf("       (%s: %s)\n", path, err);

    CHECK(prc == 0 && t.nrenders == 1 &&
          strcmp(t.renders[0], "login-state=0") == 0,
          "it pins the AUTHED login as rendered state");

    err[0] = 0;
    int rc = mmo_trace_replay(&t, NULL, err, sizeof err);
    CHECK(rc == 0, "it replays byte- and render-clean under the pinned root");
    if (rc != 0) printf("       (%s: %s)\n", path, err);
}

int trace_tests_run(void)
{
    failures = 0;
    test_capture_roundtrip_and_replay();
    test_recorded_login_replays();
    if (failures)
        printf("trace: %d check(s) FAILED\n", failures);
    else
        printf("trace: all checks passed\n");
    return failures;
}
