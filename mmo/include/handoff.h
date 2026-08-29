/* The login-stream packets that hand a client to a game server. */
#ifndef MMO_HANDOFF_H
#define MMO_HANDOFF_H

#include <stddef.h>

#include "mmo.h"
#include "codec.h"

/* Login-protocol opcodes for the handoff (LoginProtocol.kt). */
#define MMO_LOGIN_OP_REQ_GS_LIST 0x02  /* c2s */
#define MMO_LOGIN_OP_JOIN_GS     0x03  /* c2s */
#define MMO_LOGIN_OP_GS_NODES    0x03  /* s2c */
#define MMO_LOGIN_OP_GS_LIST     0x22  /* s2c */

/* The session token is a fixed 16-byte prefix + 16-byte HMAC (SessionToken.kt). */
#define MMO_SESSION_TOKEN_LEN 32

/* Write the RequestGameServerList body: empty (the codec emits zero bytes). */
void mmo_handoff_write_reqlist(mmo_wbuf *body);

/* Write the JoinGameServer body: a single U8 game-server id. */
void mmo_handoff_write_join(mmo_wbuf *body, u8 gs_id);

/* The first joinable server pulled from a GameServerList. */
typedef struct {
    int  found;       /* a joinable server was present */
    u8   id;
    char name[64];
} mmo_gameserver;

/* Parse a GameServerList body (opcode already stripped) and report the first
 * joinable server in `out`. Returns 0 on a well-formed list (out->found says
 * whether any server was joinable), -1 if the body is malformed. */
int mmo_handoff_read_serverlist(const u8 *body, size_t n, mmo_gameserver *out);

/* The parsed result of a JoinGameServer request. */
typedef struct {
    u8     login_state;                   /* LoginState id; 0 == AUTHED */
    s32    user_id;                       /* valid only when AUTHED */
    u8     token[MMO_SESSION_TOKEN_LEN];  /* the game session token */
    size_t token_len;
    u16    port;                          /* GameServerData.port (S32LE); fallback */
    char   host[64];                      /* advertised IPv4, else IPv6, else empty */
    u16    node_port;                     /* first node's S16LE port; 0 if none */
    int    node_count;
} mmo_gs_nodes;

/*
 * Parse a GameServerNodes body (opcode already stripped). On a non-AUTHED state only
 * login_state is filled.
 */
int mmo_handoff_read_nodes(const u8 *body, size_t n, mmo_gs_nodes *out);

/* The address a session should dial for the game server. */
int mmo_handoff_pick_host(const mmo_gs_nodes *nodes, const char *login_host,
                          char *out, size_t out_cap, char *err, size_t err_cap);

#endif /* MMO_HANDOFF_H */
