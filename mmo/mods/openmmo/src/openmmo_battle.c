/* Who supplies the battle scene's commands. */

#include <nitro.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "constants/battle/battle_controller.h"
#include "battle/battle_lib.h"
#include "battle/battle_system.h"

#include "../../../include/endpoint.h"

void BattleController_SendMessage(BattleSystem *battleSys, int recipient,
    int battler, void *message, u8 size);

/* The engine's own message buffer width (struct_defs/battler_data.h), which is
 * the ceiling on anything the scene can be handed. SendMessage carries its
 * size in a u8, so 255 is the real limit; a source line past either is a
 * malformed source rather than a big message. */
#define OPENMMO_BATTLE_MSG_MAX 255

/* A doubles fight has four battlers, which is the engine's own maximum, and the
 * ring between the halves is 4 KB of messages that are tens of bytes each. Both
 * are bounds rather than guesses: crossing either means the fight is a shape
 * this was not measured on, and is said out loud. */
#define OPENMMO_BATTLE_BATTLERS 4
#define OPENMMO_BATTLE_PENDING 32
#define OPENMMO_BATTLE_OFFER 32

static int sInit;
static int sHalt;
static FILE *sTrace;
static FILE *sSource;
static const char *sSourcePath;
static unsigned long sEmitSeq;
static unsigned long sFeedSeq;
static unsigned long sPumpSeq;
static unsigned long sPresentSeq;
static u8 sFeedBuf[OPENMMO_BATTLE_MSG_MAX];

static struct {
    u8 battler;
    u8 size;
    u8 bytes[OPENMMO_BATTLE_MSG_MAX];
} sOffer[OPENMMO_BATTLE_OFFER];
static int sOfferHead;
static int sOfferCount;

/*
 * What was handed to each battler and not yet executed, so that a `present` record can carry
 * the message the scene acted on rather than only its opcode.
 */
static struct {
    u8 size;
    u8 command;
} sPending[OPENMMO_BATTLE_BATTLERS][OPENMMO_BATTLE_PENDING];
static int sPendingHead[OPENMMO_BATTLE_BATTLERS];
static int sPendingCount[OPENMMO_BATTLE_BATTLERS];

/* Loud, and fatal. A scene fed half a stream draws a fight that never happened,
 * and the interesting failures here are all of that shape: a source recorded
 * from a different fight, a source that ran out, a message too wide for the
 * buffer the scene reads out of. None of those has a sane fallback. */
static void openmmo_battle_fail(const char *why, const char *detail)
{
    fprintf(stderr, "openmmo: battle seam, %s", why);
    if (detail != NULL) {
        fprintf(stderr, " (%s)", detail);
    }
    fprintf(stderr, "\n");
    fflush(NULL);
    abort();
}

static void openmmo_battle_init(void)
{
    const char *path;

    sInit = 1;

    path = getenv("OPENMMO_BATTLE_TRACE");
    if (path != NULL && path[0] != '\0') {
        sTrace = fopen(path, "w");
        if (sTrace == NULL) {
            openmmo_battle_fail("cannot write the trace", path);
        }
        fprintf(sTrace, "openmmo-battle 1\n");
    }

    path = openmmo_dev_env("OPENMMO_BATTLE_SOURCE");
    if (path != NULL && path[0] != '\0') {
        char head[32];

        sSourcePath = path;
        sSource = fopen(path, "r");
        if (sSource == NULL) {
            openmmo_battle_fail("cannot read the source", path);
        }
        if (fgets(head, sizeof head, sSource) == NULL
            || strncmp(head, "openmmo-battle 1", 16) != 0) {
            openmmo_battle_fail("the source is not a version 1 trace", path);
        }
        fprintf(stderr, "openmmo: the battle scene is driven from %s\n", path);
    }

    if (!sHalt) {
        path = openmmo_dev_env("OPENMMO_BATTLE_HALT");
        if (path != NULL && path[0] != '\0' && path[0] != '0') {
            sHalt = 1;
        }
    }
    if (sHalt) {
        fprintf(stderr, "openmmo: the battle arithmetic is stopped\n");
    }
}

static void openmmo_battle_record(const char *kind, unsigned long seq,
    int recipient, int battler, const u8 *bytes, int size)
{
    int i;

    if (sTrace == NULL) {
        return;
    }

    fprintf(sTrace, "%s %lu %d %d %d ", kind, seq, recipient, battler, size);
    for (i = 0; i < size; i++) {
        fprintf(sTrace, "%02x", bytes[i]);
    }
    fprintf(sTrace, "\n");
}

/* The next message the source has for the scene, copied into sFeedBuf. */
static int openmmo_battle_next(int *recipient, int *battler)
{
    char line[8 + 2 * OPENMMO_BATTLE_MSG_MAX + 64];

    while (fgets(line, sizeof line, sSource) != NULL) {
        char kind[16];
        int rcpt, btlr, size, i, n;
        const char *hex;

        if (sscanf(line, "%15s %*u %d %d %d %n", kind, &rcpt, &btlr, &size, &n)
            != 4) {
            continue;
        }
        if (strcmp(kind, "emit") != 0 || rcpt != COMM_RECIPIENT_CLIENT) {
            continue;
        }
        if (size <= 0 || size > OPENMMO_BATTLE_MSG_MAX) {
            openmmo_battle_fail("a source record is not a message this scene "
                                "can be handed", line);
        }

        hex = line + n;
        for (i = 0; i < size; i++) {
            unsigned byte;
            if (sscanf(hex + 2 * i, "%2x", &byte) != 1) {
                openmmo_battle_fail("a source record is short of its bytes",
                    line);
            }
            sFeedBuf[i] = (u8)byte;
        }

        *recipient = rcpt;
        *battler = btlr;
        return size;
    }

    return 0;
}

/* Handed to a battler and not yet executed. */
static void openmmo_battle_pend(int battler, const u8 *bytes, int size)
{
    int slot;

    if (battler < 0 || battler >= OPENMMO_BATTLE_BATTLERS) {
        openmmo_battle_fail("a message is addressed to a battler this was "
                            "never measured on", NULL);
    }
    if (sPendingCount[battler] >= OPENMMO_BATTLE_PENDING) {
        openmmo_battle_fail("a battler is holding more messages than the scene "
                            "has ever taken at once", NULL);
    }

    slot = (sPendingHead[battler] + sPendingCount[battler])
        % OPENMMO_BATTLE_PENDING;
    sPending[battler][slot].size = (u8)size;
    sPending[battler][slot].command = bytes[0];
    sPendingCount[battler]++;
}

/* Every message between the battle's two halves, before it is delivered. */
int openmmo_battle_emit(void *battleSys, int recipient, int battler,
    void **message, int size)
{
    const u8 *bytes = *(const u8 **)message;
    int fedSize, fedRecipient, fedBattler;

    (void)battleSys;

    if (!sInit) {
        openmmo_battle_init();
    }
    if (sTrace == NULL && sSource == NULL) {
        return size;
    }

    openmmo_battle_record("emit", sEmitSeq++, recipient, battler, bytes, size);

    /* HALT means this process is the writer, via SendMessage. Substituting
     * here would throw away the message we just decided to deliver. */
    if (sHalt || sSource == NULL || recipient != COMM_RECIPIENT_CLIENT) {
        if (recipient == COMM_RECIPIENT_CLIENT) {
            openmmo_battle_pend(battler, bytes, size);
        }
        return size;
    }

    /* The local math's answer is discarded here: from this point the scene is
     * drawing what the source says, not what this process computed. */
    fedSize = openmmo_battle_next(&fedRecipient, &fedBattler);
    if (fedSize == 0) {
        openmmo_battle_fail("the source ran out with the fight still going",
            sSourcePath);
    }
    if (fedBattler != battler) {
        char detail[96];
        snprintf(detail, sizeof detail,
            "the source's next message is for battler %d, this one is for %d",
            fedBattler, battler);
        openmmo_battle_fail("the source is out of step with the fight", detail);
    }

    openmmo_battle_record("feed", sFeedSeq++, fedRecipient, fedBattler,
        sFeedBuf, fedSize);
    openmmo_battle_pend(fedBattler, sFeedBuf, fedSize);

    *message = sFeedBuf;
    return fedSize;
}

/* Every command the scene executes, as it is dispatched. */
void openmmo_battle_present(int battler, const void *data)
{
    const u8 *bytes = data;
    int slot, size;

    if (!sInit) {
        openmmo_battle_init();
    }
    if (sTrace == NULL && sSource == NULL) {
        return;
    }
    if (battler < 0 || battler >= OPENMMO_BATTLE_BATTLERS) {
        openmmo_battle_fail("the scene executed a command for a battler this "
                            "was never measured on", NULL);
    }

    /* Nothing reaches the scene except through the seam. If this fires, that
     * sentence is false and the measurement above it is worthless, so it is
     * the check worth having, not a defensive one. */
    if (sPendingCount[battler] == 0) {
        openmmo_battle_fail("the scene executed a command nothing delivered",
            NULL);
    }

    slot = sPendingHead[battler];
    sPendingHead[battler] = (slot + 1) % OPENMMO_BATTLE_PENDING;
    sPendingCount[battler]--;

    if (sPending[battler][slot].command != bytes[0]) {
        char detail[96];
        snprintf(detail, sizeof detail,
            "delivered %d, executed %d", sPending[battler][slot].command,
            bytes[0]);
        openmmo_battle_fail("the scene executed a command out of the order it "
                            "was delivered in", detail);
    }

    size = sPending[battler][slot].size;
    openmmo_battle_record("present", sPresentSeq++, COMM_RECIPIENT_CLIENT,
        battler, bytes, size);
}

static int openmmo_battle_take_offer(int *battler)
{
    int slot, i;

    if (sOfferCount <= 0) {
        return 0;
    }

    slot = sOfferHead;
    sOfferHead = (slot + 1) % OPENMMO_BATTLE_OFFER;
    sOfferCount--;

    *battler = sOffer[slot].battler;
    for (i = 0; i < sOffer[slot].size; i++) {
        sFeedBuf[i] = sOffer[slot].bytes[i];
    }
    return sOffer[slot].size;
}

void openmmo_battle_halt(int on)
{
    sHalt = on ? 1 : 0;
}

int openmmo_battle_offer(int battler, const void *bytes, int size)
{
    const u8 *src = bytes;
    int slot, i;

    if (battler < 0 || battler >= OPENMMO_BATTLE_BATTLERS
        || bytes == NULL || size <= 0 || size > OPENMMO_BATTLE_MSG_MAX) {
        return -1;
    }
    if (sOfferCount >= OPENMMO_BATTLE_OFFER) {
        openmmo_battle_fail("more commands were offered than the scene can "
                            "hold at once", NULL);
    }

    slot = (sOfferHead + sOfferCount) % OPENMMO_BATTLE_OFFER;
    sOffer[slot].battler = (u8)battler;
    sOffer[slot].size = (u8)size;
    for (i = 0; i < size; i++) {
        sOffer[slot].bytes[i] = src[i];
    }
    sOfferCount++;
    return 0;
}

/* Whether the local arithmetic should run this frame. */
int openmmo_battle_compute(void *inSys)
{
    BattleSystem *battleSys = inSys;
    BattleContext *ctx;
    int size, recipient, battler;

    if (!sInit) {
        openmmo_battle_init();
    }
    if (!sHalt) {
        return 1;
    }

    ctx = BattleSystem_GetBattleContext(battleSys);
    if (ctx == NULL || !BattleIO_QueueIsEmpty(ctx)) {
        return 0;
    }

    recipient = COMM_RECIPIENT_CLIENT;
    battler = 0;
    size = 0;
    if (sSource != NULL) {
        /*
         * A recording of a local fight includes the menus that asked the player and the ai to
         * pick. Those wait for an answer the arithmetic would have produced.
         */
        while ((size = openmmo_battle_next(&recipient, &battler)) > 0) {
            u8 op = sFeedBuf[0];
            if (op != BATTLE_COMMAND_SET_COMMAND_SELECTION
                && op != BATTLE_COMMAND_SHOW_MOVE_SELECT_MENU
                && op != BATTLE_COMMAND_SHOW_TARGET_SELECT_MENU
                && op != BATTLE_COMMAND_SHOW_BAG_MENU
                && op != BATTLE_COMMAND_SHOW_PARTY_MENU
                && op != BATTLE_COMMAND_SHOW_YES_NO_MENU
                && op != BATTLE_COMMAND_STOP_GAUGE_ANIMATION) {
                break;
            }
        }
    } else {
        size = openmmo_battle_take_offer(&battler);
    }
    if (size <= 0) {
        return 0;
    }

    openmmo_battle_record("pump", sPumpSeq++, COMM_RECIPIENT_CLIENT, battler,
        sFeedBuf, size);
    BattleController_SendMessage(battleSys, COMM_RECIPIENT_CLIENT, battler,
        sFeedBuf, (u8)size);
    return 0;
}
