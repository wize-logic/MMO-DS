/* The Discord presence line, without a Discord. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "presence.h"

#ifndef _WIN32
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

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

#define APP_ID "1458308439121068104"

/* ------------------------------------------------------- the payload alone */

static void test_two_lines(void)
{
    mmo_presence p;
    char json[MMO_PRESENCE_TX];

    mmo_presence_init(&p, APP_ID);
    mmo_presence_set(&p, "Jubilife City", "Blue");
    CHECK(mmo_presence_activity_json(&p, json, sizeof json) > 0,
          "an activity is built from the two lines");
    CHECK(strstr(json, "\"details\":\"Jubilife City\"") != NULL,
          "the location is the first line");
    CHECK(strstr(json, "\"state\":\"Blue\"") != NULL,
          "the player name is the second");
    CHECK(strstr(json, "\"url\":\"" MMO_PRESENCE_URL "\"") != NULL,
          "one button carries the site");
    CHECK(strstr(json, "SET_ACTIVITY") != NULL && strstr(json, "\"pid\"") != NULL,
          "it is a SET_ACTIVITY naming this process");
    /* Nothing more: no timer, no artwork, no party, no invite secrets. */
    CHECK(strstr(json, "timestamps") == NULL && strstr(json, "assets") == NULL
              && strstr(json, "party") == NULL && strstr(json, "secrets") == NULL,
          "and carries nothing else");
}

static void test_clear(void)
{
    mmo_presence p;
    char json[MMO_PRESENCE_TX];

    mmo_presence_init(&p, APP_ID);
    mmo_presence_set(&p, "", "");
    mmo_presence_activity_json(&p, json, sizeof json);
    CHECK(strstr(json, "activity") == NULL && strstr(json, "SET_ACTIVITY") != NULL,
          "no lines is a clear, not an empty card");
}

static void test_escaping(void)
{
    mmo_presence p;
    char json[MMO_PRESENCE_TX];

    mmo_presence_init(&p, APP_ID);
    mmo_presence_set(&p, "Route \"1\"", "back\\slash");
    mmo_presence_activity_json(&p, json, sizeof json);
    CHECK(strstr(json, "Route \\\"1\\\"") != NULL,
          "a quote in a name is escaped, not left to break the frame");
    CHECK(strstr(json, "back\\\\slash") != NULL, "and so is a backslash");
}

static void test_short_line(void)
{
    mmo_presence p;
    char json[MMO_PRESENCE_TX];

    mmo_presence_init(&p, APP_ID);
    mmo_presence_set(&p, "Jubilife City", "B");
    mmo_presence_activity_json(&p, json, sizeof json);
    CHECK(strstr(json, "\"state\"") == NULL,
          "a one-character name is left out; Discord refuses it");
    CHECK(strstr(json, "\"details\":\"Jubilife City\"") != NULL,
          "and the line that is long enough still goes");
}

static void test_utf8_cut(void)
{
    mmo_presence p;
    char json[MMO_PRESENCE_TX];
    char longname[MMO_PRESENCE_LINE * 2];
    size_t i;

    /* Fill past the line limit with two-byte characters so the cut lands
     * mid-sequence unless the copy backs off to a boundary. */
    for (i = 0; i + 1 < sizeof longname; i += 2) {
        longname[i] = (char)0xC3;     /* U+00E9, e-acute */
        longname[i + 1] = (char)0xA9;
    }
    longname[sizeof longname - 1] = '\0';

    mmo_presence_init(&p, APP_ID);
    mmo_presence_set(&p, longname, "Blue");
    mmo_presence_activity_json(&p, json, sizeof json);
    CHECK(strlen(p.location) < MMO_PRESENCE_LINE, "an over-long line is cut");
    CHECK((strlen(p.location) % 2) == 0,
          "and never in the middle of a UTF-8 character");
    CHECK(strstr(json, "\"state\":\"Blue\"") != NULL,
          "the activity is still well formed");
}

static void test_off_without_app_id(void)
{
    mmo_presence p;

    mmo_presence_init(&p, "");
    mmo_presence_set(&p, "Jubilife City", "Blue");
    mmo_presence_tick(&p, 1000);
    CHECK(p.state == MMO_PRESENCE_OFF && p.location[0] == '\0',
          "a build with no application id never dials anything");
}

/* ------------------------------------------------------------ the transport */

#ifndef _WIN32

static int listen_at(const char *path)
{
    struct sockaddr_un sa;
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);

    if (fd < 0)
        return -1;
    unlink(path);
    memset(&sa, 0, sizeof sa);
    sa.sun_family = AF_UNIX;
    snprintf(sa.sun_path, sizeof sa.sun_path, "%s", path);
    if (bind(fd, (struct sockaddr *)&sa, sizeof sa) != 0
        || listen(fd, 1) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static int waitable(int fd, int ms)
{
    struct pollfd pfd;

    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    return poll(&pfd, 1, ms) == 1 && (pfd.revents & POLLIN) != 0;
}

static int read_exact(int fd, void *buf, size_t n)
{
    size_t got = 0;

    while (got < n) {
        long r;

        if (!waitable(fd, 500))
            return -1;
        r = read(fd, (char *)buf + got, n - got);
        if (r <= 0)
            return -1;
        got += (size_t)r;
    }
    return 0;
}

/* One frame off the wire: opcode, then the JSON body as a C string. */
static int read_frame(int fd, unsigned *op, char *body, size_t cap)
{
    unsigned char hdr[8];
    unsigned len;

    if (read_exact(fd, hdr, sizeof hdr) != 0)
        return -1;
    *op = (unsigned)hdr[0] | ((unsigned)hdr[1] << 8) | ((unsigned)hdr[2] << 16)
        | ((unsigned)hdr[3] << 24);
    len = (unsigned)hdr[4] | ((unsigned)hdr[5] << 8) | ((unsigned)hdr[6] << 16)
        | ((unsigned)hdr[7] << 24);
    if (len + 1 > cap || read_exact(fd, body, len) != 0)
        return -1;
    body[len] = '\0';
    return 0;
}

static void write_frame(int fd, unsigned op, const char *body)
{
    unsigned char hdr[8];
    size_t len = strlen(body);
    ssize_t ignored;

    hdr[0] = (unsigned char)op;
    hdr[1] = hdr[2] = hdr[3] = 0;
    hdr[4] = (unsigned char)(len & 0xFF);
    hdr[5] = (unsigned char)((len >> 8) & 0xFF);
    hdr[6] = hdr[7] = 0;
    ignored = write(fd, hdr, sizeof hdr);
    ignored = write(fd, body, len);
    (void)ignored;
}

static void test_session(void)
{
    char path[96], body[MMO_PRESENCE_TX];
    mmo_presence p;
    unsigned op = 0;
    int server, conn;

    snprintf(path, sizeof path, "/tmp/openmmo-presence-%ld.sock", (long)getpid());
    server = listen_at(path);
    if (server < 0) {
        CHECK(0, "a unix socket could be listened on");
        return;
    }
    setenv("OPENMMO_DISCORD_IPC", path, 1);

    mmo_presence_init(&p, APP_ID);
    mmo_presence_set(&p, "Jubilife City", "Blue");
    mmo_presence_tick(&p, 1000);

    conn = accept(server, NULL, NULL);
    CHECK(conn >= 0, "the client connected to the socket the override named");
    if (conn < 0) {
        close(server);
        unlink(path);
        return;
    }

    CHECK(read_frame(conn, &op, body, sizeof body) == 0 && op == 0,
          "the first frame is a handshake");
    CHECK(strstr(body, "\"client_id\":\"" APP_ID "\"") != NULL,
          "carrying the application id");
    CHECK(p.state == MMO_PRESENCE_HANDSHAKE,
          "and nothing is sent until Discord answers it");

    write_frame(conn, 1, "{\"cmd\":\"DISPATCH\",\"data\":{},\"evt\":\"READY\"}");
    mmo_presence_tick(&p, 1001);
    CHECK(mmo_presence_live(&p), "READY opens the line");
    CHECK(read_frame(conn, &op, body, sizeof body) == 0 && op == 1
              && strstr(body, "Jubilife City") != NULL
              && strstr(body, "Blue") != NULL
              && strstr(body, MMO_PRESENCE_URL) != NULL,
          "the activity follows: location, name and the site button");

    /* The rate limit is Discord's, and a change inside it is held rather than
     * spent, walking through three towns in a minute must not lose the third. */
    mmo_presence_set(&p, "Oreburgh City", "Blue");
    mmo_presence_tick(&p, 1002);
    CHECK(!waitable(conn, 50) && p.dirty,
          "a second update inside fifteen seconds is held, not sent");
    mmo_presence_tick(&p, 1001 + MMO_PRESENCE_RATE_S);
    CHECK(read_frame(conn, &op, body, sizeof body) == 0
              && strstr(body, "Oreburgh City") != NULL,
          "and goes out when the window opens");
    CHECK(p.sent == 2, "two activities, one per window");

    /* Discord quitting is an EOF, and it must leave a presence that will try
     * again rather than one that is over. */
    close(conn);
    mmo_presence_set(&p, "Eterna City", "Blue");
    mmo_presence_tick(&p, 1001 + MMO_PRESENCE_RATE_S * 2);
    CHECK(p.state == MMO_PRESENCE_IDLE && p.dirty,
          "Discord going away drops the line and keeps the lines to say");

    mmo_presence_close(&p);
    close(server);
    unlink(path);
    unsetenv("OPENMMO_DISCORD_IPC");
}

#endif /* !_WIN32 */

int presence_tests_run(void)
{
    failures = 0;
    test_two_lines();
    test_clear();
    test_escaping();
    test_short_line();
    test_utf8_cut();
    test_off_without_app_id();
#ifndef _WIN32
    test_session();
#endif

    if (failures)
        printf("presence: %d check(s) FAILED\n", failures);
    else
        printf("presence: all checks passed\n");
    return failures;
}
