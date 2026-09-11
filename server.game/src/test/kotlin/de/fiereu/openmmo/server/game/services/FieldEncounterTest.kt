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
import de.fiereu.openmmo.maps.MapDef
import de.fiereu.openmmo.maps.MapManager
import de.fiereu.openmmo.maps.NpcDef
import de.fiereu.openmmo.net.game.packets.UndergroundTalkPacket
import de.fiereu.openmmo.pokemon.SpeciesRegistry
import de.fiereu.openmmo.server.game.session.PLAYER_STATE
import de.fiereu.openmmo.server.game.storage.CharacterStore
import de.fiereu.openmmo.server.game.storage.EntityIdService
import de.fiereu.openmmo.server.game.testsupport.FakeCharacterRepository
import de.fiereu.openmmo.server.game.testsupport.FakeSession
import de.fiereu.openmmo.server.game.testsupport.battleService
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
 * A smashed rock and a headbutted tree are the cartridge's own two rolls with the dice held here.
 */
@OptIn(ExperimentalCoroutinesApi::class)
class FieldEncounterTest :
    FunSpec({
      /** A loaded die: each roll takes the next number, and the last one stays. */
      class Loaded(vararg values: Int) : Dice {
        private val next = ArrayDeque(values.toList())

        override fun roll(bound: Int): Int =
            (if (next.size > 1) next.removeFirst() else next.first()).coerceIn(0, bound - 1)
      }

      /** A ported map with a rock on it, a rock-smash table and something in its rubble. */
      fun quarry(maps: MapManager): Pair<MapDef, NpcDef>? {
        for (map in maps.all()) {
          if (!map.ported || map.rubble == null) continue
          map.encounterTable(EncounterMethod.ROCK_SMASH) ?: continue
          val rock =
              map.npcs.firstOrNull { it.script == FieldEncounterService.ROCK_MARK } ?: continue
          return map to rock
        }
        return null
      }

      /** A ported map whose trees something lives in. */
      fun grove(maps: MapManager): MapDef? =
          maps.all().firstOrNull {
            it.ported &&
                it.headbuttTrees.isNotEmpty() &&
                it.encounterTable(EncounterMethod.HEADBUTT_COMMON) != null
          }

      class Fixture(scope: CoroutineScope) {
        val store = CharacterStore(FakeCharacterRepository(), EntityIdService(), scope)
        val maps = MapManager()
        val items = ItemRegistry()
        val battles = battleService(store, InterestManager(), maps)
        val service =
            FieldEncounterService(
                NpcService(maps, store), maps, store, battles, SpeciesRegistry(), items)

        /** A player with one monster, standing at (x, y) on the map and facing [facing]. */
        suspend fun player(
            map: MapDef,
            x: Int,
            y: Int,
            facing: Direction,
            name: String,
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
                  ot = name,
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
          val session = FakeSession(id)
          val state = session.attributes[PLAYER_STATE].shouldNotBeNull()
          state.regionId = map.regionId.toInt()
          state.bankId = map.bankId.toInt()
          state.mapId = map.mapId.toInt()
          state.x = x.toShort()
          state.y = y.toShort()
          state.facingDirection = facing
          return session to id
        }

        suspend fun smash(session: FakeSession) =
            service.onPacket(
                PacketEvent(
                    UndergroundTalkPacket(UndergroundTalkPacket.KIND_ROCK_SMASH, 0, ByteArray(0)),
                    session))

        suspend fun headbutt(session: FakeSession) =
            service.onPacket(
                PacketEvent(
                    UndergroundTalkPacket(UndergroundTalkPacket.KIND_HEADBUTT, 0, ByteArray(0)),
                    session))

        fun verdicts(session: FakeSession, kind: Int): List<List<Int>> =
            session.sent
                .filterIsInstance<UndergroundTalkPacket>()
                .filter { it.kind == kind }
                .map { p -> p.payload.map { it.toInt() and 0xFF } }
      }

      test("a placed rock meets its table when the die says so, and the rubble when it does not") {
        runTest {
          val fx = Fixture(this)
          val (map, rock) = quarry(fx.maps).shouldNotBeNull()
          val rubble = map.rubble.shouldNotBeNull()
          log.info {
            "smashing the rock at (${rock.x}, ${rock.y}) on ${map.name}, odds ${rubble.odds}"
          }
          val (miner, minerId) = fx.player(map, rock.x, rock.y + 1, Direction.UP, "Bruno")
          fx.service.dice = Loaded(0)
          fx.smash(miner)
          fx.verdicts(miner, UndergroundTalkPacket.KIND_ROCK_SMASH_VERDICT) shouldBe
              listOf(listOf(FieldEncounterService.BATTLE))
          fx.battles.inBattle(minerId) shouldBe true

          val (digger, diggerId) = fx.player(map, rock.x, rock.y + 1, Direction.UP, "Chuck")
          // No encounter, the rubble's odds met, the first item drawn.
          fx.service.dice = Loaded(99, 0, 0)
          fx.smash(digger)
          val id = fx.items.idOf(rubble.items[0])
          fx.verdicts(digger, UndergroundTalkPacket.KIND_ROCK_SMASH_VERDICT) shouldBe
              listOf(listOf(FieldEncounterService.ITEM, id and 0xFF, (id shr 8) and 0xFF))
          fx.battles.inBattle(diggerId) shouldBe false
        }
      }

      test("a rock not faced gives nothing, and so does a rubble that holds nothing") {
        runTest {
          val fx = Fixture(this)
          val (map, rock) = quarry(fx.maps).shouldNotBeNull()
          val (turned, _) = fx.player(map, rock.x, rock.y + 1, Direction.DOWN, "Falkner")
          fx.service.dice = Loaded(0)
          fx.smash(turned)
          fx.verdicts(turned, UndergroundTalkPacket.KIND_ROCK_SMASH_VERDICT) shouldBe
              listOf(listOf(FieldEncounterService.NOTHING))

          val (unlucky, unluckyId) = fx.player(map, rock.x, rock.y + 1, Direction.UP, "Bugsy")
          fx.service.dice = Loaded(99)
          fx.smash(unlucky)
          fx.verdicts(unlucky, UndergroundTalkPacket.KIND_ROCK_SMASH_VERDICT) shouldBe
              listOf(listOf(FieldEncounterService.NOTHING))
          fx.battles.inBattle(unluckyId) shouldBe false
        }
      }

      test(
          "a listed tree starts a battle out of its table; a tile that is no tree starts nothing") {
            runTest {
              val fx = Fixture(this)
              val map = grove(fx.maps).shouldNotBeNull()
              log.info { "headbutting on ${map.name}, ${map.headbuttTrees.size} tree tiles" }
              val listed = map.headbuttTrees.map { it.x to it.y }.toSet()
              val bare =
                  (0 until map.height)
                      .flatMap { y -> (0 until map.width).map { x -> x to y } }
                      .first { it !in listed && (it.first to it.second + 1) !in listed }
              val (idle, idleId) =
                  fx.player(map, bare.first, bare.second + 1, Direction.UP, "Whitney")
              fx.service.dice = Loaded(0)
              fx.headbutt(idle)
              fx.verdicts(idle, UndergroundTalkPacket.KIND_HEADBUTT_VERDICT) shouldBe
                  listOf(listOf(FieldEncounterService.NOTHING))
              fx.battles.inBattle(idleId) shouldBe false

              // Every tree is common, rare or empty for a given trainer; the trees of a map are
              // never
              // all empty, so walking them finds one that answers.
              val (lumberjack, lumberjackId) = fx.player(map, 0, 0, Direction.UP, "Morty")
              val state = lumberjack.attributes[PLAYER_STATE].shouldNotBeNull()
              var battles = 0
              for (tree in map.headbuttTrees) {
                state.x = tree.x.toShort()
                state.y = (tree.y + 1).toShort()
                fx.headbutt(lumberjack)
                if (fx.verdicts(lumberjack, UndergroundTalkPacket.KIND_HEADBUTT_VERDICT).last() ==
                    listOf(FieldEncounterService.BATTLE)) {
                  battles++
                  break
                }
              }
              battles shouldBe 1
              fx.battles.inBattle(lumberjackId) shouldBe true
            }
          }
    })
