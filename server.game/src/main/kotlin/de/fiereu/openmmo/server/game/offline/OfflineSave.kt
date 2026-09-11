package de.fiereu.openmmo.server.game.offline

import de.fiereu.openmmo.common.ContestConditions
import de.fiereu.openmmo.common.enums.PokemonContainer
import de.fiereu.openmmo.common.enums.PokemonStat

/** One move slot as the save offers it: the move, the pp left in it, and its pp Ups. */
data class OfflineMove(val moveId: Int, val pp: Int, val ppUps: Int)

/** One monster as an offline save offers it, before anything has been checked. */
data class OfflineMonster(
    /** The personality value. It is the record's seed, and the nature and ability come off it. */
    val pid: Int,
    val dexId: Int,
    val form: Int,
    val level: Int,
    val xp: Int,
    val ivs: Map<PokemonStat, Int>,
    val evs: Map<PokemonStat, Int>,
    val moves: List<OfflineMove>,
    val nickname: String,
    val otName: String,
    /** The trainer id pair, public id in the low half and secret id in the high. */
    val otId: Int,
    /** The ability id the file carries. Gen 4 stores it rather than deriving it on read. */
    val abilityId: Int,
    val hasHiddenAbility: Boolean,
    /** What the file's nature byte said. The record derives its own from [pid]. */
    val natureByte: Int,
    val isShiny: Boolean,
    val heldItemId: Int,
    val friendship: Int,
    val isEgg: Boolean,
    /** For an egg, how many cycles are left before it hatches. */
    val eggCyclesLeft: Int,
    val container: PokemonContainer,
    val containerSlot: Int,
    /** What Poffins raised, one per contest type. */
    val conditions: ContestConditions = ContestConditions.NONE,
    /** How saturated with Poffins it is; nothing can raise a condition past what this leaves. */
    val sheen: Int = 0,
    /** Super Contest ribbons, in the mask layout `superContestRibbonBit` describes. */
    val superContestRibbons: Long = 0L,
    /** Where the file says it was caught, as the engine's own location label. */
    val metLocationLabel: Int = 0,
    /** The ball it is in, as a wire item id. Carried; the record has no home for it yet. */
    val ballItemId: Int = 0,
    /** The Pokerus byte, strain and days left as the file packs them. Carried, not read. */
    val pokerus: Int = 0,
    /** The six box markings, a bit each. Carried, not read. */
    val markings: Int = 0,
    /**
     * What the file left it suffering from: the client engine's own condition word, 0 for a healthy
     * one, in the layout [de.fiereu.openmmo.common.Pokemon.status] describes.
     */
    val status: Int = 0,
)

/** One line of the bag. */
data class OfflineItem(val itemId: Int, val quantity: Int)

/** Where the save left the player standing. */
data class OfflinePosition(
    val regionId: Int,
    val bankId: Int,
    val mapId: Int,
    val x: Int,
    val y: Int,
)

/** A whole character as an offline save offers it. */
data class OfflineSave(
    /** The save's own trainer id pair. */
    val trainerId: Int,
    val monsters: List<OfflineMonster>,
    val money: Int,
    val bag: List<OfflineItem>,
    /**
     * The engine's own badge bitfield, one bit a badge in `BADGE_ID_*` order. Not a count: the
     * import writes one story flag per badge and a count cannot say which eight it means.
     */
    val badges: Int,
    val dexSeen: Set<Int>,
    val dexCaught: Set<Int>,
    val position: OfflinePosition,
    /**
     * Where a white out would have put the player, as the engine's own black-out warp id: a 1-based
     * row of the cartridge's spawn table, 0 for a save that has never been in a Pokemon Center.
     */
    val blackOutWarpId: Int = 0,
    val playTimeSeconds: Int,
    /**
     * The script VM's own flags, by the key the story store gives them. They cross as they stand:
     * nothing the server gates on is read from one.
     */
    val storyFlags: Set<String> = emptySet(),
    /** The script VM's own variables, same store, same treatment. */
    val storyVars: Map<String, Int> = emptyMap(),
    /** The engine save blocks the file carries, by the id the wire gives them. */
    val blocks: Map<Int, ByteArray> = emptyMap(),
)

/**
 * What the server already knows about the character the save is landing on, which two rows of the
 * table cannot be decided without.
 */
data class ImportContext(
    /**
     * The personality values that left with this character's last export. A monster the cartridge
     * cannot produce is still this server's own if it went out through that door, so it comes home
     * rather than being dropped.
     */
    val exportedPids: Set<Int> = emptySet(),
    /**
     * Personality values living on some other character here. The save was written before the trade
     * that moved them, so its copy is the stale one.
     */
    val pidsElsewhere: Set<Int> = emptySet(),
    /**
     * True when the character has no record of its own yet, so the save's trainer id becomes it.
     */
    val firstImport: Boolean = true,
    /** The trainer id pair the server already holds, read on any import that is not the first. */
    val serverTrainerId: Int = 0,
    /** Personality values boarding at this server's day care. */
    val daycarePids: Set<Int> = emptySet(),
    /**
     * Where the character was last healed, which is where a position the server cannot draw is
     * brought back to. It is the same field a blackout sends a player to.
     */
    val lastHealLocation: OfflinePosition,
)
