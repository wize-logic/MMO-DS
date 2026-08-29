/* The login-stream game-server handoff codecs. See handoff.h. */
#include "handoff.h"

#include <stdio.h>
#include <string.h>

#include "sockets.h"  /* inet_pton/inet_ntop, from whichever stack this host has */

#include "login.h"  /* MMO_LOGIN_AUTHED */

/* Host order from network order, done here rather than through ntohl: that
 * one lives in ws2_32 on Windows and sockets.h says why this file must not
 * make the loader open it. */
static u32 mmo_ntohl(u32 v)
{
    const u8 *b = (const u8 *)&v;

    return ((u32)b[0] << 24) | ((u32)b[1] << 16) | ((u32)b[2] << 8) | b[3];
}

static int host_is_loopback(const char *host)
{
    struct in_addr v4;
    struct in6_addr v6;

    if (host == NULL || host[0] == '\0')
        return 1;
    if (strcmp(host, "localhost") == 0)
        return 1;
    if (mmo_sock_pton(AF_INET, host, &v4) == 1)
        return (mmo_ntohl(v4.s_addr) >> 24) == 127;
    if (mmo_sock_pton(AF_INET6, host, &v6) == 1)
        return IN6_IS_ADDR_LOOPBACK(&v6);
    return 0;
}

static int ipv4_unspecified(const char *host)
{
    return strcmp(host, "0.0.0.0") == 0;
}

/* TaggedIpCodec: U8 type, then S32LE IPv4 or two S64LE IPv6 halves. */
static int read_tagged_ip(mmo_rbuf *r, char *out, size_t cap)
{
    u8 type = mmo_get_u8(r);
    if (r->err)
        return -1;
    if (type == 4) {
        u32 addr = (u32)mmo_get_s32le(r);
        if (r->err || cap < 16)
            return -1;
        snprintf(out, cap, "%u.%u.%u.%u",
                 (addr >> 24) & 0xffu, (addr >> 16) & 0xffu,
                 (addr >> 8) & 0xffu, addr & 0xffu);
        return 0;
    }
    if (type == 6) {
        u64 hi = (u64)mmo_get_s64le(r);
        u64 lo = (u64)mmo_get_s64le(r);
        struct in6_addr a;
        int i;
        if (r->err)
            return -1;
        for (i = 0; i < 8; i++)
            a.s6_addr[i] = (u8)(hi >> (56 - 8 * i));
        for (i = 0; i < 8; i++)
            a.s6_addr[8 + i] = (u8)(lo >> (56 - 8 * i));
        if (mmo_sock_ntop(AF_INET6, &a, out, cap) == NULL)
            return -1;
        return 0;
    }
    return -1;
}

void mmo_handoff_write_reqlist(mmo_wbuf *body)
{
    (void)body;  /* RequestGameServerList has an empty body */
}

void mmo_handoff_write_join(mmo_wbuf *body, u8 gs_id)
{
    mmo_put_u8(body, gs_id);
}

int mmo_handoff_read_serverlist(const u8 *body, size_t n, mmo_gameserver *out)
{
    out->found = 0;
    out->id = 0;
    out->name[0] = '\0';

    mmo_rbuf r;
    mmo_rbuf_init(&r, body, n);

    u8 count = mmo_get_u8(&r);
    if (count == 0) {
        /* Empty list still carries two trailing U8s (GameServerListPacketCodec). */
        mmo_get_u8(&r);
        mmo_get_u8(&r);
        return r.err ? -1 : 0;
    }

    /* A leading U8 (the first server's id) precedes the entries. */
    mmo_get_u8(&r);
    for (u8 i = 0; i < count; i++) {
        u8 id = mmo_get_u8(&r);
        char name[64];
        mmo_get_utf16_nt(&r, name, sizeof name);
        mmo_get_u16le(&r);          /* currentPlayers */
        mmo_get_u16le(&r);          /* maxPlayers */
        int joinable = mmo_get_bool(&r);
        if (r.err)
            return -1;
        if (joinable && !out->found) {
            out->found = 1;
            out->id = id;
            size_t j = 0;
            for (; name[j] && j < sizeof out->name - 1; j++)
                out->name[j] = name[j];
            out->name[j] = '\0';
        }
    }
    return 0;
}

int mmo_handoff_read_nodes(const u8 *body, size_t n, mmo_gs_nodes *out)
{
    mmo_rbuf r;
    mmo_rbuf_init(&r, body, n);

    out->login_state = mmo_get_u8(&r);
    out->user_id = 0;
    out->token_len = 0;
    out->port = 0;
    out->host[0] = '\0';
    out->node_port = 0;
    out->node_count = 0;
    if (r.err)
        return -1;
    if (out->login_state != MMO_LOGIN_AUTHED)
        return 0;  /* nothing more on the wire for a non-AUTHED reply */

    out->user_id = mmo_get_s32le(&r);
    out->token_len = mmo_get_bytes_u8(&r, out->token, sizeof out->token);
    mmo_get_u8(&r);                     /* gameServerId */
    u8 addr[16];
    mmo_get_bytes_u8(&r, addr, sizeof addr);   /* localAddress: official loopback */
    char local_host[64];
    mmo_get_utf16_nt(&r, local_host, sizeof local_host); /* localHostname */
    out->port = (u16)mmo_get_s32le(&r);        /* port (S32LE) */

    u8 count = mmo_get_u8(&r);
    out->node_count = count;
    for (u8 i = 0; i < count; i++) {
        char ipv4[64];
        char ipv6[64];
        mmo_get_u8(&r);                 /* node index */
        if (read_tagged_ip(&r, ipv4, sizeof ipv4) != 0)
            return -1;
        if (read_tagged_ip(&r, ipv6, sizeof ipv6) != 0)
            return -1;
        u16 node_port = (u16)mmo_get_s16le(&r);
        mmo_get_u8(&r);                 /* weight */
        if (r.err)
            return -1;
        if (out->host[0] != '\0')
            continue;
        if (!ipv4_unspecified(ipv4)) {
            snprintf(out->host, sizeof out->host, "%s", ipv4);
            out->node_port = node_port;
        } else if (ipv6[0] != '\0') {
            snprintf(out->host, sizeof out->host, "%s", ipv6);
            out->node_port = node_port;
        }
    }

    if (r.err || out->token_len != MMO_SESSION_TOKEN_LEN)
        return -1;
    return 0;
}

int mmo_handoff_pick_host(const mmo_gs_nodes *nodes, const char *login_host,
                          char *out, size_t out_cap, char *err, size_t err_cap)
{
    const char *want;

    if (out == NULL || out_cap == 0)
        return -1;
    out[0] = '\0';
    if (err != NULL && err_cap > 0)
        err[0] = '\0';

    if (nodes == NULL || nodes->host[0] == '\0') {
        if (err != NULL && err_cap > 0)
            snprintf(err, err_cap,
                     "handoff: login advertised no game-server address");
        return -1;
    }

    want = nodes->host;
    if (host_is_loopback(want) && !host_is_loopback(login_host)) {
        if (err != NULL && err_cap > 0)
            snprintf(err, err_cap,
                     "handoff: login advertised %s; set GAME_SERVER_PUBLIC_IPV4 "
                     "to an address the other machine can reach",
                     want);
        return -1;
    }

    if (strlen(want) >= out_cap) {
        if (err != NULL && err_cap > 0)
            snprintf(err, err_cap, "handoff: advertised address is too long");
        return -1;
    }
    snprintf(out, out_cap, "%s", want);
    return 0;
}
