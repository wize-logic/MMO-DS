/* The login-stream game-server handoff codecs. */
#include <stdio.h>
#include <string.h>

#include "handoff.h"

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

static void test_join_request(void)
{
    printf("JoinGameServer request body:\n");
    mmo_wbuf b;
    mmo_wbuf_init(&b);
    mmo_handoff_write_join(&b, 0x07);
    CHECK(!b.err && b.len == 1 && b.data[0] == 0x07,
          "JoinGameServer is a single game-server-id byte");
    mmo_wbuf_free(&b);

    mmo_wbuf e;
    mmo_wbuf_init(&e);
    mmo_handoff_write_reqlist(&e);
    CHECK(!e.err && e.len == 0, "RequestGameServerList body is empty");
    mmo_wbuf_free(&e);
}

static void test_serverlist(void)
{
    printf("GameServerList decode (first joinable server):\n");
    /* count=1, leading id, then one server: id=0, name "OpenMMO", players,
     * joinable=1. Mirrors GameServerListPacketCodec. */
    mmo_wbuf b;
    mmo_wbuf_init(&b);
    mmo_put_u8(&b, 1);          /* count */
    mmo_put_u8(&b, 0);          /* leading first-id */
    mmo_put_u8(&b, 0);          /* server id */
    mmo_put_utf16_nt(&b, "OpenMMO");
    mmo_put_u16le(&b, 0);       /* currentPlayers */
    mmo_put_u16le(&b, 1);       /* maxPlayers */
    mmo_put_bool(&b, 1);        /* joinable */

    mmo_gameserver gs;
    int rc = mmo_handoff_read_serverlist(b.data, b.len, &gs);
    CHECK(rc == 0 && gs.found && gs.id == 0, "found the joinable server id 0");
    CHECK(strcmp(gs.name, "OpenMMO") == 0, "decoded the server name");
    mmo_wbuf_free(&b);

    /* Empty list: count 0 then two trailing zero bytes. */
    mmo_wbuf e;
    mmo_wbuf_init(&e);
    mmo_put_u8(&e, 0);
    mmo_put_u8(&e, 0);
    mmo_put_u8(&e, 0);
    rc = mmo_handoff_read_serverlist(e.data, e.len, &gs);
    CHECK(rc == 0 && !gs.found, "empty list reports no joinable server");
    mmo_wbuf_free(&e);
}

static void test_nodes(void)
{
    printf("GameServerNodes decode (AUTHED ticket):\n");
    u8 token[MMO_SESSION_TOKEN_LEN];
    for (int i = 0; i < MMO_SESSION_TOKEN_LEN; i++)
        token[i] = (u8)(0x40 + i);

    mmo_wbuf b;
    mmo_wbuf_init(&b);
    mmo_put_u8(&b, 0x00);            /* loginState AUTHED */
    mmo_put_s32le(&b, 4242);         /* userId */
    mmo_put_bytes_u8(&b, token, MMO_SESSION_TOKEN_LEN); /* sessionToken */
    mmo_put_u8(&b, 0);               /* gameServerId */
    u8 addr[4] = { 127, 0, 0, 1 };
    mmo_put_bytes_u8(&b, addr, 4);   /* localAddress */
    mmo_put_utf16_nt(&b, "localhost");
    mmo_put_s32le(&b, 7777);         /* port */
    mmo_put_u8(&b, 1);               /* one node */
    mmo_put_u8(&b, 0);               /* node index */
    mmo_put_u8(&b, 4);               /* TaggedIp IPv4 */
    mmo_put_s32le(&b, (s32)((203u << 24) | (113u << 8) | 5u)); /* 203.0.113.5 */
    mmo_put_u8(&b, 6);               /* TaggedIp IPv6 */
    mmo_put_s64le(&b, 0x20010db800000000LL); /* 2001:db8:: */
    mmo_put_s64le(&b, 1);                    /* ::1 */
    mmo_put_s16le(&b, 7000);         /* node port */
    mmo_put_u8(&b, 1);               /* weight */

    mmo_gs_nodes nodes;
    int rc = mmo_handoff_read_nodes(b.data, b.len, &nodes);
    CHECK(rc == 0 && nodes.login_state == 0x00, "AUTHED state parsed");
    CHECK(nodes.user_id == 4242, "decoded the userId");
    CHECK(nodes.token_len == MMO_SESSION_TOKEN_LEN
              && memcmp(nodes.token, token, MMO_SESSION_TOKEN_LEN) == 0,
          "decoded the session token whole");
    CHECK(nodes.port == 7777, "decoded the GameServerData port 7777");
    CHECK(nodes.node_count == 1, "read one advertised node");
    CHECK(strcmp(nodes.host, "203.0.113.5") == 0,
          "decoded the advertised IPv4, not the local 127.0.0.1");
    CHECK(nodes.node_port == 7000, "decoded the node's own port 7000");
    mmo_wbuf_free(&b);

    char picked[64];
    char why[128];
    rc = mmo_handoff_pick_host(&nodes, "127.0.0.1", picked, sizeof picked,
                               why, sizeof why);
    CHECK(rc == 0 && strcmp(picked, "203.0.113.5") == 0,
          "a public advertisement is used even when login was loopback");

    mmo_gs_nodes loop = nodes;
    snprintf(loop.host, sizeof loop.host, "%s", "127.0.0.1");
    rc = mmo_handoff_pick_host(&loop, "127.0.0.1", picked, sizeof picked,
                               why, sizeof why);
    CHECK(rc == 0 && strcmp(picked, "127.0.0.1") == 0,
          "loopback advertisement is fine when login was also loopback");

    rc = mmo_handoff_pick_host(&loop, "172.17.0.1", picked, sizeof picked,
                               why, sizeof why);
    CHECK(rc != 0 && strstr(why, "GAME_SERVER_PUBLIC_IPV4") != NULL,
          "loopback advertisement to a remote login names the knob");

    /* A non-AUTHED reply (NO_GS_AVAILABLE = 0x06) stops at the state byte. */
    u8 no_gs[1] = { 0x06 };
    rc = mmo_handoff_read_nodes(no_gs, 1, &nodes);
    CHECK(rc == 0 && nodes.login_state == 0x06 && nodes.token_len == 0,
          "non-AUTHED reply reports only the state");
}

int handoff_tests_run(void)
{
    failures = 0;
    test_join_request();
    test_serverlist();
    test_nodes();

    if (failures)
        printf("handoff: %d check(s) FAILED\n", failures);
    else
        printf("handoff: all checks passed\n");
    return failures;
}
