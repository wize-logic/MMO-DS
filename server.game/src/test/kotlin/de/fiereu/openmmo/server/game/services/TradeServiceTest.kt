package de.fiereu.openmmo.server.game.services

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
import de.fiereu.openmmo.net.game.packets.PokemonContainerPacket
import de.fiereu.openmmo.net.game.packets.StringCommandPacket
import de.fiereu.openmmo.net.game.packets.TradeActionPacket
import de.fiereu.openmmo.net.game.packets.TradeCommPacket
import de.fiereu.openmmo.net.game.packets.TradeListEntryPacket
import de.fiereu.openmmo.net.game.packets.TradeSelectMonPacket
import de.fiereu.openmmo.net.game.packets.TradeStatePacket
import de.fiereu.openmmo.pokemon.LearnsetRegistry
import de.fiereu.openmmo.pokemon.SpeciesRegistry
import de.fiereu.openmmo.server.game.battle.BattlePacketEmitter
import de.fiereu.openmmo.server.game.battle.BattleRegistry
import de.fiereu.openmmo.server.game.battle.BattleRewards
import de.fiereu.openmmo.server.game.battle.MoveLearner
import de.fiereu.openmmo.server.game.battle.TurnEngine
import de.fiereu.openmmo.server.game.battle.WildMonFactory
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
import io.kotest.matchers.shouldBe
import io.kotest.matchers.string.shouldContain
import java.time.LocalDateTime
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.runTest

private const val TACKLE: Short = 33

private fun starter(ownerId: Long, dexId: Int, slot: Short = 0): Pokemon =
    Pokemon(
        id = EntityIdService().newMonsterId(),
        ownerId = ownerId,
        container = PokemonContainer.PARTY,
        containerSlot = slot,
        dexId = dexId,
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

private class TradeFixture(scope: CoroutineScope) {
  val repo = FakeCharacterRepository()
  val store = CharacterStore(repo, EntityIdService(), scope)
  val sessions = SessionRegistry()
  val interestManager = InterestManager()
  val mapManager = MapManager()
  val battles: BattleService =
      BattleService(
          characterStore = store,
          battles = BattleRegistry(),
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
      )
  val duels = DuelService(sessions, store, battles)
  val trades = TradeService(sessions, store, battles, duels)

  suspend fun seated(name: String, userId: Int, dexId: Int): Pair<FakeSession, Long> {
    val created = store.createCharacter(userId, name, CharacterGender.MALE, Region.HOENN)
    store.addPokemon(created.info.id, starter(created.info.id, dexId))
    val session = FakeSession(created.info.id)
    sessions.bindCharacter(session, created.info.id)
    return session to created.info.id
  }

  fun request(session: FakeSession, name: String) =
      trades.onRequest(PacketEvent(StringCommandPacket(name), session))

  suspend fun action(session: FakeSession, action: Byte) =
      trades.onAction(PacketEvent(TradeActionPacket(action), session))

  fun select(session: FakeSession, slot: Int) =
      trades.onSelect(PacketEvent(TradeSelectMonPacket(slot), session))

  fun partyDex(charId: Long): List<Int> =
      store.getCharacter(charId)!!.pokemon.sortedBy { it.containerSlot }.map { it.dexId }
}

private fun FakeSession.states() = sent.filterIsInstance<TradeStatePacket>().map { it.state }

private fun FakeSession.replies() = sent.filterIsInstance<ChatMessagePacket>().map { it.message }

private const val ACCEPT: Byte = 1
private const val CONFIRM: Byte = 2
private const val CANCEL: Byte = 0

@OptIn(ExperimentalCoroutinesApi::class)
class TradeServiceTest :
    FunSpec({
      test("an accepted offer opens the table for both") {
        runTest {
          val fx = TradeFixture(backgroundScope)
          val (red, _) = fx.seated("Red", 1, 1)
          val (blue, _) = fx.seated("Blue", 2, 4)
          fx.request(red, "Blue")
          val invite = blue.sent.filterIsInstance<DuelInvitePacket>().single()
          invite.requestType shouldBe 1.toByte()
          invite.name shouldBe "Red"
          fx.action(blue, ACCEPT)
          red.states() shouldBe listOf(TradeStatePacket.STATE_OPEN)
          blue.states() shouldBe listOf(TradeStatePacket.STATE_OPEN)
          // The asker's chair is role 0 and each side hears the other's gender and name.
          val redOpen = red.sent.filterIsInstance<TradeStatePacket>().single()
          val blueOpen = blue.sent.filterIsInstance<TradeStatePacket>().single()
          redOpen.role shouldBe 0.toByte()
          blueOpen.role shouldBe 1.toByte()
          redOpen.peerName shouldBe "Blue"
          blueOpen.peerName shouldBe "Red"
        }
      }

      test("scene traffic crosses the table unread") {
        runTest {
          val fx = TradeFixture(backgroundScope)
          val (red, _) = fx.seated("Red", 1, 1)
          val (blue, _) = fx.seated("Blue", 2, 4)
          fx.request(red, "Blue")
          fx.action(blue, ACCEPT)
          val blob = TradeCommPacket(TradeCommPacket.CHANNEL_COMMAND, 22, byteArrayOf(1, 2, 3))
          fx.trades.onComm(PacketEvent(blob, red))
          blue.sent.filterIsInstance<TradeCommPacket>().single() shouldBe blob
          val sync = TradeCommPacket(TradeCommPacket.CHANNEL_SYNC, 19, ByteArray(0))
          fx.trades.onComm(PacketEvent(sync, blue))
          red.sent.filterIsInstance<TradeCommPacket>().single() shouldBe sync
        }
      }

      test("two players asking each other are agreeing, not queueing") {
        runTest {
          val fx = TradeFixture(backgroundScope)
          val (red, _) = fx.seated("Red", 1, 1)
          val (blue, _) = fx.seated("Blue", 2, 4)
          fx.request(red, "Blue")
          fx.request(blue, "Red")
          red.states() shouldBe listOf(TradeStatePacket.STATE_OPEN)
          blue.states() shouldBe listOf(TradeStatePacket.STATE_OPEN)
        }
      }

      test("both confirms swap the pair in place and resend both parties") {
        runTest {
          val fx = TradeFixture(backgroundScope)
          val (red, redId) = fx.seated("Red", 1, 1)
          val (blue, blueId) = fx.seated("Blue", 2, 4)
          fx.request(red, "Blue")
          fx.action(blue, ACCEPT)
          fx.select(red, 0)
          fx.select(blue, 0)
          // Each side saw the other's record, never its own.
          blue.sent.filterIsInstance<TradeListEntryPacket>().single().pokemon.dexId shouldBe 1
          red.sent.filterIsInstance<TradeListEntryPacket>().single().pokemon.dexId shouldBe 4
          fx.action(red, CONFIRM)
          fx.action(blue, CONFIRM)
          fx.partyDex(redId) shouldBe listOf(4)
          fx.partyDex(blueId) shouldBe listOf(1)
          // The received monster is owned by its new holder and seated in the vacated slot.
          val redMon = fx.store.getCharacter(redId)!!.pokemon.single()
          redMon.ownerId shouldBe redId
          redMon.containerSlot shouldBe 0.toShort()
          redMon.ot shouldBe "Ash"
          red.sent.filterIsInstance<PokemonContainerPacket>().shouldNotBeEmpty()
          blue.sent.filterIsInstance<PokemonContainerPacket>().shouldNotBeEmpty()
          red.states().last() shouldBe TradeStatePacket.STATE_COMPLETED
          blue.states().last() shouldBe TradeStatePacket.STATE_COMPLETED
          // The table survives the settlement, the official client's scene walks straight
          // back to it for the next trade, until someone walks away.
          fx.trades.inTrade(redId) shouldBe true
          fx.action(red, CANCEL)
          fx.trades.inTrade(redId) shouldBe false
          fx.trades.inTrade(blueId) shouldBe false
        }
      }

      test("a new selection unmakes both confirmations") {
        runTest {
          val fx = TradeFixture(backgroundScope)
          val (red, redId) = fx.seated("Red", 1, 1)
          val (blue, blueId) = fx.seated("Blue", 2, 4)
          fx.store.addPokemon(blueId, starter(blueId, 7))
          fx.request(red, "Blue")
          fx.action(blue, ACCEPT)
          fx.select(red, 0)
          fx.select(blue, 0)
          fx.action(red, CONFIRM)
          // Blue switches its offer, so Red's standing confirm may not settle the new pair.
          fx.select(blue, 1)
          fx.action(blue, CONFIRM)
          fx.partyDex(redId) shouldBe listOf(1)
          fx.action(red, CONFIRM)
          fx.partyDex(redId) shouldBe listOf(7)
          fx.partyDex(blueId) shouldBe listOf(4, 1)
        }
      }

      test("a decline leaves both parties whole") {
        runTest {
          val fx = TradeFixture(backgroundScope)
          val (red, redId) = fx.seated("Red", 1, 1)
          val (blue, blueId) = fx.seated("Blue", 2, 4)
          fx.request(red, "Blue")
          fx.action(blue, CANCEL)
          red.replies().last() shouldContain "declined"
          fx.trades.inTrade(redId) shouldBe false
          fx.trades.inTrade(blueId) shouldBe false
          fx.partyDex(redId) shouldBe listOf(1)
        }
      }

      test("a cancelled table swaps nothing") {
        runTest {
          val fx = TradeFixture(backgroundScope)
          val (red, redId) = fx.seated("Red", 1, 1)
          val (blue, blueId) = fx.seated("Blue", 2, 4)
          fx.request(red, "Blue")
          fx.action(blue, ACCEPT)
          fx.select(red, 0)
          fx.select(blue, 0)
          fx.action(red, CONFIRM)
          fx.action(blue, CANCEL)
          fx.partyDex(redId) shouldBe listOf(1)
          fx.partyDex(blueId) shouldBe listOf(4)
          red.states().last() shouldBe TradeStatePacket.STATE_CANCELLED
          blue.states().last() shouldBe TradeStatePacket.STATE_CANCELLED
        }
      }

      test("a disconnect tears the table down for the peer") {
        runTest {
          val fx = TradeFixture(backgroundScope)
          val (red, redId) = fx.seated("Red", 1, 1)
          val (blue, blueId) = fx.seated("Blue", 2, 4)
          fx.request(red, "Blue")
          fx.action(blue, ACCEPT)
          fx.trades.onDisconnect(blue)
          red.states().last() shouldBe TradeStatePacket.STATE_CANCELLED
          fx.trades.inTrade(redId) shouldBe false
          fx.trades.inTrade(blueId) shouldBe false
        }
      }

      test("an offered monster that moved out of the party cancels the settlement") {
        runTest {
          val fx = TradeFixture(backgroundScope)
          val (red, redId) = fx.seated("Red", 1, 1)
          val (blue, blueId) = fx.seated("Blue", 2, 4)
          fx.store.addPokemon(blueId, starter(blueId, 7))
          fx.request(red, "Blue")
          fx.action(blue, ACCEPT)
          fx.select(red, 0)
          fx.select(blue, 1)
          fx.action(red, CONFIRM)
          // The offered monster is deposited before the second confirm lands.
          fx.store.rearrangeMonsters(blueId) { party, pc ->
            val moved = party.first { it.dexId == 7 }
            party.filter { it.id != moved.id } to
                pc + moved.copy(container = PokemonContainer.PC, containerSlot = 0)
          }
          fx.action(blue, CONFIRM)
          fx.partyDex(redId) shouldBe listOf(1)
          red.states().last() shouldBe TradeStatePacket.STATE_CANCELLED
          red.replies().last() shouldContain "moved"
        }
      }

      test("a write that fails leaves both parties as they were") {
        runTest {
          val fx = TradeFixture(backgroundScope)
          val (red, redId) = fx.seated("Red", 1, 1)
          val (blue, blueId) = fx.seated("Blue", 2, 4)
          fx.request(red, "Blue")
          fx.action(blue, ACCEPT)
          fx.select(red, 0)
          fx.select(blue, 0)
          fx.action(red, CONFIRM)
          fx.repo.failNextSave = true
          fx.action(blue, CONFIRM)
          fx.partyDex(redId) shouldBe listOf(1)
          fx.partyDex(blueId) shouldBe listOf(4)
          red.states().last() shouldBe TradeStatePacket.STATE_CANCELLED
        }
      }

      test("a player with no monster yet cannot sit at either chair") {
        runTest {
          val fx = TradeFixture(backgroundScope)
          val (red, _) = fx.seated("Red", 1, 1)
          // Blue is at the beginning of the game: seated, but with nothing.
          val blue =
              fx.store.createCharacter(2, "Blue", CharacterGender.FEMALE, Region.HOENN).info.id
          val blueSession = FakeSession(blue)
          fx.sessions.bindCharacter(blueSession, blue)

          fx.request(red, "Blue")
          red.replies().last() shouldContain "no monster to trade"
          blueSession.sent.filterIsInstance<DuelInvitePacket>().shouldBeEmpty()

          fx.trades.onRequest(PacketEvent(StringCommandPacket("Red"), blueSession))
          blueSession.replies().last() shouldContain "no monster to trade"
          fx.trades.inTrade(blue) shouldBe false
        }
      }

      test("a busy player cannot be asked") {
        runTest {
          val fx = TradeFixture(backgroundScope)
          val (red, _) = fx.seated("Red", 1, 1)
          val (blue, _) = fx.seated("Blue", 2, 4)
          val (green, _) = fx.seated("Green", 3, 7)
          fx.request(red, "Blue")
          fx.action(blue, ACCEPT)
          green.sent.clear()
          fx.request(green, "Blue")
          green.replies().last() shouldContain "busy"
          green.states().shouldBeEmpty()
        }
      }
    })
