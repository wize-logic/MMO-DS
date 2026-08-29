/* Parse a client-action script into a typed action list. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "script.h"

typedef struct { const char *name; int val; } name_val;

/* status name -> openmmo_status */
static const name_val kStatus[] = {
    { "disconnected",    OPENMMO_DISCONNECTED },
    { "connecting",      OPENMMO_CONNECTING },
    { "handshaking",     OPENMMO_HANDSHAKING },
    { "authenticating",  OPENMMO_AUTHENTICATING },
    { "authed",          OPENMMO_AUTHED },
    { "requesting_game", OPENMMO_REQUESTING_GAME },
    { "joining_game",    OPENMMO_JOINING_GAME },
    { "in_game",         OPENMMO_IN_GAME },
    { "failed",          OPENMMO_FAILED },
};

/* event name -> openmmo_event_kind */
static const name_val kEvent[] = {
    { "status",         OPENMMO_EV_STATUS },
    { "joined",         OPENMMO_EV_JOINED },
    { "failed",         OPENMMO_EV_FAILED },
    { "disconnected",   OPENMMO_EV_DISCONNECTED },
    { "entity_spawn",   OPENMMO_EV_ENTITY_SPAWN },
    { "entity_step",    OPENMMO_EV_ENTITY_STEP },
    { "entity_turn",    OPENMMO_EV_ENTITY_TURN },
    { "entity_despawn", OPENMMO_EV_ENTITY_DESPAWN },
    { "self_correct",   OPENMMO_EV_SELF_CORRECT },
    { "warp",           OPENMMO_EV_WARP },
    { "map",            OPENMMO_EV_MAP },
    { "weather",        OPENMMO_EV_WEATHER },
    { "encounter",      OPENMMO_EV_ENCOUNTER },
    { "story_flag",     OPENMMO_EV_STORY_FLAG },
    { "party",          OPENMMO_EV_PARTY },
    { "bag",            OPENMMO_EV_BAG },
    { "money",          OPENMMO_EV_MONEY },
    { "shop",           OPENMMO_EV_SHOP },
    { "chat",           OPENMMO_EV_CHAT },
    { "dialog",         OPENMMO_EV_DIALOG },
    { "script_move",    OPENMMO_EV_SCRIPT_MOVE },
    { "objective",      OPENMMO_EV_OBJECTIVE },
    { "friends",        OPENMMO_EV_FRIENDS },
    { "guild",          OPENMMO_EV_GUILD },
    { "mail",           OPENMMO_EV_MAIL },
    { "link",           OPENMMO_EV_LINK },
    { "ui",             OPENMMO_EV_UI },
    { "sync",           OPENMMO_EV_SYNC },
};

/* engine facing DIR_* (NORTH=0/SOUTH=1/WEST=2/EAST=3) */
static const name_val kDir[] = {
    { "north", 0 }, { "south", 1 }, { "west", 2 }, { "east", 3 },
};

static int lookup(const name_val *tab, int n, const char *name)
{
    for (int i = 0; i < n; i++)
        if (strcmp(tab[i].name, name) == 0)
            return tab[i].val;
    return -1;
}
#define LOOKUP(tab, name) lookup((tab), \
    (int)(sizeof(tab) / sizeof(*(tab))), (name))

static void fail(char *err, size_t sz, int line, const char *msg)
{
    if (err && sz)
        snprintf(err, sz, "line %d: %s", line, msg);
}

const char *openmmo_script_op_name(openmmo_script_op op)
{
    switch (op) {
    case OPENMMO_SCRIPT_CONNECT:      return "connect";
    case OPENMMO_SCRIPT_AWAIT_STATUS: return "await";
    case OPENMMO_SCRIPT_AWAIT_EVENT:  return "await-event";
    case OPENMMO_SCRIPT_FRAMES:       return "frames";
    case OPENMMO_SCRIPT_MOVE:         return "move";
    case OPENMMO_SCRIPT_CHAT:         return "chat";
    case OPENMMO_SCRIPT_DISCONNECT:   return "disconnect";
    }
    return "?";
}

int openmmo_script_parse(const char *text, size_t len, openmmo_script *out,
                         char *err, size_t err_sz)
{
    out->count = 0;

    size_t i = 0;
    int lineno = 0;
    while (i < len) {
        /* Slice out one line [i, eol). */
        size_t eol = i;
        while (eol < len && text[eol] != '\n')
            eol++;
        size_t raw_start = i, raw_end = eol;
        i = (eol < len) ? eol + 1 : eol;
        lineno++;

        /* Trim a trailing '\r' and surrounding blanks. */
        if (raw_end > raw_start && text[raw_end - 1] == '\r')
            raw_end--;
        size_t s = raw_start, e = raw_end;
        while (s < e && (text[s] == ' ' || text[s] == '\t'))
            s++;
        while (e > s && (text[e - 1] == ' ' || text[e - 1] == '\t'))
            e--;

        if (s == e || text[s] == '#')
            continue;               /* blank or whole-line comment */

        /* Copy the trimmed line into a bounded scratch buffer. */
        char buf[256];
        size_t n = e - s;
        if (n >= sizeof buf) {
            fail(err, err_sz, lineno, "line too long");
            return -1;
        }
        memcpy(buf, text + s, n);
        buf[n] = '\0';

        /* Verb is the first whitespace-delimited token; `rest` is what follows,
         * already left-trimmed, for the arg or the chat text. */
        char *verb = buf;
        char *rest = buf;
        while (*rest && *rest != ' ' && *rest != '\t')
            rest++;
        if (*rest) {
            *rest++ = '\0';
            while (*rest == ' ' || *rest == '\t')
                rest++;
        }
        /* Strip a trailing "whitespace + #" inline comment from the argument,
         * the base port's PC_INPUT convention, but never from a chat message,
         * whose payload is verbatim and may itself contain a '#'. */
        if (strcmp(verb, "chat") != 0) {
            char *h = rest;
            while (*h) {
                if (*h == '#' && (h == rest || h[-1] == ' ' || h[-1] == '\t')) {
                    *h = '\0';
                    break;
                }
                h++;
            }
            char *t = rest + strlen(rest);
            while (t > rest && (t[-1] == ' ' || t[-1] == '\t'))
                *--t = '\0';
        }
        int have_rest = (*rest != '\0');

        if (out->count >= OPENMMO_SCRIPT_MAX) {
            fail(err, err_sz, lineno, "too many directives");
            return -1;
        }
        openmmo_script_action *a = &out->act[out->count];
        memset(a, 0, sizeof *a);
        a->line = lineno;

        if (strcmp(verb, "connect") == 0) {
            a->op = OPENMMO_SCRIPT_CONNECT;
        } else if (strcmp(verb, "disconnect") == 0) {
            a->op = OPENMMO_SCRIPT_DISCONNECT;
        } else if (strcmp(verb, "await") == 0) {
            if (!have_rest) { fail(err, err_sz, lineno, "await needs a status"); return -1; }
            int v = LOOKUP(kStatus, rest);
            if (v < 0) { fail(err, err_sz, lineno, "unknown status"); return -1; }
            a->op = OPENMMO_SCRIPT_AWAIT_STATUS;
            a->arg = v;
        } else if (strcmp(verb, "await-event") == 0) {
            if (!have_rest) { fail(err, err_sz, lineno, "await-event needs a kind"); return -1; }
            int v = LOOKUP(kEvent, rest);
            if (v < 0) { fail(err, err_sz, lineno, "unknown event kind"); return -1; }
            a->op = OPENMMO_SCRIPT_AWAIT_EVENT;
            a->arg = v;
        } else if (strcmp(verb, "frames") == 0) {
            if (!have_rest) { fail(err, err_sz, lineno, "frames needs a count"); return -1; }
            char *end = NULL;
            long v = strtol(rest, &end, 10);
            if (end == rest || *end != '\0' || v <= 0) {
                fail(err, err_sz, lineno, "frames needs a positive count");
                return -1;
            }
            a->op = OPENMMO_SCRIPT_FRAMES;
            a->arg = (int)v;
        } else if (strcmp(verb, "move") == 0) {
            if (!have_rest) { fail(err, err_sz, lineno, "move needs a direction"); return -1; }
            int v = LOOKUP(kDir, rest);
            if (v < 0) { fail(err, err_sz, lineno, "unknown direction"); return -1; }
            a->op = OPENMMO_SCRIPT_MOVE;
            a->arg = v;
        } else if (strcmp(verb, "chat") == 0) {
            if (!have_rest) { fail(err, err_sz, lineno, "chat needs a message"); return -1; }
            if (strlen(rest) >= sizeof a->text) {
                fail(err, err_sz, lineno, "chat message too long");
                return -1;
            }
            a->op = OPENMMO_SCRIPT_CHAT;
            strcpy(a->text, rest);
        } else {
            fail(err, err_sz, lineno, "unknown directive");
            return -1;
        }
        out->count++;
    }

    return 0;
}
