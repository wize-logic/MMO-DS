package de.fiereu.openmmo.server.game.session

import de.fiereu.openmmo.common.auth.AccountRoles
import de.fiereu.openmmo.common.enums.Direction
import de.fiereu.openmmo.maps.NpcDef
import de.fiereu.openmmo.server.game.world.UndergroundExit
import java.util.concurrent.ConcurrentHashMap

/**
 * Per-session player state. Scripts run on their own coroutine while packets are answered on the
 * mailbox coroutine, and both threads touch every field here, so all of them are volatile.
 */
data class PlayerState(
    val userId: Int,
    /**
     * What this account may do, as its join ticket stated it. Roles belong to the account, and this
     * server has no user table of its own to ask.
     */
    val roles: AccountRoles = AccountRoles.NONE,
    @field:Volatile var characterId: Long? = null,
    @field:Volatile var justWarped: Boolean = false,
    @field:Volatile var facingDirection: Direction = Direction.DOWN,
    @field:Volatile var inDialog: Boolean = false,
    @field:Volatile var dialogNpcEntityId: Long = 0,
    @field:Volatile var dialogSeqId: Int = 0,
    @field:Volatile var regionId: Int = 1,
    @field:Volatile var bankId: Int = 51,
    @field:Volatile var mapId: Int = 3,
    @field:Volatile var x: Short = 4,
    @field:Volatile var y: Short = 2,
    @field:Volatile var elevation: Int = 0,
    /**
     * What the avatar is riding, the byte `LoadEntity` and `EntityTransportation` (0x28) both
     * carry.
     */
    @field:Volatile var transportation: Byte = 0,
    /**
     * Whether Strength is on for this visit. The decomp's `FLAG_STRENGTH_ACTIVE`: set when the
     * player uses the move, cleared on a map change, never stored.
     */
    @field:Volatile var strengthActive: Boolean = false,
    /**
     * Where this visit's Strength boulders sit, keyed by the map's local object id. Empty means
     * every boulder is where the map put it.
     */
    val boulderTiles: MutableMap<Int, Pair<Int, Int>> = ConcurrentHashMap(),
    /** Trusts one source tile after scripted movement. */
    @field:Volatile var acceptNextMoveSource: Boolean = false,
    /**
     * Whether this session is sitting at a trade table. While it is,
     * [de.fiereu.openmmo.server.game.services.PokemonStorageService] refuses to move a monster
     * between containers, because a move landing between the two halves of a settlement takes back
     * the monster the first half just handed over.
     */
    @field:Volatile var atTradeTable: Boolean = false,
    /**
     * A script is warping and will run the destination's entry scripts itself. The arrival must not
     * start a second copy, which [inDialog] alone cannot prevent because a stray dialog packet
     * clears it while the script is parked on the map load.
     */
    @field:Volatile var scriptOwnsMapEntry: Boolean = false,
    /** Maps the client already holds. A warp sends deleteCache, which empties this. */
    val loadedMaps: MutableSet<Int> = ConcurrentHashMap.newKeySet(),
    /**
     * The npc walking behind this player, or null. Session-only: the decomp's `FLAG_HAS_PARTNER` is
     * a save flag, but a disconnect mid-walk leaves the destination map's own object to finish the
     * scene.
     */
    @field:Volatile var partner: PartnerFollow? = null,
    /**
     * The surface tile this session descended into the Underground from, or null when it is not
     * down there.
     */
    @field:Volatile var undergroundExit: UndergroundExit? = null,
)

/** The npc setHasPartner named, kept so a map change can spawn them again. */
data class PartnerFollow(val entityId: Long, val npc: NpcDef)

/** Packs a map address into one key for [PlayerState.loadedMaps]. */
fun mapCacheKey(regionId: Int, bankId: Int, mapId: Int): Int =
    (regionId shl 16) or (bankId shl 8) or mapId
