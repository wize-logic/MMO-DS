package de.fiereu.openmmo.server.game.services

import de.fiereu.network.PacketEvent
import de.fiereu.openmmo.common.Pokemon
import de.fiereu.openmmo.common.enums.CharacterGender
import de.fiereu.openmmo.common.enums.Direction
import de.fiereu.openmmo.common.enums.EVs
import de.fiereu.openmmo.common.enums.IVs
import de.fiereu.openmmo.common.enums.PokemonContainer
import de.fiereu.openmmo.common.enums.Region
import de.fiereu.openmmo.common.test.encodeToBytes
import de.fiereu.openmmo.maps.MapManager
import de.fiereu.openmmo.net.game.packets.EntityMovePacket
import de.fiereu.openmmo.net.game.packets.EntityMovePacketCodec
import de.fiereu.openmmo.net.game.packets.GbaEntityMovePacket
import de.fiereu.openmmo.net.game.packets.GbaEntityMovePacketCodec
import de.fiereu.openmmo.net.game.packets.LoadEntityPacketCodec
import de.fiereu.openmmo.net.game.packets.MovementPacket
import de.fiereu.openmmo.pokemon.SpeciesRegistry
import de.fiereu.openmmo.server.game.script.Script
import de.fiereu.openmmo.server.game.script.ScriptRegistry
import de.fiereu.openmmo.server.game.session.SCRIPT_SCOPE
import de.fiereu.openmmo.server.game.storage.CharacterStore
import de.fiereu.openmmo.server.game.storage.EntityIdService
import de.fiereu.openmmo.server.game.testsupport.FakeCharacterRepository
import de.fiereu.openmmo.server.game.testsupport.FakeSession
import de.fiereu.openmmo.server.game.testsupport.movementService
import de.fiereu.openmmo.server.game.world.interest.InterestManager
import de.fiereu.openmmo.server.game.world.interest.PassThroughInterestPolicy
import io.kotest.assertions.throwables.shouldThrow
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.ints.shouldBeGreaterThan
import io.kotest.matchers.shouldBe
import java.time.LocalDateTime
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.runTest

/** A new Sinnoh character starts in the Twinleaf bedroom (header 415 at (4, 6)). */
@OptIn(ExperimentalCoroutinesApi::class)
class SinnohWalkTest :
    FunSpec({
      test("LoadEntity for the Twinleaf bedroom encodes") {
        runTest {
          val store = CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
          val created = store.createCharacter(1, "Lucas", CharacterGender.MALE, Region.SINNOH).info

          val packet = MapLoadService(MapManager(), SpeciesRegistry()).createLoadEntity(created)
          packet.mapId shouldBe 159
          packet.x shouldBe 4
          packet.y shouldBe 6

          val bytes = LoadEntityPacketCodec.encodeToBytes(packet)
          bytes.size shouldBeGreaterThan (0)
        }
      }

      // Which party member walks behind the trainer, and it is HeartGold's rule
      // rather than "the first one": the first still standing, or, when the whole
      // party has fainted, the first that is not an egg. An egg never follows.
      test("the follower published is the first that can walk, HeartGold's rule") {
        runTest {
          val store = CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
          val created = store.createCharacter(2, "Dawn", CharacterGender.FEMALE, Region.SINNOH).info
          val service = MapLoadService(MapManager(), SpeciesRegistry())

          fun mon(dex: Int, hp: Int, egg: Boolean = false) =
              Pokemon(
                  id = dex.toLong(),
                  ownerId = created.id,
                  container = PokemonContainer.PARTY,
                  containerSlot = 0,
                  dexId = dex,
                  seed = 0,
                  ot = "Dawn",
                  nickname = "",
                  level = 5,
                  hp = hp.toShort(),
                  xp = 0,
                  eVs = EVs(),
                  iVs = IVs(),
                  moves = listOf(),
                  isShiny = false,
                  hasHiddenAbility = false,
                  isAlpha = false,
                  isSecret = false,
                  isFatefulEncounter = false,
                  isRaidEncounter = false,
                  caughtAt = LocalDateTime.now(),
                  isEgg = egg,
              )

          // A fainted lead is passed over for the one behind it.
          service.createLoadEntity(created, party = listOf(mon(1, 0), mon(4, 12))).let {
            it.hasFollower shouldBe true
            it.followerDexId shouldBe 4
          }
          // A whole party on the floor still has somebody behind you.
          service.createLoadEntity(created, party = listOf(mon(1, 0), mon(4, 0))).let {
            it.hasFollower shouldBe true
            it.followerDexId shouldBe 1
          }
          // An egg is never it, in either arm.
          service.createLoadEntity(created, party = listOf(mon(1, 0, egg = true), mon(4, 9))).let {
            it.hasFollower shouldBe true
            it.followerDexId shouldBe 4
          }
          service.createLoadEntity(created, party = listOf(mon(1, 5, egg = true))).let {
            it.hasFollower shouldBe false
          }
        }
      }

      test("a new Sinnoh character can step off the bedroom spawn in every direction") {
        runTest {
          val store = CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
          val created = store.createCharacter(1, "Lucas", CharacterGender.MALE, Region.SINNOH)
          val charId = created.info.id
          val maps = MapManager()
          val movement = movementService(store, maps)

          val expected =
              mapOf(
                  Direction.UP to (4 to 5),
                  Direction.DOWN to (4 to 7),
                  Direction.LEFT to (3 to 6),
                  Direction.RIGHT to (5 to 6),
              )
          for ((dir, dest) in expected) {
            store.updatePosition(
                charId, 4.toShort(), 6.toShort(), 1.toByte(), (415 and 0xFF).toByte())
            val session = FakeSession(characterId = charId, regionId = 3, bankId = 1, mapId = 159)
            session.state().x = 4.toShort()
            session.state().y = 6.toShort()

            movement.onMovement(PacketEvent(MovementPacket(4, 6, dir), session))

            val info = store.getCharacter(charId)!!.info
            (info.positionX.toInt() to info.positionY.toInt()) shouldBe dest
          }
        }
      }
      /**
       * Twinleaf Town is a matrix map, so its tiles are global: the doorstep of the player's house
       * is (116, 886), not (6, 9).
       */
      test("a step on a Sinnoh matrix map reaches observers in a packet that holds the tile") {
        runTest {
          val store = CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
          val maps = MapManager()
          val presence =
              PresenceService(
                  InterestManager(),
                  PassThroughInterestPolicy(),
                  MapLoadService(maps, SpeciesRegistry()),
                  store)
          val movement = movementService(store, maps, presence)

          val walker =
              store.createCharacter(1, "Lucas", CharacterGender.MALE, Region.SINNOH).info.id
          val watcher =
              store.createCharacter(2, "Dawn", CharacterGender.FEMALE, Region.SINNOH).info.id
          val town = (155 and 0xFF).toByte()
          store.updatePosition(walker, 116.toShort(), 887.toShort(), 1.toByte(), town)
          store.updatePosition(watcher, 114.toShort(), 887.toShort(), 1.toByte(), town)

          fun seat(charId: Long, x: Int, y: Int): FakeSession {
            val session = FakeSession(characterId = charId, regionId = 3, bankId = 1, mapId = 155)
            session.state().x = x.toShort()
            session.state().y = y.toShort()
            return session
          }
          val a = seat(walker, 116, 887)
          val b = seat(watcher, 114, 887)
          presence.enter(a)
          presence.enter(b)

          movement.onMovement(PacketEvent(MovementPacket(116, 887, Direction.UP), a))

          val step = b.sent.filterIsInstance<EntityMovePacket>().single()
          step.entityId shouldBe walker
          (step.x to step.y) shouldBe (116 to 886)
          EntityMovePacketCodec.encodeToBytes(step).size shouldBeGreaterThan (0)

          // Why it has to be that packet and not the one this used to send.
          shouldThrow<IllegalArgumentException> {
            GbaEntityMovePacketCodec.encodeToBytes(
                GbaEntityMovePacket(
                    entityId = walker,
                    bankId = 1,
                    mapId = 155,
                    x = 116,
                    y = 886,
                    direction = Direction.UP,
                ))
          }
        }
      }
      /**
       * The four steps off the bed are the four tiles carrying the bedroom's coord triggers, whose
       * scripts are the ids 7, 8, 9 and 10, ids, not labels, because a Sinnoh event carries a
       * number that only means anything on its own map.
       */
      test("a bedroom coordinate trigger resolves the map's own numeric script id") {
        runTest {
          val store = CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
          val charId =
              store.createCharacter(1, "Lucas", CharacterGender.MALE, Region.SINNOH).info.id
          var ran = 0
          val rivalTrigger = Script { ran++ }

          fun walkNorth(scripts: ScriptRegistry) {
            val movement = movementService(store, MapManager(), scripts = scripts)
            store.updatePosition(
                charId, 4.toShort(), 6.toShort(), 1.toByte(), (415 and 0xFF).toByte())
            val session = FakeSession(characterId = charId, regionId = 3, bankId = 1, mapId = 159)
            session.attributes[SCRIPT_SCOPE] = backgroundScope
            session.state().x = 4.toShort()
            session.state().y = 6.toShort()

            movement.onMovement(PacketEvent(MovementPacket(4, 6, Direction.UP), session))
            // The trigger runs on the session's own coroutine, so step it before asserting.
            testScheduler.runCurrent()
          }

          walkNorth(ScriptRegistry(mapOf("3:1:159:7" to rivalTrigger)))
          ran shouldBe 1

          // The same id on a neighbouring map is a different script, and must not answer for this
          // one.
          walkNorth(ScriptRegistry(mapOf("3:1:158:7" to rivalTrigger)))
          ran shouldBe 1
        }
      }

      /**
       * A coordinate trigger is a rectangle. The band across the top of Twinleaf Town is the only
       * way out to Route 201 and it is eight tiles wide, and the scene it runs reads the column the
       * player is standing on to know which way to walk the guitarist.
       */
      test("a coordinate trigger wider than one tile fires from every column it covers") {
        runTest {
          val store = CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
          val charId =
              store.createCharacter(1, "Lucas", CharacterGender.MALE, Region.SINNOH).info.id
          val town = MapManager().getMap(3, 1, 155)!!
          val band = town.coordScripts.single { it.script == "4" }
          band.width shouldBe 8

          val story = StoryService(store)
          story.setVar(charId, band.varKey, band.value)
          val entryScripts = MapEntryScripts(ScriptRegistry(mapOf("3:1:155:4" to Script {})), story)

          val covered =
              (band.x until band.x + band.width).count {
                entryScripts.atCoordinate(charId, town, it, band.y) != null
              }
          covered shouldBe band.width
          entryScripts.atCoordinate(charId, town, band.x + band.width, band.y) shouldBe null
        }
      }
    })
