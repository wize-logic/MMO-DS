/* The two facts Discord is told, gathered where they are. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "constants/heap.h"

#include "field/field_system.h"
#include "field_system.h"
#include "generated/text_banks.h"
#include "map_header.h"
#include "message.h"
#include "string_gf.h"

#include "res/text/bank/location_names.h"

#include "../../../include/charcode.h"
#include "../../../include/client.h"
#include "../../../include/platform.h"
#include "../../../include/presence.h"

/* charcode.h mirrors the engine's charcode_t as a uint16_t; this is one of the
 * two places the two meet, so it says so out loud. */
typedef char openmmo_presence_charcode_width_check
    [sizeof(mmo_charcode) == sizeof(charcode_t) ? 1 : -1];

/* Four times a second. The socket work is a poll and, at most once every
 * fifteen seconds, one small write; there is nothing here worth a frame. */
#define PRESENCE_EVERY 15

static mmo_presence g_pres;
static int g_started;
static unsigned g_frames;
static int g_header = -1; /* the header g_place was read from */
static int g_state = MMO_PRESENCE_OFF; /* the state the log last reported */
static unsigned long g_sent;           /* activities the log last reported */
static int g_told_missing;             /* the empty-sweep line is said once */
static char g_place[MMO_PRESENCE_LINE];

/*
 * The character name arrives as the wire carried it, which client.h records as Latin-1;
 * Discord takes JSON, which is UTF-8. Nothing else in this client has had to bridge those two,
 * because everything else that draws a name hands it to the engine's own glyphs.
 */
static void latin1_utf8(const char *src, char *dst, size_t cap)
{
    size_t o = 0;

    if (cap == 0)
        return;
    for (const unsigned char *s = (const unsigned char *)src; *s != '\0'; s++) {
        if (*s < 0x80) {
            if (o + 2 > cap)
                break;
            dst[o++] = (char)*s;
        } else {
            if (o + 3 > cap)
                break;
            dst[o++] = (char)(0xC0u | (*s >> 6));
            dst[o++] = (char)(0x80u | (*s & 0x3Fu));
        }
    }
    dst[o] = '\0';
}

static void refresh_place(FieldSystem *fs)
{
    String *s;
    u32 textID;
    int header;

    if (fs == NULL || fs->location == NULL || !FieldSystem_IsRunningFieldMap(fs))
        return; /* nothing to ask right now; the last place stands */
    header = (int)fs->location->mapHeaderID;
    if (header == g_header)
        return;

    g_header = header;
    textID = MapHeader_GetMapLabelTextID((enum MapHeaderID)header);
    if (textID == LocationNames_Text_MysteryZone) {
        g_place[0] = '\0';
        return;
    }
    s = MessageBank_GetNewStringFromNARC(NARC_INDEX_MSGDATA__PL_MSG,
                                         TEXT_BANK_LOCATION_NAMES, textID,
                                         HEAP_ID_FIELD2);
    if (s == NULL) {
        g_header = -1; /* try again next map change rather than cache nothing */
        return;
    }
    mmo_charcode_to_utf8((const mmo_charcode *)String_GetData(s), g_place,
                         sizeof g_place);
    String_Free(s);
}

/* What the log says, and why it says it at all. */
static const char *state_word(int state)
{
    switch (state) {
    case MMO_PRESENCE_IDLE:      return "is being looked for";
    case MMO_PRESENCE_HANDSHAKE: return "is open, waiting for its reply";
    case MMO_PRESENCE_READY:     return "is listening";
    }
    return "off";
}

static void start(void)
{
    const char *on = getenv("OPENMMO_DISCORD");

    g_started = 1;
    if (on == NULL || strcmp(on, "1") != 0) {
        mmo_presence_init(&g_pres, ""); /* OFF, and never asked again */
        /*
         * Said out loud, but only when somebody said it: a log with no presence line at all is
         * a build too old to have this, which is a different answer from a setting that is
         * off, and a report cannot tell them apart otherwise.
         */
        if (on != NULL)
            printf("openmmo: discord presence off (launcher setting)\n");
        return;
    }
    mmo_presence_init(&g_pres, mmo_presence_app_id());
    printf("openmmo: discord presence on\n");
    g_state = g_pres.state;
}

/* One line per change of state, and one per card that leaves. Both are rare,
 * a connect, a handshake, and at most one activity every fifteen seconds, so
 * neither can fill a log. */
static void log_progress(void)
{
    if (g_pres.state != g_state) {
        g_state = g_pres.state;
        if (g_state == MMO_PRESENCE_READY)
            g_told_missing = 0; /* a later loss is worth saying again */
        printf("openmmo: discord %s\n", state_word(g_state));
    }
    /*
     * The commonest answer of the four, and the only one whose evidence is otherwise a
     * Silence: every candidate was tried and none of them opened. retry_at is the tell, it
     * is zero until a sweep has run out, so this needs no clock of its own.
     */
    if (g_pres.state == MMO_PRESENCE_IDLE && g_pres.retry_at != 0
        && !g_told_missing) {
        g_told_missing = 1;
        printf("openmmo: discord not found, is the desktop app running? "
               "(the browser and phone apps have no pipe to talk to)\n");
    }
    if (g_pres.sent != g_sent) {
        g_sent = g_pres.sent;
        if (g_pres.location[0] == '\0' && g_pres.player[0] == '\0')
            printf("openmmo: discord card cleared\n");
        else
            printf("openmmo: discord card sent: %s / %s\n",
                   g_pres.location, g_pres.player);
    }
}

void openmmo_presence_tick(FieldSystem *fs, openmmo_client *c)
{
    const openmmo_world_state *ws = NULL;
    char who[MMO_PRESENCE_LINE];

    if (!g_started)
        start();
    if (g_pres.state == MMO_PRESENCE_OFF)
        return;
    if (++g_frames % PRESENCE_EVERY != 0)
        return;

    if (c != NULL && openmmo_client_status(c) == OPENMMO_IN_GAME)
        ws = openmmo_client_world_state(c);

    if (ws == NULL || !ws->valid || ws->name[0] == '\0') {
        /* Not in a world: the title, the character list, a session that ended.
         * Two empty lines is a clear, and the card goes away. */
        mmo_presence_set(&g_pres, "", "");
        g_header = -1;
        g_place[0] = '\0';
    } else {
        latin1_utf8(ws->name, who, sizeof who);
        refresh_place(fs);
        /* Not until the first map has been asked. */
        if (g_header != -1)
            mmo_presence_set(&g_pres, g_place, who);
    }

    mmo_presence_tick(&g_pres, mmo_plat_seconds());
    log_progress();
}

void openmmo_presence_shutdown(void)
{
    if (g_started)
        mmo_presence_close(&g_pres);
}
