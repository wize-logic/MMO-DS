package de.fiereu.openmmo.server.game.services

import de.fiereu.network.PacketEvent
import de.fiereu.openmmo.common.Pokemon
import de.fiereu.openmmo.common.PokemonMove
import de.fiereu.openmmo.common.enums.BattleAction
import de.fiereu.openmmo.common.enums.CharacterGender
import de.fiereu.openmmo.common.enums.EVs
import de.fiereu.openmmo.common.enums.IVs
import de.fiereu.openmmo.common.enums.PokemonContainer
import de.fiereu.openmmo.common.enums.Region
import de.fiereu.openmmo.maps.EncounterVariant
import de.fiereu.openmmo.maps.MapManager
import de.fiereu.openmmo.maps.generated.sinnoh.DailyEncounters
import de.fiereu.openmmo.net.game.packets.ChatMessagePacket
import de.fiereu.openmmo.net.game.packets.battle.BattleActionSelectPacket
import de.fiereu.openmmo.server.game.storage.CharacterStore
import de.fiereu.openmmo.server.game.storage.EntityIdService
import de.fiereu.openmmo.server.game.testsupport.FakeCharacterRepository
import de.fiereu.openmmo.server.game.testsupport.FakeSession
import de.fiereu.openmmo.server.game.testsupport.battleService
import de.fiereu.openmmo.server.game.testsupport.safariService
import de.fiereu.openmmo.server.game.world.interest.InterestManager
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.collections.shouldContain
import io.kotest.matchers.nulls.shouldNotBeNull
import io.kotest.matchers.shouldBe
import java.time.LocalDate
import java.time.LocalDateTime
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.runTest

/**
 * The Great Marsh's Safari game, and the conditional slots a wild table carries for state that is
 * not its own.
 */
@OptIn(ExperimentalCoroutinesApi::class)
class SafariTest :
    FunSpec({
      class Fixture(scope: CoroutineScope) {
        val store = CharacterStore(FakeCharacterRepository(), EntityIdService(), scope)
        val maps = MapManager()
        val service = safariService(store, maps)
        val battles = battleService(store, InterestManager(), maps, service)

        /** Great Marsh area 1, which is Sinnoh header 504. */
        val marsh = maps.getMap(3, 1, 248)

        /** Pastoria City, which is not. */
        val outside = maps.byName("pastoria_city")

        suspend fun walker(withParty: Boolean = false): Pair<FakeSession, Long> {
          val created = store.createCharacter(1, "Ash", CharacterGender.MALE, Region.SINNOH)
          if (withParty) {
            store.addPokemon(
                created.info.id,
                Pokemon(
                    id = EntityIdService().newMonsterId(),
                    ownerId = created.info.id,
                    container = PokemonContainer.PARTY,
                    containerSlot = 0,
                    dexId = 1,
                    seed = 0,
                    ot = "Ash",
                    nickname = "",
                    level = 20,
                    hp = 50,
                    xp = 0,
                    eVs = EVs(),
                    iVs = IVs(),
                    moves = listOf(PokemonMove(33, 35)),
                    isShiny = false,
                    hasHiddenAbility = false,
                    isAlpha = false,
                    isSecret = false,
                    isFatefulEncounter = false,
                    isRaidEncounter = false,
                    caughtAt = LocalDateTime.now(),
                ))
          }
          return FakeSession(created.info.id) to created.info.id
        }

        fun notices(session: FakeSession): List<String> =
            session.sent.filterIsInstance<ChatMessagePacket>().map { it.message }

        fun steps(id: Long) = store.getCharacter(id)?.info?.remainingSafariSteps?.toInt()

        fun balls(id: Long) = store.getCharacter(id)?.info?.remainingSafariBalls?.toInt()
      }

      test("the six marsh areas are a safari and their neighbours are not") {
        runTest {
          val fx = Fixture(this)
          val maps = fx.maps
          val service = fx.service
          for (mapId in 248..253) {
            val map = maps.getMap(3, 1, mapId)
            map.shouldNotBeNull()
            map.name shouldBe "great_marsh_${mapId - 247}"
            service.isGreatMarsh(map) shouldBe true
          }
          // The entrance building the game warps in and out of is not itself the marsh.
          val gate = maps.getMap(3, 0, 125)
          gate.shouldNotBeNull()
          gate.name shouldBe "pastoria_city_observatory_gate_1f"
          service.isGreatMarsh(gate) shouldBe false
        }
      }

      test("walking in hands out the cartridge's allowance, and each step spends one") {
        runTest {
          val fx = Fixture(this)
          val marsh = fx.marsh.shouldNotBeNull()
          val (session, id) = fx.walker()
          fx.steps(id) shouldBe 0

          // The step that arrives starts the game rather than paying for it.
          fx.service.onStep(session, session.state(), marsh) shouldBe false
          fx.steps(id) shouldBe SafariService.STEP_ALLOWANCE
          fx.balls(id) shouldBe SafariService.BALL_ALLOWANCE

          fx.service.onStep(session, session.state(), marsh) shouldBe false
          fx.steps(id) shouldBe SafariService.STEP_ALLOWANCE - 1
          fx.service.playing(id) shouldBe true
        }
      }

      test("the last step ends the game, and no later step starts another one") {
        runTest {
          val fx = Fixture(this)
          val marsh = fx.marsh.shouldNotBeNull()
          val (session, id) = fx.walker()
          fx.service.onStep(session, session.state(), marsh)
          repeat(SafariService.STEP_ALLOWANCE - 1) {
            fx.service.onStep(session, session.state(), marsh) shouldBe false
          }
          fx.steps(id) shouldBe 1

          // The step that spends the last of it meets nothing on its way out.
          fx.service.onStep(session, session.state(), marsh) shouldBe true
          fx.steps(id) shouldBe 0
          fx.balls(id) shouldBe 0
          fx.service.playing(id) shouldBe false

          // A step taken before the warp lands is still inside a game that is over.
          fx.service.onStep(session, session.state(), marsh) shouldBe true
          fx.steps(id) shouldBe 0
        }
      }

      test("an allowance does not leave the marsh") {
        runTest {
          val fx = Fixture(this)
          val marsh = fx.marsh.shouldNotBeNull()
          val outside = fx.outside.shouldNotBeNull()
          val (session, id) = fx.walker()
          fx.service.onStep(session, session.state(), marsh)
          fx.balls(id) shouldBe SafariService.BALL_ALLOWANCE

          fx.service.onStep(session, session.state(), outside) shouldBe false
          fx.steps(id) shouldBe 0
          fx.balls(id) shouldBe 0

          // And walking back in is a new game.
          fx.service.onStep(session, session.state(), marsh) shouldBe false
          fx.steps(id) shouldBe SafariService.STEP_ALLOWANCE
        }
      }

      test("a session that ended in the marsh and came back elsewhere leaves the allowance") {
        runTest {
          val fx = Fixture(this)
          val marsh = fx.marsh.shouldNotBeNull()
          val outside = fx.outside.shouldNotBeNull()
          val (session, id) = fx.walker()
          fx.service.onStep(session, session.state(), marsh)
          fx.steps(id) shouldBe SafariService.STEP_ALLOWANCE

          // The disconnect forgets that this character was ever inside; the record does not.
          fx.service.onLeave(id)
          fx.service.onStep(session, session.state(), outside) shouldBe false
          fx.steps(id) shouldBe 0
          fx.balls(id) shouldBe 0
        }
      }

      test("a ball comes out of the allowance, and runs out") {
        runTest {
          val fx = Fixture(this)
          val marsh = fx.marsh.shouldNotBeNull()
          val (session, id) = fx.walker()
          fx.service.onStep(session, session.state(), marsh)

          repeat(SafariService.BALL_ALLOWANCE) { fx.service.spendBall(id) shouldBe true }
          fx.balls(id) shouldBe 0
          fx.service.spendBall(id) shouldBe false

          // No balls ends the game the next step, the way `Field_UpdateSafari` reads them first.
          fx.service.onStep(session, session.state(), marsh) shouldBe true
          fx.steps(id) shouldBe 0
        }
      }

      test("a Safari battle has no fight in it, and its ball is not the bag's") {
        runTest {
          val fx = Fixture(this)
          val marsh = fx.marsh.shouldNotBeNull()
          val (session, id) = fx.walker(withParty = true)
          fx.service.onStep(session, session.state(), marsh)
          fx.balls(id) shouldBe SafariService.BALL_ALLOWANCE

          // Wooper, which is what slot 0 of great_marsh_1 rolls.
          fx.battles.startWildBattle(session, 194, 28, safari = true)
          fx.battles.inBattle(id) shouldBe true

          // The move is one the lead knows, so only the marsh can be refusing it.
          fx.battles.onBattleAction(
              PacketEvent(BattleActionSelectPacket(0, BattleAction.MOVE, 33, 0L, 0), session))
          fx.notices(session).any { it.contains("no battling", ignoreCase = true) } shouldBe true

          // The bag is empty and the throw still happens, out of the allowance.
          fx.store.getCharacter(id)?.items?.isEmpty() shouldBe true
          fx.battles.onBattleAction(
              PacketEvent(BattleActionSelectPacket(0, BattleAction.ITEM, 5004, 0L, 0), session))
          fx.balls(id) shouldBe SafariService.BALL_ALLOWANCE - 1
          fx.store.getCharacter(id)?.items?.isEmpty() shouldBe true
        }
      }

      test("the swarm's maps are the cartridge's own twenty-two, and each carries a swarm") {
        val maps = MapManager()
        EncounterVariantService.SWARM_MAPS.size shouldBe 22
        for (name in EncounterVariantService.SWARM_MAPS) {
          val map = maps.byName(name)
          map.shouldNotBeNull()
          map.wildEncounters.flatMap { it.overrides }.map { it.variant } shouldContain
              EncounterVariant.SWARM
        }
      }

      test("one swarm a day, the same one for everybody, and yesterday\'s is still there") {
        val service = EncounterVariantService(WorldClock())
        val today = LocalDate.of(2026, 9, 6)
        val chosen = service.swarmMapOn(today)
        EncounterVariantService.SWARM_MAPS shouldContain chosen
        // Same day, same answer: no state, so two sessions and a restart all agree.
        service.swarmMapOn(today) shouldBe chosen
        // And it is a roll rather than a walk down the table: a month lands on several maps, and
        // consecutive days are not consecutive entries.
        val month = (0L until 30L).map { service.swarmMapOn(today.plusDays(it)) }
        (month.toSet().size > 1) shouldBe true
      }

      test("a marsh area shows one species of the day, in both of the slots that rotate") {
        val service = EncounterVariantService(WorldClock())
        val maps = MapManager()
        val today = LocalDate.of(2026, 9, 6)

        // Header 504 is the first marsh area, and the six run to 509.
        val first = maps.getMap(3, 1, 248)
        first.shouldNotBeNull()
        val pair = service.dailySpeciesOn(first, today, nationalDex = true)
        // ReplaceGreatMarshDailyEncounters writes what it found into both slots.
        pair.size shouldBe 2
        pair[0] shouldBe pair[1]
        DailyEncounters.MARSH_NATIONAL_DEX shouldContain pair[0]
        // Same day, same answer, and the dex decides which of the two lists is read.
        service.dailySpeciesOn(first, today, nationalDex = true) shouldBe pair
        DailyEncounters.MARSH_LOCAL shouldContain
            service.dailySpeciesOn(first, today, nationalDex = false)[0]

        // The areas cut their index out of the same roll at five bits apiece, so on a given day
        // they do not all show the same monster.
        val areas = (0 until 6).map { service.marshPairOn(today, it, true)[0] }
        (areas.toSet().size > 1) shouldBe true
        // And it turns over: a month of one area is not one species.
        val month = (0L until 30L).map { service.marshPairOn(today.plusDays(it), 0, true)[0] }
        (month.toSet().size > 1) shouldBe true
      }

      test("the Trophy Garden shows two of the sixteen, and only with the national dex") {
        val service = EncounterVariantService(WorldClock())
        val maps = MapManager()
        val today = LocalDate.of(2026, 9, 6)

        // Header 287 is the Trophy Garden: bank 1, map 31.
        val garden = maps.getMap(3, 1, 31)
        garden.shouldNotBeNull()
        val pair = service.dailySpeciesOn(garden, today, nationalDex = true)
        pair.size shouldBe 2
        pair.forEach { DailyEncounters.TROPHY_GARDEN_MONS shouldContain it }
        // TrophyGarden_AddNewMon rerolls until the new one is in neither slot, so the two differ.
        (pair[0] == pair[1]) shouldBe false
        // WildEncounters_ReplaceTrophyGardenEncounters does nothing at all without the dex.
        service.dailySpeciesOn(garden, today, nationalDex = false) shouldBe emptyList()
        // A month of them is two distinct every day, and not the same two every day.
        val month = (0L until 30L).map { service.trophyGardenPairOn(today.plusDays(it)) }
        month.forEach { (it[0] == it[1]) shouldBe false }
        (month.toSet().size > 1) shouldBe true
      }

      test("every other map rotates nothing, which is all of them but seven") {
        val service = EncounterVariantService(WorldClock())
        val maps = MapManager()
        val today = LocalDate.of(2026, 9, 6)

        val route201 = maps.getMap(3, 1, 86)
        route201.shouldNotBeNull()
        service.dailySpeciesOn(route201, today, nationalDex = true) shouldBe emptyList()
        // A ported map travels on the same region id with a header of its own, and rotates
        // nothing either: 594 is the first of them.
        val ported = maps.getMap(3, 2, 82)
        ported.shouldNotBeNull()
        service.dailySpeciesOn(ported, today, nationalDex = true) shouldBe emptyList()
      }

      test("the second cartridge slot is named or empty, never guessed") {
        val service = EncounterVariantService(WorldClock())
        service.readSlot(null) shouldBe null
        service.readSlot("") shouldBe null
        service.readSlot("firered") shouldBe EncounterVariant.DUAL_SLOT_FIRERED
        service.readSlot("LeafGreen") shouldBe EncounterVariant.DUAL_SLOT_LEAFGREEN
        // A name nothing plugged in is an empty slot and a line in the log, not a cartridge.
        service.readSlot("crystal") shouldBe null
      }
    })
