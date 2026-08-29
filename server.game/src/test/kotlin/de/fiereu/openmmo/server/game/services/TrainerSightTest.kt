package de.fiereu.openmmo.server.game.services

import de.fiereu.openmmo.common.enums.CharacterGender
import de.fiereu.openmmo.common.enums.Region
import de.fiereu.openmmo.maps.MapManager
import de.fiereu.openmmo.server.game.session.SCRIPT_SCOPE
import de.fiereu.openmmo.server.game.storage.CharacterStore
import de.fiereu.openmmo.server.game.storage.EntityIdService
import de.fiereu.openmmo.server.game.testsupport.FakeCharacterRepository
import de.fiereu.openmmo.server.game.testsupport.FakeSession
import de.fiereu.openmmo.server.game.testsupport.trainerSightService
import de.fiereu.openmmo.trainer.TrainerRegistry
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.ints.shouldBeGreaterThan
import io.kotest.matchers.shouldBe
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.runTest

/** Route trainers, who could not see anyone. */
@OptIn(ExperimentalCoroutinesApi::class)
class TrainerSightTest :
    FunSpec({
      val route203 = Triple(3, 1, 88)
      val sebastian = 356

      fun seated(store: CharacterStore, charId: Long, x: Int, y: Int): FakeSession {
        store.updatePosition(
            charId, x.toShort(), y.toShort(), 1, (route203.third and 0xFF).toByte())
        val session =
            FakeSession(
                characterId = charId,
                regionId = route203.first,
                bankId = route203.second,
                mapId = route203.third,
            )
        session.state().x = x.toShort()
        session.state().y = y.toShort()
        return session
      }

      /**
       * The table itself. Platinum's trainers are one JSON file each rather than two C tables, and
       * `res/trainers/data` holds one per line of `generated/trainers.txt`, `none.json` being the
       * enum's own empty slot.
       */
      test("the Platinum trainer table is loaded and its parties are the decomp's") {
        val trainers = TrainerRegistry()
        trainers.size() shouldBeGreaterThan 2000

        val cynthia = checkNotNull(trainers.get(Region.SINNOH, 267))
        cynthia.name shouldBe "Cynthia"
        cynthia.party.size shouldBe 6
        // Spiritomb, Roserade, Togekiss, Lucario, Milotic, Garchomp.
        cynthia.party.map { it.dexId } shouldBe listOf(442, 407, 468, 448, 350, 445)
        cynthia.party.last().level shouldBe 62

        // The rival's three Route 201 parties are named after the starter the *player* took, and
        // each holds the one that beats it.
        trainers.get(Region.SINNOH, 850)?.party?.single()?.dexId shouldBe 387
        trainers.get(Region.SINNOH, 851)?.party?.single()?.dexId shouldBe 390
        trainers.get(Region.SINNOH, 852)?.party?.single()?.dexId shouldBe 393

        // Beaten state needs no schema: every one of the 927 carries the flag that says so.
        checkNotNull(trainers.get(Region.SINNOH, sebastian)).defeatedFlag shouldBe
            "sinnoh/FLAG_DEFEATED_TRAINER_YOUNGSTER_SEBASTIAN"
      }

      /** And the map data says which trainer he is, and how far he can see. */
      test("a Sinnoh trainer npc carries its trainer id and its sight range") {
        val route =
            checkNotNull(MapManager().getMap(route203.first, route203.second, route203.third))
        val npc = checkNotNull(route.npcs.firstOrNull { it.trainerId == sebastian })
        npc.sightRange shouldBe 5
        (npc.x to npc.y) shouldBe (234 to 757)

        // 417 of the region's npcs are a trainer the table owns; the seven objects the data marks
        // TRAINER_TYPE_UNK_003 are ground items and carry no id.
        route.npcs.count { it.trainerId != 0 } shouldBeGreaterThan 0
      }

      test("walking into a trainer's line of sight starts the challenge") {
        runTest {
          val store = CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
          val charId =
              store.createCharacter(1, "Lucas", CharacterGender.MALE, Region.SINNOH).info.id
          val maps = MapManager()
          val sight = trainerSightService(store, maps)
          val route = checkNotNull(maps.getMap(route203.first, route203.second, route203.third))

          // Two tiles west of him, on the row he is looking down.
          val session = seated(store, charId, 232, 757)
          session.attributes[SCRIPT_SCOPE] = backgroundScope
          sight.onStep(session, session.state(), route, 232, 757) shouldBe true

          // Off his row, and out past his range, nobody is challenged.
          val offRow = seated(store, charId, 232, 750)
          offRow.attributes[SCRIPT_SCOPE] = backgroundScope
          sight.onStep(offRow, offRow.state(), route, 232, 750) shouldBe false

          val tooFar = seated(store, charId, 228, 757)
          tooFar.attributes[SCRIPT_SCOPE] = backgroundScope
          sight.onStep(tooFar, tooFar.state(), route, 228, 757) shouldBe false

          // And behind him: a cone is a direction, not a radius.
          val behind = seated(store, charId, 236, 757)
          behind.attributes[SCRIPT_SCOPE] = backgroundScope
          sight.onStep(behind, behind.state(), route, 236, 757) shouldBe false
        }
      }

      test("a trainer already beaten does not look up again") {
        runTest {
          val store = CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
          val charId =
              store.createCharacter(1, "Lucas", CharacterGender.MALE, Region.SINNOH).info.id
          val maps = MapManager()
          val route = checkNotNull(maps.getMap(route203.first, route203.second, route203.third))
          StoryService(store).setFlag(charId, "sinnoh/FLAG_DEFEATED_TRAINER_YOUNGSTER_SEBASTIAN")

          val session = seated(store, charId, 232, 757)
          session.attributes[SCRIPT_SCOPE] = backgroundScope
          trainerSightService(store, maps)
              .onStep(session, session.state(), route, 232, 757) shouldBe false
        }
      }
    })
