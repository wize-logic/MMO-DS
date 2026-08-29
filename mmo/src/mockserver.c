/* The pinned-key mock server double. See mockserver.h. */
#include "mockserver.h"

#include <string.h>

#include "p256.h"
#include "mock_keys.gen.h"  /* MOCK_ROOT_PUB, MOCK_EPH_PRIV/PUB, MOCK_HELLO_SIG */

/* Export the generated test-root point as a stable pointer; MOCK_ROOT_PUB is a
 * static const with program lifetime, so pointing at it is safe. */
const u8 *const mmo_mock_root_pub = MOCK_ROOT_PUB;

void mmo_mock_server_init(mmo_mock_server *m, u8 checksum_size)
{
    memset(m, 0, sizeof *m);
    m->checksum_size = checksum_size;
}

void mmo_mock_server_hello(mmo_mock_server *m, mmo_wbuf *out)
{
    mmo_wbuf pkt;
    mmo_wbuf_init(&pkt);
    mmo_put_u8(&pkt, MMO_HS_SERVER_HELLO);
    mmo_put_u16le(&pkt, MMO_P256_POINT);
    mmo_put_bytes(&pkt, MOCK_EPH_PUB, MMO_P256_POINT);
    mmo_put_u16le(&pkt, (u16)sizeof MOCK_HELLO_SIG);
    mmo_put_bytes(&pkt, MOCK_HELLO_SIG, sizeof MOCK_HELLO_SIG);
    mmo_put_u8(&pkt, m->checksum_size);
    mmo_frame_put(out, pkt.data, pkt.len);
    mmo_wbuf_free(&pkt);
}

int mmo_mock_server_on_client_ready(mmo_mock_server *m,
                                    const void *body, size_t n)
{
    mmo_rbuf r;
    mmo_rbuf_init(&r, body, n);
    if (mmo_get_u8(&r) != MMO_HS_CLIENT_READY)
        return -1;
    if (mmo_get_u16le(&r) != MMO_P256_POINT)
        return -1;
    u8 client_pub[MMO_P256_POINT];
    mmo_get_bytes(&r, client_pub, MMO_P256_POINT);
    if (r.err)
        return -1;

    u8 secret[MMO_P256_SCALAR];
    if (mmo_p256_ecdh(MOCK_EPH_PRIV, client_pub, secret) != 0)
        return -1;

    mmo_session_derive_server(&m->crypto, secret, m->checksum_size);
    m->established = 1;
    return 0;
}

int mmo_mock_server_reply(mmo_mock_server *m, u8 req_opcode, u8 resp_opcode,
                          const u8 *body, size_t body_len)
{
    if (body_len > MMO_MOCK_MAX_BODY)
        return -1;

    /* Replace an existing entry for this opcode, else take a free slot. */
    mmo_mock_reply *slot = NULL;
    for (size_t i = 0; i < MMO_MOCK_MAX_REPLIES; i++) {
        if (m->replies[i].used && m->replies[i].req_opcode == req_opcode) {
            slot = &m->replies[i];
            break;
        }
    }
    if (!slot) {
        for (size_t i = 0; i < MMO_MOCK_MAX_REPLIES; i++) {
            if (!m->replies[i].used) {
                slot = &m->replies[i];
                m->nreplies++;
                break;
            }
        }
    }
    if (!slot)
        return -1;

    slot->req_opcode = req_opcode;
    slot->resp_opcode = resp_opcode;
    slot->body_len = body_len;
    if (body_len)
        memcpy(slot->body, body, body_len);
    slot->used = 1;
    return 0;
}

int mmo_mock_server_on_app(mmo_mock_server *m, const void *body, size_t n,
                           mmo_wbuf *out)
{
    if (!m->established)
        return -1;

    u8 plain[MMO_MOCK_MAX_BODY + 1];
    size_t plen = mmo_session_recv_app(&m->crypto, body, n, plain, sizeof plain);
    if (plen == (size_t)-1 || plen < 1)
        return -1;

    u8 req_opcode = plain[0];
    for (size_t i = 0; i < MMO_MOCK_MAX_REPLIES; i++) {
        mmo_mock_reply *rp = &m->replies[i];
        if (rp->used && rp->req_opcode == req_opcode) {
            mmo_session_send_app(&m->crypto, rp->resp_opcode,
                                 rp->body, rp->body_len, out);
            break;
        }
    }
    return req_opcode;
}
