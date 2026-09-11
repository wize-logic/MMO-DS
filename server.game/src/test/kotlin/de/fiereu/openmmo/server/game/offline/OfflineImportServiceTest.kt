package de.fiereu.openmmo.server.game.offline

import de.fiereu.openmmo.common.HealLocation
import de.fiereu.openmmo.common.enums.CharacterGender
import de.fiereu.openmmo.common.enums.PokemonContainer
import de.fiereu.openmmo.common.enums.PokemonStat
import de.fiereu.openmmo.common.enums.Region
import de.fiereu.openmmo.common.enums.TileBehavior
import de.fiereu.openmmo.items.ItemRegistry
import de.fiereu.openmmo.maps.MapManager
import de.fiereu.openmmo.moves.MoveRegistry
import de.fiereu.openmmo.net.game.packets.ChatMessagePacket
import de.fiereu.openmmo.offline.CartridgeLimits
import de.fiereu.openmmo.pokemon.EvolutionRegistry
import de.fiereu.openmmo.pokemon.LearnsetRegistry
import de.fiereu.openmmo.pokemon.MoveSourceRegistry
import de.fiereu.openmmo.pokemon.SpeciesRegistry
import de.fiereu.openmmo.server.game.battle.ExpCurves
import de.fiereu.openmmo.server.game.services.DuelService
import de.fiereu.openmmo.server.game.services.GrantBudget
import de.fiereu.openmmo.server.game.services.TradeService
import de.fiereu.openmmo.server.game.services.ViolationLog
import de.fiereu.openmmo.server.game.session.SessionRegistry
import de.fiereu.openmmo.server.game.storage.CharacterStore
import de.fiereu.openmmo.server.game.storage.Containers
import de.fiereu.openmmo.server.game.storage.EntityIdService
import de.fiereu.openmmo.server.game.storage.InMemoryImportRepository
import de.fiereu.openmmo.server.game.storage.InMemoryOfflineItemRepository
import de.fiereu.openmmo.server.game.storage.InMemorySaveBlockRepository
import de.fiereu.openmmo.server.game.storage.LinkStore
import de.fiereu.openmmo.server.game.testsupport.FakeCharacterRepository
import de.fiereu.openmmo.server.game.testsupport.FakeSession
import de.fiereu.openmmo.server.game.testsupport.battleService
import de.fiereu.openmmo.server.game.world.interest.InterestManager
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.collections.shouldBeEmpty
import io.kotest.matchers.collections.shouldContain as shouldHold
import io.kotest.matchers.collections.shouldNotBeEmpty
import io.kotest.matchers.nulls.shouldBeNull
import io.kotest.matchers.shouldBe
import io.kotest.matchers.shouldNotBe
import io.kotest.matchers.string.shouldContain
import io.kotest.matchers.types.shouldBeInstanceOf
import java.time.LocalDateTime
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.test.runTest

/**
 * The event around the door: what may happen at all, what it costs, what is written down, and how
 * it is taken back.
 */
class OfflineImportServiceTest :
    FunSpec({
      val species = SpeciesRegistry()
      val moves = MoveRegistry()
      val maps = MapManager()
      val cartridge = CartridgeLimits()

      // Route 201, and a tile of it somebody can stand on, so the position row lands rather than
      // being brought back to the heal location and changing what the test is measuring.
      val route201 = checkNotNull(maps.getMap(3, 1, 86))
      val standable =
          (0 until route201.height)
              .flatMap { y -> (0 until route201.width).map { x -> x to y } }
              .first { (x, y) ->
                val tile = route201.tileAt(x, y)
                tile != null &&
                    !tile.blocksMovement() &&
                    tile.behavior != TileBehavior.SURFABLE_WATER &&
                    tile.behavior != TileBehavior.WATERFALL
              }
      val here = OfflinePosition(3, 1, 86, standable.first, standable.second)

      fun monster(
          pid: Int,
          dexId: Int = TURTWIG,
          level: Int = 20,
          container: PokemonContainer = PokemonContainer.PARTY,
          slot: Int = 0,
          metLocation: Int = 0,
          status: Int = 0,
      ) =
          OfflineMonster(
              pid = pid,
              dexId = dexId,
              form = 0,
              level = level,
              xp =
                  species.get(dexId)?.let {
                    ExpCurves.totalXpFor(it.growthRate, level.coerceIn(1, ExpCurves.MAX_LEVEL))
                  } ?: 0,
              ivs = PokemonStat.entries.associateWith { 10 },
              evs = PokemonStat.entries.associateWith { 4 },
              moves = listOf(OfflineMove(TACKLE, checkNotNull(moves.get(TACKLE)).pp, 0)),
              nickname = "Turty",
              otName = "Barry",
              otId = TRAINER_ID,
              abilityId = species.get(dexId)?.ability1?.ordinal ?: 0,
              hasHiddenAbility = false,
              natureByte = 0,
              isShiny = false,
              heldItemId = 0,
              friendship = 70,
              isEgg = false,
              eggCyclesLeft = 0,
              container = container,
              containerSlot = slot,
              metLocationLabel = metLocation,
              status = status,
          )

      fun save(
          monsters: List<OfflineMonster> = listOf(monster(0x11111111)),
          money: Int = 50_000,
          badges: Int = 2,
          flags: Set<String> = setOf("sinnoh/BADGE_ID_COAL", "sinnoh/BADGE_ID_FOREST"),
          blackOutWarpId: Int = 0,
      ) =
          OfflineSave(
              trainerId = TRAINER_ID,
              monsters = monsters,
              money = money,
              bag = listOf(OfflineItem(SUN_STONE, 2)),
              badges = badges,
              dexSeen = setOf(TURTWIG),
              dexCaught = setOf(TURTWIG),
              position = here,
              blackOutWarpId = blackOutWarpId,
              playTimeSeconds = 7200,
              storyFlags = flags,
              storyVars = mapOf("sinnoh/LEAGUE_VICTORIES" to 1),
              blocks = mapOf(0xC2 to byteArrayOf(1, 2, 3)),
          )

      class Rig(scope: CoroutineScope, val now: () -> LocalDateTime) {
        val repository = FakeCharacterRepository()
        val store = CharacterStore(repository, EntityIdService(), scope)
        val imports = InMemoryImportRepository()
        val blocks = InMemorySaveBlockRepository()
        val offlineItems = InMemoryOfflineItemRepository()
        val links = LinkStore()
        val budget = GrantBudget()
        val sessions = SessionRegistry()
        val battles = battleService(store, InterestManager(), maps)
        val duels = DuelService(sessions, store, battles)
        val trades = TradeService(sessions, store, battles, duels, imports)
        val service =
            OfflineImportService(
                legality =
                    OfflineImportLegality(
                        species = species,
                        learnsets = LearnsetRegistry(),
                        moveSources = MoveSourceRegistry(),
                        evolutions = EvolutionRegistry(),
                        moves = moves,
                        items = ItemRegistry(),
                        maps = maps,
                        cartridge = cartridge,
                    ),
                characters = store,
                saveBlocks = blocks,
                offlineItems = offlineItems,
                imports = imports,
                budget = budget,
                violations = ViolationLog(),
                entityIds = EntityIdService(),
                species = species,
                cartridge = cartridge,
                trades = trades,
                duels = duels,
                links = links,
                sessions = sessions,
                clock = now,
            )
      }

      var clock = LocalDateTime.of(2026, 9, 4, 12, 0)

      suspend fun rigWithCharacter(scope: CoroutineScope): Pair<Rig, Long> {
        val rig = Rig(scope) { clock }
        val created =
            rig.store.createCharacter(1, "Probe${counter++}", CharacterGender.MALE, Region.SINNOH)
        return rig to created.info.id
      }

      fun request(id: Long, save: OfflineSave) =
          ImportRequest(
              characterId = id, save = save, saveSha256 = "a".repeat(64), clientRevision = 7)

      test("a save replaces the whole character and every monster it brings wears the mark") {
        runTest {
          val (rig, id) = rigWithCharacter(backgroundScope)
          val started = checkNotNull(rig.store.getCharacter(id)).info.money

          val outcome = rig.service.import(request(id, save(money = 12_345)))

          outcome.shouldBeInstanceOf<ImportOutcome.Landed>()
          val stored = checkNotNull(rig.store.getCharacter(id))
          // The whole character except its wallet: money is earned in the world it is spent in.
          stored.info.money shouldBe started
          stored.info.positionMapId.toInt() shouldBe 86
          stored.pokemon.single().dexId shouldBe TURTWIG
          stored.pokemon.single().offlineOrigin shouldBe true
          stored.items shouldBe mutableMapOf(SUN_STONE to 2)
          stored.storyFlags shouldHold "sinnoh/BADGE_ID_COAL"
          rig.blocks.load(id).keys shouldBe setOf(0xC2)
        }
      }

      /*
       * The identity door, from the service's side: the point is that nothing lands, not just
       * that a verdict was written. A stranger's finished game landing here would put its
       * unmarked half, badges, story, Pokedex, key items, on an account that never played
       * it.
       */
      test("a save that is not this character's game lands nothing at all") {
        runTest {
          val (rig, id) = rigWithCharacter(backgroundScope)
          // The character has a game of its own here: one Pokemon the record knows.
          rig.service.import(request(id, save(monsters = listOf(monster(0x11111111)))))
          val before = checkNotNull(rig.store.getCharacter(id))

          // Somebody else's save: a different trainer id, and not one Pokemon in common.
          val theirs =
              save(monsters = listOf(monster(0x77777777)), money = 999_999, badges = 0xFF)
                  .copy(trainerId = 0x0BAD0BAD)
          val outcome = rig.service.import(request(id, theirs))

          outcome.shouldBeInstanceOf<ImportOutcome.Refused>()
          outcome.why shouldContain "a new character"
          val after = checkNotNull(rig.store.getCharacter(id))
          after.info.money shouldBe before.info.money
          after.pokemon.single().seed shouldBe before.pokemon.single().seed
        }
      }

      test("the same file landing on a second character is allowed and written down") {
        runTest {
          val (rig, first) = rigWithCharacter(backgroundScope)
          val second =
              rig.store
                  .createCharacter(2, "Probe${counter++}", CharacterGender.MALE, Region.SINNOH)
                  .info
                  .id

          rig.service.import(request(first, save())).shouldBeInstanceOf<ImportOutcome.Landed>()
          // A brand-new character has nothing to test identity against, which is the offline-first
          // arrival and the one door a passed-around file can still walk through. So it lands,
          // and it says, to the player and to the log, that the file has been here before.
          val outcome = rig.service.import(request(second, save()))

          outcome.shouldBeInstanceOf<ImportOutcome.Landed>()
          outcome.verdicts.map { it.toString() }.any { it.contains("this save file") } shouldBe true
        }
      }

      test("a save keyed to a region that is not the character's is refused whole") {
        runTest {
          val (rig, id) = rigWithCharacter(backgroundScope)
          val before = checkNotNull(rig.store.getCharacter(id))
          val elsewhere = save().let { it.copy(position = it.position.copy(regionId = 1)) }

          val outcome = rig.service.import(request(id, elsewhere))

          outcome.shouldBeInstanceOf<ImportOutcome.Refused>()
          // And nothing of it landed: the character is still the one the store made.
          val after = checkNotNull(rig.store.getCharacter(id))
          after.info.money shouldBe before.info.money
          after.pokemon.shouldBeEmpty()
        }
      }

      test("the place a save says a monster was caught crosses onto the record as a label") {
        runTest {
          val (rig, id) = rigWithCharacter(backgroundScope)

          // 34 is Eterna Forest in the game's own location names, and the save is the only thing
          // that knows it: the file keeps the label a catch stamped and never the map it was on.
          rig.service.import(
              request(id, save(monsters = listOf(monster(0x11111111, metLocation = 34)))))

          val landed = checkNotNull(rig.store.getCharacter(id)).pokemon.single()
          landed.caughtLocationLabel shouldBe 34
          // And the triple stays absent rather than being invented from the label: up to 46 map
          // headers share one label, so there is no map to put here.
          landed.caughtMapId shouldBe -1
        }
      }

      test("a monster the save left poisoned comes online still poisoned") {
        runTest {
          val (rig, id) = rigWithCharacter(backgroundScope)

          // Badly poisoned, three turns in: the client engine's own condition word, whose top
          // four bits are the count of turns.
          rig.service.import(
              request(id, save(monsters = listOf(monster(0x11111111, status = 0x380)))))

          checkNotNull(rig.store.getCharacter(id)).pokemon.single().status shouldBe 0x380
        }
      }

      test("a monster a save records no place for keeps the label the game draws as unknown") {
        runTest {
          val (rig, id) = rigWithCharacter(backgroundScope)

          rig.service.import(request(id, save()))

          checkNotNull(rig.store.getCharacter(id)).pokemon.single().caughtLocationLabel shouldBe 0
        }
      }

      test("the save's black-out row becomes where a white out brings the character back to") {
        runTest {
          val (rig, id) = rigWithCharacter(backgroundScope)
          val started = checkNotNull(rig.store.getCharacter(id)).info.lastHealLocation

          // Row 2 of the cartridge's spawn table is Sandgem Town's Pokemon Center, bank 1 map 164
          // at 8,6. The position cannot stand in for it: a save left on a route names no row, and
          // the row is the only thing in the file that says where a white out lands.
          rig.service.import(request(id, save(blackOutWarpId = 2)))

          val healed = HealLocation(regionId = 3, bankId = 1, mapId = 164.toByte(), x = 8, y = 6)
          checkNotNull(rig.store.getCharacter(id)).info.lastHealLocation shouldBe healed
          healed shouldNotBe started

          // A save that has never been in a Center says row 0, which is no row at all: the
          // character keeps where it last healed rather than being sent to whatever 0 rounds to.
          rig.service.import(request(id, save()))
          checkNotNull(rig.store.getCharacter(id)).info.lastHealLocation shouldBe healed
        }
      }

      test("a boarder at the day care stays there, and the save's copy of it is dropped") {
        runTest {
          val (rig, id) = rigWithCharacter(backgroundScope)
          val boarder = 0x11111111
          rig.service.import(request(id, save(listOf(monster(boarder)))))
          // Left at the day care here, then the same save, which still has it in the party,
          // is brought online again. The custody is the server's; the file's copy is the stale one.
          rig.store.rearrangeMonsters(id) { party, pc, daycare ->
            Containers(emptyList(), pc, daycare + party)
          }

          val outcome = rig.service.import(request(id, save(listOf(monster(boarder)))))

          outcome.shouldBeInstanceOf<ImportOutcome.Landed>()
          outcome.verdicts.map { it.toString() }.any { "day care" in it } shouldBe true
          val stored = checkNotNull(rig.store.getCharacter(id))
          stored.pokemon.shouldBeEmpty()
          stored.daycare.single().seed shouldBe boarder
        }
      }

      test("an import makes the blocks the save's around the day care's, and an undo puts back") {
        runTest {
          val (rig, id) = rigWithCharacter(backgroundScope)
          // A Pokedex the file will not carry, a day care the door keeps here, and the clock block
          // the file carries a newer copy of.
          rig.blocks.save(
              id, mapOf(7 to byteArrayOf(7), 8 to byteArrayOf(8), 0xC2 to byteArrayOf(9)))

          rig.service.import(request(id, save())).shouldBeInstanceOf<ImportOutcome.Landed>()

          val landed = rig.blocks.load(id)
          landed.keys shouldBe setOf(8, 0xC2)
          landed.getValue(0xC2).toList() shouldBe listOf<Byte>(1, 2, 3)
          landed.getValue(8).toList() shouldBe listOf<Byte>(8)

          rig.service.undoLatest(id).shouldBeInstanceOf<UndoOutcome.Restored>()

          val restored = rig.blocks.load(id)
          restored.keys shouldBe setOf(7, 8, 0xC2)
          restored.getValue(0xC2).toList() shouldBe listOf<Byte>(9)
        }
      }

      test("keys only the server writes survive an import; the save's own kinds are replaced") {
        runTest {
          val (rig, id) = rigWithCharacter(backgroundScope)
          rig.store.setStoryFlag(id, "sinnoh/FLAG_MAP_LOCAL_HIDE_OBSTACLE_1")
          rig.store.setStoryFlag(id, "sinnoh/vm/flag/5")
          rig.store.setStoryFlag(id, "sinnoh/BADGE_ID_OLD")
          rig.store.setStoryVar(id, "sinnoh/vm/var/3", 7)
          rig.store.setStoryVar(id, "sinnoh/COUNTER", 2)

          rig.service.import(request(id, save())).shouldBeInstanceOf<ImportOutcome.Landed>()

          val stored = checkNotNull(rig.store.getCharacter(id))
          stored.storyFlags shouldHold "sinnoh/FLAG_MAP_LOCAL_HIDE_OBSTACLE_1"
          stored.storyFlags shouldHold "sinnoh/BADGE_ID_COAL"
          ("sinnoh/vm/flag/5" in stored.storyFlags) shouldBe false
          ("sinnoh/BADGE_ID_OLD" in stored.storyFlags) shouldBe false
          stored.storyVars["sinnoh/COUNTER"] shouldBe 2
          stored.storyVars["sinnoh/vm/var/3"] shouldBe null
          stored.storyVars["sinnoh/LEAGUE_VICTORIES"] shouldBe 1
        }
      }

      test("a landed save moves the session standing on the character to where the save was") {
        runTest {
          val (rig, id) = rigWithCharacter(backgroundScope)
          val session = FakeSession(characterId = id, regionId = 3, bankId = 0, mapId = 1)
          rig.sessions.bindCharacter(session, id)

          rig.service.import(request(id, save())).shouldBeInstanceOf<ImportOutcome.Landed>()

          session.state().regionId shouldBe here.regionId
          session.state().bankId shouldBe here.bankId
          session.state().mapId shouldBe here.mapId
          session.state().x.toInt() shouldBe here.x
          session.state().y.toInt() shouldBe here.y
        }
      }

      test("an undo moves the session back, and ending it says so before the socket closes") {
        runTest {
          val (rig, id) = rigWithCharacter(backgroundScope)
          val start = checkNotNull(rig.store.getCharacter(id)).info
          val session = FakeSession(characterId = id, regionId = 3, bankId = 0, mapId = 1)
          rig.sessions.bindCharacter(session, id)
          rig.service.import(request(id, save()))
          session.state().mapId shouldBe here.mapId

          rig.service.undoLatest(id).shouldBeInstanceOf<UndoOutcome.Restored>()
          rig.service.endSessionOf(id, "Back as you were.", "undone")

          session.state().mapId shouldBe (start.positionMapId.toInt() and 0xFF)
          session.state().x shouldBe start.positionX
          session.state().y shouldBe start.positionY
          (session.sent.last() as ChatMessagePacket).message shouldBe "Back as you were."
          session.closeReasons shouldBe listOf("undone")
        }
      }

      test("ending the session of a character nobody is playing does nothing") {
        runTest {
          val (rig, id) = rigWithCharacter(backgroundScope)
          rig.service.endSessionOf(id, "Back as you were.", "undone")
        }
      }

      test("the import is written down with the numbers a person reads a pattern from") {
        runTest {
          val (rig, id) = rigWithCharacter(backgroundScope)
          val started = checkNotNull(rig.store.getCharacter(id)).info.money

          rig.service.import(
              request(
                  id,
                  save(
                      monsters =
                          listOf(
                              monster(0x11111111, level = 40),
                              monster(0x22222222, level = 30, container = PokemonContainer.PC)),
                      money = 12_345)))

          val row = rig.imports.listFor(id, 5).single()
          row.partyCount shouldBe 1
          row.boxCount shouldBe 1
          row.speciesCount shouldBe 1
          row.levelTotal shouldBe 70
          row.levelMax shouldBe 40
          // Equal now, and both of them the character's own: the save's wallet stays behind, and
          // what it claimed is in the verdicts rather than in these two columns.
          row.moneyBefore shouldBe started
          row.moneyAfter shouldBe started
          row.badgesBefore shouldBe 0
          row.badgesAfter shouldBe 2
          row.trainerId shouldBe TRAINER_ID
          row.clientRevision shouldBe 7
          row.playTimeSeconds shouldBe 7200
        }
      }

      test("the player's own undo puts back exactly what the import replaced") {
        runTest {
          val (rig, id) = rigWithCharacter(backgroundScope)
          rig.store.addMoney(id, 4_000)
          val before = checkNotNull(rig.store.getCharacter(id)).copy()

          rig.service.import(request(id, save(money = 900_000)))
          // The file's 900,000 stayed in the file; the character kept the 4,000 it had here.
          checkNotNull(rig.store.getCharacter(id)).info.money shouldBe before.info.money

          rig.service.undoLatest(id).shouldBeInstanceOf<UndoOutcome.Restored>()

          val after = checkNotNull(rig.store.getCharacter(id))
          after.info.money shouldBe before.info.money
          after.pokemon shouldBe before.pokemon
          after.storyFlags shouldBe before.storyFlags
          rig.imports.listFor(id, 5).single().undone shouldBe true
        }
      }

      /*
       * Money does not cross, and that is a wall rather than a cap (owner's call, 2026-09-06).
       */
      test("the save's money stays in the save, and the character keeps its own") {
        runTest {
          val (rig, id) = rigWithCharacter(backgroundScope)
          val started = checkNotNull(rig.store.getCharacter(id)).info.money

          val outcome = rig.service.import(request(id, save(money = 999_999)))

          outcome.shouldBeInstanceOf<ImportOutcome.Landed>()
          checkNotNull(rig.store.getCharacter(id)).info.money shouldBe started
          // Said out loud: the player is told where their money went, not left to find out.
          outcome.verdicts
              .map { it.toString() }
              .any { it.startsWith("money dropped:") && it.contains("999999") } shouldBe true
        }
      }

      /*
       * And the bag, which is money with an extra step: every item has a price and a mart pays
       * half of it.
       */
      test("what the bag brought is marked, so a mart will not turn it into money") {
        runTest {
          val (rig, id) = rigWithCharacter(backgroundScope)

          rig.service.import(request(id, save())).shouldBeInstanceOf<ImportOutcome.Landed>()

          checkNotNull(rig.store.getCharacter(id)).items[SUN_STONE] shouldBe 2
          rig.offlineItems.load(id)[SUN_STONE] shouldBe 2
        }
      }

      test("an undo puts the marks back with the bag they are about") {
        runTest {
          val (rig, id) = rigWithCharacter(backgroundScope)
          rig.service.import(request(id, save())).shouldBeInstanceOf<ImportOutcome.Landed>()

          rig.service.undoLatest(id).shouldBeInstanceOf<UndoOutcome.Restored>()

          // The character is what it was before the file, and so is what it may sell.
          rig.offlineItems.load(id).isEmpty() shouldBe true
        }
      }

      test("once something has left the character the player's undo is refused and says why") {
        runTest {
          val (rig, id) = rigWithCharacter(backgroundScope)
          rig.service.import(request(id, save()))
          rig.imports.seal(id, clock, "a trade with Ash")

          val refused = rig.service.undoLatest(id)

          refused.shouldBeInstanceOf<UndoOutcome.Refused>()
          refused.why shouldContain "a trade with Ash"
          // The record is still there, so a moderator can still put it back.
          rig.imports.listFor(id, 5).single().undone shouldBe false
        }
      }

      test("a moderator can undo a sealed import; the player cannot") {
        runTest {
          val (rig, id) = rigWithCharacter(backgroundScope)
          rig.service.import(request(id, save()))
          rig.imports.seal(id, clock, "a monster was listed on the market")
          val row = rig.imports.listFor(id, 5).single()

          rig.service.rollBack(row.id, "a moderator").shouldBeInstanceOf<UndoOutcome.Restored>()

          checkNotNull(rig.store.getCharacter(id)).pokemon.shouldBeEmpty()
          rig.imports.find(row.id)?.rolledBackBy shouldBe "a moderator"
        }
      }

      test("a moderator's undo reaches a player who is offline, and lets them go again") {
        runTest {
          val (rig, id) = rigWithCharacter(backgroundScope)
          rig.service.import(request(id, save()))
          val row = rig.imports.listFor(id, 5).single()
          // Logged out: written and dropped from the cache, where a write used to find nothing.
          rig.store.unloadCharacterAsync(id)
          testScheduler.runCurrent()
          rig.store.getCharacter(id).shouldBeNull()
          checkNotNull(rig.repository.saved[id]).pokemon.shouldNotBeEmpty()

          rig.service.rollBack(row.id, "a moderator").shouldBeInstanceOf<UndoOutcome.Restored>()
          testScheduler.runCurrent()

          checkNotNull(rig.repository.saved[id]).pokemon.shouldBeEmpty()
          rig.imports.find(row.id)?.rolledBackBy shouldBe "a moderator"
          rig.store.getCharacter(id).shouldBeNull()
        }
      }

      test("the player's own undo closes after a week; a moderator's does not") {
        runTest {
          val (rig, id) = rigWithCharacter(backgroundScope)
          rig.service.import(request(id, save()))
          clock = clock.plusDays(8)

          val refused = rig.service.undoLatest(id)
          refused.shouldBeInstanceOf<UndoOutcome.Refused>()
          refused.why shouldContain "moderator"

          val row = rig.imports.listFor(id, 5).single()
          rig.service.rollBack(row.id, "a moderator").shouldBeInstanceOf<UndoOutcome.Restored>()
          clock = clock.minusDays(8)
        }
      }

      test("a link party in progress answers try again and changes nothing") {
        runTest {
          val (rig, id) = rigWithCharacter(backgroundScope)
          val started = checkNotNull(rig.store.getCharacter(id)).info.money
          rig.links.create(id, "Probe", id + 1, "Peer")

          val outcome = rig.service.import(request(id, save(money = 900_000)))

          outcome.shouldBeInstanceOf<ImportOutcome.TryAgain>()
          outcome.why shouldContain "try again"
          checkNotNull(rig.store.getCharacter(id)).info.money shouldBe started
          rig.imports.listFor(id, 5).shouldBeEmpty()
        }
      }

      test("a save no playthrough could have written is refused and nothing is written down") {
        runTest {
          val (rig, id) = rigWithCharacter(backgroundScope)
          val tooMany = (0 until 7).map { monster(0x30000000 + it, slot = it) }

          val outcome = rig.service.import(request(id, save(monsters = tooMany)))

          outcome.shouldBeInstanceOf<ImportOutcome.Refused>()
          checkNotNull(rig.store.getCharacter(id)).pokemon.shouldBeEmpty()
          rig.imports.listFor(id, 5).shouldBeEmpty()
        }
      }

      test("a loop on the door runs out of its own allowance rather than filling a disk") {
        runTest {
          val (rig, id) = rigWithCharacter(backgroundScope)
          val limit = GrantBudget.Limits().importsGranted

          repeat(limit) {
            rig.service.import(request(id, save())).shouldBeInstanceOf<ImportOutcome.Landed>()
          }
          val refused = rig.service.import(request(id, save()))

          refused.shouldBeInstanceOf<ImportOutcome.Refused>()
          rig.imports.listFor(id, limit + 5).size shouldBe limit
        }
      }
    })

private const val TURTWIG = 387
private const val TACKLE = 33
private const val SUN_STONE = 5093
private const val TRAINER_ID = 0x00010002
private var counter = 0
