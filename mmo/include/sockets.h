/* The socket layer, spelled once for both hosts. */
#ifndef MMO_SOCKETS_H
#define MMO_SOCKETS_H

#include <stddef.h>

#if defined(_WIN32)

#include <winsock2.h>
#include <ws2tcpip.h>

/* Winsock has no MSG_NOSIGNAL because it has no SIGPIPE to suppress. */
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#define MMO_EAGAIN      WSAEWOULDBLOCK
#define MMO_EWOULDBLOCK WSAEWOULDBLOCK
#define MMO_EINPROGRESS WSAEWOULDBLOCK   /* a Winsock connect says WOULDBLOCK */
#define MMO_EINTR       WSAEINTR
#define MMO_ECONNRESET  WSAECONNRESET

#else /* !_WIN32 */

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>

#define MMO_EAGAIN      EAGAIN
#define MMO_EWOULDBLOCK EWOULDBLOCK
#define MMO_EINPROGRESS EINPROGRESS
#define MMO_EINTR       EINTR
#define MMO_ECONNRESET  ECONNRESET

#endif /* _WIN32 */

/*
 * The socket layer, as this client uses it. Descriptors travel as `int` everywhere, including
 * through net.h, which the game half does include, and -1 is the one invalid value.
 */

/* Ready the layer. 0 when sockets can be used, -1 when they cannot; the
 * caller fails loudly rather than dispatching into something dead. */
int mmo_sock_ready(void);

/* A non-blocking stream socket, or -1. */
int mmo_sock_open(int family, int type, int protocol);

int  mmo_sock_connect(int fd, const struct sockaddr *sa, unsigned salen);
long mmo_sock_recv(int fd, void *buf, size_t len, int flags);
long mmo_sock_send(int fd, const void *buf, size_t len, int flags);
int  mmo_sock_error(int fd);          /* SO_ERROR, or -1 if it cannot be read */
void mmo_sock_nodelay(int fd);
int  mmo_sock_nonblock(int fd);       /* for a descriptor from somewhere else */
void mmo_sock_close(int fd);

/* Readiness for one socket, with a zero timeout: never blocks the frame.
 * `want_write` asks about writability as well. The answer is the same bit set
 * on both hosts. */
#define MMO_POLL_IN   0x01
#define MMO_POLL_OUT  0x02
#define MMO_POLL_ERR  0x04
#define MMO_POLL_HUP  0x08
int mmo_sock_poll(int fd, int want_write, int *revents);

/* The last socket error, and what to call it. Windows keeps this somewhere
 * other than errno, which is the whole reason these exist. */
int         mmo_sock_errno(void);
const char *mmo_sock_strerror(int err);

/* Name resolution and address text, through the same door as everything else. */
int         mmo_sock_resolve(const char *host, const char *port,
                             struct addrinfo **out);
void        mmo_sock_free_resolved(struct addrinfo *ai);
const char *mmo_sock_resolve_error(int err);
int         mmo_sock_pton(int family, const char *text, void *addr);
const char *mmo_sock_ntop(int family, const void *addr, char *out, size_t cap);

#endif /* MMO_SOCKETS_H */
