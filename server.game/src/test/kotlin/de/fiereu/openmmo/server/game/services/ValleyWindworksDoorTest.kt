package de.fiereu.openmmo.server.game.services

import de.fiereu.network.PacketEvent
import de.fiereu.openmmo.common.enums.CharacterGender
import de.fiereu.openmmo.common.enums.Direction
import de.fiereu.openmmo.common.enums.Region
import de.fiereu.openmmo.common.enums.TileBehavior
import de.fiereu.openmmo.maps.MapManager
import de.fiereu.openmmo.net.game.packets.MapTransitionPacket
import de.fiereu.openmmo.net.game.packets.MovementPacket
import de.fiereu.openmmo.net.game.packets.TileInteractPacket
import de.fiereu.openmmo.net.game.packets.dialog.DialogActionPacket
import de.fiereu.openmmo.server.game.script.ScriptRegistry
import de.fiereu.openmmo.server.game.session.SCRIPT_SCOPE
import de.fiereu.openmmo.server.game.storage.CharacterStore
import de.fiereu.openmmo.server.game.storage.EntityIdService
import de.fiereu.openmmo.server.game.testsupport.FakeCharacterRepository
import de.fiereu.openmmo.server.game.testsupport.FakeSession
import de.fiereu.openmmo.server.game.testsupport.movementService
import de.fiereu.openmmo.server.game.testsupport.scriptRunner
import de.fiereu.openmmo.server.game.testsupport.trainerSightService
import de.fiereu.openmmo.story.generated.sinnoh.SinnohFlags
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.shouldBe
import io.kotest.matchers.shouldNotBe
import kotlin.time.Duration.Companion.seconds
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.runTest

/**
 * Valley Windworks' door is a warp and a sign on the same tile. The cartridge moves one of them off
 * the map so the tile answers to the other; this is the claim that the overlay does the same.
 */
@OptIn(ExperimentalCoroutinesApi::class)
class ValleyWindworksDoorTest :
    FunSpec({
      val maps = MapManager()
      val outside = checkNotNull(maps.getMap(SINNOH, BANK, MAP))

      test("the baked events share the door tile, and it is a door") {
        outside.warps.single().let {
          it.x shouldBe DOOR_X
          it.y shouldBe DOOR_Y
        }
        outside.bgEvents[DOOR_SIGN].let {
          it.x shouldBe DOOR_X
          it.y shouldBe DOOR_Y
          it.script shouldBe "4"
        }
        outside.tileAt(DOOR_X, DOOR_Y)!!.behavior shouldBe TileBehavior.DOOR
      }

      test("without the unlock flag the warp is off the door and the sign stays") {
        val locked = emptySet<String>()
        MapEventLayout.warpAt(outside, locked, DOOR_X, DOOR_Y) shouldBe null
        MapEventLayout.warpAt(outside, locked, PARK_X, PARK_Y) shouldNotBe null
        MapEventLayout.bgEvents(outside, locked).count { it.x == DOOR_X && it.y == DOOR_Y } shouldBe
            1
      }

      test("with the unlock flag the sign is off the door and the warp stays") {
        val unlocked = setOf(SinnohFlags.FLAG_UNLOCKED_VALLEY_WINDWORKS_DOOR)
        MapEventLayout.warpAt(outside, unlocked, DOOR_X, DOOR_Y) shouldNotBe null
        MapEventLayout.bgEvents(outside, unlocked).none {
          it.x == DOOR_X && it.y == DOOR_Y
        } shouldBe true
        MapEventLayout.bgEvents(outside, unlocked).count {
          it.x == PARK_X && it.y == PARK_Y
        } shouldBe 1
      }

      test("walking into the locked door stays outside") {
        runTest {
          val store = CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
          val charId = placeAtDoor(store)
          val session = doorSession(charId)
          movementService(store, maps)
              .onMovement(PacketEvent(MovementPacket(DOOR_X, SOUTH_Y, Direction.UP), session))

          session.sent.filterIsInstance<MapTransitionPacket>() shouldBe emptyList()
          store.getCharacter(charId)!!.info.positionMapId shouldBe MAP.toByte()
          store.getCharacter(charId)!!.info.positionY.toInt() shouldBe SOUTH_Y
        }
      }

      test("walking into the unlocked door enters the works") {
        runTest {
          val store = CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
          val charId = placeAtDoor(store)
          StoryService(store).setFlag(charId, SinnohFlags.FLAG_UNLOCKED_VALLEY_WINDWORKS_DOOR)
          val session = doorSession(charId)
          movementService(store, maps)
              .onMovement(PacketEvent(MovementPacket(DOOR_X, SOUTH_Y, Direction.UP), session))

          session.sent.filterIsInstance<MapTransitionPacket>().size shouldBe 1
          store.getCharacter(charId)!!.info.positionMapId shouldBe BUILDING.toByte()
        }
      }

      test("pressing A on the unlocked door finds no sign") {
        runTest {
          val store = CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
          val charId = placeAtDoor(store)
          StoryService(store).setFlag(charId, SinnohFlags.FLAG_UNLOCKED_VALLEY_WINDWORKS_DOOR)
          val session = doorSession(charId)
          session.attributes[SCRIPT_SCOPE] = backgroundScope
          interactions(store).onTileInteract(PacketEvent(TileInteractPacket(), session))
          settle()

          session.sent.filterIsInstance<DialogActionPacket>() shouldBe emptyList()
        }
      }
    })

private const val SINNOH = 3
private const val BANK = 0
private const val MAP = 200
private const val BUILDING = 201
private const val DOOR_X = 243
private const val DOOR_Y = 654
private const val SOUTH_Y = 655
private const val PARK_X = 243
private const val PARK_Y = 650
private const val DOOR_SIGN = 1

private suspend fun placeAtDoor(store: CharacterStore): Long {
  val charId = store.createCharacter(1, "Lucas", CharacterGender.MALE, Region.SINNOH).info.id
  store.updatePosition(charId, DOOR_X.toShort(), SOUTH_Y.toShort(), BANK.toByte(), MAP.toByte())
  return charId
}

private fun doorSession(charId: Long): FakeSession {
  val session = FakeSession(characterId = charId, regionId = SINNOH, bankId = BANK, mapId = MAP)
  session.state().x = DOOR_X.toShort()
  session.state().y = SOUTH_Y.toShort()
  session.state().facingDirection = Direction.UP
  return session
}

private fun interactions(store: CharacterStore): InteractionService {
  val maps = MapManager()
  val scripts = ScriptRegistry.generated()
  return InteractionService(
      NpcService(maps, store),
      maps,
      store,
      scripts,
      scriptRunner(store, maps, scripts = scripts),
      trainerSightService(store, maps, scripts = scripts),
      ViolationLog(),
  )
}

@OptIn(ExperimentalCoroutinesApi::class)
private fun TestScope.settle() {
  repeat(3) {
    testScheduler.runCurrent()
    testScheduler.advanceTimeBy(2.seconds)
  }
  testScheduler.runCurrent()
}
