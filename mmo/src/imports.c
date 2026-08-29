/* Reading a package's own two files, and saying what it costs. */
#include <stdio.h>
#include <string.h>

#include "cartridge.h"
#include "imports.h"

/* What a kind is called in a sentence. */
static const struct {
    const char *kind;
    const char *one;
    const char *many;
} KIND_NOUNS[] = {
    { "pokemon",   "species",   "species" },
    { "item_icon", "item icon", "item icons" },
    { "trainer",   "trainer",   "trainers" },
};

static const char *kind_noun(const char *kind, int count)
{
    size_t i;

    for (i = 0; i < sizeof KIND_NOUNS / sizeof KIND_NOUNS[0]; i++) {
        if (strcmp(KIND_NOUNS[i].kind, kind) == 0)
            return count == 1 ? KIND_NOUNS[i].one : KIND_NOUNS[i].many;
    }
    return kind;
}

static int kind_slot(MmoImportPackage *p, const char *kind)
{
    int i;

    for (i = 0; i < p->kinds; i++) {
        if (strcmp(p->kind[i], kind) == 0)
            return i;
    }
    if (p->kinds >= MMO_IMPORT_KINDS)
        return -1;
    i = p->kinds++;
    snprintf(p->kind[i], sizeof p->kind[i], "%s", kind);
    p->per_kind[i] = 0;
    return i;
}

static int open_under(const char *dir, const char *name, FILE **out)
{
    char path[512];

    if (snprintf(path, sizeof path, "%s/%s", dir, name) >= (int)sizeof path)
        return -1;
    *out = fopen(path, "r");
    return *out ? 0 : -1;
}

int mmo_imports_read(const char *dir, MmoImportPackage *out)
{
    char line[512], code[64], kind[64], src[128], dst[128];
    FILE *f;

    if (!dir || !out)
        return -1;
    memset(out, 0, sizeof *out);

    if (open_under(dir, "port.recipe", &f) != 0)
        return -1;
    while (fgets(line, sizeof line, f)) {
        int slot;
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r')
            continue;
        if (sscanf(line, "%63s %63s %127s %127s", code, kind, src, dst) != 4)
            continue;
        if (strlen(code) != 4)
            continue;
        slot = kind_slot(out, kind);
        if (slot >= 0)
            out->per_kind[slot]++;
        out->lines++;
        if (out->code[0] == '\0')
            snprintf(out->code, sizeof out->code, "%s", code);
        else if (strncmp(out->code, code, 4) != 0)
            /* A recipe naming two cartridges has no single answer to "what
             * does this need"; say so by having no code rather than by
             * picking the first line's. */
            out->code[0] = '\0';
    }
    fclose(f);

    if (open_under(dir, "port.log", &f) == 0) {
        while (fgets(line, sizeof line, f)) {
            if (sscanf(line, "fill %4s", code) == 1 && strlen(code) == 4) {
                out->filled = 1;
                snprintf(out->filled_by, sizeof out->filled_by, "%s", code);
            }
        }
        fclose(f);
    }
    return 0;
}

int mmo_imports_found(const char *manifest, const char *code,
                      char *file, size_t n)
{
    char line[512], status[32], got[32], name[256];
    FILE *f;
    int hit = 0;

    if (file && n)
        file[0] = '\0';
    if (!manifest || !code || code[0] == '\0')
        return 0;
    f = fopen(manifest, "r");
    if (!f)
        return 0;
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#')
            continue;
        if (sscanf(line, "%31s %31s %255s", status, got, name) != 3)
            continue;
        if (strcmp(status, "read") != 0 || strncmp(got, code, 4) != 0)
            continue;
        if (file && n)
            snprintf(file, n, "%s", name);
        hit = 1;
        break;
    }
    fclose(f);
    return hit;
}

int mmo_imports_shortfall(const MmoImportPackage *pkg, const char *have,
                          char *buf, size_t n)
{
    const MmoCartridge *cart;
    const char *name;
    char what[160];
    size_t used = 0;
    int i;

    if (!pkg || !buf || n == 0)
        return -1;
    buf[0] = '\0';
    if (pkg->lines == 0)
        return snprintf(buf, n, "This package asks for nothing.");
    if (pkg->filled)
        return -1;

    /* What the lines were going to bring, in their own words and counted, so
     * the sentence is about content rather than about a file format. */
    what[0] = '\0';
    for (i = 0; i < pkg->kinds && used + 1 < sizeof what; i++) {
        int wrote = snprintf(what + used, sizeof what - used, "%s%d %s",
                             i ? (i + 1 == pkg->kinds ? " and " : ", ") : "",
                             pkg->per_kind[i],
                             kind_noun(pkg->kind[i], pkg->per_kind[i]));
        if (wrote < 0)
            break;
        used += (size_t)wrote;
    }

    cart = pkg->code[0] ? mmo_cartridge_slot_of(pkg->code) : NULL;
    name = cart ? cart->name : NULL;
    if (!name)
        return snprintf(buf, n,
                        "%s here come from a cartridge nobody has added, and "
                        "the game draws its own until one is.", what);
    if (have && have[0])
        return snprintf(buf, n,
                        "%s here come from %s, and yours is in the cartridge "
                        "folder as %s. Import it and they arrive; until then "
                        "the game draws its own.",
                        what, name, have);
    return snprintf(buf, n,
                    "%s here come from %s, and no %s cartridge has filled "
                    "this. The game draws its own until one does.",
                    what, name, name);
}
