/*
 * The page a session's state is published on, and the sentence the
 * window and the launcher make out of it.
 */

#include <stdio.h>
#include <string.h>

#include "client.h"
#include "status_channel.h"
#include "view_status.h"

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

static void page_init(struct openmmo_status_shm *s)
{
    memset(s, 0, sizeof *s);
    s->magic = OPENMMO_STATUS_MAGIC;
    s->version = OPENMMO_STATUS_VERSION;
}

/* The whole point of the file: the page's numbering is the client's. */
static void test_states_agree(void)
{
    CHECK((uint32_t)OPENMMO_DISCONNECTED    == OPENMMO_ST_DISCONNECTED &&
          (uint32_t)OPENMMO_CONNECTING      == OPENMMO_ST_CONNECTING &&
          (uint32_t)OPENMMO_HANDSHAKING     == OPENMMO_ST_HANDSHAKING &&
          (uint32_t)OPENMMO_AUTHENTICATING  == OPENMMO_ST_AUTHENTICATING &&
          (uint32_t)OPENMMO_AUTHED          == OPENMMO_ST_AUTHED &&
          (uint32_t)OPENMMO_REQUESTING_GAME == OPENMMO_ST_REQUESTING_GAME &&
          (uint32_t)OPENMMO_JOINING_GAME    == OPENMMO_ST_JOINING_GAME &&
          (uint32_t)OPENMMO_IN_GAME         == OPENMMO_ST_IN_GAME &&
          (uint32_t)OPENMMO_FAILED          == OPENMMO_ST_FAILED,
          "the page's states are the client's own, value for value");
    CHECK((uint32_t)OPENMMO_FAILED + 1u == OPENMMO_ST_N,
          "and nothing has been added to the client's enum without being added "
          "here");
}

static void test_layout(void)
{
    struct openmmo_status_shm s;

    CHECK(sizeof s == 8u * 4u + OPENMMO_STATUS_REASON,
          "the page is eight words and a sentence, with no padding to disagree "
          "about across the -m32 / 64-bit split");
    CHECK(OPENMMO_STATUS_REASON >= sizeof ((openmmo_event *)0)->message,
          "the reason holds the longest message the client can produce");
    page_init(&s);
    CHECK(s.magic == 0x4F4D5354u, "the magic is 'OMST'");
}

static void test_publish_read(void)
{
    struct openmmo_status_shm s;
    uint32_t state = 99, flags = 99, gen = 99;
    char reason[OPENMMO_STATUS_REASON];

    page_init(&s);
    openmmo_status_publish(&s, OPENMMO_ST_CONNECTING,
                           OPENMMO_STATUS_F_SESSION, "");
    CHECK(openmmo_status_read(&s, &state, &flags, &gen, reason) &&
          state == OPENMMO_ST_CONNECTING && flags == OPENMMO_STATUS_F_SESSION &&
          reason[0] == '\0',
          "a published state reads back as itself");
    CHECK(gen == 1 && (s.seq & 1u) == 0,
          "one publish is one generation, and the lock is left even");

    openmmo_status_publish(&s, OPENMMO_ST_FAILED,
                           OPENMMO_STATUS_F_SESSION, "login refused");
    CHECK(openmmo_status_read(&s, &state, &flags, &gen, reason) &&
          state == OPENMMO_ST_FAILED && gen == 2 &&
          strcmp(reason, "login refused") == 0,
          "the failure and its words arrive together");

    /* The same state twice is still two events, a second failure with the same
     * reason must not read as "nothing happened". */
    openmmo_status_publish(&s, OPENMMO_ST_FAILED,
                           OPENMMO_STATUS_F_SESSION, "login refused");
    CHECK(openmmo_status_read(&s, NULL, NULL, &gen, NULL) && gen == 3,
          "a repeated state still counts as a transition");
}

/* A reader that arrives inside a write must come away with nothing rather than
 * with half a sentence. Simulated by leaving the sequence odd, which is exactly
 * the state a writer is in between its two stores. */
static void test_torn_read_refused(void)
{
    struct openmmo_status_shm s;
    uint32_t state = 0;
    char reason[OPENMMO_STATUS_REASON];

    page_init(&s);
    openmmo_status_publish(&s, OPENMMO_ST_AUTHED, OPENMMO_STATUS_F_SESSION,
                           "");
    s.seq |= 1u;                     /* a write is in flight */
    memcpy((void *)s.reason, "half a sen", 10);
    CHECK(!openmmo_status_read(&s, &state, NULL, NULL, reason),
          "a read that lands inside a write returns nothing at all");

    s.magic = 0;
    CHECK(!openmmo_status_read(&s, &state, NULL, NULL, reason),
          "a page with no magic is not read");
    s.magic = OPENMMO_STATUS_MAGIC;
    s.version = OPENMMO_STATUS_VERSION + 1u;
    CHECK(!openmmo_status_read(&s, &state, NULL, NULL, reason),
          "nor is one whose version is not ours");
}

/* Which states end a session. This is what the launcher acts on: a wrong yes
 * kills a live game, a wrong no leaves a window drawing a world nobody is
 * connected to. */
static void test_over(void)
{
    CHECK(!openmmo_status_over(OPENMMO_ST_DISCONNECTED, 0),
          "a run with no session configured is never 'over'");
    CHECK(!openmmo_status_over(OPENMMO_ST_DISCONNECTED,
                              OPENMMO_STATUS_F_SESSION),
          "nor is a session that has not connected yet");
    CHECK(openmmo_status_over(OPENMMO_ST_DISCONNECTED,
                             OPENMMO_STATUS_F_SESSION |
                             OPENMMO_STATUS_F_WAS_LIVE),
          "a session that was live and is now disconnected is over");
    CHECK(openmmo_status_over(OPENMMO_ST_FAILED, OPENMMO_STATUS_F_SESSION),
          "a failure is over whether or not it ever connected");
    CHECK(!openmmo_status_over(OPENMMO_ST_CONNECTING,
                               OPENMMO_STATUS_F_SESSION) &&
          !openmmo_status_over(OPENMMO_ST_IN_GAME,
                               OPENMMO_STATUS_F_SESSION |
                               OPENMMO_STATUS_F_WAS_LIVE),
          "and nothing in flight or in the world is");
}

static void test_captions(void)
{
    uint32_t i;
    int all_named = 1;

    for (i = 0; i < OPENMMO_ST_N; i++) {
        const char *c = openmmo_status_caption(i, OPENMMO_STATUS_F_SESSION);

        if (c == NULL || c[0] == '\0' || strcmp(c, "UNKNOWN") == 0)
            all_named = 0;
    }
    CHECK(all_named, "every state a session can be in has words for a player");
    CHECK(strcmp(openmmo_status_caption(OPENMMO_ST_DISCONNECTED, 0),
                 "NOT CONNECTED") == 0,
          "a run with no server is 'not connected', not 'disconnected'");
    CHECK(strcmp(openmmo_status_caption(OPENMMO_ST_DISCONNECTED,
                                        OPENMMO_STATUS_F_SESSION),
                 "DISCONNECTED") == 0 &&
          strcmp(openmmo_status_caption(OPENMMO_ST_DISCONNECTED,
                                        OPENMMO_STATUS_F_SESSION |
                                        OPENMMO_STATUS_F_WAS_LIVE),
                 "CONNECTION LOST") == 0,
          "a link that broke under a live session says so, and a session "
          "that is merely not up does not");
    CHECK(strcmp(openmmo_status_caption(OPENMMO_ST_CONNECTING,
                                        OPENMMO_STATUS_F_SESSION |
                                        OPENMMO_STATUS_F_WAS_LIVE |
                                        OPENMMO_STATUS_F_REJOIN),
                 "RECONNECTING") == 0 &&
          strcmp(openmmo_status_caption(OPENMMO_ST_DISCONNECTED,
                                        OPENMMO_STATUS_F_SESSION |
                                        OPENMMO_STATUS_F_WAS_LIVE |
                                        OPENMMO_STATUS_F_REJOIN),
                 "RECONNECTING") == 0,
          "a rejoin campaign is one word through every state it flaps "
          "through");
    CHECK(!openmmo_status_over(OPENMMO_ST_DISCONNECTED,
                               OPENMMO_STATUS_F_SESSION |
                               OPENMMO_STATUS_F_WAS_LIVE |
                               OPENMMO_STATUS_F_REJOIN) &&
          !openmmo_status_over(OPENMMO_ST_FAILED,
                               OPENMMO_STATUS_F_SESSION |
                               OPENMMO_STATUS_F_WAS_LIVE |
                               OPENMMO_STATUS_F_REJOIN),
          "a campaign between attempts is not an ended session, or the "
          "launcher would kill the game for it");
    CHECK(strcmp(openmmo_status_caption(OPENMMO_ST_IN_GAME,
                                        OPENMMO_STATUS_F_SESSION |
                                        OPENMMO_STATUS_F_WAS_LIVE |
                                        OPENMMO_STATUS_F_REJOIN),
                 "ONLINE") == 0,
          "and a campaign that reached the world never captions it "
          "RECONNECTING");
    CHECK(strcmp(openmmo_status_caption(OPENMMO_ST_FAILED,
                                        OPENMMO_STATUS_F_SESSION),
                 "SERVER SAID NO") == 0 &&
          strcmp(openmmo_status_caption(OPENMMO_ST_FAILED,
                                        OPENMMO_STATUS_F_SESSION |
                                        OPENMMO_STATUS_F_WAS_LIVE),
                 "CONNECTION LOST") == 0,
          "a refusal and a broken link are the same state and different "
          "sentences");
}

/* What the window puts over the picture, without a window. */
static void test_banner_policy(void)
{
    struct view_status st;
    char line[64], detail[OPENMMO_STATUS_REASON];
    uint32_t colour = 0;

    view_status_init(&st);
    CHECK(!view_status_banner(&st, 0, line, sizeof line, detail, sizeof detail,
                              &colour),
          "nothing is drawn before anything has been read");

    st.any = 1;
    st.flags = OPENMMO_STATUS_F_SESSION;
    st.state = OPENMMO_ST_CONNECTING;
    CHECK(view_status_banner(&st, 60000, line, sizeof line, detail,
                             sizeof detail, &colour) &&
          strcmp(line, "CONNECTING") == 0,
          "a connect that is still trying stays on screen however long it takes");

    st.state = OPENMMO_ST_IN_GAME;
    st.flags |= OPENMMO_STATUS_F_WAS_LIVE;
    CHECK(view_status_banner(&st, 0, line, sizeof line, detail, sizeof detail,
                             &colour),
          "arriving in the world is said");
    CHECK(!view_status_banner(&st, OPENMMO_VIEW_STATUS_SETTLED_MS, line,
                              sizeof line, detail, sizeof detail, &colour),
          "and then gets out of the way of the game");

    st.state = OPENMMO_ST_JOINING_GAME;
    CHECK(view_status_banner(&st, 0, line, sizeof line, detail, sizeof detail,
                             &colour) &&
          strcmp(line, "JOINING") == 0,
          "arriving at the character list is said");
    CHECK(!view_status_banner(&st, OPENMMO_VIEW_STATUS_SETTLED_MS, line,
                              sizeof line, detail, sizeof detail, &colour),
          "and then gets out of the way of the lobby");

    st.state = OPENMMO_ST_FAILED;
    snprintf(st.reason, sizeof st.reason, "connect: Connection refused");
    CHECK(view_status_banner(&st, 60000, line, sizeof line, detail,
                             sizeof detail, &colour) &&
          strcmp(line, "CONNECTION LOST") == 0 &&
          strcmp(detail, "connect: Connection refused") == 0 &&
          colour == 0xFF5050u,
          "a session that ended stays up, in red, with the reason under it");

    st.flags = 0;
    CHECK(!view_status_banner(&st, 0, line, sizeof line, detail, sizeof detail,
                              &colour),
          "and a run with no session is never captioned at all");
}

int status_channel_tests_run(void)
{
    failures = 0;
    printf("status channel:\n");
    test_states_agree();
    test_layout();
    test_publish_read();
    test_torn_read_refused();
    test_over();
    test_captions();
    test_banner_policy();
    if (failures == 0) printf("status channel: all checks passed\n");
    else printf("status channel: %d check(s) FAILED\n", failures);
    return failures;
}
