package de.fiereu.openmmo.server.game.script

import de.fiereu.network.SessionContext
import de.fiereu.openmmo.common.DynamicWarp
import de.fiereu.openmmo.common.dialog.DialogLine
import de.fiereu.openmmo.common.enums.Direction
import de.fiereu.openmmo.common.enums.Region
import de.fiereu.openmmo.common.enums.TimeOfDay
import de.fiereu.openmmo.items.ItemDef
import de.fiereu.openmmo.maps.MapManager
import de.fiereu.openmmo.net.game.packets.dialog.DialogMessageArg
import de.fiereu.openmmo.net.game.packets.dialog.TextPokemonSpeciesArg
import de.fiereu.openmmo.net.game.packets.dialog.TextStringArg
import de.fiereu.openmmo.server.game.battle.BattleResult
import de.fiereu.openmmo.server.game.services.BattleService
import de.fiereu.openmmo.server.game.services.DialogPresentation
import de.fiereu.openmmo.server.game.services.DialogService
import de.fiereu.openmmo.server.game.services.FieldMoveService
import de.fiereu.openmmo.server.game.services.MapEntryScripts
import de.fiereu.openmmo.server.game.services.ScriptMovementService
import de.fiereu.openmmo.server.game.services.ScriptWarpService
import de.fiereu.openmmo.server.game.services.ShopService
import de.fiereu.openmmo.server.game.services.StoryClientState
import de.fiereu.openmmo.server.game.services.StoryPlayerService
import de.fiereu.openmmo.server.game.services.StoryService
import de.fiereu.openmmo.server.game.services.isSurfing
import de.fiereu.openmmo.server.game.session.PlayerState
import de.fiereu.openmmo.server.game.storage.CharacterStore
import java.time.Clock
import java.time.LocalDate
import java.time.LocalTime

/** What a [Script] uses to talk to the player it interacted with and read or write story state. */
class ScriptContext
internal constructor(
    internal val session: SessionContext,
    internal val state: PlayerState,
    /** The npc the player talked to, or -1 for a sign. */
    val entityId: Long,
    internal val dialog: DialogService,
    internal val story: StoryService,
    private val movement: ScriptMovementService,
    private val warp: ScriptWarpService? = null,
    internal val player: StoryPlayerService? = null,
    private val battles: BattleService? = null,
    internal val characters: CharacterStore? = null,
    private val maps: MapManager? = null,
    private val entryScripts: MapEntryScripts? = null,
    private val shops: ShopService? = null,
    private val fieldMoves: FieldMoveService? = null,
    private val clock: Clock = Clock.systemDefaultZone(),
) {
  private val characterId: Long?
    get() = state.characterId

  val facingDirection: Direction
    get() = state.facingDirection

  /**
   * Where the player is standing, the decomp's `GetPlayerMapPos`. These are matrix coordinates, the
   * same ones the map's own triggers and objects carry, which is what lets a scene that spans a
   * wide coordinate trigger pick the path it walks people along.
   */
  val playerX: Int
    get() = state.x.toInt()

  val playerY: Int
    get() = state.y.toInt()

  val isFemale: Boolean
    get() = movement.playerGender(state) == FEMALE

  internal val playerEntityId: Long
    get() = checkNotNull(state.characterId) { "Scene has no selected character" }

  internal val playerName: String
    get() = state.characterId?.let { characters?.getCharacter(it)?.info?.name }.orEmpty()

  internal fun send(packet: Any) = session.send(packet)

  /** String variables the lines of this scene render, by slot. */
  private val stringVars = sortedMapOf<Int, String>()

  private fun buffered(vararg extra: DialogMessageArg): DialogPresentation =
      DialogPresentation(
          messageArgs =
              stringVars.map { (slot, text) -> TextStringArg(slot.toByte(), text) } + extra)

  /** The decomp's `BufferPlayerName`: this player's own name into string variable [slot]. */
  fun bufferPlayerName(slot: Int) {
    stringVars[slot] = playerName
  }

  /**
   * The decomp's `BufferRivalName`. The cartridge asks for the name in Rowan's opening and keeps it
   * on the save; nothing here does, so every rival is the region's default one.
   */
  fun bufferRivalName(slot: Int) {
    stringVars[slot] = RIVAL_NAME
  }

  /** The decomp's `BufferCounterpartName`: the protagonist this player did not pick. */
  fun bufferCounterpartName(slot: Int) {
    stringVars[slot] = if (isFemale) COUNTERPART_NAME_MALE else COUNTERPART_NAME_FEMALE
  }

  /** Show [line] as a sign and wait for the player to close it. */
  suspend fun sign(line: DialogLine) =
      dialog.showAndWait(session, state, line.textId, SIGN, -1, buffered())

  /** Show [line] from the interacted entity and wait for the player to go on. */
  suspend fun say(line: DialogLine) =
      dialog.showAndWait(session, state, line.textId, NPC, entityId, buffered())

  /** Show [line] from a cutscene npc addressed by its decomp local id. */
  suspend fun sayNpc(localId: Int, line: DialogLine) =
      dialog.showAndWait(
          session,
          state,
          line.textId,
          NPC,
          movement.npcEntityId(state, localId) ?: -1,
          buffered(),
      )

  /** Shows dialog with a species-name variable. */
  suspend fun sayNpcWithSpeciesName(localId: Int, line: DialogLine, speciesId: Int) =
      dialog.showAndWait(
          session,
          state,
          line.textId,
          NPC,
          movement.npcEntityId(state, localId) ?: -1,
          buffered(
              TextPokemonSpeciesArg(
                  partySlot = 1,
                  stringVariable = 1,
                  speciesId = speciesId.toShort(),
              )),
      )

  /** Opens the Emerald starter picker. */
  suspend fun chooseHoennStarter(): Int = dialog.chooseHoennStarter(session, state)

  /** Opens the Platinum starter picker, Rowan's briefcase on Route 201. */
  suspend fun chooseSinnohStarter(): Int = dialog.chooseSinnohStarter(session, state)

  /** Ask a ROM-backed yes/no question from the interacted entity. */
  suspend fun askYesNo(line: DialogLine): Boolean =
      dialog.askYesNo(session, state, line.textId, entityId, buffered().messageArgs)

  /** Ask a ROM-backed yes/no question from a cutscene npc. */
  suspend fun askYesNoNpc(localId: Int, line: DialogLine): Boolean =
      dialog.askYesNo(
          session,
          state,
          line.textId,
          movement.npcEntityId(state, localId) ?: -1,
          buffered().messageArgs,
      )

  /**
   * Ask a ROM-backed text list and return the 0-based index, the decomp's `InitGlobalTextListMenu`
   * / `InitGlobalTextMenu` / `InitLocalTextMenu`. Cancel is [entries].size.
   */
  suspend fun askList(prompt: DialogLine, vararg entries: DialogLine): Int =
      dialog.askList(session, state, prompt, entries.toList(), entityId, buffered().messageArgs)

  /** The player's region, as the story store spells it. */
  private val regionName: String
    get() = Region.byWireValue(state.regionId.toByte())?.name?.lowercase() ?: ""

  /** True when something in the party knows this move, the decomp's `FindPartySlotWithMove`. */
  fun partyKnowsMove(moveId: Int): Boolean =
      characterId
          ?.let { characters?.getCharacter(it) }
          ?.pokemon
          ?.any { mon -> mon.moves.any { it.id.toInt() == moveId } } ?: false

  /** True once this badge has been earned. The decomp's `GoToIfBadgeAcquired`. */
  fun hasBadge(badge: Badge): Boolean = isFlagSet(badge.keyIn(regionName))

  /** Award a badge, the decomp's `GiveBadge`. */
  fun giveBadge(badge: Badge) = setFlag(badge.keyIn(regionName))

  /**
   * How many gym badges this player has earned, the decomp's `CountBadgesAcquired`. Each badge is
   * one story flag; this is the count over [Badge], not a second store.
   */
  fun countBadgesAcquired(): Int = Badge.entries.count { hasBadge(it) }

  /**
   * True once [clearGame] has run, the decomp's `CheckGameCompleted`. The flag is
   * `region/FLAG_GAME_COMPLETED`, the same key every other story flag uses.
   */
  fun isGameCompleted(): Boolean = isFlagSet(gameCompletedKey())

  /**
   * Hall of Fame entries, the decomp's `GetLeagueVictories`. The cartridge keeps them on the HoF
   * save; here they are one story var so a script can read the same number the next visit writes.
   */
  fun leagueVictories(): Int = getVar(Pokedex.leagueVictoriesKey(regionName))

  /** The decomp's `ClearGame`, minus the cutscene, the save-and-reset, and the credits. */
  fun clearGame() {
    setFlag(gameCompletedKey())
    setFlag("$regionName/FLAG_COMMUNICATION_CLUB_ACCESSIBLE")
    setVar(Pokedex.leagueVictoriesKey(regionName), leagueVictories() + 1)
    healParty()
  }

  /** How many Sinnoh-dex species this player has seen, the decomp's `GetLocalDexSeenCount`. */
  fun localDexSeenCount(): Int =
      characterId?.let { characters?.getCharacter(it) }?.let(Pokedex::localSeenCount) ?: 0

  /**
   * True once every Sinnoh-dex species has been seen, the decomp's `CheckLocalDexCompleted`.
   * Completion is a seen count, not a caught count.
   */
  fun isLocalDexCompleted(): Boolean =
      characterId?.let { characters?.getCharacter(it) }?.let(Pokedex::isLocalCompleted) ?: false

  /** True once Oak has upgraded the dex, the decomp's `GetNationalDexEnabled`. */
  fun isNationalDexEnabled(): Boolean = isFlagSet(Pokedex.nationalDexKey(regionName))

  /** The decomp's `SetNationalDexEnabled`. */
  fun setNationalDexEnabled() = setFlag(Pokedex.nationalDexKey(regionName))

  /**
   * Mark [speciesId] as seen without catching it, the decomp's `SetSpeciesSeen`. A wild encounter
   * and a gift mark it too; this is the cutscene form.
   */
  fun setSpeciesSeen(speciesId: Int) {
    val id = characterId ?: return
    val store = characters ?: return
    Pokedex.markSeen(store, session, id, speciesId)
  }

  /**
   * The server's time of day, the decomp's `GetTimeOfDay`. A cartridge asks the console's clock;
   * here one clock answers for everyone on the map.
   */
  fun timeOfDay(): TimeOfDay = TimeOfDay.forHour(LocalTime.now(clock).hour)

  /**
   * The server's day of the week, the decomp's `GetDayOfWeek`. Sunday is 0, matching
   * `RTCDate.week`.
   */
  fun dayOfWeek(): Weekday = Weekday.entries[LocalDate.now(clock).dayOfWeek.value % 7]

  /** True once this app is on the device, the decomp's `CheckPoketchAppRegistered`. */
  fun isPoketchAppRegistered(app: PoketchApp): Boolean =
      app in PoketchApp.ROSTER || isFlagSet(app.keyIn(regionName))

  /** Put [app] on the device, the decomp's `RegisterPoketchApp`. A roster id is already there. */
  fun registerPoketchApp(app: PoketchApp) {
    if (app in PoketchApp.ROSTER) return
    setFlag(app.keyIn(regionName))
  }

  /** True if the story [flag] is set. Keys come from the content layer, for example HoennFlags. */
  fun isFlagSet(flag: String): Boolean = characterId?.let { story.isFlagSet(it, flag) } ?: false

  fun setFlag(flag: String) {
    characterId?.let {
      story.setFlag(it, flag)
      StoryClientState.flagUpdate(state.regionId.toByte(), flag, enabled = true)?.let(session::send)
    }
  }

  fun clearFlag(flag: String) {
    characterId?.let {
      story.clearFlag(it, flag)
      StoryClientState.flagUpdate(state.regionId.toByte(), flag, enabled = false)
          ?.let(session::send)
    }
  }

  /** The story var [key], or 0 if it was never set. */
  fun getVar(key: String): Int = characterId?.let { story.getVar(it, key) } ?: 0

  fun setVar(key: String, value: Int) {
    characterId?.let { story.setVar(it, key, value) }
  }

  suspend fun givePokemon(dexId: Int, level: Int, vararg moveIds: Int) =
      checkNotNull(player) { STORY_PLAYER_UNAVAILABLE }
          .givePokemon(session, state, dexId, level, moveIds.toList())

  /** Put an egg of [dexId] in the party, the decomp giveegg. */
  suspend fun giveEgg(dexId: Int) =
      checkNotNull(player) { STORY_PLAYER_UNAVAILABLE }.giveEgg(session, state, dexId)

  fun healParty() = checkNotNull(player) { STORY_PLAYER_UNAVAILABLE }.healParty(session, state)

  suspend fun giveItem(item: ItemDef, quantity: Int = 1): Boolean =
      checkNotNull(player) { STORY_PLAYER_UNAVAILABLE }.giveItem(session, state, item, quantity)

  /** Take an item back out of the bag, the decomp removeitem. False when the bag lacks it. */
  suspend fun takeItem(item: ItemDef, quantity: Int = 1): Boolean = giveItem(item, -quantity)

  /** Whether the bag holds [item], the decomp checkitem. */
  fun hasItem(item: ItemDef, quantity: Int = 1): Boolean =
      checkNotNull(player) { STORY_PLAYER_UNAVAILABLE }.hasItem(state, item, quantity)

  /** How many Pokemon are in the party, the decomp getpartycount. */
  fun partyCount(): Int = checkNotNull(player) { STORY_PLAYER_UNAVAILABLE }.partyCount(state)

  /**
   * True when at least two party members can fight, the decomp's `CheckHasTwoAliveMons`. An egg
   * does not count; neither does a fainted one.
   */
  fun hasTwoAliveMons(): Boolean =
      characterId
          ?.let { characters?.getCharacter(it) }
          ?.pokemon
          ?.count { mon -> !mon.isEgg && mon.hp > 0 } ?: 0 >= 2

  /**
   * Opens the mart window on [items], the decomp pokemart. It does not wait: the player shops while
   * the script ends, because nothing in the capture tells the server when the window closed.
   */
  fun pokemart(vararg items: ItemDef) =
      checkNotNull(shops) { "Shop service is unavailable" }.open(session, entityId, items.toList())

  /** Run a non-catchable, non-escapable story battle and wait for its result. */
  suspend fun battle(dexId: Int, level: Int, vararg moveIds: Int): BattleResult =
      checkNotNull(battles) { "Battle service is unavailable" }
          .startScriptedBattle(session, dexId, level, moveIds.toList())

  /** Fight the decomp trainer with this id, using the region the player is standing in. */
  suspend fun trainerBattle(trainerId: Int): BattleResult {
    val region =
        checkNotNull(Region.byWireValue(state.regionId.toByte())) {
          "Scene ran in unknown region ${state.regionId}"
        }
    return checkNotNull(battles) { "Battle service is unavailable" }
        .startTrainerBattle(session, region, trainerId)
  }

  /**
   * Walk the map npc with decomp local id [localId] (its entityIdx) through [steps] and wait for
   * the whole path to finish. This is applymovement plus waitmovement for an npc.
   */
  suspend fun moveNpc(localId: Int, vararg steps: MovementStep) =
      movement.moveNpc(session, state, localId, steps.toList())

  /** Starts concurrent NPC movement paths. */
  suspend fun moveNpcs(vararg paths: Pair<Int, List<MovementStep>>) =
      movement.moveNpcs(session, state, paths.toList())

  /** Starts player and NPC paths together. */
  suspend fun moveSelfAndNpcs(
      selfSteps: List<MovementStep>,
      vararg paths: Pair<Int, List<MovementStep>>,
  ) = movement.moveSelfAndNpcs(session, state, selfSteps, paths.toList())

  /** Walk the player's own avatar through [steps] and wait for it to finish. */
  suspend fun moveSelf(vararg steps: MovementStep) =
      movement.moveSelf(session, state, steps.toList())

  /** Show a normally hidden map npc (its decomp local id) to this player, the decomp addobject. */
  fun showNpc(localId: Int) = movement.showNpc(session, state, localId)

  /** Shows a hidden NPC at a new position. */
  fun showNpcAt(localId: Int, x: Int, y: Int) = movement.showNpcAt(session, state, localId, x, y)

  /** Repositions an existing NPC. */
  fun repositionNpc(localId: Int, x: Int, y: Int) =
      movement.repositionNpc(session, state, localId, x, y)

  /** Relocate the player's overworld entity as part of a cutscene. */
  fun repositionSelf(x: Int, y: Int, facing: Direction) =
      movement.repositionSelf(session, state, x, y, facing)

  /**
   * The decomp's `SetHasPartner` plus `SetMovementType FOLLOW_PLAYER`. [localId] is the map object
   * that walks behind the player until [clearHasPartner].
   */
  fun setHasPartner(localId: Int) = movement.setHasPartner(session, state, localId)

  /** The decomp's `ClearHasPartner`. The npc stays; they just stop following. */
  fun clearHasPartner() = movement.clearHasPartner(session, state)

  /** Remove a cutscene npc and its collision (`removeobject`). */
  fun removeNpc(localId: Int) = movement.removeNpc(session, state, localId)

  /**
   * Remove the npc this script was triggered by (`RemoveObject VAR_LAST_TALKED`). A shared script
   * does not know a map's local ids, so it names the thing it was talking to instead.
   */
  fun removeNpcTalkedTo() = movement.removeNpcTalkedTo(session, state, entityId)

  /** True while the avatar is on the water, the decomp's `PLAYER_AVATAR_SURFING`. */
  val isSurfing: Boolean
    get() = state.isSurfing

  /** The decomp's `UseSurf`: mount, and ride onto the water tile the player is facing. */
  suspend fun useSurf() =
      checkNotNull(fieldMoves) { FIELD_MOVES_UNAVAILABLE }.useSurf(session, state)

  /** The decomp's `UseWaterfall`: climb the falls ahead and land on the water above them. */
  suspend fun useWaterfall() =
      checkNotNull(fieldMoves) { FIELD_MOVES_UNAVAILABLE }.useWaterfall(session, state)

  /** The decomp's `UseRockClimb`: scale the wall ahead and step off past the top of it. */
  suspend fun useRockClimb() =
      checkNotNull(fieldMoves) { FIELD_MOVES_UNAVAILABLE }.useRockClimb(session, state)

  /** True while Strength is on for this visit, the decomp's `FLAG_STRENGTH_ACTIVE`. */
  val strengthActive: Boolean
    get() = state.strengthActive

  /**
   * The decomp's `DoStrengthFunc FIELD_MOVE_FUNC_SET_ACTIVE`. Boulders on this map become pushable
   * until the player leaves it.
   */
  fun useStrength() =
      checkNotNull(fieldMoves) { FIELD_MOVES_UNAVAILABLE }.activateStrength(session, state)

  /**
   * The decomp's `SetPartyGiratinaForm`. [form] 1 is Origin (entering the Distortion World), 0 is
   * Altered (leaving it). Only party Giratina change.
   */
  fun setPartyGiratinaForm(form: Int) =
      checkNotNull(player) { STORY_PLAYER_UNAVAILABLE }.setPartyGiratinaForm(session, state, form)

  /** Set where a MAP_DYNAMIC warp sends this player (the decomp setdynamicwarp). */
  fun setDynamicWarp(regionId: Int, bankId: Int, mapId: Int, x: Int, y: Int, facing: Direction) =
      movement.setDynamicWarp(
          state,
          DynamicWarp(
              regionId.toByte(), bankId.toByte(), mapId.toByte(), x.toShort(), y.toShort(), facing))

  /**
   * Warps the player without door movement, then runs the destination map's entry scripts on this
   * same coroutine, the way the decomp's warp continues into the new map's scripts.
   */
  suspend fun warp(regionId: Int, bankId: Int, mapId: Int, x: Int, y: Int, facing: Direction) {
    val warpService = checkNotNull(warp) { "Script warp service is unavailable" }
    state.scriptOwnsMapEntry = true
    try {
      warpService.warp(
          session,
          state,
          DynamicWarp(
              regionId.toByte(),
              bankId.toByte(),
              mapId.toByte(),
              x.toShort(),
              y.toShort(),
              facing,
          ),
      )
      val destination = maps?.getMap(regionId, bankId, mapId) ?: return
      val scripts = entryScripts ?: return
      scripts.onEntry(state, destination).forEach { it.run(this) }
      state.characterId?.let { charId ->
        scripts.atCoordinate(charId, destination, state.x.toInt(), state.y.toInt())?.run(this)
      }
    } finally {
      state.scriptOwnsMapEntry = false
    }
  }

  private fun gameCompletedKey(): String = "$regionName/FLAG_GAME_COMPLETED"

  private companion object {
    // Sign boxes have no speaker, npc boxes point at the entity.
    const val SIGN = 3
    const val NPC = 4
    const val FEMALE: Byte = 1
    // The three names the DS Buffer* commands read off a save this server does not keep: the
    // cartridge names the rival in Rowan's opening and the counterpart is whichever protagonist
    // the player left, so both are the defaults Platinum ships.
    const val RIVAL_NAME = "Barry"
    const val COUNTERPART_NAME_MALE = "Lucas"
    const val COUNTERPART_NAME_FEMALE = "Dawn"
    const val STORY_PLAYER_UNAVAILABLE = "Story player service is unavailable"
    const val FIELD_MOVES_UNAVAILABLE = "Field move service is unavailable"
  }
}
