/* One HTTP GET over the client's own socket layer. */

#include "fetch.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform.h"
#include "sockets.h"

/* One transfer stalls out after this long with no bytes moving. */
#define FETCH_STALL_SECONDS 30

static int fail(char *err, size_t cap, const char *fmt, const char *a,
                const char *b)
{
    if (err != NULL && cap > 0)
        snprintf(err, cap, fmt, a, b);
    return -1;
}

int mmo_fetch_split(const char *url, char *host, size_t hostcap,
                    char *port, size_t portcap, char *base, size_t basecap,
                    char *err, size_t errcap)
{
    const char *p, *slash, *colon;
    size_t n;

    if (url == NULL || url[0] == '\0')
        return fail(err, errcap, "no update URL is configured", NULL, NULL);
    if (strncmp(url, "https://", 8) == 0)
        return fail(err, errcap,
                    "%.200s is an https URL; the update fetch speaks plain "
                    "http, because the feed's signature is what is trusted, "
                    "serve the update path over http", url, NULL);
    if (strncmp(url, "http://", 7) != 0)
        return fail(err, errcap, "%.200s is not an http:// URL", url, NULL);
    p = url + 7;
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
        snprintf(port, portcap, "80");
    }
    if (slash == NULL) {
        if (basecap > 0)
            base[0] = '\0';
        return 0;
    }
    n = strlen(slash);
    while (n > 1 && slash[n - 1] == '/')
        n--;
    if (n >= basecap)
        return fail(err, errcap, "%.200s has a path too long to use", url, NULL);
    memcpy(base, slash, n);
    base[n] = '\0';
    if (strcmp(base, "/") == 0)
        base[0] = '\0';
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

static int send_all(int fd, const char *data, size_t n, long *deadline)
{
    while (n > 0) {
        long w = mmo_sock_send(fd, data, n, MSG_NOSIGNAL);

        if (w > 0) {
            data += w;
            n -= (size_t)w;
            *deadline = mmo_plat_seconds() + FETCH_STALL_SECONDS;
            continue;
        }
        if (w == 0)
            return -1;
        {
            int e = mmo_sock_errno();

            if (e != MMO_EAGAIN && e != MMO_EWOULDBLOCK && e != MMO_EINTR)
                return -1;
        }
        if (wait_io(fd, 1, *deadline) != 0)
            return -1;
    }
    return 0;
}

/* One recv, waiting for readiness first. >0 bytes, 0 on orderly close, -1 on
 * error or stall. */
static long recv_some(int fd, char *buf, size_t cap, long *deadline)
{
    for (;;) {
        long r;

        if (wait_io(fd, 0, *deadline) != 0)
            return -1;
        r = mmo_sock_recv(fd, buf, cap, 0);
        if (r > 0) {
            *deadline = mmo_plat_seconds() + FETCH_STALL_SECONDS;
            return r;
        }
        if (r == 0)
            return 0;
        {
            int e = mmo_sock_errno();

            if (e != MMO_EAGAIN && e != MMO_EWOULDBLOCK && e != MMO_EINTR)
                return -1;
        }
    }
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
static long read_body(int fd, const char *hdr, const char *first,
                      size_t firstlen, sink_fn sink, void *ud, long *deadline,
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
                    char c = p[i++];

                    if (c == '\n') {
                        line[linelen] = '\0';
                        linelen = 0;
                        left = strtol(line, NULL, 16);
                        if (left == 0) {
                            /* The trailer is not read; the body is done. */
                            return got;
                        }
                    } else if (c != '\r' && linelen < sizeof line - 1) {
                        /* A chunk extension after ';' rides along and strtol
                         * stops at it. */
                        line[linelen++] = c;
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
                long r = recv_some(fd, buf, sizeof buf, deadline);

                if (r <= 0) {
                    fail(err, errcap,
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
        long r = recv_some(fd, buf, sizeof buf, deadline);

        if (r < 0) {
            fail(err, errcap, "the connection ended mid-download", NULL, NULL);
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

static long fetch(const char *host, const char *port, const char *path,
                  sink_fn sink, void *ud, char *err, size_t errcap)
{
    /* Room for a base, an escaped feed name and a hash query, with slack. */
    char req[4096];
    char head[8192], val[MMO_FETCH_PATH];
    struct addrinfo *ai = NULL, *a;
    long deadline, r, body;
    size_t at = 0;
    char *sep;
    int fd = -1, code, rc;

    if (err != NULL && errcap > 0)
        err[0] = '\0';
    if (mmo_sock_ready() != 0)
        return fail(err, errcap, "sockets cannot be used on this host", NULL,
                    NULL);
    rc = mmo_sock_resolve(host, port, &ai);
    if (rc != 0)
        return fail(err, errcap, "%.200s cannot be found: %.100s", host,
                    mmo_sock_resolve_error(rc));
    deadline = mmo_plat_seconds() + FETCH_STALL_SECONDS;
    for (a = ai; a != NULL; a = a->ai_next) {
        fd = mmo_sock_open(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (fd < 0)
            continue;
        if (mmo_sock_connect(fd, a->ai_addr, (unsigned)a->ai_addrlen) == 0)
            break;
        {
            int e = mmo_sock_errno();

            if ((e == MMO_EINPROGRESS || e == MMO_EAGAIN) &&
                wait_io(fd, 1, deadline) == 0 && mmo_sock_error(fd) == 0)
                break;
        }
        mmo_sock_close(fd);
        fd = -1;
    }
    mmo_sock_free_resolved(ai);
    if (a == NULL || fd < 0)
        return fail(err, errcap, "%.200s:%.20s does not answer", host, port);
    mmo_sock_nodelay(fd);

    snprintf(req, sizeof req,
             "GET %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "User-Agent: openmmo-launch\r\n"
             "Accept: */*\r\n"
             "Connection: close\r\n\r\n",
             path[0] != '\0' ? path : "/", host);
    if (send_all(fd, req, strlen(req), &deadline) != 0) {
        mmo_sock_close(fd);
        return fail(err, errcap, "%.200s stopped answering", host, NULL);
    }

    /* The head, up to the blank line; what follows it is body. */
    sep = NULL;
    while (sep == NULL) {
        if (at >= sizeof head - 1) {
            mmo_sock_close(fd);
            return fail(err, errcap, "the server's response head never ends",
                        NULL, NULL);
        }
        r = recv_some(fd, head + at, sizeof head - 1 - at, &deadline);
        if (r <= 0) {
            mmo_sock_close(fd);
            return fail(err, errcap, "%.200s stopped answering", host, NULL);
        }
        at += (size_t)r;
        head[at] = '\0';
        sep = strstr(head, "\r\n\r\n");
    }
    *sep = '\0';

    if (sscanf(head, "HTTP/%*d.%*d %d", &code) != 1) {
        mmo_sock_close(fd);
        return fail(err, errcap, "the server did not answer with HTTP", NULL,
                    NULL);
    }
    if (code != 200) {
        mmo_sock_close(fd);
        if (code >= 300 && code < 400 &&
            header_value(head, "Location", val, sizeof val) == 1)
            return fail(err, errcap,
                        "the server redirects to %.300s, if that is https, "
                        "exempt the update path from the proxy's forced "
                        "https", val, NULL);
        snprintf(val, sizeof val, "%d", code);
        return fail(err, errcap, "the server answered %.20s for %.300s", val,
                    path);
    }

    body = read_body(fd, head, sep + 4, at - (size_t)(sep + 4 - head), sink,
                     ud, &deadline, err, errcap);
    mmo_sock_close(fd);
    return body;
}

long mmo_fetch_buf(const char *host, const char *port, const char *path,
                   void *buf, size_t cap, char *err, size_t errcap)
{
    struct memsink m;

    m.buf = buf;
    m.cap = cap;
    m.at = 0;
    return fetch(host, port, path, mem_sink, &m, err, errcap);
}

long mmo_fetch_file(const char *host, const char *port, const char *path,
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
    n = fetch(host, port, path, file_sink, &s, err, errcap);
    if (fclose(s.f) != 0 && n >= 0)
        n = fail(err, errcap, "%.300s cannot be written", dest, NULL);
    if (n < 0)
        remove(dest);
    return n;
}
