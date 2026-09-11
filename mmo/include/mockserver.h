/* A hermetic, pinned-key stand-in for a OpenMMO server. */
#ifndef MMO_MOCKSERVER_H
#define MMO_MOCKSERVER_H

#include <stddef.h>

#include "mmo.h"
#include "codec.h"
#include "session.h"

/* The test root public point (65-byte uncompressed) the mock signs its
 * ServerHello under. A client that sets mmo_session.root_pub to this trusts the
 * mock; anyone else keeps the pinned mmo_root_pubkey and rejects the mock's
 * ServerHello (as it should). */
extern const u8 *const mmo_mock_root_pub;

/* One scripted reply: on an inbound application frame carrying `req_opcode`, the
 * mock enciphers (`resp_opcode` || body[0..body_len]) back through its S->C
 * stream. Bodies are small and copied in, so the table owns its bytes. */
#define MMO_MOCK_MAX_REPLIES  8
#define MMO_MOCK_MAX_BODY     256

typedef struct {
    u8     req_opcode;
    u8     resp_opcode;
    u8     body[MMO_MOCK_MAX_BODY];
    size_t body_len;
    int    used;
} mmo_mock_reply;

typedef struct {
    mmo_session_crypto crypto;                 /* server role, valid once ready */
    int                established;            /* ClientReady consumed */
    u8                 checksum_size;          /* profile the ServerHello names */
    mmo_mock_reply     replies[MMO_MOCK_MAX_REPLIES];
    size_t             nreplies;
} mmo_mock_server;

/* The one ClientHello timestamp the mock can answer, and the profiles it can offer. */
#define MMO_MOCK_HELLO_TS  ((s64)1700000000000LL)

/*
 * Initialise the mock for a session using the given checksum profile (2, 4, 8, 16 or 32; 16 is
 * the login profile the servers deploy).
 */
void mmo_mock_server_init(mmo_mock_server *m, u8 checksum_size);

/*
 * Append the framed (plaintext) ServerHello to `out`, answering a ClientHello that carried
 * `client_timestamp`. Constant for a given checksum profile: the mock's fixed ephemeral point,
 * the pinned signature over (point, size, timestamp), and the profile byte.
 */
int mmo_mock_server_hello(mmo_mock_server *m, mmo_wbuf *out, s64 client_timestamp);

/*
 * Consume the client's ClientReady frame body (opcode included, as mmo_frame_get yields it):
 * extract the client ephemeral point, run ECDH against the mock's fixed private key, and
 * derive the server-role session crypto.
 */
int mmo_mock_server_on_client_ready(mmo_mock_server *m,
                                    const void *body, size_t n);

/* Register a scripted reply. Returns 0, or -1 if the table is full or the body
 * is too long. The last registration for a given req_opcode wins. */
int mmo_mock_server_reply(mmo_mock_server *m, u8 req_opcode, u8 resp_opcode,
                          const u8 *body, size_t body_len);

/*
 * Feed one inbound application frame body (the ciphertext+tag mmo_frame_get yields). The mock
 * deciphers and checksum-verifies it under the C->S stream; if its opcode matches a scripted
 * reply, the enciphered reply frame is appended to `out`.
 */
int mmo_mock_server_on_app(mmo_mock_server *m, const void *body, size_t n,
                           mmo_wbuf *out);

#endif /* MMO_MOCKSERVER_H */
