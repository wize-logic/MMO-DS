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
import de.fiereu.openmmo.items.ItemRegistry
import de.fiereu.openmmo.maps.BgEventDef
import de.fiereu.openmmo.maps.MapDef
import de.fiereu.openmmo.maps.MapManager
import de.fiereu.openmmo.maps.NpcDef
import de.fiereu.openmmo.net.game.packets.TileInteractPacket
import de.fiereu.openmmo.server.game.battle.BattleResult
import de.fiereu.openmmo.server.game.script.ScriptRegistry
import de.fiereu.openmmo.server.game.session.PLAYER_STATE
import de.fiereu.openmmo.server.game.session.SCRIPT_SCOPE
import de.fiereu.openmmo.server.game.storage.CharacterStore
import de.fiereu.openmmo.server.game.storage.EntityIdService
import de.fiereu.openmmo.server.game.storage.InMemoryOfflineItemRepository
import de.fiereu.openmmo.server.game.testsupport.FakeCharacterRepository
import de.fiereu.openmmo.server.game.testsupport.FakeSession
import de.fiereu.openmmo.server.game.testsupport.battleService
import de.fiereu.openmmo.server.game.testsupport.scriptRunner
import de.fiereu.openmmo.server.game.testsupport.trainerSightService
import de.fiereu.openmmo.server.game.world.interest.InterestManager
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.collections.shouldContainAll
import io.kotest.matchers.collections.shouldContainExactlyInAnyOrder
import io.kotest.matchers.collections.shouldNotContain
import io.kotest.matchers.nulls.shouldNotBeNull
import io.kotest.matchers.shouldBe
import java.time.LocalDateTime
import kotlinx.coroutines.test.runTest

/**
 * A person whose script stages a wild fight: the press deals the fight, and the fight won or the
 * Pokemon caught hides the person for that character and for good.
 */
class StaticEncounterTest :
    FunSpec({
      /** A ported map with a static site on it, and the site. */
      fun site(maps: MapManager): Pair<MapDef, NpcDef>? =
          maps
              .all()
              .asSequence()
              .filter { it.ported }
              .flatMap { map ->
                map.npcs
                    .asSequence()
                    .filter { it.script.startsWith(StaticEncounterService.MARK) }
                    .map { map to it }
              }
              .firstOrNull()

      /** A site of this game's own that is scenery rather than a person, faced from the south. */
      fun scenery(maps: MapManager): Pair<MapDef, BgEventDef>? =
          maps
              .all()
              .asSequence()
              .filterNot { it.ported }
              .flatMap { map ->
                map.bgEvents
                    .asSequence()
                    .filter { it.script.startsWith(StaticEncounterService.MARK) }
                    .filter {
                      it.facingDir == "BG_EVENT_DIR_ALL" || it.facingDir == "BG_EVENT_DIR_NORTH"
                    }
                    .map { map to it }
              }
              .firstOrNull()

      class Fixture(scope: kotlinx.coroutines.CoroutineScope) {
        val store = CharacterStore(FakeCharacterRepository(), EntityIdService(), scope)
        val maps = MapManager()
        val interest = InterestManager()
        val battles = battleService(store, interest, maps)
        val npcs = NpcService(maps, store)
        val statics = StaticEncounterService(npcs, battles, store)
        val scripts = ScriptRegistry.generated()
        val interactions =
            InteractionService(
                npcs,
                maps,
                store,
                scripts,
                scriptRunner(store, maps, interest, battles, scripts),
                trainerSightService(store, maps, interest, battles, scripts),
                ViolationLog(),
                ShopService(store, ItemRegistry(), InMemoryOfflineItemRepository()),
                ItemRegistry(),
                statics,
            )

        /** A player with one monster, standing below (x, y) and facing it. */
        suspend fun playerAt(
            map: MapDef,
            x: Int,
            y: Int,
            name: String,
        ): Pair<FakeSession, Long> {
          val id = store.createCharacter(1, name, CharacterGender.MALE, Region.SINNOH).info.id
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
                  level = 50,
                  hp = 100,
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
          store.updatePosition(
              id, x.toShort(), (y + 1).toShort(), map.bankId, map.mapId, Direction.UP)
          val session =
              FakeSession(
                  characterId = id,
                  regionId = map.regionId.toInt(),
                  bankId = map.bankId.toInt(),
                  mapId = map.mapId.toInt())
          val state = session.attributes[PLAYER_STATE].shouldNotBeNull()
          state.regionId = map.regionId.toInt()
          state.bankId = map.bankId.toInt()
          state.mapId = map.mapId.toInt()
          state.x = x.toShort()
          state.y = (y + 1).toShort()
          state.facingDirection = Direction.UP
          return session to id
        }

        suspend fun player(map: MapDef, site: NpcDef, name: String): Pair<FakeSession, Long> =
            playerAt(map, site.x, site.y, name)

        fun press(session: FakeSession) =
            interactions.onTileInteract(PacketEvent(TileInteractPacket(), session))
      }

      test("the sites the cartridge stages are marked with their species and level") {
        val maps = MapManager()
        val sites =
            maps
                .all()
                .filter { it.ported }
                .flatMap { map ->
                  map.npcs
                      .filter { it.script.startsWith(StaticEncounterService.MARK) }
                      .map {
                        map.name to StaticEncounterService.siteOf(it.script).shouldNotBeNull()
                      }
                }
        // The Red Gyarados, Mewtwo and the three birds, at the source's own levels.
        sites shouldContainAll
            listOf(
                "lake_of_rage" to (130 to 30),
                "cerulean_cave_b1f" to (150 to 70),
                "seafoam_islands_b4f" to (144 to 50),
                "route_10" to (145 to 50),
                "mount_silver_cave_moltres_chamber" to (146 to 50),
            )
        sites.forEach { (_, it) -> it.second shouldBe it.second.coerceIn(1, 100) }
      }

      test("a press on a site deals its fight, and the win hides the site for that character") {
        runTest {
          val fx = Fixture(backgroundScope)
          val (map, site) = site(fx.maps).shouldNotBeNull()
          val (dexId, level) = StaticEncounterService.siteOf(site.script).shouldNotBeNull()
          val (session, charId) = fx.player(map, site, "Red")
          session.attributes[SCRIPT_SCOPE] = backgroundScope
          val region = map.regionId.toInt()
          val bank = map.bankId.toInt()
          val mapId = map.mapId.toInt()

          fx.press(session)
          fx.battles.inBattle(charId) shouldBe true
          val battle = fx.statics.running[charId].shouldNotBeNull().battle
          battle.opponent.single().species.id shouldBe dexId
          battle.opponent.single().level shouldBe level

          // The fight is fought on the client's engine, which says it is won before the run that
          // closes this instance: the site's flag is the character's, and the site is gone.
          fx.statics.onWon(session)
          fx.store
              .getCharacter(charId)
              .shouldNotBeNull()
              .storyFlags
              .contains(site.hideFlag) shouldBe true
          fx.npcs.visibleNpcAt(session, region, bank, mapId, site.x, site.y) shouldBe null
          battle.completion.complete(BattleResult.FLED)
          fx.statics.running[charId] shouldBe null
        }
      }

      /*
       * The claim a catch here is answered with, which is how a door on the other side of the
       * server tells this Pokemon apart from every other one.
       */
      test("a site's fight claims its species, and a catch keeps the claim for the grant") {
        runTest {
          val fx = Fixture(backgroundScope)
          val (map, site) = site(fx.maps).shouldNotBeNull()
          val (session, charId) = fx.player(map, site, "Silver")
          session.attributes[SCRIPT_SCOPE] = backgroundScope
          val dexId =
              site.script.removePrefix(StaticEncounterService.MARK).substringBefore(':').toInt()

          fx.press(session)
          val battle = fx.statics.running[charId].shouldNotBeNull().battle
          battle.completion.complete(BattleResult.CAUGHT)

          // The ball was thrown on the client's engine; the grant that reports it arrives after
          // this, names a species and nothing else, and this is what it is matched against.
          fx.statics.claimStaticCatch(charId, dexId) shouldBe true
          // Once. A second grant of the same species is a second monster, not this one.
          fx.statics.claimStaticCatch(charId, dexId) shouldBe false
        }
      }

      test("a fight that ended any other way claims nothing") {
        runTest {
          val fx = Fixture(backgroundScope)
          val (map, site) = site(fx.maps).shouldNotBeNull()
          val (session, charId) = fx.player(map, site, "Kris")
          session.attributes[SCRIPT_SCOPE] = backgroundScope
          val dexId =
              site.script.removePrefix(StaticEncounterService.MARK).substringBefore(':').toInt()

          fx.press(session)
          val battle = fx.statics.running[charId].shouldNotBeNull().battle
          battle.completion.complete(BattleResult.FLED)

          // Nothing went into a ball, so nothing caught afterwards is this site's.
          fx.statics.claimStaticCatch(charId, dexId) shouldBe false
        }
      }

      test("a claim is the species' own, and ages out") {
        runTest {
          val fx = Fixture(backgroundScope)
          val (map, site) = site(fx.maps).shouldNotBeNull()
          val (session, charId) = fx.player(map, site, "Ethan")
          session.attributes[SCRIPT_SCOPE] = backgroundScope
          val dexId =
              site.script.removePrefix(StaticEncounterService.MARK).substringBefore(':').toInt()

          fx.press(session)
          // The Bidoof caught on the way out is not the legendary.
          fx.statics.claimStaticCatch(charId, dexId + 1) shouldBe false
          // And a claim nobody ever spent is not still standing hours later.
          val late = System.currentTimeMillis() + StaticEncounterService.CLAIM_WINDOW_MS + 1
          fx.statics.claimStaticCatch(charId, dexId, late) shouldBe false
          fx.statics.claimStaticCatch(charId, dexId) shouldBe false
        }
      }

      test("a catch is this server's own end of the fight, and hides the site as a win does") {
        runTest {
          val fx = Fixture(backgroundScope)
          val (map, site) = site(fx.maps).shouldNotBeNull()
          val (session, charId) = fx.player(map, site, "Gold")
          session.attributes[SCRIPT_SCOPE] = backgroundScope
          fx.press(session)
          val battle = fx.statics.running[charId].shouldNotBeNull().battle
          battle.completion.complete(BattleResult.CAUGHT)
          fx.store
              .getCharacter(charId)
              .shouldNotBeNull()
              .storyFlags
              .contains(site.hideFlag) shouldBe true
          fx.npcs.visibleNpcAt(
              session,
              map.regionId.toInt(),
              map.bankId.toInt(),
              map.mapId.toInt(),
              site.x,
              site.y) shouldBe null
        }
      }

      test("a lost fight leaves the Pokemon standing, and a won site answers no second press") {
        runTest {
          val fx = Fixture(backgroundScope)
          val (map, site) = site(fx.maps).shouldNotBeNull()
          val region = map.regionId.toInt()
          val bank = map.bankId.toInt()
          val mapId = map.mapId.toInt()

          val (loser, loserId) = fx.player(map, site, "Blue")
          loser.attributes[SCRIPT_SCOPE] = backgroundScope
          fx.press(loser)
          val battle = fx.statics.running[loserId].shouldNotBeNull().battle
          battle.completion.complete(BattleResult.FLED)
          fx.store
              .getCharacter(loserId)
              .shouldNotBeNull()
              .storyFlags
              .contains(site.hideFlag) shouldBe false
          fx.npcs.visibleNpcAt(loser, region, bank, mapId, site.x, site.y).shouldNotBeNull()

          val (winner, winnerId) = fx.player(map, site, "Green")
          winner.attributes[SCRIPT_SCOPE] = backgroundScope
          fx.store.setStoryFlag(winnerId, site.hideFlag)
          fx.press(winner)
          fx.battles.inBattle(winnerId) shouldBe false
          // And a win reported with no fight dealt is nothing.
          fx.statics.onWon(winner)
        }
      }

      test("this game's own sites carry the species and level its own scripts name") {
        val maps = MapManager()
        val marks =
            maps
                .all()
                .filterNot { it.ported }
                .flatMap { map ->
                  (map.npcs.map { it.script } + map.bgEvents.map { it.script })
                      .filter { it.startsWith(StaticEncounterService.MARK) }
                      .map { map.name to StaticEncounterService.siteOf(it).shouldNotBeNull() }
                }
        // The lake trio's two catchable members, the creation trio, Giratina in Turnback Cave,
        // Heatran, the Windworks Drifloon and the Old Chateau's Rotom, at the source's own levels.
        marks shouldContainAll
            listOf(
                "acuity_cavern" to (480 to 50),
                "valor_cavern" to (482 to 50),
                "spear_pillar_dialga" to (483 to 70),
                "spear_pillar_palkia" to (484 to 70),
                "stark_mountain_room_3" to (485 to 50),
                "snowpoint_temple_b5f" to (486 to 1),
                "turnback_cave_giratina_room" to (487 to 47),
                "valley_windworks_outside" to (425 to 15),
                "old_chateau_back_middle_west_room" to (479 to 20),
            )
        // Mesprit is not one of them: its scene never stages a fight, it flees and roams.
        marks.map { it.second.first } shouldNotContain 481
        marks.forEach { (_, it) -> it.second shouldBe it.second.coerceIn(1, 100) }
      }

      test("a press on a sign that stages a fight deals it, and nothing is hidden here") {
        runTest {
          val fx = Fixture(backgroundScope)
          val (map, sign) = scenery(fx.maps).shouldNotBeNull()
          val (dexId, level) = StaticEncounterService.siteOf(sign.script).shouldNotBeNull()
          val (session, charId) = fx.playerAt(map, sign.x, sign.y, "Lucas")
          session.attributes[SCRIPT_SCOPE] = backgroundScope

          fx.press(session)
          val run = fx.statics.running[charId].shouldNotBeNull()
          run.battle.opponent.single().species.id shouldBe dexId
          run.battle.opponent.single().level shouldBe level

          // A sign is nobody, so there is no person to hide and no hide flag to set: the scene the
          // client plays sets the game's own flags and those are the record.
          run.npc shouldBe null
          fx.statics.onWon(session)
          run.battle.completion.complete(BattleResult.FLED)
          fx.statics.running[charId] shouldBe null
        }
      }

      test("a scene nobody on the map stands for is the map's own site") {
        val maps = MapManager()
        val loose =
            maps
                .all()
                .filter { it.staticSite.isNotEmpty() }
                .map { it.name to StaticEncounterService.siteOf(it.staticSite).shouldNotBeNull() }
        // Arceus off the Hall of Origin's coord event, and the Distortion World's mandatory
        // Giratina, whose room carries no event table for a mark to sit on. Nothing else in the
        // cartridge stages a fight that neither a person nor a sign carries.
        loose shouldContainExactlyInAnyOrder
            listOf(
                "hall_of_origin" to (493 to 80),
                "distortion_world_giratina_room" to (487 to 47),
            )
        // And a map's own site is only ever the fight nobody carries: no map holds both.
        maps.all().forEach { map ->
          if (map.staticSite.isNotEmpty()) {
            (map.npcs.map { it.script } + map.bgEvents.map { it.script }).filter {
              it.startsWith(StaticEncounterService.MARK)
            } shouldBe emptyList()
          }
        }
      }

      test("a press that resolves to nobody deals the map's own site") {
        runTest {
          val fx = Fixture(backgroundScope)
          val map = fx.maps.byName("distortion_world_giratina_room").shouldNotBeNull()
          // The tile Giratina stands on: the overlay that adds it puts it at (15, 13), and it is
          // on no event list here because the map has no event list.
          val (session, charId) = fx.playerAt(map, 15, 13, "Dawn")
          session.attributes[SCRIPT_SCOPE] = backgroundScope
          fx.npcs.visibleNpcAt(
              session, map.regionId.toInt(), map.bankId.toInt(), map.mapId.toInt(), 15, 13) shouldBe
              null

          fx.press(session)
          val run = fx.statics.running[charId].shouldNotBeNull()
          run.battle.opponent.single().species.id shouldBe 487
          run.battle.opponent.single().level shouldBe 47
          run.npc shouldBe null
          run.battle.completion.complete(BattleResult.FLED)
          fx.statics.running[charId] shouldBe null
        }
      }

      test("a press on a map with no site of its own deals nothing") {
        runTest {
          val fx = Fixture(backgroundScope)
          val map = fx.maps.byName("turnback_cave_giratina_room").shouldNotBeNull()
          map.staticSite shouldBe ""
          // A tile with nobody on it and no sign: the fight there is on a person, and only that
          // person's tile answers for it.
          val empty =
              (0 until map.width step 4)
                  .flatMap { x -> (0 until map.height step 4).map { y -> x to y } }
                  .first { (x, y) ->
                    map.npcs.none { it.x == x && it.y == y } &&
                        map.bgEvents.none { it.x == x && it.y == y }
                  }
          val (session, charId) = fx.playerAt(map, empty.first, empty.second, "Barry")
          session.attributes[SCRIPT_SCOPE] = backgroundScope

          fx.press(session)
          fx.battles.inBattle(charId) shouldBe false
          fx.statics.running[charId] shouldBe null
        }
      }
    })
