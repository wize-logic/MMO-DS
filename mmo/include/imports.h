/*
 * What a content package takes out of a cartridge, and what it costs when nobody
 * has one.
 */
#ifndef OPENMMO_IMPORTS_H
#define OPENMMO_IMPORTS_H

#include <stddef.h>

#define MMO_IMPORT_KINDS 4

typedef struct {
    char code[5];      /* the cartridge every line asks for, "" if the recipe
                        * is empty or names more than one */
    int  lines;        /* recipe lines that name content */
    int  kinds;        /* how many distinct kinds those lines use */
    char kind[MMO_IMPORT_KINDS][16];
    int  per_kind[MMO_IMPORT_KINDS];
    int  filled;       /* a fill log records a cartridge having answered */
    char filled_by[5]; /* which code did, "" when none has */
} MmoImportPackage;

/* Read a package directory's port.recipe and port.log. Returns 0 when there
 * was a recipe to read, -1 when there was not, a directory with no recipe is
 * not an unfilled package, it is not a package. */
int mmo_imports_read(const char *dir, MmoImportPackage *out);

/*
 * Whether a scan of the player's cartridge folder found the one this package wants, and under
 * what filename. `manifest` is a `cartridges.found` written by tools/scan_cartridges.sh.
 */
int mmo_imports_found(const char *manifest, const char *code,
                      char *file, size_t n);

/*
 * The sentence a player is told about this package, written to `buf`. Returns its length, or
 * -1 when the package is filled and costs nothing (so a caller can print only what is
 * missing).
 */
int mmo_imports_shortfall(const MmoImportPackage *pkg, const char *have,
                          char *buf, size_t n);

#endif /* OPENMMO_IMPORTS_H */
