/*
 * The Discord end of presence.h. The reasoning is in the header; what is here is
 * the wire.
 */
#include "presence.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform.h" /* mmo_plat_pid, for the pid SET_ACTIVITY wants */

#if defined(_WIN32)
/* kernel32 only. presence.h says why winsock must not appear on this path. */
#include <windows.h>
#else
#include "sockets.h"
#include <sys/un.h>
#endif

/* ------------------------------------------------------------------ the pipe */

#if defined(_WIN32)

static int ipc_open(mmo_presence *p, const char *name)
{
    HANDLE h = CreateFileA(name, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                           OPEN_EXISTING, 0, NULL);

    if (h == INVALID_HANDLE_VALUE)
        return -1;
    p->pipe = (void *)h;
    return 0;
}

static int ipc_is_open(const mmo_presence *p)
{
    return p->pipe != NULL;
}

static void ipc_close(mmo_presence *p)
{
    if (p->pipe != NULL)
        CloseHandle((HANDLE)p->pipe);
    p->pipe = NULL;
}

/* A blocking write, as Discord's own implementations do it: the frames here are
 * a few hundred bytes against a 64 KiB pipe buffer being drained by a process
 * that wants them, so there is nothing to wait for. */
static long ipc_send(mmo_presence *p, const void *buf, size_t len)
{
    DWORD wrote = 0;

    if (!WriteFile((HANDLE)p->pipe, buf, (DWORD)len, &wrote, NULL))
        return -1;
    return (long)wrote;
}

/* Peek first: ReadFile on a byte-mode pipe with nothing in it blocks, and this
 * runs inside a frame. */
static long ipc_recv(mmo_presence *p, void *buf, size_t len)
{
    DWORD avail = 0, got = 0;

    if (!PeekNamedPipe((HANDLE)p->pipe, NULL, 0, NULL, &avail, NULL))
        return -1;
    if (avail == 0)
        return 0;
    if ((size_t)avail > len)
        avail = (DWORD)len;
    if (!ReadFile((HANDLE)p->pipe, buf, avail, &got, NULL))
        return -1;
    return (long)got;
}

/* The candidates: Discord's pipes are numbered, and a second client (Canary,
 * PTB) takes the next number rather than another name. */
#define IPC_CANDIDATES 10

static int ipc_candidate(int i, char *out, size_t cap)
{
    if (i < 0 || i >= IPC_CANDIDATES)
        return -1;
    snprintf(out, cap, "\\\\.\\pipe\\discord-ipc-%d", i);
    return 0;
}

#else /* !_WIN32 */

static int ipc_open(mmo_presence *p, const char *name)
{
    struct sockaddr_un sa;
    int fd, err;

    if (strlen(name) + 1 > sizeof sa.sun_path)
        return -1;
    /* CLOEXEC because this program re-execs itself on a logout: a socket that
     * survived would leave Discord holding a session that has ended. */
    fd = mmo_sock_open(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return -1;
    memset(&sa, 0, sizeof sa);
    sa.sun_family = AF_UNIX;
    memcpy(sa.sun_path, name, strlen(name) + 1);
    if (mmo_sock_connect(fd, (const struct sockaddr *)&sa, sizeof sa) != 0) {
        err = mmo_sock_errno();
        /* A unix connect to a listening socket completes at once; in flight is
         * possible only with a full backlog, and the first send sorts it out. */
        if (err != MMO_EINPROGRESS && err != MMO_EINTR) {
            mmo_sock_close(fd);
            return -1;
        }
    }
    p->fd = fd;
    return 0;
}

static int ipc_is_open(const mmo_presence *p)
{
    return p->fd >= 0;
}

static void ipc_close(mmo_presence *p)
{
    if (p->fd >= 0)
        mmo_sock_close(p->fd);
    p->fd = -1;
}

static long ipc_send(mmo_presence *p, const void *buf, size_t len)
{
    for (;;) {
        long w = mmo_sock_send(p->fd, buf, len, MSG_NOSIGNAL);
        int err;

        if (w >= 0)
            return w;
        err = mmo_sock_errno();
        if (err == MMO_EINTR)
            continue;
        if (err == MMO_EAGAIN || err == MMO_EWOULDBLOCK)
            return 0;
        return -1;
    }
}

static long ipc_recv(mmo_presence *p, void *buf, size_t len)
{
    for (;;) {
        long r = mmo_sock_recv(p->fd, buf, len, 0);
        int err;

        if (r > 0)
            return r;
        if (r == 0)
            return -1; /* the other end hung up */
        err = mmo_sock_errno();
        if (err == MMO_EINTR)
            continue;
        if (err == MMO_EAGAIN || err == MMO_EWOULDBLOCK)
            return 0;
        return -1;
    }
}

/*
 * Where the socket is. Discord puts it in the runtime directory under one of four names
 * depending on how it was installed, and numbers it when a second client is running.
 */
static const char *const kIpcDirs[] = {
    NULL, /* XDG_RUNTIME_DIR */
    NULL, /* TMPDIR */
    NULL, /* TMP */
    NULL, /* TEMP */
    "/tmp"
};
static const char *const kIpcDirEnv[] = {
    "XDG_RUNTIME_DIR", "TMPDIR", "TMP", "TEMP", NULL
};
static const char *const kIpcSubs[] = {
    "",
    "app/com.discordapp.Discord/",       /* flatpak */
    "app/com.discordapp.DiscordCanary/",
    "snap.discord/"                      /* snap */
};

#define IPC_NDIRS (int)(sizeof kIpcDirs / sizeof kIpcDirs[0])
#define IPC_NSUBS (int)(sizeof kIpcSubs / sizeof kIpcSubs[0])
#define IPC_CANDIDATES (IPC_NDIRS * IPC_NSUBS * 10)

static int ipc_candidate(int i, char *out, size_t cap)
{
    int dir, sub, idx;
    const char *base;
    size_t n;

    if (i < 0 || i >= IPC_CANDIDATES)
        return -1;
    dir = i / (IPC_NSUBS * 10);
    sub = (i / 10) % IPC_NSUBS;
    idx = i % 10;
    base = kIpcDirEnv[dir] != NULL ? getenv(kIpcDirEnv[dir]) : kIpcDirs[dir];
    if (base == NULL || base[0] == '\0')
        return -1; /* that directory is not set on this host; not a failure */
    n = strlen(base);
    while (n > 1 && base[n - 1] == '/')
        n--;
    snprintf(out, cap, "%.*s/%sdiscord-ipc-%d", (int)n, base, kIpcSubs[sub], idx);
    return 0;
}

#endif /* _WIN32 */

/* ------------------------------------------------------------------- frames */

static void put_le32(unsigned char *d, unsigned v)
{
    d[0] = (unsigned char)(v & 0xFFu);
    d[1] = (unsigned char)((v >> 8) & 0xFFu);
    d[2] = (unsigned char)((v >> 16) & 0xFFu);
    d[3] = (unsigned char)((v >> 24) & 0xFFu);
}

/* Queue one frame. There is only ever one in flight: the caller does not build
 * a second until the first has left, so a slow Discord costs an update and
 * never a growing buffer. */
static int frame_put(mmo_presence *p, unsigned op, const char *json)
{
    size_t len = strlen(json);

    if (p->tx_head < p->tx_len)
        return -1; /* still writing the last one */
    if (len + 8 > sizeof p->tx)
        return -1;
    put_le32(p->tx, op);
    put_le32(p->tx + 4, (unsigned)len);
    memcpy(p->tx + 8, json, len);
    p->tx_len = len + 8;
    p->tx_head = 0;
    return 0;
}

/*
 * One JSON string body, escaped. Quotes and backslashes are the two that matter; a control
 * character cannot come out of a map label or a character name, and is spelled \u00xx rather
 * than trusted.
 */
static size_t json_escape(const char *src, char *out, size_t cap)
{
    size_t o = 0;

    for (const unsigned char *s = (const unsigned char *)src; *s != '\0'; s++) {
        char esc[8];
        size_t n;

        if (*s == '"' || *s == '\\') {
            esc[0] = '\\';
            esc[1] = (char)*s;
            n = 2;
        } else if (*s < 0x20) {
            n = (size_t)snprintf(esc, sizeof esc, "\\u%04x", (unsigned)*s);
        } else {
            esc[0] = (char)*s;
            n = 1;
        }
        if (o + n + 1 > cap)
            break;
        memcpy(out + o, esc, n);
        o += n;
    }
    if (cap > 0)
        out[o < cap ? o : cap - 1] = '\0';
    return o;
}

/* A line Discord will take. Its own validation refuses anything shorter than
 * two characters, so a one-letter name is no line at all rather than an
 * activity Discord throws away whole. Characters and not bytes: a name can be
 * accented, and two of those are four bytes. */
static int line_ok(const char *s)
{
    int n = 0;

    if (s == NULL)
        return 0;
    for (const unsigned char *p = (const unsigned char *)s; *p != '\0'; p++)
        if ((*p & 0xC0) != 0x80 && ++n >= 2)
            return 1;
    return 0;
}

/* Append to a bounded buffer, keeping the offset honest: snprintf reports what
 * it would have written, and an offset advanced by that walks off the end. */
static void append(char *buf, size_t cap, size_t *off, const char *fmt, ...)
{
    va_list ap;
    int n;

    if (*off >= cap)
        return;
    va_start(ap, fmt);
    n = vsnprintf(buf + *off, cap - *off, fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    *off = ((size_t)n >= cap - *off) ? cap : *off + (size_t)n;
}

size_t mmo_presence_activity_json(const mmo_presence *p, char *out, size_t cap)
{
    /* Escaping can double a line (every byte a quote or a backslash) and
     * nothing upstream survives copy_line as a control character, so twice the
     * line plus a terminator is the whole worst case. */
    char loc[MMO_PRESENCE_LINE * 2 + 2], who[MMO_PRESENCE_LINE * 2 + 2];
    char body[MMO_PRESENCE_LINE * 4 + 256];
    size_t o = 0;

    if (p == NULL || out == NULL || cap == 0)
        return 0;

    /* Neither line: a clear. SET_ACTIVITY with no activity key is how the
     * protocol says "nothing", which is what the title screen and a session
     * that has ended look like from a process that keeps running. */
    if (!line_ok(p->location) && !line_ok(p->player)) {
        append(out, cap, &o,
               "{\"cmd\":\"SET_ACTIVITY\",\"nonce\":\"%u\","
               "\"args\":{\"pid\":%u}}",
               p->nonce, mmo_plat_pid());
        return o < cap ? o : 0;
    }

    json_escape(p->location, loc, sizeof loc);
    json_escape(p->player, who, sizeof who);

    body[0] = '\0';
    {
        size_t b = 0;

        if (line_ok(p->location))
            append(body, sizeof body, &b, "\"details\":\"%s\"", loc);
        if (line_ok(p->player))
            append(body, sizeof body, &b, "%s\"state\":\"%s\"",
                   b > 0 ? "," : "", who);
        /* The one button, and the whole reason a reader can click through to
         * the game. Discord takes at most two; this build sends one and always
         * the same one, so there is nothing here to configure. */
        append(body, sizeof body, &b,
               ",\"buttons\":[{\"label\":\"%s\",\"url\":\"%s\"}]",
               MMO_PRESENCE_BUTTON, MMO_PRESENCE_URL);
        if (b >= sizeof body)
            return 0; /* it did not fit; send nothing rather than half of it */
    }

    append(out, cap, &o,
           "{\"cmd\":\"SET_ACTIVITY\",\"nonce\":\"%u\","
           "\"args\":{\"pid\":%u,\"activity\":{%s}}}",
           p->nonce, mmo_plat_pid(), body);
    return o < cap ? o : 0;
}

/* ------------------------------------------------------------------ the tick */

static void drop(mmo_presence *p, long now)
{
    ipc_close(p);
    p->state = MMO_PRESENCE_IDLE;
    p->tx_len = p->tx_head = 0;
    p->rx_len = 0;
    p->probe = 0;
    p->retry_at = now + MMO_PRESENCE_RETRY_S;
    /* Whatever Discord was told died with the connection, so the next one is
     * told again from scratch. */
    p->dirty = 1;
    p->send_at = 0;
}

/*
 * One line, as Discord will be shown it: at most 127 bytes, no control characters, and never
 * ending inside a UTF-8 sequence.
 */
static void copy_line(char *dst, const char *src)
{
    size_t i = 0, lead;
    unsigned char c;
    size_t need;

    if (src != NULL)
        for (size_t j = 0; i + 1 < MMO_PRESENCE_LINE && src[j] != '\0'; j++) {
            unsigned char b = (unsigned char)src[j];

            if (b < 0x20 || b == 0x7F)
                continue; /* not text; nothing upstream can produce one */
            dst[i++] = (char)b;
        }
    dst[i] = '\0';
    if (i == 0)
        return;

    lead = i;
    while (lead > 0 && ((unsigned char)dst[lead - 1] & 0xC0) == 0x80)
        lead--;
    if (lead == 0) {
        dst[0] = '\0'; /* continuation bytes with no lead: not text either */
        return;
    }
    c = (unsigned char)dst[lead - 1];
    if (c < 0x80)
        return; /* ends on ASCII, so it ends on a boundary */
    need = (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 1;
    if (i - (lead - 1) < need)
        dst[lead - 1] = '\0';
}

const char *mmo_presence_app_id(void)
{
    const char *over = getenv("OPENMMO_DISCORD_APP");

    return (over != NULL && over[0] != '\0') ? over : MMO_PRESENCE_APP_ID;
}

void mmo_presence_init(mmo_presence *p, const char *app_id)
{
    if (p == NULL)
        return;
    memset(p, 0, sizeof *p);
    p->fd = -1;
    p->pipe = NULL;
    if (app_id == NULL || app_id[0] == '\0') {
        p->state = MMO_PRESENCE_OFF;
        return;
    }
    snprintf(p->app_id, sizeof p->app_id, "%s", app_id);
    {
        const char *over = getenv("OPENMMO_DISCORD_IPC");

        if (over != NULL && over[0] != '\0')
            snprintf(p->path, sizeof p->path, "%s", over);
    }
    p->state = MMO_PRESENCE_IDLE;
    p->nonce = 1;
}

void mmo_presence_set(mmo_presence *p, const char *location, const char *player)
{
    char loc[MMO_PRESENCE_LINE], who[MMO_PRESENCE_LINE];

    if (p == NULL || p->state == MMO_PRESENCE_OFF)
        return;
    copy_line(loc, location);
    copy_line(who, player);
    if (strcmp(loc, p->location) == 0 && strcmp(who, p->player) == 0)
        return; /* the same two lines are not an update */
    memcpy(p->location, loc, sizeof loc);
    memcpy(p->player, who, sizeof who);
    p->dirty = 1;
}

int mmo_presence_live(const mmo_presence *p)
{
    return p != NULL && p->state == MMO_PRESENCE_READY;
}

/* Try the next few candidates. A sweep that reaches the end without finding
 * anything is a machine with no Discord running, which is not a failure and is
 * simply tried again later. */
static void probe(mmo_presence *p, long now)
{
    char name[MMO_PRESENCE_PATH];
    char json[128];
    int tries;

    if (now < p->retry_at)
        return;
    if (p->path[0] != '\0') {
        /* An override names one socket and there is nothing to sweep. */
        if (ipc_open(p, p->path) != 0) {
            p->retry_at = now + MMO_PRESENCE_RETRY_S;
            return;
        }
    } else {
        for (tries = 0; tries < MMO_PRESENCE_PROBES; tries++) {
            if (p->probe >= IPC_CANDIDATES) {
                p->probe = 0;
                p->retry_at = now + MMO_PRESENCE_RETRY_S;
                return;
            }
            if (ipc_candidate(p->probe++, name, sizeof name) != 0)
                continue; /* a directory this host does not have */
            if (ipc_open(p, name) == 0)
                break;
        }
        if (!ipc_is_open(p))
            return;
    }

    snprintf(json, sizeof json, "{\"v\":1,\"client_id\":\"%s\"}", p->app_id);
    if (frame_put(p, 0, json) != 0) {
        drop(p, now);
        return;
    }
    p->state = MMO_PRESENCE_HANDSHAKE;
    p->probe = 0;
}

/* Push whatever of the queued frame the pipe will take. */
static int flush(mmo_presence *p)
{
    while (p->tx_head < p->tx_len) {
        long w = ipc_send(p, p->tx + p->tx_head, p->tx_len - p->tx_head);

        if (w < 0)
            return -1;
        if (w == 0)
            break; /* not now; the rest goes next tick */
        p->tx_head += (size_t)w;
    }
    if (p->tx_head >= p->tx_len)
        p->tx_len = p->tx_head = 0;
    return 0;
}

/*
 * Read what is there and look for READY. The window keeps a tail across reads so the marker is
 * still found when it lands across two of them, and drops everything else, there is nothing
 * in a dispatch this client wants.
 */
#define READY_MARK "\"evt\":\"READY\""
#define READY_LEN  (sizeof READY_MARK - 1)

static int mem_has(const unsigned char *hay, size_t n, const char *needle,
                   size_t m)
{
    if (m == 0 || n < m)
        return 0;
    for (size_t i = 0; i + m <= n; i++)
        if (memcmp(hay + i, needle, m) == 0)
            return 1;
    return 0;
}

static int drain(mmo_presence *p)
{
    for (;;) {
        long r;

        if (p->rx_len >= sizeof p->rx) {
            /* Keep just enough tail that a marker split across two reads is
             * still whole, and forget the rest unread. */
            size_t keep = READY_LEN < p->rx_len ? READY_LEN : p->rx_len;

            memmove(p->rx, p->rx + p->rx_len - keep, keep);
            p->rx_len = keep;
        }
        r = ipc_recv(p, p->rx + p->rx_len, sizeof p->rx - p->rx_len);
        if (r < 0)
            return -1;
        if (r == 0)
            break;
        p->rx_len += (size_t)r;
        if (p->state == MMO_PRESENCE_HANDSHAKE
            && mem_has(p->rx, p->rx_len, READY_MARK, READY_LEN)) {
            p->state = MMO_PRESENCE_READY;
            p->rx_len = 0;
        }
    }
    return 0;
}

void mmo_presence_tick(mmo_presence *p, long now)
{
    char json[MMO_PRESENCE_TX];

    if (p == NULL || p->state == MMO_PRESENCE_OFF)
        return;

    if (!ipc_is_open(p)) {
        probe(p, now);
        if (!ipc_is_open(p))
            return;
    }

    if (flush(p) != 0 || drain(p) != 0) {
        drop(p, now);
        return;
    }

    if (p->state != MMO_PRESENCE_READY || !p->dirty)
        return;
    if (p->tx_head < p->tx_len)
        return; /* the last frame has not left yet */
    if (now < p->send_at)
        return; /* held, not lost: the rate limit is Discord's */

    if (mmo_presence_activity_json(p, json, sizeof json) == 0)
        return;
    if (frame_put(p, 1, json) != 0)
        return;
    p->nonce++;
    p->sent++;
    p->dirty = 0;
    p->send_at = now + MMO_PRESENCE_RATE_S;
    if (flush(p) != 0)
        drop(p, now);
}

void mmo_presence_close(mmo_presence *p)
{
    if (p == NULL)
        return;
    ipc_close(p);
    p->tx_len = p->tx_head = 0;
    p->rx_len = 0;
    if (p->state != MMO_PRESENCE_OFF)
        p->state = MMO_PRESENCE_IDLE;
}
