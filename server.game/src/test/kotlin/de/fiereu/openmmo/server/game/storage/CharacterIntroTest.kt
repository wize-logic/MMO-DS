package de.fiereu.openmmo.server.game.storage

import de.fiereu.openmmo.common.enums.CharacterGender
import de.fiereu.openmmo.common.enums.Region
import de.fiereu.openmmo.maps.MapManager
import de.fiereu.openmmo.server.game.testsupport.FakeCharacterRepository
import de.fiereu.openmmo.story.generated.hoenn.HoennFlags
import de.fiereu.openmmo.story.generated.hoenn.HoennVars
import de.fiereu.openmmo.story.generated.kanto.KantoFlags
import de.fiereu.openmmo.story.generated.sinnoh.SinnohFlags
import de.fiereu.openmmo.story.generated.sinnoh.SinnohVars
import io.kotest.assertions.throwables.shouldThrow
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.collections.shouldBeEmpty
import io.kotest.matchers.collections.shouldContain
import io.kotest.matchers.maps.shouldBeEmpty
import io.kotest.matchers.nulls.shouldNotBeNull
import io.kotest.matchers.shouldBe
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.runTest

@OptIn(ExperimentalCoroutinesApi::class)
class CharacterIntroTest :
    FunSpec({
      test("male characters start on Brendan's side of the moving truck intro") {
        runTest {
          val store = CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)

          val character = store.createCharacter(1, "Brendan", CharacterGender.MALE, Region.HOENN)

          character.info.rivalSex shouldBe 0
          character.info.dynamicWarp!!.x shouldBe 3
          character.storyVars[HoennVars.VAR_LITTLEROOT_INTRO_STATE] shouldBe 1
          character.storyVars[HoennVars.VAR_LITTLEROOT_HOUSES_STATE_BRENDAN] shouldBe 1
          (HoennFlags.FLAG_HIDE_LITTLEROOT_TOWN_MAYS_HOUSE_TRUCK in character.storyFlags) shouldBe
              true
          (HoennFlags.FLAG_HIDE_LITTLEROOT_TOWN_BRENDANS_HOUSE_TRUCK in
              character.storyFlags) shouldBe false
        }
      }

      test("female characters start on May's side of the moving truck intro") {
        runTest {
          val store = CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)

          val character = store.createCharacter(1, "May", CharacterGender.FEMALE, Region.HOENN)

          character.info.rivalSex shouldBe 1
          character.info.dynamicWarp!!.x shouldBe 12
          character.storyVars[HoennVars.VAR_LITTLEROOT_INTRO_STATE] shouldBe 2
          character.storyVars[HoennVars.VAR_LITTLEROOT_HOUSES_STATE_MAY] shouldBe 1
          (HoennFlags.FLAG_HIDE_LITTLEROOT_TOWN_BRENDANS_HOUSE_TRUCK in
              character.storyFlags) shouldBe true
          (HoennFlags.FLAG_HIDE_LITTLEROOT_TOWN_MAYS_HOUSE_TRUCK in character.storyFlags) shouldBe
              false
        }
      }

      test("Kanto characters start in the Pallet bedroom with FireRed's new game flags") {
        runTest {
          val store = CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)

          val character =
              store.createCharacter(
                  userId = 1,
                  name = "Leaf",
                  gender = CharacterGender.FEMALE,
                  startingRegion = Region.KANTO,
              )

          character.info.positionRegionId shouldBe 0
          character.info.positionBankId shouldBe 4
          character.info.positionMapId shouldBe 1
          character.info.positionX shouldBe 6
          character.info.positionY shouldBe 6
          // Kanto has no truck ride, so nothing depends on the player's dynamic warp yet.
          character.info.dynamicWarp shouldBe null
          character.storyVars shouldBe emptyMap()
          // Oak waits in the grass rather than in his lab or in town.
          (KantoFlags.FLAG_HIDE_OAK_IN_HIS_LAB in character.storyFlags) shouldBe true
          (KantoFlags.FLAG_HIDE_OAK_IN_PALLET_TOWN in character.storyFlags) shouldBe true
          // The rival and the three starter balls are there from the start.
          (KantoFlags.FLAG_HIDE_RIVAL_IN_LAB in character.storyFlags) shouldBe false
          (KantoFlags.FLAG_HIDE_BULBASAUR_BALL in character.storyFlags) shouldBe false
        }
      }

      test("unsupported region wire values remain locked") { Region.byWireValue(2) shouldBe null }

      test("Johto is a trainer namespace, not a world anybody starts in") {
        // It exists so the ported cartridge's 737 trainers have one region to be keyed under. A
        // ported map itself travels as Sinnoh, because it is a Platinum header.
        Region.JOHTO.creatable shouldBe false
        shouldThrow<IllegalStateException> { NewGameStarts.forRegion(Region.JOHTO, female = false) }
      }

      test("every region starts with money, no party and no bag") {
        runTest {
          val store = CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)

          Region.entries
              .filter { it != Region.JOHTO }
              .forEach { region ->
                val character = store.createCharacter(1, region.name, CharacterGender.MALE, region)
                val start = NewGameStarts.forRegion(region, female = false)

                character.info.money shouldBe start.money
                character.info.permissions shouldBe start.permissions
                character.pokemon.shouldBeEmpty()
                character.pcStorage.shouldBeEmpty()
                character.items.shouldBeEmpty()
              }
        }
      }

      test("Sinnoh characters start empty-handed in the Twinleaf bedroom") {
        runTest {
          val store = CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)

          val character =
              store.createCharacter(
                  userId = 1,
                  name = "Lucas",
                  gender = CharacterGender.MALE,
                  startingRegion = Region.SINNOH,
              )

          character.info.positionRegionId shouldBe 3
          character.info.positionBankId shouldBe 1
          character.info.positionMapId shouldBe (415 and 0xFF).toByte()
          character.info.positionX shouldBe 4
          character.info.positionY shouldBe 6
          character.info.dynamicWarp shouldBe null
          val heal = character.info.lastHealLocation.shouldNotBeNull()
          heal.regionId shouldBe 3
          heal.bankId shouldBe 1
          heal.mapId shouldBe (411 and 0xFF).toByte()
          heal.x shouldBe 116
          heal.y shouldBe 886
          character.info.money shouldBe 30000
          character.info.permissions shouldBe 8
          character.pokemon.shouldBeEmpty()
          character.pcStorage.shouldBeEmpty()
          character.items.shouldBeEmpty()
        }
      }

      /** Empty-handed is not the same as knowing nothing. */
      test("a new Sinnoh character carries what the opening script already set") {
        runTest {
          val store = CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)

          val character = store.createCharacter(1, "Lucas", CharacterGender.MALE, Region.SINNOH)

          character.storyFlags.size shouldBe 112
          character.storyFlags.count { it.startsWith("sinnoh/FLAG_HIDE_") } shouldBe 108
          character.storyFlags shouldContain
              SinnohFlags.FLAG_HIDE_TWINLEAF_TOWN_PLAYER_HOUSE_2F_RIVAL
          character.storyVars shouldBe
              mapOf(
                  SinnohVars.VAR_TWINLEAF_TOWN_GUITARIST_TRIGGER_STATE to 1,
                  SinnohVars.VAR_OREBURGH_GATE_1F_HIKER_STATE to 1,
                  SinnohVars.VAR_CONSECUTIVE_BONUS_ROUND_WINS to 9,
              )

          // The map data names the same flag on the object it hides, so the two spellings have to
          // be one spelling.
          val bedroom = MapManager().getMap(3, 1, 159)!!
          bedroom.npcs.map { it.hideFlag } shouldContain
              SinnohFlags.FLAG_HIDE_TWINLEAF_TOWN_PLAYER_HOUSE_2F_RIVAL
        }
      }
    })
