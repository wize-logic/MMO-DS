package de.fiereu.openmmo.server.game.services

import de.fiereu.network.PacketEvent
import de.fiereu.openmmo.common.Pokemon
import de.fiereu.openmmo.common.PokemonMove
import de.fiereu.openmmo.common.enums.CharacterGender
import de.fiereu.openmmo.common.enums.Direction
import de.fiereu.openmmo.common.enums.EVs
import de.fiereu.openmmo.common.enums.EncounterMethod
import de.fiereu.openmmo.common.enums.IVs
import de.fiereu.openmmo.common.enums.PokemonContainer
import de.fiereu.openmmo.common.enums.Region
import de.fiereu.openmmo.items.ItemRegistry
import de.fiereu.openmmo.items.generated.Items
import de.fiereu.openmmo.maps.MapDef
import de.fiereu.openmmo.maps.MapManager
import de.fiereu.openmmo.net.game.packets.UndergroundTalkPacket
import de.fiereu.openmmo.server.game.session.PLAYER_STATE
import de.fiereu.openmmo.server.game.storage.CharacterStore
import de.fiereu.openmmo.server.game.storage.EntityIdService
import de.fiereu.openmmo.server.game.testsupport.FakeCharacterRepository
import de.fiereu.openmmo.server.game.testsupport.FakeSession
import de.fiereu.openmmo.server.game.testsupport.battleService
import de.fiereu.openmmo.server.game.testsupport.safariService
import de.fiereu.openmmo.server.game.world.interest.InterestManager
import io.github.oshai.kotlinlogging.KotlinLogging
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.nulls.shouldNotBeNull
import io.kotest.matchers.shouldBe
import java.time.LocalDateTime
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.runTest

private val log = KotlinLogging.logger {}

/**
 * A rod cast is the same roll as a step into grass, on the other side of a rod: the line lands on
 * the tile the player faces, that tile has to be water and the rod has to be in the bag, and what
 * bit is held until the reel-in lands.
 */
@OptIn(ExperimentalCoroutinesApi::class)
class FishingTest :
    FunSpec({
      /**
       * A Sinnoh map with an Old Rod table and a walkable tile of ITS OWN whose southern neighbour
       * is water.
       */
      fun shore(maps: MapManager): Triple<MapDef, Int, Int>? {
        for (map in maps.all()) {
          if (map.regionId != Region.SINNOH.wireValue) continue
          map.encounterTable(EncounterMethod.OLD_ROD) ?: continue
          val terrain = map.terrain ?: continue
          val header = (map.bankId.toInt() and 0xFF) * 256 + (map.mapId.toInt() and 0xFF)
          for (y in 1 until map.height - 1) for (x in 1 until map.width - 1) {
            if (terrain.headerAt(x, y) != header) continue
            val here = map.tileAt(x, y) ?: continue
            val south = map.tileAt(x, y + 1) ?: continue
            if (!here.behavior.isWater && !here.blocksMovement() && south.behavior.isWater) {
              return Triple(map, x, y)
            }
          }
        }
        return null
      }

      class Fixture(scope: CoroutineScope) {
        val store = CharacterStore(FakeCharacterRepository(), EntityIdService(), scope)
        val maps = MapManager()
        val items = ItemRegistry()
        val battles = battleService(store, InterestManager(), maps)
        val service =
            EncounterService(
                store,
                battles,
                items,
                maps,
                EncounterVariantService(WorldClock()),
                safariService(store, maps))

        suspend fun angler(
            withRod: Boolean,
            map: MapDef,
            x: Int,
            y: Int,
            name: String = "Ash",
        ): Pair<FakeSession, Long> {
          val created = store.createCharacter(1, name, CharacterGender.MALE, Region.SINNOH)
          val id = created.info.id
          store.addPokemon(
              id,
              Pokemon(
                  id = EntityIdService().newMonsterId(),
                  ownerId = id,
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
          if (withRod) store.addItem(id, items.idOf(Items.OLD_ROD), 1)
          val session = FakeSession(id)
          val state = session.attributes[PLAYER_STATE].shouldNotBeNull()
          state.regionId = map.regionId.toInt()
          state.bankId = map.bankId.toInt()
          state.mapId = map.mapId.toInt()
          state.x = x.toShort()
          state.y = y.toShort()
          state.facingDirection = Direction.DOWN
          return session to id
        }

        fun cast(session: FakeSession, rod: Int) =
            service.onFishing(
                PacketEvent(
                    UndergroundTalkPacket(
                        UndergroundTalkPacket.KIND_FISHING_CAST, 0, byteArrayOf(rod.toByte())),
                    session))

        fun hooked(session: FakeSession) =
            service.onFishing(
                PacketEvent(
                    UndergroundTalkPacket(
                        UndergroundTalkPacket.KIND_FISHING_HOOKED, 0, ByteArray(0)),
                    session))

        fun lost(session: FakeSession) =
            service.onFishing(
                PacketEvent(
                    UndergroundTalkPacket(UndergroundTalkPacket.KIND_FISHING_LOST, 0, ByteArray(0)),
                    session))

        fun verdicts(session: FakeSession): List<Int> =
            session.sent.filterIsInstance<UndergroundTalkPacket>().map { it.payload[0].toInt() }
      }

      test("a cast at the water with a rod is answered, and a reported reel-in opens the battle") {
        runTest {
          val fx = Fixture(this)
          val (map, x, y) = shore(fx.maps).shouldNotBeNull()
          log.info { "fishing from ${map.name} at ($x, $y)" }
          val (session, id) = fx.angler(withRod = true, map, x, y)
          // The bite is a roll; cast until the water answers yes, which the rod's rate makes
          // certain long before the cap.
          var bites = 0
          for (i in 0 until 400) {
            fx.cast(session, EncounterService.ROD_OLD)
            if (fx.verdicts(session).last() == 1) {
              bites++
              break
            }
          }
          bites shouldBe 1
          fx.battles.inBattle(id) shouldBe false
          fx.hooked(session)
          fx.battles.inBattle(id) shouldBe true
        }
      }

      test("a fish that got away opens no battle, and neither does a second reel-in") {
        runTest {
          val fx = Fixture(this)
          val (map, x, y) = shore(fx.maps).shouldNotBeNull()
          val (session, id) = fx.angler(withRod = true, map, x, y)
          for (i in 0 until 400) {
            fx.cast(session, EncounterService.ROD_OLD)
            if (fx.verdicts(session).last() == 1) break
          }
          fx.verdicts(session).last() shouldBe 1
          fx.lost(session)
          fx.hooked(session)
          fx.battles.inBattle(id) shouldBe false
        }
      }

      test("no rod in the bag, no bite; dry land, no bite") {
        runTest {
          val fx = Fixture(this)
          val (map, x, y) = shore(fx.maps).shouldNotBeNull()
          val (bare, bareId) = fx.angler(withRod = false, map, x, y)
          for (i in 0 until 50) fx.cast(bare, EncounterService.ROD_OLD)
          fx.verdicts(bare).all { it == 0 } shouldBe true
          fx.hooked(bare)
          fx.battles.inBattle(bareId) shouldBe false

          val (inland, inlandId) = fx.angler(withRod = true, map, x, y, name = "May")
          inland.attributes[PLAYER_STATE].shouldNotBeNull().facingDirection = Direction.UP
          for (i in 0 until 50) fx.cast(inland, EncounterService.ROD_OLD)
          fx.verdicts(inland).all { it == 0 } shouldBe true
          fx.hooked(inland)
          fx.battles.inBattle(inlandId) shouldBe false
        }
      }
    })
