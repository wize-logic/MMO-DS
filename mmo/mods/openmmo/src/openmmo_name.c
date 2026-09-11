/*
 * A trainer name longer than seven glyphs, on an engine whose name field is
 * eight charcodes wide.
 */

#include <nitro.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "constants/charcode.h"
#include "constants/string.h"

#include "charcode.h"
#include "charcode_util.h"
#include "heap.h"
#include "string_gf.h"
#include "trainer_info.h"

/* The client's own charcode bridge, not the engine header of the same name.
 * It mirrors charcode_t as a uint16_t and says so; this is where the two meet,
 * so this is where that is checked. */
#include "../../../include/endpoint.h"
#include "../../../include/charcode.h"

typedef char openmmo_charcode_width_check[sizeof(mmo_charcode) == sizeof(charcode_t) ? 1 : -1];

/* The server's character name is VARCHAR(32) (server.game V1__create_game_
 * schema.sql), so 32 glyphs is what has to fit. */
#define OPENMMO_NAME_MAX 32

/* How many trainers may carry a wide name at once. The save has one player;
 * the rest are the copies the engine makes of it for a battle or a screen.
 * Running out is reported rather than evicting, because evicting silently would
 * make a name shorten for no reason a log could explain. */
#define NAME_SLOTS 8

static struct {
    const TrainerInfo *owner;
    charcode_t name[OPENMMO_NAME_MAX + 1];
} sNames[NAME_SLOTS];

static unsigned sUsed;
static int sOverflowReported;

static unsigned NameLength(const charcode_t *name)
{
    unsigned n = 0;

    while (name[n] != CHAR_EOS && n < OPENMMO_NAME_MAX) {
        n++;
    }

    return n;
}

/* The stored entry for `info`, or NULL. Stale entries, an address reused by a
 * different trainer, are rejected by comparing against the engine's own
 * field, which every path keeps in step. */
static const charcode_t *Stored(const TrainerInfo *info)
{
    unsigned i, k;

    for (i = 0; i < sUsed; i++) {
        if (sNames[i].owner != info) {
            continue;
        }

        const charcode_t *full = sNames[i].name;
        const charcode_t *seated = TrainerInfo_Name(info);

        for (k = 0; k < TRAINER_NAME_LEN; k++) {
            if (full[k] != seated[k]) {
                return NULL;
            }
            if (full[k] == CHAR_EOS) {
                break;
            }
        }

        return full;
    }

    return NULL;
}

static void Store(const TrainerInfo *info, const charcode_t *name, unsigned len)
{
    unsigned i;

    for (i = 0; i < sUsed; i++) {
        if (sNames[i].owner == info) {
            break;
        }
    }

    if (i == sUsed) {
        if (sUsed == NAME_SLOTS) {
            if (!sOverflowReported) {
                sOverflowReported = 1;
                printf("openmmo: %d trainers already hold a wide name; "
                       "further names are the engine's seven glyphs\n",
                    NAME_SLOTS);
            }
            return;
        }
        sUsed++;
    }

    sNames[i].owner = info;
    memcpy(sNames[i].name, name, len * sizeof(charcode_t));
    sNames[i].name[len] = CHAR_EOS;
}

static void Forget(const TrainerInfo *info)
{
    unsigned i;

    for (i = 0; i < sUsed; i++) {
        if (sNames[i].owner == info) {
            sNames[i] = sNames[--sUsed];
            return;
        }
    }
}

/* OPENMMO_PLAYER_NAME, as engine charcodes, or NULL if it is not set. Read once:
 * a name that changes under the game mid-run is a bug, not a feature. */
static const charcode_t *ConfiguredName(void)
{
    static charcode_t name[OPENMMO_NAME_MAX + 1];
    static int state; /* 0 unread, 1 present, 2 absent */

    if (state == 0) {
        const char *env = openmmo_dev_env("OPENMMO_PLAYER_NAME");

        if (env == NULL || env[0] == '\0') {
            state = 2;
        } else {
            mmo_charcode_result r = mmo_utf8_to_charcode(env, name, OPENMMO_NAME_MAX + 1);

            state = 1;
            printf("openmmo: player name %s, %u glyphs%s%s\n", env,
                (unsigned)r.written,
                r.unmapped != 0 ? ", some with no glyph in this font" : "",
                r.truncated ? ", truncated to fit 32" : "");
        }
    }

    return state == 1 ? name : NULL;
}

/* OPENMMO_NAME_REPORT=1: what a name that has just been seated reads back as,
 * asked through the engine's own two accessors rather than through this file's
 * table, because the claim is about the engine and not about the table. The
 * String is freed again immediately, this is an observation, not a use. */
static void Report(const TrainerInfo *info, unsigned len)
{
    const char *env = getenv("OPENMMO_NAME_REPORT");
    const charcode_t *field;
    String *wide;
    unsigned seated;

    if (env == NULL || env[0] == '\0' || env[0] == '0') {
        return;
    }

    field = TrainerInfo_Name(info);
    for (seated = 0; seated < TRAINER_NAME_LEN + 1 && field[seated] != CHAR_EOS; seated++) {
        ;
    }

    wide = TrainerInfo_NameNewString(info, HEAP_ID_SYSTEM);

    printf("openmmo: trainer name, source %u, field %u, engine string %u\n",
        len, seated, (unsigned)String_Length(wide));

    String_Free(wide);
}

/* Called from TrainerInfo_SetName in place of its unbounded CharCode_Copy: the
 * whole name goes beside the field, the first seven glyphs into it. */
void openmmo_trainer_name_set(TrainerInfo *info, const charcode_t *name)
{
    charcode_t *field = (charcode_t *)TrainerInfo_Name(info);
    const charcode_t *configured = ConfiguredName();
    unsigned len, k;

    if (configured != NULL) {
        name = configured;
    }

    len = NameLength(name);

    if (len > TRAINER_NAME_LEN) {
        Store(info, name, len);
    } else {
        Forget(info);
    }

    for (k = 0; k < TRAINER_NAME_LEN && k < len; k++) {
        field[k] = name[k];
    }
    field[k] = CHAR_EOS;

    Report(info, len);
}

/*
 * The record's ot field is seven glyphs plus the terminator; the wide name that is right on
 * every screen is too wide for it, and an unterminated ot asserts in the first String_Concat
 * that prints one (the summary screen).
 */
void openmmo_trainer_name_clamp_record(String *name)
{
    charcode_t buf[TRAINER_NAME_LEN + 1];
    u32 len;

    if (name == NULL) {
        return;
    }

    len = String_Length(name);

    if (len <= TRAINER_NAME_LEN) {
        return;
    }

    memcpy(buf, String_GetData(name), TRAINER_NAME_LEN * sizeof(charcode_t));
    buf[TRAINER_NAME_LEN] = CHAR_EOS;
    String_Clear(name);
    String_CopyChars(name, buf);
}

/* Called from TrainerInfo_SetNameFromString, whose String_ToChars refuses,
 * loudly, and leaving the old name in place, for anything over seven glyphs. */
void openmmo_trainer_name_set_from_string(TrainerInfo *info, const String *name)
{
    charcode_t buf[OPENMMO_NAME_MAX + 1];
    u32 len = String_Length(name);

    if (len > OPENMMO_NAME_MAX) {
        len = OPENMMO_NAME_MAX;
    }

    memcpy(buf, String_GetData(name), len * sizeof(charcode_t));
    buf[len] = CHAR_EOS;

    openmmo_trainer_name_set(info, buf);
}

/* Called from TrainerInfo_Copy. The struct is memcpy'd, so the wide name has to
 * be carried across by hand or the copy silently narrows to seven. */
void openmmo_trainer_name_copied(const TrainerInfo *src, TrainerInfo *dst)
{
    const charcode_t *full = Stored(src);

    if (full != NULL) {
        Store(dst, full, NameLength(full));
    } else {
        Forget(dst);
    }
}

/* Called from TrainerInfo_NameNewString. NULL means "no wide name for this
 * trainer" and the engine's own seven-glyph path runs unchanged. */
String *openmmo_trainer_name_new_string(const TrainerInfo *info, u32 heapID)
{
    const charcode_t *full = Stored(info);
    String *out;

    if (full == NULL) {
        return NULL;
    }

    out = String_Init(NameLength(full) + 1, heapID);
    String_CopyChars(out, full);

    return out;
}
