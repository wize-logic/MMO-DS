/* Record and replay a whole transport session as a byte comparison. */
#ifndef MMO_TRACE_H
#define MMO_TRACE_H

#include <stddef.h>
#include <stdio.h>

#include "mmo.h"
#include "codec.h"
#include "p256.h"

/* A login session is five short frames; a generous cap leaves room for a few
 * post-auth RPCs without making the struct a heap-only object elsewhere. */
#define MMO_TRACE_MAX_RECS   32
#define MMO_TRACE_MAX_FRAME  1024
#define MMO_TRACE_MAX_RENDER 96   /* a rendered-observation string, NUL-terminated */

typedef enum { MMO_TRACE_C2S = 0, MMO_TRACE_S2C = 1 } mmo_trace_dir;

/* Handshake frames (ClientHello/ServerHello/ClientReady) are plaintext and carry
 * no separate cleartext; APP frames carry both the wire ciphertext and the
 * deciphered opcode||body. */
typedef enum { MMO_TRACE_HS = 0, MMO_TRACE_APP = 1 } mmo_trace_kind;

typedef struct {
    mmo_trace_dir  dir;
    mmo_trace_kind kind;
    u8     wire[MMO_TRACE_MAX_FRAME];  /* the full framed bytes on the socket */
    size_t wirelen;
    u8     clear[MMO_TRACE_MAX_FRAME]; /* APP only: deciphered opcode||body */
    size_t clearlen;
} mmo_trace_rec;

typedef struct {
    u8   eph_priv[MMO_P256_SCALAR]; /* the pinned client ephemeral scalar */
    s64  hello_random;              /* the pinned ClientHello nonce */
    s64  hello_timestamp;           /* the ClientHello Unix-ms time */
    u8   checksum_size;             /* the negotiated post-handshake profile */
    char label[64];                 /* free-text provenance, e.g. "login admin@2106" */
    mmo_trace_rec recs[MMO_TRACE_MAX_RECS];
    int  nrecs;
    /*
     * The rendered-state pins (5.5): the semantic observation the client draws from each
     * renderable S->C application frame, the login outcome for a LoginResponse, say, one per
     * pinned frame, in capture order.
     */
    char renders[MMO_TRACE_MAX_RECS][MMO_TRACE_MAX_RENDER];
    int  nrenders;
} mmo_trace;

/* Start an empty trace with the pinned seed a replay reproduces the session from.
 * `label` may be NULL. */
void mmo_trace_init(mmo_trace *t, const u8 eph_priv[MMO_P256_SCALAR],
                    s64 random, s64 timestamp, u8 checksum_size,
                    const char *label);

/* Append one captured frame. `clear`/`clearlen` are ignored for HS records and
 * required for APP records. Returns 0, or -1 if the trace is full or a span
 * exceeds MMO_TRACE_MAX_FRAME. */
int mmo_trace_add(mmo_trace *t, mmo_trace_dir dir, mmo_trace_kind kind,
                  const u8 *wire, size_t wirelen,
                  const u8 *clear, size_t clearlen);

/*
 * Decode one S->C application frame's cleartext (opcode || body) into the canonical rendered
 * observation the client draws from it, "login-state=0" for an AUTHED LoginResponse, say.
 */
int mmo_trace_render_app(const u8 *clear, size_t n, char *out, size_t cap);

/* Populate t->renders from t's own recorded S->C application cleartexts, one pin
 * per renderable frame (via mmo_trace_render_app). Call after the frames are
 * captured; serialize then writes the pins and replay checks them. Idempotent. */
void mmo_trace_derive_renders(mmo_trace *t);

/* Serialize a trace to the line-oriented text format (see trace.c) and append it
 * to `out`. Returns 0, or -1 on an allocation failure. */
int mmo_trace_serialize(const mmo_trace *t, mmo_wbuf *out);

/* Parse the text format from `text`/`n` into `out`. Returns 0, or -1 with a
 * message in `err` (up to `errcap`) on a malformed trace. */
int mmo_trace_parse(const char *text, size_t n, mmo_trace *out,
                    char *err, size_t errcap);

/* Replay a trace as a byte-and-render comparison. */
int mmo_trace_replay(const mmo_trace *t, const u8 *root_pub,
                     char *err, size_t errcap);

/* Read and parse a trace text file at `path` into `out`. Returns 0, or -1 with a
 * message in `err`. */
int mmo_trace_parse_file(const char *path, mmo_trace *out,
                         char *err, size_t errcap);

/* Convenience: parse a trace text file at `path` and replay it. Returns 0, or -1
 * with a message in `err`. */
int mmo_trace_replay_file(const char *path, const u8 *root_pub,
                          char *err, size_t errcap);

#endif /* MMO_TRACE_H */
