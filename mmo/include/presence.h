/* The two lines Discord shows while somebody is playing. */
#ifndef MMO_PRESENCE_H
#define MMO_PRESENCE_H

#include <stddef.h>

/* Discord takes 2..128 characters in each of the two lines. A shorter one is
 * refused by its own validation, so this client does not send it. */
#define MMO_PRESENCE_LINE 128
#define MMO_PRESENCE_PATH 256
/* One frame at a time, a handshake or one activity, header and all. */
#define MMO_PRESENCE_TX   1024
/* The reply stream is read as a rolling window and never parsed as frames;
 * see presence.c for why a buffer this small cannot deadlock. */
#define MMO_PRESENCE_RX   512

/* Discord rate-limits SET_ACTIVITY to one update every 15 seconds and drops
 * the rest, so a change is held rather than spent. The same interval paces the
 * connect sweep: a player who has not started Discord must not pay for a probe
 * every frame. */
#define MMO_PRESENCE_RATE_S  15
#define MMO_PRESENCE_RETRY_S 15
/* Candidate sockets tried per tick. The sweep is spread across frames instead
 * of walking two hundred paths in one of them. */
#define MMO_PRESENCE_PROBES  8

/* The one link, and what the button that opens it is called. */
#define MMO_PRESENCE_URL    "https://mmods.net/"
#define MMO_PRESENCE_BUTTON "Play now"

/*
 * The Discord application this game is, which is what puts "OpenMMO" and its icon at the top
 * of the card, so the two lines below it really are the whole of what this client says.
 */
#define MMO_PRESENCE_APP_ID "1458308439121068104"

enum {
    MMO_PRESENCE_OFF = 0,   /* no application id: this build never dials */
    MMO_PRESENCE_IDLE,      /* nothing open; probing, or waiting to probe */
    MMO_PRESENCE_HANDSHAKE, /* the handshake is written; waiting for READY */
    MMO_PRESENCE_READY      /* Discord is listening */
};

typedef struct {
    int    state;
    int    fd;     /* POSIX: the IPC socket, -1 when closed */
    void  *pipe;   /* Windows: the pipe handle, NULL when closed */
    char   app_id[32];
    /*
     * OPENMMO_DISCORD_IPC: one path (or pipe name) that replaces the whole sweep. The suite
     * points it at a socket it owns, and on WSL, where Discord is a Windows program and
     * there is no socket to find, it is the only way to reach one at all.
     */
    char   path[MMO_PRESENCE_PATH];
    char   location[MMO_PRESENCE_LINE];
    char   player[MMO_PRESENCE_LINE];
    int    dirty;      /* the lines differ from what Discord was last told */
    int    probe;      /* next candidate the sweep will try */
    long   retry_at;   /* earliest a connect may be attempted */
    long   send_at;    /* earliest an activity may be sent */
    unsigned nonce;
    unsigned long sent; /* activities handed over, for logs and the suite */
    unsigned char tx[MMO_PRESENCE_TX];
    size_t tx_len, tx_head;
    unsigned char rx[MMO_PRESENCE_RX];
    size_t rx_len;
} mmo_presence;

/* This build's application id: OPENMMO_DISCORD_APP if a development build's
 * environment names one, else MMO_PRESENCE_APP_ID. */
const char *mmo_presence_app_id(void);

/* Ready one. An empty or absent app_id leaves it OFF for good: a build with no
 * application id never opens anything. */
void mmo_presence_init(mmo_presence *p, const char *app_id);

/* The two lines, as UTF-8. Either may be NULL or empty, which is that line
 * left out; both empty is a clear, which is what a session ending looks like
 * from a process that keeps running. Copying identical text is not a change
 * and does not spend the rate limit. */
void mmo_presence_set(mmo_presence *p, const char *location, const char *player);

/* One frame's worth of work: probe, handshake, drain, send if it is time.
 * `now` is seconds from the caller's own clock, the same one client.c times
 * its deadlines with, and the reason this file needs no clock of its own. */
void mmo_presence_tick(mmo_presence *p, long now);

/* Hang up. Discord clears the card when the pipe closes, so this is the whole
 * teardown; the process exiting does the same thing. */
void mmo_presence_close(mmo_presence *p);

/* Discord is listening and has taken at least the handshake. */
int mmo_presence_live(const mmo_presence *p);

/* The SET_ACTIVITY payload this presence would send right now, as a NUL
 * terminated string: what the suite reads instead of standing up a Discord.
 * Returns the length written, 0 if it did not fit. */
size_t mmo_presence_activity_json(const mmo_presence *p, char *out, size_t cap);

#endif /* MMO_PRESENCE_H */
