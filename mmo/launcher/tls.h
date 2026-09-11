/* One TLS client session under the update fetch, on mbedTLS. */

#ifndef OPENMMO_TLS_H
#define OPENMMO_TLS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mmo_tls mmo_tls;

/*
 * What a wire call, and what every call below, may answer besides a byte count: the socket has
 * to become readable or writable before anything more can happen, or the attempt is over and
 * `err` says why.
 */
#define MMO_TLS_ERR        (-1)
#define MMO_TLS_WANT_READ  (-2)
#define MMO_TLS_WANT_WRITE (-3)

/* The fetch's two wire calls. Each moves what it can and returns the count,
 * 0 for the peer's orderly close (recv only), or one of the three above. */
typedef long (*mmo_tls_send_fn)(void *ud, const void *buf, size_t n);
typedef long (*mmo_tls_recv_fn)(void *ud, void *buf, size_t n);

/*
 * A client session for `host`, the name sent as SNI and the one the certificate must carry,
 * over the caller's wire. `ca`, when not NULL or empty, is a PEM file of certificates
 * trusted beside the compiled-in roots.
 */
mmo_tls *mmo_tls_open(const char *host, const char *ca,
                      mmo_tls_send_fn send, mmo_tls_recv_fn recv, void *ud,
                      char *err, size_t errcap);

/* Drive the handshake. 0 once the session is up and the certificate has
 * passed; MMO_TLS_WANT_READ / _WRITE to be called again once the socket is
 * ready; MMO_TLS_ERR with the one line a player or operator is shown. */
int mmo_tls_handshake(mmo_tls *t, char *err, size_t errcap);

/* Application bytes, after the handshake. >0 moved (send may move fewer than
 * asked), 0 for the peer's orderly close (recv), or one of the three. */
long mmo_tls_send(mmo_tls *t, const void *data, size_t n,
                  char *err, size_t errcap);
long mmo_tls_recv(mmo_tls *t, void *buf, size_t cap, char *err, size_t errcap);

/* A best-effort close_notify, then everything freed. NULL is fine. */
void mmo_tls_close(mmo_tls *t);

#ifdef __cplusplus
}
#endif

#endif /* OPENMMO_TLS_H */
