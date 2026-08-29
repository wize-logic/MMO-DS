/* The game server's transport and its JoinPacket handshake (7777). */
#ifndef MMO_GAME_H
#define MMO_GAME_H

#include <stddef.h>

#include "mmo.h"
#include "codec.h"
#include "session.h"
#include "deflate.h"

/* Game-protocol opcodes (GameProtocol.kt). Join is 0x01 in both directions. */
#define MMO_GAME_OP_JOIN     0x01  /* c2s */
#define MMO_GAME_OP_JOIN_RSP 0x01  /* s2c */

/* The join-to-world sequence past JoinResponse (LoginService: onCharacterRequest,
 * onCharacterSelected, onRequestPlayer). Each c2s/s2c pair shares an opcode. */
#define MMO_GAME_OP_REQ_CHARS  0x02  /* c2s RequestCharacters (empty body) */
#define MMO_GAME_OP_CHARS_LIST 0x02  /* s2c CharactersList */
#define MMO_GAME_OP_CREATE_CHAR 0x03 /* c2s CreateCharacter */
#define MMO_GAME_OP_SELECT_CHAR 0x04 /* c2s SelectCharacter / s2c SelectedCharacter */
#define MMO_GAME_OP_REQ_PLAYER 0x05  /* c2s RequestPlayer (empty body) */

/*
 * Character deletion, and the two opcodes are not each other's reply: the c2s request is 0x64
 * and the answer comes back on 0x62 (GameProtocol.kt 283/288).
 */
#define MMO_GAME_OP_DELETE_CHAR   0x64 /* c2s DeleteCharacter: one S64LE id */
#define MMO_GAME_OP_DELETE_RESULT 0x62 /* s2c DeleteCharacterResult: U8 + S64LE */

/* DeleteCharacterResultPacket.result. 0 is the delete; anything else is the
 * server declining it, an unauthenticated session, one that has already
 * selected a character, or an id this account does not own. */
#define MMO_DELETE_OK      0
#define MMO_DELETE_REFUSED 1

#define MMO_GAME_OP_LOAD_MAP   0x10  /* s2c LoadMap; reloadPlayer=true -> ask for the player */
#define MMO_GAME_OP_RENDER     0xB4  /* s2c RenderScreen; true = standing in the map */

/* A server-driven warp (a door, staircase, ledge, arrow-warp or a script). */
#define MMO_GAME_OP_MAP_TRANSITION 0x1B /* s2c MapTransition: a warp is beginning */

/*
 * The world-state block the server streams after SelectCharacter (on select and on resync, 
 * WorldStateService.send).
 */
#define MMO_GAME_OP_WORLD_FLAG_RESET   0x0A /* s2c WorldFlagTableReset: the flag table */
#define MMO_GAME_OP_STORY_FLAG         0x2A /* s2c StoryFlagUpdate: one GBA flag */
#define MMO_GAME_OP_POKEMON_CONTAINER  0x13 /* bidi PokemonContainer (consumed; Phase 6) */
#define MMO_GAME_OP_ITEM_STACKS        0x40 /* s2c bag snapshot (BattleSideParty shape) */
#define MMO_GAME_OP_ITEM_STACK_UPDATE  0x42 /* s2c one bag stack (BattleSideAddPokemon shape) */
#define MMO_GAME_OP_LOCAL_PLAYER_STATE 0xF3 /* s2c LocalPlayerState */
#define MMO_GAME_OP_LOCAL_CHAR_DELTA   0x0C /* s2c LocalCharacterDelta: live cash */
#define MMO_GAME_OP_SHOP_CATALOG       0x23 /* s2c ShopCatalog: open or close the mart */
#define MMO_GAME_OP_SHOP_BUY           0x23 /* c2s ExchangeItemRequest */
#define MMO_GAME_OP_SHOP_SELL          0x24 /* c2s ShopSellRequest */
#define MMO_GAME_OP_ITEM_USE           0x26 /* c2s DialogOption: use an item on a monster */
#define MMO_GAME_OP_DIALOG_ACTION      0x21 /* s2c DialogAction: a box, or the close */
#define MMO_GAME_OP_DIALOG_REPLY       0x21 /* c2s DialogActionResponse: flags + response */
#define MMO_GAME_OP_ENTITY_INTERACT    0x22 /* c2s EntityInteract: talk to an npc */
#define MMO_GAME_OP_TILE_INTERACT      0x27 /* c2s TileInteract: talk to a sign or furniture */
#define MMO_GAME_OP_DIALOG_STATE       0x0E /* s2c DialogState: the script lock */
#define MMO_GAME_OP_SCRIPT_MOVE        0x0D /* s2c DialogData: a scripted movement sequence */

/*
 * The local-script block, 0xCB-0xCF. Every other opcode on this wire is the official client's, read off
 * the official client; these three are not, because the official client has no packet for them, its server runs the
 * cutscene and the client only ever replies to a box (settled §57).
 */
#define MMO_GAME_OP_SCRIPT_STATE       0xCB /* bidi ScriptState: the VM's flags and vars */
#define MMO_GAME_OP_SCRIPT_WARP        0xCC /* c2s ScriptWarpArrived: a local warp landed */
#define MMO_GAME_OP_SCRIPT_OWNER       0xCD /* c2s ClientScriptOwnership: who runs a field script */
#define MMO_GAME_OP_SCRIPT_GRANT       0xCE /* c2s ScriptGrantPokemon: a local script gave a mon */
#define MMO_GAME_OP_BATTLE_OUTCOME     0xCF /* c2s BattleOutcome: what a local battle did to the party */

/* The duel, and the native link battle an accepted one opens. */
#define MMO_GAME_OP_DUEL_INVITE        0x50 /* s2c DuelInvite: someone offered a battle */
#define MMO_GAME_OP_DUEL_RESPONSE      0x16 /* c2s InGameChallengeResponse: the answer */
#define MMO_GAME_OP_DUEL_OUTCOME       0x51 /* s2c DuelInviteOutcome: what the answer was */
#define MMO_GAME_OP_LINK_BATTLE_OPEN   0xC6 /* s2c LinkBattleOpen: seat a native link battle */
#define MMO_GAME_OP_LINK_BATTLE_DATA   0xC7 /* bidi LinkBattleData: one relayed link blob */
#define MMO_GAME_OP_UNDERGROUND_TALK   0xC9 /* bidi UndergroundTalk: A on a peer, and the talk */
/* The link Super Contest: the queue that fills one, and the engine traffic that runs it. */
#define MMO_GAME_OP_CONTEST_UP         0xC6 /* c2s ContestComm: queue, relay and result */
#define MMO_GAME_OP_CONTEST_DOWN       0xCC /* s2c ContestComm: waiting, seat and relay */
#define MMO_GAME_OP_BAG_DELTA          0xDD /* c2s BagDelta: the engine consumed or gained an item */
#define MMO_GAME_OP_MONEY_DELTA        0xDE /* c2s MoneyDelta: the engine spent or earned cash */
#define MMO_GAME_OP_POKEMON_RELEASE    0xDF /* c2s PokemonRelease: the box screen let a monster go */
/*
 * The registered key item, one U16LE wire id both ways, 0 for none. The report rides c2s 0xDC
 * beside the block above; the seat comes back on s2c 0xDD, because DD-DF spent the last ids
 * free in both directions of both tables.
 */
#define MMO_GAME_OP_REGISTERED_ITEM    0xDC /* c2s RegisteredItem: the bag screen registered Y */
#define MMO_GAME_OP_REGISTERED_SEAT    0xDD /* s2c RegisteredItem: the seat at join */
#define MMO_GAME_OP_OBJECTIVE_BULK     0xD3 /* s2c f/uV1: replace the objective store */
#define MMO_GAME_OP_OBJECTIVE          0xD4 /* s2c f/Q60: one objective upsert */
#define MMO_GAME_OP_TRADE_REQUEST      0x51 /* c2s f/Lpt8: one utf16 name */
#define MMO_GAME_OP_TRADE_ACTION       0x50 /* c2s f/Dt0: one action byte */
#define MMO_GAME_OP_TRADE_SELECT       0x52 /* c2s f/Dt0: one party slot */
#define MMO_GAME_OP_TRADE_ENTRY        0x52 /* s2c f/Dt0: the peer's offered record */
#define MMO_GAME_OP_TRADE_STATE        0x6A /* s2c ours: where the trade table stands */
#define MMO_GAME_OP_TRADE_COMM         0xBF /* bidi ours: the trade scene's relayed traffic */
#define MMO_GAME_OP_GTL_OPEN           0xA5 /* c2s GtlOpenSession: kind then timestamp */
#define MMO_GAME_OP_GTL_FLAGS          0xDC /* s2c CategoryFlags: the session answer */
#define MMO_GAME_OP_GTL_SEARCH_REQ     0x9B /* c2s GtlSearchPageRequest */
#define MMO_GAME_OP_GTL_SEARCH_PAGE    0x9B /* s2c GtlSearchPage: rows and quotes */
#define MMO_GAME_OP_GTL_CREATE         0x9A /* c2s CreateMarketListing */
#define MMO_GAME_OP_GTL_CANCEL         0xA1 /* c2s GtlListingCancel: id in decimal */
#define MMO_GAME_OP_GTL_BUY            0x9C /* c2s GtlConfirmPurchase */
#define MMO_GAME_OP_GTL_CLAIM          0x54 /* c2s ours: claim settled listing money */
#define MMO_GAME_OP_GTL_PRICE          0x55 /* c2s ours: lower a listing's price */
#define MMO_GAME_OP_GTL_RESULT         0xAF /* s2c ours: one shelf verb's answer */
#define MMO_GAME_OP_GTL_MARKET         0xA3 /* c2s GtlPurchase: fill cheapest-first */
#define MMO_GAME_OP_GTL_LOG_REQ        0x9F /* c2s GtlTradeLogRequest: empty */
#define MMO_GAME_OP_GTL_LOG            0x5E /* s2c SceneObjectStates: the trade log */
#define MMO_GAME_OP_BLOCK              0x60 /* c2s f/Um1: name then reason "--" */
#define MMO_GAME_OP_FRIEND_ADD         0x62 /* c2s f/Wp0: add a friend by name */
#define MMO_GAME_OP_FRIEND_REMOVE      0x63 /* c2s f/yR: remove a friend by name */
#define MMO_GAME_OP_FRIEND_LIST        0x63 /* s2c f/OE0: replace or upsert the list */
#define MMO_GAME_OP_FRIEND_INSERT      0x64 /* s2c f/hk: one friend upsert */
#define MMO_GAME_OP_FRIEND_DELETE      0x65 /* s2c f/oo1: drop one friend by id */
#define MMO_GAME_OP_FRIEND_ONLINE      0x66 /* s2c f/As: one friend's online bit */
#define MMO_GAME_OP_GUILD_CREATE       0x80 /* c2s f/hp1: name then tag */
#define MMO_GAME_OP_GUILD_MEMBERSHIP   0x80 /* s2c f/dn0: flag then gs() */
#define MMO_GAME_OP_GUILD_MOTD         0x81 /* c2s f/xb: one utf16 motd */
#define MMO_GAME_OP_GUILD_PROFILE      0x81 /* s2c f/Vb: gs() */
#define MMO_GAME_OP_GUILD_LEAVE        0x82 /* c2s f/lM1: empty */
#define MMO_GAME_OP_GUILD_INVITE       0x83 /* c2s f/q1: one utf16 name */
#define MMO_GAME_OP_GUILD_MEMBER_ADD   0x83 /* s2c f/Od0: one member row */
#define MMO_GAME_OP_GUILD_RANK         0x84 /* c2s f/QI0: id then rank */
#define MMO_GAME_OP_GUILD_RANK_CHANGE  0x84 /* s2c f/Mw: discarded id, id, rank */
#define MMO_GAME_OP_GUILD_KICK         0x85 /* c2s f/T40: one id */
#define MMO_GAME_OP_GUILD_MEMBER_DROP  0x85 /* s2c f/ok: discarded id then id */
#define MMO_GAME_OP_GUILD_PERMS        0x86 /* c2s f/uJ: five shorts */
#define MMO_GAME_OP_GUILD_PRESENCE     0x86 /* s2c f/Qs0: id then online */
#define MMO_GAME_OP_GUILD_DISBAND      0x87 /* c2s f/pg0: initiate then id */
#define MMO_GAME_OP_GUILD_RANK_LABEL   0x88 /* c2s f/IS: rank then utf16 */
#define MMO_GAME_OP_GUILD_MEMBERS      0x88 /* s2c f/uI0: replace, count, rows */
#define MMO_GAME_OP_GUILD_LOG_REQ      0x89 /* c2s f/NO0: page as S16LE */
#define MMO_GAME_OP_GUILD_LOG          0x89 /* s2c f/EB1: total, count, rows */
#define MMO_GAME_OP_MAIL_COMPOSE       0x95 /* c2s f/Ap1: recipient, subject, body, attachments */
#define MMO_GAME_OP_MAIL_RESULT        0x96 /* s2c f/oP1: one sj1 byte */
#define MMO_GAME_OP_MAIL_DETAIL_REQ    0x96 /* c2s f/f4: mail id */
#define MMO_GAME_OP_MAIL_DELETE        0x97 /* c2s f/Tm0: mail id then page */
#define MMO_GAME_OP_MAIL_PAGE          0x97 /* s2c f/ih: page, sent flag, count, Xe0 rows */
#define MMO_GAME_OP_MAIL_COUNTS        0x98 /* s2c f/ia0: inbox, inbox-seen, sent */
#define MMO_GAME_OP_MAIL_PAGE_REQ      0x99 /* c2s f/iR: page then sent flag */
#define MMO_GAME_OP_MAIL_DETAIL        0x99 /* s2c f/Ts0: present, sent flag, Xe0 with body */
#define MMO_GAME_OP_LINK_INVITE        0xD0 /* c2s f/cOm8: one utf16 name */
#define MMO_GAME_OP_LINK_SNAPSHOT      0xD0 /* s2c f/r9: present, leader, Gc rows */
#define MMO_GAME_OP_LINK_KICK          0xD1 /* c2s f/TP: one id */
#define MMO_GAME_OP_LINK_ADD           0xD1 /* s2c f/dx1: one Gc */
#define MMO_GAME_OP_LINK_REMOVE        0xD2 /* s2c f/LX0: removed id then leader */
#define MMO_GAME_OP_LINK_LEAVE         0xD3 /* c2s f/Jd: empty */
#define MMO_GAME_OP_LINK_CAPTAIN       0xD4 /* c2s f/KG1: one id */
#define MMO_GAME_OP_LINK_LEADER        0xDA /* s2c f/xG: the new leader id */
#define MMO_GAME_OP_MENU_VISIBILITY    0x58 /* s2c f/AB1: show or hide a HUD menu */
#define MMO_GAME_OP_MENU_PAGE          0x59 /* s2c f/mA0: one HUD menu page blob */
#define MMO_GAME_OP_NAME_CHOICES       0x5A /* s2c f/lk1: named entity choices */
#define MMO_GAME_OP_OPTION_LIST        0x5B /* s2c f/Nt0: typed option rows */
#define MMO_GAME_OP_LIST_WINDOW        0x5C /* s2c f/B5: a paged labelled list */
#define MMO_GAME_OP_MENU_PROMPT_OPEN   0xD6 /* s2c f/UU: open a typed prompt */
#define MMO_GAME_OP_MENU_PROMPT_CLOSE  0xD8 /* s2c f/CoM8: close that prompt */
#define MMO_GAME_OP_CONFIRM_PROMPT     0xD9 /* s2c f/a61: show or hide a confirm */
#define MMO_GAME_OP_VIEW_SCALE         0xF1 /* s2c f/SI0: one scale byte */
#define MMO_GAME_OP_DIGEST             0xA6 /* s2c f/pg ; c2s f/pu */
#define MMO_GAME_OP_DIGEST_BATCH       0xA7 /* s2c f/el1 ; c2s f/Kx0 */
#define MMO_GAME_OP_TRANSFER_BEGIN     0xAB /* s2c f/gX0 */
#define MMO_GAME_OP_TRANSFER_APPEND    0xAC /* s2c f/vG */
#define MMO_GAME_OP_STREAM_CHUNK       0x77 /* s2c f/cM1 */
#define MMO_GAME_OP_IMAGE_CHUNK        0xF6 /* s2c f/nU0 */
#define MMO_GAME_OP_QUEUE_LEAVE        0x47 /* c2s f/zr0: empty */
#define MMO_GAME_OP_QUEUE_JOIN         0x49 /* c2s f/zx: empty */
#define MMO_GAME_OP_QUEUE_SIGNUP       0x48 /* c2s f/WR0: the signup */
#define MMO_GAME_OP_TIER_SELECT        0x4A /* c2s f/Sx0: slot then tier */
#define MMO_GAME_OP_QUEUE_ACTION       0x4C /* c2s f/sz0: a typed action */
#define MMO_GAME_OP_QUEUE_CANCEL       0x6B /* c2s f/Od: empty */
#define MMO_GAME_OP_QUEUE_LANGS        0x6C /* c2s f/ny1: count then bytes */
#define MMO_GAME_OP_RENTAL_SET         0x71 /* s2c f/zS: a rental offer */
#define MMO_GAME_OP_SCORE_BOARD_REQ    0x74 /* c2s f/R60: one category byte */
#define MMO_GAME_OP_TOURNEY_VIEW       0x75 /* c2s f/xU1: id, active, tab */
#define MMO_GAME_OP_TOURNEY_PAGE       0x75 /* s2c f/Oa1: a page of tournaments */
#define MMO_GAME_OP_TOURNEY_COUNT      0x78 /* s2c f/sp1: one entry count */
#define MMO_GAME_OP_COOP_SCORE_REQ     0x7B /* c2s f/dK0: empty */
#define MMO_GAME_OP_TOURNEY_MATCHUPS   0x7B /* s2c f/jo1: the bracket */
#define MMO_GAME_OP_SCORE_BOARD        0xA4 /* s2c f/ZT1: a high-score page */
#define MMO_GAME_OP_TOURNEY_TELEPORT   0x44 /* c2s f/pY0: empty */
#define MMO_GAME_OP_TOURNEY_REGISTER   0xC1 /* c2s f/bE0: one boolean */
#define MMO_GAME_OP_GM_LOOKUP          0xA1 /* s2c f/sy0: a staff player lookup */
#define MMO_GAME_OP_GM_PANEL           0xA2 /* s2c f/G7: one staff panel body */
#define MMO_GAME_OP_ADMIN_NOTE         0xA2 /* c2s f/QP: add or drop an account note */
#define MMO_GAME_OP_MOD_CONFIRM        0xA9 /* c2s f/TO1: confirm on one id */
#define MMO_GAME_OP_GM_PANEL_ENTRY     0xF7 /* s2c f/op: one panel row, or the clear */

/* Assembled 0xAB+0xAC wired size. Each 0xAC is U16LE so one packet fits
 * the 64 KiB gbuf; the concat is heap-backed and this is the loud cap. */
#define MMO_SYNC_XFER_MAX    65536
#define MMO_SYNC_DIGEST_MAX  512
#define MMO_SYNC_SIG_MAX     256
#define MMO_SYNC_PLAIN_MAX   8192
#define MMO_SYNC_STREAM_MAX  8192
#define MMO_SYNC_IMAGE_MAX   8192

/* Wire actionType values the official client's qM1 names. 0x64 is the close; 3 and 4
 * are the sign and npc boxes; 5 is yes/no (qM1.Bs0); 0x23 is a species
 * menu (qM1.mJ); 0x31 is a text list (qM1.b). Other values are carried
 * and named as the byte they are. */
#define MMO_DIALOG_ACTION_SIGN  3
#define MMO_DIALOG_ACTION_NPC   4
#define MMO_DIALOG_ACTION_YESNO 5
#define MMO_DIALOG_ACTION_MENU  0x23
#define MMO_DIALOG_ACTION_LIST  0x31
#define MMO_DIALOG_ACTION_CLOSE 0x64

/* A 0x23 tail is a U8 count then that many S16LE species ids. A 0x31
 * tail is two unused bytes, the S16LE bank, a U8 count, then that many
 * S16LE message indices. 32 is more than any menu the server sends; a
 * larger count is refused. */
#define MMO_DIALOG_MENU_MAX 32

/* The only captured 0x26 trailer (Potion on the starter). Meaning unknown. */
#define MMO_ITEM_USE_TRAILER           0x00FF0001

/* Item stack object ids end in this tag; monster uids end in 0xC000. A 0x42
 * whose entity id does not carry the tag is a battle-side add, not a bag
 * update. Measured off itemStacksPacket / itemStackUpdatePacket. */
#define MMO_ITEM_ENTITY_TAG            0x5000

/* c2s MovementPacket: the local player's confirmed one-tile step. */
#define MMO_GAME_OP_MOVEMENT 0x06

/* c2s FaceDirectionPacket: the local player turned in place. One Direction
 * ordinal. s2c 0x07 is EntityFaceTurn, the two directions have their own
 * tables, the same way 0x0A does. */
#define MMO_GAME_OP_FACE 0x07

/* c2s ChatMessageSendPacket: a chat line. A line beginning with '/' is routed to
 * the server's chat-command dispatcher (ChatCommandService), so this doubles as
 * the way to invoke a server command such as /testbattle. */
#define MMO_GAME_OP_CHAT_SEND 0x08

/* s2c ChatMessagePacket: a line the server delivered. Same opcode as
 * PokemonMove c2s; the two directions have their own tables. */
#define MMO_GAME_OP_CHAT 0x09

/*
 * c2s PokemonMovePacket: a monster picked up from one container slot and put down on another.
 * Deposits, withdrawals and reorders are all this one gesture, and a batch may carry several
 * at once (the game client pairs the slots it dragged and sends them together).
 */
#define MMO_GAME_OP_POKEMON_MOVE 0x09

/*
 * The move-learn pair. A monster that levels past a move it can learn gets one of these per
 * move: the slot the move went into, or MMO_MOVE_LEARN_NO_SLOT when every slot was taken.
 */
#define MMO_GAME_OP_MOVE_LEARN_PROMPT 0x17 /* s2c MoveLearnPrompt */
#define MMO_GAME_OP_MOVE_LEARN_REPLY  0x0A /* c2s MoveLearnReply */

/*
 * The evolution pair. The server decides a monster evolves and names the species it becomes;
 * the client only plays it.
 */
#define MMO_GAME_OP_EVOLUTION_PROMPT 0x18 /* s2c EvolutionPrompt */
#define MMO_GAME_OP_EVOLUTION_REPLY  0x0B /* c2s EvolutionPromptResponse */

/*
 * The breeding group. The player picks two parents, the server answers with what the egg would
 * be, and a submit commits it.
 */
#define MMO_GAME_OP_BREEDING_ASSIGN   0x73 /* c2s AssignBreedingSlot */
#define MMO_GAME_OP_BREEDING_FORECAST 0x73 /* s2c BreedingForecast */
#define MMO_GAME_OP_BREEDING_SUBMIT   0x40 /* c2s SubmitBreedingParty */
#define MMO_GAME_OP_EGG_INCUBATORS    0x7E /* s2c EggIncubatorSlots */

/* s2c BattleFieldStatePacket: the field state that opens a battle scene. Carries
 * whether the opponent is wild or a trainer; the server decides an encounter has
 * begun and sends this, the client no longer rolls one locally. */
#define MMO_GAME_OP_BATTLE_FIELD_STATE 0x30

/*
 * The battle scene's two ends. s2c EntityPresence says which scene the server holds an entity
 * in; for the local player that is the only per-player frame that marks both the way in and
 * the way out.
 */
#define MMO_GAME_OP_ENTITY_PRESENCE      0x0F /* s2c EntityPresence */
#define MMO_GAME_OP_MAP_LOADED_ACK       0x33 /* c2s MapLoadedAck (empty = ready) */
#define MMO_GAME_OP_KEEPALIVE            0xC2 /* bidi KeepAlive, echoed: this client's ping */
#define MMO_GAME_OP_BATTLE_ACTION_SELECT 0x32 /* c2s BattleActionSelect */
#define MMO_GAME_OP_BATTLE_CHAT_SEND     0x4B /* c2s BattleChatMessage */

/* The ordered battle-event stream. Same numeric slots as the c2s acks above
 * are a different packet in each direction, 0x32/0x33 going up are the
 * action and the leave ack; coming down they are a prompt and a move. */
#define MMO_GAME_OP_BATTLE_ENTITY_DELTA   0x16 /* s2c BattleEntityDelta */
#define MMO_GAME_OP_BATTLE_BULK_STATE     0x31 /* s2c BattleBulkState */
#define MMO_GAME_OP_BATTLE_QUEUED_EVENT   0x32 /* s2c BattleQueuedEvent */
#define MMO_GAME_OP_BATTLE_MOVE_EVENT     0x33 /* s2c BattleEntityMoveEvent */
#define MMO_GAME_OP_BATTLE_SLOT_EVENT     0x34 /* s2c BattleSlotEventEnum */
#define MMO_GAME_OP_BATTLE_SWITCH_IN      0x35 /* s2c BattleSwitchIn */
#define MMO_GAME_OP_BATTLE_SLOT_FLAG      0x36 /* s2c BattleSlotFlagEvent */
#define MMO_GAME_OP_BATTLE_LIST_EVENT     0x37 /* s2c BattleListEvent */
#define MMO_GAME_OP_BATTLE_STAT_COUNTERS  0x79 /* s2c BattleStatCounters */
#define MMO_GAME_OP_SOCIAL_LIST_ADD       0x14 /* s2c SocialListEntryAdd */

/*
 * s2c presence opcodes: other entities appearing, stepping, turning and leaving
 * (GameProtocol.kt). LoadEntity shares 0x05 with the c2s RequestPlayer; a s2c LoadEntity is a
 * spawn.
 */
#define MMO_GAME_OP_LOAD_ENTITY   0x05  /* s2c LoadEntity: another entity spawned */
#define MMO_GAME_OP_ENTITY_TURN   0x07  /* s2c EntityFaceTurn */
#define MMO_GAME_OP_ENTITY_SNAP   0x11  /* s2c NpcUpdate: seat a pose, no walk */
#define MMO_GAME_OP_NPC_SPAWN     0x12  /* s2c NpcSpawn: a map / cutscene npc */
#define MMO_GAME_OP_ENTITY_LEAVE  0x08  /* s2c EntityLeave */
#define MMO_GAME_OP_ENTITY_DESPAWN 0xB7 /* s2c EntityDespawn */
#define MMO_GAME_OP_ENTITY_MOVE   0xE4  /* s2c EntityMove (NDS map) */
#define MMO_GAME_OP_GBA_MOVE      0xEA  /* s2c GbaEntityMove (GBA map) */

/*
 * What an entity is riding, sent when it changes: an S64LE id and one byte, which is the same
 * byte LoadEntity carries as `transportation`.
 */
#define MMO_GAME_OP_TRANSPORTATION 0x28 /* s2c EntityTransportation */

#define MMO_TRANSPORT_NONE    0x00
#define MMO_TRANSPORT_SURFING 0x01
#define MMO_TRANSPORT_BIKE    0x02
#define MMO_TRANSPORT_DIVING  0x10

/* The decomp's FLAG_STRENGTH_ACTIVE. The server sends it live on the story-flag
 * packet (0x2A) so the fused client can set the engine sysflag; it is not a
 * persisted story flag and 0x28 has no bit for it. */
#define MMO_FLAG_STRENGTH_ACTIVE 2402

/* The decomp's FLAG_HAS_PARTNER. Same 0x2A shape as Strength: the server
 * decides a cutscene npc is following and tells, so the engine's LockAll
 * and item-use checks see a partner. The walk itself is movement type
 * MMO_MOVEMENT_FOLLOW_PLAYER on the 0x12 that names the body. */
#define MMO_FLAG_HAS_PARTNER 2408

/* The decomp's MOVEMENT_TYPE_FOLLOW_PLAYER (generated/movement_types.txt:49).
 * This server packs the movement id into NpcSpawn.unk3's high byte. */
#define MMO_MOVEMENT_FOLLOW_PLAYER 48

/* s2c weather deltas streamed as the map's weather changes under the player
 * (MapWeatherModeSetPacket / OverworldWeatherControlPacket). */
#define MMO_GAME_OP_WEATHER_MODE    0xC1 /* s2c MapWeatherModeSet */
#define MMO_GAME_OP_WEATHER_CONTROL 0x1E /* s2c OverworldWeatherControl */

/* Bodies at or above this many bytes are DEFLATE-compressed (flag 1);
 * PipelineOptions.compressionThreshold default. */
#define MMO_GAME_COMPRESS_THRESHOLD 256

/* One game connection's persistent inbound compression state: the S->C inflater,
 * carrying its sliding window across packets. There is no outbound deflater, 
 * the client never compresses (measured: compression is S->C only). The AES-CTR
 * and checksum state live in the session crypto passed alongside. */
typedef struct {
    mmo_inflate infl;   /* S->C */
} mmo_game_stream;

void mmo_game_stream_init(mmo_game_stream *g);

/* Frame one outbound game packet and append it to out. The client sends
 * uncompressed, so this is just the session envelope over opcode||body
 * (AES-CTR + CRC-16 + length frame), no compression flag. Sets out->err on an
 * internal failure. */
void mmo_game_send(mmo_session_crypto *c,
                   u8 opcode, const u8 *body, size_t bodylen, mmo_wbuf *out);

/*
 * Open one inbound game frame. payload/n is a frame body from mmo_frame_get (ciphertext
 * followed by its CRC tag).
 */
size_t mmo_game_recv(mmo_session_crypto *c, mmo_game_stream *g,
                     const u8 *payload, size_t n,
                     u8 *opcode_out, u8 *body, size_t cap);

/* --- application packets ------------------------------------------------- */

/*
 * Write a minimal JoinPacket body (no opcode): NewAuthData(userId, token) plus the fields the
 * server's codec parses but its join handler ignores, zeroed mac/revisions/languages/romMask,
 * no roms, no clientInfo, platform linux, arch/bitness at their first enum value, an empty
 * unk1 and a 32-byte unk2.
 */
void mmo_game_write_join(mmo_wbuf *body, s32 user_id,
                         const u8 *token, size_t token_len);

/* The JoinResponse fields the client acts on. */
typedef struct {
    int can_join;
    s32 playtime;
    s32 reward_points;
    s32 balance;
} mmo_join_response;

/* Parse a JoinResponse body (opcode already stripped). Returns 0 on success; on
 * canJoin=false only can_join is set. Returns -1 on a malformed body. */
int mmo_game_read_join_response(const u8 *body, size_t n, mmo_join_response *out);

/* Write a SelectCharacterPacket body (no opcode): the characterId and its hash,
 * both S64LE (SelectCharacterPacketCodec). The server validates only the id
 * (LoginService.onCharacterSelected reads event.packet.characterId and never the
 * hash), so a hash of 0 is accepted. */
void mmo_game_write_select_character(mmo_wbuf *body, s64 character_id, s64 character_id_hash);

/* Write a DeleteCharacterPacket body (no opcode): the characterId, S64LE
 * (DeleteCharacterPacketCodec). */
void mmo_game_write_delete_character(mmo_wbuf *body, s64 character_id);

/* Read a DeleteCharacterResultPacket body (opcode already stripped): a U8
 * result then the S64LE id it applied to. Returns 0, or -1 on a short body. */
int mmo_game_read_delete_result(const u8 *body, size_t n, int *out_result,
                                s64 *out_id);

/* Read an EntityTransportation body (opcode already stripped): the S64LE entity
 * id then the one-byte mount. Returns 0, or -1 on a short body. */
int mmo_game_read_transportation(const u8 *body, size_t n, u32 *out_id,
                                 int *out_transportation);

/*
 * Map an engine field facing (DIR_*: NORTH=0, SOUTH=1, WEST=2, EAST=3;
 * constants/map_object.h:5) to the server's Direction ordinal (DOWN=0, UP=1, left=2, right=3;
 * common/enums/Direction.kt).
 */
u8 mmo_game_dir_from_ds(int ds_dir);

/*
 * The inverse of mmo_game_dir_from_ds: map a server Direction ordinal (DOWN=0, UP=1, LEFT=2,
 * Right=3) back to an engine field facing DIR_* (north=0, south=1, west=2, east=3).
 */
int mmo_game_dir_to_ds(u8 wire_dir);

/*
 * Write a MovementPacket body (no opcode): the from-tile x,y (S16LE each), the tile the
 * player just left, then a packed U8 state = (wire_dir & 0x03) | (running ? 0x80 : 0), where
 * wire_dir is a server Direction ordinal (see mmo_game_dir_from_ds).
 */
void mmo_game_write_movement(mmo_wbuf *body, s16 x, s16 y, u8 wire_dir, int running);

/* Write a FaceDirectionPacket body (no opcode): one U8 server Direction
 * ordinal. Mirrors FaceDirectionPacketCodec / the official client's the official client. */
void mmo_game_write_face(mmo_wbuf *body, u8 wire_dir);

/*
 * Read the first character's id from a CharactersListPacket body (opcode already stripped): a
 * U8 count, then that many CharacterEntry records, each led by a CharacterInfo whose first
 * field is the S64LE id (CharacterInfoCodec).
 */
int mmo_game_read_first_character_id(const u8 *body, size_t n, s64 *out_id);

/* The name the server accepts on CreateCharacter, and the twelve cosmetic slots
 * a SkinSet can populate. Type is 10 bits and colour is 6, packed into one
 * little-endian word per present slot. */
#define MMO_CHAR_NAME_MAX     32
#define MMO_SKIN_SLOTS        12
#define MMO_SKIN_TYPE_MASK    0x03FFu
#define MMO_SKIN_COLOR_SHIFT  10
#define MMO_SKIN_COLOR_MASK   0x3Fu
#define MMO_SKIN_HAS_OVERRIDES 0x8000u

/* Slot order is the wire's: bit 0 of the mask is the forehead, bit 11 the bike.
 * The names are only for the CLI; the packet carries the bit, not the word. */
enum {
    MMO_SKIN_FOREHEAD = 0,
    MMO_SKIN_HAT,
    MMO_SKIN_HAIR,
    MMO_SKIN_EYES,
    MMO_SKIN_FACIAL_HAIR,
    MMO_SKIN_BACK,
    MMO_SKIN_TOP,
    MMO_SKIN_GLOVES,
    MMO_SKIN_FOOTWEAR,
    MMO_SKIN_LEGGINGS,
    MMO_SKIN_FISHING_ROD,
    MMO_SKIN_BIKE
};

typedef struct {
    u8  present;   /* 1 if this slot contributes a word to the mask */
    u16 type;      /* 0..1023 */
    u8  color;     /* 0..63 */
} mmo_skin_slot;

typedef struct {
    int          region_selection_index; /* SkinSet leading byte */
    mmo_skin_slot slot[MMO_SKIN_SLOTS];
} mmo_skin_set;

typedef struct {
    const char  *name;             /* Latin-1, at most MMO_CHAR_NAME_MAX */
    int          gender;           /* 0 male, 1 female */
    int          starting_region;  /* wire region id */
    mmo_skin_set appearance;
} mmo_create_character;

/* The leading fields of the first CharactersList entry, enough to name what
 * came back after a create. `gender` is -1 when the body ended before rivalSex. */
typedef struct {
    int  count;
    s64  id;
    char name[MMO_CHAR_NAME_MAX + 1];
    int  gender;
} mmo_character_ref;

/*
 * Write a CreateCharacter body (no opcode): UTF-16LE name, S8 gender, S8 starting region, then
 * a SkinSet, regionSelectionIndex, U16LE slot mask, one U16LE packed word per set bit.
 */
int mmo_game_write_create_character(mmo_wbuf *w, const mmo_create_character *in);

/*
 * Like mmo_game_read_first_character_id, and when the body continues far enough it also takes
 * the first CharacterInfo's UTF-16 name and rivalSex (the field that stores the character's
 * own gender). Returns 0, or -1 on a truncated count/id.
 */
int mmo_game_read_first_character(const u8 *body, size_t n, mmo_character_ref *out);

/* One CharactersList entry, after the count. `gender` and `region` are -1 when
 * the CharacterInfo ended before those fields. */
typedef struct {
    s64  id;
    char name[MMO_CHAR_NAME_MAX + 1];
    int  gender; /* rivalSex */
    int  region; /* positionRegionId */
    mmo_skin_set appearance; /* first SkinSet on the list row; empty if none */
} mmo_character;

/* How many entries a join will hold. The packet's U8 allows 255; nothing here
 * has seen an account with more than a handful, and a later row is still
 * walked so a name/region pick past this cap still works. */
#define MMO_CHAR_LIST_MAX 16

typedef struct {
    int           count; /* the U8 on the wire */
    int           held;  /* entries stored, min(count, MMO_CHAR_LIST_MAX) */
    mmo_character entry[MMO_CHAR_LIST_MAX];
} mmo_character_list;

/* Walk every CharacterEntry: CharacterInfo (the short form the server writes
 * and the official client list capture uses), both SkinSets, the optional guild, and
 * the party. Returns 0, or -1 on a truncated body. A well-formed list is
 * consumed to its last byte. */
int mmo_game_read_character_list(const u8 *body, size_t n, mmo_character_list *out);

/* SelectedCharacter (s2c 0x04): a Bool, then, when present, one CharacterInfo
 * in the short form the list also uses. The server sends this after it accepts
 * SelectCharacter. Returns 0, or -1 on a truncated body. An absent optional
 * (first byte 0) succeeds with id 0, an empty name, and gender/region -1. */
int mmo_game_read_selected_character(const u8 *body, size_t n, mmo_character *out);

/*
 * Choose one row of a walked list. A non-empty `name` is a case-insensitive exact match;
 * `index` >= 0 is that 0-based slot; `region` >= 0 is the unique character whose
 * positionRegionId is that id.
 */
int mmo_game_pick_character(const mmo_character_list *list,
                            const char *name, int index, int region,
                            char *err, size_t errcap);

/* --- presence: other entities (opcode already stripped) ------------------ * */

/*
 * A LoadEntityPacket (0x05): a full spawn. `name` is the low-byte (Latin-1) rendering of the
 * packet's UTF-16LE name (truncated to cap).
 */
typedef struct {
    u32  entity_id;
    int  gender;         /* body the sprite is drawn from (0 male, 1 female) */
    int  region_id, bank_id, map_id;
    int  x, y, z;
    int  facing;         /* heading: the low 2 bits of the packed heading byte */
    int facing_flag;    /* that byte's 0x08 bit; official matches it against a
                          * tile's own flag when binding an entity to a tile */
    int  skin_region;    /* SkinSet.regionSelectionIndex (U8 leading byte) */
    int  skin_count;     /* number of populated cosmetic skin slots */
    mmo_skin_set appearance; /* kept slots; type/colour as the packet sent them */
    int  transportation; /* what the entity rides: MMO_TRANSPORT_* bits */
    int  entity_state;   /* small enum, 0 = none; meaning unestablished */
    int  has_follower;   /* flags & 0x04 */
    int  follower_dex;   /* followerDexId when has_follower, else 0 */
    char name[64];
    char name_prefix[32]; /* flags & 0x10; drawn as "[prefix]name" */
} mmo_load_entity;
int mmo_game_read_load_entity(const u8 *body, size_t n, mmo_load_entity *out);

/*
 * A GbaEntityMovePacket (0xEA): one entity's new tile after a step on a GBA map. Tile x/y are
 * U8 (the game client sign-extends them, so a GBA grid never exceeds 127 tiles);
 * `movement_mode` doubles as the model's move-speed index (default 2 = walk).
 */
typedef struct {
    u32 entity_id;
    int bank_id, map_id;
    int x, y;
    int movement_mode;
    int direction;       /* heading, the packed byte's low two bits */
    int dir_flags;       /* the packed byte verbatim */
} mmo_gba_move;
int mmo_game_read_gba_move(const u8 *body, size_t n, mmo_gba_move *out);

/* An EntityMovePacket (0xE4): one entity's new tile after a step on an NDS map.
 * Tile x/y are S16; the trailing byte is packed as in mmo_gba_move. */
typedef struct {
    u32 entity_id;
    int x, y;
    int direction;       /* heading, the packed byte's low two bits */
    int dir_flags;       /* the packed byte verbatim */
} mmo_entity_move;
int mmo_game_read_entity_move(const u8 *body, size_t n, mmo_entity_move *out);

/*
 * An NpcUpdatePacket (0x11): a whole pose seated on one entity at once, which the game client
 * applies by dropping the entity's queued walk and placing it, the scripted-cutscene
 * reposition, and the only packet that moves the local player without a step.
 */
typedef struct {
    u32 entity_id;
    int region_id, bank_id, map_id;
    int x, y;
    int movement_mode;
    int direction;       /* heading, the packed byte's low two bits */
    int dir_flags;       /* the packed byte verbatim */
} mmo_entity_snap;
int mmo_game_read_entity_snap(const u8 *body, size_t n, mmo_entity_snap *out);

/* An EntityFaceTurnPacket (0x07): one entity turned in place. `facing` is an S8:
 * 0..3 is a heading, -1 asks for the heading that points at the local player,
 * and anything else the game client drops on the floor. */
int mmo_game_read_face_turn(const u8 *body, size_t n, u32 *out_id, int *out_facing);

/*
 * One entry of a GBA map's connection list (GbaConnectionCodec): the edge a neighbour joins on
 * and which map it is.
 */
typedef struct {
    int direction;    /* server Direction ordinal (0..3) */
    int offset;       /* along-edge alignment shift (the codec's `unknown`) */
    int target_bank;
    int target_map;
} mmo_map_connection;

/* Connections kept per map; a GBA map has at most four edges, but the wire list
 * is U8-prefixed, so a longer list is truncated and the true count kept. */
#define MMO_MAP_MAX_CONNECTIONS 4

/* A LoadMapPacket (0x10), decoded whole (LoadMapPacketCodec). */
typedef struct {
    int reload_player;
    int delete_cache;
    int region_id, bank_id, map_id;
    int is_nds;
    int weather;      /* Weather ordinal */
    int lighting;     /* Lighting ordinal */
    int map_type;     /* MapType ordinal */
    /* NDS-only (0 on a GBA map). Both are carried, not interpreted: `nds_unknown`
     * is the leading halfword and `nds_pair_count` how many halfword pairs the
     * list held, the meaning of neither is established. */
    int nds_unknown;
    int nds_pair_count;
    /* GBA-only (0 on an NDS map): */
    int width, height;
    int encounter_type;               /* EncounterType ordinal */
    int connection_count;             /* stored (<= MMO_MAP_MAX_CONNECTIONS) */
    int connection_total;             /* on-wire count */
    mmo_map_connection connections[MMO_MAP_MAX_CONNECTIONS];
} mmo_load_map;
int mmo_game_read_load_map(const u8 *body, size_t n, mmo_load_map *out);

/* A MapWeatherModeSetPacket (0xC1, MapWeatherModeSetPacketCodec): a weather mode
 * toggle, `mode` (S8) and `enabled` (U8). The server streams these as the map's
 * weather changes under the player. Returns 0, or -1 on a short body. */
int mmo_game_read_map_weather_mode(const u8 *body, size_t n,
                                   int *out_mode, int *out_enabled);

/*
 * An OverworldWeatherControlPacket (0x1E, OverworldWeatherControlPacketCodec): a tagged
 * weather-effect control keyed by `effect_type` (S8).
 */
int mmo_game_read_weather_control(const u8 *body, size_t n, int *out_effect_type);

/* An EntityLeavePacket (0x08): S64 id only. */
int mmo_game_read_entity_leave(const u8 *body, size_t n, u32 *out_id);

/* The same S64 id, kept whole. Map npcs use a high-bit id whose low 32
 * bits are not unique against player character ids. */
int mmo_game_read_entity_id64(const u8 *body, size_t n, s64 *out_id);

/*
 * An NpcSpawnPacket (0x12). Field order is this server's codec (NpcSpawnPacketCodec): S64 id,
 * sprite-region U8, graphics U16, two unknown U16s, region/bank/map U8, x/y U16, unknown U8,
 * facing U8, unknown U16.
 */
typedef struct {
    s64 entity_id;
    int sprite_region;
    int graphics_id;
    int unk3;
    int unk4;
    int region_id, bank_id, map_id;
    int x, y;
    int unk5;
    int facing;
    int unk6;
    int movement; /* (unk3 >> 8) & 0xff, this server's packing */
} mmo_npc_spawn;
int mmo_game_read_npc_spawn(const u8 *body, size_t n, mmo_npc_spawn *out);

/* An EntityDespawnPacket (0xB7): a reserved byte then an S16 id. */
int mmo_game_read_entity_despawn(const u8 *body, size_t n, u32 *out_id);

/* The opponent side an opening battle carries (BattleFieldStatePacket's
 * OpposingSide). The full party/opponent blocks are a later phase; only the side
 * is read here. */
#define MMO_BATTLE_OPPOSING_WILD    0xFF
#define MMO_BATTLE_OPPOSING_TRAINER 0x04

typedef struct {
    int opposing;    /* raw OpposingSide byte (MMO_BATTLE_OPPOSING_*) */
    int wild;        /* 1 = wild encounter, 0 = trainer battle */
    int background;  /* battle backdrop selector */
    int foe_species; /* first revealed opponent's server dex id; 0 if unread */
    int foe_level;   /* that opponent's level; 0 if unread */
} mmo_battle_field_state;

/*
 * A BattleFieldStatePacket (0x30, BattleFieldStatePacketCodec): the field state that opens a
 * battle. Only the head is decoded, the backdrop byte and the OpposingSide byte, which is
 * enough to know a battle has begun and whether it is wild or a trainer.
 */
int mmo_game_read_battle_field_state(const u8 *body, size_t n,
                                     mmo_battle_field_state *out);

/* Walk the party and opponent blocks of a field state after the header. Fills
 * foe_species / foe_level on `out`. Returns 0 when a revealed opponent was
 * named, or -1 if this body does not carry one. The header read stays valid
 * either way. */
int mmo_game_read_battle_foe(const u8 *body, size_t n,
                             mmo_battle_field_state *out);


#define MMO_BATTLE_DELTA_EXPERIENCE  0x000001
#define MMO_BATTLE_DELTA_STATS       0x000002
#define MMO_BATTLE_DELTA_MOVES       0x000004
#define MMO_BATTLE_DELTA_HP          0x000008
#define MMO_BATTLE_DELTA_FAINT       0x000010
#define MMO_BATTLE_DELTA_SPECIES     0x000020
#define MMO_BATTLE_DELTA_LEVEL       0x000100
#define MMO_BATTLE_DELTA_EVS         0x000080
#define MMO_BATTLE_DELTA_HAPPINESS   0x000200

#define MMO_BATTLE_STATS 6
#define MMO_BATTLE_MOVES 4

typedef struct {
    u32 entity_id;
    u32 mask;                         /* bits present on the wire */
    int xp_level;                     /* EXPERIENCE group; -1 if absent */
    int xp;                           /* running total; -1 if absent */
    int level;                        /* LEVEL group; -1 if absent */
    int hp;                           /* CURRENT_HP group; -1 if absent */
    int faint;                        /* FAINT group; -1 if absent */
    int species;                      /* SPECIES group; -1 if absent */
    int forme;
    int happiness;                    /* -1 if absent */
    int have_stats;
    s16 stats[MMO_BATTLE_STATS];
    int have_evs;
    s16 evs[MMO_BATTLE_STATS];
    int have_moves;
    s16 move_id[MMO_BATTLE_MOVES];
    u8  move_pp[MMO_BATTLE_MOVES];
    u8  pp_ups;
} mmo_battle_entity_delta;

/* A BattleEntityDeltaPacket (0x16): a mask word then the groups it names, in
 * the order the official client walks them (packed IVs sit between VALUE_64 and origin).
 * Named groups are surfaced; the rest are consumed to keep the stream framed.
 * A bit past the 24 the reader knows, or a group it cannot cross, returns -1. */
int mmo_game_read_battle_entity_delta(const u8 *body, size_t n,
                                      mmo_battle_entity_delta *out);

#define MMO_BATTLE_SUB_HP     0
#define MMO_BATTLE_SUB_STAT   1
#define MMO_BATTLE_SUB_EFFECT 4
#define MMO_BATTLE_SUB_FAINT  5
#define MMO_BATTLE_SUB_FAIL   0x40

#define MMO_BATTLE_MOVE_TARGETS 4
#define MMO_BATTLE_MOVE_SUBS    4

typedef struct {
    int type;            /* MMO_BATTLE_SUB_* */
    int have_a, have_b;
    u32 entity_a, entity_b;
    int hp;              /* type 0 */
    int stat;            /* type 1: wire stat id, low 7 bits */
    int stages;          /* type 1: signed stage delta */
    int change_type;     /* type 1 */
    int faint_anim;      /* type 5 */
    int fail_move;       /* type 0x40 */
} mmo_battle_sub_event;

typedef struct {
    u32 entity_id;
    int outcome;         /* targetMove word: 0x0200 hit, 1 miss, 4 fail */
    int n_subs;
    mmo_battle_sub_event sub[MMO_BATTLE_MOVE_SUBS];
} mmo_battle_effect_target;

typedef struct {
    u32 source_entity;
    int source_move;
    int kind;
    int n_targets;
    mmo_battle_effect_target target[MMO_BATTLE_MOVE_TARGETS];
} mmo_battle_move_event;

/*
 * A BattleEntityMoveEventPacket (0x33): source entity, move, kind, then a U8-prefixed list of
 * targets each carrying an outcome word and a U8-prefixed list of sub-events.
 */
int mmo_game_read_battle_move_event(const u8 *body, size_t n,
                                    mmo_battle_move_event *out);

typedef struct {
    u32 entity_id;
    int base;            /* first counter; XP gained in every capture in hand */
    int have[6];
    int counter[6];
} mmo_battle_stat_counters;

/* A BattleStatCountersPacket (0x79): entity, a base int, a flag byte, then
 * one int per set bit in the low six. the official client. */
int mmo_game_read_battle_stat_counters(const u8 *body, size_t n,
                                       mmo_battle_stat_counters *out);

typedef struct {
    int phase;           /* 0 = resolved, -1 = fled in the bodies we have */
    int prize;
    int value_b;
    int flag;
} mmo_battle_bulk_state;

/* A BattleBulkStatePacket (0x31): phase, two U8-prefixed serialized-entry
 * lists, two ints, a flag, then a third list. The lists are walked so the
 * frame stays intact; their contents are not surfaced. the official client. */
int mmo_game_read_battle_bulk_state(const u8 *body, size_t n,
                                    mmo_battle_bulk_state *out);

typedef struct {
    int value;           /* low 7 bits */
    int flag;            /* high bit; 1 = action prompt in every capture */
} mmo_battle_queued;

/* A BattleQueuedEventPacket (0x32): one packed byte. the official client. */
int mmo_game_read_battle_queued(const u8 *body, size_t n,
                                mmo_battle_queued *out);

typedef struct {
    int slot;
    int event_type;      /* 0 = fled in the only body the server sends */
} mmo_battle_slot_event;

/* A BattleSlotEventEnumPacket (0x34): slot then event type, two signed bytes. */
int mmo_game_read_battle_slot_event(const u8 *body, size_t n,
                                    mmo_battle_slot_event *out);

typedef struct {
    int side;            /* 0 = player's, 1 = opponent's in the bodies we have */
    int new_slot;
    int old_slot;
    int full_block;      /* 1 = a first send-out, 0 = a return */
    u32 entity_id;
    int species;
    int level;
    int gender;
    int hp;
    int max_hp;
} mmo_battle_switch_in;

/* A BattleSwitchInPacket (0x35). No the official client body is in the playthrough; the
 * walk is the later-client capture the server already pins (4-byte header,
 * then either a full block plus a 21-byte active detail or just the detail).
 * A body shorter than that detail returns -1. */
int mmo_game_read_battle_switch_in(const u8 *body, size_t n,
                                   mmo_battle_switch_in *out);

typedef struct {
    int slot;
    int flag;            /* SJ1: unread by the presenter beyond the walk */
    int immediate;       /* 0 = open the party (forced replacement); 1 = confirm */
} mmo_battle_slot_flag;

/* A BattleSlotFlagEventPacket (0x36). the official client's the official client reads a packed slot
 * byte then two bytes it compares against 1. The second of those is the one
 * that opens the party (0) or confirms a pick (1). */
int mmo_game_read_battle_slot_flag(const u8 *body, size_t n,
                                   mmo_battle_slot_flag *out);

#define MMO_BATTLE_LIST_CATCH 4   /* sub_kind that carries the ball-throw detail */

typedef struct {
    int kind;
    int value;           /* item id for a catch throw */
    int sub_kind;
    int have_detail;
    int list_type;
    int detail_value;
} mmo_battle_list_event;

/* A BattleListEventPacket (0x37). the official client's the official client reads kind, a short, then
 * a sub-kind; only sub-kind 4 grows a detail (a list-type byte and a short).
 * That is the ball-throw event. Any other sub-kind stops there. */
int mmo_game_read_battle_list_event(const u8 *body, size_t n,
                                    mmo_battle_list_event *out);

/* The scene an EntityPresencePacket names. The official client keeps the byte as a
 * small enum and only two of its values are ever seen: the overworld, and a
 * battle. Anything else is carried as its raw value and acted on by nobody. */
#define MMO_PRESENCE_OVERWORLD 0
#define MMO_PRESENCE_IN_BATTLE 1

typedef struct {
    u32 entity_id;
    int status;      /* raw status byte (MMO_PRESENCE_*) */
} mmo_entity_presence;

/* An EntityPresencePacket (0x0F): an entity id at full width then one status
 * byte, which is what the official client's own reader takes (`the official client` reads a
 * long then a byte it looks up in an enum). Ids are truncated to 32 bits the way
 * every other presence packet's is. Returns 0, or -1 on a short body. */
int mmo_game_read_entity_presence(const u8 *body, size_t n,
                                  mmo_entity_presence *out);

/* The turn intents a battler may send. The numbers are the official client's own action
 * kinds (the official client). Sixteen more exist in that enum; they are not framed here
 * because no capture has established a meaning for them. */
#define MMO_BATTLE_ACTION_MOVE   0
#define MMO_BATTLE_ACTION_ITEM   1
#define MMO_BATTLE_ACTION_SWITCH 2
#define MMO_BATTLE_ACTION_RUN    3

typedef struct {
    u8 slot_ref;     /* packed fd1; 0 in every official client capture */
    int action;       /* MMO_BATTLE_ACTION_* */
    s16 move_or_item; /* MOVE: server move id; ITEM: server item id; SWITCH: party index */
    s64 target;       /* ITEM: target entity id; 0 when unused */
    u8  extra;        /* MOVE: packed target slot; ITEM: unknown flag */
} mmo_battle_select;

/* Frame a BattleActionSelectPacket (0x32) body. Returns 0, or -1 (and sets
 * w->err) if the kind has no established tail, nothing is written then. */
int mmo_game_write_battle_select(mmo_wbuf *w, const mmo_battle_select *sel);

/* Frame the run-away intent: slot 0, kind RUN, no tail. The the official client playthrough
 * fled with exactly those two bytes. Fleeing is the server's decision; this
 * only says the player asked. */
void mmo_game_write_battle_run(mmo_wbuf *w);

/* Frame a ChatMessageSendPacket (0x08) body carrying `text` as a plain chat line
 * (mode 0, no separate message field), the form the server routes to its
 * chat-command dispatcher. */
void mmo_game_write_chat(mmo_wbuf *w, const char *text);

void mmo_game_write_chat_mode(mmo_wbuf *w, int mode, const char *text);

void mmo_game_write_battle_chat(mmo_wbuf *w, const char *text);

void mmo_game_write_whisper(mmo_wbuf *w, const char *name, const char *text);

void mmo_game_write_trade_request(mmo_wbuf *w, const char *name);

void mmo_game_write_block(mmo_wbuf *w, const char *name, const char *reason);

/* Wire ChatType bytes, matching the official client's XR0.KJ0 (and our ChatType ordinals).
 * SYSTEM (16) is the only short form: just the message. */
#define MMO_CHAT_NORMAL  0
#define MMO_CHAT_SHOUT   3
#define MMO_CHAT_WHISPER 4
#define MMO_CHAT_TRADE   5
#define MMO_CHAT_GLOBAL  6
#define MMO_CHAT_CHANNEL 7
#define MMO_CHAT_TEAM    8
#define MMO_CHAT_LINK    9
#define MMO_CHAT_SYSTEM  16
#define MMO_CHAT_NOTICE  17
#define MMO_CHAT_BATTLE  18

#define MMO_CHAT_TEXT_MAX 128

typedef struct {
    int type;       /* MMO_CHAT_* wire byte */
    int language;   /* Language ordinal, or -1 on the system short form */
    int unknown;    /* the S8 after language; -1 when omitted */
    s64 sender_id;
    char sender[MMO_CHAR_NAME_MAX + 1];
    char text[MMO_CHAT_TEXT_MAX + 1];
} mmo_chat;

/* A ChatMessagePacket (0x09) body. System announcements (type 16) are just
 * the message; every other type is sender id, sender name, language, one
 * unknown byte, then the message. Returns 0, or -1 on a short body. */
int mmo_game_read_chat(const u8 *body, size_t n, mmo_chat *out);

/* One monster moved from one container slot to another. Both slots are signed on
 * the wire; the game client refuses to send a negative one, and so does the
 * writer below. */
typedef struct {
    u8  from_container;
    s16 from_slot;
    u8  to_container;
    s16 to_slot;
} mmo_pokemon_move;

/* The most pairs one PokemonMovePacket carries. The count is a byte and the game
 * client refuses a batch longer than this, so a longer one would be a packet no
 * real client sends. */
#define MMO_POKEMON_MOVE_MAX 127

/*
 * Frame a PokemonMovePacket (0x09) body: a byte pair count, then per pair the source container
 * byte, the source slot little-endian, and the same two for the destination.
 */
int mmo_game_write_pokemon_move(mmo_wbuf *w, const mmo_pokemon_move *moves,
                                int count);

/* --- move learn (0x17 / 0x0A) -------------------------------------------- */

/* The slot a move learn carries when the moveset was full, and the slot an
 * answer carries when the player kept the moves it had. */
#define MMO_MOVE_LEARN_NO_SLOT (-1)

/* Move slots a monster has. The chooser the game client opens draws exactly this
 * many buttons plus the new move. */
#define MMO_MOVE_SLOTS 4

/* One move a monster gained. `slot` is the move slot it went into, or
 * MMO_MOVE_LEARN_NO_SLOT when it needs an answer first. */
typedef struct {
    s64 monster_id;
    int slot;
    int move_id;
} mmo_move_learn;

/* Read a MoveLearnPromptPacket (0x17) body: the monster id, a signed slot byte
 * and the move id. Returns 0, or -1 on a short body. */
int mmo_game_read_move_learn(const u8 *body, size_t n, mmo_move_learn *out);

/* Frame a MoveLearnReplyPacket (0x0A) body: the same three fields back. `slot`
 * is 0..MMO_MOVE_SLOTS-1 to drop that move, or MMO_MOVE_LEARN_NO_SLOT to keep
 * the moveset. Returns 0, or -1 on a slot outside that range. */
int mmo_game_write_move_learn_reply(mmo_wbuf *w, s64 monster_id, int slot,
                                    int move_id);

/* --- evolution (0x18 / 0x0B) --------------------------------------------- */

/* One monster the server has decided is evolving. `species` is what it becomes,
 * as a server National Dex id; `cancelable` is whether the player is allowed to
 * stop it, which is the server's call and not a client preference. */
typedef struct {
    s64 monster_id;
    int species;
    int cancelable;
} mmo_evolution;

/* Read an EvolutionPromptPacket (0x18) body: the monster id, the species it
 * becomes and the cancelable byte. Returns 0, or -1 on a short body. */
int mmo_game_read_evolution(const u8 *body, size_t n, mmo_evolution *out);

/* Frame an EvolutionPromptResponsePacket (0x0B) body: the monster id and whether
 * the evolution went through. Returns 0. */
int mmo_game_write_evolution_reply(mmo_wbuf *w, s64 monster_id, int accepted);

/* --- breeding and eggs (0x73, 0x40, 0x7E) -------------------------------- * */

/* Incubator slots the client is shown at once: a permanent one plus the seven
 * temporary ones the game client's own text caps them at ("Up to 7 temporary
 * Incubator can be used at the same time"). The wire count is a byte, so a
 * longer list is truncated with the on-wire count kept, the way a container is. */
#define MMO_INCUBATOR_SLOTS_MAX 8

/* A temporary incubator breaks after this many uses, the denominator the game
 * client fills its wear bar with (`uses / 500`), and the number its item text
 * quotes ("It can be used up to 500 times before it breaks"). */
#define MMO_INCUBATOR_USES_FULL 500

/* One incubator slot. The id is read at full width and dropped by the game
 * client's own reader, so it is carried here verbatim and unnamed rather than
 * guessed at; `uses_left` is what the wear bar and the "{00} uses left" line
 * draw. */
typedef struct {
    s64 id;
    int uses_left;
} mmo_incubator_slot;

typedef struct {
    int total;                 /* slots on the wire */
    int count;                 /* slots stored (<= MMO_INCUBATOR_SLOTS_MAX) */
    int trailing;              /* bytes past the last slot the game client reads */
    mmo_incubator_slot slot[MMO_INCUBATOR_SLOTS_MAX];
} mmo_incubators;

/* Read an EggIncubatorSlotsPacket (0x7E) body: a byte count, then per slot an
 * id and a halfword of uses. Returns 0, or -1 on a body that runs short. */
int mmo_game_read_incubators(const u8 *body, size_t n, mmo_incubators *out);

/* Forecast caps. The stat entries are one per stat the game client draws (its IV
 * grid is the engine's six) and the move list is one per move slot, but every
 * count on the wire is a byte: a longer list is truncated with the on-wire count
 * kept, and the reader still crosses the whole body so `trailing` stays honest. */
#define MMO_BREED_STATS_MAX      6
#define MMO_BREED_CONTRIB_MAX    8
#define MMO_BREED_MOVES_MAX      4  /* the four rows its move grid has */
#define MMO_BREED_SHININESS_MAX  8

/* One IV the egg might get in a stat, and how likely it is. */
typedef struct {
    int value;                 /* the IV this outcome would give, 0..31 */
    float chance;              /* its share, drawn as a percentage */
    s32 label;                 /* a game-client string id, or <= 0 for none */
} mmo_breed_outcome;

/*
 * One stat's forecast. Entries are indexed by stat in the order the game client's own stat
 * enum uses, hp, atk, def, speed, spAtk, spDef, which is the order a monster record's IVs
 * are already in, so a stat indexes both the same way.
 */
typedef struct {
    int guaranteed;
    int item_id;
    int outcome_total;
    int outcome_count;
    mmo_breed_outcome outcome[MMO_BREED_CONTRIB_MAX];
} mmo_breed_stat;

/* How a forecast move would be known. The game client draws string 2533 + this
 * byte beside the move, which is what names them. */
#define MMO_BREED_MOVE_LEVEL_UP  0 /* "Normally learned move." */
#define MMO_BREED_MOVE_EARLY     1 /* "…taught early by parents." */
#define MMO_BREED_MOVE_PARENT    2 /* "<type> move known by a parent." */
#define MMO_BREED_MOVE_EGG       3 /* "EGG move known by a parent." */
#define MMO_BREED_MOVE_ITEM      4 /* "…due to an item held by a parent." */

/* Who the egg's original trainer would be. The game client draws the player's own
 * OT for 0 and 1, marks 1 with an asterisk ("doesn't count for the 'Caught OT'")
 * and draws "Unknown ot" for 2; a byte outside 0..2 reads as 2, as its own reader
 * does. */
#define MMO_BREED_OT_SELF         0
#define MMO_BREED_OT_SELF_UNMARKED 1
#define MMO_BREED_OT_UNKNOWN      2

/* A BreedingForecastPacket (0x73 s2c). Everything past `has_preview` is only
 * present when it is set, the server answers a pairing it will not breed with
 * the two parents and a zero byte. */
typedef struct {
    s64 parent[2];
    int has_preview;
    int species;               /* server National Dex (cross via the id map) */
    int form;
    int stat_total, stat_count;
    mmo_breed_stat stat[MMO_BREED_STATS_MAX];
    int shininess_total, shininess_count;
    int shininess[MMO_BREED_SHININESS_MAX]; /* rarity kinds, each equally likely */
    int move_total, move_count;
    int move_id[MMO_BREED_MOVES_MAX];       /* server move ids */
    int move_source[MMO_BREED_MOVES_MAX];   /* MMO_BREED_MOVE_* */
    int ot;                    /* MMO_BREED_OT_* */
    int nature;                /* 0..24, the same numbering the engine uses */
    /* Whether the player may pick the egg's gender for this pairing. The game
     * client hides the three gender buttons and the cost line when it is clear,
     * drops any choice already made back to MMO_BREED_GENDER_ANY, and draws the
     * gender the forecast itself came out with instead. */
    int gender_selectable;
    /* What the two gender buttons cost. The game client labels both with
     * "Gender Cost: ${00}" and charges gender_cost[0] for MMO_BREED_GENDER_FIRST
     * and gender_cost[1] for MMO_BREED_GENDER_SECOND; leaving it open is free. */
    s32 gender_cost[2];
    int trailing;              /* bytes past the last field the game client reads */
} mmo_breeding_forecast;

/* Read a BreedingForecastPacket (0x73) body. Returns 0, or -1 on a body that runs
 * short. */
int mmo_game_read_breeding_forecast(const u8 *body, size_t n,
                                    mmo_breeding_forecast *out);

/* The gender the player asked the egg to be. The game client's three buttons send
 * exactly these three values and nothing else; ANY is what a pairing starts at
 * and what it drops back to when the server says the gender is not selectable. */
#define MMO_BREED_GENDER_ANY    (-1)
#define MMO_BREED_GENDER_FIRST  0
#define MMO_BREED_GENDER_SECOND 1

/* Ask the server what a pairing would produce (0x73 c2s): the player's own
 * monster, the partner's, and the gender being asked for. Sent when the second
 * parent is put down and again on every gender button; the forecast above is the
 * answer. Returns 0, or -1 on a gender outside the three the buttons send. */
int mmo_game_write_breeding_assign(mmo_wbuf *w, s64 own_id, s64 partner_id,
                                   int gender);

/* Parents a submit carries. The game client's breeding screen has exactly two
 * parent tiles and its packet's array is unprefixed, so the count is not on the
 * wire, the server takes it from the body length. */
#define MMO_BREED_PARENTS 2

/* The byte a submit carries when no breeding item was selected. */
#define MMO_BREED_NO_ITEM (-1)

/*
 * Frame a SubmitBreedingPartyPacket (0x40) body: the breeding session's own byte, the two
 * parents at full width and unprefixed, the gender choice, and the selected item's key.
 */
int mmo_game_write_breeding_submit(mmo_wbuf *w, int session,
                                   const s64 parent[MMO_BREED_PARENTS],
                                   int gender, int item_key);

/* --- world state: the select/resync block (opcode already stripped) ------- * */

/* The most party members LocalPlayerState is read for, the engine's party cap
 * (PARTY_SIZE). A longer wire party is truncated and the true count kept. */
#define MMO_WS_PARTY_MAX 6

/*
 * The GBA story-variable range 0x4000..0x40ff: 256 entries, keyed on the wire by (gbaVar -
 * 0x4000). The wire key is a halfword, so it can name more than this range holds; a key past
 * the end is kept here but dropped by the store.
 */
#define MMO_WS_VAR_MAX 256

/*
 * A LocalPlayerStatePacket (0xF3, LocalPlayerStatePacketCodec): the character's region/map,
 * tile, money, gender and party.
 */
typedef struct {
    int region;
    int map_id;
    int x, y, z;
    s32 money;
    int gender;
    u16 party_dex[MMO_WS_PARTY_MAX]; /* server National Dex */
    int party_count;                 /* stored (<= MMO_WS_PARTY_MAX) */
    int party_total;                 /* on-wire count */
    int badge_count;
    int var_count;                   /* on-wire var count */
    u16 var_key[MMO_WS_VAR_MAX];     /* gbaVar - 0x4000 */
    s8  var_val[MMO_WS_VAR_MAX];
    int var_stored;                  /* stored (<= MMO_WS_VAR_MAX) */
} mmo_local_player_state;
int mmo_game_read_local_player_state(const u8 *body, size_t n,
                                     mmo_local_player_state *out);

/* A StoryFlagUpdatePacket (0x2A, StoryFlagUpdatePacketCodec): region S8, flagId
 * S16LE, enabled S16LE. One region-scoped GBA story flag set or cleared. */
int mmo_game_read_story_flag(const u8 *body, size_t n,
                             int *out_region, int *out_flag_id, int *out_enabled);

/*
 * ScriptState (0xCB), both ways. The body is the engine's own VarsFlags block as a sparse
 * delta: a U16LE-counted list of (U16LE flagID, U8 on), then a U16LE-counted list of (U16LE
 * varID, U16LE value).
 */
#define MMO_SCRIPT_FLAG_MAX  2912   /* Platinum NUM_FLAGS */
#define MMO_SCRIPT_VAR_BASE  16384  /* Platinum VARS_START */
#define MMO_SCRIPT_VAR_MAX   288    /* Platinum NUM_VARS (VARS_END - VARS_START) */

typedef struct { u16 id; u8 on; } mmo_script_flag;
typedef struct { u16 id; u16 value; } mmo_script_var;

/*
 * A save block whose working copy is the client's and whose record is the server's, carried on
 * the same op and in the same direction as the two lists above: absolute coming down, only-
 * what-changed going up.
 */
#define MMO_SAVE_BLOCK_MAX   8      /* blocks one frame may carry */
/* The widest block this client will sync. */
#define MMO_SAVE_BLOCK_BYTES 8192

typedef struct { int id; int len; const u8 *data; } mmo_save_block;

/* Frame a ScriptState body. Returns 0, or -1 if the buffer overflowed. */
int mmo_game_write_script_state(mmo_wbuf *w,
                                const mmo_script_flag *flags, int nflags,
                                const mmo_script_var *vars, int nvars,
                                const mmo_save_block *blocks, int nblocks);

/*
 * Read one. `flags`/`vars`/`blocks` may be NULL to count without storing; the counts on the
 * wire land in *out_n* even when more arrived than fit, and *out_*_stored says how many were
 * kept.
 */
int mmo_game_read_script_state(const u8 *body, size_t n,
                               mmo_script_flag *flags, int flag_cap,
                               int *out_nflags, int *out_flags_stored,
                               mmo_script_var *vars, int var_cap,
                               int *out_nvars, int *out_vars_stored,
                               mmo_save_block *blocks, int block_cap,
                               int *out_nblocks, int *out_blocks_stored);

/* ContestComm (c2s 0xC6, s2c 0xCC): a link Super Contest. */
#define MMO_CONTEST_KIND_QUEUE   0  /* c2s: rank, type, party slot */
#define MMO_CONTEST_KIND_CANCEL  1  /* c2s: leave the queue */
#define MMO_CONTEST_KIND_WAITING 2  /* s2c: how many are queued, of how many */
#define MMO_CONTEST_KIND_SEAT    3  /* s2c: the contest is starting; see below */
#define MMO_CONTEST_KIND_DATA    4  /* bidi: one comm command, id byte then body */
#define MMO_CONTEST_KIND_SYNC    5  /* bidi: a CommTiming barrier number */
#define MMO_CONTEST_KIND_LEAVE   6  /* bidi: a player is out */
#define MMO_CONTEST_KIND_RESULT  7  /* c2s: one placement byte per human seat */

/* The engine's own contestant count, human and rival together. */
#define MMO_CONTEST_SEATS 4

/* The engine's own ceiling on one relayed command: sub_02095B04 asserts it and
 * reassembles into a per-seat buffer of exactly this width. The widest real one
 * is the recorded Chatot cry at 1,005 bytes. */
#define MMO_CONTEST_DATA_MAX 1024

typedef struct {
    int kind;
    int session_id;
    int seat;                       /* -1 where the kind names no seat */
    int len;
    u8  data[MMO_CONTEST_DATA_MAX];
} mmo_contest_comm;

/* One human place in a seated contest. */
#define MMO_CONTEST_SEAT_STORY_CLEARED 0x01
#define MMO_CONTEST_SEAT_NATIONAL_DEX  0x02

typedef struct {
    int rank;
    int type;
    int humans;                     /* 2..4; the engine fills the rest */
    struct {
        char name[MMO_CHAR_NAME_MAX];
        int gender;
        int flags;                  /* MMO_CONTEST_SEAT_* */
    } seat[MMO_CONTEST_SEATS];
} mmo_contest_seat;

/* Frame a ContestComm body. Returns 0, or -1 on a payload past the cap. */
int mmo_game_write_contest_comm(mmo_wbuf *w, int kind, int session_id, int seat,
                                const void *payload, int len);

/* Read one. Returns 0, or -1 on a malformed frame or an over-long payload. */
int mmo_game_read_contest_comm(const u8 *body, size_t n, mmo_contest_comm *out);

/* Unpack a KIND_SEAT payload. Returns 0, or -1 when it does not describe a
 * contest this client could sit in. */
int mmo_game_read_contest_seat(const u8 *payload, int len, mmo_contest_seat *out);

/* KeepAlive (0xC2), both ways: U8 canJoin, then the session blob to the end of the frame. */
void mmo_game_write_keepalive(mmo_wbuf *w, u8 token);

/* The token out of an echoed KeepAlive. Returns 0 and writes *out_token, or -1
 * if the frame is too short to carry one. */
int mmo_game_read_keepalive(const u8 *body, size_t n, u8 *out_token);

/* ScriptWarpArrived (0xCC), c2s only: S32LE mapHeaderID, S16LE x, S16LE z,
 * U8 wire direction. The client has already changed map; this says where it
 * landed so the server's position and presence follow it. */
void mmo_game_write_script_warp(mmo_wbuf *w, s32 header, s16 x, s16 z,
                                u8 wire_dir);

/*
 * ClientScriptOwnership (0xCD), c2s only: one U8, 1 if this client runs the field scripts
 * itself. Sent once, immediately before RequestPlayer, so it is in front of the first map-
 * entry script the server would otherwise start.
 */
void mmo_game_write_script_owner(mmo_wbuf *w, int client_runs_scripts);

/*
 * ScriptGrantPokemon (0xCE), c2s only: U16LE dex id, U8 level, S16LE current HP (-1 = full; a
 * catch carries its battle damage), U8 container (the PokemonMovePacket's ids: 0 = the PC, 1 =
 * the party), S16LE slot (-1 = the server appends; a box catch names the box slot the engine
 * chose), S32LE seed, S32LE packed IVs and U8 shiny, the individual the engine already
 * rolled, then a null-terminated UTF-16LE nickname (empty = the species name; a catch
 * carries the name its trainer typed).
 */
void mmo_game_write_script_grant(mmo_wbuf *w, u16 dex, u8 level, s16 hp,
                                 u8 container, s16 slot, u32 seed,
                                 u32 iv_bits, u8 shiny,
                                 const u8 *nick_utf16le, size_t nick_bytes);

/* PokemonRelease (0xDF), c2s only: S64LE monster id. The box screen let this
 * monster go; the engine's containers no longer hold it and the record must
 * follow, or the next seat resurrects it. The last id of the local-play
 * block. */
void mmo_game_write_pokemon_release(mmo_wbuf *w, s64 id);

/*
 * Contest conditions. Five of them, one per contest type, in the engine's own `enum
 * PokemonContestType` order (cool, beauty, cute, smart, tough), so an index here is a contest
 * type and reaches `MON_DATA_COOL + i` directly.
 */
#define MMO_MON_CONDITIONS 5

/*
 * Super Contest ribbons: a bit per (type, rank) pair, laid out exactly as the engine lays out
 * `BoxPokemon` block C's `ribbonsDS2`, so the mask crosses unchanged and a bit is
 * `MON_DATA_SUPER_COOL_RIBBON + n`.
 */
#define MMO_MON_RIBBON_RANKS 4
#define MMO_MON_RIBBON_BIT(type, rank) \
    ((u64)1 << ((unsigned)(type) * MMO_MON_RIBBON_RANKS + (unsigned)(rank)))

/* The record's trailing byte list, the one extension point it has. */
#define MMO_MON_TLV_RIBBONS_SUPER 1  /* 8 bytes, little-endian: the mask above */
/*
 * Where the monster was caught: three S16LE, region then bank then map, the server's own ids, 
 * the same triple a warp names.
 */
#define MMO_MON_TLV_CAUGHT_WHERE  2  /* 6 bytes: region, bank, map */

/* BattleOutcome (0xCF), c2s only: what an engine-run scene left each party member as. */
typedef struct {
    s64 id;
    u8  level;
    s32 exp;
    s16 hp;
    u16 move[4];
    u8  pp[4];
    u8  cond[MMO_MON_CONDITIONS]; /* contest conditions, in contest-type order */
    u8  sheen;
    u64 ribbons_super;            /* see MMO_MON_RIBBON_BIT */
} mmo_battle_mon_outcome;

void mmo_game_write_battle_outcome(mmo_wbuf *w,
                                   const mmo_battle_mon_outcome *mons, int n);

/* BagDelta (0xDD), c2s only: U16LE wire item id, S16LE signed count. */
void mmo_game_write_bag_delta(mmo_wbuf *w, u16 item, s16 delta);

/* MoneyDelta (0xDE), c2s only: S32LE signed amount. The engine's own
 * arithmetic paid or received this, a mart purchase, a sale, a script's
 * reward, through a wallet the session keeps as a display of the server's.
 * Shop-RPC traffic never arrives this way. */
void mmo_game_write_money_delta(mmo_wbuf *w, s32 delta);

/* RegisteredItem, both ways, one U16LE wire item id, 0 for none. c2s (0xDC)
 * reports what the bag screen registered to Y; s2c (0xDD) is the seat at
 * join, absolute: what it carries is what the character has, none included.
 * Returns the id read, or -1 for a short body. */
void mmo_game_write_registered_item(mmo_wbuf *w, u16 item);
int  mmo_game_read_registered_item(const u8 *body, size_t n, u16 *item);

/*
 * A WorldFlagTableResetPacket (0x0A, WorldFlagTableResetPacketCodec): a U8-count list of
 * U16LE-length-prefixed byte groups.
 */
int mmo_game_read_world_flag_reset(const u8 *body, size_t n,
                                   u8 *blocks, int block_cap,
                                   int *lens, int group_cap, int *count);

/* One bag stack from the ItemStacks snapshot (0x40) or a 0x42 update. */
typedef struct {
    s64 object_id; /* stack object id; item stacks end in MMO_ITEM_ENTITY_TAG */
    u16 item_id;   /* server item id (region*1000 + index; cross via idmap) */
    int quantity;
} mmo_item_stack;

/*
 * Read the bag ItemStacks snapshot (0x40). It rides the BattleSideParty wire shape (side S8,
 * replace U8, then a U16LE-prefixed list of monster records whose front-sprite field carries
 * the item id and back-sprite the quantity, itemStacksPacket).
 */
int mmo_game_read_item_stacks(const u8 *body, size_t n,
                              mmo_item_stack *out, int cap, int *total,
                              int *replace);

/*
 * Read a single-stack bag update (0x42). It rides the BattleSideAddPokemon wire shape (side
 * S8, then one monster record).
 */
int mmo_game_read_item_stack_update(const u8 *body, size_t n, mmo_item_stack *out);

/*
 * A LocalCharacterDelta (s2c 0x0C). The official client f/cd1: S16LE mask, then the groups that mask names.
 */
typedef struct {
    int has_money; /* 1 when mask bit 0x1 was set */
    s32 money;     /* absolute cash; meaningful when has_money */
} mmo_local_character_delta;
int mmo_game_read_local_character_delta(const u8 *body, size_t n,
                                        mmo_local_character_delta *out);

/* A shop catalog (s2c 0x23). The official client f/s21: S8 kind, -1 a close with no further body. */
#define MMO_SHOP_KIND_CLOSED  (-1)
#define MMO_SHOP_FLAG_TITLE    0x08
#define MMO_SHOP_FLAG_SUBTITLE 0x10
#define MMO_SHOP_FLAG_PARAM    0x20
#define MMO_SHOP_FLAG_NPC      0x40
#define MMO_SHOP_KIND_PAIRS    3   /* en0.nb: extras instead of a price */
#define MMO_SHOP_ITEM_MAX      64

typedef struct {
    u16 item_id;
    s16 stock;
    s32 price; /* 0 when kind is MMO_SHOP_KIND_PAIRS */
} mmo_shop_item;

typedef struct {
    int open;          /* 0 = close (kind -1), 1 = a catalog */
    int kind;          /* en0 type; meaningful when open */
    u8  flags;
    u8  currency_kind; /* KR byte; 0 and 1 are the named values */
    int has_param;
    s32 param;
    int has_title;
    s32 title_id;
    int has_subtitle;
    s32 subtitle_id;
    int has_npc;
    s64 npc_entity_id;
    int count;         /* items stored (<= cap) */
    int total;         /* items the packet declared */
} mmo_shop_catalog;

int mmo_game_read_shop_catalog(const u8 *body, size_t n,
                               mmo_shop_catalog *out,
                               mmo_shop_item *items, int cap);

/* One filled ROM string variable, from the tag-4 message arg. A DS line
 * addresses its variables by slot, so `slot` is the number in the
 * message's own {STRVAR_1 tag, slot, 0}, and `text` is what goes there
 * (Latin-1, the way every other name off this wire arrives). */
#define MMO_DIALOG_STRVAR_MAX   8  /* slots one box can fill */
#define MMO_DIALOG_STRVAR_CHARS 64 /* characters one of them holds */

typedef struct {
    int  slot;
    char text[MMO_DIALOG_STRVAR_CHARS + 1];
} mmo_dialog_strvar;

/*
 * Tag 4 is this project's own, not the official client's: a U8 string-variable slot and one UTF-16 name,
 * which is how this server sends what the DS `BufferPlayerName` and `BufferRivalName` commands
 * buffer. The official client's tags stop at 3.
 */
typedef struct {
    s8  flags;
    s8  action_type;
    s32 text_id;
    s64 entity_id;
    s32 context_value;
    int arg_count;
    int strvar_count;  /* tag-4 args held (<= MMO_DIALOG_STRVAR_MAX) */
    mmo_dialog_strvar strvar[MMO_DIALOG_STRVAR_MAX];
    int close;         /* 1 when action_type is MMO_DIALOG_ACTION_CLOSE */
    int detail_len;    /* unread tail after the args (0 on a 0x23/0x31 menu) */
    int choice_count;  /* 0 unless action_type is a 0x23 or 0x31 menu */
    s16 choice_bank;   /* 0 unless action_type is 0x31; the DS text bank */
    s16 choices[MMO_DIALOG_MENU_MAX];
} mmo_dialog_action;

int mmo_game_read_dialog_action(const u8 *body, size_t n,
                                mmo_dialog_action *out);

/* c2s 0x21 DialogActionResponse: the box's flags byte, then a response
 * byte. 0 advances a message; yes/no writes 1 or 0; a species menu
 * writes the 1-based choice (cancel is count+1); a text list writes
 * the 0-based choice (cancel is count). */
void mmo_game_write_dialog_reply(mmo_wbuf *w, u8 flags, u8 response);

/* c2s 0x22 EntityInteract: S64LE entity id, then S64LE token. the official client's
 * the official client writes the entity's hash as the token; the server reads
 * it and does not use it, so a client that has no hash writes 0. */
void mmo_game_write_entity_interact(mmo_wbuf *w, s64 entity_id, s64 token);

/* s2c 0x0E DialogState: one U8, 1 locks the avatar, 0 releases it. */
int mmo_game_read_dialog_state(const u8 *body, size_t n, int *active);

#define MMO_SCRIPT_MOVE_MAX 255

typedef struct {
    s64 entity_id;
    u8  flag;
    u8  count;
    u8  actions[MMO_SCRIPT_MOVE_MAX];
} mmo_script_move;

int mmo_game_read_script_move(const u8 *body, size_t n,
                              mmo_script_move *out);

/* Map one action byte for `flag` onto an engine MovementAction number
 * (generated/movement_actions.h). Returns that number, or -1 when the
 * byte is not one this client will play. flag != 1 is the m20=false
 * table the official client looks up against unk1=0. */
int mmo_game_script_action(u8 flag, u8 byte);

#define MMO_OBJECTIVE_MAX 255

typedef struct {
    s8  id;
    s32 value;
    s16 count;
} mmo_objective;

int mmo_game_read_objective(const u8 *body, size_t n, mmo_objective *out);
int mmo_game_read_objective_bulk(const u8 *body, size_t n,
                                 mmo_objective *out, int cap, int *count);

#define MMO_FRIEND_SPRITE_COUNT 4
#define MMO_FRIEND_MAX          64

typedef struct {
    s64 player;
    s32 unknown;
    u8  online;
    char name[MMO_CHAR_NAME_MAX + 1];
    u8  unk0;
    s32 last_seen;
    u8  kind;
    u8  packed_slots;
    s16 sprite[MMO_FRIEND_SPRITE_COUNT];
} mmo_friend;

int mmo_game_read_friend(const u8 *body, size_t n, mmo_friend *out);
int mmo_game_read_friend_list(const u8 *body, size_t n,
                              mmo_friend *out, int cap, int *count, u8 *mode);
int mmo_game_read_friend_delete(const u8 *body, size_t n, s64 *player);
int mmo_game_read_friend_online(const u8 *body, size_t n,
                                s64 *player, u8 *online);

/* c2s 0x62 / 0x63: one utf16 name, the way the official client
 * write it. */
void mmo_game_write_friend_name(mmo_wbuf *w, const char *name);

#define MMO_GUILD_NAME_MAX        32
#define MMO_GUILD_TAG_MAX         8
#define MMO_GUILD_MOTD_MAX        128
#define MMO_GUILD_RANK_MAX        6
#define MMO_GUILD_RANK_LABEL_MAX  16
#define MMO_GUILD_PERM_COUNT      5
#define MMO_GUILD_MEMBER_MAX      64
#define MMO_GUILD_LOG_MAX         32

typedef struct {
    char name[MMO_CHAR_NAME_MAX + 1];
    u8  unk0;
    s32 last_seen;
    u8  kind;
    u8  packed_slots;
    s16 sprite[MMO_FRIEND_SPRITE_COUNT];
} mmo_guild_appearance;

typedef struct {
    u8  rank;
    s64 entity_id;
    s32 joined_at;
    mmo_guild_appearance appearance;
    u8  online;
} mmo_guild_member;

typedef struct {
    s64 guild_id;
    char name[MMO_GUILD_NAME_MAX + 1];
    char tag[MMO_GUILD_TAG_MAX + 1];
    s32 founded_at;
    char message[MMO_GUILD_MOTD_MAX + 1];
    s32 unknown;
    s16 perm[MMO_GUILD_PERM_COUNT];
    s32 expiry;
    int rank_count;
    char rank_label[MMO_GUILD_RANK_MAX][MMO_GUILD_RANK_LABEL_MAX + 1];
} mmo_guild_profile;

typedef struct {
    u8  type;
    char actor[MMO_CHAR_NAME_MAX + 1];
    char target[MMO_CHAR_NAME_MAX + 1];
    s32 timestamp;
} mmo_guild_log_entry;

int mmo_game_read_guild_profile(const u8 *body, size_t n, mmo_guild_profile *out);
int mmo_game_read_guild_membership(const u8 *body, size_t n,
                                   int *in_guild, mmo_guild_profile *out);
int mmo_game_read_guild_member(const u8 *body, size_t n, mmo_guild_member *out);
int mmo_game_read_guild_members(const u8 *body, size_t n,
                                mmo_guild_member *out, int cap,
                                int *count, u8 *replace);
int mmo_game_read_guild_rank_change(const u8 *body, size_t n,
                                    s64 *member, u8 *rank);
int mmo_game_read_guild_member_drop(const u8 *body, size_t n, s64 *member);
int mmo_game_read_guild_presence(const u8 *body, size_t n,
                                 s64 *member, u8 *online);
int mmo_game_read_guild_log(const u8 *body, size_t n,
                            mmo_guild_log_entry *out, int cap,
                            int *count, s16 *total);

void mmo_game_write_guild_create(mmo_wbuf *w, const char *name, const char *tag);
void mmo_game_write_guild_invite(mmo_wbuf *w, const char *name);
void mmo_game_write_guild_leave(mmo_wbuf *w);
void mmo_game_write_guild_disband(mmo_wbuf *w, int initiate, s64 guild_id);
void mmo_game_write_guild_kick(mmo_wbuf *w, s64 member);
void mmo_game_write_guild_rank(mmo_wbuf *w, s64 member, u8 rank);
void mmo_game_write_guild_motd(mmo_wbuf *w, const char *text);
void mmo_game_write_guild_rank_label(mmo_wbuf *w, u8 rank, const char *label);
void mmo_game_write_guild_perms(mmo_wbuf *w, const s16 *masks);
void mmo_game_write_guild_log_req(mmo_wbuf *w, s16 page);

#define MMO_MAIL_SUBJECT_MAX 40
#define MMO_MAIL_BODY_MAX    2000
#define MMO_MAIL_MAX         250

typedef struct {
    s64 mail_id;
    s64 recipient_id;
    s64 sender_id;
    u8  staff_kind;
    char sender[MMO_CHAR_NAME_MAX + 1];
    char recipient[MMO_CHAR_NAME_MAX + 1];
    s32 sent_at;
    char subject[MMO_MAIL_SUBJECT_MAX + 1];
    char body[MMO_MAIL_BODY_MAX + 1];
    u8  unread;
    u8  has_attachments;
} mmo_mail;

int mmo_game_read_mail(const u8 *body, size_t n, int sent, int with_body,
                       mmo_mail *out);
int mmo_game_read_mail_page(const u8 *body, size_t n,
                            mmo_mail *out, int cap, int *count,
                            s16 *page, u8 *sent);
int mmo_game_read_mail_detail(const u8 *body, size_t n, mmo_mail *out,
                              int *present, u8 *sent);
int mmo_game_read_mail_counts(const u8 *body, size_t n,
                              s16 *inbox, s16 *inbox_seen, s16 *sent);
int mmo_game_read_mail_result(const u8 *body, size_t n, u8 *code);

void mmo_game_write_mail_compose(mmo_wbuf *w, const char *recipient,
                                 const char *subject, const char *body);
void mmo_game_write_mail_page_req(mmo_wbuf *w, s16 page, int sent);
void mmo_game_write_mail_detail_req(mmo_wbuf *w, s64 mail_id);
void mmo_game_write_mail_delete(mmo_wbuf *w, s64 mail_id, s16 page);

#define MMO_LINK_MAX 4

typedef struct {
    s64 entity_id;
    char name[MMO_CHAR_NAME_MAX + 1];
    u8  unk0;
    s32 last_seen;
    u8  kind;
    u8  packed_slots;
    s16 sprite[MMO_FRIEND_SPRITE_COUNT];
} mmo_link_member;

int mmo_game_read_link_member(const u8 *body, size_t n, mmo_link_member *out);
int mmo_game_read_link_snapshot(const u8 *body, size_t n,
                                mmo_link_member *out, int cap, int *count,
                                int *present, s64 *leader);
int mmo_game_read_link_remove(const u8 *body, size_t n,
                              s64 *removed, s64 *leader);
int mmo_game_read_link_leader(const u8 *body, size_t n, s64 *leader);

void mmo_game_write_link_invite(mmo_wbuf *w, const char *name);
void mmo_game_write_link_id(mmo_wbuf *w, s64 entity_id);

/*
 * HUD menus and prompts outside dialog. the official client's Ew1 has two values (0 and 1); any other type
 * throws in W5, so a reader refuses it.
 */
#define MMO_UI_MENU_TYPES  2
#define MMO_UI_NAME_MAX    16
#define MMO_UI_OPTION_MAX  32
#define MMO_UI_ROW_MAX     32
#define MMO_UI_LABEL_MAX   64
#define MMO_UI_PAGE_MAX    256

typedef struct {
    int enabled;               /* 1 if the HUD menu is shown */
    s8  menu_type;             /* Ew1; only valid when enabled */
} mmo_menu_visibility;

typedef struct {
    s8  menu_type;             /* Ew1 0 or 1 */
    int present;               /* 1 if the page blob follows */
    int len;
    u8  bytes[MMO_UI_PAGE_MAX];
} mmo_menu_page;

typedef struct {
    s64 entity_id;
    char name[MMO_CHAR_NAME_MAX + 1];
} mmo_ui_name;

typedef struct {
    s8  kind;
    int flag;
    int count;
    mmo_ui_name entry[MMO_UI_NAME_MAX];
} mmo_name_choices;

typedef struct {
    s8  type_id;
    s8  sub_type;
    s16 value[5];
} mmo_ui_option;

typedef struct {
    s8  kind;
    int count;
    mmo_ui_option entry[MMO_UI_OPTION_MAX];
} mmo_option_list;

typedef struct {
    s8  row_type;
    s16 value[5];
    char label[MMO_UI_LABEL_MAX + 1];
    s16 extra;
} mmo_ui_row;

typedef struct {
    s32 window_id;
    int first_page;
    int last_page;
    s8  header[3];
    int count;
    mmo_ui_row row[MMO_UI_ROW_MAX];
} mmo_list_window;

typedef struct {
    int visible;
    s64 entity_id;
    u16 request_s;
    u16 response_s;
} mmo_confirm_prompt;

typedef struct {
    s8  kind;
    s8  prompt_type;           /* Yr byte; 0 when the kind has none */
    s16 value;                 /* kinds 3..5; 0 otherwise */
    int relation_count;        /* kind 6; the rows are walked and dropped */
} mmo_menu_prompt;

int mmo_game_read_menu_visibility(const u8 *body, size_t n,
                                  mmo_menu_visibility *out);
int mmo_game_read_menu_page(const u8 *body, size_t n, mmo_menu_page *out);
int mmo_game_read_name_choices(const u8 *body, size_t n,
                               mmo_name_choices *out);
int mmo_game_read_option_list(const u8 *body, size_t n, mmo_option_list *out);
int mmo_game_read_list_window(const u8 *body, size_t n, mmo_list_window *out);
int mmo_game_read_confirm_prompt(const u8 *body, size_t n,
                                 mmo_confirm_prompt *out);
int mmo_game_read_menu_prompt_open(const u8 *body, size_t n,
                                   mmo_menu_prompt *out);
int mmo_game_read_menu_prompt_close(const u8 *body, size_t n, s8 *kind);
int mmo_game_read_view_scale(const u8 *body, size_t n, s8 *scale);

typedef struct {
    u16 bit_count;
    u16 len;
    const u8 *bytes;
} mmo_digest;

typedef struct {
    s64 id;
    s32 size;
    u16 sig_len;
    const u8 *sig;
} mmo_transfer_begin;

typedef struct {
    u16 len;
    const u8 *data;
} mmo_transfer_append;

typedef struct {
    s64 id;
    int last;
    u16 len;
    const u8 *data;
} mmo_stream_chunk;

typedef struct {
    s8  ctrl;
    int present;
    s8  image_type;
    u8  chunk_index;
    int last;
    u16 len;
    const u8 *data;
} mmo_image_chunk;

int mmo_game_read_digest(const u8 *body, size_t n, mmo_digest *out);
int mmo_game_read_transfer_begin(const u8 *body, size_t n,
                                 mmo_transfer_begin *out);
int mmo_game_read_transfer_append(const u8 *body, size_t n,
                                  mmo_transfer_append *out);
int mmo_game_read_stream_chunk(const u8 *body, size_t n,
                               mmo_stream_chunk *out);
int mmo_game_read_image_chunk(const u8 *body, size_t n,
                              mmo_image_chunk *out);

/* Xor the wired bytes with the official client's the official client key, in place. */
void mmo_game_sync_xor(u8 *p, size_t n);

/* Xor then gzip-inflate a finished 0xAB+0xAC payload. Returns 0 and
 * writes *out_n, or -1 if the stream is not gzip or does not fit. */
int mmo_game_transfer_open(const u8 *wired, size_t n, u8 *out, size_t cap,
                           size_t *out_n);

/* c2s 0xA6 / 0xA7 with a zero count: we hold no local overlay records. */
void mmo_game_write_digest_empty(mmo_wbuf *w);

/* c2s 0x23 ExchangeItemRequest: item S16LE, quantity S16LE, one type byte. */
void mmo_game_write_shop_buy(mmo_wbuf *w, s16 item_id, s16 quantity);

/* c2s 0x24 ShopSellRequest: the bag stack's entity id as S64LE, then
 * quantity S16LE. */
void mmo_game_write_shop_sell(mmo_wbuf *w, s64 item_entity_id, s16 quantity);

/* c2s 0x26 DialogOption: use wire item `item_id` on monster `target`.
 * `trailer` is the four unidentified bytes; pass MMO_ITEM_USE_TRAILER
 * until another capture names them. */
void mmo_game_write_item_use(mmo_wbuf *w, u16 item_id, s64 target,
                             s32 trailer);

/* --- monster containers (0x13) ------------------------------------------- * */

/* Container kinds, as the wire byte names them. The client maps the byte through
 * its own container table; only the two the client sizes explicitly are named
 * here (its party table holds 6, its PC table 660). */
#define MMO_CONTAINER_PC    0
#define MMO_CONTAINER_PARTY 1

/* Moves per record: four slots always go on the wire, empty ones included. */
#define MMO_MON_MOVES 4

/*
 * Stats per record, in the order the record itself uses: HP, attack, defense, speed, special
 * attack, special defense.
 */
#define MMO_MON_STATS 6

/* Natures are derived, not sent: the client computes `nature = seed % 25` and
 * looks the result up in a 25-entry table whose order is the usual one (0 hardy,
 * 1 lonely, … 24 quirky), the same numbering the engine uses. */
#define MMO_MON_NATURES 25

/*
 * Ability slots. 0 and 1 are the species' two ordinary abilities; 2 is the hidden ability,
 * which is a post-Gen-4 concept the engine cannot draw.
 */
#define MMO_ABILITY_SLOT_HIDDEN   2
#define MMO_CONTAINER_HIDDEN_OK   0x9210u  /* containers 4, 9, 12 and 15 */

/* Trainer and nickname buffers. The engine's own name cap is 7 characters; the
 * wire is not bounded by that, so a longer name is truncated here rather than
 * refused. */
#define MMO_MON_NAME 32

/* One monster record. Named fields are the ones with an established meaning; the
 * rest are crossed at their own width and dropped (see PokemonCodec's notes on
 * why a record is never walked as opaque runs). */
typedef struct {
    s64 id;                      /* the monster's own id; the client keys on it */
    s64 owner_id;
    int container;               /* the record's own container byte */
    int slot;                    /* containerSlot */
    u16 dex_id;                  /* server National Dex (cross via the id map) */
    s32 seed;
    char ot[MMO_MON_NAME];       /* original trainer */
    char nickname[MMO_MON_NAME]; /* empty when the species name is shown */
    int level;
    int hp;                      /* current HP */
    s32 xp;
    u16 move_id[MMO_MON_MOVES];  /* server move ids; 0 = empty slot */
    u8  move_pp[MMO_MON_MOVES];
    u8  move_pp_up[MMO_MON_MOVES]; /* PP Ups applied, 0..3 (two bits per slot) */
    u8  ev[MMO_MON_STATS];       /* effort, in the stat order above */
    u8  iv[MMO_MON_STATS];       /* the word below, unpacked, same stat order */
    u8  cond[MMO_MON_CONDITIONS]; /* contest conditions, in contest-type order */
    u8 sheen;                   /* poffin saturation; the one byte official drops */
    u64 ribbons_super;           /* Super Contest ribbons; see MMO_MON_RIBBON_BIT */
    s32 iv_bits;                 /* packed 30-bit word, six 5-bit stats */
    int nature;                  /* 0..24, derived from `seed` (not on the wire) */
    int ability_slot;            /* 0, 1, or MMO_ABILITY_SLOT_HIDDEN */
    int friendship;              /* 0..255; the summary shows it as a percentage */
    int form;                    /* alternate forme, 0 for the ordinary one */
    u16 rarity;                  /* shiny/hidden-ability/alpha/... bit set */
    s32 caught_at;               /* unix seconds */
    /* Where it was caught, as the server holds it. -1 in all three for a record
     * that carries no such tail. See MMO_MON_TLV_CAUGHT_WHERE. */
    int caught_region;
    int caught_bank;
    int caught_map;
    int egg;
} mmo_monster;

/* Rarity bits, checked against the game client's own accessors. */
#define MMO_RARITY_SHINY          0x0001
#define MMO_RARITY_HIDDEN_ABILITY 0x0002
#define MMO_RARITY_ALPHA          0x0004
#define MMO_RARITY_SECRET_SHINY   0x0008
#define MMO_RARITY_FATEFUL        0x0010

/*
 * A PokemonContainerPacket (0x13). `has_change` asks the client to replace the container
 * before applying the records; without it the records are merged into what is already there,
 * keyed by monster id.
 */
typedef struct {
    int container;        /* the wire container byte (MMO_CONTAINER_*) */
    int has_change;
    int deleted;
    int has_unknown_word; /* flags bit 2: the word below was present */
    u16 unknown_word;
    int total;            /* records on the wire */
    int count;            /* records stored (<= cap) */
    int trailing;         /* bytes past the last record the client reads */
} mmo_pokemon_container;

/* Read a monster-container packet, storing the first `cap` records into `mons`.
 * Returns 0, or -1 on a body that runs short, or on a record count or trailing
 * list length above 127: the client sizes both arrays from a *signed* byte, so
 * either one throws there and the character never finishes loading. */
int mmo_game_read_pokemon_container(const u8 *body, size_t n,
                                    mmo_pokemon_container *out,
                                    mmo_monster *mons, int cap);

/* A SocialListEntryAddPacket (0x14): one monster record, no container
 * wrapper. The catch path sends it so the client can name the monster
 * when the ball throw lands. Returns 0, or -1 on a body the record
 * walk cannot cross. */
int mmo_game_read_monster(const u8 *body, size_t n, mmo_monster *out);

/* --- the duel, and the link battle it opens ------------------------------ * */
typedef struct {
    int  flags;
    int  request_type;
    char name[MMO_MON_NAME];
} mmo_duel_invite;

int mmo_game_read_duel_invite(const u8 *body, size_t n, mmo_duel_invite *out);

/* InGameChallengeResponse (c2s 0x16): U8 declined (0 = accepted), then a reply
 * line. The line is the official client's; this client sends it empty. */
void mmo_game_write_duel_response(mmo_wbuf *w, int accepted, const char *text);

/* DuelInviteOutcome (s2c 0x51): one byte. The values are ours, not measured:
 * 0 declined, 1 accepted, 2 lapsed. */
#define MMO_DUEL_DECLINED 0
#define MMO_DUEL_ACCEPTED 1
#define MMO_DUEL_LAPSED   2

int mmo_game_read_duel_outcome(const u8 *body, size_t n, int *out_packed);

/*
 * LinkBattleOpen (s2c 0xC6): S32LE battle id, U8 net id, the opponent's name, then a
 * U8-counted list of monster records in the same shape a container carries. The party on the
 * wire is the *other* player's; each client already holds its own.
 */
typedef struct {
    int  battle_id;
    int  net_id;
    char peer_name[MMO_MON_NAME];
    int  peer_gender; /* 0 male, 1 female: the sprite the other side is drawn as */
    int  total;   /* records on the wire */
    int  count;   /* records stored (<= cap) */
} mmo_link_battle_open;

int mmo_game_read_link_battle_open(const u8 *body, size_t n,
                                   mmo_link_battle_open *out,
                                   mmo_monster *mons, int cap);

/*
 * LinkBattleData (bidi 0xC7): S32LE battle id, U8 kind, U16LE length, bytes. The payload is
 * the engine's own link traffic and is not read by either the server or this codec, it
 * crosses verbatim.
 */
#define MMO_LINK_BLOB_MAX 512

/* The kinds, matching LinkBattleDataPacket's companion. */
#define MMO_LINK_KIND_MESSAGE 0 /* a BattleMessageInfo and its body */
#define MMO_LINK_KIND_SYNC    1 /* a CommTiming sync tag: one byte */
#define MMO_LINK_KIND_LEAVE   2 /* the sender is gone */
#define MMO_LINK_KIND_RESULT  3 /* the scene ended: one byte, the result mask */
#define MMO_LINK_KIND_ENDWAIT 4 /* the computing side's "the fight is over":
                                 * the engine's comm command 22, re-carried.
                                 * No payload. */

typedef struct {
    int battle_id;
    int kind;
    int len;
    u8  data[MMO_LINK_BLOB_MAX];
} mmo_link_battle_data;

int mmo_game_read_link_battle_data(const u8 *body, size_t n,
                                   mmo_link_battle_data *out);

void mmo_game_write_link_battle_data(mmo_wbuf *w, int battle_id, int kind,
                                     const u8 *data, int len);

/* --- the direct trade ---------------------------------------------------- * */
#define MMO_TRADE_ACTION_CANCEL  0
#define MMO_TRADE_ACTION_ACCEPT  1
#define MMO_TRADE_ACTION_CONFIRM 2

/*
 * TradeState (s2c 0x6A, ours): S8 state, S8 role, S8 peer gender, then a name only OPEN and
 * DONE fill.
 */
#define MMO_TRADE_STATE_OPEN      1 /* both agreed; the table is open */
#define MMO_TRADE_STATE_PEER_OK   2 /* the peer confirmed the standing pair */
#define MMO_TRADE_STATE_CANCELLED 3 /* declined, cancelled, lapsed, or gone */
#define MMO_TRADE_STATE_DONE      4 /* the swap is written; the party follows */

typedef struct {
    int  state;
    int  role;        /* 0 or 1: this side's net id at the table */
    int  peer_gender; /* 0 male, 1 female */
    char peer[MMO_MON_NAME];
} mmo_trade_state;

int mmo_game_read_trade_state(const u8 *body, size_t n, mmo_trade_state *out);

void mmo_game_write_trade_action(mmo_wbuf *w, int action);

/* TradeSelectMon (c2s 0x52): S32LE party slot. The peer's pick arrives as a
 * bare monster record on s2c 0x52, mmo_game_read_monster reads it. */
void mmo_game_write_trade_select(mmo_wbuf *w, int slot);

/*
 * TradeComm (bidi 0xBF, ours): U8 channel, S16LE command, U16LE-counted bytes. One relayed
 * message of the engine's own trade scene, a comm command (channel 0) or a timing-sync marker
 * (channel 1, the sync number in `cmd`).
 */
#define MMO_TRADE_COMM_MAX 4096

#define MMO_TRADE_CHANNEL_COMMAND 0
#define MMO_TRADE_CHANNEL_SYNC    1

typedef struct {
    int channel;
    int cmd;
    int len;
    u8  data[MMO_TRADE_COMM_MAX];
} mmo_trade_comm;

int mmo_game_read_trade_comm(const u8 *body, size_t n, mmo_trade_comm *out);

void mmo_game_write_trade_comm(mmo_wbuf *w, int channel, int cmd,
                               const u8 *data, int len);

/* --- the global trade link ----------------------------------------------- * */
#define MMO_GTL_KIND_POKEMON 0
#define MMO_GTL_KIND_ITEM    1
#define MMO_GTL_KIND_OWN     2

/* Where an own listing stands (the own-page row tail's state byte). */
#define MMO_GTL_ST_ACTIVE 0
#define MMO_GTL_ST_SOLD   1
#define MMO_GTL_ST_CLOSED 2

/* The official client pages ten rows at a time; the captures never carry more. */
#define MMO_GTL_PAGE_ROWS 10

/* What a monster search narrows by. Zero species and -1 elsewhere mean "not
 * set"; species cross the wire in the server's own id space, so map with
 * mmo_id_species_to_server before filling one in. */
typedef struct {
    u16 species;    /* 0 = no species filter */
    int min_level;  /* -1 = unset */
    int max_level;  /* -1 = unset */
    int shiny;      /* -1 = unset, else 0 or 1 */
    int nature;     /* -1 = unset, else 0..24 */
    s32 min_price;  /* -1 = unset */
    s32 max_price;  /* -1 = unset */
} mmo_gtl_search;

/* How a search page is ordered: the request's category byte. */
#define MMO_GTL_SORT_NEWEST     0
#define MMO_GTL_SORT_OLDEST     1
#define MMO_GTL_SORT_PRICE_ASC  2
#define MMO_GTL_SORT_PRICE_DESC 3

void mmo_game_write_gtl_open(mmo_wbuf *w, s64 session_ts);
void mmo_game_write_gtl_search_req(mmo_wbuf *w, int request_id, int kind,
                                   int sort, int page,
                                   const mmo_gtl_search *filter);
void mmo_game_write_gtl_create_mon(mmo_wbuf *w, s64 mon_id, s32 price);
void mmo_game_write_gtl_create_item(mmo_wbuf *w, u16 item_id, s32 quantity,
                                    s32 price);
void mmo_game_write_gtl_cancel(mmo_wbuf *w, s64 listing_id);
void mmo_game_write_gtl_buy(mmo_wbuf *w, s64 listing_id, int quantity);
void mmo_game_write_gtl_claim(mmo_wbuf *w, const s64 *listing_ids, int count);
void mmo_game_write_gtl_price(mmo_wbuf *w, s64 listing_id, s32 new_price);
void mmo_game_write_gtl_market_buy(mmo_wbuf *w, u16 item_id, int quantity,
                                   s32 budget);

/* CategoryFlags (s2c 0xDC): U8 entry kind, S64LE timestamp, U16LE-counted flag
 * bytes. The flags are official category switches this client has no categories
 * for; only their arrival means anything, the session is open. */
typedef struct {
    int entry_kind;
    s64 timestamp_ms;
    int flag_count;
} mmo_gtl_flags;

int mmo_game_read_gtl_flags(const u8 *body, size_t n, mmo_gtl_flags *out);

/* One search-page row. Every row names its own kind: an item row is two fixed
 * fields, a monster row is a whole record plus the six-stat strip the board
 * shows (EV wire order: hp, atk, def, spd, spAtk, spDef, and stats[0] repeats
 * the record's own hp). */
typedef struct {
    s64 listing_id;
    int kind;         /* MMO_GTL_KIND_POKEMON or _ITEM: the row's own byte */
    s32 price;
    s32 listed_at;    /* unix seconds */
    s32 expires_at;   /* unix seconds */
    int quantity;
    u16 item_id;      /* item rows */
    int item_state;   /* item rows */
    int have_mon;     /* monster rows: the record flag byte */
    mmo_monster mon;  /* monster rows, when have_mon */
    s16 stats[MMO_MON_STATS];
    /* Own-page rows only (the page kind is OWN): where the listing stands. */
    int own_state;     /* MMO_GTL_ST_* */
    int own_remaining; /* units still up for sale */
    int own_unclaimed; /* sold units whose money waits for Claim */
} mmo_gtl_row;

/*
 * GtlSearchPage (s2c 0x9B): S8 request id (echoed), U8 page kind, S16LE page, S32LE total
 * matches, U8-counted rows, then, only when the PAGE kind is ITEM, a U8-counted quote strip
 * of (S16LE item, S32LE price).
 */
typedef struct {
    int request_id;
    int kind;        /* the page's kind byte, MMO_GTL_KIND_* */
    int page;
    s32 total;
    int count;
    mmo_gtl_row rows[MMO_GTL_PAGE_ROWS];
    int quote_count;
    struct {
        u16 item_id;
        s32 price;
    } quotes[MMO_GTL_PAGE_ROWS];
} mmo_gtl_page;

int mmo_game_read_gtl_page(const u8 *body, size_t n, mmo_gtl_page *out);

/* GtlResult (s2c 0xAF, ours): one shelf verb's answer. The code names the
 * toast the window shows; `a` and `b` fill its blanks where the code has any
 * (a sold-notice carries what and for how much, a refusal the floor). */
#define MMO_GTL_R_LISTED         1
#define MMO_GTL_R_CAP            2
#define MMO_GTL_R_BOUGHT         4
#define MMO_GTL_R_GONE           5
#define MMO_GTL_R_OWN            6
#define MMO_GTL_R_CANCELED       7
#define MMO_GTL_R_UNSETTLED      8
#define MMO_GTL_R_PRICE_CHANGED  9
#define MMO_GTL_R_PRICE_COOLDOWN 10
#define MMO_GTL_R_CLAIMED        11
#define MMO_GTL_R_MIN_PRICE      12
#define MMO_GTL_R_NO_FUNDS       13
#define MMO_GTL_R_NO_ROOM        15
#define MMO_GTL_R_SOLD_ONE       20
#define MMO_GTL_R_SOLD_MANY      21
#define MMO_GTL_R_PRICE_FLOOR    22

typedef struct {
    int code;
    s64 a;
    s32 b;
} mmo_gtl_result;

int mmo_game_read_gtl_result(const u8 *body, size_t n, mmo_gtl_result *out);

/* One trade-log fill. The type packs direction and kind: 0 sold a monster,
 * 1 sold an item stack, 2 bought a monster, 3 bought an item stack. */
typedef struct {
    s64 sale_id;
    int type;
    int what;     /* dex or item id, the row's frame A */
    int amount;   /* item rows the quantity, monster rows the level */
    s32 total;    /* what the whole fill went for */
    int bought;   /* the frame flag: this character was the buyer */
    s32 epoch;    /* when, unix seconds */
} mmo_gtl_log_row;

#define MMO_GTL_LOG_ROWS 30

typedef struct {
    int count;
    mmo_gtl_log_row rows[MMO_GTL_LOG_ROWS];
} mmo_gtl_log;

/* SceneObjectStates (s2c 0x5E) as the trade log's answer: per fill, frame 0
 * carries what/amount/total/direction and frame 1 the moment.
 * Rows past MMO_GTL_LOG_ROWS are walked and dropped. */
int mmo_game_read_gtl_log(const u8 *body, size_t n, mmo_gtl_log *out);

/* --- the Underground, and talking to someone in it ----------------------- * */

/* A conversation's commands are small, the widest is the trainer-case record
 * exchange (command 82), an UndergroundRecord and a net id in front of it. 512
 * is well past that and leaves the kind room to grow without truncating one. */
#define MMO_UG_TALK_MAX 512

/* Kinds. c2s, s2c or both, as named. */
#define MMO_UG_TALK_REQUEST 0 /* c2s: A was pressed facing entity_id */
#define MMO_UG_TALK_STATE   1 /* c2s: data[0] is this player's availability */
#define MMO_UG_TALK_DATA    2 /* bidi: data[0] is a comm command id, rest its body */
#define MMO_UG_TALK_RESULT  3 /* s2c: data[0] a TalkResult, data[1] a role */
#define MMO_UG_TALK_END     4 /* bidi: the conversation is over */

/* MMO_UG_TALK_END's body, where it has one. */
#define MMO_UG_END_DONE    0
#define MMO_UG_END_REFUSED 1

/* MMO_UG_TALK_STATE's byte. The engine reads the first two off its own
 * CommPlayerMan, which no other party can see. */
#define MMO_UG_STATE_FREE   0
#define MMO_UG_STATE_BUSY   1 /* a menu, a step in flight, a trap, or already talking */
#define MMO_UG_STATE_MINING 2 /* inside the mining game, which has its own refusal */

/* MMO_UG_TALK_RESULT's first byte: the engine's own enum TalkResult. */
#define MMO_UG_TALK_SUCCESS 1
#define MMO_UG_TALK_FAIL    2 /* "That person seems occupied." */
#define MMO_UG_TALK_BUSY_MINING 4 /* "I'm digging right now!" */

/* MMO_UG_TALK_RESULT's second byte: which end of a successful pairing we are. */
#define MMO_UG_ROLE_INITIATOR 0 /* pressed A; runs UndergroundTalk_Start */
#define MMO_UG_ROLE_RESPONDER 1 /* was faced; runs UndergroundTalkResponse_Start */

typedef struct {
    int kind;
    s64 entity_id;
    int len;
    u8  data[MMO_UG_TALK_MAX];
} mmo_underground_talk;

int mmo_game_read_underground_talk(const u8 *body, size_t n,
                                   mmo_underground_talk *out);

void mmo_game_write_underground_talk(mmo_wbuf *w, int kind, s64 entity_id,
                                     const u8 *data, int len);

/* --- matchmaking, tournaments and score boards --------------------------- * */

#define MMO_TOURNEY_NAME_MAX   48
#define MMO_RENTAL_MAX          6
#define MMO_TOURNEY_MAX        16
#define MMO_MATCHUP_MAX        64
#define MMO_BOARD_ROW_MAX      16
#define MMO_BOARD_MON_MAX       6
#define MMO_BOARD_MON_SLOTS     4

/* One rental preview: the two shorts the official client seats onto a bare k91 when the
 * offer is a preview rather than a full record. */
typedef struct {
    s16 species;
    s16 level;
} mmo_rental_preview;

typedef struct {
    s8  mode;
    s8  param[3];
    int preview_count;                        /* mode 2 */
    mmo_rental_preview preview[MMO_RENTAL_MAX];
    int monster_count;                        /* mode 0 */
    mmo_monster monster[MMO_RENTAL_MAX];
} mmo_rentals;

/*
 * One tournament, as the official client's JT1 holds it. `kind` and `type` index its own text banks (9200 +
 * kind, 9225 + type) and `start_time` is handed to a date formatter, so those three are
 * established; `s32_a` and `u8_a` are read and carried without one.
 */
typedef struct {
    s64 id;
    s8  kind;
    s8  type;
    char name[MMO_TOURNEY_NAME_MAX + 1];
    s16 capacity;
    s16 format;                /* the client refuses a format its own tier disagrees with */
    s64 start_time;
    s8  mode;
    s32 s32_a;
    s8  u8_a;
    int flag_a;
    int flag_b;
} mmo_tourney;

typedef struct {
    s8  tab;
    int second_list;           /* the boolean; picks the window's other tab */
    s16 page;
    s32 total;
    int count;
    mmo_tourney entry[MMO_TOURNEY_MAX];
} mmo_tourney_page;

typedef struct {
    s64 tourney_id;
    s16 entered;
    s16 checked_in;
} mmo_tourney_count;

/* One bracket slot. `winner` is an index into the tournament's entrant list,
 * -1 when the match has none; `entrant` are two more of the same, present
 * only when the type byte is not 0. The slot's own position in the list is
 * its index, it is not on the wire. */
typedef struct {
    s16 winner;
    s8  type;
    s64 id;
    int has_entrants;
    s16 entrant[2];
} mmo_matchup;

typedef struct {
    int count;
    mmo_matchup entry[MMO_MATCHUP_MAX];
} mmo_matchups;

/* One board row's monster, as the official client's Prn holds it: a name, a byte, an int,
 * a marking byte, and one short per stat slot carrying a 10-bit value and a
 * 6-bit second field, with 1023 and 63 meaning absent. */
typedef struct {
    char name[MMO_CHAR_NAME_MAX + 1];
    s8  u8_a;
    s32 s32_a;
    s8  u8_b;
    s16 value[MMO_BOARD_MON_SLOTS];
    s8  extra[MMO_BOARD_MON_SLOTS];
    s8  packed[MMO_BOARD_MON_SLOTS];
} mmo_board_mon;

typedef struct {
    s64 entity_id;
    s32 rank;
    s32 score;
    int monster_count;
    mmo_board_mon monster[MMO_BOARD_MON_MAX];
} mmo_board_row;

typedef struct {
    s8  category;
    int total;
    int count;
    mmo_board_row row[MMO_BOARD_ROW_MAX];
} mmo_score_board;

int mmo_game_read_rentals(const u8 *body, size_t n, mmo_rentals *out);
int mmo_game_read_tourney_page(const u8 *body, size_t n, mmo_tourney_page *out);
int mmo_game_read_tourney_count(const u8 *body, size_t n,
                                mmo_tourney_count *out);
int mmo_game_read_matchups(const u8 *body, size_t n, mmo_matchups *out);
int mmo_game_read_score_board(const u8 *body, size_t n, mmo_score_board *out);

/* The three queue actions the official client's f/sz0 can carry, as the wire byte names
 * them. 0 selects a tier, 1 names a target and a slot, 2 asks for the
 * teleport; the tail is a different length for each. */
#define MMO_QUEUE_ACTION_TIER     0
#define MMO_QUEUE_ACTION_TARGET   1
#define MMO_QUEUE_ACTION_TELEPORT 2

/* c2s 0x49 / 0x47 / 0x6B / 0x44 / 0x7B: all five are an opcode and an empty
 * body. The opcode is the frame's, so the body writer is the same one. */
void mmo_game_write_empty_request(mmo_wbuf *w);

/* c2s 0x4C: one action byte, then the tail its kind takes. */
void mmo_game_write_queue_action_tier(mmo_wbuf *w, s32 tier_id);
void mmo_game_write_queue_action_target(mmo_wbuf *w, s64 target, s16 slot);
void mmo_game_write_queue_action_teleport(mmo_wbuf *w);

/* c2s 0x6C: a U8 count then that many language bytes. */
void mmo_game_write_queue_langs(mmo_wbuf *w, const s8 *langs, int count);

/* c2s 0x4A: the party slot, then the tier byte. */
void mmo_game_write_tier_select(mmo_wbuf *w, s8 slot, s8 tier);

/*
 * c2s 0x48: the signup, in the two shapes the official client writes. A count of queues and the party each
 * is entered with, or a leading 0 and a tournament id.
 */
int mmo_game_write_queue_signup(mmo_wbuf *w, const s8 *queues,
                                const s8 *slots, int count);
void mmo_game_write_tourney_signup(mmo_wbuf *w, s64 tourney_id, s8 slot);

/* c2s 0x75: the tournament id byte, whether it is the open one, and the tab. */
void mmo_game_write_tourney_view(mmo_wbuf *w, s8 tourney_id, int active,
                                 s16 tab);

/* c2s 0xC1: register when non-zero, withdraw when 0. */
void mmo_game_write_tourney_register(mmo_wbuf *w, int register_);

/* c2s 0x74: one board category byte. */
void mmo_game_write_score_board_req(mmo_wbuf *w, s8 category);


/* --- staff panels, notes and moderation --------------------------------- * */

#define MMO_GM_TEXT_MAX      63   /* a panel label, an address, a note */
#define MMO_GM_CHAR_MAX      16   /* character rows on a lookup */
#define MMO_GM_STATUS_MAX    32   /* trailing status bytes on the session */
#define MMO_GM_PANEL_ROW_MAX  8
#define MMO_GM_PANEL_OPT_MAX  8
#define MMO_GM_PAIR_MAX      32

/* Kept scalars on the session record, by type. Their meanings are not
 * established: the official client stores them under obfuscated names and the panel that
 * prints them is not in the group. They are carried in wire order. */
#define MMO_GM_SESSION_S32   7
#define MMO_GM_SESSION_S16   8
#define MMO_GM_SESSION_S8   11

/*
 * jK1 id, utf16 name, utf16 secondary, i32[0], i8[0], i32[1], s64, i32[2], (s32 dropped), (s8
 * dropped), i32[3], i32[4], i16[0], i32[5], i8[1], i8[2], (s8 dropped), i8[3], (s32 dropped),
 * (s8 dropped x8), i16[1], i8[4], i32[6], i8[5], i8[6], i8[7], (s8 dropped x3), i8[8], i8[9],
 * i8[10], (s8 dropped), i16[2], i16[3], (s8 dropped x2), i16[4], i16[5], status, i16[6],
 * i16[7], u8 count, count x s8
 */
typedef struct {
    s64 entity_id;
    char name[MMO_GM_TEXT_MAX + 1];
    char secondary[MMO_GM_TEXT_MAX + 1];
    s64 s64_a;
    s32 i32[MMO_GM_SESSION_S32];
    s16 i16[MMO_GM_SESSION_S16];
    s8  i8[MMO_GM_SESSION_S8];
    s8  status;
    int status_count;
    s8  status_list[MMO_GM_STATUS_MAX];
} mmo_gm_session;

/* One character row on a lookup. the official client reads two strings per row: the
 * second is the constructor's name and the first is assigned over the top
 * of a different field, so both are on the wire and both are kept. */
typedef struct {
    s64 entity_id;
    s32 value;
    char name_a[MMO_GM_TEXT_MAX + 1];
    char name_b[MMO_GM_TEXT_MAX + 1];
} mmo_gm_character;

typedef struct {
    int found;
    mmo_gm_session session;
    s8  rank;
    int has_account;
    s8  rank_extra[3];
    char address[MMO_GM_TEXT_MAX + 1];
    s64 playtime;
    s32 account_id;
    char account_a[MMO_GM_TEXT_MAX + 1];
    char account_b[MMO_GM_TEXT_MAX + 1];
    char account_c[MMO_GM_TEXT_MAX + 1];
    int has_detail;
    s32 detail_value;             /* read and dropped by the official client */
    char detail_text[MMO_GM_TEXT_MAX + 1];
    int count;
    mmo_gm_character entry[MMO_GM_CHAR_MAX];
} mmo_gm_lookup;

/* The nine panel bodies s2c 0xA2 can carry, as the wire byte names them.
 * the official client matches the byte against an enum and falls back to the 0 case, so a
 * byte outside this set is a body of no bytes rather than a refusal. The
 * three empty ones are 0, 31 and 100. */
#define MMO_GM_PANEL_EMPTY_A      0
#define MMO_GM_PANEL_NOTE         1
#define MMO_GM_PANEL_TARGET       2
#define MMO_GM_PANEL_EMPTY_B     31
#define MMO_GM_PANEL_DETAIL      40
#define MMO_GM_PANEL_SUMMARY     41
#define MMO_GM_PANEL_EMPTY_C    100
#define MMO_GM_PANEL_TITLED_NOTE 101
#define MMO_GM_PANEL_MENU       102

typedef struct {
    char label[MMO_GM_TEXT_MAX + 1];
    int option_count;
    char option[MMO_GM_PANEL_OPT_MAX][MMO_GM_TEXT_MAX + 1];
    s8  option_kind[MMO_GM_PANEL_OPT_MAX];
} mmo_gm_panel_row;

/*
 * 1 id, text_b, text_c, value 2 id 40 s8_a, s16 x3, s8_b, (s8 dropped), s32_a, flag_a, flag_b,
 * s8_c, s8_d, s8_e, u8 count, count x (s8, s8) 41 s16 x3, s8_b, (s8 dropped), s32_a, s8_c,
 * s8_d, s8_e 101 text_a, then the whole of case 1, the official client falls through, and the second string
 * lands on top of the first 102 u8 rows, each a label, a u8 count and that many (label, kind)
 */
typedef struct {
    s8  variant;
    int known;
    s64 entity_id;
    char text_a[MMO_GM_TEXT_MAX + 1];
    char text_b[MMO_GM_TEXT_MAX + 1];
    char text_c[MMO_GM_TEXT_MAX + 1];
    s32 value;
    s8  s8_a;
    s16 s16_a;
    s16 s16_b;
    s16 s16_c;
    s8  s8_b;
    s32 s32_a;
    int flag_a;
    int flag_b;
    s8  s8_c;
    s8  s8_d;
    s8  s8_e;
    int pair_count;
    s8  pair_a[MMO_GM_PAIR_MAX];
    s8  pair_b[MMO_GM_PAIR_MAX];
    int row_count;
    mmo_gm_panel_row row[MMO_GM_PANEL_ROW_MAX];
} mmo_gm_panel;

typedef struct {
    s8  clear;
    char label[MMO_GM_TEXT_MAX + 1];
    char value[MMO_GM_TEXT_MAX + 1];
    s32 count;
} mmo_gm_panel_entry;

int mmo_game_read_gm_lookup(const u8 *body, size_t n, mmo_gm_lookup *out);
int mmo_game_read_gm_panel(const u8 *body, size_t n, mmo_gm_panel *out);
int mmo_game_read_gm_panel_entry(const u8 *body, size_t n,
                                 mmo_gm_panel_entry *out);

/* The two account-note actions c2s 0xA2 can carry. The action byte is the
 * same enum s2c 0xA2 discriminates on, and every other value writes an
 * empty tail. */
#define MMO_GM_NOTE_ADD    1
#define MMO_GM_NOTE_DELETE 2

/* c2s 0xA2: the action byte, then the tail its kind takes. */
void mmo_game_write_admin_note_add(mmo_wbuf *w, s64 target, const char *text);
void mmo_game_write_admin_note_delete(mmo_wbuf *w, s64 target, s64 note_id);

/* c2s 0xA9: one id. */
void mmo_game_write_moderation_confirm(mmo_wbuf *w, s64 target);

#endif /* MMO_GAME_H */
