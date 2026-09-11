/* The netcode's public C API, for a host that already owns main(). */
#ifndef MMO_CLIENT_H
#define MMO_CLIENT_H

#include <stddef.h>

#include "mmo.h"
/* For OPENMMO_ENTITY_NAME_MAX: the render events below carry a peer's name at
 * the width the model holds it, rather than repeating the number here. */
#include "entity.h"
#include "game.h"
#include "battle_anim.h"

/* Coarse session status, ordered by progress and suitable to show on screen. */
typedef enum {
    OPENMMO_DISCONNECTED = 0, /* no live session (start not called, or torn down) */
    OPENMMO_CONNECTING,       /* TCP connect to the login server in flight */
    OPENMMO_HANDSHAKING,      /* the P-256 session handshake is running */
    OPENMMO_AUTHENTICATING,   /* LoginRequest sent, awaiting LoginResponse */
    OPENMMO_AUTHED,           /* logged in; the login session is held idle */
    OPENMMO_REQUESTING_GAME,  /* asking the login server for a game-server ticket */
    OPENMMO_JOINING_GAME,     /* connecting/handshaking/joining the game server */
    OPENMMO_IN_GAME,          /* JoinResponse accepted; standing in the game */
    OPENMMO_FAILED            /* terminal failure; see the OPENMMO_EV_FAILED event */
} openmmo_status;

/* How far a session should drive. */
typedef enum {
    OPENMMO_MODE_LOGIN_HOLD = 0, /* reach AUTHED and hold the idle login session */
    OPENMMO_MODE_GAME_JOIN,      /* go all the way to IN_GAME */
    OPENMMO_MODE_CREATE_CHAR     /* join the game server, submit a character, stop */
} openmmo_mode;

/* A session target. Strings are copied on start(); the caller need not keep them. */
typedef struct {
    const char  *user;            /* account name (ASCII) */
    const char  *pass;            /* account password (ASCII, hashed before send) */
    openmmo_mode mode;
    int          phase_timeout_s; /* per-step wall-clock budget; 0 -> 10s */
    /*
     * A join whose character lives in a region this build cannot draw ends the session with a
     * readable explanation rather than a blank map (region.h holds the roster and the
     * sentence).
     */
    int          allow_undrawable_region;
    /* CREATE_CHAR: the character to submit. `create_name` is copied on start(). */
    const char  *create_name;
    int          create_gender;       /* 0 male, 1 female */
    int          create_region;
    int          create_skin_region;
    u16          create_skin_mask;
    u16          create_skin_word[12];
    /*
     * Whether this client runs the game's own field scripts. The fused client does (its engine
     * carries the VM); the headless CLI does not, because it has no engine at all.
     */
    int          local_scripts;
    /* Which character a join selects. `select_how` is 0 (first row of the
     * ordered list), 1 (`select_name`), 2 (`select_index`, 0-based) or 3
     * (`select_region`, a wire region id). 4 means more than one was set
     * and the join refuses rather than guessing. */
    const char  *select_name;
    int          select_index;
    int          select_region;
    int          select_how;
} openmmo_config;

#define OPENMMO_SELECT_FIRST  0
#define OPENMMO_SELECT_NAME   1
#define OPENMMO_SELECT_INDEX  2
#define OPENMMO_SELECT_REGION 3
#define OPENMMO_SELECT_MANY   4
#define OPENMMO_SELECT_BAD    5
#define OPENMMO_SELECT_HOLD   6  /* list arrived; the host picks or creates */

typedef enum {
    OPENMMO_EV_STATUS = 0,     /* the coarse status changed; .status is the new value */
    OPENMMO_EV_JOINED,         /* reached IN_GAME; .join carries the JoinResponse */
    OPENMMO_EV_FAILED,         /* terminal failure; .message says why */
    OPENMMO_EV_DISCONNECTED,   /* a live (authed/in-game) session was closed by the peer */
    /*
     * Remote-entity render events, valid in-game; .entity carries the target. A host (the
     * fused build) turns each into an engine avatar call.
     */
    OPENMMO_EV_ENTITY_SPAWN,   /* create the avatar at .entity.{x,z} facing .dir */
    OPENMMO_EV_ENTITY_STEP,    /* commit one tile: .entity.{x,z} is the new tile */
    OPENMMO_EV_ENTITY_TURN,    /* face .entity.dir without moving */
    OPENMMO_EV_ENTITY_DESPAWN, /* delete the avatar in .entity.slot */
    OPENMMO_EV_ENTITY_PLACE,   /* set the avatar down on .entity.{x,z}, no walk */
    /*
     * The server rejected or corrected the local player's movement. The overworld is server-
     * authoritative: the client predicts each step and animates it at once, then sends it up,
     * but the server owns the verdict.
     */
    OPENMMO_EV_SELF_CORRECT,
    /*
     * The server warped the local player to a new map. The overworld is server-authoritative,
     * so a door, staircase, ledge or script no longer moves the player by a local warp-table
     * lookup: the server runs the transition and places the player, and the client obeys.
     */
    OPENMMO_EV_WARP,

    /* The server's scene for the current map. */
    OPENMMO_EV_MAP,

    /* The server changed the weather under the player mid-map (MapWeatherModeSet
     * 0xC1 or OverworldWeatherControl 0x1E), independent of a map load. .weather
     * carries which packet and its fields; a host re-applies the effect. */
    OPENMMO_EV_WEATHER,

    /*
     * The server started a battle (BattleFieldStatePacket 0x30). The overworld is server-
     * authoritative: a wild encounter no longer begins from a local RNG roll on a grass step, 
     * it begins because the server decided one did and sent this.
     */
    OPENMMO_EV_ENCOUNTER,

    /*
     * One packet of the battle-event stream arrived (a delta, a move, a switch, a reward, a
     * prompt, a slot event, or the bulk state that closes the fight).
     */
    OPENMMO_EV_BATTLE_EVENT,

    /*
     * The server put the player back in the overworld (EntityPresence 0x0F with the overworld
     * status, for the local player's own entity). The battle is over, who won, and what it
     * cost, is carried by the packets that came before it, not by this.
     */
    OPENMMO_EV_BATTLE_END,

    /*
     * The server set or cleared one story-progress flag mid-world (StoryFlagUpdatePacket
     * 0x2A).
     */
    OPENMMO_EV_STORY_FLAG,

    /*
     * The server changed the party (PokemonContainerPacket 0x13, container PARTY): a heal, a
     * catch, a level, a swap. The party the client holds has already been updated when this
     * fires, read it with openmmo_client_party(), so a host redraws whatever shows it.
     */
    OPENMMO_EV_PARTY,

    /*
     * The server changed the PC (PokemonContainerPacket 0x13, container PC): a deposit, a
     * withdrawal, a monster moved between boxes. The storage the client holds has already been
     * updated when this fires, read it with openmmo_client_storage().
     */
    OPENMMO_EV_STORAGE,

    /* The server changed the bag (ItemStacks 0x40, or a single-stack update
     * on 0x42). The bag the client holds has already been updated when this
     * fires, read it with openmmo_client_bag(). The snapshot in the join
     * world-state block seats the bag silently; this is only for a live change. */
    OPENMMO_EV_BAG,

    /* The server set the cash balance (LocalCharacterDelta 0x0C, mask bit
     * 0x1). The world-state snapshot already holds it from LocalPlayerState;
     * this is only for a live change after the player is in the world.
     * .money is the new absolute balance. */
    OPENMMO_EV_MONEY,

    /* The server opened or closed a mart (s2c 0x23). The catalog the client
     * holds has already been updated when this fires, read it with
     * openmmo_client_shop(). A close leaves valid set and open cleared. */
    OPENMMO_EV_SHOP,

    /*
     * A party monster gained a move (MoveLearnPromptPacket 0x17). When .move_learn.slot is a
     * slot the move is already in it and nothing is owed, the server has decided.
     */
    OPENMMO_EV_MOVE_LEARN,

    /* The server answered a breeding pairing (BreedingForecastPacket 0x73). The
     * forecast the client holds has already been updated when this fires, read
     * it with openmmo_client_breeding_forecast(). .breeding says whether the
     * pairing produces anything at all. */
    OPENMMO_EV_BREEDING_FORECAST,

    /* The server sent the incubator slots (EggIncubatorSlotsPacket 0x7E): how
     * many there are and how much wear each has left. Read them with
     * openmmo_client_incubators(). */
    OPENMMO_EV_EGG_INCUBATORS,

    /* An egg in the party hatched. The server owns this: the client holds a party
     * member with its egg flag set, and this fires when a container arrives with
     * the flag cleared on that same monster id. The party has already been
     * updated, so .hatch.party_index names what to draw. */
    OPENMMO_EV_EGG_HATCH,

    /*
     * The server has decided a monster evolves (EvolutionPromptPacket 0x18) and named the
     * species it becomes.
     */
    OPENMMO_EV_EVOLUTION,

    /*
     * The character list is in hand. On a CREATE_CHAR session this still fires only after a
     * create that made the count go up.
     */
    OPENMMO_EV_CHARACTERS,

    /* The server delivered a chat line (ChatMessagePacket 0x09). .chat is
     * the line; the last OPENMMO_CHAT_LOG of them are also
     * openmmo_client_chat(). A join notice and a player line use the same
     * event, .chat.type says which channel. */
    OPENMMO_EV_CHAT,

    /* The server sent a dialog box (s2c 0x21) or the script lock (s2c
     * 0x0E). The box the client holds has already been updated, read
     * it with openmmo_client_dialog(). .dialog says whether this packet
     * opened a resolvable line, closed the box, or changed the lock. */
    OPENMMO_EV_DIALOG,

    /* The server sent a scripted movement sequence (s2c 0x0D). The
     * store the client holds has already been updated, read it with
     * openmmo_client_script_move(). */
    OPENMMO_EV_SCRIPT_MOVE,

    /*
     * The server sent objective progress (s2c 0xD3 bulk replace, or s2c 0xD4 one upsert). The
     * store the client holds has already been updated, read it with
     * openmmo_client_objectives().
     */
    OPENMMO_EV_OBJECTIVE,

    /*
     * The server sent the friends list (s2c 0x63 replace or upsert, 0x64 one insert, 0x65 one
     * delete, 0x66 one online bit). The store the client holds has already been updated, read
     * it with openmmo_client_friends().
     */
    OPENMMO_EV_FRIENDS,

    /*
     * The server sent guild membership (s2c 0x80), a profile (0x81), a member list or row
     * (0x88 / 0x83), a rank or drop (0x84 / 0x85), a presence bit (0x86), or the activity log
     * (0x89).
     */
    OPENMMO_EV_GUILD,

    /*
     * The server sent a mailbox page (s2c 0x97), the three counts (0x98), a compose result
     * (0x96) or one opened letter (0x99). The store the client holds has already been updated,
     * read it with openmmo_client_mail().
     */
    OPENMMO_EV_MAIL,

    /*
     * The server sent a link snapshot (s2c 0xD0), one member (0xD1), a drop (0xD2) or a new
     * captain (0xDA). The store the client holds has already been updated, read it with
     * openmmo_client_link().
     */
    OPENMMO_EV_LINK,

    /*
     * The server raised a menu, list, prompt or scale outside dialog (s2c 0x58 / 0x59 / 0x5A /
     * 0x5B / 0x5C / 0xD6 / 0xD8 / 0xD9 / 0xF1). The store the client holds has already been
     * updated, read it with openmmo_client_ui().
     */
    OPENMMO_EV_UI,

    /*
     * The server sent a digest, a chunked transfer, a stream chunk or an image chunk (s2c 0xA6
     * / 0xA7 / 0xAB / 0xAC / 0x77 / 0xF6). The store the client holds has already been updated,
     * read it with openmmo_client_sync().
     */
    OPENMMO_EV_SYNC,

    /*
     * The server sent a rental offer (s2c 0x71), a page of tournaments (0x75), one
     * tournament's entry count (0x78), a bracket (0x7B) or a score board (0xA4). The store the
     * client holds has already been updated, read it with openmmo_client_compete().
     */
    OPENMMO_EV_COMPETE,
    OPENMMO_EV_GM,

    /*
     * The server seated the local script VM's flags and vars (s2c 0xCB), which it does once
     * per join before the map loads.
     */
    OPENMMO_EV_SCRIPT_STATE,

    /*
     * Another player has offered a battle (s2c 0x50). Nothing has started: the server is
     * holding the offer until this client answers with openmmo_client_reply_duel().
     */
    OPENMMO_EV_DUEL_INVITE,

    /* The answer to a challenge this client made (s2c 0x51):
     * .duel.outcome is MMO_DUEL_DECLINED, _ACCEPTED or _LAPSED. An
     * accept is followed by OPENMMO_EV_LINK_BATTLE; the other two
     * are the end of it. */
    OPENMMO_EV_DUEL_OUTCOME,

    /*
     * The server seated a native link battle (s2c 0xC6). Both players' engines fight it
     * between them: .link_battle.net_id says which side this client is, and net id 0 is the
     * one whose engine computes.
     */
    OPENMMO_EV_LINK_BATTLE,

    /* The link battle ended, from this side's point of view: either
     * the peer left (a 0xC7 of kind leave) or this client reported
     * its own result. The seat is already cleared when this fires. */
    OPENMMO_EV_LINK_BATTLE_END,

    /*
     * The direct trade moved: the table opened, the peer's offered record arrived (s2c 0x52),
     * the peer confirmed, the swap was written, or the whole thing was cancelled (s2c 0x6A).
     */
    OPENMMO_EV_TRADE,

    /* The global trade link answered: the session opened (s2c
     * 0xDC), a search page arrived (s2c 0x9B) or the trade log
     * came back (s2c 0x5E). The store the client holds has
     * already been updated, read it with openmmo_client_gtl(). */
    OPENMMO_EV_GTL,

    /* The link Super Contest moved: this client was queued, was told how
     * many are waiting, or has been seated in one. Read the whole of it
     * with openmmo_client_contest(). */
    OPENMMO_EV_CONTEST
} openmmo_event_kind;

typedef struct {
    openmmo_event_kind kind;
    openmmo_status     status;        /* valid for OPENMMO_EV_STATUS */
    char               message[128];  /* valid for OPENMMO_EV_FAILED / _DISCONNECTED */
    struct {
        s32 playtime;
        s32 reward_points;
        s32 balance;
    } join;                           /* valid for OPENMMO_EV_JOINED */
    struct {
        int slot;      /* 0..7, the compact render slot */
        int localid;   /* map-object local id: 0x100 + slot (the remote-avatar band) */
        int x, z;      /* tile the event refers to (field x, field z) */
        int dir;       /* engine facing DIR_* (NORTH=0/SOUTH=1/WEST=2/EAST=3) */
        int speed;     /* OPENMMO_ENTITY_SPEED_*: the pace the model timed this
                        * step at, and the pace the host has to draw it at */
        int gender;    /* which trainer model to build the avatar from, as the
                        * spawn packet gave it: 0 male, 1 female */
        int version;   /* dp-sprite selector; no packet carries one yet */
        int has_body;  /* 1 when gfx is an explicit catalog body */
        int gfx;       /* object-event graphics id; meaningful when has_body */
        /*
         * The Pokemon walking behind this peer: `has_follower` says whether there is one and
         * `follower_gfx` is the object-event graphics id a filled package planted for it.
         */
        int has_follower;
        int follower_gfx;
        /* The peer's display name as the spawn packet carried it, Latin-1 and
         * NUL-terminated (see openmmo_entity_appearance). It rides the slot's
         * appearance, so every ENTITY_* event for a live slot carries it, not
         * only the spawn; empty when the packet carried no name. */
        char name[OPENMMO_ENTITY_NAME_MAX];
    } entity;                         /* valid for OPENMMO_EV_ENTITY_* */
    struct {
        int region;    /* server region id */
        int bank;      /* server bank id */
        int map;       /* server map id (untranslated; a DS header is the next task) */
        int x, z;      /* tile the server placed the player on (field x, z) */
        int dir;       /* engine facing DIR_* (NORTH=0/SOUTH=1/WEST=2/EAST=3) */
        int seamless;  /* 1 = a LoadMap-less map-edge crossing predicted from the
                        * connection table; 0 = a full server warp arrival */
    } warp;                           /* valid for OPENMMO_EV_WARP */
    struct {
        int region, bank, map;        /* the map this scene belongs to */
        int is_nds;                   /* 1 = NDS map body, 0 = GBA */
        int weather;                  /* Weather ordinal */
        int lighting;                 /* Lighting ordinal */
        int map_type;                 /* MapType ordinal */
        int encounter_type;           /* EncounterType ordinal (GBA; 0 on NDS) */
        int width, height;            /* grid dimensions (GBA; 0 on NDS) */
    } map;                            /* valid for OPENMMO_EV_MAP */
    struct {
        int source;                   /* 0 = MapWeatherModeSet, 1 = WeatherControl */
        int mode;                     /* MapWeatherModeSet.mode (source 0) */
        int enabled;                  /* MapWeatherModeSet.enabled (source 0) */
        int effect;                   /* WeatherControl.effectType (source 1) */
    } weather;                        /* valid for OPENMMO_EV_WEATHER */
    struct {
        int wild;                     /* 1 = wild encounter, 0 = trainer battle */
        int background;               /* battle backdrop selector */
        int foe_species;              /* first revealed opponent; 0 if unread */
        int foe_level;
    } encounter;                      /* valid for OPENMMO_EV_ENCOUNTER */
    struct {
        int opcode;                   /* the s2c opcode that arrived */
        u32 entity_id;                /* primary entity; 0 if the packet names none */
        int move;                     /* 0x33 source move; else 0 */
        int kind;                     /* 0x33 kind, 0x32 packed value, 0x31 phase,
                                       * 0x16 mask, 0x34 event type */
        int hp;                       /* resulting hp when the packet names one */
        int n_targets;                /* 0x33 target count */
    } battle;                         /* valid for OPENMMO_EV_BATTLE_EVENT */
    struct {
        int region;                   /* server region id the flag belongs to */
        int flag_id;                  /* region-scoped GBA story-flag id */
        int enabled;                  /* 1 = set, 0 = cleared */
    } story;                          /* valid for OPENMMO_EV_STORY_FLAG */
    struct {
        int count;                    /* members the party now holds */
        int total;                    /* members the server sent */
    } party;                          /* valid for OPENMMO_EV_PARTY */
    struct {
        int count;                    /* monsters the PC now holds */
        int total;                    /* records the last container packet declared */
    } storage;                        /* valid for OPENMMO_EV_STORAGE */
    struct {
        int count;                    /* stacks the bag now holds */
        int total;                    /* stacks the last snapshot declared */
    } bag;                            /* valid for OPENMMO_EV_BAG */
    struct {
        s32 money;                    /* the new absolute cash balance */
    } money;                          /* valid for OPENMMO_EV_MONEY */
    struct {
        int open;                     /* 1 if a catalog is on the counter */
        int count;                    /* lines the client holds */
        int total;                    /* lines the packet declared */
    } shop;                           /* valid for OPENMMO_EV_SHOP */
    struct {
        s64 monster_id;               /* the monster that gained the move */
        int slot;                     /* the move slot it went into, or
                                       * OPENMMO_MOVE_LEARN_NO_SLOT: an offer */
        int move_id;                  /* server move id, untranslated */
        int engine_move_id;           /* the same move through the 4.1 id map,
                                       * or -1 when it has no engine id */
        int party_index;              /* the party member holding the monster, or
                                       * -1 when it is not in the party the client
                                       * holds */
    } move_learn;                     /* valid for OPENMMO_EV_MOVE_LEARN */
    struct {
        int has_preview;              /* 0 = the server will not breed the pair */
        int species;                  /* server National Dex id of the egg */
        int gender_selectable;        /* the gender buttons apply to this pairing */
    } breeding;                       /* valid for OPENMMO_EV_BREEDING_FORECAST */
    struct {
        int count;                    /* slots the client now holds */
        int total;                    /* slots the server sent */
    } incubators;                     /* valid for OPENMMO_EV_EGG_INCUBATORS */
    struct {
        s64 monster_id;               /* the egg that hatched */
        int party_index;              /* where it sits in the party now */
        int species;                  /* server National Dex id it hatched into */
        u16 engine_species;           /* the same through the id map; 0 = none */
    } hatch;                          /* valid for OPENMMO_EV_EGG_HATCH */
    struct {
        s64 monster_id;               /* the monster that is evolving */
        int species;                  /* server National Dex id it becomes */
        u16 engine_species;           /* the same through the id map; 0 = none */
        int cancelable;               /* the server permits the player to stop it */
        int party_index;              /* the party member holding the monster, or
                                       * -1 when it is not one */
        int storage_index;            /* the PC entry holding it, or -1. The game
                                       * client looks a prompt's monster up across
                                       * every container, so a boxed one evolves
                                       * the same way a party one does */
        int from_species;             /* the server id it is evolving from, or 0
                                       * when the client holds no record of it */
    } evolution;                      /* valid for OPENMMO_EV_EVOLUTION */
    struct {
        int count;                    /* characters the account now holds */
        s64 id;                       /* the list's first id, 0 if still empty */
        char name[MMO_TEXT_BYTES(MMO_CHAR_NAME_MAX)]; /* the name that was submitted */
        int gender;                   /* 0 male, 1 female, as submitted */
        int region;                   /* starting region as submitted */
    } character;                      /* valid for OPENMMO_EV_CHARACTERS */
    struct {
        int type;                     /* MMO_CHAT_* wire byte */
        int language;                 /* Language ordinal, or -1 */
        s64 sender_id;
        char sender[MMO_TEXT_BYTES(MMO_CHAR_NAME_MAX)];
        char text[MMO_TEXT_BYTES(MMO_CHAT_TEXT_MAX)];
    } chat;                           /* valid for OPENMMO_EV_CHAT */
    struct {
        int open;                     /* 1 if a box with a text id is up */
        int close;                    /* 1 if this packet was the close */
        int in_dialog;                /* 1 while the script lock is held */
        int action_type;              /* wire actionType byte */
        s32 text_id;                  /* wire text id, 0 on close */
        int resolved;                 /* 1 if bank/entry mapped */
        u16 bank;
        u16 entry;
        int choice_count;             /* 0 unless this is a 0x23 or 0x31 menu */
    } dialog;                         /* valid for OPENMMO_EV_DIALOG */
    struct {
        s64 entity_id;
        int flag;
        int count;
        int is_self;                  /* 1 if this is the local avatar */
        int slot;                     /* entity-table slot, or -1 */
        int mapped;                   /* actions this client will play */
    } script_move;                    /* valid for OPENMMO_EV_SCRIPT_MOVE */
    struct {
        int replace;                  /* 1 if this was the 0xD3 bulk */
        int count;                    /* entries now held */
        s8  id;                       /* the upserted id; 0 on a bulk */
        s32 value;
        s16 tally;                    /* the wire count field */
    } objective;                      /* valid for OPENMMO_EV_OBJECTIVE */
    struct {
        int replace;                  /* 1 if this was a mode-0 0x63 */
        int count;                    /* entries now held */
        int online;                   /* how many of them are online */
        s64 player;                   /* the row this packet named; 0 on a replace */
        int added;                    /* 1 if 0x64 inserted a new name */
        int removed;                  /* 1 if 0x65 dropped a name */
        char name[MMO_TEXT_BYTES(MMO_CHAR_NAME_MAX)];
    } friends;                        /* valid for OPENMMO_EV_FRIENDS */
    struct {
        int in_guild;                 /* 1 if 0x80 said we are in one */
        int member_count;
        int online;                   /* how many members are online */
        s64 guild_id;
        int added;                    /* 1 if 0x83 inserted a name */
        int removed;                  /* 1 if 0x85 dropped a name */
        char name[MMO_TEXT_BYTES(MMO_GUILD_NAME_MAX)];
        char tag[MMO_TEXT_BYTES(MMO_GUILD_TAG_MAX)];
        char member[MMO_TEXT_BYTES(MMO_CHAR_NAME_MAX)];
    } guild;                          /* valid for OPENMMO_EV_GUILD */
    struct {
        int sent;                     /* 1 if the held page is the sent box */
        int count;                    /* entries the held page now has */
        int inbox;                    /* 0x98 inbox count */
        int outbox;                   /* 0x98 sent count */
        int result;                   /* 0x96 sj1 byte; -1 if this was not one */
        s64 mail_id;                  /* the opened letter, or 0 */
        int is_detail;
        char subject[MMO_TEXT_BYTES(MMO_MAIL_SUBJECT_MAX)];
        char other[MMO_TEXT_BYTES(MMO_CHAR_NAME_MAX)];
    } mail;                           /* valid for OPENMMO_EV_MAIL */
    struct {
        int present;                  /* 1 if 0xD0 said we are in one */
        int count;
        s64 leader;                   /* 0 when not in a link */
        int added;                    /* 1 if 0xD1 inserted a name */
        int removed;                  /* 1 if 0xD2 dropped a name */
        char name[MMO_TEXT_BYTES(MMO_CHAR_NAME_MAX)];
    } link;                           /* valid for OPENMMO_EV_LINK */
    struct {
        int opcode;                   /* the s2c that updated the store */
        int scale;
        int pages;                    /* HUD slots that have arrived */
        int names;
        int options;
        int rows;
        int confirm;                  /* 1 if a confirm is showing */
        int prompt;                   /* 1 if a 0xD6 is still open */
    } ui;                             /* valid for OPENMMO_EV_UI */
    struct {
        int opcode;
        int digest;                   /* 1 if a 0xA6 / 0xA7 is held */
        int transfer_done;
        int stream_done;
        int image_done;
        int plain_len;                /* opened 0xAB+0xAC bytes */
    } sync;                           /* valid for OPENMMO_EV_SYNC */
    struct {
        int opcode;                   /* the s2c that updated the store */
        int rentals;                  /* monsters or previews on the offer */
        int tourneys;                 /* rows on the last page */
        int matchups;                 /* slots in the last bracket */
        int board_rows;
        s16 entered;                  /* the last 0x78's first short */
    } compete;                        /* valid for OPENMMO_EV_COMPETE */
    struct {
        int opcode;                   /* the s2c that updated the store */
        int found;                    /* the last 0xA1 held a player */
        int variant;                  /* the last 0xA2's discriminator byte */
        int rows;                     /* menu rows on that 0xA2 */
        int cleared;                  /* the last 0xF7 was the clear */
    } gm;                             /* valid for OPENMMO_EV_GM */
    struct {
        int outcome;                  /* MMO_DUEL_* (OPENMMO_EV_DUEL_OUTCOME) */
        int flags;                    /* invite byte, carried (no measured meaning) */
        int request_type;             /* the second invite byte, likewise */
        char name[OPENMMO_ENTITY_NAME_MAX]; /* the challenger's name */
    } duel;                           /* valid for OPENMMO_EV_DUEL_* */
    struct {
        int battle_id;                /* the server's id for this fight */
        int net_id;                   /* 0 computes the fight, 1 presents it */
        int count;                    /* opponent party members stored */
        char peer_name[OPENMMO_ENTITY_NAME_MAX];
    } link_battle;                    /* valid for OPENMMO_EV_LINK_BATTLE */
    struct {
        int state;                    /* MMO_TRADE_STATE_*, or 0 for an entry */
        int entry;                    /* 1 when the peer's record arrived */
        char peer[OPENMMO_ENTITY_NAME_MAX];
    } trade;                          /* valid for OPENMMO_EV_TRADE */
    struct {
        int opcode;                   /* the s2c that updated the store */
        int rows;                     /* rows on the held page */
        s32 total;                    /* matches behind that page */
    } gtl;                            /* valid for OPENMMO_EV_GTL */
} openmmo_event;

/*
 * The server's authoritative overworld state, consumed from the select/resync world-state
 * block (Phase 4.2) and translated to engine ids through the 4.1 id map where a translation
 * exists.
 */
typedef struct {
    int valid;
    int region;               /* server region id */
    int bank_id;              /* server bank id (identifies a GBA map with map_id) */
    int map_id;               /* server map id; header is mmo_id_map_header_from_server */
    int x, y, z;              /* server tile */
    /* The current map's server-owned scene, seated from its LoadMap. Raw server
     * ordinals; 0 on the GBA-only fields when the current map is an NDS map. */
    int map_is_nds;
    int weather, lighting, map_type, encounter_type;
    int map_width, map_height;
    s32 money;
    int gender;
    s64 character_id;         /* the selected character's id; 0 until pick */
    char name[MMO_TEXT_BYTES(MMO_CHAR_NAME_MAX)]; /* as the list / SelectedCharacter carried it */
    int party_count;          /* party members present (<= 6) */
    u16 party_species[6];     /* engine species ids; 0 = untranslatable */
    int party_untranslatable; /* party species with no engine id */
    /* party_* above is a summary of openmmo_client_party(), which is the party
     * itself. Both come from the server; the party container is the one that
     * carries the monsters, and it seats these once it has arrived. */
    int item_count;           /* bag stacks on the wire, summary of openmmo_client_bag() */
    int item_untranslatable;  /* stacks whose item id had no engine id */
    int flag_count;           /* story flags set (enabled), summary of the store */
    int var_count;            /* story variables received, summary of the store */
    /* The badges earned, in the wire's own numbering (the first gym's 0), as
     * many as MMO_WS_BADGE_MAX holds of badge_count on the wire. The fused
     * client seats them into the save's TrainerInfo, which this engine's own
     * field-move routines ask before every rock, tree and boulder. */
    int badge_count;
    s16 badges[MMO_WS_BADGE_MAX];
} openmmo_world_state;

/* --- the server-owned progression store ---------------------------------- * */
#define OPENMMO_WORLD_FLAG_GROUPS 8    /* the game client has 4 slots; headroom */
#define OPENMMO_WORLD_FLAG_BLOCK  256  /* max bytes per inflated flag block (server: 67) */
#define OPENMMO_STORY_FLAG_MAX    512  /* set story flags held; overflow is flagged */
#define OPENMMO_STORY_VARS        256  /* GBA var range 0x4000..0x40ff */

typedef struct {
    int region;                        /* the region this state is scoped to */
    /* World-flag table: the inflated flag blocks, one per group (0x0A). */
    u8  flag_block[OPENMMO_WORLD_FLAG_GROUPS][OPENMMO_WORLD_FLAG_BLOCK];
    int flag_block_len[OPENMMO_WORLD_FLAG_GROUPS];
    int flag_group_count;
    /* Story flags currently set (0x2A); a clear removes one. */
    int set_flag[OPENMMO_STORY_FLAG_MAX];
    int set_flag_count;
    int flag_overflow;                 /* set flags dropped for want of room */
    /* Story variables (GBA 0x4000+key), indexed by the wire key 0..255. */
    s16 var[OPENMMO_STORY_VARS];
    u8  var_present[OPENMMO_STORY_VARS];
    int var_count;                     /* variables present */
} openmmo_story_store;

/* --- the local script VM's own state ------------------------------------- * */

typedef struct {
    int seated;                                       /* a seat has arrived */
    unsigned seq;                                     /* bumped by each seat */
    u8  flag[(MMO_SCRIPT_FLAG_MAX + 7) / 8];          /* bit per flag id */
    u16 var[MMO_SCRIPT_VAR_MAX];                      /* indexed id - BASE */
    int flags_set;                                    /* set bits, for a log line */
    int vars_set;                                     /* non-zero vars */
    int out_of_range;                                 /* ids the seat could not hold */
    int running_shoes;                                /* synthetic flag 3000, see below */
    unsigned badges;                                  /* bit i = Sinnoh badge i, flags 3001.. */
    int respawn;                                      /* synthetic var 3009; 0 = none seated */
    /* Save blocks seated whole (game.h, mmo_save_block). Held by value because
     * the frame they arrived in is reused before the field this seats into
     * exists: a seat lands in the join burst, ahead of the map. */
    int block_count;
    struct {
        int id;                                       /* engine SaveTableEntryID */
        int len;
        u8  data[MMO_SAVE_BLOCK_BYTES];
    } block[MMO_SAVE_BLOCK_MAX];
    int blocks_dropped;                               /* past the caps above */
} openmmo_script_state;

/*
 * Synthetic flag ids: state the engine keeps outside VarsFlags (PlayerData booleans and their
 * kin) still needs a row in the server's story record, or a rejoin loses it, the play path
 * rolls a fresh save every join.
 */
#define MMO_SCRIPT_FLAG_RUNNING_SHOES 3000
/*
 * The eight Sinnoh badges, one flag each: 3001 is the Coal Badge and 3008 the Beacon Badge.
 * They are TrainerInfo bits the gym scripts set, and nothing reported them, so a badge earned
 * online was gone at the next login and every HM gate refused again.
 */
#define MMO_SCRIPT_FLAG_BADGE_BASE 3001
#define MMO_SCRIPT_BADGE_COUNT 8
/* The engine's black-out warp id (spawn_locations.c, 1..20), as a synthetic
 * Var row in the same band: FieldOverworldState is not on the wire, and a
 * fresh save answers 1, the player's own bed in Twinleaf, for every white
 * out of the session, however far the character has come. */
#define MMO_SCRIPT_VAR_RESPAWN 3009

/* --- the server-owned party ---------------------------------------------- * */
#define OPENMMO_PARTY_MAX 6

typedef struct {
    s64 id;                   /* the monster's server id; updates key on it */
    u16 species;              /* engine species id; 0 = untranslatable */
    u16 dex_id;               /* server National Dex id, as sent */
    int slot;                 /* the server's slot within the container */
    int level;
    int hp;                   /* current HP */
    s32 xp;
    int egg;
    char nickname[MMO_TEXT_BYTES(MMO_MON_NAME)]; /* empty when the species name is shown */
    char ot[MMO_TEXT_BYTES(MMO_MON_NAME)];       /* original trainer */
    u16 move_id[4];           /* server move ids; 0 = empty slot */
    u16 move[4];              /* engine move ids; 0 = empty slot or untranslatable */
    u8  move_pp[4];
    u8  move_pp_up[4];        /* PP Ups on each slot, 0..3 */
    /* What a detail screen draws. */
    u8  ev[6];
    u8  iv[6];
    /* What a contest scores on. */
    u8  cond[MMO_MON_CONDITIONS];
    u8  sheen;
    u64 ribbons_super;
    /*
     * The personality the engine rolled for this monster when it was caught, carried on the
     * record ever since.
     */
    u32 seed;
    int nature;               /* 0..24, engine nature ids (identical numbering) */
    int friendship;           /* 0..255 */
    /*
     * What it is carrying, both ways round: the server's wire id and the engine id the seat
     * writes onto the monster. 0 is carrying nothing in both, and `held_item` non-zero with
     * `held_item_engine` 0 is an item the record names and this build cannot seat (idmap.h).
     */
    int held_item;            /* server wire item id; 0 = carrying nothing */
    u16 held_item_engine;     /* engine item id; 0 = nothing or untranslatable */
    int form;                 /* alternate forme id, 0 for the ordinary one */
    int shiny;
    /* 0 or 1 index the species' two engine abilities. 2 means the hidden ability,
     * which Gen 4 has no concept of and the engine cannot name: a caller must not
     * draw an ability for it. `ability_unrenderable` says so without the caller
     * having to know why 2 is special. */
    int ability_slot;
    int ability_unrenderable;
    int move_unmapped;        /* move slots with no engine id (see idmap.h) */
    /*
     * Where and when this monster was caught, which the summary screen draws and this client
     * used to invent.
     */
    int caught_map_header;
    /*
     * The engine's own location label, where the record carries one in place of a map; 0 for a
     * record that carries none. It crosses unchanged: the file it came from is this engine's
     * own save, so its label is already an index into this engine's location names.
     */
    int caught_location_label;
    s32 caught_at;
    /*
     * What it is suffering from, as the engine's own condition word: the sleep counter in the
     * bottom three bits, then poison, burn, freeze, paralysis and bad poison a bit each, and
     * the bad poison's own counter above them. 0 is a healthy monster.
     */
    int status;
} openmmo_party_mon;

typedef struct {
    int valid;                /* 1 once a party container has been received */
    int count;                /* members held (<= OPENMMO_PARTY_MAX) */
    int total;                /* members the server sent */
    int untranslatable;       /* members whose species has no engine id */
    int unmapped_moves;       /* move slots across the party with no engine id */
    int hidden_abilities;     /* members on a hidden ability the engine cannot draw */
    int malformed;            /* party containers the client could not read */
    int trailing;             /* bytes past the last record of the last container:
                               * non-zero means the server wrote a record at a
                               * length the game client does not read */
    openmmo_party_mon mon[OPENMMO_PARTY_MAX];
} openmmo_party;

/* --- the PC ---------------------------------------------------------------- * */
#define OPENMMO_STORAGE_MAX 660

/* Slots to a box, which is the box screen's own count and the server's
 * (PC_BOX_SIZE). The wire never says it: a box is `slot / OPENMMO_BOX_SIZE`. */
#define OPENMMO_BOX_SIZE 30

/* The day care's slots, which is what the building holds. */
#define OPENMMO_DAYCARE_MAX 2

typedef struct {
    int valid;      /* 1 once a PC container has been received */
    int count;      /* monsters held */
    int total;      /* records the last container packet declared */
    int dropped;    /* records past OPENMMO_STORAGE_MAX the client could not hold */
    int malformed;  /* PC containers the client could not read */
    int trailing;   /* bytes past the last record the game client reads */
    openmmo_party_mon *mon; /* `count` entries in slot order, or NULL until one
                             * arrives; owned by the client */
} openmmo_storage;

/* --- the server-owned bag ------------------------------------------------ * */
#define OPENMMO_BAG_MAX 476

typedef struct {
    s64 object_id;            /* the stack's server id; updates key on it */
    u16 item_id;              /* server wire id (region*1000 + index) */
    u16 engine_id;            /* engine item id; 0 = untranslatable */
    int quantity;
} openmmo_bag_stack;

typedef struct {
    int valid;                /* 1 once a snapshot or update has been received */
    int count;                /* stacks held (<= OPENMMO_BAG_MAX) */
    int total;                /* stacks the last snapshot declared */
    int untranslatable;       /* stacks whose item id has no engine id */
    int dropped;              /* stacks past OPENMMO_BAG_MAX the client could not hold */
    int malformed;            /* bag packets the client could not read */
    /* The Y-registered key item as a wire id, 0 for none; seated by s2c 0xDD
     * behind the bag it points into. registered_valid says the seat arrived
     * at all, so a session against an older server leaves the engine's own
     * register alone instead of clearing it every join. */
    u16 registered_item;
    int registered_valid;
    openmmo_bag_stack stack[OPENMMO_BAG_MAX];
} openmmo_bag;

/* --- the server-owned mart --------------------------------------------- * */
#define OPENMMO_SHOP_MAX MMO_SHOP_ITEM_MAX

typedef struct {
    u16 item_id;              /* server wire id */
    u16 engine_id;            /* engine item id; 0 = untranslatable */
    s16 stock;
    s32 price;
} openmmo_shop_item;

typedef struct {
    int valid;                /* 1 once a catalog or close has been received */
    int open;                 /* 1 while a catalog is on the counter */
    int kind;                 /* en0 type; -1 after a close */
    u8  flags;
    u8  currency_kind;
    s64 npc_entity_id;        /* 0 when flags bit 0x40 was unset */
    int count;                /* lines held (<= OPENMMO_SHOP_MAX) */
    int total;                /* lines the last open declared */
    int untranslatable;       /* lines whose item id has no engine id */
    int dropped;              /* lines past OPENMMO_SHOP_MAX */
    int malformed;            /* catalog packets the client could not read */
    openmmo_shop_item item[OPENMMO_SHOP_MAX];
} openmmo_shop;

/* --- a server-driven dialog box ---------------------------------------- * */
typedef struct {
    int valid;                /* 1 once a 0x21 has been received */
    int open;                 /* 1 while a box with a text id is up */
    int close;                /* 1 if the last packet was the close */
    s8  flags;
    s8  action_type;
    s32 text_id;
    s64 entity_id;
    s32 context_value;
    int arg_count;
    int resolved;             /* 1 if bank/entry mapped */
    u16 bank;
    u16 entry;
    int locked;               /* 1 while s2c 0x0E said the lock is held */
    int awaiting;             /* 1 while a box still owes a 0x21 reply */
    int malformed;            /* 0x21 bodies the client could not read */
    const char *why;          /* static reason when resolve failed */
    int choice_count;         /* 0 unless action_type is a 0x23 or 0x31 menu */
    s16 choice_bank;          /* 0 unless action_type is 0x31; the DS text bank */
    s16 choices[MMO_DIALOG_MENU_MAX];
    /* String variables this box fills, from the tag-4 message args: the
     * names the DS Buffer* commands would have read off a save. The
     * engine's own StringTemplate is what puts them in the line. */
    int strvar_count;
    mmo_dialog_strvar strvar[MMO_DIALOG_STRVAR_MAX];
} openmmo_dialog;

/* --- a server-driven movement sequence --------------------------------- * */
typedef struct {
    int valid;                /* 1 once a 0x0D has been received */
    s64 entity_id;
    u8  flag;
    u8  count;
    u8  actions[MMO_SCRIPT_MOVE_MAX];
    int is_self;
    int slot;                 /* entity-table slot, or -1 */
    int mapped;
    int unknown;              /* count of bytes with no action */
    int malformed;            /* 0x0D bodies the client could not read */
    int dropped;              /* sequences past the arrival queue's depth */
} openmmo_script_move;

/* Scripted movement sequences held between arrival and playback. A cutscene
 * beat sends several at once (the player's turn and an npc's walk are one
 * moveSelfAndNpcs), and they are all parsed before the host drains an event,
 * so each needs a copy of its own. */
#define OPENMMO_SCRIPT_MOVE_QUEUE 8

/* --- map / cutscene npcs (s2c 0x12) ------------------------------------ * */
#define OPENMMO_NPC_MAX 32

typedef struct {
    s64 entity_id;
    int graphics_id;
    int x, y;
    int facing;               /* engine DIR_* */
    int movement;             /* this server's unk3 high byte */
    int live;
} openmmo_npc;

typedef struct {
    int valid;                /* 1 once a 0x12 has been received */
    int count;                /* live rows */
    int dropped;              /* past OPENMMO_NPC_MAX */
    int malformed;            /* 0x12 bodies the client could not read */
    openmmo_npc npc[OPENMMO_NPC_MAX];
} openmmo_npc_store;

/* --- the server-owned objective store ---------------------------------- * */
#define OPENMMO_OBJECTIVE_MAX MMO_OBJECTIVE_MAX

typedef struct {
    s8  id;
    s32 value;
    s16 count;
} openmmo_objective;

typedef struct {
    int valid;                /* 1 once a 0xD3 or 0xD4 has been received */
    int count;                /* entries held */
    int dropped;              /* entries past OPENMMO_OBJECTIVE_MAX */
    int malformed;            /* bodies the client could not read */
    openmmo_objective entry[OPENMMO_OBJECTIVE_MAX];
} openmmo_objectives;

/* --- the server-owned friends list ------------------------------------- * */
#define OPENMMO_FRIEND_MAX MMO_FRIEND_MAX

typedef struct {
    s64 player;
    s32 unknown;
    int online;
    char name[MMO_TEXT_BYTES(MMO_CHAR_NAME_MAX)];
    u8  unk0;
    s32 last_seen;
    u8  kind;
    u8  packed_slots;
    s16 sprite[MMO_FRIEND_SPRITE_COUNT];
} openmmo_friend;

typedef struct {
    int valid;                /* 1 once a 0x63/0x64/0x65/0x66 has arrived */
    int count;                /* entries held */
    int dropped;              /* entries past OPENMMO_FRIEND_MAX */
    int malformed;            /* bodies the client could not read */
    openmmo_friend entry[OPENMMO_FRIEND_MAX];
} openmmo_friends;

/* --- the server-owned guild -------------------------------------------- * */
#define OPENMMO_GUILD_MEMBER_MAX MMO_GUILD_MEMBER_MAX
#define OPENMMO_GUILD_LOG_MAX    MMO_GUILD_LOG_MAX

typedef struct {
    u8  rank;
    s64 entity_id;
    s32 joined_at;
    int online;
    char name[MMO_TEXT_BYTES(MMO_CHAR_NAME_MAX)];
    u8  unk0;
    s32 last_seen;
    u8  kind;
    u8  packed_slots;
    s16 sprite[MMO_FRIEND_SPRITE_COUNT];
} openmmo_guild_member;

typedef struct {
    int valid;                /* 1 once a guild packet has arrived */
    int in_guild;
    mmo_guild_profile profile;
    int member_count;
    int dropped;
    int malformed;
    openmmo_guild_member member[OPENMMO_GUILD_MEMBER_MAX];
    int log_valid;
    s16 log_total;
    int log_count;
    mmo_guild_log_entry log[OPENMMO_GUILD_LOG_MAX];
} openmmo_guild;

/* --- the server-owned mailbox ------------------------------------------ * */
#define OPENMMO_MAIL_MAX MMO_MAIL_MAX

typedef struct {
    int valid;                /* 1 once a mail packet has arrived */
    int inbox;                /* 0x98 inbox count */
    int inbox_seen;
    int sent;                 /* 0x98 sent count */
    int listed_sent;          /* 1 if entry[] is the sent box */
    s16 page;
    int count;                /* entries the last 0x97 held */
    int dropped;
    int malformed;
    int result;               /* last 0x96 code; -1 if none */
    /* Climbs on every 0x96. A window watches it so one answer is shown
     * once, rather than for as long as the code sits in `result`. */
    unsigned result_seq;
    int have_detail;
    int detail_sent;          /* 1 when the open letter came from the sent box */
    mmo_mail detail;
    mmo_mail entry[OPENMMO_MAIL_MAX];
} openmmo_mail;

/* --- the server-owned link --------------------------------------------- * */
#define OPENMMO_LINK_MAX MMO_LINK_MAX

typedef struct {
    int valid;                /* 1 once a link packet has arrived */
    int present;
    s64 leader;
    int count;
    int dropped;
    int malformed;
    mmo_link_member member[OPENMMO_LINK_MAX];
} openmmo_link;

/* --- the server-driven UI outside dialog -------------------------------- * */
#define OPENMMO_UI_NAME_MAX   MMO_UI_NAME_MAX
#define OPENMMO_UI_OPTION_MAX MMO_UI_OPTION_MAX
#define OPENMMO_UI_ROW_MAX    MMO_UI_ROW_MAX

typedef struct {
    int valid;                /* 1 once any of these packets arrived */
    int last_op;              /* the opcode that last updated it */
    int malformed;
    int scale_valid;
    s8  scale;
    int menu_visible;
    s8  menu_type;            /* last 0x58 type; 0 when hidden */
    int page_valid[MMO_UI_MENU_TYPES];
    int page_present[MMO_UI_MENU_TYPES];
    int page_len[MMO_UI_MENU_TYPES];
    u8  page[MMO_UI_MENU_TYPES][MMO_UI_PAGE_MAX];
    int names_valid;
    mmo_name_choices names;
    int options_valid;
    mmo_option_list options;
    int list_valid;
    mmo_list_window list;
    int confirm_valid;
    mmo_confirm_prompt confirm;
    int prompt_open;
    mmo_menu_prompt prompt;
} openmmo_ui;

/* --- chunked data-sync -------------------------------------------------- * */
#define OPENMMO_SYNC_DIGEST_MAX MMO_SYNC_DIGEST_MAX
#define OPENMMO_SYNC_SIG_MAX    MMO_SYNC_SIG_MAX
#define OPENMMO_SYNC_PLAIN_MAX  MMO_SYNC_PLAIN_MAX
#define OPENMMO_SYNC_STREAM_MAX MMO_SYNC_STREAM_MAX
#define OPENMMO_SYNC_IMAGE_MAX  MMO_SYNC_IMAGE_MAX

typedef struct {
    int valid;
    int last_op;
    int malformed;
    int digest_valid;
    int digest_op;
    u16 digest_bits;
    u16 digest_len;
    u8  digest[OPENMMO_SYNC_DIGEST_MAX];
    int transfer_valid;
    s64 transfer_id;
    s32 transfer_size;
    int transfer_sig_len;
    u8  transfer_sig[OPENMMO_SYNC_SIG_MAX];
    int transfer_got;
    int transfer_done;
    int transfer_open_ok;
    int transfer_plain_len;
    u8  transfer_plain[OPENMMO_SYNC_PLAIN_MAX];
    int stream_valid;
    s64 stream_id;
    int stream_got;
    int stream_done;
    int stream_len;
    u8  stream[OPENMMO_SYNC_STREAM_MAX];
    int image_valid;
    s8  image_ctrl;
    s8  image_type;
    int image_last;
    int image_len;
    u8  image[OPENMMO_SYNC_IMAGE_MAX];
} openmmo_sync;

/* --- matchmaking, tournaments and score boards -------------------------- * */
typedef struct {
    int valid;
    int last_op;
    int malformed;
    int rentals_valid;
    mmo_rentals rentals;
    int page_valid;
    mmo_tourney_page page;
    int count_valid;
    mmo_tourney_count count;
    int matchups_valid;
    mmo_matchups matchups;
    int board_valid;
    mmo_score_board board;
} openmmo_compete;

/* --- staff panels, notes and moderation --------------------------------- * */
typedef struct {
    int valid;
    int last_op;
    int malformed;
    int lookup_valid;
    mmo_gm_lookup lookup;
    int panel_valid;
    mmo_gm_panel panel;
    int entry_valid;
    mmo_gm_panel_entry entry;
} openmmo_gm;

typedef struct openmmo_client openmmo_client;

/* Allocate an idle client, or NULL on out-of-memory. */
openmmo_client *openmmo_client_new(void);

/* Tear down any live session and free the client. */
void openmmo_client_free(openmmo_client *c);

/*
 * Begin the session described by cfg (copied). Non-blocking: it kicks off the connect and
 * returns 0.
 */
int openmmo_client_start(openmmo_client *c, const openmmo_config *cfg);

/*
 * The state byte (MMO_LOGIN_* in login.h) of the last LoginResponse that refused this client,
 * or -1 if the current session has not been refused. Cleared by every start.
 */
int openmmo_client_login_refusal(const openmmo_client *c);

/* Advance the session by one frame's worth of I/O. Never blocks, never sleeps.
 * Call once per frame; it is a no-op on a terminal (DISCONNECTED/FAILED) client. */
void openmmo_client_pump(openmmo_client *c);

/* Dequeue one pending event into *ev. Returns 1 if one was written, 0 if the
 * queue is empty. Drain it in a loop each frame after pumping. */
int openmmo_client_poll_event(openmmo_client *c, openmmo_event *ev);

/* The current coarse status, for a connection indicator. */
openmmo_status openmmo_client_status(const openmmo_client *c);

/* Give whatever has already been queued up to ms milliseconds to reach the
 * server. Returns 0 once nothing is left, -1 if something never went (and says
 * so on stderr). The last thing a session sends needs this: the disconnect
 * below frees the send queue rather than emptying it. */
int openmmo_client_flush(openmmo_client *c, int ms);

/* Close the session (TCP FIN) and return the client to DISCONNECTED. Safe to
 * call at any time; a fresh start() reuses the same handle. */
void openmmo_client_disconnect(openmmo_client *c);

/* A human-readable name for a status, for logs and the on-screen indicator. */
const char *openmmo_status_name(openmmo_status s);

/* Adopt an already-connected socket as the login stream and begin the handshake,
 * skipping the TCP connect. For tests that drive the FSM over a socketpair, and
 * for a host that opens its own socket. cfg is copied as in start(); its host and
 * ports are unused. Returns 0, or -1 if the client is not idle. */
int openmmo_client_attach_fd(openmmo_client *c, int fd, const openmmo_config *cfg);

/*
 * Say `timestamp` in every ClientHello this client sends from now on, instead of reading the
 * clock.
 */
void openmmo_client_pin_hello_time(openmmo_client *c, s64 timestamp);

/* Send the local player's confirmed one-tile step to the server. */
int openmmo_client_send_move(openmmo_client *c, int from_x, int from_z,
                             int dir, int running);

/*
 * Send a step that covered more than one tile. `tiles` is 1..3, the number the engine's own
 * movement action moves the avatar: a walk is 1, a ledge hop (JUMP_FAR) 2, a Distortion World
 * gap (JUMP_DISTORTION_WORLD) or a top-gear ramp jump (JUMP_FARTHER) 3.
 */
int openmmo_client_send_move_tiles(openmmo_client *c, int from_x, int from_z,
                                   int dir, int running, int tiles);

/* Send a turn in place. `dir` is the engine facing DIR_* the avatar is now
 * looking. Frames a FaceDirectionPacket. No-op returning -1 unless IN_GAME;
 * returns 0 on success, or -1 (tearing the session down) if the send failed. */
int openmmo_client_send_face(openmmo_client *c, int dir);

/* Send a chat line. A line beginning with '/' is a server command (e.g.
 * "/testbattle" to start a wild battle for a developer character). Frames a
 * ChatMessageSendPacket and queues it. No-op returning -1 unless IN_GAME; returns
 * 0 on success, or -1 (tearing the session down) if the send failed. */
int openmmo_client_send_chat(openmmo_client *c, const char *text);

/* Send a chat line on a channel: `mode` is MMO_CHAT_NORMAL (the local map),
 * MMO_CHAT_GLOBAL or MMO_CHAT_TRADE (server-wide). MMO_CHAT_WHISPER is
 * refused, a whisper carries a target and goes through
 * openmmo_client_send_whisper. Otherwise openmmo_client_send_chat exactly. */
int openmmo_client_send_chat_mode(openmmo_client *c, int mode,
                                  const char *text);

/* Send a line into the current battle (BattleChatMessagePacket 0x4B). The
 * server scopes it to the sender's battle and answers with a notice when
 * there is none. No-op returning -1 unless IN_GAME and `text` is non-empty. */
int openmmo_client_send_battle_chat(openmmo_client *c, const char *text);

/*
 * Report the local player's own spawn tile, the tile the server placed it on. Seeded from the
 * join's LocalPlayerState and overwritten by the self LoadEntity when that packet arrives.
 */
int openmmo_client_self_tile(const openmmo_client *c, int *x, int *z);

/* The world-state snapshot consumed from the select/resync block, or NULL if the
 * block has not been consumed on this session. Valid from the JOINED event
 * onward; the returned pointer is owned by the client and stays live until the
 * next start(). */
const openmmo_world_state *openmmo_client_world_state(const openmmo_client *c);

/* The server-owned progression store (see openmmo_story_store). Never NULL; it is
 * zeroed until the world-state block seeds it. The pointer is owned by the client
 * and stays live until the next start(). */
const openmmo_story_store *openmmo_client_story_store(const openmmo_client *c);

/* The local script VM's seated flags and vars (see openmmo_script_state). Never
 * NULL; `seated` is 0 until the server's 0xCB arrives. The pointer is owned by
 * the client and stays live until the next start(). */
const openmmo_script_state *openmmo_client_script_state(const openmmo_client *c);

/* The server-owned party (see openmmo_party). Never NULL; `valid` is 0 until the
 * first party container arrives. The pointer is owned by the client and stays
 * live until the next start(). */
const openmmo_party *openmmo_client_party(const openmmo_client *c);

/* The server-owned PC (see openmmo_storage). Never NULL; `valid` is 0 until the
 * first PC container arrives. The pointer is owned by the client and stays live
 * until the next start(). */
const openmmo_storage *openmmo_client_storage(const openmmo_client *c);

/*
 * Who the server says is boarding at the day care, in the same shape as the PC and seated from
 * the same container packet. Never NULL; `valid` is 0 until the first day care container
 * arrives.
 */
const openmmo_storage *openmmo_client_daycare(const openmmo_client *c);

/* The server-owned bag (see openmmo_bag). Never NULL; `valid` is 0 until the
 * first item snapshot arrives. The pointer is owned by the client and stays
 * live until the next start(). */
const openmmo_bag *openmmo_client_bag(const openmmo_client *c);

/* The server-owned mart (see openmmo_shop). Never NULL; `valid` is 0 until the
 * first catalog arrives. The pointer is owned by the client and stays live
 * until the next start(). */
const openmmo_shop *openmmo_client_shop(const openmmo_client *c);

/* The last dialog box (see openmmo_dialog). Never NULL; `valid` is 0
 * until the first 0x21 or 0x0E arrives. The pointer is owned by the
 * client and stays live until the next start(). */
const openmmo_dialog *openmmo_client_dialog(const openmmo_client *c);

/* The last scripted movement sequence (see openmmo_script_move). Never
 * NULL; `valid` is 0 until the first 0x0D arrives. The pointer is owned
 * by the client and stays live until the next start(). */
const openmmo_script_move *openmmo_client_script_move(const openmmo_client *c);

/* Take the oldest sequence that has arrived and not been played yet, or NULL
 * when there is none. The returned pointer is owned by the client and stays
 * valid until OPENMMO_SCRIPT_MOVE_QUEUE more sequences have arrived. */
const openmmo_script_move *openmmo_client_script_move_take(openmmo_client *c);

/* The map / cutscene npc store (see openmmo_npc_store). Never NULL;
 * `valid` is 0 until the first 0x12 arrives. The pointer is owned by
 * the client and stays live until the next start(). */
const openmmo_npc_store *openmmo_client_npcs(const openmmo_client *c);

/* The server-owned objective store (see openmmo_objectives). Never
 * NULL; `valid` is 0 until the first 0xD3 or 0xD4 arrives. The pointer
 * is owned by the client and stays live until the next start(). */
const openmmo_objectives *openmmo_client_objectives(const openmmo_client *c);

/* The server-owned friends list (see openmmo_friends). Never NULL;
 * `valid` is 0 until the first 0x63/0x64/0x65/0x66 arrives. The
 * pointer is owned by the client and stays live until the next
 * start(). */
const openmmo_friends *openmmo_client_friends(const openmmo_client *c);

/* Ask the server to add or remove a friend by the name the list
 * shows. No-op returning -1 unless IN_GAME and `name` is non-empty;
 * returns 0 on success, or -1 (tearing the session down) if the
 * send failed. The store updates when the list comes back. */
int openmmo_client_add_friend(openmmo_client *c, const char *name);
int openmmo_client_remove_friend(openmmo_client *c, const char *name);

/* 1 if `name` is on the friends list this client holds, else 0. */
int openmmo_client_is_friend(const openmmo_client *c, const char *name);

/* Block `name` with the official client's default reason ("--"). No-op returning -1
 * unless IN_GAME and `name` is non-empty. */
int openmmo_client_block(openmmo_client *c, const char *name);

/* Ask `name` to trade (c2s 0x51). No-op returning -1 unless IN_GAME
 * and `name` is non-empty. The server may not answer yet. */
int openmmo_client_trade_request(openmmo_client *c, const char *name);

/* Send a whisper (chat mode 4) to `name`. No-op returning -1 unless
 * IN_GAME and `text` is non-empty. An empty `name` is sent as-is: the
 * server answers it with its own "Whisper who?" notice, which is the
 * feedback the chat box shows for a reply with nobody to go to. */
int openmmo_client_send_whisper(openmmo_client *c, const char *name,
                                const char *text);

/* The server-owned guild (see openmmo_guild). Never NULL;
 * `valid` is 0 until the first 0x80/0x81/0x83/0x84/0x85/0x86/0x88
 * arrives. The pointer is owned by the client and stays live until
 * the next start(). */
const openmmo_guild *openmmo_client_guild(const openmmo_client *c);

/*
 * Ask the server to found, leave, or disband a guild, invite or kick a member, assign a rank,
 * or set the motd. Create/invite need a non-empty name.
 */
int openmmo_client_guild_create(openmmo_client *c, const char *name,
                                const char *tag);
int openmmo_client_guild_invite(openmmo_client *c, const char *name);
int openmmo_client_guild_leave(openmmo_client *c);
int openmmo_client_guild_disband(openmmo_client *c);
int openmmo_client_guild_kick(openmmo_client *c, s64 member);
int openmmo_client_guild_rank(openmmo_client *c, s64 member, u8 rank);
int openmmo_client_guild_motd(openmmo_client *c, const char *text);
int openmmo_client_guild_log(openmmo_client *c, s16 page);

/* The server-owned mailbox (see openmmo_mail). Never NULL;
 * `valid` is 0 until the first 0x96/0x97/0x98/0x99 arrives. The
 * pointer is owned by the client and stays live until the next
 * start(). */
const openmmo_mail *openmmo_client_mail(const openmmo_client *c);

/*
 * Ask the server to send a letter, fetch a page, open one, or delete one. Compose needs a
 * recipient, a subject and a body.
 */
int openmmo_client_mail_send(openmmo_client *c, const char *recipient,
                             const char *subject, const char *body);
int openmmo_client_mail_page(openmmo_client *c, s16 page, int sent);
int openmmo_client_mail_read(openmmo_client *c, s64 mail_id);
int openmmo_client_mail_delete(openmmo_client *c, s64 mail_id, s16 page);

/* The server-owned link (see openmmo_link). Never NULL; `valid` is
 * 0 until the first 0xD0/0xD1/0xD2/0xDA arrives. The pointer is
 * owned by the client and stays live until the next start(). */
const openmmo_link *openmmo_client_link(const openmmo_client *c);

/* Invite a name, leave, kick an id, or hand the captain to an id.
 * No-op returning -1 unless IN_GAME; returns 0 on success, or -1
 * (tearing the session down) if the send failed. The store updates
 * when the server answers. */
int openmmo_client_link_invite(openmmo_client *c, const char *name);
int openmmo_client_link_leave(openmmo_client *c);
int openmmo_client_link_kick(openmmo_client *c, s64 member);
int openmmo_client_link_captain(openmmo_client *c, s64 member);

/* The last server-driven UI outside dialog (see openmmo_ui). Never
 * NULL; `valid` is 0 until the first 0x58/0x59/0x5A/0x5B/0x5C/
 * 0xD6/0xD8/0xD9/0xF1 arrives. The pointer is owned by the client
 * and stays live until the next start(). */
const openmmo_ui *openmmo_client_ui(const openmmo_client *c);

/* The last digest / transfer / stream / image (see openmmo_sync).
 * Never NULL for a live client; valid is 0 until one of those
 * packets has arrived. */
const openmmo_sync *openmmo_client_sync(const openmmo_client *c);

/* The last rental offer, tournament page, entry count, bracket and score
 * board (see openmmo_compete). Never NULL for a live client. */
const openmmo_compete *openmmo_client_compete(const openmmo_client *c);

/* The last staff lookup, panel body and panel row the server pushed (see
 * openmmo_gm). Never NULL for a live client. */
const openmmo_gm *openmmo_client_gm(const openmmo_client *c);

/* Send c2s 0xA2 as the note-add action: an id and a note. Requires a live
 * in-game session; -1 otherwise. */
int openmmo_client_admin_note_add(openmmo_client *c, s64 target,
                                  const char *text);

/* Send c2s 0xA2 as the note-delete action: the account id and the note's. */
int openmmo_client_admin_note_delete(openmmo_client *c, s64 target,
                                     s64 note_id);

/* Send c2s 0xA9: confirm a moderation action on one id. */
int openmmo_client_moderation_confirm(openmmo_client *c, s64 target);

/* The queue: enter (c2s 0x49), leave (0x47), cancel the search (0x6B). */
int openmmo_client_queue_join(openmmo_client *c);
int openmmo_client_queue_leave(openmmo_client *c);
int openmmo_client_queue_cancel(openmmo_client *c);

/* A queue action (c2s 0x4C). The three the wire byte distinguishes: pick a
 * tier, name a target and one of its slots, or ask for the teleport. */
int openmmo_client_queue_tier(openmmo_client *c, s32 tier_id);
int openmmo_client_queue_target(openmmo_client *c, s64 target, s16 slot);
int openmmo_client_queue_teleport(openmmo_client *c);

/* The languages this session will be matched in (c2s 0x6C). */
int openmmo_client_queue_langs(openmmo_client *c, const s8 *langs, int count);

/* The battle tier for one party slot (c2s 0x4A). */
int openmmo_client_tier_select(openmmo_client *c, s8 slot, s8 tier);

/* Sign up (c2s 0x48), in the two shapes the wire has. `queues` names the
 * queues and `slots` the party each is entered with, and there has to be at
 * least one: the leading byte is the discriminator as well as the count, so an
 * empty list reads as the tournament form. Withdrawing is `queue_leave`. */
int openmmo_client_queue_signup(openmmo_client *c, const s8 *queues,
                                const s8 *slots, int count);
int openmmo_client_tourney_signup(openmmo_client *c, s64 tourney_id, s8 slot);

/* Tournaments: register or withdraw (c2s 0xC1), ask for a page (0x75),
 * accept the teleport into the venue (0x44). */
int openmmo_client_tourney_register(openmmo_client *c, int register_);
int openmmo_client_tourney_view(openmmo_client *c, s8 tourney_id, int active,
                                s16 tab);
int openmmo_client_tourney_teleport(openmmo_client *c);

/* Boards: one high-score category (c2s 0x74), the co-op board (0x7B). */
int openmmo_client_score_board(openmmo_client *c, s8 category);
int openmmo_client_coop_board(openmmo_client *c);

/* 1 while a script lock or an unanswered box is held. A step or face
 * sent in that window is dropped: the server would reset it, and the
 * leak is the thing this exists to stop. */
int openmmo_client_in_dialog(const openmmo_client *c);

/*
 * Advance or answer the open box. `response` is 0 for a message advance, 1 for yes and 0 for
 * no, and a 1-based index for a menu (cancel is count+1).
 */
int openmmo_client_reply_dialog(openmmo_client *c, u8 response);

/* Press A on the tile the player faces. The body is empty: the server
 * uses the last reported position and facing. No-op returning -1 unless
 * IN_GAME. */
int openmmo_client_interact_tile(openmmo_client *c);

/* Press A on a server entity. `token` is 0 when this client has no
 * official hash for that entity. No-op returning -1 unless IN_GAME. */
int openmmo_client_interact_entity(openmmo_client *c, s64 entity_id,
                                   s64 token);

/* Ask the open clerk for `quantity` of wire item `item_id`. The server
 * decides; the bag and cash it sends back are what the client then holds.
 * No-op returning -1 unless IN_GAME; returns 0 on success, or -1 (tearing
 * the session down) if the send failed. */
int openmmo_client_shop_buy(openmmo_client *c, s16 item_id, s16 quantity);

/* Offer `quantity` of the bag stack `item_entity_id` (the object id the
 * bag packet gave that stack). Same return as buy. */
int openmmo_client_shop_sell(openmmo_client *c, s64 item_entity_id,
                             s16 quantity);

/* Use wire item `item_id` on monster `target_entity_id` (the id the party
 * packet gave that member). The server applies the effect; the bag and
 * party it sends back are what the client then holds. No-op returning -1
 * unless IN_GAME. */
int openmmo_client_use_item(openmmo_client *c, u16 item_id,
                            s64 target_entity_id);

/* The three containers a move can name. They are the wire's own container bytes. */
#define OPENMMO_CONTAINER_PC    0
#define OPENMMO_CONTAINER_PARTY 1
#define OPENMMO_CONTAINER_DAYCARE 3

/*
 * Move one monster from a container slot onto another: a deposit, a withdrawal, or a reorder,
 * which are one gesture on the wire. A destination that is already occupied swaps with the
 * source rather than overwriting it.
 */
int openmmo_client_move_pokemon(openmmo_client *c, int from_container,
                                int from_slot, int to_container, int to_slot);

/* The same gesture, several pairs at once. */
int openmmo_client_move_pokemon_batch(openmmo_client *c,
                                      const mmo_pokemon_move *moves, int n);

/* --- the battle scene ------------------------------------------------------ */

/*
 * Where the session stands between the overworld and a battle. The server owns every
 * transition: the client never decides that a battle has begun, and never decides that one is
 * over.
 */
typedef enum {
    OPENMMO_BATTLE_NONE = 0,  /* in the overworld */
    OPENMMO_BATTLE_ENTERING,  /* moved into a battle; no field state yet */
    OPENMMO_BATTLE_ACTIVE,    /* the field state has arrived */
} openmmo_battle_state;

/* Where the session stands. Reads the state above; never NULL-checks a session
 * into a battle it is not in. */
openmmo_battle_state openmmo_client_battle_state(const openmmo_client *c);

/*
 * The live field the event stream has built. `valid` is 0 until a 0x30 has opened a fight this
 * session; the rows are the entities the stream has named (a delta, a move target, a switch-
 * in).
 */
#define OPENMMO_BATTLE_MONS 12

typedef struct {
    int valid;
    int n_events;                     /* stream packets consumed this fight */
    int prompt;                       /* 1 = an action prompt is outstanding */
    int ended;                        /* 1 = the bulk state has closed it */
    int prize;
    int switch_owed;                  /* 1 = 0x36 asked for a replacement */
    int caught;                       /* 1 = 0x37 named a ball throw */
    int caught_item;                  /* server item id on that throw; 0 if none */
    u32 caught_id;                    /* monster the 0x14 named; 0 until then */
    int caught_species;               /* server dex id from that record */
    int xp_gained;                    /* 0x79 base counter; 0 until a reward */
    int leveled;                      /* 1 = a 0x16 this fight carried stats */
    int n_mons;
    struct {
        u32 entity_id;
        int hp;                       /* -1 until a packet names it */
        int max_hp;                   /* -1 until a full switch-in names it */
        int faint;                    /* -1 until named; 1 = fainted */
        int species;
        int level;
        int xp;
        int have_stats;
        s16 stats[6];
    } mon[OPENMMO_BATTLE_MONS];
} openmmo_battle_field;

const openmmo_battle_field *openmmo_client_battle_field(const openmmo_client *c);

/*
 * The command sequence the last 0x33 mapped onto. `n` is 0 until a move event this fight has
 * been mapped; the pointer is owned by the client and stays live until the next start() or the
 * next fight.
 */
typedef struct {
    int n;
    mmo_battle_anim cmd[MMO_BATTLE_ANIM_MAX];
} openmmo_battle_anims;

const openmmo_battle_anims *openmmo_client_battle_anims(const openmmo_client *c);

/*
 * Send a turn intent. This is not an outcome: the server decides what happens, and the client
 * learns the answer from the event stream that follows.
 */
int openmmo_client_battle_select(openmmo_client *c, const mmo_battle_select *sel);

/* Ask to run away: slot 0, kind run, no tail. Same contract as
 * openmmo_client_battle_select. */
int openmmo_client_battle_run(openmmo_client *c);

/* --- the duel, and the native link battle ---------------------------------- * */

/* The challenge the server is waiting on, or NULL when it is waiting on
 * nothing. Only one is ever open. The pointer is owned by the client and stays
 * live until the next start(). */
const mmo_duel_invite *openmmo_client_duel_pending(const openmmo_client *c);

/* Answer the open challenge. Accepting opens a link battle (an
 * OPENMMO_EV_LINK_BATTLE follows); declining ends it. No-op returning -1 unless
 * the session is IN_GAME with a challenge open; returns 0 on success, or -1
 * (tearing the session down) if the send failed. */
int openmmo_client_reply_duel(openmmo_client *c, int accepted);

/* The link battle this client is seated in. `valid` is 0 when there is none.
 * `party` is the opponent's, as the server holds it; this client's own is
 * openmmo_client_party() as always. Both clients are given the same two
 * records, so both build the same two parties. */
typedef struct {
    int valid;
    int battle_id;
    int net_id;                   /* 0 computes the fight, 1 presents it */
    char peer_name[OPENMMO_ENTITY_NAME_MAX];
    int peer_gender;              /* 0 male, 1 female */
    /*
     * The object-event graphics id the opponent walks as, resolved from the cosmetic slots the
     * seat carries (appearance.h).
     */
    int peer_body_gfx;
    openmmo_party party;          /* the opponent's */
} openmmo_link_battle;

const openmmo_link_battle *openmmo_client_link_battle(const openmmo_client *c);

/* Send one blob of the running link battle. `kind` is MMO_LINK_KIND_*; the
 * payload is the engine's own and crosses verbatim. A RESULT or a LEAVE also
 * closes the seat on this side. No-op returning -1 without a seated battle, a
 * length past MMO_LINK_BLOB_MAX, or a session that is not IN_GAME. */
int openmmo_client_link_send(openmmo_client *c, int kind,
                             const void *data, int len);

/*
 * Take the next blob the peer sent, oldest first. Returns 1 and fills `out`, or 0 when the
 * queue is empty.
 */
int openmmo_client_link_recv(openmmo_client *c, mmo_link_battle_data *out);

/* How many blobs the queue holds. */
#define OPENMMO_LINK_QUEUE 64

/* Blobs the queue could not hold since the seat opened. Non-zero means the
 * fight this client is presenting is not the fight the other side computed. */
int openmmo_client_link_dropped(const openmmo_client *c);

/* --- the link Super Contest ------------------------------------------------ * */
#define OPENMMO_CONTEST_SEATS 4

/* Blobs held for the engine to drain. Deeper than the link battle's, because a
 * contest broadcasts from up to four senders at once and the engine takes them
 * a frame at a time. */
#define OPENMMO_CONTEST_QUEUE 64

typedef struct {
    int valid;            /* 1 while seated in a contest */
    int queued;           /* 1 while waiting for one to fill */
    int queued_have;      /* players in this queue so far */
    int queued_want;      /* how many it will start with at most */
    int session_id;
    int seat;             /* this client's own net id, 0..OPENMMO_CONTEST_SEATS-1 */
    int humans;           /* human seats; the engine fills the rest */
    int rank;
    int type;
    int party_slot;       /* the slot this client entered, as asked for */
    struct {
        char name[OPENMMO_ENTITY_NAME_MAX];
        int gender;       /* 0 male, 1 female */
        int story_cleared;
        int national_dex;
        int gone;         /* left before the contest ended */
    } contestant[OPENMMO_CONTEST_SEATS];
} openmmo_contest;

/* The contest this client is queued for or seated in. Never NULL; `valid` and
 * `queued` say which, and both are 0 when there is neither. */
const openmmo_contest *openmmo_client_contest(const openmmo_client *c);

/* Ask for a link contest at this rank and type, entering the party member in
 * `party_slot`. The answer is OPENMMO_EV_CONTEST, once with `queued` while the
 * group fills and again with `valid` when it starts. No-op returning -1 unless
 * IN_GAME. */
int openmmo_client_contest_queue(openmmo_client *c, int rank, int type,
                                 int party_slot);

/* Leave the queue. Does not leave a contest that has already started; that is
 * openmmo_client_contest_leave(). */
int openmmo_client_contest_cancel(openmmo_client *c);

/* Leave a contest that has started, telling the others. */
int openmmo_client_contest_leave(openmmo_client *c);

/* Broadcast one of the contest's own comm commands (MMO_CONTEST_KIND_DATA, with
 * the command id as the first payload byte) or a barrier (MMO_CONTEST_KIND_SYNC,
 * one byte). The payload crosses verbatim and this client does not read it. */
int openmmo_client_contest_send(openmmo_client *c, int kind,
                                const void *data, int len);

/* Take the next blob another contestant sent, oldest first. Returns 1 and fills
 * `out`, whose `seat` is the sender, or 0 when the queue is empty. Queued
 * rather than raised as events for the same reason a link battle's blobs are:
 * the engine drains them on its own clock. */
int openmmo_client_contest_recv(openmmo_client *c, mmo_contest_comm *out);

/* Blobs the queue could not hold since the contest was seated. Non-zero means
 * this client is no longer running the same contest as the others. */
int openmmo_client_contest_dropped(const openmmo_client *c);

/* Report the placement this client computed: one byte per human seat, in seat
 * order, 0 for the winner. Every client runs the whole contest from the same
 * relayed inputs, so the server takes the vector only when all of them agree
 * and records nothing when they do not. */
int openmmo_client_contest_result(openmmo_client *c, const u8 *placement, int n);

/* --- the direct trade ------------------------------------------------------ * */
typedef struct {
    int open;                  /* 1 while the table is open */
    int last_state;            /* the last s2c 0x6A state, MMO_TRADE_STATE_* */
    int role;                  /* this side's net id at the table, 0 or 1 */
    int peer_gender;           /* 0 male, 1 female: the sprite the peer wears */
    char peer[OPENMMO_ENTITY_NAME_MAX];
    int self_slot;             /* the party slot this side offered, -1 none */
    int self_confirmed;        /* this side locked the standing pair */
    int peer_confirmed;        /* the peer locked it (cleared by any re-pick) */
    int have_peer_mon;
    openmmo_party_mon peer_mon; /* the peer's offer, id-mapped like the party */
} openmmo_trade;

/* The trade table this client holds. Never NULL; `open` is 0 outside a trade.
 * The pointer is owned by the client and stays live until the next start(). */
const openmmo_trade *openmmo_client_trade(const openmmo_client *c);

/* One trade action (c2s 0x50): MMO_TRADE_ACTION_ACCEPT answers a standing
 * offer, _CONFIRM locks the pair as it stands, _CANCEL walks away from either.
 * No-op returning -1 unless IN_GAME; returns 0 on success, or -1 (tearing the
 * session down) if the send failed. */
int openmmo_client_trade_action(openmmo_client *c, int action);

/* Offer the party monster in `slot` (c2s 0x52). Re-picking is allowed until
 * both sides confirm, and any re-pick clears both confirmations. No-op
 * returning -1 unless IN_GAME with the table open and 0 <= slot < 6. */
int openmmo_client_trade_select(openmmo_client *c, int slot);

/*
 * The engine trade scene's own traffic (bidi 0xBF): send one message to the other chair, and
 * take the next one the server relayed, oldest first. The bytes cross unread, their meaning
 * belongs to the two engines, exactly as a link battle's blobs do.
 */
#define OPENMMO_TRADE_COMM_QUEUE 64

int openmmo_client_trade_comm_send(openmmo_client *c, int channel, int cmd,
                                   const void *data, int len);
int openmmo_client_trade_comm_recv(openmmo_client *c, mmo_trade_comm *out);
int openmmo_client_trade_comm_dropped(const openmmo_client *c);

/* --- the global trade link ------------------------------------------------- * */
typedef struct {
    s64 listing_id;
    int kind;                 /* MMO_GTL_KIND_POKEMON or _ITEM */
    s32 price;
    s32 listed_at;            /* unix seconds */
    s32 expires_at;           /* unix seconds */
    int quantity;
    u16 item_id;              /* item rows: the server's item id space */
    int have_mon;
    openmmo_party_mon mon;    /* monster rows, id-mapped like the party */
    s16 stats[6];             /* hp, atk, def, spd, spAtk, spDef */
    int own_state;            /* own pages: MMO_GTL_ST_* */
    int own_remaining;        /* own pages: units still up */
    int own_unclaimed;        /* own pages: sold units awaiting Claim */
} openmmo_gtl_row;

typedef struct {
    int session_open;         /* 1 once a 0xDC has arrived */
    s64 session_ts;           /* its timestamp, echoed on a reopen */
    int request_id;           /* the page below answers this request */
    int kind;                 /* MMO_GTL_KIND_* of the held page */
    int page;
    s32 total;                /* matches behind the held page */
    int count;
    openmmo_gtl_row row[MMO_GTL_PAGE_ROWS];
    int quote_count;          /* item pages: the cheapest ask per item */
    struct {
        u16 item_id;
        s32 price;
    } quote[MMO_GTL_PAGE_ROWS];
    int log_rows;             /* the last trade log's row count, -1 before one */
    mmo_gtl_log log;          /* the last trade log's rows */
    mmo_gtl_result result;    /* the last 0xAF answer */
    int result_seq;           /* bumps on every 0xAF; 0 before the first */
    int next_request;         /* the request id counter the searches spend */
} openmmo_gtl;

/* The shelf as this client last saw it. Never NULL; `session_open` is 0 until
 * a 0xDC arrives. The pointer is owned by the client and stays live until the
 * next start(). */
const openmmo_gtl *openmmo_client_gtl(const openmmo_client *c);

/* Open (or reopen) a trade link session (c2s 0xA5). The 0xDC that answers
 * fires OPENMMO_EV_GTL and also pays out anything this character sold while
 * away. No-op returning -1 unless IN_GAME. */
int openmmo_client_gtl_open(openmmo_client *c);

/* Ask for one page (c2s 0x9B). `kind` is MMO_GTL_KIND_*, `sort` is
 * MMO_GTL_SORT_*; `filter` narrows a monster search and may be NULL. The
 * species in a filter is the server's own id space, map an engine species
 * with mmo_id_species_to_server first. No-op returning -1 unless IN_GAME. */
int openmmo_client_gtl_search(openmmo_client *c, int kind, int sort, int page,
                              const mmo_gtl_search *filter);

/* Put a party or PC monster up by its id (c2s 0x9A). The monster leaves the
 * party or box when the server accepts, the resend says so. No-op returning
 * -1 unless IN_GAME or when price is outside 1..999999. */
int openmmo_client_gtl_list_mon(openmmo_client *c, s64 mon_id, s32 price);

/* Put an item stack up (c2s 0x9A). `item_id` is the server's id space. */
int openmmo_client_gtl_list_item(openmmo_client *c, u16 item_id, s32 quantity,
                                 s32 price);

/* Take an own listing back (c2s 0xA1). A listing with unclaimed money on it
 * refuses with MMO_GTL_R_UNSETTLED, claim first. */
int openmmo_client_gtl_cancel(openmmo_client *c, s64 listing_id);

/* Buy `quantity` units of a listing (c2s 0x9C). A monster is one unit. */
int openmmo_client_gtl_buy(openmmo_client *c, s64 listing_id, int quantity);

/* Claim the settled money on the named listings (c2s 0x54). The 0xAF that
 * answers carries the total collected. */
int openmmo_client_gtl_claim(openmmo_client *c, const s64 *listing_ids,
                             int count);

/* Lower a standing listing's price (c2s 0x55). Raising is refused, and a
 * second change inside the server's cooldown answers PRICE_COOLDOWN. */
int openmmo_client_gtl_price(openmmo_client *c, s64 listing_id, s32 new_price);

/* Buy `quantity` of an item across listings, cheapest first (c2s 0xA3).
 * `budget` caps the whole spend; the 0xAF answer carries what was filled. */
int openmmo_client_gtl_market(openmmo_client *c, u16 item_id, int quantity,
                              s32 budget);

/* Ask for the trade log (c2s 0x9F). The 0x5E that answers fires
 * OPENMMO_EV_GTL and sets `log_rows`. */
int openmmo_client_gtl_log(openmmo_client *c);

/* --- the Underground, and talking to someone in it ------------------------- * */

/* One conversation's traffic, queued because the cavern's menus run on an
 * engine frame rather than on the event queue's clock. Deep enough for a whole
 * trainer-case exchange and the state changes either side of it. */
#define OPENMMO_UG_TALK_QUEUE 32

/*
 * Send one underground-talk message. `kind` is MMO_UG_TALK_*; `entity_id` is the player it is
 * about (0 where the kind names nobody) and `data` its bytes.
 */
int openmmo_client_ug_talk_send(openmmo_client *c, int kind, s64 entity_id,
                                const void *data, int len);

/* Take the next underground-talk message the server sent, oldest first.
 * Returns 1 and fills `out`, or 0 when the queue is empty. */
int openmmo_client_ug_talk_recv(openmmo_client *c, mmo_underground_talk *out);

/* Messages the queue could not hold since the session started. Non-zero means a
 * conversation this client is in has lost something the other side said. */
int openmmo_client_ug_talk_dropped(const openmmo_client *c);

/*
 * The server entity standing on (x, z) of the map this client is on, or 0 when nobody is. The
 * id is the one the spawn packet carried, which is the peer's character id, the same value
 * openmmo_client_ug_talk_send() names a player by.
 */
u32 openmmo_client_entity_at(const openmmo_client *c, int x, int z);

/* The live entity with this id, as a SPAWN-shaped event (the same shape
 * openmmo_client_live_entities fills): its rendered tile, its facing and the
 * appearance the spawn packet carried. Returns 1 and fills `out`, or 0 when
 * nothing on this map has that id. */
int openmmo_client_entity_by_id(const openmmo_client *c, u32 id,
                                openmmo_event *out);

/* --- move learn ------------------------------------------------------------ */

/* The slot an offer carries when the moveset was full, and the slot an answer
 * carries to keep the moveset as it is. */
#define OPENMMO_MOVE_LEARN_NO_SLOT (-1)

/* Move slots a monster has. */
#define OPENMMO_MOVE_SLOTS 4

/* The offer the server is waiting on, or NULL when it is waiting on nothing. The
 * fields are the OPENMMO_EV_MOVE_LEARN ones. Only one offer is ever open: the
 * server sends the next move a level up taught only once this one is answered.
 * The pointer is owned by the client and stays live until the next start(). */
const openmmo_event *openmmo_client_move_learn_pending(const openmmo_client *c);

/*
 * Answer the open offer. `slot` is 0..OPENMMO_MOVE_SLOTS-1, the move slot the new move takes
 * over, or OPENMMO_MOVE_LEARN_NO_SLOT to turn it down and keep the moves the monster has.
 */
int openmmo_client_reply_move_learn(openmmo_client *c, int slot);

/* --- evolution ------------------------------------------------------------- */

/* The evolution the server is waiting on, or NULL when it is waiting on nothing.
 * The fields are the OPENMMO_EV_EVOLUTION ones. Only one is ever open, a second
 * prompt replaces it, the way the game client's screen does. The pointer is owned
 * by the client and stays live until the next start(). */
const openmmo_event *openmmo_client_evolution_pending(const openmmo_client *c);

/*
 * Answer the open evolution: `accept` non-zero once the evolution has played through, or 0 to
 * stop it. Stopping one the server marked as not cancelable is refused here rather than sent.
 */
int openmmo_client_reply_evolution(openmmo_client *c, int accept);

/* --- breeding and eggs ----------------------------------------------------- * */

/* Incubator slots the client holds at once (the game client's own cap: a
 * permanent one plus seven temporary). */
#define OPENMMO_INCUBATOR_SLOTS_MAX 8

/* Uses a temporary incubator has before it breaks, the denominator its wear bar
 * is drawn against. */
#define OPENMMO_INCUBATOR_USES_FULL 500

typedef struct {
    s64 id;         /* carried verbatim: the game client reads and drops it */
    int uses_left;  /* out of OPENMMO_INCUBATOR_USES_FULL */
} openmmo_incubator_slot;

typedef struct {
    int valid;      /* 1 once the server has sent the slots */
    int count;      /* slots held (<= OPENMMO_INCUBATOR_SLOTS_MAX) */
    int total;      /* slots the server sent */
    openmmo_incubator_slot slot[OPENMMO_INCUBATOR_SLOTS_MAX];
} openmmo_incubators;

/* The incubator slots the server last sent, never NULL. `valid` is 0 until one
 * has arrived. Owned by the client; live until the next start(). */
const openmmo_incubators *openmmo_client_incubators(const openmmo_client *c);

/* The gender a pairing is asked for. ANY is what it starts at and what it falls
 * back to when the server says the gender is not selectable. */
#define OPENMMO_BREED_GENDER_ANY    (-1)
#define OPENMMO_BREED_GENDER_FIRST  0
#define OPENMMO_BREED_GENDER_SECOND 1

/* How a forecast move would come to be known. */
#define OPENMMO_BREED_MOVE_LEVEL_UP 0
#define OPENMMO_BREED_MOVE_EARLY    1
#define OPENMMO_BREED_MOVE_PARENT   2
#define OPENMMO_BREED_MOVE_EGG      3
#define OPENMMO_BREED_MOVE_ITEM     4

/* Who the egg's original trainer would be: the player, the player but not for
 * the purposes of an "own ot" count, or someone else. */
#define OPENMMO_BREED_OT_SELF          0
#define OPENMMO_BREED_OT_SELF_UNMARKED 1
#define OPENMMO_BREED_OT_UNKNOWN       2

#define OPENMMO_BREED_STATS      6
#define OPENMMO_BREED_OUTCOMES   8
#define OPENMMO_BREED_MOVES      4
#define OPENMMO_BREED_SHININESS  8

typedef struct {
    int value;    /* the IV this outcome gives, 0..31 */
    float chance; /* its share, as a percentage */
    s32 label;    /* a game-client string id naming the pass, or <= 0 for none */
} openmmo_breed_outcome;

typedef struct {
    int guaranteed;  /* the IV is fixed, not rolled */
    int item_id;     /* the held item that fixed it, 0 for none */
    int outcome_count;
    openmmo_breed_outcome outcome[OPENMMO_BREED_OUTCOMES];
} openmmo_breed_stat;

/*
 * What the server says a pairing would produce. Species and moves are the server's ids with
 * the engine's beside them, the way the party carries both; `stat` is indexed hp, atk, def,
 * speed, spAtk, spDef, the same order as a party member's IVs.
 */
typedef struct {
    int valid;                  /* 1 once a forecast has arrived */
    s64 parent[2];
    int has_preview;
    int species;                /* server National Dex id, as sent */
    u16 engine_species;         /* the same through the id map; 0 = untranslatable */
    int form;
    int stat_count;
    openmmo_breed_stat stat[OPENMMO_BREED_STATS];
    int shininess_count;
    int shininess[OPENMMO_BREED_SHININESS]; /* rarity kinds, each equally likely */
    int move_count;
    int move_id[OPENMMO_BREED_MOVES];       /* server move ids */
    u16 move[OPENMMO_BREED_MOVES];          /* engine move ids; 0 = untranslatable */
    int move_source[OPENMMO_BREED_MOVES];   /* OPENMMO_BREED_MOVE_* */
    int ot;                     /* OPENMMO_BREED_OT_* */
    int nature;                 /* 0..24, engine nature ids (identical numbering) */
    int gender_selectable;      /* the gender buttons apply to this pairing */
    s32 gender_cost[2];         /* what FIRST and SECOND cost; ANY is free */
} openmmo_breeding_forecast;

/* The last forecast the server sent, never NULL. `valid` is 0 until one has
 * arrived. Owned by the client; live until the next start(). */
const openmmo_breeding_forecast *
openmmo_client_breeding_forecast(const openmmo_client *c);

/*
 * Ask what two parents would produce, for `gender` (one of the three above). The game client
 * sends this when the second parent is put down and again on every gender button.
 */
int openmmo_client_breeding_preview(openmmo_client *c, s64 own_id,
                                    s64 partner_id, int gender);

/*
 * Commit the pairing the forecast describes. `session` is the breeding session's own byte, and
 * `item_key` is the key of the breeding item selected, or OPENMMO_BREED_NO_ITEM for none.
 */
#define OPENMMO_BREED_NO_ITEM (-1)
int openmmo_client_breeding_submit(openmmo_client *c, int session, s64 own_id,
                                   s64 partner_id, int gender, int item_key);

/*
 * Report what a local script just wrote. `flags`, `vars` and `blocks` are the changes only,
 * an empty set sends nothing and returns 0.
 */
int openmmo_client_send_script_state(openmmo_client *c,
                                     const mmo_script_flag *flags, int nflags,
                                     const mmo_script_var *vars, int nvars,
                                     const mmo_save_block *blocks, int nblocks);

/* Offer a whole save file to the server, as this character. */
int openmmo_client_send_offline_report(openmmo_client *c,
                                       const u8 *report, size_t len);

/* Offer the session records behind that save, as the evidence for it. */
int openmmo_client_send_offline_chain(openmmo_client *c,
                                      const u8 *chain, size_t len);

/* Offer the image a session just wrote out for offline play, encoded as
 * offline_chain.h's offline-copy blob, in the same numbered pieces. The
 * answer lands in the same place as the other two. */
int openmmo_client_send_offline_export(openmmo_client *c,
                                       const u8 *blob, size_t len);

/* What the server said about the last save this client offered. */
#define MMO_IMPORT_STATUS_NONE      (-1)
#define MMO_IMPORT_STATUS_LANDED    0
#define MMO_IMPORT_STATUS_TRY_AGAIN 1
#define MMO_IMPORT_STATUS_REFUSED   2
/* The two answers to a session chain rather than to a save: what the server
 * did with the evidence offered for the import that just landed. Neither says
 * anything about the save, which has already landed either way. */
#define MMO_IMPORT_STATUS_CHECK_QUEUED   3
#define MMO_IMPORT_STATUS_CHECK_DECLINED 4
/* The two answers to the offline copy a session sends as it leaves: whether
 * the server kept the image, to check the offline play that follows against.
 * The copy on the player's disk is theirs either way. */
#define MMO_IMPORT_STATUS_EXPORT_KEPT     5
#define MMO_IMPORT_STATUS_EXPORT_DECLINED 6
/* The session records behind the save are on file; nothing is replayed until
 * the player asks for a monster, or for everything, and pays for the stretch. */
#define MMO_IMPORT_STATUS_CHAIN_KEPT      7
/* One past the last status. An answer at or past this is one this build has
 * no word for and reads as no answer at all; grow it with the list above, and
 * the table of words in openmmo_import.c grows with it by construction. */
#define MMO_IMPORT_STATUS_COUNT 8

#define MMO_IMPORT_NOTE_MAX  32
#define MMO_IMPORT_TEXT_MAX  256

typedef struct {
    int  status;
    char message[MMO_IMPORT_TEXT_MAX];
    char notes[MMO_IMPORT_NOTE_MAX][MMO_IMPORT_TEXT_MAX];
    int  nnotes;
    /* How many notes the server sent, which may be more than were kept. */
    int  nnotes_sent;
    /* Set on a landed save when this server would look at the session records
     * behind it. Nothing is owed either way: a server that is not checking is
     * one where the import simply stays marked, so a client that sends nothing
     * loses nothing but the chance to have the mark lifted. */
    int  wants_chain;
} openmmo_import_answer;

/* The last answer, or NULL before one has arrived. */
const openmmo_import_answer *openmmo_client_import_answer(const openmmo_client *c);

/*
 * Report that a local script (or a local door) has already changed map. `header` is the engine
 * map header id, `dir` an engine DIR_*.
 */
int openmmo_client_send_script_warp(openmmo_client *c, int header, int x, int z,
                                    int dir);

/*
 * Report that a local script gave, or a ball caught, the player a monster. `dex` is the
 * server's dex id, `level` the level it arrived at, and the nickname (UTF-16LE, `nick_bytes`
 * bytes, may be NULL/0 for the species name) is the name its trainer typed.
 */
int openmmo_client_send_script_grant(openmmo_client *c, int dex, int level,
                                     int hp, int container, int slot,
                                     u32 seed, u32 iv_bits, int shiny,
                                     const uint8_t *nick_utf16le,
                                     size_t nick_bytes);

/* Report that the box screen let the monster with this server id go. The
 * record follows, or the next seat resurrects it. Same contract as above. */
int openmmo_client_send_pokemon_release(openmmo_client *c, s64 id);

/* Report what an engine-run battle left each party member as (see
 * mmo_battle_mon_outcome in game.h). The server writes the values onto the
 * monsters it stores, keyed by their server ids. Same contract as above. */
int openmmo_client_send_battle_outcome(openmmo_client *c,
                                       const mmo_battle_mon_outcome *mons,
                                       int n);

/* Report that the engine's own arithmetic consumed (delta < 0) or gained
 * (delta > 0) `delta` of the wire item id `item`. The server moves its bag
 * and answers with the stack, which is what the display bag then shows.
 * Same contract as the warp report above. */
int openmmo_client_send_bag_delta(openmmo_client *c, int item, int delta);

/* Report that the engine's own arithmetic spent (delta < 0) or earned
 * (delta > 0) cash. The server moves its record and answers with the money
 * line the display then shows. Same contract as above. */
int openmmo_client_send_money_delta(openmmo_client *c, int delta);

/* Report that the bag screen registered the wire item id `item` to Y, or
 * cleared it (item 0). A statement like the bag delta above: the engine's
 * own register already moved, the server's part is to record it. */
int openmmo_client_send_registered_item(openmmo_client *c, int item);

/* 1 if the story flag `flag_id` (a region-scoped GBA id) is set, else 0. */
int openmmo_story_flag_is_set(const openmmo_story_store *s, int flag_id);

/* Read the story variable `var_id`, the full GBA id (0x4000..0x40ff) or its
 * 0..255 offset. Returns 1 and writes *out if present, else 0 (out untouched). */
int openmmo_story_var_get(const openmmo_story_store *s, int var_id, s16 *out);

/* Read bit `bit` (0-based) of world-flag `group`. Returns 0 or 1, or -1 if the
 * group or bit is out of the range the store holds. */
int openmmo_world_flag_bit(const openmmo_story_store *s, int group, int bit);

/* Drop every tracked remote entity at once. */
/* The account's character list, once a HOLD join has received it or a
 * create has come back. Never NULL; `held` is 0 until then. The pointer
 * is owned by the client and stays live until the next start(). */
const mmo_character_list *openmmo_client_characters(const openmmo_client *c);

/* 1 while a HOLD join is waiting for the host to pick or create. */
int openmmo_client_picking(const openmmo_client *c);

/* Pick one row of the held list and continue the join. No-op returning
 * -1 unless picking, or on an index outside the held rows. */
int openmmo_client_pick_character(openmmo_client *c, int index);

/* Object-event graphics id for the seated character: a catalog body if
 * the list row kept one, otherwise the gender trainer. */
int openmmo_client_body_gfx(const openmmo_client *c);

/*
 * The last measured round trip to the game server, in milliseconds, or -1 when nothing has
 * been measured yet (no world session, or the first ping still out).
 */
int openmmo_client_latency_ms(const openmmo_client *c);

/*
 * What the server says the local player is riding, as MMO_TRANSPORT_* bits (game.h).
 * MMO_TRANSPORT_NONE until a spawn or an EntityTransportation says otherwise, and only the
 * surfing bit is ever set today.
 */
int openmmo_client_transportation(const openmmo_client *c);

/* Submit a CreateCharacter from the creator screens. The server answers
 * with a fresh list; a count that does not move is a refusal and the
 * session stays on the picker (openmmo_client_create_error names why).
 * No-op returning -1 unless picking. */
int openmmo_client_create_character(openmmo_client *c,
                                    const mmo_create_character *in);

/* The last create refusal, or NULL. */
const char *openmmo_client_create_error(const openmmo_client *c);

/* Delete one of this account's characters, by the id the list carries. */
int openmmo_client_delete_character(openmmo_client *c, s64 character_id);

/* The last delete refusal, or NULL. */
const char *openmmo_client_delete_error(const openmmo_client *c);

void openmmo_client_reset_entities(openmmo_client *c);

/*
 * One SPAWN-shaped event per live remote entity, at the tile and appearance the model
 * currently holds. Does not drain the event queue or change the model.
 */
int openmmo_client_live_entities(const openmmo_client *c, openmmo_event *out, int max_out);

#define OPENMMO_CHAT_LOG 16

/* The last lines the server delivered, oldest first. Returns the count
 * written into out (at most min(max_out, OPENMMO_CHAT_LOG)). */
int openmmo_client_chat(const openmmo_client *c, openmmo_event *out, int max_out);

#endif /* MMO_CLIENT_H */
