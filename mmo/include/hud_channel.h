/* Structured records for the host-drawn second screen. */

#ifndef OPENMMO_HUD_CHANNEL_H
#define OPENMMO_HUD_CHANNEL_H

#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OPENMMO_HUD_MAGIC   0x4F4D4855u /* 'OMHU' */
#define OPENMMO_HUD_VERSION 11u
#define OPENMMO_HUD_SUFFIX  ".hud"

#define OPENMMO_HUD_LOWER_POKETCH 0u
#define OPENMMO_HUD_LOWER_GUEST   1u

#define OPENMMO_HUD_APP_CHAT    0u
#define OPENMMO_HUD_APP_FRIENDS 1u
#define OPENMMO_HUD_APP_GUILD   2u
#define OPENMMO_HUD_APP_MAP     3u
#define OPENMMO_HUD_APP_PARTY   4u
#define OPENMMO_HUD_APP_NET     5u
#define OPENMMO_HUD_APP_N       6u

/* Wire chat types (game.h MMO_CHAT_*), repeated so the window need not compile
 * against the client's headers. tests/hud_channel_test.c holds them equal. */
#define OPENMMO_HUD_CHAT_NORMAL  0u
#define OPENMMO_HUD_CHAT_SHOUT   3u
#define OPENMMO_HUD_CHAT_WHISPER 4u
#define OPENMMO_HUD_CHAT_TRADE   5u
#define OPENMMO_HUD_CHAT_GLOBAL  6u
#define OPENMMO_HUD_CHAT_CHANNEL 7u
#define OPENMMO_HUD_CHAT_TEAM    8u
#define OPENMMO_HUD_CHAT_LINK    9u
#define OPENMMO_HUD_CHAT_SYSTEM  16u
#define OPENMMO_HUD_CHAT_NOTICE  17u
#define OPENMMO_HUD_CHAT_BATTLE  18u

#define OPENMMO_HUD_NAME    24
/* Holds the client's whole 128-byte chat line; at 80 the page was a third
 * truncation on top of the wire's and the window could never show what the
 * server actually said. */
#define OPENMMO_HUD_TEXT    136
#define OPENMMO_HUD_MOTD    128
#define OPENMMO_HUD_TAG     8
#define OPENMMO_HUD_REASON  128
#define OPENMMO_HUD_BATTLE  64
#define OPENMMO_HUD_CHAT_N  64
#define OPENMMO_HUD_FRIEND_N 64
#define OPENMMO_HUD_MEMBER_N 64
#define OPENMMO_HUD_PEER_N  16
#define OPENMMO_HUD_PARTY_N 6
/* Nameplates the window paints over the world. Twelve is the visible-crowd
 * ceiling (ENGINE_LIMITS 15) with a little room; a plate past this many is not
 * drawn rather than drawn somewhere wrong. */
#define OPENMMO_HUD_PLATE_N 16
/* Objective rows for the instance window (s2c 0xD3/0xD4). The official client's own
 * tracker is a table of a handful of rows; sixteen is past anything the
 * store has been seen to hold, and a row past this many is dropped rather
 * than drawn somewhere wrong. */
#define OPENMMO_HUD_OBJECTIVE_N 16

#define OPENMMO_HUD_GLYPHS      509u
#define OPENMMO_HUD_CELL        16u
#define OPENMMO_HUD_GLYPH_BYTES 128u /* 16 x 16 at 4bpp */
#define OPENMMO_HUD_ADVANCE     512u /* 509 used; 512 so the page stays aligned */

#define OPENMMO_HUD_CMD_SLOTS 64u

#define OPENMMO_HUD_CMD_APP     1u
#define OPENMMO_HUD_CMD_SCROLL  2u
#define OPENMMO_HUD_CMD_COMPOSE 3u
#define OPENMMO_HUD_CMD_PANEL   4u
/* Open one of the engine's own screens. The window's HUD bar is the official client's
 * (bottom right, ten buttons); five of those buttons name a screen the
 * pokeplatinum port already draws, so the window asks for it rather than
 * growing a second bag. The argument is one of OPENMMO_HUD_SCREEN_*. */
#define OPENMMO_HUD_CMD_SCREEN  5u
/*
 * Act on a player by name, the window's frames raise the official client's player menu (Challenge,
 * Whisper, Trade, ...) and the pick runs through the guest's own action layer
 * (openmmo_player.c), which is the half that holds the wire. The argument packs the verb in
 * bits 8..
 */
#define OPENMMO_HUD_CMD_PLAYER  6u
/* Leave the game back to character select: the guest disconnects politely
 * and re-execs itself with the preset character cleared, so the fresh boot
 * pauses at the lobby. The window asked because the official client's Menu > Logout
 * goes exactly there. No argument. */
#define OPENMMO_HUD_CMD_LOGOUT  7u
/* Reorder the party: a slot dragged onto another on the strip, the official client's own gesture. */
#define OPENMMO_HUD_CMD_PARTY_MOVE 8u
/*
 * The channel a composed line sends on, as a wire chat type (OPENMMO_HUD_CHAT_*): the window's
 * active tab picks it, the official client's own rule that the tab you are reading is the channel you are
 * speaking on.
 */
#define OPENMMO_HUD_CMD_CHANNEL 9u

/*
 * The trade link, from the window's own GTL frame. The verb rides the low byte; what rides
 * above it is the verb's own.
 */
#define OPENMMO_HUD_CMD_GTL 10u

#define OPENMMO_HUD_GTL_ASK       0 /* arg: kind<<8 | sort<<12 | page<<16 */
#define OPENMMO_HUD_GTL_BUY       1 /* arg: row<<8, cmd_gtl.qty units of it */
#define OPENMMO_HUD_GTL_BACK      2 /* arg: row<<8, take that own row back */
#define OPENMMO_HUD_GTL_SELL      3 /* arg: party slot<<8, cmd_gtl.price */
#define OPENMMO_HUD_GTL_SELL_ITEM 4 /* arg: bag row<<8, cmd_gtl.qty+price */
#define OPENMMO_HUD_GTL_CLAIM     5 /* arg: row<<8; row 255 = whole page */
#define OPENMMO_HUD_GTL_REPRICE   6 /* arg: row<<8, cmd_gtl.price */
#define OPENMMO_HUD_GTL_LOG       7 /* ask for the trade log */
#define OPENMMO_HUD_GTL_MARKET    8 /* arg: quote row<<8, cmd_gtl.qty, .price
                                     * the whole-purchase budget */

/* The mailbox, from the window's own Mail frame. Verb in the low byte, the
 * verb's own argument above it; a compose carries three strings too wide for
 * that, so they ride `cmd_mail`, written before the command word the way
 * cmd_gtl is. The guest answers by refreshing snap.mail. */
#define OPENMMO_HUD_CMD_MAIL 11u

#define OPENMMO_HUD_MAIL_ASK     0 /* arg: sent<<8 | page<<16, ask for a page */
#define OPENMMO_HUD_MAIL_READ    1 /* arg: row<<8, open that row's letter */
#define OPENMMO_HUD_MAIL_DELETE  2 /* arg: row<<8 */
#define OPENMMO_HUD_MAIL_SEND    3 /* cmd_mail carries to / subject / body */
/* Open the letter on the engine's own stationery (applications/mail.c),
 * which is the screen the game already draws. arg: row<<8. */
#define OPENMMO_HUD_MAIL_PAPER   4

#define OPENMMO_HUD_ACT_CHALLENGE 0
#define OPENMMO_HUD_ACT_WHISPER   1
#define OPENMMO_HUD_ACT_TRADE     2
#define OPENMMO_HUD_ACT_LINK      3
#define OPENMMO_HUD_ACT_FRIEND    4  /* toggles: add when absent, else remove */
#define OPENMMO_HUD_ACT_BLOCK     5
#define OPENMMO_HUD_ACT_N         6

#define OPENMMO_HUD_SCREEN_BAG     0
#define OPENMMO_HUD_SCREEN_PARTY   1
#define OPENMMO_HUD_SCREEN_DEX     2
#define OPENMMO_HUD_SCREEN_TRAINER 3
#define OPENMMO_HUD_SCREEN_OPTIONS 4
#define OPENMMO_HUD_SCREEN_START   5
/* One party member's status page, the slot in the argument's next byte
 * (screen | slot << 8): the strip's slots are the official client's, and a click on one
 * is that monster, not the list. */
#define OPENMMO_HUD_SCREEN_SUMMARY 6
/* The global trade link. Not an engine app: the guest's own list screen
 * (openmmo_gtl.c) opens on the next settled field. */
#define OPENMMO_HUD_SCREEN_GTL     7
#define OPENMMO_HUD_SCREEN_N       8

struct openmmo_hud_chat {
    uint32_t type; /* OPENMMO_HUD_CHAT_* */
    char     sender[OPENMMO_HUD_NAME];
    char     text[OPENMMO_HUD_TEXT];
};

struct openmmo_hud_party {
    char     name[OPENMMO_HUD_NAME];
    uint32_t species;
    uint32_t level;
    uint32_t hp;
    uint32_t max_hp; /* 0 when the wire did not send one */
    uint32_t status;
    uint32_t egg;
};

struct openmmo_hud_person {
    char     name[OPENMMO_HUD_NAME];
    uint32_t online;
};

struct openmmo_hud_guild {
    char     name[OPENMMO_HUD_NAME];
    char     tag[OPENMMO_HUD_TAG];
    char     motd[OPENMMO_HUD_MOTD];
    uint32_t member_n;
    struct openmmo_hud_person member[OPENMMO_HUD_MEMBER_N];
};

struct openmmo_hud_map {
    char     region[OPENMMO_HUD_NAME];
    char     name[OPENMMO_HUD_NAME];
    uint32_t x;
    uint32_t y;
    uint32_t peer_n;
    char     peer[OPENMMO_HUD_PEER_N][OPENMMO_HUD_NAME];
};

/* One name over one player's head, as the window is to draw it. */
struct openmmo_hud_plate {
    int32_t  x;
    int32_t  y;
    char     name[OPENMMO_HUD_NAME];
};

/* One row of the official client's instance window: the id / value / count triple s2c
 * 0xD3 and 0xD4 carry. The definition table those ids index (name, type,
 * string id) is not on the wire, so the window shows the id. */
struct openmmo_hud_objective {
    int32_t id;
    int32_t value;
    int32_t count;
};

struct openmmo_hud_net {
    uint32_t state;       /* OPENMMO_ST_* from status_channel.h */
    int32_t  latency_ms;  /* < 0 = none */
    char     reason[OPENMMO_HUD_REASON];
    char     battle[OPENMMO_HUD_BATTLE];
};

/*
 * One page of the trade link's shelf, as the guest's client store holds it
 * (openmmo_client_gtl). The window draws rows and answers clicks by row index; the guest
 * resolves an index against this same store, so the two sides can never name different
 * listings.
 */
#define OPENMMO_HUD_GTL_ROWS  10
#define OPENMMO_HUD_GTL_LOG_N 30
#define OPENMMO_HUD_GTL_NONE  255u /* nature/level "not carried" marker */

struct openmmo_hud_gtl_row {
    uint32_t kind;     /* 0 a monster, 1 an item stack */
    uint32_t price;    /* the unit price */
    uint32_t quantity;
    char     label[OPENMMO_HUD_NAME]; /* the species' or the item's name */
    uint32_t species;  /* engine species id; 0 none */
    uint32_t level;
    uint32_t nature;   /* 0..24; OPENMMO_HUD_GTL_NONE when not carried */
    uint32_t shiny;
    uint8_t  iv[8];    /* hp,atk,def,speed,spatk,spdef + two pad bytes */
    uint32_t listed_at;  /* unix seconds */
    uint32_t expires_at;
    uint32_t own_state;     /* own pages: 0 active, 1 sold, 2 closed */
    uint32_t own_remaining; /* own pages: units still up */
    uint32_t own_unclaimed; /* own pages: sold units awaiting Claim */
};

/* The item page's quote strip: the cheapest ask per item, the window's
 * Item Market tab. */
struct openmmo_hud_gtl_quote {
    uint32_t item;     /* server item id (what a MARKET buy names) */
    uint32_t price;
    char     label[OPENMMO_HUD_NAME];
};

/* One trade-log fill. type packs direction and kind: 0 sold a monster,
 * 1 sold an item stack, 2 bought a monster, 3 bought an item stack. */
struct openmmo_hud_gtl_logrow {
    uint32_t type;
    uint32_t amount;   /* item fills the quantity; monster fills the level */
    uint32_t total;    /* what the whole fill went for */
    uint32_t epoch;    /* when, unix seconds */
    char     label[OPENMMO_HUD_NAME]; /* what sold or arrived, by name */
};

struct openmmo_hud_gtl {
    uint32_t open;     /* the session answered (0xDC) */
    uint32_t kind;     /* the held page's kind (0 mon, 1 item, 2 own) */
    uint32_t page;
    uint32_t total;    /* matches behind the held page */
    uint32_t row_n;
    struct openmmo_hud_gtl_row row[OPENMMO_HUD_GTL_ROWS];
    uint32_t quote_n;
    struct openmmo_hud_gtl_quote quote[OPENMMO_HUD_GTL_ROWS];
    uint32_t log_n;
    struct openmmo_hud_gtl_logrow log[OPENMMO_HUD_GTL_LOG_N];
    /* The last shelf answer (s2c 0xAF). The window toasts each exactly once
     * by watching the sequence; the code map is game.h MMO_GTL_R_*. */
    int32_t  result_code;
    int32_t  result_a;
    int32_t  result_b;
    uint32_t result_seq;
};

/*
 * The mailbox, as the guest's client store holds it (openmmo_client_mail). Ten rows to a page
 * because that is the official client's own pager, f/rV0 is a Cp0(10, 8): ten rows, eight page buttons,
 * so a page here and a page on the wire are the same thing.
 */
#define OPENMMO_HUD_MAIL_ROWS    10
#define OPENMMO_HUD_MAIL_SUBJECT 48
#define OPENMMO_HUD_MAIL_BODY    2048

struct openmmo_hud_mail_row {
    uint32_t id_lo;    /* the letter's id, split so the -m32 game and the */
    uint32_t id_hi;    /* 64-bit window lay the struct out the same way */
    char     who[OPENMMO_HUD_NAME];  /* sender on the inbox, recipient on sent */
    char     subject[OPENMMO_HUD_MAIL_SUBJECT];
    uint32_t epoch;    /* when it was sent, unix seconds */
    uint32_t unread;
};

struct openmmo_hud_mail {
    uint32_t valid;       /* a mail packet has arrived */
    uint32_t inbox;       /* whole-box counts, not this page's */
    uint32_t unread;      /* what raises the bar's mail badge */
    uint32_t sent;
    uint32_t listed_sent; /* which box the held page is */
    uint32_t page;
    uint32_t row_n;
    struct openmmo_hud_mail_row row[OPENMMO_HUD_MAIL_ROWS];
    /* The letter that is open, if one is. `open_id` zero: none. */
    uint32_t open_id_lo;
    uint32_t open_id_hi;
    uint32_t open_sent;
    uint32_t open_epoch;
    char     open_who[OPENMMO_HUD_NAME];
    char     open_subject[OPENMMO_HUD_MAIL_SUBJECT];
    char     open_body[OPENMMO_HUD_MAIL_BODY];
    /* The last compose answer (s2c 0x96). The window toasts each exactly
     * once by watching the sequence; the codes are the official client's sj1.Y90. */
    int32_t  result_code;
    uint32_t result_seq;
};

/* CMD_MAIL_SEND's three strings, the same ordering discipline as cmd_gtl. */
struct openmmo_hud_mail_send {
    char to[OPENMMO_HUD_NAME];
    char subject[OPENMMO_HUD_MAIL_SUBJECT];
    char body[OPENMMO_HUD_MAIL_BODY];
};

/* The bag, for the create-listing picker. */
#define OPENMMO_HUD_BAG_N 64

struct openmmo_hud_bag {
    uint32_t item;     /* server item id */
    uint32_t count;
    char     label[OPENMMO_HUD_NAME];
};

/* CMD_GTL's wide arguments: filters for ASK, amounts for the money verbs.
 * One slot, window-written; the guest reads it when it consumes the verb,
 * and two asks in one frame keep the later one, which is also the page the
 * window wants. Unset filters are -1 (species: the empty string). */
struct openmmo_hud_gtl_ask {
    char    species[OPENMMO_HUD_NAME]; /* species NAME text; resolved on the guest */
    int32_t min_level;
    int32_t max_level;
    int32_t shiny;      /* -1 unset, else 0/1 */
    int32_t nature;     /* -1 unset, 0..24 */
    int32_t min_price;
    int32_t max_price;
    int32_t price;      /* SELL/SELL_ITEM/REPRICE unit price; MARKET budget */
    int32_t qty;        /* BUY/SELL_ITEM/MARKET unit count */
};

struct openmmo_hud_snap {
    uint32_t lower;      /* OPENMMO_HUD_LOWER_* */
    uint32_t app;        /* OPENMMO_HUD_APP_* */
    uint32_t chat_n;
    uint32_t party_n;
    uint32_t friends_n;
    uint32_t font_ready;
    uint32_t plate_n;
    uint32_t objective_n;
    /* The guest has something of its own over the world, a message, a menu,
     * a scene. The window's own panel stands aside while this is set: a
     * translucent chat overlay across a dialog box is two programs talking at
     * once, and the guest's is the one being answered. */
    uint32_t guest_busy;
    /* This save owns a Pokedex. A fresh character does not, and official
     * disables the button rather than opening a screen that cannot exist;
     * the window needs the fact to draw the same refusal. (The party's is
     * party_n.) */
    uint32_t has_dex;
    /*
     * The player is in the Underground. The cavern hands the two screens back the other way
     * round and holds resources of its own on the field's heaps, and the official client's Underground has
     * no start menu at all, X opens the Explorer Kit's menu there, not the bag.
     */
    uint32_t underground;
    struct openmmo_hud_chat   chat[OPENMMO_HUD_CHAT_N];
    struct openmmo_hud_party  party[OPENMMO_HUD_PARTY_N];
    struct openmmo_hud_person friends[OPENMMO_HUD_FRIEND_N];
    struct openmmo_hud_guild  guild;
    struct openmmo_hud_map    map;
    struct openmmo_hud_net    net;
    struct openmmo_hud_plate  plate[OPENMMO_HUD_PLATE_N];
    struct openmmo_hud_objective objective[OPENMMO_HUD_OBJECTIVE_N];
    struct openmmo_hud_gtl    gtl;
    struct openmmo_hud_mail   mail;
    /* The wallet and the bag, for the GTL's create picker and buy math. */
    uint32_t money;
    uint32_t bag_n;
    struct openmmo_hud_bag    bag[OPENMMO_HUD_BAG_N];
    uint32_t composing;
    char     compose[OPENMMO_HUD_TEXT];
    /* The whisper's target while the open compose is one (the official client's whisper
     * prefills the chat line with its recipient); empty for plain chat. */
    char     compose_to[OPENMMO_HUD_NAME];
    /*
     * The channel the next composed line actually goes out on, as the guest holds it
     * (OPENMMO_HUD_CHAT_*).
     */
    uint32_t send_type;
};

struct openmmo_hud_font {
    uint32_t glyph_n;
    uint32_t cell;
    uint8_t  gfx[OPENMMO_HUD_GLYPHS][OPENMMO_HUD_GLYPH_BYTES];
    uint8_t  advance[OPENMMO_HUD_ADVANCE];
};

struct openmmo_hud_shm {
    uint32_t magic;
    uint32_t version;
    volatile uint32_t writer;
    volatile uint32_t seq;
    struct openmmo_hud_snap snap;
    struct openmmo_hud_font font;
    volatile uint32_t cmd_head;
    uint32_t cmd_pad;
    uint32_t cmd[OPENMMO_HUD_CMD_SLOTS];
    /* CMD_PLAYER's names, indexed by the slot byte of the command's own
     * argument. Written by the window before the command word, so the
     * ACQUIRE on cmd_head orders both. */
    char     cmd_name[OPENMMO_HUD_CMD_SLOTS][OPENMMO_HUD_NAME];
    /* CMD_GTL's wide arguments, same ordering discipline. */
    struct openmmo_hud_gtl_ask cmd_gtl;
    /* CMD_MAIL_SEND's three strings, same discipline again. */
    struct openmmo_hud_mail_send cmd_mail;
};

/* Zero an ask to "no filters, no amounts": -1 everywhere, empty species. */
static inline void openmmo_hud_gtl_ask_clear(struct openmmo_hud_gtl_ask *a)
{
    memset(a, 0, sizeof *a);
    a->min_level = -1;
    a->max_level = -1;
    a->shiny = -1;
    a->nature = -1;
    a->min_price = -1;
    a->max_price = -1;
    a->price = -1;
    a->qty = 1;
}

static inline uint32_t openmmo_hud_cmd(uint32_t kind, int32_t arg)
{
    return (kind << 24) | ((uint32_t)arg & 0xFFFFFFu);
}

static inline uint32_t openmmo_hud_cmd_kind(uint32_t e)
{
    return e >> 24;
}

static inline int32_t openmmo_hud_cmd_arg(uint32_t e)
{
    int32_t a = (int32_t)(e & 0xFFFFFFu);

    if (a & 0x800000)
        a |= (int32_t)~0xFFFFFFu;
    return a;
}

static inline void openmmo_hud_push(struct openmmo_hud_shm *h, uint32_t kind,
                                    int32_t arg)
{
    uint32_t head;

    if (h == 0 || h->magic != OPENMMO_HUD_MAGIC ||
        h->version != OPENMMO_HUD_VERSION)
        return;
    head = h->cmd_head;
    h->cmd[head % OPENMMO_HUD_CMD_SLOTS] = openmmo_hud_cmd(kind, arg);
    __atomic_store_n(&h->cmd_head, head + 1u, __ATOMIC_RELEASE);
}

/* Push CMD_PLAYER: the verb and the name, which rides the slot the command
 * itself lands in. */
static inline void openmmo_hud_push_player(struct openmmo_hud_shm *h,
                                           int32_t verb, const char *name)
{
    uint32_t head, slot;
    unsigned i;

    if (h == 0 || name == 0 || h->magic != OPENMMO_HUD_MAGIC ||
        h->version != OPENMMO_HUD_VERSION)
        return;
    head = h->cmd_head;
    slot = head % OPENMMO_HUD_CMD_SLOTS;
    for (i = 0; i < OPENMMO_HUD_NAME - 1u && name[i] != '\0'; i++)
        h->cmd_name[slot][i] = name[i];
    h->cmd_name[slot][i] = '\0';
    openmmo_hud_push(h, OPENMMO_HUD_CMD_PLAYER,
                     (int32_t)((uint32_t)verb << 8 | slot));
}

/* A consumed CMD_PLAYER's halves. The name pointer is into the page: copy
 * it before the ring wraps. */
static inline int32_t openmmo_hud_player_verb(int32_t arg)
{
    return arg >> 8;
}

static inline const char *openmmo_hud_player_name(
    const struct openmmo_hud_shm *h, int32_t arg)
{
    return h->cmd_name[((uint32_t)arg & 0xFFu) % OPENMMO_HUD_CMD_SLOTS];
}

static inline unsigned openmmo_hud_read_cmds(const struct openmmo_hud_shm *h,
                                             uint32_t *tailp, uint32_t *dst,
                                             unsigned max, uint32_t *dropped)
{
    uint32_t head, tail, avail;
    unsigned n = 0;

    if (dropped)
        *dropped = 0;
    if (h == 0 || dst == 0 || tailp == 0)
        return 0;
    if (h->magic != OPENMMO_HUD_MAGIC || h->version != OPENMMO_HUD_VERSION)
        return 0;

    head = __atomic_load_n(&h->cmd_head, __ATOMIC_ACQUIRE);
    tail = *tailp;
    avail = head - tail;
    if (avail > OPENMMO_HUD_CMD_SLOTS) {
        uint32_t lost = avail - OPENMMO_HUD_CMD_SLOTS;

        if (dropped)
            *dropped = lost;
        tail += lost;
        avail = OPENMMO_HUD_CMD_SLOTS;
    }
    while (n < max && avail > 0) {
        dst[n++] = h->cmd[tail++ % OPENMMO_HUD_CMD_SLOTS];
        avail--;
    }
    *tailp = tail;
    return n;
}

static inline void openmmo_hud_publish_font(struct openmmo_hud_shm *h,
                                            const struct openmmo_hud_font *font)
{
    if (h == 0 || font == 0 || h->magic != OPENMMO_HUD_MAGIC ||
        h->version != OPENMMO_HUD_VERSION)
        return;
    memcpy((void *)&h->font, font, sizeof *font);
    __sync_synchronize();
}

static inline void openmmo_hud_publish(struct openmmo_hud_shm *h,
                                       const struct openmmo_hud_snap *snap)
{
    if (h == 0 || snap == 0 || h->magic != OPENMMO_HUD_MAGIC ||
        h->version != OPENMMO_HUD_VERSION)
        return;

    __atomic_store_n(&h->seq, h->seq + 1u, __ATOMIC_RELEASE);
    memcpy((void *)&h->snap, snap, sizeof *snap);
    __atomic_store_n(&h->seq, h->seq + 1u, __ATOMIC_RELEASE);
}

static inline int openmmo_hud_read(const struct openmmo_hud_shm *h,
                                   struct openmmo_hud_snap *snap)
{
    unsigned t;

    if (h == 0 || snap == 0)
        return 0;
    if (h->magic != OPENMMO_HUD_MAGIC || h->version != OPENMMO_HUD_VERSION)
        return 0;

    for (t = 0; t < 16u; t++) {
        uint32_t s0 = h->seq, s1;

        if (s0 & 1u)
            continue;
        __sync_synchronize();
        memcpy(snap, (const void *)&h->snap, sizeof *snap);
        __sync_synchronize();
        s1 = h->seq;
        if (s0 != s1)
            continue;
        return 1;
    }
    return 0;
}

#ifdef __cplusplus
}
#endif

#endif /* OPENMMO_HUD_CHANNEL_H */
