/* Which theme a battle on a ported map opens with. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "constants/battle.h"

/* The ported world starts here; below it every header is this game's own.
 * mmo/tools/portmap.py FIRST_FREE_HEADER is the same number. */
#define BATTLEBGM_FIRST_PORTED 594

/* Headers the porter may hand out. The current run ends at 1133 and the
 * ceiling leaves room without growing into a guess. */
#define BATTLEBGM_MAX_HEADER 2048

struct battlebgm_row {
    unsigned short wild;
    unsigned short trainer;
    unsigned short gym;
};

static struct battlebgm_row *s_rows; /* [BATTLEBGM_MAX_HEADER], calloc'd */
static int s_loaded;

/* Every package the run was handed, tried in the order PC_MODS names them.
 * The table is one package's (the region port's), but nothing here should
 * know which name that package was given. */
static void battlebgm_load(void)
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
        char path[512];
        FILE *f;
        int header, wild, trainer, gym;

        snprintf(path, sizeof path, "%s/%s/.cooked/generated/battle_bgm.txt",
                 dir, name);
        f = fopen(path, "r");
        if (f == NULL)
            continue;
        if (s_rows == NULL) {
            s_rows = calloc(BATTLEBGM_MAX_HEADER, sizeof *s_rows);
            if (s_rows == NULL) {
                fclose(f);
                return;
            }
        }
        while (fscanf(f, "%d %d %d %d", &header, &wild, &trainer, &gym) == 4) {
            if (header < 0 || header >= BATTLEBGM_MAX_HEADER)
                continue;
            s_rows[header].wild = (unsigned short)wild;
            s_rows[header].trainer = (unsigned short)trainer;
            s_rows[header].gym = (unsigned short)gym;
            rows++;
        }
        fclose(f);
    }
    if (rows > 0)
        printf("openmmo: battle themes for %d ported map(s)\n", rows);
}

/* The patched EncEffects_BGM calls this with the engine's own pick; the
 * answer is either the ported region's theme or that pick untouched. */
unsigned int openmmo_battle_bgm_override(int mapHeaderID, unsigned int battleType,
                                         unsigned int engineBGM, int trainerClass)
{
    extern int openmmo_trainer_class_battle(int trclass, unsigned int *seq);
    const struct battlebgm_row *row;
    unsigned int seq;

    if (!s_loaded)
        battlebgm_load();
    if (s_rows == NULL || mapHeaderID < BATTLEBGM_FIRST_PORTED
        || mapHeaderID >= BATTLEBGM_MAX_HEADER)
        return engineBGM;
    row = &s_rows[mapHeaderID];
    seq = (battleType & BATTLE_TYPE_TRAINER) ? row->trainer : row->wild;
    if (battleType & BATTLE_TYPE_TRAINER) {
        unsigned int own = 0;
        int kind = openmmo_trainer_class_battle(trainerClass, &own);

        if (kind == 1 && row->gym != 0)
            seq = row->gym;
        else if (kind == 2 && own != 0)
            seq = own;
    }
    if (seq == 0)
        return engineBGM;
    printf("openmmo: battle theme %u for header %d\n", seq, mapHeaderID);
    return seq;
}
