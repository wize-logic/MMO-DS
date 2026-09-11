/* The per-block ownership table. */

#include <stdio.h>

#include "save_policy.h"

typedef struct {
    mmo_save_class cls;
    const char *name;
} save_entry;

/* Indexed by MMO_SAVE_ENTRY_*. Kept dense and in enum order so the id is the
 * index; the test asserts the array length equals MMO_SAVE_ENTRY_MAX. */
static const save_entry sEntries[] = {
    [MMO_SAVE_ENTRY_SYSTEM] = { MMO_SAVE_LOCAL, "SYSTEM" },   /* device identity, RTC, DWC id */
    [MMO_SAVE_ENTRY_PLAYER] = { MMO_SAVE_SERVER, "PLAYER" },  /* TrainerInfo name/money server-owned; the Options and the trainer id inside it are carved out and kept, as their own save blocks (openmmo_save_blocks.c) */
    [MMO_SAVE_ENTRY_PARTY] = { MMO_SAVE_SERVER, "PARTY" },
    [MMO_SAVE_ENTRY_BAG] = { MMO_SAVE_SERVER, "BAG" },
    [MMO_SAVE_ENTRY_VARS_FLAGS] = { MMO_SAVE_SERVER, "VARS_FLAGS" }, /* story/quest progression */
    [MMO_SAVE_ENTRY_POKETCH] = { MMO_SAVE_SERVER, "POKETCH" }, /* which apps the player has been given: a gift app has no story flag behind it, so this block is the only record and it crosses on the wire */
    [MMO_SAVE_ENTRY_FIELD_PLAYER_STATE] = { MMO_SAVE_SERVER, "FIELD_PLAYER_STATE" }, /* Location: mapHeaderID/warp/x/z/dir */
    [MMO_SAVE_ENTRY_POKEDEX] = { MMO_SAVE_SERVER, "POKEDEX" },
    [MMO_SAVE_ENTRY_DAYCARE] = { MMO_SAVE_SERVER, "DAYCARE" },
    [MMO_SAVE_ENTRY_PAL_PAD] = { MMO_SAVE_SERVICE, "PAL_PAD" }, /* friend-code registry: networked */
    [MMO_SAVE_ENTRY_MISC] = { MMO_SAVE_SERVER, "MISC" },
    [MMO_SAVE_ENTRY_FIELD_OVERWORLD_STATE] = { MMO_SAVE_SERVER, "FIELD_OVERWORLD_STATE" },
    [MMO_SAVE_ENTRY_UNDERGROUND] = { MMO_SAVE_SERVER, "UNDERGROUND" }, /* dig/base/traps: player state (the mode is multiplayer, the block is not) */
    [MMO_SAVE_ENTRY_REGULATION_BATTLES] = { MMO_SAVE_SERVER, "REGULATION_BATTLES" },
    [MMO_SAVE_ENTRY_IMAGE_CLIPS] = { MMO_SAVE_SERVER, "IMAGE_CLIPS" },
    [MMO_SAVE_ENTRY_MAILBOX] = { MMO_SAVE_SERVER, "MAILBOX" },
    [MMO_SAVE_ENTRY_POFFINS] = { MMO_SAVE_SERVER, "POFFINS" },
    [MMO_SAVE_ENTRY_RECORD_MIXED_RNG] = { MMO_SAVE_SERVER, "RECORD_MIXED_RNG" },
    [MMO_SAVE_ENTRY_JOURNAL] = { MMO_SAVE_SERVER, "JOURNAL" },
    [MMO_SAVE_ENTRY_TRAINER_CASE] = { MMO_SAVE_SERVER, "TRAINER_CASE" },
    [MMO_SAVE_ENTRY_GAME_RECORDS] = { MMO_SAVE_SERVER, "GAME_RECORDS" },
    [MMO_SAVE_ENTRY_SEAL_CASE] = { MMO_SAVE_SERVER, "SEAL_CASE" },
    [MMO_SAVE_ENTRY_CHATOT] = { MMO_SAVE_LOCAL, "CHATOT" }, /* recorded cry: a local audio capture */
    [MMO_SAVE_ENTRY_FRONTIER] = { MMO_SAVE_SERVER, "FRONTIER" },
    [MMO_SAVE_ENTRY_RIBBONS] = { MMO_SAVE_SERVER, "RIBBONS" }, /* still without a wire on purpose: only the mystery gift command and the wireless record merge write it, and this client reaches neither */
    [MMO_SAVE_ENTRY_ENCOUNTERS] = { MMO_SAVE_SERVER, "ENCOUNTERS" },
    [MMO_SAVE_ENTRY_GLOBAL_TRADE] = { MMO_SAVE_SERVICE, "GLOBAL_TRADE" }, /* GTS */
    [MMO_SAVE_ENTRY_TV_BROADCAST] = { MMO_SAVE_SERVER, "TV_BROADCAST" },
    [MMO_SAVE_ENTRY_RANKINGS] = { MMO_SAVE_SERVER, "RANKINGS" },
    [MMO_SAVE_ENTRY_WIFI_LIST] = { MMO_SAVE_SERVICE, "WIFI_LIST" },
    [MMO_SAVE_ENTRY_WIFI_HISTORY] = { MMO_SAVE_SERVICE, "WIFI_HISTORY" },
    [MMO_SAVE_ENTRY_MYSTERY_GIFT] = { MMO_SAVE_SERVICE, "MYSTERY_GIFT" },
    [MMO_SAVE_ENTRY_PAL_PARK_TRANSFER] = { MMO_SAVE_SERVER, "PAL_PARK_TRANSFER" }, /* pending migrated mons: player state */
    [MMO_SAVE_ENTRY_LINK_CONTEST_RECORDS] = { MMO_SAVE_SERVER, "LINK_CONTEST_RECORDS" },
    [MMO_SAVE_ENTRY_UNLOCKED_EASY_CHAT_WORDS] = { MMO_SAVE_SERVER, "UNLOCKED_EASY_CHAT_WORDS" },
    [MMO_SAVE_ENTRY_EMAIL] = { MMO_SAVE_SERVICE, "EMAIL" },
    [MMO_SAVE_ENTRY_WIFI_QUESTIONS] = { MMO_SAVE_SERVICE, "WIFI_QUESTIONS" },
    [MMO_SAVE_ENTRY_PC_BOXES] = { MMO_SAVE_SERVER, "PC_BOXES" },
};

/* The count check that pins this table to the enum. If a block is added to the
 * enum without a row here (or vice versa) the array length stops matching
 * MMO_SAVE_ENTRY_MAX and this fails to compile. */
_Static_assert(sizeof(sEntries) / sizeof(sEntries[0]) == MMO_SAVE_ENTRY_MAX,
    "save-policy table must have exactly one row per SaveTableEntryID");

int mmo_save_id_valid(int save_table_id)
{
    return save_table_id >= 0 && save_table_id < MMO_SAVE_ENTRY_MAX;
}

mmo_save_class mmo_save_class_of(int save_table_id)
{
    if (!mmo_save_id_valid(save_table_id)) {
        /* Fail safe and loud: an unrecognised block is treated as the server's
         * so it can never be persisted locally, but the caller is warned rather
         * than trusted, because a save id out of range means the enum drifted
         * from the engine and the mapping can no longer be believed. */
        fprintf(stderr,
            "save_policy: save id %d out of range [0,%d), treating as SERVER\n",
            save_table_id, MMO_SAVE_ENTRY_MAX);
        return MMO_SAVE_SERVER;
    }
    return sEntries[save_table_id].cls;
}

int mmo_save_persist_locally(int save_table_id)
{
    return mmo_save_class_of(save_table_id) == MMO_SAVE_LOCAL;
}

int mmo_save_seat_from_server(int save_table_id)
{
    return mmo_save_class_of(save_table_id) == MMO_SAVE_SERVER;
}

const char *mmo_save_class_name(mmo_save_class c)
{
    switch (c) {
    case MMO_SAVE_LOCAL:
        return "local";
    case MMO_SAVE_SERVER:
        return "server";
    case MMO_SAVE_SERVICE:
        return "service";
    }
    return "?";
}

const char *mmo_save_entry_name(int save_table_id)
{
    if (!mmo_save_id_valid(save_table_id)) {
        return "?";
    }
    return sEntries[save_table_id].name;
}
