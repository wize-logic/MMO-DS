/* Non-blocking TCP transport. See net.h for the contract. */
#include "net.h"
#include "codec.h"
#include "platform.h"
#include "sockets.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Read chunk pulled per recv() call; pump loops until the socket drains. */
#define NET_READ_CHUNK 4096

/* Something ended the connection: say what was being done and why it stopped. */
static void set_fail(mmo_net *n, const char *what, const char *why)
{
    snprintf(n->errmsg, sizeof n->errmsg, "%s: %s", what, why);
    if (n->fd >= 0) {
        mmo_sock_close(n->fd);
        n->fd = -1;
    }
    n->state = MMO_NET_ERROR;
}

/* The socket call itself failed. The error is read here rather than passed in
 * because every caller reads it at exactly this point, and on Windows it does
 * not live in errno, so reading it late would read somebody else's. */
static void set_error(mmo_net *n, const char *what)
{
    set_fail(n, what, mmo_sock_strerror(mmo_sock_errno()));
}

/* --- byte buffer ------------------------------------------------------- */

static void buf_free(mmo_buf *b)
{
    free(b->data);
    b->data = NULL;
    b->head = b->len = b->cap = 0;
}

/* Drop the consumed prefix so head-based reads do not grow the buffer forever. */
static void buf_compact(mmo_buf *b)
{
    if (b->head == 0)
        return;
    if (b->head == b->len) {
        b->head = b->len = 0;
        return;
    }
    memmove(b->data, b->data + b->head, b->len - b->head);
    b->len -= b->head;
    b->head = 0;
}

/* Ensure room for `extra` more bytes past len, compacting first if it helps. */
static int buf_reserve(mmo_buf *b, size_t extra)
{
    if (b->head > 0 && b->len + extra > b->cap)
        buf_compact(b);
    if (b->len + extra <= b->cap)
        return 0;
    size_t want = b->cap ? b->cap : 256;
    while (want < b->len + extra)
        want *= 2;
    u8 *p = realloc(b->data, want);
    if (!p)
        return -1;
    b->data = p;
    b->cap = want;
    return 0;
}

static int buf_append(mmo_buf *b, const void *data, size_t len)
{
    if (len == 0)
        return 0;
    if (buf_reserve(b, len) != 0)
        return -1;
    memcpy(b->data + b->len, data, len);
    b->len += len;
    return 0;
}

/* --- lifecycle --------------------------------------------------------- */

void mmo_net_init(mmo_net *n)
{
    n->fd = -1;
    n->state = MMO_NET_IDLE;
    memset(&n->tx, 0, sizeof n->tx);
    memset(&n->rx, 0, sizeof n->rx);
    n->errmsg[0] = '\0';
}

int mmo_net_connect(mmo_net *n, const char *host, u16 port)
{
    mmo_net_init(n);

    if (mmo_sock_ready() != 0) {
        snprintf(n->errmsg, sizeof n->errmsg,
                 "socket layer unavailable: could not reach it");
        n->state = MMO_NET_ERROR;
        return -1;
    }

    char portstr[16];
    snprintf(portstr, sizeof portstr, "%u", (unsigned)port);

    struct addrinfo *res = NULL;
    int gai = mmo_sock_resolve(host, portstr, &res);

    if (gai != 0) {
        /* The address is not repeated back. It is compiled in and obfuscated
         * (endpoint.h), and a message shown on the failure screen is the one
         * place a client would otherwise print it for anyone to read. */
        snprintf(n->errmsg, sizeof n->errmsg, "could not look up the server: %s",
                 mmo_sock_resolve_error(gai));
        n->state = MMO_NET_ERROR;
        return -1;
    }

    /* Try each resolved address; keep the first whose non-blocking connect
     * either completes or is safely in flight. */
    for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        int fd = mmo_sock_open(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        int err;

        if (fd < 0)
            continue;

        mmo_sock_nodelay(fd);

        if (mmo_sock_connect(fd, ai->ai_addr, (unsigned)ai->ai_addrlen) == 0) {
            n->fd = fd;
            n->state = MMO_NET_CONNECTED;
            mmo_sock_free_resolved(res);
            return 0;
        }
        err = mmo_sock_errno();
        if (err == MMO_EINPROGRESS || err == MMO_EINTR) {
            n->fd = fd;
            n->state = MMO_NET_CONNECTING;
            mmo_sock_free_resolved(res);
            return 0;
        }
        mmo_sock_close(fd);
    }

    mmo_sock_free_resolved(res);
    snprintf(n->errmsg, sizeof n->errmsg, "could not reach the server: %s",
             mmo_sock_strerror(mmo_sock_errno()));
    n->state = MMO_NET_ERROR;
    return -1;
}

/* Resolve a pending non-blocking connect once the socket reports writable. */
static int finish_connect(mmo_net *n)
{
    int err = mmo_sock_error(n->fd);

    if (err < 0) {
        set_error(n, "getsockopt(SO_ERROR)");
        return -1;
    }
    if (err != 0) {
        set_fail(n, "connect", mmo_sock_strerror(err));
        return -1;
    }
    n->state = MMO_NET_CONNECTED;
    return 0;
}

/* Drain as much of the tx queue as the socket will take without blocking. */
static int flush_tx(mmo_net *n)
{
    while (n->tx.head < n->tx.len) {
        long w = mmo_sock_send(n->fd, n->tx.data + n->tx.head,
                               n->tx.len - n->tx.head, MSG_NOSIGNAL);
        int err;

        if (w > 0) {
            n->tx.head += (size_t)w;
            continue;
        }
        err = mmo_sock_errno();
        if (w < 0 && err == MMO_EINTR)
            continue;
        if (w < 0 && (err == MMO_EAGAIN || err == MMO_EWOULDBLOCK))
            break; /* kernel buffer full; try again next pump */
        set_error(n, "send");
        return -1;
    }
    buf_compact(&n->tx);
    return 0;
}

/* Read everything currently available into rx without blocking. */
static int fill_rx(mmo_net *n)
{
    for (;;) {
        long r;
        int err;

        if (buf_reserve(&n->rx, NET_READ_CHUNK) != 0) {
            set_fail(n, "recv", "out of memory");
            return -1;
        }
        r = mmo_sock_recv(n->fd, n->rx.data + n->rx.len, NET_READ_CHUNK, 0);
        if (r > 0) {
            n->rx.len += (size_t)r;
            continue;
        }
        if (r == 0) {
            /* Orderly shutdown by the peer. */
            mmo_sock_close(n->fd);
            n->fd = -1;
            n->state = MMO_NET_CLOSED;
            return -1;
        }
        err = mmo_sock_errno();
        if (err == MMO_EINTR)
            continue;
        if (err == MMO_EAGAIN || err == MMO_EWOULDBLOCK)
            break; /* nothing more right now */
        set_error(n, "recv");
        return -1;
    }
    return 0;
}

int mmo_net_pump(mmo_net *n)
{
    if (n->state != MMO_NET_CONNECTING && n->state != MMO_NET_CONNECTED)
        return -1;

    int want_write = (n->state == MMO_NET_CONNECTING || n->tx.head < n->tx.len);
    int revents = 0;
    int pr = mmo_sock_poll(n->fd, want_write, &revents);

    if (pr < 0) {
        if (mmo_sock_errno() == MMO_EINTR)
            return 0;
        set_error(n, "poll");
        return -1;
    }
    if (pr == 0)
        return 0; /* no readiness this frame */

    if (revents & MMO_POLL_ERR) {
        /* Surface the real error via SO_ERROR rather than a bare flag. */
        int err = mmo_sock_error(n->fd);

        if (err <= 0)
            err = MMO_ECONNRESET;
        set_fail(n, n->state == MMO_NET_CONNECTING ? "connect" : "poll",
                 mmo_sock_strerror(err));
        return -1;
    }

    if (n->state == MMO_NET_CONNECTING) {
        if (revents & (MMO_POLL_OUT | MMO_POLL_HUP)) {
            if (finish_connect(n) != 0)
                return -1;
        } else {
            return 0; /* still waiting */
        }
    }

    if (n->state == MMO_NET_CONNECTED) {
        if ((revents & MMO_POLL_OUT) && flush_tx(n) != 0)
            return -1;
        if ((revents & (MMO_POLL_IN | MMO_POLL_HUP)) && fill_rx(n) != 0)
            return -1;
    }
    return 0;
}

int mmo_net_send(mmo_net *n, const void *data, size_t len)
{
    if (n->state != MMO_NET_CONNECTING && n->state != MMO_NET_CONNECTED)
        return -1;
    return buf_append(&n->tx, data, len);
}

size_t mmo_net_recv(mmo_net *n, void *out, size_t max)
{
    size_t have = n->rx.len - n->rx.head;
    size_t take = have < max ? have : max;
    if (take > 0) {
        memcpy(out, n->rx.data + n->rx.head, take);
        n->rx.head += take;
        buf_compact(&n->rx);
    }
    return take;
}

size_t mmo_net_available(const mmo_net *n)
{
    return n->rx.len - n->rx.head;
}

void mmo_net_close(mmo_net *n)
{
    if (n->fd >= 0)
        mmo_sock_close(n->fd);
    buf_free(&n->tx);
    buf_free(&n->rx);
    n->fd = -1;
    n->state = MMO_NET_IDLE;
    n->errmsg[0] = '\0';
}

const char *mmo_net_state_name(mmo_net_state s)
{
    switch (s) {
    case MMO_NET_IDLE:       return "idle";
    case MMO_NET_CONNECTING: return "connecting";
    case MMO_NET_CONNECTED:  return "connected";
    case MMO_NET_CLOSED:     return "closed";
    case MMO_NET_ERROR:      return "error";
    }
    return "?";
}

void mmo_net_attach(mmo_net *n, int fd)
{
    mmo_net_init(n);
    if (mmo_sock_ready() != 0) {
        snprintf(n->errmsg, sizeof n->errmsg,
                 "socket layer unavailable: could not reach it");
        n->state = MMO_NET_ERROR;
        return;
    }
    (void)mmo_sock_nonblock(fd);
    n->fd = fd;
    n->state = MMO_NET_CONNECTED;
}

/* --- one-frame read with an explicit outcome --------------------------- */

mmo_read_result mmo_net_read_frame(mmo_net *n, mmo_buf *acc,
                                   const u8 **body, size_t *blen,
                                   int max_frames, int frame_us)
{
    for (int f = 0; f < max_frames; f++) {
        /* Try to complete a frame from whatever is already buffered. */
        mmo_rbuf r;
        mmo_rbuf_init(&r, acc->data + acc->head, acc->len - acc->head);
        const u8 *p;
        size_t pn;
        mmo_frame_result fr = mmo_frame_get(&r, &p, &pn);
        if (fr == MMO_FRAME_OK) {
            acc->head += r.pos; /* consume the frame; body stays valid to caller */
            *body = p;
            *blen = pn;
            return MMO_READ_OK;
        }
        if (fr == MMO_FRAME_BAD)
            return MMO_READ_BAD;

        /* Short: pump for more, then drain whatever arrived into acc. */
        int pr = mmo_net_pump(n);
        size_t avail = mmo_net_available(n);
        if (avail > 0) {
            if (buf_reserve(acc, avail) != 0)
                return MMO_READ_DROPPED;
            acc->len += mmo_net_recv(n, acc->data + acc->len, avail);
            continue; /* retry the frame decode immediately with the new bytes */
        }
        if (pr != 0)
            return MMO_READ_DROPPED; /* link gone, nothing buffered to finish a frame */
        mmo_plat_sleep_us((unsigned)frame_us);
    }
    return MMO_READ_TIMEOUT;
}

void mmo_net_read_reset(mmo_buf *acc)
{
    buf_free(acc);
}
