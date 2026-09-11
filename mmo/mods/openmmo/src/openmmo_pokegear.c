/*
 * HeartGold's Pokegear: the device itself, its card row, its clock, its
 * skins and the cursor that walks its buttons.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "openmmo_pokegear.h"

#include "constants/graphics.h"
#include "constants/map_object.h"
#include "field/field_system.h"
#include "field_map_change.h"
#include "field_system.h"
#include "field_task.h"
#include "font.h"
#include "res/sound/pl_sound_data.naix" /* SEQ_*_sseq, the engine's own ids */
#include "graphics.h"
#include "gx_layers.h"
#include "heap.h"
#include "location.h"
#include "map_header.h"
#include "player_avatar.h"
#include "rtc.h"
#include "screen_fade.h"
#include "sound.h"
#include "sound_playback.h"
#include "sprite_util.h"
#include "system.h"
#include "touch_screen.h"
#include "vram_transfer.h"

#include "pc_modfs.h"

#include "../../../include/endpoint.h"
#include "../../../include/platform.h"

/* ------------------------------------------------------------------ */
/* The package                                                         */
/* ------------------------------------------------------------------ */

#define PG_BANKS_MAX 112
#define PG_SEQS_MAX  24
#define PG_PATH_MAX  160

struct pg_bank { int hg; int member; };
struct pg_seq { char name[40]; int id; };
struct pg_box { int matrix, x0, y0, w, h; int set; };
struct pg_map { u8 set, region, wx, wy, radio, calls; s16 enc; };

static struct {
    int loaded;
    int ok;
    char narc[PG_NARC_N][PG_PATH_MAX];
    struct pg_bank banks[PG_BANKS_MAX];
    int nbanks;
    struct pg_seq seqs[PG_SEQS_MAX];
    int nseqs;
    struct pg_box box[2];
    struct pg_map map[POKEGEAR_HG_MAPS];
} s_pkg;

static void pkg_load(void)
{
    const char *dir = getenv("PC_MODS_DIR");
    const char *mods = getenv("PC_MODS");
    char list[256];
    char *name, *save = NULL;

    s_pkg.loaded = 1;
    if (dir == NULL || mods == NULL)
        return;
    snprintf(list, sizeof list, "%s", mods);
    for (name = strtok_r(list, ",", &save); name != NULL;
         name = strtok_r(NULL, ",", &save)) {
        char path[512];
        char line[256];
        FILE *f;

        snprintf(path, sizeof path, "%s/%s/.cooked/generated/pokegear.txt",
                 dir, name);
        f = fopen(path, "r");
        if (f == NULL)
            continue;
        while (fgets(line, sizeof line, f) != NULL) {
            char key[16];
            int a, b, c, d, e, g, h;

            if (sscanf(line, "%15s", key) != 1 || key[0] == '#')
                continue;
            if (strcmp(key, "narc") == 0) {
                char p[PG_PATH_MAX];

                if (sscanf(line, "narc %d %159s", &a, p) == 2
                    && a >= 0 && a < PG_NARC_N)
                    snprintf(s_pkg.narc[a], PG_PATH_MAX, "%s", p);
            } else if (strcmp(key, "bank") == 0) {
                if (sscanf(line, "bank %d %d", &a, &b) == 2
                    && s_pkg.nbanks < PG_BANKS_MAX) {
                    s_pkg.banks[s_pkg.nbanks].hg = a;
                    s_pkg.banks[s_pkg.nbanks].member = b;
                    s_pkg.nbanks++;
                }
            } else if (strcmp(key, "seq") == 0) {
                char n[40];

                if (sscanf(line, "seq %39s %d", n, &a) == 2
                    && s_pkg.nseqs < PG_SEQS_MAX) {
                    snprintf(s_pkg.seqs[s_pkg.nseqs].name, 40, "%s", n);
                    s_pkg.seqs[s_pkg.nseqs].id = a;
                    s_pkg.nseqs++;
                }
            } else if (strcmp(key, "box") == 0) {
                if (sscanf(line, "box %d %d %d %d %d %d", &a, &b, &c, &d, &e, &g) == 6
                    && a >= 0 && a < 2) {
                    s_pkg.box[a].matrix = b;
                    s_pkg.box[a].x0 = c;
                    s_pkg.box[a].y0 = d;
                    s_pkg.box[a].w = e;
                    s_pkg.box[a].h = g;
                    s_pkg.box[a].set = 1;
                }
            } else if (strcmp(key, "map") == 0) {
                if (sscanf(line, "map %d %d %d %d %d %d %d", &a, &b, &c, &d, &e, &g, &h) == 7
                    && a >= POKEGEAR_PORTED_FIRST
                    && a < POKEGEAR_PORTED_FIRST + POKEGEAR_HG_MAPS) {
                    struct pg_map *m = &s_pkg.map[a - POKEGEAR_PORTED_FIRST];

                    m->set = 1;
                    m->region = (u8)b;
                    m->wx = (u8)c;
                    m->wy = (u8)d;
                    m->radio = (u8)e;
                    m->calls = (u8)g;
                    m->enc = (s16)h;
                }
            }
        }
        fclose(f);
    }
    s_pkg.ok = s_pkg.narc[PG_NARC_GEAR][0] != '\0'
               && s_pkg.narc[PG_NARC_MAP][0] != '\0'
               && s_pkg.box[0].set;
    if (s_pkg.ok)
        printf("openmmo: pokegear: %d bank(s), %d track(s), the package's five archives\n",
               s_pkg.nbanks, s_pkg.nseqs);
}

int openmmo_pokegear_available(void)
{
    /* A release is a Sinnoh client (endpoint.h): the device is not in it,
     * whatever package is loaded, and every door into it asks here. */
    if (!openmmo_dev_features()) {
        if (!s_pkg.loaded) {
            s_pkg.loaded = 1;
            printf("openmmo: pokegear: not in this build\n");
        }
        return 0;
    }
    if (!s_pkg.loaded)
        pkg_load();
    return s_pkg.ok;
}

/* NARC_ctor by path: the engine's own opener names its archives by an
 * enum, and these five are not in its table. The package claims the paths
 * whole (pc_modfs, `.cooked/fs`), so FS_OpenFile finds them. */
NARC *openmmo_pokegear_narc(int which, enum HeapID heapID)
{
    NARC *narc;
    u32 btnfStart, chunkSize;

    if (!openmmo_pokegear_available() || which < 0 || which >= PG_NARC_N
        || s_pkg.narc[which][0] == '\0')
        return NULL;
    narc = Heap_Alloc(heapID, sizeof(NARC));
    if (narc == NULL)
        return NULL;
    narc->fatbStart = 0;
    FS_InitFile(&narc->file);
    if (!FS_OpenFile(&narc->file, s_pkg.narc[which])) {
        printf("openmmo: pokegear: cannot open %s\n", s_pkg.narc[which]);
        Heap_Free(narc);
        return NULL;
    }
    FS_SeekFile(&narc->file, 12, FS_SEEK_SET);
    FS_ReadFile(&narc->file, &narc->fatbStart, 2);
    FS_SeekFile(&narc->file, narc->fatbStart + 4, FS_SEEK_SET);
    FS_ReadFile(&narc->file, &chunkSize, 4);
    FS_ReadFile(&narc->file, &narc->numFiles, 2);
    btnfStart = narc->fatbStart + chunkSize;
    FS_SeekFile(&narc->file, btnfStart + 4, FS_SEEK_SET);
    FS_ReadFile(&narc->file, &chunkSize, 4);
    narc->fimgStart = btnfStart + chunkSize;
    return narc;
}

int openmmo_pokegear_bank(int hgBank)
{
    int i;

    if (!openmmo_pokegear_available())
        return -1;
    for (i = 0; i < s_pkg.nbanks; i++)
        if (s_pkg.banks[i].hg == hgBank)
            return s_pkg.banks[i].member;
    return -1;
}

MessageLoader *openmmo_pokegear_msg(int hgBank, enum HeapID heapID)
{
    int member = openmmo_pokegear_bank(hgBank);

    if (member < 0) {
        printf("openmmo: pokegear: the package has no text bank %d\n", hgBank);
        return NULL;
    }
    return MessageLoader_Init(MSG_LOADER_LOAD_ON_DEMAND, NARC_INDEX_MSGDATA__PL_MSG,
                              (u32)member, heapID);
}

int openmmo_pokegear_seq(const char *name)
{
    int i;

    if (!openmmo_pokegear_available())
        return 0;
    for (i = 0; i < s_pkg.nseqs; i++)
        if (strcmp(s_pkg.seqs[i].name, name) == 0)
            return s_pkg.seqs[i].id;
    return 0;
}

int openmmo_pokegear_box(int region, int *x0, int *y0, int *w, int *h)
{
    if (!openmmo_pokegear_available() || region < 0 || region > 1
        || !s_pkg.box[region].set)
        return 0;
    if (x0) *x0 = s_pkg.box[region].x0;
    if (y0) *y0 = s_pkg.box[region].y0;
    if (w) *w = s_pkg.box[region].w;
    if (h) *h = s_pkg.box[region].h;
    return 1;
}

int openmmo_pokegear_map(int header, int *region, int *wx, int *wy,
                         int *radio, int *calls, int *enc)
{
    const struct pg_map *m;

    if (!openmmo_pokegear_available() || header < POKEGEAR_PORTED_FIRST
        || header >= POKEGEAR_PORTED_FIRST + POKEGEAR_HG_MAPS)
        return 0;
    m = &s_pkg.map[header - POKEGEAR_PORTED_FIRST];
    if (!m->set)
        return 0;
    if (region) *region = m->region;
    if (wx) *wx = m->wx;
    if (wy) *wy = m->wy;
    if (radio) *radio = m->radio;
    if (calls) *calls = m->calls;
    if (enc) *enc = m->enc;
    return 1;
}

u32 openmmo_pokegear_label(int header)
{
    return MapHeader_GetMapLabelTextID((enum MapHeaderID)header);
}

/* ------------------------------------------------------------------ */
/* The session's memory of the device (HeartGold's SavePokegear)       */
/* ------------------------------------------------------------------ */

static u8 s_last_app = GEAR_APP_MAP;
static u8 s_skin;
static u8 s_zoomed;
static s16 s_radio_x = 112, s_radio_y = 76;

u8 openmmo_pokegear_last_app(void) { return s_last_app; }
void openmmo_pokegear_set_last_app(u8 app) { s_last_app = app; }
u8 openmmo_pokegear_skin(void) { return s_skin; }
void openmmo_pokegear_set_skin(u8 skin) { s_skin = skin; }
u8 openmmo_pokegear_zoomed(void) { return s_zoomed; }
void openmmo_pokegear_set_zoomed(u8 zoomed) { s_zoomed = zoomed; }
void openmmo_pokegear_radio_cursor(s16 *x, s16 *y) { *x = s_radio_x; *y = s_radio_y; }
void openmmo_pokegear_set_radio_cursor(s16 x, s16 y) { s_radio_x = x; s_radio_y = y; }

/* ------------------------------------------------------------------ */
/* Sound                                                               */
/* ------------------------------------------------------------------ */

/* HeartGold's GEAR* effects are its own archive's; these are this game's
 * nearest, and openthe design notes says so. */
void PokegearApp_PlaySE(int which)
{
    switch (which) {
    case PG_SE_CURSOR:
    case PG_SE_MAPTOUCH:
        Sound_PlayEffect(SE_CONFIRM_sseq_3);
        break;
    case PG_SE_DECIDE:
    case PG_SE_APPCHANGE:
    case PG_SE_YBUTTON:
        Sound_PlayEffect(SEQ_SE_DP_DECIDE_sseq);
        break;
    case PG_SE_CANCEL:
        Sound_PlayEffect(SEQ_SE_DP_BUTTON9_sseq);
        break;
    case PG_SE_RING:
        Sound_PlayEffect(SEQ_SE_PL_CALL_sseq);
        break;
    case PG_SE_HANGUP:
        Sound_PlayEffect(SEQ_SE_DP_BOX03_sseq);
        break;
    case PG_SE_ZOOM_IN:
    case PG_SE_ZOOM_OUT:
        Sound_PlayEffect(SEQ_SE_DP_SELECT78_sseq);
        break;
    }
}

/* The radio's player is this game's BGM player: HeartGold gives the radio a
 * player of its own (PLAYER_RADIO, sound_radio_sys.c) and stops the field's
 * two; the field's music is stopped by the card the same way, so the one
 * player serves. */
static int s_radio_seq;

BOOL SndRadio_StartSeq(int seqNo)
{
    if (seqNo <= 0)
        return FALSE;
    Sound_StopBGM(Sound_GetCurrentBGM(), 0);
    s_radio_seq = seqNo;
    return Sound_PlayBGM((u16)seqNo);
}

void SndRadio_StopSeq(int fadeFrames)
{
    if (s_radio_seq > 0)
        Sound_StopBGM((u16)s_radio_seq, fadeFrames);
    s_radio_seq = 0;
}

int SndRadio_CountPlayingSeq(void)
{
    return s_radio_seq > 0 && Sound_IsSequencePlaying((u16)s_radio_seq);
}

/* ------------------------------------------------------------------ */
/* The cursor manager (pokegear_cursor.c)                              */
/* ------------------------------------------------------------------ */

PokegearCursorManager *PokegearCursorManager_Alloc(int count, enum HeapID heapID)
{
    PokegearCursorManager *m = Heap_Alloc(heapID, sizeof(PokegearCursorManager));

    memset(m, 0, sizeof *m);
    m->count = (u16)count;
    m->activeCursorIndex = 0xFFFF;
    m->cursors = Heap_Alloc(heapID, count * sizeof(PokegearCursor));
    memset(m->cursors, 0, count * sizeof(PokegearCursor));
    return m;
}

void PokegearCursorManager_Free(PokegearCursorManager *m)
{
    int i;

    for (i = 0; i < m->count; i++)
        if (m->cursors[i].active && m->cursors[i].grid != NULL)
            PokegearCursorManager_RemoveCursor(m, (u16)i);
    Heap_Free(m->cursors);
    Heap_Free(m);
}

static u16 cursor_free_slot(PokegearCursorManager *m)
{
    u16 i;

    for (i = 0; i < m->count; i++)
        if (!m->cursors[i].active)
            return i;
    return 0xFFFF;
}

u16 PokegearCursorManager_AddButtons(PokegearCursorManager *m,
                                     const PokegearCursorGrid *spec, u8 n,
                                     u8 cursorPos, enum HeapID heapID,
                                     Sprite *s1, Sprite *s2, Sprite *s3, Sprite *s4)
{
    u16 index = cursor_free_slot(m);
    PokegearCursor *b;

    if (index == 0xFFFF)
        return 0xFFFF;
    b = &m->cursors[index];
    b->active = TRUE;
    b->buttonsAre4Tiles = TRUE;
    b->count = n;
    b->lastIndex = (u8)(n - 1);
    b->grid = Heap_Alloc(heapID, n * sizeof(PokegearCursorGrid));
    memcpy(b->grid, spec, n * sizeof(PokegearCursorGrid));
    b->cursorPos = cursorPos >= n ? 0 : cursorPos;
    b->cursorSprites[0] = s1;
    b->cursorSprites[1] = s2;
    b->cursorSprites[2] = s3;
    b->cursorSprites[3] = s4;
    return index;
}

BOOL PokegearCursorManager_RemoveCursor(PokegearCursorManager *m, u16 index)
{
    if (index >= m->count || !m->cursors[index].active)
        return FALSE;
    if (m->activeCursorIndex == index) {
        m->activeCursorIndex = 0xFFFF;
        m->lastCursor = NULL;
    }
    Heap_Free(m->cursors[index].grid);
    memset(&m->cursors[index], 0, sizeof(PokegearCursor));
    return FALSE;
}

u16 PokegearCursorManager_SetCursorSpritesDrawState(PokegearCursorManager *m, u16 index, BOOL draw)
{
    PokegearCursor *b;
    int i;

    if (index == 0xFFFF) {
        b = m->lastCursor;
        if (b == NULL)
            return 0xFFFF;
    } else if (index >= m->count || !m->cursors[index].active) {
        return 0xFFFF;
    } else {
        b = &m->cursors[index];
    }
    for (i = 0; i < (b->buttonsAre4Tiles ? 4 : 1); i++)
        if (b->cursorSprites[i] != NULL)
            Sprite_SetDrawFlag(b->cursorSprites[i], draw);
    return index;
}

static void cursor_update_position(PokegearCursorManager *m, u16 index)
{
    PokegearCursor *b;
    PokegearCursorGrid *g;

    if (index == 0xFFFF)
        b = m->lastCursor;
    else if (index >= m->count)
        return;
    else
        b = &m->cursors[index];
    if (b == NULL || b->grid == NULL)
        return;
    g = &b->grid[b->cursorPos];
    if (!b->buttonsAre4Tiles) {
        Sprite_SetPositionXY(b->cursorSprites[0], g->x, g->y);
        return;
    }
    Sprite_SetPositionXY(b->cursorSprites[0], (s16)(g->x + g->leftOffset), (s16)(g->y + g->topOffset));
    Sprite_SetPositionXY(b->cursorSprites[1], (s16)(g->x + g->leftOffset), (s16)(g->y + g->bottomOffset));
    Sprite_SetPositionXY(b->cursorSprites[2], (s16)(g->x + g->rightOffset), (s16)(g->y + g->topOffset));
    Sprite_SetPositionXY(b->cursorSprites[3], (s16)(g->x + g->rightOffset), (s16)(g->y + g->bottomOffset));
}

u16 PokegearCursorManager_SetSpecIndexAndCursorPos(PokegearCursorManager *m, u16 index, u8 cursorPos)
{
    if (index >= m->count)
        return 0xFFFF;
    m->lastCursor = &m->cursors[index];
    m->activeCursorIndex = index;
    if (cursorPos != 0xFF)
        m->cursors[index].cursorPos = cursorPos >= m->cursors[index].count ? 0 : cursorPos;
    cursor_update_position(m, 0xFFFF);
    return index;
}

u8 PokegearCursorManager_GetCursorPos(PokegearCursorManager *m)
{
    return m->lastCursor != NULL ? m->lastCursor->cursorPos : 0;
}

static void cursor_move(PokegearCursorManager *m, u8 move)
{
    PokegearCursorGrid *g;
    u8 next;

    if (m->lastCursor == NULL)
        return;
    g = &m->lastCursor->grid[m->lastCursor->cursorPos];
    switch (move) {
    case 1: next = g->buttonRight; break;
    case 2: next = g->buttonUp; break;
    case 3: next = g->buttonDown; break;
    default: next = g->buttonLeft; break;
    }
    if (next <= m->lastCursor->lastIndex)
        m->lastCursor->cursorPos = next;
}

u8 PokegearCursorManager_MoveActiveCursor(PokegearCursorManager *m, u8 move)
{
    cursor_move(m, move);
    cursor_update_position(m, 0xFFFF);
    return m->lastCursor != NULL ? m->lastCursor->cursorPos : 0;
}

u8 PokegearCursorManager_SetActiveCursorPosition(PokegearCursorManager *m, u8 newIndex)
{
    PokegearCursor *b = m->lastCursor;

    if (b == NULL)
        return 0;
    b->cursorPos = b->lastIndex < newIndex ? 0 : newIndex;
    cursor_update_position(m, m->activeCursorIndex);
    return b->cursorPos;
}

void PokegearCursorManager_SetCursorSpritesAnimateFlag(PokegearCursorManager *m, u16 index, BOOL active)
{
    PokegearCursor *b;
    int i;

    if (index == 0xFFFF)
        index = m->activeCursorIndex;
    if (index >= m->count)
        return;
    b = &m->cursors[index];
    if (!b->active)
        return;
    for (i = 0; i < (b->buttonsAre4Tiles ? 4 : 1); i++) {
        if (b->cursorSprites[i] == NULL)
            continue;
        Sprite_RestartAnim(b->cursorSprites[i]);
        Sprite_SetAnimateFlag(b->cursorSprites[i], active);
    }
}

/* ------------------------------------------------------------------ */
/* Sprites off a card's resource set                                   */
/* ------------------------------------------------------------------ */

/* A card's sprites are HeartGold's unmanaged ones; this engine's builder
 * hands back a managed wrapper, kept here so the raw sprite the card holds
 * can be deleted through it. */
#define PG_SPRITES_MAX 0xC0

static ManagedSprite *s_cardSprites[PG_SPRITES_MAX];
static int s_cardSpriteCount;

/* Which archive members a card's sprite set is, as HeartGold's resdat lists
 * them (files/data/resdat 32..47): each row is a (char, cell, anim) triple
 * off the card's archive, loaded under one resource id; the palette member
 * is 0 in all four and goes to both engines. */
struct pg_res_row { s16 chr, cell, anim; u8 both; };
static const struct pg_res_row sMapRes[3] = {
    { 4, 5, 6, 1 }, { 1, 2, 3, 0 }, { 7, 8, 9, 0 }
};
static const struct pg_res_row sOneRes[1] = { { 1, 2, 3, 0 } };

void PokegearApp_CreateSpriteManager(PokegearAppData *app, int card)
{
    SpriteResourceCapacities caps = {
        .asStruct = {
            .charCapacity = 3, .plttCapacity = 2, .cellCapacity = 3,
            .animCapacity = 3, .mcellCapacity = 0, .manimCapacity = 0,
        },
    };
    const struct pg_res_row *rows;
    int n, i, which;
    NARC *narc;

    if (app->spriteSystem == NULL)
        return;
    switch (card) {
    case GEAR_APP_MAP:
    case GEAR_APP_CANCEL:
        rows = sMapRes; n = 3; which = PG_NARC_MAP; break;
    case GEAR_APP_RADIO:
        rows = sOneRes; n = 1; which = PG_NARC_RADIO; break;
    case GEAR_APP_PHONE:
        rows = sOneRes; n = 1; which = PG_NARC_PHONE; break;
    default:
        rows = sOneRes; n = 1; which = PG_NARC_CONF; break;
    }
    app->spriteManager = SpriteManager_New(app->spriteSystem);
    SpriteSystem_InitSprites(app->spriteSystem, app->spriteManager,
                             card == GEAR_APP_MAP || card == GEAR_APP_CANCEL ? 0xC0 : 0x80);
    SpriteSystem_InitManagerWithCapacities(app->spriteSystem, app->spriteManager, &caps);
    narc = openmmo_pokegear_narc(which, app->heapID);
    if (narc == NULL)
        return;
    for (i = 0; i < n; i++) {
        SpriteSystem_LoadCharResObjFromOpenNarc(app->spriteSystem, app->spriteManager, narc,
                                                rows[i].chr, FALSE,
                                                rows[i].both ? NNS_G2D_VRAM_TYPE_2DBOTH : NNS_G2D_VRAM_TYPE_2DMAIN,
                                                i);
        SpriteSystem_LoadCellResObjFromOpenNarc(app->spriteSystem, app->spriteManager, narc,
                                                rows[i].cell, FALSE, i);
        SpriteSystem_LoadAnimResObjFromOpenNarc(app->spriteSystem, app->spriteManager, narc,
                                                rows[i].anim, FALSE, i);
    }
    SpriteSystem_LoadPlttResObjFromOpenNarc(app->spriteSystem, app->spriteManager, narc, 0, FALSE,
                                            11, NNS_G2D_VRAM_TYPE_2DMAIN, 0);
    SpriteSystem_LoadPlttResObjFromOpenNarc(app->spriteSystem, app->spriteManager, narc, 0, FALSE,
                                            11, NNS_G2D_VRAM_TYPE_2DSUB, 1);
    NARC_dtor(narc);
    s_cardSpriteCount = 0;
}

void PokegearApp_DestroySpriteManager(PokegearAppData *app)
{
    int i;

    if (app->spriteManager == NULL)
        return;
    for (i = 0; i < s_cardSpriteCount; i++)
        if (s_cardSprites[i] != NULL)
            Sprite_DeleteAndFreeResources(s_cardSprites[i]);
    s_cardSpriteCount = 0;
    SpriteSystem_FreeResourcesAndManager(app->spriteSystem, app->spriteManager);
    app->spriteManager = NULL;
}

Sprite *PokegearApp_CreateSprite(PokegearAppData *app, const PokegearSpriteTemplate *t)
{
    SpriteTemplate tmpl;
    ManagedSprite *ms;
    int i;

    memset(&tmpl, 0, sizeof tmpl);
    tmpl.x = t->x;
    tmpl.y = t->y;
    tmpl.z = t->z;
    tmpl.animIdx = t->animation;
    tmpl.priority = t->drawPriority;
    tmpl.plttIdx = 0;
    tmpl.vramType = t->vram;
    tmpl.resources[SPRITE_RESOURCE_CHAR] = t->resourceSet;
    tmpl.resources[SPRITE_RESOURCE_PLTT] = t->vram == NNS_G2D_VRAM_TYPE_2DSUB ? 1 : 0;
    tmpl.resources[SPRITE_RESOURCE_CELL] = t->resourceSet;
    tmpl.resources[SPRITE_RESOURCE_ANIM] = t->resourceSet;
    tmpl.resources[SPRITE_RESOURCE_MULTI_CELL] = SPRITE_RESOURCE_NONE;
    tmpl.resources[SPRITE_RESOURCE_MULTI_ANIM] = SPRITE_RESOURCE_NONE;
    tmpl.bgPriority = 1;
    tmpl.vramTransfer = FALSE;
    ms = SpriteSystem_NewSprite(app->spriteSystem, app->spriteManager, &tmpl);
    if (ms == NULL)
        return NULL;
    /* HeartGold's palette numbers are absolute: the card's eleven live from
     * OBJ palette 2 (PaletteData at colour 0x40). A template with pal -1 is
     * HeartGold's paletteMode 1, the sprite keeping its resource's own. */
    if (t->pal >= 0)
        Sprite_SetExplicitPalette(ms->sprite, (u32)t->pal);
    for (i = 0; i < s_cardSpriteCount; i++)
        if (s_cardSprites[i] == NULL)
            break;
    if (i == s_cardSpriteCount) {
        if (s_cardSpriteCount >= PG_SPRITES_MAX) {
            Sprite_DeleteAndFreeResources(ms);
            return NULL;
        }
        s_cardSpriteCount++;
    }
    s_cardSprites[i] = ms;
    return ms->sprite;
}

void PokegearApp_DeleteSprite(PokegearAppData *app, Sprite *sprite)
{
    int i;

    (void)app;
    if (sprite == NULL)
        return;
    for (i = 0; i < s_cardSpriteCount; i++) {
        if (s_cardSprites[i] != NULL && s_cardSprites[i]->sprite == sprite) {
            Sprite_DeleteAndFreeResources(s_cardSprites[i]);
            s_cardSprites[i] = NULL;
            return;
        }
    }
}

/* ------------------------------------------------------------------ */
/* The objects manager                                                 */
/* ------------------------------------------------------------------ */

PokegearObjectsManager *PokegearObjectsManager_Create(int count, enum HeapID heapID)
{
    PokegearObjectsManager *mgr = Heap_Alloc(heapID, sizeof(PokegearObjectsManager));

    memset(mgr, 0, sizeof *mgr);
    mgr->max = (u16)count;
    mgr->objects = Heap_Alloc(heapID, count * sizeof(PokegearManagedObject));
    memset(mgr->objects, 0, count * sizeof(PokegearManagedObject));
    return mgr;
}

void PokegearObjectsManager_Release(PokegearAppData *app, PokegearObjectsManager *mgr)
{
    (void)app;
    Heap_Free(mgr->objects);
    Heap_Free(mgr);
}

void PokegearObjectsManager_UpdateAllSpritesPos(PokegearObjectsManager *mgr)
{
    u16 i;

    for (i = 0; i < mgr->num; i++)
        if (mgr->objects[i].active && !mgr->objects[i].autoUpdateDisabled
            && mgr->objects[i].sprite != NULL)
            Sprite_SetPositionXY(mgr->objects[i].sprite, mgr->objects[i].pos.x, mgr->objects[i].pos.y);
}

u16 PokegearObjectsManager_AppendSprite(PokegearObjectsManager *mgr, Sprite *sprite)
{
    PokegearManagedObject *obj;

    if (mgr->num >= mgr->max)
        return 0xFFFF;
    obj = &mgr->objects[mgr->num];
    memset(obj, 0, sizeof *obj);
    obj->sprite = sprite;
    obj->active = TRUE;
    obj->autoCull = 1;
    return mgr->num++;
}

void PokegearObjectsManager_Reset(PokegearAppData *app, PokegearObjectsManager *mgr)
{
    u16 i;

    for (i = 0; i < mgr->num; i++)
        if (mgr->objects[i].sprite != NULL)
            PokegearApp_DeleteSprite(app, mgr->objects[i].sprite);
    memset(mgr->objects, 0, mgr->num * sizeof(PokegearManagedObject));
    mgr->num = 0;
}

void PokegearObjectsManager_DeleteSpritesFromIndexToEnd(PokegearAppData *app, PokegearObjectsManager *mgr, u8 first)
{
    u16 i, clearCount;

    if (first >= mgr->num)
        return;
    clearCount = mgr->num - first;
    for (i = first; i < mgr->num; i++)
        if (mgr->objects[i].sprite != NULL)
            PokegearApp_DeleteSprite(app, mgr->objects[i].sprite);
    memset(mgr->objects + first, 0, clearCount * sizeof(PokegearManagedObject));
    mgr->num -= clearCount;
}

/* ------------------------------------------------------------------ */
/* The shell's graphics (overlay_100_021E5900.c)                       */
/* ------------------------------------------------------------------ */

/* pgear_gra members: skins are six, one member each per kind. */
#define PGEAR_OBJ_PLTT   0   /* + skin */
#define PGEAR_OBJ_CHAR   6   /* + skin */
#define PGEAR_OBJ_CELL   12
#define PGEAR_OBJ_ANIM   13
#define PGEAR_SUB_PLTT   24  /* + skin */
#define PGEAR_MAIN_PLTT  30  /* + skin */
#define PGEAR_SUB_CHAR   36  /* + skin */
#define PGEAR_SUB_SCRN   42  /* + skin */
#define PGEAR_MAIN_CHAR  48  /* + skin */
#define PGEAR_MAIN_SCRN  54  /* + skin */

static const TouchScreenRect sTouchscreenButtonHitboxes[] = {
    { .rect = { 160, 192, 8, 56 } },
    { .rect = { 160, 192, 56, 104 } },
    { .rect = { 160, 192, 104, 152 } },
    { .rect = { 160, 192, 152, 200 } },
    { .rect = { 160, 192, 206, 254 } },
    { .rect = { TOUCHSCREEN_TABLE_TERMINATOR, 0, 0, 0 } },
};

static const u8 sAppToButtonIndex[][4] = {
    { 0, 0xFF, 0xFF, 1 },
    { 0, 0xFF, 1, 2 },
    { 0, 1, 0xFF, 2 },
    { 0, 1, 2, 3 },
};

/* The UI sheet's palette per sequence (sPlttOverrides). */
static const u8 sPlttOverrides[] = { 2, 2, 2, 3, 1, 1, 1, 1 };

BOOL PokegearApp_HandleInputModeChangeToButtons(PokegearAppData *app)
{
    app->menuInputStateBak = app->menuInputState;
    if (gSystem.pressedKeys & (PAD_BUTTON_A | PAD_BUTTON_B | PAD_KEY_RIGHT | PAD_KEY_LEFT
                               | PAD_KEY_UP | PAD_KEY_DOWN | PAD_BUTTON_X | PAD_BUTTON_Y)) {
        app->menuInputState = MENU_INPUT_STATE_BUTTONS;
        return TRUE;
    }
    return FALSE;
}

static void PokegearApp_DrawAppButtons(PokegearAppData *app)
{
    u8 cards = app->registeredCards;
    const NNSG2dScreenData *s = app->buttonsScrn;

    if (s == NULL)
        return;
    Bg_CopyToTilemapRect(app->bgConfig, BG_LAYER_MAIN_0, 0, 20, 32, 4, s->rawData, 0, 0,
                         (u8)(s->screenWidth / 8), (u8)(s->screenHeight / 8));
    if (!(cards & GEARCARD_MAP))
        Bg_CopyToTilemapRect(app->bgConfig, BG_LAYER_MAIN_0, 13, 20, 6, 4, s->rawData, 0, 8,
                             (u8)(s->screenWidth / 8), (u8)(s->screenHeight / 8));
    if (!(cards & GEARCARD_RADIO))
        Bg_CopyToTilemapRect(app->bgConfig, BG_LAYER_MAIN_0, 7, 20, 6, 4, s->rawData, 0, 8,
                             (u8)(s->screenWidth / 8), (u8)(s->screenHeight / 8));
    Bg_ScheduleTilemapTransfer(app->bgConfig, BG_LAYER_MAIN_0);
}

static BOOL PokegearApp_UpdateAppSwitchButtonBGState(PokegearAppData *app, u8 selection, u8 selected)
{
    const NNSG2dScreenData *s = app->buttonsScrn;
    u8 x;

    PokegearApp_DrawAppButtons(app);
    if (s == NULL)
        return FALSE;
    x = selection == GEAR_APP_CANCEL ? 26 : (u8)(selection * 6 + 1);
    Bg_CopyToTilemapRect(app->bgConfig, BG_LAYER_MAIN_0, x, 20, 6, 4, s->rawData, x,
                         (u8)(selected * 4), (u8)(s->screenWidth / 8), (u8)(s->screenHeight / 8));
    Bg_ScheduleTilemapTransfer(app->bgConfig, BG_LAYER_MAIN_0);
    return FALSE;
}

int PokegearApp_HandleTouchInput_SwitchApps(PokegearAppData *app)
{
    int newApp = TouchScreen_CheckRectanglePressed(sTouchscreenButtonHitboxes);
    u16 val = 0;

    if (newApp == TOUCHSCREEN_INPUT_NONE)
        return GEAR_APP_NO_INPUT;
    if (Bg_DoesPixelAtXYMatchVal(app->bgConfig, BG_LAYER_MAIN_0, gSystem.touchX, gSystem.touchY, &val) == 1)
        return GEAR_APP_NO_INPUT;
    if (newApp == app->app)
        return GEAR_APP_NO_INPUT;
    if ((newApp == GEAR_APP_RADIO && !(app->registeredCards & GEARCARD_RADIO))
        || (newApp == GEAR_APP_MAP && !(app->registeredCards & GEARCARD_MAP)))
        return GEAR_APP_NO_INPUT;
    PokegearApp_UpdateAppSwitchButtonBGState(app, (u8)newApp, 1);
    PokegearApp_PlaySE(newApp != GEAR_APP_CANCEL ? PG_SE_APPCHANGE : PG_SE_CANCEL);
    app->cursorInAppSwitchZone = 0;
    app->menuInputState = MENU_INPUT_STATE_TOUCH;
    return newApp;
}

int PokegearApp_HandleKeyInput_SwitchApps(PokegearAppData *app)
{
    if (gSystem.pressedKeys & PAD_BUTTON_B) {
        PokegearApp_PlaySE(PG_SE_CANCEL);
        return GEAR_APP_CANCEL;
    }
    if (gSystem.pressedKeys & PAD_BUTTON_A) {
        PokegearCursor *c = app->cursorManager->lastCursor;
        PokegearCursorGrid *spec;

        if (c == NULL)
            return GEAR_APP_NO_INPUT;
        spec = &c->grid[c->cursorPos];
        PokegearCursorManager_SetCursorSpritesDrawState(app->cursorManager, 0, FALSE);
        app->cursorInAppSwitchZone = 0;
        PokegearApp_PlaySE(spec->appId != GEAR_APP_CANCEL ? PG_SE_APPCHANGE : PG_SE_CANCEL);
        if (spec->appId == app->app) {
            if (app->reselectAppCB != NULL)
                app->reselectAppCB(app->childAppdata);
            return GEAR_APP_NO_INPUT;
        }
        PokegearApp_UpdateAppSwitchButtonBGState(app, (u8)spec->appId, 1);
        return (int)spec->appId;
    }
    if (gSystem.pressedKeys & PAD_KEY_LEFT) {
        PokegearApp_PlaySE(PG_SE_CURSOR);
        PokegearCursorManager_MoveActiveCursor(app->cursorManager, 0);
        return GEAR_APP_NO_INPUT;
    }
    if (gSystem.pressedKeys & PAD_KEY_RIGHT) {
        PokegearApp_PlaySE(PG_SE_CURSOR);
        PokegearCursorManager_MoveActiveCursor(app->cursorManager, 1);
        return GEAR_APP_NO_INPUT;
    }
    return GEAR_APP_NO_INPUT;
}

BOOL PokegearApp_UpdateClockSprites(PokegearAppData *app, BOOL force)
{
    RTCDate date;
    RTCTime time;
    u8 digit[4];
    int i;

    GetCurrentDateTime(&date, &time);
    if (!force && app->time.second == time.second)
        return FALSE;
    digit[0] = (u8)(time.hour / 10);
    digit[1] = (u8)(time.hour % 10);
    digit[2] = (u8)(time.minute / 10);
    digit[3] = (u8)(time.minute % 10);
    for (i = 0; i < 4; i++)
        if (app->uiSprites[i + 5] != NULL)
            ManagedSprite_SetAnimationFrame(app->uiSprites[i + 5], digit[i]);
    if (app->uiSprites[4] != NULL)
        ManagedSprite_SetAnimationFrame(app->uiSprites[4], (u16)date.week);
    app->time = time;
    app->needClockUpdate = 0;
    return TRUE;
}

int Pokegear_RegionFromCoords(u16 x, u16 y)
{
    if (x > 21) {
        if (x == 25 && y == 8)
            return POKEGEAR_REGION_JOHTO;   /* Mt. Silver */
        if ((x == 28 && y == 6) || (x == 28 && y > 8 && y < 13))
            return POKEGEAR_REGION_INDIGO;  /* the Plateau and Victory Road */
        return POKEGEAR_REGION_KANTO;
    }
    return POKEGEAR_REGION_JOHTO;
}

int Pokegear_GetCurrentRegion(PokegearAppData *app)
{
    return Pokegear_RegionFromCoords(app->args->matrixXCoord, app->args->matrixYCoord);
}

void Pokegear_ClearAppBgLayers(PokegearAppData *app)
{
    static u16 blank[0xE0];
    int i;

    memset(blank, 0, sizeof blank);
    Bg_LoadPalette(BG_LAYER_MAIN_3, blank, 0x1C0, 0);
    Bg_LoadPalette(BG_LAYER_SUB_3, blank, 0x180, 0);
    for (i = 0; i < 3; i++) {
        Bg_ClearTilemap(app->bgConfig, (u8)(i + BG_LAYER_MAIN_1));
        Bg_ClearTilesRange((u8)(i + BG_LAYER_MAIN_1), 0x40, 0, app->heapID);
        Bg_FreeTilemapBuffer(app->bgConfig, (u8)(i + BG_LAYER_MAIN_1));
        Bg_ClearTilemap(app->bgConfig, (u8)(i + BG_LAYER_SUB_1));
        Bg_ClearTilesRange((u8)(i + BG_LAYER_SUB_1), 0x40, 0, app->heapID);
        Bg_FreeTilemapBuffer(app->bgConfig, (u8)(i + BG_LAYER_SUB_1));
    }
}

BOOL Pokegear_RunFadeLayers123(PokegearAppData *app, int direction)
{
    if (app->fadeCounter > 16)
        return TRUE;
    if (direction == 0) {
        PaletteData_Blend(app->plttData, PLTTBUF_MAIN_BG, 0, 0xE0, (u8)(16 - app->fadeCounter), COLOR_BLACK);
        PaletteData_Blend(app->plttData, PLTTBUF_MAIN_OBJ, 0x40, 0xC0, (u8)(16 - app->fadeCounter), COLOR_BLACK);
    } else {
        PaletteData_Blend(app->plttData, PLTTBUF_MAIN_BG, 0, 0xE0, app->fadeCounter, COLOR_BLACK);
        PaletteData_Blend(app->plttData, PLTTBUF_MAIN_OBJ, 0x40, 0xC0, app->fadeCounter, COLOR_BLACK);
    }
    if (app->fadeCounter >= 16) {
        app->fadeCounter += 2;
        return TRUE;
    }
    app->fadeCounter += 2;
    return FALSE;
}

u8 PokegearApp_AppIdToButtonIndex(PokegearAppData *app)
{
    u8 cards = (u8)(app->registeredCards & 3);
    u8 a = app->app;

    if (a > GEAR_APP_PHONE)
        a = GEAR_APP_PHONE;
    return sAppToButtonIndex[cards][a];
}

/* A palette member into a PaletteData buffer, HeartGold's
 * PaletteData_LoadFromOpenNarc: size in bytes, positions in colours. */
static void pltt_load(PaletteData *pd, NARC *narc, int member, enum HeapID heapID,
                      enum PaletteBufferID buf, u32 size, u16 pos, u16 readPos)
{
    NNSG2dPaletteData *pltt;
    void *raw = Graphics_GetPlttDataFromOpenNARC(narc, (u32)member, &pltt, heapID);

    if (raw == NULL)
        return;
    if (size == 0)
        size = pltt->szByte;
    /* HEARTGOLD asks for more bytes than some members hold. */
    {
        static const u16 sBlack[0x100];
        u32 have = (u32)readPos * 2 < pltt->szByte ? pltt->szByte - (u32)readPos * 2 : 0;

        if (size > have) {
            if (have > 0)
                PaletteData_LoadBuffer(pd, (const u16 *)pltt->pRawData + readPos, buf, pos, (u16)have);
            PaletteData_LoadBuffer(pd, sBlack, buf, (u16)(pos + have / 2),
                                   (u16)(size - have > sizeof sBlack ? sizeof sBlack : size - have));
        } else {
            PaletteData_LoadBuffer(pd, (const u16 *)pltt->pRawData + readPos, buf, pos, (u16)size);
        }
    }
    Heap_Free(raw);
}

void PokegearApp_LoadSkinGraphics(PokegearAppData *app, u8 skin)
{
    NARC *narc = openmmo_pokegear_narc(PG_NARC_GEAR, app->heapID);

    if (narc == NULL)
        return;
    Graphics_LoadTilesToBgLayerFromOpenNARC(narc, PGEAR_MAIN_CHAR + skin, app->bgConfig, BG_LAYER_MAIN_0, 0, 0, FALSE, app->heapID);
    Graphics_LoadTilesToBgLayerFromOpenNARC(narc, PGEAR_SUB_CHAR + skin, app->bgConfig, BG_LAYER_SUB_0, 0, 0, FALSE, app->heapID);
    pltt_load(app->plttData, narc, PGEAR_MAIN_PLTT + skin, app->heapID, PLTTBUF_MAIN_BG, 0x40, 0xE0, 0xE0);
    pltt_load(app->plttData, narc, PGEAR_SUB_PLTT + skin, app->heapID, PLTTBUF_SUB_BG, 0x80, 0xC0, 0xC0);
    pltt_load(app->plttData, narc, PGEAR_OBJ_PLTT + skin, app->heapID, PLTTBUF_MAIN_OBJ, 0x80, 0, 0);
    pltt_load(app->plttData, narc, PGEAR_OBJ_PLTT + skin, app->heapID, PLTTBUF_SUB_OBJ, 0x80, 0, 0);
    if (app->buttonsScrnRaw != NULL) {
        NARC_ReadWholeMember(narc, PGEAR_MAIN_SCRN + skin, app->buttonsScrnRaw);
        NNS_G2dGetUnpackedScreenData(app->buttonsScrnRaw, &app->buttonsScrn);
    }
    PokegearApp_UpdateAppSwitchButtonBGState(app, app->app == GEAR_APP_CANCEL ? 2 : app->app, 1);
    Graphics_LoadTilemapToBgLayerFromOpenNARC(narc, PGEAR_SUB_SCRN + skin, app->bgConfig, BG_LAYER_SUB_0, 0, 0, FALSE, app->heapID);
    NARC_dtor(narc);
    Bg_ScheduleTilemapTransfer(app->bgConfig, BG_LAYER_MAIN_0);
    Bg_ScheduleTilemapTransfer(app->bgConfig, BG_LAYER_SUB_0);
}

void PokegearApp_SetGraphicsBanks(void)
{
    GXBanks banks = {
        GX_VRAM_BG_128_A,
        GX_VRAM_BGEXTPLTT_NONE,
        GX_VRAM_SUB_BG_128_C,
        GX_VRAM_SUB_BGEXTPLTT_NONE,
        GX_VRAM_OBJ_64_E,
        GX_VRAM_OBJEXTPLTT_NONE,
        GX_VRAM_SUB_OBJ_16_I,
        GX_VRAM_SUB_OBJEXTPLTT_NONE,
        GX_VRAM_TEX_NONE,
        GX_VRAM_TEXPLTT_NONE,
    };

    GXLayers_SetBanks(&banks);
}

static void PokegearApp_InitBGs(PokegearAppData *app)
{
    GraphicsModes modes = {
        GX_DISPMODE_GRAPHICS,
        GX_BGMODE_0,
        GX_BGMODE_0,
        GX_BG0_AS_2D,
    };
    BgTemplate tmpl[2] = {
        {
            .x = 0, .y = 0, .bufferSize = 0x800, .baseTile = 0,
            .screenSize = BG_SCREEN_SIZE_256x256, .colorMode = GX_BG_COLORMODE_16,
            .screenBase = GX_BG_SCRBASE_0xf800, .charBase = GX_BG_CHARBASE_0x08000,
            .bgExtPltt = GX_BG_EXTPLTT_01, .priority = 0, .areaOver = GX_BG_AREAOVER_XLU,
            .mosaic = FALSE,
        },
        {
            .x = 0, .y = 0, .bufferSize = 0x800, .baseTile = 0,
            .screenSize = BG_SCREEN_SIZE_256x256, .colorMode = GX_BG_COLORMODE_16,
            .screenBase = GX_BG_SCRBASE_0xf800, .charBase = GX_BG_CHARBASE_0x08000,
            .bgExtPltt = GX_BG_EXTPLTT_01, .priority = 3, .areaOver = GX_BG_AREAOVER_XLU,
            .mosaic = FALSE,
        },
    };
    int i;

    PokegearApp_SetGraphicsBanks();
    app->bgConfig = BgConfig_New(app->heapID);
    GX_SetDispSelect(GX_DISP_SELECT_SUB_MAIN);
    SetAllGraphicsModes(&modes);
    GXLayers_DisableEngineALayers();
    GXLayers_DisableEngineBLayers();
    for (i = 0; i < 2; i++) {
        Bg_InitFromTemplate(app->bgConfig, (u8)(i * 4), &tmpl[i], BG_TYPE_STATIC);
        Bg_ClearTilemap(app->bgConfig, (u8)(i * 4));
        Bg_ClearTilesRange((u8)(i * 4), 0x20, 0, app->heapID);
    }
    Bg_ToggleLayer(BG_LAYER_MAIN_0, 0);
    Bg_ToggleLayer(BG_LAYER_SUB_0, 0);
}

static void PokegearApp_FreeBGs(PokegearAppData *app)
{
    Bg_FreeTilemapBuffer(app->bgConfig, BG_LAYER_SUB_0);
    Bg_FreeTilemapBuffer(app->bgConfig, BG_LAYER_MAIN_0);
    Heap_Free(app->bgConfig);
    app->bgConfig = NULL;
    GX_SetDispSelect(GX_DISP_SELECT_SUB_MAIN);
}

static void PokegearApp_InitPaletteData(PokegearAppData *app)
{
    NARC *narc = openmmo_pokegear_narc(PG_NARC_GEAR, app->heapID);

    app->plttData = PaletteData_New(app->heapID);
    PaletteData_AllocBuffer(app->plttData, PLTTBUF_MAIN_BG, 0x200, app->heapID);
    PaletteData_AllocBuffer(app->plttData, PLTTBUF_MAIN_OBJ, 0x200, app->heapID);
    PaletteData_AllocBuffer(app->plttData, PLTTBUF_SUB_BG, 0x200, app->heapID);
    PaletteData_AllocBuffer(app->plttData, PLTTBUF_SUB_OBJ, 0x200, app->heapID);
    if (narc != NULL) {
        app->buttonsScrnRaw = Heap_Alloc(app->heapID, NARC_GetMemberSize(narc, PGEAR_MAIN_SCRN + app->skin));
        NARC_dtor(narc);
    }
    PokegearApp_LoadSkinGraphics(app, app->skin);
}

static void PokegearApp_FreePaletteData(PokegearAppData *app)
{
    if (app->buttonsScrnRaw != NULL) {
        Heap_Free(app->buttonsScrnRaw);
        app->buttonsScrnRaw = NULL;
        app->buttonsScrn = NULL;
    }
    PaletteData_FreeBuffer(app->plttData, PLTTBUF_SUB_OBJ);
    PaletteData_FreeBuffer(app->plttData, PLTTBUF_SUB_BG);
    PaletteData_FreeBuffer(app->plttData, PLTTBUF_MAIN_OBJ);
    PaletteData_FreeBuffer(app->plttData, PLTTBUF_MAIN_BG);
    PaletteData_Free(app->plttData);
    app->plttData = NULL;
}

static void PokegearApp_CreateSpriteSystem(PokegearAppData *app)
{
    RenderOamTemplate oam = {
        .mainOamStart = 0, .mainOamCount = 128,
        .mainAffineOamStart = 0, .mainAffineOamCount = 32,
        .subOamStart = 0, .subOamCount = 128,
        .subAffineOamStart = 0, .subAffineOamCount = 32,
    };
    CharTransferTemplateWithModes transfer = {
        .maxTasks = 64, .sizeMain = 0x10000, .sizeSub = 0x4000,
        .modeMain = GX_OBJVRAMMODE_CHAR_1D_32K, .modeSub = GX_OBJVRAMMODE_CHAR_1D_32K,
    };

    VramTransfer_New(32, app->heapID);
    app->vramLive = 1;
    app->spriteSystem = SpriteSystem_Alloc(app->heapID);
    SpriteSystem_Init(app->spriteSystem, &oam, &transfer, 0x20);
    Utility_Clear2DMainOAM(app->heapID);
    Utility_Clear2DSubOAM(app->heapID);
}

static void PokegearApp_DestroySpriteSystem(PokegearAppData *app)
{
    if (app->spriteSystem != NULL) {
        SpriteSystem_Free(app->spriteSystem);
        app->spriteSystem = NULL;
    }
    if (app->vramLive) {
        VramTransfer_Free();
        app->vramLive = 0;
    }
    Utility_Clear2DMainOAM(app->heapID);
    Utility_Clear2DSubOAM(app->heapID);
}

/*
 * The skin sheet: HeartGold's PokegearUIManager, a sprite list of its own over pgear_gra's
 * char 6+skin, palette 0+skin, cell 12 and anim 13.
 */
#define PG_UI_RES_ID 0xE000

static ManagedSprite *ui_sprite(PokegearAppData *app, u8 x, u8 y, u8 seq, int bottom)
{
    SpriteTemplate tmpl;

    memset(&tmpl, 0, sizeof tmpl);
    tmpl.x = x;
    tmpl.y = y;
    tmpl.z = 0;
    tmpl.animIdx = seq;
    tmpl.priority = 0;
    tmpl.plttIdx = sPlttOverrides[seq];
    tmpl.vramType = bottom ? NNS_G2D_VRAM_TYPE_2DSUB : NNS_G2D_VRAM_TYPE_2DMAIN;
    tmpl.resources[SPRITE_RESOURCE_CHAR] = PG_UI_RES_ID;
    tmpl.resources[SPRITE_RESOURCE_PLTT] = PG_UI_RES_ID;
    tmpl.resources[SPRITE_RESOURCE_CELL] = PG_UI_RES_ID;
    tmpl.resources[SPRITE_RESOURCE_ANIM] = PG_UI_RES_ID;
    tmpl.resources[SPRITE_RESOURCE_MULTI_CELL] = SPRITE_RESOURCE_NONE;
    tmpl.resources[SPRITE_RESOURCE_MULTI_ANIM] = SPRITE_RESOURCE_NONE;
    tmpl.bgPriority = 0;
    tmpl.vramTransfer = FALSE;
    return SpriteSystem_NewSprite(app->spriteSystem, app->uiManager, &tmpl);
}

static void ui_load(PokegearAppData *app, u8 skin)
{
    SpriteResourceCapacities caps = {
        .asStruct = { 1, 1, 1, 1, 0, 0 },
    };
    NARC *narc;
    int i, calls = 1;

    app->uiManager = SpriteManager_New(app->spriteSystem);
    SpriteSystem_InitSprites(app->spriteSystem, app->uiManager, 11);
    SpriteSystem_InitManagerWithCapacities(app->spriteSystem, app->uiManager, &caps);
    narc = openmmo_pokegear_narc(PG_NARC_GEAR, app->heapID);
    if (narc == NULL)
        return;
    SpriteSystem_LoadCharResObjFromOpenNarcWithHardwareMappingType(app->spriteSystem, app->uiManager, narc,
                                                                    PGEAR_OBJ_CHAR + skin, FALSE,
                                                                    NNS_G2D_VRAM_TYPE_2DBOTH, PG_UI_RES_ID);
    SpriteSystem_LoadPlttResObjFromOpenNarc(app->spriteSystem, app->uiManager, narc,
                                            PGEAR_OBJ_PLTT + skin, FALSE, 4,
                                            NNS_G2D_VRAM_TYPE_2DBOTH, PG_UI_RES_ID);
    SpriteSystem_LoadCellResObjFromOpenNarc(app->spriteSystem, app->uiManager, narc, PGEAR_OBJ_CELL, FALSE, PG_UI_RES_ID);
    SpriteSystem_LoadAnimResObjFromOpenNarc(app->spriteSystem, app->uiManager, narc, PGEAR_OBJ_ANIM, FALSE, PG_UI_RES_ID);
    NARC_dtor(narc);

    for (i = 0; i < 4; i++)
        app->uiSprites[i] = ui_sprite(app, 64, 64, (u8)(i + 4), 0);
    app->uiSprites[4] = ui_sprite(app, 173, 48, 2, 1);
    app->uiSprites[5] = ui_sprite(app, 70, 46, 0, 1);
    app->uiSprites[6] = ui_sprite(app, 86, 46, 0, 1);
    app->uiSprites[7] = ui_sprite(app, 110, 46, 0, 1);
    app->uiSprites[8] = ui_sprite(app, 126, 46, 0, 1);
    app->uiSprites[9] = ui_sprite(app, 98, 46, 1, 1);
    app->uiSprites[10] = ui_sprite(app, 197, 48, 3, 1);
    PokegearApp_UpdateClockSprites(app, TRUE);
    if (app->uiSprites[9] != NULL)
        ManagedSprite_SetAnimateFlag(app->uiSprites[9], TRUE);
    openmmo_pokegear_map(app->args->mapID, NULL, NULL, NULL, NULL, &calls, NULL);
    if (!calls && app->uiSprites[10] != NULL)
        ManagedSprite_SetAnimationFrame(app->uiSprites[10], 1);
    for (i = 0; i <= 3; i++) {
        if (app->uiSprites[i] == NULL)
            continue;
        ManagedSprite_SetDrawFlag(app->uiSprites[i], FALSE);
        ManagedSprite_SetAnimateFlag(app->uiSprites[i], TRUE);
    }
    for (i = 4; i < 11; i++)
        if (app->uiSprites[i] != NULL)
            ManagedSprite_SetDrawFlag(app->uiSprites[i], TRUE);
}

static void ui_unload(PokegearAppData *app)
{
    int i;

    if (app->uiManager == NULL)
        return;
    for (i = 0; i < 11; i++) {
        if (app->uiSprites[i] != NULL) {
            ManagedSprite_SetDrawFlag(app->uiSprites[i], FALSE);
            Sprite_DeleteAndFreeResources(app->uiSprites[i]);
            app->uiSprites[i] = NULL;
        }
    }
    SpriteSystem_FreeResourcesAndManager(app->spriteSystem, app->uiManager);
    app->uiManager = NULL;
}

/* A new skin on the sheet: HeartGold swaps the char and palette resources
 * in place; here the eleven sprites are rebuilt over the new members and
 * the cursor re-pointed at them. */
void PokegearUI_ReloadSkin(PokegearAppData *app, u8 skin)
{
    PokegearCursor *c;
    int i;

    ui_unload(app);
    ui_load(app, skin);
    if (app->cursorManager != NULL && app->cursorManager->count > 0) {
        c = &app->cursorManager->cursors[0];
        if (c->active)
            for (i = 0; i < 4; i++)
                c->cursorSprites[i] = app->uiSprites[i] != NULL ? app->uiSprites[i]->sprite : NULL;
    }
}

static void PokegearApp_LoadGraphics(PokegearAppData *app)
{
    PokegearApp_CreateSpriteSystem(app);
    ui_load(app, app->skin);
}

static void PokegearApp_UnloadGraphics(PokegearAppData *app)
{
    ui_unload(app);
    PokegearApp_DestroySpriteSystem(app);
}

static void Pokegear_AddAppSwitchButtons(PokegearAppData *app)
{
    static const PokegearCursorGrid sNoCards[] = {
        { GEAR_APP_CONFIGURE, 2, 1, 0xFF, 0xFF, 32, 176, -16, 16, -10, 10 },
        { GEAR_APP_PHONE, 0, 2, 0xFF, 0xFF, 176, 176, -16, 16, -10, 10 },
        { GEAR_APP_CANCEL, 1, 0, 0xFF, 0xFF, 230, 176, -16, 16, -10, 10 },
    };
    static const PokegearCursorGrid sMapOnly[] = {
        { GEAR_APP_CONFIGURE, 3, 1, 0xFF, 0xFF, 32, 176, -16, 16, -10, 10 },
        { GEAR_APP_MAP, 0, 2, 0xFF, 0xFF, 128, 176, -16, 16, -10, 10 },
        { GEAR_APP_PHONE, 1, 3, 0xFF, 0xFF, 176, 176, -16, 16, -10, 10 },
        { GEAR_APP_CANCEL, 2, 0, 0xFF, 0xFF, 230, 176, -16, 16, -10, 10 },
    };
    static const PokegearCursorGrid sRadioOnly[] = {
        { GEAR_APP_CONFIGURE, 3, 1, 0xFF, 0xFF, 32, 176, -16, 16, -10, 10 },
        { GEAR_APP_RADIO, 0, 2, 0xFF, 0xFF, 80, 176, -16, 16, -10, 10 },
        { GEAR_APP_PHONE, 1, 3, 0xFF, 0xFF, 176, 176, -16, 16, -10, 10 },
        { GEAR_APP_CANCEL, 2, 0, 0xFF, 0xFF, 230, 176, -16, 16, -10, 10 },
    };
    static const PokegearCursorGrid sBothCards[] = {
        { GEAR_APP_CONFIGURE, 4, 1, 0xFF, 0xFF, 32, 176, -16, 16, -10, 10 },
        { GEAR_APP_RADIO, 0, 2, 0xFF, 0xFF, 80, 176, -16, 16, -10, 10 },
        { GEAR_APP_MAP, 1, 3, 0xFF, 0xFF, 128, 176, -16, 16, -10, 10 },
        { GEAR_APP_PHONE, 2, 4, 0xFF, 0xFF, 176, 176, -16, 16, -10, 10 },
        { GEAR_APP_CANCEL, 3, 0, 0xFF, 0xFF, 230, 176, -16, 16, -10, 10 },
    };
    static const PokegearCursorGrid *const specs[] = { sNoCards, sMapOnly, sRadioOnly, sBothCards };
    static const u8 counts[] = { 3, 4, 4, 5 };
    u8 cards = (u8)(app->registeredCards & 3);

    app->cursorManager = PokegearCursorManager_Alloc(4, app->heapID);
    PokegearCursorManager_AddButtons(app->cursorManager, specs[cards], counts[cards], 0, app->heapID,
                                     app->uiSprites[0] ? app->uiSprites[0]->sprite : NULL,
                                     app->uiSprites[1] ? app->uiSprites[1]->sprite : NULL,
                                     app->uiSprites[2] ? app->uiSprites[2]->sprite : NULL,
                                     app->uiSprites[3] ? app->uiSprites[3]->sprite : NULL);
    if (app->app == GEAR_APP_CANCEL)
        PokegearCursorManager_SetSpecIndexAndCursorPos(app->cursorManager, 0, 2);
    else
        PokegearCursorManager_SetSpecIndexAndCursorPos(app->cursorManager, 0, PokegearApp_AppIdToButtonIndex(app));
}

static void Pokegear_RemoveAppSwitchButtons(PokegearAppData *app)
{
    PokegearCursorManager_RemoveCursor(app->cursorManager, 0);
    PokegearCursorManager_Free(app->cursorManager);
    app->cursorManager = NULL;
}

static void PokegearApp_VBlankCB(void *arg)
{
    PokegearAppData *app = arg;

    if (app->vblankCB != NULL)
        app->vblankCB(app, app->childAppdata);
    if (app->plttData != NULL)
        PaletteData_CommitFadedBuffers(app->plttData);
    if (app->spriteSystem != NULL) {
        if (app->uiManager != NULL) {
            PokegearApp_UpdateClockSprites(app, FALSE);
            SpriteSystem_DrawSprites(app->uiManager);
        }
        if (app->spriteManager != NULL)
            SpriteSystem_DrawSprites(app->spriteManager);
        SpriteSystem_TransferOam();
    }
    VramTransfer_Process();
    if (app->bgConfig != NULL)
        Bg_RunScheduledUpdates(app->bgConfig);
    OS_SetIrqCheckFlag(OS_IE_V_BLANK);
}

static BOOL PokegearApp_LoadGFX(PokegearAppData *app)
{
    switch (app->substate) {
    case 0:
        SetVBlankCallback(NULL, NULL);
        DisableHBlank();
        GXLayers_DisableEngineALayers();
        GXLayers_DisableEngineBLayers();
        GX_SetVisiblePlane(0);
        GXS_SetVisiblePlane(0);
        SetScreenColorBrightness(DS_SCREEN_MAIN, COLOR_BLACK);
        SetScreenColorBrightness(DS_SCREEN_SUB, COLOR_BLACK);
        ResetVisibleHardwareWindows(DS_SCREEN_MAIN);
        ResetVisibleHardwareWindows(DS_SCREEN_SUB);
        break;
    case 1:
        PokegearApp_InitBGs(app);
        PokegearApp_InitPaletteData(app);
        break;
    case 2:
        PokegearApp_LoadGraphics(app);
        Pokegear_AddAppSwitchButtons(app);
        break;
    case 3:
        SetVBlankCallback(PokegearApp_VBlankCB, app);
        app->substate = 0;
        return TRUE;
    }
    app->substate++;
    return FALSE;
}

static BOOL PokegearApp_UnloadGFX(PokegearAppData *app)
{
    SetVBlankCallback(NULL, NULL);
    Pokegear_RemoveAppSwitchButtons(app);
    PokegearApp_UnloadGraphics(app);
    PokegearApp_FreePaletteData(app);
    PokegearApp_FreeBGs(app);
    return TRUE;
}

/* ------------------------------------------------------------------ */
/* The shell (pokegear_main.c)                                         */
/* ------------------------------------------------------------------ */

enum {
    PG_MAIN_SETUP = 0,
    PG_MAIN_TEARDOWN,
    PG_MAIN_LAUNCH_CONFIGURE,
    PG_MAIN_RUN_CONFIGURE,
    PG_MAIN_LAUNCH_RADIO,
    PG_MAIN_RUN_RADIO,
    PG_MAIN_LAUNCH_MAP,
    PG_MAIN_RUN_MAP,
    PG_MAIN_LAUNCH_PHONE,
    PG_MAIN_RUN_PHONE,
    PG_MAIN_QUIT
};

static const ApplicationManagerTemplate sTemplate_Map = { PokegearMap_Init, PokegearMap_Main, PokegearMap_Exit, FS_OVERLAY_ID_NONE };
static const ApplicationManagerTemplate sTemplate_Configure = { PokegearConfigure_Init, PokegearConfigure_Main, PokegearConfigure_Exit, FS_OVERLAY_ID_NONE };
static const ApplicationManagerTemplate sTemplate_Radio = { PokegearRadio_Init, PokegearRadio_Main, PokegearRadio_Exit, FS_OVERLAY_ID_NONE };
static const ApplicationManagerTemplate sTemplate_Phone = { PokegearPhone_Init, PokegearPhone_Main, PokegearPhone_Exit, FS_OVERLAY_ID_NONE };

static BOOL PokegearApp_RunSubapp(ApplicationManager **child)
{
    if (*child != NULL && ApplicationManager_Exec(*child)) {
        ApplicationManager_Free(*child);
        *child = NULL;
        return TRUE;
    }
    return FALSE;
}

static int launch(PokegearAppData *app, const ApplicationManagerTemplate *t, int runState)
{
    app->childApplication = ApplicationManager_New(t, app, app->heapID);
    return runState;
}

static int after_card(PokegearAppData *app, int outcome)
{
    switch (outcome) {
    case GEAR_RETURN_MAP:
        app->app = GEAR_APP_MAP;
        Bg_RunScheduledUpdates(app->bgConfig);
        return PG_MAIN_LAUNCH_MAP;
    case GEAR_RETURN_RADIO:
        app->app = GEAR_APP_RADIO;
        Bg_RunScheduledUpdates(app->bgConfig);
        return PG_MAIN_LAUNCH_RADIO;
    case GEAR_RETURN_PHONE:
        app->app = GEAR_APP_PHONE;
        Bg_RunScheduledUpdates(app->bgConfig);
        return PG_MAIN_LAUNCH_PHONE;
    case GEAR_RETURN_CONFIGURE:
        app->app = GEAR_APP_CONFIGURE;
        Bg_RunScheduledUpdates(app->bgConfig);
        return PG_MAIN_LAUNCH_CONFIGURE;
    default:
        return PG_MAIN_TEARDOWN;
    }
}

static BOOL Pokegear_Init(ApplicationManager *man, int *state)
{
    PokegearArgs *args = ApplicationManager_Args(man);
    PokegearAppData *app;

    (void)state;
    Heap_Create(HEAP_ID_APPLICATION, POKEGEAR_HEAP, POKEGEAR_HEAP_SIZE);
    app = ApplicationManager_NewData(man, sizeof(PokegearAppData), POKEGEAR_HEAP);
    memset(app, 0, sizeof *app);
    app->args = args;
    app->heapID = POKEGEAR_HEAP;
    app->saveData = args->saveData;
    /* The cleared story's device: both cards registered. */
    app->registeredCards = GEARCARD_MAP | GEARCARD_RADIO;
    app->menuInputState = args->menuInputState;
    app->skin = openmmo_pokegear_skin();
    app->app = openmmo_pokegear_last_app();
    if (app->app == GEAR_APP_MAP && !(app->registeredCards & GEARCARD_MAP))
        app->app = GEAR_APP_CONFIGURE;
    else if (app->app == GEAR_APP_RADIO && !(app->registeredCards & GEARCARD_RADIO))
        app->app = GEAR_APP_CONFIGURE;
    else if (app->app > GEAR_APP_PHONE)
        app->app = GEAR_APP_MAP;
    app->cursorInAppSwitchZone = 1;
    Sound_SetSceneAndPlayBGM(SOUND_SCENE_SUB_55, SEQ_NONE, 0);
    printf("openmmo: pokegear opens on card %d (skin %d)\n", app->app, app->skin);
    return TRUE;
}

static BOOL Pokegear_Main(ApplicationManager *man, int *state)
{
    PokegearAppData *app = ApplicationManager_Data(man);
    int outcome;

    switch (*state) {
    case PG_MAIN_SETUP:
        if (!PokegearApp_LoadGFX(app))
            break;
        switch (app->app) {
        case GEAR_APP_MAP:
        case GEAR_APP_CANCEL:
            *state = PG_MAIN_LAUNCH_MAP;
            break;
        case GEAR_APP_RADIO:
            *state = PG_MAIN_LAUNCH_RADIO;
            break;
        case GEAR_APP_PHONE:
            *state = PG_MAIN_LAUNCH_PHONE;
            break;
        default:
            *state = PG_MAIN_LAUNCH_CONFIGURE;
            break;
        }
        break;
    case PG_MAIN_TEARDOWN:
        if (PokegearApp_UnloadGFX(app))
            *state = PG_MAIN_QUIT;
        break;
    case PG_MAIN_LAUNCH_CONFIGURE:
        *state = launch(app, &sTemplate_Configure, PG_MAIN_RUN_CONFIGURE);
        break;
    case PG_MAIN_LAUNCH_RADIO:
        *state = launch(app, &sTemplate_Radio, PG_MAIN_RUN_RADIO);
        break;
    case PG_MAIN_LAUNCH_MAP:
        *state = launch(app, &sTemplate_Map, PG_MAIN_RUN_MAP);
        break;
    case PG_MAIN_LAUNCH_PHONE:
        *state = launch(app, &sTemplate_Phone, PG_MAIN_RUN_PHONE);
        break;
    case PG_MAIN_RUN_CONFIGURE:
    case PG_MAIN_RUN_RADIO:
    case PG_MAIN_RUN_MAP:
    case PG_MAIN_RUN_PHONE:
        if (!PokegearApp_RunSubapp(&app->childApplication))
            break;
        outcome = app->appReturnCode;
        app->appReturnCode = 0;
        /* The map's marking mode (GEAR_RETURN_CANCEL out of the map) is not
         * carried: it reopens the map card instead. */
        if (*state == PG_MAIN_RUN_MAP && outcome == GEAR_RETURN_CANCEL)
            outcome = GEAR_RETURN_MAP;
        *state = after_card(app, outcome);
        break;
    case PG_MAIN_QUIT:
        return TRUE;
    }
    return FALSE;
}

static BOOL Pokegear_Exit(ApplicationManager *man, int *state)
{
    PokegearAppData *app = ApplicationManager_Data(man);

    (void)state;
    openmmo_pokegear_set_last_app(app->app == GEAR_APP_CANCEL ? GEAR_APP_MAP : app->app);
    openmmo_pokegear_set_skin(app->skin);
    ApplicationManager_FreeData(man);
    Heap_Destroy(POKEGEAR_HEAP);
    return TRUE;
}

static const ApplicationManagerTemplate sTemplate_Pokegear = { Pokegear_Init, Pokegear_Main, Pokegear_Exit, FS_OVERLAY_ID_NONE };

/* ------------------------------------------------------------------ */
/* The field side: the arguments, the launch, the way back             */
/* ------------------------------------------------------------------ */

/* FieldSystem_InitPokegearArgs. */
static void args_init(FieldSystem *fs, PokegearArgs *args)
{
    struct pc_modfs_map_header hdr;
    int header = fs->location != NULL ? (int)fs->location->mapHeaderID : 0;
    int region = 0, wx = 0, wy = 0;
    int x0 = 0, y0 = 0;

    memset(args, 0, sizeof *args);
    args->saveData = fs->saveData;
    args->fieldSystem = fs;
    args->mapID = (u16)header;
    args->mapHeader = (u16)header;
    args->menuInputState = MENU_INPUT_STATE_BUTTONS;
    args->playerGender = (u8)PlayerAvatar_GetGender(fs->playerAvatar);
    args->mapMusicID = Sound_GetCurrentBGM();
    args->x = PlayerAvatar_GetXPos(fs->playerAvatar);
    args->z = PlayerAvatar_GetZPos(fs->playerAvatar);
    if (openmmo_pokegear_map(header, &region, &wx, &wy, NULL, NULL, NULL)
        && openmmo_pokegear_box(region, &x0, &y0, NULL, NULL)) {
        args->ported = 1;
        args->region = (u8)(region == 0 ? POKEGEAR_REGION_JOHTO : POKEGEAR_REGION_KANTO);
        memset(&hdr, 0, sizeof hdr);
        if (pc_modfs_map_header(header, &hdr) && (int)hdr.matrix == s_pkg.box[region].matrix) {
            args->onMainMatrix = 1;
            args->matrixXCoord = (u8)(args->x / 32 + x0);
            args->matrixYCoord = (u8)(args->z / 32 + y0);
        } else if (wx != 0 || wy != 0) {
            args->matrixXCoord = (u8)wx;
            args->matrixYCoord = (u8)wy;
        } else {
            args->matrixXCoord = region == 0 ? 21 : 32;
            args->matrixYCoord = region == 0 ? 12 : 11;
        }
    } else {
        args->matrixXCoord = 21;
        args->matrixYCoord = 12;
    }
}

PokegearArgs *openmmo_pokegear_launch(FieldSystem *fs)
{
    PokegearArgs *args;

    if (!openmmo_pokegear_available()) {
        printf("openmmo: pokegear: no package carries the device\n");
        return NULL;
    }
    args = Heap_AllocAtEnd(HEAP_ID_FIELD2, sizeof(PokegearArgs));
    args_init(fs, args);
    FieldSystem_StartChildProcess(fs, &sTemplate_Pokegear, args);
    return args;
}

/* The fly, after the device is down: HeartGold's Task_UseFlyInField hands
 * the field a task that plays the Pokemon's cut-in and warps; here the
 * warp alone, which the field's own map change carries out and which
 * openmmo_boot.c reports to the server as a warp this client took. */
typedef struct {
    int header;
    int x;
    int z;
} PokegearFly;

extern void openmmo_boot_expect_local_warp(int header);

static BOOL PokegearFly_Task(FieldTask *task)
{
    PokegearFly *fly = FieldTask_GetEnv(task);

    printf("openmmo: pokegear: flying to header %d (%d,%d)\n", fly->header, fly->x, fly->z);
    openmmo_boot_expect_local_warp(fly->header);
    FieldTask_ChangeMapChangeFly(task, (enum MapHeaderID)fly->header, WARP_ID_NONE,
                                 fly->x, fly->z, DIR_SOUTH);
    Heap_Free(fly);
    return FALSE;
}

/* Whether the device chose a fly; when it did, the task and its data the
 * start menu should jump to. */
int openmmo_pokegear_fly_wanted(const PokegearArgs *args, void **taskFn, void **taskData)
{
    PokegearFly *fly;

    if (args == NULL || !args->setFlyDestination)
        return 0;
    fly = Heap_AllocAtEnd(HEAP_ID_FIELD2, sizeof(PokegearFly));
    fly->header = args->selectedFlyDest;
    fly->x = args->mapCursorX;
    fly->z = args->mapCursorY;
    *taskFn = (void *)PokegearFly_Task;
    *taskData = fly;
    return 1;
}

void openmmo_pokegear_free_args(PokegearArgs *args)
{
    if (args != NULL)
        Heap_FreeExplicit(HEAP_ID_FIELD2, args);
}
