/*
 * The two spellings of the socket layer. sockets.h carries the
 * reasoning, including why only the Linux half needs a shim at all.
 */

#include "sockets.h"

#include <stdio.h>
#include <string.h>

#if defined(_WIN32)

/* ================================================================== */
/* Windows                                                             */
/* ================================================================== */

/* WS2_32.DLL is loaded by hand, and that is load-bearing. */
#define WSFN(ret, name, args) static ret (WSAAPI *p_##name) args

WSFN(SOCKET, socket, (int, int, int));
WSFN(int, connect, (SOCKET, const struct sockaddr *, int));
WSFN(int, recv, (SOCKET, char *, int, int));
WSFN(int, send, (SOCKET, const char *, int, int));
WSFN(int, getsockopt, (SOCKET, int, int, char *, int *));
WSFN(int, setsockopt, (SOCKET, int, int, const char *, int));
WSFN(int, closesocket, (SOCKET));
WSFN(int, ioctlsocket, (SOCKET, long, u_long *));
WSFN(int, WSAPoll, (LPWSAPOLLFD, ULONG, INT));
WSFN(int, WSAStartup, (WORD, LPWSADATA));
WSFN(int, WSAGetLastError, (void));
WSFN(int, getaddrinfo, (const char *, const char *, const struct addrinfo *,
                        struct addrinfo **));
WSFN(void, freeaddrinfo, (struct addrinfo *));
WSFN(int, inet_pton, (int, const char *, void *));
WSFN(const char *, inet_ntop, (int, const void *, char *, size_t));

#undef WSFN

static int started;

int mmo_sock_ready(void)
{
    WSADATA wsa;
    HMODULE lib;

    if (started)
        return 0;

    lib = LoadLibraryA("ws2_32.dll");
    if (lib == NULL)
        return -1;

#define BIND(name) \
    do { \
        *(FARPROC *)&p_##name = GetProcAddress(lib, #name); \
        if (p_##name == NULL) \
            return -1; \
    } while (0)

    BIND(socket);       BIND(connect);      BIND(recv);
    BIND(send);         BIND(getsockopt);   BIND(setsockopt);
    BIND(closesocket);  BIND(ioctlsocket);  BIND(WSAPoll);
    BIND(WSAStartup);   BIND(WSAGetLastError);
    BIND(getaddrinfo);  BIND(freeaddrinfo);
    BIND(inet_pton);    BIND(inet_ntop);

#undef BIND

    if (p_WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
        return -1;
    started = 1;
    return 0;
}

int mmo_sock_open(int family, int type, int protocol)
{
    SOCKET s;
    u_long nb = 1;

    if (mmo_sock_ready() != 0)
        return -1;
    s = p_socket(family, type, protocol);
    if (s == INVALID_SOCKET)
        return -1;
    /* Winsock has no SOCK_NONBLOCK to fold into the type, so it is a second
     * call, and a socket that stayed blocking would park the render loop on
     * the first connect. A failure here closes it rather than handing back
     * something that behaves differently from every other socket in here. */
    if (p_ioctlsocket(s, FIONBIO, &nb) != 0) {
        p_closesocket(s);
        return -1;
    }
    return (int)s;
}

int mmo_sock_connect(int fd, const struct sockaddr *sa, unsigned salen)
{
    return p_connect((SOCKET)fd, sa, (int)salen);
}

long mmo_sock_recv(int fd, void *buf, size_t len, int flags)
{
    return p_recv((SOCKET)fd, (char *)buf, (int)len, flags);
}

long mmo_sock_send(int fd, const void *buf, size_t len, int flags)
{
    return p_send((SOCKET)fd, (const char *)buf, (int)len, flags);
}

int mmo_sock_error(int fd)
{
    int err = 0, len = (int)sizeof err;

    if (p_getsockopt((SOCKET)fd, SOL_SOCKET, SO_ERROR, (char *)&err, &len) != 0)
        return -1;
    return err;
}

void mmo_sock_nodelay(int fd)
{
    int one = 1;

    p_setsockopt((SOCKET)fd, IPPROTO_TCP, TCP_NODELAY, (const char *)&one,
                 (int)sizeof one);
}

int mmo_sock_nonblock(int fd)
{
    u_long nb = 1;

    return p_ioctlsocket((SOCKET)fd, FIONBIO, &nb) == 0 ? 0 : -1;
}

void mmo_sock_close(int fd)
{
    p_closesocket((SOCKET)fd);
}

int mmo_sock_poll(int fd, int want_write, int *revents)
{
    struct pollfd p;
    int rc, out = 0;

    p.fd = (SOCKET)fd;
    p.events = (short)(POLLIN | (want_write ? POLLOUT : 0));
    p.revents = 0;
    rc = p_WSAPoll(&p, 1, 0);
    if (rc <= 0) {
        *revents = 0;
        return rc;
    }
    if (p.revents & POLLIN)   out |= MMO_POLL_IN;
    if (p.revents & POLLOUT)  out |= MMO_POLL_OUT;
    if (p.revents & POLLERR)  out |= MMO_POLL_ERR;
    if (p.revents & POLLNVAL) out |= MMO_POLL_ERR;
    if (p.revents & POLLHUP)  out |= MMO_POLL_HUP;
    *revents = out;
    return rc;
}

int mmo_sock_errno(void)
{
    return p_WSAGetLastError != NULL ? p_WSAGetLastError() : 0;
}

int mmo_sock_resolve(const char *host, const char *port, struct addrinfo **out)
{
    struct addrinfo hints;

    if (mmo_sock_ready() != 0)
        return EAI_FAIL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    return p_getaddrinfo(host, port, &hints, out);
}

void mmo_sock_free_resolved(struct addrinfo *ai)
{
    if (ai != NULL && p_freeaddrinfo != NULL)
        p_freeaddrinfo(ai);
}

const char *mmo_sock_resolve_error(int err)
{
    /* gai_strerrorA is an inline in the mingw headers that calls into the
     * library; the message this build can always produce is the code's. */
    return mmo_sock_strerror(err);
}

int mmo_sock_pton(int family, const char *text, void *addr)
{
    if (mmo_sock_ready() != 0)
        return -1;
    return p_inet_pton(family, text, addr);
}

const char *mmo_sock_ntop(int family, const void *addr, char *out, size_t cap)
{
    if (mmo_sock_ready() != 0)
        return NULL;
    return p_inet_ntop(family, addr, out, cap);
}

const char *mmo_sock_strerror(int err)
{
    static char buf[192];
    DWORD n;

    n = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM |
                       FORMAT_MESSAGE_IGNORE_INSERTS,
                       NULL, (DWORD)err,
                       MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                       buf, (DWORD)sizeof buf, NULL);
    if (n == 0) {
        snprintf(buf, sizeof buf, "winsock error %d", err);
        return buf;
    }
    /* FormatMessage ends its sentences with CRLF and a full stop, which reads
     * badly in the middle of one of ours. */
    while (n > 0 && (buf[n - 1] == '\r' || buf[n - 1] == '\n' ||
                     buf[n - 1] == '.' || buf[n - 1] == ' '))
        buf[--n] = '\0';
    return buf;
}

#else /* !_WIN32 */

/* ================================================================== */
/* POSIX                                                               */
/* ================================================================== */

#include <stdlib.h>
#include <sys/syscall.h>

/* The six entry points the DS stack shadows, taken as SYSCALLS. */
#if !defined(SYS_socket) || !defined(SYS_connect) || !defined(SYS_recvfrom) || \
    !defined(SYS_sendto) || !defined(SYS_getsockopt) || !defined(SYS_setsockopt)
#error "no individual socket syscalls on this target; see the note above"
#endif

/* Nothing to ready on this host. Kept because the Windows half genuinely has
 * something to do here (WSAStartup) and every caller asks both the same way. */
int mmo_sock_ready(void)
{
    return 0;
}

static int libc_socket(int family, int type, int protocol)
{
    return (int)syscall(SYS_socket, (long)family, (long)type, (long)protocol);
}

static int libc_connect(int fd, const struct sockaddr *sa, socklen_t len)
{
    return (int)syscall(SYS_connect, (long)fd, (long)sa, (long)len);
}

static ssize_t libc_recv(int fd, void *buf, size_t len, int flags)
{
    return (ssize_t)syscall(SYS_recvfrom, (long)fd, (long)buf, (long)len,
                            (long)flags, (long)NULL, (long)NULL);
}

static ssize_t libc_send(int fd, const void *buf, size_t len, int flags)
{
    return (ssize_t)syscall(SYS_sendto, (long)fd, (long)buf, (long)len,
                            (long)flags, (long)NULL, 0L);
}

static int libc_getsockopt(int fd, int level, int name, void *val,
                           socklen_t *len)
{
    return (int)syscall(SYS_getsockopt, (long)fd, (long)level, (long)name,
                        (long)val, (long)len);
}

static int libc_setsockopt(int fd, int level, int name, const void *val,
                           socklen_t len)
{
    return (int)syscall(SYS_setsockopt, (long)fd, (long)level, (long)name,
                        (long)val, (long)len);
}

int mmo_sock_open(int family, int type, int protocol)
{
    if (mmo_sock_ready() != 0)
        return -1;
    return libc_socket(family, type | SOCK_NONBLOCK, protocol);
}

int mmo_sock_connect(int fd, const struct sockaddr *sa, unsigned salen)
{
    return libc_connect(fd, sa, (socklen_t)salen);
}

long mmo_sock_recv(int fd, void *buf, size_t len, int flags)
{
    return (long)libc_recv(fd, buf, len, flags);
}

long mmo_sock_send(int fd, const void *buf, size_t len, int flags)
{
    return (long)libc_send(fd, buf, len, flags);
}

int mmo_sock_error(int fd)
{
    int err = 0;
    socklen_t elen = sizeof err;

    if (libc_getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &elen) != 0)
        return -1;
    return err;
}

void mmo_sock_nodelay(int fd)
{
    int one = 1;

    libc_setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
}

int mmo_sock_nonblock(int fd)
{
    int fl = fcntl(fd, F_GETFL, 0);

    if (fl < 0)
        return -1;
    return fcntl(fd, F_SETFL, fl | O_NONBLOCK) == 0 ? 0 : -1;
}

void mmo_sock_close(int fd)
{
    close(fd);
}

int mmo_sock_poll(int fd, int want_write, int *revents)
{
    struct pollfd p;
    int rc, out = 0;

    p.fd = fd;
    p.events = (short)(POLLIN | (want_write ? POLLOUT : 0));
    p.revents = 0;
    rc = poll(&p, 1, 0); /* zero timeout: never blocks the frame */
    if (rc <= 0) {
        *revents = 0;
        return rc;
    }
    if (p.revents & POLLIN)   out |= MMO_POLL_IN;
    if (p.revents & POLLOUT)  out |= MMO_POLL_OUT;
    if (p.revents & POLLERR)  out |= MMO_POLL_ERR;
    if (p.revents & POLLNVAL) out |= MMO_POLL_ERR;
    if (p.revents & POLLHUP)  out |= MMO_POLL_HUP;
    *revents = out;
    return rc;
}

int mmo_sock_errno(void)
{
    return errno;
}

const char *mmo_sock_strerror(int err)
{
    return strerror(err);
}

int mmo_sock_resolve(const char *host, const char *port, struct addrinfo **out)
{
    struct addrinfo hints;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    return getaddrinfo(host, port, &hints, out);
}

void mmo_sock_free_resolved(struct addrinfo *ai)
{
    if (ai != NULL)
        freeaddrinfo(ai);
}

const char *mmo_sock_resolve_error(int err)
{
    return gai_strerror(err);
}

int mmo_sock_pton(int family, const char *text, void *addr)
{
    return inet_pton(family, text, addr);
}

const char *mmo_sock_ntop(int family, const void *addr, char *out, size_t cap)
{
    return inet_ntop(family, addr, out, (socklen_t)cap);
}

#endif /* _WIN32 */
