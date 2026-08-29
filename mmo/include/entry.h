#ifndef OPENMMO_ENTRY_H
#define OPENMMO_ENTRY_H
/* Which text-entry path a surface uses. */

#include "osk.h"

#include <stddef.h>

/* The surfaces that take typed text. ENGINE_NAME is a nickname or box name
 * the engine already stores as charcode; the other three go on the wire as
 * UTF-16. */
typedef enum {
    MMO_ENTRY_NAME = 0,     /* character creator; CreateCharacter VARCHAR(32) */
    MMO_ENTRY_CHAT,         /* a chat line */
    MMO_ENTRY_SEARCH,       /* GTL / search box */
    MMO_ENTRY_ENGINE_NAME   /* engine naming screen: nickname, box, … */
} mmo_entry_surface;

/* How that surface is typed. FIELD is osk.c plus the .text page. */
typedef enum {
    MMO_ENTRY_FIELD = 0,
    MMO_ENTRY_NAMING_SCREEN
} mmo_entry_path;

/* The naming screen's nameInputRaw[20] including the EOS slot: at most 19
 * glyphs. Measured in include/applications/naming_screen.h. */
#define MMO_ENTRY_NAMING_MAX 19

/* Which mechanism this surface uses. Unknown values take the field, so a
 * new surface fails by being typed rather than by launching a 3,358-line
 * overlay that cannot hold its bytes. */
mmo_entry_path mmo_entry_path_for(mmo_entry_surface s);

/* Cap of the surface, in UTF-16 code units. NAME is 32 (the wire); CHAT
 * and SEARCH are OSK_MAX_TEXT; ENGINE_NAME is MMO_ENTRY_NAMING_MAX. */
size_t mmo_entry_max_units(mmo_entry_surface s);

/* Nonzero if this surface should set the .text page's `want` (host keys
 * become characters) and draw the on-screen grid. Both are true of FIELD
 * and false of the naming screen, which reads the pad itself. */
int mmo_entry_wants_host(mmo_entry_surface s);
int mmo_entry_wants_osk(mmo_entry_surface s);

/* Reset `k` and cap it at this surface's max. Returns 1 for a FIELD surface.
 * Returns 0 without touching `k` for the naming screen, the caller launches
 * that overlay instead of driving this widget. */
int mmo_entry_open(osk_state *k, mmo_entry_surface s);

#endif /* OPENMMO_ENTRY_H */
