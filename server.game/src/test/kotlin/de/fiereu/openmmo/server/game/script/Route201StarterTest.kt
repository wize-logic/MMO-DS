package de.fiereu.openmmo.server.game.script

import de.fiereu.network.PacketEvent
import de.fiereu.openmmo.common.enums.CharacterGender
import de.fiereu.openmmo.common.enums.Direction
import de.fiereu.openmmo.common.enums.EncounterMethod
import de.fiereu.openmmo.common.enums.Region
import de.fiereu.openmmo.common.enums.TileBehavior
import de.fiereu.openmmo.maps.MapManager
import de.fiereu.openmmo.net.game.packets.MovementPacket
import de.fiereu.openmmo.net.game.packets.dialog.DialogActionResponsePacket
import de.fiereu.openmmo.server.game.services.StoryService
import de.fiereu.openmmo.server.game.session.PENDING_DIALOG
import de.fiereu.openmmo.server.game.session.PENDING_DIALOG_RESPONSE
import de.fiereu.openmmo.server.game.session.SCRIPT_SCOPE
import de.fiereu.openmmo.server.game.storage.CharacterStore
import de.fiereu.openmmo.server.game.storage.EntityIdService
import de.fiereu.openmmo.server.game.testsupport.FakeCharacterRepository
import de.fiereu.openmmo.server.game.testsupport.FakeSession
import de.fiereu.openmmo.server.game.testsupport.movementService
import de.fiereu.openmmo.story.generated.sinnoh.SinnohVars
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.ints.shouldBeGreaterThan
import io.kotest.matchers.shouldBe
import kotlin.time.Duration.Companion.seconds
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.runTest

/**
 * The end of the opening: the first step onto Route 201 runs the scene that puts a Pokemon in
 * the player's party, which is the thing 154 Sinnoh maps of wild tables have been waiting on, 
 * `EncounterService` will not roll grass for an empty party.
 */
@OptIn(ExperimentalCoroutinesApi::class)
class Route201StarterTest :
    FunSpec({
      val route201 = Triple(3, 1, 86)

      fun TestScope.settle() {
        repeat(4) {
          testScheduler.runCurrent()
          testScheduler.advanceTimeBy(2.seconds)
        }
        testScheduler.runCurrent()
      }

      /** Read every box the scene puts up, answering yes and picking the first Pokemon. */
      fun TestScope.readEverything(session: FakeSession, rounds: Int) {
        repeat(rounds) {
          settle()
          session.attributes[PENDING_DIALOG]?.complete(Unit)
          session.attributes[PENDING_DIALOG_RESPONSE]?.complete(
              DialogActionResponsePacket(id = 0, unk = 1))
          session.attributes.remove(PENDING_DIALOG_RESPONSE)
        }
        settle()
      }

      suspend fun newRoute201Player(
          store: CharacterStore,
          scope: kotlinx.coroutines.CoroutineScope,
          x: Int,
      ): Pair<Long, FakeSession> {
        val charId = store.createCharacter(1, "Lucas", CharacterGender.MALE, Region.SINNOH).info.id
        store.updatePosition(charId, x.toShort(), 858, 1, (route201.third and 0xFF).toByte())
        val session =
            FakeSession(
                characterId = charId,
                regionId = route201.first,
                bankId = route201.second,
                mapId = route201.third,
            )
        session.attributes[SCRIPT_SCOPE] = scope
        session.state().x = x.toShort()
        session.state().y = 858
        return charId to session
      }

      /** Getting there at all. */
      test("walking north out of Twinleaf Town hands the player to Route 201") {
        runTest {
          val store = CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
          val charId =
              store.createCharacter(1, "Lucas", CharacterGender.MALE, Region.SINNOH).info.id
          val town = (155 and 0xFF).toByte()
          store.updatePosition(charId, 112, 866, 1, town)
          val session = FakeSession(characterId = charId, regionId = 3, bankId = 1, mapId = 155)
          session.attributes[SCRIPT_SCOPE] = backgroundScope
          session.state().x = 112
          session.state().y = 866
          val movement = movementService(store, MapManager())

          // The town's own guitarist trigger is spent, so the walk north is not interrupted.
          StoryService(store)
              .setVar(charId, SinnohVars.VAR_TWINLEAF_TOWN_GUITARIST_TRIGGER_STATE, 2)

          repeat(3) {
            val at = checkNotNull(store.getCharacter(charId)).info
            movement.onMovement(
                PacketEvent(
                    MovementPacket(at.positionX.toInt(), at.positionY.toInt(), Direction.UP),
                    session))
            settle()
          }

          val info = checkNotNull(store.getCharacter(charId)).info
          info.positionY.toInt() shouldBe 863
          info.positionMapId.toInt() shouldBe 86
          session.state().mapId shouldBe 86
        }
      }

      /**
       * What a party member is for. `EncounterService` refuses to roll grass for an empty
       * party, so until the briefcase handed one over the 154 Sinnoh maps carrying wild tables
       * were dark whatever the player stood on.
       */
      test("Route 201 has tall grass over a land encounter table") {
        val route = checkNotNull(MapManager().getMap(3, 1, 86))
        checkNotNull(route.encounterTable(EncounterMethod.LAND)).slots.size shouldBeGreaterThan 0

        // Route 201 owns the matrix cells the header grid gives it; the grass is inside them.
        val grass =
            (832 until 864).sumOf { y ->
              (96 until 160).count { x ->
                route.terrain?.headerAt(x, y) == 342 &&
                    route.tileAt(x, y)?.behavior == TileBehavior.TALL_GRASS
              }
            }
        grass shouldBeGreaterThan 0
      }
    })
