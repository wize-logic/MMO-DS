package de.fiereu.openmmo.server.game.script

import de.fiereu.openmmo.common.enums.CharacterGender
import de.fiereu.openmmo.common.enums.Region
import de.fiereu.openmmo.maps.MapManager
import de.fiereu.openmmo.server.game.services.DialogService
import de.fiereu.openmmo.server.game.services.NpcService
import de.fiereu.openmmo.server.game.services.ScriptMovementService
import de.fiereu.openmmo.server.game.services.StoryService
import de.fiereu.openmmo.server.game.storage.CharacterStore
import de.fiereu.openmmo.server.game.storage.EntityIdService
import de.fiereu.openmmo.server.game.testsupport.FakeCharacterRepository
import de.fiereu.openmmo.server.game.testsupport.FakeSession
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.shouldBe
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.runTest

/**
 * The Poketch roster is six stock ids in enum order. A session already has those; a story gift of
 * any other id is one flag. Cycle order is that enum, not the overlay-lookup table.
 */
@OptIn(ExperimentalCoroutinesApi::class)
class PoketchAppTest :
    FunSpec({
      test("the enum ordinal is the decomp app id") {
        PoketchApp.DIGITALWATCH.id shouldBe 0
        PoketchApp.CALCULATOR.id shouldBe 1
        PoketchApp.MEMOPAD.id shouldBe 2
        PoketchApp.PEDOMETER.id shouldBe 3
        PoketchApp.PARTYSTATUS.id shouldBe 4
        PoketchApp.FRIENDSHIPCHECKER.id shouldBe 5
        PoketchApp.DOWSINGMACHINE.id shouldBe 6
        PoketchApp.MARKINGMAP.id shouldBe 12
        PoketchApp.ALARMCLOCK.id shouldBe 24
        PoketchApp.entries.size shouldBe 25
        PoketchApp.ROSTER.map { it.id } shouldBe listOf(0, 1, 2, 3, 4, 5)
      }

      test("an empty save already has the roster and nothing else") {
        runTest {
          val ctx = context(this)
          for (app in PoketchApp.ROSTER) {
            ctx.isPoketchAppRegistered(app) shouldBe true
          }
          ctx.isPoketchAppRegistered(PoketchApp.DOWSINGMACHINE) shouldBe false
          ctx.isPoketchAppRegistered(PoketchApp.MARKINGMAP) shouldBe false
          ctx.isPoketchAppRegistered(PoketchApp.LINKSEARCHER) shouldBe false
          ctx.isPoketchAppRegistered(PoketchApp.MOVETESTER) shouldBe false
        }
      }

      test("registering a roster id is a no-op; a stock extra is one flag") {
        runTest {
          val ctx = context(this)
          val store = ctx.characters!!
          val id = ctx.state.characterId!!
          ctx.registerPoketchApp(PoketchApp.DIGITALWATCH)
          val flags = store.getCharacter(id)!!.storyFlags
          flags.contains(PoketchApp.DIGITALWATCH.keyIn("sinnoh")) shouldBe false
          ctx.registerPoketchApp(PoketchApp.DOWSINGMACHINE)
          ctx.isPoketchAppRegistered(PoketchApp.DOWSINGMACHINE) shouldBe true
          store
              .getCharacter(id)!!
              .storyFlags
              .contains(PoketchApp.DOWSINGMACHINE.keyIn("sinnoh")) shouldBe true
        }
      }

      test("the president's 3-badge gift is the marking map, because memo pad is already ours") {
        runTest {
          val ctx = context(this)
          ctx.isPoketchAppRegistered(PoketchApp.MEMOPAD) shouldBe true
          ctx.isPoketchAppRegistered(PoketchApp.MARKINGMAP) shouldBe false
          ctx.giveBadge(Badge.COAL)
          ctx.giveBadge(Badge.FOREST)
          ctx.giveBadge(Badge.COBBLE)
          ctx.countBadgesAcquired() shouldBe 3
          ctx.registerPoketchApp(PoketchApp.MARKINGMAP)
          ctx.isPoketchAppRegistered(PoketchApp.MARKINGMAP) shouldBe true
        }
      }
    })

private suspend fun context(scope: CoroutineScope): ScriptContext {
  val store = CharacterStore(FakeCharacterRepository(), EntityIdService(), scope)
  val charId = store.createCharacter(1, "Lucas", CharacterGender.MALE, Region.SINNOH).info.id
  return contextFor(store, FakeSession(characterId = charId, regionId = 3))
}

private fun contextFor(store: CharacterStore, session: FakeSession): ScriptContext {
  val maps = MapManager()
  return ScriptContext(
      session,
      session.state(),
      entityId = -1,
      DialogService(),
      StoryService(store),
      ScriptMovementService(maps, NpcService(maps, store), store),
      characters = store,
  )
}
