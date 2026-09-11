package de.fiereu.openmmo.server.game.testsupport

import de.fiereu.openmmo.common.auth.SessionTokenVerifier
import de.fiereu.openmmo.items.ItemRegistry
import de.fiereu.openmmo.maps.MapManager
import de.fiereu.openmmo.moves.MoveRegistry
import de.fiereu.openmmo.pokemon.LearnsetRegistry
import de.fiereu.openmmo.pokemon.SpeciesRegistry
import de.fiereu.openmmo.server.game.battle.BattlePacketEmitter
import de.fiereu.openmmo.server.game.battle.BattleRegistry
import de.fiereu.openmmo.server.game.battle.BattleRewards
import de.fiereu.openmmo.server.game.battle.MoveLearner
import de.fiereu.openmmo.server.game.battle.TurnEngine
import de.fiereu.openmmo.server.game.battle.WildMonFactory
import de.fiereu.openmmo.server.game.config.DbConfig
import de.fiereu.openmmo.server.game.config.GameServerConfig
import de.fiereu.openmmo.server.game.script.ScriptRegistry
import de.fiereu.openmmo.server.game.script.ScriptRunner
import de.fiereu.openmmo.server.game.services.BattleService
import de.fiereu.openmmo.server.game.services.BlackoutService
import de.fiereu.openmmo.server.game.services.ChatLimits
import de.fiereu.openmmo.server.game.services.DialogService
import de.fiereu.openmmo.server.game.services.EncounterService
import de.fiereu.openmmo.server.game.services.EncounterVariantService
import de.fiereu.openmmo.server.game.services.FieldMoveService
import de.fiereu.openmmo.server.game.services.GrantBudget
import de.fiereu.openmmo.server.game.services.GuildService
import de.fiereu.openmmo.server.game.services.LoginService
import de.fiereu.openmmo.server.game.services.MailService
import de.fiereu.openmmo.server.game.services.MapEntryScripts
import de.fiereu.openmmo.server.game.services.MapLoadService
import de.fiereu.openmmo.server.game.services.MapScriptService
import de.fiereu.openmmo.server.game.services.MovementService
import de.fiereu.openmmo.server.game.services.MultiplayerService
import de.fiereu.openmmo.server.game.services.NpcService
import de.fiereu.openmmo.server.game.services.PresenceService
import de.fiereu.openmmo.server.game.services.SafariService
import de.fiereu.openmmo.server.game.services.ScriptMovementService
import de.fiereu.openmmo.server.game.services.ScriptWarpService
import de.fiereu.openmmo.server.game.services.ShopService
import de.fiereu.openmmo.server.game.services.SocialService
import de.fiereu.openmmo.server.game.services.StaticEncounterService
import de.fiereu.openmmo.server.game.services.StoryPlayerService
import de.fiereu.openmmo.server.game.services.StoryService
import de.fiereu.openmmo.server.game.services.TrainerSightService
import de.fiereu.openmmo.server.game.services.ViolationLog
import de.fiereu.openmmo.server.game.services.WarpService
import de.fiereu.openmmo.server.game.services.WorldClock
import de.fiereu.openmmo.server.game.services.WorldStateService
import de.fiereu.openmmo.server.game.session.SessionRegistry
import de.fiereu.openmmo.server.game.storage.CharacterStore
import de.fiereu.openmmo.server.game.storage.EntityIdService
import de.fiereu.openmmo.server.game.storage.GuildStore
import de.fiereu.openmmo.server.game.storage.InMemoryGuildRepository
import de.fiereu.openmmo.server.game.storage.InMemoryMailRepository
import de.fiereu.openmmo.server.game.storage.InMemoryOfflineItemRepository
import de.fiereu.openmmo.server.game.storage.InMemorySaveBlockRepository
import de.fiereu.openmmo.server.game.storage.InMemorySocialRepository
import de.fiereu.openmmo.server.game.storage.SocialStore
import de.fiereu.openmmo.server.game.world.interest.InterestManager
import de.fiereu.openmmo.server.game.world.interest.PassThroughInterestPolicy
import de.fiereu.openmmo.trainer.TrainerRegistry
import de.fiereu.openmmo.typechart.TypeChart
import javax.inject.Provider

/**
 * A real [MovementService] with every collaborator built for real, since none of them are worth
 * faking. Pass a [mapManager] that already holds the maps the test walks on.
 */
fun movementService(
    store: CharacterStore,
    mapManager: MapManager = MapManager(),
    /** Pass one to hold the same instance a test spawns its observers into. */
    presence: PresenceService? = null,
    /** Pass one holding the scripts the walked map's events name, keyed the way it keys them. */
    scripts: ScriptRegistry = ScriptRegistry(emptyMap()),
): MovementService {
  val mapLoad = MapLoadService(mapManager, SpeciesRegistry())
  val interest = InterestManager()
  @Suppress("NAME_SHADOWING")
  val presence = presence ?: PresenceService(interest, PassThroughInterestPolicy(), mapLoad, store)
  val npcs = NpcService(mapManager, store)
  val story = StoryService(store)
  val battles = battleService(store, interest, mapManager)
  val entryScripts = MapEntryScripts(scripts, story)
  return MovementService(
      WarpService(mapLoad, mapManager, store, presence),
      mapLoad,
      npcs,
      presence,
      mapManager,
      store,
      EncounterService(
          store,
          battles,
          ItemRegistry(),
          mapManager,
          EncounterVariantService(WorldClock()),
          safariService(store, mapManager, presence)),
      MapScriptService(
          entryScripts,
          scriptRunner(store, mapManager, interest, battles, scripts),
          blackoutService(store) { scriptRunner(store, mapManager, interest, battles, scripts) },
          fieldMoveService(store, mapManager, presence),
      ),
      trainerSightService(store, mapManager, interest, battles, scripts),
      fieldMoveService(store, mapManager, presence),
      safariService(store, mapManager, presence),
      ViolationLog(),
  )
}

/** A real [FieldMoveService], which shares the presence a test spawns its observers into. */
fun fieldMoveService(
    store: CharacterStore,
    mapManager: MapManager = MapManager(),
    presence: PresenceService =
        PresenceService(
            InterestManager(),
            PassThroughInterestPolicy(),
            MapLoadService(mapManager, SpeciesRegistry()),
            store),
): FieldMoveService =
    FieldMoveService(
        mapManager,
        store,
        presence,
        ScriptMovementService(mapManager, NpcService(mapManager, store), store),
    )

/** A real [TrainerSightService], whose cone reads the real trainer table. */
fun trainerSightService(
    store: CharacterStore,
    mapManager: MapManager = MapManager(),
    interest: InterestManager = InterestManager(),
    battles: BattleService = battleService(store, interest, mapManager),
    scripts: ScriptRegistry = ScriptRegistry(emptyMap()),
): TrainerSightService =
    TrainerSightService(
        NpcService(mapManager, store),
        TrainerRegistry(),
        StoryService(store),
        scriptRunner(store, mapManager, interest, battles, scripts),
    )

/**
 * A real [LoginService], with every collaborator built for real. The character-list and
 * character-creation handlers touch only the store and the session, so nothing here is faked; the
 * token secret is any non-empty one, since a join is not what this builds.
 */
/** The secret a [loginService] built here verifies session tokens against. */
val loginTestSecret: ByteArray = "a test secret".toByteArray()

fun loginService(
    store: CharacterStore,
    mapManager: MapManager = MapManager(),
    /** Pass one to be able to ask what the service bound while answering. */
    sessions: SessionRegistry = SessionRegistry(),
): LoginService {
  val mapLoad = MapLoadService(mapManager, SpeciesRegistry())
  val interest = InterestManager()
  val presence = PresenceService(interest, PassThroughInterestPolicy(), mapLoad, store)
  val story = StoryService(store)
  return LoginService(
      mapLoad,
      NpcService(mapManager, store),
      MultiplayerService(sessions),
      SocialService(SocialStore(InMemorySocialRepository()), sessions, store),
      GuildService(GuildStore(InMemoryGuildRepository()), store, sessions),
      MailService(InMemoryMailRepository(), store, sessions),
      sessions,
      mapManager,
      store,
      presence,
      MapScriptService(
          MapEntryScripts(ScriptRegistry(emptyMap()), story),
          scriptRunner(store, mapManager, interest),
          blackoutService(store) { scriptRunner(store, mapManager, interest) },
          fieldMoveService(store, mapManager, presence),
      ),
      SessionTokenVerifier(loginTestSecret),
      WorldStateService(WorldClock()),
      InMemorySaveBlockRepository(),
      fieldMoveService(store, mapManager, presence),
      ViolationLog(),
  )
}

/**
 * A real [BlackoutService]. The runner is passed as a factory because the script it launches can
 * start a battle, and the battle service that ends one holds this.
 */
fun blackoutService(store: CharacterStore, runner: () -> ScriptRunner): BlackoutService =
    BlackoutService(store, storyPlayerService(store), Provider { runner() })

/** A real [StoryPlayerService] over [store]. */
fun storyPlayerService(store: CharacterStore): StoryPlayerService {
  val species = SpeciesRegistry()
  val moves = MoveRegistry()
  return StoryPlayerService(
      store,
      WildMonFactory(species, moves, LearnsetRegistry(), EntityIdService()),
      species,
      moves,
      ItemRegistry(),
  )
}

/**
 * A real [ScriptRunner]. It launches on whatever scope the session carries under SCRIPT_SCOPE, so
 * put the test's own scope there to be able to step a script.
 */
fun scriptRunner(
    store: CharacterStore,
    mapManager: MapManager = MapManager(),
    interest: InterestManager = InterestManager(),
    battles: BattleService = battleService(store, interest),
    scripts: ScriptRegistry = ScriptRegistry(emptyMap()),
    /**
     * The white out a script may leave owed. Its own runner factory is never reached from here: the
     * deferred white out runs on the coroutine that is already inside this runner.
     */
    blackout: () -> BlackoutService = {
      blackoutService(store) { error("this fixture's white out starts no script of its own") }
    },
): ScriptRunner {
  val mapLoad = MapLoadService(mapManager, SpeciesRegistry())
  val presence = PresenceService(interest, PassThroughInterestPolicy(), mapLoad, store)
  val npcs = NpcService(mapManager, store)
  val story = StoryService(store)
  val species = SpeciesRegistry()
  val moves = MoveRegistry()
  val wildMons = WildMonFactory(species, moves, LearnsetRegistry(), EntityIdService())
  val items = ItemRegistry()
  return ScriptRunner(
      DialogService(),
      story,
      ScriptMovementService(mapManager, npcs, store),
      ScriptWarpService(mapManager, mapLoad, store, presence),
      StoryPlayerService(store, wildMons, species, moves, items),
      battles,
      store,
      mapManager,
      MapEntryScripts(scripts, story),
      ShopService(store, items, InMemoryOfflineItemRepository()),
      FieldMoveService(mapManager, store, presence, ScriptMovementService(mapManager, npcs, store)),
      Provider { blackout() },
  )
}

fun staticEncounterService(
    store: CharacterStore,
    mapManager: MapManager = MapManager(),
    interest: InterestManager = InterestManager(),
    battles: BattleService = battleService(store, interest, mapManager),
): StaticEncounterService = StaticEncounterService(NpcService(mapManager, store), battles, store)

/**
 * A real [SafariService]; its warp out of the marsh needs the same collaborators a script's does.
 */
fun safariService(
    store: CharacterStore,
    mapManager: MapManager = MapManager(),
    presence: PresenceService =
        PresenceService(
            InterestManager(),
            PassThroughInterestPolicy(),
            MapLoadService(mapManager, SpeciesRegistry()),
            store),
): SafariService =
    SafariService(
        store,
        ScriptWarpService(
            mapManager, MapLoadService(mapManager, SpeciesRegistry()), store, presence))

fun battleService(
    store: CharacterStore,
    interest: InterestManager,
    mapManager: MapManager = MapManager(),
    safari: SafariService = safariService(store, mapManager),
): BattleService {
  val species = SpeciesRegistry()
  val moves = MoveRegistry()
  lateinit var battles: BattleService
  val blackout = blackoutService(store) { scriptRunner(store, mapManager, interest, battles) }
  battles =
      BattleService(
          characterStore = store,
          battles = BattleRegistry(),
          engine = TurnEngine(moves, TypeChart()),
          wildMons = WildMonFactory(species, moves, LearnsetRegistry(), EntityIdService()),
          emitter = BattlePacketEmitter(interest),
          rewards = BattleRewards(),
          moveLearner = MoveLearner(LearnsetRegistry(), moves),
          interestManager = interest,
          speciesRegistry = species,
          moveRegistry = moves,
          trainers = TrainerRegistry(),
          items = ItemRegistry(),
          blackout = blackout,
          budget = GrantBudget(),
          violations = ViolationLog(),
          safariService = safari,
          chatLimits = ChatLimits(ViolationLog()),
      )
  return battles
}

/**
 * A config for the one thing a unit test ever wants out of it, the session secret that
 * [de.fiereu.openmmo.server.game.services.ReportedIndividual] keys its derivation on.
 */
fun testGameConfig(secret: String = "test-session-secret"): GameServerConfig =
    GameServerConfig(
        host = "127.0.0.1",
        port = 0,
        checksumSize = 16,
        rootKeyResource = "game.private.pem",
        rootKey = null,
        rootKeyFile = null,
        sessionSecret = secret.toByteArray(),
        sessionTokenMaxAge = java.time.Duration.ofMinutes(5),
        db = DbConfig(),
    )
