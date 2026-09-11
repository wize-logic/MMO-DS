/* An Apricorn tree on a ported map. */

#include <stdio.h>
#include <string.h>

#include "field/field_system.h"
#include "field_system.h"
#include "map_object.h"
#include "script_manager.h"

#include "../../../include/client.h"

#define APRICORN_BASE 23000          /* portmap.APRICORN_BASE */
#define APRICORN_ENTRY_ASK 0
#define APRICORN_ENTRY_NONE 1
#define APRICORN_ENTRY_NO_BOX 2
#define APRICORN_ENTRY_PICKED 3
#define APRICORN_KINDS 7
#define APRICORN_VERDICT_FRAMES 600

extern int openmmo_underground_active(void); /* openmmo_underground.c */

static openmmo_client *s_client;
static int s_asked;          /* a pick went up; waiting on the verdict */
static int s_frames;
static int s_verdict;        /* -1 none yet, else the byte the server sent */
static MapObject *s_tree;

void openmmo_apricorn_attach(openmmo_client *c)
{
    s_client = c;
    s_asked = 0;
    s_verdict = -1;
    s_tree = NULL;
}

int openmmo_apricorn_active(void)
{
    return s_asked;
}

/* ScriptManager_Set's hook (patches/src/script_manager.c.patch): a script of
 * the apricorn bank is starting on `object`. Entry 0 is the press; the tree's
 * index is the object's first data word, as HeartGold's routine reads it. */
void openmmo_apricorn_script_set(unsigned scriptID, void *object)
{
    const openmmo_world_state *ws;
    u8 idx;

    if (scriptID != APRICORN_BASE + APRICORN_ENTRY_ASK || object == NULL)
        return;
    if (s_client == NULL)
        return;
    ws = openmmo_client_world_state(s_client);
    if (ws == NULL || !ws->valid)
        return;
    idx = (u8)MapObject_GetDataAt((MapObject *)object, 0);
    if (openmmo_client_ug_talk_send(s_client, MMO_UG_TALK_APRICORN_PICK, 0, &idx, 1) != 0) {
        printf("openmmo: apricorn tree %u: the pick could not be sent\n", idx);
        return;
    }
    s_asked = 1;
    s_frames = 0;
    s_verdict = -1;
    s_tree = (MapObject *)object;
    printf("openmmo: apricorn tree %u, asking the server\n", idx);
}

void openmmo_apricorn_recv(const mmo_underground_talk *msg)
{
    if (msg == NULL || msg->kind != MMO_UG_TALK_APRICORN_VERDICT)
        return;
    if (!s_asked) {
        printf("openmmo: apricorn verdict with no tree asked\n");
        return;
    }
    s_verdict = msg->len > 0 ? msg->data[0] : 1;
    printf("openmmo: apricorn: %s\n",
           s_verdict == 0 ? "no Apricorn Box"
           : s_verdict == 1 ? "nothing on the tree today"
           : "picked one");
}

static void pump(void)
{
    mmo_underground_talk msg;

    if (s_client == NULL || openmmo_underground_active())
        return;
    while (openmmo_client_ug_talk_recv(s_client, &msg)) {
        extern void openmmo_talk_route(const mmo_underground_talk *m);

        openmmo_talk_route(&msg);
    }
}

/* Once a frame. The answer's entry starts the moment the field is free,
 * the press's own script has to have ended, or the two would fight for the
 * one task. */
void openmmo_apricorn_tick(FieldSystem *fs)
{
    unsigned entry;

    if (!s_asked)
        return;
    if (s_verdict < 0) {
        pump();
        if (s_verdict < 0) {
            if (++s_frames > APRICORN_VERDICT_FRAMES) {
                printf("openmmo: apricorn: no verdict in %d frames\n",
                       APRICORN_VERDICT_FRAMES);
                s_asked = 0;
            }
            return;
        }
    }
    if (fs == NULL || fs->task != NULL || !FieldSystem_IsRunningFieldMap(fs)
        || FieldSystem_HasChildProcess(fs))
        return;
    if (s_verdict == 0)
        entry = APRICORN_ENTRY_NO_BOX;
    else if (s_verdict == 1)
        entry = APRICORN_ENTRY_NONE;
    else if (s_verdict - 2 < APRICORN_KINDS)
        entry = APRICORN_ENTRY_PICKED + (unsigned)(s_verdict - 2);
    else {
        printf("openmmo: apricorn: verdict %d names no kind\n", s_verdict);
        s_asked = 0;
        return;
    }
    s_asked = 0;
    ScriptManager_Set(fs, (u16)(APRICORN_BASE + entry), s_tree);
}
