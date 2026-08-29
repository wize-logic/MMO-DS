/* Every cartridge this client can be handed, and what each serves. */
#ifndef OPENMMO_CARTRIDGE_H
#define OPENMMO_CARTRIDGE_H

#include <stddef.h>

/* What handing this image to the porter as a source does. The registry defines
 * each of these in prose; the short version is that only READ can fill. */
typedef enum {
    /* The player's own image: the game runs on it, nothing is taken from it. */
    MMO_CART_HOST = 0,
    /* Measured here. Serves the kinds on its row. */
    MMO_CART_READ,
    /* The porter copies members out and the engine's loader does not draw
     * them, so a fill is refused on the porter's own warning. */
    MMO_CART_EXTRACTS,
    /* Turned away before its archive is read: the layout is not one this
     * engine's loaders read at all. */
    MMO_CART_REFUSES,
    /* A slot we know, and no image of this build has been read here. */
    MMO_CART_UNREAD
} MmoCartStatus;

typedef struct {
    const char   *code;   /* the four characters in the cartridge header */
    const char   *slot;   /* which cartridge this is, as an identifier */
    const char   *name;   /* and what a player calls it, which is what a
                           * refusal says: "Heart Gold", not "heartgold" */
    MmoCartStatus status;
    const char   *kinds;  /* port.recipe kinds it serves, comma separated */
    const char   *title;  /* the title its own header carries, "" if unread */
    const char   *note;   /* how the row was arrived at */
    /*
     * Which direction this game scrambles sprite character data, in the engine's own build
     * tool's numbering (1 back-to-front, 2 front-to-back, 0 not scrambled or not measured).
     */
    int           scramble;
} MmoCartridge;

typedef struct {
    char        region;  /* the code's fourth character */
    const char *name;    /* the language that character means */
    int         read;    /* an image carrying it has been opened here */
} MmoCartridgeLang;

/* The registry, in file order. */
int                 mmo_cartridge_count(void);
const MmoCartridge *mmo_cartridge_at(int index);

/* The row for a four-character code, or NULL when no row claims it. `code`
 * need not be NUL-terminated past its fourth character. */
const MmoCartridge *mmo_cartridge_by_code(const char *code);

/* The slot a code belongs to even when that exact build is unlisted: matches on
 * the first three characters, which are the game. NULL when nothing matches, so
 * "a cartridge we have not read" and "not one of ours" stay distinguishable. */
const MmoCartridge *mmo_cartridge_slot_of(const char *code);

/* The language the fourth character names, or NULL for a character the registry
 * does not define. */
const char *mmo_cartridge_language(char region);

/* Whether a fill may take content out of this image, and the kinds it serves. */
int mmo_cartridge_serves(const MmoCartridge *cart, const char *kind);

/*
 * One sentence saying why this image cannot fill a package, written to `buf`. Returns the
 * length written, or -1 when the image can fill one (a read slot) or the arguments are
 * unusable.
 */
int mmo_cartridge_refusal(const char *code, char *buf, size_t n);

#endif /* OPENMMO_CARTRIDGE_H */
