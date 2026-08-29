/*
 * Record/replay of a transport session as a byte comparison. See trace.h for what
 * the mechanism is for; this file owns the text format, the parser and the replay driver.
 */
#include "trace.h"

#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "session.h"
#include "login.h"

/* --- hex --------------------------------------------------------------- */

static void hex_encode(mmo_wbuf *out, const u8 *b, size_t n)
{
    static const char d[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        char pair[2] = { d[b[i] >> 4], d[b[i] & 0x0F] };
        mmo_put_bytes(out, pair, 2);
    }
}

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Decode a hex token into buf (capacity cap). Returns the byte count, or -1 on a
 * bad digit, an odd length, or an overflow. An empty token decodes to 0 bytes. */
static long hex_decode(const char *s, size_t slen, u8 *buf, size_t cap)
{
    if (slen % 2 != 0)
        return -1;
    size_t n = slen / 2;
    if (n > cap)
        return -1;
    for (size_t i = 0; i < n; i++) {
        int hi = hex_nibble(s[2 * i]);
        int lo = hex_nibble(s[2 * i + 1]);
        if (hi < 0 || lo < 0)
            return -1;
        buf[i] = (u8)((hi << 4) | lo);
    }
    return (long)n;
}

/* --- build ------------------------------------------------------------- */

void mmo_trace_init(mmo_trace *t, const u8 eph_priv[MMO_P256_SCALAR],
                    s64 random, s64 timestamp, u8 checksum_size,
                    const char *label)
{
    memset(t, 0, sizeof *t);
    memcpy(t->eph_priv, eph_priv, MMO_P256_SCALAR);
    t->hello_random = random;
    t->hello_timestamp = timestamp;
    t->checksum_size = checksum_size;
    if (label)
        snprintf(t->label, sizeof t->label, "%s", label);
}

int mmo_trace_add(mmo_trace *t, mmo_trace_dir dir, mmo_trace_kind kind,
                  const u8 *wire, size_t wirelen,
                  const u8 *clear, size_t clearlen)
{
    if (t->nrecs >= MMO_TRACE_MAX_RECS)
        return -1;
    if (wirelen > MMO_TRACE_MAX_FRAME || clearlen > MMO_TRACE_MAX_FRAME)
        return -1;
    if (kind == MMO_TRACE_APP && (clear == NULL))
        return -1;
    mmo_trace_rec *r = &t->recs[t->nrecs++];
    r->dir = dir;
    r->kind = kind;
    r->wirelen = wirelen;
    memcpy(r->wire, wire, wirelen);
    if (kind == MMO_TRACE_APP) {
        r->clearlen = clearlen;
        memcpy(r->clear, clear, clearlen);
    } else {
        r->clearlen = 0;
    }
    return 0;
}

/* --- rendered state ---------------------------------------------------- */

int mmo_trace_render_app(const u8 *clear, size_t n, char *out, size_t cap)
{
    if (n < 1)
        return -1;
    u8 op = clear[0];
    const u8 *body = clear + 1;
    size_t blen = n - 1;
    switch (op) {
    case MMO_LOGIN_OP_RESPONSE: {   /* 0x01, the login outcome the client shows */
        int state = mmo_login_read_response_state(body, blen);
        snprintf(out, cap, "login-state=%d", state);
        return 0;
    }
    default:
        /* An opcode whose rendered meaning the differential does not yet pin: the
         * frame is left unpinned rather than described by a guess. */
        return -1;
    }
}

void mmo_trace_derive_renders(mmo_trace *t)
{
    t->nrenders = 0;
    for (int i = 0; i < t->nrecs; i++) {
        const mmo_trace_rec *r = &t->recs[i];
        if (r->dir != MMO_TRACE_S2C || r->kind != MMO_TRACE_APP)
            continue;
        char obs[MMO_TRACE_MAX_RENDER];
        if (mmo_trace_render_app(r->clear, r->clearlen, obs, sizeof obs) != 0)
            continue;
        if (t->nrenders < MMO_TRACE_MAX_RECS)
            snprintf(t->renders[t->nrenders++], MMO_TRACE_MAX_RENDER, "%s", obs);
    }
}

/* --- serialize --------------------------------------------------------- */

int mmo_trace_serialize(const mmo_trace *t, mmo_wbuf *out)
{
    char line[128];
    const char *hdr = "# openmmo session trace v1\n";
    mmo_put_bytes(out, hdr, strlen(hdr));
    if (t->label[0]) {
        int n = snprintf(line, sizeof line, "# label: %s\n", t->label);
        mmo_put_bytes(out, line, (size_t)n);
    }
    mmo_put_bytes(out, "seed-priv ", strlen("seed-priv "));
    hex_encode(out, t->eph_priv, MMO_P256_SCALAR);
    mmo_put_bytes(out, "\n", 1);

    int n = snprintf(line, sizeof line, "seed-random %" PRId64 "\n",
                     (int64_t)t->hello_random);
    mmo_put_bytes(out, line, (size_t)n);
    n = snprintf(line, sizeof line, "seed-timestamp %" PRId64 "\n",
                 (int64_t)t->hello_timestamp);
    mmo_put_bytes(out, line, (size_t)n);
    n = snprintf(line, sizeof line, "checksum %u\n", (unsigned)t->checksum_size);
    mmo_put_bytes(out, line, (size_t)n);

    for (int i = 0; i < t->nrecs; i++) {
        const mmo_trace_rec *r = &t->recs[i];
        const char *dir = r->dir == MMO_TRACE_C2S ? "c2s" : "s2c";
        const char *kind = r->kind == MMO_TRACE_HS ? "hs" : "app";
        n = snprintf(line, sizeof line, "%s %s ", dir, kind);
        mmo_put_bytes(out, line, (size_t)n);
        hex_encode(out, r->wire, r->wirelen);
        if (r->kind == MMO_TRACE_APP) {
            mmo_put_bytes(out, " ", 1);
            hex_encode(out, r->clear, r->clearlen);
        }
        mmo_put_bytes(out, "\n", 1);
    }
    for (int i = 0; i < t->nrenders; i++) {
        n = snprintf(line, sizeof line, "render %s\n", t->renders[i]);
        mmo_put_bytes(out, line, (size_t)n);
    }
    return out->err ? -1 : 0;
}

/* --- parse ------------------------------------------------------------- */

/* Point *tok/*toklen at the next whitespace-delimited token from *p (bounded by
 * end), advancing *p past it. Returns 0, or -1 if none remains. */
static int next_token(const char **p, const char *end,
                      const char **tok, size_t *toklen)
{
    const char *s = *p;
    while (s < end && (*s == ' ' || *s == '\t'))
        s++;
    if (s >= end)
        return -1;
    const char *t = s;
    while (s < end && *s != ' ' && *s != '\t')
        s++;
    *tok = t;
    *toklen = (size_t)(s - t);
    *p = s;
    return 0;
}

static int tok_eq(const char *tok, size_t toklen, const char *lit)
{
    return toklen == strlen(lit) && memcmp(tok, lit, toklen) == 0;
}

#define TRACE_FAIL(...)                                                         \
    do {                                                                       \
        if (err) snprintf(err, errcap, __VA_ARGS__);                           \
        return -1;                                                             \
    } while (0)

int mmo_trace_parse(const char *text, size_t n, mmo_trace *out,
                    char *err, size_t errcap)
{
    memset(out, 0, sizeof *out);
    int have_seed = 0, have_checksum = 0;

    const char *p = text, *end = text + n;
    int lineno = 0;
    while (p < end) {
        lineno++;
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        const char *le = nl ? nl : end;
        const char *lp = p;
        p = nl ? nl + 1 : end;

        const char *tok;
        size_t tl;
        if (next_token(&lp, le, &tok, &tl) != 0)
            continue;                 /* blank line */
        if (tok[0] == '#')
            continue;                 /* comment */

        if (tok_eq(tok, tl, "seed-priv")) {
            const char *h;
            size_t hl;
            if (next_token(&lp, le, &h, &hl) != 0)
                TRACE_FAIL("line %d: seed-priv missing value", lineno);
            if (hex_decode(h, hl, out->eph_priv, MMO_P256_SCALAR) != MMO_P256_SCALAR)
                TRACE_FAIL("line %d: seed-priv must be %d hex bytes",
                           lineno, MMO_P256_SCALAR);
            have_seed |= 1;
        } else if (tok_eq(tok, tl, "seed-random")) {
            const char *v;
            size_t vl;
            if (next_token(&lp, le, &v, &vl) != 0)
                TRACE_FAIL("line %d: seed-random missing value", lineno);
            out->hello_random = (s64)strtoll(v, NULL, 10);
            have_seed |= 2;
        } else if (tok_eq(tok, tl, "seed-timestamp")) {
            const char *v;
            size_t vl;
            if (next_token(&lp, le, &v, &vl) != 0)
                TRACE_FAIL("line %d: seed-timestamp missing value", lineno);
            out->hello_timestamp = (s64)strtoll(v, NULL, 10);
            have_seed |= 4;
        } else if (tok_eq(tok, tl, "checksum")) {
            const char *v;
            size_t vl;
            if (next_token(&lp, le, &v, &vl) != 0)
                TRACE_FAIL("line %d: checksum missing value", lineno);
            out->checksum_size = (u8)strtoul(v, NULL, 10);
            have_checksum = 1;
        } else if (tok_eq(tok, tl, "render")) {
            /* The rest of the line is the observation text; trim surrounding
             * whitespace (the text itself carries no leading/trailing spaces). */
            const char *s = lp;
            while (s < le && (*s == ' ' || *s == '\t'))
                s++;
            const char *e = le;
            while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r'))
                e--;
            size_t vl = (size_t)(e - s);
            if (vl == 0)
                TRACE_FAIL("line %d: render missing value", lineno);
            if (vl >= MMO_TRACE_MAX_RENDER)
                TRACE_FAIL("line %d: render observation too long", lineno);
            if (out->nrenders >= MMO_TRACE_MAX_RECS)
                TRACE_FAIL("line %d: too many render pins", lineno);
            memcpy(out->renders[out->nrenders], s, vl);
            out->renders[out->nrenders][vl] = '\0';
            out->nrenders++;
        } else if (tok_eq(tok, tl, "c2s") || tok_eq(tok, tl, "s2c")) {
            mmo_trace_dir dir = tok_eq(tok, tl, "c2s")
                                    ? MMO_TRACE_C2S : MMO_TRACE_S2C;
            const char *kt;
            size_t ktl;
            if (next_token(&lp, le, &kt, &ktl) != 0)
                TRACE_FAIL("line %d: record missing kind", lineno);
            mmo_trace_kind kind;
            if (tok_eq(kt, ktl, "hs"))
                kind = MMO_TRACE_HS;
            else if (tok_eq(kt, ktl, "app"))
                kind = MMO_TRACE_APP;
            else
                TRACE_FAIL("line %d: unknown record kind", lineno);

            const char *wh;
            size_t whl;
            if (next_token(&lp, le, &wh, &whl) != 0)
                TRACE_FAIL("line %d: record missing wire hex", lineno);
            u8 wire[MMO_TRACE_MAX_FRAME];
            long wn = hex_decode(wh, whl, wire, sizeof wire);
            if (wn < 0)
                TRACE_FAIL("line %d: bad wire hex", lineno);

            u8 clear[MMO_TRACE_MAX_FRAME];
            long cn = 0;
            if (kind == MMO_TRACE_APP) {
                const char *ch;
                size_t chl;
                if (next_token(&lp, le, &ch, &chl) != 0)
                    TRACE_FAIL("line %d: app record missing clear hex", lineno);
                cn = hex_decode(ch, chl, clear, sizeof clear);
                if (cn < 0)
                    TRACE_FAIL("line %d: bad clear hex", lineno);
            }
            if (mmo_trace_add(out, dir, kind, wire, (size_t)wn,
                              clear, (size_t)cn) != 0)
                TRACE_FAIL("line %d: too many records or frame too large", lineno);
        } else {
            TRACE_FAIL("line %d: unknown directive", lineno);
        }
    }

    if (have_seed != 7)
        TRACE_FAIL("trace is missing a seed-priv/-random/-timestamp line");
    if (!have_checksum)
        TRACE_FAIL("trace is missing a checksum line");
    return 0;
}

/* --- replay ------------------------------------------------------------ */

/* Pull the one frame body out of a full framed span. Returns 0 and points
 * body/blen at it, or -1 if the span is not exactly one whole frame. */
static int frame_body(const u8 *wire, size_t wirelen,
                      const u8 **body, size_t *blen)
{
    mmo_rbuf r;
    mmo_rbuf_init(&r, wire, wirelen);
    return mmo_frame_get(&r, body, blen) == MMO_FRAME_OK ? 0 : -1;
}

static int bytes_eq(const u8 *a, size_t an, const u8 *b, size_t bn)
{
    return an == bn && (an == 0 || memcmp(a, b, an) == 0);
}

int mmo_trace_replay(const mmo_trace *t, const u8 *root_pub,
                     char *err, size_t errcap)
{
    mmo_session s;
    mmo_wbuf pending;   /* the last handshake frame the client produced */
    mmo_wbuf_init(&pending);

    if (mmo_session_start_seeded(&s, &pending, t->eph_priv,
                                 t->hello_random, t->hello_timestamp) != 0) {
        mmo_wbuf_free(&pending);
        TRACE_FAIL("seeded ClientHello could not be built: %s", s.errmsg);
    }
    if (root_pub)
        s.root_pub = root_pub;

    int rc = 0;
    int ri = 0;   /* the next rendered-state pin to check */
    for (int i = 0; i < t->nrecs; i++) {
        const mmo_trace_rec *r = &t->recs[i];

        if (r->dir == MMO_TRACE_C2S && r->kind == MMO_TRACE_HS) {
            /* A client-produced handshake frame (ClientHello, then ClientReady):
             * whatever the session last framed must match it byte for byte. */
            if (!bytes_eq(pending.data, pending.len, r->wire, r->wirelen)) {
                if (err) snprintf(err, errcap,
                    "record %d: client handshake frame differs (%zu vs %zu bytes)",
                    i, pending.len, r->wirelen);
                rc = -1;
                break;
            }
        } else if (r->dir == MMO_TRACE_S2C && r->kind == MMO_TRACE_HS) {
            /* Feed the recorded ServerHello; the session frames ClientReady. */
            const u8 *body;
            size_t blen;
            if (frame_body(r->wire, r->wirelen, &body, &blen) != 0) {
                if (err) snprintf(err, errcap,
                    "record %d: ServerHello is not one whole frame", i);
                rc = -1;
                break;
            }
            mmo_wbuf_free(&pending);
            mmo_wbuf_init(&pending);
            if (mmo_session_on_server_hello(&s, body, blen, &pending) != 0) {
                if (err) snprintf(err, errcap,
                    "record %d: ServerHello rejected: %s", i, s.errmsg);
                rc = -1;
                break;
            }
        } else if (r->dir == MMO_TRACE_C2S && r->kind == MMO_TRACE_APP) {
            /* Re-encipher the recorded plaintext; the ciphertext frame must match
             * (proves the outbound codec/crypto/checksum still byte-freeze). */
            if (r->clearlen < 1) {
                if (err) snprintf(err, errcap,
                    "record %d: outbound app frame has no opcode", i);
                rc = -1;
                break;
            }
            mmo_wbuf frame;
            mmo_wbuf_init(&frame);
            mmo_session_send_app(&s.crypto, r->clear[0],
                                 r->clear + 1, r->clearlen - 1, &frame);
            int ok = !frame.err &&
                     bytes_eq(frame.data, frame.len, r->wire, r->wirelen);
            mmo_wbuf_free(&frame);
            if (!ok) {
                if (err) snprintf(err, errcap,
                    "record %d: re-enciphered frame differs from the recording", i);
                rc = -1;
                break;
            }
        } else { /* S2C APP */
            /* Decipher the recorded ciphertext; the plaintext must match. */
            const u8 *body;
            size_t blen;
            if (frame_body(r->wire, r->wirelen, &body, &blen) != 0) {
                if (err) snprintf(err, errcap,
                    "record %d: inbound app frame is not one whole frame", i);
                rc = -1;
                break;
            }
            u8 plain[MMO_TRACE_MAX_FRAME];
            size_t plen = mmo_session_recv_app(&s.crypto, body, blen,
                                               plain, sizeof plain);
            if (plen == (size_t)-1) {
                if (err) snprintf(err, errcap,
                    "record %d: inbound frame failed to decipher/verify", i);
                rc = -1;
                break;
            }
            if (!bytes_eq(plain, plen, r->clear, r->clearlen)) {
                if (err) snprintf(err, errcap,
                    "record %d: deciphered plaintext differs from the recording", i);
                rc = -1;
                break;
            }
            /*
             * Rendered-state differential (5.5): decode the plaintext the client just
             * deciphered into the observation it would render, and pin it. A decode regression
             * that keeps the bytes intact passes the byte comparison above but is caught here.
             */
            if (t->nrenders > 0) {
                char obs[MMO_TRACE_MAX_RENDER];
                if (mmo_trace_render_app(plain, plen, obs, sizeof obs) == 0) {
                    if (ri >= t->nrenders) {
                        if (err) snprintf(err, errcap,
                            "record %d: renders \"%s\" but the trace has no pin for it",
                            i, obs);
                        rc = -1;
                        break;
                    }
                    if (strcmp(obs, t->renders[ri]) != 0) {
                        if (err) snprintf(err, errcap,
                            "record %d: rendered state \"%s\" != pinned \"%s\"",
                            i, obs, t->renders[ri]);
                        rc = -1;
                        break;
                    }
                    ri++;
                }
            }
        }
    }

    /* Every pin must have been produced: a trace that pins more observations than
     * replay renders is as much a regression as a mismatch. */
    if (rc == 0 && t->nrenders > 0 && ri != t->nrenders) {
        if (err) snprintf(err, errcap,
            "trace pins %d rendered observation(s) but replay produced %d",
            t->nrenders, ri);
        rc = -1;
    }

    mmo_wbuf_free(&pending);
    return rc;
}

int mmo_trace_parse_file(const char *path, mmo_trace *out,
                         char *err, size_t errcap)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        TRACE_FAIL("cannot open trace %s", path);
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        TRACE_FAIL("cannot seek trace %s", path);
    }
    long sz = ftell(f);
    if (sz < 0) {
        fclose(f);
        TRACE_FAIL("cannot size trace %s", path);
    }
    rewind(f);
    char *buf = malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        TRACE_FAIL("out of memory reading %s", path);
    }
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[got] = '\0';

    int rc = mmo_trace_parse(buf, got, out, err, errcap);
    free(buf);
    return rc;
}

int mmo_trace_replay_file(const char *path, const u8 *root_pub,
                          char *err, size_t errcap)
{
    mmo_trace *t = malloc(sizeof *t);
    if (!t)
        TRACE_FAIL("out of memory for trace");
    int rc = mmo_trace_parse_file(path, t, err, errcap);
    if (rc == 0)
        rc = mmo_trace_replay(t, root_pub, err, errcap);
    free(t);
    return rc;
}
