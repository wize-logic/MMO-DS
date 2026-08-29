package de.fiereu.openmmo.server.game.storage

import de.fiereu.openmmo.common.CharacterInfo
import de.fiereu.openmmo.common.DynamicWarp
import de.fiereu.openmmo.common.HealLocation
import de.fiereu.openmmo.common.MAX_PARTY_SIZE
import de.fiereu.openmmo.common.Pokemon
import de.fiereu.openmmo.common.Skin
import de.fiereu.openmmo.common.enums.CharacterGender
import de.fiereu.openmmo.common.enums.Direction
import de.fiereu.openmmo.common.enums.PokemonContainer
import de.fiereu.openmmo.common.enums.Region
import de.fiereu.openmmo.common.enums.SkinSlot
import io.github.oshai.kotlinlogging.KotlinLogging
import java.time.LocalDateTime
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.CopyOnWriteArrayList
import javax.inject.Inject
import javax.inject.Singleton
import kotlin.time.Duration.Companion.seconds
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.joinAll
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock

private val log = KotlinLogging.logger {}

private val FLUSH_TICK = 5.seconds
private val FLUSH_DEBOUNCE = 10.seconds

/**
 * The most cash a character can hold, which is the engine's own `MONEY_MAX`. The wallet is a
 * six-digit field on both screens that draw it, so a balance past this has nowhere to be shown.
 */
const val MONEY_MAX = 999_999

/**
 * How many monsters the PC holds: the engine's eighteen boxes of thirty, which is what our
 * client can actually draw and address. The boxes are a view over one flat list here rather
 * than containers of their own, so a slot is `box * 30 + position`.
 */
const val PC_STORAGE_SIZE = 18 * 30

data class StoredCharacter(
    val info: CharacterInfo,
    val pokemon: MutableList<Pokemon>,
    val pcStorage: MutableList<Pokemon>,
    val items: MutableMap<Int, Int>,
    // Story progression. Flags are set/unset booleans, vars are named integers that default to 0.
    // Keys are opaque strings supplied by the content layer, so the store stays game agnostic.
    val storyFlags: MutableSet<String> = mutableSetOf(),
    val storyVars: MutableMap<String, Int> = mutableMapOf(),
    val skins: Map<SkinSlot, Skin> = emptyMap(),
)

/**
 * Refusal of a name some other character already answers to. It is thrown rather than returned
 * because every caller wants the character it asked for, and a name already spoken for is the
 * one ordinary way it does not get one.
 */
class CharacterNameTakenException(val name: String) :
    IllegalStateException("character name '$name' is already taken")

/**
 * Write-through cache over [CharacterRepository]. Memory is the live version and the database
 * mirrors it.
 */
@Singleton
class CharacterStore
@Inject
constructor(
    private val repository: CharacterRepository,
    private val entityIds: EntityIdService,
    scope: CoroutineScope,
) {
  private val flushJob = SupervisorJob()
  private val flushScope = CoroutineScope(scope.coroutineContext + flushJob)
  private var periodicJob: Job? = null

  private val characters = ConcurrentHashMap<Long, StoredCharacter>()
  private val charactersByUser = ConcurrentHashMap<Int, CopyOnWriteArrayList<Long>>()
  // The last aggregate a write succeeded for. A flush sends the difference to it.
  private val persisted = ConcurrentHashMap<Long, StoredCharacter>()
  private val dirtySince = ConcurrentHashMap<Long, Long>()
  private val pendingUnload = ConcurrentHashMap.newKeySet<Long>()
  private val flushLocks = ConcurrentHashMap<Long, Mutex>()
  // Held across the whole of a creation: without it two accounts submitting the same name
  // at once both read it free and both insert. The database's unique index is the backstop
  // for a second server process, but only one of these ever runs against a game database.
  private val nameLock = Mutex()

  /**
   * Create a character at its region's new-game start. Party, PC and bag stay empty. Throws
   * [CharacterNameTakenException] when [name] is already in use, whoever owns it.
   */
  suspend fun createCharacter(
      userId: Int,
      name: String,
      gender: CharacterGender,
      startingRegion: Region,
      skins: Map<SkinSlot, Skin> = emptyMap(),
      skinRegionSelectionIndex: Int = 0,
  ): StoredCharacter =
      nameLock.withLock {
        if (isNameTaken(name)) throw CharacterNameTakenException(name)
        insertNewCharacter(name, gender, startingRegion, userId, skins, skinRegionSelectionIndex)
      }

  /**
   * Whether [name] belongs to a character already, ignoring case. Cached characters are only the
   * connected ones, so the database answers for everybody else.
   */
  suspend fun isNameTaken(name: String): Boolean =
      findCachedByName(name) != null || repository.nameTaken(name)

  private suspend fun insertNewCharacter(
      name: String,
      gender: CharacterGender,
      startingRegion: Region,
      userId: Int,
      skins: Map<SkinSlot, Skin>,
      skinRegionSelectionIndex: Int,
  ): StoredCharacter {
    val female = gender == CharacterGender.FEMALE
    val start = NewGameStarts.forRegion(startingRegion, female)
    val id = entityIds.newCharacterId()
    val now = LocalDateTime.now()
    val info =
        CharacterInfo(
            id = id,
            name = name,
            namePrefix = "",
            userId = userId,
            // This historical field stores the player's gender.
            rivalSex = gender.wireValue,
            skinRegionSelectionIndex = skinRegionSelectionIndex,
            lastLogin = now,
            createdAt = now,
            money = start.money,
            permissions = start.permissions,
            remainingSafariSteps = 0,
            remainingSafariBalls = 0,
            pcExtraSlots = 0,
            battleBoxExtraSlots = 0,
            templateAmount = 0,
            positionRegionId = startingRegion.wireValue,
            positionBankId = start.bankId,
            positionMapId = start.mapId,
            positionX = start.x,
            positionY = start.y,
            repelLeft = 0,
            repelItemId = 0,
            lureLeft = 0,
            lureItemId = 0,
            dynamicWarp = start.dynamicWarp,
            lastHealLocation = start.healLocation,
        )
    val stored =
        StoredCharacter(
            info,
            mutableListOf(),
            mutableListOf(),
            mutableMapOf(),
            storyFlags = start.storyFlags.toMutableSet(),
            storyVars = start.storyVars.toMutableMap(),
            skins = skins.toMap(),
        )
    repository.insertAggregate(stored)
    characters[id] = stored
    persisted[id] = stored
    charactersByUser.computeIfAbsent(userId) { CopyOnWriteArrayList() }.add(id)
    return stored
  }

  fun getCharacter(id: Long): StoredCharacter? = characters[id]

  fun findCachedByName(name: String): StoredCharacter? =
      characters.values.firstOrNull { it.info.name.equals(name, ignoreCase = true) }

  /**
   * The id and stored spelling of whoever answers to [name], ignoring case. The cache holds
   * only the connected characters, so the database answers for everybody else, which is how
   * mail reaches a player who is not online.
   */
  suspend fun findIdByName(name: String): Pair<Long, String>? =
      findCachedByName(name)?.let { it.info.id to it.info.name } ?: repository.findIdByName(name)

  /** Like [getCharacter] but falls back to the database when the cache has no entry. */
  suspend fun getOrLoadCharacter(id: Long): StoredCharacter? {
    pendingUnload.remove(id)
    characters[id]?.let {
      return it
    }
    val loaded = repository.loadById(id) ?: return null
    return cache(loaded)
  }

  suspend fun getCharactersByUser(userId: Int): List<StoredCharacter> {
    val cachedIds: List<Long>? = charactersByUser[userId]
    if (cachedIds != null) {
      cachedIds.forEach { pendingUnload.remove(it) }
      return cachedIds.mapNotNull { characters[it] }.sortedBy { it.info.id }
    }
    val loaded = repository.loadByUser(userId).map { cache(it) }.sortedBy { it.info.id }
    loaded.forEach { pendingUnload.remove(it.info.id) }
    charactersByUser.putIfAbsent(userId, CopyOnWriteArrayList(loaded.map { it.info.id }))
    return loaded
  }

  /** Permanently delete an owned character and evict every cached reference to it. */
  suspend fun deleteCharacter(userId: Int, characterId: Long): Boolean {
    if (!repository.deleteById(userId, characterId)) return false
    characters.remove(characterId)
    persisted.remove(characterId)
    charactersByUser[userId]?.remove(characterId)
    dirtySince.remove(characterId)
    pendingUnload.remove(characterId)
    return true
  }

  /**
   * Applies [change] to the cached character under the map's own lock. Scripts run on their
   * own coroutine while packets are answered on the mailbox coroutine, so a plain read, copy
   * and write would let one thread drop the other's field.
   */
  private fun mutate(characterId: Long, change: (StoredCharacter) -> StoredCharacter?): Boolean {
    var applied = false
    val present =
        characters.computeIfPresent(characterId) { _, stored ->
          val updated = change(stored)
          if (updated == null) {
            stored
          } else {
            applied = true
            updated
          }
        }
    if (present == null) {
      // A disconnect evicts the character, so a script finishing its last statements reaches this.
      log.warn { "Dropped a write for character $characterId, it is no longer cached" }
      return false
    }
    if (applied) markDirty(characterId)
    return applied
  }

  fun updateCharacter(info: CharacterInfo) {
    mutate(info.id) { it.copy(info = info) }
  }

  /**
   * The Y-registered key item, 0 for none. Position-weight state, not currency: the client's own
   * bag screen already did the registering, so this records it and the periodic flush persists it.
   */
  fun setRegisteredItem(characterId: Long, itemId: Short) {
    mutate(characterId) { stored ->
      if (stored.info.registeredItem == itemId) null
      else stored.copy(info = stored.info.copy(registeredItem = itemId))
    }
  }

  fun updatePosition(
      characterId: Long,
      x: Short,
      y: Short,
      bankId: Byte? = null,
      mapId: Byte? = null,
      facing: Direction? = null,
  ) {
    mutate(characterId) { stored ->
      stored.copy(
          info =
              stored.info.copy(
                  positionX = x,
                  positionY = y,
                  positionBankId = bankId ?: stored.info.positionBankId,
                  positionMapId = mapId ?: stored.info.positionMapId,
                  positionFacing = facing ?: stored.info.positionFacing,
              ),
      )
    }
  }

  /**
   * Seats one monster in the container [pokemon] names, and answers with it as it was seated.
   */
  suspend fun addPokemon(
      characterId: Long,
      pokemon: Pokemon,
      preferredSlot: Int? = null,
  ): Pokemon? {
    var seated: Pokemon? = null
    val owned = pokemon.copy(ownerId = characterId)
    val written =
        mutateDurably(
            characterId,
            apply = { stored ->
              val monster = seat(stored, owned, preferredSlot) ?: return@mutateDurably null
              seated = monster
              // Copy instead of mutating in place, so flusher snapshots never see a half-updated
              // list.
              if (monster.container == PokemonContainer.PC)
                  stored.copy(pcStorage = (stored.pcStorage + monster).toMutableList())
              else stored.copy(pokemon = (stored.pokemon + monster).toMutableList())
            },
            rollback = { stored ->
              stored.copy(
                  pokemon = stored.pokemon.filter { m -> m.id != pokemon.id }.toMutableList(),
                  pcStorage = stored.pcStorage.filter { m -> m.id != pokemon.id }.toMutableList(),
              )
            },
        )
    return if (written) seated else null
  }

  /** The slot [pokemon] goes in, or null when its container is full. */
  private fun seat(stored: StoredCharacter, pokemon: Pokemon, preferredSlot: Int?): Pokemon? {
    if (pokemon.container == PokemonContainer.PC) {
      val taken: Set<Int> = stored.pcStorage.mapTo(mutableSetOf()) { it.containerSlot.toInt() }
      val slot =
          preferredSlot?.takeIf { it in 0 until PC_STORAGE_SIZE && it !in taken }
              ?: (0 until PC_STORAGE_SIZE).firstOrNull { it !in taken }
              ?: return null
      return pokemon.copy(container = PokemonContainer.PC, containerSlot = slot.toShort())
    }
    if (stored.pokemon.size >= MAX_PARTY_SIZE) return null
    return pokemon.copy(
        container = PokemonContainer.PARTY, containerSlot = stored.pokemon.size.toShort())
  }

  /**
   * Remove one monster the box screen released, from whichever container holds it. Refused when it
   * would empty the party, a character with nothing alive to send out is one no battle can start.
   */
  suspend fun releasePokemon(characterId: Long, monsterId: Long): Boolean {
    var removed: Pokemon? = null
    return mutateDurably(
        characterId,
        apply = { stored ->
          val inParty = stored.pokemon.firstOrNull { it.id == monsterId }
          val inPc = stored.pcStorage.firstOrNull { it.id == monsterId }
          when {
            inParty != null && stored.pokemon.size > 1 -> {
              removed = inParty
              stored.copy(
                  pokemon =
                      stored.pokemon
                          .filter { it.id != monsterId }
                          .mapIndexed { i, m -> m.copy(containerSlot = i.toShort()) }
                          .toMutableList())
            }
            inPc != null -> {
              removed = inPc
              stored.copy(
                  pcStorage = stored.pcStorage.filter { it.id != monsterId }.toMutableList())
            }
            else -> null
          }
        },
        rollback = { stored ->
          val back = removed
          when {
            back == null -> stored
            back.container == PokemonContainer.PC ->
                stored.copy(pcStorage = (stored.pcStorage + back).toMutableList())
            else -> stored.copy(pokemon = (stored.pokemon + back).toMutableList())
          }
        },
    )
  }

  /**
   * One half of a trade: the party monster [outgoingId] leaves and [incoming] takes its exact
   * slot, in a single durable write.
   */
  suspend fun swapPartyMonster(characterId: Long, outgoingId: Long, incoming: Pokemon): Pokemon? {
    var removed: Pokemon? = null
    val written =
        mutateDurably(
            characterId,
            apply = { stored ->
              val out = stored.pokemon.firstOrNull { it.id == outgoingId }
              if (out == null || stored.pokemon.any { it.id == incoming.id }) {
                return@mutateDurably null
              }
              removed = out
              val seated =
                  incoming.copy(
                      ownerId = characterId,
                      container = PokemonContainer.PARTY,
                      containerSlot = out.containerSlot,
                  )
              stored.copy(
                  pokemon =
                      stored.pokemon
                          .map { if (it.id == outgoingId) seated else it }
                          .toMutableList())
            },
            rollback = { stored ->
              val back = removed
              if (back == null) stored
              else
                  stored.copy(
                      pokemon =
                          stored.pokemon
                              .map { if (it.id == incoming.id) back else it }
                              .toMutableList())
            },
        )
    return if (written) removed else null
  }

  /** Replace one party monster by id, for example after a battle changed hp, xp, or level. */
  fun updatePokemon(characterId: Long, updated: Pokemon) {
    mutate(characterId) { stored ->
      stored.copy(
          pokemon = stored.pokemon.map { if (it.id == updated.id) updated else it }.toMutableList())
    }
  }

  /** Rearrange a character's monsters between the party and the PC. */
  fun rearrangeMonsters(
      characterId: Long,
      rearrange: (party: List<Pokemon>, pc: List<Pokemon>) -> Pair<List<Pokemon>, List<Pokemon>>?,
  ): Boolean =
      mutate(characterId) { stored ->
        val (party, pc) = rearrange(stored.pokemon, stored.pcStorage) ?: return@mutate null
        stored.copy(pokemon = party.toMutableList(), pcStorage = pc.toMutableList())
      }

  /** Applies a signed cash delta and answers whether the balance now holds it. */
  suspend fun addMoney(characterId: Long, amount: Int): Boolean {
    var applied = 0
    return mutateDurably(
        characterId,
        apply = { stored ->
          val before = stored.info.money
          val target = before.toLong() + amount
          if (target < 0) return@mutateDurably null
          val after = target.coerceAtMost(MONEY_MAX.toLong()).toInt()
          applied = after - before
          if (after == before) stored else stored.copy(info = stored.info.copy(money = after))
        },
        // Undo what was applied rather than restoring a snapshot, so a concurrent edit to another
        // field of the same character survives, and so a clamped earning gives back what it took.
        rollback = { it.copy(info = it.info.copy(money = it.info.money - applied)) },
    )
  }

  /** Add (or remove with a negative amount) one persisted bag stack. */
  /** False when the bag would go negative, or when the change could not be written. */
  suspend fun addItem(characterId: Long, itemId: Int, amount: Int): Boolean =
      mutateDurably(
          characterId,
          apply = { stored ->
            val newQuantity = (stored.items[itemId] ?: 0) + amount
            if (newQuantity < 0) return@mutateDurably null
            val items = stored.items.toMutableMap()
            if (newQuantity == 0) items.remove(itemId) else items[itemId] = newQuantity
            stored.copy(items = items)
          },
          rollback = { stored ->
            val reverted = (stored.items[itemId] ?: 0) - amount
            val items = stored.items.toMutableMap()
            if (reverted <= 0) items.remove(itemId) else items[itemId] = reverted
            stored.copy(items = items)
          },
      )

  /** Set (or clear with null) the runtime destination for MAP_DYNAMIC warps (setdynamicwarp). */
  fun setDynamicWarp(characterId: Long, warp: DynamicWarp?) {
    mutate(characterId) { it.copy(info = it.info.copy(dynamicWarp = warp)) }
  }

  /** Move where a white out sends this character (the decomp setrespawn). */
  fun setHealLocation(characterId: Long, location: HealLocation) {
    mutate(characterId) { stored ->
      if (stored.info.lastHealLocation == location) null
      else stored.copy(info = stored.info.copy(lastHealLocation = location))
    }
  }

  /** Set a story flag. Copies the set so flusher snapshots never see a half-updated collection. */
  fun setStoryFlag(characterId: Long, flag: String) {
    mutate(characterId) { stored ->
      if (flag in stored.storyFlags) null
      else stored.copy(storyFlags = (stored.storyFlags + flag).toMutableSet())
    }
  }

  fun clearStoryFlag(characterId: Long, flag: String) {
    mutate(characterId) { stored ->
      if (flag !in stored.storyFlags) null
      else stored.copy(storyFlags = (stored.storyFlags - flag).toMutableSet())
    }
  }

  /** Set a story var. A value of 0 is the default, so it drops the row instead of storing it. */
  fun setStoryVar(characterId: Long, key: String, value: Int) {
    mutate(characterId) { stored ->
      val newVars = stored.storyVars.toMutableMap()
      if (value == 0) newVars.remove(key) else newVars[key] = value
      if (newVars == stored.storyVars) null else stored.copy(storyVars = newVars)
    }
  }

  /** Replaces every monster, the party and the pc alike, along with the bag and story state. */
  fun replaceProgress(
      characterId: Long,
      party: List<Pokemon>,
      items: Map<Int, Int>,
      storyFlags: Set<String>,
      storyVars: Map<String, Int>,
  ) {
    mutate(characterId) { stored ->
      stored.copy(
          pokemon = party.toMutableList(),
          pcStorage = mutableListOf(),
          items = items.toMutableMap(),
          storyFlags = storyFlags.toMutableSet(),
          // A var of 0 is the default, so it is stored as absent everywhere else too.
          storyVars = storyVars.filterValues { it != 0 }.toMutableMap(),
      )
    }
  }

  /** Undoes what an unfinished script wrote, from a [snapshot] taken before it started. */
  suspend fun rollBackTo(characterId: Long, snapshot: StoredCharacter) {
    lockFor(characterId).withLock {
      mutate(characterId) { stored ->
        stored.copy(
            info =
                snapshot.info.copy(
                    positionRegionId = stored.info.positionRegionId,
                    positionBankId = stored.info.positionBankId,
                    positionMapId = stored.info.positionMapId,
                    positionX = stored.info.positionX,
                    positionY = stored.info.positionY,
                    positionFacing = stored.info.positionFacing,
                    lastHealLocation = stored.info.lastHealLocation,
                    dynamicWarp = stored.info.dynamicWarp,
                    lastLogin = stored.info.lastLogin,
                ),
            pokemon = snapshot.pokemon.toMutableList(),
            pcStorage = snapshot.pcStorage.toMutableList(),
            items = snapshot.items.toMutableMap(),
            storyFlags = snapshot.storyFlags.toMutableSet(),
            storyVars = snapshot.storyVars.toMutableMap(),
        )
      }
    }
  }

  fun startPeriodicFlush() {
    periodicJob =
        flushScope.launch {
          while (isActive) {
            delay(FLUSH_TICK)
            flushOlderThan(FLUSH_DEBOUNCE.inWholeMilliseconds)
          }
        }
  }

  /** Flush one character soon, skipping the debounce. Safe to call from Netty threads. */
  fun flushCharacterAsync(characterId: Long) {
    flushScope.launch { flush(characterId) }
  }

  /**
   * Marks the character dirty and writes it before returning. Anything a player can trade or
   * spend goes through here, so a crash cannot lose an item that the client was already told
   * it has.
   */
  /**
   * Applies a change to something a player can trade or spend and writes it before returning.
   * A failed write is undone by [rollback] and reported, so a caller never tells a player
   * about an item, a coin or a monster the database did not accept.
   */
  private suspend fun mutateDurably(
      characterId: Long,
      apply: (StoredCharacter) -> StoredCharacter?,
      rollback: (StoredCharacter) -> StoredCharacter,
  ): Boolean =
      lockFor(characterId).withLock {
        // Through [mutate] rather than a read and a write, so a script writing another field
        // between the two is not clobbered by the one this puts back.
        if (!mutate(characterId, apply)) return@withLock false
        if (flushLocked(characterId, allowEvict = false)) return@withLock true
        mutate(characterId, rollback)
        // Left dirty on purpose. The rolled back state is still the one to persist, and anything
        // else the character had waiting, a position, a story flag, was dirty before this call
        // and clearing the marker here would drop it from the queue with nothing to bring it back.
        false
      }

  /**
   * Persist the character and drop it from the cache once the write succeeded. While the save
   * keeps failing the character stays cached and dirty, and the periodic flusher finishes the
   * eviction on its next successful write.
   */
  fun unloadCharacterAsync(characterId: Long, unfinishedScript: StoredCharacter? = null) {
    pendingUnload.add(characterId)
    flushScope.launch {
      if (unfinishedScript != null) {
        // Said out loud because this undoes party, box, bag, cash and story state at once: a
        // session that ends mid-script and loses a gesture to this rollback otherwise looks
        // exactly like a packet that never arrived.
        log.info { "Character $characterId disconnected mid-script; rolling back to its snapshot" }
        rollBackTo(characterId, unfinishedScript)
      }
      flush(characterId)
    }
  }

  suspend fun flushAll() {
    for (id in dirtySince.keys) flush(id)
  }

  /** Stop the periodic loop, wait for in-flight flushes, then persist whatever is still dirty. */
  suspend fun shutdown() {
    periodicJob?.cancel()
    flushJob.children.toList().joinAll()
    flushAll()
  }

  private fun cache(stored: StoredCharacter): StoredCharacter {
    val existing = characters.putIfAbsent(stored.info.id, stored)
    if (existing == null) persisted[stored.info.id] = stored
    return existing ?: stored
  }

  private fun markDirty(id: Long) {
    dirtySince.putIfAbsent(id, System.currentTimeMillis())
  }

  private suspend fun flushOlderThan(ageMs: Long) {
    val now = System.currentTimeMillis()
    for ((id, since) in dirtySince) {
      if (now - since >= ageMs) flush(id)
    }
  }

  // Serialised per character. Without this a second flush takes the dirty marker, skips its own
  // write and returns while the first is still inside saveChanges, so persistNow would promise a
  // write it did not make.
  private suspend fun flush(id: Long) {
    lockFor(id).withLock { flushLocked(id, allowEvict = true) }
  }

  private fun lockFor(id: Long): Mutex = flushLocks.computeIfAbsent(id) { Mutex() }

  /** True when the character is in the database, either because it was written or was not dirty. */
  private suspend fun flushLocked(id: Long, allowEvict: Boolean): Boolean {
    val since = dirtySince.remove(id)
    val stored = characters[id]
    if (since != null && stored != null) {
      try {
        repository.saveChanges(persisted[id], stored)
        // The written instance, not the current one, so a racing mutation stays dirty.
        persisted[id] = stored
      } catch (e: CancellationException) {
        // A disconnect cancelling the caller must not read as a failed write.
        dirtySince.putIfAbsent(id, since)
        throw e
      } catch (e: Exception) {
        log.warn(e) { "Failed to persist character $id, will retry" }
        dirtySince.putIfAbsent(id, since)
        return false
      }
    }
    // A durable mutation must not evict the character its own caller is still working with.
    if (allowEvict) maybeEvict(id)
    return true
  }

  private fun maybeEvict(id: Long) {
    if (!pendingUnload.remove(id)) return
    if (dirtySince.containsKey(id)) {
      pendingUnload.add(id)
      return
    }
    val stored = characters.remove(id) ?: return
    persisted.remove(id)
    flushLocks.remove(id)
    charactersByUser.remove(stored.info.userId)
  }
}
