/* A trainer class the package appended. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* One byte of trdata is the class, so this is every class there can be. */
#define TRAINERCLASS_MAX 256

struct trainerclass_row {
    unsigned char known;
    unsigned char gender;    /* 0 male, 1 female */
    unsigned char kind;      /* 0 trainer theme, 1 gym theme, 2 its own */
    unsigned short prize;    /* per level of the last monster */
    unsigned short seq;      /* the sequence, for kind 2 */
};

static struct trainerclass_row s_rows[TRAINERCLASS_MAX];
static int s_loaded;

/* Every package the run was handed, in the order PC_MODS names them. */
static void trainerclass_load(void)
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
        int id, gender, prize, kind, seq;

        snprintf(path, sizeof path,
                 "%s/%s/.cooked/generated/trainer_classes.txt", dir, name);
        f = fopen(path, "r");
        if (f == NULL)
            continue;
        while (fscanf(f, "%d %d %d %d %d", &id, &gender, &prize, &kind, &seq) == 5) {
            if (id < 0 || id >= TRAINERCLASS_MAX)
                continue;
            s_rows[id].known = 1;
            s_rows[id].gender = (unsigned char)(gender != 0);
            s_rows[id].kind = (unsigned char)kind;
            s_rows[id].prize = (unsigned short)prize;
            s_rows[id].seq = (unsigned short)seq;
            rows++;
        }
        fclose(f);
    }
    if (rows > 0)
        printf("openmmo: %d trainer class(es) appended by the packages\n", rows);
}

static const struct trainerclass_row *row_of(int trclass)
{
    if (!s_loaded)
        trainerclass_load();
    if (trclass < 0 || trclass >= TRAINERCLASS_MAX || !s_rows[trclass].known)
        return NULL;
    return &s_rows[trclass];
}

/* The patched TrainerClass_Gender, for a class past its table. */
int openmmo_trainer_class_gender(int trclass, int engineGender)
{
    const struct trainerclass_row *r = row_of(trclass);

    return r != NULL ? (r->gender ? 1 : 0) : engineGender;
}

/* The patched prize calculation: the engine's own table where the class is
 * in it, the package's row past it, nothing for a class nobody declared. */
unsigned int openmmo_trainer_class_prize(int trclass, const unsigned char *table, int count)
{
    const struct trainerclass_row *r;

    if (trclass >= 0 && trclass < count)
        return table[trclass];
    r = row_of(trclass);
    return r != NULL ? r->prize : 0;
}

/* Which theme an appended class fights to: 0 for the header's trainer theme,
 * 1 for the header's gym theme, 2 for the sequence written to *seq. */
int openmmo_trainer_class_battle(int trclass, unsigned int *seq)
{
    const struct trainerclass_row *r = row_of(trclass);

    if (r == NULL)
        return 0;
    if (r->kind == 2 && seq != NULL)
        *seq = r->seq;
    return r->kind;
}
