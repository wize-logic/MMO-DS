package de.fiereu.openmmo.server.game.services.command

import de.fiereu.network.PacketEvent
import de.fiereu.openmmo.common.Pokemon
import de.fiereu.openmmo.common.PokemonMove
import de.fiereu.openmmo.common.enums.CharacterGender
import de.fiereu.openmmo.common.enums.EVs
import de.fiereu.openmmo.common.enums.IVs
import de.fiereu.openmmo.common.enums.PokemonContainer
import de.fiereu.openmmo.common.enums.Region
import de.fiereu.openmmo.items.ItemRegistry
import de.fiereu.openmmo.maps.MapManager
import de.fiereu.openmmo.moves.MoveRegistry
import de.fiereu.openmmo.net.game.packets.ChatMessagePacket
import de.fiereu.openmmo.net.game.packets.DuelInvitePacket
import de.fiereu.openmmo.net.game.packets.InGameChallengeResponsePacket
import de.fiereu.openmmo.net.game.packets.LinkBattleOpenPacket
import de.fiereu.openmmo.net.game.packets.battle.BattleFieldStatePacket
import de.fiereu.openmmo.pokemon.LearnsetRegistry
import de.fiereu.openmmo.pokemon.SpeciesRegistry
import de.fiereu.openmmo.server.game.battle.BattlePacketEmitter
import de.fiereu.openmmo.server.game.battle.BattleRegistry
import de.fiereu.openmmo.server.game.battle.BattleRewards
import de.fiereu.openmmo.server.game.battle.MoveLearner
import de.fiereu.openmmo.server.game.battle.TurnEngine
import de.fiereu.openmmo.server.game.battle.WildMonFactory
import de.fiereu.openmmo.server.game.services.BattleService
import de.fiereu.openmmo.server.game.services.DuelService
import de.fiereu.openmmo.server.game.services.GrantBudget
import de.fiereu.openmmo.server.game.services.ViolationLog
import de.fiereu.openmmo.server.game.session.CLIENT_RUNS_SCRIPTS
import de.fiereu.openmmo.server.game.session.SessionRegistry
import de.fiereu.openmmo.server.game.storage.CharacterStore
import de.fiereu.openmmo.server.game.storage.EntityIdService
import de.fiereu.openmmo.server.game.testsupport.FakeCharacterRepository
import de.fiereu.openmmo.server.game.testsupport.FakeSession
import de.fiereu.openmmo.server.game.testsupport.blackoutService
import de.fiereu.openmmo.server.game.testsupport.scriptRunner
import de.fiereu.openmmo.server.game.world.interest.InterestManager
import de.fiereu.openmmo.trainer.TrainerRegistry
import de.fiereu.openmmo.typechart.TypeChart
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.collections.shouldBeEmpty
import io.kotest.matchers.collections.shouldNotBeEmpty
import io.kotest.matchers.nulls.shouldBeNull
import io.kotest.matchers.nulls.shouldNotBeNull
import io.kotest.matchers.shouldBe
import io.kotest.matchers.string.shouldContain
import java.time.LocalDateTime
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.runTest

private const val TACKLE: Short = 33

private fun starter(ownerId: Long): Pokemon =
    Pokemon(
        id = EntityIdService().newMonsterId(),
        ownerId = ownerId,
        container = PokemonContainer.PARTY,
        containerSlot = 0,
        dexId = 1,
        seed = 0,
        ot = "Ash",
        nickname = "",
        level = 50,
        hp = 999,
        xp = 0,
        eVs = EVs(),
        iVs = IVs(),
        moves =
            listOf(
                PokemonMove(TACKLE, 35), PokemonMove(0, 0), PokemonMove(0, 0), PokemonMove(0, 0)),
        isShiny = false,
        hasHiddenAbility = false,
        isAlpha = false,
        isSecret = false,
        isFatefulEncounter = false,
        isRaidEncounter = false,
        caughtAt = LocalDateTime.now(),
    )

private class ChallengeFixture(scope: CoroutineScope) {
  val repo = FakeCharacterRepository()
  val store = CharacterStore(repo, EntityIdService(), scope)
  val sessions = SessionRegistry()
  val interestManager = InterestManager()
  val registry = BattleRegistry()
  val mapManager = MapManager()
  val battles: BattleService =
      BattleService(
          characterStore = store,
          battles = registry,
          engine = TurnEngine(MoveRegistry(), TypeChart()),
          wildMons =
              WildMonFactory(
                  SpeciesRegistry(), MoveRegistry(), LearnsetRegistry(), EntityIdService()),
          emitter = BattlePacketEmitter(interestManager),
          rewards = BattleRewards(),
          moveLearner = MoveLearner(LearnsetRegistry(), MoveRegistry()),
          interestManager = interestManager,
          speciesRegistry = SpeciesRegistry(),
          moveRegistry = MoveRegistry(),
          trainers = TrainerRegistry(),
          items = ItemRegistry(),
          blackout =
              blackoutService(store) { scriptRunner(store, mapManager, interestManager, battles) },
          budget = GrantBudget(),
          violations = ViolationLog(),
      )
  val duels = DuelService(sessions, store, battles)
  val command = ChallengeCommand(battles, duels, sessions, store)

  /** The challenged player's answer, as the handler would deliver it. */
  fun answer(session: FakeSession, accepted: Boolean) =
      duels.onResponse(PacketEvent(InGameChallengeResponsePacket(accepted, ""), session))

  suspend fun seated(name: String, userId: Int): Pair<FakeSession, Long> {
    val created = store.createCharacter(userId, name, CharacterGender.MALE, Region.HOENN)
    store.addPokemon(created.info.id, starter(created.info.id))
    val session = FakeSession(created.info.id)
    sessions.bindCharacter(session, created.info.id)
    return session to created.info.id
  }
}

private fun FakeSession.replies() = sent.filterIsInstance<ChatMessagePacket>().map { it.message }

private fun duelsInBattle(fx: ChallengeFixture, a: Long, b: Long): Boolean =
    fx.duels.inLinkBattle(a) && fx.duels.inLinkBattle(b)

@OptIn(ExperimentalCoroutinesApi::class)
class ChallengeCommandTest :
    FunSpec({
      test("a challenge is an offer, and nothing starts until it is accepted") {
        runTest {
          val fx = ChallengeFixture(backgroundScope)
          val (ash, ashId) = fx.seated("Ash", 1)
          val (misty, mistyId) = fx.seated("Misty", 2)
          val service = ChatCommandService(fx.store, setOf(fx.command))

          service.tryHandle(ash, "/challenge Misty") shouldBe true

          misty.sent.filterIsInstance<DuelInvitePacket>().single().name shouldBe "Ash"
          fx.registry.byChar(ashId).shouldBeNull()
          fx.registry.byChar(mistyId).shouldBeNull()
          ash.sent.filterIsInstance<BattleFieldStatePacket>().shouldBeEmpty()

          fx.answer(misty, accepted = true)

          fx.registry.byChar(ashId).shouldNotBeNull()
          fx.registry.byChar(mistyId).shouldNotBeNull()
          ash.sent.filterIsInstance<BattleFieldStatePacket>().shouldNotBeEmpty()
          misty.sent.filterIsInstance<BattleFieldStatePacket>().shouldNotBeEmpty()
        }
      }

      test("a declined challenge starts nothing and tells the challenger") {
        runTest {
          val fx = ChallengeFixture(backgroundScope)
          val (ash, ashId) = fx.seated("Ash", 1)
          val (misty, mistyId) = fx.seated("Misty", 2)
          val service = ChatCommandService(fx.store, setOf(fx.command))

          service.tryHandle(ash, "/challenge Misty") shouldBe true
          fx.answer(misty, accepted = false)

          fx.registry.byChar(ashId).shouldBeNull()
          fx.registry.byChar(mistyId).shouldBeNull()
          ash.replies().last() shouldContain "declined"
        }
      }

      test("two engine clients get a link battle, one net id each") {
        runTest {
          val fx = ChallengeFixture(backgroundScope)
          val (ash, ashId) = fx.seated("Ash", 1)
          val (misty, mistyId) = fx.seated("Misty", 2)
          ash.attributes[CLIENT_RUNS_SCRIPTS] = true
          misty.attributes[CLIENT_RUNS_SCRIPTS] = true
          val service = ChatCommandService(fx.store, setOf(fx.command))

          service.tryHandle(ash, "/challenge Misty") shouldBe true
          fx.answer(misty, accepted = true)

          val toAsh = ash.sent.filterIsInstance<LinkBattleOpenPacket>().single()
          val toMisty = misty.sent.filterIsInstance<LinkBattleOpenPacket>().single()
          // The challenger computes the fight; each side is told the other's party.
          toAsh.netId shouldBe 0
          toMisty.netId shouldBe 1
          toAsh.battleId shouldBe toMisty.battleId
          toAsh.peerName shouldBe "Misty"
          toMisty.peerName shouldBe "Ash"
          toAsh.party.single().ownerId shouldBe mistyId
          toMisty.party.single().ownerId shouldBe ashId
          // No server-side simulation was opened for either of them.
          fx.registry.byChar(ashId).shouldBeNull()
          fx.registry.byChar(mistyId).shouldBeNull()
          duelsInBattle(fx, ashId, mistyId) shouldBe true
        }
      }

      test("a missing player is refused") {
        runTest {
          val fx = ChallengeFixture(backgroundScope)
          val (ash, ashId) = fx.seated("Ash", 1)
          val service = ChatCommandService(fx.store, setOf(fx.command))

          service.tryHandle(ash, "/challenge Nobody") shouldBe true

          fx.registry.byChar(ashId).shouldBeNull()
          ash.replies().single() shouldContain "not in the world"
        }
      }

      test("a player on another map is too far") {
        runTest {
          val fx = ChallengeFixture(backgroundScope)
          val (ash, ashId) = fx.seated("Ash", 1)
          val created = fx.store.createCharacter(2, "Misty", CharacterGender.FEMALE, Region.HOENN)
          fx.store.addPokemon(created.info.id, starter(created.info.id))
          val misty = FakeSession(created.info.id, bankId = 50, mapId = 16)
          fx.sessions.bindCharacter(misty, created.info.id)
          val service = ChatCommandService(fx.store, setOf(fx.command))

          service.tryHandle(ash, "/challenge Misty") shouldBe true

          fx.registry.byChar(ashId).shouldBeNull()
          ash.replies().single() shouldContain "too far"
        }
      }
    })
