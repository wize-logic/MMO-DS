#ifndef OPENMMO_SAVE_POLICY_H
#define OPENMMO_SAVE_POLICY_H
/* Who owns each block of the engine's save file. */

/* Ownership class of a save block. */
typedef enum {
    MMO_SAVE_LOCAL = 0, /* settings/device: read and persisted locally */
    MMO_SAVE_SERVER,    /* game state: seated from the server, never persisted */
    MMO_SAVE_SERVICE,   /* inherently-networked: an out-of-band service, block inert */
} mmo_save_class;

/* Mirror of the engine's enum SaveTableEntryID (order and count pinned to
 * include/constants/savedata/save_table.h). Do not renumber independently. */
enum {
    MMO_SAVE_ENTRY_SYSTEM = 0,
    MMO_SAVE_ENTRY_PLAYER,
    MMO_SAVE_ENTRY_PARTY,
    MMO_SAVE_ENTRY_BAG,
    MMO_SAVE_ENTRY_VARS_FLAGS,
    MMO_SAVE_ENTRY_POKETCH,
    MMO_SAVE_ENTRY_FIELD_PLAYER_STATE,
    MMO_SAVE_ENTRY_POKEDEX,
    MMO_SAVE_ENTRY_DAYCARE,
    MMO_SAVE_ENTRY_PAL_PAD,
    MMO_SAVE_ENTRY_MISC,
    MMO_SAVE_ENTRY_FIELD_OVERWORLD_STATE,
    MMO_SAVE_ENTRY_UNDERGROUND,
    MMO_SAVE_ENTRY_REGULATION_BATTLES,
    MMO_SAVE_ENTRY_IMAGE_CLIPS,
    MMO_SAVE_ENTRY_MAILBOX,
    MMO_SAVE_ENTRY_POFFINS,
    MMO_SAVE_ENTRY_RECORD_MIXED_RNG,
    MMO_SAVE_ENTRY_JOURNAL,
    MMO_SAVE_ENTRY_TRAINER_CASE,
    MMO_SAVE_ENTRY_GAME_RECORDS,
    MMO_SAVE_ENTRY_SEAL_CASE,
    MMO_SAVE_ENTRY_CHATOT,
    MMO_SAVE_ENTRY_FRONTIER,
    MMO_SAVE_ENTRY_RIBBONS,
    MMO_SAVE_ENTRY_ENCOUNTERS,
    MMO_SAVE_ENTRY_GLOBAL_TRADE,
    MMO_SAVE_ENTRY_TV_BROADCAST,
    MMO_SAVE_ENTRY_RANKINGS,
    MMO_SAVE_ENTRY_WIFI_LIST,
    MMO_SAVE_ENTRY_WIFI_HISTORY,
    MMO_SAVE_ENTRY_MYSTERY_GIFT,
    MMO_SAVE_ENTRY_PAL_PARK_TRANSFER,
    MMO_SAVE_ENTRY_LINK_CONTEST_RECORDS,
    MMO_SAVE_ENTRY_UNLOCKED_EASY_CHAT_WORDS,
    MMO_SAVE_ENTRY_EMAIL,
    MMO_SAVE_ENTRY_WIFI_QUESTIONS,
    MMO_SAVE_ENTRY_PC_BOXES,

    MMO_SAVE_ENTRY_MAX
};

/* 1 if id is a real save-table entry (0 <= id < MMO_SAVE_ENTRY_MAX). */
int mmo_save_id_valid(int save_table_id);

/* The ownership class of a block. An out-of-range id is treated as
 * MMO_SAVE_SERVER, the fail-safe: an unknown block is never persisted locally,
 * and a warning is written to stderr rather than the call being trusted. */
mmo_save_class mmo_save_class_of(int save_table_id);

/* May a save of this block reach the local disk? True only for MMO_SAVE_LOCAL. */
int mmo_save_persist_locally(int save_table_id);

/* Is this block filled from server state (Phase 4 WorldState)? True for
 * MMO_SAVE_SERVER; false for local blocks (already real) and services (inert). */
int mmo_save_seat_from_server(int save_table_id);

/* Human-readable names, for logs and traps. */
const char *mmo_save_class_name(mmo_save_class c);
const char *mmo_save_entry_name(int save_table_id);

#endif /* OPENMMO_SAVE_POLICY_H */
