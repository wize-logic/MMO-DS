/* Carrying a session out to the offline row. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "field/field_system.h"

#include "constants/species.h"

#include "field_overworld_state.h"
#include "field_system.h"
#include "journal.h"
#include "location.h"
#include "party.h"
#include "play_time.h"
#include "player_avatar.h"
#include "pokedex.h"
#include "pokemon.h"
#include "save_player.h"
#include "savedata.h"
#include "system_vars.h"
#include "trainer_info.h"
#include "system_flags.h"
#include "applications/poketch/poketch_system.h"
#include "overlay005/ov5_021EA714.h"

#include "../../../include/client.h"
#include "../../../include/offline_chain.h"
#include "../../../include/platform.h"

/* The port's one supported accessor for the live field system; NULL when no
 * map is loaded. */
extern FieldSystem *pc_lab_field_system(void);

/* pc/src/pc_card_rom.c: put the backup image on disk now rather than after the
 * write burst settles. Not in a header, the port declares it in its own
 * translation unit and the exit path calls it the same way. */
extern void pc_card_backup_sync(void);

/* Below ground the tile is a cavern the server never hears
 * about, generated on this client. A save taken there would resume into it
 * with no server behind it. */
extern int openmmo_underground_active(void);

/* The one absolute writer of the engine's money. A SetMoney
 * outside it is refused, which is what keeps the server the owner of a
 * session's wallet. */
extern void openmmo_seat_trainer_money(TrainerInfo *info, s32 money);

/* The server's one sentence about a blob this client sent,
 * in the player's words. The offline copy is the third blob up that channel
 * and is answered the same way. */
extern void openmmo_import_print_answer(const openmmo_import_answer *a);

/* Where the front door wants the image. Empty for a fused binary started by
 * hand, which has no launcher to adopt one. */
static const char *export_target(void)
{
    const char *at = getenv("OPENMMO_EXPORT");

    return (at != NULL && at[0] != '\0') ? at : NULL;
}

/* "The field is standing still and nothing is on top of it", the port's lab's
 * own three questions. An idle task pointer alone is not enough: an
 * application on top of the field map leaves fs->task NULL the whole time it
 * runs, and HasChildProcess is the one that sees it. */
static int field_settled(FieldSystem *fs)
{
    return fs != NULL
        && fs->task == NULL
        && FieldSystem_IsRunningFieldMap(fs)
        && !FieldSystem_HasChildProcess(fs)
        /* The two the save reads straight out below. A running field map has
         * both, but this is the last thing a session does and a null here
         * would end it with a fault instead of a file. */
        && fs->location != NULL
        && fs->playerAvatar != NULL;
}

/* The JOURNAL, and why an EXPORT without this hangs the GAME it wrote. */
static void stamp_journal(SaveData *save, int mapHeaderID)
{
    JournalEntry *page;
    void *title;

    page = Journal_GetSavedPage(SaveData_GetJournal(save),
                                SystemFlag_HandleJournalAcquired(
                                    SaveData_GetVarsFlags(save),
                                    HANDLE_FLAG_CHECK));
    if (page == NULL)
        return; /* no journal in this story yet; nothing opens on continue */
    title = JournalEntry_CreateTitle((u16)mapHeaderID, HEAP_ID_FIELD2);
    if (title == NULL)
        return;
    JournalEntry_SaveData(page, title, JOURNAL_TITLE); /* frees `title` */
}

/* The other promise a seated story makes and a session never keeps. */
static void drop_pending_special_location(SaveData *save)
{
    VarsFlags *varsFlags = SaveData_GetVarsFlags(save);

    if (SystemFlag_CheckCommunicationClubAccessible(varsFlags))
        SystemFlag_ClearCommunicationClubAccessible(varsFlags);
}

/* The start menu'S party and POKEDEX rows, which a seated story never earned. */
static void unlock_menu_rows(SaveData *save)
{
    Party *party = SaveData_GetParty(save);
    VarsFlags *varsFlags = SaveData_GetVarsFlags(save);
    Pokedex *dex = SaveData_GetPokedex(save);
    Pokemon *lead;

    if (party == NULL || Party_GetCurrentCount(party) <= 0)
        return;
    if (varsFlags != NULL
        && SystemVars_GetPlayerStarter(varsFlags) == SPECIES_NONE) {
        lead = Party_GetPokemonBySlotIndex(party, 0);
        if (lead != NULL) {
            u16 species = (u16)Pokemon_GetValue(lead, MON_DATA_SPECIES, NULL);

            if (species != SPECIES_NONE) {
                SystemVars_SetPlayerStarter(varsFlags, species);
                printf("openmmo: the offline copy names species %d as the"
                       " starter, so the menu shows the party\n", species);
            }
        }
    }
    if (dex != NULL && !Pokedex_IsObtained(dex)) {
        Pokedex_ObtainPokedex(dex);
        printf("openmmo: the offline copy has the Pokedex, so the menu shows"
               " it\n");
    }
}

/* Not zero when the file is there and holds something. The chip is written
 * through a temporary and renamed, so a name that exists holds a whole image;
 * this is the check that the write happened at all. */
static int file_written(const char *path)
{
    long n = -1;
    FILE *f = fopen(path, "rb");

    if (f == NULL)
        return 0;
    if (fseek(f, 0, SEEK_END) == 0)
        n = ftell(f);
    fclose(f);
    return n > 0;
}

/* Write the seated image out and say beside it that the player asked for it. */
static int export_now(const char *character)
{
    const char *at = export_target();
    char mark[512];
    FieldSystem *fs;
    FILE *f;
    int result;

    if (at == NULL) {
        printf("openmmo: this game was not started with anywhere to save an"
               " offline copy\n");
        return -1;
    }
    if (openmmo_underground_active()) {
        printf("openmmo: not saving an offline copy from the Underground --"
               " the cavern is this client's own and has no surface tile\n");
        return -1;
    }
    fs = pc_lab_field_system();
    if (fs == NULL)
        return -1;

    /* The engine's own before-a-save work, in the engine's own order. */
    FieldSystem_SaveObjects(fs);
    FieldSystem_SendPoketchEvent(fs, POKETCH_EVENT_SAVE, 0);
    fs->location->x = PlayerAvatar_GetXPos(fs->playerAvatar);
    fs->location->z = PlayerAvatar_GetZPos(fs->playerAvatar);
    fs->location->warpId = WARP_ID_NONE;
    fs->location->faceDirection = PlayerAvatar_GetFacingDir(fs->playerAvatar);
    stamp_journal(fs->saveData, (int)fs->location->mapHeaderID);
    drop_pending_special_location(fs->saveData);
    unlock_menu_rows(fs->saveData);

    /* The wallet this image keeps is the offline GAME'S, not this session'S. */
    {
        const char *wallet = getenv("OPENMMO_EXPORT_MONEY");
        TrainerInfo *info = SaveData_GetTrainerInfo(fs->saveData);
        u32 live = 0;
        int swapped = 0;

        if (wallet != NULL && wallet[0] != '\0' && info != NULL) {
            live = TrainerInfo_Money(info);
            openmmo_seat_trainer_money(info, (s32)strtol(wallet, NULL, 10));
            swapped = 1;
        }
        result = SaveData_Save(fs->saveData);
        if (swapped)
            openmmo_seat_trainer_money(info, (s32)live);
    }
    if (result != SAVE_RESULT_OK) {
        printf("openmmo: the offline copy could not be written (save result"
               " %d)\n", result);
        return -1;
    }
    /* The port defers the file write until the burst settles, and this run is
     * about to end: ask for it now so the answer below is about a file. */
    pc_card_backup_sync();
    if (!file_written(at)) {
        printf("openmmo: the offline copy did not reach %s\n", at);
        return -1;
    }

    /*
     * The marker is separate from the image, and that is the point of it. A script inside the
     * session that saves writes the same file (the Contest Hall and the Battle Tower both do),
     * so the image alone says nothing about what the player wanted.
     */
    snprintf(mark, sizeof mark, "%s.ok", at);
    f = fopen(mark, "wb");
    if (f == NULL) {
        printf("openmmo: the offline copy could not be marked at %s\n", mark);
        return -1;
    }
    if (character != NULL && character[0] != '\0')
        fprintf(f, "character %s\n", character);
    /* What this session's clock read at the moment the image was written. */
    {
        PlayTime *pt = SaveData_GetPlayTime(fs->saveData);

        if (pt != NULL)
            fprintf(f, "play-time %d\n",
                    (int)PlayTime_GetHours(pt) * 3600
                        + (int)PlayTime_GetMinutes(pt) * 60
                        + (int)PlayTime_GetSeconds(pt));
    }
    if (fclose(f) != 0) {
        printf("openmmo: the offline copy could not be marked at %s\n", mark);
        return -1;
    }
    printf("openmmo: saved for offline play: %s\n", at);
    return 0;
}

/* The press is held, not refused. */
#define EXPORT_HOLD_SECONDS 5

/* How long the session waits for the server to say what it did with the copy
 * before leaving anyway. The copy on disk is the player's whether or not the
 * server kept one, so this is only so the log says which. */
#define EXPORT_ANSWER_SECONDS 3

static int s_want;
static long s_want_at;
static char s_want_who[32];
static int s_sent;
static long s_sent_at;

/* The image the export just wrote, whole. NULL with a line when it will not
 * read; the file is the port's own write and is not touched. */
static u8 *read_image(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    long size;
    u8 *buf;

    *n = 0;
    if (f == NULL)
        return NULL;
    if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) <= 0 ||
        (unsigned long)size > MMO_EXPORT_IMAGE_MAX_BYTES ||
        fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    buf = (u8 *)malloc((size_t)size);
    if (buf == NULL) {
        fclose(f);
        return NULL;
    }
    if (fread(buf, 1, (size_t)size, f) != (size_t)size) {
        fclose(f);
        free(buf);
        return NULL;
    }
    fclose(f);
    *n = (size_t)size;
    return buf;
}

/* The server keeps a copy, and it is this GAME that sends it. */
static int upload_export(openmmo_client *c, const char *at)
{
    mmo_wbuf blob;
    u8 *image;
    size_t n;
    int rc;

    if (c == NULL || openmmo_client_status(c) != OPENMMO_IN_GAME)
        return -1;
    image = read_image(at, &n);
    if (image == NULL) {
        printf("openmmo: the offline copy could not be read back to send;"
               " it is saved here either way\n");
        return -1;
    }
    mmo_wbuf_init(&blob);
    rc = mmo_export_image_encode(&blob, image, n);
    free(image);
    if (rc == 0)
        rc = openmmo_client_send_offline_export(c, blob.data, blob.len);
    mmo_wbuf_free(&blob);
    if (rc == 0)
        printf("openmmo: sent the offline copy (%zu bytes) for the server to"
               " keep\n", n);
    else
        printf("openmmo: the offline copy could not be sent; it is saved here"
               " either way\n");
    return rc;
}

void openmmo_offline_export_request(const char *character)
{
    s_want = 1;
    s_want_at = mmo_plat_seconds();
    s_want_who[0] = '\0';
    if (character != NULL)
        snprintf(s_want_who, sizeof s_want_who, "%s", character);
    printf("openmmo: asked to carry this session on offline\n");
}

/*
 * One frame of the export. Answers 1 the frame the session should end on a written copy: after
 * the server has said what it did with the image, or after a bounded wait for it to say
 * anything.
 */
int openmmo_offline_export_tick(openmmo_client *c)
{
    if (s_sent) {
        const openmmo_import_answer *a = openmmo_client_import_answer(c);

        if (a != NULL && a->status != MMO_IMPORT_STATUS_NONE) {
            openmmo_import_print_answer(a);
            s_sent = 0;
            return 1;
        }
        if (mmo_plat_seconds() - s_sent_at <= EXPORT_ANSWER_SECONDS)
            return 0;
        printf("openmmo: no word from the server about the offline copy; it"
               " is saved here either way\n");
        s_sent = 0;
        return 1;
    }
    if (!s_want)
        return 0;
    if (!field_settled(pc_lab_field_system())) {
        if (mmo_plat_seconds() - s_want_at <= EXPORT_HOLD_SECONDS)
            return 0;
        printf("openmmo: the offline copy waited %d seconds for the field and"
               " was let go of; press it again\n", EXPORT_HOLD_SECONDS);
        s_want = 0;
        return 0;
    }
    s_want = 0;
    if (export_now(s_want_who[0] != '\0' ? s_want_who : NULL) != 0)
        return 0;
    if (upload_export(c, export_target()) != 0)
        return 1;
    s_sent = 1;
    s_sent_at = mmo_plat_seconds();
    return 0;
}
