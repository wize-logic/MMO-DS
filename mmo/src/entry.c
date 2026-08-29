/* The per-surface text-entry table. See entry.h. */

#include "entry.h"
#include "game.h"

mmo_entry_path mmo_entry_path_for(mmo_entry_surface s)
{
    if (s == MMO_ENTRY_ENGINE_NAME)
        return MMO_ENTRY_NAMING_SCREEN;
    return MMO_ENTRY_FIELD;
}

size_t mmo_entry_max_units(mmo_entry_surface s)
{
    switch (s) {
    case MMO_ENTRY_NAME:
        return MMO_CHAR_NAME_MAX;
    case MMO_ENTRY_CHAT:
        return OSK_MAX_TEXT;
    case MMO_ENTRY_SEARCH:
        return OSK_MAX_TEXT;
    case MMO_ENTRY_ENGINE_NAME:
        return MMO_ENTRY_NAMING_MAX;
    }
    return OSK_MAX_TEXT;
}

int mmo_entry_wants_host(mmo_entry_surface s)
{
    return mmo_entry_path_for(s) == MMO_ENTRY_FIELD;
}

int mmo_entry_wants_osk(mmo_entry_surface s)
{
    return mmo_entry_path_for(s) == MMO_ENTRY_FIELD;
}

int mmo_entry_open(osk_state *k, mmo_entry_surface s)
{
    if (k == 0 || mmo_entry_path_for(s) != MMO_ENTRY_FIELD)
        return 0;
    osk_reset(k);
    osk_set_max(k, mmo_entry_max_units(s));
    return 1;
}
