/* One HTTP GET over the client's own socket layer, plain or under TLS. */

#include "fetch.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform.h"
#include "sockets.h"
#include "tls.h"

/* One transfer stalls out after this long with no bytes moving. */
#define FETCH_STALL_SECONDS 30
/* Redirects followed before the server is said to be going in circles. */
#define FETCH_MAX_HOPS 3
/* A path with a query, as long as a Location may make one. */
#define FETCH_PATHMAX 2048
/* fetch_once's answer when a redirect wants following. */
#define FETCH_REDIRECT (-2)

static int fail(char *err, size_t cap, const char *fmt, const char *a,
                const char *b)
{
    if (err != NULL && cap > 0)
        snprintf(err, cap, fmt, a, b);
    return -1;
}

/* The same, only where nothing has been said yet: the TLS layer names its
 * own failures and those words are the ones a player should see. */
static int fail_quiet(char *err, size_t cap, const char *fmt, const char *a,
                      const char *b)
{
    if (err != NULL && cap > 0 && err[0] != '\0')
        return -1;
    return fail(err, cap, fmt, a, b);
}

/*
 * `scheme://host[:port]rest` into its parts; `rest` is everything from the
 * first slash on, or "". Returns 0, or -1 with the reason in `err`.
 */
static int parse_url(const char *url, int *tls, char *host, size_t hostcap,
                     char *port, size_t portcap, char *rest, size_t restcap,
                     char *err, size_t errcap)
{
    const char *p, *slash, *colon;
    size_t n;

    if (url == NULL || url[0] == '\0')
        return fail(err, errcap, "no update URL is configured", NULL, NULL);
    if (strncmp(url, "https://", 8) == 0) {
        *tls = 1;
        p = url + 8;
    } else if (strncmp(url, "http://", 7) == 0) {
        *tls = 0;
        p = url + 7;
    } else {
        return fail(err, errcap, "%.200s is not an http:// or https:// URL",
                    url, NULL);
    }
    slash = strchr(p, '/');
    colon = memchr(p, ':', slash != NULL ? (size_t)(slash - p) : strlen(p));
    n = (colon != NULL ? (size_t)(colon - p)
                       : slash != NULL ? (size_t)(slash - p) : strlen(p));
    if (n == 0 || n >= hostcap)
        return fail(err, errcap, "%.200s names no host", url, NULL);
    memcpy(host, p, n);
    host[n] = '\0';
    if (colon != NULL) {
        const char *pe = slash != NULL ? slash : colon + strlen(colon);
        size_t pn = (size_t)(pe - colon - 1);

        if (pn == 0 || pn >= portcap ||
            strspn(colon + 1, "0123456789") < pn)
            return fail(err, errcap, "%.200s has a port that is not a number",
                        url, NULL);
        memcpy(port, colon + 1, pn);
        port[pn] = '\0';
    } else {
        snprintf(port, portcap, "%s", *tls ? "443" : "80");
    }
    if (slash == NULL) {
        if (restcap > 0)
            rest[0] = '\0';
        return 0;
    }
    if (strlen(slash) >= restcap)
        return fail(err, errcap, "%.200s has a path too long to use", url, NULL);
    memcpy(rest, slash, strlen(slash) + 1);
    return 0;
}

int mmo_fetch_split(const char *url, mmo_fetch_origin *o,
                    char *err, size_t errcap)
{
    size_t n;

    memset(o, 0, sizeof *o);
    if (parse_url(url, &o->tls, o->host, sizeof o->host, o->port,
                  sizeof o->port, o->base, sizeof o->base, err, errcap) != 0)
        return -1;
    n = strlen(o->base);
    while (n > 1 && o->base[n - 1] == '/')
        n--;
    o->base[n] = '\0';
    if (strcmp(o->base, "/") == 0)
        o->base[0] = '\0';
    return 0;
}

/* ---------------------------------------------------------------------- */

/* Where a body's bytes go: a caller's buffer, or a file. Returns 0, or -1
 * when the sink cannot take them. */
typedef int (*sink_fn)(void *ud, const void *data, size_t n);

struct memsink {
    char  *buf;
    size_t cap, at;
};

static int mem_sink(void *ud, const void *data, size_t n)
{
    struct memsink *m = ud;

    if (n > m->cap - m->at)
        return -1;
    memcpy(m->buf + m->at, data, n);
    m->at += n;
    return 0;
}

struct filesink {
    FILE *f;
    long  cap, at;
    void (*tick)(void *ud, long got, long total);
    void  *tickud;
};

static int file_sink(void *ud, const void *data, size_t n)
{
    struct filesink *s = ud;

    if ((long)n > s->cap - s->at)
        return -1;
    if (fwrite(data, 1, n, s->f) != n)
        return -1;
    s->at += (long)n;
    if (s->tick != NULL)
        s->tick(s->tickud, s->at, s->cap);
    return 0;
}

/* ---------------------------------------------------------------------- */

/* One connection: the socket, and the TLS session over it when the origin
 * is https. The deadline moves forward whenever bytes do. */
struct conn {
    int fd;
    mmo_tls *tls;
    long deadline;
    char *err;
    size_t errcap;
};

/* Wait until the socket is readable (or writable, when asked), sleeping in
 * millisecond steps; the poll itself never blocks. -1 on error or stall. */
static int wait_io(int fd, int want_write, long deadline)
{
    int rev;

    for (;;) {
        if (mmo_sock_poll(fd, want_write, &rev) < 0)
            return -1;
        if ((rev & MMO_POLL_ERR) != 0)
            return -1;
        if ((rev & (want_write ? MMO_POLL_OUT : MMO_POLL_IN)) != 0 ||
            (rev & MMO_POLL_HUP) != 0)
            return 0;
        if (mmo_plat_seconds() >= deadline)
            return -1;
        mmo_plat_sleep_us(2000);
    }
}

/* The two wire calls TLS is handed: one attempt each, never waiting, in the
 * words tls.h defines. */
static long wire_send(void *ud, const void *buf, size_t n)
{
    struct conn *c = ud;
    long w = mmo_sock_send(c->fd, buf, n, MSG_NOSIGNAL);
    int e;

    if (w > 0)
        return w;
    if (w == 0)
        return MMO_TLS_WANT_WRITE;
    e = mmo_sock_errno();
    if (e == MMO_EAGAIN || e == MMO_EWOULDBLOCK || e == MMO_EINTR)
        return MMO_TLS_WANT_WRITE;
    return MMO_TLS_ERR;
}

static long wire_recv(void *ud, void *buf, size_t n)
{
    struct conn *c = ud;
    long r = mmo_sock_recv(c->fd, buf, n, 0);
    int e;

    if (r >= 0)
        return r;
    e = mmo_sock_errno();
    if (e == MMO_EAGAIN || e == MMO_EWOULDBLOCK || e == MMO_EINTR)
        return MMO_TLS_WANT_READ;
    return MMO_TLS_ERR;
}

/* Let the TLS layer's "not yet" become a wait on the socket. 0 to try
 * again, -1 on a stall. */
static int tls_wait(struct conn *c, long want)
{
    return wait_io(c->fd, want == MMO_TLS_WANT_WRITE, c->deadline);
}

static int conn_send_all(struct conn *c, const char *data, size_t n)
{
    while (n > 0) {
        long w;

        if (c->tls != NULL) {
            w = mmo_tls_send(c->tls, data, n, c->err, c->errcap);
            if (w == MMO_TLS_WANT_READ || w == MMO_TLS_WANT_WRITE) {
                if (tls_wait(c, w) != 0)
                    return -1;
                continue;
            }
            if (w < 0)
                return -1;
            if (w == 0)
                continue;
        } else {
            w = mmo_sock_send(c->fd, data, n, MSG_NOSIGNAL);
            if (w == 0)
                return -1;
            if (w < 0) {
                int e = mmo_sock_errno();

                if (e != MMO_EAGAIN && e != MMO_EWOULDBLOCK && e != MMO_EINTR)
                    return -1;
                if (wait_io(c->fd, 1, c->deadline) != 0)
                    return -1;
                continue;
            }
        }
        data += w;
        n -= (size_t)w;
        c->deadline = mmo_plat_seconds() + FETCH_STALL_SECONDS;
    }
    return 0;
}

/* One recv, waiting for readiness first. >0 bytes, 0 on orderly close, -1 on
 * error or stall. */
static long conn_recv_some(struct conn *c, char *buf, size_t cap)
{
    for (;;) {
        long r;

        if (c->tls != NULL) {
            r = mmo_tls_recv(c->tls, buf, cap, c->err, c->errcap);
            if (r == MMO_TLS_WANT_READ || r == MMO_TLS_WANT_WRITE) {
                if (tls_wait(c, r) != 0)
                    return -1;
                continue;
            }
            if (r < 0)
                return -1;
        } else {
            if (wait_io(c->fd, 0, c->deadline) != 0)
                return -1;
            r = mmo_sock_recv(c->fd, buf, cap, 0);
            if (r < 0) {
                int e = mmo_sock_errno();

                if (e != MMO_EAGAIN && e != MMO_EWOULDBLOCK && e != MMO_EINTR)
                    return -1;
                continue;
            }
        }
        if (r > 0)
            c->deadline = mmo_plat_seconds() + FETCH_STALL_SECONDS;
        return r;
    }
}

static void conn_close(struct conn *c)
{
    mmo_tls_close(c->tls);
    c->tls = NULL;
    if (c->fd >= 0)
        mmo_sock_close(c->fd);
    c->fd = -1;
}

/* A header's value, matched without case, out of the raw header block. */
static int header_value(const char *hdr, const char *name, char *out,
                        size_t cap)
{
    size_t n = strlen(name);
    const char *p = hdr;

    while (p != NULL && *p != '\0') {
        if (strncasecmp(p, name, n) == 0 && p[n] == ':') {
            const char *v = p + n + 1, *e;
            size_t vn;

            while (*v == ' ' || *v == '\t')
                v++;
            e = strstr(v, "\r\n");
            vn = e != NULL ? (size_t)(e - v) : strlen(v);
            if (vn >= cap)
                vn = cap - 1;
            memcpy(out, v, vn);
            out[vn] = '\0';
            return 1;
        }
        p = strstr(p, "\r\n");
        if (p != NULL)
            p += 2;
    }
    return 0;
}

/*
 * The transfer, once the response head is in hand: either a known length, a chunked stream, or
 * bytes until the server closes. `first` is body bytes already read past the head.
 */
static long read_body(struct conn *c, const char *hdr, const char *first,
                      size_t firstlen, sink_fn sink, void *ud,
                      char *err, size_t errcap)
{
    char buf[8192], val[64];
    long want = -1, got = 0;
    int chunked = 0;

    if (header_value(hdr, "Transfer-Encoding", val, sizeof val) == 1)
        chunked = strcasecmp(val, "identity") != 0;
    else if (header_value(hdr, "Content-Length", val, sizeof val) == 1) {
        char *endp = NULL;

        want = strtol(val, &endp, 10);
        if (endp == val || *endp != '\0' || want < 0) {
            fail(err, errcap, "the server sent an unreadable Content-Length",
                 NULL, NULL);
            return -1;
        }
    }

    if (chunked) {
        /* Decoded with a little state machine over the same recv loop:
         * size line, that many bytes, CRLF, until a zero-size chunk. */
        char line[32];
        size_t linelen = 0, span = 0;
        long left = -1;                    /* -1: reading a size line */
        const char *p = first;
        size_t n = firstlen;

        for (;;) {
            size_t i = 0;

            while (i < n) {
                if (left < 0) {
                    char ch = p[i++];

                    if (ch == '\n') {
                        line[linelen] = '\0';
                        linelen = 0;
                        left = strtol(line, NULL, 16);
                        if (left == 0) {
                            /* The trailer is not read; the body is done. */
                            return got;
                        }
                    } else if (ch != '\r' && linelen < sizeof line - 1) {
                        /* A chunk extension after ';' rides along and strtol
                         * stops at it. */
                        line[linelen++] = ch;
                    }
                } else if (left == 0) {
                    /* The CRLF after a chunk's bytes. */
                    if (p[i++] == '\n')
                        left = -1;
                } else {
                    span = n - i < (size_t)left ? n - i : (size_t)left;
                    if (sink(ud, p + i, span) != 0) {
                        fail(err, errcap,
                             "the download is larger than the feed says",
                             NULL, NULL);
                        return -1;
                    }
                    got += (long)span;
                    left -= (long)span;
                    i += span;
                }
            }
            {
                long r = conn_recv_some(c, buf, sizeof buf);

                if (r <= 0) {
                    fail_quiet(err, errcap,
                               "the connection ended mid-download", NULL, NULL);
                    return -1;
                }
                p = buf;
                n = (size_t)r;
            }
        }
    }

    if (firstlen > 0) {
        if (sink(ud, first, firstlen) != 0) {
            fail(err, errcap, "the download is larger than the feed says",
                 NULL, NULL);
            return -1;
        }
        got = (long)firstlen;
    }
    while (want < 0 || got < want) {
        long r = conn_recv_some(c, buf, sizeof buf);

        if (r < 0) {
            fail_quiet(err, errcap, "the connection ended mid-download", NULL,
                       NULL);
            return -1;
        }
        if (r == 0)
            break;
        if (sink(ud, buf, (size_t)r) != 0) {
            fail(err, errcap, "the download is larger than the feed says",
                 NULL, NULL);
            return -1;
        }
        got += r;
    }
    if (want >= 0 && got != want) {
        fail(err, errcap, "the connection ended mid-download", NULL, NULL);
        return -1;
    }
    return got;
}

/*
 * One request to one origin. The body's length on a 200; FETCH_REDIRECT with
 * the Location in `loc` on a redirect this fetch may follow; -1 otherwise.
 */
static long fetch_once(const mmo_fetch_origin *o, const char *path,
                       sink_fn sink, void *ud, char *loc, size_t loccap,
                       char *err, size_t errcap)
{
    /* Room for a base, an escaped feed name and a hash query, with slack. */
    char req[FETCH_PATHMAX + 512];
    char head[8192], val[FETCH_PATHMAX];
    struct addrinfo *ai = NULL, *a;
    struct conn c;
    long r, body;
    size_t at = 0;
    char *sep;
    int code, rc, defport;

    if (err != NULL && errcap > 0)
        err[0] = '\0';
    c.fd = -1;
    c.tls = NULL;
    c.err = err;
    c.errcap = errcap;
    if (mmo_sock_ready() != 0)
        return fail(err, errcap, "sockets cannot be used on this host", NULL,
                    NULL);
    rc = mmo_sock_resolve(o->host, o->port, &ai);
    if (rc != 0)
        return fail(err, errcap, "%.200s cannot be found: %.100s", o->host,
                    mmo_sock_resolve_error(rc));
    c.deadline = mmo_plat_seconds() + FETCH_STALL_SECONDS;
    for (a = ai; a != NULL; a = a->ai_next) {
        c.fd = mmo_sock_open(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (c.fd < 0)
            continue;
        if (mmo_sock_connect(c.fd, a->ai_addr, (unsigned)a->ai_addrlen) == 0)
            break;
        {
            int e = mmo_sock_errno();

            if ((e == MMO_EINPROGRESS || e == MMO_EAGAIN) &&
                wait_io(c.fd, 1, c.deadline) == 0 && mmo_sock_error(c.fd) == 0)
                break;
        }
        mmo_sock_close(c.fd);
        c.fd = -1;
    }
    mmo_sock_free_resolved(ai);
    if (a == NULL || c.fd < 0)
        return fail(err, errcap, "%.200s:%.20s does not answer", o->host,
                    o->port);
    mmo_sock_nodelay(c.fd);

    if (o->tls) {
        c.tls = mmo_tls_open(o->host, o->ca, wire_send, wire_recv, &c, err,
                             errcap);
        if (c.tls == NULL) {
            conn_close(&c);
            return -1;
        }
        for (;;) {
            rc = mmo_tls_handshake(c.tls, err, errcap);
            if (rc == 0)
                break;
            if (rc == MMO_TLS_WANT_READ || rc == MMO_TLS_WANT_WRITE) {
                if (tls_wait(&c, rc) == 0)
                    continue;
                fail_quiet(err, errcap, "%.200s stopped answering during the "
                                        "TLS handshake", o->host, NULL);
            }
            conn_close(&c);
            return -1;
        }
    }

    defport = strcmp(o->port, o->tls ? "443" : "80") == 0;
    snprintf(req, sizeof req,
             "GET %s HTTP/1.1\r\n"
             "Host: %s%s%s\r\n"
             "User-Agent: openmmo-launch\r\n"
             "Accept: */*\r\n"
             "Connection: close\r\n\r\n",
             path[0] != '\0' ? path : "/", o->host,
             defport ? "" : ":", defport ? "" : o->port);
    if (conn_send_all(&c, req, strlen(req)) != 0) {
        conn_close(&c);
        return fail_quiet(err, errcap, "%.200s stopped answering", o->host,
                          NULL);
    }

    /* The head, up to the blank line; what follows it is body. */
    sep = NULL;
    while (sep == NULL) {
        if (at >= sizeof head - 1) {
            conn_close(&c);
            return fail(err, errcap, "the server's response head never ends",
                        NULL, NULL);
        }
        r = conn_recv_some(&c, head + at, sizeof head - 1 - at);
        if (r <= 0) {
            conn_close(&c);
            return fail_quiet(err, errcap, "%.200s stopped answering", o->host,
                              NULL);
        }
        at += (size_t)r;
        head[at] = '\0';
        sep = strstr(head, "\r\n\r\n");
    }
    *sep = '\0';

    if (sscanf(head, "HTTP/%*d.%*d %d", &code) != 1) {
        conn_close(&c);
        return fail(err, errcap, "the server did not answer with HTTP", NULL,
                    NULL);
    }
    if (code != 200) {
        conn_close(&c);
        if ((code == 301 || code == 302 || code == 303 || code == 307 ||
             code == 308) &&
            header_value(head, "Location", loc, loccap) == 1)
            return FETCH_REDIRECT;
        if (code >= 300 && code < 400)
            return fail(err, errcap, "the server redirects %.300s and does "
                                     "not say where", path, NULL);
        snprintf(val, sizeof val, "%d", code);
        return fail(err, errcap, "the server answered %.20s for %.300s", val,
                    path);
    }

    body = read_body(&c, head, sep + 4, at - (size_t)(sep + 4 - head), sink,
                     ud, err, errcap);
    conn_close(&c);
    return body;
}

/*
 * Turn a Location into the next origin and path, under the redirect rule at
 * the top of this file. Returns 0, or -1 with the reason in `err`.
 */
static int follow(mmo_fetch_origin *o, char *path, size_t pathcap,
                  const char *loc, char *err, size_t errcap)
{
    if (strncmp(loc, "http://", 7) == 0 || strncmp(loc, "https://", 8) == 0) {
        mmo_fetch_origin n;

        memset(&n, 0, sizeof n);
        if (parse_url(loc, &n.tls, n.host, sizeof n.host, n.port,
                      sizeof n.port, path, pathcap, err, errcap) != 0)
            return -1;
        if (o->tls && !n.tls)
            return fail(err, errcap, "%.200s redirects to plain http, which "
                                     "this fetch refuses: a channel that "
                                     "starts on https stays there", o->host,
                        NULL);
        o->tls = n.tls;
        memcpy(o->host, n.host, sizeof o->host);
        memcpy(o->port, n.port, sizeof o->port);
        return 0;
    }
    if (loc[0] == '/') {
        if (strlen(loc) >= pathcap)
            return fail(err, errcap, "the server redirects to a path too long "
                                     "to use", NULL, NULL);
        memcpy(path, loc, strlen(loc) + 1);
        return 0;
    }
    return fail(err, errcap, "the server redirects to %.300s, which this "
                             "fetch cannot follow", loc, NULL);
}

static long fetch(const mmo_fetch_origin *o0, const char *path0,
                  sink_fn sink, void *ud, char *err, size_t errcap)
{
    mmo_fetch_origin o = *o0;
    char path[FETCH_PATHMAX], loc[FETCH_PATHMAX];
    int hops;

    if (strlen(path0) >= sizeof path)
        return fail(err, errcap, "%.300s is a path too long to fetch", path0,
                    NULL);
    memcpy(path, path0, strlen(path0) + 1);
    for (hops = 0;; hops++) {
        long r = fetch_once(&o, path, sink, ud, loc, sizeof loc, err, errcap);

        if (r != FETCH_REDIRECT)
            return r;
        if (hops >= FETCH_MAX_HOPS)
            return fail(err, errcap, "%.200s redirects too many times",
                        o.host, NULL);
        if (follow(&o, path, sizeof path, loc, err, errcap) != 0)
            return -1;
    }
}

long mmo_fetch_buf(const mmo_fetch_origin *o, const char *path,
                   void *buf, size_t cap, char *err, size_t errcap)
{
    struct memsink m;

    m.buf = buf;
    m.cap = cap;
    m.at = 0;
    return fetch(o, path, mem_sink, &m, err, errcap);
}

long mmo_fetch_file(const mmo_fetch_origin *o, const char *path,
                    const char *dest, long cap,
                    void (*tick)(void *ud, long got, long total), void *tickud,
                    char *err, size_t errcap)
{
    struct filesink s;
    long n;

    s.f = fopen(dest, "wb");
    if (s.f == NULL)
        return fail(err, errcap, "%.300s cannot be written", dest, NULL);
    s.cap = cap;
    s.at = 0;
    s.tick = tick;
    s.tickud = tickud;
    n = fetch(o, path, file_sink, &s, err, errcap);
    if (fclose(s.f) != 0 && n >= 0)
        n = fail(err, errcap, "%.300s cannot be written", dest, NULL);
    if (n < 0)
        remove(dest);
    return n;
}
