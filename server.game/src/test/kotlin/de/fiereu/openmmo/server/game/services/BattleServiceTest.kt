package de.fiereu.openmmo.server.game.services

import de.fiereu.network.PacketEvent
import de.fiereu.openmmo.common.MAX_PARTY_SIZE
import de.fiereu.openmmo.common.Pokemon
import de.fiereu.openmmo.common.PokemonMove
import de.fiereu.openmmo.common.enums.BattleAction
import de.fiereu.openmmo.common.enums.CharacterGender
import de.fiereu.openmmo.common.enums.ChatType
import de.fiereu.openmmo.common.enums.EVs
import de.fiereu.openmmo.common.enums.GrowthRate
import de.fiereu.openmmo.common.enums.IVs
import de.fiereu.openmmo.common.enums.PokemonContainer
import de.fiereu.openmmo.common.enums.Region
import de.fiereu.openmmo.items.ItemRegistry
import de.fiereu.openmmo.maps.MapManager
import de.fiereu.openmmo.moves.MoveRegistry
import de.fiereu.openmmo.net.game.packets.ChatMessagePacket
import de.fiereu.openmmo.net.game.packets.EntityMovePpPacket
import de.fiereu.openmmo.net.game.packets.EntityPresencePacket
import de.fiereu.openmmo.net.game.packets.LocalCharacterDeltaPacket
import de.fiereu.openmmo.net.game.packets.MapLoadedAckPacket
import de.fiereu.openmmo.net.game.packets.PokemonContainerPacket
import de.fiereu.openmmo.net.game.packets.battle.BattleActionSelectPacket
import de.fiereu.openmmo.net.game.packets.battle.BattleBulkStatePacket
import de.fiereu.openmmo.net.game.packets.battle.BattleChatMessagePacket
import de.fiereu.openmmo.net.game.packets.battle.BattleEntityDeltaPacket
import de.fiereu.openmmo.net.game.packets.battle.BattleEntityMoveEventPacket
import de.fiereu.openmmo.net.game.packets.battle.BattleFieldStatePacket
import de.fiereu.openmmo.net.game.packets.battle.BattleListEventPacket
import de.fiereu.openmmo.net.game.packets.battle.BattleQueuedEventPacket
import de.fiereu.openmmo.net.game.packets.battle.BattleSidePacket
import de.fiereu.openmmo.net.game.packets.battle.BattleSwitchInPacket
import de.fiereu.openmmo.net.game.packets.battle.BattleTileMapPacket
import de.fiereu.openmmo.net.game.packets.battle.OpposingSide
import de.fiereu.openmmo.net.game.packets.battle.moves.MOVE_LEARN_NO_SLOT
import de.fiereu.openmmo.net.game.packets.battle.moves.MoveLearnPromptPacket
import de.fiereu.openmmo.net.game.packets.battle.moves.MoveLearnReplyPacket
import de.fiereu.openmmo.pokemon.LearnsetRegistry
import de.fiereu.openmmo.pokemon.SpeciesRegistry
import de.fiereu.openmmo.server.game.battle.BattlePacketEmitter
import de.fiereu.openmmo.server.game.battle.BattleRegistry
import de.fiereu.openmmo.server.game.battle.BattleRewards
import de.fiereu.openmmo.server.game.battle.ExpCurves
import de.fiereu.openmmo.server.game.battle.MoveLearner
import de.fiereu.openmmo.server.game.battle.TurnEngine
import de.fiereu.openmmo.server.game.battle.WildMonFactory
import de.fiereu.openmmo.server.game.session.SCRIPT_SCOPE
import de.fiereu.openmmo.server.game.storage.CharacterStore
import de.fiereu.openmmo.server.game.storage.EntityIdService
import de.fiereu.openmmo.server.game.storage.PC_STORAGE_SIZE
import de.fiereu.openmmo.server.game.testsupport.FakeCharacterRepository
import de.fiereu.openmmo.server.game.testsupport.FakeSession
import de.fiereu.openmmo.server.game.testsupport.blackoutService
import de.fiereu.openmmo.server.game.testsupport.scriptRunner
import de.fiereu.openmmo.server.game.world.interest.InterestManager
import de.fiereu.openmmo.trainer.TrainerDef
import de.fiereu.openmmo.trainer.TrainerMon
import de.fiereu.openmmo.trainer.TrainerRegistry
import de.fiereu.openmmo.typechart.TypeChart
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.booleans.shouldBeTrue
import io.kotest.matchers.collections.shouldContain
import io.kotest.matchers.collections.shouldNotBeEmpty
import io.kotest.matchers.ints.shouldBeGreaterThan
import io.kotest.matchers.nulls.shouldBeNull
import io.kotest.matchers.nulls.shouldNotBeNull
import io.kotest.matchers.shouldBe
import java.time.LocalDateTime
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.advanceUntilIdle
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest

private const val TACKLE: Short = 33
private const val GROWL: Short = 45
private const val EMBER: Short = 52
private const val WATER_GUN: Short = 55
private const val LEECH_SEED: Short = 73
private const val RATTATA = 19

private fun bulbasaur(
    ownerId: Long,
    level: Byte,
    hp: Short,
    xp: Int = 0,
    moveIds: List<Short> = listOf(TACKLE, 0, 0, 0),
): Pokemon =
    Pokemon(
        id = EntityIdService().newMonsterId(),
        ownerId = ownerId,
        container = PokemonContainer.PARTY,
        containerSlot = 0,
        dexId = 1,
        seed = 0,
        ot = "Ash",
        nickname = "",
        level = level,
        hp = hp,
        xp = xp,
        eVs = EVs(),
        iVs = IVs(),
        moves = moveIds.map { PokemonMove(it, if (it == 0.toShort()) 0 else 35) },
        isShiny = false,
        hasHiddenAbility = false,
        isAlpha = false,
        isSecret = false,
        isFatefulEncounter = false,
        isRaidEncounter = false,
        caughtAt = LocalDateTime.now(),
    )

private class Fixture(scope: CoroutineScope) {
  val repo = FakeCharacterRepository()
  val store = CharacterStore(repo, EntityIdService(), scope)
  val interestManager = InterestManager()
  val registry = BattleRegistry()
  val mapManager = MapManager()
  val blackout =
      blackoutService(store) { scriptRunner(store, mapManager, interestManager, service) }
  val service: BattleService =
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
          blackout = blackout,
          budget = GrantBudget(),
          violations = ViolationLog(),
      )

  suspend fun playerWithParty(
      level: Byte = 50,
      hp: Short = 999,
      xp: Int = 0,
      moveIds: List<Short> = listOf(TACKLE, 0, 0, 0),
      name: String = "Ash",
      userId: Int = 1,
  ): Pair<FakeSession, Long> {
    val created = store.createCharacter(userId, name, CharacterGender.MALE, Region.HOENN)
    store.addPokemon(created.info.id, bulbasaur(created.info.id, level, hp, xp, moveIds))
    // Balls to throw. A throw needs one in the bag and spends it, so a fixture with an empty bag
    // catches nothing; these tests passed because neither was asked.
    store.addItem(created.info.id, POKE_BALL_ITEM_ID, 10)
    store.addItem(created.info.id, MASTER_BALL_ITEM_ID.toInt(), 10)
    return FakeSession(created.info.id) to created.info.id
  }
}

/** The Poke Ball's id in this build, which is what the catch tests throw. */
private const val POKE_BALL_ITEM_ID = 5004

/** The one ball that always holds, for the tests about the catch and not the roll. */
private const val MASTER_BALL_ITEM_ID: Short = 5001

private fun FakeSession.startBattle(service: BattleService, dexId: Int = 19, level: Int = 2) {
  service.startWildBattle(this, dexId, level)
}

// LittlerootTown_BrendansHouse_2F, the respawn a new male Hoenn character starts with, and
// Route101, somewhere else to lose the battle.
private const val RESPAWN_BANK: Byte = 51
private const val RESPAWN_MAP: Byte = 3
private const val ELSEWHERE_BANK: Byte = 50
private const val ELSEWHERE_MAP: Byte = 16

private suspend fun FakeSession.act(
    service: BattleService,
    action: BattleAction,
    value: Short = 0,
) {
  service.onBattleAction(PacketEvent(BattleActionSelectPacket(0, action, value, 0L, 0), this))
}

private fun FakeSession.finishBattleTransition(service: BattleService) {
  service.onClientReady(PacketEvent(MapLoadedAckPacket(), this))
}

@OptIn(ExperimentalCoroutinesApi::class)
class BattleServiceTest :
    FunSpec({
      test("the start sequence arrives in the proven order") {
        runTest {
          val fx = Fixture(backgroundScope)
          val (session, _) = fx.playerWithParty()

          session.startBattle(fx.service)

          val types = session.sent.map { it::class }
          types shouldBe
              listOf(
                  EntityPresencePacket::class,
                  BattleSidePacket::class,
                  BattleFieldStatePacket::class,
                  BattleTileMapPacket::class,
                  BattleQueuedEventPacket::class,
              )
          // Bulbasaur base hp 45 at level 50 with empty IVs and EVs.
          val field = session.sent.filterIsInstance<BattleFieldStatePacket>().single()
          field.playerParty.single().maxHp shouldBe 105.toShort()
          field.opponentParty.single().level shouldBe 2.toByte()
          field.opponentParty.single().currentHp shouldBe field.opponentParty.single().maxHp
        }
      }

      test("an unknown species aborts with a chat notice") {
        runTest {
          val fx = Fixture(backgroundScope)
          val (session, charId) = fx.playerWithParty()

          session.startBattle(fx.service, dexId = 9999, level = 5)

          fx.registry.byChar(charId).shouldBeNull()
          session.sent.none { it is BattleFieldStatePacket }.shouldBeTrue()
        }
      }

      test("an empty party does not start a battle") {
        runTest {
          val fx = Fixture(backgroundScope)
          val created = fx.store.createCharacter(1, "Lucas", CharacterGender.MALE, Region.SINNOH)
          val session = FakeSession(created.info.id)

          session.startBattle(fx.service)

          fx.registry.byChar(created.info.id).shouldBeNull()
          session.sent.none { it is BattleFieldStatePacket }.shouldBeTrue()
          session.sent.filterIsInstance<ChatMessagePacket>().single().message shouldBe
              "You need a monster in your party to battle."
        }
      }

      test("a session joined to the battle key receives the broadcasts") {
        runTest {
          val fx = Fixture(backgroundScope)
          val (session, charId) = fx.playerWithParty()
          session.startBattle(fx.service)
          val battle = fx.registry.byChar(charId).shouldNotBeNull()

          val spectator = FakeSession(200L)
          fx.interestManager.join(spectator, battle.key)

          session.act(fx.service, BattleAction.MOVE, TACKLE)

          spectator.sent.shouldNotBeEmpty()
        }
      }

      test("a move resolves the turn and re-prompts while both sides stand") {
        runTest {
          val fx = Fixture(backgroundScope)
          // A bulky matchup, so neither side can faint inside one turn.
          val (session, charId) = fx.playerWithParty(level = 30, hp = 999)
          session.startBattle(fx.service, dexId = 143, level = 10)
          session.sent.clear()

          session.act(fx.service, BattleAction.MOVE, TACKLE)

          fx.registry.byChar(charId).shouldNotBeNull()
          session.sent.filterIsInstance<EntityMovePpPacket>().shouldNotBeEmpty()
          session.sent.filterIsInstance<BattleQueuedEventPacket>().shouldNotBeEmpty()
        }
      }

      test("fleeing waits for the overworld before clearing the battle") {
        runTest {
          val fx = Fixture(backgroundScope)
          val (session, charId) = fx.playerWithParty()
          session.startBattle(fx.service)

          session.act(fx.service, BattleAction.RUN)

          fx.registry.byChar(charId).shouldNotBeNull()
          fx.service.onClientReady(PacketEvent(MapLoadedAckPacket(byteArrayOf(1, 2, 3)), session))
          fx.registry.byChar(charId).shouldNotBeNull()
          session.finishBattleTransition(fx.service)
          fx.registry.byChar(charId).shouldBeNull()
          session.sent.filterIsInstance<BattleBulkStatePacket>().shouldNotBeEmpty()
          session.startBattle(fx.service)
          fx.registry.byChar(charId).shouldNotBeNull()
        }
      }

      test("victory persists the party hp and pp through the store") {
        runTest {
          val fx = Fixture(this)
          val (session, charId) = fx.playerWithParty()
          session.startBattle(fx.service)

          var rounds = 0
          while (fx.registry.byChar(charId)?.pendingResult == null && rounds < 10) {
            session.act(fx.service, BattleAction.MOVE, TACKLE)
            rounds += 1
          }
          fx.registry.byChar(charId).shouldNotBeNull()
          session.finishBattleTransition(fx.service)
          fx.registry.byChar(charId).shouldBeNull()
          advanceUntilIdle()

          // The level 2 Rattata yields its base experience scaled by level over seven.
          session.sent
              .filterIsInstance<BattleEntityDeltaPacket>()
              .any { it.experience != null }
              .shouldBeTrue()
          val saved = fx.repo.saved[charId].shouldNotBeNull()
          saved.pokemon.single().moves[0].pp shouldBe (35 - rounds).toByte()
          saved.pokemon.single().xp shouldBe 57 * 2 / 7
        }
      }

      // A level up with four moves already is the one case that needs the player: the server holds
      // the move until it is told which one to drop, and the client's chooser is what tells it.
      test("a full moveset is offered the move it cannot fit and takes the slot picked for it") {
        runTest {
          val fx = Fixture(this)
          // One level short of Leech Seed, with nothing free to put it in.
          val (session, charId) =
              fx.playerWithParty(
                  level = 6,
                  xp = ExpCurves.totalXpFor(GrowthRate.MEDIUM_SLOW, 7) - 1,
                  moveIds = listOf(TACKLE, GROWL, EMBER, WATER_GUN),
              )
          session.startBattle(fx.service)

          var rounds = 0
          while (fx.registry.byChar(charId)?.pendingResult == null && rounds < 10) {
            session.act(fx.service, BattleAction.MOVE, TACKLE)
            rounds += 1
          }

          val offer = session.sent.filterIsInstance<MoveLearnPromptPacket>().single()
          offer.moveId shouldBe LEECH_SEED
          offer.slot shouldBe MOVE_LEARN_NO_SLOT
          val monId = fx.store.getCharacter(charId).shouldNotBeNull().pokemon.single().id
          offer.entityId shouldBe monId

          fx.service.onMoveLearnReply(
              PacketEvent(MoveLearnReplyPacket(monId, 2, LEECH_SEED), session))
          advanceUntilIdle()

          fx.store.getCharacter(charId).shouldNotBeNull().pokemon.single().moves.map {
            it.id
          } shouldBe listOf(TACKLE, GROWL, LEECH_SEED, WATER_GUN)
          // The party is how the client holds a moveset once the battle screen is gone.
          session.sent
              .filterIsInstance<PokemonContainerPacket>()
              .last()
              .pokemon
              .single()
              .moves
              .map { it.id } shouldBe listOf(TACKLE, GROWL, LEECH_SEED, WATER_GUN)
        }
      }

      test("keeping the moveset leaves it alone and closes the offer") {
        runTest {
          val fx = Fixture(this)
          val (session, charId) =
              fx.playerWithParty(
                  level = 6,
                  xp = ExpCurves.totalXpFor(GrowthRate.MEDIUM_SLOW, 7) - 1,
                  moveIds = listOf(TACKLE, GROWL, EMBER, WATER_GUN),
              )
          session.startBattle(fx.service)
          var rounds = 0
          while (fx.registry.byChar(charId)?.pendingResult == null && rounds < 10) {
            session.act(fx.service, BattleAction.MOVE, TACKLE)
            rounds += 1
          }
          val monId = fx.store.getCharacter(charId).shouldNotBeNull().pokemon.single().id

          fx.service.onMoveLearnReply(
              PacketEvent(MoveLearnReplyPacket(monId, MOVE_LEARN_NO_SLOT, LEECH_SEED), session))
          advanceUntilIdle()

          fx.store.getCharacter(charId).shouldNotBeNull().pokemon.single().moves.map {
            it.id
          } shouldBe listOf(TACKLE, GROWL, EMBER, WATER_GUN)
          // A second answer has nothing left to answer and must not move anything.
          fx.service.onMoveLearnReply(
              PacketEvent(MoveLearnReplyPacket(monId, 0, LEECH_SEED), session))
          advanceUntilIdle()
          fx.store.getCharacter(charId).shouldNotBeNull().pokemon.single().moves.map {
            it.id
          } shouldBe listOf(TACKLE, GROWL, EMBER, WATER_GUN)
        }
      }

      // A move that fits needs no answer, and the client writes its "learned" line from the slot.
      test("a move that fits is announced with the slot it went into") {
        runTest {
          val fx = Fixture(this)
          val (session, charId) =
              fx.playerWithParty(
                  level = 6,
                  xp = ExpCurves.totalXpFor(GrowthRate.MEDIUM_SLOW, 7) - 1,
                  moveIds = listOf(TACKLE, GROWL, 0, 0),
              )
          session.startBattle(fx.service)
          var rounds = 0
          while (fx.registry.byChar(charId)?.pendingResult == null && rounds < 10) {
            session.act(fx.service, BattleAction.MOVE, TACKLE)
            rounds += 1
          }

          val learned = session.sent.filterIsInstance<MoveLearnPromptPacket>().single()
          learned.moveId shouldBe LEECH_SEED
          learned.slot shouldBe 2.toByte()
          fx.store.getCharacter(charId).shouldNotBeNull().pokemon.single().moves.map {
            it.id
          } shouldBe listOf(TACKLE, GROWL, LEECH_SEED, 0)
        }
      }

      test("a trainer sends out its next monster instead of losing when one faints") {
        runTest {
          val fx = Fixture(backgroundScope)
          val (session, charId) = fx.playerWithParty()
          val terry =
              TrainerDef(
                  id = 1,
                  name = "TERRY",
                  trainerClass = 0,
                  doubleBattle = false,
                  prizeRate = 5,
                  party =
                      listOf(
                          TrainerMon(RATTATA, 2, 0, 0, emptyList()),
                          TrainerMon(RATTATA, 2, 0, 0, emptyList()),
                      ),
              )
          // The battle never ends in this test, so its await must not hold the test scope open.
          backgroundScope.launch { fx.service.startTrainerBattle(session, terry) }
          runCurrent()

          val battle = fx.registry.byChar(charId).shouldNotBeNull()
          battle.opponent.size shouldBe 2

          val field = session.sent.filterIsInstance<BattleFieldStatePacket>().single()
          field.opponentParty[0].revealed.shouldBeTrue()
          field.opponentParty[1].revealed shouldBe false

          var rounds = 0
          while (battle.opponentSlot == 0 && rounds < 10) {
            session.act(fx.service, BattleAction.MOVE, TACKLE)
            rounds += 1
          }

          battle.opponentSlot shouldBe 1
          battle.opponent[0].fainted.shouldBeTrue()
          battle.pendingResult.shouldBeNull()
          session.sent.filterIsInstance<BattleSwitchInPacket>().last().side shouldBe 1.toByte()

          // The lead is paid for as it faints, not held back until the whole team is beaten.
          val xpBefore = battle.activeMon().source.xp
          xpBefore shouldBeGreaterThan 0
          session.sent
              .filterIsInstance<BattleEntityDeltaPacket>()
              .any { it.experience != null }
              .shouldBeTrue()

          rounds = 0
          while (battle.pendingResult == null && rounds < 10) {
            session.act(fx.service, BattleAction.MOVE, TACKLE)
            rounds += 1
          }

          // The second monster pays on top of the first, rather than replacing it.
          battle.activeMon().source.xp shouldBeGreaterThan xpBefore

          // The prize is persisted and the new balance goes out on 0x0C, the same
          // packet a shop uses. 4 * lastLevel(2) * prizeRate(5) = 40.
          val after = fx.store.getCharacter(charId).shouldNotBeNull().info.money
          after shouldBe 30040
          session.sent.filterIsInstance<LocalCharacterDeltaPacket>().single().money shouldBe after
        }
      }

      /**
       * The cartridge sends a catch to the box when the party is full. Appending a seventh party
       * member instead made a monster the box screen cannot draw and cannot move out again, its
       * slot is past the six the party addresses.
       */
      test("a catch with a full party goes to the box") {
        runTest {
          val fx = Fixture(this)
          val (session, charId) = fx.playerWithParty()
          repeat(MAX_PARTY_SIZE - 1) {
            fx.store.addPokemon(charId, bulbasaur(charId, 5, 20, 0, listOf(TACKLE, 0, 0, 0)))
          }
          session.startBattle(fx.service)

          session.act(fx.service, BattleAction.ITEM, MASTER_BALL_ITEM_ID)

          val stored = fx.store.getCharacter(charId).shouldNotBeNull()
          stored.pokemon.size shouldBe MAX_PARTY_SIZE
          stored.pcStorage.single().containerSlot shouldBe 0.toShort()
          // The battle's end sends the party and nothing sends the PC, so without this the catch
          // was in the database and nowhere on screen until the next join.
          session.sent
              .filterIsInstance<PokemonContainerPacket>()
              .single { it.container == PokemonContainer.PC }
              .pokemon
              .single()
              .id shouldBe stored.pcStorage.single().id
          session.sent
              .filterIsInstance<ChatMessagePacket>()
              .map { it.message }
              .any { it.contains("Box") }
              .shouldBeTrue()
        }
      }

      /**
       * Six in the party and every box slot taken. The cartridge never lets that encounter start;
       * ours ended the battle with the monster caught and then dropped on the floor, which is the
       * one outcome a player cannot recover from.
       */
      test("a catch with no room anywhere is refused and the battle stands") {
        runTest {
          val fx = Fixture(this)
          val (session, charId) = fx.playerWithParty()
          repeat(MAX_PARTY_SIZE - 1) {
            fx.store.addPokemon(charId, bulbasaur(charId, 5, 20, 0, listOf(TACKLE, 0, 0, 0)))
          }
          repeat(PC_STORAGE_SIZE) {
            fx.store.addPokemon(
                charId,
                bulbasaur(charId, 5, 20, 0, listOf(TACKLE, 0, 0, 0))
                    .copy(container = PokemonContainer.PC),
            )
          }
          session.startBattle(fx.service)

          session.act(fx.service, BattleAction.ITEM, MASTER_BALL_ITEM_ID)

          val stored = fx.store.getCharacter(charId).shouldNotBeNull()
          stored.pokemon.size shouldBe MAX_PARTY_SIZE
          stored.pcStorage.size shouldBe PC_STORAGE_SIZE
          session.sent
              .filterIsInstance<ChatMessagePacket>()
              .map { it.message }
              .any { it.contains("no room") }
              .shouldBeTrue()
          // Refused, not ended: the battle is still the one the player is standing in.
          fx.registry.byChar(charId).shouldNotBeNull()
        }
      }

      test("the ball throw names the ball that was thrown, by the id the client knows") {
        runTest {
          val fx = Fixture(this)
          val (session, _) = fx.playerWithParty()
          session.startBattle(fx.service)

          session.act(fx.service, BattleAction.ITEM, MASTER_BALL_ITEM_ID)

          session.sent
              .filterIsInstance<BattleListEventPacket>()
              .single { it.subKind == 4.toByte() }
              .value shouldBe MASTER_BALL_ITEM_ID
        }
      }

      /**
       * The engine looks a move up in the whole move table, so the id on the wire was the choice.
       */
      test("a monster cannot use a move it does not know") {
        runTest {
          val fx = Fixture(this)
          val (session, _) = fx.playerWithParty(moveIds = listOf(TACKLE, 0, 0, 0))
          session.startBattle(fx.service)
          session.sent.clear()

          // 153 is Explosion, which nothing in this party knows.
          session.act(fx.service, BattleAction.MOVE, 153)

          session.sent.filterIsInstance<BattleListEventPacket>() shouldBe emptyList()
          session.sent
              .filterIsInstance<ChatMessagePacket>()
              .map { it.message }
              .shouldContain("You can't use that now.")
        }
      }

      /** A ball has to be in the bag, and it leaves the bag when it is thrown. */
      test("a ball cannot be thrown from an empty bag, and a thrown one is spent") {
        runTest {
          val fx = Fixture(this)
          val (session, charId) = fx.playerWithParty()
          fx.store.addItem(charId, POKE_BALL_ITEM_ID, -10)
          session.startBattle(fx.service)

          session.act(fx.service, BattleAction.ITEM, POKE_BALL_ITEM_ID.toShort())

          val stored = fx.store.getCharacter(charId).shouldNotBeNull()
          stored.pcStorage.size shouldBe 0
          stored.pokemon.size shouldBe 1

          fx.store.addItem(charId, POKE_BALL_ITEM_ID, 2)
          session.act(fx.service, BattleAction.ITEM, POKE_BALL_ITEM_ID.toShort())
          fx.store.getCharacter(charId)!!.items[POKE_BALL_ITEM_ID] shouldBe 1
        }
      }

      test("a Potion in battle heals instead of throwing a ball") {
        runTest {
          val fx = Fixture(this)
          val (session, charId) = fx.playerWithParty(hp = 1)
          fx.store.addItem(charId, 5017, 1)
          session.startBattle(fx.service)
          val monId = fx.store.getCharacter(charId).shouldNotBeNull().pokemon.single().id
          session.sent.clear()

          fx.service.onBattleAction(
              PacketEvent(BattleActionSelectPacket(0, BattleAction.ITEM, 5017, monId, 0), session),
          )

          session.sent.filterIsInstance<BattleListEventPacket>().filter {
            it.subKind == 4.toByte()
          } shouldBe emptyList()
          session.sent
              .filterIsInstance<BattleEntityDeltaPacket>()
              .any { it.currentHp == 21.toShort() }
              .shouldBeTrue()
          fx.store.getCharacter(charId)!!.items[5017] shouldBe null
        }
      }

      // The recovery path for a wiped party: without it a defeat is terminal, since fainted
      // monsters can neither win the next battle nor walk to a Pokemon Center.
      test("losing the last party member heals it and warps back to the last respawn") {
        runTest {
          val fx = Fixture(backgroundScope)
          val (session, charId) = fx.playerWithParty(level = 2, hp = 1)
          session.attributes[SCRIPT_SCOPE] = backgroundScope
          fx.store.updatePosition(charId, 9, 9, ELSEWHERE_BANK, ELSEWHERE_MAP)
          val moneyBefore = fx.store.getCharacter(charId).shouldNotBeNull().info.money

          session.startBattle(fx.service, dexId = RATTATA, level = 50)
          // The Rattata picks its move at random and some of them do no damage, so a single turn
          // does not always finish a monster on one hp. Fight until the battle is decided.
          var rounds = 0
          while (fx.registry.byChar(charId)?.pendingResult == null && rounds < 10) {
            session.act(fx.service, BattleAction.MOVE, TACKLE)
            rounds += 1
          }
          fx.store.getCharacter(charId).shouldNotBeNull().pokemon.single().hp shouldBe 0.toShort()

          session.finishBattleTransition(fx.service)
          runCurrent()
          advanceUntilIdle()

          val after = fx.store.getCharacter(charId).shouldNotBeNull()
          // Bulbasaur base hp 45 at level 2 with empty IVs and EVs.
          after.pokemon.single().hp shouldBe 13.toShort()
          // The respawn a new Hoenn character starts with, from the decomp intro setrespawn.
          after.info.positionBankId shouldBe RESPAWN_BANK
          after.info.positionMapId shouldBe RESPAWN_MAP
          after.info.positionX shouldBe 4.toShort()
          after.info.positionY shouldBe 2.toShort()
          after.info.money shouldBe moneyBefore / 2
        }
      }

      test("a disconnect persists and removes the battle") {
        runTest {
          val fx = Fixture(this)
          val (session, charId) = fx.playerWithParty()
          session.startBattle(fx.service)
          session.act(fx.service, BattleAction.MOVE, TACKLE)

          fx.service.onDisconnect(session)

          fx.registry.byChar(charId).shouldBeNull()
          advanceUntilIdle()
          fx.repo.saved[charId].shouldNotBeNull()
        }
      }

      test("a player battle opens the field for both sides") {
        runTest {
          val fx = Fixture(backgroundScope)
          val (ash, ashId) = fx.playerWithParty(name = "Ash", userId = 1)
          val (misty, mistyId) = fx.playerWithParty(name = "Misty", userId = 2)

          fx.service.startPlayerBattle(ash, misty)

          val battle = fx.registry.byChar(ashId).shouldNotBeNull()
          fx.registry.byChar(mistyId) shouldBe battle
          battle.isPvp.shouldBeTrue()
          val ashField = ash.sent.filterIsInstance<BattleFieldStatePacket>().single()
          val mistyField = misty.sent.filterIsInstance<BattleFieldStatePacket>().single()
          ashField.playerId shouldBe ashId
          mistyField.playerId shouldBe mistyId
          ashField.opposing shouldBe OpposingSide.TRAINER
          mistyField.opposing shouldBe OpposingSide.TRAINER
          ashField.playerParty.single().entityId shouldBe mistyField.opponentParty.single().entityId
        }
      }

      test("a player battle waits for both moves before resolving") {
        runTest {
          val fx = Fixture(backgroundScope)
          val (ash, ashId) = fx.playerWithParty(name = "Ash", userId = 1)
          val (misty, _) = fx.playerWithParty(name = "Misty", userId = 2)
          fx.service.startPlayerBattle(ash, misty)
          ash.sent.clear()
          misty.sent.clear()

          ash.act(fx.service, BattleAction.MOVE, TACKLE)

          val battle = fx.registry.byChar(ashId).shouldNotBeNull()
          battle.pendingPlayerAction.shouldNotBeNull()
          battle.pendingResult.shouldBeNull()
          ash.sent.filterIsInstance<BattleEntityMoveEventPacket>() shouldBe emptyList()

          misty.act(fx.service, BattleAction.MOVE, TACKLE)

          battle.pendingPlayerAction.shouldBeNull()
          ash.sent.filterIsInstance<BattleEntityMoveEventPacket>().shouldNotBeEmpty()
          misty.sent.filterIsInstance<BattleEntityMoveEventPacket>().shouldNotBeEmpty()
        }
      }

      test("forfeit ends a player battle for both") {
        runTest {
          val fx = Fixture(backgroundScope)
          val (ash, ashId) = fx.playerWithParty(name = "Ash", userId = 1)
          val (misty, mistyId) = fx.playerWithParty(name = "Misty", userId = 2)
          fx.service.startPlayerBattle(ash, misty)

          ash.act(fx.service, BattleAction.RUN)
          ash.finishBattleTransition(fx.service)

          fx.registry.byChar(ashId).shouldBeNull()
          fx.registry.byChar(mistyId).shouldBeNull()
          misty.sent.filterIsInstance<ChatMessagePacket>().any {
            it.message.contains("forfeit")
          } shouldBe true
        }
      }

      test("battle chat reaches both players of a player battle") {
        runTest {
          val fx = Fixture(backgroundScope)
          val (ash, ashId) = fx.playerWithParty(name = "Ash", userId = 1)
          val (misty, _) = fx.playerWithParty(name = "Misty", userId = 2)
          fx.service.startPlayerBattle(ash, misty)
          ash.sent.clear()
          misty.sent.clear()

          fx.service.onBattleChat(PacketEvent(BattleChatMessagePacket(0, "gg"), ash))

          val toAsh = ash.sent.filterIsInstance<ChatMessagePacket>().single()
          toAsh.type shouldBe ChatType.BATTLE
          toAsh.message shouldBe "gg"
          toAsh.sender shouldBe "Ash"
          toAsh.senderId shouldBe ashId
          misty.sent.filterIsInstance<ChatMessagePacket>().single() shouldBe toAsh
        }
      }

      test("battle chat in a wild battle echoes to the sender alone") {
        runTest {
          val fx = Fixture(backgroundScope)
          val (session, _) = fx.playerWithParty()
          session.startBattle(fx.service)
          session.sent.clear()

          fx.service.onBattleChat(PacketEvent(BattleChatMessagePacket(0, "here goes"), session))

          val line = session.sent.filterIsInstance<ChatMessagePacket>().single()
          line.type shouldBe ChatType.BATTLE
          line.message shouldBe "here goes"
        }
      }

      test("battle chat outside a battle is answered with a notice") {
        runTest {
          val fx = Fixture(backgroundScope)
          val (session, _) = fx.playerWithParty()

          fx.service.onBattleChat(PacketEvent(BattleChatMessagePacket(0, "hello?"), session))

          val line = session.sent.filterIsInstance<ChatMessagePacket>().single()
          line.type shouldBe ChatType.GAME_NOTIFICATIONS
          line.message shouldBe "You are not in a battle."
        }
      }
    })
