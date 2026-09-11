/* The look a player picked, on the field, in a battle and on the card. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "constants/player_avatar.h"
#include "generated/trainer_classes.h"

#include "pc_modfs.h"

#include "../../../include/appearance.h"
#include "../../../include/client.h"

extern const char *openmmo_dev_env(const char *name);

#define LOOK_ROWS   MMO_APPEAR_LOOK_COUNT
#define BALL_FRAMES 6
#define NO_BALL     0x7FFF

struct look_row {
    unsigned char known;
    unsigned char gender;
    int cls;              /* the appended front class */
    int back;             /* the appended back picture index */
    int ballrow;          /* the engine's own row to use, or -1 for `ball` */
    short ball[BALL_FRAMES][2];
    char dir[512];        /* the package directory, for the card face */
};

static struct look_row s_rows[LOOK_ROWS];
static int s_loaded;
static openmmo_client *s_client;

/* Every package the run was handed, in the order PC_MODS names them: the
 * same walk openmmo_trainerclass.c makes for the class tables. */
static void look_load(void)
{
    const char *dir = getenv("PC_MODS_DIR");
    const char *mods = getenv("PC_MODS");
    char list[256];
    char *name, *save = NULL;
    int rows = 0;

    s_loaded = 1;
    if (dir == NULL || mods == NULL)
        return;
    snprintf(list, sizeof list, "%s", mods);
    for (name = strtok_r(list, ",", &save); name != NULL;
         name = strtok_r(NULL, ",", &save)) {
        char path[640];
        FILE *f;
        int look, cls, back, ballrow, b[BALL_FRAMES * 2];

        snprintf(path, sizeof path,
                 "%s/%s/.cooked/generated/player_looks.txt", dir, name);
        f = fopen(path, "r");
        if (f == NULL)
            continue;
        while (fscanf(f, "%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d",
                      &look, &cls, &back, &ballrow,
                      &b[0], &b[1], &b[2], &b[3], &b[4], &b[5],
                      &b[6], &b[7], &b[8], &b[9], &b[10], &b[11]) == 16) {
            const mmo_appearance *a = mmo_appearance_by_look(look);
            int k;

            if (a == NULL || cls < 0 || back < 0)
                continue;
            s_rows[look].known = 1;
            s_rows[look].gender = (unsigned char)a->gender;
            s_rows[look].cls = cls;
            s_rows[look].back = back;
            s_rows[look].ballrow = ballrow;
            for (k = 0; k < BALL_FRAMES; k++) {
                s_rows[look].ball[k][0] = (short)b[k * 2];
                s_rows[look].ball[k][1] = (short)b[k * 2 + 1];
            }
            snprintf(s_rows[look].dir, sizeof s_rows[look].dir,
                     "%s/%s/.cooked/generated", dir, name);
            rows++;
        }
        fclose(f);
    }
    if (rows > 0)
        printf("openmmo: %d player look(s) composed by the packages\n", rows);
}

static const struct look_row *row_of(int look)
{
    if (!s_loaded)
        look_load();
    if (look < 0 || look >= LOOK_ROWS || !s_rows[look].known)
        return NULL;
    return &s_rows[look];
}

void openmmo_look_attach(openmmo_client *c)
{
    s_client = c;
}

/* Whether the package serves a sheet for this graphics id: a look whose
 * package is not loaded is not drawable, whatever the wire says. */
int openmmo_look_drawable(int gfx)
{
    struct pc_modfs_billboard row;

    if (mmo_appearance_look_of_gfx(gfx) < 0)
        return 0;
    return pc_modfs_billboard(gfx, &row) && row.like >= 0;
}

/* The local player's look, or -1 for Platinum's pair, for no session, and
 * for a look no package serves. OPENMMO_LOOK=<n> is the development door
 * that dresses a session-less run (the battle lab, a card opened offline)
 * in look n, so a composed package can be filmed without a server. */
int openmmo_look_local(void)
{
    const char *forced = openmmo_dev_env("OPENMMO_LOOK");
    int gfx, look;

    if (forced != NULL && forced[0] != '\0') {
        look = atoi(forced);
        gfx = mmo_appearance_look_gfx(look, MMO_APPEAR_STATE_WALK);
        return gfx >= 0 && openmmo_look_drawable(gfx) ? look : -1;
    }
    if (s_client == NULL)
        return -1;
    gfx = openmmo_client_body_gfx(s_client);
    look = mmo_appearance_look_of_gfx(gfx);
    if (look < 0 || !openmmo_look_drawable(gfx))
        return -1;
    return look;
}

/*
 * The graphics id a body of this look wears in this player state, or -1 when the answer is the
 * engine's own. A state the look's game never drew, Platinum's contest, Vs.
 */
int openmmo_look_state_gfx(int look, int playerState)
{
    int state, gfx;

    switch (playerState) {
    case PLAYER_AVATAR_WALKING:        state = MMO_APPEAR_STATE_WALK; break;
    case PLAYER_AVATAR_CYCLING:        state = MMO_APPEAR_STATE_BIKE; break;
    case PLAYER_AVATAR_SURFING:        state = MMO_APPEAR_STATE_SURF; break;
    case PLAYER_AVATAR_USE_FIELD_MOVE: state = MMO_APPEAR_STATE_FIELD_MOVE; break;
    case PLAYER_AVATAR_FISHING:        state = MMO_APPEAR_STATE_FISHING; break;
    case PLAYER_AVATAR_SAVE:           state = MMO_APPEAR_STATE_SAVE; break;
    case PLAYER_AVATAR_HEAL:           state = MMO_APPEAR_STATE_HEAL; break;
    case PLAYER_AVATAR_POKETCH:        state = MMO_APPEAR_STATE_POKETCH; break;
    case PLAYER_AVATAR_SPRAYDUCK:      state = MMO_APPEAR_STATE_SPRAYDUCK; break;
    default:                           state = MMO_APPEAR_STATE_WALK; break;
    }
    gfx = mmo_appearance_look_gfx(look, state);
    if (gfx >= 0 && openmmo_look_drawable(gfx))
        return gfx;
    gfx = mmo_appearance_look_gfx(look, MMO_APPEAR_STATE_WALK);
    if (gfx >= 0 && openmmo_look_drawable(gfx))
        return gfx;
    return -1;
}

/* The patched Player_GetSpriteFromStateAndGender. */
int openmmo_look_sprite(int playerState, int gender)
{
    int look = openmmo_look_local();
    const mmo_appearance *a;

    if (look < 0)
        return -1;
    a = mmo_appearance_by_look(look);
    if (a == NULL || a->gender != (gender & 1))
        return -1;
    return openmmo_look_state_gfx(look, playerState);
}

/* The walking sheet a body should be seated on: the look's own when the
 * package serves it, the gender's Platinum trainer otherwise. */
int openmmo_look_body_gfx(int gfx, int gender)
{
    int look = mmo_appearance_look_of_gfx(gfx);

    if (look < 0)
        return gfx;
    if (openmmo_look_drawable(mmo_appearance_look_gfx(look, MMO_APPEAR_STATE_WALK)))
        return mmo_appearance_look_gfx(look, MMO_APPEAR_STATE_WALK);
    return mmo_appearance_gender_gfx(gender);
}

/* The trainer class whose front is this look's, or -1 when the package is
 * not loaded (the caller stands the gender's player in). */
int openmmo_look_front_class(int look)
{
    const struct look_row *r = row_of(look);

    return r != NULL ? r->cls : -1;
}

/* The look whose front this class is, or -1. */
int openmmo_look_of_class(int cls)
{
    int look;

    for (look = 0; look < LOOK_ROWS; look++) {
        const struct look_row *r = row_of(look);

        if (r != NULL && r->cls == cls)
            return look;
    }
    return -1;
}

/*
 * The patched SpriteSystem_TrainerClassBackSpriteIndex: an appended back for an appended
 * class, or -1 for the engine's own answer.
 */
int openmmo_trainer_back_index(int trainerClass)
{
    int look = openmmo_look_of_class(trainerClass);
    const struct look_row *r = look >= 0 ? row_of(look) : NULL;

    if (r != NULL)
        return r->back;
    if (trainerClass == TRAINER_CLASS_PLAYER_MALE || trainerClass == TRAINER_CLASS_PLAYER_FEMALE) {
        look = openmmo_look_local();
        r = look >= 0 ? row_of(look) : NULL;
        if (r != NULL && r->gender == (trainerClass == TRAINER_CLASS_PLAYER_FEMALE))
            return r->back;
    }
    return -1;
}

/* The patched ball-throw table read: the engine's row for its own backs
 * (and for a look whose line names one), the look's own row otherwise. */
const short (*openmmo_trainer_ball_row(int backSpriteIdx,
                                       const short (*rows)[BALL_FRAMES][2],
                                       int count))[2]
{
    int look;

    if (backSpriteIdx >= 0 && backSpriteIdx < count)
        return rows[backSpriteIdx];
    for (look = 0; look < LOOK_ROWS; look++) {
        const struct look_row *r = row_of(look);

        if (r == NULL || r->back != backSpriteIdx)
            continue;
        if (r->ballrow >= 0 && r->ballrow < count)
            return rows[r->ballrow];
        return r->ball;
    }
    /* A back nobody declared: the gender's own row, never past the table. */
    return rows[0];
}

/* The card face of a look: sixteen colours and 110 tiles of 8bpp, the way
 * the package wrote them. 0 when there is no face to read. */
int openmmo_look_card_face(int look, unsigned short *colours,
                           unsigned char *tiles, unsigned tilesLen)
{
    const struct look_row *r = row_of(look);
    char path[700];
    unsigned char pal[32];
    FILE *f;
    size_t got;
    int i;

    if (r == NULL || tiles == NULL || colours == NULL)
        return 0;
    snprintf(path, sizeof path, "%s/look_card_%d.bin", r->dir, look);
    f = fopen(path, "rb");
    if (f == NULL)
        return 0;
    got = fread(pal, 1, sizeof pal, f);
    if (got == sizeof pal)
        got = fread(tiles, 1, tilesLen, f);
    fclose(f);
    if (got != tilesLen) {
        printf("openmmo: %s is not a card face\n", path);
        return 0;
    }
    for (i = 0; i < 16; i++)
        colours[i] = (unsigned short)(pal[i * 2] | (pal[i * 2 + 1] << 8));
    return 1;
}
