/* Non-blocking TCP transport for the native client. */
#ifndef MMO_NET_H
#define MMO_NET_H

#include <stddef.h>

#include "mmo.h"

typedef enum {
    MMO_NET_IDLE = 0,   /* no socket */
    MMO_NET_CONNECTING, /* connect() in flight, waiting for writability */
    MMO_NET_CONNECTED,  /* established; may send and recv */
    MMO_NET_CLOSED,     /* peer closed cleanly (EOF) */
    MMO_NET_ERROR       /* fatal error; see .errmsg */
} mmo_net_state;

/* A growable byte buffer with a read cursor, so partial reads/writes leave the
 * unconsumed tail in place without recopying on every operation. */
typedef struct {
    u8    *data;
    size_t head; /* consumed prefix (bytes [0,head) are spent) */
    size_t len;  /* total valid bytes; live data is [head,len) */
    size_t cap;  /* allocated capacity */
} mmo_buf;

typedef struct {
    int           fd;
    mmo_net_state state;
    mmo_buf       tx; /* queued for send, drained on pump */
    mmo_buf       rx; /* received, awaiting the caller's recv */
    char          errmsg[128];
} mmo_net;

/* Reset a connection to IDLE with empty buffers and no socket. */
void mmo_net_init(mmo_net *n);

/* Resolve host and start a non-blocking connect to (host, port). Moves state to
 * MMO_NET_CONNECTING (or MMO_NET_CONNECTED if the kernel completed it inline).
 * Returns 0 on success, -1 on immediate failure (state MMO_NET_ERROR, errmsg set). */
int mmo_net_connect(mmo_net *n, const char *host, u16 port);

/* Advance the connection without blocking: finish a pending connect, flush the
 * send queue, and read any available bytes into rx. Returns 0 while the
 * connection is live (CONNECTING/CONNECTED), -1 once it is CLOSED or ERROR. */
int mmo_net_pump(mmo_net *n);

/* Queue bytes for sending. They go out on the next pump(s). Returns 0, or -1 if
 * the connection is not live. */
int mmo_net_send(mmo_net *n, const void *data, size_t len);

/* Copy up to max received bytes into out, consuming them. Returns the count. */
size_t mmo_net_recv(mmo_net *n, void *out, size_t max);

/* Received bytes waiting to be drained by mmo_net_recv(). */
size_t mmo_net_available(const mmo_net *n);

/* Bytes queued by mmo_net_send() that the socket has not taken yet. */
size_t mmo_net_pending(const mmo_net *n);

/* Wait, up to ms milliseconds, for the queue to actually reach the socket.
 * Returns what is still queued, so 0 means everything went. Anything sent in
 * the same breath as a close needs this: close() frees the queue rather than
 * sending it, and a pump only writes on a poll that says the socket is ready. */
size_t mmo_net_drain(mmo_net *n, int ms);

/* Close the socket and free buffers, returning to MMO_NET_IDLE. */
void mmo_net_close(mmo_net *n);

/* Human-readable state name, for logging. */
const char *mmo_net_state_name(mmo_net_state s);

/* Adopt an already-connected file descriptor as a live connection, switching it
 * to non-blocking. For wiring a socketpair or a pre-opened socket into the pump
 * loop without going through connect(); the tests use it to stand in a peer. */
void mmo_net_attach(mmo_net *n, int fd);

/* --- one-frame read with an explicit outcome --------------------------- */

/* The result of trying to read a whole frame within a budget. The point of the
 * enum is that a link that DROPS is never reported as a TIMEOUT: teardown and a
 * silent peer are different failures and each is surfaced as itself. */
typedef enum {
    MMO_READ_OK = 0,   /* a full frame body is available */
    MMO_READ_TIMEOUT,  /* the budget expired while the link was still live */
    MMO_READ_DROPPED,  /* the link closed or errored before a whole frame */
    MMO_READ_BAD       /* a malformed frame (length prefix < 2) */
} mmo_read_result;

/*
 * Read exactly one whole frame without blocking. `acc` is a caller-owned buffer that
 * accumulates partial input across calls; zero it before the first read and release it with
 * mmo_net_read_reset when done.
 */
mmo_read_result mmo_net_read_frame(mmo_net *n, mmo_buf *acc,
                                   const u8 **body, size_t *blen,
                                   int max_frames, int frame_us);

/* Release an accumulator filled by mmo_net_read_frame. */
void mmo_net_read_reset(mmo_buf *acc);

#endif /* MMO_NET_H */
