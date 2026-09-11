/* Starting a Super Contest without walking to Hearthome. */

#include <stdio.h>
#include <string.h>

#include "constants/contests.h"
#include "constants/heap.h"
#include "constants/string.h"

#include "struct_defs/contest_player_mon_dto.h"

#include "field/field_system.h"
#include "chatot_cry.h"
#include "contest.h"
#include "field_system.h"
#include "field_task.h"
#include "heap.h"
#include "map_header.h"
#include "party.h"
#include "pokedex.h"
#include "pokemon.h"
#include "save_player.h"
#include "savedata.h"
#include "string_gf.h"
#include "system_flags.h"
#include "trainer_info.h"
#include "unk_020298BC.h"
#include "vars_flags.h"

#include "pc_modfs.h"

#define CONTEST_HEAP HEAP_ID_FIELD2

/* ---- the cartridge's own sprites, for the length of a contest ------------- */
#define CARTRIDGE_POKEGRA_MEMBERS 2964u
#define CARTRIDGE_HEIGHT_MEMBERS  1976u

static int s_scene_up;

static int contest_claim_mask(const char *path, unsigned index)
{
    if (strcmp(path, "poketool/pokegra/pl_pokegra.narc") == 0)
        return index < CARTRIDGE_POKEGRA_MEMBERS;
    if (strcmp(path, "poketool/pokegra/height.narc") == 0)
        return index < CARTRIDGE_HEIGHT_MEMBERS;
    return 0;
}

/* Called from Contest_Init (1) and Contest_Free (0). */
void openmmo_contest_scene(int up)
{
    s_scene_up = up ? 1 : 0;
    pc_modfs_set_claim_mask(s_scene_up ? contest_claim_mask : NULL);
}

/* openmmo_sprite.c asks, so the live compositor stays out of a sheet the
 * cartridge is drawing. */
int openmmo_contest_scene_up(void)
{
    return s_scene_up;
}

typedef struct {
    Contest *contest;
    String *trainerName;
    u32 mapID;
    int link;
} ContestRun;

enum {
    CONTEST_RUN_START = 0,
    CONTEST_RUN_LINK_SETUP,
    CONTEST_RUN_FINISH,
};

/* The group this client is seated in, and the two ends
 * of a link contest's life. The pipe is already up by the time a run starts,
 * the seat is what starts it, and it comes down in the run's own teardown,
 * where the placements also go up. */
extern void openmmo_contest_link_ended(void *contest);

int openmmo_contest_party_count(FieldSystem *fs)
{
    Party *party;

    if (fs == NULL || fs->saveData == NULL)
        return 0;
    party = SaveData_GetParty(fs->saveData);
    if (party == NULL)
        return 0;
    return Party_GetCurrentCount(party);
}

/*
 * Contest_Init copies this into a String of eight characters and String_Copy asserts rather
 * than truncates, so the name is bounded here rather than taken from TrainerInfo_NameNewString,
 * whose own seven-glyph fallback asserts on a save field with no terminator
 * (openmmo_union.c found that one).
 */
static String *bounded_trainer_name(const TrainerInfo *info)
{
    String *name = String_Init(TRAINER_NAME_LEN + 1, CONTEST_HEAP);

    if (name == NULL)
        return NULL;
    if (info != NULL && !TrainerInfo_HasNoName(info))
        String_CopyNumChars(name, TrainerInfo_Name(info), TRAINER_NAME_LEN);
    return name;
}

static BOOL contest_task(FieldTask *task)
{
    FieldSystem *fs = FieldTask_GetFieldSystem(task);
    ContestRun *run = FieldTask_GetEnv(task);

    switch (task->state) {
    case CONTEST_RUN_START:
        /*
         * A link contest opens with an exchange of its own before any round runs, the seeds,
         * the entered monsters and the trainers, which is the lobby script's
         * ScrCmd_WaitForLinkContestSetup, in the same order: set the mode up, then wait for
         * the comm task it starts.
         */
        if (run->link && Contest_SetUpLinkContest(run->contest)) {
            task->state = CONTEST_RUN_LINK_SETUP;
            break;
        }
        if (run->link) {
            /* CommSys says there is no group. Running it solo would look like
             * a link contest and be three engine rivals, so say so and let the
             * rounds run for what they are. */
            printf("openmmo: link contest, no group was up, so this is a"
                   " solo contest\n");
            run->link = 0;
        }
        /* Pushes the engine's own contest task in front of this one; this
         * state machine resumes on the frame it returns. */
        task->state = CONTEST_RUN_FINISH;
        FieldTask_InitRunContestTask(task, run->contest);
        break;

    case CONTEST_RUN_LINK_SETUP:
        if (!Contest_IsCommTaskDone(run->contest))
            break;
        printf("openmmo: link contest, the group is set up; the rounds"
               " start\n");
        task->state = CONTEST_RUN_FINISH;
        FieldTask_InitRunContestTask(task, run->contest);
        break;

    case CONTEST_RUN_FINISH:
        /* Before EndContest, exactly where the lobby's own teardown command has
         * it: the placements this client computed go up and the pipe comes
         * down, while the Contest is still allocated. A no-op when there was no
         * pipe. */
        openmmo_contest_link_ended(run->contest);
        /* The script's own EndContest, in the same order: record it, then
         * free it (scrcmd_contests.c, ScrCmd_EndContest). */
        Contest_EndContest(run->contest, fs->saveData, run->mapID,
                           fs->journalEntry);
        Contest_Free(run->contest);
        if (run->trainerName != NULL)
            String_Free(run->trainerName);
        printf("openmmo: contest finished\n");
        Heap_Free(run);
        return TRUE;
    }
    return FALSE;
}

/* `caller` is the field task this is being started from, or NULL. A task may
 * not be created while one is running (FieldSystem_CreateTask asserts on it),
 * so a caller that is a task gets the contest pushed in front of it and
 * resumes when the contest is over. */
int openmmo_contest_start(FieldSystem *fs, FieldTask *caller, int rank,
                          int type, int competition, int slot, int link)
{
    PlayerMonContestDTO dto;
    ContestRun *run;
    TrainerInfo *info;
    Party *party;
    Pokemon *mon;

    if (fs == NULL || fs->saveData == NULL || fs->location == NULL)
        return 0;
    if (!FieldSystem_IsRunningFieldMap(fs) || FieldSystem_HasChildProcess(fs))
        return 0;
    if (caller == NULL && fs->task != NULL)
        return 0;

    party = SaveData_GetParty(fs->saveData);
    if (party == NULL || slot < 0 || slot >= Party_GetCurrentCount(party)) {
        printf("openmmo: contest, party slot %d is empty\n", slot + 1);
        return 0;
    }
    mon = Party_GetPokemonBySlotIndex(party, slot);
    if (mon == NULL)
        return 0;

    info = SaveData_GetTrainerInfo(fs->saveData);
    run = Heap_Alloc(CONTEST_HEAP, sizeof(ContestRun));
    if (run == NULL) {
        printf("openmmo: contest would not allocate\n");
        return 0;
    }
    memset(run, 0, sizeof(*run));
    run->link = link;
    run->trainerName = bounded_trainer_name(info);
    if (run->trainerName == NULL) {
        Heap_Free(run);
        return 0;
    }
    run->mapID = MapHeader_GetMapLabelTextID(fs->location->mapHeaderID);

    memset(&dto, 0, sizeof dto);
    dto.contestType = (u8)type;
    dto.contestRank = (u8)rank;
    dto.competitionType = (u8)competition;
    dto.isGameCompleted =
        (u8)SystemFlag_CheckGameCompleted(SaveData_GetVarsFlags(fs->saveData));
    dto.isNatDexObtained =
        (u8)Pokedex_IsNationalDexObtained(SaveData_GetPokedex(fs->saveData));
    dto.monPartySlot = (u8)slot;
    dto.mon = mon;
    dto.trainerName = run->trainerName;
    dto.trainerInfo = info;
    dto.imageClips = SaveData_GetImageClips(fs->saveData);
    dto.options = SaveData_GetOptions(fs->saveData);
    dto.saveData = fs->saveData;
    dto.chatotCry = SaveData_GetChatotCry(fs->saveData);

    run->contest = Contest_Init(&dto);
    if (run->contest == NULL) {
        String_Free(run->trainerName);
        Heap_Free(run);
        printf("openmmo: contest would not initialise\n");
        return 0;
    }

    if (caller != NULL)
        FieldTask_InitCall(caller, contest_task, run);
    else
        FieldSystem_CreateTask(fs, contest_task, run);
    printf("openmmo: %s starting, rank %d, type %d, competition %d,"
           " slot %d\n", link ? "link contest" : "contest", rank, type,
           competition, slot + 1);
    return 1;
}
