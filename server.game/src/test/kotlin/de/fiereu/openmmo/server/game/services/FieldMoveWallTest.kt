package de.fiereu.openmmo.server.game.services

import de.fiereu.network.PacketEvent
import de.fiereu.openmmo.common.Pokemon
import de.fiereu.openmmo.common.PokemonMove
import de.fiereu.openmmo.common.enums.CharacterGender
import de.fiereu.openmmo.common.enums.Direction
import de.fiereu.openmmo.common.enums.EVs
import de.fiereu.openmmo.common.enums.IVs
import de.fiereu.openmmo.common.enums.PokemonContainer
import de.fiereu.openmmo.common.enums.Region
import de.fiereu.openmmo.common.enums.TileBehavior
import de.fiereu.openmmo.maps.MapManager
import de.fiereu.openmmo.net.game.packets.EntityTransportationPacket
import de.fiereu.openmmo.net.game.packets.MovementPacket
import de.fiereu.openmmo.net.game.packets.NpcUpdatePacket
import de.fiereu.openmmo.net.game.packets.TRANSPORTATION_SURFING
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
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.ints.shouldBeGreaterThan
import io.kotest.matchers.shouldBe
import java.time.LocalDateTime
import kotlin.time.Duration.Companion.seconds
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.runTest

/** The HM walls, and the half of them this server can now see. */
@OptIn(ExperimentalCoroutinesApi::class)
class FieldMoveWallTest :
    FunSpec({
      test("a Sinnoh map's water is water, and a field-move tile says so") {
        val maps = MapManager()
        // Twinleaf Town's own map carries the lake south of the player's house.
        val town = checkNotNull(maps.getMap(3, 1, 155))
        val water =
            (860 until 896).sumOf { y ->
              (96 until 128).count { x ->
                town.tileAt(x, y)?.behavior == TileBehavior.SURFABLE_WATER
              }
            }
        water shouldBeGreaterThan 0
      }

      test("only the field-move behaviours need a field move") {
        TileBehavior.entries.filter { it.needsFieldMove } shouldBe
            listOf(
                TileBehavior.SURFABLE_WATER,
                TileBehavior.WATERFALL,
                TileBehavior.ROCK_CLIMB_NORTH_SOUTH,
                TileBehavior.ROCK_CLIMB_EAST_WEST,
            )
        TileBehavior.NORMAL.needsFieldMove shouldBe false
        TileBehavior.TALL_GRASS.needsFieldMove shouldBe false
      }

      /** A rock face is climbed one way or the other, never both. */
      test("a rock face keeps the two ways it is climbed apart, and there are still 247") {
        val maps = MapManager()
        // Sinnoh is 270 terrain planes, and a map is a stretch of one, so count each plane
        // once.
        val seen = java.util.IdentityHashMap<Any, Boolean>()
        var northSouth = 0
        var eastWest = 0
        var waterfall = 0
        for (bank in 0 until 60) for (id in 0 until 256) {
          val map = maps.getMap(3, bank, id) ?: continue
          val plane = map.terrain ?: continue
          if (seen.put(plane, true) != null) continue
          for (y in 0 until plane.height) for (x in 0 until plane.width) {
            when (map.tileAt(x, y)?.behavior) {
              TileBehavior.ROCK_CLIMB_NORTH_SOUTH -> northSouth++
              TileBehavior.ROCK_CLIMB_EAST_WEST -> eastWest++
              TileBehavior.WATERFALL -> waterfall++
              else -> {}
            }
          }
        }
        seen.size shouldBe 272
        northSouth shouldBe 200
        eastWest shouldBe 47
        northSouth + eastWest shouldBe 247
        waterfall shouldBe 55

        TileBehavior.ROCK_CLIMB_NORTH_SOUTH.rockClimbAllows(Direction.UP) shouldBe true
        TileBehavior.ROCK_CLIMB_NORTH_SOUTH.rockClimbAllows(Direction.DOWN) shouldBe true
        TileBehavior.ROCK_CLIMB_NORTH_SOUTH.rockClimbAllows(Direction.LEFT) shouldBe false
        TileBehavior.ROCK_CLIMB_EAST_WEST.rockClimbAllows(Direction.LEFT) shouldBe true
        TileBehavior.ROCK_CLIMB_EAST_WEST.rockClimbAllows(Direction.RIGHT) shouldBe true
        TileBehavior.ROCK_CLIMB_EAST_WEST.rockClimbAllows(Direction.UP) shouldBe false
        TileBehavior.SURFABLE_WATER.rockClimbAllows(Direction.UP) shouldBe false
      }

      /**
       * **Water carries no collision.** Every surfable tile in Sinnoh has the impassable bit clear,
       * because on the cartridge it is the avatar's own state that keeps a walking player off the
       * water, and that state is the client's.
       */
      test("a lake is a wall to a player on foot and a road to one on the water") {
        runTest {
          val store = CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
          val charId =
              store.createCharacter(1, "Lucas", CharacterGender.MALE, Region.SINNOH).info.id
          val maps = MapManager()
          val town = checkNotNull(maps.getMap(3, 1, 155))
          // The southern shore of Twinleaf Town: land, and the lake one tile below it.
          town.tileAt(112, 890)?.behavior shouldBe TileBehavior.NORMAL
          town.tileAt(112, 891)?.behavior shouldBe TileBehavior.SURFABLE_WATER
          town.tileAt(112, 891)?.blocksMovement() shouldBe false

          val movement = movementService(store, maps)
          store.updatePosition(charId, 112, 890, 1, (155 and 0xFF).toByte())
          val session = FakeSession(characterId = charId, regionId = 3, bankId = 1, mapId = 155)
          session.state().x = 112
          session.state().y = 890

          movement.onMovement(PacketEvent(MovementPacket(112, 890, Direction.DOWN), session))
          store.getCharacter(charId)!!.info.positionY.toInt() shouldBe 890

          // The same step, with the mount the server decides on.
          session.state().transportation = TRANSPORTATION_SURFING
          movement.onMovement(PacketEvent(MovementPacket(112, 890, Direction.DOWN), session))
          store.getCharacter(charId)!!.info.positionY.toInt() shouldBe 891
        }
      }

      /** Riding ashore ends the ride, and the wire says so without anyone asking. */
      test("stepping off the water dismounts the player and tells them") {
        runTest {
          val store = CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
          val charId =
              store.createCharacter(1, "Lucas", CharacterGender.MALE, Region.SINNOH).info.id
          val movement = movementService(store, MapManager())
          store.updatePosition(charId, 112, 891, 1, (155 and 0xFF).toByte())
          val session = FakeSession(characterId = charId, regionId = 3, bankId = 1, mapId = 155)
          session.state().x = 112
          session.state().y = 891
          session.state().transportation = TRANSPORTATION_SURFING

          movement.onMovement(PacketEvent(MovementPacket(112, 891, Direction.UP), session))

          store.getCharacter(charId)!!.info.positionY.toInt() shouldBe 890
          session.state().transportation shouldBe 0
          session.sent.filterIsInstance<EntityTransportationPacket>().last().transportation shouldBe
              0
        }
      }

      /**
       * And without the move it is the silence the cartridge answers with: the dispatcher never
       * hands out the script, so there is no box to close and no wire traffic at all.
       */
      test("the same press without Surf says nothing and moves nobody") {
        runTest {
          val store = CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
          val charId =
              store.createCharacter(1, "Lucas", CharacterGender.MALE, Region.SINNOH).info.id
          val maps = MapManager()
          val scripts = ScriptRegistry.generated()
          val interactions =
              InteractionService(
                  NpcService(maps, store),
                  maps,
                  store,
                  scripts,
                  scriptRunner(store, maps, scripts = scripts),
                  trainerSightService(store, maps, scripts = scripts),
              )
          store.updatePosition(charId, 112, 890, 1, (155 and 0xFF).toByte(), Direction.DOWN)
          val session = FakeSession(characterId = charId, regionId = 3, bankId = 1, mapId = 155)
          session.attributes[SCRIPT_SCOPE] = backgroundScope
          session.state().x = 112
          session.state().y = 890
          session.state().facingDirection = Direction.DOWN

          interactions.onTileInteract(PacketEvent(TileInteractPacket(), session))
          repeat(3) {
            testScheduler.runCurrent()
            testScheduler.advanceTimeBy(2.seconds)
          }

          session.sent.filterIsInstance<DialogActionPacket>() shouldBe emptyList()
          session.sent.filterIsInstance<EntityTransportationPacket>() shouldBe emptyList()
          session.state().transportation shouldBe 0
        }
      }

      /**
       * Victory Road is the only way to the League and its 26 boulders sit on walkable floor, so a
       * collision check that only reads the tile lets a player walk through every one of them.
       */
      test("Victory Road carries 26 Strength boulders") {
        val maps = MapManager()
        val room2 = checkNotNull(maps.getMap(3, 0, 247))
        val floor2 = checkNotNull(maps.getMap(3, 0, 245))
        room2.npcs.count { it.script == "10002" } shouldBe 10
        floor2.npcs.count { it.script == "10002" } shouldBe 16
      }

      test("a Strength boulder is a wall until the move is on, and then it slides") {
        runTest {
          val store = CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
          val charId =
              store.createCharacter(1, "Lucas", CharacterGender.MALE, Region.SINNOH).info.id
          val maps = MapManager()
          val cave = checkNotNull(maps.getMap(3, 1, 3))
          // Oreburgh Gate B1F, the boulder at (8, 12). Stand west of it and walk east.
          cave.npcs.any { it.script == "10002" && it.x == 8 && it.y == 12 } shouldBe true
          (cave.tileAt(7, 12)?.blocksMovement() == true) shouldBe false
          (cave.tileAt(9, 12)?.blocksMovement() == true) shouldBe false

          val movement = movementService(store, maps)
          store.updatePosition(charId, 7, 12, 1, 3)
          val session = FakeSession(characterId = charId, regionId = 3, bankId = 1, mapId = 3)
          session.state().x = 7
          session.state().y = 12

          movement.onMovement(PacketEvent(MovementPacket(7, 12, Direction.RIGHT), session))
          store.getCharacter(charId)!!.info.positionX.toInt() shouldBe 7
          session.state().boulderTiles shouldBe emptyMap()

          session.state().strengthActive = true
          movement.onMovement(PacketEvent(MovementPacket(7, 12, Direction.RIGHT), session))
          store.getCharacter(charId)!!.info.positionX.toInt() shouldBe 7
          session.state().boulderTiles[2] shouldBe (9 to 12)
          session.sent.filterIsInstance<NpcUpdatePacket>().last().let {
            it.x shouldBe 9
            it.y shouldBe 12
          }
        }
      }
    })

/** A party member that knows Surf, which is half of what the water asks for. */
private fun surfer(ownerId: Long): Pokemon =
    Pokemon(
        id = EntityIdService().newMonsterId(),
        ownerId = ownerId,
        container = PokemonContainer.PARTY,
        containerSlot = 0,
        dexId = 393,
        seed = 0,
        ot = "Lucas",
        nickname = "",
        level = 30,
        hp = 60,
        xp = 0,
        eVs = EVs(),
        iVs = IVs(),
        moves = listOf(PokemonMove(57, 15)),
        isShiny = false,
        hasHiddenAbility = false,
        isAlpha = false,
        isSecret = false,
        isFatefulEncounter = false,
        isRaidEncounter = false,
        caughtAt = LocalDateTime.now(),
    )

/** A party member that knows Strength, which is half of what a boulder asks for. */
private fun pusher(ownerId: Long): Pokemon =
    Pokemon(
        id = EntityIdService().newMonsterId(),
        ownerId = ownerId,
        container = PokemonContainer.PARTY,
        containerSlot = 0,
        dexId = 395,
        seed = 0,
        ot = "Lucas",
        nickname = "",
        level = 30,
        hp = 60,
        xp = 0,
        eVs = EVs(),
        iVs = IVs(),
        moves = listOf(PokemonMove(70, 15)),
        isShiny = false,
        hasHiddenAbility = false,
        isAlpha = false,
        isSecret = false,
        isFatefulEncounter = false,
        isRaidEncounter = false,
        caughtAt = LocalDateTime.now(),
    )
