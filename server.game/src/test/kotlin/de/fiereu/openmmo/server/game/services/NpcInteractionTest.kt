package de.fiereu.openmmo.server.game.services

import de.fiereu.network.PacketEvent
import de.fiereu.openmmo.common.enums.CharacterGender
import de.fiereu.openmmo.common.enums.Direction
import de.fiereu.openmmo.common.enums.Region
import de.fiereu.openmmo.maps.MapManager
import de.fiereu.openmmo.net.game.packets.TileInteractPacket
import de.fiereu.openmmo.net.game.packets.dialog.DialogActionPacket
import de.fiereu.openmmo.server.game.script.ScriptRegistry
import de.fiereu.openmmo.server.game.session.SCRIPT_SCOPE
import de.fiereu.openmmo.server.game.storage.CharacterStore
import de.fiereu.openmmo.server.game.storage.EntityIdService
import de.fiereu.openmmo.server.game.testsupport.FakeCharacterRepository
import de.fiereu.openmmo.server.game.testsupport.FakeSession
import de.fiereu.openmmo.server.game.testsupport.scriptRunner
import de.fiereu.openmmo.server.game.testsupport.trainerSightService
import de.fiereu.openmmo.story.generated.sinnoh.SinnohFlags
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.shouldBe
import kotlin.time.Duration.Companion.seconds
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.runTest

/** Pressing A while facing a person. */
@OptIn(ExperimentalCoroutinesApi::class)
class NpcInteractionTest :
    FunSpec({
      fun TestScope.settle() {
        repeat(3) {
          testScheduler.runCurrent()
          testScheduler.advanceTimeBy(2.seconds)
        }
        testScheduler.runCurrent()
      }

      fun interactions(store: CharacterStore, scope: CoroutineScope): InteractionService {
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

      /**
       * The story's own cast stays silent while it is hidden. The rival's hide flag is one of the
       * 112 a new Sinnoh character carries, and his tile is his own front door.
       */
      test("a hidden npc does not answer the tile it stands on") {
        runTest {
          val store = CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
          val charId =
              store.createCharacter(1, "Lucas", CharacterGender.MALE, Region.SINNOH).info.id
          StoryService(store).isFlagSet(charId, SinnohFlags.FLAG_HIDE_TWINLEAF_TOWN_RIVAL) shouldBe
              true
          // The rival's object sits at (105, 875); this is the tile below it.
          store.updatePosition(charId, 105, 876, 1, (155 and 0xFF).toByte(), Direction.UP)
          val session = FakeSession(characterId = charId, regionId = 3, bankId = 1, mapId = 155)
          session.attributes[SCRIPT_SCOPE] = backgroundScope
          session.state().x = 105
          session.state().y = 876
          session.state().facingDirection = Direction.UP

          interactions(store, backgroundScope)
              .onTileInteract(PacketEvent(TileInteractPacket(), session))
          settle()

          session.sent.filterIsInstance<DialogActionPacket>() shouldBe emptyList()
        }
      }

      /** And a sign still answers, because a person answering first must not shadow one. */
    })
